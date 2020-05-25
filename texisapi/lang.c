#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/stat.h>
#ifdef EPI_HAVE_MMAP
#  include <sys/mman.h>
#  ifndef MAP_FAILED
#    define MAP_FAILED  ((caddr_t)(-1))
#  endif
#endif /* EPI_HAVE_MMAP */
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#ifdef EPI_HAVE_IO_H
#  include <io.h>
#endif /* EPI_HAVE_IO_H */
#include <fcntl.h>
#include "texint.h"
#include "unicode.h"
#include "http.h"


/* Some rudimentary language analysis routines. */

/* Value to use for n in log(n) when n == 0: */
#define LOG0_VAL        ((double)1e-04)


size_t
TXgetbigramcounts(buf, sz, firstuni, lastuni, igncase, countsp)
CONST char      *buf;           /* (in) UTF-8 buffer to scan */
size_t          sz;             /* (in) (opt.) size of `buf' in bytes */
int             firstuni;       /* (in) first significant Unicode char */
int             lastuni;        /* (in) last significant Unicode char */
int             igncase;        /* (in) nonzero: ignore case */
size_t          **countsp;      /* (out) allocated 2D count array */
/* Allocates and computes a bigram count array for `buf'/`sz'.
 * Array will be (lastuni - firstuni + 1) ^ 2 in size.
 * Returns total number of bigrams found (eg. UTF-8 characters in `buf' - 1),
 * or -1 on error.
 */
{
  static CONST char     fn[] = "TXgetbigramcounts";
  size_t                totalbigrams = 0;
  int                   alphabetsz, uni1, uni2;
  CONST char            *end;

  if (firstuni < 0) firstuni = 0;               /* sanity checks */
  if (lastuni < 0) lastuni = 0;
  if (firstuni > lastuni)
    {
      alphabetsz = firstuni;
      firstuni = lastuni;
      lastuni = alphabetsz;
    }

  alphabetsz = (lastuni - firstuni) + 1;
  if (sz == (size_t)(-1)) sz = strlen(buf);
  end = buf + sz;
  *countsp = (size_t *)calloc(alphabetsz*alphabetsz, sizeof(size_t));
  if (*countsp == SIZE_TPN)
    {
      putmsg(MERR + MAE, fn, "Cannot allocate %lu bytes of memory: %s",
             (long)(alphabetsz*alphabetsz*sizeof(size_t)),
             TXstrerror(TXgeterror()));
      goto err;
    }

  for (;;)
    {
      /* Get next bigram.  Note that out-of-range characters "break"
       * a bigram, ie. if we see "x\ny" then bigram `xy' is not counted.
       */
      while ((uni1 = TXunicodeDecodeUtf8Char(&buf, end, 0)) != -2)
        {
          if (igncase && uni1 >= 'A' && uni1 <= 'Z') uni1 += 'a' - 'A';
          if (uni1 >= firstuni && uni1 <= lastuni) break;
        }
    getnext:
      if ((uni2 = TXunicodeDecodeUtf8Char(&buf, end, 0)) == -2) break;
      if (igncase && uni2 >= 'A' && uni2 <= 'Z') uni2 += 'a' - 'A';
      if (uni2 < firstuni || uni2 > lastuni) continue;
      (*countsp)[(uni1 - firstuni)*alphabetsz + (uni2 - firstuni)]++;
      uni1 = uni2;
      totalbigrams++;
      goto getnext;
    }
  goto done;

err:
  totalbigrams = (size_t)(-1);
  if (*countsp != SIZE_TPN)
    {
      free(*countsp);
      *countsp = SIZE_TPN;
    }
done:
  return(totalbigrams);
}

double
TXcomputebigramislang(exparr, actcounts, actbigrams, firstuni, lastuni)
CONST double    *exparr;        /* (in) expected bigram data array */
CONST size_t    *actcounts;     /* (in) actual bigram counts array */
size_t          actbigrams;     /* (in) total actual bigrams */
int             firstuni;       /* (in) first significant Unicode char */
int             lastuni;        /* (in) last significant Unicode char */
/* Computes a factor that indicates the relative chance that the `actcounts'
 * array (from TXgetbigramcounts()) is the language from the `exparr'
 * sample (from TXprintbigramexpected()): the higher the return value,
 * the more likely `actcounts' was from that language.
 * This is the first Ganesan/Sherman test.
 */
{
  double        sum = 0.0, actrate, actbi, exprate;
  CONST double  *expend;
  int           alphabetsz;

  if (firstuni < 0) firstuni = 0;               /* sanity checks */
  if (lastuni < 0) lastuni = 0;
  if (firstuni > lastuni)
    {
      alphabetsz = firstuni;
      firstuni = lastuni;
      lastuni = alphabetsz;
    }
  if (actbigrams <= (size_t)0) return((double)0.0);

  alphabetsz = (lastuni - firstuni) + 1;

  /* Compute the sum (for all possible bigrams) of N*ln(EXP/ACT) where:
   * N   = number of actual occurrences of given bigram
   * EXP = expected probability rate of given bigram
   * ACT = actual rate of given bigram
   */
  actbi = (double)actbigrams;
  for (expend = exparr + alphabetsz*alphabetsz;
       exparr < expend;
       exparr++, actcounts++)
    {
      actrate = (double)(*actcounts)/actbi;
      if (actrate <= (double)0.0) actrate = LOG0_VAL;
      if ((exprate = *exparr) <= (double)0.0) exprate = LOG0_VAL;
      sum += ((double)(*actcounts))*log(exprate/actrate);
    }
  return(sum);
}

