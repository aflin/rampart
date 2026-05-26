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
#include <openssl/opensslv.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/provider.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#endif
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/objects.h>

/* Forward decl — rc_md_from_name is defined alongside the other Tier-1/2
 * helpers near the bottom of this file, but it's referenced earlier by
 * the RSA-PSS / RSA-OAEP extensions in rsa_sign/rsa_verify/rsa_pub_encrypt/
 * rsa_priv_decrypt. */
static const EVP_MD *rc_md_from_name(const char *name);
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "rampart.h"

/* Forward decl — used by kmac/cshake before the EC section where
 * it is defined. */
static int rc_get_key_any(duk_context *ctx, duk_idx_t pos_idx,
                          const void **out, duk_size_t *out_len);



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

    // bug fix: added NULL check after rp_crypto_do_passwd() - 2026-02-27
    if(!hash)
    {
        if(freesa)
            free(freesa);
        RP_THROW(ctx, "passwdCheck - password hashing failed");
    }

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

    free(hash);
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

/* Internal cipher encrypt/decrypt.  Handles:
 *   - "classic" CBC/CTR/ECB/etc. — straight EVP_EncryptUpdate/Final
 *   - AEAD (GCM/OCB/CCM) — optional aad, tag append/extract
 *   - WRAP (aes-*-wrap)  — RFC-3394; no IV; output = input + 8 bytes
 *
 * For AEAD encrypt, output = (salt prefix if password) || ciphertext || tag.
 * For AEAD decrypt, input  = (salt prefix if password) || ciphertext || tag
 *   (salt stripping happens in the caller before in_buffer reaches here).
 * `tag_len` is 0 for non-AEAD ciphers; >0 for AEAD (typically 16).
 */
static void rpcrypt(
  duk_context *ctx,
  unsigned char *key,
  unsigned char *iv,
  duk_size_t iv_len,
  const char *cipher_name,
  void *in_buffer,
  duk_size_t in_len,
  unsigned char *salt,
  int decrypt,
  const void *aad,
  duk_size_t aad_len,
  int tag_len
  )
{
    EVP_CIPHER_CTX *cipher_ctx;
    static const char magic[] = "Salted__";
    int out_len=0, current_len, m_len=strlen(magic);
    void *out_buffer;
    const EVP_CIPHER *cipher;
    int saltspace=0;
    int mode;
    int is_aead = 0, is_wrap = 0;

    if(!decrypt && salt)
        saltspace=PKCS5_SALT_LEN+m_len;

    /* Retrieve the cipher by name first so we can introspect its mode. */
    cipher = EVP_get_cipherbyname(cipher_name);
    if (cipher == NULL)
        RP_THROW(ctx, "Cipher %s not found", cipher_name);

    mode = EVP_CIPHER_mode(cipher);
    /* Detect AEAD via the cipher's flag — covers GCM, CCM, OCB,
     * ChaCha20-Poly1305, and any future AEAD ciphers OpenSSL adds
     * (e.g. AES-SIV).  Avoids hard-coding a mode list. */
    is_aead = (EVP_CIPHER_get_flags(cipher) & EVP_CIPH_FLAG_AEAD_CIPHER) != 0;
    is_wrap = (mode == EVP_CIPH_WRAP_MODE);

    /* Sanity: only AEAD ciphers accept aad/tag_len. */
    if (!is_aead && tag_len)
        RP_THROW(ctx, "cipher '%s' is not an AEAD mode; tagLength/aad cannot be set", cipher_name);
    if (!is_aead && aad_len)
        RP_THROW(ctx, "cipher '%s' is not an AEAD mode; aad cannot be set", cipher_name);

    /* On AEAD decrypt, the last tag_len bytes of the input are the tag.
     * Split them off and pass the rest as ciphertext to EVP. */
    const unsigned char *tag_in = NULL;
    duk_size_t ct_len = in_len;
    if (is_aead && decrypt)
    {
        if (in_len < (duk_size_t)tag_len)
            RP_THROW(ctx, "AEAD decrypt: input shorter than tagLength (%d < %d)",
                     (int)in_len, tag_len);
        ct_len = in_len - (duk_size_t)tag_len;
        tag_in = (const unsigned char *)in_buffer + ct_len;
    }

    /* Create and initialise the context */
    if (!(cipher_ctx = EVP_CIPHER_CTX_new()))
        DUK_OPENSSL_ERROR(ctx);

    /* Allocate output: ciphertext can be up to (input + blocksize),
     * plus salt prefix (if password), plus AEAD tag (on encrypt),
     * plus WRAP overhead (8 bytes on encrypt). */
    int extra = EVP_CIPHER_block_size(cipher) + saltspace;
    if (is_aead && !decrypt) extra += tag_len;
    if (is_wrap && !decrypt) extra += 8;
    out_buffer = duk_push_dynamic_buffer(ctx, in_len + extra);

    /* AES-KW (wrap) needs an explicit "I know what I'm doing" flag, and
     * doesn't take an IV (the spec defines a fixed IV).  Detect and
     * disable IV passing for wrap ciphers. */
    if (is_wrap)
        EVP_CIPHER_CTX_set_flags(cipher_ctx, EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);

    /* For AEAD with a non-default IV length, OpenSSL needs the IVLEN
     * set via EVP_CTRL_AEAD_SET_IVLEN AFTER the cipher is bound but
     * BEFORE key+iv are bound — otherwise it silently uses the
     * cipher's default IV length (12 for GCM/CCM, 12 for OCB) and
     * truncates/ignores the rest of the user's IV.  Split-init when
     * the lengths disagree. */
    int needs_ivlen_set = 0;
    if (is_aead && iv && iv_len > 0)
    {
        int default_iv = EVP_CIPHER_iv_length(cipher);
        if ((int)iv_len != default_iv)
            needs_ivlen_set = 1;
    }

    if (decrypt)
    {
        if (needs_ivlen_set)
        {
            if (!EVP_DecryptInit_ex(cipher_ctx, cipher, NULL, NULL, NULL))
            {
                EVP_CIPHER_CTX_free(cipher_ctx);
                DUK_OPENSSL_ERROR(ctx);
            }
            if (!EVP_CIPHER_CTX_ctrl(cipher_ctx, EVP_CTRL_AEAD_SET_IVLEN,
                                     (int)iv_len, NULL))
            {
                EVP_CIPHER_CTX_free(cipher_ctx);
                DUK_OPENSSL_ERROR(ctx);
            }
            if (!EVP_DecryptInit_ex(cipher_ctx, NULL, NULL, key, iv))
            {
                EVP_CIPHER_CTX_free(cipher_ctx);
                DUK_OPENSSL_ERROR(ctx);
            }
        }
        else if (!EVP_DecryptInit_ex(cipher_ctx, cipher, NULL, key,
                                     is_wrap ? NULL : iv))
        {
            EVP_CIPHER_CTX_free(cipher_ctx);
            DUK_OPENSSL_ERROR(ctx);
        }

        /* For AEAD: feed any AAD before the ciphertext. */
        if (is_aead && aad_len)
        {
            if (!EVP_DecryptUpdate(cipher_ctx, NULL, &current_len,
                                   (const unsigned char *)aad, (int)aad_len))
            {
                EVP_CIPHER_CTX_free(cipher_ctx);
                DUK_OPENSSL_ERROR(ctx);
            }
        }

        if (!EVP_DecryptUpdate(cipher_ctx, out_buffer, &current_len,
                               in_buffer, (int)ct_len))
        {
            EVP_CIPHER_CTX_free(cipher_ctx);
            DUK_OPENSSL_ERROR(ctx);
        }
        out_len += current_len;

        /* For AEAD: set the expected tag BEFORE the final call. */
        if (is_aead)
        {
            if (!EVP_CIPHER_CTX_ctrl(cipher_ctx, EVP_CTRL_AEAD_SET_TAG,
                                     tag_len, (void *)tag_in))
            {
                EVP_CIPHER_CTX_free(cipher_ctx);
                DUK_OPENSSL_ERROR(ctx);
            }
        }

        if (!EVP_DecryptFinal_ex(cipher_ctx, (unsigned char *)out_buffer + out_len, &current_len))
        {
            EVP_CIPHER_CTX_free(cipher_ctx);
            /* For AEAD this is the tag-verification failure path. */
            RP_THROW(ctx, "decrypt: %s",
                     is_aead ? "authentication tag verification failed" :
                               "EVP_DecryptFinal_ex failed (bad padding or wrong key)");
        }
    }
    else
    {
        if (needs_ivlen_set)
        {
            if (!EVP_EncryptInit_ex(cipher_ctx, cipher, NULL, NULL, NULL))
            {
                EVP_CIPHER_CTX_free(cipher_ctx);
                DUK_OPENSSL_ERROR(ctx);
            }
            if (!EVP_CIPHER_CTX_ctrl(cipher_ctx, EVP_CTRL_AEAD_SET_IVLEN,
                                     (int)iv_len, NULL))
            {
                EVP_CIPHER_CTX_free(cipher_ctx);
                DUK_OPENSSL_ERROR(ctx);
            }
            if (!EVP_EncryptInit_ex(cipher_ctx, NULL, NULL, key, iv))
            {
                EVP_CIPHER_CTX_free(cipher_ctx);
                DUK_OPENSSL_ERROR(ctx);
            }
        }
        else if (!EVP_EncryptInit_ex(cipher_ctx, cipher, NULL, key,
                                     is_wrap ? NULL : iv))
        {
            EVP_CIPHER_CTX_free(cipher_ctx);
            DUK_OPENSSL_ERROR(ctx);
        }

        /* with password, we need to write magic and the salt necessary to recreate key,iv */
        if(saltspace)
        {
            memcpy(out_buffer,magic,m_len);
            memcpy((unsigned char *)out_buffer+m_len,salt,PKCS5_SALT_LEN);
            out_len=saltspace;
        }

        /* For AEAD: feed any AAD before the plaintext. */
        if (is_aead && aad_len)
        {
            if (!EVP_EncryptUpdate(cipher_ctx, NULL, &current_len,
                                   (const unsigned char *)aad, (int)aad_len))
            {
                EVP_CIPHER_CTX_free(cipher_ctx);
                DUK_OPENSSL_ERROR(ctx);
            }
        }

        if (!EVP_EncryptUpdate(cipher_ctx, (unsigned char *)out_buffer + out_len, &current_len, in_buffer, (int)in_len))
        {
            EVP_CIPHER_CTX_free(cipher_ctx);
            DUK_OPENSSL_ERROR(ctx);
        }
        out_len += current_len;

        if (!EVP_EncryptFinal_ex(cipher_ctx, (unsigned char *)out_buffer + out_len, &current_len))
        {
            EVP_CIPHER_CTX_free(cipher_ctx);
            DUK_OPENSSL_ERROR(ctx);
        }
        out_len += current_len;

        /* For AEAD: extract the tag and append to output. */
        if (is_aead)
        {
            if (!EVP_CIPHER_CTX_ctrl(cipher_ctx, EVP_CTRL_AEAD_GET_TAG,
                                     tag_len, (unsigned char *)out_buffer + out_len))
            {
                EVP_CIPHER_CTX_free(cipher_ctx);
                DUK_OPENSSL_ERROR(ctx);
            }
            out_len += tag_len;
        }

        /* AEAD path adds tag to current_len's running total above;
         * non-AEAD already added current_len in EVP_EncryptFinal block. */
        goto skip_final_current;
    }

    out_len += current_len;
skip_final_current:

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

/* Finalize a plain buffer at top of stack into the caller-requested
 * shape. Backward compatible with the historical boolean convention:
 *   (omitted / falsy)           → hex string  (current default)
 *   true                         → Uint8Array  (current "raw" mode)
 *   { returnType: 'hex' }       → hex string  (explicit)
 *   { returnType: 'uint8array' }→ Uint8Array  (same as `true`)
 *   { returnType: 'buffer' }    → node-style Buffer
 *
 * The plain buffer at stack top is consumed/replaced. */
static void rc_finalize_buffer(duk_context *ctx, duk_idx_t opt_idx)
{
    /* Detect options object (but NOT array/function/Buffer-data, which are
     * also typed as object). */
    if (duk_is_object(ctx, opt_idx) &&
        !duk_is_array(ctx, opt_idx) &&
        !duk_is_function(ctx, opt_idx) &&
        !duk_is_buffer_data(ctx, opt_idx))
    {
        if (duk_get_prop_string(ctx, opt_idx, "returnType") && duk_is_string(ctx, -1))
        {
            const char *t = duk_get_string(ctx, -1);
            duk_pop(ctx);
            if (strcmp(t, "buffer") == 0)
            {
                /* Wrap plain buffer in Node-style Buffer (rampart-nodeshim
                 * relies on this distinction). */
                duk_get_global_string(ctx, "Buffer");
                duk_get_prop_string(ctx, -1, "from");
                duk_remove(ctx, -2);
                duk_dup(ctx, -2);
                duk_call(ctx, 1);
                duk_remove(ctx, -2);
                return;
            }
            else if (strcmp(t, "uint8array") == 0 ||
                     strcmp(t, "Uint8Array")  == 0)
            {
                /* Plain buffer already presents as Uint8Array — no-op */
                return;
            }
            /* returnType: 'hex' or any other string → fall through to hex */
        }
        else
        {
            duk_pop(ctx);  /* drop the non-string returnType lookup */
        }
        /* Object without recognized returnType: default to hex */
        duk_rp_toHex(ctx, -1, 0);
        return;
    }

    /* Boolean convention: true = raw (Uint8Array); falsy/missing = hex */
    if (!duk_is_boolean(ctx, opt_idx) || !duk_get_boolean(ctx, opt_idx))
        duk_rp_toHex(ctx, -1, 0);
}

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
    duk_size_t in_len=0, aad_len=0, iv_len=0;
    void *in_buffer=NULL;
    const void *aad=NULL;
    const char *cipher_name = "aes-256-cbc";
    unsigned char *key=NULL, *iv=NULL, salt[PKCS5_SALT_LEN], *salt_p=NULL;
    KEYIV kiv;
    int iter=10000;
    int tag_len=0;       /* 0 = not AEAD; >0 = AEAD tag length to use */
    int is_aead=0, is_wrap=0;
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

        /* Detect AEAD / WRAP mode early so we can adapt the iv check
         * and pick a default tag length.  AEAD is detected via the
         * cipher's flag — covers GCM/CCM/OCB/ChaCha20-Poly1305 plus
         * any future AEAD ciphers OpenSSL adds. */
        {
            const EVP_CIPHER *_c = EVP_get_cipherbyname(cipher_name);
            if (_c) {
                int _m = EVP_CIPHER_mode(_c);
                is_aead = (EVP_CIPHER_get_flags(_c) & EVP_CIPH_FLAG_AEAD_CIPHER) != 0;
                is_wrap = (_m == EVP_CIPH_WRAP_MODE);
            }
        }
        if (is_aead) tag_len = 16; /* default; overridden by tagLength opt */

        /* AEAD-only opts: aad (additional authenticated data) + tagLength. */
        if (duk_get_prop_string(ctx, 0, "aad"))
        {
            if (duk_is_string(ctx, -1))
                aad = duk_get_lstring(ctx, -1, &aad_len);
            else if (duk_is_buffer_data(ctx, -1))
                aad = duk_get_buffer_data(ctx, -1, &aad_len);
            else if (!duk_is_null(ctx, -1) && !duk_is_undefined(ctx, -1))
                RP_THROW(ctx, "crypto.[en|de]crypt: option 'aad' must be a buffer or string");
        }
        duk_pop(ctx);

        if (duk_get_prop_string(ctx, 0, "tagLength"))
        {
            tag_len = (int)REQUIRE_NUMBER(ctx, -1, "crypto.[en|de]crypt: option 'tagLength' must be a Number (bytes)");
            if (tag_len != 4 && tag_len != 8 && tag_len != 12 &&
                tag_len != 13 && tag_len != 14 && tag_len != 15 && tag_len != 16)
                RP_THROW(ctx, "crypto.[en|de]crypt: 'tagLength' must be one of 4/8/12/13/14/15/16 (bytes)");
        }
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

                if (is_wrap)
                {
                    /* AES-KW uses a fixed RFC-3394 IV; reject a user-
                     * supplied one rather than silently ignore it. */
                    RP_THROW(ctx, "crypto.[en|de]crypt: AES-KW (wrap) cipher does not accept 'iv' — RFC 3394 fixed IV is used internally");
                }
                else if (is_aead)
                {
                    /* AEAD modes (GCM/CCM/OCB) accept variable-length
                     * nonces; OpenSSL's default ivlen for GCM is 12.
                     * Don't fight the user's choice — pass whatever
                     * they gave (must be a buffer).  Capture the
                     * length so rpcrypt can call EVP_CTRL_AEAD_SET_IVLEN
                     * when it differs from the cipher's default
                     * (otherwise OpenSSL truncates to 12 bytes). */
                    if (!duk_is_buffer_data(ctx, -1))
                        RP_THROW(ctx, "crypto.[en|de]crypt: option 'iv' must be a buffer (or string in hex)");
                    iv = (unsigned char *)duk_get_buffer_data(ctx, -1, &iv_len);
                }
                else
                {
                    if (!duk_is_buffer_data(ctx, -1) || duk_get_length(ctx, -1) != ivlen)
                        RP_THROW(ctx, "crypto.[en|de]crypt: option 'iv' must be a buffer (%d bytes) or a string (%d bytes in hex)", ivlen, ivlen);
                    iv = (unsigned char *)duk_get_buffer_data(ctx, -1, &iv_len);
                }
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

    if (!key)
        RP_THROW(ctx, "en/decrypt: error- either a password or a key (and iv where required) must be provided");

    /* If we came through the positional-path (encrypt('pass', data, 'aes-256-gcm'))
     * the opts-path mode-detection block didn't run; fill in here. */
    if (!is_aead && !is_wrap)
    {
        const EVP_CIPHER *_c = EVP_get_cipherbyname(cipher_name);
        if (_c)
        {
            int _m = EVP_CIPHER_mode(_c);
            is_aead = (_m == EVP_CIPH_GCM_MODE || _m == EVP_CIPH_CCM_MODE ||
                       _m == EVP_CIPH_OCB_MODE);
            is_wrap = (_m == EVP_CIPH_WRAP_MODE);
            if (is_aead && tag_len == 0) tag_len = 16;
        }
    }

    if (!iv && !is_wrap)
        RP_THROW(ctx, "en/decrypt: error- 'iv' is required for cipher '%s' (only AES-KW wrap mode omits it)", cipher_name);

    rpcrypt ( ctx, key, iv, iv_len, cipher_name, in_buffer, in_len, salt_p, decrypt,
              aad, aad_len, tag_len);

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

    rc_finalize_buffer(ctx, 3);

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
    if (!mdctx)
        RP_THROW(ctx, "crypto.hash (%s) - EVP_MD_CTX_new failed", algo);
    if (!EVP_DigestInit_ex(mdctx, md, NULL)) {
        EVP_MD_CTX_free(mdctx);
        RP_THROW(ctx, "crypto.hash (%s) - EVP_DigestInit_ex failed", algo);
    }
    if (!EVP_DigestUpdate(mdctx, in, (size_t)in_len)) {
        EVP_MD_CTX_free(mdctx);
        RP_THROW(ctx, "crypto.hash (%s) - EVP_DigestUpdate failed", algo);
    }

    /* SHAKE128 / SHAKE256 are extendable-output functions (XOFs).  In
       OpenSSL 3.x, EVP_DigestFinal_ex on a XOF either fails or
       produces an unspecified length — the correct API is
       EVP_DigestFinalXOF with an explicit output length.  Use
       EVP_MD_get_size() as the default length (16 bytes for shake128,
       32 for shake256), matching what OpenSSL 1.1.1's
       EVP_DigestFinal_ex produced. */
    if (EVP_MD_get_flags(md) & EVP_MD_FLAG_XOF) {
        md_len = (unsigned int)EVP_MD_get_size(md);
        if (!EVP_DigestFinalXOF(mdctx, md_value, (size_t)md_len)) {
            EVP_MD_CTX_free(mdctx);
            RP_THROW(ctx, "crypto.hash (%s) - EVP_DigestFinalXOF failed", algo);
        }
    } else {
        if (!EVP_DigestFinal_ex(mdctx, md_value, &md_len)) {
            EVP_MD_CTX_free(mdctx);
            RP_THROW(ctx, "crypto.hash (%s) - EVP_DigestFinal_ex failed", algo);
        }
    }
    EVP_MD_CTX_free(mdctx);

    duk_resize_buffer(ctx, -1, (duk_size_t) md_len);

    rc_finalize_buffer(ctx, bool_idx);

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
    /* Historical signature: rand(len) → Uint8Array.
     * Extended signature: rand(len, opt) where opt is true (raw), false (hex),
     * or { returnType: 'hex'|'uint8array'|'buffer' }. When `opt` is omitted,
     * preserves the historical default of returning a raw Uint8Array
     * (NOT hex, unlike hash/hmac, since rand has always returned bytes). */
    duk_size_t len = REQUIRE_POSINT(ctx, 0, "crypto.rand requires a positive integer");
    void *buffer = duk_push_fixed_buffer(ctx, len);
    checkseed(ctx);
    if (RAND_bytes(buffer, len) != 1)
        DUK_OPENSSL_ERROR(ctx);

    /* If opt is provided, honor it. Omitted → keep historical Uint8Array. */
    if (!duk_is_undefined(ctx, 1)) {
        rc_finalize_buffer(ctx, 1);
    }
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
            RP_EVP_THROW(ctx, "rsa padding 'ssl' (RSA_SSLV23_PADDING) "
                "was removed in OpenSSL 3.0 along with SSLv2/SSLv3 support; "
                "use 'pkcs' or 'oaep' instead");
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


/* generate a self-signed x509 certificate and private key
   equivalent to: openssl req -x509 -nodes -days 365 -newkey rsa:2048 -keyout key -out cert -config conf
   Takes a JS object or subject string ("/C=US/CN=example.com") with optional options object.
   Returns {key, cert} as PEM strings */
static duk_ret_t duk_gen_cert(duk_context *ctx)
{
    X509 *x509 = NULL;
    EVP_PKEY *pkey = NULL;
    RSA *rsa = NULL;
    BIGNUM *bne = NULL;
    BIO *bio_key = NULL, *bio_cert = NULL;
    X509_NAME *subject = NULL;
    X509V3_CTX v3ctx;
    X509_EXTENSION *ext = NULL;
    int ret = 0, bits = 2048, days = 365;
    const char *txt = NULL, *subj_str = NULL;
    duk_idx_t obj_idx = -1;
    void *buf;

#define CERT_CLEANUP() do {             \
    if(x509) X509_free(x509);          \
    if(pkey) EVP_PKEY_free(pkey);       \
    if(bio_key) BIO_free_all(bio_key);  \
    if(bio_cert) BIO_free_all(bio_cert);\
} while(0)

#define CERT_ERR(ctx, ...) do { \
    CERT_CLEANUP();             \
    RP_THROW(ctx, __VA_ARGS__); \
} while(0)

