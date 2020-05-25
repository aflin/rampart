/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#ifdef _WIN32
#  include <winternl.h>			/* for TXsetProcessDescription() */
#endif /* _WIN32 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"
#include "jansson.h"

/*
	This file contains various global variables used throughout
	Texis.
*/

/* TXinitapp() sets these: */
char       TxConfFile[TxConfFile_SZ] = ""; /* texis.cnf file */
CONFFILE	*TxConf = CONFFILEPN;	        /* handle to it */

int		TxOrgArgc = 0;
char		**TxOrgArgv = CHARPPN;		/* copy of original args */
static int	TxLiveArgc = 0, TxLiveArgvNonContiguousIndexStart = 0;
static char	**TxLiveArgv = NULL;		/* live (ps-able) arg ptrs */
static size_t	TxLiveArgvContiguousSize = 0;
static char	*TxLiveProcessDescription = NULL; /* alloc'd; sans prefix */
#ifdef _WIN32
static wchar_t	*TxOrgCommandLine = NULL;	/* alloc'd */
static size_t	TxOrgCommandLineByteLen = 0;
static size_t	TxOrgCommandLineMaxByteLen = 0;
#endif /* _WIN32 */
static char	*TxProcessDescriptionPrefix = NULL;	/* alloc'd */
static TXbool	TxProcessDescriptionPrefixIsDefault = TXbool_True;
/* Fully alloced copy of just global args, e.g. --install-dir: */
int		TxGlobalOptsArgc = 0;
char		**TxGlobalOptsArgv = CHARPPN;
/* argv[0], PATH-determined.  Private use; use TXgetExeFileName(): */
char		*TxExeFileName = CHARPN;
const char * const	TxExitDescList[TXEXIT_NUM+1] =
{
#undef I
#define I(tok, desc)    desc,
TXEXIT_SYMBOLS_LIST
#undef I
  CHARPN                                /* must be last */
};


/******************************************************************/
/* TXAPP (see texint.h) contains Texis app globals */

TXAPP *TXApp = TXAPPPN;

/******************************************************************/
/* Discard NULL parameters' clauses.  Setting this variable to 1 will allow
   unset parameters to be passed through, and if in the where
   clause will be ignored during evaluation.
*/

static TXbool	TXdiscardUnsetParameterClauses = TXbool_False;

int	TXverbosity = 0;	/* Verbose level */

int	TXlikepstartwt = 0;
long	TXlikeptime = 0;        /* How long to spend in likep */

/******************************************************************/
/*    This variable sets the maximum limit on the btree, above
 *    which it will not be used. */

int	TXbtreemaxpercent = 50;
int	TXmaxlinearrows = 1000;  /* Number of rows we will linear search */

/* WTF these currently only apply to the index search: */
/* see eng/mm3e.c for TXwildsufmatch, TXwildoneword */
int	TXallineardict =	/* allow linear dictionary searches */
#if TX_VERSION_MAJOR >= 6
  0;
#else /* TX_VERSION_MAJOR < 6 */
  1;
#endif /* TX_VERSION_MAJOR < 6 */

int	TXindexminsublen = 2;	/* minimum string length to search in index */

TXindexWithinFlag	TXindexWithin = TXindexWithinFlag_Default;

/* tablereadbufsz: read buffer size for tables.  Currently only used if
 * indexbatchbuild optimization is on (metamorph index build), or for
 * regular index builds.  See also TXresetproperties().
 */
size_t	TXtableReadBufSz = (size_t)16*(size_t)1024;

/* indexbtreeexclusive: nonzero to use BT_EXCLUSIVEACCESS when possible
 * in index B-trees.  Currently only used by Metamorph index create/update.
 * See also TXresetproperties():
 */
int     TXindexBtreeExclusive = 1;

/* TXbtreedump: dump B-tree indexes to files named after index:
 * What to dump (see also btdump()):
 *   bit  0: issue putmsg about where dump file(s) are
 *   bit  1: .btree:   copy of in-mem BTREE struct
 *   bit  2: .btrcopy: copy of .btr file
 *   bit  3: .cache:   page cache: {BCACHE BPAGE} x t->cachesize
 *   bit  4: .his:     history: {BTRL} x t->cachesize
 *   bit  5: .core:    fork() and dump core
 * When to dump:
 *   bit 16: At "Cannot insert value" messages
 *   bit 17: At "Cannot delete value" messages
 *   bit 18: At "Trying to insert duplicate value" messages
 */
int     TXbtreedump = 0;

/* TXbtreelog: log this B-tree's activity to .log file.
 * Syntax: {on|off}=[/dir/]file[.btr]
 */
char    *TXbtreelog = CHARPN;

/* If non-NULL, these are current file:line for btreelog msgs, e.g. Vortex.
 * Not SQL properties; set by Vortex internally:
 */
char    **TXbtreelog_srcfile = CHARPPN;
int     *TXbtreelog_srcline = INTPN;

/* Current DBTBL for btreelog.  Not a SQL property: set internally: */
DBTBL	*TXbtreelog_dbtbl = NULL;

#ifdef NO_NEW_CASE
int	TXigncase = 0;
#endif
int	TXrowsread = 0;
int	TXallowidxastbl = 0;
int	TXindexchunk = 0;
size_t  TXindexmemUser = 0;     /* indexing merge mem limit */
size_t  TXindexmmapbufsz = 0, TXindexmmapbufsz_val = 0; /* index mmap limit */
TXMDT   TXindexmeter = TXMDT_NONE;    /* show progress meter while indexing */
TXMDT   TXcompactmeter = TXMDT_NONE;    /* ALTER TABLE ... COMPACT meter */
int	TXnlikephits = 100;
int	TXlikepmode = 1;
int	TXlikermaxthresh=0;
int	TXlikermaxrows=1000;
int	TXlikepthresh=0;
int	TXlikepminsets=0;
int	TXseq=0;
int	TXunlocksig = 0;	/* Signal to issue on unlock. For cleanup etc*/
int	TXdefaultlike = FOP_MM;
int	TXsingleuser = 0;       /* Go into full single user mode. */
int	TXbtreecache = 20;
#ifdef unix
static CONST char TXPatchedInstallPath[1028] = "@(#)installpath=/usr/local/morph3\0";
#elif defined(_WIN32)
static CONST char TXPatchedInstallPath[1028] = "@(#)installpath=c:\\morph3\0";
#endif /* unix */
char    TXInstallPath[1028] = "";
int	TXinstallpathset = 0;	/* 0: no  1: install path  2: custom */
char	*TXMonitorPath = NULL;
int	TXintsem = -1;
TXbool	TXclearStuckSemaphoreAlarmed = TXbool_False;
int	TXlicensehits = 1;
int	TXverbosepredvalid = 1;
int	TXdisablenewlist = 0;
int	TXverifysingle = -2;
int	TXlockread = 1;
const char TXWhitespace[] = " \t\r\n\v\f";

/* tracedumptable bit flags; see cpdb.c */
int	TXtracedumptable = 0;

/* [Texis] Write Timeout value; 0 == no retry: */
double	TXwriteTimeout = 5.0;

#ifdef _MSC_VER
#  ifdef TX_DEBUG
int	TXexceptionbehaviour = EXCEPTION_CONTINUE_SEARCH;
#  else /* TX_DEBUG */
int	TXexceptionbehaviour = EXCEPTION_EXECUTE_HANDLER;
#  endif /* TX_DEBUG */
#endif /* _MSC_VER */

#ifdef _WIN32
/* TXisTexisMonitorService:  -1 = not yet known  0 = no  1 = yes */
static int	TXisTexisMonitorService = -1;
#endif /* _WIN32 */

#if defined(MEMDEBUG) || defined(TX_DEBUG)
static int	TXfreeMemAtExit = 1;	       	/* 1 for cleaner memdebug */
#else /* !(MEMDEBUG || TX_DEBUG) */
static int	TXfreeMemAtExit = 0;		/* not needed for valgrind */
#endif /* !(MEMDEBUG || TX_DEBUG) */

static CONST char	TxInvalidApicpVal[] = "Invalid [Apicp] %s value `%s'";
static CONST char	TxApicpGroup[] = "[Apicp] setting";
static const char	Whitespace[] = " \t\r\n\v\f";

/* - - - - - - - - - - - - - - - - bolton stuff  - - - - - - - - - - - - - - */

#ifndef _WIN32
char	*VhServerRoot = TXINSTALLPATH_VAL;	/* must be absolute */
int	VhServerRootIsExpanded = 1;
char	*VhDocumentRoot = "htdocs";
int	VhDocumentRootIsExpanded = 0;
#endif /* !_WIN32 */

/* - - - - - - - - - - - - - - - - Vortex stuff - - - - - - - - - - - - - - */

int	VxErrFd = STDERR_FILENO;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* [Monitor] Run Level:
 * bit 0: run as license/stats monitor
 * bit 1: exit if default db disappears
 */
static int	TXmonitorRunLevel = 1;
static int	TXgotMonitorRunLevel = 0;

int
TXgetMonitorRunLevel(void)
{
	if (!TXgotMonitorRunLevel && TxConf)
	{
		TXmonitorRunLevel = getconfint(TxConf, "Monitor", "Run Level",
						TXmonitorRunLevel);
		TXgotMonitorRunLevel = 1;
	}
	return(TXmonitorRunLevel);
}

int
TXgetIsTexisMonitorService(void)
/* Returns 1 if this process is running as the Texis Monitor service,
 * 0 if not, -1 if unknown.
 */
{
#ifdef _WIN32
	return(TXisTexisMonitorService);
#else /* !_WIN32 */
	return(0);				/* no services in Unix */
#endif /* !_WIN32 */
}

#ifdef _WIN32
int
TXsetIsTexisMonitorService(TXPMBUF *pmbuf, int isTexisMonitorService)
/* `isTexisMonitorService' should be -1, 0 or 1.
 * Returns 0 on error.
 */
{
	static const char	fn[] = "TXsetIsTexisMonitorService";

	switch (isTexisMonitorService)
	{
	case -1:
	case 0:
	case 1:
		TXisTexisMonitorService = isTexisMonitorService;
		return(1);			/* success */
	default:
		txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "Invalid value %d",
			       isTexisMonitorService);
		return(0);			/* error */
	}
}
#endif /* _WIN32 */

char *
TXgetmonitorpath()
{
	if(!TXMonitorPath)
	{
#ifdef unix
		TXMonitorPath=TXstrcat2(TXINSTALLPATH_VAL, "/bin/monitor");
#elif defined(_WIN32)
		TXMonitorPath=TXstrcat2(TXINSTALLPATH_VAL, "\\monitor.exe");
#else
                error error error;
#endif
	}
	return TXMonitorPath;
}

int
TXsetinstallpath(buf)
char *buf;	/* (in) (opt.) install dir */
/* Sets install dir for this process to `buf'.
 * `buf' may be NULL to set installed path (e.g. patched dir under Unix,
 * registry value under Windows).
 * Returns 0 if ok, -1 on error.
 * NOTE: should use a TXPMBUF arg for putmsgs.
 */
{
#ifdef _WIN32
	DWORD type, sz;
	LONG  rc;
	HKEY hkey;
#endif /* _WIN32 */

	if(buf)
	{
		TXstrncpy(TXINSTALLPATH_VAL, buf, 1015);
		TXinstallpathset = 2;
		TXMonitorPath = TXfree(TXMonitorPath);
		return 0;
	}
	else
	{
#ifdef unix
		TXstrncpy(TXInstallPath, TXPatchedInstallPath, sizeof(TXInstallPath));
		TXinstallpathset = 1;
		TXMonitorPath = TXfree(TXMonitorPath);
		return 0;
#elif defined(_WIN32)
		sz = 1015;
		type = 0;
		rc = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Thunderstone Software",
			0, KEY_QUERY_VALUE, &hkey);
		if(rc == 0)
		{
			rc = RegQueryValueEx(hkey,
				"InstallDir",
				NULL,
				&type,
				TXINSTALLPATH_VAL,
				&sz);
			RegCloseKey(hkey);
			TXinstallpathset = 1;
			TXMonitorPath = TXfree(TXMonitorPath);
			if(rc == 0 && type == REG_SZ)
			{
				/* Bug 5190: remove trailing slash: */
				char	*s, *e;

				s = TXINSTALLPATH_VAL;
				e = s + strlen(s);
				while (e > s + 1 &&
				       TX_ISPATHSEP(e[-1]) &&
				       !(TX_ISALPHA(*s) && s[1] == ':' &&
					 e == s + 2))
					*(--e) = '\0';
				return(0);
			}
		}
		/* fall back: */
		TXstrncpy(TXInstallPath, TXPatchedInstallPath, sizeof(TXInstallPath));
		TXinstallpathset = 1;
		TXMonitorPath = TXfree(TXMonitorPath);
		return -1;
#else /* !unix && !_WIN32 */
                error error error;
#endif /* !unix && !_WIN32 */
	}
}

