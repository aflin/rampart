
/* PBR 12-21-90  added NONALNUMEQSPACE for lpm support () */
/* PBR 01-24-91  fixed bug with NONALNUMEQSP */
/* MAW 01-16-97  added PM_FLEXPHRASE phrase handling */
/* MAW 03-05-97  added PM_NEWLANG language processing */
/* MAW 08-12-98 fix PM_FLEXPHRASE for single letter and common root word phrases */
/*
#define TESTPPM 1
#define DEBUG 1
*/
/**********************************************************************/
#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#ifdef MVS
#  if MVS_C_REL > 1
#     include <stddef.h>
#  else
#     include <stdefs.h>
#  endif
#endif
#include <string.h>
#include <ctype.h>
#include "sizes.h"
#include "os.h"
#include "txtypes.h"
#include "pm.h"
#include "api3.h"                              /* for MM3S */
#include "unicode.h"                            /* for TXCFF... */


#ifdef LINT_ARGS
int ppmsmhit(PPMS *);
#else
int ppmsmhit();
#endif

static const char       OutOfMem[] = "Out of memory";

/* SAME_BYTE(ps, a, b): nonzero if byte `a' and `b' are the same
 * (ignoring case etc. as per current locale compare table):
 */
#define SAME_BYTE(ps, a, b)     \
  ((ps)->byteCompareTable[(byte)(a)] == (ps)->byteCompareTable[(byte)(b)])

/* FIND_END_OF_MATCH(ps, ok, p1, p2): advances `p1'/`p2' to the
 * end of their common prefix, or until `ok' is false:
 */
#define FIND_END_OF_MATCH(ps, ok, p1, p2)  \
  for ( ; (ok) && SAME_BYTE((ps), *(p1), *(p2)); (p1)++, (p2)++)

/* BYTE_CMP(ps, a, b): compares bytes `a' and `b'; strcmp()-like
 * return value:
 */
#define BYTE_CMP(ps, a, b)                      \
  ((int)(ps)->byteCompareTable[(byte)(a)] -     \
   (int)(ps)->byteCompareTable[(byte)(b)])

#define MAX_BUF_IN_MSG_LEN      256

/************************************************************************/

int
TXppmStrcmp(ps, a, b)                           /* ppm's string compare */
PPMS    *ps;
byte *a;
byte *b;
/* strcmp() using BYTE_CMP(): ignore case, etc. according to byte
 * compare table.
 */
{
  FIND_END_OF_MATCH(ps, *a && *b, a, b);
  return(BYTE_CMP(ps, *a, *b));
}

/************************************************************************/

int
TXppmStrPrefixCmp(ps, prefix, s)        /* ppm's string prefix compare */
PPMS    *ps;
byte *prefix;
byte *s;
/* Like TXppmStrcmp(), but strings compare equal if `prefix' is a prefix
 * of `s'.
 */
{
  FIND_END_OF_MATCH(ps, *prefix && *s, prefix, s);
  if (*prefix == '\0') return(0);               /* compare equal */
  return(BYTE_CMP(ps, *prefix, *s));
}

/************************************************************************/
typedef struct PSS_tag  PSS;
#define PSSPN (PSS *)NULL
struct PSS_tag
{
  PPMS  *ps;
   byte *term;
   byte *orgTerm;
   PMPHR *phraseObj;
  size_t        orgIdx;                         /* original user array index*/
   TXPPM_BF     flags;
  byte  isDup;
};

int
CDECL
ppmsortcmp(va, vb)                        /* compare for sort in openppm */
CONST void *va;
CONST void *vb;
{
  PSS *a = (PSS *)va;
  PSS *b = (PSS *)vb;
  int   ret;

  ret = TXppmStrcmp(a->ps, a->term, b->term);
  if (ret == 0)                                 /* terms identical */
    {
      a->isDup = b->isDup = 1;
      /* Be deterministic/consistent: sort by original user array order if
       * terms are the same:
       */
      ret = (a->orgIdx < b->orgIdx ? -1 : (a->orgIdx > b->orgIdx ? 1 : 0));
    }
  return(ret);
}

/************************************************************************/

