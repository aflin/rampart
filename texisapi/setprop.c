/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include "sizes.h"
#include "os.h"
#include "dbquery.h"
#include "texint.h"
#include "mmsg.h"
#include "meter.h"
#include "merge.h"
#include "cgi.h"

#define CHKCP()	if(!globalcp) {globalcp=TXopenapicp();}
/******************************************************************/

extern int TXexttrig;
extern APICP	*globalcp;

/******************************************************************/

/*	Also add to sqluge.h */

char *
TXdupwsep(path)
char	*path;
/* strdup() `path', ensuring PATH_SEP is on the end (if path not empty).
 * KNG 960501
 */
{
  static const char     fn[] = "TXdupwsep";
  char	*dup;
  int	len;

  len = strlen(path);
  if (len == 0 || TX_ISPATHSEP(path[len-1]))
    {
      dup = TXstrdup(TXPMBUFPN, fn, path);
    }
  else if ((dup = (char *)TXmalloc(TXPMBUFPN, fn, len + 2)) != CHARPN)
    {
      strcpy(dup, path);
      dup[len++] = PATH_SEP;
      dup[len] = '\0';
    }
  return(dup);
}

#if defined(TX_DEBUG) && defined(_WIN32) && !defined(TX_NO_CRTDBG_H_INCLUDE)
int __cdecl
dbghktoputmsg(int nReportType, char *szMsg, int *pnRet)
{
   int nRet = FALSE;
   int msgType;

   switch (nReportType)
   {
      case _CRT_ASSERT:
      {
	 msgType = 0;
         break;
      }

      case _CRT_WARN:
      {
	 msgType = 100;
         break;
      }

      case _CRT_ERROR:
      {
	 msgType = 0;
         break;
      }

      default:
      {
	 msgType = 200;
         break;
      }
   }

   putmsg(msgType, "CRT", szMsg);

   if   (pnRet)
      *pnRet = 0;

   return   nRet;
}
#endif /* TX_DEBUG && _WIN32 && !TX_NO_CRTDBG_H_INCLUDE */

/******************************************************************/
/*

	docs comment means that it is in the docs
*/

static int setoptimize ARGS((DDIC *, char *, int));
static int setbetafeature ARGS((DDIC *, char *, int));
static int setmessages ARGS((DDIC *, char *, int));
static int setoption ARGS((DDIC *, char *, int));

#ifndef DO_TEST
#   define TXsetparm(a, b, c)
#endif

static CONST char	CommaWhiteSpace[] = ", \t\r\n\v\f";
#define WhiteSpace	(CommaWhiteSpace + 1)

int
TXparseWithinmode(pmbuf, val, mode)
TXPMBUF		*pmbuf;	/* (out) buffer for msgs */
CONST char	*val;	/* (in) value to parse */
int		*mode;	/* (out) withinmode value */
/* Parses withinmode value `val', which is either an integer or a string
 * of the form:
 *   char|word[,around|span]
 * Sets `*mode' to parsed mode.
 * Returns 0 on error, 1 on success.
 */
{
	static CONST char	fn[] = "TXparseWithinmode";
	CONST char		*s;
	char			*e;
	int			n, unit, type;

	/* See if `val' is an integer: */
	*mode = (int)strtol(val, &e, 0);
	if (e != val && strchr(WhiteSpace, *e) != CHARPN)
		return(1);			/* success */

	/* Not an int.  Parse "char|word[,around|span]" syntax: */
	unit = -1;
	type = API3WITHIN_TYPE_RADIUS;		/* default type */
	for (s = val; *s != '\0'; s += n)
	{
		s += strspn(s, CommaWhiteSpace);
		if (*s == '\0') break;		/* end of string */
		n = strcspn(s, CommaWhiteSpace);
		if (n == 4 && strnicmp(s, "char", 4) == 0)
			unit = API3WITHINCHAR;
		else if (n == 4 && strnicmp(s, "word", 4) == 0)
			unit = API3WITHINWORD;
		else if (n == 6 && strnicmp(s, "radius", 6) == 0)
			type = API3WITHIN_TYPE_RADIUS;
		else if (n == 4 && strnicmp(s, "span", 4) == 0)
			type = API3WITHIN_TYPE_SPAN;
		else
			goto badVal;
	}
	if (unit == -1)				/* must give a unit */
	{
	badVal:
		txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
			"Unknown/invalid withinmode value `%s'", val);
		return(0);
	}
	*mode = (unit | type);
	return(1);				/* success */
}

static int TXparseMeterProperty ARGS((CONST char *val));
static int
TXparseMeterProperty(val)
CONST char      *val;
/* Returns 0 on error.
 */
{
  static CONST char     equalsWhiteSpace[] = "= \t\r\n\v\f";
  CONST char            *s, *e, *procBegin, *procEnd, *typeBegin, *typeEnd;
  TXMDT                 meterVal, mVal;
  int                   ret = 1;
  size_t                procLen;

  /* value is `{PROCESS[=TYPE]}|TYPE[;PROCESS[=TYPE];...]'
   * PROCESS is `index', `compact', or `all'.
   * TYPE is `none', `simple', `percent', `on' or `off'.
   * Default TYPE is `simple'.
   */
  for (s = val; *s != '\0'; s = e + (*e == ';'))
    {
      meterVal = TXMDT_SIMPLE;                  /* default */
      e = s + strcspn(s, ";");                  /* end of semicolon-SV */
      procBegin = s + TXstrspnBuf(s, e, WhiteSpace, -1);
      procEnd = procBegin + TXstrcspnBuf(procBegin, e, equalsWhiteSpace, -1);
      procLen = procEnd - procBegin;
      s = procEnd + TXstrspnBuf(procEnd, e, WhiteSpace, -1);
      if (*s == '=')                            /* optional type present */
        {
          typeBegin = s + 1 + TXstrspnBuf(s + 1, e, WhiteSpace, -1);
          typeEnd = typeBegin + TXstrcspnBuf(typeBegin, e, WhiteSpace, -1);
          mVal = meter_str2type(typeBegin, typeEnd);
          if (mVal != TXMDT_INVALID)
            meterVal = mVal;
          else
            {
              putmsg(MWARN + UGE, CHARPN, "Invalid meter type `%.*s'",
                     (int)(typeEnd - typeBegin), typeBegin);
              ret = 0;
            }
        }
      if (procLen == 5 && strnicmp(procBegin, "index", 5) == 0)
        TXindexmeter = meterVal;
      else if (procLen == 7 && strnicmp(procBegin, "compact", 5) == 0)
        TXcompactmeter = meterVal;
      else if (procLen == 3 && strnicmp(procBegin, "all", 3) == 0)
        TXindexmeter = TXcompactmeter = meterVal;
      else if ((mVal = meter_str2type(procBegin, procEnd)) != TXMDT_INVALID)
        TXindexmeter = TXcompactmeter = mVal;
      else
        {
          putmsg(MWARN + UGE, CHARPN, "Unknown meter process or type `%.*s'",
                 (int)procLen, procBegin);
          ret = 0;
        }
    }
  return(ret);
}

int
TXsetVerbose(n)
int     n;
/* Propagates `set verbose=n' appropriately to C variables.
 * Returns 0 on success, -1 on error.
 */
{
  TXverbosity = n;
  TXfldmathverb = 0;
  TXshowindexes(0);
  TXshowactions(0);
  if (n > 0)
    {
      TXshowindexes(1);
      if (n > 1)
        {
          TXshowactions(1);
          if (n > 2)
            {
              TXfldmathverb = n - 2;
            }
        }
    }
  return(0);					/* success */
}

int
setprop(ddic, prop, value)
DDIC	*ddic;
char	*prop;
char	*value;
{
	static CONST char Fn[] = "setprop";
	static CONST char	SQLpropGroup[] = "SQL property";
	int	rc, n, errnum, intVal;
        char    propi[128];
	char	*e;
	EPI_HUGEINT	h;
	TXMDT	meterVal;

        TXstrncpy(propi, prop, sizeof(propi));
        propi[sizeof(propi) - 1] = '\0';
        strlwr(propi);


/* Let's sort these by importance.  Those likely to be called often in
   a high performance situation come first, then the less likely, then
   the don't cares, such as those that affect index creation. */

	if (!strcmp(propi, "likeprows")) /* docs */
	{
		TXsetparm(ddic, "likeprows", value);
		TXnlikephits = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "likepallmatch")) /* docs */
	{
		int	val;

		TXsetparm(ddic, "likepallmatch", value);
		val = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi, value,
					CHARPN, 2);
		if (!TXapicpSetLikepAllMatch(val)) return(-1);
                rppm_setgain(propi, val);
		return 0;
	}
	if (!strcmp(propi, "likepobeyintersects")) /* docs */
	{
		TXsetparm(ddic, "likepobeyintersects", value);
		if (!TXapicpSetLikepObeyIntersects(TXgetBooleanOrInt(TXPMBUFPN,
				SQLpropGroup, propi, value, CHARPN, 2)))
		    return(-1);
		return 0;
	}
	if (rppm_setgain(propi, atoi(value)))
	{
		TXsetparm(ddic, propi, value);
		return(0);
	}
	if (!strcmp(propi, "matchmode"))  /* docs */       /* JMT 98-05-06 */
	{
		int rc;
                rc = TXsetmatchmode(atol(value));
		TXsetparm(ddic, propi, value);
		return rc < 0 ? -1 : 0;
	}
	if (!strcmp(propi, "singleuser")) /* docs */
	{
		TXsingleuser = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup,
					propi, value, CHARPN, 2);
		if(TXsingleuser)
			ddic->nolocking = 1;
		else
			ddic->nolocking = 0;
		if(ddic->nolocking && ddic->dblock)
			ddic->dblock = closedblock(ddic->pmbuf, ddic->dblock,
						   ddic->sid,
					       TXbool_False /* !readOnly */);
		if(!ddic->nolocking && !ddic->dblock)
			return TXddicnewproc(ddic);
		return 0;
	}
	if (!strcmp(propi, "datefmt")) /* docs */
	{
		TXsetparm(ddic, "datefmt", value);
		return TXsetdatefmt(value);
	}


	if (!strcmp(propi, "nolocking")) /* docs */
	{
		ddic->nolocking = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup,
					propi, value, CHARPN, 2);
