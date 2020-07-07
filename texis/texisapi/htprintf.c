#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#if defined(TEST) || defined(STANDALONE)
#  include <signal.h>
#  ifdef _WIN32
#    include <io.h>
#  endif /* _WIN32 */
#  include <fcntl.h>
#endif
#ifdef EPI_HAVE_LOCALE_H
#  include <locale.h>
#endif
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#ifdef EPI_HAVE_STDARG
#  include <stdarg.h>
#else
#  include <varargs.h>
#endif
#include "sizes.h"
#include "os.h"
#include "dirio.h"                              /* for PATH_DIV */
#include "http.h"
#include "httpi.h"
#include "cgi.h"
#include "texint.h"     /* for TXstrftime() etc. */


#undef EPI_HAVE_LOCALE_H    /* breaks things for now KNG 980719 */
#undef EPI_HAVE_SETLOCALE       /* "" */

/* Format codes and flags used:
 *
    %       /          :    B  EFGH     QR TUVWX    bcdefg i    nop rstuv x z
! #  & *+ -. 0123456789 <=>  C     I K P         ^_abc e  h jklmn pq     w   |
 ^--space
 *
 *  This version implements the following printf features:
 *
 *	%d,%i	decimal
 *	%u	unsigned decimal
 *	%x	hexadecimal
 *	%X	hexadecimal with capital letters
 *	%o	octal
 *	%c	character
 *	%s	string
 *	%e,%E	exponential, exponential with capital 'E'
 *	%f	floating-point
 *	%g,%G	generalized floating-point, ditto with capital 'E'
 *	%p	pointer (but also as nonstandard flag below)
 *	%n	intermediate return of # of characters printed
 *      %%      %-sign
 *
 *  Flags:
 *	m.n	field width, precision
 *	-	left adjustment
 *              (for %L) argument is a latitude
 *	+	explicit '+' for positive numbers
 *              (for %L) argument is a location (lat+lon geocode)
 *              (for %s/%U/%V/%v/%Q/%B/%W/%H) if string is longer than
                  precision, print 3 less bytes and append `...'
 *	space	extra leading space for positive numbers
 *	0	zero-padding
 *	l	argument is a long int
 *	h	(ignored) argument is short
 *	*.*	width and precision taken from arguments
 *	#	"alternate format"
 *
 *  This version implements the following nonstandard features:
 *
 *	%b	binary conversion
 *	%r	roman numeral conversion
 *	%R	roman numeral conversion with capital letters
 *      %U      URL string encoding/decoding  PBR!!!!!!!!!!!!!
 *      %V      UTF-8 encoding/decoding from/to ISO-8859-1   KNG 000926
 *      %v      UTF-16 encoding/decoding from/to UTF-8   KNG 030406
 *      %Q      Quoted-printable encoding/decoding       KNG 030418
 *      %B      base64 encoding/decoding      KNG 010105
 *              (field width is taken as chars per line; neg. is no newline)
 *      %H      HTML encoding/decoding        PBR!!!!!!!!!!!!!
 *      %t      Print time_t value using strftime() format string   KNG 960123
 *      %T      like %t but in UTC (Universal Time)
 *      %F      printf double as fraction    KNG 971015
 *      %/      platform-specific directory separator (see l/ll/! flags too)
 *      %:      platform-specific search path separator (see l/ll/! flags too)
 *      %W      UTF-8 to RFC 2047 encoded-word format
 *      %L      Latitude/longitude (double) or location geocode (long)
 *      %z      gzip compress/decompress (supports .N precision, `!')
 *
 *  Nonstandard flags:
 *
 *    Flags for %s/%U/%H/%V/%v/%B/%W:
 *      m       Metamorph query and markup (except for %B/%U);    KNG 960117
 *                query is arg passed before str.
 *      p       (%U): Path, not query string, encoding: space to `%20' not '+'
 *                    (`%20' is default in v8+), and (v8+) leave `+' as-is
 *              (%!U): "", i.e. leave `+' as-is
 *              (all others):  Same as after m
 *      P       Same as after m
 *      q       (%U):   v8+: query-string encoding (space to `+' not `%20')
 *                      v7-: Full encoding: %-encode '/' '@' space
 *              (%!U):  v8+: query-string decoding (decode `+', as does %U)
 *                      v7-: Only decode safe chars, e.g. alnum -_.!~*'()
 *              (%W):   Only use Q encoding (not base64)
 *      l       (%H):   only low (7-bit) chars, e.g. <>"&         KNG 000914
 *                      Version 5+: ignored (always on)           KNG 040410
 *              (%!H):  to low (7-bit multichar) sequence         KNG 000914
 *              (%/, %:): print bracketed REX char class to match any sep
 *              (%z):   zlib-deflate instead of gzip (encode) or any (decode)
 *      ll      (%H):   Version 5+: pre-v5-%H: escape hi-bit too  KNG 040410
 *              (%/, %:): print <sandr> replace expr for canonical sep instead
 *              (%e/%f/%g): argument is a long double (if supported, else dbl)
 *              (%z):   raw-deflate instead of gzip (encode) or any (decode)
 *      !       (for %U/%H/%V/%v/%B/%Q/%W/%z) Decode instead of encode
 *              (for %c) Print ASCII code in decimal of character KNG 001108
 *              (for %l/, %l:) Negate the REX char class
 *      h       (for %V, %!v, %!W) unescape HTML sequences first  KNG 001114
 *              (for %!V, %v) HTML-esc out-of-range chars instead of ? 000926
 *      hh      (for %V %!V %=V %v %!v %W %!W) "" + HTML-esc < > " & chars
 *      hhh     (for "") do not unescape HTML first; HTML-esc < > " & chars
 *      j (except %U) Translate newlines to native form (LF Unix/CRLF Windows)
 *      Subflags for j:
 *        c     Translate newlines to CR
 *        l     Translate newlines to LF (or CRLF if c also)
 *      ^       (for %V %=V %!V %v %!v %s %W %!W) Output XML-safe chars
 *      =       (for %V) Input is same encoding (e.g. UTF-8): verify it,
 *                       and (if h or hh) do not unescape HTML sequences
 *      |       (for %!V %=V %v %W %!W) Interpret bad UTF-8 sequences as ISO
 *              (for %L) Argument is a longitude
 *
 *      Flags after m:
 *         b    HTML bold highlighting of hits
 *         B    VT100 bold highlighting of hits
 *         U    VT100 underline highlighting of hits
 *         R    VT100 reverse video highlighting of hits
 *         h    HREF highlighting (default if neither b nor h given)
 *         n    No backing off highlighting tags inside <>
 *         p    Paragraph formatting:  insert <p/> between paragraphs
 *         P    Paragraph formatting:  insert <p/> at given REX expression
 *         PP   Paragraph formatting:  insert 2nd arg at 1st arg REX expr
 *         c    Continue set hit count into next %s/H/U
 *         N    Show NOT hits too
 *         e    Exact query: don't do @0 w/. or remove LOGIAND
 *         I    Use inline styles (<span style="...">) and mark query/sets
 *         C    Use classes (<span class="query querysetN">)  ""
 *         q    Hilite the query itself; ignore text arg
 *
 *    Flags for %t/T:
 *      a       Format string for time_t is next arg; default format
 *                is like ctime() (without newline)
 *
 *    Flags for %d/%i/%f/%g:
 *      k       Comma-separate every 3 places to left of decimal
 *      K       Put next char arg every 3 places to left of decimal
 *      ll      Long long int or long double arg (if supported, else long/dbl)
 *      w       Whopping (EPI_HUGEINT/EPI_HUGEUINT/EPI_HUGEFLOAT) arg
 *      I32|I64 (Windows only) 32|64-bit ints
 *
 *    Flags for %s/%U/%H/%d/%i/%e/%f/%g/%x:
 *      _       ASCII 160 instead of space for padding KNG 990708
 *      &       &nbsp; instead of space for padding    KNG 990707
 *
 *    Flags for %B:
 *      width   Output chars/line (w/newlines); 0 for no newlines
 *
 *    Flags for %v/%!v:
 *      <       Force UTF-16LE (little-endian) input/output
 *      >       Force UTF-16BE (big-endian) input/output (default)
 *      _       (for %v) skip (do not print) a BOM at start of output
 *              (for %!v) save (do not skip) a BOM at start of input
 *      see also above
 *
 *    Flags for %Q:
 *      _       (%Q, %!Q, %W): Q encoding (' ' to '_'; whitespace, $ etc.
 *                encoded)
 *      width   Output chars/line (w/soft newlines); 0 for no newlines
 *      Negative: (%Q) source is "binary" (encode CR, LF too)
 *      see also above
 *
 *  If an attempt is made to print a null pointer as a string (0
 *  is passed to %s), "(null)" is printed.  (I think the old
 *  pdp11 doprnt did this.)
 *
 *  The %g code is imperfect, as it relies almost entirely upon
 *  gcvt().  In particular, neither %#g, %+g, nor % g work correctly.
 *
 *  Steve Summit 7/16/88
 */

/* ---------------------------- Config stuff ------------------------------- */
/* see also makefile */

/* stdout seems to change from libc 2.1.1 to libc 2.1.3 such that compiling
 * under the former blows up when run linked with the latter.  This seems
 * to fix this:  KNG 010316
 */
#if defined(EPI_HAVE_STDOUT_SYM) && defined(__linux__)
#  undef stdin
#  undef stdout
#  undef stderr
#endif

/* Define if strftime() may not return correct length printed (especially if
 * too large for given buffer):  e.g. Linux    KNG 970112
 */
#define BROKEN_STRFTIME_RET

/* Define HTPF_GCVT to use htpf_gcvt() instead of gcvt().  Originally
 * for Linux with GNU libc 2.0, where gcvt() calls sprintf(%g) which would
 * recurse indefinitely (if !HTPF_STRAIGHT).  KNG 990615
 * KNG 20061003 flipping functions and counting recursion depth
 * is not thread-safe (count gets mangled under Windows threads; Bug 1557),
 * so always use one function or the other
 * KNG 020511 OS/X (__MACH__) has no gcvt(); our gcvt() below seems to use
 * %f style always, so use htpf_gcvt().
 * KNG 20120914 always use htpf_gcvt() for consistent behavior, e.g.
 * Windows gcvt() uses %e sometimes even if !(exp < -4 || exp >= precision).
 */
#if !defined(NO_HTPF_USE_ALT_GCVT)
#  define USING_ALT_GCVT
#  define HTPF_GCVT   htpf_gcvt
#else
#  undef USING_ALT_GCVT
#  define HTPF_GCVT   gcvt
#endif

/* Start/end string tags for hits.  Printed with printf, with current/next
 * hit #'s passed.  These can NOT have any non-standard (i.e. Metamorph)
 * flags:
 */
static CONST char       HtBoldStart[] = "<b>";
static CONST char       HtBoldEnd[] = "</b>";
/* For HTML.  Note `>' added below: */
static CONST char       HtHrefStart[] = "<a name=\"hit%d\" href=\"#hit%d\"";
static CONST char       HtHrefEnd[] = "</a>";
/* for VT100: */
static CONST char       Vt100BoldStart[] = "\x1B[1m";
static CONST char       Vt100BoldEnd[] = "\x1B[m\x0F";
static CONST char       Vt100UnderlineStart[] = "\x1B[4m";
/* VT100 underline end is same as bold end */
static CONST char       Vt100ReverseStart[] = "\x1B[7m";
/* VT100 reverse video end is same as bold end */
/* Inline style end is </span> or </a>, depending: */
/* we use <span> instead of shorter-tag <b> because latter is deprecated
 * in modern HTML:
 */
static CONST char       SpanStart[] = "<span";
static CONST char       SpanEnd[] = "</span>";

/* Note that this could potentially occur inside a hit-wide <span>,
 * which is not legal XHTML 1.0 Strict (block element <p/> cannot occur
 * inside inline element <span>).  User can <sandr> the <p/> to <br/><br/>,
 * or we can add an <fmtcp> to change our <span>s to <div inline>:
 */
static CONST char       DefParaReplace[] = TX_OS_EOL_STR "<p/>" TX_OS_EOL_STR TX_OS_EOL_STR;

/* default %t format string for strftime(): */
static CONST char       DefTimeFmt[] = "%a %b %d %H:%M:%S %Z %Y";
#define DEFTIMEFMT_LEN          17
/* same, for %T (Universal Time): */
static CONST char       DefGmTimeFmt[] = "%a %b %d %H:%M:%S GMT %Y";
#define DEFGMTIMEFMT_LEN        18
/* default REX expression to match paragraph breaks for MF_PARA: */
static CONST char       DefParaRex[] = "\\x0D?>>\\x0A=\\space+";

/* default %[-|]L format string.
 * Hemisphere last, even though it is the "largest" unit and would
 * preserve sort order if printed first: default format is more for
 * human readability than sorting, and humans expect hemisphere last (?).
 * Also would cause internal spaces if printed first (less easy to
 * pull out a lat/lon column from a space-separated set of columns):
 */
static CONST char       TXdefCoordinateFormat[] = "%D%O%M'%S\"%H";

/* un#define these to turn off nonstandard features */
#define BINARY
#define ROMAN

static CONST char       NullStr[] = "(null)"; /* print null pointers thusly */
#define NULLSTR_LEN     6
#define PRNULLPTR	/* define to substitute NullStr for NULL */

/* Consistent string for NaN/Inf, so we can check for NaN by printing/casting
 * to string and comparing to this:
 */
static CONST char       NaNStr[] = "NaN";
#define NANSTR_LEN      (sizeof(NaNStr) - 1)
static CONST char       InfStr[] = "Inf";
#define INFSTR_LEN      (sizeof(InfStr) - 1)

/*
 *  Un#define FLOATING if you don't need it, want to save space,
 *  or don't have ecvt(), fcvt(), and gcvt().
 */

#define FLOATING

/*
 *  A few other miscellaneous #definitions are possible:
 *
 *	NOLONG		turn off code for explicit handling of %l,
 *			which works just fine but is a bit
 *			cumbersome and space-consuming.
 *			Don't #define NOLONG.
 *			(Unless you don't use %l at all, NOLONG only
 *			works on machines with sizeof(long)==sizeof(int),
 *			and those tend to be virtual memory machines
 *			for which the extra code size won't be a problem.)
 */

/* KNG 20070206 fcvt() has problems on some platforms (see CVS log),
 * and is thread-unsafe and deprecated.  Use snprintf() where available
 * (and it does not potentially infinitely recurse, e.g. HTPF_STRAIGHT):
 */
#undef USE_SNPRINTF_NOT_CVT
#if defined(EPI_HAVE_SNPRINTF) && defined(HTPF_STRAIGHT)
#  define USE_SNPRINTF_NOT_CVT 1
#elif !defined(EPI_HAVE_ECVT) || !defined(EPI_HAVE_FCVT) || !defined(EPI_HAVE_GCVT)
error; missing both snprintf and [efg]cvt; see old CVS rev for implementation;
#endif

/* ----------------------------- End config -------------------------------- */


typedef enum PRF_tag
{
  PRF_LONG      = (1 << 0),
  PRF_LONGLONG  = (1 << 1),
  PRF_HUGE      = (1 << 2),
  PRF_NUMSGN    = (1 << 3),
  PRF_LADJUST   = (1 << 4),
  PRF_SPACEPAD  = (1 << 5),             /* " " or "&nbsp;" padding */
  PRF_CAPITAL   = (1 << 6),
  PRF_PATHURL   = (1 << 7),
  PRF_QUERYURL  = (1 << 8),
  PRF_DECODE    = (1 << 9),
  PRF_VERIFY    = (1 <<10)                      /* `=' flag: verify encoding*/
}
PRF;

#define TRUE 1
#define FALSE 0

#define Ctod(c) ((c) - '0')
#ifndef FFSPN
#  define FFSPN ((FFS *)NULL)
#endif

#ifdef ROMAN
static void tack ARGS((int d, char *digs, char **p));
static void doit ARGS((int d, int one, char **p));
#endif

#ifdef FLOATING
#  ifndef EPI_HAVE_ECVT_PROTOTYPE
extern char *ecvt ARGS((double number, int ndigits, int *decpt, int *sign));
#  endif /* !EPI_HAVE_ECVT_PROTOTYPE */
#  ifndef EPI_HAVE_FCVT_PROTOTYPE
extern char *fcvt ARGS((double number, int ndigits, int *decpt, int *sign));
#  endif /* !EPI_HAVE_FCVT_PROTOTYPE */
#  ifndef EPI_HAVE_GCVT_PROTOTYPE
extern char *gcvt ARGS((double number, int ndigit, char *buf));
#  endif /* !EPI_HAVE_GCVT_PROTOTYPE */
#  ifdef _WIN32
/* snprintf() is actually _snprintf() under Windows: */
#    define snprintf _snprintf
#  endif /* _WIN32 */
#  if defined(EPI_HAVE_SNPRINTF) && !defined(EPI_HAVE_SNPRINTF_PROTOTYPE)
/* wtf implementation may differ, but then they should've given a prototype:*/
extern int snprintf ARGS((char *s, size_t sz, CONST char *fmt, ...));
#  endif /* EPI_HAVE_SNPRINTF && !EPI_HAVE_SNPRINTF_PROTOTYPE */
#endif /* FLOATING */

#if defined(USING_ALT_GCVT) && !defined(USE_SNPRINTF_NOT_CVT)
static char *htpf_gcvt ARGS((double num, int ndig, char *buf));
#endif /* USING_ALT_GCVT && !USE_SNPRINTF_NOT_CVT */

static int      HtpfTraceEncoding = 0;

/* ------------------------------------------------------------------------- */

typedef enum MF_tag
{
  MF_INUSE              = (1 <<  0),            /* do Metamorph query */
  MF_BOLDHTML           = (1 <<  1),            /* HTML-bold highlight hits */
  MF_HREF               = (1 <<  2),            /* HREF highlight hits */
  MF_BOLDVT100          = (1 <<  3),            /* VT100-bold highlite hits */
  MF_UNDERLINEVT100     = (1 <<  4),            /* VT100-underline hits */
  MF_INLINESTYLE        = (1 <<  5),            /* inline style highlight */
  MF_CLASS              = (1 <<  6),            /* class highlight */
  MF_NOTAGSKIP          = (1 <<  7),            /* don't chop tag-overlaps */
  MF_PARA               = (1 <<  8),            /* map "\n\n" -> "<p/>\n\n" */
  MF_CONTCOUNT          = (1 <<  9),            /* cont. set hit cnt next q */
  MF_HILITEQUERY        = (1 << 10),            /* markup query not text */
  /* internal use by mm_...() functions: */
  MF_CONTSEARCH         = (1 << 20),            /* set for CONTINUESEARCH op*/
  MF_EXACT              = (1 << 21),            /* don't do @0 w/. -LOGIAND */
  MF_NOT                = (1 << 22),            /* show NOTs too */
  MF_REVERSEVT100       = (1 << 23),            /* VT100-reverse highlight */
}
MF;
#define MFPN    ((MF *)NULL)

/* ------------------------------------------------------------------------- */

/* Set-hit info: */
typedef struct {
  char	*start, *end;
  int   setOrpos;                               /* original-order set # */
  TXPMTYPE      pmtype;                         /* pattern matcher type */
  TXLOGITYPE    logic;                          /* LOGI... logic */
} MMSH;
#define MMSHPN	((MMSH *)NULL)

/* Metamorph search state: */
typedef struct MM_tag {
  CONST TXFMTCP *fmtcp;                         /* settings */
  TXFMTSTATE    *fs;                            /* parent object */
  APICP		*cp;
  MMAPI		*mm;
  char		*buf, *end, *lastend;
  MMSH          *setHits;
  int		shsz, numsh, cursh;
  char          *hitStart, *hitEnd;             /* overall hit start/end */
  MF            flags;
  byte          queryFixupMode;
  char		*query;
  struct MM_tag	*next;
} MM;
#define MMPN		((MM *)NULL)
#define MM_CACHE_MAX	4	/* max # of queries to cache simultaneously */


static MM *delete_mm ARGS((TXFMTSTATE *fs, MM *mm));
static MM *
delete_mm(fs, mm)
TXFMTSTATE      *fs;    /* (in/out) owner of list that contains `mm' */
MM	        *mm;    /* (in) item to remove and free */
{
  MM	*prev, *cur;

  if (mm == MMPN) return(MMPN);

  if (mm->mm != MMAPIPN) closemmapi(mm->mm);
  if (mm->cp != APICPPN) closeapicp(mm->cp);
  mm->setHits = TXfree(mm->setHits);
  mm->query = TXfree(mm->query);
  /* unlink it: */
  for (prev = MMPN, cur = fs->mmList;
       cur != MMPN;
       prev = cur, cur = cur->next) {
    if (cur == mm) {
      if (prev != MMPN) prev->next = cur->next;
      else fs->mmList = cur->next;
      fs->mmNum--;
      break;
    }
  }
  mm = TXfree(mm);
  return(MMPN);
}

static MM *close_mm ARGS((MM *mm));
static MM *
close_mm(mm)
MM	*mm;
{
  if (mm == MMPN) return(MMPN);
  /* Leave query alone; may be reused by another printf() call: */
  mm->flags &= ~MF_INUSE;
  return(MMPN);
}

static MM *open_mm ARGS((CONST TXFMTCP *fmtcp, TXFMTSTATE *fs, char *q,
                         char *buf, char *end, MF flags));
static MM *
open_mm(fmtcp, fs, q, buf, end, flags)
CONST TXFMTCP   *fmtcp; /* (in) settings */
TXFMTSTATE      *fs;    /* (in) parent to attach to */
char	        *q, *buf, *end;
MF              flags;
/* Opens a Metamorph query for hit markup.
 */
{
  static CONST char     fn[] = "open_mm";
  MM	                *mm;
  MM3S                  *me;
  int                   i;
  TXPMBUF               *pmbuf = TXPMBUFPN;

  if (q == CHARPN || *q == '\0')        /* KNG 960927 */
    return(MMPN);
  /* See if we've seen this query before, and reuse it: */
  for (mm = fs->mmList; mm != MMPN; mm = mm->next) {
    if (!(mm->flags & MF_INUSE) &&
	strcmp(mm->query, q) == 0 &&
        (mm->flags & (MF_EXACT | MF_NOT)) == (flags & (MF_EXACT | MF_NOT)) &&
        mm->queryFixupMode == fmtcp->queryFixupMode)
      goto setmm;
  }
  /* Create a new query.  Close an old one first if the cache is large: */
  if (fs->mmNum >= MM_CACHE_MAX) {
    for (mm = fs->mmList; mm != MMPN; mm = mm->next) {
      if (!(mm->flags & MF_INUSE)) {
	delete_mm(fs, mm);
	break;
      }
    }
  }
  if ((mm = (MM *)TXcalloc(pmbuf, fn, 1, sizeof(MM))) == MMPN)
    {
    err:
      return(delete_mm(fs, mm));
    }
  mm->fmtcp = fmtcp;
  /* Save fixup mode; `fmtcp->queryFixupMode' may change via <fmtcp>: */
  mm->queryFixupMode = fmtcp->queryFixupMode;
  mm->fs = fs;
  /* be really %$^?! sure and set pointers to NULL:   -KNG 960911 */
  mm->cp = APICPPN;
  mm->mm = MMAPIPN;
  mm->setHits = MMSHPN;
  mm->hitStart = mm->hitEnd = CHARPN;
  mm->next = MMPN;
  if ((mm->query = TXstrdup(pmbuf, __FUNCTION__, q)) == CHARPN) goto err;
  mm->cp = dupapicp(fmtcp->apicp != APICPPN ? fmtcp->apicp : &TxApicpDefault);
  if (mm->cp == APICPPN) goto err;
  mm->mm = openmmapi(mm->query, TXbool_False /* !isRankedQuery */, mm->cp);
  if (mm->mm == MMAPIPN) goto err;
  if (!(flags & MF_EXACT) &&
      mm->queryFixupMode == TXFMTCP_QUERYFIXUPMODE_WITHINDOT)
    {
      APICP     *mmApicp;
      MM3S      *mm3s;

      /* Allow intersects and within.  Used to be needed pre-Bug 6703
       * when we tacked on `w/. @0'; probably not needed now, but be safe:
       */
      mm->cp->alintersects = mm->cp->alwithin = 1;
      /* Bug 6703: We used to modify query to be `w/. query w/. @0'
       * before openmmapi(): leading `w/.' ensures *our* start
       * delimiter is set (not query's), and trailing `w/.'/`@0'
       * ensures our end delimiter and intersects are set.  But if
       * query is malformed (e.g. open double quotes), our trailing
       * `w/.'/`@0' gets taken as part of that open-quotes phrase,
       * which then can fail to match.  So modify MMAPI/APICP
       * post-openmmapi() instead; NOTE see getmmdelims(), setmmapi(),
       * open3eapi():
       */
      mmApicp = mm->mm->acp;
      mm3s = mm->mm->mme;

      /* Change start delimiter to `.': */
      mm3s->sdx = closerex(mm3s->sdx);
      mmApicp->sdexp = mm3s->sdexp = TXfree(mmApicp->sdexp);
      mmApicp->sdexp = mm3s->sdexp =
        (byte *)TXstrdup(pmbuf, __FUNCTION__, ".");
      if (!mmApicp->sdexp) goto err;
      if (!(mm3s->sdx = openrex(mm3s->sdexp, TXrexSyntax_Rex)))
        goto err;

      /* Do not include start delimiter: */
      mmApicp->incsd = mm3s->incsd = 0;

      /* Change end delimiter to `.': */
      mm3s->edx = closerex(mm3s->edx);
      mmApicp->edexp = mm3s->edexp = TXfree(mmApicp->edexp);
      mmApicp->edexp = mm3s->edexp =
        (byte *)TXstrdup(pmbuf, __FUNCTION__, ".");
      if (!mmApicp->edexp) goto err;
      if (!(mm3s->edx = openrex(mm3s->edexp, TXrexSyntax_Rex)))
        goto err;

      /* Do not include end delimiter: */
      mmApicp->inced = mm3s->inced = 0;

      /* Misc delims derived from above: */
      mm3s->delimseq = (strcmp((char *)mm3s->sdexp, (char *)mm3s->edexp) == 0);
      mmApicp->withincount = mm3s->withincount = 0;

      /* Set 0 intersects: */
      mm->mm->qintersects = mm->mm->intersects = mm3s->intersects = 0;
    }
  if (!(flags & MF_EXACT))              /* KNG 980605 fix up query */
    {
      me = mm->mm->mme;                 /* WTF struct drill-o-rama */
      for (i = 0; i < me->nels; i++)
        if (me->el[i]->logic == LOGINOT ? (flags & MF_NOT) :
            (mm->queryFixupMode == TXFMTCP_QUERYFIXUPMODE_WITHINDOT))
          me->el[i]->logic = LOGISET;
      if (mm->queryFixupMode == TXFMTCP_QUERYFIXUPMODE_WITHINDOT)
        {
          me->intersects = mm->mm->intersects = 0;
          me->nsets += me->nands;
          me->nands = 0;
        }
      if (flags & MF_NOT)
        {
          me->nsets += me->nnots;
          me->nnots = 0;
        }
    }
  mm->shsz = 0;
  /* Attach to `fs': */
  mm->next = fs->mmList;
  fs->mmList = mm;
  fs->mmNum++;
 setmm:
  mm->cursh = mm->numsh = 0;
  mm->buf = mm->lastend = buf;
  mm->end = end;
  mm->flags = flags | MF_INUSE;
  return(mm);
}

static int CDECL mm_sortcb ARGS((CONST void *a, CONST void *b));
static int CDECL
mm_sortcb(a, b)
CONST void *a, *b;
{
  MMSH	*c, *d;

  c = (MMSH *)a;
  d = (MMSH *)b;
  if (c->start < d->start)
    return(-1);
  else if (c->start == d->start)
    return((int)(c->end - d->end));
  else
    return(1);
}

static int mm_infommapi ARGS((MMAPI *mp, int idx, char **srchs, char **poff,
                              int *plen, int *setOrpos, TXPMTYPE *pmtype,
                              TXLOGITYPE *logic));
static int
mm_infommapi(mp, idx, srchs, poff, plen, setOrpos, pmtype, logic)
MMAPI   *mp;
int     idx;
char    **srchs;
char    **poff;
int     *plen;
int     *setOrpos;
TXPMTYPE        *pmtype;
TXLOGITYPE      *logic;
/* Like infommapi() for index >= 3, but does not return NOT hits.
 * Returns 0 on error.
 */
{
  MM3S  *ms = mp->mme;
  int   i;

  if (idx < 3) return(0);
  idx -= 3;
  if (idx < ms->nels)
    {
      for (i = 0; i < ms->nels; i++)
        if (ms->el[i]->member && ms->el[i]->logic != LOGINOT && --idx < 0)
          break;
      if (i == ms->nels) return(0);             /* no more sets */
      *srchs = (char *)ms->el[i]->srchs;
      *poff = (char *)ms->el[i]->hit;
      *plen = ms->el[i]->hitsz;
      *setOrpos = ms->el[i]->orpos;
      *pmtype = ms->el[i]->pmtype;
      *logic = ms->el[i]->logic;
      return(1);                                /* success */
    }
  else
    return(0);
}

static int mm_incSetHits ARGS((MM *mm));
static int
mm_incSetHits(mm)
MM      *mm;
/* Increases `mm->setHits' allocation if needed, so at least 1 more fits.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "mm_incSetHits";
  MMSH                  *newBuf;
  TXPMBUF               *pmbuf = TXPMBUFPN;

  if ( mm->numsh < mm->shsz) return(1);         /* already big enough */

  mm->shsz += (mm->shsz >> 2) + 8;              /* arbitrary increase */
  newBuf = (MMSH *)(mm->setHits == MMSHPN ?
                    TXmalloc(pmbuf, fn, mm->shsz*sizeof(MMSH)) :
                    TXrealloc(pmbuf, fn, mm->setHits, mm->shsz*sizeof(MMSH)));
  if (newBuf == MMSHPN)
    {
#ifdef EPI_REALLOC_FAIL_SAFE
      mm->setHits = TXfree(mm->setHits);
#endif /* EPI_REALLOC_FAIL_SAFE */
      mm->numsh = mm->shsz = 0;
      return(0);
    }
  mm->setHits = newBuf;
  return(1);
}

static int mm_next ARGS((MM *mm, char **setHitStart, char **setHitEnd,
                         int *setOrpos, TXPMTYPE *pmtype, TXLOGITYPE *logic,
                         char **hitStart, char **hitEnd));
