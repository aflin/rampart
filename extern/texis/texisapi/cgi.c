#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <time.h>
#ifdef _WIN32
#  include <io.h>
#else
#  include <sys/time.h>
#endif

#ifdef EPI_HAVE_STDARG
#  include <stdarg.h>
#else
#  include <varargs.h>
#endif
#include "cgi.h"
#include "http.h"
#include "httpi.h"
#include "texint.h"             /* for _environ */

#if defined(i386) && !defined(__linux__) && !defined(__bsdi__) && !defined(__FreeBSD__)
/* MAW 03-19-96 - esix-unixware compat */
#  define environ	_environ
#endif

#ifndef _WIN32
extern char	**environ;
#endif

/* ------------------------------ Config stuff --------------------------- */
/* see also cgi.h, makefile */

/* Define USERHACK to really set user/password when NCGDIRECT;
 * serveruser/pass() become no-ops:   -KNG 960515
 */
#define USERHACK	/* see also cgipasswd.c */

/* Define to terminate string lists with "" _and_ NULL, in case old
 * code has the >> BONUS FEATURE << of stopping at NULL:  -KNG 960108
 * (see BONUS_TERMINATE in other code too -KNG 960723)
 */
#define BONUS_TERMINATE
/* Define if atexit() exists: */
#if defined(__stdc__) || defined(__GNUC__)
#  define HAS_ATEXIT
#endif
/* Define to print "expires date" instead of "expires=date" in cookies,
 * since Windoze Netscape 2.0b6a (Win16; I) seems not to like them:
 * -KNG 960208
 */
#define NETSCAPE_EXPEQ_BUG

static CONST char       WhiteSpace[] = " \t\r\n";

/* ----------------------------- End config ------------------------------ */

#if defined(USERHACK) && defined(NCGDIRECT)
#  ifdef serveruser
#    undef serveruser
#    define serveruser(u)	((ncgugp[0] = strdup(u)) != CHARPN)
#  endif
#  ifdef serverpass
#    undef serverpass
#    define serverpass(p)	((ncgugp[2] = strdup(p)) != CHARPN)
#  endif
#endif

#ifdef BONUS_TERMINATE
#  define TERMSZ	2
#else
#  define TERMSZ	1
#endif

typedef struct CGIPRIV_tag {
  CGISL	sl[CGISL_NUM];		/* url, content, etc.  KNG 960108 */
} CGIPRIV;
#define CGIPRIVPN	((CGIPRIV *)NULL)

/* Internal CGI flags: */
typedef enum CF_tag
{
  CF_STREAD	= (1 << 0),	/* we've read the state vars */
  CF_STREADOK	= (1 << 1),	/* state read was successful */
  CF_STDIRTY	= (1 << 2),	/* state vars changed */
  CF_PROCENV	= (1 << 3),	/* processed environment vars */
  CF_INHDRS	= (1 << 4),	/* between cgistart/endhdrs() */
  CF_COOKIEAGENT= (1 << 5),	/* set if browser supports cookies */
  CF_STFILE	= (1 << 6),	/* state vars go to file, not cookie */
  CF_STCOOKIEPR	= (1 << 7),	/* already printed state cookie as cookie */
  CF_DELSTATE	= (1 << 8),	/* delete/expire state cookie */
  CF_DIDHDRS    = (1 << 9),     /* if we're past headers */
  CF_ISPRE      = (1 <<10),     /* is opencgipre()-created struct */
  CF_URLDECODECOOKIEVALS = (1 << 11),   /* URL-decode cookie vals */
}
CF;

#define REALLOC(p, sz)	((p) ? realloc((p), (sz)) : malloc(sz))
#define NEXP	8
#define BUFEXP	512

static CONST char       OutOfMem[] = "Out of memory";

#define PUTMSG_MAERR(fn)	putmsg(MERR + MAE, fn, OutOfMem)

/************************************************************************/

static CGIS *releasecgis ARGS((CGIS *s));
static CGIS *
releasecgis(s)
CGIS	*s;
{
   if(s!=CGISPN)
   {
      if(s->s!=CHARPPN)
         free(s->s);
      /* free(s); */	/* part of array, don't free  -KNG 960103 */
      s->s = CHARPPN;
      if (s->len != SIZE_TPN) free(s->len);
      s->len = SIZE_TPN;
      s->tag = CHARPN;
      s->tagLen = 0;
      s->n = 0;
   }
   return(CGISPN);
}

/************************************************************************/

static void cgislinit ARGS((CGISL *sl));
static void
cgislinit(sl)
CGISL	*sl;
{
  sl->cs = CGISPN;
  sl->buf = CHARPN;
  sl->n = sl->bufsz = sl->bufused = sl->privnum = 0;
  sl->cmp = strncmp;
}

void
TXcgislClear(sl)
CGISL	*sl;
{
  int		i;
  CGISLCMP	*cmp;

  if (sl->cs) {
    for (i = 0; i < sl->n; i++) releasecgis(sl->cs + i);
    free(sl->cs);
  }
  if (sl->buf != CHARPN)
    free(sl->buf);
  cmp = sl->cmp;
  cgislinit(sl);
  sl->cmp = cmp;	/* preserve comparison function */
}

CGISL *
closecgisl(sl)
CGISL	*sl;
{
  if (sl == CGISLPN) goto done;
  TXcgislClear(sl);
  free(sl);
 done:
  return(CGISLPN);
}

CGISL *
opencgisl()
{
  static CONST char     fn[] = "opencgisl";
  CGISL                 *sl;

  if ((sl = (CGISL *)calloc(1, sizeof(CGISL))) == CGISLPN) {
    PUTMSG_MAERR(fn);
    return(CGISLPN);
  }
  cgislinit(sl);
  return(sl);
}

int
cgislsetcmp(sl, cmp)
CGISL		*sl;
CGISLCMP	*cmp;
{
  if (cmp == CGISLCMPPN) cmp = strncmp;
  sl->cmp = cmp;
  return(1);
}

char *
cgislvar(sl, n, valp)
CGISL	*sl;
int	n;
char	***valp;
/* like cgivar(), for given CGISL.  Ignores privnum (assume no private vars).
 * WTF remove in favor of cgislvarsz().
 */
{
  CGIS  *cs;

  if ((unsigned)n >= (unsigned)sl->n) {
    *valp = CHARPPN;
    return(CHARPN);
  }
  cs = sl->cs + (unsigned)n;
  *valp = cs->s;
  return(cs->tag);
}

char *
cgislvarsz(sl, n, valp, szp)
CGISL	*sl;
int	n;
char	***valp;        /* (out, opt.) */
size_t  **szp;          /* (out, opt.) */
/* same as cgislvar(), with sizes.  WTF merge cgislvar() with this.
 */
{
  CGIS  *cs;

  if ((unsigned)n >= (unsigned)sl->n) {
    if (valp) *valp = CHARPPN;
    if (szp) *szp = SIZE_TPN;
    return(CHARPN);
  }
  cs = sl->cs + (unsigned)n;
  if (valp) *valp = cs->s;
  if (szp) *szp = cs->len;
  return(cs->tag);
}