#define CERT_SSL_ERR(ctx) do {  \
    CERT_CLEANUP();             \
    DUK_OPENSSL_ERROR(ctx);     \
} while(0)

    if(duk_is_string(ctx, 0))
    {
        subj_str = duk_get_string(ctx, 0);
        if(duk_is_object(ctx, 1))
            obj_idx = 1;
    }
    else if(duk_is_object(ctx, 0))
        obj_idx = 0;
    else
        RP_THROW(ctx, "crypto.gen_cert - first argument must be a String (\"/C=US/CN=name\") or Object");

    /* get options */
    if(obj_idx > -1 && duk_get_prop_string(ctx, obj_idx, "bits"))
        bits = REQUIRE_INT(ctx, -1, "crypto.gen_cert - 'bits' must be a Number");
    if(obj_idx > -1) duk_pop(ctx);

    if(obj_idx > -1 && duk_get_prop_string(ctx, obj_idx, "days"))
        days = REQUIRE_INT(ctx, -1, "crypto.gen_cert - 'days' must be a Number");
    if(obj_idx > -1) duk_pop(ctx);

    /* generate RSA key */
    bne = BN_new();
    if(!bne || !BN_set_word(bne, RSA_F4))
    {
        if(bne) BN_free(bne);
        DUK_OPENSSL_ERROR(ctx);
    }

    rsa = RSA_new();
    if(!rsa)
    {
        BN_free(bne);
        DUK_OPENSSL_ERROR(ctx);
    }

    if(!RSA_generate_key_ex(rsa, bits, bne, NULL))
    {
        RSA_free(rsa);
        BN_free(bne);
        DUK_OPENSSL_ERROR(ctx);
    }
    BN_free(bne);

    pkey = EVP_PKEY_new();
    if(!pkey)
    {
        RSA_free(rsa);
        DUK_OPENSSL_ERROR(ctx);
    }
    EVP_PKEY_assign_RSA(pkey, rsa);
    /* rsa now owned by pkey */

    /* create X509 certificate */
    x509 = X509_new();
    if(!x509)
        CERT_ERR(ctx, "crypto.gen_cert - X509_new() failed");

    X509_set_version(x509, 2); /* v3 */

    /* random serial number */
    {
        unsigned char serial_bytes[16];
        BIGNUM *bn = NULL;
        ASN1_INTEGER *ai = NULL;

        RAND_bytes(serial_bytes, sizeof(serial_bytes));
        serial_bytes[0] &= 0x7F; /* ensure positive */
        bn = BN_bin2bn(serial_bytes, sizeof(serial_bytes), NULL);
        ai = BN_to_ASN1_INTEGER(bn, NULL);
        X509_set_serialNumber(x509, ai);
        ASN1_INTEGER_free(ai);
        BN_free(bn);
    }

    /* validity period */
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), (long)days * 86400L);

    if(!X509_set_pubkey(x509, pkey))
        CERT_SSL_ERR(ctx);

    /* set subject name */
    subject = X509_get_subject_name(x509);

    if(subj_str)
    {
        /* parse "/KEY=VALUE/KEY=VALUE/..." format (openssl req -subj style) */
        char *copy = strdup(subj_str);
        char *p = copy, *seg, *eq;

        if(*p == '/') p++;

        while(p && *p)
        {
            seg = p;
            p = strchr(p, '/');
            if(p)
            {
                *p = '\0';
                p++;
            }
            eq = strchr(seg, '=');
            if(eq && seg[0])
            {
                *eq = '\0';
                X509_NAME_add_entry_by_txt(subject, seg, MBSTRING_ASC,
                    (const unsigned char*)(eq + 1), -1, -1, 0);
            }
        }
        free(copy);
    }
    else if(obj_idx > -1)
    {
        if(duk_get_prop_string(ctx, obj_idx, "country"))
        {
            txt = REQUIRE_STRING(ctx, -1, "crypto.gen_cert - 'country' must be a String");
            X509_NAME_add_entry_by_txt(subject, "C", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "state"))
        {
            txt = REQUIRE_STRING(ctx, -1, "crypto.gen_cert - 'state' must be a String");
            X509_NAME_add_entry_by_txt(subject, "ST", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "city"))
        {
            txt = REQUIRE_STRING(ctx, -1, "crypto.gen_cert - 'city' must be a String");
            X509_NAME_add_entry_by_txt(subject, "L", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "organization"))
        {
            txt = REQUIRE_STRING(ctx, -1, "crypto.gen_cert - 'organization' must be a String");
            X509_NAME_add_entry_by_txt(subject, "O", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "organizationUnit"))
        {
            txt = REQUIRE_STRING(ctx, -1, "crypto.gen_cert - 'organizationUnit' must be a String");
            X509_NAME_add_entry_by_txt(subject, "OU", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "name"))
        {
            txt = REQUIRE_STRING(ctx, -1, "crypto.gen_cert - 'name' must be a String");
            X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "email"))
        {
            txt = REQUIRE_STRING(ctx, -1, "crypto.gen_cert - 'email' must be a String");
            X509_NAME_add_entry_by_txt(subject, "emailAddress", MBSTRING_ASC, (const unsigned char*)txt, -1, -1, 0);
        }
        duk_pop(ctx);
    }

    /* self-signed: issuer = subject */
    X509_set_issuer_name(x509, subject);

    /* add X509v3 extensions */
    X509V3_set_ctx_nodb(&v3ctx);
    X509V3_set_ctx(&v3ctx, x509, x509, NULL, NULL, 0);

    /* basicConstraints - default "CA:FALSE" */
    {
        const char *bc = "CA:FALSE";
        if(obj_idx > -1 && duk_get_prop_string(ctx, obj_idx, "basicConstraints"))
            bc = REQUIRE_STRING(ctx, -1, "crypto.gen_cert - 'basicConstraints' must be a String");
        if(obj_idx > -1) duk_pop(ctx);
        ext = X509V3_EXT_nconf_nid(NULL, &v3ctx, NID_basic_constraints, bc);
        if(ext)
        {
            X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
        }
    }

    /* keyUsage - default "digitalSignature, keyEncipherment" */
    {
        const char *ku = "digitalSignature, keyEncipherment";
        if(obj_idx > -1 && duk_get_prop_string(ctx, obj_idx, "keyUsage"))
            ku = REQUIRE_STRING(ctx, -1, "crypto.gen_cert - 'keyUsage' must be a String");
        if(obj_idx > -1) duk_pop(ctx);
        ext = X509V3_EXT_nconf_nid(NULL, &v3ctx, NID_key_usage, ku);
        if(ext)
        {
            X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
        }
    }

    /* subjectAltName */
    if(obj_idx > -1 && duk_get_prop_string(ctx, obj_idx, "subjectAltName"))
    {
        const char *san_prefix = "DNS";
        char *san_str = NULL;
        size_t san_len = 0;

        /* determine SAN type prefix */
        if(duk_get_prop_string(ctx, obj_idx, "subjectAltNameType"))
        {
            const char *type = REQUIRE_STRING(ctx, -1, "crypto.gen_cert - 'subjectAltNameType' must be a String");
            if(!strcasecmp("dns", type)) san_prefix = "DNS";
            else if(!strcasecmp("email", type)) san_prefix = "email";
            else if(!strcasecmp("ip", type)) san_prefix = "IP";
            else if(!strcasecmp("uri", type)) san_prefix = "URI";
            else CERT_ERR(ctx, "crypto.gen_cert - 'subjectAltNameType' must be 'dns', 'email', 'ip', or 'uri'");
        }
        duk_pop(ctx);

        if(duk_is_array(ctx, -1))
        {
            int i, l = (int)duk_get_length(ctx, -1);

            for(i = 0; i < l; i++)
            {
                duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
                txt = REQUIRE_STRING(ctx, -1, "crypto.gen_cert - 'subjectAltName' entries must be Strings");
                san_len += strlen(san_prefix) + 1 + strlen(txt) + 1; /* "DNS:name," */
                duk_pop(ctx);
            }
            san_str = (char *)malloc(san_len + 1);
            san_str[0] = '\0';
            for(i = 0; i < l; i++)
            {
                if(i > 0) strcat(san_str, ",");
                strcat(san_str, san_prefix);
                strcat(san_str, ":");
                duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
                strcat(san_str, duk_get_string(ctx, -1));
                duk_pop(ctx);
            }
        }
        else
        {
            txt = REQUIRE_STRING(ctx, -1, "crypto.gen_cert - 'subjectAltName' must be a String or Array of Strings");
            san_len = strlen(san_prefix) + 1 + strlen(txt) + 1;
            san_str = (char *)malloc(san_len);
            sprintf(san_str, "%s:%s", san_prefix, txt);
        }

        ext = X509V3_EXT_nconf_nid(NULL, &v3ctx, NID_subject_alt_name, san_str);
        free(san_str);
        if(ext)
        {
            X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
        }
    }
    if(obj_idx > -1) duk_pop(ctx);

    /* sign the certificate */
    ret = X509_sign(x509, pkey, EVP_sha256());
    if(ret <= 0)
        CERT_SSL_ERR(ctx);

    /* write PEM output */
    bio_key = BIO_new(BIO_s_mem());
    bio_cert = BIO_new(BIO_s_mem());
    if(!bio_key || !bio_cert)
        CERT_ERR(ctx, "crypto.gen_cert - BIO_new() failed");

    ret = PEM_write_bio_PrivateKey(bio_key, pkey, NULL, NULL, 0, NULL, NULL);
    if(ret != 1)
        CERT_SSL_ERR(ctx);

    ret = PEM_write_bio_X509(bio_cert, x509);
    if(ret != 1)
        CERT_SSL_ERR(ctx);

    /* return {key, cert} */
    duk_push_object(ctx);

    ret = BIO_get_mem_data(bio_key, &buf);
    duk_push_lstring(ctx, (char *)buf, (duk_size_t)ret);
    duk_put_prop_string(ctx, -2, "key");

    ret = BIO_get_mem_data(bio_cert, &buf);
    duk_push_lstring(ctx, (char *)buf, (duk_size_t)ret);
    duk_put_prop_string(ctx, -2, "cert");

    CERT_CLEANUP();
    return 1;

