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
#define MAX_PAYLOAD_SIZE 4096

struct server_arguments {
	int port;
};

struct client_frame {
  bool closed;
  uint8_t *buf;
  size_t buf_len;
  int totalSum;
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

int addWrapper(uint8_t **valueSerial, size_t *valueSerialLen, const uint8_t *argsSerial, size_t argsSerialLen) {
  int error = 0;

  // Deserialize/Unpack the arguments
  AddArguments *args = add_arguments__unpack(NULL, argsSerialLen, argsSerial);
  if(args == NULL) {
    return 1;
  }

  // Call the underlying function
  uint32_t sum = add(args->a, args->b);

  // Serialize/Pack the return value
  AddReturnValue value = ADD_RETURN_VALUE__INIT;
  value.sum = sum;

  *valueSerialLen = add_return_value__get_packed_size(&value);
  *valueSerial = (uint8_t *)malloc(*valueSerialLen);
  if (*valueSerial == NULL) {
    error = 1;
    goto errValueMalloc;
  }
  
  add_return_value__pack(&value, *valueSerial);

  // Cleanup
errValueMalloc:
  add_arguments__free_unpacked(args, NULL);

  return error;
}

int handleCall(uint8_t **retSerial, size_t *retSerialLen, const uint8_t *callSerial, size_t callSerialLen) {
  int error = 0;

  // Deserializing/Unpacking the call
  Call *call = call__unpack(NULL, callSerialLen, callSerial);
  if (call == NULL) {
    return 1;
  }

  // Calling the appropriate wrapper function based on the `name' field
  uint8_t *valueSerial;
  size_t valueSerialLen;
  bool success;

  if (strcmp(call->name, "add") == 0) {
    success = !addWrapper(&valueSerial, &valueSerialLen, call->args.data, call->args.len);
  } else {
    error = 1;
    goto errInvalidName;
  }

  // Serializing/Packing the return, using the return value from the invoked function
  Return ret = RETURN__INIT;
  ret.success = success;
  if (success) {
    ret.value.data = valueSerial;
    ret.value.len = valueSerialLen;
  }

  *retSerialLen = return__get_packed_size(&ret);
  *retSerial = (uint8_t *)malloc(*retSerialLen);
  if (*retSerial == NULL) {
    error = 1;
    goto errRetMalloc;
  }
  
  return__pack(&ret, *retSerial);

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
	socklen_t clientAddrLen = sizeof(clientAddr);

	// Wait for a client to connect
	int clientSock = accept(servSock, (struct sockaddr *)&clientAddr, &clientAddrLen);
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
        size_t payload_len = ntohl(*(uint32_t *)&clients[i]->buf[2]);
        size_t expectedBytes;
        if (clients[i]->buf_len < 6) {
          expectedBytes = 6 - clients[i]->buf_len;
        } else if (clients[i]->buf_len == 6 + payload_len) {
          expectedBytes = payload_len - clients[i]->buf_len - 6; 
        }
        if (expectedBytes) {
          numBytes = recv(ufds[i+1].fd, clients[i]->buf + clients[i]->buf_len, expectedBytes, 0);
          if (numBytes <= 0) {
            if (numBytes) perror("recv() failed");
            clients[i]->closed = 1;
          }
        } else if (clients[i]->buf_len == 6 + payload_len) {
          uint8_t *retSerial;
          size_t retSerialLen;
          if (handleCall(&retSerial, &retSerialLen, clients[i]->buf, clients[i]->buf_len)) {
            clients[i]->closed = 1;
          }
        }
			}
			if (ufds[i+1].revents & POLLOUT) {
			}
      if (clients[i]->closed) {
				close(ufds[i+1].fd);
				ufds[i+1].fd = -1;
				free(clients[i]);
			}
		}
    break;
	}
}
