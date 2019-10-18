#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<pthread.h>
#include<time.h>
#include "error.h"
#include "trace.h"
#include "raid.h"
#include "acache.h"

void* play_trace(void* arg){
	long i,us_wait;
	pstr_trace pt =	((pstr_acache)arg)->tpers;
	pstr_raid pr = ((pstr_acache)arg)->rpers;
	pstr_algorithm	pa =	((pstr_acache)arg)->apers;
	str_user_request ur;
	struct timeval now_time;

	i=0;
	while(ERR_SUCCESS_AC==pt->get_record(pt,&ur)){
		//ur.offset=ur.offset*512;
		//ur.offset=ur.offset%(1L*1024*1024*1024);
		//pt->print_record(&ur);
		if(pr->start_urio){
			gettimeofday(&now_time, NULL);
			/*printf("ur.ts:%lf,pt->recovery_start_ts:%lf,-,%lf,%ld,%ld\n",
						ur.ts,pt->recovery_start_ts,ur.ts-pt->recovery_start_ts,
						1000000*(now_time.tv_sec-pt->recovery_start_time.tv_sec),
						(now_time.tv_usec-pt->recovery_start_time.tv_usec));*/
			us_wait=(ur.ts-pt->recovery_start_ts)/pt->speedup_rate
								-(1000000*(now_time.tv_sec-pt->recovery_start_time.tv_sec)
									+(now_time.tv_usec-pt->recovery_start_time.tv_usec));
			//printf("us_wait:%ld\n",us_wait);
			if(us_wait>0){
				usleep(us_wait);
			}
			//pt->print_record(&ur);
			pa->replace(pa,&ur);
		}else{
			//if(pt->cnt>pt->warmup_lines)
			//	pt->print_record(&ur);
			pa->replace(pa,&ur);
		}
		
		i++;
		//usleep(20000);
		if(0==i%10000){
			printf("%ld,\n",i);
		}
		//if(100==i)
		//	break;
	}
	printf("play_trace end\n");
	return NULL;
}

#define PLAY_THREADS 40

