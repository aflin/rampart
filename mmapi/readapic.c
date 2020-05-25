#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "api3.h"
#include "mctrl.h"
#include "3dbctrl.h"
#include "cp.h"

static void lstfree ARGS((char **lst));
/**********************************************************************/
static void
lstfree(lst)                        /* frees a lst of string pointers */
char **lst;
{
int i;

   for(i=0;*lst[i]!='\0';i++) free(lst[i]);
   free(lst[i]);
   free(lst);
}                                                    /* end lstfree() */
/**********************************************************************/

static char **lstndup ARGS((int n,char **lst));
/************************************************************************/
static char **
lstndup(n,lst)
int n;
char **lst;
{
 int i;
 char **l;

 if((l=(char **)calloc(n+1,sizeof(char *)))!=CHARPPN){
    for(i=0;i<n;i++)
         {
          if((l[i]=strdup(lst[i]))==CHARPN)
              {
               for(--i;i>=0;i--) free(l[i]);
               free(l),l=CHARPPN;
               break;
              }
         }
     if((l[i]=strdup(""))==CHARPN)
         {
          for(--i;i>=0;i--) free(l[i]);
          free(l),l=CHARPPN;
         }
 }
 return(l);
}                                                    /* end lstndup() */
/**********************************************************************/

static TXbool t_suffixproc;
static TXbool t_prefixproc;
static TXbool t_rebuild;
static TXbool t_inc_sdexp;
static TXbool t_inc_edexp;
static TXbool t_withinproc;
static int    t_minwordlen;
static char  *t_sdexp;
static char  *t_edexp;
static char **t_suffix;
static char **t_suffixeq;
static char **t_prefix;
static char **t_noise;
static char **t_isassoc;
static char **t_pronoun;
static char **t_question;
static char  *t_eqprefix;
static char  *t_ueqprefix;
static TXbool t_see_reference;
static TXbool t_useequiv;
static char  *t_database;
static char  *t_timespec;
static char  *t_filespec;
static TXbool t_enablemm;
static int    t_buflen;
static char **t_worddef;
static char **t_blockdelim;
static ulong  t_blocksz;
static ulong  t_blockmax;
static char **t_allow;
static char **t_ignore;
static long   t_maxselect;

