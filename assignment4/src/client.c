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

struct client_arguments {
	struct sockaddr_in servAddr;
	int addInterval;
	int addTotalInterval;
};

int sock;


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
		{ "totalint", 'n', "total_interval", 0, "The number of 'add' RPCs between 'getAddTotal' RPCs", 0 },
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

int callAdd(uint32_t *sum, uint32_t a, uint32_t b) {
  int error = 0;

  // Serializing/Packing the arguments
  AddArguments args = ADD_ARGUMENTS__INIT;
  args.a = a;
  args.b = b;

  size_t argsSerialLen = add_arguments__get_packed_size(&args);
  uint8_t *argsSerial = (uint8_t *)malloc(argsSerialLen);
  if (argsSerial == NULL) {
    return 1;
  }

  add_arguments__pack(&args, argsSerial);

  // Serializing/Packing the call, which also placing the serialized/packed
  // arguments inside
  Call call = CALL__INIT;
  call.name = "add";
  call.args.len = argsSerialLen;
  call.args.data = argsSerial;

  size_t callSerialLen = call__get_packed_size(&call);
  uint8_t *callSerial = (uint8_t *)malloc(callSerialLen);
  if (callSerial == NULL) {
    error = 1;
    goto errCallMalloc;
  }

  call__pack(&call, callSerial);

  Return retInit = RETURN__INIT;
  size_t retSerialLen = return__get_packed_size(&retInit);
  uint8_t *retSerial = (uint8_t *)malloc(retSerialLen);

  // Send and receive message

  // Deserialize/Unpack the return message
  Return *ret = return__unpack(NULL, retSerialLen, retSerial);
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

  if (ret->success) {
    *sum = value->sum;
    printf("add(%d, %d) returned %d\n", a, b, *sum);
  } else {
    error = 1;
    printf("add(%d, %d) RPC failed!\n", a, b);
  }

  // Cleanup
  add_return_value__free_unpacked(value, NULL);
errValueUnpack:
  return__free_unpacked(ret, NULL);
errRetUnpack:
  free(retSerial);
  free(callSerial);
errCallMalloc:
  free(argsSerial);

  return error;
}

int main(int argc, char *argv[]) {
  struct client_arguments args;
	client_parseopt(&args, argc, argv);
	srand(time(NULL));

  uint32_t a = rand() / ((double)RAND_MAX + 1.0) * 1001;
  uint32_t b = rand() / ((double)RAND_MAX + 1.0) * 1001;

  uint32_t sum;
  callAdd(&sum, a, b);

  return 0;
}
