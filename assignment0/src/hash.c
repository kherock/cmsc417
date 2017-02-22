#include <string.h>
#include <strings.h>

#include "hash.h"

/* third party libraries */
#include <openssl/evp.h>

/* You shouldn't have to be looking at this file, but have fun! */


struct checksum_ctx {
	EVP_MD_CTX ctx;
	const EVP_MD *md;
	uint8_t *salt;
	size_t len;
};


struct checksum_ctx * checksum_create(const uint8_t *salt, size_t len) {
	struct checksum_ctx *csm = malloc(sizeof(*csm));
	if (!csm) {
		goto err;
	}
	bzero(csm, sizeof(*csm));
	EVP_MD_CTX_init(&csm->ctx);
	csm->md = EVP_sha256();
	csm->len = len;
	if (len > 0) {
		csm->salt = malloc(len);
		if (!csm->salt) {
			goto err;
		}
		memcpy(csm->salt, salt, len);
	}
	if (checksum_reset(csm)) {
		goto err;
	}


	return csm;

  err:
	if (csm) {
		if (csm->salt) {
			free(csm->salt);
		}
		bzero(csm, sizeof(*csm));
		free(csm);
	}
	csm = NULL;

	return csm;

		
	
}

int checksum_update(struct checksum_ctx *csm, const uint8_t *payload) {
	return EVP_DigestUpdate(&csm->ctx, payload, UPDATE_PAYLOAD_SIZE) != 1;
}

int checksum_finish(struct checksum_ctx *csm, const uint8_t *payload, size_t len, uint8_t *out) {
	int ret = 1;
	if (len) {
		ret = EVP_DigestUpdate(&csm->ctx, payload, len);
	}
	if (ret == 1) {
		return EVP_DigestFinal_ex(&csm->ctx, out, NULL) != 1;
	} else {
		return 1;
	}
}

int checksum_reset(struct checksum_ctx *csm) {
	EVP_DigestInit_ex(&csm->ctx, csm->md, NULL);
	if (csm->len) {
		return EVP_DigestUpdate(&csm->ctx, csm->salt, csm->len) != 1;
	} else {
		return 0;
	}
	
}

int checksum_destroy(struct checksum_ctx *csm) {
	EVP_MD_CTX_cleanup(&csm->ctx);
	free(csm->salt);
	bzero(csm, sizeof(*csm));
	free(csm);
	return 0;
}

