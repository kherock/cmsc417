CC=gcc
CFLAGS=-Wall -Iincludes -ggdb -std=gnu99
BISON=bison
FLEX=flex
SRC=rt.c es.c ls.c n2h.c dr.c
OBJ=$(SRC:.c=.o) ru.tab.o lex.ru.o
VPATH=src

all:		rt

.SUFFIXES: 	.c .o
%.c.o:
		$(CC) -c $(CFLAGS) $*.c

%ru.tab.c: $*ru.y $*ru.l
		$(BISON) --verbose -d -b $*ru -p ru $<
		mv $(@:.c=.h) includes

%lex.ru.c: $*ru.l $*ru.y
		$(FLEX) -i --header-file=includes/$(@F:.c=.h) -o $@ -Pru $<

ru.tab.o: $(VPATH)/ru.tab.c
		$(CC) $(CFLAGS) -c $<

lex.ru.o: $(VPATH)/lex.ru.c
		$(CC) $(CFLAGS) -c $<

rt: $(OBJ)
		$(CC) $(LIBS) -o rt $(OBJ)

clean:
		rm -f *.o */ru.tab.* */lex.ru.* src/ru.output rt
