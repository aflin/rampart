/* Copyright (C) 2025 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include "rampart.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <float.h>

#define SIMSIMD_NATIVE_F16 0
#define SIMSIMD_NATIVE_BF16 0
#include <simsimd/simsimd.h>

extern simsimd_capability_t rp_runtime_caps;
extern int rp_runtime_caps_is_init;

// ---------------------- Scalar helper: f16<->f32 -----------------
static inline float half_to_float(uint16_t h)
{
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t frac = h & 0x03FFu;
    uint32_t f;
    if (exp == 0)
    {
        if (frac == 0)
        {
            f = sign;
            float out;
            memcpy(&out, &f, 4);
            return out;
        }
        else
        {
            // subnormal
            float val = ldexpf((float)frac / 1024.0f, -14);
            if (sign)
                val = -val;
            return val;
        }
    }
    else if (exp == 31)
    {
        f = sign | 0x7F800000u | (frac << 13);
        float out;
        memcpy(&out, &f, 4);
        return out;
    }
    else
    {
        f = sign | ((exp + 112) << 23) | (frac << 13);
        float out;
        memcpy(&out, &f, 4);
        return out;
    }
}

static inline uint16_t float_to_half(float x)
{
    uint32_t f;
    memcpy(&f, &x, 4);
    uint32_t sign = (f >> 16) & 0x8000u;
    uint32_t exp = (f >> 23) & 0xFFu;
    uint32_t frac = f & 0x7FFFFFu;
    uint16_t h;
    if (exp == 255)
    {
        h = (uint16_t)(sign | 0x7C00u | (frac ? 0x200u : 0));
    }
    else if (exp <= 112)
    {
        if (exp < 103)
            h = (uint16_t)sign; // underflow to zero
        else
        {
            uint32_t mant = (0x800000u | frac) >> (125 - exp + 13);
            h = (uint16_t)(sign | mant);
        }
    }
    else if (exp >= 143)
    {
        h = (uint16_t)(sign | 0x7C00u);
    }
    else
    {
        h = (uint16_t)(sign | ((exp - 112) << 10) | (frac >> 13));
    }
    return h;
}

// -------------------- bf16 <-> f32 conversions -------------------
// bf16 is stored as the HIGH 16 bits of an IEEE754 float32; low 16 are zero on expand.

static inline uint16_t f32_to_bf16_scalar(float x)
{
    // Round-to-nearest-even on the truncated 16 bits.
    // Equivalent to hardware converts like VCVTNEPS2BF16 when available.
    union {
        float f;
        uint32_t u;
    } v = {x};
    uint32_t t = v.u + 0x00007FFFu + ((v.u >> 16) & 1u);
    return (uint16_t)(t >> 16);
}

static inline float bf16_to_f32_scalar(uint16_t h)
{
    union {
        uint32_t u;
        float f;
    } v = {(uint32_t)h << 16};
    return v.f;
}

void bf16_to_f32(const uint16_t *src, float *dst, size_t n)
{
    size_t i = 0;
    for (; i < n; ++i)
        dst[i] = bf16_to_f32_scalar(src[i]);
}

void f32_to_bf16(const float *src, uint16_t *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = f32_to_bf16_scalar(src[i]);
}

// -------------------- bf16 <-> f64 conversions -------------------
void bf16_to_f64(const uint16_t *src, double *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = (double)bf16_to_f32_scalar(src[i]);
}

void f64_to_bf16(const double *src, uint16_t *dst, size_t n)
{
    size_t i = 0;
    for (; i < n; ++i)
        dst[i] = f32_to_bf16_scalar((float)src[i]);
}


// -------------------- f16 <-> f32 conversions -------------------
void f16_to_f32(const uint16_t *src, float *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = half_to_float(src[i]);
}

void f32_to_f16(const float *src, uint16_t *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = float_to_half(src[i]);
}

// -------------------- f64 <-> f32 conversions -------------------
void f64_to_f32(const double *src, float *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = (float)src[i];
}

void f32_to_f64(const float *src, double *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = (double)src[i];
}

// -------------------- f64 <-> f16 conversions -------------------
void f64_to_f16(const double *src, uint16_t *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = float_to_half((float)src[i]);
}


void f16_to_f64(const uint16_t *src, double *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = (double)half_to_float(src[i]);
}

// ---------------- Quantize/Dequant: f32 <-> i8/u8 ---------------
void f32_to_i8(const float *src, int8_t *dst, size_t n, float scale, int zp)
{
    const float inv = 1.0f / scale;
    for (size_t i = 0; i < n; ++i)
    {
        int q = (int)lrintf(src[i] * inv + (float)zp);
        if (q < -128)
            q = -128;
        else if (q > 127)
            q = 127;
        dst[i] = (int8_t)q;
    }
}

void i8_to_f32(const int8_t *src, float *dst, size_t n, float scale, int zp)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = ((float)src[i] - (float)zp) * scale;
}

void f32_to_u8(const float *src, uint8_t *dst, size_t n, float scale, int zp)
{
    const float inv = 1.0f / scale;
    for (size_t i = 0; i < n; ++i)
    {
        int q = (int)lrintf(src[i] * inv + (float)zp);
        if (q < 0)
            q = 0;
        else if (q > 255)
            q = 255;
        dst[i] = (uint8_t)q;
    }
}

void u8_to_f32(const uint8_t *src, float *dst, size_t n, float scale, int zp)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = ((float)src[i] - (float)zp) * scale;
}

// ---------------- Quantize/Dequant: f64 <-> i8/u8 ---------------
void f64_to_i8(const double *src, int8_t *dst, size_t n, double scale, int zp)
{
    const double inv = 1.0 / scale;
    for (size_t i = 0; i < n; ++i)
    {
        long q = lrint(src[i] * inv + (double)zp);
        if (q < -128)
            q = -128;
        else if (q > 127)
            q = 127;
        dst[i] = (int8_t)q;
    }
}

void i8_to_f64(const int8_t *src, double *dst, size_t n, double scale, int zp)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = ((double)src[i] - (double)zp) * scale;
}

void f64_to_u8(const double *src, uint8_t *dst, size_t n, double scale, int zp)
{
    const double inv = 1.0 / scale;
    for (size_t i = 0; i < n; ++i)
    {
        long q = lrint(src[i] * inv + (double)zp);
        if (q < 0)
            q = 0;
        else if (q > 255)
            q = 255;
        dst[i] = (uint8_t)q;
    }
}

void u8_to_f64(const uint8_t *src, double *dst, size_t n, double scale, int zp)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = ((double)src[i] - (double)zp) * scale;
}

// ---------------- Quantize/Dequant: f16 <-> i8/u8 ---------------
/* unused currently
void f16_to_i8(const uint16_t *src, int8_t *dst, size_t n, float scale, int zp)
{
    float buf[256];
    size_t i = 0;
    while (i < n)
    {
        size_t blk = (n - i) < 256 ? (n - i) : 256;
        f16_to_f32(src + i, buf, blk);
        f32_to_i8(buf, dst + i, blk, scale, zp);
        i += blk;
    }
}
*/