static int
mm_next(mm, setHitStart, setHitEnd, setOrpos, pmtype, logic, hitStart, hitEnd)
MM	*mm;
char	**setHitStart, **setHitEnd;
int     *setOrpos;      /* (out) original-order set index # (from 0) */
TXPMTYPE *pmtype;       /* (out, opt.) set pattern matcher type */
TXLOGITYPE *logic;      /* (out, opt.) set logic */
char    **hitStart;     /* (out) overall hit start (i.e. w/delims) */
char    **hitEnd;       /* (out) overall hit end (i.e. w/delims) */
/* Gets next set hit, searching for next hit if needed; returns
 * current hit number and current set hit in `*setHitStart'/`*setHitEnd'.  If
 * MF_NOTAGSKIP was set, hits that overlap <> tags are chopped/moved.
 * Set hits are returned in the order they occur in the buffer, and
 * will not overlap.  After the last hit, `*setHitStart'/`*setHitEnd'
 * are set to* NULL; if MF_CONTCOUNT was set then n+1 is returned as usual
 * (i.e. for continuity), else 1.
 * `*hitStart'/`*hitEnd' may be set to NULL if no overall-hit highlighting
 * should be done (e.g. w/doc).
 */
{
  static CONST char     fn[] = "mm_next";
  int	                sz, i, setIdx;
  char	                *hit, *dum, *s, *e, *p, *ss, *se, *lastSetEnd;
  MMSH	                *sh;
  APICP                 *apicp;
  MM3S                  *ms;
  SEL                   *sel;

  if (mm == MMPN) {
    *setHitStart = *setHitEnd = *hitStart = *hitEnd = CHARPN;
    *setOrpos = 0;
    if (pmtype) *pmtype = (TXPMTYPE)(-1);
    if (logic) *logic = (TXLOGITYPE)(-1);
    return(0);
  }
  if (mm->cursh >= mm->numsh) {		/* look for next overall hit */
    apicp = mm->mm->acp;
    lastSetEnd = (mm->numsh == 0 ? mm->buf : mm->setHits[mm->numsh - 1].end);
    mm->cursh = mm->numsh = 0;
    if (mm->flags & MF_HILITEQUERY)             /* highlight query itself */
      {
        if (mm->hitStart != CHARPN)             /* already highlighted it */
          goto endOfHits;
        mm->hitStart = mm->buf;
        mm->hitEnd = mm->end;
      }
    else                                        /* regular search */
      {
        hit = getmmapi(mm->mm, (byte *)mm->buf, (byte *)mm->end,
                 (mm->flags & MF_CONTSEARCH ? CONTINUESEARCH : SEARCHNEWBUF));
        mm->flags |= MF_CONTSEARCH;
        if (hit == CHARPN) {
        endOfHits:
        err:
          mm->cursh = -1;			/* i.e. end of all hits */
        nohit:
          *setHitStart = *setHitEnd = *hitStart = *hitEnd = CHARPN;
          *setOrpos = 0;
          if (pmtype) *pmtype = (TXPMTYPE)(-1);
          if (logic) *logic = (TXLOGITYPE)(-1);
          if (mm->flags & MF_CONTCOUNT) {
            /* don't increment, so next %s will and will get correct count: */
            return(mm->fs->lastHitNum + 1);
          } else {
            mm->fs->lastHitNum = 0;
            return(1);
          }
        }
        mm->hitStart = (char *)mm->mm->mme->hit;
        mm->hitEnd = mm->hitStart + mm->mm->mme->hitsz;
      }

    /* Grab the set hits and sort them: */
    if (mm->flags & MF_HILITEQUERY)             /* highlight query itself */
      {
        for (mm->numsh = 0;
             apicp->set[mm->numsh] != BYTEPN && *apicp->set[mm->numsh] !='\0';
             mm->numsh++)
          {
            if (mm->numsh >= mm->shsz && !mm_incSetHits(mm)) goto err;
            sh = &mm->setHits[mm->numsh];
            sh->start = mm->buf + apicp->setqoffs[mm->numsh];
            sh->end = sh->start + apicp->setqlens[mm->numsh];
            sh->setOrpos = mm->numsh;
            sh->pmtype = (TXPMTYPE)(-1);
            sh->logic = (TXLOGITYPE)(-1);
            for (i = 0; i < mm->mm->mme->nels; i++)
              {
                sel = mm->mm->mme->el[i];
                if (sel->orpos == mm->numsh)
                  {
                    sh->pmtype = sel->pmtype;
                    sh->logic = sel->logic;
                    break;
                  }
              }
          }
      }
    else                                        /* regular search */
      {
        for (mm->numsh = 0; ; mm->numsh++)
          {
            if (mm->numsh >= mm->shsz && !mm_incSetHits(mm)) goto err;
            sh = &mm->setHits[mm->numsh];
            if (!mm_infommapi(mm->mm, 3 + mm->numsh, &dum, &sh->start, &sz,
                              &sh->setOrpos, &sh->pmtype, &sh->logic))
              break;                            /* end of set hits */
            sh->end = sh->start + sz;
          }
        if (!(mm->flags & MF_EXACT) &&
            mm->queryFixupMode == TXFMTCP_QUERYFIXUPMODE_FINDSETS)
          {
            /* Find additional hits for the sets that were found,
             * and look for sets that were not found (they may not
             * have been searched for, if intersects was already satisfied):
             */
            ms = mm->mm->mme;
            for (setIdx = 0; setIdx < ms->nels; setIdx++)
              {                                 /* for each set */
                byte    *selSearchBuf = NULL;

                sel = ms->el[setIdx];
                switch (sel->logic)
                  {
                  case LOGIAND:
                  case LOGISET:
                    /* further check below */
                    break;
                  case LOGINOT:                 /* do not look for NOTs */
                  case LOGIPROP:                /* no-op */
                    continue;
                  default:
                    putmsg(MERR, fn,
                       "Internal error: Unknown logic %d for set %d in query",
                           (int)sel->logic, (int)sel->orpos);
                    continue;                   /* do not know what to do */
                  }
                /* Add non-member hits skipped above: */
                selSearchBuf = (sel->hit ? sel->hit + sel->hitsz :
                                (byte *)mm->hitStart);
                if (!sel->member && sel->hit != BYTEPN &&
                    sel->hit >= (byte *)mm->hitStart &&
                    sel->hit + sel->hitsz <= (byte *)mm->hitEnd)
                  goto addHit;
                /* Find more hits for this set: */
                while (findsel(ms, setIdx, selSearchBuf, (byte *)mm->hitEnd,
                               SEARCHNEWBUF) != BYTEPN)
                  {                             /* while more hits for set */
                  addHit:
                    if (mm->numsh >= mm->shsz && !mm_incSetHits(mm)) goto err;
                    sh = &mm->setHits[mm->numsh];
                    sh->start = (char *)sel->hit;
                    sh->end = (char *)sel->hit + sel->hitsz;
                    sh->setOrpos = sel->orpos;
                    sh->pmtype = sel->pmtype;
                    sh->logic = sel->logic;
                    mm->numsh++;
                    /* Bug 5149: ensure forward progress if findsel()
                     * returns zero-length hit and does not advance:
                     */
                    selSearchBuf = TX_MAX((byte *)sh->end, selSearchBuf + 1);
                  }
              }
          }
        qsort(mm->setHits, mm->numsh, sizeof(MMSH), mm_sortcb);
      }
    /* Make sure set hits don't overlap: */
    for (i = 1; i < mm->numsh; i++) {
      sh = &mm->setHits[i];
      if (sh->start < sh[-1].end) {	/* overlaps previous hit */
	sh->start = sh[-1].end;
	if (sh->end < sh->start)
	  sh->end = sh->start;
      }
    }

    /* Back set hits off of overlap of <>, if any.  Do all set hits together
     * right after getmmapi(), so that we can keep a constant
     * `mm->hitStart'/`mm->hitEnd' for all set hits within it,
     * which caller assumes:
     */
    if (!(mm->flags & MF_NOTAGSKIP))
      for (i = 0; i < mm->numsh; i++)
        {
          s = ss = mm->setHits[i].start;
          e = se = mm->setHits[i].end;
          for (se = s; se < e; se++)            /* do we overlap `<' or `>' */
            {
              if (*se == '<') break;
              if (*se == '>') ss = se + 1;
            }
          if (ss == s && se == e)               /* no: see if inside `<..>' */
            {
              if (s < lastSetEnd)
                {
                  /*Last hit was pushed, too; advance us so it's not missed:*/
                  ss = lastSetEnd;
                  if (e < lastSetEnd) se = lastSetEnd;
                }
              else
                {
                  /* Look back to see if we're inside `<...>' */
                  for (p = s - 1;
                       (p >= lastSetEnd) && (*p != '<') && (*p != '>');
                       p--);
                  if (p >= lastSetEnd && *p == '<')
                    {                           /* inside tag; look for end */
                      for (p = e;
                           p < mm->end && (*p != '\0') && (*p != '>');
                           p++);
                      if (p < mm->end && *p == '>') p++;
                      ss = se = p;              /* move whole hit after tag */
                    }
                }
            }
          mm->setHits[i].start = ss;
          mm->setHits[i].end = lastSetEnd = se;
        }

    /* Make sure overall hit completely envelops set hits: */
    if (mm->hitStart > mm->setHits[0].start)
      mm->hitStart = mm->setHits[0].start;
    if (mm->hitEnd < mm->setHits[mm->numsh - 1].end)
      mm->hitEnd = mm->setHits[mm->numsh - 1].end;
  } else if (mm->cursh < 0) goto nohit;

  /* Pull off next set hit: */
  sh = &mm->setHits[mm->cursh];
  *setOrpos = sh->setOrpos;
  *setHitStart = sh->start;
  *setHitEnd = sh->end;
  if (pmtype) *pmtype = sh->pmtype;
  if (logic) *logic = sh->logic;

  /* No overall hit highlight for w/doc, since it would highlight the
   * whole document, which is useless:
   */
  apicp = mm->mm->acp;
  if (((mm->flags & MF_HILITEQUERY) ?
       ((apicp->sdexp == BYTEPN || *apicp->sdexp == '\0') &&
        (apicp->edexp == BYTEPN || *apicp->edexp == '\0')) :
       (mm->hitStart == mm->buf && mm->hitEnd == mm->end)) &&
      !mm->fmtcp->highlightWithinDoc)
    {
      *hitStart = *hitEnd = CHARPN;
    }
  else
    {
      *hitStart = mm->hitStart;
      *hitEnd = mm->hitEnd;
    }

  mm->cursh++;
  return(++mm->fs->lastHitNum);
}

/* ------------------------------------------------------------------------- */

/* We cannot rely on isalnum(), etc. to be consistent across platforms,
 * locales, or 8-bit unsigned chars, so define these macros ourselves:
 */

static CONST char       HexUp[] =    "0123456789ABCDEF";
static CONST char       HexLower[] = "0123456789abcdef";

typedef enum URLMODE_tag
  {
    URLMODE_NORMAL,
    URLMODE_PATH,
    URLMODE_QUERY
  }
  URLMODE;

static char *
dourl(char buf[4], unsigned ch, URLMODE mode)
/* Escapes `ch' for URL, returning string (possibly pointing to `buf').
 */
{
  buf[1] = '\0';
  /* NOTE: see also urlstrncpy() and USF_SAFE; maybe definition of safe
   * need to change here too, per RFC 2396?
   */
  switch ((byte)ch)
    {
    case ' ':
      if (TX_LEGACY_VERSION_7_URL_CODING_CURRENT(TXApp))
        {
          if (mode == URLMODE_NORMAL) return("+");
        }
      else                                      /* version 8+ */
        {
          if (mode == URLMODE_QUERY) return("+");
        }
      goto pct;
    /* don't translate '\0' to '&': may violate HTML spec */
    case ':':           /* part of pchar -> {path,query} in RFC 3986 */
      if (TX_LEGACY_VERSION_7_URL_CODING_CURRENT(TXApp) ||
          mode == URLMODE_QUERY)
        goto pct;
      goto asis;
    case '@':
    case '/':
      if (TX_LEGACY_VERSION_7_URL_CODING_CURRENT(TXApp) &&
          mode == URLMODE_QUERY)
        goto pct;
    /* NOTE: if unreserved or (non-query-string-relevant) sub-delims change,
     * update USF_SAFE behavior in urlstrncpy(); these are as-is chars:
     */
    /* unreserved (w/alnum below) in RFC 3986: */
    case '-':
    case '.':
    case '_':
      goto asis;
    case '~':
    /* sub-delims in RFC 3986: */
    case '!':
    case '$':
    case '\'':
    case '(':
    case ')':
    case '*':
    case ',':
      if (TX_LEGACY_VERSION_7_URL_CODING_CURRENT(TXApp)) goto pct;
      goto asis;
    /* query-string-relevant sub-delims: */
    case '&':
    case '+':
    case ';':
    case '=':
      if (TX_LEGACY_VERSION_7_URL_CODING_CURRENT(TXApp) ||
          mode != URLMODE_PATH)                 /* significant in query */
        goto pct;
      goto asis;
    default:
      if (TX_ISALNUM((byte)ch))
        {
        asis:
          buf[0] = ch;
        }
      else
        {
        pct:
          buf[0] = '%';
          buf[1] = HexUp[(ch >> 4) & 0xF];
          buf[2] = HexUp[ch & 0xF];
          buf[3] = '\0';
        }
      break;
    }
  return(buf);
}

static size_t double2frac ARGS((char *buf, size_t sz, double d, int w, int p));
static size_t
double2frac(buf, sz, d, w, p)
char    *buf;
size_t  sz;
double  d;
int     w, p;
/* Prints `d' as a fraction to buffer `buf' of size `sz'.  Returns would-be
 * length of result, ala htsnpf().  Uses printf.
 */
{
  int   whole, num, denom;

  (void)p;

  whole = (int)d;
  num = (int)((d - (double)whole)*64.0);        /* wtf to 64ths for now */
  if (num < 0)
    num = -num;
  else if (num == 0)
    return(htsnpf(buf, sz, "%*d", w, whole));
  if (whole < 0) whole = -whole;
  for (denom = 64; !(num & 1); denom >>= 1, num >>= 1);
  return(htsnpf(buf, sz, "%s%*d %d/%d",
                (d < 0.0 ? "-" : ""), w, whole, num, denom));
}

/* ------------------------------------------------------------------------ */

static int
TXgetPrecision(CONST char *fmtBuf)
/* Returns the precision of %f or %g format `fmtBuf'.
 */
{
  CONST char    *s;

  if ((s = strchr(fmtBuf, '.')) != CHARPN)      /* found it */
    return(atoi(s + 1));
  return(6);
}

/* ------------------------------------------------------------------------ */

static size_t
TXprintCoordinate(HTPFOUTCB *outFunc, void *outUserData, CONST char *fmt,
                  size_t fmtLen, double coord, int isLongitude)
/* Prints a decimal-degrees latitude or longitude `coord' according to
 * `fmt'.  Returns number of bytes printed.
 */
{
  static CONST char     okFlagChars[] = "0123456789.- ";
  CONST char            *s, *e, *fmtEnd;
  size_t                tmpFmtLen, totalPrinted = 0;
  double                degrees, minutes, seconds, dVal;
  char                  hemiLetter, hemiSign, *parseEnd;
  int                   pass, minutesRollover = 0, secondsRollover = 0;
  int                   minutesRoundup = 0, degreesRoundup = 0;
  int                   errnum;
  char                  tmpFmtBuf[512], *tmpFmt;
  enum                                          /* in ascending size order */
    {
      UNIT_SECONDS,
      UNIT_MINUTES,
      UNIT_DEGREES,
      UNIT_NONE,                                /* must be last */
    }
  lowestUnit, curUnit;
  enum
    {
      FORMAT_NONE,
      FORMAT_INT,
      FORMAT_DOUBLE_F,
      FORMAT_DOUBLE_G,
      NUM_FORMATS                               /* must be last */
    }
  curFormat, useFormat;
  static CONST char     formatChar[NUM_FORMATS] = "?dfg";
  size_t                prLen;
  int                   prOverflow = 0;
  char                  prBuf[512];
  /* PRFMT(f, arg):  prints `arg' using htpf format string `f'.
   * Note that some callers use `prBuf'/`prOverflow' afterwards:
   */
#define PRFMT(f, arg)                                           \
  {                                                             \
    prLen = htsnpf(prBuf, sizeof(prBuf) - 1, (f), (arg));       \
    if ((prOverflow = (prLen >= sizeof(prBuf) - 1)) != 0)       \
      {                                                         \
        prBuf[sizeof(prBuf) - 2] = TX_INVALID_CHAR;             \
        prBuf[sizeof(prBuf) - 1] = '\0';                        \
        prLen = sizeof(prBuf) - 1;                              \
      }                                                         \
    if (pass == 2) outFunc(outUserData, prBuf, prLen);          \
    totalPrinted += prLen;                                      \
  }
  /* FLOORVAL(f, d): returns double value `d', or 0 if d < 10^-(p-2),
   * where `p' is the precision in %g format `f'.  This should prevent
   * very small numbers (e.g. floating-point roundoff from `10deg
   * 35min') from causing %g to print in exponential format, unless
   * indirectly requested by expanding the precision in the format.
   * We do this FLOORVAL() modification for "neatness"; i.e. one
   * generally never expects exponential format in lat/lon numbers;
   * but one can get it if wanted via increasing the %g precision:
   */
#define LOG_E_10        2.302585092994045684017991454684
#define FLOORVAL(f, d)                                                  \
  ((d) < exp(((double)(-(TXgetPrecision(f) - 2)))*(double)LOG_E_10) ?   \
   (double)0.0 : (d))

  fmtEnd = fmt + (fmtLen != (size_t)(-1) ? fmtLen : strlen(fmt));

  /* Check for NaN etc.: - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (TXDOUBLE_IS_NaN(coord))
    {
      outFunc(outUserData, (char *)NaNStr, totalPrinted = NANSTR_LEN);
      goto done;
    }
  if (TXDOUBLE_IS_PosInf(coord))
    {
      outFunc(outUserData, (char *)InfStr, totalPrinted = INFSTR_LEN);
      goto done;
    }
  if (TXDOUBLE_IS_NegInf(coord))
    {
      outFunc(outUserData, "-", 1);
      totalPrinted = 1;
      outFunc(outUserData, (char *)InfStr, INFSTR_LEN);
      totalPrinted += INFSTR_LEN;
      goto done;
    }

  /* Compute values from `coord': - - - - - - - - - - - - - - - - - - - - - */
  if (coord >= 0.0)
    {
      hemiLetter = (isLongitude ? 'E' : 'N');
      hemiSign = '+';
      degrees = coord;
    }
  else
    {
      hemiLetter = (isLongitude ? 'W' : 'S');
      hemiSign = '-';
      degrees = -coord;
    }
  minutes = (degrees - floor(degrees))*60.0;
  /* WTF gcc 3.4.6 -O2 i686-unknown-linux2.6.9-64-32 floor() can
   * sometimes produce 1 less than the correct value, e.g. floor(19)
   * sometimes gives 18.  Check for it and correct:
   */
  while (minutes >= (double)60.0)
    {
      degrees++;
      minutes -= (double)60.0;
    }
  seconds = (minutes - floor(minutes))*60.0;
  while (seconds >= (double)60.0)
    {
      minutes++;
      seconds -= (double)60.0;
    }

  /* Parse and print `fmt'.  `minutes' and `seconds' are 0 <= N < 60.
   * But depending on the format code and flags, they may get rounded
   * up to 60 when printed (iff they are the smallest unit printed and
   * %g/%f format used).  This should rollover to 0 and carry 1 to the
   * next higher unit -- but we probably have already printed that
   * next higher unit.  So we parse `fmt' in 3 passes: the first
   * computes seconds rollover, the second computes minutes rollover
   * (which depends on seconds rollover), the third does the printing.
   * We cannot just check `minutes' and `seconds', because rollover
   * depends on the actual printed value, which depends on `fmt'.
   * (Note that this may fail if `fmt' prints minutes or seconds twice
   * for some odd reason, as one may rollover and the other not, and
   * we do not know which minutes or hours to carry into.)
   */

  lowestUnit = UNIT_NONE;
  for (pass = 0; pass < 3; pass++)
    {
      totalPrinted = 0;
      for (s = fmt; s < fmtEnd; s = e)
        {
          for (e = s; e < fmtEnd && *e != '%'; e++);    /* next fmt code */
          if (pass == 2 && e > s)               /* flush up to `%' or EOS */
            outFunc(outUserData, (char *)s, e - s);
          totalPrinted += e - s;
          s = e;                                /* the `%', or EOS */
          if (s >= fmtEnd) break;               /* EOS */

          /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
           * We now have a `%' format at `s'.  We allow `-/ /0/m.n'
           * flags; copy `%' up to just before the code to `tmpFmtBuf', as
           * we will pass it to htsnpf().  We also allow `d/i/f/g' flags
           * (for lowest unit anyway), which means use that printf() format.
           */
          curUnit = UNIT_NONE;
          curFormat = FORMAT_NONE;
          tmpFmtBuf[0] = '%';
          tmpFmtLen = 1;
          for (e = s + 1;
               e < fmtEnd && tmpFmtLen + 2 <= sizeof(tmpFmtBuf);
               e++)
            {                                   /* copy flags after `%' */
              if (*e == 'd' || *e == 'i')
                curFormat = FORMAT_INT;
              else if (*e == 'f')
                curFormat = FORMAT_DOUBLE_F;
              else if (*e == 'g')
                curFormat = FORMAT_DOUBLE_G;
              else if (*e == '\0')
                break;
              else if (strchr(okFlagChars, *e) != CHARPN)
                tmpFmtBuf[tmpFmtLen++] = *e;
              else
                break;
            }
          if (tmpFmtLen + 2 > sizeof(tmpFmtBuf))/* +2 for our fmt code + nul*/
            tmpFmtLen = 1;                      /* too large for buffer */
          tmpFmt = tmpFmtBuf + tmpFmtLen;

          /* Check and print the code: - - - - - - - - - - - - - - - - - - */
          switch (*e)
            {
            case 'D':                           /* degrees */
              curUnit = UNIT_DEGREES;
              if (curUnit < lowestUnit) lowestUnit = curUnit;
              if (pass < 2) break;              /*get min rollover, low unit*/
              useFormat = curFormat;
              if (tmpFmtLen == 1 && curFormat == FORMAT_NONE)
                {                               /* no flags: set defaults */
                  /* Default space padding, because `003' is not readable */
                  /* Default width 3 for lon or 2 for lat: minimal
                   * spacing needed so that a series of lat/lon values
                   * line up deg-to-deg, min-to-min etc. in a
                   * fixed-width output column:
                   */
                  *(tmpFmt++) = (isLongitude ? '3' : '2');
                }
              /* Allow %g/%f format only for the lowest unit being printed,
               * to avoid e.g. printing fraction of a degree *and* minutes,
               * which would be redundant and wrong:
               */
              if (lowestUnit < curUnit)
                useFormat = FORMAT_INT;
              /* Lowest unit uses %g by default, for more accuracy if
               * fractions, yet rounding to int (neat) if within 6 sigdigs:
               */
              else if (useFormat == FORMAT_NONE)
                useFormat = FORMAT_DOUBLE_G;
              *(tmpFmt++) = formatChar[useFormat];
              *tmpFmt = '\0';
              dVal = degrees;
              if (useFormat == FORMAT_INT)
                {
                  /* No roundup possible with int formats, so
                   * `degreesRoundup' stays 0:
                   */
                  PRFMT(tmpFmtBuf,
                        (int)(dVal + (minutesRollover ? 1.0 : 0.0)));
                }
              else                              /* floating-point format */
                {
                  if (useFormat == FORMAT_DOUBLE_G)
                    dVal = FLOORVAL(tmpFmtBuf, dVal);
                  /* See 'm' comment as to why `degreesRoundup' is needed: */
                  prLen = htsnpf(prBuf, sizeof(prBuf) - 1, tmpFmtBuf, dVal);
                  if (prLen < sizeof(prBuf) - 1 && /* `prBuf' ok */
                      (int)TXstrtod(prBuf, CHARPN, &parseEnd, &errnum) >
                      (int)dVal && errnum == 0 &&
                      *parseEnd == '\0')            /* successful parse */
                    degreesRoundup = 1;
                  PRFMT(tmpFmtBuf, (double)(dVal +
                           (minutesRollover && !degreesRoundup ? 1.0 : 0.0)));
                }
              break;
            case 'M':                           /* minutes */
              curUnit = UNIT_MINUTES;
              if (curUnit < lowestUnit) lowestUnit = curUnit;
              if (pass == 0) break;             /*get sec rollover, low unit*/
              useFormat = curFormat;
              if (tmpFmtLen == 1 && curFormat == FORMAT_NONE)
                {                               /* no flags: set defaults */
                  /* Default 0-padding for minutes, because it is like
                   * time, and prevents internal spaces in the overall
                   * default format -- keeps the lat/lon val "atomic"
                   * and easier to pull out as a column amongst other
                   * space-separated columns in a table:
                   */
                  *(tmpFmt++) = '0';
                  /* Default width 2, so a series of lat/lon values
                   * line up deg-to-deg, min-to-min etc. in a
                   * fixed-width output column:
                   */
                  *(tmpFmt++) = '2';
                }
              if (lowestUnit < curUnit)
                useFormat = FORMAT_INT;         /* g/f only for lowest unit */
              else if (useFormat == FORMAT_NONE)
                useFormat = FORMAT_DOUBLE_G;    /* g default for lowest unit*/
              *(tmpFmt++) = formatChar[useFormat];
              *tmpFmt = '\0';
              /*   Printed minutes could increment due to addition of 1.0
               * from `secondsRollover', *or* due to its own format code.
               * Check each separately, so we do not double-carry.
               * WTF is this still possible given that we force int format
               * (which has no roundup) for non-lowest units?
               *   Note that we do not check for rollover of smaller units
               * that are not printed, e.g. if `%D %gM' we do not check for
               * seconds rollover, even though it might affect printed
               * minutes: smallest unit printed should deal with any
               * smaller units/fractions, much like printf() `%.2f' should
               * deal with decimal places past 2 -- i.e. via rounding.
               */
              dVal = minutes;
              if (useFormat == FORMAT_DOUBLE_G)
                dVal = FLOORVAL(tmpFmtBuf, dVal);
              if (pass == 1)                    /*2nd pass: compute rollover*/
                {
                  if (useFormat == FORMAT_INT)  /* roundup not possible */
                    {
                      PRFMT(tmpFmtBuf,
                            (int)(dVal + (secondsRollover ? 1.0 : 0.0)));
                    }
                  else
                    {
                      PRFMT(tmpFmtBuf, dVal);
                      if (!prOverflow &&        /* `prBuf' valid */
                          (int)TXstrtod(prBuf, CHARPN, &parseEnd, &errnum) >
                          (int)dVal && errnum == 0 &&
                          *parseEnd == '\0')    /* successful parse */
                        minutesRoundup = 1;
                      PRFMT(tmpFmtBuf, (double)(dVal +
                           (secondsRollover && !minutesRoundup ? 1.0 : 0.0)));
                    }
                  if (dVal >= (double)50.0 &&   /* optimization */
                      !prOverflow &&            /* `prBuf' valid */
                      TXstrtod(prBuf, CHARPN, &parseEnd, &errnum) >=
                      (double)60.0 && errnum == 0 &&
                      *parseEnd == '\0')        /* successful parse */
                    minutesRollover = 1;
                }
              else                              /* 3rd pass: print it */
                {
                  if (useFormat == FORMAT_INT)
                    {
                      PRFMT(tmpFmtBuf, (int)(minutesRollover ? 0.0 : dVal +
                           (secondsRollover && !minutesRoundup ? 1.0 : 0.0)));
                    }
                  else
                    {
                      PRFMT(tmpFmtBuf, (double)(minutesRollover ? 0.0 : dVal +
                           (secondsRollover && !minutesRoundup ? 1.0 : 0.0)));
                    }
                }
              break;
            case 'S':                           /* seconds */
              curUnit = UNIT_SECONDS;
              if (curUnit < lowestUnit) lowestUnit = curUnit;
              if (pass == 1) break;             /* optimization */
              useFormat = curFormat;
              if (tmpFmtLen == 1 && curFormat == FORMAT_NONE)
                {                               /* no flags: set defaults */
                  /* See minutes comments for why these defaults: */
                  *(tmpFmt++) = '0';
                  *(tmpFmt++) = '2';
                }
              if (lowestUnit < curUnit)         /* cannot happen */
                useFormat = FORMAT_INT;
              else if (useFormat == FORMAT_NONE)
                useFormat = FORMAT_DOUBLE_G;
              *(tmpFmt++) = formatChar[useFormat];
              *tmpFmt = '\0';
              dVal = seconds;
              if (useFormat == FORMAT_DOUBLE_G)
                dVal = FLOORVAL(tmpFmtBuf, dVal);
              if (useFormat == FORMAT_INT)
                {
                  PRFMT(tmpFmtBuf, (int)(secondsRollover ? 0.0 : dVal));
                }
              else
                {
                  PRFMT(tmpFmtBuf, (double)(secondsRollover ? 0.0 : dVal));
                }
              if (pass == 0 &&                  /*1st pass: compute rollover*/
                  dVal >= (double)50.0 &&       /* optimization */
                  !prOverflow &&                /* `prBuf' valid */
                  TXstrtod(prBuf, CHARPN, &parseEnd, &errnum) >=
                  (double)60.0 && errnum == 0 && /* rollover */
                  *parseEnd == '\0')            /* successful parse */
                secondsRollover = 1;
              break;
            case 'H':                           /* hemisphere N/S/E/W */
              if (pass == 2) outFunc(outUserData, &hemiLetter, 1);
              totalPrinted++;
              break;
            case 'h':                           /* hemisphere +/- */
              if (pass == 2) outFunc(outUserData, &hemiSign, 1);
              totalPrinted++;
              break;
            case 'o':                           /* ISO-8859-1 degree symbol */
              if (pass == 2) outFunc(outUserData, "\xb0", 1);
              totalPrinted++;
              break;
            case 'O':                           /* UTF-8 degree symbol */
              if (pass == 2) outFunc(outUserData, "\xc2\xb0", 2);
              totalPrinted++;
              break;
            case '%':                           /* plain `%' */
              if (pass == 2) outFunc(outUserData, "%", 1);
              totalPrinted++;
              break;
            default:                            /* unknown code */
              if (pass == 2) outFunc(outUserData, (char *)s, e - s);
              totalPrinted += e - s;
              e--;                              /* keep "code"; may be nul */
              break;
            }
          e++;                                  /* skip the code */
        }
    }

done:
  return(totalPrinted);
#undef PRFMT
#undef LOG_E_10
#undef FLOORVAL
}

/* ------------------------------------------------------------------------- */

int
htfputcu(ch, fp)
int     ch;
FILE    *fp;
/* Prints `ch' to `fp' with URL escapement.
 */
{
  char  *s;
  char  buf[4];

  /* fputs() is slow under Irix? */
  for (s = dourl(buf, (unsigned)ch, URLMODE_NORMAL); *s != '\0'; s++)
    {
      if (putc(*s, fp) == EOF) return(EOF);
    }
  return((byte)ch);
}

int
htfputch(ch, fp)
int     ch;
FILE    *fp;
/* Prints `ch' to `fp' with HTML escapement.
 */
{
  char  *s;
  char  buf[EPI_OS_INT_BITS/3 + 6];

  /* fputs() is slow under Irix? */
  for (s = html2esc(((unsigned)ch & 0xff), buf, sizeof(buf), TXPMBUFPN);
       *s != '\0';
       s++)
    {
      if (putc(*s, fp) == EOF) return(EOF);
    }
  return((byte)ch);
}

char *
htsputcu(ch, s)
int     ch;
char    *s;
/* Prints `ch' to `s' with URL escapement.  Returns new end of string
 * (terminated).
 */
{
  char  *u;
  char  buf[4];

  for (u = dourl(buf, (unsigned)ch, URLMODE_NORMAL); *u != '\0'; s++, u++)
    *s = *u;
  *s = '\0';
  return(s);
}

