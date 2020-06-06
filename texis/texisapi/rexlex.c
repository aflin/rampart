#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include "api3.h"
#include "rexlex.h"
#include "texint.h"

#ifndef FFSPN
#  define FFSPN         ((FFS *)NULL)
#endif

/************************************************************************/

static byte  *runrlit ARGS((RLIT *ri,byte  *buf,byte  *end));
static int  CDECL rlitcmp ARGS((CONST void *a,CONST void *b));
static int  CDECL rlitbcmp ARGS((CONST void *a,CONST void *b));

/************************************************************************/

RLEX *                                  /* release memory from the RLEX */
closerlex(rl)
RLEX *rl;
{
 int i;
 if(rl!=RLEXPN)
    {
     if(rl->ilst!=RLITPN)
         {
          for(i=0;i<rl->n;i++)
              if (rl->ilst[i].ex &&
                  rl->ilst[i].ex != TX_FFS_NO_MATCH)
                   closerex(rl->ilst[i].ex);
          rl->ilst = TXfree(rl->ilst);
         }
     rl = TXfree((void *)rl);
    }
 return(RLEXPN);
}

/************************************************************************/

void                /* resets an RexLexITem to its initial state */
resetrlex(rl, op)
RLEX *rl;
int     op;     /* SEARCHNEWBUF or BSEARCHNEWBUF */
{
 int i;
 rl->i=0;
 for(i=0;i<rl->n;i++)
    {
     rl->ilst[i].hit=(byte *)NULL;
     rl->ilst[i].end=(byte *)NULL;
     rl->ilst[i].len=0;
     rl->ilst[i].op=op;
    }
 rl->cmp = (op == SEARCHNEWBUF ? rlitcmp : rlitbcmp);
 rl->lastMatchEnd = NULL;
 rl->gotEof = TXbool_False;
}

/************************************************************************/

RLEX *          /* pass me a list of strings and I'll give you a handle */
openrlex(explst, syntax)
const char **explst;
TXrexSyntax     syntax;
{
  static CONST char fn[] = "openrlex";
 RLEX *rl=(RLEX *)TXcalloc(TXPMBUFPN, fn, 1,sizeof(RLEX));
 int i;
 if(rl!=RLEXPN)
    {

     for(rl->i=rl->n=0;*explst[rl->n]!='\0';rl->n+=1); /* count expressions */
     if (rl->n == 0) goto reset;        /* KNG 990210 don't alloc 0 */
     if (!(rl->ilst=(RLIT *)TXcalloc(TXPMBUFPN, fn, rl->n,sizeof(RLIT))))
       goto nomem;

                                                   /* init them to NULL */
     for(i=0;i<rl->n;i++) rl->ilst[i].ex=(FFS *)NULL;

     for(i=0;i<rl->n;i++)
        {
         rl->ilst[i].index=i;
         if (!rlex_addexp(rl, i, explst[i], syntax))
           return(closerlex(rl));
        }
reset:
     if (!TXrlexDoneAdding(rl)) return(closerlex(rl));
     resetrlex(rl, SEARCHNEWBUF);
    }
 else
   {
   nomem:
     rl = closerlex(rl);
   }
 return(rl);
}

RLEX *
openrlexadd(n)
int     n;
/* Like openrlex(), but for list of `n' expressions to be added one at a
 * time via rlex_addexp().
 */
{
  static CONST char   fn[] = "openrlexadd";
  RLEX          *rl;
  int           i;

  if ((rl = (RLEX *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(RLEX))) == RLEXPN)
    goto err;
  rl->n = n;
  if (n > 0)
    {
      if (!(rl->ilst = (RLIT *)TXcalloc(TXPMBUFPN, fn, n, sizeof(RLIT))))
        goto err;
      for (i = 0; i < n; i++)
        {
          rl->ilst[i].ex = FFSPN;
          rl->ilst[i].index = i;
        }
    }
  resetrlex(rl, SEARCHNEWBUF);
  goto done;

err:
  rl = closerlex(rl);
done:
  return(rl);
}

