/*
 *  at [NOW] PLUS NUMBER SECONDS|MINUTES|HOURS|DAYS|WEEKS
 *     /NUMBER [DOT NUMBER] [AM|PM]\ /[MONTH NUMBER [NUMBER]]             \
 *     |NOON                       | |[TOMORROW]                          |
 *     |MIDNIGHT                   | |NUMBER [SLASH NUMBER [SLASH NUMBER]]|
 *     \TEATIME                    / \PLUS NUMBER SECONDS|MINUTES|HOURS|DAYS|WEEKS/
 *     YYYY/MM/DD [HH:MM[:SS]]
 */

/* System Headers */

#include "txcoreconfig.h"
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef unix
#include <unistd.h>
#endif
#include <ctype.h>
#ifdef EPI_HAVE_GNU_READLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif /* EPI_HAVE_GNU_READLINE */

/* Local headers */
#include "os.h"
#include "sizes.h"
#define MMSG_C
#include "mmsg.h"
#include "parsetim.h"
#if defined(TEST) && defined(EPI_HAVE_STDARG)
#  include <stdarg.h>
#endif
#include "txtypes.h"
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"

static int	TxParsetimeMesg = 1;		/* putmsg on error */
static int	TxParsetimeStrict = 1;		/* strict parsing */
static int	TxParsetimeRFC1123Only = 0;	/* RFC1123 date syntax only */
static TXPMBUF  *TxParsetimePmbuf = TXPMBUFPN;  /* putmsg buffer */


#define panic(a) if(TxParsetimeMesg)    \
  txpmbuf_putmsg(TxParsetimePmbuf, MERR + MAE, __FUNCTION__, a)

/**********************************************************************/

/* Structures and unions */

/* JAN - DEC, SUN - SAT must be in consecutive ascending order: */
/* I(tok, gmtOff, tmzDesc) */
#define CORE_LIST       \
I(TEOF,         0, "")  \
I(ID,           0, "")  \
I(JUNK,         0, "")  \
I(MIDNIGHT,     0, "")  \
I(NOON,         0, "")  \
I(TEATIME,      0, "")  \
I(PM,           0, "")  \
I(AM,           0, "")  \
I(TOMORROW,     0, "")  \
I(YESTERDAY,    0, "")  \
I(TODAY,        0, "")  \
I(NOW,          0, "")  \
I(SECONDS,      0, "")  \
I(MINUTES,      0, "")  \
I(HOURS,        0, "")  \
I(DAYS,         0, "")  \
I(WEEKS,        0, "")  \
I(MONTHS,       0, "")  \
I(YEARS,        0, "")  \
I(SYM_NUMBER,   0, "")  \
I(PLUS,         0, "")  \
I(SYM_THIS,     0, "")  \
I(MINUS,        0, "")  \
I(DOT,          0, "")  \
I(COLON,        0, "")  \
I(SLASH,        0, "")  \
I(COMMA,        0, "")  \
I(JAN,          0, "")  \
I(FEB,          0, "")  \
I(MAR,          0, "")  \
I(APR,          0, "")  \
I(MAY,          0, "")  \
I(JUN,          0, "")  \
I(JUL,          0, "")  \
I(AUG,          0, "")  \
I(SEP,          0, "")  \
I(OCT,          0, "")  \
I(NOV,          0, "")  \
I(DEC,          0, "")  \
I(SUN,          0, "")  \
I(MON,          0, "")  \
I(TUE,          0, "")  \
I(WED,          0, "")  \
I(THU,          0, "")  \
I(FRI,          0, "")  \
I(SAT,          0, "")  \
I(SYM_NULL,     0, "")  \
I(ISOTIME,      0, "")  \
I(SYM_LPAREN,   0, "")  \
I(SYM_RPAREN,   0, "")  \
I(SYM_FILETIME, 0, "")

/* These must be in ascending order by `tok': */
/* I(tok, gmtOff, tmzDesc) */
#define TIMEZONE_LIST   \
I(ACDT,         10*3600+30*60,  "Australian Central Daylight Time") \
I(ACST,         9*3600+30*60,   "Australian Central Standard Time") \
I(ACT,          8*3600,         "ASEAN Common Time") \
I(ADT,          -3*3600,        "Atlantic Daylight Time") \
I(AEDT,         11*3600,        "Australian Eastern Daylight Time") \
I(AEST,         10*3600,        "Australian Eastern Standard Time") \
I(AFT,          4*3600+30*60,   "Afghanistan Time") \
I(AKDT,         -8*3600,        "Alaska Daylight Time") \
I(AKST,         -9*3600,        "Alaska Standard Time") \
I(AMST,         5*3600,         "Armenia Summer Time") \
I(AMT,          4*3600,         "Armenia Time") \
I(ART,          -3*3600,        "Argentina Time") \
I(AST,          -4*3600,        "Atlantic Standard Time") \
I(AWDT,         9*3600,         "Australian Western Daylight Time") \
I(AWST,         8*3600,         "Australian Western Standard Time") \
I(AZOST,        -1*3600,        "Azores Standard Time") \
I(AZT,          4*3600,         "Azerbaijan Time") \
I(BDT,          8*3600,         "Brunei Time") \
I(BIOT,         6*3600,         "British Indian Ocean Time") \
I(BIT,          -12*3600,       "Baker Island Time") \
I(BOT,          -4*3600,        "Bolivia Time") \
I(BRT,          -3*3600,        "Brasilia Time") \
I(BST,          1*3600,         "British Summer Time") \
I(BTT,          6*3600,         "Bhutan Time") \
I(CAT,          2*3600,         "Central Africa Time") \
I(CCT,          6*3600+30*60,   "Cocos Islands Time") \
I(CDT,          -5*3600,        "Central Daylight Time") \
I(CEDT,         2*3600,         "Central European Daylight Time") \
I(CEST,         2*3600,         "Central European Summer Time") \
I(CET,          1*3600,         "Central European Time") \
I(CHAST,        12*3600+45*60,  "Chatham Standard Time") \
I(ChST,         10*3600,        "Chamorro Standard Time") \
I(CIST,         -8*3600,        "Clipperton Island Standard Time") \
I(CKT,          -10*3600,       "Cook Island Time") \
I(CLST,         -3*3600,        "Chile Summer Time") \
I(CLT,          -4*3600,        "Chile Standard Time") \
I(COST,         -4*3600,        "Colombia Summer Time") \
I(COT,          -5*3600,        "Colombia Time") \
I(CST,          -6*3600,        "Central Standard Time") \
I(CVT,          -1*3600,        "Cape Verde Time") \
I(CXT,          7*3600,         "Christmas Island Time") \
I(DFT,          1*3600,         "AIX Central European Time") \
I(EAST,         -6*3600,        "Easter Island Standard Time") \
I(EAT,          3*3600,         "East Africa Time") \
I(ECT,          -4*3600,        "Eastern Caribbean Time") \
I(EDT,          -4*3600,        "Eastern Daylight Time") \
I(EEDT,         3*3600,         "Eastern European Daylight Time") \
I(EEST,         3*3600,         "Eastern European Summer Time") \
I(EET,          2*3600,         "Eastern European Time") \
I(EST,          -5*3600,        "Eastern Standard Time") \
I(FJT,          12*3600,        "Fiji Time") \
I(FKST,         -3*3600,        "Falkland Islands Summer Time") \
I(FKT,          -4*3600,        "Falkland Islands Time") \
I(GALT,         -6*3600,        "Galapagos Time") \
I(GET,          4*3600,         "Georgia Standard Time") \
I(GFT,          -3*3600,        "French Guiana Time") \
I(GILT,         12*3600,        "Gilbert Island Time") \
I(GIT,          -9*3600,        "Gambier Island Time") \
I(GMT,          1,              "Greenwich Mean Time") \
I(GST,          -2*3600,     "South Georgia and the South Sandwich Islands") \
I(GYT,          -4*3600,        "Guyana Time") \
I(HADT,         -9*3600,        "Hawaii-Aleutian Daylight Time") \
I(HAST,         -10*3600,       "Hawaii-Aleutian Standard Time") \
I(HKT,          8*3600,         "Hong Kong Time") \
I(HMT,          5*3600,         "Heard and McDonald Islands Time") \
I(HST,          -10*3600,       "Hawaii Standard Time") \
I(IRKT,         8*3600,         "Irkutsk Time") \
I(IRST,         3*3600+30*60,   "Iran Standard Time") \
I(IST,          5*3600+30*60,   "Indian Standard Time") \
I(JST,          9*3600,         "Japan Standard Time") \
I(KRAT,         7*3600,         "Krasnoyarsk Time") \
I(KST,          9*3600,         "Korea Standard Time") \
I(LHST,         10*3600+30*60,  "Lord Howe Standard Time") \
I(LINT,         14*3600,        "Line Islands Time") \
I(MAGT,         11*3600,        "Magadan Time") \
I(MDT,          -6*3600,        "Mountain Daylight Time") \
I(MIT,          -(9*3600+30*60),"Marquesas Islands Time") \
I(MSD,          4*3600,         "Moscow Summer Time") \
I(MSK,          3*3600,         "Moscow Standard Time") \
I(MST,          -7*3600,        "Mountain Standard Time") \
I(MUT,          4*3600,         "Mauritius Time") \
I(NDT,          -(2*3600+30*60),"Newfoundland Daylight Time") \
I(NFT,          11*3600+30*60,  "Norfolk Time") \
I(NPT,          5*3600+45*60,   "Nepal Time") \
I(NST,          -(3*3600+30*60),"Newfoundland Standard Time") \
I(NT,           -(3*3600+30*60),"Newfoundland Time") \
I(OMST,         6*3600,         "Omsk Time") \
I(PDT,          -7*3600,        "Pacific Daylight Time") \
I(PETT,         12*3600,        "Kamchatka Time") \
I(PHOT,         13*3600,        "Phoenix Island Time") \
I(PKT,          5*3600,         "Pakistan Standard Time") \
I(PST,          -8*3600,        "Pacific Standard Time") \
I(RET,          4*3600,         "Reunion Time") \
I(SAMT,         4*3600,         "Samara Time") \
I(SAST,         2*3600,         "South African Standard Time") \
I(SBT,          11*3600,        "Solomon Islands Time") \
I(SCT,          4*3600,         "Seychelles Time") \
I(SLT,          5*3600+30*60,   "Sri Lanka Time") \
I(SST,          -11*3600,       "Samoa Standard Time") \
I(TAHT,         -10*3600,       "Tahiti Time") \
I(THA,          7*3600,         "Thailand Standard Time") \
I(UYST,         -2*3600,        "Uruguay Summer Time") \
I(UYT,          -3*3600,        "Uruguay Standard Time") \
I(VET,          -(4*3600+30*60),"Venezuelan Standard Time") \
I(VLAT,         10*3600,        "Vladivostok Time") \
I(WAT,          1*3600,         "West Africa Time") \
I(WEDT,         1*3600,         "Western European Daylight Time") \
I(WEST,         1*3600,         "Western European Summer Time") \
I(WET,          1,              "Western European Time") \
I(YAKT,         9*3600,         "Yakutsk Time") \
I(YEKT,         5*3600,         "Yekaterinburg Time") \

#define SYM_LIST        CORE_LIST TIMEZONE_LIST

typedef enum SYM_tag {  /* symbols:  order of enum is important */
  SYM_UNKNOWN   = -1,                           /* must be first */
#undef I
#define I(tok, gmtOff, tmzDesc) tok,
SYM_LIST
#undef I
NUM_SYM                                         /* must be last */
} SYM;
#define SYMPN   ((SYM *)NULL)

static CONST int        GmtOff[NUM_SYM] =       /* offsets from GMT (sec.) */
{
#undef I
#define I(tok, gmtOff, tmzDesc) gmtOff,
SYM_LIST
#undef I
};

#ifdef TX_DEBUG
static CONST char       *SymNames[NUM_SYM] =
  {
#undef I
#define I(tok, gmtOff, tmzDesc) #tok,
SYM_LIST
#undef I
};
#endif /* TX_DEBUG */

/*
 * parse translation table - table driven parsers can be your FRIEND!
 */
/* static int sorted=0; */
typedef struct ttok_tag {
    char *name; /* token name */
    SYM value;  /* token id */
}
ttok;