#undef CERT_CLEANUP
#undef CERT_ERR
#undef CERT_SSL_ERR
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

    /* OpenSSL 1.1+: ASN1_STRING is opaque; use accessors. */
    n = ASN1_STRING_length(sig);
    s = ASN1_STRING_get0_data(sig);
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
    duk_size_t l = (duk_size_t)BIO_get_mem_data(btmp, &p);\
    if(l && *p) {\
        duk_push_lstring(ctx, p, l);\
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
    EVP_PKEY_CTX *kctx=NULL;
    unsigned char *buf;
    /* Tier 1.4 PSS opts (when arg 3 is an object): */
    int use_pss = 0;
    int saltlen = -1;             /* RSA_PSS_SALTLEN_DIGEST (= -1) */
    const EVP_MD *sign_md = NULL;
    const EVP_MD *mgf_md  = NULL;

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

    /* Arg 2: either a string (legacy: password) or an object (new: PSS opts
     * + password inside).  Backwards-compatible. */
    if (duk_is_string(ctx, 2))
    {
        passwd = duk_get_string(ctx, 2);
    }
    else if (duk_is_object(ctx, 2) && !duk_is_buffer_data(ctx, 2) &&
             !duk_is_array(ctx, 2) && !duk_is_function(ctx, 2))
    {
        const char *pad = NULL, *hn = NULL, *mn = NULL;
        if (duk_get_prop_string(ctx, 2, "padding"))
            pad = REQUIRE_STRING(ctx, -1, "rsa_sign: 'padding' must be a string ('pkcs1' or 'pss')");
        duk_pop(ctx);
        if (pad && !strcmp(pad, "pss"))
            use_pss = 1;
        else if (pad && strcmp(pad, "pkcs1"))
            RP_MD_THROW(ctx, "rsa_sign: unknown 'padding' value '%s' (use 'pkcs1' or 'pss')", pad);

        if (duk_get_prop_string(ctx, 2, "hash"))
            hn = REQUIRE_STRING(ctx, -1, "rsa_sign: 'hash' must be a string");
        duk_pop(ctx);
        if (hn) {
            sign_md = rc_md_from_name(hn);
            if (!sign_md) RP_MD_THROW(ctx, "rsa_sign: unsupported hash '%s'", hn);
        }

        if (duk_get_prop_string(ctx, 2, "mgfHash"))
            mn = REQUIRE_STRING(ctx, -1, "rsa_sign: 'mgfHash' must be a string");
        duk_pop(ctx);
        if (mn) {
            mgf_md = rc_md_from_name(mn);
            if (!mgf_md) RP_MD_THROW(ctx, "rsa_sign: unsupported mgfHash '%s'", mn);
        }

        if (duk_get_prop_string(ctx, 2, "saltLength"))
            saltlen = (int)REQUIRE_NUMBER(ctx, -1, "rsa_sign: 'saltLength' must be a Number");
        duk_pop(ctx);

        if (duk_get_prop_string(ctx, 2, "password"))
            passwd = REQUIRE_STRING(ctx, -1, "rsa_sign: 'password' must be a string");
        duk_pop(ctx);
    }
    else if (!duk_is_null(ctx, 2) && !duk_is_undefined(ctx, 2))
    {
        RP_MD_THROW(ctx, "crypt.rsa_sign - third argument must be a string (password) or options object");
    }

    if (!sign_md) sign_md = EVP_sha256();
    if (!mgf_md)  mgf_md  = sign_md;

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

    /* EVP_DigestSignInit returns the EVP_PKEY_CTX via &kctx so we can
     * tweak padding/saltlen/mgf for PSS. */
    if( EVP_DigestSignInit(pctx, &kctx, sign_md, NULL, key) <= 0)
        DUK_MD_OPENSSL_ERROR(ctx);

    if (use_pss)
    {
        if (EVP_PKEY_CTX_set_rsa_padding(kctx, RSA_PKCS1_PSS_PADDING) <= 0)
            DUK_MD_OPENSSL_ERROR(ctx);
        if (EVP_PKEY_CTX_set_rsa_pss_saltlen(kctx, saltlen) <= 0)
            DUK_MD_OPENSSL_ERROR(ctx);
        if (EVP_PKEY_CTX_set_rsa_mgf1_md(kctx, mgf_md) <= 0)
            DUK_MD_OPENSSL_ERROR(ctx);
    }

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
    /* kctx is owned by pctx; freed with it */

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
    EVP_PKEY_CTX *kctx=NULL;
    unsigned char *sig=NULL;
    /* Tier 1.4 PSS opts (arg 3, optional, object): */
    int use_pss = 0;
    int saltlen = -1;             /* RSA_PSS_SALTLEN_DIGEST */
    const EVP_MD *verify_md = NULL;
    const EVP_MD *mgf_md    = NULL;

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

    /* Optional arg 3: opts object for PSS / non-default hash */
    if (duk_is_object(ctx, 3) && !duk_is_buffer_data(ctx, 3) &&
        !duk_is_array(ctx, 3) && !duk_is_function(ctx, 3))
    {
        const char *pad = NULL, *hn = NULL, *mn = NULL;
        if (duk_get_prop_string(ctx, 3, "padding"))
            pad = REQUIRE_STRING(ctx, -1, "rsa_verify: 'padding' must be a string ('pkcs1' or 'pss')");
        duk_pop(ctx);
        if (pad && !strcmp(pad, "pss"))
            use_pss = 1;
        else if (pad && strcmp(pad, "pkcs1"))
            RP_MD_THROW(ctx, "rsa_verify: unknown 'padding' value '%s' (use 'pkcs1' or 'pss')", pad);

        if (duk_get_prop_string(ctx, 3, "hash"))
            hn = REQUIRE_STRING(ctx, -1, "rsa_verify: 'hash' must be a string");
        duk_pop(ctx);
        if (hn) {
            verify_md = rc_md_from_name(hn);
            if (!verify_md) RP_MD_THROW(ctx, "rsa_verify: unsupported hash '%s'", hn);
        }

        if (duk_get_prop_string(ctx, 3, "mgfHash"))
            mn = REQUIRE_STRING(ctx, -1, "rsa_verify: 'mgfHash' must be a string");
        duk_pop(ctx);
        if (mn) {
            mgf_md = rc_md_from_name(mn);
            if (!mgf_md) RP_MD_THROW(ctx, "rsa_verify: unsupported mgfHash '%s'", mn);
        }

        if (duk_get_prop_string(ctx, 3, "saltLength"))
            saltlen = (int)REQUIRE_NUMBER(ctx, -1, "rsa_verify: 'saltLength' must be a Number");
        duk_pop(ctx);
    }

    if (!verify_md) verify_md = EVP_sha256();
    if (!mgf_md)    mgf_md    = verify_md;

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

    if( EVP_DigestVerifyInit(pctx, &kctx, verify_md, NULL, key) <= 0)
        DUK_MD_OPENSSL_ERROR(ctx);

    if (use_pss)
    {
        if (EVP_PKEY_CTX_set_rsa_padding(kctx, RSA_PKCS1_PSS_PADDING) <= 0)
            DUK_MD_OPENSSL_ERROR(ctx);
        if (EVP_PKEY_CTX_set_rsa_pss_saltlen(kctx, saltlen) <= 0)
            DUK_MD_OPENSSL_ERROR(ctx);
        if (EVP_PKEY_CTX_set_rsa_mgf1_md(kctx, mgf_md) <= 0)
            DUK_MD_OPENSSL_ERROR(ctx);
    }

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

    /* Pre-parse password from EITHER the legacy arg-3 string position OR
     * the new arg-2 opts-object 'password' property.  The opts-object
     * branch also captures the padding/hash/label so we can forward
     * them after the PEM read.  (We re-parse padding below to keep the
     * legacy string-padding branch unchanged.) */
    if (duk_is_string(ctx, 3))
        passwd = duk_get_string(ctx, 3);
    else if (!duk_is_null(ctx, 3) && !duk_is_undefined(ctx, 3))
        RP_EVP_THROW(ctx, "crypt.rsa_priv_decrypt - fourth optional argument must be a string (password)");
    if (!passwd && duk_is_object(ctx, 2) && !duk_is_buffer_data(ctx, 2) &&
        !duk_is_array(ctx, 2) && !duk_is_function(ctx, 2))
    {
        if (duk_get_prop_string(ctx, 2, "password"))
            passwd = REQUIRE_STRING(ctx, -1, "rsa_priv_decrypt: 'password' must be a string");
        duk_pop(ctx);
    }

    pfile = BIO_new_mem_buf((const void*)privfile, (int)psz);

    if(!passwd)
        rsa = PEM_read_bio_RSAPrivateKey(pfile, NULL, pass_cb, NULL);
    else
        rsa = PEM_read_bio_RSAPrivateKey(pfile, NULL, pass_cb, (void*)passwd);

    BIO_free_all(pfile);

    if(!rsa)
        RP_EVP_THROW(ctx, "Invalid public key file%s", passwd?" or bad password":"");

    rsasize = RSA_size(rsa);

    /* OAEP opts (only meaningful when padding=='oaep'): */
    const EVP_MD *oaep_md = NULL;
    const void   *oaep_label = NULL;
    duk_size_t    oaep_label_len = 0;

    if(duk_is_string(ctx, 2) )
    {
        const char *pad = duk_get_string(ctx, 2);
        if (!strcmp ("pkcs", pad) )
            padding=RSA_PKCS1_PADDING;
        else if (!strcmp ("oaep", pad) )
            padding=RSA_PKCS1_OAEP_PADDING;
        else if (!strcmp ("ssl", pad) )
            RP_EVP_THROW(ctx, "rsa padding 'ssl' (RSA_SSLV23_PADDING) "
                "was removed in OpenSSL 3.0 along with SSLv2/SSLv3 support; "
                "use 'pkcs' or 'oaep' instead");
        else if (!strcmp ("raw", pad) )
            padding=RSA_NO_PADDING;
        else
            RP_EVP_THROW(ctx, "crypt.rsa_priv_decrypt - third optional argument (padding type) '%s' is invalid", pad);
    }
    else if (duk_is_object(ctx, 2) && !duk_is_buffer_data(ctx, 2) &&
             !duk_is_array(ctx, 2) && !duk_is_function(ctx, 2))
    {
        /* Tier 1.5: opts-object form.  padding/hash/label/(password). */
        const char *pad = NULL, *hn = NULL;
        if (duk_get_prop_string(ctx, 2, "padding"))
            pad = REQUIRE_STRING(ctx, -1, "rsa_priv_decrypt: 'padding' must be a string ('pkcs1'|'oaep'|'raw')");
        duk_pop(ctx);
        if (pad)
        {
            if (!strcmp(pad, "pkcs1") || !strcmp(pad, "pkcs")) padding = RSA_PKCS1_PADDING;
            else if (!strcmp(pad, "oaep"))                     padding = RSA_PKCS1_OAEP_PADDING;
            else if (!strcmp(pad, "raw"))                      padding = RSA_NO_PADDING;
            else RP_EVP_THROW(ctx, "rsa_priv_decrypt: unknown padding '%s'", pad);
        }

        if (duk_get_prop_string(ctx, 2, "hash"))
            hn = REQUIRE_STRING(ctx, -1, "rsa_priv_decrypt: 'hash' must be a string");
        duk_pop(ctx);
        if (hn)
        {
            oaep_md = rc_md_from_name(hn);
            if (!oaep_md) RP_EVP_THROW(ctx, "rsa_priv_decrypt: unsupported hash '%s'", hn);
        }

        if (duk_get_prop_string(ctx, 2, "label"))
        {
            if (duk_is_string(ctx, -1))
                oaep_label = duk_get_lstring(ctx, -1, &oaep_label_len);
            else if (duk_is_buffer_data(ctx, -1))
                oaep_label = duk_get_buffer_data(ctx, -1, &oaep_label_len);
            else if (!duk_is_null(ctx, -1) && !duk_is_undefined(ctx, -1))
                RP_EVP_THROW(ctx, "rsa_priv_decrypt: 'label' must be a string or buffer");
        }
        duk_pop(ctx);

        /* Password from the opts object was already captured before the
         * PEM read above; nothing to do here. */
    }
    else if (!duk_is_undefined(ctx, 2) && !duk_is_null(ctx, 2) )
        RP_EVP_THROW(ctx, "crypt.rsa_priv_decrypt - third optional argument must be a string (padding) or options object");

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

    /* OAEP extras (only relevant when padding==OAEP). */
    if (padding == RSA_PKCS1_OAEP_PADDING)
    {
        if (oaep_md)
        {
            if (EVP_PKEY_CTX_set_rsa_oaep_md(pctx, oaep_md) <= 0)
                DUK_EVP_OPENSSL_ERROR(ctx);
            if (EVP_PKEY_CTX_set_rsa_mgf1_md(pctx, oaep_md) <= 0)
                DUK_EVP_OPENSSL_ERROR(ctx);
        }
        if (oaep_label)
        {
            /* set0 takes ownership of the buffer; malloc + memcpy. */
            unsigned char *label_dup = (unsigned char *)OPENSSL_malloc(oaep_label_len);
            if (!label_dup) RP_EVP_THROW(ctx, "rsa_priv_decrypt: oom allocating label");
            memcpy(label_dup, oaep_label, oaep_label_len);
            if (EVP_PKEY_CTX_set0_rsa_oaep_label(pctx, label_dup, (int)oaep_label_len) <= 0)
            {
                OPENSSL_free(label_dup);
                DUK_EVP_OPENSSL_ERROR(ctx);
            }
        }
    }

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

    /* Tier 1.5: OAEP opts (used when arg 2 is an opts-object): */
    const EVP_MD *oaep_md = NULL;
    const void   *oaep_label = NULL;
    duk_size_t    oaep_label_len = 0;
    int           rsasize_adjust = 11;  /* default PKCS1 overhead */

    if(duk_is_string(ctx, 2) )
    {
        const char *pad = duk_get_string(ctx, 2);
        if (!strcmp ("pkcs", pad) )
        {
            padding=RSA_PKCS1_PADDING;
            rsasize_adjust = 11;
        }
        else if (!strcmp ("oaep", pad) )
        {
            padding=RSA_PKCS1_OAEP_PADDING;
            rsasize_adjust = 42;
        }
        else if (!strcmp ("ssl", pad) )
            RP_EVP_THROW(ctx, "rsa padding 'ssl' (RSA_SSLV23_PADDING) "
                "was removed in OpenSSL 3.0 along with SSLv2/SSLv3 support; "
                "use 'pkcs' or 'oaep' instead");
        else if (!strcmp ("raw", pad) )
        {
            padding=RSA_NO_PADDING;
            rsasize_adjust = 0;
        }
        else
            RP_EVP_THROW(ctx, "crypt.rsa_pub_encrypt - third optional argument (padding type) '%s' is invalid", pad);
    }
    else if (duk_is_object(ctx, 2) && !duk_is_buffer_data(ctx, 2) &&
             !duk_is_array(ctx, 2) && !duk_is_function(ctx, 2))
    {
        /* Opts-object form: {padding, hash, label} */
        const char *pad = NULL, *hn = NULL;
        if (duk_get_prop_string(ctx, 2, "padding"))
            pad = REQUIRE_STRING(ctx, -1, "rsa_pub_encrypt: 'padding' must be a string ('pkcs1'|'oaep'|'raw')");
        duk_pop(ctx);
        if (pad)
        {
            if (!strcmp(pad, "pkcs1") || !strcmp(pad, "pkcs")) { padding = RSA_PKCS1_PADDING;      rsasize_adjust = 11; }
            else if (!strcmp(pad, "oaep"))                     { padding = RSA_PKCS1_OAEP_PADDING; rsasize_adjust = 42; }
            else if (!strcmp(pad, "raw"))                      { padding = RSA_NO_PADDING;         rsasize_adjust = 0; }
            else RP_EVP_THROW(ctx, "rsa_pub_encrypt: unknown padding '%s'", pad);
        }

        if (duk_get_prop_string(ctx, 2, "hash"))
            hn = REQUIRE_STRING(ctx, -1, "rsa_pub_encrypt: 'hash' must be a string");
        duk_pop(ctx);
        if (hn)
        {
            oaep_md = rc_md_from_name(hn);
            if (!oaep_md) RP_EVP_THROW(ctx, "rsa_pub_encrypt: unsupported hash '%s'", hn);
        }

        if (duk_get_prop_string(ctx, 2, "label"))
        {
            if (duk_is_string(ctx, -1))
                oaep_label = duk_get_lstring(ctx, -1, &oaep_label_len);
            else if (duk_is_buffer_data(ctx, -1))
                oaep_label = duk_get_buffer_data(ctx, -1, &oaep_label_len);
            else if (!duk_is_null(ctx, -1) && !duk_is_undefined(ctx, -1))
                RP_EVP_THROW(ctx, "rsa_pub_encrypt: 'label' must be a string or buffer");
        }
        duk_pop(ctx);
    }
    else if (!duk_is_undefined(ctx, 2) && !duk_is_null(ctx, 2) )
        RP_EVP_THROW(ctx, "crypt.rsa_pub_encrypt - third optional argument must be a string (padding) or options object");

    rsasize -= rsasize_adjust;

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

    /* OAEP extras (only meaningful when padding==OAEP). */
    if (padding == RSA_PKCS1_OAEP_PADDING)
    {
        if (oaep_md)
        {
            if (EVP_PKEY_CTX_set_rsa_oaep_md(pctx, oaep_md) <= 0)
                DUK_EVP_OPENSSL_ERROR(ctx);
            if (EVP_PKEY_CTX_set_rsa_mgf1_md(pctx, oaep_md) <= 0)
                DUK_EVP_OPENSSL_ERROR(ctx);
        }
        if (oaep_label)
        {
            unsigned char *label_dup = (unsigned char *)OPENSSL_malloc(oaep_label_len);
            if (!label_dup) RP_EVP_THROW(ctx, "rsa_pub_encrypt: oom allocating label");
            memcpy(label_dup, oaep_label, oaep_label_len);
            if (EVP_PKEY_CTX_set0_rsa_oaep_label(pctx, label_dup, (int)oaep_label_len) <= 0)
            {
                OPENSSL_free(label_dup);
                DUK_EVP_OPENSSL_ERROR(ctx);
            }
        }
    }

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


/* ========================================================================
 * Tier 1 / Tier 2 Web-Crypto-supporting primitives.
 *
 *   crypto.timingSafeEqual(a, b)           — constant-time compare
 *   crypto.pbkdf2({...})                   — PBKDF2 standalone
 *   crypto.hkdf({...})                     — HKDF (HMAC-based)
 *   crypto.ec_gen_key({curve})             — EC keypair
 *   crypto.ec_import_pub_key({...})        — normalize EC pub key to SPKI
 *   crypto.ec_import_priv_key({...})       — normalize EC priv key to PKCS#8
 *   crypto.ec_export_pub_key({key,format}) — SPKI <-> raw point
 *   crypto.ec_export_priv_key({key,format})— PKCS#8 <-> raw scalar
 *   crypto.ecdsa_sign({...})               — ECDSA sign (DER or P1363)
 *   crypto.ecdsa_verify({...})             — ECDSA verify
 *   crypto.ecdh({privateKey, publicKey})   — ECDH shared secret
 *
 * AES-GCM, AES-KW, RSA-PSS, RSA-OAEP are integrated into the existing
 * encrypt/decrypt/rsa_sign/rsa_verify/rsa_pub_encrypt/rsa_priv_decrypt
 * functions (see those sites for the per-cipher / padding additions).
 * ====================================================================== */

#include <openssl/kdf.h>

/* Like rc_finalize_buffer above, but the default (no returnType opt)
 * is the plain duktape buffer (presents as Uint8Array) — matching the
 * convention of crypto.encrypt / rsa_pub_encrypt / rsa_sign / rand
 * etc.  The hash/hmac convention of "default = hex" is wrong for the
 * Tier-1/2 byte-producing functions where bytes are the obvious form.
 *
 * Recognized returnType values:
 *   'hex'        → hex :green:`String`
 *   'uint8array' → plain duktape buffer (Uint8Array) — same as default
 *   'buffer'     → Node-style Buffer wrap (Buffer.from(plain));
 *                  rampart-nodeshim depends on this distinction. */
static void rc_finalize_buffer_buf_default(duk_context *ctx, duk_idx_t opt_idx)
{
    if (duk_is_object(ctx, opt_idx) &&
        !duk_is_array(ctx, opt_idx) &&
        !duk_is_function(ctx, opt_idx) &&
        !duk_is_buffer_data(ctx, opt_idx))
    {
        if (duk_get_prop_string(ctx, opt_idx, "returnType") && duk_is_string(ctx, -1))
        {
            const char *t = duk_get_string(ctx, -1);
            if (!strcmp(t, "hex"))
            {
                duk_pop(ctx);
                duk_rp_toHex(ctx, -1, 0);
                return;
            }
            if (!strcmp(t, "buffer"))
            {
                duk_pop(ctx);
                duk_get_global_string(ctx, "Buffer");
                duk_get_prop_string(ctx, -1, "from");
                duk_remove(ctx, -2);
                duk_dup(ctx, -2);
                duk_call(ctx, 1);
                duk_remove(ctx, -2);
                return;
            }
            /* 'uint8array' or unrecognized → fall through (no-op) */
        }
        duk_pop(ctx);
    }
    /* Plain duktape buffer already on stack — presents as Uint8Array. */
}

/* Map a JS hash name ('sha1'/'sha256'/'sha384'/'sha512') to an EVP_MD*.
 * Returns NULL on unknown name; caller throws.  Accepts the common
 * dashed forms too ('sha-256'). */
static const EVP_MD *rc_md_from_name(const char *name)
{
    if (!name) return NULL;
    if (!strcmp(name, "sha1") || !strcmp(name, "sha-1"))   return EVP_sha1();
    if (!strcmp(name, "sha224") || !strcmp(name, "sha-224")) return EVP_sha224();
    if (!strcmp(name, "sha256") || !strcmp(name, "sha-256")) return EVP_sha256();
    if (!strcmp(name, "sha384") || !strcmp(name, "sha-384")) return EVP_sha384();
    if (!strcmp(name, "sha512") || !strcmp(name, "sha-512")) return EVP_sha512();
    return NULL;
}

/* --- 1.3 timingSafeEqual ---
 * crypto.timingSafeEqual(a, b) → boolean.  Constant-time bytes
 * comparison.  Throws RangeError if lengths differ (matches node).
 */
static duk_ret_t duk_timing_safe_equal(duk_context *ctx)
{
    duk_size_t alen, blen;
    const void *a = NULL, *b = NULL;

    if (duk_is_string(ctx, 0))      a = duk_get_lstring(ctx, 0, &alen);
    else if (duk_is_buffer_data(ctx, 0)) a = duk_get_buffer_data(ctx, 0, &alen);
    else RP_THROW(ctx, "crypto.timingSafeEqual - arg 1 must be a string or buffer");

    if (duk_is_string(ctx, 1))      b = duk_get_lstring(ctx, 1, &blen);
    else if (duk_is_buffer_data(ctx, 1)) b = duk_get_buffer_data(ctx, 1, &blen);
    else RP_THROW(ctx, "crypto.timingSafeEqual - arg 2 must be a string or buffer");

    if (alen != blen)
        RP_THROW(ctx, "crypto.timingSafeEqual - input buffers must have the same length (got %d and %d)",
                 (int)alen, (int)blen);

    /* CRYPTO_memcmp returns 0 on equal; we want true on equal */
    duk_push_boolean(ctx, CRYPTO_memcmp(a, b, (size_t)alen) == 0);
    return 1;
}

/* --- 1.2 PBKDF2 standalone ---
 * crypto.pbkdf2({pass, salt, iter, length, hash, returnType})
 *   → Buffer (or hex/uint8array per returnType).
 *
 * Same primitive used internally by pw_to_keyiv (PKCS5_PBKDF2_HMAC),
 * just exposed directly with a configurable hash + length.
 */
static duk_ret_t duk_pbkdf2(duk_context *ctx)
{
    const char *pass = NULL, *hash_name = "sha256";
    const void *salt = NULL;
    duk_size_t passlen, saltlen;
    int iter = 0, length = 0;
    const EVP_MD *md;
    unsigned char *out;

    REQUIRE_OBJECT(ctx, 0, "crypto.pbkdf2 requires an options object");

    if (!duk_get_prop_string(ctx, 0, "pass"))
        RP_THROW(ctx, "crypto.pbkdf2: option 'pass' is required (string or buffer)");
    if (duk_is_string(ctx, -1))
        pass = duk_get_lstring(ctx, -1, &passlen);
    else if (duk_is_buffer_data(ctx, -1))
        pass = (const char *)duk_get_buffer_data(ctx, -1, &passlen);
    else
        RP_THROW(ctx, "crypto.pbkdf2: 'pass' must be a string or buffer");
    duk_pop(ctx);

    if (!duk_get_prop_string(ctx, 0, "salt"))
        RP_THROW(ctx, "crypto.pbkdf2: option 'salt' is required (string or buffer)");
    if (duk_is_string(ctx, -1))
        salt = duk_get_lstring(ctx, -1, &saltlen);
    else if (duk_is_buffer_data(ctx, -1))
        salt = duk_get_buffer_data(ctx, -1, &saltlen);
    else
        RP_THROW(ctx, "crypto.pbkdf2: 'salt' must be a string or buffer");
    duk_pop(ctx);

    if (!duk_get_prop_string(ctx, 0, "iter"))
        RP_THROW(ctx, "crypto.pbkdf2: option 'iter' is required (Number, iteration count)");
    iter = (int)REQUIRE_NUMBER(ctx, -1, "crypto.pbkdf2: 'iter' must be a Number");
    duk_pop(ctx);
    if (iter < 1)
        RP_THROW(ctx, "crypto.pbkdf2: 'iter' must be >= 1");

    if (!duk_get_prop_string(ctx, 0, "length"))
        RP_THROW(ctx, "crypto.pbkdf2: option 'length' is required (Number, output bytes)");
    length = (int)REQUIRE_NUMBER(ctx, -1, "crypto.pbkdf2: 'length' must be a Number");
    duk_pop(ctx);
    if (length < 1)
        RP_THROW(ctx, "crypto.pbkdf2: 'length' must be >= 1");

    if (duk_get_prop_string(ctx, 0, "hash"))
        hash_name = REQUIRE_STRING(ctx, -1, "crypto.pbkdf2: 'hash' must be a string");
    duk_pop(ctx);

    md = rc_md_from_name(hash_name);
    if (!md)
        RP_THROW(ctx, "crypto.pbkdf2: unsupported hash '%s' (use sha1, sha256, sha384, or sha512)", hash_name);

    out = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)length);
    if (!PKCS5_PBKDF2_HMAC(pass, (int)passlen, (const unsigned char *)salt,
                           (int)saltlen, iter, md, length, out))
        DUK_OPENSSL_ERROR(ctx);

    rc_finalize_buffer_buf_default(ctx, 0);
    return 1;
}

/* --- 2.2 HKDF ---
 * crypto.hkdf({ikm, salt, info, length, hash, returnType}) → Buffer.
 * Uses OpenSSL 3.x EVP_KDF.
 */
static duk_ret_t duk_hkdf(duk_context *ctx)
{
    const void *ikm = NULL, *salt = NULL, *info = NULL;
    duk_size_t ikmlen = 0, saltlen = 0, infolen = 0;
    const char *hash_name = "sha256";
    int length = 0;
    const EVP_MD *md;
    unsigned char *out;
    EVP_KDF *kdf = NULL;
    EVP_KDF_CTX *kctx = NULL;
    OSSL_PARAM params[5];
    int nparams = 0;

    REQUIRE_OBJECT(ctx, 0, "crypto.hkdf requires an options object");

    if (!duk_get_prop_string(ctx, 0, "ikm"))
        RP_THROW(ctx, "crypto.hkdf: option 'ikm' is required (input keying material, buffer/string)");
    if (duk_is_string(ctx, -1))
        ikm = duk_get_lstring(ctx, -1, &ikmlen);
    else if (duk_is_buffer_data(ctx, -1))
        ikm = duk_get_buffer_data(ctx, -1, &ikmlen);
    else
        RP_THROW(ctx, "crypto.hkdf: 'ikm' must be a string or buffer");
    duk_pop(ctx);

    if (duk_get_prop_string(ctx, 0, "salt"))
    {
        if (duk_is_string(ctx, -1))
            salt = duk_get_lstring(ctx, -1, &saltlen);
        else if (duk_is_buffer_data(ctx, -1))
            salt = duk_get_buffer_data(ctx, -1, &saltlen);
        else if (!duk_is_null(ctx, -1) && !duk_is_undefined(ctx, -1))
            RP_THROW(ctx, "crypto.hkdf: 'salt' must be a string or buffer");
    }
    duk_pop(ctx);

    if (duk_get_prop_string(ctx, 0, "info"))
    {
        if (duk_is_string(ctx, -1))
            info = duk_get_lstring(ctx, -1, &infolen);
        else if (duk_is_buffer_data(ctx, -1))
            info = duk_get_buffer_data(ctx, -1, &infolen);
        else if (!duk_is_null(ctx, -1) && !duk_is_undefined(ctx, -1))
            RP_THROW(ctx, "crypto.hkdf: 'info' must be a string or buffer");
    }
    duk_pop(ctx);

    if (!duk_get_prop_string(ctx, 0, "length"))
        RP_THROW(ctx, "crypto.hkdf: option 'length' is required (Number, output bytes)");
    length = (int)REQUIRE_NUMBER(ctx, -1, "crypto.hkdf: 'length' must be a Number");
    duk_pop(ctx);
    if (length < 1)
        RP_THROW(ctx, "crypto.hkdf: 'length' must be >= 1");

    if (duk_get_prop_string(ctx, 0, "hash"))
        hash_name = REQUIRE_STRING(ctx, -1, "crypto.hkdf: 'hash' must be a string");
    duk_pop(ctx);

    md = rc_md_from_name(hash_name);
    if (!md)
        RP_THROW(ctx, "crypto.hkdf: unsupported hash '%s'", hash_name);

    kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    if (!kdf)
        RP_THROW(ctx, "crypto.hkdf: EVP_KDF_fetch(HKDF) failed");
    kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kctx)
        RP_THROW(ctx, "crypto.hkdf: EVP_KDF_CTX_new failed");

    /* OSSL_PARAM digest takes a UTF-8 string of the digest's name. */
    params[nparams++] = OSSL_PARAM_construct_utf8_string(
        OSSL_KDF_PARAM_DIGEST, (char *)EVP_MD_get0_name(md), 0);
    params[nparams++] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_KEY, (void *)ikm, ikmlen);
    if (salt)
        params[nparams++] = OSSL_PARAM_construct_octet_string(
            OSSL_KDF_PARAM_SALT, (void *)salt, saltlen);
    if (info)
        params[nparams++] = OSSL_PARAM_construct_octet_string(
            OSSL_KDF_PARAM_INFO, (void *)info, infolen);
    params[nparams] = OSSL_PARAM_construct_end();

    out = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)length);
    if (EVP_KDF_derive(kctx, out, (size_t)length, params) <= 0)
    {
        EVP_KDF_CTX_free(kctx);
        DUK_OPENSSL_ERROR(ctx);
    }
    EVP_KDF_CTX_free(kctx);

    rc_finalize_buffer_buf_default(ctx, 0);
    return 1;
}

