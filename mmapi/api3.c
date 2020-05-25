#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>               /* or values.h if not have limits.h */
#include <sys/types.h>
#include "api3.h"
#include "unicode.h"
#if defined(_WIN32) && defined(GENERIC_READ)
#  define fread(b,s,c,f) wfread(b,s,c,f)
   static int
   wfread(char *buf,int sz,int cnt,FHTYPE fh)
   {
   DWORD nr;

      if(!ReadFile(fh,(LPVOID)buf,(DWORD)sz*(DWORD)cnt,&nr,(LPOVERLAPPED)NULL))
      {
      static char m[100];

         sprintf(m,"lasterror=0x%lx",(unsigned long)GetLastError());
         MessageBox(NULL,m,"ReadFile error",MB_OK);
         nr=(-1);
      }
      return((int)nr);
   }
#endif

static int nulleqedit ARGS((APICP *acp));
static int nulleqedit2 ARGS((APICP *acp,EQVLST ***eql));

/* Pointer to TXlicAddMetamorph(), for license check.  Pointer instead of
 * direct call, to avoid link issues:
 */
int     (*TxSetmmapiValidateFunc) ARGS((TXPMBUF *pmbuf)) = NULL;

/* ------------------------------------------------------------------------ */
/* APICP defaults: */

#undef BP
#define BP
static CONST byte               TxSdExpDefault[] = API3SDEXP;
static CONST byte               TxEdExpDefault[] = API3EDEXP;
CONST byte                      TxEqPrefixDefault[] = API3EQPREFIX;
CONST byte                      TxUeqPrefixDefault[] = API3UEQPREFIX;
static CONST byte               TxDatabaseDefault[] = R3DB_DATABASE;
static CONST byte               TxFilespecDefault[] = R3DB_FILESPEC;
static CONST byte               TxProfileDefault[] = API3PROFILE;
#define IS_CONST_STR(s)                                         \
  ((s) == TxSdExpDefault || (s) == TxEdExpDefault ||            \
   (s) == TxEqPrefixDefault || (s) == TxUeqPrefixDefault ||     \
   (s) == TxDatabaseDefault || (s) == TxFilespecDefault ||      \
   (s) == TxProfileDefault)
#undef BP
#define BP      (byte FAR *)

