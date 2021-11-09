/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#ifdef unix
#  include <sys/wait.h>
#endif /* unix */
#include "texint.h"
#include "http.h"
#include "scheduler.h"


/*
	SYSSCHEDULE

	Insert and run schedules
*/

#if defined(EPI_HAVE_VFORK) && !defined(EPI_VFORK_BROKEN)
#  define VFORK vfork
#else
#  define VFORK fork
#endif

#define TX_EVENTPN      ((TX_EVENT *)NULL)
#if EPI_OS_TIME_T_BITS == EPI_OS_INT_BITS
#  define TIME_T_MAX	((time_t)MAXINT)
#else
#  define TIME_T_MAX	((time_t)((unsigned)(~0) >> 1))
#endif


typedef struct SSPROC_tag
{
  struct SSPROC_tag     *next;
  PID_T                 pid;
  HANDLE                proc;                   /* for Windows */
  int                   xitcode;                /* exit code */
  ft_counter            id;                     /* id in SYSSCHEDULE */
  char                  cmd[TX_ALIGN_BYTES];
}
SSPROC;
#define SSPROCPN        ((SSPROC *)NULL)

typedef struct SSJOB_tag
{
  struct SSJOB_tag	*next;
  ft_counter		id;
  time_t                lastrun, start;
  char 			*sched, *vars, *comments, *options;
  char			suffix[TX_ALIGN_BYTES];
}
SSJOB;
#define SSJOBPN		((SSJOB *)NULL)

typedef struct SSFILE_tag
{
  struct SSFILE_tag	*next;
  int                   pid;
  time_t                init;
  SSJOB			*jobs;
  char                  script[TX_ALIGN_BYTES];
}
SSFILE;
#define SSFILEPN        ((SSFILE *)NULL)

/* Data passed to watchjobs(): */
typedef struct WATCHJOBSINFO_tag
{
  void  (*kickReaperFunc) ARGS((void));
}
WATCHJOBSINFO;
#define WATCHJOBSINFOPN ((WATCHJOBSINFO *)NULL)

/* wtf avoid a global for this? */
void    (*TXsysSchedKickReaperFunc) ARGS((void)) = NULL;

char    *TxSchedJobMutexName = CHARPN;      /* was "TEXIS_MONITOR_JOBS" */
char    *TxSchedNewJobEventName = CHARPN;   /* was "TEXIS_MONITOR_NEW_JOBS" */
double  TXschedJobMutexTimeoutSec = 1.0;

#ifdef _WIN32
static TXMUTEX		*ObjMutex = TXMUTEXPN;
/* Debug/trace stuff for ObjMutex: */
static CONST char       ObjMutexLockTimeoutFmt[] =
  "At timeout: ddicMutex lock count: %d last lock: %s:%d %1.6fs ago last unlock: %s:%d %1.6fs ago";
#  define AFTER_OBJ_MUTEX_TIMEOUT_MSG(fn, ddicMutex)            \
  if (ddicMutex)                                                \
    putmsg(MERR, fn, ObjMutexLockTimeoutFmt,                    \
           (int)(ddicMutex)->lockCount,                         \
           TXbasename((ddicMutex)->lastLockerFile),             \
           (ddicMutex)->lastLockerLine,                         \
           ((ddicMutex)->lastLockerTime ?                       \
            TXgettimeofday() - (ddicMutex)->lastLockerTime : 0.0), \
           TXbasename((ddicMutex)->lastUnlockerFile),           \
           (ddicMutex)->lastUnlockerLine,                       \
           ((ddicMutex)->lastUnlockerTime ?                     \
            TXgettimeofday() - (ddicMutex)->lastUnlockerTime : 0.0))

static HANDLE ObjEvent = NULL;
static CONST char	Timeout[] = "Timeout";
#endif /* _WIN32 */
static SSPROC	*Procs = SSPROCPN;		/* currently running jobs */
static int      NumProcs = 0;                   /* number of running jobs */
static SSFILE	*Files = SSFILEPN;		/* in-progress schedule(s) */

static CONST char       DateFmt[] = "%Y-%m-%d %H:%M:%S";
static const char	WhiteSpace[] = " \t\r\n\v\f";

/* Note: Do not recursively lock.  See also monitor.c, monsock.c: */
#define DDIC_LOCK(mutex, onErr)	{				\
  if ((mutex) != TXMUTEXPN && TXmutexLock(mutex, -1.0) != 1) 	\
		{ onErr; }	}
/* only call after DDIC_LOCK(): */
#define DDIC_UNLOCK(mutex)	{				\
	if ((mutex) != TXMUTEXPN) TXmutexUnlock(mutex); }

static void closesql ARGS((DDIC *ddic, TXMUTEX *ddicMutex));
static void
closesql(ddic, ddicMutex)
DDIC    *ddic;
TXMUTEX *ddicMutex;
/* Free the statement, so we don't hold SYSSCHEDULE open and prevent
 * -wipesched from dropping it under Windows.  KNG 010316
 */
{
#ifdef _WIN32
  DDIC_LOCK(ddicMutex, return);
  if (ddic != DDICPN && ddic->ihstmt != NULL)
    {
      SQLFreeStmt(ddic->ihstmt, SQL_DROP);
      ddic->ihstmt = NULL;
    }
  DDIC_UNLOCK(ddicMutex);
#else
  (void)ddic;
  (void)ddicMutex;
#endif /* _WIN32 */
}

#ifdef _WIN32
static DWORD WINAPI
watchjobs(LPVOID lpp)
/* Separate thread that waits for any currently-running <SCHEDULE> job
 * to finish, to obtain its exit status.  It then signals txsched_procreap().
 */
{
	static CONST char	fn[] = "watchjobs";
	DWORD		rc, finishedIdx, exitCode;
	HANDLE		*objs = NULL;
	int		aobjs = 0, nobjs, didSomething = 1;
	SSPROC		*proc;
	WATCHJOBSINFO	*watchjobsInfo = (WATCHJOBSINFO *)lpp;

	for (;;)
	{
		/* KNG 20100819 Bug 2406: if we did not accomplish something
		 * useful last loop, we might also fail this loop too, so
		 * sleep a bit to avoid large CPU usage during potential race:
		 */
		if (!didSomething) TXsleepmsec(100, 0);
		didSomething = 0;

		/* Populate local `nobjs' array with handles from `Procs': */
		switch (TXmutexLock(ObjMutex, -1.0))
		{
		case 1:				/* we have the ball */
			break;
		case 0:				/* timeout;should not happen*/
			AFTER_OBJ_MUTEX_TIMEOUT_MSG(fn, TXMUTEXPN);
			/* fall through: */
		case -1:			/* error */
		default:
		waitTryAgain:
			TXsleepmsec(30000, 1);	/* try again later */
			continue;
		}
		if (aobjs < NumProcs + 1)		/* realloc if needed */
		{
			if (objs != NULL) free(objs);
			aobjs += (aobjs >> 1) + 16;
			if (aobjs < NumProcs + 1) aobjs = NumProcs + 1;
			if ((objs=(HANDLE*)calloc(aobjs,sizeof(HANDLE)))==NULL)
			{
				TXmutexUnlock(ObjMutex);
				TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn,
						 aobjs, sizeof(HANDLE));
				aobjs = 0;
				Sleep(30000);		/* try again later */
				continue;
			}
		}
		nobjs = 0;
		objs[nobjs++] = ObjEvent;	/* must be first */
#define NUM_NON_PROC_OBJS	1		/* for ObjEvent */
		for (proc = Procs; proc != SSPROCPN; proc = proc->next)
			if (proc->proc != INVALID_HANDLE_VALUE)
				objs[nobjs++] = proc->proc;
		TXmutexUnlock(ObjMutex);

		rc = WaitForMultipleObjects(nobjs, objs, FALSE, INFINITE);
		switch (rc)
		{
		case WAIT_OBJECT_0:		/* `ObjEvent' signalled */
			didSomething = 1;
			continue;		/* re-load `objs' */
		case WAIT_ABANDONED_0:		/* `ObjEvent' closed? */
			/* We must have `ObjEvent', or we cannot be
			 * signalled when `Procs' changes:
			 */
			putmsg(MERR, fn,
			 "Internal error: ObjEvent abandoned, thread exiting");
			goto err;
		}
		if (rc >= (WAIT_OBJECT_0 + NUM_NON_PROC_OBJS) &&
		    rc < (WAIT_OBJECT_0 + nobjs))
		{				/* a process ended */
			didSomething = 1;	/* we reaped something */
			finishedIdx = rc - WAIT_OBJECT_0;
			if (GetExitCodeProcess(objs[finishedIdx], &exitCode)
			    == 0)
				continue;		/* failed */

			/* Copy the exit code back to `Procs' array,
			 * make a note of it for txsched_procreap(),
			 * and close the process handle (all done with it):
			 */
			switch (TXmutexLock(ObjMutex, -1.0))
			{
			case 1:			/* we have the ball */
				break;
			case 0:			/* timeout;should not happen*/
				AFTER_OBJ_MUTEX_TIMEOUT_MSG(fn, TXMUTEXPN);
				/* fall through: */
			case -1:		/* error */
			default:
				goto waitTryAgain;
			}
			for (proc = Procs; proc !=SSPROCPN; proc = proc->next)
			{
				if (proc->proc == objs[finishedIdx])
				{		/* found it in `Procs' */
					/* Set the exit code: */
					proc->xitcode = exitCode;
					/* And clear the handle as a note
					 * to txsched_procreap() to remove:
					 */
					proc->proc = INVALID_HANDLE_VALUE;
					break;
				}
			}
			TXmutexUnlock(ObjMutex);
			CloseHandle(objs[finishedIdx]);
			objs[finishedIdx] = INVALID_HANDLE_VALUE;

			/* Wake up txsched_procreap(), so it can remove
			 * the flagged `Procs' entry and report the exit:
			 */
			if (watchjobsInfo != WATCHJOBSINFOPN &&
			    watchjobsInfo->kickReaperFunc)
				watchjobsInfo->kickReaperFunc();
		}
		else if (rc >= (WAIT_ABANDONED_0 + NUM_NON_PROC_OBJS) &&
			 rc < (WAIT_ABANDONED_0 + nobjs))
		{
			finishedIdx = rc - WAIT_ABANDONED_0;
			putmsg(MERR, fn,
				"Internal error: Job handle 0x%lx abandonded",
				(unsigned long)objs[finishedIdx]);
			/* WTF what do we do? */
			TXsleepmsec(5000, 1);	/* slow potential race */
		}
	}
err:
	if (watchjobsInfo != WATCHJOBSINFOPN) free(watchjobsInfo);
	watchjobsInfo = WATCHJOBSINFOPN;
	return(0);
}

