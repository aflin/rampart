/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef EPI_HAVE_MMAP
#  include <sys/mman.h>
#endif
#ifdef EPI_HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif
#include "sizes.h"
#include "os.h"
#include "dbquery.h"
#include "texint.h"
#include "keyrec.h"
#include "fldcmp.h"
#include "texint.h"
#include "rppm.h"
#include "heap.h"
#include "fdbi.h"
#include "cgi.h"
#include "fbtree.h"


#ifndef DDMMAPIPN
#  define DDMMAPIPN     ((DDMMAPI *)NULL)
#endif
#ifndef BTLOCPN
#  define BTLOCPN       ((BTLOC *)NULL)
#endif
#ifndef FLDCMPPN
#  define FLDCMPPN      ((FLDCMP *)NULL)
#endif


/* KNG 000127   TxUniqNewList: unique the new list to work around bug.
 *    0:  don't unique
 *    1:  unique aux/compound index new list only
 *    2:  unique all new lists
 *    3:  unique all new lists and report first few duplicates
 */
int TxUniqNewList = 0;		/* see also setprop.c */

/* JMT - 960927 changed 4096 to 99.  No point in indexing huge string
   of characters.
 */
static char *expressions[MAX_INDEX_EXPS] = { "\\alnum{2,99}", "" };
static int freeexpr[MAX_INDEX_EXPS] = { 0, 0, 0 };

static char *IndexTmp[MAX_INDEX_EXPS] = { "" };
static int IndexTmpFree[MAX_INDEX_EXPS] = { 0 };

#ifdef SMALL_MEM
static size_t blockmax = 32000;
#else
static size_t blockmax = 1000000;
#endif
static CONST char lts[] = LASTTOKEN;

#ifdef SMALL_MEM
#define BTCSIZE 5
#else
#define BTCSIZE 20
#endif

static CONST char	textSearchModeStr[TX_TEXTSEARCHMODESTR_SZ] =
	"textsearchmode";

/******************************************************************/

size_t TXsetblockmax(bm)
size_t
	bm;
{
	size_t obm;

	obm = blockmax;
	blockmax = bm;
	return obm;
}

/******************************************************************/

size_t TXgetblockmax()
{
	return blockmax;
}

/******************************************************************/

char **
TXgetglobalexp()
{
	return expressions;
}

/******************************************************************/

char **
TXgetglobalindextmp()
{
	return (IndexTmp);
}

/******************************************************************/

static void exp_freelist ARGS((char **expr, int *fr));
static void
exp_freelist(expr, fr)
char **expr;
int *fr;
{
	int i;

	for (i = 0; i < MAX_INDEX_EXPS; i++)
	{
		if (fr[i])
		{
			expr[i] = TXfree(expr[i]);
			fr[i] = 0;
		}
	}
}

/******************************************************************/

int
TXresetexpressions()
{
	exp_freelist(expressions, freeexpr);
	expressions[0] = "\\alnum{2,99}";
	expressions[1] = "";
	freeexpr[0] = 0;
	freeexpr[1] = 0;
	return (0);
}

void
TXsaveindexexp(expr, frexpr)
char **expr;
int *frexpr;

/* Saves current index expression list to `expr', `frexpr' and clears it.
 */
{
	memcpy(expr, expressions, MAX_INDEX_EXPS * sizeof(expr[0]));
	memcpy(frexpr, freeexpr, MAX_INDEX_EXPS * sizeof(frexpr[0]));
	expressions[0] = "";
	memset(freeexpr, 0, MAX_INDEX_EXPS * sizeof(freeexpr[0]));
}

void
TXrestoreindexexp(expr, frexpr)
char **expr;
int *frexpr;

/* Clears current index expression list and restores set from `expr', `frexpr'.
 */
{
	TXresetexpressions();
	memcpy(expressions, expr, MAX_INDEX_EXPS * sizeof(expr[0]));
	memcpy(freeexpr, frexpr, MAX_INDEX_EXPS * sizeof(frexpr[0]));
}

/******************************************************************/

int
TXresetindextmp()
{
	exp_freelist(IndexTmp, IndexTmpFree);
	return (0);
}

/******************************************************************/

static void exp_list ARGS((char **expr));
static void
exp_list(expr)
char **expr;
{
	int i;

	for (i = 0; expr[i] != CHARPN && *expr[i] != '\0'; i++)
		putmsg(MINFO, CHARPN, "%2d: %s", i, expr[i]);
}

/******************************************************************/

int
_listexp(x)
char *x;
{
	(void)x;
	exp_list(expressions);
	return (0);
}

/******************************************************************/

int
TXlistindextmp(x)
char *x;
{
	(void)x;
	exp_list(IndexTmp);
	return (0);
}

/******************************************************************/

static int exp_add ARGS((char **expr, int *fr, char *value));
static int
exp_add(expr, fr, value)
char **expr;
int *fr;
char *value;
{
	static CONST char fn[] = "exp_add";
	int i;

	for (i = 0; i < MAX_INDEX_EXPS - 1; i++)
	{
		if (expr[i] == CHARPN || expr[i][0] == '\0')
		{
			if (fr[i])
				expr[i] = TXfree(expr[i]);
			if (!(expr[i] = TXstrdup(TXPMBUFPN, fn, value)))
			{
				fr[i] = 0;
				return (-1);
			}
			fr[i] = 1;
			expr[i + 1] = "";
			fr[i + 1] = 0;
			return 0;
		}
	}
	putmsg(MWARN + MAE, fn, "Too many expressions");
	return (-1);
}

/******************************************************************/

int
_addexp(value)
char *value;
{
	return (exp_add(expressions, freeexpr, value));
}

/******************************************************************/

int
TXaddindextmp(value)
char *value;
{
	return (exp_add(IndexTmp, IndexTmpFree, value));
}

/******************************************************************/

static int exp_del ARGS((char **expr, int *fr, char *value));
static int
exp_del(expr, fr, value)
char **expr;
int *fr;
char *value;
{
	char *p;
	int i, j;
	int numeric = 0;

	for (p = value; *p != '\0'; p++)
		if (!isdigit((int)*(unsigned char *)p))
		{
			numeric = 0;
			break;
		}
		else
			numeric = 1;

	if (numeric)
		i = atoi(value);
	else
	{
		for (i = 0; expr[i] != CHARPN && *expr[i] != '\0'; i++)
			if (!strcmp(expr[i], value))
				break;
	}
	if (i < 0 || i >= MAX_INDEX_EXPS ||
	    expr[i] == CHARPN || *expr[i] == '\0')
	{
		putmsg(MWARN, CHARPN, "Expression `%s' not found", value);
		return (-1);
	}
	if (fr[i])
		expr[i] = TXfree(expr[i]);
	if (i < (MAX_INDEX_EXPS - 1))
	{
		expr[i] = expr[i + 1];
		fr[i] = fr[i + 1];
	}
	for (j = i; expr[j] != CHARPN && *expr[j] != '\0'; j++)
	{
		expr[j] = expr[j + 1];
		fr[j] = fr[j + 1];
	}
	return (0);
}

/******************************************************************/

int
_delexp(value)
char *value;
{
	return (exp_del(expressions, freeexpr, value));
}

/******************************************************************/

int
TXdelindextmp(value)
char *value;
{
	return (exp_del(IndexTmp, IndexTmpFree, value));
}

/******************************************************************/

void
TXflush3dbi(dbi)
A3DBI *dbi;
{
	if (dbi != (A3DBI *) NULL)
	{
		if (dbi->ttbl != (TTBL *) NULL)
		{
			if (dbi->ttbl->bt != (BTREE *) NULL)
			{
				btflush(dbi->ttbl->bt);
			}
		}
		if (dbi->mm != (MMTBL *) NULL)
			btflush(dbi->mm->bt);
		if (dbi->td != (BTREE *) NULL)
			btflush(dbi->td);
		if (dbi->del != (BTREE *) NULL)
			btflush(dbi->del);
		if (dbi->upd != (BTREE *) NULL)
			btflush(dbi->upd);
		if (dbi->newrec != (BTREE *) NULL)
			btflush(dbi->newrec);
		if (dbi->mdel != (BTREE *) NULL)
			btflush(dbi->mdel);
		if (dbi->mupd != (BTREE *) NULL)
			btflush(dbi->mupd);
		if (dbi->mnew != (BTREE *) NULL)
			btflush(dbi->mnew);
	}
}

/******************************************************************/

A3DBI *
close3dbi(dbi)
A3DBI *dbi;
{
	BTLOC at, loc;
	char buf[PATH_MAX], buf2[PATH_MAX];

	if (dbi != (A3DBI *) NULL)
	{
		if (dbi->ttbl != (TTBL *) NULL)
		{
			if (dbi->ttbl->bt != (BTREE *) NULL)
			{
				TXsetrecid(&at, (EPI_OFF_T) dbi->lasttoken);
				loc =
					btsearch(dbi->ttbl->bt, strlen(lts),
						 (char *)lts);
				if (TXrecidvalid(&loc))
					btupdate(dbi->ttbl->bt, at);
				else
					btinsert(dbi->ttbl->bt, &at,
						 strlen(lts), (char *)lts);
			}
			closettbl(dbi->ttbl);
		}
		if (dbi->mm != (MMTBL *) NULL)
			closemmtbl(dbi->mm);
		if (dbi->td != (BTREE *) NULL)
			closebtree(dbi->td);
		if (dbi->ct != (BTREE *) NULL)
		{
			/* Calling with NULL `query' and `ix' closes
			 * static MMAPI and APICP:
			 */
			TXinsertMetamorphCounterIndexRow(NULL, NULL, at, NULL);
			closebtree(dbi->ct);
		}
		if (dbi->del != (BTREE *) NULL)
			closebtree(dbi->del);
		if (dbi->upd != (BTREE *) NULL)
			closebtree(dbi->upd);
		if (dbi->newrec != (BTREE *) NULL)
		{
			if (dbi->auxa2i != NULL && dbi->newrec->usr != NULL)
				dbi->newrec->usr =
					TXclosefldcmp(dbi->newrec->usr);
			dbi->newrec = closebtree(dbi->newrec);
		}
		if (dbi->mdel != (BTREE *) NULL)
			closebtree(dbi->mdel);
		if (dbi->mupd != (BTREE *) NULL)
			closebtree(dbi->mupd);
		if (dbi->mnew != (BTREE *) NULL)
			closebtree(dbi->mnew);
		if (dbi->explist != (char **) NULL)
		{
			dbi->explist = _freelst(dbi->explist);
		}
		dbi->locale = TXfree(dbi->locale);
		if (dbi->noiselist != (char **) NULL)
		{
			dbi->noiselist = _freelst(dbi->noiselist);
		}
                /* KNG 011218 these are only INDEX_3DB files; skip if !mm: */
		if (!dbi->creating && dbi->name && dbi->mm)
		{
			TXcatpath(buf, dbi->name, "_M");
			TXcatpath(buf2, buf, MDDATSUF);
			unlink(buf2);
			TXcatpath(buf, dbi->name, "_M");
			TXcatpath(buf2, buf, MDINDEXSUF);
			unlink(buf2);
		}
		dbi->name = TXfree(dbi->name);
		if (dbi->auxbi.table != TBLPN)
			closetbl(dbi->auxbi.table);
		if (dbi->auxa2i != NULL)
			TXadd2indcleanup((TXA2IND *) dbi->auxa2i);
		if (dbi->mauxbi.table != TBLPN)
			closetbl(dbi->mauxbi.table);
		if (dbi->mauxa2i != NULL)
			TXadd2indcleanup((TXA2IND *) dbi->mauxa2i);
		if (dbi->auxdd)
			dbi->auxdd = closedd(dbi->auxdd);
		dbi = TXfree(dbi);
	}
	return (A3DBI *) NULL;
}

/******************************************************************/

char **
TXfstrlsttoc(fld, emptyStripAndTerminate)
FLD	*fld;			/* (in) strlst FLD */
int	emptyStripAndTerminate;	/* (in, opt.) non-zero: empty-str term. also*/
/* Returns an alloced, NULL-terminated array for strlst `fld'.
 * If `emptyStripAndTerminate', array is alloced-empty-string terminated also
 * (before the NULL), and empty-string elements are skipped (to prevent
 * false early termination).  In all cases, list can be freed with freenlst().
 */
{
	static CONST char	fn[] = "TXfstrlsttoc";
	char *p, *e;
	char **rc, *buf, *bufEnd;
	size_t	n, i, nn;
	ft_strlst	sl;

	buf = TXgetStrlst(fld, &sl);
	bufEnd = buf + sl.nb;
	if (bufEnd > buf && !bufEnd[-1]) bufEnd--;	/* ign strlst-term. */

	/* Count the number of payload strings into `nn': */
	nn = 0;
	for (p = buf; p < bufEnd; p++)
		if (!*p)
			nn++;
	if (p > buf && p[-1]) nn++;		/* last item unterminated */

	if (!(rc = (char **) TXcalloc(TXPMBUFPN, fn, nn +
			      !!emptyStripAndTerminate + 1, sizeof(char *))))
		goto nomem;			/* +1 for NULL terminator */

	for (i = 0, p = buf; i < nn && p < bufEnd; i++)
	{
		if (emptyStripAndTerminate)
		{
			/* Skip empty payload strings, which would falsely
			 * be taken as end of array.  Note that this loses
			 * valid payload values:
			 */
			while (p < bufEnd && *p == '\0') p++;
			if (p >= bufEnd) break;
		}
		for (e = p; e < bufEnd && *e != '\0'; e++);
		n = e - p;			/* length of current item */
		if ((rc[i] = (char *)TXmalloc(TXPMBUFPN, fn, n + 1)) == CHARPN)
			goto nomem;
		memcpy(rc[i], p, n);
		rc[i][n] = '\0';
		p = e + 1;
	}
	if (emptyStripAndTerminate)
	{
		if ((rc[i] = TXstrdup(TXPMBUFPN, fn, "")) == CHARPN)
		{
		nomem:
			return(freenlst(rc));
		}
	}
	return rc;
}

/******************************************************************/

ft_strlst *_ctofstrlst ARGS((char **, int *));

ft_strlst *
_ctofstrlst(strlst, sz)
char **strlst;
int *sz;
/* Thread-safe. */
{
	static CONST char	fn[] = "_ctofstrlst";
	ft_strlst *rc;
	char **p;
	char *t, *buf;
	int needed;
	int c;
	char ul[256];

	needed = 1;			/* for empty string terminator */
	memset(ul, 0, sizeof(ul));
	for (p = strlst; *p && *p[0]; p++)
	{
		for (t = *p; *t; t++)
		{
			needed++;
			ul[(int)*(unsigned char *)t] = 1;
		}
		needed++;		/* for nul terminator */
	}
	*sz = needed + sizeof(ft_strlst);
	rc = (ft_strlst *) TXcalloc(TXPMBUFPN, fn, 1, *sz);
	if (rc == (ft_strlst *)NULL)
		return((ft_strlst *)NULL);
	buf = rc->buf;
	t = buf;
	for (p = strlst; *p && *p[0]; p++)
	{
		strcpy(t, *p);
		t += strlen(*p) + 1;
	}
	*t = '\0';			/* empty string terminator */
	rc->nb = needed;
	/* Emulate TXVSSEP_CREATE, but across all items: */
	for (c = 0; c < DYNABYTE; c++)
		if (!ul[(byte)TxPrefStrlstDelims[c]])
		{
			rc->delim = TxPrefStrlstDelims[c];
			break;
		}
	if (!rc->delim)
		putmsg(MWARN, fn,
		       "No unused character could be found: using nul");
	return rc;
}

/******************************************************************/

static void TXinit3dbiParams ARGS((A3DBI *dbi));
static void
TXinit3dbiParams(dbi)
A3DBI	*dbi;
/* Sets SYSINDEX.PARAMS values in `dbi' to defaults.
 */
{
	/* If textsearchmode is not found in an *existing* index's
	 * SYSINDEX.PARAMS, it is old-style (tolower(), ISO-8859-1):
	 */
	dbi->textsearchmode = TXCFF_TEXTSEARCHMODE_DEFAULT_OLD;
}

A3DBI *TX3dbiOpen(type)
int     type;
/* Creates an A3DBI object, for either TXcreate3dbiForIndexUpdate() or
 * open3dbi().  Note that this is init'd old-style, as if _P.tbl were missing.
 */
{
  static CONST char     fn[] = "TX3dbiOpen";
  A3DBI                 *dbi;

  dbi = (A3DBI *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(A3DBI));
  if (dbi == A3DBIPN)
      return(A3DBIPN);
  dbi->type = type;
  dbi->version = 1;                             /* default if not in _P.tbl */
  TXsetrecid(&dbi->delloc, (EPI_OFF_T)(-1));
  TX_INIT_TXMMPARAMTBLINFO(&dbi->paramTblInfo);
  TXinit3dbiParams(dbi);
  /* rest cleared by calloc() */
  return(dbi);
}

A3DBI *
TXcreate3dbiForIndexUpdate(name, srcDbi, flags)
char *name;
A3DBI	*srcDbi;	/* (in) A3DBI to duplicate type, index expr. etc. */
int	flags;		/* (in) bit flags:  0x2: rebuild */
/* Creates _P/[_C] files for metamorph index update, copying values from
 * `srcDbi'.  _D/_T files not created: original index's _X/_Z files
 * (created by caller) will become _D/_T files.
 * Atomic cleanup on error: removes all and only files that were created.
 */
{
	static CONST char Fn[] = "TXcreate3dbiForIndexUpdate";
	A3DBI *dbi;
	char buf[PATH_MAX];

	dbi = TX3dbiOpen(srcDbi->type);
	if (dbi == A3DBIPN) return(A3DBIPN);

	TXcatpath(buf, name, "_P");

	dbi->explist = _duplst(srcDbi->explist);
	dbi->locale = TXstrdup(TXPMBUFPN, Fn, srcDbi->locale);
	dbi->textsearchmode = srcDbi->textsearchmode;
	dbi->noiselist = _duplst(srcDbi->noiselist);
	dbi->type = srcDbi->type;
	dbi->auxsz = srcDbi->auxsz;
	if (srcDbi->auxdd)
		dbi->auxdd = TXdupDd(srcDbi->auxdd);
	dbi->version = srcDbi->version;

	/* For index update (optimize), we re-use the existing _P.tbl.
	 * Only rebuild (flags & 0x2) makes a new _P.tbl:
	 */
	if ((flags & 0x2) &&
	    !TXcreateAndWriteDbiParamsTable(buf, dbi))
		goto errbail;

	if (srcDbi->ct)				/* _C.btr only for counter */
	{
		TXcatpath(buf, name, "_C");
		dbi->ct = openbtree(buf, BTFSIZE, BTCSIZE, BT_UNIQUE | BT_FIXED, O_RDWR | O_CREAT | O_EXCL);
		if (!dbi->ct) goto errbail;
	}

	dbi->name = TXstrdup(TXPMBUFPN, Fn, name);
	if (!dbi->name)
	{
	errbail:
		/* Delete what we created, but not what may have existed,
		 * so caller knows creation is atomic regardless of error
		 * (either all files created on success or none on error):
		 */
		if (dbi->upd != BTREEPN)
		{
			dbi->upd = closebtree(dbi->upd);
			TXcatpath(buf, name, "_U.btr");
			tx_delindexfile(MERR, Fn, buf, 0);
		}
		if (dbi->ct != BTREEPN)
		{
			dbi->ct = closebtree(dbi->ct);
			TXcatpath(buf, name, "_C.btr");
			tx_delindexfile(MERR, Fn, buf, 0);
		}
		if (dbi->td != BTREEPN)
		{
			dbi->td = closebtree(dbi->td);
			TXcatpath(buf, name, "_B.btr");
			tx_delindexfile(MERR, Fn, buf, 0);
		}
		/* _P.tbl creation/deletion handled by
		 * TXcreateAndWriteDbiParamsTable()
		 */
		return close3dbi(dbi);
	}
	dbi->creating = 0;
	return dbi;
}

/******************************************************************/

int
_openupd3dbi(dbi)
A3DBI *dbi;
{
	char *name;
	char buf[PATH_MAX];

	switch (dbi->type)	/* wtf not used for INDEX_FULL KNG */
	{
	case INDEX_MM:
	case INDEX_MMCR:
	case INDEX_FULL:
	case INDEX_FULLCR:
		putmsg(MERR + UGE, "_openupd3dbi", "Bad function call");
		return (-1);
	}
	if (dbi->ttbl != NULL)
	{
#ifdef OLD_MM_INDEX
		if (dbi->ttbl->ti != NULL)	/* WTF why?  KNG 980520 */
			return 0;
#endif
		dbi->ttbl = closettbl(dbi->ttbl);
	}
	name = dbi->name;
	TXcatpath(buf, name, "_M");
	dbi->ttbl = openttbl(buf, dbi->explist);
	if (dbi->ttbl == (TTBL *) NULL)
	{
		putmsg(MWARN + FOE, NULL,
		       "Unable to open index %s for update: %s", buf,
		       strerror(errno));
		return -1;
	}
	return 0;
}

/******************************************************************/

static int TXreadDbiParamsFromTable ARGS((A3DBI *, TBL *));

