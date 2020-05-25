#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <ctype.h>
#ifdef MSDOS
#  include <io.h>
#endif
#include "texint.h"
#include "http.h"

typedef struct HTCSA_tag                        /* charset alias */
{
  char          *name;                          /* the alias */
  size_t        charsetIndex;                   /* index into HTCS array */
}
HTCSA;
#define HTCSAPN ((HTCSA *)NULL)

struct HTCSCFG_tag                              /* charset config object */
{
  size_t        refCount;                       /* number of users */
  HTCSINFO      *charsets;                      /* list of charsets */
  size_t        numCharsets;                    /* length of `charsets' */
  HTCSA         *aliases;                       /* list of aliases */
  size_t        numAliases;                     /* length of `aliases' */
};

/* ------------------------------------------------------------------------ */

/* Default charsets.conf location (appended to install dir).
 * Overridden by [Texis] Charset Config in texis.ini:
 */
static CONST char               CharsetConfigSuffix[] =
  PATH_SEP_S "conf" PATH_SEP_S "charsets.conf";

static CONST char * CONST       CharsetName[HTCHARSET_NUM] =
{
#undef I
#define I(tok, cn, tf, tl, ff, fl)  cn,
  HTCHARSET_LIST
#undef I
};

static CONST HTCSINFO   ConfigCharsets[] =
{
  /* We define an HTCS for every internal charset -- even if no
   * aliases -- so that the ConfigCharsets index equals the HTCHARSET
   * value (-1 for HTCHARSET_UNKNOWN skip), for use in ConfigAliases.
   * It also allows htstr2charset() to return a struct (and a
   * permanent one at that) for internal charsets (when no alias).
   * Note that we skip the first HTCHARSET_UNKNOWN charset when using this:
   */
#undef I
#define I(tok, cn, tf, tl, ff, fl)      { HTCHARSET_##tok, CHARPN },
  HTCHARSET_LIST
#undef I
};

#define CCSKIP  1               /* skip ConfigCharsets[HTCHARSET_UNKNOWN] */

/* NOTE: This *must* be in ascending sorted order, by alias name,
 * ignoring (but retaining) case, `-', `_', `:', '.'.  See htstr2charset():
 */
