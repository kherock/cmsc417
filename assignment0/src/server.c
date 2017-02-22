/**
 * Assignment 0 TCP Server Implementation
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
#include <unistd.h>

#include "hash.h"

#define MAX_CLIENTS 15

enum client_state { CLIENT_INIT, CLIENT_PRE_HASH, CLIENT_HASH, CLIENT_CLOSED };
// a structure to essentially preserve a client's stack frame across polls
struct client_frame {
	enum client_state state;
	struct checksum_ctx *ctx;
	size_t hash_len;
	uint8_t *sendBuf;
	size_t send_len;
	uint8_t *recvBuf;
	size_t recv_len;
	unsigned int hashnum;
	unsigned int hash_i;
};

struct server_arguments {
	int port;
	uint8_t *salt;
	size_t salt_len;
};

error_t server_parser(int key, char *arg, struct argp_state *state) {
	struct server_arguments *args = state->input;
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
	case 's':
		args->salt_len = strlen(arg);
		args->salt = malloc(args->salt_len);
		memcpy(args->salt, arg, args->salt_len);
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
		{ "salt", 's', "salt", 0, "The salt to be used for the server. Zero by default", 0 },
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

	printf("Got port %d and salt %s with length %ld\n", args->port, args->salt, args->salt_len);

	return args;
}

int handleIncomingClient(int servSock, struct client_frame *locals) {
	struct sockaddr_in clientAddr; // Client address
	// Set length of client address structure (in-out parameter)
	socklen_t clientAddrLen = sizeof(clientAddr);

	// Wait for a client to connect
	int clientSock = accept(servSock, (struct sockaddr *)&clientAddr, &clientAddrLen);
	if (clientSock < 0) {
		perror("accept() failed");
		exit(1);
	}
	fcntl(clientSock, F_SETFL, O_NONBLOCK);

	memset(locals, 0, sizeof(*locals));
	locals->state = CLIENT_INIT;
	locals->send_len = 0;
	locals->sendBuf = malloc(36);
	locals->recv_len = 0;
	locals->recvBuf = malloc(UPDATE_PAYLOAD_SIZE);

	// char clientName[INET_ADDRSTRLEN]; // String to contain client address
	// if (inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, clientName, sizeof(clientName)) != NULL) {
	// 	printf("Handling client %s/%d\n", clientName, ntohs(clientAddr.sin_port));
	// } else {
	// 	puts("Unable to get client address");
	// }
	return clientSock;
}

void handleIncomingMessage(int clientSock, struct client_frame *locals) {
	uint8_t *sendBuf = locals->sendBuf;
	uint8_t *recvBuf = locals->recvBuf;
	ssize_t expectedBytesLeft, numBytesRcvd;
	switch (locals->state) {
	case CLIENT_INIT:
		expectedBytesLeft =  4 - locals->recv_len;
		break;
	case CLIENT_PRE_HASH:
		expectedBytesLeft =  6 - locals->recv_len;
		break;
	case CLIENT_HASH:
		expectedBytesLeft = locals->hash_len - locals->recv_len;
		if (expectedBytesLeft > UPDATE_PAYLOAD_SIZE) {
			expectedBytesLeft = UPDATE_PAYLOAD_SIZE - locals->recv_len % UPDATE_PAYLOAD_SIZE;
		} else if (locals->send_len) {
			printf("%lu outgoing bytes still need to send\n", locals->send_len);
			return; // Not ready to process if bytes still need to be sent
		}
		break;
	default:;
	}
	size_t offset = locals->recv_len % UPDATE_PAYLOAD_SIZE;
	numBytesRcvd = recv(clientSock, recvBuf + offset, expectedBytesLeft, 0);
	if (numBytesRcvd < 0) {
		perror("recv() failed");
		exit(1);
	}
	locals->recv_len += numBytesRcvd;
	if (!numBytesRcvd) {
		locals->state = CLIENT_CLOSED;
	} else if (numBytesRcvd == expectedBytesLeft) switch (locals->state) {
	case CLIENT_INIT:
		locals->hashnum = ntohl(*(uint32_t *)recvBuf);
		// printf(" - requesting %d hashes\n", locals->hashnum);
		locals->recv_len = 0;
		*(uint32_t *)sendBuf = htonl(36 * locals->hashnum);
		locals->send_len = 4;
		locals->state = CLIENT_PRE_HASH;
		break;
	case CLIENT_PRE_HASH:
		if (ntohs(*(uint16_t *)recvBuf) != 0x0417) {
			// printf(" - client sent HashRequest with bad ID (0x%04x)\n", ntohs(*(uint16_t *)recvBuf));
			locals->state = CLIENT_CLOSED;
		} else {
			// printf(" - client sent HashRequest with ID 0x%04x\n", ntohs(*(uint16_t *)recvBuf));
			locals->hash_len = ntohl(*(uint32_t *)&recvBuf[2]);
			locals->recv_len = 0;
			locals->state = CLIENT_HASH;
			// printf(" - hashing a %u byte payload\n", (uint32_t)locals->hash_len);
		}
		break;
	case CLIENT_HASH:
		if (locals->recv_len == locals->hash_len) {
			size_t buf_len = locals->recv_len % UPDATE_PAYLOAD_SIZE;
			// printf(" - hashing %lu bytes\n", buf_len);
			*(uint32_t *)sendBuf = htonl(locals->hash_i++);
			checksum_finish(locals->ctx, recvBuf, buf_len, sendBuf + 4);
			// printf(" - sending out hash %u\n", ntohl(*(uint32_t *)sendBuf));
			locals->send_len = 36;
			if (locals->hash_i < locals->hashnum) {
				locals->state = CLIENT_PRE_HASH;
				locals->recv_len = 0;
				checksum_reset(locals->ctx);
			}
		} else {
			// printf(" - hashing 4096 bytes\n");
			checksum_update(locals->ctx, recvBuf);
		}
		break;
	default:;
	}
}

void flushOutgoingStream(int clientSock, struct client_frame *locals) {
	uint8_t *sendBuf = locals->sendBuf;
	ssize_t numBytesSent = send(clientSock, sendBuf, locals->send_len, MSG_NOSIGNAL);
	if (numBytesSent < 0) {
		perror("send() failed");
		exit(1);
	}
	locals->send_len -= numBytesSent;
	memmove(sendBuf, sendBuf + numBytesSent, locals->send_len);
}

int main(int argc, char *argv[]) {
	struct pollfd ufds[1 + MAX_CLIENTS]; // Server + 15 concurrent clients
	struct client_frame clients[MAX_CLIENTS];
    struct server_arguments args;

	server_parseopt(&args, argc, argv);


 	// Create socket for incoming connections
	int servSock; // Socket descriptor for server
	if ((servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("socket() failed");
		exit(1);
	}
	fcntl(servSock, F_SETFL, O_NONBLOCK);

	ufds[0].fd = servSock;
	ufds[0].events = POLLIN | POLLPRI;

	// Initialize the rest of the ufds array
	for (int i = 0; i < MAX_CLIENTS; i++) {
		ufds[i+1].fd = -1;
		ufds[i+1].events = POLLIN;
	}

	// Construct local address structure
	struct sockaddr_in servAddr; // Local address
	memset(&servAddr, 0, sizeof(servAddr)); // Zero out structure
	servAddr.sin_family = AF_INET; // IPv4 address family
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY); // Any incoming interface
	servAddr.sin_port = htons(args.port); // Local port
	
	// Bind to the local address
	if (bind(servSock, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
		perror("bind() failed");
		exit(1);
	}
	 
	// Mark the socket so it will listen for incoming connections
	if (listen(servSock, SOMAXCONN) < 0) {
		perror("listen() failed");
		exit(1);
	}
	
	int hasIncomingClient;
	for (;;) switch (poll(ufds, 1 + MAX_CLIENTS, -1)) { // Run forever
	case -1:
		perror("poll() failed");
		exit(1);
	case 0:
		puts("Waiting for connections");
		break;
	default:
		hasIncomingClient = ufds[0].revents & POLLIN;
		for (int i = 0; i < MAX_CLIENTS; i++) {
			if (ufds[i+1].fd < 0 && hasIncomingClient) { // Server can handle incoming connection
				ufds[i+1].fd = handleIncomingClient(servSock, &clients[i]);
				clients[i].ctx = checksum_create(args.salt, args.salt_len);
				hasIncomingClient = 0;
			}
			if (ufds[i+1].revents & POLLIN) {
				// printf("Handling incoming message (state %d)\n", clients[i].state);
				handleIncomingMessage(ufds[i+1].fd, &clients[i]);
				if (clients[i].state == CLIENT_CLOSED) { // close the connection immediately
					close(ufds[i+1].fd);
					ufds[i+1].fd = -1;
					ufds[i+1].revents = 0;
					checksum_destroy(clients[i].ctx);
					free(clients[i].sendBuf);
					free(clients[i].recvBuf);
				}
				if (clients[i].send_len) {
					ufds[i+1].events |= POLLOUT;
				}
			}
			if (ufds[i+1].revents & POLLOUT) {
				// printf("Attempting to flush %lu outgoing bytes\n", clients[i].send_len);
				flushOutgoingStream(ufds[i+1].fd, &clients[i]);
				if (!clients[i].send_len) {
					ufds[i+1].events &= ~POLLOUT;
				}
			}
		}
		break;
	}
}
