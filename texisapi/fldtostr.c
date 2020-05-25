/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"				/* for htsnpf() */

#define BSZ 8192
static char	thebuf[BSZ];
#define EOB	(thebuf + BSZ)
static char	*buf = thebuf;
static char	*lastAlloc = NULL;

int	TXfldtostrHandleBase10 = 0;

void
TXfldtostrFreeMemAtExit(void)
{
	/* Free fldtostr() data that may have needed to stick around
	 * after fldtostr() call:
	 */
	lastAlloc = TXfree(lastAlloc);
}

char *
fldtostr(f)
FLD *f;
/* NOTE: Thread-unsafe.
 */
{
	static CONST char	nullstr[] = "(null)";
	static CONST char	ellipsecomma[] = "...,";
	static CONST char	InternalDateFmt[] = "|%Y-%m-%d %H:%M:%S";
#define DateFmt	(InternalDateFmt + 1)
	void	*v;
	char	*ret;
	size_t	n, nPr, size;
	ft_datetime	*dt;
	ft_strlst	slHdr;
	int	res;
	FTN	type;
	ft_internal		*fti;
	TXftiValueWithCooked	*valueWithCooked;
	const char	*fmtInt = "%d", *fmtLong = "%ld";
	const char	*fmtUnsigned = "%u";
	const char	*fmtIntComma = "%d,", *fmtCommaLong = ",%ld";
	const char	*fmtUnsignedComma = "%u,";

	if (TXfldmathVerboseHexInts)
	{
		fmtInt = fmtUnsigned = "0x%x";
		fmtIntComma = fmtUnsignedComma = "0x%x,";
		fmtLong = "0x%lx";
		fmtCommaLong = ",0x%lx";
	}

	/* see also below: */
	switch (TXfldType(f) & FTN_VarBaseTypeMask)
	{
	case FTN_COUNTERI:
		/* wtf even for NULL, for column name for `select counter': */
		return("counter");
	default:
		break;
	}
	if (TXfldIsNull(f)) return(TXfldGetNullOutputString());

	type = TXfldType(f);
	v = getfld(f, &n);
	size = f->size;
	if ((type & DDTYPEBITS) == FTN_INTERNAL &&
	    (fti = (ft_internal *)v) != NULL &&
	    tx_fti_gettype(fti) == FTI_valueWithCooked &&
	    (valueWithCooked = tx_fti_getobj(fti)) != NULL)
	{
		v = TXftiValueWithCooked_GetValue(valueWithCooked, &type, &n,
						  &size);
		f = NULL;
	}

	if (buf + TX_COUNTER_HEX_BUFSZ + 64 > EOB)
		buf = thebuf;				/* wrap around */
	lastAlloc = TXfree(lastAlloc);

        switch (type & FTN_VarBaseTypeMask)
        {
                case FTN_CHAR :
                case FTN_CHAR | DDVARBIT :
                case FTN_INDIRECT:                    /* MAW 06-02-94 */
                case FTN_INDIRECT|DDVARBIT:           /* MAW 06-02-94 */
                        return v;

#ifdef GCC_DOUBLE_FIX
#  ifdef __GNUC__
#    define TMP_DECL(fttype)	\
	fttype	ftTmp; volatile byte *itemSrc, *itemDest;
#  else
#    define TMP_DECL(fttype)	fttype	ftTmp;
#  endif
#  ifdef __GNUC__
	/* gcc tries very hard to optimize this memcpy() into an
	 * inline ft_xx assignment, with possible bus error results on
	 * Sparc.  It appears we need true byte-pointer args to memcpy()
	 * to force a byte-wise copy.  See also FDBI_TXALIGN_RECID_COPY(),
	 * fldop2.c:
	 */
#    define ITEM(fttype, i)	\
			(itemSrc = (volatile byte *)&(((fttype *)v)[i]), \
			 itemDest = (volatile byte *)&ftTmp,		\
			 memcpy((byte *)itemDest, (byte *)itemSrc,	\
				sizeof(fttype)), ftTmp)
#  else /* !__GNUC__ */
#    define ITEM(fttype, i)	\
		(memcpy(&ftTmp, &(((fttype *)v)[i]), sizeof(fttype)), ftTmp)
#  endif /* !__GNUC__ */
#else /* !GCC_ALIGN_FIX */
#  define TMP_DECL(fttype)
#  define ITEM(fttype, i)	(((fttype *)v)[i])
#endif /* !GCC_ALIGN_FIX */

#define FIXNUM(fttype, fmt, fmttype)				\
	{							\
		size_t	i;					\
		char	*d;					\
		TMP_DECL(fttype)				\
								\
		if (v == NULL)					\
			/* Should not happen: TXfldIsNull() check above: */ \
			htsnpf(buf, EOB - buf, nullstr);	\
		else						\
		{						\
			for (i = 0, d = buf;			\
			     i < n && d < EOB - sizeof(ellipsecomma);\
			     i++)				\
			{					\
				d += htsnpf(d, (EOB - sizeof(ellipsecomma))\
				        - d, fmt, (fmttype)ITEM(fttype, i)); \
				/* strlst-compatible if multiple: */	\
				if (n > 1) *(d++) = TxPrefStrlstDelims[0];\
			}					\
			if (i < n)		/* short buf */	\
			{					\
				strcpy(d, ellipsecomma);	\
				d += 3;				\
			}					\
			*(d++) = '\0';				\
		}						\
	}
                case FTN_SHORT :
			FIXNUM(ft_short, fmtInt, int);
			break;
                case FTN_SMALLINT :
			FIXNUM(ft_smallint, fmtInt, int);
			break;
                case FTN_INT :
			FIXNUM(ft_int, fmtInt, int);
			break;
                case FTN_INTEGER :
			FIXNUM(ft_int, fmtInt, int);
			break;
                case FTN_LONG :
			FIXNUM(ft_long, fmtLong, long);
			break;
                case FTN_DOUBLE :                     /* MAW 06-02-94 */
			FIXNUM(ft_double, "%lg", double);
			break;
                case FTN_FLOAT :                      /* MAW 06-02-94 */
			FIXNUM(ft_float, "%lg", double);
			break;
                case FTN_DWORD :
			FIXNUM(ft_dword, fmtUnsigned, unsigned);
			break;
                case FTN_WORD :
			FIXNUM(ft_word, fmtUnsigned, unsigned);
			break;
                case FTN_DATE :                       /* MAW 06-02-94 */
			res = htsnpf(buf, EOB - buf, "%at", DateFmt,
					*(time_t *)v);
			/* If platform date/time routines cannot print it,
			 * try Texis internal:
			 */
			if (res == 5 && strnicmp(buf, "(err)", 5) == 0)
				htsnpf(buf, EOB - buf, "%at", InternalDateFmt,
					*(time_t *)v);
                        break;
		case FTN_DATE | DDVARBIT:
		{
			size_t	i;
			char	*d;

			*buf = '\0';		/* in case nothing printed */
			for (i = 0, d = buf; i < n && d < EOB; i++)
			{
				res = htsnpf(d, EOB - d,  "%at,", DateFmt,
					     (time_t)(((ft_date *)v)[i]));
				/* If platform date/time routines
				 * cannot print it, try Texis internal:
				 */
				if (res == 5 && strnicmp(buf, "(err)", 5) == 0)
					res = htsnpf(buf, EOB - d, "%at,",
						     InternalDateFmt,
						 (time_t)(((ft_date *)v)[i]));
				d += res;
			}
			if (d >= EOB)		/* ran out of space */
				strcpy(EOB - sizeof(ellipsecomma),
				       ellipsecomma);
			break;
		}
		case FTN_DATETIME:
			/* see also fodtch(): */
			dt = (ft_datetime *)v;
			nPr = htsnpf(buf, BSZ, "%04d-%02d-%02d %02d:%02d:%02d",
				(int)dt->year,
				(int)dt->month,
				(int)dt->day,
				(int)dt->hour,
				(int)dt->minute,
				(int)dt->second);
			if (nPr < BSZ && dt->fraction)
				htsnpf(buf + nPr, BSZ - nPr, ".%09d",
					(int)dt->fraction);
			break;
                case FTN_COUNTER:                     /* MAW 06-02-94 */
			TXprintHexCounter(buf, EOB - buf, (ft_counter *)v);
                        break;
		case FTN_COUNTERI:		/* see also above */
			htsnpf(buf, EOB - buf, "counter");
			break;
                case FTN_STRLST|DDVARBIT:             /* MAW 07-07-94 */
		case FTN_STRLST:
			/* Note that fldtostr() is used for some live code
			 * (wtf), not just debug/verbose/printing, so dump
			 * the strlst in a varchar-to-strlst compatible way:
			 */
                        {
                           char *s, *e, *d, *orgS;
			   FLD	tmpFld;

			   if (!f)
			   {
				   memset(&tmpFld, 0, sizeof(FLD));
				   tmpFld.type = type;
				   tmpFld.v = v;
				   tmpFld.elsz = TX_STRLST_ELSZ;
				   tmpFld.n = n;
				   tmpFld.size = size;
				   s = orgS = TXgetStrlst(&tmpFld, &slHdr);
			   }
			   else
				   s = orgS = TXgetStrlst(f, &slHdr);
			   e = s + slHdr.nb;
			   if (e > s && !e[-1]) e--;	/* ign. strlst term.*/
			   for (d = buf;
				s < e && d < EOB - 5 /* -5: "..."+delim+\0 */;
				s++, d++)
                           {
                              if(*s=='\0') *d=slHdr.delim;
                              else         *d= *s;
                           }
			   if (s < e)		/* exceeds buffer size */
			   {
				strcpy(d, "...");
				d += 3;
				*(d++) = slHdr.delim;
			   }
			   else if (e > orgS && s[-1] != '\0')
			   {			/* unterminated last item */
				   *(d++) = slHdr.delim;
			   }
			   *d = '\0';
                        }
                        break;
                case FTN_LONG    |DDVARBIT:           /* JMT 96-02-19 */
		{
			size_t	i;
			char	*d;

			for (i = 0, d = buf;
			     i < n && d < EOB - (EPI_OS_LONG_BITS/3 + 4);
			     i++)
			{
				htsnpf(d, EOB - buf, fmtCommaLong,
					(long)(((ft_long *)v)[i]));
				d += strlen(d);
			}
			if (d == buf) d++;		/* nothing printed */
			if (i < n)			/* ran out of space */
			{
				htsnpf(d, EOB - buf, "...");
				d += 3;
			}
			*(d++) = ')';
			*(d++) = '\0';
			*buf = '(';
			break;
		}
#define VARNUM(fttype, fmt, fmttype)					\
	{								\
		size_t	i;						\
		char	*d;						\
		TMP_DECL(fttype)					\
									\
		if (v == NULL)						\
			/* Should not happen: TXfldIsNull() check above: */ \
			htsnpf(buf, EOB - buf, nullstr);		\
		else							\
		{							\
			for (i = 0, d = buf;				\
			     i < n && d < EOB - sizeof(ellipsecomma);	\
			     i++)					\
			{						\
				d += htsnpf(d, (EOB - sizeof(ellipsecomma))\
						- d, fmt,		\
					    (fmttype)ITEM(fttype, i));	\
			}						\
			if (i < n)			/* short buf */	\
			{						\
				strcpy(d, ellipsecomma);		\
				d += 3;					\
			}						\
			*(d++) = '\0';					\
		}							\
	}
		case FTN_INT | DDVARBIT:
			VARNUM(ft_int, fmtIntComma, int);
			break;
		case FTN_SHORT   |DDVARBIT:
			VARNUM(ft_short, fmtIntComma, int);
			break;
		case FTN_SMALLINT|DDVARBIT:
			VARNUM(ft_smallint, fmtIntComma, int);
			break;
		case FTN_INTEGER |DDVARBIT:
			VARNUM(ft_integer, fmtIntComma, int);
			break;
		case FTN_DOUBLE  |DDVARBIT:
			VARNUM(ft_double, "%g,", double);
			break;
		case FTN_FLOAT   |DDVARBIT:
			VARNUM(ft_float, "%g,", double);
			break;
		case FTN_DWORD   |DDVARBIT:
			VARNUM(ft_dword, fmtUnsignedComma, unsigned);
			break;
		case FTN_WORD    |DDVARBIT:
			VARNUM(ft_word, fmtUnsignedComma, unsigned);
			break;
		case FTN_INT64:
			FIXNUM(ft_int64, "%wd", EPI_HUGEINT);
			break;
		case FTN_UINT64:
			FIXNUM(ft_uint64, "%wu", EPI_HUGEUINT);
			break;
		case FTN_INT64 | DDVARBIT:
			VARNUM(ft_int64, "%wd,", EPI_HUGEINT);
			break;
		case FTN_UINT64 | DDVARBIT:
			VARNUM(ft_uint64, "%wu,", EPI_HUGEUINT);
			break;
		case FTN_HANDLE:
			/* Width 4 for negative sign: */
#define RANK_DEC_FMT		\
		(TXApp && TXApp->legacyVersion7OrderByRank ? "%4wd" : "%3wd")
#define RANK_DEC_FMT_VAR	\
		(TXApp && TXApp->legacyVersion7OrderByRank ? "%4wd," : "%3wd,")
			/* We want the correct number of leading `ffff...'
			 * digits for negative off_t, so pick exact-size type:
			 */
#if TX_FT_HANDLE_BITS == EPI_HUGEINT_BITS
			FIXNUM(ft_handle, (TXfldtostrHandleBase10 > 0 ?
			    RANK_DEC_FMT : "0x%08wx"), EPI_HUGEINT);
#elif TX_FT_HANDLE_BITS == EPI_OS_INT_BITS
			FIXNUM(ft_handle, (TXfldtostrHandleBase10 > 0 ?
			    RANK_DEC_FMT : "0x%08wx"), EPI_HUGEINT);
#else
#  error Need right-sized type
#endif
			break;
		case FTN_HANDLE | DDVARBIT:
#if TX_FT_HANDLE_BITS == EPI_HUGEINT_BITS
			VARNUM(ft_handle, (TXfldtostrHandleBase10 > 0 ?
			    RANK_DEC_FMT_VAR : "0x%08wx,"), EPI_HUGEINT);
#elif TX_FT_HANDLE_BITS == EPI_OS_INT_BITS
			VARNUM(ft_handle, (TXfldtostrHandleBase10 > 0 ?
			    RANK_DEC_FMT_VAR : "0x%08wx,"), EPI_HUGEINT);
#else
#  error Need right-sized type
#endif
#undef RANK_DEC_FMT
#undef RANK_DEC_FMT_VAR
			break;
		case FTN_BLOBI:
                case FTN_BLOBI   |DDVARBIT:           /* MAW 06-02-94 */
		{
			size_t sz;
			char *x;
			ft_blobi	*blobi = (ft_blobi *)v;

			x = TXblobiGetPayload(blobi, &sz);
			/* Data is either owned by `blobi' or constant,
			 * and is nul-terminated in either case:
			 */
			return(x ? x : "");
		}
		case FTN_BYTE:
		case FTN_BYTE | DDVARBIT:
		{
			const char	hexChars[] = "0123456789abcdef";
			byte		*s, *sEnd;
			char		*d, *eob;

			if (TXApp && TXApp->hexifyBytes)
			{
				/* Copy an integral # of bytes to `buf': */
				eob = buf + ((EOB - buf) / 2)*2;
				for (s = (byte *)v, sEnd = s + n, d = buf;
				     s < sEnd && d < eob;
				     s++)
				{
					*(d++) = hexChars[*s >> 4];
					if (d < eob)
						*(d++) = hexChars[*s & 0xf];
				}
			}
			else
			{
				/* Return byte data as-is if no nuls: */
				s = (byte *)memchr(v, '\0', n);
				if (!s) return(v);
				/* Replace nuls with `.' so we can see
				 * all of the data.  This differs from
				 * byte -> char conversion (stops at nul):
				 */
				eob = EOB;
				for (s = (byte *)v, sEnd = s + n, d = buf;
				     s < sEnd && d < eob;
				     s++, d++)
					*d = (*s ? *s : '.');
			}
			/* Add ellipsis if too long: */
			if (d >= eob)
				strcpy(eob - 4, "...");
			else
				*d = '\0';
			break;
		}
                case FTN_DECIMAL :                    /* MAW 06-02-94 */
                case FTN_BLOB    :                    /* MAW 06-02-94 */
                case FTN_DECIMAL |DDVARBIT:           /* MAW 06-02-94 */
                case FTN_COUNTER |DDVARBIT:           /* MAW 06-02-94 */
                case FTN_BLOB    |DDVARBIT:           /* MAW 06-02-94 */
                case FTN_BLOBZ   |DDVARBIT:           /* MAW 06-02-94 */
                        htsnpf(buf, EOB - buf, "Non-displayable type %d (%s)",
				(int)type, ddfttypename(type));
                        break;
		case FTN_INTERNAL:
		case FTN_INTERNAL|DDVARBIT:
			return((char *)tx_fti_obj2str((ft_internal *)v));
                default:
                        htsnpf(buf, EOB - buf, "Unknown type %d",
				(int)type);
                        break;
        }
	ret = buf;
	buf += strlen(buf) + 1;			/* +1: skip nul */
        return ret;
#undef TMP_DECL
#undef ITEM
}