int
TXregsetinstallpath(buf)
char *buf;	/* (in) (opt.) install dir */
/* Like TXsetinstallpath(buf), but also sets registry.  Windows-only function.
 * Returns 0 if ok, -1 on error.
 */
{
	static CONST char	fn[] = "TXregsetinstallpath";
	int	ret = -1;
#ifdef _WIN32
	LONG  rc;
	TXERRTYPE	rcErrNum;
	HKEY hkey;
	char installpath[PATH_MAX + 1];
	char *t;
	HMODULE hModule;

	if(buf)
	{
		TXstrncpy(installpath, buf, PATH_MAX);
	}
	else
	{
		hModule = GetModuleHandle("monitor.exe");
		if(hModule == NULL)
		{
			putmsg(MERR, fn,
				"Could not get executable module handle: %s",
				TXstrerror(TXgeterror()));
			return -1;
		}
		if (GetModuleFileName(hModule, installpath, PATH_MAX) >=
			PATH_MAX)
		{
			putmsg(MERR, fn, "Executable filename too long");
			return -1;
		}
		t = strstr(installpath, "\\monitor.exe");
		if (t)
			*t = '\0';
		else if ((t = strstr(installpath, "/monitor.exe")) != CHARPN)
			*t = '\0';
	}
	rc = RegCreateKeyEx(HKEY_LOCAL_MACHINE,
			    "Software\\Thunderstone Software",
			    0,
			    NULL,
			    REG_OPTION_NON_VOLATILE,
			    KEY_SET_VALUE,
			    NULL,
			    &hkey,
			    NULL);
	rcErrNum = rc;				/* not TXgeterror() */
	if(rc == ERROR_SUCCESS)
	{
		size_t	len;

		len = strlen(installpath);
		if (len > 0xffffffff - 1)
			rc = ERROR_INVALID_PARAMETER;
		else
			rc = RegSetValueEx(hkey, "InstallDir", 0, REG_SZ,
					   installpath, (DWORD)len + 1);
		rcErrNum = rc;			/* not TXgeterror() */
		RegCloseKey(hkey);
	}
	if (rc != ERROR_SUCCESS)
	{
		putmsg(MERR, fn, "Could not set InstallDir in registry: %s",
			TXstrerror(rcErrNum));
		if (rc == ERROR_ACCESS_DENIED &&
		    TXisUserAdmin() <= 0)	/* not Administrator */
			putmsg(MINFO, NULL, "Try running as Administrator");
	}
	else
	{
		printf("InstallDir set in registry to `%s'\n",
		       installpath);
		ret = 0;			/* success so far */
	}
	if (ret == 0 && TXsetinstallpath(installpath) != 0)
	{
		putmsg(MWARN, fn, "Could not set install dir for process");
		ret = -1;
	}
#else /* !_WIN32 */
	(void)buf;
	putmsg(MERR + UGE, fn, "No registry on this platform");
#endif /* !_WIN32 */
	return ret;
}

/* ----------------------- texis.ini [Apicp] functions -------------------- */

static int txApicpInitByteBoolean ARGS((TXPMBUF *pmbuf, CONST char *name,
					size_t varOffset, CONST char *val));
static int
txApicpInitByteBoolean(pmbuf, name, varOffset, val)
TXPMBUF		*pmbuf;		/* (in, opt.) buffer for putmsgs */
CONST char	*name;		/* (in, opt.) name of setting */
size_t		varOffset;	/* (in) APICP offset of byte setting to set */
CONST char	*val;		/* (in) value to parse */
/* Handles texis.ini setting of a byte-type boolean APICP field.
 * Returns 0 on error (and does not modify setting).
 */
{
	int	n;

	n = TXgetBooleanOrInt(pmbuf, TxApicpGroup, name, val, CHARPN, 3);
	if (n != -1)				/* success: set the value */
	{
		*((byte *)&TxApicpDefault + varOffset) = n;
		/* Let later modifiers know this was set via texis.ini: */
		*((byte *)&TxApicpDefaultIsFromTexisIni + varOffset) = 1;
	}
	return(n != -1);
}

static int txApicpInitIntNum ARGS((TXPMBUF *pmbuf, CONST char *name,
				   size_t varOffset, CONST char *val));
static int
txApicpInitIntNum(pmbuf, name, varOffset, val)
TXPMBUF		*pmbuf;		/* (in, opt.) buffer for putmsgs */
CONST char	*name;		/* (in, opt.) name of setting */
size_t		varOffset;	/* (in) APICP offset of int setting to set */
CONST char	*val;		/* (in) value to parse */
/* Handles texis.ini setting of an int-type numeric APICP field.
 * Returns 0 on error (and does not modify setting).
 */
{
	long	n;
	char	*e;

	n = strtol(val, &e, 0);
	if (e == val || *e != '\0')		/* bad int value */
	{
		txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN, TxInvalidApicpVal,
			       name, val);
		return(0);			/* failure */
	}
	/* success; set the value: */
	*(int *)((byte *)&TxApicpDefault + varOffset) = n;
	*(int *)((byte *)&TxApicpDefaultIsFromTexisIni + varOffset) = 1;
	return(1);
}

static int txApicpInitStr ARGS((TXPMBUF *pmbuf, CONST char *name,
				size_t varOffset, CONST char *val));
static int
txApicpInitStr(pmbuf, name, varOffset, val)
TXPMBUF		*pmbuf;		/* (in, opt.) buffer for putmsgs */
CONST char	*name;		/* (in, opt.) name of setting */
size_t		varOffset;	/* (in) APICP offset of byte * setting to set*/
CONST char	*val;		/* (in) value to parse */
/* Handles texis.ini setting of a string APICP field.
 * Returns 0 on error (and does not modify setting).
 */
{
	static CONST char	fn[] = "txApicpInitStr";
	char			*newVal;

	(void)name;
	if ((newVal = TXstrdup(pmbuf, fn, val)) == CHARPN) return(0);
	/* `var' might be a CONST value; use TXapicpFreeDefaultStr(): */
	TXapicpFreeDefaultStr(*(char **)((byte*)&TxApicpDefault + varOffset));
	*(char **)((byte*)&TxApicpDefault + varOffset) = newVal;
	*(char **)((byte*)&TxApicpDefaultIsFromTexisIni + varOffset) =
		(char *)1L;
	return(1);
}

static int txApicpInitStrLst ARGS((TXPMBUF *pmbuf, CONST char *name,
				   size_t varOffset, CONST char *val));
static int
txApicpInitStrLst(pmbuf, name, varOffset, val)
TXPMBUF		*pmbuf;		/* (in, opt.) buffer for putmsgs */
CONST char	*name;		/* (in, opt.) name of setting */
size_t		varOffset;	/* (in) APICP offset of char** setting to set*/
CONST char	*val;		/* (in) value to parse */
/* Handles texis.ini setting of a string list APICP field.
 * Values are space-separated, and may be single- or double-quoted if
 * containing spaces.
 * Returns 0 on error (and does not modify setting).
 */
{
	static CONST char	fn[] = "txApicpInitStrLst";
	char			**list = CHARPPN, **newList, quoteChar;
	int			ret;
	CONST char		*s, *e;
	size_t			valIdx = 0;

	for (s = val, valIdx = 0; ; s = e, valIdx++)
	{					/* for each value */
		if ((valIdx & 0x1f) == 0)	/* need to realloc array */
		{
			newList = (char **)TXrealloc(pmbuf, fn, list,
					/* +2 for ""- and NULL-termination: */
					(valIdx + 0x20 + 2)*sizeof(char *));
			if (newList == CHARPPN)	/* alloc failed */
			{
#ifndef EPI_REALLOC_FAIL_SAFE
				list = CHARPPN;
#endif /* !EPI_REALLOC_FAIL_SAFE */
				goto err;
			}
			list = newList;
		}
		quoteChar = 0;
		s += strspn(s, Whitespace);
		if (*s == '\0') break;		/* no more values */
		if (*s == '"' || *s == '\'')	/* quoted value */
		{
			quoteChar = *(s++);
			e = strchr(s, quoteChar);
			if (e == CHARPN)
			{
				txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN,
				       "Missing quote in [Apicp] %s value",
				       name);
				goto err;
			}
		}
		else				/* whitespace-delim value */
			e = s + strcspn(s, Whitespace);
		list[valIdx] = (char *)TXmalloc(pmbuf, fn, (e - s) + 1);
		if (list[valIdx] == CHARPN) goto err;
		memcpy(list[valIdx], s, e - s);
		list[valIdx][e - s] = '\0';
		if (quoteChar) e++;		/* skip trailing quote */
	}
	if ((list[valIdx++] = TXstrdup(pmbuf, fn, "")) == CHARPN) goto err;
	list[valIdx] = CHARPN;			/* just for safety */

	/* `var' might be a CONST value; use TXapicpFreeDefaultStr(): */
	TXapicpFreeDefaultStrLst(*(char***)((byte*)&TxApicpDefault+varOffset));
	*(char ***)((byte*)&TxApicpDefault + varOffset) = list;
	list = CHARPPN;				/* `var' owns it now */
	*(char ***)((byte*)&TxApicpDefaultIsFromTexisIni + varOffset) =
		(char **)1L;
	ret = 1;				/* success */
	goto done;

err:
	ret = 0;				/* error */
done:
	if (list != CHARPPN)
	{
		list[valIdx] = CHARPN;		/* for freenlst() */
		list = freenlst(list);
	}
	return(ret);
}

static int txApicpInitDenyMode ARGS((TXPMBUF *pmbuf, CONST char *name,
				     size_t varOffset, CONST char *val));
static int
txApicpInitDenyMode(pmbuf, name, varOffset, val)
TXPMBUF		*pmbuf;		/* (in, opt.) buffer for putmsgs */
CONST char	*name;		/* (in, opt.) name of setting */
size_t		varOffset;	/* (in) APICP offset of int setting to set */
CONST char	*val;		/* (in) value to parse */
/* Handles texis.ini setting of `denymode'.
 * Returns 0 on error (and does not modify setting).
 */
{
	long	n;
	char	*e;

	if (strcmpi(val, "silent") == 0)
		n = API3DENYSILENT;
	else if (strcmpi(val, "warning") == 0 || strcmpi(val, "warn") == 0)
		n = API3DENYWARNING;
	else if (strcmpi(val, "error") == 0)
		n = API3DENYERROR;
	else if (((n = strtol(val, &e, 0)) != API3DENYSILENT &&
		  n != API3DENYWARNING &&
		  n != API3DENYERROR) ||
		 e == val || *e != '\0')
	{
		txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN, TxInvalidApicpVal,
			       name, val);
		return(0);			/* failure */
	}
	/* success; set the value: */
	*(int *)((byte *)&TxApicpDefault + varOffset) = n;
	*(int *)((byte *)&TxApicpDefaultIsFromTexisIni + varOffset) = 1;
	return(1);				/* success */
}

static int txApicpInitWithinMode ARGS((TXPMBUF *pmbuf, CONST char *name,
				       size_t varOffset, CONST char *val));
static int
txApicpInitWithinMode(pmbuf, name, varOffset, val)
TXPMBUF		*pmbuf;		/* (in, opt.) buffer for putmsgs */
CONST char	*name;		/* (in, opt.) name of setting */
size_t		varOffset;	/* (in) APICP offset of int setting to set */
CONST char	*val;		/* (in) value to parse */
/* Handles texis.ini setting of `withinmode'.
 * Returns 0 on error (and does not modify setting).
 */
{
	int	n;

	(void)name;
	if (!TXparseWithinmode(pmbuf, val, &n))
		return(0);			/* failure */

	/* success; set the value: */
	*(int *)((byte *)&TxApicpDefault + varOffset) = n;
	*(int *)((byte *)&TxApicpDefaultIsFromTexisIni + varOffset) = 1;
	return(1);				/* success */
}

static int txApicpInitPhraseWordProc ARGS((TXPMBUF *pmbuf, CONST char *name,
					   size_t varOffset, CONST char *val));