static int startwatchjobs ARGS((void));
static int
startwatchjobs()
/* Returns 0 on error, nonzero if ok.
 */
{
  static CONST char     fn[] = "startwatchjobs";
  WATCHJOBSINFO         *watchjobsInfo = WATCHJOBSINFOPN;
  int                   closeObjMutex = 0, closeObjEvent = 0, ret;

  /* ObjMutex should be unique to this Texis Monitor process, so that
   * we do not deadlock waiting for another process entirely.  Could
   * probably give it a NULL name to ensure that, but for now, check for
   * already-exists (which helps check for unique Texis Monitor process):
   * KNG 20060822 mutex and event now default to NULL for local name;
   * can be set with [Scheduler] Job Mutex and [Scheduler] New Job Event
   */
  TXseterror(0);                                /* so we can detect dups */
  if ((ObjMutex = TXmutexOpen(TXPMBUFPN /* must be special */,
			      TxSchedJobMutexName)) == TXMUTEXPN)
    goto err;
  closeObjMutex = 1;
  if (TXgeterror() == ERROR_ALREADY_EXISTS)
    {
      putmsg(MERR, fn, "Will not re-use mutex: Already exists");
      goto err;
    }
  if ((ObjEvent = TXCreateEvent(TxSchedNewJobEventName, FALSE, 1)) == NULL)
    goto err;
  closeObjEvent = 1;

  watchjobsInfo = (WATCHJOBSINFO *)calloc(1, sizeof(WATCHJOBSINFO));
  if (watchjobsInfo == WATCHJOBSINFOPN)
    {
      TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn, 1, sizeof(WATCHJOBSINFO));
      goto err;
    }
  watchjobsInfo->kickReaperFunc = TXsysSchedKickReaperFunc;

  if (CreateThread(NULL, 0, watchjobs, watchjobsInfo, 0, NULL) == NULL)
    {
      putmsg(MERR, fn, "Could not create watchjobs thread: %s",
             TXstrerror(TXgeterror()));
      goto err;
    }
  watchjobsInfo = WATCHJOBSINFOPN;              /* thread owns it now */
  closeObjEvent = closeObjMutex = 0;
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  if (closeObjEvent)
    {
      CloseHandle(ObjEvent);
      ObjEvent = NULL;
    }
  if (closeObjMutex) ObjMutex = TXmutexClose(ObjMutex);
  watchjobsInfo = TXfree(watchjobsInfo);
  return(ret);
}
#endif /* _WIN32 */

static int txsched_procadd ARGS((PID_T pid, HANDLE proc, ft_counter id,
                                 char *cmd, TXMUTEX *ddicMutex,
				 int lockFlags));
static int
txsched_procadd(pid, proc, id, cmd, ddicMutex, lockFlags)
PID_T           pid;    /* process id */
HANDLE          proc;   /* not INVALID_HANDLE_VALUE; process handle for Win */
ft_counter      id;     /* SYSSCHEDULE table id for this guy */
char            *cmd;   /* command line */
TXMUTEX		*ddicMutex;
int		lockFlags;	/* (in) 0x1: have ObjMutex lock already */
/* Adds `cmd' to list of running processes.
 * Returns 0 on success, -1 on error.
 */
{
  static CONST char	fn[] = "txsched_procadd";
  SSPROC		*p;

  p = (SSPROC *)TXcalloc(TXPMBUFPN, fn, 1, (sizeof(SSPROC) - sizeof(p->cmd)) +
                         strlen(cmd) + 1);
  if (!p) return(-1);                           /* error */

  p->pid = pid;
  p->proc = proc;
  p->xitcode = 0;
  p->id = id;
  strcpy(p->cmd, cmd);

#ifdef _WIN32
  if (!(lockFlags & 0x1))                       /* do not yet have lock */
    {
      if (ObjMutex == TXMUTEXPN && !startwatchjobs()) return(-1);
      switch (TXmutexLock(ObjMutex, TXschedJobMutexTimeoutSec))
        {
        case 1:                                 /* we have the ball */
          break;
        case 0:                                 /* timeout */
          AFTER_OBJ_MUTEX_TIMEOUT_MSG(fn, ddicMutex);
          return(-1);
        case -1:                                /* error */
        default:
          return(-1);				/* error */
        }
    }
#else /* !_WIN32 */
  (void)ddicMutex;
  (void)lockFlags;
#endif /* !_WIN32 */
  p->next = Procs;
  Procs = p;
  NumProcs++;
#ifdef _WIN32
  if (!(lockFlags & 0x1))
    TXmutexUnlock(ObjMutex);
  SetEvent(ObjEvent);                   /* alert watchjobs() to new job */
#endif /* _WIN32 */

  return(0);					/* success */
}

void
txsched_procreap(ddic, ddicMutex, verbose)
DDIC    *ddic;
TXMUTEX *ddicMutex;     /* (in, opt.) mutex for `ddic' */
int	verbose;
/* Called after SIGCLD and/or periodically, to reap scheduled processes
 * and update the SYSSCHEDULE table.  (Under Windows, processes were
 * actually already system-reaped by separate watchjobs() thread;
 * everything else occurs here as with Unix.)
 * >>> NOTE: must be called from main thread. <<<
 * `verbose' bit flags: see [Scheduler] Verbose in monitor.c
 */
{
  static CONST char	fn[] = "txsched_procreap";
  static CONST char     job[] = "Job";
  SSPROC		*proc, *pproc;
  int			och, xit, inWait = 0, sig = 0;
  PID_T                 pid = 0;
  ft_long		xitsig;
  char			*procDesc, *cmd;
  CONST char            *xitdesc;
  char                  jobDescBuf[32 + TX_COUNTER_HEX_BUFSZ];
  char                  counterBuf[TX_COUNTER_HEX_BUFSZ];
#ifndef _WIN32
  PID_T			res;
  int			tries, ret;
#endif /* !_WIN32 */

#ifdef _WIN32
  if (ObjMutex == TXMUTEXPN) goto finally;
#endif /* _WIN32 */

  for (;;)
    {
      proc = SSPROCPN;
#ifdef _WIN32
      xit = 0;
#else /* !_WIN32 */
      tries = 0;
      TXsetInProcessWait(1);                    /* let others know */
      inWait++;
      do
        {
          TXseterror(0);
          res = waitpid((PID_T)(-1), &ret, WNOHANG);
        }
      while (res == (PID_T)(-1) && TXgeterror() == EINTR && ++tries < 25);
      if (res == (PID_T)(-1)) break;            /* error */
      if (res == (PID_T)0) break;               /* no children available */
      pid = res;
      sig = xit = 0;
      if (WIFSIGNALED(ret)) sig = WTERMSIG(ret);
      else if (WIFEXITED(ret)) xit = WEXITSTATUS(ret);
      /* hold off TXsetInProcessWait(0) until after TXsetprocxit() */
#endif /* !_WIN32 */

      /* Look up process in `Procs': - - - - - - - - - - - - - - - - - - - */
#ifdef _WIN32
      switch (TXmutexLock(ObjMutex, TXschedJobMutexTimeoutSec))
        {
        case 1:                                 /* we have the ball */
          break;
        case 0:                                 /* timeout */
          goto finally;                         /* will reap next time */
        case -1:                                /* error */
        default:
          goto finally;
        }
#endif /* _WIN32 */
      for (pproc = SSPROCPN, proc = Procs;
#ifdef _WIN32
           proc != SSPROCPN && proc->proc != (void *)INVALID_HANDLE_VALUE;
#else /* !_WIN32 */
           proc != SSPROCPN && proc->pid != res;
#endif /* !_WIN32 */
           pproc = proc, proc = proc->next)
        ;
      if (proc != SSPROCPN)                     /* remove from list */
        {
          if (pproc != SSPROCPN) pproc->next = proc->next;
          else Procs = proc->next;
          NumProcs--;
        }
#ifdef _WIN32
      TXmutexUnlock(ObjMutex);
#endif /* _WIN32 */

      /* Handle `proc' if found: - - - - - - - - - - - - - - - - - - - - - */
      if (proc == SSPROCPN)                     /* unknown child process */
        {
#ifdef _WIN32
          procDesc = cmd = CHARPN;
          xitdesc = CHARPN;
          break;
#else /* !_WIN32 */
          /* Process not found in our job list; look in system proc list: */
          switch (TXsetprocxit(res, 0, sig, xit, &procDesc, &cmd, &xitdesc))
            {
            case 2:                             /* found process; save */
              procDesc = cmd = CHARPN;          /* do not report */
              xitdesc = CHARPN;
            case 1:                             /* found process */
              break;
            default:                            /* not found */
              procDesc = "Process";
              xitdesc = cmd = CHARPN;
              break;
            }
          /* call TXsetInProcessWait(0) ASAP after TXsetprocxit(): */
          for ( ; inWait > 0; inWait--) TXsetInProcessWait(0);
#endif /* !_WIN32 */
        }
      else
        {
          procDesc = (char *)job;
          cmd = proc->cmd;
          xitdesc = CHARPN;
          pid = proc->pid;
        }
      for ( ; inWait > 0; inWait--) TXsetInProcessWait(0);      /* ASAP */
#ifdef _WIN32
      xitsig = xit = proc->xitcode;
#else /* !_WIN32 */
      if (sig)
        xitsig = -sig;
      else
        xitsig = xit;
#endif /* !_WIN32 */
      if (procDesc == job)                      /* SYSSCHEDULE job */
        {
          if (proc != SSPROCPN)
            TXprintHexCounter(counterBuf, sizeof(counterBuf), &proc->id);
          else
            TXstrncpy(counterBuf, " ?", sizeof(counterBuf));
          htsnpf(jobDescBuf, sizeof(jobDescBuf), "%s %s",
                 procDesc, counterBuf);
          procDesc = jobDescBuf;
        }
      /* Bug 5385: only report if known (`procDesc'): */
      if (procDesc && ((verbose & 0x2) || xit || sig))
        TXreportProcessExit(TXPMBUFPN, NULL, procDesc, cmd, pid,
                            (sig ? sig : xit), !!sig, xitdesc);
      if (proc != SSPROCPN)
        {
	  HSTMT istmt2;
	  long	sz;
	  char  counterstr[TX_COUNTER_HEX_BUFSZ + 100];

          if (ddic == DDICPN)
            {
              putmsg(MERR, fn, "Internal error: no DDIC");
              goto afterDdic;
            }
          DDIC_LOCK(ddicMutex, goto mutexErr);
          if (TXddicstmt(ddic) != -1)		/* statement alloc success */
	    {
	      TXpushid(ddic, 0, 0);		/* su to _SYSTEM */
              och = ddic->ch;
              ddic->ch = 0;                     /* don't count license hits */
	      if (SQLAllocStmt(ddic->dbc, &istmt2) != SQL_SUCCESS)
		{
		  putmsg(MERR, fn, "Cannot alloc statement");
		  goto cont;
		}
	      if (SQLPrepare(istmt2, (byte *)"UPDATE SYSSCHEDULE SET PID = 0, LASTEXIT = 'now', LASTEXITCODE = ? WHERE id = ?;", SQL_NTS) != SQL_SUCCESS)
		{
		  putmsg(MERR, fn, "Cannot update SYSSCHEDULE after process exit");
		  goto cont;
		}
	      TXprintHexCounter(counterstr, sizeof(counterstr), &proc->id);
	      sz = sizeof(xitsig);
	      if (SQLSetParam(istmt2, 1, SQL_C_LONG, SQL_INTEGER, sz,
                              (SWORD)sz, &xitsig, &sz) != SQL_SUCCESS)
		{
		  putmsg(MERR, fn, "Cannot set param 1");
		  goto cont;
		}
	      sz = (long)strlen(counterstr);
	      if (SQLSetParam(istmt2, 2, SQL_C_CHAR, SQL_CHAR, sz, (SWORD)sz,
			      counterstr, &sz) != SQL_SUCCESS)
		{
		  putmsg(MERR, fn, "Cannot set param 2");
		  goto cont;
		}
	      if (SQLExecute(istmt2) != SQL_SUCCESS)
		{
		  putmsg(MERR, fn, "Cannot execute SQL update");
		  goto cont;
		}
	      SQLFetch(istmt2);
	      SQLFreeStmt(istmt2, SQL_DROP);
	    cont:
              ddic->ch = och;
	      TXpopid(ddic);			/* restore perms */
            }
          else
            putmsg(MERR + MAE, fn, "TXddicstmt() failed");
          DDIC_UNLOCK(ddicMutex);
          goto afterDdic;
        mutexErr:
          putmsg(MERR, fn, "Cannot lock DDIC mutex");
        afterDdic:
          free(proc);
          proc = SSPROCPN;
        }
    }

  /* Clear in-process-wait before TXcleanproc() */
  for ( ; inWait > 0; inWait--) TXsetInProcessWait(0);
  TXcleanproc();
  closesql(ddic, ddicMutex);			/* call after DDIC_UNLOCK() */
#ifdef _WIN32
finally:
#endif /* _WIN32 */
  for ( ; inWait > 0; inWait--) TXsetInProcessWait(0);
  return;
}

