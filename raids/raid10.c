#define _GNU_SOURCE
#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<time.h>
#include<semaphore.h>
#include<fcntl.h>
#include<libaio.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>

#include "acache.h"
#include "error.h"
#include "request_io.h"
#include "dllist.h"
#include "raid.h"
#include "raid10.h"

#define MAXAIOPERDISK 20
#define DISKIOPERDEV 20
#define DISKNAMELEN 20

int init_raid10(pstr_raid pr);
int reclaim_raid10(pstr_raid pr);
int handle_uio_raid10(long disk,long offset,long length,long rw,int (*callback)(),struct timeval *prt);
int handle_cache_request_raid10(long page, long page_size, long rw, int (*callback)(),int (*callback2)(),struct timeval *prt);
int start_recovery_raid10(pstr_raid pr);
int io_to_page_raid10(long *page,long offset, long length, long disk, long page_size);
int page_to_disk_raid10(long page, long page_size, long *disk);
void raid10_state();
void* do_uio_callback_raid10(void *pta);
void* do_recovery_io_raid10(void *pta);

static struct raid raid10={
	.level	=	"raid10",
	.start_urio = 0,
	.exit_thread = 0,
	.page_size	=	1L*64*1024,
	.strip_size	=	1L*512*1024,
	.recovery_buffer_size = 1L*10*1024*1024,
	.disk_size	=	1L*10*1024*1024*1024,
	.recovery_start_point= 1L*5*1024*1024*1024,
	.recovery_current_offset	=1L*5*1024*1024*1024,
	.recovery_end_point = 1L*5*1024*1024*1024+1L*1*1024*1024*1024,
	.ratio_of_access_bad_data = 0.3,
	.fd_per_disk	=	2,
	.thread_per_disk	=	2,
	.raid_disks	=	8,
	.spare_disk	= 0,
	.init_raid = init_raid10,
	.reclaim_raid = reclaim_raid10,
	.handle_cache_request = handle_cache_request_raid10,
	.io_to_page=io_to_page_raid10,
	.page_to_disk=page_to_disk_raid10,
	.start_recovery = start_recovery_raid10,
};

