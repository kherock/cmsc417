#include <argp.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include "rpc.pb-c.h"

#define MAX_PAYLOAD_SIZE 32

struct client_arguments {
	struct sockaddr_in servAddr;
	int addInterval;
	int addTotalInterval;
};

int sock;
uint8_t buf[MAX_PAYLOAD_SIZE];


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
	case 't':
		args->addInterval = atoi(arg);
		if (args->addInterval < 0 || 30 < args->addInterval) {
			argp_error(state, "add interval must be in the range [0,30]");
		}
		break;
	case 'n':
		args->addTotalInterval = atoi(arg);
		if (args->addTotalInterval <= 0 || 10 < args->addTotalInterval) {
			argp_error(state, "addTotal interval must be a number in the range [1,10]");
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
		{ "addr", 'a', "addr", 0, "The IP address the server is listening at", 0 },
		{ "port", 'p', "port", 0, "The port that is being used at the server", 0 },
		{ "time", 't', "add_interval", 0, "The interval in seconds to send 'add' RPCs", 0 },
		{ "totaln", 'n', "total_interval", 0, "The number of 'add' RPCs between 'getAddTotal' RPCs", 0 },
		{0}
	};

	struct argp argp_settings = { options, client_parser, 0, 0, 0, 0, 0 };

	memset(args, 0, sizeof(*args));
	args->addInterval = -1;

	if (argp_parse(&argp_settings, argc, argv, 0, NULL, args) != 0) {
		printf("Got error in parse\n");
	}
	if (!args->servAddr.sin_addr.s_addr) {
		fputs("addr must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->servAddr.sin_port) {
		fputs("port must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (args->addInterval < 0) {
		fputs("add interval must be specified\n", stderr);
		exit(EX_USAGE);
	}
	if (!args->addTotalInterval) {
		fputs("getAddTotal interval must be specified\n", stderr);
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

int callAdd(uint32_t *sum, uint32_t a, uint32_t b) {
  int error = 0;

  // Serializing/Packing the arguments
  AddArguments args = ADD_ARGUMENTS__INIT;
  args.a = a;
  args.b = b;

  size_t argsSerial_len = add_arguments__get_packed_size(&args);
  uint8_t *argsSerial = (uint8_t *)malloc(argsSerial_len);
  if (argsSerial == NULL) {
    return 1;
  }

  add_arguments__pack(&args, argsSerial);

  // Serializing/Packing the call, which also placing the serialized/packed
  // arguments inside
  Call call = CALL__INIT;
  call.name = "add";
  call.args.len = argsSerial_len;
  call.args.data = argsSerial;

  size_t callSerial_len = call__get_packed_size(&call);
  uint8_t *callSerial = (uint8_t *)malloc(4 + callSerial_len);
  if (callSerial == NULL) {
    error = 1;
    goto errCallMalloc;
  }
	*(uint32_t *)callSerial = htonl(callSerial_len);

  call__pack(&call, callSerial + 4);

  // Send and receive message
  uint8_t *retSerial = buf;
	if (handleCall(retSerial, callSerial)) {
		error = 1;
		goto errRetUnpack;
	}

  // Deserialize/Unpack the return message
  Return *ret = return__unpack(NULL, ntohl(*(uint32_t *)retSerial), retSerial + 4);
  if (ret == NULL) {
    error = 1;
    goto errRetUnpack;
  }

  // Deserialize/Unpack the return value of the add call
  AddReturnValue *value = add_return_value__unpack(NULL, ret->value.len, ret->value.data);
  if (value == NULL) {
    error = 1;
    goto errValueUnpack;
  }

  if (!ret->success) {
    error = 1;
    printf("add(%d, %d) RPC failed!\n", a, b);
  } else {
    *sum = value->sum;
  }

  // Cleanup
  add_return_value__free_unpacked(value, NULL);
errValueUnpack:
  return__free_unpacked(ret, NULL);
errRetUnpack:
  free(callSerial);
errCallMalloc:
  free(argsSerial);

  return error;
}

int callGetAddTotal(uint32_t *sum) {
  int error = 0;

  // Serializing/Packing the call, which also placing the serialized/packed
  // arguments inside
  Call call = CALL__INIT;
  call.name = "getAddTotal";
  call.args.len = 0;
  call.args.data = NULL;

  size_t callSerial_len = call__get_packed_size(&call);
  uint8_t *callSerial = (uint8_t *)malloc(4 + callSerial_len);
  if (callSerial == NULL) {
    error = 1;
    goto errCallMalloc;
  }
	*(uint32_t *)callSerial = htonl(callSerial_len);

  call__pack(&call, callSerial + 4);

  // Send and receive message
  uint8_t *retSerial = buf;
	if (handleCall(retSerial, callSerial)) {
		error = 1;
		goto errRetUnpack;
	}

  // Deserialize/Unpack the return message
  Return *ret = return__unpack(NULL, ntohl(*(uint32_t *)retSerial), retSerial + 4);
  if (ret == NULL) {
    error = 1;
    goto errRetUnpack;
  }

  // Deserialize/Unpack the return value of the add call
  AddReturnValue *value = add_return_value__unpack(NULL, ret->value.len, ret->value.data);
  if (value == NULL) {
    error = 1;
    goto errValueUnpack;
  }

  if (!ret->success) {
    error = 1;
    printf("getAddTotal() RPC failed!\n");
  } else {
    *sum = value->sum;
  }

  // Cleanup
  add_return_value__free_unpacked(value, NULL);
errValueUnpack:
  return__free_unpacked(ret, NULL);
errRetUnpack:
  free(callSerial);
errCallMalloc:

  return error;
}

int main(int argc, char *argv[]) {
  struct client_arguments args;
	client_parseopt(&args, argc, argv);
	srand(time(NULL));

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		perror("socket() failed");
		exit(1);
	}
	if (connect(sock, (struct sockaddr *)&args.servAddr, sizeof(args.servAddr)) < 0) {
		perror("connect() failed");
		exit(1);
	}

  uint32_t a, b, sum;
	int n = args.addTotalInterval;
	for (;; sleep(args.addInterval)) {
		a = rand() / ((double)RAND_MAX + 1.0) * 1001;
		b = rand() / ((double)RAND_MAX + 1.0) * 1001;
	  callAdd(&sum, a, b);
		printf("%d %d %d\n", a, b, sum);
		if (!--n) {
			callGetAddTotal(&sum);
			printf("%d\n", sum);
			n = args.addTotalInterval;
		}
	}
}