static CONST ttok specials[] = {
  /* This list must be sorted ascending by name, case-insensitive.
   * Don't qsort() every startup:   -KNG 970327
   * Only include timezones if the string is different from SYM token;
   * the SYM token strings are auto-generated in TimezoneSpecials:
   */
    { "am"       , AM       },  /* morning times for 0-12 clock */
    { "apr"      , APR      },
    { "april"    , APR      },
    { "aug"      , AUG      },
    { "august"   , AUG      },
    { "d"        , DAYS     },
    { "day"      , DAYS     },  /* days ... */
    { "days"     , DAYS     },  /* (pluralized) */
    { "dec"      , DEC      },
    { "december" , DEC      },
    { "feb"      , FEB      },
    { "february" , FEB      },
    { "filetime" , SYM_FILETIME },
    { "fri"      , FRI      },
    { "friday"   , FRI      },
    { "h"        , HOURS    },
    { "hour"     , HOURS    },  /* hours ... */
    { "hours"    , HOURS    },  /* (pluralized) */
    { "hr"       , HOURS    },  /* abbreviated */
    { "hrs"      , HOURS    },  /* abbreviated */
    { "jan"      , JAN      },
    { "january"  , JAN      },  /* month names */
    { "jul"      , JUL      },
    { "july"     , JUL      },
    { "jun"      , JUN      },
    { "june"     , JUN      },
    { "last"     , MINUS    },  /* alias for '-' */
    { "m"        , MINUTES  },
    { "mar"      , MAR      },
    { "march"    , MAR      },
    { "may"      , MAY      },
    { "midnight" , MIDNIGHT },  /* 00:00:00 of today or tomorrow */
    { "min"      , MINUTES  },
    { "mins"     , MINUTES  },
    { "minus"    , MINUS    },
    { "minute"   , MINUTES  },  /* minutes multiplier */
    { "minutes"  , MINUTES  },  /* (pluralized) */
    { "mon"      , MON      },
    { "monday"   , MON      },
    { "month"    , MONTHS   },  /* month ... */
    { "months"   , MONTHS   },  /* (pluralized) */
    { "next"     , PLUS     },  /* alias for '+' */
    { "noon"     , NOON     },  /* 12:00:00 of today or tomorrow */
    { "nov"      , NOV      },
    { "november" , NOV      },
    { "now"      , NOW      },  /* opt prefix for PLUS/MINUS */
    { "null"     , SYM_NULL },
    { "oct"      , OCT      },
    { "october"  , OCT      },
    { "plus"     , PLUS     },
    { "pm"       , PM       },  /* evening times for 0-12 clock */
    { "prev"     , MINUS    },  /* alias for '-' */     /* KNG 970327 */
    { "previous" , MINUS    },  /* alias for '-' */     /* KNG 970327 */
    { "s"        , SECONDS  },  /* abbreviated */
    { "sat"      , SAT      },
    { "saturday" , SAT      },
    { "sec"      , SECONDS  },  /* abbreviated */
    { "second"   , SECONDS  },  /* seconds */
    { "seconds"  , SECONDS  },  /* (pluralized) */
    { "secs"     , SECONDS  },  /* abbreviated */
    { "sep"      , SEP      },
    { "september", SEP      },
    { "sun"      , SUN      },
    { "sunday"   , SUN      },  /* day names */
    { "t"	 , ISOTIME  },  /* ISO Time Separator */
    { "teatime"  , TEATIME  },  /* 16:00:00 of today or tomorrow */
    { "this"     , SYM_THIS },  /* e.g. this week KNG 970327 */
    { "thu"      , THU      },
    { "thursday" , THU      },
    { "today"    , TODAY    },  /* execute today - don't advance time */
    { "tomorrow" , TOMORROW },  /* execute 24 hours from time */
    { "tue"      , TUE      },
    { "tuesday"  , TUE      },
    { "ut"       , GMT      },
    { "utc"      , GMT      },
    { "w"        , WEEKS    },
    { "wed"      , WED      },
    { "wednesday", WED      },
    { "week"     , WEEKS    },  /* week ... */
    { "weeks"    , WEEKS    },  /* (pluralized) */
    { "year"     , YEARS    },  /* year ... */
    { "years"    , YEARS    },  /* (pluralized) */
    { "yesterday", YESTERDAY},  /* execute 24 hours from time */
    { "yr"       , YEARS    },  /* abbreviated */
    { "yrs"      , YEARS    },  /* abbreviated */
    { "z"        , GMT      },
} ;
#define NSPECIALS (sizeof specials/sizeof specials[0])

static CONST ttok       TimezoneSpecials[] = {
#undef I
#define I(tok, gmtOff, tmzDesc) { #tok, tok },
  TIMEZONE_LIST
#undef I
};
#define NUM_TIMEZONE_SPECIALS   \
  (sizeof(TimezoneSpecials)/sizeof(TimezoneSpecials[0]))

/**********************************************************************/

/* File scope variables */

#define SCI struct sci_struct
#define SCIPN (SCI *)NULL
SCI
{
   const char  *buf;                    /* scanner - pointer at arglist */
   const char  *sct;     /* scanner - next char pointer in current argument */
   const char   *bufEnd;                        /* end of source buffer */
   char  *sc_token;                         /* scanner - token buffer */
   size_t sc_len;                 /* scanner - length of token buffer */
   SYM    sc_tokid;                             /* scanner - token id */
};

typedef struct TIME_tag {
  struct tm     tm;
  int           fix;            /* begin/end of:  -1, 0, +1 */
  SYM           fixunit;        /*     ""        unit */
  EPI_HUGEINT   directDelta;    /* alternate delta to apply direct to time_t */
  time_t        stdGmtOff;      /* != MAXINT: STD GMT offset */
  time_t        dstGmtOff;      /* != MAXINT: DST GMT offset */
  time_t        dstOff;         /* DST - STD offset (may be guess) */
  /* requested GMT offset:   0=local  1=GMT  other=offset from GMT (sec.) */
  int           reqgmtoff;
  double        filetime;       /* time_t from a FILETIME */
  int           didtod;         /* nonzero: did time-of-day */
  byte          userAbsHours;   /* nonzero: user gave absolute hours value */
  byte          userAbsMinutes; /*   (eg `02:00' not `-2 hours') */
  byte          userAbsSeconds; /* "" seconds */
  byte          gotFiletime;    /* nonzero: got a FILETIME value */
  byte          gotDirectDelta;
} TIME;
#define TIMEPN  ((TIME *)NULL)

/**********************************************************************/

/* Local functions */

/*
 * parse a token, checking if it's something special to us
 */
static SYM
parse_token(SCI *sc, const char *arg, size_t argSz)
{
int i, j, k, cmp;
const char  *tok;

    if (argSz == (size_t)(-1)) argSz = strlen(arg);

    for(j=0,k=NSPECIALS-1,i=k>>1;j<=k;i=(j+k)>>1){      /* binary search */
      cmp = strnicmp(arg, (tok = specials[i].name), argSz);
      if (cmp == 0 && tok[argSz]) cmp = -1;
       if(cmp==0) return(sc->sc_tokid = specials[i].value);
       if(cmp<0) k=i-1;
       else      j=i+1;
    }

    /* Not found in `specials'; look up in `TimezoneSpecials': */
    for (j = 0, k = NUM_TIMEZONE_SPECIALS - 1, i = (k >> 1);
         j <= k;
         i = ((j + k) >> 1))
      {                                         /* binary search */
        cmp = strnicmp(arg, (tok = TimezoneSpecials[i].name), argSz);
        if (cmp == 0 && tok[argSz]) cmp = -1;
        if (cmp == 0) return(sc->sc_tokid = TimezoneSpecials[i].value);
        if (cmp < 0) k = i - 1;
        else j = i + 1;
    }

    /* not special - must be some random id */
    return(ID);
}                                                      /* parse_token */

/**********************************************************************/

/*
 * init_scanner() sets up the scanner to eat arguments
 */
static int
init_scanner(SCI *sc, const char *buf, const char *bufEnd)
{
#ifdef TX_DEBUG
  printf("init_scanner to [%.*s]\n", (int)(bufEnd - buf), buf);
#endif /* TX_DEBUG */
    sc->buf = sc->sct = buf;
    sc->bufEnd = bufEnd;
    sc->sc_len = (bufEnd - buf) + 1;            /* +1 for nul */

    sc->sc_token = (char *)TXmalloc(TxParsetimePmbuf, __FUNCTION__,
                                    sc->sc_len);
    if (sc->sc_token == NULL) return(-1);
    return 0;
}                                                     /* init_scanner */

/**********************************************************************/

static void
reset_scanner(SCI *sc, const char *where)
{
    sc->sct=(where != CHARPN ? where : sc->buf);
#ifdef TX_DEBUG
    printf("reset_scanner to [%.*s]\n", (int)(sc->bufEnd - sc->sct), sc->sct);
#endif /* TX_DEBUG */
}                                              /* end reset_scanner() */

static const char *
save_scanner(SCI *sc)
{
  return(sc->sct);
}

/**********************************************************************/

static int
TXparsetimeGetNextTokenChar(SCI *sc)
/* Gets next character as a token, returning it.
 */
{
        sc->sc_token[1]='\0';
        if (sc->sct >= sc->bufEnd)
          {
            sc->sc_token[0] = '\0';
            sc->sc_tokid = TEOF;
        }else{
            sc->sc_token[0] = *(sc->sct++);
            sc->sc_tokid = ID;
        }
        return(sc->sc_token[0]);
}

static int TXparsetimePeekNextTokenChar ARGS((SCI *sc));
static int
TXparsetimePeekNextTokenChar(sc)
SCI     *sc;    /* (in/out) lexical scanner */
/* Returns next character as a token, but does not change state or
 * consume it, i.e. next TXparsetimeGetNextTokenChar() call will also
 * return this.
 * Returns -1 if next char is EOF.
 */
{
  return(sc->sct < sc->bufEnd ? *sc->sct : -1);
}

/**********************************************************************/

/*
 * token() fetches a token from the input stream
 */
static SYM token ARGS((SCI *sc));
static SYM
token(sc)
SCI *sc;
{
  size_t        idx;

        *sc->sc_token = '\0';
        sc->sc_tokid = TEOF;

        /*
         * if we need to read another argument, walk along the argument list;
         * when we fall off the arglist, we'll just return TEOF forever
         */
        if (sc->sct >= sc->bufEnd)
            return(sc->sc_tokid);
        /*
         * eat whitespace now - if we walk off the end of the argument,
         * we'll continue, which puts us up at the top of the while loop
         * to fetch the next argument in
         */
        while (sc->sct < sc->bufEnd && TX_ISSPACE(*sc->sct))
            ++sc->sct;
        if (sc->sct >= sc->bufEnd) {
            return(sc->sc_tokid);
        }

        /*
         * preserve the first character of the new token
         */
        sc->sc_token[0] = *sc->sct++;
        sc->sc_token[1] = '\0';

        /*
         * then see what it is
         */
        idx = 0;
        if (TX_ISDIGIT(sc->sc_token[0])) {
            while (sc->sct < sc->bufEnd && TX_ISDIGIT(*sc->sct))
                sc->sc_token[++idx] = *sc->sct++;
            sc->sc_token[++idx] = '\0';
            return sc->sc_tokid = SYM_NUMBER;
        } else if (TX_ISALPHA(sc->sc_token[0])) {
            while (sc->sct < sc->bufEnd && TX_ISALPHA(*sc->sct))
                sc->sc_token[++idx] = *sc->sct++;
            sc->sc_token[++idx] = '\0';
            return parse_token(sc, sc->sc_token, idx);
        }
        else
          {
            switch (sc->sc_token[0])
              {
              case ':': sc->sc_tokid = COLON;           break;
              case '.': sc->sc_tokid = DOT;             break;
              case '+': sc->sc_tokid = PLUS;            break;
              case '-': sc->sc_tokid = MINUS;           break;
              case '/': sc->sc_tokid = SLASH;           break;
              case ',': sc->sc_tokid = COMMA;           break;
              case '(': sc->sc_tokid = SYM_LPAREN;      break;
              case ')': sc->sc_tokid = SYM_RPAREN;      break;
              default:  sc->sc_tokid = JUNK;            break;
              }
            return(sc->sc_tokid);
          }
}                                               /* end token() */

#ifdef TX_DEBUG
static SYM _token ARGS((SCI *sc, int line));
static SYM
_token(sc, line)
SCI     *sc;
int     line;
{
  SYM   tok;
  char  *loc;

  loc = sc->sct;
  tok = token(sc);
  printf("at [%.20s%s] line %4d got token %s\n",
        loc, (strlen(loc) > 20 ? "..." : ""), line, SymNames[tok]);
  return(tok);
}
#  define token(sc)     _token(sc, __LINE__)
#endif /* TX_DEBUG */

static CONST char *getcurloc ARGS((SCI *sc, SYM *sym));
static CONST char *
getcurloc(sc, sym)
SCI     *sc;
SYM     *sym;   /* (out) current token symbol */
/* Returns current parse location, for later restore with setcurloc().
 */
{
  if (sym != SYMPN) *sym = sc->sc_tokid;
  return(sc->sct);
}

