/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#ifdef _WINDLL
int __cdecl sscanf(const char *, const char *, ...);
#endif /* _WINDLL */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "sizes.h"
#include "os.h"
#include "dbquery.h"
#include "texint.h"
#include "fldmath.h"
#include "fldops.h"
#include "parsetim.h"
#include "sregex.h"
#include "cgi.h"				/* for htsnpf() */
#include "httpi.h"
#include "jansson.h"


#ifndef F_OK
#define F_OK 0
#define R_OK 4
#define W_OK 2
#endif


/* All possible (256) strlst delimiters, in preferred order.
 * Note that various functions in fonumsl.c, predopt.c etc. assume the
 * first choice (TxPrefStrlstDelims[0]) is safe for items that are
 * all-[hex]digits or empty:
 */
CONST char	TxPrefStrlstDelims[256] = ",|-/~@\"$%&':;=+.^_*`#!?<>\\(){}[]0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz\177\200\201\202\203\204\205\206\207\210\211\212\213\214\215\216\217\220\221\222\223\224\225\226\227\230\231\232\233\234\235\236\237\240\241\242\243\244\245\246\247\250\251\252\253\254\255\256\257\260\261\262\263\264\265\266\267\270\271\272\273\274\275\276\277\300\301\302\303\304\305\306\307\310\311\312\313\314\315\316\317\320\321\322\323\324\325\326\327\330\331\332\333\334\335\336\337\340\341\342\343\344\345\346\347\350\351\352\353\354\355\356\357\360\361\362\363\364\365\366\367\370\371\372\373\374\375\376\377 \037\036\035\034\033\032\031\030\027\026\025\024\023\022\021\020\017\016\015\014\013\012\011\010\007\006\005\004\003\002\001\000";

static CONST char	CommaWhite[] = ",\r\n\v\f \t";
#define WhiteSpace	(CommaWhite + 1)
#define HorzSpace	(CommaWhite + 5)

/******************************************************************/

typedef struct IDDIC
{
	DDIC *ddic;
	int used;
}
IDDIC;

#define NIDD	256

static IDDIC iddic[NIDD];
static DDIC *ddic;
static int TXgetstddic(void);




/******************************************************************/
/*	Takes a buffer and returns a name of a tempfile.  The
 *	tempfile will contain the data.
 */

int
TXftoind(f1)
FLD *f1;
{
	char Fn[] = "toind";
	char *fname = NULL, *buf;
	FILE *fh;
	size_t sz;

	TXgetstddic();
	if (!ddic)
	{
		putmsg(MERR, Fn, "No database currently open");
		return -1;
	}
	fname = TXgetindirectfname(ddic);
	while (fname && TXaccess(fname, F_OK) == 0)
		fname = TXgetindirectfname(ddic);
	if (!fname)
		return -1;
	errno = 0;
	fh = fopen(fname, "wb");
	if (fh == (FILE *) NULL)
	{
		putmsg(MERR + FOE, Fn, "Unable to open indirect file %s: %s",
		       fname, strerror(errno));
		return -1;
	}
	errno = 0;
	if (fwrite(getfld(f1, NULL), 1, f1->size, fh) != f1->size)
	{
		putmsg(MERR + FWE, Fn,
		       "Unable to write %d bytes to indirect file %s: %s",
		       (int) f1->size, fname, strerror(errno));
		fclose(fh);
		unlink(fname);
		return -1;
	}
	fclose(fh);
	f1->type = FTN_CHAR | DDVARBIT;
	f1->elsz = 1;
#if 0
	putfld(f1, fname, strlen(fname));	/* was illegally freed */
#else
	/* wtf no one knows the alloc-or-not magic of shadow/v in fldmath...
	 * this is supposed to make f1 own its data so it's freed properly.
	 * copied from dbtbl.c:fromfile()   KNG 010219
	 * changed to setfld() KNG 20060210
	 */
	if ((buf = TXstrdup(TXPMBUFPN, Fn, fname)) == CHARPN)
	{
		unlink(fname);
		return (-1);
	}
	sz = strlen(buf);
	setfldandsize(f1, buf, sz + 1, FLD_FORCE_NORMAL);		/* so `f1' owns `buf' */
#endif /* !0 */
	return 0;
}


/******************************************************************/

char *
counttostr(co)
ft_counter *co;
{
	static const char	fn[] = "counttostr";
	char rc[TX_COUNTER_HEX_BUFSZ];

	TXprintHexCounter(rc, sizeof(rc), co);
	return(TXstrdup(TXPMBUFPN, fn, rc));
}

/******************************************************************/

int
fochco(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "fochco";
	ft_counter *vp2;
	size_t n1, n2, na;
	int var1;
	char *mem;

	if (op == FOP_CNV)
	{
		return fococh(f2, f1, f3, FOP_ASN);
	}
	if (op != FOP_ASN)
		return FOP_EINVAL;
	if (TXfldIsNull(f2))
		return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */
	TXmakesimfield(f1, f3);
	getfld(f1, &n1);
	vp2 = (ft_counter *) getfld(f2, &n2);
	var1 = fldisvar(f1);
	na = TX_COUNTER_HEX_BUFSZ;
	mem = (ft_char *) TXmalloc(TXPMBUFPN, fn, na);
	if (!mem)
		return FOP_ENOMEM;
	TXprintHexCounter(mem, na, vp2);
	setfld(f3, mem, na);
	if (var1)
		f3->n = f3->size = strlen(mem);
	else
	{
		for (n2 = strlen(mem); n2 < n1; n2++)
			mem[n2] = ' ';
		mem[n2] = '\0';
	}
	return 0;
}

/******************************************************************/

int
fococh(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "fococh";
	ft_char *vp2, *p, *e;
	ft_counter *vp3;
	size_t n2;
	int isdt;

	switch (op)
	{
	case FOP_CNV:
		return fochco(f2, f1, f3, FOP_ASN);
	case FOP_ASN:
		if (TXfldIsNull(f2))
			return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */
		TXmakesimfield(f1, f3);
		vp3 = (ft_counter *) getfld(f3, NULL);
		vp2 = (ft_char *) getfld(f2, &n2);
		/* MAW 09-01-94 - do implicit date parse if not 9-16 hex digits */
		/* MAW/JMT 03-26-99 - adjust for machine size */
		if (n2 == 7)
		{
			if (!strcmpi(vp2, "counter"))
			{
				ft_counter *t;

				TXgetstddic();
				if (!ddic)
				{
					putmsg(MERR, fn,
					       "No database open");
					return -1;
				}
				t = getcounter(ddic);
				memcpy(vp3, t, sizeof(ft_counter));
				t = TXfree(t);
				f3->n = 1;
				return 0;
			}
		}
		if (n2 > ((sizeof(vp3->date) + sizeof(vp3->seq)) * 2)
		    || n2 < 9)
			isdt = 1;
		else
		{
			isdt = 0;
			for (p = vp2, e = vp2 + n2; p < e; p++)
			{
				if (!isxdigit((int) *(unsigned char *) p))
				{
					isdt = 1;
					break;
				}
			}
		}
		if (n2 == 0)	/* JMT 99-02-01 empty string -> 0:0 counter */
		{
			isdt = 0;
			vp3->date = 0;
		}
		vp3->seq = 0;
		if (!isdt || (vp3->date = TXindparsetime(vp2, n2, 2, TXPMBUFPN))
                    == (-1))
		{
			/* `n2' may extend past nul: */
			for (p = vp2, e = vp2 + n2; p < e && *p; p++);
			if (!TXparseHexCounter(vp3, vp2, p))
				return(FOP_EDOMAIN);
		}
		f3->n = 1;
		return 0;
	default:
		return FOP_EINVAL;
	}
}

/******************************************************************/

/******************************************************************/

int
fobyco(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "fobyco";
	ft_counter *vp2;
	size_t n1, n2, na;
	int var1;
	char *mem;

	if (op == FOP_CNV)
	{
		return focoby(f2, f1, f3, FOP_ASN);
	}
	if (op != FOP_ASN)
		return FOP_EINVAL;
	TXmakesimfield(f1, f3);	/* KNG 970904 */
	getfld(f1, &n1);
	vp2 = (ft_counter *) getfld(f2, &n2);
	var1 = fldisvar(f1);
	na = sizeof(vp2->date) + sizeof(vp2->seq) + 1;	/* +1 for '\0' */
	mem = (ft_char *) TXmalloc(TXPMBUFPN, fn, na);
	if (!mem)
		return FOP_ENOMEM;
	memcpy(mem, &vp2->date, sizeof(vp2->date));
	memcpy(mem + sizeof(vp2->date), &vp2->seq, sizeof(vp2->seq));
	mem[na - 1] = '\0';
	setfld(f3, mem, na);
	if (var1)
		f3->n = f3->size = na - 1;
	else
	{
		for (n2 = na - 1; n2 < n1; n2++)
			mem[n2] = ' ';
		mem[n2] = '\0';
	}
	return 0;
}

/******************************************************************/

int
focoby(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_counter *vp3;
	ft_byte *vp2;
	size_t	n2;

	/* int isdt; */

	switch (op)
	{
	case FOP_CNV:
		return fobyco(f2, f1, f3, FOP_ASN);
	case FOP_ASN:
		TXmakesimfield(f1, f3);	/* KNG 970904 */
		vp3 = (ft_counter *) getfld(f3, NULL);
		vp2 = (ft_byte *) getfld(f2, &n2);
		if (n2 != sizeof(vp3->date) + sizeof(vp3->seq))
			return FOP_EINVAL;
		memcpy(&vp3->date, vp2, sizeof(vp3->date));
		memcpy(&vp3->seq, vp2 + sizeof(vp3->date), sizeof(vp3->seq));
		f3->n = 1;
		return 0;
	default:
		return FOP_EINVAL;
	}
}

#define BYTES_PER_LINE	16

int
TXsqlFunc_binToHex(byteFld, modeFld)
FLD	*byteFld;	/* (in/out) FTN_BYTE field to hexify */
FLD	*modeFld;	/* (in, opt.) FTN_CHAR field with mode */
/* SQL function; usage: bintohex(byteData[, 'pretty'])
 * SQL return: hex value of `byteData'.
 */
{
	static CONST char	fn[] = "TXsqlFunc_binToHex";
	static CONST char	hexLower[] = "0123456789abcdef";
	static CONST char	hexUpper[] = "0123456789ABCDEF";
	char			offsetFmt[8] = "";
	CONST char		*hex = hexLower;
	byte			*src, *srcEnd, *sb;
	char			*modeStr, *s, *retData = NULL, *d;
	char			*retDataEnd, *asciiD, *asciiStart;
	size_t			srcLen, n, srcLeft, i, offsetLen = 0;
	int			pretty = 0;
/* # of spaces between hex dump and ASCII dump.  Note that this must be
 * greater than 1 for ASCII-detection algorithm in TXsqlFunc_hexToBin():
 */
#define HEX_ASCII_SPACING	4

	if ((byteFld->type & DDTYPEBITS) != FTN_BYTE) return(FOP_EINVAL);

	src = (byte *)getfld(byteFld, &srcLen);
	if (!src
#ifdef EPI_OS_SIZE_T_IS_SIGNED
	    || srcLen < (size_t)0
#endif /* EPI_OS_SIZE_T_IS_SIGNED */
	    ) srcLen = 0;
	srcEnd = src + srcLen;

	/* Get optional mode/flags: */
	if (modeFld &&
	    (modeFld->type & DDTYPEBITS) == FTN_CHAR &&
	    (modeStr = (char *)getfld(modeFld, SIZE_TPN)) != CHARPN &&
	    *modeStr != '\0')
	{
		for (s = modeStr; *s; s += n)
		{
			s += strspn(s, CommaWhite);
			n = strcspn(s, CommaWhite);
			if (n == 6 && strnicmp(s, "pretty", n) == 0)
			{
				pretty = 1;
				hex = hexUpper;
			}
			else if (n == 6 && strnicmp(s, "stream", n) == 0)
			{
				pretty = 0;
				hex = hexLower;
			}
			else
				putmsg(MWARN + UGE, fn,
				       "Unknown bintohex() flag `%.*s'",
				       (int)n, s);
		}
	}

	if (pretty)
	{
		/* Determine format spec for offset: to keep columns
		 * aligned, we want the spec not to "overflow", but
		 * also want to minimize the number of leading zeroes:
		 */
		for (offsetLen = 4;		/* minimum 4 digits */
		     offsetLen < EPI_OS_SIZE_T_BITS/4 &&
			     (srcLen & (((size_t)~0) << (4*offsetLen)));
		     offsetLen += 2)		/* even # of digits */
			;
		htsnpf(offsetFmt, sizeof(offsetFmt), "%%0%dX: ",
		       (int)offsetLen);
		offsetLen += 2;			/* +2 for `: ' at fmt end */

		n = ((srcLen + BYTES_PER_LINE - 1)/BYTES_PER_LINE)* /*#lines*/
			(offsetLen + 3*BYTES_PER_LINE + HEX_ASCII_SPACING +
			 BYTES_PER_LINE + TX_OS_EOL_STR_LEN);
		retData = (char *)TXmalloc(TXPMBUFPN, fn, n + 1);
		if (!retData) return(FOP_ENOMEM);
		retDataEnd = retData + n;
		srcLeft = srcLen;
		sb = src;
		d = retData;
		while (srcLeft > (size_t)0)
		{
			d += htsnpf(d, retDataEnd - d, offsetFmt,
				    (unsigned)(sb - src));
			if (d >= retDataEnd) break;    /* should not happen */
			asciiD = asciiStart = d + 3*BYTES_PER_LINE +
				HEX_ASCII_SPACING;
			n = TX_MIN(srcLeft, BYTES_PER_LINE);
			for (i = 0; i < n; i++, sb++)
			{			/* each byte for this line */
				*(d++) = ' ';
				*(d++) = hex[(*sb & 0xf0) >> 4];
				*(d++) = hex[*sb & 0xf];
				*(asciiD++) = (*sb >= (byte)' ' &&
					       *sb <= (byte)'~' ? *sb : '.');
			}
			srcLeft -= n;
			for ( ; d < asciiStart; d++) *d = ' ';
			d = asciiD;
			strcpy(d, TX_OS_EOL_STR);
			d += TX_OS_EOL_STR_LEN;
		}
		*d = '\0';
	}
	else					/* stream */
	{
		retData = (char *)TXmalloc(TXPMBUFPN, fn, 2*srcLen + 1);
		if (!retData) return(FOP_ENOMEM);
		for (sb = src, d = retData; sb < srcEnd; sb++)
		{
			*(d++) = hex[(*sb & 0xf0) >> 4];
			*(d++) = hex[*sb & 0xf];
		}
		*d = '\0';
	}

	/* Set return value in first field, i.e. `byteFld': */
	TXfreefldshadow(byteFld);
	byteFld->type = (DDVARBIT | FTN_CHAR);
	byteFld->elsz = sizeof(ft_char);
	setfldandsize(byteFld, retData, (d - retData) + 1, FLD_FORCE_NORMAL);

	return 0;				/* success */
}

