#ifndef EPI_UNICODE_H
#define EPI_UNICODE_H


#ifndef OS_H
#  include "os.h"                               /* for ARGS() */
#endif /* !OS_H */
#ifndef SIZES_H
#  include "sizes.h"                            /* for word for pm.h */
#endif /* !SIZES_H */
#ifndef PM_H
#  include "pm.h"                               /* for DYNABYTE */
#endif /* !PM_H */
#ifndef API3_H
#  include "api3.h"                             /* for APICP */
#endif /* !API3_H */

/* Max length of a UTF-8 character sequence, per RFC3629 and our code: */
#define TX_MAX_UTF8_BYTE_LEN    4

/* Reserved codepoint range for UTF-16 surrogates: no legal characters in it.
 * Note that TXutf8DecodeChar() assumes this range is all 3-byte UTF-8:
 */
#define TX_UTF16_SURROGATE_CODEPOINT_BEGIN      TX_UTF16_HI_SURROGATE_BEGIN
#define TX_UTF16_SURROGATE_CODEPOINT_END        TX_UTF16_LO_SURROGATE_END
/* Surrogate range has hi and lo sub-ranges: */
#define TX_UTF16_HI_SURROGATE_BEGIN     0xD800  /* for hi 10 bits of char */
#define TX_UTF16_HI_SURROGATE_END       0xDBFF
#define TX_UTF16_LO_SURROGATE_BEGIN     0xDC00  /* for lo 10 bits of char */
#define TX_UTF16_LO_SURROGATE_END       0xDFFF

/* Maximum Unicode code point (as of at least version 3+): */
#define TX_UNICODE_CODEPOINT_MAX        0x10FFFF

#define TX_IS_VALID_UNICODE_CODEPOINT(u)                                \
  ((unsigned)(u) < (unsigned)TX_UTF16_SURROGATE_CODEPOINT_BEGIN ||      \
   ((unsigned)(u) > (unsigned)TX_UTF16_SURROGATE_CODEPOINT_END &&       \
    (unsigned)(u) <= (unsigned)TX_UNICODE_CODEPOINT_MAX))

/* Character to use for invalid/out-of-range/no-mapping UTF/Unicde character.
 * (note that literal '?' may still be used in some places in old code):
 */
#define TX_INVALID_CHAR '?'

