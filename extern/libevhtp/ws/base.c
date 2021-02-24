#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>

#include "../include/internal.h"
#include "base.h"

typedef uint64_t _bits_t;

static base_definition_t _base64_rfc = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/",
    /* valid: alphanum, '+', '/'; allows CR,LF,'=',SP,TAB
     * -1 (invalid) -2 (ignored)
     */
    { -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -2, -2, -1, -1, -2, -1, -1, /*  0-15 */
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 16-31 */
      -2,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, /* 32-47 */
      52,          53,  54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -2, -1, -1, /* 48-63 */
      -1,          0,   1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, /* 64-79 */
      15,          16,  17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, /* 80-95 */
      -1,          26,  27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, /* 96-111 */
      41,          42,  43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, /* 112-127 */
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,},
    64,
    3,
    4,
    6,
    '=',
    base_encode_with_padding
};

static base_definition_t _base64_rfc_nopad = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/",
    /* valid: alphanum, '+', '/'; allows CR,LF,'=',SP,TAB
     * -1 (invalid) -2 (ignored)
     */
    { -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -2, -2, -1, -1, -2, -1, -1, /*  0-15 */
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 16-31 */
      -2,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, /* 32-47 */
      52,          53,  54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -2, -1, -1, /* 48-63 */
      -1,          0,   1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, /* 64-79 */
      15,          16,  17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, /* 80-95 */
      -1,          26,  27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, /* 96-111 */
      41,          42,  43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, /* 112-127 */
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,},
    64,
    3,
    4,
    6,
    '=',
    0
};

static base_definition_t _base64_uri = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789-_",
    /* valid: alphanum, '-', '_'; ignores: CR,LF,'=',SP,TAB */
    { -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -2, -2, -1, -1, -2, -1, -1, /* 0-15 */
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 16-31 */
      -2,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, /* 32-47 */
      52,          53,  54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -2, -1, -1, /* 48-63 */
      -1,          0,   1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, /* 64-79 */
      15,          16,  17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63, /* 80-95 */
      -1,          26,  27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, /* 96-111 */
      41,          42,  43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, /* 112-127 */
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,          -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,},
    64,
    3,
    4,
    6,
    '=',
    base_encode_with_padding
};


base_definition_t      * base64_rfc       = &_base64_rfc;
base_definition_t      * base64_rfc_nopad = &_base64_rfc_nopad;
base_definition_t      * base64_uri       = &_base64_uri;

static base_definition_t _base32_rfc      = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "234567",
    { -1,    -1,  -1, -1, -1, -1, -1, -1, -1, -2, -2, -1, -1, -2, -1, -1, /* 0-15 */
      -1,    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 16-31 */
      -2,    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 32-47 */
      -1,    -1,  26, 27, 28, 29, 30, 31, -1, -1, -1, -1, -1, -2, -1, -1, /* 48-63 */
      -1,    0,   1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, /* 64-79 */
      15,    16,  17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, /* 80-95 */
      -1,    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 96-111 */
      -1,    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 112-127 */
      -1,    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,},
    32,
    5,       /* in */
    8,       /* out */
    5,       /* bits */
    '=',
    base_encode_with_padding
};

static base_definition_t _base32_hex = {
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUV",
    { -1,                    -1,  -1, -1, -1, -1, -1, -1, -1, -2, -2, -1, -1, -2, -1, -1, /* 0-15 */
      -1,                    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 16-31 */
      -2,                    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 32-47 */
      0,                     1,   2,  3,  4,  5,  6,  7,  8,  9,  -1, -1, -1, -2, -1, -1, /* 48-63 */
      -1,                    10,  11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, /* 64-79 */
      25,                    26,  27, 28, 29, 30, 31, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 80-95 */
      -1,                    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 96-111 */
      -1,                    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 112-127 */
      -1,                    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,                    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,                    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,                    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,                    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,                    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,                    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,                    -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,},
    32,
    5,
    8,
    5,
    '=',
    base_encode_with_padding
};

base_definition_t      * base32_rfc  = &_base32_rfc;
base_definition_t      * base32_hex  = &_base32_hex;

