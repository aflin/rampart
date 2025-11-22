/* Copyright (C) 2025 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define SIMSIMD_NATIVE_F16 0
#define SIMSIMD_NATIVE_BF16 0
//#define SIMSIMD_DYNAMIC_DISPATCH 1
#include <simsimd/simsimd.h>
simsimd_capability_t rp_runtime_caps;
int rp_runtime_caps_is_init = 0;

// *************************** vector conversions *******************

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

void rpvec_bf16_to_f32(const uint16_t *src, float *dst, size_t n)
{
    size_t i = 0;
    for (; i < n; ++i)
        dst[i] = bf16_to_f32_scalar(src[i]);
}

void rpvec_f32_to_bf16(const float *src, uint16_t *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = f32_to_bf16_scalar(src[i]);
}

// -------------------- bf16 <-> f64 conversions -------------------
void rpvec_bf16_to_f64(const uint16_t *src, double *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = (double)bf16_to_f32_scalar(src[i]);
}

void rpvec_f64_to_bf16(const double *src, uint16_t *dst, size_t n)
{
    size_t i = 0;
    for (; i < n; ++i)
        dst[i] = f32_to_bf16_scalar((float)src[i]);
}


// -------------------- f16 <-> f32 conversions -------------------
void rpvec_f16_to_f32(const uint16_t *src, float *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = half_to_float(src[i]);
}

void rpvec_f32_to_f16(const float *src, uint16_t *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = float_to_half(src[i]);
}

// -------------------- f64 <-> f32 conversions -------------------
void rpvec_f64_to_f32(const double *src, float *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = (float)src[i];
}

void rpvec_f32_to_f64(const float *src, double *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = (double)src[i];
}

// -------------------- f64 <-> f16 conversions -------------------
void rpvec_f64_to_f16(const double *src, uint16_t *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = float_to_half((float)src[i]);
}


void rpvec_f16_to_f64(const uint16_t *src, double *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = (double)half_to_float(src[i]);
}

// ---------------- Quantize/Dequant: f32 <-> i8/u8 ---------------
void rpvec_f32_to_i8(const float *src, int8_t *dst, size_t n, float scale, int zp)
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

void rpvec_i8_to_f32(const int8_t *src, float *dst, size_t n, float scale, int zp)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = ((float)src[i] - (float)zp) * scale;
}

void rpvec_f32_to_u8(const float *src, uint8_t *dst, size_t n, float scale, int zp)
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

void rpvec_u8_to_f32(const uint8_t *src, float *dst, size_t n, float scale, int zp)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = ((float)src[i] - (float)zp) * scale;
}

// ---------------- Quantize/Dequant: f64 <-> i8/u8 ---------------
void rpvec_f64_to_i8(const double *src, int8_t *dst, size_t n, double scale, int zp)
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

void rpvec_i8_to_f64(const int8_t *src, double *dst, size_t n, double scale, int zp)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = ((double)src[i] - (double)zp) * scale;
}

void rpvec_f64_to_u8(const double *src, uint8_t *dst, size_t n, double scale, int zp)
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

void rpvec_u8_to_f64(const uint8_t *src, double *dst, size_t n, double scale, int zp)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = ((double)src[i] - (double)zp) * scale;
}

// ---------------- Quantize/Dequant: f16 <-> i8/u8 ---------------
/* unused currently
void rpvec_f16_to_i8(const uint16_t *src, int8_t *dst, size_t n, float scale, int zp)
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

void rpvec_i8_to_f16(const int8_t *src, uint16_t *dst, size_t n, float scale, int zp)
{
    float buf[256];
    size_t i = 0;
    while (i < n)
    {
        size_t blk = (n - i) < 256 ? (n - i) : 256;
        rpvec_i8_to_f32(src + i, buf, blk, scale, zp);
        rpvec_f32_to_f16(buf, dst + i, blk);
        i += blk;
    }
}
/* unused currently
void rpvec_f16_to_u8(const uint16_t *src, uint8_t *dst, size_t n, float scale, int zp)
{
    float buf[256];
    size_t i = 0;
    while (i < n)
    {
        size_t blk = (n - i) < 256 ? (n - i) : 256;
        rpvec_f16_to_f32(src + i, buf, blk);
        rpvec_f32_to_u8(buf, dst + i, blk, scale, zp);
        i += blk;
    }
}
*/

void rpvec_u8_to_f16(const uint8_t *src, uint16_t *dst, size_t n, float scale, int zp)
{
    float buf[256];
    size_t i = 0;
    while (i < n)
    {
        size_t blk = (n - i) < 256 ? (n - i) : 256;
        rpvec_u8_to_f32(src + i, buf, blk, scale, zp);
        rpvec_f32_to_f16(buf, dst + i, blk);
        i += blk;
    }
}

// -------------------------- b8 pack/unpack ----------------------
void rpvec_f64_to_b8_threshold(const double *src, uint8_t *dst_bits, size_t n, double thresh)
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

