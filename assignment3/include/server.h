
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
#include <time.h>
#include <unistd.h>

#include "uthash.h"

#define MAX_CLIENTS 20
#define MAX_PAYLOAD_SIZE 256

enum client_state {
	CLIENT_INIT,
	CLIENT_HANDSHAKE,
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
    char nick[256];
	struct room *room;
    uint8_t *sendQueue[11];
    size_t expected_send;
	uint8_t *recvBuf;
	size_t expected_recv;
	size_t recv_len;
	time_t ttl;
	short *pollEvents;
	UT_hash_handle hh;
};

struct room {
	char *name;
    char *password;
	int isEmpty;
	UT_hash_handle hh;
};

struct server_arguments {
	int port;
};

int room_sort(struct room *a, struct room *b) { return strcmp(a->name, b->name); }

int handleIncomingClient(int servSock, struct client_frame *locals);

void handleIncomingMessage(int clientSock, struct client_frame *locals);

void handleCommand(struct client_frame *locals, int command, uint8_t *payload, size_t payload_len);

void flushOutgoingMessages(int clientSock, struct client_frame *locals);

uint8_t *createServerResponse_buf(int code, uint8_t *message, size_t message_len);

uint8_t *createServerResponse(int code, char *message);

void queueMessage(struct client_frame *client, uint8_t *message);

void destroyClient(struct client_frame *client);

void handleLeave(struct room *room);