#ifdef NEW_NOLOCK
		if(ddic->nolocking && ddic->dblock)
			ddic->dblock = closedblock(ddic->dblock, ddic->sid);
		if(!ddic->nolocking && !ddic->dblock)
		{
			return TXddicnewproc(ddic);
		}
#endif
		return 0;
	}

	if (!strcmp(propi, "lockmode")) /* docs */
	{
		if(!strcmpi(value, "manual"))
			lockmode(ddic, LOCK_MANUAL);
		else
			lockmode(ddic, LOCK_AUTOMATIC);
		return 0;
	}

	if (!strcmp(propi, "fairlock")) /* docs */
	{
		TXsetfairlock(TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup,
					propi, value, CHARPN, 2));
		return 0;
	}

	if (!strcmp(propi, "addexp")) /* docs */
	{
		rc = _addexp(value);
		return rc;
	}

	if (!strcmp(propi, "delexp")) /* docs */
	{
		rc = _delexp(value);
		return rc;
	}

	if (!strcmp(propi, "lstexp")) /* docs */
	{
		rc = _listexp(value);
		return rc;
	}

	if (!strcmp(propi, "indexblock")) /* docs */
	{
		TXsetblockmax(atoi(value));
		TXsetparm(ddic, "indexblock", value);
		return 0;
	}

	if (!strcmp(propi, "tablespace")) /* docs */
	{
		if (ddic->tbspc)
			free(ddic->tbspc);
		ddic->tbspc = TXdupwsep(value);		/* KNG 960501 */
		TXsetparm(ddic, "tablespace", ddic->tbspc);
		return 0;
	}

	if (!strcmp(propi, "indexspace")) /* docs */
	{
		/* Note: see also TXindOptsProcessRawOptions() */
		if (ddic->indspc)
			free(ddic->indspc);
		ddic->indspc = TXdupwsep(value);	/* KNG 960501 */
		TXsetparm(ddic, "indexspace", ddic->indspc);
		return 0;
	}

	if (!strcmp(propi, "indirectspace"))
	{
		if (ddic->indrctspc != ddic->pname)
			ddic->indrctspc = TXfree(ddic->indrctspc);
		ddic->indrctspc = TXdupwsep(value);/* JMT 1999-12-15 */
		TXsetparm(ddic, "indirectspace", ddic->indrctspc);
		return 0;
	}

	/* CP variables. */

	if (!strcmp(propi, "keepnoise")) /* docs */
	{
		CHKCP();
		globalcp->keepnoise = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "minwordlen")) /* docs */
	{
		CHKCP();
		globalcp->minwordlen = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "suffixproc")) /* docs */
	{
		CHKCP();
		globalcp->suffixproc = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "defsuffrm")) /* docs */
	{
		CHKCP();
		globalcp->defsuffrm = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "prefixproc")) /* docs */
	{
		CHKCP();
		globalcp->prefixproc = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "rebuild")) /* docs */
	{
		CHKCP();
		globalcp->rebuild = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "useequiv") || /* docs */
	    !strcmp(propi, "keepeqvs"))	  /* added KNG 20070213 */ /* docs */
	{
		CHKCP();
		globalcp->keepeqvs = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "inc_sdexp")) /* docs */
	{
		CHKCP();
		globalcp->incsd = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "inc_edexp")) /* docs */
	{
		CHKCP();
		globalcp->inced = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (strcmp(propi, "intersects") == 0)	/* docs */
	{
		int	newVal;

		CHKCP();
		/* Bug 7409: `intersects' is int not boolean: */
		newVal = TXstrtoi(value, NULL, NULL,
				  (0 | TXstrtointFlag_ConsumeTrailingSpace |
				       TXstrtointFlag_TrailingSourceIsError),
				  &errnum);
		if (errnum)
		{
		badNum:
			txpmbuf_putmsg(TXPMBUFPN, MERR + UGE, __FUNCTION__,
	      "%s value `%s' for %s `%s': expected decimal/hex/octal integer",
				       (errnum == ERANGE ? "Out-of-range" :
					"Invalid"), value, SQLpropGroup, propi);
			return(-1);
		}
		globalcp->intersects = newVal;
		return(0);
	}
	if (!strcmp(propi, "eqprefix"))
	{
		char	*tmp;

		tmp = strdup(value);
		if(!tmp)
			return -1;
		CHKCP();
		if(globalcp->eqprefix)
			free(globalcp->eqprefix);
		globalcp->eqprefix = (byte *)tmp;
		TXsetparm(ddic, "eqprefix", value);
		return 0;
	}
	if (!strcmp(propi, "ueqprefix"))
	{
		char	*tmp;

		tmp = strdup(value);
		if(!tmp)
			return -1;
		CHKCP();
		if(globalcp->ueqprefix)
			free(globalcp->ueqprefix);
		globalcp->ueqprefix = (byte *)tmp;
		TXsetparm(ddic, "ueqprefix", value);
		return 0;
	}
	if (!strcmp(propi, "sdexp")) /* docs */
	{
		char	*tmp;

		tmp = strdup(value);
		if(!tmp)
			return -1;
		CHKCP();
		if(globalcp->sdexp)
			free(globalcp->sdexp);
		globalcp->sdexp = (byte *)tmp;
		TXsetparm(ddic, "sdexp", value);
		return 0;
	}
	if (!strcmp(propi, "edexp")) /* docs */
	{
		char	*tmp;

		tmp = strdup(value);
		if(!tmp)
			return -1;
		CHKCP();
		if(globalcp->edexp)
			free(globalcp->edexp);
		globalcp->edexp = (byte *)tmp;
		TXsetparm(ddic, "edexp", value);
		return 0;
	}
	if (!strcmp(propi, "reqsdelim"))
	{
		CHKCP();
		globalcp->reqsdelim = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "reqedelim"))
	{
		CHKCP();
		globalcp->reqedelim = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "olddelim"))
	{
		CHKCP();
		globalcp->olddelim = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
                                 /* MAW 06-03-98 - add cp query perms */
	if (!strcmp(propi, "denymode"))
	{
		CHKCP();
		if (strcmpi(value, "silent") == 0)
			globalcp->denymode = API3DENYSILENT;
		else if (strcmpi(value, "warning") == 0)
			globalcp->denymode = API3DENYWARNING;
		else if (strcmpi(value, "error") == 0)
			globalcp->denymode = API3DENYERROR;
		else
		{
			char	*e;

			n = TXstrtoi(value, NULL, &e, 0, &errnum);
			switch (n)
			{
			case API3DENYSILENT:
			case API3DENYWARNING:
			case API3DENYERROR:
				if (errnum == 0) break;	/* valid value */
				/* fall through to error */
			default:
				putmsg(MERR + UGE, __FUNCTION__,
				       "Invalid denymode value `%s': must be `silent', `warning', `error' or integer %d, %d, %d",
				       value, (int)API3DENYSILENT,
				       (int)API3DENYWARNING,
				       (int)API3DENYERROR);
				return(-1);
			}
			globalcp->denymode = n;
		}
		return 0;
	}
	if (!strcmp(propi, "alpostproc"))
	{
		CHKCP();
		globalcp->alpostproc = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "allinear"))
	{
		CHKCP();
		globalcp->allinear = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "alwild"))
	{
		CHKCP();
		globalcp->alwild = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "alnot"))
	{
		CHKCP();
		globalcp->alnot = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "alwithin"))
	{
		CHKCP();
		globalcp->alwithin = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "alintersects"))
	{
		CHKCP();
		globalcp->alintersects = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "alequivs"))
	{
		CHKCP();
		globalcp->alequivs = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "alphrase"))
	{
		CHKCP();
		globalcp->alphrase = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "exactphrase"))
	{
		CHKCP();
		if (strcmpi(value, "ignorewordposition") == 0 ||
		    atoi(value) == API3EXACTPHRASEIGNOREWORDPOSITION)
			globalcp->exactphrase = API3EXACTPHRASEIGNOREWORDPOSITION;
		else
			globalcp->exactphrase = TXgetBooleanOrInt(TXPMBUFPN,
					SQLpropGroup, propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "qminwordlen"))
	{
		CHKCP();
		globalcp->qminwordlen = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "qminprelen"))
	{
		CHKCP();
		globalcp->qminprelen = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "qmaxsets") ||
            !strcmp(propi, "qmaxterms"))                /*back-compatibility*/
	{
		CHKCP();
		globalcp->qmaxsets = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "qmaxsetwords"))             /* KNG 990816 */
	{
		CHKCP();
		globalcp->qmaxsetwords = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "withinmode"))	/* docs */
	{
		CHKCP();
		if (!TXparseWithinmode(TXPMBUFPN, value, &n))
			return(-1);		/* error */
		globalcp->withinmode = n;
		return 0;			/* success */
	}
	if (!strcmp(propi, "phrasewordproc"))	/* docs */
	{
		CHKCP();
		if(!strcmpi(value, "mono"))
			globalcp->phrasewordproc = API3PHRASEWORDMONO;
		else if(!strcmpi(value, "none"))
			globalcp->phrasewordproc = API3PHRASEWORDNONE;
		else if(!strcmpi(value, "last"))
			globalcp->phrasewordproc = API3PHRASEWORDLAST;
		else if(!strcmpi(value, "all"))
			globalcp->phrasewordproc = API3PHRASEWORDALL;
		else if(*value >= '0' && *value <= '9')
			globalcp->phrasewordproc = atoi(value);
		else
			return -1;
		return 0;
	}
	if (!strcmp(propi, "qmaxwords"))                /* KNG 990816 */
	{
		CHKCP();
		globalcp->qmaxwords = atoi(value);
		return 0;
	}
	if (strcmp(propi, "querysettings") == 0)
	{
		n = strtol(value, &e, 0);
		if (e == value) n = -1;
		if (n == API3_QUERYSETTINGS_DEFAULTS ||
		    strcmpi(value, "defaults") == 0)
		{
			/* We allow `defaults' even for pre-version-6,
			 * so that texis/tests/global-pre.sql can set it
			 * without having to check the version.
			 */
		setApicpDefaults:
			/* We are in a Texis function, so `defaults'
			 * means Texis defaults:
			 */
			globalcp = closeapicp(globalcp);
			if ((globalcp = TXopenapicp()) == APICPPN)
				return(-1);
			return(0);		/* success */
		}
		else if (n == API3_QUERYSETTINGS_TEXIS5DEFAULTS ||
			 strcmpi(value, "texis5defaults") == 0)
		{
			globalcp = closeapicp(globalcp);
			if ((globalcp = openapicp()) == APICPPN)
				return(-1);
			if (!TXsetTexisApicpDefaults(globalcp, 0, 1))
				return(-1);
			return(0);		/* success */
		}
		else if (n == API3_QUERYSETTINGS_VORTEXDEFAULTS ||
			 strcmpi(value, "vortexdefaults") == 0)
			/* Vortex same as Texis defaults for Version 6+: */
			goto setApicpDefaults;
		else if (n == API3_QUERYSETTINGS_PROTECTIONOFF ||
			 strcmpi(value, "protectionoff") == 0)
		{
			/* We allow `protectionoff' even for pre-version-6,
			 * so that texis/tests/global-pre.sql can set it
			 * without having to check the version.
			 */
			globalcp->denymode = API3DENYWARNING;
			globalcp->alpostproc = 1;
			globalcp->allinear = 1;
			globalcp->alwild = 1;
			globalcp->alnot = 1;
			globalcp->alwithin = 1;
			globalcp->alintersects = 1;
			globalcp->alequivs = 1;
			globalcp->alphrase = 1;
			TXallineardict = 1;	/* WTF add to APICP struct */
			globalcp->exactphrase = API3EXACTPHRASEON;
			globalcp->qminwordlen = 1;
			globalcp->qminprelen = 0;
			globalcp->qmaxsets = 0;
			globalcp->qmaxsetwords = 0;
			globalcp->qmaxwords = 0;
			return(0);		/* success */
		}
		else
		{
			putmsg(MWARN + UGE, CHARPN,
			       "Invalid querysettings value `%s'", value);
			return(-1);		/*  unknown value */
		}
	}
	if (!strcmp(propi, "wildoneword"))	/* docs */
	{
		TXwildoneword = atoi(value);
                return 0;
	}
	if (!strcmp(propi, "wildsufmatch"))	/* docs */
	{
		TXwildsufmatch = atoi(value);
                return 0;
	}
	if (!strcmp(propi, "allineardict"))	/* docs */
	{
		TXallineardict = atoi(value);
                return 0;
	}
	if (!strcmp(propi, "indexminsublen"))	/* docs */
	{
		TXindexminsublen = atoi(value);
		if (TXindexminsublen < 1) TXindexminsublen = 1;
                return 0;
	}
	if (!strcmp(propi, "wildsingle"))	/* docs */
	{
		TXwildoneword = TXwildsufmatch = atoi(value);
                return 0;
	}
	if (!strcmp(propi, "indexwithin"))	/* docs */
	{
		int	intVal;
		char	*e;

		if (strcmp(value, "default") == 0)
			TXindexWithin = TXindexWithinFlag_Default;
		else
		{
			intVal = TXstrtoi(value, NULL, &e, 0, &errnum);
			e += strspn(e, WhiteSpace);
			if (errnum || *e)
			{
				txpmbuf_putmsg(TXPMBUFPN, MERR + UGE,
					       __FUNCTION__,
       "Invalid %s value `%s': Expected decimal/hex/octal value or `default'",
					       propi, value);
				return(-1);
			}
			TXindexWithin = (TXindexWithinFlag)intVal;
		}
		return 0;
	}
	if (strcmp(propi, "findselloopcheck") == 0)
	{	/* not documented; may go away, should probably always be on */
		extern int	TXfindselLoopCheck;
		TXfindselLoopCheck = atoi(value);
	}
	if (!strcmp(propi, "tablereadbufsz") ||
	    !strcmp(propi, "tablereadbufsize"))
	{
		if (!tx_parsesz(TXPMBUFPN, value, &h, propi,
				EPI_OS_SIZE_T_BITS, TXbool_True))
			return(0);
		if (h < (EPI_HUGEINT)0) h = (EPI_HUGEINT)0;
		TXtableReadBufSz = (size_t)h;
		if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(TXtableReadBufSz))
			TXtableReadBufSz = 0;
		return(0);
	}
	if (!strcmp(propi, "btreedump"))	/* docs */
	{
		TXbtreedump = (int)strtol(value, CHARPPN, 0);
		return 0;
	}
	if (!strcmp(propi, "btreelog"))		/* docs */
	{
		if (TXbtreelog != CHARPN) free(TXbtreelog);
		if (*value == '\0') TXbtreelog = CHARPN;
		else TXbtreelog = strdup(value);
		return 0;
	}
	if (!strcmp(propi, "infthresh")) /* docs */
	{
		TXsetparm(ddic, "infthresh", value);
                TXsetinfinitythreshold(atoi(value));
                return 0;
	}
	if (!strcmp(propi, "infpercent")) /* docs */
	{
		TXsetparm(ddic, "infpercent", value);
		TXsetinfinitypercent(atoi(value));
                return 0;
	}
	if (!strcmp(propi, "btreethreshold")) /* docs */
	{
		TXbtreemaxpercent=atoi(value);
		return 0;
	}
	if (!strcmp(propi, "maxlinearrows")) /* docs */
	{
		TXmaxlinearrows=atoi(value);
		return 0;
	}
	if (!strcmp(propi, "likepmode")) /* docs *//* reset */
	{
		TXsetparm(ddic, "likepmode", value);
		TXlikepmode = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "likeptime")) /* docs */
	{
		TXsetparm(ddic, "likeptime", value);
		TXlikeptime = (long)(strtod(value, NULL)*1000000.0);
		return 0;
	}
	if (!strcmp(propi, "likerpercent"))
	{
		TXsetparm(ddic, "likerpercent", value);
		TXlikermaxthresh = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "likerrows")) /* docs */
	{
		TXsetparm(ddic, "likerrows", value);
		TXlikermaxrows = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "locksleepmethod")) /* docs */
	{
		TXsetparm(ddic, "locksleepmethod", value);
		TXsetsleepparam(SLEEP_METHOD, atoi(value));
		return 0;
	}
	if (!strcmp(propi, "locksleeptime")) /* docs */
	{
		TXsetparm(ddic, "locksleeptime", value);
		TXsetsleepparam(SLEEP_MULTIPLIER, 1000*atoi(value));
		return 0;
	}
	if (!strcmp(propi, "locksleepmaxtime")) /* docs */
	{
		TXsetparm(ddic, "locksleepmaxtime", value);
		TXsetsleepparam(SLEEP_MAXSLEEP, 1000*atoi(value));
		return 0;
	}
	if (!strcmp(propi, "lockverbose")) /* docs */
	{
		int	newVal;

		TXsetparm(ddic, "lockverbose", value);
		newVal = TXstrtoi(value, NULL, NULL,
				  (0 | TXstrtointFlag_ConsumeTrailingSpace |
				       TXstrtointFlag_TrailingSourceIsError),
				  &errnum);
		if (errnum) goto badNum;
		if (!TXsetlockverbose(newVal)) return(-1);
		return 0;
	}
	if (!strcmp(propi, "bubble")) /* docs */
	{
		int	bubble;

		bubble = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup,
						propi, value, CHARPN, 2);
		if(bubble)
			ddic->no_bubble = 0;
		else
			ddic->no_bubble = 1;
		return 0;
	}
	if(!strcmp(propi, "paramchk")) /* docs */
	{
		TXsetDiscardUnsetParameterClauses(!TXgetBooleanOrInt(TXPMBUFPN,
				      SQLpropGroup, propi, value, CHARPN, 2));
		return 0;
	}
	/* KNG 20080402 ignorecase is deprecated; use stringcomparemode: */
	if(!strcmp(propi, "ignorecase")) /* docs */
	{
		int TXigncase;

		CHKCP();
		TXigncase = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi,
					value, CHARPN, 2) ? 1 : 0;
		globalcp->stringcomparemode = TXCFF_SUBST_CASESTYLE(
				globalcp->stringcomparemode,
				(TXigncase ? TXCFF_CASESTYLE_IGNORE :
				 TXCFF_CASESTYLE_RESPECT));
		return 0;
	}
	if (!strcmp(propi, "stringcomparemode"))
	{
		TXCFF	mode;

		/* Note: see also TXindOptsProcessRawOptions() */
		CHKCP();
		if (!TXstrToTxcff(value, CHARPN, globalcp->textsearchmode,
				  globalcp->stringcomparemode,
				  globalcp->stringcomparemode, 1,
				  (TXCFF)(-1), &mode))
			putmsg(MWARN + UGE, CHARPN,
				"Invalid stringcomparemode value `%s'",
				value);
		else
			globalcp->stringcomparemode = mode;
		return(0);
	}
	if (!strcmp(propi, "textsearchmode"))
	{
		TXCFF	mode;

		CHKCP();
		if (!TXstrToTxcff(value, CHARPN, globalcp->textsearchmode,
				  globalcp->stringcomparemode,
				  globalcp->textsearchmode, 0,
				  (TXCFF)(-1), &mode))
			putmsg(MWARN + UGE, CHARPN,
				"Invalid textsearchmode value `%s'",
				value);
		else
			globalcp->textsearchmode = mode;
		return(0);
	}
	if(!strcmp(propi, "predopttype")) /* docs */
	{
		TXpredopttype(atoi(value));
		return 0;
	}