static CONST byte FAR * CONST   TxSuffixDefault[] =
{
  BP"'", BP"anced", BP"ancer", BP"ances", BP"atery", BP"enced", BP"encer",
  BP"ences", BP"ibler", BP"ment", BP"ness", BP"tion", BP"able", BP"less",
  BP"sion", BP"ance", BP"ious", BP"ible", BP"ence", BP"ship", BP"ical",
  BP"ward", BP"ally", BP"atic", BP"aged", BP"ager", BP"ages", BP"ated",
  BP"ater", BP"ates", BP"iced", BP"icer", BP"ices", BP"ided", BP"ider",
  BP"ides", BP"ised", BP"ises", BP"ived", BP"ives", BP"ized", BP"izer",
  BP"izes", BP"ncy", BP"ing", BP"ion", BP"ity", BP"ous", BP"ful",
  BP"tic", BP"ish", BP"ial", BP"ory", BP"ism", BP"age", BP"ist",
  BP"ate", BP"ary", BP"ual", BP"ize", BP"ide", BP"ive", BP"ier",
  BP"ess", BP"ant", BP"ise", BP"ily", BP"ice", BP"ery", BP"ent",
  BP"end", BP"ics", BP"est", BP"ed", BP"red",
  BP"res", BP"ly", BP"er", BP"al", BP"at", BP"ic", BP"ty",
  BP"ry", BP"en", BP"nt", BP"re", BP"th", BP"es", BP"ul",
  BP"s", BP"", BYTEPN
};
static CONST byte FAR * CONST   TxSuffixEqDefault[] =
{
  BP"'", BP"s", BP"ies", BP"", BYTEPN
};
static CONST byte FAR * CONST   TxPrefixDefault[] =
{
  BP"ante", BP"anti", BP"arch", BP"auto", BP"be", BP"bi", BP"counter",
  BP"de", BP"dis", BP"em", BP"en", BP"ex", BP"extra", BP"fore",
  BP"hyper", BP"in", BP"inter", BP"mis", BP"non", BP"post", BP"pre",
  BP"pro", BP"re", BP"semi", BP"sub", BP"super", BP"ultra", BP"un",
  BP"", BYTEPN
};
static CONST byte FAR * CONST   TxNoiseDefault[] =
{
  BP"a", BP"about", BP"after", BP"again", BP"ago", BP"all",
  BP"almost", BP"also", BP"always", BP"am", BP"an",
  BP"and", BP"another", BP"any", BP"anybody", BP"anyhow", BP"anyone",
  BP"anything", BP"anyway", BP"are", BP"as", BP"at",
  BP"away", BP"back", BP"be", BP"became", BP"because", BP"been",
  BP"before", BP"being", BP"between", BP"but", BP"by", BP"came",
  BP"can", BP"cannot", BP"come", BP"could", BP"did", BP"do",
  BP"does", BP"doing", BP"done", BP"down", BP"each",
  BP"else", BP"even", BP"ever", BP"every", BP"everyone",
  BP"everything", BP"for", BP"from", BP"front", BP"get",
  BP"getting", BP"go", BP"goes", BP"going", BP"gone", BP"got",
  BP"gotten", BP"had", BP"has", BP"have",
  BP"having", BP"he", BP"her", BP"here", BP"him", BP"his",
  BP"how", BP"i", BP"if", BP"in", BP"into", BP"is",
  BP"isn't", BP"it", BP"just", BP"last", BP"least",
  BP"left", BP"less", BP"let", BP"like", BP"make", BP"many",
  BP"may", BP"maybe", BP"me", BP"mine", BP"more",
  BP"most", BP"much", BP"my", BP"myself", BP"never",
  BP"no", BP"none", BP"not", BP"now", BP"of", BP"off",
  BP"on", BP"one", BP"onto", BP"or", BP"our", BP"ourselves",
  BP"out", BP"over", BP"per", BP"put", BP"putting", BP"same",
  BP"saw", BP"see", BP"seen", BP"shall", BP"she",
  BP"should", BP"so", BP"some", BP"somebody",
  BP"someone", BP"something", BP"stand", BP"such", BP"sure", BP"take",
  BP"than", BP"that", BP"the", BP"their", BP"them",
  BP"then", BP"there", BP"these", BP"they",
  BP"this", BP"those", BP"through", BP"till", BP"to", BP"too",
  BP"two", BP"unless", BP"until", BP"up", BP"upon",
  BP"us", BP"very", BP"was",
  BP"we", BP"went", BP"were", BP"what",
  BP"what's", BP"whatever", BP"when", BP"where", BP"whether",
  BP"which", BP"while", BP"who", BP"whoever",
  BP"whom", BP"whose", BP"why", BP"will",
  BP"with", BP"within", BP"without", BP"won't",
  BP"would", BP"wouldn't", BP"yet", BP"you", BP"your",
  BP"", BYTEPN
};
static CONST char FAR * CONST   TxWordDefDefault[] =
{
  "\\alnum{2,}", "", CHARPN
};
static CONST char FAR * CONST   TxBlockDelimDefault[] =
{
  "${2,}", "", CHARPN
};
static CONST char FAR * CONST   TxAllowDefault[] =
{
  R3DB_SYSALLOW R3DB_ALLOW ""
};
static CONST char FAR * CONST   TxIgnoreDefault[] =
{
  R3DB_SYSIGNORE R3DB_IGNORE ""
};
#define IS_CONST_STRLST(s)                                      \
  ((s) == TxSuffixDefault || (s) == TxSuffixEqDefault ||        \
   (s) == TxPrefixDefault || (s) == TxNoiseDefault ||           \
   (s) == (CONST byte FAR * FAR *)TxWordDefDefault ||           \
   (s) == (CONST byte FAR * FAR *)TxBlockDelimDefault ||        \
   (s) == (CONST byte FAR * FAR *)TxAllowDefault ||             \
   (s) == (CONST byte FAR * FAR *)TxIgnoreDefault)

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#define TX_APICP_DEFAULT_VALUES                                         \
{                                                                       \
  API3SUFFIXPROC,                                                       \
  API3PREFIXPROC,                                                       \
  API3REBUILD,                                                          \
  API3INCSD,                                                            \
  API3INCED,                                                            \
  API3WITHINPROC,                                                       \
  API3SUFFIXREV,                                                        \
  API3MINWORDLEN,                                                       \
  API3INTERSECTS,                                                       \
  BP TxSdExpDefault,                                                    \
  BP TxEdExpDefault,                                                    \
  BYTEPN,                                       /* query */             \
  BYTEPPN,                                      /* set */               \
  (byte FAR * FAR *)TxSuffixDefault,                                    \
  (byte FAR * FAR *)TxSuffixEqDefault,                                  \
  (byte FAR * FAR *)TxPrefixDefault,                                    \
  (byte FAR * FAR *)TxNoiseDefault,                                     \
  BP TxEqPrefixDefault,                                                 \
  BP TxUeqPrefixDefault,                                                \
  API3SEE,                                                              \
  API3KEEPEQVS,                                                         \
  API3KEEPNOISE,                                                        \
  nulleqedit,                                                           \
  nulleqedit2,                                                          \
  BP TxDatabaseDefault,                                                 \
  R3DB_LOWTIME,                                                         \
  R3DB_HIGHTIME,                                                        \
  BP TxFilespecDefault,                                                 \
  R3DB_ENABLEMM,                                                        \
  R3DB_BUFLEN,                                                          \
  (char FAR * FAR *)TxWordDefDefault,                                   \
  (char FAR * FAR *)TxBlockDelimDefault,                                \
  R3DB_BLOCKSZ,                                                         \
  R3DB_BLOCKMAX,                                                        \
  R3DB_MAXSIMULT,                                                       \
  R3DB_ADMINMSGS,                                                       \
  (char FAR * FAR *)TxAllowDefault,                                     \
  (char FAR * FAR *)TxIgnoreDefault,                                    \
  API3MAXSELECT,                                                        \
  BP TxProfileDefault,                                                  \
  NULL,                                         /* usr */               \
  API3DENYMODE,                                                         \
  API3ALPOSTPROC,                                                       \
  API3ALLINEAR,                                                         \
  API3ALWILD,                                                           \
  API3ALNOT,                                                            \
  API3ALWITHIN,                                                         \
  API3ALINTERSECTS,                                                     \
  API3ALEQUIVS,                                                         \
  API3ALPHRASE,                                                         \
  API3EXACTPHRASE,                                                      \
  API3QMINWORDLEN,                                                      \
  API3QMINPRELEN,                                                       \
  API3QMAXSETS,                                                         \
  API3QMAXSETWORDS,                                                     \
  API3QMAXWORDS,                                                        \
  API3DEFSUFFRM,                                                        \
  API3REQSDELIM,                                                        \
  API3REQEDELIM,                                                        \
  API3OLDDELIM,                                                         \
  API3WITHINMODE,                                                       \
  0,                                            /* withincount */       \
  API3PHRASEWORDPROC,                                                   \
  TXCFF_TEXTSEARCHMODE_DEFAULT,                                         \
  TXCFF_STRINGCOMPAREMODE_DEFAULT,                                      \
  INTPN,                                        /* setqoffs */          \
  INTPN,                                        /* setqlens */          \
  CHARPPN,                                      /* originalPrefixes */  \
  CHARPPPN,                                     /* sourceExprLists */   \
}

