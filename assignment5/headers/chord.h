#include <sys/types.h>

#include "chord.pb-c.h"

#ifndef _CHORD_H_
#define _CHORD_H_

enum txState {
  IDLE,
  SEND,
  RECV_HEAD,
  RECV,
  CLOSED
};

struct chord_node {
  uint8_t id[20]; 
  struct sockaddr_in addr;
  struct pollfd *ufd;
  enum txState state;
  size_t buf_len;
  uint8_t *buf;
  int (*func)();
  void *args[1]; // Increased if functions require more extra arguments

  // Keeps track of who needs the return value of the current procedure
  // so someone else doesn't interrupt it.
  struct chord_node *ret_ctx;
};

int find_successor(Node **ret, struct chord_node *ret_ctx, uint8_t *id);

int get_predecessor(Node **ret, struct chord_node *ret_ctx);

struct chord_node *closest_preceding_node(uint8_t *id);

int join(struct chord_node *n);

#endif
