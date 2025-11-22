#ifndef DD_H
#define DD_H


/************************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/************************************************************************/

#define DDFIELDS 50                     /* how many fields can there be */
#ifndef MAKE_TESTING_A_BITCH_FOR_JOHN_ON_IRIS
#define DDNAMESZ 35                 /* what is the maximium name length */
#else
#define DDNAMESZ 32                 /* what is the maximium name length */
#endif
#define DDVARBIT  64                    /* is data type variable length */
#define DDTYPES   256                       /* max number of data types */
#define DDTYPEBITS (0xff-FTN_NotNullableFlag-DDVARBIT) /* actual type bits */

#ifndef byte
#define byte unsigned char
#endif

#ifndef dword
#define dword unsigned long
#endif

#ifndef ulong
#define ulong unsigned long
#endif

#ifdef WITH_BLOBZ
#  define TX_FTN_SYMBOL_BLOBZ   \
I(BLOBZ,        "blobz",     sizeof(ft_blobz),     0,          0)                                  /* 29 */
#else /* !WITH_BLOBZ */
#  define TX_FTN_SYMBOL_BLOBZ   \
I(BLOBZ,        "",          0,                    0,          0)                                  /* 29 */
#endif /* !WITH_BLOBZ */

/* NOTE: FTN enum values must remain constant: stored externally in tables: */
/* I(tok, name, size, nextLargerType, flags)
 * `nextLargerType' is next larger, same-signedness, same-integralness type
 */
#define TX_FTN_SYMBOLS_LIST                                                                                 \
I(BYTE,         "byte",      sizeof(ft_byte),      0,          0)                                  /*  1 */ \
I(CHAR,         "char",      sizeof(ft_char),      0,          0)                                  /*  2 */ \
I(DECIMAL,      "",          0,                    0,          0)                                  /*  3 */ \
I(DOUBLE,       "double",    sizeof(ft_double),    0,          TXFTNF_FLOATINGPOINT|TXFTNF_SIGNED) /*  4 */ \
I(DATE,         "date",      sizeof(ft_date),      0,          0)                                  /*  5 */ \
I(FLOAT,        "float",     sizeof(ft_float),     FTN_DOUBLE, TXFTNF_FLOATINGPOINT|TXFTNF_SIGNED) /*  6 */ \
I(INT,          "int",       sizeof(ft_int),       FTN_LONG,   TXFTNF_INTEGRAL|TXFTNF_SIGNED)      /*  7 */ \
I(INTEGER,      "integer",   sizeof(ft_integer),   FTN_LONG,   TXFTNF_INTEGRAL|TXFTNF_SIGNED)      /*  8 */ \
I(LONG,         "long",      sizeof(ft_long),      FTN_INT64,  TXFTNF_INTEGRAL|TXFTNF_SIGNED)      /*  9 */ \
I(SHORT,        "short",     sizeof(ft_short),     FTN_INT,    TXFTNF_INTEGRAL|TXFTNF_SIGNED)      /* 10 */ \
I(SMALLINT,     "smallint",  sizeof(ft_smallint),  FTN_INT,    TXFTNF_INTEGRAL|TXFTNF_SIGNED)      /* 11 */ \
I(unknownType12,"",          0,                    0,          0)                                  /* 12 */ \
I(WORD,         "word",      sizeof(ft_word),      FTN_DWORD,  TXFTNF_INTEGRAL)                    /* 13 */ \
I(BLOB,         "blob",      sizeof(ft_blob),      0,          0)                                  /* 14 */ \
I(HANDLE,       "off_t",     sizeof(ft_handle),    0,          0)                                  /* 15 */ \
I(INDIRECT,     "ind",       sizeof(ft_indirect),  0,          0)                                  /* 16 */ \
I(DWORD,        "dword",     sizeof(ft_dword),     FTN_UINT64, TXFTNF_INTEGRAL)                    /* 17 */ \
I(BLOBI,        "blobi",     sizeof(ft_blobi),     0,          0)                                  /* 18 */ \
I(COUNTER,      "counter",   sizeof(ft_counter),   0,          0)                                  /* 19 */ \
I(STRLST,       "strlst",    TX_STRLST_ELSZ,       0,          0)                                  /* 20 */ \
I(DATESTAMP,    "datestamp", sizeof(ft_datestamp), 0,          0)                                  /* 21 */ \
I(TIMESTAMP,    "timestamp", sizeof(ft_timestamp), 0,          0)                                  /* 22 */ \
I(DATETIME,     "datetime",  sizeof(ft_datetime),  0,          0)                                  /* 23 */ \
I(COUNTERI,     "counteri",  sizeof(ft_counteri),  0,          0)                                  /* 24 */ \
I(RECID,        "recid",     sizeof(ft_recid),     0,          0)                                  /* 25 */ \
I(INTERNAL,     "internal",  FT_INTERNAL_ALLOC_SZ, 0,          0)                                  /* 26 */ \
I(INT64,        "int64",     sizeof(ft_int64),     0,          TXFTNF_INTEGRAL|TXFTNF_SIGNED)      /* 27 */ \
I(UINT64,       "uint64",    sizeof(ft_uint64),    0,          TXFTNF_INTEGRAL)                    /* 28 */ \
TX_FTN_SYMBOL_BLOBZ                                                                                /* 29 */ \
/* vector types here - ajf 2025-11-17 */                                                                    \
I(VEC_F64,      "vecF64",    sizeof(ft_double),    0,          0)                                  /* 30 */ \
I(VEC_F32,      "vecF32",    sizeof(ft_float),     0,          0)                                  /* 31 */ \
I(VEC_F16,      "vecF16",    sizeof(uint16_t),     0,          0)                                  /* 32 */ \
I(VEC_BF16,     "vecBf16",   sizeof(uint16_t),     0,          0)                                  /* 33 */ \
I(VEC_I8,       "vecI8",     sizeof(ft_byte),      0,          0)                                  /* 34 */ \
I(VEC_U8,       "vecU8",     sizeof(ft_byte),      0,          0)                                  /* 35 */