static int
txApicpInitPhraseWordProc(pmbuf, name, varOffset, val)
TXPMBUF		*pmbuf;		/* (in, opt.) buffer for putmsgs */
CONST char	*name;		/* (in, opt.) name of setting */
size_t		varOffset;	/* (in) APICP offset of int setting to set */
CONST char	*val;		/* (in) value to parse */
/* Handles texis.ini setting of `phrasewordproc'.
 * Returns 0 on error (and does not modify setting).
 */
{
	long	n;
	char	*e;

	if (strcmpi(val, "mono") == 0)
		n = API3PHRASEWORDMONO;
	else if (strcmpi(val, "none") == 0)
		n = API3PHRASEWORDNONE;
	else if (strcmpi(val, "last") == 0)
		n = API3PHRASEWORDLAST;
	else if (strcmpi(val, "all") == 0)
		n = API3PHRASEWORDALL;
	else if (((n = strtol(val, &e, 0)) != API3PHRASEWORDMONO &&
		  n != API3PHRASEWORDNONE &&
		  n != API3PHRASEWORDLAST &&
		  n != API3PHRASEWORDALL) ||
		 e == val || *e != '\0')
	{
		txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN, TxInvalidApicpVal,
			       name, val);
		return(0);			/* failure */
	}
	*(int *)((byte *)&TxApicpDefault + varOffset) = n;
	*(int *)((byte *)&TxApicpDefaultIsFromTexisIni + varOffset) = 1;
	return(1);				/* success */
}

static int txApicpInitExactPhrase ARGS((TXPMBUF *pmbuf, CONST char *name,
					size_t varOffset, CONST char *val));
static int
txApicpInitExactPhrase(pmbuf, name, varOffset, val)
TXPMBUF		*pmbuf;		/* (in, opt.) buffer for putmsgs */
CONST char	*name;		/* (in, opt.) name of setting */
size_t		varOffset;	/* (in) APICP offset of byte setting to set */
CONST char	*val;		/* (in) value to parse */
/* Handles texis.ini setting of `exactphrase'.
 * Returns 0 on error (and does not modify setting).
 */
{
	int	n;

	if (strcmpi(val, "ignorewordposition") == 0)
		n = API3EXACTPHRASEIGNOREWORDPOSITION;
	else switch (n = TXgetBooleanOrInt(TXPMBUFPN, TxApicpGroup, name, val,
					   CHARPN, 1))
	{
		case API3EXACTPHRASEOFF:
		case API3EXACTPHRASEON:
		case API3EXACTPHRASEIGNOREWORDPOSITION:
			break;
		default:
			txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN,
				       TxInvalidApicpVal, name, val);
			return(0);		/* failure */
	}
	*((byte *)&TxApicpDefault + varOffset) = n;
	*((byte *)&TxApicpDefaultIsFromTexisIni + varOffset) = 1;
	return(1);				/* success */
}

#define APICPOFF(fld)	((byte *)&(APICPPN->fld) - (byte *)APICPPN)

static int txApicpInitTxcff ARGS((TXPMBUF *pmbuf, CONST char *name,
				  size_t varOffset, CONST char *val));
static int
txApicpInitTxcff(pmbuf, name, varOffset, val)
TXPMBUF		*pmbuf;		/* (in, opt.) buffer for putmsgs */
CONST char	*name;		/* (in, opt.) name of setting */
size_t		varOffset;	/* (in) APICP offset of byte setting to set */
CONST char	*val;		/* (in) value to parse */
/* Inits textsearchmode or stringcomparemode from texis.ini.
 * Returns 0 on error (and does not modify setting).
 */
{
	TXCFF	txcff;

	if (!TxTextsearchmodeEnabled) return(0);

	/* [Apicp] Text Search Mode and String Compare Mode are
	 * relative (+/- allowed), for convenience; user should
	 * know what the defaults are (even if those change at
	 * upgrade), and relative mode is what <apicp> does anyway.
	 * wtf we are using `TxApicpDefault' directly here,
	 * for defaults for relative mode, and to detect
	 * stringcomparemode vs. textsearchmode:
	 */
	if (TXstrToTxcff(val, CHARPN, TxApicpDefault.textsearchmode,
			 TxApicpDefault.stringcomparemode,
			 *(int *)((byte *)&TxApicpDefault + varOffset),
			 (varOffset == APICPOFF(stringcomparemode)),
			 (TXCFF)(-1), &txcff))
	{					/* valid value */
		*(int *)((byte *)&TxApicpDefault + varOffset) = txcff;
		*(int *)((byte *)&TxApicpDefaultIsFromTexisIni+varOffset) = 1;
		return(1);			/* success */
	}
	txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN, TxInvalidApicpVal,
		       name, val);
	return(0);				/* failure */
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

typedef struct AII_tag				/* APICP init item */
{
	char	*name;				/* setting name */
	int	(*setFunc) ARGS((TXPMBUF *pmbuf, CONST char *name,
				 size_t varOffset, CONST char *val));
	size_t	offset;				/* APICP field offset */
}
AII;
#define AIIPN	((AII *)NULL)

/* NOTE: These must be in ascending order by name: */
static CONST AII	TxApicpInitItems[] =
{
  { "alequivs",		txApicpInitByteBoolean,	APICPOFF(alequivs)       },
  { "alintersects",	txApicpInitByteBoolean,	APICPOFF(alintersects)   },
  { "allinear",		txApicpInitByteBoolean,	APICPOFF(allinear)       },
  { "alnot",		txApicpInitByteBoolean,	APICPOFF(alnot)          },
  { "alphrase",		txApicpInitByteBoolean,	APICPOFF(alphrase)       },
  { "alpostproc",	txApicpInitByteBoolean,	APICPOFF(alpostproc)     },
  { "alwild",		txApicpInitByteBoolean,	APICPOFF(alwild)         },
  { "alwithin",		txApicpInitByteBoolean,	APICPOFF(alwithin)       },
  { "defsuffrm",	txApicpInitByteBoolean,	APICPOFF(defsuffrm)      },
  { "defsufrm",		txApicpInitByteBoolean,	APICPOFF(defsuffrm)      },
  { "denymode",		txApicpInitDenyMode,	APICPOFF(denymode)       },
  { "edexp",		txApicpInitStr,		APICPOFF(edexp)          },
  { "eqprefix",		txApicpInitStr,		APICPOFF(eqprefix)       },
  { "exactphrase",	txApicpInitExactPhrase,	APICPOFF(exactphrase)    },
  { "inc_edexp",	txApicpInitByteBoolean,	APICPOFF(inced)          },
  { "inc_sdexp",	txApicpInitByteBoolean,	APICPOFF(incsd)          },
  { "inced",		txApicpInitByteBoolean,	APICPOFF(inced)          },
  { "incsd",		txApicpInitByteBoolean,	APICPOFF(incsd)          },
  { "intersects",	txApicpInitIntNum,	APICPOFF(intersects)     },
  { "keepeqvs",		txApicpInitByteBoolean,	APICPOFF(keepeqvs)       },
  { "keepnoise",	txApicpInitByteBoolean,	APICPOFF(keepnoise)      },
  { "minwordlen",	txApicpInitIntNum,	APICPOFF(minwordlen)     },
  { "noise",		txApicpInitStrLst,	APICPOFF(noise)          },
  { "olddelim",		txApicpInitByteBoolean,	APICPOFF(olddelim)       },
  { "phrasewordproc",	txApicpInitPhraseWordProc,APICPOFF(phrasewordproc) },
  { "prefix",		txApicpInitStrLst,	APICPOFF(prefix)         },
  { "prefixproc",	txApicpInitByteBoolean,	APICPOFF(prefixproc)     },
  { "qmaxsets",		txApicpInitIntNum,	APICPOFF(qmaxsets)       },
  { "qmaxsetwords",	txApicpInitIntNum,	APICPOFF(qmaxsetwords)   },
  { "qmaxterms",	txApicpInitIntNum,	APICPOFF(qmaxsets)       },
  { "qmaxwords",	txApicpInitIntNum,	APICPOFF(qmaxwords)      },
  { "qminprelen",	txApicpInitIntNum,	APICPOFF(qminprelen)     },
  { "qminwordlen",	txApicpInitIntNum,	APICPOFF(qminwordlen)    },
  { "rebuild",		txApicpInitByteBoolean,	APICPOFF(rebuild)        },
  { "reqedelim",	txApicpInitByteBoolean,	APICPOFF(reqedelim)      },
  { "reqsdelim",	txApicpInitByteBoolean,	APICPOFF(reqsdelim)      },
  { "sdexp",		txApicpInitStr,		APICPOFF(sdexp)          },
  { "see",		txApicpInitByteBoolean,	APICPOFF(see)            },
  { "stringcomparemode",txApicpInitTxcff,	APICPOFF(stringcomparemode) },
  { "suffix",		txApicpInitStrLst,	APICPOFF(suffix)         },
  { "suffixeq",		txApicpInitStrLst,	APICPOFF(suffixeq)       },
  { "suffixproc",	txApicpInitByteBoolean,	APICPOFF(suffixproc)     },
  { "textsearchmode",	txApicpInitTxcff,	APICPOFF(textsearchmode) },
  { "ueqprefix",	txApicpInitStr,		APICPOFF(ueqprefix)      },
  { "useequiv",		txApicpInitByteBoolean,	APICPOFF(keepeqvs)       },
  { "withinmode",	txApicpInitWithinMode,	APICPOFF(withinmode)     },
  { "withinproc",	txApicpInitByteBoolean,	APICPOFF(withinproc)     },
};
#define TX_APICP_INIT_ITEMS_NUM	\
	(sizeof(TxApicpInitItems)/sizeof(TxApicpInitItems[0]))

/* ------------------------------------------------------------------------ */

static int txGetApicpDefaults ARGS((TXPMBUF *pmbuf));
static int
txGetApicpDefaults(pmbuf)
TXPMBUF	*pmbuf;		/* (in, opt.) buffer for putmsgs */
/* Sets install-dir- and texis.ini-dependent APICP defaults.
 * Should be called after TXinitapp(), and after putmsg(), stdio initialized.
 * May issue putmsg()s to `pmbuf' for syntax errors etc.
 * Returns 0 on error (e.g. bad setting).
 */
{
	static CONST char	fn[] = "txGetApicpDefaults";
	static CONST char	apicpSectionName[] = "Apicp";
	/* Default API3UEQPREFIX, after install dir: */
	static CONST char	eqvsusrSuffix[] = PATH_SEP_S "eqvsusr";
	CONST char		*confVar, *confVal;
	CONST AII		*initItem;
	int			confVarIdx, l, r, i, cmp, ret = 1;
	char			*s;

    if (TxConf)
    {
	for (confVarIdx = 0;
	     confVal = getnextconfstring(TxConf, apicpSectionName, &confVar,
					 confVarIdx), confVar != CHARPN;
	     confVarIdx++)
	{					/* for each [Apicp] setting */
		if (confVal == CHARPN) continue;/* no value assigned */
		l = 0;
		r = TX_APICP_INIT_ITEMS_NUM;
		while (l < r)			/* binary search */
		{
			i = ((l + r) >> 1);
			initItem = TxApicpInitItems + i;
			cmp = TXstrnispacecmp(confVar, -1, initItem->name, -1,
						CHARPN);
			if (cmp < 0) r = i;
			else if (cmp > 0) l = i + 1;
			else goto foundApicpSetting;
		}
		continue;			/* unknown setting */
	foundApicpSetting:			/* known setting: set it */
		if (!initItem->setFunc(pmbuf, confVar, initItem->offset,
				confVal))
			ret = 0;
	}
    }

	/* If eqprefix/ueqprefix are the default (i.e. not set in texis.ini),
	 * then fix up install dir.  Note that we assume API3EQPREFIX is
	 * `builtin' and API3UEQPREFIX is effectively `%INSTALLDIR%/eqvsusr'.
	 * See also api3.h defaults for these (which should match),
	 * and TxApicpBuiltinDefault.[u]eqprefix usage in txopencp():
	 */
	/* default eqprefix is `builtin', no fixup needed */
	/* TxApicpBuiltinDefault not set by texis.ini; always fix up: */
	s = TXstrcatN(pmbuf, __FUNCTION__, TXINSTALLPATH_VAL, eqvsusrSuffix,
		      NULL);
	if (s)					/* alloc ok: set new value */
	{
		TXapicpFreeDefaultStr((char *)TxApicpBuiltinDefault.ueqprefix);
		TxApicpBuiltinDefault.ueqprefix = (byte *)s;
		/* `TxApicpBuiltinDefault' now owns `s', but we
		 * continue to borrow it here:
		 */
		/* Now fix up TxApicpDefault, if not set by texis.ini: */
		if (!TxApicpDefaultIsFromTexisIni.ueqprefix)
		{				/* needs fix up */
			if ((s = TXstrdup(pmbuf, fn, s)) != CHARPN)
			{
				TXapicpFreeDefaultStr((char *)
						TxApicpDefault.ueqprefix);
				TxApicpDefault.ueqprefix = (byte *)s;
			}
			else
				ret = 0;
		}
	}
	else
		ret = 0;

	return(ret);
}

