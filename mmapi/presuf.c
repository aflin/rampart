
#define USEBINSEARCH 1
/**********************************************************************/
#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#ifdef unix
#  include <sys/types.h>				  /* for os.h */
#else
#  include <stdlib.h>
#endif
#ifdef MVS
#  if MVS_C_REL > 1
#     include <stddef.h>				  /* for os.h */
#  else
#     include <stdefs.h>				  /* for os.h */
#  endif
#endif
#ifdef MSDOS
#  include <stddef.h>					  /* for os.h */
#endif
#include "os.h"
#include "api3.h"
#include "unicode.h"

#ifndef CONST
#  if defined(__STDC__) || defined(__TURBOC__) || defined(__TSC__)
#     define CONST const
#  else
#     define CONST
#  endif
#endif

static void      PSstrrev   ARGS((char *s, TXCFF textsearchmode));
static int       PSlistrev  ARGS((char **list, TXCFF textsearchmode));
static int       PSlistsz   ARGS((char **));
static int CDECL PSqstrcmpi ARGS((CONST void *,CONST void *));
/************************************************************************/

static void
PSstrrev(s, textsearchmode)
register char *s;
TXCFF           textsearchmode; /* (in) */
{
 register char *e,t;

 if (textsearchmode & TXCFF_ISO88591)
   {
     e=s+strlen(s);
     for(--e;e>s;s++,e--)
       {
         t= *s;
         *s= *e;
         *e=t;
       }
   }
 else                                           /* UTF-8 */
   TXunicodeUtf8StrRev(s);
}

/************************************************************************/

static int
PSlistrev(list, textsearchmode)
char *list[];
TXCFF   textsearchmode;
{
 int i;
 for(i=0;*list[i];i++)
   PSstrrev(list[i], textsearchmode);
 return(i);
}

/************************************************************************/

static int
PSlistsz(list)				  /* returns the size of a list */
char *list[];
{
 int i;
 for(i=0;*list[i];i++);
 return(i);
}

/************************************************************************/

static TXCFF    PSqstrcmpiTextsearchmode;       /* thread-unsafe */

#ifdef USEBINSEARCH

static int
CDECL
PSqstrcmpi(a,b) 		     /* sorts by ascending alpha and sz */
CONST void *a,*b;
/* Note: `PSqstrcmpiTextsearchmode' must be set before this function used.
 * Thread-unsafe.
 */
{
  CONST char    *as = *(char **)a, *bs = *(char **)b;

  return(TXunicodeStrFoldCmp(&as, -1, &bs, -1, PSqstrcmpiTextsearchmode));
}

#else

static int
CDECL
PSqstrcmpi(a,b)         /* sorts by descending size then ascending alpha */
CONST void *a,*b;
/* Note: `PSqstrcmpiTextsearchmode' must be set before this function used.
 * Thread-unsafe.
 */
{
 int ret;
 CONST char    *as = *(char **)a, *bs = *(char **)b;
 size_t aLen, bLen;

 ret=(int)((bLen = strlen(bs)) - (aLen = strlen(as)));
 if(ret==0)
   ret = TXunicodeStrFoldCmp(&as, -1, &bs, -1, PSqstrcmpiTextsearchmode));
 return(ret);
}

#endif
/************************************************************************/

int					  /* returns number of prefixes */
initprefix(list, textsearchmode)       /* inits the prefix list */
char *list[];
int     textsearchmode; /* (in) TXCFF textsearchmode */
/* Thread-unsafe (due to `PSqstrcmpiTextsearchmode' static var).
 */
{
 int npre=PSlistsz(list);
 if(npre>1)
   {
     PSqstrcmpiTextsearchmode = textsearchmode;
     qsort((char *)list,(size_t)npre,sizeof(char *),PSqstrcmpi);
   }
 return(npre);
}

/************************************************************************/

	     /* caution: this will make the list illegible */
	     /* call this twice to make it legible again */

int					  /* returns number of suffixes */
initsuffix(list, textsearchmode)	       /* inits the suffix list */
char *list[];
int     textsearchmode; /* (in) TXCFF textsearchmode */
/* Thread-unsafe (due to `PSqstrcmpiTextsearchmode' static var).
 */
{
  int nsuf=PSlistrev(list, textsearchmode);
 if(nsuf>1)
   {
     PSqstrcmpiTextsearchmode = textsearchmode;
     qsort((char *)list,(size_t)nsuf,sizeof(char *),PSqstrcmpi);
   }
 return(nsuf);
}

/************************************************************************/

