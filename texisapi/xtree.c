#include "txcoreconfig.h"
#include "stdio.h"
#include "errno.h"
#include "string.h"
#include "ctype.h"
#include "stdlib.h"
#include "sys/types.h"
#ifdef UMALLOC3X
#   include <malloc.h>
#endif
#include "dbquery.h"
#include "mmsg.h"
#include "texint.h"
#include "unicode.h"                            /* before xtree.h */
#include "xtree.h"

#define BALANCE 1                                  /* use balanced tree */
#ifndef DEBUG
# define dumptree(x,y)
#else
# define dumptree(x,y)
#endif


struct XTN_tag                                  /* a node in an xtree */
{
  XTN           *h;                             /* hi node */
  XTN           *l;                             /* low node */
  byte          red;
  size_t        cnt;                            /* #times inserted; 0 = del */
  XTREESEQ      seq;
  size_t        len;
  byte          s[1];                           /* variable length! */
};

struct XTREE_tag                                /* the xtree object */
{
  XTN           *root;                          /* the root of the tree */
  XTN           *z;
  XTN           zdummy;
  size_t        memUsed;                        /* how much memory used */
  size_t        maxmem;                         /* how much memory can I use*/
  /* XTREE.seq: how a node's XTN.seq gets assigned:
   *  0:  pointer sequencing: use key pointer arg from putxtree()
   * >0:  numeric sequencing: use (XTREE.seq - 1) and increment XTREE.seq
   */
  size_t        seqNum;
  size_t        numItems;                       /* #items with cnt > 0 */
  size_t        numAllocatedItems;              /* #items inserted */
  int           (*usercall)ARGS((void *userptr, byte *s, size_t len,
                                 size_t cnt, XTREESEQ seq));
  void          *userptr;                       /* moved from static */
  XTN           **stk;                          /* non-recursive walk */
  byte          *stkstate;
  int           stkcur, stksz;
  int           cleardump;
  TXCFF         txcffCmpMode;                   /* -1 == memcmp else TXCFF */
  byte          storeFolded;                    /* !=0: store strings folded*/
  TXPMBUF       *pmbuf;                         /* buffer for putmsgs */
};


/******************************************************************/

static XTN *rotate  ARGS((XTREE *tr, byte *foldedS, size_t foldedLen, XTN *x));
static XTN *split   ARGS((XTREE *tr,byte  *foldedS,size_t foldedLen,XTN *gg,XTN *g,XTN *p,XTN *x));

/************************************************************************/
                    /* default xtree comparison function */

/* note use of scratch var `xcCmp': */
#define MEMCMP_COMPARE(a, alen, b, blen)                                \
  (((xcCmp) = memcmp((void *)(a), (void *)(b),                          \
                     ((alen) < (blen) ? (alen) : (blen)))) == 0 ?       \
   ((int)(alen) - (int)(blen)) : (xcCmp))

/* note use of scratch vars `xcA', `xcB': */
#define STRING_COMPARE(a, alen, b, blen, txcff)                         \
  ((xcA) = (char *)(a), (xcB) = (char *)(b),                            \
   TXunicodeStrFoldCmp(&(xcA), (alen), &(xcB), (blen), (txcff)))

/* note use of scratch vars declared in XTNCMP_SCRATCH_VARS:
 * `foldedS' must be folded iff `tr->storeFolded' is nonzero:
 */
#define XTNCMP(tr, foldedS, foldedLen, xtn)                             \
  ((tr)->txcffCmpMode == TXCFF_UNKNOWN || (tr)->storeFolded ?                      \
   MEMCMP_COMPARE((foldedS), (foldedLen), (xtn)->s, (xtn)->len) :       \
   STRING_COMPARE((foldedS), (foldedLen), (xtn)->s, (xtn)->len,         \
                  (tr)->txcffCmpMode))

#define XTNCMP_SCRATCH_VARS     int     xcCmp; CONST char *xcA, *xcB

#define FOLD_VARS(s, len)                       \
  byte          *foldedS = (s);                 \
  size_t        foldedLen = len, foldRes;       \
  byte          foldedTmp[256]

/* If `tr->storeFolded' is nonzero, we store the folded string in the
 * tree, not the original, and compare the pre-folded new `s' key
 * against (pre-folded) tree values via memcmp() (not strfold) for speed.
 * Thus, we need to pre-fold `s' if needed:  KNG 20080718
 */
