#ifndef DL_LIST_H
#define DL_LIST_H

#define ERR_SUCCESS 0

#define CONTAINER_OF(pmember,member,ctype)	\
			((ctype*)((long)(pmember) - (long)&(((ctype*)0)->member)))
			
typedef struct dl_list_node{
	struct dl_list_node *next;
	struct dl_list_node *pre;
}str_dlnode,*pstr_dlnode,**ppstr_dlnode;
#define STRDLNODESIZE sizeof(str_dlnode)
#define PSTRDLNODESIZE sizeof(pstr_dlnode)
#define PPSTRDLNODESIZE sizeof(ppstr_dlnode)

typedef struct dl_list_descriptor{
	str_dlnode *head;
}str_dldsc,*pstr_dldsc,**ppstr_dldsc;
#define STRDLDSCSIZE sizeof(str_dldsc)
#define PSTRDLDSCSIZE sizeof(pstr_dldsc)
#define PPSTRDLDSCSIZE sizeof(ppstr_dldsc)

int dl_list_add_node_to_head(pstr_dldsc pdldsc,pstr_dlnode pdlnode);
int dl_list_add_node_to_tail(pstr_dldsc pdldsc,pstr_dlnode pdlnode);
int dl_list_remove_node(pstr_dldsc pdldsc,pstr_dlnode pdlnode);

#endif