#define FTN_VEC_START 30
#define FTN_VEC_END 35

typedef enum FTN_tag
{
  FTN_unknownType0,                             /* placeholder; makes byte=1*/
#undef I
#define I(tok, name, size, nextLargerType, flags)       FTN_##tok,
TX_FTN_SYMBOLS_LIST
#undef I
  /* end */
  FTN_NUM,                                      /* must be after all types */
  FTN_LAST      = FTN_NUM - 1,                  /* the last defined type */

  /* These values are for easy FTN type recognition in a debugger: */
  FTN_varUnknownType0   = DDVARBIT,
#undef I
#define I(tok, name, size, nextLargerType, flags)       FTN_var##tok,
TX_FTN_SYMBOLS_LIST
#undef I

  FTN_NotNullableFlag   = 0x80, /* iff set: field *not* allowed to be NULL */
#undef I
#define I(tok, name, size, nextLargerType, flags)       FTN_NotNullable##tok,
TX_FTN_SYMBOLS_LIST
#undef I

  FTN_NotNullableVarUnknownType0 = (FTN_NotNullableFlag | DDVARBIT),
#undef I
#define I(tok, name, size, nextLargerType, flags)   FTN_NotNullableVar##tok,
TX_FTN_SYMBOLS_LIST
#undef I
  FTN_VarBaseTypeMask   = (DDVARBIT | DDTYPEBITS)
}
FTN;
#define FTNPN   ((FTN *)NULL)

typedef enum TXFTNF_tag                         /* FTN flags */
  {
    TXFTNF_INTEGRAL     = 0x01,                 /* is integer type */
    TXFTNF_FLOATINGPOINT= 0x02,                 /* is floating-point type */
    TXFTNF_SIGNED       = 0x04,                 /* is signed type */
  }
TXFTNF;
#define TXFTNFPN        ((TXFTNF *)NULL)