#ifdef _WIN32
	if(!strcmp(propi, "cleanupwait")) /* docs */
	{
		TXSetCleanupWait(atoi(value));
		return 0;
	}
#endif
	if (strcmp(propi, "dbcleanupverbose") == 0)
	{
		n = strtol(value, &e, 0);
		if (e == value || e[strspn(e, WhiteSpace)])
			goto invalidPropValue;
		TXdbCleanupVerbose = n;
		return(0);
	}
	if(!strcmp(propi, "triggermode")) /* docs */
	{
		TXexttrig = atoi(value);
		return 0;
	}
	if(!strcmp(propi, "indexaccess")) /* docs */
	{
		TXallowidxastbl = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup,
					propi, value, CHARPN, 2) ? 1 : 0;
		return 0;
	}
	if(!strcmp(propi, "indexchunk")) /* docs */
	{
		TXindexchunk = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "indexmem"))  /* docs */     /* KNG 971105 */
	{
          /* Note: see also TXgetindexmem(): */
	  if (!tx_parsesz(TXPMBUFPN, value, &h, propi, EPI_OS_SIZE_T_BITS,
			  TXbool_True))
		  return(0);
          if (h < (EPI_HUGEINT)0) h = (EPI_HUGEINT)0;
          TXindexmemUser = (size_t)h;
          TXsetparm(ddic, propi, value);
          return 0;
	}
        if (!strcmp(propi, "indexmeter"))   /* docs */  /* KNG 980302 */
          {
            /* Note: see also TXindOptsProcessRawOptions() */
            meterVal = meter_str2type(value, CHARPN);
            if (meterVal == TXMDT_INVALID)
              putmsg(MWARN + UGE, CHARPN, "Unknown indexmeter value `%s'",
                     value);
            else
              TXindexmeter = meterVal;
            return(0);
          }
        if (!strcmp(propi, "meter"))
          {
            if (!TXparseMeterProperty(value)) return(-1);
            return(0);
          }
        if (!strcmp(propi, "addindextmp"))              /* KNG 980515 docs */
          {
            return(TXaddindextmp(value));
          }
        if (!strcmp(propi, "delindextmp"))              /* KNG 980515 docs */
          {
            return(TXdelindextmp(value));
          }
        if (!strcmp(propi, "lstindextmp") ||            /* KNG 980515 docs */
            !strcmp(propi, "listindextmp"))
          {
            return(TXlistindextmp(value));
          }
	if (!strcmp(propi, "defaultlike")) /* docs */    /* JMT 980414 */
	{
		if(!strcmpi(value, "LIKE"))
			TXdefaultlike = FOP_MM;
		else if(!strcmpi(value, "LIKEP"))
			TXdefaultlike = FOP_PROXIM;
		else if(!strcmpi(value, "LIKER"))
			TXdefaultlike = FOP_RELEV;
		else if(!strcmpi(value, "LIKE3"))
			TXdefaultlike = FOP_NMM;
		else if(!strcmpi(value, "MATCHES"))
			TXdefaultlike = FOP_MAT;
                else
                    putmsg(MWARN, CHARPN, "Unknown LIKE type `%s'", value);
		return(0);
	}
	if (!strcmp(propi, "btreecachesize")) /* docs */
	{
		TXbtreecache = atol(value);
		return 0;
	}
	if (!strcmp(propi, "ramrows")) /* docs */
	{
		TXsetramblocks(atol(value));
		return 0;
	}
	if (!strcmp(propi, "ramlimit")) /* docs */
	{
		TXsetramsize(atol(value));
		return 0;
	}
	if (!strcmp(propi, "hyphenphrase"))     /* docs */
	{
		pm_hyeqsp(TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi,
				value, CHARPN, 2));
		return 0;
	}
	if (!strcmp(propi, "showplan"))
	{
extern int TXshowiplan;
		TXshowiplan=TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi,
						value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "locale"))
	{
		if(TXsetlocale(value) != CHARPN)
			return 0;
		else
		{
			putmsg(MERR + UGE, Fn, "Unknown locale `%s'", value);
			return -1;
		}
	}
	if (!strcmp(propi, "verbose"))
	{
		n = atoi(value);
		return(TXsetVerbose(n));
	}
	if (strcmp(propi, "fldmathverbosemaxvaluesize") == 0)
	{
		n = (int)strtol(value, &e, 0);
		if (e == value || e[strspn(e, WhiteSpace)])
			goto invalidPropValue;
		if (n < 0) n = -1;
		TXfldmathVerboseMaxValueSize = n;
		return(0);
	}
	if (strcmp(propi, "fldmathverbosehexints") == 0)
	{
		int	val;

		val = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi, value,
					CHARPN, 3);
		if (val < 0) return(-1);	/* already yapped */
		TXfldmathVerboseHexInts = !!val;
		return(0);
	}
	if (!strcmp(propi, "optimize")) /* docs *//* reset *//* JMT 99-06-10 */
	{
		return setoptimize(ddic, value, 1);
	}
	if (!strcmp(propi, "nooptimize")) /* docs *//* reset *//* JMT 99-06-10 */
	{
		return setoptimize(ddic, value, 0);
	}
	if (!strcmp(propi, "message")) /* docs *//* reset *//* JMT 99-06-10 */
	{
		return setmessages(ddic, value, 1);
	}
	if (!strcmp(propi, "nomessage")) /* docs *//* reset *//* JMT 99-06-10 */
	{
		return setmessages(ddic, value, 0);
	}
	if (!strcmp(propi, "options"))/* docs *//* JMT 2000-06-20 */
	{
		return setoption(ddic, value, 1);
	}
	if (!strcmp(propi, "nooptions"))/* docs *//* JMT 2000-06-20 */
	{
		return setoption(ddic, value, 0);
	}
	if (!strcmp(propi, "ignorenewlist"))/* docs *//* JMT 1999-08-11 */
	{
		TXdisablenewlist = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup,
					propi, value, CHARPN, 2);
		return 0;
	}
	if (!strcmp(propi, "verifysingle"))/* JMT 1999-09-17 */
	{
		TXverifysingle = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "indirectcompat")) /* docs */
	{
		rc = TXindcompat(value);
		return rc;
	}
        if (!strcmp(propi, "traceidx") ||
            !strcmp(propi, "traceindex") ||
            !strcmp(propi, "indextrace"))       /* docs */
          {                                     /* KNG 991026 */
            n = (int)strtol(value, &e, 0);
            if (e == value || e[strspn(e, WhiteSpace)])
              goto invalidPropValue;
	    return(TXsetTraceIndex(n) ? 0 : -1);
          }
        if (!strcmp(propi, "tracerppm"))
          {
            TXtraceRppm = (int)strtol(value, CHARPPN, 0);
            return(0);
          }
        if (!strcmp(propi, "indexversion"))     /* KNG 000329 docs */
          {
            /* Note: see also TXindOptsProcessRawOptions() */
            if (!fdbi_setversion(atoi(value))) return(-1);
            return(0);
          }
        if (!strcmp(propi, "indexmaxsingle"))   /* KNG 991108 docs */
          {
            /* Note: see also TXindOptsProcessRawOptions() */
            if (!fdbi_setmaxsinglelocs(atoi(value))) return(-1);
            return(0);
          }
        if (!strcmp(propi, "dropwordmode"))     /* KNG 000110 docs */
          {                                     /* wtf move to apicp? */
            FdbiDropMode = atoi(value);
            return(0);
          }
        if (!strcmp(propi, "tracerecid"))       /* KNG 991119 docs */
          {
            EPI_OFF_T   off;

            off = TXstrtoepioff_t(value, CHARPN, &e, 16, &errnum);/* recid is hex as string */
            if (e == value || *e != '\0' || errnum != 0)
              {
                off = TXstrtoepioff_t(value, CHARPN, &e, 0, &errnum);     /* try any base */
                if (e == value || *e != '\0' || errnum != 0)
                  {
                    putmsg(MERR+UGE, CHARPN, "Garbled value `%s'for %s",
                           value, propi);
                    return(0);
                  }
              }
            TXsetrecid(&FdbiTraceRecid, off);
            return(0);
          }
        if (!strcmp(propi, "indexdump"))        /* KNG 000217 docs */
          {
            TxIndexDump = atoi(value);
            return(0);
          }
        if (!strcmp(propi, "indexmmap"))        /* KNG 000217 docs */
          {
            TxIndexMmap = atoi(value);
            return(0);
          }
        if (!strcmp(propi, "indexreadbufsz") || /* KNG 000313 docs */
            !strcmp(propi, "indexreadbufsize"))
          {
            if (!tx_parsesz(TXPMBUFPN, value, &h, propi, EPI_OS_SIZE_T_BITS,
			    TXbool_True))
              return(0);
            if (h < (EPI_HUGEINT)0) h = (EPI_HUGEINT)0;
            FdbiReadBufSz = (size_t)h;
            if (FdbiReadBufSz < 64) FdbiReadBufSz = 64; /* sanity check */
            return(0);
          }
        if (!strcmp(propi, "indexwritebufsz") || /* KNG 000303 docs */
            !strcmp(propi, "indexwritebufsize"))
          {
            if (!tx_parsesz(TXPMBUFPN, value, &h, propi, EPI_OS_SIZE_T_BITS,
			    TXbool_True))
              return(0);
            if (h < (EPI_HUGEINT)0) h = (EPI_HUGEINT)0;
            FdbiWriteBufSz = (size_t)h;
            if (FdbiWriteBufSz < 64) FdbiWriteBufSz = 64; /* sanity check */
            return(0);
          }
        if (!strcmp(propi, "indexmmapbufsz") || /* KNG 000602 docs */
            !strcmp(propi, "indexmmapbufsize"))
          {
            if (!tx_parsesz(TXPMBUFPN, value, &h, propi, EPI_OS_SIZE_T_BITS,
			    TXbool_True))
              return(0);
            if (h < (EPI_HUGEINT)0) h = (EPI_HUGEINT)0;
            TXindexmmapbufsz = (size_t)h;
            if (TXindexmmapbufsz < 64) TXindexmmapbufsz = 64; /* sanity chk */
            TXindexmmapbufsz_val = (size_t)0;   /* recompute */
            return(0);
          }
        if (!strcmp(propi, "indexslurp"))       /* KNG 011027 docs */
          {
            TxIndexSlurp = atoi(value);
            return(0);
          }
        if (!strcmp(propi, "indexappend"))      /* KNG 011120 docs */
          {
            TxIndexAppend = atoi(value);
            return(0);
          }
        if (!strcmp(propi, "indexwritesplit"))  /* KNG 020305 docs */
          {
            TxIndexWriteSplit = atoi(value);
            return(0);
          }
        if (!strcmp(propi, "indexbtreeexclusive"))
          {
            TXindexBtreeExclusive = atoi(value);
            return(0);
          }
        if (!strcmp(propi, "mergeflush"))       /* KNG 020115 docs */
          {
            TxMergeFlush = atoi(value);
            return(0);
          }
        if (!strcmp(propi, "uniqnewlist"))      /* KNG 000127 docs */
          {
	    TxUniqNewList = atoi(value);
            return(0);
          }
        if (strcmp(propi, "traceddcache") == 0)
          {
            TXtraceDdcache = (int)strtol(value, CHARPPN, 0);
            return(0);
          }
        if (strcmp(propi, "tracesqlparse") == 0)
	{
		int	newVal;

		newVal = TXstrtoi(value, NULL, &e, 0, &errnum);
		if (e == value || *e || errnum) goto badNum;
		TXtraceSqlParse = newVal;
		return(0);
	}
        if (strcmp(propi, "traceddcachetablename") == 0)
          {
            TXtraceDdcacheTableName = TXfree(TXtraceDdcacheTableName);
            TXtraceDdcacheTableName = TXstrdup(TXPMBUFPN, Fn, value);
            return(0);
          }
        if (!strcmp(propi, "tracekdbf"))        /* KNG 20070814 */
          {
            TXtraceKdbf = (int)strtol(value, CHARPPN, 0);
            return(0);
          }
        if (!strcmp(propi, "tracekdbffile"))    /* KNG 20070814 */
          {
            if (TXtraceKdbfFile != CHARPN) free(TXtraceKdbfFile);
            TXtraceKdbfFile = strdup(value);
            return(0);
          }
	if (strcmp(propi, "tracemetamorph") == 0)
	{
		TXtraceMetamorph = (int)strtol(value, CHARPPN, 0);
		return(0);
	}
	if (strcmp(propi, "tracerowfields") == 0)
	{
		if (!TXApp) goto noTXApp;
		return(TXAppSetTraceRowFields(TXPMBUFPN, TXApp, value) ?
		       0 : -1);
	}
	if (strcmp(propi, "tracelocksdatabase") == 0)
	{
		if (!TXApp) goto noTXApp;
		TXApp->traceLocksDatabase = TXfree(TXApp->traceLocksDatabase);
		if (*value)
			TXApp->traceLocksDatabase =
				TXstrdup(TXPMBUFPN, Fn, value);
		return(TXApp->traceLocksDatabase || !*value ? 0 : -1);
	}
	if (strcmp(propi, "tracelockstable") == 0)
	{
		if (!TXApp) goto noTXApp;
		TXApp->traceLocksTable = TXfree(TXApp->traceLocksTable);
		if (*value)
			TXApp->traceLocksTable =
				TXstrdup(TXPMBUFPN, Fn, value);
		return(TXApp->traceLocksTable || !*value ? 0 : -1);
	}
        if (!strcmp(propi, "kdbfiostats"))      /* KNG 000303 */
          {
            if (TxKdbfIoStatsFile != CHARPN) free(TxKdbfIoStatsFile);
            TxKdbfIoStatsFile = CHARPN;
            if (*value >= '0' && *value <= '9')
              TxKdbfIoStats = atoi(value);
            else
              {
                TxKdbfIoStats = 2;
                TxKdbfIoStatsFile = strdup(value);
              }
            return(0);
          }
	if (!strcmp(propi, "kdbfverify"))	/* KNG 010321 */
	  {
	    TxKdbfVerify = atoi(value);
            return(0);
	  }
	if (!strcmp(propi, "kdbfoptimizeon"))   /* KNG 20070418 */
	  {
	    return(kdbf_setoptimize(atoi(value), 1) ? 0 : -1);
	  }
	if (!strcmp(propi, "kdbfoptimizeoff"))  /* KNG 20070418 */
	  {
	    return(kdbf_setoptimize(atoi(value), 0) ? 0 : -1);
	  }
	if (!strcmp(propi, "btreeoptimizeon"))  /* KNG 20070423 */
	  {
	    return(TXbtsetoptimize(atoi(value), 1) ? 0 : -1);
	  }
	if (!strcmp(propi, "btreeoptimizeoff")) /* KNG 20070423 */
	  {
	    return(TXbtsetoptimize(atoi(value), 0) ? 0 : -1);
	  }
	if(!strcmp(propi, "wordc"))/* JMT 1999-11-10 *//* docs */
	{
		return(pm_setwordc(value) ? 0 : -1);
	}
	if(!strcmp(propi, "langc"))/* JMT 1999-11-10 *//* docs */
	{
		return(pm_setlangc(value) ? 0 : -1);
	}
	if (!strcmp(propi, "locksleepincrement"))
	{
		TXsetsleepparam(SLEEP_INCREMENT, atoi(value));
		return 0;
	}
	if (!strcmp(propi, "locksleepdecrement"))
	{
		TXsetsleepparam(SLEEP_DECREMENT, atoi(value));
		return 0;
	}
	if (!strcmp(propi, "max_index_text"))
	{
		/* Note: see also TXindOptsProcessRawOptions() */
		ddic->options[DDIC_OPTIONS_MAX_INDEX_TEXT] = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "max_rows"))/* JMT 2002-11-19 */
	{
		ddic->options[DDIC_OPTIONS_MAX_ROWS] = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "maxrows"))/* JMT 2002-11-19 */
	{
		ddic->options[DDIC_OPTIONS_MAX_ROWS] = atoi(value);
		return 0;
	}
	if (!strcmp(propi, "metamorphstrlstmode"))
	{
		TXMSM	msm;

		msm = TXstrToTxmsm(value);
		if (msm == TXMSM_UNKNOWN)
			putmsg(MERR + UGE, Fn,
				"Unknown metamorphstrlstmode value `%s'",
				value);
		else if (TXApp != TXAPPPN)
			TXApp->metamorphStrlstMode = msm;
		else
		{
		noTXApp:
			putmsg(MERR, Fn,
			       "Cannot apply setting `%s': No TXAPP object",
			       propi);
			return(-1);
		}
		return(0);
	}
	if (!strcmp(propi, "varchartostrlstsep") ||
	    !strcmp(propi, "varchar2strlstsep") ||
      !strcmp(propi, "varchartostrlstmode") ||
      !strcmp(propi, "varchar2strlstmode"))
	{
		TXstrlstCharConfig	sep = TXApp->charStrlstConfig;

		if(TXstrToTxvssep(TXPMBUFPN, "varchartostrlstsep", value, NULL, &sep) == 0)
    {
      TXApp->charStrlstConfig = sep;
    }
    else
    {
      return -1;
    }
		return(0);			/* success */
	}
  if(!strcmp(propi, "strlsttovarcharmode") ||
     !strcmp(propi, "strlst2varcharmode"))
  {
    if(!strnicmp(value, "json", 4))
    {
      TXApp->charStrlstConfig.fromStrlst = TXs2c_json_array;
    }
    else if(strnicmp(value, "delimited", 9))
    {
      TXApp->charStrlstConfig.fromStrlst = TXs2c_trailing_delimiter;
    }
    else return -1;
    return 0;
  }
  if (!strcmp(propi, "varchartostrlstfromjsonarray") ||
	    !strcmp(propi, "varchar2strlstfromjsonarray"))
	{
    rc = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi,
				       value, CHARPN, 3);
		if (rc == -1) return(-1);
    if(rc)
    {
      TXApp->charStrlstConfig.toStrlst |= TXc2s_json_array;
    }
    else
    {
      TXApp->charStrlstConfig.toStrlst &= ~(TXc2s_json_array);
    }
		return(0);			/* success */
	}
	if (!strcmp(propi, "multivaluetomultirow"))
	{
		rc = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi,
				       value, CHARPN, 3);
		if (rc == -1) return(-1);
		TXApp->multiValueToMultiRow = (byte)rc;
		return 0;
	}
	if (strcmp(propi, "nulloutputstring") == 0)
		return(TXfldSetNullOutputString(value) ? 0 : -1);
	if (!strcmp(propi, "timezone"))
	{
		/* Bug 6450: use tx_setenv() to avoid memleak: */
		if (!tx_setenv("TZ", value))
			return -1;
		tzset();
		return 0;
	}
	if (strcmp(propi, "eastpositive") == 0)
	{
		/* 1: true  0: false  -1: default */
		if (!TXsetEastPositive(rc = atoi(value)))
		{
			putmsg(MERR + UGE, Fn,
				"Could not set eastpositive to %d", rc);
			return(-1);		/* failure */
		}
		return(0);			/* success */
	}
	if (strcmp(propi, "mdparmodifyterms") == 0)
	{
		extern int	TxMdparModifyTerms;
		TxMdparModifyTerms = strtol(propi, CHARPPN, 0);
		return(0);			/* success */
	}
	if (strcmp(propi, "allowramtableblob") == 0)
	{
		if (!TXApp) goto noTXApp;
		intVal = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi,
					   value, CHARPN, 3);
		if (intVal != -1) TXApp->allowRamTableBlob = (byte)intVal;
		return(0);
	}
	if (strcmp(propi, "validatebtrees") == 0)
	{
		if (!TXApp) goto noTXApp;
		intVal = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi,
					   value, CHARPN, 1);
		if (intVal != -1) TXApp->validateBtrees = intVal;
		return(0);
	}
	if (strcmp(propi, "inmode") == 0)
	{					/* see also texglob.c */
		if (strcmpi(value, "intersect") == 0)
			TXApp->inModeIsSubset = 0;
		else if (strcmpi(value, "subset") == 0)
			TXApp->inModeIsSubset = 1;
		else
		{
		invalidPropValue:
			putmsg(MERR + UGE, Fn, "Invalid %s value `%s'",
			       propi, value);
			return(-1);		/* error */
		}
		return(0);
	}

	if (strcmp(propi, "legacyversion7orderbyrank") == 0)
	{
		n = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi, value,
				      CHARPN, 3);
		if (n == -1) return(-1);	/* error */
		TXApp->legacyVersion7OrderByRank = (byte)n;
		return(0);
	}