double
TXcomputedictwordscore(buf, sz, equivfile, altequivfile, igncase, pmbuf)
CONST char      *buf;           /* (in) UTF-8 buffer to scan */
size_t          sz;             /* (in) size of `buf' in bytes (-1==strlen) */
CONST char      *equivfile;     /* (in) equiv file */
CONST char      *altequivfile;  /* (in) (opt.) alternate equiv file */
int             igncase;        /* (in) nonzero: ignore case in buf/dict */
TXPMBUF         *pmbuf;         /* (in) (opt.) putmsg buffer */
/* Returns word score for UTF-8 buffer `buf': the higher the score,
 * the more it is composed of words found in equiv file `equivfile'
 * or `altequivfile'.
 * Returns -1 on error.
 */
{
  CONST char            *dictbuf = CHARPN, *dictbufe, *bufe;
  size_t                dictbufsz = 0, foundbytes, wdsz;
  size_t                foundwdsz;
  int                   fh = -1, found;
  double                score;
  EQV                   *eqv = EQVPN, *alteqv = EQVPN;
  char                  tmp[128];

  if ((eqv = openeqv((char *)equivfile, APICPPN)) == EQVPN) goto err;
  if (altequivfile != CHARPN &&
      (alteqv = openeqv((char *)altequivfile, APICPPN)) == EQVPN)
    goto err;

  /* Scan the buffer for words: */
  dictbufe = dictbuf + dictbufsz;
  if (sz == (size_t)(-1)) sz = strlen(buf);
  bufe = buf + sz;
  foundbytes = 0;
  while (buf < bufe)                            /* scan the buffer */
    {
      for (wdsz = 1, foundwdsz = 0;
           buf + wdsz <= bufe && wdsz < sizeof(tmp);
           wdsz++)
        {                                       /* find longest word match */
          /* WTF since eqv parser cannot tell us if we prefix-match,
           * we have to look for word separator characters in the raw text.
           * This may prevent a CJK equiv file from working (eg. no sep).
           * Also WTF equiv lookup uses strnicmp() which isn't UTF-8.
           * Also WTF equiv lookup always ignores case:
           */
          if (buf + wdsz < bufe &&
              ((buf[wdsz] >= 'A' && buf[wdsz] <= 'Z') ||
               (buf[wdsz] >= 'a' && buf[wdsz] <= 'z') ||
               ((CONST byte *)buf)[wdsz] >= 0x80))
            continue;
          memcpy(tmp, buf, wdsz);
          tmp[wdsz] = '\0';
          found = (epi_findrec(eqv, tmp, 1) == 0 ||
                   (alteqv != EQVPN && epi_findrec(alteqv, tmp, 1) == 0));
          if (found) foundwdsz = wdsz;
          else break;
        }
      foundbytes += foundwdsz;
      buf += (foundwdsz <= 0 ? 1 : foundwdsz);
    }
  score = ((double)foundbytes)/(double)sz;
  goto done;

err:
  score = (double)(-1.0);
done:
#ifdef EPI_HAVE_MMAP
  if (dictbuf != CHARPN && dictbuf != (char *)MAP_FAILED)
  munmap((caddr_t)dictbuf, dictbufsz);
#else /* !EPI_HAVE_MMAP */
  dictbuf = TXfree((char *)dictbuf);
#endif /* !EPI_HAVE_MMAP */
  if (fh != -1)
    {
      close(fh);
      fh = -1;
    }
  eqv = closeeqv(eqv);
  alteqv = closeeqv(alteqv);
  return(score);
}

/* ------------------------------------------------------------------------ */

TXNGRAMSET *
TXngramsetOpen(TXPMBUF *pmbuf, size_t ngramSz)
/* Opens a TXNGRAMSET object, for N-gram size `ngramSz'.
 * Attaches to `pmbuf'.
 * Returns new object, or NULL on error.
 */
{
  static const char     fn[] = "TXngramsetOpen";
  TXNGRAMSET            *ngramset = NULL;

  if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(ngramSz) || ngramSz > TXNGRAM_MAX_SZ)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "Invalid N-gram size %d",
                     (int)ngramSz);
      goto err;
    }
  ngramset = (TXNGRAMSET *)TXcalloc(pmbuf, fn, 1, sizeof(TXNGRAMSET));
  if (!ngramset) goto err;
  ngramset->pmbuf = txpmbuf_open(pmbuf);
  ngramset->ngramSz = ngramSz;
  ngramset->sqrtSumCountsSquared = 0.0;
  /* rest cleared by calloc() */
  goto done;

err:
  ngramset = TXngramsetClose(ngramset);
done:
  return(ngramset);
}

TXNGRAMSET *
TXngramsetClose(TXNGRAMSET *ngramset)
{
  if (!ngramset) return(TXNGRAMSETPN);

  ngramset->pmbuf = txpmbuf_close(ngramset->pmbuf);
  ngramset->ngrams = TXfree(ngramset->ngrams);
  ngramset->btree = closebtree(ngramset->btree);
  ngramset = TXfree(ngramset);
  return(TXNGRAMSETPN);
}

