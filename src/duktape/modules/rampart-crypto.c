/* Copyright (C) 2026  Aaron Flin - All Rights Reserved
   Copyright (C) 2026  Benjamin Flin - All Rights Reserved
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

char *rp_crypto_do_passwd(int passed_salt, char **salt_p, char **salt_malloc_p,
                     char *passwd,
                     size_t pw_maxlen, int mode);

/*
rampart> crypto.passwd('hello','mysalt','sha512')
{
   "line": "$6$mysalt$HjkH9tPwoOZC7.Tbbf.865I0VP2JrcvX25YLWcUkIkNvMWhU/minCmQlwt98agkOaRtd2xgXkljSlU1AN7Lr/0",
   "salt": "mysalt",
   "hash": "HjkH9tPwoOZC7.Tbbf.865I0VP2JrcvX25YLWcUkIkNvMWhU/minCmQlwt98agkOaRtd2xgXkljSlU1AN7Lr/0",
   "mode": "sha512"
}
rampart> crypto.passwd('hello','mysalt','sha256')
{
   "line": "$5$mysalt$njl.kLzQo5JAjJgLM8UhuINnLhZQslCv5IeR4hpzccC",
   "salt": "mysalt",
   "hash": "njl.kLzQo5JAjJgLM8UhuINnLhZQslCv5IeR4hpzccC",
   "mode": "sha256"
}
rampart> crypto.passwd('hello','mysalt','md5')
{
   "line": "$1$mysalt$wjVpLe2hQU6gA4ia4fa5J0",
   "salt": "mysalt",
   "hash": "wjVpLe2hQU6gA4ia4fa5J0",
   "mode": "md5"
}
rampart> crypto.passwd('hello','mysalt','apr1')
{
   "line": "$apr1$mysalt$VoNgA1quatjo89.CbYC7r/",
   "salt": "mysalt",
   "hash": "VoNgA1quatjo89.CbYC7r/",
   "mode": "apr1"
}
rampart> crypto.passwd('hello','mysalt','aixmd5')
{
   "line": "mysalt$w/XTjiQKfx7/FjLQe3Mc1/",
   "salt": "mysalt",
   "hash": "w/XTjiQKfx7/FjLQe3Mc1/",
   "mode": "aixmd5"
}
rampart> crypto.passwd('hello','mysalt','crypt')
{
   "line": "myou.60xjITpM",
   "salt": "my",
   "hash": "ou.60xjITpM",
   "mode": "crypt"
}
*/
#define RP_PW_TYPE_SHA512    0
#define RP_PW_TYPE_SHA256    1
#define RP_PW_TYPE_MD5       2
#define RP_PW_TYPE_APR1      3
#define RP_PW_TYPE_AIXMD5    4
#define RP_PW_TYPE_CRYPT     5

static int passwd_parse_line(const char *line, const char **salt, duk_size_t *salt_sz, const char **hash)
{
    const char *s=NULL;

    if(!line || !salt || ! salt_sz || !hash)
        return -1;

    if(*line == '$')
    {
        //sha512, sha256, md5 and apr1
        char t=line[1];
        if(t=='6'||t=='5'||t=='1'|| !strncmp(line,"$apr1$",6))
        {
            line=strchr(&line[2],'$');
            if(!line)
                return -1;
            line++;

            *salt=line;

            if( !(s=strchr(line,'$')) )
                return -1;
            *salt_sz = (duk_size_t)(s-line);

            *hash=s+1;

            switch(t) {
                case '6': return RP_PW_TYPE_SHA512;
                case '5': return RP_PW_TYPE_SHA256;
                case '1': return RP_PW_TYPE_MD5;
                case 'a': return RP_PW_TYPE_APR1;
                default:  return -1;
            }
        }
    }
    else if ( (s=strchr(line,'$')) )
    {
        //aixmd5
        if (strchr(s+1,'$'))
            return -1;
        *salt=line;
        *salt_sz = (duk_size_t)(s-line);
        *hash=s+1;
        return RP_PW_TYPE_AIXMD5;
    }
    else
    {
        //plain crypt
        *salt=line;
        *salt_sz=2;
        *hash=line+2;
        return RP_PW_TYPE_CRYPT;
    }
    return -1;
} 

static duk_ret_t passwd_components(duk_context *ctx)
{
    const char *sa=NULL, *ha=NULL, *mode=NULL,
               *line = REQUIRE_STRING(ctx, 0, "passwdComponents - parameter must be a String (encoded salt/password line)");
    duk_size_t sz=0, saltlen=0;

    int ret = passwd_parse_line(line, &sa, &sz, &ha);

    switch(ret) {
        case RP_PW_TYPE_SHA512 :
            mode="sha512"; saltlen=16;break;
        case RP_PW_TYPE_SHA256 :
            mode="sha256"; saltlen=16;break;
        case RP_PW_TYPE_MD5    :
            mode="md5";    saltlen=8; break;
        case RP_PW_TYPE_APR1   :
            mode="apr1";   saltlen=8; break;
        case RP_PW_TYPE_AIXMD5 :
            mode="aixmd5";saltlen=8; break;
        case RP_PW_TYPE_CRYPT  :
            mode="crypt";  saltlen=2; break;
        default:
            RP_THROW(ctx, "passwdComponents - error parsing line");
    }

    duk_push_object(ctx);

    duk_push_string(ctx, line);
    duk_put_prop_string(ctx, -2, "line");

    if(sz > saltlen)
        sz=saltlen;
    duk_push_lstring(ctx, sa, sz); 
    duk_put_prop_string(ctx, -2, "salt");

    duk_push_string(ctx, ha);
    duk_put_prop_string(ctx, -2, "hash");

    duk_push_string(ctx, mode);
    duk_put_prop_string(ctx, -2, "mode");

    return 1;
}

