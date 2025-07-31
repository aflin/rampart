/* -=- kai-mode: john -=- */
#ifdef TEST
#  define TEST2
#endif

#include "txcoreconfig.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sys/types.h"
#include <errno.h>
#ifdef USE_EPI
#  include "sizes.h"
#  include "os.h"
#else
#  define mac_dump()
#  define mac_ovchk()
#  ifdef __BORLANDC__
#     define off_t long
#  endif
#endif
#include "dbquery.h"
#include "fldops.h"
#include "texint.h"
#include "cgi.h"

#define AUTOPROMOTE 1

#ifndef MAX
#  define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#ifdef FMVERBOSE
#  define vb(a) a
#else
#  define vb(a)
#endif
#define vb1(a)                 vb(fprintf(stderr,a))
#define vb2(a,b)               vb(fprintf(stderr,a,b))
#define vb3(a,b,c)             vb(fprintf(stderr,a,b,c))
#define vb4(a,b,c,d)           vb(fprintf(stderr,a,b,c,d))
#define vb5(a,b,c,d,e)         vb(fprintf(stderr,a,b,c,d,e))
#define vb6(a,b,c,d,e,f)       vb(fprintf(stderr,a,b,c,d,e,f))
#define vb7(a,b,c,d,e,f,g)     vb(fprintf(stderr,a,b,c,d,e,f,g))
#define vb8(a,b,c,d,e,f,g,h)   vb(fprintf(stderr,a,b,c,d,e,f,g,h))
#define vb9(a,b,c,d,e,f,g,h,i) vb(fprintf(stderr,a,b,c,d,e,f,g,h,i))

/**********************************************************************/

int TXfldmathverb = 0;
int TXfldmathVerboseMaxValueSize = 40;
TXbool	TXfldmathVerboseHexInts = TXbool_False;

static CONST char * CONST fldopnames[FOP_LAST+1] = {
	"ok",
	"+",
	"-",
	"*",
	"/",
	"MOD",
	"Convert",
	"Assign",
	"==",
	"<",
	"<=",
	">",
	">=",
	"&&",
	"||",
	"!=",
	"LIKE",
	"LIKE3",
	"MATCHES",
	"LIKER",
	"LIKEP",
	"IN",
	"CMP",
	"LIKEIN",
	"between",
	"IS SUBSET OF",
	"INTERSECT",
	"INTERSECT IS EMPTY WITH",
	"INTERSECT IS NOT EMPTY WITH",
};


CONST char *
TXfldopname(int op)
{
	switch (op)
	{
	case FOP_EINVAL:	return("FOP_EINVAL");
	case FOP_ENOMEM:	return("FOP_ENOMEM");
	case FOP_ESTACK:	return("FOP_ESTACK");
	case FOP_EDOMAIN:	return("FOP_EDOMAIN");
	case FOP_ERANGE:	return("FOP_ERANGE");
	case FOP_EUNKNOWN:	return("FOP_EUNKNOWN");
	case FOP_EILLEGAL:	return("FOP_EILLEGAL");
	default:		if (op < 0) return("unknown-error");
	}
	op &= 0x7f;
	if (op <= FOP_LAST) return(fldopnames[op & 0x7f]);
	return("unknown-op");
}

typedef struct TXfldFuncNameItem_tag
{
	fop_type	func;
	CONST char	*name;
}
TXfldFuncNameItem;
#define TXfldFuncNameItemPN	((TXfldFuncNameItem *)NULL)

/* List of all fldmath functions, for mapping function pointers to names
 * for TXdumpPred() etc.:
 */
static CONST TXfldFuncNameItem	TXfldFuncNameList[] =
{
#undef I
#define I(type1, type2, func)	{ func, #func },
TX_FLDOP_SYMBOLS_LIST
TX_TEXIS_EXTRA_FLDOP_SYMBOLS_LIST
#undef I
};

CONST char *
TXfldFuncName(func)
fop_type	func;
/* Returns name of fldmath function `func', or "unknown" if unknown.
 */
{
	CONST TXfldFuncNameItem	*f, *e;

	e = TXfldFuncNameList + TX_ARRAY_LEN(TXfldFuncNameList);
	for (f = TXfldFuncNameList; f < e; f++)
		if (f->func == func)
			return(f->name);
	return("unknown");
}

/**********************************************************************/
static int growstack ARGS((FLDSTK *));

static int
growstack(fs)
FLDSTK *fs;
{
	FLD *nf;
	byte *nflg, *nmine;

	if (fs->numUsed == fs->numAlloced)
	{
		int i;

		vb1(", growing stack");
		if ((nf = (FLD *)TXcalloc(TXPMBUFPN, __FUNCTION__,
					  fs->numAlloced + FLDSTKINC,
					  sizeof(FLD))) == FLDPN)
		{
			return (FOP_ENOMEM);
		}
		/*
		   It looks as if people occasionally look at the flag for
		   the next item on the stack, even if the stack is full,
		   so add another for safety.
		*/
		if ((nflg = (byte *)TXcalloc(TXPMBUFPN, __FUNCTION__,
			 fs->numAlloced + FLDSTKINC + 1, 1)) == (byte *) NULL)
		{
			nf = TXfree(nf);
			return (FOP_ENOMEM);
		}
		if ((nmine = (byte *)TXcalloc(TXPMBUFPN, __FUNCTION__,
					   fs->numAlloced + FLDSTKINC, 1)) ==
		    (byte *) NULL)
		{
			nf = TXfree(nf);
			nf = TXfree(nflg);
			return (FOP_ENOMEM);
		}
		memcpy(nf, fs->f, fs->numAlloced * sizeof(FLD));
		memcpy(nflg, fs->flg, fs->numAlloced);
		memcpy(nmine, fs->mine, fs->numAlloced);
		fs->f = TXfree(fs->f);
		fs->flg = TXfree(fs->flg);
		fs->mine = TXfree(fs->mine);
		fs->f = nf;
		fs->flg = nflg;
		fs->mine = nmine;
		fs->numAlloced += FLDSTKINC;
		for (i = fs->numUsed; i < fs->numAlloced; i++)
		{
			clearfld(&nf[i]);
			nflg[i] = 0;
			nmine[i] = 0;
		}
	}
	return (0);
}				/* end growstack() */

/**********************************************************************/

/**********************************************************************/
int
fspush(FLDSTK *fs, FLD *f)
{
	return fspush2(fs, f, 0);
}

/**********************************************************************/

int
fspush2(FLDSTK *fs, FLD *f, int mine)
{
	FLD *nf;
	int rc;

	vb3("fspush: v=%lx, alloced=%d", f->v, f->alloced);
	if ((rc = growstack(fs)) != 0)
		return (rc);
	nf = fs->f;
	nf += fs->numUsed;
	if (fs->mine[fs->numUsed])
	{
		freeflddata(nf);
		setfld(nf, NULL, 0);
		fs->mine[fs->numUsed] = 0;
	}
	*nf = *f;
	fs->mine[fs->numUsed] = mine;
	if (nf->storage
#ifndef ALLOW_MISSING_FIELDS
	    || (FLD_IS_COMPUTED(nf))
#endif
		)
	{
		TXsetshadownonalloc(nf);
	}
	if (rc)
		return rc;
	fs->numUsed += 1;
	if (fs->numUsed < fs->numAlloced)
	{
		/* Flag next field above as not yet pushed; see fsnmark(): */
		fs->f[fs->numUsed].type = 0;
		fs->flg[fs->numUsed] = 0;
	}
	vb3(",out v=%lx, alloced=%d\n", nf->v, nf->alloced);
	return (0);
}				/* end fspush() */

/**********************************************************************/

/**********************************************************************/
int
fsmark(fs)			/* put a mark on the stack */
FLDSTK *fs;
{
	int rc;

	if ((rc = growstack(fs)) != 0)
		return (rc);
	fs->flg[fs->numUsed] += 1;
	return (0);
}				/* end fsmark() */

/**********************************************************************/

/**********************************************************************/
int
fsnmark(fs)			/* how many items on stack to mark or start */
FLDSTK *fs;
/* Returns number of items on top of stack above the last marked item
 * (assumes there is at least one marked item?).
 */
{
	int i = fs->numUsed;
	byte *flg = fs->flg;

	if (i == 0)
		return (0);	/* no stack */
	if (i < fs->numAlloced && flg[i] != 0 && fs->f[i].type == 0)
		return (0);	/* marked, but not pushed yet */
	for (i--; i > 0 && flg[i] == 0; i--);
	return (fs->numUsed - i);
}				/* end fsnmark() */

/**********************************************************************/

/**********************************************************************/
FLD *
fspop(fs)
FLDSTK *fs;
{
	FLD *f;

	if (fs->numUsed > 0)
	{
		fs->numUsed -= 1;
		vb3("fspop in : v=%lx, alloced=%d\n", (fs->f + fs->numUsed)->v,
		    (fs->f + fs->numUsed)->alloced);
		if (fs->mine[fs->numUsed])
		{
			f = (FLD *)TXmalloc(TXPMBUFPN, __FUNCTION__,
					    sizeof(FLD));
			*f = *(fs->f + fs->numUsed);
			f->storage = NULL;
			f->memory = NULL;
			fs->mine[fs->numUsed] = 0;
		}
		else
			f = dupfld(fs->f + fs->numUsed);
		fs->f[fs->numUsed].type = 0;
		fs->lastflg = fs->flg[fs->numUsed];
		if (fs->flg[fs->numUsed] > 0)
			fs->flg[fs->numUsed] -= 1;
		vb3("fspop out: v=%lx, alloced=%d\n", f->v, f->alloced);
	}
	else
	{
		f = FLDPN;
		putmsg(MERR, "fspop", "Internal error.  FLDMATH stack empty.");
	}
	return (f);
}				/* end fspop() */

/**********************************************************************/

