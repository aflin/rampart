/* PBR 08-23-91 moved cmptab from struct to static array */
/* MAW 01-16-97  added PM_FLEXPHRASE phrase handling */
/* MAW 04-05-97  added PM_NEWLANG language processing */
/* MAW 08-12-98 fix PM_FLEXPHRASE for single letter phrases */

/**********************************************************************/
#include "txcoreconfig.h"
#include "stdio.h"
#include "sys/types.h"
#include <stdlib.h>
#ifdef MVS
#  if MVS_C_REL > 1
#     include <stddef.h>
#  else
#     include <stdefs.h>
#  endif
#endif
#include "ctype.h"
#include "string.h"
#include "sizes.h"
#include "os.h"
#include "pm.h"
#include "api3.h"
#include "unicode.h"

#ifdef _WIN32
#  define STATIC
#else
#  define STATIC static
#endif

/* KNG 20080416 moved here from texis/texglob.c for link resolution.
 * should eventually migrate to APICP/MM3S?
 */
#if TX_VERSION_MAJOR >= 6
#  define DEF   1
#else /* TX_VERSION_MAJOR < 6 */
#  define DEF   0
#endif /* TX_VERSION_MAJOR < 6 */
int	TXwildoneword = DEF;	/* Wildcards span one word */
int	TXwildsufmatch = DEF;	/* Wildcard suffix-matches */
#undef DEF

/************************************************************************/
/************************************************************************/
/************************************************************************/
/* cmptab[ch] maps ch for equality, ie. ch1 matches ch2 iff
 * cmptab[ch1] == cmptab[ch2].  initpmct() maps upper-case to lower-case,
 * and whitespace to hyphen.  Note that cmptab may be further modified
 * by callers of pm_getspmct():
 */
static int  cmptab[DYNABYTE];
static int  doinitct=1;
/* CmpTabHas8bitCrossing: 1 if there are 7-bit chars that map to 8-bit
 * (or vice-versa) in `cmptab', 0 if not:
 */
static int  CmpTabHas8bitCrossing = -1;
static int  cmptab_locale_serial = -1;
static int  pmhyeqsp=1;                      /* does hyphen equal space */
#define CHARS_MATCH(ct, ch1, ch2)   ((ct)[(byte)(ch1)] == (ct)[(byte)(ch2)])
/************************************************************************/
void
pm_hyeqsp(tf)                 /* does hyphen == isspace ? true or false */
int tf;
{
 pmhyeqsp=tf;
}

int
pm_getHyphenPhrase()
{
  return(pmhyeqsp);
}

/************************************************************************/
static void
pm_initct(void)
{
int i;

/* If we haven't initialized `cmptab' yet, or the locale changed since we did,
 * re-initialize it:
 */
 if(doinitct || cmptab_locale_serial < TXgetlocaleserial())
    {
     CmpTabHas8bitCrossing = 0;
     for(i=0;i<DYNABYTE;i++)                  /* init the compare table */
         {
          if(isupper(i))  cmptab[i]=tolower(i);   /* map upper to lower */
          else
          if(isspace(i))  cmptab[i]=' ';                /* all white == */
          else
          cmptab[i]=i;                           /* assign unique digit */
          if ((i >= 0x80) ^ (cmptab[i] >= 0x80))
            CmpTabHas8bitCrossing = 1;
         }
     doinitct=0;
     cmptab_locale_serial = TXgetlocaleserial();
    }
 pm_reinitct();
}

/************************************************************************/
void
pm_reinitct()
{
 if(pmhyeqsp) cmptab['-']=' ';                   /* hyphen and white == */
 else         cmptab['-']='-';          /* in case pmhyeqsp has changed */
}

/************************************************************************/
void
pm_resetct()
{
  doinitct=1;
  pm_initct();
}

/************************************************************************/

int *
pm_getct()
{
  pm_initct();
  return(cmptab);
}

/************************************************************************/