static duk_ret_t do_passwd(duk_context *ctx)
{
    const char *passwd = REQUIRE_STRING(ctx, 0, "crypto.passwd - first argument must be a string (password)");
    const char *salt = NULL;
    const char *type = "sha512";
    char *salt_malloc=NULL;
    size_t pw_maxlen=255;
    int passed_salt=0;
    int saltlen;
    int mode = crypto_passwd_sha512;
    char *hash=NULL, *s;

    if(!duk_is_undefined(ctx,1) && !duk_is_null(ctx,1))
    {
        salt = REQUIRE_STRING(ctx, 1, "crypto.passwd - second argument, if defined and not null, must be a string (salt)");
        passed_salt=1;
    }

    if(!duk_is_undefined(ctx,2))
    {
        type = REQUIRE_STRING(ctx, 2, "crypto.passwd - third argument, if defined, must be a string (hash mode)");

        if(!strcmp(type,"sha512"))
            mode=crypto_passwd_sha512;
        else if(!strcmp(type,"sha256"))
            mode=crypto_passwd_sha256;
        else if(!strcmp(type,"md5"))
            mode=crypto_passwd_md5;
        else if(!strcmp(type,"apr1"))
            mode=crypto_passwd_apr1;
        else if(!strcmp(type,"aixmd5"))
            mode=crypto_passwd_aixmd5;
        else if(!strcmp(type,"crypt"))
            mode=crypto_passwd_crypt;
        else
            RP_THROW(ctx, "crypto.passwd - mode '%s' is not known", type);
    }

    if (mode == crypto_passwd_crypt)
    {
        saltlen = 2;
        if(passed_salt && strlen(salt) < 2)
            RP_THROW(ctx, "crypto.passwd - Salt for mode 'crypt' must be 2 characters");
    }
    else if (mode == crypto_passwd_md5 || mode == crypto_passwd_apr1 || mode == crypto_passwd_aixmd5)
        saltlen = 8;
    else if (mode == crypto_passwd_sha256 || mode == crypto_passwd_sha512)
        saltlen = 16;


    hash = rp_crypto_do_passwd(passed_salt, (char**)&salt, &salt_malloc, (char*) passwd, pw_maxlen, mode);

    if(!hash)
        RP_THROW(ctx, "passwd hash creation failed");

    duk_push_object(ctx);

    duk_push_string(ctx, hash);
    duk_put_prop_string(ctx, -2, "line");
    if(passed_salt)
    {
        int l = strlen(salt);
        if(l > saltlen)
            l=saltlen;
        duk_push_lstring(ctx,salt,(duk_size_t)l);
    }
    else
    {
        duk_push_string(ctx, salt_malloc);
        free(salt_malloc);
    } 
    duk_put_prop_string(ctx, -2, "salt");
    s = strrchr(hash,'$');
    if(!s) //passwd_crypt
        s=hash+2;
    else
        s++;
    duk_push_string(ctx, s);
    duk_put_prop_string(ctx, -2, "hash");
    duk_push_string(ctx, type);
    duk_put_prop_string(ctx, -2, "mode");

    free(hash);
    return 1;
}


static duk_ret_t check_passwd(duk_context *ctx)
{
    const char *sa=NULL, *ha=NULL,
               *line = REQUIRE_STRING(ctx, 0, "passwdCheck - first parameter must be a String (encoded salt/password line)"),
               *passwd = REQUIRE_STRING(ctx, 1, "passwdCheck - first parameter must be a String (password)");
    char *s=NULL, *freesa=NULL, *hash=NULL;
    duk_size_t sz=0, saltlen=0;
    int mode = crypto_passwd_sha512;

    int ret = passwd_parse_line(line, &sa, &sz, &ha);

    switch(ret) {
        case RP_PW_TYPE_SHA512 :
            mode=crypto_passwd_sha512; saltlen=16;break;
        case RP_PW_TYPE_SHA256 :
            mode=crypto_passwd_sha256; saltlen=16;break;
        case RP_PW_TYPE_MD5    :
            mode=crypto_passwd_md5;    saltlen=8; break;
        case RP_PW_TYPE_APR1   :
            mode=crypto_passwd_apr1;   saltlen=8; break;
        case RP_PW_TYPE_AIXMD5 :
            mode=crypto_passwd_aixmd5; saltlen=8; break;
        case RP_PW_TYPE_CRYPT  :
            mode=crypto_passwd_crypt;  saltlen=2; break;
        default:
            RP_THROW(ctx, "passwdCheck - error parsing line");
    }

    if(sz>saltlen)
        sz=saltlen;
    freesa = strndup(sa,sz);

    hash = rp_crypto_do_passwd(1, &freesa, NULL, (char*) passwd, 255, mode);

    s = strrchr(hash,'$');
    if(!s) //passwd_crypt
        s=hash+2;
    else
        s++;

    if(freesa)
        free(freesa);

    if(!strcmp(s,ha))
        duk_push_true(ctx);
    else
        duk_push_false(ctx);

    return 1;
}


/* make sure when we use RAND_ functions, we've seeded at least once */
static int seeded=0;
static void checkseed(duk_context *ctx)
{
    if(!seeded)
    {
        int rc = RAND_load_file("/dev/urandom", 32);
        if (rc != 32)
            DUK_OPENSSL_ERROR(ctx);
        seeded=1;
    }
}
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

        if (!EVP_DecryptUpdate(cipher_ctx, out_buffer, &current_len, in_buffer, (int)in_len))
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

        if (!EVP_EncryptUpdate(cipher_ctx, out_buffer + out_len, &current_len, in_buffer, (int)in_len))
        {
            DUK_OPENSSL_ERROR(ctx);
        }
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

//from rampart-utils.c
void duk_rp_hexToBuf(duk_context *ctx, duk_idx_t idx);
void duk_rp_toHex(duk_context *ctx, duk_idx_t idx, int ucase);

