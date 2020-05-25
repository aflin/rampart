#ifdef mvs
#  include "mvsfix.h"
#endif
/**********************************************************************/
/*#define TEST*/
/**********************************************************************
**
** @(#)sregex.c - partial implementation of System V regcmp() and regex()
**   conforms to grep (not exactly SysV regcmp(3x))
**   not have mult expressions
**   only first arg to sregcmp() used
**   only first 2 args to sregex() used
**
** adapted from grep.c - 8/10/88 - MAW
**
***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#ifdef unix
#else
#  ifdef MVS
#     if MVS_C_REL > 1
#        include <stddef.h>
#     else
#        include <stdefs.h>
#     endif
#  else
#  endif
#endif
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include "os.h"
#include "sregex.h"
#include "mmsg.h"
#ifdef _WIN32
#  define STATIC
#else
#  define STATIC static
#endif


static char *_reg_advance ARGS((char *,char *,char *));/* internal calls */
static int  _reg_ecmp     ARGS((char *,char *,int));

#ifdef TEST
#  include "tstregex.c"
#endif                                                        /* TEST */

typedef enum TOK_tag
{
	CBRA   = 1,			/* `\(' ? */
	CCHR   = 2,			/* as-is char follows */
	CANCH  = 3,			/* CCHR | STAR ? */
	CDOT   = 4,			/* `.' i.e. any char */
	CDOT_STAR = 5,			/* CDOT | STAR */
	CCL    = 6,			/* `[...]' char class; bytes follow */
	CCL_STAR = 7,			/* CCL | STAR */
	NCCL   = 8,
	NCCL_STAR = 9,
	CDOL   = 10,			/* `$' at end (right anchor)*/
	CEOF   = 11,			/* end of expression */
	CKET   = 12,			/* `\)' ? */
	CKET_STAR = 13,
	CBRC   = 14,			/* `\<' ? */
	CLET   = 15,			/* `\>' ? */
	CBACK  = 18,			/* `\N' (1 <= N <= 9) N follows */
	CBACK_STAR = 19
}
TOK;

#define STAR    01				/* bit-OR'd with a TOK */

#define NBRA    9

/* WTF CHAR_CLASS_BYTE_SIZE should be 32 to include hi-bit bytes?
 * Until then we bounds-check `ch' to avoid overflow.  See also usage, and
 * possible char-class length assumptions of sregcmp()/sregprefix() callers:
 */
#define CHAR_CLASS_BYTE_SIZE	16
#define CHAR_CLASS_ADD_CHAR(classPtr, ch)		\
  (((byte)(ch) >> 3) >= CHAR_CLASS_BYTE_SIZE ? 0 :	\
   ((classPtr)[(byte)(ch) >> 3] |= (1 << ((byte)(ch) & 0x7))))
#define CHAR_CLASS_CHAR_IS_MEMBER(classPtr, ch)		\
  (((byte)(ch) >> 3) >= CHAR_CLASS_BYTE_SIZE ? 0 :	\
   ((classPtr)[(byte)(ch) >> 3] & (1 << ((byte)(ch) & 0x7))))


STATIC int  circf;
STATIC char *braslist[NBRA];
STATIC char *braelist[NBRA];

char *__loc1;                                      /* points to match */

/**********************************************************************/
size_t
sreglen(char *oe)
{
	char *p;
	size_t esize;

	for(p=oe,esize=1; *p != CEOF; p++,esize++)
	{
		switch((TOK)(*p & 0xFE))	/* i.e. without STAR */
		{
		case CBRA:
		case CCHR:
		case CKET:
		case CBACK:
			p++;
			esize++;
			break;
		case CDOT:
		case NCCL:
		case CDOL:
		case CEOF:
		case CBRC:
		case CLET:
			break;
		case CCL:
			p += CHAR_CLASS_BYTE_SIZE;
			esize += CHAR_CLASS_BYTE_SIZE;
			break;
		default:
			break;
		}
	}
	return esize;
}
/**********************************************************************/
char *
sregdup(oe)         /* MAW 09-24-91 - duplicate a compiled expression */
char *oe;
{
char *ne;
size_t esize;
   
   esize=sreglen(oe);
   ne=(char *)calloc(1,esize);
   if(ne==(char *)NULL) errno=ENOMEM;
   else{
      memcpy(ne,oe,esize);
   }
   return(ne);
}                                                    /* end sregdup() */
/**********************************************************************/