#ifdef TX_HEXIFY_BYTES_FEATURES
	if (strcmp(propi, "hexifybytes") == 0)
	{
		n = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi, value,
				      CHARPN, 3);
		if (n == -1) return(-1);	/* error */
		TXApp->hexifyBytes = (byte)n;
		return(0);
	}
#endif /* TX_HEXIFY_BYTES_FEATURES */

	if (strcmp(propi, "preloadblobs") == 0)
	{
		n = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi, value,
				      CHARPN, 3);
		if (n == -1) return(-1);	/* error */
		TXApp->preLoadBlobs = (byte)n;
		return(0);
	}

	/* this is a v6-to-7 transitional temp setting; const-true in v7+: */
	if (strcmp(propi, "enablesubsetintersect") == 0)
	{
		TXApp->subsetIntersectEnabled = atoi(value);
		return(0);
	}

	/* this is a v6-to-7 transitional temp setting; const-true in v7+: */
	if (strcmp(propi, "strlstrelopvarcharpromoteviacreate") == 0)
	{
		TXApp->strlstRelopVarcharPromoteViaCreate = atoi(value);
		return(0);
	}

	/* this is a v6-to-7 transitional temp setting; const-true in v7+: */
	if (strcmp(propi, "usestringcomparemodeforstrlst") == 0)
	{
		TXApp->useStringcomparemodeForStrlst = atoi(value);
		return(0);
	}

	/* this is a v6-to-7 transitional temp setting; const-true in v7+: */
	if (strcmp(propi, "dedupmultiitemresults") == 0)
	{
		TXApp->deDupMultiItemResults = atoi(value);
		return(0);
	}

	if (strcmp(propi, "indexvalues") == 0)
	{
		TXindexValues	iv;

		/* Note: see also TXindOptsProcessRawOptions() */
		iv = TXstrToIndexValues(value, NULL);
		if (iv == (TXindexValues)(-1))
		{
			putmsg(MERR + UGE, Fn,
				"Invalid indexvalues value `%s'", value);
			return(-1);		/* error */
		}
		TXApp->indexValues = iv;
		return(0);			/* success */
	}

	if (strcmp(propi, "compatibilityversion") == 0)
	{
		char	errBuf[1024];

		if (TXAppSetCompatibilityVersion(TXApp, value,
						 errBuf, sizeof(errBuf)))
			return(0);		/* success */
		putmsg(MERR + UGE, Fn,
		       "Cannot set compatibility version to `%s': %s",
		       value, errBuf);
		return(-1);			/* failed */
	}
	if (strcmp(propi, "failifincompatible") == 0)
	{
		n = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi, value,
				      CHARPN, 3);
		if (n == -1) return(-1);	/* error */
		TXApp->failIfIncompatible = (byte)n;
		return(0);			/* success */
	}
	if (strcmp(propi, "unalignedbufferwarning") == 0)
	{
		n = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi, value,
				      CHARPN, 3);
		if (n == -1) return(-1);	/* error */
		TXApp->unalignedBufferWarning = (byte)n;
		return(0);			/* success */
	}

	if (strcmp(propi, "unneededrexescapewarning") == 0)
	{
		extern int	TXunneededRexEscapeWarning;

		n = TXgetBooleanOrInt(TXPMBUFPN, SQLpropGroup, propi, value,
				      CHARPN, 3);
		if (n == -1) return(-1);	/* error */
		TXunneededRexEscapeWarning = (byte)n;
		return(0);			/* success */
	}

	if (TX_PWENCRYPT_METHODS_ENABLED(TXApp) &&
	    strcmp(propi, "defaultpasswordencryptionmethod") == 0)
	{
		TXpwEncryptMethod	method;

		method = TXpwEncryptMethodStrToEnum(value);
		if (method == TXpwEncryptMethod_Unknown)
		{
			txpmbuf_putmsg(TXPMBUFPN, MERR + UGE, __FUNCTION__,
				    "Unknown password encryption method `%s'",
				       value);
			return(-1);
		}
		if (!TXAppSetDefaultPasswordEncryptionMethod(TXPMBUFPN,
							     TXApp, method))
			return(-1);
		return(0);
	}

	if (TX_PWENCRYPT_METHODS_ENABLED(TXApp) &&
	    strcmp(propi, "defaultpasswordencryptionrounds") == 0)
	{
		int	newVal;

		newVal = TXstrtoi(value, NULL, NULL,
				  (0 | TXstrtointFlag_ConsumeTrailingSpace |
				       TXstrtointFlag_TrailingSourceIsError),
				  &errnum);
		if (errnum) goto badNum;
		if (!TXAppSetDefaultPasswordEncryptionRounds(TXPMBUFPN,
							     TXApp, newVal))
			return(-1);
		return(0);
	}

	if (!strcmp(propi, "debugbreak"))
	{
#if defined(TX_DEBUG) && defined(_WIN32)
		debugbreak();
#else
		putmsg(MWARN, Fn, "Internal variable %s is not enabled", prop);
#endif
		return 0;
	}
	if (!strcmp(propi, "debugmalloc"))
	{
#if defined(TX_DEBUG) && defined(_WIN32) && !defined(TX_NO_CRTDBG_H_INCLUDE)
		int tmpFlag;
		switch(atoi(value))
		{
		case 0: /* WTF: Disable everything */
			break;
		case 1:
			tmpFlag = _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );
			/* Turn on leak-checking bit */
			tmpFlag |= _CRTDBG_LEAK_CHECK_DF;
			/* Turn on check-always bit */
			tmpFlag |= _CRTDBG_CHECK_ALWAYS_DF;
			/* Set flag to the new value */
			_CrtSetDbgFlag( tmpFlag );
			break;
		case 2:
#  ifdef _CRT_RPTHOOK_INSTALL
			_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL,
				dbghktoputmsg);