/* produce a hash from a password using pbkdf2 */
duk_ret_t duk_rp_pass_to_keyiv(duk_context *ctx)
{
    unsigned char salt[PKCS5_SALT_LEN];
    unsigned char *salt_p=NULL;
    const char *pass=NULL;
    KEYIV kiv;
    int iter=10000;
    int klen,ivlen, retbuf=0;
    const char *cipher_name = "aes-256-cbc";
    void *buf;
    const EVP_CIPHER *cipher;

    REQUIRE_OBJECT(ctx, 0, "passToKeyIv requires an object of options as its argument");
    
    if(duk_get_prop_string(ctx, 0, "password"))
        pass = REQUIRE_STRING(ctx, -1, "option 'password' must be a string");
    duk_pop(ctx);

    if(!pass)
    {
        if(duk_get_prop_string(ctx, 0, "pass"))
            pass = REQUIRE_STRING(ctx, -1, "option 'password' must be a string");
        duk_pop(ctx);
    }

    if(!pass)
        RP_THROW(ctx, "passToKeyIv requires a password");

    if(duk_get_prop_string(ctx, 0, "iter"))
    {
        iter=(int)REQUIRE_NUMBER(ctx,-1,"passToKeyIv: option 'iter' requires a Number");
    }
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, 0, "cipher") )
    {
        cipher_name = REQUIRE_STRING(ctx, -1, "passToKeyIv: option 'cipher' must be a String");
    }
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, 0, "returnBuffer") )
    {
        retbuf = REQUIRE_BOOL(ctx, -1, "passToKeyIv: option 'returnBuffer' must be a Boolean");
    }
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, 0, "salt"))
    {
        void *b;
        if(duk_is_string(ctx, -1))
        {
            duk_rp_hexToBuf(ctx, -1);
            duk_remove(ctx, -2);
        }

        if (duk_is_buffer_data(ctx, -1))
        {
            if(duk_get_length(ctx, -1) < PKCS5_SALT_LEN)
                RP_THROW(ctx, "passToKeyIv: option 'salt' must be at least %d bytes", PKCS5_SALT_LEN);
        }
        else
            RP_THROW(ctx, "passToKeyIv: option 'salt' must be a buffer (8 bytes) or a string (8 bytes in hex)");

        b = duk_get_buffer_data(ctx, -1, NULL);
        memcpy(salt, b, PKCS5_SALT_LEN);
        salt_p=salt;
    }
    duk_pop(ctx);

    kiv=pw_to_keyiv(ctx,pass,cipher_name,salt_p,iter);

    cipher = EVP_get_cipherbyname(cipher_name);
    klen   = EVP_CIPHER_key_length(cipher);
    ivlen  = EVP_CIPHER_iv_length(cipher);

    duk_push_object(ctx);

    buf = duk_push_fixed_buffer(ctx, (duk_size_t)klen);
    memcpy(buf, kiv.key, klen);
    if(!retbuf)
        duk_rp_toHex(ctx, -1, 0);
    duk_put_prop_string(ctx, -2, "key");

    buf = duk_push_fixed_buffer(ctx, (duk_size_t)ivlen);
    memcpy(buf, kiv.iv, ivlen);
    if(!retbuf)
        duk_rp_toHex(ctx, -1, 0);
    duk_put_prop_string(ctx, -2, "iv");

    buf = duk_push_fixed_buffer(ctx, (duk_size_t)PKCS5_SALT_LEN);
    memcpy(buf, kiv.salt, PKCS5_SALT_LEN);
    if(!retbuf)
        duk_rp_toHex(ctx, -1, 0);
    duk_put_prop_string(ctx, -2, "salt");

    return 1;
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
            cipher_name = REQUIRE_STRING(ctx, -1, "option 'cipher' must be a string");
        }
        duk_pop(ctx);

        if(!duk_get_prop_string(ctx, 0, "data"))
            RP_THROW(ctx, "option 'data' missing from en/decrypt");

        in_buffer = (void*)REQUIRE_STR_OR_BUF(ctx, -1, &in_len, "crypto.en/decrypt - 'data' must be a Buffer or String");
        duk_pop(ctx);

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
                (void)duk_error(ctx, DUK_ERR_ERROR, "decrypt: ciphertext was not encrypted with a password, use key and iv to decrypt");

            if(duk_get_prop_string(ctx, 0, "iter"))
            {
                iter=(int)REQUIRE_NUMBER(ctx,-1,"crypto.[en|de]crypt option iter requires a number");
            }
            duk_pop(ctx);

            kiv=pw_to_keyiv(ctx,pass,cipher_name,salt_p,iter); /* encrypting: salt_p is null, decrypting: must be set */
            key=kiv.key;
            iv=kiv.iv;
            salt_p=kiv.salt;
        }
        else
        {
            int klen, ivlen;
            const EVP_CIPHER *cipher;

            cipher = EVP_get_cipherbyname(cipher_name);
            if (cipher == NULL)
                RP_THROW(ctx, "Cipher %s not found", cipher_name);
            klen   = EVP_CIPHER_key_length(cipher);
            ivlen  = EVP_CIPHER_iv_length(cipher);

            if (duk_get_prop_string(ctx, 0, "key"))
            {
                if(duk_is_string(ctx, -1))
                {
                    duk_rp_hexToBuf(ctx, -1);
                    duk_remove(ctx, -2);
                    duk_dup(ctx, -1); //one gets popped, We need to have at least one on the stack
                    duk_insert(ctx,1);
                }

                if (!duk_is_buffer_data(ctx, -1) || duk_get_length(ctx, -1) != klen)
                    RP_THROW(ctx, "crypto.[en|de]crypt: option 'key' must be a buffer (%d bytes) or a string (%d bytes in hex)", klen, klen);

                key = (unsigned char *)duk_get_buffer_data(ctx, -1, NULL);
            }
            duk_pop(ctx);

            if (duk_get_prop_string(ctx, 0, "iv"))
            {
                if(duk_is_string(ctx, -1))
                {
                    duk_rp_hexToBuf(ctx, -1);
                    duk_remove(ctx, -2);
                    duk_dup(ctx, -1); //one gets popped, We need to have at least one on the stack
                    duk_insert(ctx,1);
                }

                if (!duk_is_buffer_data(ctx, -1) || duk_get_length(ctx, -1) != ivlen)
                    RP_THROW(ctx, "crypto.[en|de]crypt: option 'iv' must be a buffer (%d bytes) or a string (%d bytes in hex)", ivlen, ivlen);

                iv = (unsigned char *)duk_get_buffer_data(ctx, -1, NULL);
            }
            duk_pop(ctx);
        }
        duk_pop(ctx);
    }
    else
    {
        const char *pass;

        pass=REQUIRE_STRING(ctx,0, "first argument must be a password or an object with options");

        in_buffer = (void *) REQUIRE_STR_OR_BUF(ctx, 1, &in_len, "crypto.en/decrypt - second argument must be data to en/decrypt (string or buffer)");

        if( !duk_is_undefined(ctx, 2))
            cipher_name=REQUIRE_STRING(ctx, 2, "crypto.en/decrypt - optional third argument must be a string (cipher name)");


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
    duk_size_t len = REQUIRE_POSINT(ctx, -1, "crypto.rand requires a positive integer");
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
        sigma = REQUIRE_NUMBER(ctx, 0, "crypto.gaussrand requires a number (sigma) as it's argument");

    duk_push_number(ctx, gaussrand(ctx, sigma));

    return 1;
}

static duk_ret_t duk_normrand(duk_context *ctx)
{
    double scale = 1.0;

    if(!duk_is_undefined(ctx, 0))
        scale = REQUIRE_NUMBER(ctx, 0, "crypto.normrand requires a number (scale) as it's argument");

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
            bytes = REQUIRE_POSINT(ctx, -1, "crypto.seed - \"bytes\" requires a positive integer (number of bytes)");
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

duk_ret_t duk_rp_bigint_tostring(duk_context *ctx);
duk_ret_t duk_rp_bigint_tosignedstring(duk_context *ctx);

#define get_bn(ctx, bnp, idx) do {\
    if(!duk_get_prop_string(ctx, idx, DUK_HIDDEN_SYMBOL("bn")))\
        RP_THROW(ctx, "bigint: argument #%d is not a BigInt",(int)idx+1);\
    bnp = duk_get_pointer(ctx, -1);\
    duk_pop(ctx);\
} while (0)

#define get_bn_or_i(ctx, bnp, idx) ({\
    int64_t r=0;\
    if(duk_is_number(ctx, idx))\
        r=(int64_t)duk_get_int(ctx, idx);\
    else if(duk_get_prop_string(ctx, idx, DUK_HIDDEN_SYMBOL("bn")))\
        bnp = duk_get_pointer(ctx, -1);\
    else\
        RP_THROW(ctx, "bigint: argument #%d is not a BigInt",(int)idx+1);\
    duk_pop(ctx);\
    r;\
})

duk_ret_t duk_rp_bigint_finalizer(duk_context *ctx)
{
    BIGNUM *bn;

    if(duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("bn")))
    {
        bn=duk_get_pointer(ctx, -1);
        BN_free(bn);
    }
    duk_pop(ctx);
    return 0;
}

static const char * hex2binmap[] = {
    "0000", "0001", "0010", "0011",
    "0100", "0101", "0110", "0111",
    "1000", "1001", "1010", "1011",
    "1100", "1101", "1110", "1111"
};