void i8_to_f16(const int8_t *src, uint16_t *dst, size_t n, float scale, int zp)
{
    float buf[256];
    size_t i = 0;
    while (i < n)
    {
        size_t blk = (n - i) < 256 ? (n - i) : 256;
        i8_to_f32(src + i, buf, blk, scale, zp);
        f32_to_f16(buf, dst + i, blk);
        i += blk;
    }
}
/* unused currently
void f16_to_u8(const uint16_t *src, uint8_t *dst, size_t n, float scale, int zp)
{
    float buf[256];
    size_t i = 0;
    while (i < n)
    {
        size_t blk = (n - i) < 256 ? (n - i) : 256;
        f16_to_f32(src + i, buf, blk);
        f32_to_u8(buf, dst + i, blk, scale, zp);
        i += blk;
    }
}
*/

void u8_to_f16(const uint8_t *src, uint16_t *dst, size_t n, float scale, int zp)
{
    float buf[256];
    size_t i = 0;
    while (i < n)
    {
        size_t blk = (n - i) < 256 ? (n - i) : 256;
        u8_to_f32(src + i, buf, blk, scale, zp);
        f32_to_f16(buf, dst + i, blk);
        i += blk;
    }
}

// -------------------------- b8 pack/unpack ----------------------
void f64_to_b8_threshold(const double *src, uint8_t *dst_bits, size_t n, double thresh)
{
    size_t i = 0, byte = 0;
    while (i + 8 <= n)
    {
        uint8_t b = 0;
        for (int k = 0; k < 8; ++k)
            b |= (src[i + k] > thresh) ? (1u << k) : 0;
        dst_bits[byte++] = b;
        i += 8;
    }
    if (i < n)
    {
        uint8_t b = 0;
        int bit = 0;
        for (; i < n; ++i, ++bit)
            if (src[i] > thresh)
                b |= (1u << bit);
        dst_bits[byte++] = b;
    }
}

void b8_to_u8_bytes(const uint8_t *src_bits, uint8_t *dst_bytes, size_t n)
{
    size_t i = 0, byte = 0;
    while (i + 8 <= n)
    {
        uint8_t b = src_bits[byte++];
        dst_bytes[i + 0] = (b & 0x01) ? 1 : 0;
        dst_bytes[i + 1] = (b & 0x02) ? 1 : 0;
        dst_bytes[i + 2] = (b & 0x04) ? 1 : 0;
        dst_bytes[i + 3] = (b & 0x08) ? 1 : 0;
        dst_bytes[i + 4] = (b & 0x10) ? 1 : 0;
        dst_bytes[i + 5] = (b & 0x20) ? 1 : 0;
        dst_bytes[i + 6] = (b & 0x40) ? 1 : 0;
        dst_bytes[i + 7] = (b & 0x80) ? 1 : 0;
        i += 8;
    }
    if (i < n)
    {
        uint8_t b = src_bits[byte++];
        int bit = 0;
        for (; i < n; ++i, ++bit)
            dst_bytes[i] = (b & (1u << bit)) ? 1 : 0;
    }
}

// i4 conversions
void i8_to_i4(const int8_t *in, uint8_t *out, size_t dim) {
    if (!in || !out) return;

    size_t i = 0, o = 0;

    for (; i + 1 < dim; i += 2, ++o) {
        int v0 = in[i + 0]; if (v0 > 7) v0 = 7; else if (v0 < -8) v0 = -8;
        int v1 = in[i + 1]; if (v1 > 7) v1 = 7; else if (v1 < -8) v1 = -8;
        uint8_t lo = (uint8_t)((v0 < 0) ? (v0 + 16) : v0);
        uint8_t hi = (uint8_t)((v1 < 0) ? (v1 + 16) : v1);
        out[o] = (uint8_t)(lo | (hi << 4));
    }
    if (i < dim) {
        int v0 = in[i]; if (v0 > 7) v0 = 7; else if (v0 < -8) v0 = -8;
        uint8_t lo = (uint8_t)((v0 < 0) ? (v0 + 16) : v0);
        out[o++] = lo; // high nibble zero
    }
}

//
// i4x2 -> i8  (unpack two signed 4-bit values from each byte)
//   in:  uint8_t in[(dim+1)/2]  where dim = number of 4-bit values
//   out: int8_t  out[dim]       values in [-8, 7]
//
void i4_to_i8(const uint8_t *in, int8_t *out, size_t dim) {
    if (!in || !out) return;

    size_t k = 0, o = 0;
    for (; k + 1 < dim; k += 2, ++o) {
        uint8_t byte = in[o];
        uint8_t lo =  byte        & 0x0F;
        uint8_t hi = (byte >> 4)  & 0x0F;
        out[k + 0] = (int8_t)( (lo > 7) ? (lo - 16) : lo );
        out[k + 1] = (int8_t)( (hi > 7) ? (hi - 16) : hi );
    }
    if (k < dim) {
        uint8_t byte = in[o];
        uint8_t lo = byte & 0x0F;
        out[k] = (int8_t)( (lo > 7) ? (lo - 16) : lo );
    }
}

// end conversions

// l2 normalization
static void l2_normalize(float *x, int d)
{
    if (!x || d <= 0)
        return;

    // -------------------- pass 1: sum of squares --------------------
    double sum = 0.0;

    for (int i = 0; i < d; ++i)
        sum += (double)x[i] * (double)x[i];

    if (!(sum > 0.0))
        return;

    const double inv = 1.0 / sqrt(sum);

    // -------------------- pass 2: scale by inv ----------------------
    for (int i = 0; i < d; ++i)
        x[i] = (float)(x[i] * inv);
}

static void l2_normalize_d(double *x, int d)
{
    if (!x || d <= 0)
        return;

    // -------------------- pass 1: sum of squares --------------------
    double sum = 0.0;

    for (int i = 0; i < d; ++i)
        sum += x[i] * x[i];
    if (!(sum > 0.0))
        return;

    const double inv = 1.0 / sqrt(sum);

    // -------------------- pass 2: scale by inv ----------------------
    for (int i = 0; i < d; ++i)
        x[i] *= inv;
}
// end l2 normalization

#define RP_F64 0
#define RP_F32 1
#define RP_F16 2
#define RP_BF16 3