static REQUESTPARM rqp[]={ /* --==>> DON'T REARRANGE THIS LIST <<==-- */
   { Btype|DIM0|CPOPTIONAL,(char *)&t_suffixproc   ,MM_SUFFIXPROC   },/*  0 */
   { Btype|DIM0|CPOPTIONAL,(char *)&t_prefixproc   ,MM_PREFIXPROC   },/*  1 */
   { Btype|DIM0|CPOPTIONAL,(char *)&t_rebuild      ,MM_REBUILD      },/*  2 */
   { Btype|DIM0|CPOPTIONAL,(char *)&t_inc_sdexp    ,MM_INC_SDEXP    },/*  3 */
   { Btype|DIM0|CPOPTIONAL,(char *)&t_inc_edexp    ,MM_INC_EDEXP    },/*  4 */
   { Btype|DIM0|CPOPTIONAL,(char *)&t_withinproc   ,MM_WITHINPROC   },/*  5 */
   { Itype|DIM0|CPOPTIONAL,(char *)&t_minwordlen   ,MM_MINWORDLEN   },/*  6 */
   { Stype|DIM0|CPOPTIONAL,(char *)&t_sdexp        ,MM_SDEXP        },/*  7 */
   { Stype|DIM0|CPOPTIONAL,(char *)&t_edexp        ,MM_EDEXP        },/*  8 */
   { Stype|DIM1|CPOPTIONAL,(char *)&t_suffix       ,MM_SUFFIX       },/*  9 */
   { Stype|DIM1|CPOPTIONAL,(char *)&t_suffixeq     ,MM_SUFFIXEQ     },/* 10 */
   { Stype|DIM1|CPOPTIONAL,(char *)&t_prefix       ,MM_PREFIX       },/* 11 */
   { Stype|DIM1|CPOPTIONAL,(char *)&t_noise        ,MM_NOISE        },/* 12 */
   { Stype|DIM1|CPOPTIONAL,(char *)&t_isassoc      ,MM_ISASSOC      },/* 13 */
   { Stype|DIM1|CPOPTIONAL,(char *)&t_pronoun      ,MM_PRONOUN      },/* 14 */
   { Stype|DIM1|CPOPTIONAL,(char *)&t_question     ,MM_QUESTION     },/* 15 */
   { Stype|DIM0|CPOPTIONAL,(char *)&t_eqprefix     ,MM_EQPREFIX     },/* 16 */
   { Stype|DIM0|CPOPTIONAL,(char *)&t_ueqprefix    ,MM_UEQPREFIX    },/* 17 */
   { Btype|DIM0|CPOPTIONAL,(char *)&t_see_reference,MM_SEE_REFERENCE},/* 18 */
   { Btype|DIM0|CPOPTIONAL,(char *)&t_useequiv     ,MM_USEEQUIV     },/* 19 */
   { Stype|DIM0|CPOPTIONAL,(char *)&t_database     ,MM_DATABASE     },/* 20 */
   { Stype|DIM0|CPOPTIONAL,(char *)&t_timespec     ,TDB_TIMESPEC    },/* 21 */
   { Stype|DIM0|CPOPTIONAL,(char *)&t_filespec     ,TDB_FILESPEC    },/* 22 */
   { Btype|DIM0|CPOPTIONAL,(char *)&t_enablemm     ,TDB_ENABLEMM    },/* 23 */
   { Itype|DIM0|CPOPTIONAL,(char *)&t_buflen       ,TDB_BUFLEN      },/* 24 */
   { Stype|DIM1|CPOPTIONAL,(char *)&t_worddef      ,TDB_WORDDEF     },/* 25 */
   { Stype|DIM1|CPOPTIONAL,(char *)&t_blockdelim   ,TDB_BLOCKDELIM  },/* 26 */
   {ULtype|DIM0|CPOPTIONAL,(char *)&t_blocksz      ,TDB_BLOCKSZ     },/* 27 */
   {ULtype|DIM0|CPOPTIONAL,(char *)&t_blockmax     ,TDB_BLOCKMAX    },/* 28 */
   { Stype|DIM1|CPOPTIONAL,(char *)&t_allow        ,TDB_ALLOW       },/* 29 */
   { Stype|DIM1|CPOPTIONAL,(char *)&t_ignore       ,TDB_IGNORE      },/* 30 */
   { Ltype|DIM0|CPOPTIONAL,(char *)&t_maxselect    ,TDB_MAXSELECT   },/* 31 */
   {ENDLIST}
};
#define IND_SUFFIXPROC     0
#define IND_PREFIXPROC     1
#define IND_REBUILD        2
#define IND_INC_SDEXP      3
#define IND_INC_EDEXP      4
#define IND_WITHINPROC     5
#define IND_MINWORDLEN     6
#define IND_SDEXP          7
#define IND_EDEXP          8
#define IND_SUFFIX         9
#define IND_SUFFIXEQ      10
#define IND_PREFIX        11
#define IND_NOISE         12
#define IND_ISASSOC       13
#define IND_PRONOUN       14
#define IND_QUESTION      15
#define IND_EQPREFIX      16
#define IND_UEQPREFIX     17
#define IND_SEE_REFERENCE 18
#define IND_USEEQUIV      19
#define IND_DATABASE      20
#define IND_TIMESPEC      21
#define IND_FILESPEC      22
#define IND_ENABLEMM      23
#define IND_BUFLEN        24
#define IND_WORDDEF       25
#define IND_BLOCKDELIM    26
#define IND_BLOCKSZ       27
#define IND_BLOCKMAX      28
#define IND_ALLOW         29
#define IND_IGNORE        30
#define IND_MAXSELECT     31

static void boolin ARGS((int n,byte *b));
/**********************************************************************/
static void
boolin(n,b)
int n;
byte *b;
{
   if(rqp[n].found){
      *b=(byte)*(TXbool *)rqp[n].value;
   }
}                                                     /* end boolin() */
/**********************************************************************/

static void intin ARGS((int n,int *i));
/**********************************************************************/
static void
intin(n,i)
int n;
int *i;
{
   if(rqp[n].found){
      *i=(int)*(int *)rqp[n].value;
   }
}                                                      /* end intin() */
/**********************************************************************/

static void longin ARGS((int n,long *l));
/**********************************************************************/
static void
longin(n,l)
int n;
long *l;
{
   if(rqp[n].found){
      *l=(long)*(long *)rqp[n].value;
   }
}                                                     /* end longin() */
/**********************************************************************/