int
rlex_addexp(rl, i, ex, syntax)
RLEX    *rl;
int     i;
const char    *ex;
TXrexSyntax     syntax;
{
  static CONST char   fn[] = "rlex_addexp";

  if ((unsigned)i >= (unsigned)rl->n)
    {
      putmsg(MERR + UGE, fn, "Out-of-bounds index %d", i);
      goto err;
    }

  if (rl->ilst[i].ex && rl->ilst[i].ex != TX_FFS_NO_MATCH)
    rl->ilst[i].ex = closerex(rl->ilst[i].ex);

  /* Look for `\<nomatch\>'.  See also openrex() parse: */
  if (ex[0] == '\\' && ex[1] == '<' &&
      strncmp(ex+2, TX_REX_TOKEN_NO_MATCH, TX_REX_TOKEN_NO_MATCH_LEN) == 0 &&
      ex[2 + TX_REX_TOKEN_NO_MATCH_LEN] == '\\' &&
      ex[2 + TX_REX_TOKEN_NO_MATCH_LEN + 1] == '>' &&
      ex[2 + TX_REX_TOKEN_NO_MATCH_LEN + 2] != '>')
    {                                           /* `\<nomatch\>' at start */
      if (ex[2 + TX_REX_TOKEN_NO_MATCH_LEN + 2])
        {                                       /* has stuff after */
          putmsg(MERR + UGE, __FUNCTION__,
                 "`\\<%s\\>' only valid alone in an expression",
                 TX_REX_TOKEN_NO_MATCH);
          goto err;
        }
      rl->ilst[i].ex = TX_FFS_NO_MATCH;
    }
  /* Other `\<...\>' tokens may be meaningful to REX; pass them on */
  else if (!(rl->ilst[i].ex = openrex((byte *)ex, syntax)))
    goto err;

  return(1);
err:
  return(0);
}

TXbool
TXrlexDoneAdding(RLEX *rlex)
/* Call after all rlex_addexp() calls; compresses list to remove
 * items not added via rlex_addexp().
 * Returns false on error.
 */
{
  RLIT  *src, *dest;
  int   i;
  TXbool        ret, haveMatch = TXbool_False;

  for (i = 0, src = dest = rlex->ilst; i < rlex->n; i++, src++)
    if (src->ex)
      {
        if (src->ex != TX_FFS_NO_MATCH) haveMatch = TXbool_True;
        *(dest++) = *src;
      }
  rlex->n = dest - rlex->ilst;
  if (!haveMatch && rlex->n > 0)
    {
      /* All `\<nomatch\>' is invalid; no other expressions to negate. */
      putmsg(MERR + UGE, __FUNCTION__,
             /* Same error as in standalone/TEST rex.c: */
             "`\\<%s\\>' is only valid with other expressions",
             TX_REX_TOKEN_NO_MATCH);
      ret = TXbool_False;
    }
  else
    ret = TXbool_True;
  return(ret);
}

/************************************************************************/

static byte *                   /* runs rex and updates the rlit struct */
runrlit(ri,buf,end)
RLIT *ri;
byte *buf,*end;
{
  if (ri->ex == TX_FFS_NO_MATCH)
    {
      /* Set to NULL for caller, who will fix this up: */
      ri->hit = ri->end = NULL;
      ri->len = 0;
    }
  else if((ri->hit=getrex(ri->ex,buf,end,ri->op))!=BPNULL)
    {
     ri->len=rexsize(ri->ex);
     ri->end=ri->hit+ri->len;
     switch (ri->op)
       {
       case SEARCHNEWBUF:  ri->op = CONTINUESEARCH;     break;
       case BSEARCHNEWBUF: ri->op = BCONTINUESEARCH;    break;
       }
    }
 else
    {
     ri->len=0;
     ri->end=BPNULL;
    }
 return(ri->hit);
}

/************************************************************************/

static int CDECL                  /* compares two hit pointers together */
rlitcmp(a,b)
CONST void *a;
CONST void *b;
/* For forwards searches. */
{
  RLIT *ar=(RLIT *)a, *br=(RLIT *)b;
  int   arNoMatch = (ar->ex == TX_FFS_NO_MATCH);
  int   brNoMatch = (br->ex == TX_FFS_NO_MATCH);
 /* if the pointers are at the same offset, this will return the
    longer of the two expressions first */

  /* But TX_FFS_NO_MATCH always come first: */
  if (arNoMatch != brNoMatch) return(brNoMatch - arNoMatch);
  if (ar->hit != br->hit) return(ar->hit > br->hit ? 1 : -1);
  if (br->len != ar->len) return(br->len > ar->len ? 1 : -1);
  /* Be deterministic; use unique index as fallback.
   * Also required for TX_FFS_NO_MATCH processing:
   */
  return(ar->index > br->index ? 1 : -1);
}