int inline init_raid10(pstr_raid pr){
	int rst=ERR_SUCCESS_AC;
	long i,j,k;
	void* tret;
	
/*	char disk_name[MAX_RAID_DISKS][DISKNAMELEN]={"/dev/sdb",
		"/dev/sdd","/dev/sde","/dev/sdf","/dev/sdg","/dev/sdh",
		"/dev/sdi","/dev/sdj","/dev/sdk","/dev/sdl","/dev/sdm",
		"/dev/sdn","/dev/sdo","/dev/sdp","/dev/sdq","/dev/sdr"};
*/
        char disk_name[MAX_RAID_DISKS][DISKNAMELEN]={"/dev/sdb1",
		"/dev/sdb2","/dev/sdb5","/dev/sdb6","/dev/sdb7","/dev/sdb8",
		"/dev/sdb9","/dev/sdb10","/dev/sdb11","/dev/sdb12","/dev/sdb13",
		"/dev/sdb14","/dev/sdb15","/dev/sdb16","/dev/sdb17","/dev/sdb18"};        

	/*char disk_name[MAX_RAID_DISKS][DISKNAMELEN]={"/dev/sdc","/dev/sdd",
		"/dev/sde","/dev/sdf","/dev/sdg","/dev/sdh","/dev/sdi",
		"/dev/sdj","/dev/sdk","/dev/sdl","/dev/sdm","/dev/sdn",
		"/dev/sdo","/dev/sdp","/dev/sdq","/dev/sdr"};*/
	
	//we suppose that when we connected to raid10,
	//all number variants have been initialized
	//now check them
	if(	0L >= pr->page_size ||
			0L >= pr->strip_size ||
			0L >= pr->disk_size ||
			0L > pr->recovery_start_point ||
			0L >= pr->recovery_current_offset ||
			0L >= pr->recovery_end_point ||
			0 > pr->ratio_of_access_bad_data ||
			0L	>=	pr->fd_per_disk	||
			0L >= pr->thread_per_disk ||
			0L >= pr->raid_disks){
		printf("init_raid10 ERR_INVALIDPARA\n");
		return ERR_INVALIDPARA_AC;
	}

	//init fd, buf,
	for(i=0;i<pr->fd_per_disk;i++){
		for(j=0;j<pr->raid_disks;j++){
			pr->disk_fd[i*pr->raid_disks+j]=-1;
		}
		pr->disk_buf[i]	=	NULL;
		pr->disk_ios[i]	=	NULL;
	}

	//open all disk files, direct io mode          
	for(i=0;i<pr->fd_per_disk;i++){
		for(j=0;j<pr->raid_disks;j++){                        
			pr->disk_fd[i*pr->raid_disks+j]=open(disk_name[j], O_RDWR | O_DIRECT);
			if(-1==pr->disk_fd[i*pr->raid_disks+j]){
				printf("init_raid10 ERROPENFILE\n");
				rst	=	ERR_OPENFILE_AC;
				goto ERROPENFILE;
			}
		}
	}

	//setup the disk aio context
	for(i=0;i<pr->raid_disks;i++){
		memset(&pr->disk_ctx[i],0,sizeof(io_context_t));
		if(0!=io_setup(MAXAIOPERDISK,&pr->disk_ctx[i])){
			printf("init_raid10 ERRSETUPCTX\n");
			rst	= ERR_SETUPCTX_AC;
			goto ERRSETUPCTX;
		}		
	}
	
	//user disk io buffer for each disk, page aligned for direct io
	for(i=0;i<pr->raid_disks;i++){
		rst=posix_memalign((void**)&pr->disk_buf[i], getpagesize(), 16*1024*1024);
		if(0!=rst){
			printf("init_raid10 ERRNOMEM2\n");
			rst	=	ERR_NOMEM_AC;		//now, all memory alloction failures are treated as err_nomem
			goto ERRNOMEM2;
		}
	}
	//printf("init_raid10 NOMEM2\n");
	
	//alloc disk io struct to free disk io list,
	//each disk has its own free disk io list to avoid heavy contention on list mutex
	for(i=0;i<pr->raid_disks;i++){
		pr->disk_ios[i]	=	(pstr_disk_io)malloc(MAXAIOPERDISK* STRDISKIOSIZE);
		if(NULL == pr->disk_ios[i]){
			printf("init_raid10 ERRNOMEM1\n");
			rst =	ERR_NOMEM_AC;
			goto ERRNOMEM1;
		}
	}
	//printf("init_raid10 NOMEM1\n");
	
	//now init the free disk io list, by inserting all disk io structures into the lists
	for(i=0;i<pr->raid_disks;i++){
		pr->uio_cnt[i]=0;
		pr->free_disk_io_list[i].head=NULL;
//		pr->disk_io_list[i].head=NULL;
		for(j=0;j<MAXAIOPERDISK;j++){
			pr->disk_ios[i][j].disk_node.pre=NULL;
			pr->disk_ios[i][j].disk_node.next=NULL;
			dl_list_add_node_to_head(&pr->free_disk_io_list[i],&pr->disk_ios[i][j].disk_node);
		}
	}
	
	for(i=0;i<pr->raid_disks;i++){
		rst=pthread_mutex_init(&pr->uio_mutex[i],NULL);
		if(0	!=	rst){
			printf("init_raid10 ERRCREATMUTEX2\n");
			/*for(j=i-1;j>=0;j--){
				pthread_mutex_destroy(&pr->uio_mutex[j]);
			}*/
			rst=ERR_CREATEMUTEX_AC;
			goto ERRCREATMUTEX1;
		}
	}
	//printf("init_raid10 CREATMUTEX2\n");

	for(i=0;i<pr->raid_disks;i++){
		rst=pthread_mutex_init(&pr->sio_mutex[i],NULL);
		if(0	!=	rst){
			printf("init_raid10 ERRCREATMUTEX3\n");
			/*for(j=i-1;j>=0;j--){
				pthread_mutex_destroy(&pr->uio_mutex[j]);
			}*/
			rst=ERR_CREATEMUTEX_AC;
			goto ERRCREATMUTEX2;
		}
	}
	
	for(i=0;i<pr->raid_disks;i++){
		rst=pthread_cond_init(&pr->rio_cond[i],NULL);
		if(0	!=	rst){
			printf("init_raid10 ERRCREATECOND2\n");
			/*for(j=i-1;j>=0;j--){
				pthread_cond_destroy(&pr->rio_cond[j]);
			}*/
			rst=ERR_CREATECOND_AC;
			goto ERRCREATECOND2;
		}
	}
	//printf("init_raid10 CREATECOND3\n");
	
	for(i=0;i<pr->raid_disks;i++){
		rst=pthread_cond_init(&pr->uio_cond[i],NULL);
		if(0	!=	rst){
			printf("init_raid10 ERRCREATECOND1\n");
			for(j=i-1;j>=0;j--){
				pthread_cond_destroy(&pr->uio_cond[j]);
			}
			rst=ERR_CREATECOND_AC;
			goto ERRCREATECOND1;
		}
	}
	//printf("init_raid10 CREATECOND2\n");

	for(i=0;i<pr->raid_disks;i++){
		rst=pthread_cond_init(&pr->uio_finish_cond[i],NULL);
		if(0	!=	rst){
			printf("init_raid10 ERRCREATECOND0\n");
			for(j=i-1;j>=0;j--){
				pthread_cond_destroy(&pr->uio_finish_cond[j]);
			}
			rst=ERR_CREATECOND_AC;
			goto ERRCREATECOND0;
		}
	}
	
	//run user io callback handlers and recovery io threads
	for(i=0;i<pr->raid_disks;i++){
		pr->ta[i].disk=i;
		pr->ta[i].thread_idx=i;
		rst=pthread_create(&pr->disk_io_thread[i],NULL,
				do_uio_callback_raid10,(void*)&pr->ta[i]);
		if(0 != rst){
			printf("init_raid10 ERRCREATETHREAD 1\n");
			rst = ERR_CREATETHREAD_AC;
			goto ERRCREATETHREAD;
		}
	}

	for(i=0;i<pr->raid_disks;i++){
		pr->ta[pr->raid_disks+i].disk=i;
		pr->ta[pr->raid_disks+i].thread_idx=pr->raid_disks+i;
		rst=pthread_create(&pr->disk_io_thread[pr->raid_disks+i],NULL,
				do_recovery_io_raid10,(void*)&pr->ta[pr->raid_disks+i]);
		if(0 != rst){
			printf("init_raid10 ERRCREATETHREAD 1\n");
			rst = ERR_CREATETHREAD_AC;
			goto ERRCREATETHREAD;
		}
	}
	//printf("init_raid10 CREATETHREAD\n");	
	
	return ERR_SUCCESS_AC;
	
ERRCREATETHREAD:
	//we need to do something to terminate all threads
	pr->exit_thread = 1;
	pr->start_urio = 1;
	//sleep(5);//we do not know the exactly number of thread start here, may be i* thread_per_disk+j threads are successfully created

	for(i=0;i<pr->thread_per_disk*pr->raid_disks;i++){
		rst=pthread_join(pr->disk_io_thread[i], &tret);
		if(0	!=	rst){
			printf("pthread_join error, %ld\n", i);
			return rst;
		}
	}
ERRCREATECOND0:
	for(i=0;i<pr->raid_disks;i++){
		pthread_cond_destroy(&pr->uio_finish_cond[i]);
	}

ERRCREATECOND1:
	for(i=0;i<pr->raid_disks;i++){
		pthread_cond_destroy(&pr->uio_cond[i]);
	}

ERRCREATECOND2:
	for(i=0;i<pr->raid_disks;i++){
		pthread_cond_destroy(&pr->rio_cond[i]);
	}

ERRCREATMUTEX2:
	for(i=0;i<pr->raid_disks;i++){
		pthread_mutex_destroy(&pr->sio_mutex[i]);
	}
	
ERRCREATMUTEX1:
	for(i=0;i<pr->raid_disks;i++){
		pthread_mutex_destroy(&pr->uio_mutex[i]);
	}
	
ERRNOMEM1:
	for(i=0;i<pr->raid_disks;i++){
		if(NULL!=pr->disk_ios[i]){
			free(pr->disk_ios[i]);
			pr->disk_ios[i]=NULL;
		}
	}

ERRNOMEM2:
	for(i=0;i<pr->raid_disks;i++){
		if(NULL!=pr->disk_buf[i]){
			free(pr->disk_buf[i]);
			pr->disk_buf[i]=NULL;
		}
	}
ERRSETUPCTX:
	for(i=0;i<pr->raid_disks;i++){
		io_destroy(pr->disk_ctx[i]);
	}
	
ERROPENFILE:
	//close all opened disk files
	for(i=0;i<pr->raid_disks;i++){
		for(j=0;j<pr->thread_per_disk;j++){
			if(pr->disk_fd[i*pr->thread_per_disk+j]>=0){
				close(pr->disk_fd[i*pr->thread_per_disk+j]);
				pr->disk_fd[i*pr->thread_per_disk+j]	=	-1;
			}
		}
	}
	
	return rst;
}

