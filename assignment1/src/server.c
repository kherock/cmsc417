/**
 * Assignment 1 NTP Server Implementation
 * @author Kyle Herock
 */

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
#include "htonll.h"

#define TRQST_LEN 22
#define TRESP_LEN 38
#define TTL0 1000 

static uint8_t TRQST_BUF[TRQST_LEN];

enum client_state { CLIENT_INIT, CLIENT_PRE_HASH, CLIENT_HASH, CLIENT_CLOSED };
// a structure to essentially preserve a client's stack frame across polls
struct client_frame {
	char *address; // The key for uthash
	struct sockaddr_in sockAddr;
	enum client_state state;
	int maxSeq;
	uint8_t *buf;
	size_t buf_len;
	time_t ttl;
	UT_hash_handle hh;
};

struct server_arguments {
	int port;
	double drop_chance;
};

error_t server_parser(int key, char *arg, struct argp_state *state) {
	struct server_arguments *args = state->input;
	int num;
	error_t ret = 0;
	switch (key) {
	case 'p':
		args->port = atoi(arg);
		if (args->port == 0) { // port is invalid
			argp_error(state, "Invalid option for a port, must be a number");
		} else if (args->port <= 1024) {
			argp_error(state, "Port must be greater than 1024");
		}
		break;
	case 'd':
		num = atoi(arg); // The 0 case cannot be easily detected, so invalid input will just use 0
		if (num < 0 || num > 100) {
			argp_error(state, "Percentage must be between 0 and 100");
		}
		args->drop_chance = (double)num / 100.0;
		break;
	default:
		ret = ARGP_ERR_UNKNOWN;
		break;
	}
	return ret;
}

void *server_parseopt(struct server_arguments *args, int argc, char *argv[]) {
	memset(args, 0, sizeof(*args));

	struct argp_option options[] = {
		{ "port", 'p', "port", 0, "The port to be used for the server" , 0 },
		{ "drop", 'd', "drop", 0, "The percent chance a given packet will be dropped. Zero by default", 0 },
		{0}
	};
	struct argp argp_settings = { options, server_parser, 0, 0, 0, 0, 0 };
	if (argp_parse(&argp_settings, argc, argv, 0, NULL, args) != 0) {
		fputs("Got an error condition when parsing\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->port) {
		fputs("A port number must be specified\n", stderr);
		exit(EX_USAGE);
	}

	return args;
}

void handleIncomingClient(struct sockaddr_in *remaddr, struct client_frame *locals) {
	int len = INET_ADDRSTRLEN + sizeof(ntohs(remaddr->sin_port)) + 2;
	memset(locals, 0, sizeof(*locals));
	memcpy(&locals->sockAddr, remaddr, sizeof(locals->sockAddr));
	locals->address = malloc(len); // String to contain client address
	inet_ntop(AF_INET, &locals->sockAddr.sin_addr.s_addr, locals->address, sizeof(INET_ADDRSTRLEN));
	sprintf(locals->address + INET_ADDRSTRLEN, ":%d", ntohs(remaddr->sin_port));
	printf("%s\n", locals->address);
	locals->state = CLIENT_INIT;
	locals->buf = malloc(TRQST_LEN + 16);
}

int handleIncomingMessage(int sock, struct client_frame *clients) {
	struct client_frame *locals;
	struct sockaddr_in remaddr;
	socklen_t addrlen = sizeof(remaddr);
	struct timespec timeSpec1;
	clock_gettime(CLOCK_REALTIME, &timeSpec1);
	if (recvfrom(sock, TRQST_BUF, TRQST_LEN, 0, (struct sockaddr *)&remaddr, &addrlen) < 0) {
		perror("recvfrom() failed");
		exit(1);
	}
	char clientName[INET_ADDRSTRLEN]; // String to contain client address
	inet_ntop(AF_INET, &remaddr.sin_addr.s_addr, clientName, sizeof(clientName));
	HASH_FIND_STR(clients, clientName, locals);
	if (locals == NULL) {
		locals = malloc(sizeof(struct client_frame));
		handleIncomingClient(&remaddr, locals);
		HASH_ADD_STR(clients, address, locals);
		puts("Incoming client");
	}

	struct sockaddr_in *sockAddr = &locals->sockAddr;
	uint8_t *buf = locals->buf;

	if (ntohs(*(uint16_t *)TRQST_BUF) != 0x0417) {
		printf("Client sent HashRequest with bad ID (0x%04x)\n", ntohs(*(uint16_t *)TRQST_BUF));
		return 0;
	} else {
		memcpy(locals->buf, TRQST_BUF, TRQST_LEN);
		int seq = ntohl(*(uint32_t *)&locals->buf[2]);
		if (locals->maxSeq < seq) {
			printf("%s:%d %d %d\n", clientName, ntohs(sockAddr->sin_port), locals->maxSeq, seq);
			locals->maxSeq = seq;
			locals->ttl = TTL0;
		}
		*(uint64_t *)&buf[22] = htonll((uint64_t)timeSpec1.tv_sec);
		*(uint64_t *)&buf[30] = htonll((uint64_t)timeSpec1.tv_nsec);
		return 1;
	}
}

void flushOutgoingBuffers(int sock, struct client_frame *clients) {
	struct client_frame *locals, *tmp;
	HASH_ITER(hh, clients, locals, tmp) {
		puts("oi");
		struct sockaddr_in *sockAddr = &locals->sockAddr;
		uint8_t *buf = locals->buf;
		if (sendto(sock, buf, TRESP_LEN, 0, (struct sockaddr *)sockAddr, sizeof(*sockAddr)) < 0) {
			perror("send() failed");
			exit(1);
		}
	}
}

int main(int argc, char *argv[]) {
	struct client_frame *clients = NULL;
    struct server_arguments args;
	server_parseopt(&args, argc, argv);
	srand(time(NULL));

 	// Create socket for incoming connections
	struct pollfd sock;
	sock.fd = socket(AF_INET, SOCK_DGRAM, 0); // Socket descriptor for server
	if (sock.fd < 0) {
		perror("socket() failed");
		exit(1);
	}
	sock.events = POLLIN | POLLPRI;
	fcntl(sock.fd, F_SETFL, O_NONBLOCK);

	// Construct local address structure
	struct sockaddr_in servAddr; // Local address
	memset(&servAddr, 0, sizeof(servAddr)); // Zero out structure
	servAddr.sin_family = AF_INET; // IPv4 address family
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY); // Any incoming interface
	servAddr.sin_port = htons(args.port); // Local port
	
	// Bind to the local address
	if (bind(sock.fd, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
		perror("bind() failed");
		exit(1);
	}
	 
	time_t min_ttl = -1;
	for (;;) switch (poll(&sock, 1, min_ttl)) { // Run forever
	case -1:
		perror("poll() failed");
		exit(1);
	case 0:
		puts("Waiting for activity");
		break;
	default:
		if (sock.revents & POLLIN) {
			printf("Drop chance: %f\n", args.drop_chance);
			if (rand() >= args.drop_chance * ((double)RAND_MAX + 1.0)) {
				puts("accepting packet");
				if (handleIncomingMessage(sock.fd, clients)) sock.events |= POLLOUT;
			} else {
				puts("dropping packet");
				recvfrom(sock.fd, TRQST_BUF, TRQST_LEN, 0, NULL, 0); // drop the packet
			}
		}
		if (sock.revents & POLLOUT) {
			flushOutgoingBuffers(sock.fd, clients);
			sock.events &= ~POLLOUT;
		}
		break;
	}
}
