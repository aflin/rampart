#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "os.h"
#include "api3.h"
#include "mdpar.h"
#if defined(i386) && !defined(__STDC__) && !defined(STDLIB_H)
extern char *malloc ARGS((int));
extern char *calloc ARGS((int,int));
extern char *realloc ARGS((char *,int));
#endif
#ifdef _WIN32
#  define snprintf _snprintf    /* WTF is this necessary */
#endif /* _WIN32 */


/* PBR 08-07-91 Created this to allow 3db to do an mm query */
/* MAW 06-16-93 count "required"(+) items */
/* MAW 07-01-93 - make named delims externally controllable */
/* MAW 07-01-93 - make query size unlimited */
/* MAW 07-07-93 - more intelligent about what 3db can do to help */

static MDPDLM stddlim[]={
#ifdef macintosh                     /* 0x0d line end instead of 0x0a */
   {"line","$"                                  ,0,1},
   {"para","\\x0d=\\space+"                     ,1,1},
   {"sent","[^\\digit\\upper][.?!][\\space\'\"]",0,1},
   {"page","\\x0c"                              ,0,1},/* MAW 06-01-92 was x12 */
   {"vers","\\x0d=\\digit+:=\\digit+"           ,1,0},/* MAW 06-26-92 bible */
   {"buf" ,""                                   ,0,0},/* MAW 08-24-94 buffer */
   {"all" ,""                                   ,0,0},/* MAW 08-24-94 buffer */
#else
   {"line","$"                                  ,0,1},
   {"para","\\x0a=\\space+"                     ,1,1},
   {"sent","[^\\digit\\upper][.?!][\\space\'\"]",0,1},
   {"page","\\x0c"                              ,0,1},/* MAW 06-01-92 was x12 */
   {"vers","\\x0a=\\digit+:=\\digit+"           ,1,0},/* MAW 06-26-92 bible */
   {"buf" ,""                                   ,0,0},/* MAW 08-24-94 buffer */
   {"all" ,""                                   ,0,0},/* MAW 08-24-94 buffer */
#endif
   { CHARPN, NULL,                               0, 0 }
};
static MDPDLM *sdlim=stddlim, *udlim=MDPDLMPN;

/**********************************************************************/
MDPDLM *
mdpstd(nd)
MDPDLM *nd;
{
MDPDLM *ond=sdlim;

   sdlim=nd;
   return(ond);
}                                                     /* end mdpstd() */
/**********************************************************************/

/**********************************************************************/
MDPDLM *
mdpusr(nd)
MDPDLM *nd;
{
MDPDLM *ond=udlim;

   udlim=nd;
   return(ond);
}                                                     /* end mdpusr() */
/**********************************************************************/

/************************************************************************/

static int stracmp ARGS((char *a,char *b));

static int
stracmp(a,b)            /* rets true if the two are == for the length of a */
char *a,*b;
{
 for(;*a && *b && *a== *b;a++,b++);
 return(*a ? 0 : 1);
}

#define TXisspace(a) ((a < 128) && (isspace((byte)(a))))
/************************************************************************/
static char *qtok ARGS((char * *sp));
                /* MAW 05-14-92 - handle quotes like new query parser */
static char *                     /* get next token from the query line */
qtok(sp)
char **sp;
/* Returns next Metamorph token (word, phrase, equiv list, operator
 * etc.) in query `*sp', nul-terminating it ala strtok().  `*sp' is
 * advanced past the token.  `*sp' string is modified: double-quotes
 * are removed, tokens nul-terminated.
 */
{
  char  *token;
 byte *s, *d;
  int   inDoubleQuotes = 0, inParens = 0, inRexOrWithin = 0;
 /* KNG 20080940 Bug 2328: comma is a delimiter at *start* of token too,
  * if we consider it a delimiter at *end* of token in the loop below:
  */
 for(s= (byte *)*sp;*s && (TXisspace(*s) || *s == ',') ;s++);
 /* 1999-01-07 JMT (byte *) above to get high bit characters correct */
 /* MAW 03-10-99 change all to byte so all isspace work */
 if(!*s) return(CHARPN);
 token = (char *)s;
 d=s;
                                        /* MAW 11-29-95 - comma check */
  for ( ; *s && (inDoubleQuotes || inParens ||
                 (inRexOrWithin && !TXisspace(*s)) ||
                 !(TXisspace(*s) || *s==',')); s++)
    {
     if(*s==EQESC && *(s+1)) *(d++)= *(s++),*(d++)= *s; /* `\' */
     else if(*s=='"')       inDoubleQuotes = !inDoubleQuotes;
     else if(*s==EQV_CREX &&                    /* `/' */
             /* KNG 20080904 Bug 2328: do not interpret as REX if not the
              * start of the token or a `w/...' within-delimiter:
              */
             !inDoubleQuotes && !inParens &&
             (s == (byte *)*sp || TXisspace(s[-1]) ||   /* indicates REX */
              ((s[-1] == 'w' || s[-1] == 'W') &&
               (s == (byte *)*sp + 1 || TXisspace(s[-2]))))) /* within op */
       inRexOrWithin = 1, *(d++) = *s;
     else if (*s == EQV_CPPM)  inParens = 1, *(d++)= *s; /* `(' MAW 08-25-94 */
     else if (*s == EQV_CEPPM) inParens = 0, *(d++)= *s; /* `)' MAW 08-25-94 */
     else                   *(d++)= *s;
    }
 if(*s) *sp=(char *)s+1;
 else   *sp=(char *)s;
 *d='\0';
 return(token);
}

