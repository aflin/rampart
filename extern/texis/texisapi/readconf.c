/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#  include <io.h>
#endif
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#include "dbquery.h"
#include "texint.h"


typedef struct SECTION_tag
{
  char          name[8];			/* alloced as needed */
}
SECTION;
#define SECTIONPN       ((SECTION *)NULL)
#define SECTIONPPN      ((SECTION **)NULL)

typedef struct SETTING
{                                               /* A = alloced */
  char          *name;                          /* A */
  SECTION       *section;
  char          *rawValue;                      /* A value before expansion */
  char          *expandedValue;                 /* A value after expansion */
  int           expandedSerial;                 /*   serial # at expansion */
  int           doVarExpansion;                 /*   whether to do var exp. */
}
SETTING;
#define SETTINGPN       ((SETTING *)NULL)

struct CONFFILE_tag
{
  SETTING       *settings;                      /* all settings */
  SECTION       **sections;                  	/* all sections */
  size_t        nsettings;
  size_t        nsections;
  char          *scriptRootValue;		/* Script Root */
  int           scriptRootValueIsExpanded;	/* nonzero: "" var-expanded */
  char          *documentRootValue;		/* Document Root */
  int           documentRootValueIsExpanded;    /* nonzero: "" var-expanded */
  char          *serverRootValue;		/* Server Root */
  int           serverRootValueIsExpanded;	/* nonzero: "" var-expanded */
#ifdef EPI_ENABLE_LOGDIR_RUNDIR
  char          *logDirValue;                   /* [Texis] Log Dir */
  TXbool        logDirValueIsExpanded;
  char          *runDirValue;                   /* [Texis] Run Dir */
  TXbool        runDirValueIsExpanded;
#endif /* EPI_ENABLE_LOGDIR_RUNDIR */
  int           curExpandedSerialNum;
};


/******************************************************************/

static SECTION *TXconfCloseSection ARGS((SECTION *section));
static SECTION *
TXconfCloseSection(section)
SECTION *section;
{
  if (section == SECTIONPN) return(SECTIONPN);
  section = TXfree(section);			/* frees `name' too */
  return(SECTIONPN);
}

CONFFILE *
closeconffile(conffile)
CONFFILE *conffile;
{
	size_t i;
	SETTING *s;

	if (conffile)
	{
		for (i = 0; i < conffile->nsettings; i++)
		{
			s = conffile->settings + i;
			s->name = TXfree(s->name);
			s->rawValue = TXfree(s->rawValue);
			s->expandedValue = TXfree(s->expandedValue);
		}
		conffile->settings = TXfree(conffile->settings);
		for (i = 0; i < conffile->nsections; i++)
			TXconfCloseSection(conffile->sections[i]);
		conffile->sections = TXfree(conffile->sections);
		conffile->scriptRootValue =
			TXfree(conffile->scriptRootValue);
		conffile->documentRootValue =
			TXfree(conffile->documentRootValue);
		conffile->serverRootValue =
			TXfree(conffile->serverRootValue);
#ifdef EPI_ENABLE_LOGDIR_RUNDIR
		conffile->logDirValue = TXfree(conffile->logDirValue);
		conffile->runDirValue = TXfree(conffile->runDirValue);
#endif /* EPI_ENABLE_LOGDIR_RUNDIR */
		conffile = TXfree(conffile);
	}
	return NULL;
}

/******************************************************************/

