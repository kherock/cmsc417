/* $Id: ls.h,v 1.1 2000/02/23 01:00:30 bobby Exp bobby $
 * Link Set
 */

#ifndef _LS_H_
#define _LS_H_

struct link {
    struct link *next;  // next entry
    struct link *prev;  // prev entry
    node peer0, peer1;  // link peers
    int  port0, port1;
    int  sockfd0;       // if peer0 is itself, local port is bound
    int  sockfd1;       // if peer1 is itself, local port is bound
    cost c;		// cost
    char *name;
};


int create_ls();
int add_link(node peer0, int port0, node peer1, int port1, 
	     cost c, char *name);
int del_link(char *n);

struct link *find_link(char *n);

void print_link(struct link* i);
void print_ls();
struct link *ud_link(char *n, int cost);

#endif