#  else /* !_CRT_RPTHOOK_INSTALL */
			putmsg(MERR + UGE, Fn,
			"_CrtSetReportHook2() not supported in this version");
#  endif /* !_CRT_RPTHOOK_INSTALL */
			break;
		}
#else /* !(TX_DEBUG && _WIN32 && !TX_NO_CRTDBG_H_INCLUDE) */
		putmsg(MWARN, Fn, "Internal variable %s is not enabled", prop);
#endif /* !(TX_DEBUG && _WIN32 && !TX_NO_CRTDBG_H_INCLUDE) */
		return 0;
	}
        if (!strcmp(propi, "betafeatures"))
        {
                return setbetafeature(ddic, value, 1);
        }
        if (!strcmp(propi, "nobetafeatures"))
        {
                return setbetafeature(ddic, value, 0);
        }
        if (!strcmp(propi, "jsonfmt"))
        {
                return TXsetjsonfmt(value);
        }

#ifdef NEVER
	putmsg(999, Fn, "So we are trying to set %s to %s", prop, value);
#endif
	putmsg(MWARN, Fn, "Internal variable %s is not currently known", prop);
	return -1;
}

/******************************************************************/

static int
setbetafeature(ddic, value, onoff)
DDIC *ddic;
char *value;
int onoff;
{
	static const char	fn[] = "setoptimize";
	char *x;
  int rc = 0;

	x = strtok(value, " ,()");
	while (x)
	{
		if(!strcmpi(x, "json")) {
			TXApp->betafeatures[BETA_JSON] = onoff;
		} else {
			putmsg(MWARN, fn,
				"Unknown beta feature `%s'", x);
      rc = -1;
    }
		x = strtok(NULL, ",()");
  }
  return rc;
}
/******************************************************************/