int inline reclaim_raid10(pstr_raid pr){
	int rst;
	long i,j;
	void* tret;
	
	//we need to do something to terminate all threads
	pr->exit_thread = 1;
	//pr->start_urio = 1;
	
	for(i=0;i<pr->thread_per_disk*pr->raid_disks;i++){
		rst=pthread_join(pr->disk_io_thread[i], &tret);
		if(0	!=	rst){
			printf("pthread_join error, %ld\n", i);
			return rst;
		}
	}
	//printf("reclaim_raid10, exit threads!\n");
	
	for(i=0;i<pr->raid_disks;i++){
		pthread_cond_destroy(&pr->uio_finish_cond[i]);
	}
	//printf("reclaim_raid10, cond destroy!\n");
	
	for(i=0;i<pr->raid_disks;i++){
		pthread_cond_destroy(&pr->uio_cond[i]);
	}
	//printf("reclaim_raid10, cond destroy!\n");
	
	for(i=0;i<pr->raid_disks;i++){
		pthread_cond_destroy(&pr->rio_cond[i]);
	}
	//printf("reclaim_raid10, cond destroy!\n");

	for(i=0;i<pr->raid_disks;i++){
		pthread_mutex_destroy(&pr->sio_mutex[i]);
	}
	
	for(i=0;i<pr->raid_disks;i++){
		pthread_mutex_destroy(&pr->uio_mutex[i]);
	}
	//printf("reclaim_raid10, mutex destroy!\n");
	
	for(i=0;i<pr->raid_disks;i++){
		if(NULL!=pr->disk_ios[i]){
			free(pr->disk_ios[i]);
			pr->disk_ios[i]=NULL;
		}
	}
	//printf("reclaim_raid10, memory free!\n");
	
	for(i=0;i<pr->raid_disks;i++){
		if(NULL!=pr->disk_buf[i]){
			free(pr->disk_buf[i]);
			pr->disk_buf[i]=NULL;
		}
	}
	//printf("reclaim_raid10, memory free!\n");
	
	for(i=0;i<pr->raid_disks;i++){
		io_destroy(pr->disk_ctx[i]);
	}
	//printf("reclaim_raid10, io destroy!\n");
	
	//close all opened disk files
	for(i=0;i<pr->raid_disks;i++){
		for(j=0;j<pr->thread_per_disk;j++){
			if(pr->disk_fd[i*pr->thread_per_disk+j]>=0){
				close(pr->disk_fd[i*pr->thread_per_disk+j]);
				pr->disk_fd[i*pr->thread_per_disk+j]	=	-1;
			}
		}
	}
	//printf("reclaim_raid10, fd close!\n");
	
	return ERR_SUCCESS_AC;
}