#define FOLD_INIT(tr, fn, s, len, err)                                  \
  if ((tr)->storeFolded && (tr)->txcffCmpMode != TXCFF_UNKNOWN)           \
    {                                                                   \
      foldedLen = (len) + 5;                    /* initial SWAG */      \
      foldedS = foldedTmp;                                              \
      do                                                                \
        {                                                               \
          if (foldedS != foldedTmp) free(foldedS);                      \
          if (foldedLen < sizeof(foldedTmp))    /* `foldedTmp' fits */  \
            foldedS = foldedTmp;                                        \
          else if ((foldedS = (byte *)TXmalloc((tr)->pmbuf, fn,         \
                                                  foldedLen)) == BYTEPN)\
            goto err;                                                   \
          foldRes = TXunicodeStrFold((char *)foldedS, foldedLen,        \
                                     (char *)(s), (len),                \
                                     (tr)->txcffCmpMode);               \
          foldedLen += (foldedLen >> 4) + 16;   /* inc for next pass */ \
        }                                                               \
      while (foldRes == (size_t)(-1));                                  \
      foldedLen = foldRes;                                              \
    }
      
#define FOLD_CLEANUP(s)         \
  if (foldedS != BYTEPN && foldedS != (s) && foldedS != foldedTmp)      \
    free(foldedS)

/************************************************************************/

#if !defined(FOR_TEXIS) || defined(FIXED_NOISE_LIST)
/* this list is ordered by frequency in english text. It will generate
a rather optimal tree when it is inserted BEFORE any calls to
putxtree. Use the call xtreenoise() to put it in place */

char *xtnoiselst[]={
#if 0 /* wtf */
"information","page","web","home","welcome","www","internet",
"new","services","pages","world","service",
#endif
"the", "of", "a", "to", "and", "in", "that", "for", "with", "as",
"from", "at", "on", "by", "have", "an", "or", "is", "their",
"they", "it", "he", "more", "this", "may", "than", "not",
"one", "are", "be", "such", "these", "has", "about", "into",
"who", "two", "but", "some", "his", "had", "also", "up", "will",
"out", "no", "all", "now", "most", "was", "many", "between",
"we", "those", "each", "each", "so", "them", "them", "same",
"last", "over", "like", "if", "after", "even", "were", "through",
"then", "much", "because", "very", "any", "you", "within", "just",
"before", "been", "there", "should", "another", "make", "her",
"whether", "less", "she", "she", "off", "ago", "while", "yet",
"per", "least", "back", "down", "get", "see", "take", "does",
"our", "without", "too", "left", "come", "away", "every", "until",
"go", "never", "whose", "ever", "almost", "cannot", "seen", "going",
"being", "us", "us", "came", "again", "something", "your", "my",
"my", "put", "me", "got", "went", "became", "onto", "having",
"always", "goes", "him", "anything", "here", "done", "getting",
"sure", "doing", "front", "none", "upon", "saw", "stand", "whom",
"someone", "anyone", "putting", "am", "whatever", "unless", "let",
"i", "gone", "maybe", "else", "everyone", "everything", "everything",
"mine", "mine", "myself", "anyway", "ourselves", "gotten", "anybody",
"somebody", "shall", "till", "whoever", "anyhow", "isn't", "said", "its", ""
};
#endif

/************************************************************************/

static void freextn ARGS((XTN *ts,XTN *z));
static void
freextn(ts,z)
XTN *ts,*z;
/* Frees tree of nodes rooted at `ts', except for (sub-)tree(s) rooted
 * at `z'.
 */
{
 if(ts!=z)
    {                            /* PBR 07-05-93 chged to !=z from !=ts */
     if(ts->h!=z) freextn(ts->h,z);
     if(ts->l!=z) freextn(ts->l,z);
     free(ts);
    }
}

/************************************************************************/

XTREE *
closextree(tr)
XTREE *tr;
{
 if(tr!=XTPN)
    {
     if(tr->root!=XTNPN)
         freextn(tr->root,tr->z);
     if (tr->stk != XTNPPN) free(tr->stk);              /* KNG 990910 */
     if (tr->stkstate != BYTEPN) free(tr->stkstate);    /* KNG 990910 */
     tr->pmbuf = txpmbuf_close(tr->pmbuf);
     free(tr);
    }
 return(XTPN);
}

/************************************************************************/

