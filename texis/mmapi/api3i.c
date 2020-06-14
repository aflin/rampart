#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#ifndef macintosh
#  ifdef __TSC__
#     include <types.h>
#  else
#     include <sys/types.h>
#  endif
#endif
#include "api3.h"
#include "mdpar.h"

#if defined(MSDOS) || defined(macintosh)
#  if !defined(FOROS2) && !defined(MBUNLIMITED)
#     ifndef PROTECT_IT
#        define PROTECT_IT
#     endif
#  endif
#endif
#include "box.h"
#include "bugoff.h"
#if defined(FOROS2) || defined(_WIN32) || defined(__TSC__)/* MAW 06-06-91 */
#  undef bugoff
#  undef bugon
#  define bugoff()
#  define bugon()
#endif

/**********************************************************************/
int
acpdeny(acp,feature)                                  /* MAW 06-02-98 */
APICP *acp;
char *feature;
{
int mode=acp->denymode;

   if(mode==API3DENYSILENT) return(0);
   putmsg((mode==API3DENYERROR?MERR:MWARN)+UGE,CHARPN,
          "'%s' not allowed in query",feature);
   return(mode==API3DENYERROR?1:0);
}                                                    /* end acpdeny() */
/**********************************************************************/

/**********************************************************************/
#ifdef EXPIRE_MMAPI
error define EXPIRE_C_DATE yyyymmdd instead of EXPIRE_MMAPI;
#endif
#ifdef EXPIRE_C_DATE
static int productexpired ARGS((void));
static int
productexpired()
{
time_t now, exp;
struct tm *tms, myTm;

   time(&now);
   tms=localtime(&now);
   if (tms) myTm = *tms;
   else
     {
       putmsg(MERR, CHARPN, "Cannot get local time");
       exit(1);
     }
   myTm.tm_year=(int)(((long)EXPIRE_C_DATE)/10000L)-1900;
   myTm.tm_mon =(int)(((long)EXPIRE_C_DATE%10000L)/100L)-1;
   myTm.tm_mday=(int)((long)EXPIRE_C_DATE%100L);
   exp=mktime(&myTm);
   if(now>exp)
   {
      putmsg(MERR+UGE,CHARPN,"This product has expired. Please upgrade.");
      exit(1);
   }
   return(0);
}
#else                                                 /* !EXPIRE_C_DATE */
#  define productexpired() 0
#endif                                                /* !EXPIRE_C_DATE */
/**********************************************************************/

CONST char FAR api3cpr[]="METAMORPH 3 API\nCopyright (C) 1991, Expansion Pgms. Int'l Inc.\nThunderstone Software (tm)\n";
#define maerr() putmsg(MERR + MAE, __FUNCTION__, sysmsg(ENOMEM))
/**********************************************************************/
MM3S *
close3eapi(ms)
MM3S *ms;
{
 if(ms!=(MM3S *)NULL)
    {
	 closesels(ms);                         /* close the set elements */
	 if(ms->sdx!=(FFS *)NULL) ms->sdx = closerex(ms->sdx);
 	 if(ms->edx!=(FFS *)NULL) ms->edx = closerex(ms->edx);
	 ms->refcount--;
	 if(ms->refcount == 0)
	 {
		free((char *)ms);
	 }
    }
 return((MM3S *)NULL);
}
/**********************************************************************/

static int cntlst ARGS((char **));
/**********************************************************************/
static int
cntlst(l)
char **l;
{
 int i;
 for(i=0;*l[i];i++)
    ;
 return(i);
}
/**********************************************************************/

