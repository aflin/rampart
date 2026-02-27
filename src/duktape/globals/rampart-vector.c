/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
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

// vector conversions are now in vector-distance

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
    if(scale < 0.0)
    {
        for(size_t i=0; i<dim; i++)
        {
            double d = in[i];
            if(d>max)
                max=d;
            if(d<min)
                min=d;
        }
        scale = (max-min)/255.0;
    }

    rpvec_f64_to_u8(in, out, dim, scale, zp);

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
    rpvec_u8_to_f64(in, out, dim, scale, zp);
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
    if(scale < 0.0)
    {
        for(size_t i=0; i<dim; i++)
        {
            float d = in[i];
            if(d>max)
                max=d;
            if(d<min)
                min=d;
        }
        scale = (max-min)/255.0;
    }

    rpvec_f32_to_u8(in, out, dim, scale, zp);

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
    rpvec_u8_to_f32(in, out, dim, scale, zp);
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
    rpvec_f16_to_f32(in, in32, dim);

    float min=FLT_MAX, max=-FLT_MAX;
    if(scale < 0.0)
    {
        for(size_t i=0; i<dim; i++)
        {
            float d = in32[i];
            if(d>max)
                max=d;
            if(d<min)
                min=d;
        }
        scale = (max-min)/255.0;
    }

    rpvec_f32_to_u8(in32, out, dim, scale, zp);

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
    rpvec_u8_to_f16(in, out, dim, scale, zp);
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
        zp = REQUIRE_INT(ctx, 2, "vector.f64ToI8 - third argument, if present must be a Int -127 - 127 (zero point)");
        if(zp < -127 || zp > 127)
            RP_THROW(ctx, "vector.f64ToI8 - third argument, if present must be a Int -127 - 127 (zero point)");
    }

    int8_t *out = (int8_t *)duk_push_fixed_buffer(ctx, dim);

    if(scale < 0.0)
    {
        double absmax = 0.0;
        for(size_t i=0; i<dim; i++)
        {
            double abs_val = fabs(in[i]);
            if (abs_val > absmax)
                absmax = abs_val;
        }
        scale = absmax / 127.0;
    }

    rpvec_f64_to_i8(in, out, dim, scale, zp);
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
    rpvec_i8_to_f64(in, out, dim, scale, zp);
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
        zp = REQUIRE_INT(ctx, 2, "vector.f32ToI8 - third argument, if present must be a Int -127 - 127 (zero point)");
        if(zp < -127 || zp > 127)
            RP_THROW(ctx, "vector.f32ToI8 - third argument, if present must be a Int -127 - 127 (zero point)");
    }

    int8_t *out = (int8_t *)duk_push_fixed_buffer(ctx, dim);

    if(scale < 0.0)
    {
        float absmax = 0.0;
        for(size_t i=0; i<dim; i++)
        {
            float abs_val = fabs(in[i]);
            if (abs_val > absmax)
                absmax = abs_val;
        }
        scale = absmax / 127.0;
    }

    rpvec_f32_to_i8(in, out, dim, scale, zp);

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
    rpvec_i8_to_f32(in, out, dim, scale, zp);
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
        zp = REQUIRE_INT(ctx, 2, "vector.f16ToI8 - third argument, if present must be a Int -127 - 127 (zero point)");
        if(zp < -127 || zp > 127)
            RP_THROW(ctx, "vector.f16ToI8 - third argument, if present must be a Int -127 - 127 (zero point)");
    }

    int8_t *out = (int8_t *)duk_push_fixed_buffer(ctx, dim);
    float *in32 = NULL;

    REMALLOC(in32, dim*sizeof(float));
    rpvec_f16_to_f32(in, in32, dim);

    if(scale < 0.0)
    {
        float absmax = 0.0;
        for(size_t i=0; i<dim; i++)
        {
            float abs_val = fabs(in32[i]);
            if (abs_val > absmax)
                absmax = abs_val;
        }
        scale = absmax / 127.0;
    }

    rpvec_f32_to_i8(in32, out, dim, scale, zp);

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
    rpvec_i8_to_f16(in, out, dim, scale, zp);
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

    rpvec_i8_to_i4(in, out, dim);
    return 1;
}