char *
htsputch(ch, s)
int     ch;
char    *s;
/* Prints to `s' with HTML escapement.  Returns new end of string
 * (terminated).
 */
{
  char  *h;
  char  buf[EPI_OS_INT_BITS/3 + 6];

  for (h = html2esc(((unsigned)ch & 0xff), buf, sizeof(buf), TXPMBUFPN);
       *h != '\0';
       s++, h++)
    *s = *h;
  *s = '\0';
  return(s);
}

int
htfputsu(s, fp)
CONST char      *s;
FILE            *fp;
/* Like fputs(), but with URL escapement.
 */
{
  char  *u;
  char  buf[4];

  for ( ; *s != '\0'; s++)
    {
      /* fputs is slow under Irix? */
      for (u = dourl(buf, *((byte *)s), URLMODE_NORMAL); *u != '\0'; u++)
        {
          if (putc(*u, fp) == EOF) return(EOF);
        }
    }
  return(1);
}

int
htfputsh(s, fp)
CONST char      *s;
FILE            *fp;
/* Like fputs(), but with HTML escapement.
 */
{
  char  *u;
  char  buf[EPI_OS_INT_BITS/3 + 6];

  for ( ; *s != '\0'; s++)
    {
      /* fputs is slow under Irix? */
      for (u = html2esc(*((byte *)s), buf, sizeof(buf), TXPMBUFPN);
           *u != '\0';
           u++)
        {
          if (putc(*u, fp) == EOF) return(EOF);
        }
    }
  return(1);
}

int
htputsu(s)
CONST char      *s;
/* Like puts(), but with URL escapement.  Ending newline is _not_ escaped.
 */
{
  if (htfputsu(s, stdout) == EOF) return(EOF);
  if (putc('\n', stdout) == EOF) return(EOF);
  return(1);
}

int
htputsh(s)
CONST char      *s;
/* Like puts(), but with HTML escapement.  Ending newline is _not_ escaped.
 */
{
  if (htfputsh(s, stdout) == EOF) return(EOF);
  if (putc('\n', stdout) == EOF) return(EOF);
  return(1);
}

char *
htsputsh(s, d)
CONST char      *s;
char            *d;
/* Prints `s' to `d' with HTML escapement.  Returns new end of string
 * (terminated).
 */
{
  char  *u;
  char  buf[EPI_OS_INT_BITS/3 + 6];

  for ( ; *s != '\0'; s++)
    {
      for (u = html2esc(*((byte *)s), buf, sizeof(buf), TXPMBUFPN);
           *u != '\0';
           u++, d++)
        *d = *u;
    }
  *d = '\0';
  return(d);
}

/* ------------------------------------------------------------------------- */

CONST char              TxfmtcpDefaultQueryClass[] = "query";
CONST char * CONST      TxfmtcpDefaultQuerySetClasses[] =
{
  "queryset1",
  /* auto-increment will handle the rest, up to
   * TXFMTCP_DEFAULT_QUERYSETCYCLENUM
   */
  CHARPN                                        /* must be last */
};
#define TXFMTCP_DEFAULT_QUERYSETCLASSES_NUM     \
  (sizeof(TxfmtcpDefaultQuerySetClasses)/       \
   sizeof(TxfmtcpDefaultQuerySetClasses[0]) - 1)

/* for I (inline style); note combo/insert usage of these below: */
/* NOTE: `TxfmtcpDefaultQueryStyle' is assumed to completely replace
 * each of `TxfmtcpDefaultQuerySetStyles', so that only the latter
 * is printed if both are needed.  This optimization does not apply
 * for user-supplied styles: we do not know anything about their styles:
 */
CONST char              TxfmtcpDefaultQueryStyle[] =
  "background:#f0f0f0;color:black;font-weight:bold;";
CONST char * CONST
  TxfmtcpDefaultQuerySetStyles[TXFMTCP_DEFAULT_QUERYSETCYCLENUM] =
{
  "background:#ffff66;color:black;font-weight:bold;",
  "background:#a0ffff;color:black;font-weight:bold;",
  "background:#99ff99;color:black;font-weight:bold;",
  "background:#ff9999;color:black;font-weight:bold;",
  "background:#ff66ff;color:black;font-weight:bold;",
  "background:#880000;color:white;font-weight:bold;",
  "background:#00aa00;color:white;font-weight:bold;",
  "background:#886800;color:white;font-weight:bold;",
  "background:#004699;color:white;font-weight:bold;",
  "background:#990099;color:white;font-weight:bold;",
};
CONST TXFMTCP   TxfmtcpDefault = TXFMTCP_DEFAULT_DECL;


int
TxfmtcpSetQuerySetStyles(TXFMTCP        *fmtcp,         /* (in/out) */
                         TXPMBUF        *pmbuf,         /* for messages */
                         char           **querySetStyles, /* (in) styles */
                         int            fmtcpOwns)      /* (in) */
/* Returns 0 on error.
 * `querySetStyles' may be NULL for none, or "default" for defaults.
 * If `fmtcpOwns' is non-zero, `querySetStyles' is assumed alloced and
 * to be owned (and freed) by `fmtcp', even on error.
 */
{
  size_t        n;

  if (fmtcp->querySetStyles != CHARPPN &&
      fmtcp->querySetStyles != (char **)TxfmtcpDefaultQuerySetStyles)
    fmtcp->querySetStyles =
      TXfreeStrList(fmtcp->querySetStyles, fmtcp->numQuerySetStyles);
  if (querySetStyles == CHARPPN ||
      querySetStyles[0] == CHARPN ||
      (querySetStyles[0][0] == '\0' && querySetStyles[1] == CHARPN))
    {                                           /* empty list */
      fmtcp->numQuerySetStyles = 0;
      fmtcp->querySetStyles = CHARPPN;
      if (fmtcpOwns)
        querySetStyles = TXfreeStrList(querySetStyles, -1);
    }
  else if (querySetStyles == (char **)TxfmtcpDefaultQuerySetStyles)
    {
      fmtcp->numQuerySetStyles = TXFMTCP_DEFAULT_QUERYSETCYCLENUM;
      fmtcp->querySetStyles = (char **)TxfmtcpDefaultQuerySetStyles;
    }
  else if (querySetStyles[1] == CHARPN &&
           strcmpi(querySetStyles[0], "default") == 0)
    {
      fmtcp->numQuerySetStyles = TXFMTCP_DEFAULT_QUERYSETCYCLENUM;
      fmtcp->querySetStyles = (char **)TxfmtcpDefaultQuerySetStyles;
      if (fmtcpOwns)
        querySetStyles = TXfreeStrList(querySetStyles, -1);
    }
  else
    {
      for (n = 0; querySetStyles[n] != CHARPN; n++);
      fmtcp->numQuerySetStyles = (int)n;
      if (fmtcpOwns)                            /* we own it now */
        {
          fmtcp->querySetStyles = querySetStyles;
          querySetStyles = NULL;
        }
      else if ((fmtcp->querySetStyles =
                TXdupStrList(pmbuf, querySetStyles, n)) == CHARPPN)
        {
          fmtcp->numQuerySetStyles = 0;
          goto err;
        }
    }
  return(1);
err:
  return(0);
}

int
TxfmtcpSetQuerySetClasses(TXFMTCP       *fmtcp,         /* (in/out) */
                          TXPMBUF       *pmbuf,         /* for messages */
                          char          **querySetClasses, /* (in) classes */
                          int           fmtcpOwns)      /* (in) */
/* Returns 0 on error.
 * `querySetClasses' may be NULL for none, or "default" for defaults.
 * If `fmtcpOwns' is non-zero, `querySetClasses' is assumed alloced and
 * to be owned (and freed) by `fmtcp', even on error.
 */
{
  size_t        n;

  if (fmtcp->querySetClasses != CHARPPN &&
      fmtcp->querySetClasses != (char **)TxfmtcpDefaultQuerySetClasses)
    fmtcp->querySetClasses =
      TXfreeStrList(fmtcp->querySetClasses, fmtcp->numQuerySetClasses);
  if (querySetClasses == CHARPPN ||
      querySetClasses[0] == CHARPN ||
      (querySetClasses[0][0] == '\0' && querySetClasses[1] == CHARPN))
    {                                           /* empty list */
      fmtcp->numQuerySetClasses = 0;
      fmtcp->querySetClasses = CHARPPN;
      if (fmtcpOwns)
        querySetClasses = TXfreeStrList(querySetClasses, -1);
    }
  else if (querySetClasses == (char **)TxfmtcpDefaultQuerySetClasses)
    {
      fmtcp->numQuerySetClasses = TXFMTCP_DEFAULT_QUERYSETCLASSES_NUM;
      fmtcp->querySetClasses = (char **)TxfmtcpDefaultQuerySetClasses;
    }
  else if (querySetClasses[1] == CHARPN &&
           strcmpi(querySetClasses[0], "default") == 0)
    {
      fmtcp->numQuerySetClasses = TXFMTCP_DEFAULT_QUERYSETCLASSES_NUM;
      fmtcp->querySetClasses = (char **)TxfmtcpDefaultQuerySetClasses;
      if (fmtcpOwns)
        querySetClasses = TXfreeStrList(querySetClasses, -1);
    }
  else
    {
      for (n = 0; querySetClasses[n] != CHARPN; n++);
      fmtcp->numQuerySetClasses = (int)n;
      if (fmtcpOwns)                            /* we own it now */
        {
          fmtcp->querySetClasses = querySetClasses;
          querySetClasses = NULL;
        }
      else if ((fmtcp->querySetClasses =
                TXdupStrList(pmbuf, querySetClasses, n)) == CHARPPN)
        {
          fmtcp->numQuerySetClasses = 0;
          goto err;
        }
    }
  return(1);
err:
  return(0);
}

int
TxfmtcpCreateStylesheet(HTBUF           *buf,   /* (out) buf to print to */
                        CONST TXFMTCP   *fmtcp) /* (in, opt) style settings */
/* Prints a style sheet to `buf' from `fmtcp' current settings, i.e.
 * mapping classes to styles.
 * Returns 0 on error.
 */
{
  size_t        i, nClassPr, maxI;
  int           idx, ival;
  char          *style, *class, *className, *e;
  char          setClassBuf[256];

  if (fmtcp == TXFMTCPPN) fmtcp = &TxfmtcpDefault;

  if (fmtcp->queryClass != CHARPN &&
      fmtcp->queryStyle != CHARPN &&
      !htbuf_pf(buf, ".%H { %H }\n", fmtcp->queryClass, fmtcp->queryStyle))
    goto err;
  if (fmtcp->querySetStyles != CHARPPN &&
      fmtcp->numQuerySetStyles > 0 &&
      fmtcp->querySetClasses != CHARPPN &&
      fmtcp->numQuerySetClasses > 0)
    {
      /* querysetcyclenum == 0 means infinite; pick a reasonable limit: */
      if ((maxI = fmtcp->querySetCycleNum) <= 0)
        maxI = TX_MAX(fmtcp->numQuerySetStyles, fmtcp->numQuerySetClasses);
      for (i = 0; i < maxI; i++)
        {
          /* Get class.  NOTE: see also htpfengine(): */
          idx = (int)fmtcp->querySetCycleNum;
          if (idx <= 0) idx = MAXINT;
          idx = i % idx;
          /* Re-use last class if beyond array end.  But unlike
           * styles, we can increment the first number in the name,
           * for uniqueness:
           */
          if (idx >= fmtcp->numQuerySetClasses)
            {
              e = className = fmtcp->querySetClasses[
                                               fmtcp->numQuerySetClasses - 1];
              e += strcspn(className, "0123456789");
              nClassPr = htsnpf(setClassBuf,
                                sizeof(setClassBuf), "%.*s",
                                (int)(e - className), className);
              ival = (int)strtol(e, &e, 0);
              if (nClassPr < sizeof(setClassBuf) - 1)
                nClassPr += htsnpf(setClassBuf + nClassPr,
                                   sizeof(setClassBuf) - nClassPr,
                                   "%d%s", ival +
                                   (idx - fmtcp->numQuerySetClasses) + 1, e);
              class = setClassBuf;
            }
          else
            class = fmtcp->querySetClasses[idx];

          /* Get style.  NOTE: see also htpfengine(): */
          idx = (int)fmtcp->querySetCycleNum;
          if (idx <= 0) idx = MAXINT;
          idx = i % idx;
          /* Re-use last style if beyond array end: */
          if (idx >= fmtcp->numQuerySetStyles)
            idx = fmtcp->numQuerySetStyles - 1;
          style = fmtcp->querySetStyles[idx];

          /* Print stylesheet entry: */
          if (!htbuf_pf(buf, ".%H { %H }\n", class, style))
            goto err;
        }
    }
  return(1);
err:
  return(0);
}

TXFMTSTATE *
TxfmtstateOpen(pmbuf)
TXPMBUF *pmbuf;         /* (in) for error messages */
/* Allocates a TXFMTSTATE object for htpf() and friends.
 * Returns object, or NULL on error.
 */
{
  static CONST char     fn[] = "TxfmtstateOpen";
  TXFMTSTATE            *fs;

  if (!(fs = (TXFMTSTATE *)TXcalloc(pmbuf, fn, 1, sizeof(TXFMTSTATE))))
    goto err;
  TXFMTSTATE_INIT(fs);
  goto done;

err:
  fs = TxfmtstateClose(fs);
done:
  return(fs);
}

int
TxfmtstateCloseCache(fs)
TXFMTSTATE      *fs;
/* Closes Metamorph cache in `fs'.
 * Returns 0 on error.
 */
{
  while (fs->mmList != MMPN) delete_mm(fs, fs->mmList);
  return(1);
}

TXFMTSTATE *
TxfmtstateClose(fs)
TXFMTSTATE      *fs;
/* Frees a TXFMTSTATE object created with TxfmtstateOpen().
 * Returns NULL.
 */
{
  if (fs != TXFMTSTATEPN)
    {
      TXFMTSTATE_RELEASE(fs);
      fs = TXfree(fs);
    }
  return(TXFMTSTATEPN);
}

/* ------------------------------------------------------------------------- */

static void
TXhtpfNullOutputCb(void *outdata, char *s, size_t sz)
{
  /* No-op; used with HTPFF_NOOUTPUT */
  (void)outdata;
  (void)s;
  (void)sz;
}

/* ------------------------------------------------------------------------- */

static size_t
TXhtpfDoCompression(TXPMBUF *pmbuf, HTPFOUTCB *outFunc, void *outUserData,
                    size_t outMax, byte *inBuf, size_t inBufSz,
                    TXZLIBFORMAT format, TXbool decode, int traceEncoding)
/* Handles `%z' htpf() code.  Internal use.  `outMax' is max total bytes
 * to output; -1 for no limit.
 * Returns number of bytes printed (via outFunc()).
 */
{
  TXZLIB        *zlibHandle = NULL;
  TXFILTERFLAG  filterFlags;
  size_t        outTotal = 0, curOutLen;
  TXCTEHF       cteFlags;
  int           gzipTransRes = 0, noProgressNum = 0;
  byte          *curInBuf;
  byte          *curOutBuf, outBuf[65536];

  filterFlags = (TXFILTERFLAG)0;
  if (decode) filterFlags |= TXFILTERFLAG_DECODE;
  else filterFlags |= TXFILTERFLAG_ENCODE;

  zlibHandle = TXzlibOpen(format, filterFlags, traceEncoding, pmbuf);
  if (!zlibHandle) goto err;

  cteFlags = TXCTEHF_INPUT_EOF;

  do
    {
      /* Decode input via gzip: */
      curInBuf = inBuf;
      curOutBuf = outBuf;
      gzipTransRes = TXzlibTranslate(zlibHandle, cteFlags, &curInBuf,
                                     inBufSz, &curOutBuf, sizeof(outBuf));
      /* Output data, update buffers.  Do before checking result code,
       * as we might consume/get data even on error:
       */
      curOutLen = curOutBuf - outBuf;
      if (curOutLen > 0)                        /* generated some output */
        {
          if (outMax != (size_t)(-1) &&         /* if have limit and */
              curOutLen > outMax - outTotal)    /*   will be over limit */
            curOutLen = outMax - outTotal;      /* trim to `outMax' limit */
          outFunc(outUserData, (char *)outBuf, curOutLen);
          outTotal += curOutLen;
        }
      /* Check for forward progress: */
      if (curInBuf == inBuf && curOutLen == 0) noProgressNum++;
      /* Advance input past what was consumed: */
      inBufSz -= (curInBuf - inBuf);
      inBuf = curInBuf;
    }
  while (gzipTransRes == 1 && noProgressNum <= 5 &&
         (outMax == (size_t)(-1) || outTotal < outMax));

  /* Check last result code: */
  switch (gzipTransRes)
    {
    case 0:                                     /* error */
    default:
      goto err;
    case 1:                                     /* ok */
    case 2:                                     /* ok, output EOF */
      /* wtf if not input eof, keep reading to consume it? */
      break;
    }

  if (noProgressNum > 5)
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                     "Internal error: no forward progress with gzip data");
      goto err;
    }

  goto done;

err:
done:
  zlibHandle = TXzlibClose(zlibHandle);
  return(outTotal);
}

/* ------------------------------------------------------------------------- */

typedef struct TXhtpfEngineState_tag
{
  const TXFMTCP *fmtcp;
  MF            mFlags;
  TXbool        curSetSameAsOverallHit;
  TXPMTYPE      curSetPmtype;
  TXbool        copyToCurOpenSpan;
}
TXhtpfEngineState;

/* ------------------------------------------------------------------------- */

size_t
htpfengine(fmt, fmtsz, flags, fmtcp, fs, argp, getarg, getdata, args, argnp,
           out, outdata, pmbuf)