/******************************************************************/

int
TXcreatesysschedule(DDIC *ddic)
/* NOTE: assumes DDIC_LOCK() already obtained.
 */
{
	static CONST char Fn[] = "TXcreatesysschedule";
	DD *dd;
	DBTBL *dbtbl;
	char *fname;
	int   ret, n, svcr, svgr;

        TXpushid(ddic, 0, 0);	/* su so that _SYSTEM owns the table */
        svcr = stxalcrtbl(1);   /* allow create table */
        svgr = stxalgrant(1);   /* allow grant */

	dd = opendd();
	(void)ddsettype(dd, TEXIS_FAST_TABLE);
	if (!dd)
	{
	  n = sizeof(DD);
	merr:
	  TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, Fn, n, 1);
	  goto err;
	}
	putdd(dd, "id", "counter", 1, 0);
	putdd(dd, "PATH", "varchar", 20, 0);
	putdd(dd, "SUFFIX", "varchar", 20, 0);
	putdd(dd, "SCHEDULE", "varchar", 20, 0);
	putdd(dd, "FIRSTRUN", "date", 1, 0);
	putdd(dd, "LASTRUN", "date", 1, 0);
	putdd(dd, "LASTEXIT", "date", 1, 0);
	putdd(dd, "LASTEXITCODE", "long", 1, 0);
	putdd(dd, "PID", "long", 1, 0);         /* 0 if not running */
	putdd(dd, "NEXTRUN", "date", 1, 0);
	putdd(dd, "EXECUTIONS", "long", 1, 0);
	putdd(dd, "VARS", "varchar", 20, 0);
	putdd(dd, "COMMENTS", "varchar", 80, 0);
	putdd(dd, "STATUS", "long", 1, 0);      /* 0: runnable; !0: disabled*/
	putdd(dd, "OPTIONS", "varchar", 80, 0);
	/* OPTIONS: space-separated list of option tokens:
	 *   first      First schedule for this PATH value
	 *   last       Last schedule for this PATH value
	 *   off        Remove schedule(s) (not stored in SYSSCHEDULE)
	 *   istag      !0: <schedule>-directive; 0: <schedule> func or cmdln
	 *   all        All schedules, not just istag or !istag ones
	 *                (not stored; used when deleting schedules)
	 *   mutex      Mutex each run: do not run job if still running
	 */
	if ((fname = malloc(PATH_MAX)) == CHARPN)
          {
            closedd(dd);
	    n = PATH_MAX;
            goto merr;
	  }
#if 0
	strcpy(fname, ddic->pname);
	strcat(fname, TEXISFNSYSSCHED);
#else
	strcpy(fname, TEXISFNSYSSCHED);
#endif
	dbtbl = createdbtbl(ddic, dd, fname, TEXISSYSSCHED,
			    "Texis Schedule", 'S');
	closedd(dd);
        free(fname);
	if (!dbtbl)
		goto err;
        if (dbgetperms(dbtbl, ddic) >= 0)
	  permgrant(ddic, dbtbl, "PUBLIC", PM_SELECT);
	closedbtbl(dbtbl);
	ret = 0;
	goto done;

err:
	ret = -1;
done:
        stxalcrtbl(svcr);
        stxalgrant(svgr);
	TXpopid(ddic);
	return ret;
}

/******************************************************************/

typedef enum SCHED_tag
{
	SCHED_FIRST	= (1 << 0),
	SCHED_LAST	= (1 << 1),
	SCHED_OFF	= (1 << 2),
	SCHED_ISTAG	= (1 << 3),
	SCHED_ALL	= (1 << 4),
	SCHED_MUTEX	= (1 << 5)
}
SCHED;

static SCHED
cookoptions(const char *options)
/* Thread-unsafe.
 */
{
	const char	*token = NULL;
	size_t		tokenLen = 0;
#if TX_VERSION_MAJOR >= 8
	SCHED rc = SCHED_MUTEX;			/* Bug 5486 */
#else /* TX_VERSION_MAJOR < 8 */
	SCHED rc = (SCHED)0;
#endif /* TX_VERSION_MAJOR < 8 */
#define IS_MATCH(s)	\
	(tokenLen == sizeof(s) - 1 && 0 == strnicmp(token, s, tokenLen))

	for (token = options;
	     token += strspn(token, WhiteSpace), *token;
	     token += tokenLen)
	{
		tokenLen = strcspn(token, WhiteSpace);
		if (IS_MATCH("first"))
			rc = rc | SCHED_FIRST;
		else if (IS_MATCH("last"))
			rc = rc | SCHED_LAST;
		else if (IS_MATCH("off"))
			rc = rc | SCHED_OFF;
		else if (IS_MATCH("istag"))
			rc = rc | SCHED_ISTAG;
		else if (IS_MATCH("all"))
			rc = rc | SCHED_ALL;
		else if (IS_MATCH("mutex"))
			rc = rc | SCHED_MUTEX;
		else if (IS_MATCH("nomutex"))
			rc &= ~SCHED_MUTEX;
	}
	return rc;
#undef IS_MATCH
}

/******************************************************************/

char *
TXdelscheduledevent(DDIC * ddic, char *path, char *suffix, char *schedule,
		    char *vars, char *comments, char *options, int verbose)
/* Returns NULL on success, else static error message.
 * `options' is space-separated list of options.
 * If "all" in `options', deletes all jobs.  Otherwise, if
 * "istag" in `options', deletes istag jobs, else deletes non-istag jobs.
 * >>>>> NOTE: assumes mutex already held under Windows and DDIC_LOCK() <<<<<
 */
{
	long sz;
	char *tbname, fname[10];
	char	stmt[256];
	char *ret;
	int rowcount = 0, pop = 0, rc, och = 0;
	int	restoreSettings = 0;
	SCHED	optcode;
	int	saveAllinear = 0, saveAlpostproc = 0;

	(void)schedule;
	(void)vars;
	(void)comments;
	if (TXddicstmt(ddic) == -1)
	  {
	    ret = "Internal SQL error";
	    goto done;
	  }
	tbname = ddgettable(ddic, TEXISSYSSCHED, fname, 0);
	if (!tbname)
	{
		if (TXcreatesysschedule(ddic) == -1)
		{
			ret = "Cannot create SYSSCHEDULE table";
			goto done;
		}
	}
	else
	{
		free(tbname);
	}
	if(!path)
	  {
	    ret = "No path";
	    goto done;
	  }
	optcode = cookoptions(options);
	strcpy(stmt, "DELETE FROM SYSSCHEDULE WHERE pathcmp(PATH, ?) = 0");
	if (suffix)
	  strcat(stmt, " AND SUFFIX = ?");
	if (!(optcode & SCHED_ALL))
	{
		if(SCHED_ISTAG == (optcode & SCHED_ISTAG))
			strcat(stmt, " AND OPTIONS LIKE 'istag'");
		else
			strcat(stmt, " AND OPTIONS NOT LIKE 'istag'");
		/* Version 6+ Texis has query protect on by default: */
		if (TXget_globalcp() != APICPPN)
		{
			saveAllinear = globalcp->allinear;
			globalcp->allinear = 1;
			saveAlpostproc = globalcp->alpostproc;
			globalcp->alpostproc = 1;
			restoreSettings = 1;
		}
	}
	strcat(stmt, ";");

	TXpushid(ddic, 0, 0);		/* su to _SYSTEM */
	och = ddic->ch;
        ddic->ch = 0;                   /* don't count license hits */
	pop = 1;
	if (SQLPrepare (ddic->ihstmt, (byte *)stmt, SQL_NTS) != SQL_SUCCESS)
	{
		ret = "Cannot prepare SQL delete statement";
		goto done;
	}
	sz = (long)strlen(path);
	if (SQLSetParam(ddic->ihstmt, 1, SQL_C_CHAR, SQL_CHAR, sz, (SWORD)sz,
                        path, &sz) != SQL_SUCCESS)
	  {
	    ret = "Cannot set SQL param 1";
	    goto done;
	  }
	if (suffix)
	{
		sz = (long)strlen(suffix);
		if (SQLSetParam(ddic->ihstmt, 2, SQL_C_CHAR, SQL_CHAR, sz,
                                (SWORD)sz, suffix, &sz) != SQL_SUCCESS)
		  {
		    ret = "Cannot set SQL param 2";
		    goto done;
		  }
	}
	if ((rc = SQLExecute(ddic->ihstmt)) != SQL_SUCCESS)
	{
	  putmsg(MINFO, CHARPN, "error %d", rc);
		ret = "Cannot delete from SYSSCHEDULE table";
		goto done;
	}
	while (SQLFetch(ddic->ihstmt) == SQL_SUCCESS)
	{
		rowcount++;
		if (verbose & 0x8)
		{
			DBTBL		*resultTbl;
			FLD		*fld;
			char		*suffixVal = "?", *varsVal = "?";
			ft_counter	*idVal;
			char		counterBuf[TX_COUNTER_HEX_BUFSZ];

			strcpy(counterBuf, "?");
			/* Get `id', `SUFFIX', `VARS' from the DELETE: */
			resultTbl = ((LPSTMT)ddic->ihstmt)->outtbl;
			if (resultTbl)
			{
				fld = dbnametofld(resultTbl, "id");
				if (fld &&
				    (fld->type & DDTYPEBITS) == FTN_COUNTER &&
				    (idVal = (ft_counter *)getfld(fld, NULL))
				    != NULL)
					TXprintHexCounter(counterBuf,
						   sizeof(counterBuf), idVal);
				fld = dbnametofld(resultTbl, "SUFFIX");
				if (fld &&
				    (fld->type & DDTYPEBITS) == FTN_CHAR)
					suffixVal = (char *)getfld(fld, NULL);
				fld = dbnametofld(resultTbl, "VARS");
				if (fld &&
				    (fld->type & DDTYPEBITS) == FTN_CHAR)
					varsVal = (char *)getfld(fld, NULL);
			}
			putmsg(MINFO, CHARPN, "Job %s %s%s %s deleted",
			       counterBuf, path, suffixVal, varsVal);
		}
	}
	ret = CHARPN;
done:
	if (pop)
	{
		ddic->ch = och;
		TXpopid(ddic);
	}
	if (restoreSettings)
	{
		globalcp->allinear = saveAllinear;
		globalcp->alpostproc = saveAlpostproc;
	}
	closesql(ddic, TXMUTEXPN);
	return ret;
}