char *
TXconfExpandRawValue(pmbuf, conffile, rawValue)
TXPMBUF         *pmbuf;         /* (out, opt.) buffer for msgs */
CONFFILE        *conffile;          /* (in) config file */
const char      *rawValue;    /* (in) value to expand */
/* Returns malloc'd %-var-expanded copy of `rawValue'.
 * NOTE: see also TXvhttpdReplaceVarsMergePath(), which may expand
 * raw values from a CONFFILE too.
 */
{
  const char *          vars[TX_INSTBINVARS_NUM + 8];
  const char *          vals[TX_INSTBINVARS_NUM + 8];
  int                   valsAreExpanded[TX_INSTBINVARS_NUM + 8];
  size_t                i;

  for (i = 0; i < TX_INSTBINVARS_NUM; i++)
    {
      vars[i] = TxInstBinVars[i];
      vals[i] = TxInstBinVals[i];
      valsAreExpanded[i] = 1;
    }
  if (conffile->scriptRootValue != CHARPN)
    {
      vars[i] = "SCRIPTROOT";
      vals[i] = conffile->scriptRootValue;
      valsAreExpanded[i++] = conffile->scriptRootValueIsExpanded;
    }
  if (conffile->documentRootValue != CHARPN)
    {
      vars[i] = "DOCUMENT_ROOT";
      vals[i] = conffile->documentRootValue;
      valsAreExpanded[i++] = conffile->documentRootValueIsExpanded;
      vars[i] = "DOCUMENTROOT";
      vals[i] = conffile->documentRootValue;
      valsAreExpanded[i++] = conffile->documentRootValueIsExpanded;
    }
  if (conffile->serverRootValue != CHARPN)
    {
      vars[i] = "SERVERROOT";
      vals[i] = conffile->serverRootValue;
      valsAreExpanded[i++] = conffile->serverRootValueIsExpanded;
    }
#ifdef EPI_ENABLE_LOGDIR_RUNDIR
  if (conffile->logDirValue)
    {
      vars[i] = "LOGDIR";
      vals[i] = conffile->logDirValue;
      valsAreExpanded[i++] = conffile->logDirValueIsExpanded;
    }
  if (conffile->runDirValue)
    {
      vars[i] = "RUNDIR";
      vals[i] = conffile->runDirValue;
      valsAreExpanded[i++] = conffile->runDirValueIsExpanded;
    }
#endif /* EPI_ENABLE_LOGDIR_RUNDIR */
  vars[i] = CHARPN;
  vals[i] = CHARPN;
  valsAreExpanded[i] = 0;
  return(tx_replacevars(pmbuf, rawValue, 1, vars, i, vals,
                        valsAreExpanded));
}

int
TXconfSetServerRootVar(conf, serverRoot, isExpanded)
CONFFILE        *conf;          /* (in/out) CONFFILE object */
const char      *serverRoot;    /* (in, opt.) SERVER_ROOT to use */
int             isExpanded;     /* (in) nonzero: serverRoot already expanded*/
/* Sets value to use for %SERVERROOT% replacement.
 * Default is none.
 * Returns 0 on error.
 */
{
  TXPMBUF       *pmbuf = TXPMBUFPN;

  conf->serverRootValue = TXfree(conf->serverRootValue);
  /* Increment serial number, to invalidate all `SETTING.expandedValue's: */
  conf->curExpandedSerialNum++;
  if (serverRoot != CHARPN &&
      !(conf->serverRootValue = TXstrdup(pmbuf, __FUNCTION__, serverRoot)))
    return(0);
  conf->serverRootValueIsExpanded = isExpanded;
  return(1);
}

int
TXconfSetDocumentRootVar(conf, docRoot, isExpanded)
CONFFILE        *conf;          /* (in/out) CONFFILE object */
const char      *docRoot;       /* (in, opt.) DOCUMENT_ROOT to use */
int             isExpanded;     /* (in) nonzero: docRoot already %-expanded */
/* Sets value to use for %DOCUMENT_ROOT% replacement.
 * Default is [Httpd] Document Root.
 * Returns 0 on error.
 */
{
  TXPMBUF       *pmbuf = TXPMBUFPN;

  conf->documentRootValue = TXfree(conf->documentRootValue);
  /* Increment serial number, to invalidate all `SETTING.expandedValue's: */
  conf->curExpandedSerialNum++;
  if (docRoot != CHARPN &&
      !(conf->documentRootValue = TXstrdup(pmbuf, __FUNCTION__, docRoot)))
    return(0);
  conf->documentRootValueIsExpanded = isExpanded;
  return(1);
}

int
TXconfSetScriptRootVar(conf, scriptRoot, isExpanded)
CONFFILE        *conf;          /* (in/out) CONFFILE object */
const char      *scriptRoot;    /* (in, opt.) SCRIPTROOT to use */
int             isExpanded;     /* (in) nonzero: scriptRoot already expanded*/
/* Sets value to use for %SCRIPTROOT% replacement.
 * Default is [Texis] Script Root.
 * Returns 0 on error.
 */
{
  TXPMBUF       *pmbuf = TXPMBUFPN;

  conf->scriptRootValue = TXfree(conf->scriptRootValue);
  /* Increment serial number, to invalidate all `SETTING.expandedValue's: */
  conf->curExpandedSerialNum++;
  if (scriptRoot != CHARPN &&
      !(conf->scriptRootValue = TXstrdup(pmbuf, __FUNCTION__, scriptRoot)))
    return(0);
  conf->scriptRootValueIsExpanded = isExpanded;
  return(1);
}