CONST char      *fmt;           /* (in) format string */
size_t          fmtsz;          /* (in) length of `fmt' */
HTPFF           flags;          /* (in) flags */
CONST TXFMTCP   *fmtcp;         /* (in, opt.) settings */
TXFMTSTATE      *fs;            /* (in/out, opt.) state */
HTPF_SYS_VA_LIST argp;          /* (in) (opt.) format args */
HTPFCB          *getarg;        /* (in) (opt.) format arg reader callback */
void            *getdata;       /* (in) (opt.) user data for `getarg' */
HTPFARG         *args;          /* (in/out) (opt.) read/write format args */
size_t          *argnp;         /* (in/out) (opt.) size of `args' array */
HTPFOUTCB       *out;           /* (in) output string callback */
void            *outdata;       /* (in) (opt.) user data for `outdata' */
TXPMBUF         *pmbuf;         /* (out) (opt.) putmsg buffer */
/* Print engine; calls out(outdata, s, sz) to output a string at
 * a time.  Returns number of chars printed.  This function is
 * potentially recursive, as it calls sprintf().  Be afraid.  If
 * `getarg' is non-NULL, then getarg(type, what, getdata, &err) is
 * called to fetch each argument; else if HTPFF_RDHTPFARGS is set,
 * the `args' array is used; else `argp'.  Args are written to `args'
 * if HTPFF_WRHTPFARGS flag is set.
 */
{
  static CONST char     fn[] = "htpfengine";
  static CONST char     OutOfArgs[] = "Out of args";
  static CONST char     Question[] = "?";
  static const char     UnbalancedCloseSpanFmt[] =
    "Internal error: Unbalanced `</span>' averted at %s:%d";
#ifndef EPI_ENABLE_STYLE_HIGHLIGHTING
  static int            StyleHighlightingEnabled = -1;
#endif /* !EPI_ENABLE_STYLE_HIGHLIGHTING */
  char	*p, *pend;
  char	*p2, ch;
  int	size, width, prec, i, bwidth, overflowedPrecision;
  size_t        pwidth, padCharBufSz, argi = 0, pReplaceStrSz, spanDepth;
  char	padCharBuf[8];
  int   n = 0;
  unsigned int	u;
  int	base;
  int	pflags, isneg;
  UTF   utf, utfstate;
  char	signflag;
  CONST char    *digs;
#ifndef NOLONG
  long	l;
  ulong	ul;
#  ifdef EPI_HAVE_LONG_LONG
  long long ll;
#  endif
#  ifdef EPI_HAVE_UNSIGNED_LONG_LONG
  unsigned long long ull;
#  endif
#endif		/* !NOLONG */
#ifdef EPI_HAVE_LONG_DOUBLE
  long double   longDouble;
#endif
  EPI_HUGEFLOAT hugeFloat;
  EPI_HUGEINT   hi;
  EPI_HUGEUINT  hui;
  HTPFW         argWhat;
#ifdef FLOATING
  double d;
  int	decpt = 0;
  char	echar, decch = '.';
  int	negexp;
  char	*p3 = NULL, *pd, *pe;
#endif	/* FLOATING */
#ifdef ROMAN
  char	*rdigs;
  int	dig;
#endif	/* ROMAN */
#define LCONVPN ((struct lconv *)NULL)
#ifdef EPI_HAVE_LOCALE_H
  struct lconv  *lc = LCONVPN;
#endif
  char	*pts;
  size_t        nprinted = 0;
  size_t        sn;
  char          och;
#define PUTC(c) (och = (char)(c), out(outdata, &och, 1), nprinted++)
#define PUTSZ(s, sz)    (out(outdata, (s), (sz)), nprinted += (sz))
#define PUTS(s) \
  (sn = strlen(s), out(outdata, (s), sn), n += (int)sn, nprinted += sn)
  int	okflags;
  int	sch, curmmcnt, nextmmcnt;
  char	*mquery = CHARPN;
  int	okmflags;
  size_t        pbc, nClassPr;
  char  dourlbuf[4];
  char  html2escbuf[EPI_OS_INT_BITS/3 + 6];
  char	*curSetHitStart, *curSetHitEnd, *nextSetHitStart, *nextSetHitEnd;
  char  *nextEvent;
  int   curSetOrpos, nextSetOrpos, idx;
  TXPMTYPE      nextSetPmtype;
  char  *curOverallHitStart, *curOverallHitEnd;
  char  *nextOverallHitStart, *nextOverallHitEnd;
  char  *curOverallHitStartOrg, *curOverallHitEndOrg;
  char  *nextOverallHitStartOrg, *nextOverallHitEndOrg;
  char  *e, *className;
  int   doQueryStyle = 0, doQueryClass = 0, doQuerySetStyle = 0,
    doQuerySetClass = 0;
  int   ival;
  TXbool        nextSetSameAsOverallHit;
  MM	*mm = MMPN;
  FFS   *paraRex = FFSPN;
  char  *nextPara;
  size_t dtot;
  TXPMBUF       *orgHtpfobjPmbuf = NULL;
  HTCHARSETFUNC *transfunc;
  char  *inChunkEnd;
  char  *ksep, *ksepe;
  char	*hs, *tfmt, *tfmte;
  TXTIMEINFO    timeinfo;
  int           getTimeinfoResult;
  size_t        (*strftimefunc) ARGS((char *, size_t, CONST char *,
                                      const TXTIMEINFO *));
  CONST char    *fmte;
  char          *pReplaceStr;
  char          *pexpr, hbuf[TX_MAX(240, NANSTR_LEN) + EPI_HUGEUINT_BITS/4];
  char          curOpenSpan[TX_MAX(240, NANSTR_LEN) + EPI_HUGEUINT_BITS/4];
  char          setClassBuf[TX_MAX(240, NANSTR_LEN) + EPI_HUGEUINT_BITS/4];
  char          *buf = hbuf, *bufend;
  size_t        bufsz = sizeof(hbuf);
#define ALLOCBUF(n)                                             \
  if ((size_t)(n) > bufsz)                                      \
    {                                                           \
      if (buf != hbuf && buf != CHARPN) buf = TXfree(buf);      \
      bufsz = (size_t)(n);                                      \
      if (!(buf = (char *)TXmalloc(pmbuf, fn, bufsz))) goto memerr; \
      bufend = buf + bufsz;                                     \
    }
#ifdef USE_SNPRINTF_NOT_CVT
#  ifdef EPI_OS_DOUBLE_MAX_BASE10_EXPONENT
  char          cvtbuf[EPI_OS_DOUBLE_MAX_BASE10_EXPONENT + 10];
#  else /* !EPI_OS_DOUBLE_MAX_BASE10_EXPONENT */
  char          cvtbuf[308 + 10];
#  endif /* !EPI_OS_DOUBLE_MAX_BASE10_EXPONENT */
#endif /* USE_SNPRINTF_NOT_CVT */
  time_t	tim;
  double        coord, coord2;
  void          *ptr, *vp;
  size_t        sz;
  TXZLIBFORMAT  zlibFormat;
  char          *errstr;
  CONST char    *fmtstart;
  TXFMTSTATE    tmpFs;
  TXhtpfEngineState     state;
#define GETARG(v, h, w, t, vat, gadr, e)                        \
{                                                               \
  sz = (size_t)(-1);                                            \
  if (getarg != HTPFCBPN)                                       \
    {                                                           \
      errstr = (char *)fmt;                                     \
      if ((ptr = getarg(h, w, getdata, &errstr, &sz)) != NULL)  \
        v = gadr(t *)ptr;                                       \
      else                                                      \
        goto e;                                                 \
    }                                                           \
  else if (flags & HTPFF_RDHTPFARGS)                            \
    {                                                           \
      if (argi < *argnp)                                        \
        v = *((vat *)&args[argi++]);                            \
      else                                                      \
        {                                                       \
          if (argi++ == *argnp)                                 \
            txpmbuf_putmsg(pmbuf, MERR + MAE, fn, OutOfArgs);   \
          errstr = (char *)Question;                            \
          goto e;                                               \
        }                                                       \
    }                                                           \
  else                                                          \
    {                                                           \
      v = va_arg(argp, vat);                                    \
      if (flags & HTPFF_WRHTPFARGS)                             \
        {                                                       \
          if (argi < *argnp)                                    \
            *((vat *)&args[argi++]) = v;                        \
          else                                                  \
            {                                                   \
              if (argi++ == *argnp)                             \
                txpmbuf_putmsg(pmbuf, MERR+MAE, fn, OutOfArgs); \
              errstr = (char *)Question;                        \
              goto e;                                           \
            }                                                   \
        }                                                       \
    }                                                           \
}

  if (fmtcp == TXFMTCPPN) fmtcp = &TxfmtcpDefault;

  /* `fmtcp->htpfobj' might come from Vortex; make sure we do not buffer putmsgs
   * into <urlinfo putmsgs>, we are independent: consistent single TXPMBUF:
   */
   /* Caller should have taken care of */

  /*
  if (fmtcp->htobj)
    {
      orgHtobjPmbuf = fmtcp->htobj->pmbuf;
      fmtcp->htobj->pmbuf = pmbuf;
    }
*/

  if (fs == TXFMTSTATEPN)
    {
      TXFMTSTATE_INIT(&tmpFs);
      fs = &tmpFs;
    }
  *curOpenSpan = '\0';
  if (fmt == CHARPN)                            /* sanity check */
    {
      fmt = NullStr;
      fmtsz = (size_t)(-1);
    }
  fmte = fmt + (fmtsz == (size_t)(-1) ? strlen(fmt) : fmtsz);

  if (flags & HTPFF_NOOUTPUT) out = TXhtpfNullOutputCb;

  /* Init `state': */
  memset(&state, 0, sizeof(TXhtpfEngineState));
  state.fmtcp = fmtcp;

  /* tell caller to start arg count (in case they need to reset): */
  if (getarg != HTPFCBPN)
    {
      errstr = (char *)fmt;
      sz = (size_t)(-1);
      if (getarg(HTPFT_VOID, HTPFW_START, getdata, &errstr, &sz) == NULL &&
          errstr != fmt && errstr != CHARPN)
        {
          PUTS(errstr);
        }
    }

  while (fmt < fmte)
      {
        for (pts = (char *)fmt; pts < fmte && *pts != '%'; pts++);
        if (pts > (char *)fmt)                  /* literal text to print */
          {
            PUTSZ((char *)fmt, pts - (char *)fmt);
            fmt = pts;
          }
        if (fmt >= fmte) break;                 /* end of format string */
        fmtstart = fmt;         /* save in case unknown format code */
	fmt++;                  /* skip the '%' */

	width = bwidth = 0;
	prec = -1;
        overflowedPrecision = 0;
	pflags = PRF_SPACEPAD;
        if (flags & HTPFF_PROMOTE_TO_HUGE) pflags |= PRF_HUGE;
        utf = UTF_START;
        /* transfunc() below (e.g. TXencodedWordToUtf8()) may use `utf'
         * flags to override HTPFOBJ flags, so pass HTPFOBJ flags into `utf' here:
         */
        if (fmtcp->htpfobj && fmtcp->htpfobj->charsetmsgs)
          utf |= UTF_BADCHARMSG;
        utfstate = (UTF)0;
	signflag = '-';
	padCharBuf[0] = ' ';
        padCharBufSz = 1;
	state.mFlags = 0;
	tfmt = (char *)DefTimeFmt;
        tfmte = (char *)DefTimeFmt + DEFTIMEFMT_LEN;
        strftimefunc = TXosStrftime;
        ksep = ksepe = CHARPN;
        pexpr = (char *)DefParaRex;
        pReplaceStr = (char *)DefParaReplace;
        pReplaceStrSz = sizeof(DefParaReplace) - 1;
        bufend = buf + bufsz;
        transfunc = HTCHARSETFUNCPN;

    if (fmt < fmte)                             /* Bug 6231 */
      {
	for (okflags = 1; okflags; ) {
	  switch (*fmt) {	/* check flags */
	    case '-':
              if (!(pflags & PRF_SPACEPAD))     /* don't override "&nbsp;" */
                {
                  padCharBuf[0] = ' ';              /* but override "0" */
                  padCharBufSz = 1;
                }
	      pflags |= (PRF_LADJUST | PRF_SPACEPAD);
	      fmt++;
	      break;
	    case '+':
              signflag = '+';                   /* "+" overrides " " */
              fmt++;
              break;
	    case ' ':
              if (signflag != '+') signflag = ' ';
	      fmt++;
	      break;
            case '_':                           /* KNG 990708 */
              pflags |= PRF_SPACEPAD;
              padCharBuf[0] = (char)160;
              padCharBufSz = 1;
              utf |= UTF_SAVEBOM;               /* 030406 */
              fmt++;
              break;
            case '&':                           /* KNG 990707 */
              pflags |= PRF_SPACEPAD;
              padCharBuf[0] = '&';
              padCharBuf[1] = 'n';
              padCharBuf[2] = 'b';
              padCharBuf[3] = 's';
              padCharBuf[4] = 'p';
              padCharBuf[5] = ';';
              padCharBufSz = 6;
              fmt++;
              break;
            case '!':                           /* KNG 000913 */
              pflags |= PRF_DECODE;
              fmt++;
              break;
            case 'j':
#ifdef _WIN32
              utf |= (UTF_CRNL | UTF_LFNL);
#elif defined(unix)
              utf &= ~UTF_CRNL;
              utf |= UTF_LFNL;
#else /* !_WIN32 && !unix */
              error;
#endif /* !_WIN32 && !unix */
              if (++fmt < fmte)
                switch (*fmt)
                {
                case 'c':
                  utf &= ~UTF_LFNL;
                  utf |= UTF_CRNL;
                  if (++fmt < fmte && *fmt == 'l') { utf |= UTF_LFNL; fmt++; }
                  break;
                case 'l':
                  utf &= ~UTF_CRNL;
                  utf |= UTF_LFNL;
                  if (++fmt < fmte && *fmt == 'c') { utf |= UTF_CRNL; fmt++; }
                  break;
                }
              break;
	    case '0':
	      if (!(pflags & PRF_LADJUST))
                {
                  pflags &= ~PRF_SPACEPAD;
                  padCharBuf[0] = '0';
                  padCharBufSz = 1;
                }
	      fmt++;
	      break;
	    case '*':
              GETARG(width, HTPFT_INT, HTPFW_WIDTH, int, int, *, flaggae);
              fmt++;
	      if (width < 0)
                {
                  pflags |= PRF_LADJUST;
                  width = -width;
                }
	      break;
	    case '.':
	      fmt++;
              if (fmt < fmte && *fmt == '*')
                {
                  GETARG(prec, HTPFT_INT, HTPFW_PREC, int, int, *, flaggae);
                  fmt++;
                }
              else
                {
                  prec = i = 0;         /* no digits => 0 prec. KNG 961118 */
                  if (fmt < fmte && *fmt == '-')        /* should be error? */
                    {
                      i = 1;
                      fmt++;
                    }
                  while (fmt < fmte && TX_ISDIGIT(*fmt))
                    prec = 10*prec + Ctod(*fmt++);
                  if (i) prec = -prec;
                }
              if (prec < 0) prec = -1;  /* neg. precision invalid KNG 970111 */
	      break;
	    case '#':
	      pflags |= PRF_NUMSGN;
	      fmt++;
	      break;
	    case 'l':
#ifndef NOLONG
              if (pflags & PRF_LONG) pflags |= PRF_LONGLONG;
	      pflags |= PRF_LONG;
#endif
	      fmt++;
	      break;
            case 'w':           /* whopping (EPI_HUGE[U]INT/EPI_HUGEFLOAT) */
              pflags |= PRF_HUGE;
              fmt++;
              break;
#ifdef _WIN32
            case 'I':           /* I32|I64 */
              fmt++;
              if (fmt + 1 < fmte)
                {
                  if (*fmt == '3' && fmt[1] == '2')
                    pflags &= ~(PRF_LONG|PRF_LONGLONG|PRF_HUGE);
                  else if (*fmt == '6' && fmt[1] == '4')
                    {
                      pflags &= ~(PRF_LONG|PRF_LONGLONG);
                      pflags |= PRF_HUGE;
                    }
                }
              while (fmt < fmte && *fmt >= '0' && *fmt <= '9') fmt++;
              break;
#endif /* _WIN32 */
	    case 'h':
	      /*
	       *  Shorts and floats are widened when passed,
	       *  so nothing needs be done here (although
	       *  %hf may be significant for ANSI C).
	       */
              switch (utf & (UTF_ESCRANGE | UTF_ESCLOW))
                {
                case (UTF)0:                    /* no previous 'h' */
                  utf |= UTF_ESCRANGE;
                  break;
                case UTF_ESCRANGE:              /* 'h' previous */
                  utf |= UTF_ESCLOW;
                  break;
                case (UTF_ESCRANGE | UTF_ESCLOW):       /* 'hh' previous */
                  utf &= ~UTF_ESCRANGE;
                  break;
                case UTF_ESCLOW:                /* 'hhh' previous */
                  break;
                }
	      fmt++;
	      break;
            case '<':           /* %v/%!v little-endian */
              utf &= ~UTF_BIGENDIAN;
              utf |= UTF_LITTLEENDIAN;
              fmt++;
              break;
            case '>':           /* %v/%!v big-endian */
              utf &= ~UTF_LITTLEENDIAN;
              utf |= UTF_BIGENDIAN;
              fmt++;
              break;
            case '^':                           /* XML-safe only */
              utf |= UTF_XMLSAFE;
              fmt++;
              break;
            case '|':                           /* bad UTF-8 seq. as ISO */
              utf |= UTF_BADENCASISO;
              fmt++;
              break;
            case '=':                           /* verify encoding */
              pflags |= PRF_VERIFY;
              fmt++;
              break;
	    case 'm':	/* Metamorph query and markup  -KNG 960116 */
              GETARG(mquery, HTPFT_STR, HTPFW_QUERY, char, char *, , mqgae);
              if (0)
                {
                mqgae:
                  if (errstr != fmt && errstr != CHARPN)
                    {
                      PUTS(errstr);
                    }
                }
	      fmt++;
	      for (okmflags = 1; fmt < fmte && okmflags; fmt += okmflags) {
		switch (*fmt) {
		  case 'b':  state.mFlags |= MF_BOLDHTML;	break;
		  case 'B':  state.mFlags |= MF_BOLDVT100;	break;
		  case 'U':  state.mFlags |= MF_UNDERLINEVT100;	break;
                  case 'R':  state.mFlags |= MF_REVERSEVT100; break;
		  case 'h':  state.mFlags |= MF_HREF;		break;
		  case 'n':  state.mFlags |= MF_NOTAGSKIP;	break;
                  case 'P':
                    if (pexpr == (char *)DefParaRex)
                      {
                        GETARG(pexpr, HTPFT_STR, HTPFW_PREX,
                               char, char *, , paraRexGae);
                        if (pexpr == CHARPN) break;
                      }
                    else
                      {
                        GETARG(pReplaceStr, HTPFT_STR, HTPFW_PREPLACESTR,
                               char, char *, , paraRexGae);
                        pReplaceStrSz = (pReplaceStr ? strlen(pReplaceStr):0);
                        if (pReplaceStr == CHARPN) break;
                      }
                    /* fall through */
		  case 'p':  state.mFlags |= MF_PARA;		break;
		  case 'c':  state.mFlags |= MF_CONTCOUNT;	break;
                  case 'N':  state.mFlags |= MF_NOT;          break;
                  case 'e':  state.mFlags |= MF_EXACT;        break;
                  case 'I':
#ifndef EPI_ENABLE_STYLE_HIGHLIGHTING
                    if (StyleHighlightingEnabled == -1)
                      StyleHighlightingEnabled =
                        (getenv("EPI_ENABLE_STYLE_HIGHLIGHTING") != CHARPN);
                    if (!StyleHighlightingEnabled) { okmflags = 0; break; }
#endif /* !EPI_ENABLE_STYLE_HIGHLIGHTING */
                    state.mFlags |= MF_INLINESTYLE;
                    break;
                  case 'C':
#ifndef EPI_ENABLE_STYLE_HIGHLIGHTING
                    if (StyleHighlightingEnabled == -1)
                      StyleHighlightingEnabled =
                        (getenv("EPI_ENABLE_STYLE_HIGHLIGHTING") != CHARPN);
                    if (!StyleHighlightingEnabled) { okmflags = 0; break; }
#endif /* !EPI_ENABLE_STYLE_HIGHLIGHTING */
                    state.mFlags |= MF_CLASS;
                    break;
                  case 'q':
                    /* Turn on MF_EXACT also, because we want APICP.setqoffs
                     * for the real query, not our `w/. @0'-modified one.
                     * Turn on MM_NOTAGSKIP because the query-as-search-text
                     * is not live HTML:
                     */
                    state.mFlags |= (MF_HILITEQUERY | MF_EXACT | MF_NOTAGSKIP);
                    break;
		  default:   okmflags = 0;		break;
                  paraRexGae:
                    if (errstr != fmt && errstr != CHARPN)
                      {
                        PUTS(errstr);
                      }
                    break;
		}
	      }
              if (mquery != CHARPN) state.mFlags |= MF_INUSE;
	      if (!(state.mFlags & (MF_BOLDHTML | MF_BOLDVT100 |
                                    MF_UNDERLINEVT100 | MF_REVERSEVT100 |
                                    MF_HREF | MF_INLINESTYLE | MF_CLASS)))
		state.mFlags |= MF_HREF;  /* default if no highlight flags */
	      break;
	    case 'a':	/* format arg for %t/%T/%L */
	      GETARG(tfmt, HTPFT_STR, HTPFW_TIMEFMT, char, char *, , flaggae);
              fmt++;
              if (tfmt != CHARPN)
                tfmte = tfmt + (sz != (size_t)(-1) ? sz : strlen(tfmt));
              else
                {
                  tfmt = (char *)NullStr;
                  tfmte = (char *)NullStr + NULLSTR_LEN;
                }
	      break;
            case 'k':   /* comma-separate thousands KNG 961115 */
              fmt++;
#ifdef EPI_HAVE_LOCALE_H
              if ((lc != LCONVPN || (lc = localeconv()) != LCONVPN) &&
                  lc->thousands_sep != CHARPN &&
                  *lc->thousands_sep != '\0')
                {
                  ksep = lc->thousands_sep;
                  ksepe = ksep + strlen(ksep);
                }
              else
#endif /* EPI_HAVE_LOCALE_H */
                {
                  ksep = ",";
                  ksepe = ksep + 1;
                }
              break;
            case 'K':   /* separate with next arg, every thousand KNG 961115 */
              GETARG(ksep, HTPFT_STR, HTPFW_KSTR, char, char *, , flaggae);
              fmt++;
              if (ksep != CHARPN)
                ksepe = ksep + (sz != (size_t)(-1) ? sz : strlen(ksep));
              break;
            case 'P':   /* paragraph markup with arg as REX expr KNG 970128 */
              if (pexpr == (char *)DefParaRex)
                {
                  GETARG(pexpr, HTPFT_STR, HTPFW_PREX, char,
                         char *, , flaggae);
                  fmt++;
                  if (pexpr != CHARPN) state.mFlags |= MF_PARA;
                }
              else
                {
                  GETARG(pReplaceStr, HTPFT_STR, HTPFW_PREPLACESTR, char,
                         char *, , flaggae);
                  fmt++;
                  pReplaceStrSz = (pReplaceStr ? strlen(pReplaceStr) : 0);
                  if (pReplaceStr != CHARPN) state.mFlags |= MF_PARA;
                }
              break;
            case 'q':   /* full URL encoding  KNG 981202 */
              pflags |= PRF_QUERYURL;
              utf |= UTF_QENCONLY;
              fmt++;
              break;
            case 'p':   /* paragraph markup (default REX expr)   KNG 970128 */
              /* 'p' as a flag conflicts with %p (pointer format).
               * Only take as flag if probable flag or code follows:
               */
              if (fmt + 1 < fmte && (TX_ISALPHA(fmt[1]) || fmt[1] == '!'))
                {
                  fmt++;
                  if (*fmt == 'U')
                    pflags |= PRF_PATHURL;
                  else
                    state.mFlags |= MF_PARA;
                  break;
                }
              /* else fall through */
	    default:
	      if (TX_ISDIGIT(*fmt)) {
                width = 0;                              /* reset KNG 970429 */
		while (fmt < fmte && TX_ISDIGIT(*fmt))
		  width = 10 * width + Ctod(*fmt++);
	      } else okflags = 0;
	  }
          continue;
        flaggae:                /* getarg() error */
          if (errstr != fmt++ && errstr != CHARPN)      /* also inc 970127 */
            {
              PUTS(errstr);
            }
	}
      }
	/* done with flags */

	isneg = 0;
	digs = HexLower;
#ifdef FLOATING
	echar = 'e';
#endif
#ifdef ROMAN
	rdigs = "  mdclxvi";
#endif

        if (fmt >= fmte)                        /* Bug 6231: no format code */
          goto unknownOrMissingCode;

	switch(*fmt)
		{
#ifdef BINARY
		case 'b':
			base = 2;
                        if (pflags & PRF_HUGE)
                          {
                            GETARG(hui, HTPFT_HUGEUINT, HTPFW_BINARY,
                                   EPI_HUGEUINT, EPI_HUGEUINT, *, fmtgae);
			    goto dohugeint;
			  }
#ifndef NOLONG
#  ifdef EPI_HAVE_UNSIGNED_LONG_LONG
                        if (pflags & PRF_LONGLONG)
                          {
                            GETARG(ull, HTPFT_ULONGLONG, HTPFW_BINARY,
                                   unsigned long long, unsigned long long, *,
                                   fmtgae);
			    goto dolonglong;
			  }
#  endif /* EPI_HAVE_UNSIGNED_LONG_LONG */
			if (pflags & PRF_LONG)
			  {
			    GETARG(ul, HTPFT_ULONG, HTPFW_BINARY,
                                   ulong, ulong, *, fmtgae);
			    goto dolong;
			  }
#endif	/* !NOLONG */
			GETARG(u, HTPFT_UNSIGNED, HTPFW_BINARY,
                               unsigned, unsigned, *, fmtgae);
			goto donum;
#endif	/* BINARY */
		case 'c':
		  GETARG(n, HTPFT_CHAR, ((pflags & PRF_DECODE) ?
                         HTPFW_DECODECHAR : HTPFW_CHAR), char, int, *,fmtgae);
		  if (sz != 0)
                    {
                      if (pflags & PRF_DECODE)
                        {
                          base = 10;
                          u = (n & 0xff);
                          goto donum;
                        }
                      PUTC(n);
                    }
		  break;

		case 'd':
		case 'i':
			base = 10;
                        if (pflags & PRF_HUGE)
                          {
                            GETARG(hi, HTPFT_HUGEINT, HTPFW_INT, EPI_HUGEINT,
                                   EPI_HUGEINT, *, fmtgae);
                            if (hi < (EPI_HUGEINT)0)
                              {
                                hui = (EPI_HUGEUINT)(-hi);
                                isneg = 1;
                              }
                            else hui = (EPI_HUGEUINT)hi;
			    goto dohugeint;
			  }
#ifndef NOLONG
#  ifdef EPI_HAVE_LONG_LONG
                        if (pflags & PRF_LONGLONG)
                          {
                            GETARG(ll, HTPFT_LONGLONG, HTPFW_INT, long long,
                                   long long, *, fmtgae);
                            if (ll < (long long)0)
                              {
                                ull = (unsigned long long)(-ll);
                                isneg = 1;
                              }
                            else ull = (unsigned long long)ll;
			    goto dolonglong;
			  }
#  endif /* EPI_HAVE_LONG_LONG */
			if(pflags & PRF_LONG)
			  {
			    GETARG(l, HTPFT_LONG, HTPFW_INT, long, long,
                                   *, fmtgae);
			    if(l < 0L)
			      {
				ul = (ulong)(-l);
				isneg = 1;
			      }
			    else	ul = (ulong)l;
			    goto dolong;
			  }
#endif	/* !NOLONG */
			GETARG(n, HTPFT_INT, HTPFW_INT, int, int, *, fmtgae);
			if(n >= 0)
				u = n;
			else	{
				u = -n;
				isneg = 1;
				}

			goto donum;
#ifdef FLOATING
		case 'E':
			echar = 'E';
		case 'e':
#ifdef EPI_HAVE_LOCALE_H
                        if ((lc != LCONVPN ||
                             (lc = localeconv()) != LCONVPN) &&
                            lc->decimal_point != CHARPN &&
                            *lc->decimal_point != '\0')
                          decch = *lc->decimal_point;
#endif /* EPI_HAVE_LOCALE_H */
			if(prec == -1)
				prec = 6;
                        decpt = 1;
                        isneg = negexp = 0;             /* KNG 970112 */

#  ifdef EPI_HAVE_LONG_DOUBLE
                        if (pflags & PRF_LONGLONG)
                          {
                            GETARG(longDouble, HTPFT_LONGDOUBLE, HTPFW_E,
                                   long double, long double, *, fmtgae);
                            hugeFloat = (EPI_HUGEFLOAT)longDouble;
                            goto afterGetArgHugeE;
                          }
#  endif /* EPI_HAVE_LONG_DOUBLE */

                        if (pflags & PRF_HUGE)
                          {
                            GETARG(hugeFloat, HTPFT_HUGEFLOAT, HTPFW_E,
                                   EPI_HUGEFLOAT, EPI_HUGEFLOAT, *, fmtgae);
#  ifdef EPI_HAVE_LONG_DOUBLE
                          afterGetArgHugeE:
#  endif /* EPI_HAVE_LONG_DOUBLE */
                            /* Consistent string for NaN, Inf, -Inf: */
                            if (TXHUGEFLOAT_IS_NaN(hugeFloat)) goto fNaN;
                            if (TXHUGEFLOAT_IS_PosInf(hugeFloat)) goto finf2;
                            if (TXHUGEFLOAT_IS_NegInf(hugeFloat))
                              {
                                isneg = 1;
                                goto finf2;
                              }
#  ifdef USE_SNPRINTF_NOT_CVT
#    ifdef EPI_HAVE_LONG_DOUBLE
                            snprintf(cvtbuf, sizeof(cvtbuf), "%1.*Le",
                                     prec, (long double)hugeFloat);
#    else /* !EPI_HAVE_LONG_DOUBLE */
                            snprintf(cvtbuf, sizeof(cvtbuf), "%1.*e",
                                     prec, (double)hugeFloat);
#    endif /* !EPI_HAVE_LONG_DOUBLE */
                            cvtbuf[sizeof(cvtbuf)-1] = '\0';/* just in case */
                            d = (hugeFloat < (EPI_HUGEFLOAT)0.0 ? -1 : 0);
                            goto useSnprintfNotCvtE;
#  else /* !USE_SNPRINTF_NOT_CVT */
#    ifdef EPI_HAVE_QECVT
                            p2 = qecvt((long double)hugeFloat, prec + 1,
                                       &decpt, &isneg);
#    elif defined(EPI_HAVE_ECVT)
                            p2 = ecvt((double)hugeFloat, prec + 1,
                                      &decpt, &isneg);
#    else
                            error;
#    endif /* !EPI_HAVE_QECVT && !EPI_HAVE_ECVT */
                            n = (int)strlen(p2);
                            goto afterCvtE;
#  endif /* !USE_SNPRINTF_NOT_CVT */
                          }

		        GETARG(d, HTPFT_DOUBLE, HTPFW_E, double, double, *,
                               fmtgae);
                        /* Consistent string for NaN, Inf, -Inf: */
                        if (TXDOUBLE_IS_NaN(d)) goto fNaN;
                        if (TXDOUBLE_IS_PosInf(d)) goto finf2;
                        if (TXDOUBLE_IS_NegInf(d))
                          {
                            isneg = 1;
                            goto finf2;
                          }
#ifdef USE_SNPRINTF_NOT_CVT
                        snprintf(cvtbuf, sizeof(cvtbuf), "%1.*e",
                                 prec, d);
                        cvtbuf[sizeof(cvtbuf)-1] = '\0';/* just in case */
                useSnprintfNotCvtE:
                        p2 = cvtbuf;
                        n = strlen(p2);
                        /* parse and strip negative sign first: */
                        if (*p2 == '-')
                          {
                            isneg = 1;
                            memmove(p2, p2 + 1, n--);
                          }
                        else
                          isneg = (d < (double)0.0);
                        /* parse and strip decimal point next: */
                        pe = strchr(p2, '.');
                        if (pe != CHARPN)
                          {
                            decpt = pe - p2;
                            memmove(pe, pe + 1, n-- - decpt);
                          }
                        else
                          decpt = 1;            /* WAG */
                        /* parse and remove trailing exponent: */
                        for (pe = p2; *pe && *pe != 'e' && *pe != 'E'; pe++);
                        if (*pe)                /* found exponent */
                          {
                            decpt += atoi(pe + 1);
                            *pe = '\0';         /* remove exponent */
                            n = pe - p2;
                          }
#else /* !USE_SNPRINTF_NOT_CVT */
			p2 = ecvt(d, prec + 1, &decpt, &isneg);
                        n = (int)strlen(p2);
                afterCvtE:
#endif /* !USE_SNPRINTF_NOT_CVT */
                        if (*p2 == '-')                 /* -Infinity */
                          {
                            isneg = 1;
                            p2++;
                            n--;
                          }
                        p3 = p2 + n;
                        /* for exponent: */
                        n += (int)(EPI_OS_INT_BITS/3 + 10 + padCharBufSz);
                        ALLOCBUF(n);
                        p = bufend - 1;
                        if (*p2 == 'I' || *p2 == 'i')   /* Infinity */
                          {
                            p2 = (char *)InfStr;
                            p3 = p2 + INFSTR_LEN;
                            goto emantissa;
                          }
                        else if (--decpt < 0)
                          {
                            negexp = TRUE;
                            decpt = -decpt;
                          }

                        for ( ; (decpt > 0 || bufend - p < 3); )
                          {
                            *p-- = digs[decpt % 10];
                            decpt /= 10;
                          }

			if(negexp)
				*p-- = '-';
			else	*p-- = '+';
			*p-- = echar;
			if(prec == 0 && (pflags & PRF_NUMSGN))
				*p-- = decch;
                emantissa:
			while(p3 > p2)
				{
				*p-- = *--p3;

				if(p3 - p2 == 1 && *p2 != 'I' && *p2 != 'i')
					*p-- = decch;
				}
                        if (p[1] == 'i') p[1] = 'I';
			goto putnum;

		case 'f':
#ifdef EPI_HAVE_LOCALE_H
                        if ((lc != LCONVPN ||
                             (lc = localeconv()) != LCONVPN) &&
                            lc->decimal_point != CHARPN &&
                            *lc->decimal_point != '\0')
                          decch = *lc->decimal_point;
#endif /* EPI_HAVE_LOCALE_H */
			if(prec == -1)
				prec = 6;

                        /* Clear flags; aren't set if infinite:  KNG 970112 */
                        decpt = isneg = 0;

#  ifdef EPI_HAVE_LONG_DOUBLE
                        if (pflags & PRF_LONGLONG)
                          {
                            GETARG(longDouble, HTPFT_LONGDOUBLE, HTPFW_F,
                                   long double, long double, *, fmtgae);
                            hugeFloat = (EPI_HUGEFLOAT)longDouble;
                            goto afterGetArgHugeF;
                          }
#  endif /* EPI_HAVE_LONG_DOUBLE */

                        if (pflags & PRF_HUGE)
                          {
                            GETARG(hugeFloat, HTPFT_HUGEFLOAT, HTPFW_F,
                                   EPI_HUGEFLOAT, EPI_HUGEFLOAT, *, fmtgae);
#  ifdef EPI_HAVE_LONG_DOUBLE
                          afterGetArgHugeF:
#  endif /* EPI_HAVE_LONG_DOUBLE */
                            /* Use a consistent string (`NaN') for NaN;
                             * some platforms  give `nan' etc.:
                             */
                            if (TXHUGEFLOAT_IS_NaN(hugeFloat))
                              p2 = (char *)NaNStr;
                            else if (TXHUGEFLOAT_IS_PosInf(hugeFloat))
                              p2 = (char *)InfStr;
                            else if (TXHUGEFLOAT_IS_NegInf(hugeFloat))
                              {
                                p2 = (char *)InfStr;
                                isneg = 1;
                              }
                            else
#  ifdef USE_SNPRINTF_NOT_CVT
                              {
#    ifdef EPI_HAVE_LONG_DOUBLE
                                snprintf(cvtbuf, sizeof(cvtbuf), "%1.*Lf",
                                         prec, (long double)hugeFloat);
#    else /* !EPI_HAVE_LONG_DOUBLE */
                                snprintf(cvtbuf, sizeof(cvtbuf), "%1.*f",
                                         prec, (double)hugeFloat);
#    endif /* !EPI_HAVE_LONG_DOUBLE */
                                cvtbuf[sizeof(cvtbuf)-1] = '\0'; /* in case */
                                d = (hugeFloat < (EPI_HUGEFLOAT)0.0 ? -1 : 0);
                                goto useSnprintfNotCvtF;
                              }
#  else /* !USE_SNPRINTF_NOT_CVT */
#    ifdef EPI_HAVE_QFCVT
                            p2 = qfcvt((long double)hugeFloat, prec, &decpt,
                                       &isneg);
#    elif defined(EPI_HAVE_FCVT)
                            p2 = fcvt((double)hugeFloat, prec, &decpt,
                                      &isneg);
#    else
                            error;
#    endif /* !EPI_HAVE_QFCVT && !EPI_HAVE_FCVT */
#  endif /* !USE_SNPRINTF_NOT_CVT */
                            goto afterCvtF;
                          }

		        GETARG(d, HTPFT_DOUBLE, HTPFW_F, double, double, *,
                               fmtgae);
                        /* Use a consistent string (`NaN') for NaN;
                         * some platforms  give `nan' etc.:
                         */
                        if (TXDOUBLE_IS_NaN(d))
                          p2 = (char *)NaNStr;
                        else
#if defined(__APPLE__) && defined(__MACH__)
                        /* WTF fcvt(INF) throws an exception, and we don't
                         * know how to catch it yet.  Check for this first:
                         */
#endif /* __APPLE__ && __MACH__ */
                        /* KNG 20071105 consistent `Inf' all platforms too:*/
                        if (TXDOUBLE_IS_PosInf(d))
                          p2 = (char *)InfStr;
                        else if (TXDOUBLE_IS_NegInf(d))
                          {
                            p2 = (char *)InfStr;
                            isneg = 1;
                          }
                        else
#ifdef USE_SNPRINTF_NOT_CVT
                          {
                            snprintf(cvtbuf, sizeof(cvtbuf), "%1.*f",
                                     prec, d);
                            cvtbuf[sizeof(cvtbuf)-1] = '\0';/* just in case */
                          useSnprintfNotCvtF:
                            p2 = cvtbuf;
                            n = strlen(cvtbuf);
                            pe = strchr(p2, '.');
                            if (pe != CHARPN)
                              {
                                decpt = pe - p2;
                                /* remove the decimal point: */
                                memmove(pe, pe + 1, n - decpt);
                              }
                            else
                              decpt = n;        /* WAG */
                            isneg = (d < (double)0.0);
                          }
#else /* !USE_SNPRINTF_NOT_CVT */
                          p2 = fcvt(d, prec
#  if defined(__sgi) && EPI_OS_MAJOR >= 5 /* 5 and 6.5 at leat */
                                  /* KNG 010515 %1.2f of 0 was 0.0, not 0.00*/
                                    + 1
#  endif /* __sgi && EPI_OS_MAJOR >= 5 */
                                    , &decpt, &isneg);
#endif /* !USE_SNPRINTF_NOT_CVT */
                afterCvtF:
                        if (*p2 == '-')         /* -Infinity */
                          {
                            isneg = 1;          /* ensure it's negative */
                            p2++;               /* strip leading `-' */
                            decpt--;            /* "" */
                          }
                        /* Compute output buffer size and allocate buffer: */
                        n = (int)strlen(p2);
                        p3 = p2 + n;            /* end of src str */
                        /* KNG 20070116 some platforms (mips-sgi-irix6.5)
                         * can truncate large numbers in fcvt(), leaving
                         * the decimal point past end of buffer:
                         */
                        if (decpt + prec > n) n = decpt + prec;
                        n += 4;                 /* "-0." + '\0' */
                        if (decpt > 0) n += (int)((ksepe - ksep)*(decpt/3));
                        else n += -decpt;       /* .000000123... */
                        n += (int)padCharBufSz;
                        ALLOCBUF(n);
                finf:
                        /* Deal with `Inf[inity]': */
                        p = bufend - 1;         /* last char in output buf */
                        if (*p2 == 'I' || *p2 == 'i')   /* Infinity */
                          {
                          finf2:
                            p = bufend - 1;             /* in case not set */
                            p2 = (char *)InfStr;        /* switch to `Inf' */
                            p3 = p2 + INFSTR_LEN;
                            for ( ; p3 > p2; )          /* copy to out buf */
                              *(p--) = *(--p3);
                            goto putnum;
                          }
                        /* Deal with `NaN': */
                        if (*p2 == 'N' || *p2 == 'n')   /* NaN */
                          {
                          fNaN:
                            p = bufend - 1;             /* in case not set */
                            p2 = (char *)NaNStr;        /* switch to `NaN' */
                            p3 = p2 + NANSTR_LEN;
                            for ( ; p3 > p2; )          /* copy to out buf */
                              *(p--) = *(--p3);
                            goto putnum;
                          }

			if(prec == 0 && (pflags & PRF_NUMSGN))
                          *p-- = decch;         /* terminating decimal */

                        pd = p2 + decpt;        /* source-buf decimal loc. */
                        /* fcvt() may give `prec' significant digits; we want
                         * `prec' from the _decimal_ point; truncate end of
                         * source buf if too much given:  KNG 970429
                         */
                        if (p3 > pd + prec) p3 = pd + prec;     /* trunc src*/
                        /* Decimal point (and following zeros) may be
                         * beyond end of buffer for large numbers
                         * on some platforms:  20070116
                         */
                        pe = (pd + prec > p3 ? pd + prec : p3);
                        /* Copy source to output buffer, inserting decimal
                         * and thousands separator(s):
                         */
                        for (i = decpt - (int)(pe - p2); pe > p2; i++)
                          {
                            if (ksep != CHARPN && i > 0 && !(i % 3))
                              {                 /* insert 1000s separator */
                                for (pts = ksepe; pts > ksep; )
                                  *(p--) = *(--pts);
                              }
                            /* Copy next source digit.  Note that it might
                             * be beyond end of buffer:
                             */
                            --pe;
                            ch = (pe >= p3 ? '0' : *pe);
                            *(p--) = ch;
                            /* Insert decimal point, if reached: */
                            if (pe == pd) *(p--) = decch;
                          }
                        /* 970429 add first 0's before decimal if needed: */
                        if (decpt < 0)
                          {
                            if (-decpt > prec) decpt = -prec;
                            for ( ; decpt < 0; decpt++) *(p--) = '0';
                            if (prec > 0) *(p--) = decch;
                          }
                        /* 970429 at least 1 digit to left of decimal: */
                        if (decpt == 0) *(p--) = '0';
                        goto putnum;

		case 'G':
			echar = 'E';
		case 'g':
#ifdef EPI_HAVE_LOCALE_H
                        if ((lc != LCONVPN ||
                             (lc = localeconv()) != LCONVPN) &&
                            lc->decimal_point != CHARPN &&
                            *lc->decimal_point != '\0')
                          decch = *lc->decimal_point;
#endif /* EPI_HAVE_LOCALE_H */
                        if (prec == -1) prec = 6;
                        /* wtf guess at size: */
                        n = (int)(prec + EPI_OS_INT_BITS/3 + 10 +
                                  (ksepe - ksep)*(prec/3) + padCharBufSz);
                        ALLOCBUF(n);
                        isneg = 0;

#  ifdef EPI_HAVE_LONG_DOUBLE
                        if (pflags & PRF_LONGLONG)
                          {
                            GETARG(longDouble, HTPFT_LONGDOUBLE, HTPFW_G,
                                   long double, long double, *, fmtgae);
                            hugeFloat = (EPI_HUGEFLOAT)longDouble;
                            goto afterGetArgHugeG;
                          }
#  endif /* EPI_HAVE_LONG_DOUBLE */

                        if (pflags & PRF_HUGE)
                          {
                            GETARG(hugeFloat, HTPFT_HUGEFLOAT, HTPFW_G,
                                   EPI_HUGEFLOAT, EPI_HUGEFLOAT, *, fmtgae);
#  ifdef EPI_HAVE_LONG_DOUBLE
                          afterGetArgHugeG:
#  endif /* EPI_HAVE_LONG_DOUBLE */
                            /* Consistent string for NaN, Inf, -Inf: */
                            if (TXHUGEFLOAT_IS_NaN(hugeFloat)) goto fNaN;
                            if (TXHUGEFLOAT_IS_PosInf(hugeFloat))goto finf2;
                            if (TXHUGEFLOAT_IS_NegInf(hugeFloat))
                              {
                                isneg = 1;
                                goto finf2;
                              }
#  ifdef USE_SNPRINTF_NOT_CVT
#    ifdef EPI_HAVE_LONG_DOUBLE
                            snprintf(buf, n, "%1.*Lg", prec,
                                     (long double)hugeFloat);
#    else /* !EPI_HAVE_LONG_DOUBLE */
                            snprintf(buf, n, "%1.*g", prec,
                                     (double)hugeFloat);
#    endif /* !EPI_HAVE_LONG_DOUBLE */
                            buf[n - 1] = '\0';      /* just in case */
#  else /* !USE_SNPRINTF_NOT_CVT */
#    ifdef EPI_HAVE_QGCVT
                            qgcvt((long double)hugeFloat, prec, buf);
#    elif defined(EPI_HAVE_GCVT)
                            HTPF_GCVT((double)hugeFloat, prec, buf);
#    else
                            error;
#    endif /* !EPI_HAVE_QGCVT && !EPI_HAVE_GCVT */
#  endif /* !USE_SNPRINTF_NOT_CVT */
                            d = (hugeFloat < (EPI_HUGEFLOAT)0.0 ? -1 : 0);
                            goto afterCvtG;
                          }

		        GETARG(d, HTPFT_DOUBLE, HTPFW_G, double, double, *,
                               fmtgae);
                        /* Consistent string for NaN, Inf, -Inf: */
                        if (TXDOUBLE_IS_NaN(d)) goto fNaN;
                        if (TXDOUBLE_IS_PosInf(d)) goto finf2;
                        if (TXDOUBLE_IS_NegInf(d))
                          {
                            isneg = 1;
                            goto finf2;
                          }
#ifdef USE_SNPRINTF_NOT_CVT
                        snprintf(buf, n, "%1.*g", prec, d);
                        buf[n - 1] = '\0';      /* just in case */
#else /* !USE_SNPRINTF_NOT_CVT */
                        HTPF_GCVT(d, prec, buf);
#endif /* !USE_SNPRINTF_NOT_CVT */
                afterCvtG:
                        /* Check for infinity: */
                        p2 = buf;
#ifndef linux
                        if (*p2 == '-')
                          {
                            isneg = 1;
                            p2++;
                          }
                        if (*p2 == 'I' || *p2 == 'i')   /* Infinity */
                          goto finf;
#endif  /* !linux */
                        if (*p2 == 'N' || *p2 == 'n')   /* NaN */
                          goto fNaN;
                        for (p = p2 = buf, i = 0; *p != '\0'; p++)
                          {
                            switch (*p)
                              {
                              case '.':
                                switch (p[1])   /* zap trailing . KNG 000106*/
                                  {
                                  case '\0':
                                  case 'e':
                                  case 'E':  continue;
                                  default:   i = 1;
                                  }
                                break;
                              case 'e':
                              case 'E':
                                ksep = ksepe = CHARPN;
                                break;
                              case '+':
                              case '-':
                                if (p[1] == '0')
                                  {
                                    if (p[2] == '0')
                                      {         /* 1e+005 -> 1e+05   010806 */
                                        *(p2++) = *(p++);
                                        *(p2++) = *(p++);
                                        while (*p == '0') p++;
                                      }
                                  }
                                else if (p[1] > '0' && p[1] <= '9' &&
                                         p[2] == '\0' && p > buf &&
                                         (p[-1] == 'e' || p[-1] == 'E'))
                                  {             /* 1e+5 -> 1e+05 KNG 000106 */
                                    p[3] = '\0';
                                    p[2] = p[1];
                                    p[1] = '0';
                                  }
                                break;
#ifdef linux
                              case 'I':
                              case 'i':
                                /* gcvt() is broken under Linux;
                                 * returns "Infinity" w/garbage:   -KNG 970112
                                 */
                                p2 = (char *)InfStr;
                                p3 = p2 + INFSTR_LEN;
                                isneg = (d < 0.0);
                                goto finf;
                                break;
#endif /* linux */
                              }
                            *(p2++) = *p;
                          }
                        *p2 = '\0';
                        p = p2;
                        i = (i ? -2*(int)bufsz : 0);
                        for (p2 = p - 1, p = bufend - 1; p2 >= buf; p2--, i++)
				{
                                if (ksep != CHARPN && i > 0 &&
                                    !(i % 3) && *p2 != '-')
                                  {
                                    for (pts = ksepe; pts > ksep; )
                                      *(p--) = *(--pts);
                                  }
				if(*p2 == 'e' || *p2 == 'E')
					*p-- = echar;
				else	*p-- = *p2;
                                if (*p2 == '.')
                                  {
                                    *p2 = decch;
                                    i = -1;
                                  }
				}
                        switch (p[1])
                          {
                          case '-':
                            isneg = 1;
                            /* fall through */
                          case '+':
                            p++;
                            break;
                          }
                        if (p[1] == decch) *(p--) = '0';  /* .1 => 0.1 */
			goto putnum;
#endif	/* FLOATING */

		case 'n':
                  GETARG(ptr, HTPFT_INTPTR, HTPFW_NPRINTED, int,int*,,fmtgae);
                  if (ptr != NULL) *(int *)ptr = (int)nprinted;
		  break;
		case 'o':
			base = 8;
                        if (pflags & PRF_HUGE)
                          {
                            GETARG(hui, HTPFT_HUGEUINT, HTPFW_OCTAL,
                                   EPI_HUGEUINT, EPI_HUGEUINT, *, fmtgae);
			    goto dohugeint;
			  }
#ifndef NOLONG
#  ifdef EPI_HAVE_UNSIGNED_LONG_LONG
                        if (pflags & PRF_LONGLONG)
                          {
                            GETARG(ull, HTPFT_ULONGLONG, HTPFW_OCTAL,
                                   unsigned long long, unsigned long long,
                                   *, fmtgae);
			    goto dolonglong;
			  }
#  endif /* EPI_HAVE_UNSIGNED_LONG_LONG */
			if(pflags & PRF_LONG)
			  {
			    GETARG(ul, HTPFT_ULONG, HTPFW_OCTAL, ulong,
                                   ulong, *, fmtgae);
			    goto dolong;
			  }
#endif	/* !NOLONG */
			GETARG(u, HTPFT_UNSIGNED, HTPFW_OCTAL, unsigned,
                               unsigned, *, fmtgae);
			goto donum;

		case 'p':
                  vp = NULL;                            /* avoid warning */
                  GETARG(vp, HTPFT_PTR, HTPFW_PTR, void, void*, , pointerErr);
                  goto pointerCont;
                pointerErr:
                  /* Unlike all other types, a NULL getarg() return for
                   * HTPFT_PTR could be ok.  Disambiguate NULL pointer
                   * from true error by checking `errstr'; wtf hack:
                   */
                  if (getarg == HTPFCBPN || errstr != fmt) goto fmtgae;
                pointerCont:
                  digs = HexLower;
                        p = bufend - 1;                 /* buf is big enough*/
#if EPI_OS_VOIDPTR_BITS > EPI_OS_ULONG_BITS
#  if EPI_OS_VOIDPTR_BITS > EPI_HUGEUINT_BITS
                        error error error; EPI_HUGEUINT not large enough;
#  else /* EPI_OS_VOIDPTR_BITS <= EPI_HUGEUINT_BITS */
                        hui = (EPI_HUGEUINT)vp;
#  endif /* EPI_OS_VOIDPTR_BITS <= EPI_HUGEUINT_BITS */
#  define NN    hui
#  define TT    EPI_HUGEUINT
#else /* EPI_OS_VOIDPTR_BITS <= EPI_OS_ULONG_BITS */
                        ul = (ulong)vp;
#  define NN    ul
#  define TT    ulong
#endif /* EPI_OS_VOIDPTR_BITS <= EPI_OS_ULONG_BITS */
                        do
                          {
                            *p-- = digs[NN & (TT)0xF];
                            NN >>= 4;
                          }
                        while (NN);
#undef NN
#undef TT
                        /* Always give `0x' prefix, even if value is 0
                         * (unlike %#x): reminds us it is a pointer:
                         */
                        *(p--) = 'x';
                        *(p--) = '0';
                        signflag = '-';                 /* ptrs aren't signed*/
                        goto putnum;
#ifdef ROMAN
		case 'R':
			rdigs = "  MDCLXVI";
		case 'r':
                  if (pflags & PRF_HUGE)
                    {
                      GETARG(hi, HTPFT_HUGEINT, HTPFW_ROMAN, EPI_HUGEINT,
                             EPI_HUGEINT, *, fmtgae);
                      l = (long)hi;     /* WTF WTF */
                    }
                  else
#ifndef NOLONG
#  ifdef EPI_HAVE_LONG_LONG
                  if (pflags & PRF_LONGLONG)
                    {
                      GETARG(ll, HTPFT_LONGLONG, HTPFW_ROMAN,long long,
                             long long, *, fmtgae);
                      l = (long)ll;     /* WTF WTF */
                    }
                  else
#  endif /* EPI_HAVE_LONG_LONG */
                  if (pflags & PRF_LONG)
                    {
                      GETARG(l, HTPFT_LONG, HTPFW_ROMAN, long, long, *,
                             fmtgae);
                    }
                  else
#endif	/* !NOLONG */
                    {
                      GETARG(n, HTPFT_INT, HTPFW_ROMAN, int, int, *, fmtgae);
                      l = (long)n;
                    }
                  if (l < 0L)
                    {
                      ul = (ulong)(-l);
                      isneg = 1;
                    }
                  else
                    ul = (ulong)l;

                  n = (int)(ul/1000L + 30 + padCharBufSz); /* wtf may get big*/
                  ALLOCBUF(n);
                  p2 = bufend - 1;
                  n = (int)(ul % 1000L);
                  for (i = 6; i >= 0; i -= 2)
                    {
                      dig = n % 10;
                      tack(dig, rdigs + i, &p2);
                      n /= 10;
                    }
                  /* Tack on the M's for thousands (one per thousand): */
                  for (n = ul / 1000L; (n > 0) && (p2 > buf + 1); n--, p2--)
                    *p2 = rdigs[2];
                  if (n > 0) *(p2--) = 'o';     /* overflow */
                  p = p2;
                  pflags |= PRF_SPACEPAD;
                  padCharBuf[0] = ' ';
                  padCharBufSz = 1;
                  goto putnum;
#endif	/* ROMAN */

                case 'V':       /* UTF-8 encoding */            /* KNG */
                  if (pflags & PRF_VERIFY)
                    transfunc = htutf8_to_utf8;
                  else if (pflags & PRF_DECODE)
                    transfunc = htutf8_to_iso88591;
                  else
                    {
                      transfunc = htiso88591_to_utf8;
                      if (utf & UTF_ESCRANGE)
                        {
                          utf &= ~UTF_ESCRANGE;
                          utf |= UTF_HTMLDECODE;
                        }
                    }
                  width = 0;    /* disable space pad */
                  goto dostr;
                case 'v':       /* UTF-16 encoding */
                  if (pflags & PRF_DECODE)
                    {
                      transfunc = htutf16_to_utf8;
                      if (utf & UTF_ESCRANGE)
                        {
                          utf &= ~UTF_ESCRANGE;
                          utf |= UTF_HTMLDECODE;
                        }
                    }
                  else
                    transfunc = htutf8_to_utf16;
                  width = 0;    /* disable space-pad: `_' is overridden */
                  goto dostr;
                case 'Q':       /* Quoted-printable encoding */
                  transfunc = (pflags & PRF_DECODE) ?
                    htquotedprintable_to_iso88591 :
                    htiso88591_to_quotedprintable;
                  bwidth = width;
                  if (pflags & PRF_LADJUST) utf |= UTF_BINARY;
                  width = 0;    /* disable space pad */
                  goto dostr;
                case 'B':       /* base64 encoding */           /* KNG */
                  transfunc=(pflags&PRF_DECODE)?htdecodebase64:htencodebase64;
                  bwidth = width;
                  if (pflags & PRF_LADJUST) utf |= UTF_BINARY;
                  width = 0;    /* disable space pad */
                  goto dostr;
                case 'W':       /* RFC 2047 encoded-word format */
                  if (pflags & PRF_DECODE)
                    {
                      transfunc = TXencodedWordToUtf8;
                      if (utf & UTF_ESCRANGE)
                        {
                          utf &= ~UTF_ESCRANGE;
                          utf |= UTF_HTMLDECODE;
                        }
                    }
                  else
                    transfunc = TXutf8ToEncodedWord;
                  bwidth = width;
                  if (pflags & PRF_LADJUST) utf |= UTF_BINARY;
                  width = 0;    /* disable space pad */
                  goto dostr;
                case 'z':       /* gzip */
                  GETARG(p, HTPFT_STR, HTPFW_STR, char, char *, , fmtgae);
                  if (sz == (size_t)(-1)) sz = strlen(p);
                  if (pflags & PRF_LONGLONG)
                    zlibFormat = TXZLIBFORMAT_RAWDEFLATE;
                  else if (pflags & PRF_LONG)
                    zlibFormat = TXZLIBFORMAT_ZLIBDEFLATE;
                  else
                    zlibFormat = ((pflags & PRF_DECODE) ?
                                  TXZLIBFORMAT_ANY : TXZLIBFORMAT_GZIP);
                  nprinted += TXhtpfDoCompression(pmbuf, out, outdata,
                                   (size_t)prec, (byte *)p, sz, zlibFormat,
                                                  !!(pflags & PRF_DECODE),
                                                  (fmtcp && fmtcp->htpfobj ?
                                                 fmtcp->htpfobj->traceEncoding :
                                                   HtpfTraceEncoding));
                  break;
		case 'H':	/* HTML string encoding */	/* KNG */
                  utf |= UTF_HTMLDECODE;
                  utf &= ~(UTF_ESCLOW | UTF_ESCRANGE);
                  if (!(pflags & PRF_LONG)) utf |= UTF_DO8BIT;  /*for decode*/
                  /* fall through */
		case 's':	/* string */
		case 'U':	/* URL string encoding */	/* KNG */
                dostr:
		  sch = *fmt;
		  GETARG(p, HTPFT_STR, ((state.mFlags & MF_INUSE) ? HTPFW_QUERYBUF:
                                        HTPFW_STR), char, char *, , fmtgae);
                  spanDepth = 0;
                  if (state.mFlags & MF_HILITEQUERY)  /* use query as text */
                    {
                      p = mquery;
                      sz = (size_t)(-1);
                    }
		  if (p == CHARPN)
                    {
                      p = (char *)NullStr;
                      sz = NULLSTR_LEN;
                    }
                  if (!(pflags & PRF_SPACEPAD))         /* no zero padding? */
                    {
                      pflags |= PRF_SPACEPAD;
                      padCharBuf[0] = ' ';
                      padCharBufSz = 1;
                    }
                  if ((state.mFlags & MF_INUSE) && prec != -1)
                    prec += width;                      /* width is buf start*/
                  /* Compute size if unknown, and limit to precision: */
                  if (sz == (size_t)(-1))               /* size unknown */
                    {
                      if (prec == -1)
                        sz = strlen(p);
                      else
                        {
                          for (p3= p+prec, p2=p; p2 < p3 && *p2 != '\0'; p2++);
                          sz = p2 - p;
                          if (p2 >= p3) overflowedPrecision = 1;
                        }
                    }
                  else if (prec != -1 && sz > (size_t)prec)
                    {
                      sz = prec;                /* limit to precision */
                      overflowedPrecision = 1;
                    }
                  pend = p + sz;
                  if (overflowedPrecision && signflag == '+')
                    {
                      /* Will replace last 3 chars with ellipsis.  Leave `sz'
                       * alone for `width' right-justification computations:
                       */
                      if (pend >= p + 3) pend -= 3;
                      else pend = p;
                    }
                  dtot = 0;
                  if (state.mFlags & MF_INUSE)  /* width is buf start */
                    {
                      p += width;
                      if (p > pend) p = pend;
                    }
                  else if (width > 0 && !(pflags & PRF_LADJUST))
                    {
                      size_t    widthDone, widthTodo;
                      char      *s, *d;

                      for (widthDone = sz;
                           widthDone < (size_t)width;
                           widthDone += widthTodo)
                        {
                          widthTodo = (width - widthDone)*padCharBufSz;
                          if (widthTodo > sizeof(hbuf))
                            widthTodo = sizeof(hbuf);
                          /* Make `widthTodo' an integral multiple of pad: */
                          widthTodo = (widthTodo/padCharBufSz)*padCharBufSz;
                          /* Copy integral multiple of padding to `hbuf': */
                          for (d = hbuf; d < hbuf + widthTodo; )
                            for (s = padCharBuf;
                                 s < padCharBuf + padCharBufSz; )
                              *(d++) = *(s++);
                          PUTSZ(hbuf, widthTodo);
                          widthTodo /= padCharBufSz; /* bytes to width-chars*/
                        }
                    }
		  /* don't bother skipping <> if HTML: they're escaped: */
		  if (sch == 'H') state.mFlags |= MF_NOTAGSKIP;
		  curmmcnt = nextmmcnt = 0;
                  nextEvent = pend + 1;
                  state.curSetSameAsOverallHit = TXbool_False;
		  if (!(state.mFlags & MF_INUSE) ||
                      (mm = open_mm(fmtcp, fs, mquery, p,pend,state.mFlags)) ==
                      MMPN || (curmmcnt = mm_next(mm, &curSetHitStart,
                                                  &curSetHitEnd,
                      &curSetOrpos, &state.curSetPmtype, TXLOGITYPEPN,
                      &curOverallHitStartOrg, &curOverallHitEndOrg),
                               !curSetHitStart))
                    {
                      /* No hits: make sure we never run into them: */
                      curSetHitStart = curSetHitEnd = curOverallHitStart =
                        curOverallHitEnd = pend + 1;
                    }
                  else                          /* set next place to stop */
                    {
                      curOverallHitStart = curOverallHitStartOrg;
                      curOverallHitEnd = curOverallHitEndOrg;
                      /* Do not highlight overall query if no style/class: */
                      if (!(state.mFlags & (MF_INLINESTYLE | MF_CLASS)) ||
                          /* nor if w/doc: */
                          curOverallHitStartOrg == CHARPN ||
                          curOverallHitEndOrg == CHARPN)
                        curOverallHitStart = curOverallHitEnd = pend + 1;
                      nextEvent = TX_MIN(curSetHitStart, curOverallHitStart);
                      state.curSetSameAsOverallHit =
                        (curSetHitStart == curOverallHitStartOrg &&
                         curSetHitEnd == curOverallHitEndOrg);
                    }
		  nextSetHitStart = nextSetHitEnd = nextOverallHitStart =
                    nextOverallHitEnd = pend + 1;
                  nextSetSameAsOverallHit = TXbool_False;
                  if (!(state.mFlags & MF_PARA) ||
                      (paraRex = openrex((byte *)pexpr, TXrexSyntax_Rex))
                      == FFSPN ||
                      (nextPara = (char *)getrex(paraRex, (byte *)p,
                                                 (byte *)pend,
                                              SEARCHNEWBUF)) == CHARPN)
                    nextPara = pend + 1;           /* i.e. never hit it */
                  else if (nextPara < nextEvent)
                    nextEvent = nextPara;

		  /* Print string, translating if needed, and marking up MM: */
		  for (n = 0; ; )
                   {
		    /* Do Metamorph hit markup, if needed.  We do this
		     * before check for string end in case hit ends with
		     * string.  It's a loop in case hit start/end overlap:
		     */
                     while (p == nextEvent)     /* next event reached */
                     {
                       if (p == curOverallHitStart) /* overall MM hit start */
                         {
                           if (p == nextPara) goto para;   /* do para first */
                           if (state.curSetSameAsOverallHit)
                             goto doSetHitStart;        /* merge w/set hit */
                           if (((state.mFlags & MF_INLINESTYLE) &&
                                fmtcp->queryStyle != CHARPN) ||
                               ((state.mFlags & MF_CLASS) &&
                                fmtcp->queryClass != CHARPN))
                             /* ^-- note same check at </span> print below */
                             {
                               size_t   hLen;

                               strcpy(hbuf, SpanStart);
                               hLen = sizeof(SpanStart) - 1;
                               if ((state.mFlags & MF_INLINESTYLE) &&
                                   fmtcp->queryStyle != CHARPN &&
                                   hLen < sizeof(hbuf) - 1)
                                 hLen += htsnpf(hbuf + hLen,
                                                sizeof(hbuf) - hLen,
                                          " style=\"%H\"", fmtcp->queryStyle);
                               if ((state.mFlags & MF_CLASS) &&
                                   fmtcp->queryClass != CHARPN &&
                                   hLen < sizeof(hbuf) - 1)
                                 hLen += htsnpf(hbuf + hLen,
                                                sizeof(hbuf) - hLen,
                                               " class=\"%H\"",
                                               fmtcp->queryClass);
                               if (hLen < sizeof(hbuf) - 1)
                                 {
                                   hbuf[hLen++] = '>';
                                   hbuf[hLen] = '\0';
                                 }
                               if (hLen > sizeof(hbuf) - 1)
                                 hLen = sizeof(hbuf) - 1;
                               PUTSZ(hbuf, hLen);
                               n += (int)hLen;
                               spanDepth++;
                               /* Remember the currently-open <span>,
                                * in case we have to close and re-open it:
                                */
                               memcpy(curOpenSpan, hbuf, hLen);
                               curOpenSpan[hLen] = '\0';
                             }
                           /* No need to advance `curSetHitStart' per
                            * Bug 4715, since we would have skipped to
                            * `doSetHitStart' (which does that) if
                            * `curSetSameAsOverallHit'
                            */
                           curOverallHitStart = pend + 1; /*skip it next time*/
                           nextEvent = TX_MIN(curSetHitStart, nextPara);
                           nextEvent = TX_MIN(nextEvent, curOverallHitEnd);
                         }              /* if (p == curOverallHitStart) */

                      if (p == curSetHitStart)          /* MM set hit start */
                       {
                        if (p == nextPara) goto para;      /* do para first */
                        if (p == curOverallHitEnd) goto doHitEnd;
                       doSetHitStart:
			nextmmcnt = mm_next(mm, &nextSetHitStart,
                                            &nextSetHitEnd,
                             &nextSetOrpos, &nextSetPmtype, TXLOGITYPEPN,
                             &nextOverallHitStartOrg, &nextOverallHitEndOrg);
                        if (nextOverallHitStartOrg == CHARPN ||
                            /* Do not highlight overall if no style/class: */
                            !(state.mFlags & (MF_INLINESTYLE | MF_CLASS)) ||
                            /* nor if w/doc: */
                            nextOverallHitStartOrg == CHARPN ||
                            nextOverallHitEndOrg == CHARPN ||
                            /* If still same overall hit, no next-hit yet: */
                            (nextOverallHitStartOrg == curOverallHitStartOrg &&
                             nextOverallHitEndOrg == curOverallHitEndOrg))
                          nextOverallHitStart = nextOverallHitEnd = pend + 1;
                        else    /* could be in area deleted by para markup: */
                          {
                            nextOverallHitStart = nextOverallHitStartOrg;
                            nextOverallHitEnd = nextOverallHitEndOrg;
                            if (nextOverallHitStart<p) nextOverallHitStart = p;
                            if (nextOverallHitEnd < p) nextOverallHitEnd = p;
                          }
                        if (!nextSetHitStart)
                          nextSetHitStart = nextSetHitEnd = pend + 1;
                        else    /* could be in area deleted by para markup: */
                          {
                            if (nextSetHitStart < p) nextSetHitStart = p;
                            if (nextSetHitEnd < p) nextSetHitEnd = p;
                          }
                        nextSetSameAsOverallHit =
                          (nextSetHitStart == nextOverallHitStart &&
                           nextSetHitEnd == nextOverallHitEnd);
                        /* Bug 3906: do not highlight no-op sets: */
			if ((state.mFlags & MF_HREF) &&
                            state.curSetPmtype != PMISNOP)
                        {
                          pbc = htsnpf(hbuf, sizeof(hbuf), HtHrefStart,
                                       curmmcnt, nextmmcnt);
                          /* If style or class highlighting is also used,
                           * do it inside <a> tag.  This not only saves
                           * space (no <span> needed) but supports MSIE
                           * hover classes, which cannot be set in <span>:
                           */
                          state.copyToCurOpenSpan = TXbool_False;
                          if (state.mFlags & (MF_INLINESTYLE | MF_CLASS))
                            goto doInlinestyleAndClass;
                          if (pbc < sizeof(hbuf) - 1)
                            {
                              hbuf[pbc++] = '>';
                              hbuf[pbc] = '\0';
                            }
                          if (pbc > sizeof(hbuf) - 1) pbc = sizeof(hbuf) - 1;
                          PUTSZ(hbuf, pbc);
                          n += (int)pbc;
			}

                        state.copyToCurOpenSpan = TXbool_True;
                        pbc = 0;
                       doInlinestyleAndClass:
                        doQueryStyle = ((state.mFlags & MF_INLINESTYLE) &&
                                        fmtcp->queryStyle != CHARPN &&
                                        /* Only do queryStyle if this set hit
                                         * is exactly the same as overall hit:
                                         */
                                        state.curSetSameAsOverallHit &&
                                        /* Suppress queryStyle if using
                                         * all default styles: we know
                                         * default querySetStyles[n]
                                         * supercedes all queryStyle
                                         * settings.  Saves space:
                                         */
                                        !(fmtcp->queryStyle == (char *)
                                          TxfmtcpDefaultQueryStyle &&
                                          fmtcp->querySetStyles == (char**)
                                          TxfmtcpDefaultQuerySetStyles));
                        doQuerySetStyle = ((state.mFlags & MF_INLINESTYLE) &&
                                           /*Bug 3906: no hilite no-op sets:*/
                                           state.curSetPmtype != PMISNOP &&
                                           fmtcp->querySetStyles != CHARPPN &&
                                           fmtcp->numQuerySetStyles > 0);
                        doQueryClass = ((state.mFlags & MF_CLASS) &&
                                        fmtcp->queryClass != CHARPN &&
                                        /* Only do queryClass if this set hit
                                         * is exactly the same as overall hit:
                                         */
                                        state.curSetSameAsOverallHit);
                        doQuerySetClass = ((state.mFlags & MF_CLASS) &&
                                           /*Bug 3906: no hilite no-op sets:*/
                                           state.curSetPmtype != PMISNOP &&
                                           fmtcp->querySetClasses != CHARPPN&&
                                           fmtcp->numQuerySetClasses > 0);
                        if ((doQueryStyle || doQuerySetStyle ||
                             doQueryClass || doQuerySetClass) &&
                            state.copyToCurOpenSpan && /* not mid-`<a>'-tag */
                            pbc + sizeof(SpanStart) - 1 < sizeof(hbuf) - 1)
                          {
                            strcpy(hbuf, SpanStart);
                            pbc += sizeof(SpanStart) - 1;
                            spanDepth++;
                          }
                        if ((doQueryStyle || doQuerySetStyle) &&
                            pbc < sizeof(hbuf) - 1)
                          {
                            idx = (int)fmtcp->querySetCycleNum;
                            if (idx <= 0) idx = MAXINT;
                            idx = curSetOrpos % idx;
                            /* Re-use last style if beyond array end.
                             * NOTE: see also TxfmtcpCreateStylesheet():
                             */
                            if (idx >= fmtcp->numQuerySetStyles)
                              idx = fmtcp->numQuerySetStyles - 1;
                            pbc += htsnpf(hbuf + pbc, sizeof(hbuf) - pbc,
                                          " style=\"%H%H\"",
                                          (doQueryStyle ?
                                           fmtcp->queryStyle : ""),
                                          (doQuerySetStyle ?
                                           fmtcp->querySetStyles[idx]:""));
                          }
                        if ((doQueryClass || doQuerySetClass) &&
                            pbc < sizeof(hbuf) - 1)
                          {
                            idx = (int)fmtcp->querySetCycleNum;
                            if (idx <= 0) idx = MAXINT;
                            idx = curSetOrpos % idx;
                            /* Re-use last class if beyond array end.
                             * But unlike styles, we can increment the
                             * first number in the name, for uniqueness.
                             * NOTE: see also TxfmtcpCreateStylesheet():
                             */
                            if (idx >= fmtcp->numQuerySetClasses &&
                                doQuerySetClass)
                              {
                                e = className = fmtcp->querySetClasses[
                                               fmtcp->numQuerySetClasses - 1];
                                e += strcspn(className, "0123456789");
                                nClassPr = htsnpf(setClassBuf,
                                                  sizeof(setClassBuf), "%.*H",
                                             (int)(e - className), className);
                                ival = (int)strtol(e, &e, 0);
                                if (nClassPr < sizeof(setClassBuf) - 1)
                                  nClassPr += htsnpf(setClassBuf + nClassPr,
                                               sizeof(setClassBuf) - nClassPr,
                                                     "%d%s", ival +
                                        (idx - fmtcp->numQuerySetClasses) + 1,
                                                     e);
                              }
                            else
                              *setClassBuf = '\0';
                            pbc += htsnpf(hbuf + pbc, sizeof(hbuf)-pbc,
                                          " class=\"%H%s%H\"",
                                          (doQueryClass ?
                                           fmtcp->queryClass : ""),
                                          (doQueryClass && doQuerySetClass
                                           ? " " : ""),
                                          (doQuerySetClass ?
                                           (idx >= fmtcp->numQuerySetClasses ?
                                            setClassBuf :
                                            fmtcp->querySetClasses[idx]):""));
                          }
                        if ((doQueryStyle || doQuerySetStyle ||
                             doQueryClass || doQuerySetClass) ||
                            /* Even if we printed nothing here, we may still
                             * have to close an <a> tag from above:
                             */
                            !state.copyToCurOpenSpan)
                          {
                            if (pbc < sizeof(hbuf) - 1)
                              {
                                hbuf[pbc++] = '>';
                                hbuf[pbc] = '\0';
                              }
                            if (pbc > sizeof(hbuf) - 1)
                              pbc = sizeof(hbuf) - 1;
                            PUTSZ(hbuf, pbc);
                            n += (int)pbc;
                          }
                        if (state.copyToCurOpenSpan)
                          {
                            memcpy(curOpenSpan, hbuf, pbc);
                            curOpenSpan[pbc] = '\0';
                          }

			if (state.mFlags & MF_BOLDHTML) {
                          pbc = htsnpf(hbuf, sizeof(hbuf), HtBoldStart,
                                       curmmcnt, nextmmcnt);
                          if (pbc > sizeof(hbuf)) pbc = sizeof(hbuf);
                          PUTSZ(hbuf, pbc);
                          n += (int)pbc;
			}
			if (state.mFlags & MF_BOLDVT100) {
                          pbc = htsnpf(hbuf, sizeof(hbuf), Vt100BoldStart,
                                       curmmcnt, nextmmcnt);
                          if (pbc > sizeof(hbuf)) pbc = sizeof(hbuf);
                          PUTSZ(hbuf, pbc);
                          n += (int)pbc;
			}
			if (state.mFlags & MF_UNDERLINEVT100) {
                          pbc = htsnpf(hbuf, sizeof(hbuf),Vt100UnderlineStart,
                                       curmmcnt, nextmmcnt);
                          if (pbc > sizeof(hbuf)) pbc = sizeof(hbuf);
                          PUTSZ(hbuf, pbc);
                          n += (int)pbc;
			}
			if (state.mFlags & MF_REVERSEVT100)
                          {
                            pbc = htsnpf(hbuf, sizeof(hbuf),
                                         Vt100ReverseStart, curmmcnt,
                                         nextmmcnt);
                            if (pbc > sizeof(hbuf)) pbc = sizeof(hbuf);
                            PUTSZ(hbuf, pbc);
                            n += (int)pbc;
                          }
                        curSetHitStart = pend + 1;      /* skip it next time */
                        /* Bug 4715: advance `curOverallHitStart' if merged: */
                        if (state.curSetSameAsOverallHit)
                          curOverallHitStart = pend + 1;
			nextEvent = TX_MIN(curSetHitEnd, nextPara);
                        nextEvent = TX_MIN(nextEvent, curOverallHitEnd);
                      }                         /* if (p == curSetHitStart) */

                     if (p == curOverallHitEnd)         /* overall hit end */
                       {
                         TXbool orgSetSameAsOverallHit;

                       doHitEnd:
                         /* Bug 6958: `nextOverallHitStart' (and/or end?)
                          * might be < `p', i.e. the same as this overall hit:
                          */
                         if (nextOverallHitStart < p) nextOverallHitStart = p;
                         if (nextOverallHitEnd < p) nextOverallHitEnd = p;
                         /* Bug 6958 ancillary: update `next...Same' when
                          * `next...Hit' changes:
                          */
                         nextSetSameAsOverallHit =
                           (nextSetHitStart == nextOverallHitStart &&
                            nextSetHitEnd == nextOverallHitEnd);
                         curOverallHitStart = nextOverallHitStart;
                         curOverallHitEnd = nextOverallHitEnd;
                         orgSetSameAsOverallHit = state.curSetSameAsOverallHit;
                         state.curSetSameAsOverallHit=nextSetSameAsOverallHit;
                         /* no next overall hit yet: */
                         nextOverallHitStart = nextOverallHitEnd = pend + 1;
                         nextSetSameAsOverallHit = TXbool_False;
                         if (orgSetSameAsOverallHit)
                           goto doSetHitEnd;    /* merge with set end */
                         if (((state.mFlags & MF_INLINESTYLE) &&
                              fmtcp->queryStyle != CHARPN) ||
                             ((state.mFlags & MF_CLASS) &&
                              fmtcp->queryClass != CHARPN))
                           /* ^-- note same test at <span> start, above */
                           {
                             if (spanDepth > 0) /* sanity check */
                               {
                                 PUTSZ((char *)SpanEnd, sizeof(SpanEnd) - 1);
                                 n += sizeof(SpanEnd) - 1;
                                 *curOpenSpan = '\0';
                                 spanDepth--;
                               }
                             else
                               txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
                                              UnbalancedCloseSpanFmt,
                                              TXbasename(__FILE__),
                                              (int)__LINE__);
                           }
                         nextEvent = TX_MIN(curSetHitStart, nextPara);
                         nextEvent = TX_MIN(nextEvent, curSetHitEnd);
                         nextEvent = TX_MIN(nextEvent, curOverallHitStart);
                       }                /* if (p == curOverallHitEnd) */

		      if (p == curSetHitEnd) {	        /* MM set hit end */
                      doSetHitEnd:
			if (state.mFlags & (MF_BOLDVT100 | MF_UNDERLINEVT100 |
                                      MF_REVERSEVT100)) {
                          pbc = htsnpf(hbuf, sizeof(hbuf), Vt100BoldEnd,
                                       curmmcnt, nextmmcnt);
                          if (pbc > sizeof(hbuf)) pbc = sizeof(hbuf);
                          PUTSZ(hbuf, pbc);
                          n += (int)pbc;
			}
			if (state.mFlags & MF_BOLDHTML) {
                          pbc = htsnpf(hbuf, sizeof(hbuf), HtBoldEnd,
                                       curmmcnt, nextmmcnt);
                          if (pbc > sizeof(hbuf)) pbc = sizeof(hbuf);
                          PUTSZ(hbuf, pbc);
                          n += (int)pbc;
			}
                        if ((doQueryStyle || doQuerySetStyle ||
                             doQueryClass || doQuerySetClass) &&
                            /* ^-- note same test at <span> open, above */
                            !(state.mFlags & MF_HREF))
                          {
                            if (spanDepth > 0)  /* sanity check */
                              {
                                PUTSZ((char *)SpanEnd, sizeof(SpanEnd) - 1);
                                n += sizeof(SpanEnd) - 1;
                                *curOpenSpan = '\0';
                                spanDepth--;
                              }
                            else
                              txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
                                             UnbalancedCloseSpanFmt,
                                             TXbasename(__FILE__),
                                             (int)__LINE__);
                          }
			if (state.mFlags & MF_HREF) {
                          pbc = htsnpf(hbuf, sizeof(hbuf), HtHrefEnd,
                                       curmmcnt, nextmmcnt);
                          if (pbc > sizeof(hbuf)) pbc = sizeof(hbuf);
                          PUTSZ(hbuf, pbc);
                          n += (int)pbc;
			}
                        /* Bug 6958 sanity check: */
                        if (nextSetHitStart < p) nextSetHitStart = p;
                        if (nextSetHitEnd < p) nextSetHitEnd = p;
                        nextSetSameAsOverallHit =
                          (nextSetHitStart == nextOverallHitStart &&
                           nextSetHitEnd == nextOverallHitEnd);
                        /* advance to next set: */
			curSetHitStart = nextSetHitStart;
			curSetHitEnd = nextSetHitEnd;
                        curSetOrpos = nextSetOrpos;
                        state.curSetPmtype = nextSetPmtype;
			curmmcnt = nextmmcnt;
                        /* no set hit yet: */
			nextSetHitStart = nextSetHitEnd = pend + 1;
                        nextEvent = TX_MIN(curSetHitStart, nextPara);
                        nextEvent = TX_MIN(nextEvent, curOverallHitEnd);
                        /* We may have just finished the current
                         * overall hit, and therefore advanced
                         * `curOverallHitStart'; check it:
                         */
                        nextEvent = TX_MIN(nextEvent, curOverallHitStart);
		      }                         /* if (p == curSetHitEnd) */

                      if (p == nextPara)        /* paragraph break */
                        {
                        para:
                          /* Eat the hit, and move up overlapping hit ptrs: */
                          p2 = p + rexsize(paraRex);
                          if (p2 > pend) p2 = pend;
                          if (curSetHitStart < p2) curSetHitStart = p2;
                          if (curSetHitEnd < p2) curSetHitEnd = p2;
                          if (nextSetHitStart < p2) nextSetHitStart = p2;
                          if (nextSetHitEnd < p2) nextSetHitEnd = p2;
                          if (curOverallHitStart < p2) curOverallHitStart = p2;
                          if (curOverallHitEnd < p2) curOverallHitEnd = p2;
                          if (nextOverallHitStart<p2) nextOverallHitStart = p2;
                          if (nextOverallHitEnd < p2) nextOverallHitEnd = p2;
                          /* Bug 6958 ancillary: update `next...Same' when
                           * `next...Hit' changes:
                           */
                          nextSetSameAsOverallHit =
                            (nextSetHitStart == nextOverallHitStart &&
                             nextSetHitEnd == nextOverallHitEnd);
                          p = p2;
                          /* A block element like <p/> cannot occur inside
                           * an inline element like <span>, according to
                           * XHTML 1.0 Strict.  The <p/> also causes Firefox 3
                           * to forget the current <span>'s background color.
                           * So close the current span if open, and re-open
                           * after the <p/>:
                           */
                          if (*curOpenSpan != '\0')
                            {
                              if (spanDepth > 0)        /* sanity check */
                                {
                                  PUTSZ((char *)SpanEnd, sizeof(SpanEnd) - 1);
                                  n += sizeof(SpanEnd) - 1;
                                  spanDepth--;
                                }
                              else
                                txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
                                               UnbalancedCloseSpanFmt,
                                               TXbasename(__FILE__),
                                               (int)__LINE__);
                            }
                          PUTSZ(pReplaceStr, pReplaceStrSz);
                          n += (int)pReplaceStrSz;
                          if (*curOpenSpan != '\0')
                            {
                              PUTS(curOpenSpan);
                              spanDepth++;
                            }
                          nextPara = (char *)getrex(paraRex, (byte *)p,
                                                    (byte *)pend,
                                                    CONTINUESEARCH);
                          if (nextPara == CHARPN) nextPara = pend + 1;
                          nextEvent = TX_MIN(nextPara, curSetHitStart);
                          nextEvent = TX_MIN(nextEvent, curSetHitEnd);
                          nextEvent = TX_MIN(nextEvent, curOverallHitStart);
                          nextEvent = TX_MIN(nextEvent, curOverallHitEnd);
                        }                       /* if (p == nextPara) */

                     }                          /* while (p == nextEvent) */
                     if (p == pend) break;              /* end of string */

		    /* Print current char in output string, escaping if needed:
		     */
                     inChunkEnd = TX_MIN(nextEvent, pend);
		    switch (sch) {
		      case 's':	/* string */
                        pts = p;
                        if (utf & UTF_XMLSAFE)
                          {
                            pbc = htiso88591_to_iso88591(buf, bufsz, &dtot,
                                      (CONST char**)&pts, inChunkEnd - pts,
 (utf | UTF_BUFSTOP | UTF_DO8BIT | (inChunkEnd == pend ? UTF_FINAL : (UTF)0)),
                                      &utfstate, bwidth, fmtcp->htpfobj, pmbuf);
                            utf &= ~UTF_START;
                            if (pts == p)       /* no progress */
                              {
                                buf[0] = TX_INVALID_CHAR;
                                pbc = 1;
                                pts++;
                              }
                            PUTSZ(buf, pbc);
                            n += (int)pbc;
                            p = pts;
                            break;
                          }
                        /* Optimization: flush out as much of string
                         * (up to next event) as possible at a time:
                         */
                        if (utf & (UTF_CRNL | UTF_LFNL))
                          {
                            for (p2 = p; p2 < inChunkEnd; p2++)
                              switch (*p2)
                                {
                                case '\r':
                                  if (p2 > p)
                                    {
                                      PUTSZ(p, p2 - p);
                                      n += (int)(p2 - p);
                                      p = p2;
                                    }
                                  if (p2 + 1 < inChunkEnd && p2[1] == '\n')
                                    p2++;
                                  goto donewline;
                                case '\n':
                                  if (p2 > p)
                                    {
                                      PUTSZ(p, p2 - p);
                                      n += (int)(p2 - p);
                                      p = p2;
                                    }
                                donewline:
                                  switch (utf & (UTF_CRNL | UTF_LFNL))
                                    {
                                    case UTF_CRNL:
                                      PUTC('\015');
                                      n++;
                                      break;
                                    case UTF_LFNL:
                                      PUTC('\012');
                                      n++;
                                      break;
                                    case (UTF_CRNL | UTF_LFNL):
                                      PUTSZ("\015\012", 2);
                                      n += 2;
                                      break;
                                    }
                                  p = p2 + 1;
                                  break;
                                }
                            if (p == inChunkEnd) break;
                          }
                        if (inChunkEnd < p)     /* Bug 6958 sanity check */
                          {
                            txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
            "Internal error: Pointer mismatch at %s:%d; workaround attempted",
                                           TXbasename(__FILE__),
                                           (int)__LINE__);
                            inChunkEnd = p + 1; /* must advance nonzero */
                          }
                        PUTSZ(p, inChunkEnd - p);
                        n += (int)(inChunkEnd - p);
                        p = inChunkEnd;
                        break;
		      case 'H':	/* HTML encode/decode */
                        pts = p;
                        if (pflags & PRF_DECODE)
                          {
                            pbc = htiso88591_to_iso88591(buf, bufsz, &dtot,
                                      (CONST char**)&pts, inChunkEnd - pts,
              (utf | UTF_BUFSTOP | (inChunkEnd == pend ? UTF_FINAL : (UTF)0)),
                                      &utfstate, bwidth, fmtcp->htpfobj, pmbuf);
                            utf &= ~UTF_START;
                            if (pts == p)       /* no progress */
                              {
                                buf[0] = TX_INVALID_CHAR;
                                pbc = 1;
                                pts++;
                              }
                            PUTSZ(buf, pbc);
                            n += (int)pbc;
                            p = pts;
                            break;
                          }
                        /* Flush out as much as we can before escapement: */
#if TX_VERSION_MAJOR >= 5
                        if (pflags & PRF_LONGLONG) /* also esc some 8-bit */
                          for ( ; pts < inChunkEnd &&
                HTNOESC(*pts) && ((byte)(*pts) < 0x80 || (byte)(*pts) >= 160);
                                pts++);
#else /* TX_VERSION_MAJOR < 5 */
                        if (pflags & PRF_LONG)  /* only esc 7-bit chars */
                          for ( ; pts < inChunkEnd &&
                               ((byte)(*pts) > 0x7F || HTNOESC(*pts)); pts++);
#endif /* TX_VERSION_MAJOR < 5 */
                        else
                          for ( ; pts < inChunkEnd && HTNOESC(*pts); pts++);
                        if (pts > p)
                          {
                            if (utf & (UTF_CRNL | UTF_LFNL))
                              {
                                for (p2 = p; p2 < pts; p2++)
                                  switch (*p2)
                                    {
                                    case '\r':
                                      if (p2 > p)
                                        {
                                          PUTSZ(p, p2 - p);
                                          n += (int)(p2 - p);
                                          p = p2;
                                        }
                                      if (p2 + 1 < pts && p2[1] == '\n') p2++;
                                      goto donewline2;
                                    case '\n':
                                      if (p2 > p)
                                        {
                                          PUTSZ(p, p2 - p);
                                          n += (int)(p2 - p);
                                          p = p2;
                                        }
                                    donewline2:
                                      switch (utf & (UTF_CRNL | UTF_LFNL))
                                        {
                                        case UTF_CRNL:
                                          PUTC('\015');
                                          n++;
                                          break;
                                        case UTF_LFNL:
                                          PUTC('\012');
                                          n++;
                                          break;
                                        case (UTF_CRNL | UTF_LFNL):
                                          PUTSZ("\015\012", 2);
                                          n += 2;
                                          break;
                                        }
                                      p = p2 + 1;
                                      break;
                                    }
                                if (p == pts) break;
                              }
                          fish:
                            PUTSZ(p, pts - p);
                            n += (int)(pts - p);
                            p = pts;
                            break;
                          }
#if TX_VERSION_MAJOR >= 5
                        if ((pflags & PRF_LONGLONG) &&
                            (byte)(*p) >= 0x80 && (byte)(*p) < 160)
                          {     /* pre-v5 escaped some 8-bit chars too: */
                            hs = html2escbuf;
                            html2escbuf[0] = '&';
                            html2escbuf[1] = '#';
                            html2escbuf[2] = '1';
                            if ((byte)(*p) < 130) html2escbuf[3] = '2';
                            else if ((byte)(*p) < 140) html2escbuf[3] = '3';
                            else if ((byte)(*p) < 150) html2escbuf[3] = '4';
                            else html2escbuf[3] = '5';
                            html2escbuf[4] = '0' + ((int)(byte)(*p)) % 10;
                            html2escbuf[5] = ';';
                            html2escbuf[6] = '\0';
                            p++;
                          }
                        else
#endif /* TX_VERSION_MAJOR < 5 */
                          hs = html2esc((byte)(*(p++)), html2escbuf,
                                        sizeof(html2escbuf), pmbuf);
                        PUTS(hs);
                        break;
		      case 'U':	                /* URL encode/decode */
                        if (pflags & PRF_DECODE)
                          {                        /* see also urlstrncpy() */
                            USF flags;

                            for (pts = p; pts < inChunkEnd && *pts != '%' &&
                                   *pts != '+' && *pts != '&'; pts++);
                            if (pts > p) goto fish;
                            if (*p == '%')
                              {
                                pts++;
                                for (decpt = 0;
                                     decpt < 2 && pts < inChunkEnd;
                                     decpt++, pts++)
                                  if (!((*pts >= '0' && *pts <= '9') ||
                                        (*pts >= 'A' && *pts <= 'F') ||
                                        (*pts >= 'a' && *pts <= 'f')))
                                    break;
                              }
                            else pts++;
                            flags = (USF)0;
                            if (pflags & PRF_PATHURL) flags |= USF_IGNPLUS;
                            if (TX_LEGACY_VERSION_7_URL_CODING_CURRENT(TXApp)
                                && (pflags & PRF_QUERYURL))
                              flags |= USF_SAFE;
                            pbc = urlstrncpy(buf, bufsz, p, pts - p, flags);
                            PUTSZ(buf, pbc);
                            n += (int)pbc;
                            p = pts;
                          }
                        else                    /* %U encode */
                          {
                            URLMODE     mode = URLMODE_NORMAL;

                            if (pflags & PRF_QUERYURL)
                              mode = URLMODE_QUERY;
                            else if (pflags & PRF_PATHURL)
                              mode = URLMODE_PATH;
                            hs = dourl(dourlbuf, (byte)(*(p++)), mode);
                            PUTS(hs);
                          }
                        break;
                      default:   /* UTF-8 ('V'), base64 ('B'), 'v' */
                        {
                          size_t        orgDtot, srcSz, newBufSz;
                          UTF           orgUtfstate;

                          /* Save some values in case we re-run after alloc: */
                          orgDtot = dtot;
                          orgUtfstate = utfstate;
                          srcSz = inChunkEnd - p;
                        transAgain:
                          pts = p;
                          pbc = transfunc(buf, bufsz, &dtot,
                                          (const char **)&pts, srcSz,
                                          (utf | UTF_BUFSTOP |
                                           /* Always set UTF_FINAL for
                                            * enc. word format;
                                            * performs marginally
                                            * better for hit markup,
                                            * though still breaks a
                                            * lot:
                                            */
      (inChunkEnd == pend || transfunc == TXencodedWordToUtf8 ||
       transfunc == TXutf8ToEncodedWord ? UTF_FINAL : (UTF)0)),
                                          &utfstate, bwidth, fmtcp->htpfobj,
                                          pmbuf);
                        if (pts == p)                   /* no progress */
                          {
                            /* Bug 7103: Lack of progress might be due
                             * to large (> ~`bufsz') word for
                             * TXutf8ToEncodedWord(); increase `buf'
                             * to try to get past it.  WTF could use
                             * ~3x arg size of mem; should save state
                             * in TXutf8ToEncodedWord() or descendants
                             * instead:
                             */
                            if ((transfunc == TXencodedWordToUtf8 ||
                                 transfunc == TXutf8ToEncodedWord) &&
                                /* Q encoding should not take more than ~3x
                                 * source size; stop if unreasonable growth:
                                 */
                                bufsz < 3*srcSz + 100)/* for `=?UTF-8?Q?...'*/
                              {
                                newBufSz = bufsz + bufsz/2 + 4096;
                                ALLOCBUF(newBufSz);
                                dtot = orgDtot;
                                utfstate = orgUtfstate;
                                goto transAgain;
                              }
                            /* Otherwise, give up on `transfunc' here;
                             * output `?' and advance a byte:
                             */
                            buf[0] = TX_INVALID_CHAR;
                            pbc = 1;
                            pts++;
                          }
                        utf &= ~UTF_START;
                        PUTSZ(buf, pbc);
                        n += (int)pbc;
                        p = pts;
                        }
                        break;
		    }
                  }
                  /* Sanity check in case unclosed open `<span>'(s): */
                  for ( ; spanDepth > 0; spanDepth--)
                    {
                      PUTSZ((char *)SpanEnd, sizeof(SpanEnd) - 1);
                      n += sizeof(SpanEnd) - 1;
                      txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
                        "Internal error: Unbalanced `<span>' closed at %s:%d",
                                     TXbasename(__FILE__), (int)__LINE__);
                    }

		  if (mm != MMPN) mm = close_mm(mm);
                  if (paraRex) paraRex = closerex(paraRex);

                  if (overflowedPrecision && signflag == '+')
                    {
                      /* Print trailing ellipsis: */
                      PUTSZ("...", 3);
                    }

                  if (!(state.mFlags & MF_INUSE) && (pflags & PRF_LADJUST))
                   {
                     size_t     widthTodo;
                     char       *s, *d;

		    for ( ; n < width; n += (int)widthTodo)
                      {
                        widthTodo = (width - n)*padCharBufSz;
                        if (widthTodo > sizeof(hbuf))
                          widthTodo = sizeof(hbuf);
                        /* Make `widthTodo' an integral multiple of pad: */
                        widthTodo = (widthTodo/padCharBufSz)*padCharBufSz;
                        /* Copy integral multiple of padding to `hbuf': */
                        for (d = hbuf; d < hbuf + widthTodo; )
                          for (s = padCharBuf; s < padCharBuf + padCharBufSz; )
                            *(d++) = *(s++);
                        PUTSZ(hbuf, widthTodo);
                        widthTodo /= padCharBufSz;  /* bytes to width-chars */
                      }
                   }
		  break;	/* string/HTML/URL */

		case 't':	/* time_t localtime  KNG 960123 */
		        GETARG(tim, HTPFT_TIME, HTPFW_LOCAL, time_t, time_t,
                               *, fmtgae);
                        /* If first character is '|', skip it and use
                         * signal-safe versions of localtime/gmtime/strftime.
                         * These have less (and sometimes incorrect)
                         * functionality, but are guaranteed to not call
                         * malloc, for example:
                         */
                        if (tfmte > tfmt && *tfmt == '|')
                          {
                            tfmt++;
                            strftimefunc = TXstrftime;
                          }
                        /* Bug 4314: do not use functions known to be
                         * signal-unsafe during a signal: writing
                         * `(signal-unsafe-format)' is better than
                         * potentially hanging:
                         * KNG 20170504 TXstrftime() is pretty good;
                         * just use it, since TXinsignal() can have
                         * false positives if another thread is in signal:
                         */
                        else if (TXinsignal())
                          strftimefunc = TXstrftime;
                        if (strftimefunc == TXstrftime)
                          getTimeinfoResult = TXtime_tToLocalTxtimeinfo(tim,
                                                                   &timeinfo);
                        else
                          getTimeinfoResult = TXosTime_tToLocalTxtimeinfo(tim,
                                                                   &timeinfo);
			goto dotime;
		case 'T':	/* time_t gmtime     KNG 960123 */
		        GETARG(tim, HTPFT_TIME, HTPFW_GMT, time_t, time_t, *,
                               fmtgae);
                        /* If first character is '|', skip it and use
                         * signal-safe versions of localtime/gmtime/strftime.
                         * These have less (and sometimes incorrect)
                         * functionality, but are guaranteed to not call
                         * malloc, for example:
                         */
                        if (tfmte > tfmt && *tfmt == '|')
                          {
                            tfmt++;
                            strftimefunc = TXstrftime;
                          }
                        /* Bug 4314: do not use functions known to be
                         * signal-unsafe during a signal: writing
                         * `(signal-unsafe-format)' is better than
                         * potentially hanging:
                         * KNG 20170504 TXstrftime() is pretty good;
                         * just use it, since TXinsignal() can have
                         * false positives if another thread is in signal:
                         */
                        else if (TXinsignal())
                          strftimefunc = TXstrftime;
                        if (strftimefunc == TXstrftime)
                          getTimeinfoResult = TXtime_tToGmtTxtimeinfo(tim,
                                                                   &timeinfo);
                        else
                          getTimeinfoResult = TXosTime_tToGmtTxtimeinfo(tim,
                                                                   &timeinfo);
                        if (tfmt == (char *)DefTimeFmt)
                          {
                            tfmt = (char *)DefGmTimeFmt;
                            tfmte = (char *)DefGmTimeFmt + DEFGMTIMEFMT_LEN;
                          }
		      dotime:
                        if (!getTimeinfoResult) /* invalid time */
                          {
                            PUTSZ("(err)", 5);
                            n += 5;
                            break;
                          }