TXNGRAMSET *
TXngramsetOpenFromFile(TXPMBUF *pmbuf, int ngramSz, const char *path)
{
  static const char     fn[] = "TXngramsetOpenFromFile";
  TXNGRAMSET            *ngramset = NULL;
  FILE                  *fp = NULL;
  char                  *textBuf = NULL, *preppedBuf = NULL;
  size_t                textBufSz, preppedBufSz;
  EPI_STAT_S            st;

  ngramset = TXngramsetOpen(pmbuf, ngramSz);
  if (!ngramset) goto err;
  fp = fopen(path, "rb");
  if (!fp)
    {
      txpmbuf_putmsg(pmbuf, MERR + FRE, fn, "Cannot open file %s: %s",
                     path, TXstrerror(TXgeterror()));
      goto err;
    }
  if (EPI_STAT(path, &st) != 0)
    {
      txpmbuf_putmsg(pmbuf, MERR + FTE, fn, "Cannot stat %s: %s",
                     path, TXstrerror(TXgeterror()));
      goto err;
    }
  textBufSz = (size_t)st.st_size;
  if (st.st_size != (EPI_OFF_T)textBufSz)
    {
      txpmbuf_putmsg(pmbuf, MERR + MAE, fn, "File %s too large", path);
      goto err;
    }
  textBuf = (char *)TXmalloc(pmbuf, fn, textBufSz);
  if (fread(textBuf, 1, textBufSz, fp) != textBufSz)
    {
      txpmbuf_putmsg(pmbuf, MERR + FRE, NULL, "Cannot read from file %s: %s",
                     path, TXstrerror(TXgeterror()));
      goto err;
    }
  fclose(fp);
  fp = NULL;
  if (!TXngramsetPrepText(ngramset, &preppedBuf, &preppedBufSz,
                          textBuf, textBufSz))
    goto err;
  textBuf = TXfree(textBuf);
  textBufSz = 0;
  if (!TXngramsetAddNgramsFromText(ngramset, preppedBuf, preppedBufSz))
    goto err;
  if (!TXngramsetFinish(ngramset)) goto err;
  goto done;

err:
  ngramset = TXngramsetClose(ngramset);
done:
  if (fp)
    {
      fclose(fp);
      fp = NULL;
    }
  textBuf = TXfree(textBuf);
  preppedBuf = TXfree(preppedBuf);
  return(ngramset);
}

int
TXngramsetPrepText(TXNGRAMSET *ngramset, char **destText, size_t *destTextSz,
                   const char *srcText, size_t srcTextSz)
/* Preps `srcText' for N-gram analysis: compresses whitespace, makes all
 * lower-case, etc.  Sets `*destText'/`*destTextSz' to alloced copy of
 * prepped text.  `srcTextSz' may be -1 for strlen(srcText).
 * Returns 0 on error.
 */
{
  static const char     fn[] = "TXngramsetPrepText";
  char                  *s, *d, *destTextEnd;
  size_t                newDestTextSz;
  int                   ret;

  if (srcTextSz == (size_t)(-1)) srcTextSz = strlen(srcText);
  *destTextSz = srcTextSz;
  *destText = (char *)TXmalloc(ngramset->pmbuf, fn, *destTextSz);
  if (!*destText) goto err;

  /* Lower-case fold: */
  memcpy(*destText, srcText, *destTextSz);
  for (;;)
    {
      newDestTextSz = TXunicodeStrFold((char *)*destText, *destTextSz,
                                       (char *)srcText, srcTextSz,
                                       TXCFF_TEXTSEARCHMODE_DEFAULT);
      if (newDestTextSz != (size_t)(-1)) break; /* success */
      *destText = TXfree(*destText);
      *destTextSz += *destTextSz/8 + 16;        /* enlarge a bit */
      *destText = (char *)TXmalloc(ngramset->pmbuf, fn, *destTextSz);
      if (!*destText) goto err;
    }
  *destTextSz = newDestTextSz;

  /* Compress whitespace, punctuation: */
  destTextEnd = *destText + *destTextSz;
  for (s = d = *destText; s < destTextEnd; d++)
    {
      if (TX_ISSPACE(*(byte *)s))
        {
          *d = ' ';
          for (s++; s < destTextEnd && TX_ISSPACE(*(byte *)s); s++);
        }
      else if (TX_ISPUNCT(*(byte *)s))
        {
          *d = '.';
          for (s++; s < destTextEnd && TX_ISPUNCT(*(byte *)s); s++);
        }
      else
        *d = *(s++);
    }
  *destTextSz = d - *destText;

  ret = 1;
  goto done;

err:
  ret = 0;
  *destText = TXfree(*destText);
  *destTextSz = 0;
done:
  return(ret);
}

static int
TXngramsetBtreeCmp(void *a, size_t aLen, void *b, size_t bLen, void *usr)
/* B-tree comparison function for TXNGRAMSET.
 * Compares text of N-grams for sorting by N-gram.
 */
{
  int   cmp;

  cmp = memcmp(a, b, TX_MIN(aLen, bLen));
  if (cmp == 0) cmp = (aLen < bLen ? -1 : (aLen > bLen ? 1 : 0));
  return(cmp);
}

int
TXngramsetAddNgramsFromText(TXNGRAMSET *ngramset, const char *text,
                            size_t textSz)
