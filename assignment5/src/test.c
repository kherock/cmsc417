#include "hash.h"

#include <stdio.h>
#include <stdlib.h>

/* This program reads first argument as file and outputs the sha256
 * sum on EOF. It does this twice on the same file to show how to
 * reuse the hash context.
 */

#define UPDATE_PAYLOAD_SIZE 8192

int main(int argc, char *argv[]) {

	if (argc == 1) {
		abort();
	}
	FILE *fp = fopen(argv[1], "r");
	if (!fp) {
		abort();
	}
	uint8_t buffer[UPDATE_PAYLOAD_SIZE];
	
	struct sha1sum_ctx *ctx = sha1sum_create(NULL, 0);
	if (!ctx) {
		fprintf(stderr, "Error creating checksum\n");
		return 0;
	}

	for(size_t i = 0; i < 2; ++i) {
		size_t chunk_len = 0;
		while(!feof(fp)) {
			size_t cnt = UPDATE_PAYLOAD_SIZE - chunk_len;
			size_t ret = fread(buffer + chunk_len, 1, cnt, fp);
			if (ret != cnt) {
				if (ferror(fp)) {
					perror("Error reading from file: ");
					abort();
				}
			}
			chunk_len += ret;
			if (chunk_len == UPDATE_PAYLOAD_SIZE) {
				sha1sum_update(ctx, buffer, UPDATE_PAYLOAD_SIZE);
				chunk_len = 0;
			} 
		}

		uint8_t checksum[20];
		sha1sum_finish(ctx, buffer, chunk_len, checksum);

		printf("0x");
		for(size_t i = 0; i < 20; ++i) {
			printf("%02x", checksum[i]);
		}
		putchar('\n');

		rewind(fp);
		sha1sum_reset(ctx);
	}

	sha1sum_destroy(ctx);
	fclose(fp);

	return EXIT_SUCCESS;
}
