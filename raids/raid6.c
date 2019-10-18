#define _GNU_SOURCE
#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<semaphore.h>
#include<fcntl.h>
#include<libaio.h>

#include "acache.h"
#include "error.h"
#include "request_io.h"
#include "dllist.h"
#include "raid.h"
#include "raid6.h"

#define MAXAIOPERDISK 20
#define DISKIOPERDEV 20
#define DISKNAMELEN 20

int init_raid6(pstr_raid pr);
int reclaim_raid6(pstr_raid pr);
int handle_uio_raid6(long disk,long offset,long length,long rw,int (*callback)());
int handle_cache_request_raid6(long page, long page_size, long rw, int (*callback)(), int (*callback2)(), struct timeval *prt);
int start_recovery_raid6(pstr_raid pr);
int io_to_page_raid6(long *page,long offset, long length, long disk, long page_size);
int page_to_disk_raid6(long page, long page_size, long *disk);
void raid6_state();
void* do_uio_callback_raid6(void *pta);
void* do_recovery_io_raid6(void *pta);

static struct raid raid6={
	.level	=	"raid6",
	.start_urio = 0,
	.exit_thread = 0,
	.page_size	=	1L*64*1024,
	.strip_size	=	1L*512*1024,
	.recovery_buffer_size = 1L*10*1024*1024,
	.disk_size	=	1L*1024*1024*1024*1024,
	.recovery_start_point= 1L*512*1024*1024*1024,
	.recovery_current_offset	=1L*512*1024*1024*1024,
	.recovery_end_point = 1L*512*1024*1024*1024+1L*50*1024*1024*1024,
	.ratio_of_access_bad_data = 0.3,
	.fd_per_disk	=	2,
	.thread_per_disk	=	2,
	.raid_disks	=	8,
	.spare_disk	= 0,
	.init_raid = init_raid6,
	.reclaim_raid = reclaim_raid6,
	.handle_cache_request = handle_cache_request_raid6,
	.io_to_page=io_to_page_raid6,
	.page_to_disk=page_to_disk_raid6,
	.start_recovery = start_recovery_raid6,
};