/************************************************************************/
static char *rmeqcmd ARGS((char *s));

static char *
rmeqcmd(s)                               /* remove equiv command if any */
char *s;
{
 if((*s=='-' || *s=='+' || *s=='=') && *(s+1)=='~')
    {
     *(s+1)= *s;
     s++;
    }
 else if(*s=='~') s++;
                                  /* MAW 08-25-94 - handle "(eqlist)" */
 if(*s=='(' || ((*s=='-' || *s=='+' || *s=='=') && *(s+1)=='('))
    {
     char *p;
     if(*s!='(') *(s+1)= *s;
     s++;
     for(p=s;*p && *p!=';' && *p!=',' && *p!=')';p++) ;
     *p='\0';
    }
 return(s);
}

/************************************************************************/
static int is3dbtok ARGS((byte *s));

static int
is3dbtok(s)                           /* is the token a valid 3db token */
byte *s;
/* Returns nonzero if token `s' is not negated, not a special pattern
 * matcher, and does not contain spaces.
 */
{
 if(*s=='-') return(0);
 if(*s=='+' || *s=='=') ++s;
 if(*s=='/' || *s=='%' || *s=='#') return(0);
 for(;*s;s++)
    if(TXisspace(*s))
         return(0);
 return(1);
}

/************************************************************************/
#ifdef USEWILDTOK
/* from before the days when spm could do wildcards */
static char *wildtok ARGS((char *s));

static char *
wildtok(s)                   /* handle the occurence of a * in a string */
char *s;
{
 static char tbuf[MDPQSZ];
 char *q,*p=s,*t=tbuf;

 if(*p=='+' || *p=='-' || *p=='=') *t++= *p++;
 if(*p=='/' || *p=='%' || *p=='#') return(s);    /* check for non wilds */
 if((q=strchr(s,'*'))==CHARPN) return(s);
 for(s="/[^\\alnum]";*s;t++,s++) *t= *s;           /* cat the expression */
 for(;p<q;p++,t++) *t= *p;
 *t='\0';
 return(tbuf);
}
#endif                                                  /* USEWILDTOK */
/************************************************************************/
static int getbuiltin ARGS((char *s,char **expr,int *incsd, int *inced));

static int
getbuiltin(s,expr,incsd,inced)
char *s;
char **expr;
int *incsd;
int *inced;
{
MDPDLM *d;

   if(udlim!=MDPDLMPN)
   {
      for(d=udlim;d->name!=CHARPN;d++)
      {
         if(stracmp(d->name,s)) goto zgotit;
      }
   }
   if(sdlim!=MDPDLMPN)
   {
      for(d=sdlim;d->name!=CHARPN;d++)
      {
         if(stracmp(d->name,s)) goto zgotit;
      }
   }
   return(0);
zgotit:
   *expr =d->expr;
   *incsd=d->incsd;
   *inced=d->inced;
   return(1);
}

/************************************************************************/
static int qdelims ARGS((APICP *cp, MDP *mdp,byte *s));

