/* $Id$
 * Routing Table
 */
#ifndef _RT_H_
#define _RT_H_

struct rte{
    struct rte *next;  // next entry
    struct rte *prev;  // prev entry
    node d;  // dest
    cost c;  // cost
    node nh; // next hop
};

struct rte* g_rt;

int create_rt();
int add_rte(node n, cost c, node nh);
int update_rte(node n, cost c, node nh);
int del_rte(node n);
struct rte *find_rte(node n);
void print_rte(struct rte* i);
void print_rt();

#endif