#ifdef EPI_ENABLE_LOGDIR_RUNDIR
static char *
TXconfGetLogRunDirFile(TXPMBUF          *pmbuf,      /* (in, opt.) */
                       TXbool           isRun,       /* else logdir */
                       CONFFILE         *conf,       /* (in, opt.) */
                       const char       *section,    /* (in, opt.) */
                       const char       *attrib,     /* (in, opt.) */
                       const char       *defaultFile)/* (in, opt.) */
/* Gets %LOGDIR% (or %RUNDIR% if `isRun')-based setting [section]
 * attrib.  If those are NULL or not found, returns `%LOGDIR%[/file]'.
 * If `conf' NULL, uses default %LOGDIR% for platform.
 * NOTE: see also TXvxSetDefaultErrorLog(), which has the same defaults
 * but is async-signal safe.  See also install script.
 * Returns alloced value.
 */
{
  char  *dir = NULL, *defaultVal = NULL, *val = NULL;

  if (conf)
    dir = TXconfExpandRawValue(pmbuf, conf, (isRun ? "%RUNDIR%" :"%LOGDIR%"));
  else
#  ifdef _WIN32
    dir = TXstrcatN(pmbuf, __FUNCTION__, TXINSTALLPATH_VAL,
                    (isRun ? PATH_SEP_S "run" : PATH_SEP_S "logs"), NULL);
#  else /* !_WIN32 */
    dir = TXstrdup(pmbuf, __FUNCTION__,
                   (isRun ? PATH_SEP_S "run" PATH_SEP_S "texis" :
                    PATH_SEP_S "var" PATH_SEP_S "log" PATH_SEP_S "texis"));
#  endif /* !_WIN32 */
  if (!dir) goto err;
  defaultVal = TXstrcatN(pmbuf, __FUNCTION__, dir,
                        (defaultFile ? PATH_SEP_S : NULL), defaultFile, NULL);
  if (!defaultVal) goto err;
  if (section && attrib)
    val = getconfstring(conf, section, attrib, defaultVal);
  else
    val = defaultVal;
  if (val == defaultVal)
    defaultVal = NULL;            		/* `val' owns it */
  else
    val = TXstrdup(pmbuf, __FUNCTION__, val);
  goto finally;

err:
  val = TXfree(val);
finally:
  dir = TXfree(dir);
  defaultVal = TXfree(defaultVal);
  return(val);
}

char *
TXconfGetLogDirFile(TXPMBUF     *pmbuf,         /* (in, opt.) */
                    CONFFILE    *conf,          /* (in, opt.) */
                    const char  *section,       /* (in, opt.) */
                    const char  *attrib,        /* (in, opt.) */
                    const char  *defaultFile)   /* (in, opt.) */
{
  return(TXconfGetLogRunDirFile(pmbuf, TXbool_False, conf, section, attrib,
                                defaultFile));
}

char *
TXconfGetRunDirFile(TXPMBUF     *pmbuf,         /* (in, opt.) */
                    CONFFILE    *conf,          /* (in, opt.) */
                    const char  *section,       /* (in, opt.) */
                    const char  *attrib,        /* (in, opt.) */
                    const char  *defaultFile)   /* (in, opt.) */
{
  return(TXconfGetLogRunDirFile(pmbuf, TXbool_True, conf, section, attrib,
                                defaultFile));
}

int
TXconfSetLogDirVar(CONFFILE	*conf,	    /* (in/out) CONFFILE object */
		   const char	*logDir,    /* (in, opt.) LOGDIR to use */
		   TXbool	isExpanded) /* (in) logDir already expanded */
/* Sets value to use for %LOGDIR% replacement.
 * Default is [Texis] Log Dir.
 * Returns 0 on error.
 */
{
  TXPMBUF       *pmbuf = TXPMBUFPN;

  conf->logDirValue = TXfree(conf->logDirValue);
  /* Increment serial number, to invalidate all `SETTING.expandedValue's: */
  conf->curExpandedSerialNum++;
  if (logDir != CHARPN &&
      !(conf->logDirValue = TXstrdup(pmbuf, __FUNCTION__, logDir)))
    return(0);
  conf->logDirValueIsExpanded = isExpanded;
  return(1);
}