static CONST HTCSA      ConfigAliases[] =
{
  /* @@@ALIASES_BEGIN@@@   makefile marker do not remove or edit */
  { "8859-1",                   HTCHARSET_ISO_8859_1 - CCSKIP   },
  { "8859-10",                  HTCHARSET_ISO_8859_10 - CCSKIP  },
  { "8859-11",                  HTCHARSET_ISO_8859_11 - CCSKIP  },
  { "8859-13",                  HTCHARSET_ISO_8859_13 - CCSKIP  },
  { "8859-14",                  HTCHARSET_ISO_8859_14 - CCSKIP  },
  { "8859-15",                  HTCHARSET_ISO_8859_15 - CCSKIP  },
  { "8859-16",                  HTCHARSET_ISO_8859_16 - CCSKIP  },
  { "8859-2",                   HTCHARSET_ISO_8859_2 - CCSKIP   },
  { "8859-3",                   HTCHARSET_ISO_8859_3 - CCSKIP   },
  { "8859-4",                   HTCHARSET_ISO_8859_4 - CCSKIP   },
  { "8859-5",                   HTCHARSET_ISO_8859_5 - CCSKIP   },
  { "8859-6",                   HTCHARSET_ISO_8859_6 - CCSKIP   },
  { "8859-7",                   HTCHARSET_ISO_8859_7 - CCSKIP   },
  { "8859-8",                   HTCHARSET_ISO_8859_8 - CCSKIP   },
  { "8859-9",                   HTCHARSET_ISO_8859_9 - CCSKIP   },
  { "ANSI_X3.4-1968",           HTCHARSET_US_ASCII - CCSKIP     },
  { "ANSI_X3.4-1986",           HTCHARSET_US_ASCII - CCSKIP     },
  { "arabic",                   HTCHARSET_ISO_8859_6 - CCSKIP   },
  { "ASCII",                    HTCHARSET_US_ASCII - CCSKIP     },
  { "ASMO-708",                 HTCHARSET_ISO_8859_6 - CCSKIP   },
  { "CP-1250",                  HTCHARSET_WINDOWS_1250 - CCSKIP },
  { "CP-1251",                  HTCHARSET_WINDOWS_1251 - CCSKIP },
  { "CP-1252",                  HTCHARSET_WINDOWS_1252 - CCSKIP },
  { "CP-1253",                  HTCHARSET_WINDOWS_1253 - CCSKIP },
  { "CP-1254",                  HTCHARSET_WINDOWS_1254 - CCSKIP },
  { "CP-1255",                  HTCHARSET_WINDOWS_1255 - CCSKIP },
  { "CP-1256",                  HTCHARSET_WINDOWS_1256 - CCSKIP },
  { "CP-1257",                  HTCHARSET_WINDOWS_1257 - CCSKIP },
  { "CP-1258",                  HTCHARSET_WINDOWS_1258 - CCSKIP },
  { "CP367",                    HTCHARSET_US_ASCII - CCSKIP     },
  { "CP819",                    HTCHARSET_ISO_8859_1 - CCSKIP   },
  { "CP-874",                   HTCHARSET_WINDOWS_874 - CCSKIP  },
  { "csASCII",                  HTCHARSET_US_ASCII - CCSKIP     },
  { "csISOLatin1",              HTCHARSET_ISO_8859_1 - CCSKIP   },
  { "csISOLatin2",              HTCHARSET_ISO_8859_2 - CCSKIP   },
  { "csISOLatin3",              HTCHARSET_ISO_8859_3 - CCSKIP   },
  { "csISOLatin4",              HTCHARSET_ISO_8859_4 - CCSKIP   },
  { "csISOLatin5",              HTCHARSET_ISO_8859_9 - CCSKIP   },
  { "csISOLatin6",              HTCHARSET_ISO_8859_10 - CCSKIP  },
  { "csISOLatinArabic",         HTCHARSET_ISO_8859_6 - CCSKIP   },
  { "csISOLatinCyrillic",       HTCHARSET_ISO_8859_5 - CCSKIP   },
  { "csISOLatinGreek",          HTCHARSET_ISO_8859_7 - CCSKIP   },
  { "csISOLatinHebrew",         HTCHARSET_ISO_8859_8 - CCSKIP   },
  { "cyrillic",                 HTCHARSET_ISO_8859_5 - CCSKIP   },
  { "ECMA-114",                 HTCHARSET_ISO_8859_6 - CCSKIP   },
  { "ECMA-118",                 HTCHARSET_ISO_8859_7 - CCSKIP   },
  { "ELOT_928",                 HTCHARSET_ISO_8859_7 - CCSKIP   },
  { "greek",                    HTCHARSET_ISO_8859_7 - CCSKIP   },
  { "greek8",                   HTCHARSET_ISO_8859_7 - CCSKIP   },
  { "hebrew",                   HTCHARSET_ISO_8859_8 - CCSKIP   },
  { "IBM367",                   HTCHARSET_US_ASCII - CCSKIP     },
  { "IBM819",                   HTCHARSET_ISO_8859_1 - CCSKIP   },
  { "ISO_646.irv:1991",         HTCHARSET_US_ASCII - CCSKIP     },
  { "ISO646-US",                HTCHARSET_US_ASCII - CCSKIP     },
  { "ISO_8859-10:1992",         HTCHARSET_ISO_8859_10 - CCSKIP  },
  { "ISO_8859-1:1987",          HTCHARSET_ISO_8859_1 - CCSKIP   },
  { "ISO_8859-14:1998",         HTCHARSET_ISO_8859_14 - CCSKIP  },
  { "ISO_8859-15:1998",         HTCHARSET_ISO_8859_15 - CCSKIP  },
  { "ISO_8859-16:2001",         HTCHARSET_ISO_8859_16 - CCSKIP  },
  { "ISO_8859-2:1987",          HTCHARSET_ISO_8859_2 - CCSKIP   },
  { "ISO_8859-3:1988",          HTCHARSET_ISO_8859_3 - CCSKIP   },
  { "ISO_8859-4:1988",          HTCHARSET_ISO_8859_4 - CCSKIP   },
  { "ISO_8859-5:1988",          HTCHARSET_ISO_8859_5 - CCSKIP   },
  { "ISO_8859-6:1987",          HTCHARSET_ISO_8859_6 - CCSKIP   },
  { "ISO_8859-7:1987",          HTCHARSET_ISO_8859_7 - CCSKIP   },
  { "ISO_8859-8:1988",          HTCHARSET_ISO_8859_8 - CCSKIP   },
  { "ISO_8859-9:1989",          HTCHARSET_ISO_8859_9 - CCSKIP   },
  { "iso-celtic",               HTCHARSET_ISO_8859_14 - CCSKIP  },
  { "iso-ir-100",               HTCHARSET_ISO_8859_1 - CCSKIP   },
  { "iso-ir-101",               HTCHARSET_ISO_8859_2 - CCSKIP   },
  { "iso-ir-109",               HTCHARSET_ISO_8859_3 - CCSKIP   },
  { "iso-ir-110",               HTCHARSET_ISO_8859_4 - CCSKIP   },
  { "iso-ir-126",               HTCHARSET_ISO_8859_7 - CCSKIP   },
  { "iso-ir-127",               HTCHARSET_ISO_8859_6 - CCSKIP   },
  { "iso-ir-138",               HTCHARSET_ISO_8859_8 - CCSKIP   },
  { "iso-ir-144",               HTCHARSET_ISO_8859_5 - CCSKIP   },
  { "iso-ir-148",               HTCHARSET_ISO_8859_9 - CCSKIP   },
  { "iso-ir-157",               HTCHARSET_ISO_8859_10 - CCSKIP  },
  { "iso-ir-179",               HTCHARSET_ISO_8859_13 - CCSKIP  },
  { "iso-ir-199",               HTCHARSET_ISO_8859_14 - CCSKIP  },
  { "iso-ir-203",               HTCHARSET_ISO_8859_15 - CCSKIP  },
  { "iso-ir-226",               HTCHARSET_ISO_8859_16 - CCSKIP  },
  { "iso-ir-6",                 HTCHARSET_US_ASCII - CCSKIP     },
  { "l1",                       HTCHARSET_ISO_8859_1 - CCSKIP   },
  { "l10",                      HTCHARSET_ISO_8859_16 - CCSKIP  },
  { "l2",                       HTCHARSET_ISO_8859_2 - CCSKIP   },
  { "l3",                       HTCHARSET_ISO_8859_3 - CCSKIP   },
  { "l4",                       HTCHARSET_ISO_8859_4 - CCSKIP   },
  { "l5",                       HTCHARSET_ISO_8859_9 - CCSKIP   },
  { "l6",                       HTCHARSET_ISO_8859_10 - CCSKIP  },
  { "l7",                       HTCHARSET_ISO_8859_13 - CCSKIP  },
  { "l8",                       HTCHARSET_ISO_8859_14 - CCSKIP  },
  { "latin1",                   HTCHARSET_ISO_8859_1 - CCSKIP   },
  { "latin10",                  HTCHARSET_ISO_8859_16 - CCSKIP  },
  { "latin2",                   HTCHARSET_ISO_8859_2 - CCSKIP   },
  { "latin3",                   HTCHARSET_ISO_8859_3 - CCSKIP   },
  { "latin4",                   HTCHARSET_ISO_8859_4 - CCSKIP   },
  { "latin5",                   HTCHARSET_ISO_8859_9 - CCSKIP   },
  { "latin6",                   HTCHARSET_ISO_8859_10 - CCSKIP  },
  { "latin7",                   HTCHARSET_ISO_8859_13 - CCSKIP  },
  { "latin8",                   HTCHARSET_ISO_8859_14 - CCSKIP  },
  { "Latin-9",                  HTCHARSET_ISO_8859_15 - CCSKIP  },
  { "MS-ANSI",                  HTCHARSET_WINDOWS_1252 - CCSKIP },
  { "MS-ARAB",                  HTCHARSET_WINDOWS_1256 - CCSKIP },
  { "MS-CYRL",                  HTCHARSET_WINDOWS_1251 - CCSKIP },
  { "MS-EE",                    HTCHARSET_WINDOWS_1250 - CCSKIP },
  { "MS-GREEK",                 HTCHARSET_WINDOWS_1253 - CCSKIP },
  { "MS-HEBR",                  HTCHARSET_WINDOWS_1255 - CCSKIP },
  { "MS-TURK",                  HTCHARSET_WINDOWS_1254 - CCSKIP },
  { "US",                       HTCHARSET_US_ASCII - CCSKIP     },
  { "WINBALTRIM",               HTCHARSET_WINDOWS_1257 - CCSKIP },
  /* @@@ALIASES_END@@@   makefile marker do not remove or edit */
};