/**********************************************************************/
int
fsdisc(fs)			/* discard an item on the stack - pop it but don't dup it */
FLDSTK *fs;
{
	FLD *nf;

	/* MAW 11-11-93 - unmacro so it can handle marks */
	/* was #define fsdisc(a)  ((a)->numUsed>0?(a)->numUsed-=1,0:FOP_ESTACK) */
	if (fs->numUsed == 0)
		return (FOP_ESTACK);
	fs->numUsed -= 1;
	nf = fs->f + fs->numUsed;
	if (FLD_IS_COMPUTED(nf))
	{
		TXfreefldshadow(nf);
	}
	else if (fs->mine[fs->numUsed])
	{
		freeflddata(nf);
		if (nf->fldlist)
		{
			nf->fldlist = TXfree(nf->fldlist);
			nf->vfc = 0;
		}
		setfld(nf, NULL, 0);
		fs->mine[fs->numUsed] = 0;
	}
	fs->f[fs->numUsed].type = 0;
	fs->lastflg = fs->flg[fs->numUsed];
	if (fs->flg[fs->numUsed] > 0)
		fs->flg[fs->numUsed] -= 1;
	return (0);
}				/* end fsdisc() */

/**********************************************************************/

/**********************************************************************/
FLDSTK *
fsclose(fs)
FLDSTK *fs;
{
	int i;
	FLD *f;

	if (fs == FLDSTKPN) goto done;

	if (fs->f != FLDPN)
	{
		for (f = fs->f, i = 0; i < fs->numAlloced; i++)
		{
			if (fs->mine[i])
				setfld(&f[i], NULL, 0);
			else
				f[i].v = f[i].shadow = NULL;
		}
		fs->f = TXfree(fs->f);
	}
	fs->flg = TXfree(fs->flg);
	fs->mine = TXfree(fs->mine);
	fs = TXfree(fs);
done:
	return (FLDSTKPN);
}				/* end fsclose() */

/**********************************************************************/

/**********************************************************************/
FLDSTK *
TXfsopen()
{
	FLDSTK *fs;

	if ((fs = (FLDSTK *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1,
				     sizeof(FLDSTK))) != FLDSTKPN)
	{
		if ((fs->f = (FLD *)TXcalloc(TXPMBUFPN, __FUNCTION__,
					     FLDSTKINC, sizeof(FLD))) == FLDPN)
			goto err;
		if ((fs->flg = (byte *)TXcalloc(TXPMBUFPN, __FUNCTION__,
						FLDSTKINC, 1)) == (byte *)NULL)
			goto err;
		if ((fs->mine = (byte *)TXcalloc(TXPMBUFPN, __FUNCTION__,
					     FLDSTKINC, 1)) == (byte *)NULL)
			goto err;
		fs->numAlloced = FLDSTKINC;
		fs->numUsed = 0;
		fs->lastflg = 0;
	}
	else
	{
	err:
		fs = fsclose(fs);
	}
	return (fs);
}				/* end TXfsopen() */

/**********************************************************************/

/**********************************************************************/
/**********************************************************************/
/**********************************************************************/

#if FO_NTTBL==1
/*#define t2i(f,a,b) (((a)&DDTYPEBITS)-1)*(f)->ntypes+(((b)&DDTYPEBITS)-1)*/
#define t2i(f,a,b) ((f)->row[((a)&DDTYPEBITS)-1]+((b)&DDTYPEBITS)-1)
#else
static int t2i ARGS((FLDOP * fo, int type1, int type2));

/**********************************************************************/
static int
t2i(fo, type1, type2)		/* get op table index for types */
FLDOP *fo;
int type1;
int type2;
{
	int i;

	i = 0;
#if FO_NTTBL!=1
	if (type1 & DDVARBIT)
		i += fo->tblsz;
	if (type2 & DDVARBIT)
		i += 2 * fo->tblsz;
#endif
	type1 &= DDTYPEBITS;	/* type number */
	type1--;		/* adjust to 0 base */
	type2 &= DDTYPEBITS;
	type2--;
	/*i+=type1*fo->ntypes+type2; */
	i += fo->row[type1] + type2;
	return (i);
}				/* end t2i() */

/**********************************************************************/
#endif /* FO_NTTBL==1 */

static int foaddtypes ARGS((FLDOP * fo, int nxtypes));

/**********************************************************************/
static int
foaddtypes(fo, n)
FLDOP *fo;
int n;
/* Returns 0 on success, nonzero (FOP_...) on error.
 */
{
	fop_type *ops, *d, *s;
	int *row;
	int ntypes, tblsz;
	int x, y, z;

	ntypes = fo->ntypes + n;
	tblsz = ntypes * ntypes;
	if ((ops = (fop_type *)TXcalloc(TXPMBUFPN, __FUNCTION__,
					tblsz * FO_NTTBL, sizeof(fop_type))) ==
	    (fop_type *) NULL)
		goto maerr;
	if ((row = (int *)TXcalloc(TXPMBUFPN, __FUNCTION__,
				   ntypes * FO_NTTBL, sizeof(int))) ==
	    (int *) NULL)
	{
		ops = TXfree(ops);
		goto maerr;
	}
	for (z = 0, s = fo->ops, d = ops; z < FO_NTTBL; z++)
	{
		for (y = 0; y < fo->ntypes; y++)
		{
			for (x = 0; x < fo->ntypes; x++, s++, d++)
			{
				*d = *s;
			}
			for (; x < ntypes; x++, d++)
			{
				*d = (fop_type) NULL;
			}
		}
		for (; y < ntypes; y++)
		{
			for (x = 0; x < ntypes; x++, d++)
			{
				*d = (fop_type) NULL;
			}
		}
	}
	fo->ops = TXfree(fo->ops);
	fo->ops = ops;
	for (x = 0, y = 0; x < ntypes * FO_NTTBL; x++, y += ntypes)
		row[x] = y;
	fo->row = TXfree(fo->row);
	fo->row = row;
	fo->ntypes = ntypes;
#if FO_NTTBL!=1
	fo->tblsz = tblsz;
#endif
	return(FOP_EOK);
maerr:
	return(FOP_ENOMEM);
}				/* end foaddtypes() */

/**********************************************************************/

/**********************************************************************/
int
fosetop(fo, type1, type2, func, pfunc)
FLDOP *fo;
int type1;
int type2;
fop_type func;
fop_type *pfunc;
/* Returns 0 on success, nonzero (FOP_...) on error.
 */
{
	int rc = 0;
	int i;

	type1 &= DDTYPEBITS;
	type2 &= DDTYPEBITS;
	i = MAX(type1, type2);
	if (i > fo->ntypes)
	{
		rc = foaddtypes(fo, i - fo->ntypes);
		if (rc != 0)
			return (rc);		/* error */
	}
	i = t2i(fo, type1, type2);
	if (pfunc != (fop_type *) NULL)
		*pfunc = fo->ops[i];
	fo->ops[i] = func;
	return (0);				/* success */
}				/* end fosetop() */

/**********************************************************************/

#define HANDLER(a,b) (fo->ops[t2i(fo,(a)->type,(b)->type)])

/**********************************************************************/

int
fogetop(fo, type1, type2, pfunc)
FLDOP *fo;
int type1;
int type2;
fop_type *pfunc;
{
	int i;

	type1 &= DDTYPEBITS;
	type2 &= DDTYPEBITS;
	i = MAX(type1, type2);
	if (i > fo->ntypes)
	{
		return -1;
	}
	i = t2i(fo, type1, type2);
	if (pfunc != (fop_type *) NULL)
		*pfunc = fo->ops[i];
	return (0);
}				/* end fogetop() */

/**********************************************************************/

#if AUTOPROMOTE
static int cnvmsg ARGS((FLD *fs, FLD *fd, int showVal));
static int
cnvmsg(fs, fd, showVal)
FLD	*fs;	/* (in) source field */
FLD	*fd;	/* (in) dest field */
int	showVal;/* (in) nonzero: show value too */
{
	putmsg(MINFO, NULL,
		"Converting type %s(%d) to %s(%d)%s%+.*s%s%+.*s%s",
		TXfldtypestr(fs), (int)fs->n,
		TXfldtypestr(fd), (int)fd->n,
		(showVal ? " [" : ""),
	       TXfldmathVerboseMaxValueSize,
		(showVal ? fldtostr(fs) : ""),
		(showVal ? "] to [" : ""),
	       TXfldmathVerboseMaxValueSize,
	       (showVal ? fldtostr(fd) : ""),
		(showVal ? "]" : ""));
	return(0);
}

int
TXfldresultmsg(pfx, arg, fr, res, verb)
CONST char	*pfx;	/* (in) prefix message */
CONST char	*arg;	/* (in) arg message */
FLD		*fr;	/* (in) result field */
int		res;	/* (in) result code */
int		verb;	/* (in) nonzero: verbose */
{
	putmsg(MINFO, NULL,
		"%s%s result is type %s(%d) code %d=%s%s%+.*s%s",
		pfx, arg, TXfldtypestr(fr), (int)fr->n,
	       res, TXfldopname(res), (verb ? " [" : ""),
	       TXfldmathVerboseMaxValueSize,
		(verb ? fldtostr(fr) : ""),
		(verb ? "]" : ""));
	return(0);
}

/* We do not do promotion on non-FOP_EINVAL errors; Bug 6918 also for
 * second promotion, to avoid changing type from retoptype() at run-time
 * (e.g. int 11 % float 0):
 */
#define CONVERTABLE_ERROR(rc)	(rc == FOP_EINVAL)

static int promop ARGS((FLDOP * fo, FLD * f1, FLD * f2, FLD * f3, int op));

