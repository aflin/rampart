#ifndef PM_FLEXPHRASE
#  define PM_FLEXPHRASE 1
#endif
#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include "sizes.h"
#include "os.h"
#include "txtypes.h"
#include "pm.h"
#include "api3.h"
#include "mm3e.h"                               /* for MM3S */
#include "unicode.h"


/**********************************************************************/
PMPHR *
closepmphr(ph)
PMPHR *ph;
{
PMPHR *t;

   if(ph!=PMPHRPN)
   {
      for(;ph->prev!=PMPHRPN;ph=ph->prev) ;
      if(ph->buf!=(byte *)NULL)
         free(ph->buf);
      for(;ph!=PMPHRPN;ph=t)
      {
         t=ph->next;
         free(ph);
      }
   }
   return(PMPHRPN);
}
/**********************************************************************/

static PMPHR *iopenpmphr ARGS((byte *s,PMPHR *ph,int *dis,int stoponstar,
                               MM3S *ms, int hyphenPhrase));
/**********************************************************************/
static PMPHR *
iopenpmphr(s,ph,dis,stoponstar,ms,hyphenPhrase)
byte *s;
PMPHR *ph;
int   *dis;
int    stoponstar;
MM3S    *ms;
int     hyphenPhrase;
{
PMPHR *nph;
int space;
CONST int       *phrCmpTab;

   *dis=0;
   /* wtf Catch-22: we do not have an official cmptab from
    * TXisSpmSearchable() until below, but cannot determine ph->term
    * for TXisSpmSearchable() until we have a cmptab here for spaces.
    * Just use standard one; should be ok:  KNG 20080418
    */
   phrCmpTab = pm_getct();
   space=phrCmpTab[' '];
   for(;phrCmpTab[*s]==space;s++) ;
   if(*s=='\0')
      return(PMPHRPN);
   if((nph=(PMPHR *)calloc(1,sizeof(PMPHR)))==PMPHRPN)
   {
      *dis=1;
      return(closepmphr(ph));
   }
   if(ph==PMPHRPN)
   {
      ph=nph;
      ph->prev=PMPHRPN;
      if((ph->buf=(byte *)strdup((char *)s))==(byte *)NULL)
      {
         *dis=1;
         return(closepmphr(ph));
      }
      s=ph->buf;
   }
   else
   {
      ph->next=nph;
      nph->prev=ph;
      nph->buf=ph->buf;
      ph=nph;
   }
   ph->nterms=0;
   ph->term=s;
   ph->remain=(byte *)NULL;
   for(;*s!='\0' && phrCmpTab[*s]!=space && !(stoponstar && *s=='*');s++) ;
   ph->len=s-ph->term;
   if(*s!='\0')
   {
      if(!(stoponstar && *s=='*'))
      {
         *s='\0';
         iopenpmphr(s+1,ph,dis,stoponstar,ms,hyphenPhrase);
         if(*dis)
            ph=PMPHRPN;
      }
      else
      {
         *s='\0';
         ph->remain=s;
      }
   }
   /* KNG 20080409 flag whether we need a Unicode search/compare: */
   ph->textsearchmode = ms->textsearchmode;
   ph->useUnicode = !TXisSpmSearchable((char *)ph->term, -1,
                                       ms->textsearchmode, hyphenPhrase,
                                       &ph->cmptab);
   if (!ph->cmptab) ph->cmptab = phrCmpTab;     /* just in case */
   return(ph);
}
/**********************************************************************/

/**********************************************************************/
PMPHR *
openpmphr(s,stoponstar,ms,hyphenPhrase)
byte *s;
int stoponstar;
MM3S    *ms;            /* (in) for settings */
int     hyphenPhrase;   /* (in) hyphenphrase value (boolean) */
{
int dis, maxl, n;
byte *remain=(byte *)NULL;
PMPHR *ph, *lt;

   ph=iopenpmphr(s,PMPHRPN,&dis,stoponstar,ms,hyphenPhrase);
   for(lt=PMPHRPN,maxl=0,n=0;ph!=PMPHRPN;ph=ph->next,n++)
   {
      if(ph->remain!=(byte *)NULL)
         remain=ph->remain;
      if(ph->len>maxl)
      {
         maxl=ph->len;
         lt=ph;
      }
   }
   if(lt!=PMPHRPN)
   {
      lt->nterms=n;
      if(remain!=(byte *)NULL)
         lt->remain=s+(remain-lt->buf);
   }
   return(lt);
}
/**********************************************************************/