void set_para(pstr_acache pacache, long disks, long cache_size, double speed_up, long lines){
	pstr_algorithm pa = (pstr_algorithm)pacache->apers;
	pstr_trace pt = (pstr_trace)pacache->tpers;
	pstr_raid pr = (pstr_raid)pacache->rpers;

	//web2 r:189953 w:6 all:189959 dev_cap:20*1024*1024*1024
	//web3 remove bad line:#line 4261524 r:189967 w:8 all:189975 dev_cap:20*1024*1024*1024
	//fin1: remove lines:#line 4663812 4663813 4663814 4673484 r:25925 w:60017 all:85942 dev_cap:1*1024*1024*1024 
	//fin2: w:35732 r:22262 all:57994 dev_cap:1*1024*1024*1024
	//dtrs: w:2540175 r:1359270 all:3899445
	//lmtbe: w:1108709 r:987070 all:2095779 dev_cap:
	//dads: w:3711 r:69844 all:73555 dev_cap:
	//dap: low reaccesses w:591003 r:592311 dev_cap:
	if(0==strcmp(pt->name,"spc-web") && 0==strcmp(pt->plus,"2")){
		//pt->speedup_rate=1.0;
		pt->speedup_rate=speed_up;
		pa->pages 	=	1L*(189953+6)*cache_size/1000;
		pa->hash_size	=	pa->pages;
		pa->dpages	=	20;
		pa->destage_threadhold	=	18;
		//pt->warmup_lines=pa->pages;
		pt->warmup_lines=4579809/3;
		pt->end_lines=pt->warmup_lines+lines;
		if(pa->dpages < 20){
			pa->dpages	=	20;
			pa->destage_threadhold	=	18;
		}
	}else if(0==strcmp(pt->name,"spc-web") && 0==strcmp(pt->plus,"3")){
		//pt->speedup_rate=1.0;
		pt->speedup_rate=speed_up;
		pa->pages 	=	1L*(189967+8)*cache_size/1000;
		pa->hash_size	=	pa->pages;
		pa->dpages	=	20;
		pa->destage_threadhold	=	18;
		//pt->warmup_lines=pa->pages;
		pt->warmup_lines=4261708/3;
		pt->end_lines=pt->warmup_lines+lines;
		if(pa->dpages < 20){
			pa->dpages	=	20;
			pa->destage_threadhold	=	18;
		}
	}else if(0==strcmp(pt->name,"spc-fin") && 0==strcmp(pt->plus,"1")){
		//pt->speedup_rate=1.0;
		pt->speedup_rate=speed_up;
		pa->pages 	=	1L*(25925+60017)*cache_size/1000;
		pa->hash_size	=	pa->pages;
		pa->dpages	=	60017*cache_size/1000;
		pa->destage_threadhold	=	pa->dpages*9/10;
		//pt->warmup_lines=pa->pages;
		pt->warmup_lines=5334983/3;
		pt->end_lines=pt->warmup_lines+lines;
		if(pa->dpages < 20){
			pa->dpages	=	20;
			pa->destage_threadhold	=	18;
		}
	}else if(0==strcmp(pt->name,"spc-fin") && 0==strcmp(pt->plus,"2")){
		//pt->speedup_rate=1.0;
		pt->speedup_rate=speed_up;
		pa->pages 	=	1L*(35732+22262)*cache_size/1000;
		pa->hash_size	=	pa->pages;
		pa->dpages	=	22262*cache_size/1000;
		pa->destage_threadhold	=	pa->dpages*9/10;
		//pt->warmup_lines=pa->pages;
		pt->warmup_lines=3699194/3;
		pt->end_lines=pt->warmup_lines+lines;
		if(pa->dpages < 20){
			pa->dpages	=	20;
			pa->destage_threadhold	=	18;
		}
	}else if(0==strcmp(pt->name,"ms-dtrs") && 0==strcmp(pt->plus,"1")){
		//pt->speedup_rate=1.0;
		pt->speedup_rate=speed_up;
		pa->pages 	=	1L*(698047+2446829)*cache_size/1000;
		pa->hash_size	=	pa->pages;
		pa->dpages	=	2446829*cache_size/1000;
		pa->destage_threadhold	=	pa->dpages*9/10;
		//pt->warmup_lines=pa->pages;
		pt->warmup_lines=18146898/3;
		pt->end_lines=pt->warmup_lines+lines;
		if(pa->dpages < 20){
			pa->dpages	=	20;
			pa->destage_threadhold	=	18;
		}
	}else if(0==strcmp(pt->name,"cam-dads") && 0==strcmp(pt->plus,"1")){
		//pt->speedup_rate=1.0;
		pt->speedup_rate=speed_up;
		pa->pages 	=	1L*(69844+3711)*cache_size/1000;
		pa->hash_size	=	pa->pages;
		pa->dpages	=	3711*cache_size/1000;
		pa->destage_threadhold	=	pa->dpages*9/10;
		//pt->warmup_lines=pa->pages;
		pt->warmup_lines=1523821/3;
		pt->end_lines=pt->warmup_lines+lines;
		if(pa->dpages < 20){
			pa->dpages	=	20;
			pa->destage_threadhold	=	18;
		}
	}else{
		printf("error para\n");
	}
	pr->raid_disks=disks;
	return;
}