/* Global default APICP, manipulated by [Apicp] section of texis.ini: */
APICP           TxApicpDefault = TX_APICP_DEFAULT_VALUES;
/* Which fields in `TxApicpDefault' have been set via texis.ini:
 * if `TxApicpDefault.FIELD' was set via texis.ini, then
 * `TxApicpDefaultIsFromTexisinit.FIELD' is nonzero.  Not a real APICP:
 */
APICP           TxApicpDefaultIsFromTexisIni;
/* Factory default APICP, same as above but never modified by texis.ini.
 * Would be CONST except for fixup of eqprefix/ueqprefix by TXinitapp():
 */
APICP     TxApicpBuiltinDefault = TX_APICP_DEFAULT_VALUES;
#ifndef EPI_ENABLE_APICP_6
int       TxEpiEnableApicp6 = -1;               /* 1: EPI_ENABLE_APICP_6 set*/
#endif /* !EPI_ENABLE_APICP_6 */

/* SQL likepallmatch: whether LIKE{P,R} requires all SETs 
 * if !(likepobeyintersects and `@' given):
 */
static TXbool   TXlikepAllMatch = TXbool_False;
/* SQL likepobeyintersects: whether to obey `@' operator in LIKE{P,R} query */
static TXbool   TXlikepObeyIntersects = TXbool_False;


void
TXapicpFreeDefaultStr(s)
char    *s;
{
  if (!IS_CONST_STR((CONST byte FAR *)s)) free(s);
}

void
TXapicpFreeDefaultStrLst(s)
char    **s;
{
  if (!IS_CONST_STRLST((CONST byte FAR * FAR *)s)) TXapi3FreeEmptyTermList((byte **)s);
}

/* ------------------------------------------------------------------------ */

TXbool
TXapicpGetLikepAllMatch(void)
{
  return(TXlikepAllMatch);
}

TXbool
TXapicpSetLikepAllMatch(TXbool likepAllMatch)
/* Returns false on error.
 */
{
  TXlikepAllMatch = !!likepAllMatch;
  return(TXbool_True);
}

TXbool
TXapicpGetLikepObeyIntersects(void)
{
  return(TXlikepObeyIntersects);
}

TXbool
TXapicpSetLikepObeyIntersects(TXbool likepObeyIntersects)
/* Returns false on error.
 */
{
  TXlikepObeyIntersects = !!likepObeyIntersects;
  return(TXbool_True);
}

/* ------------------------------------------------------------------------ */

#define maerr() putmsg(MERR + MAE, __FUNCTION__, sysmsg(ENOMEM))
/************************************************************************/

static void strip8 ARGS((byte *buf,int n));

static void
strip8(buf,n)                                 /* strips off the 8th bit */
register byte *buf;
int n;
{
 register byte *end;
 for(end=buf+n;buf<end;buf++)
    *buf&=0x7f;
}

/************************************************************************/