/******************************************************************/

static SSJOB *closessjoblist ARGS((SSJOB *ssj));
static SSJOB *
closessjoblist(ssj)
SSJOB   *ssj;
{
  SSJOB *ssjn;

  for ( ; ssj != SSJOBPN; ssj = ssjn)
    {
      ssjn = ssj->next;
      free(ssj);
    }
  return(SSJOBPN);
}

/******************************************************************/

static SSFILE *closessfile ARGS((SSFILE *ssf));
static SSFILE *
closessfile(ssf)
SSFILE  *ssf;
{
  SSFILE        *prev;

  if (ssf == SSFILEPN) return(SSFILEPN);
  if (Files == ssf)
    Files = Files->next;
  else if (Files != SSFILEPN)
    {
      for (prev = Files; prev->next != SSFILEPN; prev = prev->next)
	if (prev->next == ssf)
	  {
	    prev->next = ssf->next;
	    break;
	  }
    }
  closessjoblist(ssf->jobs);
  free(ssf);
  return(SSFILEPN);
}

/******************************************************************/

static SSJOB *getcurjobs ARGS((DDIC *ddic, char *path, int istag));
static SSJOB *
getcurjobs(ddic, path, istag)
DDIC    *ddic;
char    *path;
int     istag;
/* SELECTs currently scheduled jobs for `path' and returns list, ordered
 * by id.  If `istag', returns istag jobs, otherwise returns non-istag jobs.
 * >>>>> NOTE: assumes mutex already held under Windows and DDIC_LOCK() <<<<<
 */
{
	static CONST char	fn[] = "getcurjobs";
	static CONST char SQLQuery[] =
		"SELECT id, SUFFIX, SCHEDULE, LASTRUN FROM SYSSCHEDULE WHERE pathcmp(PATH, ?) = 0 AND OPTIONS %s LIKE 'istag' ORDER BY id;";
	int och = 0;		/* Holder for counthits flag */
	FLD *idf = NULL, *schedf = NULL,
	  *lastf = NULL, *sufff = NULL;
	long sz;
	int pop = 0;
	char *sched, *suff, *tbname, fname[10], *d;
	DBTBL *rtable;
	SSJOB	*list = SSJOBPN, *cur, *liste = SSJOBPN;
	int	saveAllinear = 0, saveAlpostproc = 0, restoreSettings = 0;
	byte    tmp[256];

	if (TXddicstmt(ddic) == -1) goto err;

	tbname = ddgettable(ddic, TEXISSYSSCHED, fname, 0);
        if (tbname == CHARPN) goto done;        /* no table: nothing to do */
	else free(tbname);

	TXpushid(ddic, 0, 0);		/* su to _SYSTEM */
	och = ddic->ch;
	ddic->ch = 0;		     	/* do not count license hits */
	pop = 1;
	tmp[sizeof(tmp)-1] = '\0';
	sprintf((char *)tmp, SQLQuery, (istag ? "" : "NOT"));
	if (tmp[sizeof(tmp)-1] != '\0')
	{
		putmsg(MERR + MAE, fn, "Statement too large");
		goto err;
	}

	/* Version 6+ Texis has query protect on by default: */
	if (TXget_globalcp() != APICPPN)
	{
		saveAllinear = globalcp->allinear;
		globalcp->allinear = 1;
		saveAlpostproc = globalcp->alpostproc;
		globalcp->alpostproc = 1;
		restoreSettings = 1;
	}

	if (SQLPrepare(ddic->ihstmt, tmp, SQL_NTS) != SQL_SUCCESS)
	{
		putmsg(MERR, fn, "Cannot prepare SQL SELECT");
		goto err;
	}

	sz = (long)strlen(path);
	if (SQLSetParam(ddic->ihstmt, 1, SQL_C_CHAR, SQL_CHAR, sz, (SWORD)sz,
			path, &sz) != SQL_SUCCESS)
	  {
	    putmsg(MERR, fn, "Cannot set SQL param 1");
	    goto err;
	  }

	if (SQLExecute(ddic->ihstmt) != SQL_SUCCESS)
	  {
	    putmsg(MERR, fn, "Cannot exec SQL SELECT");
	    goto err;
	  }

	while (SQLFetch(ddic->ihstmt) == SQL_SUCCESS)
	{
		rtable = ((LPSTMT) ddic->ihstmt)->outtbl;
		if (!idf)
			idf = dbnametofld(rtable, "id");
		if (!schedf)
			schedf = dbnametofld(rtable, "SCHEDULE");
		if (!lastf)
			lastf = dbnametofld(rtable, "LASTRUN");
		if (!sufff)
			sufff = dbnametofld(rtable, "SUFFIX");
		if (!idf || !schedf || !lastf || !sufff) goto err;
		sched = getfld(schedf, NULL);
		suff = getfld(sufff, NULL);
		if ((cur = (SSJOB *)calloc(1, (sizeof(SSJOB) -
		      sizeof(cur->suffix)) + strlen(sched) +
                      strlen(suff) + 2)) == SSJOBPN)
		  goto err;
		d = cur->suffix;
		strcpy(d, suff);
		d += strlen(d) + 1;
		strcpy(cur->sched = d, sched);
		cur->id = *(ft_counter *)getfld(idf, NULL);
		cur->lastrun = *(ft_date *)getfld(lastf, NULL);
		if (liste == SSJOBPN) list = cur;
		else liste->next = cur;
		liste = cur;
	}
	goto done;

err:
	list = closessjoblist(list);
done:
	if (restoreSettings)
	{
		globalcp->allinear = saveAllinear;
		globalcp->alpostproc = saveAlpostproc;
	}
	if (pop)
	{
		ddic->ch = och;
		TXpopid(ddic);
	}
	return(list);
}

/*****************************************************************************/

char *
TXaddscheduledevent(DDIC *ddic, TXMUTEX *ddicMutex, char *path, char *suffix,
	char *start, char *schedule, char *vars, char *comments,
	char *options, int pid, char *errbuf, size_t errbufsz, int verbose)
