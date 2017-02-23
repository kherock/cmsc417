#ifndef HTONLL_H
#define HTONLL_H

#define ntohll htonll

uint64_t htonll(uint64_t n) {
#if __BYTE_ORDER == __BIG_ENDIAN
    return n; 
#else
    return (((uint64_t)htonl(n)) << 32) + htonl(n >> 32);
#endif
}

#endif /* HTONLL_H */