XTREE *
openxtree(pmbuf, maxmem)
TXPMBUF *pmbuf;         /* (in, opt.) buffer for putmsgs; will attach to it */
size_t maxmem;
{
  static CONST char   fn[] = "openxtree";
 XTREE *tr;

 errno=ENOMEM;                                        /* MAW 04-18-91 */
 tr = (XTREE *)TXcalloc(pmbuf, fn, 1,sizeof(XTREE));
 if(tr==XTPN)                                  /* and checks too */
     return(tr);
 tr->txcffCmpMode = TXCFF_UNKNOWN;                         /* i.e. memcmp() key compare*/
 tr->storeFolded = 0;
 tr->pmbuf = txpmbuf_open(pmbuf);               /* attach to it */
 tr->root = (XTN *)TXcalloc(pmbuf, fn, 1, XTN_BASE_SZ + 1);    /* +1 for nul string */
 if(tr->root==XTNPN)
   {
     tr = TXfree(tr);
     return(XTPN);
   }

 tr->z=&tr->zdummy;
 tr->z->l=tr->z->h=tr->z;                               /* PBR 11-08-91 */
 tr->z->red=0;

 tr->memUsed = sizeof(XTREE) + XTN_BASE_SZ + 1;
 tr->maxmem =maxmem;

 tr->root->h=tr->root->l=tr->z;                         /* PBR 11-08-91 */
 tr->root->s[0]='\0';                                   /* PBR 05-11-93 */
 tr->root->len =0;                                      /* PBR 06-15-93 */
 tr->root->seq.num = 0;
 tr->seqNum = 1;                                /* numeric sequencing */
 tr->numItems = tr->numAllocatedItems = 0;      /* nothing inserted yet */
 tr->stkcur = -1;                                       /* KNG 990910 */
 return(tr);
}

/************************************************************************/

TXCFF
TXxtreeGetCmpMode(tr)
XTREE   *tr;            /* (in) tree */
/* Returns current compare mode; -1 indicated memcmp() compare.
 */
{
  return(tr->txcffCmpMode);
}

int
TXxtreeSetCmpMode(tr, cmpMode)
XTREE   *tr;            /* (in/out) tree to modify */
TXCFF   cmpMode;        /* (in) -1 == memcmp(), else strfoldcmp */
/* Returns 0 on error.
 */
{
  static CONST char     fn[] = "TXxtreeSetCmpMode";

  if (tr->numAllocatedItems > 0)
    {
      txpmbuf_putmsg(tr->pmbuf, MERR + UGE, fn,
                     "Cannot modify compare mode on non-empty xtree");
      return(0);
    }
  tr->txcffCmpMode = cmpMode;
  return(1);
}

int
TXxtreeGetStoreFolded(tr)
XTREE   *tr;            /* (in) tree */
{
  return(tr->storeFolded);
}

int
TXxtreeSetStoreFolded(tr, storeFolded)
XTREE   *tr;            /* (in/out) tree to modify */
int     storeFolded;    /* (in) nonzero: store strings folded */
/* Returns 0 on error.
 */
{
  static CONST char     fn[] = "TXxtreeSetStoreFolded";

  if (tr->numAllocatedItems > 0)
    {
      txpmbuf_putmsg(tr->pmbuf, MERR + UGE, fn,
                     "Cannot modify store-folded flag on non-empty xtree");
      return(0);
    }
  tr->storeFolded = (storeFolded ? 1 : 0);
  return(1);
}

int
TXxtreeGetNumItems(xtree, numItems)
XTREE   *xtree;         /* (in) */
size_t  *numItems;      /* (out) */
/* Sets `*numItems' to number of items (with cnt > 0) in xtree.
 * Returns 0 on error.
 */
{
  *numItems = xtree->numItems;
  return(1);
}

int
TXxtreeGetNumAllocatedItems(xtree, numAllocatedItems)
XTREE   *xtree;                 /* (in) */
size_t  *numAllocatedItems;     /* (out) */
/* Sets `*numAllocatedItems' to number of alloced items in xtree.
 * Returns 0 on error.
 */
{
  *numAllocatedItems = xtree->numAllocatedItems;
  return(1);
}

int
TXxtreeGetMemUsed(xtree, memUsed)
XTREE   *xtree;         /* (in) */
size_t  *memUsed;       /* (out) */
/* Sets `*memUsed' to total memory used by xtree.
 * Returns 0 on error.
 */
{
  *memUsed = xtree->memUsed;
  return(1);
}

int
TXxtreeSetSequenceMode(xtree, usePtr)
XTREE   *xtree;         /* (in/out) */
int     usePtr;         /* (in) nonzero: use pointer not numeric sequencing */
/* Returns 0 on error.
 */
{
  static CONST char     fn[] = "TXxtreeSetSequenceMode";

  if (xtree->numAllocatedItems > 0)
    {
      txpmbuf_putmsg(xtree->pmbuf, MERR + UGE, fn,
                     "Cannot modify sequence mode on non-empty xtree");
      return(0);
    }
  xtree->seqNum = (usePtr ? 0 : 1);
  return(1);
}