char **
getcgisl(sl, name)
CGISL	*sl;
char	*name;
/* Returns list of values for variable `name' in `sl', or NULL if
 * not present.
 * Thread-safe and signal-safe iff `sl->cmp' is
 * (i.e. if cgislsetcmp() not called).
 */
{
  int	        i;
  size_t        nameLen;

  nameLen = strlen(name);
  for (i = 0; i < sl->n; i++) {
    if (sl->cs[i].tagLen == nameLen &&
        sl->cmp(sl->cs[i].tag, name, nameLen) == 0)
      return(sl->cs[i].s);
  }
  return(CHARPPN);
}

char **
TXcgislGetVarAndValues(sl, varName)
CGISL   *sl;            /* (in) CGISL to get var from */
char    **varName;      /* (in/out) variable name to find */
/* Returns list of values for variable `*varName' in `sl', or NULL if
 * not present.  Sets `*varName' to variable name found; caller must
 * copy it immediately.
 */
{
  int	        i;
  size_t        varNameLen;

  varNameLen = strlen(*varName);
  for (i = 0; i < sl->n; i++) {
    if (sl->cs[i].tagLen == varNameLen &&
        sl->cmp(sl->cs[i].tag, *varName, varNameLen) == 0)
      {
        /*`sl' value for variable name may differ from caller's, eg. if
         * case-insensitive `sl->cmp':
         */
        *varName = sl->cs[i].tag;
        return(sl->cs[i].s);
      }
  }
  /* not found: */
  *varName = CHARPN;
  return(CHARPPN);
}

/* ------------------------------------------------------------------------- */

static int addvar ARGS((CGISL *cl, CONST char *name, size_t nameLen,
                        CONST char *val, size_t sz, int priv, int lowerName));
static int
addvar(cl, name, nameLen, val, sz, priv, lowerName)
CGISL           *cl;
CONST char      *name;          /* (in) name of variable (no embedded nuls) */
size_t          nameLen;        /* (in) length of `name'; -1 for strlen() */
CONST char      *val;
size_t          sz;             /* size of `val', -1 for strlen() */
int             priv;
int             lowerName;
/* Does actual work of adding a tag/value to a string list.  Will be
 * private if `priv' is non-zero.  Returns number of values for `name'
 * (0 on error).
 */
{
  static CONST char     fn[] = "addvar";
  int		        v, req, i, j, vn;
  CGIS		        *cs;
  char		        *s, *oldbuf;
  char		        **sp;

  if (nameLen == (size_t)(-1)) nameLen = strlen(name);
  if (nameLen == 0)
    {
      putmsg(MERR + UGE, fn, "Invalid variable name `%.*s'",
             (int)nameLen, name);
      return(0);
    }
  if (sz == (size_t)(-1)) sz = strlen(val);
  /* The first `privnum' vars are considered private and not accessible
   * to the user; they are independent and may duplicate later vars:
   */
  if (priv) {
    i = 0;
    vn = cl->privnum;
  } else {
    i = cl->privnum;
    vn = cl->n;
  }
  for (v = i, cs = cl->cs + i; v < vn; v++, cs++) {
    if (cs->tagLen == nameLen &&
        cl->cmp(cs->tag, name, nameLen) == 0)
      break;                                    /* found */
  }
  req = sz + 1;                 /* need space at least for value */
  if (v == vn) {		/* new tag; realloc tag list if needed */
    if (cl->n % NEXP == 0 &&	/* must realloc */
	(cl->cs = (CGIS *)REALLOC(cl->cs, (cl->n+NEXP)*sizeof(CGIS))) ==CGISPN)
      goto merr;
    req += nameLen + 1;                         /* need space for tag too */
  }

  /* Realloc string buffer if needed, and update pointers: */
  if (cl->bufused + req > cl->bufsz) {	/* need to realloc */
    cl->bufsz += ((req + BUFEXP - 1)/BUFEXP)*BUFEXP;
    oldbuf = cl->buf;
    if ((cl->buf = (char *)REALLOC(cl->buf, cl->bufsz)) == CHARPN)
      goto merr;
    /* realign pointers if buffer pointer changed: */
    if (oldbuf != CHARPN && oldbuf != cl->buf) {
      for (i = 0, cs = cl->cs; i < cl->n; i++, cs++) {
	cs->tag = cl->buf + (cs->tag - oldbuf);
	for (j = 0; j < cs->n; j++)
	  cs->s[j] = cl->buf + (cs->s[j] - oldbuf);
      }
    }
  }

  /* Add the tag (if new): */
  cs = cl->cs + v;
  s = cl->buf + cl->bufused;
  if (v == vn) {		/* new tag */
    if (v != cl->n)		/* in middle of tag list; make room */
      memmove(cs + 1, cs, (cl->n - v)*sizeof(CGIS));
    memcpy(s, name, nameLen);
    s[nameLen] = '\0';
    if (lowerName) TXstrToLowerCase(s, nameLen);
    cs->tag = s;
    cs->tagLen = nameLen;
    cs->s = CHARPPN;		/* value added below */
    cs->len = SIZE_TPN;
    cs->n = 0;
    cl->n += 1;
    if (priv) cl->privnum++;
    s += nameLen + 1;
  }

  /* Add the value: */
  if (cs->n % NEXP == 0 &&	/* must realloc */
      (((cs->s = (char**)REALLOC(cs->s, (cs->n+TERMSZ+NEXP)*sizeof(char*))) ==
        CHARPPN) ||
       ((cs->len=(size_t*)REALLOC(cs->len,(cs->n+TERMSZ+NEXP)*sizeof(size_t)))
        == SIZE_TPN)))
    goto merr;
  memcpy(s, val, sz);		/* copy value string to end of buffer */
  s[sz] = '\0';
  cl->bufused += req;
  sp = cs->s + cs->n;
  cs->len[cs->n] = sz;
  cs->len[cs->n + 1] = 0;
  cs->n += 1;
  *(sp++) = s;
  *(sp++) = "";			/* terminate list */
#ifdef BONUS_TERMINATE
  *sp = CHARPN;
#endif
  return(cs->n);

 merr:
  PUTMSG_MAERR(fn);
  if (cl) TXcgislClear(cl);
  return(0);
}

int
cgisladdvar(cl, name, val)
CGISL           *cl;
CONST char      *name, *val;
/* Public version.
 */
{
  return(addvar(cl, name, -1, val, -1, 0, 0));
}

int
cgisladdvarsz(cl, name, val, sz)
CGISL           *cl;
CONST char      *name, *val;
size_t          sz;
/* Public version.
 */
{
  return(addvar(cl, name, -1, val, sz, 0, 0));
}

int
TXcgislAddVarLenSz(cl, name, nameLen, val, sz)
CGISL           *cl;            /* (in/out) list to add to */
CONST char      *name;          /* (in) name (no embedded nuls) */
size_t          nameLen;        /* (in) length of `name' (-1 for strlen()) */
CONST char      *val;           /* (in) value */
size_t          sz;             /* (in) size of `val' (-1 for strlen()) */
/* Public version.
 */
{
  return(addvar(cl, name, nameLen, val, sz, 0, 0));
}