/* ------------------------------------------------------------------------ */

int
TXsqlFunc_hexToBin(hexFld, modeFld)
FLD	*hexFld;		/* (in/out) FTN_BYTE field to hexify */
FLD	*modeFld;		/* (in, opt.) FTN_CHAR field with mode */
/* SQL function; usage: hextobin(hexData, 'pretty'])
 * SQL return: binary value of `hexData'.
 */
{
	static CONST char	fn[] = "TXsqlFunc_hexToBin";
	char			*src, *srcEnd;
	char			*modeStr, *s, *e, *tmpSrcEnd;
	size_t			srcLen, n;
	int			pretty = 0, ret;
	HTBUF			*buf = NULL;
	byte			curVal;
#define IS_HEX_DIGIT(ch)						\
(((ch) >= '0' && (ch) <= '9') || ((ch) >= 'a' && (ch) <= 'f') ||	\
 ((ch) >= 'A' && (ch) <= 'F'))
#define UNHEX(ch)	((ch) >= '0' && (ch) <= '9' ? (ch) - '0' :	\
 ((ch) >= 'a' && (ch) <= 'f' ? 10 + ((ch) - 'a') :			\
 ((ch) >= 'A' && (ch) <= 'F' ? 10 + ((ch) - 'A') : 0)))

	if (TXfldbasetype(hexFld) != FTN_CHAR)
	{
		ret = FOP_EINVAL;
		goto done;
	}

	src = (char *)getfld(hexFld, &srcLen);
	if (!src || TX_SIZE_T_VALUE_LESS_THAN_ZERO(srcLen)) srcLen = 0;
	srcEnd = src + srcLen;

	/* Get optional mode/flags: */
	if (modeFld &&
	    TXfldbasetype(modeFld) == FTN_CHAR &&
	    (modeStr = (char *)getfld(modeFld, SIZE_TPN)) != CHARPN &&
	    *modeStr != '\0')
	{
		for (s = modeStr; *s; s += n)
		{
			s += strspn(s, CommaWhite);
			n = strcspn(s, CommaWhite);
			if (n == 6 && strnicmp(s, "pretty", n) == 0)
				pretty = 1;
			else if (n == 6 && strnicmp(s, "stream", n) == 0)
				pretty = 0;
			else
				putmsg(MWARN + UGE, fn,
				       "Unknown hextobin() flag `%.*s'",
				       (int)n, s);
		}
	}

	if (!(buf = openhtbuf())) goto noMem;
	if (pretty)
	{
		/* Handle either pretty or stream format: */
		for (s = src; s < srcEnd; )
		{				/* for each non-empty line */
			s += strspn(s, WhiteSpace);

			/* Skip leading offset, if present: */
			tmpSrcEnd = s + 20;	/* do not waste time */
			tmpSrcEnd = TX_MIN(srcEnd, tmpSrcEnd);
			for (e = s; e < tmpSrcEnd && IS_HEX_DIGIT(*e); e++);
			e += strspn(s, HorzSpace);
			if (e < srcEnd && *e == ':') s = e + 1; /*skip offset*/

			/* Parse hex bytes, but stop before right-side ASCII.
			 * Since we cannot know if there are BYTES_PER_LINE
			 * hex bytes on this line, scan hex bytes until
			 * parse fails, or more than one space (in case
			 * ASCII dump is itself hex):
			 */
			for (s += strspn(s, HorzSpace); s < srcEnd; s += 2)
			{
				if (*s == ' ' || *s == '\t') s++;
				if (s + 2 > srcEnd ||
				    !IS_HEX_DIGIT(*s) ||
				    !IS_HEX_DIGIT(s[1]))
					break;
				curVal = ((UNHEX(*s) << 4) | UNHEX(s[1]));
				if (!htbuf_write(buf, (char *)&curVal, 1))
					goto noMem;
			}

			/* Skip potential ASCII to end of line: */
			for ( ; s < srcEnd && *s != '\r' && *s != '\n'; s++);
			/* EOL skipped at top of loop above */
		}
	}
	else					/* stream format only */
	{
		for (s = src; s < srcEnd; s += 2)
		{				/* for each byte parsed */
			/* Be somewhat strict in what we expect, so we
			 * can fail on parse errors instead of silently
			 * returning potentially truncated/bad result:
			 */
			s += strspn(s, WhiteSpace);
			if (s >= srcEnd) break;
			if (s + 2 > srcEnd ||
			    !IS_HEX_DIGIT(*s) ||
			    !IS_HEX_DIGIT(s[1]))
			{
				putmsg(MERR + UGE, fn,
				    "Invalid hex byte at source offset 0x%wx",
				       (EPI_HUGEINT)(s - src));
				ret = FOP_EINVAL;
				goto done;
			}
			curVal = ((UNHEX(s[0]) << 4) | UNHEX(s[1]));
			if (!htbuf_write(buf, (char *)&curVal, 1))
			{
			noMem:
				ret = FOP_ENOMEM;
				goto done;
			}
		}
	}

	/* Set return value in first arg, i.e. `hexFld': */
	TXfreefldshadow(hexFld);
	hexFld->type = (DDVARBIT | FTN_BYTE);
	hexFld->elsz = sizeof(ft_byte);
	n = htbuf_getdata(buf, &s, 0x3);
	if (!s)					/* do not return NULL */
	{
		n = 0;
		s = TXstrdup(TXPMBUFPN, fn, "");
	}
	setfldandsize(hexFld, s, n + 1, FLD_FORCE_NORMAL);
	ret = 0;				/* success */

done:
	buf = closehtbuf(buf);
	return(ret);
#undef IS_HEX_DIGIT
}

/******************************************************************/

static void
bin2hex(unsigned char *bin, int nbytes, char *hexstr)
{
	int idx;
	static CONST char trans[] = "0123456789abcdef";
	int hexindex = 0;

	for (idx = 0; idx < nbytes; idx++)
	{
		hexstr[hexindex++] = trans[(bin[idx] & 0xF0) >> 4];
		hexstr[hexindex++] = trans[bin[idx] & 0x0F];
	}
	hexstr[hexindex] = '\0';
}


#define hex2byte(hexchar) ((hexchar) >= 'a' ? 10 + ((hexchar) - 'a') : \
                                ((hexchar) >= 'A' ? 10 + ((hexchar) - 'A') : \
                                 (hexchar) - '0'))

static size_t
hex2bin(char *hexstr, unsigned char *bin, int maxlen, int *success)
/* Converts hexadecimal string `hexstr' to binary string `bin'
 * (allocated size `maxlen').  Sets `*success' to 1 on success, 0 on error
 * (bad hex characters).
 * Returns length of `bin' written to (not nul-terminated).
 */
{
	int idx = 0;
	int binindex = 0;

	for (; hexstr[idx] && binindex < maxlen; idx += 2)
	{
		while (hexstr[idx] &&
		       isspace((int) ((unsigned char *) hexstr)[idx]))
			idx++;
		if (!isxdigit((int) ((unsigned char *) hexstr)[idx]) ||
		    !hexstr[idx + 1] ||
		    !isxdigit((int) ((unsigned char *) hexstr)[idx + 1]))
		{
			/* Not a proper hex character; error: */
			if (success)
				*success = 0;
			return binindex;
		}
		bin[binindex] = hex2byte(hexstr[idx]) << 4;
		bin[binindex++] |= hex2byte(hexstr[idx + 1]);
	}
	if (success)
		*success = 1;
	return binindex;
}

/******************************************************************/

int
fochby(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "fochby";
	byte *vp2;
	size_t n1, n2, na;
	int var1;
	char *mem;

	if (op == FOP_CNV)
	{
		return fobych(f2, f1, f3, FOP_ASN);
	}

	if (op != FOP_ASN)
		return FOP_EINVAL;
	if (TXfldIsNull(f2))
		return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */
	TXmakesimfield(f1, f3);
	getfld(f1, &n1);
	vp2 = (byte *) getfld(f2, &n2);
	var1 = fldisvar(f1);
	na = (TXApp->hexifyBytes ? 2 : 1)*n2 + 1;
	if (!var1)
	{
		if (n1 + 1 < na)
			return FOP_ENOMEM;
		na = n1 + 1;
	}
	mem = (ft_char *) TXmalloc(TXPMBUFPN, fn, na);
	if (!mem)
		return FOP_ENOMEM;
	if (TXApp->hexifyBytes)
	{
		char *mp;
		mem[0] = '\0';
		bin2hex(vp2, n2, mem);
		mp = mem + (2 * n2);	/* 2 characters per byte */
		setfld(f3, mem, na);
		if (var1)
			f3->n = f3->size = mp - mem;
		else
		{
			for (n2 = mp - mem; n2 < n1; n2++)
				mem[n2] = '\0';
		}
	}
	else					/* byte -> char as-is */
	{
		memcpy(mem, vp2, TX_MIN(n2, na - 1));
		mem[TX_MIN(n2, na - 1)] = '\0';
		setfldandsize(f3, mem, na, FLD_FORCE_NORMAL);
	}
	return 0;
}

/******************************************************************/

int
fobych(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static CONST char	fn[] = "fobych";
	ft_byte	*x, *p;
	ft_char *vp2;
	size_t n1, n2, xc;
	int na;
	int var1;

	switch (op)
	{
	case FOP_CNV:
		return fochby(f2, f1, f3, FOP_ASN);
	case FOP_ASN:
		if (TXfldIsNull(f2))
			return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */
		TXmakesimfield(f1, f3);
		var1 = fldisvar(f1);
		getfld(f1, &n1);
		vp2 = (ft_char *) getfld(f2, &n2);
		if (TXApp->hexifyBytes)
		{
			int success;
		/* Alloc enough for hexadecimal-string to byte conversion: */
			na = (n2 + 3) / 2;
			p = x = (ft_byte *) TXmalloc(TXPMBUFPN, fn, na);
			if (!p)
				return FOP_ENOMEM;
			xc = hex2bin(vp2, x, na - 1, &success);	/* -1: nul */
			x[xc] = '\0';
			if (!success)
			{
			/* Hex-to-byte conversion failed.  Just copy string:*/
				p = x = TXfree(p);
				xc = n2;
				na = n2 + 1;	/* +1 for nul */
				x = p = (ft_byte *)TXmalloc(TXPMBUFPN, fn, na);
				if (!p)
					return FOP_ENOMEM;
				memcpy(x, vp2, n2);
				x[n2] = '\0';
			}
		}
		else				/* char -> byte as-is */
		{
			na = n2 + 1;		/* +1 for nul */
			xc = n2;
			x = p = (ft_byte *)TXmalloc(TXPMBUFPN, fn, na);
			if (!p) return(FOP_ENOMEM);
			memcpy(x, vp2, n2);
			x[n2] = '\0';
		}
		if (var1)
		{
			setfldandsize(f3, p, xc + 1, FLD_FORCE_NORMAL);
		}
		else
		{
			if (xc > n1)		/* too big to fit in `f1' */
			{
				p = x = TXfree(p);
				return FOP_ENOMEM;
			}
			x = (ft_byte *) TXcalloc(TXPMBUFPN, fn, 1, n1);
			if (!x)
				return FOP_ENOMEM;
			memcpy(x, p, xc);
			p = TXfree(p);
			setfld(f3, x, n1);
		}
		return 0;
	default:
		return FOP_EINVAL;
	}
}

/******************************************************************/

int
fococo(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_counter *vp1, *vp2, *vp3;
	size_t n1, n2, i;
	int var1, r;
	int rc = 0, found;

	if (op == FOP_CNV)
		TXmakesimfield(f2, f3);	/* KNG 970904 */
	else if (!(op & FOP_CMP))
		TXmakesimfield(f1, f3);
	vp1 = (ft_counter *) getfld(f1, &n1);
	vp2 = (ft_counter *) getfld(f2, &n2);
	vp3 = (ft_counter *) getfld(f3, NULL);
	var1 = fldisvar(f1);
	if (n1 > 1 || var1)
	{
		switch (op)
		{
		case FOP_ADD:
		case FOP_CNV:
		case FOP_ASN:
		default:
			rc = FOP_EINVAL;
		}
	}
	else
	{
		switch (op)
		{
		case FOP_ADD:
		case FOP_MUL:
		case FOP_DIV:
#ifndef fonomodulo
		case FOP_MOD:
#endif
			rc = FOP_EINVAL;
			break;
		case FOP_SUB:
			vp3->date = vp1->date - vp2->date;
			if (vp3->date == (TXft_counterDate)0)
				vp3->seq = vp1->seq - vp2->seq;
			else
				vp3->seq = (TXft_counterSeq)0;
			break;
		case FOP_CNV:
			*vp3 = *vp1;
			break;
		case FOP_ASN:
			*vp3 = *vp2;
			break;
		case FOP_EQ:
			rc = fld2finv(f3, (vp1->date == vp2->date) &&
				      (vp1->seq == vp2->seq));
			break;
		case FOP_IN:
		case FOP_IS_SUBSET:
		case FOP_INTERSECT_IS_EMPTY:
		case FOP_INTERSECT_IS_NOT_EMPTY:
		/* WTF support multi/var `f1' and subset/intersect
		 * properly; for now `f1' is single-item fixed
		 * (checked above), thus intersect and subset behave
		 * same:
		 */
			found = 0;
			for (i = 0; i < n2; i++)
			{
				if ((vp1->date == vp2[i].date) &&
				    (vp1->seq == vp2[i].seq))
				{
					rc = fld2finv(f3,
					       (op != FOP_INTERSECT_IS_EMPTY));
					found++;
					break;
				}
			}
			if (!found)
				rc = fld2finv(f3,(op==FOP_INTERSECT_IS_EMPTY));
			break;
		case FOP_INTERSECT:
			/* INTERSECT returns set not boolean; wtf support */
			rc = FOP_EILLEGAL;
			break;
		case FOP_NEQ:
			rc = fld2finv(f3, (vp1->date != vp2->date) ||
				      (vp1->seq != vp2->seq));
			break;
		case FOP_LT:
			CTRCMP(vp1, vp2, r);
			rc = fld2finv(f3, (r < 0));
			break;
		case FOP_LTE:
			CTRCMP(vp1, vp2, r);
			rc = fld2finv(f3, (r <= 0));
			break;
		case FOP_GT:
			CTRCMP(vp1, vp2, r);
			rc = fld2finv(f3, (r > 0));
			break;
		case FOP_GTE:
			CTRCMP(vp1, vp2, r);
			rc = fld2finv(f3, (r >= 0));
			break;
		case FOP_COM:
			CTRCMP(vp1, vp2, rc);
			rc = fld2finv(f3, rc);
			break;

		default:
			rc = FOP_EINVAL;
		}
	}
	return (rc);
}				/* end fococo() */