/************************************************************************/
MM3S *
open3eapi(acp)
APICP *acp;                                    /* api control structure */
{
 MM3S *ms=(MM3S *)calloc(1,sizeof(MM3S));
 static CONST char Fn[]="open3eapi";

 if(ms==(MM3S *)NULL)
    {
     maerr();
     goto finally;
    }
 if(productexpired())/* MAW 09-03-96 - will exit if it should and can */
    {
     free((char *)ms);
     ms = NULL;
     goto finally;
    }
 if(!isok2run(PROT_API))
    {
     free((char *)ms);
     ms = NULL;
     goto finally;
    }
             /* copy structure members */
 ms->suffixproc =(TXbool )acp->suffixproc ;
 ms->prefixproc =(TXbool )acp->prefixproc ;
 ms->rebuild    =(TXbool )acp->rebuild    ;
 ms->filter_mode= TXbool_False;
 ms->incsd      =(TXbool )acp->incsd      ;
 ms->inced      =(TXbool )acp->inced      ;
 ms->minwordlen =(int    )acp->minwordlen ;
 ms->intersects =(int    )acp->intersects ;
 ms->sdexp      =(byte * )acp->sdexp      ;
 ms->edexp      =(byte * )acp->edexp      ;
 ms->query      =(char * )acp->query      ;
 ms->set        =(byte **)acp->set        ;
 ms->suffix     =(byte **)acp->suffix     ;
 ms->prefix     =(byte **)acp->prefix     ;
 ms->defsuffrm  =(TXbool )acp->defsuffrm  ;/* JMT 1999-10-01 */
 ms->reqsdelim  =(TXbool )acp->reqsdelim  ;/* JMT 2000-02-11 */
 ms->reqedelim  =(TXbool )acp->reqedelim  ;/* JMT 2000-02-11 */
 ms->olddelim   =(TXbool )acp->olddelim   ;/* JMT 2000-02-11 */
 ms->withinmode =(int    )acp->withinmode   ;/* JMT 2004-02-27 */
 ms->withincount=(int    )acp->withincount   ;/* JMT 2004-02-27 */
 ms->phrasewordproc=(int )acp->phrasewordproc;/* KNG 2004-04-14 */
 ms->textsearchmode =     acp->textsearchmode;
 ms->stringcomparemode =  acp->stringcomparemode;
 ms->denymode   =         acp->denymode;
 ms->qmaxsetwords =       acp->qmaxsetwords;
 ms->qmaxwords  =         acp->qmaxwords;
 ms->exactphrase =        acp->exactphrase;
 ms->keepnoise  =         acp->keepnoise;

                                  /* init the prefix and suffix lists */
 ms->npre=initprefix((char **)ms->prefix, ms->textsearchmode);
 if(!acp->suffixrev)
    {
     ms->nsuf=initsuffix((char **)ms->suffix, ms->textsearchmode);
     acp->suffixrev=1;
    }
 else
     ms->nsuf=cntlst((char **)ms->suffix);

 if(!opensels(&ms))
   goto err;
 ms->refcount++;
 if ((ms->sdx = openrex(ms->sdexp, TXrexSyntax_Rex)) == (FFS *)NULL)
   goto err;
 if ((ms->edx = openrex(ms->edexp, TXrexSyntax_Rex)) == (FFS *)NULL)
   goto err;
 ms->delimseq = strcmp((char *)ms->sdexp, (char *)ms->edexp) ? 0 : 1;

 /* order optimize the sels */

 qsort((char *)ms->el,(size_t)ms->nels,sizeof(SEL *),selcmp);

 /* Report opens, now that SEL #s are sorted: */
 if (TXtraceMetamorph & TX_TMF_Open)
   {
     int        i;
     const char *pmType;
     SEL        *sel;
     void       *obj;

     for (i = 0; i < ms->nels; i++)
       {
         sel = ms->el[i];
         switch (sel->pmtype)
           {
           case PMISREX:        pmType = "REX"; obj = sel->ex;  break;
           case PMISSPM:        pmType = "SPM"; obj = sel->ss;  break;
           case PMISPPM:        pmType = "PPM"; obj = sel->ps;  break;
           case PMISNPM:        pmType = "NPM"; obj = sel->np;  break;
           case PMISXPM:        pmType = "XPM"; obj = sel->xs;  break;
           default:             pmType = "?";   obj = NULL;     break;
           }
         putmsg(MINFO, Fn,
                "Opened SEL #%d `%s' from set `%s' with %s object %p",
                i, sel->lst[0], ms->set[sel->orpos], pmType, obj);
       }
   }

#ifndef ALLOWALLNOT
 if(ms->el[0]->logic==LOGINOT)
    {
     putmsg(MERR,Fn,"Cannot allow an all NOT logic search");
     goto err;
    }
#endif
 goto finally;

err:
 ms = close3eapi(ms);
finally:
 return(ms);
}                                                  /* end open3eapi() */
/**********************************************************************/

