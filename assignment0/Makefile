CC=gcc
CFLAGS=-Wall -Iincludes -Wextra -std=c99 -ggdb
LDLIBS=
VPATH=src

all: client server

client: client.c

server: server.c

clean:
	rm -rf client server


.PHONY : clean all
