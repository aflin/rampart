#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <ctype.h>
#include "api3.h"
/*
#include "vortex.h"
*/
#include "texint.h"
/*
#include "../vortex/vufunc.h"	*/		/* for VXsandr() prototype */


#ifndef FFSPN
#define FFSPN ((FFS *)NULL)
#endif
#ifndef INTPN
#define INTPN ((int *)NULL)
#endif

/************************************************************************/

#ifdef MVS
#define EESC '/'
#define OSET '<'
#define CSET '>'
#define EBEG '('
#define EEND ')'
#define ENOT ':'
#define EBOL ':'
#else
				  /* MAW 07-20-92 - add macintosh def */
#if defined(MSDOS) || defined(unix) || defined(macintosh) || defined(AOSVS) || defined(VMS)
#define EESC '\\'
#define OSET '['
#define CSET ']'
#define EBEG '{'
#define EEND '}'
#define ENOT '^'
#define EBOL '^'
#else
stop.
#endif /* MSDOS || unix */
#endif /* MVS */

/******************************************************************/

#define EORS  -255		/* end of replace string */
#define DITTO -256		/* dup the input to the output */
#define PHEX  -257		/* print the output in hex */
#define NUMB  -258		/* number the hits */
#define SUBCP -259		/* copy subexpression */
#define ENTIRE_EXPR     -260

#ifdef EPI_ENABLE_RE2
#  define Re2Enabled    1
#else /* !EPI_ENABLE_RE2 */
static int      Re2Enabled = -1;
#endif /* !EPI_ENABLE_RE2 */


static int parserepl ARGS((byte *s, byte *e, int *a, size_t aLen));
static int
parserepl(s, e, a, aLen)
byte *s;	/* (in) replacement string */
byte	*e;	/* (in) end of `s' */
int *a;		/* (out) parsed codes */
size_t	aLen;	/* (in) length of `a' */
/* Parses <sandr> replacement string `s' into `a'.
 * NOTE: see also rex.c:parserepl().
 * Returns nonzero on success.
 */
{
	int	*aOrg = a;

#ifndef EPI_ENABLE_RE2
	if (Re2Enabled == -1) Re2Enabled = (getenv("EPI_ENABLE_RE2") != NULL);
#endif /* !EPI_ENABLE_RE2 */

	for (; s < e; s++, a++)
	{
		if ((size_t)(a - aOrg) >= aLen) return(0);
		if (*s == EBEG)			/* `{' */
		{
			char *t;
			int	errnum;

			for (t = (char *)s; t < (char *)e && *t != EEND; t++);
			if (t >= (char *)e || *t != EEND)  /* missing `}' */
				return (0);
			*a = -(TXstrtoi((char *)++s, (char *)e, NULL, 10,
					&errnum));
			if (*a <= EORS || *a >= 0 || errnum != 0)
				return (0);	/* bad #; overlaps specials */
			s = (byte *)t;
		}
		else if (*s == '+')
			*a = NUMB;
		else if (*s == '#')
		{
			*(a++) = PHEX;
			if ((size_t)(a - aOrg) >= aLen) return(0);
			if (s + 1 < e && isdigit(*(s + 1))) /* MAW 09-09-96 */
			{
				int	errnum;

				s++;
				*a = TXstrtoi((char *)s, (char *)e, NULL, 10,
					      &errnum);
				if (*a <= 0 || errnum != 0)
					return (0);
				for (; s + 1 < e && isdigit(*(s + 1)); s++);
			}
			else
				*a = 0;
		}
		else if (*s == '?')
			*a = DITTO;
		else if (*s == EESC)		/* `\' */
		{
			++s;
			if (s >= e)
				return (0);
			else if (isdigit(*s))
			{
				int	errnum;

				*(a++) = SUBCP;
				if ((size_t)(a - aOrg) >= aLen) return(0);
				*a = TXstrtoi((char*)s, (char *)e, NULL, 10,
					      &errnum);
				if (*a <= 0 || errnum != 0)
					return (0);
				for (; s + 1 < e && isdigit(*(s + 1)); s++);
			}
			else if (*s == 'x' || *s == 'X')
			{
				static CONST byte hex[] = "0123456789abcdef";
				int hi, lo;
				byte hic, loc;

				++s;	/* point past the x */
				if (s >= e)		/* unexpected EOS */
					return (0);
				hic = tolower(*s);	/* point past the first byte */
				for (hi = 0; hex[hi] != '\0' && hic != hex[hi]; hi++);	/* cvt to hex */
				if (hi >= 16)
					return (0);
				++s;
				if (s >= e)		/* unexpected EOS */
					return (0);
				loc = tolower(*s);	/* point past the second hex byte */
				for (lo = 0; hex[lo] != '\0' && loc != hex[lo]; lo++);
				if (lo >= 16)
					return (0);
				*a = ((hi << 4) | lo) & 0xff;	/* convert it into a */
			}
			else if (Re2Enabled && *s == '&')
				*a = ENTIRE_EXPR;
			else
				*a = (int) *s;	/* literal character */
		}
		else
			*a = (int) *s;	/* just a normal character */
	}
	*a = EORS;
	return (1);
}