/**********************************************************************/
char *
sregcmp(expr, escChar)
CONST char      *expr;          /* (in) expression */
int             escChar;        /*(in) esc char (TX_REGEXP_DEFAULT_ESC_CHAR)*/
{
static CONST char fn[] = "sregcmp";
register int c;
register byte *ep;                        /* alias: expbuf, astr */
CONST byte      *sp, *cstart;
byte *lastep;
TOK	last;
int  cclcnt;
char bracket[NBRA], *bracketp;
int  closed;
char numbra;
char neg;
byte    tmp[4];
byte *expbuf = tmp, *expend = BYTEPN;	/* compiled expression buffer */
int  esize;
int  pass;				/* KNG 010412 */


                              /* MAW 03-17-00 calc actual buffer need */
  for (pass = 0; pass < 2; pass++)	/* KNG 010412 count then compile */
    {
      ep = expbuf;
      sp = (byte *)expr;
      last = '\0';
      lastep = BYTEPN;
      bracketp = bracket;
      closed = numbra = 0;
      if (*sp == '^')
	{
	  circf++;
	  sp++;
	}
      for (;;)
	{
	  if (ep >= expend && pass) goto zcerror;
	  if ((c = *(sp++)) != '*') lastep = ep;
	  if (c == escChar) goto zescchar; /* MAW 3/2/89 - ugly but simplest */
	  switch(c)
	    {
	    case '\0':
	      if (pass) { *ep = CEOF; ep[1] = '\0'; }; ep += 2;
              goto nextpass;
	    case '.':  last = CDOT; goto setch;
	    case '*':
	      if (lastep == BYTEPN || last == CBRA || last == CKET ||
		  last == CBRC || last == CLET)
		goto zdefchar;
	      last |= STAR;
	      if (pass) *lastep |= STAR;
	      continue;
	    case '$':
	      if (*sp != '\0') goto zdefchar;
	      last = CDOL;
	    setch:
	      if (pass) *ep = last;  ep++;  continue;
	    case '[':
	      if ((ep + CHAR_CLASS_BYTE_SIZE + 1 >= expend) && pass)
		      goto zcerror;
	      last = CCL;
	      if (pass) *ep = last;  ep++;
	      neg = 0;
	      if ((c = *(sp++)) == '^')
		{
		  neg = 1; c = *sp++;
		}
	      cstart = sp;
	      do
		{
		  if (c == '\0') goto zcerror;
		  if (c == '-' && sp > cstart && *sp != ']')
		    {				/* range e.g. `a-z' */
		      for (c = sp[-2]; c < *sp; c++)
			      if (pass) CHAR_CLASS_ADD_CHAR(ep, c);
		      sp++;
		    }
		  if (pass) CHAR_CLASS_ADD_CHAR(ep, c);
		}
	      while ((c = *(sp++)) != ']');
	      if (neg && pass)
		{
		  for (cclcnt = 0; cclcnt < CHAR_CLASS_BYTE_SIZE; cclcnt++)
			  ep[cclcnt] ^= (-1);	/* negate all bits */
		  ep[0] &= 0xfe;		/* do not match nul byte */
		}
	      ep += CHAR_CLASS_BYTE_SIZE;
	      continue;
	      /*case '\\':*/         /* MAW 3/2/89 - ugly but simplest */
	    zescchar:
	      if ((c = *(sp++)) == 0) goto zcerror;
	      if (c == '<') { last = CBRC; goto setch;  }
	      if (c == '>') { last = CLET;  goto setch; }
	      if (c == '(')
		{
		  if (numbra >= NBRA) goto zcerror;
		  *(bracketp++) = numbra;
		  last = CBRA;
		  if (pass) { *ep = last; ep[1] = numbra; }; numbra++; ep += 2;
		  continue;
		}
	      if (c == ')')
		{
		  if (bracketp <= bracket) goto zcerror;
		  last = CKET;
		  bracketp--;
		  if (pass) { *ep = last; ep[1] = *bracketp; };  ep += 2;
		  closed++;
		  continue;
		}
	      if (c >= '1' && c <= '9')
		{
		  if ((c -= '1') >= closed) goto zcerror;
		  last = CBACK;
		  if (pass) { *ep = last; ep[1] = (byte)c; }; ep += 2;
		  continue;
		}
	    zdefchar:
	    default:
	      last = CCHR;
	      if (pass) { *ep = last; ep[1] = (byte)c; }; ep += 2;
	    }
	}
    nextpass:
      if (pass) break;
      esize = ep - expbuf;
      errno = 0;
      /* KNG need calloc() for [] */
      if ((expbuf = (byte *)calloc(1, esize)) == BYTEPN)
	{
	  putmsg(MERR + MAE, fn, "Can't alloc %u bytes: %s",
		 (unsigned)esize, strerror(errno));
	  goto err;
	}
      expend = expbuf + esize;
    }
  return((char *)expbuf);
zcerror:
#ifdef EINVAL
  errno = EINVAL;
#endif /* EINVAL */
err:
  if (expbuf && expbuf != tmp) free(expbuf);
  return(CHARPN);
}                                                    /* end sregcmp() */
/**********************************************************************/