static int setcurloc ARGS((SCI *sc, CONST char *mark, SYM sym));
static int
setcurloc(sc, mark, sym)
SCI             *sc;
CONST char      *mark;
SYM             sym;    /* (in) symbol to restore */
/* Sets current parse location, from earlier save with getcurloc().
 */
{
  sc->sc_tokid = sym;
  sc->sct = mark;
#ifdef TX_DEBUG
  printf("setcurloc to [%.*s]\n", (int)(sc->bufEnd - sc->sct), sc->sct);
#endif /* TX_DEBUG */
  return(1);                                    /* success WTF error check */
}


/**********************************************************************/

int
TXgetparsetimemesg()
{
	return TxParsetimeMesg;
}

int
TXsetparsetimemesg(n)
int n;
{
	int rc;

	rc = TxParsetimeMesg;
	TxParsetimeMesg = n;
	return rc;
}

/*
 * plonk() gives an appropriate error message if a token is incorrect
 */
#define plonk(a,b) iplonk(a, b, __FUNCTION__)
static void iplonk ARGS((SCI *sc, int tok, CONST char *fn));
static void
iplonk(sc, tok, fn)
SCI *sc;
int tok;
CONST char *fn;
{
    if(TxParsetimeMesg)
       txpmbuf_putmsg(TxParsetimePmbuf, MERR + UGE, fn, "%s time: %s %.*s",
           tok==TEOF?"incomplete":"garbled",
                      sc->sc_token, (int)(sc->bufEnd - sc->sct), sc->sct);
}                                                           /* iplonk */

/**********************************************************************/

/*
 * expect() gets a token and dies most horribly if it's not the token we want
 */
static int expect ARGS((SCI *sc, SYM desired));
static int
expect(sc, desired)
SCI *sc;
SYM desired;
{
    if (token(sc) != desired){
        plonk(sc,sc->sc_tokid);                 /* and we die here... */
        return(-1);
    }
    return(0);
}                                                           /* expect */

static struct tm *
doLocalTime(TIME *info, time_t tim)
/* Wrapper for localtime() that lets us grab STD/DST offsets.
 */
{
  struct tm     *tm;

  tm = localtime(&tim);
  if (tm)
    {
#if defined(EPI_HAVE_TM_GMTOFF)
      if (tm->tm_isdst)
        info->dstGmtOff = tm->tm_gmtoff;
      else
        info->stdGmtOff = tm->tm_gmtoff;
      if (info->stdGmtOff != EPI_OS_TIME_T_MAX &&
          info->dstGmtOff != EPI_OS_TIME_T_MAX)
        info->dstOff = info->dstGmtOff - info->stdGmtOff;
#endif /* EPI_HAVE_TM_GMTOFF */
    }
  return(tm);
}

static time_t
doMkTime(TIME *info)
/* Wrapper for mktime() that lets us grab STD/DST offsets.
 */
{
  struct tm     *tm = &info->tm;
  time_t        ret;

  ret = mktime(tm);
#if defined(EPI_HAVE_TM_GMTOFF)
  if (ret != (time_t)(-1))
    {
      if (tm->tm_isdst)
        info->dstGmtOff = tm->tm_gmtoff;
      else
        info->stdGmtOff = tm->tm_gmtoff;
      if (info->stdGmtOff != EPI_OS_TIME_T_MAX &&
          info->dstGmtOff != EPI_OS_TIME_T_MAX)
        info->dstOff = info->dstGmtOff - info->stdGmtOff;
    }
#endif /* EPI_HAVE_TM_GMTOFF */
  return(ret);
}

static int
doDstStdCrossingFixup(TIME *tim, time_t *timer, int wasDst)
/* Assumes `tim->tm.tm_isdst' is valid for `tim->tm' and `*timer',
 * and `*timer' is valid; fixes both for DST <-> STD crossing.
 */
{
  struct tm     *tp;
  int           ret;
  time_t        newTimer;

  /* If the `tm_...' time(s) added to `tim->tm' (since its last
   * localtime() computation) cross an odd number of DST <-> STD
   * changeovers, the resulting `*timer' will be off by an hour;
   * adjust for that here.  Note that we turned off this adjustment
   * (or avoided it via `directDelta') for units < day, because
   * e.g. `-240 hours' is expected to be 240*60*60 seconds ago, even
   * if crossing DST <-> STD and the HH number changes +/-1:
   */

  if (wasDst < 0 || tim->tm.tm_isdst < 0) goto err;

  newTimer = *timer - ((tim->tm.tm_isdst > 0) - (wasDst > 0))*tim->dstOff;
  tp = doLocalTime(tim, newTimer);
  if (!tp)
    {
      txpmbuf_putmsg(TxParsetimePmbuf, MERR, __FUNCTION__,
                     "localtime() failed for time_t %wd",
                     (EPI_HUGEINT)newTimer);
      goto err;
    }

  /* If `now' is Sat 2:30 STD the day before STD -> DST, then `+1 day'
   * initially gives Sun 3:30 DST, and our fix above would subtract
   * dstOff to try for `2:30' display time.  But Sun 2:30 DST is
   * canonically Sun 1:30 STD; we cannot disply 2:30 on Sun STD -> DST
   * (invalid zone), must pick 1:30 or 3:30.  Either choice is equally
   * "wrong" from a maintain-the-wall-clock-display standpoint; but
   * skipping the DST fixup (i.e. using 3:30 DST) at least maintains
   * +24 hours diff, so we go with it.  This issue does not arise
   * with DST -> STD crossing, because there is a wall clock overlap,
   * not a gap: we can "choose" which 1:30 we want.
   *
   * We detect this issue by looking for STD <-> DST change during
   * +/-dstOff fixup above: fixup will never cross if *original*
   * (wasDst -> tim) cross is DST -> STD (because fixup would always
   * go *away* from DST -> STD change time); fixup only potentially
   * crosses if original cross is STD -> DST:
   */
  if ((tp->tm_isdst > 0) == (tim->tm.tm_isdst > 0))
    {                                           /* no issue; keep new time */
      *timer = newTimer;
      tim->tm = *tp;                            /* and save new tm_isdst */
    }

  ret = 1;
  goto finally;

err:
  ret = 0;
finally:
  return(ret);
}

/**********************************************************************/

static void
TXprTm(char *buf, size_t bufSz, const struct tm *tm)
{
  htsnpf(buf, bufSz, "%04d-%02d-%02d %02d:%02d:%02d",
         (int)(tm->tm_year + 1900), (int)tm->tm_mon + 1, (int)tm->tm_mday,
         (int)tm->tm_hour, (int)tm->tm_min, (int)tm->tm_sec);
}

static void
dateaddsub(time_t seconds, TIME *tim, int sign, int doDstStdFixup)
/* Subtracts a number of seconds from a date.
 */
{
  struct tm     *tm, *tp;
  time_t        t2;
  int           wasDst;

  tm = &tim->tm;
  tm->tm_wday = tm->tm_yday = -1;
  /* Do not clear `tm_isdst': we are merely converting *unchanged*
   * (hopefully) `tm_...' fields back to original time_t here via
   * mktime(), and need original `tm_isdst' to resolve DST -> STD
   * overlap hour.  Our localtime() will update `tm_isdst'.  See
   * `tm_isdst changes' comment elsewhere:
   */
  wasDst = tm->tm_isdst;
  t2 = doMkTime(tim);
  if (t2 == (time_t)(-1))                       /* mktime() failed */
    {
      if (TxParsetimeMesg)
        {
          char  tmBuf[128];
          TXprTm(tmBuf, sizeof(tmBuf), tm);
          txpmbuf_putmsg(TxParsetimePmbuf, MERR, __FUNCTION__,
                         "Cannot mktime %s", tmBuf);
        }
      goto err;
    }
  if (sign < 0)
    t2 -= seconds;
  else if (sign > 0)
    t2 += seconds;
  if ((tp = doLocalTime(tim, t2)) != (struct tm *)NULL)     /* MAW 03-03-03 */
    *tm = *tp;                                  /* also saves new tm_isdst */
  else
    {
      if (TxParsetimeMesg)
        txpmbuf_putmsg(TxParsetimePmbuf, MERR, __FUNCTION__,
                       "Cannot localtime %wd", (EPI_HUGEINT)t2);
      goto err;
    }

  /* Do DST <-> STD crossing fixup, if requested (e.g. for weeks/days,
   * which should align to same wall clock HH:MM as starting time):
   */
  if (doDstStdFixup)
    doDstStdCrossingFixup(tim, &t2, wasDst);

err:                                            /* wtf */
  return;
}                                                          /* dateaddsub */

/**********************************************************************/

static int assign_date(TIME *info, long mday, long mon, long year);

/*
 * plus() parses a now + time
 *
 *  at [NOW] {PLUS|MINUS} NUMBER [SECONDS|MINUTES|HOURS|DAYS|WEEKS|MONTHS]
 *  at [NOW] {PLUS|MINUS} DAYOFWEEK
 *
 */

/* sign == 0 for "this"  KNG 970327 */

static int plusminus ARGS((SCI *sc, TIME *tim, int sign));
static int
plusminus(sc, tim, sign)
SCI *sc;
TIME    *tim;
int sign;
{
struct tm       *tm;
time_t delay=1;
int    day, mday, mon, year, ret;
SYM     tok;

    tm = &tim->tm;
    if (sign < 0)
      sign = -1;
    else if (sign > 0)
      sign = 1;

    tim->fixunit = tok = token(sc);
    switch (tok) {
    case YEARS  : goto zyears;
    case MONTHS : goto zmonths;
    case WEEKS  : goto zweeks;
    case DAYS   : goto zdays;
    case HOURS  : goto zhours;
    case MINUTES: goto zminutes;
    case SECONDS: goto zseconds;
    case SYM_NUMBER:
       delay = atoi(sc->sc_token);
       tim->fixunit = tok = token(sc);
       switch (tok) {
       case YEARS:
       zyears:
         tm->tm_year += sign*delay;
         tm->tm_isdst = -1;                     /* see `tm_isdst changes' */
         goto ok;
       case MONTHS:
       zmonths:
         for ( ; delay > 12; delay -= 12)
           tm->tm_year += sign;
         tm->tm_mon += sign*delay;
         tm->tm_isdst = -1;                     /* see `tm_isdst changes' */
         if (tm->tm_mon < 0)
           {
             tm->tm_year--;
             tm->tm_mon += 12;
           }
         else if (tm->tm_mon > 11)
           {
             tm->tm_year++;
             tm->tm_mon -= 12;
           }
         goto ok;
       case WEEKS:
       zweeks:
               delay *= 7;
       case DAYS:
       zdays:
               delay *= 24;
       case HOURS:
       zhours:
               delay *= 60;
       case MINUTES:
       zminutes:
               delay *= 60;
       case SECONDS:
       zseconds:
         switch (tok)
           {
           case HOURS:
           case MINUTES:
           case SECONDS:
             /* KNG 20171114 for `fixunit' less than DAYS, caller
              * should modify time_t directly, instead of us changing
              * h/m/s here, so that DST <-> STD change is ignored:
              */
             tim->directDelta = sign*delay;
             tim->gotDirectDelta = 1;
             break;
           default:
             if (sign)
               dateaddsub(delay, tim, (sign < 0 ? -1 : 1),
                          (tok == DAYS || tok == WEEKS));
             break;
           }
         goto ok;
       case SUN: case MON: case TUE: case WED: case THU: case FRI: case SAT:
         goto zwday;
       case JAN: case FEB: case MAR: case APR: case MAY: case JUN:
       case JUL: case AUG: case SEP: case OCT: case NOV: case DEC:
         goto znmonth;
       default:         /* shut up gcc */
         break;
       }
       break;
    case SUN: case MON: case TUE: case WED: case THU: case FRI: case SAT:
    zwday:
       tim->fixunit = DAYS;
       day=sc->sc_tokid-SUN;
       if (sign<0){
          if(day<tm->tm_wday) day = tm->tm_wday-day;
          else                day=7-(day-tm->tm_wday);
          tm->tm_mday-=day;
          tm->tm_isdst = -1;                    /* see `tm_isdst changes' */
       }else if (sign > 0) {
          if(day>tm->tm_wday) day -= tm->tm_wday;
          else                day=7-(tm->tm_wday-day);
          tm->tm_mday+=day;
          tm->tm_isdst = -1;                    /* see `tm_isdst changes' */
       }
       else             /* "this" */
         {
           tm->tm_mday += (day - tm->tm_wday);
           tm->tm_isdst = -1;                   /* see `tm_isdst changes' */
         }
       delay--;
       if (delay > 0)
         {
           delay *= 86400*7;
           if (sign)
             /* Also doDstStdCrossingFixup(): weekdays should HH:MM align? */
             dateaddsub(delay, tim, (sign < 0 ? -1 : 1), 1);
         }
       goto ok;
    case JAN: case FEB: case MAR: case APR: case MAY: case JUN:
    case JUL: case AUG: case SEP: case OCT: case NOV: case DEC:
    znmonth:
      /* ... MONTH [DAY|YEAR] */
      year = tm->tm_year;
      mon = (tok - JAN);
      tim->fixunit = MONTHS;
      mday = tm->tm_mday;
      switch (token(sc))
        {
        case SYM_NUMBER:                /* mday */
          mday = atol(sc->sc_token);
          if (mday > 50)                /* > 50: prob. year not day */
            {
              tim->fixunit = MONTHS;
              year = mday;
              mday = tm->tm_mday;
              delay = 0;                /* there is no _next_ July 1997 */
            }
          else
            tim->fixunit = DAYS;
          break;
        case TEOF:                      /* e.g. month alone */
          mday = tm->tm_mday;
          break;
        default:
          plonk(sc, sc->sc_tokid);
          goto err;
        }
      if (delay > 0)
        {
          if (sign < 0)
            {
              if (mon < tm->tm_mon) delay--;
            }
          else if (sign > 0)
            {
              if (mon > tm->tm_mon) delay--;
            }
        }
      year += sign*delay;
      if (assign_date(tim, mday, mon, year) < 0)
        goto err;
    ok:
      ret = 0;
      goto finally;
    default:            /* shut up gcc */
      break;
    }
    plonk(sc,sc->sc_tokid);
    goto err;

err:
    ret = -1;
finally:
    return(ret);
}                                                             /* plusminus */