/*
int inline callback_do_nothing_raid10(long disk, long offset, long length){
	pstr_algorithm pa =(pstr_raid)((pstr_acache)raid10.pacache)->apers;
	pstr_trace pt =	(pstr_trace)((pstr_acache)raid10.pacache)->tpers;
	pstr_alg_alru	pal=(pstr_alg_alru)pa->alg;

		pthread_mutex_lock(&pal->alru_mutex);
		//record the latency
		if(raid10.start_urio){
			int latency;
			long disk2;
			struct timeval now_time;

			pr->page_to_disk(page,alru.page_size,&disk2);
			gettimeofday(&now_time,NULL);
			latency=	1000000*(now_time.tv_sec-ppage->requested_time.tv_sec)
							+now_time.tv_usec-ppage->requested_time.tv_usec;
			//pr->lc[disk2].io_num++;
			pr->lc[disk2].avg_lat=	pr->lc[disk2].avg_lat*LATENCY_ARRAY_SIZE
													+latency-
													pr->lc[disk2].lat[pr->lc[disk2].io_num%LATENCY_ARRAY_SIZE];
			pr->lc[disk2].lat[pr->lc[disk2].io_num%LATENCY_ARRAY_SIZE]
												=	latency;
			pr->lc[disk2].io_num++;
		}
	return ERR_SUCCESS_AC;
}*/

int inline handle_uio_raid10(long disk, long offset, long length, long rw, int (*callback)(), struct timeval *prt){
	int rst;
	struct timeval now_time;
	struct timespec out_time;
	pstr_disk_io pdio=NULL;
	struct iocb *piocb=NULL;
	pstr_trace pt =	(pstr_trace)((pstr_acache)raid10.pacache)->tpers;
	
	//printf("handle_uio_raid10\n");
	pthread_mutex_lock(&raid10.uio_mutex[disk]);
	pt->real_disk_ios++;
	while(NULL==raid10.free_disk_io_list[disk].head){
		pthread_cond_signal(&raid10.uio_cond[disk]);//wsg, wake up the do_callback thread?
		gettimeofday(&now_time, NULL);
		ADD_TIME_MSEC(out_time,now_time,10);
		pthread_cond_timedwait(&raid10.uio_finish_cond[disk],&raid10.uio_mutex[disk],&out_time);
	}

	//printf("handle_uio_raid10 2\n");

	//now we got a free disk io struct
	pdio=CONTAINER_OF(raid10.free_disk_io_list[disk].head, disk_node, str_disk_io);
	dl_list_remove_node(&raid10.free_disk_io_list[disk], &pdio->disk_node);
	memcpy(&pdio->requested_time,prt,sizeof(struct timeval));
	piocb=&pdio->aiocb;
	if(IO_READ==rw)
		io_prep_pread(piocb, raid10.disk_fd[disk], raid10.disk_buf[disk], length, offset);
	else
		io_prep_pwrite(piocb, raid10.disk_fd[disk], raid10.disk_buf[disk], length, offset);
	//io_set_callback(piocb, callback);
	piocb->data=(void*)callback;
	//printf("handle_uio_raid10 3 offset:%ld,length:%ld,disk:%ld\n",offset,length,disk);
	raid10.uio_cnt[disk]++;
	rst = io_submit(raid10.disk_ctx[disk], 1, &piocb);//not sure
	//printf("handle_uio_raid10 4 %d\n",rst);
	//raid10_state();
	pthread_mutex_unlock(&raid10.uio_mutex[disk]);
	return ERR_SUCCESS_AC;
}