typedef enum TXCFF_tag                          /* character fold flags */
{
 TXCFF_UNKNOWN = -1,
  /* NOTE: these modes/flags may end up in SYSINDEX.PARAMS or may get sent
   * as ints over TISP api, and these values are assumed in unicode.c for
   * linear arrays, and assumed for TX3dbiScoreIndex(); do not change here.
   * NOTE: see unicode.c, TX3dbiScoreIndex() etc. if this enum changes.
   */

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   * Bits 0-3 reserved for case-fold mode enum:
   * NOTE: case modes may have an effect even if TXCFF_CASESTYLE_RESPECT,
   * ie. for certain ligature expansions:
   */
  TXCFF_CASEMODE_CTYPE = 0,                    /* use <ctype.h> functions */
  TXCFF_CASEMODE_UNICODEMONO,                   /* mono-char Unicode fold */
  TXCFF_CASEMODE_UNICODEMULTI,                 /* multi-char Unicode fold */
  /* ... future case-fold modes here, up through 15 ... */
  TXCFF_CASEMODE_MAX,                           /* must be last mode */
#define TXCFF_GET_CASEMODE(x)           ((x) & 0xf)
#define TXCFF_SUBST_CASEMODE(x, ct)     (((x) & ~0xf) | (ct))

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   * Bits 4-6 reserved for case-fold style enum:
   */
#define TXCFF_MK_CASESTYLE(ct)  ((ct) << 4)     /* internal use */
  TXCFF_CASESTYLE_RESPECT       = TXCFF_MK_CASESTYLE(0), /* case-sensitive */
  TXCFF_CASESTYLE_IGNORE        = TXCFF_MK_CASESTYLE(1), /* ignore case */
  TXCFF_CASESTYLE_UPPER         = TXCFF_MK_CASESTYLE(2), /* uppercase */
  TXCFF_CASESTYLE_LOWER         = TXCFF_MK_CASESTYLE(3), /* lowercase */
  TXCFF_CASESTYLE_TITLE         = TXCFF_MK_CASESTYLE(4), /* titlecase */
  /* ... future case-style modes here ... */
  TXCFF_CASESTYLE_MAX           = TXCFF_MK_CASESTYLE(5), /* must be last */
#define TXCFF_GET_CASESTYLE(x)          ((x) & 0x70)
#define TXCFF_SUBST_CASESTYLE(x, ct)    (((x) & ~0x70) | (ct))

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   * Optional flags to bit-wise OR with a given TXCFF_CASEMODE and CASESTYLE.
   * These are roughly in order of increasing significance, for
   * TX3dbiScoreIndex():
   */
#define TXCFF_GET_FLAGS(x)      ((x) & ~0x7f)

  /* ... future less-significant flags here ie. (1 << 12) thru (1 << 7) ... */

#define TXCFF_FLAG_MIN          TXCFF_IGNWIDTH
  /* TXCFF_IGNWIDTH: ignore fullwidth/halfwidth differences.
   * See ignWidth script:
   */
  TXCFF_IGNWIDTH                = (1 << 13),

  /* TXCFF_EXPDIACRITICS: expand phonological umlauts that add a letter,
   * ie. other than to NF[K]D form, eg. `u-umlaut' to `u' `e', only
   * for `a' `o' `u' (German).  Also `o-circumflex' to `o' `s' for French.
   * Applied before (and instead of) TXCFF_IGNDIACRITICS because latter
   * would remove these.
   * NOTE: expanded `e'/`s' is optional-match, eg. in case text author
   * forgot both diacritic and `e': `f u-umlaut r' matches `f u r'.
   * NOTE: neither expanded vowel nor optional `e'/`s' will match a
   * TXCFF_IGNDIACRITICS-stripped char; prevents `f u-umlaut r' from
   * matching `f u e-acute r'.
   * NOTE: an optional `e'/`s' cannot "match" a different-char optional
   * `e'/`s' (ie. skip both), so `o-circumflex' does not match `o-umlaut'.
   * NOTE: an optional `e'/`s' that matches an optional `e'/`s' will not
   * skip either one (ie. both are "used up" to match each other, not
   * later chars), so that `f u-umlaut r' does not match `f u-umlaut e r'.
   */
  TXCFF_EXPDIACRITICS           = (1 << 14),

  /* TXCFF_EXPLIGATURES: expand ligatures, eg. `oe' to `o' `e'.  Note
   * that some expansions here also happen in TXCFF_CASEMODE_UNICODEMULTI
   * + TXCFF_CASESTYLE_IGNORECASE (eg. `ffi' to `f' `f' `i' is in both).
   * Applied before TXCFF_CASESTYLE_... since some TXCFF_EXPLIGATURES
   * source characters are not in CaseFolding.txt, but all
   * TXCFF_EXPLIGATURES target (expanded) characters are in it.  See
   * expLigatures script:
   */
  TXCFF_EXPLIGATURES            = (1 << 15),

  /* TXCFF_IGNDIACRITICS: ignore non-starter (CombiningClass > 0) or
   * modifier symbols (GeneralCategory Sk) characters (eg. accent etc.)
   * resulting from NFD decomposition, if not otherwise removed by eg.
   * TXCFF_EXPDIACRITICS.  See ignDiacritics script for further details.
   * Applied before TXCFF_IGNWIDTH because there are fewer halfwidth/narrow
   * characters with diacritics; applying this after TXCFF_IGNWIDTH could
   * leave some characters still fullwidth.  Also applied before
   * TXCFF_CASESTYLE_... because fewer NFD decompositions happen that way:
   */
  TXCFF_IGNDIACRITICS           = (1 << 16),

  /*   By default we take valid UTF-8 as such, then any invalid bytes as
   * ISO-8859-1 *and* map them to UTF-8.  This supports all-UTF-8
   * text, as well as most ISO-8859-1 text (ie. except when 2+ adjacent
   * ISO-8859-1 bytes make a valid UTF-8), which should help when the
   * encoding is unknown.  The reason we map invalid/ISO to UTF-8 is so
   * that strfoldcmp(a, b) === memcmp(strfold(a), strfold(b)) always
   * (for Metamorph index dictionary B-tree), and so that an ISO/UTF-8
   * query will match ISO/UTF-8 text.
   *   Alternately, TXCFF_ISO88591 takes all bytes as
   * ISO-8859-1, and maps to ISO only (also note ignore-case difference
   * for U+00B5: stays as-is whereas mapped to U+03BC in UTF-8): to be
   * used when encoding is known to be all ISO-8859-1.
   *   There is no strict-UTF-8 mode (ie. take valid-UTF-8 as such,
   * leave all else as-is, or ignore, or?): user should have translated
   * the "all-else" to UTF-8 already anyway.
   */
  TXCFF_ISO88591                = (1 << 17),
#define TXCFF_FLAG_MAX          TXCFF_ISO88591

  /* Internal-use only flags below: - - - - - - - - - - - - - - - - - - - - */

  /* TXCFF_PREFIX: TXunicodeStrFoldCmp returns 1000 if `a' is prefix of `b'.
   * Internal use:
   */
  TXCFF_PREFIX                  = (1 << 18)

  /* ... future internal flags here ... */

  /* Known flags (except TXCFF_PREFIX which is internal use): */
#define TXCFF_KNOWN_FLAGS       (TXCFF_IGNWIDTH | TXCFF_EXPDIACRITICS | \
  TXCFF_EXPLIGATURES | TXCFF_IGNDIACRITICS | TXCFF_ISO88591)
}
TXCFF;
#define TXCFFPN ((TXCFF *)NULL)