extern const TXFTNF     TXftnFlags[DDTYPEBITS + 1];

#define TXftnIsIntegral(type)   \
  (TXftnFlags[((unsigned)(type)) & DDTYPEBITS] & TXFTNF_INTEGRAL)
#define TXftnIsFloatingPoint(type)   \
  (TXftnFlags[((unsigned)(type)) & DDTYPEBITS] & TXFTNF_FLOATINGPOINT)
#define TXftnIsNumeric(type)   \
  (TXftnFlags[((unsigned)(type)) & DDTYPEBITS] & (TXFTNF_INTEGRAL | TXFTNF_FLOATINGPOINT))
#define TXftnIsSigned(type)     \
  (TXftnFlags[((unsigned)(type)) & DDTYPEBITS] & TXFTNF_SIGNED)

#define TXftnGetFlags(type)     (TXftnFlags[((unsigned)(type)) & DDTYPEBITS])

extern const FTN        TXftnNextLargerType[DDTYPEBITS + 1];
#define TXftnGetNextLargerType(type)    \
  (TXftnNextLargerType[((unsigned)(type)) & DDTYPEBITS])

typedef byte    ft_byte;
typedef char    ft_char;
typedef byte    ft_decimal;                     /* not supported */
typedef double  ft_double;
typedef dword   ft_dword;
typedef time_t  ft_date;
typedef float   ft_float;
/* KNG SQL spec says `int' (or `integer'?) must be 32-bit: */
#if EPI_OS_INT_BITS == 32
typedef int     ft_int;
#elif EPI_OS_LONG_BITS == 32
typedef long    ft_int;
#else
  error error error Need 32-bit integral type for ft_int;
#endif
typedef ft_int          ft_integer;
typedef long            ft_long;
typedef short           ft_short;
typedef short           ft_smallint;
typedef EPI_UINT16      ft_word;
typedef size_t          ft_pointer;
typedef EPI_OFF_T       ft_blob;
typedef EPI_OFF_T       ft_blobz;
typedef EPI_OFF_T       ft_handle;
#define TX_FT_HANDLE_BITS       EPI_OFF_T_BITS
typedef char            ft_indirect;

/* First byte of blobz data determines format and encoding.
 * NOTE: these values must remain constant, as they are stored in .blb file:
 */
typedef enum TX_BLOBZ_TYPE_tag
{
	TX_BLOBZ_TYPE_ASIS	= ' ',		/* as-is, no VSH org size */
	TX_BLOBZ_TYPE_GZIP	= 'G',		/* VSH org size + gzip */
	TX_BLOBZ_TYPE_EXTERNAL	= 'E'		/* VSH+... Compress Exe data */
}
TX_BLOBZ_TYPE;

#define TX_GZIP_START_BYTE	0x1f		/* native gzip start byte */

/* Max size of a blobz header: */
#define TX_BLOBZ_MAX_HDR_SZ	(1 + VSH_MAXLEN)

/* Internal (mem) version of a blob; should never be externally stored
 * (but see Bug 4390):
 */
typedef struct ft_blobi_tag {
	EPI_OFF_T	off;	   /* Where */
#ifdef DBFPN
	DBF		*dbf;	   /* Data file */
#else
	void		*dbf;	   /* Data file */
#endif
	size_t		len;	   /* Length of data (not including nul) */
	FTN		otype;	   /* Original Type of Data (blob or blobz) */
	char		*memdata;  /* In memory Data (nul-terminated) */
	int		ndfree;    /* Do I own memdata, and need to free */
} ft_blobi;

/* ------------------------------------------------------------------------ */

/* Bug 3895: sizeof(time_t) > sizeof(long) sometimes, e.g. on win64.
 * But legacy ft_counter.date field is long, and is stored in .tbl
 * files etc., so we cannot change existing platforms' types.  At this
 * time (20130409) win64 is a new platform so we can change its
 * ft_counter definition, and no other (pre-existing) platform has
 * time_t > long, so we can define a type that is long iff long >=
 * time_t, time_t otherwise.  Also define an equally-large unsigned
 * type for ft_counter.seq, so we do not waste struct space when
 * time_t > long (and because TXparseHexCounter() assumes equal-sized
 * date and seq fields, at least for 32-bit):
 */