static void TXxtreeGetMaxDepthActual ARGS((XTN *x, XTN *z, size_t curDepth,
                                           size_t *maxDepth));
static void
TXxtreeGetMaxDepthActual(x, z, curDepth, maxDepth)
XTN     *x;             /* (in) current node */
XTN     *z;             /* (in) zero node? */
size_t  curDepth;       /* (in) depth at `x' */
size_t  *maxDepth;      /* (in/out) max depth reached before `x' */
{
  if (*maxDepth < curDepth) *maxDepth = curDepth;
  if (x->l != z) TXxtreeGetMaxDepthActual(x->l, z, curDepth + 1, maxDepth);
  if (x->h != z) TXxtreeGetMaxDepthActual(x->h, z, curDepth + 1, maxDepth);
}

int
TXxtreeGetMaxDepth(xtree, maxDepth)
XTREE   *xtree;         /* (in) */
size_t  *maxDepth;      /* (out) */
/* Sets `*maxDepth' to maximum depth of xtree.
 * Returns 0 on error.
 */
{
  *maxDepth = 0;
  if (xtree->root != xtree->z && xtree->root != XTNPN)
    TXxtreeGetMaxDepthActual(xtree->root, xtree->z, 1, maxDepth);
  return(1);
}

/************************************************************************/

static XTN *
rotate(tr, foldedS, foldedLen, x)       /* page 225 sedgewick v2 */
XTREE   *tr;
byte  *foldedS;         /* item, already TXCFF-folded iff `tr->storeFolded' */
size_t foldedLen;       /* its length */
XTN   *x;
{
 XTNCMP_SCRATCH_VARS;
 XTN *c,*gc;

 if(XTNCMP(tr, foldedS, foldedLen, x)<0) c=x->l;
 else            c=x->h;

 if(XTNCMP(tr, foldedS, foldedLen, c)<0) { gc=c->l;c->l=gc->h;gc->h=c; }
 else            { gc=c->h;c->h=gc->l;gc->l=c; }

 if(XTNCMP(tr, foldedS, foldedLen, x)<0) x->l=gc;
 else            x->h=gc;

 return(gc);
}

/************************************************************************/

static XTN *
split(tr,foldedS,foldedLen,gg,g,p,x)            /* page 226 sedgewick v2 */
XTREE *tr;
byte  *foldedS;
size_t foldedLen;
XTN *gg,*g,*p,*x;
{
 XTNCMP_SCRATCH_VARS;

 x->red=1;x->l->red=0;x->h->red=0;
 if(p->red)
    {
     dumptree(tr,tr->root);
     g->red=1;
     if((XTNCMP(tr, foldedS, foldedLen, g)<0) !=
        (XTNCMP(tr, foldedS, foldedLen, p)<0))
         {
          p=rotate(tr, foldedS, foldedLen, g);
          dumptree(tr,tr->root);
         }
     x=rotate(tr, foldedS, foldedLen, gg);
     x->red=0;
     dumptree(tr,tr->root);
    }
 tr->root->h->red=0;
 return(x);
}

/************************************************************************/

int     /* 1 if added, 0 if failed, -1 if added but memory was exceeded */
incxtree(tr,s,len)                             /* page 221 sedgewick v2 */
XTREE *tr;
byte  *s;
size_t len;                           /* length of the item to be added */
{
 static CONST char    fn[] = "incxtree";
 XTN *gg,*g,*p,*z,*x;
 int cmp, ret;
 XTNCMP_SCRATCH_VARS;
 FOLD_VARS(s, len);

 if (tr->stkcur >= 0)                   /* KNG 990910 no mods while walking */
   {
     txpmbuf_putmsg(tr->pmbuf, MERR + UGE, fn,
                    "Cannot modify xtree while walking it");
     goto err;
   }

 FOLD_INIT(tr, fn, s, len, err);

 errno=ENOMEM;                                          /* MAW 04-18-91 */
 z=tr->z;
 g=p=x=tr->root;

 do {
     gg=g;g=p;p=x;
     cmp=XTNCMP(tr, foldedS, foldedLen, x);
     if(cmp==0)
       {
         if (x->cnt++ == 0) tr->numItems++;
         tr->userptr = x;
         ret = 1;
         goto done;
       }  /* PBR 05-08-93 */
     else
     if(cmp<0)  x=x->l;
     else       x=x->h;
     if(x->l->red && x->h->red)          /* split nodes on the way down */
         x=split(tr,foldedS,foldedLen,gg,g,p,x);
    } while(x!=z);
 ret = 0;
 goto done;

err:
 ret = 0;
done:
 FOLD_CLEANUP(s);
 return(ret);
}

