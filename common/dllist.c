#include<stdio.h>
#include<stdlib.h>

#include "dllist.h"

int inline dl_list_add_node_to_head(pstr_dldsc pdldsc,pstr_dlnode pdlnode){
	if(NULL==pdldsc->head){
		//
		pdlnode->next=pdlnode->pre=pdlnode;
		pdldsc->head=pdlnode;
	}else{
		//
		pdlnode->next=pdldsc->head;
		pdlnode->pre=pdldsc->head->pre;
		pdldsc->head->pre->next=pdlnode;
		pdldsc->head->pre=pdlnode;
		pdldsc->head=pdlnode;
	}
	return ERR_SUCCESS;
}

int inline dl_list_add_node_to_tail(pstr_dldsc pdldsc,pstr_dlnode pdlnode){
	if(NULL==pdldsc->head){
		//
		pdlnode->next=pdlnode->pre=pdlnode;
		pdldsc->head=pdlnode;
	}else{
		//
		pdlnode->next=pdldsc->head;
		pdlnode->pre=pdldsc->head->pre;
		pdldsc->head->pre->next=pdlnode;
		pdldsc->head->pre=pdlnode;
	}
	return ERR_SUCCESS;
}

int inline dl_list_remove_node(pstr_dldsc pdldsc,pstr_dlnode pdlnode){
	if(pdlnode==pdldsc->head){
		//pdlnode
		if(pdlnode==pdlnode->next){
			//
			pdlnode->next=pdlnode->pre=NULL;
			pdldsc->head=NULL;
		}else{
			pdlnode->next->pre=pdlnode->pre;
			pdlnode->pre->next=pdlnode->next;
			pdldsc->head=pdlnode->next;
			//
			pdlnode->next=pdlnode->pre=NULL;
		}
	}else{
		//
		pdlnode->next->pre=pdlnode->pre;
		pdlnode->pre->next=pdlnode->next;
		//
		pdlnode->next=pdlnode->pre=NULL;
	}
	return ERR_SUCCESS;
}