#if EPI_OS_LONG_BITS >= EPI_OS_TIME_T_BITS
/* `long' large enough, and pre-existing platforms: */
typedef long TXft_counterDate;
#  define TX_FT_COUNTER_DATE_BITS	EPI_OS_LONG_BITS
typedef ulong TXft_counterSeq;
#  define TX_FT_COUNTER_SEQ_BITS	EPI_OS_ULONG_BITS
#else /* EPI_OS_LONG_BITS < EPI_OS_TIME_T_BITS */
/* The double macro CAT/CAT_INTERNAL trickery is needed so that we
 * can concatenate the *value* of EPI_OS_TIME_T_BITS to EPI_[U]INT:
 */
#  define CAT_INTERNAL(x, y) x##y
#  define CAT(x, y) CAT_INTERNAL(x, y)
#  ifdef EPI_OS_TIME_T_IS_SIGNED
typedef time_t TXft_counterDate;
#  else /* !EPI_OS_TIME_T_IS_SIGNED */
/* ft_counter.date historical type `long' is signed, so pick a signed type: */
typedef CAT(EPI_INT, EPI_OS_TIME_T_BITS) TXft_counterDate;
#  endif /* !EPI_OS_TIME_T_IS_SIGNED */
#  define TX_FT_COUNTER_DATE_BITS	EPI_OS_TIME_T_BITS
typedef CAT(EPI_UINT, EPI_OS_TIME_T_BITS) TXft_counterSeq;
#  define TX_FT_COUNTER_SEQ_BITS	EPI_OS_TIME_T_BITS
#  undef CAT
#  undef CAT_INTERNAL
#endif /* EPI_OS_LONG_BITS < EPI_OS_TIME_T_BITS */

typedef struct ft_counter {
	TXft_counterDate	date;
	TXft_counterSeq		seq;
} ft_counter, ft_counteri;

#define TX_CTRCMP(a, b)                 \
  ((a)->date > (b)->date ? 1 :          \
   ((a)->date < (b)->date ? -1 :        \
    ((a)->seq > (b)->seq ? 1 :          \
     ((a)->seq < (b)->seq ? -1 : 0))))

/* `r' is a temp int: */
#define CTRCMP(a, b, r) { (r) = TX_CTRCMP((a), (b)); }

#define TX_ZERO_COUNTER(co)     \
  ((co)->date = (TXft_counterDate)0, (co)->seq = (TXft_counterSeq)0)
#define TX_COUNTER_IS_ZERO(co)  \
  ((co)->date == (TXft_counterDate)0 && (co)->seq == (TXft_counterSeq)0)

/* ------------------------------------------------------------------------ */

/* Format of ft_strlst.buf buffer is:
 *   [string nul [string nul] ...] nul
 * i.e. nul-terminated payload strings followed by empty terminating string.
 * `nb' count includes nuls for payload strings, plus the term-string nul.
 */
typedef struct ft_strlst {
        size_t  nb;                   /* total number of bytes in buf */
        char    delim;                     /* delimiter used on input */
        char    buf[1];/* string list buffer */
} ft_strlst;
#define ft_strlstPN     ((ft_strlst *)NULL)
/* Absolute minimum size of a strlst (i.e. just header): */
#define TX_STRLST_MINSZ       (ft_strlstPN->buf - (char *)ft_strlstPN)
/* FLD.elsz for ft_strlst, which is not sizeof(ft_strlst): */
#define TX_STRLST_ELSZ  sizeof(byte)

#ifndef SWORD
#define	SWORD	short int
#endif

#ifndef UWORD
#define	UWORD	unsigned short int
#endif

