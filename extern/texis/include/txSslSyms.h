#ifndef TXSSLSYMS_H
#define TXSSLSYMS_H

#ifdef HAVE_OPENSSL

#include <stdint.h>                             /* for uint64_t */

/* 0xMNNFFPPS: Major miNor Fix Patch Status */
#define TX_SSL_GET_MAJOR_MINOR_NUM(x)   ((x) >> 20)
#define TX_SSL_GET_MAJOR_NUM(x)         ((x) >> 28)
#define TX_SSL_GET_MINOR_NUM(x)         (((x) >> 20) & 0xff)
#define TX_SSL_GET_FIX_NUM(x)           (((x) >> 12) & 0xff)
#define TX_SSL_GET_PATCH_NUM(x)         (((x) >> 4) & 0xff)
#define TX_SSL_GET_STATUS_NUM(x)        ((x) & 0xf)

#if defined(OPENSSL_VERSION_NUMBER) && TX_SSL_GET_MAJOR_MINOR_NUM(OPENSSL_VERSION_NUMBER) < 0x100  /* < 1.0.x */
/* used in VER_FILEVERSION[_STR] in source/html/ssl.rc: */
#  define TX_SSL_WINDOWS_FILEVERSION_MAJOR 2
/* else we use OpenSSL's FILEVERSION */
#endif

/* Windows wincrypt.h defines X509_NAME as a pointer value: */
#undef X509_NAME

#ifndef EPI_STATIC_SSL_COMPILE_TEST
#  if !defined(HEADER_OPENSSL_TYPES_H) && !defined(OPENSSL_TYPES_H)  /* avoid include of SSL by everyone else */
typedef void EVP_MD_CTX;
typedef void EVP_MD;
typedef void ENGINE;
#    if TX_SSL_GET_MAJOR_MINOR_NUM(OPENSSL_VERSION_NUMBER) >= 0x100 /*>=1.0.x*/
typedef void SSL_CTX;
typedef void SSL;
#    endif /* >= 1.0.x */
#  endif /* ![HEADER_]OPENSSL_TYPES_H */
#  if !defined(HEADER_PEM_H) && !defined(OPENSSL_TYPES_H)
typedef void pem_password_cb;
#  endif
#  if !defined(HEADER_SSL_H) && !defined(OPENSSL_SSL_H)
typedef void SSL_METHOD;
#    if TX_SSL_GET_MAJOR_MINOR_NUM(OPENSSL_VERSION_NUMBER) < 0x100 /* <1.0.x*/
typedef void SSL_CTX;
typedef void SSL;
#    endif /* < 1.0.x */
#  endif /* !{HEADER,OPENSSL}_SSL_H */
#  ifndef HEADER_HMAC_H
typedef void HMAC_CTX;
#  endif /* !HEADER_HMAC_H */
#  ifndef HEADER_BIO_H
typedef void BIO;
typedef void BIO_METHOD;
#  endif /* !HEADER_BIO_H */
#  ifndef HEADER_STACK_H
typedef void OPENSSL_STACK;
#    define STACK_OF(type)    struct stack_st_##type
#  endif /* !HEADER_STACK_H */
#  ifndef HEADER_CRYPTO_H
typedef int CRYPTO_EX_new(void);
typedef int CRYPTO_EX_dup(void);
typedef int CRYPTO_EX_free(void);
#  endif /* !HEADER_CRYPTO_H */
#  if !defined(HEADER_ASN1_H) && !defined(OPENSSL_ASN1_H)
typedef int i2d_of_void(const void *, unsigned char **);
#  endif
#  if !defined(HEADER_OPENSSL_TYPES_H) && !defined(OPENSSL_TYPES_H)
typedef void ASN1_INTEGER;
typedef void X509;
typedef void X509_NAME;
typedef void X509_STORE;
typedef void X509_STORE_CTX;
typedef void EVP_PKEY;
typedef void EVP_CIPHER;
typedef void OPENSSL_INIT_SETTINGS;
#  endif /* ![HEADER_]OPENSSL_TYPES_H */
#  ifndef HEADER_X509_H
typedef void X509_EXTENSION;
#  endif /* !HEADER_X509_H */
#  if !defined(HEADER_OPENSSL_TYPES_H) && !defined(OPENSSL_TYPES_H)
typedef void BIGNUM;
#  endif /* ![HEADER_]OPENSSL_TYPES_H */
#  ifndef HEADER_X509V3_H
typedef void BASIC_CONSTRAINTS;
#  endif /* !HEADER_X509V3_H */
#  ifndef HEADER_PKCS7_H
typedef void PKCS7;
#  endif /* !HEADER_PKCS7_H */
#endif /* !EPI_STATIC_SSL_COMPILE_TEST */

