/* Copyright (C) 2021 Aaron Flin - All Rights Reserved
   Copyright (C) 2021 Benjamin Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/asn1t.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "rampart.h"



#define OPENSSL_ERR_STRING_MAX_SIZE 1024
#define DUK_OPENSSL_ERROR(ctx)                                                     \
    {                                                                              \
        void *err_buf = duk_push_fixed_buffer(ctx, OPENSSL_ERR_STRING_MAX_SIZE);   \
        ERR_error_string_n(ERR_get_error(), err_buf, OPENSSL_ERR_STRING_MAX_SIZE); \
        (void)duk_error(ctx, DUK_ERR_ERROR, "OpenSSL Error (%d): %s", __LINE__,err_buf);        \
    }
/* make sure when we use RAND_ functions, we've seeded at least once */
static int seeded=0;
void checkseed(duk_context *ctx)
{
    if(!seeded)
    {
        int rc = RAND_load_file("/dev/urandom", 32);
        if (rc != 32)
            DUK_OPENSSL_ERROR(ctx);
        seeded=1;
    }
}

static void rpcrypt(
  duk_context *ctx,
  unsigned char *key,
  unsigned char *iv,
  const char *cipher_name,
  void *in_buffer,
  duk_size_t in_len,
  unsigned char *salt,
  int decrypt
  )
{
    EVP_CIPHER_CTX *cipher_ctx;
    static const char magic[] = "Salted__";
    int out_len=0, current_len, m_len=strlen(magic);
    void *out_buffer;
    const EVP_CIPHER *cipher;
    int saltspace=0;

    if(!decrypt && salt)
        saltspace=PKCS5_SALT_LEN+m_len;

    /* Create and initialise the context */
    if (!(cipher_ctx = EVP_CIPHER_CTX_new()))
        DUK_OPENSSL_ERROR(ctx);

    /* Retrieve the cipher by name */
    cipher = EVP_get_cipherbyname(cipher_name);
    if (cipher == NULL)
        RP_THROW(ctx, "Cipher %s not found", cipher_name);

    out_buffer = duk_push_dynamic_buffer(ctx, in_len + EVP_CIPHER_block_size(cipher) + saltspace);

    if (decrypt)
    {
        /* Initialize the decryption operation with the found cipher, key and iv */
        if (!EVP_DecryptInit_ex(cipher_ctx, cipher, NULL, key, iv))
            DUK_OPENSSL_ERROR(ctx);

        if (!EVP_DecryptUpdate(cipher_ctx, out_buffer, &current_len, in_buffer, in_len))
                DUK_OPENSSL_ERROR(ctx);
        out_len += current_len;

        /*
         * Finalise the decryption. Further ciphertext bytes may be written at
         * this stage.
         */

        if (!EVP_DecryptFinal_ex(cipher_ctx, out_buffer + out_len, &current_len))
            DUK_OPENSSL_ERROR(ctx);
    }
    else
    {
        /* Initialize the encryption operation with the found cipher, key and iv */
        if (!EVP_EncryptInit_ex(cipher_ctx, cipher, NULL, key, iv))
            DUK_OPENSSL_ERROR(ctx);

        /* with password, we need to write magic and the salt necessary to recreate key,iv */
        if(saltspace)
        {
            memcpy(out_buffer,magic,m_len);
            memcpy(out_buffer+m_len,salt,PKCS5_SALT_LEN);
            out_len=saltspace;
        }

        if (!EVP_EncryptUpdate(cipher_ctx, out_buffer + out_len, &current_len, in_buffer, in_len))
            DUK_OPENSSL_ERROR(ctx);
        out_len += current_len;

        /*
         * Finalise the encryption. Further ciphertext bytes may be written at
         * this stage.
         */
        if (!EVP_EncryptFinal_ex(cipher_ctx, out_buffer + out_len, &current_len))
            DUK_OPENSSL_ERROR(ctx);
    }

    out_len += current_len;

    /* Resize the buffer to the actual output length */
    duk_resize_buffer(ctx, -1, out_len);

    /* Clean up */
    EVP_CIPHER_CTX_free(cipher_ctx);
}

#define KEYIV struct keyiv

KEYIV {
    unsigned char key[EVP_MAX_KEY_LENGTH];
    unsigned char iv[EVP_MAX_IV_LENGTH];
    unsigned char salt[PKCS5_SALT_LEN];
};

void printkiv(unsigned char *key,unsigned char *iv,unsigned char *salt,const EVP_CIPHER *cipher){
  int i;

  printf("key=");
  for (i = 0; i < EVP_CIPHER_key_length(cipher); i++)
    printf("%02X", key[i]);
  printf("\n");
  printf("iv =");
  for (i = 0; i < EVP_CIPHER_iv_length(cipher); i++)
    printf("%02X", iv[i]);
  printf("\n");
  printf("salt=");
  if(salt)
  {
      for (i = 0; i < PKCS5_SALT_LEN; i++)
          printf("%02X", salt[i]);
      printf("\n");
  }
  else printf("NULL\n");
}


static KEYIV pw_to_keyiv(duk_context *ctx, const char *pass, const char *cipher_name, unsigned char *salt_p, int iter)
{
    unsigned char salt[PKCS5_SALT_LEN];
    unsigned char keyiv[EVP_MAX_KEY_LENGTH + EVP_MAX_IV_LENGTH];
    KEYIV kiv;
    int klen,ivlen;
    const EVP_CIPHER *cipher=EVP_get_cipherbyname(cipher_name);

    if(!cipher)
        RP_THROW(ctx, "Cipher %s not found", cipher_name);


    klen = EVP_CIPHER_key_length(cipher);
    ivlen = EVP_CIPHER_iv_length(cipher);

    if(!salt_p)
    {
        checkseed(ctx);
        if (RAND_bytes(salt, sizeof(salt)) <= 0)
            DUK_OPENSSL_ERROR(ctx)
        salt_p=salt;
    }

    if (!PKCS5_PBKDF2_HMAC(pass, strlen(pass), salt_p, sizeof(salt), iter, EVP_sha256(), klen+ivlen, keyiv))
        DUK_OPENSSL_ERROR(ctx)

    memcpy(kiv.key, keyiv,          klen);
    memcpy(kiv.iv,  keyiv+klen, ivlen);
    memcpy(kiv.salt,salt_p, sizeof(salt));

    return kiv;
}



static duk_ret_t duk_rp_crypt(duk_context *ctx, int decrypt)
{
    duk_size_t in_len=0;
    void *in_buffer=NULL;
    const char *cipher_name = "aes-256-cbc";
    unsigned char *key=NULL, *iv=NULL, salt[PKCS5_SALT_LEN], *salt_p=NULL;
    KEYIV kiv;
    int iter=10000;
    static const char magic[] = "Salted__";

    if(duk_is_object(ctx,0))
    {
        /* Get options */
        if(duk_get_prop_string(ctx, 0, "cipher") )
        {
            cipher_name = duk_get_string(ctx, -1);
        }
        duk_pop(ctx);

        if(!duk_get_prop_string(ctx, 0, "data"))
            (void)duk_error(ctx, DUK_ERR_ERROR, "option 'data' missing from en/decrypt");

        duk_to_buffer(ctx,-1,&in_len);
        in_buffer = duk_get_buffer_data(ctx, -1, &in_len);
        /* don't pop */

        if(decrypt)
        {
            /* check for magic and salt, skip past*/
            size_t m_len=strlen(magic);
            if( in_len>m_len && !memcmp(in_buffer,magic,m_len) )
            {
                in_buffer+=m_len;
                in_len-=m_len;
                memcpy(salt,in_buffer,PKCS5_SALT_LEN);
                in_buffer+=PKCS5_SALT_LEN;
                in_len-=PKCS5_SALT_LEN;
                salt_p=salt;
            }
        }

        if(duk_get_prop_string(ctx, 0, "pass"))
        {
            const char *pass=duk_require_string(ctx, -1);

            duk_pop(ctx);
            if(!salt_p && decrypt)
                (void)duk_error(ctx, DUK_ERR_ERROR, "decrypt: ciphertext was not encrypted with a password, use key and iv to decrypt");

            if(duk_get_prop_string(ctx, 0, "iter"))
            {
                iter=(int)duk_require_number(ctx,-1);
                duk_pop(ctx);
            }

            kiv=pw_to_keyiv(ctx,pass,cipher_name,salt_p,iter); /* encrypting: salt_p is null, decrypting: must be set */
            key=kiv.key;
            iv=kiv.iv;
            salt_p=kiv.salt;
            duk_pop(ctx);
        }
        else
        {
            if (duk_get_prop_string(ctx, 0, "key"))
                key = (unsigned char *)duk_get_string(ctx, -1);
            duk_pop(ctx);

            if (duk_get_prop_string(ctx, 0, "iv"))
                iv = (unsigned char *)duk_get_string(ctx, -1);
            duk_pop(ctx);
        }
    }
    else if(duk_is_string(ctx,0))
    {
        const char *pass;

        if(!duk_is_string(ctx,0))
            (void)duk_error(ctx, DUK_ERR_ERROR, "first argument must be a password or an object with options");

        pass=duk_get_string(ctx,0);

        if( !duk_is_string(ctx,1) && !duk_is_buffer_data(ctx,1))
            (void)duk_error(ctx, DUK_ERR_ERROR, "second argument must be data to en/decrypt (string or buffer)");

        if( duk_is_string(ctx, 2))
            cipher_name=duk_get_string(ctx, 2);

        duk_to_buffer(ctx,1,&in_len);
        in_buffer = duk_get_buffer_data(ctx, 1, &in_len);

        if(decrypt)
        {
            /* check for magic and salt, skip past*/
            size_t m_len=strlen(magic);
            if( in_len>m_len && !memcmp(in_buffer,magic,m_len) )
            {
                in_buffer+=m_len;
                in_len-=m_len;
                memcpy(salt,in_buffer,PKCS5_SALT_LEN);
                in_buffer+=PKCS5_SALT_LEN;
                in_len-=PKCS5_SALT_LEN;
                salt_p=salt;
            }

            if(!salt_p)
                (void)duk_error(ctx, DUK_ERR_ERROR, "decrypt: ciphertext was not encrypted with a password, use key and iv to decrypt");
        }

        kiv=pw_to_keyiv(ctx,pass,cipher_name,salt_p,iter);
        key=kiv.key;
        iv=kiv.iv;
        salt_p=kiv.salt;
    }
    //printkiv(key,iv,salt_p,EVP_get_cipherbyname(cipher_name));

    if(!key || !iv)
        RP_THROW(ctx, "en/decrypt: error- either a password or a key/iv pair must be provided");

    rpcrypt ( ctx, key, iv, cipher_name, in_buffer, in_len, salt_p, decrypt);

    return 1;
}

