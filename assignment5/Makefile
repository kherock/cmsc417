CC=gcc
CFLAGS=-Wall -Iheaders -Wextra -std=gnu99 -ggdb
LDLIBS=-lcrypto -lprotobuf-c
VPATH=src:headers

all: chord

chord: chord.c chord.pb-c.o hash.o

%chord.pb-c.h: chord.proto
	protoc-c --c_out=headers $<

chord.pb-c.o: headers/chord.pb-c.h
	$(CC) -c -o $@ $(<:.h=.c)
	rm $(<:.h=.c)

clean:
	rm -rf *~ *.o chord headers/chord.pb-c.*


.PHONY : clean all