int
TXcgislAddVarLenSzLower(cl, name, nameLen, val, sz)
CGISL           *cl;            /* (in/out) list to add to */
CONST char      *name;          /* (in) name (no embedded nuls) */
size_t          nameLen;        /* (in) length of `name' (-1 for strlen()) */
CONST char      *val;           /* (in) value */
size_t          sz;             /* (in) size of `val' (-1 for strlen()) */
/* Same as TXcgislAddVarLenSz(), but lower-cases var name.
 */
{
  return(addvar(cl, name, nameLen, val, sz, 0, 1));
}

CGISL *
dupcgisl(sl)
CGISL   *sl;
/* Returns duplicate copy of `sl'.
 */
{
  static CONST char     fn[] = "dupcgisl";
  CGISL                 *newsl;
  CGIS                  *newcs, *cs;
  size_t                i, j;

  if ((newsl = (CGISL *)calloc(1, sizeof(CGISL))) == CGISLPN) goto merr;
  if (sl->n > 0)
    {
      newsl->cs = (CGIS *)calloc(((sl->n + NEXP-1)/NEXP)*NEXP, sizeof(CGIS));
      if (newsl->cs == CGISPN) goto merr;
      if ((newsl->buf = (char *)malloc(sl->bufsz)) == CHARPN) goto merr;
      memcpy(newsl->buf, sl->buf, sl->bufused);
      for (i = 0, cs = sl->cs, newcs = newsl->cs;
           i < (size_t)sl->n;
           i++, cs++, newcs++)
        {
          newsl->n++;
          newcs->tag = newsl->buf + (cs->tag - sl->buf);
          newcs->tagLen = cs->tagLen;
          newcs->s = (char **)calloc(((cs->n + NEXP-1)/NEXP)*NEXP + TERMSZ,
                                     sizeof(char *));
          if (newcs->s == CHARPPN) goto merr;
          newcs->len = (size_t *)calloc(((cs->n + NEXP-1)/NEXP)*NEXP + TERMSZ,
                                        sizeof(size_t));
          if (newcs->len == SIZE_TPN)
            {
            merr:
              PUTMSG_MAERR(fn);
              return(closecgisl(newsl));
            }
          for (j = 0; j < (size_t)cs->n; j++)
            {
              newcs->s[j] = newsl->buf + (cs->s[j] - sl->buf);
              newcs->len[j] = cs->len[j];
            }
          newcs->s[j] = "";
          newcs->n = cs->n;
        }
    }
  newsl->bufsz = sl->bufsz;
  newsl->bufused = sl->bufused;
  newsl->privnum = sl->privnum;
  newsl->cmp = sl->cmp;
  return(newsl);
}

size_t
cgisl_numvals(cgisl)
CGISL   *cgisl;
/* Returns number of values stored in `cgisl'.
 */
{
  int           i;
  char          **sp;
  size_t        n;

  n = (size_t)0;
  for (i = 0; i < cgisl->n; i++)
    {
      for (sp = cgisl->cs[i].s; *sp != CHARPN && **sp != '\0'; sp++, n++);
    }
  return(n);
}

size_t
TXcgislNumVars(cgisl)
const CGISL     *cgisl;
/* Returns number of variables in `cgisl'.
 */
{
  return(cgisl->n);
}

/* ------------------------------------------------------------------------- */

CGI *
closecgi(cp)
CGI *cp;
{
  int	i;

  if (cp == CGIPN) goto done;

  if (cp->flags & CF_INHDRS) {
    /* don't call cgiendhrs()? may not want to kick out cookie: */
    fputs("\n", stdout);
    fflush(stdout);
    cp->flags &= ~CF_INHDRS;
  }
  if (!(cp->flags & CF_ISPRE) && cp->content != CHARPN)
    free(cp->content);                  /* KNG 981217 not owner if CF_ISPRE */
  if (cp->priv == CGIPRIVPN) goto done;
  for (i = 0; i < CGISL_NUM; i++)
    TXcgislClear(&cp->priv->sl[i]);
  free(cp->priv);
 done:
  if (cp != CGIPN) free(cp);
#if 0           /* KNG 970318 leave open for bolt-on Vortex; user must close */
  closehtpf();	/* close any htpf() Metamorph queries */
#endif
  fflush(stdout);
  return(CGIPN);
}

/************************************************************************/

static int htoi ARGS((char **sp));
static int
htoi(sp)
char **sp;
/* see also urlstrncpy()
 */
{
  char  *s, ch, ch2;
  int   i;

  s = *sp;
  ch = '%';
  for (i = 0; (i < 2) && (*s != '\0'); i++)
    {
      if ((ch2 = *(s++)) >= '0' && ch2 <= '9')
        ch2 -= '0';
      else if (ch2 >= 'A' && ch2 <= 'F')
        ch2 -= ('A' - 10);
      else if (ch2 >= 'a' && ch2 <= 'f')
        ch2 -= ('a' - 10);
      else
        {
          s--;
          break;                /* illegal escape; keep what we got */
        }
      ch = (i ? (char)(((byte)ch << 4) | (byte)ch2) : ch2);
    }
  *sp = s;
  return(ch);
}

/************************************************************************/

int
getcgich(ps)
char **ps;
/* Returns translated character at `*ps', or -1 at end.
 * Advances `*ps'.  See also urlstrncpy().
 */
{
 int c;
 char *p= *ps;

 switch(*p)
    {
     case '\0': return(-1);
     case '%':
         {
          ++p;
          c=htoi(&p);
         } break;
     case '+':
         {
          c=' ';
          ++p;
         } break;
     case '&':
         {
          c='\0';
          ++p;
         }break;
     default:
         {
          c=*p;
          ++p;
         }break;
    }
 *ps=p;
 return(c);
}

/************************************************************************/

int
cgisladdstr(cl, s)
CGISL	*cl;
char	*s;
/* Translates URL-encoded vars in `s' and adds to C string list `cl'.
 * Returns 0 on error, 1 if ok.
 */
{
  static CONST char     fn[] = "cgisladdstr";
  char                  *pv, *dv, *buf, *e, *end;
  int                   ret;
  size_t                n, bufsz, nameLen;

  if (s == CHARPN || *s == '\0') return(1);

  bufsz = strlen(s);
  end = s + bufsz;
  if ((buf = (char *)malloc(bufsz + 2)) == CHARPN)
    {
      PUTMSG_MAERR(fn);
      goto err;
    }
  /* Pull off each "name=value" pair, decode and add to list: */
  for ( ; s < end; s = e + 1)
    {
      /* Look for delimiters '&'/';' and '=' _before_ decoding: KNG 961020 */
      for (e = s; (e < end) && (*e != '&') && (*e != ';'); e++);
      for (pv = s; (pv < e) && (*pv != '='); pv++);
      nameLen = urlstrncpy(buf, bufsz, s, pv - s, (USF)0);
      buf[nameLen] = '\0';
      if (*buf == '\0') continue;       /* empty name */
      dv = buf + nameLen + 1;
      if (pv < e) pv++;                 /* skip '=' */
      n = urlstrncpy(dv, bufsz - nameLen, pv, e - pv, (USF)0);
      dv[n] = '\0';
      if (!addvar(cl, buf, nameLen, dv, n, 0, 0)) goto err;
    }
  ret = 1;

done:
  if (buf != CHARPN) free(buf);
  return(ret);
err:
  ret = 0;
  goto done;
}