#ifdef BROKEN_STRFTIME_RET
                        /* Linux strftime() broken; will overwrite buf.
                         * (partial) sanity check ahead of time:   -KNG 970112
                         */
                        if ((size_t)(n = (int)(tfmte - tfmt) + 30) > bufsz)
                          goto talloc;
#endif  /* BROKEN_STRFTIME_RET */
                        n = (int)strftimefunc(buf, bufsz, tfmt, &timeinfo);
                        if ((size_t)n >= bufsz)         /* buf too small */
                          {
                            n = 2*n + 2;                /* WAG correct size */
                          talloc:
                            ALLOCBUF(n);
                            goto dotime;
                          }
                        bufend = buf + n;
                        p = buf - 1;
                        signflag = '-';
                        pflags |= PRF_SPACEPAD;
                        padCharBuf[0] = ' ';
                        padCharBufSz = 1;
			goto putnum;

                case 'L':                       /* lat/lon/location */
                  if (signflag == '+')          /* %+L location (geocode) */
                    {
                      argWhat = HTPFW_LOCATION;
                      GETARG(l, HTPFT_LONG, argWhat, long, long, *, fmtgae);
                      coord = TXgeocode2lat(l);
                      coord2 = TXgeocode2lon(l);
                    }
                  else
                    {
                      if (utf & UTF_BADENCASISO)    /* %|L longitude */
                        argWhat = HTPFW_LONGITUDE;
                      else                          /* %[-]L latitude */
                        argWhat = HTPFW_LATITUDE;
                      GETARG(coord, HTPFT_DOUBLE, argWhat, double, double, *,
                             fmtgae);
                      coord2 = coord;
                    }
                  if (tfmt == (char *)DefTimeFmt)
                    {
                      tfmt = (char *)TXdefCoordinateFormat;
                      tfmte = tfmt + sizeof(TXdefCoordinateFormat) - 1;
                    }
                  nprinted += TXprintCoordinate(out, outdata, tfmt,
                                                tfmte - tfmt, coord,
                                                (argWhat == HTPFW_LONGITUDE));
                  /* If %+L, print a space and then the longitude.
                   * Any other format (e.g. `(lat,lon)') and caller
                   * can just manually split lat/lon and print as desired:
                   */
                  if (argWhat == HTPFW_LOCATION)
                    {
                      PUTSZ(" ", 1);
                      nprinted += TXprintCoordinate(out, outdata, tfmt,
                                                    tfmte - tfmt, coord2, 1);
                    }
                  break;

                case 'F':       /* double as fraction  KNG 971015 */
		        GETARG(d, HTPFT_DOUBLE, HTPFW_FRAC, double, double,
                               *, fmtgae);
                        if (pflags & PRF_LADJUST)       /* wtf other flagstoo*/
                          {
                            width = -width;             /* for double2frac() */
                            pflags &= ~PRF_LADJUST;
                          }
                      doF:
                        n = (int)double2frac(buf, bufsz, d, width, prec);
                        if ((size_t)n >= bufsz)         /* buf too small */
                          {
                            n = 2*n + 2;                /* WAG correct size */
                            ALLOCBUF(n);
                            goto doF;
                          }
                        bufend = buf + n;
                        p = buf - 1;
                        signflag = '-';
                        pflags |= PRF_SPACEPAD;
                        padCharBuf[0] = ' ';
                        padCharBufSz = 1;
                        width = 0;                      /* double2frac() did */
			goto putnum;

		case 'u':
			base = 10;
                        if (pflags & PRF_HUGE)
                          {
                            GETARG(hui, HTPFT_HUGEUINT, HTPFW_UNSIGNED,
                                   EPI_HUGEUINT, EPI_HUGEUINT, *, fmtgae);
			    goto dohugeint;
			  }
