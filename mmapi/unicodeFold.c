/* Decodes one character from string `src' (advancing it), and folds
 * and encodes into (possibly multiple) `dest' characters (advancing it).
 * NOTE: assumes `src' < `srcEnd' (and `*src' != '\0' if applicable) already.
 * #include'd by unicode.c several times, with these macros set/unset:
 *   DO_SRC_BACKWARDS           Read from `src' backwards instead of forwards;
 *                              `srcEnd' is interpreted as start of source
 *   DO_TXCFF_ISO88591          Assume TXCFF_ISO88591 set
 *   DO_LEAVE_TXUNICHARS        Do final fold in-place in `foldCharsBuf'
 *                              instead of encoding to `dest'.
 * Code is #include instead of function for in-line speed; #include instead
 * of macro for debugability (distinct line numbers for stepping).
 * Also assumes following declarations:
 */
#if 0
  TXCFF                 mode, modeFlags;        /* (in) mode and flags */
  CONST char            *src;                   /* (in/out) source data */
  CONST char            *srcEnd;                /* (in) end of `src' */
  char                  *dest;                  /* (out) final encoded data */
  char                  *destEnd;               /* (in) alloc'd end of "" */
  /* Note: `prevCharIsWhiteSpace' is initialized to 1, but only at start of
   * overall string, not for each character:
   */
  int                   prevCharIsWhiteSpace = 1;/*(in/out) prev. whitespace*/
destShort:                                      /* jump-to for short `dest' */
  /* these are scratch, unless DO_LEAVE_TXUNICHARS set (see comments): */
  TXUNIFOLDCHAR         foldCharsBuf[EXP_LEN_MAX]; /* (out) dest. for chars */
#define foldCharsBufEnd (foldCharsBuf+EXP_LEN_MAX) /* (in) alloc'd end of ""*/
  TXUNIFOLDCHAR         *foldCharsStart;        /* (out) start of fold data */
  TXUNIFOLDCHAR         *foldCharsEnd;          /* (out) end of fold data */
  /* these vars are always scratch: */
  TXUNIFOLDCHAR         *uniDest;               /* scratch */
  CONST TXUNIFOLDCHAR   *uniSrc;                /* scratch */
  CONST TXUNICHAR       *uniSrc2;               /* scratch */
  CONST TXUNIFOLD       *uni;                   /* scratch */
  TXUNICHAR             srcCh;                  /* scratch */
  int                   l, r, i;                /* scratch */
  CONST char            *saveSrc;               /* scratch */
  TXUNICHAR             tmpChars[TX_UNICODE_CHAR_FOLD_LEN_MAX]; /* scratch */
  TXUNICHAR             *te;                    /* scratch */
multiUtf8:                                      /* *local* label */
utf8CopyN:                                      /* *local* label */
lowerCtype:                                     /* *local* label */
upperCtype:                                     /* *local* label */
doLower:                                        /* *local* label */
#endif /* 0 */

/* ------------------------------- Local macros --------------------------- */

#ifdef DO_SRC_BACKWARDS
#  define GET_BYTE(src) *(CONST byte *)(--(src))
#else /* !DO_SRC_BACKWARDS */
#  define GET_BYTE(src) *(CONST byte *)((src)++)
#endif /* !DO_SRC_BACKWARDS */

#if TX_UNICODE_CHAR_FOLD_LEN_MAX != 4
  error check loop-unrolling assumption in COPY_TO_UNIFOLD, COPY_TO_BYTE;
#endif
/* srcUniCharPtr: (incremented) TXUNICHAR pointer to char src; nul-term.
 * srcFoldOrg:     single TXUNIFOLD pointer to orgSrcChar etc. source
 */
#define COPY_TO_UNIFOLD(destFoldPtr, destTest, srcUniCharPtr, srcFoldOrg) \
  destTest;                                                               \
  (destFoldPtr)->foldedChar = *((srcUniCharPtr)++);                       \
  (destFoldPtr)->orgSrcChar = (srcFoldOrg)->orgSrcChar;                   \
  ((destFoldPtr)++)->flags = (srcFoldOrg)->flags;                         \
  if (*srcUniCharPtr == 0) continue;                                      \
  destTest;                                                               \
  (destFoldPtr)->foldedChar = *((srcUniCharPtr)++);                       \
  (destFoldPtr)->orgSrcChar = (srcFoldOrg)->orgSrcChar;                   \
  ((destFoldPtr)++)->flags = (srcFoldOrg)->flags;                         \
  if (*srcUniCharPtr == 0) continue;                                      \
  destTest;                                                               \
  (destFoldPtr)->foldedChar = *((srcUniCharPtr)++);                       \
  (destFoldPtr)->orgSrcChar = (srcFoldOrg)->orgSrcChar;                   \
  ((destFoldPtr)++)->flags = (srcFoldOrg)->flags;                         \
  if (*srcUniCharPtr == 0) continue;                                      \
  destTest;                                                               \
  (destFoldPtr)->foldedChar = *((srcUniCharPtr)++);                       \
  (destFoldPtr)->orgSrcChar = (srcFoldOrg)->orgSrcChar;                   \
  ((destFoldPtr)++)->flags = (srcFoldOrg)->flags

#define COPY_TO_BYTE(destBytePtr, destTest, srcUniCharPtr)      \
  destTest;                                                     \
  *(byte *)((destBytePtr)++) = *(srcUniCharPtr++);              \
  if (*srcUniCharPtr == 0) continue;                            \
  destTest;                                                     \
  *(byte *)((destBytePtr)++) = *(srcUniCharPtr++);              \
  if (*srcUniCharPtr == 0) continue;                            \
  destTest;                                                     \
  *(byte *)((destBytePtr)++) = *(srcUniCharPtr++);              \
  if (*srcUniCharPtr == 0) continue;                            \
  destTest;                                                     \
  *(byte *)((destBytePtr)++) = *(srcUniCharPtr++)

/* ------------------------------------------------------------------------ */

#ifdef UNICODE_SANITY_CHECKS
  memset(foldCharsBuf, -1, sizeof(foldCharsBuf));
#endif /* UNICODE_SANITY_CHECKS */

  /* Get the source character from `src': */
#ifdef DO_TXCFF_ISO88591
  srcCh = GET_BYTE(src);
#else /* !DO_TXCFF_ISO88591 */
#  ifdef DO_SRC_BACKWARDS
  if (((CONST byte *)src)[-1] < 0x80)           /* optimization: save call */
#  else /* !DO_SRC_BACKWARDS */
  if (*(CONST byte *)src < 0x80)                /* optimization: save call */