static int CDECL                  /* compares two hit pointers together */
rlitbcmp(a,b)
CONST void *a;
CONST void *b;
/* For backwards searches. */
{
  RLIT *ar=(RLIT *)a, *br=(RLIT *)b;
  int   arNoMatch = (ar->ex == TX_FFS_NO_MATCH);
  int   brNoMatch = (br->ex == TX_FFS_NO_MATCH);
 /* if the pointers are at the same offset, this will return the
    longer of the two expressions first */

  /* But TX_FFS_NO_MATCH always come first: */
  if (arNoMatch != brNoMatch) return(brNoMatch - arNoMatch);
  if (ar->hit != br->hit) return(br->hit > ar->hit ? 1 : -1);
  if (br->len != ar->len) return(br->len > ar->len ? 1 : -1);
  /* Be deterministic; use unique index as fallback.
   * Also required for TX_FFS_NO_MATCH processing:
   */
  return(ar->index > br->index ? 1 : -1);
}

/************************************************************************/

int                 /* returns the length of the last matched rlex item */
rlexlen(rl)
RLEX *rl;
{
 return(rl->ilst[rl->i].len);
}

/************************************************************************/

int        /* returns the index of the expression that was last matched */
rlexn(rl)
RLEX *rl;
{
 return(rl->ilst[rl->i].index);
}

/************************************************************************/

byte *       /* returns the pointer to the text that was last matched */
rlexhit(rl)
RLEX *rl;
{
 return(rl->ilst[rl->i].hit);
}

/************************************************************************/

byte *       /* returns the pointer to the text that was last matched */
rlexfirst(rl)
RLEX *rl;
{
  RLIT  *rlit = &rl->ilst[rl->i];

  if (rlit->ex == TX_FFS_NO_MATCH) return(rlit->hit);
  return(rexfirst(rlit->ex));
}

/************************************************************************/

int           /* returns the length of the text that was last matched */
rlexflen(rl)
RLEX *rl;
{
  RLIT  *rlit = &rl->ilst[rl->i];

  if (rlit->ex == TX_FFS_NO_MATCH) return(rlit->len);
  return(rexfsize(rlit->ex));
}

/************************************************************************/

byte *
getrlex(rl,buf,end,op)
RLEX *rl;
byte *buf,*end;
int op;
{
 int  i;
 RLIT t, *rlit, *rlitNoMatch = NULL;
 byte   *ret;

 if (op == SEARCHNEWBUF || op == BSEARCHNEWBUF)
    {
      resetrlex(rl, op);                        /* reset the RLIT's */
      rl->lastMatchEnd = buf;
     for(i=0;i<rl->n;i++)                      /* run all the PM's once */
         runrlit(&rl->ilst[i],buf,end);
     if (rl->n > 1)                             /* KNG 990210 optimization */
       qsort(rl->ilst,rl->n,sizeof(RLIT),rl->cmp);/* sort by pointer loc */

      /* find the first non-null (WTF: assumes NULL < all other PTRS */
     for(rl->i=0;rl->i<rl->n && rl->ilst[rl->i].hit==BPNULL;rl->i+=1);

     /* Init TX_FFS_NO_MATCH objects; rlit[b]cmp() would have sorted
      * them up front:
      */
     rlitNoMatch = NULL;
     for (rlit = rl->ilst;
          rlit < rl->ilst + rl->i && rlit->ex == TX_FFS_NO_MATCH;
          rlit++)
       {
         if (!rlitNoMatch) rlitNoMatch = rlit;  /* remember first one */
         rlit->hit = rl->lastMatchEnd;
         if (rl->i < rl->n)                     /* have hit: end before it */
           rlit->end = rl->ilst[rl->i].hit;
         else                                   /* no hit: use entire buf */
           rlit->end = end;
         rlit->len = rlit->end - rlit->hit;
         if (rlit->len == 0) rlit->hit = rlit->end = NULL;
       }
     if (rlitNoMatch && !rlitNoMatch->hit) rlitNoMatch = NULL;

     if(rl->i>=rl->n)      /* all pointers were NULL ( NO HITS IN BUF ) */
       {
         if (rl->gotEof) goto err;
         rl->gotEof = TXbool_True;
         goto returnNoMatchHitOrErr;
       }

     /* There is at least one hit -- `rl->i' */
     rl->lastMatchEnd = rl->ilst[rl->i].end;

     /* But return preceding no-match text first, if any: */
     if (rl->i > 0 &&
         rlitNoMatch &&
         rlitNoMatch->hit < rl->ilst[rl->i].hit) /* non-empty no-match text */
       {
         rl->i = rlitNoMatch - rl->ilst;
         goto returnNoMatch;
       }

     goto returnHitOrErr;
    }

 /* [B]CONTINUESEARCH: */

 if(runrlit(&rl->ilst[rl->i],buf,end)==BPNULL)
    {
    /* this one NULLed out, so inc the index and return the next lower */
      /* Could also be another TX_FFS_NO_MATCH, which was init'd above */
      do
        rl->i++;
      /* Skip to next RLIT if this one is non-NO_MATCH and at EOF,
       * i.e. because we went back to rl->i = 0 for a previous NO_MATCH:
       */
      while (rl->i < rl->n &&
             rl->ilst[rl->i].ex != TX_FFS_NO_MATCH &&
             !rl->ilst[rl->i].hit);
     if (rl->i >= rl->n)                        /* no more hits at all */
       {
         if (rl->gotEof) goto err;
         rl->gotEof = TXbool_True;
         /* maybe return remaining buf as TX_FFS_NO_MATCH "hit": */
         goto updateAndReturn;
       }
     goto updateAndReturn;
    }

 /* `rl->i' has a new hit, and is thus also past TX_FFS_NO_MATCH objs too */
 for(i=rl->i;i<(rl->n)-1;i++) /* bubble the hit down */
    {
       /* if this one's less than the the next one, then stop */
     if(rl->cmp(&rl->ilst[i],&rl->ilst[i+1])<=0) break;
       /* otherwise swap the items around */
     t=rl->ilst[i];
     rl->ilst[i]=rl->ilst[i+1];
     rl->ilst[i+1]=t;
    }

updateAndReturn:
 /* Update TX_FFS_NO_MATCH objects, now that we know the next hit: */
 rlitNoMatch = NULL;
 for (rlit = rl->ilst;
      rlit < rl->ilst + rl->i && rlit->ex == TX_FFS_NO_MATCH;
      rlit++)
   {
     if (!rlitNoMatch) rlitNoMatch = rlit;  /* remember first one */
     rlit->hit = rl->lastMatchEnd;
     rlit->end = (rl->i < rl->n ?
                  TX_MAX(rl->ilst[rl->i].hit, rl->lastMatchEnd) : end);
     rlit->len = rlit->end - rlit->hit;
     if (rlit->len == 0) rlit->hit = rlit->end = NULL;
   }
 if (rlitNoMatch && !rlitNoMatch->hit) rlitNoMatch = NULL;
 rl->lastMatchEnd = (rl->i < rl->n ?
                     TX_MAX(rl->ilst[rl->i].end, rl->lastMatchEnd) : end);

returnNoMatchHitOrErr:
 if (rlitNoMatch)
   {
     rl->i = rlitNoMatch - rl->ilst;
     goto returnNoMatch;
   }
 /* else returnHitOrErr */

returnHitOrErr:
 ret = (rl->i < rl->n ? rl->ilst[rl->i].hit : NULL);
 goto finally;
returnNoMatch:
 ret = rlitNoMatch->hit;
 goto finally;
err:
  ret = NULL;
finally:
  return(ret);
}

