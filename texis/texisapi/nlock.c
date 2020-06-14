/* -=- kai-mode: John -=- */
#ifndef OLD_LOCKING
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <math.h>				/* for floor() */
#include <fcntl.h>				/* for open() */
#include <sys/types.h>
#ifdef EPI_HAVE_IO_H
#  include <io.h>
#endif /* EPI_HAVE_IO_H */
#ifdef USE_SHM
#  include <sys/ipc.h>
#  include <sys/shm.h>
#endif
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef NEED_GETOPT_H
#  include "os.h"
#  ifdef NEED_EPIGETOPT_H
#    include "epigetopt.h"
#  else /* !NEED_EPIGETOPT_H */
#    include <getopt.h>
#  endif /* !NEED_EPIGETOPT_H */
#endif
#include "mmsg.h"
#include "cgi.h"				/* for htsnpf() */
#if defined(sparc) && !defined(__SVR4)
extern int getopt ARGS((int argc,char **argv,char *opts));
extern char *optarg;
extern int optind, opterr, optopt;
#endif

#if defined(FORCENOLOCK) && !defined(NOLOCK)
#  define NOLOCK              /* MAW 08-03-95 - config.h turns if off */
#endif
#ifdef NOLOCK
#  define semlock(a)	0
#  define semunlock(pm, a)	0
#endif
/******************************************************************/

#define VALIDLOCK(x)	((x)<MAX_LOCKS && (x) >= 0)
#define VALIDTABLE(x)	((x)<LTABLES && (x) >= 0)
#define LOCKS(x)	((LOCK *)((x)<0?NULL:((x)<MAX_LOCKS?&locks[(x)]:NULL)))
#define TABLE(x)	((LTABLE *)((x)<0?NULL:((x)<LTABLES?&tables[(x)]:NULL)))

/******************************************************************/

static int lmc[32][32];
static int lmcinited = 0;

/* lockverbose:
 * 0:             only serious issues (default)
 * 1:             as defined in docs
 * 2:             ""
 * 3:             ""
 * 0x10:  system call begin
 * 0x20:  system call end
 * 0x40:  semlock()/semunlock() begin
 * 0x80:  semlock()/semunlock() end
 * 0x10000000:  pause for 60 seconds after successful semlock()
 */
int	TxLockVerbose = 0;

#define SEMVARS	int _res

static CONST char	BeforeFmt[] = "%s(0x%wx, 0x%wx, 0x%p, ...) starting at line %d";
static CONST char	AfterFmt[] =  "%s(0x%wx, 0x%wx, 0x%p, ...) returned %d";
static CONST char	ShmCtl[] = "shmctl";
static const char	ServerIdOutOfRangeFmt[] = "Server id %ld out of range";
static const char	ServerIdOutOfRangeWillFixFmt[] = "Server id %ld out of range; will try to fix";

#define BEFORE(pm, w, a, b, c)			\
((TxLockVerbose & 0x10) ? txpmbuf_putmsg(pm, MINFO, Fn,	\
BeforeFmt, w, (EPI_HUGEINT)(a), (EPI_HUGEINT)(b), (c), __LINE__) : 0)
#define AFTER(pm, w, a, b, c)			\
((TxLockVerbose & 0x20) ? txpmbuf_putmsg(pm, MINFO, Fn,	\
AfterFmt, w, (EPI_HUGEINT)(a), (EPI_HUGEINT)(b), (c), _res) : 0)

#define SHMCTL(pm, id, cmd, ptr)	\
(BEFORE(pm, ShmCtl, id, cmd, ptr),	\
 _res = shmctl(id, cmd, ptr),		\
 AFTER(pm, ShmCtl,  id, cmd, ptr), _res)

/******************************************************************/

#include "dbquery.h"
#include "texint.h"

static	int	addserver ARGS((TXPMBUF *pmbuf, DBLOCK *dblock));
static	int	removeserver ARGS((DBLOCK *, ulong));
static	int	recalcstate ARGS((DDIC *, LTABLE *));
static	int	destroylock ARGS((DBLOCK *, long));
static	int	waitforlock ARGS((DDIC *, int, long, int, char *));
static	int	addlchain ARGS((DBLOCK *, long, long *, long *, int));
static	int	calcstate ARGS((DBLOCK *, LTABLE *, ulong));
static	int	calcmystate ARGS((DBLOCK *, LTABLE *, ulong));
static	int	destroyserver ARGS((DBLOCK *, LSERVER *, ulong));
static	int	checkserver(DBLOCK *dblock, LSERVER *server, ulong sid,
			    int fix, TXbool verbose);
static	int	checktable ARGS((DBLOCK *, LTABLE *, int));
static	int	checklock ARGS((DBLOCK *, LOCK *, long, int));
static	int	rmlock ARGS((DBLOCK *, LOCK *, LTABLE *));
static	int	cleanlock ARGS((DBLOCK *, LOCK *));

/******************************************************************/

TXbool
TXsetlockverbose(int n)
/* Returns false on error.
 */
{
	TxLockVerbose = n;
	return(TXbool_True);
}

int
TXgetlockverbose()
/* Returns current lockverbose value.  Preserves errno/GetLastError().
 */
{
	return TxLockVerbose;
}

const char *
TXlockTypeDescription(int lockType)
{
	switch (lockType)
	{
	case INDEX_READ:		return("index read");
	case INDEX_WRITE:		return("index write");
	case INDEX_VERIFY:		return("index verify");
	case INDEX_READ|INDEX_VERIFY:	return("index read+verify");
	case INDEX_WRITE|INDEX_VERIFY:	return("index write+verify");
	case R_LCK:			return("table read");
	case W_LCK:			return("table write");
	case V_LCK:			return("table verify");
	case R_LCK|V_LCK:		return("table read+verify");
	case W_LCK|V_LCK:		return("table write+verify");
	default:			return("unknown type");
	}
}

/****************************************************************************/

int
tx_loglockop(ddic, act, ltype, t, dbtbl, a, b, rc)
CONST DDIC		*ddic;	/* (in) database */
CONST char		*act;	/* (in) action */
int			ltype;	/* (in) lock type */
VOLATILE LTABLE		*t;	/* (in) (opt.) shmem lock struct */
DBTBL			*dbtbl;	/* (in) (opt.) DBTBL, if known */
VOLATILE ft_counter	*a;	/* (in) (opt.) first counter to log */
VOLATILE ft_counter	*b;	/* (in) (opt.) second counter to log */
int			*rc;	/* (in) (opt.) return code */
/* Logs lock action to `locks.log' file in database dir.
 * WTF would like to log to B-tree log file to ensure ops are logged
 * in occurrence order (clock resolution may not be enough), but could
 * be multiple B-tree log files, and we don't know what they are here.
 * Returns -1 on error, 0 if ok.
 */
{
	static CONST char	fn[] = "tx_loglockop";
	static CONST char	ll[] = "locks.log";
	static CONST char	any[] = "!any";
	int			fh = -1, res, ret, i;
	double			tim;
	CONST char		*lts;
	char	*logFilePath = NULL;
	char	rctmp[EPI_OS_INT_BITS/3+3], tmp[1024];
	char	ltmp[EPI_OS_INT_BITS/3+3];
	char	tofftmp[EPI_OS_INT_BITS/4+4];
	char	atmp[TX_COUNTER_HEX_BUFSZ], btmp[TX_COUNTER_HEX_BUFSZ];
	char	dbtbltmp[EPI_OS_LONG_BITS/4+3];
	char	rltmp[EPI_OS_INT_BITS/3+3], wltmp[EPI_OS_INT_BITS/3+3];
#define PR(buf, fmt, type, ptr)					\
	if (ptr)						\
		htsnpf(buf, sizeof(buf), fmt, (type)*(ptr));	\
	else							\
	{							\
		buf[0] = '-';					\
		buf[1] = '\0';					\
	}

	/* Sanity checks: */
	if (ddic == DDICPN || ddic->epname == CHARPN || !TXApp) goto ok;

	/* See if logging is enabled for this db and table: */
	if (!TXApp->traceLocksDatabase && !TXApp->traceLocksTable)
		goto ok;			/* logging not enabled */
	if (TXApp->traceLocksTable &&
	    strcmpi(TXApp->traceLocksTable, any) != 0 &&
	    ((t != NULL &&
	      strcmp(TXApp->traceLocksTable, (char *)t->name) != 0) ||
	     (dbtbl != NULL &&
	      strcmp(TXApp->traceLocksTable, dbtbl->lname) != 0)))
		goto ok;			/* not logging this table */
	if (TXApp->traceLocksDatabase &&
	    strcmpi(TXApp->traceLocksDatabase, any) != 0 &&
	    TXpathcmp(ddic->epname, -1, TXApp->traceLocksDatabase, -1) != 0)
		goto ok;			/* not logging this db */

	tim = TXgettimeofday();
	switch (ltype)
	{
	case INDEX_READ:		lts = "ir";	break;
	case INDEX_WRITE:		lts = "iw";	break;
	case INDEX_VERIFY:		lts = "iv";	break;
	case INDEX_READ|INDEX_VERIFY:	lts = "irv";	break;
	case INDEX_WRITE|INDEX_VERIFY:	lts = "iwv";	break;
	case R_LCK:			lts = "tr";	goto ok;
	case W_LCK:			lts = "tw";	goto ok;
	case V_LCK:			lts = "tv";	goto ok;
	case R_LCK|V_LCK:		lts = "trv";	goto ok;
	case W_LCK|V_LCK:		lts = "twv";	goto ok;
	default:			lts = "-";	break;
	}
	logFilePath = TXstrcatN(ddic->pmbuf, fn, ddic->epname, PATH_SEP_S, ll,
				NULL);
	if (!logFilePath) goto err;
	if ((fh = TXrawOpen(ddic->pmbuf, __FUNCTION__, "lock log file",
			    logFilePath, TXrawOpenFlag_None,
			    (O_WRONLY | O_APPEND | O_CREAT), 0666)) < 0)
		goto err;
	if (a)
          TXprintHexCounter(atmp, sizeof(atmp), (ft_counter *)a);
	else
	{
		atmp[0] = '-';
		atmp[1] = '\0';
	}
	if (b)
          TXprintHexCounter(btmp, sizeof(btmp), (ft_counter *)b);
	else
	{
		btmp[0] = '-';
		btmp[1] = '\0';
	}
	PR(ltmp, "%d", int, TXbtreelog_srcline)
	PR(rctmp, "%d", int, rc)
	if (t)
	{
		htsnpf(tofftmp, sizeof(tofftmp), "0x%x",
			(int)((byte *)t - (byte *)ddic->dblock->idbl));
	}
	else
	{
		tofftmp[0] = '-';
		tofftmp[1] = '\0';
	}
	if (dbtbl)
	{
		htsnpf(rltmp, sizeof(rltmp), "%d", dbtbl->nireadl);
		htsnpf(wltmp, sizeof(wltmp), "%d", dbtbl->niwrite);
		htsnpf(dbtbltmp, sizeof(dbtbltmp), "0x%lx", (long)dbtbl);
	}
	else
	{
		dbtbltmp[0] = rltmp[0] = wltmp[0] = '-';
		dbtbltmp[1] = rltmp[1] = wltmp[1] = '\0';
	}
	/* Log file format:
 * date time /script:line PID dbtbl rl wl Btree act type tbl off a b rc
	 *
	 * dbtbl	DBTBL pointer
	 * rl		number of index read locks (DBTBL.nireadl)
	 * wl		number of index write locks (DBTBL.niwrite)
	 * Btree	placeholder for B-tree field of w/btreelog
	 * act  	Action
	 * type		Lock type
	 * tbl  	Table
	 * off  	LTABLE offset (lock structure offset)
	 * a/b		Counter `a'/`b'; varies by `act':
	 *	TXlockindex	before/after dbtbl->iwritec (or local fc)
	 *	TXunlockindex 	before/after dbtbl->iwritec (or local fc)
	 *	dblock		before/after t->[ti]write
	 *	dbunlock	before/after t->[ti]write
	 *	verify		t->[ti]write/dbtbl->iwritec
	 *	dblk-ver	t->[ti]write/dbtbl->iwritec
	 *	rgetctr		before/after ddic->dblock->idbl->lcount
	 * rc		return value
	 */
	i = htsnpf(tmp, sizeof(tmp),
"%at.%06d %4s:%-4s %5u %-10s %s %s %-10s %-13s %-3s %-10s %-6s %-12s %-12s %s\n",
		"%Y-%m-%d %H:%M:%S", (time_t)tim,
		(int)(((double)1000000.0)*(tim - floor(tim))),
		(TXbtreelog_srcfile != CHARPPN && *TXbtreelog_srcfile!=CHARPN
			? TXbasename(*TXbtreelog_srcfile) : "-"),
		ltmp, (unsigned)TXgetpid(0), dbtbltmp, rltmp, wltmp, "-",
		act, lts, (t ? t->name : (dbtbl ? dbtbl->lname : "-")),
		tofftmp, atmp, btmp, rctmp);
	if ((size_t)i > sizeof(tmp))		/* message too long */
	{
		strcpy(tmp + sizeof(tmp) - 5, "...\n");
		i = sizeof(tmp) - 1;
	}
	res = tx_rawwrite(ddic->pmbuf, fh, ll, TXbool_False, (byte *)tmp, i,
			  TXbool_False);
	if (res != i) goto err;
ok:
	ret = 0;
	goto done;

err:
	ret = -1;
done:
	if (fh >= 0)
	{
		close(fh);
		fh = -1;
	}
	logFilePath = TXfree(logFilePath);
	return(ret);
}