/* ------------------------------------------------------------------------ */

static TXbool
TXAppSetCompatibilityVersionDependentFields(TXAPP *app)
/* Sets compatibilityversion-dependent fields in `app' according to
 * current `app->compatibilityVersion...' values.  Assumes latter
 * is set to a supported version.
 * Returns false on error.
 */
{
	const char	*progName;

	progName = (TxOrgArgv && TxOrgArgv[0] ? TxOrgArgv[0] : "unknown");

	/* Version 8+: - - - - - - - - - - - - - - - - - - - - - - - - - - */
	app->legacyVersion7OrderByRank =
		TX_LEGACY_VERSION_7_ORDER_BY_RANK_DEFAULT(app);
	app->defaultPasswordEncryptionMethod =
		TXpwEncryptMethod_BUILTIN_DEFAULT(app);
	app->defaultPasswordEncryptionRounds = TX_PWENCRYPT_ROUNDS_DEFAULT;
	app->legacyVersion7UrlCoding =
		TX_LEGACY_VERSION_7_URL_CODING_DEFAULT(app);
	/* syntaxversion pragma also defaults to compatibilityversion */
	app->metaCookiesDefault = TX_METACOOKIES_DEFAULT_DEFAULT(app);

	/* Version 7+: - - - - - - - - - - - - - - - - - - - - - - - - - - */
  app->charStrlstConfig.toStrlst = TXVSSEP_BUILTIN_DEFAULT(TXApp);
	/* Bug 4036: byte <-> char convert() will not hexify by default v7+:*/
	app->hexifyBytes = TX_HEXIFY_BYTES_DEFAULT(app, progName);
	/* Bug 4065: IN will behave as subset not intersect in version 7+: */
	app->inModeIsSubset = TX_IN_MODE_IS_SUBSET_DEFAULT(app);
	app->strlstRelopVarcharPromoteViaCreate =
		TX_STRLST_RELOP_VARCHAR_PROMOTE_VIA_CREATE_DEFAULT(app);
	app->useStringcomparemodeForStrlst =
		TX_USE_STRINGCOMPAREMODE_FOR_STRLST_DEFAULT(app);
	app->deDupMultiItemResults = TX_DE_DUP_MULTI_ITEM_RESULTS_DEFAULT(app);
	app->multiValueToMultiRow = TX_MULTI_VALUE_TO_MULTI_ROW_DEFAULT(app);

	return(TXbool_True);			/* success */
}

/* ------------------------------------------------------------------------ */

TXbool
TXparseTexisVersion(const char *texisVersion, const char *texisVersionEnd,
		    int earliestMajor, int earliestMinor,
		    int latestMajor, int latestMinor,
		    int *versionMajor, int *versionMinor,
		    char *errBuf, size_t errBufSz)
/* Parses `n[.n[.n]]' value of `texisVersion', setting `*versionMajor'
 * and `*versionMinor'.
 * Returns true if ok (with `Ok' in `errBuf'), or false on error (with
 * `errBuf' set to human err message), e.g. unsupported version.
 */
{
	const char	*numBegin, *numEnd;
	TXbool		ret;
	int		errNum;

	if (!texisVersionEnd)
		texisVersionEnd = texisVersion + strlen(texisVersion);

	if (texisVersionEnd - texisVersion == 7 &&
	    strnicmp(texisVersion, "default", 7) == 0)
	{
		*versionMajor = TX_VERSION_MAJOR;
		*versionMinor = TX_VERSION_MINOR;
	}
	else
	{
		/* Get major version: */
		numBegin = texisVersion;
		numEnd = numBegin + TXstrcspnBuf(numBegin, texisVersionEnd,
						 ".", -1);
		*versionMajor = TXstrtoi(numBegin, numEnd, NULL,
					 (10 | TXstrtointFlag_SolelyNumber),
					 &errNum);
		if (errNum != 0)
		{
			htsnpf(errBuf, errBufSz, "Unparsable major version");
			goto err;
		}

		/* Get minor version, if present: */
		if (numEnd < texisVersionEnd && *numEnd == '.')
		{
			numBegin = numEnd + 1;
			numEnd = numBegin + TXstrcspnBuf(numBegin,
							 texisVersionEnd,
							 ".", -1);
			*versionMinor = TXstrtoi(numBegin, numEnd, NULL,
					   (10 | TXstrtointFlag_SolelyNumber),
						 &errNum);
			if (errNum != 0)
			{
				htsnpf(errBuf, errBufSz,
				       "Unparsable minor version");
				goto err;
			}
			/* Get revision, if present: */
			if (numEnd < texisVersionEnd && *numEnd == '.')
			{
				int	val;

				numBegin = numEnd + 1;
				numEnd = texisVersionEnd;
				/* We do not care about revision,
				 * just that it's parseable, to detect
				 * utter nonsense like `1.2.FlargityFloo':
				 */
				val = TXstrtoi(numBegin, numEnd, NULL,
					   (10 | TXstrtointFlag_SolelyNumber),
					       &errNum);
				if (errNum != 0 || val < 800000000)
				{
					htsnpf(errBuf, errBufSz,
					"Unparsable/invalid revision number");
					goto err;
				}
			}
		}
		else
			*versionMinor = 0;
	}

	/* See if requested version is supported for compatibilityversion.
	 * Note that Vortex syntaxversion caller does additional check:
	 */
	if (*versionMajor < earliestMajor ||
	    (*versionMajor == earliestMajor && *versionMinor<earliestMinor) ||
	    *versionMinor < 0)
	{
		htsnpf(errBuf, errBufSz,
		       "Versions earlier than %d.%02d not supported",
		       earliestMajor, earliestMinor);
		goto err;
	}
	if ((*versionMajor == 6 && *versionMinor > 1) ||
	    (*versionMajor == 7 && *versionMinor > 7))
#if TX_VERSION_MAJOR > 8
#  error Add hard-coded limits for Version 8 here; update Vortex test584 and any syntaxversion tests
#endif
	{
		htsnpf(errBuf, errBufSz,
		       "Version %d.%02d did not exist or is not supported",
		       *versionMajor, *versionMinor);
		goto err;
	}
	if ((*versionMajor == latestMajor && *versionMinor > latestMinor) ||
	    *versionMajor > latestMajor)
	{
		htsnpf(errBuf, errBufSz,
		       "Versions later than %d.%02d not supported",
		       latestMajor, latestMinor);
		goto err;
	}
	TXstrncpy(errBuf, "Ok", errBufSz);
	ret = TXbool_True;
	goto finally;

err:
	*versionMajor = *versionMinor = 0;
	ret = TXbool_False;
finally:
	return(ret);
}

/* ------------------------------------------------------------------------ */

TXbool
TXAppSetCompatibilityVersion(TXAPP *app, const char *texisVersion,
			     char *errBuf, size_t errBufSz)
/* Sets Texis to be compatible with the version indicated by
 * `texisVersion' if possible, i.e. behave as if version
 * `texisVersion' was running.  `texisVersion' is an x[.y[.z]] texis
 * -version string, or `default' for native version.
 * Returns true on success with `errBuf' set to `Ok', or false on error with
 * human-readable error message in `errBuf'.  May still partially set
 * `texisVersion' features on error, i.e. does best-effort.
 * `errBufSz' should be a few hundred bytes.
 */
{
	int			requestedMajorVersion = 0;
	int			requestedMinorVersion = 0;
	TXbool			ret;

	if (!TXparseTexisVersion(texisVersion, NULL, 6, 0,
				 TX_VERSION_MAJOR, TX_VERSION_MINOR,
				 &requestedMajorVersion,
				 &requestedMinorVersion, errBuf, errBufSz))
		goto err;

	/* We support the requested version; apply it: */
	app->compatibilityVersionMajor = requestedMajorVersion;
	app->compatibilityVersionMinor = requestedMinorVersion;
	if (!TXAppSetCompatibilityVersionDependentFields(app))
	{					/* should never happen */
		htsnpf(errBuf, errBufSz, "Internal error");
		goto err;
	}

	TXstrncpy(errBuf, "Ok", errBufSz);
	ret = TXbool_True;
	goto done;

err:
	ret = TXbool_False;
done:
	app->setCompatibilityVersionFailed = !ret;
	return(ret);
}

/* ------------------------------------------------------------------------ */

size_t
TXAppGetCompatibilityVersion(TXAPP *app, char *buf, size_t bufSz)
/* Prints major.minor compatibility version currently set, to `buf'.
 * Returns would-be strlen() of `buf' (not written past `bufSz').
 */
{
	return(htsnpf(buf, bufSz, "%d.%02d",
		      (int)app->compatibilityVersionMajor,
		      (int)app->compatibilityVersionMinor));
}

/* ------------------------------------------------------------------------ */

int
TXAppSetTraceRowFields(TXPMBUF *pmbuf, TXAPP *app, const char *traceRowFields)
/* Applies CSV `traceRowFields' to `app'.  Form is:
 *   table.field[, table.field ...]
 * Leading/trailing whitespace around tables/fields ignored.
 * Returns 0 on error.
 */
{
	static const char	fn[] = "TXAppSetTraceRowFields";
	static const char	commaWhitespace[] = ", \t\r\n\v\f";
	size_t			numTablesAlloced = 0, numFieldsAlloced = 0;
	size_t			numItems = 0, i;
	char			**tables = NULL, **fields = NULL;
	const char		*tbl, *tblEnd, *fld, *fldEnd;
	int			ret;

	for (tbl = traceRowFields; *tbl; tbl = fldEnd)
	{
		tbl += strspn(tbl, commaWhitespace);
		if (!*tbl) break;		/* EOF */
		/* Point `fldEnd' at next comma, back up over whitespace: */
		for (fldEnd = tbl + strcspn(tbl, ",");
		     fldEnd > tbl && strchr(commaWhitespace, fldEnd[-1]);
		     fldEnd--)
			;
		tblEnd = tbl + TXstrcspnBuf(tbl, fldEnd, ".", -1);
		if (*tblEnd != '.') goto noFldName;
		fld = tblEnd + 1;		/* just past `.' */
		for ( ;				/* back up over whitespace: */
		     tblEnd > tbl && strchr(Whitespace, tblEnd[-1]);
		     tblEnd--)
			;
		fld += TXstrspnBuf(fld, fldEnd, Whitespace, -1);
		if (fld >= fldEnd)
		{
		noFldName:
			txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
				       "Missing field name after table name");
			goto err;
		}
		/* Add to arrays: */
		if (!TX_INC_ARRAY(pmbuf, &tables, numItems, &numTablesAlloced))
			goto err;
		if (!TX_INC_ARRAY(pmbuf, &fields, numItems, &numFieldsAlloced))
			goto err;
		tables[numItems] = TXstrndup(pmbuf, fn, tbl, tblEnd - tbl);
		fields[numItems++] = TXstrndup(pmbuf, fn, fld, fldEnd - fld);
		if (!tables[numItems - 1] || !fields[numItems - 1]) goto err;
	}
	if (numItems > 0)
	{
		/* NULL-terminate and apply to `app': */
		if (!TX_INC_ARRAY(pmbuf, &tables, numItems, &numTablesAlloced))
			goto err;
		if (!TX_INC_ARRAY(pmbuf, &fields, numItems, &numFieldsAlloced))
			goto err;
		tables[numItems] = fields[numItems] = NULL;
	}
	app->traceRowFieldsTables =
		TXapi3FreeNullList(app->traceRowFieldsTables);
	app->traceRowFieldsFields =
		TXapi3FreeNullList(app->traceRowFieldsFields);
	app->traceRowFieldsTables = tables;
	tables = NULL;
	app->traceRowFieldsFields = fields;
	fields = NULL;
	numItems = numTablesAlloced = numFieldsAlloced = 0;
	ret = 1;
	goto finally;

err:
	ret = 0;
finally:
	for (i = 0; i < numItems; i++)
	{
		tables[i] = TXfree(tables[i]);
		fields[i] = TXfree(fields[i]);
	}
	tables = TXfree(tables);
	fields = TXfree(fields);
	numItems = numTablesAlloced = numFieldsAlloced = 0;
	return(ret);
}

int
TXAppSetLogDir(TXPMBUF *pmbuf, TXAPP *app, const char *logDir,
	       size_t logDirSz)
