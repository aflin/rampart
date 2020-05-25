/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"
#include "sregex.h"


typedef struct TX_MATCH_tag {
	int	magic;		/* TX_INTERNAL_MATCHES */
	size_t	size;		/* Size of this structure (malloced) */
	size_t	orig_off;	/* Offset in data to the original string */
	char	data[1];	/* Data region hold compiled \0 original */
} TX_MATCH ;
#define TX_MATCHPN	((TX_MATCH *)NULL)

static CONST char       CannotAllocMem[] =
"Cannot alloc %lu bytes of memory: %s";
static CONST char	WrongSubType[] = "Wrong FTN_INTERNAL subtype #%u = %s";
static CONST char	NullFtInternal[] = "NULL";

/**********************************************************************/
#define ADD(c) (i?(int)(*(p++)=(char)(c)):++sz)
#define PATH_ESC	'\\'
/******************************************************************/
/*	This function translates a string into a regexp string.
	It allows a translation mode to be specified.

	Mode	0 - Standard SQL. (_, %)
		1 - Unix style (?, *)
		2 - Regexp.
*/

static char	*TXtransexp ARGS((CONST char *, int));

static char *TXtransexp(expr, mode)  
CONST char	*expr;
int	mode;
{
	int i, sz=0;
	char *newp = CHARPN, *p = CHARPN;
	CONST byte	*wrk;	/* `byte' for isalpha() etc. safety */
#ifdef NEVER
	static CONST char escme[]="*?.^$+{}()";    /* chars to escape in RE */
#else
	static CONST char escme[]="*.\\";    /* chars to escape in RE */
#endif
	char	mce, sce, ignorecase;

	if (globalcp == APICPPN) globalcp = TXopenapicp();
	ignorecase = (TXCFF_GET_CASESTYLE(globalcp->stringcomparemode) ==
		      TXCFF_CASESTYLE_IGNORE);

	switch (mode)
	{
	case 0:
		mce = '%';
		sce = '_';
		break;
	case 1:
		mce = '*';
		sce = '?';
		break;
	case 2:
		return strdup(expr);
	default:
		putmsg(MERR + UGE, "TXtransexp", "Invalid mode %d", mode);
		return(CHARPN);
	}
	for(i=0;i<2;i++)
	{                    /* 2 passes, 0 count, 1 build */
		ADD('^');
		for(wrk=(CONST byte *)expr;*wrk!='\0';++wrk)
		{
			switch(*wrk)
			{
				case '?':
				case '_':
					if(*wrk == sce)
						ADD('.');
					else
					{
						if(strchr(escme,*wrk)!=NULL)
							ADD(PATH_ESC);
						ADD(*wrk);
					}
					break;
				case '*':
				case '%':
					if(*wrk == mce)
					{
						ADD('.');
						ADD('*');
					}
					else
					{
						if(strchr(escme,*wrk)!=NULL)
							ADD(PATH_ESC);
						ADD(*wrk);
					}
					break;
				case PATH_ESC:
					if(*++wrk=='\0')
					{
						--wrk;
						break;
					}
					if((*wrk != mce) && (*wrk != sce))
					{
						ADD(PATH_ESC);
						ADD(PATH_ESC);
					}
				default:
                             /* MAW 09-02-99 - add ignorecase support */
					if(ignorecase && isalpha(*wrk))
					{/* assume escme not include alpha */
					   ADD('[');
					   ADD(toupper(*wrk));
					   ADD(tolower(*wrk));
					   ADD(']');
					}
					else
					{
					   if(strchr(escme,*wrk)!=NULL)
						ADD(PATH_ESC);
					   ADD(*wrk);
					}
			}
		}
		ADD('$'); ADD('\0');
		if(i==0)
		{
			if((newp=(char *)malloc(sz))==NULL)
			{
				errno=ENOMEM;
				break;
			}
			p=newp;
		}
	}
	return(newp);
}

/******************************************************************/

static int TXmatchmode = 0;


/* ------------ FTN_INTERNAL methods; called by ftinternal.c  ------------- */

