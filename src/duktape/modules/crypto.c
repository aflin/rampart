#include "../core/duktape.h"
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <string.h>
#include <stdio.h>

/* multiple of cipher block size */
#define OPENSSL_INIT_BUFFER_SIZE 32
#define OPENSSL_ERR_STRING_MAX_SIZE 1024
#define DUK_OPENSSL_ERROR(ctx)                                                     \
    {                                                                              \
        void *err_buf = duk_push_fixed_buffer(ctx, OPENSSL_ERR_STRING_MAX_SIZE);   \
        ERR_error_string_n(ERR_get_error(), err_buf, OPENSSL_ERR_STRING_MAX_SIZE); \
        return duk_error(ctx, DUK_ERR_ERROR, "OpenSSL Error: %s", err_buf);        \
    }
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
    duk_size_t in_buffer_len;
    void *in_buffer = duk_get_buffer_data(ctx, -1, &in_buffer_len);

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
    void *out_buffer = duk_push_dynamic_buffer(ctx, in_buffer_len + EVP_CIPHER_block_size(cipher) - 1);
    int current_len;
    do
    {
        if (!EVP_EncryptUpdate(cipher_ctx, out_buffer + out_len, &current_len, in_buffer, in_buffer_len))
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
    duk_size_t in_buffer_len;
    void *in_buffer = duk_get_buffer_data(ctx, -1, &in_buffer_len);

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
    void *out_buffer = duk_push_dynamic_buffer(ctx, in_buffer_len + EVP_CIPHER_block_size(cipher));
    int current_len;
    do
    {
        if (!EVP_DecryptUpdate(cipher_ctx, out_buffer + out_len, &current_len, in_buffer, in_buffer_len))
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

const duk_function_list_entry crypto_funcs[] = {
    {"encrypt", duk_encrypt, 1},
    {"decrypt", duk_decrypt, 1},
    {NULL, NULL, 0}};

duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_object(ctx);
    duk_put_function_list(ctx, -1, crypto_funcs);
    return 1;
}