static void ulongin ARGS((int n,ulong *l));
/**********************************************************************/
static void
ulongin(n,l)
int n;
ulong *l;
{
   if(rqp[n].found){
      *l=(ulong)*(ulong *)rqp[n].value;
   }
}                                                    /* end ulongin() */
/**********************************************************************/

static void strin ARGS((int n,char **s));
/**********************************************************************/
static void
strin(n,s)
int n;
char **s;
{
   if(rqp[n].found){
      if(*s!=CHARPN) free(*s);
      *s=strdup(*(char **)rqp[n].value);
   }
}                                                      /* end strin() */
/**********************************************************************/

static void lstin ARGS((int,char ***,CP *));
/**********************************************************************/
static void
lstin(i,dest,cpp)
int i;
char ***dest;
CP *cpp;
{
   if(rqp[i].found){
   int n;
   char **lst;

      lst= *(char ***)rqp[i].value;
      n=numentries(cpp,(char *)lst);
      if(n>0){
         if(*dest!=CHARPPN) lstfree(*dest);
         *dest=lstndup(n-1,lst);
      }
   }
}                                                      /* end lstin() */
/**********************************************************************/

static void str2times ARGS((APICP *cp,char *s));
/**********************************************************************/
         /* handle time specifications in the form ">days" or "<days" */
	 /* JMT 96-03-29 convert to time_t from long */
static void
str2times(cp,s)
APICP *cp;
char *s;
{
time_t tim;

   switch(*s){
   case '+': case '>':                        /* more that n days ago */
      ++s;                                /* move past direction char */
      tim = (time_t)atol(s);
      tim *= (time_t)(24*60*60);                /* convert days to seconds */
      tim=time((time_t *)NULL)-tim;     /* subtract from current time */
      cp->hightime=tim;
      cp->lowtime=0L;
      break;
   case '-': case '<':                        /* less than n days ago */
      ++s;                                /* move past direction char */
      /*nobreak;*/
   default:
     tim = (time_t)atol(s);
     tim *= (time_t)(24*60*60);                 /* convert days to seconds */
      tim=time((time_t *)NULL)-tim;     /* subtract from current time */
      cp->hightime=EPI_OS_LONG_MAX;
      cp->lowtime=tim;
      break;
   }
}                                                  /* end str2times() */
/**********************************************************************/

static char **freenlst ARGS((int n,char **lst));
/**********************************************************************/
static char **
freenlst(n,lst)
int n;
char **lst;
{
char **l;

   for(l=lst;n>0;l++,n--) free(*l);
   free(lst);
   return(CHARPPN);
}                                                   /* end freenlst() */
/**********************************************************************/

