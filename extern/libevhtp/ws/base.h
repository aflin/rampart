#ifndef _EVHTP_BASE_H_
#define _EVHTP_BASE_H_

typedef enum {
    base_encode_with_padding     = 1,
    base_ignore_non_encoded_data = 2,
} base_flags;

typedef struct base_definition {
    const char * alphabet;
    const char   dictionary[256];
    size_t       base;                       /* base of the encoding (ie strlen of encoder) */
    uint8_t      igroups;                    /* number of 8 bits input groups to consume */
    uint8_t      ogroups;                    /* number of obits input groups to produce */
    uint8_t      obits;                      /* number of bits in an output group */
    char         pad;
    base_flags   flags;
} base_definition_t;

extern base_definition_t * base64_rfc;       /* rfc 3548/4648 definition */
extern base_definition_t * base64_rfc_nopad; /* rfc 3548/4648 definition, but no padding */
extern base_definition_t * base64_uri;       /* rfc 3548/4648 URI safe definition */

extern base_definition_t * base32_rfc;       /* rfc 3548/4648 definition */
extern base_definition_t * base32_hex;       /* rfc 4648 HEX definition */

extern base_definition_t * base16_rfc;       /* rfc 3548/4648 definition */
EVHTP_EXPORT int base_encode(base_definition_t *def, const void *in, size_t in_bytes, void **out, size_t *out_bytes);

#define base64_encode(in, in_bytes, out, out_bytes) \
    base_encode(base64_rfc, in, in_bytes, out, out_bytes)

#define base64_encode_nopad(in, in_bytes, out, out_bytes) \
    base_encode(base64_rfc_nopad, in, in_bytes, out, out_bytes)

#define base64_encode_uri(in, in_bytes, out, out_bytes) \
    base_encode(base64_uri, in, in_bytes, out, out_bytes)

#define base32_encode(in, in_bytes, out, out_bytes) \
    base_encode(base32_rfc, in, in_bytes, out, out_bytes)

#define base32_encode_hex(in, in_bytes, out, out_bytes) \
    base_encode(base32_hex, in, in_bytes, out, out_bytes)

#define base16_encode(in, in_bytes, out, out_bytes) \
    base_encode(base16_rfc, in, in_bytes, out, out_bytes)

EVHTP_EXPORT int base_decode(base_definition_t * def, const void * in, size_t in_bytes, void **out, size_t *out_bytes);

#define base64_decode(in, in_bytes, out, out_bytes) \
    base_decode(base64_rfc, in, in_bytes, out, out_bytes)

#define base64_decode_uri(in, in_bytes, out, out_bytes) \
    base_decode(base64_uri, in, in_bytes, out, out_bytes)

#define base32_decode(in, in_bytes, out, out_bytes) \
    base_decode(base32_rfc, in, in_bytes, out, out_bytes)

#define base32_decode_hex(in, in_bytes, out, out_bytes) \
    base_decode(base32_hex, in, in_bytes, out, out_bytes)

#define base16_decode(in, in_bytes, out, out_bytes) \
    base_decode(base16_rfc, in, in_bytes, out, out_bytes)


#endif /* _ZT_BASE_H_ */