#ifndef UDWORD
#define	UDWORD	unsigned long int
#endif

typedef struct ft_datestamp {
	SWORD	year;
	UWORD	month;
	UWORD	day;
} ft_datestamp;

typedef struct ft_timestamp {
	UWORD	hour;
	UWORD	minute;
	UWORD	second;
} ft_timestamp;

typedef struct ft_datetime {
	SWORD	year;
	UWORD	month;
	UWORD	day;
	UWORD	hour;
	UWORD	minute;
	UWORD	second;
	UDWORD	fraction;
} ft_datetime;

#include "ftinternalsyms.h"
/* Sub-types of FTN_INTERNAL.  Auto-created from FTINTERNALSYMBOLS_LIST: */
typedef enum FTI_tag
{
  FTI_UNKNOWN = -1,                             /* first */
#undef I
#define I(type) FTI_##type,
  FTINTERNALSYMBOLS_LIST
#undef I
  FTI_NUM                                       /* last */
}
FTI;
#define FTIPN   ((FTI *)NULL)

/* Prototypes for sub-object methods: */
#undef I
#define I(type)                                                         \
  void *tx_fti_##type##_open ARGS((CONST char *usr, size_t sz));        \
  void *tx_fti_##type##_close ARGS((void *obj));                        \
  void *tx_fti_##type##_dup ARGS((void *obj));                          \
  CONST char *tx_fti_##type##_tostr ARGS((void *obj));
FTINTERNALSYMBOLS_LIST
#undef I

/* The FTN_INTERNAL object itself.
 * >>>>> NOTE: no user-serviceable parts inside.  All access to     <<<<<
 * >>>>> FTN_INTERNAL objects must be through methods/macros below; <<<<<
 * >>>>> no direct memcpy/alloc in fldmath!                         <<<<<
 */
typedef struct ft_internal_tag
{
  FTI                           type;           /* subtype */
  size_t                        refcnt;         /* # users of object */
  void                          *obj;           /* sub object */
  struct ft_internal_tag        *next;          /* next of FLD.n items */
}
ft_internal;
#define ft_internalPN   ((ft_internal *)NULL)

/* Official alloced size; helps detect drilling/hand-copying: */
#define FT_INTERNAL_ALLOC_SZ    1

/* Methods to operate on an ft_internal object: */
ft_internal *tx_fti_open ARGS((FTI type, CONST char *usr, size_t sz));
ft_internal *tx_fti_close ARGS((ft_internal *fti, size_t n));
ft_internal *tx_fti_copy4read ARGS((ft_internal *fti, size_t n));
ft_internal *tx_fti_prep4write ARGS((ft_internal *fti));
int          tx_fti_append ARGS((ft_internal *fti, ft_internal *next));
CONST char  *tx_fti_obj2str ARGS((ft_internal *fti));
FTI          tx_fti_str2type ARGS((CONST char *s));
CONST char  *tx_fti_type2str ARGS((FTI type));
#define tx_fti_getobj(fti)              ((fti)->obj)
#define tx_fti_setobj(fti, newObj)      ((fti)->obj = (newObj))
#define tx_fti_gettype(fti)             ((fti)->type)
#define tx_fti_getnext(fti)             ((fti)->next)

#define FT_INTERNAL_MAGIC       0xCabFaced

#define TX_FTI_VALID(f)                                         \
  (((ft_internal_uber *)((char *)(f) - TX_FTI_UBER_OFF))->magic \
   == (int)FT_INTERNAL_MAGIC &&                                 \
   (unsigned)(f)->type < (unsigned)FTI_NUM &&                   \
   (f)->refcnt > 0)

extern CONST char       TxCorruptFtiObj[];

int fochil ARGS((FLD *chf1, FLD *ilf2, FLD *f3, int op));
int foilch ARGS((FLD *ilf1, FLD *chf2, FLD *f3, int op));
int foslil ARGS((FLD *slf1, FLD *ilf2, FLD *f3, int op));
int foilil ARGS((FLD *ilf1, FLD *ilf2, FLD *f3, int op));