/**********************************************************************/

char *TXgetStrlst ARGS((FLD *f, ft_strlst *hdr));
char *
TXgetStrlst(f, hdr)
FLD		*f;
ft_strlst	*hdr;	/* (out) a proper ft_strlst struct */
/* Copies and fixes strlst header from `*f' into `*hdr', returning
 * pointer to ft_strlst.buf data.  Note that return value
 * (ft_strlst.buf pointer) might be inside `f' data, or inside `*hdr'
 * (e.g. if `*f' strlst header is truncated).  Thus upon return,
 * `hdr->buf' should not be used, though all other `*hdr' fields
 * should be.  Will never return NULL, even on error (err return is
 * proper empty strlst).  Yaps on error.
 */
{
	static CONST char	Fn[] = "TXgetStrlst";
	ft_strlst		*v;
	size_t			n;

	v = (ft_strlst *)getfld(f, &n);

	if ((f->type & DDTYPEBITS) != FTN_STRLST)
	{					/*caller should have checked*/
		putmsg(MERR + UGE, Fn, "Non-strlst field");
		goto empty;
	}

	/* Be generous; only require TX_STRLST_MINSZ, not sizeof(ft_strlst):*/
	if (f->size < (size_t)TX_STRLST_MINSZ)	/* truncated */
	{
		putmsg(MERR + MAE, Fn, "Truncated strlst header");
		goto empty;
	}

	if (v == (ft_strlst *)NULL)
	{
		putmsg(MERR + MAE, Fn, "NULL strlst field");
	empty:
		memset(hdr, 0, sizeof(ft_strlst));
		return(hdr->buf);
	}
	if (f->size >= sizeof(ft_strlst))
		*hdr = *v;			/* faster than memcpy */
	else
		memcpy(hdr, v, TX_STRLST_MINSZ);
	if ((size_t)TX_STRLST_MINSZ + hdr->nb < (size_t)TX_STRLST_MINSZ)
	{
		putmsg(MERR + MAE, Fn,
			"Negative/overflow strlst.nb value (%wd)",
			(EPI_HUGEINT)hdr->nb);
		hdr->nb = 0;
	}
	if (((size_t)TX_STRLST_MINSZ + hdr->nb) - (size_t)1 > f->size)
	{
		putmsg(MERR + MAE, Fn,
		    "Truncated strlst data (strlst.nb = %wd fld.size = %wd)",
			(EPI_HUGEINT)hdr->nb, (EPI_HUGEINT)f->size);
		hdr->nb = (f->size - TX_STRLST_MINSZ) + 1;
	}
	return(v->buf);
}

/**********************************************************************/

size_t
TXgetStrlstLength(list, strData)
const ft_strlst *list;
const char      *strData;       /* (in, opt.) ft_strlst.buf override */
/* Counts and returns the number of strings in a strlst.
 * `strData' overrides `list->buf', e.g. TXgetStrlst() return.
 */
{
  size_t        strCount = 0;
  CONST char    *s, *strEnd;

  if (!strData) strData = list->buf;
  strEnd = strData + list->nb;
  if (strEnd > strData && strEnd[-1] == '\0')
    strEnd--;                                   /* ignore list terminator */

  /* Count each string, but do not walk past end of `strData', in case
   * last string is unterminated:
   */
  for (s = strData; s < strEnd; s++)
    if (*s == '\0') strCount++;

  /* Check for unterminated last string, and count it: */
  if (s > strData && s[-1] != '\0') strCount++;

  return(strCount);
}

/**********************************************************************/


int
foslsl(f1, f2, f3, op)		/* variable or n>1 not supported */
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "foslsl";
	ft_strlst	*vp2, *vp3, sl1, sl2;
	size_t		n2, n3;
	FLD *ft;
	int rc = 0;
	char		*buf1, *buf2, *s1, *s2, *e1, *e2, *end1, *end2;
	char		*bufUnused2;
#define MAXTMP	1024
	byte		elIsUsedTmp2[MAXTMP], *elIsUsed2 = elIsUsedTmp2;
	size_t		elIdx2, elIdxUnused2;
	CONST char	*p1, *p2;
	HTBUF		*resBuf = NULL;

#ifdef DEBUG
	DBGMSG(9, (999, (char *) NULL, "foslsl(,,%d)", op));
#endif
	switch (op)
	{
	case FOP_CNV:
		ft = f1;
		f1 = f2;
		f2 = ft;
		goto asn;			/* avoid gcc warning */
	case FOP_ASN:
	asn:
		TXmakesimfield(f1, f3);
		vp2 = (ft_strlst *) getfld(f2, &n2);
		vp3 = (ft_strlst *) TXmalloc(TXPMBUFPN, fn, n2);
		memcpy(vp3, vp2, n2);
		setfld(f3, vp3, n2);
		f3->n = f2->n;
		f3->size = f2->size;
		break;
	case FOP_NEQ:		/* nobreak; */
	case FOP_EQ:
		buf1 = TXgetStrlst(f1, &sl1);
		buf2 = TXgetStrlst(f2, &sl2);
		/* Ignore strlst-term. nuls: */
		end1 = buf1 + sl1.nb;
		if (end1 > buf1 && !end1[-1]) end1--;
		end2 = buf2 + sl2.nb;
		if (end2 > buf2 && !end2[-1]) end2--;
		if (TXApp->useStringcomparemodeForStrlst)	/* Bug 3677 */
		{
			TXget_globalcp();
			/* Item nuls count; TXstringcompare() stops at nul: */
			p1 = (CONST char *)buf1;
			p2 = (CONST char *)buf2;
			rc = (TXunicodeStrFoldCmp(&p1, end1 - buf1, &p2,
						  end2 - buf2,
					globalcp->stringcomparemode) == 0);
		}
		else
		{
			if (sl1.nb != sl2.nb)
				rc = 0;
			else
				rc = (memcmp(buf1, buf2, sl1.nb) == 0);
		}
		if (op == FOP_NEQ) rc = !rc;
		rc = fld2finv(f3, rc);
		break;
	case FOP_COM:
	case FOP_LT:
	case FOP_LTE:
	case FOP_GT:
	case FOP_GTE:
		buf1 = TXgetStrlst(f1, &sl1);
		buf2 = TXgetStrlst(f2, &sl2);
		/* Ignore strlst-term. nuls: */
		end1 = buf1 + sl1.nb;
		if (end1 > buf1 && !end1[-1]) end1--;
		end2 = buf2 + sl2.nb;
		if (end2 > buf2 && !end2[-1]) end2--;
		if (TXApp->useStringcomparemodeForStrlst)	/* Bug 3677 */
		{
			TXget_globalcp();
			/* Item nuls count; TXstringcompare() stops at nul: */
			p1 = (CONST char *)buf1;
			p2 = (CONST char *)buf2;
			rc = TXunicodeStrFoldCmp(&p1, end1 - buf1, &p2,
						 end2 - buf2,
					globalcp->stringcomparemode);
		}
		else
		{
			if (sl1.nb <= sl2.nb)
				rc = memcmp(buf1, buf2, sl1.nb);
			else
				rc = memcmp(buf1, buf2, sl2.nb);
			if (rc == 0)
				rc = sl1.nb - sl2.nb;
		}
		switch (op)
		{
		case FOP_LT:	rc = (rc < 0);	break;
		case FOP_LTE:	rc = (rc <= 0);	break;
		case FOP_GT:	rc = (rc > 0);	break;
		case FOP_GTE:	rc = (rc >= 0);	break;
		}
		rc = fld2finv(f3, rc);
		break;
	case FOP_IN:
	case FOP_IS_SUBSET:
	case FOP_INTERSECT:
	case FOP_INTERSECT_IS_EMPTY:
	case FOP_INTERSECT_IS_NOT_EMPTY:
		/* Bug 2399:  True if any string of `f1' is also in `f2': */
		/* Bug 3677:  only if inmode='intersect': */
		/* Bug 4065:    or INTERSECT_IS_NOT_EMPTY: */
		buf1 = TXgetStrlst(f1, &sl1);
		buf2 = TXgetStrlst(f2, &sl2);
		end1 = buf1 + sl1.nb;
		if (end1 > buf1 && !end1[-1]) end1--;	/* ign strlst-term. */
		end2 = buf2 + sl2.nb;
		if (end2 > buf2 && !end2[-1]) end2--;	/* ign strlst-term. */
		if (op == FOP_IS_SUBSET ||
		    (op == FOP_IN && TXApp->inModeIsSubset) ||
		    /* Even though it is using intersection, INTERSECT
		     * actually generates a set, so must iterate all `f1':
		     */
		    op == FOP_INTERSECT)
		{
		    /* Every member of `f1' must be in `f2'. */
		    if (op == FOP_INTERSECT)
		    {
			/* Open and prep output buffer: */
			if (!(resBuf = openhtbuf()) ||
			    !TXstrlstBufBegin(resBuf))
				goto noMem;
		    }
		    else			/* subset */
			    /* Empty set is subset of anything: */
			    if (end1 <= buf1) goto isTrue;

		    /* Optimization: if LHS is empty, it is a subset of
		     * anything (and intersection is empty):
		     */
		    if (end1 <= buf1) goto finishSubOrIntersect;

		    /* `f1' dups must map distinctly (injection),
		     * so track `f2' element usage with `elIsUsed2'.
		     * Max number of elements in `f2' would be `sl2nb'
		     * if they were all empty strings, so `sl2.nb' is
		     * an upper limit:
		     */
		    if (sl2.nb > MAXTMP &&
		        !(elIsUsed2 = (byte *)TXmalloc(TXPMBUFPN, fn, sl2.nb)))
			goto noMem;
		    memset(elIsUsed2, 0, sl2.nb);
		    bufUnused2 = buf2;
		    elIdxUnused2 = 0;

		    for (s1 = buf1; s1 < end1; s1 = e1 + 1)
		    {				/* for each `f1' element */
			/* Get next `f1' string: */
			for (e1 = s1; e1 < end1 && *e1 != '\0'; e1++);
			/* Iterate over every `f2' string looking for match:*/
			for (s2 = bufUnused2, elIdx2 = elIdxUnused2;
			     s2 < end2;
			     s2 = e2 + 1, elIdx2++)
			{
				/* Get next `f2' string: */
				for (e2 = s2; e2 < end2 && *e2 != '\0'; e2++);
				/* Compare: */
				if (!elIsUsed2[elIdx2] &&
				    TXstringcompare(s1, s2, e1-s1,e2-s2) == 0)
				{		/* got a match for `f1' el */
					elIsUsed2[elIdx2] = 1;
					/* Optimization: if using the first
					 * unused `f2' element, "remove" it
					 * to save scan time later:
					 */
					if (s2 == bufUnused2)
					{
						bufUnused2 = e2 + 1;
						elIdxUnused2 = elIdx2 + 1;
					}
					break;
				}
			}
			if (s2 < end2)		/* match found in `f2' */
			{
				/* Add item from `f1', not `f2': may be
				 * different even though they string-compare
				 * equal (e.g. ignore-case), we favor LHS:
				 */
				if (op == FOP_INTERSECT &&
				    !TXstrlstBufAddString(resBuf, s1, e1 - s1))
				{
				noMem:
					rc = FOP_ENOMEM;
					goto done;
				}
			}
			else			/* no match found in `f2' */
			{
				if (op == FOP_INTERSECT) continue;
				rc = fld2finv(f3, 0);	/* false */
				goto done;
			}
		    }
		finishSubOrIntersect:
		    if (op == FOP_INTERSECT)
		    {
			TXmakesimfield(f1, f3);
			if (!TXstrlstBufEnd(resBuf)) goto noMem;
			n3 = htbuf_getdata(resBuf, (char **)(void *)&vp3, 0x3);
			setfldandsize(f3, vp3, n3 + 1, FLD_FORCE_NORMAL);
			rc = 0;			/* success */
		    }
		    else
		    {
		    isTrue:
			rc = fld2finv(f3, 1);	/* `f3' is true */
		    }
		}
		else				/* intersect-boolean */
		{
		    /* Optimization: if RHS is empty set, intersection is
		     * empty set too:
		     */
		    if (end2 <= buf2) goto finishIntersectBoolean;

		    for (s1 = buf1; s1 < end1; s1 = e1 + 1)
		    {
			/* Get next `f1' string: */
			for (e1 = s1; e1 < end1 && *e1 != '\0'; e1++);
			/* Iterate over every `f2' string looking for match:*/
			for (s2 = buf2; s2 < end2; s2 = e2 + 1)
			{
				/* Get next `f2' string: */
				for (e2 = s2; e2 < end2 && *e2 != '\0'; e2++);
				/* Compare: */
				if (e1 - s1 == e2 - s2 &&
				    TXstringcompare(s1, s2, e1-s1,e2-s2) == 0)
				{
					rc = fld2finv(f3,
					      (op != FOP_INTERSECT_IS_EMPTY));
					goto done;
				}
			}
		    }
		finishIntersectBoolean:
		    rc = fld2finv(f3, (op == FOP_INTERSECT_IS_EMPTY));
		}
		break;
	case FOP_MAT:
		/* Bug 3677: do not support multi-item RHS of MATCHES until
		 * properly supported; see also TXprepMatchesExpression():
		 */
		rc = FOP_EILLEGAL;	/* should be putmsg'd by foop[2]()? */
		break;
	default:
		rc = FOP_EINVAL;
		break;
	}
done:
	if (elIsUsed2 && elIsUsed2 != elIsUsedTmp2)
		elIsUsed2 = TXfree(elIsUsed2);
	if (resBuf) resBuf = closehtbuf(resBuf);
	return(rc);
#undef MAXTMP
}				/* end foslsl() */

/**********************************************************************/
typedef enum STRARRAY_STATE
{
  STRARRAY_START,
  STRARRAY_PRE_STRING,
  STRARRAY_IN_STRING,
  STRARRAY_IN_STRING_ESC,
  STRARRAY_POST_STRING,
  STRARRAY_END
} STRARRAY_STATE;