/**
 * Does encryption given a cipher, buffer, key, and iv
 * @typedef {Object} EncryptOptions
 * @property {String} pass - the password to generate key and iv
 * @property {Number} iter - the number of iterations for hashing password (default 10000)
 * @property {String} key - the secret key to be used if no pass
 * @property {String} iv - the initialization vector/nonce if no pass
 * @property {String} cipher - The openssl name for the encryption/decryption scheme
 * @property {BufferData|String} data - the data to be encrypted
 * @returns {Buffer} the encrypted buffer
 */

/* also does encrypt("password","string"|buffer) */

static duk_ret_t duk_encrypt(duk_context *ctx)
{
    return duk_rp_crypt(ctx,0);
}
/**
 * Does decryption given a cipher, buffer, key, and iv
 * @typedef {Object} DecryptOptions
 * @property {String} pass - the password to generate key and iv
 * @property {Number} iter - the number of iterations for hashing password (default 10000)
 * @property {String} key - the secret key to be used if no pass
 * @property {String} iv - the initialization vector/nonce if no pass
 * @property {String} cipher - The openssl name for the encryption/decryption scheme
 * @property {BufferData} buffer - the data to be decrypted
 * @param {DecryptOptions} Options
 * @returns {Buffer} the decrypted buffer
 */

/* also does decrypt("password","string"|buffer) */

static duk_ret_t duk_decrypt(duk_context *ctx)
{
    return duk_rp_crypt(ctx,1);
}

static duk_ret_t duk_hmac(duk_context *ctx)
{
    duk_size_t keysz, datasz;
    void *key= REQUIRE_STR_TO_BUF(ctx, 0, &keysz, "crypto.hmac - arg 0 (key) requires a string or buffer");
    void *data= REQUIRE_STR_TO_BUF(ctx, 1, &datasz, "crypto.hmac - arg 1 (data) requires a string or buffer");
    const EVP_MD *md=EVP_get_digestbyname("sha256");
    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len;

    if(!duk_is_undefined(ctx, -2)){
        const char *digestfunc=NULL;
        digestfunc=REQUIRE_STRING(ctx, 2, "crypto.hmac - arg 3 (\"digest function\") requires a string");
        md=EVP_get_digestbyname(digestfunc);
        if(md==NULL)
            RP_THROW(ctx, "crypto.hmac - arg 3 (\"digest function\") \"%s\" invalid", digestfunc);
    }

    if(! HMAC(md, key, (int)keysz, data, (int)datasz, md_value, &md_len) )
        DUK_OPENSSL_ERROR(ctx);

    void *out = duk_push_fixed_buffer(ctx, (duk_size_t)md_len);
    memcpy(out, md_value, (size_t)md_len );

    if(!duk_is_boolean(ctx,3)||!duk_get_boolean(ctx,3))
        duk_rp_toHex(ctx,-1,0);

    return 1;
}


/**
 * Macro to make a duktape SHA hash function from a given digest size
 * and context size
 * @param {BufferData} the input buffer
 * @returns {Buffer} the message digest
 *
#define DUK_SHA_FUNC(ctx_size, md_size)                                       \
    static duk_ret_t duk_sha##md_size(duk_context *ctx)                       \
    {                                                                         \
        duk_size_t in_len;                                                    \
        void *in;                                                             \
                                                                              \
        in = REQUIRE_STR_TO_BUF(ctx, 0, &in_len,                                     \
          "crypto hash function requires a string or buffer as the first argument"); \
        SHA##ctx_size##_CTX sha_ctx;                                          \
                                                                              \
        if (!SHA##md_size##_Init(&sha_ctx))                                   \
            DUK_OPENSSL_ERROR(ctx);                                           \
                                                                              \
        if (!SHA##md_size##_Update(&sha_ctx, in, in_len))                     \
            DUK_OPENSSL_ERROR(ctx);                                           \
                                                                              \
        void *out = duk_push_fixed_buffer(ctx, SHA##md_size##_DIGEST_LENGTH); \
        if (!SHA##md_size##_Final(out, &sha_ctx))                             \
            DUK_OPENSSL_ERROR(ctx);                                           \
                                                                              \
        if(!duk_is_boolean(ctx,1)||!duk_get_boolean(ctx,1))                   \
            duk_rp_toHex(ctx,2,0);                                            \
        return 1;                                                             \
    }
* declare all supported ctx_size, md_size *
DUK_SHA_FUNC(256, 224);
DUK_SHA_FUNC(256, 256);
DUK_SHA_FUNC(512, 384);
DUK_SHA_FUNC(512, 512);

**
 * MD5 Hash function binding
 * @param {BufferData} the input buffer
 * @returns {Buffer} the message digest
 *
static duk_ret_t duk_md5(duk_context *ctx)
{
    duk_size_t in_len;
    void *in;
    MD5_CTX md5_ctx;
    duk_size_t sz;

    if(duk_is_string(ctx, 0)) duk_to_buffer(ctx, 0, &sz);
    in = duk_require_buffer_data(ctx, 0, &in_len);

    if (!MD5_Init(&md5_ctx))
        DUK_OPENSSL_ERROR(ctx);

    if (!MD5_Update(&md5_ctx, in, in_len))
        DUK_OPENSSL_ERROR(ctx);

    void *out = duk_push_fixed_buffer(ctx, MD5_DIGEST_LENGTH);
    if (!MD5_Final(out, &md5_ctx))
        DUK_OPENSSL_ERROR(ctx);

    if(!duk_is_boolean(ctx,1)||!duk_get_boolean(ctx,1))
        duk_rp_toHex(ctx,2,0);

    return 1;
}
*/

/* one function to rule them all and with options bind them */

static duk_ret_t duk_hash(duk_context *ctx)
{
    EVP_MD_CTX *mdctx;
    const EVP_MD *md;
    unsigned char *md_value;
    unsigned int md_len;
    const char *algo = "sha256";
    void *in;
    duk_size_t in_len;
    duk_idx_t bool_idx=2;

    in=REQUIRE_STR_TO_BUF(ctx, 0, &in_len,
          "crypto hash function requires a string or buffer as the first argument");

    if(duk_is_string(ctx, 1))
        algo = duk_get_string(ctx, 1);
    else
        bool_idx=1;

    md = EVP_get_digestbyname(algo);

    if (md == NULL)
        RP_THROW(ctx, "crypto.hash - \"%s\" is not a valid hash function\n", algo);

    md_value = duk_push_dynamic_buffer(ctx, EVP_MAX_MD_SIZE);

    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, in, (size_t)in_len);
    EVP_DigestFinal_ex(mdctx, md_value, &md_len);
    EVP_MD_CTX_free(mdctx);

    duk_resize_buffer(ctx, -1, (duk_size_t) md_len);

    if(!duk_is_boolean(ctx, bool_idx)||!duk_get_boolean(ctx, bool_idx))
        duk_rp_toHex(ctx,-1,0);

    return 1;
}

#define DUK_SHA_FUNC(md_type,md_name)               \
static duk_ret_t duk_##md_type(duk_context *ctx)    \
{                                                   \
    duk_push_string(ctx, md_name);                  \
    duk_insert(ctx, 1);                             \
    return duk_hash(ctx);                           \
}

DUK_SHA_FUNC(sha1, "sha1")
DUK_SHA_FUNC(sha224, "sha224")
DUK_SHA_FUNC(sha256, "sha256")
DUK_SHA_FUNC(sha384, "sha384")
DUK_SHA_FUNC(sha512, "sha512")
DUK_SHA_FUNC(sha3_224, "sha3-224")
DUK_SHA_FUNC(sha3_256, "sha3-256")
DUK_SHA_FUNC(sha3_384, "sha3-384")
DUK_SHA_FUNC(sha3_512, "sha3-512")
DUK_SHA_FUNC(md4, "md4")
DUK_SHA_FUNC(md5, "md5")
DUK_SHA_FUNC(blake2b512,"blake2b512")
DUK_SHA_FUNC(blake2s256,"blake2s256")
DUK_SHA_FUNC(mdc2,"mdc2")
DUK_SHA_FUNC(rmd160,"rmd160")
DUK_SHA_FUNC(sha512_224,"sha512-224")
DUK_SHA_FUNC(sha512_256,"sha512-256")
DUK_SHA_FUNC(shake128,"shake128")
DUK_SHA_FUNC(shake256,"shake256")
DUK_SHA_FUNC(sm3,"sm3")

/**
 * Uses RAND_bytes to fill a buffer with random data.
 * @param {uint} the output length of the buffer to be returned
 * @returns {Buffer} the buffer filled with random data
 */