/******************************************************************/

static	int
destroyserver(dbl, server, sid)
DBLOCK	*dbl;
LSERVER *server;
ulong	sid;
{
	LOCK	*locks;
	int	loopcount = 0;

	locks = &dbl->idbl->locks[0];
	while(VALIDLOCK(server->hlock))
	{
		if(LOCKS(server->hlock)->sid == sid)
		{
			destroylock(dbl, server->hlock);
		}
		else
		{
			if(TX_LOCKVERBOSELEVEL())
				putmsg(MWARN, NULL,
				       "Orphaning lock %d for `%s'",
				       (int)server->hlock,
				       TX_DBLOCK_PATH(dbl));
			server->hlock = LOCKS(server->hlock)->psl;
		}
		if(++loopcount > MAX_LOCKS)
			break;
	}
	server->pid = 0;
	server->tlock=-1;
	server->hlock=-1;
	return 0; /* WTF */
}

/******************************************************************/

int
TXkeepgoing(ddic)
DDIC	*ddic;
{
	if(ddic->state == DDIC_STOPPING)
	{
                return 0;
	}
	return 1;
}

/******************************************************************/

static int
dumpsyslocks(DBLOCK *dblock)
/* Returns 0 if ok, -1 on error.
 */
{
	char dumpfname[PATH_MAX];
	FILE *dumpfh;

	if(dblock && dblock->dumponbad != 0 && dblock->ddic)
	{
		if (htsnpf(dumpfname, sizeof(dumpfname), "%s%/SYSLOCKS.%d",
			   dblock->ddic->epname, (int)TXgetpid(0)) >=
		    (int)sizeof(dumpfname))
		{
			putmsg(MERR + MAE, NULL,
	       "Could not dump bad SYSLOCKS for database `%s': Path too long",
			       dblock->ddic->epname);
			return(-1);
		}
		dumpfh = fopen(dumpfname, "wb+");
		if(dumpfh)
		{
			fwrite(dblock->idbl, sizeof(IDBLOCK), 1, dumpfh);
			fclose(dumpfh);
			dblock->dumponbad = 0;
			putmsg(MINFO, NULL, "Dumped bad SYSLOCKS to `%s'",
			       dumpfname);
		}
		else
		{
			putmsg(MERR, NULL,
			       "Could not dump bad SYSLOCKS to `%s': %s",
			       TXstrerror(TXgeterror()));
			return(-1);
		}
	}
	return 0;
}

/******************************************************************/

static	int
checktable(dblock, table, fix)
DBLOCK	*dblock;
LTABLE	*table;
int	fix;
{
	static const char	willFix[] = "; will try to fix";
#define MAYBE_FIX(fx)	((fx) ? willFix : "")
	int	nerrs = 0;
	int	loopcount = 0;
	long	tlid, tblid;
	LOCK	*tl, *locks;
	LTABLE	*tables;
	EPI_VOIDPTR_INT	diff;

	if(!table)
		return 0;
	locks = &dblock->idbl->locks[0];
	tables = &dblock->idbl->tblock[0];

	diff = table - tables;
	tblid = (long)diff;
	if ((EPI_VOIDPTR_INT)tblid != diff)
	{
		putmsg(MERR + MAE, __FUNCTION__,
	       "Table %s out of range of lock structure for `%s': id 0x%wx%s",
		       table->name, TX_DBLOCK_PATH(dblock), (EPI_HUGEINT)diff,
		       MAYBE_FIX(fix));
		nerrs++;
		return(nerrs);
	}
	if((table->hlock == -1 && table->tlock != -1)||
	   (table->tlock == -1 && table->hlock != -1))
	{
		putmsg(MERR, __FUNCTION__,
		       "Table %s has bad chain in lock structure for `%s'%s",
		       table->name, TX_DBLOCK_PATH(dblock), MAYBE_FIX(fix));
		dumpsyslocks(dblock);
		nerrs++;
	}
	for(tlid = table->hlock, tl = LOCKS(tlid);
	    VALIDLOCK(tlid);
	    tlid = tl->ptl, tl = LOCKS(tlid))
	{
		if(VALIDLOCK(tl->ntl) && LOCKS(tl->ntl)->ptl != tlid)
		{
			putmsg(MERR, __FUNCTION__,
			 "Next -> Prev != Current in lock structure for %s%s",
			       TX_DBLOCK_PATH(dblock), MAYBE_FIX(fix));
			dumpsyslocks(dblock);
			nerrs++;
		}
		if(VALIDLOCK(tl->ptl) && LOCKS(tl->ptl)->ntl != tlid)
		{
			if(VALIDLOCK(tl->ptl))
				putmsg(MERR, __FUNCTION__,
	   "Prev(%d) -> Next(%d) != Current(%d) in lock structure for `%s'%s",
				       (int)tl->ptl, (int)LOCKS(tl->ptl)->ntl,
				       (int)tlid, TX_DBLOCK_PATH(dblock),
				       MAYBE_FIX(fix));
			else
				putmsg(MERR, __FUNCTION__,
		 "Prev(%d) Invalid; current(%d) in lock structure for `%s'%s",
				       (int)tl->ptl, (int)tlid,
				       TX_DBLOCK_PATH(dblock), MAYBE_FIX(fix));
			dumpsyslocks(dblock);
			nerrs++;
		}
		if(tl->ptl==tlid)
		{
			putmsg(MERR, __FUNCTION__,
			       "Prev == Current in lock structure for `%s'%s",
			       TX_DBLOCK_PATH(dblock), MAYBE_FIX(fix));
			dumpsyslocks(dblock);
			nerrs++;
			break;
		}
		if(tl->ntl==tlid)
		{
			putmsg(MERR, __FUNCTION__,
			       "Next == Current in lock structure for `%s'%s",
			       TX_DBLOCK_PATH(dblock), MAYBE_FIX(fix));
			dumpsyslocks(dblock);
			nerrs++;
			break;
		}
		if(!TABLE(tl->tbl))
		{
			putmsg(MERR, __FUNCTION__,
		   "Table(%s) has bad lock (%d) in lock structure for `%s'%s",
			       table->name, (int)tlid, TX_DBLOCK_PATH(dblock),
			       MAYBE_FIX(fix));
			dumpsyslocks(dblock);
			nerrs++;
		} else if(TABLE(tl->tbl) != table)
		{
			putmsg(MERR, __FUNCTION__,
		      "Table(%s) != Current(%s) in lock structure for `%s'%s",
				TABLE(tl->tbl)->name, table->name,
			       TX_DBLOCK_PATH(dblock));
			dumpsyslocks(dblock);
			nerrs++;
		}
		if(++loopcount>MAX_LOCKS)
		{
			putmsg(MERR, __FUNCTION__,
			    "MAX_LOCKS exceeded in lock structure for `%s'%s",
			       TX_DBLOCK_PATH(dblock), MAYBE_FIX(fix));
			dumpsyslocks(dblock);
			nerrs++;
			break;
		}
	}
	if(nerrs > 0 && fix)
	{
		long nextlock;

		for(tlid = table->hlock, tl = LOCKS(tlid);
		    VALIDLOCK(tlid);
		    tlid = nextlock, tl = LOCKS(tlid))
		{
			nextlock = tl->ptl;
			tl->tbl = tblid;
			cleanlock(dblock, tl);
		}
		table->hlock = -1;
		table->tlock = -1;
	}
	return nerrs;
#undef MAYBE_FIX
}

/******************************************************************/

static int
cleanlock(dbl, l)
DBLOCK *dbl;
LOCK *l;
{
	if(l)
	{
		if(l->status == LOCK_AVAILABLE &&
		   l->pid == 0 &&
		   l->sid == 0 &&
		   l->tbl == 0 &&
		   l->nsl == -1 &&
		   l->psl == -1 &&
		   l->ntl == -1 &&
		   l->ptl == -1)
		   return 0;
		if(VALIDTABLE(l->tbl))
			rmlock(dbl, l, &dbl->idbl->tblock[l->tbl]);
		memset(l, 0, sizeof(LOCK));
		l->pid = 0;
		l->nsl = -1;
		l->psl = -1;
		l->ntl = -1;
		l->ptl = -1;
		l->tbl = -1;
		l->sid = -1;
		l->status = LOCK_AVAILABLE;
	}
	return 0;
}

/******************************************************************/