/* --- KMAC (NIST SP 800-185) ---
 *
 *   kmac(key, data [, variant] [, opts])
 *     variant: "kmac-128" (default) or "kmac-256"
 *     opts:    { length, customization, returnType }
 *
 * Output length defaults to 32 bytes for KMAC-128 and 64 bytes for
 * KMAC-256 (matching the natural security level).  Customization
 * string is empty by default.
 */
static duk_ret_t duk_kmac(duk_context *ctx)
{
    const void *key = NULL, *data = NULL, *custom = NULL;
    duk_size_t keylen = 0, datalen = 0, customlen = 0;
    const char *variant = "kmac-128";
    int length = -1;       /* -1 = use default for the variant */
    EVP_MAC *mac = NULL;
    EVP_MAC_CTX *mctx = NULL;
    OSSL_PARAM params[3];
    int nparams = 0;
    size_t outlen;
    int opt_idx = -1;

    if (!rc_get_key_any(ctx, 0, &key, &keylen))
        RP_THROW(ctx, "kmac: first argument (key) must be string or buffer");
    if (!rc_get_key_any(ctx, 1, &data, &datalen))
        RP_THROW(ctx, "kmac: second argument (data) must be string or buffer");

    /* 3rd arg: variant string OR opts object. */
    if (duk_is_string(ctx, 2))
        variant = duk_get_string(ctx, 2);
    else if (duk_is_object(ctx, 2) && !duk_is_buffer_data(ctx, 2) &&
             !duk_is_array(ctx, 2) && !duk_is_function(ctx, 2))
        opt_idx = 2;
    else if (!duk_is_undefined(ctx, 2) && !duk_is_null(ctx, 2))
        RP_THROW(ctx, "kmac: third argument must be a variant string or options object");

    /* 4th arg: opts object (if 3rd was variant). */
    if (opt_idx < 0 && duk_is_object(ctx, 3) && !duk_is_buffer_data(ctx, 3) &&
        !duk_is_array(ctx, 3) && !duk_is_function(ctx, 3))
        opt_idx = 3;

    if (opt_idx >= 0)
    {
        if (duk_get_prop_string(ctx, opt_idx, "length"))
            length = (int)REQUIRE_NUMBER(ctx, -1, "kmac: 'length' must be a Number");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, opt_idx, "customization"))
        {
            if (duk_is_string(ctx, -1))      custom = duk_get_lstring(ctx, -1, &customlen);
            else if (duk_is_buffer_data(ctx, -1)) custom = duk_get_buffer_data(ctx, -1, &customlen);
            else if (!duk_is_null(ctx, -1) && !duk_is_undefined(ctx, -1))
                RP_THROW(ctx, "kmac: 'customization' must be a string or buffer");
        }
        duk_pop(ctx);
    }

    /* Map variant → KMAC algorithm name + default output length. */
    const char *algo;
    int default_len;
    if (!strcmp(variant, "kmac-128") || !strcmp(variant, "KMAC-128") ||
        !strcmp(variant, "kmac128")  || !strcmp(variant, "KMAC128"))
        { algo = "KMAC-128"; default_len = 32; }
    else if (!strcmp(variant, "kmac-256") || !strcmp(variant, "KMAC-256") ||
             !strcmp(variant, "kmac256")  || !strcmp(variant, "KMAC256"))
        { algo = "KMAC-256"; default_len = 64; }
    else
        RP_THROW(ctx, "kmac: unknown variant '%s' (use 'kmac-128' or 'kmac-256')", variant);

    if (length < 0) length = default_len;
    if (length < 1) RP_THROW(ctx, "kmac: 'length' must be >= 1");

    mac = EVP_MAC_fetch(NULL, algo, NULL);
    if (!mac) RP_THROW(ctx, "kmac: EVP_MAC_fetch(%s) failed", algo);
    mctx = EVP_MAC_CTX_new(mac);
    EVP_MAC_free(mac);
    if (!mctx) RP_THROW(ctx, "kmac: EVP_MAC_CTX_new failed");

    if (custom && customlen)
        params[nparams++] = OSSL_PARAM_construct_octet_string(
            OSSL_MAC_PARAM_CUSTOM, (void *)custom, customlen);
    {
        size_t lenz = (size_t)length;
        params[nparams++] = OSSL_PARAM_construct_size_t(
            OSSL_MAC_PARAM_SIZE, &lenz);
        params[nparams] = OSSL_PARAM_construct_end();

        if (EVP_MAC_init(mctx, (const unsigned char *)key, (size_t)keylen, params) <= 0)
            { EVP_MAC_CTX_free(mctx); DUK_OPENSSL_ERROR(ctx); }
        if (EVP_MAC_update(mctx, (const unsigned char *)data, (size_t)datalen) <= 0)
            { EVP_MAC_CTX_free(mctx); DUK_OPENSSL_ERROR(ctx); }

        unsigned char *out = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)length);
        outlen = (size_t)length;
        if (EVP_MAC_final(mctx, out, &outlen, (size_t)length) <= 0)
            { EVP_MAC_CTX_free(mctx); DUK_OPENSSL_ERROR(ctx); }
    }
    EVP_MAC_CTX_free(mctx);

    if (opt_idx >= 0) rc_finalize_buffer_buf_default(ctx, opt_idx);
    return 1;
}

/* --- cSHAKE (NIST SP 800-185) ---
 *
 *   cshake128(data [, opts])
 *   cshake256(data [, opts])
 *     opts: { length, customization, functionName, returnType }
 *
 * Default length: 16 bytes for cSHAKE-128, 32 bytes for cSHAKE-256
 * (matching shake128 / shake256 defaults).  With empty N and S,
 * cSHAKE-X is identical to SHAKE-X.  In OpenSSL, cSHAKE is exposed
 * via the regular EVP_MD interface with an OSSL_DIGEST_PARAM_*
 * parameter for the function-name N and customization S strings.
 */
static duk_ret_t rc_cshake(duk_context *ctx, int bits)
{
    const void *data = NULL, *custom = NULL, *fname = NULL;
    duk_size_t datalen = 0, customlen = 0, fnamelen = 0;
    int length = (bits == 128) ? 16 : 32;
    EVP_MD *md = NULL;
    EVP_MD_CTX *mctx = NULL;
    OSSL_PARAM params[3];
    int nparams = 0;
    int opt_idx = -1;

    if (!rc_get_key_any(ctx, 0, &data, &datalen))
        RP_THROW(ctx, "cshake: first argument (data) must be string or buffer");

    if (duk_is_object(ctx, 1) && !duk_is_buffer_data(ctx, 1) &&
        !duk_is_array(ctx, 1) && !duk_is_function(ctx, 1))
    {
        opt_idx = 1;
        if (duk_get_prop_string(ctx, opt_idx, "length"))
            length = (int)REQUIRE_NUMBER(ctx, -1, "cshake: 'length' must be a Number");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, opt_idx, "customization"))
        {
            if (duk_is_string(ctx, -1))      custom = duk_get_lstring(ctx, -1, &customlen);
            else if (duk_is_buffer_data(ctx, -1)) custom = duk_get_buffer_data(ctx, -1, &customlen);
            else if (!duk_is_null(ctx, -1) && !duk_is_undefined(ctx, -1))
                RP_THROW(ctx, "cshake: 'customization' must be a string or buffer");
        }
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, opt_idx, "functionName"))
        {
            if (duk_is_string(ctx, -1))      fname = duk_get_lstring(ctx, -1, &fnamelen);
            else if (duk_is_buffer_data(ctx, -1)) fname = duk_get_buffer_data(ctx, -1, &fnamelen);
            else if (!duk_is_null(ctx, -1) && !duk_is_undefined(ctx, -1))
                RP_THROW(ctx, "cshake: 'functionName' must be a string or buffer");
        }
        duk_pop(ctx);
    }
    if (length < 1) RP_THROW(ctx, "cshake: 'length' must be >= 1");

    md = EVP_MD_fetch(NULL, bits == 128 ? "CSHAKE-128" : "CSHAKE-256", NULL);
    if (!md) RP_THROW(ctx, "cshake: EVP_MD_fetch(CSHAKE-%d) failed", bits);
    mctx = EVP_MD_CTX_new();
    if (!mctx) { EVP_MD_free(md); RP_THROW(ctx, "cshake: EVP_MD_CTX_new failed"); }

    /* cSHAKE customization (S) and function-name (N) are UTF-8
     * strings in OpenSSL's params interface. */
    if (custom && customlen)
        params[nparams++] = OSSL_PARAM_construct_utf8_string(
            OSSL_DIGEST_PARAM_CUSTOMIZATION, (char *)custom, customlen);
    if (fname && fnamelen)
        params[nparams++] = OSSL_PARAM_construct_utf8_string(
            OSSL_DIGEST_PARAM_FUNCTION_NAME, (char *)fname, fnamelen);
    params[nparams] = OSSL_PARAM_construct_end();

    if (EVP_DigestInit_ex2(mctx, md, params) <= 0)
        { EVP_MD_CTX_free(mctx); EVP_MD_free(md); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_DigestUpdate(mctx, data, (size_t)datalen) <= 0)
        { EVP_MD_CTX_free(mctx); EVP_MD_free(md); DUK_OPENSSL_ERROR(ctx); }

    unsigned char *out = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)length);
    if (EVP_DigestFinalXOF(mctx, out, (size_t)length) <= 0)
        { EVP_MD_CTX_free(mctx); EVP_MD_free(md); DUK_OPENSSL_ERROR(ctx); }
    EVP_MD_CTX_free(mctx);
    EVP_MD_free(md);

    if (opt_idx >= 0) rc_finalize_buffer_buf_default(ctx, opt_idx);
    return 1;
}

static duk_ret_t duk_cshake128(duk_context *ctx) { return rc_cshake(ctx, 128); }
static duk_ret_t duk_cshake256(duk_context *ctx) { return rc_cshake(ctx, 256); }

/* --- 2.1 EC family (P-256 / P-384 / P-521) ---
 *
 * Uses EVP_PKEY_* throughout (recommended path in OpenSSL 3.x; the
 * older EC_KEY_* API is deprecated but the EVP layer is stable).
 *
 *   ec_gen_key({curve}) → {publicKey:Buffer(SPKI DER), privateKey:Buffer(PKCS#8 DER)}
 *   ec_import_pub_key({key, curve?, format?})  → SPKI Buffer
 *   ec_import_priv_key({key, curve?, format?}) → PKCS#8 Buffer
 *   ec_export_pub_key({key, format})           → Buffer ('spki' default; 'raw' = uncompressed 04||X||Y)
 *   ec_export_priv_key({key, format})          → Buffer ('pkcs8' default; 'raw' = scalar bytes)
 *   ecdsa_sign({key, data, hash, format})      → Buffer ('der' default; 'p1363' = r||s)
 *   ecdsa_verify({key, data, signature, hash, format}) → boolean
 *   ecdh({privateKey, publicKey})              → Buffer (raw shared secret)
 */

/* Curve name → NID + group name + scalar byte size.  Returns 0 on
 * unknown curve. */
static int rc_ec_curve_from_name(const char *name, int *out_nid,
                                 const char **out_group, int *out_size)
{
    if (!name) return 0;
    if (!strcmp(name, "P-256") || !strcmp(name, "prime256v1") || !strcmp(name, "secp256r1")) {
        *out_nid = NID_X9_62_prime256v1; *out_group = "prime256v1"; *out_size = 32; return 1;
    }
    if (!strcmp(name, "P-384") || !strcmp(name, "secp384r1")) {
        *out_nid = NID_secp384r1; *out_group = "secp384r1"; *out_size = 48; return 1;
    }
    if (!strcmp(name, "P-521") || !strcmp(name, "secp521r1")) {
        *out_nid = NID_secp521r1; *out_group = "secp521r1"; *out_size = 66; return 1;
    }
    return 0;
}

/* Get EVP_PKEY's curve byte size by querying its group.  Returns 0 on
 * non-EC keys.  Used by ECDSA P1363 format conversion. */
static int rc_ec_pkey_size(EVP_PKEY *pkey)
{
    char gname[64] = {0};
    size_t gname_len = 0;
    int nid, dummy_size;
    const char *dummy_group;
    if (EVP_PKEY_get_utf8_string_param(pkey, OSSL_PKEY_PARAM_GROUP_NAME,
                                       gname, sizeof(gname), &gname_len) <= 0)
        return 0;
    if (rc_ec_curve_from_name(gname, &nid, &dummy_group, &dummy_size))
        return dummy_size;
    return 0;
}

/* Helper: pull a buffer/string opt; sets *out + *out_len. */
static int rc_get_opt_bytes(duk_context *ctx, duk_idx_t obj_idx,
                            const char *propname, const void **out, duk_size_t *out_len)
{
    int have = 0;
    if (duk_get_prop_string(ctx, obj_idx, propname))
    {
        if (duk_is_string(ctx, -1))      { *out = duk_get_lstring(ctx, -1, out_len); have = 1; }
        else if (duk_is_buffer_data(ctx, -1)) { *out = duk_get_buffer_data(ctx, -1, out_len); have = 1; }
        else if (!duk_is_null(ctx, -1) && !duk_is_undefined(ctx, -1))
            RP_THROW(ctx, "option '%s' must be a string or buffer", propname);
    }
    /* leave the value on stack; caller pops */
    return have;
}

/* Push a Buffer with the SPKI DER of pkey's public part. */
/* Push the SPKI DER of pkey's public part as a plain duktape buffer
 * (presents to JS as Uint8Array — same convention as
 * rsa_pub_encrypt / encrypt / rand etc.).  Callers wanting a Node
 * Buffer wrap can do `Buffer.from(x)` in JS. */
static void rc_push_pkey_spki(duk_context *ctx, EVP_PKEY *pkey)
{
    int spki_len = i2d_PUBKEY(pkey, NULL);
    if (spki_len <= 0) DUK_OPENSSL_ERROR(ctx);
    unsigned char *spki = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)spki_len);
    unsigned char *p = spki;
    if (i2d_PUBKEY(pkey, &p) <= 0) DUK_OPENSSL_ERROR(ctx);
}

/* Same — PKCS#8 DER of pkey's private part. */
static void rc_push_pkey_pkcs8(duk_context *ctx, EVP_PKEY *pkey)
{
    PKCS8_PRIV_KEY_INFO *p8 = EVP_PKEY2PKCS8(pkey);
    if (!p8) DUK_OPENSSL_ERROR(ctx);
    int p8_len = i2d_PKCS8_PRIV_KEY_INFO(p8, NULL);
    if (p8_len <= 0) { PKCS8_PRIV_KEY_INFO_free(p8); DUK_OPENSSL_ERROR(ctx); }
    unsigned char *buf = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)p8_len);
    unsigned char *p = buf;
    if (i2d_PKCS8_PRIV_KEY_INFO(p8, &p) <= 0) { PKCS8_PRIV_KEY_INFO_free(p8); DUK_OPENSSL_ERROR(ctx); }
    PKCS8_PRIV_KEY_INFO_free(p8);
}

/* Decode SPKI DER → EVP_PKEY*.  Caller frees. */
static EVP_PKEY *rc_pkey_from_spki(const unsigned char *spki, duk_size_t spki_len)
{
    const unsigned char *p = spki;
    return d2i_PUBKEY(NULL, &p, (long)spki_len);
}

/* Decode PKCS#8 DER → EVP_PKEY*.  Caller frees. */
static EVP_PKEY *rc_pkey_from_pkcs8(const unsigned char *p8, duk_size_t p8_len)
{
    const unsigned char *p = p8;
    PKCS8_PRIV_KEY_INFO *info = d2i_PKCS8_PRIV_KEY_INFO(NULL, &p, (long)p8_len);
    if (!info) return NULL;
    EVP_PKEY *pkey = EVP_PKCS82PKEY(info);
    PKCS8_PRIV_KEY_INFO_free(info);
    return pkey;
}

/* Build an EC EVP_PKEY (public) from raw uncompressed point bytes. */
static EVP_PKEY *rc_pkey_from_raw_pub(const char *group, const unsigned char *raw, duk_size_t raw_len)
{
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!pctx) return NULL;
    if (EVP_PKEY_fromdata_init(pctx) <= 0) { EVP_PKEY_CTX_free(pctx); return NULL; }
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, (char *)group, 0),
        OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY, (void *)raw, raw_len),
        OSSL_PARAM_construct_end()
    };
    if (EVP_PKEY_fromdata(pctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) <= 0)
        pkey = NULL;
    EVP_PKEY_CTX_free(pctx);
    return pkey;
}

/* Build an EC EVP_PKEY (private) from raw scalar bytes. */
static EVP_PKEY *rc_pkey_from_raw_priv(const char *group, const unsigned char *raw, duk_size_t raw_len)
{
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!pctx) return NULL;
    if (EVP_PKEY_fromdata_init(pctx) <= 0) { EVP_PKEY_CTX_free(pctx); return NULL; }
    BIGNUM *priv = BN_bin2bn(raw, (int)raw_len, NULL);
    if (!priv) { EVP_PKEY_CTX_free(pctx); return NULL; }
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, (char *)group, 0),
        OSSL_PARAM_construct_BN(OSSL_PKEY_PARAM_PRIV_KEY, NULL, 0),
        OSSL_PARAM_construct_end()
    };
    /* OSSL_PARAM_construct_BN doesn't accept a literal value pointer
     * directly; build the param via the bld API for portability. */
    OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
    OSSL_PARAM *p_built = NULL;
    if (!bld) { BN_free(priv); EVP_PKEY_CTX_free(pctx); return NULL; }
    OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, group, 0);
    OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PRIV_KEY, priv);
    p_built = OSSL_PARAM_BLD_to_param(bld);
    OSSL_PARAM_BLD_free(bld);
    if (!p_built) { BN_free(priv); EVP_PKEY_CTX_free(pctx); return NULL; }
    if (EVP_PKEY_fromdata(pctx, &pkey, EVP_PKEY_KEYPAIR, p_built) <= 0)
        pkey = NULL;
    OSSL_PARAM_free(p_built);
    BN_free(priv);
    EVP_PKEY_CTX_free(pctx);
    (void)params; /* silence unused */
    return pkey;
}

/* ===========================================================
 * Generic key I/O helpers — used by EC functions to match the
 * shape of the RSA functions (PEM-first, accepts both PEM and DER,
 * optional password).
 * =========================================================== */

/* Universal private-key parser: accepts PEM string, PEM in Buffer,
 * or DER Buffer (encrypted PKCS#8 supported via password).  Returns
 * EVP_PKEY*; caller frees. */
static EVP_PKEY *rc_load_priv_pkey_any(const void *data, duk_size_t len, const char *password)
{
    EVP_PKEY *pkey = NULL;
    BIO *bio;

    /* Try PEM first. */
    bio = BIO_new_mem_buf(data, (int)len);
    if (bio)
    {
        pkey = PEM_read_bio_PrivateKey(bio, NULL, pass_cb, (void *)password);
        BIO_free(bio);
        if (pkey) return pkey;
    }

    /* Fall back to encrypted PKCS#8 DER. */
    bio = BIO_new_mem_buf(data, (int)len);
    if (bio)
    {
        pkey = d2i_PKCS8PrivateKey_bio(bio, NULL, pass_cb, (void *)password);
        BIO_free(bio);
        if (pkey) return pkey;
    }

    /* Fall back to unencrypted DER (PKCS#8 or legacy SEC1/PKCS#1). */
    {
        const unsigned char *p = (const unsigned char *)data;
        pkey = d2i_AutoPrivateKey(NULL, &p, (long)len);
    }
    return pkey;
}

