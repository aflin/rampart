#include "texint.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include "http.h"
#include "httpi.h"
#include "unicode.h"
#include "monochar.h"

static CONST char       Localhost[] = "localhost";
static CONST char       CommaWhitespace[] = ", \t\r\n\v\f";
static CONST char       Utf8[] = "UTF-8";
static CONST char       Utf16[] = "UTF-16";
static CONST char       Utf16Le[] = "UTF-16LE";
static CONST char       Utf16Be[] = "UTF-16BE";
static CONST char       Iso[] = "ISO-8859-1";
static CONST char       UsAscii[] = "US-ASCII";
static CONST char       TruncChar[] = "Truncated character sequence";
static CONST char       InvalidChar[] = "Invalid character sequence";
static CONST char       RangeChar[] = "Out-of-range character sequence";
static CONST char       RangeEsc[] = "Out-of-range HTML escape sequence";
static CONST char       InvalidUnicode[] = "Invalid Unicode value";
static CONST char       InvalidXmlChar[] = "Invalid XML character";

/* Nonzero if given character doesn't need to be HTML-escaped: */
const char HtmlNoEsc[256] =
{
  0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
#if TX_VERSION_MAJOR >= 5
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
#else /* TX_VERSION_MAJOR < 5 */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
#endif /* TX_VERSION_MAJOR < 5 */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
const char      TxIsValidXmlCodepointIso[256] =
{
  0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
typedef struct HTESC_tag
{
  /* note use of arrays instead of pointers, struct ordering to save space: */
  char  seq[10];        /* the escape sequence */
  char  asciistr[6];    /* 7-bit ASCII string replacement */
  int   unicode;        /* Unicode char */
}
HTESC;

/* List of HTML escape sequences.
 * NOTE: If any changes are made, Htescindex[] _MUST_ be regenerated
 * NOTE: This list _MUST_ be sorted case-sensitive ascending by `seq'.
 */
static CONST HTESC      Htesclist[] =
{
  /* HTESC-START placeholder */
  { "AElig",    "AE",   198   }, /* capital AE diphthong (ligature) */
  { "Aacute",   "A",    193   }, /* capital A, acute accent */
  { "Acirc",    "A",    194   }, /* capital A, circumflex accent */
  { "Agrave",   "A",    192   }, /* capital A, grave accent */
  { "Alpha",    "A",    913   }, /* greek capital letter alpha */
  { "Aring",    "A",    197   }, /* capital A, ring */
  { "Atilde",   "A",    195   }, /* capital A, tilde */
  { "Auml",     "A",    196   }, /* capital A, dieresis or umlaut mark */
  { "Beta",     "B",    914   }, /* greek capital letter beta */
  { "Ccedil",   "C",    199   }, /* capital C, cedilla */
  { "Chi",      "X",    935   }, /* greek capital letter chi */
  { "Dagger",   "**",   8225  }, /* double dagger */
  { "Delta",    "D",    916   }, /* greek capital letter delta */
  { "ETH",      "D",    208   }, /* capital Eth, Icelandic */
  { "Eacute",   "E",    201   }, /* capital E, acute accent */
  { "Ecirc",    "E",    202   }, /* capital E, circumflex accent */
  { "Egrave",   "E",    200   }, /* capital E, grave accent */
  { "Epsilon",  "E",    917   }, /* greek capital letter epsilon */
  { "Eta",      "H",    919   }, /* greek capital letter eta */
  { "Euml",     "E",    203   }, /* capital E, dieresis or umlaut mark */
  { "Gamma",    "G",    915   }, /* greek capital letter gamma */
  { "Iacute",   "I",    205   }, /* capital I, acute accent */
  { "Icirc",    "I",    206   }, /* capital I, circumflex accent */
  { "Igrave",   "I",    204   }, /* capital I, grave accent */
  { "Iota",     "I",    921   }, /* greek capital letter iota */
  { "Iuml",     "I",    207   }, /* capital I, dieresis or umlaut mark */
  { "Kappa",    "K",    922   }, /* greek capital letter kappa */
  { "Lambda",   "L",    923   }, /* greek capital letter lambda */
  { "Mu",       "M",    924   }, /* greek capital letter mu */
  { "Ntilde",   "N",    209   }, /* capital N, tilde */
  { "Nu",       "N",    925   }, /* greek capital letter nu */
  { "OElig",    "OE",   338   }, /* latin capital ligature OE */
  { "Oacute",   "O",    211   }, /* capital O, acute accent */
  { "Ocirc",    "O",    212   }, /* capital O, circumflex accent */
  { "Ograve",   "O",    210   }, /* capital O, grave accent */
  { "Omega",    "W",    937   }, /* greek capital letter omega */
  { "Omicron",  "O",    927   }, /* greek capital letter omicron */
  { "Oslash",   "O",    216   }, /* capital O, slash */
  { "Otilde",   "O",    213   }, /* capital O, tilde */
  { "Ouml",     "O",    214   }, /* capital O, dieresis or umlaut mark */
  { "Phi",      "P",    934   }, /* greek capital letter phi */
  { "Pi",       "P",    928   }, /* greek capital letter pi */
  { "Prime",    "\"",   8243  }, /* double prime = seconds = inches */
  { "Psi",      "Y",    936   }, /* greek capital letter psi */
  { "Rho",      "P",    929   }, /* greek capital letter rho */
  { "Scaron",   "S",    352   }, /* latin capital letter S with caron */
  { "Sigma",    "E",    931   }, /* greek capital letter sigma */
  { "THORN",    "P",    222   }, /* capital THORN, Icelandic */
  { "Tau",      "T",    932   }, /* greek capital letter tau */
  { "Theta",    "O",    920   }, /* greek capital letter theta */
  { "Uacute",   "U",    218   }, /* capital U, acute accent */
  { "Ucirc",    "U",    219   }, /* capital U, circumflex accent */
  { "Ugrave",   "U",    217   }, /* capital U, grave accent */
  { "Upsilon",  "Y",    933   }, /* greek capital letter upsilon */
  { "Uuml",     "U",    220   }, /* capital U, dieresis or umlaut mark */
  { "Xi",       "X",    926   }, /* greek capital letter xi */
  { "Yacute",   "Y",    221   }, /* capital Y, acute accent */
  { "Yuml",     "Y",    376   }, /* latin capital letter Y with diaeresis */
  { "Zeta",     "Z",    918   }, /* greek capital letter zeta */
  { "aacute",   "a",    225   }, /* small a, acute accent */
  { "acirc",    "a",    226   }, /* small a, circumflex accent */
  { "acute",    "",     180   }, /* acute accent */
  { "aelig",    "ae",   230   }, /* small ae diphthong (ligature) */
  { "agrave",   "a",    224   }, /* small a, grave accent */
  { "alefsym",  "",     8501  }, /* alef symbol = first transfinite cardinal*/
  { "alpha",    "a",    945   }, /* greek small letter alpha */
  { "amp",      "&",    38    }, /* ampersand */
  { "and",      "&&",   8743  }, /* logical and = wedge */
  { "ang",      "",     8736  }, /* angle */
  { "apos",     "'",    39    }, /* apostrophe */
  { "aring",    "a",    229   }, /* small a, ring */
  { "asymp",    "~=",   8776  }, /* almost equal to = asymptotic to */
  { "atilde",   "a",    227   }, /* small a, tilde */
  { "auml",     "a",    228   }, /* small a, dieresis or umlaut mark */
  { "bdquo",    "\"",   8222  }, /* double low-9 quotation mark */
  { "beta",     "b",    946   }, /* greek small letter beta  */
  { "brvbar",   "|",    166   }, /* broken (vertical) bar */
  { "bull",     "*",    8226  }, /* bullet = black small circle */
  { "cap",      "^",    8745  }, /* intersection = cap */
#ifndef HT_UNICODE
  { "cbsp",     " ",    32    }, /* conditional breaking space (non-std.) */
#endif /* HT_UNICODE */
  { "ccedil",   "c",    231   }, /* small c, cedilla */
  { "cedil",    "",     184   }, /* cedilla */
  { "cent",     "c",    162   }, /* cent sign */
  { "chi",      "x",    967   }, /* greek small letter chi */
  { "circ",     "^",    710   }, /* modifier letter circumflex accent */
  { "clubs",    "*",    9827  }, /* black club suit = shamrock */
  { "cong",     "~=",   8773  }, /* approximately equal to */
  { "copy",     "(c)",  169   }, /* copyright sign */
  { "crarr",    "",     8629  }, /* downwards arrow with corner leftwards */
  { "cup",      "",     8746  }, /* union = cup */
  { "curren",   "$",    164   }, /* general currency sign */
  { "dArr",     "",     8659  }, /* downwards double arrow */
  { "dagger",   "*",    8224  }, /* dagger */
  { "darr",     "",     8595  }, /* downwards arrow */
#ifdef HT_UNICODE
  { "deg",      "deg.", 176   }, /* degree sign */
#else /* !HT_UNICODE */
  { "deg",      "",     176   }, /* degree sign */
#endif /* !HT_UNICODE */
  { "delta",    "d",    948   }, /* greek small letter delta */
  { "diams",    "*",    9830  }, /* black diamond suit */
  { "divide",   "/",    247   }, /* divide sign */
  { "eacute",   "e",    233   }, /* small e, acute accent */
  { "ecirc",    "e",    234   }, /* small e, circumflex accent */
  { "egrave",   "e",    232   }, /* small e, grave accent */
#ifndef HT_UNICODE
  { "emdash",   "--",   151   }, /* em dash (non-std.) */
#endif /* HT_UNICODE */
  { "empty",    "",     8709  }, /* empty set = null set = diameter */
  { "emsp",     "  ",   8195  }, /* em space */
#ifndef HT_UNICODE
  { "endash",   "-",    150   }, /* en dash (non-std.) */
#endif /* HT_UNICODE */
  { "ensp",     " ",    8194  }, /* en space */
  { "epsilon",  "e",    949   }, /* greek small letter epsilon */
  { "equiv",    "==",   8801  }, /* identical to */
  { "eta",      "h",    951   }, /* greek small letter eta */
  { "eth",      "o",    240   }, /* small eth, Icelandic */
  { "euml",     "e",    235   }, /* small e, dieresis or umlaut mark */
  { "euro",     "Eu",   8364  }, /* euro sign */
  { "exist",    "",     8707  }, /* there exists */
  { "fnof",     "fn",   402   }, /* latin small f with hook = function */
  { "forall",   "",     8704  }, /* for all */
  { "frac12",   "1/2",  189   }, /* fraction one-half */
  { "frac14",   "1/4",  188   }, /* fraction one-quarter */
  { "frac34",   "3/4",  190   }, /* fraction three-quarters */
  { "frasl",    "/",    8260  }, /* fraction slash */
  { "gamma",    "g",    947   }, /* greek small letter gamma */
  { "ge",       ">=",   8805  }, /* greater-than or equal to */
  { "gt",       ">",    62    }, /* greater-than sign */
  { "hArr",     "",     8660  }, /* left right double arrow */
  { "harr",     "",     8596  }, /* left right arrow */
  { "hearts",   "*",    9829  }, /* black heart suit = valentine */
  { "hellip",   "...",  8230  }, /* horizontal ellipsis = three dot leader */
  { "iacute",   "i",    237   }, /* small i, acute accent */
  { "icirc",    "i",    238   }, /* small i, circumflex accent */
  { "iexcl",    "!",    161   }, /* inverted exclamation mark */
  { "igrave",   "i",    236   }, /* small i, grave accent */
  { "image",    "I",    8465  }, /* blackletter capital I = imaginary part */
  { "infin",    "",     8734  }, /* infinity */
  { "int",      "",     8747  }, /* integral */
  { "iota",     "i",    953   }, /* greek small letter iota */
  { "iquest",   "?",    191   }, /* inverted question mark */
  { "isin",     "",     8712  }, /* element of */
  { "iuml",     "i",    239   }, /* small i, dieresis or umlaut mark */
  { "kappa",    "k",    954   }, /* greek small letter kappa */
  { "lArr",     "",     8656  }, /* leftwards double arrow */
  { "lambda",   "l",    955   }, /* greek small letter lambda */
  { "lang",     "<",    9001  }, /* left-pointing angle bracket = bra */
  { "laquo",    "<<",   171   }, /* angle quotation mark, left */
  { "larr",     "",     8592  }, /* leftwards arrow */
  { "lceil",    "",     8968  }, /* left ceiling = apl upstile */
  { "ldquo",    "``",   8220  }, /* left double quotation mark */
  { "le",       "<=",   8804  }, /* less-than or equal to */
  { "lfloor",   "",     8970  }, /* left floor = apl downstile */
  { "lowast",   "*",    8727  }, /* asterisk operator */
  { "loz",      "",     9674  }, /* lozenge */
  { "lrm",      "",     8206  }, /* left-to-right mark */
  { "lsaquo",   "<",    8249  }, /* single left-pointing angle quot. mark */
  { "lsquo",    "`",    8216  }, /* left single quotation mark */
  { "lt",       "<",    60    }, /* less-than sign */
  { "macr",     "-",    175   }, /* macron */
  { "mdash",    "--",   8212  }, /* em dash */
  { "micro",    "u",    181   }, /* micro sign */
  { "middot",   ".",    183   }, /* middle dot */
  { "minus",    "-",    8722  }, /* minus sign */
  { "mu",       "u",    956   }, /* greek small letter mu */
  { "nabla",    "",     8711  }, /* nabla = backward difference */
  { "nbsp",     " ",    160   }, /* non-breaking space */
  { "ndash",    "-",    8211  }, /* en dash */
  { "ne",       "!=",   8800  }, /* not equal to */
  { "ni",       "",     8715  }, /* contains as member */
  { "not",      "-",    172   }, /* not sign */
  { "nsub",     "",     8836  }, /* not a subset of */
  { "ntilde",   "n",    241   }, /* small n, tilde */
  { "nu",       "n",    957   }, /* greek small letter nu */
  { "oacute",   "o",    243   }, /* small o, acute accent */
  { "ocirc",    "o",    244   }, /* small o, circumflex accent */
  { "oelig",    "oe",   339   }, /* latin small ligature oe */
  { "ograve",   "o",    242   }, /* small o, grave accent */
  { "oline",    "_",    8254  }, /* overline = spacing overscore */
  { "omega",    "w",    969   }, /* greek small letter omega */
  { "omicron",  "o",    959   }, /* greek small letter omicron */
  { "oplus",    "(+)",  8853  }, /* circled plus = direct sum */
  { "or",       "||",   8744  }, /* logical or = vee */
#ifdef HT_UNICODE
  { "ordf",     "a",    170   }, /* ordinal indicator, feminine */
  { "ordm",     "o",    186   }, /* ordinal indicator, masculine */
#else /* !HT_UNICODE */
  { "ordf",     "",     170   }, /* ordinal indicator, feminine */
  { "ordm",     "",     186   }, /* ordinal indicator, masculine */
#endif /* !HT_UNICODE */
  { "oslash",   "o",    248   }, /* small o, slash */
  { "otilde",   "o",    245   }, /* small o, tilde */
  { "otimes",   "(x)",  8855  }, /* circled times = vector product */
  { "ouml",     "o",    246   }, /* small o, dieresis or umlaut mark */
  { "para",     "P",    182   }, /* pilcrow (paragraph sign) */
  { "part",     "",     8706  }, /* partial differential */
  { "permil",   "o/oo", 8240  }, /* per mille sign */
  { "perp",     "",     8869  }, /* up tack = orthogonal to = perpendicular */
  { "phi",      "p",    966   }, /* greek small letter phi */
  { "pi",       "p",    960   }, /* greek small letter pi */
  { "piv",      "p",    982   }, /* greek pi symbol */
  { "plusmn",   "+/-",  177   }, /* plus-or-minus sign */
  { "pound",    "#",    163   }, /* pound sterling sign */
  { "prime",    "'",    8242  }, /* prime = minutes = feet */
  { "prod",     "*",    8719  }, /* n-ary product = product sign */
  { "prop",     "",     8733  }, /* proportional to */
  { "psi",      "y",    968   }, /* greek small letter psi */
  { "quot",     "\"",   34    }, /* double-quote mark */
  { "rArr",     "",     8658  }, /* rightwards double arrow */
  { "radic",    "",     8730  }, /* square root = radical sign */
  { "rang",     ">",    9002  }, /* right-pointing angle bracket = ket */
  { "raquo",    ">>",   187   }, /* angle quotation mark, right */
  { "rarr",     "",     8594  }, /* rightwards arrow */
  { "rceil",    "",     8969  }, /* right ceiling */
  { "rdquo",    "''",   8221  }, /* right double quotation mark */
  { "real",     "R",    8476  }, /* blackletter capital R = real part symbol*/
  { "reg",      "(r)",  174   }, /* registered sign */
  { "rfloor",   "",     8971  }, /* right floor */
  { "rho",      "p",    961   }, /* greek small letter rho */
  { "rlm",      "",     8207  }, /* right-to-left mark */
  { "rsaquo",   ">",    8250  }, /* single right-pointing angle quot. mark */
  { "rsquo",    "'",    8217  }, /* right single quotation mark */
  { "sbquo",    "'",    8218  }, /* single low-9 quotation mark */
  { "scaron",   "s",    353   }, /* latin small letter s with caron */
  { "sdot",     "*",    8901  }, /* dot operator */
#ifdef HT_UNICODE
  { "sect",     "S",    167   }, /* section sign */
#else /* !HT_UNICODE */
  { "sect",     "",     167   }, /* section sign */
#endif /* !HT_UNICODE */
  { "shy",      "-",    173   }, /* soft hyphen */
  { "sigma",    "e",    963   }, /* greek small letter sigma */
  { "sigmaf",   "e",    962   }, /* greek small letter final sigma */
  { "sim",      "~",    8764  }, /* tilde operator = varies with=similar to */
  { "spades",   "*",    9824  }, /* black spade suit */
  { "sub",      "",     8834  }, /* subset of */
  { "sube",     "",     8838  }, /* subset of or equal to */
  { "sum",      "",     8721  }, /* n-ary summation */
  { "sup",      "",     8835  }, /* superset of */
  { "sup1",     "1",    185   }, /* superscript one */
  { "sup2",     "2",    178   }, /* superscript two */
  { "sup3",     "3",    179   }, /* superscript three */
  { "supe",     "",     8839  }, /* superset of or equal to */
  { "szlig",    "ss",   223   }, /* small sharp s, German (sz ligature) */
  { "tau",      "t",    964   }, /* greek small letter tau */
  { "there4",   "",     8756  }, /* therefore */
  { "theta",    "o",    952   }, /* greek small letter theta */
  { "thetasym", "o",    977   }, /* greek small letter theta symbol */
  { "thinsp",   " ",    8201  }, /* thin space */
  { "thorn",    "p",    254   }, /* small thorn, Icelandic */
  { "tilde",    "~",    732   }, /* small tilde */
  { "times",    "x",    215   }, /* multiply sign */
#ifdef HT_UNICODE
  { "trade",    "(tm)", 8482  }, /* trade mark sign */
#else /* !HT_UNICODE */
  { "trade",    "(tm)", 153   }, /* (non-std.) */
#endif /* !HT_UNICODE */
  { "uArr",     "",     8657  }, /* upwards double arrow */
  { "uacute",   "u",    250   }, /* small u, acute accent */
  { "uarr",     "",     8593  }, /* upwards arrow */
  { "ucirc",    "u",    251   }, /* small u, circumflex accent */
  { "ugrave",   "u",    249   }, /* small u, grave accent */
  { "uml",      "",     168   }, /* umlaut (dieresis) */
  { "upsih",    "y",    978   }, /* greek upsilon with hook symbol */
  { "upsilon",  "y",    965   }, /* greek small letter upsilon */
  { "uuml",     "u",    252   }, /* small u, dieresis or umlaut mark */
  { "weierp",   "P",    8472  }, /* script capital P = power set */
  { "xi",       "x",    958   }, /* greek small letter xi */
  { "yacute",   "y",    253   }, /* small y, acute accent */
  { "yen",      "Y",    165   }, /* yen sign */
  { "yuml",     "y",    255   }, /* small y, dieresis or umlaut mark */
  { "zeta",     "z",    950   }, /* greek small letter zeta */
  { "zwj",      "",     8205  }, /* zero width joiner */
  { "zwnj",     "",     8204  }, /* zero width non-joiner */
  /* HTESC-END placeholder */
};
#define HTESC_LNUM      (sizeof(Htesclist)/sizeof(Htesclist[0]))
/* Index into `Htesclist' of char, for char < 256 (-1 == no entry).
 * This list is generated by the following:
   grep -v '^# *[ip][nr]' htparse.c|/bin/awk -f $SRCDIR/ifdef -v defined= |
   awk '/HTESC-END/{n=i-1; c=1; printf(" "); for (i=0;i<256;i++){for(j=0;j<n
   &&chars[j]!=i;j++);x=sprintf(" %d,", (j<n?j:-1));l=length(x);if(c+l >78)
   {printf("\n "); c=1};printf("%s",x);c+=l;}printf("\n"); exit;}{if(i)
   {if($2<=p)printf("%s out of order\n",$2);p=$2;gsub("^[^,]*,[^,]*, *", "");
   chars[i++-1]=0+$0;}} /HTESC-START/{i=1}'
 */
static CONST short      Htescindex[] =
{
#ifdef HT_UNICODE
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 193, -1, -1, -1,
  66, 69, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, 149, -1, 118, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, 157, 125, 81, 188, 89, 247, 76, 211,
  240, 86, 174, 138, 161, 212, 202, 150, 93, 187, 222, 223, 61, 152, 180, 153,
  80, 221, 175, 197, 113, 112, 114, 131, 3, 1, 2, 6, 7, 5, 0, 9, 16, 14, 15,
  19, 23, 21, 22, 25, 13, 29, 34, 32, 33, 38, 39, 233, 37, 52, 50, 51, 54, 56,
  47, 225, 63, 59, 60, 72, 73, 70, 62, 79, 99, 97, 98, 107, 126, 123, 124,
  133, 106, 163, 168, 165, 166, 177, 179, 96, 176, 239, 236, 238, 243, 246,
  231, 248,
#else /* !HT_UNICODE */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 79, -1, 196, -1, -1, -1,
  66, 69, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, 152, -1, 121, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 104,
  101, -1, 237, -1, -1, -1, -1, -1, -1, 160, 128, 82, 191, 90, 250, 76, 214,
  243, 87, 177, 141, 164, 215, 205, 153, 94, 190, 225, 226, 61, 155, 183, 156,
  81, 224, 178, 200, 116, 115, 117, 134, 3, 1, 2, 6, 7, 5, 0, 9, 16, 14, 15,
  19, 23, 21, 22, 25, 13, 29, 34, 32, 33, 38, 39, 236, 37, 52, 50, 51, 54, 56,
  47, 228, 63, 59, 60, 72, 73, 70, 62, 80, 100, 98, 99, 110, 129, 126, 127,
  136, 109, 166, 171, 168, 169, 180, 182, 97, 179, 242, 239, 241, 246, 249,
  234, 251,
#endif /* !HT_UNICODE */
};

/* WTF this should be merged into HTCSINFO someday? */
typedef struct HTCHARSETINFO_tag
{
  HTCHARSETFUNC         *cs_to_utf8, *utf8_to_cs;
  UTF                   cs_to_utf8flags, utf8_to_csflags;
  CONST EPI_UINT16      *unicodemap;
  CONST byte            *unicodeindex;
  int                   numunicodeindex;
}
HTCHARSETINFO;
#define HTCHARSETINFOPN ((HTCHARSETINFO *)NULL)

static CONST HTCHARSETINFO      HtCharsetInfo[HTCHARSET_NUM] =
{
#undef I
#define I(tok, cn, tf, tl, ff, fl)  \
  { tf,ff,tl,fl,HtUnicodeMap_##tok, HtUnicodeIndex_##tok, NUM_##tok##_CODES },
  HTCHARSET_LIST
#undef I
};


/* Other control chars considered ok in HTML: see also htpf %!H */
static CONST char       Htokctrl[] = "\t\n\r\f";  /* wtf \f not really legal*/
#define HT_OKCTRL(ch)   ((byte)(ch) <= '~' && \
  ((byte)(ch) >= ' ' || ((ch) != '\0' && strchr(Htokctrl, ch) != CHARPN)))



char *
html2esc(ch, buf, bufsz, pmbuf)
unsigned        ch;
char            *buf;           /* scratch buf (EPI_OS_INT_BITS/3 + 6) */
size_t          bufsz;          /* size of `buf' */
TXPMBUF         *pmbuf;         /* (out) (optional) putmsg buffer */
/* Escapes `ch' for HTML, returning static string.  `ch' may be nul,
 * or > 256.
 */
{
  char  *d;

  if (ch < 256 && HtmlNoEsc[ch])
    {
      if (bufsz < 2) goto toosmall;
      buf[0] = ch;
      buf[1] = '\0';
      return(buf);
    }
  switch (ch)
    {
    case (unsigned)'<':  return("&lt;");
    case (unsigned)'>':  return("&gt;");
    case (unsigned)'&':  return("&amp;");
    case (unsigned)'"':  return("&quot;");
    default:
      if (bufsz < 5) goto toosmall;
      d = buf + bufsz;
      *(--d) = '\0';
      *(--d) = ';';
      do                                /* convert to decimal */
        {
          *(--d) = '0' + (ch % 10);
          ch /= 10;
        }
      while (ch);
      *(--d) = '#';
      *(--d) = '&';
      if (d < buf)                      /* it's actually too late... */
        {
        toosmall:
          txpmbuf_putmsg(pmbuf, MERR + MAE, "html2esc", "Buffer overflow");
          return("?");
        }
      return(d);
    }
}

char *
htesc2html(s, e, do8bit, szp, valp, buf, bufsz)
CONST char      *s, *e;
int             do8bit;
size_t          *szp;           /* set to length of returned string */
int             *valp;          /* (optional) (ret) Unicode (-1==bad input)*/
char            *buf;           /* scratch buf that may be used (20+ chars) */
size_t          bufsz;          /* size of `buf' */
/* `s' points to HTML escape sequence (e.g. "quot") ending before `e'.
 * Returns replacement string (without ctrl chars) or NULL if unknown/>8-bit.
 * (if >8-bit, still sets `*valp'.)
 * If `do8bit', 8-bit ISO Latin 1 char is returned, otherwise
 * replacement string is 7-bit "plain" (i.e. without accents, copyright
 * symbol becomes "(c)", etc.).
 */
{
  CONST HTESC	*esc;
  char		*d, *pe;
  int		i, l, r, cmp, len, ishex;
  long          val;

  len = e - s;
  if (len <= 0)
    {
    bad:
      if (valp != INTPN) *valp = -1;
      *szp = 0;
      return(CHARPN);
    }
  /* First see if it's a numeric escape: */
  if (*s == '#') {
    ishex = 0;
    s++;
    if (s < e && (*s == 'x' || *s == 'X'))      /* hex escape */
      {
        ishex = 1;
        s++;
      }
    for (d = buf;
         (s < e) && (d < buf + bufsz - 1) &&
           ((*s >= '0' && *s <= '9') || (ishex && ((*s >= 'A' && *s <= 'F') ||
                      (*s >= 'a' && *s <= 'f'))));
	 s++, d++)
      *d = *s;
    *d = '\0';
    val = strtol(buf, &pe, (ishex ? 16 : 10));
    /* WTF if we didn't use all chars to `e', then let call know somehow: */
    if (d == buf || *pe != '\0' || val >= 0x7fffffffL || val < 0L)
      goto bad;
    if (valp != INTPN) *valp = (int)val;
    if ((unsigned)val < (unsigned)256) {        /* valid ISO-8859-1 char */
      if (do8bit)
        {
          *(byte *)buf = (byte)val;
          *szp = 1;
          return(buf);
        }
      /* See if it's an escape code: */
      if ((l = (int)Htescindex[(unsigned)val]) >= 0) {
        esc = Htesclist + l;
      retrepl:
        strcpy(buf, esc->asciistr);
        *szp = strlen(buf);
        return(buf);
      }
      /* It's not; return as-is or zap if ctrl: */
      *(byte *)buf = (HT_OKCTRL((byte)val) ? (byte)val : ' ');
      *szp = 1;
      return(buf);
    }
  range:
    *szp = 0;
    return(CHARPN);                             /* out-of-8-bit-range escape*/
  }

  /* Not numeric; do bsearch on named escapes: */
  l = 0;
  r = HTESC_LNUM;
  while (l < r) {
    i = ((l + r) >> 1);
    esc = Htesclist + i;
    cmp = strncmp(s, esc->seq, len);
    if (cmp < 0) r = i;
    else if (cmp > 0) l = i + 1;
    else if (esc->seq[len] != '\0') r = i;	/* i.e. actually cmp < 0 */
    else {                                      /* match */
#ifndef HT_UNICODE
      if (esc->unicode == 8194 || esc->unicode == 8195)
        {
          if (valp != INTPN) *valp = ' ';
          goto retrepl;
        }
      if (esc->unicode >= 256) break;           /* pretend we didn't see it */
#endif /* HT_UNICODE */
      if (valp != INTPN) *valp = esc->unicode;
      if (do8bit)
        {
          if (esc->unicode >= 256) goto range;
          buf[0] = esc->unicode;
          *szp = 1;
          return(buf);
        }
      goto retrepl;
    }
  }
  if (valp != INTPN) *valp = -1;
  *szp = 0;
  return(CHARPN);				/* no match */
}

/* Encodes 21-bit UCS-4 character `ch' to UTF-8 buffer `buf'.
 * NOTE: TX_IS_VALID_UNICODE_CODEPOINT(ch) is assumed to be true:
 * NOTE: this should be merged with TX_UNICODE_ENCODE_UTF8_CHAR():
 */
#define UTF8_ENCODECHAR(buf, i, bufsz, ch, flags)                       \
  if ((ch) < 0x80) goto chkit;                  /* encode as-is */      \
  if ((ch) < 0x800)                             /* 2-byte UTF-8 seq */  \
    {                                                                   \
      if ((i) < (bufsz)) (buf)[(i)] = (0xC0 | ((ch) >> 6));  (i)++;     \
      goto last1;                                                       \
    }                                                                   \
  else if ((ch) < 0x10000)                      /* 3-byte UTF-8 seq */  \
    {                                                                   \
      if ((i) < (bufsz)) (buf)[(i)] = (0xE0 | ((ch) >> 12));  (i)++;    \
      goto last2;                                                       \
    }                                                                   \
  else                                          /* 4-byte UTF-8 seq */  \
    {                                                                   \
      if ((i) < (bufsz)) (buf)[(i)] = (0xF0 | ((ch) >> 18));  (i)++;    \
      if ((i) < (bufsz)) (buf)[(i)] = (0x80 | (((ch) >> 12) & 0x3F)); (i)++; \
    last2:                                                              \
      if ((i) < (bufsz)) (buf)[(i)] = (0x80 | (((ch) >> 6) & 0x3F));  (i)++; \
    last1:                                                              \
      if ((i) < (bufsz)) (buf)[(i)] = (0x80 | ((ch) & 0x3F));           \
      else if ((flags) & UTF_BUFSTOP) break;                            \
    }

/* ------------------------------------------------------------------------- */
#define DO_8BIT_NEWLINE(flags, s, se, buf, i, bufsz, e, stop, assign)   \
  if (*s == '\r' && (flags & (UTF_CRNL | UTF_LFNL)))                    \
    {                                                                   \
      e = s + 1;                                                        \
      if (e >= se)                      /* short src buffer */          \
        {                                                               \
          if (!(flags & UTF_FINAL)) stop;  /* need more data */         \
        }                                                               \
      else if (*e == '\n') e++;         /* skip LF of CRLF */           \
      goto donewline;                                                   \
    }                                                                   \
  else if (*s == '\n' && (flags & (UTF_CRNL | UTF_LFNL)))               \
    {                                                                   \
      e = s + 1;                                                        \
    donewline:                                                          \
      if (flags & UTF_CRNL)                                             \
        {                                                               \
          if ((i) < (bufsz)) (buf)[(i)] = '\015';                       \
          else if (flags & UTF_BUFSTOP) stop;                           \
          (i)++;                                                        \
        }                                                               \
      if (flags & UTF_LFNL)                                             \
        {                                                               \
          if ((i) < (bufsz)) (buf)[(i)] = '\012';                       \
          else if (flags & UTF_BUFSTOP) stop;                           \
          (i)++;                                                        \
        }                                                               \
      assign                                                            \
    }

/* ------------------------------------------------------------------------- */
static void
TXreportCannotConvert(TXPMBUF *pmbuf, const char *fn, const char *srcCharset,
                      const char *destCharset, const char *reason,
                      const char *buf, const char *bufEnd, const char *badLoc)
{
  size_t        lineNum, col, badLocOffset = (size_t)(badLoc - buf);
  const char    *s, *e, *eol, *s2, *bol;
  int           littleEndian = 0;
#define NBYTES  16
  char          badBuf[3*NBYTES + 2 + NBYTES + 1 + 2 + 32], *d, *badBufEnd;

  /* Hex dump of `badLoc' area: */
  s = badLoc - NBYTES/2;
  if (s < buf) s = buf;
  e = s + NBYTES;
  if (e > bufEnd) e = bufEnd;
  badBufEnd = badBuf + sizeof(badBuf);
  d = badBuf;
  d += htsnpf(d, badBufEnd - d, "%04wX:  ", (EPI_HUGEINT)(s - buf));
  for (s2 = s; d < badBufEnd && s2 < e; s2++)
    {
      /* Emphasize bad byte with underlines, not `->' arrow: latter
       * gets escaped in HTML mode:
       */
      d += htsnpf(d, badBufEnd - d, (s2 == badLoc ? "_%02X_ " : "%02X "),
                  (unsigned)(*(byte *)s2));
    }
  if (d < badBufEnd) d += htsnpf(d, badBufEnd - d, " ");
  for (s2 = s; d < badBufEnd && s2 < e; s2++)
    *(d++) = (*s2 >= ' ' && *s2 <= '~' ? *s2 : '.');
  if (d < badBufEnd) *d = '\0';
  else badBufEnd[-1] = '\0';

  /* Determine line/column of `badLoc' in `buf': */
  for (lineNum = 1, bol = buf; bol < badLoc; bol = eol, lineNum++)
    {
      for (eol = bol; eol < badLoc && *eol != '\r' && *eol != '\n'; eol++);
      if (eol >= badLoc) break;                 /* no newline: last line */
      s = eol;
      htskipeol((char **)&eol, (char *)badLoc);
      if (eol <= s) eol = TX_MAX(s + 1, badLoc); /* ensure `eol' advances */
    }
  /* `bol' now at start of line of `badLoc'; determine column: */
  if (strcmpi(srcCharset, Utf8) == 0)
    {
      for (s = bol, col = 1; s < badLoc; col++)
        {
          s2 = s;
          TXunicodeDecodeUtf8Char(&s, badLoc, 1);
          if (s <= s2) s = TX_MIN(s2 + 1, badLoc);  /* ensure `s' advances */
        }
    }
  else if (strcmpi(srcCharset, Utf16Le) == 0)
    {
      littleEndian = 1;
      goto utf16;
    }
  else if (strcmpi(srcCharset, Utf16Be) == 0 ||
           strcmpi(srcCharset, Utf16) == 0)     /* big-endian is default */
    {
      littleEndian = 0;
    utf16:
      /* wtf decode forwards like UTF-8 above?  just need forwards func: */
      for (s = badLoc, col = 1; s > bol; col++)
        {
          s2 = s;
          TXunicodeDecodeUtf16CharBackwards(&s, bol, littleEndian);
          if (s >= s2) s = TX_MAX(s2 - 1, bol); /* ensure `s' decrements */
        }
    }
  else                                          /* assume monobyte */
    col = (size_t)(badLoc - bol) + 1;

  txpmbuf_putmsg(pmbuf, MERR, fn, "Cannot completely convert charset %s to %s: %s at source byte offset %wu (line %wu character %wu): %s",
                 srcCharset, destCharset, reason, (EPI_HUGEINT)badLocOffset,
                 (EPI_HUGEINT)lineNum, (EPI_HUGEINT)col, badBuf);
#undef NBYTES
}

size_t
htiso88591_to_utf8(d, dlen, dtot, sp, slen, flags, state, width, htpfobj, pmbuf)
char            *d;     /* destination buffer */
size_t          dlen;   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot;  /* accumulating destination payload count */
CONST char      **sp;   /* *sp is source string */
size_t          slen;   /*    ""    ""   length (-1 == strlen(s)) */
UTF             flags;
UTF             *state;
int             width;  /* max chars per line (ignored) */
HTPFOBJ           *htpfobj; /* (in/out, opt.) (ignored) */
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
/* Translates one-byte-per-char ISO-8859-1 character buffer `*sp' to
 * multi-byte UTF-8 buffer `d', which should not be the same pointer
 * (UTF-8 is up to 2x longer).  Returns would-be length of `d'; if >
 * `dlen', not written past.  `d' is not nul-terminated, and may be
 * truncated mid-char if short unless UTF_BUFSTOP set.  Advances `*sp'
 * past decoded source.
 * UTF_ONECHAR        Only decode 1 character
 * UTF_ESCRANGE       Ignored (range increases)
 * UTF_ESCLOW         HTML-escape low (7-bit) chars < > " &
 * UTF_BUFSTOP        Stop decoding when out of `d' buffer space
 * UTF_FINAL          Must be set if this is the last of multiple buffers
 * UTF_BINARY         Ignored (quoted-printable)
 * UTF_BIGENDIAN      Ignored (UTF-16)
 * UTF_LITTLEENDIAN   Ignored (UTF-16)
 * UTF_SAVEBOM        Ignored (UTF-16)
 * UTF_START          Ignored (UTF-16)
 * UTF_CRNL           Map CR/LF/CRLF newlines to CR
 * UTF_LFNL           Map CR/LF/CRLF newlines to LF
 * UTF_DO8BIT         Ignore (ISO-8859-1 -> ISO-8859-1)
 * UTF_HTMLDECODE     HTML-unescape sequences and output as target char
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Output XML-safe chars only
 * UTF_BADENCASISO    Ignored
 * UTF_BADENCASISOERR Ignored
 * UTF_QENCONLY       Ignored
 * UTF_NOCHARTRANSLATE WTF need to implement in this function
 * >>>>> NOTE: See also htpf_engine() <<<<<
 */
{
  static CONST char     fn[] = "htiso88591_to_utf8";
  CONST byte            *s, *se, *e;
  char                  *esc;
  size_t                sz, i, si;
  int                   ch;
  char                  buf[20 + EPI_OS_INT_BITS/3 + 6];

  (void)width;
  (void)htpfobj;
  s = (CONST byte *)(*sp);
  if (slen == (size_t)(-1)) slen = strlen((char *)s);
  if (dlen == (size_t)(-1)) dlen = (d == (char *)s ? slen : strlen(d));
  i = 0;
  se = s + slen;
  if (flags & UTF_START) *state = (flags & ~UTF_BADCHARMSG);
  while (s < se)
    {
      e = s + 1;
      ch = *s;
      si = i;
      if (ch & 0x80)
        {
          /* U+0080 - U+00FF are XML-safe, so no UTF_XMLSAFE check needed */
          if (si < dlen) d[si] = ((*s & 0x40) ? 0xC3 : 0xC2);
          si++;
          if (si < dlen) d[si] = (0x80 | (*s & 0x3F));
          else if (flags & UTF_BUFSTOP) break;
          i = si;
        }
      else if (ch == '&' && (flags & UTF_HTMLDECODE))
        {
          si = i;
          while (e < se && *e!=';' && strchr(TXWhitespace,(char)(*e)) == CHARPN)
            e++;
          esc = htesc2html((CONST char *)s + 1, (CONST char *)e, 0, &sz, &ch,
                           buf, sizeof(buf));
          if (ch <= -1)                         /* bad escape: copy as-is */
            {
              e = s + 1;
              ch = *s;
              goto chkit;
            }
          if (e < se && *e == ';') e++;         /* skip over escape */
          if (!TX_IS_VALID_UNICODE_CODEPOINT(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Iso, Utf8,
                                  InvalidUnicode, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              ch = TX_INVALID_CHAR;
            }
          else if ((flags & UTF_XMLSAFE) &&
                   !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Iso, Utf8,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              ch = TX_INVALID_CHAR;
            }
          UTF8_ENCODECHAR(d, si, dlen, ch, flags);
          i = si;
        }
      else DO_8BIT_NEWLINE(flags, s, se, d, si, dlen,e,break,i=si;goto cont1;)
      else
        {
          ch = *s;
        chkit:
          if ((flags & UTF_XMLSAFE) && !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Iso, Utf8,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              ch = TX_INVALID_CHAR;
              goto asIs;
            }
          if (!HTNOESC(ch) && (flags & UTF_ESCLOW))
            {
              si = i;
              for (esc = html2esc((unsigned)(ch), buf, sizeof(buf), pmbuf);
                   *esc != '\0';
                   esc++, si++)
                if (si < dlen) d[si] = *esc;
              if (si > dlen && (flags & UTF_BUFSTOP)) break;
              i = si - 1;
            }
          else
            {
            asIs:
              if (i < dlen) d[i] = ch;
              else if (flags & UTF_BUFSTOP) break;
            }
        }
      i++;
    cont1:
      s = e;
      if (flags & UTF_ONECHAR) break;
    }
  *sp = (CONST char *)s;
  *dtot += i;
  return(i);
}

size_t
htusascii_to_utf8(d, dlen, dtot, sp, slen, flags, state, width, htpfobj, pmbuf)
char            *d;     /* destination buffer */
size_t          dlen;   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot;  /* accumulating destination payload count */
CONST char      **sp;   /* *sp is source string */
size_t          slen;   /*    ""    ""   length (-1 == strlen(s)) */
UTF             flags;
UTF             *state;
int             width;  /* max chars per line (ignored) */
HTPFOBJ           *htpfobj; /* (in/out, opt.) (ignored) */
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
/* Translates one-byte-per-char US-ASCII character buffer `*sp' to
 * multi-byte UTF-8 buffer `d', which should not be the same pointer
 * (UTF-8 is up to 2x longer).  Returns would-be length of `d'; if >
 * `dlen', not written past.  `d' is not nul-terminated, and may be
 * truncated mid-char if short unless UTF_BUFSTOP set.  Advances `*sp'
 * past decoded source.
 * UTF_ONECHAR        Only decode 1 character
 * UTF_ESCRANGE       Ignored (range increases)
 * UTF_ESCLOW         HTML-escape low (7-bit) chars < > " &
 * UTF_BUFSTOP        Stop decoding when out of `d' buffer space
 * UTF_FINAL          Must be set if this is the last of multiple buffers
 * UTF_BINARY         Ignored (quoted-printable)
 * UTF_BIGENDIAN      Ignored (UTF-16)
 * UTF_LITTLEENDIAN   Ignored (UTF-16)
 * UTF_SAVEBOM        Ignored (UTF-16)
 * UTF_START          Ignored (UTF-16)
 * UTF_CRNL           Map CR/LF/CRLF newlines to CR
 * UTF_LFNL           Map CR/LF/CRLF newlines to LF
 * UTF_DO8BIT         Ignore (ISO-8859-1 -> ISO-8859-1)
 * UTF_HTMLDECODE     HTML-unescape sequences and output as target char
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Output XML-safe chars only
 * UTF_BADENCASISO    Ignored
 * UTF_BADENCASISOERR Ignored
 * UTF_QENCONLY       Ignored
 * UTF_NOCHARTRANSLATE WTF need to implement in this function
 * >>>>> NOTE: See also htpf_engine() <<<<<
 */
{
  static CONST char     fn[] = "htusascii_to_utf8";
  CONST byte            *s, *se, *e;
  char                  *esc;
  size_t                sz, i, si;
  int                   ch;
  char                  buf[20 + EPI_OS_INT_BITS/3 + 6];

  (void)width;
  (void)htpfobj;
  s = (CONST byte *)(*sp);
  if (slen == (size_t)(-1)) slen = strlen((char *)s);
  if (dlen == (size_t)(-1)) dlen = (d == (char *)s ? slen : strlen(d));
  i = 0;
  se = s + slen;
  if (flags & UTF_START) *state = (flags & ~UTF_BADCHARMSG);
  while (s < se)
    {
      e = s + 1;
      ch = *s;
      si = i;
      if (ch & 0x80)
        {
          /* Hi-bit chars are technically illegal in US-ASCII;
           * yap about it but assume they meant ISO-8859-1
           * since that's probable and still safe output in UTF-8:
           * U+0080 - U+00FF are XML-safe, so no UTF_XMLSAFE check needed:
           */
          if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
            TXreportCannotConvert(pmbuf, fn, UsAscii, Utf8,
                                  RangeChar, *sp, (char *)se, (char *)s);
          *state |= UTF_BADCHARMSG;             /* flag for caller and us */
          if (si < dlen) d[si] = ((*s & 0x40) ? 0xC3 : 0xC2);
          si++;
          if (si < dlen) d[si] = (0x80 | (*s & 0x3F));
          else if (flags & UTF_BUFSTOP) break;
          i = si;
        }
      else if (ch == '&' && (flags & UTF_HTMLDECODE))
        {
          si = i;
          while (e < se && *e!=';' && strchr(TXWhitespace,(char)(*e)) == CHARPN)
            e++;
          esc = htesc2html((CONST char *)s + 1, (CONST char *)e, 0, &sz, &ch,
                           buf, sizeof(buf));
          if (ch <= -1)                         /* bad escape: copy as-is */
            {
              e = s + 1;
              ch = *s;
              goto chkit;
            }
          if (e < se && *e == ';') e++;         /* skip over escape */
          if (!TX_IS_VALID_UNICODE_CODEPOINT(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, UsAscii, Utf8,
                                  InvalidUnicode, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              ch = TX_INVALID_CHAR;
            }
          else if ((flags & UTF_XMLSAFE) &&
                   !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
             {
               if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                 TXreportCannotConvert(pmbuf, fn, UsAscii, Utf8,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
               *state |= UTF_BADCHARMSG;         /* flag for caller and us */
               ch = TX_INVALID_CHAR;
             }
          UTF8_ENCODECHAR(d, si, dlen, ch, flags);
          i = si;
        }
      else DO_8BIT_NEWLINE(flags, s, se, d, si, dlen,e,break,i=si;goto cont1;)
      else
        {
          ch = *s;
        chkit:
          if ((flags & UTF_XMLSAFE) && !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, UsAscii, Utf8,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              ch = TX_INVALID_CHAR;
              goto asIs;
            }
          if (!HTNOESC(ch) && (flags & UTF_ESCLOW))
            {
              si = i;
              for (esc = html2esc((unsigned)(ch), buf, sizeof(buf), pmbuf);
                   *esc != '\0';
                   esc++, si++)
                if (si < dlen) d[si] = *esc;
              if (si > dlen && (flags & UTF_BUFSTOP)) break;
              i = si - 1;
            }
          else
            {
            asIs:
              if (i < dlen) d[i] = ch;
              else if (flags & UTF_BUFSTOP) break;
            }
        }
      i++;
    cont1:
      s = e;
      if (flags & UTF_ONECHAR) break;
    }
  *sp = (CONST char *)s;
  *dtot += i;
  return(i);
}

size_t
htutf8_to_iso88591(d, dlen, dtot, sp, slen, flags, state, width, htpfobj, pmbuf)
char            *d;     /* destination buffer */
size_t          dlen;   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot;  /* accumulating destination payload count */
CONST char      **sp;   /* *sp is source string */
size_t          slen;   /*    ""    ""   length (-1 == strlen(s)) */
UTF             flags;
UTF             *state;
int             width;  /* max chars per line (ignored) */
HTPFOBJ           *htpfobj; /* (in/out, opt.) (ignored) */
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
/* Translates multibyte UTF-8 character buffer `*sp' to
 * one-byte-per-char ISO-8859 buffer `d', which may be the same
 * pointer unless UTF_ESCRANGE specified.  (ISO-8859 is 1x to 0.5x the
 * length).  Returns would-be length of `d'; if > `dlen', not written
 * past.  `d' is not nul-terminated, and may be truncated mid-char if
 * short unless UTF_BUFSTOP set.  Advances `*sp' past decoded source.
 * Illegal, short-buf, out-of-range chars are transposed as TX_INVALID_CHAR
 * unless otherwise noted:
 * UTF_ONECHAR        Only decode 1 character
 * UTF_ESCRANGE       HTML-escape out-of-range (>256) characters
 * UTF_ESCLOW         HTML-escape low (7-bit) chars < > " &
 * UTF_BUFSTOP        Stop decoding when out of `d' buffer space
 * UTF_FINAL          Must be set if this is the last of multiple buffers
 * UTF_BINARY         Ignored (quoted-printable)
 * UTF_BIGENDIAN      Ignored (UTF-16)
 * UTF_LITTLEENDIAN   Ignored (UTF-16)
 * UTF_SAVEBOM        Ignored (UTF-16)
 * UTF_START          Ignored (UTF-16)
 * UTF_CRNL           Map CR/LF/CRLF newlines to CR
 * UTF_LFNL           Map CR/LF/CRLF newlines to LF
 * UTF_DO8BIT         Use 8-bit 1-char esc replacements, not 7-bit multi-char
 * UTF_HTMLDECODE     HTML-unescape sequences and output as target char
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Output XML-safe chars only
 * UTF_BADENCASISO    Interpret bad UTF-8 sequence as ISO-8859-1
 * UTF_BADENCASISOERR Issue msg if "" and UTF_BADCHARMSG
 * UTF_QENCONLY       Ignored
 * UTF_NOCHARTRANSLATE WTF need to implement in this function
 * >>>>> NOTE: See also htpf_engine() <<<<<
 */
{
  static CONST char     fn[] = "htutf8_to_iso88591";
  CONST byte            *se, *s, *e;
  char                  *esc;
  CONST char            *ss;
  size_t                sz, i, si;
  int                   ch;
  char                  buf[20 + EPI_OS_INT_BITS/3 + 6];

  (void)width;
  (void)htpfobj;
  s = (CONST byte *)(*sp);
  if (slen == (size_t)(-1)) slen = strlen((char *)s);
  if (dlen == (size_t)(-1)) dlen = (d == (char *)s ? slen : strlen(d));
  i = 0;
  se = s + slen;
  if (flags & UTF_START) *state = (flags & ~UTF_BADCHARMSG);
  while (s < se)
    {
      si = i;
      if (*s & 0x80)                                    /* UTF esc seq. */
        {
          ss = (CONST char *)s;
          ch = TXunicodeDecodeUtf8Char(&ss, (CONST char *)se, 0);
          if (ch == TXUNICHAR_SHORT_BUFFER)
            {
              if ((flags & UTF_FINAL) && i < dlen)      /* really truncated */
                {
                  ss = (CONST char *)s + 1;             /* skip 1 byte */
                  /* KNG 20090611 if UTF_BADENCASISO set, ...ERR
                   * must be too for this to be an error:
                   */
                  if ((flags & (UTF_BADENCASISO | UTF_BADENCASISOERR)) !=
                      UTF_BADENCASISO)
                    {
                      if ((flags & UTF_BADCHARMSG) &&
                          !(*state & UTF_BADCHARMSG))
                        TXreportCannotConvert(pmbuf, fn, Utf8, Iso,
                                       TruncChar, *sp, (char *)se, (char *)s);
                      *state |= UTF_BADCHARMSG; /* flag for caller and us */
                    }
                  if (flags & UTF_BADENCASISO)
                    {
                      ch = *s;                  /* interpret as ISO-8859-1 */
                      goto onechar;
                    }
                  goto badchar;
                }
              break;
            }
          if (ch <= TXUNICHAR_INVALID_SEQUENCE)
            {
              /* KNG 20090611 if UTF_BADENCASISO set, ...ERR
               * must be too for this to be an error:
               */
              if ((flags & (UTF_BADENCASISO | UTF_BADENCASISOERR)) !=
                  UTF_BADENCASISO)
                {
                  if ((flags & UTF_BADCHARMSG) &&
                      !(*state & UTF_BADCHARMSG))
                    TXreportCannotConvert(pmbuf, fn, Utf8, Iso,
                                     InvalidChar, *sp, (char *)se, (char *)s);
                  *state |= UTF_BADCHARMSG;     /* flag for caller and us */
                }
              if (flags & UTF_BADENCASISO)
                {
                  ss = (CONST char *)s + 1;     /* skip 1 byte */
                  ch = *s;                      /* interpret as ISO-8859-1 */
                }
              else
                goto badchar;
            }
          if ((flags & UTF_XMLSAFE) && !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, Iso,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              goto badchar;
            }
          if (ch < 256) goto onechar;                   /* ok as ISO-8859-1 */
          if (flags & UTF_ESCRANGE)                     /* HTML-escape it */
            {
            doesc:
              esc = html2esc((unsigned)ch, buf, sizeof(buf), pmbuf);
              sz = strlen(esc);
            doesccp:
              si = i;
              for ( ; sz > 0; esc++, sz--, si++)
                if (si < dlen) d[si] = *esc;
              if (si > dlen && (flags & UTF_BUFSTOP)) break;
              i = si;
            }
          else
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, Iso,
                                      RangeChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
            badchar:
              ch = TX_INVALID_CHAR;
            onechar:
              if (i < dlen) d[i] = ch;
              else if (flags & UTF_BUFSTOP) break;
              i++;
            }
          s = (CONST byte *)ss;
        }
      else if (*s == '&' && (flags & UTF_HTMLDECODE))
        {
          for (e = s + 1;
               e < se && *e != ';' && strchr(TXWhitespace, (char)(*e)) ==CHARPN;
               e++);
          esc = htesc2html((CONST char *)s + 1, (CONST char *)e,
                           (flags & UTF_DO8BIT), &sz, &ch, buf, sizeof(buf));
          if (ch <= -1) goto chkit;                     /* bad escape */
          if (e < se && *e == ';') e++;
          ss = (CONST char *)e;
          if (esc == CHARPN)                            /* out of range */
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, Iso,
                                      RangeEsc, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              goto badchar;
            }
          if (!TX_IS_VALID_UNICODE_CODEPOINT(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, Iso,
                                  InvalidUnicode, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              goto badchar;
            }
          else if ((flags & UTF_XMLSAFE) &&
                   !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, Iso,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              goto badchar;
            }
          goto doesccp;
        }
      else DO_8BIT_NEWLINE(flags, s, se, d, si, dlen, e, break, i = si; s=e;)
      else
        {
        chkit:
          ch = *s;
          if ((flags & UTF_XMLSAFE) && !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, Iso,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              ch = TX_INVALID_CHAR;
              goto asIs;
            }
          if (!HTNOESC(ch) && (flags & UTF_ESCLOW))
            {
              ss = (CONST char *)s + 1;
              goto doesc;
            }
          else
            {
            asIs:
              if (i < dlen) d[i] = ch;
              else if (flags & UTF_BUFSTOP) break;
              i++;
              s++;
            }
        }
      if (flags & UTF_ONECHAR) break;
    }
  *sp = (CONST char *)s;
  *dtot += i;
  return(i);
}

size_t
htutf8_to_usascii(d, dlen, dtot, sp, slen, flags, state, width, htpfobj, pmbuf)
char            *d;     /* destination buffer */
size_t          dlen;   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot;  /* accumulating destination payload count */
CONST char      **sp;   /* *sp is source string */
size_t          slen;   /*    ""    ""   length (-1 == strlen(s)) */
UTF             flags;
UTF             *state;
int             width;  /* max chars per line (ignored) */
HTPFOBJ           *htpfobj; /* (in/out, opt.) (ignored) */
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
/* Translates multibyte UTF-8 character buffer `*sp' to
 * one-byte-per-char US-ASCII buffer `d', which may be the same
 * pointer unless UTF_ESCRANGE specified.  (US-ASCII is 1x to 0.5x the
 * length).  Returns would-be length of `d'; if > `dlen', not written
 * past.  `d' is not nul-terminated, and may be truncated mid-char if
 * short unless UTF_BUFSTOP set.  Advances `*sp' past decoded source.
 * Illegal, short-buf, out-of-range chars are transposed as TX_INVALID_CHAR
 * unless otherwise noted:
 * UTF_ONECHAR        Only decode 1 character
 * UTF_ESCRANGE       HTML-escape out-of-range (>256) characters
 * UTF_ESCLOW         HTML-escape low (7-bit) chars < > " &
 * UTF_BUFSTOP        Stop decoding when out of `d' buffer space
 * UTF_FINAL          Must be set if this is the last of multiple buffers
 * UTF_BINARY         Ignored (quoted-printable)
 * UTF_BIGENDIAN      Ignored (UTF-16)
 * UTF_LITTLEENDIAN   Ignored (UTF-16)
 * UTF_SAVEBOM        Ignored (UTF-16)
 * UTF_START          Ignored (UTF-16)
 * UTF_CRNL           Map CR/LF/CRLF newlines to CR
 * UTF_LFNL           Map CR/LF/CRLF newlines to LF
 * UTF_DO8BIT         Use 8-bit 1-char esc replacements, not 7-bit multi-char
 * UTF_HTMLDECODE     HTML-unescape sequences and output as target char
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Output XML-safe chars only
 * UTF_BADENCASISO    Interpret bad UTF-8 sequence as ISO-8859-1
 * UTF_BADENCASISOERR Issue msg if "" and UTF_BADCHARMSG
 * UTF_QENCONLY       Ignored
 * UTF_NOCHARTRANSLATE WTF need to implement in this function
 * >>>>> NOTE: See also htpf_engine() <<<<<
 */
{
  static CONST char     fn[] = "htutf8_to_usascii";
  CONST byte            *se, *s, *e;
  char                  *esc;
  CONST char            *ss;
  size_t                sz, i, si;
  int                   ch;
  char                  buf[20 + EPI_OS_INT_BITS/3 + 6];

  (void)width;
  (void)htpfobj;
  s = (CONST byte *)(*sp);
  if (slen == (size_t)(-1)) slen = strlen((char *)s);
  if (dlen == (size_t)(-1)) dlen = (d == (char *)s ? slen : strlen(d));
  i = 0;
  se = s + slen;
  if (flags & UTF_START) *state = (flags & ~UTF_BADCHARMSG);
  while (s < se)
    {
      si = i;
      if (*s & 0x80)                                    /* UTF sequence */
        {
          ss = (CONST char *)s;
          ch = TXunicodeDecodeUtf8Char(&ss, (CONST char *)se, 0);
          if (ch == TXUNICHAR_SHORT_BUFFER)
            {
              if ((flags & UTF_FINAL) && i < dlen)      /* really truncated */
                {
                  ss = (CONST char *)s + 1;             /* skip 1 byte */
                  /* KNG 20090611 if UTF_BADENCASISO set, ...ERR
                   * must be too for this to be an error:
                   */
                  if ((flags & (UTF_BADENCASISO | UTF_BADENCASISOERR)) !=
                      UTF_BADENCASISO)
                    {
                      if ((flags & UTF_BADCHARMSG) &&
                          !(*state & UTF_BADCHARMSG))
                        TXreportCannotConvert(pmbuf, fn, Utf8, UsAscii,
                                       TruncChar, *sp, (char *)se, (char *)s);
                      *state |= UTF_BADCHARMSG; /* flag for caller and us */
                    }
                  if (flags & UTF_BADENCASISO)
                    {
                      ch = *s;                  /* interpret as ISO-8859-1 */
                      goto onechar;
                    }
                  goto badchar;
                }
              break;
            }
          if (ch <= TXUNICHAR_INVALID_SEQUENCE)
            {
              /* KNG 20090611 if UTF_BADENCASISO set, ...ERR
               * must be too for this to be an error:
               */
              if ((flags & (UTF_BADENCASISO | UTF_BADENCASISOERR)) !=
                  UTF_BADENCASISO)
                {
                  if ((flags & UTF_BADCHARMSG) &&
                      !(*state & UTF_BADCHARMSG))
                    TXreportCannotConvert(pmbuf, fn, Utf8, UsAscii,
                                     InvalidChar, *sp, (char *)se, (char *)s);
                  *state |= UTF_BADCHARMSG;     /* flag for caller and us */
                }
              if (flags & UTF_BADENCASISO)
                {
                  ss = (CONST char *)s + 1;     /* skip 1 byte */
                  ch = *s;                      /* interpret as ISO-8859-1 */
                }
              else
                goto badchar;
            }
          if ((flags & UTF_XMLSAFE) && !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, UsAscii,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              goto badchar;
            }
          if (ch < 128) goto onechar;                   /* ok as US-ASCII */
          if (flags & UTF_ESCRANGE)                     /* HTML-escape it */
            {
            doesc:
              esc = html2esc((unsigned)ch, buf, sizeof(buf), pmbuf);
              sz = strlen(esc);
            doesccp:
              si = i;
              for ( ; sz > 0; esc++, sz--, si++)
                if (si < dlen) d[si] = *esc;
              if (si > dlen && (flags & UTF_BUFSTOP)) break;
              i = si;
            }
          else
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, UsAscii,
                                      RangeChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
            badchar:
              ch = TX_INVALID_CHAR;
            onechar:
              if (i < dlen) d[i] = ch;
              else if (flags & UTF_BUFSTOP) break;
              i++;
            }
          s = (CONST byte *)ss;
        }
      else if (*s == '&' && (flags & UTF_HTMLDECODE))
        {
          for (e = s + 1;
               e < se && *e != ';' && strchr(TXWhitespace, (char)(*e)) ==CHARPN;
               e++);
          esc = htesc2html((CONST char *)s + 1, (CONST char *)e,
                           (flags & UTF_DO8BIT), &sz, &ch, buf, sizeof(buf));
          if (ch <= -1) goto chkit;                     /* bad escape */
          if (e < se && *e == ';') e++;
          ss = (CONST char *)e;
          if (esc == CHARPN || ch >= 128)               /* out of range */
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, UsAscii,
                                      RangeEsc, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              goto badchar;
            }
          if (!TX_IS_VALID_UNICODE_CODEPOINT(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, UsAscii,
                                  InvalidUnicode, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              goto badchar;
            }
          else if ((flags & UTF_XMLSAFE) &&
                   !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, UsAscii,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              goto badchar;
            }
          goto doesccp;
        }
      else DO_8BIT_NEWLINE(flags, s, se, d, si, dlen, e, break, i = si; s=e;)
      else
        {
        chkit:
          ch = *s;
          if ((flags & UTF_XMLSAFE) && !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, UsAscii,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              ch = TX_INVALID_CHAR;
              goto asIs;
            }
          if (!HTNOESC(ch) && (flags & UTF_ESCLOW))
            {
              ss = (CONST char *)s + 1;
              goto doesc;
            }
          else
            {
            asIs:
              if (i < dlen) d[i] = ch;
              else if (flags & UTF_BUFSTOP) break;
              i++;
              s++;
            }
        }
      if (flags & UTF_ONECHAR) break;
    }
  *sp = (CONST char *)s;
  *dtot += i;
  return(i);
}


/* ------------------------------------------------------------------------- */

size_t
htutf8_to_utf8(
char            *d,     /* destination buffer */
size_t          dlen,   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot,  /* accumulating destination payload count */
CONST char      **sp,   /* *sp is source string */
size_t          slen,   /*    ""    ""   length (-1 == strlen(s)) */
UTF             flags,
UTF             *state, /* (in/out) cross-call state; uses UTF_BADCHARMSG */
int             width,  /* max chars per line (ignored) */
HTPFOBJ         *htpfobj, /* (in/out, opt.) (ignored) */
TXPMBUF         *pmbuf    /* (out) (optional) putmsg buffer */
)
/* Decodes multi-byte UTF-8 character buffer `*sp' and re-encodes to `d'.
 * Returns would-be length of `d'; if > `dlen', not written past.
 * `d' is not nul-terminated, and may be truncated mid-char if short
 * unless UTF_BUFSTOP set.  Advances `*sp' past decoded source.
 * UTF_ONECHAR        Only decode 1 character
 * UTF_ESCRANGE       Ignored (src/dest same range)
 * UTF_ESCLOW         HTML-escape low (7-bit) chars < > " &
 * UTF_BUFSTOP        Stop decoding when out of `d' buffer space
 * UTF_FINAL          Must be set if this is the last of multiple buffers
 * UTF_BINARY         Ignored (quoted-printable)
 * UTF_BIGENDIAN      Ignored (UTF-16)
 * UTF_LITTLEENDIAN   Ignored (UTF-16)
 * UTF_SAVEBOM        Ignored (UTF-16)
 * UTF_START          Ignored (UTF-16)
 * UTF_CRNL           Map CR/LF/CRLF newlines to CR
 * UTF_LFNL           Map CR/LF/CRLF newlines to LF
 * UTF_DO8BIT         Ignored (ISO-8859-1 -> ISO-8859-1)
 * UTF_HTMLDECODE     HTML-unescape sequences and output as target char
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Output XML-safe chars only
 * UTF_BADENCASISO    Interpret bad UTF-8 sequence as ISO-8859-1
 * UTF_BADENCASISOERR Issue msg if "" and UTF_BADCHARMSG
 * UTF_QENCONLY       Ignored
 * UTF_NOCHARTRANSLATE  Do not do charset (UTF-8 to UTF-8) translation
 * >>>>> NOTE: See also htpf_engine() <<<<<
 */
{
  static CONST char     fn[] = "htutf8_to_utf8";
  CONST byte            *s, *se, *e;
  char                  *esc;
  CONST char            *ss;
  size_t                sz, i, si;
  int                   ch;
  char                  buf[20 + EPI_OS_INT_BITS/3 + 6];

  (void)width;
  (void)htpfobj;
  s = (CONST byte *)(*sp);
  if (slen == (size_t)(-1)) slen = strlen((char *)s);
  if (dlen == (size_t)(-1)) dlen = (d == (char *)s ? slen : strlen(d));
  i = 0;
  se = s + slen;

  /* NOTE: if `*state' uses more than UTF_BADCHARMSG here, see
   * TXencodedWordToUtf8() and make sure latter's `*state' flags do
   * not conflict.  Conversely, do not clear any `*state' flags here
   * that we do not use, to avoid stomping TXencodeWordToUtf8() usage:
   */
  if (flags & UTF_START) *state &= ~UTF_BADCHARMSG;

  while (s < se)
    {
      e = s + 1;
      ch = *s;
      si = i;
      if ((ch & 0x80) && !(flags & UTF_NOCHARTRANSLATE))
        {
          ss = (CONST char *)s;
          ch = TXunicodeDecodeUtf8Char(&ss, (CONST char *)se, 0);
          if (ch == TXUNICHAR_SHORT_BUFFER)
            {
              if ((flags & UTF_FINAL) && i < dlen)      /* really truncated */
                {
                  ss = (CONST char *)s + 1;             /* skip 1 byte */
                  /* KNG 20090611 if UTF_BADENCASISO set, ...ERR
                   * must be too for this to be an error:
                   */
                  if ((flags & (UTF_BADENCASISO | UTF_BADENCASISOERR)) !=
                      UTF_BADENCASISO)
                    {
                      if ((flags & UTF_BADCHARMSG) &&
                          !(*state & UTF_BADCHARMSG))
                        TXreportCannotConvert(pmbuf, fn, Utf8, Utf8,
                                       TruncChar, *sp, (char *)se, (char *)s);
                      *state |= UTF_BADCHARMSG; /* flag for caller and us */
                    }
                  if (flags & UTF_BADENCASISO)
                    {
                      ch = *s;                  /* interpret as ISO-8859-1 */
                      e = (CONST byte *)ss;
                      goto encCh;
                    }
                  ch = TX_INVALID_CHAR;
                  e = (CONST byte *)ss;
                  goto encCh;
                }
              break;
            }
          if (ch <= TXUNICHAR_INVALID_SEQUENCE)
            {
              /* KNG 20090611 if UTF_BADENCASISO set, ...ERR
               * must be too for this to be an error:
               */
              if ((flags & (UTF_BADENCASISO | UTF_BADENCASISOERR)) !=
                  UTF_BADENCASISO)
                {
                  if ((flags & UTF_BADCHARMSG) &&
                      !(*state & UTF_BADCHARMSG))
                    TXreportCannotConvert(pmbuf, fn, Utf8, Utf8,
                                     InvalidChar, *sp, (char *)se, (char *)s);
                  *state |= UTF_BADCHARMSG;     /* flag for caller and us */
                }
              if (flags & UTF_BADENCASISO)
                {
                  ss = (CONST char *)s + 1;     /* skip 1 byte */
                  ch = *s;                      /* interpret as ISO-8859-1 */
                }
              else
                {
                  ch = TX_INVALID_CHAR;
                  goto skipXml;                 /* skip XML-safe check */
                }
            }
          if ((flags & UTF_XMLSAFE) && !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, Utf8,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              ch = TX_INVALID_CHAR;
            }
        skipXml:
          e = (CONST byte *)ss;
          goto encCh;
        }
      else if (ch == '&' && (flags & UTF_HTMLDECODE))
        {
          si = i;
          while (e < se && *e!=';' && strchr(TXWhitespace,(char)(*e)) == CHARPN)
            e++;
          esc = htesc2html((CONST char *)s + 1, (CONST char *)e, 0, &sz, &ch,
                           buf, sizeof(buf));
          if (ch <= -1)                         /* bad escape: copy as-is */
            {
              e = s + 1;
              ch = *s;
              goto chkit;
            }
          if (e < se && *e == ';') e++;         /* skip over escape */
          if (!TX_IS_VALID_UNICODE_CODEPOINT(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, Utf8,
                                  InvalidUnicode, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              ch = TX_INVALID_CHAR;
            }
          else if ((flags & UTF_XMLSAFE) &&
                   !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, Utf8,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              ch = TX_INVALID_CHAR;
            }
        encCh:
          UTF8_ENCODECHAR(d, si, dlen, ch, flags);
          i = si;
        }
      else DO_8BIT_NEWLINE(flags, s, se, d, si, dlen,e,break,i=si;goto cont1;)
      else
        {
          ch = *s;
        chkit:
          if ((flags & UTF_XMLSAFE) && !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, Utf8,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              ch = TX_INVALID_CHAR;
              goto asIs;
            }
          if (!HTNOESC(ch) && (flags & UTF_ESCLOW))
            {
              si = i;
              for (esc = html2esc((unsigned)(ch), buf, sizeof(buf), pmbuf);
                   *esc != '\0';
                   esc++, si++)
                if (si < dlen) d[si] = *esc;
              if (si > dlen && (flags & UTF_BUFSTOP)) break;
              i = si - 1;
            }
          else
            {
            asIs:
              if (i < dlen) d[i] = ch;
              else if (flags & UTF_BUFSTOP) break;
            }
        }
      i++;
    cont1:
      s = e;
      if (flags & UTF_ONECHAR) break;
    }
  *sp = (CONST char *)s;
  *dtot += i;
  return(i);
}
size_t
htiso88591_to_iso88591(d, dlen, dtot, sp, slen, flags, state, width, htpfobj,
                       pmbuf)
char            *d;     /* destination buffer */
size_t          dlen;   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot;  /* accumulating destination payload count */
CONST char      **sp;   /* *sp is source string */
size_t          slen;   /*    ""    ""   length (-1 == strlen(s)) */
UTF             flags;
UTF             *state;
int             width;  /* max chars per line (ignored) */
HTPFOBJ           *htpfobj; /* (in/out, opt.) (ignored) */
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
/* Copies single-byte ISO-8859-1 character buffer `*sp' to `d', which may be
 * the same pointer unless UTF_ESCLOW.  Returns would-be length of `d';
 * if > `dlen', not written past.  `d' is not nul-terminated, and may be
 * truncated mid-char if short unless UTF_BUFSTOP set.  Advances `*sp'
 * past decoded source.
 * UTF_ONECHAR        Only decode 1 character
 * UTF_ESCRANGE       Ignored (src/dest same range)
 * UTF_ESCLOW         HTML-escape low (7-bit) chars < > " &
 * UTF_BUFSTOP        Stop decoding when out of `d' buffer space
 * UTF_FINAL          Must be set if this is the last of multiple buffers
 * UTF_BINARY         Ignored (quoted-printable)
 * UTF_BIGENDIAN      Ignored (UTF-16)
 * UTF_LITTLEENDIAN   Ignored (UTF-16)
 * UTF_SAVEBOM        Ignored (UTF-16)
 * UTF_START          Ignored (UTF-16)
 * UTF_CRNL           Map CR/LF/CRLF newlines to CR
 * UTF_LFNL           Map CR/LF/CRLF newlines to LF
 * UTF_DO8BIT         Use 8-bit 1-char esc replacements, not 7-bit multi-char
 * UTF_HTMLDECODE     HTML-unescape sequences and output as target char
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Output XML-safe chars only
 * UTF_BADENCASISO    Ignored
 * UTF_BADENCASISOERR Ignored
 * UTF_QENCONLY       Ignored
 * UTF_NOCHARTRANSLATE  Ignored (always happens)
 * >>>>> NOTE: See also htpf_engine() <<<<<
 * WTF only tested for UTF_HTMLDECODE
 * WTF won't handle split esc sequences
 */
{
  static CONST char     fn[] = "htiso88591_to_iso88591";
  CONST byte            *s, *se, *e;
  CONST HTESC           *htesc;
  char                  *esc;
  size_t                sz, i, si;
  int                   ch, j;
  char                  buf[20 + EPI_OS_INT_BITS/3 + 6];
  char                  badCharBuf[2];

  (void)width;
  (void)htpfobj;
  s = (CONST byte *)(*sp);
  if (slen == (size_t)(-1)) slen = strlen((char *)s);
  if (dlen == (size_t)(-1)) dlen = (d == (char *)s ? slen : strlen(d));
  i = 0;
  se = s + slen;
  if (flags & UTF_START) *state = (flags & ~UTF_BADCHARMSG);
  while (s < se)
    {
      e = s + 1;
      ch = *s;
      si = i;
      if (ch == '&' && (flags & UTF_HTMLDECODE))
        {                               /* WTF what if seq is split/trunc? */
          while (e < se && *e!=';' && strchr(TXWhitespace,(char)(*e)) == CHARPN)
            e++;
          esc = htesc2html((CONST char *)s + 1, (CONST char *)e,
                           (flags & UTF_DO8BIT), &sz, &ch, buf, sizeof(buf));
          if (ch <= -1)                         /* bad escape */
            {
              e = s + 1;
              ch = *s;
              goto chkit;
            }
          if (e < se && *e == ';') e++;
#ifndef HT_UNICODE
          if (ch >= 256) {e = s+1; ch = *s; goto chkit; }
#endif /* !HT_UNICODE */
          if (esc == CHARPN)                    /* out-of-range escape */
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Iso, Iso,
                                      RangeEsc, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              badCharBuf[0] = TX_INVALID_CHAR;
              badCharBuf[1] = '\0';
              esc = badCharBuf;
              sz = 1;
            }
          else if (!TX_IS_VALID_UNICODE_CODEPOINT(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Iso, Iso,
                                  InvalidUnicode, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              badCharBuf[0] = TX_INVALID_CHAR;
              badCharBuf[1] = '\0';
              esc = badCharBuf;
              sz = 1;
            }
          else if ((flags & UTF_XMLSAFE) &&
                   !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Iso, Iso,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              badCharBuf[0] = TX_INVALID_CHAR;
              badCharBuf[1] = '\0';
              esc = badCharBuf;
              sz = 1;
            }
          goto cpescsz;
        }
      else DO_8BIT_NEWLINE(flags, s, se, d, si, dlen,e,break,i=si;goto cont1;)
      else if (flags & UTF_DO8BIT) goto chkit;
      else if ((unsigned)ch < (unsigned)256 &&
               (j = (int)Htescindex[(unsigned)ch]) >= 0)
        {
          htesc = Htesclist + j;
          esc = (char *)htesc->asciistr;
          sz = strlen(esc);
          goto cpescsz;
        }
      else if (HT_OKCTRL(ch)) goto chkit;
      else
        {
          ch = ' ';
        chkit:
          if ((flags & UTF_XMLSAFE) && !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Iso, Iso,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              ch = TX_INVALID_CHAR;
              goto asIs;
            }
          if (!HTNOESC(ch) && (flags & UTF_ESCLOW))
            {
              si = i;
              esc = html2esc((unsigned)(ch), buf, sizeof(buf), pmbuf);
              sz = strlen(esc);
            cpescsz:
              for ( ; sz > 0; sz--, esc++, si++) if (si < dlen) d[si] = *esc;
              if (si > dlen && (flags & UTF_BUFSTOP)) break;
              i = si - 1;
            }
          else
            {
            asIs:
              if (i < dlen) d[i] = ch;
              else if (flags & UTF_BUFSTOP) break;
            }
        }
      i++;
    cont1:
      s = e;
      if (flags & UTF_ONECHAR) break;
    }
  *sp = (CONST char *)s;
  *dtot += i;
  return(i);
}

/* ------------------------------------------------------------------------- */

size_t
htutf8_to_utf16(d, dlen, dtot, sp, slen, flags, state, width, htpfobj, pmbuf)
char            *d;     /* destination buffer */
size_t          dlen;   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot;  /* accumulating destination payload count */
CONST char      **sp;   /* *sp is source string */
size_t          slen;   /*    ""    ""   length (-1 == strlen(s)) */
UTF             flags;
UTF             *state;
int             width;  /* max chars per line (ignored) */
HTPFOBJ           *htpfobj; /* (in/out, opt.) (ignored) */
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
/* Translates multibyte UTF-8 character buffer `*sp' to multibyte
 * UTF-16 buffer `d'.  Returns would-be length of `d'; if > `dlen',
 * not written past.  `d' is not nul-terminated, and may be truncated
 * mid-char if short unless UTF_BUFSTOP set.  Advances `*sp' past
 * decoded source.  Illegal, short-buf, out-of-range chars are
 * transposed as TX_INVALID_CHAR unless otherwise noted.  `d' size should
 * be at least 12 bytes for forward progress (e.g. for `&quot;' 16-bit).
 * UTF_ONECHAR        Only decode 1 character
 * UTF_ESCRANGE       HTML-escape out-of-range (>=0x110000) characters
 * UTF_ESCLOW         HTML-escape low (7-bit) chars < > " &
 * UTF_BUFSTOP        Stop decoding when out of `d' buffer space
 * UTF_FINAL          Must be set if this is the last of multiple buffers
 * UTF_BINARY         Ignored (quoted-printable)
 * UTF_BIGENDIAN      Make dest big-endian (default)
 * UTF_LITTLEENDIAN   Make dest little-endian
 * UTF_SAVEBOM        Do *NOT* set leading BOM in output
 * UTF_START          This is the first buffer (required for 1st buffer)
 * UTF_CRNL           Map CR/LF/CRLF newlines to CR
 * UTF_LFNL           Map CR/LF/CRLF newlines to LF
 * UTF_DO8BIT         Ignored (ISO-8859-1 -> ISO-8859-1)
 * UTF_HTMLDECODE     HTML-unescape sequences and output as target char
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Output XML-safe chars only
 * UTF_BADENCASISO    Interpret bad UTF-8 sequence as ISO-8859-1
 * UTF_BADENCASISOERR Issue msg if "" and UTF_BADCHARMSG
 * UTF_QENCONLY       Ignored
 * UTF_NOCHARTRANSLATE WTF need to implement in this function
 * >>>>> NOTE: See also htpf_engine() <<<<<
 */
{
  static CONST char     fn[] = "htutf8_to_utf16";
  CONST byte            *se, *s, *e;
  char                  *esc;
  const char            *ss, *destCharset;
  int                   ch, val2;
  size_t                sz, i, si;
  char                  buf[20 + EPI_OS_INT_BITS/3 + 6];
  /* Outputs 16-bit value in either big- (default) or little-endian format: */
#define PUT16(d, v, flags)                      \
  if ((flags) & UTF_LITTLEENDIAN)               \
    {                                           \
      *(byte *)(d) = (byte)(v);                 \
      ((byte *)(d))[1] = (byte)((v) >> 8);      \
    }                                           \
  else                                          \
    {                                           \
      *(byte *)(d) = (byte)((v) >> 8);          \
      ((byte *)(d))[1] = (byte)(v);             \
    }

  (void)width;
  (void)htpfobj;

  destCharset = ((flags & UTF_LITTLEENDIAN) ? Utf16Le : Utf16Be);

  s = (CONST byte *)(*sp);
  if (slen == (size_t)(-1)) slen = strlen((char *)s);
  if (dlen == (size_t)(-1)) dlen = (d == (char *)s ? slen : strlen(d));
  i = 0;
  se = s + slen;
  if (flags & UTF_START) *state = (flags & ~UTF_BADCHARMSG);
  if ((flags & (UTF_START | UTF_SAVEBOM)) == UTF_START)
    {                                                   /* output BOM */
      if (i + 1 < dlen)                                 /* room in dest. */
        {
          PUT16(d + i, 0xfeff, flags);
        }
      else if (flags & UTF_BUFSTOP)
        goto stop;
      i += 2;
    }
  while (s < se)
    {
      if (*s & 0x80)                                    /* UTF esc seq. */
        {
          ss = (CONST char *)s;
          ch = TXunicodeDecodeUtf8Char(&ss, (CONST char *)se, 0);
          if (ch == TXUNICHAR_SHORT_BUFFER)
            {
              if ((flags & UTF_FINAL) && i + 1 < dlen)  /* really truncated */
                {
                  ss = (CONST char *)s + 1;             /* skip 1 byte */
                  /* KNG 20090611 if UTF_BADENCASISO set, ...ERR
                   * must be too for this to be an error:
                   */
                  if ((flags & (UTF_BADENCASISO | UTF_BADENCASISOERR)) !=
                      UTF_BADENCASISO)
                    {
                      if ((flags & UTF_BADCHARMSG) &&
                          !(*state & UTF_BADCHARMSG))
                        TXreportCannotConvert(pmbuf, fn, Utf8, destCharset,
                                       TruncChar, *sp, (char *)se, (char *)s);
                      *state |= UTF_BADCHARMSG; /* flag for caller and us */
                    }
                  if (flags & UTF_BADENCASISO)
                    {
                      ch = *s;                  /* interpret as ISO-8859-1 */
                      goto one16bit;
                    }
                  goto badchar;
                }
              break;
            }
          if (ch <= TXUNICHAR_INVALID_SEQUENCE)
            {
              /* KNG 20090611 if UTF_BADENCASISO set, ...ERR
               * must be too for this to be an error:
               */
              if ((flags & (UTF_BADENCASISO | UTF_BADENCASISOERR)) !=
                  UTF_BADENCASISO)
                {
                  if ((flags & UTF_BADCHARMSG) &&
                      !(*state & UTF_BADCHARMSG))
                    TXreportCannotConvert(pmbuf, fn, Utf8, destCharset,
                                     InvalidChar, *sp, (char *)se, (char *)s);
                  *state |= UTF_BADCHARMSG;     /* flag for caller and us */
                }
              if (flags & UTF_BADENCASISO)
                {
                  ss = (CONST char *)s + 1;     /* skip 1 byte */
                  ch = *s;                      /* interpret as ISO-8859-1 */
                }
              else
                goto badchar;
            }
          if ((flags & UTF_XMLSAFE) && !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, destCharset,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              goto badchar;
            }
        doch:
          if (ch < 0x10000) goto one16bit;
          if (ch < 0x110000)                            /* two 16-bit seq. */
            {
              ch -= 0x10000;
              val2 = (TX_UTF16_LO_SURROGATE_BEGIN | (ch & 0x3FF));
              ch = (TX_UTF16_HI_SURROGATE_BEGIN | (ch >> 10));
              if (i + 1 < dlen)                         /* room for 16 bits */
                {
                  PUT16(d + i, ch, flags);
                }
              i += 2;
              if (i + 1 < dlen)                         /* room for next 16 */
                {
                  PUT16(d + i, val2, flags);
                }
              else if (flags & UTF_BUFSTOP) break;
              i += 2;
            }
          else if (flags & UTF_ESCRANGE)                /* range; HTML esc. */
            goto doesc;
          else                                          /* out of range */
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, destCharset,
                                      RangeChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
            badchar:
              ch = TX_INVALID_CHAR;
              goto one16bit;
            }
        }
      else if (*s == '&' && (flags & UTF_HTMLDECODE))
        {
          for (e = s + 1;
               e < se && *e != ';' && strchr(TXWhitespace, (char)(*e)) ==CHARPN;
               e++);
          esc = htesc2html((CONST char *)s + 1, (CONST char *)e, 0, &sz, &ch,
                           buf, sizeof(buf));
          if (ch <= -1) goto chkit;                     /* bad escape */
          if (e < se && *e == ';') e++;
          if (!TX_IS_VALID_UNICODE_CODEPOINT(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, destCharset,
                                  InvalidUnicode, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              goto badchar;
            }
          else if ((flags & UTF_XMLSAFE) &&
                   !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, destCharset,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              goto badchar;
            }
          ss = (CONST char *)e;
          goto doch;
        }
      else if (*s == '\r' && (flags & (UTF_CRNL | UTF_LFNL)))
        {
          ss = (CONST char *)s + 1;
          if ((CONST byte *)ss >= se)                   /* short src buffer */
            {
              if (!(flags & UTF_FINAL)) break;          /* need more data */
            }
          else if (*ss == '\n') ss++;                   /* skip LF of CRLF */
          goto donewline;
        }
      else if (*s == '\n' && (flags & (UTF_CRNL | UTF_LFNL)))
        {
          ss = (CONST char *)s + 1;
        donewline:
          si = i;
          if (flags & UTF_CRNL)
            {
              if (si + 1 < dlen)                        /* room for 16 bits */
                {
                  PUT16(d + si, '\015', flags);
                }
              else if (flags & UTF_BUFSTOP) break;
              si += 2;
            }
          if (flags & UTF_LFNL)
            {
              if (si + 1 < dlen)                        /* room for next 16 */
                {
                  PUT16(d + si, '\012', flags);
                }
              else if (flags & UTF_BUFSTOP) break;
              si += 2;
            }
          i = si;
        }
      else                                              /* 7-bit 1 byte */
        {
        chkit:
          ch = *s;
          ss = (CONST char *)s + 1;
          if ((flags & UTF_XMLSAFE) && !TX_UNICODE_CODEPOINT_IS_VALID_XML(ch))
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, Utf8, destCharset,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              ch = TX_INVALID_CHAR;
              goto one16bit;
            }
          if (!HTNOESC(*s) && (flags & UTF_ESCLOW))     /* 7-bit HTML esc. */
            {
            doesc:
              si = i;
              for (esc = html2esc((unsigned)ch, buf, sizeof(buf), pmbuf);
                   *esc != '\0';
                   esc++, si += 2)
                {
                  ch = *esc;
                  if (si + 1 >= dlen) continue;
                  PUT16(d + si, ch, flags);
                }
              if (si > dlen && (flags & UTF_BUFSTOP)) break;
              i = si;
            }
          else                                          /* 7-bit as-is */
            {
            one16bit:
              if (i + 1 < dlen)                         /* room for 16 bits */
                {
                  PUT16(d + i, ch, flags);
                }
              else if (flags & UTF_BUFSTOP) break;
              i += 2;
            }
        }
      s = (CONST byte *)ss;
      if (flags & UTF_ONECHAR) break;
    }
stop:
  *sp = (CONST char *)s;
  *dtot += i;
  return(i);
#undef PUT16
}

size_t
htutf16_to_utf8(d, dlen, dtot, sp, slen, flags, state, width, htpfobj, pmbuf)
char            *d;     /* destination buffer */
size_t          dlen;   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot;  /* accumulating destination payload count */
CONST char      **sp;   /* *sp is source string */
size_t          slen;   /*    ""    ""   length (-1 == strlen(s)) */
UTF             flags;
UTF             *state; /* state variable (pass as-is on all related calls) */
int             width;  /* max chars per line (ignored) */
HTPFOBJ           *htpfobj; /* (in/out, opt.) (ignored) */
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
/* Translates multibyte UTF-16 character buffer `*sp' to multibyte
 * UTF-8 buffer `d', which may not be the same pointer (length may
 * increase).  Returns would-be length of `d'; if > `dlen', not
 * written past.  `d' is not nul-terminated, and may be truncated
 * mid-char if short unless UTF_BUFSTOP set.  Advances `*sp' past
 * decoded source.  Illegal, short-buf, out-of-range chars are
 * transposed as TX_INVALID_CHAR unless otherwise noted.  `d' size should be
 * at least 6 bytes for forward progress (e.g. for `&quot;' or 6-byte UTF-8).
 * UTF_ONECHAR        Only decode 1 character
 * UTF_ESCRANGE       Ignored (dest range > src range)
 * UTF_ESCLOW         HTML-escape low (7-bit) chars < > " &
 * UTF_BUFSTOP        Stop decoding when out of `d' buffer space
 * UTF_FINAL          Must be set if this is the last of multiple buffers
 * UTF_BINARY         Ignored (quoted-printable)
 * UTF_BIGENDIAN      Assume source is big-endian
 * UTF_LITTLEENDIAN   Assume source is little-endian
 * UTF_SAVEBOM        Save leading BOM in output
 * UTF_START          This is the first buffer (required for 1st buffer)
 * UTF_CRNL           Map CR/LF/CRLF newlines to CR
 * UTF_LFNL           Map CR/LF/CRLF newlines to LF
 * UTF_DO8BIT         Ignored (ISO-8859-1 -> ISO-8859-1)
 * UTF_HTMLDECODE     HTML-unescape sequences and output as target char
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Output XML-safe chars only
 * UTF_BADENCASISO    Ignored (wtf what to interpret bad UTF-16 as?)
 * UTF_BADENCASISOERR Ignored
 * UTF_QENCONLY       Ignored
 * UTF_NOCHARTRANSLATE WTF need to implement in this function
 * >>>>> NOTE: See also htpf_engine() <<<<<
 */
{
  static CONST char     fn[] = "htutf16_to_utf8";
  CONST byte            *se, *s, *e, *esav;
  char                  *esc;
  const char            *srcCharset;
  int                   val, val2, ch;
  size_t                sz, i, si;
  char                  buf[EPI_OS_INT_BITS/3 + 6], buf2[EPI_OS_INT_BITS/3+6];
  /* Decode big- or little-endian 16-bits at `s' according to `flags'.
   * Note that we default to big-endian if unknown as per RFC 2781:
   */
#define GET16(s, flags)                                                 \
 (((flags) & UTF_LITTLEENDIAN) ? (((int)(((byte *)(s))[1]) << 8) +      \
   (int)*(byte *)(s)) : (((int)*(byte *)(s) << 8) + (int)(((byte *)(s))[1])))

  (void)width;
  (void)htpfobj;

  srcCharset = ((flags & UTF_LITTLEENDIAN) ? Utf16Le : Utf16Be);

  s = (CONST byte *)(*sp);
  if (slen == (size_t)(-1)) slen = strlen((char *)s);
  if (dlen == (size_t)(-1)) dlen = (d == (char *)s ? slen : strlen(d));
  si = i = 0;
  se = s + slen;
  if (flags & UTF_START)                                /* deal with BOM */
    {
      if (s + 1 >= se) goto trunc;                      /* short buffer */
      if (*s >= 0xfe && s[1] >= 0xfe && s[1] != *s)     /* it's a BOM */
        {
          if (!(flags & (UTF_BIGENDIAN | UTF_LITTLEENDIAN)))
            flags |= (*s == 0xfe ? UTF_BIGENDIAN : UTF_LITTLEENDIAN);
          if (!(flags & UTF_SAVEBOM)) s += 2;           /* skip the BOM */
        }
      else if (!(flags & (UTF_BIGENDIAN | UTF_LITTLEENDIAN)))
        {                                               /* guess endianness */
          int   numChars;

          esav = (se - s > 256 ? s + 256 : se) - 1;     /* opt.: fast check */
          val = val2 = 0;                               /* # big/little-end.*/
          for (e = s; e < esav; )
            {
              if (*(e++) == 0) val++;                   /* prob. big-endian */
              if (*(e++) == 0) val2++;                  /* little-endian? */
            }
          ch = numChars = (int)((esav + 1 - s) >> 1);
          if (ch > 16) ch = 16 + ((3*(ch - 16)) >> 2);  /* arbitrary */
          if (val >= ch && val2 <= numChars - ch)
            flags |= UTF_BIGENDIAN;
          else if (val2 >= ch && val <= numChars - ch)
            flags |= UTF_LITTLEENDIAN;
        }
      *state = (flags & ~UTF_BADCHARMSG);
    }
  else                                                  /* copy state */
    {
      flags &= ~(UTF_BIGENDIAN | UTF_LITTLEENDIAN);
      flags |= (*state & (UTF_BIGENDIAN | UTF_LITTLEENDIAN));
    }
  while (s < se)
    {
      si = i;
      if (s + 1 >= se) goto trunc;                      /* short src buf */
      val = GET16(s, flags);
      e = s + 2;                                        /* accepted 16 bits */
      if (val >= TX_UTF16_LO_SURROGATE_BEGIN)           /* error or valid */
        {
          if (val <= TX_UTF16_LO_SURROGATE_END)         /* invalid */
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, srcCharset, Utf8,
                                     InvalidChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
              goto bad;
            }
        }
      else if (val >= TX_UTF16_HI_SURROGATE_BEGIN)      /* 2x16-bit char */
        {
          if (e + 1 >= se)                              /* short src buf */
            {
            trunc:
               if ((flags & UTF_FINAL) && i < dlen)     /* really truncated */
                 {
                   e = se;
                   if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                     TXreportCannotConvert(pmbuf, fn, srcCharset, Utf8,
                                       TruncChar, *sp, (char *)se, (char *)s);
                   *state |= UTF_BADCHARMSG;    /* flag for caller and us */
                   goto bad;
                 }
               break;
            }
          val2 = GET16(e, flags);                       /* get 2nd val */
          if (val2 < TX_UTF16_LO_SURROGATE_BEGIN ||
              val2 > TX_UTF16_LO_SURROGATE_END)         /* invalid */
            {
              if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
                TXreportCannotConvert(pmbuf, fn, srcCharset, Utf8,
                                     InvalidChar, *sp, (char *)se, (char *)s);
              *state |= UTF_BADCHARMSG;         /* flag for caller and us */
            bad:
              val = TX_INVALID_CHAR;
              goto cont2;
            }
          e += 2;                                       /* accepted 16 bits */
          val = (((val & 0x3ff) << 10) | (val2 & 0x3ff)) + 0x10000;
        }
      else if (val == '\r' && (flags & (UTF_CRNL | UTF_LFNL)))
        {
          if (e >= se)                                  /* short src buffer */
            {
              if (!(flags & UTF_FINAL)) break;          /* need more data */
            }
          else if (GET16(e, flags) == '\n') e += 2;     /* skip LF of CRLF */
          goto donewline;
        }
      else if (val == '\n' && (flags & (UTF_CRNL | UTF_LFNL)))
        {
        donewline:
          if (flags & UTF_CRNL)
            {
              if (si < dlen) d[si] = '\015';
              else if (flags & UTF_BUFSTOP) break;
              si++;
            }
          if (flags & UTF_LFNL)
            {
              if (si < dlen) d[si] = '\012';
              else if (flags & UTF_BUFSTOP) break;
              si++;
            }
          goto cont3;
        }
      else if (val == '&' && (flags & UTF_HTMLDECODE))  /* decode HTML esc. */
        {
          esav = e;
          val2 = 0;
          for (esc = buf2; e + 1 < se && (val2 = GET16(e, flags)) < 256 &&
                 val2 != ';' && strchr(TXWhitespace, (char)val2) == CHARPN &&
                 esc < buf2 + sizeof(buf2); e += 2)
            *(esc++) = (char)val2;
          if (e + 1 < se && val2 == ';') e += 2;
          esc = htesc2html((CONST char *)buf2, esc, 0, &sz, &ch,
                           buf, sizeof(buf));
          if (ch <= -1) e = esav;                       /* bad escape */
          else val = ch;
        }
      if (!TX_IS_VALID_UNICODE_CODEPOINT(val))
        {
          if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
            TXreportCannotConvert(pmbuf, fn, srcCharset, Utf8,
                                  InvalidUnicode, *sp, (char *)se, (char *)s);
          *state |= UTF_BADCHARMSG;             /* flag for caller and us */
          val = TX_INVALID_CHAR;
        }
      else if ((flags & UTF_XMLSAFE) &&
               !TX_UNICODE_CODEPOINT_IS_VALID_XML(val))
        {
          if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
            TXreportCannotConvert(pmbuf, fn, srcCharset, Utf8,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
          *state |= UTF_BADCHARMSG;             /* flag for caller and us */
          val = TX_INVALID_CHAR;
        }
      UTF8_ENCODECHAR(d, si, dlen, val, flags);
      goto cont;
    chkit:                                              /* 7-bit char */
      if ((flags & UTF_XMLSAFE) && !TX_UNICODE_CODEPOINT_IS_VALID_XML(val))
        {
          if ((flags & UTF_BADCHARMSG) && !(*state & UTF_BADCHARMSG))
            TXreportCannotConvert(pmbuf, fn, srcCharset, Utf8,
                                  InvalidXmlChar, *sp, (char *)se, (char *)s);
          *state |= UTF_BADCHARMSG;             /* flag for caller and us */
          val = TX_INVALID_CHAR;
          goto cont2;
        }
      if (!HTNOESC(val) && (flags & UTF_ESCLOW))        /* HTML-escape it */
        {
          for (esc = html2esc((unsigned)val, buf, sizeof(buf), pmbuf);
               *esc != '\0';
               esc++, si++)
            if (si < dlen) d[si] = *esc;
          if (si > dlen && (flags & UTF_BUFSTOP)) break;
          si--;
        }
      else                                              /* pass as-is */
        {
        cont2:
          if (si < dlen) d[si] = val;
          else if (flags & UTF_BUFSTOP) break;
        }
    cont:
      si++;
    cont3:
      i = si;
      s = e;
      if (flags & UTF_ONECHAR) break;
    }
  *sp = (CONST char *)s;
  *dtot += i;
  return(i);
#undef GET16
}
/* ------------------------------------------------------------------------- */

/* 1: safe for quoted-printable
 * 2: safe for "Q" encoding, in any and all RFC 2047 uses
 */
static CONST char       IsQPSafe[] =
/*                                  !"#$%&'()*+,-./0123456789:;<=>? */
  "0000000001000000000000000000000013111111113313133333333333111011"
/* @ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~  */
  "1333333333333333333333333331111113333333333333333333333333311110"
  "0000000000000000000000000000000000000000000000000000000000000000"
  "0000000000000000000000000000000000000000000000000000000000000000";
#define ISQPSAFE(ch, mask)      (IsQPSafe[(byte)(ch)] & (mask))
static CONST char       HexUp[] = "0123456789ABCDEF";

size_t
htiso88591_to_quotedprintable(d, dlen, dtot, sp, slen, flags, state, width,
                              htpfobj, pmbuf)
char            *d;     /* destination buffer */
size_t          dlen;   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot;  /* accumulating destination payload count */
CONST char      **sp;   /* *sp is source string */
size_t          slen;   /*    ""    ""   length (-1 == strlen(s)) */
UTF             flags;
UTF             *state;
int             width;  /* max chars per line (0 == no soft line breaks) */
HTPFOBJ           *htpfobj; /* (in/out, opt.) (ignored) */
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
/* Translates single-byte ISO-8859-1 character buffer `*sp' to multibyte
 * quoted-printable buffer `d'.  Returns would-be length of `d'; if > `dlen',
 * not written past.  `d' is not nul-terminated, and may be truncated
 * mid-char if short unless UTF_BUFSTOP set.  Advances `*sp' past
 * decoded source.  `d' size should be at least 5 bytes for forward
 * progress (e.g. for `=20=\n').
 * UTF_ONECHAR        Only decode 1 character
 * UTF_ESCRANGE       Ignored (one-to-one mapping)
 * UTF_ESCLOW         Ignored (UTF-N)
 * UTF_BUFSTOP        Stop decoding when out of `d' buffer space
 * UTF_FINAL          Must be set if this is the last of multiple buffers
 * UTF_BINARY         Binary data: hex-escape CRLF too
 * UTF_BIGENDIAN      Ignored (UTF-16)
 * UTF_LITTLEENDIAN   Ignored (UTF-16)
 * UTF_SAVEBOM        "Q" encoding (RFC 2047): SP to `_', no whitespace output
 * UTF_START          This is the first buffer (required for 1st buffer)
 * UTF_CRNL           Map CR/LF/CRLF newlines to CR
 * UTF_LFNL           Map CR/LF/CRLF newlines to LF
 * UTF_DO8BIT         Ignored (ISO-8859-1 -> ISO-8859-1)
 * UTF_HTMLDECODE     Ignored
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Ignored
 * UTF_BADENCASISO    Ignored
 * UTF_BADENCASISOERR Ignored
 * UTF_QENCONLY       Ignored
 * UTF_NOCHARTRANSLATE WTF need to implement in this function
 * >>>>> NOTE: See also htpf_engine() <<<<<
 */
{
  CONST byte    *se, *s, *e;
  char          mask;
  int           statelastch, ch;
  size_t        i, si, ilastch;

  (void)htpfobj;
  (void)pmbuf;
  mask = ((flags & UTF_SAVEBOM) ? 2 : 1);
  if (flags & UTF_SAVEBOM) width = 0;                   /* no whitespace */
  if (flags & UTF_START) *state = (UTF)0;               /* cur. line width */
  s = (CONST byte *)(*sp);
  if (slen == (size_t)(-1)) slen = strlen((char *)s);
  if (dlen == (size_t)(-1)) dlen = (d == (char *)s ? slen : strlen(d));
  ilastch = si = i = 0;
  statelastch = (int)*state;
  se = s + slen;
  while (s < se)
    {
      e = s + 1;
      ilastch = i;      /* `i' just before next real char printed */
      statelastch = (int)*state;
    again:
      if (e >= se && !(flags & UTF_FINAL)) goto stop;   /* may be NL next */
      si = i;
      ch = *s;
      switch (ch)
        {
        case ' ':
          if (flags & UTF_SAVEBOM) ch = '_';            /* Q encoding */
          goto safe;
        case '\r':
          if (flags & (UTF_SAVEBOM | UTF_BINARY)) goto hexesc;
          if (flags & (UTF_CRNL | UTF_LFNL))            /* eat and replace */
            {
              if (e < se && *e == '\n') e++;
              goto donewline;
            }
          goto cont3;
        case '\n':
          if (flags & (UTF_SAVEBOM | UTF_BINARY)) goto hexesc;
          if (flags & (UTF_CRNL | UTF_LFNL))            /* eat and replace */
            {
            donewline:
              if (flags & UTF_CRNL)
                {
                  if (si < dlen) d[si] = '\015';
                  else if (flags & UTF_BUFSTOP) goto stop;
                  si++;
                }
              if (!(flags & UTF_LFNL)) goto cont2;
              ch = '\012';
            }
        cont3:
          if (si < dlen) d[si] = ch;
          else if (flags & UTF_BUFSTOP) goto stop;
          si++;
        cont2:
          *state = (UTF)0;                              /* new line */
          goto cont;
        }
      if (ISQPSAFE(ch, mask))                           /* ok as-is */
        {
        safe:
          if (e >= se ||                                /* no next char */
              (*e != '\r' && *e != '\n') ||             /* next char not NL */
              (flags & (UTF_SAVEBOM | UTF_BINARY)))     /* encode source NL */
            {
              if (width &&                              /* fixed width */
                  (int)*state > 0 &&                    /* non-empty line */
                  (int)*state + 2 > width)              /* `ch=' past EOL */
                goto softnl;                            /* softNL before ch */
            }
          else if (width &&                             /* fixed width */
                   (int)*state > 0 &&                   /* non-empty line */
                   (int)*state + 1 > width)             /* `ch' past EOL */
            goto softnl;
          else if (ch == ' ' || ch == '\t')             /* SP before hardNL */
            goto hexesc;                                /* escape it */
          if (si < dlen) d[si] = ch;
          else if (flags & UTF_BUFSTOP) goto stop;
        }
      else                                              /* hex escape */
        {
        hexesc:
          if (width &&                                  /* fixed width */
              (int)*state > 0 &&                        /* non-empty line */
              ((int)*state + 3 > width ||               /* hex esc past EOL */
               ((int)*state + 3 == width &&             /* hex esc at EOL */
                ((e >= se || (*e!='\r' && *e!='\n')) || /* no NL next */
                 (flags & (UTF_SAVEBOM|UTF_BINARY)))))) /* encode src NL */
            {
            softnl:
              /* Note that we may stop (via UTF_BUFSTOP) after
               * this soft-NL but before the next char.
               * This is ok, because we reset line length here
               * (2nd reason for *state > 0 checks before softnl):
               */
              if (si < dlen) d[si] = '=';               si++;
              if (flags & UTF_CRNL)
                {
                  if (si < dlen) d[si] = '\015';        si++;
                }
              if ((flags & (UTF_CRNL | UTF_LFNL)) != UTF_CRNL)
                {
                  if (si < dlen) d[si] = '\012';        si++;
                }
              if (si > dlen && (flags & UTF_BUFSTOP)) goto stop;
              i = si;
              *state = (UTF)0;                          /* new line */
              goto again;                               /* same source char */
            }
          if (si < dlen) d[si] = '=';                   si++;
          if (si < dlen) d[si] = HexUp[ch >> 4];        si++;
          if (si < dlen) d[si] = HexUp[ch & 0x0F];
          else if (flags & UTF_BUFSTOP) goto stop;
        }
      *state += (UTF)(++si - i);                        /* current line len */
    cont:
      i = si;
      s = e;
    }
  if ((int)*state > 0 && (flags & UTF_FINAL) && width)
    {                                                   /* soft-NL at end */
      if (si < dlen) d[si] = '=';       si++;
      if (flags & UTF_CRNL)
        {
          if (si < dlen) d[si] = '\015';    si++;
        }
      if ((flags & (UTF_CRNL | UTF_LFNL)) != UTF_CRNL)
        {
          if (si < dlen) d[si] = '\012';    si++;
        }
      if (si > dlen && (flags & UTF_BUFSTOP))
        {
          /* Since we've used all the source, caller won't call us again
           * to finish this soft-NL.  So back off the last character,
           * in `s', `i' and `*state':
           */
          s--;
          i = ilastch;
          *state = (UTF)statelastch;
          goto stop;
        }
      i = si;
      *state = (UTF)0;                                  /* new line */
    }
stop:
  if (flags & UTF_FINAL) *state &= ~UTF_BADCHARMSG;
  *sp = (CONST char *)s;
  *dtot += i;
  return(i);
}

size_t
htquotedprintable_to_iso88591(d, dlen, dtot, sp, slen, flags, state, width,
                              htpfobj, pmbuf)
char            *d;     /* destination buffer */
size_t          dlen;   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot;  /* accumulating destination payload count */
CONST char      **sp;   /* *sp is source string */
size_t          slen;   /*    ""    ""   length (-1 == strlen(s)) */
UTF             flags;
UTF             *state; /* state variable (pass as-is on all related calls) */
int             width;  /* max chars per line (ignored) */
HTPFOBJ           *htpfobj; /* (in/out, opt.) (ignored) */
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
/* Translates multi-byte quoted-printable character buffer `*sp' to
 * single-byte ISO-8859-1 buffer `d'.  Returns would-be length of `d';
 * if > `dlen', not written past.  `d' is not nul-terminated, and may
 * be truncated mid-char if short unless UTF_BUFSTOP set.  Advances
 * `*sp' past decoded source.  Illegal, short-buf, out-of-range chars
 * are transposed as-is unless otherwise noted (a broken quoted-printable
 * escape is more likely to be intended as-is than a broken UTF-8 encoding,
 * and `?' is normally encoded, so decoding it could be misconstrued as
 * a broken escape).
 * UTF_ONECHAR        Only decode 1 character
 * UTF_ESCRANGE       Ignored (UTF-N)
 * UTF_ESCLOW         Ignored (UTF-N)
 * UTF_BUFSTOP        Stop decoding when out of `d' buffer space
 * UTF_FINAL          Must be set if this is the last of multiple buffers
 * UTF_BINARY         Ignored (encode)
 * UTF_BIGENDIAN      Ignored (UTF-16)
 * UTF_LITTLEENDIAN   Ignored (UTF-16)
 * UTF_SAVEBOM        "Q" encoding (RFC 2047): SP to `_', no whitespace output
 * UTF_START          This is the first buffer (required for 1st buffer)
 * UTF_CRNL           Map CR/LF/CRLF newlines to CR
 * UTF_LFNL           Map CR/LF/CRLF newlines to LF
 * UTF_DO8BIT         Ignored (ISO-8859-1 -> ISO-8859-1)
 * UTF_HTMLDECODE     Ignored
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Ignored
 * UTF_BADENCASISO    Ignored
 * UTF_BADENCASISOERR Ignored
 * UTF_QENCONLY       Ignored
 * UTF_NOCHARTRANSLATE WTF need to implement in this function
 * >>>>> NOTE: See also htpf_engine() <<<<<
 */
{
  CONST byte    *se, *s, *e;
  int           ch;
  size_t        i, si;
#define CHKSHORTBUF()                   \
  if (e >= se)                          \
    {                                   \
      if (flags & UTF_FINAL) goto bad;  \
      goto stop;                        \
    }

  (void)width;
  (void)htpfobj;
  (void)pmbuf;
  s = (CONST byte *)(*sp);
  if (slen == (size_t)(-1)) slen = strlen((char *)s);
  if (dlen == (size_t)(-1)) dlen = (d == (char *)s ? slen : strlen(d));
  i = 0;
  se = s + slen;
  if (flags & UTF_START) *state = (flags & ~UTF_BADCHARMSG);
  while (s < se)                                        /* more source data */
    {
      si = i;
      ch = *s;
      e = s + 1;
      switch (ch)
        {
        case '=':                                       /* escape sequence */
          CHKSHORTBUF();
          switch (*e)                                   /* what's next */
            {
            case ' ':
            case '\t':                                  /* trailing space? */
              while (++e < se && (*e == ' ' || *e == '\t'));
              CHKSHORTBUF();
              if (*e == '\n') goto softlf;              /* `=<SP><SP>...\n' */
              if (*e != '\r') goto bad;                 /* `=<SP><SP>...X' */
              /* fall through: */
            case '\r':                                  /* soft-NL */
              if (++e >= se)                            /* short buffer */
                {
                  if (!(flags & UTF_FINAL)) goto stop;  /* need more data */
                }
              else if (*e == '\n') e++;                 /* skip LF of CRLF */
              break;                                    /* nothing to print */
            case '\n':                                  /* soft-NL */
            softlf:
              e++;
              break;                                    /* nothing to print */
            default:                                    /* hex escape? */
              if (*e >= '0' && *e <= '9') ch = *e - '0';
              else if (*e >= 'A' && *e <= 'F') ch = (*e - 'A') + 10;
              else if (*e >= 'a' && *e <= 'f') ch = (*e - 'a') + 10;
              else goto bad;                            /* `=X' */
              e++;
              CHKSHORTBUF();
              ch <<= 4;
              if (*e >= '0' && *e <= '9') ch |= *e - '0';
              else if (*e >= 'A' && *e <= 'F') ch |= (*e - 'A') + 10;
              else if (*e >= 'a' && *e <= 'f') ch |= (*e - 'a') + 10;
              else goto bad;                            /* `=9X' */
              e++;
              goto one;                                 /* print unesc char */
            }
          break;
        case '_':                                       /* header space? */
          if (flags & UTF_SAVEBOM) ch = ' ';
          goto one;
        case '\r':
          if (!(flags & (UTF_CRNL | UTF_LFNL))) goto one;
          if (e >= se)                                  /* short src buffer */
            {
              if (!(flags & UTF_FINAL)) goto stop;      /* need more data */
            }
          else if (*e == '\n') e++;                     /* skip LF of CRLF */
          goto donewline;
        case '\n':
          if (!(flags & (UTF_CRNL | UTF_LFNL))) goto one;
        donewline:
          if (flags & UTF_CRNL)
            {
              if (si < dlen) d[si] = '\015';
              else if (flags & UTF_BUFSTOP) goto stop;
              si++;
            }
          if (flags & UTF_LFNL)
            {
              if (si < dlen) d[si] = '\012';
              else if (flags & UTF_BUFSTOP) goto stop;
              si++;
            }
          break;
        case ' ':
        case '\t':                                      /* trailing space? */
          while (e < se && (*e == ' ' || *e == '\t')) e++;
          if (e >= se)                                  /* short buffer */
            {
              if (!(flags & UTF_FINAL)) goto stop;      /* need more data */
            }
          else if (*e == '\r' || *e == '\n') break;     /* `<SP><SP>...\n' */
          /* Optimization: copy up to `e', instead of just next char.
           * Thus if `s' is a large string, we avoid repeated scans.
           * Ok if we need to bail in the middle due to UTF_BUFSTOP.
           * WTF do this optimization across UTF_BUFSTOP calls?
           */
        bad:    /* copy as-is, could be ASCII.  wtf flag as bad? */
          if (flags & UTF_ONECHAR) { ch = *s; goto one; }
          while (s < e)
            {
              if (i < dlen) d[i] = *s;
              else if (flags & UTF_BUFSTOP) goto stop;
              s++;
              i++;
            }
          continue;
        default:                                        /* as-is char */
        one:
          if (si < dlen) d[si] = ch;
          else if (flags & UTF_BUFSTOP) goto stop;
          si++;
          break;
        }
      i = si;
      s = e;
      if (flags & UTF_ONECHAR) break;                   /* WTF iff i > si */
    }
stop:
  *sp = (CONST char *)s;
  *dtot += i;
  return(i);
#undef CHKSHORTBUF
}

/* ------------------------------------------------------------------------- */

static CONST char       Base64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


size_t
htencodebase64(d, dlen, dtot, sp, slen, flags, state, width, htpfobj, pmbuf)
char            *d;     /* destination buffer */
size_t          dlen;   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot;  /* accumulating destination payload count */
CONST char      **sp;   /* *sp is source string */
size_t          slen;   /*    ""    ""   length (-1 == strlen(s)) */
UTF             flags;
UTF             *state;
int             width;  /* max chars per line (0 == no newlines) */
HTPFOBJ           *htpfobj; /* (in/out, opt.) (ignored) */
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
/* Translates character buffer `*sp' to base64-encoded buffer `d',
 * which should not be the same pointer (base64 is longer.
 * Returns would-be length of `d'; if > `dlen', not written past.  `d'
 * is not nul-terminated, and may be truncated mid-char if short
 * unless UTF_BUFSTOP set.  Advances `*sp' past decoded source.
 * UTF_ONECHAR        Ignored (UTF-N)
 * UTF_ESCRANGE       Ignored (UTF-N)
 * UTF_ESCLOW         Ignored (UTF-N)
 * UTF_BUFSTOP        Stop encoding integrally when out of `d' buffer space
 * UTF_FINAL          Must be set if this is the last of multiple buffers
 * UTF_BINARY         Ignored (quoted-printable)
 * UTF_BIGENDIAN      Ignored (UTF-16)
 * UTF_LITTLEENDIAN   Ignored (UTF-16)
 * UTF_SAVEBOM        Ignored (UTF-16)
 * UTF_START          Ignored (UTF-16)
 * UTF_CRNL           Output CR for newlines
 * UTF_LFNL           Output LF for newlines
 * UTF_DO8BIT         Ignored (ISO-8859-1 -> ISO-8859-1)
 * UTF_HTMLDECODE     Ignored
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Ignored
 * UTF_BADENCASISO    Ignored
 * UTF_BADENCASISOERR Ignored
 * UTF_QENCONLY       Ignored
 * UTF_NOCHARTRANSLATE WTF need to implement in this function
 * >>>>> NOTE: See also htpf_engine(), htiso88591_to_utf8() <<<<<
 */
{
  CONST byte    *s, *se1, *se, *e;
  size_t        tot, i, si;

  (void)htpfobj;
  (void)pmbuf;
  /* Note that we never translate source-data newlines.
   * UTF_CRNL/UTF_LFNL only affect newlines in the output:
   */
  if (!(flags & (UTF_CRNL | UTF_LFNL))) flags |= UTF_LFNL;

  if (width <= 0) width = 0;            /* sanity */
  else if (width < 4) width = 4;        /* minimum */
  tot = *dtot % (width ? width : 1);
  *dtot -= tot;
  s = (CONST byte *)(*sp);
  if (slen == (size_t)(-1)) slen = strlen((char *)s);
  if (dlen == (size_t)(-1)) dlen = (d == (char *)s ? slen : strlen(d));
  i = 0;
  se = s + slen;
  se1 = s + (slen/3)*3;
  if (flags & UTF_START) *state = (flags & ~UTF_BADCHARMSG);
  while (s < se1)
    {
      e = s + 3;
      si = i;
      if (si < dlen) d[si] = Base64[(s[0] >> 2)];                       si++;
      if (si < dlen) d[si] = Base64[((s[0] & 0x3) << 4) | (s[1] >> 4)]; si++;
      if (si < dlen) d[si] = Base64[((s[1] & 0xF) << 2) | (s[2] >> 6)]; si++;
      if (si < dlen) d[si] = Base64[s[2] & 0x3F];
      else if (flags & UTF_BUFSTOP) goto stop;
      si++;
      if (width && tot >= (size_t)width - 4)    /* reached max line width */
        {
          if (flags & UTF_CRNL)
            {
              if (si < dlen) d[si] = '\015';
              else if (flags & UTF_BUFSTOP) goto stop;
              si++;
            }
          if (flags & UTF_LFNL)
            {
              if (si < dlen) d[si] = '\012';
              else if (flags & UTF_BUFSTOP) goto stop;
              si++;
            }
          *dtot += tot + 4;
          tot = 0;
        }
      else
        tot += 4;
      i = si;
      s = e;
    }
  if (!(flags & UTF_FINAL)) goto stop;
  si = i;
  e = s;
  if (s + 1 == se)                              /* one extra char */
    {
      e = s + 1;
      if (si < dlen) d[si] = Base64[(s[0] >> 2)];                       si++;
      if (si < dlen) d[si] = Base64[(s[0] & 0x3) << 4];                 si++;
      if (si < dlen) d[si] = '=';                                       si++;
      if (si < dlen) d[si] = '=';
      else if (flags & UTF_BUFSTOP) goto stop;
      si++;
      tot += 4;
    }
  else if (s + 2 == se)                         /* 2 extra chars */
    {
      e = s + 2;
      if (si < dlen) d[si] = Base64[(s[0] >> 2)];                       si++;
      if (si < dlen) d[si] = Base64[((s[0] & 0x3) << 4) | (s[1] >> 4)]; si++;
      if (si < dlen) d[si] = Base64[(s[1] & 0xF) << 2];                 si++;
      if (si < dlen) d[si] = '=';
      else if (flags & UTF_BUFSTOP) goto stop;
      si++;
      tot += 4;
    }
  if (width && tot > 0)                         /* newline at end */
    {
      if (flags & UTF_CRNL)
        {
          if (si < dlen) d[si] = '\015';
          else if (flags & UTF_BUFSTOP) goto stop;
          si++;
        }
      if (flags & UTF_LFNL)
        {
          if (si < dlen) d[si] = '\012';
          else if (flags & UTF_BUFSTOP) goto stop;
          si++;
        }
    }
  i = si;
  s = e;
stop:
  *sp = (CONST char *)s;
  *dtot += tot;
  return(i);
}

size_t
htdecodebase64(d, dlen, dtot, sp, slen, flags, state, width, htpfobj, pmbuf)
char            *d;     /* destination buffer */
size_t          dlen;   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot;  /* accumulating destination payload count */
CONST char      **sp;   /* *sp is source string */
size_t          slen;   /*    ""    ""   length (-1 == strlen(s)) */
UTF             flags;
UTF             *state;
int             width;  /* max chars per line (ignored) */
HTPFOBJ           *htpfobj; /* (in/out, opt.) (ignored) */
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
/* Translates base64-encoded buffer `*sp' to normal buffer `d', which
 * may be the same pointer (base64 is longer).  Returns would-be
 * length of `d'; if > `dlen', not written past.  `d' is not
 * nul-terminated, and may be truncated mid-char if short unless
 * UTF_BUFSTOP set.  Advances `*sp' past decoded source.
 * UTF_ONECHAR        Ignored (UTF-N)
 * UTF_ESCRANGE       Ignored (UTF-N)
 * UTF_ESCLOW         Ignored (UTF-N)
 * UTF_BUFSTOP        Stop decoding integrally when out of `d' buffer space
 * UTF_FINAL          Ignored (encode)
 * UTF_BINARY         Ignored (quoted-printable)
 * UTF_BIGENDIAN      Ignored (UTF-16)
 * UTF_LITTLEENDIAN   Ignored (UTF-16)
 * UTF_SAVEBOM        Ignored (UTF-16)
 * UTF_START          Ignored (UTF-16)
 * UTF_CRNL           Ignored (encode)
 * UTF_LFNL           Ignored (encode)
 * UTF_DO8BIT         Ignored (ISO-8859-1 -> ISO-8859-1)
 * UTF_HTMLDECODE     Ignored
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Ignored
 * UTF_BADENCASISO    Ignored
 * UTF_BADENCASISOERR Ignored
 * UTF_QENCONLY       Ignored
 * UTF_NOCHARTRANSLATE WTF need to implement in this function
 * >>>>> NOTE: See also htpf_engine(), htutf8_to_iso88591() <<<<<
 */
{
  CONST byte    *s, *se, *e;
  char          *c = CHARPN;
  int           n = 0, j = 0;
  size_t        i, si;

  (void)width;
  (void)htpfobj;
  (void)pmbuf;
  s = (CONST byte *)(*sp);
  if (slen == (size_t)(-1)) slen = strlen((char *)s);
  if (dlen == (size_t)(-1)) dlen = (d == (char *)s ? slen : strlen(d));
  i = 0;
  se = s + slen;
  e = s;
  if (flags & UTF_START) *state = (flags & ~UTF_BADCHARMSG);
  while (s < se)
    {
      si = i;
      e = s;
      for (n = j = 0; j < 4; j++, e++)  /* decode 4 chars into 24 bits */
        {
          while (e < se && (*e=='\0' || (c=strchr(Base64,*e)) == CHARPN)) e++;
          if (e >= se) goto chktrim;    /* end of source buffer */
          n = (n << 6) + (c - Base64);
        }
      if (si < dlen) d[si] = (n >> 16); si++;
      if (si < dlen) d[si] = (n >> 8);  si++;
      if (si < dlen) d[si] = n;
      else if (flags & UTF_BUFSTOP) goto stop;
      i = si + 1;                       /* integrally advance source/dest */
      s = e;
    }
chktrim:
  si = i;
  switch (j)                            /* take care of remainder chars */
    {
    case 3:                             /* 2 additional decode bytes */
      if (si < dlen) d[si] = (n >> 10); si++;
      if (si < dlen) d[si] = (n >> 2);
      else if (flags & UTF_BUFSTOP) goto stop;
      goto inc;
    case 2:                             /* 1 additional decode bytes */
      if (si < dlen) d[si] = (n >> 4);
      else if (flags & UTF_BUFSTOP) goto stop;
    inc:
      i = si + 1;                       /* integrally advance source/dest */
      s = e;
      break;
    case 0:                             /* i.e. trailing whitespace */
      s = se;                           /* skip whitespace */
      break;
    }
stop:
  *sp = (CONST char *)s;
  *dtot += i;
  return(i);
}

size_t
TXencodedWordToUtf8(d, dlen, dtot, sp, slen, flags, state, width, htpfobj,
                    pmbuf)
char            *d;     /* destination buffer */
size_t          dlen;   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot;  /* accumulating destination payload count */
CONST char      **sp;   /* *sp is source string */
size_t          slen;   /*    ""    ""   length (-1 == strlen(*sp)) */
UTF             flags;
UTF             *state; /* (in/out) cross-call state */
int             width;  /* max chars per line (ignored) */
HTPFOBJ           *htpfobj; /* (in/out, opt.) settings for charset conversion */
TXPMBUF         *pmbuf;
/* >>> NOTE: unlike other HTCHARSETFUNCs, this one might "buffer"
 * >>> (i.e. fail to consume) a large amount of source (*sp) -- up to
 * >>> MAX_ENCODED_WORD_INPUT_LEN -- until an encoded-word terminator is
 * >>> reached.  Other funcs only "buffer" a few bytes, e.g. an entire
 * >>> UTF-8 or HTML sequence.
 * Translates RFC 2047 style encoded-word string buffer `*sp' to
 * UTF-8 buffer `d', which should not be the same pointer.
 * Returns would-be length of `d'; if > `dlen', not written past.
 * `d' is not nul-terminated, and may be truncated mid-char if short
 * unless UTF_BUFSTOP set.  Advances `*sp' past decoded source.
 * Non-encoded-word parts of `*sp' are assumed to be already UTF-8.
 * If `htpfobj' is NULL, only internal, factory-default charsets recognized.
 * UTF_ONECHAR        Ignored
 * UTF_ESCRANGE       Ignored (UTF-8 output has enough range)
 * UTF_ESCLOW         HTML-escape low (7-bit) chars < > " &
 * UTF_BUFSTOP        Stop decoding integrally when out of `d' buffer space
 * UTF_FINAL          Must be set if this is the last of multiple buffers
 * UTF_BINARY         Ignored (encode)
 * UTF_BIGENDIAN      Ignored (UTF-16)
 * UTF_LITTLEENDIAN   Ignored (UTF-16)
 * UTF_SAVEBOM        Ignored (UTF-16)
 * UTF_START          This is the first buffer (required for 1st buffer)
 * UTF_CRNL           Map CR/LF/CRLF newlines to CR
 * UTF_LFNL           Map CR/LF/CRLF newlines to LF
 * UTF_DO8BIT         Ignored (ISO-8859-1 -> ISO-8859-1)
 * UTF_HTMLDECODE     HTML-unescape sequences (outside of encoded words)
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Output XML-safe chars only
 * UTF_BADENCASISO    Interpret bad UTF-8 sequence as ISO-8859-1
 * UTF_BADENCASISOERR Issue msg if "" and UTF_BADCHARMSG
 * UTF_QENCONLY       Ignored (used by TXutf8ToEncodedWord())
 * UTF_NOCHARTRANSLATE WTF need to implement in this function
 */
{
  static CONST char     fn[] = "TXencodedWordToUtf8";
  static CONST char     linearWhiteSpace[] = " \t\r\n";
  static CONST char     stopChars[] = " \t\r\n=";
  CONST char            *srcOrg, *utf8DataPtr;
  CONST char            *src, *srcEnd, *srcEndLessOne, *encWordDataEnd;
  CONST char            *charset, *charsetEnd, *lang, *langEnd, *s;
  CONST char            *encoding, *encodingEnd, *encText, *encTextEnd;
  CONST char            *decodeSrc, *encWordStart, *encWordEnd;
  char                  *dest, *destEnd, *newBuf;
  UTF                   decodeFlags, decodeState;
  HTCHARSETFUNC         *decodeFunc;
  char                  decodeBufTmp[512], *decodeBufAlloced = CHARPN;
  char                  *decodeBuf = decodeBufTmp;
  size_t                decodeBufAllocedSz = 0, subDestTot;
  size_t                decodeBufSz = sizeof(decodeBufTmp);
  size_t                decodeTotal, decodeSz, newSz, numQues;
  char                  srcCharsetStrTmp[512], *srcCharsetStrAlloced = CHARPN;
  char                  *srcCharsetStr;
  HTCHARSET             srcCharset;
  CONST HTCSCFG         *charsetConfig;
  CONST HTCSINFO        *srcCharsetInfo;
  char                  *utf8Data = CHARPN;
  size_t                utf8DataSz = 0, osCopySz;
  int                   encWordIsValid = 0;
  int                   saveDo8bit = 0, saveBadEncAsIso = 0;
  int                   saveBadEncAsIsoErr = 0, transbufFlags;
  int                   saveBadCharMsg = 0;
#define DEST_LEFT_SZ    (dest < destEnd ? (size_t)(destEnd - dest) : (size_t)0)
  /* OUTPUT_STR(): copy `os' of length `sz' to output.
   * Should be paired with a `src' advance afterwards, and a `break' here
   * should break out of main `while' loop:
   */
#define OUTPUT_STR(os, sz)                                              \
  {                                                                     \
    osCopySz = TX_MIN((size_t)(sz), DEST_LEFT_SZ);                      \
    if (osCopySz > 0) memcpy(dest, (os), osCopySz);                     \
    if (osCopySz < (size_t)(sz) && (flags & UTF_BUFSTOP)) break;        \
    dest += (sz);                                                       \
  }
  /* We use `*state & UTF_ONECHAR' to indicate that the immediately
   * previous source was an encoded word.  Note that this differs from
   * UTF_BADCHARMSG which htutf8_to_utf8() uses; if we add state flags
   * ensure they do not conflict with htutf8_to_utf8() state flags:
   */
#define STATEFLAG_PREV_IS_ENC_WORD      UTF_ONECHAR
  /* RFC 2047 defines a max of 75 chars for encoded words, so that
   * parsers do not have to buffer too much.  We theoretically have no
   * limit to encoded word lengths, but set a max anyway, so that we
   * do not "buffer" (fail to advance `*sp') a lot if no `?='
   * terminator is present yet; excessive source "buffering" might
   * cause the caller problems, i.e. having to keep a large source buffer:
   */
#define MAX_ENCODED_WORD_INPUT_LEN      1000
  /* Define EPI_REQUIRE_WHITESPACE_AROUND_ENCODED_WORDS to require
   * encoded words to have whitespace around them, as per RFC 2047
   * (e.g. decode `foo =?UTF-8?Q?=C3=B8?= bar' but not
   * `foo=?UTF-8?Q?=C3=B8?=bar').  We are more relaxed by default, as
   * is the real world, so we #undef this to decode the latter too:
   */
#undef EPI_REQUIRE_WHITESPACE_AROUND_ENCODED_WORDS

  (void)width;
  src = srcOrg = (*sp);
  dest = d;
  if (slen == (size_t)(-1)) slen = strlen(src);
  if (dlen == (size_t)(-1)) dlen = (dest == src ? slen : strlen(d));
  srcEnd = src + slen;
  destEnd = dest + dlen;
  srcEndLessOne = srcEnd - 1;
  transbufFlags = 0x1;                          /* to UTF-8 */

  if (htpfobj != HTPFOBJPN)
    {
      if (pmbuf == TXPMBUFPN) pmbuf = htpfobj->pmbuf;
      charsetConfig = htpfobj->charsetconfig;
      /* Let some of our UTF_... flags override `htpfobj' flags: */
      saveDo8bit = htpfobj->do8bit;
      saveBadEncAsIso = htpfobj->Utf8BadEncAsIso88591;
      saveBadEncAsIsoErr = htpfobj->Utf8BadEncAsIso88591Err;
      saveBadCharMsg = htpfobj->charsetmsgs;
      if (flags & UTF_DO8BIT)
        htpfobj->do8bit = TXbool_True;
      else
        htpfobj->do8bit = TXbool_False;
      if (flags & UTF_BADENCASISO)
        htpfobj->Utf8BadEncAsIso88591 = TXbool_True;
      else
        htpfobj->Utf8BadEncAsIso88591 = TXbool_False;
      if (flags & UTF_BADENCASISOERR)
        htpfobj->Utf8BadEncAsIso88591Err = TXbool_True;
      else
        htpfobj->Utf8BadEncAsIso88591Err = TXbool_False;
      if (flags & UTF_BADCHARMSG)
        htpfobj->charsetmsgs = TXbool_True;
      else
        htpfobj->charsetmsgs = TXbool_False;
    }
  else
    charsetConfig = HTCSCFGPN;

  if (flags & UTF_START) *state = (UTF)0;       /* init state */

  /* `src' does not get advanced until we actually consume data.
   * `s' is a temp var that is advanced as we parse, before we know
   * if we are going to consume the data yet:
   */

  while (src < srcEnd)
    {
      /* Look for start of encoded word, i.e. `=?' (possibly after
       * linear white space).  We can copy any other character as-is
       * until then.  (We need to pause at linear white space even if
       * EPI_REQUIRE_WHITESPACE_AROUND_ENCODED_WORDS is not set,
       * because we still might have to eat this space if it is
       * between two encoded words, which we do not know yet.):
       */
#ifdef EPI_REQUIRE_WHITESPACE_AROUND_ENCODED_WORDS
    srcCharNoDecode:
#endif /* EPI_REQUIRE_WHITESPACE_AROUND_ENCODED_WORDS */
      s = src;
      /* Advance `s' past leading non-`stopChars' in `src': */
    srcToSNoDecode:
      for ( ;
           s < srcEnd && (*s == '\0' || strchr(stopChars, *s) == CHARPN);
           s++)
        ;
      /* Send `src' to `s' to output: */
      if (s > src)                              /* leading non-`stopChars' */
        {
          subDestTot = 0;
          /* Since `stopChars' is just whitespace/`=', we know `s'
           * does not stop mid-UTF-8 char (nor mid-HTML-sequence),
           * unless the source is indeed truncated mid-sequence.  So
           * UTF-8 parsing to `s' is safe here, as is passing our
           * UTF_START/UTF_FINAL flags.  Passing our `state' is ok too;
           * we ensured above that our state flags do not conflict:
           */
          newSz = htutf8_to_utf8(dest, DEST_LEFT_SZ, &subDestTot, &src,
                                 s - src, flags, state, 0, htpfobj, pmbuf);
          if (newSz == (size_t)0 && (flags & UTF_BUFSTOP)) break;
          dest += newSz;
          *state &= ~STATEFLAG_PREV_IS_ENC_WORD;
          continue;
        }

      /* We are at linear white space or `='.  Look forward for potential
       * start of encoded word.  Note that our interpretation of
       * `linear white space' is not the same as RFC 2047/822:
       * we allow any string of space, tab, CR or LF chars for ease of
       * parsing:
       */
      for (s = src;
           s < srcEnd && *s != '\0' && strchr(linearWhiteSpace, *s) != CHARPN;
           s++)
        ;                                       /* skip linear white space */
      encWordStart = s;                         /* potentially */
      if (s >= srcEnd)                          /* out of data */
        {
          if (flags & UTF_FINAL) goto srcToSNoDecode;
          else break;                           /* wait for more data */
        }
#ifdef EPI_REQUIRE_WHITESPACE_AROUND_ENCODED_WORDS
      /* `=?' must be at start of buffer or after linear white space: */
      if (s == src &&                           /* no LWSP before & */
          !((flags & UTF_START) && s == srcOrg))/*   not start of all data */
        {                                       /* then not enc-word start */
          goto srcCharNoDecode;
        }
#endif /* EPI_REQUIRE_WHITESPACE_AROUND_ENCODED_WORDS */
      if (*s != '=')
        {
          s++;
          goto srcToSNoDecode;
        }
      s++;                                      /* skip opening `=' */
      if (s >= srcEnd)                          /* out of data */
        {
          if (flags & UTF_FINAL) goto srcToSNoDecode;
          else break;                           /* wait for more data */
        }
      if (*s != '?')                            /* not encoded word start */
        {
          s++;
          goto srcToSNoDecode;
        }
      s++;                                      /* skip opening `?' */

      /* `s' now points to the start of encoded word payload data
       * (just past opening `=?').  Find the end of the encoded word
       * by looking for closing `?=':
       */
      encWordIsValid = 1;                       /* until we know otherwise */
      numQues = 0;
      for (encWordDataEnd=s; encWordDataEnd < srcEndLessOne; encWordDataEnd++)
        {
          if (*encWordDataEnd == '?') numQues++;
          if ((*encWordDataEnd == '?' && encWordDataEnd[1] == '=' &&
               /* Do not be confused by false end delimiter at start of
                * Q-encoded word data, e.g. `=?UTF-8?Q?=C3=B8?=':
                *                                     ^^
                */
               numQues != 2) ||
              /* Also look for another opening `=?' delimiter, in case
               * bad syntax and a stray `=?' was never closed or not
               * really an encoded word.  But do not be confused by end
               * of base64 encoded word, e.g. `...ZA==?=', which is
               * base64 `ZA==' and end delimiter `?=', not base64 `ZA='
               * and false start delimiter `=?' and `='.
               */
              (*encWordDataEnd == '=' && encWordDataEnd[1] == '?' &&
               encWordDataEnd + 2 < srcEnd && encWordDataEnd[2] != '='))
            break;
        }
      if (encWordDataEnd < srcEndLessOne)       /* `?=' or '=?' found */
        {
          if (*encWordDataEnd == '=' && encWordDataEnd[1] == '?')
            {
              /* Found another opening delimiter before the end delimiter.
               * Syntax violation; try to recover by assuming the first
               * opening delimiter we found was bogus, so flush and retry:
               */
              goto srcToSNoDecode;
            }
          /* else we must have found the `?=' terminator */
          encWordEnd = encWordDataEnd + 2;      /* +2 for `?=' terminator */
        }
      else                                      /* `?=' terminator not found*/
        {
          /* If this is not the final buffer, wait for more input so
           * we can find the terminator:
           */
          if (!(flags & UTF_FINAL) &&
              srcEnd - src < MAX_ENCODED_WORD_INPUT_LEN)
            break;                              /* need more input */
          /* Missing terminator means data is not encoded word;
           * be strict like RFC and user agents, and do not decode:
           */
          s = srcEnd;
          goto srcToSNoDecode;
        }

      /* Get the charset: from opening `=?' to `?' or `*': */
      for (charsetEnd = charset=s; charsetEnd < encWordDataEnd; charsetEnd++)
        if (*charsetEnd == '?' || *charsetEnd == '*') break;

      /* Get the optional language, if `*' present after charset: */
      lang = langEnd = CHARPN;                  /* not present by default */
      s = charsetEnd;
      if (charsetEnd < encWordDataEnd)          /* charset was terminated */
        {
          s++;                                  /* skip `*'/`?' terminator */
          if (*charsetEnd == '*')               /* language follows */
            {
              lang = s;
              for (langEnd = lang; langEnd < encWordDataEnd; langEnd++)
                if (*langEnd == '?') break;
              s = langEnd;
              if (langEnd < encWordDataEnd) s++;
            }
        }

      /* Get the encoding: */
      for (encodingEnd = encoding = s;
           encodingEnd < encWordDataEnd;
           encodingEnd++)
        if (*encodingEnd == '?') break;
      s = encodingEnd;
      if (encodingEnd < encWordDataEnd) s++;    /* skip terminator */
      else goto encWordInvalid;                 /* encoding may be truncated*/

      /* Remainder of the encoded word is the encoded payload text: */
      encText = s;
      encTextEnd = encWordDataEnd;

      /* Identify the encoding: */
      decodeFunc = HTCHARSETFUNCPN;
      decodeFlags = (UTF)0;
      decodeFlags |= (flags & UTF_HTMLDECODE);  /* wtf pass on others too? */
      /* wtf UTF_HTMLDECODE not supported by htquotedprintable_to_iso88591()
       * nor htdecodebase64() anyway:
       */
      if (encodingEnd - encoding == 1)
        switch (*encoding)
          {
          case 'q':
          case 'Q':
            decodeFunc = htquotedprintable_to_iso88591;
            decodeFlags |= UTF_SAVEBOM;         /* Q-encoding instead */
            break;
          case 'b':
          case 'B':
            decodeFunc = htdecodebase64;
            break;
          }
      if (decodeFunc == HTCHARSETFUNCPN)        /* unknown encoding */
        goto encWordInvalid;
      /* WTF add UTF_... flag to copy `encText' as-is on error? */

      /* Decode `encText' to a temp buffer.  Even though all known
       * encodings (Q and B) have partial-buffer-capable translation
       * functions, the charset translation that comes after this
       * is not, so we need to decode the whole thing at once anyway:
       */
    decodeIt:
      decodeTotal = 0;
      decodeSrc = encText;
      decodeState = (UTF)0;
      decodeSz = decodeFunc(decodeBuf, decodeBufSz, &decodeTotal, &decodeSrc,
                            encTextEnd - encText,
                            (decodeFlags | UTF_BUFSTOP | UTF_START|UTF_FINAL),
                            &decodeState, 0, htpfobj, pmbuf);
      if (decodeSrc == encText && encTextEnd > encText)
        goto encWordInvalid;                    /* no progress at all */
      if (decodeSrc < encTextEnd)               /* partial progress */
        {                                       /* increase buf size */
          newSz = decodeBufSz + (decodeBufSz >> 1);
          if ((newBuf = (char *)realloc(decodeBufAlloced, newSz)) == CHARPN)
            {
#ifndef EPI_REALLOC_FAIL_SAFE
              decodeBufAlloced = CHARPN;
              decodeBufAllocedSz = 0;
#endif /* !EPI_REALLOC_FAIL_SAFE */
              TXputmsgOutOfMem(pmbuf, MERR + MAE, fn, newSz, 1);
              goto err;
            }
          decodeBuf = decodeBufAlloced = newBuf;
          decodeBufSz = decodeBufAllocedSz = newSz;
          goto decodeIt;
        }

      /* Identify the charset: */
      srcCharsetInfo = htstr2charset(charsetConfig, charset, charsetEnd);
      if (srcCharsetInfo != HTCSINFOPN)
        {
          srcCharset = srcCharsetInfo->tok;
          srcCharsetStr = CHARPN;
        }
      else                                      /* unknown charset */
        {
          srcCharset = HTCHARSET_UNKNOWN;
          /* Copy `charset' to a nul-terminated string: */
          newSz = (charsetEnd - charset) + 1;
          if (newSz <= sizeof(srcCharsetStrTmp))
            srcCharsetStr = srcCharsetStrTmp;
          else
            {
              srcCharsetStrAlloced = (char *)TXmalloc(pmbuf, fn, newSz);
              if (!srcCharsetStrAlloced) goto err;
              srcCharsetStr = srcCharsetStrAlloced;
            }
          newSz--;
          memcpy(srcCharsetStr, charset, newSz);
          srcCharsetStr[newSz] = '\0';
        }

      /* Translate the decoded data from `charset' to UTF-8: */
      utf8DataSz = TXtransbuf(htpfobj, srcCharset, srcCharsetStr,
                              HTCHARSETFUNCPN, transbufFlags, (UTF)0,
                              decodeBuf, decodeSz, &utf8Data);
      if (srcCharsetStrAlloced)                 /* free it once done */
        srcCharsetStrAlloced = TXfree(srcCharsetStrAlloced);
      srcCharsetStr = CHARPN;
      if (utf8Data == CHARPN)                   /* charset translate failed */
        {
        encWordInvalid:
          encWordIsValid = 0;
          /* fall through to copy leading linear white space, below */
        }

      /* Done translating encoded word; ready to output its translation.
       * But first, copy any leading linear white space that occurred
       * before it (separator), *unless* that was immediately after
       * a previous encoded-word (RFC 2047 6.2 paragraph 3):
       */
      if (!(*state & STATEFLAG_PREV_IS_ENC_WORD))
        {
          /* All CR LF space tab: no need for htutf8_to_utf8() to handle
           * UTF_XMLSAFE, UTF_BADENCASISO etc.?
           */
          OUTPUT_STR(src, encWordStart - src);
          src = encWordStart;                   /* Bug 7103 advance `src' */
        }
      /* Now copy the translated word, or TX_INVALID_CHAR: */
      if (encWordIsValid)                       /* output `utf8Data' */
        {
          if (flags & (UTF_XMLSAFE | UTF_ESCLOW | UTF_CRNL | UTF_LFNL))
            {
              /* Need htutf8_to_utf8() to handle above flags on
               * `utf8Data'.  Note that UTF_HTMLDECODE is not in the
               * above flags, so we do not double-decode encoded words
               * (decode word, then decode HTML):
               */
              subDestTot = 0;
              utf8DataPtr = utf8Data;
              newSz = htutf8_to_utf8(dest, DEST_LEFT_SZ, &subDestTot,
                                     &utf8DataPtr, utf8DataSz,
                                     /* Set UTF_FINAL: encoded word is
                                      * self-contained.  Remove
                                      * UTF_HTMLDECODE per above comment:
                                      */
                                     ((flags & ~UTF_HTMLDECODE) | UTF_FINAL),
                                     state, 0, htpfobj, pmbuf);
              if (newSz == (size_t)0 && (flags & UTF_BUFSTOP)) break;
              dest += newSz;
            }
          else
            {
              /* httransbuf() should have handled all of `flags';
               * can copy `utf8Data' as-is:
               */
              OUTPUT_STR(utf8Data, utf8DataSz);
            }
          *state |= STATEFLAG_PREV_IS_ENC_WORD;
        }
      else                                      /* output TX_INVALID_CHAR */
        {
          if (DEST_LEFT_SZ > 0) *dest = TX_INVALID_CHAR;
          else if (flags & UTF_BUFSTOP) break;
          dest++;
        }
      utf8Data = TXfree(utf8Data);              /* free when done */
      utf8DataSz = 0;
      src = encWordEnd;
    }

err:
  *sp = src;
  *dtot += (dest - d);
  decodeBufAlloced = TXfree(decodeBufAlloced);
  srcCharsetStrAlloced = TXfree(srcCharsetStrAlloced);
  utf8Data = TXfree(utf8Data);
  /* Restore `htpfobj' flags: */
  if (htpfobj != HTPFOBJPN)
    {
      htpfobj->do8bit = saveDo8bit;
      htpfobj->Utf8BadEncAsIso88591 = saveBadEncAsIso;
      htpfobj->Utf8BadEncAsIso88591Err = saveBadEncAsIsoErr;
      htpfobj->charsetmsgs = saveBadCharMsg;
    }
  return(dest - d);
#undef DEST_LEFT_SZ
#undef OUTPUT_STR
#undef STATEFLAG_PREV_IS_ENC_WORD
#undef MAX_ENCODED_WORD_INPUT_LEN
}

/* ------------------------------------------------------------------------- */
static size_t TXmakeEncodedWordSequence ARGS((char *d, size_t dlen,
      CONST char **sp, size_t slen, UTF flags, size_t maxWidth,
      HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
static size_t
TXmakeEncodedWordSequence(d, dlen, sp, slen, flags, maxWidth, htpfobj, pmbuf)
char            *d;             /* (out) output buffer */
size_t          dlen;           /* (in) size of `d' buffer */
CONST char      **sp;           /* (in) valid-UTF-8 text input */
size_t          slen;           /* (in) length of `*sp' */
UTF             flags;
size_t          maxWidth;       /* (in) max enc-word len (0 == no limit) */
HTPFOBJ           *htpfobj;         /* (in/out, opt.) HTOBJ */
TXPMBUF         *pmbuf;         /* (out, opt.) for messages */
/* Encodes `src' to `dest' as one or more RFC 2047 encoded words.
 * Advances `*sp' past consumed data.  Assumes `*sp' is already valid UTF-8.
 * NOTE: UTF_FINAL should be set in `flags', as caller should only
 * call this for a known atomic source-word (i.e. whitespace-separated
 * in the source).
 * Returns would-be length of `dest' used.
 */
{
  static CONST char     fn[] = "TXmakeEncodedWordSequence";
  static CONST char     qPreamble[] = "=?UTF-8?Q?";
  static CONST char     bPreamble[] = "=?UTF-8?B?"; /* must be same len */
  size_t                qLen, bLen, prevQLen, prevBLen, subDestTot;
  size_t                destLeftSz, outSz, curMaxWidth, encLen;
  CONST char            *src, *srcEnd, *srcWordEnd, *s, *e, *prevS;
  CONST char            *subSrcPtr, *srcOrg, *preamble;
  char                  *dest, *destEnd, *destTmp;
  TXUNICHAR             uniChar;
  HTCHARSETFUNC         *transFunc;
  UTF                   transFlags, subState;
  /* Would-be byte length of `s' to `e', encoded to base64: */
#define BASE64LEN(s, e) (((((e) - (s)) + 2) / 3)*4)
#define PREPOSTLEN      ((sizeof(qPreamble) - 1) + 2)

  dest = d;
  destEnd = d + dlen;
  src = srcOrg = *sp;
  srcEnd = src + slen;
  if (maxWidth <= (size_t)0) maxWidth = (size_t)EPI_OS_SIZE_T_MAX;
  if (!(flags & (UTF_CRNL | UTF_LFNL))) flags |= UTF_LFNL;

  while (src < srcEnd)                          /* input still available */
    {
      /* Put linear whitespace between encoded-words, per RFC and UTF_...: */
      if (src > srcOrg)
        {
          if (flags & UTF_CRNL)
            {
              if (dest < destEnd) *dest = '\015';
              else if (flags & UTF_BUFSTOP) break;
              dest++;
            }
          if (flags & UTF_LFNL)
            {
              if (dest < destEnd) *dest = '\012';
              else if (flags & UTF_BUFSTOP) break;
              dest++;
            }
          if (dest < destEnd) *dest = ' ';
          else if (flags & UTF_BUFSTOP) break;
          dest++;
        }

      /* PREPOSTLEN + 3*TX_MAX_UTF8_BYTE_LEN is the longest encoded
       * length needed for at least 1 atomic source character
       * (e.g. 4-byte UTF-8 sequence Q-encoded).  If `maxWidth' is
       * less than that, there is a chance we will not make forward
       * progress (consume some `src') on this pass, so perhaps
       * increase it.  We do this for every encoded word, because the
       * minimum size needed to make forward progress varies according
       * to the leading character: an ASCII char takes less space than
       * a 4-byte UTF-8 sequence:
       */
      curMaxWidth = maxWidth;
      if (curMaxWidth < PREPOSTLEN + TX_MAX_UTF8_BYTE_LEN*3)
        {
          /* Compute Q and base64 length of leading atomic UTF-8 char: */
          e = src;
          uniChar = TXunicodeDecodeUtf8Char(&e, srcEnd,
                                            (flags & UTF_BADENCASISO));
          if (uniChar == TXUNICHAR_SHORT_BUFFER) e = srcEnd;
          qLen = bLen = PREPOSTLEN;
          for (s = src; s < e; s++)
            {
              if (*s == ' ' || ISQPSAFE(*(byte *)s, 2))
                qLen++;                         /* safe as-is */
              else
                qLen += 3;                      /* hex escaped */
              bLen = PREPOSTLEN + BASE64LEN(src, e);
            }
          encLen = (qLen <= bLen || (flags & UTF_QENCONLY)) ? qLen : bLen;
          if (curMaxWidth < encLen) curMaxWidth = encLen;
        }

      /* Find the longest prefix of `src' that we can atomicly
       * transform into an encoded word of not more than `curMaxWidth'
       * bytes.  Use Q encoding or base64, whichever is shorter
       * (or required per `flags'):
       */
      qLen = bLen = prevQLen = prevBLen = PREPOSTLEN;
      for (s = prevS = src;
           s < srcEnd && (qLen <= curMaxWidth ||
                          (bLen <= curMaxWidth && !(flags & UTF_QENCONLY)));
           s = e)
        {
          prevS = s;
          prevQLen = qLen;
          prevBLen = bLen;
          /* Do not split UTF-8 characters mid-sequence: bad form, and
           * illegal per RFC 2047 as each encoded word must be legal
           * standalone:
           */
          if (*(byte *)s & 0x80)                /* potential UTF-8 char */
            {
              e = s;
              uniChar = TXunicodeDecodeUtf8Char(&e, srcEnd,
                                                (flags & UTF_BADENCASISO));
              if (uniChar == TXUNICHAR_SHORT_BUFFER)
                {
                  if (flags & UTF_FINAL) goto oneByte;
                  /* This should not happen; we were supposed to be
                   * called with UTF_FINAL (see comments at top).
                   */
                  break;
                }
              qLen += (e - s)*3;                /* Q would hex escape them */
              bLen = PREPOSTLEN + BASE64LEN(src, e);
            }
          else                                  /* 7-bit ASCII char */
            {
            oneByte:
              e = s + 1;
              if (*s == ' ' || ISQPSAFE(*(byte *)s, 2))
                qLen++;                         /* safe as-is */
              else
                qLen += 3;                      /* hex escaped */
              bLen = PREPOSTLEN + BASE64LEN(src, e);
            }
        }
      /* Choose Q or base64 encoding for this word: */
      if (qLen <= curMaxWidth &&                   /* Q encoding fits and */
          (qLen <= bLen || (flags & UTF_QENCONLY)))   /* is shorter than B */
        {
          srcWordEnd = s;
          transFunc = htiso88591_to_quotedprintable;
          /* UTF_SAVEBOM for Q encoding; UTF_START|UTF_FINAL because
           * the word is self-contained.  Turn off UTF_BUFSTOP for
           * true `qLen'/`bLen' comparision:
           */
          transFlags = ((flags & ~UTF_BUFSTOP) |
                        UTF_SAVEBOM | UTF_START | UTF_FINAL);
          preamble = qPreamble;
        }
      else if (bLen <= curMaxWidth && !(flags & UTF_QENCONLY))
        {
          srcWordEnd = s;
          transFunc = htencodebase64;
          transFlags = (flags | UTF_START | UTF_FINAL);
          preamble = bPreamble;
        }
      else if (prevQLen <= curMaxWidth &&
               (prevQLen <= prevBLen || (flags & UTF_QENCONLY)))
        {
          srcWordEnd = prevS;
          transFunc = htiso88591_to_quotedprintable;
          transFlags = ((flags & ~UTF_BUFSTOP) |
                        UTF_SAVEBOM | UTF_START | UTF_FINAL);
          preamble = qPreamble;
        }
      else      /* prevBLen <= curMaxWidth && !(flags & UTF_QENCONLY) */
        {
          srcWordEnd = prevS;
          transFunc = htencodebase64;
          transFlags = (flags | UTF_START | UTF_FINAL);
          preamble = bPreamble;
        }
      /* Sanity: if no forward progress (`curMaxWidth' too small), yap.
       * Should not happen, due to `curMaxWidth' increment above:
       */
      if (srcWordEnd <= src && src < srcEnd)
        {
          txpmbuf_putmsg(pmbuf, MERR, fn, "Width too small");
          srcWordEnd = src + 1;                 /* just force it */
        }

      /* Now encode `src'-to-`srcWordEnd', to `dest': */
      /* Need to advance `src' and `dest' together atomically if
       * UTF_BUFSTOP, and an encoded word is at atom, so first alias
       * `dest' to `destTmp':
       */
      destTmp = dest;
      /* First output the preamble: */
      for (s = preamble; *s != '\0'; s++)
        {
          if (destTmp < destEnd) *destTmp = *s;
          else if (flags & UTF_BUFSTOP) break;
          destTmp++;
        }
      /* Then the encoded word data: */
      subDestTot = 0;
      subState = (UTF)0;
      subSrcPtr = src;
      destLeftSz = (destTmp <= destEnd ? destEnd - destTmp : 0);
      outSz = transFunc(destTmp, destLeftSz, &subDestTot, &subSrcPtr,
                        srcWordEnd - subSrcPtr, transFlags, &subState,
                        0, htpfobj, pmbuf);
      if (subSrcPtr < srcWordEnd && (flags & UTF_BUFSTOP))
        break;                                  /* not all consumed */
      destTmp += outSz;
      /* Finally the `?=' suffix: */
      if (destTmp < destEnd) *destTmp = '?';
      else if (flags & UTF_BUFSTOP) break;
      destTmp++;
      if (destTmp < destEnd) *destTmp = '=';
      else if (flags & UTF_BUFSTOP) break;
      destTmp++;

      /* Now we can atomically increment `src' and `dest': */
      dest = destTmp;
      src = srcWordEnd;                         /* should be == subSrcPtr? */
    }

  *sp = src;
  return(dest - d);
#undef BASE64LEN
#undef PREPOSTLEN
}

size_t
TXutf8ToEncodedWord(d, dlen, dtot, sp, slen, flags, state, width, htpfobj,
                    pmbuf)
char            *d;     /* destination buffer */
size_t          dlen;   /*     ""        "" length (-1 == strlen(d)) */
size_t          *dtot;  /* accumulating destination payload count */
CONST char      **sp;   /* *sp is source string */
size_t          slen;   /*    ""    ""   length (-1 == strlen(*sp)) */
UTF             flags;
UTF             *state;
int             width;  /* max bytes per encoded-word */
HTPFOBJ         *htpfobj; /* (in/out, opt.) (ignored) */
TXPMBUF         *pmbuf;
/* Translates UTF-8 string `*sp' to RFC 2047 style encoded-word buffer `d',
 * which should not be the same pointer.
 * Returns would-be length of `d'; if > `dlen', not written past.
 * `d' is not nul-terminated, and may be truncated mid-char if short
 * unless UTF_BUFSTOP set.  Advances `*sp' past decoded source.
 * If `htpfobj' is NULL, only internal, factory-default charsets recognized.
 * UTF_ONECHAR        Ignored
 * UTF_ESCRANGE       Ignored (UTF-8 output has enough range)
 * UTF_ESCLOW         HTML-escape low (7-bit) chars < > " &
 * UTF_BUFSTOP        Stop decoding integrally when out of `d' buffer space
 * UTF_FINAL          Must be set if this is the last of multiple buffers
 * UTF_BINARY         Binary data: hex-escape CRLF too
 * UTF_BIGENDIAN      Ignored (UTF-16)
 * UTF_LITTLEENDIAN   Ignored (UTF-16)
 * UTF_SAVEBOM        Ignored (UTF-16)
 * UTF_START          This is the first buffer (required for 1st buffer)
 * UTF_CRNL           Map CR/LF/CRLF newlines to CR
 * UTF_LFNL           Map CR/LF/CRLF newlines to LF
 * UTF_DO8BIT         Ignored (ISO-8859-1 -> ISO-8859-1)
 * UTF_HTMLDECODE  Ignored (wtf only since we htutf8_to_utf8 1 char at a time)
 * UTF_BADCHARMSG     Report bad/out-of-range character sequences
 * UTF_XMLSAFE        Output XML-safe chars only
 * UTF_BADENCASISO    Interpret bad UTF-8 sequence as ISO-8859-1
 * UTF_BADENCASISOERR Issue msg if "" and UTF_BADCHARMSG
 * UTF_QENCONLY       Use Q encoding only (no base64)
 * UTF_NOCHARTRANSLATE WTF need to implement in this function
 */
{
  static CONST char     fn[] = "TXutf8ToEncodedWord";
  static CONST char     linearWhiteSpaceNewlines[] = " \t\r\n";
  static const char     linearWhiteSpaceNoNewlines[] = " \t";
  const char            *linearWhiteSpace = linearWhiteSpaceNewlines;
  CONST char            *src, *srcOrg, *s, *okTextEnd, *okTextLwspEnd;
  CONST char            *srcEnd, *subSrcPtr, *wordEnd;
  char                  *dest, *destEnd;
  UTF                   subState;
  size_t                subDestTot, newSz, destLeftSz;
  char                  utf8ValidBufTmp[512], *utf8ValidBuf = utf8ValidBufTmp;
  char                  *utf8ValidBufAlloced = CHARPN, *newPtr;
  size_t                utf8ValidBufSz = sizeof(utf8ValidBufTmp);
  size_t                utf8ValidBufAllocedSz = 0, utf8ValidBufLen;
#define IS_PRINTABLE_ASCII(ch)  ((ch) >= '!' && (ch) <= '~') /* per RFC 822 */
#define IS_LWSP_CHAR(ch)        \
  ((ch) != '\0' && strchr(linearWhiteSpace, (ch)) != CHARPN)
#define IS_OK_TEXT_CHAR(ch)     (IS_PRINTABLE_ASCII(ch) || IS_LWSP_CHAR(ch))
  /* STATEFLAG_PREV_IS_LWSP is set if `src[-1]' is/was linear white space: */
#define STATEFLAG_PREV_IS_LWSP  UTF_ONECHAR

  /* If UTF_BINARY, CR/LF are not considered linear whitespace nor text,
   * so that they are encoded:
   */
  linearWhiteSpace = ((flags & UTF_BINARY) ? linearWhiteSpaceNoNewlines :
                      linearWhiteSpaceNewlines);
  if (width < 0) width = 0;                     /* sanity */
  src = srcOrg = (*sp);
  dest = d;
  if (slen == (size_t)(-1)) slen = strlen(src);
  if (dlen == (size_t)(-1)) dlen = (dest == src ? slen : strlen(d));
  srcEnd = src + slen;
  destEnd = dest + dlen;
  if (flags & UTF_START) *state = (UTF)0;

  while (src < srcEnd)
    {
      /*  v-- src                     v-- srcEnd
       * [  texttext  text%C3%A4text  ]
       *              ^   ^-- okTextEnd
       *              +-- okTextLwspEnd
       */
      /* Find initial length of string that does not need encoding: */
      for (s = src; s < srcEnd && IS_OK_TEXT_CHAR(*s); s++);
      okTextEnd = s;

      /* Back off to end of whitespace: since encoded words need
       * to be separated by whitespace, we will need to encode
       * the entire whitespace-delimited word containing `okTextEnd',
       * not just the chars immediately at it:
       */
      for (s = okTextEnd; s > src && !IS_LWSP_CHAR(s[-1]); s--);
      okTextLwspEnd = s;

      /* We can flush at least from `src' to `okTextLwspEnd' with no
       * encoding.  If *nothing* needs encoding (`okTextEnd == srcEnd'),
       * and this is the final input, we can flush to `okTextEnd' too.
       * (If this is not the final input, the next call may add
       * need-encoding characters to the word that `srcEnd' may be in
       * the middle of.):
       */
      s = okTextLwspEnd;
      if (okTextEnd == srcEnd &&                /* nothing needs encoding & */
          (flags & UTF_FINAL))                  /*   final input */
        s = okTextEnd;                          /* then flush more */
      /* Flush `src' to `s' without encoding.  Need htutf8_to_utf8()
       * to handle some `flags':
       */
      subDestTot = 0;
      subState = (UTF)0;
      subSrcPtr = src;
      destLeftSz = (dest <= destEnd ? destEnd - dest : 0);
      newSz = htutf8_to_utf8(dest, destLeftSz, &subDestTot, &subSrcPtr,
                             s - src, flags, &subState, 0, htpfobj, pmbuf);
      dest += newSz;
      src = subSrcPtr;
      if (okTextLwspEnd > src && subSrcPtr == okTextLwspEnd)
        *state |= STATEFLAG_PREV_IS_LWSP;
      if (subSrcPtr < s && (flags & UTF_BUFSTOP)) break;
      if (dest >= destEnd && (flags & UTF_BUFSTOP)) break;
      if (src >= srcEnd) break;                 /* done with input */

      /* If nothing (yet) needs encoding, must wait for more input
       * to find word end and/or if later part of word needs encoding:
       */
      if (okTextEnd >= srcEnd) break;

      /*              v-- src         v-- srcEnd
       * [  texttext  text%C3%A4text  ]
       *              ^   ^         ^-- wordEnd
       *              |   +-- okTextEnd
       *              +-- okTextLwspEnd
       */
      /* `src' now points to a word that definitely needs encoding.
       * Try to find the end of the word:
       */
      for (s = okTextEnd; s < srcEnd && !IS_LWSP_CHAR(*s); s++);
      wordEnd = s;
      /* If we have not found LWSP after the word, we have not found
       * the end of the word; wait for more input:
       */
      if (wordEnd >= srcEnd && !(flags & UTF_FINAL)) break;

      /* Now we have the source word to encode.  First validate UTF-8
       * sequences in it, because htiso88591_to_quotedprintable() and
       * htencodebase64() do not.  Copy to `utf8ValidBuf':
       */
    validateUtf8:
      subDestTot = 0;
      subState = (UTF)0;
      subSrcPtr = src;
      newSz = htutf8_to_utf8(utf8ValidBuf, utf8ValidBufSz, &subDestTot,
                             &subSrcPtr, wordEnd - src,
                             /* Add UTF_START|UTF_FINAL because the word
                              * is self-contained.  Turn off UTF_BUFSTOP
                              * because we want to know full size needed:
                              * Turn off UTF_ESCLOW because `<>"&' will be
                              * encoded by base64 or Q encoding:
                              */
                             ((flags & ~(UTF_BUFSTOP | UTF_ESCLOW)) |
                              UTF_START | UTF_FINAL),
                             &subState, 0, htpfobj, pmbuf);
      if (newSz >= utf8ValidBufSz)              /* need to re-alloc */
        {
          newSz++;                              /* for nul */
          utf8ValidBufAllocedSz += (utf8ValidBufAllocedSz >> 1);
          if (utf8ValidBufAllocedSz < newSz) utf8ValidBufAllocedSz = newSz;
          newPtr = (char*)realloc(utf8ValidBufAlloced, utf8ValidBufAllocedSz);
          if (newPtr == CHARPN)                 /* realloc() failed */
            {
#ifndef EPI_REALLOC_FAIL_SAFE
              utf8ValidBufAlloced = CHARPN;
#endif /* EPI_REALLOC_FAIL_SAFE */
              TXputmsgOutOfMem(pmbuf, MERR+MAE, fn, utf8ValidBufAllocedSz, 1);
              goto err;
            }
          utf8ValidBuf = utf8ValidBufAlloced = newPtr;
          utf8ValidBufSz = utf8ValidBufAllocedSz;
          goto validateUtf8;
        }
      utf8ValidBufLen = newSz;

      /* Now we can encode the source word, with preamble and suffix.
       * We might need more than one *encoded* word, to keep each less
       * than or equal to `width' (e.g. 75 per RFC 2047); let
       * TXmakeEncodedWordSequence() handle that:
       */
      subDestTot = 0;
      subState = (UTF)0;
      subSrcPtr = utf8ValidBuf;
      destLeftSz = (dest <= destEnd ? destEnd - dest : 0);
      newSz = TXmakeEncodedWordSequence(dest, destLeftSz, &subSrcPtr,
                                        utf8ValidBufLen,
                                        /* Add UTF_START | UTF_FINAL
                                         * because the word is
                                         * self-contained:
                                         */
                                        (flags | UTF_START | UTF_FINAL),
                                        width, htpfobj, pmbuf);
      if (subSrcPtr < utf8ValidBuf + utf8ValidBufLen && /* not all consumed */
          (flags & UTF_BUFSTOP))
        break;
      if (dest + newSz > destEnd && (flags & UTF_BUFSTOP)) break;
      dest += newSz;
      src = wordEnd;
      *state &= ~STATEFLAG_PREV_IS_LWSP;
    }

err:
  *sp = src;
  *dtot += (dest - d);
  utf8ValidBufAlloced = TXfree(utf8ValidBufAlloced);
  return(dest - d);
#undef IS_PRINTABLE_ASCII
#undef IS_LWSP_CHAR
#undef IS_OK_TEXT_CHAR
#undef STATEFLAG_PREV_IS_LWSP
}

size_t
TXtransbuf(HTPFOBJ *obj, HTCHARSET charset, const char *charsetbuf, HTCHARSETFUNC *func, int flags, UTF utfExtra, const char *buf, size_t sz, char **ret)
/* Translates `buf' of size `sz' to/from `charset'/UTF-8 using `flags'.
 * Sets `*ret' to malloc'd return buffer, and returns buffer size.
 * If `obj' is NULL, or its charsetconverter is `%NONE%', will only use
 * internal routines (no exec).
 * Flags:
 *   bit 0: to UTF-8
 *   bit 1: zap end space

 HTPFOBJ         *obj;           (in/out, opt.) HTOBJ
 HTCHARSET       charset;        charset
 CONST char      *charsetbuf;    "" if `charset' is HTCHARSET_UNKNOWN
 HTCHARSETFUNC   *func;          optional function to use instead
 int             flags;
 UTF             utfExtra;       (opt.) non-HTOBJ UTF flags
 CONST char      *buf;           buffer (nul-term. for htbuf_setdata)
 size_t          sz;             length of `buf' (-1 for strlen)
 char            **ret;          (out) translated text

 */
{
  static CONST char     fn[] = "httransbuf";
  static CONST char     safe[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-.:_";
  static CONST char     deftrans[] = "\"%INSTALLDIR%" PATH_SEP_S "etc"
    PATH_SEP_S "iconv\" -f %CHARSETFROM% -t %CHARSETTO% -c";
  static CONST char     cantconvmsg[] =
    "Cannot %sconvert charset %s to %s via converter `%s': %s";
  size_t                dtot, a, esz;
  UTF                   state, uflags = (UTF)0;
  char                  *cmdline = CHARPN, **argv = CHARPPN, *edata;
  char                  *acmdline = CHARPN;
  CONST char            *sp, *ep;
  CONST byte            *s, *e;
  CONST HTCHARSETINFO   *csi;
  HTCHARSETFUNC         *transfunc = HTCHARSETFUNCPN;
  HTBUF                 *tmpbuf = HTBUFPN;
  int                   i, gotsig, unichar, l, r, ch, gotall = 0, gotNone = 0;
  CONST char            *vars[4], *vals[4];
  char                  *d, *dsav;
#ifndef _WIN32
  char                  *dupargv0 = CHARPN;
#endif /* !_WIN32 */
  TXPOPENARGS           po;
  TXPIPEARGS            pa;
  time_t                timeout, deadline;
  int                   numVars;
  TXPMBUF               *pmbuf = TXPMBUFPN;
  char                  tmp[1024];

  TXPOPENARGS_INIT(&po);
  TXPIPEARGS_INIT(&pa);
  if (sz == (size_t)(-1)) sz = strlen((char *)buf);
  if (charsetbuf == CHARPN) charsetbuf = htcharset2str(charset);
  if (obj)
  {
    pmbuf = htpfgetpmbuf(obj);
    if (!obj->charsetmsgs)
      pmbuf = TXPMBUF_SUPPRESS;
  }

  /* KNG 20100923 Bug 3303: do not exec iconv if nothing to convert;
   * will hang and timeout because we will not write anything to
   * iconv, yet we will wait for its output:
   */
  if (sz <= (size_t)0)                          /* nothing to do */
    {
      *ret = CHARPN;
      a = 0;
      goto retbuf;
    }

  /* Set up var replacement for command line: */
  i = ((flags & 1) ? 1 : 0);
  vars[0] = "CHARSETFROM";
  vars[1] = "CHARSETTO";
  vals[i] = "UTF-8";
  vals[1 - i] = charsetbuf;
  numVars = 2;
  vars[numVars] = vals[numVars] = CHARPN;
  cmdline = TXgetcharsetconv();
  if (cmdline == CHARPN)                        /* use default */
  {
    cmdline = (char *)deftrans;
    vars[numVars] = "INSTALLDIR";             /* not replaced yet */
    vals[numVars++] = TXINSTALLPATH_VAL;
    vars[numVars] = vals[numVars] = CHARPN;
  }

  /* Check for %ALL%, %NONE% in charset converter config: */
  d = cmdline + strspn(cmdline, TXWhitespace);
  if (strncmp(d, "%ALL%", 5) == 0 && d[5] != '%')
    {
      gotall = (charset != HTCHARSET_UTF_8);
      cmdline = d + 5;
      cmdline += strspn(cmdline, TXWhitespace);
    }
  if (strncmp(d, "%NONE%", 6) == 0 && d[6] != '%')
    {
      gotNone = 1;
      cmdline = d + 6;
      cmdline += strspn(cmdline, TXWhitespace);
    }

  csi = HtCharsetInfo + charset;
  if (charset == HTCHARSET_UNKNOWN ||
      csi->cs_to_utf8 == HTCHARSETFUNCPN ||
      gotall)
    {                                           /* no transfunc */
      if (!gotall &&
          csi->unicodemap != EPI_UINT16PN &&
          csi->unicodeindex != BYTEPN)
        {                                       /* use monobyte mapping */
          if (flags & 1)                        /* charset to UTF-8 */
            {
              for (s = (CONST byte *)buf, e = s + sz, d = tmp; s < e; s++)
                {
                  unichar = (int)csi->unicodemap[*s];
                  if (unichar == 0xFFFF)        /* no mapping */
                    *(d++) = TX_INVALID_CHAR;
                  else if (unichar < 0x80)      /* inline optimization */
                    *(d++) = (char)unichar;
                  else
                    {
                      d = TXunicodeEncodeUtf8Char(dsav = d, tmp + sizeof(tmp),
                                                  unichar);
                      if (d == CHARPN)          /* out of range */
                        {
                          *dsav = TX_INVALID_CHAR;
                          d = dsav + 1;
                        }
                    }
                  if (d >= tmp + sizeof(tmp) - 6)
                    {                           /* flush tmp to tmpbuf */
                      if (tmpbuf == HTBUFPN &&
                          (tmpbuf = openhtbuf()) == HTBUFPN)
                        goto nomem;
                      if (!htbuf_write(tmpbuf, tmp, (size_t)(d - tmp)))
                        goto nomem;
                      d = tmp;
                    }
                }
            }
          else                                  /* UTF-8 to charset */
            {
              for (sp = buf, ep = sp + sz, d = tmp; sp < ep; )
                {
                  if (!(*(CONST byte *)sp & 0x80))  /* inline optimization */
                    unichar = (int)(*((CONST byte *)(sp++)));
                  else
                    {
                      unichar = TXunicodeDecodeUtf8Char(&sp, ep, 0);
                      if (unichar == -2) sp = ep;   /* short buf */
                      if (unichar < 0)          /* illegal sequence */
                        unichar = TX_INVALID_CHAR;
                    }
                  l = i = 0;
                  r = csi->numunicodeindex;
                  while (l < r)                 /* binary search */
                    {
                      i = ((l + r) >> 1);
                      ch = (int)csi->unicodemap[csi->unicodeindex[i]];
                      if (ch == 0xFFFF) ch = -1;
                      if (unichar < ch) r = i;
                      else if (unichar > ch) l = i + 1;
                      else break;               /* found it */
                    }
                  if (l < r) *(d++) = (char)csi->unicodeindex[i];
                  else *(d++) = TX_INVALID_CHAR;/* didn't find it */
                  if (d >= tmp + sizeof(tmp) - 6)
                    {                           /* flush tmp to tmpbuf */
                      if (tmpbuf == HTBUFPN &&
                          (tmpbuf = openhtbuf()) == HTBUFPN)
                        goto nomem;
                      if (!htbuf_write(tmpbuf, tmp, (size_t)(d - tmp)))
                        goto nomem;
                      d = tmp;
                    }
                }
            }
          if (tmpbuf == HTBUFPN)                /* never flushed */
            {
              a = (size_t)(d - tmp);
              goto duptmp;
            }
          if (d > tmp && !htbuf_write(tmpbuf, tmp, (size_t)(d - tmp)))
            goto nomem;
          a = htbuf_getdata(tmpbuf, ret, 0x3);
          goto retbuf;
        }
      else                                      /* exec iconv */
        {
          /* Make sure charset parameter is safe for command line, e.g. DOS: */
          if (charsetbuf[strspn(charsetbuf, safe)] != '\0' ||
              !*charsetbuf)
            {
              txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "Invalid charset `%s'",
                             charsetbuf);
              goto err;
            }
          if (obj == HTPFOBJPN || gotNone)        /* no exec if no `obj' */
            {
              txpmbuf_putmsg(pmbuf, MERR + EXE, fn,
                             "Cannot convert charset %s to %s: External converter is %%NONE%% and charset not supported internally",
                             vals[0], vals[1]);
              goto err;
            }
          cmdline = acmdline = tx_replacevars(pmbuf, cmdline, 1, vars,
                                              (size_t)numVars, vals, INTPN);
          if (cmdline == CHARPN)
            {
            nomem:
              goto err;
            }
          if ((argv = tx_dos2cargv(cmdline, 1)) == CHARPPN) goto nomem;
#ifdef _WIN32
          /* TXpopenduplex() does its own path search in Windows?
           * and argv[0] may not contain `.exe':
           */
          po.cmd = argv[0];
#else /* !_WIN32 */
          dupargv0 = epipathfindmode(argv[0], getenv("PATH"), (X_OK | 0x8));
          po.cmd = dupargv0;
          if (po.cmd == CHARPN) goto err1;
#endif /* !_WIN32 */
          po.argv = argv;
          po.flags = (TXPDF_QUOTEARGS | TXPDF_REAP | TXPDF_SAVE);
          po.desc = "charset converter process";
          po.fh[STDIN_FILENO] = TXFHANDLE_CREATE_PIPE_VALUE; /*stdin from us*/
          po.fh[STDOUT_FILENO] = TXFHANDLE_CREATE_PIPE_VALUE;/*stdout to us */
          po.fh[STDERR_FILENO] = TXFHANDLE_CREATE_PIPE_VALUE;/*stderr to us */
          po.pmbuf = pmbuf;
          if (!TXpopenduplex(&po, &pa))
            {
#ifndef _WIN32
            err1:
#endif /* !_WIN32 */
              txpmbuf_putmsg(pmbuf, MERR + EXE, fn, cantconvmsg,
                             "", vals[0], vals[1], argv[0],
                             TXstrerror(TXgeterror()));
              goto err;
            }
          htbuf_setdata(pa.pipe[STDIN_FILENO].buf, (char *)buf, sz, sz+1, 0);
          deadline = (time_t)(-1);
          /* Bug 3303: if nothing to send to stdin, close it before
           * attempting to read stdout; avoids deadlock:
           */
          if (TXFHANDLE_IS_VALID(pa.pipe[STDIN_FILENO].fh) &&
              htbuf_getsendsz(pa.pipe[STDIN_FILENO].buf) <= 0)
            TXpendio(&pa, 0);                   /* send EOF to child stdin */
          do
            {
              if (deadline == (time_t)(-1))
                timeout = (time_t)(-1);
              else if ((timeout = deadline - time(TIME_TPN)) <= 0)
                {
                  strcpy(tmp, "Timeout");
                  goto cantconv;
                }
              if (!TXpreadwrite(&pa, (int)timeout)) break;
              if (TXFHANDLE_IS_VALID(pa.pipe[STDIN_FILENO].fh))
                TXpendio(&pa, 0);               /* send EOF to child stdin */
            }
          while (TXFHANDLE_IS_VALID(pa.pipe[STDOUT_FILENO].fh) ||
                 TXFHANDLE_IS_VALID(pa.pipe[STDERR_FILENO].fh));
          TXpendio(&pa, 1);                     /* send EOF to stdin */
          i = gotsig = 0;
          TXpgetexitcode(&pa, 0, &i, &gotsig);  /* no yap: may have exited */
          TXpcloseduplex(&pa, 0);
          TXcleanproc();
          if ((esz = htbuf_getdata(pa.pipe[STDERR_FILENO].buf, &edata,0)) > 0)
            {                                   /* report stderr */
              while (esz > 0 && strchr(TXWhitespace, edata[esz - 1]) != CHARPN)
                esz--;
              while (esz > 0 && strchr(TXWhitespace, *edata) != CHARPN)
                (edata++, esz--);
              txpmbuf_putmsg(pmbuf, MWARN + EXE, fn,
                    "Charset converter `%s' stderr converting %s to %s: %.*s",
                             cmdline, vals[0], vals[1], (int)esz, edata);
            }
          if (i)                                /* non-zero exit/signal */
            {
              htsnpf(tmp, sizeof(tmp), "%s %d",
                     (gotsig ? "received signal" : "returned exit code"), i);
            cantconv:
              i = (pa.pipe[STDOUT_FILENO].buf == HTBUFPN ||
                   htbuf_getdatasz(pa.pipe[STDOUT_FILENO].buf) == 0);
              txpmbuf_putmsg(pmbuf, MERR + EXE, fn, cantconvmsg,
                             (i ? "" : "completely "), vals[0], vals[1],
                             cmdline, tmp);
              if (i) goto err;
            }
          a = htbuf_getdata(pa.pipe[STDOUT_FILENO].buf, ret, 0x3);
        retbuf:
          if (*ret == CHARPN &&
              (*ret = strdup("")) == CHARPN)
            goto merr;
        }
    }
  else                                          /* use internal routines */
    {
      if (flags & 1)                            /* charset to UTF-8 */
        {
          uflags = csi->cs_to_utf8flags;
          transfunc = (func != HTCHARSETFUNCPN ? func : csi->cs_to_utf8);
        }
      else                                      /* UTF-8 to charset */
        {
          uflags = csi->utf8_to_csflags;
          transfunc = (func != HTCHARSETFUNCPN ? func : csi->utf8_to_cs);
        }
      uflags |= (UTF_START | UTF_FINAL);
      if (obj != HTPFOBJPN)
        {
          if (obj->do8bit)
            uflags |= UTF_DO8BIT;
          if (obj->charsetmsgs)
            uflags |= UTF_BADCHARMSG;
          if (obj->Utf8BadEncAsIso88591)
            uflags |= UTF_BADENCASISO;
          if (obj->Utf8BadEncAsIso88591Err)
            uflags |= UTF_BADENCASISOERR;
        }
      else
        {
          /* Derived from default flags in openhtobj(): */
          uflags |= (UTF_DO8BIT | UTF_BADCHARMSG | UTF_BADENCASISO |
                     UTF_BADENCASISOERR);
        }
      uflags |= utfExtra;
      dtot = 0;
      state = (UTF)0;
      sp = buf;
      a = transfunc(tmp, sizeof(tmp), &dtot, &sp, sz, uflags, &state, 0, obj,
                    pmbuf);
      if (state & UTF_BADCHARMSG)               /* (partial?) problem */
        {
          if (obj != HTPFOBJPN)
            goto err;
        }
    duptmp:
      if ((*ret = (char *)malloc(a + 1)) == CHARPN)
        {
        merr:
          TXputmsgOutOfMem(pmbuf, MERR + MAE, fn, a + 1, 1);
          goto err;
        }
      if (a <= sizeof(tmp))
        memcpy(*ret, tmp, a);
      else
        {
          dtot = 0;
          state = (UTF)0;
          sp = buf;
          a = transfunc(*ret, a, &dtot, &sp, sz, uflags, &state, 0,
                        obj, pmbuf);
        }
      (*ret)[a] = '\0';
    }
  if (flags & 2)
    hturlzapendspace(*ret);
  goto done;

err:
  a = 0;
  *ret = CHARPN;
done:
  if (pa.pid != (PID_T)0)
    {
      TXpkill(&pa, 1);
      TXpcloseduplex(&pa, 3);
    }
  else
    TXpcloseduplex(&pa, 1);                     /* buffers could be open */
  if (acmdline != CHARPN) free(acmdline);
  if (argv != CHARPPN) freenlst(argv);
#ifndef _WIN32
  if (dupargv0 != CHARPN) free(dupargv0);
#endif /* !_WIN32 */
  if (tmpbuf != HTBUFPN) closehtbuf(tmpbuf);
  return(a);
}
