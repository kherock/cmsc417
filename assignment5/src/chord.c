/**
 * Assignment 5 Chord Implementation
 * @author Kyle Herock
 */
#include <argp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include "chord.h"
#include "common.h"
#include "hash.h"

#define MAX_MESSAGE_SIZE 128

struct chord_arguments {
  struct sockaddr_in bindAddr;
  struct sockaddr_in chordAddr;
  int stabilizeInterval;
  int fixFingersInterval;
  int checkPredecessorInterval;
  int numSucessors;
};

char line[LINE_MAX];
struct chord_node me;
struct chord_node *finger[160];
struct Node *predecessor;
struct Node *successorList;

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

/**
 * Initializes a new RPC with the passed in context
 */
void createRPC(struct chord_node *ctx, char *name, size_t argsSerial_len, uint8_t *argsSerial) {
  // Serializing/Packing the call
  Call call = CALL__INIT;
  call.name = name;
  call.args.len = argsSerial_len;
  call.args.data = argsSerial;
  ctx->buf_len = 4 + call__get_packed_size(&call);
  *(uint32_t *)ctx->buf = htonl(ctx->buf_len - 4);
  call__pack(&call, ctx->buf + 4);
  ctx->ufd->events &= ~POLLIN;
  ctx->ufd->events |= POLLOUT;
  ctx->state = SEND;
}
/**
 * Attempts to run an initialized RPC to completion
 *
 * A return value of 0 indicates (at least partial) success
 * If nonblocking and a send/recv sets errno to EWOULDBLOCK, *ret is NULL
 * If blocking (or if no EWOULDBLOCK is encountered), *ret will point to the unpacked Return struct 
 */
int runRPC(Return **ret, struct chord_node *ctx) {
  int error = 0;
  ssize_t numBytes;
  *ret = NULL;
  switch (ctx->state) {
  case SEND:
    // Send message
    numBytes = send_all(ctx->ufd->fd, ctx->buf, ctx->buf_len, 0);
    if (numBytes < 0 && errno != EWOULDBLOCK) {
      if (numBytes == -1) perror("send()");
      error = -1;
      break;
    }
    ctx->buf_len -= numBytes;
    if (ctx->buf_len) {
      memmove(ctx->buf + numBytes, ctx->buf, ctx->buf_len);
      break;
    }
    ctx->state = RECV_HEAD;
    ctx->ufd->events &= ~POLLOUT;
    ctx->ufd->events |= POLLIN;
    // fall through
  case RECV_HEAD:
    numBytes = recv_all(ctx->ufd->fd, ctx->buf + ctx->buf_len, 4 - ctx->buf_len, 0);
    if (numBytes <= 0 && errno != EWOULDBLOCK) {
      if (numBytes == -1) perror("recv()");
      error = -1;
      break;
    }
    ctx->buf_len += numBytes;
    ctx->state = RECV;    
    // fall through
  case RECV:
    // Receive message
    numBytes = recv_all(ctx->ufd->fd, ctx->buf + ctx->buf_len, 4 + ntohl(*(uint32_t *)ctx->buf) - ctx->buf_len, 0);
    if (numBytes <= 0) {
      if (errno != EWOULDBLOCK) {
        if (numBytes == -1) perror("recv()");
        error = -1;
      }
      ctx->buf_len += numBytes;
      break;
    }
    // Deserialize/Unpack the return message
    Return *rpcRet = return__unpack(NULL, ntohl(*(uint32_t *)ctx->buf), ctx->buf + 4);
    if (rpcRet == NULL) {
      error = -1;
      break;
    }
    *ret = rpcRet;
    break;
  default:;
  }
  return error;
}
/**
 * Binds the implementations of RPCs to the passed in context
 */
int handleRPC(struct chord_node *ctx) {
  int error = 0;

  // Deserializing/Unpacking the call
  size_t callSerial_len = ntohl(*(uint32_t *)ctx->buf);
  uint8_t *callSerial = ctx->buf + 4;
  Call *call = call__unpack(NULL, callSerial_len, callSerial);
  if (call == NULL) {
    error = -1;
    goto err;
  }

  // Set the appropriate function based on the `name` field
  // Deserialize/Unpack the arguments and run the procedure
  if (strcmp(call->name, "find_successor") == 0) {
    FindSuccessorArgs *args = find_successor_args__unpack(NULL, call->args.len, call->args.data);
    if (args == NULL) {
      error = -1;
      goto errArgsUnpack;
    }
    if (args->id.len != 20) {
      error = -1;
      goto errBindFunc;
    }
    ctx->func = (int (*)(void))find_successor;
    ctx->args[0] = args->id.data;
  errBindFunc:
    find_successor_args__free_unpacked(args, NULL);
  } else {
    error = -1;
    goto errArgsUnpack;
  }
  ctx->ret_ctx = ctx;

errArgsUnpack:
  call__free_unpacked(call, NULL);
err:
  return error;
}
void returnRPC(struct chord_node *ctx, bool success, size_t valueSerial_len, uint8_t *valueSerial) {
  // Serializing/Packing the return, using the return value from the invoked function
  Return ret = RETURN__INIT;
  ret.success = success;
  ret.value.len = valueSerial_len;
  ret.value.data = valueSerial;

  ctx->buf_len = 4 + return__get_packed_size(&ret);
  *(uint32_t *)ctx->buf = htonl(ctx->buf_len - 4);
  return__pack(&ret, ctx->buf + 4);
  ctx->ufd->events &= ~POLLIN;
  ctx->ufd->events |= POLLOUT;
}