static int
setoptimize(ddic, value, onoff)
DDIC *ddic;
char *value;
int onoff;
{
	static const char	fn[] = "setoptimize";
	char *x;

	x = strtok(value, " ,()");
	while (x)
	{
		if(!strcmpi(x, "join"))/* JMT 1999-06-10 *//* docs */
			ddic->optimizations[OPTIMIZE_JOIN] = onoff;
		else if(!strcmpi(x, "compoundindex"))/* JMT 1999-06-10 *//* docs */
			ddic->optimizations[OPTIMIZE_COMPOUND_INDEX] = onoff;
		else if(!strcmpi(x, "copy"))/* JMT 1999-06-10 */
			ddic->optimizations[OPTIMIZE_COPY] = onoff;
		else if(!strcmpi(x, "countstar"))/* JMT 1999-06-10 *//* docs */
			ddic->optimizations[OPTIMIZE_COUNT_STAR] = onoff;
		else if(!strcmpi(x, "minimallocking"))/* JMT 1999-06-10 *//* docs */
			ddic->optimizations[OPTIMIZE_MINIMAL_LOCKING] = onoff;
		else if(!strcmpi(x, "groupby"))/* JMT 1999-06-10 *//* docs */
			ddic->optimizations[OPTIMIZE_GROUP] = onoff;
		else if(!strcmpi(x, "faststats"))/* JMT 1999-08-23 *//* docs */
			ddic->optimizations[OPTIMIZE_FASTSTATS] = onoff;
		else if(!strcmpi(x, "readlock"))/* JMT 1999-08-23 *//* docs */
			ddic->optimizations[OPTIMIZE_READLOCK] = onoff;
		else if(!strcmpi(x, "analyze"))/* JMT 1999-10-25 *//* docs */
			ddic->optimizations[OPTIMIZE_ANALYZE] = onoff;
		else if(!strcmpi(x, "skipahead"))/* JMT 1999-11-02 *//* docs */
			ddic->optimizations[OPTIMIZE_SKIPAHEAD] = onoff;
		else if(!strcmpi(x, "auxdatalen"))/* JMT 2000-01-04 */
			ddic->optimizations[OPTIMIZE_AUXDATALEN] = onoff;
		else if(!strcmpi(x, "indexonly"))/* JMT 2000-01-27 */
			ddic->optimizations[OPTIMIZE_INDEXONLY] = onoff;
		else if(!strcmpi(x, "fastmmindexupdate"))/* JMT 2000-02-23 */
			ddic->optimizations[OPTIMIZE_MMIDXUPDATE] = onoff;
		else if(!strcmpi(x, "indexdatagroupby"))/* JMT 2000-02-23 */
			ddic->optimizations[OPTIMIZE_INDEXDATAGROUP] = onoff;
		else if(!strcmpi(x, "likeand"))/* JMT 2002-05-07 */
			ddic->optimizations[OPTIMIZE_LIKE_AND] = onoff;
		else if(!strcmpi(x, "likeandnoinv"))/* JMT 2002-05-07 */
			ddic->optimizations[OPTIMIZE_LIKE_AND_NOINV] = onoff;
		else if(!strcmpi(x, "likewithnots"))/* JMT 2002-12-04 */
			ddic->optimizations[OPTIMIZE_LIKE_WITH_NOTS] = onoff;
		else if(strcmpi(x, "optimization18") == 0 ||/*JMT 2003-08-08*/
			strcmpi(x, "shortcuts") == 0)
			ddic->optimizations[OPTIMIZE_SHORTCUTS] = onoff;
		else if(!strcmpi(x, "indexbatchbuild"))	/* KNG 20070424 */
			ddic->optimizations[OPTIMIZE_INDEX_BATCH_BUILD]=onoff;
		else if(!strcmpi(x, "linearrankindexexps"))/* KNG 20080104 */
			ddic->optimizations[OPTIMIZE_LINEAR_RANK_INDEX_EXPS]=onoff;
		else if(!strcmpi(x, "pointersintostrlst"))
			ddic->optimizations[OPTIMIZE_PTRS_TO_STRLSTS]=onoff;
		else if(!strcmpi(x, "sortedvarflds"))
			ddic->optimizations[OPTIMIZE_SORTED_VARFLDS]=onoff;
		else if(strcmpi(x, "indexvirtualfields") == 0)/*KNG 20111202*/
			ddic->optimizations[OPTIMIZE_INDEX_VIRTUAL_FIELDS]=onoff;
		else if (strcmpi(x, "indexdataonlycheckpredicates") == 0)
			ddic->optimizations[OPTIMIZE_INDEX_DATA_ONLY_CHECK_PREDICATES] = onoff;
		else if (strcmpi(x, "groupbymem") == 0)
			ddic->optimizations[OPTIMIZE_GROUP_BY_MEM] = onoff;
		else if (strcmpi(x, "likehandled") == 0)
			ddic->optimizations[OPTIMIZE_LIKE_HANDLED] = onoff;
		else if (strcmpi(x, "sqlfunctionparametercache") == 0)
			ddic->optimizations[OPTIMIZE_SQL_FUNCTION_PARAMETER_CACHE] = onoff;
		else
			putmsg(MWARN, fn,
				"Unknown optimization `%s'", x);
		x = strtok(NULL, ",()");
	}
	return 0;
}

