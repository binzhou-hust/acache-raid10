#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include "acache.h"
#include "error.h"
#include "raid.h"
#include "algorithm.h"
#include "lru.h"
#include "alru.h"

int connect_algorithm(void* pac, char* algorithm_name){
	//check if trace name is null character string
	pstr_algorithm pa=NULL;
	if(strlen(algorithm_name)<1){
		printf("%ld",strlen(algorithm_name));
		return ERR_INVALIDALGORITHM_AC;
	}
	//printf("connect_algorithm\n");

	if(0==strcmp(algorithm_name,"lru")){
		//printf("connect_algorithm 2\n");
		connect_lru(pac);
		//printf("connect_lru ok!");
		pa=((pstr_acache)pac)->apers;
		pa->exit	=	0;
		pa->page_size	=	64*1024;
		//web2 r:189953 w:6 all:189959 dev_cap:20*1024*1024*1024
		//web3 remove bad line:#line 4261524 r:189967 w:8 all:189975 dev_cap:20*1024*1024*1024
		//fin1: remove lines:#line 4663812 4663813 4663814 4673484 r:25925 w:60017 all:85942 dev_cap:1*1024*1024*1024 
		//fin2: w:35732 r:22262 all:57994 dev_cap:1*1024*1024*1024
		//dtrs: w:2540175 r:1359270 all:3899445
		//lmtbe: w:1108709 r:987070 all:2095779 dev_cap:
		//dads: w:3711 r:69844 all:73555 dev_cap:
		//dap: low reaccesses w:591003 r:592311 dev_cap:
		pa->pages 	=	1L*65536;
		pa->hash_size	=	pa->pages;
		pa->dpages	=	pa->pages/2;
		pa->dpage_cnt	=	0;
		pa->destage_cnt	=	0;
		pa->destage_threadhold	=	pa->dpages*9/10;
		//pa->max_destage_num= pa->dpages/10;
		pa->alg=NULL;
		pa->alg=(void*)malloc(STRALGLRUSIZE);
		if(NULL==pa->alg)
			return ERR_NOMEM_AC;
		//pa->rpers	=	pr;
	}else if(0==strcmp(algorithm_name,"alru")){
		//printf("connect_algorithm 2\n");
		connect_alru(pac);
		//printf("connect_lru ok!");
		pa=((pstr_acache)pac)->apers;
		pa->exit	=	0;
		pa->page_size	=	64*1024;
		pa->pages 	=	1L*65536;
		pa->hash_size	=	pa->pages;
		pa->dpages	=	pa->pages/2;
		pa->dpage_cnt	=	0;
		pa->destage_cnt	=	0;
		pa->destage_threadhold	=	pa->dpages*9/10;
		//pa->max_destage_num= pa->dpages/10;
		pa->alg=NULL;
		pa->alg=(void*)malloc(STRALGALRUSIZE);
		if(NULL==pa->alg)
			return ERR_NOMEM_AC;
		//pa->rpers	=	pr;
	}else{
		return ERR_INVALIDTRACE_AC;
	}
	
	return ERR_SUCCESS_AC;
}