/* Adds N-grams to `ngramset'from `text', which should be UTF-8 ideally.
 * Call TXngramsetFinish() when done.
 * Returns 0 on error.  `textSz' may be -1 for strlen(text).
 */
{
  const char    fn[] = "TXngramsetAddNgramsFromText";
  const char    *curText, *textEnd, *ngramTextEnd;
  size_t        n;
  int           ret, textIsRemainder = 0, res;
  char          tmpNgramBuf[TXNGRAM_MAX_SZ];

  if (textSz == (size_t)(-1)) textSz = strlen(text);
  textEnd = text + textSz;

  if (ngramset->ngrams)
    {
      txpmbuf_putmsg(ngramset->pmbuf, MERR + UGE, fn,
                     "Error: Cannot add N-grams to finished set");
      goto err;
    }
  if (!ngramset->btree)
    {
      ngramset->btree = openbtree(NULL, BT_MAXPGSZ, 20, 0, O_RDWR | O_CREAT);
      if (!ngramset->btree) goto err;
      btsetcmp(ngramset->btree, TXngramsetBtreeCmp);
    }

  /* Skip last (N-gram-size) - 1 bytes; taken care of below: */
  ngramTextEnd = textEnd - (ngramset->ngramSz - 1);
  if (ngramTextEnd < text) ngramTextEnd = text;

  for (textIsRemainder = 0; textIsRemainder < 2; textIsRemainder++)
    {
      for (curText = text; curText < ngramTextEnd; curText++)
        {
          BTLOC curNgramCountBtloc;

          /* Look up N-gram at `curText' in `ngramset->btree': */
          curNgramCountBtloc = btsearch(ngramset->btree,
                                     (int)ngramset->ngramSz, (void *)curText);
          /* Increment N-gram's count, or insert in tree if not found: */
          if (TXrecidvalid2(&curNgramCountBtloc))
            {                                   /* found: increment count */
              TXsetrecid(&curNgramCountBtloc,
                         TXgetoff2(&curNgramCountBtloc) + 1);
              btupdate(ngramset->btree, curNgramCountBtloc);
              res = 0;                          /* WTF check btupdate() err */
            }
          else                                  /* not found: insert it */
            {
              TXsetrecid(&curNgramCountBtloc, 1);
              res = btinsert(ngramset->btree, &curNgramCountBtloc,
                             ngramset->ngramSz, (void *)curText);
            }
          if (res != 0) goto err;
          ngramset->numNgrams++;
        }

      /* Add remaining bytes with nul appended: */
      if (!textIsRemainder && ngramTextEnd < textEnd)
        {                                       /* have remainder */
          n = textEnd - ngramTextEnd;
          memcpy(tmpNgramBuf, ngramTextEnd, n);
          memset(tmpNgramBuf + n, '\0', sizeof(tmpNgramBuf) - n);
          text = tmpNgramBuf;
          ngramTextEnd = textEnd = tmpNgramBuf + n;
          textIsRemainder = 1;
        }
    }

  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;
done:
  return(ret);
}

int
TXngramsetFinish(TXNGRAMSET *ngramset)
/* "Finishes" `ngramset', after zero or more TXngramsetAddNgramsFromText()
 * calls: preps it for TXngramsetCosineDistance() calls.
 * Returns 0 on error.
 */
{
  static const char     fn[] = "TXngramsetFinish";
  BTLOC                 curNgramCountBtloc;
  TXNGRAM               *curNgram, *ngramsEnd;
  int                   ret;
  size_t                ngramSz;

  /* Dump B-tree `ngramset->btree' into an array for faster use: */
  ngramset->ngrams = TXfree(ngramset->ngrams);
  ngramset->sqrtSumCountsSquared = 0.0;
  if (!ngramset->btree || ngramset->numNgrams <= 0)
    {                                           /* nothing in set */
      ngramset->numNgrams = 0;
      goto ok;
    }
  ngramset->ngrams = (TXNGRAM *)TXmalloc(ngramset->pmbuf, fn,
                                         ngramset->numNgrams*sizeof(TXNGRAM));
  if (!ngramset->ngrams) goto err;

  ngramsEnd = ngramset->ngrams + ngramset->numNgrams;
  rewindbtree(ngramset->btree);
  for (curNgram = ngramset->ngrams; curNgram < ngramsEnd; curNgram++)
    {
      ngramSz = ngramset->ngramSz;
      curNgramCountBtloc = btgetnext(ngramset->btree, &ngramSz,
                                     curNgram->text, NULL);
      if (!TXrecidvalid2(&curNgramCountBtloc)) break;   /* short B-tree */
      curNgram->count = TXgetoff2(&curNgramCountBtloc);
      if (ngramSz != ngramset->ngramSz)         /* should never happen */
        {
          txpmbuf_putmsg(ngramset->pmbuf, MERR + MAE, fn,
                         "Internal error: N-gram size conflict");
          goto err;
        }
      ngramset->sqrtSumCountsSquared +=
        (double)curNgram->count*(double)curNgram->count;
    }
  ngramset->numNgrams = curNgram - ngramset->ngrams;
  ngramset->sqrtSumCountsSquared = sqrt(ngramset->sqrtSumCountsSquared);
ok:
  ret = 1;
  goto finally;

err:
  ret = 0;
  ngramset->ngrams = TXfree(ngramset->ngrams);
  ngramset->numNgrams = 0;
  ngramset->sqrtSumCountsSquared = 0.0;
finally:
  ngramset->btree = closebtree(ngramset->btree);
  return(ret);
}

double
TXngramsetCosineDistance(const TXNGRAMSET *ngramsetA,
                         const TXNGRAMSET *ngramsetB)