#  endif /* !DO_SRC_BACKWARDS */
    srcCh = GET_BYTE(src);
  else
    {
      saveSrc = src;                            /* bookmark in case failure */
      /* Note that some #include's of this assume `srcEnd'- *or* nul-
       * termination of `src'.  Although caller checked already,
       * we might hit nul in 2nd+ bytes of a UTF-8 sequence here.
       * This is ok, because that would be invalid and thus stop the
       * sequence (and TXunicodeDecodeUtf8Char()'s bad-seq. skip):
       */
#  ifdef DO_SRC_BACKWARDS
      if ((srcCh = TXunicodeDecodeUtf8CharBackwards(&src, srcEnd)) < 0)
#  else /* !DO_SRC_BACKWARDS */
      if ((srcCh = TXunicodeDecodeUtf8Char(&src, srcEnd, 1)) < 0)
#  endif /* !DO_SRC_BACKWARDS */
        {                                       /* bad UTF-8 or short `src' */
          /* We ignore TXunicodeDecodeUtf8Char()'s `src' skip on
           * invalid char, because we must have a character for
           * compare; restore `src' and assume ISO-8859-1:
           */
          src = saveSrc;                        /* restore */
          srcCh = GET_BYTE(src);                /* assume ISO-8859-1 */
        }
    }
#endif /* !DO_TXCFF_ISO88591 */

  /* Order of application of flags has some dependencies; see TXCFF
   * enum comments in unicode.h.
   * NOTE: if order of application changes, see blockers script
   * and TXtxupmOpen().
   * NOTE: revTable assumes that all but last folding (TXCFF_CASESTYLE_...)
   * do not change, ie. they are either applied or not; only the
   * last folding varies.
   * We accumulate mapped characters in `foldCharsBuf', re-mapping
   * as needed and right- or left-justifying the array each time
   * to make room for potential expansion:
   */
  foldCharsEnd = foldCharsStart = foldCharsBuf;
  foldCharsEnd->orgSrcChar = srcCh;
  if ((modeFlags & TXCFF_EXPDIACRITICS) &&
      srcCh <= (int)TX_UNICODE_EXPDIACRITICS_LINEAR_CODEPOINT_MAX)
    {
      uni = &TxUnicodeExpDiacriticsLinear[srcCh];
#if TX_UNICODE_EXPDIACRITICS_REV_FOLD_LEN_MIN != 2 || TX_UNICODE_EXPDIACRITICS_REV_FOLD_LEN_MAX != 2
      error; we assume expdiacritics is all 2-char folds, for loop-unrolling;
#endif
      foldCharsEnd->foldedChar = uni->foldedChars[0];
      foldCharsEnd->flags = ((uni->numFoldedChars != 1 ||
                              uni->foldedChars[0] != srcCh) ? 
                             TUFF_FROMEXPDIACRITICS : (TUFF)0);
      foldCharsEnd++;
      if (uni->numFoldedChars > 1)
        {
          foldCharsEnd->orgSrcChar = srcCh;
          foldCharsEnd->foldedChar = uni->foldedChars[1];
          /* expdiacritics' 2nd char is currently the only optional
           * char in all TXCFF... foldings:
           */
          foldCharsEnd->flags = (TUFF_ISOPTIONAL | TUFF_FROMEXPDIACRITICS);
          foldCharsEnd++;
        }
    }
  else
    {
      foldCharsEnd->foldedChar = srcCh;
      foldCharsEnd->flags = (TUFF)0;
      foldCharsEnd++;
    }
  /* `foldCharsBuf' is now left-justified */

  if (modeFlags & TXCFF_IGNDIACRITICS)
    {                                           /* 1-to-1 map; do in place */
      for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
        {
          if (uniDest->foldedChar <= 
              (int)TX_UNICODE_IGNDIACRITICS_LINEAR_CODEPOINT_MAX)
            {
              if (uniDest->foldedChar !=
                  TxUnicodeIgnDiacriticsLinear[uniDest->foldedChar])
                {
                  uniDest->foldedChar =
                    TxUnicodeIgnDiacriticsLinear[uniDest->foldedChar];
                  uniDest->flags |= TUFF_FROMIGNDIACRITICS;
                }
            }
          else
            {
              BINARY_SEARCH(TxUnicodeIgnDiacriticsBinary, uniDest->foldedChar,
                            l, r, i, uni, ->srcChar,
                            {uniDest->foldedChar = uni->foldedChars[0];
                           uniDest->flags |= TUFF_FROMIGNDIACRITICS; break;});
              /* if we get here, it was not found or was already mapped */
            }
        }
    }
  /* `foldCharsBuf' is now left-justified */

  if (modeFlags & TXCFF_EXPLIGATURES)
    {
      /* Apply TXCFF_EXPLIGATURES mapping; since it is 1-to-N, we go
       * from left- to right-justification to allow for expansion:
       */
      uniDest = foldCharsBufEnd;
      for (uniSrc = foldCharsEnd - 1; uniSrc >= foldCharsStart; uniSrc--)
        {
#ifdef DO_TXCFF_ISO88591
          if (uniSrc->foldedChar == TxUnicodeExpLigaturesBinary[0].srcChar)
            uni = &TxUnicodeExpLigaturesBinary[0];
          else if (uniSrc->foldedChar==TxUnicodeExpLigaturesBinary[1].srcChar)
            uni = &TxUnicodeExpLigaturesBinary[1];
          else if (uniSrc->foldedChar==TxUnicodeExpLigaturesBinary[2].srcChar)
            uni = &TxUnicodeExpLigaturesBinary[2];
#  if TX_UNICODE_EXPLIGATURES_BINARY_CODEPOINT_INDEX3 <= 0xff
          error: we assume remaining TXCFF_EXPLIGATURES source codepoints
          are > U+00FF;
#  endif
          else                                  /* no mapping: copy as-is */
            {
              *(--uniDest) = *uniSrc;
              continue;
            }
          uniDest -= uni->numFoldedChars;
          for (i = 0; i < uni->numFoldedChars; i++)
            {
              uniDest[i].orgSrcChar = uniSrc->orgSrcChar;
              uniDest[i].foldedChar = uni->foldedChars[i];
              uniDest[i].flags = uniSrc->flags;
            }
#else /* !DO_TXCFF_ISO88591 */
          /* Optimization: only bother to binary-search if >= min: */
          if (uniSrc->foldedChar >= TxUnicodeExpLigaturesBinary[0].srcChar)
            {
              BINARY_SEARCH(TxUnicodeExpLigaturesBinary, uniSrc->foldedChar,
                            l, r, i, uni, ->srcChar, goto utf8CopyN);
              /* if we get here, `uniSrc->foldedChar' not found; copy as-is:*/
              *(--uniDest) = *uniSrc;
              continue;
            utf8CopyN:
              uniDest -= uni->numFoldedChars;
              for (i = 0; i < uni->numFoldedChars; i++)
                {
                  uniDest[i].orgSrcChar = uniSrc->orgSrcChar;
                  uniDest[i].foldedChar = uni->foldedChars[i];
                  uniDest[i].flags = uniSrc->flags;
                }
            }
          else                                  /* no mapping: copy as-is */
            *(--uniDest) = *uniSrc;
#endif /* !DO_TXCFF_ISO88591 */
          SANITY_ASSERT(uniDest >= uniSrc && uniDest >= foldCharsBuf);
        }
      /* `foldCharsBuf' is now right-justified: */
      foldCharsStart = uniDest;
      foldCharsEnd = foldCharsBufEnd;
    }
  /* `foldCharsBuf' is now left OR right-justified */

