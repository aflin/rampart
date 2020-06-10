#ifndef TXTYPES_H
#define TXTYPES_H


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef TX_CONFIG_H
#  include "txcoreconfig.h"
#endif /* !TX_CONFIG_H */
/* note: `#ifndef SIZES_H' needed for proper cmstrip generation of tispi.h: */
#ifndef SIZES_H
#  include "sizes.h"
#endif /* SIZES_H */
#ifdef EPI_HAVE_ISNAN_CALL
#  include <math.h>
#endif

/* Atomic operations on integers.
 * These are thread-safe (iff TX_ATOMIC_THREAD_SAFE defined),
 * signal-safe, non-blocking, and fast.
 * WTF thread-unsafe on some platforms (TX_ATOMIC_THREAD_SAFE undefined),
 * but still non-blocking and fast.
 *
 *   TX_ATOMIC_ADD(TXATOMINT *valPtr, TXATOMINT n)
 *     Atomic version of:   (*valPtr) += n
 *     Returns original value of *valPtr
 *
 *   TX_ATOMIC_SUB(TXATOMINT *valPtr, TXATOMINT n)
 *     Atomic version of:   (*valPtr) -= n
 *     Returns original value of *valPtr
 *
 *   TX_ATOMIC_INC(TXATOMINT *valPtr)
 *     Atomic version of:   (*valPtr)++
 *     Returns original value of *valPtr
 *
 *   TX_ATOMIC_DEC(TXATOMINT *valPtr)
 *     Atomic version of:   (*valPtr)--
 *     Returns original value of *valPtr
 *
 *   TX_ATOMIC_COMPARE_AND_SWAP(TXATOMINT *valPtr, TXATOMINT oldVal,
 *                              TXATOMINT newVal)
 *    Atomic version of:    if (*valPtr == oldVal) *valPtr = newVal
 *    Returns original value of *valPtr
 */

/* TX_ATOMIC_THREAD_SAFE determined at build time, in config_ind.h */
/* TX_ATOMIC_FALLBACK_FUNCS: defined if using fallback (thread-unsafe)
 *   functions when no other implementation available
 */
#undef TX_ATOMIC_FALLBACK_FUNCS                 /* do not need fallbacks */
#undef TXATOMINTBITS
#undef TXATOMUINTBITS
#undef TX_ATOMIC_IMPLEMENTATION_STR

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* These versions are preferred over gcc-deprecated __sync_...() functions:
 */
#if defined(EPI_HAVE_ATOMIC_FETCH_FUNCTIONS)
#  define TX_ATOMIC_IMPLEMENTATION_STR  "__atomic_...()"
/* Note: TXATOMINT type should be the same as EPI_HAVE_ATOMIC_FETCH_FUNCTIONS
 * auto-detect code in epi/Makefile; we only force it to be 32 bits here
 * so that TXATOMINT_MAX can be an integer literal for the pre-processor:
 * Note: TXATOMINT typedef'd volatile to avoid compiler optimizer issues:
 */
typedef EPI_INT32               TXATOMINT_NV;
typedef EPI_UINT32              TXATOMUINT_NV;
typedef volatile TXATOMINT_NV   TXATOMINT;
typedef volatile TXATOMUINT_NV  TXATOMUINT;
typedef EPI_INT64                       TXATOMINT_WIDE_NV;
typedef volatile TXATOMINT_WIDE_NV      TXATOMINT_WIDE;
#  define TXATOMINTBITS 32
#  define TXATOMUINTBITS 32
#  define TXATOMINT_MAX 0x7fffffff
#  define TXATOMUINT_MAX 0xffffffff
#  define TX_ATOMIC_ADD(valPtr, n)      \
  __atomic_fetch_add((valPtr), (n), __ATOMIC_SEQ_CST)
#  define TX_ATOMIC_SUB(valPtr, n)      \
  __atomic_fetch_sub((valPtr), (n), __ATOMIC_SEQ_CST)
#  define TX_ATOMIC_INC(valPtr)         TX_ATOMIC_ADD(valPtr, 1)
#  define TX_ATOMIC_DEC(valPtr)         TX_ATOMIC_SUB(valPtr, 1)
  /* this assumes gcc, which should be true given EPI_HAVE_ATOMIC_FETCH...: */
#  define TX_ATOMIC_COMPARE_AND_SWAP(valPtr, oldVal, newVal)            \
  ({ TXATOMINT_NV       _oldVal = oldVal;                               \
  __atomic_compare_exchange_n((valPtr), &_oldVal, (newVal), 0,          \
                               __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);     \
  _oldVal; })
#  define TX_ATOMIC_COMPARE_AND_SWAP_WIDE(valPtr, oldVal, newVal)       \
  ({ TXATOMINT_WIDE_NV  _oldVal = oldVal;                               \
  __atomic_compare_exchange_n((valPtr), &_oldVal, (newVal), 0,          \
                               __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);     \
  _oldVal; })
#  define TX_MEMORY_BARRIER()           __atomic_thread_fence(__ATOMIC_SEQ_CST)
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* Should use gcc -march=pentium to enable EPI_HAVE_SYNC_FETCH_FUNCTIONS.
 * These versions are preferred over the inline-asm versions for portability:
 */
#elif defined(EPI_HAVE_SYNC_FETCH_FUNCTIONS)
#  define TX_ATOMIC_IMPLEMENTATION_STR  "__sync_...()"
/* Note: TXATOMINT type should be the same as EPI_HAVE_SYNC_FETCH_FUNCTIONS
 * auto-detect code in epi/Makefile; we only force it to be 32 bits here
 * so that TXATOMINT_MAX can be an integer literal for the pre-processor:
 * Note: TXATOMINT typedef'd volatile to avoid compiler optimizer issues:
 */
typedef EPI_INT32               TXATOMINT_NV;
typedef EPI_UINT32              TXATOMUINT_NV;
typedef volatile TXATOMINT_NV   TXATOMINT;
typedef volatile TXATOMUINT_NV  TXATOMUINT;
typedef EPI_INT64                       TXATOMINT_WIDE_NV;
typedef volatile TXATOMINT_WIDE_NV      TXATOMINT_WIDE;
#  define TXATOMINTBITS 32
#  define TXATOMUINTBITS 32
#  define TXATOMINT_MAX 0x7fffffff
#  define TXATOMUINT_MAX 0xffffffff
#  define TX_ATOMIC_ADD(valPtr, n)      __sync_fetch_and_add((valPtr), (n))
#  define TX_ATOMIC_SUB(valPtr, n)      __sync_fetch_and_sub((valPtr), (n))
#  define TX_ATOMIC_INC(valPtr)         TX_ATOMIC_ADD(valPtr, 1)
#  define TX_ATOMIC_DEC(valPtr)         TX_ATOMIC_SUB(valPtr, 1)
#  define TX_ATOMIC_COMPARE_AND_SWAP(valPtr, oldVal, newVal)    \
  __sync_val_compare_and_swap((valPtr), (oldVal), (newVal))
#  define TX_ATOMIC_COMPARE_AND_SWAP_WIDE(valPtr, oldVal, newVal)       \
  __sync_val_compare_and_swap((valPtr), (oldVal), (newVal))