char **
TXapi3FreeNullList(list)
char    **list;
{
  size_t        i;

  if (list != CHARPPN)
    {
      for (i = 0; list[i] != CHARPN; i++)
        free(list[i]);
      free(list);
    }
  return(CHARPPN);
}

/**********************************************************************/
int
get3eqsapi(MMAPI *mp, TXbool isRankedQuery)
/* `isRankedQuery' should be true for LIKE{P,R} queries: if true and
 * !(likepobeyintersects and (intersects set in query)), sets
 * `mp->intersects' to an arbitrary LIKEP-style minimum (which may be
 * < 0); otherwise normally (from query, APICP or max-possible).  Note
 * that actual search uses MM3S.intersects, copied from
 * `mp->intersects' indirectly in setmmapi() via APICP and open3eapi()
 * call.  Also sets `mp->qintersects'.  Returns 0 if ok.
 */
{
  int n, i, freeSet = 0, maxIntersects, nlogiand, nlogiset;
 EQV *eq=mp->eq;
 EQVLST **eql=EQVLSTPPN;
 size_t j;
 APICP  *acp = mp->acp;
 static CONST char Fn[]="get equivs";

   bugoff();                           /* disable debugger interrupts */
   acp->setqoffs = acp->setqlens = INTPN;
   if (acp->originalPrefixes)
     acp->originalPrefixes = TXapi3FreeNullList(acp->originalPrefixes);
   if (acp->sourceExprLists)
     {
       for (i = 0; acp->sourceExprLists[i] != CHARPPN; i++)
         {
           TXapi3FreeNullList(acp->sourceExprLists[i]);
           acp->sourceExprLists[i] = CHARPPN;
         }
       free(acp->sourceExprLists);
       acp->sourceExprLists = CHARPPPN;
     }

   if(!isok2run(PROT_API)) goto zerr;
#ifdef NEVER            /* MAW 04-20-92 - has its own suffix list now */
   if(mp->acp->suffixrev){
      initsuffix((char **)mp->acp->suffix);
      mp->acp->suffixrev=0;
   }
#endif
   mp->qintersects = -1;  /* MAW 05-09-95 - geteqvs will set qintersects to -1 if @# not specified */
   if (!(eql = geteqvs(eq, (char *)mp->acp->query, &mp->qintersects)))
     goto zerr;

                                                 /* call user eq edit */
   bugon();                       /* allow user to debug his routines */
   if((*mp->acp->eqedit2)(mp->acp,&eql)!=0 || eql==EQVLSTPPN){
      bugoff();
      goto zerr;
   }
   bugoff();
   rmdupeqls(eql);/* remove duplicate words */        /* MAW 04-30-92 */
   for(n=0;eql[n]->words!=CHARPPN;n++) ;             /* count entries */
   if(n==0){                                          /* MAW 05-28-92 */
                               /* PBR 10-17-96 - change MERR to MWARN */
     /* KNG 20150902 source/html/search4.src looks for this message: */
      putmsg(MWARN+UGE,Fn,"Nothing to search for in query");
      goto zerr;
   }
   if((mp->acp->set=(byte **)calloc(n+1,sizeof(byte *)))==(byte **)NULL){
      maerr();
      goto zerr;
   }
   freeSet = 1;
   if ((mp->acp->setqoffs = (int *)calloc(n + 1, sizeof(int))) == INTPN)
     {
       maerr();
       goto zerr;
     }
   if ((mp->acp->setqlens = (int *)calloc(n + 1, sizeof(int))) == INTPN)
     {
       maerr();
       goto zerr;
     }
   if ((mp->acp->originalPrefixes = (char **)calloc(n + 1, sizeof(char *))) ==
       CHARPPN)
     {
       maerr();
       goto zerr;
     }
   if ((mp->acp->sourceExprLists = (char ***)calloc(n + 1, sizeof(char**))) ==
       CHARPPPN)
     {
       maerr();
       goto zerr;
     }

   nlogiset = nlogiand = 0;
   for(i=0;i<n;i++){
      switch (eql[i]->logic)
        {
        case EQV_CSET:  nlogiset++;     break;
        case EQV_CAND:  nlogiand++;     break;
        }
      if((mp->acp->set[i]=(byte *)eqvfmt(eql[i]))==BYTEPN){
         maerr();
         goto zerr;
      }
      /* KNG 20080903 save set's query offset/length for later mminfo(): */
      mp->acp->setqoffs[i] = eql[i]->qoff;
      mp->acp->setqlens[i] = eql[i]->qlen;

      /* KNG 20100811 save original prefix, source expressions: */
      mp->acp->originalPrefixes[i] = eql[i]->originalPrefix;
      eql[i]->originalPrefix = CHARPN;          /* APICP owns it now */
      mp->acp->sourceExprLists[i] = eql[i]->sourceExprs;
      eql[i]->sourceExprs = CHARPPN;            /* APICP owns it now */
   }
   /* max intersects = # of sets - 1: */
   maxIntersects = nlogiset;
   if (maxIntersects > 0) maxIntersects--;

   /* Bug 7375: use same intersects as index search does, especially
    * its arbitrary number-of-ANDs-and-SETs-based value when
    * appropriate.  Note that latter can result in effectively @-1,
    * i.e. none of the SETs required; downstream code (findmm() etc.)
    * must deal with that and not assume it means default-intersects.
    * (Tested in Texis test676, test 677.)
    *
    * Note also that fdbi_get() and rppm_precomp() now use this
    * computed intersects, so that the logic is in one place:
    */
   if (isRankedQuery &&
       !(TXapicpGetLikepObeyIntersects() && mp->qintersects >= 0))
     {
       int      minsets;

       if (TXapicpGetLikepAllMatch())
         minsets = nlogiand + nlogiset;
       else
         /* Use index formula; see also fdbi_get(), rppm_precomp(): */
         minsets = (nlogiand + nlogiset <= 4 ? 1 :
                    (nlogiand + nlogiset <= 9 ? 2 : 4));
       if (minsets < nlogiand) minsets = nlogiand;    /* at least nlogiand */
       /* Note that this can leave mp->intersects < 0, e.g. for `+foo bar': */
       mp->intersects = (minsets - nlogiand) - 1;
       mp->intersectsIsArbitrary = TXbool_True;
     }
   else                                         /* from user, APICP or max */
     {
       if (mp->qintersects != -1)               /* "@#" specified */
         mp->intersects = mp->qintersects;
       else
         mp->intersects = mp->acp->intersects;  /* MAW 05-25-95 - APICP */
       if (mp->intersects < 0 ||                /* auto-set */
           /* Bug 7408: Always limit intersects to max possible, not just
            * when auto-setting (i.e. when no APICP intersects and no
            * query intersects); for consistency with index search:
            */
           mp->intersects > maxIntersects)
         mp->intersects = maxIntersects;
       mp->intersectsIsArbitrary = TXbool_False;
     }

   if((mp->acp->set[i]=(byte *)malloc(1))==(byte *)NULL){
      maerr();
      goto zerr;
   }
   *mp->acp->set[i]='\0';
   if(eql!=EQVLSTPPN) closeeqvlst2lst(eql);
   bugon();                           /* reenable debugger interrupts */
   return(0);
zerr:
   if (freeSet && mp->acp->set)
     {
       TXapi3FreeNullList((char **)mp->acp->set);
       mp->acp->set = NULL;
     }
   if (mp->acp->setqoffs != INTPN)
     {
       free(mp->acp->setqoffs);
       mp->acp->setqoffs = INTPN;
     }
   if (mp->acp->setqlens != INTPN)
     {
       free(mp->acp->setqlens);
       mp->acp->setqlens = INTPN;
     }
   if (mp->acp->originalPrefixes != CHARPPN)
     mp->acp->originalPrefixes = TXapi3FreeNullList(mp->acp->originalPrefixes);
   if (mp->acp->sourceExprLists != CHARPPPN)
     {
       for (j = 0; mp->acp->sourceExprLists[j] != CHARPPN; j++)
         mp->acp->sourceExprLists[j] =
           TXapi3FreeNullList(mp->acp->sourceExprLists[j]);
       free(mp->acp->sourceExprLists);
       mp->acp->sourceExprLists = CHARPPPN;
     }
   if(eql!=EQVLSTPPN) closeeqvlst2lst(eql);
   bugon();                           /* reenable debugger interrupts */
   return(-1);
}                                                 /* end get3eqsapi() */
/**********************************************************************/