static duk_ret_t duk_rand(duk_context *ctx)
{
    duk_size_t len = REQUIRE_UINT(ctx, -1, "crypto.rand requires a positive integer");
    void *buffer = duk_push_fixed_buffer(ctx, len);
    /* RAND_bytes may return 0 or -1 on error */
    checkseed(ctx);
    if (RAND_bytes(buffer, len) != 1)
        DUK_OPENSSL_ERROR(ctx);
    return 1;
}

static duk_ret_t duk_randnum(duk_context *ctx)
{
    uint64_t randint=0;
    double ret=0;
    /* RAND_bytes may return 0 or -1 on error */
    checkseed(ctx);
    if (RAND_bytes((unsigned char *)&randint, sizeof(uint64_t)) != 1)
        DUK_OPENSSL_ERROR(ctx);

    ret = (double)randint/(double)UINT64_MAX;
    duk_push_number(ctx, ret);
    return 1;
}

/* rand between -1.0 and 1.0 */
#define rrand(ctx) ({\
    uint64_t randint=0;\
    checkseed(ctx);\
    if (RAND_bytes((unsigned char *)&randint, sizeof(uint64_t)) != 1)\
        DUK_OPENSSL_ERROR(ctx);\
    ( -1.0 + (2.0 * (double)randint/(double)UINT64_MAX) );\
})

static double gaussrand(duk_context *ctx, double sigma)
{
	double x, y, r2;
   do
   {
		/* choose x,y in uniform square (-1,-1) to (+1,+1) */
      x=rrand(ctx);
      y=rrand(ctx);
		/* see if it is in the unit circle */
		r2 = x * x + y * y;
   } while (r2 > 1.0 || r2 == 0);

   /* Box-Muller transform */
   return ((sigma * y * sqrtf (-2.0 * logf (r2) / r2)));
}

static double normrand(duk_context *ctx, double scale)
{
   double t;
   t=gaussrand(ctx, 1.0)/5.0;
   if(t>1.0)       t=1.0;  // truncate for scaling
   else if(t<-1.0) t=-1.0;
   t*=scale;
   return(t);
}

static duk_ret_t duk_gaussrand(duk_context *ctx)
{
    double sigma = 1.0;

    if(!duk_is_undefined(ctx, 0))
        REQUIRE_NUMBER(ctx, 0, "crypto.gaussrand requires a number (sigma) as it's argument");

    duk_push_number(ctx, gaussrand(ctx, sigma));

    return 1;
}

static duk_ret_t duk_normrand(duk_context *ctx)
{
    double scale = 1.0;

    if(!duk_is_undefined(ctx, 0))
        REQUIRE_NUMBER(ctx, 0, "crypto.normrand requires a number (scale) as it's argument");

    duk_push_number(ctx, normrand(ctx, scale));

    return 1;
}


/**
 * Seeds the random number generator with a given file and size.
 * NOTE: Files are meant to be seed files like /dev/random. See https://wiki.openssl.org/index.php/Random_Numbers
 * @typedef {Object} SeedOptions
 * @property {file} The seed file
 * @property {bytes} the number of bytes to be taken from the seed file.
 * @param {SeedOptions} The Seed Options
 */
static duk_ret_t duk_seed_rand(duk_context *ctx)
{
    duk_uint_t bytes = 32;
    const char *file = "/dev/urandom";
    int rc;

    if(!duk_is_undefined(ctx,0))
    {
        if(!duk_is_object(ctx, 0))
            RP_THROW(ctx, "crypto.seed - argument must be an object");

        if(duk_get_prop_string(ctx, 0, "bytes"))
            bytes = REQUIRE_UINT(ctx, -1, "crypto.seed - \"bytes\" requires a positive integer (number of bytes)");
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "file"))
            file = REQUIRE_STRING(ctx, -1, "crypto.seed - \"file\" requires a string (filename)");
        duk_pop(ctx);
    }

    rc = RAND_load_file(file, bytes);
    if (rc != bytes)
        DUK_OPENSSL_ERROR(ctx);

    seeded=1;
    return 0;
}
static int pass_cb(char *buf, int size, int rwflag, void *u)
{
    const char *p = (const char *)u;
    size_t len = 0;

    if(!p)
        return 0;

    len = strlen(p);
    if (len > size)
         len = size;

    memcpy(buf, p, len);
    return len;
}

/*
duk_ret_t duk_rsa_pub_encrypt_bak(duk_context *ctx)
{
    duk_size_t sz, psz;
    unsigned char *plain=NULL;
    const char *pubfile=NULL;
    RSA *rsa;
    int ret, rsasize, outsize, padding=RSA_PKCS1_PADDING;
    BIO *pfile;
    unsigned char *buf;

    // data to be encrypted
    if(duk_is_string(ctx, 0) )
        plain = (unsigned char *) duk_get_lstring(ctx, 0, &sz);
    else if (duk_is_buffer_data(ctx, 0) )
        plain = (unsigned char *) duk_get_buffer_data(ctx, 0, &sz);
    else
        RP_THROW(ctx, "crypt.rsa_pub_encrypt - first argument must be a string or buffer (data to encrypt)");

    if(duk_is_string(ctx, 1) )
        pubfile = duk_get_lstring(ctx, 1, &psz);
    else if (duk_is_buffer_data(ctx, 1) )
        pubfile = (const char *) duk_get_buffer_data(ctx, 1, &psz);
    else
        RP_THROW(ctx, "crypt.rsa_pub_encrypt - second argument must be a string or buffer (pem file)");

    pfile = BIO_new_mem_buf((const void*)pubfile, (int)psz);

    rsa = PEM_read_bio_RSA_PUBKEY(pfile, NULL, NULL, NULL);
    if (!rsa)
    {
        if(BIO_reset(pfile)!=1)
            RP_THROW(ctx, "crypt.rsa_pub_encrypt - internal error,  BIO_reset()");
        rsa = PEM_read_bio_RSAPublicKey(pfile, NULL, NULL, NULL);
    }

    BIO_free(pfile);

    if(!rsa)
        RP_THROW(ctx, "Invalid public key file '%s'", pubfile);

    rsasize = RSA_size(rsa);
    outsize=rsasize;

    if(duk_is_string(ctx, 2) )
    {
        const char *pad = duk_get_string(ctx, 2);
        if (!strcmp ("pkcs", pad) )
        {
            padding=RSA_PKCS1_PADDING;
            rsasize-=11;
        }
        else if (!strcmp ("oaep", pad) )
        {
            padding=RSA_PKCS1_OAEP_PADDING;
            rsasize-=42;
        }
        else if (!strcmp ("ssl", pad) )
        {
            padding=RSA_SSLV23_PADDING;
            rsasize-=11;
        }
        else if (!strcmp ("raw", pad) )
            padding=RSA_NO_PADDING;
        else
            RP_THROW(ctx, "crypt.rsa_pub_encrypt - third optional argument (padding type) '%s' is invalid", pad);
    }
    else if (!duk_is_undefined(ctx, 2) && !duk_is_null(ctx, 2) )
        RP_THROW(ctx, "crypt.rsa_pub_encrypt - third optional argument must be a string (padding type)");
    else
        rsasize -= 11; //default is RSA_PKCS1_PADDING

    if((int)sz > rsasize )
        RP_THROW(ctx, "crypt.rsa_pub_encrypt, input data is %d long, must be less than or equal to %d\n", sz, rsasize);


    buf = (unsigned char *) duk_push_fixed_buffer(ctx, (duk_size_t)outsize);

    ret = RSA_public_encrypt((int)sz, plain, buf, rsa, padding);

    if (ret < 0)
        DUK_OPENSSL_ERROR(ctx);

    return 1;
}
*/

