#ifndef EQVINT_H
#define EQVINT_H
/***********************************************************************
history:

04-00-92
   first rev

05-28-92
   add a version tag to the header, changed magic, still read old magic tho
   switch to binary chaining instead of source chaining

**********************************************************************
file structure:

32bit-off 8bit-lenw --\
...                    -- nrecs
32bit-off 8bit-lenw --/
32bit-off 8bit-lenw ----- one extra for algorithmic ease

equiv rec           --\<- dataoff
...                    -- nrecs
equiv rec           --/

8bit-recn 32bit-off 8bit-lenw lenwbyte-word --\<- fixcacheoff
                                               -- nfixcache (<=NEQVFIXCACHE)
8bit-recn 32bit-off 8bit-lenw lenwbyte-word --/

chainto_filename    -- optional

32bit-chainoff      -- offset of chainto_filename or 0, only present for MAGIC21
 8bit-chainlen      -- length of chainto_filename or 0, only present for MAGIC21
 8bit-version       -- only present for MAGIC21
32bit-magic
16bit-maxwrdlen
16bit-maxreclen
16bit-maxwords
32bit-nrecs
32bit-dataoff
32bit-fixcacheoff
 8bit-nfixcache

items before magic in the header appeared in version 2.1 - MAW 05-28-92

***********************************************************************/
#ifndef NOKRYPTONITE/* give me a way to turn it off wo/editing header */
#  define KRYPTONITE                            /* encrypt equiv file */
#endif
#include "sizes.h"
#include "otree.h"
#include "salloc.h"
#include "eqv.h"

#ifdef TEST
   int debuglevel=0;
#  define debug(l,a) ((l)>debuglevel?0:((a),0))
#else
#  ifdef DEBUG
#     define debug(l,a) ((l)>DEBUG?0:(a),0)
#  else
#     define debug(l,a)
#  endif
#endif

#define HDRSZ    23       /* 1 8bit byte, 3 16bit words,4 32bit words */
#define HDRSZ21  29       /* 3 8bit byte, 3 16bit words,5 32bit words */
#define NDXSZ     5                     /* 1 32bit dword,1 16bit word */
#define HDRKEY   35                          /* header encryption key */
#define CACHEKEY 15                     /* fixed cache encryption key */
#define MAGICM   0x7165574d                                 /* "MWeq" */
#define MAGICU   0x71657575                                 /* "uueq" */
#define MAGICM21 0x7165776d                                 /* "mweq" */
#define MAGICU21 0x71655555                                 /* "UUeq" */
#ifdef EQV_INTERNAL
#  define MAGIC   MAGICM
#  define MAGIC21 MAGICM21
#else
#  define MAGIC   MAGICU
#  define MAGIC21 MAGICU21
#endif
#define EQVERSION 0x21                                         /* 2.1 */

#define EQVNDX struct eqvndx_struct
EQVNDX {
   dword off;
   byte  len;
};

/**********************************************************************/
#ifdef KRYPTONITE
#  define mmcrypt(a,b,c) strweld((a),(b),(c))
#else
#  define mmcrypt(a,b,c) (a),(b),(c)
#endif
extern int  eqvparserec ARGS((EQVREC *rec));
extern int  eqvpq       ARGS((char *query,char ***lst,int *nlst,
                              char ***originalPrefixedTerms, int *isects,
                              int **a_setqoffs, int **a_setqlens));

extern int  eqvwritew   ARGS((word  *val,int n,EQV *eq,word key));
extern int  eqvreadb    ARGS((byte  *val,int n,EQV *eq,word key));
extern int  eqvreaddw   ARGS((dword *val,int n,EQV *eq,word key));
extern int  eqvreadw    ARGS((word  *val,int n,EQV *eq,word key));
extern int  eqvwriteb   ARGS((byte  *val,int n,EQV *eq,word key));
extern int  eqvwritedw  ARGS((dword *val,int n,EQV *eq,word key));
extern int  eqvwritew   ARGS((word  *val,int n,EQV *eq,word key));

extern int  rdeqvndx    ARGS((EQV *eq,EQVNDX *ndx,long n));
extern void strweld     ARGS((byte *,int,word));
/**********************************************************************/
#endif                                                    /* EQVINT_H */