#ifdef DO_TXCFF_ISO88591
#  ifdef UNICODE_SANITY_CHECKS
  for (uniSrc = foldCharsStart; uniSrc < foldCharsEnd; uniSrc++)
    if ((unsigned)uniSrc->foldedChar > 0xff)
      putmsg(MERR, CHARPN, "Out-of-ISO-8859-1-range mapped char at %s:%d",
             __FILE__, __LINE__);
#  endif /* UNICODE_SANITY_CHECKS */

#  if TX_UNICODE_IGNWIDTH_BINARY_CODEPOINT_MIN <= 0xff
  error: we assume all TXCFF_IGNWIDTH source codepoints are > U+00FF;
#  endif
#else /* !DO_TXCFF_ISO88591 */
  if (modeFlags & TXCFF_IGNWIDTH)
    {                                           /* 1-to-1 map: do in place */
      /* We don't know if `foldCharsBuf' is left- or right-justified
       * at this point, but fortunately that does not matter,
       * because the TXCFF_IGNWIDTH mapping is 1-to-1 and can be
       * done in-place:
       */
      for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
        {
          if (uniDest->foldedChar >= TX_UNICODE_IGNWIDTH_BINARY_CODEPOINT_MIN)
            {
              BINARY_SEARCH(TxUnicodeIgnWidthBinary, uniDest->foldedChar,
                            l, r, i, uni, ->srcChar,
                         {uniDest->foldedChar = uni->foldedChars[0];
                           /* ->orgSrcChar, ->flags do not need copying:
                            * 1-to-1, no justification change, so
                            * `uniDest' == `uniSrc' (if latter were used)
                            */
                           break;});
              /* if we get here, it was not found or was already mapped */
            }
        }
    }