static int
TXreadDbiParamsFromTable(dbi, ptbl)
A3DBI *dbi;
TBL *ptbl;
{
	static CONST char	fn[] = "TXreadDbiParamsFromTable";
	FLD *pfld, *vfld;
	ft_strlst *fsl;
	size_t fslsz;
	char *pname;
	RECID *where;

	pfld = nametofld(ptbl, "explist");
	if (pfld)				/* old-style _P.tbl */
	{
		gettblrow(ptbl, NULL);
		dbi->explist = TXfstrlsttoc(pfld, 1);
		dbi->noiselist = NULL;
                dbi->locale = CHARPN;
		return 0;
	}
	pfld = nametofld(ptbl, "Param");
	vfld = nametofld(ptbl, "Value");
	if (pfld && vfld)
	{
		for (where = gettblrow(ptbl, NULL); TXrecidvalid(where);
		     where = gettblrow(ptbl, NULL))
		{
			pname = getfld(pfld, NULL);
			fsl = getfld(vfld, &fslsz);
			if (!strcmp(pname, "Index Expression"))
			{
				dbi->explist = TXfstrlsttoc(vfld, 1);
				continue;
			}
			if (!strcmp(pname, "Locale"))
			{
				if (fsl->nb > 0)
					dbi->locale = TXstrdup(TXPMBUFPN, fn, fsl->buf);
				continue;
			}
			/* textsearchmode is in SYSINDEX.PARAMS;
			 * see TXtextParamsTo3dbi()
			 */
			if (!strcmp(pname, "Noise List"))
			{
				dbi->noiselist = TXfstrlsttoc(vfld, 1);
				continue;
			}
			if (!strcmp(pname, "Aux Size"))
			{
				if (fsl->nb > 0)
					dbi->auxsz = strtol(fsl->buf, NULL, 0);
				continue;
			}
			if (!strcmp(pname, "Aux DD"))
			{
				dbi->auxdd = convertdd(fsl, fslsz);
				continue;
			}
			if (!strcmp(pname, "Version"))
			{
				if (fsl->nb > 0)
					dbi->version =
						strtol(fsl->buf, NULL, 0);
				continue;
			}
			if (!strcmp(pname, "Table Size"))
			{
				if (fsl->nb > 0)
					dbi->paramTblInfo.originalTableSize =
					     TXstrtoepioff_t(fsl->buf, CHARPN,
							    NULL, 0, NULL);
				continue;
			}
			if (strcmp(pname, "Total RowCount") == 0)
			{
				if (fsl->nb > 0)
					dbi->paramTblInfo.totalRowCount =
						TXstrtoh(fsl->buf,CHARPN,
							      NULL, 0, NULL);
				continue;
			}
			if (strcmp(pname, "Total OccurrenceCount") == 0)
			{
				if (fsl->nb > 0)
					dbi->paramTblInfo.totalOccurrenceCount =
					TXstrtoh(fsl->buf, CHARPN, NULL, 0,
						 NULL);
				continue;
			}
			if (strcmp(pname, "Total Words") == 0)
			{
				if (fsl->nb > 0)
					dbi->paramTblInfo.totalWords =
					TXstrtoh(fsl->buf, CHARPN, NULL, 0,
						 NULL);
				continue;
			}
			if (strcmp(pname, "Max Word Len") == 0)
			{
				if (fsl->nb > 0)
					dbi->paramTblInfo.maxWordLen =
						TXstrtosize_t(fsl->buf, CHARPN,
							 NULL, 0, NULL);
				continue;
			}
		}
	}
	return -1;
}

int
TXcreateAndWriteDbiParamsTable(tblPath, dbi)
CONST char	*tblPath;	/* (in) path to _P table (sans ext.) */
A3DBI		*dbi;		/* (in) A3DBI to write */
/* Creates _P.tbl `tblPath' and writes parameters from `dbi' to it.
 * Note that more parameters (e.g. table size) may be added later.
 * Returns 0 on error.
 */
{
	static CONST char	fn[] = "TXcreateAndWriteDbiParamsTable";
	DD			*dd;
	TBL			*tbl = NULL;
	RECID			*recid;
	FLD			*paramFld, *valueFld;
	ft_strlst		*fsl = NULL;
	int			ret, fslSz = 0;
	char			*strlist[2], tbuf[PATH_MAX];

	dd = opennewdd(2);
	if (!dd) return(0);
	putdd(dd, "Param", "varchar", 80, 0);
	putdd(dd, "Value", "varstrlst", 1, 0);
	tbl = createtbl(dd, (char *)tblPath);
	dd = closedd(dd);
	if (!tbl) goto err;
	paramFld = nametofld(tbl, "Param");
	valueFld = nametofld(tbl, "Value");

	putfld(paramFld, "Index Expression", 16);
	fsl = _ctofstrlst(dbi->explist, &fslSz);
	if (!fsl) goto err;
	putfld(valueFld, fsl, fslSz);
	recid = puttblrow(tbl, NULL);
	if (!TXrecidvalid3(recid)) goto err;
	fsl = TXfree(fsl);

	putfld(paramFld, "Locale", 6);
        strlist[0] = dbi->locale;
        strlist[1] = CHARPN;
        fsl = _ctofstrlst(strlist, &fslSz);
        if (!fsl) goto err;
	putfld(valueFld, fsl, fslSz);
	recid = puttblrow(tbl, NULL);
	if (!TXrecidvalid3(recid)) goto err;
	fsl = TXfree(fsl);

	putfld(paramFld, "Noise List", 10);
	fsl = _ctofstrlst(dbi->noiselist, &fslSz);
	if (!fsl) goto err;
	putfld(valueFld, fsl, fslSz);
	recid = puttblrow(tbl, NULL);
	if (!TXrecidvalid3(recid)) goto err;
	fsl = TXfree(fsl);

	if (dbi->auxdd)
	{
		putfld(paramFld, "Aux Size", 8);
		htsnpf(tbuf, sizeof(tbuf), "%wd", (EPI_HUGEINT)dbi->auxsz);
		strlist[0] = tbuf;
		strlist[1] = NULL;
		fsl = _ctofstrlst(strlist, &fslSz);
		if (!fsl) goto err;
		putfld(valueFld, fsl, fslSz);
		recid = puttblrow(tbl, NULL);
		if (!TXrecidvalid3(recid)) goto err;
		fsl = TXfree(fsl);

		putfld(paramFld, "Aux DD", 6);
		putfld(valueFld, dbi->auxdd, dbi->auxdd->size);
		recid = puttblrow(tbl, NULL);
		if (!TXrecidvalid3(recid)) goto err;
	}

	putfld(paramFld, "Version", 7);
	htsnpf(tbuf, sizeof(tbuf), "%ld", (long)dbi->version);
	strlist[0] = tbuf;
	strlist[1] = CHARPN;
	fsl = _ctofstrlst(strlist, &fslSz);
	if (!fsl) goto err;
	putfld(valueFld, fsl, fslSz);
	recid = puttblrow(tbl, NULL);
	if (!TXrecidvalid3(recid)) goto err;
	fsl = TXfree(fsl);

	/* Table Size is added after we've indexed, in case the table
	 * size changes.   KNG 000417
	 */
	ret = 1;				/* success */
	goto done;

err:
	tbl = closetbl(tbl);
	TXcatpath(tbuf, tblPath, ".tbl");
	tx_delindexfile(MERR, fn, tbuf, 0);
	ret = 0;
done:
	tbl = closetbl(tbl);
	fsl = TXfree(fsl);
	return(ret);
}

int
TXtextParamsTo3dbi(A3DBI *dbi,		/* (out) object to set params on */
		   const char *buf,	/* (in) buffer to parse params from */
		   const char *indexPath,/* (in) index name, for messages */
		   int flags)		/* (in) flags */
/* Parses SYSINDEX.PARAMS buffer `buf' and sets corresponding params in `dbi'.
 * (Note that most params come from ..._P.tbl instead; see
 * TXreadDbiParamsFromTable().)
 * If `flags & 1', messages are issued instead of suppressed.
 * If `flags & 2', error messages are warnings instead of errors and are
 * recoverable if possible; e.g. this is PM_SELECT read-only access, not
 * index update.
 * Returns -2 on severe error (bad/invalid param and !`flags & 1'), -1 on
 * probably-recoverable error (bad/invalid param, `flags & 1' set; still sets
 * `dbi' as best as possible), or 0 if ok.
 */
{
	static CONST char	fn[] = "TXtextParamsTo3dbi";
	static CONST char	sepeqwhite[] = ";= \t\r\n\v\f";
#define white			(sepeqwhite + 2)
	static CONST char	willTryToUse[] = "; will try to use index but results may be incorrect";
	size_t			n;
	CONST char		*val, *valE;
	int			ret = 0, res;
	int			yap = (flags & 0x1), recover = (flags & 0x2);
	TXCFF			textSearchMode;

	for ( ; ; buf = valE + (*valE == ';' ? 1 : 0))
	{
		buf += strspn(buf, white);
		if (*buf == '\0') break;	/* end of string */
		n = strcspn(buf, sepeqwhite);	/* length of token name */
		val = buf + n;
		val += strspn(val, white);
		if (*val == '=') val++;
		valE = val + strcspn(val, ";");
		if (n == 0) continue;		/* empty statement */
		if (n == TX_TEXTSEARCHMODESTR_SZ &&
		    strncmp(buf, textSearchModeStr, TX_TEXTSEARCHMODESTR_SZ) == 0)
		{
			/* SYSINDEX.PARAMS textsearchmode is absolute,
			 * because we do not know the current/default
			 * setting in effect when the index was created:
			 */
			res = TXstrToTxcff(val, valE, (TXCFF)(-1),(TXCFF)(-1),
					(TXCFF)(-1), 0, (TXCFF)(-1),
					&textSearchMode);
			dbi->textsearchmode = (int)textSearchMode;
			if (!res)
			{
				/* If we do not know the mode/flag(s),
				 * we must fail the open, otherwise we
				 * might corrupt the index (unless `recover'):
				 */
				if (yap)
					putmsg((recover ? MWARN : MERR) + UGE, fn, "Index %s uses unknown textsearchmode `%.*s': Incompatible, use later Texis release%s",
						indexPath, (int)(valE - val), val,
						(recover ? willTryToUse : ""));
				ret = (recover ? -1 : -2);	/* error */
				/* `dbi->textsearchmode' set to best guess */
			}
		}
		else				/* unknown token */
		{
			/* Unknown token is an error, because we cannot know
			 * if we are compatible with the index or not:
			 */
			if (yap)
				putmsg((recover ? MWARN : MERR) + UGE, fn, "Index %s has unknown SYSINDEX.PARAMS setting `%.*s': Incompatible, use later Texis release%s",
					indexPath, (int)(valE - buf), buf,
					(recover ? willTryToUse : ""));
			ret = (recover ? -1 : -2);	/* error */
		}
	}
	return(ret);
#undef white
}

size_t
TX3dbiParamsToText(buf, bufSz, dbi)
char	*buf;	/* (out) buffer to write params to */
size_t	bufSz;	/* (in) `buf' size */
A3DBI	*dbi;	/* (in) object to get params from */
/* Writes SYSINDEX.PARAMS params to `buf' from `dbi'.
 * (Note that most params go to ..._P.tbl instead, written elsehwere.)
 * Returns would-be strlen of `buf'; if >= `bufSz', `buf' was too small.
 * Nul-terminates `buf' if room.
 */
{
	size_t	needSz = 0, sz;
	char	tmp[MM_INDEX_PARAMS_TEXT_MAXSZ];

	if (bufSz > 0) buf[0] = '\0';

	/* KNG 20080402 Previous Texis versions do not have textsearchmode,
	 * and it effectively defaulted to ...DEFAULT_OLD then, so if we
	 * did not see textsearchmode in SYSINDEX.PARAMS we know to use
	 * the old default.  So for brevity (and limited back-compatibility)
	 * we only need to store textsearchmode into SYSINDEX.PARAMS here
	 * if it has changed from the *old* default.  We also store it
	 * symbolically rather than numerically, for human readability;
	 * it is less machine-compatible that way, but old versions
	 * cannot *implement* numeric flags they do not know anyway:
	 */
	if (dbi->textsearchmode != TXCFF_TEXTSEARCHMODE_DEFAULT_OLD)
	{
		strcpy(tmp, textSearchModeStr);
		tmp[TX_TEXTSEARCHMODESTR_SZ] = '=';
		sz = TXtxcffToStr(tmp + TX_TEXTSEARCHMODESTR_SZ + 1,
				  sizeof(tmp) - (TX_TEXTSEARCHMODESTR_SZ + 1),
				  dbi->textsearchmode) +
					TX_TEXTSEARCHMODESTR_SZ + 1;
		if (sz + 1 < sizeof(tmp))	/* it + ";" fits into `buf' */
		{
			tmp[sz] = ';';
			tmp[sz+1] = '\0';
		}
		sz++;				/* for `;' */
		needSz += sz;
		if (sz < bufSz)			/* will fit in `buf' */
		{
			strcpy(buf, tmp);
			buf += sz;
			bufSz -= sz;
		}
	}

	return(needSz);
}

int
TX3dbiScoreIndex(int indexType,			/* (in) INDEX_ type (M/F) */
		 const char *sysindexParams,	/* (in) SYSINDEX.PARAMS text*/
		 const int *ddicOptions,	/* (in) DDIC.options */
		 const char *indexPath,		/* (in) index path */
		 QNODE_OP op)			/* (in) operation */
/* Returns an index score for SYSINDEX.PARAMS text `sysindexParams' for
 * Metamorph index `indexPath', based on comparing it to current settings.
 * 0 means index cannot be used at all.  Higher numbers indicate greater
 * preference.  Value returned must be in range 0-TX_INDEX_SUBSCORE_MAX.
 */
{
	static CONST char	fn[] = "TX3dbiScoreIndex";
	A3DBI			dbi;
	TXCFF			modeCur, modeIdx, flagsCur, flagsIdx, flag;
	TXCFF			caseStyleCur, caseStyleIdx;
	int			score, flagScoreVal, paramRes;
	TXbool			isMetamorphCounterIndex;
	char			*ctrPath = NULL;

	(void)ddicOptions;
	memset(&dbi, 0, sizeof(A3DBI));
	TXinit3dbiParams(&dbi);			/* set defaults */
	/* Assume we are scoring indexes for read-only (PM_SELECT)
	 * usage, and try to recover from SYSINDEX.PARAMS errors: maybe
	 * try to use such an index for reading later (but see score
	 * deduction below).  Also suppress messages, since any
	 * failure may be for an index we are not going to choose
	 * anyway, and the index we do choose will be opened (and
	 * errors reported) separately elsewhere:
	 */
	paramRes = TXtextParamsTo3dbi(&dbi, sysindexParams, indexPath, 0x2);
	if (paramRes < -1) goto err;		/* severe error parsing */
	if (globalcp == APICPPN) globalcp = TXopenapicp();
	/* We prefer an index that agrees with current settings,
	 * so that we search as expected (i.e. if case-sensitive and
	 * ignore-case indexes are both present, select appropriate),
	 * but use it even if it does not agree (probably better than
	 * linear search).  The more it agrees, the better:
	 * NOTE: see also TXbtreeScoreIndex():
	 */
	score = 1;				/* at least non-zero */

	modeCur = TXCFF_GET_CASEMODE(globalcp->textsearchmode);
	modeIdx = TXCFF_GET_CASEMODE(dbi.textsearchmode);
	if (modeCur == modeIdx)
		score += 20;
	/* unicodemono and unicodemulti mode are more similar than
	 * either are to ctype, so if both modes are unicode,
	 * give partial credit:
	 */
	else if (modeCur != TXCFF_CASEMODE_CTYPE &&
		 modeIdx != TXCFF_CASEMODE_CTYPE)
		score += 12;

	caseStyleCur = TXCFF_GET_CASESTYLE(globalcp->textsearchmode);
	caseStyleIdx = TXCFF_GET_CASESTYLE(dbi.textsearchmode);
	if (caseStyleCur == caseStyleIdx)
		score += 10;
	/* lowercase is similar to ignorecase, and uppercase is similar
	 * to titlecase:
	 */
	else if ((caseStyleCur == TXCFF_CASESTYLE_IGNORE ||
		  caseStyleCur == TXCFF_CASESTYLE_LOWER) &&
		 (caseStyleIdx == TXCFF_CASESTYLE_IGNORE ||
		  caseStyleIdx == TXCFF_CASESTYLE_LOWER))
		score += 8;
	else if ((caseStyleCur == TXCFF_CASESTYLE_UPPER ||
		  caseStyleCur == TXCFF_CASESTYLE_TITLE) &&
		 (caseStyleIdx == TXCFF_CASESTYLE_UPPER ||
		  caseStyleIdx == TXCFF_CASESTYLE_TITLE))
		score += 8;

	/* Flags are roughly ordered in unicode.h such that larger
	 * flag int values are more "important" here, so increase
	 * score obtained by each higher flag:
	 */
	flagsCur = TXCFF_GET_FLAGS(globalcp->textsearchmode);
	flagsIdx = TXCFF_GET_FLAGS(dbi.textsearchmode);
	for (flag = TXCFF_FLAG_MIN, flagScoreVal = 10;
	     flag <= TXCFF_FLAG_MAX;
	     flag <<= 1, flagScoreVal += flagScoreVal/3)
		if ((flagsIdx & flag) == (flagsCur & flag))
			score += flagScoreVal;

	/* Take off points if there was an error parsing params: */
	if (paramRes != 0) score -= 50;		/* WAG; see also below */

	/* Bug 7112: check index type (Metamorph inverted/compact/counter)
	 * against op (LIKEP/LIKEIN/LIKE3).  E.g. try not to use counter
	 * index (with non-standard index expressions and no word positions)
	 * for non-LIKEIN query, and prefer inverted over compact in case
	 * word positions needed for phrases etc.:
	 */
	/* wtf store is-counter-index knowledge in options/params
	 * to avoid stat?
	 */
	ctrPath = TXstrcat2(indexPath, "_C");
	if (!ctrPath) goto err;
	isMetamorphCounterIndex = !!existsbtree(ctrPath);
	switch (op)
	{
	case FOP_MMIN:				/* LIKEIN */
		/* LIKEIN will likely fail spectactularly without a
		 * true metamorph counter index, so completely
		 * disallow this index if it is not counter:
		 */
		if (!isMetamorphCounterIndex) goto err;
		break;
	case FOP_NMM:				/* LIKE3 */
	case FOP_RELEV:				/* LIKER */
	case FOP_MM:				/* LIKE */
	case FOP_PROXIM:			/* LIKEP */
		/* We do not have an open FDBI object here to truly
		 * know if inverted is needed, but assume it might be
		 * needed for phrase resolution etc. and favor
		 * inverted over compact:
		 */
		if (indexType != INDEX_FULL)
			score -= 50;		/* WAG; see also above */
		/* Non-LIKEIN LIKE might make use of a counter index,
		 * but it's probably worse than compact:
		 */
		if (isMetamorphCounterIndex)
			score -= 10;		/* WAG */
		break;
	default:
		break;
	}

	if (score > TX_INDEX_SUBSCORE_MAX)	/* should not happen */
	{
		putmsg(MWARN, fn, "Index sub-score exceeds max");
		score = TX_INDEX_SUBSCORE_MAX;
	}
	else if (score < 1)
		score = 1;
	goto finally;

err:
	score = 0;
finally:
	ctrPath = TXfree(ctrPath);
	return(score);
}

/******************************************************************/