/******************************************************************/

static int
setmessages(ddic, value, onoff)
DDIC *ddic;
char *value;
int onoff;
{
	char *x;

	x = strtok(value, " ,()");
	while (x)
	{
		if(!strcmpi(x, "duplicate"))/* JMT 1999-08-30 */
			ddic->messages[MESSAGES_DUPLICATE] = onoff;
		else if(!strcmpi(x, "indexuse"))/* JMT 1999-09-23 */
			ddic->messages[MESSAGES_INDEXUSE] = onoff;
		else if(!strcmpi(x, "timefdbi"))/* JMT 2002-10-29 */
			ddic->messages[MESSAGES_TIME_FDBI] = onoff;
		else if(!strcmpi(x, "dumpqnode"))/* JMT 2002-10-29 */
			/* We set it to 2 initially, to skip the
			 * message for this SQL `set' statement because
			 * user probably wants the next statement.
			 * But if already set, then they *do* want it;
			 * set to 1:
			 */
			ddic->messages[MESSAGES_DUMP_QNODE] = (onoff ?
			   (ddic->messages[MESSAGES_DUMP_QNODE] ? 1 : 2) : 0);
		else if(!strcmpi(x, "dumpqnodefetch"))
			ddic->messages[MESSAGES_DUMP_QNODE_FETCH] = onoff;
		else
			putmsg(MWARN, "set message",
				"Unknown message %s", x);
		x = strtok(NULL, ",()");
	}
	return 0;
}