/* Adds a job to the schedule table.  Called by schedule server in
 * Texis monitor (proc_schedule()) when a Vortex compile encounters a
 * <SCHEDULE> tag.  Returns NULL if ok, else error message (const or
 * written to `errbuf').
 * See [Scheduler] Verbose for `verbose' bit flags.
 */
{
#ifdef _WIN32
	static CONST char fn[] = "TXaddscheduledevent";
#endif /* _WIN32 */
	CONST char *emsg;
#define STALETIME	60
	long sz;
	int	pop = 0, och = 0, haveDdicMutex = 0;
	SCHED	optcode;
#ifdef _WIN32
	int	mutex = 0;
	CONST char	*msg;
#endif /* _WIN32 */
	time_t now, starttim, st, nw;
	ft_date	next;
	char *d, *e, *tbname, *ret, fname[10];
	TX_EVENT *event;
	SSFILE	*ssf, *ssfn;
	SSJOB	*ssj, *ssjn, *curjobs = SSJOBPN, *ssjc;
        char trans[512];
        char    counterBuf[TX_COUNTER_HEX_BUFSZ + 128];
        ft_counter      *idVal;
        FLD             *idFld;
        DBTBL           *resultTbl;

#ifdef _WIN32
	/* Make sure we're single-threaded: */
	if (ObjMutex == TXMUTEXPN && !startwatchjobs())
          {
            ret = "startwatchjobs failed";
            goto done;
          }
	switch (TXmutexLock(ObjMutex, TXschedJobMutexTimeoutSec))
	{
	case 1:					/* we have the ball */
		break;
	case 0:					/* timeout */
		AFTER_OBJ_MUTEX_TIMEOUT_MSG(fn, ddicMutex);
		msg = Timeout;
		goto retErr;
	case -1:				/* error */
	default:
		msg = TXstrerror(TXgeterror());
	retErr:
		htsnpf(errbuf, errbufsz, "Cannot lock ObjMutex: %s", msg);
		ret = errbuf;
		goto done;
	}
	mutex = 1;
#endif /* _WIN32 */

	DDIC_LOCK(ddicMutex, {ret = "Cannot lock DDIC mutex"; goto done;});
	haveDdicMutex = 1;
	if (TXddicstmt(ddic) == -1)
	  {
	    ret = "Internal SQL error";
	    goto done;
	  }
	tbname = ddgettable(ddic, TEXISSYSSCHED, fname, 0);
	if (!tbname)
	{
		if (TXcreatesysschedule(ddic) == -1)
		{
		  ret = "Cannot create SYSSCHEDULE table";
		  goto done;
		}
	}
	else
	{
		free(tbname);
	}
	if(!path)
	  {
	    ret = "No path";
	    goto done;
	  }
	if(!schedule)
	  {
	    ret = "No schedule";
	    goto done;
	  }
	if (!options)
	  {
	    ret = "No options";
	    goto done;
	  }
	optcode = cookoptions(options);
	now = time(NULL);
	starttim = 0;
	if (start && *start)
	  {
            if ((starttim = TXindparsetime(start, -1, 0, TXPMBUFPN)) ==
                (time_t)(-1))
	      {
		ret = "Bad start time";
		goto done;
	      }
	  }

	/* Verify schedule syntax now, so errors occur on the correct tag: */
	if (!(optcode & SCHED_OFF))
	  {
	    st = starttim;
	    if (!st) st = now;
	    st = (st/60)*60;
	    nw = (now/60)*60;				/* integral minute */
	    event = TXvcal(schedule, st, 0, 0, 1, nw, TIME_T_MAX, 0);
	    emsg = CHARPN;
	    if (event == TX_EVENTPN &&
		(emsg =tx_english2vcal(trans,sizeof(trans),schedule))==CHARPN)
	      {
		event = TXvcal(trans, st, 0, 0, 1, nw, TIME_T_MAX, 0);
		if (event == TX_EVENTPN) emsg = (CONST char *)2;
	      }
	    if (event) TXcloseevent(event);
	    else
	      {
		d = errbuf;
		e = d + errbufsz;
		e += htsnpf(d, e-d, "Invalid schedule syntax `%s'", schedule);
		if (emsg != CHARPN && emsg != (CONST char *)1)
		  {
		    if (d < e) e += htsnpf(d, e - d, ": ");
		    if (d < e)
		      {
			if (emsg == (CONST char *)2) /* shouldn't happen... */
			  e += htsnpf(d, e-d, "Bad translation `%s'", trans);
			else
			  e += htsnpf(d, e-d, "%s", emsg);
		      }
		  }
		ret = errbuf;
		goto done;
	      }
	  }

	/* Find or create the pending schedule object for this script: */
again:
	for (ssf = Files;
	     ssf != SSFILEPN && strcmp(ssf->script, path) != 0;
	     ssf = ssfn)
	  {					/* remove stale entries */
	    ssfn = ssf->next;
	    if (now - ssf->init >= STALETIME) closessfile(ssf);
	  }
	if (ssf == SSFILEPN)			/* this script not pending */
	  {
	    if (optcode & SCHED_FIRST)		/* first <SCHEDULE> */
	      {
		if (optcode & SCHED_OFF)
		  {
		    /* Note that we drop the suffix, so that all schedules
		     * for this script (or all istag ones) get dropped:
		     */
	 	    /* this call assumes DDIC_LOCK() already obtained: */
		    ret = TXdelscheduledevent(ddic, path, CHARPN, NULL, NULL,
					      NULL, options, verbose);
		    goto done;
		  }
		if ((ssf = (SSFILE *)calloc(1, (sizeof(SSFILE) -
                      sizeof(ssf->script)) + strlen(path) + 1)) == SSFILEPN)
		  {
		    ret = "Out of memory";
		    goto done;
		  }
		ssf->init = now;
		strcpy(ssf->script, path);
		ssf->pid = pid;
		ssf->next = Files;
		Files = ssf;
	      }
	    else				/* not first <SCHEDULE> */
	      {
		ret = "Initial schedule is not FIRST";
		goto done;
	      }
	  }
	else if (ssf->pid != pid)		/* pending script, other pid*/
	  {
	    if (now - ssf->init >= STALETIME)	/* pending script stale */
	      {
		closessfile(ssf);
		goto again;
	      }
	    ret = "Script being scheduled by another process";
	    goto done;
	  }
	else if (optcode & SCHED_FIRST)		/* ok pid but wrong flag */
	  {
	    ret = "Secondary schedule has FIRST flag";
	    goto done;
	  }
	else if (optcode & SCHED_OFF)
	  {
	    ret = "Secondary schedule has OFF flag";
	    goto done;
	  }

	/* Add this job to the file's list, in order: */
	if ((ssjn = (SSJOB*)calloc(1, (sizeof(SSJOB) - sizeof(ssjn->suffix)) +
               strlen(schedule) + strlen(vars) + strlen(comments) +
               strlen(options) + strlen(suffix) + 5)) == SSJOBPN)
	  {
	    ret = "Out of memory";
	    goto done;
	  }
	d = ssjn->suffix;
	strcpy(d, suffix);
	d += strlen(d) + 1;
	strcpy(ssjn->sched = d, schedule);
	d += strlen(d) + 1;
	strcpy(ssjn->vars = d, vars);
	d += strlen(d) + 1;
	strcpy(ssjn->comments = d, comments);
	d += strlen(d) + 1;
	strcpy(ssjn->options = d, options);
	d += strlen(d) + 1;
	ssjn->start = starttim;
	if (ssf->jobs == SSJOBPN) ssf->jobs = ssjn;
	else
	  {
	    for (ssj = ssf->jobs; ssj->next != SSJOBPN; ssj = ssj->next);
	    ssj->next = ssjn;
	  }

	/* If this is not the LAST job for this file, bail until then: */
	if (!(optcode & SCHED_LAST))
	  {
	    ret = CHARPN;
	    goto done;
	  }

	/* We've now got the complete new schedule list for this file.
	 * We waited until the LAST guy so that this update will be
	 * atomic: no waiting for further <SCHEDULE>s, and we're
	 * single-threaded so our multiple SQLs are effectively atomic.
	 * Update/insert the list into the table:
	 */
	curjobs = getcurjobs(ddic, path, (optcode & SCHED_ISTAG));
	TXpushid(ddic, 0, 0);		/* su to _SYSTEM */
	och = ddic->ch;
	ddic->ch = 0;                   /* don't count license hits */
	pop = 1;
	for (ssj = ssf->jobs; ssj != SSJOBPN; ssj = ssj->next)
	  {
	    /* If this matches an existing schedule, update it, and
	     * possibly use its LASTRUN as a start time.  This avoids
	     * running a job that has no START time every time it's compiled:
	     */
	    for (ssjc = curjobs; ssjc != SSJOBPN; ssjc = ssjc->next)
	      {
		if (strcmp(ssjc->suffix, ssj->suffix) == 0 &&
		    strcmp(ssjc->sched, ssj->sched) == 0)
		  {
		    if (!ssj->start && ssjc->lastrun)
		      ssj->start = ssjc->lastrun;
		    break;
		  }
	      }
	    if (!ssj->start) ssj->start = ssf->init;	/* consistent starts*/
	    optcode = cookoptions(ssj->options);
	    starttim = (ssj->start/60)*60;
	    now = ssf->init;
	    event = TXvcal(ssj->sched, starttim, 0, 0, 1, now, TIME_T_MAX, 0);
	    if (event == TX_EVENTPN &&
		tx_english2vcal(trans, sizeof(trans), ssj->sched) == CHARPN)
	      event = TXvcal(trans, starttim, 0, 0, 1, now, TIME_T_MAX, 0);
	    if (event)
	      {
		next = (ft_date)event->when;
		TXcloseevent(event);
	      }
	    else					/* should not happen*/
	      {
		ret = (ssj->next == SSJOBPN ? "Invalid schedule syntax" :
		       "Invalid previous schedule syntax");
		goto done;
	      }
	    if (ssjc != SSJOBPN)		/* update */
	      {
		if (SQLPrepare(ddic->ihstmt, (byte *)
			       "UPDATE SYSSCHEDULE SET NEXTRUN = ?, VARS = ?, COMMENTS = ?, OPTIONS = ? WHERE id = ?;", SQL_NTS) != SQL_SUCCESS)
		  {
		    ret = "Cannot prepare SYSSCHEDULE table for update";
		    goto done;
		  }
		sz = (long)sizeof(next);
		if (SQLSetParam(ddic->ihstmt, 1, SQL_C_CDATE, SQL_CDATE, sz,
				(SWORD)sz, &next, &sz) != SQL_SUCCESS)
		  {
		    ret = "Cannot set SQL param 1";
		    goto done;
		  }
		if (ssj->vars)
		  {
                    sz = (long)strlen(ssj->vars);
		    if (SQLSetParam(ddic->ihstmt, 2, SQL_C_CHAR, SQL_CHAR, sz,
				    (SWORD)sz, ssj->vars, &sz) != SQL_SUCCESS)
		      {
			ret = "Cannot set SQL param 2";
			goto done;
		      }
		  }
		if (ssj->comments)
		  {
		    sz = (long)strlen(ssj->comments);
		    if (SQLSetParam(ddic->ihstmt, 3, SQL_C_CHAR, SQL_CHAR, sz,
				(SWORD)sz, ssj->comments, &sz) != SQL_SUCCESS)
		      {
			ret = "Cannot set SQL param 3";
			goto done;
		      }
		  }
		if (ssj->options)
		  {
		    sz = (long)strlen(ssj->options);
		    if (SQLSetParam(ddic->ihstmt, 4, SQL_C_CHAR, SQL_CHAR, sz,
				 (SWORD)sz, ssj->options, &sz) != SQL_SUCCESS)
		      {
			ret = "Cannot set SQL param 4";
			goto done;
		      }
		  }
		sz = (long)sizeof(ssjc->id);
		if (SQLSetParam(ddic->ihstmt, 5, SQL_C_COUNTER, SQL_COUNTER,
				sz, (SWORD)sz, &ssjc->id, &sz) != SQL_SUCCESS)
		  {
		    ret = "Cannot set SQL param 5";
		    goto done;
		  }
		if (SQLExecute(ddic->ihstmt) != SQL_SUCCESS)
		  {
		    ret = "Cannot update SYSSCHEDULE table";
		    goto done;
		  }
		if (SQLFetch(ddic->ihstmt) == SQL_SUCCESS)
		  {
                    if (verbose & 0x4)
                      {
                        TXprintHexCounter(counterBuf, sizeof(counterBuf),
                                          &ssjc->id);
                        putmsg(MINFO, CHARPN,
                         "Job %s %s%s %s re-scheduled for %s (next run: %at)",
                               counterBuf, path, ssjc->suffix, ssj->vars,
                               ssjc->sched, DateFmt, (time_t)next);
                      }
		    memset(&ssjc->id, 0, sizeof(ssjc->id)); /* don't delete */
		    *ssjc->suffix = '\0';		    /* don't re-use */
		    *ssjc->sched = '\0';
		    continue;
		  }
	      }
	    /* No current job matches.  Insert a new one: */
	    if (SQLPrepare(ddic->ihstmt, (byte *)
	     "INSERT INTO SYSSCHEDULE (id, PATH, SUFFIX, SCHEDULE, FIRSTRUN, LASTRUN, LASTEXIT, LASTEXITCODE, PID, NEXTRUN, EXECUTIONS, VARS, COMMENTS, STATUS, OPTIONS) values (counter, ?, ?, ?, 0, 0, 0, 0, 0, ?, 0, ?, ?, 0, ?);",
			   SQL_NTS) != SQL_SUCCESS)
	      {
		ret = "Cannot prepare SYSSCHEDULE table for insertion";
		goto done;
	      }
	    sz = (long)strlen(path);
	    if (SQLSetParam(ddic->ihstmt, 1, SQL_C_CHAR, SQL_CHAR, sz,
                            (SWORD)sz, path, &sz) != SQL_SUCCESS)
	      {
		ret = "Cannot set SQL insert param 1";
		goto done;
	      }
		sz = (long)strlen(ssj->suffix);
		if (SQLSetParam(ddic->ihstmt, 2, SQL_C_CHAR, SQL_CHAR, sz,
                                (SWORD)sz, ssj->suffix, &sz) != SQL_SUCCESS)
		  {
		    ret = "Cannot set SQL insert param 2";
		    goto done;
		  }
	    sz = (long)strlen(ssj->sched);
	    if (SQLSetParam(ddic->ihstmt, 3, SQL_C_CHAR, SQL_CHAR, sz,
                            (SWORD)sz, ssj->sched, &sz) != SQL_SUCCESS)
	      {
		ret = "Cannot set SQL insert param 3";
		goto done;
	      }
	    sz = (long)sizeof(next);
	    if (SQLSetParam(ddic->ihstmt, 4, SQL_C_CDATE, SQL_CDATE, sz,
                            (SWORD)sz, &next, &sz) != SQL_SUCCESS)
	      {
		ret = "Cannot set SQL insert param 4";
		goto done;
	      }
	    if (ssj->vars)
	      {
		sz = (long)strlen(ssj->vars);
		if (SQLSetParam(ddic->ihstmt, 5, SQL_C_CHAR, SQL_CHAR, sz,
                                (SWORD)sz, ssj->vars, &sz) != SQL_SUCCESS)
		  {
		    ret = "Cannot set SQL insert param 5";
		    goto done;
		  }
	      }
	    if (ssj->comments)
	      {
		sz = (long)strlen(ssj->comments);
		if (SQLSetParam(ddic->ihstmt, 6, SQL_C_CHAR, SQL_CHAR, sz,
                                (SWORD)sz, ssj->comments, &sz) != SQL_SUCCESS)
		  {
		    ret = "Cannot set SQL insert param 6";
		    goto done;
		  }
	      }
	    if (ssj->options)
	      {
		sz = (long)strlen(ssj->options);
		if (SQLSetParam(ddic->ihstmt, 7, SQL_C_CHAR, SQL_CHAR, sz,
                                (SWORD)sz, ssj->options, &sz) != SQL_SUCCESS)
		  {
		    ret = "Cannot set SQL insert param 7";
		    goto done;
		  }
	      }
	    if (SQLExecute(ddic->ihstmt) != SQL_SUCCESS)
	      {
		ret = "Cannot insert into SYSSCHEDULE table";
		goto done;
	      }
	    if (SQLFetch(ddic->ihstmt) != SQL_SUCCESS)
	      {
		ret = "Cannot complete insert into SYSSCHEDULE table";
		goto done;
	      }
            if (verbose & 0x4)
              {
                /* Get the `id' value from the INSERT: */
		resultTbl = ((LPSTMT)ddic->ihstmt)->outtbl;
                if (resultTbl != DBTBLPN &&
                    (idFld = dbnametofld(resultTbl, "id")) != FLDPN &&
                    (idFld->type & DDTYPEBITS) == FTN_COUNTER &&
                    (idVal = (ft_counter *)getfld(idFld, NULL)) != NULL)
                  TXprintHexCounter(counterBuf, sizeof(counterBuf), idVal);
                else
                  strcpy(counterBuf, "?");
                putmsg(MINFO, CHARPN,
                       "Job %s %s%s %s scheduled for %s (next run: %at)",
                       counterBuf, path, ssj->suffix, ssj->vars,
                       ssj->sched, DateFmt, (time_t)next);
              }
	  }

	/* Now delete any old schedules that weren't updated: */
	for (ssjc = curjobs; ssjc != SSJOBPN; ssjc = ssjc->next)
	  {
	    if (ssjc->id.date == 0 && ssjc->id.seq == 0) continue;
	    if (SQLPrepare (ddic->ihstmt, (byte *)
	     "DELETE FROM SYSSCHEDULE WHERE id = ?;", SQL_NTS) != SQL_SUCCESS)
	      {
		ret = "Cannot prepare SYSSCHEDULE for deletion";
		goto done;
	      }
	    sz = sizeof(ssjc->id);
	    if (SQLSetParam(ddic->ihstmt, 1, SQL_C_COUNTER, SQL_COUNTER, sz,
                            (SWORD)sz, &ssjc->id, &sz) != SQL_SUCCESS)
	      {
		ret = "Cannot set SQL delete param 1";
		goto done;
	      }
	    if (SQLExecute(ddic->ihstmt) != SQL_SUCCESS)
	      {
		ret = "Cannot delete old schedule from SYSSCHEDULE table";
		goto done;
	      }
	    SQLFetch(ddic->ihstmt);		/* one row */
            if (verbose & 0x8)
              {
                TXprintHexCounter(counterBuf, sizeof(counterBuf), &ssjc->id);
                putmsg(MINFO, CHARPN,
                       "Job %s %s%s %s deleted",
                       counterBuf, path,
                       (ssjc->suffix != CHARPN ? ssjc->suffix : ""),
                       (ssjc->vars != CHARPN ? ssjc->vars : ""));
              }
	  }

	closessfile(ssf);			/* flush the job list */
	ret = CHARPN;
done:
	if (pop)
	{
		ddic->ch = och;
		TXpopid(ddic);
	}
	/* Release `ddicMutex' before `ObjMutex', since it was obtained
	 * "inside" it (avoid possible deadlock), and before closesql()
	 * re-locks it (no recursive mutexes):
	 */
	if (haveDdicMutex) DDIC_UNLOCK(ddicMutex);
#ifdef _WIN32
	if (mutex) TXmutexUnlock(ObjMutex);
#endif /* _WIN32 */
	closessjoblist(curjobs);
	closesql(ddic, ddicMutex);
	return(ret);
}