int                              /* returns number of bytes read or EOF */
rdmmapi(buf,n,fh,mp)         /* does a Metamorph legal fread of n bytes */
byte *buf;                                     /* where to put the data */
int     n;                                    /* how many bytes to read */
FHTYPE fh;                 /* the FILE * to be read from (BINARY ONLY!) */
MMAPI *mp;                        /* the API to get delimiter info from */
{
 FFS *ex=mp->mme->edx;                         /* alias the REX pointer */
 if(ex==(FFS *)NULL)                       /* end delimiter not defined */
    {
     int nr=fread((char *)buf,sizeof(char),n,fh);
     if(nr<0) putmsg(MWARN+FRE,"rdmmapi","Can't read file");
     else if(freadex_strip8) strip8(buf,nr);
     return(nr);
    }
 return(freadex(fh,buf,n,ex));         /* freadex has strip8() build in */
}

/************************************************************************/

byte *                    /* duplicates a string and rets a ptr to it */
bstrdup(s)
byte *s;
{
 byte *p;
 if(s==(byte *)NULL) return((byte *)NULL);
 p=(byte *)malloc(strlen((char *)s)+1);
 if(p!=(byte *)NULL)
      strcpy((char *)p,(char *)s);
 return(p);
}

/************************************************************************/

byte **
blstdup(slst)    /* dupes list with all strings allocated individually  */
byte **slst;
{
 int n,i;
 byte **plst;
 if(slst==(byte **)NULL) return((byte **)NULL);
 for(n=0;*slst[n]!='\0';n++);
 n++;
 plst=(byte **)calloc(n,sizeof(byte *));
 if(plst!=(byte **)NULL)
    {
     for(i=0;i<n;i++)
    {
     plst[i]=bstrdup(slst[i]);
     if(plst[i]==(byte *)NULL)
         {
          int j;
          for(j=0;j<i;j++) free(plst[j]);
          free((char *)plst);
          return((byte **)NULL);
         }
    }
    }
 return(plst);
}

/************************************************************************/

static int
nulleqedit(acp) /* this is a null eqedit function to point to in init */
APICP *acp;
{
#ifdef SHOWHOWEQEDIT
/* NOTE: this function is example code only, and is not intended to be used
         as is in an app. error checking has been abandoned for clarity. */
/* set string format:
   xrootword[;class][,equiv[;class]][...]
where:
   x        is the logic character (+ - =)
   rootword is the user's word/phrase/pattern
   class    is the optional classification for this and following words
   equiv    is an equivalent word/phrase
*/
int i;
byte **set, *p, *c, *wrd, *clas;

 puts("**************** eqedit ****************");
 for(i=0,set=acp->set;*set[i]!='\0';i++){
    printf("set[%d]=\"%s\"\n",i,set[i]);      /* display whole string */
                                         /* break out parts of string */
    printf("  logic: %c\n",*set[i]);
    clas=(byte *)strdup("");
    for(wrd=p=set[i]+1,c=BYTEPN;*p!='\0';p++){
       if(*p==';') c=p;                                  /* new class */
       else if(*p==','){                                  /* new word */
          *p='\0';
          if(c!=BYTEPN){
             free(clas);
             clas=(byte *)strdup((char *)c+1);
             *c='\0';
          }
          printf("  %s)%s\n",clas,wrd);
                                       /* important to restore string */
          *p=',';
          if(c!=BYTEPN) *c=';',c=BYTEPN;
          wrd=p+1;
       }
    }
 }
#else
 (void)acp;
#endif                                               /* !SHOWHOWEQEDIT */
 return(0);
}

/************************************************************************/

static int
nulleqedit2(acp,eqlp)/* this is a null eqedit2 function to point to in init */
APICP *acp;
EQVLST ***eqlp;
{
/* this is called before acp->eqedit, and acp->set is not setup yet */
/* you may make a new array and set *eqlp=new array */
#ifdef SHOWHOWEQEDIT
/* NOTE: this function is example code only, and is not intended to be used
         as is in an app. error checking has been abandoned for clarity. */
int i, j;
EQVLST **eql= *eqlp;
char **w, **c;

 puts("**************** eqedit2 ****************");
 for(i=0;eql[i]->words!=CHARPPN;i++){
    w=eql[i]->words;
    c=eql[i]->clas ;
    printf("logic: %c\n",eql[i]->logic);
    printf("%s)%s\n",c[0],w[0]);
    for(j=1;*w[j]!='\0';j++){
       printf("  %s)%s\n",c[j],w[j]);
    }
 }
#else
 (void)acp;
 (void)eqlp;
#endif                                               /* !SHOWHOWEQEDIT */
 return(0);
}

/************************************************************************/

byte **
TXapi3FreeEmptyTermList(lst)            /* frees a lst of string pointers */
byte **lst;
{
 int i;
 if (!lst) return(NULL);
 for (i = 0; lst[i] != BYTEPN && *lst[i] != '\0'; i++) free((char *)lst[i]);
 if (lst[i]) free((char *)lst[i]);
 free((char *)lst);
 return(NULL);
}

/************************************************************************/

