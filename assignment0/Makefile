CC=gcc
CFLAGS=-Wall -Iincludes -Wextra -std=gnu99
LDLIBS=-lcrypto
VPATH=src

all: client server

client: client.c

server: server.c hash.o

hash.o: hash.c

clean:
	rm -rf client server *.o


.PHONY : clean all