int init_raid6(pstr_raid pr){
	int rst=ERR_SUCCESS_AC;
	long i,j,k;
	void* tret;
	
	char disk_name[MAX_RAID_DISKS][DISKNAMELEN]={"/dev/sde","/dev/sdf",
		"/dev/sdg","/dev/sdh","/dev/sdi","/dev/sdj","/dev/sdk",
		"/dev/sdl","/dev/sdm","/dev/sdn","/dev/sdo","/dev/sdp",
		"/dev/sdq","/dev/sdr","/dev/sds","/dev/sdt"};
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
		printf("init_raid6 ERR_INVALIDPARA\n");
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
			pr->disk_fd[i*pr->raid_disks+j]=open(disk_name[j], O_RDWR| O_DIRECT);
			if(-1==pr->disk_fd[i*pr->raid_disks+j]){
				printf("init_raid6 ERROPENFILE\n");
				rst	=	ERR_OPENFILE_AC;
				goto ERROPENFILE;
			}
		}
	}

	//setup the disk aio context
	for(i=0;i<pr->raid_disks;i++){
		memset(&pr->disk_ctx[i],0,sizeof(io_context_t));
		if(0!=io_setup(MAXAIOPERDISK,&pr->disk_ctx[i])){
			printf("init_raid6 ERRSETUPCTX\n");
			rst	= ERR_SETUPCTX_AC;
			goto ERRSETUPCTX;
		}		
	}
	
	//user disk io buffer for each disk, page aligned for direct io
	for(i=0;i<pr->raid_disks;i++){
		rst=posix_memalign((void**)&pr->disk_buf[i], getpagesize(), 16*1024*1024);
		if(0!=rst){
			printf("init_raid6 ERRNOMEM2\n");
			rst	=	ERR_NOMEM_AC;		//now, all memory alloction failures are treated as err_nomem
			goto ERRNOMEM2;
		}
	}
	//printf("init_raid6 NOMEM2\n");
	
	//alloc disk io struct to free disk io list,
	//each disk has its own free disk io list to avoid heavy contention on list mutex
	for(i=0;i<pr->raid_disks;i++){
		pr->disk_ios[i]	=	(pstr_disk_io)malloc(MAXAIOPERDISK* STRDISKIOSIZE);
		if(NULL == pr->disk_ios[i]){
			printf("init_raid6 ERRNOMEM1\n");
			rst =	ERR_NOMEM_AC;
			goto ERRNOMEM1;
		}
	}
	//printf("init_raid6 NOMEM1\n");
	
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
			printf("init_raid6 ERRCREATMUTEX2\n");
			/*for(j=i-1;j>=0;j--){
				pthread_mutex_destroy(&pr->uio_mutex[j]);
			}*/
			rst=ERR_CREATEMUTEX_AC;
			goto ERRCREATMUTEX1;
		}
	}
	//printf("init_raid6 CREATMUTEX2\n");

	for(i=0;i<pr->raid_disks;i++){
		rst=pthread_cond_init(&pr->rio_cond[i],NULL);
		if(0	!=	rst){
			printf("init_raid6 ERRCREATECOND2\n");
			/*for(j=i-1;j>=0;j--){
				pthread_cond_destroy(&pr->rio_cond[j]);
			}*/
			rst=ERR_CREATECOND_AC;
			goto ERRCREATECOND2;
		}
	}
	//printf("init_raid6 CREATECOND3\n");
	
	for(i=0;i<pr->raid_disks;i++){
		rst=pthread_cond_init(&pr->uio_cond[i],NULL);
		if(0	!=	rst){
			printf("init_raid6 ERRCREATECOND1\n");
			for(j=i-1;j>=0;j--){
				pthread_cond_destroy(&pr->uio_cond[j]);
			}
			rst=ERR_CREATECOND_AC;
			goto ERRCREATECOND1;
		}
	}
	//printf("init_raid6 CREATECOND2\n");

	for(i=0;i<pr->raid_disks;i++){
		rst=pthread_cond_init(&pr->uio_finish_cond[i],NULL);
		if(0	!=	rst){
			printf("init_raid6 ERRCREATECOND0\n");
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
				do_uio_callback_raid6,(void*)&pr->ta[i]);
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
				do_recovery_io_raid6,(void*)&pr->ta[pr->raid_disks+i]);
		if(0 != rst){
			printf("init_raid6 ERRCREATETHREAD 1\n");
			rst = ERR_CREATETHREAD_AC;
			goto ERRCREATETHREAD;
		}
	}
	//printf("init_raid6 CREATETHREAD\n");	
	
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

int reclaim_raid6(pstr_raid pr){
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
	printf("reclaim_raid6, exit threads!\n");
	
	for(i=0;i<pr->raid_disks;i++){
		pthread_cond_destroy(&pr->uio_finish_cond[i]);
	}
	printf("reclaim_raid6, cond destroy!\n");
	
	for(i=0;i<pr->raid_disks;i++){
		pthread_cond_destroy(&pr->uio_cond[i]);
	}
	printf("reclaim_raid6, cond destroy!\n");
	
	for(i=0;i<pr->raid_disks;i++){
		pthread_cond_destroy(&pr->rio_cond[i]);
	}
	printf("reclaim_raid6, cond destroy!\n");
	
	for(i=0;i<pr->raid_disks;i++){
		pthread_mutex_destroy(&pr->uio_mutex[i]);
	}
	printf("reclaim_raid6, mutex destroy!\n");
	
	for(i=0;i<pr->raid_disks;i++){
		if(NULL!=pr->disk_ios[i]){
			free(pr->disk_ios[i]);
			pr->disk_ios[i]=NULL;
		}
	}
	printf("reclaim_raid6, memory free!\n");
	
	for(i=0;i<pr->raid_disks;i++){
		if(NULL!=pr->disk_buf[i]){
			free(pr->disk_buf[i]);
			pr->disk_buf[i]=NULL;
		}
	}
	printf("reclaim_raid6, memory free!\n");
	
	for(i=0;i<pr->raid_disks;i++){
		io_destroy(pr->disk_ctx[i]);
	}
	printf("reclaim_raid6, io destroy!\n");
	
	//close all opened disk files
	for(i=0;i<pr->raid_disks;i++){
		for(j=0;j<pr->thread_per_disk;j++){
			if(pr->disk_fd[i*pr->thread_per_disk+j]>=0){
				close(pr->disk_fd[i*pr->thread_per_disk+j]);
				pr->disk_fd[i*pr->thread_per_disk+j]	=	-1;
			}
		}
	}
	printf("reclaim_raid6, fd close!\n");
	
	return ERR_SUCCESS_AC;
}

