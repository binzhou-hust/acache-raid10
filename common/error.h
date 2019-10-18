#ifndef ERROR_H
#define ERROR_H

#include<time.h>

#define ERR_SUCCESS_AC 0
#define ERR_NOMEM_AC 1
#define ERR_OPENFILE_AC 2
#define ERR_READFILE_AC 3
#define ERR_WRITEFILE_AC 4
#define ERR_INVALIDPARA_AC 5
#define ERR_INVALIDTRACE_AC 6
#define ERR_INVALIDRAID_AC 7
#define ERR_INVALIDALGORITHM_AC 8
#define ERR_READTRACERECORD_AC 9
#define ERR_CREATEMUTEX_AC 10
#define ERR_CREATESEMA_AC 11
#define ERR_CREATECOND_AC 12
#define ERR_CREATETHREAD_AC 13
#define ERR_SETUPCTX_AC 14

#define ADD_TIME_MSEC(out, now, msec) do{	\
	(out).tv_nsec = ((now).tv_usec*1000uL + (2)*1000000uL)%1000000000uL; \
	(out).tv_sec	=	(now).tv_sec +	((now).tv_usec*1000uL + (2)*1000000uL)/1000000000uL; \
}while(0)

#endif