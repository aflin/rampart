#ifndef FDBF_H
#define FDBF_H
/************************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/************************************************************************/
#ifndef FILE
#include "stdio.h"
#endif

#define FDBFHIGHIO 0

#ifdef MSDOS
#define FDBFCACHESZ 8192
#else
#define FDBFCACHESZ 48000
#endif


/* the external (disk) representation a FDBF block works as follows:

byte     sizetype flag  : 0=nibble, 1=byte , 2=word, 3=dword
sizetype used           : how many bytes of the block are used
sizetype size           : how many bytes are in the block
data | next             : if the block is free the next ptr is in the block
.
.
.
header 2
data   2
.
.
.
size_t free             : ptr to the first free block
*/
#ifndef byte
#define byte unsigned char
#endif

#ifndef word
#define word unsigned short
#endif


#define FDBFTYBITS 3             /* the bits that are used for size type */

#define FDBFNIBBLE 0   /* the type field */
#define FDBFBYTE   1
#define FDBFWORD   2
#define FDBFSTYPE  3


#define FDBFN struct db_file_nibble
#define FDBFB struct db_file_byte
#define FDBFW struct db_file_word
#define FDBFS struct db_file_size_t
#define FDBFNPN ( FDBFN *)NULL
#define FDBFBPN ( FDBFB *)NULL
#define FDBFWPN ( FDBFW *)NULL
#define FDBFSPN ( FDBFS *)NULL


#define FDBFNMAX 0x000F
#define FDBFBMAX 0x00FF
#define FDBFWMAX 0xFFFF
#ifdef __alpha                 /* MAW 12-19-94 - can't shift left 64! */
#  define FDBFSMAX      0xFFFFFFFFFFFFFFFF
#else
  /* derived: could be a prob */
#  define FDBFSMAX      ((size_t)1 << ((sizeof(size_t) << 3) - 1))
#endif

       /* these are the external representations of a FDBF block */

/* note:  I changed the struct members to arrays to ensure the packing
*  that was desired when structures are allocated.
*/

FDBFN
{
 byte used_size;
};

FDBFB
{
 byte  used_size[2];
};

FDBFW
{
 word  used_size[2];
};

FDBFS /* size_t determines the largest allocation */
{
 size_t used_size[2];
};

#define FDBFALL union fdbf_all_types
FDBFALL
{
 FDBFN n;
 FDBFB b;
 FDBFW w;
 FDBFS s;
};


/************************************************************************/


#define FDBF struct db_struct
#define FDBFPN (FDBF *)NULL
FDBF
{
 char    *fn;  /* the file name used for this struct */
#if FDBFHIGHIO
 FILE    *fh;  /* the file handle */
#else
 int      fh;  /* the file handle */
#endif
 byte    tmp;  /* is this a temporary file */

         /* my copies of what's on the disk */
 off_t   at;   /* offset of this record */
 off_t  end;   /* offset of end of this record + 1  */
 byte   type;  /* size type flag for the current block */
 size_t used;  /* how much of the block is used */
 size_t size;  /* how much can the block hold */
 off_t  next;  /* wheres the next available free block */

 void   *blk;   /* allocated user data for user */
 size_t  blksz; /* size of block buffer */

/* the following is for write cache   */
 byte   cache_on; /* flag to indicate whether or not to cache writes */
 off_t  coff;     /* offset of the cache ( should be eof-sizeof(off_t) */
 size_t csz;      /* the size of the cache */
 byte   cache[FDBFCACHESZ+17]; /* the cache itself + 17 for the largest header */
 byte   new;      /* flag inidicating this is new ( no free + nobody else ) */
 byte   overalloc; /* size>>overalloc amount to overallocate new blocks by */
};


/************************************************************************/
/* prototypes go here */
/************************************************************************/
int     TXfdbfIsEnabled(void);

FDBF  *closefdbf ARGS((FDBF *df));
FDBF  *openfdbf  ARGS((char *fn));
int    freefdbf  ARGS((FDBF *df, EPI_OFF_T epi_at));
EPI_OFF_T  fdbfalloc ARGS((FDBF *df,void *buf, size_t n));
EPI_OFF_T  putfdbf   ARGS((FDBF *df, EPI_OFF_T epi_at, void *buf, size_t sz));
void  *getfdbf   ARGS((FDBF *df, EPI_OFF_T epi_at, size_t *psz));
void  *agetfdbf  ARGS((FDBF *df, EPI_OFF_T epi_at, size_t *psz));
size_t readfdbf  ARGS((FDBF *df, EPI_OFF_T epi_at, size_t *off, void *buf, size_t sz));
EPI_OFF_T  tellfdbf  ARGS((FDBF *df));
char  *getfdbffn ARGS((FDBF *df));
int    getfdbffh ARGS((FDBF *df));
void   setfdbfoveralloc ARGS((FDBF *df,int div));
int    validfdbf ARGS((FDBF *df, EPI_OFF_T epi_at));

#endif /* FDBF_H */