#  define TX_MEMORY_BARRIER()           __sync_synchronize()
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
#elif defined(_WIN32)
#  define TX_ATOMIC_IMPLEMENTATION_STR  "Interlocked...()"
/* Note: TXATOMINT typedef'd volatile to avoid compiler optimizer issues: */
typedef LONG                    TXATOMINT_NV;
typedef ULONG                   TXATOMUINT_NV;
typedef volatile TXATOMINT_NV   TXATOMINT;
typedef volatile TXATOMUINT_NV  TXATOMUINT;
typedef long long                       TXATOMINT_WIDE_NV;
typedef volatile TXATOMINT_WIDE_NV      TXATOMINT_WIDE;
/* WTF ...BITS/MAX need #include of sizes.h, which may not exist yet: */
#  define TXATOMINTBITS EPI_OS_LONG_BITS
#  define TXATOMUINTBITS EPI_OS_ULONG_BITS
#  define TXATOMINT_MAX EPI_OS_LONG_MAX
#  define TXATOMUINT_MAX EPI_OS_ULONG_MAX
#  define TX_ATOMIC_ADD(valPtr, n)      InterlockedExchangeAdd((valPtr), (n))
#  define TX_ATOMIC_SUB(valPtr, n)      InterlockedExchangeAdd((valPtr), -(n))
/* Interlocked{Inc|Dec}rement() return the value *after* the operation,
 * so add/subtract to/from return value for TX_ATOMIC_...() semantics:
 */
#  define TX_ATOMIC_INC(valPtr)         (InterlockedIncrement(valPtr) - 1L)
#  define TX_ATOMIC_DEC(valPtr)         (InterlockedDecrement(valPtr) + 1L)
#  define TX_ATOMIC_COMPARE_AND_SWAP(valPtr, oldVal, newVal)    \
  InterlockedCompareExchange((valPtr), (newVal), (oldVal))
#  define TX_ATOMIC_COMPARE_AND_SWAP_WIDE(valPtr, oldVal, newVal)       \
  InterlockedCompareExchange64((valPtr), (newVal), (oldVal))
#  define TX_MEMORY_BARRIER()           MemoryBarrier()
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* This version uses some i486+ instructions; should use gcc -march=pentium
 * or -march=i486:
 */
/* WTF KNG 20061012 TX_ATOMIC_ADD/SUB broken under gcc 4 */
/* KNG 20081123 appears fixed in gcc 4.1.2 */
/* KNG 20090410 probably use __sync_fetch... functions in gcc 4.1+ anyway */
/* KNG 20110621 using inline function works under gcc 4.0.2 */
/* KNG 20110621 enable if `__i386' too: almost certainly running on a 486
 * or better these days; do not care about 386-only compatibility anymore:
 */
#elif defined(__GNUC__) && __GNUC__ >= 2 && (defined(__i486) || defined(__pentium) || defined(__pentiumpro) || defined(__x86_64) || defined(__i386))
#  define TX_ATOMIC_IMPLEMENTATION_STR  "i486/Pentium GCC assembly"
/* Note: TXATOMINT typedef'd volatile to avoid compiler optimizer issues: */
/* Note: TXATOMINT assumed 32 bits in inline assembly below: */
typedef EPI_INT32               TXATOMINT_NV;
typedef EPI_UINT32              TXATOMUINT_NV;
typedef volatile TXATOMINT_NV   TXATOMINT;
typedef volatile TXATOMUINT_NV  TXATOMUINT;
typedef EPI_INT64                       TXATOMINT_WIDE_NV;
typedef volatile TXATOMINT_WIDE_NV      TXATOMINT_WIDE;
#  define TXATOMINTBITS 32
#  define TXATOMUINTBITS 32
#  define TXATOMINT_MAX 0x7fffffff
#  define TXATOMUINT_MAX 0xffffffff
static __inline__ TXATOMINT_NV                  /* w/o volatile; no warning */
TXatomicAdd(TXATOMINT *valPtr, TXATOMINT n)
{
  __asm__ __volatile__(
                       "lock;"
                       "xaddl %%eax, %2;"
                       : "=a" (n)               /* output registers */
                       : "a" (n), "m" (*valPtr) /* input registers */
                       : "memory"               /* clobbered registers */
                       );
  return(n);
}
#  define TX_ATOMIC_ADD(valPtr, n)      TXatomicAdd(valPtr, n)
#  define TX_ATOMIC_SUB(valPtr, n)      TX_ATOMIC_ADD(valPtr, (-(n)))
#  define TX_ATOMIC_INC(valPtr)         TX_ATOMIC_ADD(valPtr, 1)
#  define TX_ATOMIC_DEC(valPtr)         TX_ATOMIC_SUB(valPtr, 1)
static __inline__ TXATOMINT_NV                  /* w/o volatile; no warning */
TXatomicCompareAndSwap(TXATOMINT *valPtr, TXATOMINT oldVal, TXATOMINT newVal)
{
  TXATOMINT     ret;

  __asm__ __volatile__ (
        "lock;"
        "cmpxchgl %2, %1;"      /* if (AX == *valPtr) *valPtr = newVal */
                                /* else AX = *valPtr */
        : "=a" (ret), "=m" (*valPtr)                    /* output registers */
        : "r" (newVal), "m" (*valPtr), "a" (oldVal)     /* input registers */
        : "memory"              /* clobbered registers */
                        );
  return(ret);
}
#  define TX_ATOMIC_COMPARE_AND_SWAP(valPtr, oldVal, newVal)    \
  TXatomicCompareAndSwap((valPtr), (oldVal), (newVal))
#  if 0 /* untested: */
/* This code from:
 https://github.com/drothlis/open-vm-tools/blob/master/lib/include/vm_atomic.h
 */
static
#  if __GNUC__ >= 3             /* per above link */
  __inline__
#  endif
  TXATOMINT_WIDE_NV             /* w/o volatile; no warning */
TXatomicCompareAndSwapWide(TXATOMINT_WIDE *valPtr, TXATOMINT_WIDE oldVal,
                           TXATOMINT_WIDE newVal)
{
#  ifdef __x86_64
#    error should have used __sync... functions above
#  endif
  struct
  {
    TXATOMINT_NV        lowValue;
    TXATOMINT_NV        highValue;
  }
  *oldValPtr = (void *)&oldVal, *newValPtr = (void *)&newVal;
#  ifdef __PIC__
  char  equal;
#    if __GNUC__ < 3
#      error This code must be adapted from __sync_bool_compare_and_swap() return style to __sync_val_compare_and_swap() return style
  int   dummy1, dummy2;
  __asm__ __volatile__(
      "xchg %%ebx, %6;"
      "mov 4(%%ebx), %%ecx;"
      "mov (%%ebx), %%ebx;"
      "lock; cmpxchg8b (%3);"
      "xchg %%ebx, %6;"
      "sete %0;"
      : "=a" (equal),                           /* output registers */
        "=d" (dummy2),                          /* %1 */
        "=D" (dummy1)                           /* %2 */
      : /* Input registers.  See the
         * "Rules for __asm__ statements in __PIC__ code" in link above:
         * %3 must use a register class which does not contain %ebx.
         */
        "S" (valPtr),                           /* %3 */
        "0" (oldValPtr->lowValue),              /* %4 */
        "1" (oldValPtr->highValue),             /* %5 */
        "D" (newValPtr)                         /* %6 */
      : "ecx", "cc", "memory"                   /* clobber registers */
                       );
#    else
#      error Get gcc 4+ version from above link
#    endif
  return(equal);
#  else /* !__PIC__ */
  TXATOMINT_NV          outLow, outHigh;
  char  equal;
  __asm__ __volatile__(
      "lock; cmpxchg8b %0;"
      "sete %1;"
      : "+m" (*valPtr),                         /* output registers */
        "=qm" (equal),                          /* %1 */
        "=a" (outLow),
        "=d" (outHigh)
      : "2" (oldValPtr->lowValue),              /* input registers */
        "3" (oldValPtr->highValue),
        "b" (newValPtr->lowValue),
        "c" (newValPtr->highValue)
      : "cc"                                    /* clobbered registers */
                       );
  return(((EPI_UINT64)(EPI_UINT32)outHigh << 32) |
          (EPI_UINT64)(EPI_UINT32)outLow);
#  endif /* !__PIC__ */
}
#  define TX_ATOMIC_COMPARE_AND_SWAP_WIDE(valPtr, oldVal, newVal)       \
  TXatomicCompareAndSwapWide((valPtr), (oldVal), (newVal))