/* Universal public-key parser: PEM (SPKI) or DER. */
static EVP_PKEY *rc_load_pub_pkey_any(const void *data, duk_size_t len)
{
    EVP_PKEY *pkey = NULL;
    BIO *bio = BIO_new_mem_buf(data, (int)len);
    if (bio)
    {
        pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
        BIO_free(bio);
        if (pkey) return pkey;
    }
    const unsigned char *p = (const unsigned char *)data;
    pkey = d2i_PUBKEY(NULL, &p, (long)len);
    return pkey;
}

/* Push SPKI PEM string of pkey's public part. */
static void rc_push_pkey_pem_pub(duk_context *ctx, EVP_PKEY *pkey)
{
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) RP_THROW(ctx, "BIO_new failed");
    if (PEM_write_bio_PUBKEY(bio, pkey) != 1)
        { BIO_free(bio); DUK_OPENSSL_ERROR(ctx); }
    char *buf; long len = BIO_get_mem_data(bio, &buf);
    duk_push_lstring(ctx, buf, (duk_size_t)len);
    BIO_free(bio);
}

/* Push PKCS#8 PEM of pkey's private part.  Encrypted (AES-256-CBC)
 * if password is non-NULL, else unencrypted PRIVATE KEY PEM. */
static void rc_push_pkey_pem_priv(duk_context *ctx, EVP_PKEY *pkey, const char *password)
{
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) RP_THROW(ctx, "BIO_new failed");
    int ok;
    if (password)
        ok = PEM_write_bio_PKCS8PrivateKey(bio, pkey, EVP_aes_256_cbc(),
                 (char *)password, (int)strlen(password), NULL, NULL);
    else
        ok = PEM_write_bio_PKCS8PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL);
    if (!ok) { BIO_free(bio); DUK_OPENSSL_ERROR(ctx); }
    char *buf; long len = BIO_get_mem_data(bio, &buf);
    duk_push_lstring(ctx, buf, (duk_size_t)len);
    BIO_free(bio);
}

/* Push traditional-OpenSSL form (SEC1 "EC PRIVATE KEY" for EC,
 * PKCS#1 "RSA PRIVATE KEY" for RSA, etc.) of pkey's private part.
 * Encrypted if password is non-NULL.  Returns 1 on success, 0 if
 * the key type has no traditional form (e.g. X25519/Ed25519). */
static int rc_push_pkey_pem_traditional(duk_context *ctx, EVP_PKEY *pkey, const char *password)
{
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) RP_THROW(ctx, "BIO_new failed");
    int ok;
    if (password)
        ok = PEM_write_bio_PrivateKey_traditional(bio, pkey, EVP_aes_256_cbc(),
                 (unsigned char *)password, (int)strlen(password), NULL, NULL);
    else
        ok = PEM_write_bio_PrivateKey_traditional(bio, pkey, NULL, NULL, 0, NULL, NULL);
    if (!ok) { BIO_free(bio); return 0; }
    char *buf; long len = BIO_get_mem_data(bio, &buf);
    duk_push_lstring(ctx, buf, (duk_size_t)len);
    BIO_free(bio);
    return 1;
}

/* Common: read a "key" argument from positional slot or opts.
 * Stack slot stays live; caller pops at end.  Returns 1 if found. */
static int rc_get_key_any(duk_context *ctx, duk_idx_t pos_idx,
                          const void **out, duk_size_t *out_len)
{
    if (duk_is_string(ctx, pos_idx))
    {
        *out = duk_get_lstring(ctx, pos_idx, out_len);
        return 1;
    }
    if (duk_is_buffer_data(ctx, pos_idx))
    {
        *out = duk_get_buffer_data(ctx, pos_idx, out_len);
        return 1;
    }
    return 0;
}

/* --- ec_gen_key([curve][, password]) ---
 *   ec_gen_key()                          → P-256, unencrypted PEMs
 *   ec_gen_key("P-384")                   → P-384, unencrypted PEMs
 *   ec_gen_key("P-384", "pw")             → P-384, private PEMs encrypted with "pw"
 *   ec_gen_key({curve:"P-384", password:"pw"})
 * Returns {public, private, ec_private} — all PEM strings (mirrors
 * rsa_gen_key's {public, private, rsa_public, rsa_private} shape,
 * minus the SEC1 EC PUBLIC KEY form which OpenSSL doesn't define). */
static duk_ret_t duk_ec_gen_key(duk_context *ctx)
{
    const char *curve_name = "P-256";   /* default */
    const char *password = NULL;
    int nid, size;
    const char *group_name;
    EVP_PKEY_CTX *pctx = NULL;
    EVP_PKEY *pkey = NULL;

    /* Two calling styles: opts object or positional (curve, password). */
    if (duk_is_object(ctx, 0) && !duk_is_buffer_data(ctx, 0) &&
        !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0))
    {
        if (duk_get_prop_string(ctx, 0, "curve"))
            curve_name = REQUIRE_STRING(ctx, -1, "ec_gen_key: 'curve' must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "password"))
            password = REQUIRE_STRING(ctx, -1, "ec_gen_key: 'password' must be a string");
        duk_pop(ctx);
    }
    else
    {
        if (duk_is_string(ctx, 0)) curve_name = duk_get_string(ctx, 0);
        else if (!duk_is_undefined(ctx, 0) && !duk_is_null(ctx, 0))
            RP_THROW(ctx, "ec_gen_key: first argument must be a curve name string or options object");
        if (duk_is_string(ctx, 1)) password = duk_get_string(ctx, 1);
        else if (!duk_is_undefined(ctx, 1) && !duk_is_null(ctx, 1))
            RP_THROW(ctx, "ec_gen_key: second argument must be a password string");
    }

    if (!rc_ec_curve_from_name(curve_name, &nid, &group_name, &size))
        RP_THROW(ctx, "ec_gen_key: unsupported curve '%s' (use P-256, P-384, or P-521)", curve_name);

    pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!pctx) DUK_OPENSSL_ERROR(ctx);
    if (EVP_PKEY_keygen_init(pctx) <= 0) { EVP_PKEY_CTX_free(pctx); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, nid) <= 0)
        { EVP_PKEY_CTX_free(pctx); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0)
        { EVP_PKEY_CTX_free(pctx); DUK_OPENSSL_ERROR(ctx); }
    EVP_PKEY_CTX_free(pctx);

    duk_push_object(ctx);
    rc_push_pkey_pem_pub(ctx, pkey);
    duk_put_prop_string(ctx, -2, "public");
    rc_push_pkey_pem_priv(ctx, pkey, password);
    duk_put_prop_string(ctx, -2, "private");
    if (rc_push_pkey_pem_traditional(ctx, pkey, password))
        duk_put_prop_string(ctx, -2, "ec_private");
    EVP_PKEY_free(pkey);
    return 1;
}

/* --- ec_import_pub_key(pub_or_raw [, curve]) ---
 *   ec_import_pub_key(pem_or_der)        → re-canonicalize to SPKI PEM
 *   ec_import_pub_key(raw_bytes, "P-256") → build SPKI PEM from 04||X||Y point
 *   ec_import_pub_key({key, curve, format:"raw"|"spki"})  opts form
 * Returns the canonical SPKI PEM string. */
static duk_ret_t duk_ec_import_pub_key(duk_context *ctx)
{
    const void *key = NULL; duk_size_t key_len = 0;
    const char *format = NULL, *curve = NULL;

    /* Opts-object form first. */
    if (duk_is_object(ctx, 0) && !duk_is_buffer_data(ctx, 0) &&
        !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0) &&
        !duk_is_string(ctx, 0))
    {
        if (!rc_get_opt_bytes(ctx, 0, "key", &key, &key_len))
            RP_THROW(ctx, "ec_import_pub_key: 'key' is required");
        if (duk_get_prop_string(ctx, 0, "format"))
            format = REQUIRE_STRING(ctx, -1, "ec_import_pub_key: 'format' must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "curve"))
            curve = REQUIRE_STRING(ctx, -1, "ec_import_pub_key: 'curve' must be a string");
        duk_pop(ctx);
    }
    else
    {
        if (!rc_get_key_any(ctx, 0, &key, &key_len))
            RP_THROW(ctx, "ec_import_pub_key: first argument must be string or buffer");
        if (duk_is_string(ctx, 1)) curve = duk_get_string(ctx, 1);
        /* If curve is provided positionally, assume raw format. */
        if (curve) format = "raw";
    }
    if (!format) format = "spki";

    EVP_PKEY *pkey = NULL;
    if (!strcmp(format, "spki") || !strcmp(format, "pem") || !strcmp(format, "der"))
    {
        pkey = rc_load_pub_pkey_any(key, key_len);
        if (!pkey) RP_THROW(ctx, "ec_import_pub_key: failed to parse PEM/DER public key");
    }
    else if (!strcmp(format, "raw"))
    {
        int nid, size;
        const char *group;
        if (!curve)
            RP_THROW(ctx, "ec_import_pub_key: 'curve' is required when format='raw'");
        if (!rc_ec_curve_from_name(curve, &nid, &group, &size))
            RP_THROW(ctx, "ec_import_pub_key: unsupported curve '%s'", curve);
        pkey = rc_pkey_from_raw_pub(group, (const unsigned char *)key, key_len);
        if (!pkey) RP_THROW(ctx, "ec_import_pub_key: failed to build key from raw point (wrong length or invalid point?)");
    }
    else
        RP_THROW(ctx, "ec_import_pub_key: unknown format '%s'", format);

    rc_push_pkey_pem_pub(ctx, pkey);
    EVP_PKEY_free(pkey);
    return 1;
}

/* --- ec_import_priv_key(priv [, oldpass [, newpass]]) ---
 *   ec_import_priv_key(pem)              → {public, private, ec_private} unencrypted
 *   ec_import_priv_key(pem, "OLD")       → decrypt input with OLD, output unencrypted
 *   ec_import_priv_key(pem, "OLD","NEW") → re-encrypt output with NEW
 *   ec_import_priv_key(pem, {decryptPassword, encryptPassword})
 *   ec_import_priv_key({key, curve, format:"raw"})  raw bytes + curve
 * Mirrors rsa_import_priv_key. */