int
pm_getCmpTabHas8bitCrossing()
{
  pm_initct();
  return(CmpTabHas8bitCrossing);
}

/************************************************************************/
/************************************************************************/
/************************************************************************/
static int spatlen ARGS((char *s));

static int
spatlen(s)                                              /* PBR 08-23-91 */
char *s;
{
 int i;
 for(i=0;*s!='*' && *s;s++,i++);
 return(i);
}

/************************************************************************/

SPMS *
openspm(s)
char    *s;
/* KNG 20080409 now obsolete; use openspmmm3s to use new settings
 */
{
  MM3S  msBuf;

  /* Fake up an MM3S with default settings.  See also ppm.c: */
  memset(&msBuf, 0, sizeof(MM3S));
  msBuf.textsearchmode = TxApicpDefault.textsearchmode;
  return(openspmmm3s(s, &msBuf));
}

SPMS *
openspmmm3s(s, ms)             /* open and init the single pattern matcher */
char *s;
MM3S    *ms;
{
 SPMS *fs=(SPMS *)calloc(1,sizeof(SPMS));
 int patlen,i,j,k;
 byte *srch;
#if PM_FLEXPHRASE                                     /* MAW 01-16-97 */
 byte *remain;
#endif


 if(fs==(SPMS *)NULL)
    return(fs);
 fs->next=(SPMS *)NULL;
#if PM_NEWLANG                                        /* MAW 03-05-97 */
 fs->lang = 1;
#  ifndef NO_ROOT_WILD
 /* KNG 20080416 set flag *before* stripping leading wildcard: */
 if (*s == '*') fs->lang = LANG_WILD;
#  endif
#endif
 for(;*s=='*' || *s=='?';s++);         /* eat leading *?'s PBR 08-23-91 */
#if PM_NEWLANG                                        /* MAW 03-05-97 */
 fs->langc=pm_getlangc();
 fs->wordc=pm_getwordc();
 /* Check for wildcard or non-langc char.
  * KNG 20080416 unless we already know it's wildcard (no check needed then):
  */
if (fs->lang != LANG_WILD)
 for(srch=(byte *)s;*srch;srch++)
    if(!fs->langc[*srch])
      {
#ifndef NO_ROOT_WILD                       /* JMT 98-05-26 Make default */
	if(*srch == '*') /* JMT 98-04-06 Root Wildcard searches to word */
	   fs->lang = LANG_WILD;/* JMT 2000-04-13 */
	else
#endif /* !NO_ROOT_WILD */
       { fs->lang=0; break; }
      }
#endif /* PM_NEWLANG */
#if PM_FLEXPHRASE                                     /* MAW 01-16-97 */
if((fs->phrase=openpmphr((byte *)s,1,ms,pmhyeqsp))==PMPHRPN)
    {
     free(fs);
     return((SPMS *)NULL);
    }
 if(fs->phrase->nterms>1)
    {
     fs->patlen=patlen=fs->phrase->len;
     srch=fs->sstr=fs->phrase->term;
     remain=fs->phrase->remain;
    }
 else
    {
     fs->phrase=closepmphr(fs->phrase);
     remain=(byte *)NULL;
     fs->patlen=patlen=spatlen(s);
     srch=fs->sstr=(byte *)s;
    }
#else
 patlen=spatlen(s);
 fs->patlen=patlen;
 srch=fs->sstr=(byte *)s;
#endif
 pm_initct();

 /* KNG 20080408 SPM cannot handle UTF-8 or certain other textsearchmodes;
  * use TXUPM where needed.  Not fully implemented, eg. we should allow
  * SPM to do (and correctly implement) `respectcase,iso88591,tolower':
  */
 if (!TXisSpmSearchable((char *)srch, patlen, ms->textsearchmode,
                        pmhyeqsp, &fs->cmptab))
   {                                              /* TXUPM needed */
     fs->upm = TXtxupmOpen((char *)srch, patlen, ms->textsearchmode);
     if (fs->upm == TXUPMPN) return(closespm(fs));
     fs->cmptab = pm_getct();                   /* for occasional use */
   }

 if(patlen>1)                                     /*  skip table needed */
    {
     memset((char *)fs->skiptab,patlen,(unsigned)DYNABYTE);/* init skip */
     --patlen; /* decrement patsz in order to ignore last byte in pattern */
      for(i=0,j=patlen;i<patlen;i++,j--)                    /* forwards */
          for(k=0;k<DYNABYTE;k++)
              if(CHARS_MATCH(fs->cmptab, k, srch[i]))
                   fs->skiptab[k]=(byte)j;
    }
 if(patlen==0) return(closespm(fs)); /* PBR 5/14/92 */
#if PM_FLEXPHRASE
 if(fs->phrase!=PMPHRPN)
    {
     if(remain!=(byte *)NULL && remain[1])
        {
          if ((fs->next=openspmmm3s((char *)remain+1, ms)) == SPMSPN)
             return(closespm(fs));
        }
    }
 else
#endif
    {
     if(srch[fs->patlen]=='*' && srch[fs->patlen+1])  /* PBR 08-23-91 */
        {
          if ((fs->next=openspmmm3s((char *)srch+fs->patlen+1, ms)) == SPMSPN)
             return(closespm(fs));
        }
    }
 return(fs);
}

