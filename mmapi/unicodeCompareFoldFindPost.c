/* Helper code #include'd into unicode.c, for comparing folded chars
 * in TXtxupmFind().  Included just after search buf char(s) folded.
 * #include instead of macro for easier line-by-line debugging.
 * NOTE: See also COMPARE_FOLD() macro in unicode.c:
 */

          /* Next search char was just folded into `txtFoldBuf' here */

          /* Bookmark for later, to see if we used any of `txtFoldBuf': */
          txtFoldStartOrg = txtFoldStart;
        }
      /* Now compare current folded characters: */
      if (keyFoldStart->foldedChar == txtFoldStart->foldedChar &&
          /* Do not let any part of a TXCFF_EXPDIACRITICS char expansion
           * match a TXCFF_IGNDIACRITICS-stripped char;
           * this prevents `f u-umlaut r' from matching `f u e-acute r':
           */
          (((keyFoldStart->flags | txtFoldStart->flags) &
            (TUFF_FROMEXPDIACRITICS | TUFF_FROMIGNDIACRITICS)) !=
           (TUFF_FROMEXPDIACRITICS | TUFF_FROMIGNDIACRITICS)))
        {                                       /* chars match */
          /* Note last unskipped optional chars for possible fallback.
           * wtf might have to fall back multiple times and permute
           * all optional chars, not just most-recent; but those cases
           * should be rare/non-existent in practice, so just do one:
           */
          if (keyFoldStart->flags & TUFF_ISOPTIONAL)
            {
              /* Optional char that matches an optional char: neither
               * should be skipped later (during fallback), so that
               * `f u-umlaut r' does not match `f u-umlaut e r'; do not
               * save as potential later fallback skip if both optional:
               */
              if (!(txtFoldStart->flags & TUFF_ISOPTIONAL))
                {
                  lastKeyFoldOptUnSkippedEnd = keyFoldStart + 1;
                  lastKeyFoldOptUnSkippedTxtSrcStart = prevS;
                }
            }
          if (txtFoldStart->flags & TUFF_ISOPTIONAL)
            {
              if (!(keyFoldStart->flags & TUFF_ISOPTIONAL))
                {
                  lastTxtSrcOptUnSkippedEnd = s;
                  lastTxtSrcOptUnSkippedKeyFoldStart = keyFoldStart;
                }
            }
        }
      else                                      /* chars differ */
        {
          /* Mismatch; if optional character(s), skip and try again: */
          if (keyFoldStart->flags & TUFF_ISOPTIONAL)
            {
              /* Do not skip if search-buf char is optional too;
               * otherwise the optional parts of `o-umlaut' and
               * `o-circumflex' would match:
               */
              if (txtFoldStart->flags & TUFF_ISOPTIONAL) break;
              /* this optional char was skipped; cannot fall back (?): */
              lastKeyFoldOptUnSkippedEnd = TXUNIFOLDCHARPN;
              lastKeyFoldOptUnSkippedTxtSrcStart = CHARPN;
              keyFoldStart++;
              continue;                         /* same `txtFoldStart' */
            }
          else if (txtFoldStart->flags & TUFF_ISOPTIONAL)
            {
              if (keyFoldStart->flags & TUFF_ISOPTIONAL) break;
              /* this optional char was skipped; cannot fall back (?): */
              lastTxtSrcOptUnSkippedEnd = CHARPN;
              lastTxtSrcOptUnSkippedKeyFoldStart = TXUNIFOLDCHARPN;
              txtFoldStart++;
              continue;                         /* same `keyFoldStart' */
            }
          /* Mismatch and neither current char is optional.  Fall back
           * to previous optional char, and skip it; see if that works:
           */
          else if (lastKeyFoldOptUnSkippedEnd != TXUNIFOLDCHARPN)
            {
              keyFoldStart = lastKeyFoldOptUnSkippedEnd;
              s = lastKeyFoldOptUnSkippedTxtSrcStart;
              txtFoldStart = txtFoldEnd;        /* force queue re-start */
              lastKeyFoldOptUnSkippedEnd = TXUNIFOLDCHARPN; /* now skipped */
              lastKeyFoldOptUnSkippedTxtSrcStart = CHARPN;  /* "" */
              continue;
            }
          else if (lastTxtSrcOptUnSkippedEnd != CHARPN)
            {
              keyFoldStart = lastTxtSrcOptUnSkippedKeyFoldStart;
              s = lastTxtSrcOptUnSkippedEnd;
              txtFoldStart = txtFoldEnd;        /* force queue re-start */
              lastTxtSrcOptUnSkippedEnd = CHARPN;          /* now skipped */
              lastTxtSrcOptUnSkippedKeyFoldStart = TXUNIFOLDCHARPN; /* "" */
              continue;
            }
          else                                  /* chars differ, period */
            break;
        }
      keyFoldStart++;
      txtFoldStart++;
    }