size_t
incxtreeseq(tr, s, len, seq)
XTREE           *tr;
byte            *s;
size_t          len;
XTREESEQ        *seq;   /* (out, opt.) */
/* same as incxtree(), but returns count and gets sequence number,
 * or -1 if not found.
 */
{
  tr->userptr = NULL;
  incxtree(tr, s, len);
  if (seq != XTREESEQPN)
    {
      if (tr->userptr)                          /* inctree() found `s' */
        *seq = ((XTN *)tr->userptr)->seq;
      else if (tr->seqNum > 0)                  /* numeric sequencing */
        seq->num = (size_t)(-1);
      else                                      /* pointer sequencing */
        seq->ptr = NULL;
    }
  return(tr->userptr ? ((XTN *)tr->userptr)->cnt : (size_t)(-1));
}

/******************************************************************/

int     /* 1 if added, 0 if failed, -1 if added but memory was exceeded */
putxtree(tr,s,len)                             /* page 221 sedgewick v2 */
XTREE *tr;
byte  *s;
size_t len;                           /* length of the item to be added */
{
  static CONST char   fn[] = "putxtree";
 XTN *gg,*g,*p,*z,*x;
 int cmp, ret;
 XTNCMP_SCRATCH_VARS;
 FOLD_VARS(s, len);

 if (tr->stkcur >= 0)                   /* KNG 990910 no mods while walking */
   {
     txpmbuf_putmsg(tr->pmbuf, MERR + UGE, fn,
                    "Cannot insert into xtree while walking it");
     goto err;
   }

 FOLD_INIT(tr, fn, s, len, err);

 errno=ENOMEM;                                          /* MAW 04-18-91 */
 z=tr->z;
 g=p=x=tr->root;

 do {
     gg=g;g=p;p=x;
     cmp=XTNCMP(tr, foldedS, foldedLen, x);
     if(cmp==0)                         /* PBR 05-08-93 */
       {
         if (x->cnt++ == 0) tr->numItems++;
         tr->userptr = x;               /* KNG 990910 save for putxtreeseq()*/
         ret = 1;
         goto done;
       }
     else
     if(cmp<0)  x=x->l;
     else       x=x->h;
     if(x->l->red && x->h->red)          /* split nodes on the way down */
         x=split(tr,foldedS,foldedLen,gg,g,p,x);
    } while(x!=z);

                      /* alloc and init new node */
  if ((x = (XTN *)TXcalloc(tr->pmbuf, fn, 1, XTN_BASE_SZ + foldedLen + 1))
      != XTNPN)
    {
     x->l=z;x->h=z;
     x->cnt=1;                                         /* PBR 05-08-93 */
     if (tr->seqNum == 0)                       /* pointer sequencing */
     {
        /*x->seq=tr->seq;*/      /* MAW 07-26-96 - insertion sequence */
       x->seq.ptr = s;                 /* JMT 97-04-09 - insertion sequence */
     }
     else                                       /* numeric sequencing */
     {
        x->seq.num = tr->seqNum - 1;    /* JMT 97-04-09 - insertion seq */
        tr->seqNum++;              /* MAW 07-26-96 - insertion sequence */
     }
     memcpy(x->s,foldedS,foldedLen);            /* PBR 06-15-93 */
     x->s[foldedLen] = '\0';                    /* in case it's a string */
     x->len=foldedLen;
     if(cmp<0) p->l=x;
     else      p->h=x;
     tr->userptr = x;
     x=split(tr,x->s,x->len,gg,g,p,x);
     dumptree(tr,tr->root);
     tr->memUsed += XTN_BASE_SZ + foldedLen + 1;
     tr->numAllocatedItems++;
     tr->numItems++;
     if (tr->memUsed >= tr->maxmem)
       ret = -1;
     else
       ret = 1;
     goto done;
    }

err:
 tr->userptr = NULL;
 ret = 0;
done:
 FOLD_CLEANUP(s);
 return(ret);
}

size_t
putxtreeseq(tr, s, len, seq)
XTREE           *tr;
byte            *s;
size_t          len;
XTREESEQ        *seq;   /* (out, opt.) */
/* same as putxtree(), but sets `*seq' to sequence # assigned to the node.
 * KNG 990624
 * and returns cnt KNG 000712 or -1 if failed
 */
{
  tr->userptr = NULL;
  putxtree(tr, s, len);
  if (seq != XTREESEQPN)
    {
      if (tr->userptr)                          /* inctree() found `s' */
        *seq = ((XTN *)tr->userptr)->seq;
      else if (tr->seqNum > 0)                  /* numeric sequencing */
        seq->num = (size_t)(-1);
      else                                      /* pointer sequencing */
        seq->ptr = NULL;
    }
  return(tr->userptr ? ((XTN *)tr->userptr)->cnt : (size_t)(-1));
}