int
prefcmpi(a,bp,textsearchmode)
char *a,**bp;
int     textsearchmode; /* (in) TXCFF textsearchmode */
/* compares 2 strs based on the length of the 1st one */
/* returns 0 if not the same or character (not byte) length of '*bp' if match.
 * Advances `*bp' past # of characters returned.
 */
{
  CONST char    *b, *bOrg;
 size_t n;

 b = bOrg = (CONST char *)(*bp);
 switch (TXunicodeStrFoldCmp((CONST char **)&a, -1, &b, -1,
                             (textsearchmode | TXCFF_PREFIX)))
   {
   case 0:                                      /* `a' == `b' */
   case TX_STRFOLDCMP_ISPREFIX:                 /* `a' is a prefix of `b' */
     *bp = (char *)b;
     if (textsearchmode & TXCFF_ISO88591)
       return(b - bOrg);
     else                                       /* UTF-8 mode */
       {
         n = (size_t)(-1);
         TXunicodeGetUtf8CharOffset(bOrg, b, &n);
         return((int)n);                        /* count chars not bytes */
       }
   default:                                     /* `a' != `b' nor prefix */
     /* do not advance `*bp' */
     return(0);
   }
}

/************************************************************************/
#ifdef USEBINSEARCH
/************************************************************************/

int					   /* rets or Where it would be */
PSbsrch(s,lst,n,textsearchmode)       /* sl binary search for a string */
char *s;
char **lst;
int n;
int     textsearchmode; /* (in) TXCFF textsearchmode */
/* Returns index into `lst' of last item that starts with the same letter
 * as `s' (or the nearest letter before it).
 */
{
 int first,last,i,cmp;
 size_t sFirstCharByteLen;      /* byte length of first character of `s' */
 CONST char     *sTmp, *lstTmp, *sEnd;

 if (textsearchmode & TXCFF_ISO88591)
   sFirstCharByteLen = 1;
 else                                           /* UTF-8 mode */
   {
     sTmp = s;
     sEnd = s + strlen(s);
     if (TXunicodeDecodeUtf8Char(&sTmp, sEnd, 1) < 0)
       sFirstCharByteLen = 1;                   /* short buffer */
     else
       sFirstCharByteLen = sTmp - (CONST char *)s;
   }

 i=first=0;
 last=n-1;

 while(first<=last)
    {
     i=(first+last)/2;
     sTmp = s;
     lstTmp = lst[i];
     /* Compare first characters of `sTmp' and `lstTmp': */
     cmp = TXunicodeStrFoldCmp(&sTmp, sFirstCharByteLen, &lstTmp, -1,
                               (textsearchmode | TXCFF_PREFIX));
     /* We want the *end* of the sub-list matching the first char of `s',
      * so treat a match (or prefix) the same as if `s' were *greater*
      * than `lst[i]':
      */
     if (cmp >= 0 || cmp == TX_STRFOLDCMP_ISPREFIX)
       first = i + 1;
     else
       last = i - 1;
    }
 first--;
 if(first<0) first=0;
 else if(first>=n) first=n-1;
 return(first);
}

/************************************************************************/


int
prefsz(list,n,strp,min,wlen,textsearchmode)
/* DOES NOT PERFORM ANY ACTION ON `*strp' */
/* returns number of valid characters (not bytes) to zap based on list.
 * Advances `*strp' past returned # of characters.
 */
char *list[];
int n;
char **strp;
int min;                /* (in) character (not byte) minwordlen */
int wlen;               /* (in) character (not byte) length of `*strp' */
int textsearchmode;     /* (in) TXCFF textsearchmode */
{
 int i,len,max; /* sept 3 90 pbr added a tolower to fix presuf bug */
 size_t strFirstCharByteLen;    /* byte length of first character of `str' */
 char           *str = *strp, *maxStrAdvance = *strp, *s;
 CONST char     *strTmp, *lstTmp, *strEnd;

 if (textsearchmode & TXCFF_ISO88591)
   strFirstCharByteLen = 1;
 else                                           /* UTF-8 mode */
   {
     strTmp = str;
     strEnd = str + strlen(str);
     if (TXunicodeDecodeUtf8Char(&strTmp, strEnd, 1) < 0)
       strFirstCharByteLen = 1;                 /* short buf */
     else
       strFirstCharByteLen = strTmp - (CONST char *)str;
   }

 for (i = PSbsrch(str, list, n, textsearchmode), max = 0;
      i >= 0;
      i--)
    {
      strTmp = str;
      lstTmp = list[i];
      /* Compare first characters of `sTmp' and `lstTmp': */
      switch (TXunicodeStrFoldCmp(&strTmp, strFirstCharByteLen,
                      &lstTmp, -1, (textsearchmode | TXCFF_PREFIX)))
        {
        case 0:                                 /* 1st chars match */
        case TX_STRFOLDCMP_ISPREFIX:            /* `sTmp' 1st char is prefix*/
          s = str;
          len = prefcmpi(list[i], &s, textsearchmode);
          if ((wlen - len) >= min && len > max)
            {
              max = len;
              maxStrAdvance = s;
            }
          continue;
        default:                                /* no longer matches */
          break;
        }
      break;
    }
 *strp = maxStrAdvance;
 return(max);
}