#  endif /* 0 untested */
static __inline__ void
TXmemoryBarrier(void)
{
  __asm__ __volatile__("" : : : "memory");
}
#  define TX_MEMORY_BARRIER()           TXmemoryBarrier()
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* This version uses SPARC v9 instructions; use gcc -mcpu=v9 (wtf fails?) */
#elif defined(__GNUC__) && __GNUC__ >= 2 && defined(__sparcv9)
#  define TX_ATOMIC_IMPLEMENTATION_STR  "SPARC v9 GCC assembly"
/* Note: TXATOMINT typedef'd volatile to avoid compiler optimizer issues: */
typedef EPI_INT32               TXATOMINT_NV;
typedef EPI_UINT32              TXATOMUINT_NV;
typedef volatile TXATOMINT_NV   TXATOMINT;
typedef volatile TXATOMUINT_NV  TXATOMUINT;
typedef EPI_INT64                       TXATOMINT_WIDE_NV;
typedef volatile TXATOMINT_WIDE_NV      TXATOMINT_WIDE;
#  define TXATOMINTBITS 32
#  define TXATOMUINTBITS 32
#  define TXATOMINT_MAX 0x7fffffff
#  define TXATOMUINT_MAX 0xffffffff
#  define TX_ATOMIC_ADD(valPtr, n)                                      \
   ({ register TXATOMINT        __tmp;                                  \
    __asm__ __volatile__ (                                              \
      "txai_again%=:\n\t"                                               \
      "ld      [%1], %%o2\n\t"          /* o2 = *valPtr */              \
      "add     %%o2, %2, %%o3\n\t"      /* o3 = o2 + n */               \
      "cas     [%1], %%o2, %%o3\n\t"    /* *valPtr == o2 ? *valPtr = o3 */ \
      "cmp     %%o2, %%o3\n\t"          /* did we set it? */            \
      "bne     txai_again%=\n\t"        /* no, try again */             \
      "mov     %%o2, %0\n\t"            /* __tmp = o2 */                \
      "nop"                             /* for pipeline */              \
      : "=r" (__tmp)                    /* output registers */          \
      : "r" (valPtr), "r" (n)           /* input registers */           \
      : "%o2", "%o3", "memory", "cc"    /* clobber registers */         \
    );                                                                  \
   __tmp; })
#  define TX_ATOMIC_SUB(valPtr, n)                                      \
   ({ register TXATOMINT        __tmp;                                  \
    __asm__ __volatile__ (                                              \
      "txad_again%=:\n\t"                                               \
      "ld      [%1], %%o2\n\t"          /* o2 = *valPtr */              \
      "sub     %%o2, %2, %%o3\n\t"      /* o3 = o2 - n */               \
      "cas     [%1], %%o2, %%o3\n\t"    /* *valPtr == o2 ? *valPtr = o3 */ \
      "cmp     %%o2, %%o3\n\t"          /* did we set it? */            \
      "bne     txad_again%=\n\t"        /* no, try again */             \
      "mov     %%o2, %0\n\t"            /* __tmp = o2 */                \
      "nop"                             /* for pipeline */              \
      : "=r" (__tmp)                    /* output registers */          \
      : "r" (valPtr), "r" (n)           /* input registers */           \
      : "%o2", "%o3", "memory", "cc"    /* clobber registers */         \
    );                                                                  \
   __tmp; })
#  define TX_ATOMIC_INC(valPtr) TX_ATOMIC_ADD(valPtr ,1)
#  define TX_ATOMIC_DEC(valPtr) TX_ATOMIC_SUB(valPtr ,1)
#  error Need TX_ATOMIC_COMPARE_AND_SWAP[_WIDE]() implementations
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* Itanium/ia64: */
#elif defined(__GNUC__) && defined(__itanium__)
#  define TX_ATOMIC_IMPLEMENTATION_STR  "Itanium GCC assembly"
/* Assembly below seems to require 32-bit ints: */
/* Note: TXATOMINT typedef'd volatile to avoid compiler optimizer issues: */
typedef EPI_INT32               TXATOMINT_NV;
typedef EPI_UINT32              TXATOMUINT_NV;
typedef volatile TXATOMINT_NV   TXATOMINT;
typedef volatile TXATOMUINT_NV  TXATOMUINT;
typedef EPI_INT64                       TXATOMINT_WIDE_NV;
typedef volatile TXATOMINT_WIDE_NV      TXATOMINT_WIDE;
#  define TXATOMINTBITS 32
#  define TXATOMUINTBITS 32
#  define TXATOMINT_MAX 0x7fffffffL
#  define TXATOMUINT_MAX 0xffffffffL
static __inline__ TXATOMINT_NV                  /* w/o volatile; no warning */
TXatomicAdd(TXATOMINT *operand, TXATOMINT incr)
{
  TXATOMINT     res, oldVal, newVal, org = *operand;
  do
    {
      oldVal = *operand;        /* atomic if operand is 32-bit aligned */
      newVal = oldVal + incr;
      __asm__ __volatile__ (
        "mov ar.ccv=%0;;"
        :                                       /* output registers */
        : "rO" (oldVal)                         /* input registers */
      );
      /* these are separate because the compiler inserts stuff in between */
      __asm__ __volatile__ (
        "cmpxchg4.acq %0=[%1],%2,ar.ccv"
        : "=r" (res)                            /* output registers */
        : "r" (operand), "r" (newVal)           /* input registers */
        : "memory"                              /* clobber registers */
      );
    }
  while (res != oldVal);        /* if res == oldVal, our value is stale */
  return(org);                                  /* per our spec */
}
#  define TX_ATOMIC_ADD(valPtr, n)      TXatomicAdd(valPtr, n)
#  define TX_ATOMIC_SUB(valPtr, n)      TXatomicAdd(valPtr, -(n))
#  define TX_ATOMIC_INC(valPtr)         TX_ATOMIC_ADD(valPtr, 1)
#  define TX_ATOMIC_DEC(valPtr)         TX_ATOMIC_SUB(valPtr, 1)
#  error Need TX_ATOMIC_COMPARE_AND_SWAP[_WIDE]() implementations
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
#else /* unknown platform */
#  define TX_ATOMIC_IMPLEMENTATION_STR  "Fallback thread-unsafe functions"
/* WTF thread-unsafe, signal-unsafe */
/* Note: TXATOMINT typedef'd volatile to avoid compiler optimizer issues: */
typedef EPI_INT32                       TXATOMINT_NV;
typedef EPI_UINT32                      TXATOMUINT_NV;
typedef volatile TXATOMINT_NV           TXATOMINT;
typedef volatile TXATOMUINT_NV          TXATOMUINT;
typedef EPI_INT64                       TXATOMINT_WIDE_NV;
typedef volatile TXATOMINT_WIDE_NV      TXATOMINT_WIDE;
#  define TXATOMINTBITS 32
#  define TXATOMUINTBITS 32
#  define TXATOMINT_MAX 0x7fffffffL
#  define TXATOMUINT_MAX 0xffffffffL
/* Return w/o volatile; no compiler warning: */
TXATOMINT_NV TXatomicAdd ARGS((TXATOMINT *valPtr, TXATOMINT n));
TXATOMINT_NV TXatomicSub ARGS((TXATOMINT *valPtr, TXATOMINT n));
TXATOMINT_NV TXatomicCompareAndSwap(TXATOMINT *valPtr, TXATOMINT oldVal,
                                    TXATOMINT newVal);
