/* Helper code #include'd into unicode.c, for comparing folded chars
 * in TXunicodeStrFoldCmp() and TXunicodeStrFoldIsEqualBackwards().
 * Included just after char(s) folded.
 * #include instead of macro for easier line-by-line debugging.
 * NOTE: See also unicodeCompareFoldPost.c
 */

    if (aFoldStart->foldedChar == bFoldStart->foldedChar)
      {
        /* Do not let any part of a TXCFF_EXPDIACRITICS char expansion
         * match a TXCFF_IGNDIACRITICS-stripped char;
         * this prevents `f u-umlaut r' from matching `f u e-acute r':
         */
        if (((aFoldStart->flags | bFoldStart->flags) &
             (TUFF_FROMEXPDIACRITICS | TUFF_FROMIGNDIACRITICS)) ==
            (TUFF_FROMEXPDIACRITICS | TUFF_FROMIGNDIACRITICS))
          {
            /* Folded chars compare equal, but we still need a nonzero cmp: */
            cmp = (aFoldStart->orgSrcChar < bFoldStart->orgSrcChar ? -1 : 1);
            goto checkOpt;
          }
        cmp = 0;
        /* Note last unskipped optional chars for possible fallback.
         * wtf might have to fall back multiple times and permute
         * all optional chars, not just most-recent; but those cases
         * should be rare/non-existent in practice, so just do one:
         */
        if (aFoldStart->flags & TUFF_ISOPTIONAL)
          {
            /* Optional char that matches an optional char: neither
             * should be skipped later (during fallback), so that
             * `f u-umlaut r' does not match `f u-umlaut e r'; do not
             * save as potential later fallback skip if both optional:
             */
            if (!(bFoldStart->flags & TUFF_ISOPTIONAL))
              {
                lastASrcOptUnSkippedEnd = a;
                lastASrcOptUnSkippedBSrcStart = *bp;
              }
          }
        if (bFoldStart->flags & TUFF_ISOPTIONAL)
          {
            if (!(aFoldStart->flags & TUFF_ISOPTIONAL))
              {
                lastBSrcOptUnSkippedEnd = b;
                lastBSrcOptUnSkippedASrcStart = *ap;
              }
          }
      }
    else                                        /* chars differ */
      {
        /* Do not just use `cmp = a - b': need to reserve value 1000
         * for TXCFF_PREFIX, and some Unicode chars could be 1000
         * codepoints apart:
         */
        cmp = (aFoldStart->foldedChar < bFoldStart->foldedChar ? -1 : 1);
      checkOpt:
        /* Mismatch; if optional character(s), skip and try again: */
        if (aFoldStart->flags & TUFF_ISOPTIONAL)                        
          {
            /* Do not skip if `b' char is optional too;
             * otherwise the optional parts of `o-umlaut' and
             * `o-circumflex' would match:
             */
            if (bFoldStart->flags & TUFF_ISOPTIONAL)
              {
                aFoldStart++;
                bFoldStart++;
                break;
              }
            /* this optional char was skipped; cannot fall back (?): */
            lastASrcOptUnSkippedEnd = CHARPN;
            lastASrcOptUnSkippedBSrcStart = CHARPN;
            aFoldStart++;
            continue;                           /* same `bFoldStart' */
          }
        else if (bFoldStart->flags & TUFF_ISOPTIONAL)
          {
            if (aFoldStart->flags & TUFF_ISOPTIONAL)
              {
                aFoldStart++;
                bFoldStart++;
                break;
              }
            /* this optional char was skipped; cannot fall back (?): */
            lastBSrcOptUnSkippedEnd = CHARPN;
            lastBSrcOptUnSkippedASrcStart = CHARPN;
            bFoldStart++;
            continue;                           /* same `aFoldStart' */
          }
        /* Mismatch and neither current char is optional.  Fall back
         * to previous optional char, and skip it; see if that works:
         */
        else if (lastASrcOptUnSkippedEnd != CHARPN)
          {
            a = *ap = lastASrcOptUnSkippedEnd;
            b = *bp = lastASrcOptUnSkippedBSrcStart;
            aFoldStart = aFoldEnd;              /* force queue re-start */
            bFoldStart = bFoldEnd;
            lastASrcOptUnSkippedEnd = CHARPN;   /* now skipped */
            lastASrcOptUnSkippedBSrcStart = CHARPN;  /* "" */
            continue;
          }
        else if (lastBSrcOptUnSkippedEnd != CHARPN)
          {
            b = *bp = lastBSrcOptUnSkippedEnd;
            a = *ap = lastBSrcOptUnSkippedASrcStart;
            aFoldStart = aFoldEnd;              /* force queue re-start */
            bFoldStart = bFoldEnd;
            lastBSrcOptUnSkippedEnd = CHARPN;   /* now skipped */
            lastBSrcOptUnSkippedASrcStart = CHARPN;  /* "" */
            continue;
          }
        else                                  /* chars differ, period */
          break;
      }
    aFoldStart++;
    bFoldStart++;