#if TX_SSL_GET_MAJOR_MINOR_NUM(OPENSSL_VERSION_NUMBER) >= 0x100 /* >= 1.0.x */
/* Some prototypes changed in OpenSSL 1.0.x: */
#  define TX_SSL_10x_CONST      const
#  define TX_SSL_10x_SIZE_T     size_t
#  define TX_SSL_10x_SIZE_T_INT size_t
#  define TX_SSL_10x_SIZE_T_ULONG size_t
#  define TX_SSL_10x_INT        int
#  define TX_SSL_10x_VOID       void
#else /* < 1.0.x */
#  define TX_SSL_10x_CONST
#  define TX_SSL_10x_SIZE_T     unsigned int
#  define TX_SSL_10x_SIZE_T_INT int
#  define TX_SSL_10x_SIZE_T_ULONG unsigned long
#  define TX_SSL_10x_INT        void
#  define TX_SSL_10x_VOID       char
#endif /* < 1.0.x */
#if TX_SSL_GET_MAJOR_MINOR_NUM(OPENSSL_VERSION_NUMBER) >= 0x300 /* >= 3.0.x */
#  define TX_SSL_30x_CONST      const
#  define TX_SSL_OPTION_TYPE    uint64_t
#else /* < 3.0.x */
#  define TX_SSL_30x_CONST
#  define TX_SSL_OPTION_TYPE    unsigned long
#endif /* < 3.0.x */

/* use typedef to avoid `declared inside parameter list' warnings: */
typedef STACK_OF(X509)          TX_STACK_OF_X509;
typedef STACK_OF(X509_NAME)     TX_STACK_OF_X509_NAME;

/* These ..._LIST macros are used so that we can keep the function names,
 * strings, and prototypes in one place, so adding function(s) only involves
 * changes here.  Each place this macro is used defines I() appropriately:
 */
#define TXCRYPTOSYMBOLS_LIST                                            \
I(BIGNUM *,     ASN1_INTEGER_to_BN,                                     \
  (TX_SSL_10x_CONST ASN1_INTEGER *ai, BIGNUM *bn))                      \
I(void,         BASIC_CONSTRAINTS_free, (BASIC_CONSTRAINTS *bc))        \
I(long,         BIO_ctrl,         (BIO *b, int cmd, long larg, void *parg)) \
I(const BIO_METHOD *, BIO_f_base64,     (void))                         \
I(int,          BIO_free,               (BIO *a))                       \
I(BIO *,        BIO_new,                (const BIO_METHOD *type))       \
I(BIO *,        BIO_new_mem_buf,        (const void *buf, int len))     \
I(const BIO_METHOD *, BIO_s_file,       (void))                         \
I(const BIO_METHOD *, BIO_s_mem,        (void))                         \
I(char *,       BN_bn2dec,              (CONST BIGNUM *a))              \
I(void,         BN_free,                (BIGNUM *a))                    \
I(int,          CRYPTO_get_ex_new_index,   (int class_index, long argl, \
  void *argp, CRYPTO_EX_new *new_func, CRYPTO_EX_dup *dup_func,         \
                                            CRYPTO_EX_free *free_func)) \
I(EVP_PKEY *,   d2i_PrivateKey,         (int type, EVP_PKEY **a,        \
                          TX_SSL_10x_CONST unsigned char **pp, long length)) \
I(X509 *,d2i_X509, (X509 **a, TX_SSL_10x_CONST unsigned char **in, long len)) \
I(char *,       DES_fcrypt,             (const char *buf, const char *salt, \
                                         char *ret))                    \
I(int,          ENGINE_free,            (ENGINE *e))                    \
I(ENGINE *,     ENGINE_get_first,       (void))                         \
I(CONST char *, ENGINE_get_name,        (CONST ENGINE *e))              \
I(ENGINE *,     ENGINE_get_next,        (ENGINE *e))                    \
I(void,         ERR_clear_error,        (void))                         \
I(void,         ERR_error_string_n, (unsigned long e, char *buf, size_t len))\
I(unsigned long, ERR_get_error,         (void))                         \
I(unsigned long, ERR_peek_error,        (void))                         \
I(unsigned long, ERR_peek_last_error,   (void))                         \
I(int,          EVP_DigestFinal_ex,     (EVP_MD_CTX *, unsigned char *, \
                                         unsigned int *))               \