static char *hextobin(char *hex)
{
    size_t len, outsz, begsz;
    char *ret=NULL, *out, *in;
    int firstbit=0;

    if(!hex)
        return NULL;

    len = strlen(hex);

    if( len > 2 && !strncasecmp("0x", hex, 2) )
        begsz = 2;
    else if ( len > 3 && !strncasecmp("-0x", hex, 3) )
        begsz = 3;
    else if(*hex=='-')
        begsz = 1;
    else
        begsz=0;

    outsz = (len-begsz)*4 + 1 +begsz;

    REMALLOC(ret, outsz);
    
    out=ret;
    in=hex;

    if(begsz)
    {
        if(begsz == 3 || begsz == 1)
            *out++ = *in++;
        if(begsz > 1)
        {
            *out++='0';
            *out++='b';
            in+=2;
        }
    }

    if(hex[begsz]=='0' && hex[begsz+1]=='\0')
    {
        *out++='0';
        *out++='\0';
        return ret;
    }

    while(*in)
    {
        int c,i;

        if(*in < 58 ) c= *in - 48;
        else if (*in < 71) c = *in - 55;
        else c = *in - 87;
        
        if(c < 0 || c > 15)
        {
            free(ret);
            return NULL;
        }

        if(!firstbit)//strip leading 0s
        {
            for(i=0;i<4;i++)
            {
                if(firstbit || hex2binmap[c][i]=='1')
                {
                    *out++=hex2binmap[c][i];
                    firstbit=1;
                }
            }
        }
        else
        {
            for(i=0;i<4;i++)
                *out++=hex2binmap[c][i];
        }
        in++;
    }    
    *out='\0';

    return ret;
}

static void push_bn(duk_context *ctx, BIGNUM *bn)
{
    duk_push_object(ctx);
    duk_push_pointer(ctx, bn);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("bn"));
    duk_push_c_function(ctx, duk_rp_bigint_finalizer, 1);
    duk_set_finalizer(ctx, -2);
    duk_push_c_function(ctx, duk_rp_bigint_tostring, 1);
    duk_put_prop_string(ctx, -2, "toString");
    duk_push_c_function(ctx, duk_rp_bigint_tosignedstring, 1);
    duk_put_prop_string(ctx, -2, "toSignedString");
}

char * bintohex(char *bin)
{
    size_t len, outsz, begsz, i;
    char *ret=NULL;
    char *s;
    int bit, val=0;

    if(!bin)
        return NULL;

    len = strlen(bin);

    if( len > 2 && !strncasecmp("0b", bin, 2) )
        begsz = 2;
    else if ( len > 3 && !strncasecmp("-0b", bin, 3) )
        begsz = 3;
    else
        return NULL;

    s=&bin[len-1];

    len -= begsz;

    outsz = (len-1)/4 + begsz + 2;

    if(!( (outsz-begsz) % 2 ))
        outsz++;

    REMALLOC(ret, outsz);

    i=0;
    val=0;
    outsz--;
    ret[outsz--]='\0';

    while (len--) 
    {
        bit = i%4;

        if( i && !bit )
        {
            ret[outsz--]=(char)( ( (val>9) ? 87 : 48 ) + val);
            val=0;
        }

        if(*s == '1')
            val |= 1 << bit;
        else if(*s != '0')
        {
            free(ret);
            return NULL;
        }
        s--;
        i++;
    }

    if( i )
        ret[outsz--]=(char)( ( (val>9) ? 87 : 48 ) + val);

    if(outsz != begsz-1)
        ret[outsz--]='0';

    ret[outsz--]='X';
    ret[outsz--]='0';

    if( begsz == 3 )
        ret[outsz--]='-';

    return ret;
}

static inline BIGNUM * new_bn(duk_context *ctx, const char *cnum, int make_object)
{
    BIGNUM *bn=BN_new();
    char *num = (char *)cnum;
    if(num)
    {
        int nchar=0, len;
        char *s=num;

        if(*s=='-') s++;

        if( *s=='0' && (s[1]=='b'||s[1]=='B'||s[1]=='x'||s[1]=='X') )
        {
            char *freeme = NULL;

            if(s[1]=='b' || s[1]=='B' )
            {
                num = bintohex(num);
                freeme=num;
            }

            if(!num)
            {
                BN_free(bn);
                RP_SYNTAX_THROW(ctx, "bigint: invalid value");
            }

            if(*num == '-'){
                if(!freeme) //num is currently a const char
                {
                    freeme = strdup(num);
                    num = freeme;
                }

                num+=2;
                *num = '-';
            }
            else
                num+=2;

            if(!(nchar=BN_hex2bn(&bn, num)))
            {
                if(freeme)
                    free(freeme);
                BN_free(bn);
                RP_SYNTAX_THROW(ctx, "bigint: invalid value");
            }
            len = strlen(num);

            if(freeme)
                free(freeme);
        }
        else
        {
            if(!(nchar=BN_dec2bn(&bn, num)))
            {
                BN_free(bn);
                RP_SYNTAX_THROW(ctx, "bigint: invalid value");
            }
            len = strlen(num);
        }

        if(len != nchar)
        {
            BN_free(bn);
            RP_SYNTAX_THROW(ctx, "bigint: invalid value");
        }
    }

    if(make_object)
        push_bn(ctx, bn);

    return bn;
}

duk_ret_t _bigint(duk_context *ctx)
{
    if(duk_is_number(ctx,0))
    {
        double numval = duk_get_number(ctx, 0);
        duk_push_sprintf(ctx, "%.0f",numval);
        duk_replace(ctx, 0);
    }

    if(duk_is_string(ctx, 0))
    {
        duk_trim(ctx, 0);
        (void)new_bn(ctx, duk_get_string(ctx, 0),1);
    }
    else
        goto bnerr;

    return 1;

    bnerr:
    RP_SYNTAX_THROW(ctx, "bigint: invalid value");
    return 0;
}

#define BNOP_ADD 0
#define BNOP_SUB 1
#define BNOP_MUL 2
#define BNOP_DIV 3
#define BNOP_MOD 4
#define BNOP_EXP 5
#define BNOP_NEG 6
static duk_ret_t duk_rp_bigint_op(duk_context *ctx, int op)
{
    BIGNUM *bna, *bnb=NULL, *bnr;

    get_bn(ctx, bna, 0);
    if(duk_get_top(ctx)>1)
        get_bn(ctx, bnb, 1);

    bnr = new_bn(ctx,NULL,1);
    switch(op)
    {
        case BNOP_ADD:
            BN_add(bnr, bna, bnb);
            break;
        case BNOP_SUB:
            BN_sub(bnr, bna, bnb);
            break;        
        case BNOP_MUL:
        {
            BN_CTX *tmp=BN_CTX_new();
            BN_mul(bnr, bna, bnb, tmp);
            BN_CTX_free(tmp);
            break;        
        }
        case BNOP_DIV:
        {
            BN_CTX *tmp=BN_CTX_new();
            BN_div(bnr, NULL, bna, bnb, tmp);
            BN_CTX_free(tmp);
            break;        
        }
        case BNOP_MOD:
        {
            BN_CTX *tmp=BN_CTX_new();
            BN_div(NULL, bnr, bna, bnb, tmp);
            BN_CTX_free(tmp);
            break;        
        }
        case BNOP_EXP:
        {
            BN_CTX *tmp=BN_CTX_new();
            BN_exp(bnr, bna, bnb, tmp);
            BN_CTX_free(tmp);
            break;        
        }
        case BNOP_NEG:
        {
            BN_CTX *tmp=BN_CTX_new();
            bnb = BN_new();
            BN_dec2bn(&bnb, "-1"); 
            BN_mul(bnr, bna, bnb, tmp);
            BN_CTX_free(tmp);
            BN_free(bnb);
            break;        
        }
    }
    return 1;
}

