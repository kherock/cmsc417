%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "ru.tab.h"

#define YY_DECL int yylex ( YYSTYPE *lvalp) 
#define YY_NO_UNPUT

//#define ruwrap() 1
int ruwrap(void) { return 1; }

extern int ru_line_num;
%}

%option noinput
%option nounput

ws      [ \t]+
num     -?[0-9]+|([0-9]*\.[0-9]+)([eE][-+]?[0-9]+)?

name_t    [a-zA-Z_][-a-zA-Z0-9_.]*

ipaddr  [0-9]+\.[0-9]+\.[0-9]+\.[0-9]+


nl      [\n]+
op      \(
cp      \)
ob      \{
cb      \}
comment ";".*"\n"

%%

{comment}       {ru_line_num++;}
{ws}            ;
{nl}            {ru_line_num++; return nl;}

{op}            { return open_paren; }
{cp}            { return close_paren; }

{num}           { lvalp->n = atoi(rutext);
                    return number;
                }



"node"        {  strcpy(lvalp->str, rutext); 
                   return token_node; }
"name"        {  strcpy(lvalp->str, rutext); 
                   return token_name; }

"cost"        {  strcpy(lvalp->str, rutext); 
                   return token_cost; }
"port"        {  strcpy(lvalp->str, rutext); 
                   return token_port; }

"establish"        {  strcpy(lvalp->str, rutext); 
                   return establish_link; }

"tear-down"       {  strcpy(lvalp->str, rutext); 
                   return teardown_link; }

"update"       {  strcpy(lvalp->str, rutext); 
                   return update_link; }

{name_t}         {
                  strcpy(lvalp->str, rutext);
                  return name_t;
               }

.               { printf ("error in lexer : %s\n", rutext);
                }
%%