A3DBI *
open3dbi(name, mode, type, sysindexParams)
char		*name;	/* (in) path to index, sans file extension */
int		mode;	/* (in) PM_... mode flags */
int		type;	/* (in) INDEX_... type e.g. INDEX_FULL */
CONST char	*sysindexParams;	/* (in) SYSINDEX.PARAMS value */
{
	static CONST char Fn[] = "open3dbi";
	A3DBI *dbi;
	BTLOC btloc;
	TBL *ptbl;
	char buf[PATH_MAX];

	dbi = TX3dbiOpen(type);
	if (dbi == A3DBIPN) return(A3DBIPN);

	TXcatpath(buf, name, "_P");
	ptbl = opentbl(TXPMBUFPN, buf);
	if (ptbl == (TBL *) NULL)
	{
		dbi->explist = _duplst(expressions);
	}
	else
	{
		TXreadDbiParamsFromTable(dbi, ptbl);
		closetbl(ptbl);
		ptbl = TBLPN;
	}
	/* Allow SYSINDEX.PARAMS parse errors if we are only SELECTing
	 * from the index, on the theory that it is probably still
	 * largely usable, and using it is probably better than a linear
	 * search.  But if we are modifying the index, do not allow errors,
	 * or we might corrupt the index.  Report errors regardless:
	 */
	if (TXtextParamsTo3dbi(dbi, sysindexParams, name,
			       ((mode == PM_SELECT ? 0x2 : 0) | 0x1)) < -1)
		return(close3dbi(dbi));
#ifndef NEVER
	if (mode & PM_SELECT)
	{
#endif
		if (type != INDEX_MM &&
		    type != INDEX_MMCR && type != INDEX_FULL && type != INDEX_FULLCR)	/* KNG 971011 */
		{
			TXcatpath(buf, name, "_M");
			/* Read last token.  Close files not needed for search */
			dbi->ttbl = openrttbl(buf, dbi->explist);
			if (dbi->ttbl != (TTBL *) NULL)
			{
				btloc =
					btsearch(dbi->ttbl->bt, strlen(lts),
						 (char *)lts);
				dbi->lasttoken = (ulong)TXgetoff(&btloc);
				if (
				    (mode &
				     (PM_INSERT | PM_SELECT | PM_DELETE)) ==
				    0)
					dbi->ttbl = closettbl(dbi->ttbl);
			}

			dbi->mm = openmmtbl(buf);
			if (dbi->mm == (MMTBL *) NULL)
				return close3dbi(dbi);
			TXcatpath(buf, name, "_B");
			if (!existsbtree(buf))
			{
				putmsg(MERR, Fn,
				       "Index %s should exist, but does not",
				       name);
				return close3dbi(dbi);
			}
			dbi->td = openbtree(buf, BTFSIZE, BTCSIZE, 0, O_RDWR);
			if (dbi->td == (BTREE *) NULL)
				return close3dbi(dbi);
		}		/* type != INDEX_FULL/CR */
		TXcatpath(buf, name, "_C");
		if (existsbtree(buf))
		{
			dbi->ct = openbtree(buf, BTFSIZE, BTCSIZE,
					    (BT_UNIQUE | BT_FIXED), O_RDWR);
	/* no bttexttoparam() call: SYSINDEX.PARAMS belongs to WTIX/FDBI */
		}
#ifndef NEVER
	}
	if (mode & (PM_INSERT | PM_SELECT | PM_DELETE))
	{
#endif
		TXcatpath(buf, name, "_D");
		dbi->del = openbtree(buf, BTFSIZE, BTCSIZE,
				     (BT_UNIQUE | BT_FIXED), O_RDWR);
		if (dbi->del == (BTREE *) NULL)
			return close3dbi(dbi);
	/* no bttexttoparam() call: SYSINDEX.PARAMS belongs to WTIX/FDBI */
		if (type != INDEX_MM && type != INDEX_MMCR &&	/* KNG 971111 */
		    type != INDEX_FULL && type != INDEX_FULLCR)
		{
			TXcatpath(buf, name, "_U");
			dbi->upd = openbtree(buf, BTFSIZE, BTCSIZE, 0, O_RDWR);
			if (dbi->upd == (BTREE *) NULL)
				return close3dbi(dbi);
		}
		TXcatpath(buf, name, "_T");
		if (dbi->auxdd)
		{
			dbi->newrec = openbtree(buf, 8192, BTCSIZE, 0, O_RDWR);
			/* btreesetdd()? no: makes btree dirty */
		}
		else
			dbi->newrec = openbtree(buf, BTFSIZE, BTCSIZE,
					      (BT_UNIQUE | BT_FIXED), O_RDWR);
		if (dbi->newrec == (BTREE *) NULL)
			return close3dbi(dbi);
	/* no bttexttoparam() call: SYSINDEX.PARAMS belongs to WTIX/FDBI */
#ifndef NEVER
	}
#endif
/* See if the new piles are there */
	/* KNG 011218 But if we're _only_ doing PM_SELECT, we don't care
	 * about new piles until the index update process flips them.
	 * Only a table- or index-update process cares about them:
	 */
	if (mode != PM_SELECT)
	{
		TXcatpath(buf, name, "_X.btr");
		if (existsbtree(buf))
		{
#ifndef NEVER
			if (mode & (PM_INSERT | PM_SELECT | PM_DELETE))
			{
#endif
				dbi->mdel = openbtree(buf, BTFSIZE, BTCSIZE,
					      (BT_UNIQUE | BT_FIXED), O_RDWR);
				if (dbi->mdel == (BTREE *) NULL)
					return close3dbi(dbi);
	/* no bttexttoparam() call: SYSINDEX.PARAMS belongs to WTIX/FDBI */
				if (type != INDEX_MM &&
				    type != INDEX_MMCR &&
				    type != INDEX_FULL && type != INDEX_FULLCR)
				{
					TXcatpath(buf, name, "_Y");
					dbi->mupd = openbtree(buf, BTFSIZE,
				     BTCSIZE, (BT_UNIQUE | BT_FIXED), O_RDWR);
					if (dbi->mupd == (BTREE *) NULL)
						return close3dbi(dbi);
				}
				TXcatpath(buf, name, "_Z");
				if (dbi->auxdd)
				{
					dbi->mnew = openbtree(buf, 8192,
							 BTCSIZE, 0, O_RDWR);
					/* btreesetdd()? no: makes it dirty */
				}
				else
					dbi->mnew = openbtree(buf, BTFSIZE,
				     BTCSIZE, (BT_UNIQUE | BT_FIXED), O_RDWR);
				if (dbi->mnew == (BTREE *) NULL)
					return close3dbi(dbi);
	/* no bttexttoparam() call: SYSINDEX.PARAMS belongs to WTIX/FDBI */
#ifndef NEVER
			}
#endif
		}
	}

	dbi->name = TXstrdup(TXPMBUFPN, Fn, name);
	if (!dbi->name)
		return close3dbi(dbi);
	dbi->creating = 0;
	return dbi;
}

/******************************************************************/

BTREE *
TXset3dbi(dbi, infld, fname, dbtbl, nopre, nhits, nopost, import, op)
A3DBI *dbi;
FLD *infld;
char *fname;
DBTBL *dbtbl;
int nopre;
EPI_HUGEUINT	*nhits;
int *nopost;
short *import;
int op;
{
	static CONST char	fn[] = "TXset3dbi";
	FLD *tf;
	FLD temp;
	BTREE *rc = (BTREE *) NULL;
	BTLOC btloc, btloc1;
	size_t s;
	EPI_OFF_T token;
	tin_t tintoken;
	DDMMAPI *ddmmapi = getfld(infld, NULL);

	temp = *infld;
	*nhits = 0;
	if (ddmmapi == (void *) NULL
	    || ((DDMMAPI *) ddmmapi)->query == (char *) NULL)
		return rc;
#ifndef NEVER
/* We are assuming here that we don't need lasttoken for setmmatbl if
   we are not doing relevancy ranking */
#else
	if (dbi->lasttoken == 0)
		_openupd3dbi(dbi);
#endif
	if (TXsetmmatbl
	    (dbi->mm, ddmmapi, dbi->lasttoken, NULL, dbi->ct, nopost, import,
	     op) == -1L)
		return rc;
#ifndef NO_INFINITY
	if (TXisinfinite(dbi->mm->tl))
		return rc;
#endif /* NO_INFINITY */
	if (*nopost)
		nopre = 0;
	rc = openbtree(NULL, BTFSIZE, BTCSIZE, BT_FIXED | BT_UNIQUE, O_RDWR | O_CREAT);
	while (getttl(dbi->mm->tl, &tintoken))
	{
		EPI_OFF_T offset;

		token = tintoken;
		btloc = btsearch(dbi->td, sizeof(token), &token);
		if (TXrecidvalid(&btloc))
		{
			offset = TXgetoff(&btloc);
			btinsert(rc, &btloc, sizeof(offset), &offset);
			++*nhits;
		}
#ifdef NEVER
		else
			putmsg(MWARN, NULL,
			       "Your index appears to be corrupt");
#endif
	}
	rewindbtree(dbi->del);
	while (s = sizeof(btloc),
               btloc1 = btgetnext(dbi->del, &s, &btloc, NULL),
	       TXrecidvalid(&btloc1))
	{
		btdelete(rc, &btloc, s, &btloc);
	}
	tf = dbnametofld(dbtbl, fname);
	rewindbtree(dbi->newrec);
	while (s = sizeof(btloc),
               btloc1 = btgetnext(dbi->newrec, &s, &btloc, NULL),
	       TXrecidvalid(&btloc1))
	{
		size_t x;
		RECID *res;
		int match;

		if (nopre)
		{
			EPI_OFF_T to;

			to = TXgetoff(&btloc);
			btinsert(rc, &btloc, sizeof(EPI_OFF_T), &to);
			++*nhits;
			continue;
		}
/*
 *    This needs to do some fancy metamorph search on the junk in
 *      the table, and only insert it if good.  I need the table
 *      handle and field name I care about.
 */
		if (TXlocktable(dbtbl, R_LCK) == -1)
		{
			/* Could not lock table */
			rc = closebtree(rc);
			return (rc);
		}
#ifdef ASSERT_DEL_BLOCK
		if (validrow(dbtbl->tbl, &btloc))
			res = gettblrow(dbtbl->tbl, &btloc);
		else
			res = NULL;
#else
		res = gettblrow(dbtbl->tbl, &btloc);
#endif
		TXunlocktable(dbtbl, R_LCK);
		if (!TXrecidvalid(res))
			continue;
		x = tf->alloced;
		switch (tf->type & DDTYPEBITS)
		{
		case FTN_CHAR:
			if (op == FOP_MMIN)
				match = TXlikein(tf, &temp);
			else
				match = metamorphop(tf, &temp);
			break;
		case FTN_INDIRECT:
			match = fmetamorphop(tf, &temp);
			break;
		case FTN_BLOBI:
			match = bmetamorphop(tf, &temp);
			break;
		default:
			match = 1;
		}
		if (match)
		{
			EPI_OFF_T to;

			to = TXgetoff(&btloc);
			btinsert(rc, &btloc, sizeof(EPI_OFF_T), &to);
			++*nhits;
		}
		if (!tf->shadow)
		{
                        /*
                         * Not quite sure how this code would get hit
                         *
                         * Looks like shadow should always be set, and it
                         * will get freed again later.
                         */
                        TX_FLD_KIND wk = tf->kind;
			setfld(tf, TXmalloc(TXPMBUFPN, fn, x), x);
                        tf->kind = wk;
		}
	}
	rewindbtree(rc);
	return rc;
}

/******************************************************************/

#define EL(x) mm->mme->el[x]
#define NELS  mm->mme->nels

static char **getwordlist ARGS((MMAPI *, TXPMTYPE **));

static char **
getwordlist(mm, typelist)
MMAPI *mm;
TXPMTYPE **typelist;
{
	static CONST char	fn[] = "getwordlist";
	int i, n;
	char **lst;


	n = 1;
	for (i = 0; i < NELS; i++)	/* perform initial count */
	{
		switch (EL(i)->pmtype)
		{
		case PMISPPM:
		case PMISSPM:
			n += 1;
			break;
		default:
			n++;
			break;
		}
	}
	if (!(lst = (char **) TXcalloc(TXPMBUFPN, fn, n, sizeof(char *))))
	{
		return NULL;
	}
	if (!(*typelist = (TXPMTYPE *) TXcalloc(TXPMBUFPN, fn, n, sizeof(TXPMTYPE))))
	{
		lst = TXfree(lst);
		return NULL;
	}
	lst[n - 1] = TXstrdup(TXPMBUFPN, fn, "");
	for (n = i = 0; i < NELS; i++)	/* perform initial count */
	{
		switch ((EL(i)->pmtype))
		{
		case PMISPPM:
		case PMISSPM:
			lst[EL(i)->orpos] = TXstrdup(TXPMBUFPN, fn, EL(i)->lst[0]);
			(*typelist)[EL(i)->orpos] = EL(i)->pmtype;
			break;
		default:	/* Other PM types */
			lst[EL(i)->orpos] = TXstrdup(TXPMBUFPN, fn, EL(i)->lst[0]);
			(*typelist)[EL(i)->orpos] = EL(i)->pmtype;
			break;
		}
	}
	return lst;
}

/******************************************************************/

static short stimport[MAXSELS];

static int i3dbinsert ARGS((PROXBTREE *bt, long token, long order,
				short *import));
static int
i3dbinsert(bt, token, order, import)
PROXBTREE *bt;
long token;
long order;
short *import;
{
	BTLOC btloc, btlocd;
	EPI_OFF_T token1, token2;

/*
   putmsg(999, NULL, "Inserting token %ld with relevance %ld", token, order);
 */
	(void)import;
	token1 = -order;
	token2 = token;
	btloc = btsearch(bt->s, sizeof(token2), &token2);
	if (TXrecidvalid(&btloc))
	{
		if (bt->d)
			btlocd = btsearch(bt->d, sizeof(btloc), &btloc);
		else
			TXsetrecid(&btlocd, -1);
		if (!TXrecidvalid(&btlocd))
		{
			bt->cnt++;
			btinsert(bt->i, &btloc, sizeof(token1), &token1);
		}
	}
	return 0;
}

/******************************************************************/

static int heapcmp ARGS((void *a, void *b, void *usr));
static int
heapcmp(a, b, usr)
void *a, *b, *usr;
{
	(void)usr;
	return ((int)(long) a - (int)(long) b);
}

/******************************************************************/

static void i3dbfinit ARGS((void *usr, int postreduce));
static void
i3dbfinit(usr, postreduce)
void *usr;			/* user data */
int postreduce;			/* post-process needed _and_ likely to reduce hits */

/* Initial fdbi_get() callback function for setf3dbi().  Called once
 * before first "normal" callback.
 */
{
	PROXBTREE *pbt = (PROXBTREE *) usr;
	EXTRA	*exs;

	exs = (EXTRA *)pbt->xxx;

	if (postreduce)
	{
		/* A post-process is needed and will likely reduce the overall
		 * hit count, so don't filter out low hits now with the heap
		 * (i.e. leave enough for a good post-process):
		 */
		pbt->fh = closefheap(pbt->fh);
		if (exs)
		{
			/* No keyrecsetmaxsortrows(maxrows), even if
			 * maxrows: will need all rows later for
			 * post-process, and KEYREC heap can truncate
			 * KEYREC.items.
			 */
			exs->useMaxRows = 0;
			/* Optimization: If we have an ORDER BY
			 * containing $rank (or might -- ORDER BY
			 * num), $rank will change during
			 * post-process, so do not sort for ORDER BY
			 * now: waste of time, and post-proc is likely
			 * more efficient if reading rows in recid
			 * order.  More important is to not do
			 * likeprows cutoff (Bug 4166 comment #3), but
			 * that is handled by rank heap close above,
			 * and no-likeprows-cutoff check in dolikep():
			 */
			if (!TXApp->legacyVersion7OrderByRank &&
			    (exs->haveOrderByContainingRank ||
			     exs->haveOrderByNum))
			{
				exs->nosort = 1;
				if (exs->keyrec)
					exs->keyrec->sorted = 1;
				/* Close the `order' that tells
				 * TXdotree() ORDER_OP case that we
				 * did the ORDER BY, since we are not
				 * doing it.  WTF put setf3dbi()'s
				 * `savedorderby' back in place?:
				 */
				if (pbt->t)
				    pbt->t->order = closeproj(pbt->t->order);
			}
		}
		if (TXtraceIndexBits & 0x200000)
			putmsg(MINFO, __FUNCTION__,
       "Rank heap closed, useMaxRows set to 0%s: Need Metamorph post-process",
			       ((exs && !TXApp->legacyVersion7OrderByRank &&
				 (exs->haveOrderByContainingRank ||
				  exs->haveOrderByNum)) ?
			 ", KEYREC sort (using $rank expr) turned off" : ""));
	}
	else					/* no MM post-proc needed */
	if (exs)
	{
		if (!exs->lonely)
		{
			/* We need more than maxrows items later, for
			 * e.g. AND post-processing; do not set a
			 * KEYREC heap (which can truncate
			 * KEYREC.items).
			 */
			exs->useMaxRows = 0;
			if (TXtraceIndexBits & 0x200000)
				putmsg(MINFO, __FUNCTION__,
				       "useMaxRows set to 0: lonely is 0");
		}
		/* If we only need `maxrows' items, ok to use keyrec heap: */
		if (exs->useMaxRows > 0)
			keyrecsetmaxsortrows(exs->keyrec, exs->useMaxRows);
	}
}

/******************************************************************/

static FASTBUF *fastbufinit ARGS((void *buf, TBL *tbl, size_t sz));

static FASTBUF *
fastbufinit(buf, tbl, sz)
void *buf;
TBL *tbl;
size_t sz;
{
	static CONST char	fn[] = "fastbufinit";
	FASTBUF *rc;
	unsigned int i;
	FLD *fld;

	rc = TXcalloc(TXPMBUFPN, fn, tbl->n, sizeof(FASTBUF));
	if (!rc) return(NULL);
	buftofld(buf, tbl, sz);
	for (i = 0; i < tbl->n; i++)
	{
		fld = tbl->field[i];
		rc[i].fld = fld;
		rc[i].off = (byte *) fld->v - (byte *) buf;
	}
	return rc;
}

/******************************************************************/

static int fastbuftofld ARGS((void *, TBL *, size_t, FASTBUF *));

static int
fastbuftofld(buf, tbl, sz, fbufinfo)
void *buf;
TBL *tbl;
size_t sz;
FASTBUF *fbufinfo;
{
	unsigned int i;
	FLD *fld;

	(void)sz;
	for (i = 0; i < tbl->n; i++)
	{
		fld = fbufinfo[i].fld;
		fld->shadow = (byte *) buf + fbufinfo[i].off;
		setfldv(fld);
	}
	return 0;
}

/******************************************************************/

static int i3dbfauxok ARGS((void *usr, RECID recid, void *auxfld));
static int
i3dbfauxok(usr, recid, auxfld)
void *usr;
RECID recid;
void *auxfld;

/* We check here if there are any WHERE clauses we can use to discard
 * this record.  Returns nonzero if this record matches.  Note that
 * this could be a deleted recid, so we can't save the recid.
 */
{
	PROXBTREE *bt = (PROXBTREE *) usr;
	EXTRA *exs = bt->xxx;
	DBTBL *dbtbl;
	size_t extrasz = 0;
	FLDOP *fo;
	PRED *goodpred;

	(void)recid;
	if (auxfld != NULL && exs != EXTRAPN && exs->goodpred)
	{
		dbtbl = exs->dbtbl;
		extrasz = exs->extrasz;
		fo = exs->fc->fo;
		goodpred = exs->goodpred;
#ifndef NEVER
		if (!exs->fbufinfo)
			exs->fbufinfo =
				fastbufinit(auxfld, dbtbl->tbl, extrasz);
		else
			fastbuftofld(auxfld, dbtbl->tbl, extrasz,
				     exs->fbufinfo);
#else
		buftofld(auxfld, dbtbl->tbl, extrasz);	/* Create the appropriate table */
#endif
#ifndef NEVER
		if (goodpred->lat == FIELD_OP && goodpred->rt == FIELD_OP)
		{
			FLD *f1, *f2;
			FLD f3;
			ft_int rc;

			initfld(&f3, FTN_INT, 1);
			putfld(&f3, &rc, 1);
			f1 = goodpred->altleft;
			if (goodpred->rat == FIELD_OP)
				f2 = goodpred->altright;
			else
			{
				f2 = goodpred->right;
				/*
				   If the types differ, try and downconvert type.
				   Don't do if the Compound index contains character
				   data as we may run into prefix issues.
				   WTF JMT 2000-08-01 
				   KNG 20060707 for "... where Text like
				     'query' and LongField between (9, 12)":
				   varlong(2) FOP_CNV long(1) now would give
				   long(1) not varlong(2) (fldop1.c:1.11);
				   would break FOP_TWIXT later.  Don't convert
				   if same underlying type:
				 */
				if (goodpred->rat != NO_OP &&
				   ((f1->type & DDTYPEBITS) != (f2->type & DDTYPEBITS)) &&
				   (f1->type != FTN_CHAR))
				{
				/* KNG 20110225 for "... where Text like
				 *   'query' and IntFld in (1,2,3)":
				 * `[var]long(3) FOP_CNV int(1)' here would
				 * give int(1) not [var]int(3) (fldop2.c:1.7);
				 * temp. set DDVARBIT to preserve # values:
				 */
					int orgVarBit = (f1->type & DDVARBIT);
					int restoreVarBit = 0, res;

					if (TXftnIsNumeric(f1->type) &&
					    TXftnIsNumeric(f2->type))
					{
						f1->type |= DDVARBIT;
						restoreVarBit = 1;
					}
					fopush(fo, f2);	/* left arg */
					fopush(fo, f1);	/* right arg */
					res = foop(fo, FOP_CNV);
					if (restoreVarBit)
					{
						f1->type &= ~DDVARBIT;
						f1->type |= orgVarBit;
					}
					if (res != -1)
					{
						goodpred->altright =
							fopop(fo);
						goodpred->rat = FIELD_OP;
						goodpred->fldmathfunc = NULL;
						f2 = goodpred->altright;
					}
					else
					{
						goodpred->rat = NO_OP;
					}
				}
			}
			if (!exs->cmpfunc)
				fogetop(fo, f1->type, f2->type,
					&exs->cmpfunc);
			if (exs->cmpfunc)
			{
				if (exs->cmpfunc(f1, f2, &f3, goodpred->op) ==
				    0)
				{
					ft_int rc = *(ft_int *) getfld(&f3, NULL);
					TXfreefldshadow(&f3);
					return rc;
				}
				else
					TXfreefldshadow(&f3);
			}
		}
#endif
		if (!tup_match(dbtbl, goodpred, fo))
		{
			return 0;	/* 0 = doesn't match */
		}
	}
	return 1;
}

/******************************************************************/

