/**
 * Assignment 3 Chat Server
 * @author Kyle Herock
 */

#include "server.h"

struct client_frame *clients = NULL; // Tracks clients that have gotten past the handshake
struct room *rooms = NULL;

error_t server_parser(int key, char *arg, struct argp_state *state) {
	struct server_arguments *args = state->input;
	error_t ret = 0;
	switch (key) {
	case 'p':
		args->port = atoi(arg);
		if (args->port == 0) { // port is invalid
			argp_error(state, "Invalid option for a port, must be a number");
		}
		break;
	default:
		ret = ARGP_ERR_UNKNOWN;
		break;
	}
	return ret;
}

void server_parseopt(struct server_arguments *args, int argc, char *argv[]) {
	memset(args, 0, sizeof(*args));

	struct argp_option options[] = {
		{ "port", 'p', "port", 0, "The port to be used for the server" , 0 },
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
				hasIncomingClient = 0;
			}
			if (ufds[i+1].revents & POLLIN) {
				// printf("Handling incoming message (state %d)\n", clients[i].state);
				handleIncomingMessage(ufds[i+1].fd, &clients[i]);
				if (clients[i].state == CLIENT_CLOSED) { // close the connection immediately
					close(ufds[i+1].fd);
					ufds[i+1].fd = -1;
					ufds[i+1].revents = 0;
					destroyClient(&clients[i]);
				} else if (clients[i].state == CLIENT_CLOSING) {
					ufds[i+1].events &= ~POLLIN;
				}
				if (clients[i].sendQueue[0] != NULL) {
					ufds[i+1].events |= POLLOUT;
				}
			}
			if (ufds[i+1].revents & POLLOUT) {
				if (!clients[i].expected_send) {
					// Safe since having the POLLOUT revent guarentees at least one message in queue
					clients[i].expected_send = 7 + ntohl(*(uint32_t *)&clients[i].sendQueue[0][2]);
				}
				flushOutgoingMessages(ufds[i+1].fd, &clients[i]);
				if (clients[i].sendQueue[0] == NULL) {
					ufds[i+1].events &= ~POLLOUT;
				}
			}
			if (clients[i].state == CLIENT_CLOSING && clients[i].sendQueue[0] == NULL) {
				destroyClient(&clients[i]);
			}
		}
		break;
	}
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
	locals->sendQueue[0] = NULL;
	locals->recv_len = 0;
	locals->recvBuf = malloc(MAX_PAYLOAD_SIZE);
	locals->expected_recv = 0;
	locals->room = NULL;

	// char clientName[INET_ADDRSTRLEN]; // String to contain client address
	// if (inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, clientName, sizeof(clientName)) != NULL) {
	// 	printf("Handling client %s/%d\n", clientName, ntohs(clientAddr.sin_port));
	// } else {
	// 	puts("Unable to get client address");
	// }
	return clientSock;
}