#ifndef NOLONG
#  ifdef EPI_HAVE_UNSIGNED_LONG_LONG
                        if (pflags & PRF_LONGLONG)
                          {
                            GETARG(ull, HTPFT_ULONGLONG, HTPFW_UNSIGNED,
                                   unsigned long long, unsigned long long,
                                   *, fmtgae);
			    goto dolonglong;
			  }
#  endif /* EPI_HAVE_UNSIGNED_LONG_LONG */
			if(pflags & PRF_LONG)
                          {
                            GETARG(ul, HTPFT_ULONG, HTPFW_UNSIGNED,
                                   ulong, ulong, *, fmtgae);
                            goto dolong;
                          }
#endif	/* !NOLONG */
			GETARG(u, HTPFT_UNSIGNED, HTPFW_UNSIGNED, unsigned,
                               unsigned, *, fmtgae);
			goto donum;

		case 'X':
			digs = HexUp;
		case 'x':
			base = 16;
                        if (pflags & PRF_HUGE)
                          {
                            GETARG(hui, HTPFT_HUGEUINT, HTPFW_HEX,
                                   EPI_HUGEUINT, EPI_HUGEUINT, *, fmtgae);
dohugeint:
                            if (base != 10) ksep = ksepe = CHARPN;
                            n = (int)(EPI_HUGEUINT_BITS/3 + 4 +
                                    (1 + EPI_HUGEUINT_BITS/9)*(ksepe - ksep) +
                                      padCharBufSz);
                            ALLOCBUF(n);
                            p = bufend - 1;
                            if (hui == (EPI_HUGEUINT)0) pflags &= ~PRF_NUMSGN;
                            n = prec;
                            i = 0;
                            do
                              {
                                if (ksep != CHARPN && i && !(i % 3))
                                  {
                                    for (pts = ksepe; pts > ksep; )
                                      *(p--) = *(--pts);
                                  }
                                *(p--) = digs[hui % (EPI_HUGEUINT)base];
                                hui /= (EPI_HUGEINT)base;
                                n--;
                                i++;
                              }
                            while (hui || (prec != -1 && n > 0));
                            goto donumsgn;
                          }
#ifndef NOLONG
#  if defined(EPI_HAVE_LONG_LONG) || defined(EPI_HAVE_UNSIGNED_LONG_LONG)
                        if (pflags & PRF_LONGLONG)
                          {
                            GETARG(ull, HTPFT_ULONGLONG, HTPFW_HEX,
                                   unsigned long long, unsigned long long, *,
                                   fmtgae);
dolonglong:
                            if (base != 10) ksep = ksepe = CHARPN;
                            n = (int)(EPI_OS_ULONGLONG_BITS/3 + 4 +
                                      (1 + EPI_OS_ULONGLONG_BITS/9)*
                                      (ksepe - ksep) + padCharBufSz);
                            ALLOCBUF(n);
                            p = bufend - 1;
                            if (ull == (unsigned long long)0)
                              pflags &= ~PRF_NUMSGN;
                            n = prec;
                            i = 0;
                            do
                              {
                                if (ksep != CHARPN && i && !(i % 3))
                                  {
                                    for (pts = ksepe; pts > ksep; )
                                      *(p--) = *(--pts);
                                  }
                                *(p--) = digs[ull % (unsigned long long)base];
                                ull /= (unsigned long long)base;
                                n--;
                                i++;
                              }
                            while (ull || (prec != -1 && n > 0));
                            goto donumsgn;
                          }
#  endif /* EPI_HAVE_LONG_LONG || EPI_HAVE_UNSIGNED_LONG_LONG */
			if(pflags & PRF_LONG)
				{
				GETARG(ul, HTPFT_ULONG, HTPFW_HEX,
                                       ulong, ulong, *, fmtgae);
dolong:
                                if (base != 10) ksep = ksepe = CHARPN;
                                n = (int)(EPI_OS_ULONG_BITS/3 + 4 +
                                          (1 + EPI_OS_ULONG_BITS/9)*
                                          (ksepe - ksep) + padCharBufSz);
                                ALLOCBUF(n);
                                p = bufend - 1;
				if(ul == 0L)
					pflags &= ~PRF_NUMSGN;
				n = prec;
                                i = 0;
				do {
                                  if (ksep != CHARPN && i && !(i % 3))
                                    {
                                      for (pts = ksepe; pts > ksep; )
                                        *(p--) = *(--pts);
                                    }
				  *(p--) = digs[ul % (ulong)base];
				  ul /= (ulong)base;
				  n--;
                                  i++;
				} while(ul || (prec != -1 && n > 0));
				goto donumsgn;
			      }
