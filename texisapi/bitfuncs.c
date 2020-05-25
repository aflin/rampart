#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbquery.h"
#include "bitfuncs.h"
#include "texint.h"


/* Bit-wise Texis SQL functions */


static CONST char       TxNoMem[] = "Out of memory";

/*
 * bitand(a, b)           a & b
 * bitor(a, b)            a | b
 * bitxor(a, b)           a ^ b
 *
 * bitnot(a)              ~a
 * bitsize(a)             sizeof(a)*8
 * bitcount(a)            number of 1 bits in a
 * bitmin(a)              lowest bit set to 1 in a (-1 if none)
 * bitmax(a)              highest bit set to 1 in a (-1 if none)
 * bitlist(a)             list of bits in a  return varint?
 *
 * bitshiftleft(a, n)     a << n
 * bitshiftright(a, n)    a >> n      treat as unsigned
 * bitrotateleft(a, n)    a << n      with rotation
 * bitrotateright(a, n)   a >> n      with rotation
 * bitset(a, n)           a | (1 << n)
 * bitclear(a, n)         a & ~(1 << n)
 * bitisset(a, n)         (a & (1 << n)) ? 1 : 0
 */

/* ------------------------------------------------------------------------ */

/* We could check type of bitfield arguments here, to detect a different
 * size int and return error instead of silently losing bits during cast.
 * But that would disallow literal ints as parameters, so let fldmath cast.
 * Set return type to `bittype' for consistency, and because fofuncret()
 * expects constant type.
 * sizeof(ft_int) == 32 always, in dbqueryi.h, and
 * sizeof(dword) == 32 always, in sizes.c:
 */

/* Return type is *var*int, for consistency with TXdbfldfuncs[] array,
 * i.e. so return type in SQL and in Vortex compiled SQL expressions
 * is the same: latter does not get TXdbfldfuncs[] return-type casting
 * in tup_project() as former does:
 */
#define RETURN_TYPE(type)       \
  (((type) & ~(DDTYPEBITS | DDVARBIT)) | (BITTYPE_FTN | DDVARBIT))

#define BINARYFUNC(func, op)                                            \
int                                                                     \
func(f1, f2)                                                            \
FLD     *f1;                                                            \
FLD     *f2;                                                            \
{                                                                       \
  bittype       *a, *b, *retData = NULL;                                \
  size_t        an, bn, i, retDataN;                                    \
  int           ret;                                                    \
                                                                        \
  if (f1 == FLDPN ||                                                    \
      f2 == FLDPN ||                                                    \
      (a = (bittype *)getfld(f1, &an)) == BITTYPEPN ||                  \
      (b = (bittype *)getfld(f2, &bn)) == BITTYPEPN)                    \
    return(FOP_EINVAL);                                                 \
  retDataN = TX_MAX(an, bn);                                            \
  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__, retDataN + 1,  \
                                sizeof(bittype));                       \
  if (!retData) goto err;                                               \
                                                                        \
  for (i = 0; i < retDataN; i++)                                        \
    retData[i] = ((i < an ? a[i] : 0) op (i < bn ? b[i] : 0));          \
                                                                        \
  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,          \
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN, \
                                    sizeof(bittype), retDataN, 0) ? FOP_EOK :\
         FOP_EUNKNOWN);                                                 \
  retData = NULL;                       /* owned by `f1' or freed */    \
  goto finally;                                                         \
                                                                        \
err:                                                                    \
  ret = FOP_EUNKNOWN;                                                   \
finally:                                                                \
  if (retData) retData = TXfree(retData);                               \
  return(ret);                                                          \
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

BINARYFUNC(txfunc_bitand, &)
BINARYFUNC(txfunc_bitor,  |)
BINARYFUNC(txfunc_bitxor, ^)

/* ------------------------------------------------------------------------ */

/* Start a unary func.  Sets `a'/`an' to first field's data/nels.
 * `s'/`e' are scratch:
 */
#define UNARYINIT()                                     \
  if (f1 == FLDPN ||                                    \
      (a = (bittype *)getfld(f1, &an)) == BITTYPEPN)    \
    return(FOP_EINVAL)

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int
txfunc_bitnot(f1)
FLD     *f1;
/* Computes ~f1.
 */
{
  bittype       *a, *s, *e, *retData = NULL, *d;
  size_t        an;
  int           ret;

  UNARYINIT();
  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__, an + 1,
                                sizeof(bittype));
  if (!retData) goto err;

  /* Set `retData': */
  for (s = a, e = a + an, d = retData; s < e; s++, d++) *d = ~*s;

  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN,
                                    sizeof(bittype), an, 0) ? FOP_EOK :
         FOP_EUNKNOWN);
  retData = NULL;                               /* owned by `f1' or freed */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  if (retData) retData = TXfree(retData);
  return(ret);
}

