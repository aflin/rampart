#include "../core/duktape.h"
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <string.h>
#include <stdio.h>
#include <openssl/md5.h>

#define OPENSSL_ERR_STRING_MAX_SIZE 1024
#define DUK_OPENSSL_ERROR(ctx)                                                     \
    {                                                                              \
        void *err_buf = duk_push_fixed_buffer(ctx, OPENSSL_ERR_STRING_MAX_SIZE);   \
        ERR_error_string_n(ERR_get_error(), err_buf, OPENSSL_ERR_STRING_MAX_SIZE); \
        return duk_error(ctx, DUK_ERR_ERROR, "OpenSSL Error: %s", err_buf);        \
    }

/**
 * Does encryption given a cipher, buffer, key, and iv
 * @typedef {Object} EncryptOptions
 * @property {String} key - the secret key to be used to decrypt
 * @property {String} iv - the initialization vector/nonce
 * @property {String} cipher - The openssl name for the encryption/decryption scheme
 * @property {BufferData} buffer - the data to be encrypted 
 * @param {DecryptOptions} Options
 * @returns {Buffer} the encrypted buffer
 */
static duk_ret_t duk_encrypt(duk_context *ctx)
{
    /* Get options from Duktape */
    duk_require_object(ctx, -1);
    duk_idx_t options_idx = duk_normalize_index(ctx, -1);

    duk_get_prop_string(ctx, options_idx, "key");
    unsigned char *key = (unsigned char *)duk_get_string(ctx, -1);
    duk_get_prop_string(ctx, options_idx, "iv");
    unsigned char *iv = (unsigned char *)duk_get_string(ctx, -1);
    duk_get_prop_string(ctx, options_idx, "cipher");
    const char *cipher_name = duk_get_string(ctx, -1);
    duk_get_prop_string(ctx, options_idx, "buffer");
    duk_size_t in_len;
    void *in_buffer = duk_get_buffer_data(ctx, -1, &in_len);

    EVP_CIPHER_CTX *cipher_ctx;
    int out_len = 0;

    /* Create and initialise the context */
    if (!(cipher_ctx = EVP_CIPHER_CTX_new()))
        DUK_OPENSSL_ERROR(ctx);

    /* Retrieve the cipher by name */
    const EVP_CIPHER *cipher = EVP_get_cipherbyname(cipher_name);
    if (cipher == NULL)
        return duk_error(ctx, DUK_ERR_ERROR, "Cipher %s not found", cipher_name);

    /* Initialize the encryption operation with the found cipher, key and iv */
    if (!EVP_EncryptInit_ex(cipher_ctx, cipher, NULL, key, iv))
        DUK_OPENSSL_ERROR(ctx);

    /*
     * Encrypt the in buffer. EncryptUpdate is block aligned, so it can push up
     * to out_len + cipher_block_size - 1 bytes into the buffer
     */
    void *out_buffer = duk_push_dynamic_buffer(ctx, in_len + EVP_CIPHER_block_size(cipher) - 1);
    int current_len;
    do
    {
        if (!EVP_EncryptUpdate(cipher_ctx, out_buffer + out_len, &current_len, in_buffer, in_len))
            DUK_OPENSSL_ERROR(ctx);
        out_len += current_len;

    } while (current_len != 0);

    /*
     * Finalise the encryption. Further ciphertext bytes may be written at
     * this stage.
     */
    if (!EVP_EncryptFinal_ex(cipher_ctx, out_buffer + out_len, &current_len))
        DUK_OPENSSL_ERROR(ctx);

    out_len += current_len;

    /* Resize the buffer to the actual output length */
    duk_resize_buffer(ctx, -1, out_len);

    /* Clean up */
    EVP_CIPHER_CTX_free(cipher_ctx);

    return 1;
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
static duk_ret_t duk_decrypt(duk_context *ctx)
{
    /* Get options from Duktape */
    duk_require_object(ctx, -1);
    duk_idx_t options_idx = duk_normalize_index(ctx, -1);

    duk_get_prop_string(ctx, options_idx, "key");
    unsigned char *key = (unsigned char *)duk_get_string(ctx, -1);
    duk_get_prop_string(ctx, options_idx, "iv");
    unsigned char *iv = (unsigned char *)duk_get_string(ctx, -1);
    duk_get_prop_string(ctx, options_idx, "cipher");
    const char *cipher_name = duk_get_string(ctx, -1);
    duk_get_prop_string(ctx, options_idx, "buffer");
    duk_size_t in_len;
    void *in_buffer = duk_get_buffer_data(ctx, -1, &in_len);

    EVP_CIPHER_CTX *cipher_ctx;
    int out_len = 0;

    /* Create and initialise the context */
    if (!(cipher_ctx = EVP_CIPHER_CTX_new()))
        DUK_OPENSSL_ERROR(ctx);

    /* Retrieve the cipher by name */
    const EVP_CIPHER *cipher = EVP_get_cipherbyname(cipher_name);
    if (cipher == NULL)
        return duk_error(ctx, DUK_ERR_ERROR, "Cipher %s not found", cipher_name);

    /* Initialize the decryption operation with the found cipher, key and iv */
    if (!EVP_DecryptInit_ex(cipher_ctx, cipher, NULL, key, iv))
        DUK_OPENSSL_ERROR(ctx);

    /*
     * Decrypt the in buffer. DecryptUpdate is block aligned, so it can push up
     * to out_len + cipher_block_size bytes into the buffer
     */
    void *out_buffer = duk_push_dynamic_buffer(ctx, in_len + EVP_CIPHER_block_size(cipher));
    int current_len;
    do
    {
        if (!EVP_DecryptUpdate(cipher_ctx, out_buffer + out_len, &current_len, in_buffer, in_len))
            DUK_OPENSSL_ERROR(ctx);
        out_len += current_len;

    } while (current_len != 0);

    /*
     * Finalise the decryption. Further ciphertext bytes may be written at
     * this stage.
     */
    if (!EVP_DecryptFinal_ex(cipher_ctx, out_buffer + out_len, &current_len))
        DUK_OPENSSL_ERROR(ctx);

    out_len += current_len;

    /* Resize the buffer to the actual output length */
    duk_resize_buffer(ctx, -1, out_len);

    /* Clean up */
    EVP_CIPHER_CTX_free(cipher_ctx);

    return 1;
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
        void *in = duk_require_buffer_data(ctx, -1, &in_len);                 \
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
    void *in = duk_require_buffer_data(ctx, -1, &in_len);
    MD5_CTX md5_ctx;

    if (!MD5_Init(&md5_ctx))
        DUK_OPENSSL_ERROR(ctx);

    if (!MD5_Update(&md5_ctx, in, in_len))
        DUK_OPENSSL_ERROR(ctx);

    void *out = duk_push_fixed_buffer(ctx, MD5_DIGEST_LENGTH);
    if (!MD5_Final(out, &md5_ctx))
        DUK_OPENSSL_ERROR(ctx);

    return 1;
}

const duk_function_list_entry crypto_funcs[] = {
    {"encrypt", duk_encrypt, 1},
    {"decrypt", duk_decrypt, 1},
    {"sha224", duk_sha224, 1},
    {"sha256", duk_sha256, 1},
    {"sha384", duk_sha384, 1},
    {"sha512", duk_sha512, 1},
    {"md5", duk_md5, 1},
    {NULL, NULL, 0}};

duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_object(ctx);
    duk_put_function_list(ctx, -1, crypto_funcs);
    return 1;
}
