#include<stdio.h>
#include<string.h>

#include	"acache.h"
#include "error.h"
#include "trace.h"
#include "web.h"
#include "fin.h"
#include "lmtbe.h"
#include "dtrs.h"
#include "tpce.h"

int connect_trace(void* pac, char* trace_name, char* trace_plus){
	int rst;
	pstr_trace pt=NULL;
	//check if trace name is null character string
	if(strlen(trace_name)<1 || strlen(trace_plus)<1){
		printf("%ld,%ld\n",strlen(trace_name),strlen(trace_plus));
		return ERR_INVALIDTRACE_AC;
	}
	//printf("%s,%s\n",trace_name,trace_plus);
	if(0==strcmp(trace_name,"spc-web")){
		connect_web_trace(pac);
		pt=((pstr_acache)pac)->tpers;
		sprintf(pt->plus,"%s",trace_plus);
		pt->cnt=0;
		pt->warmup_lines=100000;
		pt->recovery_start_ts=0;
		pt->speedup_rate=4.0;
		pt->disk_ios=0;
		pt->real_disk_ios=0;
		pt->requested_pages=0;
		rst=pthread_mutex_init(&pt->trace_mutex,NULL);
		if(0!=rst){
			return ERR_CREATEMUTEX_AC;
		}
	}else if(0==strcmp(trace_name,"spc-fin")){
		connect_fin_trace(pac);
		pt=((pstr_acache)pac)->tpers;
		sprintf(pt->plus,"%s",trace_plus);
		pt->cnt=0;
		pt->warmup_lines=100000;
		pt->recovery_start_ts=0;
		pt->speedup_rate=4.0;
		pt->disk_ios=0;
		pt->real_disk_ios=0;
		pt->requested_pages=0;
		rst=pthread_mutex_init(&pt->trace_mutex,NULL);
		if(0!=rst){
			return ERR_CREATEMUTEX_AC;
		}
	}else if(0==strcmp(trace_name,"ms-lmtbe")){
		connect_lmtbe_trace(pac);
		pt=((pstr_acache)pac)->tpers;
		sprintf(pt->plus,"%s",trace_plus);
		pt->cnt=0;
		pt->warmup_lines=100000;
		pt->recovery_start_ts=0;
		pt->speedup_rate=4.0;
		pt->disk_ios=0;
		pt->real_disk_ios=0;
		pt->requested_pages=0;
		rst=pthread_mutex_init(&pt->trace_mutex,NULL);
		if(0!=rst){
			return ERR_CREATEMUTEX_AC;
		}
	}else if(0==strcmp(trace_name,"ms-dtrs")){
		connect_dtrs_trace(pac);
		pt=((pstr_acache)pac)->tpers;
		sprintf(pt->plus,"%s",trace_plus);
		pt->cnt=0;
		pt->warmup_lines=100000;
		pt->recovery_start_ts=0;
		pt->speedup_rate=4.0;
		pt->disk_ios=0;
		pt->real_disk_ios=0;
		pt->requested_pages=0;
		rst=pthread_mutex_init(&pt->trace_mutex,NULL);
		if(0!=rst){
			return ERR_CREATEMUTEX_AC;
		}
	}else if(0==strcmp(trace_name,"tpce")){
		connect_tpce_trace(pac);
		pt=((pstr_acache)pac)->tpers;
		sprintf(pt->plus,"%s",trace_plus);
		pt->cnt=0;
		pt->warmup_lines=100000;
		pt->recovery_start_ts=0;
		pt->speedup_rate=4.0;
		pt->disk_ios=0;
		pt->real_disk_ios=0;
		pt->requested_pages=0;
		rst=pthread_mutex_init(&pt->trace_mutex,NULL);
		if(0!=rst){
			return ERR_CREATEMUTEX_AC;
		}
	}else if(0==strcmp(trace_name,"cam-dads")){
		connect_dads_trace(pac);
		pt=((pstr_acache)pac)->tpers;
		sprintf(pt->plus,"%s",trace_plus);
		pt->cnt=0;
		pt->warmup_lines=100000;
		pt->recovery_start_ts=0;
		pt->speedup_rate=4.0;
		pt->disk_ios=0;
		pt->real_disk_ios=0;
		pt->requested_pages=0;
		rst=pthread_mutex_init(&pt->trace_mutex,NULL);
		if(0!=rst){
			return ERR_CREATEMUTEX_AC;
		}
	}else{
		return ERR_INVALIDTRACE_AC;
	}
	
	return ERR_SUCCESS_AC;
}