static duk_ret_t rp_i4_to_i8(duk_context *ctx)
{
    duk_size_t dim=0, sz4=0;
    int8_t *out;
    uint8_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz4, "vector.i4Toi8 - argument must be a Buffer");
    dim = sz4 * 2;

    // bug fix: use dim instead of sz4 to prevent heap buffer overflow - 2026-02-27
    out = duk_push_fixed_buffer(ctx, dim);
    rpvec_i4_to_i8(in, out, dim);
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

    rpvec_f16_to_f32(in, out, dim);

    return 1;
}

static duk_ret_t rp_f32_to_f16(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    float *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f32ToF16 - argument must be a Buffer");
    dim = sz / sizeof(float); // f32 - four bytes

    uint16_t *out = duk_push_fixed_buffer(ctx, dim * sizeof(uint16_t));

    rpvec_f32_to_f16(in, out, dim);

    return 1;
}

static duk_ret_t rp_bf16_to_f32(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    uint16_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.bf16ToF32 - argument must be a Buffer");
    dim = sz / sizeof(uint16_t); // f16 - two bytes

    float *out = duk_push_fixed_buffer(ctx, dim * sizeof(float));

    rpvec_bf16_to_f32(in, out, dim);

    return 1;
}

static duk_ret_t rp_f32_to_bf16(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    float *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f32ToBf16 - argument must be a Buffer");
    dim = sz / sizeof(float); // f32 - four bytes

    uint16_t *out = duk_push_fixed_buffer(ctx, dim * sizeof(uint16_t));

    rpvec_f32_to_bf16(in, out, dim);

    return 1;
}

static duk_ret_t rp_f64_to_f32(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    double *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f64ToF32 - argument must be a Buffer");
    dim = sz / sizeof(double);

    float *out = duk_push_fixed_buffer(ctx, dim * sizeof(float));

    rpvec_f64_to_f32(in, out, dim);

    return 1;
}

static duk_ret_t rp_f32_to_f64(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    float *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f32ToF64 - argument must be a Buffer");
    dim = sz / sizeof(float); // f32 - four bytes

    double *out = duk_push_fixed_buffer(ctx, dim * sizeof(double));

    rpvec_f32_to_f64(in, out, dim);

    return 1;
}

static duk_ret_t rp_f64_to_f16(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    double *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f64ToF16 - argument must be a Buffer");
    dim = sz / sizeof(double);

    uint16_t *out = duk_push_fixed_buffer(ctx, dim * sizeof(uint16_t));

    rpvec_f64_to_f16(in, out, dim);

    return 1;
}

static duk_ret_t rp_f16_to_f64(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    uint16_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f16ToF64 - argument must be a Buffer");
    dim = sz / sizeof(uint16_t); // f16 - tw0 bytes

    double *out = duk_push_fixed_buffer(ctx, dim * sizeof(double));

    rpvec_f16_to_f64(in, out, dim);

    return 1;
}

static duk_ret_t rp_f64_to_bf16(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    double *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.f64ToBf16 - argument must be a Buffer");
    dim = sz / sizeof(double);

    uint16_t *out = duk_push_fixed_buffer(ctx, dim * sizeof(uint16_t));

    rpvec_f64_to_bf16(in, out, dim);

    return 1;
}

static duk_ret_t rp_bf16_to_f64(duk_context *ctx)
{
    duk_size_t sz, dim = 0;
    uint16_t *in = REQUIRE_BUFFER_DATA(ctx, 0, &sz, "vector.bf16ToF64 - argument must be a Buffer");
    dim = sz / sizeof(uint16_t); // f16 - tw0 bytes

    double *out = duk_push_fixed_buffer(ctx, dim * sizeof(double));

    rpvec_bf16_to_f64(in, out, dim);

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
            out64[i] = REQUIRE_NUMBER(ctx, -1, "numbersToF64 - array[%lu] is not a Number", i);
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
            out64[i] = REQUIRE_NUMBER(ctx, -1, "numbersToBf16 - array[%lu] is not a Number", i);
            duk_pop(ctx);
        }

        rpvec_f64_to_bf16(out64, outbf16, len);

        free(out64);
        return 1;
    }

    float *out = duk_push_fixed_buffer(ctx, len * sizeof(float));

    for (; i < len; i++)
    {
        duk_get_prop_index(ctx, arridx, (duk_uarridx_t)i);
        out[i] = (float)REQUIRE_NUMBER(ctx, -1, "numbersTo - array[%lu] is not a Number", i);
        duk_pop(ctx);
    }

    if (type == RP_F16)
    {
        uint16_t *out16 = duk_push_fixed_buffer(ctx, len * sizeof(uint16_t));
        rpvec_f32_to_f16(out, out16, len);
        duk_remove(ctx, -2); // the f32 *out
    }

    return 1;
}