static CONST HTCSCFG    TxCharsetConfigDefault =
{
  1,
  (HTCSINFO *)(ConfigCharsets + CCSKIP),
  sizeof(ConfigCharsets)/sizeof(ConfigCharsets[0]) - CCSKIP,
  (HTCSA *)ConfigAliases,    sizeof(ConfigAliases)/sizeof(ConfigAliases[0]),
};

static CONST char       Whitespace[] = " \t\v\f\r\n";
#define EOLSPACE        (Whitespace + 4)
/* note: HORZSPACE/ColonHorzspace cannot have EOLSPACE chars: */
static CONST char       ColonHorzspace[] = ": \t\v\f";
#define HORZSPACE       (ColonHorzspace + 1)

/* ------------------------------------------------------------------------ */

static int htstrnipunctcmp ARGS((CONST char *a, size_t an, CONST char *b,
                                 size_t bn));
static int
htstrnipunctcmp(a, an, b, bn)
CONST char	*a;
size_t		an;
CONST char	*b;
size_t		bn;
/* Compares up to `an' chars of `a' with up to `bn' chars of `b',
 * ignoring case and skipping certain punctuation.  Return value ala strcmp().
 * If `an' or `bn' is -1, strlen(a) or strlen(b) is assumed.
 */
{
  CONST char    *ae, *be;
  int           ac, bc;
#define ISIGNORE(ch) ((ch) == '-' || (ch) == '_' || (ch) == ':' || (ch) == '.')
#define TOUPPER(ch)  ((ch) >= 'a' && (ch) <= 'z' ? (ch) - ('a' - 'A') : (ch))

  ae = (an == (size_t)(-1) ? (CONST char *)EPI_OS_VOIDPTR_MAX : a + an);
  be = (bn == (size_t)(-1) ? (CONST char *)EPI_OS_VOIDPTR_MAX : b + bn);
  while (a < ae && b < be && *a != '\0' && *b != '\0')
    {
      if (ISIGNORE(*a)) { a++; continue; }
      if (ISIGNORE(*b)) { b++; continue; }
      ac = TOUPPER(*a);
      bc = TOUPPER(*b);
      if (ac != bc) return(ac - bc);            /* differ */
      a++;
      b++;
    }
  while (a < ae && ISIGNORE(*a)) a++;
  while (b < be && ISIGNORE(*b)) b++;
  ac = (a < ae ? TOUPPER(*a) : 0);
  bc = (b < be ? TOUPPER(*b) : 0);
  return(ac - bc);
#undef ISIGNORE
#undef TOUPPER
}

