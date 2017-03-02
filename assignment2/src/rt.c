/* $Id: rt.c,v 1.2 2000/02/23 00:51:25 bobby Exp bobby $
 * QD DQ implementation of RT
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "common.h"
#include "rt.h"
#include "queue.h"

#define logf (stdout)

int create_rt(){
	InitDQ(g_rt, struct rte);
	assert (g_rt);
	g_rt->d =  g_rt->c = g_rt ->nh = -1;
	return (g_rt != 0x0);
}

int add_rte(node n, cost c, node nh){
	struct rte* ne = (struct rte *) getmem (sizeof (struct rte));
	ne->d = n;
	ne->c = c;
	ne->nh = nh;
  
	InsertDQ(g_rt, ne);
	return (ne != 0x0);
}

struct rte *find_rte(node n){
	struct rte *i;
  
	for (i = g_rt->next; i != g_rt; i = i->next){
		if (i->d == n)
			break;
	}
	if (i->d == n){
		return i;
	}
	else{
		return 0x0;
	}
}

int update_rte(node n, cost c, node nh){
	struct rte *i = find_rte(n);

	if (i->d == n){
		i->c = c;
		i->nh = nh;
		return 0;
	}
	else{
		return -1;
	}
}

int del_rte(node n){
	struct rte *i = find_rte(n);
  
	if (i->d == n){
		DelDQ(i);
		free(i);
		return 0;
	}
	else{
		return -1;
	}
}

/* print route */
void print_rte(struct rte* i)
{
	assert (i);
	fprintf (logf, "[rt]\tNode %d  Cost %d NextHop %d\n", 
		i->d, i->c, i->nh);
	return;
  
}

/* print routing table */
void print_rt()
{
	struct rte *i;

	fprintf (logf, "\n-- Routing table --\n");

	for (i = g_rt->next; i != g_rt; i = i->next){
		print_rte(i);
	}
}