static int i3dbfinsert ARGS((void *usr, RECID recid, void *auxfld, int rank));
static int
i3dbfinsert(usr, recid, auxfld, rank)
void *usr;
RECID recid;
int rank;
void *auxfld;
/* fdbi_get() callback: saves given `recid' with optional `auxfld', and its
 * `rank'.  Returns minimum rank to look for, or RPPM_MIN_WT - 1 if this
 * record doesn't match (don't count in indexcount).
 */
{
	PROXBTREE *pbt = (PROXBTREE *) usr;
	EPI_OFF_T ranktok;
	int iranktok;
	BTLOC rankloc;
	FHEAP *fh = (FHEAP *) pbt->fh;
	int rc = pbt->threshold;
	EXTRA *exs = pbt->xxx;
	size_t extrasz = 0;
	enum
	{
		NOT_DISCARDED,
		LTE_PBT_THRESHOLD,
		LT_LOW_HEAP_RANK,
		SOME_ERROR,
	}
	discardReason = NOT_DISCARDED;

	if (rank <= rc)		/* must be non-negative rank */
	{
		discardReason = LTE_PBT_THRESHOLD;
		goto nonMatch;
	}

	pbt->cntg += 1;		/* for new records */

	/* Existence of the heap means we're allowed to limit the result
	 * set to the top likeprows hits, because we know we won't need
	 * more (e.g. for other ANDed SQL clauses, or post-processing):
	 */
	if (fh != FHEAPPN)
	{
		char	xtraMsgBuf[1024];

		*xtraMsgBuf = '\0';
		if (fheap_num(fh) >= (size_t)TXnlikephits)
		{				/* full: delete low guy? */
			rc = (int)(long) fheap_top(fh);	/*lowest rank in heap*/
			if (rank < rc)		/*   no: this rank too low */
			{
				discardReason = LT_LOW_HEAP_RANK;
				goto finally;
			}
			if (TXtraceIndexBits & 0x400000)/* Bug 6796 tracing */
				htsnpf(xtraMsgBuf, sizeof(xtraMsgBuf),
				       " (and deleted earlier rank %d)",
				       (int)rc);
			fheap_deletetop(fh);
		}
		fheap_insert(fh, (void *)(EPI_VOIDPTR_INT)rank);
		if (TXtraceIndexBits & 0x400000)	/* Bug 6796 tracing */
			putmsg(MINFO, __FUNCTION__,
		      "Inserted rank %d (for recid 0x%08wx) into rank heap%s",
			       (int)rank, (EPI_HUGEINT)TXgetoff2(&recid),
			       xtraMsgBuf);
	}

	/* Negative rank is how we historically
	 * (legacyVersion7OrderByRank) got $rank-descending ordering,
	 * both LIKEP default and for explicit `ORDER BY $rank'.  But
	 * it means `ORDER BY $rank + X' is wrong (neither ascending
	 * nor descending) if `X' is roughly ascending positive (when
	 * sorted by rank), because evalpred() below would use $rank <
	 * 0.  So make rank positive.  But see also
	 * TXmakeOrderByRankProj() and other legacyVersion7OrderByRank
	 * places where we compensate for positive [internal] ranks:
	 * (e.g. `btsetcmp(..., TXfixedUnsignedReverseCmp)'):
	 */
	iranktok = TX_RANK_USER_TO_INTERNAL(TXApp, rank);
	ranktok = (EPI_OFF_T)TX_RANK_USER_TO_INTERNAL(TXApp, rank);

	if (pbt->flags & (PBF_RECIDORDER | PBF_INVTREE))
	{
		TXsetrecid(&rankloc, ranktok);
		/* WTF use btappend() for indexed records, because they're in
		 * recid order already?:
		 */
		btinsert(pbt->i, &rankloc, sizeof(recid), &recid);
		if (TXtraceIndexBits & 0x400000)	/* Bug 6796 tracing */
			putmsg(MINFO, __FUNCTION__,
	    "Inserted rank loc %wd recid key 0x%08wx into recid-keyed B-tree",
			       (EPI_HUGEINT)TXgetoff2(&rankloc),
			       (EPI_HUGEINT)TXgetoff2(&recid));
	}
	else if (pbt->i->flags & BT_FIXED)
	{
		btinsert(pbt->i, &recid, sizeof(ranktok), &ranktok);
		if (TXtraceIndexBits & 0x400000)	/* Bug 6796 tracing */
			putmsg(MINFO, __FUNCTION__,
	     "Inserted recid loc 0x%08wx rank key %wd into rank-keyed B-tree",
			       (EPI_HUGEINT)TXgetoff2(&recid),
			       (EPI_HUGEINT)ranktok);
	}
	else if (exs != EXTRAPN && exs->keyrec != KEYRECPN)
	{
		FTN	type;
		void *v = NULL;
		byte	*buf = NULL;
		size_t	n;

		/* Using aux data; e.g. SELECT all-aux or
		 * ORDER BY all-aux or indexcache (and no unhandled AND/OR)
		 */

		if (exs->keyrec->extrasz)
		{
			size_t	vSz, needed;

			/* We have computed-from-aux field(s)
			 * (e.g. auxFld + 1) not just aux fields.
			 * Probably an ORDER BY auxFld + 1
			 */

			/* Create the appropriate table */
			buftofld(auxfld, exs->dbtbl->tbl, exs->extrasz);
			exs->dbtbl->rank = iranktok;
			v = evalpred(exs->dbtbl, exs->computedorderby,
				     exs->fc->fo, &n, &type);
			if (v)
			{
				/* Bug 6449: keyrecappend() will
				 * copy `exs->keyrec->extrasz' of
				 * `v': former may be aligned up
				 * and exceed alloced size of `v'.
				 * Copy to big-enough buffer:
				 */
				vSz = n*ddftsize(type);
				needed = exs->keyrec->extrasz;
				if (needed > vSz)
				{	/* need to expand `v' */
					buf = (byte *)TXmalloc(TXPMBUFPN,
							       __FUNCTION__,
							       needed);
					if (!buf) goto err;
					memcpy(buf, v, vSz);
					memset(buf + vSz, 0, needed - vSz);
				}
				else
					buf = v;
			}
			else			/* evalpred() failed */
				goto err;
		}
		keyrecappend(exs->keyrec, &recid, auxfld, ranktok, buf);
		if (buf != v) buf = TXfree(buf);
		if (v) TXftnFreeData(v, n, type, 1);
		v = buf = NULL;
	}
	else
	{
		/* Probably using aux data, but no keyrec: dead code? */
		btinsert(pbt->i, &recid, extrasz, auxfld);
		if (TXtraceIndexBits & 0x400000)
			putmsg(MINFO, __FUNCTION__,
 "Inserted recid 0x%08wx (rank %d dropped) and aux key into aux-keyed B-tree",
			       (EPI_HUGEINT)TXgetoff2(&recid), (int)rank);
	}
        goto finally;

err:
	discardReason = SOME_ERROR;
nonMatch:
	/* wtf another way to signal error? */
	rc = RPPM_MIN_WT - 1;			/* signifies a non-match */
finally:
	if (discardReason != NOT_DISCARDED &&
	    (TXtraceIndexBits & 0x400000))	/* Bug 6796 tracing */
	{
		char	discardReasonBuf[256];

		switch (discardReason)
		{
		case NOT_DISCARDED:
			htsnpf(discardReasonBuf, sizeof(discardReasonBuf),
			       "Not discarded");
			break;
		case LTE_PBT_THRESHOLD:
			htsnpf(discardReasonBuf, sizeof(discardReasonBuf),
			       "Less than or equal to threshold rank %d",
			       (int)pbt->threshold);
			break;
		case LT_LOW_HEAP_RANK:
			htsnpf(discardReasonBuf, sizeof(discardReasonBuf),
			       "Less than lowest heap rank %d",
			       (int)(EPI_VOIDPTR_INT)fheap_top(fh));
			break;
		case SOME_ERROR:
			htsnpf(discardReasonBuf, sizeof(discardReasonBuf),
			       "Error encountered");
			break;
		default:
			htsnpf(discardReasonBuf, sizeof(discardReasonBuf),
			       "Unknown reason");
			break;
		}
		putmsg(MINFO, __FUNCTION__,
		       "Discarded rank %d recid 0x%08wx: %s",
		       (int)rank,  (EPI_HUGEINT)TXgetoff2(&recid),
		       discardReasonBuf);
	}
	return (rc);
}

/******************************************************************/

BTREE *
setr3dbi(dbi, infld, fname, dbtbl, nhits)
A3DBI *dbi;
FLD *infld;
char *fname;
DBTBL *dbtbl;
EPI_HUGEUINT	*nhits;
{
	FLD temp;
	BTREE *rc = (BTREE *) NULL;

	/* BTREE *rc2 = (BTREE *) NULL; */

#ifdef NEVER
	BTLOC btloc;
	size_t s;

#endif
	DDMMAPI *ddmmapi = getfld(infld, NULL);
	PROXBTREE ttbt;
	int rc1;

	(void)fname;
	(void)dbtbl;
	temp = *infld;
	if (ddmmapi == (void *) NULL
	    || ((DDMMAPI *) ddmmapi)->query == (char *) NULL)
		return rc;
#ifndef NO_NEW_RANK
	rc = openbtree(NULL, BTFSIZE, BTCSIZE, BT_FIXED | BT_UNSIGNED, O_RDWR | O_CREAT);
#else
	rc = openbtree(NULL, BTFSIZE, BTCSIZE, BT_FIXED, O_RDWR | O_CREAT);
#endif
	memset(&ttbt, 0, sizeof(ttbt));	/* for safety */
	ttbt.i = rc;
	ttbt.s = dbi->td;
	ttbt.d = dbi->del;
	ttbt.cnt = 0;
	if (dbi->lasttoken == 0)
		_openupd3dbi(dbi);
	if (TXsetmmatbl
	    (dbi->mm, ddmmapi, dbi->lasttoken,
	     (int (*)ARGS((void *, long, long, short *))) i3dbinsert, &ttbt,
	     &rc1, stimport, FOP_RELEV) == -1L)
		return rc;
#ifdef NEVER			/* Never insert deleted records.  Store rank not recid */
	rewindbtree(dbi->del);
	while (s = sizeof(btloc),
               x = btgetnext(dbi->del, &s, &btloc),
               TXrecidvalid(&x))
	{
		btdelete(rc, &btloc, s, &btloc);
	}
#endif
	*nhits = ttbt.cnt;
	rewindbtree(rc);
	return rc;
}

/******************************************************************/

typedef int (FINSFUNC)

ARGS((void *usr, RECID recid, void *auxfld, int order));
#define FINSFUNCPN      ((FINSFUNC *)NULL)

static int i3dbinsertp ARGS((PROXBTREE * bt, RECID recid, void *auxfld,
			     short *import, FINSFUNC * ins, MMAPI * mm,
			     int op, FLD * qfld));
static int
i3dbinsertp(bt, recid, auxfld, import, ins, mm, op, qfld)
PROXBTREE *bt;
RECID recid;			/* the location */
void *auxfld;			/* aux fields data (may be NULL) */
short *import;
FINSFUNC *ins;			/* insertion function */
MMAPI *mm;			/* non-NULL: must match this query */
int op;
FLD *qfld;
{
	static CONST char fn[] = "i3dbinsertp";
	int fldtype, ok, rank = -1, ret;
	FILE *fd;
	byte *v = NULL;
	size_t sz;
	RECID res;
	ft_blobi	*blobi = NULL;

#ifdef EPI_HAVE_MMAP
	char *f;
#else /* !EPI_HAVE_MMAP */
	size_t buflen, toread, nread;
	int maxrank;
#endif /* !EPI_HAVE_MMAP */

	ok = (mm == MMAPIPN);
	if (!(bt->flags & PBF_SETWEIGHTS))
	{			/* WTF - do it */
		if (import)
			rppm_setwts(bt->r, INTPN, LONGPN /*import */ );
		bt->flags |= PBF_SETWEIGHTS;
	}
	fldtype = bt->f->type & DDTYPEBITS;
	if (!TXrecidvalid(&recid))
		goto ret0;

	res = TXmygettblrow(bt->t, &recid);
	if (!TXrecidvalid(&res))
		goto ret0;		/* KNG 000217 could be deleted */
	v = getfld(bt->f, &sz);
	if (!v) goto ret0;			/* WTF correct action? */
	bt->cnt++;
        bt->r->curRecid = recid;                /* for RPPM tracing */
	switch (fldtype)
	{
	case FTN_CHAR:
		if (!ok)
		{
			if (op == FOP_MMIN)
			{
				ok = TXlikein(bt->f, qfld);
				if (!ok)
					goto ret0;
			}
			else if (getmmapi(mm, v, v + sz, SEARCHNEWBUF) !=
				 CHARPN)
				ok = 1;
			else
				goto ret0;	/* don't bother ranking */
		}
		if (op == FOP_MMIN)
			rank = 1000;
		else
			rank = rppm_rankbuf(bt->r, mm, v, v + sz, SIZE_TPN);
		DBGMSG(9, (999, NULL, "Kai says %d", rank));
		break;
	case FTN_BLOBI:
		v = TXblobiGetPayload(blobi = (ft_blobi *)v, &sz);
		if (!v) goto ret0;		/* WTF correct action? */
		if (!ok)
		{
			if (getmmapi(mm, v, v + sz, SEARCHNEWBUF) != CHARPN)
				ok = 1;
			else
				goto ret0;	/* don't bother ranking */
		}
		rank = rppm_rankbuf(bt->r, mm, v, v + sz, SIZE_TPN);
		TXblobiFreeMem(blobi);		/* save some mem */
		v = NULL;
		DBGMSG(9, (999, NULL, "Kai says %d", rank));
		break;
	case FTN_INDIRECT:
		/* WTF use freadex() */
		if (*(char *) v == '\0')	/* MAW 05-05-99 */
		{
			errno = ENOENT;
			goto ret0;
		}
		errno = 0;
		fd = fopen((char *) v, "rb");
		if (fd == (FILE *) NULL)
		{
			if (*(char *) v != '\0')
				putmsg(MERR + FOE, fn,
				       "Can't open indirect file %s: %s", v,
				       strerror(errno));
			goto ret0;
		}
		fseek(fd, 0L, SEEK_END);
		sz = ftell(fd);
		fseek(fd, 0L, SEEK_SET);
#ifdef EPI_HAVE_MMAP
		errno = 0;
		f = (char *) v;
		v =
			(unsigned char *) mmap(VOIDPN, sz,
					       (PROT_READ | PROT_WRITE),
					       MAP_PRIVATE, fileno(fd), 0);
		if ((void *) v == (void *) (-1))
		{
			putmsg(MERR + FOE, fn,
			       "Can't mmap() indirect file %s: %s", f,
			       strerror(errno));
			fclose(fd);
			goto ret0;
		}
		fclose(fd);
		if (!ok && getmmapi(mm, v, v + sz, SEARCHNEWBUF) != CHARPN)
			ok = 1;
		if (ok)
			rank = rppm_rankbuf(bt->r, mm, v, v + sz, SIZE_TPN);
		munmap((caddr_t) v, sz);
		DBGMSG(9, (999, NULL, "Kai says %d", rank));
#else /* !EPI_HAVE_MMAP */
/* Make a loop here */
/* Need to read as many buffers of size ? till we get to flen */
		buflen = TXgetblockmax();
		toread = sz;
		v = (unsigned char *) TXmalloc(TXPMBUFPN, fn, buflen);
		maxrank = -1;
		while (toread > 0)
		{
			/* WTF should really collect hits across buffers via
			 * rppm_searchbuf(), then rank at end:
			 */
			nread = fread(v, 1, buflen, fd);
			toread -= nread;
			if (!ok &&
			    getmmapi(mm, v, v + sz, SEARCHNEWBUF) != CHARPN)
				ok = 1;
			if (ok)
				rank = rppm_rankbuf(bt->r, mm, v, v + nread,
						    SIZE_TPN);
			if (rank > maxrank)
				maxrank = rank;
			/* If we're ranking on mminfo only (e.g.  LIKE), and we got a
			 * match, then stop; don't waste time getting a better rank
			 * (and don't attempt to rank a non-hit):
			 */
			if (ok && (bt->r->flags & RPF_RANKMMINFO))
				break;
		}
		rank = maxrank;
		DBGMSG(9, (999, NULL, "Kai says %d", rank));
		v = TXfree(v);
		fclose(fd);
#endif /* !EPI_HAVE_MMAP */
		break;
	default:
		putmsg(MWARN, fn, "Can't rank FTN type %d", fldtype);
		rank = 0;
	}
	ret = (ok ? ins(bt, recid, auxfld, rank) : 0);
	goto finally;

ret0:
	ret = 0;
finally:
	return(ret);
}

static int mmins ARGS((void *usr, RECID recid, void *auxfld, int rank));
static int
mmins(usr, recid, auxfld, rank)
void *usr;
RECID recid;			/* location */
int rank;			/* rank */
void *auxfld;			/* aux fields data (may be NULL) */
/* Insert callback from i3dbinsertp() for INDEX_3DB (old Metamorph)
 * index, new records.  Returns -1 on error.  `auxfld' is ignored
 * because it's only present in INDEX_MM and INDEX_FULL indexes.
 */
{
	PROXBTREE *bt = (PROXBTREE *) usr;
	int perc;
	EPI_OFF_T t_rank;

	(void)auxfld;
	if (rank > bt->threshold)
	{
		bt->cntg++;
		t_rank = (EPI_OFF_T) (-rank);
		btinsert(bt->i, &recid, sizeof(t_rank), &t_rank);
		if (bt->maxhits > 0 && bt->cntg > bt->maxhits)
			return (-1);
		if (bt->minhits > 0 && bt->cntg > bt->minhits)
		{
			perc = (int)((bt->cntg * (EPI_HUGEUINT)100) / bt->cnt);
			if (perc > bt->maxperc)
				return (-1);
		}
	}
	return (0);
}

/******************************************************************/

BTREE *
setp3dbi2(dbi, infld, fname, dbtbl, nhits)
A3DBI *dbi;
FLD *infld;
char *fname;
DBTBL *dbtbl;
EPI_HUGEUINT	*nhits;
{
	static CONST char	fn[] = "setp3dbi2";
	FLD temp;
	BTREE *rc = (BTREE *) NULL;
	BTREE *rc2;
	BTLOC btloc, x;
	size_t s;
	MMAPI *mmapi;
	MMQL *ql;
	DDMMAPI *ddmmapi = getfld(infld, NULL);
	PROXBTREE *ttbt;
	char **wordlist;
	TXPMTYPE *typelist = NULL;

#ifdef DEBUG
	char *lwl;
	int i;
#endif

	temp = *infld;
	if (ddmmapi == (void *) NULL
	    || ((DDMMAPI *) ddmmapi)->query == (char *) NULL)
		return rc;
	mmapi = ((DDMMAPI *) ddmmapi)->mmapi;
	wordlist = getwordlist(mmapi, &typelist);
#ifdef DEBUG
	for (i = 0; lwl = wordlist[i], *lwl; i++)
	{
		DBGMSG(9, (999, "prox", "%s", lwl));
	}
#endif
	ttbt = (PROXBTREE *) TXcalloc(TXPMBUFPN, fn, 1, sizeof(PROXBTREE));
	if ((ql = mmrip(mmapi, 0)) == MMQLPN)
	{
		ttbt = TXfree(ttbt);
		return (rc);
	}			/* wtf ---v  correct FOP? */
	ttbt->r = openrppm(mmapi, ql, FOP_PROXIM, FDBIPN, 0);
	ttbt->i = rc;
	ttbt->s = dbi->td;
	ttbt->flags = (PBF)0;
	ttbt->cnt = 0;
	ttbt->t = dbtbl;
	ttbt->f = dbnametofld(dbtbl, fname);
	/* WTF - These need to be tweaked for parameters */
	ttbt->maxperc = 100;	/* 100 - allow all, 0 allow only minhits */
	ttbt->threshold = -1;	/* Rank must be greater than this to count */
	/* JMT - Changed to 0 961004 so not found won't show up */
	ttbt->cnt = 0;
	ttbt->cntg = 0;
	ttbt->minhits = 0;	/* Allow at least this many */
	ttbt->maxhits = 0;	/* Stop at this many */
	if (!ttbt->f)
	{
		closerppm(ttbt->r);
		return closebtree(rc);
	}
	if (dbi->lasttoken == 0)
		_openupd3dbi(dbi);

	if (TXapicpGetLikepAllMatch())
	{
		int nopost;

		rc2 = TXset3dbi(dbi, infld, fname, dbtbl, 1, nhits,
				&nopost, stimport, FOP_NMM);
	}
	else
	{
		EPI_OFF_T zx = 0;

		rc2 = setr3dbi(dbi, infld, fname, dbtbl, nhits);
		if (rc2 != BTREEPN)	/* KNG 980326 */
		{
			rewindbtree(dbi->newrec);
			while (s = sizeof(btloc),
                               x = btgetnext(dbi->newrec, &s, &btloc, NULL),
			       TXrecidvalid(&x))
			{
				btinsert(rc2, &btloc, sizeof(zx), &zx);
			}
		}
	}
	if (stimport != SHORTPN)
		rppm_setwts(ttbt->r, INTPN, LONGPN /*stimport */ );

	ttbt->threshold = 0;	/* Rank must be greater than this to count */

	ddmmapi->bt = ttbt;
	/* next guy to use this probably doesn't have index: */
	rppm_setflags(ttbt->r, RPF_LOGICHECK, 1);
	ddmmapi->wordlist = wordlist;
/*  Pattern matchers need this!!!
   if(wordlist)
   _freelst(wordlist);
 */
	if (typelist)
		typelist = TXfree(typelist);
	if (rc2 != BTREEPN)
		rewindbtree(rc2);
	return rc2;
}

/******************************************************************/