/**********************************************************************/
static int
promop(fo, f1, f2, f3, op)	/* promote f2 or f1, then perform op */
FLDOP *fo;
FLD *f1, *f2, *f3;
int op;
{
	static CONST char	fn[] = "promop";
	int rc = FOP_EINVAL;
	fop_type func;
	FLD *ft;
	FOP	rcFop = FOP_EUNKNOWN;

	if (f1->type == f2->type)
		return (FOP_EINVAL);
	/* wtf - should i consider elsz when promoting? */
	/* convert f2 to f1's type and try op */
	if ((func = HANDLER(f2, f1)) != (fop_type) NULL)
	{
#ifndef NO_CACHE_CONSTANTS
		if (fo->tf2 && fo->owntf2)
		{
			TXmakesimfield(f1, fo->tf2);
			ft = fo->tf2;
			goto promote2;
		} else
#endif /* NO_CACHE_CONSTANTS */
		if ((ft = newfld(f1)) == FLDPN)
		{
			rc = FOP_ENOMEM;
			goto finally;
		}
promote2:
		if (TXfldmathverb >= 3)
			cnvmsg(f2, ft, 1);
		rc = (*func) (f2, ft, ft, (rcFop = FOP_CNV));
		if (TXfldmathverb >= 3)
			TXfldresultmsg("Convert", "", ft, rc, 1);
		if (rc == FOP_EOK &&
		    (func = HANDLER(f1, ft)) != (fop_type) NULL)
			rc = (*func) (f1, ft, f3, (rcFop = op));
#ifndef NO_CACHE_CONSTANTS
		fo->tf2 = ft;
		fo->owntf2 = 1;
		if (!CONVERTABLE_ERROR(rc))
			fo->hadtf2 = 1;
#else /* NO_CACHE_CONSTANTS */
		closefld(ft);
#endif /* NO_CACHE_CONSTANTS */
	}
	else
	{
		if (TXfldmathverb >= 3)
			putmsg(MINFO, fn, "No handler for %s/%s",
				TXfldtypestr(f2), TXfldtypestr(f1));
	}
	if (CONVERTABLE_ERROR(rc))
	{
		if (TXfldmathverb >= 3)
			putmsg(MINFO, CHARPN,
	"Fldmath op %s=%d returned %d=%s, will convert arg1 to arg2 type",
			       TXfldopname(rcFop), (rcFop & 0x7f),
			       rc, TXfldopname(rc));
		/* convert f1 to f2's type and try op */
		if ((func = HANDLER(f1, f2)) != (fop_type) NULL)
		{
#ifndef NO_CACHE_CONSTANTS
			if (fo->tf1 && fo->owntf1)
			{
				TXmakesimfield(f2, fo->tf1);
				ft = fo->tf1;
				goto promote1;
			} else
#endif /* NO_CACHE_CONSTANTS */
			if ((ft = newfld(f2)) == FLDPN)
				rc = FOP_ENOMEM;
			else
			{
promote1:
				if (TXfldmathverb >= 3)
					cnvmsg(f1, ft, 1);
				rc = (*func) (f1, ft, ft, FOP_CNV);
				if (TXfldmathverb >= 3)
					TXfldresultmsg("Convert", "", ft, rc,
						       1);
				/* JMT 04-29-94 - get correct handler */
				if (rc == 0
				    && (func =
					HANDLER(ft, f2)) != (fop_type) NULL)
					rc = (*func) (ft, f2, f3, op);
#ifndef NO_CACHE_CONSTANTS
				fo->tf1 = ft;
				fo->owntf1 = 1;
				if (rc >= 0)
					fo->hadtf1 = 1;
#else /* NO_CACHE_CONSTANTS */
				closefld(ft);
#endif /* NO_CACHE_CONSTANTS */
			}
		}
		else
		{
			if (TXfldmathverb >= 3)
				putmsg(MINFO, fn, "No handler for %s/%s",
					TXfldtypestr(f1), TXfldtypestr(f2));
		}
	}
finally:
	return (rc);
}				/* end promop() */

/**********************************************************************/
#else /* AUTOPROMOTE */
#  define promop(a,b,c,d,e) FOP_EINVAL
#endif /* AUTOPROMOTE */

static FLD emptyintfld;

int
TXfldmathopmsg(f1, f2, op, opName)
FLD	*f1;	/* (in) left operand */
FLD	*f2;	/* (in) right operand */
int	op;	/* (in) FOP_... operator */
CONST char	*opName;/* (in, opt.) name for `op' instead */
{
	char	f2nbuf[EPI_OS_LONG_BITS/3 + 20];
	int	f2isddmmapi = 0, showVal = (TXfldmathverb >= 2);
	DDMMAPI	*ddmmapi = NULL;

	if (f2->n == sizeof(DDMMAPI) && (f2->type & DDTYPEBITS) == FTN_CHAR)
		switch (op)
		{
		case FOP_MM:
		case FOP_NMM:
		case FOP_RELEV:
		case FOP_PROXIM:
		case FOP_MMIN:
			f2isddmmapi = 1;
			ddmmapi = (DDMMAPI *)f2->v;
			break;
		}
	if (f2isddmmapi)
		strcpy(f2nbuf, "sizeof(DDMMAPI)");
	else
		sprintf(f2nbuf, "%ld", (long)f2->n);

        if (opName == CHARPN)
		opName = TXfldopname(op);
	putmsg(MINFO, NULL,
		"Fldmath op %s=%d %s(%d) %s(%s)%s%+.*s%s%+.*s%s",
		opName, (int)(op & 0x7f),
		TXfldtypestr(f1), (int)f1->n,
		TXfldtypestr(f2), f2nbuf,
		(showVal ? " [" : ""),
	        TXfldmathVerboseMaxValueSize,
		(showVal ? fldtostr(f1) : ""),
		(showVal ? "] [" : ""),
	        TXfldmathVerboseMaxValueSize,
	        (showVal ? (f2isddmmapi ? (ddmmapi && ddmmapi->query ? ddmmapi->query : "DDMMAPI") : fldtostr(f2)) : ""),
		(showVal ? "]" : ""));
	return(0);
}