int callback_do_nothing_raid6(long disk, long offset, long length){
	return ERR_SUCCESS_AC;
}

int handle_uio_raid6(long disk, long offset, long length, long rw, int (*callback)()){
	int rst;
	struct timeval now_time;
	struct timespec out_time;
	pstr_disk_io pdio=NULL;
	struct iocb *piocb=NULL;
	pstr_trace pt =	(pstr_trace)((pstr_acache)raid6.pacache)->tpers;
	
	//printf("handle_uio\n");
	pthread_mutex_lock(&raid6.uio_mutex[disk]);
	pt->real_disk_ios++;
	while(NULL==raid6.free_disk_io_list[disk].head){
		pthread_cond_signal(&raid6.uio_cond[disk]);//wsg, wake up the do_callback thread?
		gettimeofday(&now_time, NULL);
		ADD_TIME_MSEC(out_time,now_time,10);
		pthread_cond_timedwait(&raid6.uio_finish_cond[disk],&raid6.uio_mutex[disk],&out_time);
	}

	//printf("handle_uio2\n");

	//now we got a free disk io struct
	pdio=CONTAINER_OF(raid6.free_disk_io_list[disk].head, disk_node, str_disk_io);
	dl_list_remove_node(&raid6.free_disk_io_list[disk], &pdio->disk_node);
	piocb=&pdio->aiocb;
	if(IO_READ==rw)
		io_prep_pread(piocb, raid6.disk_fd[disk], raid6.disk_buf[disk], length, offset);
	else
		io_prep_pwrite(piocb, raid6.disk_fd[disk], raid6.disk_buf[disk], length, offset);
	//io_set_callback(piocb, callback);
	piocb->data=(void*)callback;
	//printf("handle_uio3 offset:%ld,length:%ld,disk:%ld\n",offset,length,disk);
	rst = io_submit(raid6.disk_ctx[disk], 1, &piocb);//not sure
	raid6.uio_cnt[disk]++;
	//printf("handle_uio4 %d\n",rst);
	//raid10_state();
	pthread_mutex_unlock(&raid6.uio_mutex[disk]);
	return ERR_SUCCESS_AC;
}