#endif	/* !NOLONG */
			GETARG(u, HTPFT_UNSIGNED, HTPFW_HEX, unsigned,
                               unsigned, *, fmtgae);
donum:
                        if (base != 10) ksep = ksepe = CHARPN;
                        n = (int)(EPI_OS_INT_BITS/3 + 4 +
                                  (1 + EPI_OS_INT_BITS/9)*(ksepe - ksep) +
                                  padCharBufSz);
                        ALLOCBUF(n);
                        p = bufend - 1;

			if(u == 0)
				pflags &= ~PRF_NUMSGN;

			n = prec;
                        i = 0;
			do	{
                                if (ksep != CHARPN && i && !(i % 3))
                                  {
                                    for (pts = ksepe; pts > ksep; )
                                      *(p--) = *(--pts);
                                  }
				*(p--) = digs[u % base];
				u /= base;
				n--;
                                i++;
				} while(u || (prec != -1 && n > 0));
donumsgn:
                        pwidth = 0;
			if(pflags & PRF_NUMSGN)
				{
                                pts = p;
                                if (fmt < fmte)
                                  switch(*fmt)
					{
#ifdef BINARY
					case 'b':
						*(p--) = 'b';
						*(p--) = '0';
						break;
#endif
					case 'x':
						*(p--) = 'x';
						/* fall through */
					case 'o':
						*(p--) = '0';
						break;

					case 'X':
						*(p--) = 'X';
						*(p--) = '0';
						break;
					}
                                pwidth = pts - p;
                                goto putnumcont;
				}

putnum:
                        pwidth = 0;
putnumcont:
                        size = (int)((bufend - 1) - p);
                        if (isneg)
                          {
                            if (pflags & PRF_SPACEPAD)
                              *(p--) = '-';
                            else
                              PUTC('-');
                            size++;
                          }
                        else if (signflag != '-')
                          {
                            if (pflags & PRF_SPACEPAD)
                              {
                                char    *s;
                                /* padCharBuf could be "&nbsp;"; use that if
                                 * signflag is space:   KNG 990707
                                 */
                                if (signflag == ' ')
                                  for (s = padCharBuf + padCharBufSz - 1;
                                       s >= padCharBuf; )
                                    *(p--) = *(s--);
                                else
                                  *(p--) = signflag;
                              }
                            else
                              PUTC(signflag);
                            size++;
                          }
			if(size < width && !(pflags & PRF_LADJUST))
				{
                                /* Bug 980721: was putting 0-padding
                                 * to left of "0x"/"0b" prefix:
                                 */
                                if (!(pflags & PRF_SPACEPAD) && pwidth > 0)
                                  {
                                    PUTSZ(p + 1, pwidth);
                                    p += pwidth;
                                  }
				while(width > size)
					{
					PUTSZ(padCharBuf, padCharBufSz);
					width--;
					}
				}
                        if (++p < bufend)
                          {
                            PUTSZ(p, bufend - p);
                            p = bufend;
                          }

			if(size < width)	/* must be ladjust */
				{
				while(width > size)
					{
					PUTSZ(padCharBuf, padCharBufSz);
					width--;
					}
				}

			break;

                case '%':
                        PUTC('%');
                        break;
                case '/':
                  if (pflags & PRF_LONGLONG)
                    PUTSZ(TX_PATH_SEP_REPLACE_EXPR,
                          TX_PATH_SEP_REPLACE_EXPR_LEN);
                  else if (pflags & PRF_LONG)
                    {
                      if (pflags & PRF_DECODE)
                        {
                          hbuf[0] = '[';
                          hbuf[1] = '^';
                          memcpy(hbuf + 2, TX_PATH_SEP_REX_CHAR_CLASS + 1,
                                 TX_PATH_SEP_REX_CHAR_CLASS_LEN - 1);
                          PUTSZ(hbuf, TX_PATH_SEP_REX_CHAR_CLASS_LEN + 1);
                        }
                      else
                        PUTSZ(TX_PATH_SEP_REX_CHAR_CLASS,
                              TX_PATH_SEP_REX_CHAR_CLASS_LEN);
                    }
                  else
                    PUTC(PATH_SEP);
                  break;
                case ':':
                  if (pflags & PRF_LONGLONG)
                    PUTSZ(TX_PATH_DIV_REPLACE_EXPR,
                          TX_PATH_DIV_REPLACE_EXPR_LEN);
                  else if (pflags & PRF_LONG)
                    {
                      if (pflags & PRF_DECODE)
                        {
                          hbuf[0] = '[';
                          hbuf[1] = '^';
                          memcpy(hbuf + 2, TX_PATH_DIV_REX_CHAR_CLASS + 1,
                                 TX_PATH_DIV_REX_CHAR_CLASS_LEN - 1);
                          PUTSZ(hbuf, TX_PATH_DIV_REX_CHAR_CLASS_LEN + 1);
                        }
                      else
                        PUTSZ(TX_PATH_DIV_REX_CHAR_CLASS,
                              TX_PATH_DIV_REX_CHAR_CLASS_LEN);
                    }
                  else
                    PUTC(PATH_DIV);
                  break;
		default:
                  /* print format code as-is if unknown:  -KNG 960920 */
                unknownOrMissingCode:
                  sz = (size_t)(-1);
                  if (getarg != HTPFCBPN)
                    {
                      errstr = (char *)fmt;
                      if ((ptr = getarg(HTPFT_VOID, HTPFW_FMTERR, getdata,
                                        &errstr, &sz)) == NULL)
                        goto fmtgae;
                    }
                  else
                    {
                      PUTSZ((char *)fmtstart, TX_MIN(fmt+1, fmte) - fmtstart);
                    }
                  break;
		}
	fmt++;
        continue;
      fmtgae:           /* getarg() error */
        if (errstr != fmt++ && errstr != CHARPN)
          {
            PUTS(errstr);
          }
        continue;
      memerr:
        buf = hbuf;
        bufsz = sizeof(hbuf);
        bufend = hbuf + sizeof(buf);
      }

  /* note: `fmt' might be 1 beyond `fmte' here, if missing format code */

  if (buf != hbuf && buf != CHARPN) buf = TXfree(buf);
  if (flags & (HTPFF_WRHTPFARGS|HTPFF_RDHTPFARGS)) *argnp = argi;
  if (fs == &tmpFs) TXFMTSTATE_RELEASE(&tmpFs);
  /*
   * Caller should handle, now HTPFOBJ

  if (fmtcp && fmtcp->htobj) fmtcp->htobj->pmbuf = orgHtobjPmbuf;
   */
  return(nprinted);
}

/* ------------------------------------------------------------------------- */

#ifdef ROMAN
static void
tack(d, digs, p)
int	d;
char	*digs;
char	**p;
{
if(d == 0) return;
if(d >= 1 && d <= 3)
	{
	doit(d, digs[2], p);
	return;
	}

if(d == 4 || d == 5)
	{
	**p = digs[1];
	(*p)--;
	}

if(d == 4)
	{
	**p = digs[2];
	(*p)--;
	return;
	}

if(d == 5) return;

if(d >= 6 && d <= 8)
	{
	doit(d - 5, digs[2], p);
	**p = digs[1];
	(*p)--;
	return;
	}

/* d == 9 */

**p = digs[0];
(*p)--;
**p = digs[2];
(*p)--;
return;
}

/* ------------------------------------------------------------------------- */

static void
doit(d, one, p)
int	d, one;
char	**p;
{
  int	i;

  for (i = 0; i < d; i++) {
    **p = one;
    (*p)--;
  }
}
#endif	/* ROMAN */

/* ------------------------------------------------------------------------- */

void
TXhtpfFileCb(data, s, sz)
void    *data;
char    *s;
size_t  sz;
{
  if (sz == 1)
    putc(*s, (FILE *)data);
  else
    fwrite(s, 1, sz, (FILE *)data);
}