/******************************************************************/

static int
setoption(ddic, value, onoff)
DDIC *ddic;
char *value;
int onoff;
{
	char *x;

	x = strtok(value, " ,()");
	while (x)
	{
		if(!strcmpi(x, "triggers"))/* JMT 2000-06-20 */
			ddic->options[DDIC_OPTIONS_NO_TRIGGERS] = onoff;
		else if(!strcmpi(x, "indexcache"))/* JMT 2000-06-20 */
			ddic->options[DDIC_OPTIONS_INDEX_CACHE] = onoff;
		else if(!strcmpi(x, "ignoremissingfields"))/* JMT 2004-05-04 */
			ddic->options[DDIC_OPTIONS_IGNORE_MISSING_FIELDS] = onoff;
		else
			putmsg(MWARN, "set option",
				"Unknown option %s", x);
		x = strtok(NULL, ",()");
	}
	return 0;
}

/******************************************************************/
/* The idea here is to reset all the settable properties to their
   initialized state; */
int
TXresetproperties(ddic)
DDIC *ddic;
{
	int	ret = 0;

	/* Index expressions */
	TXresetexpressions();

        /* index tmp   KNG 980515 */
        TXresetindextmp();

	/* Indexblock */
#ifdef SMALL_MEM
	TXsetblockmax(32000);
#else
	TXsetblockmax(100000);
#endif
	/* Indirectcompat */
	TXindcompat("off");

	TXindexWithin = TXindexWithinFlag_Default;

	/* should match texglob.c default: */
	TXtableReadBufSz = (size_t)16*(size_t)1024;

	TXbtreedump = 0;

	if (TXbtreelog != CHARPN)
	{
		free(TXbtreelog);
		TXbtreelog = CHARPN;
	}

	/* tablespace */
	if(ddic->tbspc && *ddic->tbspc)
	{
		free(ddic->tbspc);
		ddic->tbspc=strdup("");
	}

	/* Indexspace */
	if(ddic->indspc && *ddic->indspc)
	{
		free(ddic->indspc);
		ddic->indspc=strdup("");
	}

	/* Nolocking */
	ddic->nolocking=0;

	/* Lock mode */
	lockmode(ddic, LOCK_AUTOMATIC);

	/* fairlock */
	TXsetfairlock(1);

	/* datefmt */
	TXsetdatefmt("");

	/* infthresh */
	TXsetinfinitythreshold(-1);

	/* infpercent */
	TXsetinfinitypercent(-1);

	/* likeprows */
	TXnlikephits=100;

	/* likepallmatch */
        if (!TXapicpSetLikepAllMatch(TXbool_False)) ret = -1;
        /* likepallmatch, leadbias, order, proximity, maxdist */
        rppm_resetvals();
        /* likepobeyintersects */
	if (!TXapicpSetLikepObeyIntersects(TXbool_False)) ret = -1;

	/* locksleepmethod */
	TXsetsleepparam(SLEEP_METHOD, 1);

	/* locksleeptime */
	TXsetsleepparam(SLEEP_MULTIPLIER, 2);

	/* locksleepmaxtime */
	TXsetsleepparam(SLEEP_MAXSLEEP, 99);

	/* lockverbose */
	TXsetlockverbose(0);

	/* bubble */
	ddic->no_bubble = 0;

	/* paramchk */
	TXsetDiscardUnsetParameterClauses(TXbool_False);

	/* predopttype */
	TXpredopttype(2);

	/* cleanupwait */
#ifdef _WIN32
	TXSetCleanupWait(20);
#endif

	/* triggermode */
	TXexttrig = 0;

        /* indexmem */
        TXindexmemUser = (size_t)0;

        /* indexmeter */
        TXindexmeter = TXMDT_NONE;

	/* likepmode */
	TXlikepmode = 1;

	/* defaultlike */
	TXdefaultlike = FOP_MM;

        /* traceindex/traceidx/indextrace */
        FdbiTraceIdx = 0;

        /* tracerppm */
        TXtraceRppm = 0;

        /* indexmaxsingle */
        /* indexversion */
        fdbi_setversion(0);                     /* sets both to default */

        /* dropwordmode */
        FdbiDropMode = 0;

        /* tracerecid */
        TXsetrecid(&FdbiTraceRecid, (off_t)(-1));

        /* indexdump */
        TxIndexDump = 0;

        /* indexmmap */
        TxIndexMmap = 1;

        /* indexreadbufsz */
        FdbiReadBufSz = (size_t)(64*1024);

        /* indexwritebufsz */
        FdbiWriteBufSz = (size_t)(128*1024);

        /* indexmmapbufsz */
        TXindexmmapbufsz = TXindexmmapbufsz_val = (size_t)0;

        /* indexslurp */
        TxIndexSlurp = 1;

        /* indexappend */
        TxIndexAppend = 1;

        /* indexwritesplit */
        TxIndexWriteSplit = 1;

        /* indexbtreeexclusive; should match texglob.c default: */
        TXindexBtreeExclusive = 1;

        /* mergeflush */
        TxMergeFlush = 1;

        /* uniqnewlist */
        TxUniqNewList = 0;

        /* kdbfiostats */
        TxKdbfIoStats = 0;
        if (TxKdbfIoStatsFile != CHARPN) free(TxKdbfIoStatsFile);
        TxKdbfIoStatsFile = CHARPN;

	/* kdbfverify */
	TxKdbfVerify = 0;

	/* kdbfoptimizeon/kdbfoptimizeoff */
	kdbf_setoptimize(0, 2);			/* (..., 2) == defaults */

	/* btreeoptimizeon/btreeoptimizeoff */
	TXbtsetoptimize(0, 2);			/* (..., 2) == defaults */

	TXsetVerbose(0);

	TXsetEastPositive(-1);			/* -1 == default */

	TXddicdefaultoptimizations(ddic);

	ddic->options[DDIC_OPTIONS_MAX_ROWS] = 0;

	TXApp->indexValues = TX_INDEX_VALUES_DEFAULT;

	TXApp->allowRamTableBlob = TX_ALLOW_RAM_TABLE_BLOB_DEFAULT;

	TXApp->inModeIsSubset = TX_IN_MODE_IS_SUBSET_DEFAULT(TXApp);

	TXApp->legacyVersion7OrderByRank =
		TX_LEGACY_VERSION_7_ORDER_BY_RANK_DEFAULT(TXApp);

	/* see also TXinitapp(): */
	TXApp->unalignedBufferWarning = 1;

	return(ret);
}

/******************************************************************/

int
TXsetproperties(ddic, set)
DDIC	*ddic;
char	*set;
{
	char	*a, *b, *x;

	a = set + 3;
	b = strchr(a, ' ') +1;
	strchr(a, ' ')[0] = '\0';
	if (b[0] == '\'')
	{
		x = strchr(b+1, '\'');
		if (x)
			x[0] = '\0';
		return setprop(ddic, a, b+1);
	}
	else
		return setprop(ddic, a, b);
}