/* Returns the cosine distance between `ngramsetA' and `ngramsetB' arrays:
 * treats each array as a vector with each N-gram a separate dimension;
 * the cosine distance is the cosine of the angle between them,
 * i.e. 1 if the vectors are colinear, 0 if perpendicular (the max
 * separation since all N-grams are non-negative).
 * Both arrays must be sorted by text ascending.
 * Returns distince (-1 through 1), -2 if uncomputable (e.g. zero-length
 * vector) or other error.
 */
{
  static const char     fn[] = "TXngramCosineDistance";
  double                dotProduct, ret;
  const TXNGRAM         *curNgramA, *curNgramB, *ngramsAEnd, *ngramsBEnd;
  int                   cmp;

  if (ngramsetA->ngramSz != ngramsetB->ngramSz)
    {
      txpmbuf_putmsg(ngramsetA->pmbuf, MERR + UGE, fn,
                     "Different-size N-gram sets");
      goto err;
    }
  /* We use the relation:  a dot b = |a||b|cos(theta)
   * where theta is the angle between vectors a and b.
   * Thus cos(theta) = (a dot b) / (|a||b|)
   */

  ngramsAEnd = ngramsetA->ngrams + ngramsetA->numNgrams;
  ngramsBEnd = ngramsetB->ngrams + ngramsetB->numNgrams;

  /* Compute a dot b = sum-over-i(a[i] x b[i]): */
  dotProduct = 0.0;
  for (curNgramA = ngramsetA->ngrams, curNgramB = ngramsetB->ngrams;
       curNgramA < ngramsAEnd && curNgramB < ngramsBEnd;
       )
    {
      cmp = memcmp(curNgramA->text, curNgramB->text, ngramsetA->ngramSz);
      if (cmp < 0)                              /* just A exists */
        {
          /* wtf fake a count of 1 for missing B N-gram, to smooth?
           * seems to make things worse...
           */
          curNgramA++;
        }
      else if (cmp > 0)                         /* just B exists */
        {
          /* wtf smooth? */
          curNgramB++;
        }
      else                                      /* both exist; same N-gram */
        {
          dotProduct += (double)curNgramA->count*(double)curNgramB->count;
          curNgramA++;
          curNgramB++;
        }
    }
  /* Finish up the longer array, if unequal lengths: */
#if 0
  for ( ; curNgramA < ngramsAEnd; curNgramA++)
    /* wtf smooth? */;
  for ( ; curNgramB < ngramsBEnd; curNgramB++)
    /* wtf smooth? */;
#endif /* 0 */
  
  /* Compute cosine distance: */
  ret = ngramsetA->sqrtSumCountsSquared*ngramsetB->sqrtSumCountsSquared;
  if (ret <= 0.0) goto err;                     /* would be divide by 0 */
  ret = dotProduct / ret;
  goto done;

err:
  ret = -2.0;
done:
  return(ret);
}

double
TXngramsetIdentifyLanguage(TXPMBUF *pmbuf,const TXNGRAMSETLANG *ngramsetlangs,
                           const char *text, size_t textSz,
                           TXNGRAMSETLANG **langIdent)
/* Identifies the language of `text' using `ngramsetLangs' (NULL-terminated).
 * Returns the probability (0.0 - 1.0) that `text' is the identified language,
 * which is returned in `*langIdent' (optional); or -1 on error.
 */
{
  size_t                ngramSz, preppedTextSz = 0;
  TXNGRAMSET            *textNgramset = NULL;
  char                  *preppedText = NULL;
  double                ret, bestLangProb, prob;
  const TXNGRAMSETLANG  *ngramsetlang, *bestLang;

  /* Build a TXNGRAMSET for `text': */
  ngramSz = (ngramsetlangs && ngramsetlangs[0].ngramset ?
             ngramsetlangs[0].ngramset->ngramSz : 3);
  textNgramset = TXngramsetOpen(pmbuf, ngramSz);
  if (!textNgramset) goto err;
  if (!TXngramsetPrepText(textNgramset, &preppedText, &preppedTextSz,
                          text, textSz))
    goto err;
  if (!TXngramsetAddNgramsFromText(textNgramset, preppedText, preppedTextSz))
    goto err;
  preppedText = TXfree(preppedText);
  preppedTextSz = 0;
  if (!TXngramsetFinish(textNgramset)) goto err;

  /* See which language matches the best: */
  bestLang = NULL;
  bestLangProb = -1.0;
  for (ngramsetlang = ngramsetlangs; ngramsetlang->ngramset; ngramsetlang++)
    {
      prob = TXngramsetCosineDistance(textNgramset, ngramsetlang->ngramset);
      if (prob > bestLangProb)
        {
          bestLangProb = prob;
          bestLang = ngramsetlang;
        }
    }
  if (!bestLang) goto err;                      /* no languages given */
  if (langIdent) *langIdent = (TXNGRAMSETLANG *)bestLang;
  ret = bestLangProb;
  goto done;

err:
  ret = -1.0;
  if (langIdent) *langIdent = NULL;
done:
  preppedText = TXfree(preppedText);
  preppedTextSz = 0;
  textNgramset = TXngramsetClose(textNgramset);
  return(ret);
}