TXATOMINT_WIDE_NV TXatomicCompareAndSwapWide(TXATOMINT_WIDE *valPtr,
                           TXATOMINT_WIDE oldVal, TXATOMINT_WIDE newVal);
#  define TX_ATOMIC_ADD(valPtr, n)      TXatomicAdd(valPtr, n)
#  define TX_ATOMIC_SUB(valPtr, n)      TXatomicSub(valPtr, n)
#  define TX_ATOMIC_INC(valPtr)         ((*(valPtr))++)
#  define TX_ATOMIC_DEC(valPtr)         ((*(valPtr))--)
#  define TX_ATOMIC_COMPARE_AND_SWAP(valPtr, oldVal, newVal)    \
  TXatomicCompareAndSwap((valPtr), (oldVal), (newVal))
#  define TX_ATOMIC_COMPARE_AND_SWAP_WIDE(valPtr, oldVal, newVal)       \
  TXatomicCompareAndSwapWide((valPtr), (oldVal), (newVal))
#  define TX_ATOMIC_FALLBACK_FUNCS
#endif /* unknown platform */
#define TXATOMINTPN     ((TXATOMINT *)NULL)

/* ------------------------------------------------------------------------ */

/* Use TX_PIPE_MAX, not PIPE_MAX: a write() of PIPE_MAX may block on HP-UX: */
#ifdef EPI_PIPE_MAX                             /* overrides PIPE_MAX */
#  define TX_PIPE_MAX   EPI_PIPE_MAX
#else
#  define TX_PIPE_MAX   PIPE_MAX
#endif

/* ------------------------------------------------------------------------ */

/* EPI_UNSAFE_FP_OPS: +/-/== etc. on Inf/-Inf/NaN can cause FPE.
 * wtf auto-detect in source/epi/sizes.c?  separate macro for NaN vs. Inf?
 * separate macro for each op?
 * Alpha: FPE
 * hppa1.1-hp-hpux11.00/hppa1.1-hp-hpux10.20: incorrect </<=/>/>= with NaN
 */
#undef EPI_UNSAFE_FP_OPS
#if defined(__alpha) || defined(hpux) || defined(__hpux)
#  define EPI_UNSAFE_FP_OPS     1
#endif /* __alpha */

/* EPI_UNSAFE_STRTOD: strtod() either FPEs or does not return proper value
 * for Inf/-Inf/NaN.  wtf auto-detect in source/epi/sizes.c?
 */
#undef EPI_UNSAFE_STRTOD
#if defined(__alpha) || defined(__sgi) || defined(hpux) || defined(__hpux)
#  define EPI_UNSAFE_STRTOD
#endif

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* Macros for testing double values carefully to avoid FPE.
 * NOTE that all arguments MUST be true double lvalues, as bits are accessed:
 */
#if EPI_OS_DOUBLE_BITS != 64
error TXDOUBLE_... macros assume sizeof(double) == 8;
#endif /* EPI_OS_DOUBLE_BITS != 64 */
#define TXDOUBLE_HI32_SIGNBIT   0x80000000
#define TXDOUBLE_HI32_EXPMASK   0x7ff00000
#define TXDOUBLE_HI32_MANTMASK  0x000fffff
/* wtf (void *) stops `dereferencing type-punned pointer' compiler warning: */
#ifdef EPI_LITTLE_ENDIAN
#  define TXDOUBLE_LO32(x)      (((EPI_UINT32 *)(void *)&(x))[0])
#  define TXDOUBLE_HI32(x)      (((EPI_UINT32 *)(void *)&(x))[1])
#elif defined(EPI_BIG_ENDIAN)
#  define TXDOUBLE_LO32(x)      (((EPI_UINT32 *)(void *)&(x))[1])
#  define TXDOUBLE_HI32(x)      (((EPI_UINT32 *)(void *)&(x))[0])
#else
  error error error;
#endif
/* wtf memcpys avoid `dereference of type-punned pointer' gcc warning: */
/* TXDOUBLE_IS_INFINITE(x): true if x is plus or minus infinity: */
#define TXDOUBLE_IS_INFINITE(x)                                              \
    ((TXDOUBLE_HI32(x) & ~TXDOUBLE_HI32_SIGNBIT) == TXDOUBLE_HI32_EXPMASK && \
     !TXDOUBLE_LO32(x))
#ifdef __GNUC__
/* TXDOUBLE_IS_PosInf(x): true if x is +Inf: */
#  define TXDOUBLE_IS_PosInf(x)                                         \
  ({ EPI_UINT64 _gotBits, _posInfBits =                                 \
      (((EPI_UINT64)TXDOUBLE_HI32_EXPMASK << 32) | 0);                  \
   memcpy(&_gotBits, &(x), sizeof(double)); _gotBits == _posInfBits; })
/* TXDOUBLE_IS_NegInf(x): true if x is -Inf: */
#  define TXDOUBLE_IS_NegInf(x)                                              \
  ({ EPI_UINT64 _gotBits, _negInfBits =                                      \
   (((EPI_UINT64)(TXDOUBLE_HI32_SIGNBIT | TXDOUBLE_HI32_EXPMASK) << 32) | 0);\
   memcpy(&_gotBits, &(x), sizeof(double)); _gotBits == _negInfBits; })
#else /* !__GNUC__ */
#define TXDOUBLE_IS_PosInf(x)   \
  (TXDOUBLE_HI32(x) == TXDOUBLE_HI32_EXPMASK && TXDOUBLE_LO32(x) == 0)
#  define TXDOUBLE_IS_NegInf(x)                                           \
  (TXDOUBLE_HI32(x) == (TXDOUBLE_HI32_SIGNBIT | TXDOUBLE_HI32_EXPMASK) && \
   TXDOUBLE_LO32(x) == 0)
#endif /* !__GNUC__ */

/* TXDOUBLE_IS_NEGZERO(x): true if x is -0 (? not sure) */
#define TXDOUBLE_IS_NEGZERO(x)  (TXDOUBLE_HI32(x) == TXDOUBLE_HI32_SIGNBIT &&\
                                 TXDOUBLE_LO32(x) == 0)
/* TXDOUBLE_SET_PosInf(x): sets x to Inf.  x must be a double lvalue: */
#define TXDOUBLE_SET_PosInf(x)                                               \
  do { EPI_UINT64 _bits =                                                    \
  (((EPI_UINT64)(                        TXDOUBLE_HI32_EXPMASK) << 32) | 0); \
  memcpy((void *)&(x), &_bits, sizeof(double)); } while (0)
/* TXDOUBLE_SET_NegInf(x): sets x to -Inf.  x must be a double lvalue: */
#define TXDOUBLE_SET_NegInf(x)                                              \
  do { EPI_UINT64 _bits =                                                   \
  (((EPI_UINT64)(TXDOUBLE_HI32_SIGNBIT | TXDOUBLE_HI32_EXPMASK) << 32) | 0);\
  memcpy((void *)&(x), &_bits, sizeof(double)); } while (0)