static int fixampm ARGS((TIME *tim, int sym));
static int
fixampm(tim, sym)
TIME    *tim;
int     sym;
{
  switch ((SYM)sym)
    {
    case TEOF:
      break;
    case AM:
      if (tim->tm.tm_hour == 12)
        {
          tim->tm.tm_hour -= 12;
          tim->tm.tm_isdst = -1;                /* see `tm_isdst changes' */
        }
      break;
    case PM:
      if (tim->tm.tm_hour < 12)
        {
          tim->tm.tm_hour += 12;
          tim->tm.tm_isdst = -1;                /* see `tm_isdst changes' */
        }
      break;
    default:
      return(-1);
    }
  return(0);
}

/* ------------------------------------------------------------------------ */

static int TXparsetimeGetSecondsFraction ARGS((SCI *sc, TIME *tim));
static int
TXparsetimeGetSecondsFraction(sc, tim)
SCI	*sc;	/* (in/out) lexical scanner */
TIME	*tim;	/* (out) time to add to */
/* Parses optional `.nnn...' fractional seconds.
 * Returns 2 if parsed, 1 if not present, 0 on error.
 */
{
  (void)tim;
  if (TXparsetimePeekNextTokenChar(sc) != '.') return(1);
  if (token(sc) != DOT) return(0);              /* should not happen */
  if (token(sc) != SYM_NUMBER) return(0);
  /* parse sc->sc_token here someday, for fraction of a second */
  return(2);                                    /* success */
}

/**********************************************************************/

static int ymdhms ARGS((SCI *sc, TIME *tim));
static int
ymdhms(sc, tim)                    /* parse YYYY-MM-DD [HH:MM[:SS][PM]] */
SCI *sc;
TIME    *tim;
/* Returns -1 on error, 0 on success.
 */
{
  struct tm       *tm;
  CONST char    *afterhms = (CONST char *)NULL, *saveLoc = (CONST char *)NULL;
  SYM           afterHmsSym = TEOF, saveSym = TEOF, sym;
  int           n, dirfactor;
  size_t        len;
char sep, sep2, *tok=sc->sc_token;

  tm = &tim->tm;
                                                      /* process date */
   len = strlen(tok);
   switch (len)
     {
     case 8:                                            /* YYYYMMDD */
      tm->tm_mday=atoi(tok+6);
      tok[6]='\0';
      tm->tm_mon=atoi(tok+4)-1;
      tok[4]='\0';
      tm->tm_year=atoi(tok)-1900;
      tim->fixunit = DAYS;
      break;
     case 4:                                            /* YYYY[-MM[-DD]] */
      tm->tm_year=atoi(tok)-1900;
      tim->fixunit = YEARS;
      tm->tm_mon = 0;
      tm->tm_mday = 1;
      tm->tm_isdst = -1;                        /* see `tm_isdst changes' */
      if ((sep = TXparsetimeGetNextTokenChar(sc)) == '\0')
        /* Allow just `YYYY', e.g. for Group By binning by year: */
        break;
      /* KNG 20181009 `2004x5' is probably invalid: */
#define OK_YMD_SEP(c)   (TX_ISSPACE(c) || (c) == '/' || (c) == '\\' || \
                         (c) == '-' || (c) == ':' || (c) == '.' || (c) == '_')
      /* more after YYYY: */
      if (!OK_YMD_SEP(sep)) return(-1);
      sym = token(sc);
      switch (sym)
        {
        case JAN:
        case FEB:
        case MAR:
        case APR:
        case MAY:
        case JUN:
        case JUL:
        case AUG:
        case SEP:
        case OCT:
        case NOV:
        case DEC:
          tm->tm_mon = (int)(sym - JAN);
          tm->tm_isdst = -1;                    /* see `tm_isdst changes' */
          break;
        case SYM_NUMBER:
          tm->tm_mon = atoi(tok) - 1;
          tm->tm_isdst = -1;                    /* see `tm_isdst changes' */
          break;
        default:
          return(-1);
        }
      tim->fixunit = MONTHS;
      if ((sep2 = TXparsetimeGetNextTokenChar(sc)) == '\0') /* just YYYY-MM */
        break;
      /* more after YYYY-MM: */
      if (sep2 != sep ||                        /* `2004-06:12' probably bad*/
          token(sc) != SYM_NUMBER)
        return(-1);
      tm->tm_mday=atoi(tok);
      tim->fixunit = DAYS;
      tm->tm_isdst = -1;                        /* see `tm_isdst changes' */
      break;
     default:
      return(-1);
   }
   if(tm->tm_mon<0 || tm->tm_mon>11 || 
      tm->tm_mday<0 || tm->tm_mday>31){ return(-1); }
                                             /* process optional time */
   token(sc);
   tm->tm_hour = 0;
   tm->tm_min = 0;
   tm->tm_sec = 0;
   tm->tm_isdst = -1;                           /* see `tm_isdst changes' */
   if(sc->sc_tokid==ISOTIME) token(sc);
   if(sc->sc_tokid==TEOF)
   {
	   return(0);
   }
   if(sc->sc_tokid!=SYM_NUMBER){ return(-1); }
   len = strlen(tok);
   if (len == 4 || len == 6)                    /* HHMM[SS[.nnn...]][AM|PM] */
     {
      if (len == 6)                             /* HHMMSS... */
        {
         tm->tm_sec=atoi(tok+4);
         tim->userAbsSeconds = 1;
         tok[4]='\0';
         tim->fixunit = MINUTES;
         /* delay TXparsetimeGetSecondsFraction() until done with `tok' */
      }
      else                                      /* HHMM... */
        {
         tm->tm_sec=0;
         tim->fixunit = SECONDS;
        }
      tm->tm_min=atoi(tok+2);
      tim->userAbsMinutes = 1;
      tok[2]='\0';
      tm->tm_hour=atoi(tok);
      tim->userAbsHours = 1;
      if (tim->fixunit == MINUTES &&
          !TXparsetimeGetSecondsFraction(sc, tim))
        return(-1);
      saveLoc = getcurloc(sc, &saveSym);        /* bookmark */
      if (fixampm(tim, token(sc)) < 0)          /* not AM/PM */
        setcurloc(sc, saveLoc, saveSym);        /* then restore to bookmark */
   }else if (len == 1 || len == 2)              /* H[H]:M[:S] */
     {
      tm->tm_hour=atoi(tok);
      tim->userAbsHours = 1;
      tim->fixunit = HOURS;
      /* KNG 20181009 `12x34' is probably invalid: */
#define OK_HMS_SEP(c)   (TX_ISSPACE(c) || \
                         (c) == '-' || (c) == ':' || (c) == '.' || (c) == '_')
      if ((sep = TXparsetimeGetNextTokenChar(sc)) == '\0' ||
          !OK_HMS_SEP(sep) ||
          token(sc) != SYM_NUMBER)
        return(-1);
      tm->tm_min=atoi(tok);
      tim->userAbsMinutes = 1;
      tim->fixunit = MINUTES;
      /* KNG 20181009 bookmark early to catch `... 12:34_' error: */
      afterhms = getcurloc(sc, &afterHmsSym);   /* bookmark after H:M[:S] */
      if ((sep2 = TXparsetimeGetNextTokenChar(sc)) == '\0') /* H[H]:M[M] */
        {
         tm->tm_sec=0;
      }else{
         if(sep2==sep){                         /* H[H]:M[M]:S[.nnn...]... */
            if(token(sc)!=SYM_NUMBER){ return(-1); }
            tm->tm_sec=atoi(tok);
            tim->userAbsSeconds = 1;
            tim->fixunit = SECONDS;
            if (!TXparsetimeGetSecondsFraction(sc, tim)) return(-1);
            afterhms = getcurloc(sc, &afterHmsSym); /* bookmark after H:M:S*/
            sep2 = TXparsetimeGetNextTokenChar(sc);
         }
         while (TX_ISSPACE(sep2))               /* MAW 09-16-94 */
           sep2 = TXparsetimeGetNextTokenChar(sc);
	 if (sep2 == '+' || sep2 == '-')
	 {
		dirfactor = (sep2 == '-' ? -1 : 1);
		if(token(sc)==SYM_NUMBER)
		{
			int	gmtoff;

			gmtoff = (int)strtol(tok, NULL, 10);
			if (gmtoff >= 100)	/* +0400 */
			{
				/* Convert `gmtoff' to minutes: */
				if (gmtoff % 100 > 59) return(-1);
				gmtoff = (gmtoff/100)*60 + (gmtoff % 100);
			}
			else			/* +04[:00] */
			{
				gmtoff *= 60;	/* convert hours to minutes */
				sep2 = TXparsetimeGetNextTokenChar(sc);
				switch (sep2)
				{
				case ':':	/* +04:00 */
					if (token(sc) != SYM_NUMBER)
						return(-1);
					if ((n = atoi(tok)) > 59) return(-1);
					gmtoff += n;
					break;
				case TEOF:	/* +04 */
					break;
				default:	/* ? */
					return(-1);
				}
			}
			sep2 = TXparsetimeGetNextTokenChar(sc);
			/* convert `gmtoff' minutes to seconds, and offset: */
			tim->reqgmtoff = dirfactor * 60 * gmtoff;
			/* Bug 6149: set GMT flag *after* convert to seconds:*/
			if (tim->reqgmtoff == 0) tim->reqgmtoff = 1;/*1: GMT*/
		} else
		{
			return -1;
		}
	 }
	 else if(sep2 == 'Z')
	 {
		 tim->reqgmtoff = 1;		/* 1: GMT */
	 } else
         if(sep2!='\0'){
            setcurloc(sc, afterhms, afterHmsSym); /* back up to after H:M:S */
            switch ((sym = token(sc)))
              {
              case AM:
              case PM:
                if (fixampm(tim, sym) < 0) return(-1);
                break;
              default:
                /* Back up before unknown token.  May be timezone, which
                 * caller will parse (?), or unknown, which caller will
                 * yap about if not TEOF:   KNG 20070321
                 */
                setcurloc(sc, afterhms, afterHmsSym);
                break;
              }
         }
      }
   }else{
      return(-1);
   }
   if(tm->tm_hour<0 || tm->tm_hour>23 ||
      tm->tm_min<0 || tm->tm_min>59 ||
      tm->tm_sec<0 || tm->tm_sec>59){
      return(-1);
   }
   return(0);
}                                                         /* ymdhms() */

