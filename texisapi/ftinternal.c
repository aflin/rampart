#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "texint.h"
#include "fldops.h"

/* --------------------- Object-oriented FTN_INTERNAL --------------------- */

/* Info for FTN_INTERNAL, which is a "polymorphic" internal-only FTN object
 * (never to be stored externally e.g. directly in a table column).
 * Basically supports multiple object sub-types and copy-on-write.
 * Use a parallel macro list here to organize all sub-type info concisely.
 * Enums, structs etc. automatically initialized from this.
 * Iteration macro:  I(type)
 * Makes and uses these prototypes for management of each sub-type:
 *
 *   void *tx_fti_type_open(CONST char *usr, size_t sz);
 *      Alloc and return new object, with optional init data `usr/sz'
 *   void *tx_fti_type_close(void *obj);
 *      Close and free `obj'
 *   void *tx_fti_type_dup(void *obj);
 *      Duplicate `obj', i.e. create independent new object as copy of it
 *   CONST char *tx_fti_type_tostr(void *obj);
 *      Convert object to string; typically "[type object]"
 *      Must be const and valid for duration of object
 *
 * Parent ft_internal object maintains reference count for copy-on-write.
 * Any other sub-type object manipulation is custom to each object type.
 */

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Object-oriented fldmath type FTN_INTERNAL and its subtypes. */

#ifdef FT_INTERNAL_DEBUG
#  define DMSG(a)       putmsg a
#else /* !FT_INTERNAL_DEBUG */
#  define DMSG(a)
#endif /* !FT_INTERNAL_DEBUG */


/* String names of sub-types.  In ascending order by type name
 * because FTINTERNALSYMBOLS_LIST is:
 */
static CONST char * CONST       TxFtiNames[FTI_NUM] =
{
#undef I
#define I(type) #type,
  FTINTERNALSYMBOLS_LIST
#undef I
};

typedef struct FTIMETH_tag                      /* methods for a sub-type */
{
  void *(*open) ARGS((CONST char *usr, size_t sz));
  void *(*close) ARGS((void *obj));
  void *(*dup) ARGS((void *obj));
  CONST char *(*tostr) ARGS((void *obj));
}
FTIMETH;
#define FTIMETHPN       ((FTIMETH *)NULL)

static CONST FTIMETH    TxFtiMeth[FTI_NUM] =
{
#undef I
#define I(type)                 \
    {                           \
      tx_fti_##type##_open,     \
      tx_fti_##type##_close,    \
      tx_fti_##type##_dup,      \
      tx_fti_##type##_tostr     \
    },
  FTINTERNALSYMBOLS_LIST
#undef I
};

static CONST char       CannotAllocMem[] =
"Cannot alloc %lu bytes of memory: %s";
CONST char              TxCorruptFtiObj[] ="Corrupt ft_internal object 0x%lx";
static CONST char       NullFti[] = "NULL ft_internal object";

/* Extra sanity checks to help catch those who memcpy() directly (wrong): */
#define SANITY_CHECK(fti)                                       \
  if (!TX_FTI_VALID(fti))                                       \
    {                                                           \
      putmsg(MERR + UGE, fn, TxCorruptFtiObj, (long)(fti));     \
      goto err;                                                 \
    }


/* ---------------- General ft_internal routines for fldmath -------------- */