#define COPYCHAR if(result){*destinationbuffer++=*current;byteUsed[(int)*current]=1;}
static int
char2StrlstArrayFormat(char *input, size_t len, ft_strlst **result, char2strlstmode c2slmode)
{
  char *current, *destinationbuffer = NULL;
  byte byteUsed[256];
  ft_strlst *result_p = NULL;
  STRARRAY_STATE state;
  int rc = 0, i;
	char quotechar = '"';

	if(!(c2slmode & TXc2s_json_array))
	{
		if(result) *result = NULL;
		goto done;
	}
  if(!len)
  {
     len = strlen(input);
  }
  if(len < 2)
  {
    goto done;
  }
  if(input[0] != '[' || input[len-1] != ']')
  {
    goto done;
  }
  if(result)
  {
    result_p = *result = TXcalloc(NULL, NULL, sizeof(ft_strlst) + len + 2, 1);
    if(!result_p)
    {
      rc = FOP_ENOMEM;
      goto cleanup;
    }
    destinationbuffer=result_p->buf;
    memset(byteUsed, 0, sizeof(byteUsed));
  }
  for (current = input, state = STRARRAY_START; *current; current++)
  {
      switch(state)
      {
          case STRARRAY_START:
            if(*current == '[')
            {
              state = STRARRAY_PRE_STRING;
              break;
            } else {
              goto cleanup;
            }
          case STRARRAY_PRE_STRING:
						if(*current == '"')
						{
							state = STRARRAY_IN_STRING;
							quotechar = *current;
							break;
						}
            if(*current == '\'' && (c2slmode & TXc2s_json_lax))
            {
              state = STRARRAY_IN_STRING;
							quotechar = *current;
              break;
            }
            if(isspace(*current))
              break;
            goto cleanup;
          case STRARRAY_IN_STRING:
            if(*current == quotechar)
            {
              state = STRARRAY_POST_STRING;
              if(result){*destinationbuffer++ = '\0';}
              break;
            }
            if(*current == '\\')
            {
              state = STRARRAY_IN_STRING_ESC;
              break;
            }
            COPYCHAR
            break;
          case STRARRAY_IN_STRING_ESC:
            state = STRARRAY_IN_STRING;
            COPYCHAR
            break;
          case STRARRAY_POST_STRING:
            if(*current == ',')
            {
              state = STRARRAY_PRE_STRING;
              break;
            }
            if(isspace(*current))
            {
              break;
            }
            if(*current == ']')
            {
              state = STRARRAY_END;
              break;
            }
            goto cleanup;
          case STRARRAY_END:
            if(isspace(*current))
            {
              break;
            }
            goto cleanup;
      }
  }
  if(state == STRARRAY_END)
  {
    if(result_p && destinationbuffer)
    {
      *destinationbuffer++ = '\0';
      for (i = 0; i < 256; i++)
        if (!byteUsed[(byte)TxPrefStrlstDelims[i]])
        {
          result_p->delim = TxPrefStrlstDelims[i];
          result_p->nb = destinationbuffer - result_p->buf;
          break;
        }
    }
    return 1;
  }
cleanup:
  if(result && result_p)
  {
    *result = TXfree(result_p);
  }
done:
  return rc;
}

int
foslch(f1, f2, f3, op)		/* variable sl or n>1 not supported */
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static CONST char	fn[] = "foslch";
	ft_strlst *vp1, sl1;
	ft_strlst *vp3 = NULL;
	ft_char *vp2, *p, *e, dc;
	size_t n1, n2, n3, na = 0, i;
	char *lstbuf, *b, *cs, *cse;
	int rc = 0;
	char2strlstmode c2slmode = TXApp->charStrlstConfig.toStrlst;
	byte	byteUsed[256];
	TXPMBUF	*pmbuf = TXPMBUFPN;
	CONST char	*p1, *p2;
	HTBUF	*resBuf = NULL;

#ifdef DEBUG
	DBGMSG(9, (999, (char *) NULL, "foslch(,,%d)", op));
#endif
	switch (op)
	{
	case FOP_CNV:
		rc = fochsl(f2, f1, f3, FOP_ASN);
		break;
	case FOP_ASN:
		if (TXfldIsNull(f2))
			return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */
		TXmakesimfield(f1, f3);
		/* WTF f1's ft_strlst struct may be short, from a FOP_CNV? */
		vp2 = (ft_char *) getfld(f2, &n2);
		if(c2slmode & TXc2s_json_array)
		{
			char2StrlstArrayFormat(vp2, n2, &vp3, c2slmode);
			if(vp3)
			{
				na = sizeof(ft_strlst) + vp3->nb;
			}
		}
		if(!vp3)
		{
			vp3 = (ft_strlst *) getfld(f3, NULL);

			/* `n2' will include space for all payload strings' nuls,
			 * except for last one iff not TXVSSEP_LASTCHAR (Bug 3682;
			 * fix below).  Add 1 for list terminator, plus 1 for
			 * fldmath terminator (setfldandsize() removes):
			 */
			na = TX_STRLST_MINSZ + n2 + 2;
			rc = 0;
			/* Get delimiter char: */
			byteUsed[0] = 0;		/* stop compiler warning */
			if(c2slmode & TXc2s_trailing_delimiter)
			{
				dc = (n2 > 0 ? vp2[n2 - 1] : '\0');
			}
			else
			{
				if(c2slmode & TXc2s_defined_delimiter)
				{
					na++;			/* Bug 3682; last str nul */
					dc = TXApp->charStrlstConfig.delimiter;
				}
				else
				{
					na++;			/* Bug 3682; last str nul */
					memset(byteUsed, 0, sizeof(byteUsed));
					dc = '\0';		/* least-preferred sep */
				}
			}
			/* Make sure `na' is at least enough for a full struct,
			 * for those that assume so (+1 for fldmath nul):
			 */
			if (na < sizeof(ft_strlst) + 1) na = sizeof(ft_strlst) + 1;
			/* Alloc `lstbuf', now that we have correct `na': */
			lstbuf = TXcalloc(pmbuf, fn, na, 1);
			if (lstbuf == (char *) NULL)
			{
				rc = FOP_ENOMEM;
				break;
			}
			vp3 = (ft_strlst *) lstbuf;
			lstbuf = vp3->buf;
			/* copy buffer, converting delims */
			for (p = vp2, e = p + n2, b = lstbuf; p < e; p++, b++)
			{
				if (*p == dc && c2slmode != TXc2s_create_delimiter)
					*b = '\0';
				else
					*b = *p;
				byteUsed[*(byte *)p] = 1;
			}
			/* KNG 20090122 TXVSSEP_LASTCHAR mode has just terminated the
			 * last item; ..._CREATE has not; others may or may not have:
			 */
			if (c2slmode != TXc2s_trailing_delimiter &&
			    /* Maintain Bug 3677 #12 behavior for `create' mode,
			     * i.e. empty-string should become empty-strlst:
			     */
			    (TXVSSEP_CREATE_EMPTY_STR_TO_EMPTY_STRLST(TXApp) ?
			     !(c2slmode == TXc2s_create_delimiter && n2 == 0) : 1))
				*(b++) = '\0';		/* terminate last item */
			*(b++) = '\0';			/* terminate the list */
			vp3->nb = b - lstbuf;		/* includes list-end nul */
			vp3->delim = dc;
			if (c2slmode == TXc2s_create_delimiter)
			{				/* pick preferred delimiter */
				for (i = 0; i < 256; i++)
					if (!byteUsed[(byte)TxPrefStrlstDelims[i]])
					{
						vp3->delim = TxPrefStrlstDelims[i];
						break;
					}
				/* `f2' could be a DDMMAPI struct which may indeed
				 * have nuls; no guaranteed way of knowing until
				 * Bug 4443 implemented and DDMMAPI is put in a
				 * separate type.  Until then, if `f2' is the size
				 * of a DDMMAPI, it's likely a DDMMAPI:
				 */
				if (byteUsed[0] && n2 != sizeof(DDMMAPI))
					putmsg(MWARN + UGE, fn, "Varchar value contains nuls: conversion to strlst split value");
			}
		}
		setfldandsize(f3, vp3, na, FLD_FORCE_NORMAL);
		break;
	case FOP_IN:
	case FOP_IS_SUBSET:
	case FOP_INTERSECT:
	case FOP_INTERSECT_IS_EMPTY:
	case FOP_INTERSECT_IS_NOT_EMPTY:
		vp2 = (ft_char *) getfld(f2, &n2);
		cs = TXgetStrlst(f1, &sl1);
		cse = cs + sl1.nb;
		if (cse > cs && !cse[-1]) cse--;	/* ign strlst-term. */

		/* Empty set is subset of anything: */
		if ((op == FOP_IS_SUBSET ||
		     (op == FOP_IN && TXApp->inModeIsSubset)) && /* subset */
		    cse <= cs)			/* LHS is empty set */
			goto isTrue;

		/* FOP_INTERSECT: will return set, not boolean: */
		if (op == FOP_INTERSECT &&
		    (!(resBuf = openhtbuf()) ||
		     !TXstrlstBufBegin(resBuf)))
			goto noMem;

		/* Empty varchar is taken as empty strlst, not
		 * one-empty-string strlst (Bug 3677 comment #12);
		 * but not for FOP_IN (Bug 3677 comment #13):
		 */
		if (op != FOP_IN && n2 == 0)	/* RHS is empty set */
		{
			/* For subset, check LHS: empty set is a subset
			 * of empty set.  Otherwise (intersection) LHS
			 * does not matter; result is empty anyway:
			 */
			if (op == FOP_IS_SUBSET ||
			    (op == FOP_IN && TXApp->inModeIsSubset))
				rc = (cse <= cs);  /* true iff LHS is empty */
			else
				rc = 0;
			goto finishSet;
		}

		if (op == FOP_IS_SUBSET ||
		    (op == FOP_IN && TXApp->inModeIsSubset))
		{
			/* For a strlst to be a subset of a varchar,
			 * the strlst must be a single item equal to
			 * the varchar:  Bug 3677:
			 */
			for (e = cs; e < cse && *e; e++); /* 1st item len */
			if (e + 1 < cse)	/* `f1' has 2+ items */
				rc = 0;
			else
				rc = (TXstringcompare(cs, vp2, e-cs, n2) == 0);
		}
		else				/* intersect */
		{
			/* Intersection; true if any `f1' item is `f2': */
			rc = 0;
			for ( ; cs < cse; cs = e + 1)
			{			/* for each `f1' element */
				for (e = cs; e < cse && *e; e++); /* el len */
				if (TXstringcompare(cs, vp2, e - cs, n2) == 0)
				{		/* `f1' element matches `f2'*/
					if (op == FOP_INTERSECT)
					{
						/* Add from `f1'; favor LHS: */
						if (!TXstrlstBufAddString(resBuf, cs, e - cs))
							goto noMem;
					}
					rc = 1;
					break;
				}
			}
		finishSet:
			if (op == FOP_INTERSECT_IS_EMPTY)
				rc = !rc;
			else if (op == FOP_INTERSECT)
			{
				if (!TXstrlstBufEnd(resBuf))
				{
				noMem:
					rc = FOP_ENOMEM;
					goto done;
				}
				n3 = htbuf_getdata(resBuf, (char **)(void *)&vp3, 0x3);
				TXmakesimfield(f1, f3);
				setfldandsize(f3, vp3, n3 + 1, FLD_FORCE_NORMAL);
				rc = 0;
				goto done;
			}
		}
		rc = fld2finv(f3, rc);
		break;
	case FOP_MAT:		/* JMT 98-05-01 */
		vp1 = (ft_strlst *) getfld(f1, &n1);
		vp2 = (ft_char *) getfld(f2, &n2);
		for (cs = vp1->buf; *cs; cs += strlen(cs) + 1)
		{
			if (sregex(vp2, cs))
			{
			isTrue:
				rc = fld2finv(f3, 1);
				goto done;
			}
		}
		rc = fld2finv(f3, 0);
		goto done;
	case FOP_EQ:		/* JMT 98-05-01 */
	case FOP_NEQ:		/* KNG 20081107 */
		vp2 = (ft_char *) getfld(f2, &n2);
		cs = TXgetStrlst(f1, &sl1);
		cse = cs + sl1.nb;
		if (cse > cs && !cse[-1]) cse--;	/* ign strlst-term. */
		if (TXApp->strlstRelopVarcharPromoteViaCreate)
		{
			/* Bug 3677: sl RELOP ch should promote `ch'
			 * to strlst via TXVSSEP_CREATE (not
			 * TXVSSEP_LASTCHAR), and be consistent with
			 * strlstr RELOP strlst; for consistent
			 * behavior when RHS is a Vortex
			 * arrayconverted param.  Old (v6-) behavior
			 * is inmode=intersect; can be obtained with
			 * IN/INTERSECT.  See also TXtrybubble().
			 */
			/* Since foslsl() FOP_CMP ops are stringcompare of
			 * ft_strlst.buf, and our promote is TXVSSEP_CREATE,
			 * we can just do foslsl()-like memcmp() here
			 * (except we ignore strlst-term. nul, as it is
			 * missing from `f2' varchar data; foslsl should too?):
			 */
			if (TXApp->useStringcomparemodeForStrlst)
			{			/* Bug 3677 */
				/* Per Bug 3677 comment #12,
				 * empty-varchar treated as empty-strlst,
				 * not one-empty-string strlst:
				 */
				if (n2 == 0)	/* RHS is empty set */
					rc = (cse == cs); /* true iff LHS mt*/
				else
				{
				  TXget_globalcp();
				  /* TXstringcompare stops @nul; do not use:*/
				  p1 = (CONST char *)cs;
				  p2 = (CONST char *)vp2;
				  rc = TXunicodeStrFoldCmp(&p1, cse - cs, &p2,
					/* n2 + 1 for str-nul: */
					n2 + 1, globalcp->stringcomparemode);
				  rc = (rc == 0);
				}
			}
			else
			{
				if (sl1.nb - 1 != n2 + 1)/*strlst-nul str-nul*/
					rc = 0;
				else
					rc = (memcmp(cs, vp2, n2 + 1) == 0);
			}
			if (op == FOP_NEQ) rc = !rc;
			rc = fld2finv(f3, rc);
			break;
		}
		for ( ; cs < cse && *cs != '\0'; cs = e + 1)
		{
			/* Get next string from `f1': */
			for (e = cs; e < cse && *e != '\0'; e++);
			/* Compare it to `vp2': */
			if ((size_t)(e - cs) == n2 && !memcmp(vp2, cs, n2))
			{			/* `f1' item equals `f2' */
				rc = fld2finv(f3, (op == FOP_EQ));
				goto done;
			}
		}
		rc = fld2finv(f3, !(op == FOP_EQ));
		goto done;
	default:
		if (TXApp->strlstRelopVarcharPromoteViaCreate && (op & FOP_CMP))
		{			/* < <= > >= COM MMIN TWIXT */
			/* Bug 3677: sl RELOP ch promotes `ch' via
			 * TXVSSEP_CREATE; see comments above at FOP_EQ:
			 */
			vp2 = (ft_char *)getfld(f2, &n2);
			cs = TXgetStrlst(f1, &sl1);
			cse = cs + sl1.nb;
			if (cse > cs && !cse[-1]) cse--; /* ign strlst-term. */
			/* based on foslsl(); -2 == strlst-term + str-term: */
			if (TXApp->useStringcomparemodeForStrlst)
			{			/* Bug 3677 */
				TXget_globalcp();
				/* TXstringcompare stops at nul; do not use:*/
				p1 = (CONST char *)cs;
				p2 = (CONST char *)vp2;
				rc = TXunicodeStrFoldCmp(&p1, cse - cs, &p2,
					/* n2 + 1 for str-nul: */
					n2 + 1, globalcp->stringcomparemode);
			}
			else
			{
				rc = memcmp(cs, vp2, TX_MIN(sl1.nb-1, n2+1));
				if (rc == 0)
					rc = (sl1.nb - 2) - n2;
			}
			switch (op)
			{
			case FOP_LT:	rc = (rc < 0);	break;
			case FOP_LTE:	rc = (rc <= 0);	break;
			case FOP_GT:	rc = (rc > 0);	break;
			case FOP_GTE:	rc = (rc >= 0);	break;
			case FOP_COM:	/* rc as-is */	break;
			default:		/* MMIN TWIXT */
				rc = FOP_EILLEGAL;	/* should be putmsg'd by foop[2]()? */
				goto done;
			}
			rc = fld2finv(f3, rc);
			break;
		}
		rc = FOP_EINVAL;
		break;
	}