#define TXCFF_IS_VALID_MODEFLAGS(mf)                    \
  (TXCFF_GET_CASEMODE(mf) < TXCFF_CASEMODE_MAX &&       \
   TXCFF_GET_CASESTYLE(mf) < TXCFF_CASESTYLE_MAX &&     \
   (TXCFF_GET_FLAGS(mf) & ~TXCFF_KNOWN_FLAGS) == (TXCFF)0)

/* Default folding modes, for text search and string compare.
 *
 * For text search, should handle both UTF-8 and ISO-8859-1, since we
 * may not know which format we will be passed without more info;
 * hence TXCFF_ISO88591 is off by default.  Most flags (except not-
 * correctly-supported-by-index-yet flags eg. TXCFF_EXPDIACRITICS Bug
 * 2207 WTF) are enabled by default, on the theory that it's better to
 * find something undesired than not find something desired, and this
 * is a looser, word match (ie. historically case-insensitive).
 *
 * For string compare mode, we are looking for exact character match,
 * not looser same-word match, so all flags and foldings are off.
 * However, since it is still usually a *character* match and not a
 * *byte* match (ie. varchar not varbyte), we leave TXCFF_ISO88591
 * off, so that a hi-bit ISO-8859-1 char matches the same hi-bit UTF-8
 * char.  This is different from pre-version-5 behavior (which was
 * memcmp() unless ignorecase set), but any var*char* fields with
 * hi-bit bytes are almost certainly real ISO/UTF-8 chars, not image
 * data etc. which would be in var*byte* (and hence stringcomparemode
 * ignored).  Plus, the new mode only applies to new indexes (via
 * SYSINDEX.PARAMS) not existing indexes, and can be reverted site-wide
 * with texis.cnf.
 */