static size_t
TXcgiParseHeaderParamName(const char *name, int *idx, int *isEncoded)
/* Parses a `name' such as:
 *   foo[*23][*]
 * and sets `*idx' to 23 (or -1 if not present).  `*idx' will be >= 0
 * if index is present.  Sets `*isEncoded' to 1 if trailing `*' present,
 * 0 if not.
 * Returns length of root name.
 */
{
  const char    *s;
  char          *e;
  int           num;
  size_t        rootLen;

  *idx = -1;                                    /* no suffix is default */
  *isEncoded = 0;                               /* default */
  rootLen = strcspn(name, "*");
  s = name + rootLen;
  if (*s == '*')                                /* `*N' or `*' */
    {
      if (s[1])                                 /* probably `*N' */
        {
          int   errnum;

          num = TXstrtoi(s + 1, NULL, &e, 10, &errnum);
          if (e > s + 1 && num >= 0 && errnum == 0)     /* parsed N ok */
            {
              if (*e == '*')                    /* probably EOB `*' */
                {
                  if (e[1]) goto badSyntax;     /* garbage after end `*' */
                  *isEncoded = 1;
                }
              else if (*e)
                goto badSyntax;
              *idx = num;
            }
          else                                  /* bad syntax: bad N */
            {
            badSyntax:
              rootLen = strlen(name);
            }
        }
      else                                      /* `*' at EOB only */
        *isEncoded = 1;
    }
  return(rootLen);
}

typedef struct ParamName_tag
{
  const char    *name;
  size_t        nameLen;
  int           contIdx;                        /* `*N' index */
  int           isEncoded;
  size_t        orgIdx;                         /* original param index */
  size_t        orgIdxForSort;
}
ParamName;

static int
TXcgiMergeParameters_SortCb(const void *a, const void *b)
/* qsort() callback for sorting raw param names.
 */
{
  const ParamName       *paramA = (const ParamName *)a;
  const ParamName       *paramB = (const ParamName *)b;
  int                   cmp;

  /* First sort by `orgIdxForSort': */
  if (paramA->orgIdxForSort < paramB->orgIdxForSort) return(-1);
  if (paramA->orgIdxForSort > paramB->orgIdxForSort) return(1);
  /* Second sort by name (case-insensitive per RFC 2231 section 7): */
  cmp = strnicmp(paramA->name, paramB->name,
                 TX_MIN(paramA->nameLen, paramB->nameLen));
  if (cmp < 0) return(-1);
  if (cmp > 0) return(1);
  if (paramA->nameLen < paramB->nameLen) return(-1);
  if (paramA->nameLen > paramB->nameLen) return(1);
  /* Third sort by continuation index: */
  if (paramA->contIdx < paramB->contIdx) return(-1);
  if (paramA->contIdx > paramB->contIdx) return(1);
  /* Should not get here; last sort encoded-first: */
  if (paramA->isEncoded > paramB->isEncoded) return(-1);
  if (paramA->isEncoded < paramB->isEncoded) return(1);
  return(0);
}

static int
TXcgiMergeParameters_Flush(HTPFOBJ *htpfobj, CGISL *outParams, const char *name,
                           size_t nameLen, HTBUF *buf, const char *charset)
/* Translates charset of merged and decoded `buf' value and flushes with
 * name `name' to `outParams'.  `htpfobj' is optional, for msgs/charset conv.
 * Returns 0 on severe error, 1 on partial error (e.g. unknown charset),
 * 2 if ok.
 */
{
  char          *bufData, *outBuf = NULL;
  size_t        bufDataSz, outBufSz = 0;
  int           ret = 2;
  TXPMBUF       *pmbuf;

  pmbuf = htpfgetpmbuf(htpfobj);
  bufDataSz = htbuf_getdata(buf, &bufData, 0);
  if (charset && *charset)
    {                                           /* translate charset */
      const HTCSINFO    *csInfo;
      HTCHARSET         cs;

      csInfo = htstr2charset(htpfgetcharsetconfigobj(htpfobj),
                             charset, NULL);
      cs = (csInfo ? csInfo->tok : HTCHARSET_UNKNOWN);
      outBufSz = TXtransbuf(htpfobj, cs,
                            (cs == HTCHARSET_UNKNOWN ? charset : NULL),
                            NULL, 0x1, (UTF)0, bufData, bufDataSz, &outBuf);
      if (outBuf)
        {
          bufData = outBuf;
          bufDataSz = outBufSz;
        }
      else
        ret = 1;                                /* charset trans. failed */
    }
  if (!TXcgislAddVarLenSz(outParams, name, nameLen, bufData, bufDataSz))
    goto err;
  goto finally;

err:
  ret = 0;
finally:
  outBuf = TXfree(outBuf);
  htbuf_clear(buf);
  return(ret);
}

