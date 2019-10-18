#ifndef TRACE_H
#define TRACE_H
#include<pthread.h>
#include<time.h>
#include "dllist.h"
#include "request_io.h"

#define TRACENAMELEN 10
#define TRACEPATHLEN 256
#define TRACEPLUSLEN 10

typedef struct trace{
	char name[TRACENAMELEN];
	char path[TRACEPATHLEN];
	char plus[TRACEPLUSLEN];
	void* pacache;
	long cnt;
	long warmup_lines;				// #lines
	long end_lines;						// total plays
	long	requested_pages;
	long	disk_ios;
	long real_disk_ios;
	double recovery_start_ts;		// time stamp of the trace
	double speedup_rate;
	struct timeval recovery_start_time;
	struct timeval recovery_end_time;
	pthread_mutex_t trace_mutex;
	FILE *fTrace;
	int (*open_trace_file)(struct trace *pt);
	int (*close_trace_file)(struct trace *pt);
	int (*get_record)(struct trace *pt,pstr_user_request pur);
	void (*print_record)(pstr_user_request pur);
}str_trace,*pstr_trace,**ppstr_trace;

#define STRTRACESIZE sizeof(str_trace)
#define PSTRTRACESIZE sizeof(pstr_trace)
#define PPSTRTRACESIZE sizeof(ppstr_trace)

int connect_trace(void* pac, char* trace_name, char* trace_plus);

#endif
