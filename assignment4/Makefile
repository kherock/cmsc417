CC=gcc
CFLAGS=-Wall -Iheaders -Wextra -std=gnu99 -ggdb
LDLIBS=-lprotobuf-c
VPATH=src:headers

all: client server

client: client.c rpc.pb-c.o

server: server.c rpc.pb-c.o

%rpc.pb-c.h: rpc.proto
	protoc-c --c_out=headers $<

rpc.pb-c.o: headers/rpc.pb-c.h
	$(CC) -c -o $@ $(<:.h=.c)
	rm $(<:.h=.c)

clean:
	rm -rf *~ *.o client server headers/rpc.pb-c.*


.PHONY : clean all