/**
 * Creates and runs the find_successor RPC
 */
int find_successor_rpc(Node **ret, struct chord_node *ctx, uint8_t *id) {
  int error = 0;
  *ret = NULL;
  switch (ctx->state) {
  case IDLE:;
    // Serializing/Packing the arguments
    FindSuccessorArgs args = FIND_SUCCESSOR_ARGS__INIT;
    args.id.len = 20;
    args.id.data = id;

    size_t argsSerial_len = find_successor_args__get_packed_size(&args);
    uint8_t *argsSerial = emalloc(argsSerial_len);

    find_successor_args__pack(&args, argsSerial);
    createRPC(ctx, "find_successor", argsSerial_len, argsSerial);
    // fall through
  default:;
    // Resume RPC
    Return *rpcRet;
    if ((error = runRPC(&rpcRet, ctx)) || rpcRet == NULL) break;
    // Deserialize/Unpack the return value of the call
    FindSuccessorRet *fsRet = find_successor_ret__unpack(NULL, rpcRet->value.len, rpcRet->value.data);
    return__free_unpacked(rpcRet, NULL);
    if (fsRet == NULL) {
      error = -1;
      break;
    }
    *ret = fsRet->node;
    free(fsRet); // Only free the containing struct
    break;
  }
  return error;
}
/**
 * Checks if id is in the half open range between this node and id. If not,
 * the query is forwarded around the ring. If the socket is nonblocking,
 * the find_successor RPC is bound to the context.
 */
int find_successor(Node **ret, struct chord_node *ret_ctx, uint8_t *id) {
  int error = 0;
  *ret = NULL;
  // id ∈ (me, successor] 
  if (in_range(id, me.id, finger[0]->id) || memcmp(id, finger[0]->id, 20) == 0) {
    *ret = emalloc(sizeof(Node));
    node__init(*ret);
    (*ret)->id.len = 20;
    (*ret)->id.data = finger[0]->id;
    (*ret)->address = finger[0]->addr.sin_addr.s_addr;
    (*ret)->port = finger[0]->addr.sin_port;
  } else {
    struct chord_node *n0 = closest_preceding_node(id);
    if (n0->ret_ctx == NULL) {
      if ((error = find_successor_rpc(ret, n0, id))) {
        n0->state = CLOSED;
      } else if (*ret == NULL) {
        n0->func = find_successor_rpc;
        n0->args[0] = (void *)id;
        n0->ret_ctx = ret_ctx;
      }
    }
  }
  return error;
}

struct chord_node *closest_preceding_node(uint8_t *id) {
  for (int i = 159; i >= 0; i--) {
    if (finger[i]->ufd->fd && in_range(finger[i]->id, me.id, id)) {
      return finger[i];
    }
  }
  return &me;
}

int create() {
  predecessor = NULL;
  finger[0] = emalloc(sizeof(struct chord_node));
  memcpy(finger[0]->id, me.id, 20);
  finger[0]->addr = me.addr;
  finger[0]->buf = emalloc(MAX_MESSAGE_SIZE);
  return 0;
}

int join(struct chord_node *n) {
  predecessor = NULL;
  Node *succ;
  if (find_successor_rpc(&succ, n, me.id) == -1) return -1;
  finger[0] = emalloc(sizeof(struct chord_node));
  memcpy(finger[0]->id, succ->id.data, 20);
  finger[0]->addr.sin_family = AF_INET;
  finger[0]->addr.sin_addr.s_addr = succ->address;
  finger[0]->addr.sin_port = succ->port;
  free(succ);
  return 0;
}

int stabilize() {
  return 0;
}

int notify(struct chord_node *ret_ctx, struct chord_node *n) {
  UNUSED(ret_ctx);
  UNUSED(n);
  return 0;
}

int fix_fingers() {
  return 0;
}

int check_predecessor() {
  return 0;
}

void printNode(struct chord_node *n) {
  for (int i = 0; i < 20; i++) {
    printf("%x", n->id[i]);
  }
  char name[INET_ADDRSTRLEN]; // String to contain client address
  inet_ntop(AF_INET, &n->addr.sin_addr.s_addr, name, sizeof(name));
  printf(" %s %d\n", name, ntohs(n->addr.sin_port));
}