/* TXDOUBLE_IS_NaN(x): 1 if x is NaN, 0 if not.  x must be a double lvalue: */
#ifdef EPI_HAVE_ISNAN_CALL
/* Use isnan() if available: seems to help prevent some optimizer issues: */
#  define TXDOUBLE_IS_NaN(x)    isnan(x)
#else /* !EPI_HAVE_ISNAN_CALL */
#define TXDOUBLE_IS_NaN(x)                                                   \
    ((TXDOUBLE_HI32(x) & TXDOUBLE_HI32_EXPMASK) == TXDOUBLE_HI32_EXPMASK &&  \
     (TXDOUBLE_LO32(x) || (TXDOUBLE_HI32(x) & TXDOUBLE_HI32_MANTMASK)))
#endif /* !EPI_HAVE_ISNAN_CALL */
/* TXDOUBLE_SET_NaN(x): sets x to NaN.  x must be a double lvalue: */
/* wtf memcpy avoids `dereference of type-punned pointer' gcc error */
#define TXDOUBLE_SET_NaN(x)                                             \
  do { EPI_UINT64 _bits =                                               \
  (((EPI_UINT64)(TXDOUBLE_HI32_EXPMASK | TXDOUBLE_HI32_MANTMASK) << 32) | \
   ((EPI_UINT64)0xffffffff));                                           \
  memcpy((void *)&(x), &_bits, sizeof(double)); } while (0)

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* TXFLOAT_... macros: for floats.
 * NOTE that all arguments MUST be true float lvalues, as bits are accessed:
 */
#if EPI_OS_FLOAT_BITS != 32
error TXFLOAT_... macros assume sizeof(float) == 4;
#endif /* EPI_OS_FLOAT_BITS != 32 */
#define TXFLOAT_SIGNBIT         0x80000000
#define TXFLOAT_EXPMASK         0x7f800000
#define TXFLOAT_MANTMASK        0x007fffff
#define TXFLOAT_32(x)   (*((EPI_UINT32 *)(void *)&(x)))
/* TXFLOAT_IS_INFINITE(x): true if x is plus or minus infinity: */
#define TXFLOAT_IS_INFINITE(x)  \
  ((TXFLOAT_32(x) & ~TXFLOAT_SIGNBIT) == TXFLOAT_EXPMASK)
/* TXFLOAT_IS_PosInf(x): true if x is +Inf: */
#define TXFLOAT_IS_PosInf(x)    \
  (TXFLOAT_32(x) == TXFLOAT_EXPMASK)
/* TXFLOAT_IS_NegInf(x): true if x is -Inf: */
#define TXFLOAT_IS_NegInf(x)    \
  (TXFLOAT_32(x) == (TXFLOAT_SIGNBIT | TXFLOAT_EXPMASK))
/* TXFLOAT_IS_NEGZERO(x): true if x is -0 (? not sure) */
#define TXFLOAT_IS_NEGZERO(x)  \
  (TXFLOAT_32(x) == TXFLOAT_SIGNBIT)
/* TXFLOAT_SET_PosInf(x): sets x to -Inf.  x must be a float lvalue: */
#define TXFLOAT_SET_PosInf(x)   \
  (TXFLOAT_32(x) = TXFLOAT_EXPMASK)
/* TXFLOAT_SET_NegInf(x): sets x to -Inf.  x must be a float lvalue: */
#define TXFLOAT_SET_NegInf(x)   \
  (TXFLOAT_32(x) = (TXFLOAT_EXPMASK | TXFLOAT_SIGNBIT))

/* TXFLOAT_IS_NaN(x): 1 if x is NaN, 0 if not.  x must be a float lvalue: */
#define TXFLOAT_IS_NaN(x)                                       \
  ((TXFLOAT_32(x) & TXFLOAT_EXPMASK) == TXFLOAT_EXPMASK &&      \
   (TXFLOAT_32(x) & TXFLOAT_MANTMASK))
/* TXFLOAT_SET_NaN(x): sets x to NaN.  x must be a float lvalue: */
#define TXFLOAT_SET_NaN(x)      \
  (TXFLOAT_32(x) = (TXFLOAT_EXPMASK | 0x00400000))

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
#undef EPI_INCLUDED_IEEEFP_H
#ifdef EPI_HAVE_LONG_DOUBLE
#  undef EPI_LONG_DOUBLE_IS_DOUBLE
#  if EPI_OS_LONG_DOUBLE_BITS == EPI_OS_DOUBLE_BITS && EPI_OS_LONG_DOUBLE_MANTISSA_BITS == EPI_OS_DOUBLE_MANTISSA_BITS && EPI_OS_LONG_DOUBLE_EXPONENT_BITS == EPI_OS_DOUBLE_EXPONENT_BITS
#    define EPI_LONG_DOUBLE_IS_DOUBLE
#  endif
  /* TXLONGDOUBLE_IS_NaN(x): 1 if x is NaN, 0 if not.  x must be a long
   * double lvalue:
   */
#  ifdef EPI_HAVE_ISNAN_CALL
  /* Use isnan() if available: seems to help prevent some optimizer issues.
   * It should be a macro that can deal with any size floating-point type:
   */
#    define TXLONGDOUBLE_IS_NaN(x)      isnan(x)
#  elif defined(EPI_LONG_DOUBLE_IS_DOUBLE)
#    define TXLONGDOUBLE_IS_NaN(x)      TXDOUBLE_IS_NaN(x)
#  elif defined(EPI_HAVE_FPCLASS_CALL)
#    if defined(EPI_HAVE_IEEEFP_H) && !defined(EPI_INCLUDED_IEEEFP_H)
#      include <ieeefp.h>
#      define EPI_INCLUDED_IEEEFP_H
#    endif /* EPI_HAVE_IEEEFP_H */
#    define TXLONGDOUBLE_IS_NaN(x)      (fpclass(x) == FP_SNAN || \
                                         fpclass(x) == FP_QNAN)
#  else
  /* wtf check bits */
#  endif
/* TXLONGDOUBLE_SET_NaN(x): sets x to NaN.  x must be a long double lvalue: */
#  define TXLONGDOUBLE_SET_NaN(x)       ((x) = nanl(""))

/* TXLONGDOUBLE_IS_PosInf(x): true if x is +Inf: */
/* TXLONGDOUBLE_IS_NegInf(x): true if x is -Inf: */
#  ifdef EPI_HAVE_ISINF_CALL
#    define TXLONGDOUBLE_IS_PosInf(x)     (isinf(x) && (x) > (long double)0)
#    define TXLONGDOUBLE_IS_NegInf(x)     (isinf(x) && (x) < (long double)0)
#  elif defined(EPI_LONG_DOUBLE_IS_DOUBLE)
#    define TXLONGDOUBLE_IS_PosInf(x)     TXDOUBLE_IS_PosInf(x)
#    define TXLONGDOUBLE_IS_NegInf(x)     TXDOUBLE_IS_NegInf(x)
#  elif defined(EPI_HAVE_FPCLASS_CALL)
#    if defined(EPI_HAVE_IEEEFP_H) && !defined(EPI_INCLUDED_IEEEFP_H)
#      include <ieeefp.h>
#      define EPI_INCLUDED_IEEEFP_H
#    endif /* EPI_HAVE_IEEEFP_H */
#    define TXLONGDOUBLE_IS_PosInf(x)   (fpclass(x) == FP_PINF)
#    define TXLONGDOUBLE_IS_NegInf(x)   (fpclass(x) == FP_NINF)
#  else
  /* wtf */