ft_internal *
tx_fti_open(type, usr, sz)
FTI             type;   /* (in) sub-type to create */
CONST char      *usr;   /* (in) (opt.) init data (nul-term.) */
size_t          sz;     /* (in) (opt.) size of `usr' */
/* Allocs a new FTN_INTERNAL object of `type' with init data `usr'.
 * Returns a pointer to alloc'd object, or NULL on error.
 * NOTE: caller must call tx_fti_prep4write() before modifying.
 * If `usr' is NULL, does not open an object (used by createfld to store
 * subtype without opening an object yet, and by XML API to indicate
 * zero items).
 */
{
  static CONST char     fn[] = "tx_fti_open";
  ft_internal_uber      *ftiu = ft_internal_uberPN;

  if ((unsigned)type >= (unsigned)FTI_NUM)
    {
      putmsg(MERR + MAE, fn, "Invalid FTN_INTERNAL subtype #%u",
             (unsigned)type);
      goto err;
    }
  /* Note that we alloc an ft_internal_uber object and return a pointer
   * to its ft_internal, after setting magic.  This helps detect incorrect
   * manual copy/free of ft_internal objects:
   */
  ftiu = (ft_internal_uber *)calloc(1, sizeof(ft_internal_uber));
  if (ftiu == ft_internal_uberPN)
    {
      putmsg(MERR + MAE, fn, CannotAllocMem,
             (long)sizeof(ft_internal_uber), TXstrerror(TXgeterror()));
      goto err;
    }
  ftiu->magic = FT_INTERNAL_MAGIC;
  ftiu->fti.type = type;
  ftiu->fti.refcnt = 1;                         /* currently sole user */
  /* rest cleared by calloc() */
  DMSG((999, CHARPN, "0x%lx refcnt = to %d (opened)", (long)&ftiu->fti,
        (int)ftiu->fti.refcnt));
  if (usr != CHARPN &&
      (ftiu->fti.obj = TxFtiMeth[ftiu->fti.type].open(usr, sz)) == NULL)
    goto err;
  goto done;

err:
  if (ftiu != ft_internal_uberPN) tx_fti_close(&ftiu->fti, -1);
  return(ft_internalPN);
done:
  return(&ftiu->fti);
}

ft_internal *
tx_fti_close(fti, n)
ft_internal     *fti;
size_t          n;
/* Closes `n' objects of `fti' list, freeing sub-object if ref count reaches 0.
 * Returns NULL.
 */
{
  static CONST char     fn[] = "tx_fti_close";
  ft_internal           *next, *prev;
  ft_internal_uber      *ftiu;

  if (n == (size_t)(-1)) n = (size_t)EPI_OS_SIZE_T_MAX;
  for (prev = ft_internalPN;
       fti != ft_internalPN && n > (size_t)0;
       prev = fti, n--, fti = next)
    {
      SANITY_CHECK(fti);
      next = fti->next;
      DMSG((999, CHARPN, "0x%lx refcnt-- to %d%s", (long)fti,
            (int)fti->refcnt - 1, (fti->refcnt - 1 <= 0 ? " (closed)" : "")));
      if (--fti->refcnt > 0) continue;          /* other users of this obj */
      if (fti->obj != NULL)
        fti->obj = TxFtiMeth[fti->type].close(fti->obj);
      fti->next = ft_internalPN;
      ftiu = (ft_internal_uber *)((char *)fti - TX_FTI_UBER_OFF);
      ftiu->magic = 0;
      free(ftiu);
      /* Detach from earlier item's linked list for sanity: */
      if (prev != ft_internalPN) prev->next = next;
      fti = prev;                               /* same `prev' next loop */
    }
err:
  return(ft_internalPN);
}

ft_internal *
tx_fti_copy4read(fti, n)
ft_internal     *fti;
size_t          n;
/* Copies `n' objects of `fti' for reading, ie. makes a "new" ft_internal
 * object list that shares underlying sub-objects.
 * NOTE: caller must call tx_fti_prep4write() before modifying.
 * Returns `fti' or a new object if ok, else NULL on error.
 * Both `fti' and return value should be closed when done.
 */
{
  static CONST char     fn[] = "tx_fti_copy4read";
  ft_internal           *ftiorg = fti;

  if (n == (size_t)(-1)) n = (size_t)EPI_OS_SIZE_T_MAX;
  for ( ; fti != ft_internalPN && n > (size_t)0; n--, fti = fti->next)
    {
      SANITY_CHECK(fti);
      DMSG((999, CHARPN, "0x%lx refcnt++ to %d", (long)fti,
            (int)fti->refcnt + 1));
      fti->refcnt++;                            /* caller is another user */
    }
  return(ftiorg);
err:
  return(ft_internalPN);
}