duk_ret_t duk_rp_bigint_add(duk_context *ctx)
{
    return duk_rp_bigint_op(ctx, BNOP_ADD);
}
duk_ret_t duk_rp_bigint_sub(duk_context *ctx)
{
    return duk_rp_bigint_op(ctx, BNOP_SUB);
}
duk_ret_t duk_rp_bigint_mul(duk_context *ctx)
{
    return duk_rp_bigint_op(ctx, BNOP_MUL);
}
duk_ret_t duk_rp_bigint_div(duk_context *ctx)
{
    return duk_rp_bigint_op(ctx, BNOP_DIV);
}
duk_ret_t duk_rp_bigint_mod(duk_context *ctx)
{
    return duk_rp_bigint_op(ctx, BNOP_MOD);
}
duk_ret_t duk_rp_bigint_exp(duk_context *ctx)
{
    return duk_rp_bigint_op(ctx, BNOP_EXP);
}
duk_ret_t duk_rp_bigint_neg(duk_context *ctx)
{
    return duk_rp_bigint_op(ctx, BNOP_NEG);
}

#define BNCMP_EQL 0
#define BNCMP_NEQ 1
#define BNCMP_LT  2
#define BNCMP_LTE 3
#define BNCMP_GT  4
#define BNCMP_GTE 5

static duk_ret_t duk_rp_bigint_cmp(duk_context *ctx, int cmp)
{
    BIGNUM *bna, *bnb;
    int res;

    get_bn(ctx, bna, 0);
    get_bn(ctx, bnb, 1);

    res = BN_cmp(bna, bnb);

    switch (cmp)
    {
        case BNCMP_EQL:
            if(res)
                duk_push_false(ctx);
            else
                duk_push_true(ctx);
            break;
        case BNCMP_NEQ:
            if(!res)
                duk_push_false(ctx);
            else
                duk_push_true(ctx);
            break;
        case BNCMP_LT:
            if(res>-1)
                duk_push_false(ctx);
            else
                duk_push_true(ctx);
            break;
        case BNCMP_LTE:
            if(res>0)
                duk_push_false(ctx);
            else
                duk_push_true(ctx);
            break;
        case BNCMP_GT:
            if(res<1)
                duk_push_false(ctx);
            else
                duk_push_true(ctx);
            break;
        case BNCMP_GTE:
            if(res<0)
                duk_push_false(ctx);
            else
                duk_push_true(ctx);
            break;
    }
    return 1;            
}

duk_ret_t duk_rp_bigint_eql(duk_context *ctx)
{
    return duk_rp_bigint_cmp(ctx, BNCMP_EQL);
}
duk_ret_t duk_rp_bigint_neq(duk_context *ctx)
{
    return duk_rp_bigint_cmp(ctx, BNCMP_NEQ);
}
duk_ret_t duk_rp_bigint_lt(duk_context *ctx)
{
    return duk_rp_bigint_cmp(ctx, BNCMP_LT);
}
duk_ret_t duk_rp_bigint_lte(duk_context *ctx)
{
    return duk_rp_bigint_cmp(ctx, BNCMP_LTE);
}
duk_ret_t duk_rp_bigint_gt(duk_context *ctx)
{
    return duk_rp_bigint_cmp(ctx, BNCMP_GT);
}
duk_ret_t duk_rp_bigint_gte(duk_context *ctx)
{
    return duk_rp_bigint_cmp(ctx, BNCMP_GTE);
}

//returns 1 if coerced to bigint and bigint replaces whatever is at idx
//returns 0 if no coercion possible and stack remains unchanged
static int bigint_coerce(duk_context *ctx, duk_idx_t idx)
{
    if( duk_is_object(ctx, idx) )
    { 
        if (duk_has_prop_string(ctx, idx, DUK_HIDDEN_SYMBOL("bn")) )
        {
            return 1;
        }
        return 0;
    }

    if(duk_is_number(ctx, idx))
    {
        double numval = duk_get_number(ctx, idx);
        duk_push_sprintf(ctx, "%.0f",numval);
        duk_replace(ctx, idx);
    }

    if(duk_is_string(ctx, idx))
    {
        duk_trim(ctx, idx);
        new_bn(ctx, duk_get_string(ctx, idx), 1);
        duk_replace(ctx, idx);
        return 1;
    }

    return 0;
}

#define DOCOERCE \
if(!bigint_coerce(ctx, 0))\
{\
    duk_push_false(ctx);\
    return 1;\
}\
if(!bigint_coerce(ctx, 1))\
{\
    duk_push_false(ctx);\
    return 1;\
}


duk_ret_t duk_rp_bigint_Eql(duk_context *ctx)
{
    DOCOERCE
    return duk_rp_bigint_cmp(ctx, BNCMP_EQL);
}
duk_ret_t duk_rp_bigint_Neq(duk_context *ctx)
{
    DOCOERCE
    return duk_rp_bigint_cmp(ctx, BNCMP_NEQ);
}
duk_ret_t duk_rp_bigint_Lt(duk_context *ctx)
{
    DOCOERCE
    return duk_rp_bigint_cmp(ctx, BNCMP_LT);
}
duk_ret_t duk_rp_bigint_Lte(duk_context *ctx)
{
    DOCOERCE
    return duk_rp_bigint_cmp(ctx, BNCMP_LTE);
}
duk_ret_t duk_rp_bigint_Gt(duk_context *ctx)
{
    DOCOERCE
    return duk_rp_bigint_cmp(ctx, BNCMP_GT);
}
duk_ret_t duk_rp_bigint_Gte(duk_context *ctx)
{
    DOCOERCE
    return duk_rp_bigint_cmp(ctx, BNCMP_GTE);
}

duk_ret_t duk_rp_bigint_Add(duk_context *ctx)
{
    int aisbi=0, bisbi=0;
    duk_idx_t bi_idx=0;

    if(duk_is_object(ctx, 0) && duk_has_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("bn")))
        aisbi=1;
    if(duk_is_object(ctx, 1) && duk_has_prop_string(ctx, 1, DUK_HIDDEN_SYMBOL("bn")))
        bisbi=1;
    
    if (aisbi && bisbi)
        return duk_rp_bigint_add(ctx);
    else if (aisbi || bisbi)
    {
        if(bisbi)
            bi_idx=1;
        duk_push_string(ctx, "toString");
        duk_call_prop(ctx, bi_idx, 0);
        duk_replace(ctx, bi_idx);
    }        
    duk_concat(ctx, 2);
    return 1;
}