static int
setupphrase(ps)                                       /* MAW 01-08-97 */
PPMS *ps;
{
  static const char fn[] = "setupphrase";
  int i;
  PSS *dlst = NULL, *pss;
  MM3S  msBuf;

   /* Fake up an MM3S with default settings.  See also spm.c.
    * wtf someday pass in a real MM3S to get actual live settings;
    * until then use ..._DEFAULT_OLD so openpmphr() agrees with us:
    */
   memset(&msBuf, 0, sizeof(MM3S));
   msBuf.textsearchmode = TXCFF_TEXTSEARCHMODE_DEFAULT_OLD;

   ps->orgTermList = ps->wordList;              /* preserve caller's list */
   ps->wordList = (byte **)calloc(ps->numTerms, sizeof(byte *));
   ps->phraseObjList = (PMPHR **)calloc(ps->numTerms, sizeof(PMPHR *));
   if (!ps->wordList || !ps->phraseObjList) goto zmae;
   for (i = 0; i < ps->numTerms; i++)           /* dup list */
   {
      if ((ps->phraseObjList[i] = openpmphr(ps->orgTermList[i], 0, &msBuf,
                                            pm_getHyphenPhrase())) == PMPHRPN)
      {
         for(i--;i>=0;i--)
            closepmphr(ps->phraseObjList[i]);
         free(ps->phraseObjList);
         ps->phraseObjList = (PMPHR **)NULL;
         goto zmae;
      }
      if (ps->phraseObjList[i]->nterms <= 1)   /* one-word term */
      {
         ps->phraseObjList[i] = closepmphr(ps->phraseObjList[i]);
         ps->wordList[i] = ps->orgTermList[i];
      }
      else
         ps->wordList[i] = ps->phraseObjList[i]->term;
   }
               /* sort the search list keeping the query list in sync */
              /* ie: sort 3 parallel lists using the first list's key */
   if (!(dlst = (PSS *)calloc(ps->numTerms, sizeof(PSS))))
      goto zmae;
   for (i = 0; i < ps->numTerms; i++)
   {
     pss = &dlst[i];
     pss->ps = ps;
     pss->term = ps->wordList[i];
     pss->orgTerm = ps->orgTermList[i];
     pss->phraseObj = ps->phraseObjList[i];
     pss->orgIdx = i;
     pss->flags = ps->flags[i];
     pss->isDup = 0;
   }
   qsort((char *)dlst, ps->numTerms, sizeof(PSS), ppmsortcmp);
   for (i = 0; i < ps->numTerms; i++)
   {
     pss = &dlst[i];
     ps->wordList[i] = pss->term;
     ps->orgTermList[i] = pss->orgTerm;
     ps->phraseObjList[i] = pss->phraseObj;
     ps->flags[i] = pss->flags;
     /* Set TXPPM_BF_IS_DUP_OF_NEXT_TERM.  We use PSS.isDup merely as an
      * optimization to try to avoid comparing all terms: it is
      * nonzero if the term is a duplicate of some other term (now
      * adjacent before or after, since list is sorted); we set it
      * during the qsort() since we were comparing there anyway.  We
      * know it is reliably set, because any term that is a duplicate
      * of some other term (or group of terms) must eventually be
      * compared against at least one member of that group by the sort
      * algorithm; otherwise its correct location is unknown.  We can
      * thus use it to only have to compare terms we already know are
      * dups of something:
      */
     if (pss->isDup &&                          /* dup of something */
         i + 1 < ps->numTerms &&                /* there is a next term */
         TXppmStrcmp(ps, ps->wordList[i], ps->wordList[i + 1]) == 0)
       ps->flags[i] |= TXPPM_BF_IS_DUP_OF_NEXT_TERM;
   }
   free(dlst);
   dlst = NULL;
   return(1);
zmae:
   putmsg(MERR + MAE, fn, OutOfMem);
   return(0);
}

/************************************************************************/

static void
cleanupphrase(ps)
PPMS *ps;
{
int i;

   if (ps->phraseObjList)
   {
      for (i = 0; i < ps->numTerms; i++)
      {
         if (ps->phraseObjList[i])
            ps->phraseObjList[i] = closepmphr(ps->phraseObjList[i]);
      }
      free(ps->phraseObjList);
      ps->phraseObjList = (PMPHR **)NULL;
   }
   if (ps->wordList)
   {
      free(ps->wordList);
      ps->wordList = (byte **)NULL;
   }
}

/**********************************************************************/

static int setuplang ARGS((PPMS *ps));
static int
setuplang(ps)
PPMS *ps;
/* Sets `ps->wordc', `ps->langc', `ps->flags[i] & TXPPM_BF_IS_LANGUAGE_QUERY'.
 * Note that `ps->wordList' is the *original* (user-given) string list here
 * (e.g. like `ps->orgTermList' later: phrases possible), not the later
 * all-single-word string list.
 * Returns 0 on error.
 */
{
byte *s;
int i;

   ps->wordc=pm_getwordc();
   ps->langc=pm_getlangc();
   for (i = 0; i < ps->numTerms; i++)
   {
      ps->flags[i] |= TXPPM_BF_IS_LANGUAGE_QUERY;
      for (s = ps->wordList[i]; *s; s++)
         if(!ps->langc[*s])
            { ps->flags[i] &= ~TXPPM_BF_IS_LANGUAGE_QUERY; break; }
   }
   return(1);
}

/************************************************************************/

static int initppms ARGS((PPMS *ps, byte **sl, const int *pmCompareTable,
                          int localeSerial));