duk_ret_t duk_gen_csr(duk_context *ctx)
{
    int ret=0;
    X509_REQ *req=NULL;
    EVP_PKEY *key=NULL;
    duk_size_t sz=0;
    duk_idx_t obj_idx=-1, str_idx=0, pass_idx=2;
    const char *privkey=NULL, *txt=NULL, *passwd=NULL;
    unsigned char *der=NULL;
    X509_NAME *x509_name = NULL;
    RSA *rsa;
    BIO *pfile;
    BIO * csr = NULL;
    void *buf;

    if (duk_is_object(ctx, 0))
    {
        obj_idx=0;
        str_idx=1;
    }
    else if (duk_is_object(ctx, 1))
        obj_idx=1;
    else
        pass_idx=1;


    privkey = REQUIRE_STR_OR_BUF(ctx, str_idx, &sz, "crypto.gen_csr - parameter must be a string or buffer (private key file)");

    req = X509_REQ_new();

    x509_name = X509_REQ_get_subject_name(req);

    if (obj_idx>-1)
    {
        int gen_type = GEN_DNS;

        if(duk_get_prop_string(ctx, obj_idx, "country"))
        {
            txt=REQUIRE_STRING(ctx, -1, "crypto.gen_csr - 'country' parameter must be a string");
            ret = X509_NAME_add_entry_by_txt(x509_name,"C", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "state"))
        {
            txt=REQUIRE_STRING(ctx, -1, "crypto.gen_csr - 'state' parameter must be a string (state/province)");
            ret = X509_NAME_add_entry_by_txt(x509_name,"ST", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "city"))
        {
            txt=REQUIRE_STRING(ctx, -1, "crypto.gen_csr - 'city' parameter must be a string (city/locality)");
            ret = X509_NAME_add_entry_by_txt(x509_name,"L", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "organization"))
        {
            txt=REQUIRE_STRING(ctx, -1, "crypto.gen_csr - 'organization' parameter must be a string (organization)");
            ret = X509_NAME_add_entry_by_txt(x509_name,"O", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "organizationUnit"))
        {
            txt=REQUIRE_STRING(ctx, -1, "crypto.gen_csr - 'organizationUnit' parameter must be a string (organization Unit)");
            ret = X509_NAME_add_entry_by_txt(x509_name,"OU", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "name"))
        {
            txt=REQUIRE_STRING(ctx, -1, "crypto.gen_csr - 'name' parameter must be a string (name/domain name/common name)");
            ret = X509_NAME_add_entry_by_txt(x509_name,"CN", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "email"))
        {
            txt=REQUIRE_STRING(ctx, -1, "crypto.gen_csr - 'email' parameter must be a string (email address)");
            ret = X509_NAME_add_entry_by_txt(x509_name,"C", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);

#define addext(str) do {\
    GENERAL_NAME *gen = GENERAL_NAME_new();\
    ASN1_IA5STRING *san_ASN1 = ASN1_IA5STRING_new();\
    if(!san_ASN1)\
        RP_THROW(ctx, "crypto.gen_csr - internal error - ASN1_IA5STRING_new()\n");\
    if(!ASN1_STRING_set(san_ASN1, (unsigned char*) (str), strlen((str)))){\
        ASN1_IA5STRING_free(san_ASN1);\
        GENERAL_NAME_free(gen);\
        GENERAL_NAMES_free(gens);\
        RP_THROW(ctx, "crypto.gen_csr - internal error - ASN1_STRING_set()");\
    }\
    GENERAL_NAME_set0_value(gen, gen_type, san_ASN1);\
    sk_GENERAL_NAME_push(gens, gen);\
} while(0)

        if(duk_get_prop_string(ctx, obj_idx, "subjectAltNameType"))
        {
            const char *type = REQUIRE_STRING(ctx, -1, "crypto.gen_csr - 'subjectAltNameType' must be a string(e.g., 'dns' 'ip', 'email', etc.)");
            if(!strcasecmp("dns", type))
                gen_type=GEN_DNS;
            else if (!strcasecmp("email", type))
                gen_type=GEN_EMAIL;
            else if (!strcasecmp("ip", type))
                gen_type=GEN_IPADD;
            else if (!strcasecmp("othername", type))
                gen_type=GEN_OTHERNAME;
            else if (!strcasecmp("x400", type))
                gen_type=GEN_X400;
            else if (!strcasecmp("dirname", type))
                gen_type=GEN_DIRNAME;
            else if (!strcasecmp("ediparty", type))
                gen_type=GEN_EDIPARTY;
            else if (!strcasecmp("uri", type))
                gen_type=GEN_URI;
            else if (!strcasecmp("rid", type))
                gen_type=GEN_RID;
            else
                RP_THROW(ctx, "crypto.gen_csr - 'subjectAltNameType' must be a string(e.g., 'dns' 'ip', 'email', etc.)");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "subjectAltName"))
        {
            const char *san;
            int ret=0;
            STACK_OF(X509_EXTENSION) *ext_list = NULL;
            GENERAL_NAMES *gens = sk_GENERAL_NAME_new_null();
            if (gens == NULL)
                RP_THROW(ctx, "crypto.gen_csr - internal error - sk_GENERAL_NAME_new_null()");

            if(duk_is_array(ctx, -1))
            {
                int i=0, l = duk_get_length(ctx, -1);
                for (; i<l;i++)
                {
                    duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
                    san = REQUIRE_STRING(ctx, -1, "crypto.gen_csr - 'subjectAltName' parameter must be a string or array of strings");
                    addext(san);
                    duk_pop(ctx);
                }
            }
            else
            {
                san = REQUIRE_STRING(ctx, -1, "crypto.gen_csr - 'subjectAltName' parameter must be a string or array of strings");
                addext(san);
            }

            if (!X509V3_add1_i2d(&ext_list, NID_subject_alt_name, gens, 0, 0))
            {
                GENERAL_NAMES_free(gens);
                RP_THROW(ctx, "crypto.gen_csr - internal error - X509V3_add1_i2d()");
            }

            ret = X509_REQ_add_extensions(req, ext_list);
            GENERAL_NAMES_free(gens);
            sk_X509_EXTENSION_pop_free (ext_list, X509_EXTENSION_free);
            if(!ret)
                RP_THROW(ctx, "crypto.gen_csr - internal error - X509_REQ_add_extensions()\n");

        }
        duk_pop(ctx);
    }

    if(!duk_is_null(ctx, pass_idx) && !duk_is_undefined(ctx, pass_idx))
        passwd = REQUIRE_STRING(ctx, pass_idx, "crypto.gen_csr - parameter %d must be a string (password)", (int)(pass_idx + 1));

    pfile = BIO_new_mem_buf((const void*)privkey, (int)sz);

    if(!passwd)
        rsa = PEM_read_bio_RSAPrivateKey(pfile, NULL, pass_cb, NULL);
    else
        rsa = PEM_read_bio_RSAPrivateKey(pfile, NULL, pass_cb, (void*)passwd);

    if(!rsa)
    {
        BIO_free_all(pfile);
        RP_THROW(ctx, "Invalid public key file%s", passwd?" or bad password":"");
    }

    key = EVP_PKEY_new();
    EVP_PKEY_assign_RSA(key, rsa);

    ret = X509_REQ_set_pubkey(req, key);
    if (ret != 1)
    {
        X509_REQ_free(req);
        EVP_PKEY_free(key);
        BIO_free_all(pfile);
        DUK_OPENSSL_ERROR(ctx);
    }

    ret = X509_REQ_sign(req, key, EVP_sha256());
    if (ret <= 0)
    {
        X509_REQ_free(req);
        EVP_PKEY_free(key);
        BIO_free_all(pfile);
        DUK_OPENSSL_ERROR(ctx);
    }

    csr = BIO_new(BIO_s_mem());
    ret = PEM_write_bio_X509_REQ(csr, req);
    if (ret != 1)
    {
        X509_REQ_free(req);
        EVP_PKEY_free(key);
        BIO_free_all(csr);
        BIO_free_all(pfile);
        DUK_OPENSSL_ERROR(ctx);
    }

    duk_push_object(ctx);
    ret = i2d_X509_REQ(req, NULL);
    if (ret < 0 )
    {
        X509_REQ_free(req);
        EVP_PKEY_free(key);
        BIO_free_all(csr);
        BIO_free_all(pfile);
        DUK_OPENSSL_ERROR(ctx);
    }

    der = (unsigned char*) duk_push_fixed_buffer(ctx, (duk_size_t)ret);
    ret = i2d_X509_REQ(req, &der);
    if (ret < 0 )
    {
        X509_REQ_free(req);
        EVP_PKEY_free(key);
        BIO_free_all(csr);
        BIO_free_all(pfile);
        DUK_OPENSSL_ERROR(ctx);
    }
    duk_put_prop_string(ctx, -2, "der");


    ret = BIO_get_mem_data(csr, &buf);
    duk_push_lstring(ctx, (char *) buf, (duk_size_t)ret);
    duk_put_prop_string(ctx, -2, "pem");
    X509_REQ_free(req);
    EVP_PKEY_free(key);
    BIO_free_all(csr);
    BIO_free_all(pfile);

    return 1;
}



#define DUK_GEN_OPENSSL_ERROR(ctx) do { \
    if(rsa) RSA_free(rsa);              \
    if(e)BN_free(e);                    \
    BIO_free_all(bio_priv);             \
    BIO_free_all(bio_pub);              \
    BIO_free_all(bio_rsapriv);          \
    BIO_free_all(bio_rsapub);           \
    DUK_OPENSSL_ERROR(ctx);             \
} while(0)

#define RP_GEN_THROW(ctx, ...) do {     \
    if(rsa) RSA_free(rsa);              \
    if(e)BN_free(e);                    \
    BIO_free_all(bio_priv);             \
    BIO_free_all(bio_pub);              \
    BIO_free_all(bio_rsapriv);          \
    BIO_free_all(bio_rsapub);           \
    RP_THROW( (ctx), __VA_ARGS__);      \
} while(0)

static int sig_dump(BIO *bp, const ASN1_STRING *sig)
{
    const unsigned char *s;
    int i, n;

    n = sig->length;
    s = sig->data;
    for (i = 0; i < n; i++) {
        if (BIO_printf(bp, "%02x", s[i]) <= 0)
            return 0;
    }
    return 1;
}