static base_definition_t _base16_rfc = {
    "0123456789ABCDEF",
    { -1,              -1,  -1, -1, -1, -1, -1, -1, -1, -2, -2, -1, -1, -2, -1, -1, /* 0-15 */
      -1,              -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 16-31 */
      -2,              -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 32-47 */
      0,               1,   2,  3,  4,  5,  6,  7,  8,  9,  -1, -1, -1, -2, -1, -1, /* 48-63 */
      -1,              10,  11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 64-79 */
      -1,              -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 80-95 */
      -1,              -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 96-111 */
      -1,              -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 112-127 */
      -1,              -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,              -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,              -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,              -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,              -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,              -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,              -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1,              -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,},
    16,
    1,
    2,
    4,
    '=',
    0
};

base_definition_t      * base16_rfc = &_base16_rfc;

static int
_valid_dictionary_p(base_definition_t * def) {
    if (!def) {
        return 0;
    }

    if ( 8 * def->igroups != def->ogroups * def->obits) {
        return 0;
    }

    /* can we overflow the bit container */
    if ( def->igroups > sizeof(_bits_t)) {
        return 0;
    }

    return 1;
}

static int
_encode64(base_definition_t * def, const unsigned char * in, const size_t lcnt, const size_t mod, unsigned char * out) {
    const char * alphabet = def->alphabet;
    const char   pad      = def->pad;
    size_t       i        = 0;

    for (i = 0; i < lcnt; i++) {
        const unsigned int b0 = *in++;
        const unsigned int b1 = *in++;
        const unsigned int b2 = *in++;

        /*       b0        |       b1        |       b2
         * 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0
         * |_________| |___________| |___________| |_________|
         */

        *out++ = alphabet[ b0 >> 2 ];
        *out++ = alphabet[ ((b0 & 0x03) << 4) | ((b1 & 0xF0) >> 4) ];
        *out++ = alphabet[ ((b1 & 0x0F) << 2) | ((b2 & 0xC0) >> 6) ];
        *out++ = alphabet[ b2 & 0x3F ];
    }

    if (mod > 0) {
        const unsigned int b0 = *in++;
        const unsigned int b1 = (mod > 1) ? *in++ : 0;
        const unsigned int b2 = 0;

        *out++ = alphabet[ b0 >> 2 ];
        *out++ = alphabet[ ((b0 & 0x03) << 4) | ((b1 & 0xF0) >> 4) ];

        if (mod > 1) {
            *out++ = alphabet[ ((b1 & 0x0F) << 2) | ((b2 & 0xC0) >> 6) ];
        }

        if (EVHTP_BIT_ISSET(def->flags, base_encode_with_padding)) {
            for (i = 0; i < (3 - mod); ++i) {
                *out++ = pad;
            }
        }
    }

    return 0;
}

#define BUFSIZE_64 4
static int
_decode64(base_definition_t * def, const unsigned char * in, const size_t bytes, unsigned char * out, size_t * out_bytes) {
    size_t       i;
    const char * dictionary = def->dictionary;
    unsigned int buf[BUFSIZE_64];
    size_t       cnt        = 0;
    size_t       obytes     = 0;
    int          result     = 0;

    for (i = 0; i < bytes; i++) {
        const int v = dictionary[*in++];

        if (v >= 0) {
            buf[cnt++] = v;

            if (cnt == BUFSIZE_64) {
                const unsigned int b0 = buf[0];
                const unsigned int b1 = buf[1];
                const unsigned int b2 = buf[2];
                const unsigned int b3 = buf[3];

                cnt     = 0;

                /*      b0     |      b1     |     b2      |     b4
                 * 7 6 5 4 3 2 | 1 0 7 6 5 4 | 3 2 1 0 7 6 | 5 4 3 2 1 0
                 * |_______________| |_______________| |_______________|
                 */
                *out++  = b0 << 2 | (b1 >> 4);
                *out++  = (b1 & 0x0F) << 4 | (b2 >> 2);
                *out++  = (b2 & 0x03) << 6 | b3;

                obytes += 3;
            }
        } else if (v == -1) {
            return -1;
        }
    }

    if (cnt > 0) {
        switch (cnt) {
            case 1:
                result = -1;
                break;
            case 2:
                *out++ = buf[0] << 2 | (buf[1] >> 4);
                break;
            case 3:
                *out++ = buf[0] << 2 | (buf[1] >> 4);
                *out++ = (buf[1] & 0x0F) << 4 | (buf[2] >> 2);
                break;
        }

        obytes += (cnt - 1);
    }

    *out_bytes = obytes;
    return result;
} /* _decode64 */