/************************************************************************/
#else						 /* use a linear search */
/************************************************************************/


int
prefsz(list,n,strp,min,wlen,textsearchmode)
/* DOES NOT PERFORM ANY ACTION ON `*strp' */
/* returns number of valid characters (not bytes) to zap based on list.
 * Advances `*strp' past returned # of characters.
 */
char *list[];
int n;
char **strp;
int min;                /* (in) character (not byte) minwordlen */
int wlen;               /* (in) character (not byte) length of `*strp' */
int textsearchmode;     /* (in) TXCFF textsearchmode */
{
 register int i,len;
 char   *s, *str = *strp, *maxStrAdvance = *strp;

 for(i=0;i<n;i++)
    {
      s = str;
      if((len=prefcmpi(list[i],&s,textsearchmode))!=0 && wlen-len>=min)
        {
          *strp = s;
          return(len);
        }
    }
 return(0);
}
#endif

/************************************************************************/

static int
rmprefixlen(sp,list,n,min,wlen,textsearchmode)
char **sp;					      /* addr of string */
char *list[];				    /* list of ptrs to suffixes */
int n;						  /* number of suffixes */
int min;                /* (in) character (not byte) min sz after strip */
int wlen;               /* (in) character (not byte) length of `*sp' */
TXCFF textsearchmode;   /* (in) textsearchmode */
/* Returns number of characters (not bytes) left.
 */
{
 int plen; 			 /* prefix length */
 char   *s;

 if(wlen>min) /* If there are more characters than min */
    {
      for(s = *sp, plen = prefsz(list, n, &s, min, wlen, textsearchmode);
	  plen && wlen - plen >= min;
	  s = *sp, plen = prefsz(list, n, &s, min, wlen, textsearchmode)
	 )
	 {
	  *sp = s;
	  wlen-=plen;
#ifdef  DEBUG
	       printf("(%d)%s, ",wlen,*sp);
#endif
	 }
    }
 return(wlen);
}

/************************************************************************/

int
rmprefix(sp,list,n,min,textsearchmode)
char **sp;					      /* addr of string */
char *list[];				    /* list of ptrs to suffixes */
int n;						  /* number of suffixes */
int min;			  /* min character (not byte) sz after strip */
int textsearchmode;     /* (in) TXCFF textsearchmode */
{
 int wlen; 			 /* word length , prefix length */
 size_t charLen;

 if (textsearchmode & TXCFF_ISO88591)
   wlen=strlen(*sp);
 else
   {
     charLen = (size_t)(-1);
     TXunicodeGetUtf8CharOffset(*sp, CHARPN, &charLen);
     wlen = (int)charLen;
   }
 return rmprefixlen(sp,list,n,min,wlen,textsearchmode);
}

/************************************************************************/

static int
wordstrlen(char *s, int phraseproc, TXCFF textsearchmode)
/* Returns character (not byte) length
 */
{
	static int yapped = 0;
	CONST char	*lastStart;
	byte *c, *wordc;
	int len = 0;
	size_t  n;

	switch(phraseproc)
	{
		default:
			if(!yapped)
			{
				yapped++;
				putmsg(MWARN, NULL, "Invalid phrasewordproc setting (%d)", phraseproc);
			}
		case API3PHRASEWORDNONE: /* Both NONE and LAST look for word breaks */
		case API3PHRASEWORDLAST:
			wordc = pm_getwordc();
			for(c = (byte *)s, lastStart = CHARPN; *c; c++)
			{
				if(wordc[(int)(*c)])
				{
					len++;
					if (lastStart == CHARPN)
						lastStart = (CONST char *)c;
				}
				else
				{
					len = 0;
					lastStart = CHARPN;
					if(phraseproc == API3PHRASEWORDNONE)
					{
						return 0;
					}
				}
			}
			if (lastStart == CHARPN) return(0);
			if (textsearchmode & TXCFF_ISO88591)
				return(len);
			else			/* UTF-8 mode */
			{
				n = (size_t)(-1);
				TXunicodeGetUtf8CharOffset(lastStart, CHARPN,
							   &n);
				return((int)n);
			}
			break;
		case API3PHRASEWORDMONO:
			if (textsearchmode & TXCFF_ISO88591)
				len =  strlen(s);
			else			/* UTF-8 mode */
			{
				n = (size_t)(-1);
				TXunicodeGetUtf8CharOffset(s, CHARPN, &n);
				len = (int)n;
			}
			break;
	}
	return len;
}