duk_ret_t duk_cert_info(duk_context *ctx)
{
//    long l;
//    int i;
//    char *m = NULL, mlch = ' ';
//    int nmindent = 0;
    ASN1_INTEGER *bs;
    EVP_PKEY *pkey = NULL;
//    const char *neg;

    X509 *x=NULL;
    const char *file=NULL;
    BIO *pfile=NULL, *btmp=NULL;
    duk_size_t psz;
    int ret=0;
    BIGNUM *bn=NULL;
    char *hex=NULL;
    const X509_ALGOR *tsig_alg;
    const ASN1_TIME *at;
    struct tm timev, *tm=&timev;

    if(duk_is_string(ctx, 0) )
        file = duk_get_lstring(ctx, 0, &psz);
    else if (duk_is_buffer_data(ctx, 0) )
        file = (const char *) duk_get_buffer_data(ctx, 0, &psz);
    else
        RP_THROW(ctx, "crypt.cert_info - argument must be a string or buffer (pem file content)");

    pfile = BIO_new_mem_buf((const void*)file, (int)psz);

    x = PEM_read_bio_X509(pfile, &x, pass_cb, NULL);
    if(!x)
    {
        BIO_free(pfile);
        X509_free(x);
        RP_THROW(ctx, "crypto.cert_info - invalid input");
    }

    duk_push_object(ctx);
    duk_push_int(ctx, (int)X509_get_version(x));
    duk_put_prop_string(ctx, -2, "version");

    bs = X509_get_serialNumber(x);
    bn = ASN1_INTEGER_to_BN(bs, bn);
    hex=BN_bn2hex(bn);
    BN_free(bn);
    if(!hex)
    {
        BIO_free(pfile);
        X509_free(x);
        RP_THROW(ctx, "crypt.cert_info - internal error, bn2hex(e)");
    }
    duk_push_string(ctx, hex);
    OPENSSL_free(hex);
    hex=NULL;
    duk_put_prop_string(ctx, -2, "serialNumber");

    tsig_alg = X509_get0_tbs_sigalg(x);

#define putbio(str) {\
    char *p=NULL;\
    BIO_get_mem_data(btmp, &p);\
    if(*p) {\
        duk_push_string(ctx, p);\
        duk_put_prop_string(ctx, -2, str);\
    }\
}\
if(BIO_reset(btmp)!=1){\
    RP_THROW(ctx, "crypt.cert_info - internal error,  BIO_reset()");\
}

    btmp = BIO_new(BIO_s_mem());
    if (X509_signature_print(btmp, tsig_alg, NULL) > 0)
    {
        char *p=NULL;
        BIO_get_mem_data(btmp, &p);
        if(*p) {
            char *s;
            p+=25;
            s=strchr(p,'\n');
            if(s)
                *s='\0';
            duk_push_string(ctx, p);
            duk_put_prop_string(ctx, -2, "signatureAlgorithm");
        }
    } else DUK_OPENSSL_ERROR(ctx);

    if(BIO_reset(btmp)!=1)
        RP_THROW(ctx, "crypt.cert_info - internal error,  BIO_reset()");

    if (X509_NAME_print_ex(btmp, X509_get_issuer_name(x), 0, 0) > -1)
        putbio("issuer");

    at = X509_get0_notBefore(x);
    ret = ASN1_TIME_to_tm(at, tm);
    if(ret)
    {
        double time = (double) timegm(tm);
        (void)duk_get_global_string(ctx, "Date");
        duk_push_number(ctx, 1000.0 * (duk_double_t) time);
        duk_new(ctx, 1);
        duk_put_prop_string(ctx, -2, "notBefore");
    }

    at = X509_get0_notAfter(x);
    ret = ASN1_TIME_to_tm(at, tm);
    if(ret)
    {
        double time = (double) timegm(tm);
        (void)duk_get_global_string(ctx, "Date");
        duk_push_number(ctx, 1000.0 * (duk_double_t) time);
        duk_new(ctx, 1);
        duk_put_prop_string(ctx, -2, "notAfter");
    }

    if (X509_NAME_print_ex(btmp, X509_get_subject_name(x), 0,0) > -1)
        putbio("subject");

    if(BIO_reset(btmp)!=1)
        RP_THROW(ctx, "crypt.cert_info - internal error,  BIO_reset()");

#define pushrsahex(bn) do {\
    hex=BN_bn2hex((bn));\
    if(!hex){ \
        RSA_free(rsa);\
        RP_THROW(ctx, "crypt.cert_info- internal error, bn2hex(e)");\
    }\
    duk_push_string(ctx, hex);\
    OPENSSL_free(hex);\
    hex=NULL;\
} while(0)

    {
        X509_PUBKEY *xpkey = X509_get_X509_PUBKEY(x);
        ASN1_OBJECT *xpoid;
        X509_PUBKEY_get0_param(&xpoid, NULL, NULL, NULL, xpkey);
        if (i2a_ASN1_OBJECT(btmp, xpoid) > -1)
            putbio("publicKeyAlgorithm");
        pkey = X509_get0_pubkey(x);
        if (pkey != NULL) {
            RSA *rsa = EVP_PKEY_get1_RSA(pkey);
            const BIGNUM *n, *e;

            n = RSA_get0_n(rsa);
            e = RSA_get0_e(rsa);
            pushrsahex(n);
            duk_put_prop_string(ctx, -2, "publicKeyModulus");
            pushrsahex(e);
            duk_put_prop_string(ctx, -2, "publicKeyExponent");
            RSA_free(rsa);
        }
    }
    {
        const ASN1_BIT_STRING *iuid, *suid;
        X509_get0_uids(x, &iuid, &suid);
        if (iuid != NULL) {
            if (X509_signature_dump(btmp, iuid, 12))
                putbio("issuerUID");
        }
        if (suid != NULL) {
            if (!X509_signature_dump(btmp, suid, 12))
                putbio("subjectUID");
        }
    }
#define pushbio {\
    char *p=NULL;\
    duk_size_t l = (duk_size_t)BIO_get_mem_data(btmp, &p);\
    if(*p) {\
        duk_push_lstring(ctx, p, l);\
    }\
}\
if(BIO_reset(btmp)!=1){\
    RP_THROW(ctx, "crypt.cert_info - internal error,  BIO_reset()");\
}

    duk_push_object(ctx);
    {
        int i, j;
        const STACK_OF(X509_EXTENSION) *exts = X509_get0_extensions(x);
        ret = 1;
        if (sk_X509_EXTENSION_num(exts) <= 0)
            ret=0;


        for (i = 0; i < sk_X509_EXTENSION_num(exts); i++) {
            ASN1_OBJECT *obj;
            X509_EXTENSION *ex;
            ex = sk_X509_EXTENSION_value(exts, i);
            obj = X509_EXTENSION_get_object(ex);
            i2a_ASN1_OBJECT(btmp, obj);
            pushbio;

            j = X509_EXTENSION_get_critical(ex);
            if(j)
            {
                duk_push_string(ctx, "_critical");
                duk_concat(ctx, 2);
            }

            if (X509V3_EXT_print(btmp, ex, 0, 0)) {
                pushbio;
            }
            else
            {
                ASN1_STRING_print(btmp, X509_EXTENSION_get_data(ex));
                pushbio;
            }
            duk_put_prop(ctx, -3);
        }
    }

    if(ret==1)
        duk_put_prop_string(ctx, -2, "extensions");
    else
        duk_pop(ctx);

    {
        const X509_ALGOR *sigalg;
        ASN1_BIT_STRING *sig;
        X509_get0_signature((const ASN1_BIT_STRING **)&sig, &sigalg, x);

        if (i2a_ASN1_OBJECT(btmp, sigalg->algorithm) > -1)
            putbio("signatureAlgorithm")

        if(sig_dump(btmp, sig) == 1)
            putbio("signature")

        if (X509_aux_print(btmp, x, 0))
            putbio("aux")
    }
/*
        X509V3_extensions_print(bp, "X509v3 extensions",
                                X509_get0_extensions(x), cflag, 8);
*/
    X509_free(x);
    BIO_free(pfile);
    BIO_free(btmp);
    return 1;

}


duk_ret_t duk_rsa_import_priv_key(duk_context *ctx)
{
    RSA *rsa=NULL;
    duk_size_t psz=0;
    const char *outpasswd=NULL, *inpasswd=NULL, *privfile=NULL;
    BIO *pfile=NULL;
    BIO * bio_priv = BIO_new(BIO_s_mem());
    BIO * bio_pub = BIO_new(BIO_s_mem());
    BIO * bio_rsapriv = BIO_new(BIO_s_mem());
    BIO * bio_rsapub = BIO_new(BIO_s_mem());
    void *buf, *e=NULL;
    EVP_PKEY *privkey;
    int ret=0;

    if(duk_is_string(ctx, 0) )
        privfile = duk_get_lstring(ctx, 0, &psz);
    else if (duk_is_buffer_data(ctx, 0) )
        privfile = (const char *) duk_get_buffer_data(ctx, 0, &psz);
    else
        RP_THROW(ctx, "crypt.rsa_import_key - first argument must be a string or buffer (pem file content)");

    if(!privfile)
        RP_THROW(ctx, "crypt.rsa_sign - argument must be a string or buffer (pem file content)");

    if (duk_is_string(ctx, 1))
        inpasswd = REQUIRE_STRING(ctx, 1, "rypt.rsa_sign - decrypt password must be a string");
    else if(duk_is_object(ctx, 1) )
    {
        if(duk_get_prop_string(ctx, 1, "decryptPassword"))
            inpasswd = REQUIRE_STRING(ctx, -1, "rypt.rsa_sign - decryptPassword must be a string");
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 1, "encryptPassword"))
            outpasswd = REQUIRE_STRING(ctx, -1, "rypt.rsa_sign - encryptPassword must be a string");
        duk_pop(ctx);
    }
    else if (!duk_is_undefined(ctx, 1) && !duk_is_null(ctx, 1))
        RP_THROW(ctx, "second argument must be an object (with passwords)");

    if (!outpasswd && duk_is_string(ctx, 2))
        outpasswd = REQUIRE_STRING(ctx, 2, "rypt.rsa_sign - decrypt password must be a string");

    pfile = BIO_new_mem_buf((const void*)privfile, (int)psz);

    if(!inpasswd)
        rsa = PEM_read_bio_RSAPrivateKey(pfile, NULL, pass_cb, NULL);
    else
        rsa = PEM_read_bio_RSAPrivateKey(pfile, NULL, pass_cb, (void*)inpasswd);

    BIO_free_all(pfile);

    /* get '-----BEGIN RSA PRIVATE KEY-----' file */
    if(outpasswd)
        ret=PEM_write_bio_RSAPrivateKey(bio_rsapriv, rsa, EVP_aes_256_cbc(), (unsigned char *)outpasswd, strlen(outpasswd), NULL, NULL);
    else
        ret=PEM_write_bio_RSAPrivateKey(bio_rsapriv, rsa, NULL, NULL, 0, NULL, NULL);

    if(ret !=1)
        RP_GEN_THROW(ctx, "crypto.rsa_gen_key - erro generating key\n");

    ret = BIO_get_mem_data(bio_rsapriv, &buf);
    duk_push_object(ctx);
    duk_push_lstring(ctx, (char *) buf, (duk_size_t)ret);
    duk_put_prop_string (ctx, -2, "rsa_private");


    /* get '-----BEGIN RSA PUBLIC KEY-----' file */
    ret = PEM_write_bio_RSAPublicKey(bio_rsapub, rsa);
    if(ret !=1)
        RP_GEN_THROW(ctx, "crypto.rsa_gen_key - erro generating key\n");

    ret = BIO_get_mem_data(bio_rsapub, &buf);
    duk_push_lstring(ctx, (char *) buf, (duk_size_t)ret);
    duk_put_prop_string (ctx, -2, "rsa_public");

    /* get '-----BEGIN PUBLIC KEY-----' file */
    ret = PEM_write_bio_RSA_PUBKEY(bio_pub, rsa);
    if(ret !=1)
        RP_GEN_THROW(ctx, "crypto.rsa_gen_key - erro generating key\n");

    ret = BIO_get_mem_data(bio_pub, &buf);
    duk_push_lstring(ctx, (char *) buf, (duk_size_t)ret);
    duk_put_prop_string (ctx, -2, "public");

    /* get '-----BEGIN PRIVATE KEY-----' file */
    privkey = EVP_PKEY_new();
    EVP_PKEY_assign_RSA(privkey, rsa);
    if(outpasswd)
        ret=PEM_write_bio_PKCS8PrivateKey(bio_priv, privkey, EVP_aes_256_cbc(), (char *)outpasswd, strlen(outpasswd), NULL, NULL);
    else
        ret=PEM_write_bio_PKCS8PrivateKey(bio_priv, privkey, NULL, NULL, 0, NULL, NULL);
    EVP_PKEY_free(privkey);
    rsa=NULL;

    if(ret !=1)
        RP_GEN_THROW(ctx, "crypto.rsa_gen_key - erro generating key\n");

    ret = BIO_get_mem_data(bio_priv, &buf);
    duk_push_lstring(ctx, (char *) buf, (duk_size_t)ret);
    duk_put_prop_string (ctx, -2, "private");

    BN_free(e);
    BIO_free_all(bio_priv);
    BIO_free_all(bio_pub);
    BIO_free_all(bio_rsapriv);
    BIO_free_all(bio_rsapub);

    return 1;
}