/*************************************************************
RAID10 layout:
	Disk0 Disk1 Disk2 Disk3 Disk4 Disk5
	   0       0        1      1       2       2
	   3       3        4      4       5       5
	   6       6        7      7       8       8
**************************************************************/
//callback2 is callback do nothing
int inline handle_cache_request_raid10(long page, long page_size, long rw, int (*callback)(),int (*callback2)(),struct timeval *prt){
	long disk, mirror_disk, offset, length, strip;
	struct timeval requested_time;

	//memcpy(&requested_time,prt,sizeof(struct timeval));
	gettimeofday(&requested_time,NULL);
	//calculate the disk, offset, and length of the request
	//printf("page:%ld,page_size:%ld,strip_size:%ld,raid_disks:%ld\n",page,page_size,raid10.strip_size,raid10.raid_disks);
	strip	=	page*page_size/raid10.strip_size*2;//since strip_size is N times of page_size
	disk	=	strip%raid10.raid_disks;
	mirror_disk	=	disk+1;
	offset	=	strip/raid10.raid_disks*raid10.strip_size
					+	page*page_size%raid10.strip_size;
	length	=	page_size;
	//printf("disk:%ld,mirror_disk:%ld,offset:%ld,length:%ld\n",disk,mirror_disk,offset,length);

	if(0==raid10.start_urio){
		//now, we do not need to perform user ios on the disk
		//instead, we respond that the ios are handled successfully
		//if(NULL!=callback)
		if(REQ_READ==rw || REQ_DESTAGE_READ==rw)
			callback(offset,length,disk);
		else{
			callback2(offset,length,disk);
			callback(offset,length,disk);
		}
			//pthread_mutex_lock(&raid10.uio_mutex[disk]);
			//raid10.uio_cnt[disk]--;
			//pthread_mutex_lock(&raid10.uio_mutex[disk]);
		//else{
		//	callback_do_nothing_raid10(offset,length,disk);
		//}
		return ERR_SUCCESS_AC;
	}
	
	if(REQ_READ==rw || REQ_DESTAGE_READ==rw){
		if(disk==raid10.spare_disk){
			if(0==page%3) //33% of the read ios are serviced by the spare disk (disk)
				handle_uio_raid10(disk,offset,length,IO_READ,callback,&requested_time);
			else
				handle_uio_raid10(mirror_disk,offset,length,IO_READ,callback,&requested_time);
		}else if(mirror_disk==raid10.spare_disk){
			if(0==page%3)	// 33% of the read ios are serviced by the spare disk (mirror_disk)
				handle_uio_raid10(mirror_disk,offset,length,IO_READ,callback,&requested_time);
			else
				handle_uio_raid10(disk,offset,length,IO_READ,callback,&requested_time);
		}else{
			if(0==page%2)	//0.5 to 0.5
				handle_uio_raid10(disk,offset,length,IO_READ,callback,&requested_time);
			else
				handle_uio_raid10(mirror_disk,offset,length,IO_READ,callback,&requested_time);
		}
		/*if(disk==raid10.spare_disk || mirror_disk==raid10.spare_disk){
			//recaculate the disk
			disk=disk;//or disk = mirror_disk
		}
		//printf("handle_cache_request_raid10 read,disk:%ld,offset:%ld,length:%ld\n",disk,offset,length);
		handle_uio_raid10(disk,offset,length,IO_READ,callback,&requested_time);*/
	}else{
		//write a page
		//printf("handle_cache_request_raid10 write,disk:%ld,offset:%ld,length:%ld\n",disk,offset,length);
		handle_uio_raid10(disk,offset,length,IO_WRITE,callback,&requested_time);
		//printf("handle_cache_request_raid10 write,mirror_disk:%ld,offset:%ld,length:%ld\n",mirror_disk,offset,length);
		handle_uio_raid10(mirror_disk,offset,length,IO_WRITE,callback2,&requested_time);
	}
	
	return ERR_SUCCESS_AC;
}

int inline start_recovery_raid10(pstr_raid pr){
	pr->start_urio=1;
	return ERR_SUCCESS_AC;
}