static int
TXcgiMergeParameters(HTPFOBJ *htpfobj, CGISL *rawParams, CGISL **mergedParams)
/* Decodes and merges RFC 2231/5987 value-continuation, charset and language
 * syntax for params, to UTF-8:
 *   arg[*0][*]=[charset'lang']This%20is%20a%20t%E9st
 *   [arg*1[*]=...]
 * `htpfobj' is optional, for msgs/charset config.
 * Returns 2 if ok, 1 silently on syntax problems (e.g. missing parts),
 * 0 on severe error.  Sets `*mergedParams' to alloced merged params.
 */
{
  static const char     fn[] = "TXcgiMergeParameters";
  CGISL                 *outParams = NULL;
  ParamName             *sortedParams = NULL, *param, *sortedParamsEnd;
  size_t                i, useOrgIdx, numRawParams;
  int                   ret = 2, isFirstSection;
  char                  *charsetThisParam = NULL;
  HTBUF                 *buf = NULL;
  TXPMBUF               *pmbuf;

  pmbuf = htpfgetpmbuf(htpfobj);
  outParams = opencgisl();
  if (!outParams) goto err;
  cgislsetcmp(outParams, strnicmp);             /* per RFC 2231 section 7 */
  /* Sort `rawParams' into `sortedRawParams', first by name/cont-idx: */
  numRawParams = TXcgislNumVars(rawParams);
  if (numRawParams == 0) goto finally;          /* nothing to do */
  sortedParams = (ParamName *)TXcalloc(pmbuf, fn, numRawParams,
                                       sizeof(ParamName));
  if (!sortedParams) goto err;
  sortedParamsEnd = sortedParams + numRawParams;
  for (param = sortedParams, i = 0; param < sortedParamsEnd; param++, i++)
    {
      param->name = cgislvarsz(rawParams, i, NULL, NULL);
      if (!param->name) goto missingName;
      param->nameLen = TXcgiParseHeaderParamName(param->name, &param->contIdx,
                                                 &param->isEncoded);
      param->orgIdx = i;
      /* First sort is by name/cont-idx, so ignore primary `orgIdxForSort':*/
      param->orgIdxForSort = 0;
    }
  qsort(sortedParams, numRawParams, sizeof(ParamName),
        TXcgiMergeParameters_SortCb);
  /* Set `orgIdxForSort' back to `orgSortIdx', but use the first (`*0')
   * cont-idx's `orgIdx' for all of that same-name var.  This will sort
   * vars by original-index-of-cont-0 first, then by name (same), then by
   * cont-idx:
   */
  param = sortedParams;
  param->orgIdxForSort = useOrgIdx = param->orgIdx;
  for (param++; param < sortedParamsEnd; param++)
    {
      if (param->contIdx < param[-1].contIdx || param->contIdx < 0)
        useOrgIdx = param->orgIdx;
      param->orgIdxForSort = useOrgIdx;
    }
  qsort(sortedParams, numRawParams, sizeof(ParamName),
        TXcgiMergeParameters_SortCb);

  /* Now decode and merge params: */
  buf = openhtbuf();
  if (!buf) goto err;
  if (!htbuf_setpmbuf(buf, pmbuf, 0x3)) goto err;
  for (param = sortedParams, isFirstSection = 1;
       param < sortedParamsEnd;
       param++, isFirstSection = 0)
    {
      const char        *name;
      char              **vals;
      size_t            *sizes, i;

      name = cgislvarsz(rawParams, param->orgIdx, &vals, &sizes);
      if (!name || !vals)
        {
        missingName:
          txpmbuf_putmsg(pmbuf, MERR, fn,
                         "Internal error: Cannot get var name/data");
          goto err;
        }
      if (param == sortedParams ||              /* first param */
          param->contIdx < param[-1].contIdx || /* new param name */
          param->contIdx < 0 ||                 /* non-continuation param */
          param[-1].contIdx < 0)                /* previous is "" */
        {                                       /* new param */
          if (param > sortedParams)             /* flush old param */
            switch (TXcgiMergeParameters_Flush(htpfobj, outParams,
                    param[-1].name, param[-1].nameLen, buf, charsetThisParam))
              {
              case 2:           break;          /* all ok */
              case 1: ret = 1;  break;          /* partial failure */
              case 0:                           /* error */
              default:          goto err;
              }
          isFirstSection = 1;
          charsetThisParam = TXfree(charsetThisParam);
          /* First section should be index 0 if continuation: */
          if (param->contIdx > 0) ret = 1;      /* syntax error; continue */
        }
      else if (param->contIdx != param[-1].contIdx + 1)
        ret = 1;                                /* gap in continuation vals */
      i = 0;
      if (param->isEncoded)
        {
          const char    *s = vals[i], *e;

          if (isFirstSection)                   /* get charset/lang */
            {
              if (!(*vals[i]
#ifdef BONUS_TERMINATE
                    || vals[i + 1]
#endif
                    ))                          /* no first val */
                ret = 1;
              else
                {
                  s = strchr(s, '\'');
                  if (!s)                       /* missing "charset'lang'" */
                    {
                      ret = 1;
                      s = vals[i];
                    }
                  else                          /* get charset */
                    {
                      charsetThisParam = TXstrndup(pmbuf, fn, vals[i], s-vals[i]);
                      if (!charsetThisParam) goto err;
                      s++;                      /* skip charset-lang "'" */
                      e = strchr(s, '\'');
                      if (!e)                   /* missing lang separator */
                        ret = 1;
                      else
                        s = e + 1;              /* skip lang and "'" WTF */
                    }
                }
            }
          if (!htbuf_pf(buf, "%!U", s)) goto err;
          i++;
        }
      for ( ; *vals[i]
#ifdef BONUS_TERMINATE
              || vals[i + 1]
#endif
              ; i++)
        if (param->isEncoded ? !htbuf_pf(buf, "%!U", vals[i]) :
            !htbuf_write(buf, vals[i], sizes[i]))
          goto err;
    }
  if (param > sortedParams)                     /* flush last param */
    switch (TXcgiMergeParameters_Flush(htpfobj, outParams, param[-1].name,
                                       param[-1].nameLen, buf,
                                       charsetThisParam))
      {
      case 2:           break;                  /* all ok */
      case 1: ret = 1;  break;                  /* partial failure */
      case 0:                                   /* error */
      default:          goto err;
      }
  goto finally;

err:
  outParams = closecgisl(outParams);
  ret = 0;
finally:
  sortedParams = TXfree(sortedParams);
  *mergedParams = outParams;
  outParams = NULL;
  buf = closehtbuf(buf);
  charsetThisParam = TXfree(charsetThisParam);
  return(ret);
}

char *
cgiparsehdr(htpfobj, hdr, end, parms)
HTPFOBJ           *htpfobj;         /* (opt.) for messages, charset config */
CONST char      *hdr;           /* (in) header to parse */
CONST char      **end;          /* (out) end of initial value */
CGISL           **parms;        /* (out, opt.) parameters */
/* Parses a MIME header for its value and returns it, with `*end' set
 * to the value end.  `hdr' is header string, of form:
 *       value; [arg=val | 'val' | "val"; [arg=val; ...]]
 * If `parms' is non-NULL, `*parms' is set to a CGISL with the
 * parameters (caller must closecgisl()).  Returns NULL on error
 * (eg. no mem).  `hdr' must be writable (but is not changed).
 * Decodes and merges RFC 2231/5987 value-continuation, charset and
 * language syntax for params, to UTF-8:
 *   arg[*0][*]=[charset'lang']This%20is%20a%20t%E9st
 *   [arg*1[*]=...]
 * `*parms' may be set NULL if no headers.
 */
{
  static CONST char     whitesemi[] = " \t\r\n;=";
  static CONST char     mt[] = "";
  CONST char            *vals, *vale, *e, *nams, *name, *ret;
  char                  q;
  int                   r, res, foundMergeableParam = 0;

#define SKIPSP(s)       (s += strspn(s, WhiteSpace))

  /* Pull off nominal value: */
  SKIPSP(hdr);
  e = hdr + strcspn(hdr, whitesemi);
  ret = hdr;
  if (end) *end = e;

  /* Pull off parameters, if requested: */
  if (parms == CGISLPPN) goto done;
  SKIPSP(hdr);
  if (*hdr == '\0')
    {
      *parms = CGISLPN;
      goto done;
    }
  if ((*parms = opencgisl()) == CGISLPN) goto err;
  hdr = e;
  while (*hdr != '\0')
    {
      SKIPSP(hdr);
      if (*hdr == ';') hdr++;
      SKIPSP(hdr);
      if (*hdr == '\0') break;                  /* end of params */
      nams = hdr;
      hdr = name = nams + strcspn(nams, whitesemi);
      SKIPSP(hdr);
      if (*hdr == '=')                          /* has a value */
        {
          hdr++;
          SKIPSP(hdr);
          vals = hdr;
          q = *hdr;
          if (q == '"' || q == '\'')            /* quoted value */
            {
              for (vale = ++vals; *vale != '\0' && *vale != q; vale++);
              hdr = vale;
              if (*vale == q) hdr++;
            }
          else
            hdr = vale = vals + strcspn(vals, whitesemi);
        }
      else
        vals = vale = (char *)mt;               /* dummy empty value */
      if (nams == name) continue;               /* empty name */
      r = TXcgislAddVarLenSz(*parms, nams, name - nams, vals, vale - vals);
      if (!r) goto err;
      /* Optimization: only merge params below if needed: */
      if (!foundMergeableParam)
        foundMergeableParam = (memchr(nams, '*', name - nams) != NULL);
    }

  /* Decode/merge RFC 2231/5987 continuation/charset: */
  /* Optimization: only if needed: */
  if (foundMergeableParam)
    {
      CGISL     *mergedParams;

      res = TXcgiMergeParameters(htpfobj, *parms, &mergedParams);
      if (!res) goto err;                       /* wtf if partial error? */
      *parms = closecgisl(*parms);
      *parms = mergedParams;
    }

done:
  return((char *)ret);
err:
  ret = CHARPN;
  goto done;
}