/**********************************************************************/
byte *
verifyphrase(ph,bufptr,endptr,hit,hitend)
PMPHR *ph;              /* (in) phrase list to match */
byte *bufptr, *endptr;  /* (in) overall search buffer to match against */
byte *hit;              /* (in, opt.) current hit at `ph' */
byte **hitend;          /* (in opt., out) end of `hit' */
/* Matches phrase `ph' -- which may be in the middle of its phrase
 * term list -- against current `hit'/`*hitend'.  Assumes `ph' term
 * of phrase is already matched at `hit' (possibly as substring).
 * If `hit' is NULL, `ph' should be start of phrase, and the entire phrase
 * (not just before/after terms) are matched against `bufptr'.
 * Returns overall phrase hit, with `*hitend' set to phrase hit end;
 * or NULL if no match.
 */
{
register int space;
byte *h, *s;
PMPHR *t;
CONST int       *phrCmpTab = ph->cmptab;

   space=phrCmpTab[' '];
   if(hit==(byte *)NULL)
   {
      h=hit=bufptr;
      t=ph;
      goto zwordcmp;
   }
   else
   {
      h = *hitend;                              /* skip the known hit word */
      t=ph->next;
   }

   /* Match the phrase terms after `ph', looking forward: */
   for(;t!=PMPHRPN;t=t->next)                         /* scan forward */
   {
      if(h>=endptr || space!=phrCmpTab[*h])
        goto noMatch;
      for(h++;h<endptr && space==phrCmpTab[*h];h++) ;  /* skip spaces after */
zwordcmp:
      s=t->term;
      if (t->useUnicode)                        /* not SPM-searchable */
        {
          switch (TXunicodeStrFoldCmp((CONST char **)&s, -1,
                                      (CONST char **)&h, endptr - h,
                                      (t->textsearchmode | TXCFF_PREFIX)))
            {
            case 0:                             /* matches */
            case TX_STRFOLDCMP_ISPREFIX:        /* `t->term' is a prefix */
              break;
            default:                            /* no match */
              goto noMatch;
            }
        }
      else                                      /* can use SPM tables */
        {
          for(;*s;s++,h++)                      /* compare words */
            {
              if(h>=endptr || phrCmpTab[*s]!=phrCmpTab[*h])
                goto noMatch;
            }
        }
   }
   *hitend=h;

   /* Match the phrase terms before `ph', looking backward: */
   h=hit-1;
   for(t=ph->prev;t!=PMPHRPN;t=t->prev)                  /* scan back */
   {
      if(h<bufptr || space!=phrCmpTab[*h])
        goto noMatch;
      for(h--;h>=bufptr && space==phrCmpTab[*h];h--) ;   /* skip spaces */
      if (t->useUnicode)                        /* not SPM-searchable */
        {
          s = t->term + t->len;
          h++;                                  /* end of (potential) match */
          switch (TXunicodeStrFoldIsEqualBackwards((CONST char**)&s, t->len,
                                                   (CONST char**)&h, h-bufptr,
                                          (t->textsearchmode | TXCFF_PREFIX)))
            {
            case 1:                             /* matches */
            case TX_STRFOLDCMP_ISPREFIX:        /* `t->term' is a prefix */
              break;
            default:                            /* no match */
              goto noMatch;
            }
        }
      else                                      /* can use SPM tables */
        {
          s=t->term+t->len-1;
          for(;s>=t->term;s--,h--)              /* compare words */
            {
              if(h<bufptr || phrCmpTab[*s]!=phrCmpTab[*h])
                goto noMatch;
            }
        }
   }
   h++;
   goto done;

noMatch:
   h = NULL;
done:
   if (TXtraceMetamorph & TX_TMF_Phrase)
     {
       byte     *theHit = (h ? h : hit);
       char     phraseBuf[1024], contextBuf[256];

       TXphrasePrint(ph, phraseBuf, sizeof(phraseBuf));
       TXmmShowHitContext(contextBuf, sizeof(contextBuf), theHit - bufptr,
                          (hitend ? *hitend - theHit : 0), NULL, NULL, 0,
                          (char *)bufptr, endptr - bufptr);
       putmsg(MINFO, NULL,
              "verifyphrase of %s: %smatched at %+wd length %wd: `%s'",
              phraseBuf, (h ? "" : "not "), (EPI_HUGEINT)(theHit - bufptr),
              (EPI_HUGEINT)(hitend ? *hitend - theHit : 0), contextBuf);
     }
   return(h);
}
/**********************************************************************/

