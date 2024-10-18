#ifndef FLD_H
#define FLD_H

/************************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/************************************************************************/

/************************************************************************/

#define FREESHADOW	0xbdac  /* JMT Some non-accidental number for frees */
typedef struct db_field_struct FLD;
#define FLDPN ((FLD *)NULL)
#define FLDPPN ((FLD **)NULL)

#include "dd.h"

typedef struct tx_string
{
	char *v;
	size_t len;
} TX_String;

typedef enum TX_FLD_KIND
{
   TX_FLD_NORMAL,
   TX_FLD_VIRTUAL,
   TX_FLD_COMPUTED_JSON
} TX_FLD_KIND;

struct db_field_struct
{
 int    type;    /* the data type of this field */
 void  *v;       /* value pointer */
 /* JMT - fixed to determine who is drilling */
 void  *shadow;  /* MY storage area pointer */
 size_t n;       /* how many elements are in here */
 size_t size;    /* how big in bytes is it */
 size_t alloced; /* how much memory is alloced */
 size_t elsz;    /* size of a single element */
 int	frees;   /* Should closefld free shadow */
 TX_FLD_KIND kind; /* What kind of field is it */
 int	vfc;	 /* Count of virtual fields */
 FLD	**fldlist;/* List of virtual fields */
#ifndef NO_HAVE_DDBF
 FLD	*storage;
 FLD	*memory;
#endif
#ifndef NEVER
 int	wasmemo;
#endif
 int	issorted; /* When n>0 are the elements in order */
 struct fldsortstruct {
	FLD *allocedby;
 	int ptrsalloced;
 	int ptrsused;
	 union dataptrs {
	   TX_String *strings;
	 } dptrs;
 } dsc;
};

/*
 * VIRTUAL Field is a computed field by concatenating varchar representations of multiple fields
 * COMPUTED Field is any computed field that creates an alloced buf to return from getfld.
 *
 * Can use IS_COMPUTED to determine if safe to release memory after use.
 */
#define FLD_IS_VIRTUAL(f) (f->kind == TX_FLD_VIRTUAL)
#define FLD_IS_COMPUTED(f) ((f->kind == TX_FLD_VIRTUAL) || (f->kind == TX_FLD_COMPUTED_JSON))
#define FLD_IS_NULLABLE(f) (TXftnIsNullable(f->type))
#define FLD_FORCE_NORMAL TXbool_True
#define FLD_KEEP_KIND    TXbool_False

/* Can be called immediately after setfld/setfldandsize,
 * if value was not alloced:
 */
#define TXsetshadownonalloc(f)  ((f)->frees = 0)
/* would really like to deprecate this in favor of setfld...: */
#define TXsetshadowalloc(f)     ((f)->frees = FREESHADOW)

int     TXfldMoveFld(FLD *dest, FLD *src);
int     TXfldSetNull(FLD *fld);
int     TXfldIsNull(FLD *fld);

/* TX_FLD_NULL_CMP(ap, bp, cmp): like TXDOUBLE_CMP(*ap, *bp), but where
 * `ap'/`bp' may be NULL (SQL NULL); Bug 5395.  See TXDOUBLE_CMP()
 * comments for why this must be definitive and obey trichotomy etc.,
 * despite most NULL ops resulting in NULL and violating trichotomy:
 */
#define TX_FLD_NULL_CMP(ap, bp, cmp)    \
  (!(ap) ? (!(bp) ? 0 : 1) : (!(bp) ? -1 : (cmp)))

char    *TXfldGetNullOutputString(void);
int     TXfldSetNullOutputString(const char *s);

/************************************************************************/
#endif /* FLD_H */
/************************************************************************/