ft_internal *
tx_fti_prep4write(fti)
ft_internal     *fti;
/* Prepares `fti' for writing: underlying sub-object will be unique and may
 * be modified by caller (who is already an owner of `fti').  Only return
 * value should be closed when done; `fti' is closed here (even on error).
 * Returns `fti' or a new object if ok, else NULL on error.
 */
{
  static CONST char     fn[] = "tx_fti_prep4write";
  ft_internal_uber      *newftiu = ft_internal_uberPN;

  SANITY_CHECK(fti);
  if (fti->refcnt == 1) return(fti);            /* already sole user */

  newftiu = (ft_internal_uber *)calloc(1, sizeof(ft_internal_uber));
  if (newftiu == ft_internal_uberPN)
    {
      putmsg(MERR + MAE, fn, CannotAllocMem,
             (long)sizeof(ft_internal_uber), TXstrerror(TXgeterror()));
      goto err;
    }
  newftiu->magic = FT_INTERNAL_MAGIC;
  newftiu->fti.type = fti->type;
  newftiu->fti.refcnt = 1;                      /* currently sole user */
  /* rest cleared by calloc() */
  if (fti->obj != NULL)
    newftiu->fti.obj = TxFtiMeth[newftiu->fti.type].dup(fti->obj);
  if (newftiu->fti.obj == NULL) goto err;
  goto done;

err:
  if (newftiu != ft_internal_uberPN) tx_fti_close(&newftiu->fti, -1);
  return(ft_internalPN);
done:
  fti = tx_fti_close(fti, 1);                   /* always close old */
  return(&newftiu->fti);
}

int
tx_fti_append(fti, next)
ft_internal     *fti;   /* (in/out) item to append to */
ft_internal     *next;  /* (in) item to append (may be NULL to terminate) */
/* Appends `next' after `fti', which must be the last item already.
 * Caller must be a owner of `fti' (ie. already responsible for a refcnt inc).
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "tx_fti_append";

  if (tx_fti_getnext(fti) != next && fti->refcnt != 1)
    {
      /* Other owners of `fti' may be using the `fti->next' pointer.
       * WTF this limits the ability to mix-and-match sub-lists of the same
       * group of ft_internals amongst different owners, as there can be
       * multiple owners of an ft_internal but it can have only one next-item:
       */
      putmsg(MERR + UGE, fn,
             "Attempt to re-append to multi-owner ft_internal object");
      goto err;
    }
  fti->next = next;
  return(1);
err:
  return(0);
}

CONST char *
tx_fti_obj2str(fti)
ft_internal     *fti;
/* Converts `fti' to string, eg. "[type object]".
 * Return value is const and valid for life of object.
 * Will not return NULL.
 */
{
  static CONST char     fn[] = "tx_fti_obj2str";
  CONST char            *ret;

  if (fti == ft_internalPN)
    {
      putmsg(MERR + MAE, fn, NullFti);
      goto err;
    }
  SANITY_CHECK(fti);
  if (fti->obj == NULL) goto err;
  ret = TxFtiMeth[fti->type].tostr(fti->obj);
  if (ret == CHARPN) goto err;
  goto done;

err:
  ret = "?";                                    /* something non-NULL */
done:
  return(ret);
}

FTI
tx_fti_str2type(s)
CONST char      *s;     /* (in) string name of FTN_INTERNAL sub-type */
/* Returns FTI type for string name `s', or FTI_UNKNOWN if not known.
 */
{
  FTI   l, r, i;
  int   c;

  l = (FTI)0;
  r = FTI_NUM;
  while (l < r)                                 /* binary search */
    {
      i = ((l + r) >> 1);
      c = strcmpi(s, TxFtiNames[i]);
      if (c == 0) return(i);                    /* found it */
      if (c < 0) r = i;
      else l = i + 1;
    }
  return(FTI_UNKNOWN);
}