APICP *
closeapicp(acp)                  /* cleans up and frees an APICP struct */
APICP *acp;
{
  size_t        i;

 if(acp!=APICPPN)
    {
     if(acp->sdexp     != (byte * )NULL) free((char *)acp->sdexp    );
     if(acp->edexp     != (byte * )NULL) free((char *)acp->edexp    );
     if(acp->eqprefix  != (byte * )NULL) free((char *)acp->eqprefix );
     if(acp->ueqprefix != (byte * )NULL) free((char *)acp->ueqprefix);
     if(acp->profile   != (byte * )NULL) free((char *)acp->profile  );
     if(acp->query     != (byte * )NULL) free((char *)acp->query    );
     if(acp->set       != (byte **)NULL) TXapi3FreeEmptyTermList(acp->set     );
     if(acp->suffix    != (byte **)NULL) TXapi3FreeEmptyTermList(acp->suffix  );
     if(acp->suffixeq  != (byte **)NULL) TXapi3FreeEmptyTermList(acp->suffixeq);
     if(acp->prefix    != (byte **)NULL) TXapi3FreeEmptyTermList(acp->prefix  );
     if(acp->noise     != (byte **)NULL) TXapi3FreeEmptyTermList(acp->noise   );
     if(acp->database  != (byte * )NULL) free(acp->database);
     if(acp->filespec  != (byte * )NULL) free(acp->filespec);
     if(acp->worddef   != (char **)NULL) TXapi3FreeEmptyTermList((byte **)acp->worddef   );
     if(acp->blockdelim!= (char **)NULL) TXapi3FreeEmptyTermList((byte **)acp->blockdelim);
     if(acp->allow     != (char **)NULL) TXapi3FreeEmptyTermList((byte **)acp->allow     );
     if(acp->ignore    != (char **)NULL) TXapi3FreeEmptyTermList((byte **)acp->ignore    );
     if(acp->setqoffs  != INTPN) free(acp->setqoffs);
     if(acp->setqlens  != INTPN) free(acp->setqlens);
     if(acp->originalPrefixes != CHARPPN)
       acp->originalPrefixes = TXapi3FreeNullList(acp->originalPrefixes);
     if(acp->sourceExprLists != CHARPPPN)
       {
         for (i = 0; acp->sourceExprLists[i] != CHARPPN; i++)
           {
             TXapi3FreeNullList(acp->sourceExprLists[i]);
             acp->sourceExprLists[i] = CHARPPN;
           }
         free(acp->sourceExprLists);
         acp->sourceExprLists = CHARPPPN;
       }
     free((char *)acp);
    }
 return(APICPPN);
}

/************************************************************************/

APICP *
openapicp()                        /* opens and inits a defacto APICP */
{                                         /* don't forget dupapicp()! */

 APICP *acp=(APICP *)calloc(1,sizeof(APICP));
 if(acp==APICPPN) return(acp);

                             /* copy and/or init all the struct stuff */
 /* KNG 20080714 there are fewer alloced pointers than non-pointers,
  * so copy entire struct for speed, and re-alloc the pointer fields:
  */
 *acp = TxApicpDefault;
 acp->sdexp      =  bstrdup(TxApicpDefault.sdexp)    ;
 acp->edexp      =  bstrdup(TxApicpDefault.edexp)    ;
 acp->eqprefix   =  bstrdup(TxApicpDefault.eqprefix) ;
 acp->ueqprefix  =  bstrdup(TxApicpDefault.ueqprefix);
 acp->profile    =  bstrdup(TxApicpDefault.profile)  ;
 acp->suffix     =  blstdup(TxApicpDefault.suffix)    ;
 acp->suffixeq   =  blstdup(TxApicpDefault.suffixeq)  ;
 acp->prefix     =  blstdup(TxApicpDefault.prefix)    ;
 acp->noise      =  blstdup(TxApicpDefault.noise)     ;
 acp->database   =  bstrdup(TxApicpDefault.database) ;
 acp->filespec   =  bstrdup(TxApicpDefault.filespec) ;
 acp->worddef    =  (char **)blstdup((byte **)TxApicpDefault.worddef);
 acp->blockdelim =  (char **)blstdup((byte **)TxApicpDefault.blockdelim);
 acp->allow      =  (char **)blstdup((byte **)TxApicpDefault.allow);
 acp->ignore     =  (char **)blstdup((byte **)TxApicpDefault.ignore);

             /* ck the allocated ptrs */
 if( acp->sdexp      == (byte * ) NULL   ||
     acp->edexp      == (byte * ) NULL   ||
     acp->eqprefix   == (byte * ) NULL   ||
     acp->ueqprefix  == (byte * ) NULL   ||
     acp->profile    == (byte * ) NULL   ||
     acp->suffix     == (byte **) NULL   ||
     acp->suffixeq   == (byte **) NULL   ||
     acp->prefix     == (byte **) NULL   ||
     acp->noise      == (byte **) NULL   ||

     acp->database   == (byte * ) NULL   ||
     acp->filespec   == (byte * ) NULL   ||
     acp->worddef    == (char **) NULL   ||
     acp->blockdelim == (char **) NULL   ||
     acp->allow      == (char **) NULL   ||
     acp->ignore     == (char **) NULL
   ) acp=closeapicp(acp);

 return(acp);
}
/************************************************************************/