// u8 js conversions
static duk_ret_t rp_f64_to_u8(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    double scale=-1.0;
    int zp=0;
    double *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f64ToU8 - argument must be a Buffer");
    dim = sz / sizeof(double);

    if(!duk_is_undefined(ctx, 1))
    {
        scale = REQUIRE_NUMBER(ctx, 1, "vector.f64ToU8 - second argument, if present must be a Positive Number (scale)");
        if(scale <= 0.0)
            RP_THROW(ctx, "vector.f64ToU8 - second argument, if present must be a Positive Number (scale)");
    }

    if(!duk_is_undefined(ctx, 2))
    {
        zp = REQUIRE_POSINT(ctx, 2, "vector.f64ToU8 - third argument, if present must be a Positive Int 0-255 (zero point)");
        if(zp < 0 || zp >255)
            RP_THROW(ctx, "vector.f64ToU8 - third argument, if present must be a Positive Int 0-255 (zero point)");
    }

    uint8_t *out = (uint8_t *)duk_push_fixed_buffer(ctx, dim); 
    double min=DBL_MAX, max=-DBL_MAX;
    if(scale < 0.0 || zp < 0)
    {
        for(size_t i=0; i<dim; i++)
        {
            double d = in[i];
            if(d>max)
                max=d;
            if(d<min)
                min=d;
        }
        if(scale < 0.0)
            scale = (max-min)/255.0;
    }

    f64_to_u8(in, out, dim, scale, zp);
    return 1;
}

static duk_ret_t rp_u8_to_f64(duk_context *ctx)
{
    duk_size_t dim = 0;
    double scale=1.0/255.0;
    int zp=0;
    uint8_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &dim, "vector.u8ToF64 - argument must be a Buffer");

    if(!duk_is_undefined(ctx, 1))
    {
        scale = REQUIRE_NUMBER(ctx, 1, "vector.u8ToF64 - second argument, if present must be a Positive Number (scale)");
        if(scale <= 0.0)
            RP_THROW(ctx, "vector.u8ToF64 - second argument, if present must be a Positive Number (scale)");
    }

    if(!duk_is_undefined(ctx, 2))
    {
        zp = REQUIRE_POSINT(ctx, 2, "vector.u8ToF64 - third argument, if present must be a Positive Int 0-255 (zero point)");
        if(zp < 0 || zp >255)
            RP_THROW(ctx, "vector.u8ToF64 - third argument, if present must be a Positive Int 0-255 (zero point)");
    }

    double *out = (double *)duk_push_fixed_buffer(ctx, dim*sizeof(double)); 
    u8_to_f64(in, out, dim, scale, zp);
    return 1;
}

static duk_ret_t rp_f32_to_u8(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    float scale=-1.0;
    int zp=0;
    float *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f32ToU8 - argument must be a Buffer");
    dim = sz / sizeof(float);

    if(!duk_is_undefined(ctx, 1))
    {
        scale = (float)REQUIRE_NUMBER(ctx, 1, "vector.f32ToU8 - second argument, if present must be a Positive Number (scale)");
        if(scale <= 0.0)
            RP_THROW(ctx, "vector.f32ToU8 - second argument, if present must be a Positive Number (scale)");
    }

    if(!duk_is_undefined(ctx, 2))
    {
        zp = REQUIRE_POSINT(ctx, 2, "vector.f32ToU8 - third argument, if present must be a Positive Int 0-255 (zero point)");
        if(zp < 0 || zp >255)
            RP_THROW(ctx, "vector.f32ToU8 - third argument, if present must be a Positive Int 0-255 (zero point)");
    }

    uint8_t *out = (uint8_t *)duk_push_fixed_buffer(ctx, dim); 
    float min=FLT_MAX, max=-FLT_MAX;
    if(scale < 0.0 || zp < 0)
    {
        for(size_t i=0; i<dim; i++)
        {
            float d = in[i];
            if(d>max)
                max=d;
            if(d<min)
                min=d;
        }
        if(scale < 0.0)
            scale = (max-min)/255.0;
    }

    f32_to_u8(in, out, dim, scale, zp);
    return 1;
}

static duk_ret_t rp_u8_to_f32(duk_context *ctx)
{
    duk_size_t dim = 0;
    float scale=1.0/255.0;
    int zp=0;
    uint8_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &dim, "vector.u8ToF32 - argument must be a Buffer");

    if(!duk_is_undefined(ctx, 1))
    {
        scale = (float)REQUIRE_NUMBER(ctx, 1, "vector.u8ToF32 - second argument, if present must be a Positive Number (scale)");
        if(scale <= 0.0)
            RP_THROW(ctx, "vector.u8ToF32 - second argument, if present must be a Positive Number (scale)");
    }

    if(!duk_is_undefined(ctx, 2))
    {
        zp = REQUIRE_POSINT(ctx, 2, "vector.u8ToF32 - third argument, if present must be a Positive Int 0-255 (zero point)");
        if(zp < 0 || zp >255)
            RP_THROW(ctx, "vector.u8ToF32 - third argument, if present must be a Positive Int 0-255 (zero point)");
    }

    float *out = (float *)duk_push_fixed_buffer(ctx, dim*sizeof(float)); 
    u8_to_f32(in, out, dim, scale, zp);
    return 1;
}

static duk_ret_t numbers_to(duk_context *ctx, duk_idx_t arridx, int type);
static duk_ret_t rp_num_to_u8(duk_context *ctx)
{
    REQUIRE_ARRAY(ctx, 0, "vector.numbersToU8 - first argument must be an Array of Numbers");
    numbers_to(ctx, 0, RP_F32);
    duk_replace(ctx, 0);
    return rp_f32_to_u8(ctx);
}

static duk_ret_t f32_to_num(duk_context *ctx);
static duk_ret_t rp_u8_to_num(duk_context *ctx)
{
    REQUIRE_BUFFER_DATA(ctx, 0, NULL, "vector.u8ToNumbers - first argument must be a Buffer");
    rp_u8_to_f32(ctx);
    duk_replace(ctx, 0);
    return f32_to_num(ctx);
}