/* ------------------------------------------------------------------------ */

CONST HTCSINFO *
htstr2charset(cfg, s, e)
CONST HTCSCFG   *cfg;   /* (in, opt.) config (NULL: internal-no-aliases) */
CONST char      *s;     /* (in) start of string */
CONST char      *e;     /* (in, opt.) end of string (s + strlen(s) if NULL) */
/* Returns HTCSINFO * for `s' (ends at `e', or s+strlen(s) if NULL);
 * or NULL if unknown.  `cfg' may be NULL for just internal charsets
 * (no aliases).  Object pointed to by return value should be copied ASAP.
 */
{
  HTCSA                 *csa;
  CONST HTCSINFO        *cs;
  size_t                l, r, i;
  int                   cmp;

  if (e == CHARPN) e = s + strlen(s);

  /* 1st: check aliases so that even known charsets can be remapped: */
  if (cfg != HTCSCFGPN)
    {
      l = 0;
      r = cfg->numAliases;
      while (l < r)                             /* binary search */
        {
          i = (l + r)/2;
          csa = cfg->aliases + i;
          cmp = htstrnipunctcmp(s, e - s, csa->name, -1);
          if (cmp < 0) r = i;
          else if (cmp > 0) l = i + 1;
          else return(cfg->charsets + csa->charsetIndex);   /* found it */
        }

      /* 2nd: Check charsets, for new charsets, future HTCSINFO info: */
      l = 0;
      r = cfg->numCharsets;
      while (l < r)                             /* binary search */
        {
          i = (l + r)/2;
          cs = cfg->charsets + i;
          cmp = htstrnipunctcmp(s, e - s, (cs->tok == HTCHARSET_UNKNOWN ?
                                       cs->buf : CharsetName[cs->tok]), -1);
          if (cmp < 0) r = i;
          else if (cmp > 0) l = i + 1;
          else return(cs);                      /* found it */
        }
    }

  /* 3rd: check all internal charsets, so we at least get the token
   * mapping and/or any future HTCSINFO info defaults.  Note that we
   * assume `ConfigCharsets' has an entry for every HTCHARSET (and
   * only those), unlike `cfg->charsets' which may be missing some or
   * have non-internals:
   */
  if (cfg == HTCSCFGPN ||
      ConfigCharsets + CCSKIP != cfg->charsets) /* optimization */
    {
      l = CCSKIP;
      r = sizeof(ConfigCharsets)/sizeof(ConfigCharsets[0]);
      while (l < r)                             /* binary search */
        {
          i = (l + r)/2;
          cs = ConfigCharsets + i;
          cmp = htstrnipunctcmp(s, e - s, (cs->tok == HTCHARSET_UNKNOWN ?
                                        cs->buf : CharsetName[cs->tok]), -1);
          if (cmp < 0) r = i;
          else if (cmp > 0) l = i + 1;
          else return(cs);                      /* found it */
        }
    }

  /* Not found: */
  return(HTCSINFOPN);
}

/* ------------------------------------------------------------------------ */