done:
	if (resBuf) resBuf = closehtbuf(resBuf);
	return (rc);
}				/* end foslch() */

/**********************************************************************/
int
fochsl(f1, f2, f3, op)		/* variable sl or n>1 not supported */
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static CONST char	Fn[] = "fochsl";
	ft_char *vp1;
	ft_strlst	sl2;
	void *mem;
	char *p, *e, *d, *buf2;
	size_t n1, na, n3;
	int rc, var1;
	HTBUF	*resBuf = NULL;

#ifdef DEBUG
	DBGMSG(9, (999, (char *) NULL, "fochsl(,,%d)", op));
#endif
	/* KNG 030725 check for FOP_CNV before TXgetStrlst():
	 * ft_strlst header may be bad/short (FLD type only):
	 */
	if (op == FOP_CNV)			/* KNG 000427 */
	{
		rc = foslch(f2, f1, f3, FOP_ASN);
		goto done;
	}

	if (op == FOP_ASN && TXfldIsNull(f2))
		return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */

	vp1 = (ft_char *) getfld(f1, &n1);
	buf2 = TXgetStrlst(f2, &sl2);
	switch (op)
	{
	case FOP_ASN:
		TXmakesimfield(f1, f3);
		var1 = fldisvar(f1);
		if(f1->size == sizeof(DDMMAPI))
		{
			var1=1;
		}
		e = buf2 + (sl2.nb > 0 ? sl2.nb - 1 : 0);
		if(TXApp->charStrlstConfig.fromStrlst == TXs2c_json_array)
		{
			json_t *tempstring;
			json_t *thearray;
			int strstart = 1;

			thearray = json_array();
			for (p = buf2; p < e; p++)
			{
				if(strstart)
				{
					tempstring = json_string(p);
					if(tempstring)
					{
						json_array_append_new(thearray, tempstring);
					}
				}
				if(*p == '\0')
					strstart = 1;
				else
					strstart = 0;
			}
			mem = json_dumps(thearray, JSON_COMPACT);
			json_decref(thearray);
			na = strlen(mem) + 1;
			setfldandsize(f3, mem, na, FLD_FORCE_NORMAL);
			rc = 0;
		} else {
			na = sl2.nb + 1;		/* +1 for ft_char nul */
			/* Add 1 for trailing delimiter if last string unterminated
			 * (and thus will not have delimiter added):
			 */
			if (e > buf2 && e[-1]) na++;
			if (!var1 && na < n1 + 1)
				na = n1 + 1;		/* fixed-size padding */
			if (na <= 0)			/* sanity check */
				na = 1;
			mem = TXmalloc(TXPMBUFPN, Fn, na);
			if (mem == (void *) NULL) goto noMem;
			setfld(f3, mem, na);
			/* Copy strings, and separate with delimiters: */
			for (p = buf2, d = mem; p < e; p++, d++)
			{
				if (*p == '\0')
					*d = sl2.delim;
				else
					*d = *p;
			}
			/* Add a trailing delimiter if not present and needed
			 * (last string present and unterminated):
			 */
			if (e > buf2 && e[-1]) *(d++) = sl2.delim;
			/* Set `f3' size, and pad data if fixed-size: */
			if (var1)
				f3->n = f3->size = d - (char *)mem;
			else				/* fixed-size f1: pad */
			{
				for (e = (char *) mem + n1; d < e; d++)
					*d = ' ';
			}
			/* Nul-terminate ft_char data: */
			*d = '\0';
			rc = 0;
		}
		goto done;
	case FOP_IN:
	case FOP_IS_SUBSET:
	case FOP_INTERSECT:
	case FOP_INTERSECT_IS_EMPTY:
	case FOP_INTERSECT_IS_NOT_EMPTY:
		/* Both intersect and subset are same for ch OP sl: */
		{
			char *cs;
			size_t cslen;

			if (op == FOP_INTERSECT &&
			    (!(resBuf = openhtbuf()) ||
			     !TXstrlstBufBegin(resBuf)))
				goto noMem;

			/* Empty varchar is taken as empty strlst, not
			 * one-empty-string strlst (Bug 3677 comment #12);
			 * but not for FOP_IN (Bug 3677 comment #13):
			 */
			if (op != FOP_IN && n1 == 0)
			{			/* LHS is empty set */
				/* Empty set is a subset of all sets;
				 * its intersection with any set is empty:
				 */
				if (op == FOP_INTERSECT) goto endRes;
				rc = (op == FOP_IS_SUBSET ||
				      op == FOP_INTERSECT_IS_EMPTY ||
				      (op == FOP_IN && TXApp->inModeIsSubset));
				rc = fld2finv(f3, rc);
				goto done;
			}

			if(f2->dsc.ptrsused > 0
			  && (f2->dsc.ptrsused == f2->dsc.ptrsalloced))
			{
				int j;
				if(f2->issorted)
				{
					int l, r;

					if (TXfldmathverb >= 4)
					  putmsg(MINFO, __FUNCTION__,
                                                 "Binary searching strlst field with pointers");
					l = 0;
					r = f2->dsc.ptrsused;
					while (l < r)
					{
						j = (l+r)/2;
						cs = f2->dsc.dptrs.strings[j].v;
						cslen = f2->dsc.dptrs.strings[j].len;
						rc = TXstringcompare(vp1, cs, n1, cslen);
						switch (rc)
						{
						case 0:
							if (op == FOP_INTERSECT)
								goto addStrAndEndRes;
							rc = fld2finv(f3, (op != FOP_INTERSECT_IS_EMPTY));
							goto done;
						case TX_STRFOLDCMP_ISPREFIX:
							if (op == FOP_INTERSECT)
								goto addStrAndEndRes;
							rc = fld2finv(f3, TX_STRFOLDCMP_ISPREFIX);
							goto done;
						}
						if(rc < 0) r = j;
						else l = j + 1;
					}
					rc = fld2finv(f3, (op == FOP_INTERSECT_IS_EMPTY));
					goto done;
				}

                                if (TXfldmathverb >= 4)
                                  putmsg(MINFO, __FUNCTION__,
                               "Linear searching strlst field with pointers");
				for(j = 0; j < f2->dsc.ptrsused; j++)
				{
					cs = f2->dsc.dptrs.strings[j].v;
					cslen = f2->dsc.dptrs.strings[j].len;
					rc = TXstringcompare(vp1, cs, n1, cslen);
					switch (rc)
					{
					case 0:
						if (op == FOP_INTERSECT)
							goto addStrAndEndRes;
						rc = fld2finv(f3, (op != FOP_INTERSECT_IS_EMPTY));
						goto done;
					case TX_STRFOLDCMP_ISPREFIX:
						if (op == FOP_INTERSECT)
							goto addStrAndEndRes;
						rc = fld2finv(f3, TX_STRFOLDCMP_ISPREFIX);
						goto done;
					}
				}
				rc = fld2finv(f3, (op == FOP_INTERSECT_IS_EMPTY));
				goto done;
			}

                        if (TXfldmathverb >= 4)
                          putmsg(MINFO, __FUNCTION__,
                                 "Linear searching strlst field");
			e = buf2 + sl2.nb;
			if (e > buf2 && !e[-1]) e--;	/* ign. strlst-term.*/
			for (cs = buf2; cs < e; cs += cslen + 1)
			{
				for (cslen = 0; cs + cslen < e && cs[cslen];
				     cslen++);	/* length of item at `cs' */
				rc = TXstringcompare(vp1, cs, n1, cslen);
				switch (rc)
				{
				case 0:		/* `f1' matches `f2' item */
					if (op == FOP_INTERSECT)
						goto addStrAndEndRes;
					rc = fld2finv(f3, (op != FOP_INTERSECT_IS_EMPTY));
					goto done;
				case TX_STRFOLDCMP_ISPREFIX:
					if (op == FOP_INTERSECT)
					{
					addStrAndEndRes:
						/* INTERSECT favors LHS val: */
						if (!TXstrlstBufAddString(resBuf, vp1, n1))
							goto noMem;
						goto endRes;
					}
					rc = fld2finv(f3, TX_STRFOLDCMP_ISPREFIX);
					goto done;
				}
			}
			if (op == FOP_INTERSECT) goto endRes;
			rc = fld2finv(f3, (op == FOP_INTERSECT_IS_EMPTY));
			goto done;
		endRes:				/* FOP_INTERSECT set */
			if (!TXstrlstBufEnd(resBuf))
			{
			noMem:
				rc = FOP_ENOMEM;
				goto done;
			}
			TXmakesimfield(f2, f3);
			n3 = htbuf_getdata(resBuf, &p, 0x3);
			setfldandsize(f3, p, n3 + 1, FLD_FORCE_NORMAL);
			rc = 0;
			goto done;
		}
	case FOP_MAT:
		/* Bug 3677: do not support multi-item RHS of MATCHES until
		 * properly supported; see also TXprepMatchesExpression():
		 */
		rc = FOP_EILLEGAL;	/* should be reported by foop[2]()? */
		goto done;
	default:
		if (TXApp->strlstRelopVarcharPromoteViaCreate && (op & FOP_CMP))
		{			/* = != < <= > >= COM MMIN TWIXT */
			/* Bug 3677; see foslch() comments: */
			switch (op)
			{
			case FOP_LT:
				rc = foslch(f2, f1, f3, FOP_GT);
				goto done;
			case FOP_LTE:
				rc = foslch(f2, f1, f3, FOP_GTE);
				goto done;
			case FOP_GT:
				rc = foslch(f2, f1, f3, FOP_LT);
				goto done;
			case FOP_GTE:
				rc = foslch(f2, f1, f3, FOP_LTE);
				goto done;
			case FOP_EQ:
				rc = foslch(f2, f1, f3, op);
				goto done;
			case FOP_NEQ:
				rc = foslch(f2, f1, f3, op);
				goto done;
			case FOP_COM:
				if ((rc = foslch(f2, f1, f3, op)) == 0)
				{
					ft_int	*v;
					v = (ft_int *)getfld(f3, NULL);
					*v = -*v;
				}
				goto done;
			default:		/* MMIN TWIXT */
				rc = FOP_EILLEGAL;	/* foop[2]() reports?*/
				goto done;
			}
		}
		rc = FOP_EINVAL;
		goto done;
	}
done:
	if (resBuf) resBuf = closehtbuf(resBuf);
	return(rc);
}				/* end fochsl() */

/**********************************************************************/

int
foslco(f1, f2, f3, op)		/* variable sl or n>1 not supported */
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "foslco";
	ft_strlst *vp3;
	ft_counter *vp2;
	size_t n2, na, i;
	char	*b, *e, *fldBuf, *fldBufEnd;
	int rc = 0;

#ifdef DEBUG
	DBGMSG(9, (999, (char *) NULL, "foslco(,,%d)", op));
#endif
	switch (op)
	{
	case FOP_CNV:
		rc = focosl(f2, f1, f3, FOP_ASN);
		break;
	case FOP_ASN:
		TXmakesimfield(f1, f3);
		vp2 = (ft_counter *) getfld(f2, &n2);

		/* +1 for strlst nul, +1 for FLD nul: */
		na = TX_STRLST_MINSZ + (n2 * TX_COUNTER_HEX_BUFSZ) + 2;
		if (na < sizeof(ft_strlst) + 1) na = sizeof(ft_strlst) + 1;
		fldBuf = TXcalloc(TXPMBUFPN, fn, na, 1);
		if (fldBuf == (char *) NULL)
		{
			rc = FOP_ENOMEM;
			break;
		}
		fldBufEnd = fldBuf + na;
		rc = 0;
		vp3 = (ft_strlst *)fldBuf;
		/* print each counter as a string, with nul: */
		for (i = 0, b = vp3->buf; i < n2; i++, b += strlen(b) + 1)
		{
			e = b + TXprintHexCounter(b, fldBufEnd - b, vp2 + i);
			if (e >= fldBufEnd)	/* should not happen */
			{
				putmsg(MERR + MAE, fn, "Short buffer");
				rc = FOP_ENOMEM;
				break;
			}
		}
		*(b++) = '\0';			/* strlst nul */
		vp3->nb = b - vp3->buf;
		vp3->delim = TxPrefStrlstDelims[0];
		*(b++) = '\0';			/* FLD.v nul */
		setfldandsize(f3, vp3, b - fldBuf, FLD_FORCE_NORMAL);
		break;
	case FOP_IN:
		/* intersect boolean is commutative: */
		if (!TXApp->inModeIsSubset)
			return(focosl(f2, f1, f3, op));
		return(FOP_EILLEGAL);	/* wtf not defined/implemented */
	case FOP_IS_SUBSET:
	case FOP_INTERSECT:
		return(FOP_EILLEGAL);	/* wtf not defined/implemented */
	case FOP_INTERSECT_IS_EMPTY:
	case FOP_INTERSECT_IS_NOT_EMPTY:
		/* intersect boolean is commutative: */
		return(focosl(f2, f1, f3, op));
	case FOP_EQ:		/* JMT 98-05-01 */
	default:
		rc = FOP_EINVAL;
		break;
	}
	return (rc);
}				/* end foslco() */

/**********************************************************************/

int
focosl(f1, f2, f3, op)		/* variable sl or n>1 not supported */
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "focosl";
	ft_counter *vp1, *d;
	void *mem;
	char *p, *e, *slBuf2, *pEnd;
	size_t n1, na, n2;
	ft_strlst	slHdr2;
	int rc;

#ifdef DEBUG
	DBGMSG(9, (999, (char *) NULL, "focosl(,,%d)", op));