/**********************************************************************/
int
foop2(FLDOP *fo, int op, FLD *f3, fop_type *infunc)
{
	FLD *f1, *f2;

	FLD *ft, tf3;

	int rc;
	int f3mine;
	fop_type func;

	fo->hadtf1 = 0;
	fo->hadtf2 = 0;
	if(!f3)
	{
		tf3 = emptyintfld;
		f3 = &tf3;
		f3mine = 1;
	}
	else
	{
		f3mine = 0;
	}
#ifdef FO_SLOWSTACK
	f1 = fopeek2(fo);
	f2 = fopeek(fo);
	if (f1 == FLDPN || f2 == FLDPN)
		rc = FOP_ESTACK;
	else
	{
		fodisc(fo);
#else /* optimized inline version of above */
	if (fo->fs->numUsed < 2)
		rc = FOP_ESTACK;
	else
	{
		fo->fs->numUsed -= 1;
		f2 = fo->fs->f + fo->fs->numUsed;
		f1 = f2 - 1;
#endif
		if (TXfldmathverb >= 1)
			TXfldmathopmsg(f1, f2, op, CHARPN);
		vb5("foop: v1=%lx, alloced1=%d, v2=%lx, alloced2=%d\n", f1->v,
		    f1->alloced, f2->v, f2->alloced);
		if(infunc && *infunc)
		{
#ifdef TX_DEBUG
			func = HANDLER(f1, f2);
			if (func != *infunc)
			{
				putmsg(MERR, "foop2",
				       "Debug error: func != *infunc");
				return(FOP_EINVAL);
			}
#else
			func = *infunc;
#endif
		}
		else
		{
			func = HANDLER(f1, f2);
		}
		if (func == (fop_type) NULL)
		{
			if (TXfldmathverb >= 3)
				putmsg(MINFO, CHARPN,
			"No handler for %s/%s, will convert arg2 to arg1 type",
					TXfldtypestr(f1), TXfldtypestr(f2));
			rc = promop(fo, f1, f2, f3, op);
			fo->fs->f[fo->fs->numUsed - 1] = *f3;
			TXfsSetMineTop(fo->fs, f3mine);
		}
		else
		{
			if(infunc)
			{
				*infunc = func;
			}
			rc = (*func) (f1, f2, f3, op);
			if (CONVERTABLE_ERROR(rc))
			{
				if (TXfldmathverb >= 3)
					putmsg(MINFO, CHARPN,
	       "Fldmath op %s=%d returned %s, will convert arg2 to arg1 type",
					       TXfldopname(op), (op & 0x7f),
					       TXfldopname(rc));
				rc = promop(fo, f1, f2, f3, op);
			}
			ft = &fo->fs->f[fo->fs->numUsed - 1];
			if (TXfsIsMineTop(fo->fs))
			{
				freeflddata(ft);
				TXfreefldshadownotblob(ft);
			}
			fo->fs->f[fo->fs->numUsed - 1] = *f3;
			TXfsSetMineTop(fo->fs, f3mine);
		}
		fo->fs->f[fo->fs->numUsed].type = 0;
		fo->fs->flg[fo->fs->numUsed] = 0;
		if(TXfldmathverb >= 2)
		{
		putmsg(MINFO, NULL,
			"Fldmath op %s=%d result=%d=%s %s(%d) [%+.*s]",
		       TXfldopname(op), (int)(op & 0x7f), rc, TXfldopname(rc),
		       TXfldtypestr(f3), (int)f3->n,
		       TXfldmathVerboseMaxValueSize,
		       fldtostr(f3));
		}
	}
	return (rc);
}				/* end foop2() */

/**********************************************************************/
int
foop(fo, op)
FLDOP *fo;
int op;
{
	FLD *f1, *f2;

	FLD *ft;

	FLD f3;
	int rc;
	fop_type func;

	fo->hadtf1 = 0;
	fo->hadtf2 = 0;
	f3 = emptyintfld;
#ifdef FO_SLOWSTACK
	f1 = fopeek2(fo);
	f2 = fopeek(fo);
	if (f1 == FLDPN || f2 == FLDPN)
		rc = FOP_ESTACK;
	else
	{
		fodisc(fo);
#else /* optimized inline version of above */
	if (fo->fs->numUsed < 2)
		rc = FOP_ESTACK;
	else
	{
		fo->fs->numUsed -= 1;
		f2 = fo->fs->f + fo->fs->numUsed;
		f1 = f2 - 1;
#endif
		if (TXfldmathverb >= 1)
			TXfldmathopmsg(f1, f2, op, CHARPN);
		vb5("foop: v1=%lx, alloced1=%d, v2=%lx, alloced2=%d\n", f1->v,
		    f1->alloced, f2->v, f2->alloced);
		if ((func = HANDLER(f1, f2)) == (fop_type) NULL)
		{
			if (TXfldmathverb >= 3)
				putmsg(MINFO, CHARPN,
			"No handler for %s/%s, will convert arg2 to arg1 type",
					TXfldtypestr(f1), TXfldtypestr(f2));
			rc = promop(fo, f1, f2, &f3, op);
			fo->fs->f[fo->fs->numUsed - 1] = f3;
			TXfsSetMineTop(fo->fs, 1);
		}
		else
		{
			rc = (*func) (f1, f2, &f3, op);
			if (CONVERTABLE_ERROR(rc))
			{
				if (TXfldmathverb >= 3)
					putmsg(MINFO, CHARPN,
	       "Fldmath op %s=%d returned %s, will convert arg2 to arg1 type",
					       TXfldopname(op), (op & 0x7f),
					       TXfldopname(rc));
				rc = promop(fo, f1, f2, &f3, op);
			}
			ft = &fo->fs->f[fo->fs->numUsed - 1];
			if (TXfsIsMineTop(fo->fs))
			{
				freeflddata(ft);
				TXfreefldshadownotblob(ft);
			}
			fo->fs->f[fo->fs->numUsed - 1] = f3;
			TXfsSetMineTop(fo->fs, 1);
		}
		fo->fs->f[fo->fs->numUsed].type = 0;
		fo->fs->flg[fo->fs->numUsed] = 0;
		if(TXfldmathverb >= 2)
			putmsg(MINFO, NULL,
			 "Fldmath op %s=%d result=%d=%s %s(%d) [%+.*s]",
			       TXfldopname(op), (int)(op & 0x7f), rc,
			       TXfldopname(rc),
				TXfldtypestr(&f3), (int)f3.n,
			       TXfldmathVerboseMaxValueSize,
			       fldtostr(&f3));
	}
	return (rc);
}				/* end foop() */

/**********************************************************************/

/**********************************************************************/
FLDFUNC *
fofunc(fo, fname)
FLDOP *fo;
char *fname;
{
	FLDFUNC *ff;
	int	l, r, i, cmp;

	l = 0;
	r = fo->nfldfuncs;
	while (l < r)			/* binary search */
	{
		i = (l + r)/2;
		ff = fo->fldfuncs + i;
		cmp = TXfldopFuncNameCompare(fname, ff->name);
		if (cmp < 0) r = i;
		else if (cmp > 0) l = i + 1;
		else return(ff);
	}
	return (FLDFUNCPN);
}				/* end fofunc() */

/**********************************************************************/

/**********************************************************************/
int
fofuncret(fo, fname)
FLDOP *fo;
char *fname;
{
	FLDFUNC *ff;

	if ((ff = fofunc(fo, fname)) == FLDFUNCPN)
		return (0);
	return (ff->rettype);
}				/* end fofuncret() */

/**********************************************************************/

/**********************************************************************/
static int foaddfuncs_cmp ARGS((CONST void *a, CONST void *b));
static int
foaddfuncs_cmp(a, b)
CONST void	*a;
CONST void	*b;
{
  return(TXfldopFuncNameCompare(((FLDFUNC *)a)->name, ((FLDFUNC *)b)->name));
}
/**********************************************************************/

/**********************************************************************/
int
foaddfuncs(fo, ff, n)
FLDOP *fo;
FLDFUNC *ff;
int n;
/* Returns -1 on error, 0 on success.
 */
{
#define FF_ALIGN  	32			/* over-alloc fudge */
#define FF_ALLOC_NUM(n)	((((n) + FF_ALIGN - 1) / FF_ALIGN) * FF_ALIGN)
	int i;
	size_t	a;
	FLDFUNC	*nf;

	i = fo->nfldfuncs;
	if (i + n > FF_ALLOC_NUM(i))		/* need to realloc */
	{
		a = sizeof(FLDFUNC)*FF_ALLOC_NUM(i + n);
		nf = (FLDFUNC *)(fo->fldfuncs == FLDFUNCPN ?
				 TXmalloc(TXPMBUFPN, __FUNCTION__, a) :
				 TXrealloc(TXPMBUFPN, __FUNCTION__,
					   fo->fldfuncs, a));
		if (nf == FLDFUNCPN)
		{
#ifndef EPI_REALLOC_FAIL_SAFE
			fo->fldfuncs = FLDFUNCPN;
			fo->nfldfuncs = 0;
#endif /* !EPI_REALLOC_FAIL_SAFE */
			return(-1);		/* error */
		}
		fo->fldfuncs = nf;
	}

	memcpy(fo->fldfuncs + i, ff, n*sizeof(FLDFUNC));/*copy caller table */
	fo->nfldfuncs += n;

#if TX_VERSION_MAJOR < 8
	if (TX_IPv6_ENABLED(TXApp))
	{
		FLDFUNC	*fldFunc = fo->fldfuncs + i;
		FLDFUNC	*fldFuncEnd = fo->fldfuncs + fo->nfldfuncs;
#  include "inetfuncs.h"
		/* Update inet2int() return type to varint: */
		for ( ; fldFunc < fldFuncEnd; fldFunc++)
			if (fldFunc->func == (int (*)(void))(void *)txfunc_inet2int)
			{
				fldFunc->rettype = FTN_varINT;
				break;
			}
	}
#endif /* TX_VERSION_MAJOR < 8 */

	/* Maintain sort order.  Optimization: TXdbfldfuncs already sorted: */
	if (i > 0 || ff != (FLDFUNC *)TXdbfldfuncs)
		qsort(fo->fldfuncs, fo->nfldfuncs, sizeof(FLDFUNC),
			foaddfuncs_cmp);
	return (0);
#undef FF_ALLOC_NUM
#undef FF_ALIGN
}				/* end foaddfunc() */

/**********************************************************************/

/**********************************************************************/
int
focall(fo, fname, pmbuf)	/* expects args pushed right to left */
FLDOP *fo;
char *fname;
TXPMBUF	*pmbuf;	/* where to send putmsgs (default NULL for putmsg()) */
{
	static CONST char	fn[] = "focall";
	int numStackArgs, rc, i;
	FLDFUNC *ff;
	FLD *f[MAXFLDARGS];	/* The fields that we will use to call the function */
	TXbool	freeFld[MAXFLDARGS];
	TXbool	freeFldShadow[MAXFLDARGS];
	FLD	*firstArgAndReturn = FLDPN;
	/* Bug 3968: clear promotion cache during function calls too,
	 * not just ..._OP operators (e.g. foop[2]()):
	 */
	fo->hadtf1 = 0;
	fo->hadtf2 = 0;

	numStackArgs = fonmark(fo);	/* get # pushes since last mark */
	if ((ff = fofunc(fo, fname)) == FLDFUNCPN)
	{
		txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
				"Unknown SQL function `%s'", fname);
		return (FOP_EINVAL);	/* unknown func */
	}
	if (numStackArgs < ff->minargs)
	{
		char	maxBuf[EPI_OS_INT_BITS/3 + 10];
		if (ff->maxargs != ff->minargs)
			htsnpf(maxBuf, sizeof(maxBuf), "-%d",
				(int)ff->maxargs);
		else
			*maxBuf = '\0';
		txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
		"Too few arguments (%d) for SQL function %s: Expected %d%s",
			       (int)numStackArgs, fname, (int)ff->minargs,
			       maxBuf);
		return (FOP_ESTACK);	/* too few args */
	}
	if (numStackArgs > MAXFLDARGS)		/* too many args */
	{
		txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
		"Too many arguments (%d) for SQL function %s: Limit is %d",
			       (int)numStackArgs, fname, (int)MAXFLDARGS);
		return (FOP_EDOMAIN);	/* too many args */
	}
	if (numStackArgs > ff->maxargs)		/* too many args this func */
	{
		char	minBuf[EPI_OS_INT_BITS/3 + 10];
		if (ff->minargs != ff->maxargs)
			htsnpf(minBuf, sizeof(minBuf), "%d-",
				(int)ff->minargs);
		else
			*minBuf = '\0';
		txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
		"Too many arguments (%d) for SQL function %s: Expected %s%d",
			       (int)numStackArgs, fname, minBuf,
			       (int)ff->maxargs);
		return(FOP_EDOMAIN);
	}
	for (i = 0; i < numStackArgs; i++)
	{			/* get the args */
		f[i] = fopeekn(fo, i + 1);
		freeFld[i] = TXbool_False;
		freeFldShadow[i] = (TXbool)TXfsIsMineN(fo->fs, i + 1);
		TXfsSetMineN(fo->fs, i + 1, 0);
#     ifdef AUTOPROMOTE		/* auto cast func args */
		if (ff->types[i] != 0 &&
		    (f[i]->type & FTN_VarBaseTypeMask) !=
		    (ff->types[i] & FTN_VarBaseTypeMask))
		{
			fop_type func;
			FLD *ft;

			if (TXfldmathverb >= 3)
				putmsg(MINFO, CHARPN,
	     "Arg #%d type %s(%d) not expected type %s, will convert [%+.*s]",
				       (int)i, TXfldtypestr(f[i]),(int)f[i]->n,
				       ddfttypename(ff->types[i]),
				       TXfldmathVerboseMaxValueSize,
				       fldtostr(f[i]));
			rc = FOP_EINVAL;
			if ((ft = emptyfld(ff->types[i], 1)) == FLDPN ||	/* scratch field */
			    (func = HANDLER(f[i], ft)) == (fop_type) NULL ||
			    (TXfldmathverb >= 3 ? cnvmsg(f[i], ft, 1) : 0) ||
			    (rc = (*func) (f[i], ft, ft, FOP_CNV),
				(TXfldmathverb >= 3 ?
				    TXfldresultmsg("Convert", "", ft, rc,
						   1) : 0), rc) != 0
				)
			{
				if (ft != FLDPN)
					closefld(ft);
				txpmbuf_putmsg(pmbuf, MWARN, fn,
	"Could not promote argument #%d to proper type for SQL function %s",
					(int)(i + 1), fname);
				return (rc);
			}
			if (freeFldShadow[i])
			{
				TXfreefldshadow(f[i]);
				freeFldShadow[i] = TXbool_False;
			}
			f[i] = ft;
			freeFld[i] = TXbool_True;
		}
#     endif
	}

	/* Clear unused args of varargs: */
	for (; i < ff->maxargs; i++)
	{
		f[i] = FLDPN;
		freeFld[i] = TXbool_False;
		freeFldShadow[i] = TXbool_False;
	}

	if (numStackArgs == 0)
	{			/* no args, no result */
		fo->fs->flg[fo->fs->numUsed] -= 1;
	}
	else
	{		/* cleanup args from stack, leaving one for result */
		fo->fs->numUsed -= numStackArgs - 1;
		fo->fs->flg[fo->fs->numUsed - 1] -= 1;
		/*
		   Had to allocate an extra item in flg stack for this
		*/
		fo->fs->flg[fo->fs->numUsed] = 0;
	}
	if (ff->maxargs > 0)
	{
		/* First arg to SQL functions is also modified by them
		 * as the return value, so ensure `firstArgAndReturn'
		 * is writable by duping if needed:
		 */
		if (!freeFld[0])
		{
			firstArgAndReturn = dupfld(f[0]);
			if (freeFldShadow[0])
				TXfreefldshadow(f[0]);
			freeFldShadow[0] = TXbool_False;
		}
		else
		{
			firstArgAndReturn = f[0];
			freeFld[0] = TXbool_False;
		}
	}
	switch (ff->maxargs)
	{
	case 0:
		rc = (*ff->func) ();
		break;
	case 1:
		rc = ((int (*) ARGS((FLD *)))(void *)ff->func)(firstArgAndReturn);
		break;
	case 2:
		rc = ((int (*) ARGS((FLD *, FLD *)))(void *)ff->func)(firstArgAndReturn, f[1]);
		break;
	case 3:
		rc = ((int (*) ARGS((FLD *, FLD *, FLD*)))(void *)ff->func)(firstArgAndReturn, f[1], f[2]);
		break;
	case 4:
		rc = ((int (*) ARGS((FLD *, FLD *, FLD *, FLD *)))(void *)ff->func)(firstArgAndReturn, f[1], f[2], f[3]);
		break;
	case 5:
		rc = ((int (*) ARGS((FLD *, FLD *, FLD *, FLD *, FLD *)))(void *)ff->func)(firstArgAndReturn, f[1], f[2], f[3], f[4]);
		break;
	default:
		rc = FOP_EINVAL;
		break;
	}
	if (TXfldmathverb >= 3)
		TXfldresultmsg("Function call ", fname, firstArgAndReturn, rc,
			       1);

    if (numStackArgs > 0)
    {        /* swap first and last args so result is on stack */
        int last = fo->fs->numUsed - 1;

        /* first check if we are replacing something that needs freeing -ajf 07/30/2025 */
        FLD *fldlast = &fo->fs->f[last];
        int i=0, freefld=0, freeshadow=0;
        for (;i<numStackArgs;i++)
        {
            if(f[i]==fldlast)
            {
                freefld    = freeFld[i];
                freeshadow = freeFldShadow[i];
                break;
            }
        }
        if(fldlast)
        {
            if (freefld)
                fldlast= closefld(fldlast);
            else if (freeshadow)
                TXfreefldshadow(fldlast);
        }

        fo->fs->f[last] = *firstArgAndReturn;
        TXfsSetMineTop(fo->fs, 1);
        firstArgAndReturn = TXfree(firstArgAndReturn);
    }
	for (i = 0; i < ff->maxargs; i++)
	{
		if (freeFld[i] && f[i])
			f[i] = closefld(f[i]);
		else if (freeFldShadow[i] && f[i] &&
			 /* Bug 3610: `f[i]' might somehow be our
			  * return val `fo->fs->f[fo->fs->numUsed-1]';
			  * only free if not so:
			  */
			 (fo->fs->numUsed <= 0 ||	/* stack empty */
			  f[i] != &fo->fs->f[fo->fs->numUsed - 1] || /* not on stk*/
			  !TXfsIsMineTop(fo->fs)))	/* not owned by stk */
			TXfreefldshadow(f[i]);
	}
	if (rc < 0)
	{
		TXbool	isLookup;

		/* Do not yap every row; wtf just lookup() for now,
		 * should be distinct check for each function:
		 */
		isLookup = (strcmp(fname, "lookup") == 0);
		if (!isLookup ||
		    !TXApp ||
		    !TXApp->didOncePerSqlMsg[TXoncePerSqlMsg_LookupFailed])
		{
			if (TXApp && isLookup)
				TXApp->didOncePerSqlMsg[TXoncePerSqlMsg_LookupFailed] = 1;
			txpmbuf_putmsg(pmbuf, MWARN, fn,
				       "SQL function %s failed", fname);
		}
	}
	return (rc);
}				/* end focall() */