/**********************************************************************/
char *sregex(re,sub)
char *re, *sub;
{
register char *p1, *p2;                             /* alias: sub, re */
register int c;
char *nxt;

 p1=sub;
 p2=re;
 if(circf){
   if((nxt=_reg_advance(p1,p2,re)) != CHARPN) goto zfound;
   goto znfound;
 }
                                    /* fast check for first character */
 if(*p2==CCHR){
   c=p2[1];
   do{
     if(*p1!=(char)c) continue;
     if((nxt=_reg_advance(p1,p2,re)) != CHARPN) goto zfound;
   }while(*p1++);
   goto znfound;
 }
                                                 /* regular algorithm */
 do{
   if((nxt=_reg_advance(p1,p2,re)) != CHARPN) goto zfound;
 }while(*p1++);
znfound:
  return(NULL);
zfound:
  __loc1=p1;                                      /* start of pattern */
  return(nxt);                                     /* first non-match */
}                                                     /* end sregex() */
/**********************************************************************/

/**********************************************************************/
#define uletter(c) (isalpha((byte)(c)) || (c) == '_')
static char *_reg_advance(lp,ep,expbuf)
register char *lp, *ep;
char *expbuf;
{
register char *curlp;
char c;
char *bbeg, *nxt;
int ct;

  for(;;) switch(*ep++){
  case CCHR:
    if(*ep++ == *lp++) continue;
    return(NULL);
  case CDOT:
    if(*lp++) continue;
    return(NULL);
  case CDOL:
    if(*lp==0) continue;
    return(NULL);
  case CEOF:
    return(lp);
  case CCL:
    c= *lp++;
    if (CHAR_CLASS_CHAR_IS_MEMBER(ep, c)) {
      ep += CHAR_CLASS_BYTE_SIZE; continue;
    }
    return(NULL);
  case CBRA:
    braslist[(byte)*ep++]=lp; continue;
  case CKET:
    braelist[(byte)*ep++]=lp; continue;
  case CBACK:
    bbeg=braslist[(byte)*ep];
    if(braelist[(byte)*ep]==0) return(NULL);
    ct=braelist[(byte)*ep++]-bbeg;
    if(_reg_ecmp(bbeg,lp,ct)){
      lp += ct; continue;
    }
    return(NULL);
  case CBACK|STAR:
    bbeg=braslist[(byte)*ep];
    if(braelist[(byte)*ep]==0) return(NULL);
    ct=braelist[(byte)*ep++]-bbeg;
    curlp=lp;
    while(_reg_ecmp(bbeg,lp,ct)) lp+=ct;
      while(lp>=curlp){
        if((nxt=_reg_advance(lp,ep,expbuf)) != CHARPN) return(nxt);
        lp-=ct;
    }
    return(NULL);
  case CDOT|STAR:
    curlp=lp;
    while(*lp++) ;
    goto zstar;
  case CCHR|STAR:
    curlp=lp;
    while(*lp++ == *ep) ;
    ep++;
    goto zstar;
  case CCL|STAR:
    curlp=lp;
    do{
      c= *lp++;
    } while (CHAR_CLASS_CHAR_IS_MEMBER(ep, c));
    ep += CHAR_CLASS_BYTE_SIZE;
    goto zstar;
zstar:
    if(--lp==curlp) continue;
    if(*ep==CCHR){
      c=ep[1];
      do{
        if(*lp!=c) continue;
        if((nxt=_reg_advance(lp,ep,expbuf)) != CHARPN) return(nxt);
      }while(lp-->curlp);
      return(NULL);
    }
    do{
      if((nxt=_reg_advance(lp,ep,expbuf)) != CHARPN) return(nxt);
    }while(lp-->curlp);
    return(NULL);
  case CBRC:
    if(lp==expbuf) continue;
    if(uletter((int)(*(byte *)lp))||isdigit((int)(*(byte *)lp))){
      if(!uletter((int)((byte *)lp)[-1]) && !isdigit((int)((byte *)lp)[-1])) continue;
    }
    return(NULL);
  case CLET:
    if(!uletter((int)(*(byte *)lp)) && !isdigit((int)(*(byte *)lp))) continue;
    return(NULL);
  default:                                        /* internal screwup */
    return(NULL);
  }
}                                               /* end _reg_advance() */
/**********************************************************************/