static int
checklock(dbl, l, ln, fix)
DBLOCK  *dbl;
LOCK	*l;
long	ln;
int	fix;
{
	static const char	willFix[] = "; will try to fix";
#define MAYBE_FIX(fx)	((fx) ? willFix : "")
	IDBLOCK	*idbl;
	LOCK	*tl, *locks;
	long	tlid;
	int	loopcount = 0;
	int	found=0;

	idbl = dbl->idbl;
	locks = &idbl->locks[0];

	if(l->status == LOCK_AVAILABLE ||
	   (l->tbl == -1 && l->sid == (size_t)(-1)))
	{
		if(fix)
			cleanlock(dbl, l);
		return 0;
	}
	if(l->type == 0)
	{
		putmsg(MERR, NULL ,
		       "Invalid lock type %d for lock structure for `%s'%s",
		       (int)l->type, TX_DBLOCK_PATH(dbl), MAYBE_FIX(fix));/* MAW 11-09-98 - add word "lock" */
		if(fix)
			cleanlock(dbl, l);
		return 1;
	}
	if ((ulong)l->sid >= (ulong)NSERVERS)
	{
		putmsg(MERR, __FUNCTION__, (fix ? ServerIdOutOfRangeWillFixFmt:
				  ServerIdOutOfRangeFmt), (long)l->sid);
		if(fix)
			cleanlock(dbl, l);
		return 1;
	}
	if(!VALIDTABLE(l->tbl))
	{
		putmsg(MERR, NULL,
		       "Table id %ld bad for lock structure for `%s'%s",
		       (long)l->tbl, TX_DBLOCK_PATH(dbl), MAYBE_FIX(fix));
		if(fix)
			cleanlock(dbl, l);
		return 1;
	}
	if(l->pid && !TXprocessexists(l->pid))
	{
		if(TX_LOCKVERBOSELEVEL())
			putmsg(MERR, NULL,
			       "Lock %d (%d, %d) held by dead process %u (%d) in lock structure for `%s'%s",
			       (int)ln, (int)l->type, (int)l->status,
			       (unsigned)l->pid, (int)l->sid,
			       TX_DBLOCK_PATH(dbl), MAYBE_FIX(fix));
		if(fix)
			cleanlock(dbl, l);
		return 1;
	}
	if(l->pid != idbl->servers[l->sid].pid)
	{
		if(TX_LOCKVERBOSELEVEL())
			putmsg(MERR, NULL,
	    "Lock PID does not match server PID in lock structure for `%s'%s",
			       TX_DBLOCK_PATH(dbl), MAYBE_FIX(fix));
		if(fix)
			cleanlock(dbl, l);
		return 1;
	}
	found = 0;
	for(tlid = idbl->servers[l->sid].hlock, tl = LOCKS(tlid);
	    VALIDLOCK(tlid) && loopcount++ < MAX_LOCKS;
	    tlid = tl->psl, tl = LOCKS(tlid))
	{
		if(tlid == ln)
			found++;
	}
	if(found > 1)
	{
		putmsg(MERR,NULL,"Lock PID in server chain %d times in lock structure for `%s'%s", found, TX_DBLOCK_PATH(dbl), MAYBE_FIX(fix));
		if(fix)
			cleanlock(dbl, l);
		return 1;
	}
	if(found == 0)
	{
		putmsg(MERR,NULL,"Lock PID not in server chain in lock structure for `%s'%s", TX_DBLOCK_PATH(dbl), MAYBE_FIX(fix));
		if(fix)
			cleanlock(dbl, l);
		return 1;
	}

	found = 0;
	for(loopcount = 0, tlid = idbl->tblock[l->tbl].hlock, tl = LOCKS(tlid);
	    VALIDLOCK(tlid) && loopcount++ < MAX_LOCKS;
	    tlid = tl->ptl, tl = LOCKS(tlid))
	{
		if(tlid == ln)
			found++;
	}
	if(found > 1)
	{
		putmsg(MERR,NULL,"Lock PID in table chain %d times in lock structure for `%s'%s", found, TX_DBLOCK_PATH(dbl), MAYBE_FIX(fix));
		if(fix)
			cleanlock(dbl, l);
		return 1;
	}
	if(found == 0)
	{
		putmsg(MERR,NULL,"Lock PID not in table chain in lock structure for `%s'%s", TX_DBLOCK_PATH(dbl), MAYBE_FIX(fix));
		if(fix)
			cleanlock(dbl, l);
		return 1;
	}

#ifdef NEVER
	if(!VALIDLOCK(l->nsl) || !VALIDLOCK(l->psl) || !VALIDLOCK(l->ntl)
	   || !VALIDLOCK(l->ptl))
	{
		if(fix)
			cleanlock(dbl, l);
		return 1;
	}
#endif
	return 0;
#undef MAYBE_FIX
}

/******************************************************************/

static int
checkserver(dblock, server, sid, fix, verbose)
DBLOCK	*dblock;
LSERVER	*server;
ulong	sid;
int	fix;
TXbool	verbose;
{
	int	nerrs = 0;
	int	pid;
	int	loopcount = 0;
	long	tlid;
	LOCK	*tl, *locks;

	pid = server->pid;
	locks = &dblock->idbl->locks[0];

	if(pid && !TXprocessexists(pid))
	{  /* Dead process connected */
		nerrs ++;
		if (verbose)
		{
			putmsg(MWARN, NULL, "Process %u no longer exists in lock structure for `%s'%s", (unsigned)pid, TX_DBLOCK_PATH(dblock),
			       (fix ? "; destroying its locks" : ""));
			dumpsyslocks(dblock);
		}
		if(fix)
			destroyserver(dblock, server, sid);
		pid = server->pid;
	}
#if defined(LOCK_VERBOSE) && defined(NEVER)
	putmsg(MINFO, NULL, "Checking server %d, fix=%d", sid, fix);
#endif
	do
	{
		if(VALIDLOCK(server->hlock) && LOCKS(server->hlock)->nsl != -1)
		{ /* Bad chain */
			putmsg(MERR, __FUNCTION__, "Server lock chain bad in lock structure for `%s'%s", TX_DBLOCK_PATH(dblock), (fix ? "; fixing" : ""));
			dumpsyslocks(dblock);
			nerrs++;
			if(fix)
			{
				if(server->hlock == server->tlock)
				{
					server->hlock = -1;
					server->tlock = -1;
				}
				else
					server->hlock=LOCKS(server->hlock)->psl;
			}
		}
		else
			break;
	} while(fix && loopcount++ < MAX_LOCKS);
	loopcount = 0;
	for(tlid = server->hlock, tl = LOCKS(tlid);
	    VALIDLOCK(tlid);
	    tlid = tl->psl, tl = LOCKS(tlid))
	{
		if(VALIDLOCK(tl->nsl) && LOCKS(tl->nsl)->psl != tlid)
		{
			putmsg(MERR, __FUNCTION__, "Bad lock chain nsl->psl != me in lock structure for `%s'", pid, TX_DBLOCK_PATH(dblock));
			dumpsyslocks(dblock);
			nerrs++;
		}
		if(VALIDLOCK(tl->psl) && LOCKS(tl->psl)->nsl != tlid)
		{
			putmsg(MERR, __FUNCTION__, "Bad lock chain psl->nsl != me in lock structure for `%s'", pid, TX_DBLOCK_PATH(dblock));
			dumpsyslocks(dblock);
			nerrs++;
		}
		if(tl->nsl == tlid || tl->psl == tlid)
		{
			putmsg(MERR, __FUNCTION__, "Bad lock chain loop in lock structure for `%s'", pid, TX_DBLOCK_PATH(dblock));
			dumpsyslocks(dblock);
			nerrs++;
		}
		if(tl->sid != sid)
		{
			putmsg(MERR, __FUNCTION__, "Bad lock wrong sid in lock structure for `%s'", pid, TX_DBLOCK_PATH(dblock));
			dumpsyslocks(dblock);
#ifdef NEVER
			if(fix)
				cleanlock(dblock, tl);
#endif
			nerrs++;
		}
		if(++loopcount>MAX_LOCKS)
		{
			putmsg(MERR, __FUNCTION__, "Lock chain too long in lock structure for `%s'", pid, TX_DBLOCK_PATH(dblock));
			dumpsyslocks(dblock);
			nerrs++;
			break;
		}
	}
	if((nerrs > 0) && fix)
	{
		/* WTF KNG do not yap about fixing if we are not fixing: */
		/* if (verbose) */
			/* putmsg(MERR, __FUNCTION__, "Found %d errors.  Destroying connection info for %u in lock structure for `%s'", */
			       /* nerrs, (unsigned)pid, TX_DBLOCK_PATH(dblock)); */
		/* destroyserver(dblock, server, sid); */
	}
	return nerrs;
}

/******************************************************************/

int
TXverifylocks(pmbuf, dblock, locked, fix, verbose)
TXPMBUF	*pmbuf;		/* (in, opt.) buffer for messages */
DBLOCK	*dblock;
int	locked, fix;
TXbool	verbose;
{
	IDBLOCK	*idbl;
	int	serverc, i;
	int	nerrs = 0;

	idbl = dblock->idbl;

	if(!locked)
	{
		if (semlock(pmbuf, dblock, TXbool_True) == -1)
		{
			return -1;
		}
	}
	/* Check version string */
	/* Check server count (and if alive) */

	serverc = 0;
	for(i=0; i < NSERVERS; i++)
	{
		if(idbl->servers[i].pid&&TXprocessexists(idbl->servers[i].pid))
			serverc++;
	}
	if(serverc != idbl->nservers)
	{
		if (verbose)
		{
			txpmbuf_putmsg(pmbuf, MWARN, NULL, "Server connection mismatch (%d/%d) in lock structure for `%s'%s",
			       (int)serverc, (int)idbl->nservers,
				       TX_DBLOCK_PATH(dblock),
				       (fix ? "; fixing" : ""));
			dumpsyslocks(dblock);
		}
		if(serverc > idbl->nservers)
		{
			nerrs += serverc - idbl->nservers;
		}
		if(idbl->nservers > serverc)
		{
			nerrs += idbl->nservers - serverc;
		}
		if(fix)
			idbl->nservers = serverc;
	}

	/* Check semaphore */

	/* For each server check lock chain */

	for(i=0; i < NSERVERS; i++)
	{
		nerrs += checkserver(dblock, &idbl->servers[i], i, fix,
				     verbose);
	}

	/* For each table, check status, lock chain, times */

	for(i=0; i < LTABLES; i++)
	{
		nerrs += checktable(dblock, &idbl->tblock[i], fix);
	}

	/* For each lock, check status, type, check against server,
			processid, table, chains. */

	for(i=0; i < MAX_LOCKS; i++)
	{
		nerrs += checklock(dblock, &idbl->locks[i], i, fix);
	}

	if(!locked)
	{
		if (semunlock(pmbuf, dblock) == -1)
		{
			return -1;
		}
	}
	return nerrs;
}

/******************************************************************/

static int initlmc ARGS((void));