static int TXparsetimeParseTimezoneIfPresent ARGS((SCI *sc, TIME *tim));
static int
TXparsetimeParseTimezoneIfPresent(sc, tim)
SCI     *sc;
TIME    *tim;
/* Parses timezone if present at current token.  Formats:
 *   [(]TZ[)] [+/-NNNN] [FILETIME=...]
 *   +|-NNNN [[(]TZ[)]] [FILETIME=...]
 * Returns 1 if found, 0 if not.
 */
{
  int           inParen = 0, num, hours, minutes, gotTz = 0, gotOffset = 0;
  SYM           saveSym = TEOF, orgSym;
  CONST char    *saveLoc = CHARPN;
  EPI_UINT32    filetimeLo, filetimeHi;
  char          *e;

  switch (orgSym = sc->sc_tokid)
    {
    case PLUS:
    case MINUS:                                 /* maybe `-0400' */
    getOffset:
      saveLoc = getcurloc(sc, &saveSym);        /* bookmark */
      if (sc->sct > sc->buf && sc->sct[-1] != '+' && sc->sct[-1] != '-')
        goto restoreAndBail;                    /* do not accept `plus' */
      if (token(sc) != SYM_NUMBER ||            /* not a number or */
          sc->sct - saveLoc != 4)               /* not 4 digits */
        goto restoreAndBail;
      num = atoi(sc->sc_token);
      hours = num/100;
      minutes = num % 100;
      if (hours > 23) goto restoreAndBail;      /* invalid */
      if (minutes > 59) goto restoreAndBail;    /* invalid */
      tim->reqgmtoff = hours*3600 + minutes*60;
      if (orgSym == MINUS)
        tim->reqgmtoff = -tim->reqgmtoff;
      /* Bug 6149: fix 0 offset: */
      if (tim->reqgmtoff == 0) tim->reqgmtoff = 1;	/* 1: GMT */
      gotOffset = 1;
      break;
    case SYM_LPAREN:                            /* maybe `(EDT)' */
    getParenTz:
      inParen = 1;
      /* See if next token is a timezone: */
      saveLoc = getcurloc(sc, &saveSym);        /* bookmark */
      token(sc);                                /* advance to (maybe) TZ */
      if (GmtOff[sc->sc_tokid] == 0)            /* next tok not a timezone */
        {
        restoreAndBail:
          setcurloc(sc, saveLoc, saveSym);      /* then restore to bookmark */
          return(gotTz || gotOffset);
        }
      /* fall through: */
    default:                                    /* maybe `EDT' */
      if (GmtOff[sc->sc_tokid] == 0)            /* not a timezone */
        return(gotTz || gotOffset);
    getTz:
      tim->reqgmtoff = GmtOff[sc->sc_tokid];    /* will be 1 for GMT */
      if (inParen)
        {
          saveLoc = getcurloc(sc, &saveSym);    /* bookmark */
          if (token(sc) != SYM_RPAREN)          /* not expected */
            setcurloc(sc, saveLoc, saveSym);
        }
      gotTz = 1;
      break;
    }

  /* If we got offset but not timezone, check for timezone, and vice-versa.
   * For `-0400 EDT' and `EDT -0400':
   */
  inParen = 0;
  if (gotTz ^ gotOffset)                        /* exactly one obtained */
    {
      saveLoc = getcurloc(sc, &saveSym);
      switch (orgSym = token(sc))
        {
        case PLUS:
        case MINUS:
          if (sc->sct > sc->buf &&
              (sc->sct[-1] == '+' || sc->sct[-1] == '-') &&
              !gotOffset)
            goto getOffset;
          break;
        case SYM_LPAREN:
          if (!gotTz) goto getParenTz;
          break;
        case SYM_FILETIME:                      /* optimization */
          goto getFiletime;
        default:
          if (!gotTz && GmtOff[sc->sc_tokid] != 0) goto getTz;
          break;
        }
      setcurloc(sc, saveLoc, saveSym);
    }

  /* After timezone or offset, accept (and ignore) `FILETIME=[....]': */
  if (gotTz || gotOffset)
    {
      saveLoc = getcurloc(sc, &saveSym);
      if (token(sc) != SYM_FILETIME) goto restoreAndBail;
    getFiletime:
      if (sc->sct < sc->bufEnd && *(sc->sct++) != '=') goto restoreAndBail;
      if (sc->sct < sc->bufEnd && *(sc->sct++) != '[') goto restoreAndBail;
      filetimeLo = TXstrtoul(sc->sct, sc->bufEnd, &e, 0x10, NULL);
      if (e <= sc->sct) goto restoreAndBail;
      sc->sct = e;
      if (sc->sct < sc->bufEnd && *(sc->sct++) != ':') goto restoreAndBail;
      filetimeHi = TXstrtoul(sc->sct, sc->bufEnd, &e, 0x10, NULL);
      if (e <= sc->sct) goto restoreAndBail;
      sc->sct = e;
      if (sc->sct < sc->bufEnd && *(sc->sct++) != ']') goto restoreAndBail;
      /* Convert and save the FILETIME value: it is absolute (no
       * timezones) and sub-second resolution, so let it override
       * computed time later:
       */
      tim->filetime = TXfiletime2time_t(filetimeLo, filetimeHi);
      tim->gotFiletime = 1;
      /* No need to warn about no timezone in STD/DST ambiguous zone,
       * since we have an absolute time_t now:
       */
      tim->reqgmtoff = 1;                       /* 1: GMT */
    }

  return(gotTz || gotOffset);
}

/**********************************************************************/

/*
 * tod() computes the time of day
 *     [NUMBER [DOT NUMBER [DOT NUMBER]] [AM|PM]]
 */
static int tod ARGS((SCI *sc, TIME *tim));
static int
tod(sc, tim)
SCI *sc;
TIME    *tim;
/* return -1 on error, 0 if ok, 1 if nothing parsed.
 */
{
  struct tm     *tm;
  int           hour, minute = 0, sec = 0, ret = 1;
  size_t        tlen;
  SYM		sym;

  if (tim->didtod) goto chkgmt;         /* for Thu Jun 21 13:47:01 2001 GMT */
    tm = &tim->tm;
    hour = atoi(sc->sc_token);
    tlen = strlen(sc->sc_token);
    if(tlen == 0)
    {
       tm->tm_hour = 0;
       tm->tm_min = 0;
       tm->tm_sec = 0; /* JMT Wed Nov 23 11:41:34 EST 1994 */
       tm->tm_isdst = -1;                       /* see `tm_isdst changes' */
       return 0;
    }
    if (sc->sc_tokid != SYM_NUMBER) goto chkgmt;

    /*
     * first pick out the time of day - if it's 4 digits, we assume
     * a HHMM time, otherwise it's HH DOT MM time
     */
    tim->fixunit = HOURS;
    tim->userAbsHours = 1;
    if (tlen <= 2 && ((sym = token(sc)) == DOT || sym == COLON))
      {
        if(expect(sc,SYM_NUMBER)<0) return(-1);
        minute = atoi(sc->sc_token);
        tim->fixunit = MINUTES;
        tim->userAbsMinutes = 1;
        if (minute > 59){
            panic("garbled time: minute>59");
            return(-1);
        }
        ret = 0;
        if ((sym = token(sc)) == DOT || sym == COLON)  /* .SS   KNG 970403 */
          {
            if (expect(sc, SYM_NUMBER) < 0) return(-1);
            sec = atoi(sc->sc_token);
            tim->fixunit = SECONDS;
            tim->userAbsSeconds = 1;
            ret = 0;
            if (!TXparsetimeGetSecondsFraction(sc, tim))
              return(-1);
            token(sc);                  /* KNG 010620 */
          }
    } else if (tlen == 4) {
        minute = hour%100;
        tim->fixunit = MINUTES;
        tim->userAbsMinutes = 1;
        if (minute > 59){
            panic("garbled time: minute>59");
            return(-1);
        }
        ret = 0;
        hour = hour/100;
        token(sc);                      /* KNG 010620 */
    } else if (tlen == 6)               /* HHMMSS   KNG 970403 */
      {
        sec = hour % 100;
        hour /= 100;
        minute = hour % 100;
        hour /= 100;
        tim->userAbsMinutes = 1;
        tim->fixunit = SECONDS;
        tim->userAbsSeconds = 1;
        if (minute > 59 || sec > 59)
          {
            panic("garbled time: minute/hour > 59");
            return(-1);
          }
        ret = 0;
        if (!TXparsetimeGetSecondsFraction(sc, tim)) return(-1);
        token(sc);                      /* KNG 010620 */
      }

    /*
     * check if an AM or PM specifier was given
     */
    tm->tm_hour = hour;
    tm->tm_min = minute;
    tm->tm_sec = sec;           /* JMT Wed Nov 23 11:41:34 EST 1994 */
    tm->tm_isdst = -1;                          /* see `tm_isdst changes' */
    if (sc->sc_tokid == AM || sc->sc_tokid == PM) {
        fixampm(tim, sc->sc_tokid);
        token(sc);
    }
    if (hour > 23){
        panic("garbled time: hour>23");
        return(-1);
    }

    if (tm->tm_hour == 24) {
        tm->tm_hour = 0;
        tm->tm_mday++;
    }

chkgmt:
    if (TXparsetimeParseTimezoneIfPresent(sc, tim))
      token(sc);

    tim->didtod++;
    return(ret);
}                                                              /* tod */

/**********************************************************************/

/*
 * assign_date() assigns a date, wrapping to next year if needed
 */
static int
assign_date(info, mday, mon, year)
TIME    *info;
long mday, mon, year;
{
  struct tm     *tm = &info->tm;

    if (year>=0 && year < 70) year += 2000; /* MAW 08-22-00 - year>=0 */
    if (year > 99) {
        if (year > 1899)
            year -= 1900;
        else{
            panic("garbled time: year<1900");
            return(-1);
        }
    }

#ifdef NEVER /* We are not at.  Keep it this year */
    if (year < 0 &&
        ((long)tm->tm_mon > mon ||
        ((long)tm->tm_mon == mon && (long)tm->tm_mday > mday)))
        year = tm->tm_year + 1;
#endif

    tm->tm_mday = (int)mday;
    tm->tm_mon = (int)mon;
    tm->tm_isdst = -1;                          /* see `tm_isdst changes' */
    if (year >= 0)
        tm->tm_year = (int)year;
    return(0);
}                                                      /* assign_date */

/**********************************************************************/

/*
 * month() picks apart a month specification
 *
 *  /[<month> NUMBER [NUMBER]]           \
 *  |[TOMORROW]                          |
 *  |NUMBER [SLASH NUMBER [SLASH NUMBER]]|
 *  \PLUS NUMBER MINUTES|HOURS|DAYS|WEEKS/
 */