void* do_uio_callback_raid10(void *pta){
	int i,j;
	long disk,rst,num,page,offset,length;
	int	(*pcb)(long,long,long);					//a pointer to a function
	pstr_disk_io pdi=NULL;
	struct iocb *pio=NULL;
	struct timeval now_time;
	struct timespec out_time;
	struct io_event ioe[MAXAIOPERDISK];
	
	disk	=	((str_thread_arg*)pta)->disk;

	while(!raid10.exit_thread){
		if(raid10.uio_cnt[disk]>0){
			//printf("do_uio_callback_raid10: to get u io\n");
			//executing user ios, do not schedule recovery io
			num=io_getevents(raid10.disk_ctx[disk],1,MAXAIOPERDISK,ioe,NULL);
			if(raid10.start_urio && 
					(raid10.uio_cnt[disk]==num))
				//all user ios have been finished, wake up the recovery thread
				pthread_cond_signal(&raid10.rio_cond[disk]);

			//do the callback functions
			//the best way i can find to handle these events is:
			//lock cache
			//update all pages
			//perform all left things, such as write back after read, and so on
			//unlock cache
			//lock raid disk
			//put the iocbs back to the list
			//update the state of the disk: iocb_cnt, and so on
			//unlock the raid disk
			//tell the waiting cache requests: "you can go"

			//however, due to the time limitation
			//i use a lot of locks here
			for(i=0;i<num;i++){
				pcb = ioe[i].data;
				pio = ioe[i].obj;
				//printf("events[%d].data = %x, res = %d, res2 = %d\n", i, pcb, ioe[i].res, ioe[i].res2);
				if(NULL!=pcb){
					offset=pio->u.c.offset;
					length=pio->u.c.nbytes;
					//printf("do_uio_callback_raid10,offset:%ld,length:%ld,disk:%ld\n",offset,length,disk);
					pcb(offset,length,disk);
				}
				
				pthread_mutex_lock(&raid10.uio_mutex[disk]);
				//put the io back to the free disk io list
				pdi=CONTAINER_OF(pio, aiocb, str_disk_io);

				if(raid10.start_urio){
					long latency,disk2=disk/2,new_avg_lat;
					struct timeval now_time;

					pthread_mutex_lock(&raid10.sio_mutex[disk/2]);
					gettimeofday(&now_time,NULL);
					latency=	1000000*(now_time.tv_sec-pdi->requested_time.tv_sec)
									+now_time.tv_usec-pdi->requested_time.tv_usec;
					if(latency<0)
						printf("error latency\n");
					//pr->lc[disk2].io_num++;
					/*printf("latency:%ld,disk:%ld,avg_lat:%ld,io_num:%ld,old_lat:%ld\n",
						latency,disk2,raid10.lc[disk2].avg_lat,raid10.lc[disk2].io_num,
						raid10.lc[disk2].lat[raid10.lc[disk2].io_num%LATENCY_ARRAY_SIZE]);*/
					new_avg_lat=(raid10.lc[disk2].avg_lat
															+latency-
															raid10.lc[disk2].lat[raid10.lc[disk2].io_num%LATENCY_ARRAY_SIZE]);
					/*new_avg_lat=(raid10.lc[disk2].avg_lat*LATENCY_ARRAY_SIZE
															+latency-
															raid10.lc[disk2].lat[raid10.lc[disk2].io_num%LATENCY_ARRAY_SIZE])
															/LATENCY_ARRAY_SIZE;*/
					raid10.lc[disk2].avg_lat=new_avg_lat;
					raid10.lc[disk2].lat[raid10.lc[disk2].io_num%LATENCY_ARRAY_SIZE]
														=	latency;
					raid10.lc[disk2].io_num++;
					raid10.lc[disk2].total_lat+=latency;
					pthread_mutex_unlock(&raid10.sio_mutex[disk/2]);
				}
								
				dl_list_add_node_to_head(&raid10.free_disk_io_list[disk], &pdi->disk_node);
				raid10.uio_cnt[disk]--;
				//raid10_state();
				pthread_mutex_unlock(&raid10.uio_mutex[disk]);
				pthread_cond_signal(&raid10.uio_finish_cond[disk]);
				//pthread_cond_signal(&raid10.io_finish_cond);
			}
		}else{
			//no pending user ios, scheduling recovery io
			//printf("do_uio_callback_raid10: to wake up r io thread\n");
			pthread_mutex_lock(&raid10.rio_mutex[disk]);
			gettimeofday(&now_time, NULL);
			ADD_TIME_MSEC(out_time,now_time,10);
			if(raid10.start_urio)
				//at the begining, we do not need to start the user io
				pthread_cond_signal(&raid10.rio_cond[disk]);
			pthread_cond_timedwait(&raid10.uio_cond[disk],&raid10.rio_mutex[disk],&out_time);
			pthread_mutex_unlock(&raid10.rio_mutex[disk]);
		}
	}
	
	return;
}

#define RECOVERY_READ_SIZE (64*1024)
#define RECOVERY_WRITE_SIZE (4*1024*1024)