duk_ret_t duk_rsa_gen_key(duk_context *ctx)
{
    BIGNUM *e = NULL;
    RSA *rsa=NULL;
    int bits=4096;
    const char *passwd=NULL;
    BIO * bio_priv = BIO_new(BIO_s_mem());
    BIO * bio_pub = BIO_new(BIO_s_mem());
    BIO * bio_rsapriv = BIO_new(BIO_s_mem());
    BIO * bio_rsapub = BIO_new(BIO_s_mem());
    void *buf;
    EVP_PKEY *privkey;
    int ret=0;

    e=BN_new();
    if( BN_set_word(e, RSA_F4) !=1)
        RP_GEN_THROW(ctx, "crypto.rsa_gen_key - erro generating key\n");

    rsa = RSA_new();
    if(!rsa)
        RP_GEN_THROW(ctx, "crypto.rsa_gen_key - erro generating key\n");

    if (duk_is_number(ctx,0))
        bits=duk_get_int(ctx, 0);
    else if (!duk_is_undefined(ctx, 0) && !duk_is_null(ctx, 0))
        RP_GEN_THROW(ctx, "crypto.rsa_gen_key - first argument must be a number (bits)");

    if (duk_is_string(ctx,1))
        passwd=duk_get_string(ctx, 1);
    else if (!duk_is_undefined(ctx, 1) && !duk_is_null(ctx, 1))
        RP_GEN_THROW(ctx, "crypto.rsa_gen_key - second optional argument must be a string (password)");


    if (RAND_load_file("/dev/urandom", 32) != 32)
        DUK_GEN_OPENSSL_ERROR(ctx);

    if (RSA_generate_key_ex(rsa, bits, e, NULL) != 1)
        RP_GEN_THROW(ctx, "crypto.rsa_gen_key - erro generating key\n");

    /* get '-----BEGIN RSA PRIVATE KEY-----' file */
    if(passwd)
        ret=PEM_write_bio_RSAPrivateKey(bio_rsapriv, rsa, EVP_aes_256_cbc(), (unsigned char *)passwd, strlen(passwd), NULL, NULL);
    else
        ret=PEM_write_bio_RSAPrivateKey(bio_rsapriv, rsa, NULL, NULL, 0, NULL, NULL);

    if(ret !=1)
        RP_GEN_THROW(ctx, "crypto.rsa_gen_key - erro generating key\n");

    ret = BIO_get_mem_data(bio_rsapriv, &buf);
    duk_push_object(ctx);
    duk_push_lstring(ctx, (char *) buf, (duk_size_t)ret);
    duk_put_prop_string (ctx, -2, "rsa_private");


    /* get '-----BEGIN RSA PUBLIC KEY-----' file */
    ret = PEM_write_bio_RSAPublicKey(bio_rsapub, rsa);
    if(ret !=1)
        RP_GEN_THROW(ctx, "crypto.rsa_gen_key - erro generating key\n");

    ret = BIO_get_mem_data(bio_rsapub, &buf);
    duk_push_lstring(ctx, (char *) buf, (duk_size_t)ret);
    duk_put_prop_string (ctx, -2, "rsa_public");

    /* get '-----BEGIN PUBLIC KEY-----' file */
    ret = PEM_write_bio_RSA_PUBKEY(bio_pub, rsa);
    if(ret !=1)
        RP_GEN_THROW(ctx, "crypto.rsa_gen_key - erro generating key\n");

    ret = BIO_get_mem_data(bio_pub, &buf);
    duk_push_lstring(ctx, (char *) buf, (duk_size_t)ret);
    duk_put_prop_string (ctx, -2, "public");

    /* get '-----BEGIN PRIVATE KEY-----' file */
    privkey = EVP_PKEY_new();
    EVP_PKEY_assign_RSA(privkey, rsa);
    if(passwd)
        ret=PEM_write_bio_PKCS8PrivateKey(bio_priv, privkey, EVP_aes_256_cbc(), (char *)passwd, strlen(passwd), NULL, NULL);
    else
        ret=PEM_write_bio_PKCS8PrivateKey(bio_priv, privkey, NULL, NULL, 0, NULL, NULL);
    EVP_PKEY_free(privkey);
    rsa=NULL;

    if(ret !=1)
        RP_GEN_THROW(ctx, "crypto.rsa_gen_key - erro generating key\n");

    ret = BIO_get_mem_data(bio_priv, &buf);
    duk_push_lstring(ctx, (char *) buf, (duk_size_t)ret);
    duk_put_prop_string (ctx, -2, "private");

    BN_free(e);
    BIO_free_all(bio_priv);
    BIO_free_all(bio_pub);
    BIO_free_all(bio_rsapriv);
    BIO_free_all(bio_rsapub);

    return 1;
}


#define DUK_EVP_OPENSSL_ERROR(ctx) do { \
    EVP_PKEY_free(key);                 \
    if(pctx)EVP_PKEY_CTX_free(pctx);    \
    DUK_OPENSSL_ERROR(ctx);             \
} while(0)

#define RP_EVP_THROW(ctx, ...) do {     \
    EVP_PKEY_free(key);                 \
    if(pctx)EVP_PKEY_CTX_free(pctx);    \
    RP_THROW( (ctx), __VA_ARGS__);      \
} while(0)

#define DUK_MD_OPENSSL_ERROR(ctx) do { \
    EVP_PKEY_free(key);                 \
    if(pctx)EVP_MD_CTX_free(pctx);      \
    DUK_OPENSSL_ERROR(ctx);             \
} while(0)

#define RP_MD_THROW(ctx, ...) do {      \
    EVP_PKEY_free(key);                 \
    if(pctx)EVP_MD_CTX_free(pctx);      \
    RP_THROW( (ctx), __VA_ARGS__);      \
} while(0)