static int
_encode32(base_definition_t * def, const unsigned char * in, const size_t lcnt, const size_t mod, unsigned char * out) {
    const char * alphabet = def->alphabet;
    size_t       i        = 0;

    for (i = 0; i < lcnt; i++) {
        const unsigned int b0 = *in++;
        const unsigned int b1 = *in++;
        const unsigned int b2 = *in++;
        const unsigned int b3 = *in++;
        const unsigned int b4 = *in++;

        /*       b0        |       b1        |       b2        |       b3        |       b4
         * 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0
         * |_______| |_________| |_______| |_________| |_________| |_______| |_________| |_______|
         */

        *out++ = alphabet[ b0 >> 3 ]; /* 5 bits out */
        *out++ = alphabet[ ((b0 & 0x07) << 2) | ((b1 & 0xC0) >> 6) ];
        *out++ = alphabet[ ((b1 & 0x3F) >> 1) ];
        *out++ = alphabet[ ((b1 & 0x01) << 4) | ((b2 & 0xF0) >> 4) ];
        *out++ = alphabet[ ((b2 & 0x0F) << 1) | ((b3 & 0x80) >> 7) ];
        *out++ = alphabet[ ((b3 & 0x7C) >> 2) ];
        *out++ = alphabet[ ((b3 & 0x03) << 3) | ((b4 & 0xE0) >> 5) ];
        *out++ = alphabet[ b4 & 0x1F ];
    }

    if (mod > 0) {
        const unsigned int b0 = *in++;
        const unsigned int b1 = (mod > 1) ? *in++ : 0;
        const unsigned int b2 = (mod > 2) ? *in++ : 0;
        const unsigned int b3 = (mod > 3) ? *in++ : 0;
        const unsigned int b4 = 0;
        size_t             pad;

        *out++ = alphabet[ b0 >> 3 ]; /* 5 bits out */
        *out++ = alphabet[ ((b0 & 0x07) << 2) | ((b1 & 0xC0) >> 6) ];
        pad    = 6;

        if (mod > 1) {
            *out++ = alphabet[ ((b1 & 0x3F) >> 1) ];
            *out++ = alphabet[ ((b1 & 0x01) << 4) | ((b2 & 0xF0) >> 4) ];
            pad    = 4;
        }

        if (mod > 2) {
            *out++ = alphabet[ ((b2 & 0x0F) << 1) | ((b3 & 0x80) >> 7) ];
            pad    = 3;
        }

        if (mod > 3) {
            *out++ = alphabet[ ((b3 & 0x7C) >> 2) ];
            *out++ = alphabet[ ((b3 & 0x03) << 3) | ((b4 & 0xE0) >> 5) ];
            pad    = 1;
        }
        /* *out++ = alphabet[ b4 & 0x1F ]; */

        if (EVHTP_BIT_ISSET(def->flags, base_encode_with_padding)) {
            for (i = 0; i < pad; ++i) {
                *out++ = def->pad;
            }
        }
    }

    return 0;
} /* _encode32 */