static int
initlmc()
{
	int	i, j;

	for(i=0; i < 32; i++) /* Current State */
	{
		for(j=0; j < 32; j++) /* Desired type */
		{
			switch(j)
			{
				case 0:
					lmc[i][j] = 1;
					break;
				case R_LCK:
					switch(i)
					{
						case 0:
							lmc[i][j] = 1;
							break;
						case R_LCK:
							lmc[i][j] = 1;
							break;
						case W_LCK:
							lmc[i][j] = 0;
							break;
						case INDEX_READ:
							lmc[i][j] = 1;
							break;
						case INDEX_READ | R_LCK:
							lmc[i][j] = 1;
							break;
						case INDEX_READ | W_LCK:
							lmc[i][j] = 0;
							break;
						case INDEX_WRITE:
							lmc[i][j] = 0;
							break;
						case INDEX_WRITE | R_LCK:
							lmc[i][j] = 1;
							break;
						case INDEX_WRITE | W_LCK:
							lmc[i][j] = 0;
							break;
						default:
							lmc[i][j] = -1;
							break;
					}
					break;
				case W_LCK:
					switch(i)
					{
						case 0:
							lmc[i][j] = 1;
							break;
						case R_LCK:
							lmc[i][j] = 0;
							break;
						case W_LCK:
							lmc[i][j] = 0;
							break;
						case INDEX_READ:
							lmc[i][j] = 1;
							break;
						case INDEX_READ | R_LCK:
							lmc[i][j] = 0;
							break;
						case INDEX_READ | W_LCK:
							lmc[i][j] = 0;
							break;
						case INDEX_WRITE:
							lmc[i][j] = 1;
							break;
						case INDEX_WRITE | R_LCK:
							lmc[i][j] = 0;
							break;
						case INDEX_WRITE | W_LCK:
							lmc[i][j] = 0;
							break;
						default:
							lmc[i][j] = -1;
							break;
					}
					break;
				case INDEX_READ:
					switch(i)
					{
						case 0:
							lmc[i][j] = 1;
							break;
						case R_LCK:
							lmc[i][j] = 1;
							break;
						case W_LCK:
							lmc[i][j] = 1;
							break;
						case INDEX_READ:
							lmc[i][j] = 1;
							break;
						case INDEX_READ | R_LCK:
							lmc[i][j] = 1;
							break;
						case INDEX_READ | W_LCK:
							lmc[i][j] = 1;
							break;
						case INDEX_WRITE:
							lmc[i][j] = 0;
							break;
						case INDEX_WRITE | R_LCK:
							lmc[i][j] = 0;
							break;
						case INDEX_WRITE | W_LCK:
							lmc[i][j] = 0;
							break;
						default:
							lmc[i][j] = -1;
							break;
					}
					break;
				case INDEX_WRITE:
					switch(i)
					{
						case 0:
							lmc[i][j] = 1;
							break;
						case R_LCK:
							lmc[i][j] = 0;
							break;
						case W_LCK:
							lmc[i][j] = 1;
							break;
						case INDEX_READ:
							lmc[i][j] = 0;
							break;
						case INDEX_READ | R_LCK:
							lmc[i][j] = 0;
							break;
						case INDEX_READ | W_LCK:
							lmc[i][j] = 0;
							break;
						case INDEX_WRITE:
							lmc[i][j] = 0;
							break;
						case INDEX_WRITE | R_LCK:
							lmc[i][j] = 0;
							break;
						case INDEX_WRITE | W_LCK:
							lmc[i][j] = 0;
							break;
						default:
							lmc[i][j] = -1;
							break;
					}
					break;
				default:
					lmc[i][j] = -1;
					break;
			}
		}
	}
	return 0;
}

/******************************************************************/

static int
addlchain(sem, lid, h, t, type)
DBLOCK	*sem;
long	lid, *h, *t;
int	type;
{
	LOCK	*l, *locks;

	locks = &sem->idbl->locks[0];
	l = &locks[lid];
	if(*h == -1 && *t == -1)
	{
#ifdef LOCK_VERBOSE
		putmsg(MINFO, NULL, "Added to head of chain");
#endif
		*h = lid;
		*t = lid;
	}
	else
	{
		switch(type)
		{
			case SERVER_CHAIN:
#ifdef LOCK_VERBOSE
		putmsg(MINFO, NULL, "Added to after %d in server chain", *t);
#endif
				l->nsl = *t;
				locks[*t].psl = lid;
				break;
			case TABLE_CHAIN:
#ifdef LOCK_VERBOSE
		putmsg(MINFO, NULL, "Added to after %d in table chain", *t);
#endif
				l->ntl = *t;
				locks[*t].ptl = lid;
				break;
		}
		*t = lid;
	}
	return 0;
}

/******************************************************************/

int	verifyserver ARGS((LSERVER *));

int
verifyserver(server)
LSERVER	*server;
{
/* WTF - put in sysdep */
	if(server && server->pid == TXgetpid(1))
		return 0;
	return -1;
}

/******************************************************************/

int TXminserver = 1;

static	int
addserver(pmbuf, dblock)
TXPMBUF	*pmbuf;
DBLOCK	*dblock;
{
	int	i;

	(void)pmbuf;
	for(i=TXminserver; i < NSERVERS; i++)
	{
		if(dblock->idbl->servers[i].pid == 0)
		{
			dblock->idbl->servers[i].pid = TXgetpid(1);
			dblock->idbl->servers[i].hlock = -1;
			dblock->idbl->servers[i].tlock = -1;
			dblock->idbl->nservers++;
			return i;
		}
	}
	return -1; /* WTF */
}

/******************************************************************/

static int
removeserver(dbl, sid)
DBLOCK	*dbl;
ulong	sid;
{
	static const char	fn[] = "removeserver";
	LSERVER	*server;
	LOCK	*locks;
	int	loopcount = 0;

	if ((ulong)sid >= (ulong)NSERVERS)
	{
		putmsg(MERR, fn, ServerIdOutOfRangeFmt, (long)sid);
		return(-1);
	}
	locks = &dbl->idbl->locks[0];
	server = &dbl->idbl->servers[sid];
	if(verifyserver(server) == -1)
		return -1;
	while(VALIDLOCK(server->hlock))
	{
		if(LOCKS(server->hlock)->sid == sid)
		{
			if(TX_LOCKVERBOSELEVEL())
				putmsg(MERR, NULL, "Server still had lock in lock structure for %s", TX_DBLOCK_PATH(dbl));
			destroylock(dbl, server->hlock);
		}
		else
		{
			if(TX_LOCKVERBOSELEVEL())
				putmsg(MERR, NULL, "Server still had bad lock in chain, orphaning in lock structure for %s", TX_DBLOCK_PATH(dbl));
			server->hlock = LOCKS(server->hlock)->psl;
		}
		if(++loopcount > MAX_LOCKS)
			break;
	}
	dbl->idbl->nservers--;
	server->pid = 0;
	return 0;
}

/******************************************************************/

#ifdef TEST_NEW_CODE
#undef TX_USE_SEM_ARRAYS
#endif

#ifdef TX_USE_SEM_ARRAYS

#define WAIT_FOR_SEM_SUCCESS 0
#define WAIT_FOR_SEM_ERR 1

#ifndef _WIN32
static int
waitforsem(DDIC *ddic, int tblid)
{
	int semsperid = ddic->options[DDIC_OPTIONS_SEMS_PER_ID];
	int semid;
	struct sembuf s;
	TXSEMUN	su;

	if(tblid < 5)
		return WAIT_FOR_SEM_ERR;
	semid = tblid - 4;
	if(semsperid > semid)
	{
		s.sem_num = semid;
		s.sem_op = 0;
		s.sem_flg = 0;
		if (semop(dblock->idbl->semid, &s, 1) == -1)
		{
			return WAIT_FOR_SEM_ERR;
		}
		return WAIT_FOR_SEM_SUCCESS;
	}
	else
		return WAIT_FOR_SEM_ERR;
}
#else
static int
waitforsem(DDIC *ddic, char *tname)
{
	char *ename, *cp;
	HANDLE *event;

	ename = TXstrcat3(ddic->pname, "/", tname);
	for(cp = ename; *cp; cp++)
	{
		switch(*cp)
		{
			case '\\':
			case '/':
				*cp = '_';
				break;
		}
	}
	event = TXCreateEvent(ename, FALSE, 0);
	if(event)
	{
		ResetEvent(event);
		WaitForSingleObject(event, 500);
		CloseHandle(event);
		return WAIT_FOR_SEM_SUCCESS;
	}
	return WAIT_FOR_SEM_ERR;
}
#endif
#endif

/******************************************************************/

static	int
waitforlock(DDIC *ddic, int n, long tblid, int locktype, char *tname)
{
	(void)tblid;
	(void)locktype;
	if(TX_LOCKVERBOSELEVEL() > 1)
		putmsg(MINFO, NULL, "Could not obtain lock.  Calling shortsleep %d for lock structure for %s", n, ddic->epname);
#if defined(TX_USE_SEM_ARRAYS) && defined(_WIN32)
	waitforsem(ddic, tname);
#else
	(void)tname;
	TXshortsleep(n); /* WTF? */
#endif
	if(n%100 == 0)
		TXverifylocks(ddic->pmbuf, ddic->dblock, 0 /* !locked */,
			      1 /* fix */, (TX_LOCKVERBOSELEVEL() > 0));
	return 0;
}

/******************************************************************/

DBLOCK *
TXdblockOpenDirect(TXPMBUF *pmbuf, const char *path, int *sid,
		   TXbool readOnly)
/* Opens/creates lock structure directly, returning it.
 * If `readOnly', will not create shm/sem nor modify lock structure;
 * this is used by dumplock.
 * Note: caller should set DBLOCK.ddic, and `sid' should be &DDIC.sid.
 */
{
	DBLOCK	*dbl;

	if(!lmcinited)
	{
		initlmc();
		lmcinited++;
	}
	dbl = (DBLOCK *)TXcalloc(pmbuf, __FUNCTION__, 1, sizeof(DBLOCK));
	if(dbl)
	{
#ifdef NOLOCK
		dbl->idbl = (IDBLOCK *)TXcalloc(pmbuf, __FUNCTION__, 1,
						sizeof(IDBLOCK));
		return dbl;
#endif
		dbl->idbl = getshared(pmbuf, dbl, path, readOnly);
		if(dbl->idbl)
		{
			if (!readOnly && semlock(pmbuf, dbl, !readOnly) != -1)
			{
				/* TXverifylocks(dbl, 1, 1, */
					      /* (TX_LOCKVERBOSELEVEL() > 0)); */
				*sid = addserver(pmbuf, dbl);
				semunlock(pmbuf, dbl);
			}
			else
				*sid = -1;
			if (!readOnly && *sid == -1)
			{
				dbl->idbl = (IDBLOCK *)ungetshared(pmbuf, dbl,
								   readOnly);
				dbl = TXfree(dbl);
			}
			DBGMSG(1, (999, NULL, "Opening connection %d", *sid));
		}
		else
			dbl = TXfree(dbl);
		if (dbl)
		{
			if(TXApp)
				dbl->dumponbad = TXApp->LogBadSYSLOCKS;
			else
				dbl->dumponbad = 0;
		}
	}
	return dbl;
}

/******************************************************************/

DBLOCK	*
opendblock(DDIC *ddic)
{
	DBLOCK	*dbl;

	if(!ddic)
		return NULL;
	dbl = TXdblockOpenDirect(ddic->pmbuf, ddic->pname, &ddic->sid,
				 TXbool_False /* !readOnly */);
	if (dbl) dbl->ddic = ddic;
	return dbl;
}

/******************************************************************/

