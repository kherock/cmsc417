/**
 * Assignment 4 RPC Server Implementation
 * @author Kyle Herock
 */

#include <argp.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <unistd.h>

#include "rpc.pb-c.h"

#define MAX_CLIENTS 20
#define MAX_PAYLOAD_SIZE 20

struct server_arguments {
	int port;
};

struct client_frame {
  bool closed;
  uint8_t *buf;
  size_t buf_len;
  uint32_t totalSum;
};

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

void *server_parseopt(struct server_arguments *args, int argc, char *argv[]) {
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

	return args;
}

uint32_t add(uint32_t a, uint32_t b) {
  return a + b;
}

int packAddReturnValue(uint8_t **valueSerial, size_t *valueSerial_len, AddReturnValue *value) {
  int error = 0;

  *valueSerial_len = add_return_value__get_packed_size(value);
  *valueSerial = (uint8_t *)malloc(*valueSerial_len);
  if (*valueSerial == NULL) {
    return 1;
  }
  
  add_return_value__pack(value, *valueSerial);

  return error;
}

int addWrapper(uint8_t **valueSerial, size_t *valueSerial_len, const uint8_t *argsSerial, size_t argsSerial_len, uint32_t *addTotal) {
  int error = 0;

  // Deserialize/Unpack the arguments
  AddArguments *args = add_arguments__unpack(NULL, argsSerial_len, argsSerial);
  if (args == NULL) {
    return 1;
  }

  // Call the underlying function
  uint32_t sum = add(args->a, args->b);
	*addTotal += sum;

  // Serialize/Pack the return value
  AddReturnValue value = ADD_RETURN_VALUE__INIT;
  value.sum = sum;

	error = packAddReturnValue(valueSerial, valueSerial_len, &value);

  // Cleanup
  add_arguments__free_unpacked(args, NULL);

	return error;
}

int getAddTotalWrapper(uint8_t **valueSerial, size_t *valueSerial_len, uint32_t *addTotal) {
  // Serialize/Pack the return value
  AddReturnValue value = ADD_RETURN_VALUE__INIT;
  value.sum = *addTotal;

	return packAddReturnValue(valueSerial, valueSerial_len, &value);
}

int handleCall(uint8_t **retSerial, const uint8_t *callSerial, struct client_frame *locals) {
  int error = 0;

  // Deserializing/Unpacking the call
	size_t callSerial_len = ntohl(*(uint32_t *)callSerial);
  Call *call = call__unpack(NULL, callSerial_len, callSerial + 4);
  if (call == NULL) {
    return 1;
  }

  // Calling the appropriate wrapper function based on the `name' field
  uint8_t *valueSerial;
  size_t valueSerial_len;
  bool success;

  if (strcmp(call->name, "add") == 0) {
    success = !addWrapper(&valueSerial, &valueSerial_len, call->args.data, call->args.len, &locals->totalSum);
  } else if (strcmp(call->name, "getAddTotal") == 0) {
		success = !getAddTotalWrapper(&valueSerial, &valueSerial_len, &locals->totalSum);
	} else {
    error = 1;
    goto errInvalidName;
  }

  // Serializing/Packing the return, using the return value from the invoked function
  Return ret = RETURN__INIT;
  ret.success = success;
  if (success) {
    ret.value.data = valueSerial;
    ret.value.len = valueSerial_len;
  }

	size_t retSerial_len = return__get_packed_size(&ret);
  *retSerial = (uint8_t *)malloc(4 + retSerial_len);
  if (*retSerial == NULL) {
    error = 1;
    goto errRetMalloc;
  }
	*(uint32_t *)*retSerial = htonl(retSerial_len);
  
  return__pack(&ret, *retSerial + 4);

  // Cleanup
errRetMalloc:
  if (success) {
    free(valueSerial);
  }
errInvalidName:
  call__free_unpacked(call, NULL);

  return error;
}