//argv[1] trace name
//argv[2] trace plus name
//argv[3] algorithm name
//argv[4] disks
//argv[5] cache_size
//argv[6] speed up
//argv[7] #lines to play
int main(int argc, char* argv[]){
	//demo
	int rst;
	long i,avg_lat=0,io_num=0,disks=0,cache_size=0,lines=0;
	double speed_up=0.0f;
	str_acache acache;
	str_user_request ur;
	pstr_trace pt=NULL;
	pstr_raid pr=NULL;
	pthread_t threads[PLAY_THREADS];
	void* tret;
	
	memset(&acache, 0, STRACACHESIZE);
	memset(&ur, 0, STRUSERREQUESTSIZE);
	//trace
	//if(ERR_SUCCESS_AC != connect_trace(&acache, "spc-web","2")){
	if(ERR_SUCCESS_AC != connect_trace(&acache, argv[1],argv[2])){
		printf("Connect trace error!\n");
		return 1;
	}else{
		//printf("Connect trace success!\n");
	}

	//raid
	if(ERR_SUCCESS_AC != connect_raid(&acache, "raid10")){
		printf("connect raid error!\n");
		return 1;
	}else{
		//printf("connect raid success!\n");
	}

	//alg
	//if(ERR_SUCCESS_AC != connect_algorithm(&acache,"lru")){
	if(ERR_SUCCESS_AC != connect_algorithm(&acache,argv[3])){
		printf("connect algorithm error!\n");
		return 1;
	}else{
		//printf("connect algorithm success!\n");
	}
	disks=atol(argv[4]);
	cache_size=atol(argv[5]);
	speed_up=atof(argv[6]);
	lines=atol(argv[7]);
	set_para(&acache,disks,cache_size,speed_up,lines);
	
	if(ERR_SUCCESS_AC != acache.rpers->init_raid(acache.rpers)){
		printf("Init raid error!\n");
		return 1;
	}else{
		//printf("Init raid success!\n");
	}
	
	if(ERR_SUCCESS_AC != acache.apers->init_algorithm(acache.apers)){
		printf("Init algorithm error!\n");
		return 1;
	}else{
		//printf("Init algorithm success!\n");
	}

	
	//acache.tpers && acache.tpers->open_trace_file && 
	if(ERR_SUCCESS_AC != acache.tpers->open_trace_file(acache.tpers)){
		printf("Open trace file error!\n");
		return 1;
	}else{
		//printf("Open trace file success!\n");
	}
	//acache.rpers->start_urio=1;
        play_trace(&acache);
/*	for(i=0;i<PLAY_THREADS;i++){
		rst = pthread_create(&threads[i],NULL, play_trace,&acache);
		if(0 != rst){
			printf("main ERRCREATETHREAD \n");
			exit(1);
		}
	}*/
/*	i=0;
	while(ERR_SUCCESS_AC==acache.tpers->get_record(acache.tpers,&ur)){
		//printf("%ld,",i);
		//acache.tpers->print_record(&ur);
		acache.apers->replace(acache.apers,&ur);
		i++;
		if(0==i%10000)
			printf("%ld,\n",i);
	} */
	
/*	for(i=0;i<PLAY_THREADS;i++){
		int rt=pthread_join(threads[i],&tret);
		if(0 != rt){
			printf("pthread_join error, %ld\n", i);
			return rt;
		}else{
			//printf("pthread_join success, %ld\n", i);
		}
	}*/
	//printf("pthread_join OK!\n");

	printf("%s,%s,%s,%s,%s,%s,%s\n",argv[1],argv[2],argv[3],argv[4],argv[5],argv[6],argv[7]);
	printf("request pages:%ld,disk_ios:%ld,real_disk_ios:%ld\n",
			((pstr_trace)acache.tpers)->requested_pages,
			((pstr_trace)acache.tpers)->disk_ios,
			((pstr_trace)acache.tpers)->real_disk_ios);
	pt=(pstr_trace)acache.tpers;
	printf("recovery time:%ld us\n",
		1000000*(pt->recovery_end_time.tv_sec
		-pt->recovery_start_time.tv_sec)
		+(pt->recovery_end_time.tv_usec
		-pt->recovery_start_time.tv_usec));
	pr=(pstr_raid)acache.rpers;
	for(i=0;i<pr->raid_disks;i++){
		if(0!=pr->lc[i].io_num)
			printf("disk:%ld, lat:%ld, io num:%ld\n",i,pr->lc[i].total_lat/pr->lc[i].io_num,pr->lc[i].io_num);
		avg_lat+=pr->lc[i].total_lat;
		io_num+=pr->lc[i].io_num;
	}
	if(0!=io_num){
		avg_lat=avg_lat/io_num;
		printf("avg_lat:%ld\n",avg_lat);
	}
	printf("total recovered :%ld\n",pr->recovery_current_offset-pr->recovery_start_point);
	printf("\n");
	//some ios may still not be finished
	//we sleep 5s here
	sleep(5);
	
	if(ERR_SUCCESS_AC != acache.tpers->close_trace_file(acache.tpers)){
		printf("Close trace file error!\n");
		return 1;
	}else{
		printf("Close trace file success!\n");
	}
	
	if(ERR_SUCCESS_AC != acache.apers->reclaim_algorithm(acache.apers)){
		printf("Reclaim algorithm error!\n");
		return 1;
	}else{
		printf("Reclaim algorithm success!\n");
	}
	
	if(ERR_SUCCESS_AC != acache.rpers->reclaim_raid(acache.rpers)){
		printf("Reclaim raid error!\n");
		return 1;
	}else{
		printf("Reclaim raid success!\n");
	}
	
	return 0;

}
