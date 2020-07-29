#include "../core/duktape.h"
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <string.h>
#include <stdio.h>
#include "../../rp.h"

#define OPENSSL_ERR_STRING_MAX_SIZE 1024
#define DUK_OPENSSL_ERROR(ctx)                                                     \
    {                                                                              \
        void *err_buf = duk_push_fixed_buffer(ctx, OPENSSL_ERR_STRING_MAX_SIZE);   \
        ERR_error_string_n(ERR_get_error(), err_buf, OPENSSL_ERR_STRING_MAX_SIZE); \
        duk_error(ctx, DUK_ERR_ERROR, "OpenSSL Error (%d): %s", __LINE__,err_buf);        \
    }

static void crypt(
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
        duk_error(ctx, DUK_ERR_ERROR, "Cipher %s not found", cipher_name);
    
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


static KEYIV pw_to_keyiv(duk_context *ctx, const char *pass, const char *cipher_name, unsigned char *salt_p)
{
    unsigned char salt[PKCS5_SALT_LEN];
    unsigned char keyiv[EVP_MAX_KEY_LENGTH + EVP_MAX_IV_LENGTH];
    KEYIV kiv;
    int klen,ivlen;
    const EVP_CIPHER *cipher=EVP_get_cipherbyname(cipher_name);

    if(!cipher)
        DUK_OPENSSL_ERROR(ctx)

    klen = EVP_CIPHER_key_length(cipher);
    ivlen = EVP_CIPHER_iv_length(cipher);
    
    if(!salt_p)
    {
        if (RAND_bytes(salt, sizeof(salt)) <= 0)
            DUK_OPENSSL_ERROR(ctx)
        salt_p=salt;
    }

    if (!PKCS5_PBKDF2_HMAC(pass, strlen(pass), salt_p, sizeof(salt), 10000, EVP_sha256(), klen+ivlen, keyiv))
        DUK_OPENSSL_ERROR(ctx)
        
    memcpy(kiv.key, keyiv,          klen);
    memcpy(kiv.iv,  keyiv+klen, ivlen);
    memcpy(kiv.salt,salt_p, sizeof(salt));

    return kiv;
}



static duk_ret_t duk_rp_crypt(duk_context *ctx, int decrypt)
{
    duk_size_t in_len;
    void *in_buffer;
    const char *cipher_name = "aes-256-cbc";
    unsigned char *key=NULL, *iv=NULL, salt[PKCS5_SALT_LEN], *salt_p=NULL;
    KEYIV kiv;
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
            duk_error(ctx, DUK_ERR_ERROR, "option 'data' missing from en/decrypt");

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
            
            if(!salt_p && decrypt)
                duk_error(ctx, DUK_ERR_ERROR, "decrypt: ciphertext was not encrypted with a password, use key and iv to decrypt"); 

            kiv=pw_to_keyiv(ctx,pass,cipher_name,salt_p); /* encrypting, salt_p is null */
            key=kiv.key;
            iv=kiv.iv;
            salt_p=kiv.salt;
        }
        else
        {
            duk_get_prop_string(ctx, 0, "key");
            key = (unsigned char *)duk_get_string(ctx, -1);
            duk_pop(ctx);

            duk_get_prop_string(ctx, 0, "iv");
            iv = (unsigned char *)duk_get_string(ctx, -1);
            duk_pop(ctx);
        }


    }
    else if(duk_is_string(ctx,0))
    {
        const char *pass;
        
        if(!duk_is_string(ctx,0))
            duk_error(ctx, DUK_ERR_ERROR, "first argument must be a password or an object with options");

        pass=duk_get_string(ctx,0);

        if( !duk_is_string(ctx,1) && !duk_is_buffer_data(ctx,1))
            duk_error(ctx, DUK_ERR_ERROR, "second argument must be data to en/decrypt (string or buffer)");

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
                duk_error(ctx, DUK_ERR_ERROR, "decrypt: ciphertext was not encrypted with a password, use key and iv to decrypt"); 
        }

        kiv=pw_to_keyiv(ctx,pass,cipher_name,salt_p);
        key=kiv.key;
        iv=kiv.iv;
        salt_p=kiv.salt;
    }
    //printkiv(key,iv,salt_p,EVP_get_cipherbyname(cipher_name));

    crypt ( ctx, key, iv, cipher_name, in_buffer, in_len, salt_p, decrypt);
    
    return 1;
}

/**
 * Does encryption given a cipher, buffer, key, and iv
 * @typedef {Object} EncryptOptions
 * @property {String} pass - the password to generate key and iv
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
 * @property {String} key - the secret key to be used to decrypt
 * @property {String} iv - the initialization vector/nonce
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

/**
 * Macro to make a duktape SHA hash function from a given digest size 
 * and context size 
 * @param {BufferData} the input buffer
 * @returns {Buffer} the message digest
 */
#define DUK_SHA_FUNC(ctx_size, md_size)                                       \
    static duk_ret_t duk_sha##md_size(duk_context *ctx)                       \
    {                                                                         \
        duk_size_t in_len;                                                    \
        void *in;                                                             \
        duk_size_t sz;                                                        \
                                                                              \
        if(duk_is_string(ctx, 0)) duk_to_buffer(ctx, 0, &sz);                 \
        in = duk_require_buffer_data(ctx, 0, &in_len);                        \
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
/* declare all supported ctx_size, md_size */
DUK_SHA_FUNC(256, 224);
DUK_SHA_FUNC(256, 256);
DUK_SHA_FUNC(512, 384);
DUK_SHA_FUNC(512, 512);

/**
 * MD5 Hash function binding
 * @param {BufferData} the input buffer 
 * @returns {Buffer} the message digest
 */
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

/**
 * Uses RAND_bytes to fill a buffer with random data.
 * @param {uint} the output length of the buffer to be returned
 * @returns {Buffer} the buffer filled with random data
 */
static duk_ret_t duk_rand(duk_context *ctx)
{
    duk_size_t len = duk_require_uint(ctx, -1);
    void *buffer = duk_push_fixed_buffer(ctx, len);
    /* RAND_bytes may return 0 or -1 on error */
    if (RAND_bytes(buffer, len) != 1)
        DUK_OPENSSL_ERROR(ctx);
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
    duk_require_object(ctx, -1);
    duk_idx_t options_idx = duk_normalize_index(ctx, -1);
    duk_get_prop_string(ctx, options_idx, "bytes");
    duk_uint_t bytes = duk_get_uint_default(ctx, -1, 32);
    duk_get_prop_string(ctx, options_idx, "file");
    const char *file = duk_get_string_default(ctx, -1, "/dev/random");
    int rc = RAND_load_file(file, bytes);
    if (rc != bytes)
        DUK_OPENSSL_ERROR(ctx);
    return 0;
}
const duk_function_list_entry crypto_funcs[] = {
    {"encrypt", duk_encrypt, 2},
    {"decrypt", duk_decrypt, 2},
    {"sha224", duk_sha224, 2},
    {"sha256", duk_sha256, 2},
    {"sha384", duk_sha384, 2},
    {"sha512", duk_sha512, 2},
    {"md5", duk_md5, 2},
    {"rand", duk_rand, 1},
    {"seed", duk_seed_rand, 1},
    {NULL, NULL, 0}};

duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_object(ctx);
    duk_put_function_list(ctx, -1, crypto_funcs);
    return 1;
}
