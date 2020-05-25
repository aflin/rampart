#ifndef TIN_H
#define TIN_H

/*#define ZEROTHETREE 1*/ /* zero cnts instead of closing it between passes */

#ifndef RLEX_H
#        include "rexlex.h"
#endif
#ifndef DBF_H
#        include "dbf.h"
#endif
#ifndef DBTABLE_H
#        include "dbtable.h"
#endif
#ifndef XTREE_H
#        include "xtree.h"
#endif
#ifndef MDAT_H
#        include "mdat.h"
#endif
#ifndef BTREE_H
#       include "btree.h"
#endif

/************************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/************************************************************************/

#ifndef byte
#define byte unsigned char
#endif

#ifndef word
#define word unsigned short
#endif

#ifndef dword
#define dword unsigned long
#endif

#ifndef ulong
#define ulong unsigned long
#endif

#ifndef tin_t
#define tin_t unsigned long
#endif

#define  TINDICTMEM 2000000       /* max default internal dictionary sz */
#define  TINMERGMEM 2000000              /* max default merge memory sz */
#define  TTBTPSZ    8192
#define  TTBTCSZ    20
#define  TINTMPPREFIX ""                 /* the temp file dirctory area */

#define TIN struct ti_struct
#define TINPN (TIN *)NULL
TIN
{
 XTREE *xt;      /* an internal tree to hold dictionaries */
 RLEX  *rl;      /* a lexical input matcher */
#ifdef OLD_MM_INDEX
 MDAT  *md;      /* a place to put my data */
#endif /* OLD_MM_INDEX */
 char  *omdpre;  /* prefix name of resulting mdat file */
 char  *tmdpre;  /* prefix name of temp mdat file */
 tin_t  th;      /* the tin type token to assign to the matched items */
 long dmem;      /* allowed dictionary ram */
 long mmem;      /* allowed merge ram */
 byte **exprs;   /* the REX expressions in use */
 byte **noise;   /* the noise list to be deleted ( may be null ) */
 long    n;      /* how many tins have been produced */
 int (*usrput) ARGS((void *, tin_t, byte *, size_t)); /* a user callback for the merge function */
 void *usr;       /* the users data structure */
};

/* The contents of an MDAT record written with
   TIN look like this:

   handle,string

   where string is the string that was matched by getrlex()
   and
   handle is a tin_t that is the user assigned handle to the string.

   so "abc" with a handle==5 would more than likely look like this:

   0x00,0x00,0x00,0x05,0x61,0x62,0x63
             5          a    b    c
*/

/************************************************************************/

/* TTL Stands for tin_t(ype) list */

#define TTLALLOCSZ 256

#define TTL struct tl_struct
#define TTLPN (TTL *)NULL
TTL
{
 byte    *buf;  /* the buffer that holds the info */
 size_t bufsz;  /* size of the buffer */
 byte    *end;  /* the end of the buffer pointer */
 byte     *pp;  /* a pointer to the current put location */
 byte     *gp;  /* a pointer to the current get location */
 tin_t    val;  /* the last value input output to/from the lst */
 tin_t    orun; /* the size of a delta 1 run (signified by a 0 vsl) */
 tin_t    irun; /* the size of a delta 1 run (signified by a 0 vsl) */
 EPI_OFF_T handle;  /* dbf handle associated with this entry */
 tin_t  count;  /* How many values */
 tin_t minval;  /* Minimum value */
 tin_t maxval;  /* Maximum value */
 int  stvalid;  /* Are the stats valid */
};

/************************************************************************/

#define TTBL struct tin_table
#define TTBLPN (TTBL *)NULL
TTBL
{
#ifdef OLD_MM_INDEX
 TIN *ti;      /* a tin pointer for indexing the info */
 TTL *tl;      /* a list pointer for shoving things out */
 TTL *exc;     /* a list of tokens to exclude */
 BTREE *bt;    /* btree index to hold data */
 DBF *bdbf;    /* the blob dbf from the table */
 tin_t handle; /* the current handle associated with putttin() */
 int  newdb;     /* is this a new database or not */
#else  /* !OLD_MM_INDEX */
 BTREE *bt;    /* btree index to hold data */
#endif /* !OLD_MM_INDEX */
};

/************************************************************************/

/* prototypes go here */

extern TTBL *openttbl ARGS((char *tblnm,char **explst));
extern TTBL *openrttbl ARGS((char *tblnm,char **explst));
extern TTBL *closettbl ARGS((TTBL *tt));
extern long  addttbl ARGS((TTBL *tt,byte *buf,byte *end,tin_t handle));
extern int   putttbl ARGS((TTBL *tt));


/* note:  the memory amounts below are allocated at seperate times
usually */

extern  long   tindictmem ;         /* how much xtree memory can it use */
extern  long   tinmergmem ;         /* how much merge memory can it use */
extern  char  *tintmpprefix;     /* prefix appended onto tmp file names */


/* OLD DEFS
int    nexttin ARGS((TIN *ti,tin_t  handle));
int    tincmp ARGS((TIN *ti,byte  *a,size_t  alen,byte  *b,size_t  blen));
int    tinmerge ARGS((TIN *ti,int (*userput)(void *usr,tin_t handle,byte *buf,size_t len),void *usr));
long   puttin ARGS((TIN *ti,byte  *buf,byte  *end));
TIN   *closetin ARGS((TIN *ti));
TIN   *opentin ARGS((char  *ofpre,char  * *explst,tin_t  handle));
static int  tinxtcb ARGS((TIN *ti,byte  *s,size_t  len,int  cnt));
int  main(int  argc,char  * *argv);
*/

int  getttl ARGS((TTL *tl,tin_t  *pval));
int  nexttin ARGS((TIN *ti,tin_t  handle));
int  put ARGS((void  *usr,tin_t  handle,byte  *buf,size_t  len));
int  putttl ARGS((TTL *tl,tin_t  val));
int  tinmerge ARGS((TIN *ti,int  (*usrput)(void  *usr,tin_t handle ,byte  *buf,size_t len ),void  *usr));
long puttin ARGS((TIN *ti,byte  *buf,byte  *end));
TIN *closetin ARGS((TIN *ti));
TIN *opentin ARGS((char  *ofpre,char  * *explst,tin_t  handle));
TTL *closettl ARGS((TTL *tl));

TTL * getdbfttl ARGS(( DBF *df, EPI_OFF_T at));
EPI_OFF_T putdbfttl ARGS(( DBF *df, EPI_OFF_T at, TTL *tl));

TTL *openttl ARGS((void));
void  rewindttl ARGS((TTL *tl));
void  resetttl ARGS((TTL *tl));
void  tinnoise ARGS((TIN *ti,char **lst));

TTL *orttl ARGS((TTL *, TTL *));
TTL *andttl ARGS((TTL *, TTL *));
TTL *subttl ARGS((TTL *, TTL *));
unsigned long countttl ARGS((TTL *));
/************************************************************************/
#endif /* TIN_H */
/************************************************************************/
