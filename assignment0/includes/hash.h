#ifndef HASH_H
#define HASH_H

#include <stdint.h>
#include <stddef.h>

struct checksum_ctx;

#define UPDATE_PAYLOAD_SIZE 4096

/* This takes an initial salt and salt length and returns a context
 * that can be used with the other functions. If len is 0, salt can be
 * NULL. Returns NULL on error */
struct checksum_ctx * checksum_create(const uint8_t *salt, size_t len);

/* With a valid context, add the payload to the hash. Payload must
 * have a length of 4096 bytes. Repeated calls of update will let you
 * compute the hash incrementally (4096 bytes at a time). Function
 * returns 0 on success.
 */
int checksum_update(struct checksum_ctx *, const uint8_t *payload);

/* With a valid context, add the payload (with a specified length) to
 * the current hash and output the full checksum into out. out must
 * have enough space to write 32 bytes of output. Function returns 0
 * on success. Note that you can compute a sha256 checksum by calling
 * checksum_finish directly, since checksum_finish allows for unbounded
 * length payloads while checksum_update only handles 4096 byte payloads.
 * After this call, the context is no longer in a valid state
 * and must be either reset or destroyed
 */
int checksum_finish(struct checksum_ctx*, const uint8_t *payload, size_t len, uint8_t *out);

/* Reset the context to prepare it to computer another hash. This
 * reuses the originally given salt. This is equivalent (but more
 * efficient than) destroying and recreating the context for each hash
 * that you want to compute. Returns 0 on success */
int checksum_reset(struct checksum_ctx*);

/* Destroy the context. This frees all memory and resources associated
 * with the context */
int checksum_destroy(struct checksum_ctx*);

#endif