duk_ret_t duk_rsa_sign(duk_context *ctx)
{
    duk_size_t sz, psz;
    unsigned char *msg=NULL;
    const char *privfile=NULL, *passwd=NULL;
    RSA *rsa;
    EVP_PKEY *key=EVP_PKEY_new();
    size_t outsize;
    BIO *pfile;
    EVP_MD_CTX *pctx=NULL;
    unsigned char *buf;

    /* data to be encrypted */
    if(duk_is_string(ctx, 0) )
        msg = (unsigned char *) duk_get_lstring(ctx, 0, &sz);
    else if (duk_is_buffer_data(ctx, 0) )
        msg = (unsigned char *) duk_get_buffer_data(ctx, 0, &sz);
    else
        RP_MD_THROW(ctx, "crypt.rsa_sign - first argument must be a string or buffer (data to encrypt)");

    if(duk_is_string(ctx, 1) )
        privfile = duk_get_lstring(ctx, 1, &psz);
    else if (duk_is_buffer_data(ctx, 1) )
        privfile = (const char *) duk_get_buffer_data(ctx, 1, &psz);
    else
        RP_MD_THROW(ctx, "crypt.rsa_sign - second argument must be a string or buffer (pem file content)");

    if(!privfile)
        RP_THROW(ctx, "crypt.rsa_sign - argument must be a string or buffer (pem file content)");

    if(duk_is_string(ctx, 2))
        passwd = duk_get_string(ctx, 2);
    else if (!duk_is_null(ctx, 2) && !duk_is_undefined(ctx, 2) )
        RP_MD_THROW(ctx, "crypt.rsa_sign - third optional argument must be a string (password)");

    pfile = BIO_new_mem_buf((const void*)privfile, (int)psz);

    if(!passwd)
        rsa = PEM_read_bio_RSAPrivateKey(pfile, NULL, pass_cb, NULL);
    else
        rsa = PEM_read_bio_RSAPrivateKey(pfile, NULL, pass_cb, (void*)passwd);

    BIO_free_all(pfile);

    if(!rsa)
        RP_MD_THROW(ctx, "Invalid public key file%s", passwd?" or bad password":"");

    EVP_PKEY_assign_RSA(key, rsa);

    pctx = EVP_MD_CTX_new();
    if (!pctx)
        DUK_MD_OPENSSL_ERROR(ctx);

    if( EVP_DigestSignInit(pctx, NULL, EVP_sha256(), NULL, key) <= 0)
        DUK_MD_OPENSSL_ERROR(ctx);

    if( EVP_DigestSignUpdate(pctx, msg, (size_t)sz) <= 0)
        DUK_MD_OPENSSL_ERROR(ctx);

    if (EVP_DigestSignFinal(pctx, NULL, &outsize) <= 0)
        DUK_MD_OPENSSL_ERROR(ctx);

    buf = (unsigned char *) duk_push_dynamic_buffer(ctx, (duk_size_t)outsize);

    if (EVP_DigestSignFinal(pctx, buf, &outsize) <= 0)
        DUK_MD_OPENSSL_ERROR(ctx);

    duk_resize_buffer(ctx, -1, outsize);

    EVP_PKEY_free(key);
    EVP_MD_CTX_free(pctx);

    return 1;
}

duk_ret_t duk_rsa_verify(duk_context *ctx)
{
    duk_size_t sz, psz, sigsz;
    unsigned char *msg=NULL;
    const char *pubfile=NULL;
    RSA *rsa;
    EVP_PKEY *key=EVP_PKEY_new();
    BIO *pfile;
    EVP_MD_CTX *pctx=NULL;
    unsigned char *sig=NULL;

    if(duk_is_string(ctx, 0) )
        msg = (unsigned char *) duk_get_lstring(ctx, 0, &sz);
    else if (duk_is_buffer_data(ctx, 0) )
        msg = (unsigned char *) duk_get_buffer_data(ctx, 0, &sz);
    else
        RP_MD_THROW(ctx, "crypt.rsa_verify - first argument must be a string or buffer (data to encrypt)");

    if(duk_is_string(ctx, 1) )
        pubfile = duk_get_lstring(ctx, 1, &psz);
    else if (duk_is_buffer_data(ctx, 1) )
        pubfile = (const char *) duk_get_buffer_data(ctx, 1, &psz);
    else
        RP_MD_THROW(ctx, "crypt.rsa_verify - second argument must be a string or buffer (pem file content)");

    if(!pubfile)
        RP_THROW(ctx, "crypt.rsa_verify - argument must be a string or buffer (pem file content)");

    if(duk_is_string(ctx, 2) )
        sig = (unsigned char *)duk_get_lstring(ctx, 2, &sigsz);
    else if (duk_is_buffer_data(ctx, 2) )
        sig = (unsigned char *) duk_get_buffer_data(ctx, 2, &sigsz);
    else
        RP_MD_THROW(ctx, "crypt.rsa_verify - third argument must be a string or buffer (signature)");

    pfile = BIO_new_mem_buf((const void*)pubfile, (int)psz);

    rsa = PEM_read_bio_RSA_PUBKEY(pfile, NULL, NULL, NULL);
    if (!rsa)
    {
        if(BIO_reset(pfile)!=1)
            RP_MD_THROW(ctx, "crypt.rsa_verify - internal error,  BIO_reset()");
        rsa = PEM_read_bio_RSAPublicKey(pfile, NULL, NULL, NULL);
    }

    BIO_free_all(pfile);

    if(!rsa)
        RP_MD_THROW(ctx, "Invalid public key file");

    EVP_PKEY_assign_RSA(key, rsa);

    pctx = EVP_MD_CTX_new();
    if (!pctx)
        DUK_MD_OPENSSL_ERROR(ctx);

    if( EVP_DigestVerifyInit(pctx, NULL, EVP_sha256(), NULL, key) <= 0)
        DUK_MD_OPENSSL_ERROR(ctx);

    if( EVP_DigestVerifyUpdate(pctx, msg, (size_t)sz) <= 0)
        DUK_MD_OPENSSL_ERROR(ctx);

    if (EVP_DigestVerifyFinal(pctx, sig, (size_t)sigsz) == 1)
        duk_push_true(ctx);
    else
        duk_push_false(ctx);

    EVP_PKEY_free(key);
    EVP_MD_CTX_free(pctx);

    return 1;
}

duk_ret_t duk_rsa_components(duk_context *ctx)
{
    BIO *pfile=NULL;
    int ispub=0;
    RSA *rsa=NULL;
    const char *file=NULL, *passwd=NULL, *s=NULL;
    duk_size_t psz=0;
    const BIGNUM *n;
    const BIGNUM *e;
    char *hex=NULL;

    if(duk_is_string(ctx, 0) )
        file = duk_get_lstring(ctx, 0, &psz);
    else if (duk_is_buffer_data(ctx, 0) )
        file = (const char *) duk_get_buffer_data(ctx, 0, &psz);
    else
        RP_THROW(ctx, "crypt.rsa_components - argument must be a string or buffer (pem file content)");

    if(!file)
        RP_THROW(ctx, "crypt.rsa_components - argument must be a string or buffer (pem file content)");

    if(duk_is_string(ctx, 1))
        passwd = duk_get_string(ctx, 1);
    else if (!duk_is_null(ctx, 1) && !duk_is_undefined(ctx, 1) )
        RP_THROW(ctx, "crypt.rsa_components - second optional argument must be a string (password for encrypted private pem)");


    s=strstr(file," PUBLIC ");
    if(s)
        ispub=1;
    else
    {
        s=strstr(file," PRIVATE ");
        if(!s)
            RP_THROW(ctx, "crypt.rsa_components - argument is not a pem file");
    }

    pfile = BIO_new_mem_buf((const void*)file, (int)psz);

    if (ispub)
    {
        rsa = PEM_read_bio_RSA_PUBKEY(pfile, NULL, NULL, NULL);
        if (!rsa)
        {
            if(BIO_reset(pfile)!=1)
            {
                BIO_free_all(pfile);
                RP_THROW(ctx, "crypt.rsa_components - internal error,  BIO_reset()");
            }
            rsa = PEM_read_bio_RSAPublicKey(pfile, NULL, NULL, NULL);
        }
    }
    else
    {
        if(!passwd)
            rsa = PEM_read_bio_RSAPrivateKey(pfile, NULL, pass_cb, NULL);
        else
            rsa = PEM_read_bio_RSAPrivateKey(pfile, NULL, pass_cb, (void*)passwd);

    }

    BIO_free_all(pfile);

    if(!rsa)
        RP_THROW(ctx, "crypt.rsa_components - Invalid pem file%s", passwd?" or bad password":"");

    duk_push_object(ctx);

#define pushhex(bn) do {\
    hex=BN_bn2hex((bn));\
    if(!hex){ \
        RSA_free(rsa);\
        RP_THROW(ctx, "crypt.rsa_components - internal error, bn2hex(e)");\
    }\
    duk_push_string(ctx, hex);\
    OPENSSL_free(hex);\
    hex=NULL;\
} while(0)

    n = RSA_get0_n(rsa);
    e = RSA_get0_e(rsa);

    pushhex(e);
    duk_put_prop_string(ctx, -2, "exponent");

    pushhex(n);
    duk_put_prop_string(ctx, -2, "modulus");

    if(!ispub)
    {
        const BIGNUM *d = RSA_get0_d(rsa);
        const BIGNUM *p = RSA_get0_p(rsa);
        const BIGNUM *q = RSA_get0_q(rsa);

        pushhex(d);
        duk_put_prop_string(ctx, -2, "privateExponent");

        pushhex(p);
        duk_put_prop_string(ctx, -2, "privateFactorp");

        pushhex(q);
        duk_put_prop_string(ctx, -2, "privateFactorq");
    }

    RSA_free(rsa);
    return 1;
}