static duk_ret_t rp_f16_to_u8(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    float scale=-1.0;
    int zp=0;
    uint16_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f16ToU8 - argument must be a Buffer");
    dim = sz / sizeof(uint16_t);

    if(!duk_is_undefined(ctx, 1))
    {
        scale = (float)REQUIRE_NUMBER(ctx, 1, "vector.f16ToU8 - second argument, if present must be a Positive Number (scale)");
        if(scale <= 0.0)
            RP_THROW(ctx, "vector.f16ToU8 - second argument, if present must be a Positive Number (scale)");
    }

    if(!duk_is_undefined(ctx, 2))
    {
        zp = REQUIRE_POSINT(ctx, 2, "vector.f16ToU8 - third argument, if present must be a Positive Int 0-255 (zero point)");
        if(zp < 0 || zp >255)
            RP_THROW(ctx, "vector.f16ToU8 - third argument, if present must be a Positive Int 0-255 (zero point)");
    }

    uint8_t *out = (uint8_t *)duk_push_fixed_buffer(ctx, dim); 
    float *in32 = NULL;

    // up convert to floats, so we can do min/max
    REMALLOC(in32, dim*sizeof(float));
    f16_to_f32(in, in32, dim);

    float min=FLT_MAX, max=-FLT_MAX;
    if(scale < 0.0 || zp < 0)
    {
        for(size_t i=0; i<dim; i++)
        {
            float d = in32[i];
            if(d>max)
                max=d;
            if(d<min)
                min=d;
        }
        if(scale < 0.0)
            scale = (max-min)/255.0;
    }

    f32_to_u8(in32, out, dim, scale, zp);

    free(in32);
    return 1;
}

static duk_ret_t rp_u8_to_f16(duk_context *ctx)
{
    duk_size_t dim = 0;
    float scale=1.0/255.0;
    int zp=0;
    uint8_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &dim, "vector.u8ToF16 - argument must be a Buffer");

    if(!duk_is_undefined(ctx, 1))
    {
        scale = (float)REQUIRE_NUMBER(ctx, 1, "vector.u8ToF16 - second argument, if present must be a Positive Number (scale)");
        if(scale <= 0.0)
            RP_THROW(ctx, "vector.u8ToF16 - second argument, if present must be a Positive Number (scale)");
    }

    if(!duk_is_undefined(ctx, 2))
    {
        zp = REQUIRE_POSINT(ctx, 2, "vector.u8ToF16 - third argument, if present must be a Positive Int 0-255 (zero point)");
        if(zp < 0 || zp >255)
            RP_THROW(ctx, "vector.u8ToF16 - third argument, if present must be a Positive Int 0-255 (zero point)");
    }

    uint16_t *out = (uint16_t *)duk_push_fixed_buffer(ctx, dim*sizeof(uint16_t)); 
    u8_to_f16(in, out, dim, scale, zp);
    return 1;
}
// end u8 js conversions

// i8 js converstions
static duk_ret_t rp_f64_to_i8(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    double scale=-1.0;
    int zp=0;
    double *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f64ToI8 - argument must be a Buffer");
    dim = sz / sizeof(double);

    if(!duk_is_undefined(ctx, 1))
    {
        scale = REQUIRE_NUMBER(ctx, 1, "vector.f64ToI8 - second argument, if present must be a Positive Number (scale)");
        if(scale <= 0.0)
            RP_THROW(ctx, "vector.f64ToI8 - second argument, if present must be a Positive Number (scale)");
    }

    if(!duk_is_undefined(ctx, 2))
    {
        zp = REQUIRE_INT(ctx, 2, "vector.f64ToI8 - third argument, if present must be a Int -128 - 127 (zero point)");
        if(zp < -128 || zp > 127)
            RP_THROW(ctx, "vector.f64ToI8 - third argument, if present must be a Int -128 - 127 (zero point)");
    }

    int8_t *out = (int8_t *)duk_push_fixed_buffer(ctx, dim); 
    double min=DBL_MAX, max=-DBL_MAX;
    if(scale < 0.0 || zp < -128)
    {
        for(size_t i=0; i<dim; i++)
        {
            double d = in[i];
            if(d>max)
                max=d;
            if(d<min)
                min=d;
        }
        if(scale < 0.0)
            scale = (max-min)/254.0;
    }

    f64_to_i8(in, out, dim, scale, zp);
    return 1;
}

static duk_ret_t rp_i8_to_f64(duk_context *ctx)
{
    duk_size_t dim = 0;
    double scale=1.0/127.0;
    int zp=0;
    int8_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &dim, "vector.i8ToF64 - argument must be a Buffer");

    if(!duk_is_undefined(ctx, 1))
    {
        scale = REQUIRE_NUMBER(ctx, 1, "vector.i8ToF64 - second argument, if present must be a Positive Number (scale)");
        if(scale <= 0.0)
            RP_THROW(ctx, "vector.i8ToF64 - second argument, if present must be a Positive Number (scale)");
    }

    if(!duk_is_undefined(ctx, 2))
    {
        zp = REQUIRE_INT(ctx, 2, "vector.i8ToF64 - third argument, if present must be a Int -128 - 127 (zero point)");
        if(zp < -128 || zp > 127)
            RP_THROW(ctx, "vector.i8ToF64 - third argument, if present must be a Int -128 - 127 (zero point)");
    }

    double *out = (double *)duk_push_fixed_buffer(ctx, dim*sizeof(double)); 
    i8_to_f64(in, out, dim, scale, zp);
    return 1;
}

static duk_ret_t rp_f32_to_i8(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    float scale=-1.0;
    int zp=0;
    float *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f32ToI8 - argument must be a Buffer");
    dim = sz / sizeof(float);

    if(!duk_is_undefined(ctx, 1))
    {
        scale = (float)REQUIRE_NUMBER(ctx, 1, "vector.f32ToI8 - second argument, if present must be a Positive Number (scale)");
        if(scale <= 0.0)
            RP_THROW(ctx, "vector.f32ToI8 - second argument, if present must be a Positive Number (scale)");
    }

    if(!duk_is_undefined(ctx, 2))
    {
        zp = REQUIRE_INT(ctx, 2, "vector.f32ToI8 - third argument, if present must be a Int -128 - 127 (zero point)");
        if(zp < -128 || zp > 127)
            RP_THROW(ctx, "vector.f32ToI8 - third argument, if present must be a Int -128 - 127 (zero point)");
    }

    int8_t *out = (int8_t *)duk_push_fixed_buffer(ctx, dim); 
    float min=FLT_MAX, max=-FLT_MAX;
    if(scale < 0.0 || zp < -128)
    {
        for(size_t i=0; i<dim; i++)
        {
            float d = in[i];
            if(d>max)
                max=d;
            if(d<min)
                min=d;
        }
        if(scale < 0.0)
            scale = (max-min)/254.0;
    }

    f32_to_i8(in, out, dim, scale, zp);

    return 1;
}