/************************************************************************/

SPMS *
closespm(fs)                        /* close the single pattern matcher */
SPMS *fs;
{
 if (fs == SPMSPN) return(SPMSPN);
 if(fs->next!=(SPMS *)NULL)                 /* close chain PBR 08-23-91 */
    fs->next=closespm(fs->next);
#if PM_FLEXPHRASE                                     /* MAW 01-16-97 */
 if(fs->phrase!=PMPHRPN)
    closepmphr(fs->phrase);
#endif
 if (fs->upm != TXUPMPN)
   fs->upm = TXtxupmClose(fs->upm);
 free(fs);
 return(SPMSPN);                                 /* always returns null */
}

/************************************************************************/

static CONST char copyright[] =
/************************************************************************/
           "Copyright 1985,1986,1987,1988 P. Barton Richards";
/************************************************************************/

int
findspm(fs)                                /* locates the pattern in fs */
SPMS *fs;
{
                   /* in order of register priority */
 register int   hitc;                       /* last byte in the pattern */
 register byte *bufptr,                        /* the byte being tested */
                slen,          /* this is the ending byte in the string */
               *sstr,
               *endptr;                                   /* end of buf */

 register byte *jmptab;                                   /* jump table */
 register CONST int     *fsCmpTab = fs->cmptab;

 jmptab=fs->skiptab;
 slen=(byte)(fs->patlen-1);
 sstr=fs->sstr;
 bufptr=fs->start+slen;
 hitc=fsCmpTab[fs->sstr[slen]];
 endptr=fs->end;

 /* KNG 20080409 use TXUPM object for Unicode search, if needed: */
 if (fs->upm != TXUPMPN)
   {
     for (fs->hit = (byte *)TXtxupmFind(fs->upm, fs->start, endptr,
                                        SEARCHNEWBUF);
          fs->hit != BPNULL;
          fs->hit = (byte *)TXtxupmFind(fs->upm, fs->start, endptr,
                                        CONTINUESEARCH))
       {
         fs->hitend = fs->hit + TXtxupmGetHitSz(fs->upm);
#if PM_FLEXPHRASE
         if (fs->phrase != PMPHRPN &&
             (fs->hit = verifyphrase(fs->phrase, fs->start, endptr, fs->hit,
                                     &fs->hitend)) == BPNULL)
           continue;                            /* phrase did not match */
#endif /* PM_FLEXPHRASE != 0 */
         if (fs->next == SPMSPN)
           return(1);
         else                                   /* chain to next pattern */
           {
             byte *end, *e;
             /* -1 for symmetry with normal SPM `bufptr', which is
              * effectively `fs->hitend - 1' at this point:
              */
             end = (fs->hitend - 1) + SPMWILDSZ;
             if (end > endptr) end = endptr;
             if (TXwildoneword)                 /* wild spans 1 word only */
               {
                 /* Need to limit the next-term search to the current word.
                  * Could plausibly define a word as [^\space]* or [\wordc]*.
                  * [^\space]* pros/cons:
                  *   +  Helps span abbreviations eg. `a*p' matches `A&P'
                  *   +  Will not truncate word early if second term
                  *      has non-wordc chars, eg. `a*&t' matches `AT&T',
                  *      because we would include `&T' in word here
                  * [\wordc]* pros/cons:
                  *   +  Consistent with expand-to-word in remorph()
                  *   +  More consistent than [^\space]* with index expr,
                  *      ie. for linear dictionary search
                  *   +  Modifiable by user
                  *   +  Does not include quotes/periods in hit, eg. `b*t'
                  *      matches only `boat' part of `"boat."'
                  * Choice: [\wordc]* + next-pattern-length.  wordc is
                  * more consistent, and adding next-pattern-length does
                  * not require the next pattern to be within wordc,
                  * while still limiting `*' to wordc-only.  KNG 20080417
                  */
                 for (e = fs->hitend; e < end && fs->wordc[*e]; e++);
                 e += fs->next->patlen;
                 if (e > end) e = end;
                 end = e;
               }
             /* else wildcard can span 2+ words */
             fs->next->start = fs->hitend;
             fs->next->end = end;
             fs->next->hit = BPNULL;
             if (findspm(fs->next)) return(1);
           }
       }
     return(0);                                 /* not found */
   }

 if(!slen)                             /* faster single byte match */
    {
     for(;bufptr<endptr;bufptr++)       /* MAW 11-14-95 - obob was <= */
        {
         if(hitc==*(fsCmpTab+ *bufptr))
              {
               fs->hit=bufptr;
#if PM_FLEXPHRASE
               fs->hitend=fs->hit+1;                  /* MAW 03-17-97 */
               if(fs->phrase!=PMPHRPN &&              /* MAW 08-12-98 */
                  (fs->hit=verifyphrase(fs->phrase,fs->start,endptr,fs->hit,&fs->hitend))==(byte *)NULL)
                  /* nohit - noop */ ;
               else
#endif
               if(fs->next==(SPMS *)NULL)               /* PBR 08-23-91 */
                    return(1);
               else
                   {
                     byte *end, *e;
                     end = bufptr + SPMWILDSZ;
                     if (end > endptr) end = endptr;
                     if (TXwildoneword)         /* wild spans 1 word only */
                       {
                         /* see space vs. wordc comment above */
                         for (e = bufptr + 1; e < end && fs->wordc[*e]; e++);
                         e += fs->next->patlen;
                         if (e > end) e = end;
                         end = e;
                       }
                     /* else wildcard can span 2+ words */
                    fs->next->start=bufptr+1;
                    fs->next->end=end;
                    fs->next->hit=BPNULL;
                    if(findspm(fs->next))
                        return(1);
                   }
              }
         }
    }

 for(;bufptr<endptr;bufptr+= *(jmptab + *bufptr))/* MAW 11-14-95 - obob was <= */
    {
     if(hitc==*(fsCmpTab+ *bufptr))
         {               /*  match them until != or complete match */
          byte *s1,*s2;
          for(s1=sstr+slen-1,s2=bufptr-1;
              s1 >= sstr && CHARS_MATCH(fsCmpTab, *s1, *s2);
              s1--, s2--);
          if(s1<sstr)
              {
               fs->hit=s2+1;

#if PM_FLEXPHRASE
               fs->hitend=fs->hit+fs->patlen;
               if(fs->phrase!=PMPHRPN &&
                  (fs->hit=verifyphrase(fs->phrase,fs->start,endptr,fs->hit,&fs->hitend))==(byte *)NULL)
                  /* nohit - noop */ ;
               else
#endif
               if(fs->next==(SPMS *)NULL)               /* PBR 08-23-91 */
                    return(1);
               else
                   {
                     byte *end, *e;
                     end = bufptr + SPMWILDSZ;
                     if (end > endptr) end = endptr;
                     if (TXwildoneword)         /* wild spans 1 word only */
                       {
                         /* see space vs. wordc comment above */
                         for (e = bufptr + 1; e < end && fs->wordc[*e]; e++);
                         e += fs->next->patlen;
                         if (e > end) e = end;
                         end = e;
                       }
                     /* else wildcard can span 2+ words */
                    fs->next->start=bufptr+1;
                    fs->next->end=end;
                    fs->next->hit=BPNULL;
                    if(findspm(fs->next))
                        return(1);
                   }
              }
         }
    }
 fs->hit=BPNULL;                                              /* no hit */
 return(0);
}

