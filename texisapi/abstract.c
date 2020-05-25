#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>				/* for sqrt() */
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "dbquery.h"
#include "texint.h"
#include "unicode.h"				/* for TX_MAX_UTF8_BYTE_LEN */
#include "http.h"

#ifndef FFSPN
#  define FFSPN	((FFS *)NULL)
#endif /* !FFSPN */

/* play it safe with UTF-8/hi-bit chars: */
#define ATSPACE(s)  (*((byte *)(s)) <  0x80 && isspace((int)*((byte *)(s))))
#define ATALNUM(s)  (*((byte *)(s)) >= 0x80 || isalnum((int)*((byte *)(s))))

#define ATASCIIALPHA(s)	\
	((*(s) >= 'a' && *(s) <= 'z') || (*(s) >= 'A' && *(s) <= 'Z'))

typedef enum HAL_tag				/*hit alignment within locus*/
{
	HAL_LEFT,
	HAL_CENTER,
	HAL_RIGHT
}
HAL;

typedef enum LF_tag				/* locus flags */
{
	LF_URLSPLIT	= (1 << 0),		/* a URL-split locus */
	LF_INURL	= (1 << 1),		/* hit was in a URL */
	LF_TEXTISURL	= (1 << 2),		/* entire text is URL */
	LF_ISHTTP	= (1 << 3)		/* is HTTP */
}
LF;

/* A locus is a contiguous chunk of text (i.e. between ellipses) in the
 * abstract.  It is generally centered on a raw query-term hit:
 */
typedef struct LOCUS_tag
{
	CONST char	*start;			/* start of locus */
	CONST char	*end;			/* end of locus */
	CONST char	*expnLeftLimit;		/* (opt.) expansion limit */
	/* Start/end of query hit (may be inside or outside locus bounds): */
	CONST char	*hitStart;
	CONST char	*hitEnd;
	HAL		hitAlignment;
	LF		flags;
	size_t		nSets;			/* # sets (usually 1) */
	size_t		nSetHits;		/* # *all* hits */
}
LOCUS;
#define LOCUSPN		((LOCUS *)NULL)

/* `IsUrlChar[n]' is non-zero if character n is considered valid in a URL.
 * No parens since they are more likely surrounding a URL that part of it:
 */
static CONST byte	IsUrlChar[256] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static CONST char	Ellipsis[] = "...";
#define ELLIPSIS_SZ	(sizeof(Ellipsis) - 1)

/******************************************************************/

static FFS *abrex = (FFS *) NULL;

/******************************************************************/

int TXcloseabsrex(void);

int
TXcloseabsrex(void)
{
	if(abrex)
		abrex = closerex(abrex);
	return 0;
}

/******************************************************************/

static byte *findabs ARGS((char *));

static byte *
findabs(s)
char *s;
{
	byte *p, *where = BPNULL;
	byte *end = (byte *) s + strlen(s);
	int new = 1, size, maxsize = 0;

	if (abrex == (FFS *) NULL &&
	    (abrex = openrex((byte *) ">>\\upper=\\lower{2,}\\x20=[\\alnum&\\x20,'\\-\"/:;()]{15,}[.!?]=[\\alnum&\\space,'\\-\".?!/:;()]{12,}", TXrexSyntax_Rex)) == (FFS *) NULL
		)
		return NULL;

	while (maxsize < 60 && (p = getrex(abrex, (byte *) s, end, new)) != NULL)
	{
		new = 0;
		if ((size = rexsize(abrex)) > maxsize)
		{
			where = p;
			maxsize = size;
		}
	}
	/* closerex(abrex);  not performed! */
	return where;
}

/******************************************************************/

static DDMMAPI *findmmapi ARGS((PRED *, char *));

static
DDMMAPI *
findmmapi(pred, query)
PRED *pred;
char *query;
{
	DDMMAPI	*ddmmapi;

	if(!pred)
		return NULL;
	if(TXismmop(pred->op, NULL) && pred->rt == FIELD_OP)
	{
		ddmmapi = getfld(pred->right, NULL);
		if(!strcmp(query, ddmmapi->query) && ddmmapi->bt)
			return ddmmapi;
	}
	if(pred->lt == 'P')
	{
		ddmmapi = findmmapi(pred->left, query);
		if(ddmmapi)
			return ddmmapi;
	}
	if(pred->rt == 'P')
	{
		ddmmapi = findmmapi(pred->right, query);
		if(ddmmapi)
			return ddmmapi;
	}
	return NULL;
}

/******************************************************************/

static byte *findrankabs ARGS((char *, char *, DBTBL *, char **idxExprs,
				char *locale, FDBIHI ***hits, int *nHitsP));

static byte *
findrankabs(text, query, table, idxExprs, locale, hits, nHitsP)
char *text;
char *query;
DBTBL *table;
char	**idxExprs;	/* (in, opt.) index exprs to use instead of global */
char	*locale;	/* (in, opt.) locale to use w/"" instead of global */
FDBIHI	***hits;	/* (out, opt.) array of best set hits */
int	*nHitsP;	/* (out, opt.) length of `*hits' */
{
	DDMMAPI *ddmmapi;
	size_t sz = strlen(text), off, median = (size_t)(-1);
	RPF	oldFlags = (RPF)0;
	RPPM	*r = RPPMPN;
	static RPPM *lastRppm = RPPMPN;
	static MMAPI *mmapi = NULL;
	static APICP *cp = NULL;
	static MMQL *mq = NULL;
	static char *lquery = NULL;

	if (hits) *hits = FDBIHIPPN;
	if (nHitsP) *nHitsP = 0;
	if(table)
		ddmmapi = findmmapi(table->pred, query);
	else
		ddmmapi = NULL;
	if(ddmmapi)
	{
		r = ((PROXBTREE *)ddmmapi->bt)->r;
		/* We want byte offset info from rppm_rankbuf(),
		 * but we do not want to disturb existing `r' flags:
		 */
		oldFlags = (TXrppmGetFlags(r) & (RPF_SAVEBYTEHITS |
						 RPF_SAVEBESTHITS));
		rppm_setflags(r, RPF_SAVEBYTEHITS, 1);
		/* Optimization: only save best hits if requested;
		 * may save a little time in rppm_rankbuf():
		 */
		if (hits) rppm_setflags(r, RPF_SAVEBESTHITS, 1);
		TXsetrecid(&r->curRecid, 0x0);	/* for RPPM tracing */
		rppm_rankbuf(r, ddmmapi->mmapi, (byte *)text,
                             (byte *)text + sz, &median);
		/* Index exprs should already have been set in fdbi_get() */
		/* restore flags: */
		rppm_setflags(r, RPF_SAVEBYTEHITS,
			((oldFlags & RPF_SAVEBYTEHITS) ? 1 : 0));
		rppm_setflags(r, RPF_SAVEBESTHITS,
			((oldFlags & RPF_SAVEBESTHITS) ? 1 : 0));
	}
	else
	{
		r = lastRppm;
		if(!r || !lquery || strcmp(query, lquery))
		{
			if(cp)
				cp = closeapicp(cp);
			cp = dupapicp(globalcp);

			if(mmapi)
				mmapi = closemmapi(mmapi);
                        /* wtf pass on proper `isRankedQuery' here?
                         * i.e. arg to abtract(), with a default of
                         * last-LIKE-operator-used?
                         */
			mmapi = openmmapi(query, TXbool_False, cp);
			if(!mmapi)
			{
				return NULL;
			}

			if(mq)
				mq = TXclosemmql(mq, 0);
			mq = mmrip(mmapi, 0);

			if (lastRppm)
				lastRppm = closerppm(lastRppm);

			r = openrppm(mmapi, mq, FOP_PROXIM, FDBIPN, 0);

			if(lquery)
				free(lquery);
			lquery=strdup(query);

			if (r != RPPMPN)
			{
				lastRppm = r;
				rppm_setflags(r, RPF_SAVEBYTEHITS, 1);
				/* Optimization: only save best hits if
				 * requested; may save a little time in
				 * rppm_rankbuf():
				 */
				if (hits)
					rppm_setflags(r, RPF_SAVEBESTHITS, 1);
				/* Use global index expressions if none
				 * given; smarter than none at all:
				 */
				if (idxExprs == CHARPPN)
					idxExprs = TXgetglobalexp();
				/* No point in getting current locale
				 * if `locale' is NULL; will be used anyway
				 */
				if (idxExprs != CHARPPN)
				{
					TXrppmSetIndexExprs(r,idxExprs,locale);
					rppm_setflags(r, RPF_USEINDEXEXPRS, 1);
				}
			}
		}
		if (r == RPPMPN) return(NULL);
		TXsetrecid(&r->curRecid, 0x0);	/* for RPPM tracing */
		rppm_rankbuf(r, mmapi, (byte *)text, (byte *)text + sz,
                             &median);
	}

	if (hits && nHitsP && r) *nHitsP = TXrppmGetBestHitInfo(r, hits);

	if(median != (size_t)(-1))
	{
		off = median;
		if (off > sz) off = sz;
		return (byte *)text + off;
	}

	return NULL;
}