static int
initppms(ps, sl, pmCompareTable, localeSerial)  /* init the ppms struct */
PPMS *ps;
byte *sl[];
const int       *pmCompareTable;        /* (in) pm_getct() compare table */
int             localeSerial;           /* (in) serial # of locale */
/* Initializes `ps' to search `sl' list.  Note that `ps' is assumed to
 * be zeroed, `sl' must remain alloced for duration of `ps', and `sl'
 * will be reordered.  `pmCompareTable' will be copied.
 * Returns -1 on error, 1 on success.
 */
{
  static const char     fn[] = "initppms";
 register int i,j,k,patsz;

 /* Make a private copy of the byte compare table, in case locale etc.
  * changes during the life of this PPMS object.  Note the serial #
  * so we can detect global locale changes:
  */
 for (i = 0; i < DYNABYTE; i++)
   ps->byteCompareTable[i] = (byte)(pmCompareTable[i]);
 ps->localeSerial = localeSerial;
 ps->localeChangeYapped = 0;

 ps->minTermLen = DYNABYTE;                     /* init min and max values */
 ps->maxTermLen = 0;

 for (i = 0; sl[i] && *sl[i] != '\0'; i++);
/* Potential fix for some bad queries? WTF JMT 98-01-21 */
 if (i == 0) return(-1);

 ps->wordList = sl;
 ps->numTerms = i;
 if (!(ps->flags = (TXPPM_BF *)calloc(ps->numTerms, sizeof(TXPPM_BF))))
   {
     ps->wordList = NULL;
     ps->numTerms = 0;
     return(-1);
   }

 ps->phraseObjList = (PMPHR **)NULL;
 if(!setuplang(ps))
 {
     ps->wordList = NULL; /* JMT 98-01-23 wordList not alloced yet */
     ps->numTerms = 0;
     return(-1);
 }

 if(!setupphrase(ps))                                 /* MAW 01-08-97 */
     return(-1);

 for (i = 0; i < ps->numTerms; i++)
    {
      register byte *term = ps->wordList[i], *s;
     register int  termLen = strlen((char *)term);
     register PMBITGROUP bit;

     bit = TXPPM_INDEX_BIT_MASK(i);

     if (termLen > TX_PPM_MAX_TERM_LEN)         /* entry oversized */
       {
         putmsg(MERR + MAE, fn, "Term `%+.50s' too long to search for",
                term);
         return(-1);
       }

     if (termLen > ps->maxTermLen)              /* readjust max if needed */
	 {
				 /* grab mem for more sets if termLen > max */
	  for (j = ps->maxTermLen; j < termLen; j++)
	      {
	       ps->setTable[j] = (PMBITGROUP *)calloc(DYNABYTE,
                                                      sizeof(PMBITGROUP));
	       if (ps->setTable[j] == (PMBITGROUP *)NULL)
                 {
                   putmsg(MERR + MAE, fn, OutOfMem);
		   return(-1);
                 }
	      }
	  ps->maxTermLen = termLen;
	 }

     if (termLen < ps->minTermLen) ps->minTermLen = termLen;

     /* set length bit (correspond to end byte): */
     ps->lengthTable[termLen - 1] |= bit;

     for (j = 0, s = term; *s != '\0'; s++, j++)  /* set set table entries */
       {
         for (k = 0; k < DYNABYTE; k++)
           if (SAME_BYTE(ps, k, *s))
             ps->setTable[j][k] |= bit;
       }
    }

	  /* make an empty table at the end to ensure a stop */
 ps->setTable[ps->maxTermLen] = (PMBITGROUP *)calloc(DYNABYTE,
                                                     sizeof(PMBITGROUP));
 if (ps->setTable[ps->maxTermLen] == (PMBITGROUP *)NULL)
   {
     putmsg(MERR + MAE, fn, OutOfMem);
     return(-1);
   }

 ps->numTerms = i;                              /* set the qty of strings */

 for(i=0;i<DYNABYTE;i++)		  /* set jt default to jump min */
    ps->jumpTable[i] = (byte)ps->minTermLen;

 patsz = ps->minTermLen - 1;    /* min-1 to ignore end byte in pattern */

 if(patsz<=0)					  /* no jumping allowed */
    return(1);

 for(i=0,j=patsz;i<patsz;i++,j--)		 /* set jmp tab entries */
     for(k=0;k<DYNABYTE;k++)
	 if (ps->setTable[i][k])
	      ps->jumpTable[k] = (byte)j;

#ifdef DEBUG
 for(i=0;i<DYNABYTE;i++)
    if(isgraph(i))
      printf("%c %d\t", i, (int)ps->jumpTable[i]);
#endif

 return(1);

}

/************************************************************************/

PPMS *
closeppm(ps)
PPMS *ps;
{
 int i;
 if(ps!=PPMSPNULL)
    {
      if (TXtraceMetamorph & TX_TMF_Open)
        putmsg(MINFO, "closeppm", "Closing PPM object %p", ps);
      for (i = 0; i < TX_PPM_MAX_TERM_LEN + 1 && ps->setTable[i]; i++)
       {
	 free(ps->setTable[i]);
         ps->setTable[i] = NULL;
       }
     cleanupphrase(ps);
     if (ps->flags)
       {
         free(ps->flags);
         ps->flags = NULL;
       }
     free((char *)ps);
     ps = NULL;
    }
  return(PPMSPNULL);
}

/************************************************************************/

PPMS *
openppm(sl)
byte *sl[];
{
  static const char     fn[] = "openppm";
 PPMS *ps=(PPMS *)calloc(1,sizeof(PPMS));

 if (!ps)
   {
     putmsg(MERR + MAE, fn, OutOfMem);
     return(NULL);
   }
 if (initppms(ps, sl, pm_getct(), TXgetlocaleserial()) == -1)
    return(closeppm(ps));

 if (TXtraceMetamorph & TX_TMF_Open)
   {
     int        i;
     size_t     sLen;
     char       *d, *e, *s, buf[2048];

     d = buf;
     e = buf + sizeof(buf);
     *d = '\0';
     for (i = 0; i < ps->numTerms; i++)
       {
         s = (char *)ps->orgTermList[i];
         if (d + (sLen = strlen(s)) + 7 < e)    /* 3 for " `'", 4 for ` ...'*/
           {
             sprintf(d, " `%s'", s);
             d += sLen + 3;
           }
         else
           {
             strcpy(d, " ...");
             break;
           }
       }
     putmsg(MINFO, fn, "Opened PPM object %p with terms%s", ps, buf);
   }

 return(ps);
}

/************************************************************************/

int
ppmstrn(register PPMS *ps, register PMBITGROUP mask)
{
 register PMBITGROUP tmask;
 register int i,n;

 for(n=0,tmask=1;n<PMTBITS;n++,tmask<<=1)	   /* find out what bit */
    if(tmask&mask)
       for (i = n; i < ps->numTerms; i += PMTBITS)
	  {
            if (TXppmStrPrefixCmp(ps, ps->wordList[i], ps->hit) == 0)
	       {
                ps->hitTermListIndex = i;
		return(1);
	       }
	  }

 return(0);
}