/*************************************************************
RAID6 layout:
	Disk0 Disk1 Disk2 Disk3 Disk4 Disk5
	   	0       1        2      3       p       q
	   	q       4        5      6       7       p
	   	p       q        8      9       10     11
		15     p        q      12     13      14
		18    19       p       q      16      17
	   21     22      23      p      q       20
**************************************************************/
int handle_cache_request_raid6(long page, long page_size, long rw, int (*callback)(), int (*callback2)(),struct timeval *prt){
	long i,disk, pdisk, qdisk, offset, length, strip;

	//calculate the disk, offset, and length of the request
	//printf("page:%ld,page_size:%ld,strip_size:%ld,raid_disks:%ld\n",page,page_size,raid6.strip_size,raid6.raid_disks);
	strip	=	page*page_size/raid6.strip_size;//since strip_size is N times of page_size
	disk		=	(strip%(raid6.raid_disks-2)
						+strip/(raid6.raid_disks-2)%(raid6.raid_disks-2))
					%raid6.raid_disks;
	pdisk	=	(raid6.raid_disks-2
						+strip/(raid6.raid_disks-2)%(raid6.raid_disks-2))
					%raid6.raid_disks;
	qdisk	=	(raid6.raid_disks-1
						+strip/(raid6.raid_disks-2)%(raid6.raid_disks-2))
					%raid6.raid_disks;
	offset	=	strip/(raid6.raid_disks-2)*raid6.strip_size
					+	page*page_size%raid6.strip_size;
	length	=	page_size;
	//printf("disk:%ld,mirror_disk:%ld,offset:%ld,length:%ld\n",disk,mirror_disk,offset,length);

	if(0==raid6.start_urio){
		//now, we do not need to perform user ios on the disk
		//instead, we respond that the ios are handled successfully
		//if(NULL!=callback)
			callback(offset,length,disk);
			//pthread_mutex_lock(&raid6.uio_mutex[disk]);
			//raid6.uio_cnt[disk]--;
			//pthread_mutex_lock(&raid6.uio_mutex[disk]);
		//else{
		//	callback_do_nothing_raid6(offset,length,disk);
		//}
		return ERR_SUCCESS_AC;
	}
	
	if(REQ_READ==rw){
		//printf("handle_cache_request_raid6 read,disk:%ld,offset:%ld,length:%ld\n",disk,offset,length);
		if(disk==raid6.spare_disk){
			//maybe we should perform a degraded rate
			//it is determined by the ratio of bad data on the spare disk
			for(i=raid6.raid_disks-1;i>1;i++){
				handle_uio_raid6((disk+i)%raid6.raid_disks,offset,length,IO_READ,callback_do_nothing_raid6);
			}
			handle_uio_raid6((disk+1)%raid6.raid_disks,offset,length,IO_READ,callback);
		}else{
			handle_uio_raid6(disk,offset,length,rw,callback);
		}
	}else if(REQ_DESTAGE_READ==rw){
		if(disk==raid6.spare_disk){
			//maybe we should perform a degraded rate
			//it is determined by the ratio of bad data on the spare disk
			for(i=raid6.raid_disks-1;i>1;i++){
				handle_uio_raid6((disk+i)%raid6.raid_disks,offset,length,IO_READ,callback_do_nothing_raid6);
			}
			handle_uio_raid6((disk+1)%raid6.raid_disks,offset,length,IO_READ,callback);
		}else{
			handle_uio_raid6(pdisk,offset,length,IO_READ,callback_do_nothing_raid6);
			handle_uio_raid6(qdisk,offset,length,IO_READ,callback_do_nothing_raid6);
			handle_uio_raid6(disk,offset,length,IO_READ,callback);
		}
	}else{
		//write a page
		//rmw
		//printf("handle_cache_request_raid6 write,disk:%ld,offset:%ld,length:%ld\n",disk,offset,length);
		handle_uio_raid6(disk,offset,length,IO_WRITE,callback);
		//printf("handle_cache_request_raid6 write,mirror_disk:%ld,offset:%ld,length:%ld\n",mirror_disk,offset,length);
		handle_uio_raid6(pdisk,offset,length,IO_WRITE,callback_do_nothing_raid6);
		handle_uio_raid6(qdisk,offset,length,IO_WRITE,callback_do_nothing_raid6);
	}
	
	return ERR_SUCCESS_AC;
}

int start_recovery_raid6(pstr_raid pr){
	pr->start_urio=1;
	return ERR_SUCCESS_AC;
}