PROXBTREE *
TXcloseproxbtree(bt)
PROXBTREE *bt;
{
	if (bt)
	{
		bt->r = closerppm(bt->r);
		bt->fh = closefheap(bt->fh);
		if (bt->xxx)
		{
			EXTRA *exs = bt->xxx;

			if (!exs->iscached)
			{
				closeextra(exs, 0);
			}
			else
			{
				/* This is an indexcache EXTRA object;
				 * keep it around for future use, but
				 * let TXiextraForIndexCache() know we
				 * are done with it so it is safe to
				 * close:
				 */
				exs->iscached = 2;	/* I'm done with it */
			}
			bt->xxx = NULL;
		}
		if (bt->fldOp != FLDOPPN) foclose(bt->fldOp);
		if (bt->cnvFld != FLDPN) closefld(bt->cnvFld);
		bt = TXfree(bt);
	}
	return PROXBTREEPN;
}

/******************************************************************/

BTREE *
setp3dbi(dbi, infld, fname, dbtbl, nhits)
A3DBI *dbi;
FLD *infld;
char *fname;
DBTBL *dbtbl;
EPI_HUGEUINT	*nhits;
{
	FLD temp;
	BTREE *rc = (BTREE *) NULL;
	BTREE *rc2;
	BTLOC btloc, x;
	size_t s;
	MMAPI *mmapi;
	MMQL *ql;
	DDMMAPI *ddmmapi = getfld(infld, NULL);
	PROXBTREE ttbt;
	char **wordlist;
	TXPMTYPE *typelist = NULL;

#ifdef DEBUG
	char *lwl;

#endif
#ifdef _WIN32
	DWORD stp, etp;

#else
	struct timeval stp, etp;	/* Start Time, End Time */
	struct timezone stz, etz;	/* Start Timezone, End Timezone */

#endif
	int i;

	temp = *infld;
	if (ddmmapi == (void *) NULL
	    || ((DDMMAPI *) ddmmapi)->query == (char *) NULL)
		return rc;
	mmapi = ((DDMMAPI *) ddmmapi)->mmapi;
#ifndef NO_NEW_RANK
	rc = openbtree(NULL, BTFSIZE, BTCSIZE, BT_FIXED | BT_UNSIGNED, O_RDWR | O_CREAT);
#else
	rc = openbtree(NULL, BTFSIZE, BTCSIZE, BT_FIXED, O_RDWR | O_CREAT);
#endif
	wordlist = getwordlist(mmapi, &typelist);
#ifdef DEBUG
	for (i = 0; lwl = wordlist[i], *lwl; i++)
	{
		DBGMSG(9, (999, "prox", "%s", lwl));
	}
#endif
	memset(&ttbt, 0, sizeof(ttbt));	/* for safety */
	if ((ql = mmrip(mmapi, 0)) == MMQLPN)
	{
		return (rc);
	}			/* wtf ---v  correct FOP? */
	ttbt.r = openrppm(mmapi, ql, FOP_PROXIM, FDBIPN, 0);
	ttbt.i = rc;
	ttbt.s = dbi->td;
	ttbt.flags = (PBF)0;
	ttbt.cnt = 0;
	ttbt.t = dbtbl;
	ttbt.f = dbnametofld(dbtbl, fname);
	/* WTF - These need to be tweaked for parameters */
	ttbt.maxperc = 100;	/* 100 - allow all, 0 allow only minhits */
	ttbt.threshold = -1;	/* Rank must be greater than this to count */
	/* JMT - Changed to 0 961004 so not found won't show up */
	ttbt.cnt = 0;
	ttbt.cntg = 0;
	ttbt.minhits = 0;	/* Allow at least this many */
	ttbt.maxhits = 0;	/* Stop at this many */
	if (!ttbt.f)
	{
		closerppm(ttbt.r);
		return closebtree(rc);
	}
	if (dbi->lasttoken == 0)
		_openupd3dbi(dbi);

	if (TXlikeptime)
#ifdef _WIN32
		stp = GetTickCount();
#else
		gettimeofday(&stp, &stz);
#endif
	if (TXapicpGetLikepAllMatch())
	{
		int nopost;

		rc2 = TXset3dbi(dbi, infld, fname, dbtbl, 1, nhits,
				&nopost, stimport, FOP_NMM);
	}
	else
		rc2 = setr3dbi(dbi, infld, fname, dbtbl, nhits);
	for (i = 0; !TXnlikephits || i < TXnlikephits; i++)
	{
		s = sizeof(btloc);	/* For safety */
		x = btgetnext(rc2, &s, &btloc, NULL);
		if (!TXrecidvalid(&x))
			break;
		i3dbinsertp(&ttbt, x, NULL, stimport, mmins, MMAPIPN,
			    FOP_PROXIM, NULL);
		if (TXlikeptime)
		{
			long diff;

#ifdef _WIN32
			etp = GetTickCount();
			diff = (etp - stp) * 1000;	/* WTF: Should I worry about Rollover */
#else
			gettimeofday(&etp, &etz);
			if (stp.tv_sec == etp.tv_sec)
			{
				diff = etp.tv_usec - stp.tv_usec;
			}
			else
			{
				diff = etp.tv_sec - stp.tv_sec;
				diff = diff * 1000000;
				diff = diff - stp.tv_usec;
				diff = diff + etp.tv_usec;
			}
#endif
			if (diff >= TXlikeptime)
				break;
		}
	}
	closebtree(rc2);
	ttbt.threshold = 0;	/* Rank must be greater than this to count */

#ifndef OLD_LIKEP
/*
   Should add new records in here, and then loop through and pull the top
   TXnlikephits off.
 */
	if (!TXapicpGetLikepAllMatch())
	{
		rewindbtree(dbi->newrec);
		rppm_setflags(ttbt.r, RPF_LOGICHECK, 1);
		while (s = sizeof(btloc),
                       x = btgetnext(dbi->newrec, &s, &btloc, NULL),
		       TXrecidvalid(&x))
		{
			i3dbinsertp(&ttbt, x, NULL, stimport, mmins, MMAPIPN,
				    FOP_PROXIM, NULL);
		}
	}
	rc2 = rc;
#ifndef NO_NEW_RANK
	rc = openbtree(NULL, BTFSIZE, BTCSIZE, BT_FIXED | BT_UNSIGNED, O_RDWR | O_CREAT);
#else
	rc = openbtree(NULL, BTFSIZE, BTCSIZE, BT_FIXED, O_RDWR | O_CREAT);
#endif
	rewindbtree(rc2);
	for (i = 0; !TXnlikephits || i < TXnlikephits; i++)
	{
		s = sizeof(btloc);	/* For safety */
		x = btgetnext(rc2, &s, &btloc, NULL);
		if (!TXrecidvalid(&x))
			break;
		btspinsert(rc, &x, s, &btloc, 90);
	}
	closebtree(rc2);

#endif
#ifdef NEVER
	if (ttbt.cnt < TXblikephits)
		*nhits = ttbt.cnt;
#endif
	closerppm(ttbt.r);
	if (wordlist)
		_freelst(wordlist);
	if (typelist)
		typelist = TXfree(typelist);
	rewindbtree(rc);
	return rc;
}

/* ----------------------------------------------------------------------- */

static BTREE *
TXcreateResultsLinearBtreeAndKeyrec(EXTRA *exs, DD *newDd,
			      size_t computedFromAuxFldsSz, size_t ranksize,
			      int pageSize, int *orderseq, size_t orderseqLen)
/* Helper function for TXsetupauxorder().  Creates a BT_LINEAR B-tree
 * and KEYREC for LIKE...  results.  Will own `newDd' and `orderseq',
 * even on error.  Called when there is an ORDER BY all-aux or SELECT all-aux
 * or indexcache, and no unhandled AND/OR.
 * Returns B-tree, or NULL on error.
 */
{
	BTREE	*rc = NULL;

	newDd->ivar = newDd->n + 1;
	exs->keysz = exs->extrasz + computedFromAuxFldsSz + ranksize;
	if (!exs->keyrec)
		exs->keyrec = keyrecopen(NULL, exs->extrasz, ranksize,
					 computedFromAuxFldsSz, 0, 10000,
					 newDd);
	if (!exs->keyrec) goto bail1;

	rc = openbtree(NULL, pageSize, BTCSIZE, BT_LINEAR,
		       (O_RDWR | O_CREAT));
	if (!rc) goto bail1;

	btreesetdd(rc, newDd);
	exs->fc = TXopenfldcmp(rc, TXOPENFLDCMP_CREATE_FLDOP);
	if (!exs->fc)
	{
	bail1:
		exs->keyrec = keyrecclose(exs->keyrec);
		rc = closebtree(rc);
		newDd = closedd(newDd);
		orderseq = TXfree(orderseq);
		return(rc);
	}
	rc->usr = exs->fc;
	btsetcmp(rc, (btcmptype)fldcmp);
	/* `keyrec->cmporder' array gives order (actual not user
	 * indexes) in which keyrecfldcmp() (during keyrecsort())
	 * should compare `exs->fc' fields:
	 */
	exs->keyrec->cmporder = orderseq;
	exs->keyrec->auxalloced = orderseqLen;
	orderseq = NULL;
	orderseqLen = 0;

	if (exs->keyrec->auxalloced == 1 &&	/* sort just 1 field and */
	    exs->keyrec->cmporder[0] == 0 &&	/*   it is first field and */
	    newDd->n >= 1 &&
	    newDd->fd[0].type == FTN_HANDLE)	/*   it is recid/off_t and */
		/* Optimization: can stay with faster keyreccmp()
		 * (or its negation TXkeyrecSetCmpRankDescRecid());
		 * equivalent to keyrecfldcmp() in this case
		 */
	{
		if (newDd->fd[0].order & OF_DESCENDING)
			TXkeyrecSetCmpRankDescRecid(exs->keyrec);
		/* else stay with keyreccmp() */
	}
	else
	{
		/* Use keyrecfldcmp(): if sorting, will do so via DD
		 * field(s), not necessarily just rank:
		 */
		keyrecsetfldcmp(exs->keyrec, exs->fc);
	}

	/* Delay keyrecsetmaxsortrows() until i3dbfinit(),
	 * when we know whether a post-proc is needed
	 */

	newDd = closedd(newDd);
	return(rc);
}

/* ----------------------------------------------------------------------- */

BTREE *
TXsetupauxorder(EXTRA * exs, DD * auxdd, TBSPEC * tbspec, DBTBL * dbtbl)
/* Called for LIKE[RP3] when there is Metamorph compound/aux data;
 * tries to use the latter for an ORDER BY (or SELECT?) if present.
 */
{
	static CONST char	fn[] = "TXsetupauxorder";
	PROJ *proj = tbspec->proj;
	int gotAllNeededFlds, i;
	BTREE *rc = NULL;
	DD *auxDdCnv = NULL;
	DD *newDd = NULL;
	size_t ranksize = 0;
	/* byte size of `newDd' fields *computed* (as opposed to *actual*)
	 * aux fields:
	 */
	size_t	computedFromAuxFldsSz = 0;
	int *orderseq = NULL;	/* field sort order array for `exs->keyrec' */
	FLDOP	*fo = NULL;

	exs->nosort = 0;
	if (proj)				/* there is an ORDER BY (?) */
	{		/* NOTE: `ORDER BY N' does not give a `proj' here */
		gotAllNeededFlds = 1;
		auxDdCnv = convertdd(auxdd, 0);
		if(NULL == auxDdCnv) goto err;

		/* - - - - - - - - - - - - - - - - - - - - - - - - - -
                 * Create `newDd' as $rank + `auxDdCnv' fields, with no
                 * ordering set.  Leave room for another `proj->n'
                 * possible computed fields:
		 */
		newDd = opennewdd(auxDdCnv->n + proj->n + 2);
		if(NULL == newDd)
		{
			auxDdCnv = closedd(auxDdCnv);
			goto err;
		}
		newDd->tbltype = auxDdCnv->tbltype;
		/* wtf should be TXrankColumnTypeStr (i.e. int) not off_t,
		 * but keyrec etc. stuff downstream of this may be assuming
		 * off_t?  KNG 20130604
		 */
		putdd(newDd, (char *)TXrankColumnName, "off_t", 1, 0);
		ranksize = newDd->fd[0].size;
		while (((long)ranksize % TX_ALIGN_BYTES) != 0L)
			ranksize ++;
		newDd->fd[0].order = OF_DONT_CARE;
		orderseq = TXcalloc(TXPMBUFPN, fn, proj->n+1, sizeof(int));
		for (i = 0; i < auxDdCnv->n; i++)
		{
			auxDdCnv->fd[i].order = OF_DONT_CARE;
			newDd->fd[i+1] = auxDdCnv->fd[i];
			newDd->fd[i+1].num = auxDdCnv->fd[i].num + 1;
			newDd->fd[i+1].pos = auxDdCnv->fd[i].pos + ranksize;
			newDd->n++;
			newDd->varpos = newDd->fd[i+1].pos +
				newDd->fd[i+1].size;
		}
		newDd->ivar = newDd->n;

		/* - - - - - - - - - - - - - - - - - - - - - - - - - -
                 * Now set `newDd' ordering per `proj', and add `proj'
		 * fields that are missing (e.g. computed-from-aux).
		 * Note (with `gotAllNeededFlds = 0') if any cannot be
		 * handled solely with aux fields:
		 */
		for (i = 0; i < proj->n; i++)
		{
			int sz;
			TXOF	order;
			int nn;
			char *fname, *predType;
			size_t	fnameLen;

			fname = TXdisppred(proj->preds[i], 0, 0, 1000);
			fnameLen = strlen(fname);
#ifdef TX_USE_ORDERING_SPEC_NODE
			/* ASC/DESC/IGNCASE indicators in top PRED now: */
			order = proj->preds[i]->orderFlags;
#else /* !TX_USE_ORDERING_SPEC_NODE */
			order = (TXOF)0;
			if (fnameLen > 0 && fname[fnameLen - 1] == '^')
			{			/* IGNCASE */
				fname[--fnameLen] = '\0';
				order |= OF_IGN_CASE;
			}
			if (fnameLen > 0 && fname[fnameLen - 1] == '-')
			{			/* DESC */
				fname[--fnameLen] = '\0';
				order |= OF_DESCENDING;
			}
#endif /* !TX_USE_ORDERING_SPEC_NODE */
			if (!fo && !(fo = dbgetfo())) goto err;
			predType = predtype(proj->preds[i], exs->dbtbl, fo,
					    &sz, &nn);
			if(!predType)
			{	/* `fname' not computable from aux (+$rank) */
			notComputableFromAux:
				/* note: Webinator scripts look for this: */
				putmsg(MINFO, NULL,
				 "Can't handle ORDER BY with compound index");
				gotAllNeededFlds = 0;
			}
			/* Bug 7207: predtype() does not check if args of
			 * functions exist in the (aux) table, so check:
			 */
			else if (!TXpredicateIsResolvableWithAltTable(proj->preds[i], dbtbl, exs->dbtbl, TXbool_True))
			{
				goto notComputableFromAux;
			}
			else if(ddsetordern(newDd, fname, order) == -1)
			{
				/* `fname' is not in `newDd'.  Probably
				 * expression based on aux [+$rank] fields:
				 */
				if(NULL == exs->computedorderby)
				{
				  int home;

				  home = putdd(newDd, fname, predType, sz, nn);
				  if (home < 0)
				  {
				  cantAdd:
					  putmsg(MWARN, __FUNCTION__,
				  "Cannot add `%s' to DD or set its ordering",
						 fname);
					  gotAllNeededFlds = 0;
					  fname = TXfree(fname);
					  continue;
				  }
				  home--;	/* convert to index */
				  /* Bug 4425 comment #8: set order by index,
				   * not name: latter will fail if too large:
				   */
				  if (!TXddSetOrderFlagsByIndex(newDd, home,
								order))
					  goto cantAdd;
				  computedFromAuxFldsSz += newDd->fd[home].size;
				  if (newDd->fd[home].elsz == 1)
				  {
					computedFromAuxFldsSz ++;
					if(dbtbl->ddic->optimizations[OPTIMIZE_AUXDATALEN])
					{
						newDd->fd[home].size++;
						computedFromAuxFldsSz ++;
					}
				  }
				  if (computedFromAuxFldsSz % TX_ALIGN_BYTES)
				  {
					int d = TX_ALIGN_BYTES -
				     (computedFromAuxFldsSz % TX_ALIGN_BYTES);
					if(newDd->fd[home].elsz == 1)
					{
						newDd->fd[home].size+=d;
					}
					computedFromAuxFldsSz += d;
				  }
				  orderseq[i] = home;
				  exs->computedorderby = proj->preds[i];
				}
				else
				{
				  /* note: Webinator scripts look for this: */
				  putmsg(MINFO, NULL,
				  "Can't handle ORDER BY with compound index");
				  gotAllNeededFlds = 0;
				}
			}
			else		/* `fname' is already in `newDd' */
			{
				orderseq[i] = ddfindname(newDd, fname);
			}
			if (fname)
				fname = TXfree(fname);
		}
		auxDdCnv = closedd(auxDdCnv);

		/* - - - - - - - - - - - - - - - - - - - - - - - - - -
		 * If there was an ORDER BY predicate that cannot be
		 * handled by aux [+$rank], bail.  Note that we might
		 * also "bail" later in i3dbfinit(), if we need MM post:
		 */
		if (!gotAllNeededFlds) goto err;

		/* - - - - - - - - - - - - - - - - - - - - - - - - - - */
		rc = TXcreateResultsLinearBtreeAndKeyrec(exs, newDd,
					 computedFromAuxFldsSz, ranksize,
					 BT_REALMAXPGSZ, orderseq, proj->n);
		newDd = NULL;			/* regardless of error */
		orderseq = NULL;		/* "" */
		if (!rc) goto err;
		/* Setting `dbtbl->order' lets TXdotree() ORDER_OP
		 * case know that we did the ORDER BY already.  Note
		 * that it might get removed in i3dbfinit() if we need
		 * a post-process:
		 */
		dbtbl->order = closeproj(dbtbl->order);	/* remove old if any */
		dbtbl->order = dupproj(proj);
	}					/* end have PROJ */
	else if (tbspec->pflist)		/* SELECT fields from table? */
	{	/* i.e. fields needed by parent PRED(s)?  But "native" fields,
		 * not expressions; e.g. for `SELECT foo + 5*bar' we get
		 * `foo' and `bar' not `foo + 5*bar' in `tbspec->pflist'
		 */
		int numNeededFields;

		/* - 1: `tbspec->pflist' is empty-string-terminated: */
		numNeededFields = tbspec->pflist->cnt - 1;
		if(numNeededFields <= 0)
			goto nokeyrec;
		gotAllNeededFlds = 1;
		auxDdCnv = convertdd(auxdd, 0);
		if(NULL == auxDdCnv) goto err;

		/* - - - - - - - - - - - - - - - - - - - - - - - - - -
		 * Create `newDd' as $rank + `auxDdCnv' fields, with
		 * no ordering set:
		 */
		newDd = opennewdd(auxDdCnv->n + numNeededFields + 2);
		if(NULL == newDd)
		{
			auxDdCnv = closedd(auxDdCnv);
			goto err;
		}
		newDd->tbltype = auxDdCnv->tbltype;
		/* wtf should be TXrankColumnTypeStr (i.e. int) not off_t,
		 * but keyrec etc. stuff downstream of this may be assuming
		 * off_t?  KNG 20130604
		 */
		putdd(newDd, (char *)TXrankColumnName, "off_t", 1, 0);
		ranksize = newDd->fd[0].size;
		while (((long)ranksize % TX_ALIGN_BYTES) != 0L)
			ranksize ++;
		newDd->fd[0].order = OF_DONT_CARE;
		orderseq = TXcalloc(TXPMBUFPN, fn, numNeededFields + 1,
				    sizeof(int));
		for (i = 0; i < auxDdCnv->n; i++)
		{
			auxDdCnv->fd[i].order = OF_DONT_CARE;
			newDd->fd[i+1] = auxDdCnv->fd[i];
			newDd->fd[i+1].num = auxDdCnv->fd[i].num + 1;
			newDd->fd[i+1].pos = auxDdCnv->fd[i].pos + ranksize;
			newDd->n++;
			newDd->varpos = newDd->fd[i+1].pos +
				newDd->fd[i+1].size;
		}
		newDd->ivar = newDd->n;

		/* - - - - - - - - - - - - - - - - - - - - - - - - - -
		 * See if all needed fields are computable from aux
		 * fields [+$rank]; bail if not possible:
		 */
		for (i = 0; i < numNeededFields; i++)
		{
			int sz;
			int nn;
			char *fname, *predType = NULL;
			PRED tpred;

			fname = tbspec->pflist->s[i];
			memset(&tpred, 0, sizeof(PRED));
			tpred.op = 0;
			tpred.lt = NAME_OP;
			tpred.lat = 0;
			tpred.left = fname;
			if (!fo && !(fo = dbgetfo())) goto err;
			if (dbnametofld(exs->dbtbl, fname) ||
			    /* KNG 20171017 $rank also ok? otherwise
			     * we hit .tbl for `SELECT $rank LIKE' w/aux idx.
			     * Why can't we dispense with dbnametofld()
			     * altogether and just suppress predtype() putmsg?
			     */
			    strcmp(fname, TXrankColumnName) == 0)
				predType = predtype(&tpred, exs->dbtbl, fo,
						    &sz, &nn);
			if (!predType)
			{
				/* `fname' not computable from aux [+$rank] */
				gotAllNeededFlds = 0;
				break;
			}
			/* KNG wtf should set `orderseq[i]' too just
			 * in case?  ignored for now since there will
			 * be no keyrecsort() later (no ORDER BY)
			 */
		}
		auxDdCnv = closedd(auxDdCnv);
		if (!gotAllNeededFlds)
		{
			/* Cannot compute all needed (SELECT?) fields
			 * from aux data [+$rank]; bail:
			 */
			newDd = closedd(newDd);	/* wantcache may reopen it */
			orderseq = TXfree(orderseq);	/* "" */
			if(dbtbl->ddic->options[DDIC_OPTIONS_INDEX_CACHE])
				goto wantcache;
			goto err;
		}
		/* Have all needed fields.  Note that
		 * TXtup_project_setup() will do a similar check
		 * against our results B-tree's DD, when trying to set
		 * DBTBL.DBIDX.indexdataonly.
		 */

		/* - - - - - - - - - - - - - - - - - - - - - - - - - - */
		rc = TXcreateResultsLinearBtreeAndKeyrec(exs, newDd,
					 computedFromAuxFldsSz, ranksize,
					 BTFSIZE, orderseq, numNeededFields);
		newDd = NULL;			/* regardless of error */
		orderseq = NULL;
		if (!rc) goto err;
		exs->keyrec->sorted = 1;
		exs->nosort = 1;		/* no ORDER BY, so no sort */
	}					/* end SELECT flds from tbl */
	else if(dbtbl->ddic->options[DDIC_OPTIONS_INDEX_CACHE])
	{
wantcache:
		auxDdCnv = convertdd(auxdd, 0);
		if(NULL == auxDdCnv) goto err;

		/* - - - - - - - - - - - - - - - - - - - - - - - - - -
		 * Create `newDd' as $rank + `auxDdCnv' fields, with
		 * no ordering set:
		 */
		newDd = opennewdd(auxDdCnv->n + 2);
		if(NULL == newDd)
		{
			auxDdCnv = closedd(auxDdCnv);
			goto err;
		}
		newDd->tbltype = auxDdCnv->tbltype;
		/* wtf should be TXrankColumnTypeStr (i.e. int) not off_t,
		 * but keyrec etc. stuff downstream of this may be assuming
		 * off_t?  KNG 20130604
		 */
		putdd(newDd, (char *)TXrankColumnName, "off_t", 1, 0);
		ranksize = newDd->fd[0].size;
		while (((long)ranksize % TX_ALIGN_BYTES) != 0L)
			ranksize ++;
		newDd->fd[0].order = OF_DONT_CARE;
		orderseq = TXcalloc(TXPMBUFPN, fn, 1, sizeof(int));
		for (i = 0; i < auxDdCnv->n; i++)
		{
			auxDdCnv->fd[i].order = OF_DONT_CARE;
			newDd->fd[i+1] = auxDdCnv->fd[i];
			newDd->fd[i+1].num = auxDdCnv->fd[i].num + 1;
			newDd->fd[i+1].pos = auxDdCnv->fd[i].pos + ranksize;
			newDd->n++;
			newDd->varpos = newDd->fd[i+1].pos +
				newDd->fd[i+1].size;
			/* KNG wtf should set `orderseq[i]' too (fix
			 * alloc and ..AndKeyRec arg though) just in
			 * case?  ignored for now since there will be
			 * no keyrecsort() later (no ORDER BY)
			 */
		}
		newDd->ivar = newDd->n;
		auxDdCnv = closedd(auxDdCnv);

		/* - - - - - - - - - - - - - - - - - - - - - - - - - - */
		rc = TXcreateResultsLinearBtreeAndKeyrec(exs, newDd,
					     computedFromAuxFldsSz, ranksize,
					     BTFSIZE, orderseq, 1);
		newDd = NULL;			/* regardless of error */
		orderseq = NULL;		/* "" */
		if (!rc) goto err;
		exs->keyrec->sorted = 1;
		exs->nosort = 1;		/* no ORDER BY, so no sort */
	}					/* end indexcache */
	else
	{
		/* No ORDER BY, no SELECT field list (or is `*'),
		 * no indexcache.
		 */
nokeyrec:
		exs->fc = TXopenfldcmp(NULL, TXOPENFLDCMP_CREATE_FLDOP);
		if (exs->fc)
		{
			rc = openbtree(NULL, BTFSIZE,
				       BTCSIZE, (BT_FIXED | BT_UNSIGNED),
				       (O_RDWR | O_CREAT));
		}
	}
	goto finally;

err:
	rc = closebtree(rc);
finally:
	fo = foclose(fo);
	orderseq = TXfree(orderseq);
	newDd = closedd(newDd);
	return rc;
}