/************************************************************************/

static int matchphrase ARGS((PPMS *,byte *));
static int
matchphrase(ps,bp)
PPMS *ps;
byte *bp;
/* Returns 1 if phrase matched at current hit, 0 if not.  Note that
 * `ps->hitTermListIndex' may be changed, e.g. if the phrase of a
 * different term (but same word) matches.  Examines `ps->hitTermListIndex'
 * term first, then terms after it until "different".
 */
{
  int   i, ret, orgHitTermListIndex = ps->hitTermListIndex;
  byte  *termPtr, *hitPtr = ps->hitend, *orgWordHit = ps->hit, *searchBuf;
  byte  *orgWordHitEnd = ps->hitend;

  /* Bug 1765 msg #6: allow phrase matching to back up to previous
   * overall hit -- which may be 1 byte before `ps->searchBuf' --
   * if we made forward progress on the word hit.  E.g. searching
   * for (body-flu,body-fluids) against `... body fluids ...':
   * `body flu' phrase anchors at `body', `body fluids' at `fluids',
   * and `body flu' will be overall matched.  Then on CONTINUESEARCH,
   * TXppmFindNextTermAtCurrentHit() will fail -- because the phrases
   * anchor to different words -- which is ok: getppm() will advance
   * `ps->searchBuf' to `ps->hit + 1' and call pfastpm().  But this
   * +1 advance cuts off the `b' in `body', so when `fluids' is found,
   * it cannot be phrase-matched here.  So we let phrase matching
   * back up to previous hit here -- but only if `wordHit' made forward
   * progress, so we do not keep matching the same term over and over:
   */
  searchBuf = ps->searchBuf;
  if (ps->prevWordHit && ps->wordHit > ps->prevWordHit)
    searchBuf = ps->prevHit;

             /* MAW 08-12-98 - handle multiple phrases with same root */
  /* Bug 5018: only search forward in `wordList' (from
   * `hitTermListIndex'), not backward: pfastpm() etc. now bias hits
   * towards the start of identical `wordList' entries, so no need to
   * search backward here, and this helps prevent duplicate same-term
   * same-hit-location hits
   */
  for (i = ps->hitTermListIndex; i < ps->numTerms; i++)
   {
      /* Bug 5420: check for current-word-match *before* verifying
       * phrase, instead of after, so we do not inadvertently return
       * a match for the wrong term here simply because it has no phrase.
       * E.g. when looking for `(no-installing,uninstalling)' minwordlen 5
       * against text `... uninstalling ...', getppm() will find `uninstal',
       * then on CONTINUESEARCH pfastpm() gets `instal' (of `no-installing'):
       * Then here, verifyphrase() will fail to match `no-installing' (ok),
       * but then the next word in the term list (`uninstal') would be
       * assumed to match because it has no phrase: we were doing the
       * FIND_END_OF_MATCH() word-verification too late:
       * (Was causing infinite same-hit loop if looking for
       * `(no-installing,uninstalling,uninstalled)' minwordlen 5):
       */
      /* Optimization: first term is already known by caller to word-match,
       * so only word-match the later terms:
       */
      if (i > orgHitTermListIndex)
        {
          termPtr = ps->wordList[i];
          hitPtr = orgWordHit;
          FIND_END_OF_MATCH(ps, *termPtr && (hitPtr <= bp), termPtr, hitPtr);
          if (*termPtr || hitPtr <= bp)         /* different term */
            break;
        }

      ps->hitTermListIndex = i;
      ps->hit = orgWordHit;
      ps->hitend = hitPtr;
      if (ps->phraseObjList[i] == PMPHRPN)  /* single word; already matched */
        {
          ret = 1;
          goto finally;
        }
      if ((ps->hit = verifyphrase(ps->phraseObjList[i],
                                  searchBuf, ps->searchBufEnd,
                                  ps->hit,&ps->hitend)
           )!=(byte *)NULL)
        {
          ret = 1;
          goto finally;
        }
   }
  ret = 0;
finally:
  if (TXtraceMetamorph & TX_TMF_Phrase)
    {
      byte      *hit;
      size_t    hitLen, selHitOffset;
      int       termIdx;
      char      contextBuf[256];

      if (ret)                                  /* got a phrase match */
        {
          hit = ps->hit;
          hitLen = ps->hitend - ps->hit;
          termIdx = ps->hitTermListIndex;
        }
      else
        {
          hit = orgWordHit;
          hitLen = orgWordHitEnd - orgWordHit;
          termIdx = orgHitTermListIndex;
        }
      selHitOffset = hit - ps->searchBuf;
      TXmmShowHitContext(contextBuf, sizeof(contextBuf),
                         -1, 0, &selHitOffset, &hitLen, 1,
                         (char *)ps->searchBuf,
                         ps->searchBufEnd - ps->searchBuf);
      putmsg(MINFO, NULL, "matchphrase of PPM object %p: term #%d `%s'%s phrase-matched at %+wd length %wd: `%s'",
             ps, (int)termIdx, ps->orgTermList[termIdx],
             (ret ? "" : " and later were not"),
             (EPI_HUGEINT)(hit - ps->searchBuf),
             (EPI_HUGEINT)hitLen, contextBuf);
    }
  return(ret);
}

/************************************************************************/

