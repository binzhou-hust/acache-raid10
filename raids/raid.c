#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>

#include "acache.h"
#include "error.h"
#include "raid.h"
#include "raid10.h"
#include "raid6.h"

int connect_raid(void* pac, char* raid_level){
	int i;
	pstr_raid	pr=NULL;
	
	if(0==strcmp(raid_level,"raid10")){
		connect_raid10(pac);
		pr=((pstr_acache)pac)->rpers;
		pr->start_urio	=	0;
		pr->exit_thread	=	0;
		pr->page_size	=	64*1024;																											//64KiB
		pr->strip_size	=	512*1024;																										//512KiB
		pr->recovery_buffer_size = 10*1024*1024;																			//10MiB
		pr->disk_size	=	1L*10*1024*1024*1024;																				// 1TiB
		pr->recovery_start_point	=	1L*5*1024*1024*1024;														//0.5TiB
		pr->recovery_current_offset	=	pr->recovery_start_point;										//0.5TiB
		pr->recovery_end_point	=	pr->recovery_start_point+1L*1*1024*1024*1024;	//0.5TiB+5GiB
		pr->ratio_of_access_bad_data	=	0.3;																					//30% of requests to bad/spare disk induce degraded reads / reconstruction writes
		pr->fd_per_disk = 2;
		pr->thread_per_disk	=	2;																											//
		pr->raid_disks	=	8;																													//number of disks
		pr->spare_disk	=	0;																													//disk number of spare disk
		for(i=0;i<pr->raid_disks*pr->thread_per_disk;i++){											
			pr->disk_fd[i]	=	-1;
		}
		
		for(i=0;i<pr->raid_disks;i++)
			pr->disk_recovery_current_offset[i]	=	pr->recovery_start_point;

		for(i=0;i<pr->raid_disks;i++)
			memset(&pr->lc[i],0,STRLATCNTSIZE);
	}else{
		return ERR_INVALIDRAID_AC;
	}
	
	return ERR_SUCCESS_AC;
}