/******************************************************************/

int
setf3dbi(DBI_SEARCH *dbisearch)
/* Intermediate-level wrapper for LIKE[3,R,P,IN] (fdbi_get()).
 */
{
	static CONST char Fn[] = "setf3dbi";
	TXPMBUF	*pmbuf;
	FLD temp, *tf;
	BTREE *rc = BTREEPN, *and;
	BTREE *prevResultsTree = BTREEPN;
	BTLOC btloc, rankloc;
	int i, ret, flags;
	size_t sz, auxbufsz = 0;
	EPI_HUGEUINT	numhits = 0, indexthresh, nandrecs;
	int off;
	MMAPI *mm;
	MMQL *mq;
	DDMMAPI *ddmmapi;
	PROXBTREE *pbt = PROXBTREEPN;
	EXTRA *exs = NULL;
	FDBI *fi;
	A3DBI *dbi;
	void *auxbuf = NULL;
	TXCFF tmpsm;
	BTREE *didrecs = BTREEPN;
	int nbad = 0;
	PROJ *savedorderby = NULL;
	int btpagesize;
	TXRESOURCESTATS	tstart, tnow;
	int dotiming = 0;
	IINDEX	*iindex;
	DBTBL *dbtbl;
	int op;			/* FOP_... operation (LIKEP, LIKE, etc.) */
	FLD *infld;
	char *fname;
	TBSPEC *tbspec;		/* Prior Index to AND with (if non-NULL) */
	int	haveOrderByNotRankDesc;

	if(!dbisearch)
		return -1;
	iindex = dbisearch->iindex;
	dbtbl = dbisearch->dbtbl;
	pmbuf = (dbtbl && dbtbl->ddic ? dbtbl->ddic->pmbuf : TXPMBUFPN);
	op = dbisearch->op;
	infld = dbisearch->infld;
	fname = dbisearch->fname;
	tbspec = dbisearch->tbspec;

	/* Pull off and save any existing ordering that has already
	 * been done; we may add our own, and/or restore this later:
	 */
	savedorderby = dbtbl->order;
	dbtbl->order = NULL;

	fi = (FDBI *) dbisearch->fip;
	dbi = fdbi_getdbi(fi);
	if (op == FOP_MMIN && dbi->ct == NULL)
		goto err;
	dbisearch->nhits = 0;
	dotiming = dbtbl->ddic->messages[MESSAGES_TIME_FDBI];
	if (dotiming)
	{
		TXgetResourceStats(pmbuf, TXRUSAGE_SELF, &tstart);
	}
	if (!(pbt = (PROXBTREE *)TXcalloc(pmbuf, Fn, 1, sizeof(PROXBTREE))))
		goto err;
	ddmmapi = getfld(infld, NULL);
	temp = *infld;
	if (ddmmapi == NULL || ddmmapi->query == CHARPN)
		goto err;

	pbt->newlist = dbi->newrec;
	pbt->d = BTREEPN;
	pbt->threshold = 0;	/* rank must be > this */
	pbt->t = dbtbl;
	pbt->f = dbnametofld(dbtbl, fname);
	if (pbt->f == FLDPN)
		goto err;
	pbt->cnt = 0;
	pbt->cntg = 0;
	pbt->minhits = 0;
	pbt->fh = FHEAPPN;

	if (dbi->auxsz > 0)			/* have aux fields */
	{
		if (dbtbl->ddic->options[DDIC_OPTIONS_INDEX_CACHE])
		{
			exs = TXiextraForIndexCache(tbspec, dbi->auxdd,
						  dbi->auxsz, &dbisearch->inv,
						    dbtbl, ddmmapi, fname);
			if (!exs)
				goto err;
			rc = exs->btree;
			exs->btree = NULL;
			pbt->xxx = exs;
		}
		else				/* not caching results */
		{
			pred_rmalts(tbspec->pred);
			exs = (EXTRA *) TXcalloc(pmbuf, Fn, 1, sizeof(EXTRA));
			if (!exs) goto err;
			exs->dbtbl = (DBTBL *) TXcalloc(pmbuf, Fn, 1,
							sizeof(DBTBL));
			exs->dbtbl->tbl = createtbl(dbi->auxdd, NULL);
			exs->dbtbl->type = 'T';
			exs->dbtbl->lname = TXstrcatN(pmbuf, __FUNCTION__,
						      (dbi->name ? TXbasename(dbi->name) : "Unnamed Metamorph index"), " Aux flds RAM table", NULL);
			exs->dbtbl->ddic = dbtbl->ddic;
			exs->goodpred = TXmakepredvalid(tbspec->pred,
							exs->dbtbl, 0, 0, 0);
			pred_sethandled(exs->goodpred);
			if (pred_allhandled(tbspec->pred))
			{
				/* WTF ??? Don't need invert
				   if doing complete query */
				if (TXtraceIndexBits & 0x200000)
				{		/* temp Bug 6796 tracing */
				    char	*predStr;

				    predStr = TXdisppred(tbspec->pred, 0, 0,
							 1024);
				    txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
					   "PRED `%s' all handled; setting dbisearch->inv/ddmmapi->lonely to 0/1: was %d/%d",
					   predStr, (int)dbisearch->inv,
					   (int)ddmmapi->lonely);
				    predStr = TXfree(predStr);
				}
				dbisearch->inv = 0;
				ddmmapi->lonely = 1;
			}
			exs->extrasz = dbi->auxsz;
			exs->keysz = dbi->auxsz;
			pbt->xxx = exs;
			if (dbisearch->inv ||	/* e.g. unhandled AND/OR */
			    !(rc = TXsetupauxorder(exs, dbi->auxdd, tbspec,
						   dbtbl)))
			{
				exs->fc = TXopenfldcmp(NULL,
						   TXOPENFLDCMP_CREATE_FLDOP);
				if (!exs->fc)
					goto err;
				rc = openbtree(NULL, BTFSIZE, BTCSIZE,
					       BT_FIXED | BT_UNSIGNED,
					       O_RDWR | O_CREAT);
			}
		}
		exs->lonely = ddmmapi->lonely;
		/* `maxrows' is maxrows/max_rows (default 0), not likeprows: */
		exs->useMaxRows = dbtbl->ddic->options[DDIC_OPTIONS_MAX_ROWS];
	}
	else					/* no aux fields */
	{
#ifndef NO_NEW_RANK
		rc = openbtree(NULL, BTFSIZE, BTCSIZE,
			       (BT_FIXED | BT_UNSIGNED), (O_RDWR | O_CREAT));
#else /* NO_NEW_RANK */
		dead code;
		rc = openbtree(NULL, BTFSIZE, BTCSIZE, BT_FIXED,
			       (O_RDWR | O_CREAT));
#endif /* NO_NEW_RANK */
		pbt->xxx = NULL;
	}
	pbt->i = rc;
	if (dbisearch->inv)			/* e.g. unhandled AND/OR */
		pbt->flags |= PBF_INVTREE;
	mm = ddmmapi->mmapi;

	/* Do we have an ORDER BY that is not `ORDER BY $rank desc'?
	 * That will turn off rank heap.
	 * WTF this does not detect SELECT $rank ... ORDER BY 1 desc:
	 */
	haveOrderByNotRankDesc =
		(tbspec->proj && !TXprojIsRankDescOnly(tbspec->proj));

	if (exs)
	{
		/* See if we have an ORDER BY that contains any
		 * reference to $rank.  WTF does not see ORDER BY num:
		 */
		exs->haveOrderByContainingRank =
			(tbspec->proj && TXprojHasRank(tbspec->proj));
		/* TXsettablepred() can tell us we have ORDER BY num,
		 * but not its schema, because it hasn't been
		 * established yet:
		 */
		exs->haveOrderByNum = tbspec->haveOrderByNum;
	}
		
	switch (op)
	{
	case FOP_PROXIM:	/* LIKEP */
	case FOP_RELEV:		/* LIKER */
		iindex->nrank = 1;		/* have rank */
		/* wtf this may be wrong for `ORDER BY num', since we
		 * do not get a `tbspec->proj' for those?  could set
		 * to TXprojIsRankDescOnly() or !tbspec->haveOrderByNum
		 * but that changes lots of tests' results:
		 */
		iindex->orank = (TXApp->legacyVersion7OrderByRank ? 1 :
				 !haveOrderByNotRankDesc);

		mm = MMAPIPN;	/* don't pre-search new recs */
		/* If this is the only SQL clause (`lonely') and likeprows
		 * is non-zero (limiting final hits), then we *might* be
		 * able to reduce the result set by only keeping the top
		 * likeprows hits in a heap.  But if a post-process is
		 * required, we'll need to keep *all* hits for post-process;
		 * we find this out in i3dbfinit() and maybe close this heap
		 * (and do not trim the B-tree down to likeprows, below):
		 */
		if (ddmmapi->lonely && TXnlikephits > 0)
		{
			/* Bug 4166: if doing an ORDER BY other than
			 * `$rank desc' (or we might be -- ORDER BY
			 * num), truncating to likeprows with the rank
			 * heap is premature, as truncated low-rank
			 * rows might sort higher via the final ORDER
			 * BY.  Use the keyrec heap (if possible),
			 * which sorts with the ORDER BY projection
			 * not rank.  See also dolikep():
			 */
			if (!TXApp->legacyVersion7OrderByRank &&
			    (haveOrderByNotRankDesc ||
			     tbspec->haveOrderByNum))/*might be !($rank desc)*/
			{
				/* Use keyrec maxrows/heap -- iff we
				 * know the ORDER BY projection/sort
				 * for that heap to use (we do for
				 * `ORDER BY expr', but not for `ORDER
				 * BY num'):
				 */
				if (exs && exs->keyrec &&
				    !tbspec->haveOrderByNum)
				{
					if (exs->useMaxRows == 0 ||
					    exs->useMaxRows > TXnlikephits)
						exs->useMaxRows =TXnlikephits;
					/* else already maxrows limited */
				}
				/* else likeprows trunc in keyrec heap
				 * or below might cut off valid rows,
				 * since we will not have done the
				 * ORDER BY yet; thus we check
				 * `haveOrderByNotRankDesc' etc. below
				 */
			}
			else
				pbt->fh = openfheap(heapcmp, NULL, 0);
		}
		/* see traceindex msg below */
		if (!(pbt->flags & (PBF_RECIDORDER | PBF_INVTREE)) &&
		    (pbt->i->flags & BT_FIXED) &&
		    !(TXApp && TXApp->legacyVersion7OrderByRank))
			/* B-tree is rank-order; since we will be
			 * inserting positive ranks, use reverse sort.
			 */
			btsetcmp(pbt->i, TXfixedUnsignedReverseCmp);
		break;
	case FOP_MM:				/* LIKE */
	case FOP_NMM:				/* LIKEIN */
		iindex->nrank = 1;		/* have rank */
		iindex->orank = 0;		/* do not order by rank */
		/* fall through: */
	default:				/* LIKE, LIKE3, LIKEIN */
		if (pbt->i->flags & BT_FIXED)
			pbt->flags |= PBF_RECIDORDER;
		/* else probably a BT_LINEAR B-tree from
		 * TXcreateResultsLinearBtreeAndKeyrec()
		 */
	}

	/* Results B-tree (`pbt->i') and PROXBTREE flags:
	 *
	 * BT_LINEAR (and !BT_FIXED) set (for any LIKE... type) iff:
	 *   !dbisearch->inv (i.e. no unhandled AND/OR) and
	 *   (ORDER BY all-aux or SELECT all-aux or indexcache)
	 * Set by TXcreateResultsLinearBtreeAndKeyrec() via TXsetupauxorder().
	 * B-tree key is $rank + aux data, loc is recid.
	 * Reason: saving aux data in results for later ORDER BY, SELECT
	 * or indexcache; BT_FIXED does not have the room for it.
	 * `exs->keyrec' also opened (which does the sort, if done).
	 *
	 * BT_FIXED | BT_UNSIGNED set if above condition false.
	 * Reason: not saving aux data for ORDER BY/SELECT/indexcache.
	 *
	 * PBF_RECIDORDER: B-tree results in recid not rank order.
	 * B-tree is BT_FIXED, key is table recid, loc is neg. rank.
	 * Set when LIKE[3,IN] and not BT_LINEAR.
	 * Reason: LIKE[3,IN] do not rank-sort, and no aux data saved
	 * so BT_FIXED has room.
	 *
	 * PBF_INVTREE: flip rank and recid when stored in B-tree.
	 * B-tree is BT_FIXED; key is table recid, loc is neg. rank.
	 * Set if dbisearch->inv, which is set if unhandled AND/OR
	 * (and hence no BT_LINEAR).
	 * Reason: later AND/OR needs to merge these results by recid,
	 * so recid order needed; no aux data saved, so BT_FIXED has room.
	 *
	 * If neither PBF_RECIDORDER nor PBF_INVTREE set,
	 * B-tree may be BT_LINEAR (if saving aux data),
	 * or BT_FIXED (LIKE{P,R}, key is neg. rank, loc is recid).
	 */
	if (TXtraceIndexBits & 0x200000)	/* temp Bug 6796 tracing */
	{
		char	buf[1024];

		TXbtreePrFlags(pbt->i, buf, sizeof(buf));
		txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
		       "%s output B-tree flags: %s cmp: %s",
		       TXqnodeOpToStr((QNODE_OP)op, NULL, 0), buf,
			       TXbtreeCmpFuncToStr(pbt->i));
		txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			       "PROXBTREE flags:%s%s%s",
		       ((pbt->flags & PBF_SETWEIGHTS) ? " PBF_SETWEIGHTS":""),
		       ((pbt->flags & PBF_RECIDORDER) ? " PBF_RECIDORDER":""),
		       ((pbt->flags & PBF_INVTREE) ? " PBF_INVTREE" : ""));
		if (exs && exs->keyrec)
		{
			char	orderBuf[1024], *d, *e, *ddSchema = NULL;
			size_t	i;

			if (exs->keyrec->cmporder)
			{
				for (i=0, d=orderBuf, e = d + sizeof(orderBuf);
				     d < e && i < exs->keyrec->auxalloced;
				     i++)
				{
					/* `cmporder' is internal order;
					 * WTF map to user order since that is
					 * how schema is printed in putmsg:
					 */
					d += htsnpf(d, e - d, "%s%d",
						    (i > 0 ? ", " : ""),
					       (int)exs->keyrec->cmporder[i]);
				}
				strcpy(orderBuf + sizeof(orderBuf) - 4, "...");
			}
			else
				strcpy(orderBuf, "not set");
			ddSchema = TXddSchemaToStr(tbldd(exs->fc->tbl1), 2);
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
				       "KEYREC schema: %s to sort in internal field order %s via %s with useMaxRows %wkd%s",
				       ddSchema, orderBuf,
				       TXkeyrecCmpFuncToStr(exs->keyrec),
				       (EPI_HUGEINT)exs->useMaxRows,
				       (exs->nosort ? " (but not sorting)" :
					""));
			ddSchema = TXfree(ddSchema);
		}
		else
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
				       "No KEYREC opened");
		txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			       "Rank heap%s opened: DDMMAPI.lonely: %d likeprows: %d proj: %s haveOrderByNotRankDesc: %d haveOrderByNum: %d legacyVersion7OrderByRank: %d",
			       (pbt->fh ? "" : " not"),
			       (int)ddmmapi->lonely, (int)TXnlikephits,
			       (tbspec->proj ? "Y" : "N"),
			       (int)haveOrderByNotRankDesc,
			       (int)tbspec->haveOrderByNum,
			       (int)TXApp->legacyVersion7OrderByRank);

	}

	and = BTREEPN;
	nandrecs = (EPI_HUGEUINT)0;
	if (tbspec && tbspec->pind != IINDEXPN)
	{
#ifdef NEW_I
		dead code;
		if(tbspec->pind->mirror)
		{
			and = tbspec->pind->mirror->btree;
			nandrecs = tbspec->pind->cntorig;
		}
		else if(tbspec->pind->inv)
		{
			and = tbspec->pind->inv->btree;
			nandrecs = tbspec->pind->cntorig;
		}
#else /* !NEW_I */
		if(tbspec->pind->mirror != BTREEPN)
		{
			and = tbspec->pind->mirror;
			nandrecs = tbspec->pind->cntorig;
		}
		else if(tbspec->pind->inv != BTREEPN)
		{
			and = tbspec->pind->inv;
			nandrecs = tbspec->pind->cntorig;
		}
#endif /* !NEW_I */
	}
	if (op == FOP_MMIN)
	{
		if ((mq = mmripq(ddmmapi->query)) == MMQLPN)
			goto err;
	}
	else if ((mq = mmrip(ddmmapi->mmapi, 1)) == MMQLPN)
		goto err;
	if (!globalcp &&
	    !(globalcp = TXopenapicp()))
		goto err;
	tmpsm = globalcp->stringcomparemode;
	globalcp->stringcomparemode |= TXCFF_PREFIX;

    {
	EXTRA	*extra = (EXTRA *)pbt->xxx;

	if (extra && extra->goodpred)
	{
		if (!extra->cachedkeyrec)	/* no indexcache results */
		{
			ret =
				fdbi_get(fi, ddmmapi->mmapi, mq, i3dbfinit,
					 i3dbfauxok, i3dbfinsert, pbt,
					 &dbisearch->nopost,
					 op, and, (EPI_OFF_T)nandrecs);
			extra->cachednopost = dbisearch->nopost;
		}
		else
		{
			/* No need to search with fdbi_get(): we are
			 * re-using indexcache results in `extra->keyrec':
			 */
			ret = 1;
			dbisearch->nopost = extra->cachednopost;
		}
	}
	else
		ret =
			fdbi_get(fi, ddmmapi->mmapi, mq, i3dbfinit, NULL,
				 i3dbfinsert, pbt, &dbisearch->nopost, op, and,
				(EPI_OFF_T)nandrecs);
    }

	if (op == FOP_MMIN)
		mq = TXclosemmql(mq, 1);
	else
		mq = TXclosemmql(mq, 0);
	switch (ret)
	{
	case 1:
		break;		/* ok */
	case 0:		/* no indexable sets */
		if (op == FOP_RELEV || op == FOP_NMM)
		{
			globalcp->stringcomparemode = tmpsm;
			pbt->fh = closefheap(pbt->fh);
			dbtbl->order = closeproj(dbtbl->order);/*just in case*/
			dbtbl->order = savedorderby;
			savedorderby = NULL;
			goto trimcont;	/* Johannes sez leave tree */
					/* KNG 20060124 but flush `rc' and
					 * avoid closeextra() double-free
					 * of exs->fc
					 */
		}
		/* fall through */
	default:		/* error */
		globalcp->stringcomparemode = tmpsm;
		goto err;
	}

	if (dotiming)
	{
		TXgetResourceStats(pmbuf, TXRUSAGE_SELF, &tnow);
		txpmbuf_putmsg(pmbuf, MINFO, Fn,
			       "%05.3lf,%05.3lf,%05.3lf,%d,get,%s",
		       tnow.values[TXRESOURCESTAT_UserTime] -
		       tstart.values[TXRESOURCESTAT_UserTime],
		       tnow.values[TXRESOURCESTAT_SystemTime] -
		       tstart.values[TXRESOURCESTAT_SystemTime],
		       tnow.values[TXRESOURCESTAT_RealTime] -
		       tstart.values[TXRESOURCESTAT_RealTime],
		       TXgetpid(0),
		       ddmmapi->query);

	}
	pbt->r = fdbi_getrppm(fi);	/* now safe to call this */

	/* WTF WTF if infinite results, bail here? */

	/* Normally, we just toss new records into the results pile for a
	 * like, because a post-search will be done anyway on all result
	 * records: thus we don't need to Metamorph search them here.  But
	 * if the index says no post-search is needed (all sets resolved),
	 * then we need to search them here, so turn off the
	 * skip-pre-search-of-new-records flag:
	 */
	if (dbisearch->nopost)
		dbisearch->nopre = 0;

	/* Delete list was handled by fdbi_get() */

	/* Get indexcount.  pbt->cntg is inaccurate because fdbi_get() may
	 * not have called the callback for hits where the theoretical rank
	 * was lower than the running threshold.  But fdbi_nrecs() may
	 * include hits rejected by the callback: -KNG 980403
	 * KNG 981123 minhits no longer set by callback: tells fdbi_get() what
	 * it rejected so indexcount can be accurate on the fly (if auxfld
	 * stuff) and thus indexthresh works:
	 */
	numhits = (EPI_HUGEUINT)fdbi_getnrecs(fi) - pbt->minhits;

	/* Add new records: */
	if (op != FOP_RELEV &&	/* not LIKER */
	    /* optimization: */
	    pbt->newlist != BTREEPN && !btreeisnew(pbt->newlist) &&
	    pbt->r != RPPMPN && !TXdisablenewlist)
	{
		/* If requested, unique the new list, to (mostly) work
		 * around duplicate-new-list-entries bug.  Slows
		 * search a little, and still not correct because we
		 * don't know which of the duplicate entries has the
		 * correct aux/compound data (if any).  WTF WTF still
		 * need to root cause of duplicate entries: KNG 000127
		 */
		if (TxUniqNewList >= 2 || (TxUniqNewList && dbi->auxsz > 0))
			didrecs =
				openbtree(CHARPN, BTFSIZE, BTCSIZE,
					  (BT_FIXED | BT_UNIQUE),
					  (O_RDWR | O_CREAT));

		indexthresh = (EPI_HUGEUINT)RppmValsCur[RVAR_INDEXTHRESH].gain;
		/* wtf get indexthresh from rppm struct */
		if (indexthresh == (EPI_HUGEUINT)0)
			indexthresh = (EPI_HUGEUINT)(EPI_HUGEUINT_MAX);
		pbt->flags |= PBF_SETWEIGHTS;	/* set by fdbi_get() */
		/* WTF - These need to be tweaked for parameters */
		/* wtf some of these aren't used by i3dbfinsert?: KNG 980325 */
		pbt->maxperc = 100; /* 100 - allow all, 0 allow only minhits */
		/* JMT - Changed to 0 961004 so not found won't show up */
		pbt->cntg = 0;	/* let it count new records */
		pbt->minhits = 0;	/* Allow at least this many */
		pbt->maxhits = 0;	/* Stop at this many */

		tf = dbnametofld(dbtbl, fname);
		if(dbi->auxsz > 0)
		{
#if defined(__hpux) || defined(__DGUX__)
		/* WTF: For HPUX etc where we force alignment */
			auxbufsz = dbi->auxsz + TX_ALIGN_BYTES;
#else
			auxbufsz = dbi->auxsz + sizeof(EPI_OFF_T);
#endif
		}
		else
		{
			auxbufsz = sizeof(BTLOC);
		}
		auxbuf = TXmalloc(pmbuf, Fn, auxbufsz);
		if (auxbuf)
		{
			rewindbtree(pbt->newlist);
			rppm_setflags(pbt->r, RPF_LOGICHECK, 1);
			while (sz = auxbufsz,
			       btloc =
			       btgetnext(pbt->newlist, &sz, auxbuf, NULL),
			       TXrecidvalid(&btloc))
			{
				/* KNG 000127 check dups */
				if (didrecs != BTREEPN &&
				    btinsert(didrecs, &btloc, sizeof(btloc),
					     &btloc) != 0)
				{
					if (++nbad <= 3 && TxUniqNewList >= 3)
						txpmbuf_putmsg(pmbuf, MERR, Fn,
				       "Duplicate recid 0x%wx in new list %s",
						(EPI_HUGEINT)TXgetoff(&btloc),
						       getdbffn(pbt->newlist->
								dbf));
					continue;  /* skip duplicate recid */
				}
				if (numhits + pbt->cntg >= indexthresh)
					break;
				if (dbisearch->nopre)
				{
			/* We're not bothering to validate new records
			 * here, because a post-search will
			 * (e.g. like).  So fake a rank for these
			 * records, but make it low so that they
			 * appear late in the results: since most
			 * probably don't match, no point in
			 * post-searching them early on if the user
			 * only fetches a few rows:
			 */
			/* Modified so that it will check aux data for record
			 * correctness as we have the data.  Also uses
			 * i3dbfinsert to ensure keyrec stuff works right
			 * for ordering these.  Also bump up the rank as newly
			 * added stuff most important?
			 */
					off = 1000;
					if (dbi->auxsz == 0 || i3dbfauxok(pbt, btloc, auxbuf))
					{
						i3dbfinsert(pbt, btloc, auxbuf, off);
						numhits++;
					}
				}
				else
				{
					if (dbi->auxsz == 0
					    || i3dbfauxok(pbt, btloc, auxbuf))
						i3dbinsertp(pbt, btloc,
							    (dbi->auxsz >
							     0 ? auxbuf :
							     NULL), SHORTPN,
							    i3dbfinsert, mm,
							    op, infld);
				}
			}
			if (TxUniqNewList >= 3 && nbad > 3)
				txpmbuf_putmsg(pmbuf, MERR, Fn,
				       "%d duplicates in new list %s", nbad,
				       getdbffn(pbt->newlist->dbf));
			numhits += pbt->cntg;	/* plus new records' hits */
			auxbuf = TXfree(auxbuf);
			auxbufsz = 0;
		}
	}

	/* Add empty records: */
	if (op == FOP_MMIN &&	/* only LIKEIN */
	    dbi->ct != BTREEPN && pbt->r != RPPMPN)
	{
		indexthresh = (EPI_HUGEUINT)RppmValsCur[RVAR_INDEXTHRESH].gain;
		/* wtf get indexthresh from rppm struct */
		if (indexthresh == (EPI_HUGEUINT)0)
			indexthresh = (EPI_HUGEUINT)(EPI_HUGEUINT_MAX);
		pbt->flags |= PBF_SETWEIGHTS;	/* set by fdbi_get() */
		/* WTF - These need to be tweaked for parameters */
		/* wtf some of these aren't used by i3dbfinsert?: KNG 980325 */
		pbt->maxperc = 100; /* 100 - allow all, 0 allow only minhits */
		/* JMT - Changed to 0 961004 so not found won't show up */
		pbt->cntg = 0;	/* let it count new records */
		pbt->minhits = 0;	/* Allow at least this many */
		pbt->maxhits = 0;	/* Stop at this many */

		tf = dbnametofld(dbtbl, fname);
		auxbufsz = sizeof(BTLOC);
		if (auxbuf != NULL)
			auxbuf = TXfree(auxbuf);
		auxbuf = TXmalloc(pmbuf, Fn, auxbufsz);
		if (auxbuf)
		{
			rewindbtree(dbi->ct);
			rppm_setflags(pbt->r, RPF_LOGICHECK, 1);
			while (sz = auxbufsz,
			       btloc = btgetnext(dbi->ct, &sz, auxbuf, NULL),
			       TXrecidvalid(&btloc))
			{
				if (numhits + pbt->cntg >= indexthresh)
					break;
				if (!TXgetoff(&btloc))
					i3dbinsertp(pbt, *(BTLOC *) auxbuf,
						    NULL, SHORTPN,
						    i3dbfinsert, mm, op,
						    infld);
			}
			numhits += pbt->cntg;	/* plus new records' hits */
			auxbuf = TXfree(auxbuf);
			auxbufsz = 0;
		}
	}

	if(dbisearch->imode && dbtbl)
	{
		if(TXunlockindex(dbtbl, dbisearch->imode, NULL) == 0)
			dbisearch->imode = 0;
	}
	if (tbspec)
		pred_rmalts(tbspec->pred);
	globalcp->stringcomparemode = tmpsm;	/* restore it */

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	/* Sort (maybe) `exs->keyrec', put into results B-tree `pbt->i',
	 * and free (if not indexcache'ing):
	 */
	if (exs && exs->keyrec)
	{
		BTLOC btl;
		size_t sz;
		byte tmpbuf[BT_MAXPGSZ];
		int  rowscopied = 0;

		/* some maxrows logic here moved to i3dbfinit() */
		if(!exs->nosort)
			/* Do the ORDER BY on the `exs->keyrec' results: */
			keyrecsort(exs->keyrec);

		if (dotiming)
		{
			TXgetResourceStats(pmbuf, TXRUSAGE_SELF, &tnow);
			txpmbuf_putmsg(pmbuf, MINFO, Fn,
				       "%05.3lf,%05.3lf,%05.3lf,%d,sort,%d",
			       tnow.values[TXRESOURCESTAT_UserTime] -
			       tstart.values[TXRESOURCESTAT_UserTime],
			       tnow.values[TXRESOURCESTAT_SystemTime] -
			       tstart.values[TXRESOURCESTAT_SystemTime],
			       tnow.values[TXRESOURCESTAT_RealTime] -
			       tstart.values[TXRESOURCESTAT_RealTime],
			       TXgetpid(0),
			       (int)numhits);

		}
		/*
		fldcmp2(0, 0, 0, 0, 0);
		*/

		/* Put the `exs->keyrec' records into B-tree `pbt->i': */
		if (TXtraceIndexBits & 0x200000)/* Bug 6796 tracing */
		{
			char	xtra[100];

			if (exs->useMaxRows)
				htsnpf(xtra, sizeof(xtra),
				       " and trimming to useMaxRows %wkd",
				       (EPI_HUGEINT)exs->useMaxRows);
			else
				*xtra = '\0';
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			   "Appending %wkd keyrec results to output B-tree%s",
			    (EPI_HUGEINT)(exs->keyrec->toprows ?
					  fheap_stknum(exs->keyrec->toprows) :
					  exs->keyrec->used), xtra);
		}
		keyrecrewind(exs->keyrec);
		sz = sizeof(tmpbuf);
		btl = keyrecgetnext(exs->keyrec, tmpbuf, &sz);
		rowscopied = 0;
		if (exs->useMaxRows > 0) iindex->rowsReturned = 0;
		while (TXrecidvalid(&btl))
		{
			if(sz < exs->extrasz)
			{
				txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
				"Internal consistency check failed at: %s:%d",
				       __FILE__, __LINE__);
				goto err;
			}
			btappend(pbt->i, &btl, sz, tmpbuf, 90, NULL);
			if (exs->useMaxRows > 0)
			{	/* Trimming to maxrows or maybe likeprows */
				iindex->rowsReturned++;
				if(++rowscopied >= exs->useMaxRows)
					break;
			}
			sz = sizeof(tmpbuf);
			btl = keyrecgetnext(exs->keyrec, tmpbuf, &sz);
		}
		btflush(pbt->i);
		rewindbtree(pbt->i);
		if (TXtraceIndexBits & 0x800000)/* Bug 6796 tracing */
		{
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
				    "%wkd B-tree %p records after appending:",
				 (EPI_HUGEINT)TXbtreeGetNumItemsDelta(pbt->i),
				       pbt->i);
			TXbtreeDump(pmbuf, pbt->i, 2, 0);
		}

		/* We're done with `exs->keyrec' -- unless indexcache'ing: */
		if (!dbtbl->ddic->options[DDIC_OPTIONS_INDEX_CACHE])
			exs->keyrec = keyrecclose(exs->keyrec);
		else if (TXtraceIndexBits & 0x100000)
		{
			txpmbuf_putmsg(pmbuf, MINFO, CHARPN,
				       "indexcache saved records (%wd):",
			    (EPI_HUGEINT)(exs->keyrec->toprows ?
					  fheap_stknum(exs->keyrec->toprows) :
					  exs->keyrec->used));
			TXkeyrecDump(pmbuf, exs->keyrec, 2);
		}
	}

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	/* KNG 20060124 moved exs->fc clear to `trimcont' below */
	pbt->fh = closefheap(pbt->fh);
	rewindbtree(rc);
	if ((pbt->flags & (PBF_RECIDORDER | PBF_INVTREE)) == PBF_RECIDORDER)
	{			/* LIKE/LIKE3/LIKEIN */
		/* The B-tree is in recid order, with recid as the key.
		 * We want the same order, but with rank as the key.
		 * We know this is a fixed tree (no aux fields)
		 * because it's PBF_RECIDORDER:
		 */
		prevResultsTree = rc;
		pbt->i = rc = openbtree(NULL, BTFSIZE, BTCSIZE,
					(BT_FIXED | BT_UNSIGNED | BT_LINEAR),
					O_RDWR | O_CREAT);
		if (TXtraceIndexBits & 0x200000) /* Bug 6796 tracing */
		{
			char	flagsBuf[256];

			TXbtreePrFlags(rc, flagsBuf, sizeof(flagsBuf));
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
     "Converting B-tree (with %wkd results) to %s, recid order with rank key",
			(EPI_HUGEINT)TXbtreeGetNumItemsDelta(prevResultsTree),
			       flagsBuf);
		}
		rewindbtree(prevResultsTree);
		while (sz = sizeof(btloc),
		       rankloc = btgetnext(prevResultsTree, &sz, &btloc, NULL),
		       TXrecidvalid(&rankloc))
		{
			sz = sizeof(rankloc);
			/* WTF WTF WTF: we are appending to preserve
			 * recid order, even though the key is now
			 * rank.  This means the only safe operations
			 * later are rewind and btgetnext() (but no
			 * one should be inserting...):
			 */
			if (btappend(rc, &btloc, sz, &rankloc, 100, BTBMPN) <0)
				break;
		}
	}
	else if ((op == FOP_PROXIM || op == FOP_RELEV) &&
		 /* KNG 20080124 Bug 2022: do not trim result set if a
		  * post-process is needed, as valid hits might be lost;
		  * i.e. check `dbisearch->nopost' too:
		  */
		 (ddmmapi->lonely && TXnlikephits > 0 && dbisearch->nopost) &&
		 !(pbt->flags & PBF_RECIDORDER) &&
		 (TXApp->legacyVersion7OrderByRank ||
		  /* Bug 4166: do not likeprows-trim here if ORDER BY
		   * not $rank desc: might remove valid rows before
		   * caller does ORDER BY:
		   */
		  (!(haveOrderByNotRankDesc || tbspec->haveOrderByNum) &&
		   /* Optimization due to Bug 4166 fix above: We might
		    * already have just likeprows results, if a KEYREC
		    * above with useMaxRows = likeprows trimmed to that.
		    * Or if likeprows is large, we might not have
		    * reached that limit, but might still have a large
		    * tree; we can avoid unneeded copy:
		    */
		   TXbtreeGetNumItemsDelta(rc) > TXnlikephits)))
	{
		DD	*newDd = NULL, *oldDd;

		/* The B-tree is either in rank order with rank as the
		 * key, or recid order with recid as the key
		 * (PBF_INVTREE).  Either way, preserve the order and
		 * key, just delete hits over TXnlikephits (but it
		 * only applies to LIKEP/LIKER):
		 */
		if (auxbuf == NULL)	/* need buf for reading */
		{
			if(exs && (exs->keysz > sizeof(BTLOC)))
			{
				auxbufsz = exs->keysz;
			}
			else
			{
				if(dbi->auxsz > 0)
				{
#if defined(__hpux) || defined(__DGUX__)
				/* WTF: For HPUX etc where we force alignment*/
					auxbufsz = dbi->auxsz + TX_ALIGN_BYTES;
#else /* !__hpux && !__DGUX__ */
					auxbufsz = dbi->auxsz +
						sizeof(EPI_OFF_T);
#endif /* !__hpux && !__DGUX__ */
				}
				else
				{
					auxbufsz = sizeof(BTLOC);
				}
			}
			auxbuf = TXmalloc(pmbuf, Fn, auxbufsz);
			if (auxbuf == NULL)
				goto trimcont;
		}
		flags = (BT_UNSIGNED | BT_LINEAR);
		if (dbi->auxsz == 0)
		{
			flags |= BT_FIXED;
			btpagesize = BTFSIZE;
		}
		else
		{
			btpagesize = 30000;
		}
		prevResultsTree = rc;	/* hold on to old tree */
		pbt->i = rc = openbtree(NULL, btpagesize, BTCSIZE, flags,
					(O_RDWR | O_CREAT));
		btsetcmp(rc, prevResultsTree->cmp); /*wtf ok to use old cmp?*/
		rc->usr = prevResultsTree->usr;
		/* Bug 6796 comment #2: failure to copy over DD
		 * meant TXtup_project_setup() could not validate
		 * projection against results B-tree DD, and thus did
		 * not set indexdataonly (for no table reads iff aux only):
		 */
		oldDd = btreegetdd(prevResultsTree);
		if (oldDd)
		{
			newDd = TXdupDd(oldDd);
			if (newDd) btreesetdd(rc, newDd);
			newDd = closedd(newDd);
		}
		if (TXtraceIndexBits & 0x200000) /* Bug 6796 tracing */
		{
			char	flagsBuf[256];

			TXbtreePrFlags(rc, flagsBuf, sizeof(flagsBuf));
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			       "Converting B-tree (with %wkd results) to %s, same order and key, trimming to %kd likeprows",
			(EPI_HUGEINT)TXbtreeGetNumItemsDelta(prevResultsTree),
			       flagsBuf, (int)TXnlikephits);
		}
		rewindbtree(prevResultsTree);
		/* Since we are trimming otherwise-matching results,
		 * set `iindex->rowsReturned' since it will differ
		 * from `dbisearch->nhits':
		 */
		for (i = 0, iindex->rowsReturned = 0;
		     i < TXnlikephits;
		     i++, iindex->rowsReturned++)
		{
			sz = auxbufsz;
			/* auxbuf is rank or fields */
			btloc = btgetnext(prevResultsTree, &sz, auxbuf, NULL);
			if (!TXrecidvalid(&btloc))
				break;
			/* prevResultsTree is already sorted, we can
			 * use linear mode to save an extra sort:
			 */
			btappend(rc, &btloc, sz, auxbuf, 100, BTBMPN);
		}
	}
      trimcont:
	if (exs)
	{
		/* KNG 20060124 this avoids double-free of `exs->fc' in
		 * closeextra()?  moved down here so fdbi_get() ret of 0
		 * (which jumps down here) also does this (and btflush()):
		 */
		if (pbt->i->usr == exs->fc)
			exs->fc = NULL;
	}
	btflush(rc);		/* must flush linear */
	if (prevResultsTree)
	{
		prevResultsTree->usr = NULL;	/* may point to `exs->fc'? */
		prevResultsTree = closebtree(prevResultsTree);
	}

	dbisearch->nhits = numhits;
	ddmmapi->bt = pbt;	/* for dolikep() */
	if (ddmmapi->self != ddmmapi)
		ddmmapi->self->bt = pbt;	/* for close() */

	/* TXtraceIndexBits & 0x8000 will report returned B-tree in
	 * ixfmmindex()
	 */

	goto done;

      err:
	pbt = TXcloseproxbtree(pbt);
	rc = closebtree(rc);
	dbtbl->order = closeproj(dbtbl->order);	/* just in case */
	dbtbl->order = savedorderby;
	savedorderby = NULL;
      done:
	savedorderby = closeproj(savedorderby);
	if (auxbuf != NULL)
		auxbuf = TXfree(auxbuf);
	closebtree(didrecs);