int
TXsqlFuncIdentifylanguage(FLD *textFld, FLD *langFld, FLD *sampleSizeFld)
/* SQL function identifylanguage(text[, lang[, sampleSize]]): SQL return is
 * probability (0-1) that `textFld' is (optional) language `langFld',
 * and identified language (`langFld' if given, non-empty and known),
 * as 2-element strlst.  `sampleSizeFld', if given, non-empty and nonzero,
 * is the leading size of `textFld' to sample; default TX_LANG_IDENT_BUF_SZ;
 * -1 for entire `textFld'.
 * Returns 0 if ok, else < 0 on error.
 */
{
  static const char     fn[] = "TXsqlFuncIdentifylanguage";
  HTBUF                 *buf = NULL;
  int                   ret = 0;
  char                  *text, *lang = NULL, *strlstData = NULL;
  size_t                textSz, langSz = 0, strlstSz = 0;
  size_t                sampleSize = TX_LANG_IDENT_BUF_SZ;
  TXPMBUF               *pmbuf = NULL;
  const TXNGRAMSETLANG  *ngramsetlangs, *nl;
  TXNGRAMSETLANG        tmpNgramsetlangList[2], *langIdent = NULL;
  double                prob = -1.0;
  char                  probBuf[32];

  if (!textFld ||
      !(text = (char *)getfld(textFld, &textSz)) ||
      (textFld->type & DDTYPEBITS) != FTN_CHAR)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
                     "Text field missing or wrong type");
      goto err;
    }

  if (!TXngramsetlangs[0].ngramset)
    {
      /* Failed to replace langNgramsDummy.o with langNgrams.o: at build */
      txpmbuf_putmsg(pmbuf, MERR, fn,
                     "Internal error: TXngramsetlangs not populated");
      goto err;
    }

  /* Use all languages, or just the one given by `langFld' if present: */
  if (langFld)
    {
      if (!(lang = (char *)getfld(langFld, &langSz)) ||
          (langFld->type & DDTYPEBITS) != FTN_CHAR)
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
                         "Language field missing data or wrong type");
          goto err;
        }
      if (langSz > 0 && *lang != '\0')          /* if non-empty language */
        {
          for (nl = TXngramsetlangs; nl->ngramset; nl++)
            if (strcmpi(nl->langCode, lang) == 0) break;
          if (!nl->ngramset)                    /* unknown language */
            {
              prob = -1.0;
              langIdent = NULL;
              goto setRet;
            }
          tmpNgramsetlangList[0] = *nl;
          memset(&tmpNgramsetlangList[1], 0, sizeof(TXNGRAMSETLANG));
          ngramsetlangs = tmpNgramsetlangList;
        }
      else                                      /* empty language */
        {
          lang = NULL;
          ngramsetlangs = TXngramsetlangs;
        }
    }
  else                                          /* no language given */
    {
      lang = NULL;
      ngramsetlangs = TXngramsetlangs;
    }      

  /* Get the sample size, if given: */
  if (sampleSizeFld)
    {
      ft_int64  *sampleSizeData;
      size_t    sampleSizeSz;

      sampleSizeData = (ft_int64 *)getfld(sampleSizeFld, &sampleSizeSz);
      if (!sampleSizeData ||
          (sampleSizeFld->type & DDTYPEBITS) != FTN_INT64)
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
                         "Sample-size field missing data or wrong type");
          goto err;
        }
      if (sampleSizeSz > 0 && *sampleSizeData != (ft_int64)0)
        {
          if (*sampleSizeData < (ft_int64)0     /* -1 == entire `text' */
#if EPI_OS_SIZE_T_BITS < 64
              || *sampleSizeData > (ft_int64)EPI_OS_SIZE_T_MAX
#endif /* EPI_OS_SIZE_T_BITS < 64 */
              )
            sampleSize = (size_t)EPI_OS_SIZE_T_MAX;
          else
            sampleSize = (size_t)(*sampleSizeData);
        }
    }

  /* Get the language probability: */
  prob = TXngramsetIdentifyLanguage(pmbuf, ngramsetlangs, text,
                                    TX_MIN(sampleSize, textSz), &langIdent);

  /* Set the return value: */
setRet:
  if (!(buf = openhtbuf())) goto err;
  if (!TXstrlstBufBegin(buf)) goto err;
  htsnpf(probBuf, sizeof(probBuf), "%1.6lf", prob);
  if (!TXstrlstBufAddString(buf, probBuf, -1)) goto err;
  if (!TXstrlstBufAddString(buf, (langIdent ? langIdent->langCode : "?"), -1))
    goto err;
  if (!TXstrlstBufEnd(buf)) goto err;
  strlstSz = htbuf_getdata(buf, &strlstData, 0x3);
  /* Return value goes into first argument, i.e. `textFld': */
  releasefld(textFld);                          /* before changing its type */
  textFld->type = (FTN_STRLST | DDVARBIT);
  textFld->elsz = TX_STRLST_ELSZ;
  setfldandsize(textFld, strlstData, TX_STRLST_ELSZ*strlstSz + 1, FLD_FORCE_NORMAL);
  strlstData = NULL;                            /* owned by `textFld' now */
  strlstSz = 0;
  ret = 0;                                      /* success */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  buf = closehtbuf(buf);
  strlstData = TXfree(strlstData);
  return(ret);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
#ifdef STANDALONE
static void TXprintbigramexpected ARGS((CONST size_t *counts,
       size_t totbigrams, int firstuni, int lastuni, int igncase,
       CONST char *varpfx));