/* Sets TXAPP log dir.  Currently only used for core files.
 * Returns 0 on error.
 */
{
	char	*newDir;
	size_t	len;

	newDir = TXstrndup(pmbuf, __FUNCTION__, logDir, logDirSz);
	if (!newDir) return(0);
	/* Trim trailing slash, if any: */
	len = strlen(newDir);
	if (len > 1 && TX_ISPATHSEP(newDir[len - 1])) newDir[len - 1] = '\0';
	/* Set it as new log dir: */
	app->logDir = TXfree(app->logDir);
	app->logDir = newDir;
	newDir = NULL;
	return(1);
}

TXbool
TXAppSetDefaultPasswordEncryptionMethod(TXPMBUF *pmbuf, TXAPP *app,
					TXpwEncryptMethod method)
{
	if (method <= TXpwEncryptMethod_Unknown ||
	    method >= TXpwEncryptMethod_NUM)
	{
		txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
			       "Unknown password encryption method %d",
			       (int)method);
		return(TXbool_False);
	}
	app->defaultPasswordEncryptionMethod = method;
	return(TXbool_True);
}

TXbool
TXAppSetDefaultPasswordEncryptionRounds(TXPMBUF *pmbuf, TXAPP *app,
					int rounds)
{
	if (rounds < TX_PWENCRYPT_ROUNDS_MIN ||
	    rounds > TX_PWENCRYPT_ROUNDS_MAX)
	{
		txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
			      "Password encryption rounds %d is out of range",
			       rounds);
		return(TXbool_False);
	}
	app->defaultPasswordEncryptionRounds = rounds;
	return(TXbool_True);
}

/* ------------------------------------------------------------------------ */