/******************************************************************/

static char *isandr ARGS((FFS *ex, char *rs, char *rsEnd, byte *srchbuf,
			  byte *srchbufEnd, size_t *replLen));
static char *			/* retns ok or !ok */
isandr(ex, rs, rsEnd, srchbuf, srchbufEnd, replLen) /* search and replace */
FFS *ex;			/* search expression */
char *rs;			/* replace string */
char *rsEnd;			/* (in) end of `rs' */
byte *srchbuf;			/* buffer to search */
byte *srchbufEnd;		/* its end */
size_t	*replLen;		/* (out) length of returned buffer */
{
	static CONST char fn[] = "isandr";
	byte *loc, *locEnd;
	byte *lastwrite, *subhit, *esubhit, *bl, *hit;
	int i;
	long hitcount = 0;
	char *replstr = CHARPN;
	size_t	replstrNumAlloced = 0, nwrite = 0;
	int *tmpra = INTPN, *ra;
	char *s;
	size_t n, hitSize;
	int ara[DYNABYTE];	/* replace array */
	byte	*prevLoc = NULL, *prevLocEnd = NULL;
	int	numSameHits = 0;
	char	tmpBuf[256];
	TXPMBUF	*pmbuf = NULL;
	/* Append `n' bytes at `s' to `replstr' at `nwrite', advancing it: */
#define OUTPUT(s, n)					\
 {	if (!TX_EXPAND_ARRAY(pmbuf, &replstr, nwrite, &replstrNumAlloced,(n)))\
		goto err;				\
	if ((n) == 1)		/* optimize */		\
		replstr[nwrite++] = *(s);		\
	else						\
	{						\
		memcpy(replstr + nwrite, (s), (n));	\
		nwrite += (n);				\
	}						\
 }
	/* Bug 2084: use CONTINUESEARCH for no infinite loops and <rex>
	 * consistency:
	 */
#ifdef TX_NO_INFINITE_LOOP_SANDR
#  define contOp	CONTINUESEARCH		/* consistent w/<rex> */
#  define contBufRef	(&srchbuf)
#else /* !TX_NO_INFINITE_LOOP_SANDR */
	static int	noInfLoop = -1;
	TXPMOP		contOp;
	byte		**contBufRef;

	if (noInfLoop == -1)
		noInfLoop = (getenv("TX_NO_INFINITE_LOOP_SANDR") != NULL);
	if (noInfLoop)
	{
		contOp = CONTINUESEARCH;
		contBufRef = &srchbuf;
	}
	else
	{
		contOp = SEARCHNEWBUF;		/* old-style; avoid overlap */
		contBufRef = &locEnd;
	}
#endif /* !TX_NO_INFINITE_LOOP_SANDR */


#ifndef EPI_ENABLE_RE2
	if (Re2Enabled == -1) Re2Enabled = (getenv("EPI_ENABLE_RE2") != NULL);
#endif /* !EPI_ENABLE_RE2 */

	for (n = 1, s = rs; s < rsEnd; s++, n++)
		if (*s == '#')
			n++;

	if (n > TX_ARRAY_LEN(ara))
	{
		if (!(ra = tmpra = (int *) TXmalloc(pmbuf, fn, n*sizeof(int))))
			goto err;
	}
	else
	{
		ra = ara;
		n = TX_ARRAY_LEN(ara);
	}

	if (!parserepl((byte *)rs, (byte *)rsEnd, ra, n))
	{
		putmsg(MWARN + UGE, fn, "Invalid replace string");
		goto err;
	}

	hitcount = 0;		/* JMT 98-08-17 Reset count so starts at 1 */
	lastwrite = srchbuf;
#define INBOUND(a)      ((a) >= srchbuf && (a) < srchbufEnd)
	for (loc = getrex(ex, srchbuf, srchbufEnd, SEARCHNEWBUF);
	     loc;
	     prevLoc = loc, prevLocEnd = locEnd,
		     loc = getrex(ex, *contBufRef, srchbufEnd, contOp))
	{
		locEnd = loc + rexsize(ex);

		/* Since we switched to one-pass <sandr>, while the
		 * infinite-loop bug is still unfixed we will not only
		 * infinitely-loop but consume mem too; so stop
		 * infinite loops.  This check should not be needed
		 * once we use CONTINUESEARCH, as REX does the check
		 * for us, but leave it in just in case:
		 */
		if (loc == prevLoc && locEnd == prevLocEnd)
		{
			if (++numSameHits >= 100)
			{
				putmsg(MERR + UGE, fn,
   "Potential infinite loop detected: stopping at %d same-place replacements",
				       numSameHits);
				break;
			}
		}
		else
			numSameHits = 0;

		/* Bug 2084: when using CONTINUESEARCH, hits may
		 * overlap, so `loc' may be *before* `lastwrite':
		 */
		if (loc > lastwrite)		/* flush non-hit data */
		{
			n = (size_t)(loc - lastwrite);
			OUTPUT(lastwrite, n);
		}
		if (locEnd > lastwrite)
			lastwrite = locEnd;	/* mv ptr past pattern */

		for (i = 0; ra[i] != EORS; i++)
		{
			if (ra[i] > EORS && ra[i] < 0)
			{
				bl = loc - (ra[i] + 1);
				if (INBOUND(bl))
					OUTPUT(bl, 1);
				continue;
			}
			switch (ra[i])
			{
			case PHEX:
				i++;
				if (ra[i] == 0)
				{
					bl = loc + i;
					if (INBOUND(bl))
					{
						n = htsnpf(tmpBuf,
							   sizeof(tmpBuf),
						  "\\X%02X", (unsigned)(*bl));
						n = TX_MIN(n,sizeof(tmpBuf)-1);
						OUTPUT(tmpBuf, n);
					}
				}
				else
				{
					subhit = rexshit(ex, ra[i] - 1);
					if (subhit != BYTEPN)
					{
						esubhit = subhit + rexssize(ex, ra[i] - 1);
						for (; subhit < esubhit; subhit++)
						{
							n = htsnpf(tmpBuf,
							       sizeof(tmpBuf),
					      "\\X%02X", (unsigned)(*subhit));
							n = TX_MIN(n, sizeof(tmpBuf)-1);
							OUTPUT(tmpBuf, n);
						}
					}

				}
				break;
			case DITTO:
				bl = loc + i;
				if (INBOUND(bl))
					OUTPUT(bl, 1);
				break;
			case NUMB:
				n = htsnpf(tmpBuf, sizeof(tmpBuf), "%ld",
					   (long)++hitcount);
				n = TX_MIN(n, sizeof(tmpBuf) - 1);
				OUTPUT(tmpBuf, n);
				break;
			case SUBCP:
				++i;
				subhit = rexshit(ex, ra[i] - 1);
				if (subhit != BYTEPN)
				{
					n = rexssize(ex, ra[i] - 1);
					OUTPUT(subhit, n);
				}
				break;
			case ENTIRE_EXPR:
				if (Re2Enabled)
				{
					hit = rexhit(ex);
					hitSize = rexsize(ex);
					if (hit && hitSize > 0)
						OUTPUT(hit, hitSize);
				}
				else
					OUTPUT("?", 1);
				break;
			default:
				tmpBuf[0] = ra[i];
				OUTPUT(tmpBuf, 1);
				break;
			}
		}
	}
	/* flush end of buffer */
	if (srchbufEnd > lastwrite)
	{
		n = srchbufEnd - lastwrite;
		OUTPUT(lastwrite, n);
		lastwrite = srchbufEnd;
	}
	tmpBuf[0] = '\0';
	OUTPUT(tmpBuf, 1);			/* nul-terminate */
	/* Save mem by reducing `replstr' alloc to just what is needed: */
	if (replstrNumAlloced > nwrite)
		replstr = TXrealloc(pmbuf, fn, replstr, nwrite);
	goto done;

err:
	replstr = TXfree(replstr);
	nwrite = replstrNumAlloced = 0;
done:
	if (tmpra != INTPN)
		tmpra = TXfree(tmpra);
	if (replLen) *replLen = (nwrite > (size_t)0 ? nwrite - 1 : 0);
	return (replstr);
#undef OUTPUT
}

