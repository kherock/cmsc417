/* $Id: ls.h,v 1.1 2000/02/23 01:00:30 bobby Exp bobby $
 * Link Set
 */

#ifndef _N2H_H_
#define _N2H_H_

struct n2h {
    struct n2h *next;
    struct n2h *prev;
    node nid;    // node id
    char *hostname;  // hostname
};

// interface
char *gethostbynode(node nid);
int  bind_port(int port);

// internal
int create_n2h();
int add_n2h(node nid, char *hostname);
void print_n2h();
node get_myid();
void set_myid (node myid);
int init_rt_from_n2h();
bool is_me(node nid); // is nid on my machine?

#endif
