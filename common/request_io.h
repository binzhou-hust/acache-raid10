#ifndef REQUEST_IO_H
#define REQUEST_IO_H

#include <libaio.h>
#include <time.h>
#include "dllist.h"

#define REQ_READ 0
#define REQ_WRITE 1
#define REQ_DESTAGE_READ 2

typedef struct user_request{
	double ts;
	long	dev;
	long	rw;						//read:0, write:1
	long	offset;
	long	length;
}str_user_request,*pstr_user_request,**ppstr_user_request;
#define STRUSERREQUESTSIZE sizeof(str_user_request)
#define PSTRUSERREQUESTSIZE sizeof(pstr_user_request)
#define PPSTRUSERREQUESTSIZE sizeof(ppstr_user_request)

typedef struct cache_request{
	long offset;
	long size;
}str_cache_request,*pstr_cache_request,**ppstr_cache_request;
#define STRCACHEREQUEST sizeof(str_cache_request)
#define PSTRCACHEREQUEST sizeof(pstr_cache_request)
#define PPSTRCACHEREQUEST sizeof(ppstr_cache_request)

#define IO_READ REQ_READ
#define IO_WRITE REQ_WRITE

typedef struct disk_io{
	long disk;																		//disk number
	long offset;																	
	long len;
	long rw;																			//read 0, write 1
	struct timeval requested_time;									//copy from the page requested time
	struct iocb aiocb;
	str_dlnode disk_node;
}str_disk_io,*pstr_disk_io,**ppstr_disk_io;
#define STRDISKIOSIZE sizeof(str_disk_io)
#define PSTRDISKIOSIZE sizeof(pstr_disk_io)
#define PPSTRDISKIOSIZE sizeof(ppstr_disk_io)

#define LATENCY_ARRAY_SIZE 10

#endif