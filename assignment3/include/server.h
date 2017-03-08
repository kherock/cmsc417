
#include <argp.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sysexits.h>
#include <unistd.h>

#include "uthash.h"

#define MAX_CLIENTS 15
#define MAX_PAYLOAD_SIZE 65536

enum client_state {
	CLIENT_INIT,
	CLIENT_CONNECTED,
    CLIENT_CLOSING, // Flush outgoing messages before close
	CLIENT_CLOSED,
	CLIENT_INVALID
};
enum command {
	HELLO=0xff,
	JOIN=0x17,
	LEAVE,
	LIST_ROOMS,
	LIST_USERS,
	NICK,
	MSG,
	CHAT
};

struct client_frame {
	enum client_state state;
    char *nick;
	char *room;
    uint8_t *sendQueue[11];
    size_t expected_send;
	uint8_t *recvBuf;
	size_t expected_recv;
	size_t recv_len;
	UT_hash_handle hh;
};

struct room {
	char *name;
    char *password;
	struct client_frame **clients;
	UT_hash_handle hh;
};

struct server_arguments {
	int port;
};

int handleIncomingClient(int servSock, struct client_frame *locals);

void handleIncomingMessage(int clientSock, struct client_frame *locals);

void flushOutgoingMessages(int clientSock, struct client_frame *locals);

uint8_t *createServerResponse(char *message, int code) ;

void queueMessage(uint8_t **sendQueue, uint8_t *message);

void destroyClient(struct client_frame *client);