void* do_uio_callback_raid6(void *pta){
	int i;
	long disk,rst,num,page,offset,length;
	int	(*pcb)(long,long,long);					//a pointer to a function
	pstr_disk_io pdi=NULL;
	struct iocb *pio=NULL;
	struct timeval now_time;
	struct timespec out_time;
	struct io_event ioe[MAXAIOPERDISK];
	
	disk	=	((str_thread_arg*)pta)->disk;

	while(!raid6.exit_thread){
		if(raid6.uio_cnt[disk]>0){
			//printf("do_uio_callback_raid10: to get u io\n");
			//executing user ios, do not schedule recovery io
			num=io_getevents(raid6.disk_ctx[disk],1,MAXAIOPERDISK,ioe,NULL);
			if(raid6.start_urio && 
					(raid6.uio_cnt[disk]==num))
				//all user ios have been finished, wake up the recovery thread
				pthread_cond_signal(&raid6.rio_cond[disk]);

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
					//printf("do_uio_callback_raid6,offset:%ld,length:%ld,disk:%ld\n",offset,length,disk);
					pcb(offset,length,disk);
				}
				
				pthread_mutex_lock(&raid6.uio_mutex[disk]);
				//put the io back to the free disk io list
				pdi=CONTAINER_OF(pio, aiocb, str_disk_io);
				dl_list_add_node_to_head(&raid6.free_disk_io_list[disk], &pdi->disk_node);
				raid6.uio_cnt[disk]--;
				//raid6_state();
				pthread_mutex_unlock(&raid6.uio_mutex[disk]);
				pthread_cond_signal(&raid6.uio_finish_cond[disk]);
				//pthread_cond_signal(&raid6.io_finish_cond);
			}
		}else{
			//no pending user ios, scheduling recovery io
			//printf("do_uio_callback_raid6: to wake up r io thread\n");
			pthread_mutex_lock(&raid6.rio_mutex[disk]);
			gettimeofday(&now_time, NULL);
			ADD_TIME_MSEC(out_time,now_time,10);
			if(raid6.start_urio)
				//at the begining, we do not need to start the user io
				pthread_cond_signal(&raid6.rio_cond[disk]);
			pthread_cond_timedwait(&raid6.uio_cond[disk],&raid6.rio_mutex[disk],&out_time);
			pthread_mutex_unlock(&raid6.rio_mutex[disk]);
		}
	}
	
	return;
}

#define RECOVERY_READ_SIZE (64*1024)
#define RECOVERY_WRITE_SIZE (4*1024*1024)