int CDECL
#ifdef EPI_HAVE_STDARG
htpf(CONST char *fmt, ...)
{
  va_list	argp;
  size_t	r;

  va_start(argp, fmt);
#else	/* !EPI_HAVE_STDARG */
htpf(va_alist)
va_dcl
{
  va_list	argp;
  size_t        r;
  CONST char	*fmt;

  va_start(argp);
  fmt = va_arg(argp, CONST char *);
#endif	/* !EPI_HAVE_STDARG */

  r = htpfengine(fmt, (size_t)(-1), (HTPFF)0, TXFMTCPPN, TXFMTSTATEPN, argp,
                 HTPFCBPN, NULL, HTPFARGPN, NULL, TXhtpfFileCb,
                 (void *)stdout, TXPMBUFPN);
  va_end(argp);
  if (ferror(stdout)) return(EOF);
  else return((int)r);
}

/* ------------------------------------------------------------------------- */

int CDECL
#ifdef EPI_HAVE_STDARG
htfpf(FILE *fp, CONST char *fmt, ...)
{
  va_list	argp;
  size_t	r;

  va_start(argp, fmt);
#else	/* !EPI_HAVE_STDARG */
htfpf(va_alist)
va_dcl
{
  va_list	argp;
  size_t        r;
  FILE		*fp;
  CONST char	*fmt;

  va_start(argp);
  fp = va_arg(argp, FILE *);
  fmt = va_arg(argp, CONST char *);
#endif	/* !EPI_HAVE_STDARG */

  r = htpfengine(fmt, (size_t)(-1), (HTPFF)0, TXFMTCPPN, TXFMTSTATEPN, argp,
                 HTPFCBPN, NULL, HTPFARGPN, NULL, TXhtpfFileCb,
                 (void *)fp, TXPMBUFPN);
  va_end(argp);
  if (ferror(fp)) return(EOF);
  else return((int)r);
}

/* ------------------------------------------------------------------------- */

int
htvfpf(fp, fmt, argp)
FILE                    *fp;
CONST char              *fmt;
HTPF_SYS_VA_LIST        argp;
{
  size_t        r;

  r = htpfengine(fmt, (size_t)(-1), (HTPFF)0, TXFMTCPPN, TXFMTSTATEPN, argp,
                 HTPFCBPN, NULL, HTPFARGPN, NULL, TXhtpfFileCb,
                 (void *)fp, TXPMBUFPN);
  if (ferror(fp)) return(EOF);
  else return((int)r);
}

/* ------------------------------------------------------------------------- */

/* sprintf state vars.  Need at least 2 since doprint() recurses once:
 * -KNG 960122
 */
typedef struct SPFSTATE_tag
{
  char  *st, *end;
}
SPFSTATE;

static void prstrcb ARGS((void *data, char *s, size_t sz));
static void
prstrcb(data, s, sz)
void    *data;
char    *s;
size_t  sz;
{
  SPFSTATE	*spt = (SPFSTATE *)data;

  if (spt->st < spt->end)
    {
      if (spt->st + sz > spt->end) sz = spt->end - spt->st;
      memcpy(spt->st, s, sz);
      spt->st += sz;
    }
}

/* ------------------------------------------------------------------------- */

SPFRET_TYPE CDECL
#ifdef EPI_HAVE_STDARG
htspf(char *buf, CONST char *fmt, ...)
{
  va_list	argp;
  size_t	r;
  SPFSTATE      spt;

  va_start(argp, fmt);
#else	/* !EPI_HAVE_STDARG */
htspf(va_alist)
va_dcl
{
  va_list	argp;
  size_t        r;
  SPFSTATE      spt;
  char		*buf;
  CONST char	*fmt;

  va_start(argp);
  buf = va_arg(argp, char *);
  fmt = va_arg(argp, CONST char *);
#endif	/* !EPI_HAVE_STDARG */

  spt.st = buf;
  spt.end = (char *)((ulong)~0L);               /* i.e. very long */
  r = htpfengine(fmt, (size_t)(-1), (HTPFF)0, TXFMTCPPN, TXFMTSTATEPN, argp,
                 HTPFCBPN, NULL, HTPFARGPN, NULL, prstrcb, (void *)&spt,
                 TXPMBUFPN);
  va_end(argp);
  *spt.st = '\0';	/* caller responsible for allocating enough space... */
  SPFRET((int)r, buf);
}

/* ------------------------------------------------------------------------- */

int
htvspf(buf, fmt, argp)
char                    *buf;
CONST char              *fmt;
HTPF_SYS_VA_LIST        argp;
{
  size_t        r;
  SPFSTATE      spt;

  spt.st = buf;
  spt.end = (char *)((ulong)~0L);               /* i.e. very long */
  r = htpfengine(fmt, (size_t)(-1), (HTPFF)0, TXFMTCPPN, TXFMTSTATEPN, argp,
                 HTPFCBPN, NULL, HTPFARGPN, NULL, prstrcb, (void *)&spt,
                 TXPMBUFPN);
  *spt.st = '\0';	/* caller responsible for allocating enough space... */
  return((int)r);
}

/* ------------------------------------------------------------------------- */

int CDECL
#ifdef EPI_HAVE_STDARG
htsnpf(char *buf, size_t sz, CONST char *fmt, ...)
{
  va_list	argp;
  size_t	r;
  SPFSTATE      spt;

  va_start(argp, fmt);
#else	/* !EPI_HAVE_STDARG */
htsnpf(va_alist)
va_dcl
{
  va_list	argp;
  size_t        r;
  SPFSTATE      spt;
  char		*buf;
  int		sz;
  CONST char	*fmt;

  va_start(argp);
  buf = va_arg(argp, char *);
  sz = va_arg(argp, int);
  fmt = va_arg(argp, CONST char *);
#endif	/* !EPI_HAVE_STDARG */

  spt.st = buf;
  spt.end = buf + (sz > 0 ? sz - 1 : 0);	/* skip last byte for '\0' */
  r = htpfengine(fmt, (size_t)(-1), (HTPFF)0, TXFMTCPPN, TXFMTSTATEPN, argp,
                 HTPFCBPN, NULL, HTPFARGPN, NULL, prstrcb, (void *)&spt,
                 TXPMBUFPN);
  va_end(argp);
  if (sz > 0) *spt.st = '\0';
  return((int)r);
}

#ifndef HTPF_STRAIGHT
int CDECL
#ifdef EPI_HAVE_STDARG
#  if (defined(hpux) && defined(__LP64__))
snprintf(char *buf, size_t sz, char *fmt, ...)
#  else /* !(hpux && __LP64__) */
snprintf(char *buf, size_t sz, CONST char *fmt, ...)
#  endif /* !(hpux && __LP64__) */
{
  va_list	argp;
  size_t	r;
  SPFSTATE      spt;

  va_start(argp, fmt);
#else	/* !EPI_HAVE_STDARG */
snprintf(va_alist)
va_dcl
{
  va_list	argp;
  size_t        r;
  SPFSTATE      spt;
  char		*buf;
  int		sz;
  CONST char	*fmt;

  va_start(argp);
  buf = va_arg(argp, char *);
  sz = va_arg(argp, int);
  fmt = va_arg(argp, CONST char *);
#endif	/* !EPI_HAVE_STDARG */

  spt.st = buf;
  spt.end = buf + (sz > 0 ? sz - 1 : 0);	/* skip last byte for '\0' */
  r = htpfengine(fmt, (size_t)(-1), (HTPFF)0, TXFMTCPPN, TXFMTSTATEPN, argp,
                 HTPFCBPN, NULL, HTPFARGPN, NULL, prstrcb, (void *)&spt,
                 TXPMBUFPN);
  va_end(argp);
  if (sz > 0) *spt.st = '\0';
  return((int)r);
}
#endif /* !HTPF_STRAIGHT */

/* ------------------------------------------------------------------------- */

int
htvsnpf(buf, sz, fmt, flags, fmtcp, fs, argp, args, argnp, pmbuf)
char		*buf;
size_t		sz;
CONST char	*fmt;
HTPFF           flags;  /* (in) */
CONST TXFMTCP   *fmtcp; /* (in) settings */
TXFMTSTATE      *fs;    /* (in/out) state */
HTPF_SYS_VA_LIST argp;
HTPFARG         *args;  /* (in/out) (opt.) format args array */
size_t          *argnp; /* (in/out) (opt.) format args count */
TXPMBUF         *pmbuf; /* (in/out) (opt.) putmsg buffer */
{
  size_t        r;
  SPFSTATE      spt;

  spt.st = buf;
  spt.end = buf + (sz > 0 ? sz - 1 : 0);	/* skip last byte for '\0' */
  r = htpfengine(fmt, (size_t)(-1), flags, fmtcp, fs, argp, HTPFCBPN, NULL,
                 args, argnp, prstrcb, (void *)&spt, pmbuf);
  if (sz > 0) *spt.st = '\0';
  return((int)r);
}

int
htcsnpf(buf, sz, fmt, fmtSz, flags, fmtcp, fs, cb, data)
char            *buf;
size_t          sz;
CONST char      *fmt;
size_t          fmtSz;  /* (in) length of `fmt'; -1 == strlen(fmt) */
HTPFF           flags;
CONST TXFMTCP   *fmtcp; /* (in) settings */
TXFMTSTATE      *fs;    /* (in/out) state */
HTPFCB          *cb;
void            *data;
{
  size_t        r;
  SPFSTATE      spt;
  va_list       dummyargp;              /* (va_list)NULL is non-portable... */

  *((char *)&dummyargp) = '\0';                 /* shut up warning */
  spt.st = buf;
  spt.end = buf + (sz > 0 ? sz - 1 : 0);        /* skip last byte for '\0' */
  r = htpfengine(fmt, fmtSz,
                 (flags & ~(HTPFF_RDHTPFARGS | HTPFF_WRHTPFARGS)), fmtcp, fs,
                 dummyargp, cb, data, HTPFARGPN, NULL, prstrcb, (void *)&spt,
                 TXPMBUFPN);
  if (sz > 0) *spt.st = '\0';
  return((int)r);
}

int
htcfpf(fp, fmt, fmtSz, flags, fmtcp, fs, cb, data)
FILE            *fp;
CONST char      *fmt;
size_t          fmtSz;  /* (in) length of `fmt'; -1 == strlen(fmt) */
HTPFF           flags;
CONST TXFMTCP   *fmtcp; /* (in) settings */
TXFMTSTATE      *fs;    /* (in/out) state */
HTPFCB          *cb;
void            *data;
{
  size_t        r;
  va_list       dummyargp;              /* (va_list)NULL is non-portable... */

  *((char *)&dummyargp) = '\0';         /* shut up warning */
  r = htpfengine(fmt, fmtSz,
                 (flags & ~(HTPFF_RDHTPFARGS | HTPFF_WRHTPFARGS)), fmtcp, fs,
                 dummyargp, cb, data, HTPFARGPN, NULL, TXhtpfFileCb,
                 (void *)fp, TXPMBUFPN);
  if (ferror(fp)) return(EOF);
  else return((int)r);
}

/* ------------------------------------------------------------------------- */

typedef struct TXringBufferPrintState_tag
{
  char          *buf;
  size_t        bufSz;
  size_t        head;
  size_t        tail;
}
TXringBufferPrintState;

static void
TXprintToRingBufferCallback(void *usr, char *s, size_t sz)
/* Thread-safe.  Async-signal-safe.
 */
{
  TXringBufferPrintState        *state = (TXringBufferPrintState *)usr;
  size_t                        lenToCopy;

  /* Sanity checks: */
  if (!state->buf) goto finally;
  if (state->bufSz <= 0) goto finally;          /* avoid size_t underflow */
  if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(state->head) ||
      state->head > state->bufSz)
    goto finally;
  if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(state->tail) ||
      state->tail > state->bufSz)
    goto finally;

  /* Free space could be wrapped or not.  Handle wrapped space first,
   * because second part of that is same as non-wrapped, so we can do
   * that second part with the non-wrapped check.
   *
   * Note that when computing free space, we leave at least one byte
   * remaining -- to distinguish full buffer from empty buffer, and
   * for nul-termination.
   */
  if (state->tail >= state->head && sz > 0)
    {
      /* (Potentially) wrapped free space (---):
       *   0          head                 tail                     bufSz
       *   |-----------+====================+-------------------------|
       */
      if (state->head == state->bufSz)          /* and thus head == tail too*/
        state->head = state->tail = 0;
      /* Copy first part (tail to bufSz): */
      lenToCopy = state->bufSz - state->tail;
      if (state->head <= 0 && lenToCopy > 0)
        lenToCopy--;                            /* at least 1 byte left free*/
      if (lenToCopy > sz) lenToCopy = sz;
      if (lenToCopy > 0)
        {
          memcpy(state->buf + state->tail, s, lenToCopy);
          s += lenToCopy;
          sz -= lenToCopy;
          state->tail += lenToCopy;
          /* Wrap tail around if buffer end reached: */
          if (state->tail >= state->bufSz) state->tail = 0;
        }
      /* Fall through and copy second part, if more remaining: */
    }

  if (state->tail < state->head && sz > 0)
    {
      /* Non-wrapped free space (---):
       *   0             tail                      head             BufSz
       *   |==============+-------------------------+=================|
       */
      lenToCopy = (state->head - state->tail) - 1; /* -1: 1 byte left free */
      if (lenToCopy > sz) lenToCopy = sz;
      if (lenToCopy > 0)
        {
          memcpy(state->buf + state->tail, s, lenToCopy);
          s += lenToCopy;
          sz -= lenToCopy;
          state->tail += lenToCopy;
        }
    }

finally:
  return;
}

/* ------------------------------------------------------------------------ */

size_t
TXvsnprintfToRingBuffer(TXPMBUF *pmbuf, char *buf, size_t bufSz,
        size_t headOffset, size_t *tailOffset, const char *fmt, HTPFF flags,
        const TXFMTCP *fmtcp, TXFMTSTATE *fs, HTPF_SYS_VA_LIST argp,
        HTPFARG *args, size_t *argnp)
/* Like htvsnpf(), but prints to ring buffer `buf' of total size `bufSz'.
 * Assumes existing data spans `headOffset' to `*tailOffset'.  Prints
 * at `*tailOffset', advancing it.  Nul-terminates buffer.
 * Returns would-be length of data printed.
 * Thread-safe and async-signal-safe, iff htpfengine() is.
 */
{
  size_t                        fullLen;
  TXringBufferPrintState        state;

  memset(&state, 0, sizeof(TXringBufferPrintState));
  if (!buf) bufSz = 0;
  state.buf = buf;
  state.bufSz = bufSz;
  state.head = headOffset;
  state.tail = *tailOffset;

  fullLen = htpfengine(fmt, (size_t)(-1), flags, fmtcp, fs, argp, HTPFCBPN,
                       NULL, args, argnp, TXprintToRingBufferCallback,
                       (void *)&state, pmbuf);
  if (bufSz > 0) buf[state.tail] = '\0';
  *tailOffset = state.tail;
  return(fullLen);
}

/**********************************************************************/
#if !defined(USE_SNPRINTF_NOT_CVT) && defined(__FreeBSD__) /* MAW 04-09-98 - totally lacking ecvt(), fcvt(), gcvt() */
/*
** cvt() function taken verbatim from FreeBSD 2.2.5 stdlib printf.c 04-09-98
** except changed "sign" to (int *) instead of (char *)
** and removed "flag" usage (only 1, and unimportant)
*/
/* KNG 020304 FreeBSD 4.5 changed the standard to darkness; __dtoa() has
 * extra parameter char **resultp.  Actual runtime call may (4.5) or will
 * not (4.0) set this; we don't know till runtime due to shared libs.
 */
extern char *__dtoa ARGS((double d, int mode, int ndigits, int *decpt,
                          int *sign, char **rve, char **resultp));

static char *cvt ARGS((double value, int ndigits, int flags, int *sign,
                       int *decpt, int ch, int *length));
static char *
cvt(value, ndigits, flags, sign, decpt, ch, length)
	double value;
	int ndigits, flags, *decpt, ch, *length;
	int *sign;
{
        static char *presult = CHARPN;
        static char zbuf[64];
        int mode, dsgn, n;
	char *digits, *bp, *rve, *d;

	if (ch == 'f')
		mode = 3;		/* ndigits after the decimal point */
	else {
		/*
		 * To obtain ndigits after the decimal point for the 'e'
		 * and 'E' formats, round to ndigits + 1 significant
		 * figures.
		 */
		if (ch == 'e' || ch == 'E')
			ndigits++;
		mode = 2;		/* ndigits significant digits */
	}
	if (value < (double)0.0) {
		value = -value;
		*sign = -1;
	} else
		*sign = 0;
        presult = TXfree(presult);      /*avoid memleak; WTF not thread-safe*/
        digits = __dtoa(value, mode, ndigits, decpt, &dsgn, &rve, &presult);
        /* KNG 001005 don't do trailing zeros for Infinity either: */
        if (ch != 'g' && ch != 'G' && *digits != 'I' && *digits != 'i') {
		/* print trailing zeros */
		bp = digits + ndigits;
		if (ch == 'f') {
			if (*digits == '0' && value)
				*decpt = -ndigits + 1;
			bp += *decpt;
		}
                if (value == (double)0.0)
                  {	                /* cannot write to *rve KNG 001005 */
                    n = rve - digits;
                    if (n > sizeof(zbuf) - 1) n = sizeof(zbuf) - 1;
                    memcpy(zbuf, digits, n);
                    bp = zbuf + n + (int)(bp - rve);
                    if (bp > zbuf + sizeof(zbuf)-1) bp = zbuf+ sizeof(zbuf)-1;
                    rve = zbuf + n;
                    for (d = zbuf + sizeof(zbuf) - 1; d >= rve; ) *d-- = '\0';
                    digits = zbuf;
                  }
		while (rve < bp)
			*rve++ = '0';
                *rve = '\0';            /* KNG 020304 re-terminate it */
	}
	*length = rve - digits;
	return (digits);
}

char *
ecvt(value,ndigits,decpt,sign)
double value; int ndigits; int *decpt; int *sign;
{
int length;
   return(cvt(value,ndigits,0,sign,decpt,'e',&length));
}

char *
fcvt(value,ndigits,decpt,sign)
double value; int ndigits; int *decpt; int *sign;
{
int length;
   return(cvt(value,ndigits,0,sign,decpt,'f',&length));
}

char *
gcvt(value,ndigits,buf)
double value; int ndigits; char *buf;
{
int length, sign, decpt;
char *d, *p=buf;

   d=cvt(value,ndigits,0,&sign,&decpt,'g',&length);
   if(sign) *(p++)='-';
   if(decpt==9999)/* FreeBSD __dtoa() code comment says this is infinity */
   {
      strcpy(p,InfStr);
      return(buf);
   }
   if(decpt<=0)
   {
      *(p++)='.';
      for(;decpt<0;decpt++,p++) *p='0';/* MAW 08-01-00 - change decpt-- to decpt++ */
      strcpy(p,d);
   }
   else
   {
      for(;*d!='\0';d++,p++,decpt--)
      {
         if(decpt==0) *(p++)='.';
         *p= *d;
      }
      for(;decpt>0;decpt--,p++)
         *p='0';
      *p='\0';
   }
   return(buf);
}
#endif /* !USE_SNPRINTF_NOT_CVT && __FreeBSD__ */

#if defined(USING_ALT_GCVT) && !defined(USE_SNPRINTF_NOT_CVT)
static char *
htpf_gcvt(num, ndig, buf)
double  num;
int     ndig;
char    *buf;
/* Replacement for gcvt() when unusable (e.g. Linux 2.0 calls sprintf(%g)).
 * May not be 100% accurate, and still calls sprintf().
 * KNG 20061003 Do _not_ recurse to htpf-style functions.
 */
{
  int   ex, n;
  char  *s, *d, *digitStart;

  if (ndig < 1) ndig = 1;
  sprintf(buf, "%.*e", ndig - 1, num);
  for (s = buf; *s != '\0' && *s != 'e' && *s != 'E'; s++);
  if (*s == '\0') goto done;                            /* no 'e': Inf? */
  ex = atoi(s + 1);

  /* Do %e or %f, mostly according to gcvt() and sprintf("%g") man page: */
  for (d = s - 1; d > buf && *d == '0'; d--);           /* find trailing 0s */
  if ((*d != '0' && *d != '.') || d == buf) d++;
  if (ex < -4 || ex >= ndig)                    /* use %e style */
    {
      if (d < s)
        {
          while (*s != '\0') *(d++) = *(s++);
          *d = '\0';
        }
    }
  else                                          /* use %f style */
    {
      n = ndig;
      if (ex < n) n--;
      n -= ex;
      if (n < 0) n = 0;
      sprintf(buf, "%.*f", n, num);
      d = CHARPN;
      for (s = buf; *s != '\0'; s++)                    /* zap trailing 0s */
        if (*s == '.') d = s;
      for (s--; s > buf && *s == '0'; s--);
      if (*s != '.' && (*s != '0' || s == buf)) s++;
      if (d != CHARPN)
        {
          *s = '\0';
          ndig++;
        }
      if (ex < 0) ndig -= ex;
      digitStart = buf;
      if (*digitStart == '-') digitStart++;     /* minus sign does not count*/
      if ((s - digitStart) > ndig)              /* excess precision */
        {
          s = digitStart + ndig;
          if (*s >= '5')
            while (s > digitStart)
              {
                if (*--s == '.')
                  {
                    if (s > digitStart) s--;
                    else break;
                  }
                if (++(*s) == '9' + 1) *s = '0';
                else break;
              }
          s = digitStart + ndig;
          while (*s != '\0') *(s++) = '0';
          for (d = digitStart; d < s && *d != '.'; d++);
          if (*d == '.')                                /* zap trailing 0s */
            {
              for (s--; s > digitStart && *s == '0'; s--);
              if (*s != '.' && (*s != '0' || s == digitStart)) s++;
              *s = '\0';
            }
        }
    }
done:
  return(buf);
}
#endif /* USING_ALT_GCVT && !USE_SNPRINTF_NOT_CVT */

/* ======================================================================== */
/* Everything below here uses TXcalloc() etc. directly, so that under
 * MEMDEBUG we can pass-thru file/line/memo args.  WTF should rename
 * actual TXcalloc() etc. functions so they can be called directly
 * with such args, *and* still have the macro versions for
 * MEMDEBUG-unaware functions above; could also then move this code up
 * since `undef TXcalloc' etc. would be unneeded.
 */

#ifdef MEMDEBUG
#  undef TxfmtcpDup
#  undef TxfmtcpClose
#  undef TXcalloc
#  undef TXfree
#endif /* !MEMDEBUG */

TXFMTCP *
TxfmtcpDup(CONST TXFMTCP   *src,        /* (in, opt.) object to copy */
           TXPMBUF         *pmbuf       /* (in) for error messages */
           TXALLOC_PROTO)
/* Allocates a duplicate of `src', or creates a new object if NULL.
 * Returns object, or NULL on error.
 */
{
  static CONST char     fn[] = "TxfmtcpDup";
  TXFMTCP               *fmtcp;

  if ((fmtcp = (TXFMTCP *)TXcalloc(pmbuf, fn, 1, sizeof(TXFMTCP)
                                   TXALLOC_ARGS_PASSTHRU)) ==
      TXFMTCPPN)
    goto err;
  if (src == TXFMTCPPN) src = &TxfmtcpDefault;

  if (src->apicp == APICPPN)
    fmtcp->apicp = APICPPN;
  else if ((fmtcp->apicp = dupapicp(src->apicp)) == APICPPN)
    goto err;

  if (src->htpfobj == HTPFOBJPN ||                  /* no `htpfobj' or */
      !(src->htpfobjDupOwned & 0x2))              /*   do not dup it */
    {
      fmtcp->htpfobj = src->htpfobj;
      fmtcp->htpfobjDupOwned = 0;
    }
  else                                          /* have dup-able `htpfobj' */
    {
      if ((fmtcp->htpfobj = duphtpfobj(src->htpfobj)) == HTPFOBJPN)
        goto err;
      fmtcp->htpfobjDupOwned = 0x3;
    }

  fmtcp->querySetCycleNum = src->querySetCycleNum;

  if (src->queryStyle == CHARPN ||
      src->queryStyle == TxfmtcpDefaultQueryStyle)
    fmtcp->queryStyle = src->queryStyle;
  else if (!(fmtcp->queryStyle = TXstrdup(pmbuf, fn, src->queryStyle)))
    goto err;

  if (src->querySetStyles == CHARPPN ||
      src->numQuerySetStyles <= 0)
    {
      fmtcp->querySetStyles = CHARPPN;
      fmtcp->numQuerySetStyles = 0;
    }
  else if (src->querySetStyles == (char **)TxfmtcpDefaultQuerySetStyles)
    {
      fmtcp->querySetStyles = (char **)TxfmtcpDefaultQuerySetStyles;
      fmtcp->numQuerySetStyles = TXFMTCP_DEFAULT_QUERYSETCYCLENUM;
    }
  else
    {
      if ((fmtcp->querySetStyles = TXdupStrList(pmbuf, src->querySetStyles,
                                          src->numQuerySetStyles)) == CHARPPN)
        goto err;
      fmtcp->numQuerySetStyles = src->numQuerySetStyles;
    }

  if (src->queryClass == CHARPN ||
      src->queryClass == TxfmtcpDefaultQueryClass)
    fmtcp->queryClass = src->queryClass;
  else if (!(fmtcp->queryClass = TXstrdup(pmbuf, fn, src->queryClass)))
    goto err;

  if (src->querySetClasses == CHARPPN ||
      src->numQuerySetClasses <= 0)
    {
      fmtcp->querySetClasses = CHARPPN;
      fmtcp->numQuerySetClasses = 0;
    }
  else if (src->querySetClasses == (char **)TxfmtcpDefaultQuerySetClasses)
    {
      fmtcp->querySetClasses = (char **)TxfmtcpDefaultQuerySetClasses;
      fmtcp->numQuerySetClasses = TXFMTCP_DEFAULT_QUERYSETCLASSES_NUM;
    }
  else
    {
      if ((fmtcp->querySetClasses = TXdupStrList(pmbuf, src->querySetClasses,
                                          src->numQuerySetClasses)) == CHARPPN)
        goto err;
      fmtcp->numQuerySetClasses = src->numQuerySetClasses;
    }

  fmtcp->highlightWithinDoc = src->highlightWithinDoc;
  fmtcp->queryFixupMode = src->queryFixupMode;

  goto done;

err:
  fmtcp = TxfmtcpClose(fmtcp TXALLOC_ARGS_PASSTHRU);
done:
  return(fmtcp);
}

TXFMTCP *
TxfmtcpClose(TXFMTCP *fmtcp TXALLOC_PROTO)
/* Frees a TXFMTCP object created with TxfmtcpDup().
 * Returns NULL.
 */
{
  if (fmtcp != TXFMTCPPN)
    {
      fmtcp->apicp = closeapicp(fmtcp->apicp);
      if (fmtcp->htpfobj != HTPFOBJPN)
        {
          if (fmtcp->htpfobjDupOwned & 0x1) closehtpfobj(fmtcp->htpfobj);
          fmtcp->htpfobj = HTPFOBJPN;
        }

      if (fmtcp->queryStyle != CHARPN &&
          fmtcp->queryStyle != TxfmtcpDefaultQueryStyle)
        fmtcp->queryStyle = TXfree(fmtcp->queryStyle TXALLOC_ARGS_PASSTHRU);

      if (fmtcp->querySetStyles != CHARPPN &&
          fmtcp->querySetStyles != (char **)TxfmtcpDefaultQuerySetStyles)
        TXfreeStrList(fmtcp->querySetStyles, fmtcp->numQuerySetStyles);

      if (fmtcp->queryClass != CHARPN &&
          fmtcp->queryClass != TxfmtcpDefaultQueryClass)
        fmtcp->queryClass = TXfree(fmtcp->queryClass TXALLOC_ARGS_PASSTHRU);

      if (fmtcp->querySetClasses != CHARPPN &&
          fmtcp->querySetClasses != (char **)TxfmtcpDefaultQuerySetClasses)
        TXfreeStrList(fmtcp->querySetClasses, fmtcp->numQuerySetClasses);

      fmtcp = TXfree(fmtcp TXALLOC_ARGS_PASSTHRU);
    }
  return(TXFMTCPPN);
}

/* ------------------------------------------------------------------------- */

#if defined(TEST) || defined(STANDALONE)

#ifndef SIGARGS
#  ifdef _WIN32
#    define SIGARGS     ARGS((HWND hwnd, UINT msg, UINT event, DWORD dwTime))
#    undef SIGTYPE
#    define SIGTYPE     void
#  else /* !_WIN32 */
#    define SIGARGS     ARGS((int sig))
#  endif /* !_WIN32 */
#endif /* !SIGARGS */

static char     *Progname = CHARPN;
static int      Argc = 0, Ret = 0, Arg = 0, Fpe = 0;
static char     *Format = CHARPN, **Args = CHARPPN;
static int      *ArgLen = INTPN, FileArgs = 0;
static HTPFF    HtFlags = HTPFF_PROMOTE_TO_HUGE;

#ifndef __alpha
extern char *basename ARGS((CONST char *));
#endif /* !__alpha */

#ifdef TEST
static int      Test = 0;

static void *cbtest ARGS((HTPFT type, HTPFW what, void *data, char **fmterr,
                          size_t *sz));
static void *
cbtest(type, what, data, fmterr, sz)
HTPFT   type;
HTPFW   what;
void    *data;
char    **fmterr;
size_t  *sz;
{
  static int    n;

  if (type != HTPFT_INT)
    {
      *fmterr = "(bad type)";
      return(NULL);
    }
  n = (int)data;
  return(&n);
}

void
test ARGS((void))
{
  char		*bob = "BOB WAS HERE";
  time_t	tim;
  float		f = 3.14159;
  int		width = 40, prec = 10;
  int           i;
  char          buf[100];

  tim = time(NULL);
  htpf("printf test:\n%80s\n%*.*s\n%s %s %d %R %c\ncurrent time: %at\nUT time: >>%40T<<\ne: %e  f: %f  g: %g\n%1.3e %1.3f %1.3g\nend of test\n",
       bob,
       width, prec, bob,
       "hello",
       "world",
       11115,
       1995,
       '?',
       "%A, %d-%b-%Y %H:%M:%S", tim,
       tim,
       f,
       f,
       f,
       f,
       f,
       f
       );
  htpf("non-ASCII HTML: \"%H\"\nnon-ASCII URL: \"%U\"\n",
       "newline ->\n<- fj\xF3rd",
       "newline ->\n<- fj\xF3rd"
       );

  htpf("URL escape test:\n");
  for (i = -10; i < 266; i++)
    htputcu(i, stdout);
  htpf("\nHTML escape test:\n");
  for (i = -10; i < 266; i++)
    htputch(i, stdout);
  htpf("\nMisc. URL functions (4 `+'s, newline, 3 `+'s):\n");
  htfputcu(' ', stdout);
  buf[0] = 'x';  htsputcu(' ', buf);    fputs(buf, stdout);
  htfputsu(" ", stdout);
  htputsu (" ");
  htputcu (' ', stdout);
  htputcharu(' ');

  htpf("\nMisc. HTML functions (4 `&gt;'s, newline, 3 `&gt;'s):\n");
  htfputch('>', stdout);
  buf[0] = 'x';  htsputch('>', buf);    fputs(buf, stdout);
  htfputsh(">", stdout);
  htputsh (">");
  buf[0] = 'x'; htsputsh(">", buf);     fputs(buf, stdout);
  htputch ('>', stdout);
  htputcharh('>');

  htpf("\nCallback test: (`xx123yy' `xx(bad type)yy'):\n");
  htcfpf(stdout, "xx%dyy\n", -1,  cbtest, (void *)123);
  htcfpf(stdout, "xx%fyy\n", -1, cbtest, (void *)123);
}
#endif  /* TEST */

static SIGTYPE
sigfpe_handler SIGARGS
{
  (void)sig;

  Fpe = 1;
  signal(SIGFPE, sigfpe_handler);
  SIGRETURN;
}

static void *printcb ARGS((HTPFT type, HTPFW what, void *data, char **fmterr,
                           size_t *sz));
static void *
printcb(type, what, data, fmterr, sz)
HTPFT   type;
HTPFW   what;
void    *data;
char    **fmterr;
size_t  *sz;
{
  static const char     whitespace[] = " \t\r\n\v\f";
  static HTPFARG        val;
  EPI_HUGEINT   h;
  EPI_HUGEUINT  hu;
  EPI_HUGEFLOAT hugeFloat;
  static char   *fmt = CHARPN;
  char          *s, *e;
  int           isnum = 1, len;

  (void)data;

  if (type == HTPFT_VOID)       /* no argument */
    {
      switch (what)
        {
        case HTPFW_START:
          fmt = *fmterr;
          Arg = 0;              /* reset count */
          break;
        case HTPFW_FMTERR:
          fputs(fmt, stderr);
          len = (int)strlen(fmt);
          if (len == 0 || fmt[len-1] != '\n') fputc('\n', stderr);
          for (len = (int)(*fmterr - fmt); len > 0; len--) fputc(' ', stderr);
          fputs("^-- error\n", stderr);
          fflush(stderr);
          goto err;
        default:                /* Should Not Happen(tm) */
          goto bigerr;
        }
      return(NULL);
    }

  if (Arg++ >= Argc)
    return(NULL);               /* too few args for format */

  s = Args[Arg - 1];
  Fpe = 0;
  if (*s == '\0')
    {
      isnum = 0;
      h = (EPI_HUGEINT)0;
      hu = (EPI_HUGEUINT)0;
      hugeFloat = (EPI_HUGEFLOAT)0.0;
    }
  else
    {
      int       errnum;

      h = TXstrtoh(s, CHARPN, &e, 0, &errnum);
      hu = (EPI_HUGEUINT)h;
      hugeFloat = (EPI_HUGEFLOAT)h;
      e += strspn(e, whitespace);
      if (*e != '\0' || errnum != 0)            /* parse/range error */
        {
          hu = TXstrtouh(s, CHARPN, &e, 0, &errnum);
          h = (EPI_HUGEINT)hu;
          hugeFloat = (EPI_HUGEFLOAT)hu;
          e += strspn(e, whitespace);
          if (*e != '\0' || errnum != 0)        /* parse/range error */
            {
              if (s[strcspn(s, ".eEiInN")] != '\0')
                {
                  hugeFloat = TXstrtohf(s, CHARPN, &e, &errnum);
                  h = (EPI_HUGEINT)hugeFloat;
                  hu = (EPI_HUGEUINT)hugeFloat;
                  e += strspn(e, whitespace);
                }
              if (*e != '\0' || errnum != 0) isnum = 0;
            }
        }
    }
  switch (type)
    {
    case HTPFT_UNSIGNED:
      if (!isnum) goto err;
      val.u = (unsigned)hu;
      if (Fpe || (EPI_HUGEUINT)val.u != hu) goto fpe;
      return((void *)&val.u);
    case HTPFT_INT:
      if (!isnum) goto err;
      val.i = (int)h;
      if (Fpe || (EPI_HUGEINT)val.i != h) goto fpe;
      return((void *)&val.i);
    case HTPFT_INTPTR:
      return((void *)&val.i);
    case HTPFT_LONG:
      if (isnum)
        {
          val.l = (long)h;
          if (Fpe || (EPI_HUGEINT)val.l != h) goto fpe;
        }
      else if (what == HTPFW_LOCATION)
        {
          val.l = TXparseLocation(s, &e, NULL, NULL);
          if (val.l == -1L) goto err;
          e += strspn(e, " \t\r\n\v\f");
          if (*e != '\0') goto err;
        }
      else
        goto err;
      return((void *)&val.l);
    case HTPFT_ULONG:
      if (!isnum) goto err;
      val.ul = (ulong)hu;
      if (Fpe || (EPI_HUGEUINT)val.ul != hu) goto fpe;
      return((void *)&val.ul);
#ifdef EPI_HAVE_LONG_LONG
    case HTPFT_LONGLONG:
      if (!isnum) goto err;
      val.ll = (long long)h;
      if (Fpe || (EPI_HUGEINT)val.ll != h) goto fpe;
      return((void *)&val.ll);
#endif /* EPI_HAVE_LONG_LONG */
#ifdef EPI_HAVE_UNSIGNED_LONG_LONG
    case HTPFT_ULONGLONG:
      if (!isnum) goto err;
      val.ull = (unsigned long long)hu;
      if (Fpe || (EPI_HUGEUINT)val.ull != hu) goto fpe;
      return((void *)&val.ull);
#endif /* EPI_HAVE_UNSIGNED_LONG_LONG */
    case HTPFT_HUGEINT:
      if (!isnum) goto err;
      val.hi = h;
      if (Fpe || val.hi != h) goto fpe;
      return((void *)&val.hi);
    case HTPFT_HUGEUINT:
      if (!isnum) goto err;
      val.hui = hu;
      if (Fpe || val.hui != hu) goto fpe;
      return((void *)&val.hui);
    case HTPFT_DOUBLE:
      if (isnum)
        val.d = (double)hugeFloat;
      else if (what == HTPFW_LATITUDE || what == HTPFW_LONGITUDE)
        {
          val.d = TXparseCoordinate(s, (what == HTPFW_LONGITUDE), &e);
          if (TXDOUBLE_IS_NaN(val.d)) goto err;
          e += strspn(e, " \t\r\n\v\f");
          if (*e != '\0') goto err;
        }
      else
        goto err;
      return((void *)&val.d);
    case HTPFT_HUGEFLOAT:
      if (isnum)
        val.hugeFloat = hugeFloat;
      else if (what == HTPFW_LATITUDE || what == HTPFW_LONGITUDE)
        {
          double        dVal;

          dVal = TXparseCoordinate(s, (what == HTPFW_LONGITUDE), &e);
          if (TXDOUBLE_IS_NaN(dVal)) goto err;
          e += strspn(e, " \t\r\n\v\f");
          if (*e != '\0') goto err;
          val.hugeFloat = (EPI_HUGEFLOAT)dVal;
        }
      else
        goto err;
      return((void *)&val.hugeFloat);
    case HTPFT_LONGDOUBLE:
#ifdef EPI_HAVE_LONG_DOUBLE
      if (isnum)
        val.longDouble = (long double)hugeFloat;
      else if (what == HTPFW_LATITUDE || what == HTPFW_LONGITUDE)
        {
          double        dVal;

          dVal = TXparseCoordinate(s, (what == HTPFW_LONGITUDE), &e);
          if (TXDOUBLE_IS_NaN(dVal)) goto err;
          e += strspn(e, " \t\r\n\v\f");
          if (*e != '\0') goto err;
          val.longDouble = (long double)dVal;
        }
      else
        goto err;
      return((void *)&val.longDouble);
#else /* !EPI_HAVE_LONG_DOUBLE */
      /* long double unsupported; cannot return one: */
      goto err;
#endif /* !EPI_HAVE_LONG_DOUBLE */
    case HTPFT_CHAR:
      if (isnum)
        {
          if (h < (EPI_HUGEINT)0 || h > (EPI_HUGEINT)255) goto err;
          val.c = (char)h;
          return((void *)&val.c);
        }
    case HTPFT_STR:
      *sz = ArgLen[Arg - 1];
      return(s);
    case HTPFT_TIME:
      if (isnum)
        {
#ifdef EPI_OS_TIME_T_IS_SIGNED
          val.t = (time_t)h;
          if (Fpe || (EPI_HUGEINT)val.t != h) goto fpe;
#else /* !EPI_OS_TIME_T_IS_SIGNED */
          val.t = (time_t)hu;
          if (Fpe || (EPI_HUGEUINT)val.t != hu) goto fpe;
#endif /* !EPI_OS_TIME_T_IS_SIGNED */
        }
      else
        {
          val.t = TXindparsetime(s, -1, 0x2, TXPMBUFPN);
          if (val.t == (time_t)(-1)) goto err;
        }
      return((void *)&val.t);
    case HTPFT_PTR:
      if (!isnum) goto err;
#if EPI_OS_VOIDPTR_BITS < EPI_HUGEINT_BITS
      if (h < (EPI_HUGEINT)0 || h >= ((EPI_HUGEINT)1 << EPI_OS_VOIDPTR_BITS))
        goto err;
#  if EPI_OS_VOIDPTR_BITS <= EPI_OS_ULONG_BITS
      val.vp = (void *)(unsigned long)hu;
#  else
      val.vp = (void *)hu;
#  endif
#elif EPI_OS_VOIDPTR_BITS > EPI_HUGEINT_BITS
      /* TXstrto[u]h() probably overflowed */
      goto err;
#else /* EPI_OS_VOIDPTR_BITS == EPI_HUGEINT_BITS */
      val.vp = (void *)hu;
#endif /* EPI_OS_VOIDPTR_BITS == EPI_HUGEINT_BITS */
      return(val.vp);
    err:
      *fmterr = "?";
      Ret = 1;
      return(NULL);
    default:
    bigerr:
      *fmterr = "(unknown format)";
      Ret = 1;
      return(NULL);
    }
fpe:
  *fmterr = "(FPE)";
  return(NULL);
}

static void usage ARGS((void));
static void
usage()
{
  char  verNumBuf[1024];

  TXgetTexisVersionNumString(verNumBuf, sizeof(verNumBuf),
                             TXbool_True /* vortexStyle */,
                             TXbool_False /* !forHtml */);
  htpf("Texis Printf Version %s %aT\n",
       verNumBuf, "|%Y%m%d", (time_t)TxSeconds);
#ifdef TEST
  htpf("Usage: %s [options] [format [arg ...]]\n", Progname);
  htpf("  -t       Run tests\n");
#else
  htpf("Usage: %s [options] format [arg ...]\n", Progname);
  htpf("  -f   All arguments are files not literals\n");
  htpf("  -F   Only arguments preceded by `file:' are files not literals\n");
  htpf("  --queryfixupmode{=| }{withindot|findsets}\n"
       "       Set queryfixupmode\n");
  htpf("  --promote-numbers{=| }{on|off}\n"
       "       Turn on/off int/double promotion to huge (default on)\n");
  htpf("  --traceencoding{=| }N\n"
       "       Set trace encoding (e.g. for %%z/gzip) level\n");
#endif
  fflush(stdout);
  exit(1);
}

static void parseargs ARGS((int argc, char *argv[], TXFMTCP *fmtcp));
static void
parseargs(argc, argv, fmtcp)
int     argc;
char    *argv[];
TXFMTCP *fmtcp;
{
#ifdef TEST
  int   i;
  char  *s;
#endif
  char  **ap;
  int   intVal, errNum;

  Progname = (char *)basename(argv[0]);
#ifdef TEST
  i = 1;
  for ( ; (i < argc) && *(s = argv[i]) == '-'; i++)
    {
      if (!s[0] || s[2])
        {
        unkopt:
          fprintf(stderr, "%s: Unknown option `%s' (-h for help)\n",
                  Progname, s);
          exit(1);
        }
      switch (s[1])
        {
        case 't':  Test = 1;  break;
        default:   goto unkopt;
        }
    }
  if (Test) return;
#endif  /* TEST */
  for (ap = argv + 1; *ap && **ap == '-'; ap++)
    {
      char      *optValue;

      if (strcmp(*ap, "-f") == 0)
        {
          FileArgs = 1;
          continue;
        }
      if (strcmp(*ap, "-F") == 0)
        {
          FileArgs = 2;
          continue;
        }
      switch (TXgetLongOptionValue(&ap, "--queryfixupmode", &optValue))
        {
        case 2:                                 /* match, success */
          /* NOTE: see also <fmtcp queryfixupmode> */
          if (strcmpi(optValue, "withindot") == 0)
            fmtcp->queryFixupMode = TXFMTCP_QUERYFIXUPMODE_WITHINDOT;
          else if (strcmpi(optValue, "findsets") == 0)
            fmtcp->queryFixupMode = TXFMTCP_QUERYFIXUPMODE_FINDSETS;
          else if (strcmpi(optValue, "default") == 0)
            fmtcp->queryFixupMode = TXFMTCP_DEFAULT_QUERYFIXUPMODE;
          else
            {
              htpf("Unknown --queryfixupmode value `%s' (-h for help)\n",
                   optValue);
              exit(1);
            }
          continue;
        case 1: usage(); break;
        }
      switch (TXgetLongOptionValue(&ap, "--promote-numbers", &optValue))
        {
        case 2:                                 /* match, success */
          switch (TXgetBooleanOrInt(TXPMBUFPN, CHARPN, "--promote-numbers",
                                    optValue, CHARPN, 3))
            {
            case 1: HtFlags |= HTPFF_PROMOTE_TO_HUGE; break;
            case 0: HtFlags &= ~HTPFF_PROMOTE_TO_HUGE; break;
            default: usage();                     break;
            }
          continue;
        case 1: usage(); break;                 /* match but error */
        }
      switch (TXgetLongOptionValue(&ap, "--traceencoding", &optValue))
        {
        case 2:                                 /* match, success */
          intVal = TXstrtoi(optValue, NULL, NULL,
                            (0 | TXstrtointFlag_ConsumeTrailingSpace |
                             TXstrtointFlag_TrailingSourceIsError), &errNum);
          if (errNum)
            {
              txpmbuf_putmsg(TXPMBUFPN, MERR + UGE, NULL,
                             "Invalid integer value `%s'", optValue);
              usage();
            }
          HtpfTraceEncoding = intVal;
          continue;
        case 1: usage(); break;                 /* match but error */
        }
      txpmbuf_putmsg(TXPMBUFPN, MERR + UGE, NULL,
                     "Unknown option `%s'", *ap);
      usage();
    }
  if (!*ap) usage();
  Format = *(ap++);
  Args = ap;
  Argc = (int)((argv + argc) - ap);
}

int
main(int argc, char *argv[])
{
  SIGTYPE       (*prevfpe) SIGARGS;
  char          *fmt = CHARPN;
  char          **args = CHARPPN, *arg;
  int           ch, argIsFile, i;
  size_t        fmtSz;
  FILE          *fp;
  long          n;
  HTBUF         *buf;
  int           argcStripped = 0, res;
  char          **argvStripped = CHARPPN;
  TXFMTCP       *fmtcp = NULL;

#ifdef EPI_TEST_PRINT_TO_RING_BUFFER
  {
    union
    {
      va_list argp;
      int dum;
    }
    dum;
    HTPFARG       harg;
    size_t        fullLen, head, tail, argn, j;
    char          mbuf[20];

    memset(mbuf, 'x', sizeof(mbuf));
    /* `X' marks past-end parts of buf; `x' marks untouched parts: */
    mbuf[0] = mbuf[sizeof(mbuf)-1] = 'X';
    head = tail = 5;

    dum.dum = 0;
    harg.cp = "This is a test";
    argn = 1;
    fullLen =
      TXvsnprintfToRingBuffer(mbuf + 1, sizeof(mbuf) - 2, head, &tail,
                              "%s", HTPFF_RDHTPFARGS, TXFMTCPPN, TXFMTSTATEPN,
                              dum.argp, &harg, &argn, TXPMBUFPN);
    printf("ret %d\n", (int)fullLen);
    for (j = 0; j < sizeof(mbuf); j++)
      printf("%c", (mbuf[j] == '\0' ? '0' :
                    (mbuf[j] >= ' ' && mbuf[j] <= '~' ? mbuf[j] : '.')));
    printf("\n");
    /* Caret points to head, pipe to tail: */
    printf("%*s^\n", (int)(head + 1), "");
    printf("%*s|\n", (int)(tail + 1), "");

    harg.cp = "So is this";
    argn = 1;
    fullLen =
      TXvsnprintfToRingBuffer(mbuf + 1, sizeof(mbuf) - 2, head, &tail,
                              "%s", HTPFF_RDHTPFARGS, TXFMTCPPN, TXFMTSTATEPN,
                              dum.argp, &harg, &argn, TXPMBUFPN);
    printf("ret %d\n", (int)fullLen);
    for (j = 0; j < sizeof(mbuf); j++)
      printf("%c", (mbuf[j] == '\0' ? '0' :
                    (mbuf[j] >= ' ' && mbuf[j] <= '~' ? mbuf[j] : '.')));
    printf("\n");
    /* Caret points to head, pipe to tail: */
    printf("%*s^\n", (int)(head + 1), "");
    printf("%*s|\n", (int)(tail + 1), "");

    exit(0);
#endif /* EPI_TEST_PRINT_TO_RING_BUFFER */

#  ifdef _WIN32
    __try
      {
#  endif /* _WIN32 */
    tx_setgenericsigs();
    if ((res = TXinitapp(TXPMBUFPN, "htpf", argc, argv, &argcStripped,
                         &argvStripped)) > 0)
      _exit(res);

#ifdef EPI_HAVE_SETLOCALE
  setlocale(LC_ALL, "");
#endif

#ifdef EPI_TEST_ENCODED_WORD_FORMAT
  {
#  define OUTBUFSZ      60
    char        outBuf[OUTBUFSZ + 1];
    size_t      outBufSz, inBufSz, outUsedLen, outTot = 0, numChars;
    char        *inBuf;
    CONST char  *inPtr;
    int         width = 0;
    size_t      len;
    UTF         flags, state = (UTF)0;
#define UTF8_STRLEN(s, e)  (numChars = -1,      \
  TXunicodeGetUtf8CharOffset((s), (e), &numChars), numChars)

    outBuf[sizeof(outBuf) - 1] = 'x';           /* overflow detector */

    /* htpf str [inLen [outLen ["final[,bufstop]" [width]]]] */
    inBuf = TXcesc2str(argv[1], -1, &len);
    inBufSz = (argc > 2 && *argv[2] ? atoi(argv[2]) : len);
    outBufSz = (argc > 3 && *argv[3] ? atoi(argv[3]) : OUTBUFSZ);
    if (outBufSz > OUTBUFSZ) outBufSz = OUTBUFSZ;
    flags = UTF_START;
    if (argc > 4 && strstri(argv[4], "final")) flags |= UTF_FINAL;
    if (argc > 4 && strstri(argv[4], "bufstop")) flags |= UTF_BUFSTOP;
    width = (argc > 5 && *argv[5] ? atoi(argv[5]) : 0);

    inPtr = inBuf;
    outUsedLen = TXutf8ToEncodedWord(outBuf, outBufSz, &outTot, &inPtr,
                                     inBufSz, flags, &state, width, HTPFOBJPN,
                                     TXPMBUFPN);
    htpf("in:        [%.*s]\n", (int)inBufSz, inBuf);
    htpf("in used:    %*s^\n", (int)UTF8_STRLEN(inBuf, inPtr), "");
    htpf("out given:  %*sv\n", (int)(outBufSz), "");
    htpf("out:       [%.*s]\n", (int)TX_MIN(outUsedLen, outBufSz), outBuf);
    htpf("out used:   %*s^\n", (int)UTF8_STRLEN(outBuf, outBuf + outUsedLen), "");
    if (outBuf[sizeof(outBuf) - 1] != 'x')
      htpf("*** output buffer overlow ***\n");
    return(0);
  }
#endif /* EPI_TEST_ENCODED_WORD_FORMAT */

  if (!(fmtcp = TxfmtcpDup(TXFMTCPPN, TXPMBUFPN TXALLOC_ARGS_DEFAULT)))
    goto err;

  parseargs(argcStripped, argvStripped, fmtcp);

#ifdef TEST
  if (Test)
    {
      test();
      exit(0);
      return(0);
    }
#endif

#ifdef _WIN32
  /* Prevent mapping of LF to CRLF; we want %s to print as-is, and can
   * always print proper platform EOL sequence with `j' flag:
   */
  if (setmode(fileno(stdout), O_BINARY) == -1)
    fprintf(stderr, "Warning: Could not set output mode to binary\n");
#endif /* _WIN32 */

  if ((fmt = TXcesc2str(Format, -1, &fmtSz)) == CHARPN ||
      (args = (char **)calloc(Argc, sizeof(char *))) == CHARPPN ||
      (ArgLen = (int *)calloc(Argc, sizeof(int))) == INTPN)
    goto err;
  for (i = 0; i < Argc; i++)
    {
      arg = Args[i];
      argIsFile = 0;
      if (FileArgs == 1)
        argIsFile = 1;
      else if (FileArgs == 2 && strncmp(Args[i], "file:", 5) == 0)
        {
          argIsFile = 1;
          arg += 5;
        }
      if (argIsFile)
        {
          if (*arg == '-' && arg[1] == '\0')    /* stdin */
            {
              if ((buf = openhtbuf()) == HTBUFPN) goto err;
              while ((ch = fgetc(stdin)) != EOF) htbuf_pf(buf, "%c", ch);
              htbuf_write(buf, "", 0);          /* ensure non-NULL buf */
              n = (long)htbuf_getdata(buf, &args[i], 1);
              closehtbuf(buf);
            }
          else                                          /* file */
            {
              if ((fp = fopen(arg, "rb")) == NULL)
                {
                  htfpf(stderr, "Cannot open file %s: %s\n",
                        arg, strerror(errno));
                  goto err;
                }
              fseek(fp, 0L, SEEK_END);
              n = ftell(fp);
              fseek(fp, 0L, SEEK_SET);
              if ((args[i] = (char *)malloc(n + 1L)) == CHARPN)
                goto err;
              if (fread(args[i], (size_t)1, (size_t)n, fp) != (size_t)n)
                {
                  htfpf(stderr, "Cannot read %s: %s\n",
                        arg, strerror(errno));
                  goto err;
                }
              args[i][n] = '\0';
            }
          ArgLen[i] = n;
        }
      else if (ArgLen[i] = (int)strlen(arg),
               (args[i] = strdup(arg)) == CHARPN)
          goto err;
    }
  Args = args;

  prevfpe = signal(SIGFPE, sigfpe_handler);
  Ret = 0;
  htcfpf(stdout, fmt, fmtSz, HtFlags, fmtcp, TXFMTSTATEPN, printcb, NULL);
  signal(SIGFPE, prevfpe);
  fflush(stdout);
  if (Arg < Argc)
    {
      htfpf(stderr, "Too many arguments for format\n");
      Ret = 1;
    }
  else if (Arg > Argc)
    {
      htfpf(stderr, "Too few arguments for format\n");
      Ret = 1;
    }
  goto done;

err:
  Ret = 0;
done:
  fmtcp = TxfmtcpClose(fmtcp TXALLOC_ARGS_DEFAULT);
  fmt = TXfree(fmt);
  args = TXfreeStrList(args, Argc);
  ArgLen = TXfree(ArgLen);
#  ifdef _WIN32
  }
    __except(TXgenericExceptionHandler(_exception_code(), _exception_info()))
      {
        /* TXgenericExceptionHandler() exits */
      }
#  endif /* _WIN32 */
  return(Ret);
}
#endif	/* TEST || STANDALONE */