/* ------------------------------------------------------------------------ */

static size_t
txAddTextPtr(CONST char **sp,			/* (in/out) pointer to move */
		size_t sz,			/* (in) min. size to add */
		CONST char *limit)		/* (in) limit to expansion */
/* Adds at least `sz' chars to `*sp', rubber-banding whitespace in `*sp'
 * (i.e. contiguous whitespace in `*sp' counts as 1 char against `sz').
 * Will not go past `limit'.  See also txSubTextPtr(), txDiffTextPtrs(),
 * txAlignAndPrintLoci().
 * Returns amount of `sz' actually moved.
 */
{
	CONST char	*s = *sp;
	size_t		orgSz = sz;

	for ( ; s < limit && sz > 0; sz--)
	{
		if (ATSPACE(s))
			for (s++; s < limit && ATSPACE(s); s++);
		else
			s++;
	}
	*sp = s;
	return(orgSz - sz);
}

static size_t
txSubTextPtr(CONST char **sp,			/* (in/out) pointer to move */
		size_t sz,			/* (in) min. sz to subtract */
		CONST char *limit)		/* (in) limit to expansion */
/* Subtracts at least `sz' chars from `*sp', rubber-banding whitespace
 * (i.e. contiguous whitespace in `*sp' counts as 1 char against `sz').
 * Will not go past `limit'.  See also txAddTextPtr(), txDiffTextPtrs(),
 * txAlignAndPrintLoci().
 * Returns amount of `sz' actually moved.
 */
{
	CONST char	*s = *sp;
	size_t		orgSz = sz;

	for ( ; s > limit && sz > 0; sz--)
	{
		s--;
		if (ATSPACE(s))
		{
			for (s--; s >= limit && ATSPACE(s); s--);
			s++;			/* back to start of wspace */
		}
	}
	*sp = s;
	return(orgSz - sz);
}

static size_t
txDiffTextPtrs(CONST char *a,			/* (in) right pointer */
		CONST char *b)			/* (in) left pointer */
/* Returns `a' - `b', with contiguous whitespace counting as 1.
 */
{
	return(txAddTextPtr(&b, EPI_OS_SIZE_T_MAX, a));
}

static void
txAlignLocus(
	LOCUS *locus,			/* (in/out) locus to align */
	int absStyle,			/* (in) TXABS_STYLE_... */
	CONST char *prevEnd,		/* (in) end of previous locus */
	CONST char *nextStart,		/* (in) start of next locus */
	CONST char *textStart,		/* (in) overall text start */
	CONST char *textEnd)		/* (in) overall text end */