static duk_ret_t doshift(duk_context *ctx, BIGNUM *bn, int64_t nshift)
{
    BIGNUM *bnr = BN_dup(bn),
           *bn_neg1,
           *bn_zero;
    int bncmp;
    int left = (nshift>0);

    duk_push_this(ctx);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_zero"));
    get_bn(ctx, bn_zero, -1);
    duk_pop(ctx);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_neg1"));
    get_bn(ctx, bn_neg1, -1);
    duk_pop_2(ctx);

    bncmp = BN_cmp(bn, bn_zero);

    if(!left)
        nshift *= -1;

    if(!bncmp)
    {
        push_bn(ctx, bnr);
        return 1;
    }
    
    
    if (bncmp < 0)
    {
        /* shift must be done on positive number */
        BN_sub(bnr, bn_zero, bnr);
    }

    if(left)
        BN_lshift(bnr, bnr, (int)nshift); 
    else
        BN_rshift(bnr, bnr, (int)nshift);

    if(bncmp < 0)
    {
        if(BN_is_zero(bnr)) //if was negative and now shifted to zero bnr should be -1
        {
            BN_free(bnr);
            bnr=bn_neg1;// use existing -1
        }
        else
        {
            /* undo negation and subtract 1*/
            BN_sub(bnr, bn_neg1, bnr);
        }
    }

    push_bn(ctx, bnr);
    return 1;
}

static duk_ret_t duk_rp_bigint_shift(duk_context *ctx, int left)
{
    BIGNUM *bna,
           *bnb=NULL;
    int64_t nshift = get_bn_or_i(ctx, bnb, 1);
    get_bn(ctx, bna, 0);

    if(bnb)
    {
        char *num = BN_bn2dec(bnb);
        errno=0;
        nshift = strtoll(num, NULL, 10);
        OPENSSL_free(num);
        if(errno)
            RP_THROW(ctx, "bigint: range error");
    }

    if(!left) nshift*=-1;

    //if(nshift > 1073741815) //this is the limit in node's JSBI
    if(nshift >    536870775)  //openssl bignum is about half that
        RP_THROW(ctx, "bigint: range error");

    if(nshift)
        return doshift(ctx, bna, nshift);
    else
    {
        BIGNUM *bnr = BN_dup(bna);

        push_bn(ctx, bnr);
    }    

    return 1;
}

duk_ret_t duk_rp_bigint_sl(duk_context *ctx)
{
    return duk_rp_bigint_shift(ctx, 1);
}

duk_ret_t duk_rp_bigint_sr(duk_context *ctx)
{
    return duk_rp_bigint_shift(ctx, 0);
}

#define bn_printat(ctx, idx) do{\
    BIGNUM *b;\
    get_bn(ctx, b, idx);\
    printf("at %d ", (int)idx);\
    BN_print_fp(stdout, b);\
    putchar('\n');\
} while(0)

static BIGNUM *bn_negate(BIGNUM *bn)
{
    BIGNUM *bnr;
    int i=0, alen=0;
    unsigned char *bufa=NULL;

    alen = BN_num_bytes(bn);
    REMALLOC(bufa, alen);
    BN_bn2lebinpad(bn, bufa, alen);

    while(i<alen)
    {
        bufa[i]=~bufa[i];                
        i++;
    }

    bnr = BN_new();
    BN_lebin2bn(bufa, alen, bnr);

    free(bufa);
    return bnr;
}

static duk_ret_t duk_rp_bigint_x_or(duk_context *ctx, int xor)
{
    BIGNUM *bna, *bnb=NULL, *bnr, *bn_zero, *bn_neg1, *bnan=NULL, *bnbn=NULL;
    int i=0, alen=0, blen=0, slen=0, llen=0, a_is_neg=0, b_is_neg=0;
    unsigned char *bufa=NULL, *bufb=NULL, empty = 0;

    duk_push_this(ctx);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_zero"));
    get_bn(ctx, bn_zero, -1);
    duk_pop(ctx);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_neg1"));
    get_bn(ctx, bn_neg1, -1);
    duk_pop_2(ctx);

    get_bn(ctx, bna, 0);

    /* bits are as if positive and there is a negative flag 
       to do bitwise op, must convert to signed int         */
    if(BN_cmp(bna, bn_zero)<0)
    {
        int nbits = BN_num_bytes(bna) * 8;
        a_is_neg=1;

        bnan=bn_negate(bna);
        bna=bnan;
        BN_sub(bna, bna, bn_neg1);

        BN_set_bit(bna, nbits); // expand buffer by one byte. set to ff below

    }
    alen = BN_num_bytes(bna);

    get_bn(ctx, bnb, 1);

    if(BN_cmp(bnb, bn_zero)<0)
    {
        int nbits = BN_num_bytes(bnb) * 8;
        b_is_neg=1;

        bnbn=bn_negate(bnb);
        bnb=bnbn;

        BN_sub(bnb, bnb, bn_neg1);

        BN_set_bit(bnb, nbits);

    }
    blen = BN_num_bytes(bnb);

    REMALLOC(bufa, alen);
    BN_bn2lebinpad(bna, bufa, alen);

    if(bnan)
    {
        bufa[alen-1]=255;
        BN_free(bnan);
    }

    REMALLOC(bufb, blen);
    BN_bn2lebinpad(bnb, bufb, blen);

    if(bnbn)
    {
        BN_free(bnbn);
        bufb[blen-1]=255;
    }

    slen = (alen<blen) ? alen : blen;

    i=0;

    llen=alen;

    if(slen == alen) //use the longer as ret
    {
        unsigned char *t = bufa;
        alen=blen;
        llen=blen;
        bufa=bufb;
        bufb=t;
        if(a_is_neg)
            empty=255;
    }
    else if (b_is_neg)
        empty=255;

    if(xor)
        while(i<llen)
        {
            if(i<slen)
                bufa[i] ^= bufb[i];
            else
                bufa[i] ^= empty;
            i++;
        }
    else
        while(i<llen)
        {
            if(i<slen)
                bufa[i] |= bufb[i];
            else
                bufa[i] |= empty;
            i++;
        }

    if(bufb)
        free(bufb);

    bnr = new_bn(ctx,NULL,1);
    if( (xor && a_is_neg ^ b_is_neg) || (!xor && a_is_neg | b_is_neg) )
    {
        i=0;
        while(i<alen)
        {
            bufa[i]=~bufa[i];                
            i++;
        }
        BN_lebin2bn(bufa, alen, bnr);
        BN_sub(bnr, bn_neg1, bnr);
    }
    else
        BN_lebin2bn(bufa, alen, bnr);

    free(bufa);
    return 1;    
}

static duk_ret_t duk_rp_bigint_or(duk_context *ctx)
{
    return duk_rp_bigint_x_or(ctx, 0);
}

static duk_ret_t duk_rp_bigint_xor(duk_context *ctx)
{
    return duk_rp_bigint_x_or(ctx, 1);
}