static duk_ret_t num_to_f64(duk_context *ctx)
{
    REQUIRE_ARRAY(ctx, 0, "numbersToF64 - argument must be an Array of Numbers");
    return numbers_to(ctx, 0, RP_F64);
}

static duk_ret_t num_to_f32(duk_context *ctx)
{
    REQUIRE_ARRAY(ctx, 0, "numbersToF32 - argument must be an Array of Numbers");
    return numbers_to(ctx, 0, RP_F32);
}

static duk_ret_t num_to_f16(duk_context *ctx)
{
    REQUIRE_ARRAY(ctx, 0, "numbersToF16 - argument must be an Array of Numbers");
    return numbers_to(ctx, 0, RP_F16);
}

static duk_ret_t num_to_bf16(duk_context *ctx)
{
    REQUIRE_ARRAY(ctx, 0, "numbersToBf16 - argument must be an Array of Numbers");
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
        rpvec_bf16_to_f64(bf16, f64, dim);

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
        rpvec_f16_to_f32((uint16_t *)buf, f32, dim);
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

// simsimd distance calcs for raw
static duk_ret_t vdistance(duk_context *ctx)
{
    // defaults:
    const char *metric = "dot";
    const char *datatype = "f16";
    const char *err = NULL;
    duk_size_t asz, bsz;
    void *a, *b;

    if (!duk_is_undefined(ctx, 3))
        datatype = REQUIRE_STRING(ctx, 3, "raw.vector.distance() - Fourth argument, if present, must be a datatype (default: 'f16')");

    if (strcasecmp("number", datatype) == 0 || strcasecmp("numbers", datatype) == 0)
    {
        REQUIRE_ARRAY(ctx, 0, "raw.vector.distance() - First argument must be an Array of Numbers (vector)");
        REQUIRE_ARRAY(ctx, 1, "raw.vector.distance() - Second argument must be an Array of Numbers (vector)");

        numbers_to(ctx, 0, RP_F64);
        duk_replace(ctx, 0);

        numbers_to(ctx, 1, RP_F64);
        duk_replace(ctx, 1);

        datatype = "f64";
    }
    a = REQUIRE_BUFFER_DATA(ctx, 0, &asz, "raw.vector.distance() - First argument must be a Buffer (vector)");
    b = REQUIRE_BUFFER_DATA(ctx, 1, &bsz, "raw.vector.distance() - Second argument must be a Buffer (vector)");
    double ret;

    if (!duk_is_undefined(ctx, 2))
        metric = REQUIRE_STRING(ctx, 2, "raw.vector.distance() - Third argument, if present, must be a metric (default: 'dot')");

    if (!asz || asz != bsz)
        RP_THROW(ctx, "raw.vector.distance() - Buffers are 0 Length or sizes do not match");

    // todo - need to check that asz or bsz is evenly divisible by the data size and calc dimensions, but that's rather
    // inconvenient here. so we are duplicating the call.  Fix so only one call.

    ret = rp_vector_distance(a, b, asz, metric, datatype, &err);

    if (err)
        RP_THROW(ctx, "raw.vector.distance() - %s", err);

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

    idx=duk_normalize_index(ctx, idx);

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

        rpvec_f16_to_f32(f16, tmp_f32, dim);
        l2_normalize(tmp_f32, dim);
        rpvec_f32_to_f16(tmp_f32, f16, dim);

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

typedef duk_ret_t (*rp_conversion_func)(duk_context *ctx);

static const rp_conversion_func rp_conversions[7][7] = {
/*             unk,    f64,              f32,              f16,             bf16,             i8,              u8          */
/* unk  */   { NULL,   NULL,             NULL,             NULL,            NULL,             NULL,            NULL         },
/* f64  */   { NULL,   NULL,             rp_f64_to_f32,    rp_f64_to_f16,   rp_f64_to_bf16,   rp_f64_to_i8,    rp_f64_to_u8 },
/* f32  */   { NULL,   rp_f32_to_f64,    NULL,             rp_f32_to_f16,   rp_f32_to_bf16,   rp_f32_to_i8,    rp_f32_to_u8 },
/* f16  */   { NULL,   rp_f16_to_f64,    rp_f16_to_f32,    NULL,            NULL,             rp_f16_to_i8,    rp_f16_to_u8 },
/* bf32 */   { NULL,   rp_bf16_to_f64,   rp_bf16_to_f32,   NULL,            NULL,             NULL,            NULL         },
/* i8   */   { NULL,   rp_i8_to_f64,     rp_i8_to_f32,     rp_i8_to_f16,    NULL,             NULL,            NULL         },
/* u8   */   { NULL,   rp_u8_to_f64,     rp_u8_to_f32,     rp_u8_to_f16,    NULL,             NULL,            NULL         }
};

static const size_t rp_elsz[7] = {1,8,4,2,2,1,1};

static void push_vec_methods(duk_context *ctx, rp_vec_type type);
static duk_ret_t new_vector(duk_context *ctx);

static duk_ret_t v2_(duk_context *ctx, rp_vec_type totype)
{
    rp_vec_type type = rp_vec_unknown;
    int dim=0;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("vectype"));
    type = duk_get_int(ctx, -1);
    duk_pop(ctx);

    if(type == totype)
        return 1; //no conversion, return self

    duk_get_prop_string(ctx, -1, "dim");
    dim = duk_get_int(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("rpvec"));
    duk_insert(ctx, 0);  //raw vec to idx 0 for conversion funcs

    duk_push_object(ctx); //return vec obj

    rp_conversion_func cfunc = rp_conversions[type][totype];
    if(cfunc)
        cfunc(ctx);
    else
        RP_THROW(ctx, "vector conversion not supported");

    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("rpvec"));

    duk_push_int(ctx, (int) totype);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("vectype"));

    duk_push_int(ctx, dim);
    duk_rp_put_prop_string_ro(ctx, -2, "dim");

    switch(totype)
    {
        case rp_vec_f64:  duk_push_string(ctx, "f64");  break;
        case rp_vec_f32:  duk_push_string(ctx, "f32");  break;
        case rp_vec_f16:  duk_push_string(ctx, "f16");  break;
        case rp_vec_bf16: duk_push_string(ctx, "bf16"); break;
        case rp_vec_i8:   duk_push_string(ctx, "i8");   break;
        default:          duk_push_string(ctx, "u8");   break;
    }
    duk_rp_put_prop_string_ro(ctx, -2, "type");

    push_vec_methods(ctx, totype);

    return 1;
}
static duk_ret_t v2f64(duk_context *ctx)
{
    return v2_(ctx, rp_vec_f64);
}