/* Aligns locus to word boundaries if possible, by shrinking if needed.
 * Will not split UTF-8 sequences.  Tries to avoid splitting words or hit.
 * Tries to slide locus left/right (within `prevEnd'/`nextStart') to start
 * at a sentence.
 * Thread-unsafe (static vars).
 */
{
	static CONST char * CONST	sentStartExprs[] =
	{					/* start-of-sentence exprs */
	/* Upper-case letter after a likely sentence end: */
	"[^\\digit\\upper][.?!]=['\"]{,3}\\space\\P+>>\\upper=",
	/* Upper-case letter that starts a paragraph: */
	"\\x0D?\\x0A=\\x0D?\\x0A=[\\x20\\x09]\\P{,10}['\"]{,3}>>\\upper=",
	/* Upper-case letter at buffer start: */
	"=\\space\\P*>>\\upper=",
	"",
	CHARPN
	};
	static CONST char * CONST	sentEndExprs[] =
	{					/* end-of-sentence exprs */
	">>[^\\digit\\upper][.?!]=['\"]\\P{,3}\\space+\\upper=",
	"",
	CHARPN
	};
	static RLEX	*sentStartRlex = RLEXPN, *sentEndRlex = RLEXPN;
	size_t		locusSz;
	char		embeddedBreakChars[16];	/* embedded word-break chars*/
	char		*d;
	CONST char	*s, *orgS, *locusStart = locus->start;
	CONST char	*locusEnd = locus->end, *bestSentStart, *bestSentEnd;
	CONST char	*searchStart, *searchEnd, *okStart, *okEnd;
	int		i;
#define MAX_WORD_SZ	20			/* max typical word size */

	if (locusEnd > textEnd) locusEnd = locus->end = textEnd;  /* sanity */

	/* Allow certain chars to start/end a word, if followed by
	 * non-space.  E.g. allow `/', so that `foo/bar/baz' can snap
	 * to `.../bar/...' and not `...bar...' in a path or URL:
	 */
	d = embeddedBreakChars;
	*(d++) = '/';
	*(d++) = '\\';
	if (locus->flags & LF_INURL)
	{
		/* These are allowed only in a URL, so we do not otherwise
		 * break up `AT&T' etc.:
		 */
		*(d++) = '&';
		*(d++) = ';';
		*(d++) = '?';
	}
	*d = '\0';				/* terminate the list */

	/* Slide locus left or right to start at a sentence, if possible.
	 * We only do this for TXABS_STYLE_QUERYMULTIPLE style and above,
	 * since TXABS_STYLE_DUMB always starts at text start,
	 * TXABS_STYLE_SMART does it own start detection, and
	 * TXABS_STYLE_QUERYSINGLE is legacy (predates sentence-snap):
	 */
	switch (absStyle)
	{
	case TXABS_STYLE_DUMB:
	case TXABS_STYLE_SMART:
	case TXABS_STYLE_QUERYSINGLE:
		break;
	default:
		if (locusStart <= textStart) break;/* doc start is better */
		/* Find sentence start closest to current locus start,
		 * that still contains hit and does not make locus overlap.
		 * Could BSEARCHNEWBUF back from locus start, then
		 * SEARCHNEWBUF forward from locus start, but might not
		 * detect sentence start *at* locus start, because search
		 * buffers' start/end would be in the middle of most
		 * sentence expressions.  So search forwards only,
		 * from left-most possible, and track the best hit:
		 */
		if (locusEnd >= locus->hitEnd)
		{
			okStart = locusStart;
			txSubTextPtr(&okStart,
				     txDiffTextPtrs(locusEnd, locus->hitEnd),
				     prevEnd);
		}
		else
			okStart = locusStart;
		if (okStart < prevEnd) okStart = prevEnd;
		if (locus->expnLeftLimit != CHARPN &&
		    okStart < locus->expnLeftLimit)
			okStart = locus->expnLeftLimit;
		if (locus->hitStart > locusStart)
			okEnd = locus->hitStart;
		else
			okEnd = locusStart;
		locusSz = txDiffTextPtrs(locusEnd, locusStart);
		s = okEnd;
		if (txAddTextPtr(&s, locusSz, nextStart) < locusSz)
		{
			okEnd = nextStart;
			txSubTextPtr(&okEnd, locusSz, prevEnd);
		}
		if (okStart >= okEnd) break;
		/* Let actual search buffer extend a few bytes farther,
		 * so end-of-previous-sentence subexpression parts can match:
		 */
		searchStart = okStart - 5;
		if (searchStart < textStart) searchStart = textStart;
		searchEnd = okEnd + 5;
		if (searchEnd > textEnd) searchEnd = textEnd;
		/* Open the REX expressions, if not already open: */
		if (sentStartRlex == RLEXPN &&
		    (sentStartRlex =openrlex((const char **)sentStartExprs,
                                             TXrexSyntax_Rex)) == RLEXPN)
			break;			/* N/A or error */
		/* Scan for best sentence start (closest to locus start): */
		bestSentStart = CHARPN;
		for (s = (CONST char *)getrlex(sentStartRlex,
			(byte *)searchStart, (byte *)searchEnd, SEARCHNEWBUF);
		     s != CHARPN;
		     s = (CONST char *)getrlex(sentStartRlex,
			(byte*)searchStart, (byte*)searchEnd, CONTINUESEARCH))
		{
			if (bestSentStart == CHARPN || s <= locusStart)
				bestSentStart = s;
			if (s >= locusStart) break;	/*best already found*/
		}
		s = bestSentStart;
		if (s != CHARPN && s >= okStart && s <= okEnd)
		{				/* ok to use `s' */
			locusEnd = s;
			txAddTextPtr(&locusEnd, locusSz, nextStart);
			locus->end = locusEnd;
			locusStart = locus->start = s;
			break;
		}

		/* Sentence start not found.  Check for sentence end
		 * and align locus end to that:
		 */
		okStart = locus->hitEnd;
		s = prevEnd;
		txAddTextPtr(&s, locusSz, nextStart);
		if (okStart < s) okStart = s;
		if (locus->expnLeftLimit != CHARPN)
		{
			s = locus->expnLeftLimit;
			txAddTextPtr(&s, locusSz, nextStart);
			if (okStart < s) okStart = s;
		}
		/* 15-char fudge factor: leave some text at locus start
		 * before hit, for some readability context, since we know
		 * hit is not starting a sentence:
		 */
		s = locusStart;
		txAddTextPtr(&s, 15, nextStart);
		if (locus->hitStart > s)
		{
			okEnd = locusEnd;
			txAddTextPtr(&okEnd,
				     txDiffTextPtrs(locus->hitStart, s),
				     nextStart);
		}
		else
			okEnd = locusEnd;
		s = okEnd;
		if (txAddTextPtr(&s, locusSz, nextStart) < locusSz)
		{
			okEnd = nextStart;
			txSubTextPtr(&okEnd, locusSz, prevEnd);
		}
		if (okStart >= okEnd) break;
		/* Let actual search buffer extend a few bytes farther,
		 * so end-of-previous-sentence subexpression parts can match:
		 */
		searchStart = okStart - 5;
		if (searchStart < textStart) searchStart = textStart;
		searchEnd = okEnd + 5;
		if (searchEnd > textEnd) searchEnd = textEnd;
		/* Open the REX expressions, if not already open: */
		if (sentEndRlex == RLEXPN &&
		    (sentEndRlex = openrlex((const char **)sentEndExprs,
                                            TXrexSyntax_Rex)) == RLEXPN)
			break;			/* N/A or error */
		/* Scan for best sentence end (closest to locus end): */
		bestSentEnd = CHARPN;
		for (s = (CONST char *)getrlex(sentEndRlex,
			(byte *)searchStart, (byte *)searchEnd, SEARCHNEWBUF);
		     s != CHARPN;
		     s = (CONST char *)getrlex(sentEndRlex,
			(byte*)searchStart, (byte*)searchEnd, CONTINUESEARCH))
		{
			if (bestSentEnd == CHARPN || s <= locusEnd)
				bestSentEnd = s;
			if (s >= locusEnd) break;	/*best already found*/
		}
		s = bestSentEnd;
		if (s != CHARPN && s >= okStart && s <= okEnd)
		{				/* ok to use `s' */
			locusEnd = locus->end = s;
			txSubTextPtr(&s, locusSz, prevEnd);
			locusStart = locus->start = s;
			break;
		}

		break;
	}

	/* Align start: - - - - - - - - - - - - - - - - - - - - - - - - - - */
	/* Forward to start of a word (but not too far; do not shrink much):
	 * Also let `embeddedBreakChars' start a word, if followed by
	 * non-space.  This allows path/URL elements to be words, instead
	 * of whole URL:
	 */
#define FRAG_START(s)	\
  ((s) + 1 < textEnd && !ATSPACE((s) + 1) && strchr(embeddedBreakChars, *(s)))
	if (locusStart < textEnd && !ATSPACE(locusStart) &&
	    (locusStart == textStart || ATSPACE(locusStart - 1) ||
	     FRAG_START(locusStart)))
		;				/* already at word start */
	else					/* not already at word start*/
	{
		for ( ;
		     locusStart < locusEnd && locusStart < locus->hitStart &&
			(locusStart - locus->start) < MAX_WORD_SZ &&
			!ATSPACE(locusStart) && !FRAG_START(locusStart);
		     locusStart++);		/* skip to end of word */
		for ( ;
		     locusStart < locusEnd && locusStart < locus->hitStart &&
			ATSPACE(locusStart);
		     locusStart++);		/* skip to next word start */
	}
	/* If start of word not found, leave start where it was.
	 * E.g. if locus (or locus start) is entirely inside 1 large word,
	 * do not trim the word down (to possible nothing):
	 */
	if (locusStart != locus->start && !ATSPACE(locusStart - 1) && !FRAG_START(locusStart))
		locusStart = locus->start;

	/* Skip past UTF-8 char if in the middle of one, to avoid
	 * returning invalid UTF-8 due to splitting a UTF-8 sequence.
	 * (It is unlikely that we're in one, as we should be at a space
	 * or end of buffer, but hit could be a mid-sequence REX expression.)
	 */
	if (locusStart < textEnd && (*locusStart & 0xC0) == 0x80)
	{					/* possible mid-UTF-8-char */
		/* Check that we are at a true UTF-8 char, to try to avoid
		 * unneeded skip, e.g. for ISO-8859-1 hi-bit chars:
		 */
		for (s = locusStart, i = 0;
		     s > textStart && i < TX_MAX_UTF8_BYTE_LEN - 1 &&
			(*(byte *)s & 0xC0) == 0x80;
		     s--, i++);			/* back up to char start */
		i = TXunicodeDecodeUtf8Char(&s, textEnd, 0);
		if (i >= 0)			/* valid UTF-8 char */
		{				/*   skip forward over it */
			locusStart = s;
			if (locusStart > locusEnd)
				locusStart = locusEnd;
		}
	}

	/* Align end: - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	if (locusEnd == textEnd)		/* 1st maybe get inside buf */
	{
		if (locusEnd == locusStart || !ATSPACE(locusEnd - 1))
			goto done;
		locusEnd--;			/* move into buffer */
	}

	/* Backward to end of a word (but not too far).
	 * Also let `embeddedBreakChars' end a word, if preceded by non-space.
	 * This allows path/URL elements to be words, instead of whole URL:
	 */
#define FRAG_END(s)	((s) > textStart + 1 && !ATSPACE((s) - 2) &&	\
		strchr(embeddedBreakChars, (s)[-1]))
	for ( ;
	     locusEnd > locusStart && locusEnd > locus->hitEnd &&
		(locus->end - locusEnd) < MAX_WORD_SZ &&
		!ATSPACE(locusEnd) && !FRAG_END(locusEnd);
	     locusEnd--);
	for ( ;
	     locusEnd > locusStart && locusEnd > locus->hitEnd &&
		ATSPACE(locusEnd - 1);
	     locusEnd--);
	/* If end-of-word not found, restore locus end (e.g. if locus
	 * is entirely within 1 large word, do not trim down to nothing):
	 */
	if (!ATSPACE(locusEnd) && !FRAG_END(locusEnd))
		locusEnd = locus->end;

	/* Back up over UTF-8 char, if in the middle of one: */
	if ((*locusEnd & 0xC0) == 0x80)
	{					/* middle of a UTF-8 char */
		for (s = locusEnd, i = 0;
		     s > textStart && i < TX_MAX_UTF8_BYTE_LEN - 1 &&
			(*(byte *)s & 0xC0) == 0x80;
		     s--, i++);			/* back up to char start */
		orgS = s;
		i = TXunicodeDecodeUtf8Char(&s, textEnd, 0);
		if (i >= 0)			/* valid UTF-8 char */
		{				/*   skip backward over it */
			locusEnd = orgS;
			if (locusEnd < locusStart)
				locusEnd = locusStart;
		}
	}

done:
	locus->start = locusStart;
	locus->end = locusEnd;
#undef FRAG_START
#undef FRAG_END
#undef MAX_WORD_SZ
}

/* ------------------------------------------------------------------------ */

static int
txBestLociCmp(CONST void *a, CONST void *b)
/* Comparison function for qsort(), when selecting "best" loci.
 */
{
	LOCUS	*locusA = (LOCUS *)a, *locusB = (LOCUS *)b;

	/* Loci with more distinct sets are preferred, as more distinct
	 * terms can then be highlighted later.  Note that this also
	 * prefers "real" loci (with hits) over URL-split loci:
	 */
	if (locusB->nSets > locusA->nSets) return(1);
	if (locusB->nSets < locusA->nSets) return(-1);
	/* Then loci with more document hits, as they likely ranked higher: */
	if (locusB->nSetHits > locusA->nSetHits) return(1);
	if (locusB->nSetHits < locusA->nSetHits) return(-1);
	/* Then loci closest to start of doc (for rank and/or readability): */
	if (locusA->hitStart > locusB->hitStart) return(1);
	if (locusA->hitStart < locusB->hitStart) return(-1);
	return(0);
}