static int
qdelims(APICP *cp, MDP *mdp, byte *s)
/* Sets `mdp' delimiter fields: `incsd', `inced', `withincount',
 * `sdexp', `edexp' from `w/...' expression `s' (modified by `cp' settings).
 * Returns 1 if ok, 0 on error (silently if `s' is not a `w/...' expression).
 */
{
  static CONST char     fn[] = "qdelims";
  /* KNG 20080128 Bug 2028 "fake" word expression should use `*' not `+'
   * REX operator, as that is the effective behavior of
   * setuphitWithinWordRadius():
   */
 static CONST char       wordExpr[] = "[^\\alnum]*[\\alnum]*";
#define WORDEXPR_SZ     (sizeof(wordExpr) - 1)
 char *p=CHARPN, *d;
 char pbuf[EPI_OS_INT_BITS/3+7];
 int  include=0, ret, nwords, i;

 if(tolower((int)*s)=='w' && *(s+1)=='/')
   {
    if(*s=='W') include=1;
    s+=2;
   }
 else
   goto err;

 if(isdigit((int)*s))                           /* w/N */
    {
     mdp->incsd=1;
     mdp->inced=1;
     mdp->withincount = atoi((char *)s);

     /* Within-N processing does not actually use the start/ending
      * REX expressions (see setuphit... functions), but set them
      * anyway for informational purposes.  We do not really care about
      * API3WITHIN_GET_TYPE() value here, since setuphit...() will
      * deal with it, but at least report unknown mode, so that we
      * report it just once (not repeatedly for repeated setuphit() calls):
      */
     switch (API3WITHIN_GET_TYPE(cp->withinmode))
       {
       case API3WITHIN_TYPE_RADIUS:
       case API3WITHIN_TYPE_SPAN:
         break;
       default:
         putmsg(MERR + UGE, fn, "Unknown/invalid withinmode %d",
                (int)cp->withinmode);
       }
     switch (API3WITHIN_GET_UNIT(cp->withinmode))
     {
     case API3WITHINCHAR:
       /* KNG 20080128 Bug 2028: since w/N has its own setuphit... functions
        * that find both start and end delims together via `mdp->withincount',
        * set both start and end delims here, ala API3WITHINWORD:
        */
       sprintf(pbuf,".{,%d}",mdp->withincount);
       if (mdp->sdexp!=CHARPN) free(mdp->sdexp);
       mdp->sdexp = strdup(pbuf);
       if (mdp->edexp!=CHARPN) free(mdp->edexp);
       mdp->edexp = strdup(pbuf);
       break;
     case API3WITHINWORD:
       nwords = mdp->withincount;
       if(mdp->sdexp!=CHARPN) free(mdp->sdexp);
       mdp->sdexp = malloc(nwords*WORDEXPR_SZ + 1);
       for (i = 0, d = mdp->sdexp; i < nwords; i++, d += WORDEXPR_SZ)
         strcpy(d, wordExpr);
       *d = '\0';
       if(mdp->edexp!=CHARPN) free(mdp->edexp);
       mdp->edexp = malloc(nwords*WORDEXPR_SZ + 1);
       for (i = 0, d = mdp->edexp; i < nwords; i++, d += WORDEXPR_SZ)
         strcpy(d, wordExpr);
       *d = '\0';
       break;
     }
     goto ok;
    }

 /* KNG 20080128 If a previous `w/N' was set, then it set both start and end
  * delimiters; this `w/' must start anew so that we do not have a mix of
  * `w/N'-start-delim and this-`w/'-end-delim (would flummox setuphit()).
  * Bug 2028:
  */
 if (mdp->withincount > 0)                      /* had a previous `w/N' */
   {                                            /*   then clear both delims */
     if (mdp->sdexp != CHARPN)
       {
         free(mdp->sdexp);
         mdp->sdexp = CHARPN;
       }
     if (mdp->edexp != CHARPN)
       {
         free(mdp->edexp);
         mdp->edexp = CHARPN;
       }
   }
 mdp->withincount = 0;
 if(!getbuiltin((char *)s,&p,&mdp->incsd,&mdp->inced))
    {
     if(mdp->sdexp!=CHARPN)  mdp->inced=include;
     else                    mdp->incsd=mdp->inced=include;
     if(!*s) goto ok;
     p = (char *)s;
    }

 /* Set both delimiters if none set yet, else just set the end delimiter.
  * This effectively means we parse the first and last `w/' (except for
  * `w/N', see above), instead of last two (or sole) `w/'.  Probably done
  * to make it easier to override just the start (by prefixing to query)
  * or just the end delimiter (by suffixing to query):
  */
 if(mdp->edexp!=CHARPN) free(mdp->edexp);
 if(mdp->sdexp==CHARPN)
   mdp->sdexp = strdup(p);
 /* else start-delim already set, and this is the end-delim expression only */
 mdp->edexp = strdup(p);

ok:
 ret = 1;
 goto done;
err:
 ret = 0;
done:
 return(ret);
}

/************************************************************************/
static int interset ARGS((char *s));

static int
interset(s)
char *s;
{
 if(*s=='+' || *s=='-') return(0);
 return(1);
}