static int month ARGS((SCI *sc, TIME *tim));
static int
month(sc, tim)
SCI *sc;
TIME    *tim;
/* Returns -1 on error, 0 if ok, 1 if nothing parsed.
 */
{
struct tm     *tm;
long year= (-1);
long mday = 1, mon = 0;
size_t  tlen;
SYM sep = SYM_UNKNOWN, sep2 = SYM_UNKNOWN, sym;
const char    *sav;

  tm = &tim->tm;

  if (sc->sc_tokid == COMMA) token(sc);         /* Thu, 09 Jul 1998 */

    switch (sc->sc_tokid) {
    case PLUS:
      if (TxParsetimeRFC1123Only) return(-1);
      if (plusminus(sc, tim, 1) < 0) return(-1);
            break;
    case MINUS:
      if (TxParsetimeRFC1123Only) return(-1);
      if (plusminus(sc, tim, -1) < 0) return(-1);
            break;
    case TOMORROW:
      if (TxParsetimeRFC1123Only) return(-1);
            tm->tm_mday++;
            tim->fixunit = DAYS;
            tm->tm_isdst = -1;                  /* see `tm_isdst changes' */
            token(sc);
            break;
    case YESTERDAY:
      if (TxParsetimeRFC1123Only) return(-1);
            tm->tm_mday--;
            tim->fixunit = DAYS;
            tm->tm_isdst = -1;                  /* see `tm_isdst changes' */
            token(sc);
            break;
    case TODAY: /* force ourselves to stay in today - no further processing */
      if (TxParsetimeRFC1123Only) return(-1);
            tim->fixunit = DAYS;
            token(sc);
            break;
    case SUN: case MON: case TUE: case WED: case THU: case FRI: case SAT:
            tim->fixunit = DAYS;
            tm->tm_mday += (sc->sc_tokid - SUN) - tm->tm_wday;
            tm->tm_isdst = -1;                  /* see `tm_isdst changes' */
            token(sc);                          /* KNG 010621 */
            break;
    case JAN: case FEB: case MAR: case APR: case MAY: case JUN:
    case JUL: case AUG: case SEP: case OCT: case NOV: case DEC:
            /*
             * do month [mday] [year]
             */
            tim->fixunit = MONTHS;
            mon = (sc->sc_tokid-JAN);
            switch (token(sc))
              {
              case SYM_NUMBER:                  /* mday or year */
                mday = atol(sc->sc_token);
              mdy:
                tim->fixunit = DAYS;
              mdy2:
                sav = save_scanner(sc);
                if (token(sc) == SYM_NUMBER)    /* year or hour */
                  {
                    if (((sym = token(sc)) == DOT || /*mar 8 12{:}34:56 1998*/
                         sym == COLON) &&
                        /* not `26/Jan/2012{:}23:54:03 -0500': */
                        sep2 == SYM_UNKNOWN)
                      {
                        reset_scanner(sc, sav);
                        token(sc);
                        if (tod(sc, tim) < 0) return(-1);
                      }
                    else
                      {
                        reset_scanner(sc, sav);
                        token(sc);
                      }
                    /* note: could be a TZ we don't know... */
                    if (sc->sc_tokid != TEOF) year = atol(sc->sc_token);
                    token(sc);
                    /* skip `{:}' in `26/Jan/2012{:}23:54:55 -0500': */
                    if (sep2 != SYM_UNKNOWN && sc->sc_tokid == COLON)
                      token(sc);
                  }
                else if (sc->sc_tokid == MINUS ||/* cookie Thu, 09-Jul-1998 */
                         sc->sc_tokid == COMMA || /* December 22, 1998 */
                            /* transfer.log `26/Jan{/}2012:23:54:03 -0500': */
                         sc->sc_tokid == SLASH)
                  {
                    sep2 = sc->sc_tokid;
                    goto mdy2;
                  }
                else if (mday > 50)             /* > 50: prob. year not day */
                  {
                    tim->fixunit = MONTHS;
                    year = mday;
                    mday = 1;
                  }
                break;
              case TEOF:                        /* e.g. month alone */
                mday = 1;
                break;
              default:
                plonk(sc, sc->sc_tokid);
                return(-1);
              }
            if (assign_date(tim, mday, mon, year) < 0)
                return(-1);
            break;
    case SYM_NUMBER:
            /*
             * get numeric MMDDYY, mm/dd/yy, dd.mm.yy
             * or dd MONTH yyyy, dd-MONTH-yyyy
             */
            tlen = strlen(sc->sc_token);
            mon = atol(sc->sc_token);
            tim->fixunit = MONTHS;
            token(sc);

            switch (sc->sc_tokid)
              {
              case SLASH:
              case DOT:
	      case COLON:
              case MINUS:        /* cookie date: Thu, 09-Jul-1998 */
                sep = sc->sc_tokid;
                switch (token(sc))
                  {
                  case JAN: case FEB: case MAR: case APR: case MAY: case JUN:
                  case JUL: case AUG: case SEP: case OCT: case NOV: case DEC:
                    goto namedmonth;
                  case SYM_NUMBER: break;
                  default:
                    plonk(sc, sc->sc_tokid);
                    return(-1);
                  }
                mday = atol(sc->sc_token);
                tim->fixunit = DAYS;
                if (token(sc) == sep) {
                    if(expect(sc,SYM_NUMBER)<0) return(-1);
                    year = atol(sc->sc_token);
                    token(sc);
                }

                /*
                 * flip months and days for european timing
                 */
                if (sep == DOT || sep == COLON ||
                    (mon > 12 && mday <= 12))
		{
                    long x = mday;
                    mday = mon;
                    mon = x;
                }
                break;
              case JAN: case FEB: case MAR: case APR: case MAY: case JUN:
              case JUL: case AUG: case SEP: case OCT: case NOV: case DEC:
              namedmonth:
                mday = mon;
                mon = (sc->sc_tokid - JAN);
                goto mdy;
                break;
              default:
                if (tlen == 6 || tlen == 8) {
                  if (tlen == 8) {
                    year = (mon % 10000) - 1900;
                    mon /= 10000;
                  } else {
                    year = mon % 100;
                    mon /= 100;
                  }
                  mday = mon % 100;
                  mon /= 100;
                  tim->fixunit = DAYS;
                } else{
                  panic("garbled time: bad numeric date fmt");
                  return(-1);
                }
              }

            mon--;
            if (mon < 0 || mon > 11 || mday < 1 || mday > 31){
                panic("garbled time: bad month/day");
                return(-1);
            }

            if (assign_date(tim, mday, mon, year) < 0)
               return(-1);
            break;
    case TEOF:
	    return(1);                                  /* did nothing */
            break;
    default:
            panic("garbled time");
            return(-1);
            break;
    } /* case */
    return(0);
}                                                            /* month */

/**********************************************************************/

static int monthdays ARGS((int y,int m));
static int
monthdays(y,m)
int y,m;
{
static CONST char norm[12]={ 31,28,31,30,31,30,31,31,30,31,30,31 };
static CONST char leap[12]={ 31,29,31,30,31,30,31,31,30,31,30,31 };

   y+=1900;
   if((y%4==0 && y%100!=0) || y%400==0) return(leap[m]);
   return(norm[m]);
}                                                        /* monthdays */

/**********************************************************************/

static void fixmonth ARGS((TIME *tim));
static void
fixmonth(tim)                        /* adjust for mday over/underflow */
TIME    *tim;
{
  struct tm     *tm;
  int           md;

  tm = &tim->tm;
   if(tm->tm_mday<1){
     tm->tm_isdst = -1;                         /* see `tm_isdst changes' */
      while(tm->tm_mday<1){
         if(tm->tm_mon==0){
            tm->tm_mon=11;
            tm->tm_year--;
         }else{
            tm->tm_mon--;
         }
         tm->tm_mday+=monthdays(tm->tm_year,tm->tm_mon);
      }
   }else if(tm->tm_mday>28){
      while(tm->tm_mday>(md=monthdays(tm->tm_year,tm->tm_mon))){
        tm->tm_isdst = -1;                      /* see `tm_isdst changes' */
         /* If right after this we'll be doing "end of"/"start of" a
          * month, then don't advance the month:
          */
         if (tim->fix && tim->fixunit == MONTHS)
           {
             tm->tm_mday = md;
             break;
           }
         tm->tm_mon++;
         tm->tm_mday-=md;
         if(tm->tm_mon==12){
            tm->tm_year++;
            tm->tm_mon=0;
         }
      }
   }
}                                                         /* fixmonth */

/**********************************************************************/

static void fixtime ARGS((TIME *tim));
static void
fixtime(tim)                            /* fix to begin or end of day */
TIME    *tim;
{
  struct tm     *tm;

  tm = &tim->tm;
/*
    TEOF, ID, JUNK,
    MIDNIGHT, NOON, TEATIME,
    PM, AM, TOMORROW, YESTERDAY, TODAY, NOW,
    SECONDS, MINUTES, HOURS, DAYS, WEEKS, MONTHS, YEARS,
    NUMBER, PLUS, MINUS, DOT, SLASH,
    JAN, FEB, MAR, APR, MAY, JUN,
    JUL, AUG, SEP, OCT, NOV, DEC,
    SUN, MON, TUE, WED, THU, FRI, SAT
 */
  /* Note the extensive fall-through in these switches: */
  if (tim->fix > 0)                     /* "end of" */
    {
      switch (tim->fixunit)
        {
        case YEARS:     tm->tm_mon = 11;
        case MONTHS:    tm->tm_mday = monthdays(tm->tm_year, tm->tm_mon);
        case WEEKS:     /* note: +weeks fixed by caller */
        case DAYS:      tm->tm_hour = 23;
        case HOURS:     tm->tm_min = 59;
        case MINUTES:   tm->tm_sec = 59;
          tm->tm_isdst = -1;                    /* see `tm_isdst changes' */
          break;
        default:
          break;
        }
    }
  else if (tim->fix < 0)                /* "start of" */
    {
      switch (tim->fixunit)
        {
        case YEARS:     tm->tm_mon = 0;
        case MONTHS:    tm->tm_mday = 1;
        case WEEKS:     /* note: -weeks fixed by caller */
        case DAYS:      tm->tm_hour = 0;
        case HOURS:     tm->tm_min = 0;
        case MINUTES:   tm->tm_sec = 0;
          tm->tm_isdst = -1;                    /* see `tm_isdst changes' */
          break;
        default:
          break;
        }
    }
  /* else nothing to do */
}                                                          /* fixtime */

/**********************************************************************/

static const char *
skipOptionalOf(const char *buf, const char *bufEnd)
/* Skips an optional ` of ' at `buf'.
 */
{
  while (buf < bufEnd && TX_ISSPACE(*buf)) buf++;
  if (bufEnd - buf >= 3 &&
      strnicmp(buf, "of", 2) == 0 &&
      TX_ISSPACE(buf[2]))
    {
      buf+=3;
      while (buf < bufEnd && TX_ISSPACE(*buf)) buf++;
   }
   return(buf);
}

/**********************************************************************/

/* Global functions */

/* wtf this should be part of TX?, and/or passed to parsetime():  KNG 970326 */
static time_t Prevtimer = (time_t)0;
static time_t Prevruntimer = (time_t)0;
#define PBSZ 80
static int      TxParsetimeUsePrevbuf = 1;
static char   Prevbuf[PBSZ] = "";
static size_t   PrevbufUsedSz = (size_t)(-1);   /* -1 == `Prevbuf' unset */

int
TXresettimecache()
/* Resets parsetime()'s recently-parsed-time cache.  Returns 0 on error.
 */
{
  Prevtimer = Prevruntimer = (time_t)0;
  Prevbuf[0] = '\0';
  PrevbufUsedSz = (size_t)(-1);                 /* -1 == `Prevbuf' unset */
  return(1);
}

time_t
TXindparsetime(const char *buf, size_t bufSz, int flags, TXPMBUF *pmbuf)
/* Parses time, without using or affecting cache.  Prints error messages
 * only if (flags & 1).  If (flags & 2), does strict parsing.
 * If (flags & 4), only allows RFC 1123 style dates.
 */
{
  time_t        s1, s2, ret;
  int           s3, s4, s5, s6;
  TXPMBUF       *savepmbuf;

  s1 = Prevtimer;
  s2 = Prevruntimer;
  s3 = TxParsetimeUsePrevbuf;
  s4 = TxParsetimeMesg;
  s5 = TxParsetimeStrict;
  s6 = TxParsetimeRFC1123Only;
  savepmbuf = TxParsetimePmbuf;
  TxParsetimeUsePrevbuf = 0;
  TxParsetimeMesg = (flags & 1);
  TxParsetimeStrict = (flags & 2);
  TxParsetimeRFC1123Only = (flags & 4);
  TxParsetimePmbuf = pmbuf;
  ret = parsetime(buf, bufSz);
  Prevtimer = s1;
  Prevruntimer = s2;
  TxParsetimeUsePrevbuf = s3;
  TxParsetimeMesg = s4;
  TxParsetimeStrict = s5;
  TxParsetimeRFC1123Only = s6;
  TxParsetimePmbuf = savepmbuf;
  return(ret);
}  