static duk_ret_t v2f32(duk_context *ctx)
{
    return v2_(ctx, rp_vec_f32);
}


static duk_ret_t v2f16(duk_context *ctx)
{
    return v2_(ctx, rp_vec_f16);
}


static duk_ret_t v2bf16(duk_context *ctx)
{
    return v2_(ctx, rp_vec_bf16);
}

static duk_ret_t v2i8(duk_context *ctx)
{
    return v2_(ctx, rp_vec_i8);
}

static duk_ret_t v2u8(duk_context *ctx)
{
    return v2_(ctx, rp_vec_u8);
}

static duk_ret_t v2num(duk_context *ctx)
{
    rp_vec_type type = rp_vec_unknown;
    int dim=0;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("vectype"));
    type = duk_get_int(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("rpvec"));
    duk_insert(ctx, 0); // the raw buffer to idx 0

    switch(type)
    {
        case rp_vec_f64:  f64_to_num(ctx);   break;
        case rp_vec_f32:  f32_to_num(ctx);   break;
        case rp_vec_f16:  f16_to_num(ctx);   break;
        case rp_vec_bf16: bf16_to_num(ctx);  break;
        case rp_vec_i8:   rp_i8_to_num(ctx); break;
        case rp_vec_u8:   rp_u8_to_num(ctx); break;
        default:     break; //won't happen, silence warnings
    }
    return 1;
}