#endif /* !DO_TXCFF_ISO88591 */
  /* `foldCharsBuf' is now left OR right-justified */

  switch (TXCFF_GET_CASESTYLE(modeFlags))
    {
    case TXCFF_CASESTYLE_IGNORE: /* - - - - - - - - - - - - - - - - - - - - */
      switch (mode)
        {
        case TXCFF_CASEMODE_CTYPE:
          /* ignorecase same as lowercase for monobyte ctype: */
          goto lowerCtype;
        case TXCFF_CASEMODE_UNICODEMONO:
#ifdef DO_TXCFF_ISO88591
          for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
            {
              SANITY_ASSERT((unsigned)uniDest->foldedChar <= (unsigned)0xff);
#  ifdef DO_LEAVE_TXUNICHARS
              uniDest->foldedChar =
                TxUnicodeIgnCaseMonoIso88591[uniDest->foldedChar];
#  else /* !DO_LEAVE_TXUNICHARS */
              if (dest >= destEnd) goto destShort;
              *(byte *)(dest++) =
                TxUnicodeIgnCaseMonoIso88591[uniDest->foldedChar];
#  endif /* !DO_LEAVE_TXUNICHARS */
            }
          break;
#else /* !DO_TXCFF_ISO88591 */
          for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
            /* Optimization: use array to avoid function call: */
            uniDest->foldedChar = (uniDest->foldedChar <=
                              TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX ?
                              TxUnicodeIgnCaseMonoLinear[uniDest->foldedChar] :
     txUnicodeCaseFoldCharToMono(TxUnicodeIgnCaseMonoLinear,
                                 TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                 TxUnicodeIgnCaseMonoBinary,
                                 ARRAY_LEN(TxUnicodeIgnCaseMonoBinary),
                                 uniDest->foldedChar));
          goto multiUtf8;
#endif /* !DO_TXCFF_ISO88591 */
        case TXCFF_CASEMODE_UNICODEMULTI:
        default:
#  ifdef DO_LEAVE_TXUNICHARS
          /* 1-to-N mapping, so we must check `foldCharBuf' justification
           * and expand accordingly:
           */
          if (foldCharsStart == foldCharsBuf)     /* left-justified */
            {
              uniDest = foldCharsBufEnd;
              for (uniSrc = foldCharsEnd-1; uniSrc >= foldCharsStart; uniSrc--)
                {
#    ifdef DO_TXCFF_ISO88591
                  uni = &TxUnicodeIgnCaseMultiIso88591[uniSrc->foldedChar];
#      if TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX < 0xff
                  error linear range assumed to cover all of ISO-8859-1;
#      endif
                  uniDest -= uni->numFoldedChars;
                  for (i = 0; i < uni->numFoldedChars; i++)
                    {
                      uniDest[i].orgSrcChar = uniSrc->orgSrcChar;
                      uniDest[i].foldedChar = uni->foldedChars[i];
                      uniDest[i].flags = uniSrc->flags;
                    }
#    else /* !DO_TXCFF_ISO88591 */
                  /* Optimization: use array to avoid function call: */
                  if (uniSrc->foldedChar <=
                      TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX)
                    {
                      uni = &TxUnicodeIgnCaseMultiLinear[uniSrc->foldedChar];
                      uniSrc2 = uni->foldedChars;
                      i = uni->numFoldedChars;
                    }
                  else
                    {
                      uniSrc2 = tmpChars;
                      i = txUnicodeCaseFoldCharToMulti(tmpChars,
                                   TxUnicodeIgnCaseMultiLinear,
                                   TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                   TxUnicodeIgnCaseMultiBinary,
                                   ARRAY_LEN(TxUnicodeIgnCaseMultiBinary),
                                   uniSrc->foldedChar);
                    }
                  uniDest -= i;
                  for (l = 0; l < i; l++, uniSrc2++)
                    {
                      uniDest[l].orgSrcChar = uniSrc->orgSrcChar;
                      uniDest[l].foldedChar = *uniSrc2;
                      uniDest[l].flags = uniSrc->flags;
                    }
#    endif /* !DO_TXCFF_ISO88591 */
                  SANITY_ASSERT(uniDest >= uniSrc && uniDest >= foldCharsBuf);
                }
              /* `foldCharsBuf' is now right-justified */
              foldCharsStart = uniDest;
              foldCharsEnd = foldCharsBufEnd;
            }
          else                                    /* right-justified */
            {
              uniDest = foldCharsBuf;
              for (uniSrc = foldCharsStart; uniSrc < foldCharsEnd; uniSrc++)
                {
#    ifdef DO_TXCFF_ISO88591
                  uniSrc2 = TxUnicodeIgnCaseMultiIso88591[uniSrc->foldedChar].foldedChars;
#      if TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX < 0xff
                  error linear range assumed to cover all of ISO-8859-1;
#      endif
                  COPY_TO_UNIFOLD(uniDest, , uniSrc2, uniSrc);
#    else /* !DO_TXCFF_ISO88591 */
                  /* Optimization: use array to avoid function call: */
                  if (uniSrc->foldedChar <=
                      TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX)
                    {
                      uniSrc2 = TxUnicodeIgnCaseMultiLinear[uniSrc->foldedChar].foldedChars;
                      COPY_TO_UNIFOLD(uniDest, , uniSrc2, uniSrc);
                    }
                  else
                    {
                      te = tmpChars + txUnicodeCaseFoldCharToMulti(tmpChars,
                                   TxUnicodeIgnCaseMultiLinear,
                                   TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                   TxUnicodeIgnCaseMultiBinary,
                                   ARRAY_LEN(TxUnicodeIgnCaseMultiBinary),
                                   uniSrc->foldedChar);
                      for (uniSrc2 = tmpChars; uniSrc2 <te; uniSrc2++,uniDest++)
                        {
                          uniDest->orgSrcChar = uniSrc->orgSrcChar;
                          uniDest->foldedChar = *uniSrc2;
                          uniDest->flags = uniSrc->flags;
                        }
                    }
#    endif /* !DO_TXCFF_ISO88591 */
                  SANITY_ASSERT(uniDest <= uniSrc + 1 &&
                                uniDest <= foldCharsBufEnd);
                }
              /* `foldCharsBuf' is now left-justified */
              foldCharsStart = foldCharsBuf;
              foldCharsEnd = uniDest;
            }
          /* `foldCharsBuf' is now left- OR right-justified */
#  else /* !DO_LEAVE_TXUNICHARS */
          /* 1-to-N mapping, but we encode direct to `dest',
           * so `foldCharsBuf' justification does not matter:
           */
          for (uniSrc = foldCharsStart; uniSrc < foldCharsEnd; uniSrc++)
            {
#  ifdef DO_TXCFF_ISO88591
              uniSrc2 =
                TxUnicodeIgnCaseMultiIso88591[uniSrc->foldedChar].foldedChars;
#    if TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX < 0xff
              error linear range assumed to cover all of ISO-8859-1;
#    endif
              COPY_TO_BYTE(dest, if (dest>=destEnd) goto destShort, uniSrc2);
#  else /* !DO_TXCFF_ISO88591 */
              /* Optimization: use array to avoid function call: */
              if (uniSrc->foldedChar <=
                  TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX)
                {
                  uni = &TxUnicodeIgnCaseMultiLinear[uniSrc->foldedChar];
                  for (i = 0; i < uni->numFoldedChars; i++)
                    {
                      TX_UNICODE_ENCODE_UTF8_CHAR(dest, destEnd,
                                    uni->foldedChars[i], goto destShort);
                    }
                }
              else
                {
                  te = tmpChars +
                    txUnicodeCaseFoldCharToMulti(tmpChars,
                                   TxUnicodeIgnCaseMultiLinear,
                                   TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                   TxUnicodeIgnCaseMultiBinary,
                                   ARRAY_LEN(TxUnicodeIgnCaseMultiBinary),
                                   uniSrc->foldedChar);
                  for (uniSrc2 = tmpChars; uniSrc2 < te; uniSrc2++)
                    {
                      TX_UNICODE_ENCODE_UTF8_CHAR(dest, destEnd, *uniSrc2,
                                                  goto destShort);
                    }
                }
#  endif /* !DO_TXCFF_ISO88591 */
            }
#endif /* !DO_LEAVE_TXUNICHARS */
          break;
        }
      break;
    case TXCFF_CASESTYLE_LOWER: /* - - - - - - - - - - - - - - - - - - - - */
    doLower:
      switch (mode)
        {
        case TXCFF_CASEMODE_CTYPE:
        lowerCtype:
#ifdef DO_TXCFF_ISO88591
          for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
            {
              SANITY_ASSERT((unsigned)uniDest->foldedChar <= (unsigned)0xff);
#  ifdef DO_LEAVE_TXUNICHARS
              uniDest->foldedChar = tolower(uniDest->foldedChar);
#  else /* !DO_LEAVE_TXUNICHARS */
              if (dest >= destEnd) goto destShort;
              *(byte *)(dest++) = tolower(uniDest->foldedChar);
#  endif /* !DO_LEAVE_TXUNICHARS */
            }
          break;
#else /* !DO_TXCFF_ISO88591 */
          /* 1-to-1 mapping; `foldCharsBuf' justification irrelevant: */
          for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
            /* tolower() is undefined beyond unsigned char; limit it: */
            if ((unsigned)uniDest->foldedChar <= 0xff)
              uniDest->foldedChar = tolower((unsigned)uniDest->foldedChar);
          goto multiUtf8;
#endif /* !DO_TXCFF_ISO88591 */
        case TXCFF_CASEMODE_UNICODEMONO:
#ifdef DO_TXCFF_ISO88591
          for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
            {
              SANITY_ASSERT((unsigned)uniDest->foldedChar <= (unsigned)0xff);
#  ifdef DO_LEAVE_TXUNICHARS
              uniDest->foldedChar =
                TxUnicodeLowerCaseMonoIso88591[uniDest->foldedChar];
#  else /* !DO_LEAVE_TXUNICHARS */
              if (dest >= destEnd) goto destShort;
              *(byte *)(dest++) =
                TxUnicodeLowerCaseMonoIso88591[uniDest->foldedChar];
#  endif /* !DO_LEAVE_TXUNICHARS */
            }
          break;
#else /* !DO_TXCFF_ISO88591 */
          for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
            /* Optimization: use array to avoid function call: */
            uniDest->foldedChar = (uniDest->foldedChar <=
                          TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX ?
                          TxUnicodeLowerCaseMonoLinear[uniDest->foldedChar] :
     txUnicodeCaseFoldCharToMono(TxUnicodeLowerCaseMonoLinear,
                                 TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                 TxUnicodeLowerCaseMonoBinary,
                                 ARRAY_LEN(TxUnicodeLowerCaseMonoBinary),
                                 uniDest->foldedChar));
          goto multiUtf8;
#endif /* !DO_TXCFF_ISO88591 */
        case TXCFF_CASEMODE_UNICODEMULTI:
        default:
#  ifdef DO_LEAVE_TXUNICHARS
          /* 1-to-N mapping, so we must check `foldCharBuf' justification
           * and expand accordingly:
           */
          if (foldCharsStart == foldCharsBuf)     /* left-justified */
            {
              uniDest = foldCharsBufEnd;
              for (uniSrc = foldCharsEnd-1; uniSrc >= foldCharsStart; uniSrc--)
                {
#    ifdef DO_TXCFF_ISO88591
                  uni = &TxUnicodeLowerCaseMultiIso88591[uniSrc->foldedChar];
#      if TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX < 0xff
                  error linear range assumed to cover all of ISO-8859-1;
#      endif
                  uniDest -= uni->numFoldedChars;
                  for (i = 0; i < uni->numFoldedChars; i++)
                    {
                      uniDest[i].orgSrcChar = uniSrc->orgSrcChar;
                      uniDest[i].foldedChar = uni->foldedChars[i];
                      uniDest[i].flags = uniSrc->flags;
                    }
#    else /* !DO_TXCFF_ISO88591 */
                  /* Optimization: use array to avoid function call: */
                  if (uniSrc->foldedChar <=
                      TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX)
                    {
                      uni= &TxUnicodeLowerCaseMultiLinear[uniSrc->foldedChar];
                      uniSrc2 = uni->foldedChars;
                      i = uni->numFoldedChars;
                    }
                  else
                    {
                      uniSrc2 = tmpChars;
                      i = txUnicodeCaseFoldCharToMulti(tmpChars,
                                   TxUnicodeLowerCaseMultiLinear,
                                   TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                   TxUnicodeLowerCaseMultiBinary,
                                   ARRAY_LEN(TxUnicodeLowerCaseMultiBinary),
                                   uniSrc->foldedChar);
                    }
                  uniDest -= i;
                  for (l = 0; l < i; l++, uniSrc2++)
                    {
                      uniDest[l].orgSrcChar = uniSrc->orgSrcChar;
                      uniDest[l].foldedChar = *uniSrc2;
                      uniDest[l].flags = uniSrc->flags;
                    }
#    endif /* !DO_TXCFF_ISO88591 */
                  SANITY_ASSERT(uniDest >= uniSrc && uniDest >= foldCharsBuf);
                }
              /* `foldCharsBuf' is now right-justified */
              foldCharsStart = uniDest;
              foldCharsEnd = foldCharsBufEnd;
            }
          else                                    /* right-justified */
            {
              uniDest = foldCharsBuf;
              for (uniSrc = foldCharsStart; uniSrc < foldCharsEnd; uniSrc++)
                {
#    ifdef DO_TXCFF_ISO88591
                  uniSrc2 = TxUnicodeLowerCaseMultiIso88591[uniSrc->foldedChar].foldedChars;
#      if TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX < 0xff
                  error linear range assumed to cover all of ISO-8859-1;
#      endif
                  COPY_TO_UNIFOLD(uniDest, , uniSrc2, uniSrc);
#    else /* !DO_TXCFF_ISO88591 */
                  /* Optimization: use array to avoid function call: */
                  if (uniSrc->foldedChar <=
                      TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX)
                    {
                      uniSrc2 = TxUnicodeLowerCaseMultiLinear[uniSrc->foldedChar].foldedChars;
                      COPY_TO_UNIFOLD(uniDest, , uniSrc2, uniSrc);
                    }
                  else
                    {
                      te = tmpChars + txUnicodeCaseFoldCharToMulti(tmpChars,
                                   TxUnicodeLowerCaseMultiLinear,
                                   TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                   TxUnicodeLowerCaseMultiBinary,
                                   ARRAY_LEN(TxUnicodeLowerCaseMultiBinary),
                                   uniSrc->foldedChar);
                      for (uniSrc2 = tmpChars; uniSrc2 <te; uniSrc2++,uniDest++)
                        {
                          uniDest->orgSrcChar = uniSrc->orgSrcChar;
                          uniDest->foldedChar = *uniSrc2;
                          uniDest->flags = uniSrc->flags;
                        }
                    }
#    endif /* !DO_TXCFF_ISO88591 */
                  SANITY_ASSERT(uniDest <= uniSrc + 1 &&
                                uniDest <= foldCharsBufEnd);
                }
              /* `foldCharsBuf' is now left-justified */
              foldCharsStart = foldCharsBuf;
              foldCharsEnd = uniDest;
            }
          /* `foldCharsBuf' is now left- OR right-justified */
#  else /* !DO_LEAVE_TXUNICHARS */
          /* 1-to-N mapping, but we encode direct to `dest',
           * so `foldCharsBuf' justification does not matter:
           */
          for (uniSrc = foldCharsStart; uniSrc < foldCharsEnd; uniSrc++)
            {
#  ifdef DO_TXCFF_ISO88591
              uniSrc2 =
              TxUnicodeLowerCaseMultiIso88591[uniSrc->foldedChar].foldedChars;
#    if TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX < 0xff
              error linear range assumed to cover all of ISO-8859-1;
#    endif
              COPY_TO_BYTE(dest, if (dest>=destEnd) goto destShort, uniSrc2);
#  else /* !DO_TXCFF_ISO88591 */
              /* Optimization: use array to avoid function call: */
              if (uniSrc->foldedChar <=
                  TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX)
                {
                  uni = &TxUnicodeLowerCaseMultiLinear[uniSrc->foldedChar];
                  for (i = 0; i < uni->numFoldedChars; i++)
                    {
                      TX_UNICODE_ENCODE_UTF8_CHAR(dest, destEnd,
                                    uni->foldedChars[i], goto destShort);
                    }
                }
              else
                {
                  te = tmpChars +
                    txUnicodeCaseFoldCharToMulti(tmpChars,
                                   TxUnicodeLowerCaseMultiLinear,
                                   TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                   TxUnicodeLowerCaseMultiBinary,
                                   ARRAY_LEN(TxUnicodeLowerCaseMultiBinary),
                                   uniSrc->foldedChar);
                  for (uniSrc2 = tmpChars; uniSrc2 < te; uniSrc2++)
                    {
                      TX_UNICODE_ENCODE_UTF8_CHAR(dest, destEnd, *uniSrc2,
                                                  goto destShort);
                    }
                }
#  endif /* !DO_TXCFF_ISO88591 */
            }
#endif /* !DO_LEAVE_TXUNICHARS */
          break;
        }
      break;
    case TXCFF_CASESTYLE_TITLE: /* - - - - - - - - - - - - - - - - - - - - */
      /* Titlecase is context-dependent; wtf use UAX #29 word-separation
       * algorithm.  For now, use initcap() algorithm:
       *   prevCharIsWhiteSpace = 1
       *   for each character C do
       *     C = (prevCharIsWhiteSpace ? totitle(C) : tolower(C))
       *     prevCharIsWhiteSpace = isspace(C)
       *   done
       */
      i = prevCharIsWhiteSpace;                 /* save it for a moment */
      prevCharIsWhiteSpace = TX_UNICODE_ISSPACE(srcCh);
      if (!i) goto doLower;
      switch (mode)
        {
        case TXCFF_CASEMODE_CTYPE:
          /* titlecase same as uppercase for monobyte ctype: */
          goto upperCtype;
        case TXCFF_CASEMODE_UNICODEMONO:
#ifdef DO_TXCFF_ISO88591
          for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
            {
              SANITY_ASSERT((unsigned)uniDest->foldedChar <= (unsigned)0xff);
#  ifdef DO_LEAVE_TXUNICHARS
              uniDest->foldedChar =
                TxUnicodeTitleCaseMonoIso88591[uniDest->foldedChar];
#  else /* !DO_LEAVE_TXUNICHARS */
              if (dest >= destEnd) goto destShort;
              *(byte *)(dest++) =
                TxUnicodeTitleCaseMonoIso88591[uniDest->foldedChar];
#  endif /* !DO_LEAVE_TXUNICHARS */
            }
          break;
#else /* !DO_TXCFF_ISO88591 */
          for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
            /* Optimization: use array to avoid function call: */
            uniDest->foldedChar = (uniDest->foldedChar <=
                          TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX ?
                          TxUnicodeTitleCaseMonoLinear[uniDest->foldedChar] :
     txUnicodeCaseFoldCharToMono(TxUnicodeTitleCaseMonoLinear,
                                 TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                 TxUnicodeTitleCaseMonoBinary,
                                 ARRAY_LEN(TxUnicodeTitleCaseMonoBinary),
                                 uniDest->foldedChar));
          goto multiUtf8;
#endif /* !DO_TXCFF_ISO88591 */
        case TXCFF_CASEMODE_UNICODEMULTI:
        default:
#  ifdef DO_LEAVE_TXUNICHARS
          /* 1-to-N mapping, so we must check `foldCharBuf' justification
           * and expand accordingly:
           */
          if (foldCharsStart == foldCharsBuf)     /* left-justified */
            {
              uniDest = foldCharsBufEnd;
              for (uniSrc = foldCharsEnd-1; uniSrc >= foldCharsStart; uniSrc--)
                {
#    ifdef DO_TXCFF_ISO88591
                  uni = &TxUnicodeTitleCaseMultiIso88591[uniSrc->foldedChar];
#      if TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX < 0xff
                  error linear range assumed to cover all of ISO-8859-1;
#      endif
                  uniDest -= uni->numFoldedChars;
                  for (i = 0; i < uni->numFoldedChars; i++)
                    {
                      uniDest[i].orgSrcChar = uniSrc->orgSrcChar;
                      uniDest[i].foldedChar = uni->foldedChars[i];
                      uniDest[i].flags = uniSrc->flags;
                    }
#    else /* !DO_TXCFF_ISO88591 */
                  /* Optimization: use array to avoid function call: */
                  if (uniSrc->foldedChar <=
                      TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX)
                    {
                      uni= &TxUnicodeTitleCaseMultiLinear[uniSrc->foldedChar];
                      uniSrc2 = uni->foldedChars;
                      i = uni->numFoldedChars;
                    }
                  else
                    {
                      uniSrc2 = tmpChars;
                      i = txUnicodeCaseFoldCharToMulti(tmpChars,
                                   TxUnicodeTitleCaseMultiLinear,
                                   TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                   TxUnicodeTitleCaseMultiBinary,
                                   ARRAY_LEN(TxUnicodeTitleCaseMultiBinary),
                                   uniSrc->foldedChar);
                    }
                  uniDest -= i;
                  for (l = 0; l < i; l++, uniSrc2++)
                    {
                      uniDest[l].orgSrcChar = uniSrc->orgSrcChar;
                      uniDest[l].foldedChar = *uniSrc2;
                      uniDest[l].flags = uniSrc->flags;
                    }
#    endif /* !DO_TXCFF_ISO88591 */
                  SANITY_ASSERT(uniDest >= uniSrc && uniDest >= foldCharsBuf);
                }
              /* `foldCharsBuf' is now right-justified */
              foldCharsStart = uniDest;
              foldCharsEnd = foldCharsBufEnd;
            }
          else                                    /* right-justified */
            {
              uniDest = foldCharsBuf;
              for (uniSrc = foldCharsStart; uniSrc < foldCharsEnd; uniSrc++)
                {
#    ifdef DO_TXCFF_ISO88591
                  uniSrc2 = TxUnicodeTitleCaseMultiIso88591[uniSrc->foldedChar].foldedChars;
#      if TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX < 0xff
                  error linear range assumed to cover all of ISO-8859-1;
#      endif
                  COPY_TO_UNIFOLD(uniDest, , uniSrc2, uniSrc);
#    else /* !DO_TXCFF_ISO88591 */
                  /* Optimization: use array to avoid function call: */
                  if (uniSrc->foldedChar <=
                      TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX)
                    {
                      uniSrc2 = TxUnicodeTitleCaseMultiLinear[uniSrc->foldedChar].foldedChars;
                      COPY_TO_UNIFOLD(uniDest, , uniSrc2, uniSrc);
                    }
                  else
                    {
                      te = tmpChars + txUnicodeCaseFoldCharToMulti(tmpChars,
                                   TxUnicodeTitleCaseMultiLinear,
                                   TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                   TxUnicodeTitleCaseMultiBinary,
                                   ARRAY_LEN(TxUnicodeTitleCaseMultiBinary),
                                   uniSrc->foldedChar);
                      for (uniSrc2 = tmpChars; uniSrc2 <te; uniSrc2++,uniDest++)
                        {
                          uniDest->orgSrcChar = uniSrc->orgSrcChar;
                          uniDest->foldedChar = *uniSrc2;
                          uniDest->flags = uniSrc->flags;
                        }
                    }
#    endif /* !DO_TXCFF_ISO88591 */
                  SANITY_ASSERT(uniDest <= uniSrc + 1 &&
                                uniDest <= foldCharsBufEnd);
                }
              /* `foldCharsBuf' is now left-justified */
              foldCharsStart = foldCharsBuf;
              foldCharsEnd = uniDest;
            }
          /* `foldCharsBuf' is now left- OR right-justified */
#  else /* !DO_LEAVE_TXUNICHARS */
          /* 1-to-N mapping, but we encode direct to `dest',
           * so `foldCharsBuf' justification does not matter:
           */
          for (uniSrc = foldCharsStart; uniSrc < foldCharsEnd; uniSrc++)
            {
#  ifdef DO_TXCFF_ISO88591
              uniSrc2 =
              TxUnicodeTitleCaseMultiIso88591[uniSrc->foldedChar].foldedChars;
#    if TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX < 0xff
              error linear range assumed to cover all of ISO-8859-1;
#    endif
              COPY_TO_BYTE(dest, if (dest>=destEnd) goto destShort, uniSrc2);
#  else /* !DO_TXCFF_ISO88591 */
              /* Optimization: use array to avoid function call: */
              if (uniSrc->foldedChar <=
                  TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX)
                {
                  uni = &TxUnicodeTitleCaseMultiLinear[uniSrc->foldedChar];
                  for (i = 0; i < uni->numFoldedChars; i++)
                    {
                      TX_UNICODE_ENCODE_UTF8_CHAR(dest, destEnd,
                                    uni->foldedChars[i], goto destShort);
                    }
                }
              else
                {
                  te = tmpChars +
                    txUnicodeCaseFoldCharToMulti(tmpChars,
                                   TxUnicodeTitleCaseMultiLinear,
                                   TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                   TxUnicodeTitleCaseMultiBinary,
                                   ARRAY_LEN(TxUnicodeTitleCaseMultiBinary),
                                   uniSrc->foldedChar);
                  for (uniSrc2 = tmpChars; uniSrc2 < te; uniSrc2++)
                    {
                      TX_UNICODE_ENCODE_UTF8_CHAR(dest, destEnd, *uniSrc2,
                                                  goto destShort);
                    }
                }
#  endif /* !DO_TXCFF_ISO88591 */
            }
#endif /* !DO_LEAVE_TXUNICHARS */
          break;
        }
      break;
    case TXCFF_CASESTYLE_UPPER: /* - - - - - - - - - - - - - - - - - - - - */
      switch (mode)
        {
        case TXCFF_CASEMODE_CTYPE:
        upperCtype:
#ifdef DO_TXCFF_ISO88591
          for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
            {
              SANITY_ASSERT((unsigned)uniDest->foldedChar <= (unsigned)0xff);
#  ifdef DO_LEAVE_TXUNICHARS
              uniDest->foldedChar = toupper(uniDest->foldedChar);
#  else /* !DO_LEAVE_TXUNICHARS */
              if (dest >= destEnd) goto destShort;
              *(byte *)(dest++) = toupper(uniDest->foldedChar);
#  endif /* !DO_LEAVE_TXUNICHARS */
            }
          break;
#else /* !DO_TXCFF_ISO88591 */
          /* 1-to-1 mapping; `foldCharsBuf' justification irrelevant: */
          for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
            /* toupper() is undefined beyond unsigned char; limit it: */
            if ((unsigned)uniDest->foldedChar <= 0xff)
              uniDest->foldedChar = toupper((unsigned)uniDest->foldedChar);
          goto multiUtf8;
#endif /* !DO_TXCFF_ISO88591 */
        case TXCFF_CASEMODE_UNICODEMONO:
#ifdef DO_TXCFF_ISO88591
          for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
            {
              SANITY_ASSERT((unsigned)uniDest->foldedChar <= (unsigned)0xff);
#  ifdef DO_LEAVE_TXUNICHARS
              uniDest->foldedChar =
                TxUnicodeUpperCaseMonoIso88591[uniDest->foldedChar];
#  else /* !DO_LEAVE_TXUNICHARS */
              if (dest >= destEnd) goto destShort;
              *(byte *)(dest++) =
                TxUnicodeUpperCaseMonoIso88591[uniDest->foldedChar];
#  endif /* !DO_LEAVE_TXUNICHARS */
            }
          break;
#else /* !DO_TXCFF_ISO88591 */
          for (uniDest = foldCharsStart; uniDest < foldCharsEnd; uniDest++)
            /* Optimization: use array to avoid function call: */
            uniDest->foldedChar = (uniDest->foldedChar <=
                          TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX ?
                          TxUnicodeUpperCaseMonoLinear[uniDest->foldedChar] :
     txUnicodeCaseFoldCharToMono(TxUnicodeUpperCaseMonoLinear,
                                 TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                 TxUnicodeUpperCaseMonoBinary,
                                 ARRAY_LEN(TxUnicodeUpperCaseMonoBinary),
                                 uniDest->foldedChar));
          goto multiUtf8;
#endif /* !DO_TXCFF_ISO88591 */
        case TXCFF_CASEMODE_UNICODEMULTI:
        default:
#  ifdef DO_LEAVE_TXUNICHARS
          /* 1-to-N mapping, so we must check `foldCharBuf' justification
           * and expand accordingly:
           */
          if (foldCharsStart == foldCharsBuf)     /* left-justified */
            {
              uniDest = foldCharsBufEnd;
              for (uniSrc = foldCharsEnd-1; uniSrc >= foldCharsStart; uniSrc--)
                {
#    ifdef DO_TXCFF_ISO88591
                  uni = &TxUnicodeUpperCaseMultiIso88591[uniSrc->foldedChar];
#      if TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX < 0xff
                  error linear range assumed to cover all of ISO-8859-1;
#      endif
                  uniDest -= uni->numFoldedChars;
                  for (i = 0; i < uni->numFoldedChars; i++)
                    {
                      uniDest[i].orgSrcChar = uniSrc->orgSrcChar;
                      uniDest[i].foldedChar = uni->foldedChars[i];
                      uniDest[i].flags = uniSrc->flags;
                    }
#    else /* !DO_TXCFF_ISO88591 */
                  /* Optimization: use array to avoid function call: */
                  if (uniSrc->foldedChar <=
                      TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX)
                    {
                      uni= &TxUnicodeUpperCaseMultiLinear[uniSrc->foldedChar];
                      uniSrc2 = uni->foldedChars;
                      i = uni->numFoldedChars;
                    }
                  else
                    {
                      uniSrc2 = tmpChars;
                      i = txUnicodeCaseFoldCharToMulti(tmpChars,
                                   TxUnicodeUpperCaseMultiLinear,
                                   TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                   TxUnicodeUpperCaseMultiBinary,
                                   ARRAY_LEN(TxUnicodeUpperCaseMultiBinary),
                                   uniSrc->foldedChar);
                    }
                  uniDest -= i;
                  for (l = 0; l < i; l++, uniSrc2++)
                    {
                      uniDest[l].orgSrcChar = uniSrc->orgSrcChar;
                      uniDest[l].foldedChar = *uniSrc2;
                      uniDest[l].flags = uniSrc->flags;
                    }
#    endif /* !DO_TXCFF_ISO88591 */
                  SANITY_ASSERT(uniDest >= uniSrc && uniDest >= foldCharsBuf);
                }
              /* `foldCharsBuf' is now right-justified */
              foldCharsStart = uniDest;
              foldCharsEnd = foldCharsBufEnd;
            }
          else                                    /* right-justified */
            {
              uniDest = foldCharsBuf;
              for (uniSrc = foldCharsStart; uniSrc < foldCharsEnd; uniSrc++)
                {
#    ifdef DO_TXCFF_ISO88591
                  uniSrc2 = TxUnicodeUpperCaseMultiIso88591[uniSrc->foldedChar].foldedChars;
#      if TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX < 0xff
                  error linear range assumed to cover all of ISO-8859-1;
#      endif
                  COPY_TO_UNIFOLD(uniDest, , uniSrc2, uniSrc);
#    else /* !DO_TXCFF_ISO88591 */
                  /* Optimization: use array to avoid function call: */
                  if (uniSrc->foldedChar <=
                      TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX)
                    {
                      uniSrc2 = TxUnicodeUpperCaseMultiLinear[uniSrc->foldedChar].foldedChars;
                      COPY_TO_UNIFOLD(uniDest, , uniSrc2, uniSrc);
                    }
                  else
                    {
                      te = tmpChars + txUnicodeCaseFoldCharToMulti(tmpChars,
                                   TxUnicodeUpperCaseMultiLinear,
                                   TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                   TxUnicodeUpperCaseMultiBinary,
                                   ARRAY_LEN(TxUnicodeUpperCaseMultiBinary),
                                   uniSrc->foldedChar);
                      for (uniSrc2 = tmpChars; uniSrc2 <te; uniSrc2++,uniDest++)
                        {
                          uniDest->orgSrcChar = uniSrc->orgSrcChar;
                          uniDest->foldedChar = *uniSrc2;
                          uniDest->flags = uniSrc->flags;
                        }
                    }
#    endif /* !DO_TXCFF_ISO88591 */
                  SANITY_ASSERT(uniDest <= uniSrc + 1 &&
                                uniDest <= foldCharsBufEnd);
                }
              /* `foldCharsBuf' is now left-justified */
              foldCharsStart = foldCharsBuf;
              foldCharsEnd = uniDest;
            }
          /* `foldCharsBuf' is now left- OR right-justified */
#  else /* !DO_LEAVE_TXUNICHARS */
          /* 1-to-N mapping, but we encode direct to `dest',
           * so `foldCharsBuf' justification does not matter:
           */
          for (uniSrc = foldCharsStart; uniSrc < foldCharsEnd; uniSrc++)
            {
#  ifdef DO_TXCFF_ISO88591
              uniSrc2 =
              TxUnicodeUpperCaseMultiIso88591[uniSrc->foldedChar].foldedChars;
#    if TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX < 0xff
              error linear range assumed to cover all of ISO-8859-1;
#    endif
              COPY_TO_BYTE(dest, if (dest>=destEnd) goto destShort, uniSrc2);
#  else /* !DO_TXCFF_ISO88591 */
              /* Optimization: use array to avoid function call: */
              if (uniSrc->foldedChar <=
                  TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX)
                {
                  uni = &TxUnicodeUpperCaseMultiLinear[uniSrc->foldedChar];
                  for (i = 0; i < uni->numFoldedChars; i++)
                    {
                      TX_UNICODE_ENCODE_UTF8_CHAR(dest, destEnd,
                                    uni->foldedChars[i], goto destShort);
                    }
                }
              else
                {
                  te = tmpChars +
                    txUnicodeCaseFoldCharToMulti(tmpChars,
                                   TxUnicodeUpperCaseMultiLinear,
                                   TX_UNICODE_CASE_FOLD_LINEAR_CODEPOINT_MAX,
                                   TxUnicodeUpperCaseMultiBinary,
                                   ARRAY_LEN(TxUnicodeUpperCaseMultiBinary),
                                   uniSrc->foldedChar);
                  for (uniSrc2 = tmpChars; uniSrc2 < te; uniSrc2++)
                    {
                      TX_UNICODE_ENCODE_UTF8_CHAR(dest, destEnd, *uniSrc2,
                                                  goto destShort);
                    }
                }
#  endif /* !DO_TXCFF_ISO88591 */
            }
#endif /* !DO_LEAVE_TXUNICHARS */
          break;
        }
      break;
    case TXCFF_CASESTYLE_RESPECT: /* - - - - - - - - - - - - - - - - - - - */
    default:
#ifndef DO_TXCFF_ISO88591
    multiUtf8:
#endif /* !DO_TXCFF_ISO88591 */
#ifdef DO_LEAVE_TXUNICHARS
      ;                                         /* no-op: leave in place */
#else /* !DO_LEAVE_TXUNICHARS */
      for (uniSrc = foldCharsStart; uniSrc < foldCharsEnd; uniSrc++)
        {
#  ifdef DO_TXCFF_ISO88591
          if (dest >= destEnd) goto destShort;
          *(byte *)(dest++) = uniSrc->foldedChar;
#  else /* !DO_TXCFF_ISO88591 */
          TX_UNICODE_ENCODE_UTF8_CHAR(dest, destEnd, uniSrc->foldedChar,
                                      goto destShort);
#  endif /* !DO_TXCFF_ISO88591 */
        }
#endif /* !DO_LEAVE_TXUNICHARS */
      break;
    }

#undef GET_BYTE
#undef COPY_TO_UNIFOLD
#undef COPY_TO_BYTE