int
TXconfSetRunDirVar(CONFFILE	*conf,	    /* (in/out) CONFFILE object */
		   const char	*runDir,    /* (in, opt.) RUNDIR to use */
		   TXbool	isExpanded) /* (in) runDir already expanded */
/* Sets value to use for %RUNDIR% replacement.
 * Default is [Texis] Run Dir.
 * Returns 0 on error.
 */
{
  TXPMBUF       *pmbuf = TXPMBUFPN;

  conf->runDirValue = TXfree(conf->runDirValue);
  /* Increment serial number, to invalidate all `SETTING.expandedValue's: */
  conf->curExpandedSerialNum++;
  if (runDir != CHARPN &&
      !(conf->runDirValue = TXstrdup(pmbuf, __FUNCTION__, runDir)))
    return(0);
  conf->runDirValueIsExpanded = isExpanded;
  return(1);
}
#endif /* EPI_ENABLE_LOGDIR_RUNDIR */

CONFFILE *
openconffile(filename, yap)
char *filename;
int yap;			/* 0: silent  1: non-ENOENT errors  2: all errors */
/*
 * NOTE: should use a TXPMBUF arg for putmsgs.
 */
{
	static const char whitespace[] = " \t\v\f\r\n";
#define EOLSPACE	(whitespace + 4)
	CONFFILE *rc = NULL;
	int fh = -1, saidTooManySettings = 0, saidTooManySections = 0;
	EPI_STAT_S st;
	char *t, *e, *buf = CHARPN, *ln, *eob, *nl;
	size_t nsections, nsettings, nasections, nasettings, len;
	size_t sz;
	SETTING *s;
	SECTION	*section;
	TXPMBUF	*pmbuf = (yap ? TXPMBUFPN : TXPMBUF_SUPPRESS);
	TXrawOpenFlag	roFlags;
	char tmp[65537];

	roFlags = TXrawOpenFlag_None;
	if (yap == 1) roFlags |= TXrawOpenFlag_SuppressNoSuchFileErr;
	fh = TXrawOpen(pmbuf, __FUNCTION__,
		       "Texis config file", filename, roFlags,
		       (O_RDONLY | TX_O_BINARY), 0666);
	if (fh == -1) goto err;
	buf = tmp;
	sz = (size_t) tx_rawread(pmbuf, fh, filename, (byte *)buf,
				 sizeof(tmp) - 1,
			0 /* don't yap: file may be shorter than our read */);
	if (sz == sizeof(tmp) - 1)	/* could be more data */
	{
		if (EPI_FSTAT(fh, &st) != 0)
		{
			if (yap >= 2 || (yap >= 1 && errno != ENOENT))
				txpmbuf_putmsg(pmbuf, MERR + FOE, NULL,
				       "Could not open Texis config file %s: %s",
				       filename, strerror(errno));
			goto err;
		}
		if (!(buf = (char *)TXmalloc(pmbuf, __FUNCTION__,
					     (size_t) st.st_size + 1)))
			goto err;
		memcpy(buf, tmp, sizeof(tmp) - 1);
		sz +=
			(size_t) tx_rawread(pmbuf, fh, filename,
                                            (byte *)buf + sizeof(tmp) - 1,
					      (size_t) st.st_size -
					      (sizeof(tmp) - 1), (yap >= 1));
	}
	close(fh);
	fh = -1;
	eob = buf + sz;
	*eob = '\0';
	rc = (CONFFILE *)TXcalloc(pmbuf, __FUNCTION__, 1, sizeof(CONFFILE));
	if (rc == CONFFILEPN)
		goto err;

	nasections = nasettings = 0;
	for (ln = buf; ln < eob; )
	{			/* count sections/settings */
	skipwhite:
		ln += strspn(ln, whitespace);
		if (ln < eob && *ln == '\0')	/* embedded nul found */
		{
			*(ln++) = ' ';		/* change to space and skip */
			goto skipwhite;
		}
		switch (*ln)
		{
		case ';':
		case '#':
		case '\0':
			break;
		case '[':
			nasections++;
			break;
		default:
			nasettings++;
			break;
		}
	skiptoeol:
		ln += strcspn(ln, EOLSPACE);
		if (ln < eob && *ln == '\0')	/* embedded nul found */
		{
			*(ln++) = ' ';		/* change to space and skip */
			goto skiptoeol;
		}
	}
	if (nasections)		/* don't alloc 0 bytes  KNG 000524 */
	{
		rc->sections = (SECTION**)TXcalloc(pmbuf, __FUNCTION__,
						nasections, sizeof(SECTION*));
		if (rc->sections == SECTIONPPN)
			goto err;
	}
	if (nasettings)		/* don't alloc 0 bytes  KNG 000524 */
	{
		rc->settings =
			(SETTING *)TXcalloc(pmbuf, __FUNCTION__,
					    nasettings, sizeof(SETTING));
		if (rc->settings == SETTINGPN)
			goto err;
	}

	nsections = nsettings = 0;
	for (ln = buf; ln < eob; ln = nl)
	{
		ln += strspn(ln, whitespace);
		nl = ln + strcspn(ln, EOLSPACE);
		*(nl++) = '\0';	/* terminate this line */
		/* We no longer allow trailing comments, because semi-colon
		 * is the path separator in Windows and thus might be a
		 * valid part of a value.  KNG 020826
		 */
		switch (*ln)
		{
		case ';':
		case '#':
		case '\0':
			break;	/* blank line/comment */
		case '[':			/* new section starting */
			if (nsections >= nasections)
			{			/* should never happen */
				if (!saidTooManySections)
				{
					txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
     "%s: Miscalculated number of sections; extra ignored at `%s' offset %wd",
					       filename, ln,
					       (EPI_HUGEINT)(ln - buf));
					saidTooManySections = 1;
				}
				break;
			}
			section = (SECTION *)TXcalloc(pmbuf, __FUNCTION__,
						      (sizeof(SECTION) -
				sizeof(section->name)) + strlen(ln + 1)+1, 1);
			if (section == SECTIONPN) goto err;
			rc->sections[nsections++] = section;
			strcpy(section->name, ln + 1);
			if ((t = strchr(section->name, ']')) != CHARPN)
				*t = '\0';	/* remove trailing `]' */
			break;
		default:
			for (t = ln + strlen(ln) - 1;
			     t >= ln && strchr(whitespace, *t) != CHARPN; t--)
				*t = '\0';	/* zap trailing space */
			t = strchr(ln, '=');
			if (!t)			/* no value */
			{
				txpmbuf_putmsg(pmbuf, MWARN + UGE, __FUNCTION__,
		      "%s: Setting `%s' missing value at offset %wd; ignored",
					       filename, ln,
					       (EPI_HUGEINT)(ln - buf));
				break;
			}
			if (nsections <= 0)
			{
				txpmbuf_putmsg(pmbuf, MWARN + UGE, CHARPN,
 "%s: Configuration setting `%s' is not in a section, at offset %wd; ignored",
					       filename, ln,
					       (EPI_HUGEINT)(ln - buf));
				break;
			}
			if (nsettings >= nasettings)
			{			/* should never happen */
				if (!saidTooManySettings)
				{
					txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
     "%s: Miscalculated number of settings; extra ignored at `%s' offset %wd",
					       filename, ln,
					       (EPI_HUGEINT)(ln - buf));
					saidTooManySettings = 1;
				}
				break;
			}
			s = &rc->settings[nsettings++];
			s->section = rc->sections[nsections - 1];
			s->doVarExpansion = 1;	/* unless `:=' used */
			/* Bug 5034: if `:=', no var substitution: */
			if (t > ln && t[-1] == ':')
			{
				s->doVarExpansion = 0;
				t[-1] = '\0';
			}
			*(t++) = '\0';	/* nul-terminate name */
			t += strspn(t, whitespace);
			for (e = ln + strlen(ln) - 1;
			     e >= ln && strchr(whitespace, *e) != CHARPN;
			     e--)
				*e = '\0';	/* zap trail space */
			/* We must delay %-var expansion until later
			 * (late binding), since %SCRIPTROOT% etc.
			 * depend on other settings that we may not
			 * know yet:
			 */
			if (!(s->rawValue = TXstrdup(pmbuf, __FUNCTION__, t)))
				goto err;
			s->expandedValue = CHARPN;	/* expand later */
			if (!(s->name = TXstrdup(pmbuf, __FUNCTION__,ln)))
				goto err;
			/* Expand and save value if [Texis] Script Root,
			 * for later %SCRIPTROOT% replacement in
			 * other settings:
			 */
			if ((*s->section->name == 'T' ||
			     *s->section->name == 't') &&
			    (*s->name == 'S' || *s->name == 's') &&
			    TXstrnispacecmp(s->section->name, (size_t)(-1),
					    "Texis", 5, CHARPN) == 0 &&
			    TXstrnispacecmp(s->name, (size_t)(-1),
					    "Script Root", 11, CHARPN) == 0)
			{
				rc->scriptRootValue =
					TXfree(rc->scriptRootValue);
				rc->scriptRootValue =
					TXstrdup(pmbuf, __FUNCTION__,
                                                 s->rawValue);
				if (rc->scriptRootValue == CHARPN)
					goto err;
				rc->scriptRootValueIsExpanded = 0;
			}
			/* Same for [Httpd] Document Root: */
			if ((*s->section->name == 'H' ||
			     *s->section->name == 'h') &&
			    (*s->name == 'D' || *s->name == 'd') &&
			    TXstrnispacecmp(s->section->name, (size_t)(-1),
					    "Httpd", 5, CHARPN) == 0 &&
			    TXstrnispacecmp(s->name, (size_t)(-1),
					    "Document Root", 13, CHARPN) == 0)
			{
				rc->documentRootValue =
					TXfree(rc->documentRootValue);
				rc->documentRootValue =
					TXstrdup(pmbuf, __FUNCTION__,
                                                 s->rawValue);
				if (rc->documentRootValue == CHARPN)
					goto err;
				rc->documentRootValueIsExpanded = 0;
			}
#ifdef EPI_ENABLE_LOGDIR_RUNDIR
			/* And [Texis] Log Dir and [Texis] Run Dir: */
			if (TX_TOUPPER(*s->section->name) == 'T' &&
			    (TX_TOUPPER(*s->name) == 'L' ||
                             TX_TOUPPER(*s->name) == 'R') &&
			    TXstrnispacecmp(s->section->name, (size_t)(-1),
					    "Texis", 5, NULL) == 0)
			{
                          if (TXstrnispacecmp(s->name, (size_t)(-1),
                                              "Log Dir", 7, NULL) == 0)
                            {
                              rc->logDirValue = TXfree(rc->logDirValue);
                              rc->logDirValue = TXstrdup(pmbuf, __FUNCTION__,
                                                         s->rawValue);
                              if (!rc->logDirValue) goto err;
                              rc->logDirValueIsExpanded = TXbool_False;
                            }
                          if (TXstrnispacecmp(s->name, (size_t)(-1),
                                              "Run Dir", 7, NULL) == 0)
                            {
                              rc->runDirValue = TXfree(rc->runDirValue);
                              rc->runDirValue = TXstrdup(pmbuf, __FUNCTION__,
                                                         s->rawValue);
                              if (!rc->runDirValue) goto err;
                              rc->runDirValueIsExpanded = TXbool_False;
                            }
                        }
#endif /* EPI_ENABLE_LOGDIR_RUNDIR */
			break;
		}
	}
	rc->nsettings = nsettings;
	rc->nsections = nsections;

	/* If we saw no [Texis] Script Root value, get the default so that
	 * %SCRIPTROOT% expands properly in other settings:
	 */
	if (rc->scriptRootValue == CHARPN)
	{
		rc->scriptRootValue = t =
			(char *)TXmalloc(pmbuf, __FUNCTION__,
					 len = strlen(TXINSTALLPATH_VAL) + 15);
		if (!t) goto err;
		strcpy(t, TXINSTALLPATH_VAL);
		strcat(t, PATH_SEP_S "texis" PATH_SEP_S "scripts");
		rc->scriptRootValueIsExpanded = 1;
	}
	/* Same for [Httpd] Document Root: */
	if (rc->documentRootValue == CHARPN)
	{
		rc->documentRootValue = t =
			(char *)TXmalloc(pmbuf, __FUNCTION__,
					 len = strlen(TXINSTALLPATH_VAL) + 8);
		if (!t) goto err;
		strcpy(t, TXINSTALLPATH_VAL);
		strcat(t, PATH_SEP_S "htdocs");
		rc->documentRootValueIsExpanded = 1;
	}
