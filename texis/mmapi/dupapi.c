#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "api3.h"


APICP *dupapicp ARGS((APICP *cp));
/**********************************************************************/
APICP *
dupapicp(oacp)
APICP *oacp;
{
 APICP *acp;

 if(oacp==APICPPN) return(APICPPN);
 if((acp=(APICP *)calloc(1,sizeof(APICP)))!=APICPPN)
    {
     acp->suffixproc=oacp->suffixproc;
     acp->prefixproc=oacp->prefixproc;
     acp->rebuild   =oacp->rebuild;
     acp->incsd     =oacp->incsd;
     acp->inced     =oacp->inced;
     acp->withinproc=oacp->withinproc;
     acp->suffixrev =oacp->suffixrev;
     acp->minwordlen=oacp->minwordlen;
     acp->intersects=oacp->intersects;
     acp->sdexp     =bstrdup(oacp->sdexp);
     acp->edexp     =bstrdup(oacp->edexp);
     acp->query     =(byte *)NULL;
     acp->set       =(byte **)NULL;
     acp->suffix    =blstdup(oacp->suffix);
     acp->suffixeq  =blstdup(oacp->suffixeq);
     acp->prefix    =blstdup(oacp->prefix);
     acp->noise     =blstdup(oacp->noise);
     acp->eqprefix  =bstrdup(oacp->eqprefix);
     acp->ueqprefix =bstrdup(oacp->ueqprefix);
     acp->see       =oacp->see;
     acp->keepeqvs  =oacp->keepeqvs;
     acp->keepnoise =oacp->keepnoise;
     acp->eqedit    =oacp->eqedit;
     acp->eqedit2   =oacp->eqedit2;

     acp->database  =bstrdup(oacp->database);
     acp->lowtime   =oacp->lowtime;
     acp->hightime  =oacp->hightime;
     acp->filespec  =bstrdup(oacp->filespec);
     acp->enablemm  =oacp->enablemm;
     acp->buflen    =oacp->buflen;
     acp->worddef   =(char **)blstdup((byte **)oacp->worddef);
     acp->blockdelim=(char **)blstdup((byte **)oacp->blockdelim);
     acp->blocksz   =oacp->blocksz;
     acp->blockmax  =oacp->blockmax;
     acp->maxsimult =oacp->maxsimult;
     acp->adminmsgs =oacp->adminmsgs;
     acp->allow     =(char **)blstdup((byte **)oacp->allow);
     acp->ignore    =(char **)blstdup((byte **)oacp->ignore);

     acp->maxselect =oacp->maxselect;
     acp->profile   =bstrdup(oacp->profile);
     acp->usr       =oacp->usr;

     acp->denymode    =oacp->denymode;
     acp->alpostproc  =oacp->alpostproc;
     acp->allinear    =oacp->allinear;
     acp->alwild      =oacp->alwild;
     acp->alnot       =oacp->alnot;
     acp->alwithin    =oacp->alwithin;
     acp->alintersects=oacp->alintersects;
     acp->alequivs    =oacp->alequivs;
     acp->alphrase    =oacp->alphrase;
     acp->exactphrase =oacp->exactphrase;
     acp->qminwordlen =oacp->qminwordlen;
     acp->qminprelen  =oacp->qminprelen;
     acp->qmaxsets    =oacp->qmaxsets;
     acp->qmaxsetwords=oacp->qmaxsetwords;
     acp->qmaxwords   =oacp->qmaxwords;
     acp->defsuffrm   =oacp->defsuffrm;
     acp->reqsdelim   =oacp->reqsdelim;
     acp->reqedelim   =oacp->reqedelim;
     acp->olddelim    =oacp->olddelim;
     acp->withinmode  =oacp->withinmode;
     acp->withincount =oacp->withincount;
     acp->phrasewordproc=oacp->phrasewordproc;
     acp->textsearchmode = oacp->textsearchmode;
     acp->stringcomparemode = oacp->stringcomparemode;
     acp->setqoffs    = INTPN;
     acp->setqlens    = INTPN;
     acp->originalPrefixes = CHARPPN;
     acp->sourceExprLists = CHARPPPN;

                                             /* ck the allocated ptrs */
#define BADB(a)  (oacp->a!=(byte  *)NULL && acp->a==(byte  *)NULL)
#define BADBL(a) (oacp->a!=(byte **)NULL && acp->a==(byte **)NULL)
#define BADCL(a) (oacp->a!=(char **)NULL && acp->a==(char **)NULL)
     if(BADB(sdexp)        ||
        BADB(edexp)        ||
        BADBL(suffix)      ||
        BADBL(suffixeq)    ||
        BADBL(prefix)      ||
        BADBL(noise)       ||
        BADB(eqprefix)     ||
        BADB(ueqprefix)    ||

        BADB(database)     ||
        BADB(filespec)     ||
        BADCL(worddef)     ||
        BADCL(blockdelim)  ||
        BADCL(allow)       ||
        BADCL(ignore)      ||

        BADB(profile)
     ){
        acp=closeapicp(acp);
     }
#undef BADB
#undef BADBL
#undef BADCL
    }
 return(acp);
}                                                   /* end dupapicp() */
/**********************************************************************/

/**********************************************************************/
MMAPI *
dupmmapi(MMAPI *omp, const char *query, TXbool isRankedQuery)
{
static CONST char Fn[]="dupmmapi";
MMAPI *mp;

   if(omp==MMAPIPN) return(MMAPIPN);
   if((mp=(MMAPI *)calloc(1,sizeof(MMAPI)))==MMAPIPN){
      putmsg(MERR+MAE,Fn,strerror(ENOMEM));
      goto zerr;
   }
   mp->mme=(MM3S *)NULL;
   mp->eqreal=EQVPN;
   mp->eq=omp->eq;        /* just copy equiv pointer, don't reopen it */
   if((mp->acp=dupapicp(omp->acp))==APICPPN){
      putmsg(MERR+MAE,Fn,strerror(ENOMEM));
      goto zerr;
   }
   if (query && !setmmapi(mp, query, isRankedQuery)) {
      mp->acp=closeapicp(mp->acp);
      goto zerr;
   }
   return(mp);
zerr:
   return(closemmapi(mp));
}                                                   /* end dupmmapi() */
/**********************************************************************/