static int isnoise ARGS((char **l,char *w,void *a));
/**********************************************************************/
static int
isnoise(l,w,argunused)                           /* is w a noise word */
char **l, *w;
void *argunused;
{
   (void)argunused;
   for(;**l!='\0';l++) if(strcmpi(w,*l)==0) return(1);
   return(0);
}                                                    /* end isnoise() */
/**********************************************************************/

/**********************************************************************/
void
closemmeq(mp)                                 /* close the equiv file */
MMAPI *mp;
{
   if(mp->eqreal!=EQVPN){
      closeueqv(mp->eqreal);
      mp->eq=mp->eqreal=closeeqv(mp->eqreal);
   }
}                                                  /* end closemmeq() */
/**********************************************************************/

/**********************************************************************/
int
openmmeq(mp)                                   /* open the equiv file */
MMAPI *mp;
{
int oueq=0;

   if(mp->eq==EQVPN){
      mp->eq=mp->eqreal=openeqv((char *)mp->acp->eqprefix,mp->acp);
      if(mp->eqreal==EQVPN) return(1);
      oueq=1;
   }
/* MAW 07-06-92 - always reset parameters, so can change w/o close/reopen api */
   eqvsee(mp->eq,mp->acp->see);
   eqvsufproc(mp->eq,mp->acp->suffixproc);
   eqvsuflst(mp->eq,(char **)mp->acp->suffixeq);
   eqvminwl(mp->eq,mp->acp->minwordlen);
   eqvkpeqvs(mp->eq,mp->acp->keepeqvs);
   eqvkpnoise(mp->eq,mp->acp->keepnoise);
   eqvrmdef(mp->eq,mp->acp->defsuffrm);
   eqvnoise(mp->eq,(char **)mp->acp->noise);
   eqvisnoise(mp->eq,isnoise);
   if(oueq){
      if(mp->acp->ueqprefix!=BYTEPN && *(mp->acp->ueqprefix)!='\0' &&
         fexists((char *)mp->acp->ueqprefix) &&
         openueqv(mp->eq,(char *)mp->acp->ueqprefix)==EQVPN
      ){
         putmsg(MWARN,CHARPN,"User equiv %s not opened",(char *)mp->acp->ueqprefix);
      }
   }else{
      if(mp->eq->ueq==getueqv && mp->eq->ueqarg!=VOIDPN){
         cpyeq2ueq(mp->eq);                           /* MAW 07-06-92 */
      }
   }
   return(0);
}                                                   /* end openmmeq() */
/**********************************************************************/

/**********************************************************************/
MMAPI *
closemmapi(mp)
MMAPI *mp;
{
   if(mp!=MMAPIPN){
      if(mp->mme!=(MM3S *)NULL) close3eapi(mp->mme);
      closemmeq(mp);
      free((char *)mp);
   }
   return(MMAPIPN);
}                                                 /* end closemmapi() */
/**********************************************************************/

/**********************************************************************/
MMAPI *
openmmapi(const char *query, TXbool isRankedQuery, APICP *acp)
{
MMAPI *mp;

   if((mp=(MMAPI *)calloc(1,sizeof(MMAPI)))==MMAPIPN){
      maerr();
      goto zerr;
   }
   mp->mme=(MM3S *)NULL;
   mp->acp=acp;
   mp->eq=mp->eqreal=EQVPN;
   if(openmmeq(mp)!=0) goto zerr;
   if (query && !setmmapi(mp, query, isRankedQuery)) goto zerr;
   return(mp);
zerr:
   return(closemmapi(mp));
}                                                  /* end openmmapi() */
/**********************************************************************/

/**********************************************************************/
char *
getmmapi(mp,buf,end,op)
MMAPI *mp;
byte *buf, *end;
int op;
{
  char  *ret;

   if(!mp->acp->suffixrev){
     initsuffix((char **)mp->acp->suffix, mp->acp->textsearchmode);
      mp->acp->suffixrev=1;
   }
   ret = (char *)getmm(mp->mme,buf,end,op);
  if (TXtraceMetamorph & TX_TMF_OverallHit)
    {
      MM3S      *ms = mp->mme;
      byte      *startOk = TX_MIN(ms->start, ms->end);
      char      contextBuf[256];

      TXmmSetupHitContext(ms, contextBuf, sizeof(contextBuf));
      if (ms->hit)
        putmsg(MINFO, CHARPN,
               "getmmapi of MMAPI object %p: hit at %+wd length %d: `%s'",
               mp, (EPI_HUGEINT)(ms->hit - startOk), (int)ms->hitsz,
               contextBuf);
      else
        putmsg(MINFO, CHARPN,
               "getmmapi of MMAPI object %p: no%s hits in `%s'",
               mp, (op == CONTINUESEARCH ? " more" : ""), contextBuf);
    }
  return(ret);
}                                                   /* end getmmapi() */
/**********************************************************************/