static void
TXppmReportWordHit(PPMS *ps, const char *fn, int flags)
/* flags:
 *   0x1        found
 *   0x2        matching in place, not scanning
 */
{
  char  contextBuf[256];

  if (flags & 0x1)                              /* word found */
    {
      size_t    selHitOffset = ps->hit - ps->searchBuf;
      size_t    selHitLen = ps->hitend - ps->hit;

      TXmmShowHitContext(contextBuf, sizeof(contextBuf),
                         -1, 0, &selHitOffset, &selHitLen, 1,
                         (char *)ps->searchBuf,
                         ps->searchBufEnd - ps->searchBuf);
      putmsg(MINFO, NULL,
  "%s of PPM object %p: term #%d `%s' word `%s' hit at %+wd length %wd: `%s'",
             fn, ps, (int)ps->hitTermListIndex,
             ps->orgTermList[ps->hitTermListIndex],
             ps->wordList[ps->hitTermListIndex],
             (EPI_HUGEINT)(ps->hit - ps->searchBuf),
             (EPI_HUGEINT)(ps->hitend - ps->hit), contextBuf);
    }
  else
    {
      if (flags & 0x2)                          /* matching in place */
        putmsg(MINFO, NULL,
               "%s of PPM object %p: no more hits at offset %+wd",
               fn, ps, (EPI_HUGEINT)(ps->wordHit - ps->searchBuf));
      else
        {
          TXmmShowHitContext(contextBuf, sizeof(contextBuf), -1, 0,
                             NULL, NULL, 0, (char *)ps->searchBuf,
                             ps->searchBufEnd - ps->searchBuf);
          putmsg(MINFO, NULL, "%s of PPM object %p: no hits in `%s'",
                 fn, ps, contextBuf);
        }
    }
}

static int
TXppmFindNextTermAtCurrentHit(PPMS *ps)
/* Finds the next matching term (if any) at current hit location.
 * Assumes pfastpm() already found a hit.
 * Returns 1 if found, 0 if not.
 */
{
  static const char     fn[] = "TXppmFindNextTermAtCurrentHit";
 PMBITGROUP mask;
 PMBITGROUP **sets = ps->setTable;
 byte *termPtr, *hitPtr, *h, *bp, **wordList, *matchEnd;
 register int curIdx, lowIdx, highIdx, l;
 int    prevHitTermListIndex = ps->hitTermListIndex;

 /* Bug 1765 msg #5: when searching for (bodily-flu,bodily-flus)
  * against `... bodily flus ...', `ps->wordList' will be `bodily',
  * `bodily' -- two identical terms.  Thus, incrementing `ps->maskOffset'
  * here will go beyond the end of `bodily' and fail to match the second
  * `bodily' in `wordList'.  So if the next term is a dup of this term,
  * do not increment `ps->maskOffset'.  Below, we also check that the next
  * match we find is beyond the current one, so we do not
  * inadvertently keep matching the same term again.
  * Due to Bug 5018 bias to start of matching `wordList' members,
  * we know pfastpm() found the earliest `wordList' match, so
  * we only need to look forward (in `wordList') for additional hits:
  */
 if (!(ps->flags[prevHitTermListIndex] & TXPPM_BF_IS_DUP_OF_NEXT_TERM))
   ++ps->maskOffset;
 wordList = ps->wordList;
 l = ps->maskOffset;
 /* Bug 1765: continue matching at the *word* hit -- as found by pfastpm() --
  * not the *phrase* hit -- as expanded by matchphrase(): the latter
  * may point to a different word in the phrase than pfastpm() found
  * (before matchphrase()) and we expect here.  E.g. looking for
  * (body-fluid,body-fluids) against `... body fluids ...', pfastpm()
  * found `fluid' (which we should continue with here), but matchphrase()
  * expanded that to `body fluid', which will fail here:
  */
 h = ps->wordHit;
 bp = ps->wordHit + l;
 mask=ps->mask;

 for(;mask &= sets[l][*bp];bp++,l++)
     {
      if (mask & ps->lengthTable[l])            /* `mask' term(s) end at `l'*/
	  {
            /* One or more terms that match at offset `l' also end(s)
             * at offset `l'.  Binary-search all terms to find a
             * complete match.  Bias search towards the first match in
             * `wordList' that is past previous hit `prevHitTermListIndex':
             * avoids repeating same-term same-hit-location but still
             * finds all different-term same-hit-location:
             */
            lowIdx = 0;
            highIdx = ps->numTerms;
            matchEnd = NULL;
            while (lowIdx < highIdx)
             {                                  /* binary search all terms */
               curIdx = ((lowIdx + highIdx) >> 1);
               termPtr = wordList[curIdx];
               hitPtr = h;
               FIND_END_OF_MATCH(ps, *termPtr && (hitPtr <= bp), termPtr,
                                 hitPtr);
               if (*termPtr == '\0' && hitPtr > bp)
                 {                              /* wordList[curIdx] matches */
                   matchEnd = hitPtr;           /* Bug 5149: save match end */
                   /* Bug 1765 msg #5: we may not have incremented
                    * `ps->maskOffset' above, so make sure the hit we
                    * found here is not the same term.  Bug 5018: but
                    * make sure it is *just* past `prevHitTermListIndex',
                    * so we get all different-term same-hit-location
                    * hits, but no dups:
                    */
                   if (prevHitTermListIndex < curIdx)
                     highIdx = curIdx;
                   else
                     lowIdx = curIdx + 1;
                 }
               else if (BYTE_CMP(ps, (hitPtr <= bp ? *hitPtr : '\0'),
                                 *termPtr) < 0)
                 highIdx = curIdx;
	       else
                 lowIdx = curIdx + 1;
	      }
            if (matchEnd)
              {
                ps->mask = mask;
                ps->maskOffset = l;
                ps->hitTermListIndex = lowIdx;
                ps->hit = ps->wordHit = h;
                ps->hitend = matchEnd;
                if (TXtraceMetamorph & TX_TMF_PpmInternal)
                  TXppmReportWordHit(ps, fn, 0x3);
                if (matchphrase(ps, bp))
                  return(1);
              }
	  }
     }
 if (TXtraceMetamorph & TX_TMF_PpmInternal)
   TXppmReportWordHit(ps, fn, 0x2);
 return(0);
}
/************************************************************************/