/**********************************************************************/
static int _reg_ecmp(a,b,count)
char *a,*b;
int count;
{
register int cc=count;

  while(cc--) if(*a++ != *b++) return(0);
  return(1);
}                                                  /* end _reg_ecmp() */
/**********************************************************************/
/* Puts a constant prefix from the regular expression `a' into `b'.  Will
   return 1 if the expression is rooted at both ends, and has no wildcards,
   2 if it is rooted at the beginning, and allows any suffix, or 0 otherwise.
*/

int
sregprefix(a, b, n, sz, igncase)
char	*a;	/* (in) compiled regular expression (from sregcmp()?) */
char	*b;	/* (out) constant prefix from `a' */
size_t	n;	/* (in) size of `b' */
size_t	*sz;	/* (out) length of data put into `b' */
int	igncase;/* (in) nonzero: ignore case */
{
	char	*bt;

	if(!circf)
	{
		if(b && n >= 1)
			b[0] = '\0';
		*sz = 0;
		return 0;
	}
	*sz = 0;
	bt = b;
	while(*a && *sz < n)
	{
		switch(*a++)
		{
			case	CCHR:
				*bt++ = *a++;
				(*sz)++;
				break;
			case	CDOL:
				*bt = '\0';
				return 1;
			case	CCL:  /* MAW 09-07-99 - handle ranges */
			if(igncase){/* [Aa] ==> a etc, so indexed ignorecase works */
			 int c, l=(-1);
				for (c = 0; c < CHAR_CLASS_BYTE_SIZE*8; c++)
				{
				   if (CHAR_CLASS_CHAR_IS_MEMBER(a, c))
				   {
				      if (!isalpha((byte)(c)))
				      {
				         *bt = '\0';
				         return(0);
				      }
				      if (l==(-1)) l = tolower((byte)(c));
				      else if (tolower((byte)(c)) != l)
				      {
				         *bt = '\0';
				         return(0);
				      }
				   }
				}
				*bt++ = l;
				(*sz)++;
				a += CHAR_CLASS_BYTE_SIZE;
			}
			else
			{
				*bt = '\0';
				return 0;
			}
				break;
			case CDOT|STAR:
				if(*a == CDOL)
				{
					*bt = '\0';
					return 2;
				}
			default:
				*bt = '\0';
				return 0;
		}
	}
	*bt = '\0';
	return 2;
}

/******************************************************************/