static int
txHitOrderLociCmp(CONST void *a, CONST void *b)
/* Comparison function for qsort(), when restoring order by hit position.
 */
{
	LOCUS	*locusA = (LOCUS *)a, *locusB = (LOCUS *)b;

	if (locusA->hitStart > locusB->hitStart) return(1);
	if (locusA->hitStart < locusB->hitStart) return(-1);
	if (locusA->hitEnd > locusB->hitEnd) return(1);
	if (locusA->hitEnd < locusB->hitEnd) return(-1);
	return(0);
}

static int
txLocusOrderLociCmp(CONST void *a, CONST void *b)
/* Comparison function for qsort(), when restoring order by locus position.
 */
{
	LOCUS	*locusA = (LOCUS *)a, *locusB = (LOCUS *)b;

	if (locusA->start > locusB->start) return(1);
	if (locusA->start < locusB->start) return(-1);
	if (locusA->end > locusB->end) return(1);
	if (locusA->end < locusB->end) return(-1);
	return(0);
}

/************************************************************************/

int
TXstrToAbs(s)
CONST char *s;
/* Returns TXABS_STYLE_... mode for string `s'.
 */
{
	if (s == CHARPN)
		return TXABS_STYLE_SMART;
	if (strcmpi(s, "dumb") == 0)
		return TXABS_STYLE_DUMB;
	if (strcmpi(s, "querysingle") == 0 ||
	    strcmpi(s, "query:single") == 0)
		return TXABS_STYLE_QUERYSINGLE;
	if (strcmpi(s, "querymultiple") == 0 ||
	    strcmpi(s, "query:multiple") == 0)
		return TXABS_STYLE_QUERYMULTIPLE;
	if (strcmpi(s, "querybest") == 0)	/* alias for best mode */
		return TXABS_STYLE_QUERYBEST;
	if (*s >= '0' && *s <= '9')
		return atoi(s);
	return TXABS_STYLE_SMART;
}

/******************************************************************/

static int
txExpandToUrl(CONST char *hit,			/* (in) hit */
	CONST char *textStart,			/* (in) overall text start */
	CONST char *textEnd,			/* (in) overall text end */
	CONST char **urlStart,			/* (out) start of URL */
	CONST char **urlHostEnd,		/* (out) `http://host/' end */
	CONST char **urlTrailStart,		/* (out) trailing-part start*/
	CONST char **urlEnd)			/* (out) end of URL */
/* Expands `hit' to `url...' vars (if in URL) and returns 1;
 * else sets to NULL and returns 0.
 */
{
	CONST char	*s;
	size_t		i;

	if (!IsUrlChar[*(byte *)hit]) goto err;	/* not in a URL */

	/* Look backwards from `hit' to start of URL: */
	for (s = hit; s > textStart && IsUrlChar[((byte *)s)[-1]]; s--);
	/* Look right from there for `\alpha+://=': */
	*urlStart = s;
	for (i = 0; i < 10 && s < textEnd && ATASCIIALPHA(s); i++, s++);
	if (i < 3 || i >= 10 || s + 3 > textEnd ||
	    *s != ':' || s[1] != '/' || s[2] != '/')
		goto err;
	s += 3;					/* skip `://' */
	/* Look right from there for `host/': */
	for ( ; s < textEnd && (ATASCIIALPHA(s) || *s == '.'); s++);
	if (s < textEnd && *s == '/') s++;
	*urlHostEnd = s;
	/* Look right from `*urlHostEnd' for URL end: */
	for (s = *urlHostEnd; s < textEnd && IsUrlChar[*(byte *)s]; s++);
	*urlEnd = s;
	/* Look left from there for `.../foo.txt', `...?foo=bar' or `.html':*/
	if (--s < textStart) goto err;
	if (s > *urlHostEnd && (*s == '/' || *s == '?' || *s == '.')) s--;
	for ( ;
	     s > *urlHostEnd && *s != '/' && *s != '?' && *s != '.' &&
		IsUrlChar[*(byte*)s];
	     s--);
	*urlTrailStart = (s > *urlHostEnd && (*s == '/' || *s == '?' ||
			  *s == '.') ? s : *urlHostEnd);
	return(1);				/* success */
err:
	*urlStart = *urlHostEnd = *urlTrailStart = *urlEnd = CHARPN;
	return(0);				/* failure */
}

static size_t
txExpandLocus(LOCUS *locus,			/* (in/out) locus to expand*/
		size_t expnSz,			/* (in) amount to expand */
		CONST char *textStart,		/* (in) overall text start */
		CONST char *textEnd)		/* (in) overall text end */
/* Expands `locus' by up to `expnSz', according to alignment.
 * Contiguous whitespace counts as 1 char against `expnSz'.
 * Returns amount actually expanded.
 */
{
	size_t		orgExpnSz = expnSz;
	CONST char	*limit;

	switch (locus->hitAlignment)
	{
	case HAL_CENTER:
		/* Expand `locus->start' backward by up to 1/2: */
		if (locus->expnLeftLimit != CHARPN &&	/* has a limit */
		    locus->expnLeftLimit <= locus->start)	/* sanity */
			limit = locus->expnLeftLimit;
		else
			limit = textStart;
		expnSz -= txSubTextPtr(&locus->start, expnSz/2, limit);
		/* fall through: */
	case HAL_LEFT:
		expnSz -= txAddTextPtr(&locus->end, expnSz, textEnd);
		/* If we could not fully expand right, expand left more: */
		if (expnSz == 0) break;		/* all expanded */
		if (locus->hitAlignment == HAL_LEFT) break;
		/* fall through: */
	case HAL_RIGHT:
		/* Do not let a URL suffix expand left past URL start,
		 * or a deleted `http://' prefix may re-appear:
		 */
		if (locus->expnLeftLimit != CHARPN &&	/* has a limit */
		    locus->expnLeftLimit <= locus->start)	/* sanity */
			limit = locus->expnLeftLimit;
		else
			limit = textStart;
		expnSz -= txSubTextPtr(&locus->start, expnSz, limit);
		break;
	}
	return(orgExpnSz - expnSz);
}

static size_t
txShrinkLocus(LOCUS *locus,			/* (in/out) locus to shrink */
		size_t shrinkSz,		/* (in) amount to shrink */
		CONST char *textStart,		/* (in) overall text start */
		CONST char *textEnd)		/* (in) overall text end */
/* Returns amount shrunk.
 */
{
	size_t	orgShrinkSz = shrinkSz;

	(void)textStart;
	(void)textEnd;

	switch (locus->hitAlignment)
	{
	case HAL_CENTER:
		/* Shrink start forward by up to 1/2: */
		shrinkSz -= txAddTextPtr(&locus->start, shrinkSz/2,
					 locus->hitStart);
		/* Fall through: */
	case HAL_LEFT:
		/* Shrink end backward by remainder: */
		shrinkSz -= txSubTextPtr(&locus->end, shrinkSz,
					 locus->hitEnd);
		if (shrinkSz == 0) break;	/* all shrunk */
		if (locus->hitAlignment == HAL_LEFT) break;
		/* Fall through and shrink start forward some more: */
	case HAL_RIGHT:
		shrinkSz -= txAddTextPtr(&locus->start, shrinkSz,
					 locus->hitStart);
		break;
	}
	return(orgShrinkSz - shrinkSz);
}

static void
txMergeLociAux(LOCUS *leftTarget, CONST LOCUS *rightSrc)
/* Merges loci flags and some ancillary properties into `leftTarget',
 * which is assumed to be left of `rightSrc'.
 */
{
	leftTarget->nSets += rightSrc->nSets;
	leftTarget->nSetHits += rightSrc->nSetHits;
	/* A "real" hit takes precedence for alignment: */
	if (!(rightSrc->flags & LF_URLSPLIT) &&
	    (leftTarget->flags & LF_URLSPLIT))
		leftTarget->hitAlignment = rightSrc->hitAlignment;
	/* If either locus is "real" (non-URL-split), the target is real;
	 * the "real" locus' expansion limit has priority:
	 */
	if (!(rightSrc->flags & LF_URLSPLIT))
	{
		leftTarget->flags &= ~LF_URLSPLIT;
		leftTarget->expnLeftLimit = rightSrc->expnLeftLimit;
	}
	/* If either locus is in a URL, the target is in a URL: */
	if (rightSrc->flags & LF_INURL)
		leftTarget->flags |= LF_INURL;
	/* Same for LF_TEXTISURL: */
	if (rightSrc->flags & LF_TEXTISURL)
		leftTarget->flags |= LF_TEXTISURL;
	/* LF_ISHTTP set by caller */
}

