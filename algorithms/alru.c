#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<time.h>
#include<unistd.h>
#include<string.h>

#include "acache.h"
#include "error.h"
#include "request_io.h"
#include "dllist.h"
#include "algorithm.h"
#include "alru.h"

int alru_check_one_stack(pstr_dldsc pdld,char *stackname,long stacktype,long disk);
int alru_print_stack(pstr_dldsc pdld,char *stackname,long stacktype);
int alru_check_stack(pstr_algorithm pa);
int alru_state(pstr_algorithm pa);
int init_alru(pstr_algorithm pa);
int reclaim_alru(pstr_algorithm pa);
int alru_replace(pstr_algorithm pa, pstr_user_request pur);
int alru_generate_bm(long s, long e, unsigned long* bm_mask);
int alru_find(long page, ppstr_alru_page pppage, pstr_algorithm pa);
int alru_call_back_do_nothing(long offset, long length, long disk);
int alru_finish_read(long offset, long length, long disk);
int alru_finish_write(long offset, long length, long disk);
int alru_finish_destage_read(long offset, long length, long disk);
int alru_finish_destage_write(long offset, long length, long disk);

int do_alru_replacepage(long page, long rw, unsigned long* bm_mask, pstr_algorithm pa);
pstr_alru_page find_alru_page_ds_stack(pstr_dldsc pd);
pstr_alru_page find_dirty_alru_page(pstr_alg_alru pal);
pstr_alru_page find_clean_alru_page(pstr_alg_alru pal);
void* alru_destage(void* destage_arg);


int inline init_alru(pstr_algorithm pa){
	void* tret=NULL;
	int rst;
	long i;
	pstr_alg_alru pal	= (pstr_alg_alru)pa->alg;
	pstr_raid pr =	(pstr_raid)((pstr_acache)pa->pacache)->rpers;
	
	if(	0>=pa->page_size
			||	0!=pa->exit
			||	0>=pa->pages
			||	0>=pa->hash_size
			||	0>=pa->dpages
			||	0>=pa->destage_threadhold
			||	0!=pa->dpage_cnt
			||	NULL==pal
			||	NULL==pr){
		return ERR_INVALIDPARA_AC;
	}

	//alloc alru pages
	pal->alru_pages	= NULL;
	pal->alru_pages	=	(pstr_alru_page)malloc(pa->pages * STRALRUPAGESIZE);
	if(NULL == pal->alru_pages){
		rst =	ERR_NOMEM_AC;
		goto ERRNOMEM2;
	}
	
	//init alru pages
	for(i=0;i<pa->pages;i++){
		pal->alru_pages[i].alru_node.pre=NULL;
		pal->alru_pages[i].alru_node.next=NULL;
		pal->alru_pages[i].alru_ds_node.pre=NULL;
		pal->alru_pages[i].alru_ds_node.next=NULL;
		pal->alru_pages[i].hash_node.pre=NULL;
		pal->alru_pages[i].hash_node.next=NULL;
	}

	//add pages to free_dirty_list and free_clean_list 
	pal->free_dirty_list.head=NULL;
	pal->free_clean_list.head=NULL;	
	pal->dirty_list.head=NULL;
	pal->clean_list.head=NULL;
	for(i=0;i<pr->raid_disks;i++){
		pal->dirty_ds_list[i].head=NULL;
		pal->clean_ds_list[i].head=NULL;
	}
	
	for(i=0;i<pa->dpages;i++)
		dl_list_add_node_to_head(&pal->free_dirty_list,&pal->alru_pages[i].alru_node);

	for(;i<pa->pages;i++)
		dl_list_add_node_to_head(&pal->free_clean_list,&pal->alru_pages[i].alru_node);

	//printf("init alru2\n");
	//alloc hash
	pal->hash = NULL;
	pal->hash	=	(pstr_dldsc)malloc(pa->hash_size * STRDLDSCSIZE);
	if(NULL == pal->hash){
		rst =	ERR_NOMEM_AC;
		goto ERRNOMEM1;
	}
	
	//init hash
	for(i=0;i<pa->hash_size;i++){
		pal->hash[i].head=NULL;
	}
	
	//init mutex
	rst=pthread_mutex_init(&pal->alru_mutex,NULL);
	if(0!=rst){
		rst = ERR_CREATEMUTEX_AC;
		goto ERRINITMUTEX;
	}

/*
	rst=pthread_mutex_init(&pal->hash_mutex,NULL);
	if(0!=rst){
		rst = ERR_CREATEMUTEX_AC;
		goto ERRINITMUTEX1;
	}
*/
	//init cond
	rst=pthread_cond_init(&pal->destage_cond,NULL);
	if(0!=rst){
		rst = ERR_CREATECOND_AC;
		goto ERRINITCOND2;
	}

	rst=pthread_cond_init(&pal->destage_finish_cond,NULL);
	if(0!=rst){
		rst = ERR_CREATECOND_AC;
		goto ERRINITCOND1;
	}
	
	//start the destage thread
	rst=pthread_create(&pal->destage_thread,
				NULL, alru_destage,NULL);
	if(0!=rst){
		rst = ERR_CREATETHREAD_AC;
		goto ERRCREATETHREAD;
	}
	//printf("init alru3\n");
	return ERR_SUCCESS_AC;

pa->exit=1;
	pthread_join(pal->destage_thread,tret);
ERRCREATETHREAD:
	pthread_cond_destroy(&pal->destage_finish_cond);
ERRINITCOND1:
	pthread_cond_destroy(&pal->destage_cond);
ERRINITCOND2:
	pthread_mutex_destroy(&pal->alru_mutex);
ERRINITMUTEX:
	free(pal->hash);
ERRNOMEM1:
	free(pal->alru_pages);
ERRNOMEM2:

	return rst;
}

int inline reclaim_alru(pstr_algorithm pa){
	void* tret=NULL;
	pstr_alg_alru pal= (pstr_alg_alru)pa->alg;
	
	pa->exit=1;
	pthread_join(pal->destage_thread,tret);
	pthread_cond_destroy(&pal->destage_finish_cond);
	pthread_cond_destroy(&pal->destage_cond);
	pthread_mutex_destroy(&pal->alru_mutex);
	free(pal->hash);
	free(pal->alru_pages);
	free(pa->alg);
	return ERR_SUCCESS_AC;
}

int inline alru_generate_bm(long s, long e, unsigned long* bm_mask){
	long i,start,end;
	for(i=0;i<MAX_BM_MASK_SIZE;i++){
		bm_mask[i]=0x0uL;
	}
	
	for(i=s/SECTORS_PER_BM_LONG;
			i<=e/SECTORS_PER_BM_LONG;
			i++){
		if(i==s/SECTORS_PER_BM_LONG){
			//the first sub_piece
			start=s%SECTORS_PER_BM_LONG;
		}else{
			start=0;
		}

		if(i<e/SECTORS_PER_BM_LONG){
			//not the last sub_piece
			end=SECTORS_PER_BM_LONG-1;
		}else{
			end=e%SECTORS_PER_BM_LONG;
		}
		//printf("start:%ld,end:%ld\n",start,end);
		//log or al shift? take care!!!!!!!!!!!!!!!!!
		//printf("%16lx\n%16lx\n",(~0x0uL)>>start,((~0x0uL)<<(SECTORS_PER_BM_LONG-end-1)));
		bm_mask[i]=((~0x0uL)>>start)	
			&	((~0x0uL)<<(SECTORS_PER_BM_LONG-end-1));
	}

	return ERR_SUCCESS_AC;
}