CONST char *
tx_fti_type2str(type)
FTI     type;
/* Returns string name of `type'.
 */
{
  if ((unsigned)type < (unsigned)FTI_NUM) return(TxFtiNames[type]);
  return("?");
}

/* ---------------------------- fldmath functions ------------------------- */

int
fochil(chf1, ilf2, f3, op)
FLD *chf1;      /* (in) char field */
FLD *ilf2;      /* (in) internal field */
FLD *f3;        /* (out) result field */
int op;         /* (in) FOP operator */
/* fldmath handler for (char,internal) pair.
 * Returns 0 if ok, else FOP_E... error code.
 */
{
  ft_int        rc;
  ft_internal   *fti;
  char          *s;
  CONST char    *v2;
  size_t        sl, n, i;

  switch (op)
    {
#if 0   /* KNG wtf migrate this to new OO routines someday? */
    case FOP_MM:
      mac_vsum();
      rc = metamorphop(f1, f2);
      fld2finv(f3, rc);
      return(0);
    case FOP_NMM:
    case FOP_RELEV:
    case FOP_PROXIM:
      return fld2finv(f3, 1);
    case FOP_MMIN:
      rc = TXlikein(f1, f2);
      fld2finv(f3, rc);
      return(0);
#endif /* 0 */
    case FOP_CNV:                               /* f3 = (ft_internal)chf1 */
      return(foilch(ilf2, chf1, f3, FOP_ASN));
    case FOP_ASN:                               /* f3 = (ft_char)ilf2 */
      if (TXfldIsNull(ilf2))
        return(TXfldmathReturnNull(chf1, f3));  /* Bug 5395 */
      TXmakesimfield(chf1, f3);
      fti = (ft_internal *)getfld(ilf2, NULL);
      v2 = tx_fti_obj2str(fti);
      s = strdup(v2);
      sl = strlen(s);
      setfldandsize(f3, s, sl + 1, FLD_FORCE_NORMAL);
      return(0);
    case FOP_MAT:                               /* f3 = (chf1 MATCHES ilf2) */
      s = (char *)getfld(chf1, NULL);
      fti = (ft_internal *)getfld(ilf2, &n);
      rc = 0;
      for (i = 0;
           i < n && fti != ft_internalPN;
           i++, fti = tx_fti_getnext(fti))
        {
          if (tx_fti_gettype(fti) != FTI_matches)
            return(FOP_EINVAL);                 /* some other sub-type */
          if (TXmatchesi(s, fti) != CHARPN)
            {                                   /* got a match */
              rc = 1;
              break;
            }
        }
      return(fld2finv(f3, rc));
    default:                                    /* unknown/unsupported op */
      break;
    }
  return(FOP_EINVAL);
}

int
foilch(ilf1, chf2, f3, op)
FLD *ilf1;      /* (in) internal field */
FLD *chf2;      /* (in) char field */
FLD *f3;        /* (out) result field */
int op;         /* (in) FOP operator */
/* fldmath handler for (internal,char) pair.
 * Returns 0 if ok, else FOP_E... error code.
 */
{
  ft_int                rc;
  ft_internal           *fti;
  FTI                   ftype;
  CONST char            *v2;
  size_t                sz;

  switch (op)
    {
    case FOP_CNV:                               /* f3 = (ft_char)ilf1 */
      return(fochil(chf2, ilf1, f3, FOP_ASN));
    case FOP_ASN:                               /* f3 = (ft_internal)chf2 */
      TXmakesimfield(ilf1, f3);
      /* wtf We need to look at ilf1's data in order to get its FTI subtype,
       * but it's likely that ilf1 has no data, eg. if this is an implicit
       * conversion by fldmath of FTN_CHAR data given for an FTN_INTERNAL
       * parameter.  In that case, set the data NULL, since we cannot know
       * the internal subtype.  (Would need to expand FLDFUNC struct to
       * include expected FTI_... subtype, so that fldmath can add it.)
       */
      fti = (ft_internal *)getfld(ilf1, NULL);  /* get ilf1's subtype */
      if (fti == ft_internalPN)
        setfldandsize(f3, NULL, 0, FLD_FORCE_NORMAL);
      else
        {
          ftype = tx_fti_gettype(fti);
          v2 = (char *)getfld(chf2, &sz);       /* get init data from chf2 */
          if ((fti = tx_fti_open(ftype, v2, sz)) == ft_internalPN)
            return(FOP_EINVAL);
          setfldandsize(f3, fti, FT_INTERNAL_ALLOC_SZ + 1, FLD_FORCE_NORMAL);
        }
      return(0);                                /* success */
    case FOP_MAT:                               /* f3 = (ilf1 MATCHES chf2) */
      rc = TXmatchesc(ilf1, chf2);
      return(fld2finv(f3, rc));
    default:                                    /* unknown/unsupported op */
      break;
    }
  return(FOP_EINVAL);
}