/************************************************************************/
#define REQ(s) (*(s)=='+'?1:0)

/************************************************************************/
static int catqtok ARGS((char *query,char *token,size_t querySz));

static int
catqtok(query, token, querySz)
char *query,*token;
size_t querySz;
/* Concatenates `token' onto end of nul-terminated `query'.
 * `query' is assumed to be `querySz' in total allocated size.
 * Space-separates tokens, and double-quotes tokens that contain
 * any of ` \t.,?'.
 * Returns 1 if ok.
 */
{
  char          *d;
  int           quoteToken;
  size_t        tokenLen;

  d = query + strlen(query);                    /* append to `query' */
  tokenLen = strlen(token);
  if ((size_t)(d - query) + tokenLen + (size_t)4 > querySz)
    return(0);                                  /* not enough room */
  if (d > query) *d++ = ' ';                    /* space-separate tokens */
  quoteToken = (!*token || strpbrk(token, " \t.,?"));/* MAW 06-22-95 - add ".,?" */
  if (quoteToken) *(d++) = '"';
  memcpy(d, token, tokenLen);
  d += tokenLen;
  if (quoteToken) *(d++) = '"';
  *d = '\0';
  return(1);
}

/************************************************************************/

MDP *
freemdp(mdp)
MDP *mdp;
{
 if(mdp->dbq  !=CHARPN) free(mdp->dbq  );
 if(mdp->mmq  !=CHARPN) free(mdp->mmq  );
 if(mdp->sdexp!=CHARPN) free(mdp->sdexp);
 if(mdp->edexp!=CHARPN) free(mdp->edexp);
 free((char *)mdp);
 return(MDPPN);
}

/************************************************************************/

/* TxMdparModifyTerms: nonzero: allow mdpar() to modify terms and their
 * location in `mdp->mmq' relative to original query, ie. add/remove
 * quotes, spaces.  Used to do this, but will break APICP.setqoff, setqlen.
 * KNG 20080903
 */
int     TxMdparModifyTerms = 0;

MDP *
mdpar(APICP *cp, const char *us)
{
 MDP  *mdp;
 char *p, *s, *dbset, *mmqDest, *tokStart;
 CONST char     *orgQuery = us;
 size_t         n;
 int i;
 int mmreq, dbreq;

 i=strlen(us)+1;
 if((s=malloc(i))==CHARPN)
    return(MDPPN);
 strcpy(s,us);
 us=s;             /* we will free us at end, s is modified by qtok() */
 if((mdp=(MDP *)calloc(1,sizeof(MDP)))==MDPPN)
    return(MDPPN);
 mdp->mmq=mdp->dbq=CHARPN;
 mdp->dbisets=mdp->mmisets=0;
 /*mdp->*/dbreq=/*mdp->*/mmreq=0;
 mdp->intersects= -1;
 mdp->edexp=mdp->sdexp=CHARPN;
 mdp->isdbable=1;                                     /* MAW 07-01-93 */
 /*i+=i/4+7;*/           /* some room for minimal expansion and " @99999" */
 i+=(i*2)+7;   /* MAW 10-09-01 - room for max expansion and " @99999" */
 if((mdp->mmq=malloc(i))==CHARPN ||
    (mdp->dbq=malloc(i))==CHARPN ||
    (dbset   =malloc(i))==CHARPN)
    return(freemdp(mdp));
 mdp->qsz=(size_t)i;
 mdp->dbq[0]=mdp->mmq[0]=dbset[0]='\0';
 mmqDest = mdp->mmq;
 while (tokStart = s, (p = qtok(&s)) != CHARPN)
    {
     if (*p == EQV_CISECT && isdigit((int)*((byte *)p+1)))
       {                                      /* `@N' */
          if (TxMdparModifyTerms)
            {
              if(!catqtok(mdp->mmq,p,mdp->qsz))
                  goto zerr;
            }
          else
            {
              /* Copy the term as-is, padding if needed to get same offset: */
              if ((size_t)(s - us) >= mdp->qsz) goto zerr;
              while (mmqDest - mdp->mmq < tokStart - us) *(mmqDest++) = ' ';
              memcpy(mmqDest, orgQuery + (tokStart - us), n = s - tokStart);
              mmqDest += n;
              *mmqDest = '\0';
            }
          mdp->intersects=atoi(p+1);
          continue;
         }
     if(qdelims(cp, mdp,(byte *)p)) continue;   /* check and process delimiters */
     if (TxMdparModifyTerms)
       {
         if(!catqtok(mdp->mmq,p,mdp->qsz))
             goto zerr;
       }
     else
       {
         /* Copy the term as-is, padding if needed to get same offset: */
         if ((size_t)(s - us) >= mdp->qsz) goto zerr;
         while (mmqDest - mdp->mmq < tokStart - us) *(mmqDest++) = ' ';
         memcpy(mmqDest, orgQuery + (tokStart - us), n = s - tokStart);
         mmqDest += n;
         *mmqDest = '\0';
       }
     mdp->mmisets+=interset(p);
     /*mdp->*/mmreq+=REQ(p);                              /* MAW 06-16-93 */
     if(is3dbtok((byte *)p))
         {
          p=rmeqcmd(p);             /* MAW 07-16-93 - remove ~ if any */
          if(REQ(p))   /* MAW 07-07-93 - separate requireds from sets */
              {
               if(!catqtok(mdp->dbq,p,mdp->qsz))
                   goto zerr;
               dbreq+=1;
              }
          else
              {
               if(!catqtok(dbset,p,mdp->qsz))
                   goto zerr;
               mdp->dbisets+=1;
              }
         }
    }
 if(mdp->intersects<0)
    mdp->intersects=mdp->mmisets-1;
 i=mdp->mmisets-mdp->dbisets;        /* find intersects for 3db query */
 if(mdp->intersects<i)
    mdp->intersects=(-1);
 else
   {
    char buf[32];
    snprintf(buf, sizeof(buf), "@%d", mdp->intersects - i);
    if(!catqtok(mdp->dbq,buf,mdp->qsz))
       goto zerr;
   }
 if(mdp->intersects<0 && dbreq==0)/* MAW 07-01-93 - can 3db help us out */
    {
     mdp->isdbable=0;
    }
 else if(mdp->intersects>=0)                          /* MAW 07-07-93 */
    {
     if(strlen(mdp->dbq)+strlen(dbset)+2>mdp->qsz) goto zerr;
     strcat(mdp->dbq," ");
     strcat(mdp->dbq,dbset);
    }
 free(dbset);
 dbset = NULL;
 free((void *)us);
 us = NULL;
 return(mdp);
zerr:
 free(dbset);
 dbset = NULL;
 free((void *)us);
 us = NULL;
 return(freemdp(mdp));
}