I(int,          EVP_DigestInit_ex, (EVP_MD_CTX *, CONST EVP_MD *, ENGINE *)) \
I(int,          EVP_DigestUpdate,       (EVP_MD_CTX *ctx, CONST void *d, \
                                         TX_SSL_10x_SIZE_T cnt))        \
I(CONST EVP_MD *, EVP_get_digestbyname, (CONST char *name))             \
I(CONST EVP_MD *, EVP_md5,              (void))                         \
I(EVP_MD_CTX *, EVP_MD_CTX_new,         (void))                         \
I(void,         EVP_MD_CTX_free,        (EVP_MD_CTX *ctx))              \
I(unsigned char *, HMAC, (CONST EVP_MD *evp_md, CONST void *key, int key_len,\
         CONST unsigned char *d, TX_SSL_10x_SIZE_T_INT n, unsigned char *md, \
                          unsigned int *md_len))                        \
I(void,         EVP_PKEY_free,          (EVP_PKEY *key))                \
I(const EVP_MD *, EVP_sha1,             (void))                         \
I(const EVP_MD *, EVP_sha256,           (void))                         \
I(const EVP_MD *, EVP_sha512,           (void))                         \
I(HMAC_CTX *,   HMAC_CTX_new,           (void))                         \
I(void,         HMAC_CTX_free,          (HMAC_CTX *ctx))                \
I(TX_SSL_10x_INT, HMAC_Final,           (HMAC_CTX *ctx, unsigned char *md, \
                                         unsigned int *len))            \
I(TX_SSL_10x_INT, HMAC_Init_ex, (HMAC_CTX *ctx, const void *key, int key_len,\
                               const EVP_MD *md, ENGINE *impl))         \
I(TX_SSL_10x_INT, HMAC_Update, (HMAC_CTX *ctx, const unsigned char *data, \
                                TX_SSL_10x_SIZE_T_INT len))                 \
I(int,          i2d_X509, (TX_SSL_30x_CONST X509 *x, unsigned char **out)) \
I(unsigned char *, MD4, (const unsigned char *d, TX_SSL_10x_SIZE_T_ULONG n, \
                         unsigned char *md))                            \
I(int,         OPENSSL_init_crypto,    (uint64_t opts,                  \
                                const OPENSSL_INIT_SETTINGS *settings)) \
I(TX_SSL_10x_VOID *,       PEM_ASN1_read_bio, (TX_SSL_10x_VOID *(*d2i)  \
             (void **a, TX_SSL_10x_CONST unsigned char **in, long len), \
                        CONST char *name, BIO *bp, TX_SSL_10x_VOID **x, \
                                    pem_password_cb *cb, void *u))      \
I(int,          PEM_ASN1_write_bio, (i2d_of_void *i2d,                  \
  CONST char *name, BIO *bp, TX_SSL_30x_CONST TX_SSL_10x_VOID *x,       \
  CONST EVP_CIPHER *enc, TX_SSL_30x_CONST unsigned char *kstr,          \
  int klen, pem_password_cb *cb, void *u))                              \
I(EVP_PKEY *,   PEM_read_bio_PrivateKey,                                \
  (BIO *bp, EVP_PKEY **x, pem_password_cb *cb, void *u))                \
I(X509 *,       PEM_read_bio_X509,                                      \
  (BIO *bp, X509 **x, pem_password_cb *cb, void *u))                    \
I(PKCS7 *,      PEM_read_bio_PKCS7, (BIO *bp, PKCS7 **x,                \
                                     pem_password_cb *cb, void *u))     \
I(void,         PKCS7_free,     (PKCS7 *p7))                            \
I(int,          PKCS7_verify,   (PKCS7 *p7, TX_STACK_OF_X509 *certs,    \
                 X509_STORE *store, BIO *indata, BIO *out, int flags))  \
I(void,         RAND_add,             (CONST void *buf, int num, double en)) \
I(int,          RAND_bytes,             (unsigned char *buf, int num))  \
I(int,          RAND_status,            (void))                         \
I(void *,       OPENSSL_sk_delete,      (OPENSSL_STACK *st, int loc))   \
I(OPENSSL_STACK *, OPENSSL_sk_dup,      (const OPENSSL_STACK *st))      \
I(void,         OPENSSL_sk_free,        (OPENSSL_STACK *st))            \
I(int,          OPENSSL_sk_insert, (OPENSSL_STACK *st,                  \
                                    const void *data, int loc))         \