/**********************************************************************/

/**********************************************************************/
FLDOP *
foclose(fo)
FLDOP *fo;
{
	vb1("foclose\n");
	if (fo == FLDOPPN) goto done;

	fo->row = TXfree(fo->row);
	fo->ops = TXfree(fo->ops);
	fo->fs = fsclose(fo->fs);
	if (fo->tf1 && fo->owntf1)
		fo->tf1 = closefld(fo->tf1);
	if (fo->tf2 && fo->owntf2)
		fo->tf2 = closefld(fo->tf2);
	fo->fldfuncs = TXfree(fo->fldfuncs);
	fo = TXfree(fo);
done:
	return (FLDOPPN);
}				/* end foclose() */

/**********************************************************************/

/**********************************************************************/
FLDOP *
foopen()
/* Returns NULL on error.
 */
{
	FLDOP *fo;
	int i, j, tblsz;

	vb1("foopen\n");
	if ((fo = (FLDOP *)TXcalloc(TXPMBUFPN, __FUNCTION__,
				    1, sizeof(FLDOP))) != FLDOPPN)
	{
		if (!initfld(&emptyintfld, FTN_LONG, 1)) goto err;
		fo->fs = FLDSTKPN;
		fo->ops = (fop_type *) NULL;
		fo->row = (int *) NULL;
		fo->nfldfuncs = 0;
		if ((fo->fs = TXfsopen()) == FLDSTKPN) goto err;
		fo->ntypes = FOP_NTYPES;
#     if FO_NTTBL!=1
		tblsz = fo->tblsz = fo->ntypes * fo->ntypes;
#     else
		tblsz = fo->ntypes * fo->ntypes;
#     endif
		fo->ops =
			(fop_type *)TXcalloc(TXPMBUFPN, __FUNCTION__,
					     tblsz * FO_NTTBL,
					     sizeof(fop_type));
		if (fo->ops == (fop_type *) NULL) goto err;
		for (i = 0; i < tblsz * FO_NTTBL; i++)
			fo->ops[i] = (fop_type) NULL;
		fo->row = (int *)TXcalloc(TXPMBUFPN, __FUNCTION__,
					  fo->ntypes * FO_NTTBL, sizeof(int));

		if (fo->row == (int *) NULL) goto err;
		for (i = 0, j = 0; i < fo->ntypes * FO_NTTBL;
		     i++, j += fo->ntypes) fo->row[i] = j;

		/* Add most functions via macro with TX_FLDOP_SYMBOLS_LIST: */
#undef I
#define I(type1, type2, func)	\
		fosetop(fo, (type1), (type2), func, (fop_type *)NULL);
TX_FLDOP_SYMBOLS_LIST
#undef I
		/* Additional types using TX_FLDOP_SYMBOLS_LIST functions: */
		fosetop(fo, FTN_INDIRECT, FTN_INDIRECT,fochch,(fop_type*)NULL);
		fosetop(fo, FTN_CHAR, FTN_INTEGER, fochin, (fop_type *) NULL);
		fosetop(fo, FTN_HANDLE, FTN_INTEGER, fohain, (fop_type *)NULL);
		fosetop(fo, FTN_INT64, FTN_INTEGER, foi6in, (fop_type *)NULL);
		fosetop(fo, FTN_FLOAT, FTN_INTEGER, foflin, (fop_type *)NULL);
		fosetop(fo, FTN_DOUBLE, FTN_INTEGER, fodoin, (fop_type *)NULL);
		fosetop(fo, FTN_INTEGER, FTN_CHAR, foinch, (fop_type *) NULL);
		fosetop(fo, FTN_INTEGER, FTN_DOUBLE, foindo,(fop_type *) NULL);
		fosetop(fo, FTN_INTEGER, FTN_FLOAT, foinfl,(fop_type *) NULL);
		fosetop(fo, FTN_INTEGER, FTN_HANDLE, foinha, (fop_type *)NULL);
		fosetop(fo, FTN_INTEGER, FTN_INT64, foini6, (fop_type *)NULL);
		fosetop(fo, FTN_INTEGER, FTN_UINT64, foinu6, (fop_type *)NULL);
		fosetop(fo, FTN_INTEGER, FTN_INT, foirir, (fop_type *) NULL);
		fosetop(fo, FTN_INT, FTN_INTEGER, foirir, (fop_type *) NULL);
		fosetop(fo, FTN_SHORT, FTN_SMALLINT, foshsh, (fop_type *)NULL);
		fosetop(fo, FTN_SMALLINT, FTN_SHORT, fosmsm, (fop_type *)NULL);
		fosetop(fo, FTN_UINT64, FTN_INTEGER, fou6in, (fop_type *)NULL);
		fosetop(fo, FTN_INTEGER, FTN_SHORT, foinsh, (fop_type *)NULL);
		fosetop(fo, FTN_SHORT, FTN_INTEGER, foshin, (fop_type *)NULL);
		fosetop(fo, FTN_INTEGER, FTN_SMALLINT,foinsh,(fop_type*)NULL);
		fosetop(fo, FTN_SMALLINT, FTN_INTEGER,foshin,(fop_type*)NULL);
		fosetop(fo, FTN_WORD, FTN_INTEGER, fowoin, (fop_type *)NULL);
		fosetop(fo, FTN_INTEGER, FTN_WORD, foinwo, (fop_type *)NULL);
		/* wtf rest of default ops... */
	}
	else
	{
	err:
		fo = foclose(fo);
	}
	return (fo);
}				/* end foopen() */

