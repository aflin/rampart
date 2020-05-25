#include "txcoreconfig.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"
#include <sys/types.h>
#include "unicode.h"
#include "xtree.h"
#include "api3.h"
#define MAXRRWORDS 10000
#define RRMAXWLEN  20         /* longest word processed */
#define RRMINWLEN  4          /* after suffix ripping size */
#define DEFSUFFRM  1          /* also remove default suffixes */
#define RRS struct rank_rip
#define RRW struct rankword

#ifndef NRRWORDS    /* number of words in a relevancy rank */
#define NRRWORDS 10 /* Really empirical ( just like its purpose ) */
#endif 

#define NRRSUFS  91
static CONST char * CONST suflst[NRRSUFS] = {
  /* KNG 20061016 init array to the following suffix list,
   * but _after_ it was passed through initsuffix() (sufdone=1),
   * so we do not modify this array's constant strings in initsuffix():
     "'", "able", "age", "aged", "ager", "ages", "al", "ally", "ance",
    "anced", "ancer", "ances", "ant", "ary", "at", "ate", "ated", "ater",
    "atery", "ates", "atic", "ed", "en", "ence", "enced", "encer", "ences",
    "end", "ent", "er", "ery", "es", "ess", "est", "ful", "ial", "ible",
    "ibler", "ic", "ical", "ice", "iced", "icer", "ices", "ics", "ide",
    "ided", "ider", "ides", "ier", "ily", "ing", "ion", "ious", "ise",
    "ised", "ises", "ish", "ism", "ist", "ity", "ive", "ived", "ives",
    "ize", "ized", "izer", "izes", "less", "ly", "ment", "ncy", "ness",
    "nt", "ory", "ous", "re", "red", "res", "ry", "s", "ship", "sion", "th"
    , "tic", "tion", "ty", "ual", "ul", "ward", ""
  */
    "'", "ci", "cit", "cita", "de", "deci", "decna", "decne", "dedi",
    "dega", "der", "desi", "deta", "devi", "dezi", "dne", "draw", "eci",
    "ecna", "ecne", "edi", "ega", "elba", "elbi", "er", "esi", "eta", "evi",
    "ezi", "gni", "hsi", "ht", "la", "laci", "lai", "lau", "lu", "luf",
    "msi", "ne", "noi", "nois", "noit", "pihs", "re", "reci", "recna",
    "recne", "redi", "rega", "rei", "relbi", "reta", "rezi", "s", "sci",
    "se", "seci", "secna", "secne", "sedi", "sega", "ser", "sesi", "seta",
    "sevi", "sezi", "sse", "ssel", "ssen", "suo", "suoi", "ta", "tn", "tna",
    "tne", "tnem", "tse", "tsi", "ycn", "yl", "yli", "ylla", "yr", "yra",
    "yre", "yreta", "yro", "yt", "yti", ""
};
#define suflst ((char **)suflst)

static CONST int sufdone=1;