#endif

	if (op == FOP_CNV)
		return(foslco(f2, f1, f3, FOP_ASN));

	vp1 = (ft_counter *) getfld(f1, &n1);
	slBuf2 = TXgetStrlst(f2, &slHdr2);
	switch (op)
	{
	case FOP_ASN:
		{
			TXmakesimfield(f1, f3);
			n2 = TXgetStrlstLength(&slHdr2, slBuf2);
			na = n2*sizeof(ft_counter) + 1;	/* +1 for FLD.v term.*/
			mem = TXmalloc(TXPMBUFPN, fn, na);
			if (mem == (void *) NULL)
				return (FOP_ENOMEM);
			e = slBuf2 + slHdr2.nb;
			if (e > slBuf2 && e[-1] == '\0') e--; /* list term. */
			for (p = slBuf2, d = mem; p < e; d++)
			{			/* for each string in `f2' */
				for (pEnd = p; pEnd < e && *pEnd; pEnd++);
				/* fail on error?  we let it go, multi-val: */
				TXparseHexCounter(d, p, pEnd);
				p = pEnd;
				if (p < e && !*p) p++;	/* skip str. term. */
			}
			setfldandsize(f3, mem, na, FLD_FORCE_NORMAL);
			return (0);
		}
	case FOP_IN:
	case FOP_IS_SUBSET:
	case FOP_INTERSECT_IS_EMPTY:
	case FOP_INTERSECT_IS_NOT_EMPTY:
		/* WTF support multi/var `f1' and subset/intersect
		 * properly; for now, for n1 == 1, subset/intersect same:
		 */
		if (n1 != 1) return(FOP_EILLEGAL);	/* wtf implement */
		{
			ft_counter tc;

			e = slBuf2 + slHdr2.nb;
			if (e > slBuf2 && e[-1] == '\0') e--; /* list term. */
			for (p = slBuf2; p < e; )
			{			/* for each string in `f2' */
				for (pEnd = p; pEnd < e && *pEnd; pEnd++);
				if (TXparseHexCounter(&tc, p, pEnd) &&
				    vp1->date == tc.date &&
				    vp1->seq == tc.seq)
				{
					rc = fld2finv(f3,
					       (op != FOP_INTERSECT_IS_EMPTY));
					return rc;
				}
				p = pEnd;
				if (p < e && !*p) p++;	/* skip str. term. */
			}
			rc = fld2finv(f3, (op == FOP_INTERSECT_IS_EMPTY));
			return rc;
		}
	case FOP_INTERSECT:			/*returns set; wtf implement?*/
		return(FOP_EILLEGAL);
	default:
		return FOP_EINVAL;
	}
}				/* end focosl() */

/**********************************************************************/

#undef EXTRAVARS
#define EXTRAVARS	int errnum;

#undef foxxsl
#undef foslxx
#undef foslxx_fn
#undef foxxsl_fn
#undef ft_xx
#undef ctype
#undef fmt
#undef cvtxx
#undef strsz

#define foxxsl		foinsl
#define foslxx		foslin
#define foslxx_fn	"foslin"
#define foxxsl_fn	"foinsl"
#define ft_xx		ft_int
#define ctype		int
#define fmt		"%d"
/* Bug 7971: strtol("0xff000000") will return MAXINT on 32-bit platforms,
 * but 0x00000000ff000000 (cast to -16777216 here) on 64-bit.
 * Use 64-bit strtol() replacement for consistency, since SQL int is defined
 * to be consistently 32-bit on all platforms:
 */
#define cvtxx(a, b)	(*(b) = (ft_xx)TXstrtoh((a), NULL, &e, 0, &errnum), \
			 (e > (char *)(a) && errnum == 0))
#define strsz		(EPI_OS_INT_BITS/3 + 3)
#include "fonumsl.c"
#undef foxxsl
#undef foslxx
#undef foslxx_fn
#undef foxxsl_fn
#undef ft_xx
#undef ctype
#undef fmt
#undef cvtxx
#undef strsz

#define foxxsl		folosl
#define foslxx		fosllo
#define foslxx_fn	"fosllo"
#define foxxsl_fn	"folosl"
#define ft_xx		ft_long
#define ctype		long
#define fmt		"%ld"
/* See Bug 7971 comment above: */
#define cvtxx(a, b)	(*(b) = (ft_xx)TXstrtoh((a), NULL, &e, 0, &errnum), \
			 (e > (char *)(a) && errnum == 0))
#define strsz		(EPI_OS_LONG_BITS/3 + 3)
#include "fonumsl.c"
#undef foxxsl
#undef foslxx
#undef foslxx_fn
#undef foxxsl_fn
#undef ft_xx
#undef ctype
#undef fmt
#undef cvtxx
#undef strsz

#undef EXTRAVARS
#define EXTRAVARS	int	errnum;

#define foxxsl	foi6sl
#define foslxx	fosli6
#define foslxx_fn	"fosli6"
#define foxxsl_fn	"foi6sl"
#define ft_xx		ft_int64
#define ctype		EPI_HUGEINT
#define fmt		"%wd"
#define cvtxx(a, b)	(*(b) = (ft_xx)TXstrtoh((a), CHARPN, &e, 0, &errnum),\
			 (e > (char *)(a) && errnum == 0))
#define strsz		(EPI_HUGEINT_BITS/3 + 3)
#include "fonumsl.c"
#undef foxxsl
#undef foslxx
#undef foslxx_fn
#undef foxxsl_fn
#undef ft_xx
#undef ctype
#undef fmt
#undef cvtxx
#undef strsz

#define foxxsl	fou6sl
#define foslxx	foslu6
#define foslxx_fn	"foslu6"
#define foxxsl_fn	"fou6sl"
#define ft_xx		ft_uint64
#define ctype		EPI_HUGEUINT
#define fmt		"%wu"
#define cvtxx(a, b)	(*(b) = (ft_xx)TXstrtouh((a), CHARPN, &e, 0,&errnum),\
			 (e > (char *)(a) && errnum == 0))
#define strsz		(EPI_HUGEUINT_BITS/3 + 3)
#include "fonumsl.c"
#undef foxxsl
#undef foslxx
#undef foslxx_fn
#undef foxxsl_fn
#undef ft_xx
#undef ctype
#undef fmt
#undef cvtxx
#undef strsz

/* NOTE: see also fohach(), fochha(), fldtostr(): */
#define foxxsl	fohasl
#define foslxx	foslha
#define foslxx_fn	"foslha"
#define foxxsl_fn	"fohasl"
#define ft_xx		ft_handle
/* We want the correct number of leading `ffff...'
 * digits for negative off_t, so pick exact-size type for `fmt':
 */
#if TX_FT_HANDLE_BITS == EPI_HUGEINT_BITS
#  define ctype		EPI_HUGEINT
#  define fmt		"%0x08wx"
#elif TX_FT_HANDLE_BITS == EPI_OS_INT_BITS
#  define ctype		int
#  define fmt		"%0x08x"
#else
#  error Need htpf code and C type exactly matching ft_handle
#endif
#define strsz		(EPI_HUGEINT_BITS/3 + 3)
#define cvtxx(a, b)  	(*(b) = (ft_xx)TXstrtoepioff_t((a), CHARPN, &e, 0, \
				  &errnum), (e > (char *)(a) && errnum == 0))
#include "fonumsl.c"
#undef foxxsl
#undef foslxx
#undef foslxx_fn
#undef foxxsl_fn
#undef ft_xx
#undef ctype
#undef fmt
#undef cvtxx
#undef strsz

#define foxxsl		fofosl
#define foslxx		foslfo
#define foslxx_fn	"foslfo"
#define foxxsl_fn	"fofosl"
#define ft_xx		ft_float
#define ctype		double
#define fmt		"%g"
#define cvtxx(a, b)	(*(b) = (ft_xx)TXstrtod((a), CHARPN, &e, &errnum), \
			 (e > (char *)(a) && errnum == 0))
#define strsz		(100)
#include "fonumsl.c"
#undef foxxsl
#undef foslxx
#undef foslxx_fn
#undef foxxsl_fn
#undef ft_xx
#undef ctype
#undef fmt
#undef cvtxx
#undef strsz

#define foxxsl		fodosl
#define foslxx		fosldo
#define foslxx_fn	"fosldo"
#define foxxsl_fn	"fodosl"
#define ft_xx		ft_double
#define ctype		double
#define fmt		"%g"
#define cvtxx(a, b)	(*(b) = (ft_xx)TXstrtod((a), CHARPN, &e, &errnum), \
			 (e > (char *)(a) && errnum == 0))
#define strsz		(100)
#include "fonumsl.c"
#undef foxxsl
#undef foslxx
#undef foslxx_fn
#undef foxxsl_fn
#undef ft_xx
#undef ctype
#undef fmt
#undef cvtxx
#undef strsz

/* ------------------------------------------------------------------------ */

int
TXstrToTxvssep(TXPMBUF *pmbuf, const char *settingName, const char *s,
	       const char *e, TXstrlstCharConfig *res)
/* Returns varchartostrlstsep value for `s' (ending at `e', or strlen(s)
 * if NULL), or TXVSSEP_UNKNOWN if invalid.  Yaps to `pmbuf'.
 */
{
	size_t	n;

	if (!e) e = s + strlen(s);
	n = e - s;
	if (n <= 1)
	{
		CLEAR_C2S_DELIMITERS(res->toStrlst);
		res->toStrlst |= TXc2s_defined_delimiter;
		res->delimiter = s[0];
		return(0);
	}
	if (n == 8 && strnicmp(s, "lastchar", 8) == 0)
	{
		CLEAR_C2S_DELIMITERS(res->toStrlst);
		res->toStrlst |= TXc2s_trailing_delimiter;
		return(0);
	}
	if (n == 6 && strnicmp(s, "create", 6) == 0)
	{
		CLEAR_C2S_DELIMITERS(res->toStrlst);
		res->toStrlst |= TXc2s_create_delimiter;
		return(0);
	}
	if (n == 7 && strnicmp(s, "default", 7) == 0)
	{
		if (TXApp->charStrlstConfigFromIni.toStrlst != TXc2s_unspecified)
		{
			*res = TXApp->charStrlstConfigFromIni;
			return(0);
		}
		else
		{
			res->toStrlst = TXVSSEP_BUILTIN_DEFAULT(TXApp);
			return(0);
		}
	}
	if (n == 14 && strnicmp(s, "builtindefault", 14) == 0)
	{
		res->toStrlst = TXVSSEP_BUILTIN_DEFAULT(TXApp);
		return(0);
	}
	if (n == 4 && strnicmp(s, "json", 4) == 0)
	{
		CLEAR_C2S_JSON(res->toStrlst);
		res->toStrlst |= TXc2s_json_array;
		return(0);
	}
	if (n == 7 && strnicmp(s, "jsonlax", 7) == 0)
	{
		CLEAR_C2S_JSON(res->toStrlst);
		res->toStrlst |= TXc2s_json_array_lax;
		return(0);
	}
	if (n == 6 && strnicmp(s, "nojson", 6) == 0)
	{
		CLEAR_C2S_JSON(res->toStrlst);
		return(0);
	}
	txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN, "Unknown %s value `%.*s'",
		       settingName, (int)n, s);
	return(-1);
}

/* ------------------------------------------------------------------------ */

#ifdef TX_CONVERT_MODE_ENABLED
int
TXsqlFunc_convert(FLD *srcFld, FLD *typeFld, FLD *modeFld)
/* SQL function convert(src, type[, mode]) SQL return: `srcFld' (any
 * type) converted to type named in `typeFld', using optional mode
 * named in `modeFld' (e.g. varchartostrlstsep value for strlst).
 * Note that normal two-arg convert() is handled via conversion to
 * FOP_CNV in ireadnode(); this only sees three-arg convert()s, which
 * are currently only valid for converting to strlst.  Also note that
 * return type is expected to be named in second arg; see check after
 * fofuncret() call in predtype().
 * Returns 0 on success, else FOP_E... error.
 */
{
	static const char	fn[] = "TXsqlFunc_convert";
	size_t			n;
	int			ret;
	TXstrlstCharConfig			saveSep = TXApp->charStrlstConfig, sep;
	FLD			*cnvTypeFld = NULL;
	FLD			*resultFld = NULL;
	char			*s;
	FLDOP			*fo = NULL;
	TXPMBUF			*pmbuf = NULL;

	if (!srcFld || !typeFld) goto err;

	/* Create a field of the target type named in typeFld, for FOP_CNV: */
	if ((typeFld->type & DDTYPEBITS) != FTN_CHAR) goto err;
	s = (char *)getfld(typeFld, &n);
	if (!s) goto err;
	cnvTypeFld = createfld(s, 1, 0);
	if (!cnvTypeFld) goto err;

	/* Apply `modeFld' if given: if strlst, it is varchartostrlstsep
	 * mode; other types do not (yet) support a mode:
	 */
	if (modeFld &&
	    (s = (char *)getfld(modeFld, &n)) != NULL)
	{
		if ((modeFld->type & DDTYPEBITS) != FTN_CHAR) goto err;
		if (*s)
		{
			if ((cnvTypeFld->type & DDTYPEBITS) != FTN_STRLST)
				txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
		"Ignoring mode argument to convert(): only valid for strlst");
				/* continue, ignoring mode */
			else
			{
				sep = TXApp->charStrlstConfig;
				sep.toStrlst = TXc2s_unspecified;
				if(TXstrToTxvssep(TXPMBUFPN,
						     "strlst separator",
						     s, s + n, &sep) == -1) goto err;
				TXApp->charStrlstConfig = sep;
			}
		}
	}

	/* Push args onto stack and perform the convert: */
	if (!(fo = TXgetFldopFromCache())) goto err;
	if (fopush(fo, srcFld) != 0) goto err;
	if (fopush(fo, cnvTypeFld) != 0) goto err;
	if (foop(fo, FOP_CNV) != 0 ||
	    !(resultFld = fopop(fo)))
		goto err;

	/* SQL function return syntax has us put it in the first arg,
	 * i.e. `srcFld':
	 */
	if (!TXfldMoveFld(srcFld, resultFld)) goto err;
	ret = 0;				/* success */
	goto done;

err:
	ret = FOP_EINVAL;
done:
	if (fo) fo = TXreleaseFldopToCache(fo);
	cnvTypeFld = closefld(cnvTypeFld);
	resultFld = closefld(resultFld);
	TXApp->charStrlstConfig = saveSep;
	return(ret);
}
#endif /* TX_CONVERT_MODE_ENABLED */

/******************************************************************/