static char *
txAlignAndPrintLoci(LOCUS *loci,		/* (in) loci to print */
		size_t nLoci,			/* (in) # loci */
		CONST LOCUS *orgLoci,		/* (in) original loci */
		size_t nOrgLoci,		/* (in) # original loci */
		CONST char *textStart,		/* (in) overall text start */
		CONST char *textEnd,		/* (in) overall text end */
		int absStyle)			/* (in) TXABS_STYLE... */
/* Aligns/shifts `loci' to word/sentence boundaries, and prints.
 * Returns alloc'd buffer, or NULL on error.
 */
{
	static CONST char	space[] = " ";
#define MIN_INITIAL_ELLIPSIS_DIST	2500
	HTBUF			*buf;
	char			prevCh, *ret;
	CONST char		*s, *e;
	size_t			numConsecutive, sz;
	LOCUS			*locus, *lociSrcEnd;
	CONST LOCUS		*curOrgLocus, *orgLociEnd;
	CONST char		*prevSrcEnd;
	int			justDidEllipsis;

	/* Now align loci ends (i.e. with word boundaries), and print: */
	if ((buf = openhtbuf()) == HTBUFPN) goto err;
	prevCh = '\0';
	numConsecutive = 0;
	prevSrcEnd = textStart;
	curOrgLocus = orgLoci;			/* for tracking true hits */
	justDidEllipsis = 0;
	lociSrcEnd = loci + nLoci;
	orgLociEnd = orgLoci + nOrgLoci;
	for (locus = loci; locus < lociSrcEnd; locus++)
	{
		/* Bring locus start/end inward to word/UTF-8 boundaries,
		 * but do not truncate the hit if possible: this might
		 * "steal" space from later loci, but at least this hit
		 * will get shown (for later query highlighting).  Also
		 * try to slide locus left/right so it starts a sentence:
		 */
		txAlignLocus(locus, absStyle, prevSrcEnd,
			(locus + 1 < lociSrcEnd ? locus[1].start : textEnd),
			textStart, textEnd);

		/* If locus is now empty, skip it (and any `...' separator):*/
		if (locus->end <= locus->start) continue;

		/* Print a `...' separator, if gap with previous locus: */
		sz = txDiffTextPtrs(locus->start, prevSrcEnd);
		if ((int)sz > 0)		/* gap with previous locus */
		{
			for (s = prevSrcEnd;
			     s < locus->start && ATSPACE(s);
			     s++);		/* skip space after prev. */
			/* Only print space before `...' if there was space
			 * after last locus, so if we truncated it mid-`word'
			 * we print `wo...' instead of `wo ...':
			 */
			if (s > prevSrcEnd &&	/* space after prev locus */
			    prevSrcEnd > textStart && /*there's a prev locus*/
			    !ATSPACE(prevSrcEnd - 1)) /* no sp. in prev end */
				htbuf_write(buf, space, 1);
			if (s < locus->start &&	/* non-space between loci */
			    !justDidEllipsis &&	/* did not just print `...' */
			    /* No initial ellipsis if we're close to start
			     * of text (ala Google), and not splitting a word
			     * at start of locus:
			     */
			    (prevSrcEnd > textStart ||	/* not first locus */
			     sz > MIN_INITIAL_ELLIPSIS_DIST ||
			     !ATSPACE(locus->start-1)) &&/*start is mid-word*/
			    /* No initial `...' if `http://' at text start: */
			    (locus->flags & (LF_TEXTISURL | LF_ISHTTP)) !=
				(LF_TEXTISURL | LF_ISHTTP))
			{
				htbuf_write(buf, Ellipsis, ELLIPSIS_SZ);
				justDidEllipsis = 1;
				if (locus->start > textStart &&
				    ATSPACE(locus->start - 1) &&
				    !ATSPACE(locus->start))
					htbuf_write(buf, space, 1);
			}
			prevCh = '\0';		/* reset */
			numConsecutive = 0;
		}

		/* Copy the locus, collapsing whitespace and non-alnum:
		 * wtf this may make it smaller than expected;
		 * account for this above somehow?
		 */
		for (s = locus->start; s < locus->end; s++)
		{
			for ( ;
			     curOrgLocus<orgLociEnd && curOrgLocus->hitEnd<=s;
			     curOrgLocus++);
			if (*s != prevCh) numConsecutive = 0;
			numConsecutive++;
			/* Do not delete repeating chars if inside a hit,
			 * so later highlight works.  Note that we check
			 * `orgLoci' array, in case hits were removed
			 * (too many loci) or merged (adjacent) in `loci':
			 */
			if (curOrgLocus < orgLociEnd &&
			    s >= curOrgLocus->hitStart &&
			    s < curOrgLocus->hitEnd)
				goto writeIt;
			/* Collapse whitespace, since HTML (likely display
			 * mode) will anyway.  NOTE: see also txAddTextPtr()
			 * etc. which assume this collapse will happen:
			 */
			if (ATSPACE(s))
			{
				while (s < locus->end && ATSPACE(s))
					++s;
				if (s >= locus->end)
					break;	/* MAW 10-09-95 */
				/* Only write space if it's not the leading
				 * space in the output buffer:
				 */
				if (htbuf_getdata(buf, CHARPPN, 0) > 0)
					htbuf_write(buf, space, 1);
			}
			if (ATALNUM(s)) goto writeIt;
			/* `s' is non-space, non-alnum.  Collapse long runs
			 * of a repeating byte; pares down `---------' etc.
			 * But print at least 3 of the bytes; preserves
			 * UTF-8 chars and `C++' etc.  WTF note this collapse
			 * in txAddTextPtr() etc.?:
			 */
			if (numConsecutive > TX_MAX_UTF8_BYTE_LEN - 1)
			{
				/* Replace remaining repeats with ellipsis,
				 * but only if that is shorter; else print
				 * repeats:
				 */
				for (e = s + 1;
				     e < locus->end && *e == *s;
				     e++);
				justDidEllipsis = ((size_t)(e - s) >
						   ELLIPSIS_SZ);
				if (justDidEllipsis)
				{
					htbuf_write(buf, Ellipsis, ELLIPSIS_SZ);
				}
				else
					htbuf_write(buf, s, e - s);
				s = e - 1;
				/* prevCh/numConsecutive auto-reset */
			}
			else
			{
			writeIt:
				htbuf_write(buf, s, 1);
				justDidEllipsis = 0;
			}
			prevCh = *s;
		}
		prevSrcEnd = locus->end;
	}

	/* Print a trailing `...' if non-whitespace text remains
	 * (and we printed stuff already):
	 */
	for (s = prevSrcEnd; s < textEnd && ATSPACE(s); s++);
	if (s < textEnd && !justDidEllipsis)	/* non-whitespace remains */
	{
		if (s > prevSrcEnd)		/*whitespace after locus end*/
			htbuf_write(buf, space, 1);
		htbuf_write(buf, Ellipsis, ELLIPSIS_SZ);
		justDidEllipsis = 1;
	}

	htbuf_write(buf, "", 0);		/* ensure non-NULL return */
	htbuf_getdata(buf, &ret, 0x3);
	goto done;

err:
	ret = CHARPN;
done:
	buf = closehtbuf(buf);
	return(ret);
}