/******************************************************************/

int
TXfldmathReturnNull(FLD *f1, FLD *f3)
/* Helper function for fldmath functions to return a NULL FLD.
 * Sets `f3' to a NULL value of `f1' type.
 * Return FOP_EOK (0) on success, else < 0 (FOP_E...).
 */
{
	/* Set `f3' NULL before we check if it is legal,
	 * in case caller does not pay attention to err:
	 */
	TXmakesimfield(f1, f3);
	if (!TXfldSetNull(f3)) return(FOP_EUNKNOWN);
	if (TXfldType(f1) & FTN_NotNullableFlag) return(FOP_EILLEGAL);
	return(FOP_EOK);
}

/**********************************************************************/

#ifdef TEST
#include "signal.h"
/**********************************************************************/
static void
memtrap(sig)
int sig;
{
	char *s;

	switch (sig)
	{
	case SIGBUS:
		s = "bus error";
		break;
	case SIGSEGV:
		s = "seg violation";
		break;
	default:
		s = "unknown";
		break;
	}
	fprintf(stderr, "memory trap: %s (%d)\n", s, sig);
	mac_dump();
	mac_ovchk();
}				/* end memtrap() */

/**********************************************************************/
#endif

#ifdef TEST0
void main ARGS((int argc, char **argv));

/**********************************************************************/
void
main(argc, argv)
int argc;
char **argv;
{
	FLD *f;
	static ft_long l = 999999999;
	static FLD tl =
		{ FTN_LONG, (void *) &l, (void *) NULL, 1,
		1 * sizeof(ft_long), 0, sizeof(ft_long) };

	signal(SIGBUS, memtrap);
	signal(SIGSEGV, memtrap);
	f = dupfld(&tl);
	closefld(f);
	f = dupfld(&tl);
	closefld(f);
	f = dupfld(&tl);
	closefld(f);
	f = dupfld(&tl);
	closefld(f);
	mac_ovchk();
	exit(0);
}				/* end main() */

/**********************************************************************/
#endif

#ifdef TEST1			/* test the stack */
void main ARGS((int argc, char **argv));

/**********************************************************************/
void
main(argc, argv)
int argc;
char **argv;
{
/* #ifdef TEST1 */	static const char	nullStr[] = "NULL";
	FLDOP *fo;
	static FLD fl[] = {

			{FTN_CHAR | DDVARBIT, (void *) "zero", (void *) NULL,
		 4, 4, 0, 1},
		{FTN_CHAR | DDVARBIT, (void *) "one", (void *) NULL, 3, 3, 0,
		 1},
		{FTN_CHAR | DDVARBIT, (void *) "two", (void *) NULL, 3, 3, 0,
		 1},
		{FTN_CHAR | DDVARBIT, (void *) "three", (void *) NULL, 5, 5,
		 0, 1},
		{FTN_CHAR | DDVARBIT, (void *) "four", (void *) NULL, 4, 4, 0,
		 1},
	};
	FLD *f;

	fo = foopen();

	fopush(fo, &fl[0]);
	fopush(fo, &fl[1]);
	fopush(fo, &fl[2]);
	fopush(fo, &fl[3]);
	fopush(fo, &fl[4]);

	f = fopop(fo);
	printf("\"%s\"\n", f == FLDPN ? nullStr : f->v);
	closefld(f);
	f = fopop(fo);
	printf("\"%s\"\n", f == FLDPN ? nullStr : f->v);
	closefld(f);
	f = fopop(fo);
	printf("\"%s\"\n", f == FLDPN ? nullStr : f->v);
	closefld(f);
	f = fopop(fo);
	printf("\"%s\"\n", f == FLDPN ? nullStr : f->v);
	closefld(f);
	f = fopop(fo);
	printf("\"%s\"\n", f == FLDPN ? nullStr : f->v);
	closefld(f);
	f = fopop(fo);
	printf("\"%s\"\n", f == FLDPN ? nullStr : f->v);	/* should be NULL */
	closefld(f);

	foclose(fo);
	mac_ovchk();
	exit(0);
}				/* end main() */

/**********************************************************************/
#endif /* TEST1 */

#ifdef TEST2
#ifdef MSDOS
#  include "io.h"
#endif
#include "ctype.h"
/*#include "date.h"*/

struct mine
{
	int m1, m2;
};

#define FTN_MINE (FTN_LAST+1)
#define ft_mine  struct mine

#define FOP_XOR (FOP_LAST+1)

static double strtof ARGS((char *, char **, int));

/**********************************************************************/
static double
strtof(s, ap, b)
char *s, **ap;
int b;				/* assume base 10 only */
{
	double f = 0.0;
	char *p, sv;

	p = s;
	if (*p == '-' || *p == '+')
		p++;
	for (; isdigit(*p) || *p == '.' || tolower(*p) == 'e'; p++);
	sv = *p;
	*p = '\0';
	sscanf(s, "%lf", &f);
	*p = sv;
	if (ap != (char **) NULL)
		*ap = p;
	return (f);
}				/* end strtof() */

/**********************************************************************/

static int stkcnt ARGS((FLDOP * fo));

/**********************************************************************/
static int
stkcnt(fo)
FLDOP *fo;
{
	return (fo->fs->numUsed);
}				/* end stkcnt() */

/**********************************************************************/

static FLD *prt ARGS((FLD * f));

/**********************************************************************/
static FLD *
prt(f)
FLD *f;
{
	unsigned int i;

	if (f == FLDPN)
/* #ifdef TEST2 */	puts("NULL");
	else
	{
		switch (f->type & DDTYPEBITS)
		{
		case FTN_BYTE:
			printf("Byte   :");
			for (i = 0; i < f->n; i++)
				printf(" %lu",
				       (ulong) * (((ft_byte *) f->v) + i));
				break;
		case FTN_INT:
			printf("Int    :");
			for (i = 0; i < f->n; i++)
				printf(" %ld",
				       (long) *(((ft_int *) f->v) + i));
				break;
		case FTN_LONG:
			printf("Long   :");
			for (i = 0; i < f->n; i++)
				printf(" %ld",
				       (long) *(((ft_long *) f->v) + i));
				break;
		case FTN_CHAR:
			printf("Char   : \"");
			for (i = 0; i < f->n; i++)
				printf("%c",
				       (char) *(((ft_char *) f->v) + i));
				putchar('"');
			break;
		case FTN_FLOAT:
			printf("Float  :");
			for (i = 0; i < f->n; i++)
				printf(" %g",
				       (float) *(((ft_float *) f->v) + i));
				break;
		case FTN_DOUBLE:
			printf("Double :");
			for (i = 0; i < f->n; i++)
				printf(" %lg",
				       (double) *(((ft_double *) f->v) + i));
			break;
		case FTN_MINE:
			printf("Mine   :");
			printf(" %d,%d", ((ft_mine *) f->v)->m1,
			       ((ft_mine *) f->v)->m2);
			break;
/*
      case FTN_DATE  : printf("Date   :"); printf(" %lu days",((ft_date *)f->v)->days); break;
      case FTN_TIME  : printf("Time   :"); printf(" %u hours %lu usecs",((ft_time *)f->v)->hours,((ft_time *)f->v)->usecs); break;
*/
		case FTN_COUNTER:
			printf("Counter:");
			printf(" %08lx%lx", ((ft_counter *) f->v)->date,
			       ((ft_counter *) f->v)->seq);
			break;
		case FTN_STRLST:
			printf("Strlst :");
			{
				ft_strlst *sl = (ft_strlst *) f->v;
				char *s = sl->buf;
				char *e = s + sl->nb - 1;

				printf(" %d: ", sl->nb);
				for (; s < e; s++)
				{
					if (*s == '\0')
						putchar(sl->delim);
					else
						putchar(*s);
				}
				if (*s != '\0')
					printf("NO TERMINATOR!");
			}
			break;
		default:
			printf("Unknown: %x", f->type);
		}
		putchar('\n');
	}
	return (f);
}				/* end prt() */

/**********************************************************************/

static FLD *popprt ARGS((FLDOP * op));

/**********************************************************************/
static FLD *
popprt(fo)
FLDOP *fo;
{
	return (prt(fopop(fo)));
}				/* end popprt() */

/**********************************************************************/

static FLD *peekprt ARGS((FLDOP * op));

/**********************************************************************/
static FLD *
peekprt(fo)
FLDOP *fo;
{
	return (prt(fopeek(fo)));
}				/* end popprt() */