#define BUFSIZE_32 8
static int
_decode32(base_definition_t * def, const unsigned char * in, const size_t bytes, unsigned char * out, size_t * out_bytes) {
    size_t       i;
    const char * dictionary = def->dictionary;
    unsigned int buf[BUFSIZE_32];
    size_t       cnt        = 0;
    size_t       obytes     = 0;
    int          result     = 0;

    for (i = 0; i < bytes; i++) {
        const int v = dictionary[*in++];

        if (v >= 0) {
            buf[cnt++] = v;

            if (cnt == BUFSIZE_32) {
                const unsigned int b0 = buf[0];
                const unsigned int b1 = buf[1];
                const unsigned int b2 = buf[2];
                const unsigned int b3 = buf[3];
                const unsigned int b4 = buf[4];
                const unsigned int b5 = buf[5];
                const unsigned int b6 = buf[6];
                const unsigned int b7 = buf[7];

                cnt     = 0;

                /*     b0    |    b1     |    b2     |    b3     |    b4     |    b5     |    b6     |    b7
                 * 7 6 5 4 3 | 2 1 0 7 6 | 5 4 3 2 1 | 0 7 6 5 4 | 3 2 1 0 7 | 6 5 4 3 2 | 1 0 7 6 5 | 4 3 2 1 0
                 * |_______________| |_________________| |_______________| |_________________| |_______________|
                 */

                *out++  = b0 << 3 | b1 >> 2;
                *out++  = b1 << 6 | b2 << 1 | b3 >> 4;
                *out++  = b3 << 4 | b4 >> 1;
                *out++  = b4 << 7 | b5 << 2 | b6 >> 3;
                *out++  = b6 << 5 | b7;

                obytes += 5;
            }
        } else if (v == -1) {
            return -1;
        }
    }

    if (cnt > 0) {
        switch (cnt) {
            case 1:
                result  = -1;
                break;
            case 2:
                *out++  = buf[0] << 3 | buf[1] >> 2;
                obytes += 1;
                break;
            case 3:
                result  = -1;
                break;
            case 4:
                *out++  = buf[0] << 3 | buf[1] >> 2;
                *out++  = buf[1] << 6 | buf[2] << 1 | buf[3] >> 4;
                obytes += 2;
                break;
            case 5:
                *out++  = buf[0] << 3 | buf[1] >> 2;
                *out++  = buf[1] << 6 | buf[2] << 1 | buf[3] >> 4;
                *out++  = buf[3] << 4 | buf[4] >> 1;
                obytes += 3;
                break;
            case 6:
                return -1;
                break;
            case 7:
                *out++  = buf[0] << 3 | buf[1] >> 2;
                *out++  = buf[1] << 6 | buf[2] << 1 | buf[3] >> 4;
                *out++  = buf[3] << 4 | buf[4] >> 1;
                *out++  = buf[4] << 7 | buf[5] << 2 | buf[6] >> 3;
                obytes += 4;
                break;
            default:
                result  = -1;
                break;
        } /* switch */
    }

    *out_bytes = obytes;
    return result;
}         /* _decode32 */

static int
_encode16(base_definition_t * def, const unsigned char * in, const size_t lcnt, const size_t mod, unsigned char * out) {
    const char * alphabet = def->alphabet;
    size_t       i        = 0;

    for (i = 0; i < lcnt; i++) {
        const unsigned int b0 = *in++;

        /*  IN: 1 * 8 bits (8 bits)
         * OUT: 8 * 4 bits (8 bits)
         */

        /*       b0
         * 7 6 5 4 3 2 1 0
         * |_____| |_____|
         */

        *out++ = alphabet[ b0 >> 4 ];
        *out++ = alphabet[ b0 & 0x0F ];
    }

    return 0;
}

#define BUFSIZE_16 2
static int
_decode16(base_definition_t * def, const unsigned char * in, const size_t bytes, unsigned char * out, size_t * out_bytes) {
    size_t       i;
    const char * dictionary = def->dictionary;
    unsigned int buf[BUFSIZE_16];
    size_t       cnt        = 0;
    size_t       obytes     = 0;
    int          result     = 0;

    for (i = 0; i < bytes; i++) {
        const int v = dictionary[*in++];

        if (v >= 0) {
            buf[cnt++] = v;

            if (cnt == BUFSIZE_16) {
                const unsigned int b0 = buf[0];
                const unsigned int b1 = buf[1];

                cnt     = 0;

                /*    b0   |   b1
                 * 7 6 5 4 | 3 2 1 0
                 * |_______________|
                 */
                *out++  = b0 << 4 | b1;

                obytes += 1;
            }
        } else if (v == -1) {
            return -1;
        }
    }

    *out_bytes = obytes;
    return result;
}