/************************************************************************/

int
spmhitsz(fs)
SPMS *fs;
{
#if PM_FLEXPHRASE
 byte *start=fs->hit;

 for(;fs->next!=(SPMS *)NULL;fs=fs->next)
    ;
 return(fs->hitend-start);
#else
 int l;
 for(l=0;fs->next!=(SPMS *)NULL;fs=fs->next)
    l+=fs->next->hit-fs->hit;
 if (fs->upm != TXUPMPN)
   l += TXtxupmGetHitSz(fs->upm);
 else
   l+=fs->patlen;
 return(l);
#endif
}

/************************************************************************/

byte *
getspm(fs,buf,end,op)
SPMS *fs;
byte *buf,*end;
TXPMOP op;
{
 if(op==SEARCHNEWBUF)
    {
     fs->start=buf;
     fs->end=end;
     fs->hit=BPNULL;
     if(findspm(fs))
        {
         fs->patsize=spmhitsz(fs);
         return(fs->hit);
        }
    }
 else if(op==CONTINUESEARCH)
    {
     fs->start=fs->hit+1;
     fs->hit=BPNULL;
     if(findspm(fs))
        {
         fs->patsize=spmhitsz(fs);
         return(fs->hit);
        }
    }
 return(BPNULL);
}

/************************************************************************/