static duk_ret_t rp_i8_to_f32(duk_context *ctx)
{
    duk_size_t dim = 0;
    float scale=1.0/127.0;
    int zp=0;
    int8_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &dim, "vector.i8ToF32 - argument must be a Buffer");

    if(!duk_is_undefined(ctx, 1))
    {
        scale = (float)REQUIRE_NUMBER(ctx, 1, "vector.i8ToF32 - second argument, if present must be a Positive Number (scale)");
        if(scale <= 0.0)
            RP_THROW(ctx, "vector.i8ToF32 - second argument, if present must be a Positive Number (scale)");
    }

    if(!duk_is_undefined(ctx, 2))
    {
        zp = REQUIRE_INT(ctx, 2, "vector.i8ToF32 - third argument, if present must be a Int -128 - 127 (zero point)");
        if(zp < -128 || zp > 127)
            RP_THROW(ctx, "vector.i8ToF32 - third argument, if present must be a Int -128 - 127 (zero point)");
    }

    float *out = (float *)duk_push_fixed_buffer(ctx, dim*sizeof(float)); 
    i8_to_f32(in, out, dim, scale, zp);
    return 1;
}
static duk_ret_t rp_num_to_i8(duk_context *ctx)
{
    REQUIRE_ARRAY(ctx, 0, "vector.numbersToI8 - first argument must be an Array of Numbers");
    numbers_to(ctx, 0, RP_F32);
    duk_replace(ctx, 0);
    return rp_f32_to_i8(ctx);
}

static duk_ret_t rp_i8_to_num(duk_context *ctx)
{
    REQUIRE_BUFFER_DATA(ctx, 0, NULL, "vector.i8ToNumbers - first argument must be a Buffer");
    rp_i8_to_f32(ctx);
    duk_replace(ctx, 0);
    return f32_to_num(ctx);
}

static duk_ret_t rp_f16_to_i8(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    float scale=-1.0;
    int zp=0;
    uint16_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f16ToI8 - argument must be a Buffer");
    dim = sz / sizeof(uint16_t);

    if(!duk_is_undefined(ctx, 1))
    {
        scale = (float)REQUIRE_NUMBER(ctx, 1, "vector.f16ToI8 - second argument, if present must be a Positive Number (scale)");
        if(scale <= 0.0)
            RP_THROW(ctx, "vector.f16ToI8 - second argument, if present must be a Positive Number (scale)");
    }

    if(!duk_is_undefined(ctx, 2))
    {
        zp = REQUIRE_INT(ctx, 2, "vector.f16ToI8 - third argument, if present must be a Int -128 - 127 (zero point)");
        if(zp < -128 || zp > 127)
            RP_THROW(ctx, "vector.f16ToI8 - third argument, if present must be a Int -128 - 127 (zero point)");
    }

    int8_t *out = (int8_t *)duk_push_fixed_buffer(ctx, dim); 
    float *in32 = NULL;

    REMALLOC(in32, dim*sizeof(float));
    f16_to_f32(in, in32, dim);

    float min=FLT_MAX, max=-FLT_MAX;
    if(scale < 0.0 || zp < -128)
    {
        for(size_t i=0; i<dim; i++)
        {
            float d = in32[i];
            if(d>max)
                max=d;
            if(d<min)
                min=d;
        }
        if(scale < 0.0)
            scale = (max-min)/254.0;
    }

    f32_to_i8(in32, out, dim, scale, zp);

    free(in32);
    return 1;
}

static duk_ret_t rp_i8_to_f16(duk_context *ctx)
{
    duk_size_t dim = 0;
    float scale=1.0/127.0;
    int zp=0;
    int8_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &dim, "vector.i8ToF16 - argument must be a Buffer");

    if(!duk_is_undefined(ctx, 1))
    {
        scale = (float)REQUIRE_NUMBER(ctx, 1, "vector.i8ToF16 - second argument, if present must be a Positive Number (scale)");
        if(scale <= 0.0)
            RP_THROW(ctx, "vector.i8ToF16 - second argument, if present must be a Positive Number (scale)");
    }

    if(!duk_is_undefined(ctx, 2))
    {
        zp = REQUIRE_INT(ctx, 2, "vector.i8ToF16 - third argument, if present must be a Int -128 - 127 (zero point)");
        if(zp < -128 || zp > 127)
            RP_THROW(ctx, "vector.i8ToF16 - third argument, if present must be a Int -128 - 127 (zero point)");
    }

    uint16_t *out = (uint16_t *)duk_push_fixed_buffer(ctx, dim*sizeof(uint16_t)); 
    i8_to_f16(in, out, dim, scale, zp);
    return 1;
}
// end i8 js conversions


static duk_ret_t rp_i8_to_i4(duk_context *ctx)
{
    duk_size_t dim=0, sz4=0;
    uint8_t *out;
    int8_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &dim, "vector.i8Toi4 - argument must be a Buffer");
    
    if(dim%2)
        RP_THROW(ctx, "vector.i8Toi4 - i8 vector must have an even number of elements");

    sz4 = dim/2;
    out = duk_push_fixed_buffer(ctx, sz4);

    i8_to_i4(in, out, dim);
    return 1;
}

static duk_ret_t rp_i4_to_i8(duk_context *ctx)
{
    duk_size_t dim=0, sz4=0;
    int8_t *out;
    uint8_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz4, "vector.i4Toi8 - argument must be a Buffer");
    dim = sz4 * 2;

    out = duk_push_fixed_buffer(ctx, sz4);
    i4_to_i8(in, out, dim);
    return 1;
}

// end i4 conversions

// float js conversions
static duk_ret_t rp_f16_to_f32(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    uint16_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f16ToF32 - argument must be a Buffer");
    dim = sz / sizeof(uint16_t); // f16 - two bytes

    float *out = duk_push_fixed_buffer(ctx, dim * sizeof(float));

    f16_to_f32(in, out, dim);

    return 1;
}

static duk_ret_t rp_f32_to_f16(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    float *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f32ToF16 - argument must be a Buffer");
    dim = sz / sizeof(float); // f32 - four bytes

    uint16_t *out = duk_push_fixed_buffer(ctx, dim * sizeof(uint16_t));

    f32_to_f16(in, out, dim);

    return 1;
}

static duk_ret_t rp_bf16_to_f32(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    uint16_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.bf16ToF32 - argument must be a Buffer");
    dim = sz / sizeof(uint16_t); // f16 - two bytes

    float *out = duk_push_fixed_buffer(ctx, dim * sizeof(float));

    bf16_to_f32(in, out, dim);

    return 1;
}

static duk_ret_t rp_f32_to_bf16(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    float *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f32ToBf16 - argument must be a Buffer");
    dim = sz / sizeof(float); // f32 - four bytes

    uint16_t *out = duk_push_fixed_buffer(ctx, dim * sizeof(uint16_t));

    f32_to_bf16(in, out, dim);

    return 1;
}

