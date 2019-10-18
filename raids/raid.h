#ifndef RAID_H
#define RAID_H

#include<pthread.h>
#include<time.h>
#include<libaio.h>
#include "dllist.h"
#include "request_io.h"

#define USER_IO 0
#define RECOVERY_READ_IO 1
#define RECOVERY_WRITE_IO 2
#define RECOVERY_NO_IO 3

typedef struct thread_arg{
	long disk;																		//disk number
	long thread_idx;															//thread idx help to access disk_fd
	long ur;																			//handle user io or recovery io, 
																								// 0 for user io, 
																								// 1 for recovery io read, 
																								// 2 for write, 
																								// and 3 for non-recovery-io
}str_thread_arg,*pstr_thread_arg,**ppstr_thread_arg;


typedef struct lat_cnt{
	long io_num;
	long total_lat;
	long avg_lat;
	long lat[LATENCY_ARRAY_SIZE];										//latency, in us(microseconds)
}str_lat_cnt,*pstr_lat_cnt,**ppstr_lat_cnt;
#define STRLATCNTSIZE sizeof(str_lat_cnt)

#define RAIDLEVELLEN 10
#define MAX_RAID_DISKS 16

//one for recovery io and another for user io
#define MAX_FDS_PER_DISK 2
#define MAX_THREADS_PER_DISK 2

typedef struct raid{
	char level[RAIDLEVELLEN];																				//10,6
	long start_urio;																								//!0 to start performing user and recovery io
	long exit_thread;																								//start_urio!=0 && exit_thread!=0 to exit thread
	long page_size;
	long strip_size;
	long recovery_buffer_size;
	long disk_size;																									//the size of a disk in the array
	long recovery_start_point;																			//the start point of recovery process
	long recovery_current_offset;
	long recovery_end_point;																				//end point should be not larger than disk size
	double ratio_of_access_bad_data;																//the ratio of degraded-read/reconstruction-write
																																//in all user ios to spare disk
	long fd_per_disk;
	long thread_per_disk;																						//thread_per_disk-1 threads for user io and 1 for recovery io
	long raid_disks;																								//number of working disks in the raid
	long spare_disk;																								//disk number of the spare disk
	long uio_cnt[MAX_RAID_DISKS];
	long disk_recovery_current_offset[MAX_RAID_DISKS];								//
	void* pacache;
	str_thread_arg ta[MAX_RAID_DISKS*MAX_THREADS_PER_DISK];					//
	int disk_fd[MAX_RAID_DISKS*MAX_FDS_PER_DISK];									//each disk thread has a file descriptor to read and write user data, using direct io
	str_lat_cnt lc[MAX_RAID_DISKS];
	io_context_t disk_ctx[MAX_RAID_DISKS];

	pthread_t disk_io_thread[MAX_RAID_DISKS*MAX_THREADS_PER_DISK];	//disk thread descriptors
	
	//sem_t io_sem[MAX_RAID_DISKS];																	//the semaphore to share handle disk io
	pthread_cond_t wcache_cond;																			//used to wake up cache 
	pthread_mutex_t wcache_mutex;
	pthread_cond_t io_finish_cond;																			//used to wake up cache 
	pthread_mutex_t rcache_mutex;	
	pthread_cond_t rio_cond[MAX_RAID_DISKS];
	pthread_mutex_t rio_mutex[MAX_RAID_DISKS];
	pthread_mutex_t sio_mutex[MAX_RAID_DISKS];
	
	pthread_cond_t uio_cond[MAX_RAID_DISKS];												//try to use pthread cond to efficiently handle disk io
	pthread_cond_t uio_finish_cond[MAX_RAID_DISKS];									//try to use pthread cond to efficiently handle disk io
	pstr_disk_io disk_ios[MAX_RAID_DISKS];														//disk io structs, easy to free
	str_dldsc free_disk_io_list[MAX_RAID_DISKS];											//to service a user io, 
																																//the cache should fetch at least a disk io struct from the free list
	pthread_mutex_t fio_mutex[MAX_RAID_DISKS];											//the mutex to access free disk io list
	//str_dldsc disk_io_list[MAX_RAID_DISKS];													//then insert it(them) into the right disk_io_list
	pthread_mutex_t uio_mutex[MAX_RAID_DISKS];											//the mutex to access disk io list
	char* disk_buf[MAX_RAID_DISKS];																	//each disk has a buf to read and write data, 
																																//we do not alloc a huge amount of memory buffer for the cache and io
	//init mutexs, open the files, create the threads, init the disk io list
	int (*init_raid)(struct raid *pr);

	//close the files, reclaim the disk io list, close all threads
	int (*reclaim_raid)(struct raid *pr);

	//handle a request from the cache
	int (*handle_cache_request)(long page,long page_size,long rw,int (*callback)(long offset,long length,long disk),int (*callback2)(long offset,long length,long disk),struct timeval *prt);

	//find the page by the disk io information: offset, length, disk, and layout
	int (*io_to_page)(long *page,long offset, long length, long disk, long page_size);	

	//find the disk by page id, according to the layout?
	int (*page_to_disk)(long page,long page_size, long* disk);
	
	//start the recovery process
	int (*start_recovery)(struct raid *pr);																
}str_raid,*pstr_raid,**ppstr_raid;
#define STRRAIDSIZE sizeof(str_raid)
#define PSTRRAIDSIZE sizeof(pstr_raid)
#define PPSTRRAIDSIZE sizeof(ppstr_raid)


int connect_raid(void* pac, char* raid_level);

#endif
