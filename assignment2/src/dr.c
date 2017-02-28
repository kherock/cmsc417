#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <unistd.h>
#include <strings.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "es.h"
#include "n2h.h"

long alloc_read (char **s, char *fname);
void usage(char *err_msg, char* name);
int  parse_arg(int argc, char **argv);
void parser_init(char *sc_file);
void ru_scan_string (char *sc);

char *sc_file;
extern int ruparse();

unsigned int update_time = 3;
unsigned int time_between_sets = 30;
unsigned int verbose = 0;
//FILE *ConfigFile;

int main (int argc, char *argv[])
{
	/* check cmd-line arguments, will exit on any error */
	parse_arg(argc, argv);

	parser_init(sc_file);
	ruparse();

	walk_el(update_time, time_between_sets, verbose);
	return 0;
}

long alloc_read (char **s, char *fname)
{
	int fd = -1;
	struct stat sbuf;
	char *fin;
  
	fd = open(fname, O_RDONLY);
	if (fd < 0){
		fprintf(stderr, "alloc-read: open failure\n");
		exit(1);
	}
	fstat (fd, &sbuf);
  
	fin = (char *) malloc (sbuf.st_size+1);
	if (!fin) {
		fprintf(stderr, "alloc-read: malloc failure\n");
		exit(1);
	}
	if (fd) {
		read (fd, fin, sbuf.st_size);
		close (fd);
	}
	fin[sbuf.st_size]=0x0;
	*s = fin;
	return sbuf.st_size;
}
 
void parser_init(char *sc_file)
{
	char *sc;

	alloc_read (&sc, sc_file);
	// puts contents of sc_file in string sc
	ru_scan_string (sc);
	//yy_scan_string (sc);
	// parser is set to parse from sc
}


/*[]------------------------------------------------------------------[]
  [] dr -n <my_node_id> -f <config_file>
  []------------------------------------------------------------------[]*/ 
extern char *optarg;
extern int  opterr, optind, optopt;

int parse_arg(int argc, char **argv)
{
	char  opt_char;
	bool  got_config = false;
	bool  got_myid = false;

	/* to turn off default report of illegal option, uncomment the next line */
	/* opterr = 0; */
	while ((opt_char = getopt(argc, argv, "n:f:u:t:v")) != EOF) {
		switch (opt_char) {
			case 'n':
				set_myid (atoi(optarg));
				got_myid = true;
				break;
			case 'f':
				sc_file = optarg; 
				got_config = true;
				break;
			case 'u':
				update_time = atoi(optarg); 
				break;
			case 't':
				time_between_sets = atoi(optarg); 
				break;
			case 'v':
				//verbose = atoi(optarg); 
				verbose = 1; 
				break;
			case '?':
				usage("", argv[0]);
				break;
		}
	}
	if (optind != argc)
		usage("", argv[0]);

	if (!got_myid)
		usage("", argv[0]);

	if (!got_config)
		sc_file = DefaultConfigFile;

	///
		//ConfigFile = fopen(DefaultConfigFile, "r");
    
		return optind;
}

/*[]------------------------------------------------------------------[]
  []------------------------------------------------------------------[]*/ 
void usage(char *err_msg, char *name)
{
	fprintf(stderr, "\n%s\nUsage: %s -n <my_node_id> [-f <config_file>] [-u update_time] [-t time_between_updates] [-v]\n",
		err_msg, name);
	exit(1);
}