CONST char *
htcharset2str(charset)
HTCHARSET       charset;
{
  if ((unsigned)charset >= (unsigned)HTCHARSET_NUM)
    return(CharsetName[HTCHARSET_UNKNOWN]);
  return(CharsetName[(unsigned)charset]);
}

/* ------------------------------------------------------------------------ */

int
TXcharsetConfigOpenFromText(cfgp, pmbuf, hterrnop, text, yap, filename)
HTCSCFG         **cfgp;         /* (out) returned config */
TXPMBUF         *pmbuf;         /* (out, opt.) for messages */
HTERR           *hterrnop;      /* (out, opt.) for error */
CONST char      *text;          /* (in, opt.) text buf (NULL: internal) */
int             yap;            /* 0: silent 1: non-ENOENT errs 2: all errs */
CONST char      *filename;      /* (in, opt.) for messages */
/* Reads and parses charset.conf buffer `text'.  If NULL, returns internal.
 * Returns 0 on error, 1 on partial success (some parse errors), 2 on success.
 * `text' can be NULL or "builtin" for internal default.
 * Sets `*cfgp' to object if not error, else NULL.
 */
{
  static CONST char     fn[] = "TXcharsetConfigOpenFromText";
  static CONST char     charset[] = "Charset";
  static CONST char     aliases[] = "Aliases";
  static CONST char     linemsg[] = "charset config line ";
  int                   cmp, ret = 2;
  size_t                numaliasesalloc = 0, numcharsetsalloc = 0;
  size_t                line, l, r, i, curCharsetIndex = -1;
  HTCSCFG               *cfg = HTCSCFGPN;
  HTCSINFO              *cs;
  CONST HTCSINFO        *csres;
  HTCSA                 *csa, *csaEnd;
  CONST char            *eob, *eol, *eolorg, *s, *e, *s2, *e2;
  CONST char            *synmsg;
  int                   igncharset = 0;

  if (text == CHARPN || strcmpi(text, "builtin") == 0)
    {
      cfg = (HTCSCFG *)&TxCharsetConfigDefault;
      goto done;
    }
  eob = text + strlen(text);

  /* Parse the buffer: - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if ((cfg = (HTCSCFG *)calloc(1, sizeof(HTCSCFG))) == HTCSCFGPN)
    goto maerr;
  cfg->refCount = 1;
  /* rest cleared by calloc() */
  line = 1;
  for (s = text; s < eob; s=eolorg+1,htskipeol((char**)&s,(char*)eob), line++)
    {
      eolorg = eol = s + strcspn(s, EOLSPACE);  /* get EOL */
      for (e = s; e < eol && *e != '#'; e++);   /* find possible comment */
      eol = e;                                  /* stop before it */
      s += strspn(s, HORZSPACE);                /* skip to first token */
      if (s >= eol) continue;                   /* blank/comment */
      e = s + strcspn(s, ColonHorzspace);       /* end of first token */
      if (e >= eol) e = eol;                    /* stop at comment/EOL */
      s2 = e + strspn(e, HORZSPACE);            /* colon after first token */
      if (*(s2++) != ':')                       /* colon must follow token */
        {
          synmsg = "Missing colon";
          goto synerr;
        }
      s2 += strspn(s2, HORZSPACE);              /* start of second token */
      e2 = s2 + strcspn(s2, Whitespace);        /* end of second token */
      if (e2 >= eol) e2 = eol;                  /* stop at comment/EOL */
      if (e - s == sizeof(charset) - 1 &&
          strnicmp(s, charset, sizeof(charset) - 1) == 0)
        {       /* - - - - - - - - - - - - - - - - - - - Charset: charset */
          igncharset = 0;
          if (s2 >= eol)
            {
              synmsg = "Missing charset name";
              goto synerr;
            }
          if (!TXincarray(pmbuf, (void **)(void *)&cfg->charsets,
                          cfg->numCharsets, &numcharsetsalloc,
                          sizeof(HTCSINFO)))
            goto maerrsilent;
          /* See if we've seen this charset already: */
          l = 0;
          r = cfg->numCharsets;
          while (l < r)                         /* binary search */
            {
              i = (l + r)/2;
              cs = cfg->charsets + i;
              cmp = htstrnipunctcmp(s2, e2 - s2,
                                    (cs->tok == HTCHARSET_UNKNOWN ?
                                     cs->buf : CharsetName[cs->tok]), -1);
              if (cmp < 0) r = i;
              else if (cmp > 0) l = i + 1;
              else break;                       /* found it */
            }
          if (l < r)                            /* charset already used */
            {
              if (yap)
                txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                "%s%s%ld: Duplicate Charset `%.*s'; skipping to next Charset",
                               (filename != CHARPN ? filename : linemsg),
                               (filename != CHARPN ? ":" : ""),
                               (long)line, (int)(e2 - s2), s2);
              ret = 1;                          /* partial failure */
              igncharset = 1;
              continue;                         /* next line */
            }
          curCharsetIndex = l;
          /* See if we've seen this as an alias already: */
          l = 0;
          r = cfg->numAliases;
          while (l < r)                         /* binary search */
            {
              i = (l + r)/2;
              cmp = htstrnipunctcmp(s2, e2 - s2, cfg->aliases[i].name, -1);
              if (cmp < 0) r = i;
              else if (cmp > 0) l = i + 1;
              else break;                       /* found it */
            }
          if (l < r)                            /* already used as alias */
            {
              if (yap)
                txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                              "%s%s%ld: Charset `%.*s' is a duplicate of an alias; skipping to next Charset",
                               (filename != CHARPN ? filename : linemsg),
                               (filename != CHARPN ? ":" : ""),
                               (long)line, (int)(e2 - s2), s2);
              ret = 1;                          /* partial failure */
              igncharset = 1;
              continue;
            }
          /* Add the charset: */
          if (curCharsetIndex < cfg->numCharsets)
            {                                   /* make room */
              memmove(cfg->charsets + curCharsetIndex + 1,
                      cfg->charsets + curCharsetIndex,
                      (cfg->numCharsets - curCharsetIndex)*sizeof(HTCSINFO));
              /* We just slid the charsets array; fix up charset indexes: */
              for (csa = cfg->aliases, csaEnd = csa + cfg->numAliases;
                   csa < csaEnd;
                   csa++)
                if (csa->charsetIndex >= curCharsetIndex) csa->charsetIndex++;
            }
          cs = cfg->charsets + curCharsetIndex;
          cfg->numCharsets++;
          /* Use NULL config, for no aliases, to get internal name: */
          csres = htstr2charset(HTCSCFGPN, s2, e2);
          if (csres == HTCSINFOPN)              /* not internal charset */
            {
              cs->tok = HTCHARSET_UNKNOWN;
              if ((cs->buf = (char *)malloc((e2 - s2) + 1)) == CHARPN)
                goto maerr;
              memcpy(cs->buf, s2, e2 - s2);
              cs->buf[e2 - s2] = '\0';
            }
          else                                  /* internal charset */
            {
              cs->tok = csres->tok;
              cs->buf = CHARPN;
            }
        }
      else if (e - s == sizeof(aliases) - 1 &&
               strnicmp(s, aliases, sizeof(aliases) - 1) == 0)
        {       /* - - - - - - - - - - - - - - - - - - Aliases: [alias ...] */
          if (igncharset) continue;             /* skipping entire section */
          if (curCharsetIndex == (size_t)(-1))  /* not in a Charset yet */
            {
              if (yap)
                txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                               "%s%s%ld: Syntax error: %.*s not permitted before first Charset; ignoring line",
                           (filename != CHARPN ? filename : linemsg),
                           (filename != CHARPN ? ":" : ""),
                               (long)line, (int)(e - s), s);
              ret = 1;                          /* partial failure */
              continue;
            }
          for (;
               s2 < eol;
               s2 = e2 + strspn(e2,HORZSPACE), e2 = s2+strcspn(s2,Whitespace))
            {
              if (e2 > eol) e2 = eol;           /* stop at comment/EOL */
              if (!TXincarray(pmbuf, (void **)(void *)&cfg->aliases,
                              cfg->numAliases, &numaliasesalloc,
                              sizeof(HTCSA)))
                goto maerrsilent;
              /* See if we've seen this alias as a charset: */
              l = 0;
              r = cfg->numCharsets;
              while (l < r)                     /* binary search */
                {
                  i = (l + r)/2;
                  cs = cfg->charsets + i;
                  cmp = htstrnipunctcmp(s2, e2 - s2,
                                        (cs->tok == HTCHARSET_UNKNOWN ?
                                         cs->buf : CharsetName[cs->tok]), -1);
                  if (cmp < 0) r = i;
                  else if (cmp > 0) l = i + 1;
                  else break;                   /* found it */
                }
              if (l < r)                        /* already used as charset */
                {
                  if (yap)
                    txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                "%s%s%ld: Alias `%.*s' is a duplicate of a Charset; ignoring",
                                   (filename != CHARPN ? filename : linemsg),
                                   (filename != CHARPN ? ":" : ""),
                                   (long)line, (int)(e2 - s2), s2);
                  ret = 1;                      /* partial failure */
                  continue;                     /* next alias */
                }
              /* See if we've seen this alias already: */
              l = 0;
              r = cfg->numAliases;
              while (l < r)                     /* binary search */
                {
                  i = (l + r)/2;
                  cmp = htstrnipunctcmp(s2, e2-s2, cfg->aliases[i].name, -1);
                  if (cmp < 0) r = i;
                  else if (cmp > 0) l = i + 1;
                  else break;                   /* found it */
                }
              if (l < r)                        /* already used */
                {
                  if (yap)
                    txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                                  "%s%s%ld: Duplicate alias `%.*s'; ignoring",
                                   (filename != CHARPN ? filename : linemsg),
                                   (filename != CHARPN ? ":" : ""),
                                   (long)line, (int)(e2 - s2), s2);
                  ret = 1;                      /* partial failure */
                  continue;                     /* next alias */
                }
              if (l < cfg->numAliases)          /* make room */
                memmove(cfg->aliases + l + 1, cfg->aliases + l,
                        (cfg->numAliases - l)*sizeof(HTCSA));
              csa = cfg->aliases + l;
              cfg->numAliases++;
              if ((csa->name = (char *)malloc((e2 - s2) + 1)) == CHARPN)
                goto maerr;
              memcpy(csa->name, s2, e2 - s2);
              csa->name[e2 - s2] = '\0';
              csa->charsetIndex = curCharsetIndex;
            }
        }
      /* note: "if (igncharset) continue;" and curCharsetIndex test
       * for any future labels here
       */
      else
        {
          synmsg = "Unknown label";
        synerr:
          if (yap)
            txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                           "%s%s%ld: Syntax error: %s; ignoring line",
                           (filename != CHARPN ? filename : linemsg),
                           (filename != CHARPN ? ":" : ""),
                           (long)line, synmsg);
          ret = 1;                              /* partial failure */
          continue;
        }
    }
  goto done;