/************************************************************************/

int
writecgi(cp,fp)
CGI *cp;
FILE *fp;
{
size_t l;

   if(cp->content_length!=(char *)NULL && cp->content!=(char *)NULL)
   {
      l=(size_t)atoi(cp->content_length);
      if(fwrite(cp->content,1,l,fp)!=l)
         return(0);
   }
   return(1);
}

/************************************************************************/
static int fromenv ARGS((CGI *cp));
static int
fromenv(cp)
CGI *cp;
{
     cp->server_software   =getenv("SERVER_SOFTWARE"  );
     cp->server_name       =getenv("SERVER_NAME"      );
     cp->gateway_interface =getenv("GATEWAY_INTERFACE");
     cp->server_protocol   =getenv("SERVER_PROTOCOL"  );
     cp->server_port       =getenv("SERVER_PORT"      );
     cp->request_method    =getenv("REQUEST_METHOD"   );
     cp->http_connection   =getenv("HTTP_CONNECTION"  );/* PBR 1-1-96 */
     cp->http_user_agent   =getenv("HTTP_USER_AGENT"  );/* PBR 1-1-96 */
     cp->http_host         =getenv("HTTP_HOST"        );/* PBR 1-1-96 */
     cp->http_accept       =getenv("HTTP_ACCEPT"      );
     cp->http_cookie       =getenv("HTTP_COOKIE"      );/* PBR 1-1-96 */
     cp->http_x_forwarded_for=getenv("HTTP_X_FORWARDED_FOR");
     cp->path_info         =getenv("PATH_INFO"        );
     cp->path_translated   =getenv("PATH_TRANSLATED"  );
     cp->script_name       =getenv("SCRIPT_NAME"      );
     cp->query_string      =getenv("QUERY_STRING"     );
     cp->remote_host       =getenv("REMOTE_HOST"      );
     cp->remote_addr       =getenv("REMOTE_ADDR"      );
     cp->remote_user       =getenv("REMOTE_USER"      );
     cp->auth_type         =getenv("AUTH_TYPE"        );
     cp->content_type      =getenv("CONTENT_TYPE"     );
     cp->content_length    =getenv("CONTENT_LENGTH"   );
     cp->document_root     =getenv("DOCUMENT_ROOT"    );
     cp->server_root       =getenv("SERVER_ROOT"      );
     return(1);
}
/************************************************************************/
static char *getmapxy ARGS((CGISL *cl, char *qs));
static char *
getmapxy(cl, qs)
CGISL	*cl;
char	*qs;
/* Checks query string `qs' for "xxx,yyy" at start, and adds X,Y vars
 * for them:  for (non-form) image map refs.  Returns rest of query string.
 */
{
  static CONST char     digs[] = "0123456789";
  int		        xn, yn;
  char		        ch;

  if ((xn = strspn(qs, digs)) > 0 &&
      qs[xn] == ',' &&
      (yn = strspn(qs + xn + 1, digs)) > 0) {
    qs[xn] = '\0';	/* temp. terminate */
    addvar(cl, "x", 1, qs, xn, 0, 0);
    qs[xn] = ',';
    qs += xn + 1;
    ch = qs[yn];
    qs[yn] = '\0';
    addvar(cl, "y", 1, qs, yn, 0, 0);
    qs[yn] = ch;
    qs += yn;
  }
  return(qs);
}

int
TXcgislAddCookiesFromHeader(CGISL *sl, const char *hdrVal, size_t hdrValLen,
                            int urlDecodeValues)
/* Parses `Cookie:' header value `hdrVal' (length `hdrValLen') and adds
 * cookies to `sl'.  URL-decodes values if `urlDecodeValues' is non-zero.
 * Returns 0 on error.
 */
{
  static const char     fn[] = "TXcgislAddCookiesFromHeader";
  const char            *varName, *nextCookie, *hdrValEnd, *cookieVal;
  size_t                varNameLen, cookieValLen;
  int                   ret = 0;
  char                  *tmpAllocBuf = NULL;
  char                  tmpBuf[1024];


  if (hdrValLen == (size_t)(-1)) hdrValLen = strlen(hdrVal);
  hdrValEnd = hdrVal + hdrValLen;

  for (varName = hdrVal;
       varName += TXstrspnBuf(varName, hdrValEnd, WhiteSpace, -1),
         varName < hdrValEnd;
       varName = nextCookie)
    {
      nextCookie = varName + TXstrcspnBuf(varName, hdrValEnd, ";", 1);
      varNameLen = TXstrcspnBuf(varName, nextCookie, "=", 1);
      cookieVal = varName + varNameLen;
      if (cookieVal < nextCookie) cookieVal++;  /* skip the `=' */
      cookieValLen = nextCookie - cookieVal;
      if (nextCookie < hdrValEnd) nextCookie++; /* skip the `;' */
      if (urlDecodeValues)
        {
          if (cookieValLen < sizeof(tmpBuf))
            {
              cookieValLen = urlstrncpy(tmpBuf, sizeof(tmpBuf), cookieVal,
                                        cookieValLen, (USF)0);
              cookieVal = tmpBuf;
            }
          else
            {
              tmpAllocBuf = (char *)TXmalloc(TXPMBUFPN, fn, cookieValLen + 1);
              if (!tmpAllocBuf) goto err;
              cookieValLen = urlstrncpy(tmpAllocBuf, cookieValLen + 1,
                                        cookieVal, cookieValLen, (USF)0);
              cookieVal = tmpAllocBuf;
            }
        }
      if (!TXcgislAddVarLenSz(sl, varName, varNameLen, cookieVal,
                              cookieValLen))
        goto err;
      tmpAllocBuf = TXfree(tmpAllocBuf);
    }
  ret = 1;
  goto finally;

err:
  ret = 0;
finally:
  tmpAllocBuf = TXfree(tmpAllocBuf);
  return(ret);
}

/************************************************************************/

static int make_cookie_sl ARGS((CGI *cp));
static int
make_cookie_sl(cp)
CGI	*cp;
/* Parses HTTP cookie string and sets up cookie CGISL.  Returns 1 if ok,
 * 0 on error (no mem).  Automatically called by opencgi[pre](); may be
 * called again if OCF_URLDECODECOOKIEVALS changes.
 */
{
  CGISL *cl;

  cl = &cp->priv->sl[CGISL_COOKIE];
  TXcgislClear(cl);
  if (cp->http_cookie == CHARPN) return(1);

  if (!TXcgislAddCookiesFromHeader(cl, cp->http_cookie, -1,
                              ((cp->flags & CF_URLDECODECOOKIEVALS) ? 1 : 0)))
    goto err;
  return(1);

err:
  if (cl) TXcgislClear(cl);
  return(0);
}