/******************************************************************/

static int runjob ARGS((char *texis, char *path, char *vars, ft_counter id,
                        int istag, ft_long *pid, TXMUTEX *ddicMutex,
                        int lockFlags));
static int
runjob(texis, path, vars, id, istag, pid, ddicMutex, lockFlags)
char            *texis;		/* optional path to texis executable */
char		*path, *vars;
ft_counter      id;
int             istag;
ft_long         *pid;
TXMUTEX		*ddicMutex;
int		lockFlags;	/* (in) 0x1: have ObjMutex lock already */
/* Starts job and adds to process list for reaper.  Returns -1 on
 * error, 0 if ok.
 */
{
	static CONST char	fn[] = "runjob";

#ifdef _WIN32
	PROCESS_INFORMATION piProcInfo;
	SECURITY_ATTRIBUTES saAttr;
	STARTUPINFO siStartInfo;
	size_t cmdlinssz, i;
	char *cmdlin, *x;

	*pid = 0;
	cmdlinssz = strlen(vars) + 2;
	cmdlinssz += strlen(path) + 3;
	if (texis == CHARPN)
	{
		cmdlinssz += 1 + strlen(TXINSTALLPATH_VAL);  /* quote too */
		cmdlinssz += 12;        /* `\texis.exe" ' */
	}
	else
		cmdlinssz += strlen(texis) + 3;  /* 2 quotes too */
	cmdlinssz += 18;		/* `-r -sched istag|iscmdln ' */
	cmdlin = malloc(cmdlinssz);
	if (!cmdlin)
	  {
	    TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn, cmdlinssz, 1);
	    return -1;
	  }
	if (texis == CHARPN)
	{
		strcpy(cmdlin, "\"");
		strcat(cmdlin, TXINSTALLPATH_VAL);
		strcat(cmdlin, PATH_SEP_S "texis.exe\" ");
	}
	else
	{
		strcpy(cmdlin, texis);
		strcat(cmdlin, " ");
	}
	strcat(cmdlin, "-r -sched ");
	strcat(cmdlin, (istag ? "istag " : "iscmdln "));
	i = strlen(cmdlin);
	for (x = vars; *x != '\0'; x++, i++)
	{
		if (*x == '&')
		{
			cmdlin[i] = ' ';
		}
		else
		{
			cmdlin[i] = *x;
		}
	}
	cmdlin[i] = '\0';
	strcat(cmdlin, " \"");
	strcat(cmdlin, path);
	strcat(cmdlin, "\"");

	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	/* Set up members of STARTUPINFO structure. */

	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.lpReserved = NULL;
	siStartInfo.lpReserved2 = NULL;
	siStartInfo.cbReserved2 = 0;
	siStartInfo.lpDesktop = NULL;
	siStartInfo.dwFlags = STARTF_USESHOWWINDOW;
	siStartInfo.wShowWindow = SW_HIDE;

	/* Create the child process. */

	if (!CreateProcess(NULL, cmdlin,	/* command line   */
			   NULL,    /* process security attributes    */
			   NULL,    /* primary thread security attributes */
			   FALSE,   /* handles are inherited              */
			   0,	    /* creation flags                     */
			   NULL,    /* use parent's environment           */
			   NULL,    /* use parent's current directory     */
			   &siStartInfo, /* STARTUPINFO pointer           */
			   &piProcInfo)) /* receives PROCESS_INFORMATION  */
	  {
            LPVOID      m;

            FormatMessage((FORMAT_MESSAGE_ALLOCATE_BUFFER |
                           FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS),
                          NULL, GetLastError(),
                          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                          (LPTSTR)&m, 0, NULL);
            putmsg(MERR + EXE, CHARPN, "Cannot exec scheduled job `%s': %s",
                   cmdlin, (char *)m);
            LocalFree(m);
	    free(cmdlin);
	    return -1;
	  }

	CloseHandle(piProcInfo.hThread);
	*pid = piProcInfo.dwProcessId;
	if (txsched_procadd(*pid, piProcInfo.hProcess, id, cmdlin,
			    ddicMutex, lockFlags) < 0)
	  CloseHandle(piProcInfo.hProcess);
	cmdlin = TXfree(cmdlin);
	return(0);

#else /* !_WIN32  - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	static CONST char	whitespace[] = " \t\r\n\v\f";
	static CONST char	devnull[] = "/dev/null";
	int argc = 0, i, ret;
	TXFHANDLE		pfh[2*TX_NUM_STDIO_HANDLES];
        char *args = CHARPN, *x, **argv = CHARPPN, *cmd = CHARPN;
	char	*cmdLine = NULL;
	char tmp[PATH_MAX];