void* do_recovery_io_raid6(void *pta){
	int i;
	long disk,rst,num,page,rw_bytes,mirror_disk;
	struct timeval now_time;
	struct timespec out_time;
	
	disk	=	((str_thread_arg*)pta)->disk;
	mirror_disk	=	 (disk/2)*2+(1-disk%2);
	while(!raid6.exit_thread){
		if(raid6.start_urio){
			if(0==raid6.uio_cnt[disk]){
				//perform a recovery io
				if(disk==raid6.spare_disk){
					//write back
					long min_recovery_current_offset=raid6.disk_size;
					//find the minume recovery current offset
					for(i=0;i<raid6.raid_disks;i++){
						if(i!=(raid6.spare_disk-1)%raid6.raid_disks
								&& i!=raid6.spare_disk){
								//not the dnp and faulty disk
							if(raid6.disk_recovery_current_offset[i]<min_recovery_current_offset){
								min_recovery_current_offset	=	raid6.disk_recovery_current_offset[i];
							}
						}
					}
					
					if(	raid6.recovery_current_offset<raid6.recovery_end_point
							&& (min_recovery_current_offset-raid6.recovery_current_offset>=RECOVERY_WRITE_SIZE
								|| min_recovery_current_offset>=raid6.recovery_end_point)){
						//do recovery io
						lseek64(raid6.disk_fd[raid6.raid_disks+disk], raid6.disk_recovery_current_offset[disk], SEEK_SET);
						rw_bytes=write(raid6.disk_fd[raid6.raid_disks+disk],raid6.disk_buf[disk],RECOVERY_WRITE_SIZE);
						if(RECOVERY_WRITE_SIZE!=rw_bytes)
							printf("Rcvy write fails\n");
						raid6.disk_recovery_current_offset[disk]+=RECOVERY_WRITE_SIZE;
						raid6.recovery_current_offset+=RECOVERY_WRITE_SIZE;
						if(0==raid6.recovery_current_offset%(1L*1024*1024*1024)){
							printf("recovery_current_offset:%ld\n",raid6.recovery_current_offset);
						}
						if(raid6.recovery_current_offset>=raid6.recovery_end_point){
							//stop the program
							printf("recovery end\n");
							raid6.start_urio=0;
							gettimeofday(&((pstr_acache)raid6.pacache)->tpers->recovery_end_time,NULL);
						}
					}
				}else if(disk!=(raid6.spare_disk-1)%raid6.raid_disks){
					//read the surviving data
					if(	raid6.disk_recovery_current_offset[disk]<raid6.recovery_end_point
							&& raid6.disk_recovery_current_offset[disk]-raid6.recovery_buffer_size<raid6.recovery_current_offset){
						//do recovery read io
						lseek64(raid6.disk_fd[raid6.raid_disks+disk], raid6.disk_recovery_current_offset[disk], SEEK_SET);
						rw_bytes=read(raid6.disk_fd[raid6.raid_disks+disk],raid6.disk_buf[disk],RECOVERY_READ_SIZE);
						if(RECOVERY_READ_SIZE!=rw_bytes)
							printf("Rcvy read fails\n");
						raid6.disk_recovery_current_offset[disk]+=RECOVERY_READ_SIZE;	
						//printf("disk_recovery_current_offset:%ld\n",raid6.disk_recovery_current_offset[disk]);
					}
				}else{
					//do nothing
					usleep(10000);
				}
			}else{
				pthread_mutex_lock(&raid6.rio_mutex[disk]);
				while(0!=raid6.uio_cnt[disk] && !raid6.exit_thread){
					//if there are no pending user ios, wake up the recovery thread
					gettimeofday(&now_time, NULL);
					ADD_TIME_MSEC(out_time,now_time,10);
					pthread_cond_signal(&raid6.uio_cond[disk]);
					pthread_cond_timedwait(&raid6.rio_cond[disk],&raid6.rio_mutex[disk],&out_time);
				}
				pthread_mutex_unlock(&raid6.rio_mutex[disk]);
			}
		}else{
			sleep(1);
		}
	}

	return;
}

int io_to_page_raid6(long *page,long offset, long length, long disk, long page_size){
/*
	if(0	!=	offset%page_size ||	page_size	!=	length){
			printf("io error!");
			//may be we need retry this io
	}
	
	*page=((offset-offset%raid6.strip_size)*raid6.raid_disks/2+
					offset%raid6.strip_size+
					disk/2*raid6.strip_size)/page_size;*/
	return ERR_SUCCESS_AC;
}

int page_to_disk_raid6(long page, long page_size, long *disk){
	long strip;

	//calculate the disk, offset, and length of the request
	//printf("page:%ld,page_size:%ld,strip_size:%ld,raid_disks:%ld\n",page,page_size,raid6.strip_size,raid6.raid_disks);
	strip	=	page*page_size/raid6.strip_size;//since strip_size is N times of page_size
	*disk		=	(strip%(raid6.raid_disks-2)
						+strip/(raid6.raid_disks-2)%(raid6.raid_disks-2))
					%raid6.raid_disks;
	return ERR_SUCCESS_AC;
}

void raid6_state(){
	long i;
	printf("raid6 state\n");
	for(i=0;i<raid6.raid_disks;i++){
		printf("uio_cnt[%ld]:%ld\t",i,raid6.uio_cnt[i]);
	}
	printf("\n");
	return;
}

int connect_raid6(void* pac){
	((pstr_acache)pac)->rpers	=	(void*)&raid6;
	raid6.pacache=pac;
	return ERR_SUCCESS_AC;
}
