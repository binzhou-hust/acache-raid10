#ifndef LRU_H
#define LRU_H

#include<pthread.h>
#include<time.h>

#include "dllist.h"
#include "algorithm.h"
#include "raid.h"

#define LRU_GET_HASH_VALUE(page,hash_size) ((page)%(hash_size))
#define LRU_MISS 0
#define LRU_HIT 1

#define STATE_DIRTY (0)
#define STATE_UPTODATE (1)

#define IS_PAGE_DIRTY(state) ((state)&((0x1uL)<<STATE_DIRTY))
#define IS_PAGE_UPTODATE(state) ((state)&((0x1uL)<<STATE_UPTODATE))

#define SECTOR_SIZE (512)
#define BYTES_PER_BM_LONG (1L*8*8*SECTOR_SIZE)
#define SECTORS_PER_BM_LONG (BYTES_PER_BM_LONG/SECTOR_SIZE)
#define MAX_PAGE_SIZE (1L*128*SECTOR_SIZE)
#define MAX_BM_MASK_SIZE MAX_PAGE_SIZE/BYTES_PER_BM_LONG

typedef struct lru_page{
	long page;																				//page id
	unsigned long state;																//STATE_DIRTY bit, dirty:1 clean:0 
																									// STATE_UPTODATE bit, there are no ios between the buf and disk, uptodate:1 
	long refs;																				//refs
	unsigned long bm_mask[MAX_BM_MASK_SIZE];				//bitmap, a bit 1 denotes its a dirty sector at the size of 512 Bytes
	struct timeval requested_time; 
	str_dlnode lru_node;
	str_dlnode hash_node;
}str_lru_page,*pstr_lru_page,**ppstr_lru_page;
#define STRLRUPAGESIZE sizeof(str_lru_page)
#define PSTRLRUPAGESIZE sizeof(pstr_lru_page)
#define PPSTRLRUPAGESIZE sizeof(ppstr_lru_page)

typedef struct alg_lru{
	pstr_lru_page lru_pages;																									//lru page structs, easy to free
	str_dldsc free_clean_list;																								//hold free clean pages
	str_dldsc free_dirty_list;																								//hold free dirty pages
	str_dldsc clean_list;																											//clean list descriptor, lru style
	str_dldsc dirty_list;																											//dirty list descriptor, lru style
	str_dldsc *hash;																													//hash bucket, need to alloc
	pthread_mutex_t lru_mutex;																									//used to access clean and dirty list, for simplistic, we only move clean page to dirty list when comes a write
//	pthread_mutex_t hash_mutex;																									//used to access hash, we can have multiple mutexes to avoid contention on accessing hash
	pthread_cond_t destage_cond;
	pthread_cond_t destage_finish_cond;
	pthread_t	destage_thread;
}str_alg_lru,*pstr_alg_lru,**ppstr_alg_lru;
#define STRALGLRUSIZE sizeof(str_alg_lru)
#define PSTRALGLRUSIZE sizeof(pstr_alg_lru)
#define PPSTRALGLRUSIZE sizeof(ppstr_alg_lru)

int connect_lru(void* pac);

#endif