int inline alru_find(long page, ppstr_alru_page pppage, pstr_algorithm pa){
	pstr_alg_alru pal=(pstr_alg_alru)pa->alg;
	long hash_value	=	ALRU_GET_HASH_VALUE(page,pa->hash_size);
	pstr_dlnode pn=NULL, ph=NULL;

	pn=ph=pal->hash[hash_value].head;

	if(NULL==pn)
		return ALRU_MISS;
	
	do{
		if(CONTAINER_OF(pn,hash_node,str_alru_page)->page==page){
			*pppage=CONTAINER_OF(pn,hash_node,str_alru_page);
			return ALRU_HIT;
		}
		else
			pn=pn->next;
	}while(pn!=ph);

	return ALRU_MISS;
}

int inline alru_replace(pstr_algorithm pa, pstr_user_request pur){
	//make it reenterable
	long i,page,start_sector,end_sector;
	unsigned long bm_mask[MAX_BM_MASK_SIZE];
	pstr_alg_alru pal=(pstr_alg_alru)(pa->alg);
	
	//slice the request to page aligned pieces
	page=pur->offset/pa->page_size;
	do{
		if(page==pur->offset/pa->page_size){
			start_sector=(pur->offset%pa->page_size)/SECTOR_SIZE;
		}else{
			//not the first piece
			start_sector=0;
		}

		if(page<(pur->offset+pur->length-1)/pa->page_size){
			//not the last piece
			end_sector=pa->page_size/SECTOR_SIZE-1;
		}else{
			end_sector=((pur->offset+pur->length-1)%pa->page_size)/SECTOR_SIZE;
		}
		//printf("start_sector:%ld,end_sector:%ld\n",start_sector,end_sector);

		alru_generate_bm(start_sector,end_sector,bm_mask);
		/*for(i=0;i<pa->page_size/BYTES_PER_BM_LONG;i++){
			printf("bm_mask %ld:%16lx\n",i,bm_mask[i]);
		}*/
		//now we got the bm_mask
		do_alru_replacepage(page,pur->rw, bm_mask, pa);
		page++;
	}while(page<=(pur->offset+pur->length-1)/pa->page_size);
	//printf("quit alru_replace\n");
	return ERR_SUCCESS_AC;
}


