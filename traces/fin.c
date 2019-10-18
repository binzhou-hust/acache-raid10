#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<time.h>

#include "acache.h"
#include "error.h"
#include "dllist.h"
#include "request_io.h"
#include "trace.h"
#include "fin.h"

int open_fin_trace_file(pstr_trace pt){
	char path[256];
	sprintf(path,"%s%s",pt->path,pt->plus);
	pt->fTrace=fopen(path,"r");
	if(NULL	==	pt->fTrace)
	{
		printf("Error open file %s\n",path);
		return ERR_OPENFILE_AC;
	}	
	fseek(pt->fTrace,0,SEEK_SET);
	return ERR_SUCCESS_AC;
}

int close_fin_trace_file(pstr_trace pt){
	fclose(pt->fTrace);
	pt->fTrace=NULL;
	return ERR_SUCCESS_AC;
}

int get_fin_record(pstr_trace pt, pstr_user_request pur){
	char hostname[50];
	char rw;
	long response;
	//printf("get_web_record begin\n");
	//pthread_mutex_lock(&pt->trace_mutex);
	//printf("get_web_record locked\n");
	
	if(5!=fscanf(pt->fTrace,"%ld,%ld,%ld,%c,%lf\n",&pur->dev,&pur->offset,&pur->length,&rw,&pur->ts)){
		//printf("end of file\n");
		//pthread_mutex_unlock(&pt->trace_mutex);
		return ERR_READTRACERECORD_AC;
	}
	pur->offset=512*pur->offset+FIN_DEV_CAP*pur->dev;
	if(rw=='r' || rw=='R')
		pur->rw=REQ_READ;
	else
		pur->rw=REQ_WRITE;
	pur->ts*=1000000;

	pt->cnt++;
	//pt->cnt++;
	//printf("get_web_record warmup\n");
	if(pt->cnt==pt->warmup_lines){
	//if(1==pt->cnt){
		//usleep(10000);
		//from now on,
		//we play the trace in an open loop style, 
		//until the recovery is done
		gettimeofday(&pt->recovery_start_time,NULL);
		pt->recovery_start_ts	=	pur->ts;
		((pstr_acache)(pt->pacache))->rpers->start_urio=1;
		printf("recovery start, cnt:%ld\n",pt->cnt);
		//printf("get_web_record set\n");
	}

	if(pt->cnt==pt->end_lines){
		gettimeofday(&pt->recovery_end_time,NULL);
		((pstr_acache)(pt->pacache))->rpers->start_urio=0;
	}
	//pthread_mutex_unlock(&pt->trace_mutex);
	//printf("get_web_record end\n");
	return ERR_SUCCESS_AC;
}

void print_fin_user_request(pstr_user_request pur){
	printf("ts:%lf\t",pur->ts);
	printf("dev:%ld\t",pur->dev);
	printf("rw:%ld\t",pur->rw);
	printf("offset:%ld\t",pur->offset);
	printf("length:%ld\t",pur->length);
	printf("start page:%ld\t",pur->offset/(64*1024));
	printf("end page:%ld\n",(pur->offset+pur->length-1)/(64*1024));
	
}

static struct trace fin_trace =
{
	.name							= "spc-fin",
	.path							= "/trace/spc-fin/fin",
	.plus								= "",
	.cnt								=	0,
	.recovery_start_ts		=	0,
	.warmup_lines				=	200000,
	.speedup_rate				=	2.0,
	.fTrace							=	NULL,
	.open_trace_file			= open_fin_trace_file,
	.close_trace_file			=	close_fin_trace_file,
	.get_record					= get_fin_record,
	.print_record				= print_fin_user_request,
};

//global entrance
int connect_fin_trace(void* pac){
	((pstr_acache)pac)->tpers=(void*)&fin_trace;
	fin_trace.pacache=pac;
	return ERR_SUCCESS_AC;
}