/******************************************************************/

/* WTF function pointer to avoid link issues with Vortex: */
int     (*TXvxRetTypeFunc)(FTN type, int *len, int num, int isType) = NULL;

char **
VXsandr(search, replace, data)
char **search;
char **replace;
char **data;
{
	static CONST char fn[] = "VXsandr";
	FFS *rpm = FFSPN;
	char **y = CHARPPN, *replstr;
	int i, j, nrep;
	int ndata;
	int freelist = 0;
	size_t	retLen;
	int	*retLens = NULL;
	FTN	retType = (FTN_CHAR | DDVARBIT);
	int	*dataLens = NULL;
	int	dataLensIsAlloced = 0;

	if (search == CHARPPN ||
	    replace == CHARPPN ||
	    data == CHARPPN)
		goto err;
	ndata = TXstrlstcount(data) + 1;
	nrep = TXstrlstcount(replace);
	for (i = 0; search[i] != CHARPN; i++)
	{
		rpm = openrex((byte *)search[i], TXrexSyntax_Rex);
		if (!rpm)
			goto err;
		if (!vokrex(rpm, search[i]))
			goto err;	/* KNG 970102 */
		if (i < nrep)
			replstr = replace[i];
		else
			replstr = "";
#ifdef TEST
		printf("Search = %s Replace = %s\n", search[i], replstr);
#endif
		if ((y = (char **) TXcalloc(TXPMBUFPN, fn, ndata,
					    sizeof(char *))) == CHARPPN)
			goto err;
		if (!(retLens = (int *)TXcalloc(TXPMBUFPN, fn, ndata,
						sizeof(int))))
			goto err;
		for (j = 0; data[j] != CHARPN; j++)
		{
			y[j] = isandr(rpm, replstr, replstr + strlen(replstr),
				      (byte *)data[j], (byte *)data[j] +
				      (dataLens ? (size_t)dataLens[j] :
				       strlen(data[j])),
				      &retLen);
			retLens[j] = (int)retLen;
#ifdef TEST
			printf("in = %s out = %s\n", data[j], y[j]);
#endif
			if (freelist)
				data[j] = TXfree(data[j]);
		}
		y[j] = CHARPN;
		if (freelist)
			data = TXfree(data);
		data = y;
		freelist = 1;
		if (dataLens && dataLensIsAlloced)
			dataLens = TXfree(dataLens);
		if (search[i + 1])		/* use `retLens' next time */
		{
			dataLens = retLens;
			dataLensIsAlloced = 1;
			retLens = NULL;
		}
		rpm = closerex(rpm);	/* KNG 970516 close it; was mem leak */
	}
	if (y == CHARPPN)	/* no searches; must still dup data */
	{
		if ((y = (char **) TXcalloc(TXPMBUFPN, fn, ndata,
					    sizeof(char *))) == CHARPPN)
			  goto err;

		for (i = 0; data[i] != CHARPN; i++)
			if ((y[i] = TXstrdup(TXPMBUFPN, fn, data[i])) == CHARPN)
				goto err;
		y[i] = CHARPN;
	}
	goto done;

      err:
	y = CHARPPN;		/* wtf free it? */
      done:
	if (rpm != FFSPN)
		rpm = closerex(rpm);
	if (retLens && y)
	{
		if (TXvxRetTypeFunc)		/* in Vortex */
		{
			/* If there are embedded nul(s) in the return data,
			 * set the type to varbyte.  WTF do this for SQL too:
			 */
			for (i = 0;
			     y[i] && strlen(y[i]) == (size_t)retLens[i];
			     i++);
			if (y[i]) retType = (FTN_BYTE | DDVARBIT);
			TXvxRetTypeFunc(retType, retLens, ndata - 1, 0);
		}
		else
			retLens = TXfree(retLens);
	}
	if (dataLens && dataLensIsAlloced) dataLens = TXfree(dataLens);
	return y;
}