int
txfunc_bitsize(f1)
FLD     *f1;
/* Computes number of bits in field, e.g. max set elements.
 */
{
  bittype       *a, *retData = NULL;
  size_t        an;
  int           ret;

  UNARYINIT();
  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__, 2, sizeof(bittype));
  if (!retData) goto err;

  /* Set `retData': */
  *retData = (bittype)(an*BITTYPEBITS);

  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN,
                                    sizeof(bittype), 1, 0) ? FOP_EOK :
         FOP_EUNKNOWN);
  retData = NULL;                               /* owned by `f1' or freed */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  if (retData) retData = TXfree(retData);
  return(ret);
}

int
txfunc_bitcount(f1)
FLD     *f1;
/* Computes number of 1 bits in field, e.g. number of elements in set.
 */
{
  bittype       *a, *s, *e, *retData = NULL;
  size_t        an, n = 0, i;
  int           ret;

  UNARYINIT();
  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__, 2, sizeof(bittype));
  if (!retData) goto err;

  /* Set `retData': */
  for (s = a, e = a + an; s < e; s++)
    if (*s)                                     /* optimization */
      for (i = 0; i < BITTYPEBITS; i++)
        if (*s & ((bittype)1L << i)) n++;
  *retData = (bittype)n;

  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN,
                                    sizeof(bittype), 1, 0) ? FOP_EOK :
         FOP_EUNKNOWN);
  retData = NULL;                               /* owned by `f1' or freed */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  if (retData) retData = TXfree(retData);
  return(ret);
}

int
txfunc_bitmin(f1)
FLD     *f1;
/* Computes lowest bit number that is set in field, counting from 0.
 * -1 if none set.
 */
{
  bittype       *a, *s, *e, *retData = NULL;
  size_t        an;
  int           i, n = -1, ret;

  UNARYINIT();
  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__, 2, sizeof(bittype));
  if (!retData) goto err;

  /* Set `retData': */
  for (s = a, e = a + an; s < e; s++)
    {
      if (!*s) continue;                        /* no bits set this element */
      for (i = 0; i < BITTYPEBITS; i++)
        if (*s & ((bittype)1L << i))
          {
             n = (s - a)*BITTYPEBITS + i;
             goto gotit;
          }
    }
gotit:
  *retData = (bittype)n;

  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN,
                                    sizeof(bittype), 1, 0) ? FOP_EOK :
         FOP_EUNKNOWN);
  retData = NULL;                               /* owned by `f1' or freed */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  if (retData) retData = TXfree(retData);
  return(ret);
}

int
txfunc_bitmax(f1)
FLD     *f1;
/* Computes highest bit number that is set in field, counting from 0.
 * -1 if none set.
 */
{
  bittype       *a, *s, *e, *retData = NULL;
  size_t        an;
  int           i, n = -1, ret;

  UNARYINIT();
  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__, 2, sizeof(bittype));
  if (!retData) goto err;

  /* Set `retData': */
  for (s = a + an, e = a; s > e; )              /* optimization: backwards */
    {
      s--;
      if (!*s) continue;                        /* no bits set this element */
      for (i = BITTYPEBITS - 1; i >= 0; i--)
        if (*s & ((bittype)1L << i))
          {
             n = (s - a)*BITTYPEBITS + i;
             goto gotit;
          }
    }
gotit:
  *retData = (bittype)n;

  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN,
                                    sizeof(bittype), 1, 0) ? FOP_EOK :
         FOP_EUNKNOWN);
  retData = NULL;                               /* owned by `f1' or freed */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  if (retData) retData = TXfree(retData);
  return(ret);
}