static duk_ret_t v2l2(duk_context *ctx)
{
    rp_vec_type type = rp_vec_unknown;
    int dim=0;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("vectype"));
    type = duk_get_int(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("rpvec"));

    switch(type)
    {
        case rp_vec_f64:  do_l2_norm(ctx, -1, L2NORM_F64); break;
        case rp_vec_f32:  do_l2_norm(ctx, -1, L2NORM_F32); break;
        case rp_vec_f16:  do_l2_norm(ctx, -1, L2NORM_F16); break;
        default:     break; //won't happen, silence warnings
    }
    duk_pop(ctx);// buffer, return this
    return 1;
}

static duk_ret_t v2vlen(duk_context *ctx)
{
    size_t blen;
    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("rpvec"));
    (void)duk_get_buffer_data(ctx, -1, &blen);
    duk_push_int(ctx, (int)blen);
    return 1;
}

static duk_ret_t v2resz(duk_context *ctx)
{
    size_t blen;
    duk_size_t elsz=1, olddim, newdim = (duk_size_t) REQUIRE_POSINT(ctx, 0, "vector.resize() - argument must be a positive integer (new size dim)");
    void *v, *newv;
    rp_vec_type type;

    if(!newdim)
    {
        duk_push_fixed_buffer(ctx, 0);
        return 1;
    }

    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("rpvec"));
    v = duk_get_buffer_data(ctx, -1, &blen);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("vectype"));
    type = duk_get_int(ctx, -1);
    duk_pop(ctx);

    elsz = rp_elsz[type];

    olddim = blen / elsz;

    //set up call to new_vector through duk_call()
    duk_push_c_function(ctx, new_vector, 2);
    duk_get_prop_string(ctx, -2, "type"); // from 'this'

    newv = duk_push_fixed_buffer(ctx, newdim * elsz);
    blen = (size_t) ( olddim < newdim ? olddim*elsz : newdim*elsz );
    memcpy(newv, v, blen); // copy lesser of old and new

    duk_new(ctx, 2); // new rampart.vector(type, newv_buffer)

    return 1;
}

static duk_ret_t v2copy(duk_context *ctx)
{
    size_t blen;
    duk_size_t dim;
    void *v, *newv;
    rp_vec_type type;

    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("rpvec"));
    v = duk_get_buffer_data(ctx, -1, &blen);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("vectype"));
    type = duk_get_int(ctx, -1);
    duk_pop(ctx);

    dim = blen / rp_elsz[type];

    newv = duk_push_fixed_buffer(ctx, blen);
    memcpy(newv, v, blen);

    rp_push_new_vector(ctx, type, dim, -1);

    return 1;
}

static duk_ret_t v2raw(duk_context *ctx)
{
    size_t blen;
    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("rpvec"));
    return 1;
}