maerr:
  if (yap) txpmbuf_putmsg(pmbuf, MERR + MAE, fn, "Out of memory");
maerrsilent:
  if (hterrnop != HTERRPN) *hterrnop = HTERR_NO_MEM;
  cfg = TXcharsetConfigClose(cfg);
  ret = 0;                                      /* error */
done:
  *cfgp = cfg;
  return(ret);
}

/* ------------------------------------------------------------------------ */

int
TXcharsetConfigOpenFromFile(cfgp, pmbuf, hterrnop, f, yap)
HTCSCFG         **cfgp;         /* (out) returned object */
TXPMBUF         *pmbuf;         /* (out, opt.) for messages */
HTERR           *hterrnop;      /* (out, opt.) for error */
CONST char      *f;             /* (in, opt.) filename (NULL for default) */
int             yap;            /* 0: silent 1: non-ENOENT errs 2: all errs */
/* Reads and parses charset.conf file `f'.  If `f' is NULL, texis.ini
 * is checked for file; if not set, default config file then used.
 * File can be "builtin" to use builtin default settings.
 * Returns 0 on error, 1 on partial failure (parse error), 2 on success.
 * Sets `*cfgp' to object if not error, else NULL.
 */
{
  static CONST char     fn[] = "TXcharsetConfigOpenFromFile";
  EPI_STAT_S            st;
  int                   fd = -1, ret = 2;
  size_t                sz;
  HTCSCFG               *cfg = HTCSCFGPN;
  char                  *buf = CHARPN, *eob, *s;
  TXrawOpenFlag         txFlags;
  char                  tmpfile[PATH_MAX], tmp[16385];

  if (f == CHARPN)                              /* use texis.ini value */
    {
      if (TxConf == CONFFILEPN ||
          (f = getconfstring(TxConf, "Texis", "Charset Config",
                             CHARPN)) == CHARPN)
        {                                       /* use default file */
          TXstrncpy(tmpfile, TXINSTALLPATH_VAL,
                    sizeof(tmpfile) - sizeof(CharsetConfigSuffix));
          strcat(tmpfile, CharsetConfigSuffix);
          f = tmpfile;
        }
    }

  if (strcmpi(f, "builtin") == 0)
    {
      buf = CHARPN;                             /* force internal default */
      goto parseIt;
    }

  /* Open config file and read it all into a buffer: - - - - - - - - - - - */
  txFlags = TXrawOpenFlag_None;
  if (yap == 1) txFlags |= TXrawOpenFlag_SuppressNoSuchFileErr;
  fd = TXrawOpen((yap > 0 ? pmbuf : TXPMBUF_SUPPRESS), __FUNCTION__,
                 "charset config file", f, txFlags,
                 (O_RDONLY | TX_O_BINARY), 0666);
  if (fd == -1) goto err;
  buf = tmp;
  /* don't yap: file may be shorter than our read: */
  sz = (size_t)tx_rawread(pmbuf, fd, f, (byte *)buf, sizeof(tmp) - 1, 0);
  if (sz == sizeof(tmp) - 1)                    /* could be more data */
    {
      if (EPI_FSTAT(fd, &st) != 0)              /* error */
        {
          if (yap >= 2 || (yap >= 1 && errno != ENOENT))
            txpmbuf_putmsg(pmbuf, MERR + FOE, NULL,
                           "Could not open charset config file %s: %s",
                           f, strerror(errno));
          goto err;
        }
      if ((buf = (char *)malloc((size_t) st.st_size + 1)) == CHARPN)
        goto maerr;
      memcpy(buf, tmp, sizeof(tmp) - 1);
      sz += (size_t)tx_rawread(pmbuf, fd, f, (byte *)buf + sizeof(tmp) - 1,
                               (size_t) st.st_size - (sizeof(tmp) - 1),
                               (yap >= 1));
    }
  close(fd);
  fd = -1;
  eob = buf + sz;
  *eob = '\0';                                  /* nul-terminate for parse */

  /* Change any embedded nuls to spaces: */
  s = buf;
  for (;;)
    {
      s += strlen(s);
      if (s >= eob) break;
      *(s++) = ' ';
    }

parseIt:
  ret = TXcharsetConfigOpenFromText(&cfg, pmbuf, hterrnop, buf, yap, f);
  goto done;

maerr:
  if (yap) txpmbuf_putmsg(pmbuf, MERR + MAE, fn, "Out of memory");
  if (hterrnop != HTERRPN) *hterrnop = HTERR_NO_MEM;
err:
  cfg = TXcharsetConfigClose(cfg);
  ret = 0;
done:
  if (fd != -1) close(fd);
  if (buf != CHARPN && buf != tmp) free(buf);
  *cfgp = cfg;
  return(ret);
}