void handleInput() {
  fgets(line, LINE_MAX, stdin);
  ssize_t len = memchr(line, '\n', LINE_MAX) - (void *)line;
  if (len < 0) return;
  line[len] = '\0';
  if (strcmp(line, "PrintState") == 0) {
    printf("Self ");
    printNode(&me);
    // Print fingers
    struct chord_node *lastFinger = finger[0];
    for (int i = 0; i < 160; i++) {
      printf("Finger[%d] ", i + 1);
      printNode(finger[i] ? finger[i] : lastFinger);
    }
  }
}

int handleIncomingConnection(struct chord_node **ret, struct pollfd *ufd) {
  *ret = emalloc(sizeof(struct chord_node));
  // Set length of client address structure (in-out parameter)
  struct chord_node *ctx = *ret;
  memset(ctx, 0, sizeof(*ctx));
  socklen_t addr_len = sizeof(ctx->addr);

  // Wait for a client to connect
  if ((ufd->fd = accept(me.ufd->fd, (struct sockaddr *)&ctx->addr, &addr_len)) < 0) {
    perror("accept() failed");
    free(ctx);
    return -1;
  }
  fcntl(ufd->fd, F_SETFL, O_NONBLOCK);
  ufd->events = POLLIN;

  ctx->buf = emalloc(MAX_MESSAGE_SIZE);
  ctx->ufd = ufd;

  // char name[INET_ADDRSTRLEN]; // String to contain client address
  // if (inet_ntop(AF_INET, &ctx->addr.sin_addr.s_addr, name, sizeof(name)) != NULL) {
  //   printf("Handling connection from %s/%d\n", name, ntohs(ctx->addr.sin_port));
  // } else {
  //   puts("Unable to get connection address");
  // }
  return 0;
}

int handleIncomingBytes(struct chord_node *ctx) {
  int error;
  size_t valueSerial_len = 0;
  uint8_t *valueSerial = NULL;
  if (ctx->func == NULL) error = handleRPC(ctx);
  if (error) goto err;
  if (ctx->func == find_successor
     || ctx->func == find_successor_rpc) {
    FindSuccessorRet fsRet = FIND_SUCCESSOR_RET__INIT;
    int (*func)(void *, struct chord_node *, uint8_t *) = ctx->func;
    if ((error = func(&fsRet.node, ctx, ctx->args[0]))) {
      goto err;
    }
    if (fsRet.node != NULL) {
      ctx->func = NULL;
      valueSerial_len = find_successor_ret__get_packed_size(&fsRet);
      valueSerial = emalloc(valueSerial_len);
      find_successor_ret__pack(&fsRet, valueSerial);
      node__free_unpacked(fsRet.node, NULL);
    }
  }
err:
  if (error) ctx->state = CLOSED;
  if (valueSerial || error) {
    returnRPC(ctx->ret_ctx, !error, valueSerial_len, valueSerial);
    free(valueSerial);
  }
  return error;
}