DBLOCK *
closedblock(pmbuf, dbl, sid, readOnly)
TXPMBUF	*pmbuf;		/* (in, opt.) buffer for messages */
DBLOCK	*dbl;
int	sid;
TXbool	readOnly;	/* no mods nor sem/shm create; for dumplock */
{
	if(!dbl)
		return dbl;
	DBGMSG(1, (999, NULL, "Closing connection %d", sid));
#ifndef NOLOCK
	if (!readOnly && semlock(pmbuf, dbl, !readOnly) != -1)
	{
		/* TXverifylocks(pmbuf, dbl, 1, 1, (TX_LOCKVERBOSELEVEL() > 0)); */
		removeserver(dbl, sid);
		semunlock(pmbuf, dbl);
	}
	dbl->idbl = (IDBLOCK *)ungetshared(pmbuf, dbl, readOnly);
#endif
	dbl = TXfree(dbl);
	return NULL; /* WTF */
}

int
TXdblockGetNumAttachedServers(DBLOCK *dblock)
/* Returns number of servers attached to locks, or -1 if indeterminable.
 */
{
	if (dblock && dblock->idbl)
		return(dblock->idbl->nservers);
	else
		return(-1);
}

/******************************************************************/

static long getfreelock ARGS((DBLOCK *));

static long
getfreelock(dbl)
DBLOCK	*dbl;
{
	int	i;

	for(i=0; i < MAX_LOCKS; i++)
	{
		if(dbl->idbl->locks[i].status == LOCK_AVAILABLE)
		{
			dbl->idbl->locks[i].nsl = -1;
			dbl->idbl->locks[i].psl = -1;
			dbl->idbl->locks[i].ntl = -1;
			dbl->idbl->locks[i].ptl = -1;
			return i;
		}
	}
	putmsg(MWARN, "getfreelock", "No locks currently available in lock structure for %s", TX_DBLOCK_PATH(dbl));
	return -1; /* WTF */
}

/******************************************************************/
/* KNG 20060222 this must be public to work around compiler issue (?)
 * on x86_64-unknown-linux2.6.9
 */
int tx_nlock_addlock ARGS((DBLOCK *, ulong, LTABLE *, long));

int
tx_nlock_addlock(dblock, sid, table, lid)
DBLOCK	*dblock;
ulong	sid;
LTABLE	*table;
long	lid;
{
	static const char	fn[] = "tx_nlock_addlock";
	LSERVER	*server;
	LOCK	*lock;

	if ((ulong)sid >= (ulong)NSERVERS)
	{
		putmsg(MERR, fn, ServerIdOutOfRangeFmt, (long)sid);
		return(-1);
	}
	server = &dblock->idbl->servers[sid];
	lock = &dblock->idbl->locks[lid];

	if(verifyserver(server)==-1)
		return -1;
	if(table != &dblock->idbl->tblock[lock->tbl])
		return -1;
	addlchain(dblock, lid, &(server->hlock), &(server->tlock), SERVER_CHAIN);
	addlchain(dblock, lid, &(table->hlock), &(table->tlock), TABLE_CHAIN);
	return 0;
}
#define addlock tx_nlock_addlock

/******************************************************************/

static LTABLE *gettable ARGS((DBLOCK *, long *, char *));

int
addltable(pmbuf, sem, table)
TXPMBUF	*pmbuf;
DBLOCK	*sem;
char	*table;
{
	long	tblid;
	LTABLE	*t;

#ifdef NOLOCK
	return 0;
#else /* NOLOCK */
	if(!sem)
		return 0;
	tblid = -1;
	if (semlock(pmbuf, sem, TXbool_True) != -1)
	{
		t = gettable(sem, &tblid, table);
		if(t)
		{
			t->usage ++;
		}
		semunlock(pmbuf, sem);
	}
	return tblid;
#endif /* NOLOCK */
}

/******************************************************************/

int
TXfindltable(pmbuf, sem, table)
TXPMBUF	*pmbuf;		/* (in, opt.) buffer for messages */
DBLOCK	*sem;
char	*table;
{
	long	tblid;
	LTABLE	*t;

#ifdef NOLOCK
	return 0;
#else /* NOLOCK */
	tblid = -1;
	if (semlock(pmbuf, sem, TXbool_True) != -1)
	{
		t = gettable(sem, &tblid, table);
		(void)t;		/* wtf check it? */
		semunlock(pmbuf, sem);
	}
	return tblid;
#endif /* NOLOCK */
}

/******************************************************************/

int
delltable(pmbuf, sem, table, tblid)
TXPMBUF	*pmbuf;		/* (in, opt.) buffer for messages */
DBLOCK	*sem;
char	*table;
int	tblid;
{
	LTABLE	*t;
	long	ntblid;

#ifndef NOLOCK
	if(!sem)
		return 0;
	ntblid = tblid;
	if (semlock(pmbuf, sem, TXbool_True) != -1)
	{
		t = gettable(sem, &ntblid, table);
		if(t)
		{
			if(t->usage > 0)
				t->usage --;
		}
		semunlock(pmbuf, sem);
	}
#endif /* NOLOCK */
	return 0;
}

/******************************************************************/

static LTABLE	*
gettable(dblock, tblid, tname)
DBLOCK	*dblock;	/* (in/out) lock object */
long	*tblid;		/* (in-opt, out) id of table */
char	*tname;		/* (in) table name to find */
{
	LTABLE	*t, *tb, *ftb = NULL;
	int	i;
	long	fid = 0L;

	/* Loop through tables to find.  Keep a pointer to a free one
	just in case */
	if(!tname)
	{
		*tblid = -1;
		return NULL;
	}
	if(*tblid >= 0)
	{
		t = &dblock->idbl->tblock[*tblid];
		if(!strcmp(t->name, tname))
			return t;
	}
	tb = &dblock->idbl->tblock[0];
	for(i=0; i < LTABLES; i++)
	{
		if(!ftb && tb[i].name[0] == '\0')
		{
			ftb = &dblock->idbl->tblock[i];
			fid = i;
		}
		if(!strcmp(tb[i].name, tname))
		{
			tb = &dblock->idbl->tblock[i];
			*tblid = i;
			return tb;
		}
	}
	if(!ftb)
	{
		for(i=0; i < LTABLES && !ftb; i++)
		{
			if(!ftb && tb[i].name[0] == '\0')
			{
				ftb = &tb[i];
				fid = i;
			}
			if(!tb[i].usage)
			{
				ftb = &tb[i];
				fid = i;
			}
		}
	}
	if(ftb)
	{
		TXstrncpy(ftb->name, tname, 40);
		ftb->status = 0;
		*tblid = fid;
		ftb->hlock = -1;
		ftb->tlock = -1;
		ftb->twrite.date=0;
		ftb->twrite.seq=0;
		ftb->iwrite.date=0;
		ftb->iwrite.seq=0;
	}
	return ftb;
}

/******************************************************************/

static int TXfairlocking = 1;

int TXsetfairlock(n)
int n;
{
	int rc = TXfairlocking;
	TXfairlocking = n;
	return rc;
}

/******************************************************************/

static int lmcompatible ARGS((DDIC *, LOCK *, LTABLE *, ulong));

static int
lmcompatible(ddic, l, t, sid)
DDIC	*ddic;
LOCK	*l;
LTABLE	*t;
ulong	sid;
{
	int	rc = 0, ostatus = 0;
	DBLOCK	*sem = ddic->dblock;

	if(l->status == LOCK_GRANTED)
		return 1;  /* Allow an external process to grant the lock */

	if(TXfairlocking) /* This bit of code will make sure that the
			     lock at the head of the queue will be obtained
			     at some point. */
	{
		LOCK *locks, *hl;

		ostatus = calcmystate(sem, t, sid);  /* Calculate my state */
		if(ostatus != 0)
		     goto reglock;
		locks = &sem->idbl->locks[0];  /* Start of lock array */
		hl = LOCKS(t->hlock);     /* Get the lock at the head */
		if(hl && hl != l &&
		   hl->status != LOCK_GRANTED)     /* If it is not us */
		{
			rc = lmc[hl->type][l->type];
			if(rc == 0)                  /* Can't have it */
				return rc;/* Check this compatibility */
		}
	}
reglock:
	rc = lmc[t->status][l->type];	/* Is this type compatible */
	if(rc == 1)
		return rc;
	if((ostatus & W_LCK) && (l->type == R_LCK))
		return 1;
	recalcstate(ddic, t);
	ostatus = calcstate(sem, t, sid);  /* Calculate state of other locks */
	rc = lmc[ostatus][l->type];
	return rc;
}

/******************************************************************/
#ifdef NEW_WAIT_LOCK
/*
	Return the number of locks that need to be processed before
	we should be able to get the lock
*/

static int queuelength ARGS((DBLOCK *, LOCK *, LTABLE *, ulong));

static int
queuelength(sem, l, t, sid)
DBLOCK	*sem;
LOCK	*l;
LTABLE	*t;
ulong	sid;
{
	int	rc = 0, loopcount = 0;
	LOCK *locks, *hl;

	if(l->status == LOCK_GRANTED)
		return rc;  /* Allow an external process to grant the lock */

	locks = &sem->idbl->locks[0];  /* Start of lock array */
	hl = LOCKS(t->hlock);     /* Get the lock at the head */
	while(hl && hl != l)     /* If it is not us */
	{
		if(lmc[hl->type][l->type] == 0) /* Can't have it */
			rc++;
		hl = LOCKS(hl->ptl);
		loopcount++;
		if(loopcount > MAX_LOCKS)
			break;
	}
	return rc;
}

#endif /* NEW_WAIT_LOCK */
/******************************************************************/

static int verifytime ARGS((LTABLE *, int, ft_counter *));

static int
verifytime(t, type, counterp)
LTABLE		*t;		/* (in) table lock structure (shmem) */
int		type;		/* (in) lock type: V_LCK or INDEX_VERIFY */
ft_counter	*counterp;	/* (in) timestamp to compare */
/* Verifies that the table or index indicated by `t'/`type' has not been
 * written to since the timestamp `*counterp'.
 * Should only be called if semlock() has been obtained (or singleuser).
 * Returns:
 *   -2 if table (type == V_LCK) or index (type == INDEX_VERIFY)
 *      was modified after `*counterp'.
 *   -1 on error, e.g. V_LCK/INDEX_VERIFY given but `counterp' is NULL.
 *    0 if ok (table/index not modified after `*counterp').
 *    1 if lock `type' is neither V_LCK nor INDEX_VERIFY.
 */
{
	if((type == V_LCK) || (type == INDEX_VERIFY))
	{
		if(counterp)
		{
			ft_counter *twrite;

			twrite = type == V_LCK ? &(t->twrite): &(t->iwrite);
			if(twrite->date >  counterp->date ||
			  (twrite->date == counterp->date &&
			   twrite->seq  >  counterp->seq))
			{
				return -2;
			}
			else
			{
				return 0;
			}
		}
		else
		{
			return -1;
		}
	}
	else
		return 1;
}

/******************************************************************/