/******************************************************************/

#ifdef TEST

main(argc, argv)
int argc;
char *argv[];
{
	char **x;
	int i;

	x = VXsandr(argv + 1, argv + 2, argv);
	for (i = 0; x[i]; i++)
		puts(x[i]);
}

#endif

#ifdef BECMDLINESANDR
static char *Prog;

static void
help()
{
	printf("Usage: %s -ssearch -rreplace [-ssearch -rreplace ...] files\n", Prog);
	exit(TXEXIT_INCORRECTUSAGE);
}

void
main(argc, argv)
int argc;
char **argv;
{
#define MAXSR 20
	char *srch[MAXSR + 1];
	char *repl[MAXSR + 1];
	char *data[2];
	char **ret;
	char *fn;
	FILE *fp;
	int ns = 0, nr = 0, nd;

	Prog = argv[0];
	for (--argc, ++argv; argc > 0 && **argv == '-'; argc--, argv++)
	{
		switch (*++*argv)
		{
		case 's':
			srch[ns++] = ++*argv;
			break;
		case 'r':
			repl[nr++] = ++*argv;
			break;
		default:
			help();
		}
	}
	if (argc < 1)
		help();
	srch[ns] = CHARPN;
	repl[nr] = CHARPN;
	data[1] = CHARPN;
	for (; argc > 0; argc--, argv++)
	{
		fn = *argv;
		if ((fp = fopen(fn, "rb")) == FILEPN)
			perror(fn);
		else
		{
			fseek(fp, 0l, SEEK_END);
			nd = (int) ftell(fp);
			if (!(data[0] = TXmalloc(TXPMBUFPN, fn, nd + 1)))
				puts("no memory");
			else
			{
				if ((nd = fread(data[0], 1, nd, fp)) < 0)
					perror(fn);
				else
				{
					fclose(fp);
					ret = VXsandr(srch, repl, data);
					if (ret != CHARPPN && ret[0] != CHARPN)
					{
						if ((fp = fopen(fn, "wb")) == FILEPN)
							perror(fn);
						else
						{
							nd = fwrite(data[0], 1, strlen(data[0]), fp);
							if (nd != strlen(data[0]))
								perror(fn);
						}
					}
				}
			}
			if (fp != FILEPN)
				fclose(fp);
		}
	}
	exit(TXEXIT_OK);
}
#endif