#ifdef NEW_I
	if(dbisearch->inv)
		iindex->inv = dbidxfrombtree(rc, DBIDX_MEMORY);
	else
		iindex->orig = dbidxfrombtree(rc, DBIDX_MEMORY);
#else
	if(dbisearch->inv)
		iindex->inv = rc;
	else
		iindex->orig = rc;
#endif
	if(tbspec)
		iindex->piand = tbspec->pind;
	if (dotiming)
	{
		TXgetResourceStats(pmbuf, TXRUSAGE_SELF, &tnow);
		txpmbuf_putmsg(pmbuf, MINFO, Fn,
			       "%05.3lf,%05.3lf,%05.3lf,%d,done,%d",
		       tnow.values[TXRESOURCESTAT_UserTime] -
		       tstart.values[TXRESOURCESTAT_UserTime],
		       tnow.values[TXRESOURCESTAT_SystemTime] -
		       tstart.values[TXRESOURCESTAT_SystemTime],
		       tnow.values[TXRESOURCESTAT_RealTime] -
		       tstart.values[TXRESOURCESTAT_RealTime],
		       TXgetpid(0),
		       (int)numhits);

	}
	if(rc)
		return 0;
	else
		return -1;
}

/******************************************************************/

RECID *
put3dbi(dbi, loc)
A3DBI *dbi;
BTLOC *loc;
{
	btinsert(dbi->newrec, loc, sizeof(BTLOC), loc);
	if (dbi->mnew)
		btinsert(dbi->mnew, loc, sizeof(BTLOC), loc);
	return loc;
}