static int
grantlock(
DDIC		*ddic,		/* (in/out) whose locks to use */
int		type,		/* (in) R_LCK, W_LCK, INDEX_READ,INDEX_WRITE*/
int		verify,		/* (in) nonzero: verify `*counterp' as index*/
ft_counter	*counterp,	/* (opt.) in: verify this  out: new time */
LOCK		*l,		/* (in/out) individual lock */
LTABLE		*t)		/* (in/out) table lock structure (shmem) */
/* Grants a lock of `type' and calls semunlock().
 * If `verify', verifies `*counterp' with verifytime(INDEX_VERIFY).
 * Always gets a new counter and returns it in `*counterp', updating
 * `t->[ti]write' to it if W_LCK or INDEX_WRITE.
 * Should only be called during a semlock() (? -KNG).
 * Returns:
 *   -2 if index was modified after (in) `*counterp'.
 *   -1 on error, e.g. `verify' set but `counterp' is NULL.
 *    0 if ok (`verify' is 0, or index not modified after `*counterp').
 * Lock granted regardless of return value.
 */
{
	static const char	fn[] = "grantlock";
	DBLOCK *sem;
	int rc;
	ft_counter	b4, af;

	sem = ddic->dblock;
#ifdef NEW_WAIT_LOCK
	TXrooster();
#endif
	l->status = LOCK_GRANTED;
	recalcstate(ddic, t);
	DBGMSG(1, (999, NULL, "Granting lock type %d from %s", l->type, t->name));
	if(verify)
	{
		rc = verifytime(t, INDEX_VERIFY, counterp);
		if (!TXTRACELOCKS_IS_OFF())
			tx_loglockop(ddic, "verify", INDEX_VERIFY, t, NULL,
				&t->iwrite, counterp, &rc);
		if (rc == -1)			/* should not happen? */
			txpmbuf_putmsg(ddic->pmbuf, MWARN, fn,
				       "verifytime() failed for %s",
				       ddic->epname);
	}
	else
		rc = 0;
	switch(type)
	{
		case	R_LCK:
			b4 = t->twrite;
			if(counterp)
				rgetcounter(ddic, counterp, 0);
			af = t->twrite;
			break;
		case	W_LCK:
			b4 = t->twrite;
			rgetcounter(ddic, &t->twrite, 0);
			if(counterp)
				*counterp = t->twrite;
			af = t->twrite;
			break;
		case	INDEX_READ:
			b4 = t->iwrite;
			if(counterp)
				rgetcounter(ddic, counterp, 0);
			af = t->iwrite;
			break;
		case	INDEX_WRITE:
			b4 = t->iwrite;
			rgetcounter(ddic, &t->iwrite, 0);
			if(counterp)
				*counterp = t->iwrite;
			af = t->iwrite;
			break;
		default:
			b4.date = af.date = 0L;
			b4.seq = af.seq = 0L;
			break;
	}
	if (!TXTRACELOCKS_IS_OFF())
		tx_loglockop(ddic, "dblock", type, t, NULL, &b4, &af, &rc);
	semunlock(ddic->pmbuf, sem);		/* release system lock */
	return rc;
}

/******************************************************************/
/* Returns -2 if `type' is/contains V_LCK/INDEX_VERIFY and `*tblid'/`tname'
 * was modified since `*counterp'; -1 on error; 0 if no mods (or INDEX_VERIFY/
 * V_LCK not set).  Lock only *not* granted if return is -1, or if `type'
 * is V_LCK/INDEX_VERIFY alone (no ...READ/...WRITE/etc.).
 * NOTE: might not issue putmsg() if LOCK_TIMEOUT error set, or if just
 * verifying.
 */

static int
i_dblock(DDIC *ddic, int sid, long *tblid, int type, char *tname, ft_counter *counterp)
{
	static const char	fn[] = "dblock";
	DBLOCK	*sem;
	LOCK	*l, *locks;
	LTABLE	*t;
	int	verify = 0;
	long	lid;
	int	loopcount = 0;
	PID_T	serverPid;
#ifdef NEW_WAIT_LOCK
	int	queuelen = 0;
#endif

#ifdef NOLOCK
	return 0;
#else /* NOLOCK */

	if ((ulong)sid >= (ulong)NSERVERS)
	{
		txpmbuf_putmsg(ddic->pmbuf, MERR, fn, ServerIdOutOfRangeFmt,
			       (long)sid);
		return(-1);
	}

	if(!TXkeepgoing(ddic))
	{
		/* No message here: SQLFetch() caller should call
		 * TXsqlWasCancelled() to check for this event,
		 * caused by asynchronous SQLCancel() call:
		 */
		texispusherror(ddic, MAKEERROR(MOD_LOCK, LOCK_TIMEOUT));
		return -1;
	}
	if(ddic->nolocking)
	{
#ifndef NEVER
    if(0 != (type & (V_LCK | INDEX_VERIFY)))
			return TXverifysingle;
#endif
		return 0;
	}
	if(*tblid == -1)
		return 0;
	sem = ddic->dblock;
	if(!sem)
	{
#ifdef NEVER
		if(type == V_LCK || type == INDEX_VERIFY)
			return -1;
#endif
		return 0;
	}
	if (semlock(ddic->pmbuf, sem, TXbool_True) == -1)
		return -1;
	if ((serverPid = sem->idbl->servers[sid].pid) != TXgetpid(0))
	{
		txpmbuf_putmsg(ddic->pmbuf, MERR, NULL,
			       "Wrong server ID in lock structure for %s: ID %d owned by %u, not current process %u",
			       ddic->epname, (int)sid, (unsigned)serverPid,
			       (unsigned)TXgetpid(0));
		semunlock(ddic->pmbuf, sem);
		return -1;
	}
	locks = &sem->idbl->locks[0];
	t = gettable(sem, tblid, tname);
	if(!t)
	{
		semunlock(ddic->pmbuf, sem);
		txpmbuf_putmsg(ddic->pmbuf, MERR, fn,
   "Could not obtain LTABLE object for table %s/%ld in lock structure for %s",
			       tname, (tblid ? *tblid : -1L), ddic->epname);
		return -1;
	}
#ifdef LOCK_VERBOSE
	txpmbuf_putmsg(ddic->pmbuf, MINFO, NULL, "Table %s, tblid %d",
		       tname, *tblid);
#endif
	verify = verifytime(t, type, counterp);
	/* only log this if verifytime() does something: */
	if (!TXTRACELOCKS_IS_OFF())
	{
		if (type == V_LCK)
			tx_loglockop(ddic, "dblk-ver", type, t, NULL,
				&t->twrite, counterp ,&verify);
		else if (type == INDEX_VERIFY)
			tx_loglockop(ddic, "dblk-ver", type, t, NULL,
				&t->iwrite, counterp ,&verify);
	}
	if(verify <= 0)			/* `type' is V_LCK or INDEX_VERIFY */
	{
		semunlock(ddic->pmbuf, sem);
		return verify;
	}
	verify = 0;
	if(type == INDEX_VERIFY + INDEX_WRITE)
	{
		type = INDEX_WRITE;
		verify = 1;
	}
	if(type == INDEX_VERIFY + INDEX_READ)
	{
		type = INDEX_READ;
		verify = 1;
	}
	lid = getfreelock(sem);
#ifdef LOCK_VERBOSE
	txpmbuf_putmsg(ddic->pmbuf, MINFO, NULL, "lockid %d", lid);
#endif
	l = LOCKS(lid);
	if(l)
	{
		l->status = LOCK_PENDING;
		l->pid = TXgetpid(0);
		l->sid = sid;
		l->type = type;
		l->tbl = *tblid;
		l->nsl = l->psl = l->ntl = l->ptl = -1;
		/* Initialize lock structure */
#ifdef LOCK_VERBOSE
		txpmbuf_putmsg(ddic->pmbuf, MINFO, NULL,
			       "lockid %d initialized", lid);
#endif
	}
	else
	{
		semunlock(ddic->pmbuf, sem);
		txpmbuf_putmsg(ddic->pmbuf, MERR, fn,
			  "Could not obtain LOCK object for free lock id %ld in lock structure for %s",
			       (long)lid, ddic->epname);	/* KNG 20131125 yap */
		return -1;
	}
	if(addlock(sem, sid, t, lid)==-1)
	{
		semunlock(ddic->pmbuf, sem);
		txpmbuf_putmsg(ddic->pmbuf, MERR, NULL,
			       "Could not add lock to chains in lock structure for %s", ddic->epname);
		return -1;
	}
#ifdef LOCK_VERBOSE
	txpmbuf_putmsg(ddic->pmbuf, MINFO, NULL, "lockid %d added to chain",
		       lid);
#endif
	if(lmcompatible(ddic, l, t, sid) == 1)
	{
		return grantlock(ddic, type, verify, counterp, l, t);
	}
#ifdef NEW_WAIT_LOCK
	TXsandman();
	queuelen = queuelength(sem, l, t, sid);
#endif
	semunlock(ddic->pmbuf, sem);
#ifdef NEW_WAIT_LOCK
	waitforlock(ddic, queuelen, *tblid, type, tname);
#else
	waitforlock(ddic, loopcount, *tblid, type, tname);
#endif
	while(1)
	{
		loopcount++;
		if (semlock(ddic->pmbuf, sem, TXbool_True) != -1)
		{
			t = gettable(sem, tblid, tname);
			if (!t)
			{
				txpmbuf_putmsg(ddic->pmbuf, MERR, fn,
			    "Could not obtain LTABLE object for table %s/%ld in lock structure for %s",
					       tname, (tblid ? *tblid : -1L),
					       ddic->epname);
				return(-1);	/* KNG 20131125 error check */
			}
			if(lmcompatible(ddic, l, t, sid) == 1)
			{
				return grantlock(ddic, type, verify, counterp, l, t);
			}
#ifdef NEW_WAIT_LOCK
			queuelen = queuelength(sem, l, t, sid);
			TXsandman();
#endif
			semunlock(ddic->pmbuf, sem);
		}
		else				/* semlock() failed */
			return -1;
#ifdef NEW_WAIT_LOCK
		waitforlock(ddic, queuelen, *tblid, type, tname);
#else
		waitforlock(ddic, loopcount, *tblid, type, tname);
#endif
	}
#endif /* NOLOCK */
}

int
dblock(ddic, sid, tblid, type, tname, counterp)
DDIC		*ddic;
ulong		sid;
long		*tblid;
int		type;		/* (in) lock type INDEX_READ etc. */
char		*tname;
ft_counter	*counterp;/* (opt.) in: verify it  out: new time (if R/W lck)*/
/* Returns -2 if `type' is/contains V_LCK/INDEX_VERIFY and `*tblid'/`tname'
 * was modified since `*counterp'; -1 on error; 0 if no mods (or INDEX_VERIFY/
 * V_LCK not set).  Lock only *not* granted if return is -1, or if `type'
 * is V_LCK/INDEX_VERIFY alone (no ...READ/...WRITE/etc.).
 * NOTE: might not issue putmsg() if LOCK_TIMEOUT error set, or if just
 * verifying.
 */
{
  int rc;

  rc = i_dblock(ddic, sid, tblid, type, tname, counterp);
/*
  printf("DBLOCK: %s(%d:%d): %d\n", tname, type, (type & (V_LCK | INDEX_VERIFY)), rc);
*/
  return rc;
}

/******************************************************************/

static LOCK *getlock ARGS((DBLOCK *, LTABLE *, ulong, int));