// simsimd distance calcs
static duk_ret_t v2dist(duk_context *ctx)
{
    // defaults:
    const char *metric = "dot";
    const char *datatype = "f16";
    const char *err = NULL;
    duk_size_t asz, bsz;
    rp_vec_type atype = rp_vec_unknown, btype = rp_vec_unknown;
    void *a, *b;

    /* the comparison vec at idx=0 */
    if(!duk_is_object(ctx, 0) || !duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("rpvec")))
        RP_THROW(ctx, "vector.distance() - First argument must be a comparison vector of the same type/dim");

    b = REQUIRE_BUFFER_DATA(ctx, -1, &bsz, "vector.distance() - First argument must be a comparison vector of the same type/dim");
    duk_pop(ctx);

    if(!duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("vectype")))
        RP_THROW(ctx, "vector.distance() - First argument must be a comparison vector of the same type/dim");

    btype = duk_get_int(ctx, -1);
    duk_pop(ctx);


    /* current vec at this */
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("rpvec"));
    a=duk_get_buffer_data(ctx, -1, &asz);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("vectype"));
    atype = duk_get_int(ctx, -1);
    duk_pop_2(ctx);

    if(atype != btype)
        RP_THROW(ctx, "vector.distance() - vectors must be the same type, convert one first");

    if (!duk_is_undefined(ctx, 1))
        metric = REQUIRE_STRING(ctx, 1, "vector.distance() - Second argument, if present, must be a metric (default: 'dot')");

    double ret;

    if (!asz || asz != bsz)
        RP_THROW(ctx, "vector.distance() - Vectors are 0 Length or sizes do not match");

    switch(btype)
    {
        case rp_vec_f64: datatype="f64";   break;
        case rp_vec_f32: datatype="f32";   break;
        case rp_vec_bf16: datatype="bf16"; break;
        case rp_vec_i8: datatype="i8";     break;
        case rp_vec_u8: datatype="u8";     break;
        default:     break; //won't happen, silence warnings
    }

    ret = rp_vector_distance(a, b, asz, metric, datatype, &err);

    if (err)
        RP_THROW(ctx, "vector.distance() - %s", err);

    duk_push_number(ctx, ret);

    return 1;
}


static void push_vec_methods(duk_context *ctx, rp_vec_type type)
{

    // every type supported
    duk_push_c_function(ctx, v2f64, 2);
    duk_put_prop_string(ctx, -2, "toF64");

    // every type supported
    duk_push_c_function(ctx, v2f32, 2);
    duk_put_prop_string(ctx, -2, "toF32");

    /* for every type except bf16 */
    if( type != rp_vec_bf16)
    {
        duk_push_c_function(ctx, v2f16, 2);
        duk_put_prop_string(ctx, -2, "toF16");
    }

    /* only f64 or f32 to bf16 */
    if( type == rp_vec_f64 || type == rp_vec_f32)
    {
        duk_push_c_function(ctx, v2bf16, 2);
        duk_put_prop_string(ctx, -2, "toBf16");
    }

    /* only f64, f32, f16 to u8 */
    if( type != rp_vec_u8 && type != rp_vec_bf16)
    {
        duk_push_c_function(ctx, v2i8, 2);
        duk_put_prop_string(ctx, -2, "toI8");
    }

    /* only f64, f32, f16 to i8 */
    if( type != rp_vec_i8 && type != rp_vec_bf16)
    {
        duk_push_c_function(ctx, v2u8, 2);
        duk_put_prop_string(ctx, -2, "toU8");
    }

    // make array of numbers
    duk_push_c_function(ctx, v2num, 2);
    duk_put_prop_string(ctx, -2, "toNumbers");

    // normalize for f64, f32 and f16 only
    if(type == rp_vec_f64 || type == rp_vec_f32 || type == rp_vec_f16)
    {
        duk_push_c_function(ctx, v2l2, 0);
        duk_put_prop_string(ctx, -2, "l2Normalize");
    }

    // return raw buffer
    duk_push_c_function(ctx, v2raw, 0);
    duk_put_prop_string(ctx, -2, "toRaw");

    // length in bytes
    duk_push_c_function(ctx, v2vlen, 0);
    duk_put_prop_string(ctx, -2, "byteLength");

    // copy to vector of new length.
    duk_push_c_function(ctx, v2resz, 1);
    duk_put_prop_string(ctx, -2, "resize");

    // copy to vector of same length.
    duk_push_c_function(ctx, v2copy, 1);
    duk_put_prop_string(ctx, -2, "copy");

    // calc distance (vec, metric)
    duk_push_c_function(ctx, v2dist, 2);
    duk_put_prop_string(ctx, -2, "distance");

}