void handleIncomingMessage(int clientSock, struct client_frame *locals) {
	uint8_t *recvBuf = locals->recvBuf;
	ssize_t numBytesRcvd;
	if (!locals->expected_recv) locals->expected_recv = 7; // ID + size + command
	// printf("Attempting to recv %lu bytes\n", locals->expected_recv);
	numBytesRcvd = recv(clientSock, recvBuf + locals->recv_len, locals->expected_recv, 0);
	if (numBytesRcvd < 0) {
		perror("recv() failed");
		locals->state = CLIENT_CLOSED;
		return;
	}
	locals->recv_len += numBytesRcvd;
	locals->expected_recv -= numBytesRcvd;
	// printf("Got %ld bytes, %lu bytes total\n", numBytesRcvd, locals->recv_len);
	if (!numBytesRcvd) {
		locals->state = CLIENT_CLOSED;
	} else if (!locals->expected_recv) {
		size_t payload_len = ntohl(*(uint32_t *)&recvBuf[2]);
		if (locals->recv_len == 7 && payload_len != 0) {
			locals->expected_recv = payload_len;
		} else if (ntohs(*(uint16_t *)recvBuf) != 0x0417) {
			locals->state = CLIENT_CLOSED;
		} else switch (locals->state) {
		case CLIENT_INIT:
			// Stop the client if ID doesn't match, payload len is > 5, or 7th byte isn't 0xff
			// printf("hello (%lu bytes)\n", payload_len);
			if (payload_len > 5 || recvBuf[6] != HELLO) {
				locals->state = CLIENT_CLOSED;
			} else if (!payload_len) {
				locals->state = CLIENT_INVALID; // 0-length Hello packet puts client into invalid state
			} else {
				char *hello = "Hello";
				int valid = 1;
				// Only the bytes the client has sent us need to match
				for (size_t i = 0; valid && i < payload_len; i++) {
					if (recvBuf[7 + i] != hello[i]) valid = 0;
				}
				if (!valid) {
					queueMessage(
						locals->sendQueue,
						createServerResponse("I don't actually have time for this nonsense.", 1)
					);
					locals->state = CLIENT_INVALID;
				} else {
					struct client_frame *existingNickEntry = NULL;
					int incr = 0;
					locals->nick = malloc(256);
					sprintf(locals->nick, "rand");
					do {
						sprintf(locals->nick + 4, "%d", incr++);
						HASH_FIND_STR(clients, locals->nick, existingNickEntry);
					} while (existingNickEntry != NULL);
					printf("connecting client with nick %s\n", locals->nick);
					HASH_ADD_KEYPTR(hh, clients, locals->nick, strlen(locals->nick), locals);
					queueMessage(locals->sendQueue, createServerResponse(locals->nick, 0));
					locals->state = CLIENT_CONNECTED;
				}
			}
			break;
		case CLIENT_CONNECTED:
			switch (recvBuf[6]) {
				char *room, *password;
			case JOIN:
				// puts("join");
				room = (char *)&recvBuf[9];
				size_t roomName_len = recvBuf[8];
				password = (char *)&recvBuf[9 + roomName_len];
				size_t password_len = recvBuf[8 + roomName_len];
				if (roomName_len + password_len > payload_len - 2
						|| memchr(room, '\0', roomName_len) != NULL
						|| memchr(password, '\0', password_len) != NULL) {
					queueMessage(
						locals->sendQueue,
						createServerResponse("The hotel manager looks at you askance. Methinks you speak not good.", 1)
					);
					locals->state = CLIENT_CLOSING;
				} else {
					// TODO: verify password
					locals->room = room;
					queueMessage(locals->sendQueue, createServerResponse("", 0));
				}
				break;
			case LEAVE:
				// puts("leave");
				if (locals->room == NULL) {
					locals->state = CLIENT_CLOSING;
				} else {
					locals->room = NULL;
				}
				queueMessage(locals->sendQueue, createServerResponse("", 0));
				break;
			case LIST_ROOMS:
				break;
			case LIST_USERS:
				break;
			case MSG:
				break;
			case CHAT:
				break;
			}
			break;
		default:;
		}
	}
}

void flushOutgoingMessages(int clientSock, struct client_frame *locals) {
	uint8_t *sendBuf = locals->sendQueue[0];
	ssize_t numBytesSent;
	int i = 0;
	while (locals->sendQueue[i] != NULL) {
		// printf("Sending message %d\n", i);
		// printf("Attempting to send %lu bytes\n", locals->expected_send);
		numBytesSent = send(clientSock, locals->sendQueue[i], locals->expected_send, MSG_NOSIGNAL);
		// printf("Sent %ld bytes\n", numBytesSent);
		if (numBytesSent < 0) break;
		locals->expected_send -= numBytesSent;
		memmove(locals->sendQueue[i], locals->sendQueue[i] + numBytesSent, locals->expected_send);
		if (!locals->expected_send) {
			i++;
			locals->expected_send = 7 + ntohl(*(uint8_t *)&sendBuf[2]);
		}
	}
	if (numBytesSent < 0 && errno != EWOULDBLOCK) {
		perror("send() failed");
		locals->state = CLIENT_CLOSED;
	}
	size_t queue_len = 0;
	while (locals->sendQueue[i + queue_len] != NULL) queue_len++;
	memmove(locals->sendQueue, locals->sendQueue + i, queue_len + 1);
	locals->sendQueue[queue_len] = NULL;
}

uint8_t *createServerResponse(char *message, int code) {
	puts(message);
	size_t message_len = strlen(message);
	uint8_t *response = malloc(8 + message_len);
	*(uint16_t *)response = htons(0x0417);
	*(uint32_t *)&response[2] = htonl(1 + message_len);
	response[6] = 0xfe;
	response[7] = code;
	memcpy(response + 8, message, message_len);
	return response;
}

void queueMessage(uint8_t **sendQueue, uint8_t *message) {
	int i = 0;
	while (sendQueue[i] != NULL) i++;
	if (i == 10) { // Drop old messages if the queue gets too long
		memmove(sendQueue, sendQueue + 1, --i);
	}
	sendQueue[i] = message;
	sendQueue[i+1] = NULL;
}

void destroyClient(struct client_frame *client) {
	HASH_DEL(clients, client);
	for (int i = 0; clients->sendQueue[i] != NULL; i++) free(clients->sendQueue[i]);
	free(client->sendQueue);
	free(client->recvBuf);
}