#  endif
/* EPI_HUGEFLOAT versions: */
#  define TXHUGEFLOAT_IS_NaN(x)         TXLONGDOUBLE_IS_NaN(x)
#  define TXHUGEFLOAT_SET_NaN(x)        TXLONGDOUBLE_SET_NaN(x)
#  define TXHUGEFLOAT_IS_PosInf(x)      TXLONGDOUBLE_IS_PosInf(x)
#  define TXHUGEFLOAT_IS_NegInf(x)      TXLONGDOUBLE_IS_NegInf(x)
#else /* !EPI_HAVE_LONG_DOUBLE */
#  define TXHUGEFLOAT_IS_NaN(x)         TXDOUBLE_IS_NaN(x)
#  define TXHUGEFLOAT_SET_NaN(x)        TXDOUBLE_SET_NaN(x)
#  define TXHUGEFLOAT_IS_PosInf(x)      TXDOUBLE_IS_PosInf(x)
#  define TXHUGEFLOAT_IS_NegInf(x)      TXDOUBLE_IS_NegInf(x)
#endif /* !EPI_HAVE_LONG_DOUBLE */

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* Binary operators.  Args must be same-type-as-macro-name lvalues.
 * Note that we follow IEEE-754, in particular for NaN, which can break
 * trichotomy and means that NaN's order in an index is undefined:
 */
#ifdef EPI_UNSAFE_FP_OPS
#  define TXFLOAT_ISEQ(a, b)    \
  (TXFLOAT_IS_NaN(a) || TXFLOAT_IS_NaN(b) ? 0 : (a) == (b))
#  define TXDOUBLE_ISEQ(a, b)   \
  (TXDOUBLE_IS_NaN(a) || TXDOUBLE_IS_NaN(b) ? 0 : (a) == (b))
#  define TXFLOAT_ISLT(a, b)    \
  (TXFLOAT_IS_NaN(a) || TXFLOAT_IS_NaN(b) ? 0 : (a) < (b))
#  define TXDOUBLE_ISLT(a, b)   \
  (TXDOUBLE_IS_NaN(a) || TXDOUBLE_IS_NaN(b) ? 0 : (a) < (b))
#  define TXFLOAT_ISLTE(a, b)   \
  (TXFLOAT_IS_NaN(a) || TXFLOAT_IS_NaN(b) ? 0 : (a) <= (b))
#  define TXDOUBLE_ISLTE(a, b)  \
  (TXDOUBLE_IS_NaN(a) || TXDOUBLE_IS_NaN(b) ? 0 : (a) <= (b))
#  define TXFLOAT_ISGT(a, b)    \
  (TXFLOAT_IS_NaN(a) || TXFLOAT_IS_NaN(b) ? 0 : (a) > (b))
#  define TXDOUBLE_ISGT(a, b)   \
  (TXDOUBLE_IS_NaN(a) || TXDOUBLE_IS_NaN(b) ? 0 : (a) > (b))
#  define TXFLOAT_ISGTE(a, b)   \
  (TXFLOAT_IS_NaN(a) || TXFLOAT_IS_NaN(b) ? 0 : (a) >= (b))
#  define TXDOUBLE_ISGTE(a, b)  \
  (TXDOUBLE_IS_NaN(a) || TXDOUBLE_IS_NaN(b) ? 0 : (a) >= (b))
#  define TXFLOAT_ADD(c, a, b)                                               \
(TXFLOAT_IS_NaN(a) || TXFLOAT_IS_NaN(b) ? TXFLOAT_SET_NaN(c) :               \
(TXFLOAT_IS_PosInf(a) ? (TXFLOAT_IS_NegInf(b)?TXFLOAT_SET_NaN(c):((c)=(a))): \
(TXFLOAT_IS_NegInf(a) ?(TXFLOAT_IS_PosInf(b)?TXFLOAT_SET_NaN(c):((c)=(a))):  \
(TXFLOAT_IS_INFINITE(b) ? ((c) = (b)) : ((c) = (a) + (b))))))
#  define TXDOUBLE_ADD(c, a, b)                                              \
(TXDOUBLE_IS_NaN(a) || TXDOUBLE_IS_NaN(b) ? TXDOUBLE_SET_NaN(c) :            \
(TXDOUBLE_IS_PosInf(a)?(TXDOUBLE_IS_NegInf(b)?TXDOUBLE_SET_NaN(c):((c)=(a))):\
(TXDOUBLE_IS_NegInf(a)?(TXDOUBLE_IS_PosInf(b)?TXDOUBLE_SET_NaN(c):((c)=(a))):\
(TXDOUBLE_IS_INFINITE(b) ? ((c) = (b)) : ((c) = (a) + (b))))))
void TXdouble2float ARGS((CONST double *dp, float *fp));
#  define TXHUGEFLOAT_ADD(c, a, b)                                           \
(TXHUGEFLOAT_IS_NaN(a) || TXHUGEFLOAT_IS_NaN(b) ? TXHUGEFLOAT_SET_NaN(c) :   \
(TXHUGEFLOAT_IS_PosInf(a) ? (TXHUGEFLOAT_IS_NegInf(b) ?TXHUGEFLOAT_SET_NaN(c)\
: ((c) = (a))) :                                                             \
(TXHUGEFLOAT_IS_NegInf(a) ? (TXHUGEFLOAT_IS_PosInf(b) ?TXHUGEFLOAT_SET_NaN(c)\
: ((c) = (a))) :                                                             \
(TXHUGEFLOAT_IS_INFINITE(b) ? ((c) = (b)) : ((c) = (a) + (b))))))
#else /* !EPI_UNSAFE_FP_OPS */
#  define TXFLOAT_ISEQ(a, b)    ((a) == (b))
#  define TXDOUBLE_ISEQ(a, b)   ((a) == (b))
#  define TXFLOAT_ISLT(a, b)    ((a) < (b))
#  define TXDOUBLE_ISLT(a, b)   ((a) < (b))
#  define TXFLOAT_ISLTE(a, b)   ((a) <= (b))
#  define TXDOUBLE_ISLTE(a, b)  ((a) <= (b))
#  define TXFLOAT_ISGT(a, b)    ((a) > (b))
#  define TXDOUBLE_ISGT(a, b)   ((a) > (b))
#  define TXFLOAT_ISGTE(a, b)   ((a) >= (b))
#  define TXDOUBLE_ISGTE(a, b)  ((a) >= (b))
#  define TXFLOAT_ADD(c, a, b)  ((c) = (a) + (b))
#  define TXDOUBLE_ADD(c, a, b) ((c) = (a) + (b))
#  define TXHUGEFLOAT_ADD(c, a, b) ((c) = (a) + (b))
#  define TXdouble2float(dp, fp)        (*(fp) = (float)(*(dp)))
#endif /* !EPI_UNSAFE_FP_OPS */

/* TX{DOUBLE|FLOAT}_CMP(a, b): compare of two doubles/floats, with
 * strcmp()-style return (<0, 0, >0).  Note that in IEEE 754 binary
 * ops with NaN are always false (except `!='), so trichotomy and
 * commutativity fail, but FOP_COM depends on those (e.g. for index
 * builds).  So we define an order for consistent indexes; wtf
 * inconsistent results still possible since non-FOP_COM relops still
 * violate trichotomy/commutativty?  KNG 20071105
 */
#define TXDOUBLE_CMP(a, b)      (TXDOUBLE_IS_NaN(a) ?                   \
  (TXDOUBLE_IS_NaN(b) ? 0 : 1) : (TXDOUBLE_IS_NaN(b) ? -1 :             \
       (TXDOUBLE_ISGT(a, b) ? 1 : (TXDOUBLE_ISLT(a, b) ? -1 : 0))))