#ifdef EPI_ENABLE_LOGDIR_RUNDIR
        /* And [Texis] Log Dir and [Texis] Run Dir: */
	if (!rc->logDirValue)
	{
		/* note: no CONFFILE passed in; no circular dependency: */
		rc->logDirValue = TXconfGetLogDirFile(pmbuf, NULL, NULL, NULL,
						      NULL);
		if (!rc->logDirValue) goto err;
		rc->documentRootValueIsExpanded = 1;
	}
	if (!rc->runDirValue)
	{
		/* note: no CONFFILE passed in; no circular dependency: */
		rc->runDirValue = TXconfGetRunDirFile(pmbuf, NULL, NULL, NULL,
						      NULL);
		if (!rc->runDirValue) goto err;
		rc->documentRootValueIsExpanded = 1;
	}
#endif /* EPI_ENABLE_LOGDIR_RUNDIR */

	goto done;
      err:
	rc = closeconffile(rc);
      done:
	if (fh != -1)
		close(fh);
	if (buf && buf != tmp)
		buf = TXfree(buf);
	return rc;
}

/******************************************************************/

char *
TXgetcharsetconv()
{
	static char *charsetconv = NULL;
	static int checkedconffile = 0;

	if(!checkedconffile)
	{
		charsetconv = getconfstring(TxConf, "Texis", "Charset Converter", CHARPN);
		checkedconffile++;
	}
	return charsetconv;
}

