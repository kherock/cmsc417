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
	struct client_frame *clientsArr[MAX_CLIENTS];
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
	for (int i = 0; i < MAX_CLIENTS; i++) ufds[i+1].fd = -1;

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
	int timeout = -1;
	for (;;) switch (poll(ufds, 1 + MAX_CLIENTS, timeout)) {
	case -1:
		perror("poll() failed");
		exit(1);
	case 0:
	default:
		timeout = -1;
		hasIncomingClient = ufds[0].revents & POLLIN;
		for (int i = 0; i < MAX_CLIENTS; i++) {
			if (ufds[i+1].fd < 0) {
				if (hasIncomingClient) { // Server can handle incoming connection
					clientsArr[i] = malloc(sizeof(struct client_frame));
					ufds[i+1].fd = handleIncomingClient(servSock, clientsArr[i]);
					ufds[i+1].events = POLLIN;
					clientsArr[i]->pollEvents = &ufds[i+1].events;
					hasIncomingClient = 0;
				} else continue;
			}
			if (ufds[i+1].revents & POLLIN) {
				handleIncomingMessage(ufds[i+1].fd, clientsArr[i]);
				if (clientsArr[i]->recv_len < 7 && !clientsArr[i]->ttl) {
					
				}
			}
			if (ufds[i+1].revents & POLLOUT) {
				flushOutgoingMessages(ufds[i+1].fd, clientsArr[i]);
			}
			if (clientsArr[i]->ttl) {
				if (clientsArr[i]->ttl <= time(NULL)) {
					clientsArr[i]->state = CLIENT_CLOSED;
				} else if (timeout < 0 || timeout < 1000 * (clientsArr[i]->ttl - time(NULL))) {
					timeout = 1000 * (clientsArr[i]->ttl - time(NULL));
				}
			}
			if (clientsArr[i]->state == CLIENT_CLOSED) {
				close(ufds[i+1].fd);
				ufds[i+1].fd = -1;
				destroyClient(clientsArr[i]);
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
	locals->recvBuf = malloc(7 + MAX_PAYLOAD_SIZE);
	locals->ttl = time(NULL) + 30;

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
	if (!locals->expected_recv) {
		locals->expected_recv = 7; // ID + size + command
	}
	if (locals->state == CLIENT_INIT) {
		locals->state = CLIENT_HANDSHAKE;
		locals->ttl += 30;
	}
	numBytesRcvd = recv(clientSock, recvBuf + locals->recv_len, locals->expected_recv, 0);
	if (numBytesRcvd < 0) {
		perror("recv() failed");
		locals->state = CLIENT_CLOSED;
		return;
	}
	locals->recv_len += numBytesRcvd;
	locals->expected_recv -= numBytesRcvd;
	if (!numBytesRcvd) {
		locals->state = CLIENT_CLOSED;
	} else if (!locals->expected_recv) { // We've received at least the entire header
		uint8_t *payload = recvBuf + 7;
		size_t payload_len = ntohl(*(uint32_t *)&recvBuf[2]);

		if (payload_len > MAX_PAYLOAD_SIZE) {
			queueMessage(locals, createServerResponse(1, "Length limit exceeded."));
			locals->state = CLIENT_CLOSING;
		} else if (locals->recv_len == 7 && payload_len != 0) {
			locals->expected_recv = payload_len;
		} else if (ntohs(*(uint16_t *)recvBuf) != 0x0417) {
			locals->state = CLIENT_CLOSED;
		} else {
			switch (locals->state) {
			case CLIENT_HANDSHAKE:
				// Stop the client if ID doesn't match, payload len is > 5, or 7th byte isn't 0xff
				// printf("hello (%lu bytes)\n", payload_len);
				if (payload_len > 5 || recvBuf[6] != HELLO) {
					locals->state = CLIENT_CLOSED;
				} else if (!payload_len) {
					locals->state = CLIENT_INVALID; // 0-length Hello packet puts client into invalid state
				} else {
					locals->ttl = 0;
					char *hello = "Hello";
					if (strncmp(hello, (char *)payload, payload_len) != 0) {
						queueMessage(locals, createServerResponse(1, "I don't actually have time for this nonsense."));
						locals->state = CLIENT_CLOSING;
					} else {						
						struct client_frame *existingNickEntry;
						int incr = 0;
						sprintf(locals->nick, "rand");
						do {
							sprintf(locals->nick + 4, "%d", incr++);
							HASH_FIND_STR(clients, locals->nick, existingNickEntry);
						} while (existingNickEntry != NULL);
						HASH_ADD_STR(clients, nick, locals);
						queueMessage(locals, createServerResponse(0, locals->nick));
						locals->state = CLIENT_CONNECTED;
					}
				}
				break;
			case CLIENT_CONNECTED:
				handleCommand(locals, recvBuf[6], payload, payload_len);
				break;
			default:;
			}
			locals->recv_len = 0;
		}
	}
	if (locals->state == CLIENT_CLOSING) {
		// Closing, stop accepting messages
		*locals->pollEvents &= ~POLLIN;
	} else if (locals->state == CLIENT_INVALID) {
		*locals->pollEvents = 0;
	}
}

void handleCommand(struct client_frame *locals, int command, uint8_t *payload, size_t payload_len) {
	uint8_t *response = NULL;
	int i;
	char *roomName, *password, *message;
	struct client_frame *c;
	struct room *r;
	size_t roomName_len, password_len, nick_len, message_len;
	switch (command) {
	case JOIN:
		// Buffer holds two strings prefixed with their lengths
		roomName = (char *)&payload[1];
		roomName_len = payload[0];
		password = (char *)&payload[1 + roomName_len + 1];
		password_len = payload[1 + roomName_len];
		// Room and password cannot contain the null character
		if (roomName_len + password_len > payload_len - 2
				|| memchr(roomName, '\0', roomName_len) != NULL
				|| memchr(password, '\0', password_len) != NULL) {
			response = createServerResponse(1, "The hotel manager looks at you askance. Methinks you speak not good.");
			locals->state = CLIENT_CLOSING;
		} else {
			HASH_FIND(hh, rooms, roomName, roomName_len, r);
			if (!r) { // Initialize a new room
				r = malloc(sizeof(struct room));
				r->name = malloc(roomName_len + 1);
				memcpy(r->name, roomName, roomName_len);
				r->name[roomName_len] = '\0';
				r->password = malloc(password_len + 1);
				memcpy(r->password, password, password_len);
				r->password[password_len] = '\0';
				HASH_ADD_KEYPTR(hh, rooms, r->name, roomName_len, r);
				HASH_SORT(rooms, room_sort);
			} else {
				if (strncmp(password, r->password, password_len) != 0) {
					response = createServerResponse(1, "Invalid password. You shall not pass.");
					break;
				}
			}
			locals->room = r;
		}
		break;
	case LEAVE:
		if (!locals->room) { // Leaving while not in the room disconnects
			locals->state = CLIENT_CLOSING;
		} else {
			r = locals->room;
			locals->room = NULL;
			handleLeave(r);
		}
		break;
	case LIST_ROOMS:
		message_len = 0;
		for (r = rooms; r != NULL; r = r->hh.next) {
			message_len += 1 + strlen(r->name);
		}
		message = malloc(message_len + 1);
		message[message_len] = '\0';
		i = 0;
		for (r = rooms; r != NULL; r = r->hh.next) {
			roomName_len = strlen(r->name);
			message[i] = roomName_len;
			memcpy(message + i + 1, r->name, roomName_len);
			i += 1 + roomName_len;
		}
		response = createServerResponse_buf(0, (uint8_t *)message, message_len);
		free(message);
		break;
	case LIST_USERS:
		message_len = 0;
		// Print users in the reversed order they have connected
		for (struct client_frame *curr = clients; curr != NULL; curr = curr->hh.next) {
			if (!locals->room || curr->room == locals->room) {
				message_len += 1 + strlen(curr->nick);
				c = curr;
			}
		}
		message = malloc(message_len);
		i = 0;
		for (; c != NULL; c = c->hh.prev) {
			if (!locals->room || c->room == locals->room) {
				nick_len = strlen(c->nick);
				message[i] = nick_len;
				memcpy(message + i + 1, c->nick, nick_len);
				i += 1 + nick_len;
			}
		}
		response = createServerResponse_buf(0, (uint8_t *)message, message_len);
		free(message);
		break;
	case NICK:
		nick_len = payload[0];
		HASH_DEL(clients, locals);
		memcpy(locals->nick, payload + 1, nick_len);
		locals->nick[nick_len] = '\0';
		HASH_ADD_STR(clients, nick, locals);
		break;
	case MSG:
		roomName = (char *)&payload[1];
		roomName_len = payload[0];
		message = (char *)&payload[1 + roomName_len + 2];
		message_len = ntohs(*(uint16_t *)&payload[1 + roomName_len]);
		if (roomName_len + message_len > payload_len - 2
				|| memchr(roomName, '\0', roomName_len)
				|| memchr(message, '\0', message_len)) {
			// Immediately close the connection for bad message formats
			locals->state = CLIENT_CLOSED;
		} else {
			nick_len = strlen(locals->nick);
			size_t send_len = 1 + nick_len + 2 + message_len;
			uint8_t *messageBuf = malloc(7 + send_len);
			*(uint16_t *)messageBuf = htons(0x0417);
			*(uint32_t *)&messageBuf[2] = htonl(send_len);
			messageBuf[6] = MSG;
			messageBuf[7] = nick_len;
			memcpy(messageBuf + 7 + 1, locals->nick, nick_len);
			*(uint16_t *)&messageBuf[7 + 1 + nick_len] = htons(message_len);
			memcpy(messageBuf + 7 + 1 + nick_len + 2, message, message_len);
			HASH_FIND(hh, clients, roomName, roomName_len, c);
			if (c) {
				queueMessage(c, messageBuf);
			} else {
				response = createServerResponse(1, "Nick not present");
			}
		}
		break;
	case CHAT:
		roomName = (char *)&payload[1];
		roomName_len = payload[0];
		message = (char *)&payload[1 + roomName_len + 2];
		message_len = ntohs(*(uint16_t *)&payload[1 + roomName_len]);
		if (roomName_len + message_len > payload_len - 2
				|| memchr(roomName, '\0', roomName_len)
				|| memchr(message, '\0', message_len)) {
			// Immediately close the connection for bad message formats
			locals->state = CLIENT_CLOSED;
		} else if (!locals->room) {
			response = createServerResponse(1, "You shout into the void and hear nothing.");
		} else if (strncmp(locals->room->name, roomName, roomName_len) != 0) {
			response = createServerResponse(1, "You can't astral project");
		} else {
			nick_len = strlen(locals->nick);
			size_t send_len = 1 + roomName_len + 1 + nick_len + 2 + message_len;
			uint8_t *messageBuf = malloc(7 + send_len);
			*(uint16_t *)messageBuf = htons(0x0417);
			*(uint32_t *)&messageBuf[2] = htonl(send_len);
			messageBuf[6] = CHAT;
			messageBuf[7] = roomName_len;
			memcpy(messageBuf + 7 + 1, roomName, roomName_len);
			messageBuf[8 + roomName_len] = nick_len;
			memcpy(messageBuf + 7 + 1 + roomName_len + 1, locals->nick, nick_len);
			*(uint16_t *)&messageBuf[7 + 1 + roomName_len + 1 + nick_len] = htons(message_len);
			memcpy(messageBuf + 7 + send_len - message_len, message, message_len);
			for (struct client_frame *c = clients; c != NULL; c = c->hh.next) {
				if (locals->room == c->room && c != locals) {
					queueMessage(c, memcpy(malloc(7 + send_len), messageBuf, 7 + send_len));
				}
			}
			free(messageBuf);
		}
		break;
	default:
		if (command < 0x17) { // Request hangs with commands below 0x17
			locals->state = CLIENT_INVALID;
		} else {
			response = createServerResponse(1, "What is this strange command you speak of? Out devil!");
			locals->state = CLIENT_CLOSING;
		}
		break;
	}
	if (!response) response = createServerResponse(0, "");
	queueMessage(locals, response);	
}

void flushOutgoingMessages(int clientSock, struct client_frame *locals) {
	ssize_t numBytesSent;
	int i = 0;
	while (locals->sendQueue[i] != NULL) {
		if (!locals->expected_send) { // Read the stored payload size if starting a new send
			locals->expected_send = 7 + ntohl(*(uint32_t *)&locals->sendQueue[i][2]);
		}
		numBytesSent = send(clientSock, locals->sendQueue[i], locals->expected_send, MSG_NOSIGNAL);
		if (numBytesSent <= 0) break;
		locals->expected_send -= numBytesSent;
		memmove(locals->sendQueue[i], locals->sendQueue[i] + numBytesSent, locals->expected_send);
		if (!locals->expected_send) {
			free(locals->sendQueue[i]);
			i++;
		}
	}
	if (numBytesSent == 0) { // Connection has closed, so the send queue is dropped
		while (locals->sendQueue[i] != NULL) free(locals->sendQueue[i++]);
		locals->sendQueue[0] = NULL;
	} else if (numBytesSent < 0 && errno != EWOULDBLOCK) {
		perror("send() failed");
		locals->state = CLIENT_CLOSED;
	} else {
		size_t queue_len = 0;
		while (locals->sendQueue[i + queue_len] != NULL) queue_len++;
		memmove(locals->sendQueue, locals->sendQueue + i, sizeof(void *) * (queue_len + 1));
	}
	if (locals->sendQueue[0] == NULL) {
		*locals->pollEvents &= ~POLLOUT;
		if (locals->state == CLIENT_CLOSING) locals->state = CLIENT_CLOSED;
	}
}

uint8_t *createServerResponse(int code, char *message) {
	return createServerResponse_buf(code, (uint8_t *)message, strlen(message));
}

uint8_t *createServerResponse_buf(int code, uint8_t *message, size_t message_len) {
	uint8_t *response = malloc(7 + 1 + message_len);
	*(uint16_t *)response = htons(0x0417);
	*(uint32_t *)&response[2] = htonl(1 + message_len);
	response[6] = 0xfe;
	response[7] = code;
	memcpy(response + 8, message, message_len);
	return response;
}

void queueMessage(struct client_frame *client, uint8_t *message) {
	int i = 0;
	while (client->sendQueue[i] != NULL) i++;
	if (i == 10) { // Drop old messages if the queue gets too long
		memmove(client->sendQueue, client->sendQueue + 1, --i);
	}
	client->sendQueue[i] = message;
	client->sendQueue[i+1] = NULL;
	*client->pollEvents |= POLLOUT;
}

void destroyClient(struct client_frame *client) {
	if (clients) HASH_DEL(clients, client);
	handleLeave(client->room);
	for (int i = 0; client->sendQueue[i] != NULL; i++) free(client->sendQueue[i]);
	free(client->recvBuf);
	free(client);
}

void handleLeave(struct room *room) {
	int isRoomEmpty = 1;
	if (room) {
		for (struct client_frame *c = clients; c != NULL; c = c->hh.next) {
			isRoomEmpty &= c->room != room;
		}
		if (isRoomEmpty) {
			HASH_DEL(rooms, room);
			free(room);
		}
	}
}