/**********************************************************************/
int
infommapi(mp,index,srchs,poff,plen)
MMAPI *mp;
int index;
char **srchs;
char **poff;
int  *plen;
{
 MM3S *ms=mp->mme;

 switch(index)
    {
     case 0 :
          *srchs=(char *)ms->query;
          *poff= (char *)ms->hit;
          *plen=  ms->hitsz;
          return(1);
     case 1 :
          *srchs=(char *)mp->acp->sdexp;
          *poff= (char *)rexhit(ms->sdx);
          *plen=         rexsize(ms->sdx);
          return(1);
     case 2 :
          *srchs=(char *)mp->acp->edexp;
          *poff= (char *)rexhit(ms->edx);
          *plen=         rexsize(ms->edx);
          return(1);
     default :
          if(index<0) return(-1);
          index-=3;                 /* subtract the first cases */
          if(index<ms->nels)
             {
              int i;
              for(i=0;i<ms->nels;i++)
                   if(ms->el[i]->member && --index<0)
                        break;
              if(i==ms->nels) return(0);                /* no more sets */
              *srchs=(char *)ms->el[i]->srchs;
              *poff=(char *)ms->el[i]->hit;
              *plen=ms->el[i]->hitsz;
              return(1);
             }
         else return(0);
    }
 return(0);                  /* not required but keeps compiler quiet */
}
/**********************************************************************/

