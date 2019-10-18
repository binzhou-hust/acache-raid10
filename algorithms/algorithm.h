#ifndef ALGORITHMS_H
#define ALGORITHMS_H


#include "request_io.h"
#include "raid.h"

#define ALGORITHMNAMELEN 20
#define DESTAGE_GAP 8
#define MAX_DESTAGE_PAGES 12

typedef struct algorithm{
	char name[ALGORITHMNAMELEN];
	long exit;
	long page_size;
	long pages;
	long hash_size;
	long dpages;
	long dpage_cnt;
	long destage_cnt;
	long destage_threadhold;
	//long max_destage_num;
	void* pacache;																																//point to a detail struct to describe the algorithm's properties
	void* alg;
//	pstr_raid rpers;																													//pointer to underline raid instance
	int (*init_algorithm)(struct algorithm *pa);															//init mutexs, open the files, init the threads, init the disk io list
	int (*reclaim_algorithm)(struct algorithm *pa);														//close the files, reclaim the disk io list, close all threads
	int (*replace)(struct algorithm *pa, pstr_user_request pur);							//start the recovery process
}str_algorithm,*pstr_algorithm,**ppstr_algorithm;
#define STRALGORITHMSIZE sizeof(str_algorithm)
#define PSTRALGORITHMSIZE sizeof(pstr_algorithm)
#define PPSTRALGORITHMSIZE sizeof(ppstr_algorithm)

//#define PAGE_CLEAN 0
//#define PAGE_DIRTY 1

int connect_algorithm(void* pac, char* algorithm_name);

#endif