int
foslil(slf1, ilf2, f3, op)
FLD     *slf1;  /* (in) strlst field */
FLD     *ilf2;  /* (in) internal field */
FLD     *f3;    /* (out) result field */
int     op;     /* FOP operator */
/* fldmath handler for (strlst,internal) pair.
 * Returns 0 if ok, else FOP_E... error code.
 */
{
  static CONST char     fn[] = "foslil";
  ft_strlst             *vp1;
  ft_internal           *fti;
  size_t                n1, n2;
  char                  *cs;

  switch (op)
    {
    case FOP_MAT:                               /* f3 = (slf1 MATCHES ilf2) */
      vp1 = (ft_strlst *)getfld(slf1, &n1);
      fti = (ft_internal *)getfld(ilf2, &n2);
      if (!vp1)
        {
          putmsg(MERR, fn, "NULL field value");
          return(FOP_EINVAL);
        }
      if (!fti)                                 /* eg bad MATCHES expr */
        return(FOP_EINVAL);
      if (tx_fti_gettype(fti) != FTI_matches)
        return(FOP_EINVAL);
      for (cs = vp1->buf; *cs; cs += strlen(cs) + 1)
        {
          if (TXmatchesi(cs, fti))
            return(fld2finv(f3, 1));
        }
      return(fld2finv(f3, 0));
    default:                                    /* unknown/unsupported op */
      break;
    }
  return(FOP_EINVAL);
}

int
foilil(ilf1, ilf2, f3, op)
FLD     *ilf1;  /* (in) internal field */
FLD     *ilf2;  /* (in) internal field */
FLD     *f3;    /* (out) result field */
int     op;     /* FOP operator */
/* fldmath handler for (internal,internal) pair.
 * Supports NULL fields.
 * Returns 0 if ok, else FOP_E... error code.
 */
{
  ft_internal   *fti1, *fti2, *fti3;
  size_t        n1, n2, n3;

  switch (op)
    {
    case FOP_CNV:                               /* f3 = (ft_internal)ilf1 */
      return(foilil(ilf2, ilf1, f3, FOP_ASN));
    case FOP_ASN:                               /* f3 = (ft_internal)ilf2 */
      TXmakesimfield(ilf1, f3);                 /* copy ilf1->n, not subtype*/
      fti1 = (ft_internal *)getfld(ilf1, &n1);
      fti2 = (ft_internal *)getfld(ilf2, &n2);
      n3 = n1;                                  /* how many objs we need */
      /* If n2 < n3, this will give us a short array, but ...copy4read()
       * can deal with that, and we'll still store `n3' in `f3':
       */
      fti3 = tx_fti_copy4read(fti2, n3);
      setfldandsize(f3, fti3, FT_INTERNAL_ALLOC_SZ*n3 + 1, FLD_FORCE_NORMAL);
      return(0);
    default:                                    /* unknown/unsupported op */
      break;
    }
  return(FOP_EINVAL);
}