#  define RD    0
#  define WR    1

	for (i = 0; i < 2*TX_NUM_STDIO_HANDLES; i++)
		pfh[i] = TXFHANDLE_INVALID_VALUE;
	*pid = 0;

	if (texis == CHARPN)			/* default texis */
	{
		TXstrncpy(tmp, TXINSTALLPATH_VAL, sizeof(tmp) - 11);
		strcat(tmp, PATH_SEP_S "bin" PATH_SEP_S "texis");
		texis = tmp;
	}
	else
	{
		for (x = texis; *x != '\0'; x++)
			if (strchr(whitespace, *x) != CHARPN) argc++;
	}

	args = TXstrdup(TXPMBUFPN, __FUNCTION__, vars);
	if (!args) goto err;
	if (*args != '\0') argc++;
	for (x = args; *x != '\0'; x++)
	{
		if (*x == '&' || *x == ';')
		{
			argc++;
		}
	}
	argv = (char **) TXcalloc(TXPMBUFPN, __FUNCTION__, argc + 6,
				  sizeof(char *));
	if (!argv) goto err;

	i = 0;
	if (texis == tmp)
	  argv[i++] = texis;
	else				/* split on spaces */
	  {
	    if (strlen(texis) >= sizeof(tmp))
	      {
		putmsg(MWARN + MAE, fn, "Executable and args too large");
		argv[i++] = texis;
	      }
	    else
	      {
		TXstrncpy(tmp, texis, sizeof(tmp));
		argv[i] = strtok(tmp, whitespace);
		while (CHARPN != argv[i])
		  {
		    argv[++i] = strtok(CHARPN, whitespace);
		  }
	      }
	  }
	argv[i++] = "-r";
	argv[i++] = "-sched";
	argv[i++] = (istag ? "istag" : "iscmdln");
	argv[i] = strtok(args, "&;");
	while (CHARPN != argv[i])
	{
		argv[++i] = strtok(NULL, "&;");
	}
	argv[i++] = path;
	argv[i] = CHARPN;

	cmdLine = tx_c2dosargv(argv, TXbool_True);

	if ((cmd = epipathfindmode(argv[0], getenv("PATH"), X_OK)) == CHARPN)
	{
	  putmsg(MERR + EXE, CHARPN, "Cannot exec scheduled job `%s': %s",
		 cmdLine, strerror(errno));
	  goto err;
	}

	/* This code from vortex/vio.c:vx_popenduplex(). */
	switch (*pid = TXfork(TXPMBUFPN, "Vortex <schedule> script", NULL,0x7))
	{
	case -1:
		goto err;
	case 0:			/* child */
		/* set up stdin with appropriate pipe: */
		pfh[2*STDIN_FILENO + RD] = open(devnull, O_RDONLY, 0666);
		if (!TXFHANDLE_IS_VALID(pfh[2*STDIN_FILENO + RD]))
		{
			putmsg(MERR + FOE, fn, "open(`%s') for read failed: %s",
			       devnull, strerror(errno));
			goto childbail;
		}
		if (pfh[2*STDIN_FILENO + RD] == STDIN_FILENO)
			pfh[2*STDIN_FILENO + RD] = TXFHANDLE_INVALID_VALUE;
		else
		{
			if (dup2(pfh[2*STDIN_FILENO + RD], STDIN_FILENO) < 0)
				goto dupfail;
		}
		/* set up stdout with appropriate pipe: */
		pfh[2*STDOUT_FILENO + WR] = open(devnull, O_WRONLY, 0666);
		if (!TXFHANDLE_IS_VALID(pfh[2*STDOUT_FILENO + WR]))
		{
			putmsg(MERR + FOE, fn, "open(`%s') for write failed: %s",
			       devnull, strerror(errno));
			goto childbail;
		}
		if (pfh[2*STDOUT_FILENO + WR] == STDOUT_FILENO)
			pfh[2*STDOUT_FILENO + WR] = TXFHANDLE_INVALID_VALUE;
		else
		{
			if (dup2(pfh[2*STDOUT_FILENO + WR], STDOUT_FILENO) < 0)
			{
			dupfail:
				putmsg(MERR + FOE, fn, "dup2() failed: %s",
				       strerror(errno));
				goto childbail;
			}
		}
		/* set up stderr with appropriate pipe: */
		pfh[2*STDERR_FILENO + WR] = open(devnull, O_WRONLY, 0666);
		if (!TXFHANDLE_IS_VALID(pfh[2*STDERR_FILENO + WR]))
		{
			putmsg(MERR + FOE, fn, "open(`%s') for write failed: %s",
			       devnull, strerror(errno));
			goto childbail;
		}
		if (pfh[2*STDERR_FILENO + WR] == STDERR_FILENO)
			pfh[2*STDERR_FILENO + WR] = TXFHANDLE_INVALID_VALUE;
		else
		{
			if (dup2(pfh[2*STDERR_FILENO + WR], STDERR_FILENO) < 0)
				goto dupfail;
		}

		/* close unneeded pipe ends first: */
		for (i = 0; i < 2*TX_NUM_STDIO_HANDLES; i++)
			if (TXFHANDLE_IS_VALID(pfh[i]))
			{
				(void)TXfhandleClose(pfh[i]);
				pfh[i] = TXFHANDLE_INVALID_VALUE;
			}
		TXclosedescriptors(0x4); /* see TXfork() args too */
		/* Now run program: */
		execv(cmd, argv);
		/* if we get here, the exec() failed: */
		putmsg(MERR + EXE, fn, "Cannot exec `%s': %s", cmdLine,
		       strerror(errno));
	childbail:
		_exit(TXEXIT_CANNOTEXECSUBPROCESS);       /* _exit avoids stdio flush, vfork problems */
	}
	/* Parent */
	ret = 0;
	goto done;

err:
	*pid = 0;
	ret = -1;
done:
	for (i = 0; i < 2*TX_NUM_STDIO_HANDLES; i++)
		if (TXFHANDLE_IS_VALID(pfh[i]))
		{
			(void)TXfhandleClose(pfh[i]);
			pfh[i] = TXFHANDLE_INVALID_VALUE;
		}
	if (*pid)
		txsched_procadd(*pid, NULL, id, cmdLine,
				ddicMutex, lockFlags);
	args = TXfree(args);
	argv = TXfree(argv);
	cmd = TXfree(cmd);
	cmdLine = TXfree(cmdLine);
        return(ret);
#  undef RD
#  undef WR
#endif /* !_WIN32 */
}

/******************************************************************/