static int
TXppmFindSingleChar(PPMS *ps) /* single character parallel pattern matcher */
{
  static const char     fn[] = "TXppmFindSingleChar";
 register PMBITGROUP *hitc = ps->setTable[0];
 register PMBITGROUP **sets = ps->setTable;
 register byte **wordList = ps->wordList;
 register byte *buf,*bufend;

 for (buf = ps->searchBuf, bufend = ps->searchBufEnd; buf < bufend; buf++)/* MAW 11-14-95 - obob was <= */
    {
     if(*(hitc + *buf))
	  {
	   PMBITGROUP mask;
	   byte *termPtr, *hitPtr, *h, *bp, *matchEnd;
	   register int curIdx, lowIdx, highIdx, l;

	   h=bp=buf;
	   mask= *(hitc + *bp);

	   for(l=0;mask &= sets[l][*bp];bp++,l++)
	       {
		if (mask & ps->lengthTable[l])  /* `mask' term(s) end at `l'*/
                  {
                    /* One or more terms that match at offset `l'
                     * also end(s) at offset `l'.  Binary-search all
                     * terms to find a complete match:  Bias search
                     * towards the first match in `wordList', for Bug
                     * 5018 checks (i.e. past `hitTermListIndex') in
                     * TXppmFindNextTermAtCurrentHit():
                     */
                    lowIdx = 0;
                    highIdx = ps->numTerms;
                    matchEnd = NULL;
                    while (lowIdx < highIdx)
                      {                         /* binary search all terms */
                        curIdx = ((lowIdx + highIdx) >> 1);
                        termPtr = wordList[curIdx];
                        hitPtr = h;
                        FIND_END_OF_MATCH(ps, *termPtr && (hitPtr <= bp),
                                          termPtr, hitPtr);
                        if (*termPtr == '\0' && hitPtr > bp)
                          {                     /* wordList[curIdx] matches */
                            matchEnd = hitPtr;  /* Bug 5149: save match end */
                            highIdx = curIdx;   /* Bug 5018: bias to start */
                          }
                        else if (BYTE_CMP(ps, *hitPtr, *termPtr) < 0)
                          highIdx = curIdx;
                        else
                          lowIdx = curIdx + 1;
                      }
                    if (matchEnd)
                      {
                        ps->mask = mask;
                        ps->maskOffset = l;
                        ps->hitTermListIndex = lowIdx;
                        ps->hit = ps->wordHit = h;
                        ps->hitend = matchEnd;
                        if (TXtraceMetamorph & TX_TMF_PpmInternal)
                          TXppmReportWordHit(ps, fn, 0x1);
                        if (matchphrase(ps, bp))        /* MAW 08-12-98 */
                          return(1);
                      }
		    }
	       }
	  }
    }
 ps->hit = ps->wordHit = (byte *)NULL;
 if (TXtraceMetamorph & TX_TMF_PpmInternal)
   TXppmReportWordHit(ps, fn, 0x0);
 return(0);
}

/************************************************************************/

static CONST char copyright[] =
/************************************************************************/
	   "Copyright 1985,1986,1987,1988 P. Barton Richards";
/************************************************************************/


int
pfastpm(ps)
PPMS *ps;
/* Guts of PPM: finds next hit.
 * Returns 1 if match found, 0 if not.
 */
{
  static const char     fn[] = "pfastpm";
		   /* in order of register priority */
 register PMBITGROUP *hitc; 		    /* byte[min] in the patterns */
 register PMBITGROUP **setTable;
 register byte *bufptr, 		       /* the byte being tested */
	       *jumpTable, 				  /* jump table */
		   min, 			     /* minimum pat len */
	       *endptr; 				  /* end of buf */

 register byte **wordList = ps->wordList;

 if (ps->minTermLen <= 1)
    return(TXppmFindSingleChar(ps));

 min = (byte)(ps->minTermLen - 1);
 jumpTable = ps->jumpTable;
 bufptr = ps->searchBuf + min;
 hitc = ps->setTable[min];
 endptr = ps->searchBufEnd;
 setTable = ps->setTable;

                                        /* MAW 11-14-95 - obob was <= */
 while(bufptr<endptr)		     /* longer than one byte match */
    {
     if(*(hitc + *bufptr))
	 {
	  PMBITGROUP mask;
	  byte *termPtr, *hitPtr, *h, *bp, *matchEnd;
	  register int curIdx, lowIdx, highIdx, l;

	  h=bp=bufptr-min;
	  mask= *(hitc + *bufptr);

	  for(l=0;bp < endptr && (mask &= setTable[l][*bp]);bp++,l++)
	      {
                if (mask & ps->lengthTable[l])  /* `mask' term(s) end at `l'*/
		   {
                     /* One or more terms that match at offset `l'
                      * also end(s) at offset `l'.  Binary-search all
                      * terms to find a complete match.  Bias search
                      * towards the first match in `wordList', for Bug
                      * 5018 checks (i.e. past `hitTermListIndex') in
                      * TXppmFindNextTermAtCurrentHit():
                      */
                     lowIdx = 0;
                     highIdx = ps->numTerms;
                     matchEnd = NULL;
                     while (lowIdx < highIdx)   /* binary search all terms */
		       {
                         curIdx = ((lowIdx + highIdx) >> 1);
                         termPtr = wordList[curIdx];
                         hitPtr = h;
                         FIND_END_OF_MATCH(ps, *termPtr && (hitPtr <= bp),
                                           termPtr, hitPtr);
                         if (*termPtr == '\0' && hitPtr > bp)
                           {                    /* wordList[curIdx] matches */
                             matchEnd = hitPtr; /* Bug 5149: save match end */
                             highIdx = curIdx;  /* Bug 5018: bias to start */
                           }
                         else if (BYTE_CMP(ps, (hitPtr <= bp ? *hitPtr :'\0'),
                                           *termPtr) < 0)
                           highIdx = curIdx;
                         else
                           lowIdx = curIdx + 1;
		       }
                     if (matchEnd)
                       {
                         ps->mask = mask;
                         ps->maskOffset = l;
                         ps->hitTermListIndex = lowIdx;
                         ps->hit = ps->wordHit = h;
                         ps->hitend = matchEnd;
                         if (TXtraceMetamorph & TX_TMF_PpmInternal)
                           TXppmReportWordHit(ps, fn, 0x1);
                         if (matchphrase(ps, bp))       /* MAW 08-12-98 */
                           return(1);
                       }
		   }
	      }
	 }
     bufptr+= *(jumpTable + *bufptr);			       /* skip */
    }
 ps->hit = ps->wordHit = (byte *)NULL;          /* no hit */
 if (TXtraceMetamorph & TX_TMF_PpmInternal)
   TXppmReportWordHit(ps, fn, 0x0);
 return(0);
}