static duk_ret_t rp_f64_to_f32(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    double *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f64ToF32 - argument must be a Buffer");
    dim = sz / sizeof(double);

    float *out = duk_push_fixed_buffer(ctx, dim * sizeof(float));

    f64_to_f32(in, out, dim);

    return 1;
}

static duk_ret_t rp_f32_to_f64(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    float *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f32ToF64 - argument must be a Buffer");
    dim = sz / sizeof(float); // f32 - four bytes

    double *out = duk_push_fixed_buffer(ctx, dim * sizeof(double));

    f32_to_f64(in, out, dim);

    return 1;
}

static duk_ret_t rp_f64_to_f16(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    double *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f64ToF16 - argument must be a Buffer");
    dim = sz / sizeof(double);

    uint16_t *out = duk_push_fixed_buffer(ctx, dim * sizeof(uint16_t));

    f64_to_f16(in, out, dim);

    return 1;
}

static duk_ret_t rp_f16_to_f64(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    uint16_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f16ToF64 - argument must be a Buffer");
    dim = sz / sizeof(uint16_t); // f16 - tw0 bytes

    double *out = duk_push_fixed_buffer(ctx, dim * sizeof(double));

    f16_to_f64(in, out, dim);

    return 1;
}

static duk_ret_t rp_f64_to_bf16(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    double *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f64ToBf16 - argument must be a Buffer");
    dim = sz / sizeof(double);

    uint16_t *out = duk_push_fixed_buffer(ctx, dim * sizeof(uint16_t));

    f64_to_bf16(in, out, dim);

    return 1;
}

static duk_ret_t rp_bf16_to_f64(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    uint16_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.bf16ToF64 - argument must be a Buffer");
    dim = sz / sizeof(uint16_t); // f16 - tw0 bytes

    double *out = duk_push_fixed_buffer(ctx, dim * sizeof(double));

    bf16_to_f64(in, out, dim);

    return 1;
}

static duk_ret_t numbers_to(duk_context *ctx, duk_idx_t arridx, int type)
{
    size_t i = 0, len = (size_t)duk_get_length(ctx, arridx);

    if (type == RP_F64)
    {
        double *out64 = duk_push_fixed_buffer(ctx, len * sizeof(double));
        for (; i < len; i++)
        {
            duk_get_prop_index(ctx, arridx, (duk_uarridx_t)i);
            out64[i] = REQUIRE_NUMBER(ctx, -1, "utils.numbersToF - array[%lu] is not a Number", i);
            duk_pop(ctx);
        }
        return 1;
    }

    if (type == RP_BF16)
    {
        uint16_t *outbf16 = duk_push_fixed_buffer(ctx, len * sizeof(uint16_t));
        double *out64 = NULL;

        REMALLOC(out64, len * sizeof(double));

        for (; i < len; i++)
        {
            duk_get_prop_index(ctx, arridx, (duk_uarridx_t)i);
            out64[i] = REQUIRE_NUMBER(ctx, -1, "utils.numbersToF - array[%lu] is not a Number", i);
            duk_pop(ctx);
        }

        f64_to_bf16(out64, outbf16, len);

        free(out64);
        return 1;
    }

    float *out = duk_push_fixed_buffer(ctx, len * sizeof(float));

    for (; i < len; i++)
    {
        duk_get_prop_index(ctx, arridx, (duk_uarridx_t)i);
        out[i] = (float)REQUIRE_NUMBER(ctx, -1, "utils.numbersToF - array[%lu] is not a Number", i);
        duk_pop(ctx);
    }

    if (type == RP_F16)
    {
        uint16_t *out16 = duk_push_fixed_buffer(ctx, len * sizeof(uint16_t));
        f32_to_f16(out, out16, len);
    }

    return 1;
}

static duk_ret_t num_to_f64(duk_context *ctx)
{
    REQUIRE_ARRAY(ctx, 0, "utils.numbersToF64 - argument must be an Array of Numbers");
    return numbers_to(ctx, 0, RP_F64);
}

static duk_ret_t num_to_f32(duk_context *ctx)
{
    REQUIRE_ARRAY(ctx, 0, "utils.numbersToF32 - argument must be an Array of Numbers");
    return numbers_to(ctx, 0, RP_F32);
}

static duk_ret_t num_to_f16(duk_context *ctx)
{
    REQUIRE_ARRAY(ctx, 0, "utils.numbersToF16 - argument must be an Array of Numbers");
    return numbers_to(ctx, 0, RP_F16);
}

static duk_ret_t num_to_bf16(duk_context *ctx)
{
    REQUIRE_ARRAY(ctx, 0, "utils.numbersToBf16 - argument must be an Array of Numbers");
    return numbers_to(ctx, 0, RP_BF16);
}

static duk_ret_t f_to_num(duk_context *ctx, void *buf, size_t sz, int type)
{
    float *freebuf = NULL, *f32 = NULL;
    size_t dim, i = 0;

    if (type == RP_F64)
    {
        if (!sz || sz % 8)
            RP_THROW(ctx, "f64ToNumbers - invalid buffer data");

        double *f64 = (double *)buf;
        dim = sz / sizeof(double);
        duk_push_array(ctx);
        for (; i < dim; i++)
        {
            duk_push_number(ctx, f64[i]);
            duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
        }
        return 1;
    }

    if (type == RP_BF16)
    {
        if (!sz || sz % 2)
            RP_THROW(ctx, "bf16ToNumbers - invalid buffer data");

        uint16_t *bf16 = (uint16_t *)buf;
        double *f64 = NULL;

        dim = sz / sizeof(uint16_t);

        REMALLOC(f64, dim * sizeof(double));
        bf16_to_f64(bf16, f64, dim);

        duk_push_array(ctx);
        for (; i < dim; i++)
        {
            duk_push_number(ctx, f64[i]);
            duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
        }
        free(f64);
        return 1;
    }

    if (type == RP_F16)
    {
        if (!sz || sz % 2)
            RP_THROW(ctx, "f16ToNumbers - invalid buffer data");
        dim = sz / sizeof(uint16_t);
        REMALLOC(freebuf, dim * sizeof(float));
        f32 = freebuf;
        f16_to_f32((uint16_t *)buf, f32, dim);
    }
    else
    {
        if (!sz || sz % 4)
            RP_THROW(ctx, "f32ToNumbers - invalid buffer data");
        dim = sz / sizeof(float);
        ;
        f32 = (float *)buf;
    }
    duk_push_array(ctx);
    for (; i < dim; i++)
    {
        duk_push_number(ctx, (double)f32[i]);
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
    }

    if (freebuf)
        free(freebuf);

    return 1;
}

