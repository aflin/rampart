#ifndef __EVHTP_SHA1_H__
#define __EVHTP_SHA1_H__

typedef struct sha1_ctx sha1_ctx;

struct sha1_ctx {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t  buffer[64];
};


/* standard API for incremental building a sha1 hash */
extern void sha1_init(sha1_ctx *ctx);
extern void sha1_update(sha1_ctx *ctx, uint8_t *data, size_t len);
extern void sha1_finalize(sha1_ctx * ctx, uint8_t digest[20]);

/* build a sha1 hash of a fixed amount of data */
extern void      sha1_data(void *data, size_t len, uint8_t digest[20]);
extern char    * sha1_tostr(uint8_t digest[20], char sha1[41]);
extern uint8_t * str_tosha1(char sha1[41], uint8_t digest[20]);

#endif