static CONST char * CONST def_noiselst[]={
/* added for web pages */
#ifdef FORWEB
"information","web","home","welcome","www","internet",
"services","pages","world","service",
"site","copyright","please","information","page",
#endif
/* default list from xtree.c */
"when","only","can","which","what","new","other","your",
"the", "of", "a", "to", "and", "in", "that", "for", "with", "as",
"from", "at", "on", "by", "have", "an", "or", "is", "their",
"their", "they", "it", "he", "more", "this", "may", "than", "not",
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


RRW
{
 char *s;
 int  len;
 int  cnt;
 int  seq;
};


RRS
{
 int    n;
 RRW    lst[MAXRRWORDS];
 byte  *buf,*end;
 int    maxwords;
};


static CONST char * CONST wrdexps[]={  /* BTW: ORDERING IS IMPORTANT HERE */
   "\\alpha{3,}",
""};

static CONST char * CONST phrexps[]={  /* phrase expressions */
   "\\alpha{4,} \\P=>>\\alpha{4,}",
   "\\alpha{4,} \\P=>>of =\\alpha{4,}",
   "\\alpha{4,} \\P=>>and =\\alpha{4,}",
""};


static CONST char * CONST keywrdexps[]={ /* BTW: ORDERING IS IMPORTANT HERE */
   "\\alnum{3,}",
""};

static CONST char * CONST keyphrexps[]={  /* phrase expressions */
 "\\alnum{4,}[ \\-\\n]\\P=>>\\alnum{4,}",
 "\\alnum{4,}[ \\-\\n]=\\alnum{4,}[ \\-\\n]\\P=>>\\alnum{4,}",
 "\\alnum{3,}[ \\-\\n]\\P=>>of[ \\-\\n]=\\alnum{3,}",
 "\\alnum{3,}[ \\-\\n]\\P=>>and[ \\-\\p]=\\alnum{3,}",
 "\\upper=\\lower{2,}\\space=\\upper\\. \\space?\\upper=\\lower{2,}",
""};

static int xtcallback ARGS((void *vp, byte *s, size_t len, size_t cnt,
                            XTREESEQ seq));

static int
xtcallback(vp,s,len,cnt,seq)
void *vp;
byte *s;
size_t len;
size_t cnt;
XTREESEQ seq;
{
 RRS *rs=(RRS *)vp;
 if(rs->n<MAXRRWORDS)
   {
    RRW *w=&rs->lst[rs->n];
    w->s=(char *)s;
    w->len=len;
    w->cnt = (int)(cnt != (size_t)(-1) ? cnt : 0);
    w->seq = (int)(long)seq.ptr;
    rs->n+=1;
    /*printf("%.*s\n",len,s);*/
   } 
 else
   {
    /*printf("ignored: %.*s\n",len,s);return(1);*/
    return(0);
   }
 return(1);
}

static int xtphrasecallback ARGS((void *vp, byte *s, size_t len, size_t cnt,
                                  XTREESEQ seq));
static int
xtphrasecallback(vp,s,len,cnt,seq)
void *vp;
byte *s;
size_t len;
size_t cnt;
XTREESEQ seq;
{
 RRS *rs=(RRS *)vp;
 if(rs->n<MAXRRWORDS && cnt != (size_t)(-1) && cnt > 1)
   {
    RRW *w=&rs->lst[rs->n];
    w->s=(char *)s;
    w->len=len;
    w->cnt = (int)(cnt != (size_t)(-1) ? cnt : 0);
    w->seq = (int)(long)seq.ptr;
    rs->n+=1;
    /*printf("%.*s\n",len,s);*/
   } 
 else
   {
    /*printf("ignored: %.*s\n",len,s);return(1);*/
    return(0);
   }
 return(1);
}


static int 
ripcmp(RRW *a, RRW *b)
{
 int rc=b->cnt-a->cnt;       /* inverse order of frequency */
 if(rc==0) rc=b->len-a->len;   /* then size */
 if(rc==0) rc=a->seq-b->seq;   /* then seq. (avoid qsort diff.) KNG 011219 */
 return(rc); 
}

static int 
seqcmp(RRW *a, RRW *b)
{
 int rc=a->seq-b->seq;       /*  order by insert sequence */
 if(a->cnt == 0 && b->cnt != 0)
 	return 1;
 if(b->cnt == 0 && a->cnt != 0)
 	return -1;
 return(rc); 
}

/******************************************************************************/

static void rmdupes ARGS((RRW *, int, APICP *apicp));

static void
rmdupes(rw,n, apicp)
/* avoid replicated words + clump the weights into one pile */
RRW *rw;
int   n;
APICP   *apicp;         /* (in) settings eg. textsearchmode */
{
 int i, j;
 char aa[RRMAXWLEN+1];
 char bb[RRMAXWLEN+1];

 /* KNG 20061016 init at compile 
 if(!sufdone)
   {
    sufdone=1;
    initsuffix(suflst);
   }
 */
 for(i=0;i<n-1;i++)
  {
   char *a=aa;
   if(rw[i].cnt==0)
      continue;
   TXstrncpy(a,rw[i].s,RRMAXWLEN);
   rmsuffix(&a, suflst, NRRSUFS, RRMINWLEN, DEFSUFFRM, 0,
            apicp->textsearchmode);
   for(j=i+1;j<n;j++)
      {
       char *b=bb;
       if(rw[j].cnt==0)
          continue;
       TXstrncpy(b,rw[j].s,RRMAXWLEN);
       if(strncmp(b,a,3)!=0)
          break;
       rmsuffix(&b, suflst, NRRSUFS, RRMINWLEN, DEFSUFFRM, 0,
                apicp->textsearchmode);
       if(!strcmp(a,b))
         {            /* the words are the same, so put the word count in i */
          rw[i].cnt+=rw[j].cnt; 
          rw[j].cnt=0; /* j is no longer important */
          
          if(rw[j].seq<rw[i].seq) /* change to lesser sequence order */
             rw[i].seq=rw[j].seq; 
         }
      }
  }
}


/******************************************************************************/

static void rmphrased ARGS((RRW *, int, RRW *, int, APICP *apicp));

static void
rmphrased(wl,nwl,pl,npl, apicp)
/* avoid replicated words within a phrase */
RRW *wl;
int  nwl;
RRW *pl;
int  npl;
APICP   *apicp;         /* (in) settings eg. textsearchmode */
{
 int i, j;
 char aa[RRMAXWLEN+1];
 
 /* KNG 20061016 init at compile 
 if(!sufdone)
   {
    sufdone=1;
    initsuffix(suflst);
   }
 */
 for(i=1;i<nwl;i++,wl++) /*  for each word in word list */
  {
   char *a=aa;
   if(wl->cnt==0)
      continue;
   TXstrncpy(a,wl->s,RRMAXWLEN);
   rmsuffix(&a, suflst, NRRSUFS, RRMINWLEN, DEFSUFFRM, 0,
            apicp->textsearchmode);
   for(j=0;j<npl;j++)
      {
       if(pl[j].cnt==0)
          continue;
       if(strstr(pl[j].s,a)!=(char *)NULL)/* if the word is within the phrase */
          {
           pl[j].cnt+=wl->cnt;     /* increment the phrases cnt by word freq */
           wl->cnt=0;
          }
      }
  }
}




/******************************************************************************/

static int expsize ARGS((register FFS *));

static int            /* DERIVED from rexsize() ,  it keeps excluded stuff in */
expsize(fs)                                /* returns the size of a hit */
register FFS *fs;
{
 register int size=0;
 fs = firstexp(fs);
 for(;fs!=(FFS *)NULL;fs=fs->next)
   {
    /*
    if(fs->exclude<0) continue;        
    else
    if(fs->exclude>0) break;
    */
    size+=fs->hitsize;
   }
 return(size);
}

/******************************************************************************/

static XTREE *getexps ARGS((RRS *, CONST char * CONST *,
                            TXCFF textSearchMode));

static XTREE *
getexps(rs,explst,textSearchMode)
RRS *rs;
CONST char * CONST *explst;
TXCFF   textSearchMode;         /* (in) textsearchmode to use */
{
 int i,cnt;
 FFS *e;
 XTREE *xt=openxtree(TXPMBUFPN, 100000L);
 if(xt==NULL)
   return(xt);
 /* Use current textsearchmode, eg. for case-insensitivity etc.: */
 TXxtreeSetCmpMode(xt, textSearchMode);
 /* Use pointers for sequence "numbers" (because we did originally? KNG) */
 TXxtreeSetSequenceMode(xt, 1);
 /* Store strings folded for speed (do not have to re-fold at each search).
  * Note that this might change the length of the string, but this is ok
  * as the ultimate return value is built from the (folded) XTREE values;
  * the original text is not referred to again after the first scan here:
  */
 TXxtreeSetStoreFolded(xt, 1);
 for(cnt=i=0;*explst[i];i++)
   {
    if ((e = openrex((byte *)explst[i], TXrexSyntax_Rex)) != (FFS *)NULL)
      {
       byte *p;
       for(p=getrex(e,rs->buf,rs->end,SEARCHNEWBUF);
           p!=BPNULL;
           p=getrex(e,rs->buf,rs->end,CONTINUESEARCH)
          )
          {
            int  sz;
            cnt++;
            p = rexfirst(e);
            sz=expsize(e);
            putxtree(xt,p,sz);
          }
       closerex(e);
      }
   }
 return(xt);
}



/******************************************************************************/

static CONST char * CONST *noiselst=NULL;    /* MAW 06-02-97 - make it settable */

CONST char * CONST *
text2mmnoise(lst)
CONST char * CONST *lst;
{
CONST char * CONST *rc=noiselst;

   noiselst=lst;
   return(rc==NULL?def_noiselst:rc);
}

char *
text2mm(txt,maxwords,apicp)
/* convert given text to suitable liker or likep query */
char *txt;
int maxwords;
APICP   *apicp;         /* (in) textsearchmode to use */
{
 char  *p,*q,*query=(char *)NULL;
 XTREE *xword,*xphrase;
 RRS   *rs=(RRS *)calloc(1,sizeof(RRS));
 RRW   *wl,*pl; /* word & phrase lists */
 
 int    i,tlen;
 int    nwords,nphrases,uphrases;  /* uphrases is the number of actually used phrases */
 
 xword=xphrase=(XTREE *)NULL;
 if(noiselst==NULL)             /* MAW 06-02-97 - make it settable */
    noiselst=def_noiselst;
 
 if(maxwords<1)
    maxwords=NRRWORDS;
 
 rs->maxwords=maxwords;
 rs->buf=(byte *)txt;
 rs->end=rs->buf+strlen(txt);
 rs->n  =0;

 if((xword  =getexps(rs,wrdexps,apicp->textsearchmode))==(XTREE *)NULL ||
    (xphrase=getexps(rs,phrexps,apicp->textsearchmode))==(XTREE *)NULL 
   ) goto END;
 
 delxtreesl(xword,(byte **)noiselst);     /* remove noise from words tree */
 
 walkxtree(xphrase,xtphrasecallback,(void *)rs);
 nphrases=rs->n;
 pl=&rs->lst[0];        /* the phrase list is at the base of the overall list */
 
 walkxtree(xword,xtcallback,(void *)rs);
 nwords=rs->n - nphrases;
 wl=&rs->lst[nphrases]; /* the word list is after the phrase list */
 
                        /* sort the phrases into prority */
 qsort(pl,nphrases,sizeof(RRW),(int (*)ARGS((CONST void *,CONST void *)))ripcmp);
 
                        /* count non-unique phrases */
 for(uphrases=0;uphrases<nphrases && pl[uphrases].cnt>1;uphrases++);
 
 
 rmdupes(wl,nwords,apicp);    /* remove duplication within the word list */

                        /* sort the words into prority */
 qsort(wl,nwords,sizeof(RRW),(int (*)ARGS((CONST void *,CONST void *)))ripcmp);
  
                        /* within the possible HOT words, prevent duplication within phrases */
 rmphrased(wl,maxwords,pl,uphrases,apicp);
 
                         /* sort all words and phrases into prority */
 qsort(rs->lst,rs->n,sizeof(RRW),(int (*)ARGS((CONST void *,CONST void *)))ripcmp);
  
 if(rs->n<maxwords)
    maxwords=rs->n;

                        /* sort TOP-10 by sequence  */
 qsort(rs->lst,maxwords,sizeof(RRW),(int (*)ARGS((CONST void *,CONST void *)))seqcmp);           
                        /* Get the required buffer size  */
 for(tlen=i=0;i<rs->n && i<maxwords && rs->lst[i].cnt;i++) /* get the 'Top 10' */
    tlen+= (rs->lst[i].len) + 1;
 
 if((query=(char *)malloc(tlen+1))!=(char *)NULL)
   {
    for(p=query,i=0;i<rs->n && i<maxwords && rs->lst[i].cnt;i++)
      {
       for(q=rs->lst[i].s;*q;p++,q++)
          if(*q==' ') *p='-';
          else        *p=*q;
       *p++ = ' ';
      }
    *p='\0';
   }
   
 xword = closextree(xword);  
 
 rs->n=0;          /* now break up the phrases */
 rs->buf=(byte *)query;
 rs->end=(byte *)(query+tlen+1);  
 if((xword  =getexps(rs,wrdexps,apicp->textsearchmode))==(XTREE *)NULL)
   goto END;
 delxtreesl(xword,(byte **)noiselst);
 walkxtree(xword,xtcallback,(void *)rs);
 qsort(rs->lst,rs->n,sizeof(RRW),(int (*)ARGS((CONST void *,CONST void *)))seqcmp);
 for(p=query,i=0;i<rs->n && i < maxwords;i++)
  {
   for(q=rs->lst[i].s;*q;p++,q++)
     *p = *q;
   *p++ = ' ';
  }
 *p ='\0';
   
 END:
 if(xword  !=(XTREE *)NULL) closextree(xword);
 if(xphrase!=(XTREE *)NULL) closextree(xphrase);
 if(rs!=(RRS *)NULL)   free(rs);
 return(query);
}



char *
keywords(txt,maxwords,apicp)
char *txt;
int maxwords;
APICP   *apicp;         /* (in) textsearchmode to use */
/* find meaning */
{
 char  *p,*q,*query=(char *)NULL;
 XTREE *xword,*xphrase;
 RRS   *rs=(RRS *)calloc(1,sizeof(RRS));
 RRW   *wl,*pl; /* word & phrase lists */
 
 int    i,tlen;
 int    nwords,nphrases,uphrases;  /* uphrases is the number of actually used phrases */
 
 xword=xphrase=(XTREE *)NULL;
 if(noiselst==NULL)             /* MAW 06-02-97 - make it settable */
    noiselst=def_noiselst;

 if(maxwords<1)
    maxwords=NRRWORDS;
 
 rs->maxwords=maxwords;
 rs->buf=(byte *)txt;
 rs->end=rs->buf+strlen(txt);
 rs->n  =0;

 if((xword  =getexps(rs,keywrdexps,apicp->textsearchmode))==(XTREE *)NULL ||
    (xphrase=getexps(rs,keyphrexps,apicp->textsearchmode))==(XTREE *)NULL 
   ) goto END;
 
 delxtreesl(xword,(byte **)noiselst);     /* remove noise from words tree */
 
 walkxtree(xphrase,xtcallback,(void *)rs);
 nphrases=rs->n;
 pl=&rs->lst[0];        /* the phrase list is at the base of the overall list */
 
 walkxtree(xword,xtcallback,(void *)rs);
 nwords=rs->n - nphrases;
 wl=&rs->lst[nphrases]; /* the word list is after the phrase list */
 
                        /* sort the phrases into prority */
 qsort(pl,nphrases,sizeof(RRW),(int (*)ARGS((CONST void *,CONST void *)))ripcmp);
 
                        /* count non-unique phrases */
 for(uphrases=0;uphrases<nphrases && pl[uphrases].cnt>1;uphrases++)
   pl[uphrases].cnt+=2; /* Bias up the phrases */
  
 
 rmdupes(wl,nwords,apicp);    /* remove duplication within the word list */

                        /* sort the words into prority */
 qsort(wl,nwords,sizeof(RRW),(int (*)ARGS((CONST void *,CONST void *)))ripcmp);
  
                        /* within the possible HOT words, prevent duplication within phrases */
 rmphrased(wl,maxwords,pl,uphrases,apicp);
 rmphrased(pl,maxwords,pl,uphrases,apicp);
 
                         /* sort all words and phrases into prority */
 qsort(rs->lst,rs->n,sizeof(RRW),(int (*)ARGS((CONST void *,CONST void *)))ripcmp);
  
 if(rs->n<maxwords)
    maxwords=rs->n;

                        /* Get the required buffer size  */
 for(tlen=i=0;i<rs->n && i<maxwords;i++) /* get the 'Top 10' */
    tlen+= (rs->lst[i].len) + 1;
 
 if((query=(char *)malloc(tlen+1))!=(char *)NULL)
   {
    for(p=query,i=0;i<rs->n && i<maxwords ;i++)
      {
       for(q=rs->lst[i].s;*q;p++,q++)
          if(*q==' ') *p=' ';
          else        *p=*q;
       *(p++) = 0x0a;
      }
    *p='\0';
   }


 END:
 if(xword  !=(XTREE *)NULL) closextree(xword);
 if(xphrase!=(XTREE *)NULL) closextree(xphrase);
 if(rs!=(RRS *)NULL)   free(rs);
 return(query);
}

#ifdef TEST

#define BUFSZ 1000000
char buf[BUFSZ];

void
main(argc,argv)
int argc;
char **argv;
{
 char  *mm;
 
   if(argc==1)
   {
     int nread=fread(buf,1,BUFSZ,stdin);
     if(nread>1)
       buf[nread<BUFSZ ? nread : BUFSZ-1]='\0';
     if((mm=text2mm(buf,0))!=(char *)NULL)
         {
          puts(mm);
          free(mm);
         }
   }
   else
   {
      for(--argc,++argv;argc>0;argc--,argv++)
      {
         if((mm=keywords(*argv,250))!=(char *)NULL)
         {
            puts(mm);
            free(mm);
         }
      }
   }
   exit(0);
}
#endif                                                        /* TEST */