/************************************************************************/
/*  looks up an item in the tree and returns its count; -1 if not found */

size_t
getxtree(tr,s,len)
XTREE *tr;
byte  *s;
size_t len;                           /* length of the item to be added */
{
 static CONST char     fn[] = "getxtree";
 XTN *x=tr->root;
 int cmp;
 size_t ret;
 XTNCMP_SCRATCH_VARS;
 FOLD_VARS(s, len);

 FOLD_INIT(tr, fn, s, len, err);

 do {
     cmp=XTNCMP(tr, foldedS, foldedLen, x);
     if(cmp==0) { ret = x->cnt; goto done; }
     else
     if(cmp<0)  x=x->l;
     else       x=x->h;
    } while(x!=tr->z);
err:
 ret = (size_t)(-1);
done:
 FOLD_CLEANUP(s);
 return(ret);
}

/************************************************************************/
/* looks up an item and returns its count and seq, or -1 if not found */

size_t
getxtreeseq(tr,s,len,seq)
XTREE *tr;
byte  *s;
size_t len;                           /* length of the item to be found */
XTREESEQ        *seq;   /* (out, opt.) */
{
 static CONST char     fn[] = "getxtreeseq";
 XTN *x=tr->root;
 int cmp;
 size_t ret;
 XTNCMP_SCRATCH_VARS;
 FOLD_VARS(s, len);

 FOLD_INIT(tr, fn, s, len, err);

 do {
     cmp=XTNCMP(tr, foldedS, foldedLen, x);
     if(cmp==0)
       {
         if (seq) *seq = x->seq;
         ret = x->cnt;
         goto done;
       }
     else
     if(cmp<0)  x=x->l;
     else       x=x->h;
    } while(x!=tr->z);
err:
 ret = (size_t)(-1);
done:
 FOLD_CLEANUP(s);
 return(ret);
}

/************************************************************************/
   /*  WTF: doesnt actually del anything, just sets its count to 0 */

void
delxtree(tr,s,len)
XTREE *tr;
byte  *s;
size_t len;                           /* length of the item to be added */
{
 static CONST char     fn[] = "delxtree";
 XTN *x=tr->root;
 int cmp;
 XTNCMP_SCRATCH_VARS;
 FOLD_VARS(s, len);

 FOLD_INIT(tr, fn, s, len, err);

 do {
     cmp=XTNCMP(tr, s, len, x);
     if(cmp==0)
       {
         if (x->cnt > 0) tr->numItems--;
         x->cnt = 0;
         goto done;
       }
     else
     if(cmp<0)  x=x->l;
     else       x=x->h;
    } while(x!=tr->z);
done:
err:
 FOLD_CLEANUP(s);
}

size_t
delxtreeseq(tr,s,len,seq)
XTREE *tr;
byte  *s;
size_t len;                           /* length of the item to be added */
XTREESEQ        *seq;   /* (out, opt.) */
/* same as delxtree(), but returns count and sequence number or -1 if
 * not found.
 */
{
 static CONST char     fn[] = "delxtreeseq";
 XTN *x=tr->root;
 int cmp;
 size_t ret;
 XTNCMP_SCRATCH_VARS;
 FOLD_VARS(s, len);

 FOLD_INIT(tr, fn, s, len, err);

 do {
     cmp=XTNCMP(tr, s, len, x);
     if(cmp==0)
       {
	 ret = x->cnt;
	 if (seq != XTREESEQPN) *seq = x->seq;
         if (x->cnt > 0) tr->numItems--;
	 x->cnt=0;
         goto done;
       }
     else
     if(cmp<0)  x=x->l;
     else       x=x->h;
    } while(x!=tr->z);
err:
 if (seq != XTREESEQPN)
   {
     if (tr->seqNum > 0)                        /* numeric sequencing */
       seq->num = (size_t)(-1);
     else                                       /* pointer sequencing */
       seq->ptr = NULL;
   }
 ret = (size_t)(-1);
done:
 FOLD_CLEANUP(s);
 return(ret);
}

/************************************************************************/

     /* removes (zero's the entries) a string list from the xtree */

void
delxtreesl(tr,sl)
XTREE *tr;
byte  **sl; /* an array of ptrs to strings, the last one is empty ("") */
{
 for(;**sl;sl++)
    delxtree(tr,*sl,strlen((char *)*sl));
}

/************************************************************************/

static FILE *_tsfh;
static byte *_tss;
static byte *_tssend;                                   /* PBR 06-15-93 */