static LOCK *
getlock(dbl, t, sid, type)
DBLOCK	*dbl;
LTABLE	*t;
ulong	sid;
int	type;
{
	/* We'll search the chains to find the lock. */
	static const char	fn[] = "getlock";
	LSERVER	*server;
	LOCK	*tl;
	LOCK	*locks;
	LTABLE	*tables;
	int	loopcount=0;

	if ((ulong)sid >= (ulong)NSERVERS)
	{
		putmsg(MERR, fn, ServerIdOutOfRangeFmt, (long)sid);
		return(NULL);
	}

	server = &dbl->idbl->servers[sid];
	locks = &dbl->idbl->locks[0];
	tables = &dbl->idbl->tblock[0];

	if(verifyserver(server) == -1)
		return NULL;
	for(tl=LOCKS(server->hlock); tl; tl = LOCKS(tl->psl))
	{
		if(TABLE(tl->tbl) == t && tl->type == type && tl->sid == sid)
		{
			return tl;
		}
		if(++loopcount > MAX_LOCKS)
			break;
	}
	if(TX_LOCKVERBOSELEVEL())
	{
		putmsg(MWARN, NULL, "Server->hlock=%d in lock structure for %s", (int)server->hlock, TX_DBLOCK_PATH(dbl));
		for(tl=LOCKS(server->hlock); tl; tl = LOCKS(tl->psl))
		{
			putmsg(MWARN, NULL, "tl->psl=%d in lock structure for %s", (int)tl->psl, TX_DBLOCK_PATH(dbl));
			if(tl->type == type && tl->sid == sid)
			{
				putmsg(MWARN, NULL, "Have a lock of same type on table %d (%s) in lock structure for %s", (int)tl->tbl, TABLE(tl->tbl)->name,
				       TX_DBLOCK_PATH(dbl));
			}
			if(++loopcount > MAX_LOCKS)
				break;
		}
	}
	return NULL; /* WTF */
}

/******************************************************************/


static int
destroylock(dbl, l)
DBLOCK	*dbl;
long	l;
{
	LOCK	*ll;
	int	rc;

	ll = &dbl->idbl->locks[l];
	if(VALIDLOCK(l))
	{
		if(ll->tbl == -1)
		{
			rc = -1;
		}
		else
		{
			rc = rmlock(dbl, ll, &dbl->idbl->tblock[ll->tbl]);
		}
		ll->tbl = -1;
		ll->status = LOCK_AVAILABLE;
		ll->sid = -1;
		return rc;
	}
	return -1;
}

/******************************************************************/

static int unchain ARGS((DBLOCK *, LOCK *, LTABLE *, LSERVER *));

static int
unchain(sem, l, t, s)
DBLOCK	*sem;
LOCK	*l;
LTABLE	*t;
LSERVER	*s;
{
	static  CONST char	Fn[] = "unchain";
	LOCK	*tl;
	LOCK	*locks;
	int	lid;
	EPI_VOIDPTR_INT	diff;

	if(!l)
		return 0;

	locks = &sem->idbl->locks[0];
	diff = l - locks;
	lid = (int)diff;
	if ((EPI_VOIDPTR_INT)lid != diff)
	{
		putmsg(MERR + MAE, Fn, "Lock id out of range: 0x%wx in lock structure for %s",
		       (EPI_HUGEINT)diff, sem->idbl->path);
		return(-1);
	}

	if(l->psl == lid) l->psl = -1;
	if(l->nsl == lid) l->nsl = -1;
	if(l->ptl == lid) l->ptl = -1;
	if(l->ntl == lid) l->ntl = -1;
	tl = LOCKS(l->nsl);
	if(tl != NULL)
	{
		if(tl->psl == lid)
			tl->psl = l->psl;
		else
			putmsg(MWARN, Fn, "Bad psl lock chain in lock structure for %s", sem->idbl->path);
	}
	else if(s)
	{
		if(s->hlock == lid)
			s->hlock = l->psl;
		else
			putmsg(MWARN, Fn, "Bad shlock lock chain in lock structure for %s", sem->idbl->path);
	}
	tl = LOCKS(l->psl);
	if(tl != NULL)
	{
		if(tl->nsl == lid)
			tl->nsl = l->nsl;
		else
			putmsg(MWARN, Fn, "Bad nsl lock chain in lock structure for %s", sem->idbl->path);
	}
	else if(s)
	{
		if(s->tlock == lid)
			s->tlock = l->nsl;
		else
			putmsg(MWARN, Fn, "Bad stlock lock chain in lock structure for %s", sem->idbl->path);
	}
	l->psl = -1;
	l->nsl = -1;

	tl = LOCKS(l->ntl);
	if(tl != NULL)
	{
		if(tl->ptl == lid)
			tl->ptl = l->ptl;
		else
			putmsg(MWARN, Fn, "Bad ptl lock chain (%d, %d) in lock structure for %s",
			       (int)tl->ptl, (int)lid, sem->idbl->path);
	}
	else if(t)
	{
		if(t->hlock == lid)
			t->hlock = l->ptl;
		else
			putmsg(MWARN, Fn, "Bad thlock lock chain in lock structure for %s", sem->idbl->path);
	}
	tl = LOCKS(l->ptl);
	if(tl != NULL)
	{
		if(tl->ntl == lid)
			tl->ntl = l->ntl;
		else
			putmsg(MWARN, Fn, "Bad ntl lock chain in lock structure for %s", sem->idbl->path);
	}
	else if(t)
	{
		if(t->tlock == lid)
			t->tlock = l->ntl;
		else
			putmsg(MWARN, Fn, "Bad thlock lock chain in lock structure for %s", sem->idbl->path);
	}
	l->ptl = -1;
	l->ntl = -1;

	return 0;
}

/******************************************************************/

static int
rmlock(dbl, l, t)
DBLOCK	*dbl;
LOCK	*l;
LTABLE	*t;
{
	LSERVER	*s;
	ulong	sid;

	if(!l)
		return -1;
	sid = l->sid;
	if ((ulong)sid >= (ulong)NSERVERS)
		s = NULL;
	else
	{
		s = &dbl->idbl->servers[sid];
	}
	if(!t)
		return -1;
/*
	if(verifyserver(s) == -1)
		return -1;
*/
	if(TX_LOCKVERBOSELEVEL() > 2)
		putmsg(MINFO, NULL, "Destroying Lock Type %d on table %s, Server ID %d (%u) in lock structure for %s",
		       (int)l->type, t?t->name:"Null table", (int)l->sid, (unsigned)l->pid, TX_DBLOCK_PATH(dbl));
#if defined(_WIN32) && defined(TX_DEBUG)
	if(l->type == 0 && l->sid == 0 && l->pid == 0)
		debugbreak();
#endif
	if(checktable(dbl, t, 0))
	{
		DBGMSG(-1, (999, NULL, "Checking table %s failed", t->name));
	}
	unchain(dbl, l, t, s);
#ifdef NEVER
	if(checktable(dbl, t, 0))
	{
		DBGMSG(-1, (999, NULL, "Checking table %s failed", t->name));
	}
#endif
	return 0;
}

/******************************************************************/

static int  makefree ARGS((DBLOCK *, LOCK *));

static int
makefree(dbl, l)
DBLOCK	*dbl;
LOCK	*l;
{
	(void)dbl;
	l->status = LOCK_AVAILABLE;
	l->tbl = -1;
	l->sid = -1;
	return -1;
}

/******************************************************************/

static int
calcstate(sem, t, sid)
DBLOCK	*sem;
LTABLE	*t;
ulong	sid;
/* Returns bitwise-OR of lock types currently granted on `t' to servers
 * other than `sid'. (? -KNG)
 */
{
	LOCK	*l, *locks;
	int	rc = 0;
	int	loopcount = 0;

	locks = &sem->idbl->locks[0];
#ifdef NEVER
	if(checktable(sem, t, 0))
	{
		DBGMSG(-1, (999, NULL, "Checking table %s failed", t->name));
	}
#endif
	for(l = LOCKS(t->hlock); l; l = LOCKS(l->ptl))
	{
		if(l->status == LOCK_GRANTED && l->sid != sid)
			rc |= l->type;
		if(++loopcount > MAX_LOCKS)
			break;
	}
	return rc;
}

/******************************************************************/

static int
calcmystate(sem, t, sid)
DBLOCK	*sem;
LTABLE	*t;
ulong	sid;
{
	LOCK	*l, *locks;
	int	rc = 0;
	int	loopcount = 0;

	locks = &sem->idbl->locks[0];
#ifdef NEVER
	if(checktable(sem, t, 0))
	{
		DBGMSG(-1, (999, NULL, "Checking table %s failed", t->name));
	}
#endif
	for(l = LOCKS(t->hlock); l; l = LOCKS(l->ptl))
	{
		if(l->status == LOCK_GRANTED && l->sid == sid)
			rc |= l->type;
		if(++loopcount > MAX_LOCKS)
			break;
	}
	return rc;
}

/******************************************************************/

static int
recalcstate(DDIC *ddic, LTABLE *t)
/* Updates `t->status' to bitwise-OR of lock types currently granted on `t',
 * and returns that value. (? -KNG)
 */
{
	int newstatus;
	DBLOCK	*sem;

	sem = ddic->dblock;
	newstatus = calcstate(sem, t, -1);
#ifdef TX_USE_SEM_ARRAYS
#ifdef _WIN32
	if(newstatus != t->status)
	{
		char *ename, *cp;
		HANDLE event;
		ename = TXstrcat3(ddic->pname, "/", t->name);
		for(cp = ename; *cp; cp++)
		{
			switch(*cp)
			{
				case '\\':
				case '/':
					*cp = '_';
					break;
			}
		}
		event = TXCreateEvent(ename, FALSE, 0);
		if(event)
		{
			SetEvent(event, 500);
			CloseHandle(event);
		}
	}
#endif
#endif
	t->status = newstatus;
	return t->status;
}

/******************************************************************/

int
dbunlock(ddic, sid, tblid, type, tname)
DDIC	*ddic;
ulong	sid;
long	*tblid;
int	type;
char	*tname;
{
	static const char	fn[] = "dbunlock";
	DBLOCK	*sem;
	LOCK	*l;
	LTABLE	*t;
	int	rc = -1;
	ft_counter	*tc, b4;

#ifdef NOLOCK
	return 0;
#else /* !NOLOCK */
	if(ddic->nolocking)
		return 0;
	sem = ddic->dblock;
	if(!sem)
	{
		return 0;
	}
	if ((ulong)sid >= (ulong)NSERVERS)
	{
		putmsg(MERR, fn, ServerIdOutOfRangeFmt, (long)sid);
		return(-1);
	}
	if (semlock(ddic->pmbuf, sem, TXbool_True) != -1)
	{
		if(sem->idbl->servers[sid].pid != TXgetpid(0))
		{
			putmsg(MERR, NULL, "Server ID (%d) owned by %u, not %u in lock structure for %s",
			       (int)sid, (unsigned)sem->idbl->servers[sid].pid,
			       (unsigned)TXgetpid(0), ddic->epname);
			semunlock(ddic->pmbuf, sem);
			return -1;
		}
		t = gettable(sem, tblid, tname);
		if(t)
		{
			l = getlock(sem, t, sid, type);
			if(l)
			{
				DBGMSG(1, (999, NULL, "Removing lock type %d from %s", l->type, t->name));
				tc = (type == INDEX_READ ||
				      type == INDEX_WRITE ||
				      type == INDEX_VERIFY ? &t->iwrite :
				      &t->twrite);
				b4 = *tc;
				rmlock(sem, l, t);
				makefree(sem, l);
				rc = 0;
				if (!TXTRACELOCKS_IS_OFF())
					tx_loglockop(ddic, "dbunlock", type,
						t, NULL, &b4, tc, &rc);
			}
			else if(TX_LOCKVERBOSELEVEL())
				putmsg(MWARN, NULL, "No lock on %s %d, %d in lock structure for %s",
				       tname, (int)sid, (int)type,
				       ddic->epname);
			recalcstate(ddic, t);
		}
		else if(TX_LOCKVERBOSELEVEL())
			putmsg(MWARN, NULL, "No table %s in lock structure for %s", tname, ddic->epname);
		semunlock(ddic->pmbuf, sem);
	}
	return rc; /* WTF */
#endif /* !NOLOCK */
}

