/* $Id: ls.h,v 1.1 2000/02/23 01:00:30 bobby Exp $
 * Link Set
 */

#ifndef _LS_C_
#define _LS_C_

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "common.h"
#include "ls.h"
#include "queue.h"
#include "n2h.h"
#include "rt.h"

int create_ls() {
	InitDQ(g_ls, struct link);
	assert (g_ls);
  
	g_ls->peer0 =  g_ls->peer1 = g_ls ->c = -1;
	g_ls->name = 0x0;
  
	return (g_ls != 0x0);
}

int add_link(node peer0, int port0, node peer1, int port1, cost c, char *name) {
	struct link *nl = (struct link *)getmem(sizeof(struct link));
	nl->peer0 = peer0;
	nl->port0 = port0;
	nl->peer1 = peer1;
	nl->port1 = port1;
	nl->c = c;
	nl->name = (char *)malloc(strlen(name)+1);
	strcpy(nl->name, name);

	if (peer0 == get_myid())
	 	nl->sockfd0 = bind_port(port0);
	else
	   	nl->sockfd0 = -1;
	if (peer1 == get_myid())
	 	nl->sockfd1 = bind_port(port1);
	else
   		nl->sockfd1 = -1;

	InsertDQ(g_ls, nl);
	return (nl != 0x0);
}


struct link *ud_link(char *n, int cost) {
	struct link *i = find_link(n);

	assert(i);
	i->c = cost;
	return i;
}


struct link *find_link(char *n) {
	struct link *i;
	for (i = g_ls->next; i != g_ls; i= i->next) {
		if (!(strcmp(i->name, n))) {
			break;
		}
	}
	if (!strcmp(i->name, n)) {
		return i;
	} else {
		return 0x0;
	}
}

int del_link(char *name) {
	struct link *i = find_link(name);
	assert (i);
	DelDQ(i);
	free(i->name);
	free(i);
	return 0x0;
}


void print_link(struct link* i) {
	fprintf (stdout, "[ls]\t ----- link name(%s) ----- \n", i->name);
	fprintf (stdout, "[ls]\t node(%d)host(%s)port(%d) <--> node(%d)host(%s)port(%d)\n",
		i->peer0, gethostbynode(i->peer0), i->port0,
		i->peer1, gethostbynode(i->peer1), i->port1);
	fprintf (stdout, "[ls]\t cost(%d), sock1(%d) sock2(%d)\n",
		i->c, i->sockfd0, i->sockfd1);
}

void print_ls() {
	struct link *i;

	fprintf (stdout, "\n[ls] ***** dumping link set *****\n");
	for (i=g_ls->next; i != g_ls; i=i->next){
		assert(i);
		print_link (i);
	}
}

#endif