int
TXinitapp(pmbuf, progName, argc, argv, argcStripped, argvStripped)
TXPMBUF	*pmbuf;			/* (in, opt.) buffer for putmsgs */
const char	*progName;	/* (in, opt.) program name for signal msgs */
int	argc;			/* (in) main() argc */
char	**argv;			/* (in, opt.) real main() argv pointer list */
int	*argcStripped;		/* (out, opt.) global-option-stripped `argc' */
char	***argvStripped;	/* (out, opt.) "" `argv', dup'd */
/* Should be called at start of every Texis application, immediately;
 * API users will (eventually?) call this implicitly at the first API call?
 * `argc'/`argv' will be parsed for global options (--texis-conf etc.)
 * and marked as live argv (e.g. for TXgetExeFileName(), and arg replacement
 * by monitor).  texis.ini will be opened and parsed for some global
 * options (Lib Path, [Apicp] section etc.).
 * If non-NULL, `argvStripped' will be alloc'd to a completely dup'd copy
 * of `argv' with global options stripped.
 * Returns 0 on success, -1 if already called, else TXEXIT error code
 * (should cause exit).
 */
{
	static CONST char	fn[] = "TXinitapp";
	static CONST char	texisSectionName[] = "Texis";
	static CONST char	vortexAddTrailingSlash[] =
					"Vortex Add Trailing Slash";
	static CONST char	multiValueToMultiRow[] =
					"Multi Value To Multi Row";
	static CONST char	texisDefaultsWarning[] =
					"Texis Defaults Warning";
	static CONST char	hexifyBytesStr[] = "Hexify Bytes";
	static const char	preLoadBlobsStr[] = "Pre Load Blobs";
	static const char  createDbOkDirExists[] = "Create Db Ok Dir Exists";
	static const char	restoreStdioInheritance[] = "Restore Stdio Inheritance";
	static const char	validateBtrees[] = "Validate Btrees";
	static const char	trap[] = "Trap";
	static const char	unneededRexEscapeWarning[] =
		"Unneeded REX Escape Warning";
	static const char	legacyVersion7OrderByRank[] =
		"Legacy Version 7 Order By Rank";
	static const char	defaultPasswordEncryptionMethod[] =
		"Default Password Encryption Method";
	static const char	defaultPasswordEncryptionRounds[] =
		"Default Password Encryption Rounds";
#ifdef EPI_ENABLE_CONF_TEXIS_INI
	static CONST char * CONST	confSuffixes[] =
	{	/* these are highest-priority first: */
		TX_TEXIS_INI_NAME,
		"conf" PATH_SEP_S "texis.cnf",
		"texis.ini",
		"texis.cnf",			/* original name */
		CHARPN
	};
#endif /* EPI_ENABLE_CONF_TEXIS_INI */
	int			argcGlobalOpts = 0;
	size_t			nameLen;
#define GLOBAL_OPTS_MAX		64
	char			*argvGlobalOpts[GLOBAL_OPTS_MAX];
	CONST char		*optVal, *legacyConfOpt = CHARPN;
	char			**sp, **dp;
	char			*d, *e;
	int			i, force = 0, didConfOpen = 0;
	EPI_STAT_S		st;
	int			ret;
	double			doubleVal;
	char			tmp[16 /* sizeof("Trace License") */];

#ifndef EPI_ENABLE_TEXTSEARCHMODE
	if (TxTextsearchmodeEnabled == -1)	/* not initialized yet */
		TxTextsearchmodeEnabled =
			(getenv("EPI_ENABLE_TEXTSEARCHMODE") != CHARPN);
#endif /* !EPI_ENABLE_TEXTSEARCHMODE */

	/* One-time init: */
	TXprocessInit(pmbuf);
  json_set_alloc_funcs(malloc, free);
	/* Regardless of whether we were init'd already, caller is
	 * probably counting on a copy of args in `*argvStripped':
	 */
	if (argv == CHARPPN) argc = 0;
	if (argcStripped != INTPN) *argcStripped = argc;
	if (argvStripped != CHARPPPN)
	{
		*argvStripped = (char **)TXcalloc(pmbuf, fn, argc + 1,
						     sizeof(char *));
		if (*argvStripped == CHARPPN)
		{
			if (argcStripped != INTPN) *argcStripped = 0;
			ret = TXEXIT_OUTOFMEMORY;
			goto finally;
		}
		memcpy(*argvStripped, argv, argc*sizeof(char *));
		(*argvStripped)[argc] = CHARPN;
	}

	if(TXApp != TXAPPPN) 			/* Already initialized */
	{
		ret = -1;			/* success */
		goto finally;			/* but no argv parse */
	}

	if (!progName && argc > 0 && argv && *argv)
		progName = TXbasename(*argv);	/* default */
	/* Only set if we have something; try not to mistakenly clear prev: */
	if (progName) TXsetSigProcessName(pmbuf, progName);

	/* These are initialized here so non-Texis-lib functions can
	 * use them.  They are function pointers to avoid having to
	 * link epi/api/etc. libs with Texis lib.  WTF:
	 */
#ifdef MEMDEBUG
#  undef TXallocProtectableFunc
#  undef TXfreeProtectableFunc
#  undef TXprotectMemFunc
#endif /* MEMDEBUG */
#ifdef TX_ENABLE_MEM_PROTECT
	TXallocProtectableFunc = (void  *(*)(void *pmbuf, const char *fn,
	  size_t sz, TXMEMPROTECTFLAG flags TXALLOC_PROTO))TXallocProtectable;
	TXfreeProtectableFunc = (int (*)(void *pmbuf, void *p TXALLOC_PROTO))
		TXfreeProtectable;
	TXprotectMemFunc = (int (*)(void *pmbuf, void *p,
			   TXMEMPROTECTPERM perms TXALLOC_PROTO))TXprotectMem;
#endif /* TX_ENABLE_MEM_PROTECT */

	/* Set args before TXApp defaults; some of latter use former: */
	TXsetargv(pmbuf, argc, argv);

	TXApp = (TXAPP *)TXcalloc(pmbuf, fn, 1, sizeof(TXAPP));
	if(TXApp == TXAPPPN)
	{
		ret = TXEXIT_OUTOFMEMORY;
		goto finally;
	}
	TXApp->LogBadSYSLOCKS = 0;
	TXApp->metamorphStrlstMode = TXMSM_equivlist;
  TXApp->charStrlstConfig.toStrlst = TXc2s_unspecified;
  TXApp->charStrlstConfig.fromStrlst = TXs2c_trailing_delimiter;
	/* Bug 4070: at-CREATE indexvalues setting: */
	TXApp->indexValues = TX_INDEX_VALUES_DEFAULT;

	/* Default for [Texis] Createlocks Methods is `direct monitor':
	 * `direct'-first minimizes traffic to Texis Monitor -- which is
	 * single-threaded -- for every db open, especially for Unix
	 * which does not generally need `monitor' method.  Also helps
	 * avoid errors from legacy monitors which do not support
	 * HTMETH_CREATELOCKS:
	 */
	for (i = 0; i < (int)TXCREATELOCKSMETHOD_NUM; i++)
		TXApp->createLocksMethods[i] = TXCREATELOCKSMETHOD_UNKNOWN;
	TXApp->createLocksMethods[0] = TXCREATELOCKSMETHOD_DIRECT;
	TXApp->createLocksMethods[1] = TXCREATELOCKSMETHOD_MONITOR;

	TXApp->createLocksTimeout = 10;		/* low: talking to localhost*/

	TXApp->vortexAddTrailingSlash = 0;
	TXApp->traceLicense = 0;		/* see texint.h TXApp */
	TXApp->traceMutex = 0;		        /* see texint.h TXApp */
	TXApp->subsetIntersectEnabled = TX_SUBSET_INTERSECT_ENABLED_DEFAULT;

	/* In version 6+, <apicp texisdefaults> is deprecated,
	 * because Texis defaults have changed (to match
	 * Vortex defaults); use <apicp querysettings texis5defaults>.
	 * However <apicp texisdefaults> is still supported
	 * for compatibility:
	 */
	TXApp->texisDefaultsWarning = 1;

	/* see also TXresetproperties(): */
	TXApp->unalignedBufferWarning = 1;

	TXApp->createDbOkDirExists = 0;
	TXApp->validateBtrees = TX_VALIDATE_BTREES_DEFAULT;

	TXApp->trap = TXtrap_Default;

	/* Bug 4031: RAM table blobs are disabled until fully supported
	 * (internal RAM DBFs with blobs still ok):
	 */
	TXApp->allowRamTableBlob = TX_ALLOW_RAM_TABLE_BLOB_DEFAULT;

  TXApp->putmsgFlags = (VXPMF_ERRHDR | VXPMF_DEFAULT
  #ifdef EPI_PUTMSG_DATE_PID_THREAD
                                   /* see also TXinitapp() env check */
              | VXPMF_ShowPid | VXPMF_ShowNonMainThreadId | VXPMF_ShowDateAlways
  #endif /* EPI_PUTMSG_DATE_PID_THREAD */
                                   );

	/* Version 8+: - - - - - - - - - - - - - - - - - - - - - - - - - - */
	/* (except those set via TXAppSet...VersionDependentFields()) */
#ifdef EPI_ENABLE_SQL_MOD
	TXApp->sqlModEnabled = 1;
#else /* !EPI_ENABLE_SQL_MOD */
	TXApp->sqlModEnabled = (getenv("EPI_ENABLE_SQL_MOD") != NULL);
#endif /* !EPI_ENABLE_SQL_MOD */
	TXApp->ipv6Enabled = TX_IPv6_ENABLED((TXAPP *)NULL);
	TXApp->pwEncryptMethodsEnabled =
		TX_PWENCRYPT_METHODS_ENABLED((TXAPP *)NULL);

#ifndef EPI_PUTMSG_DATE_PID_THREAD
/* see also vxglobs.c:VxPutmsgFlags */
	if (getenv("EPI_PUTMSG_DATE_PID_THREAD"))
		TXApp->putmsgFlags |= (VXPMF_ShowPid | VXPMF_ShowNonMainThreadId |
				  VXPMF_ShowDateAlways);
#endif /* EPI_PUTMSG_DATE_PID_THREAD */

	/* Version 7+: - - - - - - - - - - - - - - - - - - - - - - - - - - */
	/* (and some version 8+ via TXAppSet...VersionDependentFields()) */

	/* Bug 4526: compatibilityversion: */
	TXApp->compatibilityVersionMajor = TX_VERSION_MAJOR;
	TXApp->compatibilityVersionMinor = TX_VERSION_MINOR;
	/* Can init `TXApp->compatibilityVersion...'-dependent fields now
	 * that `TXApp->compatibilityVersion...' set:
	 */
	if (!TXAppSetCompatibilityVersionDependentFields(TXApp))
	{
		ret = TXEXIT_INTERNALERROR;
		goto finally;
	}

	TXApp->setCompatibilityVersionFailed = 0;
	TXApp->failIfIncompatible = 0;
	/* See also below after [Texis] Compatibility Version read */

	/* end version 7+ - - - - - - - - - - - - - - - - - - - - - - - - - */

	if (argc >= 1)
	{					/* establish legacy option */
		const char	*s;

		s = TXbasename(argv[0]);
		for (nameLen = 0;
		     (s[nameLen] >= 'a' && s[nameLen] <= 'z') ||
		     (s[nameLen] >= 'A' && s[nameLen] <= 'Z');
			nameLen++);
		if (nameLen == 7 && (*s == 'm' || *s == 'M') &&
		    strnicmp(s, "monitor", 7) == 0)
			legacyConfOpt = "-r";
		else if (nameLen == 5 && (*s == 't' || *s == 'T') &&
			 strnicmp(s, "texis", 5) == 0)
			legacyConfOpt = "-conf";
	}

	/* 1st: look for --install-dir..., set it if found: */
	optVal = CHARPN;			/* no --install-dir yet */
	for (i = 1; i < argc; i++)
	{
		const char	*s;

		s = argv[i];
		if (s[0] == '-' && s[1] == '-')
		{
			if (s[2] == '\0') break;  /* end of options */
			if (strnicmp(s, "--install-dir", 13) == 0)
			{		/* --install-dir... */
				s += 13;
				force = 0;
				if (strnicmp(s, "-force", 6) == 0)
				{	/* --install-dir-force */
					s += 6;
					force = 1;
				}
				if (*s == '='
#ifdef EPI_TWO_ARG_ASSIGN_OPTS
				    || !*s
#endif /* EPI_TWO_ARG_ASSIGN_OPTS */
				    )
				{
                                  if (argcGlobalOpts < GLOBAL_OPTS_MAX)
                                    argvGlobalOpts[argcGlobalOpts++] = argv[i];
                                  if (argvStripped != CHARPPPN)
                                    (*argvStripped)[i] = CHARPN;
                                  if (*s == '=')    /* --install-dir=dir */
                                    optVal = s + 1;
#ifdef EPI_TWO_ARG_ASSIGN_OPTS
                                  else if (!*s)     /* --install-dir dir */
                                    {
                                      if (i + 1 >= argc) goto requiresArg;
                                      optVal = argv[++i];
                                      if (argcGlobalOpts < GLOBAL_OPTS_MAX)
                                        argvGlobalOpts[argcGlobalOpts++] =
                                          argv[i];
                                      if (argvStripped != CHARPPPN)
                                        (*argvStripped)[i] = CHARPN;
                                    }
#endif /* EPI_TWO_ARG_ASSIGN_OPTS */
				}
			}
		}
	}
	if (optVal != CHARPN)			/* --install-dir... given */
	{
		/* If the install dir is invalid, do not fall back
		 * to default dir, as that might cross-couple
		 * separate 32/64-bit installs.  Yap and exit,
		 * or stumble on if --install-dir-force:
		 */
		if (!force && EPI_STAT(optVal, &st) != 0)
		{				/* bad install dir */
			/* but we need to set default dir
			 * in case putmsg() needs it (logging?)
			 */
			TXsetinstallpath(CHARPN);
			txpmbuf_putmsg(pmbuf, MERR + FTE, fn,
					"Cannot access install dir `%s': %s",
					optVal, TXstrerror(TXgeterror()));
			ret = TXEXIT_INVALIDINSTALLDIR;
			goto finally;
		}
		TXsetinstallpath((char *)optVal);
	}

	/* 2nd: ensure dir set if no --install-dir.
	 * After --install-dir check as optimization,
	 * to avoid 2x TXsetinstallpath() calls:
	 */
	if (!TXinstallpathset) TXsetinstallpath(CHARPN);
	tx_inittz();

	/* 3rd: look for --texis-conf and set it or default;
         * also look for --ignore-arg:
         */
	optVal = CHARPN;			/* none specified yet */
	for (i = 1; i < argc; i++)
	{
		const char	*s;

		s = argv[i];
		if (s[0] == '-' && s[1] == '-')
		{
			if (s[2] == '\0') break;  /* end of options */
			if (strnicmp(s, "--texis-conf", 12) == 0 &&
			    (s[12] == '='
#ifdef EPI_TWO_ARG_ASSIGN_OPTS
			     || !s[12]
#endif /* EPI_TWO_ARG_ASSIGN_OPTS */
			     ))
			{	/* --texis-conf=file or --texis-conf file */
				if (argcGlobalOpts < GLOBAL_OPTS_MAX)
					argvGlobalOpts[argcGlobalOpts++] =
						argv[i];
				if (argvStripped != CHARPPPN)
					(*argvStripped)[i] = CHARPN;
				if (s[12] == '=')
					optVal = s + 13;
#ifdef EPI_TWO_ARG_ASSIGN_OPTS
				else if (!s[12])
				{
                                  if (i + 1 >= argc)
                                    {
                                    requiresArg:
                                      txpmbuf_putmsg(pmbuf, MERR + UGE, NULL,
                                                "Usage: `%s arg' or `%s=arg'",
                                                     argv[i], argv[i]);
                                      ret = TXEXIT_INCORRECTUSAGE;
                                      goto finally;
                                    }
                                  optVal = argv[++i];
                                  if (argcGlobalOpts < GLOBAL_OPTS_MAX)
                                    argvGlobalOpts[argcGlobalOpts++] =
                                      argv[i];
                                  if (argvStripped != CHARPPPN)
                                    (*argvStripped)[i] = CHARPN;
				}
#endif /* EPI_TWO_ARG_ASSIGN_OPTS */
			}
			/* --ignore-arg: command-line padding for better
			 * TXsetProcessDescription():
			 */
			else if (strnicmp(s, "--ignore-arg", 12) == 0)
			{		/* --ignore-arg... */
				s += 12;
				if (*s == '='
#ifdef EPI_TWO_ARG_ASSIGN_OPTS
				    || !*s
#endif /* EPI_TWO_ARG_ASSIGN_OPTS */
				    )
				{
                                  if (argcGlobalOpts < GLOBAL_OPTS_MAX)
                                    argvGlobalOpts[argcGlobalOpts++] = argv[i];
                                  if (argvStripped != CHARPPPN)
                                    (*argvStripped)[i] = CHARPN;
                                  if (*s == '=')    /* --ignore-arg=arg */
                                    { ; }
#ifdef EPI_TWO_ARG_ASSIGN_OPTS
                                  else if (!*s)     /* --ignore-arg arg */
                                    {
                                      if (i + 1 >= argc) goto requiresArg;
                                      i++;
                                      if (argcGlobalOpts < GLOBAL_OPTS_MAX)
                                        argvGlobalOpts[argcGlobalOpts++] =
                                          argv[i];
                                      if (argvStripped != CHARPPPN)
                                        (*argvStripped)[i] = CHARPN;
                                    }
#endif /* EPI_TWO_ARG_ASSIGN_OPTS */
				}
			}
		}
		/* support backwards-compatible option: */
		if (legacyConfOpt != CHARPN && s[0] == legacyConfOpt[0] &&
		    i + 1 < argc &&
		    (s[0] == '\0' ||
		     (s[1] == legacyConfOpt[1] &&
		      strcmp(s, legacyConfOpt) == 0)))
		{
			optVal = argv[++i];
			if (argvStripped != CHARPPPN)
				(*argvStripped)[i] = (*argvStripped)[i-1] =
					CHARPN;
		}
	}
	if (optVal != CHARPN)			/* conf file specified */
		TXstrncpy(TxConfFile, optVal, TxConfFile_SZ);
#ifdef _WIN32
#  ifdef EPI_ENABLE_CONF_TEXIS_INI
	else					/* check registry */
	{
		sp = queryregistry(pmbuf, "HKEY_LOCAL_MACHINE",
				   "SOFTWARE\\Thunderstone Software",
				   "TexisConfigFile", CHARPN, 16);
		if (sp != CHARPPN && *sp != CHARPN && **sp != '\0')
		{
			/* Ok to use TxInstBinVars/Vals here, *after*
			 * TXsetinstallpath() and TXsetargv() called:
			 */
			d = tx_replacevars(pmbuf, *sp, 1, TxInstBinVars,
                                           TX_INSTBINVARS_NUM, TxInstBinVals,
                                           INTPN);
			if (d != CHARPN)
			{
				TXstrncpy(TxConfFile, d, TxConfFile_SZ);
				optVal = TxConfFile;	/* flag as set */
				free(d);
			}
		}
		sp = TXfreeStrList(sp, -1);
	}
#  endif /* EPI_ENABLE_CONF_TEXIS_INI */
#endif /* _WIN32 */
	if (optVal == CHARPN)			/* not set: default */
	{
		d = TxConfFile;
		TXstrncpy(d, TXINSTALLPATH_VAL, TxConfFile_SZ);
		d += strlen(d);
		if (d > TxConfFile && !TX_ISPATHSEP(d[-1]))
			*(d++) = PATH_SEP;
#ifdef EPI_ENABLE_CONF_TEXIS_INI
		for (i = 0; confSuffixes[i] != CHARPN; i++)
		{
			TXstrncpy(d, confSuffixes[i],
				  (TxConfFile + TxConfFile_SZ) - d);
			TxConf = openconffile(TxConfFile, 0);
			if (TxConf != CONFFILEPN) break;
		}
		/* If none succeeded, fall back to preferred name: */
		if (TxConf == CONFFILEPN)
			TXstrncpy(d, confSuffixes[0],
				  (TxConfFile + TxConfFile_SZ) - d);
		didConfOpen = 1;
#else /* !EPI_ENABLE_CONF_TEXIS_INI */
		TXstrncpy(d, TX_TEXIS_INI_NAME,
			  (TxConfFile + TxConfFile_SZ) - d);
#endif /* !EPI_ENABLE_CONF_TEXIS_INI */
	}

	/* 4th: open TxConfFile (after --texis-conf/default): */
	if (!didConfOpen)
		TxConf = openconffile(TxConfFile, 0);	/* silent for texis */

	/* These stripped options will be passed to exec'd monitor,
	 * anytotx, etc.  Set this after TXsetargv() called:
	 */
	argvGlobalOpts[argcGlobalOpts] = CHARPN;
	TXsetglobaloptsargv(pmbuf, argcGlobalOpts, argvGlobalOpts);

	txGetApicpDefaults(pmbuf);		/* Bug 6933 even if !TxConf */

	TXApp->betafeatures[BETA_JSON] = 1;

	if (TxConf != CONFFILEPN)
	{
		const char	*s;

		/* Set Compatibility Version first, in case other settings
		 * override parts of it:
		 */
		if ((s = getconfstring(TxConf, texisSectionName,
				       "Compatibility Version", NULL)) != NULL)
		{
			char	errBuf[1024];

			if (!TXAppSetCompatibilityVersion(TXApp, s,
						     errBuf, sizeof(errBuf)))
				txpmbuf_putmsg(pmbuf, MWARN + UGE, NULL,
			  "Cannot set [Texis] Compatibility Version `%s': %s",
					       s, errBuf);
		}

		/* always at least set Lib Path for texis: */
		s = getconfstring(TxConf, texisSectionName,"Lib Path",CHARPN);
		if (s != CHARPN) TXsetlibpath(pmbuf, s);
		s = getconfstring(TxConf, texisSectionName, "Write Timeout",
				  CHARPN);
		if (s != CHARPN)
		{
			doubleVal = strtod(s, &e);
			if (e == s || *e != '\0' || doubleVal < (double)0.0)
				txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
				    "Invalid [%s] Write Timeout value `%s'",
					texisSectionName, s);
			else
				TXwriteTimeout = doubleVal;
		}
		s = getconfstring(TxConf, texisSectionName,
				  "Varchar To Strlst Sep", CHARPN);
		if (s != CHARPN)
		{
			TXstrlstCharConfig sep;

			if(TXstrToTxvssep(pmbuf,
					     "[Texis] Varchar To Strlst Sep",
					     s, NULL, &sep) == 0)
      {
        TXApp->charStrlstConfigFromIni = sep;
        TXApp->charStrlstConfig = sep;
      }
		}

		s = getconfstring(TxConf, texisSectionName,
				  "Createlocks Methods", CHARPN);
		if (s != CHARPN)
			TXsetCreateLocksMethods(pmbuf, TXApp,
						"[Texis] Createlocks Methods",
						s, -1);

		TXApp->createLocksTimeout = getconfint(TxConf,texisSectionName,
			    "Createlocks Timeout", TXApp->createLocksTimeout);

		s = getconfstring(TxConf, texisSectionName,
				  createDbOkDirExists, CHARPN);
		if (s)
		{
			i = TXgetBooleanOrInt(pmbuf, texisSectionName,
					   createDbOkDirExists, s, CHARPN, 3);
			if (i != -1)
				TXApp->createDbOkDirExists = (byte)i;
		}

		s = getconfstring(TxConf, texisSectionName,
				  validateBtrees, CHARPN);
		if (s)
		{
			i = TXgetBooleanOrInt(pmbuf, texisSectionName,
					      validateBtrees, s, CHARPN, 1);
			if (i != -1) TXApp->validateBtrees = i;
		}

		s = getconfstring(TxConf, texisSectionName, trap, CHARPN);
		if (s)
		{
			i = TXgetBooleanOrInt(pmbuf, texisSectionName, trap,
					      s, CHARPN, 1);
			if (i != -1)
				TXApp->trap = (TXtrap)i;
		}

		s = getconfstring(TxConf, texisSectionName,
				  unneededRexEscapeWarning, CHARPN);
		if (s)
		{
			i = TXgetBooleanOrInt(pmbuf, texisSectionName,
				     unneededRexEscapeWarning, s, CHARPN, 1);
			if (i != -1)
			{
				extern int TXunneededRexEscapeWarning;
				TXunneededRexEscapeWarning = i;
			}
		}

		s = getconfstring(TxConf, texisSectionName,
				  vortexAddTrailingSlash, CHARPN);
		if (s != CHARPN)
		{
			i = TXgetBooleanOrInt(pmbuf, texisSectionName,
					vortexAddTrailingSlash, s, CHARPN, 3);
			if (i != -1)
				TXApp->vortexAddTrailingSlash = (byte)i;
		}
		/* Obfuscate "Trace License": */
		tmp[0] = 'T';		tmp[1] = 'r';
		tmp[2] = 'a';		tmp[3] = 'c';
		tmp[4] = 'e';		tmp[5] = ' ';
		tmp[6] = 'L';		tmp[7] = 'i';
		tmp[8] = 'c';		tmp[9] = 'e';
		tmp[10] = 'n';		tmp[11] = 's';
		tmp[12] = 'e';		tmp[13] = '\0';
		TXApp->traceLicense = getconfint(TxConf, texisSectionName,
						 tmp, TXApp->traceLicense);

		TXApp->traceMutex = getconfint(TxConf, texisSectionName,
					"Trace Mutex", TXApp->traceMutex);

		s = getconfstring(TxConf, texisSectionName,
				  "Trace Locks Database", CHARPN);
		if (s && *s)
		{
			TXApp->traceLocksDatabase =
				TXfree(TXApp->traceLocksDatabase);
			TXApp->traceLocksDatabase = TXstrdup(pmbuf, fn, s);
		}
		s = getconfstring(TxConf, texisSectionName,
				  "Trace Locks Table", CHARPN);
		if (s && *s)
		{
			TXApp->traceLocksTable =
				TXfree(TXApp->traceLocksTable);
			TXApp->traceLocksTable = TXstrdup(pmbuf, fn, s);
		}

		TXtracedumptable = getconfint(TxConf, texisSectionName,
					"Trace Dumptable", TXtracedumptable);

		s = getconfstring(TxConf, texisSectionName,
				  multiValueToMultiRow, CHARPN);
		if (s != CHARPN)
		{
			i = TXgetBooleanOrInt(pmbuf, texisSectionName,
					multiValueToMultiRow, s, CHARPN, 3);
			if (i != -1)
				TXApp->multiValueToMultiRow = (byte)i;
		}
		s = getconfstring(TxConf, texisSectionName,
				  texisDefaultsWarning, CHARPN);
		if (s != CHARPN)
		{
			i = TXgetBooleanOrInt(pmbuf, texisSectionName,
					texisDefaultsWarning, s, CHARPN, 3);
			if (i != -1)
				TXApp->texisDefaultsWarning = (byte)i;
		}
		TXApp->blobCompressExe = getconfstring(TxConf, texisSectionName,"Blob Compress EXE",CHARPN);
		TXApp->blobUncompressExe = getconfstring(TxConf, texisSectionName,"Blob Uncompress EXE",CHARPN);

		s = getconfstring(TxConf, texisSectionName,
				  legacyVersion7OrderByRank, CHARPN);
		if (s)
		{
			i = TXgetBooleanOrInt(pmbuf, texisSectionName,
					      legacyVersion7OrderByRank, s,
					      CHARPN, 3);
			if (i != -1)
				TXApp->legacyVersion7OrderByRank = (byte)i;
		}

		s = getconfstring(TxConf, texisSectionName,
				  "IN Mode", CHARPN);
		if (s != CHARPN)
		{				/* see also setprop.c */
			if (strcmpi(s, "intersect") == 0)
				TXApp->inModeIsSubset = 0;
			else if (strcmpi(s, "subset") == 0)
				TXApp->inModeIsSubset = 1;
			else
				txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN,
					"Unknown [%s] IN Mode value `%s'",
						texisSectionName, s);
		}

		s = getconfstring(TxConf, texisSectionName, hexifyBytesStr,
				  CHARPN);
		if (s != CHARPN)
		{
			i = TXgetBooleanOrInt(pmbuf, texisSectionName,
					      hexifyBytesStr, s, CHARPN, 3);
			if (i != -1)
				TXApp->hexifyBytes = (byte)i;
		}

		s = getconfstring(TxConf, texisSectionName, preLoadBlobsStr,
				  CHARPN);
		if (s != CHARPN)
		{
			i = TXgetBooleanOrInt(pmbuf, texisSectionName,
					      preLoadBlobsStr, s, CHARPN, 3);
			if (i != -1)
				TXApp->preLoadBlobs = (byte)i;
		}

		if (TX_PWENCRYPT_METHODS_ENABLED(TXApp) &&
		    (s = getconfstring(TxConf, texisSectionName,
				       defaultPasswordEncryptionMethod,
				       NULL)) != NULL)
		{
			TXpwEncryptMethod	method;

			method = TXpwEncryptMethodStrToEnum(s);
			if (method == TXpwEncryptMethod_Unknown)
				txpmbuf_putmsg(pmbuf, MERR + UGE, NULL,
					       "Unknown [%s] %s value `%s'",
					       texisSectionName,
					       defaultPasswordEncryptionMethod,
					       s);
			else
				TXApp->defaultPasswordEncryptionMethod =
					method;
		}

		if (TX_PWENCRYPT_METHODS_ENABLED(TXApp))
		{
			int	intVal;

			intVal = getconfint(TxConf, texisSectionName,
					    defaultPasswordEncryptionRounds,
					    TX_PWENCRYPT_ROUNDS_DEFAULT);
			TXAppSetDefaultPasswordEncryptionRounds(pmbuf, TXApp,
								intVal);
		}

		s = getconfstring(TxConf, texisSectionName,
				  restoreStdioInheritance, CHARPN);
		if (s)
		{
			i = TXgetBooleanOrInt(pmbuf, texisSectionName,
				       restoreStdioInheritance, s, CHARPN, 3);
			if (i != -1)
				TXApp->restoreStdioInheritance = (TXbool)i;
		}
	}