/**********************************************************************/
byte *
getmmdelims(const byte *query, APICP *acp)
/* Sets APICP.{sdexp,edexp,incsd,inced,withincount} if specified in `query'.
 * Yaps and fails if `w/...' used but not allowed.
 * Returns alloced copy of `query', sans `w/...' operators.
 */
{
MDP *md;
byte *mmq;
int sayerror=1;

   md=mdpar(acp, (char *)query);
   if(md==MDPPN) goto zerr;
   if((mmq=(byte *)strdup(md->mmq))==(byte *)NULL) goto zerr;
   if(!acp->alwithin && (md->sdexp!=CHARPN || md->edexp!=CHARPN)){
      sayerror=0;
      if(acpdeny(acp,"delimiters")) goto zerr;
      goto zret;
   }
   if(md->sdexp!=CHARPN){
      if(acp->sdexp!=(byte *)NULL) free((char *)acp->sdexp);
      acp->sdexp=(byte *)strdup(md->sdexp);
      acp->incsd=(byte)md->incsd;
   }
   if(md->edexp!=CHARPN){
      if(acp->edexp!=(byte *)NULL) free((char *)acp->edexp);
      acp->edexp=(byte *)strdup(md->edexp);
      acp->inced=(byte)md->inced;
   }
   acp->withincount = md->withincount;
zret:
   freemdp(md);
   return(mmq);
zerr:
   if(md!=MDPPN) freemdp(md);
   if(sayerror)
      maerr();
   return((byte *)NULL);
}                                                /* end getmmdelims() */
/**********************************************************************/