/************************************************************************/

#if TEST
int chkcmd ARGS((char *s));
int
chkcmd(s)
char *s;
{
 static MDPDLM *svstddlim=MDPDLMPN;
 static MDPDLM mydlim[]={
    {"sect","^Section: ",1,0},
    {"page","^Page: "   ,1,0},
    {CHARPN}
 };
 if(strcmp(s,"nodlm")==0)
    {
     if(svstddlim==MDPDLMPN) svstddlim=mdpstd(MDPDLMPN);
    }
 else if(strcmp(s,"mydlm")==0)
    {
     mdpusr(mydlim);
    }
 else if(strcmp(s,"stddlm")==0)
    {
     mdpusr(MDPDLMPN);
     if(svstddlim!=MDPDLMPN) mdpstd(svstddlim);
    }
 else
    return(0);
 return(1);
}

void main ARGS((int,char **));
void
main(argc,argv)
int argc;
char **argv;
{
 char ln[80];
 MDP *mdp;
 int abbrev=0;
 for(--argc,++argv;argc>0 && **argv=='-';argc--,argv++)
     {
      switch(*++*argv)
         {
          case 'a': abbrev=1; break;
         }
     }
 while(gets(ln))
     {
      if(chkcmd(ln))
         continue;
      if((mdp=mdpar(ln))!=MDPPN)
         {
          if(abbrev)
              printf("\"%s\" ==> \"%s\"\n",ln,mdp->mmq);
          else
              printf("MM : \"%s\"\n3DB: \"%s\"\nsd : \"%s\"\ned : \"%s\"\nincsd=%d, inced=%d\nmmsets=%d, dbsets=%d, intrsects=%d\nisdbable=%d\n\n",
                     mdp->mmq,mdp->dbq,
                     mdp->sdexp==NULL ? "NULL" : mdp->sdexp,
                     mdp->edexp==NULL ? "NULL" : mdp->edexp,
                     mdp->incsd,mdp->inced,
                     mdp->mmisets,mdp->dbisets,
                     mdp->intersects,
                     mdp->isdbable
                     /*mdp->mmreq,mdp->dbreq*/
          );
          freemdp(mdp);
         }
      else puts("ERROR:  NULL MDP");
    }
 exit(0);
}

#endif /* TEST */
/************************************************************************/