#ifdef _WIN32
	/* Set stdio handles non-inheritable once at startup, if not
	 * [Texis] Restore Stdio Inheritance (which if true, means we
	 * would instead set both non-inherit and restore every
	 * TXpopenduplex() call, which is a race condition amongst
	 * threads, e.g. Vortex in monitor web server).  Kinda
	 * thread-unsafe, but assumed ok since this is TXinitapp()
	 * which must be called before any threads start:
	 */
	if (!TXApp->restoreStdioInheritance)
	{
		DWORD		dwParentStdOrgHandleInfo[TX_NUM_STDIO_HANDLES];
		TXFHANDLE	hParentStd[TX_NUM_STDIO_HANDLES];

		hParentStd[STDIN_FILENO] = TXFHANDLE_STDIN;
		hParentStd[STDOUT_FILENO] = TXFHANDLE_STDOUT;
		hParentStd[STDERR_FILENO] = TXFHANDLE_STDERR;
		TXpopenSetParentStdioNonInherit(pmbuf, hParentStd,
		                                dwParentStdOrgHandleInfo);
	}
#endif /* _WIN32 */

	ret = 0;				/* success */

finally:
	/* Compress stripped argv array, and dup elements: - - - - - - - - */
	if (argvStripped != CHARPPPN)
	{
		for (sp = dp = *argvStripped; sp < *argvStripped + argc; sp++)
			if (*sp != CHARPN)
			{
				*dp = TXstrdup(pmbuf, __FUNCTION__, *sp);
				if (!*dp) break;
				dp++;
			}
		if (argcStripped != INTPN)
			*argcStripped = (int)(dp - *argvStripped);
		*dp = CHARPN;
	}
	return(ret);
}

void
TXcloseapp()
/* Called at process end, mainly for MEMDEBUG cleanup.
 */
{
	size_t	i;

	if (TXApp == TXAPPPN) goto done;

	TXapicpFreeDefaultStr((char *)TxApicpBuiltinDefault.ueqprefix);
	TxApicpBuiltinDefault.ueqprefix = (byte *)TxEqPrefixDefault;
	TXapicpFreeDefaultStr((char *)TxApicpDefault.ueqprefix);
	TxApicpDefault.ueqprefix = (byte *)TxEqPrefixDefault;

	for (i = 0; i < TXApp->fldopCacheSz; i++)
		if (TXApp->fldopCache[i] != FLDOPPN)
			TXApp->fldopCache[i] = foclose(TXApp->fldopCache[i]);
	TXApp->traceRowFieldsTables =
		TXapi3FreeNullList(TXApp->traceRowFieldsTables);
	TXApp->traceRowFieldsFields =
		TXapi3FreeNullList(TXApp->traceRowFieldsFields);

	TXApp = TXfree(TXApp);

	TXsetlibpath(TXPMBUFPN, CHARPN);
	TXfreeabendcache();
	TXfreeAllProcs();
	TxGlobalOptsArgv = TXfreeStrList(TxGlobalOptsArgv, TxGlobalOptsArgc);
done:
	return;
}

int
TXsetProcessDescriptionPrefixFromPath(TXPMBUF *pmbuf, const char *path)
/* Sets process description prefix to e.g. `texis: ' given
 * `c:/morph3/bin/texis.exe'.
 */
{
	char	*newPfx = NULL, *base;
	int	ret;
	size_t	baseLen;

	base = TXbasename(path);		/* strip leading dir */
	baseLen = TXfileext(base) - base;	/* strip `.exe' */
	newPfx = (char *)TXmalloc(pmbuf, __FUNCTION__, baseLen + 3);
	if (!newPfx) goto err;
	memcpy(newPfx, base, baseLen);
	newPfx[baseLen] = ':';
	newPfx[baseLen + 1] = ' ';
	newPfx[baseLen + 2] = '\0';
	TxProcessDescriptionPrefix = TXfree(TxProcessDescriptionPrefix);
	TxProcessDescriptionPrefix = newPfx;
	newPfx = NULL;
	TxProcessDescriptionPrefixIsDefault = TXbool_False;
	ret = 1;
	goto finally;

err:
	ret = 0;
finally:
	newPfx = TXfree(newPfx);
	return(ret);
}

int
TXsetargv(pmbuf, argc, argv)
TXPMBUF	*pmbuf;
int	argc;
char	**argv;
/* `argc'/`argv' *must* be live (true main() `argv' pointers): they
 * might be overwritten later to change appearance in ps (via
 * TXsetProcessDescription()).
 * Returns 0 on error (out of mem).
 */
{
	char	**newArgv = NULL;
	int	i;
	size_t	sz;

	/* Sanity checks: */
	if (argc < 0 || !argv)
	{
		argc = 0;
		argv = NULL;
	}

	/* Dup args to `TxOrgArgv' for later use of values: */
	if (argv && argc > 0 &&
	    (newArgv = TXdupStrList(pmbuf, argv, argc)) == NULL)
		return(0);
	TXfreeStrList(TxOrgArgv, TxOrgArgc);
	TxOrgArgv = newArgv;
	newArgv = NULL;
	TxOrgArgc = argc;

	/* Save live pointers for ps manipulation: */
	TxLiveArgc = argc;
	TxLiveArgv = argv;
	/* Find out how many initial args are contiguously stored: */
	for (i = 0, sz = 0;
	     i < TxLiveArgc && TxLiveArgv[0] + sz == TxLiveArgv[i];
	     i++)
		sz += strlen(TxLiveArgv[i]) + 1;
	TxLiveArgvContiguousSize = sz;
	TxLiveArgvNonContiguousIndexStart = i;

	/* Set default process description prefix: */
	if (TxProcessDescriptionPrefixIsDefault && argv && argv[0])
	{
		TXsetProcessDescriptionPrefixFromPath(pmbuf, argv[0]);
		/* maintain defaultness: */
		TxProcessDescriptionPrefixIsDefault  = TXbool_True;
	}

	return(1);
}