/* - - - - - - - - - - - - ftinternal.c use only: - - - - - - - - - - - - - */
typedef struct ft_internal_uber_tag
{
  int           magic;                          /* must be first */
  ft_internal   fti;                            /* must be next */
}
ft_internal_uber;
#define ft_internal_uberPN      ((ft_internal_uber *)NULL)

#define TX_FTI_UBER_OFF ((char *)&ft_internal_uberPN->fti - CHARPN)

/* ------------------------------------------------------------------------ */

typedef EPI_INT64       ft_int64;
#define ft_int64PN      ((ft_int64 *)NULL)
typedef EPI_UINT64      ft_uint64;
#define ft_uint64PN     ((ft_uint64 *)NULL)

/* ------------------------------------------------------------------------ */

/* TX_FTN_SIZE_T: FTN_... value of ft_... type closest to OS size_t
 * TXftSize_t: ft_... type closest to OS size_t
 * WTF for now we use a signed type, even though size_t is typically unsigned,
 * to avoid funky Texis promotion rules when unsigned mixed with signed;
 * may allow some size_t values to overflow.  Sanify Texis promotion rules
 * and use proper unsigned types someday?:
 */
/* Try long before int: historically we use long (e.g. in $ret.off): */
#if EPI_OS_SIZE_T_BITS == EPI_OS_LONG_BITS
typedef ft_long TXftSize_t;
#  define TX_FTN_SIZE_T FTN_LONG
#elif EPI_OS_SIZE_T_BITS == 64
typedef ft_int64 TXftSize_t;
#  define TX_FTN_SIZE_T FTN_INT64
#else
#  error Need an FTN type for size_t of EPI_OS_SIZE_T_BITS bits
#endif

/******************************************************************/

typedef struct TXRECID
{
	EPI_OFF_T	off;
} RECID, * PRECID, ft_recid;

/************************************************************************/

/* Data Definition Field Type : is used for looking up information
   about a given data type
*/

typedef struct DDFT_tag
{
 char *name;  /* name of the type */
 int   type;  /* it's type number */
 int   size;  /* its size */
}
DDFT;
#define DDFTPN ((DDFT *)NULL)

/************************************************************************/

/* Data Definition Field Description : is used to store information
   about a field present in a table
*/

/************************************************************************/

typedef struct ODDFD
{
 size_t size;                 /* the size IN BYTES of this field 0==VAR */
 size_t elsz;                                    /* size of one element */
 size_t pos; /* position of field within record if fixed length or if it
                is variable, the variable field number */
 short  order;
 short  num;                      /* The number of the field, if wanted */
 byte   type;                                 /* the type of this field */
 char   name[DDNAMESZ];                              /* user given name */
} ODDFD;

/******************************************************************/
/* Order flags.  These modify how an order by (or any other compare)
   will occur. */

typedef enum TXOF_tag
{
	OF_NORMAL	= 0,
	OF_DESCENDING	= 1,
	OF_IGN_CASE	= 2,
	OF_DONT_CARE	= 4,
	OF_PREFER_END	= 8,
	OF_PREFER_START	=16
}
TXOF;
#define TXOFPN	((TXOF *)NULL)

/******************************************************************/

typedef struct DDFD_tag
{
 size_t size;                 /* the size IN BYTES of this field 0==VAR */
 size_t elsz;                                   /* byte size of one element */
 size_t pos; /* byte offset of field within record if fixed-size; if var,
                the `fd' index amongst variable fields */
 short  order;                  /* OF_... ordering bit flags */
 short  num;  /* original (user-created order) field index, starting with 0 */
 byte   type;                                 /* FTN type of this field */
 char   name[DDNAMESZ];                              /* user given name */
 byte	sttype;						/* Storage Type */
 size_t	stsize;						/* Storage Size */
 size_t	stelsz;						/* Storage Size */
}
DDFD;
#define DDFDPN  ((DDFD *)NULL)