static duk_ret_t duk_rp_bigint_and(duk_context *ctx)
{
    BIGNUM *bna, *bnb=NULL, *bnr, *bn_zero, *bn_neg1, *bnan=NULL, *bnbn=NULL;
    int i=0, alen=0, blen=0, slen=0, llen=0, a_is_neg=0, b_is_neg=0;
    unsigned char *bufa=NULL, *bufb=NULL, empty=0;
    duk_push_this(ctx);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_zero"));
    get_bn(ctx, bn_zero, -1);
    duk_pop(ctx);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_neg1"));
    get_bn(ctx, bn_neg1, -1);
    duk_pop_2(ctx);

    get_bn(ctx, bna, 0);

    /* bits are as if positive and there is a negative flag 
       to do bitwise op, must convert to signed int         */
    if(BN_cmp(bna, bn_zero)<0)
    {
        a_is_neg=1;

        bnan=bn_negate(bna);
        bna=bnan;
        BN_sub(bna, bna, bn_neg1);
    }
    alen = BN_num_bytes(bna);

    get_bn(ctx, bnb, 1);

    if(BN_cmp(bnb, bn_zero)<0)
    {
        b_is_neg=1;

        bnbn=bn_negate(bnb);
        bnb=bnbn;

        BN_sub(bnb, bnb, bn_neg1);
    }
    blen = BN_num_bytes(bnb);

    REMALLOC(bufa, alen);
    BN_bn2lebinpad(bna, bufa, alen);

    if(bnan)
        BN_free(bnan);

    REMALLOC(bufb, blen);
    BN_bn2lebinpad(bnb, bufb, blen);

    if(bnbn)
        BN_free(bnbn);

    slen = (alen<blen) ? alen : blen;

    i=0;

    llen=alen;

    if(slen == alen) //use the longer as ret
    {
        unsigned char *t = bufa;
        alen=blen;
        llen=blen;
        bufa=bufb;
        bufb=t;
        if(a_is_neg)
            empty=255;
    }
    else if (b_is_neg)
        empty=255;

    while(i<llen)
    {
        
        if(i<slen)
            bufa[i] &= bufb[i];
        else
            bufa[i] &= empty;
        i++;
    }

    if(bufb)
        free(bufb);

    bnr = new_bn(ctx,NULL,1);
    if(a_is_neg & b_is_neg)
    {
        i=0;
        while(i<alen)
        {
            bufa[i]=~bufa[i];                
            i++;
        }
        BN_lebin2bn(bufa, alen, bnr);
        BN_sub(bnr, bn_neg1, bnr);
    }
    else
        BN_lebin2bn(bufa, alen, bnr);

    free(bufa);
    return 1;    
}

static duk_ret_t _bigint_tostring(duk_context *ctx, const char *fname, int binary_signed)
{
    int radix = 10;
    BIGNUM *bn;
    char *val;

    if(!duk_is_undefined(ctx, 0))
    {
        radix = REQUIRE_INT(ctx, 0, "bigint: %s requires an int (2, 10 or 16)", fname);
        if(radix!=16 && radix!=10 && radix!=2)
            RP_THROW(ctx, "bigint: %s requires an int (2, 10 or 16)", fname);
    }
    
    get_bn(ctx, bn, 1);

    if(radix == 10)
    {
        val = BN_bn2dec(bn);
        duk_push_string(ctx, val);
        OPENSSL_free(val);
    }
    else
    {
        val = BN_bn2hex(bn);

        if(radix == 2)
        {
            char *s = hextobin(val);
            if(binary_signed)
                duk_push_string(ctx, s+1);
            else
                duk_push_string(ctx, s);
            free(s);
        }
        else
            duk_push_string(ctx, val);

        OPENSSL_free(val);
    }

    return 1;
}

duk_ret_t duk_rp_bigint_tostring(duk_context *ctx)
{
    duk_push_this(ctx);
    return _bigint_tostring(ctx, "toString", 0);
}


static void bi_sign_negate(duk_context *ctx, duk_idx_t idx)
{
    BIGNUM *bn, *bnr, *neg1;

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_neg1"));
    get_bn(ctx, neg1, -1);
    duk_pop(ctx);

    get_bn(ctx, bn, idx);

    duk_pull(ctx, idx);
    duk_insert(ctx, 0);

    bnr = bn_negate(bn);
    BN_sub(bnr, neg1, bnr);
    push_bn(ctx, bnr);
    duk_remove(ctx, 0);
}

duk_ret_t duk_rp_bigint_not(duk_context *ctx)
{
    BIGNUM *bna, *bnr, *bn_neg1;

    duk_push_this(ctx);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_neg1"));
    get_bn(ctx, bn_neg1, -1);
    duk_pop_2(ctx);

    get_bn(ctx, bna, 0);
    bnr = BN_dup(bna);
    BN_sub(bnr, bn_neg1, bnr);
    push_bn(ctx, bnr);

    return 1;
    
}



duk_ret_t duk_rp_bigint_tosignedstring(duk_context *ctx)
{
    BIGNUM *bna, *bnr, *bn_zero;
    int radix=10;

    if(!duk_is_undefined(ctx, 0))
    {
        radix = REQUIRE_INT(ctx, 0, "bigint: toSignedString requires an int (2, 10 or 16)");
        if(radix!=16 && radix!=10 && radix!=2)
            RP_THROW(ctx, "bigint: toSignedString requires an int (2, 10 or 16)");
    }
    
    duk_push_this(ctx);

    get_bn(ctx, bna, -1);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_zero"));
    get_bn(ctx, bn_zero, -1);
    duk_pop(ctx);

    if(BN_cmp(bna, bn_zero) > -1 || radix!=2)
    {
        duk_pop(ctx);
        return duk_rp_bigint_tostring(ctx);
    }

    bnr = BN_dup(bna);    

    push_bn(ctx, bnr);
    bi_sign_negate(ctx, -1);

    duk_replace(ctx, 0);
    duk_push_int(ctx, radix);
    duk_insert(ctx, 0);

    return _bigint_tostring(ctx, "toSignedString", 1);
}


duk_ret_t duk_rp_bigint_ton(duk_context *ctx)
{
    if( duk_is_object(ctx, 0) )
    { 
        if (duk_has_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("bn")) )
        {
            duk_get_global_string(ctx, "parseFloat");
            duk_push_string(ctx, "toString");
            duk_call_prop(ctx, 0, 0);
            duk_call(ctx, 1);
            return 1;
        }
        RP_THROW(ctx, "bigint: value is not a bigint");
        return 0;
    }
    RP_THROW(ctx, "bigint: value is not a bigint");
    return 0;
}
duk_ret_t duk_rp_bigint_const(duk_context *ctx);

duk_ret_t duk_rp_bigint(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_pull(ctx, 0);
    duk_new(ctx, 1);
    return 1;
}
//https://stackoverflow.com/questions/101439/the-most-efficient-way-to-implement-an-integer-based-power-function-powint-int
/*
static BN_ULONG bn_pow(BN_ULONG base, int exp)
{
    BN_ULONG result = 1;
    for (;;)
    {
        if (exp & 1)
            result *= base;
        exp >>= 1;
        if (!exp)
            break;
        base *= base;
    }

    return result;
}
*/