static int cp2apicp ARGS((APICP *acp,CP *cpp));
/**********************************************************************/
static int
cp2apicp(acp,cpp)
APICP *acp;
CP *cpp;
{
int rc=0;
char *ts=(char *)NULL;

   boolin (IND_SUFFIXPROC   ,          &acp->suffixproc    );
   boolin (IND_PREFIXPROC   ,          &acp->prefixproc    );
   boolin (IND_REBUILD      ,          &acp->rebuild       );
   boolin (IND_INC_SDEXP    ,          &acp->incsd         );
   boolin (IND_INC_EDEXP    ,          &acp->inced         );
   boolin (IND_WITHINPROC   ,          &acp->withinproc    );
   intin  (IND_MINWORDLEN   ,          &acp->minwordlen    );
   strin  (IND_SDEXP        ,(char **) &acp->sdexp          );
   strin  (IND_EDEXP        ,(char **) &acp->edexp          );
   lstin  (IND_SUFFIX       ,(char ***)&acp->suffix     ,cpp);
   lstin  (IND_SUFFIXEQ     ,(char ***)&acp->suffixeq   ,cpp);
   lstin  (IND_PREFIX       ,(char ***)&acp->prefix     ,cpp);
   strin  (IND_EQPREFIX     ,(char **) &acp->eqprefix      );
   strin  (IND_UEQPREFIX    ,(char **) &acp->ueqprefix     );
   boolin (IND_SEE_REFERENCE,          &acp->see           );
   boolin (IND_USEEQUIV     ,          &acp->keepeqvs      );
   strin  (IND_DATABASE     ,(char **) &acp->database      );
   strin  (IND_TIMESPEC     ,          &ts                 );
   strin  (IND_FILESPEC     ,(char **) &acp->filespec      );
   boolin (IND_ENABLEMM     ,          &acp->enablemm      );
   intin  (IND_BUFLEN       ,          &acp->buflen        );
   lstin  (IND_WORDDEF      ,(char ***)&acp->worddef   ,cpp);
   lstin  (IND_BLOCKDELIM   ,(char ***)&acp->blockdelim,cpp);
   ulongin(IND_BLOCKSZ      ,          &acp->blocksz       );
   ulongin(IND_BLOCKMAX     ,          &acp->blockmax      );
   lstin  (IND_ALLOW        ,(char ***)&acp->allow     ,cpp);
   lstin  (IND_IGNORE       ,(char ***)&acp->ignore    ,cpp);
   ulongin(IND_MAXSELECT    ,          &acp->blockmax      );
   {
   int i, n, nq=0, ni=0, np=0, nn=0;
   char **lq = CHARPPN, **li = CHARPPN, **lp = CHARPPN, **ln = CHARPPN;

      if(rqp[IND_NOISE   ].found) nn=numentries(cpp,(char *)(ln=*(char ***)rqp[IND_NOISE   ].value))-1;
      if(rqp[IND_ISASSOC ].found) ni=numentries(cpp,(char *)(li=*(char ***)rqp[IND_ISASSOC ].value))-1;
      if(rqp[IND_PRONOUN ].found) np=numentries(cpp,(char *)(lp=*(char ***)rqp[IND_PRONOUN ].value))-1;
      if(rqp[IND_QUESTION].found) nq=numentries(cpp,(char *)(lq=*(char ***)rqp[IND_QUESTION].value))-1;
      n=nn+ni+np+nq;
      if(n>0){
         if(acp->noise!=BYTEPPN) lstfree((char **)acp->noise);
         if((acp->noise=(byte **)calloc(n+1,sizeof(byte *)))!=BYTEPPN){
            n=0;
#define ADDL(nl,l) for(i=0;i<(nl);i++,n++){ \
                      if((acp->noise[n]=(byte *)strdup((l)[i]))==BYTEPN){ \
                         acp->noise=(byte **)freenlst(n,(char **)acp->noise);rc=1;goto znerr; \
                      } \
                   }
            ADDL(nq,lq);
            ADDL(ni,li);
            ADDL(np,lp);
            ADDL(nn,ln);
#undef ADDL
            if((acp->noise[n]=(byte *)strdup(""))==BYTEPN){
               acp->noise=(byte **)freenlst(n,(char **)acp->noise);rc=1;goto znerr;
            }
znerr:;
         }
      }
   }
   if(acp->minwordlen>99)     acp->minwordlen=99;
   else if(acp->minwordlen<3) acp->minwordlen=3;
   if(ts!=(char *)NULL){
      str2times(acp,ts);
      free(ts);
   }
   return(rc);
}                                                   /* end cp2apicp() */
/**********************************************************************/

int readapicp ARGS((APICP *acp,char *profile));
/**********************************************************************/
int
readapicp(acp,profile)
APICP *acp;
char *profile;
{
CP *cpp;
char *msg;
static CONST char Fn[]="readapicp";

 if(profile==CHARPN) profile="profile.mm3";
 if((profile=proffind(profile))!=(char *)NULL){
    if((cpp=opencp(profile,"r"))==(CP *)NULL){
       putmsg(MWARN,Fn,"Can't read profile %s",profile);
    }else{
       if((msg=readcp(cpp,rqp))==(char *)NULL){
          if(cp2apicp(acp,cpp)!=0){
             putmsg(MWARN+MAE,Fn,"%s for profile %s",sysmsg(ENOMEM),profile);
          }else{
             if(acp->profile!=(byte *)NULL) free(acp->profile);
             acp->profile=(byte *)profile;
             profile=(char *)NULL;
          }
       }else{
          putmsg(MWARN,Fn,"Can't read profile %s: %s",profile,msg);
       }
       closecp(cpp);
    }
    if(profile!=(char *)NULL) free(profile);
 }
 return(1);
}                                                  /* end readapicp() */
/************************************************************************/