#ifdef TEST
int                                                     /* n bytes read */
freadnl(fh,buf,len)
FILE *fh;
register byte *buf;
int  len;
{
 int nactr=fread(buf,sizeof(byte),len,fh);                 /* do a read */
 register int nread=nactr;                             /* end of buffer */
 register byte *loc;

 if(nread && nread==len)               /* read ok && as big as possible */
    {
     for(loc=buf+nread;loc>buf && *loc!='\n';loc--);
     if(loc<=buf)                        /* no expression within buffer */
         return(-1);
     nread=(int)(loc-buf)+1;                        /* just beyond expr */
     if (FSEEKO(fh, (off_t)(nread - nactr), SEEK_CUR) == -1)
         return(-1);
     return(nread);
    }
 return(nactr);                     /* read was smaller || eof || error */
}


#define LNBUFSZ 128000
char lnbuf[LNBUFSZ];

main(argc,argv)
int argc;
char *argv[];
{
 int count=0, nread;
 byte *hit;
 SPMS *fs=openspm(argv[1]);
 FILE *fh=fopen(argv[2],"r");


 if(fh==NULL)
    fh=stdin;
 while((nread=freadnl(fh,lnbuf,LNBUFSZ))>0)
    {
     for(hit=getspm(fs,(byte *)lnbuf,(byte *)lnbuf+nread,SEARCHNEWBUF);
         hit!=NULL;
         hit=getspm(fs,(byte *)lnbuf,(byte *)lnbuf+nread,CONTINUESEARCH)
        )
        {
         byte *end;
         ++count;
         putchar('"');
         for(end=hit+fs->patsize;hit<end;hit++)
              putchar(*hit);
         putchar('"');
         putchar('\n');
        }
    }
 printf("%d hits\n",count);
 closespm(fs);
}
#endif