/**********************************************************************/
MMAPI *
setmmapi(MMAPI *mp, const char *query, TXbool isRankedQuery)
{
 int isects;
 size_t i;
 MMAPI  *ret;

   if(mp->mme!=(MM3S *)NULL) mp->mme=close3eapi(mp->mme);/* PBR 03-08-91 */
   if(mp->acp->set!=(byte **)NULL){
      TXapi3FreeEmptyTermList(mp->acp->set);
      mp->acp->set=(byte **)NULL;
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
       for (i = 0; mp->acp->sourceExprLists[i] != CHARPPN; i++)
         {
           TXapi3FreeNullList(mp->acp->sourceExprLists[i]);
           mp->acp->sourceExprLists[i] = CHARPPN;
         }
       free(mp->acp->sourceExprLists);
       mp->acp->sourceExprLists = CHARPPPN;
     }
   if(mp->acp->query!=(byte *)NULL)
     {
       free((char *)mp->acp->query);
       mp->acp->query = BYTEPN;
     }

#ifdef EPI_LICENSE_6
   /* License count and check: */
   if (TxSetmmapiValidateFunc && !TxSetmmapiValidateFunc(TXPMBUFPN))
     goto zerr;
#endif /* EPI_LICENSE_6 */

   if(mp->acp->withinproc){
      mp->acp->query=getmmdelims((byte *)query,mp->acp);
   }else{
      mp->acp->query=bstrdup((byte *)query);
      if(mp->acp->query==(byte *)NULL) maerr();
   }
   if(mp->acp->query==(byte *)NULL){
      goto zerr;
   }
   if(openmmeq(mp)!=0) goto zerr;   /* make sure equiv file is opened */

   /* Look up equivs for query.  Also set `mp->[q]intersects': */
   if (get3eqsapi(mp, isRankedQuery) != 0) goto zerr;

   if(mp->acp->withinproc){               /* restore query as entered */
      free((char *)mp->acp->query);
      mp->acp->query=bstrdup((byte *)query);
      if(mp->acp->query==(byte *)NULL){
         maerr();
         goto zerr;
      }
   }
   if((*mp->acp->eqedit)(mp->acp)!=0) goto zerr; /* call user eq edit */
   isects=mp->acp->intersects;/* MAW 05-25-95 - preserve APICP.intersects */
   mp->acp->intersects=mp->intersects;                /* MAW 05-25-95 */
   mp->mme=open3eapi(mp->acp);
   mp->acp->intersects=isects;                        /* MAW 05-25-95 */
   if(mp->mme==(MM3S *)NULL) goto zerr;
   mp->mme->isRankedQuery = isRankedQuery;
   ret = mp;
   goto finally;

zerr:
   ret = NULL;
finally:
   if (TXtraceMetamorph & TX_TMF_Open)
     putmsg(MINFO, __FUNCTION__,
            "Set %s query `%s' with intersects %d%s for MMAPI object %p",
            (isRankedQuery ? "ranked" : "non-ranked"), query,
            (int)mp->intersects, (ret ? "" : " failed"), mp);
   return(ret);
}                                                   /* end setmmapi() */
/**********************************************************************/

/**********************************************************************/
#ifdef TEST
#include <time.h>
                                         /* define search buffer size */
#define BSZ 128000

int srchfp  ARGS((MMAPI **,int,FILE *,long *,long *));
/**********************************************************************/
int
srchfp(mp,n,fp,tnh,tnb)
MMAPI **mp;
int n;
FILE *fp;
long *tnh, *tnb;
{
static byte buf[BSZ];
char *hit;
int nr, nh=0, flag, i;
long tnr=0L;

                               /* assume that all delims are the same */
   for(;(nr=rdmmapi(buf,BSZ,fp,mp[0]))>0;tnr+=nr){ /* for each buffer */
      for(i=0;i<n;i++){                             /* for each query */
         for(flag=SEARCHNEWBUF;
             (hit=getmmapi(mp[i],buf,buf+nr,flag))!=CHARPN;
             flag=CONTINUESEARCH
         ){                                           /* for each hit */
         char *where;                          /* where was it located */
         char  *what;                         /* what item was located */
         int  length;                /* the length of the located item */

            nh++;
            infommapi(mp[i],3,&what,&where,&length);
            putmsg(MHIT,CHARPN,"Got a hit for query %d @%ld %.*s",
                   i,tnr+(long)(hit-(char *)buf),length,where);
         }
      }
   }
   *tnh+=nh;
   *tnb+=tnr;
   putmsg(MFILEINFO,CHARPN,"%d Hits, %ld Bytes",nh,tnr);
   return(0);
}                                                     /* end srchfp() */
/**********************************************************************/

int srchfns ARGS((MMAPI **,int,int,char **,long *,long *));
/**********************************************************************/
int
srchfns(mp,n,argc,argv,tnh,tnb)
MMAPI **mp;
int n;
int argc;
char **argv;
long *tnh, *tnb;
{
FILE *fp;

   for(;argc>0;argc--,argv++){
      if((fp=fopen(*argv,"rb"))==(FILE *)NULL){
         putmsg(MERR+FOE,CHARPN,"Can't open: %s",*argv);
      }else{
         putmsg(MFILEINFO,CHARPN,"Searching: %s",*argv);
         srchfp(mp,n,fp,tnh,tnb);
         fclose(fp);
      }
   }
   return(0);
}                                                    /* end srchfns() */
/**********************************************************************/