char **
TXgetConfStrings(TXPMBUF *pmbuf, CONFFILE *conffile, const char *sectionName,
                 int sectionNum, const char *attrib, char *defval)
/* Looks up all values for `attrib' in `sectionName' (if non-NULL) or
 * `sectionNum'.  If `attrib' is NULL, returns list of setting names instead.
 * Returns an alloced list of zero or more values, including duplicate
 * settings in possibly duplicate sections.
 */
{
  size_t        i, numUsed = 0, numAlloced = 0;
  SETTING       *setting;
  char          **ret = NULL, *val;

  if (conffile)
    for (i = 0; i < conffile->nsettings; i++)
      {
        setting = &conffile->settings[i];
        if ((sectionName ? TXstrnispacecmp(setting->section->name,
                                           (size_t)(-1), sectionName,
                                           (size_t)(-1), NULL) == 0 :
             (sectionNum >= 0 && (size_t)sectionNum < conffile->nsections &&
              setting->section == conffile->sections[sectionNum])) &&
            (attrib ? TXstrnispacecmp(setting->name, (size_t)(-1),
                                      attrib, (size_t)(-1), CHARPN) == 0 :
             TXbool_True))
          {
            if (!attrib)
              val = setting->name;
            /* Bug 5034: no var subst. if `:=': */
            else if (!setting->doVarExpansion)
              val = setting->rawValue;
            else
              {
                /* Free `expandedValue' if invalid: */
                if (setting->expandedSerial != conffile->curExpandedSerialNum)
                  setting->expandedValue = TXfree(setting->expandedValue);
                /* Expand if not already expanded: */
                if (setting->expandedValue == CHARPN)
                  setting->expandedValue =
                    TXconfExpandRawValue(pmbuf, conffile, setting->rawValue);
                val = setting->expandedValue;
              }
            if (!val) goto err;
            if (!TX_INC_ARRAY(pmbuf, &ret, numUsed, &numAlloced))
              goto err;
            if (!(ret[numUsed] = TXstrdup(pmbuf, __FUNCTION__, val)))
                  goto err;
            numUsed++;
          }
      }
  if (numUsed == 0 && defval)
    {
      if (!TX_INC_ARRAY(pmbuf, &ret, numUsed, &numAlloced))
        goto err;
      if (!(ret[numUsed] = TXstrdup(pmbuf, __FUNCTION__, defval)))
        goto err;
      numUsed++;
    } 
  if (!TX_INC_ARRAY(pmbuf, &ret, numUsed, &numAlloced))
    goto err;
  ret[numUsed] = NULL;
  goto finally;

err:
  ret = TXfreeStrList(ret, numUsed);
  numUsed = numAlloced = 0;
finally:
  return(ret);
}