int
fodach(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "fodach";
	ft_date *vp1, *vp3, d2;
	ft_char *vp2;
	size_t n1, n2;

	if (op == FOP_CNV)
		return fochda(f2, f1, f3, FOP_ASN);

	vp1 = (ft_date *) getfld(f1, &n1);
	if (TXfldIsNull(f2))
		return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */
	vp2 = (ft_char *) getfld(f2, &n2);
	if (*vp2 == '\0')
		d2 = (ft_date)0;
	else
		d2 = (ft_date)parsetime(vp2, n2);

	switch (op)
	{
	case FOP_ASN:
		TXmakesimfield(f1, f3);
		vp3 = (ft_date *) getfld(f3, NULL);
		*vp3 = d2;
		if (*vp3 == (ft_date)(-1))
		{
			if (TXgetparsetimemesg())
#ifdef EPI_NEG_DATE_VALID
				putmsg(MWARN, fn, "Date %s is invalid",
				       vp2);
#else /* !EPI_NEG_DATE_VALID */
				putmsg(MWARN, fn, "Date %s is invalid/out of range",
				       vp2);
#endif /* !EPI_NEG_DATE_VALID */
#ifdef NEVER			/* WTF - Should probably return this. */
			return FOP_EDOMAIN;
#endif
		}
		return 0;
	case FOP_EQ:
		return fld2finv(f3, *vp1 == d2);
	case FOP_NEQ:
		return fld2finv(f3, *vp1 != d2);
	case FOP_LT:
		return fld2finv(f3, *vp1 < d2);
	case FOP_LTE:
		return fld2finv(f3, *vp1 <= d2);
	case FOP_GT:
#ifdef DEBUG
		DBGMSG(9, (999, NULL, "Comparing %d to %d = %d",
			   *vp1, d2, *vp1 > d2));
#endif
		return fld2finv(f3, *vp1 > d2);
	case FOP_GTE:
		return fld2finv(f3, *vp1 >= d2);
	case FOP_COM:
		if (*vp1 > d2)
			return fld2finv(f3, 1);
		else if (*vp1 < d2)
			return fld2finv(f3, -1);
		else
			return fld2finv(f3, 0);
	default:
		return FOP_EINVAL;
	}
}

/******************************************************************/

int
fodtch(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_datetime *datetime, *datetimeDest, datetime2;
	ft_char *vp2, *s, *e;
	size_t n1, n2;
	long	lVal;
	double	dVal;
	time_t	tim;

	if (op == FOP_CNV)
		return fochdt(f2, f1, f3, FOP_ASN);

	datetime = (ft_datetime *) getfld(f1, &n1);
#define ISNULL(f)	((f)->v == NULL || (f)->n <= (size_t)0)
	if (TXfldIsNull(f2))
		return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */
	vp2 = (ft_char *) getfld(f2, &n2);

	/* Clear struct padding too, to shut up Valgrind and avoid random
	 * bytes (albeit padding) in a table column:
	 */
	memset(&datetime2, 0, sizeof(ft_datetime));
	/* WTF need a parsetime() that can handle full datetime range/frac: */
	if (*vp2 == '\0' || strcmpi(vp2, "null") == 0)
	{
		/* already 0 */
	}
	else if (strcmpi(vp2, "now") == 0)	/* WTF parsetime() cache */
	{
		TXTIMEINFO	timeinfo;

		dVal = TXgettimeofday();
		tim = (time_t)dVal;
		if (!TXtime_tToLocalTxtimeinfo(tim, &timeinfo))
			return(FOP_EINVAL);
		datetime2.year = timeinfo.year;
		datetime2.month = timeinfo.month;
		datetime2.day = timeinfo.dayOfMonth;
		datetime2.hour = timeinfo.hour;
		datetime2.minute = timeinfo.minute;
		datetime2.second = timeinfo.second;
		datetime2.fraction = (UDWORD)((dVal-(double)tim)*1000000000.0);
	}
	else					/* parse `vp2' for datetime */
	{
		/* WTF only YYYY-mm-dd HH:MM:SS.nnnnnnnnn format for now: */
		s = vp2;
		lVal = strtol(s, &e, 10);	/* YYYY year */
		datetime2.year = (SWORD)lVal;
		if ((long)datetime2.year != lVal || *s == '\0' || e == s)
			goto eInval;
		s = e;

		if (*s == '-') s++;		/* -mm month */
		lVal = strtol(s, &e, 10);
		datetime2.month = (UWORD)lVal;
		if ((long)datetime2.month != lVal || *s == '\0' || e == s)
			goto eInval;
		s = e;

		if (*s == '-') s++;		/* -dd day */
		lVal = strtol(s, &e, 10);
		datetime2.day = (UWORD)lVal;
		if ((long)datetime2.day != lVal || *s == '\0' || e == s)
			goto eInval;
		s = e;

		if (*s == ' ') s++;		/* HH hour */
		lVal = strtol(s, &e, 10);
		datetime2.hour = (UWORD)lVal;
		if ((long)datetime2.hour != lVal || *s == '\0' || e == s)
			goto eInval;
		s = e;

		if (*s == ':') s++;		/* :MM minute */
		lVal = strtol(s, &e, 10);
		datetime2.minute = (UWORD)lVal;
		if ((long)datetime2.minute != lVal || *s == '\0' || e == s)
			goto eInval;
		s = e;

		if (*s == ':') s++;		/* :SS second */
		lVal = strtol(s, &e, 10);
		datetime2.second = (UWORD)lVal;
		if ((long)datetime2.second != lVal || *s == '\0' || e == s)
			goto eInval;
		s = e;

		if (*s == '.')			/* .nnnnnnnnn fraction */
		{
			s++;
			lVal = strtol(s, &e, 10);
			datetime2.fraction = (UDWORD)lVal;
			if ((long)datetime2.fraction != lVal ||
			    *s == '\0' || e == s)
				goto eInval;
			s = e;
			s += strspn(s, " \t\r\n\v\f");
			if (*s != '\0') goto eInval;
		}
		else
			datetime2.fraction = 0;
	}

	switch (op)
	{
	case FOP_ASN:
		TXmakesimfield(f1, f3);
		datetimeDest = (ft_datetime *) getfld(f3, NULL);
		*datetimeDest = datetime2;
		return 0;
	case FOP_EQ:
		return fld2finv(f3, (!ISNULL(f1) &&
				     ISEQ_DT(datetime, &datetime2)));
	case FOP_NEQ:
		return fld2finv(f3, (!ISNULL(f1) &&
				     !ISEQ_DT(datetime, &datetime2)));
	case FOP_LT:
		return fld2finv(f3, (!ISNULL(f1) &&
				     ISLT_DT(datetime, &datetime2)));
	case FOP_LTE:
		return fld2finv(f3, (!ISNULL(f1) &&
				     (ISLT_DT(datetime, &datetime2) ||
				      ISEQ_DT(datetime, &datetime2))));
	case FOP_GT:
		return fld2finv(f3, (!ISNULL(f1) &&
				     ISGT_DT(datetime, &datetime2)));
	case FOP_GTE:
		return fld2finv(f3, (!ISNULL(f1) &&
				     (ISGT_DT(datetime, &datetime2) ||
				      ISEQ_DT(datetime, &datetime2))));
	case FOP_COM:
		if (ISNULL(f1))
			return fld2finv(f3, 1);
		else if (ISGT_DT(datetime, &datetime2))
			return fld2finv(f3, 1);
		else if (ISLT_DT(datetime, &datetime2))
			return fld2finv(f3, -1);
		else
			return fld2finv(f3, 0);

	default:
	eInval:
		return FOP_EINVAL;
	}
#undef ISNULL
}

/******************************************************************/

static char *datefmt = "%Y-%m-%d %H:%M:%S";
static int freedate = 0;
static size_t datebufsz = 20;

int
TXsetdatefmt(fmt)
char *fmt;
{
	static const char	fn[] = "TXsetdatefmt";

	if (freedate)
		datefmt = TXfree(datefmt);
	if (fmt && fmt[0])
	{
		datefmt = TXstrdup(TXPMBUFPN, fn, fmt);
		datebufsz = strlen(fmt) + 50;	/* WTF WAG   -KNG 980331 */
		freedate = 1;
	}
	else
	{
		datefmt = "%Y-%m-%d %H:%M:%S";
		datebufsz = 20;
		freedate = 0;
	}
	return 0;
}

/******************************************************************/

int
fochda(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "fochda";
	ft_date *vp2;
	size_t n1, n2, na;
	int var1;
	char *mem;
	struct tm *tm;

	switch (op)
	{
	case FOP_ASN:
		if (TXfldIsNull(f2))
			return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */
		TXmakesimfield(f1, f3);
		getfld(f1, &n1);
		vp2 = (ft_date *) getfld(f2, &n2);
		var1 = fldisvar(f1);
		if ((*vp2 > 0
#ifdef EPI_NEG_DATE_VALID
		    || *vp2 < -1
#endif /* EPI_NEG_DATE_VALID */
			)
#if defined(hpux) && defined(__LP64__)
/* Despite the man page, localtime() under 64-bit not only doesn't
 * handle dates past 2037, but segfaults:   KNG 020619
 */
			&& *vp2 <= (ft_date)0x7fffffff
#endif
			)
		{
		      again:
			na = (var1 ? datebufsz : TX_MAX(datebufsz, n1 + 1));
			mem = (ft_char *) TXcalloc(TXPMBUFPN, fn, na, 1);
			if (!mem) goto noMem;
			tm = localtime(vp2);
			if (tm == NULL) goto range;
			/* WTF Linux strftime() is broken and may overflow
			 * the buffer if too small:   -KNG 980331
			 * KNG 20160930 strftime() returns 0 on overflow:
			 */
			if (strftime(mem, na, datefmt, tm) == 0)
			{
				mem = TXfree(mem);
				datebufsz += 5;
				goto again;
			}
		}
		else
		{
			if (*vp2 == 0)
				mem = TXstrdup(TXPMBUFPN, fn, "NULL");
			else
			if (*vp2 == -1)
				mem = TXstrdup(TXPMBUFPN, fn, "Invalid");
			else	/* if (*vp2 < -1) */
			{
			range:
				mem = TXstrdup(TXPMBUFPN, fn, "Out of range");
			}
			if (!mem) goto noMem;
			na = strlen(mem) + 1;
		}
		if (!var1)
		{
			/* Increase alloc if too small for `f1' size: */
			if (na < n1 + 1)
			{
				char	*newBuf;

				newBuf = (char *)TXmalloc(TXPMBUFPN, fn,
							  n1 + 1);
				if (!newBuf)
				{
				noMem:
					/* Clear possible TXmakesimfield()
					 * garbage:
					 */
					setfld(f3, NULL, 0);
					return(FOP_ENOMEM);
				}
				memcpy(newBuf, mem, na);
				mem = TXfree(mem);
				mem = newBuf;
				newBuf = NULL;
				na = n1 + 1;
			}
			/* Pad right with spaces if result too small: */
			for (n2 = strlen(mem); n2 < n1; n2++)
				mem[n2] = ' ';
			/* Truncate if result too large, or terminate
			 * padding if result was too small:
			 */
			mem[n1] = '\0';
		}
		setfld(f3, mem, na);
		f3->n = f3->size = strlen(mem);
		return(FOP_EOK);
	case FOP_CNV:
		return fodach(f2, f1, f3, FOP_ASN);
	}
	return FOP_EINVAL;
}

/******************************************************************/

int
fochdt(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "fochdt";
	ft_datetime *datetime;
	size_t n1, n2, na, numWritten, n;
	int var1;
	char *mem;

	switch (op)
	{
	case FOP_ASN:
		TXmakesimfield(f1, f3);
		getfld(f1, &n1);
		datetime = (ft_datetime *) getfld(f2, &n2);
		var1 = fldisvar(f1);
		if (n2 > 0 && datetime != (ft_datetime *)NULL &&
		    (datetime->year != 0 ||	/* WAG valid */
		     datetime->month != 0 ||
		     datetime->day != 0 ||
		     datetime->hour != 0 ||
		     datetime->minute != 0 ||
		     datetime->second != 0 ||
		     datetime->fraction != 0))
		{
			/* WTF we use YYYY-mm-dd HH:MM:SS.nnnnnnnnn instead
			 * of datefmt:
			 */
#define DATETIME_FMTLEN	29
			if (var1)
				na = DATETIME_FMTLEN + 1;
			else
			{
				if (n1 < DATETIME_FMTLEN)
					return FOP_ENOMEM;
				na = n1 + 1;
			}
			mem = (ft_char *) TXmalloc(TXPMBUFPN, fn, na);
			if (!mem)
				return FOP_ENOMEM;
			numWritten = htsnpf(mem, na,
				"%04d-%02d-%02d %02d:%02d:%02d",
				(int)datetime->year,
				(int)datetime->month,
				(int)datetime->day,
				(int)datetime->hour,
				(int)datetime->minute,
				(int)datetime->second);
			/* For maximum parsetime() compatibility and brevity,
			 * do not print the fraction unless it is non-zero:
			 */
			if (datetime->fraction)
				htsnpf(mem + numWritten, na - numWritten,
					".%09d", (int)datetime->fraction);
		}
		else
		{
			/* WTF check NULL/Invalid/Out of range: */
			mem = TXstrdup(TXPMBUFPN, fn, "NULL");
			na = strlen(mem) + 1;
		}
		setfld(f3, mem, na);
		if (var1)
			f3->n = f3->size = strlen(mem);
		else
		{
			for (n = strlen(mem); n < n1; n++)
				mem[n] = ' ';
			mem[n] = '\0';
		}
		return 0;
	case FOP_CNV:
		return fodtch(f2, f1, f3, FOP_ASN);
	}
	return FOP_EINVAL;
}

/******************************************************************/

static int TXindcom = 0;

int
TXindcompat(value)
char *value;
{
	if (!strcmpi(value, "on"))
	{
		TXindcom = 1;
		return 1;
	}
	if (!strcmpi(value, "off"))
	{
		TXindcom = 0;
		return 1;
	}
	return 0;
}

/******************************************************************/

int
fochid(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "fochid";
	ft_indirect *vp2;
	size_t n1, n2, na;
	int var1;
	char *mem;

	/* struct       tm      *tm; */

	if (op == FOP_CNV)
		return foidch(f2, f1, f3, FOP_ASN);
	if (op != FOP_ASN)
		return FOP_EINVAL;
	TXmakesimfield(f1, f3);
	getfld(f1, &n1);
	vp2 = (ft_indirect *) getfld(f2, &n2);
	var1 = fldisvar(f1);
	{
		na = n2 + 2;
		if (!var1)
		{
			if (n1 < na)
				return FOP_ENOMEM;
			na = n1;
		}
		mem = (ft_char *) TXmalloc(TXPMBUFPN, fn, na);
		if (!mem)
			return FOP_ENOMEM;
		strcpy(mem, vp2);
		if (TXindcom)
			strcat(mem, "@");
		else
			na--;
	}
	setfld(f3, mem, na);
	if (var1)
		f3->n = f3->size = strlen(mem);
	else
	{
		for (n2 = strlen(mem); n2 < n1; n2++)
			mem[n2] = '\0';
	}
	return 0;
}

/******************************************************************/