duk_ret_t duk_rsa_priv_decrypt(duk_context *ctx)
{
    duk_size_t sz, psz;
    unsigned char *enc=NULL;
    const char *privfile=NULL, *passwd=NULL;
    RSA *rsa;
    EVP_PKEY *key=EVP_PKEY_new();
    int rsasize, padding=RSA_PKCS1_PADDING;
    size_t outsize;
    BIO *pfile;
    EVP_PKEY_CTX *pctx=NULL;
    unsigned char *buf;

    /* data to be encrypted */
    if(duk_is_string(ctx, 0) )
        enc = (unsigned char *) duk_get_lstring(ctx, 0, &sz);
    else if (duk_is_buffer_data(ctx, 0) )
        enc = (unsigned char *) duk_get_buffer_data(ctx, 0, &sz);
    else
        RP_EVP_THROW(ctx, "crypt.rsa_priv_decrypt - first argument must be a string or buffer (data to encrypt)");

    if(duk_is_string(ctx, 1) )
        privfile = duk_get_lstring(ctx, 1, &psz);
    else if (duk_is_buffer_data(ctx, 1) )
        privfile = (const char *) duk_get_buffer_data(ctx, 1, &psz);
    else
        RP_EVP_THROW(ctx, "crypt.rsa_priv_decrypt - second argument must be a string or buffer (pem file content)");

    if(!privfile)
        RP_THROW(ctx, "crypt.rsa_priv_decrypt - argument must be a string or buffer (pem file content)");

    if(duk_is_string(ctx, 3))
        passwd = duk_get_string(ctx, 3);
    else if (!duk_is_null(ctx, 3) && !duk_is_undefined(ctx, 3) )
        RP_EVP_THROW(ctx, "crypt.rsa_priv_decrypt - fourth optional argument must be a string (password)");

    pfile = BIO_new_mem_buf((const void*)privfile, (int)psz);

    if(!passwd)
        rsa = PEM_read_bio_RSAPrivateKey(pfile, NULL, pass_cb, NULL);
    else
        rsa = PEM_read_bio_RSAPrivateKey(pfile, NULL, pass_cb, (void*)passwd);

    BIO_free_all(pfile);

    if(!rsa)
        RP_EVP_THROW(ctx, "Invalid public key file%s", passwd?" or bad password":"");

    rsasize = RSA_size(rsa);

    if(duk_is_string(ctx, 2) )
    {
        const char *pad = duk_get_string(ctx, 2);
        if (!strcmp ("pkcs", pad) )
            padding=RSA_PKCS1_PADDING;
        else if (!strcmp ("oaep", pad) )
            padding=RSA_PKCS1_OAEP_PADDING;
        else if (!strcmp ("ssl", pad) )
            padding=RSA_SSLV23_PADDING;
        else if (!strcmp ("raw", pad) )
            padding=RSA_NO_PADDING;
        else
            RP_EVP_THROW(ctx, "crypt.rsa_priv_decrypt - third optional argument (padding type) '%s' is invalid", pad);
    }
    else if (!duk_is_undefined(ctx, 2) && !duk_is_null(ctx, 2) )
        RP_EVP_THROW(ctx, "crypt.rsa_priv_decrypt - third optional argument must be a string (padding type)");

    if((int)sz > rsasize )
        RP_EVP_THROW(ctx, "crypt.rsa_priv_decrypt, input data is %d long, must be less than or equal to %d\n", sz, rsasize);

    EVP_PKEY_assign_RSA(key, rsa);

    pctx = EVP_PKEY_CTX_new(key, NULL);
    if (!pctx)
        DUK_EVP_OPENSSL_ERROR(ctx);

    if( EVP_PKEY_decrypt_init(pctx) <= 0)
        DUK_EVP_OPENSSL_ERROR(ctx);

    if( EVP_PKEY_CTX_set_rsa_padding(pctx, padding) <= 0)
        DUK_EVP_OPENSSL_ERROR(ctx);

    if (EVP_PKEY_decrypt(pctx, NULL, &outsize, enc, (int)sz) <= 0)
        DUK_EVP_OPENSSL_ERROR(ctx);

    buf = (unsigned char *) duk_push_dynamic_buffer(ctx, (duk_size_t)outsize);

    if (EVP_PKEY_decrypt(pctx, buf, &outsize, enc, (int)sz) <= 0)
        DUK_EVP_OPENSSL_ERROR(ctx);

    duk_resize_buffer(ctx, -1, outsize);

    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(pctx);
    return 1;
}



duk_ret_t duk_rsa_pub_encrypt(duk_context *ctx)
{
    duk_size_t sz, psz;
    unsigned char *plain=NULL;
    const char *pubfile=NULL;
    RSA *rsa;
    EVP_PKEY *key=EVP_PKEY_new();
    int rsasize, padding=RSA_PKCS1_PADDING;
    size_t outsize;
    BIO *pfile;
    EVP_PKEY_CTX *pctx = NULL;
    unsigned char *buf;

    /* data to be encrypted */
    if(duk_is_string(ctx, 0) )
        plain = (unsigned char *) duk_get_lstring(ctx, 0, &sz);
    else if (duk_is_buffer_data(ctx, 0) )
        plain = (unsigned char *) duk_get_buffer_data(ctx, 0, &sz);
    else
        RP_EVP_THROW(ctx, "crypt.rsa_pub_encrypt - first argument must be a string or buffer (data to encrypt)");

    if(duk_is_string(ctx, 1) )
        pubfile = duk_get_lstring(ctx, 1, &psz);
    else if (duk_is_buffer_data(ctx, 1) )
        pubfile = (const char *) duk_get_buffer_data(ctx, 1, &psz);
    else
        RP_EVP_THROW(ctx, "crypt.rsa_pub_encrypt - second argument must be a string or buffer (pem file content)");

    if(!pubfile)
        RP_THROW(ctx, "crypt.rsa_pub_encrypt - argument must be a string or buffer (pem file content)");

    pfile = BIO_new_mem_buf((const void*)pubfile, (int)psz);

    rsa = PEM_read_bio_RSA_PUBKEY(pfile, NULL, NULL, NULL);
    if (!rsa)
    {
        if(BIO_reset(pfile)!=1)
            RP_EVP_THROW(ctx, "crypt.rsa_pub_encrypt - internal error,  BIO_reset()");
        rsa = PEM_read_bio_RSAPublicKey(pfile, NULL, NULL, NULL);
    }

    BIO_free_all(pfile);

    if(!rsa)
        RP_EVP_THROW(ctx, "Invalid public key file");

    rsasize = RSA_size(rsa);

    if(duk_is_string(ctx, 2) )
    {
        const char *pad = duk_get_string(ctx, 2);
        if (!strcmp ("pkcs", pad) )
        {
            padding=RSA_PKCS1_PADDING;
            rsasize-=11;
        }
        else if (!strcmp ("oaep", pad) )
        {
            padding=RSA_PKCS1_OAEP_PADDING;
            rsasize-=42;
        }
        else if (!strcmp ("ssl", pad) )
        {
            padding=RSA_SSLV23_PADDING;
            rsasize-=11;
        }
        else if (!strcmp ("raw", pad) )
            padding=RSA_NO_PADDING;
        else
            RP_EVP_THROW(ctx, "crypt.rsa_pub_encrypt - third optional argument (padding type) '%s' is invalid", pad);
    }
    else if (!duk_is_undefined(ctx, 2) && !duk_is_null(ctx, 2) )
        RP_EVP_THROW(ctx, "crypt.rsa_pub_encrypt - third optional argument must be a string (padding type)");
    else
        rsasize -= 11; //default is RSA_PKCS1_PADDING

    if((int)sz > rsasize )
        RP_EVP_THROW(ctx, "crypt.rsa_pub_encrypt, input data is %d long, must be less than or equal to %d\n", sz, rsasize);

    EVP_PKEY_assign_RSA(key, rsa);

    pctx = EVP_PKEY_CTX_new(key, NULL);

    if (!pctx)
        DUK_EVP_OPENSSL_ERROR(ctx);

    if( EVP_PKEY_encrypt_init(pctx) <= 0)
        DUK_EVP_OPENSSL_ERROR(ctx);

    if( EVP_PKEY_CTX_set_rsa_padding(pctx, padding) <= 0)
        DUK_EVP_OPENSSL_ERROR(ctx);

    if (EVP_PKEY_encrypt(pctx, NULL, &outsize, plain, (int)sz) <= 0)
        DUK_EVP_OPENSSL_ERROR(ctx);

    buf = (unsigned char *) duk_push_dynamic_buffer(ctx, (duk_size_t)outsize);

    if (EVP_PKEY_encrypt(pctx, buf, &outsize, plain, (int)sz) <= 0)
        DUK_EVP_OPENSSL_ERROR(ctx);

    duk_resize_buffer(ctx, -1, outsize);

    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(pctx);
    return 1;
}

const duk_function_list_entry crypto_funcs[] = {
    {"encrypt", duk_encrypt, 3},
    {"decrypt", duk_decrypt, 3},
    {"sha1", duk_sha1, 2},
    {"sha224", duk_sha224, 2},
    {"sha256", duk_sha256, 2},
    {"sha384", duk_sha384, 2},
    {"sha512", duk_sha512, 2},
    {"sha3_224", duk_sha3_224, 2},
    {"sha3_256", duk_sha3_256, 2},
    {"sha3_384", duk_sha3_384, 2},
    {"sha3_512", duk_sha3_512, 2},
    {"md5", duk_md5, 2},
    {"md4", duk_md4, 2},
    {"blake2b512", duk_blake2b512, 2},
    {"blake2s256", duk_blake2s256, 2},
    {"mdc2", duk_mdc2, 2},
    {"rmd160", duk_rmd160, 2},
    {"sha512_224", duk_sha512_224, 2},
    {"sha512_256", duk_sha512_256, 2},
    {"shake128", duk_shake128, 2},
    {"shake256", duk_shake256, 2},
    {"sm3", duk_sm3, 2},
    {"rand", duk_rand, 1},
    {"gaussrand", duk_gaussrand, 1},
    {"normrand", duk_normrand, 1},
    {"randnum", duk_randnum, 0},
    {"seed", duk_seed_rand, 1},
    {"hmac", duk_hmac, 4},
    {"hash", duk_hash, 3},
    {"rsa_pub_encrypt", duk_rsa_pub_encrypt, 3},
    {"rsa_priv_decrypt", duk_rsa_priv_decrypt, 4},
    {"rsa_sign", duk_rsa_sign, 3},
    {"rsa_verify", duk_rsa_verify, 3},
    {"rsa_gen_key", duk_rsa_gen_key, 2},
    {"gen_csr", duk_gen_csr, 3},
    {"rsa_components", duk_rsa_components, 2},
    {"rsa_import_priv_key", duk_rsa_import_priv_key, 3},
    {"cert_info", duk_cert_info,1},
    {NULL, NULL, 0}};

duk_ret_t duk_open_module(duk_context *ctx)
{
    OpenSSL_add_all_digests() ;
    duk_push_object(ctx);
    duk_put_function_list(ctx, -1, crypto_funcs);
    return 1;
}