static duk_ret_t f64_to_num(duk_context *ctx)
{
    size_t sz;
    void *v = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "f64ToNumbers - argument must be a Buffer");
    return f_to_num(ctx, v, sz, RP_F64);
}

static duk_ret_t f32_to_num(duk_context *ctx)
{
    size_t sz;
    void *v = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "f32ToNumbers - argument must be a Buffer");
    return f_to_num(ctx, v, sz, RP_F32);
}

static duk_ret_t f16_to_num(duk_context *ctx)
{
    size_t sz;
    void *v = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "f16ToNumbers - argument must be a Buffer");
    return f_to_num(ctx, v, sz, RP_F16);
}

static duk_ret_t bf16_to_num(duk_context *ctx)
{
    size_t sz;
    void *v = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "bf16ToNumbers - argument must be a Buffer");
    return f_to_num(ctx, v, sz, RP_BF16);
}
// end js float conversions

// simsimd distance calcs
static duk_ret_t vdistance(duk_context *ctx)
{
    // defaults:
    const char *metric = "dot";
    const char *datatype = "f16";
    const char *err = NULL;
    duk_size_t asz, bsz;
    void *a, *b;

    if (!duk_is_undefined(ctx, 3))
        datatype = REQUIRE_STRING(ctx, 3, "Fourth argument, if present, must be a datatype (default: 'f16')");

    if (strcasecmp("number", datatype) == 0 || strcasecmp("numbers", datatype) == 0)
    {
        REQUIRE_ARRAY(ctx, 0, "First argument must be an Array of Numbers (vector)");
        REQUIRE_ARRAY(ctx, 1, "Second argument must be an Array of Numbers (vector)");

        numbers_to(ctx, 0, RP_F64);
        duk_replace(ctx, 0);

        numbers_to(ctx, 1, RP_F64);
        duk_replace(ctx, 1);

        datatype = "f64";
    }
    a = REQUIRE_BUFFER_DATA(ctx, 0, &asz, "First argument must be a Buffer (vector)");
    b = REQUIRE_BUFFER_DATA(ctx, 1, &bsz, "Second argument must be a Buffer (vector)");
    double ret;

    if (!duk_is_undefined(ctx, 2))
        metric = REQUIRE_STRING(ctx, 2, "Third argument, if present, must be a metric (default: 'dot')");

    if (!asz || asz != bsz)
        RP_THROW(ctx, "vector.distance - Buffers are 0 Length or sizes dont match");

    // todo - need to check that asz or bsz is evenly divisible by the data size and calc dimensions, but that's rather
    // inconvenient here. so we are duplicating the call.  Fix so only one call.

    ret = rp_vector_distance(a, b, asz, metric, datatype, &err);

    if (err)
        RP_THROW(ctx, "vector.distance - %s", err);

    duk_push_number(ctx, ret);

    return 1;
}
// end simsimd distance calcs


// js l2 normalization
#define L2NORM_F64 0
#define L2NORM_F32 1
#define L2NORM_F16 2
#define L2NORM_NUM 3
static void do_l2_norm(duk_context *ctx, duk_idx_t idx, int type)
{
    duk_size_t len = 0;
    int dim = 0;
    void *vec;
    if (type == L2NORM_NUM)
    {
        REQUIRE_ARRAY(ctx, idx, "vector.l2norm - argument must be an array of Numbers (vector)");
        numbers_to(ctx, idx, RP_F64);
        vec = duk_get_buffer_data(ctx, -1, &len);
    }
    else
        vec = REQUIRE_BUFFER_DATA(ctx, idx, &len, "vector.l2norm - argument must be a Buffer (vector)");

    switch (type)
    {
    case L2NORM_NUM:
    case L2NORM_F64: {
        double *d = (double *)vec;
        if (len % 8)
            RP_THROW(ctx, "vector.l2norm - buffer length is not a multiple of vector element size");
        dim = len / 8;
        l2_normalize_d(d, dim);
        if (type == L2NORM_NUM)
        {
            int i = 0;
            for (; i < dim; i++)
            {
                duk_push_number(ctx, d[i]);
                duk_put_prop_index(ctx, idx, (duk_uarridx_t)i);
            }
            duk_pop(ctx); // converted vec
        }
        return;
    }
    case L2NORM_F16: {
        float *tmp_f32 = NULL;
        uint16_t *f16 = (uint16_t *)vec;

        if (len % 2)
            RP_THROW(ctx, "vector.l2norm - buffer length is not a multiple of vector element size");

        dim = len / 2;
        REMALLOC(tmp_f32, dim * sizeof(float));

        f16_to_f32(f16, tmp_f32, dim);
        l2_normalize(tmp_f32, dim);
        f32_to_f16(tmp_f32, f16, dim);

        free(tmp_f32);
        return;
    }
    case L2NORM_F32: {
        if (len % 4)
            RP_THROW(ctx, "vector.l2norm - buffer length is not a multiple of vector element size");

        dim = len / 4;

        l2_normalize((float *)vec, dim);
        return;
    }
    }
}

static duk_ret_t l2norm_f64(duk_context *ctx)
{
    do_l2_norm(ctx, 0, L2NORM_F64);
    return 0;
}

static duk_ret_t l2norm_f32(duk_context *ctx)
{
    do_l2_norm(ctx, 0, L2NORM_F32);
    return 0;
}

static duk_ret_t l2norm_f16(duk_context *ctx)
{
    do_l2_norm(ctx, 0, L2NORM_F16);
    return 0;
}

static duk_ret_t l2norm_num(duk_context *ctx)
{
    do_l2_norm(ctx, 0, L2NORM_NUM);
    return 0;
}
// end js l2 normalization

// map capacity
static void veccap(duk_context *ctx)
{
    if (!rp_runtime_caps_is_init)
    {
        rp_runtime_caps = simsimd_capabilities();
        rp_runtime_caps_is_init = 1;
    }

    duk_push_object(ctx);
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_neon_k) != 0);
    duk_put_prop_string(ctx, -2, "neon");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_neon_f16_k) != 0);
    duk_put_prop_string(ctx, -2, "neonF16");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_neon_bf16_k) != 0);
    duk_put_prop_string(ctx, -2, "neonBf16");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_neon_i8_k) != 0);
    duk_put_prop_string(ctx, -2, "neonI8");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_sve_k) != 0);
    duk_put_prop_string(ctx, -2, "sve");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_sve_f16_k) != 0);
    duk_put_prop_string(ctx, -2, "sveF16");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_sve_bf16_k) != 0);
    duk_put_prop_string(ctx, -2, "sveBf16");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_sve_i8_k) != 0);
    duk_put_prop_string(ctx, -2, "sveI8");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_sve2_k) != 0);
    duk_put_prop_string(ctx, -2, "sve2");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_haswell_k) != 0);
    duk_put_prop_string(ctx, -2, "haswell");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_skylake_k) != 0);
    duk_put_prop_string(ctx, -2, "skylake");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_ice_k) != 0);
    duk_put_prop_string(ctx, -2, "ice");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_genoa_k) != 0);
    duk_put_prop_string(ctx, -2, "genoa");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_sapphire_k) != 0);
    duk_put_prop_string(ctx, -2, "sapphire");
    duk_push_boolean(ctx, (rp_runtime_caps & simsimd_cap_turin_k) != 0);
    duk_put_prop_string(ctx, -2, "turin");
    duk_put_prop_string(ctx, -2, "runtimeCapabilities");
}