#define TXFLOAT_CMP(a, b)       (TXFLOAT_IS_NaN(a) ?                    \
  (TXFLOAT_IS_NaN(b) ? 0 : 1) : (TXFLOAT_IS_NaN(b) ? -1 :               \
       (TXFLOAT_ISGT(a, b) ? 1 : (TXFLOAT_ISLT(a, b) ? -1 : 0))))

/* ------------------------------------------------------------------------ */

#ifdef _WIN32
typedef DWORD   TXERRTYPE;
/* TX{get,set,clear}error() should be thread-safe and async-signal-safe: */
#  define TXgeterror()		GetLastError()
#  define TXseterror(err)	SetLastError(err)
#  define TXclearError()        (errno = 0, TXseterror(0))
char	*TXstrerror(int err);
#  define TXERR_EINTR           WSAEINTR
#  define TXERR_ECONNREFUSED    WSAECONNREFUSED
#  define TXERR_EWOULDBLOCK     WSAEWOULDBLOCK
#  define TX_PUSHERROR()        \
 { TXERRTYPE saveErr; int saveErrno; saveErr = TXgeterror(); saveErrno = errno
#  define TX_POPERROR()         errno = saveErrno; TXseterror(saveErr); }
#  define TX_ERROR_IS_NO_SUCH_FILE(n)   \
  ((n) == ERROR_FILE_NOT_FOUND ||       \
   (n) == ERROR_PATH_NOT_FOUND ||       \
   (n) == ERROR_INVALID_NAME ||         \
   (n) == ERROR_BAD_NETPATH ||          \
   (n) == ERROR_BAD_NET_NAME)
  /* Derived from dosmaperr.c mapping for EACCES in MSVC source;
   * added ERROR_SHARING_VIOLATION:
   */
#  define TX_ERROR_IS_PERM_DENIED(n)    \
  ((n) == ERROR_ACCESS_DENIED ||        \
   (n) == ERROR_CURRENT_DIRECTORY ||    \
   (n) == ERROR_LOCK_VIOLATION ||       \
   (n) == ERROR_NETWORK_ACCESS_DENIED ||\
   (n) == ERROR_CANNOT_MAKE ||          \
   (n) == ERROR_FAIL_I24 ||             \
   (n) == ERROR_DRIVE_LOCKED ||         \
   (n) == ERROR_SEEK_ON_DEVICE ||       \
   (n) == ERROR_NOT_LOCKED ||           \
   (n) == ERROR_LOCK_FAILED ||          \
   (n) == ERROR_SHARING_VIOLATION ||    \
   /* Bug 5781: stat()/open() of UNC share can give ERROR_LOGON_FAILURE */  \
   /* (e.g. if Windows tries to forward its credentials?) */                \
   (n) == ERROR_LOGON_FAILURE)
#  define TX_ERROR_IS_WIN_TOK(val, tok) ((val) == WSA##tok)
#  ifdef WSAEPIPE
#    define TX_ERROR_IS_WSAEPIPE(val)   ((val) == WSAEPIPE)
#  else /* !WSAEPIPE */
#    define TX_ERROR_IS_WSAEPIPE(val)   0
#  endif /* !WSAEPIPE */
#  define TXERR_INVALID_PARAMETER       ERROR_INVALID_PARAMETER
#else /* !_WIN32 */
typedef int     TXERRTYPE;
/* TX{get,set,clear}error() should be thread-safe and async-signal-safe: */
#  define TXgeterror()		errno
#  define TXseterror(err)	(errno = (err))
#  define TXclearError()        TXseterror(0)
#  define TXstrerror(err)	strerror(err)
#  define TXERR_EINTR           EINTR
#  define TXERR_ECONNREFUSED    ECONNREFUSED
#  define TXERR_EWOULDBLOCK     EWOULDBLOCK
#  define TX_PUSHERROR()        { TXERRTYPE saveErr; saveErr = TXgeterror()
#  define TX_POPERROR()         TXseterror(saveErr); }
#  define TX_ERROR_IS_NO_SUCH_FILE(n)   ((n) == ENOENT)
#  define TX_ERROR_IS_PERM_DENIED(n)    ((n) == EACCES)
#  define TX_ERROR_IS_WIN_TOK(val, tok) 0
#  define TX_ERROR_IS_WSAEPIPE(val)     0
#  define TXERR_INVALID_PARAMETER       EINVAL
#endif /* !_WIN32 */

#ifdef EPIPE
#  define TX_ERROR_IS_EPIPE(val)        \
  ((val) == EPIPE || TX_ERROR_IS_WSAEPIPE(val))
#else /* !EPIPE */
#  define TX_ERROR_IS_EPIPE(val)        TX_ERROR_IS_WSAEPIPE(val)
#endif /* !EPIPE */
#ifdef ECONNRESET
#  define TX_ERROR_IS_ECONNRESET(val)   \
  ((val) == ECONNRESET || TX_ERROR_IS_WIN_TOK((val), ECONNRESET))
#else /* !ECONNRESET */
#  define TX_ERROR_IS_ECONNRESET(val)   TX_ERROR_IS_WIN_TOK((val), ECONNRESET)
#endif /* !ECONNRESET */
#ifdef ENOTCONN
#  define TX_ERROR_IS_ENOTCONN(val)     \
  ((val) == ENOTCONN || TX_ERROR_IS_WIN_TOK((val), ENOTCONN))
#else /* !ENOTCONN */
#  define TX_ERROR_IS_ENOTCONN(val)     TX_ERROR_IS_WIN_TOK((val), ENOTCONN)
#endif /* !ENOTCONN */
#ifdef EBADF
#  define TX_ERROR_IS_EBADF(val)        \
  ((val) == EBADF || TX_ERROR_IS_WIN_TOK((val), EBADF))
#else /* !EBADF */
#  define TX_ERROR_IS_EBADF(val)        TX_ERROR_IS_WIN_TOK((val), EBADF)
#endif /* !EBADF */
#ifdef ECONNABORTED
#  define TX_ERROR_IS_ECONNABORTED(val) \
  ((val) == ECONNABORTED || TX_ERROR_IS_WIN_TOK((val), ECONNABORTED))
#else /* !ECONNABORTED */
#  define TX_ERROR_IS_ECONNABORTED(val) \
  TX_ERROR_IS_WIN_TOK((val), ECONNABORTED)
#endif /* !ECONNABORTED */

#define TX_ERROR_IS_CONNECTION_RESET(errnum)    \
  (TX_ERROR_IS_EPIPE(errnum) ||                 \
   TX_ERROR_IS_ECONNRESET(errnum) ||            \
   TX_ERROR_IS_ENOTCONN(errnum) ||              \
   TX_ERROR_IS_EBADF(errnum) ||                 \
   TX_ERROR_IS_ECONNABORTED(errnum))

#define TXERRTYPEPN   ((TXERRTYPE *)NULL)

/* ------------------------------------------------------------------------ */

#define TX_MIN(a, b)    ((a) < (b) ? (a) : (b))
#define TX_MAX(a, b)    ((a) > (b) ? (a) : (b))
/* note: TX_ABS(-MAXINT) is undefined; will result in -MAXINT: */
#define TX_ABS(a)       ((a) < 0 ? -(a) : (a))

/* TX_ARRAY_LEN(a) returns the length of array `a', in `a' elements.
 * The type of `a' *must* be a true array, not a (lengthless) pointer:
 */
#define TX_ARRAY_LEN(a) (sizeof(a)/sizeof((a)[0]))

typedef enum TXbool_tag
{
  /* false must be 0, to keep in line with C behavior: */
  TXbool_False  = 0,
  TXbool_True   = 1,
}
TXbool;

/* TX_OFFSET_OF(): byte offset into struct `type' of field `field': */
#define TX_OFFSET_OF(type, field)       \
  ((size_t)((char *)&(((type *)NULL)->field) - (char *)NULL))

/* TX_STRUCT_LEN_THRU(): byte length of struct `type' through and including
 * field `field':
 */
#define TX_STRUCT_LEN_THRU(type, field) \
  (TX_OFFSET_OF(type, field) + sizeof(((type *)NULL)->field))

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Compress or Content-/Transfer-Encoding handler flags.  Here instead of
 * texint.h or httpi.h to avoid compile dependency issues:
 */
typedef enum TXCTEHF_tag
{
  TXCTEHF_INPUT_EOF             = (1 << 0),     /* EOF on input */
  TXCTEHF_INPUT_TRUNCATED       = (1 << 1)      /* input is truncated */
}
TXCTEHF;
#define TXCTEHFPN       ((TXCTEHF *)NULL)

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Macros for managing a byte array of bits: */

/* Byte size of an array of `n' bits: */
#define TX_BIT_ARRAY_BYTE_SZ(n) (((n) + 7) >> 3)

/* Sets bit # `b' in byte `array'; 0 <= b < n: */
#define TX_BIT_ARRAY_SET_BIT(array, b)  \
  ((array)[(b) >> 3] |= (1 << ((b) & 0x7)))

/* Clears bit # `b' in byte `array'; 0 <= b < n: */
#define TX_BIT_ARRAY_CLR_BIT(array, b)  \
  ((array)[(b) >> 3] &= ~(1 << ((b) & 0x7)))

/* Returns 1 if bit # `b' is set in `array', 0 if not; 0 <= b < n: */
#define TX_BIT_ARRAY_GET_BIT(array, b)  \
  (((array)[(b) >> 3] & (1 << ((b) & 0x7))) ? 1 : 0)

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Locale-independent, consistent/predictable isalpha() etc. macros.
 * Just for ISO-8859-1 range, and only true (or transforms) ASCII,
 * so that inadvertent use on UTF-8 text will not harm the text:
 */
#define TX_CTYPE_UPPER_MASK     0x01
#define TX_CTYPE_LOWER_MASK     0x02
#define TX_CTYPE_DIGIT_MASK     0x04
#define TX_CTYPE_SPACE_MASK     0x08
#define TX_CTYPE_PUNCT_MASK     0x10
#define TX_CTYPE_XDIGIT_MASK    0x20

extern const unsigned char      TXctypeBits[256];
extern const unsigned char      TXctypeToupperMap[256];
extern const unsigned char      TXctypeTolowerMap[256];

#define TX_ISUPPER(c)   (TXctypeBits[(unsigned char)(c)] &       \
                         TX_CTYPE_UPPER_MASK)