int
txfunc_bitlist(f1)
FLD     *f1;
/* Computes list of bit numbers, in ascending order, that are 1.
 * -1 if none set, since we cannot return a 0-element field.
 * Will expand `f1' if short.
 */
{
  bittype       *a, *s, *e, *d, *retData = NULL;
  size_t        an;
  size_t        n = 0, i;
  int           ret;

  UNARYINIT();

  /* Compute `n': */
  for (s = a, e = a + an; s < e; s++)           /* count the 1 bits */
    if (*s)                                     /* optimization */
      for (i = 0; i < BITTYPEBITS; i++)
        if (*s & ((bittype)1L << i)) n++;

  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__, (n == 0 ? 1 : n) + 1,
                                sizeof(bittype));
  if (!retData) goto err;

  /* Set `retData': */
  if (n == 0)                                   /* no bits set */
    {
      n = 1;
      *retData = (bittype)(-1L);                /* special return value */
    }
  else                                          /* alloc and set return buf */
    {
      for (s = a, e = a + an, d = retData; s < e; s++)
        if (*s)                                 /* optimization */
          for (i = 0; i < BITTYPEBITS; i++)
            if (*s & ((bittype)1L << i))
              *(d++) = (bittype)((s - a)*BITTYPEBITS + i);
    }

  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN,
                                    sizeof(bittype), n, 0) ? FOP_EOK :
         FOP_EUNKNOWN);
  retData = NULL;                               /* owned by `f1' or freed */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  if (retData) retData = TXfree(retData);
  return(ret);
}

/* ------------------------------------------------------------------------ */

/* Start a (set, n) func.  Sets `a'/`an' to first field's data/nels,
 * `b'/`bn' to second field's data/nels.  `s'/`e' are scratch.
 * NOTE: `f2' type checked by fldmath via TXdbfldfuncs[] param type;
 * autocast is ok since it's used as a single (relatively small) integer:
 */
#define FUNCINIT()                                      \
  if (f1 == FLDPN ||                                    \
      f2 == FLDPN ||                                    \
      (a = (bittype *)getfld(f1, &an)) == BITTYPEPN ||  \
      (b = (bittype *)getfld(f2, &bn)) == BITTYPEPN)    \
    return(FOP_EINVAL)

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int
txfunc_bitshiftleft(f1, f2)
FLD     *f1;
FLD     *f2;
/* Shifts elements of `f1' `f2' bits to the left, padding 0s on the right
 * and truncating on the left.  If `f2' is negative, shifts right instead.
 */
{
  bittype       *a, *b, *s, *d, n, l, *retData = NULL;
  size_t        an, bn;
  int           m, ret;
  FLD           tmpfld;

  FUNCINIT();
  n = (bn > 0 ? *b : 0);                        /* total bits to shift */
  if (n < 0)                                    /* shift right */
    {
      n = -n;
      tmpfld = *f2;
      tmpfld.v = (void *)&n;
      tmpfld.n = 1;
      ret = txfunc_bitshiftright(f1, &tmpfld);
      goto finally;
    }

  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__, an + 1,
                                sizeof(bittype));
  if (!retData) goto err;
  l = n / (bittype)BITTYPEBITS;                 /* # bittypes to shift */
  n %= (bittype)BITTYPEBITS;                    /* # bits to shift */
  m = BITTYPEBITS - (int)n;                     /* # bits carryover */
  if (l > (bittype)0L)                          /* need to shift bittypes */
    {
      s = a + (an - 1) - l;
      d = retData + (an - 1);
      if (l < (bittype)an)                      /* shift `an' max */
        for ( ; s >= a; s--, d--)
          *d = *s;
      /* remainder (new lower bittypes) cleared by calloc() */
    }
  else
    memcpy(retData, a, an*sizeof(bittype));
  if ((int)n > 0)                               /* need to shift bits */
    {
      for (d = retData + an - 1; d > retData; d--)
        *d = (*d << (int)n) | ((ubittype)(d[-1]) >> m);
      if (d >= retData) *d <<= (int)n;
    }

  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN,
                                    sizeof(bittype), an, 0) ? FOP_EOK :
         FOP_EUNKNOWN);
  retData = NULL;                               /* owned by `f1' or freed */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  if (retData) retData = TXfree(retData);
  return(ret);
}