void
TXsetglobaloptsargv(pmbuf, argc, argv)
TXPMBUF	*pmbuf;		/* (in, opt.) buffer for putmsgs */
int	argc;
char	**argv;
{
	TxGlobalOptsArgv = TXfreeStrList(TxGlobalOptsArgv, TxGlobalOptsArgc);
	TxGlobalOptsArgc = 0;
	if (argv != CHARPPN)
	{
		TxGlobalOptsArgv = TXdupStrList(pmbuf, argv, argc);
		TxGlobalOptsArgc = (TxGlobalOptsArgv != CHARPPN ? argc : 0);
	}
}

#ifdef _WIN32
static int
TXgrabOrgCommandLine(TXPMBUF *pmbuf, PROCESS_BASIC_INFORMATION *procInfo)
{
	int	ret;

	TxOrgCommandLine = TXfree(TxOrgCommandLine);

	TxOrgCommandLineMaxByteLen = procInfo->PebBaseAddress->
		ProcessParameters->CommandLine.MaximumLength;
	if (TxOrgCommandLineMaxByteLen > sizeof(wchar_t))
		TxOrgCommandLineMaxByteLen -= sizeof(wchar_t);	/* - nul */

	TxOrgCommandLineByteLen = (procInfo->PebBaseAddress->
				   ProcessParameters->CommandLine.Length/
				   sizeof(wchar_t))*sizeof(wchar_t);
	TxOrgCommandLine = (wchar_t *)TXmalloc(pmbuf, __FUNCTION__,
				   TxOrgCommandLineByteLen + sizeof(wchar_t));
	if (!TxOrgCommandLine)
	{
		TxOrgCommandLineByteLen = 0;
		goto err;
	}
	memcpy(TxOrgCommandLine, procInfo->PebBaseAddress->
	       ProcessParameters->CommandLine.Buffer,
	       TxOrgCommandLineByteLen);
	TxOrgCommandLine[TxOrgCommandLineByteLen/sizeof(wchar_t)] = (wchar_t)0;
	ret = 1;
	goto finally;

err:
	ret = 0;
finally:
	return(ret);
}

static int
TXgetBasicProcInfo(TXPMBUF *pmbuf, PROCESS_BASIC_INFORMATION *procInfo)
{
	HANDLE	processHandle = NULL;
	int	ret;

	processHandle = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,
				    FALSE, GetCurrentProcessId());
	if (!processHandle)
	{
		txpmbuf_putmsg(pmbuf, MERR + FOE, __FUNCTION__,
			       "Cannot open handle to current process: %s",
			       TXstrerror(TXgeterror()));
		goto err;
	}
	if (!TXqueryInformationProcess(pmbuf, processHandle,
				       ProcessBasicInformation, procInfo,
				       sizeof(PROCESS_BASIC_INFORMATION)))
		goto err;
	if (!procInfo->PebBaseAddress ||
	    !procInfo->PebBaseAddress->ProcessParameters ||
	    !procInfo->PebBaseAddress->ProcessParameters->CommandLine.Buffer)
	{
		txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
			       "Missing pointers in command line information");
		goto err;
	}
	ret = 1;
	goto finally;

err:
	ret = 0;
finally:
	if (processHandle)
	{
		CloseHandle(processHandle);
		processHandle = NULL;
	}
	return(ret);
}
#endif /* _WIN32 */

size_t
TXgetMaxProcessDescriptionLen(TXPMBUF *pmbuf)
/* Returns max length of message that can be set (without truncation)
 * by TXsetProcessDescription().  Length is bytes for Unix, chars for
 * Windows; sans prefix.
 */
{
	size_t	n, pfxLen;

#ifdef _WIN32
	if (!TxOrgCommandLine)
	{
		/* No `TxOrgCommandLineMaxByteLen/sizeof' yet.  Get it: */
		PROCESS_BASIC_INFORMATION	procInfo;

		if (!TXgetBasicProcInfo(pmbuf, &procInfo)) return(0);
		if (!TXgrabOrgCommandLine(pmbuf, &procInfo)) return(0);
	}

	n = TxOrgCommandLineMaxByteLen/sizeof(wchar_t);
#else /* !_WIN32 */
	(void)pmbuf;
	n = TxLiveArgvContiguousSize;
	if (n > 0) n--;				/* for nul terminator */
#endif /* !_WIN32 */

	/* Discount prefix length: */
	if (TxProcessDescriptionPrefix)
	{
		pfxLen = strlen(TxProcessDescriptionPrefix);
		if (n > pfxLen) n -= pfxLen;
		else n = 0;
	}

	return(n);
}

const char *
TXgetProcessDescription(void)
/* Returns currently-set process description (may be NULL if none set);
 * sans prefix.
 */
{
	return(TxLiveProcessDescription);
}

int
TXsetProcessDescription(TXPMBUF *pmbuf, const char *msg)
/* Overwrites live args with prefix plus `msg' (may be truncated) for ps
 * and Task Manager/Process Explorer.
 * Call with NULL to restore original args.  May be called multiple times.
 * Returns 2 on success, 1 if truncated, 0 on error.
 */
{
#ifdef _WIN32
	wchar_t				*wideMsg = NULL;
	size_t				wideMsgCharLen = 0, charLenToUse = 0;
	int				ret = 0;
	PROCESS_BASIC_INFORMATION	procInfo;
	char				*mergedMsg = NULL;

	if (!TXgetBasicProcInfo(pmbuf, &procInfo)) goto err;

	/* Save original command line, if not yet: */
	if (!TxOrgCommandLine &&
	    !TXgrabOrgCommandLine(pmbuf, &procInfo))
		goto err;

	/* Live command line buffer is wide char; convert ours: */
	if (msg)
	{
		mergedMsg = TXstrcatN(pmbuf, __FUNCTION__,
				      (TxProcessDescriptionPrefix ?
				       TxProcessDescriptionPrefix : ""), msg,
				      NULL);
		if (!mergedMsg) goto err;
		wideMsg = TXutf8ToWideChar(pmbuf, mergedMsg,
					   strlen(mergedMsg));
		if (!wideMsg) goto err;
		for (wideMsgCharLen = 0;	/* wide-char strlen() */
		     wideMsg[wideMsgCharLen];
		     wideMsgCharLen++);
	}
	else					/* restore original */
	{
		wideMsg = TxOrgCommandLine;
		wideMsgCharLen = TxOrgCommandLineByteLen/sizeof(wchar_t);
	}

	/* Compute length of `wideMsg' we can copy, and maybe add `...': */
	ret = 2;				/* `wideMsg' fits (so far) */
	charLenToUse = procInfo.PebBaseAddress->ProcessParameters->
		CommandLine.MaximumLength/sizeof(wchar_t);
	if (charLenToUse > 0) charLenToUse--;	/* allow for nul */
	else					/* no room even for nul */
	{
		ret = 1;
		goto finally;
	}
	if (charLenToUse < wideMsgCharLen)	/* not enough room */
	{
		ret = 1;
		/* Change end of message to `...' to indicate truncation,
		 * so e.g. we know a path at end is truncated.
		 * But do not waste space on `...' if real short,
		 * e.g. do not make `monit' into `mo...':
		 */
		if (wideMsg != TxOrgCommandLine && charLenToUse > 10)
		{
			size_t	idx, charLen = charLenToUse;
			char	*d, *e;

			for (idx = charLenToUse - 3; idx < charLenToUse; idx++)
				wideMsg[idx] = (wchar_t)'.';
			if (mergedMsg)
			{
				e = TXunicodeGetUtf8CharOffset(mergedMsg, NULL,
							       &charLen);
				for (d = e - 3; d < e; d++) *d = '.';
			}
		}
	}
	else
		charLenToUse = wideMsgCharLen;

	/* Copy the new `wideMsg' live, truncating to `charLenToUse': */
	memcpy(procInfo.PebBaseAddress->ProcessParameters->CommandLine.Buffer,
	       wideMsg, charLenToUse*sizeof(wchar_t));
   ((wchar_t *)procInfo.PebBaseAddress->ProcessParameters->CommandLine.Buffer)
		[charLenToUse] = 0;		/* nul-terminate */
	procInfo.PebBaseAddress->ProcessParameters->CommandLine.Length =
		(USHORT)(charLenToUse*sizeof(wchar_t));

	/* Update our copy of live description, reflecting any truncation: */
	TxLiveProcessDescription = TXfree(TxLiveProcessDescription);
	if (mergedMsg)
	{
		size_t	charLen = charLenToUse;
		/* Save from after prefix to truncated end: */
		char	*mergedMsgSaveBegin, *mergedMsgSaveEnd;

		mergedMsgSaveBegin = mergedMsg;
		if (TxProcessDescriptionPrefix)
		    mergedMsgSaveBegin += strlen(TxProcessDescriptionPrefix);
		mergedMsgSaveEnd = TXunicodeGetUtf8CharOffset(mergedMsg, NULL,
							      &charLen);
		TxLiveProcessDescription = TXstrndup(pmbuf, __FUNCTION__,
						     mergedMsgSaveBegin,
				      (mergedMsgSaveEnd - mergedMsgSaveBegin));
	}

	goto finally;

err:
	ret = 0;
finally:
	if (wideMsg != TxOrgCommandLine) wideMsg = TXfree(wideMsg);
	mergedMsg = TXfree(mergedMsg);
	return(ret);
#else /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	int	i, ret = 0;
	size_t	mergedMsgLen, lenToCopy = 0;
	char	*mergedMsg = NULL;

	if (!TxLiveArgv || TxLiveArgc < 1 || TxLiveArgvContiguousSize <= 0)
		return(0);

	if (msg)
	{
		mergedMsg = TXstrcatN(pmbuf, __FUNCTION__,
				      (TxProcessDescriptionPrefix ?
				       TxProcessDescriptionPrefix : ""), msg,
				      NULL);
		if (!mergedMsg) goto err;
		/* Overwrite initial contiguous block with `mergedMsg': */
		memset(TxLiveArgv[0], 0, TxLiveArgvContiguousSize);
		mergedMsgLen = strlen(mergedMsg);
		if (mergedMsgLen < TxLiveArgvContiguousSize)
		{				/* `mergedMsg' fits */
			lenToCopy = mergedMsgLen;
			ret = 2;
		}
		else				/* truncate */
		{
			lenToCopy = TxLiveArgvContiguousSize - 1;
			ret = 1;
			/* See above truncation comments: */
			if (lenToCopy > 10)
			{
				size_t	idx;

				for (idx = lenToCopy - 3;
				     idx < lenToCopy;
				     idx++)
					mergedMsg[idx] = '.';
			}
		}
		memcpy(TxLiveArgv[0], mergedMsg, lenToCopy);
		/* Clear out rest of args (non-contiguous), if any: */
		for (i = TxLiveArgvNonContiguousIndexStart; i <TxLiveArgc; i++)
			if (TxLiveArgv[i])
				memset(TxLiveArgv[i], 0,strlen(TxLiveArgv[i]));
	}
	else					/* restore original */
	{
		for (i = 0; i < TxLiveArgc; i++)
			strcpy(TxLiveArgv[i], TxOrgArgv[i]);
		ret = 2;
	}

	/* Update our copy of live description, reflecting any truncation: */
	TxLiveProcessDescription = TXfree(TxLiveProcessDescription);
	if (mergedMsg)
	{
		/* Save from after prefix to truncated end: */
		char	*mergedMsgSaveBegin, *mergedMsgSaveEnd;

		mergedMsgSaveBegin = mergedMsg;
		if (TxProcessDescriptionPrefix)
		    mergedMsgSaveBegin += strlen(TxProcessDescriptionPrefix);
		mergedMsgSaveEnd = mergedMsg + lenToCopy;
		TxLiveProcessDescription = TXstrndup(pmbuf, __FUNCTION__,
						     mergedMsgSaveBegin,
				      (mergedMsgSaveEnd - mergedMsgSaveBegin));
	}

	goto finally;

err:
	ret = 0;
finally:
	mergedMsg = TXfree(mergedMsg);
	return(ret);
#endif /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
}

int
TXsetFreeMemAtExit(int on)
/* Returns 0 on error.
 */
{
	TXfreeMemAtExit = !!on;
	return(1);
}

int
TXgetFreeMemAtExit(void)
/* Thread-safe.  Signal-safe.
 */
{
	return(TXfreeMemAtExit);
}

TXbool
TXgetDiscardUnsetParameterClauses(void)
{
	return(TXdiscardUnsetParameterClauses);
}

TXbool
TXsetDiscardUnsetParameterClauses(TXbool discard)
{
	TXdiscardUnsetParameterClauses = !!discard;
	return(TXbool_True);
}
