#ifndef _ES_C_
#define _ES_C_

/* $Id: es.c,v 1.1 2000/03/01 14:09:09 bobby Exp bobby $
 * ----
 * $Revision: 1.1 $
 */

#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <string.h>

#include "queue.h"
#include "common.h"
#include "es.h"
#include "ls.h"
#include "rt.h"
#include "n2h.h"

static struct el *g_el;

int init_new_el() {
	InitDQ(g_el, struct el);
	assert (g_el);

	g_el->es_head = 0x0;
	return (g_el != 0x0);
}

void add_new_es() {
	struct es *n_es;
	struct el *n_el = (struct el*)getmem(sizeof(struct el));

	struct el *tail = g_el->prev;
	InsertDQ(tail, n_el);

	// added new es to tail
	// lets start a new queue here
  
	{
		struct es *nhead = tail->es_head;
		InitDQ(nhead, struct es);

		tail = g_el->prev;     
		tail->es_head = nhead; 

		n_es = nhead;

		n_es->ev = _es_null;
		n_es->peer0 = n_es->peer1 =
		n_es->port0 = n_es->port1 =
		n_es->cost = -1;
		n_es->name = 0x0;
	}
}

void add_to_last_es(e_type ev, node peer0, int port0, node peer1, int port1, int cost, char *name) {
	struct el *tail = g_el->prev;
	bool local_event = false;
  
	assert(tail->es_head);

	// check for re-defined link (for establish)
	// check for local event (for tear-down, update)
	switch (ev) {
	case _es_link:
		// a local event?
		if (peer0 == get_myid() || peer1 == get_myid())
			local_event = true;
		break;
	case _ud_link:
		// a local event?
		if (geteventbylink(name))
			local_event = true;
		break;
	case _td_link:
		// a local event?
		if (geteventbylink(name))
			local_event = true;
		break;
	default:
		printf("[es]\t\tUnknown event!\n");
		break;
	}

	if (!local_event) {
		printf("[es]\t Not a local event, skip\n");
		return;
	}

	printf("[es]\t Adding into local event\n");

	{
		struct es *es_tail = tail->es_head->prev;
    
		struct es *n_es = (struct es *)getmem(sizeof(struct es));
    
		n_es->ev = ev;
		n_es->peer0 = peer0;
		n_es->port0 = port0;
		n_es->peer1 = peer1;
		n_es->port1 = port1;
		n_es->cost = cost;
		n_es->name = (char *)getmem(strlen(name) + 1);
		strcpy(n_es->name, name);

		InsertDQ(es_tail, n_es);
	}
}

/*
 * A simple walk of event sets: dispatch and print a event SET every 2 sec
 */
void walk_el(int update_time, int time_between, int verb) {
	struct el *el;
	struct es *es_hd;
	struct es *es;

	struct pollfd sds[64];
	size_t sds_len = 0;

	uint8_t *buf = malloc(65536);
	buf[0] = ntohl(0x7);
	buf[1] = ntohl(0x1);

	UNUSED(verb);

	assert(g_el->next);
  
	print_el();

	/* initialize link set, routing table, and routing table */
	create_ls();
	create_rt();
	init_rt_from_n2h();
	
	for (el = g_el->next; el != g_el; el = el->next) {
		assert(el);
		es_hd = el->es_head;
		assert (es_hd);
	
		printf("[es] >>>>>>>>>> Dispatch next event set <<<<<<<<<<<<<\n");
		for (es = es_hd->next; es != es_hd; es=es->next) {
			printf("[es] Dispatching next event ... \n");
			dispatch_event(es);
		}

		sds_len = 0;
		// Add peers to pollfd list
		for (struct link *ls = g_ls->next; ls != g_ls; ls = ls->next) {
			if (is_me(ls->peer0)) {
				sds[sds_len].fd = ls->peer1;
				sds[sds_len].events = POLLIN | POLLOUT;
				sds_len++;
			} else if (is_me(ls->peer1)) {
				sds[sds_len].fd = ls->peer0;
				sds[sds_len].events = POLLIN | POLLOUT;
				sds_len++;
			}
		}

		*(uint16_t *)&buf[2] = 0;
		for (struct rte *route = g_rt->next; route != g_rt; route = route->next) {
			uint16_t *i = (uint16_t *)&buf[2];
			buf[4] = htonl(route->d);
			*(uint16_t *)&buf[4 * *i + 2] = htonl(route->c);
			(*i)++;
		}
		*(uint16_t *)&buf[2] = htonl(*(uint16_t *)&buf[2]);

		/* Run DISTANCE VECTOR ALGORITHM */
		int done = 0;
		int timeout = update_time * 1000;
		while (!done) switch (poll(sds, sds_len, timeout)) {
			case -1:
				perror("poll() failed");
				exit(1);
			case 0:
				// send out update
				break;
			default:;
				int num_routes = ntohl(*(uint16_t *)&buf[2]);
				for (int i = 0; i < sds_len; i++) {
					if (sds[i].revents & POLLOUT) {
						send(sds[i].fd, buf, 4 + 4 * num_routes, 0);
					}
				}
		}

		printf("[es] >>>>>>> Start dumping data stuctures <<<<<<<<<<<\n");
		print_n2h();
		print_ls();
		print_rt();
	}
}