char *
abstract(text, maxsz, absstyle, query, table, idxExprs, locale)
char *text;		/* (in) text to make an abstract for */
int maxsz;		/* (in, opt.) max size in bytes of abstract */
int absstyle;		/* (in) TXABS_STYLE_... value */
char *query;		/* (in, opt.) query for TXABS_STYLE_QUERY... styles */
DBTBL *table;
char	**idxExprs;	/* (in, opt.) index exprs to use instead of global */
char	*locale;	/* (in, opt.) locale to use w/"" instead of global */
/* Thread-unsafe (static vars).
 */
{
	static CONST char	fn[] = "abstract";
	static CONST char	missingByteOffsets[] =
		"Internal error: Missing byte offsets";
	static CONST char	httpPfx[] = "http://";
#define HTTP_PFX_SZ		(sizeof(httpPfx) - 1)
#define MIN_DESIRED_LOCUS_SZ	30		/* arbitrary minimum */
	static volatile TXATOMINT	gotMissingByteOffsets = 0;
	int nHits = 0, hitIdx, mergePass, reSort;
	CONST char	*s, *e;
	CONST char	*urlStart, *urlHostEnd, *urlTrailStart, *urlEnd;
	CONST char	*prevUrlStart = CHARPN, *prevUrlEnd = CHARPN;
	byte		*bp;
	int		incLocus, pfxHttp;
	CONST char	*textEnd;		/* end of `text' */
	size_t		desiredLocusSz;		/* desired locus size */
	size_t		expnSz;			/* size to expand loci */
	size_t		shrinkSz;		/* size to shrink loci */
	size_t		hitSz;			/* size of hit */
	size_t		sz, sz2, gap, diff;
	char		*rc = CHARPN;		/* return value */
	FDBIHI		**hits = FDBIHIPPN, *h;
	LOCUS		singleLocus[3];		/* 3x in case URL expands */
	LOCUS		*loci = LOCUSPN;	/* computed loci array */
	LOCUS		*orgLoci = LOCUSPN;	/* copy of all original loci*/
	LOCUS		*locus, *prevLocus, *lociSrcEnd;
	LOCUS		*newLocus;
	size_t		nLoci = 0, nOrgLoci = 0;/* # of loci */
	size_t		nUrlSplitLoci = 0;	/* # of loci split from URLs*/
	size_t		nAlignRight = 0;	/* # of HAL_RIGHT loci */
	size_t		maxLoci;		/* max loci allowed */
	TXPMBUF		*pmbuf = TXPMBUFPN;	/* wtf set someday */

	if (maxsz <= 0) maxsz = TXABS_DEFSZ;
	if (query != CHARPN && *query == '\0') query = CHARPN;

	textEnd  = text + strlen(text);

	/* Correct `absstyle' to actual style used, based on params etc.:*/
	switch (absstyle)
	{
	case TXABS_STYLE_QUERYMULTIPLE:		/* N chunks from N sets */
		break;
	case TXABS_STYLE_QUERYSINGLE:		/* 1 chunk centered on query*/
		break;
	case TXABS_STYLE_SMART:			/* "best" chunk of text */
	case TXABS_STYLE_DUMB:			/* start of text */
		/* Supplying a query upgrades non-TXABS_STYLE_QUERY_... modes
		 * to the best mode available (which uses a query).
		 * We do not upgrade QUERY modes, so that old QUERY modes
		 * can be specifically invoked for back-compatibility:
		 */
		if (query != CHARPN) absstyle = TXABS_STYLE_QUERYBEST;
		break;
	default:
		/* Also upgrade unknown mode if query supplied, else use dumb.
		 * Back-compatible behavior:
		 */
		if (query != CHARPN) absstyle = TXABS_STYLE_QUERYBEST;
		else absstyle = TXABS_STYLE_DUMB;
		break;
	}

	/* Now get abstract loci into `hits' array, according to style: */
	switch (absstyle)
	{
	case TXABS_STYLE_QUERYMULTIPLE:		/* N chunks from N sets */
		/* A query is required; if none, fall back to dumb mode,
		 * but preserve `absstyle' in case we want to split locus
		 * or do other TXABS_STYLE_QUERYMULTIPLE-style stuff later:
		 */
		if (query == CHARPN) goto doDumb;
		bp = findrankabs(text, query, table, idxExprs, locale,
				&hits, &nHits);
		if (bp == BYTEPN) goto doDumb;	/* failed: fallback to dumb */
		/* Copy `hits' to `loci'.  If there is only one set,
		 * consider adding more hits from it to generate more loci
		 * (See also size limits for reducing loci, below.):
		 */
		if (nHits == 1 && maxsz >= 144)	/* one set, decent abs. sz */
		{
			h = hits[0];
			if (maxsz < 256) nLoci = 2;
			else nLoci = ((size_t)sqrt((double)maxsz))/4 - 1;
			/* Readjust `nLoci' and starting hit to fit the set:*/
			hitIdx = h->curHit;
			if (nLoci > h->nhits) nLoci = h->nhits;
			if (hitIdx + nLoci > h->nhits) hitIdx =h->nhits-nLoci;
			/* Copy hits from the set.  Alloc 3x needed in case
			 * future URL expansion:
			 */
			loci = (LOCUS *)TXcalloc(pmbuf, fn, 3*nLoci,
						 sizeof(LOCUS));
			if (loci == LOCUSPN) goto err;
			lociSrcEnd = loci + nLoci;
			/* Bug 3746: make sure `byteHits' set as expected: */
			if (h->byteHits && h->byteHitEnds)
			{			/* findrankabs() set them */
				for (locus = loci;
				     locus < lociSrcEnd;
				     locus++, hitIdx++)
				{
				locus->hitStart = text + h->byteHits[hitIdx];
				locus->hitEnd = text + h->byteHitEnds[hitIdx];
				locus->nSets = 1;
				locus->nSetHits = 1;
				locus->hitAlignment = HAL_CENTER;
				}
			}
			else			/* failed to set them wtf */
			{
				/* No `byteHits' for some reason; guess using
				 * `h->hits' and our usual average-word-len
				 * of 6 bytes.  But yap because this should
				 * not happen (but only once to avoid bloat):
				 */
				if (TX_ATOMIC_INC(&gotMissingByteOffsets) == 0)
					txpmbuf_putmsg(pmbuf, MERR, fn,
							missingByteOffsets);
				for (locus = loci;
				     locus < lociSrcEnd;
				     locus++, hitIdx++)
				{
				locus->hitStart = text + h->hits[hitIdx]*6;
				locus->hitEnd = locus->hitStart + 5;
				locus->nSets = 1;
				locus->nSetHits = 1;
				locus->hitAlignment = HAL_CENTER;
				}
			}
		}
		else				/* one locus per set */
		{
			nLoci = nHits;
			loci = (LOCUS *)TXcalloc(pmbuf, fn, 3*nLoci,
						 sizeof(LOCUS));
			if (loci == LOCUSPN) goto err;
			lociSrcEnd = loci + nLoci;
			for (locus = loci, hitIdx = 0;
			     locus < lociSrcEnd;
			     locus++, hitIdx++)
			{
				h = hits[hitIdx];
				/* Bug 3746: make sure `byteHits' set as
				 * expected:
				 */
				if (h->byteHits && h->byteHitEnds)
				{
				locus->hitStart = text+h->byteHits[h->curHit];
				locus->hitEnd =text+h->byteHitEnds[h->curHit];
				}
				else		/* byteHits missing wtf */
				{
				if (TX_ATOMIC_INC(&gotMissingByteOffsets) == 0)
					txpmbuf_putmsg(pmbuf, MERR, fn,
							missingByteOffsets);
				locus->hitStart = text + h->hits[h->curHit]*6;
				locus->hitEnd = locus->hitStart + 5;
				}
				locus->nSets = 1;
				locus->nSetHits = h->nhits;
				locus->hitAlignment = HAL_CENTER;
			}
		}
		break;
	case TXABS_STYLE_QUERYSINGLE:		/* 1 chunk centered on query*/
		if (query == CHARPN) goto doDumb;	/* see reason above */
		bp = findrankabs(text, query, table, idxExprs, locale,
				FDBIHIPPPN, INTPN);
		if (bp == BYTEPN) goto doDumb;	/* failed: fallback to dumb */
		s = (char *)bp;
		/* Fake a single locus `singleHi' centered at `s': */
		memset(singleLocus, 0, sizeof(singleLocus));
		loci = singleLocus;
		loci->nSets = loci->nSetHits = 1;
		loci->hitStart = s;
		loci->hitEnd = s;
		loci->hitAlignment = HAL_CENTER;
		nLoci = 1;
		lociSrcEnd = loci + nLoci;
		break;
	case TXABS_STYLE_SMART:			/* "best" chunk of text */
		s = (char *)findabs(text);
		if (s == CHARPN) goto doDumb;	/* failed: fallback to dumb */
		goto startAtS;
	case TXABS_STYLE_DUMB:			/* start of text */
	default:
	doDumb:
		s = text;
	startAtS:
		/* Fake a single locus `singleHi' starting at `s',
		 * with hit at `s' too, which helps prevent txAlignLocus()
		 * from sliding the start forward past start-of-text for
		 * "dumb" mode:
		 */
		memset(singleLocus, 0, sizeof(singleLocus));
		loci = singleLocus;
		/* No sets, since no query; also lets txExpandToUrl() loop
		 * (below) take over this locus:
		 */
		loci->nSets = loci->nSetHits = 0;
		loci->hitStart = s;
		loci->hitEnd = s;
		loci->hitAlignment = HAL_LEFT;
		nLoci = 1;
		lociSrcEnd = loci + nLoci;
		break;
	}

	/* Dup the loci array before mods or deletions, for later use: */
	orgLoci = (LOCUS *)TXcalloc(pmbuf, fn, nLoci, sizeof(LOCUS));
	if (orgLoci == LOCUSPN) goto err;
	memcpy(orgLoci, loci, nLoci*sizeof(LOCUS));
	nOrgLoci = nLoci;

	/* If a hit is within a URL, potentially add loci for the
	 * URL start and end, since that may be significant (this is
	 * why we over-alloc'ed `loci' 3x).  Do this early, so that
	 * loci-expansion can expand these potentially beyond `urlHostEnd'
	 * and `urlTrailStart'.  These new loci do not count as
	 * "significant" loci, i.e. towards the too-many-loci count,
	 * because they were originally one locus, and do not seem to
	 * fragment the abstract too much (plus, otherwise they might
	 * get eliminated in a small abstract of a single URL):
	 */
	nUrlSplitLoci = 0;
	switch (absstyle)
	{
	case TXABS_STYLE_QUERYMULTIPLE:		/* supports multiple loci */
		for (locus = loci; locus < lociSrcEnd; locus++)
		{
			/* If hit is within previous URL, no need to make
			 * more (dup) loci; also saves txExpandToUrl() call:
			 */
			if (locus->hitStart >= prevUrlStart &&
			    locus->hitStart <= prevUrlEnd)
				continue;
			if (!txExpandToUrl(locus->hitStart, text, textEnd,
					   &urlStart, &urlHostEnd,
					   &urlTrailStart, &urlEnd))
				continue;	/* not in a URL */
			locus->flags |= LF_INURL;
			incLocus = pfxHttp = 0;

			/* Create locus for URL start: */
			newLocus = loci + nLoci++;
			newLocus->hitStart = urlStart;
			newLocus->hitEnd = urlHostEnd;
			/* newLocus->expnRightLimit = urlEnd; */
			newLocus->hitAlignment = HAL_LEFT;
			/* Strip one common prefix to save space: */
			if ((size_t)(urlHostEnd - urlStart) >= HTTP_PFX_SZ &&
			    strnicmp(urlStart,httpPfx,HTTP_PFX_SZ)==0)
			{
				/* Move dumb-mode hit up too,
				 * since it doesn't have real hit,
				 * so that later merge stays at +7:
				 */
				incLocus = (locus->nSets == 0 &&
					    locus->hitStart == urlStart &&
					    locus->hitEnd == urlStart);
				if (incLocus ||
				    locus->hitStart >= urlStart + HTTP_PFX_SZ)
				{
					newLocus->hitStart += HTTP_PFX_SZ;
					newLocus->flags |= LF_ISHTTP;
					pfxHttp = 1;
					/* Do not let "real" hit expand left
					 * to re-include the removed prefix,
					 * if the text is just the URL:
					 */
					if (urlStart == text &&
					    urlEnd == textEnd)
						locus->expnLeftLimit =
						    newLocus->hitStart;
				}
				if (incLocus)
				{
					locus->hitStart = newLocus->hitStart;
					locus->hitEnd = newLocus->hitEnd;
					locus->flags |= LF_ISHTTP;
				}
			}
			/* No sets in this locus, so that it is
			 * lower-priority if deletes are needed
			 * later due to too many loci:
			 */
			newLocus->nSets = newLocus->nSetHits = 0;
			newLocus->flags |= (LF_URLSPLIT | LF_INURL);
			if (urlStart == text && urlEnd == textEnd)
			{
				newLocus->flags |= LF_TEXTISURL;
				if (incLocus) locus->flags |= LF_TEXTISURL;
			}
			nUrlSplitLoci++;

			/* Create locus for URL end, if beyond hit and
			 * "interesting", i.e. `/' or `?' appears near end
			 * and we're likely to fit it all in the locus:
			 */
			if (locus->hitEnd < urlEnd &&
			    urlEnd - urlTrailStart <= 20)
			{
				newLocus = loci + nLoci++;
				newLocus->hitStart = urlTrailStart;
				newLocus->hitEnd = urlEnd;
				newLocus->expnLeftLimit = urlStart;
				if (pfxHttp)
					newLocus->expnLeftLimit += HTTP_PFX_SZ;
				newLocus->hitAlignment = HAL_RIGHT;
				newLocus->nSets = newLocus->nSetHits = 0;
				newLocus->flags |= (LF_URLSPLIT | LF_INURL);
				if (urlStart == text && urlEnd == textEnd)
					newLocus->flags |= LF_TEXTISURL;
				nUrlSplitLoci++;
				nAlignRight++;
			}
			prevUrlStart = urlStart;
			prevUrlEnd = urlEnd;
		}
		if (nUrlSplitLoci > 0)		/* we added loci: re-order */
		{
			/* LOCUS.start/end not defined yet; use hit-order: */
			qsort(loci, nLoci, sizeof(LOCUS), txHitOrderLociCmp);
			lociSrcEnd = loci + nLoci;
		}
		break;
	default:
		/* Other styles do not support multiple loci, so do not
		 * URL-split if those modes specifically requested
		 */
		break;
	}

	/* Expand each locus to its fraction of the overall abstract size.
	 * Also note if any loci are already too large, and whether other
	 * loci might be shrunk (in a later pass) to compensate:
	 */
	desiredLocusSz = maxsz/nLoci;		/* desired text per locus */
        expnSz = shrinkSz = 0;
	reSort = 0;
	for (locus = loci; locus < lociSrcEnd; locus++)
	{					/* for each locus */
		/* Locus is initially the hit: */
		locus->start = locus->hitStart;
		locus->end = locus->hitEnd;

		/* Expand locus to `desiredLocusSz', checking centering: */
		hitSz = txDiffTextPtrs(locus->hitEnd, locus->hitStart);
		if (desiredLocusSz >= hitSz)	/* locus can be expanded */
		{
			txExpandLocus(locus, desiredLocusSz - hitSz,
				      text, textEnd);
			/* Note (for later) that we can shrink this locus
			 * if needed, but not too much:
			 */
			sz = hitSz;
			if (sz < MIN_DESIRED_LOCUS_SZ)
				sz = MIN_DESIRED_LOCUS_SZ;
			sz2 = txDiffTextPtrs(locus->end, locus->start);
			if (sz2 > sz)
				shrinkSz += sz2 - sz;
		}
		else				/* locus too big: trim down */
		{
			expnSz += hitSz - desiredLocusSz;
			/* These alignment-based shifts assumed below: */
			switch (locus->hitAlignment)
			{
			case HAL_LEFT:
			case HAL_CENTER:
				locus->end = locus->start;
				txAddTextPtr(&locus->end, desiredLocusSz,
					     textEnd);
				break;
			case HAL_RIGHT:
				locus->start = locus->end;
				txSubTextPtr(&locus->start, desiredLocusSz,
					     text);
				break;
			}
		}
		/* Loci must stay in LOCUS.start order from here on: */
		if (locus > loci && locus->start < locus[-1].start)
			reSort = 1;
	}
	if (reSort) qsort(loci, nLoci, sizeof(LOCUS), txLocusOrderLociCmp);

	/* If some hits were larger than `desiredLocusSz' (unlikely,
	 * but possible e.g. a REX for a long URL), try to expand their loci,
	 * iff we can steal space from other loci:
	 */
	if (expnSz > shrinkSz) expnSz = shrinkSz;
	shrinkSz = expnSz;
	for (locus = loci;
	     locus < lociSrcEnd && (expnSz > 0 || shrinkSz > 0);
	     locus++)
	{					/* for each locus */
		hitSz = txDiffTextPtrs(locus->hitEnd, locus->hitStart);
		if (desiredLocusSz >= hitSz)	/* can steal space from it */
		{
			sz = hitSz;
			if (sz < MIN_DESIRED_LOCUS_SZ)
				sz = MIN_DESIRED_LOCUS_SZ;
			if (desiredLocusSz > sz)
			{
				sz = desiredLocusSz - sz;
				if (sz > shrinkSz) sz = shrinkSz;
				shrinkSz -= txShrinkLocus(locus, sz, text,
							  textEnd);
			}
		}
		else				/* want to expand */
		{
			/* Alignment-based shifts assumed from above: */
			switch (locus->hitAlignment)
			{
			case HAL_LEFT:
			case HAL_CENTER:
				sz = txDiffTextPtrs(locus->hitEnd,locus->end);
				if (sz > expnSz) sz = expnSz;
				expnSz -= txAddTextPtr(&locus->end, sz,
						       textEnd);
				break;
			case HAL_RIGHT:
				sz = txDiffTextPtrs(locus->start,
						    locus->hitStart);
				if (sz > expnSz) sz = expnSz;
				expnSz -= txSubTextPtr(&locus->start,sz,text);
				break;
			}
		}
	}

	/* Merge overlapping/adjacent/real-close loci.  This may be done in
	 * several passes, as any overlap is re-alloted to expand remaining
	 * loci, which may cause further merges if they now abut other loci:
	 * Note that this loop assumes loci are in LOCUS.start order:
	 */
	for (mergePass = 0; mergePass < 5; mergePass++)
	{					/* arbitrary max 5; << Inf. */
		/* First merge and delete adjacent/overlapping loci: */
		expnSz = shrinkSz = 0;		/* no expansions yet */
		prevLocus = loci;
		for (locus = loci + 1; locus < lociSrcEnd; locus++)
		{
			/* If locus overlaps or is adjacent to previous,
			 * or is within `...' of previous, merge and delete:
			 */
			if (prevLocus->end + ELLIPSIS_SZ + 2 >=
				locus->start)
			{
				/* Set LF_ISHTTP before `start' changes: */
				if ((locus->flags & LF_ISHTTP) &&
				    locus->start == prevLocus->start)
					prevLocus->flags |= LF_ISHTTP;
				/* If overlap is due solely to `...'-size,
				 * we'll "overexpand" when merging, so
				 * remember the amount for shrinking later:
				 */
				if (prevLocus->end < locus->start)
				{		/* not adj. but within `...'*/
					shrinkSz+=txDiffTextPtrs(locus->start,
							    prevLocus->end);
					/* no `expnSz' overlap: */
					prevLocus->end = locus->start;
				}
				/* Note the amount of overlap: */
				expnSz += txDiffTextPtrs(prevLocus->end,
							 locus->start);
				/* Merge `locus' into `prevLocus' and remove.
				 * Merge hit too so txAlignLocus() and
				 * later checks this loop:
				 */
				if (locus->end > prevLocus->end)
					prevLocus->end = locus->end;
				if (locus->hitEnd > prevLocus->hitEnd)
					prevLocus->hitEnd = locus->hitEnd;
				if (locus->hitStart < prevLocus->hitStart)
					prevLocus->hitStart = locus->hitStart;
				if ((prevLocus->flags | locus->flags) &
				    LF_URLSPLIT)
					nUrlSplitLoci--;
				txMergeLociAux(prevLocus, locus);
				nLoci--;	/* deleting `locus' */
				continue;
			}
			/* Slide locus and previous locus together
			 * (preserving sizes), if that would make them
			 * adjacent and still contain their hits.  Avoids
			 * an unneeded `...' (at the expense of some lead
			 * and trail words though):
			 */
			s = prevLocus->hitStart;
			if (locus->hitStart < s) s = locus->hitStart;
			e = locus->hitEnd;
			if (prevLocus->hitEnd > e) e = prevLocus->hitEnd;
			if (txDiffTextPtrs(e, s) <= txDiffTextPtrs(
				prevLocus->end, prevLocus->start) +
				txDiffTextPtrs(locus->end, locus->start))
			{			/* can contain both hits */
				gap = txDiffTextPtrs(locus->start,
						     prevLocus->end);
				sz = gap/2;	/* slide LHS rightward */
				sz2 = gap - sz;	/* slide RHS leftward */
				diff = txDiffTextPtrs(s, prevLocus->start);
				if (sz > diff)
				{
					sz2 += sz - diff;
					sz = diff;
				}
				diff = txDiffTextPtrs(locus->end, e);
				if (sz2 > diff)
				{
					sz += sz2 - diff;
					sz2 = diff;
				}
				txAddTextPtr(&prevLocus->start, sz, textEnd);
				prevLocus->end = locus->end;
				txSubTextPtr(&prevLocus->end, sz2, text);
				prevLocus->hitStart = s;
				prevLocus->hitEnd = e;
				if ((prevLocus->flags | locus->flags) &
				    LF_URLSPLIT)
					nUrlSplitLoci--;
				txMergeLociAux(prevLocus, locus);
				nLoci--;	/* deleting `locus' */
				continue;
			}
			/* Removed deleted loci from array: */
			*(++prevLocus) = *locus;
		}
		/* Reset end of loci, since we may have deleted some items: */
		lociSrcEnd = loci + nLoci;

		/* We are about to expand loci (below) if we gained some
		 * overlap (`expnSz') from merges above; if not, we will
		 * not need another pass and may exit this loop:
		 */
		if (expnSz >= shrinkSz)
			sz = expnSz - shrinkSz;
		else				/* wtf now too big? */
			sz = 0;
		sz /= nLoci;			/* per-locus value */
		/* But first, reduce number of loci if too many for given
		 * abstract size (i.e. reduce "choppiness").  Arbitrary
		 * formula for max loci is sqrt(maxsz)/4; optimize
		 * common cases to avoid slower sqrt() calls.  Do this after
		 * loci-merge above (in case that reduces # of loci better)
		 * but before loci expansion below (because deleting loci
		 * adds to `expnSz').  (See also size limits for adding
		 * loci on one-set queries, above.):
		 */
		if (sz <= 0)			/* last pass otherwise */
		{
			if (maxsz < 64) maxLoci = 1;
			else if (maxsz < 144) maxLoci = 2;
			else if (maxsz < 256) maxLoci = 3;
			else			/* maxLoci >= 4 */
			{
				if (nLoci <= 4) maxLoci = 4; /* avoid sqrt()*/
				else maxLoci = ((size_t)sqrt((double)maxsz))/4;
			}
			/* Select "best" loci if too many.  Note that
			 * URL-split loci do not count:
			 */
			if (nLoci - nUrlSplitLoci > maxLoci)
			{
				/* Move best `maxLoci' loci to array start: */
				qsort(loci, nLoci, sizeof(LOCUS),
					txBestLociCmp);
				/* Credit `expnSz' for deleted loci' sizes.
				 * Also add LF_TEXTISURL loci back, if any;
				 * this helps ensure an abstract of just URL
				 * always has the URL prefix and suffix:
				 */
				for (locus = loci + maxLoci;
				     locus < loci + nLoci;
				     locus++)
				{
					if (locus->flags & LF_TEXTISURL)
					{
						if (locus > loci + maxLoci)
							loci[maxLoci] = *locus;
						maxLoci++;
					}
					else
						expnSz += txDiffTextPtrs(
						    locus->end, locus->start);
				}
				/* Delete trailing loci and re-order: */
				nLoci = maxLoci;
				qsort(loci, nLoci, sizeof(LOCUS),
				      txLocusOrderLociCmp);
				lociSrcEnd = loci + nLoci;
				/* Fix `nUrlSplitLoci': must be < `nLoci': */
				nUrlSplitLoci = 0;
				for (locus=loci; locus < lociSrcEnd; locus++)
					if (locus->flags & LF_URLSPLIT)
						nUrlSplitLoci++;
			}
		}

		/* Expand loci if we have some overlap from merges above.
		 * If not, there is nothing to expand, and thus we do not
		 * need another merge-check pass, so stop now:
		 */
		if (expnSz >= shrinkSz)
			expnSz -= shrinkSz;
		else				/* wtf now too big? */
			expnSz = 0;
		expnSz /= nLoci;		/* now per-locus value */
		if (expnSz <= 0) break;		/* no overlap: done */
		reSort = 0;
		for (locus = loci; locus < lociSrcEnd; locus++)
		{
			txExpandLocus(locus, expnSz, text, textEnd);
			if (locus > loci && locus->start < locus[-1].start)
				reSort = 1;
		}
		if (reSort)
			qsort(loci, nLoci, sizeof(LOCUS),txLocusOrderLociCmp);
	}

	/* Now align loci ends (i.e. with word boundaries), and print: */
	rc = txAlignAndPrintLoci(loci, nLoci, orgLoci, nOrgLoci, text,
				 textEnd, absstyle);
	goto done;

err:
	rc = CHARPN;
done:
	if (loci != LOCUSPN && loci != singleLocus) free(loci);
	if (orgLoci != LOCUSPN && orgLoci != singleLocus) free(orgLoci);
	return rc;
}

/**********************************************************************/