/******************************************************************/

RECID *
put3dbiu(dbi, nloc, loc)
A3DBI *dbi;
BTLOC *nloc;
BTLOC *loc;
{
	switch (dbi->type)
	{
	case INDEX_MM:
	case INDEX_MMCR:
	case INDEX_FULL:
	case INDEX_FULLCR:
		/* nothing to do; we don't use _U index   KNG 971112 */
		break;
	default:
		btinsert(dbi->upd, loc, sizeof(BTLOC), nloc);
		if (dbi->mupd)
			btinsert(dbi->mupd, loc, sizeof(BTLOC), nloc);
	}
	return loc;
}

/* ------------------------------------------------------------------------- */

static int init3dbia2ind ARGS((DBTBL * dbtbl, A3DBI * dbi));
static int
init3dbia2ind(dbtbl, dbi)
DBTBL *dbtbl;
A3DBI *dbi;

/* Returns 0 on error.
 */
{
	static CONST char fn[] = "init3dbia2ind";
	BTREE *bt;
	DD *dd;
	FLDCMP *fc;

	dbi->auxbi.btree = bt = dbi->newrec;	/* alias it */
	if (bt == BTREEPN)
		goto err;
	dd = btreegetdd(bt);
	if (dd == DDPN)
		goto baddd;
	btsetcmp(bt, (btcmptype) fldcmp);
	if (!(fc = TXopenfldcmp(bt, TXOPENFLDCMP_CREATE_FLDOP)))
		goto err;
	bt->usr = fc;
	dbi->auxbi.table = createtbl(dd, NULL);
	if (dbi->auxbi.table == TBLPN || fc->tbl1 == TBLPN
	    || fc->tbl2 == TBLPN)
		goto badtbl;
	if ((dbi->auxa2i = TXadd2indsetup(dbtbl, &dbi->auxbi)) == NULL)
		goto err;

	if ((bt = dbi->mnew) != BTREEPN)	/* index update in progress */
	{
		dbi->mauxbi.btree = bt;
		dd = btreegetdd(bt);
		if (dd == DDPN)
		{
		      baddd:
			putmsg(MERR, fn, "Can't get dd from %s",
			       getdbffn(bt->dbf));
			goto err;
		}
		btsetcmp(bt, (btcmptype) fldcmp);
		if (!(fc = TXopenfldcmp(bt, TXOPENFLDCMP_CREATE_FLDOP)))
			goto err;
		bt->usr = fc;
		dbi->mauxbi.table = createtbl(dd, NULL);
		if (dbi->mauxbi.table == TBLPN || fc->tbl1 == TBLPN
		    || fc->tbl2 == TBLPN)
		{
		      badtbl:
			putmsg(MERR, fn, "Can't createtbl(dd, NULL) for %s",
			       getdbffn(bt->dbf));
			goto err;
		}
		if ((dbi->mauxa2i = TXadd2indsetup(dbtbl, &dbi->mauxbi)) ==
		    NULL)
			goto err;
	}

	return (1);
      err:
	return (0);
}

static void prdbimsg ARGS((CONST char *msg, RECID recid, byte * aux, size_t sz));
static void
prdbimsg(msg, recid, aux, sz)
CONST char *msg;
RECID recid;
byte *aux;
size_t sz;
{
	char tmp[256];
	size_t i;

	if (TXrecidvalid2(&FdbiTraceRecid) &&
	    TXrecidcmp(&FdbiTraceRecid, &recid) != 0)
		return;
	*tmp = '\0';
	if (sz > 0)
	{
		if (sz > (sizeof(tmp) - 1) / 3)
			sz = (sizeof(tmp) - 1) / 3;
		*tmp = ' ';
		for (i = 0; i < sz; i++)
			sprintf(tmp + 1 + 3 * i, " %02X", (unsigned) aux[i]);
	}
	putmsg(MINFO, CHARPN, "%s 0x%wx%s",
	       msg, (EPI_HUGEINT)TXgetoff(&recid), tmp);
}

#define DBIMSG(msg, recid, aux, sz)     \
  if (FdbiTraceIdx == 2) prdbimsg(msg, recid, aux, sz)

RECID *
addto3dbi(dbtbl, dbi, loc)
DBTBL *dbtbl;
A3DBI *dbi;
BTLOC *loc;

/* Like put3dbi(): insert `loc' into new lists (for insert, update).
 * But handles aux field data as well.  Returns NULL on error.
 */
{
	/* WTF can't distinguish between unique btinsert() and real error: */
	if (dbi->auxsz > 0)	/* aux fields: vbtree */
	{			/* duplicate aux,recid pairs (during update) handled by delfromnew */
		if (dbi->auxa2i == NULL && !init3dbia2ind(dbtbl, dbi))
			goto err;
		TXa2i_setbuf(dbi->auxa2i);
		DBIMSG("addto3dbi    (+new)    ", *loc,
		       ((TXA2IND *) dbi->auxa2i)->tbl->orec,
		       ((TXA2IND *) dbi->auxa2i)->tbl->orecdatasz);
		TXa2i_btreeinsert(dbi->auxa2i, loc);
		if (dbi->mauxa2i != NULL)	/* index update in progress */
		{
			TXa2i_setbuf(dbi->mauxa2i);
			DBIMSG("addto3dbi    (+new tmp)", *loc,
			       ((TXA2IND *) dbi->mauxa2i)->tbl->orec,
			       ((TXA2IND *) dbi->mauxa2i)->tbl->orecdatasz);
			TXa2i_btreeinsert(dbi->mauxa2i, loc);
		}
	}
	else			/* no aux fields: fbtree */
	{			/* BT_UNIQUE handles dups */
		DBIMSG("addto3dbi    (+new)    ", *loc, BYTEPN, 0);
		btinsert(dbi->newrec, loc, sizeof(*loc), loc);
		if (dbi->mnew != BTREEPN)
		{
			DBIMSG("addto3dbi    (+new tmp)", *loc, BYTEPN, 0);
			btinsert(dbi->mnew, loc, sizeof(*loc), loc);
		}
	}
	return (loc);
      err:
	return (BTLOCPN);
}

int
delfromnew3dbi(dbtbl, dbi, loc)
DBTBL *dbtbl;
A3DBI *dbi;
BTLOC *loc;

/* Deletes `*loc' from new-records list (for update, delete).
 * NOTE: addtodel3dbi() should be called right after with same `loc'.
 */
{
	BTLOC x1, btloc;

	TXsetrecid(&dbi->delloc, (EPI_OFF_T) (-1));	/* no FDBI direct stuff */
	dbi->auxnewdel = 0;

	if (dbi->auxsz > 0)	/* aux fields: vbtree */
	{
		if (dbi->auxa2i == NULL && !init3dbia2ind(dbtbl, dbi))
			goto err;
		TXa2i_setbuf(dbi->auxa2i);	/* _old_ row's data */
		DBIMSG("delfromnew3d (-new)    ", *loc,
		       ((TXA2IND *) dbi->auxa2i)->tbl->orec,
		       ((TXA2IND *) dbi->auxa2i)->tbl->orecdatasz);
		TXa2i_btreedelete(dbi->auxa2i, loc);
		if (dbi->mauxbi.btree != BTREEPN)	/* index update in progress */
		{
			TXa2i_setbuf(dbi->mauxa2i);	/* _old_ row's data */
			DBIMSG("delfromnew3d (-new tmp)", *loc,
			       ((TXA2IND *) dbi->mauxa2i)->tbl->orec,
			       ((TXA2IND *) dbi->mauxa2i)->tbl->orecdatasz);
			TXa2i_btreedelete(dbi->mauxa2i, loc);
		}
	}
	else			/* no aux fields: fbtree */
	{
		btloc = x1 = *loc;
		DBIMSG("delfromnew3d (-new)    ", btloc, BYTEPN, 0);
		btdelete(dbi->newrec, &x1, sizeof(btloc), &btloc);
		if (dbi->mnew != BTREEPN)	/* KNG 000217 */
		{
			btloc = x1 = *loc;
			DBIMSG("delfromnew3d (-new tmp)", btloc, BYTEPN, 0);
			btdelete(dbi->mnew, &x1, sizeof(btloc), &btloc);
		}
	}
	return (0);
      err:
	return (-1);
}

int
addtodel3dbi(dbtbl, dbi, loc)
DBTBL *dbtbl;
A3DBI *dbi;
BTLOC *loc;
/* Adds `loc' to deleted record lists (for update, delete).
 * Returns -1 on error, 0 if ok.
 */
{
	BTLOC x;

	(void)dbtbl;
	x = *loc;
	/* WTF can't distinguish between true failure, and unique insert: */
	DBIMSG("addtodel3dbi (+del)    ", x, BYTEPN, 0);
	btinsert(dbi->del, &x, sizeof(x), &x);	/* BT_UNIQUE handles dups */
	if (dbi->mdel != BTREEPN)	/* index update in progress */
	{
		DBIMSG("addtodel3dbi (+del tmp)", x, BYTEPN, 0);
		btinsert(dbi->mdel, &x, sizeof(x), &x);
	}
	return (0);
}

int
delfromfdbi(dbtbl, fi, loc)
DBTBL *dbtbl;
FDBI *fi;
BTLOC *loc;
/* Like delfromnew3dbi() and addtodel3dbi() together, but for
 * aux-fields-update-only.  NOTE: assumes addtofdbi() to be called
 * right after, for the same row with new aux data (recid may change).
 */
{
	static CONST char fn[] = "delfromfdbi";
	A3DBI *dbi;
	BTLOC btloc;
	RECID	invalidRecid;

	TXsetrecid(&invalidRecid, (EPI_OFF_T)(-1));
	dbi = fdbi_getdbi(fi);
	/* If appropriate, fall back to new/del list update only.  Also fall
	 * back during index update, for now: may not be safe to muck with
	 * token file:
	 */
	if (dbi->auxsz <= 0 || dbi->mnew != BTREEPN || dbi->mdel != BTREEPN)
	{			/* fallback */
		if (delfromnew3dbi(dbtbl, dbi, loc) < 0)
			return (-1);
		/* Optimization: if the recid isn't in the token file, don't add
		 * it to the regular delete list.  This costs us a little time
		 * here on update, but saves delete-list-searching on search.
		 * KNG 000307 still must add it to the _mdel_ delete list,
		 * since it might eventually be in the new token file:
		 */
		if (fdbi_updatetokaux(fi, *loc, NULL, invalidRecid) == 1)	/* not in token file */
		{
			/* WTF can't distinguish between true failure, and unique insert: */
			if (dbi->mdel != BTREEPN)	/* index update in progress */
			{
				btloc = *loc;	/* BT_UNIQUE handles dups */
				DBIMSG("delfromfdbi  (+del tmp)", btloc,
				       BYTEPN, 0);
				btinsert(dbi->mdel, &btloc, sizeof(btloc),
					 &btloc);
			}
			return (0);
		}
		return (addtodel3dbi(dbtbl, dbi, loc));
	}

	if (dbi->auxa2i == NULL && !init3dbia2ind(dbtbl, dbi))
		goto err;

	/* Sanity check: if delloc is valid, then a delfromfdbi() wasn't
	 * followed by an addtofdbi():
	 */
	if (TXrecidvalid2(&dbi->delloc))
		putmsg(MERR, (char *) fn,
		       "Unfinished update for recid 0x%wx in %s",
		       (EPI_HUGEINT)TXgetoff(&dbi->delloc),
		       getdbffn(dbi->auxbi.btree->dbf));

	dbi->delloc = *loc;	/* note for addtofdbi() */
	dbi->auxnewdel = 0;

	/* Even though we're attempting a token-only update, this row may be
	 * already present in the new list (for whatever reason).  If so,
	 * we've got to update that new list entry in addtofdbi(), so delete
	 * it and make a note here:
	 */
	btloc = *loc;
	TXa2i_setbuf(dbi->auxa2i);	/* _old_ row's data */
	if (TXa2i_btreedelete(dbi->auxa2i, &btloc) > 0)	/* was in new list */
	{
		DBIMSG("delfromfdbi  (-new)    ", btloc, BYTEPN, 0);
		dbi->auxnewdel |= 1;
	}
	if (dbi->mauxbi.btree != BTREEPN)	/* index update in progress */
	{
		btloc = *loc;
		TXa2i_setbuf(dbi->mauxa2i);	/* _old_ row's data */
		if (TXa2i_btreedelete(dbi->mauxa2i, &btloc) > 0)
		{
			DBIMSG("delfromfdbi  (-new tmp)", btloc, BYTEPN, 0);
			dbi->auxnewdel |= 2;
		}
	}
	/* No delete-list insert unless things go wrong in addtofdbi() */
	return (0);
      err:
	return (-1);
}

int
addtofdbi(dbtbl, fi, loc)
DBTBL *dbtbl;
FDBI *fi;
BTLOC *loc;

/* Like addto3dbi(), but for aux-fields-update-only.  Tries to update
 * the token file only, avoiding a new/delete list insert and thus
 * outdating the index.  NOTE: assumes delfromfdbi() called right
 * before this, for the same row (recid may change).
 */
{
	static CONST char fn[] = "addtofdbi";
	BTLOC btloc;
	RECID	invalidRecid;
	A3DBI *dbi;
	int ret, res;

	TXsetrecid(&invalidRecid, (EPI_OFF_T)(-1));
	dbi = fdbi_getdbi(fi);
	if (dbi->auxsz <= 0 || dbi->mnew != BTREEPN || dbi->mdel != BTREEPN)
		goto fallback;
	if (dbi->auxa2i == NULL && !init3dbia2ind(dbtbl, dbi))
		goto err;

	TXa2i_setbuf(dbi->auxa2i);	/* setup new row's data */

	/* Sanity check: should be delfromfdbi() call just before us.  If
	 * the row moved (aux/non-aux var data changed), we need to fall
	 * back to a new/del list update:
	 */
	if (TXrecidcmp(&dbi->delloc, loc) != 0)
	{
		if (!TXrecidvalid2(&dbi->delloc))	/* no previous delfromfdbi */
		{
			putmsg(MERR, (char *) fn,
			       "Non-update insert attempted for recid 0x%wx into index %s",
			       (EPI_HUGEINT)TXgetoff(loc),
			       getdbffn(dbi->auxbi.btree->dbf));
		}
		/* Optimization: if the deleted recid is not in the token file,
		 * we don't need to add it to the delete list.  Costs update
		 * time, but saves search time.  Note: make sure we haven't
		 * already done the token search in delfromfdbi().
		 * KNG 000307 if the index is being updated, we still need to add
		 * to mdel (deleted recid could still end up in new token file):
		 * this was checked in delfromfdbi().
		 */
		if (fdbi_updatetokaux(fi, dbi->delloc, NULL, invalidRecid) == 1)
			goto fallback;
	      fallback2:
		addtodel3dbi(dbtbl, dbi, &dbi->delloc);
	      fallback:
		ret = (addto3dbi(dbtbl, dbi, loc) != BTLOCPN ? 0 : -1);
		goto done;
	}

	/* Try to update the token file.  If it fails, then update the new list.
	 * If it's not found, and the new recid is the same as the delete recid,
	 * then we can skip the delete list insert:
	 */
	res =
		fdbi_updatetokaux(fi, *loc,
				  ((TXA2IND *) dbi->auxa2i)->tbl->orec,
				  invalidRecid);
	DBIMSG(
	       (res ==
		1 ? "fdbi_updatetok (!found)" : "fdbi_updatetok         "),
	       *loc, ((TXA2IND *) dbi->auxa2i)->tbl->orec,
	       ((TXA2IND *) dbi->auxa2i)->tbl->orecdatasz);
	switch (res)
	{
	case 2:
		break;		/* token file updated */
	case 1:
		if (TXrecidcmp(&dbi->delloc, loc) == 0)
			goto fallback;	/*!found */
	case 0:
		goto fallback2;	/* error */
	default:
		putmsg(MERR + UGE, (char *) fn,
		       "Internal error: Unknown fdbi_updatetokaux() return value");
		goto fallback2;
	}

	/* If we deleted the old data from the new list in delfromfdbi(), or
	 * we couldn't update the token file, insert data into new list now:
	 */
	if (dbi->auxnewdel & 1)
	{
		btloc = *loc;
		DBIMSG("addtofdbi    (+new)    ",
		       btloc, ((TXA2IND *) dbi->auxa2i)->tbl->orec,
		       ((TXA2IND *) dbi->auxa2i)->tbl->orecdatasz);
		TXa2i_btreeinsert(dbi->auxa2i, &btloc);
	}
	if (dbi->auxnewdel & 2)
	{
		TXa2i_setbuf(dbi->mauxa2i);
		btloc = *loc;
		DBIMSG("addtofdbi    (+new tmp)",
		       btloc, ((TXA2IND *) dbi->auxa2i)->tbl->orec,
		       ((TXA2IND *) dbi->auxa2i)->tbl->orecdatasz);
		TXa2i_btreeinsert(dbi->mauxa2i, &btloc);
	}
	ret = 0;
	goto done;

      err:
	ret = -1;
      done:
	TXsetrecid(&dbi->delloc, (EPI_OFF_T) (-1));
	dbi->auxnewdel = 0;
	return (ret);
}

int
TXfdbiChangeLoc(dbtbl, fdbi, newLoc)
DBTBL	*dbtbl;		/* (in/out) the table */
FDBI	*fdbi;		/* (in/out) FDBI for the MM index on the table */
BTLOC	newLoc;		/* (in) new recid to change it to */
/* Changes references to current `dbtbl' recid to `newLoc' in index `fdbi'.
 * Used by ALTER TABLE ... COMPACT when moving a recid.
 * Returns 0 on error.
 */
{
	int	ret;
	A3DBI	*dbi;

	dbi = fdbi_getdbi(fdbi);
	if (dbi == A3DBIPN) goto err;

	/* Update new list(s): */
	if (dbi->auxsz > 0)			/* if Metamorph compound */
	{
		if (dbi->auxa2i == NULL && !init3dbia2ind(dbtbl, dbi))
			goto err;
		if (!TXaddtoindChangeLoc(dbi->auxa2i, newLoc, 1))
			goto err;
		if (dbi->mauxa2i != NULL)	/* index update in progress */
		{
			if (!TXaddtoindChangeLoc(dbi->mauxa2i, newLoc, 1))
				goto err;
		}
	}
	else					/* Metamorph non-compound */
	{
		if (!TXbtreeChangeLocOrKey(dbi->newrec, &dbtbl->recid,
					sizeof(dbtbl->recid), dbtbl->recid,
					newLoc, BYTEPN))
			goto err;
		if (dbi->mnew != BTREEPN &&	/* index is being updated */
		    !TXbtreeChangeLocOrKey(dbi->mnew, &dbtbl->recid,
					sizeof(dbtbl->recid), dbtbl->recid,
					newLoc, BYTEPN))
			goto err;
	}

	/* Update delete list(s): */
	if (!TXbtreeChangeLocOrKey(dbi->del, &dbtbl->recid,
			sizeof(dbtbl->recid), dbtbl->recid, newLoc, BYTEPN))
		goto err;
	if (dbi->mdel != BTREEPN &&	/* index is being updated */
	    !TXbtreeChangeLocOrKey(dbi->mdel, &dbtbl->recid,
			sizeof(dbtbl->recid), dbtbl->recid, newLoc, BYTEPN))
		goto err;

	/* Update token file: */
	if (!fdbi_updatetokaux(fdbi, dbtbl->recid, NULL, newLoc))
		goto err;

	ret = 1;				/* success */
	goto done;

err:
	ret = 0;				/* error */
done:
	return(ret);
}