static duk_ret_t duk_ec_import_priv_key(duk_context *ctx)
{
    const void *key = NULL; duk_size_t key_len = 0;
    const char *inpasswd = NULL, *outpasswd = NULL;
    const char *format = NULL, *curve = NULL;
    EVP_PKEY *pkey = NULL;

    /* Detect call shape. */
    if (duk_is_object(ctx, 0) && !duk_is_buffer_data(ctx, 0) &&
        !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0) &&
        !duk_is_string(ctx, 0))
    {
        if (!rc_get_opt_bytes(ctx, 0, "key", &key, &key_len))
            RP_THROW(ctx, "ec_import_priv_key: 'key' is required");
        if (duk_get_prop_string(ctx, 0, "format"))
            format = REQUIRE_STRING(ctx, -1, "ec_import_priv_key: 'format' must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "curve"))
            curve = REQUIRE_STRING(ctx, -1, "ec_import_priv_key: 'curve' must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "decryptPassword"))
            inpasswd = REQUIRE_STRING(ctx, -1, "ec_import_priv_key: 'decryptPassword' must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "encryptPassword"))
            outpasswd = REQUIRE_STRING(ctx, -1, "ec_import_priv_key: 'encryptPassword' must be a string");
        duk_pop(ctx);
    }
    else
    {
        if (!rc_get_key_any(ctx, 0, &key, &key_len))
            RP_THROW(ctx, "ec_import_priv_key: first argument must be string or buffer");
        if (duk_is_string(ctx, 1))      inpasswd  = duk_get_string(ctx, 1);
        else if (duk_is_object(ctx, 1) && !duk_is_null(ctx, 1) && !duk_is_undefined(ctx, 1))
            RP_THROW(ctx, "ec_import_priv_key: second argument must be a password string or null");
        if (duk_is_string(ctx, 2))      outpasswd = duk_get_string(ctx, 2);
    }

    if (format && !strcmp(format, "raw"))
    {
        int nid, size;
        const char *group;
        if (!curve)
            RP_THROW(ctx, "ec_import_priv_key: 'curve' is required when format='raw'");
        if (!rc_ec_curve_from_name(curve, &nid, &group, &size))
            RP_THROW(ctx, "ec_import_priv_key: unsupported curve '%s'", curve);
        pkey = rc_pkey_from_raw_priv(group, (const unsigned char *)key, key_len);
        if (!pkey) RP_THROW(ctx, "ec_import_priv_key: failed to build key from raw scalar");
    }
    else
    {
        pkey = rc_load_priv_pkey_any(key, key_len, inpasswd);
        if (!pkey) RP_THROW(ctx, "ec_import_priv_key: failed to parse PEM/DER private key%s",
                            inpasswd ? " (wrong password?)" : "");
    }

    duk_push_object(ctx);
    rc_push_pkey_pem_pub(ctx, pkey);
    duk_put_prop_string(ctx, -2, "public");
    rc_push_pkey_pem_priv(ctx, pkey, outpasswd);
    duk_put_prop_string(ctx, -2, "private");
    if (rc_push_pkey_pem_traditional(ctx, pkey, outpasswd))
        duk_put_prop_string(ctx, -2, "ec_private");
    EVP_PKEY_free(pkey);
    return 1;
}

/* Convert DER ECDSA signature → IEEE P1363 (r||s) with `size`-byte
 * components.  Caller provides out buffer of 2*size bytes. */
static int rc_ecdsa_der_to_p1363(const unsigned char *der, size_t der_len,
                                 int size, unsigned char *out)
{
    const unsigned char *p = der;
    ECDSA_SIG *sig = d2i_ECDSA_SIG(NULL, &p, (long)der_len);
    if (!sig) return 0;
    const BIGNUM *r, *s;
    ECDSA_SIG_get0(sig, &r, &s);
    if (BN_bn2binpad(r, out,        size) < 0 ||
        BN_bn2binpad(s, out + size, size) < 0) { ECDSA_SIG_free(sig); return 0; }
    ECDSA_SIG_free(sig);
    return 1;
}

/* Convert IEEE P1363 (r||s) → DER ECDSA signature.  `*out_len` set
 * to actual DER length; caller responsible for OPENSSL_free(*out). */
static int rc_ecdsa_p1363_to_der(const unsigned char *p1363, int size,
                                 unsigned char **out, int *out_len)
{
    BIGNUM *r = BN_bin2bn(p1363,        size, NULL);
    BIGNUM *s = BN_bin2bn(p1363 + size, size, NULL);
    if (!r || !s) { BN_free(r); BN_free(s); return 0; }
    ECDSA_SIG *sig = ECDSA_SIG_new();
    if (!sig) { BN_free(r); BN_free(s); return 0; }
    ECDSA_SIG_set0(sig, r, s);   /* takes ownership of r,s */
    int len = i2d_ECDSA_SIG(sig, NULL);
    if (len <= 0) { ECDSA_SIG_free(sig); return 0; }
    *out = (unsigned char *)OPENSSL_malloc(len);
    if (!*out) { ECDSA_SIG_free(sig); return 0; }
    unsigned char *q = *out;
    if (i2d_ECDSA_SIG(sig, &q) <= 0) { OPENSSL_free(*out); ECDSA_SIG_free(sig); return 0; }
    *out_len = len;
    ECDSA_SIG_free(sig);
    return 1;
}

/* --- ecdsa_sign(message, private_key [, password|opts]) ---
 *   ecdsa_sign(msg, priv)              → DER signature, sha256
 *   ecdsa_sign(msg, priv, "pw")        → priv decrypted with "pw"
 *   ecdsa_sign(msg, priv, {hash, format, password})
 * Mirrors rsa_sign. */
static duk_ret_t duk_ecdsa_sign(duk_context *ctx)
{
    const void *keybytes = NULL, *data = NULL;
    duk_size_t keylen = 0, datalen = 0;
    const char *hash_name = "sha256", *format = "der", *password = NULL;
    const EVP_MD *md;
    EVP_PKEY *pkey;
    EVP_MD_CTX *mctx = NULL;
    size_t siglen = 0;
    unsigned char *sig_der;

    if (!rc_get_key_any(ctx, 0, &data, &datalen))
        RP_THROW(ctx, "ecdsa_sign: first argument (data) must be string or buffer");
    if (!rc_get_key_any(ctx, 1, &keybytes, &keylen))
        RP_THROW(ctx, "ecdsa_sign: second argument (private_key) must be string or buffer");

    if (duk_is_string(ctx, 2))
        password = duk_get_string(ctx, 2);
    else if (duk_is_object(ctx, 2) && !duk_is_buffer_data(ctx, 2) &&
             !duk_is_array(ctx, 2) && !duk_is_function(ctx, 2))
    {
        if (duk_get_prop_string(ctx, 2, "hash"))
            hash_name = REQUIRE_STRING(ctx, -1, "ecdsa_sign: 'hash' must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 2, "format"))
            format = REQUIRE_STRING(ctx, -1, "ecdsa_sign: 'format' must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 2, "password"))
            password = REQUIRE_STRING(ctx, -1, "ecdsa_sign: 'password' must be a string");
        duk_pop(ctx);
    }
    else if (!duk_is_undefined(ctx, 2) && !duk_is_null(ctx, 2))
        RP_THROW(ctx, "ecdsa_sign: third argument must be a password string or options object");

    md = rc_md_from_name(hash_name);
    if (!md) RP_THROW(ctx, "ecdsa_sign: unsupported hash '%s'", hash_name);
    if (strcmp(format, "der") && strcmp(format, "p1363"))
        RP_THROW(ctx, "ecdsa_sign: unknown format '%s' (use 'der' or 'p1363')", format);

    pkey = rc_load_priv_pkey_any(keybytes, keylen, password);
    if (!pkey) RP_THROW(ctx, "ecdsa_sign: failed to parse private key%s",
                       password ? " (wrong password?)" : "");

    mctx = EVP_MD_CTX_new();
    if (!mctx) { EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_DigestSignInit(mctx, NULL, md, NULL, pkey) <= 0)
        { EVP_MD_CTX_free(mctx); EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_DigestSignUpdate(mctx, data, (size_t)datalen) <= 0)
        { EVP_MD_CTX_free(mctx); EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_DigestSignFinal(mctx, NULL, &siglen) <= 0)
        { EVP_MD_CTX_free(mctx); EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
    sig_der = (unsigned char *)OPENSSL_malloc(siglen);
    if (!sig_der) { EVP_MD_CTX_free(mctx); EVP_PKEY_free(pkey); RP_THROW(ctx, "ecdsa_sign: oom"); }
    if (EVP_DigestSignFinal(mctx, sig_der, &siglen) <= 0)
        { OPENSSL_free(sig_der); EVP_MD_CTX_free(mctx); EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
    EVP_MD_CTX_free(mctx);

    if (!strcmp(format, "der"))
    {
        void *out = duk_push_fixed_buffer(ctx, (duk_size_t)siglen);
        memcpy(out, sig_der, siglen);
        OPENSSL_free(sig_der);
    }
    else /* p1363 */
    {
        int size = rc_ec_pkey_size(pkey);
        if (size == 0) { OPENSSL_free(sig_der); EVP_PKEY_free(pkey); RP_THROW(ctx, "ecdsa_sign: not an EC key"); }
        unsigned char *out = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)(2 * size));
        if (!rc_ecdsa_der_to_p1363(sig_der, siglen, size, out))
            { OPENSSL_free(sig_der); EVP_PKEY_free(pkey); RP_THROW(ctx, "ecdsa_sign: DER→P1363 conversion failed"); }
        OPENSSL_free(sig_der);
    }
    EVP_PKEY_free(pkey);
    return 1;
}

/* --- ecdsa_verify(data, public_key, signature [, opts]) ---
 *   ecdsa_verify(msg, pub, sig)
 *   ecdsa_verify(msg, pub, sig, {hash, format})
 * Mirrors rsa_verify. */
static duk_ret_t duk_ecdsa_verify(duk_context *ctx)
{
    const void *keybytes = NULL, *data = NULL, *sig = NULL;
    duk_size_t keylen = 0, datalen = 0, siglen = 0;
    const char *hash_name = "sha256", *format = "der";
    const EVP_MD *md;
    EVP_PKEY *pkey;
    EVP_MD_CTX *mctx = NULL;
    unsigned char *sig_der = NULL;
    int sig_der_len = 0;
    int verify_result;

    if (!rc_get_key_any(ctx, 0, &data, &datalen))
        RP_THROW(ctx, "ecdsa_verify: first argument (data) must be string or buffer");
    if (!rc_get_key_any(ctx, 1, &keybytes, &keylen))
        RP_THROW(ctx, "ecdsa_verify: second argument (public_key) must be string or buffer");
    if (!rc_get_key_any(ctx, 2, &sig, &siglen))
        RP_THROW(ctx, "ecdsa_verify: third argument (signature) must be string or buffer");

    if (duk_is_object(ctx, 3) && !duk_is_buffer_data(ctx, 3) &&
        !duk_is_array(ctx, 3) && !duk_is_function(ctx, 3))
    {
        if (duk_get_prop_string(ctx, 3, "hash"))
            hash_name = REQUIRE_STRING(ctx, -1, "ecdsa_verify: 'hash' must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 3, "format"))
            format = REQUIRE_STRING(ctx, -1, "ecdsa_verify: 'format' must be a string");
        duk_pop(ctx);
    }

    md = rc_md_from_name(hash_name);
    if (!md) RP_THROW(ctx, "ecdsa_verify: unsupported hash '%s'", hash_name);

    pkey = rc_load_pub_pkey_any(keybytes, keylen);
    if (!pkey) RP_THROW(ctx, "ecdsa_verify: failed to parse public key");

    /* If signature is in P1363 format, convert to DER for OpenSSL. */
    const unsigned char *sig_use = (const unsigned char *)sig;
    int sig_use_len = (int)siglen;
    if (!strcmp(format, "p1363"))
    {
        int size = rc_ec_pkey_size(pkey);
        if (size == 0 || (int)siglen != 2 * size)
            { EVP_PKEY_free(pkey); RP_THROW(ctx, "crypto.ecdsa_verify: P1363 signature length mismatch (got %d, expected %d for this curve)", (int)siglen, 2*size); }
        if (!rc_ecdsa_p1363_to_der((const unsigned char *)sig, size, &sig_der, &sig_der_len))
            { EVP_PKEY_free(pkey); RP_THROW(ctx, "crypto.ecdsa_verify: P1363→DER conversion failed"); }
        sig_use = sig_der;
        sig_use_len = sig_der_len;
    }
    else if (strcmp(format, "der"))
        { EVP_PKEY_free(pkey); RP_THROW(ctx, "crypto.ecdsa_verify: unknown format '%s' (use 'der' or 'p1363')", format); }

    mctx = EVP_MD_CTX_new();
    if (!mctx) { if (sig_der) OPENSSL_free(sig_der); EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_DigestVerifyInit(mctx, NULL, md, NULL, pkey) <= 0)
        { EVP_MD_CTX_free(mctx); if (sig_der) OPENSSL_free(sig_der); EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_DigestVerifyUpdate(mctx, data, (size_t)datalen) <= 0)
        { EVP_MD_CTX_free(mctx); if (sig_der) OPENSSL_free(sig_der); EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
    verify_result = EVP_DigestVerifyFinal(mctx, sig_use, (size_t)sig_use_len);
    EVP_MD_CTX_free(mctx);
    if (sig_der) OPENSSL_free(sig_der);
    EVP_PKEY_free(pkey);

    duk_push_boolean(ctx, verify_result == 1);
    return 1;
}

/* --- ecdh(private_key, public_key [, password]) ---
 *   ecdh(priv, pub)            → shared secret bytes
 *   ecdh(priv, pub, "pw")      → priv decrypted with "pw"
 *   ecdh({private, public, password, returnType})
 * Returns the raw X-coordinate; pass through hkdf for a real key. */
static duk_ret_t duk_ecdh(duk_context *ctx)
{
    const void *priv_bytes = NULL, *pub_bytes = NULL;
    duk_size_t priv_len = 0, pub_len = 0;
    const char *password = NULL;
    EVP_PKEY *priv = NULL, *pub = NULL;
    EVP_PKEY_CTX *pctx = NULL;
    size_t secret_len = 0;
    int opt_idx = -1;

    if (duk_is_object(ctx, 0) && !duk_is_buffer_data(ctx, 0) &&
        !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0) &&
        !duk_is_string(ctx, 0))
    {
        opt_idx = 0;
        if (duk_get_prop_string(ctx, 0, "private"))
        {
            if (duk_is_string(ctx, -1))      priv_bytes = duk_get_lstring(ctx, -1, &priv_len);
            else if (duk_is_buffer_data(ctx, -1)) priv_bytes = duk_get_buffer_data(ctx, -1, &priv_len);
        }
        if (!priv_bytes) RP_THROW(ctx, "ecdh: 'private' is required (string/buffer)");
        if (duk_get_prop_string(ctx, 0, "public"))
        {
            if (duk_is_string(ctx, -1))      pub_bytes = duk_get_lstring(ctx, -1, &pub_len);
            else if (duk_is_buffer_data(ctx, -1)) pub_bytes = duk_get_buffer_data(ctx, -1, &pub_len);
        }
        if (!pub_bytes) RP_THROW(ctx, "ecdh: 'public' is required (string/buffer)");
        if (duk_get_prop_string(ctx, 0, "password"))
            password = REQUIRE_STRING(ctx, -1, "ecdh: 'password' must be a string");
        duk_pop(ctx);
    }
    else
    {
        if (!rc_get_key_any(ctx, 0, &priv_bytes, &priv_len))
            RP_THROW(ctx, "ecdh: first argument (private_key) must be string or buffer");
        if (!rc_get_key_any(ctx, 1, &pub_bytes, &pub_len))
            RP_THROW(ctx, "ecdh: second argument (public_key) must be string or buffer");
        if (duk_is_string(ctx, 2)) password = duk_get_string(ctx, 2);
    }

    priv = rc_load_priv_pkey_any(priv_bytes, priv_len, password);
    if (!priv) RP_THROW(ctx, "ecdh: failed to parse private key%s",
                       password ? " (wrong password?)" : "");
    pub = rc_load_pub_pkey_any(pub_bytes, pub_len);
    if (!pub) { EVP_PKEY_free(priv); RP_THROW(ctx, "ecdh: failed to parse public key"); }

    pctx = EVP_PKEY_CTX_new(priv, NULL);
    if (!pctx) { EVP_PKEY_free(priv); EVP_PKEY_free(pub); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_PKEY_derive_init(pctx) <= 0 ||
        EVP_PKEY_derive_set_peer(pctx, pub) <= 0 ||
        EVP_PKEY_derive(pctx, NULL, &secret_len) <= 0)
        { EVP_PKEY_CTX_free(pctx); EVP_PKEY_free(priv); EVP_PKEY_free(pub); DUK_OPENSSL_ERROR(ctx); }

    unsigned char *out = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)secret_len);
    if (EVP_PKEY_derive(pctx, out, &secret_len) <= 0)
        { EVP_PKEY_CTX_free(pctx); EVP_PKEY_free(priv); EVP_PKEY_free(pub); DUK_OPENSSL_ERROR(ctx); }

    EVP_PKEY_CTX_free(pctx);
    EVP_PKEY_free(priv);
    EVP_PKEY_free(pub);

    if (opt_idx >= 0) rc_finalize_buffer_buf_default(ctx, opt_idx);
    return 1;
}

/* --- ec_components(key) ---
 * Returns {curve, x, y} for an EC public key or {curve, x, y, scalar}
 * for a private key.  All numeric fields are hex strings (matches the
 * rsa_components convention). */
static duk_ret_t duk_ec_components(duk_context *ctx)
{
    const void *key = NULL; duk_size_t key_len = 0;
    EVP_PKEY *pkey = NULL;
    int is_private = 0;
    char *gname_buf;
    size_t gname_len = 0;
    BIGNUM *x = NULL, *y = NULL, *scalar = NULL;
    char *hex = NULL;

    if (!rc_get_key_any(ctx, 0, &key, &key_len))
        RP_THROW(ctx, "ec_components: argument must be string or buffer");

    /* Try as private first, then as public. */
    pkey = rc_load_priv_pkey_any(key, key_len, NULL);
    if (pkey) is_private = 1;
    else      pkey = rc_load_pub_pkey_any(key, key_len);
    if (!pkey) RP_THROW(ctx, "ec_components: failed to parse key");

    if (EVP_PKEY_get_group_name(pkey, NULL, 0, &gname_len) <= 0 || gname_len == 0)
        { EVP_PKEY_free(pkey); RP_THROW(ctx, "ec_components: not an EC key"); }
    gname_buf = (char *)OPENSSL_malloc(gname_len + 1);
    if (!gname_buf) { EVP_PKEY_free(pkey); RP_THROW(ctx, "ec_components: oom"); }
    if (EVP_PKEY_get_group_name(pkey, gname_buf, gname_len + 1, &gname_len) <= 0)
        { OPENSSL_free(gname_buf); EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }

    duk_push_object(ctx);
    /* Translate OpenSSL group name to friendly "P-256" etc. */
    {
        const char *friendly = gname_buf;
        if      (!strcmp(gname_buf, "prime256v1")) friendly = "P-256";
        else if (!strcmp(gname_buf, "secp384r1"))  friendly = "P-384";
        else if (!strcmp(gname_buf, "secp521r1"))  friendly = "P-521";
        duk_push_string(ctx, friendly);
        duk_put_prop_string(ctx, -2, "curve");
    }
    OPENSSL_free(gname_buf);

    /* X / Y from OSSL_PKEY_PARAM_EC_PUB_X / Y (BIGNUM params). */
    if (EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_EC_PUB_X, &x) <= 0 ||
        EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_EC_PUB_Y, &y) <= 0)
        { BN_free(x); BN_free(y); EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
    hex = BN_bn2hex(x); duk_push_string(ctx, hex); duk_put_prop_string(ctx, -2, "x"); OPENSSL_free(hex);
    hex = BN_bn2hex(y); duk_push_string(ctx, hex); duk_put_prop_string(ctx, -2, "y"); OPENSSL_free(hex);
    BN_free(x); BN_free(y);

    if (is_private)
    {
        if (EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_PRIV_KEY, &scalar) <= 0)
            { EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
        hex = BN_bn2hex(scalar);
        duk_push_string(ctx, hex);
        duk_put_prop_string(ctx, -2, "scalar");
        OPENSSL_free(hex);
        BN_free(scalar);
    }
    EVP_PKEY_free(pkey);
    return 1;
}

/* --- pemToDer(pem) → Buffer ---
 * Strip any PEM headers/whitespace and base64-decode the body. */
static duk_ret_t duk_pem_to_der(duk_context *ctx)
{
    const char *pem = NULL; duk_size_t pem_len = 0;
    if (duk_is_string(ctx, 0))           pem = duk_get_lstring(ctx, 0, &pem_len);
    else if (duk_is_buffer_data(ctx, 0)) pem = (const char *)duk_get_buffer_data(ctx, 0, &pem_len);
    else RP_THROW(ctx, "pemToDer: argument must be a string or buffer");

    BIO *bio = BIO_new_mem_buf(pem, (int)pem_len);
    if (!bio) RP_THROW(ctx, "pemToDer: BIO_new_mem_buf failed");
    char *name = NULL, *header = NULL;
    unsigned char *data = NULL;
    long dlen = 0;
    if (PEM_read_bio(bio, &name, &header, &data, &dlen) != 1)
        { BIO_free(bio); RP_THROW(ctx, "pemToDer: not a valid PEM block"); }
    BIO_free(bio);
    OPENSSL_free(name);
    OPENSSL_free(header);

    void *out = duk_push_fixed_buffer(ctx, (duk_size_t)dlen);
    memcpy(out, data, dlen);
    OPENSSL_free(data);
    return 1;
}

/* --- derToPem(der, type) → string ---
 * Wrap DER bytes in PEM headers with the given type label. */
static duk_ret_t duk_der_to_pem(duk_context *ctx)
{
    const void *der = NULL; duk_size_t der_len = 0;
    const char *type = NULL;
    if (!rc_get_key_any(ctx, 0, &der, &der_len))
        RP_THROW(ctx, "derToPem: first argument (DER bytes) must be string or buffer");
    type = REQUIRE_STRING(ctx, 1, "derToPem: second argument (type) must be a string (e.g. 'PUBLIC KEY', 'CERTIFICATE')");

    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) RP_THROW(ctx, "derToPem: BIO_new failed");
    ERR_clear_error();   /* don't report stale errors from earlier PEM-detect probes */
    /* PEM_write_bio returns the encoded byte count, not 1, in OpenSSL 4.0. */
    if (PEM_write_bio(bio, type, "", (unsigned char *)der, (long)der_len) <= 0)
        { BIO_free(bio); DUK_OPENSSL_ERROR(ctx); }
    char *buf; long len = BIO_get_mem_data(bio, &buf);
    duk_push_lstring(ctx, buf, (duk_size_t)len);
    BIO_free(bio);
    return 1;
}

/* ===========================================================
 * Tier 3 additions: X25519, Ed25519, scrypt
 *
 * X25519/Ed25519 functions mirror the EC family's calling
 * convention (positional + opts forms, PEM output, password/rekey
 * support, *_components introspection).
 * =========================================================== */

/* Generate an EVP_PKEY of the given simple type (X25519/ED25519). */
static EVP_PKEY *rc_keygen_simple(int pkey_type)
{
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(pkey_type, NULL);
    if (!pctx) return NULL;
    if (EVP_PKEY_keygen_init(pctx) <= 0) { EVP_PKEY_CTX_free(pctx); return NULL; }
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0) pkey = NULL;
    EVP_PKEY_CTX_free(pctx);
    return pkey;
}

/* --- gen_key([password]) / gen_key({password}) → {public, private} PEMs --- */
static duk_ret_t rc_25519_gen_key(duk_context *ctx, int pkey_type, const char *fname)
{
    const char *password = NULL;

    if (duk_is_object(ctx, 0) && !duk_is_buffer_data(ctx, 0) &&
        !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0) &&
        !duk_is_string(ctx, 0))
    {
        if (duk_get_prop_string(ctx, 0, "password"))
            password = REQUIRE_STRING(ctx, -1, "password must be a string");
        duk_pop(ctx);
    }
    else if (duk_is_string(ctx, 0))
        password = duk_get_string(ctx, 0);
    else if (!duk_is_undefined(ctx, 0) && !duk_is_null(ctx, 0))
        RP_THROW(ctx, "crypto.%s: first argument must be a password string or options object", fname);

    EVP_PKEY *pkey = rc_keygen_simple(pkey_type);
    if (!pkey) RP_THROW(ctx, "crypto.%s: keygen failed", fname);
    duk_push_object(ctx);
    rc_push_pkey_pem_pub(ctx, pkey);
    duk_put_prop_string(ctx, -2, "public");
    rc_push_pkey_pem_priv(ctx, pkey, password);
    duk_put_prop_string(ctx, -2, "private");
    EVP_PKEY_free(pkey);
    return 1;
}

/* --- import_pub_key(pub) or ({key, format:"raw"}) → SPKI PEM --- */
static duk_ret_t rc_25519_import_pub_key(duk_context *ctx, int pkey_type, const char *fname)
{
    const void *key = NULL; duk_size_t key_len = 0;
    const char *format = NULL;
    EVP_PKEY *pkey = NULL;

    if (duk_is_object(ctx, 0) && !duk_is_buffer_data(ctx, 0) &&
        !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0) &&
        !duk_is_string(ctx, 0))
    {
        if (!rc_get_opt_bytes(ctx, 0, "key", &key, &key_len))
            RP_THROW(ctx, "crypto.%s: 'key' is required", fname);
        if (duk_get_prop_string(ctx, 0, "format"))
            format = REQUIRE_STRING(ctx, -1, "format must be a string");
        duk_pop(ctx);
    }
    else
    {
        if (!rc_get_key_any(ctx, 0, &key, &key_len))
            RP_THROW(ctx, "crypto.%s: first argument must be string or buffer", fname);
    }

    if (format && !strcmp(format, "raw"))
        pkey = EVP_PKEY_new_raw_public_key(pkey_type, NULL,
                                           (const unsigned char *)key, (size_t)key_len);
    else
        pkey = rc_load_pub_pkey_any(key, key_len);

    if (!pkey) RP_THROW(ctx, "crypto.%s: failed to parse public key", fname);
    rc_push_pkey_pem_pub(ctx, pkey);
    EVP_PKEY_free(pkey);
    return 1;
}