int main(int argc, char *argv[]) {
  struct chord_arguments args;
  struct sha1sum_ctx *hash_ctx = sha1sum_create(NULL, 0);
  int maxConnections = 8;

  parseopt(&args, argc, argv);
  struct pollfd *ufds = emalloc((2 + maxConnections) * sizeof(struct pollfd));
  struct chord_node **connections = emalloc(maxConnections * sizeof(void *));
  memset(finger, 0, sizeof(finger));

  ufds[0].fd = STDIN_FILENO;
  ufds[0].events = POLLIN;

  sha1sum_finish(hash_ctx, (uint8_t *)&args.bindAddr, sizeof(args.bindAddr), me.id);
  sha1sum_reset(hash_ctx);
  me.addr = args.bindAddr;
  me.ufd = &ufds[1];
  if ((me.ufd->fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP)) < 0) {
    perror("socket() failed");
    exit(1);
  }
  // Bind to the local address
  if (bind(me.ufd->fd, (struct sockaddr *)&args.bindAddr, sizeof(args.bindAddr)) < 0) {
    perror("bind() failed");
    exit(1);
  }
  me.ufd->events = POLLIN | POLLPRI;

  // Initialize the rest of the ufds array
  for (int i = 0; i < maxConnections; i++) ufds[i+2].fd = -1;

  // Connect to an existing ring if a Chord node is specified
  if (args.chordAddr.sin_family) {
    struct chord_node node;
    struct pollfd dummyUfd = { socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), POLLIN | POLLOUT, POLLIN | POLLOUT };
    if (dummyUfd.fd < 0) {
      perror("socket() failed");
      exit(1);
    }
    node.state = IDLE;
    node.ufd = &dummyUfd;
    node.addr = args.chordAddr;
    if (connect(node.ufd->fd, (struct sockaddr *)&node.addr, sizeof(node.addr)) < 0) {
      perror("connect() failed");
      exit(1);
    }
    node.buf = emalloc(MAX_MESSAGE_SIZE);
    if (join(&node) < 0) {
      fputs("An error occurred while joining the ring\n", stderr);
      exit(1);
    }
    free(node.buf);
    close(node.ufd->fd);
    finger[0]->ufd = &ufds[2];
    if ((finger[0]->ufd->fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP)) < 0) {
      perror("socket() failed");
      return -1;
    }
    if (connect(finger[0]->ufd->fd, (struct sockaddr *)&finger[0]->addr, sizeof(finger[0]->addr)) < 0
        && errno != EINPROGRESS) {
      perror("connect() failed");
      exit(1);
    }
    connections[0] = finger[0];
    stabilize();
  } else {
    if (create() < 0) {
      fputs("Unable to create a new ring\n", stderr);
      exit(1);
    }
  }

  // Mark the socket so it will listen for incoming connections
  if (listen(me.ufd->fd, SOMAXCONN) < 0) {
    perror("listen() failed");
    exit(1);
  }

  bool hasIncomingConnection;
  ssize_t numBytes;
  for (;;) switch (poll(ufds, 2 + maxConnections, -1)) {
    case -1:
      perror("poll() failed");
      exit(1);
    case 0:
    default:
      if (ufds[0].revents & POLLIN) handleInput();
      hasIncomingConnection = ufds[1].revents & POLLIN;
      for (int i = 0; i < maxConnections; i++) {
        if (ufds[i+2].fd < 0) {
          if (hasIncomingConnection) { // Server can handle incoming connection
            hasIncomingConnection = false;
            handleIncomingConnection(&connections[i], &ufds[i+2]);
          } else continue;
        }
        if (connections[i]->ufd->revents & POLLIN) {
          size_t payload_len = ntohl(*(uint32_t *)connections[i]->buf);
          size_t expectedBytes;
          if (connections[i]->buf_len < 4) {
            // We don't know the size of the payload yet
            expectedBytes = 4 - connections[i]->buf_len;
          } else {
            expectedBytes = 4 + payload_len - connections[i]->buf_len; 
          }
          if (connections[i]->buf_len + expectedBytes > MAX_MESSAGE_SIZE) {
            printf("payload of size %lu is too large\n", connections[i]->buf_len + expectedBytes);
            connections[i]->state = CLOSED;
          } else if (expectedBytes) {
            // Attempt to receive the rest of the expected bytes
            numBytes = recv(connections[i]->ufd->fd, connections[i]->buf + connections[i]->buf_len, expectedBytes, 0);
            if (numBytes <= 0) {
              if (numBytes) perror("recv() failed");
              connections[i]->state = CLOSED;
            } else {
              connections[i]->buf_len += numBytes;
              payload_len = ntohl(*(uint32_t *)connections[i]->buf);
            }
          }
          if (connections[i]->buf_len >= 4 && connections[i]->buf_len == payload_len + 4) {
            // Run the procedure call
            // printf("Received %lu bytes\n", connections[i]->buf_len);
            connections[i]->ufd->events &= ~POLLIN;
            connections[i]->ufd->events |= POLLOUT;
            handleIncomingBytes(connections[i]);
          }
        }
        if (connections[i]->ufd->revents & POLLOUT) {
          numBytes = send(connections[i]->ufd->fd, connections[i]->buf, connections[i]->buf_len, 0);
          if (numBytes <= 0) {
            if (numBytes) perror("recv() failed");
            connections[i]->state = CLOSED;
          } else {
            connections[i]->buf_len -= numBytes;
            memmove(connections[i]->buf, connections[i]->buf + numBytes, connections[i]->buf_len);
          }
          if (!connections[i]->buf_len) {
              connections[i]->ufd->events &= ~POLLOUT;
              connections[i]->ufd->events |= POLLIN;
          }
        }
        if (connections[i]->state == CLOSED) {
          close(connections[i]->ufd->fd);
          for (int j = 0; j < 160; j++) {
            if (finger[j] == connections[i]) finger[j] = NULL;
          }
          connections[i]->ufd->fd = -1;
          free(connections[i]->buf);
          free(connections[i]);
        }
      }
      // There's still an incoming connection, expand the number of connections
      if (hasIncomingConnection) {
        maxConnections *= 2;
        ufds = realloc(ufds, (2 + maxConnections) * sizeof(struct pollfd));
        connections = realloc(connections, maxConnections * sizeof(void *));
        if (!ufds || !connections) {
          perror("realloc() failed");
          exit(1);
        }
        // Initialize the new client slots
        for (int i = maxConnections / 2; i < maxConnections; i++) ufds[i+2].fd = -1;
      }
      break;
    }
}