void* do_recovery_io_raid10(void *pta){
	int i;
	long j,disk,rst,num,page,rw_bytes,mirror_disk;
	pstr_trace pt =	(pstr_trace)((pstr_acache)raid10.pacache)->tpers;
	struct timeval now_time;
	struct timespec out_time;
	
	disk	=	((str_thread_arg*)pta)->disk;
	mirror_disk	=	 (disk/2)*2+(1-disk%2);
	while(!raid10.exit_thread){
		if(raid10.start_urio){
			if(0==raid10.uio_cnt[disk]){
				//perform a recovery io
				if(disk==raid10.spare_disk){
					//write back
					if(	raid10.recovery_current_offset<raid10.recovery_end_point
							&& (raid10.disk_recovery_current_offset[mirror_disk]-raid10.recovery_current_offset>=RECOVERY_WRITE_SIZE
								|| raid10.disk_recovery_current_offset[mirror_disk]>=raid10.recovery_end_point)){
						//do recovery io
						lseek64(raid10.disk_fd[raid10.raid_disks+disk], raid10.disk_recovery_current_offset[disk], SEEK_SET);
						rw_bytes=write(raid10.disk_fd[raid10.raid_disks+disk],raid10.disk_buf[disk],RECOVERY_WRITE_SIZE);
						if(RECOVERY_WRITE_SIZE!=rw_bytes)
							printf("Rcvy write fails\n");
						raid10.disk_recovery_current_offset[disk]+=RECOVERY_WRITE_SIZE;
						raid10.recovery_current_offset+=RECOVERY_WRITE_SIZE;
						if(0==raid10.recovery_current_offset%(1L*1024*1024*1024)){
							printf("recovery_current_offset:%ld\n",raid10.recovery_current_offset);
							printf("pt->cnt:%ld\n",pt->cnt);
							for(j=0;j<raid10.raid_disks;j++){
								printf("disk: %ld,\tavg lat: %ld,\tio num: %ld\t\n",j,raid10.lc[j].avg_lat,raid10.lc[j].io_num);
							}
						}
						if(raid10.recovery_current_offset>=raid10.recovery_end_point){
							//stop the program
							printf("recovery end\n");
							raid10.start_urio=0;
							gettimeofday(&((pstr_acache)raid10.pacache)->tpers->recovery_end_time,NULL);
						}
					}
				}else if(disk/2==raid10.spare_disk/2){
					//read the surviving data
					if(	raid10.disk_recovery_current_offset[disk]<raid10.recovery_end_point
							&& raid10.disk_recovery_current_offset[disk]-raid10.recovery_buffer_size<raid10.recovery_current_offset){
						//do recovery read io
						lseek64(raid10.disk_fd[raid10.raid_disks+disk], raid10.disk_recovery_current_offset[disk], SEEK_SET);
						rw_bytes=read(raid10.disk_fd[raid10.raid_disks+disk],raid10.disk_buf[disk],RECOVERY_READ_SIZE);
						if(RECOVERY_READ_SIZE!=rw_bytes)
							printf("Rcvy read fails\n");
						raid10.disk_recovery_current_offset[disk]+=RECOVERY_READ_SIZE;
						//printf("disk_recovery_current_offset:%ld\n",raid10.disk_recovery_current_offset[disk]);
					}
				}else{
					//do nothing
					sleep(1);
				}
			}else{
				pthread_mutex_lock(&raid10.rio_mutex[disk]);
				while(0!=raid10.uio_cnt[disk] && !raid10.exit_thread){
					//if there are no pending user ios, wake up the recovery thread
					gettimeofday(&now_time, NULL);
					ADD_TIME_MSEC(out_time,now_time,10);
					pthread_cond_signal(&raid10.uio_cond[disk]);
					pthread_cond_timedwait(&raid10.rio_cond[disk],&raid10.rio_mutex[disk],&out_time);
				}
				pthread_mutex_unlock(&raid10.rio_mutex[disk]);
			}
		}else{
			sleep(1);
		}
	}

	return;
}

int inline io_to_page_raid10(long *page,long offset, long length, long disk, long page_size){
	if(0	!=	offset%page_size ||	page_size	!=	length){
			printf("io error!");
			//may be we need retry this io
	}
	
	*page=((offset-offset%raid10.strip_size)*raid10.raid_disks/2+
					offset%raid10.strip_size+
					disk/2*raid10.strip_size)/page_size;
	return ERR_SUCCESS_AC;
}

int inline page_to_disk_raid10(long page, long page_size, long *disk){
	long strip;

	//calculate the disk, offset, and length of the request
	//printf("page:%ld,page_size:%ld,strip_size:%ld,raid_disks:%ld\n",page,page_size,raid10.strip_size,raid10.raid_disks);
	strip	=	page*page_size/raid10.strip_size*2;//since strip_size is N times of page_size
	*disk	=	(strip%raid10.raid_disks)/2;
	return ERR_SUCCESS_AC;
}

void raid10_state(){
	long i;
	printf("raid10 state\n");
	for(i=0;i<raid10.raid_disks;i++){
		printf("uio_cnt[%ld]:%ld\t",i,raid10.uio_cnt[i]);
	}
	printf("\n");
	return;
}

//besides the sync_io, we can try async_io, such as libaio, to handle the disk ios, 
//so that we do not need to create a lot of threads
/*void* do_user_disk_io(void *pta){
	long disk,thread_idx,ur,rw_bytes,rst;
	pstr_disk_io pdi=NULL;
	
	disk	=	((str_thread_arg*)pta)->disk;
	thread_idx	=	((str_thread_arg*)pta)->thread_idx;
	ur	=	((str_thread_arg*)pta)->ur;
	
	//do user io only, can we access the static struct raid10?
	while(1){
		if(raid10.start_urio){
			break;
		}
		sleep(1);
	}
	
	printf("thread start performing user disk io, thread id:%ld, disk:%ld, thread type:%ld, \n",thread_idx,disk,ur);
	
	//fetch a user disk io from the disk user io list
	while(!raid10.exit_thread){
		pthread_mutex_lock(&raid10.uio_mutex[disk]);
		while(NULL == raid10.disk_io_list[disk].head && !raid10.exit_thread){
			//wake up the recovery thread
			pthread_cond_signal(&raid10.rio_cond[disk]);
			pthread_cond_wait(&raid10.uio_cond[disk],&raid10.uio_mutex[disk]);
		}
		
		if(raid10.exit_thread)
			break;

		//remove the last one from the list, since we insert the lastest disk io to the head of the list				
		pdi=CONTAINER_OF((raid10.disk_io_list[disk].head)->pre,disk_node,str_disk_io);
		dl_list_remove_node(&raid10.disk_io_list[disk],&pdi->disk_node);
		pthread_mutex_unlock(&raid10.uio_mutex[disk]);

		//issue the disk io
		if(IO_READ==pdi->rw){
			lseek64(raid10.disk_fd[thread_idx], pdi->offset, SEEK_SET);
			rw_bytes=read(raid10.disk_fd[thread_idx], raid10.disk_buf[disk], pdi->len);
			if(pdi->len!=rw_bytes){
				printf("Read fails,disk:%ld\n",disk);
				//exit(0);
			}else{
				//printf("Read success,disk:%ld\n",disk);
			}
		}else{
			lseek64(raid10.disk_fd[thread_idx], pdi->offset, SEEK_SET);
			rw_bytes=write(raid10.disk_fd[thread_idx], raid10.disk_buf[disk], pdi->len);
			if(pdi->len!=rw_bytes)
				printf("Write fails,disk:%ld\n",disk);
		}
		
		//if we finished the disk io, we need to check if we have finished a user request
		
		//then put the disk io struct to the free list
		pthread_mutex_lock(&raid10.fio_mutex[disk]);
		dl_list_add_node_to_head(&raid10.free_disk_io_list[disk],&pdi->disk_node);
		pthread_mutex_unlock(&raid10.fio_mutex[disk]);
	}
	
	printf("exit thread performing user disk io, thread id:%ld, disk:%ld, thread type:%ld, \n",thread_idx,disk,ur);
	return;
	
}*/

