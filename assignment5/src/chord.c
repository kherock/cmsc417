/**
 * Assignment 5 Chord Implementation
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
#include <sys/types.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include "chord.pb-c.h"
#include "hash.h"

#define MAX_PAYLOAD_SIZE 32

struct chord_arguments {
	struct sockaddr_in bindAddr;
	struct sockaddr_in chordAddr;
	int stabilizeInterval;
	int fixFingersInterval;
	int checkPredecessorInterval;
	int numSucessors;
};

int sock;
uint8_t buf[MAX_PAYLOAD_SIZE];

error_t args_parser(int key, char *arg, struct argp_state *state) {
	struct chord_arguments *args = state->input;
	error_t ret = 0;
	int num;
	switch (key) {
	case 'a':
		args->bindAddr.sin_family = AF_INET; // IPv4 address family
		// Convert address
		if (!inet_pton(AF_INET, arg, &args->bindAddr.sin_addr.s_addr)) {
			argp_error(state, "Invalid address");
		}
		break;
	case 'p':
		num = atoi(arg);
		if (num <= 0) {
			argp_error(state, "Invalid option for a port, must be a number greater than 0");
		}
		args->bindAddr.sin_port = htons(num); // Server port
		break;
	case 300:
		args->chordAddr.sin_family = AF_INET; // IPv4 address family
		// Convert address
		if (!inet_pton(AF_INET, arg, &args->chordAddr.sin_addr.s_addr)) {
			argp_error(state, "Invalid address");
		}
		break;
	case 301:
		num = atoi(arg);
		if (num <= 0) {
			argp_error(state, "Invalid option for a port, must be a number greater than 0");
		}
		args->chordAddr.sin_port = htons(num); // Server port
		break;
	case 302:
		args->stabilizeInterval = atoi(arg);
		if (args->stabilizeInterval < 1 || 60000 < args->stabilizeInterval) {
			argp_error(state, "stabilize interval must be in the range [1,60000]");
		}
		break;
	case 303:
		args->fixFingersInterval = atoi(arg);
		if (args->fixFingersInterval < 1 || 60000 < args->fixFingersInterval) {
			argp_error(state, "fix fingers interval must be in the range [1,60000]");
		}
		break;
	case 304:
		args->checkPredecessorInterval = atoi(arg);
		if (args->checkPredecessorInterval < 1 || 60000 < args->checkPredecessorInterval) {
			argp_error(state, "check predecessor interval must be in the range [1,60000]");
		}
		break;
	case 'r':
		args->numSucessors = atoi(arg);
		if (args->numSucessors < 1 || 32 < args->numSucessors) {
			argp_error(state, "successor count must be a number in the range [1,32]");
		}
		break;
	default:
		ret = ARGP_ERR_UNKNOWN;
		break;
	}
	return ret;
}


void parseopt(struct chord_arguments *args, int argc, char *argv[]) {
	struct argp_option options[] = {
		{ "addr", 'a', "addr", 0, "The IP address that the Chord client will bind to", 0 },
		{ "port", 'p', "port", 0, "The port that the Chord client will bind to and listen on", 0 },
		{ "ja", 300, "join_addr", 0, "The IP address of the machine running a Chord node", 0 },
		{ "jp", 301, "join_port", 0, "The port that an existing Chord node is bound to and listening on", 0 },
		{ "ts", 302, "time_stabilize", 0, "The time in milliseconds between invocations of 'stabilize'", 0 },
		{ "tff", 303, "time_fix_fingers", 0, "The time in milliseconds between invocations of 'fix_fingers'", 0 },
		{ "tcp", 304, "time_check_predecessor", 0, "The time in milliseconds between invocations of 'check_predecessor'", 0 },
		{ "sucessors", 'r', "num_successors", 0, "The number of successors maintained by the Chord client", 0 },
		{0}
	};

	struct argp argp_settings = { options, args_parser, 0, 0, 0, 0, 0 };

	memset(args, 0, sizeof(*args));

	if (argp_parse(&argp_settings, argc, argv, 0, NULL, args) != 0) {
		printf("Got error in parse\n");
		exit(1);
	}
	if (!args->bindAddr.sin_addr.s_addr) {
		fputs("chord: addr must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->bindAddr.sin_port) {
		fputs("chord: port must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->chordAddr.sin_addr.s_addr != !args->chordAddr.sin_port) {
		fputs("chord: both addr and port of an existing Chord node must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->stabilizeInterval) {
		fputs("chord: stabilize interval must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->fixFingersInterval) {
		fputs("chord: fix_fingers interval must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->checkPredecessorInterval) {
		fputs("chord: check_predecessor interval must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->numSucessors) {
		fputs("chord: successor count must be specified\n", stderr);
		exit(EX_USAGE);
	}
}

int handleCall(uint8_t *retSerial, const uint8_t *callSerial) {
	int error = 0;
	ssize_t numBytes;

	send(sock, callSerial, 4 + ntohl(*(uint32_t *)callSerial), 0);
	numBytes = recv(sock, retSerial, 4, 0);
	if (numBytes <= 0) {
		if (numBytes) perror("recv() failed");
		return 1;
	}

	numBytes = recv(sock, retSerial + 4, ntohl(*(uint32_t *)retSerial), 0);
	if (numBytes <= 0) {
		if (numBytes) perror("recv() failed");
		return 1;
	}
	
	return error;
}

int main(int argc, char *argv[]) {
  struct chord_arguments args;
  parseopt(&args, argc, argv);
  struct pollfd *ufds = malloc((1 + args.numSucessors) * sizeof(struct pollfd));

  // Create socket for incoming connections
  int servSock;
  if ((servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    perror("socket() failed");
    exit(1);
  }
  fcntl(servSock, F_SETFL, O_NONBLOCK);
  ufds[0].fd = servSock;
  ufds[0].events = POLLIN | POLLPRI;
  
  // Initialize the rest of the ufds array
  for (int i = 0; i < args.numSucessors; i++) ufds[i+1].fd = -1;

  // Bind to the local address
  if (bind(servSock, (struct sockaddr *)&args.bindAddr, sizeof(args.bindAddr)) < 0) {
    perror("bind() failed");
    exit(1);
  }
	 
  // Mark the socket so it will listen for incoming connections
  if (listen(servSock, SOMAXCONN) < 0) {
    perror("listen() failed");
    exit(1);
  }

  // Connect to an existing ring if a Chord node is specified
//   if (args.chordAddr.sin_family) {
//     sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
//     if (sock < 0) {
//         perror("socket() failed");
//         exit(1);
//     }
//     if (connect(sock, (struct sockaddr *)&args.chordAddr, sizeof(args.chordAddr)) < 0) {
//         perror("connect() failed");
//         exit(1);
//     }
//   }
  for (;;) switch(poll(ufds, 1 + args.numSucessors, -1)) {
    case -1:
      perror("poll() failed");
      exit(1);
	case 0:
    default:
      break;
  }
}
