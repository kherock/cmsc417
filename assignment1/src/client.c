/**
 * Assignment 1 NTP Client Implementation
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
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include "htonll.h"

#define TRQST_LEN 22
#define TRESP_LEN 38

static uint8_t TRQST_BUF[TRESP_LEN];

struct client_arguments {
	struct sockaddr_in servAddr;
	int n;
	time_t t;
};

struct trequest {
	time_t ttl;
	int timed_out;
	double theta;
	double delta;
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
		args->n = atoi(arg);
		if (args->n < 0) {
			argp_error(state, "timereq must be a number >= 0");
		}
		break;
	case 't':
		args->t = 1000 * atoi(arg);
		if (args->t < 0) {
			argp_error(state, "timeout must be a number >= 0");
		}
		if (!args->t) args->t = -1; // Mimic poll() treatment of the value -1
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
		{ "timereq", 'n', "timereq", 0, "The number of time requests to send to the server", 0},
		{ "timeout", 't', "timeout", 0, "The time in seconds to wait after sending or receiving a response before terminating", 0},
		{0}
	};

	struct argp argp_settings = { options, client_parser, 0, 0, 0, 0, 0 };

	memset(args, 0, sizeof(*args));
	args->n = -1;
	args->t = -1;

	if (argp_parse(&argp_settings, argc, argv, 0, NULL, args) != 0) {
		printf("Got error in parse\n");
		exit(1);
	}
	if (!args->servAddr.sin_addr.s_addr) {
		fputs("addr must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->servAddr.sin_port) {
		fputs("port must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (args->n < 0) {
		fputs("timereq option must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->t) {
		fputs("timeout option must be specified\n", stderr);
		exit(EX_USAGE);
	}
}

int main(int argc, char *argv[]) {
    struct client_arguments args;
	client_parseopt(&args, argc, argv);
	srand(time(NULL));

	struct pollfd sock;
	sock.fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock.fd < 0) {
		perror("socket() failed");
		exit(1);
	}
	sock.events = POLLIN | POLLOUT;
	fcntl(sock.fd, F_SETFL, O_NONBLOCK);

	struct timespec timeSpec0;
	struct trequest *trqsts = malloc(args.n * sizeof(struct trequest));
	for (int i = 0; i < args.n; i++) trqsts->ttl = -1;

	int seqNum = 1;
	ssize_t numBytes;
	time_t elapsed, lastPoll = 0;
	long timeout = -1;
	int done = 0;
	
	// Send out TimeRequests
	*(uint16_t *)TRQST_BUF = htons(0x0417);
	while (!done) switch (poll(&sock, 1, timeout)) {
	case -1:
		perror("poll() failed");
		exit(1);
	case 0:
		puts("Waiting");
		elapsed = lastPoll;
		lastPoll = time(NULL);
		elapsed = lastPoll - elapsed;
		timeout = ~0;
		for (int i = 0; i < args.n; i++) {
			if (trqsts[i].ttl == -1) continue;
			if (trqsts[i].ttl - elapsed <= 0) {
				trqsts[i].ttl = -1;
				trqsts[i].timed_out = 1;
			} else if (trqsts[i].ttl -= elapsed < timeout) {
				timeout = trqsts[i].ttl;
			}
		}
		break;
	default:
		elapsed = lastPoll;
		lastPoll = time(NULL);
		// skip on first run
		if (elapsed) elapsed = lastPoll - elapsed;
		if (sock.revents & POLLIN) {
			puts("receiving");
			numBytes = recvfrom(sock.fd, TRQST_BUF, TRESP_LEN, 0, NULL, 0);
			if (numBytes < 0) {
				perror("recvfrom() failed");
				exit(1);
			}
			struct timespec timeSpec2;
			clock_gettime(CLOCK_REALTIME, &timeSpec2);
			int res_seqNum = ntohl(*(uint32_t *)TRQST_BUF);
			if (trqsts[res_seqNum -1].ttl == -1) break;
			trqsts[res_seqNum - 1].ttl = -1;
			time_t sec0 = ntohll(*(uint64_t *)&TRQST_BUF[6]),
			       sec1 = ntohll(*(uint64_t *)&TRQST_BUF[22]),
			       sec2 = timeSpec2.tv_sec;
			long nsec0 = ntohll(*(uint64_t *)&TRQST_BUF[14]),
			     nsec1 = ntohll(*(uint64_t *)&TRQST_BUF[30]),
				 nsec2 = timeSpec2.tv_sec;
			time_t sec = sec1 - sec0 + sec1 - sec2;
			long nsec = nsec1 - nsec0 + nsec1 - nsec2;
			if (nsec < 0) {
				nsec += 1000000000;
				sec--;
			}
			trqsts[res_seqNum - 1].theta = (double)sec / 2.0 + (double)nsec / 2000000000.0;
			trqsts[res_seqNum - 1].delta = (double)(sec2 - sec0) + (double)(nsec2 - nsec0) / 1000000000.0;
		}
		if (sock.revents & POLLOUT) {
			clock_gettime(CLOCK_REALTIME, &timeSpec0);
			*(uint32_t *)&TRQST_BUF[2] = htonl(seqNum);
			*(uint64_t *)&TRQST_BUF[6] = htonll((uint64_t)timeSpec0.tv_sec);
			*(uint64_t *)&TRQST_BUF[14] = htonll((uint64_t)timeSpec0.tv_nsec);

			numBytes = sendto(sock.fd, TRQST_BUF, TRQST_LEN, 0, (struct sockaddr *)&args.servAddr, sizeof(args.servAddr));
			if (errno != EAGAIN) {
				if (numBytes < 0) {
					perror("sendto() failed");
					exit(1);
				}
				printf("resetting ttl to %ld\n", args.t);
				trqsts[seqNum - 1].ttl = args.t;
				if (++seqNum == args.n) sock.events &= ~POLLOUT; // Stop sending after last seqNum
			}
		}
		timeout = ~0;
		for (int i = 0; i < args.n; i++) {
			if (trqsts[i].ttl == -1) continue;
			if (trqsts[i].ttl - elapsed <= 0) {
				trqsts[i].ttl = -1;
				trqsts[i].timed_out = 1;
			} else if (trqsts[i].ttl -= elapsed < timeout) {
				timeout = trqsts[i].ttl;
			}
		}
		break;
	}
	close(sock.fd);
	free(trqsts);
	puts("All done!");
}