char *
getconfstring(conffile, section, attrib, defval)
CONFFILE *conffile;
const char *section;
const char *attrib;
char *defval;
/*
 * NOTE: should use a TXPMBUF arg for putmsgs.
 */
{
	size_t i;
	SETTING	*setting;

	if (conffile)
		for (i = 0; i < conffile->nsettings; i++)
		{
			setting = &conffile->settings[i];
			if (TXstrnispacecmp
			    (setting->section->name, (size_t)(-1),
			     section, (size_t)(-1), CHARPN) == 0
			    && TXstrnispacecmp(setting->name, (size_t)(-1),
					   attrib, (size_t)(-1), CHARPN) == 0)
			{
				/* Bug 5034: no var subst. if `:=': */
				if (!setting->doVarExpansion)
					return(setting->rawValue);
				/* Free `expandedValue' if invalid: */
				if (setting->expandedSerial != conffile->curExpandedSerialNum)
					setting->expandedValue = TXfree(setting->expandedValue);
				/* Expand if not already expanded: */
				if (setting->expandedValue == CHARPN)
					setting->expandedValue =
					    TXconfExpandRawValue(TXPMBUFPN,
						conffile,
						setting->rawValue);
				return(setting->expandedValue ?
					setting->expandedValue : defval);
			}
		}
	return defval;
}

