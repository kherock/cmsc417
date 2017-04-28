#include <stdbool.h>
#include <sys/types.h>

#ifndef _COMMON_H_
#define _COMMON_H_
#define UNUSED(x) (void)(x)

void *emalloc(unsigned size) {
  void *p = malloc(size);
  if (p == NULL) {
    fputs("Out of memory!\n", stderr);
    abort();
  }
  return p;
}

/**
 * Attempt to send all bytes
 */
ssize_t send_all(int sockfd, const void *buf, size_t len, int flags) {
  ssize_t numBytesSent;
  size_t offset = 0;
  while (offset < len) {
    numBytesSent = send(sockfd, buf + offset, len - offset, flags);
    if (numBytesSent <= 0) return numBytesSent;
    offset += numBytesSent;
  }
  return len;
}

/**
 * Attempt to receive all bytes
 */
ssize_t recv_all(int sockfd, void *buf, size_t len, int flags) {
  ssize_t numBytesRcvd;
  size_t offset = 0;
  while (offset < len) {
    numBytesRcvd = recv(sockfd, buf + offset, len - offset, flags);
    if (numBytesRcvd <= 0) return numBytesRcvd;
    offset += numBytesRcvd;
  }
  return len;
}

/**
 * Compares a 160-bit integer to see if it is within the interval (min, max)
 */
bool in_range(uint8_t *x, uint8_t *min, uint8_t *max) {
  switch (memcmp(min, max, 20)) {
  case -1:
    return memcmp(min, x, 20) < 0 && memcmp(x, max, 20) < 0;
  case 0:
    return true;
  case 1:
    return memcmp(x, max, 20) < 0 || memcmp(min, x, 20) < 0;
  }
  return false;
}

#endif
