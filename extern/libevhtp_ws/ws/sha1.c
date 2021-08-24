#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>

/* SHA-1 origional by Steve Reid <steve@edmweb.com> */
#include <limits.h>

#include "../include/internal.h"
#include "sha1.h"

typedef union CHAR64LONG16 CHAR64LONG16;

union CHAR64LONG16 {
    uint8_t  c[64];
    uint32_t l[16];
};

#define rol(value, bits)     (((value) << (bits)) | ((value) >> (32 - (bits))))

static void _sha1_transform(uint32_t state[5], uint8_t buffer[64]);

#if HOST_BIGENDIAN
# define blk0(i)             block->l[i]
#else
# define blk0(i)             (block->l[i] = (rol(block->l[i], 24) & 0xFF00FF00) \
                                            | (rol(block->l[i], 8) & 0x00FF00FF))
#endif

#define blk(i)               (block->l[i & 15] = rol(block->l[(i + 13) & 15] ^ block->l[(i + 8) & 15] \
                                                     ^ block->l[(i + 2) & 15] ^ block->l[i & 15], 1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0(v, w, x, y, z, i) z += ((w & (x ^ y)) ^ y) + blk0(i) + 0x5A827999 + rol(v, 5); w = rol(w, 30);
#define R1(v, w, x, y, z, i) z += ((w & (x ^ y)) ^ y) + blk(i) + 0x5A827999 + rol(v, 5); w = rol(w, 30);
#define R2(v, w, x, y, z, i) z += (w ^ x ^ y) + blk(i) + 0x6ED9EBA1 + rol(v, 5); w = rol(w, 30);
#define R3(v, w, x, y, z, i) z += (((w | x) & y) | (w & x)) + blk(i) + 0x8F1BBCDC + rol(v, 5); w = rol(w, 30);
#define R4(v, w, x, y, z, i) z += (w ^ x ^ y) + blk(i) + 0xCA62C1D6 + rol(v, 5); w = rol(w, 30);


size_t
binary_to_hex(void *data, size_t dlen, char *hex, size_t hlen) {
    size_t n;
    char * dptr = hex;

    for (n = 0; (n < dlen) && ((n * 2) < hlen); n++, dptr += 2) {
        uint8_t c = ((uint8_t *)data)[n];
        if (hex != NULL) {
            dptr[0] = EVHTP_HEX_DIGITS[((c >> 4) & 0xF)];
            dptr[1] = EVHTP_HEX_DIGITS[(c & 0xF)];
        }
    }
    return n * 2;
}

static int8_t
hex_to_char(char hex) {
    uint8_t c = hex;

    if (c >= '0' && c <= '9') {
        c = c - 48;
    } else if (c >= 'A' && c <= 'F') {
        c = c - 55;  /* 65 - 10 */
    } else if (c >= 'a' && c <= 'f') {
        c = c - 87;  /* 97 - 10 */
    } else {
        return -1;
    }
    return c;
}

static size_t
hex_to_binary(char *hex, size_t hlen, void *data, size_t dlen) {
    size_t n;
    size_t y = 0;

    if (data == NULL) {
        dlen = -1;
    }

    for (n = 0, y = 0; n < hlen && *hex != '\0' && y < dlen; n++) {
        int8_t c;
        int8_t c2;
        int8_t cc = 0;

        if ((c = hex_to_char(*hex++)) == -1) {
            errno = EINVAL;
            return 0;
        }

        if ((c2 = hex_to_char(*hex++)) == -1) {
            return 0;
        } else {
            n++;
        }

        cc = (c << 4) | (c2 & 0xF);

        if (data != NULL) {
            ((char *)data)[y] = (char)cc;
        }
        y++;
    }
    return y;
}