int
iscgiprog()
/* Returns non-zero if we're invoked in a CGI environment.
 * Signal-safe.
 * Thread-unsafe.
 */
{
  static TXATOMINT      res = -1;
  char                  **env, *v;

  if (res != -1) return(res);

  /* Don't use getenv(): not signal-safe, and slower: */
  for (env = _environ; (v = *env) != CHARPN; env++)
    switch (*v)
      {
      case 'C':
        if (strncmp(v, "CONTENT_LENGTH=", 15) == 0) return(res = 1);
        break;
      case 'Q':
        if (strncmp(v, "QUERY_STRING=", 13) == 0) return(res = 1);
        break;
      case 'R':
        if (strncmp(v, "REMOTE_HOST=", 12) == 0 ||
            strncmp(v, "REMOTE_ADDR=", 12) == 0 ||
            strncmp(v, "REQUEST_METHOD=", 15) == 0)
          return(res = 1);
        break;
      case 'S':
        if (strncmp(v, "SCRIPT_NAME=", 12) == 0) return(res = 1);
        break;
      }
  return(res = 0);
}

/* ------------------------------------------------------------------------- */

static CGI *cgi_create ARGS((OCF flags));
static CGI *
cgi_create(flags)
OCF     flags;
{
  static CONST char     fn[] = "cgi_create()";
  CGI                   *cg;
  CGISL                 *sl;
  int                   i;

#if defined(MSDOS) || defined(__MINGW32__)
  _setmode(fileno(stdin), _O_BINARY);           /* for GIFs, etc. */
  _setmode(fileno(stdout), _O_BINARY);
#endif
  if ((cg = (CGI *)calloc(1, sizeof(CGI))) == CGIPN ||
      (cg->priv = (CGIPRIV *)calloc(1, sizeof(CGIPRIV))) == CGIPRIVPN)
    {
      PUTMSG_MAERR(fn);
      goto err;
    }
  if (flags & OCF_URLDECODECOOKIEVALS) cg->flags |= CF_URLDECODECOOKIEVALS;
  for (i = 0, sl = cg->priv->sl; i < CGISL_NUM; i++, sl++)
    cgislinit(sl);

done:
  return(cg);
err:
  cg = closecgi(cg);
  goto done;
}

static int cgi_postinit ARGS((CGI *cg));
static int
cgi_postinit(cg)
CGI     *cg;
/* Initializes stuff after standard CGI vars are set, stdin read, etc.
 */
{
  char          *qs;
  CONST char    *ct, *ce;
  CGISL         *sl;

  /* Check Content-Type to know if content vars are URL-encoded or MIME:
   * KNG 961001
   */
  if (cg->content != CHARPN &&
      (cg->content_type == CHARPN ||            /* maybe HTTP/0.9 */
       (ct = cgiparsehdr(NULL, cg->content_type, &ce, CGISLPPN)) == CHARPN ||
       ((ce - ct) == 33 &&
        strnicmp(ct, "application/x-www-form-urlencoded", 33) == 0) ||
       ((ce - ct) == 31 &&
        strnicmp(ct, "application/www-form-urlencoded", 31) == 0)))
    {
      cgisladdstr(&cg->priv->sl[CGISL_CONTENT], cg->content);
    }
  /* else if MIME, use getmime() */

  if (cg->query_string != CHARPN)
    {
      sl = &cg->priv->sl[CGISL_URL];
      qs = getmapxy(sl, cg->query_string);      /* pull off xxx,yyy first */
      cgisladdstr(sl, qs);
    }
  make_cookie_sl(cg);
  return(1);
}

CGI *
opencgi(flags)
OCF     flags;
{
  static CONST char     fn[] = "opencgi";
  CGI                   *cg;
  size_t                sz, rsz, rd;
  int                   fd;
  char                  *d;

  if ((cg = cgi_create(flags)) == CGIPN) goto err;

  /*
  if (Direct)
    fromhttp(cg);
  else
  */
    fromenv(cg);

  /* Read stdin: */
  cg->content = CHARPN;
  cg->content_sz = 0;
  if (cg->content_length != CHARPN &&
      (sz = (size_t)atoi(cg->content_length)) > 0)
    {
      if ((cg->content = (char *)malloc(sz + 1)) == CHARPN)
        {
          PUTMSG_MAERR(fn);                             /* don't bail */
        }
      else
        {
          fd = fileno(stdin);
          for (rsz = 0, d = cg->content;
               sz > 0;
               rsz += rd, sz -= rd, d += rd)
            {
              rd = (size_t)read(fd, d, sz);
              if ((int)rd == -1 || rd == 0) break;      /* error/EOF */
            }
          cg->content[rsz] = '\0';
          cg->content_sz = rsz;                         /* could be shorter */
        }
    }

  if (!cgi_postinit(cg)) goto err;

done:
  return(cg);
err:
  cg = closecgi(cg);
  goto done;
}

CGI *
opencgipre(flags, init, envn, envv)
OCF     flags;
CGI     *init;
char    **envn, **envv;
/* Like opencgi(), but standard CGI vars, stdin, etc. come from
 * elsewhere (`init').  `envn', `envv' (if non-NULL) are name, value
 * array for environment variables for CGI.  Undocumented function.
 * -KNG 970227
 */
{
  CGI   *cg, sav;
  CGISL *sl;

  if ((cg = cgi_create(flags)) == CGIPN) goto err;

  sav.flags = cg->flags;
  sav.priv = cg->priv;
  memcpy(cg, init, sizeof(CGI));
  cg->flags = (sav.flags | CF_ISPRE);
  cg->priv = sav.priv;

  if (!cgi_postinit(cg)) goto err;

  if (envn != CHARPPN && envv != CHARPPN)
    {
      sl = &cg->priv->sl[CGISL_ENV];
      for ( ; *envn != CHARPN; envn++, envv++)
        if (**envn != '\0') addvar(sl, *envn, -1, *envv, -1, 0, 0);
      cg->flags |= CF_PROCENV;
    }

done:
  return(cg);
err:
  cg = closecgi(cg);
  goto done;
}

OCF
cgigetflags(cp)
CGI     *cp;
{
  return((cp->flags & CF_URLDECODECOOKIEVALS) ? OCF_URLDECODECOOKIEVALS : 0);
}

int
cgisetflags(cp, flags, on)
CGI     *cp;
OCF     flags;
int     on;
/* Sets (if `on') or clears (if not `on') `flags'.
 * NOTE: changing OCF_URLDECODECOOKIEVALS may cause re-parse of cookie vars.
 * Returns 0 on error (out of mem), 1 if ok.
 */
{
  int   oflags, aflags;

  oflags = cp->flags;
  flags &= OCF_URLDECODECOOKIEVALS;             /* only public flags */
  aflags = 0;                                   /* map OCF to CF */
  if (flags & OCF_URLDECODECOOKIEVALS) aflags |= CF_URLDECODECOOKIEVALS;
  if (on)
    cp->flags |= aflags;
  else
    cp->flags &= ~aflags;
  if ((cp->flags ^ oflags) & CF_URLDECODECOOKIEVALS)
    return(make_cookie_sl(cp));
  return(1);
}

/************************************************************************/