static duk_ret_t duk_rp_bigint_asi(duk_context *ctx)
{
    int is_positive = 1, bits = duk_get_int_default(ctx, 0, 0);
    BIGNUM *bna, *bnr, *bn_neg1, *bn_zero;

    duk_remove(ctx,0);

    if(bits < 0)
        RP_THROW(ctx, "bigint: first agrument - number of bits must be a positive number");
    get_bn(ctx, bna, 0);

    duk_pop(ctx);// empty stack

    duk_push_this(ctx);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_zero"));
    get_bn(ctx, bn_zero, -1);
    duk_pop(ctx);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_neg1"));
    get_bn(ctx, bn_neg1, -1);
    duk_pop_2(ctx);// empty stack

    if( BN_cmp(bna, bn_zero) < 0)
        is_positive=0;

    bnr = BN_dup(bna);
    BN_mask_bits(bnr, bits);

    if(BN_is_bit_set(bnr, bits-1))
    {
        int i=0, nsetbits = 8 - (bits % 8);//need to set these so they will clear in negation
        BIGNUM *bn_temp;

        while(nsetbits--)
        {
            BN_set_bit(bnr, bits + i++);
        }

        bn_temp = bn_negate(bnr);
        BN_free(bnr);
        bnr=bn_temp;

        if(!is_positive)
        {
            BN_sub(bnr, bn_neg1, bnr);
            if(!BN_is_bit_set(bnr, bits-1))
                BN_sub(bnr, bn_zero, bnr);
        }
        else
        {
            BN_sub(bnr, bn_zero, bnr);
            BN_add(bnr, bn_neg1, bnr);
        }
    }

    push_bn(ctx, bnr);        

    return 1;
}


duk_ret_t duk_rp_bigint_asu(duk_context *ctx)
{
    int bits = duk_get_int_default(ctx, 0, 0);
    BIGNUM *bna, *bnr, 
           *bn_neg1, *bn_zero;

    duk_push_this(ctx);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_zero"));
    get_bn(ctx, bn_zero, -1);
    duk_pop(ctx);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_neg1"));
    get_bn(ctx, bn_neg1, -1);
    duk_pop_2(ctx);

    get_bn(ctx, bna, 1);
    bnr = BN_dup(bna);

    if(BN_cmp(bnr, bn_zero) >= 0)
    {

        BN_mask_bits(bnr, bits);
        push_bn(ctx, bnr);

        return 1;
    }
    else
    {
        int i=0, nsetbits = 8 - (bits % 8);//need to set these so they will clear in negation
        BIGNUM *bn_temp;

        BN_sub(bnr, bn_zero, bnr);
        BN_mask_bits(bnr, bits);

        while(nsetbits--)
        {
            BN_set_bit(bnr, bits + i++);
        }        

        duk_pop_2(ctx);
        bn_temp = bn_negate(bnr);
        BN_free(bnr);
        bnr = bn_temp;
        BN_sub(bnr, bnr, bn_neg1);//add 1
        BN_clear_bit(bnr, bits);
        push_bn(ctx, bnr);

        return 1;
    }
}

const duk_function_list_entry bigint_funcs[] = {
    {"BigInt",             duk_rp_bigint,     1},
    {"add"   ,             duk_rp_bigint_add, 2},
    {"subtract",           duk_rp_bigint_sub, 2},
    {"multiply",           duk_rp_bigint_mul, 2},
    {"divide",             duk_rp_bigint_div, 2},
    {"remainder",          duk_rp_bigint_mod, 2},
    {"exponentiate",       duk_rp_bigint_exp, 2},
    {"unaryMinus",         duk_rp_bigint_neg, 1},
    {"equal",              duk_rp_bigint_eql, 2},
    {"notEqual",           duk_rp_bigint_neq, 2},
    {"lessThan",           duk_rp_bigint_lt,  2},
    {"lessThanOrEqual",    duk_rp_bigint_lte, 2},
    {"greaterThan",        duk_rp_bigint_gt,  2},
    {"greaterThanOrEqual", duk_rp_bigint_gte, 2},
    {"EQ",                 duk_rp_bigint_Eql, 2},
    {"NE",                 duk_rp_bigint_Neq, 2},
    {"LT",                 duk_rp_bigint_Lt,  2},
    {"LE",                 duk_rp_bigint_Lte, 2},
    {"GT",                 duk_rp_bigint_Gt,  2},
    {"GE",                 duk_rp_bigint_Gte, 2},
    {"ADD",                duk_rp_bigint_Add, 2},
    {"leftShift",          duk_rp_bigint_sl,  2},
    {"signedRightShift",   duk_rp_bigint_sr,  2},
    {"bitwiseNot",         duk_rp_bigint_not, 1},
    {"bitwiseAnd",         duk_rp_bigint_and, 2},
    {"bitwiseOr",          duk_rp_bigint_or,  2},
    {"bitwiseXor",         duk_rp_bigint_xor, 2},
    {"toNumber",           duk_rp_bigint_ton, 1},
    {"asIntN",             duk_rp_bigint_asi, 2},
    {"asUintN",            duk_rp_bigint_asu, 2},
    {NULL, NULL, 0}
};

/* all this constructor stuff is to make 
        (a instanceof JSBI)
    work.
*/
duk_ret_t duk_rp_bigint_const(duk_context *ctx)
{
    if(duk_is_constructor_call(ctx))
    {
        duk_push_this(ctx);
        duk_push_c_function(ctx, _bigint, 1);
        if(duk_is_number(ctx,0) || duk_is_string(ctx,0))
            duk_pull(ctx,0);
        else
            duk_push_number(ctx, 0.0);
        duk_call(ctx, 1);

        if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("bn")))
        {
            duk_put_prop_string(ctx, -3, DUK_HIDDEN_SYMBOL("bn"));
            duk_get_prop_string(ctx, -1, "toString");
            duk_put_prop_string(ctx, -3, "toString");
            duk_get_prop_string(ctx, -1, "toSignedString");
            duk_put_prop_string(ctx, -3, "toSignedString");
            duk_push_undefined(ctx);
            duk_set_finalizer(ctx, -2);
            duk_pop(ctx);
            duk_push_c_function(ctx, duk_rp_bigint_finalizer, 1);
            duk_set_finalizer(ctx, -2);
            return 1;
        }
    }
    return 0;
}

duk_ret_t jsbi_finalizer(duk_context *ctx)
{
    BIGNUM *bn_neg1, *bn_zero;

    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("bigint_zero"));
    get_bn(ctx, bn_zero, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("bigint_neg1"));
    get_bn(ctx, bn_neg1, -1);
    duk_pop(ctx);

    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("bigint_zero"));
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("bigint_neg1"));
    BN_free(bn_neg1);
    BN_free(bn_zero);

    return 0;
}

static void duk_rp_create_jsbi(duk_context *ctx)
{
    duk_push_c_function(ctx, duk_rp_bigint_const, 1);
    duk_put_function_list(ctx, -1, bigint_funcs);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, "prototype");
//    duk_push_c_function(ctx, jsbi_finalizer, 1);
//    duk_set_finalizer(ctx, -2);

    new_bn(ctx, "0", 1);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_zero"));
    new_bn(ctx, "-1", 1);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("bigint_neg1"));

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
    {"passToKeyIv", duk_rp_pass_to_keyiv, 1},
    {"passwd", do_passwd, 3},
    {"passwdCheck", check_passwd, 2},
    {"passwdComponents", passwd_components, 1},
    {NULL, NULL, 0}
};

duk_ret_t duk_open_module(duk_context *ctx)
{
    OpenSSL_add_all_digests() ;
    duk_push_object(ctx);
    duk_put_function_list(ctx, -1, crypto_funcs);
    duk_rp_create_jsbi(ctx);
    duk_put_prop_string(ctx, -2, "JSBI");
    return 1;
}