/* Hash a single 512-bit block. This is the core of the algorithm. */
static void
_sha1_transform(uint32_t state[5], uint8_t buffer[64]) {
    uint32_t       a;
    uint32_t       b;
    uint32_t       c;
    uint32_t       d;
    uint32_t       e;
    CHAR64LONG16 * block;

    /* uint32_t      block[80]; */

    block = (CHAR64LONG16 *)buffer;

    a     = state[0];
    b     = state[1];
    c     = state[2];
    d     = state[3];
    e     = state[4];

    /* 4 rounds of 20 operations each. Loop unrolled. */
    R0(a, b, c, d, e, 0); R0(e, a, b, c, d, 1); R0(d, e, a, b, c, 2); R0(c, d, e, a, b, 3);
    R0(b, c, d, e, a, 4); R0(a, b, c, d, e, 5); R0(e, a, b, c, d, 6); R0(d, e, a, b, c, 7);
    R0(c, d, e, a, b, 8); R0(b, c, d, e, a, 9); R0(a, b, c, d, e, 10); R0(e, a, b, c, d, 11);
    R0(d, e, a, b, c, 12); R0(c, d, e, a, b, 13); R0(b, c, d, e, a, 14); R0(a, b, c, d, e, 15);
    R1(e, a, b, c, d, 16); R1(d, e, a, b, c, 17); R1(c, d, e, a, b, 18); R1(b, c, d, e, a, 19);
    R2(a, b, c, d, e, 20); R2(e, a, b, c, d, 21); R2(d, e, a, b, c, 22); R2(c, d, e, a, b, 23);
    R2(b, c, d, e, a, 24); R2(a, b, c, d, e, 25); R2(e, a, b, c, d, 26); R2(d, e, a, b, c, 27);
    R2(c, d, e, a, b, 28); R2(b, c, d, e, a, 29); R2(a, b, c, d, e, 30); R2(e, a, b, c, d, 31);
    R2(d, e, a, b, c, 32); R2(c, d, e, a, b, 33); R2(b, c, d, e, a, 34); R2(a, b, c, d, e, 35);
    R2(e, a, b, c, d, 36); R2(d, e, a, b, c, 37); R2(c, d, e, a, b, 38); R2(b, c, d, e, a, 39);
    R3(a, b, c, d, e, 40); R3(e, a, b, c, d, 41); R3(d, e, a, b, c, 42); R3(c, d, e, a, b, 43);
    R3(b, c, d, e, a, 44); R3(a, b, c, d, e, 45); R3(e, a, b, c, d, 46); R3(d, e, a, b, c, 47);
    R3(c, d, e, a, b, 48); R3(b, c, d, e, a, 49); R3(a, b, c, d, e, 50); R3(e, a, b, c, d, 51);
    R3(d, e, a, b, c, 52); R3(c, d, e, a, b, 53); R3(b, c, d, e, a, 54); R3(a, b, c, d, e, 55);
    R3(e, a, b, c, d, 56); R3(d, e, a, b, c, 57); R3(c, d, e, a, b, 58); R3(b, c, d, e, a, 59);
    R4(a, b, c, d, e, 60); R4(e, a, b, c, d, 61); R4(d, e, a, b, c, 62); R4(c, d, e, a, b, 63);
    R4(b, c, d, e, a, 64); R4(a, b, c, d, e, 65); R4(e, a, b, c, d, 66); R4(d, e, a, b, c, 67);
    R4(c, d, e, a, b, 68); R4(b, c, d, e, a, 69); R4(a, b, c, d, e, 70); R4(e, a, b, c, d, 71);
    R4(d, e, a, b, c, 72); R4(c, d, e, a, b, 73); R4(b, c, d, e, a, 74); R4(a, b, c, d, e, 75);
    R4(e, a, b, c, d, 76); R4(d, e, a, b, c, 77); R4(c, d, e, a, b, 78); R4(b, c, d, e, a, 79);

    /* Add the working vars back into context.state[] */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
} /* _sha1_transform */

void
sha1_init(sha1_ctx *ctx) {
    /* SHA1 initialization constants */
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count[0] = ctx->count[1] = 0;
}

void
sha1_update(sha1_ctx *ctx, uint8_t *data, size_t len) {
    uint32_t i;
    uint32_t j;
    uint32_t llen;

    /* FIXME: 64bit (or better unbounded) sha1_update */
    assert(len <= UINT_MAX - 1);
    llen = (uint32_t)len;

    j    = (ctx->count[0] >> 3) & 63;

    if ((ctx->count[0] += llen << 3) < (llen << 3)) {
        ctx->count[1]++;
    }

    ctx->count[1] += (llen >> 29);

    if ((j + llen) > 63) {
        memcpy(&ctx->buffer[j], data, (i = 64 - j));

        _sha1_transform(ctx->state, ctx->buffer);

        for (; i + 63 < llen; i += 64) {
            _sha1_transform(ctx->state, &data[i]);
        }

        j = 0;
    } else {
        i = 0;
    }

    memcpy(&ctx->buffer[j], &data[i], llen - i);
}

void
sha1_finalize(sha1_ctx *ctx, uint8_t digest[20]) {
    uint32_t i;
    uint8_t  finalcount[8];

    for (i = 0; i < 8; i++) {
        finalcount[i] = (uint8_t)((ctx->count[(i >= 4 ? 0 : 1)]
                                   >> ((3 - (i & 3)) * 8) ) & 255);  /* Endian independent */
    }

    sha1_update(ctx, (uint8_t *)"\200", 1);

    while ((ctx->count[0] & 504) != 448) {
        sha1_update(ctx, (uint8_t *)"\0", 1);
    }

    sha1_update(ctx, finalcount, 8);  /* Should cause a SHA1Transform() */
    for (i = 0; i < 20; i++) {
        digest[i] = (uint8_t)
                    ((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8) ) & 255);
    }

    memset(ctx->buffer, 0, 64);
    memset(ctx->state, 0, 20);
    memset(ctx->count, 0, 8);
    memset(&finalcount, 0, 8);
}

void
sha1_data(void *data, size_t len, uint8_t digest[20]) {
    sha1_ctx ctx;

    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_finalize(&ctx, digest);
}

char *
sha1_tostr(uint8_t digest[20], char sha1[41]) {
    binary_to_hex(digest, 20, sha1, 41);
    return sha1;
}

uint8_t *
str_tosha1(char sha1[41], uint8_t digest[20]) {
    hex_to_binary(sha1, 40, digest, 20);
    return digest;
}