#define TXCFF_TEXTSEARCHMODE_DEFAULT_NEW                        \
  (TXCFF_CASEMODE_UNICODEMULTI | TXCFF_CASESTYLE_IGNORE |       \
   TXCFF_IGNWIDTH | TXCFF_EXPLIGATURES | TXCFF_IGNDIACRITICS)
#define TXCFF_STRINGCOMPAREMODE_DEFAULT_NEW                     \
  (TXCFF_CASEMODE_UNICODEMULTI | TXCFF_CASESTYLE_RESPECT)
/* Old defaults (before textsearchmode stored in Metamorph index): */
#define TXCFF_TEXTSEARCHMODE_DEFAULT_OLD        \
  (TXCFF_CASEMODE_CTYPE | TXCFF_CASESTYLE_IGNORE | TXCFF_ISO88591)
#define TXCFF_STRINGCOMPAREMODE_DEFAULT_OLD     \
  (TXCFF_CASEMODE_CTYPE | TXCFF_CASESTYLE_RESPECT | TXCFF_ISO88591)

#if TX_VERSION_MAJOR >= 6
#  define TXCFF_TEXTSEARCHMODE_DEFAULT     TXCFF_TEXTSEARCHMODE_DEFAULT_NEW
#  define TXCFF_STRINGCOMPAREMODE_DEFAULT  TXCFF_STRINGCOMPAREMODE_DEFAULT_NEW
#else /* TX_VERSION_MAJOR < 6 */
#  define TXCFF_TEXTSEARCHMODE_DEFAULT     TXCFF_TEXTSEARCHMODE_DEFAULT_OLD
#  define TXCFF_STRINGCOMPAREMODE_DEFAULT  TXCFF_STRINGCOMPAREMODE_DEFAULT_OLD
#endif /* TX_VERSION_MAJOR < 6 */

int     TXstrToTxcff ARGS((CONST char *s, CONST char *e, TXCFF textSearchMode,
                           TXCFF stringCompareMode, TXCFF curVal,
                           int whichSetting, TXCFF defVal, TXCFF *newVal));
size_t  TXtxcffToStr ARGS((char *s, size_t sz, TXCFF modeFlags));
/* Max byte size of buffer needed by TXtxcffToStr(): */
#define TX_TXCFFTOSTR_MAXSZ     (108 + EPI_OS_INT_BITS/4)

/* A Unicode character (not UTF-8 or otherwise encoded): */
typedef EPI_INT32       TXUNICHAR;
#define TXUNICHARPN     ((TXUNICHAR *)NULL)
#define TXUNICHARPPN    ((TXUNICHAR **)NULL)

/* Encodes Unicode char `ch' to `buf', incrementing it on success.
 * NOTE: TX_IS_VALID_UNICODE_CODEPOINT(ch) is assumed to be true:
 */
#define TX_UNICODE_ENCODE_UTF8_CHAR(buf, bufEnd, ch, ifBufShort)        \
  if ((ch) < 0x80)                              /* encode as-is */      \
    {                                                                   \
      if ((buf) < (bufEnd)) *(buf)++ = (ch);                            \
      else ifBufShort;                                                  \
    }                                                                   \
  else if ((ch) < 0x800)                        /* 2-byte UTF-8 seq */  \
    {                                                                   \
      if ((buf) + 1 < (bufEnd))                                         \
        {                                                               \
          *(buf)++ = (0xC0 | ((ch) >> 6));                              \
          *(buf)++ = (0x80 | ((ch) & 0x3F));                            \
        }                                                               \
      else ifBufShort;                                                  \
    }                                                                   \
  else if ((ch) < 0x10000)                      /* 3-byte UTF-8 seq */  \
    {                                                                   \
      if ((buf) + 2 < (bufEnd))                                         \
        {                                                               \
          *(buf)++ = (0xE0 | ((ch) >> 12));                             \
          *(buf)++ = (0x80 | (((ch) >> 6) & 0x3F));                     \
          *(buf)++ = (0x80 | ((ch) & 0x3F));                            \
        }                                                               \
      else ifBufShort;                                                  \
    }                                                                   \
  else                                          /* 4-byte UTF-8 seq */  \
    {                                                                   \
      if ((buf) + 3 < (bufEnd))                                         \
        {                                                               \
          *(buf)++ = (0xF0 | ((ch) >> 18));                             \
          *(buf)++ = (0x80 | (((ch) >> 12) & 0x3F));                    \
          *(buf)++ = (0x80 | (((ch) >> 6) & 0x3F));                     \
          *(buf)++ = (0x80 | ((ch) & 0x3F));                            \
        }                                                               \
      else ifBufShort;                                                  \
    }