time_t
parsetime(const char *buf, size_t bufSz)
{
/*
 * Do the argument parsing, die if necessary, and return the time the job
 * should be run.
 */
  time_t nowtimer, runtimer, tmpt;
  int   wasDst;
struct tm nowtime, ptime, reqtm, tt, *tp;
TIME    runtime;        /* KNG 970326 */
#if !defined(EPI_HAVE_TM_GMTOFF) && defined(EPI_HAVE_TIMEZONE_VAR)
long    runtimezone;
#endif /* !EPI_HAVE_TM_GMTOFF && EPI_HAVE_TIMEZONE_VAR */
int hr = 0;  /* MUST be initialized to zero for midnight/noon/teatime */
int skipwday = 0, did, gmtofffix = 0;
SCI sci, *sc= &sci;
SYM     tok;
const char      *bufOrg = buf, *bufEnd;
#define BUFSZ_LEFT      (bufEnd - buf)

    nowtimer = time(NULL);
    if (bufSz == (size_t)(-1)) bufSz = strlen(buf);
    bufEnd = buf + bufSz;

    if(nowtimer<=Prevtimer+1 && TxParsetimeUsePrevbuf &&
       PrevbufUsedSz == bufSz &&
       memcmp(buf, Prevbuf, bufSz) == 0)
    {
       Prevtimer=nowtimer;
       return(Prevruntimer);
    }

    memset(&runtime, 0, sizeof(TIME));
    runtime.fixunit = TEOF;
    runtime.filetime = 0.0;
    runtime.stdGmtOff = runtime.dstGmtOff = EPI_OS_TIME_T_MAX;
    runtime.dstOff = 3600;                      /* may automagically update */

    tp = doLocalTime(&runtime, nowtimer);
    if (!tp)
      {
        panic("system can't process time");
        return(-1);
      }
    nowtime = *tp;
    runtimer = nowtimer;
    runtime.tm = nowtime;

    /* tm_isdst changes:  (label referenced elsewhere)
     * Two kinds of time changes we do: altering TIME.tm, and adding
     * seconds to a time_t (itself first computed from TIME.tm).
     * Sometimes we need to preserve TIME.tm.tm_isdst, sometimes not:
     *
     * TIME.tm changes:
     *   Clear tm_isdst before mktime():
     *     We are jumping to a new computed time; DST/STD is unknown
     *   No doDstStdCrossingFixup(): new (semi-)absolute computed time
     *
     * time_t deltas:
     *   Preserve tm_isdst:
     *     Needed to reproduce original time_t accurately
     *     (e.g. during DST -> STD overlap hour) before adding delta,
     *     and may be needed by later users.  Will be updated by
     *     localtime() after delta added
     *   doDstStdCrossingFixup(), if requested (depends on `fixunit')
     *
     * We do DST <-> STD crossing fixups ASAP after each time change,
     * not just once at the end, to avoid errors during later changes.
     * E.g. `start of next week' at 00:30 on DST -> STD day:
     * `next week' would give +Sat 23:30; needs fixup to +Sun 00:30
     * before doing `start', so `start' jump does not fall back to
     * Sun 00:00 on DST -> STD day.
     */

    while (BUFSZ_LEFT > 0 && TX_ISSPACE(*buf)) buf++;  /* before `begin' */
    if (BUFSZ_LEFT >= 5 &&
        (strnicmp(buf, "begin", 5) == 0 ||
         strnicmp(buf, "start", 5) == 0))         /* KNG 970326 */
      {
        if (TxParsetimeRFC1123Only) return(-1);
       runtime.fix= -1;
       buf+=5;
       if (BUFSZ_LEFT >= 4 && strnicmp(buf, "ning", 4) == 0) buf += 4;
       buf = skipOptionalOf(buf, bufEnd);
    } else if (BUFSZ_LEFT >= 3 && strnicmp(buf, "end", 3) == 0)
      {
        if (TxParsetimeRFC1123Only) return(-1);
       runtime.fix=1;
       buf+=3;
       buf = skipOptionalOf(buf, bufEnd);
    }
    if (init_scanner(sc, buf, bufEnd) < 0)
       return(-1);

again:
    tok = token(sc);
    if (tok == NOW)
      {
        if (TxParsetimeRFC1123Only) goto fbail;
        switch (tok = token(sc))
          {
          case PLUS:
          case MINUS:
            break;
          case TEOF:
            tok = NOW;
            break;
          default:
          pbail:
            plonk(sc, sc->sc_tokid);
          fbail:
	    sc->sc_token = TXfree(sc->sc_token);
            return(-1);
          }
      }
    switch (tok) {
    case NOW:
      runtime.gotDirectDelta = 1;
      runtime.directDelta = 0;
            break;
    case SYM_NULL:      /* KNG 000609 for symmetry with printing (copydb) */
            runtimer = (time_t)0;
            goto fin;
    case PLUS:
      if (TxParsetimeRFC1123Only) goto fbail;
      if (plusminus(sc, &runtime, 1) < 0) goto fbail;
            break;
    case MINUS:
      if (TxParsetimeRFC1123Only) goto fbail;
      if (plusminus(sc, &runtime, -1) < 0) goto fbail;
            break;
    case SYM_THIS:              /* KNG 970327 */
      if (TxParsetimeRFC1123Only) goto fbail;
            if (plusminus(sc, &runtime, 0) < 0) goto fbail;
            break;

    case SUN:
    case MON:
    case TUE:
    case WED:
    case THU:
    case FRI:
    case SAT:
      if (skipwday) goto pbail;
      skipwday++;
      /* Either Thu 12:34:56, or Thu[,] 09[-]Jul[-]1998; check first: */
      while ((tok = token(sc)) != TEOF)
        switch (tok)
          {
          case JAN: case FEB: case MAR: case APR: case MAY: case JUN:
          case JUL: case AUG: case SEP: case OCT: case NOV: case DEC:
            skipwday++;
            reset_scanner(sc, CHARPN);
            token(sc);                  /* skip the leading weekday */
            goto again;
          default: break;
          }
      reset_scanner(sc, CHARPN);
      token(sc);
      goto def;

    case SYM_NUMBER:
            if(ymdhms(sc,&runtime)==0)          /* success */
              {
                if (TxParsetimeRFC1123Only) goto fbail;
                break;
              }
            reset_scanner(sc, CHARPN);
            if (skipwday == 2) token(sc);
            runtime.fixunit = TEOF;
            token(sc);
            if(month(sc,&runtime)<0 ||
               tod(sc,&runtime)<0
            )
              goto fbail;
            break;

            /*
             * evil coding for TEATIME|NOON|MIDNIGHT - we've initialised
             * hr to zero up above, then fall into this case in such a
             * way so we add +12 +4 hours to it for teatime, +12 hours
             * to it for noon, and nothing at all for midnight, then
             * set our runtime to that hour before leaping into the
             * month scanner
             */
    case TEATIME:
      if (TxParsetimeRFC1123Only) goto fbail;
            hr += 4;
    case NOON:
      if (TxParsetimeRFC1123Only) goto fbail;
            hr += 12;
    case MIDNIGHT:
      if (TxParsetimeRFC1123Only) goto fbail;
/*
            if (runtime.tm_hour >= hr)
                runtime.tm_mday++;
*/
            runtime.tm.tm_hour = hr;
            runtime.tm.tm_min = 0;
            runtime.tm.tm_sec = 0; /* JMT Set seconds to 0 as well */
            runtime.fixunit = HOURS;    /* WAG  KNG 970326 */
            runtime.tm.tm_isdst = -1;           /* see `tm_isdst changes' */
            runtime.userAbsHours = runtime.userAbsMinutes =
              runtime.userAbsSeconds = 1;
            token(sc);                 /* read next token for month() */
	    if(month(sc,&runtime)<0) goto fbail;
            break;
    default:
    def:
            did = 0;
	    switch(month(sc,&runtime))
              {
              case -1:  goto fbail;
              case 0:   did = 1;
              }
            switch (tod(sc,&runtime))
              {
              case -1:  goto fbail;
              case 0:   did = 1;
              }
            if (!did && TxParsetimeStrict)	/* nothing parsed */
              {
                sc->sc_tokid = SYM_UNKNOWN;  /* force "garbled" not "incomplete" */
                goto pbail;
              }
            break;
    } /* ugly case statement */

    /* Apply `directDelta': */
    if (runtime.gotDirectDelta)
      {
        struct tm       *tm;

        runtimer = (time_t)((EPI_HUGEINT)runtimer + runtime.directDelta);
        /* Re-obtain runtime.tm fields, for possible `runtime.fix' below: */
        tm = doLocalTime(&runtime, runtimer);
        if (!tm)
          {
            panic("localtime() failed");
            return(-1);
          }
        runtime.tm = *tm;
      }

    /* check for GMT     KNG 010620 */
    tok = token(sc);
    if (!TXparsetimeParseTimezoneIfPresent(sc, &runtime) &&
        /* `2018-10-01 12:34_56', `...34_x', `...34_' invalid: */
        /*xxxxxxxxxxxxxxx or check sc->sc_tokid?:*/
        tok != TEOF)
      goto pbail;

    if(expect(sc,TEOF)<0) goto fbail;

    fixmonth(&runtime);
    fixtime(&runtime);			/* align for "start/end of" */

    runtime.tm.tm_wday = runtime.tm.tm_yday = -1;
    /* preserve `tm_isdst', per `tm_isdst changes' comment above */
#if defined(EPI_HAVE_TM_GMTOFF)
    runtime.tm.tm_gmtoff = -1;          /* make sure mktime() sets it */
#elif defined(EPI_HAVE_TIMEZONE_VAR)    /* wtf not thread safe */
#  ifndef _WIN32                        /* already set; breaks mktime */
    timezone = -1L;                     /* make sure mktime() sets it */
#  endif /* !_WIN32 */
#endif /* !EPI_HAVE_TM_GMTOFF && !EPI_HAVE_TIMEZONE_VAR */
    reqtm = runtime.tm;                 /* save before mktime() mods */
    /* still need to do mktime() even if `runtime.gotDirectDelta', to
     * get timezone?
     */
    runtimer = doMkTime(&runtime);
#if !defined(EPI_HAVE_TM_GMTOFF) && defined(EPI_HAVE_TIMEZONE_VAR)
    runtimezone = timezone;
#endif /* !EPI_HAVE_TM_GMTOFF && EPI_HAVE_TIMEZONE_VAR */

    /* doDstStdCrossingFixup() was handled (if needed) earlier; see
     * `tm_isdst changes' comment.  But might be called below for WEEKS.
     */

    ptime = runtime.tm;
    /* KNG 20070319 some systems (i686-unknown-linux2.6.17-64-32) return
     * MAXINT for out-of-range-high, -MAXINT for out-of-range-low.
     * KNG 20071106 but some do not (i686-unknown-linux2.6.22.7-64-32),
     * and MAXINT could be legit; check with localtime() and compare `tm's:
     */
    if (runtimer == ~((time_t)1 << (EPI_OS_TIME_T_BITS - 1)) ||
        runtimer == ((time_t)1 << (EPI_OS_TIME_T_BITS - 1)))
      {                                         /* could be out-of-range hi */
        /* If we get the same broken-out struct tm values from localtime()
         * as our request has, then trust `runtimer'.  Otherwise set invalid:
         */
        tmpt = runtimer;
        if ((tp = doLocalTime(&runtime, tmpt)) == (struct tm *)NULL)
          runtimer = (time_t)(-1);              /* invalid */
        else
          {
            tt = *tp;
            if (tt.tm_sec != reqtm.tm_sec ||
                tt.tm_min != reqtm.tm_min ||
                tt.tm_hour != reqtm.tm_hour ||
                tt.tm_mday != reqtm.tm_mday ||
                tt.tm_mon != reqtm.tm_mon ||
                tt.tm_year != reqtm.tm_year ||
                /* Check mktime()'s value for tm_isdst/tm_gmtoff instead
                 * of `reqtm', since we did not set them in our request:
                 */
                tt.tm_isdst != ptime.tm_isdst
#ifdef EPI_HAVE_TM_GMTOFF
                || tt.tm_gmtoff != ptime.tm_gmtoff
#endif /* EPI_HAVE_TM_GMTOFF */
                )
              runtimer = (time_t)(-1);          /* invalid */
          }
      }
    /* KNG 20060321 OSX mktime() is strict about invalid HH:MM times
     * 1 hour after STD -> DST shift (e.g. 02:30) and returns error
     * instead of a +/-1 hour DST/STD time.  Attempt to detect:
     */
    if (runtimer == (time_t)(-1) && runtime.tm.tm_hour < 23 &&
        runtime.userAbsHours /* wtf? could happen even if no user hours? */)
        {
          tt = runtime.tm;
          tt.tm_hour++;
          tmpt = mktime(&tt);
          if (tmpt == ~((time_t)1 << (EPI_OS_TIME_T_BITS - 1)) ||
              tmpt == ((time_t)1 << (EPI_OS_TIME_T_BITS - 1)))
            tmpt = (time_t)(-1);
          if (tmpt != (time_t)(-1) && tt.tm_isdst > 0)
            {
              /* 1 hour later is valid and DST: requested time was
               * probably in the STD -> DST invalid area.  We can
               * fix the DST issue below during STD -> DST area check:
               */
              runtime.tm = tt;
              /* WTF what if `runtime.dstOff' ends up != 3600? */
              runtime.tm.tm_hour--;             /* correct for our skip */
              runtimer = tmpt - 3600;           /* "" */
#if !defined(EPI_HAVE_TM_GMTOFF) && defined(EPI_HAVE_TIMEZONE_VAR)
              runtimezone = timezone;
#endif /* !EPI_HAVE_TM_GMTOFF && EPI_HAVE_TIMEZONE_VAR */
              ptime = runtime.tm;
            }
        }

    /* KNG 20070319 do not attempt any fixups to `runtimer' if invalid: */
    if (runtimer == (time_t)(-1)) goto fin;

    /* Detect and fix times that are in the STD -> DST or DST -> STD
     * invalid/ambiguous areas; see descriptions below.  These fixes
     * only apply if absolute hours were specifically given (e.g. `02:30'
     * not `-2 hours'; otherwise the hours came from `nowtimer' which
     * is always legal/unambiguous:
     */
    if (runtime.userAbsHours /* wtf userAbsMinutes, if STD/DST shift <1h? */)
      {
        /* For 1 hour after STD -> DST change, 02:00:00 - 02:59:59 is
         * invalid (without a timezone), because clock skips it.
         * Various platforms' mktime() either skip forward to DST,
         * back to STD, or return error.  We can detect the first two
         * cases because the hour will be +/-1 from our original request
         * (assuming mktime() fixes it up); we map the last case (error
         * e.g. OSX) to the first case above.  If detected, yap, and assume
         * trailing timezone was meant (STD).  WTF we assume DST = STD + 1h
         * and that original request values are each in their proper range:
         */
        if (runtime.tm.tm_hour ==
            reqtm.tm_hour + (runtime.tm.tm_isdst > 0 ? 1 : -1) &&
            runtime.tm.tm_min == reqtm.tm_min &&
            runtime.tm.tm_sec == reqtm.tm_sec &&
            runtime.tm.tm_mday == reqtm.tm_mday &&
            runtime.tm.tm_mon == reqtm.tm_mon &&
            runtime.tm.tm_year == reqtm.tm_year)
          {                                     /* STD -> DST invalid area */
            /* Even though we're assuming the request was in standard time,
             * because it's beyond the changeover, when canonicalized it
             * must print as DST.  Thus roll forward to DST if we got STD:
             */
            if (runtime.tm.tm_isdst == 0)       /* got a STD time */
              {
                /* WTF what if `runtime.dstOff' ends up != 3600? */
                runtimer += 3600;
                /* Optimization: avoid localtime(), just inc fields direct: */
                runtime.tm.tm_hour += 2;        /* 1 real + 1 STD -> DST */
                runtime.tm.tm_isdst = 1;
#if defined(EPI_HAVE_TM_GMTOFF)
                runtime.tm.tm_gmtoff += 3600;
#elif defined(EPI_HAVE_TIMEZONE_VAR)
                runtimezone = timezone;
#endif /* !EPI_HAVE_TM_GMTOFF && EPI_HAVE_TIMEZONE_VAR */
              }
            /* If they did not give a specific STD or DST timezone to
             * disambiguate below, tell them:
             */
            if (runtime.reqgmtoff == 0)         /* no timezone in request */
              {
                txpmbuf_putmsg(TxParsetimePmbuf, MWARN + UGE, __FUNCTION__,
                               "Invalid local time `%.*s': during standard-to-daylight time change; assuming standard",
                               (int)(bufEnd - buf), buf);
              }
            /* Because we cannot represent the original HH:MM in either zone,
             * the requested-zone fixup below will be off by an hour either
             * way (depending on which zone we picked above).  Note for later:
             */
             gmtofffix = -3600;                 /* we picked DST above */
          }
        else                                    /* check DST -> STD area */
          {
            /* For +/-1 hour around DST -> STD change, HH:MM is ambiguous:
             * e.g. `01:30' could be DST (before change) or STD (after change).
             * Various platforms' mktime()s return one or the other.  Prefer
             * the former for consistency (trailing zone); map the latter to
             * the former, and warn (for either) that it is ambiguous.
             * Determine which case applies (if any) by adding (if DST)
             * or subtracting (if STD) 1 hour and re-localtime()ing: if we
             * get the same Y/M/D/H/M/S numbers but different `tm_isdst',
             * we are in the overlap area.
             */
            /* WTF what if `runtime.dstOff' ends up != 3600? */
            tmpt = runtimer + (runtime.tm.tm_isdst > 0 ? 3600 : -3600);
            /* Do not fail if localtime() fails: `tmpt' could have ended up
             * in an invalid area (e.g. 02:30 on STD -> DST changeover):
             */
            if ((tp = doLocalTime(&runtime, tmpt)) != (struct tm *)NULL)
              tt = *tp;
            if (tp != (struct tm *)NULL &&
                tt.tm_hour == runtime.tm.tm_hour &&
                tt.tm_isdst != runtime.tm.tm_isdst &&
                tt.tm_min == runtime.tm.tm_min &&
                tt.tm_sec == runtime.tm.tm_sec &&
                tt.tm_year == runtime.tm.tm_year &&
                tt.tm_mon == runtime.tm.tm_mon &&
                tt.tm_mday == runtime.tm.tm_mday)
              {                                 /* DST -> STD overlap area */
                /* Roll back to DST if we originally got STD: */
                if (runtime.tm.tm_isdst == 0)
                  {
                    /* WTF what if `runtime.dstOff' ends up != 3600? */
                    runtimer -= 3600;
                    runtime.tm = tt;
#if !defined(EPI_HAVE_TM_GMTOFF) && defined(EPI_HAVE_TIMEZONE_VAR)
                    runtimezone = timezone;
#endif /* !EPI_HAVE_TM_GMTOFF && EPI_HAVE_TIMEZONE_VAR */
                  }
                /* If they did not give a specific STD or DST timezone
                 * to disambiguate below, tell them:
                 */
                if (runtime.reqgmtoff == 0)     /* no timezone in request */
                  txpmbuf_putmsg(TxParsetimePmbuf, MWARN + UGE, __FUNCTION__,
                                 "Ambiguous local time `%.*s': during daylight-to-standard time change; assuming daylight",
                                 (int)(bufEnd - buf), buf);
              }
          }
      }

    if (runtime.gotFiletime)                    /* have a FILETIME value */
      {
        runtimer = (time_t)runtime.filetime;    /* use the FILETIME */
        goto replaceTm;
      }
    else if (runtime.reqgmtoff != 0)            /* requested specific zone */
      {
        /* First shift `runtimer' to GMT, i.e. pretend local zone was GMT
         * during the mktime() above, by adding local GMT offset:
         */
#if defined(EPI_HAVE_TM_GMTOFF)
        if (runtime.tm.tm_gmtoff != -1)
          runtimer += runtime.tm.tm_gmtoff;
        else
#elif defined(EPI_HAVE_TIMEZONE_VAR)
        if (runtimezone != -1)
          runtimer -= runtimezone - (runtime.tm.tm_isdst > 0 ?
                                     runtime.dstOff : 0);
        else
#endif /* !EPI_HAVE_TM_GMTOFF && !EPI_HAVE_TIMEZONE_VAR */
          {
            tmpt = 900000000;           /* Thu Jul 09 16:00:00 GMT 1998 */
            if ((tp = doLocalTime(&runtime, tmpt)) == (struct tm *)NULL)
            {
               panic("system can't process time");
               return(-1);
            }
            tt = *tp;
            runtimer -= ((9*86400 + 16*3600) - (tt.tm_mday*86400 +
              tt.tm_hour*3600 + tt.tm_min*60 + tt.tm_sec - (tt.tm_isdst > 0 ?
                runtime.dstOff : 0))) -
              (runtime.tm.tm_isdst > 0 ? runtime.dstOff : 0);
          }
        runtimer += gmtofffix;                  /* see above */
        /* Now shift `runtimer' to the requested zone: */
        if (runtime.reqgmtoff != 1) runtimer -= runtime.reqgmtoff;
      replaceTm:
        /* Now replace local `struct tm' values for the shifted time: */
        if ((tp = doLocalTime(&runtime, runtimer)) == (struct tm *)NULL)
        {
           panic("system can't process time");
           return(-1);
        }
        runtime.tm = *tp;
      }

    /* `begin/end of' case that fixtime() doesn't handle:  KNG 970327 */
    switch (runtime.fixunit)
      {
      case WEEKS:
        if (TxParsetimeRFC1123Only) goto fbail;
	if (!runtime.fix) break;
        /* do start/end of: */
        wasDst = runtime.tm.tm_isdst;
	if (runtime.fix > 0)
	  runtimer += 86400*(6 - ptime.tm_wday);
	else if (runtime.fix < 0)
	  runtimer -= 86400*ptime.tm_wday;
        if ((tp = doLocalTime(&runtime, runtimer)) == (struct tm *)NULL)
        {
           panic("system can't process time");
           return(-1);
        }
	runtime.tm = *tp;
        /* Do DST <-> STD crossing fixup ASAP; see `tm_isdst changes' */
        /* Bug 170 `start of this week' 1-6 days after DST -> STD: */
        doDstStdCrossingFixup(&runtime, &runtimer, wasDst);
	break;
      case HOURS:
      case MINUTES:
      case SECONDS:
	break;
      case DAYS:
        break;
      default:
        if (TxParsetimeRFC1123Only) goto fbail;
	break;
      }

fin:
    sc->sc_token = TXfree(sc->sc_token);
    if (TxParsetimeUsePrevbuf && bufSz < PBSZ)
      {
        memcpy(Prevbuf, bufOrg, bufSz);
        Prevbuf[bufSz] = '\0';
        PrevbufUsedSz = bufSz;
      }
    Prevtimer=nowtimer;
    Prevruntimer=runtimer;
    return runtimer;
}                                                        /* parsetime */

