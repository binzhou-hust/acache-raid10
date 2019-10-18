#ifndef ALRU_H
#define ALRU_H

#include<pthread.h>
#include<time.h>

#include "dllist.h"
#include "algorithm.h"
#include "raid.h"

#define ALRU_GET_HASH_VALUE(page,hash_size) ((page)%(hash_size))
#define ALRU_MISS 0
#define ALRU_HIT 1

#define STATE_DIRTY (0)
#define STATE_UPTODATE (1)

#define IS_PAGE_DIRTY(state) ((state)&((0x1uL)<<STATE_DIRTY))
#define IS_PAGE_UPTODATE(state) ((state)&((0x1uL)<<STATE_UPTODATE))

#define SECTOR_SIZE (512)
#define BYTES_PER_BM_LONG (1L*8*8*SECTOR_SIZE)
#define SECTORS_PER_BM_LONG (BYTES_PER_BM_LONG/SECTOR_SIZE)
#define MAX_PAGE_SIZE (1L*128*SECTOR_SIZE)
#define MAX_BM_MASK_SIZE MAX_PAGE_SIZE/BYTES_PER_BM_LONG

typedef struct alru_page{
	long page;																				//page id
	unsigned long state;																//STATE_DIRTY bit, dirty:1 clean:0 
																									// STATE_UPTODATE bit, there are no ios between the buf and disk, uptodate:1 
	long refs;																				//refs
	unsigned long bm_mask[MAX_BM_MASK_SIZE];//bitmap,a bit1denotesits adirtysector atthesize of 512 Bytes
	struct timeval requested_time; 
	str_dlnode alru_node;
	str_dlnode alru_ds_node;
	str_dlnode hash_node;
}str_alru_page,*pstr_alru_page,**ppstr_alru_page;
#define STRALRUPAGESIZE sizeof(str_alru_page)
#define PSTRALRUPAGESIZE sizeof(pstr_alru_page)
#define PPSTRALRUPAGESIZE sizeof(ppstr_alru_page)

typedef struct alg_alru{
	pstr_alru_page alru_pages;																									//alru page structs, easy to free
	str_dldsc free_clean_list;																								//hold free clean pages
	str_dldsc free_dirty_list;																								//hold free dirty pages
	str_dldsc clean_list;																											//clean list descriptor, alru style
	str_dldsc dirty_list;																											//dirty list descriptor, alru style
	str_dldsc clean_ds_list[MAX_RAID_DISKS];
	str_dldsc dirty_ds_list[MAX_RAID_DISKS];
	str_dldsc *hash;																													//hash bucket, need to alloc
	pthread_mutex_t alru_mutex;																									//used to access clean and dirty list, for simplistic, we only move clean page to dirty list when comes a write
//pthread_mutex_t hash_mutex;																									//used to access hash, we can have multiple mutexes to avoid contention on accessing hash
	pthread_cond_t destage_cond;
	pthread_cond_t destage_finish_cond;
	pthread_t	destage_thread;
}str_alg_alru,*pstr_alg_alru,**ppstr_alg_alru;
#define STRALGALRUSIZE sizeof(str_alg_alru)
#define PSTRALGALRUSIZE sizeof(pstr_alg_alru)
#define PPSTRALGALRUSIZE sizeof(ppstr_alg_alru)

int connect_alru(void* pac);

#endif