int handleIncomingClient(int servSock, struct client_frame *locals) {
	struct sockaddr_in clientAddr; // Client address
	// Set length of client address structure (in-out parameter)
	socklen_t clientAddr_len = sizeof(clientAddr);

	// Wait for a client to connect
	int clientSock = accept(servSock, (struct sockaddr *)&clientAddr, &clientAddr_len);
	if (clientSock < 0) {
		perror("accept() failed");
		exit(1);
	}
	fcntl(clientSock, F_SETFL, O_NONBLOCK);

	memset(locals, 0, sizeof(*locals));
	locals->buf = malloc(MAX_PAYLOAD_SIZE);

	// char clientName[INET_ADDRSTRLEN]; // String to contain client address
	// if (inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, clientName, sizeof(clientName)) != NULL) {
	// 	printf("Handling client %s/%d\n", clientName, ntohs(clientAddr.sin_port));
	// } else {
	// 	puts("Unable to get client address");
	// }
	return clientSock;
}

int main(int argc, char *argv[]) {
	struct pollfd ufds[1 + MAX_CLIENTS];
  struct client_frame *clients[MAX_CLIENTS];
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

	bool hasIncomingClient;
  ssize_t numBytes;
	for (;;) switch (poll(ufds, 1 + MAX_CLIENTS, -1)) { // Run forever
	case -1:
		perror("poll() failed");
		exit(1);
	case 0:
		puts("Waiting for activity");
		break;
	default:
		hasIncomingClient = ufds[0].revents & POLLIN;
		for (int i = 0; i < MAX_CLIENTS; i++) {
			if (ufds[i+1].fd < 0) {
				if (hasIncomingClient) { // Server can handle incoming connection
					clients[i] = malloc(sizeof(struct client_frame));
					ufds[i+1].fd = handleIncomingClient(servSock, clients[i]);
					ufds[i+1].events = POLLIN;
					hasIncomingClient = 0;
				} else continue;
			}
			if (ufds[i+1].revents & POLLIN) {
        size_t payload_len = ntohl(*(uint32_t *)clients[i]->buf);
        size_t expectedBytes;
        if (clients[i]->buf_len < 4) {
          expectedBytes = 4 - clients[i]->buf_len;
        } else {
          expectedBytes = 4 + payload_len - clients[i]->buf_len; 
        }
				if (clients[i]->buf_len + expectedBytes > MAX_PAYLOAD_SIZE) {
					printf("payload of size %lu is too large\n", clients[i]->buf_len + expectedBytes);
					clients[i]->closed = 1;
				} else if (expectedBytes) {
          numBytes = recv(ufds[i+1].fd, clients[i]->buf + clients[i]->buf_len, expectedBytes, 0);
          if (numBytes <= 0) {
            if (numBytes) perror("recv() failed");
            clients[i]->closed = 1;
          } else {
						clients[i]->buf_len += numBytes;
						payload_len = ntohl(*(uint32_t *)clients[i]->buf);
					}
        }
				if (clients[i]->buf_len >= 4 && clients[i]->buf_len == payload_len + 4) {
          uint8_t *retSerial;
					ufds[i+1].events &= ~POLLIN;
          if (handleCall(&retSerial, clients[i]->buf, clients[i])) {
            clients[i]->closed = 1;
          } else {
						clients[i]->buf_len = 4 + ntohl(*(uint32_t *)retSerial);
						memcpy(clients[i]->buf, retSerial, clients[i]->buf_len);
						free(retSerial);
						ufds[i+1].events |= POLLOUT;
					}
        }
			}
			if (ufds[i+1].revents & POLLOUT) {
				numBytes = send(ufds[i+1].fd, clients[i]->buf, clients[i]->buf_len, 0);
				if (numBytes <= 0) {
					if (numBytes) perror("recv() failed");
					clients[i]->closed = 1;
				} else {
					clients[i]->buf_len -= numBytes;
					memmove(clients[i]->buf, clients[i]->buf + numBytes, clients[i]->buf_len);
				}
				if (!clients[i]->buf_len) {
						ufds[i+1].events &= ~POLLOUT;
						ufds[i+1].events |= POLLIN;
				}
			}
      if (clients[i]->closed) {
				close(ufds[i+1].fd);
				ufds[i+1].fd = -1;
				free(clients[i]->buf);
				free(clients[i]);
			}
		}
    break;
	}
}