/************************************************************************/

/* Data Definition : is used to store information
   about the collection of fields present in a table
*/

typedef struct ODD_tag
{
 byte  n;                              /* how many fields are described */
 ODDFD  fd[DDFIELDS];                          /* the field descriptions */
 size_t varpos;  /* where in a record does the first variable item start*/
 int    ivar;                                    /* which fd item is it */
 int    blobs;             /* are there blobs associated with this data */
}
ODD;
#define ODDPN ((ODD *)NULL)

typedef struct NDD_tag
{
	byte	n;                          /* how many fields are described */
	ODDFD	fd[DDFIELDS];                      /* the field descriptions */
	size_t	varpos;/* where in record does the first variable item start */
	int	ivar;                                 /* which fd item is it */
	int	blobs;          /* are there blobs associated with this data */
/* Above this line it is the same as before.  Only easy way for compatibility */
	int	tbltype;
}
NDD;
#define NDDPN ((NDD *)NULL)

#define DD_MAGIC	0xff1301dd	/* First byte > 50, so can't confuse */
#define DD_CURRENT_VERSION	1

typedef struct DD_tag
{
	int	magic;                   /* Identify this as a DD structure */
	int	version;       /* Allow versioning, which I'll need to know */
	size_t	size;	                                    /* How big am I */
	int	slots;                        /* How many fields can I hold */
	int	n;                          /* how many fields are in use */
	size_t	varpos;/* byte offset in record of first variable `fd' item */
	int	ivar;		       /* `fd' index of first variable item */
	int	blobs;          /* are there blobs associated with this data */
/* Above this line it is the same as before.  Only easy way for compatibility */
	int	tbltype;
	DDFD	fd[1];   /* field descriptions (fixed followed by variable) */
}
DD;
#define DDPN ((DD *)NULL)

char	*ddgetname	ARGS((DD *, int));
DDFD	*ddgetfd	ARGS((DD *, int));
DDFD	*ddgetfdr	ARGS((DD *, int));
int	ddsetordern(DD *dd, const char *fname, TXOF order);
int	ddgetnum	ARGS((DD *, int));

#define ddgetnfields(a)		((a)->n)
#define ddgetfd(a, b)		(&((a)->fd[(b)]))
#define ddgetsize(a, b)		(((a)->fd[(b)]).size/((a)->fd[(b)]).elsz)
#define ddgetorder(a, b)	(((a)->fd[(b)]).order)
#define ddsetorder(a, b, c)	((((a)->fd[(b)]).order) = (c))
int     TXddSetOrderFlagsByIndex(DD *dd, size_t index, TXOF orderFlags);
#define ddgettype(a)		((a)->tbltype)
#define ddgetftype(a, b)	(((a)->fd[(b)]).type)
#define ddsetftype(a, b, c)	(((a)->fd[(b)]).type) = (c)
#define ddgetblobs(a)		((a)->blobs)
#define ddgetivar(a)		((a)->ivar)

char    *TXorderFlagsToStr(TXOF orderFlags, int verbose);

char    *TXddSchemaToStr(const DD *dd, int orderToo);
size_t TXddPrintFieldNames ARGS((char *buf, EPI_SSIZE_T bufSz, DD *dd));
int    TXddOkForTable(TXPMBUF *pmbuf, DD *dd);

extern const char       TXrankColumnName[];
#define TX_RANK_COLUMN_NAME_LEN 5
#define TX_RANK_COLUMN_TYPE_FTN FTN_INT
extern const char       TXrankColumnTypeStr[];
typedef ft_int  TXrankColumnType;

extern const char       TXrecidColumnName[];
#define TX_RECID_COLUMN_NAME_LEN        6
#define TX_RECID_COLUMN_TYPE_FTN        FTN_RECID
extern const char       TXrecidColumnTypeStr[];
typedef ft_recid        TXrecidColumnType;

/************************************************************************/
#endif /* DD_H */
/************************************************************************/
