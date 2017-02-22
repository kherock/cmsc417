/**
 * Assignment 0 TCP Client Implementation
 * @author Kyle Herock
 */

#include <argp.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#define MAX_PAYLOAD 16777216

struct client_arguments {
	struct sockaddr_in servAddr;
	int hashnum;
	int smin;
	int smax;
	FILE *file; /* you can store this as a string, but I probably wouldn't */
	struct stat fstats;
};

error_t client_parser(int key, char *arg, struct argp_state *state) {
	struct client_arguments *args = state->input;
	error_t ret = 0;
	int num;
	switch (key) {
	case 'a':
		memset(&args->servAddr, 0, sizeof(args->servAddr)); // Zero out structures
		args->servAddr.sin_family = AF_INET; // IPv4 address family
		// Convert address
		if (!inet_pton(AF_INET, arg, &args->servAddr.sin_addr.s_addr)) {
			argp_error(state, "Invalid address");
		}
		break;
	case 'p':
		num = atoi(arg);
		if (num <= 0) {
			argp_error(state, "Invalid option for a port, must be a number greater than 0");
		}
		args->servAddr.sin_port = htons(num); // Server port
		break;
	case 'n':
		args->hashnum = atoi(arg);
		if (args->hashnum < 0) {
			argp_error(state, "hashreq must be a number >= 0");
		}
		break;
	case 300:
		args->smin = atoi(arg);
		if (args->smin <= 0) {
			argp_error(state, "smin must be a number >= 1");
		}
		break;
	case 301:
		args->smax = atoi(arg);
		if (!args->smax || args->smax > MAX_PAYLOAD) {
			argp_error(state, "smax must be a number <= 2^24");
		}
		break;
	case 'f':
		/* validate file */
		args->file = fopen(arg, "r");
		if (args->file == NULL) {
			argp_error(state, "\"%s\" does not exist or could not be opened for reading", arg);
		} else if (fstat(fileno(args->file), &args->fstats) < 0) {
			perror("fstat() failed");
			exit(1);
		}
		if (S_ISDIR(args->fstats.st_mode)) {
			argp_error(state, "\"%s\" is a directory", arg);
		}
		break;
	default:
		ret = ARGP_ERR_UNKNOWN;
		break;
	}
	return ret;
}


void client_parseopt(struct client_arguments *args, int argc, char *argv[]) {
	struct argp_option options[] = {
		{ "addr", 'a', "addr", 0, "The IP address the server is listening at", 0},
		{ "port", 'p', "port", 0, "The port that is being used at the server", 0},
		{ "hashreq", 'n', "hashreq", 0, "The number of hash requests to send to the server", 0},
		{ "smin", 300, "minsize", 0, "The minimum size for the data payload in each hash request", 0},
		{ "smax", 301, "maxsize", 0, "The maximum size for the data payload in each hash request", 0},
		{ "file", 'f', "file", 0, "The file that the client reads data from for all hash requests", 0},
		{0}
	};

	struct argp argp_settings = { options, client_parser, 0, 0, 0, 0, 0 };

	memset(args, 0, sizeof(*args));
	args->hashnum = -1;

	if (argp_parse(&argp_settings, argc, argv, 0, NULL, args) != 0) {
		printf("Got error in parse\n");
	}
	if (args->hashnum < 0) {
		fputs("hashreq option must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->smin) {
		fputs("smin option must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->smax) {
		fputs("smax option must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->servAddr.sin_addr.s_addr) {
		fputs("addr must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->servAddr.sin_port) {
		fputs("port must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->file) {
		fputs("file must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (args->fstats.st_size < args->hashnum * args->smax
			&& !S_ISBLK(args->fstats.st_mode) && !S_ISCHR(args->fstats.st_mode)) {
		fputs("File is too small\n", stderr);
		exit(EX_DATAERR);
	}
}

int main(int argc, char *argv[]) {
    struct client_arguments args;
	client_parseopt(&args, argc, argv);
	srand(time(NULL));

	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		perror("socket() failed");
		exit(1);
	}
	if (connect(sock, (struct sockaddr *)&args.servAddr, sizeof(args.servAddr)) < 0) {
		perror("connect() failed");
		exit(1);
	}

	unsigned int offset = 0;
	ssize_t numBytes;
	size_t sendBuf_len, recvBuf_len;
	uint8_t *sendBuf = malloc(6 + args.smax);
	uint8_t *recvBuf = malloc(36);
	
	*(uint32_t *)sendBuf = htonl(args.hashnum);
	sendBuf_len = 4;
	offset = 0;
	while (offset < sendBuf_len) {
		numBytes = send(sock, sendBuf + offset, sendBuf_len - offset, 0);
		if (numBytes < 0) {
			perror("send() failed");
			exit(1);
		}
		offset += numBytes;
	}
	numBytes = recv(sock, recvBuf, 4, 0);
	if (numBytes < 0) {
		perror("send() failed");
		exit(1);
	}
	// size_t response_len = ntohl(*(uint32_t *)recvBuf);

	// Send out HashRequests
	*(uint16_t *)sendBuf = htons(0x0417);
	for (int i = 0; i < args.hashnum; i++) {
		int l = args.smin + rand() / (RAND_MAX + 1.0) * (args.smax - args.smin + 1);
		*(uint32_t *)&sendBuf[2] = htonl(l);
		fgets((void *)&sendBuf[6], l, args.file);
		sendBuf_len = 6 + l;
		offset = 0;
		while (offset < sendBuf_len) {
			numBytes = send(sock, sendBuf + offset, sendBuf_len - offset, 0);
			if (numBytes < 0) {
				perror("send() failed");
				exit(1);
			}
			offset += numBytes;
		}
		offset = 0;
		recvBuf_len = 36;
		while (offset < recvBuf_len) {
			numBytes = recv(sock, recvBuf + offset, recvBuf_len - offset, 0);
			if (numBytes < 0) {
				perror("recv() failed");
				exit(1);
			}
			if (numBytes == 0) {
				fputs("Connection closed by host\n", stderr);
				exit(1);
			}
			offset += numBytes;
		}
		printf("%u: 0x", ntohl(*(uint32_t *)recvBuf));
		for (int i = 0; i < 32; i++) printf("%02x", recvBuf[i+4]);
		printf("\n");
	}
	close(sock);
	fclose(args.file);
	free(sendBuf);
	free(recvBuf);
	// puts("All done!");
}