#ifdef TEST
/**********************************************************************/
EPIPUTMSG()
/*{*/
   printf("%03d ",n);
   vprintf(fmt,args);
   if(fn!=(char *)NULL)
      printf(" in the function: %s", fn);
   putchar('\n');
   va_end(args);
   return(0);
}                                                     /* end putmsg() */
/**********************************************************************/

static char *prtim ARGS((time_t tim));
static char *
prtim(tim)
time_t  tim;
{
  static char	buf[128];
  struct tm	t, *tp;

  if((tp=localtime(&tim))==(struct tm *)NULL)         /* MAW 03-03-03 */
  {
     panic("system can't process time");
     return("");
  }
  t = *tp;
  strftime(buf, sizeof(buf), "%a %b %d %Y %H:%M:%S %Z", &t);
  return(buf);
}

static void
printDiff(time_t sec)
{
  time_t        days, hours, minutes, seconds, orgSec = sec;
  char          *signStr;

  if (sec < (time_t)0)
    {
      signStr = "-";
      sec = -sec;
    }
  else
    signStr = "";
  days = sec/86400;
  sec %= 86400;
  hours = sec/3600;
  sec %= 3600;
  minutes = sec/60;
  sec %= 60;
  seconds = sec;
  htpf("%kwd seconds (%s%kwd 24h-days %02d:%02d:%02d) from now",
       (EPI_HUGEINT)orgSec, signStr, (EPI_HUGEINT)days,
       hours, minutes, seconds);
}  

/**********************************************************************/
int
main(int argc, char **argv)
{
  time_t t, n;
  int echo=0;
  size_t i;
  char  buf[1024];
  char  *lineBuf;

  for (i = 0; i < NSPECIALS - 1; i++)
    if (strcmpi(specials[i].name, specials[i+1].name) >= 0)
      {
        printf("*** token list unsorted: item `%s' >= item `%s' ***\n",
               specials[i].name, specials[i+1].name);
        exit(1);
      }

  for (i = 0; i < NUM_TIMEZONE_SPECIALS - 1; i++)
    if (strcmpi(TimezoneSpecials[i].name, TimezoneSpecials[i+1].name) >= 0)
      {
        printf("*** token list unsorted: item `%s' >= item `%s' ***\n",
               TimezoneSpecials[i].name, TimezoneSpecials[i+1].name);
        exit(1);
      }

   for(--argc,++argv;argc>0 && (**argv=='/' || **argv == '-');argc--,argv++){
      switch(argv[0][1]){
      case 'e': echo=1; continue;
      case 's': TxParsetimeStrict = !TxParsetimeStrict; continue;
      case 'r': TxParsetimeRFC1123Only = !TxParsetimeRFC1123Only; continue;
      default: /* could be "-1 week" */ break;
      }
      break;
   }
   if(argc==0){
#ifdef EPI_HAVE_GNU_READLINE
     while ((lineBuf = readline("date> ")) != CHARPN) {
       add_history(lineBuf);
#else /* !EPI_HAVE_GNU_READLINE */
     while(fgets(buf, sizeof(buf), stdin)!=(char *)NULL){
       char *s;
       s = buf + strcspn(buf, "\r\n");
       if (*s == '\0') fprintf(stderr, "Error: line too long\n");
       *s = '\0';
       lineBuf = buf;
#endif /* !EPI_HAVE_GNU_READLINE */
       if(echo) puts(lineBuf);
         time(&n);
         printf("now:  %10ld = %s\n",(long)n,prtim(n));
         t=parsetime(lineBuf, -1);
         if (t == (time_t)(-1)) printf("then: invalid\n");
         else
           {
             printf("then: %10ld = %s\n", (long)t, prtim(t));
             printDiff(t - n);
             printf("\n");
           }
#ifdef EPI_HAVE_GNU_READLINE
         lineBuf = TXfree(lineBuf);
#endif /* EPI_HAVE_GNU_READLINE */
      }
     printf("\n");
   }else{
      *buf = '\0';
      for(;argc>0;argc--,argv++){
         strcat(buf, *argv);
         if (argc > 1) strcat(buf, " ");
      }
      if(echo) puts(buf);
      time(&n);
      printf("now:  %10ld = %s\n",(long)n,prtim(n));
      t=parsetime(buf, -1);
      printf("then: %10ld = %s\n",(long)t,prtim(t));
      printDiff(t - n);
      printf("\n");
   }
   exit(0);
   return(0);
}
/**********************************************************************/
#endif