/*
 * -------------------------------------
 * Dispatch one event
 * -------------------------------------
 */
void dispatch_event(struct es *es) {
	assert(es);

	switch (es->ev) {
	case _es_link:
		add_link(es->peer0, es->port0, es->peer1, es->port1, es->cost, es->name);
		break;
	case _ud_link:
		ud_link(es->name, es->cost);
		break;
	case _td_link:
		del_link(es->name);
		break;
	default:
		printf("[es]\t\tUnknown event!\n");
		break;
	}

}

/*
 * print out the whole event LIST
 */
void print_el() {
	struct el *el;
	struct es *es_hd;
	struct es *es;

	assert (g_el->next);

	printf("\n\n");
	printf("[es] >>>>>>>>>>>>>>>>>>>>>><<<<<<<<<<<<<<<<<<<<<<<<<<\n");
	printf("[es] >>>>>>>>>> Dumping all event sets  <<<<<<<<<<<<<\n");
	printf("[es] >>>>>>>>>>>>>>>>>>>>>><<<<<<<<<<<<<<<<<<<<<<<<<<\n");

	for (el = g_el->next; el != g_el; el = el->next) {

		assert(el);
		es_hd = el->es_head;
		assert (es_hd);
	
		printf("\n[es] ***** Dumping next event set *****\n");

		for (es=es_hd->next; es!=es_hd; es=es->next)
			print_event(es);
	}     
}

/*
 * print out one event: establish, update, or, teardown
 */
void print_event(struct es *es) {
	assert(es);

	switch (es->ev) {
	case _es_null:
		printf("[es]\t----- NULL event -----\n");
		break;
	case _es_link:
		printf("[es]\t----- Establish event -----\n");
		break;
	case _ud_link:
		printf("[es]\t----- Update event -----\n");
		break;
	case _td_link:
		printf("[es]\t----- Teardown event -----\n");
		break;
	default:
		printf("[es]\t----- Unknown event-----\n");
		break;
	}
	printf("[es]\t link-name(%s)\n",es->name);
	printf("[es]\t node(%d)port(%d) <--> node(%d)port(%d)\n", 
		es->peer0, es->port0, es->peer1, es->port1);
	printf("[es]\t cost(%d)\n", es->cost);
}

struct es *geteventbylink(char *lname) {
	struct el *el;
	struct es *es_hd;
	struct es *es;

	assert (g_el->next);
	assert (lname);

	for (el = g_el->next ; el != g_el ; el = el->next) {
		assert(el);
		es_hd = el->es_head;
		assert (es_hd);
	
		for (es=es_hd->next ; es!=es_hd ; es=es->next)
			if (!strcmp(lname, es->name))
				return es;
	}
	return 0x0;
}

#endif

