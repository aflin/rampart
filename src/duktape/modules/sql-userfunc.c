#ifdef INCLUDE_ADDUSERFUNCS

/* sql_userfunc_vec16.c
 *
 * Texis-style user functions with foaddfuncs():
 *   int func(FLD *f)  // f[0] = return FLD, f[1..n] = args
 *
 * Functions:
 *   vrank16( BYTE vec16, BYTE qvec16 )  -> returns DOUBLE in f[0]
 *
 * Assumptions:
 *   - Inputs should be unit-normalized for cosine via dot.
 */

#include "texint.h"   /* must define FLD, FLDFUNC, FLDOP, getfld(), putfld(), foaddfuncs(), etc. */
#include "fld.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define F(f)    ((int (*)(void))(f))

/* ----------------------------- Config ---------------------------- */

// ---- x86 / AVX family ----
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))

  // Only pull AVX intrinsics if the compiler is actually building with AVX enabled
  // (e.g., -mavx / -mavx2 on GCC/Clang, or /arch:AVX2 on MSVC).
  #if defined(__AVX__) || defined(__AVX2__) || defined(__AVX512F)

    // Clang/GCC/MSVC all provide <immintrin.h> for x86 intrinsics
    #if defined(__has_include)
      #if __has_include(<immintrin.h>)
        #include <immintrin.h>
      #else
        #error "AVX requested but <immintrin.h> not found."
      #endif
    #else
      #include <immintrin.h>
    #endif

  #endif // AVX enabled

  // If you only need SSE:
  // #if defined(__SSE2__)
  //   #include <emmintrin.h>
  // #endif

// ---- ARM / NEON (Apple Silicon, Android, etc.) ----
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)

  #include <arm_neon.h>

#endif



// ----------------------------- Config -----------------------------

// Choose and pin a user type id (32..63 are typically free for user types).
// Make sure this ID does not collide with other custom types in your system.
static int TID_VECTOR16_768 = 42; // <-- set at runtime by dbaddtype(); keep a global copy

// ------------------------ Utility: half<->float -------------------
// We provide 3 paths: x86 F16C, AArch64 NEON, and a portable scalar fallback.