/************************************************************************/

void
rmsuffix(sp,list,n,min,rmdef,phraseproc,textsearchmode)
char **sp;					      /* addr of string */
char *list[];				    /* list of ptrs to suffixes */
int n;						  /* number of suffixes */
int min;			  /* min character (not byte) sz after strip */
int rmdef;                                   /* remove default suffixes */
int phraseproc;
int textsearchmode;     /* (in) TXCFF textsearchmode */
{
  CONST char    *s, *e, *s2, *e2;
  char          tmp[TX_MAX_UTF8_BYTE_LEN + 10];
 int len=wordstrlen(*sp,phraseproc,textsearchmode);
 if(len>=min)
    {
      PSstrrev(*sp, textsearchmode);			/* reverse string */
      if((len=rmprefixlen(sp,list,n,min,len,textsearchmode))>=min &&      /* do prefix removal */
         rmdef)		     /* do default removals *//* JMT 1999-10-01 */
	 {
           /* Find first character's length; will span `s' to `e': */
           e = s = *sp;
           if (textsearchmode & TXCFF_ISO88591)
             e++;
           else                                 /* UTF-8 mode */
             TXunicodeDecodeUtf8Char(&e, s + strlen(s), 1);
           /* Fold first character into `tmp': */
           TXunicodeStrFold(tmp, sizeof(tmp), s, e - s, textsearchmode);
           switch (*tmp)                        /* rm trailing vowel */
	       {
		case 'a': case 'e':  case 'i': case 'o':  case 'u':  case 'y':
                  len--;                        /* 1 character removed */
                  *sp = (char *)e;
		   break;
		default:
                  /* Find next character's length; will span `s2' to `e2': */
                  e2 = s2 = e;
                  if (textsearchmode & TXCFF_ISO88591)
                    e2++;
                  else                          /* UTF-8 mode */
                    TXunicodeDecodeUtf8Char(&e2, s2 + strlen(s2), 1);
                  /* Compare first two characters, folded: */
                  if (TXunicodeStrFoldCmp(&s, e - s, &s2, e2 - s2,
                                          textsearchmode) == 0)
                    {                           /* rm double consonant */
                      len--;                    /* 1 character removed */
                      *sp = (char *)e;
                    }
		   break;
	       }
	 }
     PSstrrev(*sp, textsearchmode);		   /* re-reverse string */
    }
 return;
}

/************************************************************************/

/* MAW 02-27-92 - make this reentrant by passing rmtrail state around */
int
rm1suffix(sp,list,n,min,rmtrail,rmdef,phraseproc,textsearchmode)
char **sp;					    /* addr of string */
char *list[];				  /* list of ptrs to suffixes */
int n;						/* number of suffixes */
int min;			/* min character (not byte) sz after strip */
int *rmtrail;                  /* state: prevent further stripping if */
				   /* stripped vowel or dbl consonant */
int rmdef;
int phraseproc;
int textsearchmode;     /* (in) TXCFF textsearchmode */
/* Returns character (not byte) length remaining.
 */
{
  char          tmp[TX_MAX_UTF8_BYTE_LEN + 10];
  char          *st;
  CONST char    *s, *e, *s2, *e2;
  int len=wordstrlen(*sp,phraseproc,textsearchmode), plen;

   if(len>=min && *rmtrail==0){
       PSstrrev(*sp, textsearchmode);		    /* reverse string */
       st = *sp;
       plen=prefsz(list,n,&st,min,len,textsearchmode);
       if(plen && len-plen>=min){		 /* do prefix removal */
	   *sp = st;
	   len-=plen;
       }else if(rmdef){/* nothing rm'ed, do defaults? *//* JMT 2001-11-27 */
           /* Find first character's length; will span `s' to `e': */
           e = s = *sp;
           if (textsearchmode & TXCFF_ISO88591)
             e++;
           else                                 /* UTF-8 mode */
             TXunicodeDecodeUtf8Char(&e, s + strlen(s), 1);
           /* Fold first character into `tmp': */
           TXunicodeStrFold(tmp, sizeof(tmp), s, e - s, textsearchmode);
	   switch (*tmp) {                      /* rm trailing vowel */
	      case 'a': case 'e':  case 'i': case 'o':  case 'u':  case 'y':
                len--;                          /* 1 character removed */
                *sp = (char *)e;
		 *rmtrail=1;
		 break;
	      default:
                  /* Find next character's length; will span `s2' to `e2': */
                  e2 = s2 = e;
                  if (textsearchmode & TXCFF_ISO88591)
                    e2++;
                  else                          /* UTF-8 mode */
                    TXunicodeDecodeUtf8Char(&e2, s2 + strlen(s2), 1);
                  /* Compare first two characters, folded: */
                  if (TXunicodeStrFoldCmp(&s, e - s, &s2, e2 - s2,
                                          textsearchmode) == 0)
                    {                           /* rm double consonant */
                      len--;                    /* 1 character removed */
                      *sp = (char *)e;
                      *rmtrail = 1;
                    }
                  break;
	   }
       }
       PSstrrev(*sp, textsearchmode);		 /* re-reverse string */
   }
   return(len);
}

