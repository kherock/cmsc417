%{
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <assert.h>
#include "es.h"
#include "ls.h"
#include "n2h.h"

extern char *rutext;
int rulex (void *x);
int ruerror(char *s);
int ru_line_num = 1;
%}

%pure-parser

%union
{
  char str[1024];
  int n;
}



%token open_paren
%token close_paren

%token establish_link
%token teardown_link
%token update_link

%token token_node
%token token_port
%token token_cost
%token token_name

%token nl

%token <str> name_t
%token <n> number

%%

ru: 
{
}
|
node_line ru
{
    // identify myself
    if (is_me(get_myid()) == false) {
	printf("[ru] ==> given nodeid(%d)host(%s) is not localhost\n",
	        get_myid(), gethostbynode(get_myid()));
        exit(1);
    }

}
|
open_paren_n event_set close_paren nl ru
{
    //printf ("[ru]\tparsed an event set\n");
}
|
open_paren_n nl event_set close_paren nl ru
{
    //printf ("[ru]\tparsed an event set\n");
}
;

open_paren_n:
open_paren
{
    printf ("\n\n[ru]\tcreating a new (empty) event set\n");
    add_new_es();
}


node_line: 
token_node number name_t nl
{
    static char init_done = false;

    if (init_done == false) {
	// init node->hostname mapping
	// init event list
	printf("[ru]\tInit structures...\n");
	create_n2h();
	init_new_el();
	init_done = true;
    }

    // add to node_to_hostname
    assert (add_n2h($2, $3));

    printf ("[ru]\tFound node %d %s\n", $2, $3);

};

event_set: 
|
es_link event_set 
{
}
|
td_link event_set 
{
}
|
ud_link event_set
{
}
;


es_link: establish_link token_node number token_port number token_node number token_port number token_cost number token_name name_t nl
{
    printf ("[ru]\tEstablish link between %d [%d] <--> %d [%d] cost %d name %s\n",
	    $3, $5, $7, $9, $11, $13);
    /* add to event set */
    /* peer0, port0, peer1, port1, cost, name */
    add_to_last_es(_es_link, $3, $5, $7, $9, $11, $13);
}


td_link: teardown_link name_t nl
{
    printf ("[ru]\tTeardown link %s\n", $2);

    /* add to event set */
    /* peer0, port0, peer1, port1, cost, name */
    add_to_last_es(_td_link, -1, -1, -1, -1, -1, $2);
}

ud_link: update_link name_t token_cost number  nl
{
    printf ("[ru]\tUpdate link %s new cost %d\n", $2, $4);

    /* add to event set */
    /* peer0, port0, peer1, port1, cost, name */
    add_to_last_es(_ud_link, -1, -1, -1, -1, $4, $2);
};

%%
 
int ruerror(char *s) {
    fprintf (stdout,"ru:%s::token :<%s> :line number %d\n", s, rutext, 
	     ru_line_num);
    if (strlen(rutext) == 0){
	fprintf (stdout,"No EOF at end of file??\n");
    }
    return 0;
}