void *
tx_fti_matches_open(usr, sz)
CONST char	*usr;	/* (in) MATCHES pattern */
size_t		sz;	/* (in) size of `usr' */
/* Opens an FTN_INTERNAL sub-type FTI_matches object.
 * Called by ftinternal.c.
 */
{
	static CONST char	fn[] = "tx_fti_matches_open";
	char	*x, *y;
	TX_MATCH *rc;
	size_t  ylen;
	size_t	n;

	if (usr == CHARPN) usr = "";
	x = TXtransexp(usr, TXmatchmode);
	if (x == CHARPN) return(NULL);
	y = sregcmp(x, TX_REGEXP_DEFAULT_ESC_CHAR);
	DBGMSG(1, (999, NULL, "REGEX = %s(%s)", x, usr));
	free(x);
	if (y == CHARPN) return(CHARPN);
	ylen = sreglen(y);
	n = sizeof(TX_MATCH) + ylen + 1 + sz;
	if ((rc = (TX_MATCH *)malloc(n)) == TX_MATCHPN)
	{
		putmsg(MERR + MAE, fn, CannotAllocMem,
			(long)n, TXstrerror(TXgeterror()));
		return(CHARPN);
	}
	rc->magic = TX_INTERNAL_MATCHES;
	rc->size = n;
	memcpy(rc->data, y, ylen);
	rc->data[ylen] = '\0';
	memcpy(rc->data + ylen + 1, usr, sz);
	rc->data[ylen + 1 + sz] = '\0';
	rc->orig_off = ylen + 1;
	TXfree(y);
	return(rc);
}

void *
tx_fti_matches_close(obj)
void	*obj;	/* TX_MATCH * */
/* Closes FTN_INTERNAL sub-type FTI_matches object `obj'.
 */
{
	TX_MATCH	*ms = (TX_MATCH *)obj;

	if (ms == TX_MATCHPN) goto done;
	free(ms);
done:
	return(NULL);
}

void *
tx_fti_matches_dup(obj)
void	*obj;	/* TX_MATCH * */
/* Duplicates `obj', ie. so caller can write to it.
 */
{
	static CONST char	fn[] = "tx_fti_matches_dup";
	TX_MATCH		*ms = (TX_MATCH *)obj, *rc;

	if ((rc = (TX_MATCH *)calloc(1, ms->size)) == TX_MATCHPN)
	{
		putmsg(MERR + MAE, fn, CannotAllocMem,
			(long)ms->size, TXstrerror(TXgeterror()));
		return(NULL);
	}
	memcpy(rc, ms, ms->size);
	return(rc);
}

CONST char *
tx_fti_matches_tostr(obj)
void	*obj;	/* TX_MATCH * */
/* Converts `obj' to string.  Returns a const that is valid for life of `obj'.
 */
{
	TX_MATCH	*ms = (TX_MATCH *)obj;

	return(ms->data + ms->orig_off);
}

/* ------------------------------------------------------------------------ */

char *
TXmatchorig(void *v)
{
	TX_MATCH *ms = (TX_MATCH *)v;

	if(ms)
		return ms->data + ms->orig_off;
	return NULL;
}

/******************************************************************/

int
TXsetmatchmode(m)
int	m;
{
	int om = TXmatchmode;
	if (m >= 0 && m <= 2)
	{
		TXmatchmode = m;
		return om;
	}
	putmsg(MWARN+UGE, "matches", "Invalid mode %d", m);
	return -1;
}

int
TXgetmatchmode(void)
{
	return(TXmatchmode);
}

/******************************************************************/

#if 0	/* deprecated  KNG 20060215 */
char *
TXsregexi(TX_MATCH *ms, char *data)
{
	if(ms && ms->magic == TX_INTERNAL_MATCHES)
	{
		return sregex(ms->data, data);
	}
	return NULL;
}
#endif /* 0 */

/******************************************************************/