I(OPENSSL_STACK *, OPENSSL_sk_new_null, (void))                         \
I(int,          OPENSSL_sk_num,         (const OPENSSL_STACK *st))      \
I(void,         OPENSSL_sk_pop_free,    (OPENSSL_STACK *st,             \
                                         void (*func)(void *)))         \
I(TX_SSL_10x_VOID *, OPENSSL_sk_set, (OPENSSL_STACK *st, int i,         \
                                      const void *value))               \
I(void *,       OPENSSL_sk_value,       (const OPENSSL_STACK *st, int i)) \
I(unsigned long, OpenSSL_version_num,   (void))                         \
I(void *,       X509V3_EXT_d2i,         (X509_EXTENSION *ext))          \
I(X509 *,       X509_dup,               (TX_SSL_30x_CONST X509 *a))     \
I(void,         X509_free,              (X509 *a))                      \
I(X509_EXTENSION *, X509_get_ext,       (const X509 *x, int loc))       \
I(int,          X509_get_ext_by_NID,  (const X509 *x, int nid, int lastpos)) \
I(X509_NAME *,  X509_get_issuer_name,   (const X509 *a))                \
I(X509_NAME *,  X509_get_subject_name,  (const X509 *a))                \
I(X509_NAME *,  X509_NAME_dup,          (TX_SSL_30x_CONST X509_NAME *xn)) \
I(void,         X509_NAME_free,         (X509_NAME *xn))                \
I(int,          X509_NAME_get_text_by_NID, (TX_SSL_30x_CONST X509_NAME *name,\
                                            int nid, char *buf, int len)) \
I(int,          X509_NAME_print_ex,     (BIO *out, const X509_NAME *nm, \
                                         int indent, unsigned long flags)) \
I(X509 *,       X509_STORE_CTX_get_current_cert,                        \
  (TX_SSL_30x_CONST X509_STORE_CTX *ctx))                               \
I(int, X509_STORE_CTX_get_error,(TX_SSL_30x_CONST X509_STORE_CTX *ctx)) \
I(int, X509_STORE_CTX_get_error_depth,(TX_SSL_30x_CONST X509_STORE_CTX *ctx))\
I(void *,       X509_STORE_CTX_get_ex_data,                             \
  (TX_SSL_30x_CONST X509_STORE_CTX *ctx, int idx))                      \
I(void,         X509_STORE_CTX_set_error, (X509_STORE_CTX *ctx, int s)) \
I(int,          X509_STORE_add_cert,    (X509_STORE *ctx, X509 *x))     \
I(void,         X509_STORE_free,        (X509_STORE *vfy))              \
I(X509_STORE *, X509_STORE_new,         (void))                         \
I(CONST char *, X509_verify_cert_error_string, (long n))                \
I(int,          X509_print_ex,          (BIO *bp, X509 *x,              \
                                 unsigned long nmflag, unsigned long cflag))

#define TXSSLSYMBOLS_LIST                                               \
I(STACK_OF(X509_NAME) *,   SSL_load_client_CA_file, (CONST char *file)) \
I(int,          OPENSSL_init_ssl, (uint64_t opts,                       \
                                   const OPENSSL_INIT_SETTINGS *settings)) \