static void xwrtts ARGS((XTN *ts,XTN *z));
static void
xwrtts(ts,z)
XTN *ts;
XTN *z;
{
 if(_tsfh==(FILE *)NULL) return;
 if(ts==z || !ts) return;
 xwrtts(ts->l,z);
 if(_tsfh==(FILE *)NULL) return;                      /* MAW 11-15-91 */
 if(ts->cnt>0)
    {
     for(_tss=ts->s,_tssend=_tss+ts->len;
         _tss<_tssend ;
         _tss++
        )
        if(putc(*_tss,_tsfh)==EOF)
            {
             _tsfh=(FILE *)NULL;
             return;
            }
     if(putc('\n',_tsfh)==EOF)
         {
          _tsfh=(FILE *)NULL;
          return;
         }
     }
 xwrtts(ts->h,z);
}

/************************************************************************/

int
writextree(tr,fh)
XTREE *tr;
FILE *fh;
{
 _tsfh=fh;
 if(tr->root)
   {
    xwrtts(tr->root->l,tr->z);
    xwrtts(tr->root->h,tr->z);
   }
 if(_tsfh==(FILE *)NULL)
    return(0);
 return(1);
}

/************************************************************************/

static void walkxtn ARGS((XTREE *tr, XTN *ts));
static void                        /* PBR 05-08-93 added this function */
walkxtn(tr, ts)
XTREE   *tr;
XTN     *ts;
{
  if (ts == tr->z) return;
  walkxtn(tr, ts->l);
  if (ts->cnt > 0)
    {
      if (!(*tr->usercall)(tr->userptr, ts->s, ts->len, ts->cnt, ts->seq))
        return;
    }
  walkxtn(tr, ts->h);
}

/************************************************************************/

void
walkxtree(tr,callback,usr)          /* PBR 05-08-93 added this function */
XTREE   *tr;
int     (*callback) ARGS((void *userptr, byte *s, size_t len, size_t cnt,
                          XTREESEQ seq));
void    *usr;
{
  tr->usercall = callback;
  tr->userptr = usr;
  if (tr->root != XTNPN)
    {
      walkxtn(tr, tr->root->l);
      walkxtn(tr, tr->root->h);
    }
}

int
xtree_walkstart(tr, clear)
XTREE   *tr;
int     clear;
/* Starts a non-recursive in-order traversal of `tr'.  Call
 * xtree_walknext() to get each node in succession, then
 * xtree_walkstop() when done.  Note that deleting/inserting is
 * disabled during this walk, so the stack isn't corrupted.  Returns 0
 * on error.  If `clear', also clears each node while walking.
 */
{
  static CONST char     fn[] = "xtree_walkstart";
  int           depth;
  size_t        n;

  if (tr->stkcur >= 0)
    {
      txpmbuf_putmsg(tr->pmbuf, MERR + UGE, fn,
                     "Only one walk at a time per xtree permitted");
      return(0);
    }
  if (tr->root == XTNPN)                        /* empty tree */
    {
      tr->stkcur = -1;
      return(1);
    }

  /* Determine depth of stack needed; since tree is mostly balanced, 2x
   * max theoretical depth should suffice:
   */
  for (depth = 16, n = tr->numAllocatedItems; n > 0; n >>= 1, depth += 2);
  if (depth > tr->stksz)                        /* need new stack */
    {
      if (tr->stk != XTNPPN) free(tr->stk);
      if (tr->stkstate != BYTEPN) free(tr->stkstate);
      tr->stkstate = BYTEPN;
      if ((tr->stk = (XTN **)TXmalloc(tr->pmbuf, fn, depth*sizeof(XTN *)))
          == XTNPPN ||
          (tr->stkstate = (byte*)TXmalloc(tr->pmbuf, fn, depth)) == BYTEPN)
        {
          tr->stksz = 0;
          tr->stkcur = -1;                      /* not in use */
          return(0);
        }
      tr->stksz = depth;
    }
  tr->stkcur = 0;                               /* now active */
  tr->stk[0] = tr->root;
  tr->stkstate[0] = 0;
  tr->cleardump = clear;
  return(1);
}