int
cgiprocenv(cp)
CGI	*cp;
/* Creates CGISL list from environment.  Called automagically when
 * cgivar() refers to environment vars; must be called by user after
 * putenv()'s to update list.
 */
{
  int		n;
  char		*var, *val;
  CGISL		*cl;

  cl = &cp->priv->sl[CGISL_ENV];
  TXcgislClear(cl);
  for (n = 0; (var = environ[n]) != CHARPN; n++) {
    if ((val = strchr(var, '=')) != CHARPN) {
      if (val > var)                            /* non-empty name */
        {
          *val = '\0';
          addvar(cl, var, -1, val + 1, -1, 0, 0);
          *val = '=';
        }
    } else if (*var != '\0') addvar(cl, var, -1, "", 0, 0, 0);
  }
  cp->flags |= CF_PROCENV;
  return(1);
}

char *
cgivar(cp, n, which, valp)
CGI	*cp;
int	n, which;
char	***valp;
/* Gets `n'th CGI variable from the group given by CGI_... `flags'.
 * Returns variable name, setting `*valp' to values.
 * WTF drop in favor of cgivarsz().
 */
{
  int	i, vn;
  CGISL	*sl;
  CGIS	*cs;

  /* Don't snarf state, environment until needed: */
  if (!(cp->flags & CF_PROCENV) && (which & CGI_ENV)) cgiprocenv(cp);

  for (sl = cp->priv->sl, i = 0; (n >= 0) && (i < CGISL_NUM); i++, sl++) {
    if (!(which & (1 << i))) continue;
    if (n >= (vn = sl->n - sl->privnum)) {
      n -= vn;
    } else {
      cs = &sl->cs[sl->privnum + n];
      *valp = cs->s;
      return(cs->tag);
    }
  }
  return(CHARPN);
}

char *
cgivarsz(cp, n, which, valp, szp)
CGI	*cp;
int	n, which;
char	***valp;
size_t  **szp;
/* cgivar(), but returns sizes too.
 */
{
  int	i, vn;
  CGISL	*sl;
  CGIS	*cs;

  /* Don't snarf state, environment until needed: */
  if (!(cp->flags & CF_PROCENV) && (which & CGI_ENV)) cgiprocenv(cp);

  for (sl = cp->priv->sl, i = 0; (n >= 0) && (i < CGISL_NUM); i++, sl++) {
    if (!(which & (1 << i))) continue;
    if (n >= (vn = sl->n - sl->privnum)) {
      n -= vn;
    } else {
      cs = &sl->cs[sl->privnum + n];
      *valp = cs->s;
      *szp = cs->len;
      return(cs->tag);
    }
  }
  return(CHARPN);
}

#ifdef CGITEST
static char *cgivarpriv ARGS((CGI *cp, int n, int which, char ***valp));
static char *
cgivarpriv(cp, n, which, valp)
CGI	*cp;
int	n, which;
char	***valp;
/* cgivar() for private variables.
 */
{
  int	i;
  CGISL	*sl;
  CGIS	*cs;

  /* Don't snarf state, environment until needed: */
  if (!(cp->flags & CF_PROCENV) && (which & CGI_ENV)) cgiprocenv(cp);

  for (sl = cp->priv->sl, i = 0; (n >= 0) && (i < CGISL_NUM); i++, sl++) {
    if (!(which & (1 << i))) continue;
    if (n >= sl->privnum) {
      n -= sl->privnum;
    } else {
      cs = &sl->cs[n];
      *valp = cs->s;
      return(cs->tag);
    }
  }
  return(CHARPN);
}
#endif	/* CGITEST */

/* ------------------------------------------------------------------------- */

char **
getcgi(cp, name, which)
CGI	*cp;
char	*name;
int	which;
/* Looks up public variable `name' in `cp', returning its value(s) or
 * NULL if not found.  Only checks lists indicated by CGI_... flags in
 * `which', with precedence (see cgivar()).
 * WTF drop in favor of getcgisz().
 */
{
  int	i, j;
  CGISL	*sl;
  size_t        nameLen;

  /* Don't snarf state, environment until needed: */
  if (!(cp->flags & CF_PROCENV) && (which & CGI_ENV)) cgiprocenv(cp);

  nameLen = strlen(name);
  for (sl = cp->priv->sl, i = 0; i < CGISL_NUM; i++, sl++) {
    if (!(which & (1 << i))) continue;
    for (j = sl->privnum; j < sl->n; j++) {
      if (sl->cs[j].tagLen == nameLen &&
          sl->cmp(sl->cs[j].tag, name, nameLen) == 0)
	return(sl->cs[j].s);
    }
  }
  return(CHARPPN);
}

char **
getcgisz(cp, name, which, szp)
CGI	*cp;
char	*name;
int	which;
size_t  **szp;
/* getcgi() with sizes.  WTF drop getcgi() in favor of this.
 */
{
  int	i, j;
  CGISL	*sl;
  size_t        nameLen;

  /* Don't snarf state, environment until needed: */
  if (!(cp->flags & CF_PROCENV) && (which & CGI_ENV)) cgiprocenv(cp);

  nameLen = strlen(name);
  for (sl = cp->priv->sl, i = 0; i < CGISL_NUM; i++, sl++) {
    if (!(which & (1 << i))) continue;
    for (j = sl->privnum; j < sl->n; j++) {
      if (sl->cs[j].tagLen == nameLen &&
          sl->cmp(sl->cs[j].tag, name, nameLen) == 0)
        {
          *szp = sl->cs[j].len;
          return(sl->cs[j].s);
        }
    }
  }
  *szp = SIZE_TPN;
  return(CHARPPN);
}

/************************************************************************/

#ifdef CGITEST
void
prtcgi(cp)
CGI *cp;
{
#ifdef __stdc__
#  define prt(x) printf("%s = %s <P>\n",#x,(x==CHARPN ? "(null)" : x));
#else
#  define prt(x) printf("%s = %s <P>\n","x",(x==CHARPN ? "(null)" : x));
#endif
 int i;
 char *tag,**value;

 prt(cp->server_software  );
 prt(cp->server_name      );
 prt(cp->gateway_interface);
 prt(cp->server_protocol  );
 prt(cp->server_port      );
 prt(cp->document_root    );
 prt(cp->server_root      );
 prt(cp->request_method   );
 prt(cp->http_accept      );
 prt(cp->path_info        );
 prt(cp->path_translated  );
 prt(cp->script_name      );
 prt(cp->query_string     );
 prt(cp->remote_host      );
 prt(cp->remote_addr      );
 prt(cp->remote_user      );
 prt(cp->auth_type        );
 prt(cp->content_type     );
 prt(cp->content_length   );
 prt(cp->content          );


 for(i=0;(tag=cgicontent(cp,i,&value))!=NULL;i++)
    printf("content: %s = %s<p>\n",tag,value[0]);

 for(i=0;(tag=cgiurl(cp,i,&value))!=NULL;i++)
    printf("url: %s = %s<p>\n",tag,value[0]);
}

/************************************************************************/

int
main(argc,argv)
int     argc;
char   *argv[];
{
 int i;
 CGI *cp;

 puts("Content-type: text/html\n");
 puts("<TITLE>THUNDERSTONE CGI OBJECT TEST</TITLE>\n");

 for(i=1;i<argc;i++)
    puts(argv[i]);
 if((cp=opencgi(OCF_URLDECODECOOKIEVALS))!=CGIPN)
    {
     prtcgi(cp);
     closecgi(cp);
    }

 exit(0);
}
#endif /* CGITEST */