int
txfunc_bitshiftright(f1, f2)
FLD     *f1;
FLD     *f2;
/* Shifts elements of `f1' `f2' bits to the right, padding 0s on the left
 * and truncating on the right.  If `f2' is negative, shifts left instead.
 */
{
  bittype       *a, *b, *s, *d, n, l, *retData = NULL, *e;
  size_t        an, bn;
  int           m, ret;
  FLD           tmpfld;

  FUNCINIT();
  n = (bn > 0 ? *b : 0);                        /* total bits to shift */
  if (n < 0)                                    /* shift left */
    {
      n = -n;
      tmpfld = *f2;
      tmpfld.v = (void *)&n;
      tmpfld.n = 1;
      ret = txfunc_bitshiftleft(f1, &tmpfld);
      goto finally;
    }

  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__, an + 1,
                                sizeof(bittype));
  if (!retData) goto err;
  l = n / (bittype)BITTYPEBITS;                 /* # bittypes to shift */
  n %= (bittype)BITTYPEBITS;                    /* # bits to shift */
  m = BITTYPEBITS - (int)n;                     /* # bits carryover */
  if (l > (bittype)0L)                          /* need to shift bittypes */
    {
      s = a + l;
      d = retData;
      if (l < (bittype)an)                      /* shift `an' max */
        for (e = a + (bittype)an; s < e; s++, d++)
          *d = *s;
      /* remainder (new upper bittypes) cleared by calloc() */
    }
  else
    memcpy(retData, a, an*sizeof(bittype));
  if ((int)n > 0)                               /* need to shift bits */
    {
      for (d = retData, e = retData + an - 1; d < e; d++)
        *d = ((ubittype)(*d) >> (int)n) | (d[1] << m);
      if (d <= e) *d = ((ubittype)(*d) >> (int)n);
    }

  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN,
                                    sizeof(bittype), an, 0) ? FOP_EOK :
         FOP_EUNKNOWN);
  retData = NULL;                               /* owned by `f1' or freed */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  if (retData) retData = TXfree(retData);
  return(ret);
}

int
txfunc_bitrotateleft(f1, f2)
FLD     *f1;
FLD     *f2;
/* Rotates elements of `f1' `f2' bits to the left, with wraparound to right.
 * If `f2' is negative, rotates right instead.
 */
{
  bittype       *a, *b, *d, wrap, n, l, *retData = NULL;
  size_t        an, bn, i;
  int           m, ret;
  FLD           tmpfld;

  FUNCINIT();
  n = (bn > 0 ? *b : 0);                        /* total bits to rotate */
  if (n < (bittype)0L)                          /* rotate right */
    {
      n = -n;
      tmpfld = *f2;
      tmpfld.v = (void *)&n;
      tmpfld.n = 1;
      ret = txfunc_bitrotateright(f1, &tmpfld);
      goto finally;
    }

  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__, an + 1,
                                sizeof(bittype));
  if (!retData) goto err;

  l = n / (bittype)BITTYPEBITS;                 /* # bittypes to rotate */
  if (an > 0)
    l %= (bittype)an;                           /* rotate once max */
  else
    l = 0;
  n %= (bittype)BITTYPEBITS;                    /* # bits to rotate */
  m = BITTYPEBITS - (int)n;                     /* # bits carryover */
  if (l > (bittype)0L)                          /* need to rotate bittypes */
    {
      for (i = 0; i < (size_t)l; i++)
        retData[i] = a[i + an - l];
      for ( ; i < (size_t)an; i++)
        retData[i] = a[i - l];
    }
  else
    memcpy(retData, a, an*sizeof(bittype));
  if ((int)n > 0 && an > 0)                     /* need to rotate bits */
    {
      d = retData + an - 1;
      wrap = ((ubittype)(*d) >> m);
      for ( ; d > retData; d--)                 /* walk backwards */
        *d = (*d << (int)n) | ((ubittype)(d[-1]) >> m);
      *d = (*d << (int)n) | wrap;
    }

  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN,
                                    sizeof(bittype), an, 0) ? FOP_EOK :
         FOP_EUNKNOWN);
  retData = NULL;                               /* owned by `f1' or freed */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  if (retData) retData = TXfree(retData);
  return(ret);
}

int
txfunc_bitrotateright(f1, f2)
FLD     *f1;
FLD     *f2;
/* Rotates elements of `f1' `f2' bits to the right, with wraparound to left.
 * If `f2' is negative, rotates left instead.
 */
{
  bittype       *a, *b, *d, *e, wrap, n, l, *retData = NULL;
  size_t        an, bn, i;
  int           m, ret;
  FLD           tmpfld;

  FUNCINIT();
  n = (bn > 0 ? *b : 0);                        /* total bits to rotate */
  if (n < (bittype)0L)                          /* rotate left */
    {
      n = -n;
      tmpfld = *f2;
      tmpfld.v = (void *)&n;
      tmpfld.n = 1;
      ret = txfunc_bitrotateleft(f1, &tmpfld);
      goto finally;
    }

  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__, an + 1,
                                sizeof(bittype));
  if (!retData) goto err;

  l = n / (bittype)BITTYPEBITS;                 /* # bittypes to rotate */
  if (an > 0)
    l %= (bittype)an;                           /* rotate once max */
  else
    l = 0;
  n %= (bittype)BITTYPEBITS;                    /* # bits to rotate */
  m = BITTYPEBITS - (int)n;                     /* # bits carryover */
  if (l > (bittype)0L)                          /* need to rotate bittypes */
    {
      for (i = 0; i < (size_t)(an - l); i++)
        retData[i] = a[i + l];
      for ( ; i < (size_t)an; i++)
        retData[i] = a[i + l - an];
    }
  else
    memcpy(retData, a, an*sizeof(bittype));
  if ((int)n > 0 && an > 0)                     /* need to rotate bits */
    {
      wrap = (*retData << m);
      for (d = retData, e = retData + an - 1; d < e; d++)
        *d = ((ubittype)(*d) >> n) | (d[1] << m);
      *d = ((ubittype)(*d) >> n) | wrap;
    }

  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN,
                                    sizeof(bittype), an, 0) ? FOP_EOK :
         FOP_EUNKNOWN);
  retData = NULL;                               /* owned by `f1' or freed */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  if (retData) retData = TXfree(retData);
  return(ret);
}