/* --- import_priv_key(priv [, oldpass [, newpass]]) → {public, private} --- */
static duk_ret_t rc_25519_import_priv_key(duk_context *ctx, int pkey_type, const char *fname)
{
    const void *key = NULL; duk_size_t key_len = 0;
    const char *inpasswd = NULL, *outpasswd = NULL, *format = NULL;
    EVP_PKEY *pkey = NULL;

    if (duk_is_object(ctx, 0) && !duk_is_buffer_data(ctx, 0) &&
        !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0) &&
        !duk_is_string(ctx, 0))
    {
        if (!rc_get_opt_bytes(ctx, 0, "key", &key, &key_len))
            RP_THROW(ctx, "crypto.%s: 'key' is required", fname);
        if (duk_get_prop_string(ctx, 0, "format"))
            format = REQUIRE_STRING(ctx, -1, "format must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "decryptPassword"))
            inpasswd = REQUIRE_STRING(ctx, -1, "decryptPassword must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "encryptPassword"))
            outpasswd = REQUIRE_STRING(ctx, -1, "encryptPassword must be a string");
        duk_pop(ctx);
    }
    else
    {
        if (!rc_get_key_any(ctx, 0, &key, &key_len))
            RP_THROW(ctx, "crypto.%s: first argument must be string or buffer", fname);
        if (duk_is_string(ctx, 1)) inpasswd  = duk_get_string(ctx, 1);
        if (duk_is_string(ctx, 2)) outpasswd = duk_get_string(ctx, 2);
    }

    if (format && !strcmp(format, "raw"))
        pkey = EVP_PKEY_new_raw_private_key(pkey_type, NULL,
                                            (const unsigned char *)key, (size_t)key_len);
    else
    {
        pkey = rc_load_priv_pkey_any(key, key_len, inpasswd);
        if (!pkey) RP_THROW(ctx, "crypto.%s: failed to parse private key%s",
                            fname, inpasswd ? " (wrong password?)" : "");
    }
    if (!pkey) RP_THROW(ctx, "crypto.%s: failed to build key", fname);

    duk_push_object(ctx);
    rc_push_pkey_pem_pub(ctx, pkey);
    duk_put_prop_string(ctx, -2, "public");
    rc_push_pkey_pem_priv(ctx, pkey, outpasswd);
    duk_put_prop_string(ctx, -2, "private");
    EVP_PKEY_free(pkey);
    return 1;
}

/* --- components(key) → {curve, public, private?} where each is hex --- */
static duk_ret_t rc_25519_components(duk_context *ctx, const char *curve_label, const char *fname)
{
    const void *key = NULL; duk_size_t key_len = 0;
    EVP_PKEY *pkey = NULL;
    int is_private = 0;

    if (!rc_get_key_any(ctx, 0, &key, &key_len))
        RP_THROW(ctx, "crypto.%s: argument must be string or buffer", fname);

    pkey = rc_load_priv_pkey_any(key, key_len, NULL);
    if (pkey) is_private = 1;
    else      pkey = rc_load_pub_pkey_any(key, key_len);
    if (!pkey) RP_THROW(ctx, "crypto.%s: failed to parse key", fname);

    duk_push_object(ctx);
    duk_push_string(ctx, curve_label);
    duk_put_prop_string(ctx, -2, "curve");

    /* Public bytes — always 32 for X25519/Ed25519. */
    {
        size_t rawlen = 0;
        if (EVP_PKEY_get_raw_public_key(pkey, NULL, &rawlen) <= 0)
            { EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
        unsigned char tmp[64];
        if (rawlen > sizeof(tmp)) rawlen = sizeof(tmp);
        if (EVP_PKEY_get_raw_public_key(pkey, tmp, &rawlen) <= 0)
            { EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
        char *hex = OPENSSL_buf2hexstr(tmp, rawlen);
        /* OPENSSL_buf2hexstr inserts ':' between bytes; strip them. */
        if (hex)
        {
            char *clean = (char *)OPENSSL_malloc(rawlen * 2 + 1);
            if (clean)
            {
                int j = 0;
                for (char *p = hex; *p; ++p) if (*p != ':') clean[j++] = *p;
                clean[j] = 0;
                duk_push_string(ctx, clean);
                OPENSSL_free(clean);
            } else duk_push_string(ctx, hex);
            OPENSSL_free(hex);
        } else duk_push_string(ctx, "");
        duk_put_prop_string(ctx, -2, "public");
    }
    if (is_private)
    {
        size_t rawlen = 0;
        if (EVP_PKEY_get_raw_private_key(pkey, NULL, &rawlen) <= 0)
            { EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
        unsigned char tmp[64];
        if (rawlen > sizeof(tmp)) rawlen = sizeof(tmp);
        if (EVP_PKEY_get_raw_private_key(pkey, tmp, &rawlen) <= 0)
            { EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
        char *hex = OPENSSL_buf2hexstr(tmp, rawlen);
        if (hex)
        {
            char *clean = (char *)OPENSSL_malloc(rawlen * 2 + 1);
            if (clean)
            {
                int j = 0;
                for (char *p = hex; *p; ++p) if (*p != ':') clean[j++] = *p;
                clean[j] = 0;
                duk_push_string(ctx, clean);
                OPENSSL_free(clean);
            } else duk_push_string(ctx, hex);
            OPENSSL_free(hex);
        } else duk_push_string(ctx, "");
        duk_put_prop_string(ctx, -2, "private");
    }
    EVP_PKEY_free(pkey);
    return 1;
}

/* === X25519 (key agreement) === */

static duk_ret_t duk_x25519_gen_key(duk_context *ctx)
    { return rc_25519_gen_key(ctx, EVP_PKEY_X25519, "x25519_gen_key"); }
static duk_ret_t duk_x25519_import_pub_key(duk_context *ctx)
    { return rc_25519_import_pub_key(ctx, EVP_PKEY_X25519, "x25519_import_pub_key"); }
static duk_ret_t duk_x25519_import_priv_key(duk_context *ctx)
    { return rc_25519_import_priv_key(ctx, EVP_PKEY_X25519, "x25519_import_priv_key"); }
static duk_ret_t duk_x25519_components(duk_context *ctx)
    { return rc_25519_components(ctx, "X25519", "x25519_components"); }

/* x25519_derive(private_key, public_key [, password]) — mirrors ecdh */
static duk_ret_t duk_x25519_derive(duk_context *ctx)
{
    const void *priv_bytes = NULL, *pub_bytes = NULL;
    duk_size_t priv_len = 0, pub_len = 0;
    const char *password = NULL;
    EVP_PKEY *priv = NULL, *pub = NULL;
    EVP_PKEY_CTX *pctx = NULL;
    size_t secret_len = 0;
    int opt_idx = -1;

    if (duk_is_object(ctx, 0) && !duk_is_buffer_data(ctx, 0) &&
        !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0) &&
        !duk_is_string(ctx, 0))
    {
        opt_idx = 0;
        if (duk_get_prop_string(ctx, 0, "private"))
        {
            if (duk_is_string(ctx, -1))      priv_bytes = duk_get_lstring(ctx, -1, &priv_len);
            else if (duk_is_buffer_data(ctx, -1)) priv_bytes = duk_get_buffer_data(ctx, -1, &priv_len);
        }
        if (!priv_bytes) RP_THROW(ctx, "x_derive: 'private' is required");
        if (duk_get_prop_string(ctx, 0, "public"))
        {
            if (duk_is_string(ctx, -1))      pub_bytes = duk_get_lstring(ctx, -1, &pub_len);
            else if (duk_is_buffer_data(ctx, -1)) pub_bytes = duk_get_buffer_data(ctx, -1, &pub_len);
        }
        if (!pub_bytes) RP_THROW(ctx, "x_derive: 'public' is required");
        if (duk_get_prop_string(ctx, 0, "password"))
            password = REQUIRE_STRING(ctx, -1, "password must be a string");
        duk_pop(ctx);
    }
    else
    {
        if (!rc_get_key_any(ctx, 0, &priv_bytes, &priv_len))
            RP_THROW(ctx, "x_derive: first argument (private_key) must be string or buffer");
        if (!rc_get_key_any(ctx, 1, &pub_bytes, &pub_len))
            RP_THROW(ctx, "x_derive: second argument (public_key) must be string or buffer");
        if (duk_is_string(ctx, 2)) password = duk_get_string(ctx, 2);
    }

    priv = rc_load_priv_pkey_any(priv_bytes, priv_len, password);
    if (!priv) RP_THROW(ctx, "x_derive: failed to parse private key%s",
                       password ? " (wrong password?)" : "");
    pub = rc_load_pub_pkey_any(pub_bytes, pub_len);
    if (!pub) { EVP_PKEY_free(priv); RP_THROW(ctx, "x_derive: failed to parse public key"); }

    pctx = EVP_PKEY_CTX_new(priv, NULL);
    if (!pctx) { EVP_PKEY_free(priv); EVP_PKEY_free(pub); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_PKEY_derive_init(pctx) <= 0 ||
        EVP_PKEY_derive_set_peer(pctx, pub) <= 0 ||
        EVP_PKEY_derive(pctx, NULL, &secret_len) <= 0)
        { EVP_PKEY_CTX_free(pctx); EVP_PKEY_free(priv); EVP_PKEY_free(pub); DUK_OPENSSL_ERROR(ctx); }

    unsigned char *out = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)secret_len);
    if (EVP_PKEY_derive(pctx, out, &secret_len) <= 0)
        { EVP_PKEY_CTX_free(pctx); EVP_PKEY_free(priv); EVP_PKEY_free(pub); DUK_OPENSSL_ERROR(ctx); }

    EVP_PKEY_CTX_free(pctx);
    EVP_PKEY_free(priv);
    EVP_PKEY_free(pub);

    if (opt_idx >= 0) rc_finalize_buffer_buf_default(ctx, opt_idx);
    return 1;
}

/* === Ed25519 (signing) === */

static duk_ret_t duk_ed25519_gen_key(duk_context *ctx)
    { return rc_25519_gen_key(ctx, EVP_PKEY_ED25519, "ed25519_gen_key"); }
static duk_ret_t duk_ed25519_import_pub_key(duk_context *ctx)
    { return rc_25519_import_pub_key(ctx, EVP_PKEY_ED25519, "ed25519_import_pub_key"); }
static duk_ret_t duk_ed25519_import_priv_key(duk_context *ctx)
    { return rc_25519_import_priv_key(ctx, EVP_PKEY_ED25519, "ed25519_import_priv_key"); }
static duk_ret_t duk_ed25519_components(duk_context *ctx)
    { return rc_25519_components(ctx, "Ed25519", "ed25519_components"); }

/* ed25519_sign(message, private_key [, password | opts]) — mirrors rsa_sign.
 * Ed25519 is a "pure" signature scheme: no hash arg; uses one-shot
 * EVP_DigestSign (OpenSSL requires NULL MD and the one-shot variant). */
static duk_ret_t duk_ed25519_sign(duk_context *ctx)
{
    const void *keybytes = NULL, *data = NULL;
    duk_size_t keylen = 0, datalen = 0;
    const char *password = NULL;
    EVP_PKEY *pkey = NULL;
    EVP_MD_CTX *mctx = NULL;
    size_t siglen = 0;
    unsigned char *out;

    if (!rc_get_key_any(ctx, 0, &data, &datalen))
        RP_THROW(ctx, "ed_sign: first argument (data) must be string or buffer");
    if (!rc_get_key_any(ctx, 1, &keybytes, &keylen))
        RP_THROW(ctx, "ed_sign: second argument (private_key) must be string or buffer");

    if (duk_is_string(ctx, 2))
        password = duk_get_string(ctx, 2);
    else if (duk_is_object(ctx, 2) && !duk_is_buffer_data(ctx, 2) &&
             !duk_is_array(ctx, 2) && !duk_is_function(ctx, 2))
    {
        if (duk_get_prop_string(ctx, 2, "password"))
            password = REQUIRE_STRING(ctx, -1, "ed_sign: 'password' must be a string");
        duk_pop(ctx);
    }
    else if (!duk_is_undefined(ctx, 2) && !duk_is_null(ctx, 2))
        RP_THROW(ctx, "ed_sign: third argument must be a password string or options object");

    pkey = rc_load_priv_pkey_any(keybytes, keylen, password);
    if (!pkey) RP_THROW(ctx, "ed_sign: failed to parse private key%s",
                       password ? " (wrong password?)" : "");

    mctx = EVP_MD_CTX_new();
    if (!mctx) { EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_DigestSignInit(mctx, NULL, NULL, NULL, pkey) <= 0)
        { EVP_MD_CTX_free(mctx); EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_DigestSign(mctx, NULL, &siglen, (const unsigned char *)data, (size_t)datalen) <= 0)
        { EVP_MD_CTX_free(mctx); EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }

    out = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)siglen);
    if (EVP_DigestSign(mctx, out, &siglen, (const unsigned char *)data, (size_t)datalen) <= 0)
        { EVP_MD_CTX_free(mctx); EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }

    EVP_MD_CTX_free(mctx);
    EVP_PKEY_free(pkey);
    return 1;
}

/* ed25519_verify(data, public_key, signature) — mirrors rsa_verify. */
static duk_ret_t duk_ed25519_verify(duk_context *ctx)
{
    const void *keybytes = NULL, *data = NULL, *sig = NULL;
    duk_size_t keylen = 0, datalen = 0, siglen = 0;
    EVP_PKEY *pkey = NULL;
    EVP_MD_CTX *mctx = NULL;
    int verify_result = 0;

    if (!rc_get_key_any(ctx, 0, &data, &datalen))
        RP_THROW(ctx, "ed_verify: first argument (data) must be string or buffer");
    if (!rc_get_key_any(ctx, 1, &keybytes, &keylen))
        RP_THROW(ctx, "ed_verify: second argument (public_key) must be string or buffer");
    if (!rc_get_key_any(ctx, 2, &sig, &siglen))
        RP_THROW(ctx, "ed_verify: third argument (signature) must be string or buffer");

    pkey = rc_load_pub_pkey_any(keybytes, keylen);
    if (!pkey) RP_THROW(ctx, "ed_verify: failed to parse public key");

    mctx = EVP_MD_CTX_new();
    if (!mctx) { EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_DigestVerifyInit(mctx, NULL, NULL, NULL, pkey) <= 0)
        { EVP_MD_CTX_free(mctx); EVP_PKEY_free(pkey); DUK_OPENSSL_ERROR(ctx); }
    verify_result = EVP_DigestVerify(mctx, (const unsigned char *)sig, (size_t)siglen,
                                     (const unsigned char *)data, (size_t)datalen);
    EVP_MD_CTX_free(mctx);
    EVP_PKEY_free(pkey);

    duk_push_boolean(ctx, verify_result == 1);
    return 1;
}

/* === X448 / Ed448 ===
 * Same call shapes as X25519 / Ed25519.  The derive/sign/verify
 * functions are reused directly (registered under both names) since
 * they get the algorithm from the key's pkey type at load time.
 * Only gen_key / import_* / components need new wrappers that pass
 * EVP_PKEY_X448 / EVP_PKEY_ED448 to the generic helpers. */

static duk_ret_t duk_x448_gen_key(duk_context *ctx)
    { return rc_25519_gen_key(ctx, EVP_PKEY_X448, "x448_gen_key"); }
static duk_ret_t duk_x448_import_pub_key(duk_context *ctx)
    { return rc_25519_import_pub_key(ctx, EVP_PKEY_X448, "x448_import_pub_key"); }
static duk_ret_t duk_x448_import_priv_key(duk_context *ctx)
    { return rc_25519_import_priv_key(ctx, EVP_PKEY_X448, "x448_import_priv_key"); }
static duk_ret_t duk_x448_components(duk_context *ctx)
    { return rc_25519_components(ctx, "X448", "x448_components"); }

static duk_ret_t duk_ed448_gen_key(duk_context *ctx)
    { return rc_25519_gen_key(ctx, EVP_PKEY_ED448, "ed448_gen_key"); }
static duk_ret_t duk_ed448_import_pub_key(duk_context *ctx)
    { return rc_25519_import_pub_key(ctx, EVP_PKEY_ED448, "ed448_import_pub_key"); }
static duk_ret_t duk_ed448_import_priv_key(duk_context *ctx)
    { return rc_25519_import_priv_key(ctx, EVP_PKEY_ED448, "ed448_import_priv_key"); }
static duk_ret_t duk_ed448_components(duk_context *ctx)
    { return rc_25519_components(ctx, "Ed448", "ed448_components"); }

/* === ML-DSA (NIST FIPS 204, formerly CRYSTALS-Dilithium) ===
 *
 * Three variants: ML-DSA-44, ML-DSA-65, ML-DSA-87 (security strengths
 * roughly equivalent to AES-128, AES-192, AES-256 respectively).
 *
 * Sign/verify shape is identical to Ed25519 — pure signature scheme,
 * no hash arg; uses one-shot EVP_DigestSign with NULL md.  So the
 * existing duk_ed25519_sign/verify functions work transparently for
 * ML-DSA keys; we register them under mldsa_sign/verify names.
 *
 * Only gen_key / import / components need ML-DSA-specific wrappers
 * to translate the user-facing variant name to OpenSSL's algorithm
 * name and run the right keygen path. */

/* Generate an EVP_PKEY by algorithm name (used by ML-DSA, ML-KEM). */
static EVP_PKEY *rc_keygen_byname(const char *algo)
{
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_from_name(NULL, algo, NULL);
    if (!pctx) return NULL;
    if (EVP_PKEY_keygen_init(pctx) <= 0) { EVP_PKEY_CTX_free(pctx); return NULL; }
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0) pkey = NULL;
    EVP_PKEY_CTX_free(pctx);
    return pkey;
}

/* Translate a user-facing variant string to OpenSSL's algorithm name. */
static const char *rc_mldsa_name(const char *variant)
{
    if (!strcmp(variant, "ml-dsa-44") || !strcmp(variant, "ML-DSA-44") ||
        !strcmp(variant, "mldsa44")   || !strcmp(variant, "MLDSA44")) return "ML-DSA-44";
    if (!strcmp(variant, "ml-dsa-65") || !strcmp(variant, "ML-DSA-65") ||
        !strcmp(variant, "mldsa65")   || !strcmp(variant, "MLDSA65")) return "ML-DSA-65";
    if (!strcmp(variant, "ml-dsa-87") || !strcmp(variant, "ML-DSA-87") ||
        !strcmp(variant, "mldsa87")   || !strcmp(variant, "MLDSA87")) return "ML-DSA-87";
    return NULL;
}

/* mldsa_gen_key(variant [, password]) → {public, private} PEMs. */
static duk_ret_t duk_mldsa_gen_key(duk_context *ctx)
{
    const char *variant = NULL, *password = NULL, *algo = NULL;
    EVP_PKEY *pkey = NULL;

    if (duk_is_object(ctx, 0) && !duk_is_buffer_data(ctx, 0) &&
        !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0) &&
        !duk_is_string(ctx, 0))
    {
        if (duk_get_prop_string(ctx, 0, "variant"))
            variant = REQUIRE_STRING(ctx, -1, "mldsa_gen_key: 'variant' must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "password"))
            password = REQUIRE_STRING(ctx, -1, "mldsa_gen_key: 'password' must be a string");
        duk_pop(ctx);
    }
    else
    {
        if (duk_is_string(ctx, 0)) variant = duk_get_string(ctx, 0);
        if (duk_is_string(ctx, 1)) password = duk_get_string(ctx, 1);
    }
    if (!variant)
        RP_THROW(ctx, "mldsa_gen_key: variant is required ('ml-dsa-44', 'ml-dsa-65', or 'ml-dsa-87')");
    algo = rc_mldsa_name(variant);
    if (!algo) RP_THROW(ctx, "mldsa_gen_key: unknown variant '%s'", variant);

    pkey = rc_keygen_byname(algo);
    if (!pkey) RP_THROW(ctx, "mldsa_gen_key: keygen failed for %s", algo);

    duk_push_object(ctx);
    rc_push_pkey_pem_pub(ctx, pkey);
    duk_put_prop_string(ctx, -2, "public");
    rc_push_pkey_pem_priv(ctx, pkey, password);
    duk_put_prop_string(ctx, -2, "private");
    EVP_PKEY_free(pkey);
    return 1;
}

/* mldsa_import_pub_key(pub) → canonical SPKI PEM. */
static duk_ret_t duk_mldsa_import_pub_key(duk_context *ctx)
{
    const void *key = NULL; duk_size_t key_len = 0;
    if (!rc_get_key_any(ctx, 0, &key, &key_len))
        RP_THROW(ctx, "mldsa_import_pub_key: argument must be string or buffer");
    EVP_PKEY *pkey = rc_load_pub_pkey_any(key, key_len);
    if (!pkey) RP_THROW(ctx, "mldsa_import_pub_key: failed to parse public key");
    rc_push_pkey_pem_pub(ctx, pkey);
    EVP_PKEY_free(pkey);
    return 1;
}

/* mldsa_import_priv_key(priv [, oldpass [, newpass]]) → {public, private}. */
static duk_ret_t duk_mldsa_import_priv_key(duk_context *ctx)
{
    const void *key = NULL; duk_size_t key_len = 0;
    const char *inpasswd = NULL, *outpasswd = NULL;
    EVP_PKEY *pkey = NULL;

    if (duk_is_object(ctx, 0) && !duk_is_buffer_data(ctx, 0) &&
        !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0) &&
        !duk_is_string(ctx, 0))
    {
        if (!rc_get_opt_bytes(ctx, 0, "key", &key, &key_len))
            RP_THROW(ctx, "mldsa_import_priv_key: 'key' is required");
        if (duk_get_prop_string(ctx, 0, "decryptPassword"))
            inpasswd = REQUIRE_STRING(ctx, -1, "decryptPassword must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "encryptPassword"))
            outpasswd = REQUIRE_STRING(ctx, -1, "encryptPassword must be a string");
        duk_pop(ctx);
    }
    else
    {
        if (!rc_get_key_any(ctx, 0, &key, &key_len))
            RP_THROW(ctx, "mldsa_import_priv_key: first argument must be string or buffer");
        if (duk_is_string(ctx, 1)) inpasswd  = duk_get_string(ctx, 1);
        if (duk_is_string(ctx, 2)) outpasswd = duk_get_string(ctx, 2);
    }

    pkey = rc_load_priv_pkey_any(key, key_len, inpasswd);
    if (!pkey) RP_THROW(ctx, "mldsa_import_priv_key: failed to parse private key%s",
                       inpasswd ? " (wrong password?)" : "");

    duk_push_object(ctx);
    rc_push_pkey_pem_pub(ctx, pkey);
    duk_put_prop_string(ctx, -2, "public");
    rc_push_pkey_pem_priv(ctx, pkey, outpasswd);
    duk_put_prop_string(ctx, -2, "private");
    EVP_PKEY_free(pkey);
    return 1;
}

/* Helper: hex-encode a raw byte buffer, stripping the colons that
 * OPENSSL_buf2hexstr inserts.  Used for ML-DSA / ML-KEM components
 * where keys are too large for the small stack buffer used by 25519. */
static void rc_push_hex_octets(duk_context *ctx, const unsigned char *bytes, size_t len)
{
    char *hex = OPENSSL_buf2hexstr(bytes, len);
    if (!hex) { duk_push_string(ctx, ""); return; }
    /* allocate and strip colons */
    char *clean = (char *)OPENSSL_malloc(len * 2 + 1);
    if (clean)
    {
        int j = 0;
        for (char *p = hex; *p; ++p) if (*p != ':') clean[j++] = *p;
        clean[j] = 0;
        duk_push_string(ctx, clean);
        OPENSSL_free(clean);
    }
    else duk_push_string(ctx, hex);
    OPENSSL_free(hex);
}

/* Generic components extractor for PQ-shape keys (large raw bytes,
 * variant name as the curve label).  Used by both ML-DSA and ML-KEM. */
static duk_ret_t rc_pq_components(duk_context *ctx, const char *fname,
                                  const char *fallback_variant)
{
    const void *key = NULL; duk_size_t key_len = 0;
    EVP_PKEY *pkey = NULL;
    int is_private = 0;

    if (!rc_get_key_any(ctx, 0, &key, &key_len))
        RP_THROW(ctx, "crypto.%s: argument must be string or buffer", fname);

    pkey = rc_load_priv_pkey_any(key, key_len, NULL);
    if (pkey) is_private = 1;
    else      pkey = rc_load_pub_pkey_any(key, key_len);
    if (!pkey) RP_THROW(ctx, "crypto.%s: failed to parse key", fname);

    duk_push_object(ctx);
    const char *type_name = EVP_PKEY_get0_type_name(pkey);
    duk_push_string(ctx, type_name ? type_name : fallback_variant);
    duk_put_prop_string(ctx, -2, "variant");

    size_t rawlen = 0;
    if (EVP_PKEY_get_raw_public_key(pkey, NULL, &rawlen) > 0 && rawlen > 0)
    {
        unsigned char *buf = (unsigned char *)OPENSSL_malloc(rawlen);
        if (buf && EVP_PKEY_get_raw_public_key(pkey, buf, &rawlen) > 0)
        {
            rc_push_hex_octets(ctx, buf, rawlen);
            duk_put_prop_string(ctx, -2, "public");
        }
        OPENSSL_free(buf);
    }
    if (is_private)
    {
        rawlen = 0;
        if (EVP_PKEY_get_raw_private_key(pkey, NULL, &rawlen) > 0 && rawlen > 0)
        {
            unsigned char *buf = (unsigned char *)OPENSSL_malloc(rawlen);
            if (buf && EVP_PKEY_get_raw_private_key(pkey, buf, &rawlen) > 0)
            {
                rc_push_hex_octets(ctx, buf, rawlen);
                duk_put_prop_string(ctx, -2, "private");
            }
            OPENSSL_free(buf);
        }
    }
    EVP_PKEY_free(pkey);
    return 1;
}

static duk_ret_t duk_mldsa_components(duk_context *ctx)
    { return rc_pq_components(ctx, "mldsa_components", "ML-DSA"); }


/* === ML-KEM (NIST FIPS 203, formerly CRYSTALS-Kyber) ===
 *
 * Three variants: ML-KEM-512 / ML-KEM-768 / ML-KEM-1024.
 *
 * KEM (Key Encapsulation Mechanism) is a different shape from key
 * agreement:
 *   - encapsulate(pub) → {ciphertext, sharedSecret}
 *   - decapsulate(ciphertext, priv) → sharedSecret
 *
 * The sender uses encapsulate to produce a ciphertext to send to the
 * receiver and a shared secret to use locally.  The receiver runs
 * decapsulate on the ciphertext with its private key to recover the
 * same shared secret. */

