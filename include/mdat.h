#ifndef MDAT_H
#define MDAT_H


/* the purpose of MDAT is to provide a method of containing that which
would normally be put into many files into a single set of files.  This
code minimizes the operating system overhead for managing many files
by collecting them  together and providing read/write caching for
each sub-file.


This program expects that :

A: Reading and writing is not performed concurrently.
B: All write's are performed at EOF.
C: All reads will be performed sequentially with repect to the subfile.

All of the above could be easily changed, but it might cost a little
on the performance end of things.

*/

#ifndef byte
#define byte unsigned char
#endif
#ifndef word
#define word unsigned short
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

#if defined(unix) || defined(__unix)
#define  MDBUFSZ 32000
#else
#define  MDBUFSZ 8192
#endif

#define  MINMGBUFSZ 1024            /* min incoming cache size in merge */
#define  OMGBUFSZ   8192                /* outgoing cache size in merge */

#define MDINDEXSUF    ".mdi"                  /* index file name suffix */
#define MDDATSUF      ".mdd"                   /* data file name suffix */

/************************************************************************/

#define MDATI struct mdat_index        /* these go into the ".mdi" file */
MDATI
{
 off_t bod; /* begin of data block */
 off_t eod; /* end of data block */
};

#define MDAT struct md_struct                     /* multiple data file */
#define MDATPN (MDAT *)NULL
MDAT
{
 int ifh;           /* the index file handle */
 int dfh;           /* the dict file handle */
 byte *buf;         /* a cache buffer for reads writes to the file */
 size_t bufsz;      /* size of the cache */
 byte *bp;          /* buffer cache pointer */
 byte *end;         /* end of buffer cache pointer */
 byte mode;         /* the mode this MDAT is in */
 byte clone;        /* am I a clone, or am I the original */
 MDATI info;        /* the info about this blocks location */
 long     n;        /* how many are there ( valid on read mode only ) */
 int  inclose;      /* am I trying to close the MDAT?  don't close on fail */

/* Below is used only for the MERGE functions */

 byte  *mbuf;        /* buffer to hold the alloced mdat item */
 size_t mbufsz;      /* size of the buffer */
 int    mitemsz;     /* size of the item in the buffer */

 char *ifn, *dfn;   /* file names KNG 971021 */
};

/* the putmdat()/getmdat() structure of the ".mdd" file is:
{
 word size;  assumes 2-byte short
 char   data[size];
}  file[entries in the ".mdi" file];
*/

/************************************************************************/
/* prototypes go here */

MDAT *closemdat ARGS((MDAT *md));
int  readmdat ARGS((MDAT *md,byte  *buf,size_t  n));
int  writemdat ARGS((MDAT *md,byte  *buf,size_t  n));
int  getmdat ARGS((MDAT *md,byte  * *bufp,size_t  *maxp));
int  putmdat ARGS((MDAT *md,byte  *buf,int  sz));
MDAT *_openmdat ARGS((char  *prefn,char  *mode,size_t bufsz));
MDAT *_nextmdat ARGS((MDAT *emd,size_t bufsz));
char *tmpmdpre ARGS((char *dir));
#define openmdat(prefn,mode) _openmdat(prefn,mode,MDBUFSZ)
#define nextmdat(emd)        _nextmdat(emd,emd->bufsz)
int  rmmdat ARGS((char *pren));

#ifdef merge_prototype
int
mergemdat(
 char  *prefn,           /* name of the MDAT file i'm supposed to merge */
 char  *tempn,             /* name of the temp MDAT im  supposed to use */
                                          /* your item compare function */
 int  (*cmp)(void *usr,byte *a,size_t,alen,byte *b,size_t,blen),
                              /* your item put function ( may be null ) */
 int  (*put)(void *usr,byte *a,size_t,alen),
 void  *usr,                   /* data you'd like to see when comparing */
 long   memsz                    /* how much memory am I allowed to use */
);

#endif
/************************************************************************/
#endif /* MDAT_H */