I(TX_SSL_10x_CONST SSL_METHOD *, TLS_client_method,     (void))         \
I(TX_SSL_10x_CONST SSL_METHOD *, TLS_server_method,     (void))         \
I(long,         SSL_ctrl,   (SSL *ssl, int cmd, long larg, void *parg)) \
I(TX_SSL_OPTION_TYPE, SSL_clear_options,(SSL *ssl,TX_SSL_OPTION_TYPE options))\
I(TX_SSL_OPTION_TYPE, SSL_set_options, (SSL *ssl, TX_SSL_OPTION_TYPE options))\
I(STACK_OF(X509_NAME) *, SSL_get_client_CA_list, (TX_SSL_10x_CONST SSL *ssl))\
I(void *,       SSL_get_ex_data,        (TX_SSL_10x_CONST SSL *ssl, int idx))\
I(int,          SSL_get_ex_data_X509_STORE_CTX_idx, (void))             \
I(X509 *,       SSL_get1_peer_certificate, (TX_SSL_10x_CONST SSL *ssl)) \
I(long,         SSL_get_verify_result,  (TX_SSL_10x_CONST SSL *ssl))    \
I(CONST char *, SSL_get_version,        (TX_SSL_10x_CONST SSL *s))      \
I(int,          SSL_set_ex_data,        (SSL *ssl, int idx, void *arg)) \
I(int,          SSL_CTX_use_certificate,(SSL_CTX *ctx, X509 *x))        \
I(int, SSL_CTX_use_certificate_chain_file, (SSL_CTX *ctx, CONST char *file)) \
I(int, SSL_CTX_use_certificate_file, (SSL_CTX*ctx,CONST char *file,int type))\
I(int,          SSL_CTX_use_PrivateKey, (SSL_CTX *ctx, EVP_PKEY *pkey)) \
I(int, SSL_CTX_use_PrivateKey_file, (SSL_CTX*ctx, CONST char*file, int type))\
I(long,         SSL_CTX_ctrl, (SSL_CTX *ctx, int cmd, long larg, void *parg))\
I(int,          SSL_CTX_check_private_key, (TX_SSL_10x_CONST SSL_CTX *ctx)) \
I(int,          SSL_CTX_load_verify_locations, (SSL_CTX *ctx,           \
                           CONST char *CAfile, CONST char *CApath))     \
I(void,         SSL_CTX_set_cert_store, (SSL_CTX *ctx, X509_STORE *store)) \
I(int,          SSL_CTX_set_cipher_list, (SSL_CTX *ctx, const char *s)) \
I(int,          SSL_CTX_set_ciphersuites, (SSL_CTX *ctx, const char *s)) \
I(void, SSL_CTX_set_client_CA_list,(SSL_CTX *ctx, STACK_OF(X509_NAME) *list))\
I(void, SSL_CTX_set_default_passwd_cb, (SSL_CTX *ctx, pem_password_cb *cb)) \
I(void, SSL_CTX_set_default_passwd_cb_userdata, (SSL_CTX *ctx, void *u)) \
I(void,         SSL_CTX_set_verify,     (SSL_CTX *ctx, int mode,        \
                        int (*verify_callback)(int, X509_STORE_CTX *))) \
I(SSL_CTX *,    SSL_CTX_new,           (TX_SSL_10x_CONST SSL_METHOD *method))\
I(void,         SSL_CTX_free,           (SSL_CTX *ctx))                 \
I(SSL *,        SSL_new,                (SSL_CTX *ctx))                 \
I(void,         SSL_free,               (SSL *ssl))                     \
I(int,          SSL_set_fd,             (SSL *ssl, int fd))             \
I(void,         SSL_set_verify,         (SSL *ssl, int mode,            \
                        int (*verify_callback)(int, X509_STORE_CTX *))) \
I(void,         SSL_set_verify_depth,   (SSL *ssl, int depth))          \
I(int,          SSL_connect,            (SSL *ssl))                     \
I(int,          SSL_accept,             (SSL *ssl))                     \
I(int,          SSL_write,              (SSL *ssl, CONST void *buf, int num))\
I(int,          SSL_read,               (SSL *ssl, void *buf, int num)) \
I(int,          SSL_shutdown,           (SSL *ssl))                     \
I(int,          SSL_get_error,          (TX_SSL_10x_CONST SSL *ssl, int ret))

typedef struct TXCRYPTOSYMBOLS_tag
{
#undef I
#define I(ret, func, args)      ret (CDECL *func) ARGS(args);
TXCRYPTOSYMBOLS_LIST
#undef I
}
TXCRYPTOSYMBOLS;
#define TXCRYPTOSYMBOLSPN       ((TXCRYPTOSYMBOLS *)NULL)

extern TXCRYPTOSYMBOLS  TXcryptoSymbols;

typedef struct TXSSLSYMBOLS_tag
{
#undef I
#define I(ret, func, args)      ret (CDECL *func) ARGS(args);
TXSSLSYMBOLS_LIST
#undef I
}
TXSSLSYMBOLS;
#define TXSSLSYMBOLSPN  ((TXSSLSYMBOLS *)NULL)

extern TXSSLSYMBOLS     TXsslSymbols;
extern int              TXcheckSslVersion;
extern int              TXsslHtsktExDataIndex;

int  TXsslLoadLibrary(TXPMBUF *pmbuf);


#define TXSSL_DES_FCRYPT_RET_BUF_SZ     14

#endif /* HAVE_OPENSSL */

#endif /* !TXSSLSYMS_H */