static void
TXprintbigramexpected(counts, totbigrams, firstuni, lastuni, igncase, varpfx)
CONST size_t    *counts;        /* (in) bigram counts */
size_t          totbigrams;     /* (in) total bigrams found */
int             firstuni;       /* (in) first significant Unicode char */
int             lastuni;        /* (in) last significant Unicode char */
int             igncase;        /* (in) nonzero: ignore case */
CONST char      *varpfx;        /* (in) variable name prefix */
/* Prints `counts' array (from TXgetbigramcounts()) as a static C array
 * of logs of probabilities, for later usage as `exparr' parameter
 * to TXcomputebigramislang().  Header is printed to stderr.
 */
{
  int           uni1, uni2, i, j, alphabetsz;
  double        prob;
  char          tmp[64];
#  define DEC           10
#  define UNICHAR(u)    ((u) >= ' ' && (u) <= '~' ? (u) : '?')

  if (firstuni < 0) firstuni = 0;               /* sanity checks */
  if (lastuni < 0) lastuni = 0;
  if (firstuni > lastuni)
    {
      alphabetsz = firstuni;
      firstuni = lastuni;
      lastuni = alphabetsz;
    }

  alphabetsz = (lastuni - firstuni) + 1;

  fprintf(stderr,
      "/* >>>>> DO NOT EDIT: this file generated from text files <<<<< */\n\n"
         );
  fprintf(stderr, "#define %s_FIRSTUNICODE  %3d\n", varpfx, firstuni);
  fprintf(stderr, "#define %s_LASTUNICODE   %3d\n", varpfx, lastuni);
  fprintf(stderr, "#define %s_IGNORECASE    %3d\n", varpfx, igncase);
  fprintf(stderr,
     "#define %s_ALPHABETSZ     ((%s_LASTUNICODE - %s_FIRSTUNICODE) + 1)\n\n",
         varpfx, varpfx, varpfx);
  fprintf(stderr, "extern const double %s_exparr[];\n", varpfx);

  printf(
      "/* >>>>> DO NOT EDIT: this file generated from text files <<<<< */\n\n"
         );
  printf("/* array of %s_ALPHABETSZ*%s_ALPHABETSZ doubles: */\n",
         varpfx, varpfx);
  printf("const double  %s_exparr[%u] =\n{ /*\n",
         varpfx, (unsigned)alphabetsz*alphabetsz);
  printf("1st-v 2nd:");
  for (j = 0; j < alphabetsz; j++)
    {
      uni2 = j + firstuni;
      printf(" %c %*s", UNICHAR(uni2), DEC, "");
    }
  printf(" */\n");
  for (i = 0; i < alphabetsz; i++)
    {
      uni1 = i + firstuni;
      printf(" /* %c */  ", UNICHAR(uni1));
      for (j = 0; j < alphabetsz; j++, counts++)
        {
          uni2 = j + firstuni;
          prob = ((double)(*counts))/(double)totbigrams;
          sprintf(tmp, "%10g,", prob);
          printf(" %-*s", DEC+2, tmp + strspn(tmp, " \t\r\n\v\f"));
        }
      printf("\n");
    }
  printf("};\n");
#undef DEC
#undef UNICHAR
}

#ifdef WITH_EMPTY_ARRAY
const double EXPARR[1] = { 0.0 };
#  define FIRSTUNICODE  32
#  define LASTUNICODE   32
#  define IGNORECASE    1
#else
#  include BIGRAM_INCLUDE
#endif /* WITH_EMPTY_ARRAY */

/* - - - - - - - - - - - - func dups to avoid lib link - - - - - - - - - - */

/* STANDALONE */ EPIPUTMSG(/* int n, CONST char *fn, CONST char *fmt,
                              va_list args */)
{
  fprintf(stderr, "%03d ", n);
  vfprintf(stderr, fmt, args);
  if (fn != CHARPN) fprintf(stderr, " in the function %s", fn);
  fprintf(stderr, "\n");
  fflush(stderr);
  va_end(args);
  return(0);
}
}