/******************************************************************/
#ifdef CLEAN_STOP

int
TXinlocks(ddic)
DDIC *ddic;
{
	DBLOCK	*sem;
	static const char	Fn[] = "inlocks";
	LOCK	*l, *locks, *tl;
	LTABLE	*t;
	LSERVER *server;
	int	rc = 0;
	int	sid, tlid;
	int	loopcount = 0;
	PID_T	pid;

#ifdef NOLOCK
	return 0;
#else /* NOLOCK */
	if(ddic->nolocking)
		return 0;
	sem = ddic->dblock;
	if(sem->idbl->curpid == TXgetpid(0)) /* We have semaphore */
		return 1;

	sid = ddic->sid;
	if ((ulong)sid >= (ulong)NSERVERS)
	{
		putmsg(MERR, Fn, ServerIdOutOfRangeFmt, (long)sid);
		return(0);
	}
	server = &sem->idbl->servers[sid];
	if(server->pid != TXgetpid(0))
	{
		return 0;	/* We are all confused. */
	}
	pid = server->pid;
	locks = &sem->idbl->locks[0];

	if(VALIDLOCK(server->hlock) && LOCKS(server->hlock)->nsl != -1)
	{ /* Bad chain */
		putmsg(MERR, Fn, "Server lock chain bad in lock structure for %s", ddic->epname);
	}
	for(tlid = server->hlock, tl = LOCKS(tlid);
	    VALIDLOCK(tlid);
	    tlid = tl->psl, tl = LOCKS(tlid))
	{
		if(VALIDLOCK(tl->nsl) && LOCKS(tl->nsl)->psl != tlid)
		{
			putmsg(MERR, Fn, "Bad lock chain nsl->psl != me in lock structure for %s", ddic->epname);
		}
		if(VALIDLOCK(tl->psl) && LOCKS(tl->psl)->nsl != tlid)
		{
			putmsg(MERR, Fn, "Bad lock chain psl->nsl != me in lock structure for %s", ddic->epname);
		}
		if(tl->nsl == tlid || tl->psl == tlid)
		{
			putmsg(MERR, Fn, "Bad lock chain loop in lock structure for %s", ddic->epname);
		}
		if(tl->sid != sid)
		{
			putmsg(MERR, Fn, "Bad lock wrong sid in lock structure for %s", ddic->epname);
		}
		if(tl->status == LOCK_GRANTED)
			rc = 1;
		if(++loopcount>MAX_LOCKS)
		{
			putmsg(MERR, Fn, "Lock chain too long in lock structure for %s", ddic->epname);
			break;
		}
	}
	return rc; /* WTF */
#endif /* NOLOCK */
}

#endif /* CLEAN_STOP */

/* ------------------------------------------------------------------------ */

PID_T
TXddicGetDbMonitorPid(DDIC *ddic)
/* Returns PID of db monitor, 0 if none currently running, or -1 if
 * indeterminable.
 */
{
	return(ddic &&
	       ddic->dblock &&
	       ddic->dblock->idbl ?
	       ddic->dblock->idbl->servers[0].pid : -1);
}

int
TXddicGetNumAttachedServers(DDIC *ddic)
/* Returns number of servers attached to locks, or -1 if indeterminable.
 */
{
	if (ddic && ddic->dblock)
		return(TXdblockGetNumAttachedServers(ddic->dblock));
	else
		return(-1);
}

/******************************************************************/

int
TXrmlocks(const char *db, int forceremoval, int verbose)
{
	static CONST char	Fn[] = "TXrmlocks";
#ifdef USE_SHM
	SEMVARS;
#endif /* USE_SHM */
	DBLOCK	*dblck;
#ifndef _WIN32
	int	j;
#endif /* !_WIN32 */
	int	sid, timeout;
	int	i;
	char 	fpath[1024];
#ifdef USE_SHM
	int	shmid = -1;
#endif
#if !defined(USE_NTSHM) && !defined(USE_POSIX_SEM) && defined(HAVE_SEM)
	int	semid = -1;
#endif
	int	monitorprocessid = 0;
	int	nprocesses = 0;
	int	savverb, ret;
	TXCREATELOCKSMETHOD	methods[TXCREATELOCKSMETHOD_NUM];

	savverb = TXgetlockverbose();
	TXsetlockverbose(verbose);

	if(db)
	{
		fullpath(fpath, (char *)db, 1024);
	}
	else
	{
		fullpath(fpath, ".", 1024);
	}
        if(!fisdir(fpath))
        {
		putmsg(MERR+UGE, NULL, "Database %s does not exist",fpath);
		goto err;
        }
	if(verbose)
		putmsg(MINFO, NULL, "About to open locking for %s", fpath);
	fflush(stderr);
	fflush(stdout);

	if (TXApp)
	{
		memcpy(methods, TXApp->createLocksMethods,
		       TXCREATELOCKSMETHOD_NUM*sizeof(TXCREATELOCKSMETHOD));
		timeout = TXApp->createLocksTimeout;
	}
	else
	{
		putmsg(MWARN, Fn,
		       "Internal error: TXApp not initialized, using createlocks method direct for %s", fpath);
		for (i = 0; i < (int)TXCREATELOCKSMETHOD_NUM; i++)
			methods[i] = TXCREATELOCKSMETHOD_UNKNOWN;
		methods[0] = TXCREATELOCKSMETHOD_DIRECT;
		timeout = 10;
	}
	dblck = TXdblockOpenViaMethods(TXPMBUFPN, fpath, methods, timeout,
				       &sid);

	if(dblck && forceremoval)
	{
		if (verbose >= 2)
			putmsg(MINFO, NULL, "Locking semaphore");
		i = semlock(TXPMBUFPN, dblck, TXbool_True);
		if (verbose >= 2)
			putmsg(MINFO, NULL, "Deleting locks");
		for(i=0; i < MAX_LOCKS; i++)
		{
			destroylock(dblck, i);
		}
#ifdef USE_SHM
		shmid = dblck->idbl->shmid;
#endif /* USE_SHM */
#if !defined(USE_NTSHM) && !defined(USE_POSIX_SEM) && defined(HAVE_SEM)
		semid = dblck->idbl->semid;
#endif
		nprocesses = dblck->idbl->nservers - 1; /* We are one */
		monitorprocessid = dblck->idbl->servers[0].pid;
		if (verbose >= 2)
			putmsg(MINFO, NULL, "Clearing lock structure");
		TXinitshared(dblck->idbl);
		if (verbose >= 2)
			putmsg(MINFO, NULL, "Unlocking semaphore");
		semunlock(TXPMBUFPN, dblck);
	}
	if(dblck)
	{
		if(verbose)
			putmsg(MINFO, NULL, "About to close locking");
		dblck = closedblock(TXPMBUFPN, dblck, sid,
				    TXbool_False /* !readOnly */);
	}
	if(forceremoval)
	{
		if(monitorprocessid > 0)
		{
#ifdef _WIN32
			char MyName[30];
			HANDLE hEvent;

			sprintf(MyName, "TEXIS_MONITOR_STOP_%d", monitorprocessid);
			hEvent = TXOpenEvent(EVENT_MODIFY_STATE, FALSE, MyName, 0);
			if(hEvent)
			{
				if(verbose)
					putmsg(MINFO, NULL, "Killing database monitor pid %u (%d other server%s)",
					(unsigned)monitorprocessid, nprocesses,
					(nprocesses > 1 ? "s" : ""));
				SetEvent(hEvent);
				CloseHandle(hEvent);
				sleep(2); /* Give it a chance to quit */
			}
		}
#else /* !_WIN32 */
			if(verbose >= 1)
				putmsg(MINFO, NULL, "Killing database monitor pid %u (%d other server%s)",
					(unsigned)monitorprocessid, nprocesses,
					(nprocesses > 1 ? "s" : ""));
			TXseterror(0);
			if (kill(monitorprocessid, SIGTERM) == 0)
			{
				for (j = 0; j < 24; j++)
				{
					if (!(j % 4))
					{
						if (j && !(j % 12))
							kill(monitorprocessid,
								SIGTERM);
					}
					TXsleepmsec(250, 1);
					TXseterror(0);
					if (kill(monitorprocessid, 0) != 0 &&
						TXgeterror() == ESRCH)
					{
						if (verbose >= 1)
							putmsg(MINFO, NULL,
						"Database monitor pid %u gone",
							       (unsigned)monitorprocessid);
						break;
					}
				}
				if (j >= 24)		/* not dead yet... */
				{
					kill(monitorprocessid, SIGKILL);
					for (j = 0; j < 4; j++)
					{
						TXsleepmsec(250, 1);
						TXseterror(0);
						if (kill(monitorprocessid, 0) != 0 &&
							TXgeterror() == ESRCH)
							break;
					}
					if (j >= 4)
						putmsg(MWARN, CHARPN,
						       "Cannot kill database monitor process %u", (unsigned)monitorprocessid);
				}
			}
			else switch (TXgeterror())
			{
			case ESRCH: break;	/* PID does not exist */
			case EPERM:		/* permission denied */
			default:
				putmsg(MWARN, CHARPN,
				"Cannot kill database monitor process %u: %s",
				       (unsigned)monitorprocessid,
					TXstrerror(TXgeterror()));
			}
		}
#  ifdef USE_SHM
		if (shmid != -1)
		{
			if (verbose >= 1)
				putmsg(MINFO, CHARPN,
					"Deleting shared memory segment id %d",
					(int)shmid);
			SHMCTL(TXPMBUFPN, shmid, IPC_RMID, (void *)NULL);
		}
#  else /* !USE_SHM */
		strcat(fpath, "/SYSLOCKS");
		unlink(fpath);
#  endif /* !USE_SHM */
#  if !defined(USE_NTSHM) && !defined(USE_POSIX_SEM) && defined(HAVE_SEM)
		if (semid != -1) tx_delsem(TXPMBUFPN, semid);
#  endif
#endif /* !_WIN32 */
	}
	ret = 0;
	goto done;

err:
	ret = -1;
done:
	TXsetlockverbose(savverb);
	return(ret);
}

#endif /* OLD_LOCKING */