// init the js functions
void duk_vector_init(duk_context *ctx)
{
    static int isinit = 0;
    if (!duk_get_global_string(ctx, "rampart"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }
    if (!duk_get_prop_string(ctx, -1, "vector"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }

    if (!isinit)
    {
        veccap(ctx);
        isinit = 1;
    }

    duk_push_c_function(ctx, rp_u8_to_num, 3);
    duk_put_prop_string(ctx, -2, "u8ToNumbers");

    duk_push_c_function(ctx, rp_num_to_u8, 3);
    duk_put_prop_string(ctx, -2, "numbersToU8");

    duk_push_c_function(ctx, rp_u8_to_f64, 3);
    duk_put_prop_string(ctx, -2, "u8ToF64");

    duk_push_c_function(ctx, rp_f64_to_u8, 3);
    duk_put_prop_string(ctx, -2, "f64ToU8");

    duk_push_c_function(ctx, rp_u8_to_f32, 3);
    duk_put_prop_string(ctx, -2, "u8ToF32");

    duk_push_c_function(ctx, rp_f32_to_u8, 3);
    duk_put_prop_string(ctx, -2, "f32ToU8");

    duk_push_c_function(ctx, rp_u8_to_f16, 3);
    duk_put_prop_string(ctx, -2, "u8ToF16");

    duk_push_c_function(ctx, rp_f16_to_u8, 3);
    duk_put_prop_string(ctx, -2, "f16ToU8");

    duk_push_c_function(ctx, rp_i8_to_num, 3);
    duk_put_prop_string(ctx, -2, "i8ToNumbers");

    duk_push_c_function(ctx, rp_num_to_i8, 3);
    duk_put_prop_string(ctx, -2, "numbersToI8");

    duk_push_c_function(ctx, rp_i8_to_f64, 3);
    duk_put_prop_string(ctx, -2, "i8ToF64");

    duk_push_c_function(ctx, rp_f64_to_i8, 3);
    duk_put_prop_string(ctx, -2, "f64ToI8");

    duk_push_c_function(ctx, rp_i8_to_f32, 3);
    duk_put_prop_string(ctx, -2, "i8ToF32");

    duk_push_c_function(ctx, rp_f32_to_i8, 3);
    duk_put_prop_string(ctx, -2, "f32ToI8");

    duk_push_c_function(ctx, rp_i8_to_f16, 3);
    duk_put_prop_string(ctx, -2, "i8ToF16");

    duk_push_c_function(ctx, rp_f16_to_i8, 3);
    duk_put_prop_string(ctx, -2, "f16ToI8");

    duk_push_c_function(ctx, rp_f32_to_f64, 1);
    duk_put_prop_string(ctx, -2, "f32ToF64");

    duk_push_c_function(ctx, rp_f64_to_f32, 1);
    duk_put_prop_string(ctx, -2, "f64ToF32");

    duk_push_c_function(ctx, rp_f64_to_f16, 1);
    duk_put_prop_string(ctx, -2, "f64ToF16");

    duk_push_c_function(ctx, rp_f16_to_f64, 1);
    duk_put_prop_string(ctx, -2, "f16ToF64");

    duk_push_c_function(ctx, rp_f64_to_bf16, 1);
    duk_put_prop_string(ctx, -2, "f64ToBf16");

    duk_push_c_function(ctx, rp_bf16_to_f64, 1);
    duk_put_prop_string(ctx, -2, "bf16ToF64");

    duk_push_c_function(ctx, rp_f32_to_f16, 1);
    duk_put_prop_string(ctx, -2, "f32ToF16");

    duk_push_c_function(ctx, rp_f16_to_f32, 1);
    duk_put_prop_string(ctx, -2, "f16ToF32");

    duk_push_c_function(ctx, rp_f32_to_bf16, 1);
    duk_put_prop_string(ctx, -2, "f32ToBf16");

    duk_push_c_function(ctx, rp_bf16_to_f32, 1);
    duk_put_prop_string(ctx, -2, "bf16ToF32");

    duk_push_c_function(ctx, num_to_f64, 1);
    duk_put_prop_string(ctx, -2, "numbersToF64");

    duk_push_c_function(ctx, num_to_f32, 1);
    duk_put_prop_string(ctx, -2, "numbersToF32");

    duk_push_c_function(ctx, num_to_f16, 1);
    duk_put_prop_string(ctx, -2, "numbersToF16");

    duk_push_c_function(ctx, num_to_bf16, 1);
    duk_put_prop_string(ctx, -2, "numbersToBf16");

    duk_push_c_function(ctx, f64_to_num, 1);
    duk_put_prop_string(ctx, -2, "f64ToNumbers");

    duk_push_c_function(ctx, f32_to_num, 1);
    duk_put_prop_string(ctx, -2, "f32ToNumbers");

    duk_push_c_function(ctx, f16_to_num, 1);
    duk_put_prop_string(ctx, -2, "f16ToNumbers");

    duk_push_c_function(ctx, bf16_to_num, 1);
    duk_put_prop_string(ctx, -2, "bf16ToNumbers");

    duk_push_c_function(ctx, l2norm_f64, 1);
    duk_put_prop_string(ctx, -2, "l2NormalizeF64");

    duk_push_c_function(ctx, l2norm_f32, 1);
    duk_put_prop_string(ctx, -2, "l2NormalizeF32");

    duk_push_c_function(ctx, l2norm_f16, 1);
    duk_put_prop_string(ctx, -2, "l2NormalizeF16");

    duk_push_c_function(ctx, l2norm_num, 1);
    duk_put_prop_string(ctx, -2, "l2NormalizeNumbers");

    duk_push_c_function(ctx, vdistance, 4);
    duk_put_prop_string(ctx, -2, "distance");

    duk_put_prop_string(ctx, -2, "vector");
    duk_put_global_string(ctx, "rampart");
}