int
xtree_walknext(tr, sp, lenp, cntp, seqp)
XTREE           *tr;
byte            **sp;
size_t          *lenp;
size_t          *cntp;
XTREESEQ        *seqp;
/* Returns next value in-order from `tr' in sp/lenp/cntp/seqp, or 0 if
 * no more values.  Basically the same as walkxtree() but using an
 * explicit stack for "recursion", and eliminating a tail recursion call.
 * Also clears node if `clear' set in xtree_walkstart().
 */
{
  static CONST char     fn[] = "xtree_walknext";
  XTN           *cur;

  while (tr->stkcur >= 0)                       /* while stack in use */
    {
      cur = tr->stk[tr->stkcur];
      switch (tr->stkstate[tr->stkcur])
        {
        case 0:                                 /* just entered this node */
          if (cur == tr->z)                     /* empty/deleted node? */
            {
              tr->stkcur--;                     /* pop one level off */
              continue;
            }
          tr->stkstate[tr->stkcur] = 1;         /* about to walk low */
          cur = cur->l;
          break;
        case 1:                                 /* walked low */
          tr->stkstate[tr->stkcur] = 0;         /* about to enter high */
          tr->stk[tr->stkcur] = cur->h;
          if (cur->cnt > 0 && cur != tr->root)
            {                                   /* return this node */
              *sp = cur->s;
              *lenp = cur->len;
              *cntp = cur->cnt;
              *seqp = cur->seq;
              if (tr->cleardump)                /* clear if requested */
                {
                  if (cur->cnt > 0) tr->numItems--;
                  cur->cnt = 0;
                }
              return(1);
            }
          continue;                             /* no push: tail recur. elim*/
        default:
          txpmbuf_putmsg(tr->pmbuf, MERR, fn,"Internal error: Unknown state");
          return(0);
        }
    
      if (++tr->stkcur >= tr->stksz)            /* push new node `cur' */
        {
          txpmbuf_putmsg(tr->pmbuf, MERR + MAE, fn, "Out of stack space");
          tr->stkcur--;
          return(0);                            /* error */
        }
      tr->stk[tr->stkcur] = cur;
      tr->stkstate[tr->stkcur] = 0;             /* entering new node */
    }
  return(0);
}

void
xtree_walkstop(tr)
XTREE   *tr;
{
  tr->stkcur = -1;
}

/************************************************************************/

static void rmwalkxtn ARGS((XTREE *, XTN *));

static void
rmwalkxtn(tr, ts)
XTREE   *tr;
XTN     *ts;
{
 if (ts == tr->z) return;
 rmwalkxtn(tr, ts->l);
 ts->l = tr->z;
 if(ts->cnt>0)
    {
     if (!(*tr->usercall)(tr->userptr, ts->s, ts->len, ts->cnt, ts->seq))
        return;
    }
 rmwalkxtn(tr, ts->h);
 if (ts->cnt > 0) tr->numItems--;
 ts->h = tr->z;
 tr->memUsed -= (XTN_BASE_SZ + ts->len + 1);
 tr->numAllocatedItems--;
 free(ts);
}

/* walks the tree and deletes the nodes as it goes */

void
rmwalkxtree(tr,callback,usr)           /* PBR 07-05-93 speed up removes */
XTREE *tr;
int (*callback) ARGS((void *userptr, byte *s, size_t len, size_t cnt,
                      XTREESEQ seq));
void *usr;
{
  static CONST char     fn[] = "rmwalkxtree";

 if (tr->stkcur >= 0)                   /* KNG 990910 no mods while walking */
   {
     txpmbuf_putmsg(tr->pmbuf, MERR + UGE, fn,
                    "Cannot delete xtree while walking it");
     return;
   }
  tr->usercall = callback;
  tr->userptr = usr;
  if(tr->root != XTNPN)
   {
    rmwalkxtn(tr, tr->root->l);
    rmwalkxtn(tr, tr->root->h);
    tr->root->l = tr->root->h = tr->z;
    tr->memUsed = sizeof(XTREE) + XTN_BASE_SZ + tr->root->len + 1;
    tr->numItems = tr->numAllocatedItems = 0;   /* all deleted */
    if (tr->stk != XTNPPN) free(tr->stk);       /* KNG 990910 */
    tr->stk = XTNPPN;
    if (tr->stkstate != BYTEPN) free(tr->stkstate);
    tr->stkstate = BYTEPN;
    tr->stksz = 0;
    tr->stkcur = -1;
   }
}

/************************************************************************/
            /* zero's count variable of a node of the xtree */

static void zeroxtn ARGS((XTREE *tr, XTN *x));
static void
zeroxtn(tr, ts)
XTREE   *tr;
XTN     *ts;
{
 if (ts == tr->z) return;
 zeroxtn(tr, ts->l);
 if (ts->cnt > 0) tr->numItems--;
 ts->cnt=0;
 zeroxtn(tr, ts->h);
}

/************************************************************************/

          /* zero's the count variable in all the xtree nodes */

void
zeroxtree(tr)
XTREE *tr;
{
 zeroxtn(tr, tr->root->l);
 zeroxtn(tr, tr->root->h);
}