/* ------------------------------------------------------------------------ */

HTCSCFG *
TXcharsetConfigClose(cfg)
HTCSCFG *cfg;   /* (in/out) object to close */
{
  size_t        i;

  if (cfg != HTCSCFGPN &&
      cfg != &TxCharsetConfigDefault &&
      --cfg->refCount <= 0)
    {
      if (cfg->aliases != HTCSAPN)
        {
          for (i = 0; i < cfg->numAliases; i++)
            if (cfg->aliases[i].name != CHARPN) free(cfg->aliases[i].name);
          free(cfg->aliases);
        }
      if (cfg->charsets != HTCSINFOPN)
        {
          for (i = 0; i < cfg->numCharsets; i++)
            if (cfg->charsets[i].buf != CHARPN) free(cfg->charsets[i].buf);
          free(cfg->charsets);
        }
      free(cfg);
    }
  return(HTCSCFGPN);
}

/* ------------------------------------------------------------------------ */

HTCSCFG *
TXcharsetConfigClone(cfg)
HTCSCFG *cfg;   /* (in, opt.) object to clone */
/* Clones `cfg' and returns a "new" object (actually the same).
 */
{
  if (cfg == HTCSCFGPN || cfg == &TxCharsetConfigDefault) return(cfg);
  cfg->refCount++;                              /* one more user */
  return(cfg);
}