static char *gut[32]={ (char *)NULL };

static void eat ARGS((long memamt));
/**********************************************************************/
static void
eat(memamt)
long memamt;
{
unsigned int i, n;
long amt;

   if(memamt==0L){
      for(i=0,amt=0L;i<32;i++,amt+=64512L){
         if((gut[i]=malloc(64512))==(char *)NULL) break;
      }
      if(i<32){
         for(n=64512;n>1023;n-=1024){
            if((gut[i]=malloc(n))!=(char *)NULL){
               amt+=(long)n;
               break;
            }
         }
      }
      putmsg(MREPT,(char *)NULL,"Alloced %ldK bytes",amt/1024L);
   }else if(memamt>0L){
      for(i=0,amt=memamt;i<32 && amt>0L;i++){
         if(amt>64512L) n=64512;
         else           n=(unsigned int)amt;
         amt-=(long)n;
         if((gut[i]=malloc(n))==(char *)NULL) break;
      }
      putmsg(MREPT,(char *)NULL,"Alloced %ldK bytes",(memamt-amt)/1024L);
   }
}                                                        /* end eat() */
/**********************************************************************/

static void regurg ARGS((void));
/**********************************************************************/
static void
regurg()
{
int i;

   for(i=0;i<32 && gut[i]!=(char *)NULL;i++) free(gut[i]);
}                                                     /* end regurg() */
/**********************************************************************/

int main    ARGS((int,char **));
/**********************************************************************/
int
main(argc,argv)
int argc;
char **argv;
{
APICP *acp;
MMAPI *mp[10];
char *query[10];
int nquery=0, i, closeeq=0;
char buf[256];
long tnh=0L, tnb=0L, stim, etim, bps, memamt=(-1L);

   if(argc==1){
      fprintf(stderr,"Use: %s [-a[memamt]] [-c] [-q\"query\" ...] [files]\n",argv[0]);
      return(1);
   }
   for(--argc,++argv;argc>0 && **argv=='-';argc--,argv++){
      switch(*++*argv){
      case 'a': memamt=atol(++*argv); break;
      case 'c': closeeq=1; break;
      case 'q': query[nquery++]= ++*argv; break;
      }
   }
   putmsg(MMPROCBEG,CHARPN,"Search starting");
   if((acp=openapicp())==APICPPN) return(1);
   if(nquery==0){
      if((mp[0]=openmmapi(CHARPN,acp))!=MMAPIPN){
         if(closeeq) closemmeq(mp[0]);
         while(1){
            fputs("Enter query\n>",stdout); fflush(stdout);
            if(fgets(buf,81,stdin)==CHARPN) break;
            if(setmmapi(mp[0],buf)==MMAPIPN) break;
            if(closeeq) closemmeq(mp[0]);
            eat(memamt);
            if(argc>0) srchfns(mp,1,argc,argv,&tnh,&tnb);
            else       srchfp(mp,1,stdin,&tnh,&tnb);
            putmsg(MTOTALS,CHARPN,"Search totals: %ld hits, %ld bytes",
                   tnh,tnb);
            regurg();
         }
         closemmapi(mp[0]);
      }
   }else{
      putmsg(MREPT,CHARPN,"%d Quer%s",nquery,nquery>1?"ies":"y");
      putmsg(MREPT,CHARPN,"%s",query[0]);
      if((mp[0]=openmmapi(query[0],acp))!=MMAPIPN){
         for(i=1;i<nquery;i++){
            putmsg(MREPT,CHARPN,"%s",query[i]);
            if((mp[i]=dupmmapi(mp[0],query[i]))==MMAPIPN){
               putmsg(MERR,CHARPN,"Failed to open query: %s\n",query[i]);
               break;
            }
         }
         if(closeeq) closemmeq(mp[0]);
         eat(memamt);
         stim=time((long *)NULL);
         if(argc>0) srchfns(mp,i,argc,argv,&tnh,&tnb);
         else       srchfp(mp,i,stdin,&tnh,&tnb);
         etim=time((long *)NULL);
         if(etim>stim) bps=tnb/(etim-stim);
         else          bps=tnb;
         putmsg(MTOTALS,CHARPN,"Search totals: %ld Hits, %ld Bytes, %ld bps",
                tnh,tnb,bps);
         regurg();
         for(--i;i>0;i--){
            closeapicp(mp[i]->acp);
            closemmapi(mp[i]);
         }
         closemmapi(mp[0]);
      }
   }
   closeapicp(acp);
   putmsg(MMPROCEND,CHARPN,"Search finished");
   exit(0);
}                                                       /* end main() */
/**********************************************************************/
#endif                                                        /* TEST */