char *
TXmatchesi(buf, fti)
char		*buf;
ft_internal	*fti;
/* Does `buf MATCHES fti'.
 */
{
	static CONST char	fn[] = "TXmatchesi";
	char	 *ret;
	TX_MATCH *v2;

	if (fti == ft_internalPN || tx_fti_gettype(fti) != FTI_matches)
	{
		putmsg(MERR + UGE, fn, WrongSubType,
			(fti ? (unsigned)tx_fti_gettype(fti) : 0U),
			(fti ? tx_fti_type2str(tx_fti_gettype(fti)) :
			 NullFtInternal));
		goto err;
	}
	v2 = (TX_MATCH *)tx_fti_getobj(fti);
	if(!v2) goto err;
	ret = sregex(v2->data, buf);
	goto done;
err:
	ret = CHARPN;
done:
	return(ret);
}

ft_int
TXmatchesc(f1, f2)
FLD	*f1;	/* char field data */
FLD	*f2;	/* char field pattern */
{
	char	*rc = 0;
	char	*v1, *v2;

	v1 = getfld(f1, NULL);
	v2 = getfld(f2, NULL);
	DBGMSG(1, (999, NULL, "Checking %s (%s)", v1, v2));
	if(v2)
	{
		char *x, *y;

		x = TXtransexp(v2, TXmatchmode);
		y = sregcmp(x, TX_REGEXP_DEFAULT_ESC_CHAR);
		rc = sregex(y, v1);
		TXfree(y);
		TXfree(x);
	}
	DBGMSG(1, (999, NULL, "Result %s", rc));
	if(rc)
		return 1;
	return 0;
}

char *
TXmatchgetr(FLD *f, size_t *sz)
/* Returns NULL on error.
 */
{
	static CONST char	fn[] = "TXmatchgetr";
	ft_internal	*fti;
	TX_MATCH *ms;

	switch (f->type & DDTYPEBITS)
	{
	case FTN_CHAR:
		return getfld(f, sz);
	case FTN_INTERNAL:
		fti = (ft_internal *)getfld(f, sz);
		if (fti == ft_internalPN || tx_fti_gettype(fti) !=FTI_matches)
		{
			putmsg(MERR + UGE, fn, WrongSubType,
				(fti ? (unsigned)tx_fti_gettype(fti) : 0U),
				(fti ? tx_fti_type2str(tx_fti_gettype(fti)) :
				 NullFtInternal));
			*sz = 0;
			return(CHARPN);
		}
		ms = (TX_MATCH *)tx_fti_getobj(fti);
		if (ms == TX_MATCHPN)
		{
			putmsg(MERR + UGE, fn, "Missing TX_MATCH object");
			*sz = 0;
			return(CHARPN);
		}
		*sz = strlen(ms->data);
		return ms->data;
	}
	return NULL;
}

#ifdef TEST
/******************************************************************/

teststrings(expbuf)
char	*expbuf;
{
	char	*tst[] = {
	"bob",
	"Aboband more",
	"More stuff before bob.",
	"No b here",
	"",
	};
	char	**x;

	for(x = tst; **x; x++)
	{
		printf("Checking string %s ... ", *x);
		if(sregex(expbuf, *x))
			printf("MATCH.\n");
		else
			printf("no match.\n");
	}
}

/******************************************************************/

main()
{
#define	ESIZE	8192

	char	inbuf[128];
	char	*expbuf;

	while(fgets(inbuf, sizeof(inbuf), stdin))
	{
		char	*out;

		out = inbuf + strlen(inbuf) -1;
		while(out > inbuf && isspace(*out))
		{
			*out = '\0';
			out--;
		}
		printf("In %s\n", inbuf);
		out = TXtransexp(inbuf, 0);
		if(out)
		{
			expbuf = sregcmp(out, TX_REGEXP_DEFAULT_ESC_CHAR);
			teststrings(expbuf);
			printf("Out %s\n", out);
			free(out);
			free(expbuf);
		}
		out = TXtransexp(inbuf, 1);
		if(out)
		{
			expbuf = sregcmp(out, TX_REGEXP_DEFAULT_ESC_CHAR);
			teststrings(expbuf);
			printf("Out %s\n", out);
			free(out);
			free(expbuf);
		}
		out = TXtransexp(inbuf, 2);
		if(out)
		{
			expbuf = sregcmp(out, TX_REGEXP_DEFAULT_ESC_CHAR);
			teststrings(expbuf);
			printf("Out %s\n", out);
			free(out);
			free(expbuf);
		}
	}
}

/******************************************************************/
#endif /* TEST */