int
TXrunscheduledevents(DDIC *ddic, TXMUTEX *ddicMutex, char *texis, int verb)
/* Called once a minute by Texis monitor to start Vortex jobs scheduled
 * for this minute (or earlier and haven't run yet).
 * `verb' bit flags: see [Scheduler] Verbose in monitor.c
 */
{
	static CONST char fn[] = "TXrunscheduledevents";
	byte SQLQuery[] =

		"SELECT id, PATH, SUFFIX, SCHEDULE, FIRSTRUN, LASTRUN, NEXTRUN, VARS, OPTIONS, PID FROM SYSSCHEDULE WHERE NEXTRUN <= 'now' AND STATUS = 0 ORDER BY id;";
	int och = 0;		/* Holder for counthits flag */
	int	lockFlags = 0;
	FLD *idf = NULL, *pathf = NULL, *schedf = NULL, *nextf = NULL,
		*firstf = NULL, *lastf = NULL, *varsf = NULL, *optf = NULL,
		*sufff = NULL, *pidResultFld = NULL;
	ft_date first, last, next, now;
	ft_counter id;
	long sz;
	ft_long execed, pid;
	int cookopt, pop = 0, ret, haveDdicMutex = 0;
	char *path, *sched, *vars, *opt, *suff, *tbname, fname[10];
	DBTBL *rtable;
	TX_EVENT *nextevent, *event, *ev;
        char trans[512];
	int rc;
        char    counterBuf[TX_COUNTER_HEX_BUFSZ + 128];
        char    dateBuf[128];
#ifdef _WIN32
	if (ObjMutex == TXMUTEXPN && !startwatchjobs()) return(-1);
	switch (TXmutexLock(ObjMutex, TXschedJobMutexTimeoutSec))
	  {
	  case 1:                               /* we have the ball */
	    lockFlags |= 0x1;
            break;
          case 0:                               /* timeout */
            AFTER_OBJ_MUTEX_TIMEOUT_MSG(fn, ddicMutex);
            return(-1);
          case -1:                              /* error */
	  default:
	    return(-1);
	  }
#endif /* _WIN32 */

	DDIC_LOCK(ddicMutex, goto errout);
	haveDdicMutex = 1;
	if (TXddicstmt(ddic) == -1) goto errout;

	tbname = ddgettable(ddic, TEXISSYSSCHED, fname, 0);
        if (tbname == CHARPN)                   /* no table: nothing to do */
          goto ok;
	else
	  free(tbname);

	now = time(NULL);
	TXpushid(ddic, 0, 0);
	och = ddic->ch;
	ddic->ch = 0;				/* do not count license hits */
	pop = 1;
	if (SQLPrepare(ddic->ihstmt, SQLQuery, SQL_NTS) != SQL_SUCCESS)
	{
		putmsg(MERR, fn, "Cannot prepare SQL SELECT");
		goto errout;
	}
	while (SQLFetch(ddic->ihstmt) == SQL_SUCCESS)
	{
		void	*val;
		size_t	valSz;

		rtable = ((LPSTMT) ddic->ihstmt)->outtbl;
		/* Run Job */
		if (!idf)
			idf = dbnametofld(rtable, "id");
		if (!pathf)
			pathf = dbnametofld(rtable, "PATH");
		if (!schedf)
			schedf = dbnametofld(rtable, "SCHEDULE");
		if (!firstf)
			firstf = dbnametofld(rtable, "FIRSTRUN");
		if (!lastf)
			lastf = dbnametofld(rtable, "LASTRUN");
		if (!nextf)
			nextf = dbnametofld(rtable, "NEXTRUN");
		if (!varsf)
			varsf = dbnametofld(rtable, "VARS");
		if (!optf)
			optf = dbnametofld(rtable, "OPTIONS");
		if (!sufff)
			sufff = dbnametofld(rtable, "SUFFIX");
		if (!pidResultFld)
			pidResultFld = dbnametofld(rtable, "PID");
		if (!idf || !pathf || !schedf || !firstf || !lastf || !nextf||
		    !varsf || !optf || !sufff || !pidResultFld)
			goto errout;
		id = *(ft_counter *) getfld(idf, NULL);
		path = getfld(pathf, NULL);
		sched = getfld(schedf, NULL);
		first = *(ft_date *) getfld(firstf, NULL);
		last = *(ft_date *) getfld(lastf, NULL);
		next = *(ft_date *) getfld(nextf, NULL);
		vars = getfld(varsf, NULL);
		opt = getfld(optf, NULL);
		cookopt = cookoptions(opt);
		suff = getfld(sufff, NULL);
		val = getfld(pidResultFld, &valSz);
		if (val && valSz > (size_t)0)
			pid = *(ft_long *)val;
		else
			pid = 0;
		execed = 0;

		/* Find out when this job is supposed to run, and its
                 * next run, according to its schedule.  `next' may be
		 * old if the monitor died:
		 */
		event = TXvcal(sched, now/60*60, 0, 0, 3, 0, TIME_T_MAX, 0);
                if (event == TX_EVENTPN &&
                    tx_english2vcal(trans, sizeof(trans), sched) == CHARPN)
                  event = TXvcal(trans, now/60*60, 0, 0, 3, 0, TIME_T_MAX, 0);
		/* Get the latest event <= now: */
		for (ev = event; ev != NULL; ev = ev->next)
		  if (ev->next != NULL && ev->next->when > now) break;
		nextevent = NULL;
		if (ev != NULL)
		  {
		    next = ev->when;
		    nextevent = ev->next;
		  }
		if (now - next < 60)	/* Or option to run asap */
		{
			char *torun;
			int freetorun = 0;

			switch (*suff)
			{
			case '\0':
				torun = path;
				break;
			case '/':
				torun = TXstrcat2(path, suff);
				freetorun = 1;
				break;
			default:
				torun = TXstrcat3(path, "/", suff);
				freetorun = 1;
				break;
			}
			/* Bug 5439: mutex schedule runs to help prevent
			 * pileup, in case exec'd script fails to check:
			 */
			if ((cookopt & SCHED_MUTEX) &&
			    pid &&
			    TXprocessexists(pid))
			{
				TXprintHexCounter(counterBuf,
						  sizeof(counterBuf), &id);
				putmsg(MWARN, CHARPN,
				       "Job %s %s %s not run: mutex option given and PID %u still exists (started %at)",
				       counterBuf, torun, vars, (unsigned)pid,
				       DateFmt, (time_t)last);
			}
			else
			{
				rc = runjob(texis, torun, vars, id,
                                    (cookopt & SCHED_ISTAG), &pid, ddicMutex,
					    lockFlags);
				if (verb & 0x1)
				{
					if (nextevent)
						htsnpf(dateBuf,
						       sizeof(dateBuf), "%at",
						    DateFmt, nextevent->when);
					else
						strcpy(dateBuf, "never");
					TXprintHexCounter(counterBuf,
							  sizeof(counterBuf),
							  &id);
					putmsg(MINFO, CHARPN,
				     "Job %s %s %s %s (pid: %u next run: %s)",
					       counterBuf, torun, vars,
                                   (rc == 0 ? "started ok":"failed to start"),
					       (unsigned)pid, dateBuf);
				}
				execed++;
			}
			if (freetorun)
				TXfree(torun);
		}
		if (nextevent)
		{
			HSTMT istmt2;
			int   och2;
			char counterstr[TX_COUNTER_HEX_BUFSZ + 128];

			if (first == (ft_date)0 && execed) first = now;
			if (execed) last = now;
			next = nextevent->when;
			TXpushid(ddic, 0, 0);		/* su to _SYSTEM */
			och2 = ddic->ch;
			ddic->ch = 0;                   /* no license hits */
			if (SQLAllocStmt(ddic->dbc, &istmt2) != SQL_SUCCESS)
			  {
			    putmsg(MERR, fn, "Cannot alloc SQL stmt");
			    goto cont;
			  }
			if (SQLPrepare(istmt2, (byte *)
				       "UPDATE SYSSCHEDULE SET FIRSTRUN = ?, LASTRUN = ?, PID = ?, NEXTRUN = ?, EXECUTIONS = EXECUTIONS + ? WHERE id = ?;",
				       SQL_NTS) != SQL_SUCCESS)
			  {
			    putmsg(MERR, fn, "Cannot prepare SQL UPDATE");
			    goto cont;
			  }
			TXprintHexCounter(counterstr, sizeof(counterstr),
                                          &id);
			sz = sizeof(first);
			if (SQLSetParam(istmt2, 1, SQL_C_CDATE, SQL_CDATE, sz,
                                       (SWORD)sz, &first, &sz) != SQL_SUCCESS)
			  {
			    putmsg(MERR, fn, "Cannot set SQL update param 1");
			    goto cont;
			  }
			sz = sizeof(last);
			if (SQLSetParam(istmt2, 2, SQL_C_CDATE, SQL_CDATE, sz,
                                        (SWORD)sz, &last, &sz) != SQL_SUCCESS)
			  {
			    putmsg(MERR, fn, "Cannot set SQL update param 2");
			    goto cont;
			  }
			sz = sizeof(pid);
			if (SQLSetParam(istmt2, 3, SQL_C_LONG, SQL_INTEGER,
                                     sz, (SWORD)sz, &pid, &sz) != SQL_SUCCESS)
			  {
			    putmsg(MERR, fn, "Cannot set SQL update param 3");
			    goto cont;
			  }
			sz = sizeof(next);
			if (SQLSetParam(istmt2, 4, SQL_C_CDATE, SQL_CDATE,
				    sz, (SWORD)sz, &next, &sz) != SQL_SUCCESS)
			  {
			    putmsg(MERR, fn, "Cannot set SQL update param 4");
			    goto cont;
			  }
			sz = sizeof(execed);
			if (SQLSetParam(istmt2, 5, SQL_C_LONG, SQL_INTEGER,
				  sz, (SWORD)sz, &execed, &sz) != SQL_SUCCESS)
			  {
			    putmsg(MERR, fn, "Cannot set SQL update param 5");
			    goto cont;
			  }
			sz = (long)strlen(counterstr);
			if (SQLSetParam(istmt2, 6, SQL_C_CHAR, SQL_CHAR, sz,
                                   (SWORD)sz, counterstr, &sz) != SQL_SUCCESS)
			  {
			    putmsg(MERR, fn, "Cannot set SQL update param 6");
			    goto cont;
			  }
			if (SQLExecute(istmt2) != SQL_SUCCESS)
			  {
			    putmsg(MERR, fn, "Cannot update SYSSCHEDULE");
			    goto cont;
			  }
			SQLFetch(istmt2);
			SQLFreeStmt(istmt2, SQL_DROP);
		cont:
			ddic->ch = och2;
			TXpopid(ddic);			/* restore perms */
		}
		TXcloseevent(event);
	}
ok:
	ret = 0;
	goto done;

errout:
	ret = -1;
done:
	if (pop)
	{
		ddic->ch = och;
		TXpopid(ddic);
	}
	/* release `ddicMutex' before closesql()/ObjMutex: */
	if (haveDdicMutex) DDIC_UNLOCK(ddicMutex);
#ifdef _WIN32
	TXmutexUnlock(ObjMutex);
	lockFlags &= ~0x1;
#endif /* _WIN32 */
	closesql(ddic, ddicMutex);
	return(ret);
}

int
TXgetMonitorServerUrl(char **url, int *runLevel, int *hasScheduleService)
/* Sets `*url' to Texis Monitor schedule server URL (alloced), i.e.
 * protocol://host:port, and `*runLevel' to [Scheduler] Run Level.
 * Returns 0 on error.
 */
{
	static const char	fn[] = "TXgetMonitorServerUrl";
	static const char	schedulerSectionName[] = "Scheduler";
	const char		*protocol, *addr, *s;
	int			runLev, len, ret, port;
	char			tmpBuf[512], *listen = NULL;
        TXPMBUF                 *pmbuf = TXPMBUFPN;

	protocol = "http";
	addr = "127.0.0.1";
	port = TX_DEFSCHEDULEPORT;
	runLev = TX_DEF_SCHEDULER_RUN_LEVEL;
	if (hasScheduleService)
		*hasScheduleService = TX_DEF_SCHEDULER_SCHEDULER_ENABLED;

	if (TxConf)
	{
		s = getconfstring(TxConf, schedulerSectionName, "SSL",
				  CHARPN);
		if (s && strcmpi(s, "on") == 0)
		{
			protocol = "https";
			port = TX_DEFSCHEDULEPORT_SECURE;
		}
		addr = getconfstring(TxConf, schedulerSectionName,
				     "Bind Address", (char *)addr);
		/* Check [Scheduler] Port here, *after* checking
		 * [Scheduler] SSL and setting default port:
		 */
		port = getconfint(TxConf, schedulerSectionName, "Port", port);
                /* Bind Address / Port overridden by Listen in version 8+: */
		if (TX_IPv6_ENABLED(TXApp) &&
                    (listen = getconfstring(TxConf, schedulerSectionName,
                                            "Listen", NULL)) != NULL &&
                    (listen = TXstrdup(pmbuf, __FUNCTION__, listen)) != NULL)
                  {
                    char        *s;
                    const char  *portStr;
                    size_t      portStrLen;
                    int         portMaybe, errNum;

                    s = strrchr(listen, ':');   /* *last* `:' in case IPv6 */
                    if (s)                      /* addr:port */
                      {
                        *(s++) = '\0';          /* terminate addr */
                        addr = listen;
                        portStrLen = strlen(s);
                        if (*s == '[' && s[portStrLen - 1] == ']')
                          {                     /* IPv6 */
                            /* `[abcd::1234]' -> `abcd::1234': */
                            memmove(s, s + 1, portStrLen - 2);
                            s[portStrLen - 2] = '\0';
                          }
                        portStr = s;
                      }
                    else                        /* port only */
                      portStr = listen;
                    portMaybe = TXstrtoui16(portStr, NULL, NULL,
                                      (0 | TXstrtointFlag_NoLeadZeroOctal |
				       TXstrtointFlag_ConsumeTrailingSpace |
				       TXstrtointFlag_TrailingSourceIsError),
                                            &errNum);
                    if (errNum == 0) port = portMaybe;
                  }

		runLev = getconfint(TxConf, schedulerSectionName, "Run Level",
				    runLev);
		if (hasScheduleService)
		{
			char	*services, *token;
			size_t	tokenLen;

			/* NOTE: see also txMonitorGetVortexScheduleSettings()
			 */
			*hasScheduleService = 0;
			services = getconfstring(TxConf, schedulerSectionName,
						 "Services",
						 TX_DEF_SCHEDULER_SERVICES);
			for (token = services; *token; token += tokenLen)
			{
				token += strspn(token, WhiteSpace);
				if (!*token) break;
				tokenLen = strcspn(token, WhiteSpace);
				/* schedule status createlocks: */
				if (tokenLen == 8 &&
				    strnicmp(token, "schedule", 8) == 0)
					*hasScheduleService = 1;
			}
		}
	}

	len = htsnpf(tmpBuf, sizeof(tmpBuf), "%s://%s:%d",
		     protocol, addr, port);
	if (len >= (int)sizeof(tmpBuf))
	{
		putmsg(MERR + MAE, fn, "Buffer too small");
		goto err;
	}
	if (url && !(*url = TXstrdup(TXPMBUFPN, fn, tmpBuf))) goto err;
	if (runLevel) *runLevel = runLev;
	ret = 1;
	goto done;

err:
	ret = 0;
	if (url) *url = CHARPN;
	if (runLevel) *runLevel = 0;
done:
        listen = TXfree(listen);
	return(ret);
}

#ifdef TEST

main()
{
	DDIC *ddic;
	int i;

	ddic = ddopen("h:/tmp/junk");
	TXcreatesysschedule(ddic);
	for (i = 0; i < 4; i++)
	{
		TXrunscheduledevents(ddic, 1);
		Sleep(5000);
	}
}

#endif