int
txfunc_bitset(f1, f2)
FLD     *f1;
FLD     *f2;
/* Sets bit number `f2' of `f1' to 1.
 * Will expand `f1' if short.
 */
{
  bittype       *a, *b, *retData = NULL;
  size_t        an, bn, retDataN = 0;
  int           n, m, ret;

  FUNCINIT();
  n = (int)(bn > 0 ? *b : 0);
  if (n < 0)                                    /* no negative bit numbers */
    {
      n = -1;
      m = 0;
    }
  else
    {
      m = (n % BITTYPEBITS);                    /* bit # */
      n /= BITTYPEBITS;                         /* bittype # */
    }

  retDataN = TX_MAX(an, (size_t)(n + 1));
  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__,
                                retDataN + 1, sizeof(bittype));
  if (!retData) goto err;

  if (an > 0) memcpy(retData, a, an*sizeof(bittype));
  /* rest cleared by calloc() */
  if (n >= 0) retData[n] |= ((bittype)1L << m);

  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN,
                                    sizeof(bittype), retDataN, 0) ? FOP_EOK :
         FOP_EUNKNOWN);
  retData = NULL;                               /* owned by `f1' or freed */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  if (retData) retData = TXfree(retData);
  return(ret);
}

int
txfunc_bitclear(f1, f2)
FLD     *f1;
FLD     *f2;
/* Clears bit number `f2' of `f1' to 0.
 * Will expand `f1' if short.
 */
{
  bittype       *a, *b, *retData = NULL;
  size_t        an, bn, retDataN = 0;
  int           n, m, ret;

  FUNCINIT();
  n = (int)(bn > 0 ? *b : 0);
  if (n < 0)                                    /* no negative bit numbers */
    {
      n = -1;
      m = 0;
    }
  else
    {
      m = (n % BITTYPEBITS);                    /* bit # */
      n /= BITTYPEBITS;                         /* bittype # */
    }

  retDataN = TX_MAX(an, (size_t)(n + 1));
  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__,
                                retDataN + 1, sizeof(bittype));
  if (!retData) goto err;

  if (an > 0) memcpy(retData, a, an*sizeof(bittype));
  /* rest cleared by calloc() */
  if (n >= 0) retData[n] &= ~((bittype)1L << m);

  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN,
                                    sizeof(bittype), retDataN, 0) ? FOP_EOK :
         FOP_EUNKNOWN);
  retData = NULL;                               /* owned by `f1' or freed */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  if (retData) retData = TXfree(retData);
  return(ret);
}

int
txfunc_bitisset(f1, f2)
FLD     *f1;
FLD     *f2;
/* Computes 1 if bit number `f2' of `f1' is 1, 0 if not.
 */
{
  bittype       *a, *b, *retData = NULL;
  size_t        an, bn;
  int           n, m, ret;

  FUNCINIT();
  n = (int)(bn > 0 ? *b : 0);

  retData = (bittype *)TXcalloc(TXPMBUFPN, __FUNCTION__, 2, sizeof(bittype));
  if (!retData) goto err;

  if (n >= 0)
    {
      m = (n % BITTYPEBITS);                    /* bit # */
      n /= BITTYPEBITS;                         /* bittype # */
      if ((size_t)n < an && (a[n] & ((bittype)1L << m)))
        *retData = (bittype)1L;
      else
        *retData = (bittype)0L;
    }
  else
    *retData = (bittype)0L;

  ret = (TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData,
                                    RETURN_TYPE(f1->type), FTI_UNKNOWN,
                                    sizeof(bittype), 1, 0) ? FOP_EOK :
         FOP_EUNKNOWN);
  retData = NULL;                               /* owned by `f1' or freed */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  if (retData) retData = TXfree(retData);
  return(ret);
}