/******************************************************************/

int
vokrex(rex, expr)
FFS     *rex;
char    *expr;
/* Returns 0 if REX expression is unsafe to getrex() on.  Most of
 * these are checks that should be in openrex(), IMHO -KNG 961223
 */
{
  /* checks apply to REX: */
  if (rex == TX_FFS_NO_MATCH || rex->re2) return(1);

#ifdef NOREXANCHOR
  if (rex->patsize == 0)
    {
      putmsg(MWARN + UGE, CHARPN,
             "Root REX subexpression of `%s' cannot be empty", expr);
      return(0);
    }
#endif
  for (rex = firstexp(rex); rex && rex->exclude != 0; rex = rex->next);
  if (rex == (FFS *)NULL)
    {
      /* This rex will core dump rexhit():  -KNG 961223 */
      putmsg(MWARN + UGE, CHARPN,
             "REX expression `%s' will not match anything (all \\P or \\F)",
             expr);
      return(0);
    }
  return(1);
}

/************************************************************************/

#ifdef TEST

static byte ln[8192];

int
main(argc,argv)
int argc;
char *argv[];
{
 int i;
 RLEX *rl;
 for(i=1;i<argc && *argv[i]=='-';i++)
    {
     switch(*(++argv[i]))
         {
          case  ' ' : break;
          default   : printf("%c invalid\n",*argv[i]);
          case  'h' : printf("USE %s your name here\n",argv[0]);
                      exit(1);
         }
    }
 argv[argc]="";
 if((rl=openrlex(argv))!=RLEXPN)
    {
     while(gets(ln))
         {
          byte *p;
          for(p=getrlex(rl,ln,ln+strlen(ln),SEARCHNEWBUF);
              p!=BPNULL;
              p=getrlex(rl,ln,ln+strlen(ln),CONTINUESEARCH)
             ){
               byte c;
               byte *q=p+rlexlen(rl);
               for(;p<q;p++) putchar(*p);
               putchar(' ');
              }
          putchar('\n');
         }
     closerlex(rl);
    }
 exit(0);
}

#endif /* test */
/************************************************************************/