static const char *rc_mlkem_name(const char *variant)
{
    if (!strcmp(variant, "ml-kem-512")  || !strcmp(variant, "ML-KEM-512") ||
        !strcmp(variant, "mlkem512")    || !strcmp(variant, "MLKEM512"))   return "ML-KEM-512";
    if (!strcmp(variant, "ml-kem-768")  || !strcmp(variant, "ML-KEM-768") ||
        !strcmp(variant, "mlkem768")    || !strcmp(variant, "MLKEM768"))   return "ML-KEM-768";
    if (!strcmp(variant, "ml-kem-1024") || !strcmp(variant, "ML-KEM-1024") ||
        !strcmp(variant, "mlkem1024")   || !strcmp(variant, "MLKEM1024"))  return "ML-KEM-1024";
    return NULL;
}

static duk_ret_t duk_mlkem_gen_key(duk_context *ctx)
{
    const char *variant = NULL, *password = NULL, *algo = NULL;
    EVP_PKEY *pkey = NULL;

    if (duk_is_object(ctx, 0) && !duk_is_buffer_data(ctx, 0) &&
        !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0) &&
        !duk_is_string(ctx, 0))
    {
        if (duk_get_prop_string(ctx, 0, "variant"))
            variant = REQUIRE_STRING(ctx, -1, "mlkem_gen_key: 'variant' must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "password"))
            password = REQUIRE_STRING(ctx, -1, "mlkem_gen_key: 'password' must be a string");
        duk_pop(ctx);
    }
    else
    {
        if (duk_is_string(ctx, 0)) variant = duk_get_string(ctx, 0);
        if (duk_is_string(ctx, 1)) password = duk_get_string(ctx, 1);
    }
    if (!variant)
        RP_THROW(ctx, "mlkem_gen_key: variant is required ('ml-kem-512', 'ml-kem-768', or 'ml-kem-1024')");
    algo = rc_mlkem_name(variant);
    if (!algo) RP_THROW(ctx, "mlkem_gen_key: unknown variant '%s'", variant);

    pkey = rc_keygen_byname(algo);
    if (!pkey) RP_THROW(ctx, "mlkem_gen_key: keygen failed for %s", algo);

    duk_push_object(ctx);
    rc_push_pkey_pem_pub(ctx, pkey);
    duk_put_prop_string(ctx, -2, "public");
    rc_push_pkey_pem_priv(ctx, pkey, password);
    duk_put_prop_string(ctx, -2, "private");
    EVP_PKEY_free(pkey);
    return 1;
}

static duk_ret_t duk_mlkem_import_pub_key(duk_context *ctx)
{
    const void *key = NULL; duk_size_t key_len = 0;
    if (!rc_get_key_any(ctx, 0, &key, &key_len))
        RP_THROW(ctx, "mlkem_import_pub_key: argument must be string or buffer");
    EVP_PKEY *pkey = rc_load_pub_pkey_any(key, key_len);
    if (!pkey) RP_THROW(ctx, "mlkem_import_pub_key: failed to parse public key");
    rc_push_pkey_pem_pub(ctx, pkey);
    EVP_PKEY_free(pkey);
    return 1;
}

static duk_ret_t duk_mlkem_import_priv_key(duk_context *ctx)
{
    const void *key = NULL; duk_size_t key_len = 0;
    const char *inpasswd = NULL, *outpasswd = NULL;
    EVP_PKEY *pkey = NULL;

    if (duk_is_object(ctx, 0) && !duk_is_buffer_data(ctx, 0) &&
        !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0) &&
        !duk_is_string(ctx, 0))
    {
        if (!rc_get_opt_bytes(ctx, 0, "key", &key, &key_len))
            RP_THROW(ctx, "mlkem_import_priv_key: 'key' is required");
        if (duk_get_prop_string(ctx, 0, "decryptPassword"))
            inpasswd = REQUIRE_STRING(ctx, -1, "decryptPassword must be a string");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "encryptPassword"))
            outpasswd = REQUIRE_STRING(ctx, -1, "encryptPassword must be a string");
        duk_pop(ctx);
    }
    else
    {
        if (!rc_get_key_any(ctx, 0, &key, &key_len))
            RP_THROW(ctx, "mlkem_import_priv_key: first argument must be string or buffer");
        if (duk_is_string(ctx, 1)) inpasswd  = duk_get_string(ctx, 1);
        if (duk_is_string(ctx, 2)) outpasswd = duk_get_string(ctx, 2);
    }

    pkey = rc_load_priv_pkey_any(key, key_len, inpasswd);
    if (!pkey) RP_THROW(ctx, "mlkem_import_priv_key: failed to parse private key%s",
                       inpasswd ? " (wrong password?)" : "");

    duk_push_object(ctx);
    rc_push_pkey_pem_pub(ctx, pkey);
    duk_put_prop_string(ctx, -2, "public");
    rc_push_pkey_pem_priv(ctx, pkey, outpasswd);
    duk_put_prop_string(ctx, -2, "private");
    EVP_PKEY_free(pkey);
    return 1;
}

static duk_ret_t duk_mlkem_components(duk_context *ctx)
    { return rc_pq_components(ctx, "mlkem_components", "ML-KEM"); }

/* mlkem_encapsulate(public_key) → {ciphertext, sharedSecret} */
static duk_ret_t duk_mlkem_encapsulate(duk_context *ctx)
{
    const void *pub_bytes = NULL; duk_size_t pub_len = 0;
    EVP_PKEY *pub = NULL;
    EVP_PKEY_CTX *pctx = NULL;
    size_t ct_len = 0, ss_len = 0;

    if (!rc_get_key_any(ctx, 0, &pub_bytes, &pub_len))
        RP_THROW(ctx, "mlkem_encapsulate: argument must be a public key (string or buffer)");

    pub = rc_load_pub_pkey_any(pub_bytes, pub_len);
    if (!pub) RP_THROW(ctx, "mlkem_encapsulate: failed to parse public key");

    pctx = EVP_PKEY_CTX_new(pub, NULL);
    if (!pctx) { EVP_PKEY_free(pub); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_PKEY_encapsulate_init(pctx, NULL) <= 0)
        { EVP_PKEY_CTX_free(pctx); EVP_PKEY_free(pub); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_PKEY_encapsulate(pctx, NULL, &ct_len, NULL, &ss_len) <= 0)
        { EVP_PKEY_CTX_free(pctx); EVP_PKEY_free(pub); DUK_OPENSSL_ERROR(ctx); }

    duk_push_object(ctx);
    unsigned char *ct_out = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)ct_len);
    unsigned char *ss_out = (unsigned char *)OPENSSL_malloc(ss_len);
    if (!ss_out) { EVP_PKEY_CTX_free(pctx); EVP_PKEY_free(pub); RP_THROW(ctx, "mlkem_encapsulate: oom"); }
    if (EVP_PKEY_encapsulate(pctx, ct_out, &ct_len, ss_out, &ss_len) <= 0)
        { OPENSSL_free(ss_out); EVP_PKEY_CTX_free(pctx); EVP_PKEY_free(pub); DUK_OPENSSL_ERROR(ctx); }
    duk_put_prop_string(ctx, -2, "ciphertext");

    unsigned char *ss_buf = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)ss_len);
    memcpy(ss_buf, ss_out, ss_len);
    OPENSSL_clear_free(ss_out, ss_len);
    duk_put_prop_string(ctx, -2, "sharedSecret");

    EVP_PKEY_CTX_free(pctx);
    EVP_PKEY_free(pub);
    return 1;
}

/* mlkem_decapsulate(ciphertext, private_key [, password]) → sharedSecret */
static duk_ret_t duk_mlkem_decapsulate(duk_context *ctx)
{
    const void *ct = NULL, *priv_bytes = NULL;
    duk_size_t ct_len_in = 0, priv_len = 0;
    const char *password = NULL;
    EVP_PKEY *priv = NULL;
    EVP_PKEY_CTX *pctx = NULL;
    size_t ss_len = 0;

    if (!rc_get_key_any(ctx, 0, &ct, &ct_len_in))
        RP_THROW(ctx, "mlkem_decapsulate: first argument (ciphertext) must be string or buffer");
    if (!rc_get_key_any(ctx, 1, &priv_bytes, &priv_len))
        RP_THROW(ctx, "mlkem_decapsulate: second argument (private_key) must be string or buffer");
    if (duk_is_string(ctx, 2)) password = duk_get_string(ctx, 2);

    priv = rc_load_priv_pkey_any(priv_bytes, priv_len, password);
    if (!priv) RP_THROW(ctx, "mlkem_decapsulate: failed to parse private key%s",
                       password ? " (wrong password?)" : "");

    pctx = EVP_PKEY_CTX_new(priv, NULL);
    if (!pctx) { EVP_PKEY_free(priv); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_PKEY_decapsulate_init(pctx, NULL) <= 0)
        { EVP_PKEY_CTX_free(pctx); EVP_PKEY_free(priv); DUK_OPENSSL_ERROR(ctx); }
    if (EVP_PKEY_decapsulate(pctx, NULL, &ss_len, (const unsigned char *)ct, (size_t)ct_len_in) <= 0)
        { EVP_PKEY_CTX_free(pctx); EVP_PKEY_free(priv); DUK_OPENSSL_ERROR(ctx); }

    unsigned char *ss_out = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)ss_len);
    if (EVP_PKEY_decapsulate(pctx, ss_out, &ss_len, (const unsigned char *)ct, (size_t)ct_len_in) <= 0)
        { EVP_PKEY_CTX_free(pctx); EVP_PKEY_free(priv); DUK_OPENSSL_ERROR(ctx); }

    EVP_PKEY_CTX_free(pctx);
    EVP_PKEY_free(priv);
    return 1;
}

/* === scrypt === */

static duk_ret_t duk_scrypt(duk_context *ctx)
{
    const void *pass = NULL, *salt = NULL;
    duk_size_t passlen = 0, saltlen = 0;
    unsigned int N = 0, r = 0, p = 0;
    int length = 0;
    EVP_KDF *kdf = NULL;
    EVP_KDF_CTX *kctx = NULL;
    OSSL_PARAM params[6];
    int nparams = 0;
    uint64_t Nv = 0;
    unsigned char *out;

    REQUIRE_OBJECT(ctx, 0, "crypto.scrypt requires an options object");

    if (!rc_get_opt_bytes(ctx, 0, "pass", &pass, &passlen))
        RP_THROW(ctx, "crypto.scrypt: 'pass' is required (string/buffer)");
    if (!rc_get_opt_bytes(ctx, 0, "salt", &salt, &saltlen))
        RP_THROW(ctx, "crypto.scrypt: 'salt' is required (string/buffer)");

    if (!duk_get_prop_string(ctx, 0, "N"))
        RP_THROW(ctx, "crypto.scrypt: option 'N' is required (Number, cost factor)");
    Nv = (uint64_t)REQUIRE_NUMBER(ctx, -1, "crypto.scrypt: 'N' must be a Number");
    duk_pop(ctx);
    if (Nv < 2 || (Nv & (Nv - 1)) != 0)
        RP_THROW(ctx, "crypto.scrypt: 'N' must be a power of two ≥ 2");
    N = (unsigned int)Nv;

    if (!duk_get_prop_string(ctx, 0, "r"))
        RP_THROW(ctx, "crypto.scrypt: option 'r' is required (Number, block size)");
    r = (unsigned int)REQUIRE_NUMBER(ctx, -1, "crypto.scrypt: 'r' must be a Number");
    duk_pop(ctx);
    if (r < 1) RP_THROW(ctx, "crypto.scrypt: 'r' must be ≥ 1");

    if (!duk_get_prop_string(ctx, 0, "p"))
        RP_THROW(ctx, "crypto.scrypt: option 'p' is required (Number, parallelization)");
    p = (unsigned int)REQUIRE_NUMBER(ctx, -1, "crypto.scrypt: 'p' must be a Number");
    duk_pop(ctx);
    if (p < 1) RP_THROW(ctx, "crypto.scrypt: 'p' must be ≥ 1");

    if (!duk_get_prop_string(ctx, 0, "length"))
        RP_THROW(ctx, "crypto.scrypt: option 'length' is required (Number, output bytes)");
    length = (int)REQUIRE_NUMBER(ctx, -1, "crypto.scrypt: 'length' must be a Number");
    duk_pop(ctx);
    if (length < 1) RP_THROW(ctx, "crypto.scrypt: 'length' must be ≥ 1");

    kdf = EVP_KDF_fetch(NULL, "SCRYPT", NULL);
    if (!kdf) RP_THROW(ctx, "crypto.scrypt: EVP_KDF_fetch(SCRYPT) failed");
    kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kctx) RP_THROW(ctx, "crypto.scrypt: EVP_KDF_CTX_new failed");

    params[nparams++] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_PASSWORD, (void *)pass, passlen);
    params[nparams++] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_SALT, (void *)salt, saltlen);
    params[nparams++] = OSSL_PARAM_construct_uint64(
        OSSL_KDF_PARAM_SCRYPT_N, &Nv);
    params[nparams++] = OSSL_PARAM_construct_uint(
        OSSL_KDF_PARAM_SCRYPT_R, &r);
    params[nparams++] = OSSL_PARAM_construct_uint(
        OSSL_KDF_PARAM_SCRYPT_P, &p);
    params[nparams] = OSSL_PARAM_construct_end();

    out = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)length);
    if (EVP_KDF_derive(kctx, out, (size_t)length, params) <= 0)
    {
        EVP_KDF_CTX_free(kctx);
        DUK_OPENSSL_ERROR(ctx);
    }
    EVP_KDF_CTX_free(kctx);

    /* drop 'pass' and 'salt' stack refs */
    duk_remove(ctx, -2);
    duk_remove(ctx, -2);

    rc_finalize_buffer_buf_default(ctx, 0);
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
    {"rand", duk_rand, 2},
    {"gaussrand", duk_gaussrand, 1},
    {"normrand", duk_normrand, 1},
    {"randnum", duk_randnum, 0},
    {"seed", duk_seed_rand, 1},
    {"hmac", duk_hmac, 4},
    {"hash", duk_hash, 3},
    {"rsa_pub_encrypt", duk_rsa_pub_encrypt, 3},
    {"rsa_priv_decrypt", duk_rsa_priv_decrypt, 4},
    {"rsa_sign", duk_rsa_sign, 3},
    {"rsa_verify", duk_rsa_verify, 4},
    {"rsa_gen_key", duk_rsa_gen_key, 2},
    {"gen_csr", duk_gen_csr, 3},
    {"gen_cert", duk_gen_cert, 2},
    {"rsa_components", duk_rsa_components, 2},
    {"rsa_import_priv_key", duk_rsa_import_priv_key, 3},
    {"cert_info", duk_cert_info,1},
    {"passToKeyIv", duk_rp_pass_to_keyiv, 1},
    {"passwd", do_passwd, 3},
    {"passwdCheck", check_passwd, 2},
    {"passwdComponents", passwd_components, 1},
    /* Tier 1 / Tier 2 additions */
    {"timingSafeEqual", duk_timing_safe_equal, 2},
    {"pbkdf2", duk_pbkdf2, 1},
    {"hkdf", duk_hkdf, 1},
    {"kmac", duk_kmac, 4},
    {"cshake128", duk_cshake128, 2},
    {"cshake256", duk_cshake256, 2},
    {"ec_gen_key", duk_ec_gen_key, 2},
    {"ec_import_pub_key", duk_ec_import_pub_key, 2},
    {"ec_import_priv_key", duk_ec_import_priv_key, 3},
    {"ec_components", duk_ec_components, 1},
    {"ecdsa_sign", duk_ecdsa_sign, 3},
    {"ecdsa_verify", duk_ecdsa_verify, 4},
    {"ecdh", duk_ecdh, 3},
    {"pemToDer", duk_pem_to_der, 1},
    {"derToPem", duk_der_to_pem, 2},
    /* Tier 3: X25519 / Ed25519 / scrypt */
    {"x25519_gen_key", duk_x25519_gen_key, 1},
    {"x25519_import_pub_key", duk_x25519_import_pub_key, 1},
    {"x25519_import_priv_key", duk_x25519_import_priv_key, 3},
    {"x25519_components", duk_x25519_components, 1},
    {"x25519_derive", duk_x25519_derive, 3},
    {"ed25519_gen_key", duk_ed25519_gen_key, 1},
    {"ed25519_import_pub_key", duk_ed25519_import_pub_key, 1},
    {"ed25519_import_priv_key", duk_ed25519_import_priv_key, 3},
    {"ed25519_components", duk_ed25519_components, 1},
    {"ed25519_sign", duk_ed25519_sign, 3},
    {"ed25519_verify", duk_ed25519_verify, 3},
    /* X448 / Ed448 — derive/sign/verify share the 25519 C functions
     * (algorithm is determined by the key's pkey type). */
    {"x448_gen_key", duk_x448_gen_key, 1},
    {"x448_import_pub_key", duk_x448_import_pub_key, 1},
    {"x448_import_priv_key", duk_x448_import_priv_key, 3},
    {"x448_components", duk_x448_components, 1},
    {"x448_derive", duk_x25519_derive, 3},
    {"ed448_gen_key", duk_ed448_gen_key, 1},
    {"ed448_import_pub_key", duk_ed448_import_pub_key, 1},
    {"ed448_import_priv_key", duk_ed448_import_priv_key, 3},
    {"ed448_components", duk_ed448_components, 1},
    {"ed448_sign", duk_ed25519_sign, 3},
    {"ed448_verify", duk_ed25519_verify, 3},
    /* ML-DSA — sign/verify share Ed25519 C functions (pure signature
     * scheme, one-shot EVP_DigestSign with NULL md). */
    {"mldsa_gen_key", duk_mldsa_gen_key, 2},
    {"mldsa_import_pub_key", duk_mldsa_import_pub_key, 1},
    {"mldsa_import_priv_key", duk_mldsa_import_priv_key, 3},
    {"mldsa_components", duk_mldsa_components, 1},
    {"mldsa_sign", duk_ed25519_sign, 3},
    {"mldsa_verify", duk_ed25519_verify, 3},
    /* ML-KEM — Key Encapsulation Mechanism (NIST FIPS 203). */
    {"mlkem_gen_key", duk_mlkem_gen_key, 2},
    {"mlkem_import_pub_key", duk_mlkem_import_pub_key, 1},
    {"mlkem_import_priv_key", duk_mlkem_import_priv_key, 3},
    {"mlkem_components", duk_mlkem_components, 1},
    {"mlkem_encapsulate", duk_mlkem_encapsulate, 1},
    {"mlkem_decapsulate", duk_mlkem_decapsulate, 3},
    {"scrypt", duk_scrypt, 1},
    {NULL, NULL, 0}
};

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
/* Saved handles from the OSSL_PROVIDER_load calls in duk_open_module so
   we can OSSL_PROVIDER_unload at exit.  Without the unload, OpenSSL keeps
   the providers' init state alive past OPENSSL_cleanup, leaving ~40 KB
   indirect plus a CRYPTO_zalloc direct alloc reported as definitely-lost. */
static OSSL_PROVIDER *rp_legacy_provider = NULL;
static OSSL_PROVIDER *rp_default_provider = NULL;
#endif

static void rp_openssl_cleanup_atexit(void *arg)
{
    (void)arg;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    if (rp_legacy_provider)  { OSSL_PROVIDER_unload(rp_legacy_provider);  rp_legacy_provider  = NULL; }
    if (rp_default_provider) { OSSL_PROVIDER_unload(rp_default_provider); rp_default_provider = NULL; }
#endif
    OPENSSL_cleanup();
}

/* Called from this module's duk_open_module and from rampart-curl.c's
   curl_global_init path.  Guarded so OPENSSL_cleanup runs at most once
   even if both modules are loaded. */
void rp_openssl_register_cleanup(void)
{
    static int registered = 0;
    if (!registered)
    {
        add_exit_func(rp_openssl_cleanup_atexit, NULL);
        registered = 1;
    }
}

duk_ret_t duk_open_module(duk_context *ctx)
{
    rp_openssl_register_cleanup();
    OpenSSL_add_all_digests() ;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    /* OpenSSL 3.0+: digests like md4, mdc2, rmd160 live in the legacy
       provider, which isn't loaded by default.  Loading legacy alone
       hides the default provider, so explicitly load both.  Once-only:
       OSSL_PROVIDER_load is internally refcounted. */
    static int providers_loaded = 0;
    if (!providers_loaded) {
        rp_legacy_provider  = OSSL_PROVIDER_load(NULL, "legacy");
        rp_default_provider = OSSL_PROVIDER_load(NULL, "default");
        providers_loaded = 1;
    }
#endif
    duk_push_object(ctx);
    duk_put_function_list(ctx, -1, crypto_funcs);
    duk_rp_create_jsbi(ctx);
    duk_put_prop_string(ctx, -2, "JSBI");
    return 1;
}