int
foidch(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "foidch";
	ft_char *vp2;
	size_t n1, n2, na;
	int var1;
	char *mem;

	/* struct       tm      *tm; */

	switch (op)
	{
	case FOP_CNV:
		return fochid(f2, f1, f3, FOP_ASN);
	case FOP_ASN:
		TXmakesimfield(f1, f3);
		getfld(f1, &n1);
		vp2 = (ft_char *) getfld(f2, &n2);
		var1 = fldisvar(f1);
		{
			na = n2 + 1;
			if (!var1)
			{
				if (n1 < na)
					return FOP_ENOMEM;
				na = n1;
			}
			mem = (ft_char *) TXmalloc(TXPMBUFPN, fn, na);
			if (!mem)
				return FOP_ENOMEM;
			strcpy(mem, vp2);
		}
		setfld(f3, mem, na);
		if (var1)
			f3->n = f3->size = strlen(mem);
		else
		{
			for (n2 = strlen(mem); n2 < n1; n2++)
				mem[n2] = '\0';
			mem[n2] = '\0';
		}
		return 0;
	default:
		return FOP_EINVAL;
	}
}

/******************************************************************/

int
fodaco(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_date *vp3;
	ft_counter *vp2;
	size_t n1, n2;

	/* int  var1; */
	/* char *mem; */

	if (op == FOP_CNV)
	{
		return focoda(f2, f1, f3, FOP_ASN);
	}
	if (op != FOP_ASN)
		return FOP_EINVAL;
	TXmakesimfield(f1, f3);
	vp3 = (ft_date *) getfld(f3, &n1);
	vp2 = (ft_counter *) getfld(f2, &n2);
	if (vp2)
		*vp3 = (ft_date)vp2->date;
	else
		*vp3 = 0;
	return 0;
}

/******************************************************************/

int
focoda(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_counter *vp3;
	ft_date *vp2;
	size_t n1, n2;

	/* int isdt; */

	switch (op)
	{
	case FOP_CNV:
		return fodaco(f2, f1, f3, FOP_ASN);
	case FOP_ASN:
		TXmakesimfield(f1, f3);
		vp3 = (ft_counter *) getfld(f3, &n1);
		vp2 = (ft_date *) getfld(f2, &n2);
		vp3->date = (TXft_counterDate)(*vp2);
		vp3->seq = (TXft_counterSeq)0;
		f3->n = 1;
		return 0;
	default:
		return FOP_EINVAL;
	}
}

/******************************************************************/

int (*opendbfunc) (void *) = NULL;
void *opendbusr = NULL;

/******************************************************************/

int (*TXsetopendbfunc(int (*newfunc) (void *), void *usr, void **oldusr))
	(void *)
{
	int (*rc) (void *) = opendbfunc;

	if (oldusr)
		*oldusr = opendbusr;
	opendbusr = usr;
	opendbfunc = newfunc;
	return rc;
}

/******************************************************************/

/* returns the current default global ddic */
static int
TXgetstddic()
{
	if (ddic)
		return 0;
	if (opendbfunc)
		(*opendbfunc) (opendbusr);
	if (ddic)
		return 0;
	return -1;
}

/* Sets an already-opened ddic to be the default global ddic. */
int
TXusestddic(d)
DDIC *d;
{
	int i;
	for(i=0;i<NIDD; i++)
	{
		if(iddic[i].used != 0 && iddic[i].ddic == d)
		{
			ddic = d;
			return 0;
		}
	}
	return -1;
}

/* Registers a newly opened ddic in the global list of ddics, and sets
 * it to be the default global ddic. */
int
TXsetstddic(d)
DDIC *d;
{
	int i;

	for (i = 0; i < NIDD; i++)
		if (iddic[i].used == 0)
		{
			iddic[i].used = 1;
			iddic[i].ddic = d;
			ddic = d;
			return 0;
		}
	return -1;
}

/* Removes the ddic we're closing from the global list.  If this
 * was the current global default, a new one is chosen from the
 * list (if any), just to have A default).*/
int
TXunsetstddic(d)
DDIC *d;
{
	int i, j;

	for (i = 0; i < NIDD; i++)
		if (iddic[i].ddic == d && iddic[i].used == 1)
		{
			iddic[i].used = 0;
			iddic[i].ddic = NULL;
			if (ddic == d)
			{
				ddic = NULL;
				for (j = 0; j < NIDD; j++)
					if (iddic[j].used == 1)
						ddic = iddic[j].ddic;
			}
			return 0;
		}
	return -1;
}

/******************************************************************/

/* counteri ops: mapped to counter ops via fldopci.c helper functions;
 * each `xx' type here must have focoxx() and foxxco() functions already:
 */
#undef ft_xx
#undef xx
#define ft_xx	ft_counter
#define xx	co
#include "fldopci.c"

#undef ft_xx
#undef xx
#define ft_xx	ft_char
#define xx	ch
#include "fldopci.c"

#undef ft_xx
#undef xx
#define ft_xx	ft_byte
#define xx	by
#include "fldopci.c"

#undef ft_xx
#undef xx
#define ft_xx	ft_date
#define xx	da
#include "fldopci.c"

#undef ft_xx
#undef xx
#define ft_xx	ft_strlst
#define xx	sl
#include "fldopci.c"

/******************************************************************/

int
forech(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	switch (op)
	{
	case FOP_CNV:
		return fochre(f2, f1, f3, FOP_ASN);
	default:
		return FOP_EINVAL;
	}
}

/******************************************************************/

int
fochre(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
/* NOTE: see also fochha() */
{
	static const char	fn[] = "fochre";
	ft_char	*mem;
	ft_recid *vp2, re;
	size_t n1, n2, na;
	int var1;

	switch (op)
	{
	case FOP_ASN:
		getfld(f3, &n1);
		vp2 = (ft_recid *) getfld(f2, &n2);
		re = *vp2;
		TXmakesimfield(f1, f3);
		var1 = fldisvar(f1);
#define BYTES	(TX_MAX(EPI_OFF_T_BITS, 32)/4)
		na = BYTES + 1;
		mem = (ft_char *) TXmalloc(TXPMBUFPN, fn, na);
		if (!mem)
			return FOP_ENOMEM;
		htsnpf(mem, na, "%08wx", (EPI_HUGEINT)re.off);
#undef BYTES
		setfld(f3, mem, na);
		if (var1)
			f3->n = f3->size = strlen(mem);
		else
		{
			for (n2 = strlen(mem); n2 < n1; n2++)
				mem[n2] = ' ';
			mem[n2] = '\0';
		}
		return 0;

	default:
		return FOP_EINVAL;
	}
}

/******************************************************************/

int
fochbi(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "fochbi";
	ft_char	*mem = (ft_char *) NULL;
	ft_blobi *vp2;
	size_t	n2, na;
	DBF *fd;
	void	*vp2Mem = NULL;
	size_t	vp2MemSz = 0;

	switch (op)
	{
	case FOP_CNV:
		return fobich(f2, f1, f3, FOP_ASN);
	case FOP_ASN:
		if (TXfldIsNull(f2))
			return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */
		vp2 = (ft_blobi *) getfld(f2, &n2);
		TXmakesimfield(f1, f3);
		fd = TXblobiGetDbf(vp2);
		vp2Mem = TXblobiGetMem(vp2, &vp2MemSz);
		if (vp2Mem)
		{
			/* KNG 20120118 Bug 4030: we no longer count/store
			 * varchar's nul-terminator as blob payload, so add
			 * nul back in here:
			 */
			na = vp2MemSz + 1;	/* +1 for nul */
			if(na > 0)
			{
				mem = TXmalloc(TXPMBUFPN, fn, na);
				if (mem)
				{
					memcpy(mem, (char *)vp2Mem, vp2MemSz);
					mem[vp2MemSz] = '\0';
				}
			}
		} else
		if (fd)
		{
			switch (TXblobiGetStorageType(vp2))
			{
			case FTN_BLOB:
				if(vp2->off == (EPI_OFF_T)(-1))
				{		/* invalid offset */
					na = 1;
					mem = (ft_char *) TXcalloc(TXPMBUFPN, fn, na, sizeof(ft_char));
				}
				else
				{
					mem = agetdbf(fd, vp2->off, &na);
					/* KNG 20120118 Bug 4030: varchar nul
					 * not part of blob payload anymore:
					 */
					na++;	/* for nul from agetdbf() */
				}
				break;
			case FTN_BLOBZ:
				mem = (ft_char *)TXagetblobz(vp2, &na);
				na++;		/* nul from TXagetblobz() */
				break;
			default:
				putmsg(MERR + UGE, fn, "Unknown blob type %d",
				       (int)TXblobiGetStorageType(vp2));
				return(FOP_EINVAL);
			}
		}
		if (!mem)
		{
			na = 1;
			mem = (ft_char *) TXcalloc(TXPMBUFPN, fn, na, sizeof(ft_char));
		}
		setfldandsize(f3, mem, na, FLD_FORCE_NORMAL);
		return(mem ? 0 : FOP_ENOMEM);
	default:
		return FOP_EINVAL;
	}
}

/******************************************************************/

int
fobich(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "fobich";
	ft_blobi *vp3, blbi;
	ft_char *vp2;
	size_t n2, na;
	char *mem;

	/* struct       tm      *tm; */

	switch (op)
	{
	case FOP_CNV:
		return fochbi(f2, f1, f3, FOP_ASN);
	case FOP_ASN:
		if (TXfldIsNull(f2))
			return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */
		TXmakesimfield(f1, f3);
		vp2 = (ft_char *) getfld(f2, &n2);
		na = n2 + 1;
		mem = (ft_char *) TXmalloc(TXPMBUFPN, fn, na);
		if (!mem)
			return FOP_ENOMEM;
		memcpy(mem, vp2, n2);		/* Bug 1782 copy all data */
		mem[n2] = '\0';
		vp3 = TXcalloc(TXPMBUFPN, fn, 1, sizeof(blbi) + 1);
		if (!vp3)
		{
			mem = TXfree(mem);
			return(FOP_EINVAL);
		}
		TXblobiSetMem(vp3, mem, na-1, 1);/*Bug 4030 nul is not payload*/
		mem = NULL;
		TXblobiSetDbf(vp3, NULL);
		vp3->otype = FTN_BLOB;
		setfldandsize(f3, vp3, sizeof(blbi) + 1, FLD_FORCE_NORMAL);
		return 0;
	default:
		return FOP_EINVAL;
	}
}

/******************************************************************/

int
fobibi(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "fobibi";
	ft_blobi *vp3, blbi;
	ft_blobi *vp2;
	void	*vp2Mem = NULL;
	size_t	vp2MemSz = 0;

	/* struct       tm      *tm; */

	switch (op)
	{
	case FOP_CNV:
		return fobibi(f2, f1, f3, FOP_ASN);
	case FOP_ASN:
		TXmakesimfield(f1, f3);
		vp2 = (ft_blobi *) getfld(f2, NULL);
		memset(&blbi, 0, sizeof(ft_blobi));
		blbi.otype = vp2->otype;
		blbi.dbf = vp2->dbf;
		blbi.off = vp2->off;
		blbi.ndfree = 0;
		vp2Mem = TXblobiGetMem(vp2, &vp2MemSz);
		if (vp2Mem)
		{
			void	*mem;

			mem = TXmalloc(TXPMBUFPN, fn, vp2->len + 1);
			memcpy(mem, vp2Mem, vp2MemSz);
			((char *)mem)[vp2MemSz] = '\0';
			TXblobiSetMem(&blbi, mem, vp2MemSz, 1);
		}
		else
			TXblobiSetMem(&blbi, NULL, 0, 0);
		vp3 = TXcalloc(TXPMBUFPN, fn, 1, sizeof(blbi) + 1);
		memcpy(vp3, &blbi, sizeof(blbi));
		setfld(f3, vp3, sizeof(blbi));
		f3->elsz = f3->size = sizeof(blbi);
		f3->n = 1;
		return 0;
	default:
		return FOP_EINVAL;
	}
}

/******************************************************************/

int
fobybi(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "fobybi";
	ft_byte	*mem = (ft_byte *) NULL;
	ft_blobi *vp2;
	size_t	n2, na;
	int var1;
	DBF *fd;
	void	*vp2Mem = NULL;
	size_t	vp2MemSz = 0;

	switch (op)
	{
	case FOP_ASN:
		vp2 = (ft_blobi *) getfld(f2, &n2);
		TXmakesimfield(f1, f3);
		var1 = fldisvar(f1);
		fd = vp2->dbf;
		vp2Mem = TXblobiGetMem(vp2, &vp2MemSz);
		if(vp2Mem)
		{
			na = vp2MemSz + 1;
			mem = TXmalloc(TXPMBUFPN, fn, na);
			if (!mem)
				return FOP_ENOMEM;
			memcpy(mem, (ft_byte *) vp2Mem, vp2MemSz);
			mem[vp2MemSz] = '\0';
		} else if (fd)
		{
			if(vp2->off == -1)
			{
				na = 1;
				mem = (ft_byte *) TXcalloc(TXPMBUFPN, fn, na, sizeof(ft_byte));
			}
			else
			{
				mem = agetdbf(fd, vp2->off, &na);
				na++;		/* for nul from agetdbf() */
			}
		}
		if (!mem)
		{
			na = 1;
			mem = (ft_byte *) TXcalloc(TXPMBUFPN, fn, na,
						sizeof(ft_char));
		}
		if (var1)
			setfldandsize(f3, mem, na, FLD_FORCE_NORMAL);
		else
			setfld(f3, mem, na);
		return 0;
	case FOP_CNV:
		return fobiby(f2, f1, f3, FOP_ASN);
	default:
		return FOP_EINVAL;
	}
}

/******************************************************************/

int
fobiby(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = "fobiby";
	ft_blobi *vp3;
	ft_byte *vp2;
	size_t n2, na;
	ft_byte *mem;

	/* struct       tm      *tm; */

	switch (op)
	{
	case FOP_CNV:
		return fobybi(f2, f1, f3, FOP_ASN);
	case FOP_ASN:
		TXmakesimfield(f1, f3);
		vp2 = (ft_byte *) getfld(f2, &n2);
		na = n2 + 1;
		mem = (ft_byte *) TXmalloc(TXPMBUFPN, fn, na);
		if (!mem)
			return FOP_ENOMEM;
		memcpy(mem, vp2, n2);
		mem[n2] = '\0';
		vp3 = TXcalloc(TXPMBUFPN, fn, 1, sizeof(ft_blobi) + 1);
		TXblobiSetMem(vp3, mem, na - 1, 1);
		mem = NULL;
		TXblobiSetDbf(vp3, NULL);
		vp3->otype = FTN_BLOB;
		setfldandsize(f3, vp3, sizeof(ft_blobi) + 1, FLD_FORCE_NORMAL);
		return 0;
	default:
		return FOP_EINVAL;
	}
}