/**********************************************************************/

#define foerror(a,b) fomerror(a,b,(char *)NULL)
static void fomerror ARGS((int n, char *s, char *m));

/**********************************************************************/
static void
fomerror(n, s, m)
int n;
char *s;
char *m;
{
	static char *elst[] = {
		"FOP_EOK",
		"FOP_EINVAL",
		"FOP_ENOMEM",
		"FOP_ESTACK",
		"FOP_EDOMAIN",
		"FOP_ERANGE",
		"FOP_??",
		"FOP_??",
		"FOP_??",
	};

	printf(m ==
	       (char *) NULL ? "%s: error==%d %s\n" :
	       "%s: error==%d %s (%s)\n", s, n, elst[-n], m);
}				/* end foerror() */

/**********************************************************************/

/**********************************************************************/
int
fomimi(f1, f2, op)		/* a new type */
FLD *f1;
FLD *f2;
int op;
{
	ft_mine *vp1, *vp2;
	int rc = 0;

	vp1 = (ft_mine *) f1->v;
	vp2 = (ft_mine *) f2->v;
	switch (op)
	{
	case FOP_ADD:
		vp1->m1 += vp2->m1;
		vp1->m2 += vp2->m2;
		break;
	case FOP_SUB:
		vp1->m1 -= vp2->m1;
		vp1->m2 -= vp2->m2;
		break;
	case FOP_CNV:
		break;
	case FOP_ASN:
		vp1->m1 = vp2->m1;
		vp1->m2 = vp2->m2;
		break;
	case FOP_EQ:
		rc = fld2finv(f1, (vp1->m1 == vp2->m1 && vp1->m2 == vp2->m2));
		break;
	case FOP_NEQ:
		rc = fld2finv(f1, (vp1->m1 != vp2->m1 || vp1->m2 != vp2->m2));
		break;
	case FOP_LT:
		rc = fld2finv(f1, (vp1->m1 < vp2->m1 && vp1->m2 < vp2->m2));
		break;
	case FOP_LTE:
		rc = fld2finv(f1, (vp1->m1 <= vp2->m1 && vp1->m2 <= vp2->m2));
		break;
	case FOP_GT:
		rc = fld2finv(f1, (vp1->m1 > vp2->m1 && vp1->m2 > vp2->m2));
		break;
	case FOP_GTE:
		rc = fld2finv(f1, (vp1->m1 >= vp2->m1 && vp1->m2 >= vp2->m2));
		break;
	default:
		rc = FOP_EINVAL;
	}
	return (rc);
}				/* end fomimi() */

/**********************************************************************/

static fop_type o_finfin;

/**********************************************************************/
int
n_finfin(f1, f2, op)		/* add xor to existing int type */
FLD *f1;
FLD *f2;
int op;
{
	switch (op)
	{
	case FOP_XOR:
		*(ft_int *) f1->v ^= *(ft_int *) f2->v;
		break;
	default:
		return ((*o_finfin) (f1, f2, op));
	}
	return (0);
}

/**********************************************************************/

/**********************************************************************/
int
fsubstr(f1, f2, f3)
FLD *f1, *f2, *f3;
{
	int o, l;

	o = *(int *) f2->v;
	if (o > f1->size)
		o = f1->size;
	if (f3 == FLDPN)
		l = f1->size - o;
	else
	{
		l = *(int *) f3->v;
		if (l > f1->size - o)
			l = f1->size - o;
	}
	memmove((char *) f1->v, (char *) f1->v + o, l);
	*((char *) f1->v + l) = '\0';
	f1->n = f1->size = l;
	return (0);
}				/* end fsubstr() */

/**********************************************************************/

/**********************************************************************/
int
fstrlen(f1)
FLD *f1;
{
	return (fld2finv(f1, strlen(f1->v)));
}				/* end fstrlen() */

/**********************************************************************/
int
fnocare(f1)
FLD *f1;
{
	fputs("nocare called: ", stdout);
	prt(f1);
	return (0);
}				/* end fnocare() */

/**********************************************************************/
int
fnop()
{
	puts("fnop called");
	return (0);
}				/* end fnop() */

/**********************************************************************/
int
fsqr(f1)
FLD *f1;
{
	int *p;

	p = f1->v;
	*p *= *p;
	return (0);
}				/* end fsqr() */

/**********************************************************************/
FLDFUNC myfldfuncs[] = {

		{"substr", fsubstr, 2, 3, FTN_CHAR | DDVARBIT,
	 FTN_CHAR | DDVARBIT, FTN_INT, FTN_INT, 0, 0}
	,

		{"strlen", fstrlen, 1, 1, FTN_CHAR | DDVARBIT,
	 FTN_CHAR | DDVARBIT, 0, 0, 0, 0}
	,
	{"nocare", fnocare, 1, 1, 0, 0, 0, 0, 0, 0}
	,
	{"nop", fnop, 0, 0, 0, 0, 0, 0, 0, 0}
	,
	{"sqr", fsqr, 1, 1, FTN_INT, FTN_INT, 0, 0, 0, 0}
	,
};

#define NFLDFUNCS (sizeof(myfldfuncs)/sizeof(myfldfuncs[0]))


void main ARGS((int argc, char **argv));

/**********************************************************************/
void
main(argc, argv)
int argc;
char **argv;
{
	FLDOP *fo;
	FLD *f = FLDPN;
	static ft_int i1[1] = { 4 };
	static ft_int i3[3] = { 5, 6, 7 };
	static ft_long l1[1] = { 5 };
	static ft_long l3[3] = { 6, 7, 8 };
	static ft_char c1[1] = { 'a' };
	static ft_char c3[3] = { 'c', 'a', 'b' };
	static ft_char c7[7] = { 'a', 'b', 'a', 'c', 'a', 'b', 'a' };
	static ft_float f1[1] = { (ft_float) 3.14159 };
	static ft_double d1[1] = { (ft_double) 3.14159 };
	static ft_mine m1[1] = { 3, 5 };

/*
static ft_date   e1[1];
static ft_time   t1[1];
*/
	static ft_counter C1[1] = { 4, 2 };
	static ft_strlst S1[1] = { 9, ',', 'o' };
	static char _S1[] = { "ne\0two\0" };
	static ft_byte b1[1] = { 9 };

#define F(a,b,c,d) {a,(void*)b,(void*)NULL,c,c*sizeof(d),0,sizeof(d)}
	static FLD fi1 = F(FTN_INT, i1, 1, ft_int);
	static FLD fi3 = F(FTN_INT | DDVARBIT, i3, 3, ft_int);
	static FLD fl1 = F(FTN_LONG, l1, 1, ft_long);
	static FLD fl3 = F(FTN_LONG | DDVARBIT, l3, 3, ft_long);
	static FLD fc0 = F(FTN_CHAR | DDVARBIT, c1, 0, ft_char);
	static FLD fc1 = F(FTN_CHAR, c1, 1, ft_char);
	static FLD fc3 = F(FTN_CHAR, c3, 3, ft_char);
	static FLD fc7 = F(FTN_CHAR, c7, 7, ft_char);
	static FLD ff1 = F(FTN_FLOAT, f1, 1, ft_float);
	static FLD fd1 = F(FTN_DOUBLE, d1, 1, ft_double);
	static FLD fm1 = F(FTN_MINE, m1, 1, ft_mine);

/*
static FLD fe1=F(FTN_DATE           ,e1,1,ft_date  );
static FLD ft1=F(FTN_TIME           ,t1,1,ft_time  );
*/
	static FLD fb1 = F(FTN_BYTE, b1, 1, ft_byte);
	static FLD fC1 = F(FTN_COUNTER, C1, 1, ft_counter);
	static FLD fS1 = F(FTN_STRLST | DDVARBIT, S1, 1, ft_strlst);
	char buf[80], *p, *e, save;
	int tty = isatty(0), op, rc;

	if ((fo = foopen()) == FLDOPPN ||
	    foaddfuncs(fo, myfldfuncs, NFLDFUNCS) != 0)
	{
		puts("foopen failed - no mem");
		if (fo != FLDOPPN)
			foclose(fo);
	}
	else
	{
		if (fosetop(fo, FTN_MINE, FTN_MINE, fomimi, (fop_type *) NULL)
		    != 0
/* ||
         fosetop(fo,FTN_DATE,FTN_DATE,fodada,(fop_type *)NULL)!=0 ||
         fosetop(fo,FTN_TIME,FTN_TIME,fotiti,(fop_type *)NULL)!=0 ||
         fosetop(fo,FTN_DATM,FTN_DATM,fodtdt,(fop_type *)NULL)!=0 ||
         fosetop(fo,FTN_CHAR,FTN_TIME,fochti,(fop_type *)NULL)!=0 ||
         fosetop(fo,FTN_CHAR,FTN_DATE,fochda,(fop_type *)NULL)!=0 ||
         fosetop(fo,FTN_CHAR,FTN_DATM,fochdt,(fop_type *)NULL)!=0 ||
         fosetop(fo,FTN_TIME,FTN_CHAR,fotich,(fop_type *)NULL)!=0 ||
         fosetop(fo,FTN_DATE,FTN_CHAR,fodach,(fop_type *)NULL)!=0 ||
         fosetop(fo,FTN_DATM,FTN_CHAR,fodtch,(fop_type *)NULL)!=0
*/
			)
		{
			puts("couldn't add a type/op - no mem");
		}
		fosetop(fo, FTN_INT, FTN_INT, n_finfin, &o_finfin);

		while (printf("%d>", stkcnt(fo)), fflush(stdout),
		       (gets(buf) != (char *) NULL))
		{
			if (!tty)
				puts(buf);
			for (p = buf; *p != '\0'; p++)
			{
				op = -1;
				rc = 0;
				switch (*p)
				{
				case 'u':
					rc = fopush(fo, f);
					break;
				case 'p':
					peekprt(fo);
					break;
				case 'P':
					if (f != FLDPN)
						closefld(f);
					f = popprt(fo);
					break;
				case 'i':
					i1[0] =
						(ft_int) strtol(p + 1, &p,
								10);
						rc = fopush(fo, &fi1);
					p--;
					break;
				case 'I':
					i3[0] = i3[1] = i3[2] =
						(ft_int) strtol(p + 1, &p,
								10);
						rc = fopush(fo, &fi3);
					p--;
					break;
				case 'l':
					l1[0] =
						(ft_long) strtol(p + 1, &p,
								 10);
						rc = fopush(fo, &fl1);
					p--;
					break;
				case 'L':
					l3[0] = l3[1] = l3[2] =
						(ft_long) strtol(p + 1, &p,
								 10);
						rc = fopush(fo, &fl3);
					p--;
					break;
				case 'f':
					f1[0] =
						(ft_float) strtof(p + 1, &p,
								  10);
						rc = fopush(fo, &ff1);
					p--;
					break;
				case 'd':
					d1[0] =
						(ft_double) strtof(p + 1, &p,
								   10);
						rc = fopush(fo, &fd1);
					p--;
					break;
				case 'b':
					b1[0] =
						(ft_byte) strtol(p + 1, &p,
								 10);
						rc = fopush(fo, &fb1);
					p--;
					break;
				case 'm':
					rc = fopush(fo, &fm1);
					break;
/*
            case 'e': nowdate(e1); rc=fopush(fo,&fe1); break;
            case 't': nowtime(t1); rc=fopush(fo,&ft1); break;
*/
				case 'C':
					rc = fopush(fo, &fC1);
					break;
				case 'S':
					rc = fopush(fo, &fS1);
					break;
				case 's':
					switch (*++p)
					{
					case '1':
						rc = fopush(fo, &fc1);
						break;
					case '3':
						rc = fopush(fo, &fc3);
						break;
					case '7':
						rc = fopush(fo, &fc7);
						break;
					default:
						for (e = p + 1;
						     *e != '\0' && *e != *p;
						     e++);
						p++;
						fc0.n = fc0.size = e - p;
						fc0.v = p;
						rc = fopush(fo, &fc0);
						if (*e == '\0')
							e--;
						p = e;
						break;
					}
					break;
				case 'M':
					if ((rc = fomark(fo)) < 0)
					{
						foerror(rc, "focall()");
						rc = 0;
					}
					break;
				case 'F':
					p++;
					for (e = p + 1;
					     *e != '\0' && *e != *p; e++);
					p++;
					save = *e;
					*e = '\0';
					if ((rc = focall(fo, p)) < 0)
					{
						fomerror(rc, "focall()", p);
						rc = 0;
					}
					*e = save;
					if (*e == '\0')
						e--;
					p = e;
					break;
				case '+':
					op = FOP_ADD;
					break;
				case '-':
					op = FOP_SUB;
					break;
				case '*':
					op = FOP_MUL;
					break;
				case '/':
					op = FOP_DIV;
					break;
				case '%':
					op = FOP_MOD;
					break;
				case 'c':
					op = FOP_CNV;
					break;
				case 'a':
					op = FOP_ASN;
					break;
				case '=':
					op = FOP_EQ;
					break;
				case '!':
					op = FOP_NEQ;
					break;
				case '<':
					op = FOP_LT;
					break;
				case '>':
					op = FOP_GT;
					break;
				case '^':
					op = FOP_XOR;
					break;
				}
				if (rc != 0)
				{
					foerror(rc, "fopush()");
				}
				if (op != (-1))
				{
					if ((rc = foop(fo, op)) < 0)
					{
						foerror(rc, "foop()");
					}
				}
			}
		}
		putchar('\n');
		if (f != FLDPN)
			closefld(f);

		foclose(fo);
	}
	exit(0);
}				/* end main() */