/*
void* do_recovery_disk_io(void *pta){
	long disk,thread_idx,ur,mirror_disk,rw_bytes;
	disk	=	((str_thread_arg*)pta)->disk;
	thread_idx	=	((str_thread_arg*)pta)->thread_idx;
	ur	=	((str_thread_arg*)pta)->ur;
	mirror_disk = (disk/2)*2+(1-disk%2);
	
	//do recovery io only
	while(1){
		if(raid10.start_urio){
			break;
		}
		sleep(1);
	}
	
	printf("thread start performing recovery disk io, thread id:%ld, disk:%ld, thread type:%ld, \n",thread_idx,disk,ur);
	
	while(!raid10.exit_thread){
		if(RECOVERY_READ_IO==ur){
			//performing recovery read
			pthread_mutex_lock(&raid10.rio_mutex[disk]);
			while(NULL != raid10.disk_io_list[disk].head && !raid10.exit_thread){
				//wake up the uio thread
				pthread_cond_signal(&raid10.uio_cond[disk]);
				pthread_cond_wait(&raid10.rio_cond[disk],&raid10.rio_mutex[disk]);
			}
			
			pthread_mutex_unlock(&raid10.rio_mutex[disk]);
			
			if(raid10.exit_thread)
				break;
				
			while(NULL == raid10.disk_io_list[disk].head
							&& raid10.disk_recovery_current_offset[disk]<raid10.recovery_end_point
							&& raid10.disk_recovery_current_offset[disk]-raid10.recovery_buffer_size<raid10.recovery_current_offset
							&&	!raid10.exit_thread){
				//do recovery io
				lseek64(raid10.disk_fd[thread_idx], raid10.disk_recovery_current_offset[disk], SEEK_SET);
				rw_bytes=read(raid10.disk_fd[thread_idx],raid10.disk_buf[disk],64*1024);
				if(64*1024!=rw_bytes)
					printf("Rcvy read fails\n");
				raid10.disk_recovery_current_offset[disk]+=64*1024;		
			}
		}else if(RECOVERY_WRITE_IO==ur){
			//performing recovery write
			pthread_mutex_lock(&raid10.rio_mutex[disk]);
			while(NULL != raid10.disk_io_list[disk].head && !raid10.exit_thread){
				//wake up the uio thread
				pthread_cond_signal(&raid10.uio_cond[disk]);
				pthread_cond_wait(&raid10.rio_cond[disk],&raid10.rio_mutex[disk]);
			}
			
			pthread_mutex_unlock(&raid10.rio_mutex[disk]);
			
			if(raid10.exit_thread)
				break;

			while(	NULL == raid10.disk_io_list[disk].head
							&&	raid10.recovery_current_offset<raid10.recovery_end_point
							&& (raid10.disk_recovery_current_offset[mirror_disk]-raid10.recovery_current_offset>=1*1024*1024
								|| raid10.disk_recovery_current_offset[mirror_disk]>=raid10.recovery_end_point)
							&&	!raid10.exit_thread){
				//do recovery io
				lseek64(raid10.disk_fd[thread_idx], raid10.disk_recovery_current_offset[disk], SEEK_SET);
				rw_bytes=write(raid10.disk_fd[thread_idx],raid10.disk_buf[disk],1*1024*1024);
				if(1*1024*1024!=rw_bytes)
					printf("Rcvy write fails\n");
				raid10.disk_recovery_current_offset[disk]+=1*1024*1024;
				raid10.recovery_current_offset+=1*1024*1024;
			}
			//the cache should check if the recovery is done
		}else{
			sleep(1);
		}
	}	
	
	printf("exit thread performing recovery disk io, thread id:%ld, disk:%ld, thread type:%ld, \n",thread_idx,disk,ur);
	return;
}*/

int inline connect_raid10(void* pac){
	((pstr_acache)pac)->rpers	=	(void*)&raid10;
	raid10.pacache=pac;
	return ERR_SUCCESS_AC;
}