/************************************************************************/

#ifdef TEST

#define LNSZ 4096				  /* max size of a line */
#define WRDSALN 500				 /* max words on a line */
#define MINWRDSZ 5				   /* minimum word size */


 char *  suffix[]   = {
    "'","anced","ancer","ances","atery","enced","encer",
    "ences","ibler","ment","ness","tion","able","less",
    "sion","ance","ious","ible","ence","ship","ical",
    "ward","ally","atic","aged","ager","ages","ated",
    "ater","ates","iced","icer","ices","ided","ider",
    "ides","ised","ises","ived","ives","ized","izer",
    "izes","ncy","ing","ion","ity","ous","ful",
    "tic","ish","ial","ory","ism","age","ist",
    "ate","ary","ual","ize","ide","ive","ier",
    "ess","ant","ise","ily","ice","ery","ent",
    "end","ics","est","ed","red",
    "res","ly","er","al","at","ic","ty",
    "ry","en","nt","re","th","es","ul",
    "s",""
 };
 char *  prefix[]   = {
    "ante","anti","arch","auto","be","bi","counter",
    "de","dis","em","en","ex","extra","fore",
    "hyper","in","inter","mis","non","post","pre",
    "pro","re","semi","sub","super","ultra","un",
    ""
 };

/************************************************************************/

int				 /* returns the number of words located */
mkwordlst(s,lst,max)		 /* turns a string into a list of words */
char *s;						  /* the string */
char **lst;				  /* list of ptrs to be initted */
int max;					/* max n of ptrs in lst */
{
 int n; 				   /* number of words processed */
 char *end=s+strlen(s); 			     /* the end pointer */

 for(n=0;s<end && n<max;s++,n++)
    {
     for(;s<end && !isalpha((int)*(byte *)s);++s);
     for(lst[n]=s;s<end && isalpha((int)*(byte *)s);s++);
     *s='\0';
    }
 return(n);
}

/************************************************************************/

char lnbuf[LNSZ];
char *wrdlst[WRDSALN];

main(argc,argv)
int argc;
char **argv;
{
 int npre=initprefix(prefix);
 int nsuf=initsuffix(suffix);
 int nwrds,i,len,plen,fl,minwrdsz=MINWRDSZ;

 for(--argc,++argv;argc>0 && **argv=='-';argc--,argv++)
 {
    switch(*++*argv)
    {
     case 'm': minwrdsz=atoi(++*argv); break;
    }
 }


/*
  while(gets(lnbuf))
    {
     char *s=lnbuf;
     rmsuffix(&s,suffix,nsuf,5,1);
     rmprefix(&s,prefix,npre,5,1);
     puts(s);
    }
*/



 while(gets(lnbuf))
    {
     nwrds=mkwordlst(lnbuf,wrdlst,WRDSALN);
     for(i=0;i<nwrds;i++)
	{
	 printf("%s\t",wrdlst[i]);
	 if(argc==1){
	    rmsuffix(&wrdlst[i],suffix,nsuf,minwrdsz,1);
	    len=rmprefix(&wrdlst[i],prefix,npre,minwrdsz);
	    printf("(%d)%s\n",len,wrdlst[i]);
	 }else{
	    plen=strlen(wrdlst[i]);
	    fl=RM_INIT;
	    while(1){
	       len=rm1suffix(&wrdlst[i],suffix,nsuf,minwrdsz,&fl);
	       printf("(%d)%s, ",len,wrdlst[i]);
	       if(len==plen) break;
	       plen=len;
	    }
	    printf("\n");
	 }
	}
    }
}
#endif /* TEST */