/**********************************************************************/
#endif /* TEST2 */

#ifdef TEST3
/**********************************************************************/
#include "sys/stat.h"

#define TBLNM "tmp"

#define LNSZ 512

char ln[LNSZ];

void main ARGS((int argc, char **argv));

/**********************************************************************/
void
main(argc, argv)
int argc;
char **argv;
{
	TBL *tb = opentbl(TBLNM);
	int i;
	FLD *fnamef, *sizef, *mtimef, *linef;
	FLDOP *fo;
	FLD *tot;
	static ft_long tl = 0;
	static FLD t =
		{ FTN_LONG, (void *) &tl, (void *) NULL, 1,
		 1 * sizeof(ft_long), 0, sizeof(ft_long) };

	if (tb == TBLPN)
	{
		DD *dd;

		if ((dd = opendd()) == DDPN ||
		    !putdd(dd, "fname", "varchar", 65, 0) ||
		    !putdd(dd, "line", "varchar", 65, 0) ||
		    !putdd(dd, "size", "long", 1, 0) ||
		    !putdd(dd, "mtime", "long", 1, 0) ||
		    (tb = createtbl(dd, TBLNM)) == TBLPN)
			exit(0);
	}

	if ((fnamef = nametofld(tb, "fname")) == FLDPN ||
	    (mtimef = nametofld(tb, "mtime")) == FLDPN ||
	    (sizef = nametofld(tb, "size")) == FLDPN ||
	    (linef = nametofld(tb, "line")) == FLDPN)
		exit(0);

	for (i = 1; i < argc; i++)
	{
		EPI_STAT_S info;

		if (!EPI_STAT(argv[i], &info))
		{
			strcpy(ln, "TESTDATA");
			putfld(fnamef, (void *) argv[i], strlen(argv[i]));
			putfld(linef, (void *) ln, strlen(ln));
			putfld(mtimef, (void *) &info.st_mtime, 1);
			putfld(sizef, (void *) &info.st_size, 1);
			if (puttblrow(tb, -1L) < 0L)
				exit(0);
		}
	}

	fo = foopen();
	if (rewindtbl(tb))
	{
		long tsize;
		size_t tsz;

		fopush(fo, &t);
		while (gettblrow(tb, -1L) > 0L)
		{
			size_t sz;
			long mtime = *((long *) getfld(mtimef, &sz));
			long size = *((long *) getfld(sizef, &sz));
			char *name = (char *) getfld(fnamef, &sz);
			char *line = (char *) getfld(linef, &sz);

			printf("%10ld %10ld %s %s\n", size, mtime, name,
			       line);
			fopush(fo, sizef);
			foop(fo, FOP_ADD);
		}
		tot = fopop(fo);
		tsize = *(long *) getfld(tot, &tsz);
		printf("%10ld total bytes\n", tsize);
	}
	foclose(fo);
	if (tb != TBLPN)
		closetbl(tb);
	exit(0);
}				/* end main() */

/**********************************************************************/
#endif /* TEST3 */

#ifdef TEST4
/**********************************************************************/
void
main(argc, argv)
int argc;
char **argv;
{
	FLDOP *fo;
	FLD *tot;
	static ft_long i, n = 100000;
	static FLD t =
		{ FTN_LONG, (void *) &i, (void *) NULL, 1,
		 1 * sizeof(ft_long), 0, sizeof(ft_long) };

	if (argc > 1)
		n = atol(argv[1]);
	fo = foopen();
	i = 0;
	fopush(fo, &t);
	for (i = 0; i < n; i++)
	{
		fopush(fo, &t);
		foop(fo, FOP_ADD);
	}
	tot = fopop(fo);
	closefld(tot);
	foclose(fo);
	exit(0);
}				/* end main() */

/**********************************************************************/
#endif /* TEST4 */

#ifdef TEST5
/**********************************************************************/
void
main(argc, argv)
int argc;
char **argv;
{
	FLDOP *fo;
	FLD *f;
	int mode = 0;
	static ft_long l, n = 10000;
	static ft_int i = 8888;
	static ft_byte b = 88;
	static FLD tl =
		{ FTN_LONG, (void *) &l, (void *) NULL, 1,
		1 * sizeof(ft_long), 0, sizeof(ft_long) };
	static FLD ti =
		{ FTN_INT, (void *) &i, (void *) NULL, 1, 1 * sizeof(ft_int),
			0, sizeof(ft_int) };
	static FLD tb =
		{ FTN_BYTE, (void *) &b, (void *) NULL, 1,
		1 * sizeof(ft_byte), 0, sizeof(ft_byte) };

	signal(SIGBUS, memtrap);
	signal(SIGSEGV, memtrap);
	for (--argc, ++argv; argc > 0 && **argv == '-'; argc--, argv++)
	{
		switch (*++*argv)
		{
		case '1':
		case 'h':
			mode = 1;
			break;
		case '2':
		case 'c':
			mode = 2;
			break;
		}
	}
	if (argc > 0)
		n = atol(*argv);
	fo = foopen();
	mac_ovchk();
	switch (mode)
	{
	case 0:		/* homo */
		for (l = 0; l < n; l++)
		{
			fopush(fo, &tl);
			mac_ovchk();
			fopush(fo, &tl);
			mac_ovchk();
			foop(fo, FOP_ADD);
			mac_ovchk();
			f = fopop(fo);
			mac_ovchk();
			closefld(f);
			mac_ovchk();
		}
		break;
	case 1:		/* hetero */
		for (l = 0; l < n; l++)
		{
			fopush(fo, &tl);
			fopush(fo, &ti);
			foop(fo, FOP_ADD);
			closefld(fopop(fo));
		}
		break;
	case 2:		/* hetero with cast */
		for (l = 0; l < n; l++)
		{
			fopush(fo, &tl);
			fopush(fo, &tb);
			foop(fo, FOP_ADD);
			closefld(fopop(fo));
		}
		break;
	}
	foclose(fo);
	exit(0);
}				/* end main() */

/**********************************************************************/
#endif /* TEST5 */