#define TX_ISLOWER(c)   (TXctypeBits[(unsigned char)(c)] &       \
                         TX_CTYPE_LOWER_MASK)
#define TX_ISALPHA(c)   (TXctypeBits[(unsigned char)(c)] &       \
                         (TX_CTYPE_UPPER_MASK | TX_CTYPE_LOWER_MASK))
#define TX_ISDIGIT(c)   (TXctypeBits[(unsigned char)(c)] &       \
                         TX_CTYPE_DIGIT_MASK)
#define TX_ISXDIGIT(c)  (TXctypeBits[(unsigned char)(c)] &       \
                         TX_CTYPE_XDIGIT_MASK)
#define TX_ISALNUM(c)   (TXctypeBits[(unsigned char)(c)] &       \
          (TX_CTYPE_UPPER_MASK | TX_CTYPE_LOWER_MASK | TX_CTYPE_DIGIT_MASK))
#define TX_ISSPACE(c)   (TXctypeBits[(unsigned char)(c)] &      \
                         TX_CTYPE_SPACE_MASK)
#define TX_ISPUNCT(c)   (TXctypeBits[(unsigned char)(c)] &      \
                         TX_CTYPE_PUNCT_MASK)

#define TX_TOUPPER(c)   (TXctypeToupperMap[(unsigned char)(c)])
#define TX_TOLOWER(c)   (TXctypeTolowerMap[(unsigned char)(c)])

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Only define debug alloc routings if debug and/or requested,
 * to avoid accidental production use; they are expensive:
 */
#if defined(_DEBUG) && !defined(TX_ENABLE_MEM_PROTECT)
#  define TX_ENABLE_MEM_PROTECT 1
#endif

#ifdef TX_ENABLE_MEM_PROTECT

/* Aliases for some debug alloc functions in texis lib, to avoid link
 * issues; wtf:
 */

typedef enum TXMEMPROTECTPERM_tag
  {
    TXMEMPROTECTPERM_NONE       = 0,
    TXMEMPROTECTPERM_READ       = (1 << 0),
    TXMEMPROTECTPERM_WRITE      = (1 << 1),
    TXMEMPROTECTPERM_EXEC       = (1 << 2)
  }
TXMEMPROTECTPERM;

typedef enum TXMEMPROTECTFLAG_tag
  {
    TXMEMPROTECTFLAG_NO_DEAD_PAGE_ABOVE = (1 << 0),
    TXMEMPROTECTFLAG_NO_DEAD_PAGE_BELOW = (1 << 1),
    TXMEMPROTECTFLAG_ALIGN_BELOW        = (1 << 2),     /* default above */
    TXMEMPROTECTFLAG_BYTE_ALIGN         = (1 << 3)      /* default calloc */
  }
TXMEMPROTECTFLAG;

/* if TXALLOC... changes see also texint.h wtf: */
#  ifndef TXALLOC_PROTO
#    ifdef MEMDEBUG
#      define TXALLOC_PROTO_SOLE  CONST char *file, int line, CONST char *memo
#      define TXALLOC_PROTO             , TXALLOC_PROTO_SOLE
#      define TXALLOC_ARGS_DEFAULT      , __FILE__, __LINE__, CHARPN
#    else /* !MEMDEBUG */
#      define TXALLOC_PROTO_SOLE
#      define TXALLOC_PROTO
#      define TXALLOC_ARGS_DEFAULT
#    endif /* !MEMDEBUG */
#  endif /* !TXALLOC_PROTO */

#  ifdef MEMDEBUG
#    undef TXallocProtectableFunc
#    undef TXfreeProtectableFunc
#    undef TXprotectMemFunc
#  endif /* MEMDEBUG */

extern void  *(*TXallocProtectableFunc)(void *pmbuf, const char *fn,
                             size_t sz, TXMEMPROTECTFLAG flags TXALLOC_PROTO);
extern int   (*TXfreeProtectableFunc)(void *pmbuf, void *p TXALLOC_PROTO);
extern int   (*TXprotectMemFunc)(void *pmbuf, void *p, TXMEMPROTECTPERM perms
                                 TXALLOC_PROTO);

#  ifdef MEMDEBUG
#    define TXallocProtectableFunc(pmbuf, fn, sz, flags)        \
  TXallocProtectableFunc(pmbuf, fn, sz, flags TXALLOC_ARGS_DEFAULT)
#    define TXfreeProtectableFunc(pmbuf, p)     \
  TXfreeProtectableFunc(pmbuf, p TXALLOC_ARGS_DEFAULT)
#    define TXprotectMemFunc(pmbuf, p, flags)   \
  TXprotectMemFunc(pmbuf, p, flags TXALLOC_ARGS_DEFAULT)
#  endif /* MEMDEBUG */
#endif /* TX_ENABLE_MEM_PROTECT */

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !TXTYPES_H */