int inline do_alru_replacepage(long page, long rw, unsigned long* bm_mask, pstr_algorithm pa){
	int rst,bm_hit;
	long i;
	long hash_value, disk, disk2,strip, stripe, disk_page;
	pstr_alg_alru pal=(pstr_alg_alru)pa->alg;
	pstr_raid pr =	(pstr_raid)((pstr_acache)pa->pacache)->rpers;
	pstr_trace pt =	(pstr_trace)((pstr_acache)pa->pacache)->tpers;
	pstr_alru_page ppage=NULL,ppage2=NULL;
	struct timeval now_time,requested_time;
	struct timespec out_time;

	
	pthread_mutex_lock(&pal->alru_mutex);
	//gettimeofday(&requested_time,NULL);
	pt->requested_pages++;
	//alru_check_stack(pa);
	//alru_state(pa);
	pr->page_to_disk(page,pa->page_size,&disk);
	rst=alru_find(page,&ppage,pa);

	switch(rst){
		case ALRU_HIT:
			//printf("do_alru_replacepage LRU_HIT\n");
			ppage->refs++;
			
			//wait for the page to be uptodate
			while(!IS_PAGE_UPTODATE(ppage->state)){
				gettimeofday(&now_time, NULL);
				ADD_TIME_MSEC(out_time,now_time,10);
				pthread_cond_timedwait(&pr->io_finish_cond,&pal->alru_mutex, &out_time);
			}
	
			if((REQ_READ==rw && !IS_PAGE_DIRTY(ppage->state))
				||	(REQ_WRITE==rw && IS_PAGE_DIRTY(ppage->state))){
				//printf("read a clean page or write a dirty page\n");
				//read a clean page or write a dirty page
				//and now the page is uptodate
				if(REQ_READ==rw){
					//printf("read a clean page\n");
					dl_list_remove_node(&pal->clean_list, &ppage->alru_node);
					dl_list_remove_node(&pal->clean_ds_list[disk], &ppage->alru_ds_node);
					dl_list_add_node_to_head(&pal->clean_list, &ppage->alru_node);
					dl_list_add_node_to_head(&pal->clean_ds_list[disk], &ppage->alru_ds_node);
				}else{
					//printf("write a dirty page\n");
					//REQ_WRITE==rw, maybe we need to udpate the bitmap
					for(i=0;i<pa->page_size/BYTES_PER_BM_LONG;i++){
						ppage->bm_mask[i]	|=	bm_mask[i];
					}
					dl_list_remove_node(&pal->dirty_list, &ppage->alru_node);
					dl_list_remove_node(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
					dl_list_add_node_to_head(&pal->dirty_list, &ppage->alru_node);
					dl_list_add_node_to_head(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
				}
				ppage->refs--;
				//alru_state(pa);
				pthread_mutex_unlock(&pal->alru_mutex);
			}else if(REQ_READ==rw && IS_PAGE_DIRTY(ppage->state)){
				//read a dirty page, we need to varify all requested sectors are in the cache
				//by checking the page bit map, which only record the dirty sectors,
				//for a clean page, we treat it as a full page in cache, but all bits in bm are 0
				//printf("read a dirty page\n");
				bm_hit=1;
				for(i=0;i<pa->page_size/BYTES_PER_BM_LONG;i++){
					//printf("read a dirty page: i:%ld,bm_mask[i]:%16lx,ppage->bm_mask[i]:%16lx\n",i,bm_mask[i],ppage->bm_mask[i]);
					//printf("%d\n",bm_mask[i]	==	ppage->bm_mask[i] & bm_mask[i]);
					//printf("bm_hit:%d\n",bm_hit);
					bm_hit =bm_hit && (bm_mask[i]	==	(ppage->bm_mask[i] & bm_mask[i]));
					//printf("bm_hit:%d\n",bm_hit);
				}
				
				if(bm_hit){
					//all requested data are in the cache
					//printf("read a dirty page bm_hit\n");
					dl_list_remove_node(&pal->dirty_list, &ppage->alru_node);
					dl_list_remove_node(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
					dl_list_add_node_to_head(&pal->dirty_list, &ppage->alru_node);
					dl_list_add_node_to_head(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
					ppage->refs--;
					//alru_state(pa);
					pthread_mutex_unlock(&pal->alru_mutex);
				}else{
					//printf("read a dirty page bm_miss\n");
					//we need to read the whole page from the disk and mark page as full dirty page
					//move the page to the head of the dirty_list
					//dl_list_remove_node(&pal->dirty_list, &ppage->alru_node);
					//dl_list_add_node_to_head(&pal->dirty_list, &ppage->alru_node);

					//set the page as not uptodate, keep the dirty bit here
					ppage->state	&=	~((0x1uL)<<STATE_UPTODATE);
					dl_list_remove_node(&pal->dirty_list, &ppage->alru_node);
					dl_list_remove_node(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
					dl_list_add_node_to_head(&pal->dirty_list, &ppage->alru_node);
					dl_list_add_node_to_head(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
					//only recorded before a real disk io
					//here this latency is used to adjust the a-cache Skewing
					//but not to measure the user request latency
					gettimeofday(&ppage->requested_time,NULL);
					pthread_mutex_unlock(&pal->alru_mutex);

					//read the whole page
					//usleep(1000);
					pr->handle_cache_request(page,pa->page_size,REQ_READ,alru_finish_read,alru_call_back_do_nothing, &ppage->requested_time);
					//update the bm_mask, a full dirty page
					/*pthread_mutex_lock(&pal->alru_mutex);
					for(i=0;i<pa->page_size/BYTES_PER_BM_LONG;i++){
						ppage->bm_mask[i]=~0x0uL;
					}
					ppage->state	|=	(0x1uL)<<STATE_UPTODATE;
					ppage->refs--;
					//alru_state(pa);
					pthread_mutex_unlock(&pal->alru_mutex);
					pthread_cond_broadcast(&pa->rpers->io_finish_cond);*/
				}
			}else if(REQ_WRITE==rw && !IS_PAGE_DIRTY(ppage->state)){
				//REQ_WRITE==rw && PAGE_CLEAN==ppage->dirty
				//write a clean page, all sectors will be marked as dirty for simplitic
				//and we only perform raw, to flush a dirty page

				//move the clean page to the dirty list
				//printf("write a clean page\n");
				dl_list_remove_node(&pal->clean_list, &ppage->alru_node);
				dl_list_remove_node(&pal->clean_ds_list[disk], &ppage->alru_ds_node);
				dl_list_add_node_to_head(&pal->dirty_list, &ppage->alru_node);
				dl_list_add_node_to_head(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
				ppage->state |= (0x1uL<<STATE_DIRTY);
				ppage->refs--;
				for(i=0;i<pa->page_size/BYTES_PER_BM_LONG;i++){
					ppage->bm_mask[i]=~0x0uL;
				}
				pa->dpage_cnt++;
				//printf("write a clean page 2\n");
				//if the number of dirty pages exceed the threshold
				//wake up the destage thread to flush the alru dirty pages
				if(pa->dpage_cnt>=pa->destage_threadhold){
					pthread_cond_signal(&pal->destage_cond);
				}
				//printf("write a clean page 3\n");
				//get a page from free_dirty_list,
				//if there is no dirty page, wait until it has,
				//maybe we need to wake up the destage thread
				while(NULL==pal->free_dirty_list.head){
					pthread_cond_signal(&pal->destage_cond);
					gettimeofday(&now_time, NULL);
					ADD_TIME_MSEC(out_time,now_time,10);
					pthread_cond_timedwait(&pal->destage_finish_cond,&pal->alru_mutex,&out_time);
				}
				//printf("write a clean page 4\n");
				//now we have a free dirty page,move it into free clean page list
				ppage2=CONTAINER_OF(pal->free_dirty_list.head,alru_node, str_alru_page);
				dl_list_remove_node(&pal->free_dirty_list, &ppage2->alru_node);
				dl_list_add_node_to_head(&pal->free_clean_list, &ppage2->alru_node);
				//printf("write a clean page 5\n");
				//alru_state(pa);
				pthread_mutex_unlock(&pal->alru_mutex);
			}else{
				printf("impossible\n");
				printf("rw:%ld,dirty:%ld",rw,IS_PAGE_DIRTY(ppage->state));
			}
			break;
		case ALRU_MISS:
			//printf("do_alru_replacepage ALRU_MISS\n");
			if(REQ_READ==rw){
				//printf("do_alru_replacepage read miss\n");
				//read miss, get a page from the free clean page list
				//first lock alru, then lock hash
				ppage=NULL;
				if(NULL==pal->free_clean_list.head){
					//find the alru page with refs==0
					ppage=find_clean_alru_page(pal);
					//printf("do_alru_replacepage read miss find_clean_alru_page\n");
					if(NULL==ppage){
						//treat as bug
						//very very low probability 
						//that the number of refs of all pages in alru are not 0
						//when the cache is very small and has to face a long scan,
						//the all pages may be referred
						//so we must handle this in the next version
						//alru_state(pa);
						//printf("ALRU bug\n");
						//exit(1);
						ppage2=NULL;
						do{
							gettimeofday(&now_time, NULL);
							ADD_TIME_MSEC(out_time,now_time,10);
							pthread_cond_timedwait(&pr->io_finish_cond,&pal->alru_mutex,&out_time);
							ppage2=find_clean_alru_page(pal);
						}while(NULL==ppage2);
						
						//now ppage2 is the candidate to evict
						if(ALRU_MISS==alru_find(page,&ppage,pa) ){
							pr->page_to_disk(ppage2->page,pa->page_size,&disk2);
							hash_value=ALRU_GET_HASH_VALUE(ppage2->page,pa->hash_size);
							dl_list_remove_node(&pal->hash[hash_value], &ppage2->hash_node);
							dl_list_remove_node(&pal->clean_list, &ppage2->alru_node);
							dl_list_remove_node(&pal->clean_ds_list[disk2], &ppage2->alru_ds_node);

							ppage2->page=page;
							pr->page_to_disk(ppage2->page,pa->page_size,&disk2);
							ppage2->refs=1;
							hash_value=ALRU_GET_HASH_VALUE(ppage2->page,pa->hash_size);
							//it is a clean and not up to date page
							ppage2->state=0x0uL;
							//ppage->state	&=	(0x0uL<<STATE_UPTODATE) | (0x0uL<<STATE_DIRTY);
							for(i=0;i<pa->page_size/BYTES_PER_BM_LONG;i++){
								ppage2->bm_mask[i]=0x0uL;
							}
							
							dl_list_add_node_to_head(&pal->hash[hash_value], &ppage2->hash_node);
							dl_list_add_node_to_head(&pal->clean_list, &ppage2->alru_node);
							dl_list_add_node_to_head(&pal->clean_ds_list[disk2], &ppage2->alru_ds_node);
							gettimeofday(&ppage2->requested_time,NULL);
							//ppage2->requested_time.tv_sec=requested_time.tv_sec;
							//ppage2->requested_time.tv_usec=requested_time.tv_usec;
							//printf("we add the node to lists\n");
							pthread_mutex_unlock(&pal->alru_mutex);
							pr->handle_cache_request(page,pa->page_size,REQ_READ,alru_finish_read,alru_call_back_do_nothing,&ppage->requested_time);
							break;//quit the switch
						}else{
							//hit ppage
							ppage->refs++;
							pr->page_to_disk(ppage->page,pa->page_size,&disk);
							//wait for the page to be uptodate
							while(!IS_PAGE_UPTODATE(ppage->state)){
								gettimeofday(&now_time, NULL);
								ADD_TIME_MSEC(out_time,now_time,10);
								pthread_cond_timedwait(&pr->io_finish_cond,&pal->alru_mutex, &out_time);
							}

							if(IS_PAGE_DIRTY(ppage->state)){
								bm_hit=1;
								for(i=0;i<pa->page_size/BYTES_PER_BM_LONG;i++){
									bm_hit =bm_hit && (bm_mask[i]	==	(ppage->bm_mask[i] & bm_mask[i]));
								}
								if(bm_hit){
									//all requested data are in the cache
									dl_list_remove_node(&pal->dirty_list, &ppage->alru_node);
									dl_list_remove_node(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
									dl_list_add_node_to_head(&pal->dirty_list, &ppage->alru_node);
									dl_list_add_node_to_head(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
									ppage->refs--;
									pthread_mutex_unlock(&pal->alru_mutex);
								}else{
									//printf("read a dirty page bm_miss\n");
									//we need to read the whole page from the disk and mark page as full dirty page
									//move the page to the head of the dirty_list
									//set the page as not uptodate, keep the dirty bit here
									ppage->state	&=	~((0x1uL)<<STATE_UPTODATE);
									dl_list_remove_node(&pal->dirty_list, &ppage->alru_node);
									dl_list_remove_node(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
									dl_list_add_node_to_head(&pal->dirty_list, &ppage->alru_node);
									dl_list_add_node_to_head(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
									gettimeofday(&ppage->requested_time,NULL);
									//ppage->requested_time.tv_sec=requested_time.tv_sec;
									//ppage->requested_time.tv_usec=requested_time.tv_usec;
									pthread_mutex_unlock(&pal->alru_mutex);
									pr->handle_cache_request(page,pa->page_size,REQ_READ,alru_finish_read,alru_call_back_do_nothing,&ppage->requested_time);
								}
							}else{
								//read a clean page
								ppage->refs--;
								dl_list_remove_node(&pal->clean_list, &ppage->alru_node);
								dl_list_remove_node(&pal->clean_ds_list[disk], &ppage->alru_ds_node);
								dl_list_add_node_to_head(&pal->clean_list, &ppage->alru_node);
								dl_list_add_node_to_head(&pal->clean_ds_list[disk], &ppage->alru_ds_node);
								pthread_mutex_unlock(&pal->alru_mutex);
							}
							break;
						}
						//we must break here
						printf("This line should not be executed, break fails\n");
						return ERR_SUCCESS_AC;//we do not need do this
					}
					pr->page_to_disk(ppage->page,pa->page_size,&disk);
					hash_value=ALRU_GET_HASH_VALUE(ppage->page,pa->hash_size);
					dl_list_remove_node(&pal->hash[hash_value], &ppage->hash_node);
					dl_list_remove_node(&pal->clean_list, &ppage->alru_node);
					dl_list_remove_node(&pal->clean_ds_list[disk], &ppage->alru_ds_node);
				}else{
					ppage=CONTAINER_OF(pal->free_clean_list.head,alru_node, str_alru_page);
					dl_list_remove_node(&pal->free_clean_list, &ppage->alru_node);
				}
				//printf("we got a free page\n");
				//we got a free page
				ppage->page=page;
				pr->page_to_disk(ppage->page,pa->page_size,&disk);
				ppage->refs=1;
				hash_value=ALRU_GET_HASH_VALUE(ppage->page,pa->hash_size);
				//it is a clean and not up to date page
				ppage->state=0x0uL;
				//ppage->state	&=	(0x0uL<<STATE_UPTODATE) | (0x0uL<<STATE_DIRTY);
				for(i=0;i<pa->page_size/BYTES_PER_BM_LONG;i++){
					ppage->bm_mask[i]=0x0uL;
				}
				dl_list_add_node_to_head(&pal->hash[hash_value], &ppage->hash_node);
				dl_list_add_node_to_head(&pal->clean_list, &ppage->alru_node);
				dl_list_add_node_to_head(&pal->clean_ds_list[disk], &ppage->alru_ds_node);
				gettimeofday(&ppage->requested_time,NULL);
				//ppage->requested_time.tv_sec=requested_time.tv_sec;
				//ppage->requested_time.tv_usec=requested_time.tv_usec;
				//printf("we add the node to lists\n");
				pthread_mutex_unlock(&pal->alru_mutex);
				pr->handle_cache_request(page,pa->page_size,REQ_READ,alru_finish_read,alru_call_back_do_nothing,&ppage->requested_time);
				//read the page from the disk
				//usleep(1000);
				
				//pthread_mutex_lock(&pal->alru_mutex);
				//printf("we read the content\n");
				//ppage->state	|=	(0x1uL)<<STATE_UPTODATE;
				//ppage->refs--;
				//alru_state(pa);
				//pthread_mutex_unlock(&pal->alru_mutex);
				//printf("we unlock the mutex\n");
				//pthread_cond_broadcast(&pa->rpers->io_finish_cond);
				//printf("broadcast OK\n");
			}else{
				//printf("do_alru_replacepage write miss\n");
				//write miss, get a free dirty page
				//if the number of dirty pages exceed the threshold
				//wake up the destage thread to flush the alru dirty pages

				//make sure free_dirty_list is not empty
				//if there is no dirty page, wait until it has,
				//maybe we need to wake up the destage thread
				while(NULL==pal->free_dirty_list.head){
					pthread_cond_signal(&pal->destage_cond);
					gettimeofday(&now_time, NULL);
					ADD_TIME_MSEC(out_time,now_time,10);
					pthread_cond_timedwait(&pal->destage_finish_cond,&pal->alru_mutex,&out_time);
				}

				if(ALRU_MISS==alru_find(page,&ppage,pa) ){
					//printf("do_alru_replacepage write miss and miss again\n");
					//now we have a free dirty page,move it into free clean page list
					ppage=CONTAINER_OF(pal->free_dirty_list.head,alru_node, str_alru_page);
					dl_list_remove_node(&pal->free_dirty_list, &ppage->alru_node);
					ppage->page=page;
					pr->page_to_disk(page,pa->page_size,&disk);
					//printf("page:%ld,disk:%ld\n",page,disk);
					hash_value=ALRU_GET_HASH_VALUE(ppage->page,pa->hash_size);
					for(i=0;i<pa->page_size/BYTES_PER_BM_LONG;i++){
						ppage->bm_mask[i]	|=	bm_mask[i];
					}
					ppage->state=0x0uL;
					ppage->state	|=	(0x1uL<<STATE_UPTODATE) | (0x1uL<<STATE_DIRTY);
					//alru_state(pa);
					dl_list_add_node_to_head(&pal->hash[hash_value], &ppage->hash_node);
					dl_list_add_node_to_head(&pal->dirty_list, &ppage->alru_node);
					dl_list_add_node_to_head(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
					pa->dpage_cnt++;
					//alru_state(pa);
					pthread_mutex_unlock(&pal->alru_mutex);
				}else{
					//hit
					//printf("do_alru_replacepage write miss and hit again\n");
					ppage->refs++;
					
					if(IS_PAGE_DIRTY(ppage->state)){
						//wait for the page to be uptodate
						while(!IS_PAGE_UPTODATE(ppage->state)){
							gettimeofday(&now_time, NULL);
							ADD_TIME_MSEC(out_time,now_time,10);
							pthread_cond_timedwait(&pr->io_finish_cond,&pal->alru_mutex,&out_time);
						}
					
						//printf("do_alru_replacepage write miss and hit dirty\n");
						//another thread has insert this page into dirty stack
						//REQ_WRITE==rw, maybe we need to udpate the bitmap
						for(i=0;i<pa->page_size/BYTES_PER_BM_LONG;i++){
							ppage->bm_mask[i]	|=	bm_mask[i];
						}

						//we don't need do this
						//ppage->state	|=	(0x1uL<<STATE_UPTODATE) | (0x1uL<<STATE_DIRTY);
						pr->page_to_disk(ppage->page,pa->page_size,&disk);
						ppage->refs--;
						dl_list_remove_node(&pal->dirty_list, &ppage->alru_node);
						dl_list_remove_node(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
						dl_list_add_node_to_head(&pal->dirty_list, &ppage->alru_node);
						dl_list_add_node_to_head(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
						//alru_state(pa);
						pthread_mutex_unlock(&pal->alru_mutex);
					}else{
						//wait for the page to be uptodate
						while(!IS_PAGE_UPTODATE(ppage->state)){
							gettimeofday(&now_time, NULL);
							ADD_TIME_MSEC(out_time,now_time,10);
							pthread_cond_timedwait(&pr->io_finish_cond,&pal->alru_mutex,&out_time);
						}

						pr->page_to_disk(ppage->page,pa->page_size,&disk);
						if(IS_PAGE_DIRTY(ppage->state)){
							//maybe we will not enter this branch
							//printf("write a dirty page\n");
							//REQ_WRITE==rw, maybe we need to udpate the bitmap
							for(i=0;i<pa->page_size/BYTES_PER_BM_LONG;i++){
								ppage->bm_mask[i]	|=	bm_mask[i];
							}
							ppage->refs--;
							dl_list_remove_node(&pal->dirty_list, &ppage->alru_node);
							dl_list_remove_node(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
							dl_list_add_node_to_head(&pal->dirty_list, &ppage->alru_node);
							dl_list_add_node_to_head(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
							pthread_mutex_unlock(&pal->alru_mutex);
						}else{
							//printf("write a clean page\n");
							dl_list_remove_node(&pal->clean_list, &ppage->alru_node);
							dl_list_remove_node(&pal->clean_ds_list[disk], &ppage->alru_ds_node);
							dl_list_add_node_to_head(&pal->dirty_list, &ppage->alru_node);
							dl_list_add_node_to_head(&pal->dirty_ds_list[disk], &ppage->alru_ds_node);
							ppage->state |= (0x1uL<<STATE_DIRTY);
							ppage->refs--;
							for(i=0;i<pa->page_size/BYTES_PER_BM_LONG;i++){
								ppage->bm_mask[i]=~0x0uL;
							}
							pa->dpage_cnt++;
							//printf("write a clean page 2\n");
							//if the number of dirty pages exceed the threshold
							//wake up the destage thread to flush the alru dirty pages
							if(pa->dpage_cnt>=pa->destage_threadhold){
								pthread_cond_signal(&pal->destage_cond);
							}
							//printf("write a clean page 3\n");
							//get a page from free_dirty_list,
							//if there is no dirty page, wait until it has,
							//maybe we need to wake up the destage thread
							while(NULL==pal->free_dirty_list.head){
								pthread_cond_signal(&pal->destage_cond);
								gettimeofday(&now_time, NULL);
								ADD_TIME_MSEC(out_time,now_time,10);
								pthread_cond_timedwait(&pal->destage_finish_cond,&pal->alru_mutex,&out_time);
							}
							//printf("write a clean page 4\n");
							//now we have a free dirty page,move it into free clean page list
							ppage2=CONTAINER_OF(pal->free_dirty_list.head,alru_node, str_alru_page);
							dl_list_remove_node(&pal->free_dirty_list, &ppage2->alru_node);
							dl_list_add_node_to_head(&pal->free_clean_list, &ppage2->alru_node);
							//printf("write a clean page 5\n");
							//alru_state(pa);
							pthread_mutex_unlock(&pal->alru_mutex);
						}
					}
				}
			}
			break;
		default:
			//impossible
			break;
	}
	//printf("quit do_alru_replacepage\n");
	//alru_state(pa);
	return ERR_SUCCESS_AC;
}


/*
void* read_page((void*) read_page_callback, pstr_alru_page ppage, pstr_alg_alru pal){
	msleep(10);
	pthread_mutex_lock(&pal->alru_mutex);
	read_page_call_back(ppage,pal);
	pthread_mutex_unlock(&pal->alru_mutex);
	return NULL;
}

void* write_page((void*) write_page_callback, pstr_alru_page ppage, pstr_alg_alru pal){
	msleep(10);
	pthread_mutex_lock(&pal->alru_mutex);
	read_page_call_back(ppage,pal);
	pthread_mutex_unlock(&pal->alru_mutex);
	return NULL;
}*/

//the main function of destage thread, only one thead, is not re-enterable
//I think we can simply move the alru dirty page to the clean list and
//treat it as no uptodate, then we do not

static struct algorithm alru={
	.name	=	"alru",
	.exit	=	0,
	.page_size = 64*1024,
	.pages = 1L*1*1024*1024,
	.hash_size	=	1L*1*1024*1024,
	.dpages	=	1L*1*1024*1024/10,
	.dpage_cnt = 0,
	.destage_cnt	=	0,
	.destage_threadhold	=	1L*1*1024*1024*9/100,
	.alg	=	NULL,
	.pacache = NULL,
	.init_algorithm = init_alru,
	.reclaim_algorithm = reclaim_alru,
	.replace = alru_replace,
};

pstr_alru_page find_alru_page_ds_stack(pstr_dldsc pd){
	pstr_dlnode pn=NULL, ph=NULL;
	if(NULL==pd->head)
		return NULL;
	
	pn=ph=pd->head;
	pn=pn->pre;
	do{
		if(0==CONTAINER_OF(pn,alru_ds_node,str_alru_page)->refs 
				&& IS_PAGE_UPTODATE(CONTAINER_OF(pn,alru_ds_node,str_alru_page)->state)){
			return CONTAINER_OF(pn,alru_ds_node,str_alru_page);
		}
		else
			pn=pn->pre;
	}while(pn!=ph);

	return NULL;
}
	
pstr_alru_page find_dirty_alru_page(pstr_alg_alru pal){
	pstr_dlnode pn=NULL, ph=NULL;

	pstr_raid pr =	(pstr_raid)((pstr_acache)alru.pacache)->rpers;
	pstr_trace pt =	(pstr_trace)((pstr_acache)alru.pacache)->tpers;
	long i,disk=-1,min_lat=1000000000L;
	pstr_alru_page ppage=NULL,ppage2=NULL;
	
	if(pr->start_urio){
		//find the disk pair stack which has the minume ave_latency
		for(i=0;i<pr->raid_disks/2;i++){
			if(pr->lc[i].avg_lat<=min_lat && 
					NULL!=(ppage=find_alru_page_ds_stack(&pal->dirty_ds_list[i]))){
				min_lat=pr->lc[i].avg_lat;
				disk=i;
				ppage2=ppage;
			}
		}
		
		return ppage2;
		/*if(-1==disk){
			return NULL;
		}

		pn=pal->dirty_ds_list[disk].head;
		ph=pal->dirty_ds_list[disk].head;

		pn=pn->pre;
		do{
			if(0==CONTAINER_OF(pn,alru_ds_node,str_alru_page)->refs 
					&& IS_PAGE_UPTODATE(CONTAINER_OF(pn,alru_ds_node,str_alru_page)->state)){
				return CONTAINER_OF(pn,alru_ds_node,str_alru_page);
			}
			else
				pn=pn->pre;
		}while(pn!=ph);*/
	}else{
		pn=ph=pal->dirty_list.head;

		if(NULL==pn)
			return NULL;

		pn=pn->pre;
		do{
			if(0==CONTAINER_OF(pn,alru_node,str_alru_page)->refs 
				&& IS_PAGE_UPTODATE(CONTAINER_OF(pn,alru_node,str_alru_page)->state)){
				return CONTAINER_OF(pn,alru_node,str_alru_page);
			}
			else
				pn=pn->pre;
		}while(pn!=ph);
	}
	return NULL;
}

pstr_alru_page find_clean_alru_page(pstr_alg_alru pal){
	pstr_dlnode pn=NULL, ph=NULL;
	pstr_alru_page ppage=NULL, ppage2=NULL;
	pstr_raid pr =	(pstr_raid)((pstr_acache)alru.pacache)->rpers;
	long i,disk=-1,min_lat=1000000000L;

	if(pr->start_urio){
		//printf("find_clean_alru_page start_urio\n");
		//find the disk pair stack which has the minume ave_latency
		for(i=0;i<pr->raid_disks/2;i++){
			if(pr->lc[i].avg_lat<=min_lat &&
					NULL!=(ppage=find_alru_page_ds_stack(&pal->clean_ds_list[i]))){
				min_lat=pr->lc[i].avg_lat;
				disk=i;
				ppage2=ppage;
			}
		}
		
		return ppage2;
		/*if(-1==disk)
			return NULL;

		pn=pal->clean_ds_list[disk].head;
		ph=pal->clean_ds_list[disk].head;

		pn=pn->pre;
		do{
			if(0==CONTAINER_OF(pn,alru_ds_node,str_alru_page)->refs 
					&& IS_PAGE_UPTODATE(CONTAINER_OF(pn,alru_ds_node,str_alru_page)->state)){
				return CONTAINER_OF(pn,alru_ds_node,str_alru_page);
			}
			else
				pn=pn->pre;
		}while(pn!=ph);	*/
	}else{
		pn=ph=pal->clean_list.head;

		if(NULL==pn)
			return NULL;

		pn=pn->pre;
		do{
			if(0==CONTAINER_OF(pn,alru_node,str_alru_page)->refs 
					&& IS_PAGE_UPTODATE(CONTAINER_OF(pn,alru_node,str_alru_page)->state)){
				return CONTAINER_OF(pn,alru_node,str_alru_page);
			}
			else
				pn=pn->pre;
		}while(pn!=ph);		
	}
	return NULL;
}

int inline alru_call_back_do_nothing(long offset, long length, long disk){
	long page;
	//pstr_alru_page ppage=NULL;
	pstr_alg_alru	pal=(pstr_alg_alru)alru.alg;
	pstr_raid pr =	(pstr_raid)((pstr_acache)alru.pacache)->rpers;
	pstr_trace pt =	(pstr_trace)((pstr_acache)alru.pacache)->tpers;
	
	//printf("alru_finish_read, offset:%ld,length:%ld,disk:%ld\n",offset,length,disk);
	//pr->io_to_page(&page,offset,length,disk,alru.page_size);
	//printf("alru_call_back_do_nothing,page:%ld\n",page);
	pthread_mutex_lock(&pal->alru_mutex);
	pt->disk_ios++;

	//record the latency
	/*
	if(pr->start_urio){
		int latency;
		long disk2;
		struct timeval now_time;

		pr->page_to_disk(page,alru.page_size,&disk2);
		gettimeofday(&now_time,NULL);
		latency=	1000000*(now_time.tv_sec-ppage->requested_time.tv_sec)
						+now_time.tv_usec-ppage->requested_time.tv_usec;
		printf("latency:%d\n",latency);
		//pr->lc[disk2].io_num++;
		pr->lc[disk2].avg_lat=	pr->lc[disk2].avg_lat*LATENCY_ARRAY_SIZE
												+latency-
												pr->lc[disk2].lat[pr->lc[disk2].io_num%LATENCY_ARRAY_SIZE];
		pr->lc[disk2].lat[pr->lc[disk2].io_num%LATENCY_ARRAY_SIZE]
											=	latency;
		pr->lc[disk2].io_num++;
	}*/
	pthread_mutex_unlock(&pal->alru_mutex);
	return ERR_SUCCESS_AC;
}

int inline alru_finish_read(long offset, long length, long disk){
	long page,rst,i;
	pstr_alru_page ppage=NULL;
	pstr_alg_alru	pal=(pstr_alg_alru)alru.alg;
	pstr_raid pr =	(pstr_raid)((pstr_acache)alru.pacache)->rpers;
	pstr_trace pt =	(pstr_trace)((pstr_acache)alru.pacache)->tpers;
	
	//printf("alru_finish_read, offset:%ld,length:%ld,disk:%ld\n",offset,length,disk);
	pr->io_to_page(&page,offset,length,disk,alru.page_size);
	//printf("alru_finish_read,page:%ld\n",page);
	pthread_mutex_lock(&pal->alru_mutex);
	pt->disk_ios++;
	rst=alru_find(page, &ppage, &alru);
	if(ALRU_MISS==rst){
		printf("alru bug, can't find the page\n");
	}else{
		if(IS_PAGE_DIRTY(ppage->state)){
			for(i=0;i<alru.page_size/BYTES_PER_BM_LONG;i++){
				ppage->bm_mask[i]	=	~0x0uL;
			}
			ppage->refs--;
			ppage->state	|= 0x1uL<<STATE_UPTODATE;
			//dl_list_remove_node(&pal->dirty_list,&ppage->alru_node);
			//dl_list_remove_node(&pal->dirty_ds_list[disk2],&ppage->alru_ds_node);
			//dl_list_add_node_to_head(&pal->dirty_list,&ppage->alru_node);
			//dl_list_add_node_to_head(&pal->dirty_ds_list[disk2],&ppage->alru_ds_node);
		}else{
			//we do not need to update the bm_masks here
			/*for(i=0;i<alru.page_size/BYTES_PER_BM_LONG;i++){
				ppage->bm_mask[i]	=	0x0uL;
			}*/
			//read a clean page means, it is caused by a read miss
			//the page has been already add to the head of the list
			ppage->refs--;
			ppage->state	|= 0x1uL<<STATE_UPTODATE;
			//dl_list_remove_node(&pal->clean_list,&ppage->alru_node);
			//dl_list_add_node_to_head(&pal->clean_list,&ppage->alru_node);
		}

		//record the latency
		/*if(pr->start_urio){
			int latency;
			long disk2;
			struct timeval now_time;

			pr->page_to_disk(page,alru.page_size,&disk2);
			gettimeofday(&now_time,NULL);
			latency=	1000000*(now_time.tv_sec-ppage->requested_time.tv_sec)
							+now_time.tv_usec-ppage->requested_time.tv_usec;
			//pr->lc[disk2].io_num++;
			pr->lc[disk2].avg_lat=	pr->lc[disk2].avg_lat*LATENCY_ARRAY_SIZE
													+latency-
													pr->lc[disk2].lat[pr->lc[disk2].io_num%LATENCY_ARRAY_SIZE];
			pr->lc[disk2].lat[pr->lc[disk2].io_num%LATENCY_ARRAY_SIZE]
												=	latency;
			pr->lc[disk2].io_num++;
		}*/
	}
	pthread_mutex_unlock(&pal->alru_mutex);
	pthread_cond_broadcast(&pr->io_finish_cond);
	return ERR_SUCCESS_AC;
}

int inline alru_finish_destage_read(long offset, long length, long disk){
	long page,rst,i;
	pstr_alru_page ppage=NULL;
	pstr_alg_alru	pal=(pstr_alg_alru)alru.alg;
	pstr_raid pr =	(pstr_raid)((pstr_acache)alru.pacache)->rpers;
	pstr_trace pt =	(pstr_trace)((pstr_acache)alru.pacache)->tpers;
	
	pr->io_to_page(&page,offset,length,disk,alru.page_size);
	
	pthread_mutex_lock(&pal->alru_mutex);
	pt->disk_ios++;
	rst=alru_find(page, &ppage, &alru);
	if(ALRU_MISS==rst){
		printf("alru bug, can't find the page\n");
	}else{
		if(IS_PAGE_DIRTY(ppage->state)){
			for(i=0;i<alru.page_size/BYTES_PER_BM_LONG;i++){
				ppage->bm_mask[i]	=	~0x0uL;
			}
			ppage->state |= 0x1uL<<STATE_UPTODATE;
			ppage->refs--;
			alru.destage_cnt--;
			//perform a full write
			//alru.rpers->handle_cache_request(ppage->page,alru.page_size,REQ_WRITE,alru_finish_destage_write,alru_call_back_do_nothing,&ppage->requested_time);
		}else{
			//bug here, a destage read is for a dirty page only
			printf("alru bug, destage read performed for a clean page\n");
		}

		//record the latency
		/*if(pr->start_urio){
			int latency;
			long disk2;
			struct timeval now_time;

			pr->page_to_disk(page,alru.page_size,&disk2);
			gettimeofday(&now_time,NULL);
			latency=	1000000*(now_time.tv_sec-ppage->requested_time.tv_sec)
							+now_time.tv_usec-ppage->requested_time.tv_usec;
			//pr->lc[disk2].io_num++;
			pr->lc[disk2].avg_lat=	pr->lc[disk2].avg_lat*LATENCY_ARRAY_SIZE
													+latency-
													pr->lc[disk2].lat[pr->lc[disk2].io_num%LATENCY_ARRAY_SIZE];
			pr->lc[disk2].lat[pr->lc[disk2].io_num%LATENCY_ARRAY_SIZE]
												=	latency;
			pr->lc[disk2].io_num++;
		}*/
	}
	pthread_mutex_unlock(&pal->alru_mutex);
	return ERR_SUCCESS_AC;
}

int inline alru_finish_destage_write(long offset, long length, long disk){
	long page,rst,i,hash_value,disk2;
	pstr_alru_page ppage=NULL;
	pstr_alg_alru	pal=(pstr_alg_alru)alru.alg;
	pstr_raid pr =	(pstr_raid)((pstr_acache)alru.pacache)->rpers;
	pstr_trace pt =	(pstr_trace)((pstr_acache)alru.pacache)->tpers;
	
	pr->io_to_page(&page,offset,length,disk,alru.page_size);
	
	pthread_mutex_lock(&pal->alru_mutex);
	pt->disk_ios++;
	rst=alru_find(page, &ppage, &alru);
	if(ALRU_MISS==rst){
		printf("alru bug, can't find the page\n");
	}else{
		if(IS_PAGE_DIRTY(ppage->state)){
			pr->page_to_disk(page,alru.page_size,&disk2);
			ppage->state |= 0x1uL<<STATE_UPTODATE;
			alru.destage_cnt--;
			ppage->refs--;
			if(0==ppage->refs){
				alru.dpage_cnt--;
				hash_value=ALRU_GET_HASH_VALUE(ppage->page,alru.hash_size);
				dl_list_remove_node(&pal->hash[hash_value], &ppage->hash_node);
				dl_list_remove_node(&pal->dirty_list,&ppage->alru_node);
				dl_list_remove_node(&pal->dirty_ds_list[disk2],&ppage->alru_ds_node);
				dl_list_add_node_to_head(&pal->free_dirty_list,&ppage->alru_node);
			}
		}else{
			//bug here, a destage read is for a dirty page only
			printf("alru bug, destage read performed for a clean page\n");
			exit(1);
		}

		//record the latency
		/*if(pr->start_urio){
			int latency;
			struct timeval now_time;

			gettimeofday(&now_time,NULL);
			latency=	1000000*(now_time.tv_sec-ppage->requested_time.tv_sec)
							+now_time.tv_usec-ppage->requested_time.tv_usec;
			//pr->lc[disk2].io_num++;
			pr->lc[disk2].avg_lat=	pr->lc[disk2].avg_lat*LATENCY_ARRAY_SIZE
													+latency-
													pr->lc[disk2].lat[pr->lc[disk2].io_num%LATENCY_ARRAY_SIZE];
			pr->lc[disk2].lat[pr->lc[disk2].io_num%LATENCY_ARRAY_SIZE]
												=	latency;
			pr->lc[disk2].io_num++;
		}*/
	}
	pthread_mutex_unlock(&pal->alru_mutex);
	pthread_cond_broadcast(&pal->destage_finish_cond);
	return ERR_SUCCESS_AC;
}

void* alru_destage(void* destage_arg){
	long i,hash_value;
	long bm_full;
	long pending_destage_pages=0;
	pstr_alg_alru pal=(pstr_alg_alru)alru.alg;
	pstr_raid pr =	(pstr_raid)((pstr_acache)alru.pacache)->rpers;
	pstr_alru_page ppage=NULL;
	struct timeval now_time,requested_time;
	struct timespec out_time;
	
	while(!alru.exit){
		/*if(	(alru.dpage_cnt>=alru.destage_threadhold -DESTAGE_GAP
					&&	alru.destage_cnt<MAX_DESTAGE_PAGES)
				||	(NULL==pal->free_dirty_list.head
					&&	alru.destage_cnt<MAX_DESTAGE_PAGES)){*/
		if(	(alru.dpage_cnt>=alru.destage_threadhold //-DESTAGE_GAP
					&&	alru.destage_cnt<pr->raid_disks)//&&	alru.destage_cnt<MAX_DESTAGE_PAGES)
				||	(NULL==pal->free_dirty_list.head
					&&	alru.destage_cnt<pr->raid_disks)){//&&	alru.destage_cnt<MAX_DESTAGE_PAGES)){
			pthread_mutex_lock(&pal->alru_mutex);
			ppage=NULL;
			while(NULL==ppage){
				ppage=find_dirty_alru_page(pal);
				//no candidate, we should wait for a signal, here we do not do that
				if(NULL==ppage){
					gettimeofday(&now_time, NULL);
					ADD_TIME_MSEC(out_time,now_time,10);
					pthread_cond_timedwait(&pr->io_finish_cond,&pal->alru_mutex, &out_time);
				}
				if(NULL==ppage){
					//printf("Error find dirty alru page!\n");
					//usleep(1000);
				}
			}
			//gettimeofday(&requested_time,NULL);
			//for simplistic we only consider full page write and read modify write here
			bm_full=1;
			for(i=0;i<alru.page_size/BYTES_PER_BM_LONG;i++){
				bm_full	=	bm_full && (	ppage->bm_mask[i]	==	(~0x0uL));
			}
			ppage->state	&= ~(0x1uL<<STATE_UPTODATE);

			/************
			the following codes are only for raid10
			*************/
			/*if(bm_full)
				ppage->refs+=2;
			else*/
				ppage->refs++;
			alru.destage_cnt++;
			//ppage->requested_time.tv_sec=requested_time.tv_sec;
			//ppage->requested_time.tv_usec=requested_time.tv_usec;
			gettimeofday(&ppage->requested_time,NULL);
			gettimeofday(&requested_time,NULL);
			pthread_mutex_unlock(&pal->alru_mutex);
				
			if(bm_full){
				//perform a full write
				//printf("destage perform a full write\n");
				pr->handle_cache_request(ppage->page,alru.page_size,REQ_WRITE,alru_finish_destage_write,alru_call_back_do_nothing,&requested_time);
			}else{
				//perform a read modify write
				//printf("destage perform a read modify write\n");
				
				pr->handle_cache_request(ppage->page,alru.page_size,REQ_DESTAGE_READ,alru_finish_destage_read,alru_call_back_do_nothing,&requested_time);
			}
			
			//msleep(10);
			/*pthread_mutex_lock(&pal->alru_mutex);
			ppage->state	|= (0x1uL<<STATE_UPTODATE);
			ppage->refs--;
			for(i=0;i<alru.page_size/BYTES_PER_BM_LONG;i++){
				ppage->bm_mask[i]=(~0x0uL);
			}
			
			if(0==ppage->refs){
				hash_value=ALRU_GET_HASH_VALUE(ppage->page,alru.hash_size);
				dl_list_remove_node(&pal->hash[hash_value], &ppage->hash_node);
				dl_list_remove_node(&pal->dirty_list, &ppage->alru_node);
				dl_list_add_node_to_head(&pal->free_dirty_list, &ppage->alru_node);
				alru.dpage_cnt--;
			}
			pthread_mutex_unlock(&pal->alru_mutex);
			pthread_cond_broadcast(&pal->destage_finish_cond);*/
		}else{
			pthread_mutex_lock(&pal->alru_mutex);
			pthread_cond_signal(&pal->destage_finish_cond);
			gettimeofday(&now_time, NULL);
			ADD_TIME_MSEC(out_time,now_time,10);
			pthread_cond_timedwait(&pal->destage_cond,&pal->alru_mutex,&out_time);
			pthread_mutex_unlock(&pal->alru_mutex);
		}
	}
}

#define GS_STACK 0
#define DS_STACK 1

int inline alru_print_stack(pstr_dldsc pdld,char *stackname,long stacktype){
	pstr_dlnode pn=NULL, ph=NULL;
	printf("%s stack start\n",stackname);
	if(NULL!=pdld->head){
		pn=ph=pdld->head;
		do{
			if(GS_STACK==stacktype){
				printf("page id:%ld\t\n",CONTAINER_OF(pn,alru_node,str_alru_page)->page);
				//printf("page state:%ld\t",CONTAINER_OF(pn,alru_node,str_alru_page)->state);
				//printf("page refs:%ld\t\n",CONTAINER_OF(pn,alru_node,str_alru_page)->refs);
				/*for(i=0;i<pa->page_size/BYTES_PER_BM_LONG;i++){
					printf("page mask %ld:%lx\n",i,CONTAINER_OF(pn,alru_node,str_alru_page)->bm_mask[i]);
				}*/
			}else{
				printf("page id:%ld\t",CONTAINER_OF(pn,alru_ds_node,str_alru_page)->page);
				printf("page state:%ld\t",CONTAINER_OF(pn,alru_ds_node,str_alru_page)->state);
				printf("page refs:%ld\t\n",CONTAINER_OF(pn,alru_ds_node,str_alru_page)->refs);
			}
			pn=pn->next;
			
		}while(pn!=ph);
	}
	printf("%s stack end\n",stackname);
}

int inline alru_state(pstr_algorithm pa){
	long i;
	char stackname[100];
	pstr_alg_alru pal	= (pstr_alg_alru)pa->alg;
	pstr_dlnode pn=NULL, ph=NULL;
	pstr_raid pr =	(pstr_raid)((pstr_acache)alru.pacache)->rpers;
	
	printf("alru max dpages: %ld\n",pa->dpages);
	printf("alru dpage_cnt: %ld\n",pa->dpage_cnt);
	printf("alru destage_cnt: %ld\n",pa->destage_cnt);

	sprintf(stackname,"dirty list");
	alru_print_stack(&pal->dirty_list,stackname,0);

	for(i=0;i<pr->raid_disks;i++){
		sprintf(stackname,"dirty ds list %ld",i);
		alru_print_stack(&pal->dirty_ds_list[i],stackname,1);
	}

	sprintf(stackname,"clean list");
	alru_print_stack(&pal->clean_list,stackname,0);

	for(i=0;i<pr->raid_disks;i++){
		sprintf(stackname,"clean ds list %ld",i);
		alru_print_stack(&pal->clean_ds_list[i],stackname,1);
	}
	
	return ERR_SUCCESS_AC;
}

int inline alru_check_one_stack(pstr_dldsc pdld,char *stackname,long stacktype,long disk){
	long page,disk2;
	pstr_raid pr =	(pstr_raid)((pstr_acache)alru.pacache)->rpers;
	pstr_dlnode pn=NULL, ph=NULL;
	
	if(NULL!=pdld->head){
		pn=ph=pdld->head;
		do{
			if(DS_STACK==stacktype){
				page=CONTAINER_OF(pn,alru_ds_node,str_alru_page)->page;
				pr->page_to_disk(page,alru.page_size,&disk2);
				if(disk!=disk2){
					printf("page %ld is not in the disk stack %ld, but in the disk stack %ld\n",page,disk2,disk);
				}
			}
			pn=pn->next;
			
		}while(pn!=ph);
	}
	return ERR_SUCCESS_AC;
}

	
int inline alru_check_stack(pstr_algorithm pa){
	long i;
	char stackname[100];
	pstr_alg_alru pal	= (pstr_alg_alru)pa->alg;
	pstr_dlnode pn=NULL, ph=NULL;
	pstr_raid pr =	(pstr_raid)((pstr_acache)alru.pacache)->rpers;

	for(i=0;i<pr->raid_disks;i++){
		sprintf(stackname,"dirty ds list %ld",i);
		alru_check_one_stack(&pal->dirty_ds_list[i],stackname,1,i);
	}

	for(i=0;i<pr->raid_disks;i++){
		sprintf(stackname,"clean ds list %ld",i);
		alru_check_one_stack(&pal->clean_ds_list[i],stackname,1,i);
	}
	
	return ERR_SUCCESS_AC;
}

int inline connect_alru(void* pac){
	((pstr_acache)pac)->apers	=	(void*)&alru;
	alru.pacache	=	pac;
	return ERR_SUCCESS_AC;
}