void rp_push_new_vector(duk_context *ctx, rp_vec_type type, size_t dim, duk_idx_t idx)
{
    const char *stype = "f16";

    switch(type)
    {
        case rp_vec_f64:  stype="f64";  break;
        case rp_vec_f32:  stype="f32";  break;
        case rp_vec_bf16: stype="bf16"; break;
        case rp_vec_i8:   stype="i8";   break;
        case rp_vec_u8:   stype="u8";   break;
        default:     break; //won't happen, silence warnings
    }

    idx = duk_normalize_index(ctx, idx);

    duk_push_object(ctx); // the return object

    duk_pull(ctx, idx); //the buffer

    /* attach buffer to object as hidden prop */
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("rpvec"));

    /* save our vector type as hidden prop */
    duk_push_int(ctx, (int)type);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("vectype"));

    /* user read only string description */
    duk_push_string(ctx, stype);
    duk_rp_put_prop_string_ro(ctx, -2, "type");

    /* user read only num dim */
    duk_push_int(ctx, (int)dim);
    duk_rp_put_prop_string_ro(ctx, -2, "dim");

    /* add all the methods */
    push_vec_methods(ctx, type);

}

/* new rampart.vector(type, dim|buffer|array[, scale, zp]]) */
static duk_ret_t new_vector(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx))
    {
        RP_THROW(ctx, "rampart.vector():  Must be called with 'new rampart.vector()");
    }

    size_t elsz=0, dim=0;
    rp_vec_type type=rp_vec_unknown;
    const char *itype = REQUIRE_STRING(ctx, 0, "new rampart.vector() - first argument must be a String (vec type)");

    if( ! strcasecmp(itype, "f64") )
        type  = rp_vec_f64;
    else if( ! strcasecmp(itype, "f32") )
        type  = rp_vec_f32;
    else if( ! strcasecmp(itype, "f16") )
        type  = rp_vec_f16;
    else if( ! strcasecmp(itype, "bf16") )
        type  = rp_vec_bf16;
    else if( ! strcasecmp(itype, "i8") )
        type  = rp_vec_i8;
    else if( ! strcasecmp(itype, "u8") )
        type  = rp_vec_u8;
    else
        RP_THROW(ctx, "new rampart.vector() - invalid type '%s'", itype);

    elsz = rp_elsz[type];

    /* push the appropriate buffer to stack */
    if(duk_is_number(ctx, 1))
    {
        dim = REQUIRE_INT(ctx, 1, "new rampart.vector() - second argument must be an integer (vec dim)");
        duk_push_fixed_buffer(ctx, dim*elsz);
        //buffer is at -1
    }
    else if(duk_is_array(ctx, 1))
    {
        // use our raw functions, which requre stack [array[, scale, zp]]
        dim = duk_get_length(ctx, 1);
        // remove type;
        duk_remove(ctx, 0); //type
        switch(type)
        {
            case rp_vec_f64:  num_to_f64(ctx);break;
            case rp_vec_f32:  num_to_f32(ctx);break;
            case rp_vec_f16:  num_to_f16(ctx);break;
            case rp_vec_bf16: num_to_bf16(ctx);break;
            case rp_vec_i8:   rp_num_to_i8(ctx);break;
            case rp_vec_u8:   rp_num_to_u8(ctx);break;
            default:     break; //won't happen, silence warnings
        }

        //buffer is at -1
    }
    else if(duk_is_buffer_data(ctx, 1))
    {
        duk_size_t sz;
        (void)REQUIRE_BUFFER_DATA(ctx, 1,  &sz, "new rampart.vector() - second argument, if present, must be a buffer (vector)");

        if(sz % elsz)
            RP_THROW(ctx,  "new rampart.vector(%s, %lu, buf) - buf length(%lu) is not a multiple of vec type size(%lu)",
                itype, dim, sz, elsz);
        dim = sz/elsz;
        duk_pull(ctx, 1);
        //buffer is at -1
    }
    else
         RP_THROW(ctx,  "new rampart.vector() - second argument must be a Number (zeroed vector dims) / Buffer (raw vector) / Array of Numbers");

    rp_push_new_vector(ctx, type, dim, -1);

    return 1;
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
        // rampart.vector() - typed vectors
        duk_push_c_function(ctx, new_vector, 4);
    }

    if (!isinit)
    {
        veccap(ctx);
        isinit = 1;
    }

    duk_push_object(ctx); //raw

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

    //rampart.vector.raw - for buffer conversions
    duk_put_prop_string(ctx, -2, "raw");

    duk_put_prop_string(ctx, -2, "vector");
    duk_put_global_string(ctx, "rampart");
}