static int
TXppmShowSet(char *buf, size_t bufSz, PPMS *ps)
/* Prints set for `ps' as CSV Metamorph list, to `buf'.
 * Returns 0 on error (`buf' too small; still written to).
 */
{
  char  *s, *d, *bufEnd = buf + bufSz;
  int   i;

  for (d = buf, i = 0; d < bufEnd && i < ps->numTerms; i++)
    {
      if (i > 0) *(d++) = ',';
      for (s = (char *)ps->orgTermList[i]; *s && d < bufEnd; s++, d++)
        {
          switch (*s)
            {
            case EQDELIM:                       /* `,' */
            case EQESC:                         /* `\' */
            case POSDELIM:                      /* `;' */
              *(d++) = EQESC;
              break;
            }
          if (d < bufEnd) *d = *s;
        }
    }
  if (d < bufEnd)                               /* all fits in `buf' */
    {
      *d = '\0';
      return(1);
    }
  if (buf <= bufEnd - 4)                        /* can fit `...' */
    {
      strcpy(bufEnd - 4, "...");
      return(0);
    }
  if (buf < bufEnd) *buf = '\0';
  return(0);
}

/************************************************************************/

byte *
getppm(ps,buf,end,op)
PPMS *ps;
byte *buf,*end; 			     /* start end end of buffer */
TXPMOP op;
{
  static const char     fn[] = "getppm";
  int   prevHitTermListIndex = ps->hitTermListIndex;

  if (TXgetlocaleserial() != ps->localeSerial &&
      !ps->localeChangeYapped)
    {
      putmsg(MWARN + UGE, fn, "Locale changed: re-open PPM");
      ps->localeChangeYapped = 1;               /* do not yap a lot */
      /* wtf should we just initppms() here if locale/hyphenphrase changes?
       * set/jump/etc. tables are for other locale/hyphenphrase
       */
    }

reSearch:
 switch(op)
    {
     case SEARCHNEWBUF:
	  ps->searchBuf = buf;
	  ps->searchBufEnd = end;
          ps->hit = ps->hitend = ps->prevHit = ps->wordHit =
            ps->prevWordHit = NULL;
          ps->numHitsSameLoc = 0;
	  pfastpm(ps);
          break;
     case CONTINUESEARCH:
	 {
          byte *oh=ps->hit;                           /* MAW 08-19-98 */
	  if (TXppmFindNextTermAtCurrentHit(ps))
            break;
          ps->hit=oh;                                 /* MAW 08-19-98 */
	  ps->searchBuf = ps->hit + 1;
	  pfastpm(ps);
          break;
	 }
     default:
       ps->hit = ps->hitend = ps->prevHit = ps->wordHit =
         ps->prevWordHit = NULL;
       ps->numHitsSameLoc = 0;
       break;
    }
 if (TXtraceMetamorph & TX_TMF_Getppm)
   {
     char       contextBuf[256];
     size_t    selHitOffset = (ps->hit ? ps->hit - ps->searchBuf : -1);
     size_t    selHitLen = ps->hitend - ps->hit;

     TXmmShowHitContext(contextBuf, sizeof(contextBuf), -1, 0,
                        &selHitOffset, &selHitLen, 1, (char *)ps->searchBuf,
                        ps->searchBufEnd - ps->searchBuf);
     if (ps->hit)
       putmsg(MINFO, NULL,
  "getppm of PPM object %p: term #%d `%s' hit at %+wd length %wd: `%s'",
              ps, (int)ps->hitTermListIndex,
              ps->orgTermList[ps->hitTermListIndex],
              (EPI_HUGEINT)(ps->hit - ps->searchBuf),
              (EPI_HUGEINT)(ps->hitend - ps->hit), contextBuf);
     else
       putmsg(MINFO, NULL, "getppm of PPM object %p: no%s hits in `%s'",
              ps, (op == CONTINUESEARCH ? " more" : ""), contextBuf);

   }
 /* Bug 5420: try to detect and stop infinite loop if it ever happens.
  * Note that there could be multiple same-word terms in the PPM list
  * (e.g. due to different suffixes stripped by caller); thus it is ok
  * to return the same hit twice, iff for *different* terms:
  */
 if (ps->hit &&
     ps->hit == ps->prevHit)
   {
     /* Due to Bug 5018 bias to start and thus only looking forward in the
      * term list, we know that finding an *earlier* term at the same place
      * (not just the same term) should also be symptomatic of infinite loop.
      * Bug 5420 comment #9: above logic flawed: can get legit same-loc hit
      * from earlier term too, if phrase matching backs up.  Use findsel()
      * logic: trigger error if number of hits at current location exceeds
      * number of terms:
      */
     if (ps->numHitsSameLoc++ > (size_t)ps->numTerms)
       {
         char   setBuf[1024], contextBuf[256];
         size_t selHitOffset = ps->hit - ps->searchBuf;
         size_t selHitLen = ps->hitend - ps->hit;

         TXppmShowSet(setBuf, sizeof(setBuf), ps);
         TXmmShowHitContext(contextBuf, sizeof(contextBuf), -1, 0,
                            &selHitOffset, &selHitLen, 1,
                            (char *)ps->searchBuf,
                            ps->searchBufEnd - ps->searchBuf);
         putmsg(MERR, fn,
                "Internal error: PPM for set `%s' found same hit multiple times at offset %+wd (context: `%s'); restarting search at hit + 1",
                setBuf, (EPI_HUGEINT)(ps->hit - ps->searchBuf), contextBuf);
         op = SEARCHNEWBUF;
         buf = ps->hit + 1;
         goto reSearch;
       }
   }
 else
   ps->numHitsSameLoc = 0;

 /* Bug 1765 msg #6: save previous word/overall hit for
  * matchphrase() use later:
  * Bug 5420: *always* save prev hits (after TXppmFindNextTermAtCurrentHit()
  * too), to aid infinite-loop detection above:
  */
 ps->prevWordHit = ps->wordHit;
 ps->prevHit = ps->hit;
 return(ps->hit);
}