/* ------------------------------------------------------------------------ */

char *
TXcharsetConfigToText(pmbuf, cfg)
TXPMBUF *pmbuf; /* (in, opt.) for messages */
HTCSCFG *cfg;   /* (in) config */
/* Returns an alloc'd text buffer equivalent to `cfg' if parsed via
 * TXcharsetConfigFromText(), or NULL on error (e.g. no mem).
 */
{
  HTBUF         *buf = HTBUFPN;
  char          *ret;
  size_t        csIndex;
  HTCSINFO      *csInfo;
  HTCSA         *csa, *csaEnd;

  if ((buf = openhtbuf()) == HTBUFPN) goto err;
  for (csIndex = 0; csIndex < cfg->numCharsets; csIndex++)
    {
      csInfo = cfg->charsets + csIndex;
      if (!htbuf_pf(buf, "Charset: %s\n", (csInfo->tok == HTCHARSET_UNKNOWN ?
                               csInfo->buf : htcharset2str(csInfo->tok))))
        goto err;
      if (!htbuf_pf(buf, "Aliases:")) goto err;
      for (csa = cfg->aliases, csaEnd = csa + cfg->numAliases;
           csa < csaEnd;
           csa++)
        if (csa->charsetIndex == csIndex &&
            !htbuf_pf(buf, " %s", csa->name))
          goto err;
      if (!htbuf_write(buf, "\n", 1)) goto err;
    }
  goto done;

err:
  buf = closehtbuf(buf);
done:
  ret = CHARPN;
  if (buf != HTBUFPN) htbuf_getdata(buf, &ret, 0x3);
  return(ret);
}