char   *TXunicodeEncodeUtf8Char ARGS((char *d, char *de, TXUNICHAR ch));
TXUNICHAR TXunicodeDecodeUtf8Char ARGS((CONST char **sp, CONST char *end,
                                        int badAsIso));
TXUNICHAR TXunicodeDecodeUtf8CharBackwards ARGS((CONST char **sp,
                                                 CONST char *start));
#define TXUNICHAR_SHORT_BUFFER          (-2)
#define TXUNICHAR_INVALID_SEQUENCE      (-1)
char   *TXunicodeGetUtf8CharOffset ARGS((CONST char *utf8Str, CONST char *e,
                                         size_t *numCharsPtr));
char   *TXunicodeEncodeUtf16Char ARGS((char *d, char *de, TXUNICHAR ch,
                                       int doLittleEndian));
TXUNICHAR TXunicodeDecodeUtf16CharBackwards(const char **sp,
                                        const char *start, int littleEndian);
size_t  TXunicodeStrFold ARGS((char *dest, size_t destSz, CONST char *src,
                               size_t srcLen, TXCFF modeFlags));

int TXunicodeStrFoldCmp ARGS((CONST char **ap, size_t alen, CONST char **bp,
                              size_t blen, TXCFF modeFlags));
int TXunicodeStrFoldIsEqualBackwards ARGS((CONST char **ap, size_t alen,
                                           CONST char **bp, size_t blen,
                                           TXCFF modeFlags));

#define TX_STRFOLDCMP_ISPREFIX 1000

int TXisSpmSearchable ARGS((CONST char *key, size_t keyLen, TXCFF modeFlags,
                            int hyphenPhrase, CONST int **cmpTabP));

/* -------------------------- Unicode Pattern Matcher --------------------- */

#ifndef TXUQIPN
typedef struct TXUQI_tag        TXUQI;
#  define TXUQIPN       ((TXUQI *)NULL)
#endif /* !TXUQIPN */

/* may be typedef'd elsewhere too: */
#ifndef TXUPMPN
typedef struct TXUPM_tag        TXUPM;
#  define TXUPMPN       ((TXUPM *)NULL)
#endif /* !TXUPMPN */

TXUPM *TXtxupmOpen ARGS((CONST char *key, size_t keyLen, TXCFF modeFlags));
TXUPM *TXtxupmClose ARGS((TXUPM *upm));
CONST byte *TXtxupmFind ARGS((TXUPM *upm, CONST byte *buf, CONST byte *bufEnd,
                              TXPMOP op));
size_t TXtxupmGetHitSz ARGS((TXUPM *upm));

int    TXunicodeUtf8StrRev ARGS((char *s));
int    TXunicodeIsWildcardMatch ARGS((CONST char *wildExpr,
               CONST char *wildExprEnd, CONST char *tameText,
               CONST char *tameTextEnd, int ignoreCase));

#ifdef EPI_ENABLE_TEXTSEARCHMODE
#  define TxTextsearchmodeEnabled       1
#else /* !EPI_ENABLE_TEXTSEARCHMODE */
/* initialized in TXinitapp(): */
extern int      TxTextsearchmodeEnabled;
#endif /* !EPI_ENABLE_TEXTSEARCHMODE */

#endif /* !EPI_UNICODE_H */