/************************************************************************/

void
xlateppm(tfs)
PPMS *tfs;
{
 int i,j,k;
 for (i = 0; *tfs->wordList[i] != '\0'; i++)
    puts((char *)tfs->wordList[i]);
 putchar('\n');
 printf("min=%d max=%d numTerms=%d  ps->wordList[hitTermListIndex]=%s\n",
	tfs->minTermLen, tfs->maxTermLen, tfs->numTerms,
        tfs->wordList[tfs->hitTermListIndex]);
 for (i = 0; i < tfs->maxTermLen; i++)
   {
    putchar('[');
    for(j=0;j<DYNABYTE;j++)
	{
	 if (tfs->setTable[i][j] != '\0')
	     {
	      if(isgraph(j))
		   putchar(j);
	      else printf("\\X%02X",j);
	      for(k= j+1<DYNABYTE ? j+1:j ;
		  k < DYNABYTE && tfs->setTable[i][k] != '\0';
		  k++);
	      if(--k>j+1)
		{
		 if(isgraph(k))
		      printf("-%c",k);
		 else printf("-\\X%02X",k);
		 j=k;
		}
	     }
	}
    putchar(']');
   }
 putchar('\n');
}

/************************************************************************/

#ifdef TESTPPM

#ifdef unix
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

int							/* n bytes read */
freadnl(fh,buf,len)
FILE *fh;
register byte *buf;
int  len;
{
 int nactr=fread(buf,sizeof(byte),len,fh);		   /* do a read */
 register int nread=nactr;			       /* end of buffer */
 register byte *loc;

 if(nread && nread==len)	       /* read ok && as big as possible */
    {
     for(loc=buf+nread;loc>buf && *loc!='\n';loc--);
     if(loc<=buf)			 /* no expression within buffer */
	 return(-1);
     nread=(int)(loc-buf)+1;			    /* just beyond expr */
     if (FSEEKO(fh, (off_t)(nread - nactr), SEEK_CUR) == -1)
	 return(-1);
     return(nread);
    }
 return(nactr); 		    /* read was smaller || eof || error */
}

/************************************************************************/
#define PBUFSZ 128000

byte patbuf[PBUFSZ];
char *sl[100];
int  cnt[100];

main(argc,argv)
int argc;
char *argv[];
{
 PPMS *ps;
 int nread;
 int i, n=0;
 FILE *fh;

 fh=stdin;
 for(i=1;i<argc;i++)
    {
     if(*argv[i]=='-')
	{
	 fh=fopen(argv[i]+1,"rb");
	 break;
	}
     sl[i-1]=argv[i];
    }
 sl[i-1]="";

 if((ps=openppm((byte **)sl))==PPMSPNULL)
    exit(1);
#ifdef DEBUG
 xlateppm(ps);
#endif

 while( (nread=freadnl(fh,patbuf,PBUFSZ) ) >0)
    {
     byte *hit,*end;
     end=patbuf+nread;
     for(hit=getppm(ps,patbuf,end,SEARCHNEWBUF);
	 hit;
	 hit=getppm(ps,patbuf,end,CONTINUESEARCH)
	)
        {
         ++cnt[ps->hitTermListIndex];
         printf("%d:%s:%d: \"%.*s\"\n",
                n,ppmsrchs(ps),
                ppmhitsz(ps),ppmhitsz(ps),hit);
        }
    }
 for(i=0;*sl[i]!='\0';i++)
    printf("%5d %s(%s)\n",cnt[i],sl[i],ps->wordList[i]);
 ps=closeppm(ps);
 exit(0);
}

#endif