void rpvec_b8_to_u8_bytes(const uint8_t *src_bits, uint8_t *dst_bytes, size_t n)
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
void rpvec_i8_to_i4(const int8_t *in, uint8_t *out, size_t dim) {
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
void rpvec_i4_to_i8(const uint8_t *in, int8_t *out, size_t dim) {
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
// ********************* end vector conversions **********************

// --- Parse "metric" ---
static int parse_metric(const char *metric, simsimd_metric_kind_t *out) {
    if (!metric || !out) return 0;

    if (!strcasecmp(metric, "dot") || !strcasecmp(metric, "ip") || !strcasecmp(metric, "inner"))
        *out = simsimd_metric_dot_k;
    else if (!strcasecmp(metric, "vdot") || !strcasecmp(metric, "cdot"))
        *out = simsimd_metric_vdot_k;
    else if (!strcasecmp(metric, "cos") || !strcasecmp(metric, "cosine"))
        *out = simsimd_metric_cos_k;
    else if (!strcasecmp(metric, "l2") || !strcasecmp(metric, "l2sq") || !strcasecmp(metric, "euclidean"))
        *out = simsimd_metric_l2sq_k;
    else if (!strcasecmp(metric, "hamming"))
        *out = simsimd_metric_hamming_k;
    else if (!strcasecmp(metric, "jaccard"))
        *out = simsimd_metric_jaccard_k;
    else if (!strcasecmp(metric, "kl") || !strcasecmp(metric, "kld") || !strcasecmp(metric, "kullback_leibler"))
        *out = simsimd_metric_kl_k;
    else if (!strcasecmp(metric, "js") || !strcasecmp(metric, "jensen_shannon"))
        *out = simsimd_metric_js_k;
    else if (!strcasecmp(metric, "intersect") || !strcasecmp(metric, "overlap"))
        *out = simsimd_metric_intersect_k;
    else
        return 0;

    return 1;
}

// --- Parse "dtype" ---
static int parse_dtype(const char *dtype, simsimd_datatype_t *out) {
    if (!dtype || !out) return 0;

    // Floats
    if (!strcasecmp(dtype, "f64") || !strcasecmp(dtype, "float64") || !strcasecmp(dtype, "double"))
        *out = simsimd_datatype_f64_k;
    else if (!strcasecmp(dtype, "f32") || !strcasecmp(dtype, "float32") || !strcasecmp(dtype, "float"))
        *out = simsimd_datatype_f32_k;
    else if (!strcasecmp(dtype, "f16") || !strcasecmp(dtype, "float16") || !strcasecmp(dtype, "half"))
        *out = simsimd_datatype_f16_k;
    else if (!strcasecmp(dtype, "bf16") || !strcasecmp(dtype, "bfloat16"))
        *out = simsimd_datatype_bf16_k;

    // Integers (signed)
    else if (!strcasecmp(dtype, "i8")  || !strcasecmp(dtype, "int8"))
        *out = simsimd_datatype_i8_k;
    else if (!strcasecmp(dtype, "i16") || !strcasecmp(dtype, "int16"))
        *out = simsimd_datatype_i16_k;
    else if (!strcasecmp(dtype, "i32") || !strcasecmp(dtype, "int32"))
        *out = simsimd_datatype_i32_k;
    else if (!strcasecmp(dtype, "i64") || !strcasecmp(dtype, "int64"))
        *out = simsimd_datatype_i64_k;

    // Integers (unsigned)
    else if (!strcasecmp(dtype, "u8")  || !strcasecmp(dtype, "uint8"))
        *out = simsimd_datatype_u8_k;
    else if (!strcasecmp(dtype, "u16") || !strcasecmp(dtype, "uint16"))
        *out = simsimd_datatype_u16_k;
    else if (!strcasecmp(dtype, "u32") || !strcasecmp(dtype, "uint32"))
        *out = simsimd_datatype_u32_k;
    else if (!strcasecmp(dtype, "u64") || !strcasecmp(dtype, "uint64"))
        *out = simsimd_datatype_u64_k;

    // Sub-byte
    else if (!strcasecmp(dtype, "b8") || !strcasecmp(dtype, "b1x8") ||
             !strcasecmp(dtype, "binary") || !strcasecmp(dtype, "bit") || !strcasecmp(dtype, "bits"))
        *out = simsimd_datatype_b8_k;     // 1-bit values packed in bytes
    else if (!strcasecmp(dtype, "i4x2") || !strcasecmp(dtype, "int4x2") ||
             !strcasecmp(dtype, "i4") || !strcasecmp(dtype, "int4"))
        *out = simsimd_datatype_i4x2_k;   // 2 signed 4-bit packed per byte

    // Complex (interleaved real,imag)
    else if (!strcasecmp(dtype, "f64c") || !strcasecmp(dtype, "complex64") || !strcasecmp(dtype, "c64"))
        *out = simsimd_datatype_f64c_k;
    else if (!strcasecmp(dtype, "f32c") || !strcasecmp(dtype, "complex32") || !strcasecmp(dtype, "c32"))
        *out = simsimd_datatype_f32c_k;
    else if (!strcasecmp(dtype, "f16c") || !strcasecmp(dtype, "complex16") || !strcasecmp(dtype, "c16"))
        *out = simsimd_datatype_f16c_k;
    else if (!strcasecmp(dtype, "bf16c") || !strcasecmp(dtype, "bcomplex16") || !strcasecmp(dtype, "bc16"))
        *out = simsimd_datatype_bf16c_k;

    else
        return 0;

    return 1;
}

static size_t simsimd_scalar_word_size(simsimd_datatype_t t) {
    switch (t) {
        // sub-byte packed → word is one byte
        case simsimd_datatype_b8_k:
        case simsimd_datatype_i4x2_k:
            return 1;

        // integers
        case simsimd_datatype_i8_k:
        case simsimd_datatype_u8_k:
            return 1;
        case simsimd_datatype_i16_k:
        case simsimd_datatype_u16_k:
        case simsimd_datatype_f16_k:
        case simsimd_datatype_bf16_k:
            return 2;
        case simsimd_datatype_i32_k:
        case simsimd_datatype_u32_k:
        case simsimd_datatype_f32_k:
            return 4;
        case simsimd_datatype_i64_k:
        case simsimd_datatype_u64_k:
        case simsimd_datatype_f64_k:
            return 8;

        // complex → scalar word is underlying REAL size
        case simsimd_datatype_f16c_k:  return 2; // real f16
        case simsimd_datatype_bf16c_k: return 2; // real bf16
        case simsimd_datatype_f32c_k:  return 4; // real f32
        case simsimd_datatype_f64c_k:  return 8; // real f64

        default:
            return 0; // unknown
    }
}



/*
 * Public: compute_distance
 * Returns 0 on success, non-zero on error.
 *
 * mk:      simsimd_metric_kind_t
 * dt:      simsimd_datatype_t
 * dim:     number of elements
 * a, b:    pointers to the vector data of the given dtype
 * out:     pointer to the calculated distance
 */
static int compute_distance(simsimd_metric_kind_t mk,
                  simsimd_datatype_t dt,
                  size_t dim,
                  const void *a,
                  const void *b,
                  simsimd_distance_t *out)
{
    if (!a || !b || !out) return 1;

    // Pick the best kernel for (metric, dtype, caps)
    simsimd_kernel_punned_t punned = simsimd_metric_punned(mk, dt, rp_runtime_caps);
    if (!punned) return 4; // no kernel available for this combo on this platform

    typedef void (*fn_t)(void const *, void const *, simsimd_size_t, void *);

    // Dispatch by dtype with a generic-typed function pointer.
    switch (dt) {
        case simsimd_datatype_f32_k:
        case simsimd_datatype_f64_k:
        case simsimd_datatype_f16_k:
        case simsimd_datatype_bf16_k:
        case simsimd_datatype_i8_k:
        case simsimd_datatype_u8_k:
        case simsimd_datatype_b8_k:
        case simsimd_datatype_f32c_k:
        case simsimd_datatype_f64c_k:
        {
            fn_t fn = (fn_t)punned;
            fn(a, b, dim, out);
            return 0;
        }
        default:
            return 5; // dtype not supported here
    }
}

double rp_vector_distance(void *a, void *b, size_t bytesize, const char *metric, const char *datatype, const char **err)
{
    simsimd_distance_t distance;
    size_t dim=0, dsize;
    static int isinit=0;

    if(!rp_runtime_caps_is_init)
    {
        rp_runtime_caps = simsimd_capabilities();
        rp_runtime_caps_is_init=1;
    }

    if(!bytesize)
    {
        *err="Invalid buffer length";
        return NAN;
    }

    simsimd_datatype_t dt;
    if (!parse_dtype(datatype, &dt))
    {
        *err="Unknown dataType";
        return NAN;
    }

    dsize = simsimd_scalar_word_size(dt);
    if( bytesize % dsize)
    {
        *err="buffer length must be a multiple of datatype size";
        return NAN;
    }

    dim = bytesize/dsize;

    simsimd_metric_kind_t mk;
    if (!parse_metric(metric, &mk))
    {
        *err="Unknown Metric";
        return NAN;
    }

    int res = compute_distance(mk, dt, dim, a, b, &distance);

    switch(res)
    {
        case 0:
            *err=NULL;
            return (double)distance;
        case 1:
            *err="Invalid Parameter";
            return NAN;
        /*
        case 2:
            *err="Unknown Metric";
            return NAN;
        case 3:
            *err="Unknown DataType";
            return NAN;
        */
        case 4:
            *err="Metric/Datatype kernel unavailable";
            return NAN;
        case 5:
            *err="Unsupported DataType";
            return NAN;
    }
    return NAN;
}