int CDECL
#ifdef EPI_HAVE_STDARG
/* STANDALONE */ txpmbuf_putmsg(TXPMBUF *pmbuf, int num, CONST char *fn,
                                CONST char *fmt, ...)
{
  va_list               argp;

  va_start(argp, fmt);
#else /* !EPI_HAVE_STDARG */
/* STANDALONE */ txpmbuf_putmsg(va_alist)
va_dcl
{
  va_list               argp;
  TXPMBUF               *pmbuf;
  int                   num;
  CONST char            *fn, *fmt;

  va_start(argp);
  pmbuf = va_arg(argp, TXPMBUF *);
  num = va_arg(argp, int);
  fn = va_arg(argp, CONST char *);
  fmt = va_arg(argp, CONST char *);
#endif /* !EPI_HAVE_STDARG */
  putmsg(num, fn, "An error occurred");
  va_end(argp);
  return(0);
}

int
/* STANDALONE */ TXutf8strnicmp(a, alen, b, blen)
CONST char      *a;
size_t          alen;
CONST char      *b;
size_t          blen;
{
  CONST char    *ae, *be;
  byte          ac, bc;
  int           cmp, aeof, beof;

  if (alen == (size_t)(-1)) ae = (CONST char *)EPI_OS_VOIDPTR_MAX;
  else ae = a + alen;
  if (blen == (size_t)(-1)) be = (CONST char *)EPI_OS_VOIDPTR_MAX;
  else be = b + blen;

  for (cmp = 0; cmp == 0; a++, b++)
    {
      if (!(aeof = (a >= ae || *a == '\0')))
        {
          ac = *(byte *)a;
          if (ac >= 'A' && ac <= 'Z') ac += 'a' - 'A';
        }
      else
        ac = 0;
      if (!(beof = (b >= be || *b == '\0')))
        {
          bc = *(byte *)b;
          if (bc >= 'A' && bc <= 'Z') bc += 'a' - 'A';
        }
      else
        bc = 0;
      cmp = ((int)ac - (int)bc);
      if (aeof && beof) break;
    }

  return(cmp);
}

int
/* STANDALONE */ htutf8decodechar(sp, end)
CONST char      **sp;
CONST char      *end;
{
  CONST byte    *s;
  int           ch, sv;

  s = (CONST byte *)(*sp);
  if (s >= (CONST byte *)end) return(-2);
  sv = *(s++);
  if (!(sv & 0x80))                                     /* 1 byte */
    {
      ch = sv;
      goto done;
    }
  if (!(sv & 0x40)) goto skip;                          /* illegal */
  if (s >= (CONST byte *)end) return(-2);               /* short buffer */
  if ((*s & 0xC0) != 0x80) goto skip;
  ch = (*(s++) & 0x3F);
  if (!(sv & 0x20))                                     /* 2 byte sequence */
    {
      ch |= ((sv & 0x1F) << 6);
      if (ch < 0x80) goto bad;                          /* over-encoded */
      goto done;
    }
  if (s >= (CONST byte *)end) return(-2);               /* short buffer */
  if ((*s & 0xC0) != 0x80) goto skip;
  ch = ((ch << 6) | (*(s++) & 0x3F));
  if (!(sv & 0x10))                                     /* 3 byte sequence */
    {
      ch |= ((sv & 0xF) << 12);
      if (ch < 0x800) goto bad;                         /* over-encoded */
      goto done;
    }
  if (s >= (CONST byte *)end) return(-2);               /* short buffer */
  if ((*s & 0xC0) != 0x80) goto skip;
  ch = ((ch << 6) | (*(s++) & 0x3F));
  if (!(sv & 0x8))                                      /* 4 byte sequence */
    {
      ch |= ((sv & 0x7) << 18);
      if (ch < 0x10000) goto bad;                       /* over-encoded */
      goto done;
    }
  if (s >= (CONST byte *)end) return(-2);               /* short buffer */
  if ((*s & 0xC0) != 0x80) goto skip;
  ch = ((ch << 6) | (*(s++) & 0x3F));
  if (!(sv & 0x4))                                      /* 5 byte sequence */
    {
      ch |= ((sv & 0x3) << 24);
      if (ch < 0x200000) goto bad;                      /* over-encoded */
      goto done;
    }
  if (s >= (CONST byte *)end) return(-2);               /* short buffer */
  if ((*s & 0xC0) != 0x80) goto skip;
  ch = ((ch << 6) | (*(s++) & 0x3F));
  if (!(sv & 0x2))                                      /* 6 byte sequence */
    {
      ch |= ((sv & 0x1) << 30);
      if (ch < 0x4000000) goto bad;                     /* over-encoded */
      goto done;
    }
  /* illegal */
  s = (CONST byte *)(*sp + 1);
skip:
  /* Seek to next 7-bit (ASCII) byte, or next UTF-8 start byte, but no
   * more than max UTF-8 sequence length (so long garbage input produces
   * proportional long garbage output `?' list):
   */
  while (s < (CONST byte *)end && ((*s & 0xC0) == 0x80) &&
         (s - (CONST byte *)(*sp) < 6))
    s++;
bad:
  ch = -1;

done:
  *sp = (CONST char *)s;
  return(ch);
}

/* ------------------------------------------------------------------------ */

int
/* STANDALONE */ main(argc, argv)
int     argc;
char    *argv[];
/* Prints exparr bigram array named `varpfx_...' from stdin data,
 * for first/last Unicode numbers `n1'/`n2'.
 */
{
  static CONST char     fn[] = "main";
  char                  *buf = CHARPN;
  size_t                sz, totbigrams, *counts;
  int                   firstuni, lastuni, igncase;
#define SZ              (100*1024*1024)

  if (argc < 2)
    {
    usage:
      fprintf(stderr,
             "Usage: %s exparr n1 n2 igncase varpfx <text >out.c 2>out.h\n"
             "          Generate expected bigram data from (English) text\n"
             "or:    %s analyze <text\n"
             "          Analyze text and print English-ness factor\n",
             argv[0], argv[0]);
      goto err;
    }

  buf = (char *)malloc(SZ);
  if (buf == CHARPN)
    {
      putmsg(MERR + MAE, fn, "Cannot allocate %lu bytes of memory: %s",
             (long)SZ, strerror(errno));
      goto err;
    }
  sz = fread(buf, 1, SZ, stdin);
  if (sz >= SZ)
    {
      putmsg(MERR + MAE, fn, "Input too large");
      goto err;
    }

  if (strcmp(argv[1], "exparr") == 0)
    {
      if (argc < 6) goto usage;
      firstuni = atoi(argv[2]);
      lastuni = atoi(argv[3]);
      igncase = atoi(argv[4]);
      totbigrams = TXgetbigramcounts(buf, sz, firstuni, lastuni, igncase,
                                     &counts);
      if (totbigrams == (size_t)(-1)) goto err;
      TXprintbigramexpected(counts, totbigrams, firstuni, lastuni, igncase,
                            argv[5]);
    }
  else if (strcmp(argv[1], "analyze") == 0)
    {
      totbigrams = TXgetbigramcounts(buf, sz, FIRSTUNICODE, LASTUNICODE,
                                     IGNORECASE, &counts);
      if (totbigrams == (size_t)(-1)) goto err;
      printf("%1.10f\n", TXcomputebigramislang(EXPARR, counts, totbigrams,
                                FIRSTUNICODE, LASTUNICODE));
    }
  else
    {
      putmsg(MERR + UGE, fn, "Unknown action `%s'", argv[1]);
      goto err;
    }
  return(0);
err:
  return(1);
}
#endif /* STANDALONE */