char *
TXconfGetRawString(conffile, section, attrib, defval)
CONFFILE *conffile;
const char *section;
const char *attrib;
char *defval;
/* Returns raw value (%-vars not expanded) for [section] attrib.
 */
{
	size_t i;
	SETTING	*setting;

	if (conffile)
		for (i = 0; i < conffile->nsettings; i++)
		{
			setting = &conffile->settings[i];
			if (TXstrnispacecmp
			    (setting->section->name, (size_t)(-1),
			     section, (size_t)(-1), CHARPN) == 0
			    && TXstrnispacecmp(setting->name, (size_t)(-1),
					attrib, (size_t)(-1), CHARPN) == 0)
				return(setting->rawValue);
		}
	return defval;
}

/******************************************************************/

int
getconfint(conffile, section, attrib, defval)
CONFFILE *conffile;
const char *section;
const char *attrib;
int defval;
{
	size_t i;
	SETTING	*setting;

	if (conffile)
		for (i = 0; i < conffile->nsettings; i++)
		{
			setting = &conffile->settings[i];
			if (TXstrnispacecmp
			    (setting->section->name, (size_t)(-1),
			     section, (size_t)(-1), CHARPN) == 0
			    && TXstrnispacecmp(setting->name, (size_t)(-1),
					attrib, (size_t)(-1), CHARPN) == 0)
			{
				/* Free `expandedValue' if invalid: */
				if (setting->expandedSerial != conffile->curExpandedSerialNum)
					setting->expandedValue = TXfree(setting->expandedValue);
				/* Expand if not already expanded: */
				if (setting->expandedValue == CHARPN)
					setting->expandedValue =
					    TXconfExpandRawValue(TXPMBUFPN,
						conffile,
						setting->rawValue);
				if (setting->expandedValue != CHARPN)
					return((int)strtol(setting->expandedValue, NULL, 0));
				else
					return(defval);
			}
		}
	return defval;
}

/******************************************************************/

char *
getnextconfstring(conffile, section, attrib, i)
CONFFILE *conffile;
const char *section;
const char **attrib;
int	i;
/* Gets `i'th attribute/value pair from `section', setting `*attrib' to the
 * attribute name and returning the value.  Returns NULL if no such pair.
 * NOTE: should use a TXPMBUF parameter for putmsgs.
 */
{
	size_t j;
	SETTING	*setting;

	if (conffile)
		for (j = 0; j < conffile->nsettings; j++)
		{
			setting = &conffile->settings[j];
			if (TXstrnispacecmp(setting->section->name,
			  (size_t)(-1), section, (size_t)(-1), CHARPN) == 0 &&
			    i-- == 0)
			{
				*attrib = setting->name;
				/* Free `expandedValue' if invalid: */
				if (setting->expandedSerial != conffile->curExpandedSerialNum)
					setting->expandedValue = TXfree(setting->expandedValue);
				/* Expand if not already expanded: */
				if (setting->expandedValue == CHARPN)
					setting->expandedValue =
					    TXconfExpandRawValue(TXPMBUFPN,
						conffile,
						setting->rawValue);
				return(setting->expandedValue);
			}
		}
	*attrib = CHARPN;
	return(CHARPN);
}

size_t
TXconfGetNumSections(conf)
CONFFILE        *conf;  /* (in) config file */
/* Returns number of sections in `conf', including duplicate-name sections.
 */
{
  return(conf->nsections);
}

const char *
TXconfGetSectionName(conf, sectionIdx)
CONFFILE        *conf;          /* (in) config file */
size_t          sectionIdx;     /* (in) index of section to return */
/* Returns name of section `i', or NULL if no more sections.
 */
{
  return((TX_SIZE_T_VALUE_LESS_THAN_ZERO(sectionIdx) ||
          sectionIdx >= conf->nsections) ? CHARPN :
         conf->sections[sectionIdx]->name);
}