// Portable scalar fallback: 16-bit half -> float32
static inline float f16_to_f32_scalar(uint16_t h) {
    uint32_t sign = (uint32_t)(h >> 15);
    uint32_t exp  = (uint32_t)((h >> 10) & 0x1F);
    uint32_t mant = (uint32_t)(h & 0x3FF);
    uint32_t f;

    if (exp == 0) {
        if (mant == 0) {
            f = sign << 31;  // +/- 0
        } else {
            // subnormal -> normalize
            while ((mant & 0x400) == 0) { mant <<= 1; exp--; }
            mant &= 0x3FF;
            exp = exp + (127 - 15);
            f = (sign << 31) | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        // Inf/NaN
        f = (sign << 31) | (0xFF << 23) | (mant << 13);
    } else {
        // normalized
        exp = exp + (127 - 15);
        f = (sign << 31) | (exp << 23) | (mant << 13);
    }
    float out;
    memcpy(&out, &f, sizeof(out));
    return out;
}
/*
// Portable scalar fallback: float32 -> 16-bit half (round-to-nearest-even)
static inline uint16_t f32_to_f16_scalar(float x) {
    uint32_t f;
    memcpy(&f, &x, sizeof(f));
    uint32_t sign = (f >> 31) & 0x1;
    int32_t  exp  = (int32_t)((f >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = f & 0x7FFFFF;

    uint16_t h;
    if (exp <= 0) {
        if (exp < -10) {
            // too small -> +/-0
            h = (uint16_t)(sign << 15);
        } else {
            // subnormal
            mant |= 0x800000;
            uint32_t shift = (uint32_t)(14 - exp);
            uint32_t mant_h = mant >> (shift + 13);
            // round to nearest even
            if ((mant >> (shift + 12)) & 1u) mant_h += 1;
            h = (uint16_t)((sign << 15) | mant_h);
        }
    } else if (exp >= 31) {
        // overflow -> +/-Inf
        h = (uint16_t)((sign << 15) | (0x1F << 10));
    } else {
        // normal
        uint16_t mant_h = (uint16_t)(mant >> 13);
        // round
        if (mant & 0x00001000u) mant_h += 1;
        h = (uint16_t)((sign << 15) | ((uint16_t)exp << 10) | mant_h);
    }
    return h;
}
*/

// Convert 8 halfs -> 8 floats (dst must have room for 8 floats)
static inline void f16_to_f32_8(const uint16_t *src, float *dst) {

#if defined(__AVX__)
    // x86 F16C path
    __m128i h = _mm_loadu_si128((const __m128i*)src); // 8 x 16-bit
    __m256 f  = _mm256_cvtph_ps(h);                   // to 8 x float
    _mm256_storeu_ps(dst, f);
#elif defined(__aarch64__)
    // AArch64 NEON path
    float16x8_t hv;
    memcpy(&hv, src, sizeof(hv));                     // unaligned OK
    float32x4_t lo = vcvt_f32_f16(vget_low_f16(hv));
    float32x4_t hi = vcvt_f32_f16(vget_high_f16(hv));
    vst1q_f32(dst,    lo);
    vst1q_f32(dst+4,  hi);
#else
    // Scalar fallback
    for (int i = 0; i < 8; ++i) dst[i] = f16_to_f32_scalar(src[i]);
#endif

}

/*
// Convert 8 floats -> 8 halfs (src has 8 floats)
static inline void f32_to_f16_8(const float *src, uint16_t *dst) {

#if defined(__F16C__) || defined(__AVX512FP16__) || defined(__AVX__)
    __m256 f = _mm256_loadu_ps(src);
    __m128i h = _mm256_cvtps_ph(f, 0);                // round-to-nearest-even
    _mm_storeu_si128((__m128i*)dst, h);
#elif defined(__aarch64__)
    float32x4_t lo = vld1q_f32(src);
    float32x4_t hi = vld1q_f32(src+4);
    float16x4_t hlo = vcvt_f16_f32(lo);
    float16x4_t hhi = vcvt_f16_f32(hi);
    float16x8_t hv  = vcombine_f16(hlo, hhi);
    memcpy(dst, &hv, sizeof(hv));
#else
    for (int i = 0; i < 8; ++i) dst[i] = f32_to_f16_scalar(src[i]);
#endif
}
*/

// Horizontal sum of 8 floats
#if defined(__AVX__) // x86 AVX version takes __m256
static inline float hsum8_ps(__m256 v) {
    __m128 vlow  = _mm256_castps256_ps128(v);
    __m128 vhigh = _mm256_extractf128_ps(v, 1);
    __m128 vsum  = _mm_add_ps(vlow, vhigh);
    vsum = _mm_hadd_ps(vsum, vsum);
    vsum = _mm_hadd_ps(vsum, vsum);
    return _mm_cvtss_f32(vsum);
}
#elif defined(__aarch64__) // arm64 NEON version takes two 128-bit lanes
static inline float hsum8_ps_neon(float32x4_t lo, float32x4_t hi) {
    float32x4_t sum = vaddq_f32(lo, hi);
    return vaddvq_f32(sum); // horizontal add across 4 lanes (ARMv8)
}
#endif

/* ----------------------- User Functions -------------------------- */
/* Texis calls: int func(FLD *f)
 *   f[0] = return slot
 *   f[1]..f[minargs] = arguments
 */

/* vrank16(BYTE vec16, BYTE qvec16) -> DOUBLE in f[0] */
int vrank16(FLD *f1, FLD *f2)
{
    /* read args as varbyte */
    size_t len0 = 0, len1 = 0;
    int dim=0, sz=0;
    if(!f1)
    {
      putmsg(MERR + UGE, "vrank16", "Null field in arg 1");
      return(FOP_EINVAL);
    }
    if(!f2)
    {
      putmsg(MERR + UGE, "vrank16", "Null field in arg 2");
      return(FOP_EINVAL);
    }

    if((f1->type&DDTYPEBITS) != FTN_BYTE)
    {
      putmsg(MERR + UGE, "vrank16", "wrong type in field 1");
      return(FOP_EINVAL);
    }

    if((f2->type&DDTYPEBITS) != FTN_BYTE)
    {
      putmsg(MERR + UGE, "vrank16", "wrong type in field 2");
      return(FOP_EINVAL);
    }

    if(f1->size % 16 || f1->size != f2->size)
    {
      putmsg(MERR + UGE, "vrank16", "varbyte field must be a multiple of 16");
      return(FOP_EINVAL);
    }

    if(f1->size != f2->size)
    {
      putmsg(MERR + UGE, "vrank16", "varbyte fields must be the same size");
      return(FOP_EINVAL);
    }

    sz = (int)f1->size;
    dim=sz/2;

    const uint8_t *b0 = (const uint8_t *)getfld(f1, &len0);
    const uint8_t *b1 = (const uint8_t *)getfld(f2, &len1);
    int i=0;

    ft_double *sum = malloc(sizeof(ft_double));

    *sum = 0.0;

    if (!b0 || !b1)
    {
        /* return 0.0 on bad input */
        goto end;
    }

    const uint16_t *h0 = (const uint16_t *)b0;
    const uint16_t *h1 = (const uint16_t *)b1;

#if defined(__AVX__) // -------- x86: AVX/F16C ----------
    for (; i + 8 <= dim; i += 8) {
        float a8[8], b8[8];
        f16_to_f32_8(h0 + i, a8);
        f16_to_f32_8(h1 + i, b8);

        __m256 a = _mm256_loadu_ps(a8);
        __m256 b = _mm256_loadu_ps(b8);
        __m256 p = _mm256_mul_ps(a, b);
        *sum += hsum8_ps(p);          // your AVX hsum (__m256 -> float)
    }

#elif defined(__aarch64__) // ---- arm64: NEON ----------
    for (; i + 8 <= dim; i += 8) {
        float a8[8], b8[8];
        f16_to_f32_8(h0 + i, a8);
        f16_to_f32_8(h1 + i, b8);

        float32x4_t alo = vld1q_f32(a8);
        float32x4_t ahi = vld1q_f32(a8 + 4);
        float32x4_t blo = vld1q_f32(b8);
        float32x4_t bhi = vld1q_f32(b8 + 4);

        float32x4_t plo = vmulq_f32(alo, blo);
        float32x4_t phi = vmulq_f32(ahi, bhi);

        // Horizontal sums (ARMv8 has vaddvq_f32)
        *sum += vaddvq_f32(plo) + vaddvq_f32(phi);
    }

#else // --------------- portable scalar ----------------

    for (; i < dim; ++i) {
        float a = f16_to_f32_scalar(h0[i]);
        float b = f16_to_f32_scalar(h1[i]);
        *sum += a * b;
    }
#endif

    end:
    setfld(f1, sum, sizeof(ft_double));
    f1->elsz=sizeof(ft_double);
    f1->size=sizeof(ft_double);
    f1->type = FTN_DOUBLE;
    return 0;
}

/* ------------------------ Registration --------------------------- */

#define NUSERFUNC 1
static FLDFUNC g_vec16_funcs[NUSERFUNC] = {
    /* name,        func,         minargs, maxargs, rettype   input type     */
    { "vrank16",    F(vrank16),    2,       2,     FTN_DOUBLE, 0}
};

void adduserfuncs(FLDOP *fo)
{
    /* Register our functions; ignore return per your pattern */
    (void)foaddfuncs(fo, g_vec16_funcs, NUSERFUNC);
}

#endif