/**********************************************************************/
int
samephrase(ph,s)
PMPHR *ph;
byte *s;
{
byte *end;
int rc;
PMPHR *root=ph;      /* MAW 05-14-02 - remember root for remain check */

   for(;ph->prev!=PMPHRPN;ph=ph->prev) ;   /* roll back to first term */
   rc=verifyphrase(ph,s,s+strlen((char *)s),(byte *)NULL,&end)!=(byte *)NULL;
   if(rc && *end!='\0')/* match, but with more after end of last term */
#ifndef NO_ROOT_WILD    /* JMT 98-05-26 Make default wild card rooted */
      if(root->remain==(byte *)NULL || *root->remain!='*')
#endif
      rc=0;
   return(rc);
}
/**********************************************************************/

EPI_SSIZE_T
TXphrasePrint(PMPHR *ph, char *buf, EPI_SSIZE_T bufSz)
/* Prints `ph' to `buf' for messages, marking the start term with
 * curly braces.
 * Returns would-be length of `buf', ala htsnpf().
 */
{
  PMPHR *p;
  char  *bufEnd = buf + bufSz, *d, *s;

  for (p = ph; p->prev; p = p->prev);
  d = buf;
  for ( ; p; p = p->next)
    {
      /* wtf would use htsnpf() if linkable: */
      if (p->prev)                              /* space-separate terms */
        {
          if (d < bufEnd) *d = ' ';
          d++;
        }
      if (d < bufEnd) *d = (p == ph ? '{' : '[');
      d++;
      for (s = (char *)p->term; *s; s++)
        {
          if (d < bufEnd) *d = *s;
          d++;
        }
      if (d < bufEnd) *d = (p == ph ? '}' : ']');
      d++;
    }
  if (d < bufEnd) *d = '\0';
  return(d - buf);
}

#ifdef TEST

/**********************************************************************/
int
main(argc,argv)
int argc;
char **argv;
{
PMPHR *ph, *t;
byte *hit, *end;
int n=1, stoponstar=0, compare=0;
byte buf[256];

   for(--argc,++argv;argc>0 && **argv=='-';argc--,argv++)
   {
      switch(*++*argv)
      {
       case 's': stoponstar=1; break;
       case 'c': compare=1; break;
      }
   }
   if(compare)
   {
      
      if((ph=openpmphr(hit=(byte *)*argv,stoponstar))!=PMPHRPN)
      {
         for(argc--,argv++;argc>0;argc--,argv++)
         {
            printf("\"%s\"",hit);
            if(samephrase(ph,(byte *)*argv)) printf(" == ");
            else                             printf(" != ");
            printf("\"%s\"\n",*argv);
         }
         ph=closepmphr(ph);
      }
   }
   else
   {
      for(;argc>0;argc--,argv++)
      {
         if((ph=openpmphr((byte *)*argv,stoponstar))!=PMPHRPN)
         {
            printf("\"%s\" ==%d==> ",*argv,ph->nterms);
            for(t=ph;t->prev!=PMPHRPN;t=t->prev) ;
            printf("\"");
            for(;t!=PMPHRPN;t=t->next)
            {
               printf("%s[%d]%s%s%s%s",t->prev==PMPHRPN?"":".",t->len,
                      t==ph?"<":"",t->term,t==ph?">":"",
                      t->remain==(byte *)NULL?"":(char *)t->remain);
            }
            if(ph->remain!=(byte *)NULL)
               printf("\" ==> \"%s",ph->remain);
            printf("\"\n");
            while(gets((char *)buf))
            {
               if((hit=(byte *)strstri((char *)buf,(char *)ph->term))!=(byte *)NULL)
               {
                  if((hit=verifyphrase(ph,buf,buf+strlen(buf),hit,&end))!=(byte *)NULL)
                     printf("%d:hit: \"%.*s\"\n",n,end-hit,hit);
                  else
                     printf("%d:partial: %s\n",n,ph->term);
               }
               n++;
            }
            ph=closepmphr(ph);
         }
      }
   }
   return(0);
}
/**********************************************************************/
#endif                                                        /* TEST */