#define MAKE_MASK(x) ((1 << x) - 1)
int
base_encode(base_definition_t * def, const void * in, size_t in_bytes, void **out, size_t *out_bytes) {
    size_t                ocount;
    size_t                lcount;
    size_t                mod;
    size_t                olen;
    const unsigned char * inp  = in;
    unsigned char       * outp = out ? *(unsigned char **)out : NULL;

    if (!_valid_dictionary_p(def)) {
        return -1;
    }

    /* if we are missing out_bytes then we cannot continue */
    if (!out_bytes) {
        return -1;
    }

    /* if there is no input then we cannot continue */
    if (!in || !in_bytes) {
        *out_bytes = 0;
        return 0;
    }

    /* calculate the number of (N * 8-bit) input groups and remainder */
    lcount = in_bytes / def->igroups;                        /* number of whole input groups */
    mod    = ((8 * (in_bytes % def->igroups)) / def->obits); /* portion of partial output groups */
    ocount = (lcount * def->ogroups);                        /* number of whole output groups */

    if (mod > 0) {
        if EVHTP_BIT_ISSET(def->flags, base_encode_with_padding) {
            ocount += def->ogroups;
        } else {
            ocount += mod + 1;
        }
    }

    if (out && outp == NULL && *out_bytes == 0) {
        outp       = calloc(ocount, 1);
        *out       = (void *)outp;
        *out_bytes = ocount;
    }

    olen       = *out_bytes;
    *out_bytes = ocount;

    /* if there is not enough space in the output then fail */
    if (olen < ocount) {
        return -2;
    }

    /* if there is no output then return just the size */
    if (!out || !outp) {
        return 0;
    }

    /* perform encoding */
    switch (def->base) {
        case 64:
            return _encode64(def, inp, lcount, in_bytes % def->igroups, outp);
            break;
        case 32:
            return _encode32(def, inp, lcount, in_bytes % def->igroups, outp);
            break;
        case 16:
            return _encode16(def, inp, lcount, in_bytes % def->igroups, outp);
            break;
        default:
            break;
    }

    return -1;
} /* base_encode */

/*
 * decodes the encoded data from void * in and writes the output to
 * the void ** out pointer. If *out is NULL and *out_bytes is 0, *out is
 * calloc'd and must be free()'d by the user.
 */
int
base_decode(base_definition_t * def, const void * in, size_t in_bytes, void ** out, size_t * out_bytes) {
    const unsigned char * inp;
    unsigned char       * outp;
    size_t                lcount = 0;
    size_t                ocount = 0;
    size_t                mod    = 0;
    ssize_t               i;

    if (!_valid_dictionary_p(def)) {
        return -1;
    }

    if (!out_bytes) {
        return -1;
    }

    if (!in || !in_bytes) {
        *out_bytes = 0;
        return 0;
    }

    inp = (unsigned char *)in;

    /* strip any follow on pad chars */
    for (i = in_bytes - 1; i > 0; i--) {
        if (inp[i] != def->pad) {
            i = in_bytes - 1 - i;
            break;
        }
    }

    lcount = (in_bytes - i) / def->ogroups;   /* number of whole input groups */
    mod    = ((in_bytes - i) % def->ogroups); /* partial inputs */
    ocount = (lcount * def->igroups) + mod;   /* total output bytes */

    if (out == NULL) {
        *out_bytes = ocount;
        return 0;
    }

    if (*out != NULL && *out_bytes < ocount) {
        *out_bytes = ocount;
        return -2;
    }

    outp = *((unsigned char **)out);

    if ((outp == NULL && *out_bytes == 0)) {
        /* dynamically allocate the buffer */
        if (!(outp = calloc(ocount + 1, 1))) {
            return -1;
        }

        *out       = outp;
        *out_bytes = ocount;
    }

    switch (def->base) {
        case 64:
            return _decode64(def, inp, in_bytes, outp, out_bytes);
            break;
        case 32:
            return _decode32(def, inp, in_bytes, outp, out_bytes);
            break;
        case 16:
            return _decode16(def, inp, in_bytes, outp, out_bytes);
            break;
        default:
            break;
    }

    return -1;
} /* base_decode */

