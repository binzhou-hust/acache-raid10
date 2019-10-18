#ifndef ACACHE_H
#define ACACHE_H


#include "trace.h"
#include "raid.h"
#include "algorithm.h"

//#define CONFIG_RAID10 1
//#define CONFIG_RAID6  0

/*something about the algorithm*/
/*
#define ACACHE_LRU 1

typedef struct alg_info{
	long alg_type;
	long page_size;
	long max_pages;
	long hash_size;
	double rcache_ratio;		//the fraction of read cache in the entire cache, that of write cache = 1-rcache_ratio
}str_ainfo,*pstr_ainfo,**pstr_ainfo;
#define STRAINFOSIZE sizeof(str_ainfo)
#define PSTRAINFOSIZE sizeof(pstr_ainfo)
#define PPSTRAINFOSIZE sizeof(ppstr_ainfo)
*/

/*something about the underline RAID*/
/*
#define NONELAYOUT 0
#define LEFT_ASYMMETRIC 1
#define LAYOUTCOUNT (LEFT_ASYMMETRIC+1)

typedef struct diskarray_info{
	int* dev_fd;				//file descriptors in the RAID, we always treat the first disks as spare disks
	long raid_levels;
	long devs;
	long strip_size;
	long layout;				//if it is a raid-6, we can have different layout; for a raid-0, we leave it with NONELAYOUT
}str_dainfo,*pstr_dainfo,**pstr_dainfo;
#define STRDAINFOSIZE sizeof(str_dainfo)
#define PSTRDAINFOSIZE sizeof(pstr_dainfo)
#define PPSTRDAINFOSIZE sizeof(ppstr_dainfo)
*/

/*run time information of acache*/
/*
typedef struct run_info{
	double dp_latency;			//the average user i/o latency on dp disks
	double dnp_latency; 		//the average user i/o latency on dnp disks
	double balance_factor;	//the balance_factor between dp_latency and dnp_latency
	double time_run;				//the time point to start the cache
	double time_fail;				//the time point to make one disk failed, 
													//before this time point we do not send the user I/Os to the disks, 
													//after this time point we start the recovery process
}str_rinfo,*pstr_rinfo,**pstr_rinfo;
#define STRPINFOSIZE sizeof(str_pinfo)
#define PSTRPINFOSIZE sizeof(pstr_pinfo)
#define PPSTRPINFOSIZE sizeof(ppstr_pinfo)
*/

/*statistics about the rst*/
/*
typedef struct cache_statistics{
	long		hits;
	long		misses;
	long		reqs;
}str_cst,*pstr_cst,**pstr_cst;
#define STRCSTSIZE sizeof(str_cst)
#define PSTRCSTSIZE sizeof(pstr_cst)
#define PPSTRCSTSIZE sizeof(pstr_cst)
*/

typedef struct ACache{
/*	pstr_tinfo tinfo;																			//trace info
	pstr_ainfo ainfo;																			//cache algorithm info
	pstr_dainfo dainfo;																		//disk array info
	
	pstr_rinfo rinfo;																			//run time info
	str_cst cst;																					//statistics
*/

	pstr_trace tpers;																			//trace handler
	pstr_algorithm apers;																//replacement algorithm handler
	pstr_raid rpers;																			//raid handler
	
/*
	void cpers;																						//cache function
	void rpers;																						//raid function
*/

	
/*	
	int (*alg_init)(pstr_ainfo ainfo, void *apers);				//alloc memory space
	int (*alg_reclaim)(void *apers);											//reclaim the memory space
	int (*alg_rpl)(pstr_req req, void *apers);						//replacement
	
	int (*raid_init)(pstr_dainfo dainfo, void *rpers);		//open file descriptors & req lists & create threads
	int (*raid_relaim)(void *rpers);											//close file descriptors & reclaim req lists & close threads
*/
}str_acache,*pstr_acache,**ppstr_acache;
#define STRACACHESIZE sizeof(str_acache)
#define PSTRACACHESIZE sizeof(pstr_acache)
#define PPSTRACACHESIZE sizeof(ppstr_acache)

#endif