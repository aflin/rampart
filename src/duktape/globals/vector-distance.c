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
