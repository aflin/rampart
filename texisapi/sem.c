/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#if defined(FORCENOLOCK) && !defined(NOLOCK)
#  define NOLOCK              /* MAW 08-03-95 - config.h turns it off */
#endif

#ifndef NOLOCK
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef USE_POSIX_SEM
#  include <semaphore.h>
#endif /* USE_POSIX_SEM */
#ifdef EPI_HAVE_SYS_SEM_H
#  include <sys/sem.h>
#endif /* EPI_HAVE_SYS_SEM_H */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "sizes.h"
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"
#include "mmsg.h"
#ifndef HAVE_STRERROR
#  define strerror(a)  sys_errlist[a]
#endif

#define SLEEP(pmbuf, sec)	\
	TXsleepForLocks(pmbuf, (sec)*1000, __FUNCTION__, __LINE__)

int     TxNoLockYap = 0;

/* wtf TXsemlockCount/Callback is mostly hackery until we start using
 * semtimedop() etc.
 */

static TXATOMINT	TXsemlockCount = 0;
static TXALARMFUNC	*TXsemunlockCallbackFunc = NULL;
static void		*TXsemunlockCallbackVal = NULL;

#define SEMLOCK_END()	TX_ATOMIC_INC(&TXsemlockCount)
#define SEMUNLOCK_END()	{						\
	if (TXsemlockCount > 0) TX_ATOMIC_DEC(&TXsemlockCount); 	\
	if (TXsemunlockCallbackFunc)					\
		TXsemunlockCallbackFunc(TXsemunlockCallbackVal); }

int
TXgetSemlockCount(void)
/* Thread-safe.  Signal-safe.
 */
{
	return(TXsemlockCount);
}

TXbool
TXsetSemunlockCallback(TXALARMFUNC *func, void *usr)
/* Thread-safe.  Signal-safe.
 */
{
	TXsemunlockCallbackFunc = func;
	TXsemunlockCallbackVal = usr;
	return(TXbool_True);
}

extern int TxLockVerbose;

/* lockverbose stuff: */
#ifndef USE_NTSHM
#  define SEMCTL3(pm, id, num, cmd)	\
	TXsemctl3(pm, __FUNCTION__, id, num, cmd)
#  define SEMCTL(pm, id, num, cmd, arg)	\
	TXsemctl4(pm, __FUNCTION__, id, num, cmd, arg)
#  define SEMGET(pm, key, nsems, flags)	\
	TXsemget(pm, __FUNCTION__, key, nsems, flags)

static const char *
TXsemctlCmdToStr(int cmd)
{
	switch (cmd)
	{
#  ifdef GETVAL
	case GETVAL:	return("GETVAL");
#  endif
#  ifdef SETVAL
	case SETVAL:	return("SETVAL");
#  endif
#  ifdef IPC_RMDIR
	case IPC_RMID:	return("IPC_RMID");
#  endif
#  ifdef IPC_STAT
	case IPC_STAT:	return("IPC_STAT");
#  endif
#  ifdef IPC_SET
	case IPC_SET:	return("IPC_SET");
#  endif
#  ifdef IPC_INFO
	case IPC_INFO:	return("IPC_INFO");
#  endif
	default:	return("?");
	}
}

int
TXsemctl3(TXPMBUF *pmbuf, const char *func, int semid, int semNum, int cmd)
{
	int	ret;

	if (TXgetlockverbose() & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, func,
			       "semctl(id %d, semnum %d, %s) starting",
			       semid, semNum, TXsemctlCmdToStr(cmd));
	TXclearError();			/* may not get cleared on success */
	ret = semctl(semid, semNum, cmd);
	if (TXgetlockverbose() & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, func,
			 "semctl(id %d, semnum %d, %s) returned %d err %d=%s",
			       semid, semNum, TXsemctlCmdToStr(cmd), ret,
			       (int)saveErr, TXgetOsErrName(saveErr, "?"));
		TX_POPERROR();
	}
	return(ret);
}

int
TXsemctl4(TXPMBUF *pmbuf, const char *func, int semid, int semNum, int cmd,
	  TXSEMUN su)
{
	int	ret;

	if (TXgetlockverbose() & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, func,
			       "semctl(id %d, num %d, %s, ...) starting",
			       semid, semNum, TXsemctlCmdToStr(cmd));
	TXclearError();			/* may not get cleared on success */
	ret = semctl(semid, semNum, cmd, su);
	if (TXgetlockverbose() & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, func,
		       "semctl(id %d, num %d, %s, ...) returned %d err %d=%s",
			       semid, semNum, TXsemctlCmdToStr(cmd), ret,
			       (int)saveErr, TXgetOsErrName(saveErr, "?"));
		TX_POPERROR();
	}
	return(ret);
}

static int
TXsemget(TXPMBUF *pmbuf, const char *func, int semkey, int nsems, int semflags)
{
	int	ret;
	char	flagsBuf[256], *d, keyBuf[256];

	*flagsBuf = '\0';
	if (TXgetlockverbose() & 0x30)
	{
		if (semkey == IPC_PRIVATE)
			TXstrncpy(keyBuf, "IPC_PRIVATE", sizeof(keyBuf));
		else
			htsnpf(keyBuf, sizeof(keyBuf), "0x%x", semkey);
		d = flagsBuf;
		if (semflags & IPC_CREAT)
		{
			if (d > flagsBuf)
			{
				strcpy(d, " | ");
				d += strlen(d);
			}
			strcpy(d, "IPC_CREAT");
			d += strlen(d);
		}
		if (semflags & IPC_EXCL)
		{
			if (d > flagsBuf)
			{
				strcpy(d, " | ");
				d += strlen(d);
			}
			strcpy(d, "IPC_EXCL");
			d += strlen(d);
		}
		if (semflags & ~(IPC_CREAT | IPC_EXCL))
		{
			if (d > flagsBuf)
			{
				strcpy(d, " | ");
				d += strlen(d);
			}
			htsnpf(d, (flagsBuf + sizeof(flagsBuf)) - d, "0%o",
			       (int)(semflags & ~(IPC_CREAT | IPC_EXCL)));
		}
	}

	if (TXgetlockverbose() & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, func,
			      "semget(key %s, numsems %d, flags %s) starting",
			       keyBuf, nsems, flagsBuf);
	TXclearError();
	ret = semget(semkey, nsems, semflags);
	if (TXgetlockverbose() & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, func,
	      "semget(key %s, numsems %d, flags %s) returned id %d err %d=%s",
			       keyBuf, nsems, flagsBuf, ret,
			       (int)saveErr, TXgetOsErrName(saveErr, "?"));
		TX_POPERROR();
	}
	return(ret);
}
#endif /* !USE_NTSHM */

/******************************************************************/

void
seminit(s)
SEM *s;
{
#ifndef HAVE_SEM
	int i;

	for (i = 0; i < NSERVERS; i++)
		s->sem[i] = 0;
	s->value = NSERVERS;
#else /* HAVE_SEM */
	s->wwant = 0;
	s->writel = 0;
	s->nreader = 0;
#endif /* HAVE_SEM */
}

/******************************************************************/

#ifndef HAVE_SEM
void
Pprim(SEM *t, int sid)
{
	int first, i;
loop:
	first = t->lastid;
	t->sem[sid] = 1;		/* Declare intent */
forloop:
	for (i = first; i < NSERVERS; i++)
	{
		if (i == sid)
		{
			t->sem[sid] = 2;
			for (i = 0; i < NSERVERS; i++)
				if (i != sid && t->sem[i] == 2)
					goto loop;
			t->lastid = sid;
			return;
		}
		else if (t->sem[i])
			goto loop;
	}
	first = 0;
	goto forloop;
}

/******************************************************************/

void
Vprim(SEM *t, int sid)
{
	t->lastid = (sid + 1) % NSERVERS;
	t->sem[sid] = 0;
}

/******************************************************************/

void
V(SEM *sem, int sid)
{
	Pprim(sem, sid);
	sem->value ++;
	if (sem->sqh - sem->sqt)
		kill(sem->sq[sem->sqh++], SIGUSR1);
	Vprim(sem, sid);
}

/******************************************************************/

int
PC(SEM *sem, int sid)
{
	Pprim(sem, sid);
	sem->value --;
	if (sem->value >= 0)
	{
		Vprim(sem, sid);
		return 1;
	}
	sem->value ++;
	Vprim(sem, sid);
	return 0;
}

/******************************************************************/

void
sighv(int sig, int code, struct sigcontext *sc)
{
	putmsg(MINFO,(char *)NULL, "Wakeup!");
}

/******************************************************************/

int
P(SEM *sem, int sid)
{
loop:
	Pprim(sem, sid);
	sem->value --;
	if (sem->value >= 0)
	{
		Vprim(sem, sid);
		return 1;
	}
	sem->value ++;
	sem->sq[sem->sqt++] = TXgetpid(0);
	Vprim(sem, sid);
	signal(SIGUSR1, sighv);
	putmsg(MINFO,(char *)NULL, "Sleep!");
	SLEEP(TXPMBUFPN, 2);
	goto loop;
}

#else /* HAVE_SEM */

#ifdef USE_POSIX_SEM
#  error add lockverbose tracing
/******************************************************************/

int
semlock(pmbuf, dblock, create)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dblock;
TXbool		create;	/* ok to create */
/* Returns -1 on error, 0 on success.
 */
{
	int rc, tries, ret;

    for (tries = 1; ; tries++)
    {
	rc = sem_wait(dblock->semid);
	switch(rc)
	{
		case 0:				/* success */
			ret = 0;
			goto finally;
		default:
			switch(errno)
			{
			case EINVAL:
				txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
					       "Semaphore for database `%s' no longer exists", TX_DBLOCK_PATH(dblock));
				goto err;
			case EINTR:		/* signal interrupted us */
				if (tries < 5) continue;	/* retry */
				/* else fall through and report: */
			default:
				txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			    "Invalid return (%d/%s) while waiting for semaphore for database `%s'",
					       (int)errno, strerror(errno), TX_DBLOCK_PATH(dblock));
				goto err;
			}
	}
    }
err:
    ret = -1;
finally:
    if (ret == 0) SEMLOCK_END();
    return(ret);
}

/******************************************************************/

int
semunlock(pmbuf, dblock)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dblock;
{
	sem_post(dblock->semid);
	SEMUNLOCK_END();
	return 0;
}

static char *
gensemname(TXPMBUF *pmbuf, char *path)
{
	static const char	fn[] = "gensemname";
#define SEM_PATH_LENGTH	5
	int	temp[SEM_PATH_LENGTH];
	int	i;
	char	*rc;

	for(i = 0; i < SEM_PATH_LENGTH; i++)
		temp[i] = i;
	rc = TXmalloc(pmbuf, fn, SEM_PATH_LENGTH+3);
	if(!rc)
		return NULL;
	for(i = 0; path[i]; i++)
		temp[i%SEM_PATH_LENGTH] += path[i];
	rc[0] = '/';
	for(i = 0; i < SEM_PATH_LENGTH; i++)
		rc[i+1] = (temp[i] % 58) + 64;
	rc[SEM_PATH_LENGTH+2] = '\0';
	return(rc);
}

int
initmutex(pmbuf, dbl)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dbl;
{
	char *semname;
	int rc = 0, res;

	semname = gensemname(pmbuf, dbl->idbl->path);
	if (TXgetlockverbose() & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			       "sem_unlink(`%s') starting", semname);
	res = sem_unlink(semname);
	if (TXgetlockverbose() & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			       "sem_unlink(`%s') returned %d err %d=%s",
			       semname, res, (int)TXgeterror(),
			       TXstrerror(TXgeterror()));
		TX_POPERROR();
	}
	if (TXgetlockverbose() & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			       "sem_open(`%s', (O_CREAT | O_EXCL), 0777, value 1) starting", semname);
	dbl->semid = sem_open(semname, O_CREAT | O_EXCL, 0777, 1);
	if (TXgetlockverbose() & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			       "sem_open(`%s', (O_CREAT | O_EXCL), 0777, value 1) returned id %d err %d=%s",
			       semname, (int)dbl->semid, (int)TXgeterror(),
			       TXstrerror(TXgeterror()));
		TX_POPERROR();
	}
/*
	rc = sem_init(dbl->semid, 1, 1);
*/
	if(dbl->semid == -1)
	{
		txpmbuf_putmsg(pmbuf, MERR, Fn, "sem_open [%s] failed [%s]",
			       semname, strerror(errno));
		rc = -1;
	}
	else if (TX_LOCKVERBOSELEVEL() >= 1)
		txpmbuf_putmsg(pmbuf, MINFO, CHARPN, "Created semaphore %s",
			       semname);
	semname = TXfree(semname);
	return rc;
}

int
openmutex(pmbuf, dbl, create)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dbl;
TXbool	create;
{
	char *semname;
	int rc;

	semname = gensemname(pmbuf, dbl->idbl->path);
	if (TXgetlockverbose() & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			   "sem_open(`%s', flags %s, 0777, value 1) starting",
			       semname, (create ? "O_CREAT" : "0"));
	dbl->semid = sem_open(semname, (create ? O_CREAT : 0), 0777, 1);
	if (TXgetlockverbose() & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			       "sem_open(`%s', flags %s, 0777, value 1) returned id %d err %d=%s",
			       semname, (create ? "O_CREAT" : "0"),
			       (int)dbl->semid, (int)TXgeterror(),
			       TXstrerror(TXgeterror()));
		TX_POPERROR();
	}
	if(dbl->semid == -1)
	{
		txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
			       "sem_open(`%s') failed for `%s': %s",
			       semname, dbl->idbl->path,
			       TXstrerror(TXgeterror()));
		semname = TXfree(semname);
		return -1;
	}
	semname = free(semname);
	if (TXgetlockverbose() & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			       "sem_trywait(id %d) starting",
			       (int)dbl->semid);
	rc = sem_trywait(dbl->semid);
	if (TXgetlockverbose() & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			       "sem_trywait(id %d) returned id %d err %d=%s",
			       (int)dbl->semid, rc, (int)TXgeterror(),
			       TXstrerror(TXgeterror()));
		TX_POPERROR();
	}
	if(rc == 0)
	{
		if (TXgetlockverbose() & 0x10)
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
				       "sem_post(id %d) starting",
				       (int)dbl->semid);
		res = sem_post(dbl->semid);
		if (TXgetlockverbose() & 0x20)
		{
			TX_PUSHERROR();
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
				   "sem_post(id %d) returned id %d err %d=%s",
				       (int)dbl->semid, res, (int)TXgeterror(),
				       TXstrerror(TXgeterror()));
			TX_POPERROR();
		}
		return 0;
	}
	else if (errno == EAGAIN)
	{
		return 0;
	}
	txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
		       "sem_trywait failed for `%s': %s",
		       TX_DBLOCK_PATH(dbl), TXstrerror(TXgeterror()));
	return -1;
}

int
closemutex(dbl)
VOLATILE DBLOCK	*dbl;
{
	sem_close(dbl->semid);
	return 0;
}

int
delmutex(pmbuf, dbl)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dbl;
{
	char *semname;

	semname = gensemname(pmbuf, dbl->idbl->path);
	if(semname)
	{
		if (TX_LOCKVERBOSELEVEL() >= 1)
			txpmbuf_putmsg(pmbuf, MINFO, CHARPN,
				       "Deleting semaphore %s", semname);
		sem_unlink(semname);
	}
	semname = TXfree(semname);
	return 0;
}

#else /* !USE_POSIX_SEM */
#ifdef USE_NTSHM

/******************************************************************/

int
semlock(pmbuf, dblock, create)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dblock;
TXbool		create;	/* ok to create */
/* Returns -1 on error, 0 on success.
 */
{
	DWORD	dwWaitResult;
	int	ret;

#ifdef TX_DEBUG
	dwWaitResult = WaitForSingleObject(dblock->semid, INFINITE);
#else
	dwWaitResult = WaitForSingleObject(dblock->semid, 5000L);
#endif
	switch(dwWaitResult)
	{
		case WAIT_OBJECT_0:
		case WAIT_ABANDONED:
			ret = 0;
			goto finally;
		case WAIT_TIMEOUT:
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
				       "Timed out waiting for mutex for database `%s'", TX_DBLOCK_PATH(dblock));
			goto err;
#ifdef NEVER
		case WAIT_ABANDONED:
			txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
				       "Mutex no longer exists for database `%s'", TX_DBLOCK_PATH(dblock));
			goto err;
#endif
		default:
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
		     "Invalid return (0x%x) while waiting for mutex for database `%s': error %d (%s)",
				       (int)dwWaitResult,
				       TX_DBLOCK_PATH(dblock),
				       (int)TXgeterror(),
				       TXstrerror(TXgeterror()));
			goto err;
	}
err:
	ret = -1;
finally:
	if (ret == 0) SEMLOCK_END();
	return(ret);
}

/******************************************************************/

int
semunlock(pmbuf, dblock)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dblock;
{
	ReleaseMutex(dblock->semid);
	SEMUNLOCK_END();
	return 0;
}

int
initmutex(pmbuf, dbl)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dbl;
/* Returns 0 on success, -1 on error.
 */
{
	static CONST char Fn[] = "initmutex";
	char	*MutexName;

	MutexName = TXstrcatN(pmbuf, Fn, dbl->idbl->path, "MUTEX", NULL);
	if (!MutexName) return(-1);
	dbl->semid = TXCreateMutex(pmbuf, MutexName);
	if (dbl->semid != NULL && TX_LOCKVERBOSELEVEL() >= 1)
		txpmbuf_putmsg(pmbuf, MINFO, CHARPN,
			       "Created semaphore handle 0x%wx",
			       (EPI_HUGEINT)dbl->semid);
	MutexName = TXfree(MutexName);
	return dbl->semid != NULL ? 0 : -1 ;
}

int
openmutex(pmbuf, dbl, create)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dbl;
TXbool	create;
/* Returns 0 on success, -1 on error.
 */
{
	char	*mutexName = NULL;

	mutexName = TXstrcatN(pmbuf, __FUNCTION__, dbl->idbl->path, "MUTEX",
			      NULL);
	if (!mutexName) return(-1);
	if (create)
		dbl->semid = TXCreateMutex(pmbuf, mutexName);
	else
		dbl->semid = TXOpenMutex(pmbuf, mutexName);
	TX_PUSHERROR();
	mutexName = TXfree(mutexName);
	TX_POPERROR();
	return(dbl->semid ? 0 : -1);
}

int
delmutex(pmbuf, dbl)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dbl;
{
	if (TX_LOCKVERBOSELEVEL() >= 1)
		txpmbuf_putmsg(pmbuf, MINFO, CHARPN,
			       "Deleting mutex handle 0x%wx",
			       (EPI_HUGEINT)dbl->semid);
	CloseHandle(dbl->semid);
	return 0;
}

#else /* !USE_NTSHM */
/******************************************************************/

#ifdef MEM_PROTECT
#include <sys/shm.h>
static int
TXprotect(DBLOCK *a)
{
	int	sid = a->idbl->shmid;

	struct	shmid_ds	sm;

	shmctl(sid, IPC_STAT, &sm);
	sm.shm_perm.mode = 0444;
	shmctl(sid, IPC_SET, &sm);
}

static int
TXunprotect(DBLOCK *a)
{
	int	sid = a->idbl->shmid;

	struct	shmid_ds	sm;

	shmctl(sid, IPC_STAT, &sm);
	sm.shm_perm.mode = 0777;
	shmctl(sid, IPC_SET, &sm);
}
#else /* !MEM_PROTECT */
#define TXprotect(a)
#define TXunprotect(a)
#endif /* !MEM_PROTECT */

/******************************************************************/

#ifdef NEVER
static SIGTYPE
dummy(sig)
int	sig;
{
}
#endif /* NEVER */

static void
TXprintSemOps(char *buf, size_t bufSz, struct sembuf *ops, unsigned numOps)
{
	char	flagsBuf[128], *d;

	if (!ops)
		TXstrncpy(buf, "NULL", bufSz);
	else if (numOps <= 0)
		TXstrncpy(buf, "?", bufSz);
	else
	{
		d = flagsBuf;
		if (ops->sem_flg & IPC_NOWAIT)
		{
			if (d > flagsBuf)
			{
				strcpy(d, " | ");
				d += strlen(d);
			}
			strcpy(d, "IPC_NOWAIT");
			d += strlen(d);
		}
		if (ops->sem_flg & SEM_UNDO)
		{
			if (d > flagsBuf)
			{
				strcpy(d, " | ");
				d += strlen(d);
			}
			strcpy(d, "SEM_UNDO");
			d += strlen(d);
		}
		if (ops->sem_flg & ~(IPC_NOWAIT | SEM_UNDO))
		{
			if (d > flagsBuf)
			{
				strcpy(d, " | ");
				d += strlen(d);
			}
			htsnpf(d, (flagsBuf + sizeof(flagsBuf)) - d, "0%o",
			       (int)(ops->sem_flg & ~(IPC_NOWAIT | SEM_UNDO)));
		}
		*d = '\0';

		d = buf + htsnpf(buf, bufSz, "semnum %d, op %d, flags %s",
		       (int)ops->sem_num, (int)ops->sem_op, flagsBuf);
		if (numOps > 1) 
			htsnpf(d, (buf + bufSz) - d, "...");
	}
}

#ifndef EPI_HAVE_SEMTIMEDOP
static void CDECL
TXsemopTimeoutHandler(void *usr)
{
	*(TXbool *)usr = TXbool_True;
}
#endif /* !EPI_HAVE_SEMTIMEDOP */

static int
TXsemtimedop(TXPMBUF *pmbuf, const char *func, int semid,
	     struct sembuf *ops, unsigned numOps, int timeoutMsec)
{
	char	preOpBuf[128], timeBuf[128];
	int	ret;
#ifdef EPI_HAVE_SEMTIMEDOP
	struct timespec	ts, *tsPtr;

	if (timeoutMsec < 0)			/* wait indefinitely */
		tsPtr = NULL;
	else
	{
		ts.tv_sec = timeoutMsec/1000;
		ts.tv_nsec = (timeoutMsec % 1000)*1000000;
		tsPtr = &ts;
	}
#  define PFX	", timeout "
#  define SFX	")"
#  define TIMED	"timed"
#else /* !EPI_HAVE_SEMTIMED_OP */
	TXbool	gotAlarm = TXbool_False;

#  define PFX	") alarm "
#  define SFX	""
#  define TIMED	""
#endif /* !EPI_HAVE_SEMTIMED_OP */

	*preOpBuf = *timeBuf = '\0';
	if (TxLockVerbose & 0x30)
	{
		if (timeoutMsec >= 0)
			htsnpf(timeBuf, sizeof(timeBuf), PFX "%kd.%03d s" SFX,
			       timeoutMsec/1000, timeoutMsec % 1000);
		else
			htsnpf(timeBuf, sizeof(timeBuf), PFX "NULL" SFX);
		TXprintSemOps(preOpBuf, sizeof(preOpBuf), ops, numOps);
	}
	if (TxLockVerbose & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, func,
		     "sem" TIMED "op(id %d, ops { %s }, numops %d%s starting",
			       semid, preOpBuf, (int)numOps, timeBuf);

#ifndef EPI_HAVE_SEMTIMEDOP
	if (timeoutMsec >= 0)
		TXsetalarm(TXsemopTimeoutHandler, &gotAlarm,
			    ((double)timeoutMsec)/1000.0, TXbool_False);
#endif /* !EPI_HAVE_SEMTIMED_OP */
	TXclearError();
	TXintsem = semid;			/* for monitor.c alarm */
	TXclearStuckSemaphoreAlarmed = TXbool_False;
#ifdef EPI_HAVE_SEMTIMEDOP
	ret = semtimedop(semid, ops, numOps, tsPtr);
#else /* !EPI_HAVE_SEMTIMED_OP */
	ret = semop(semid, ops, numOps);
#endif /* !EPI_HAVE_SEMTIMED_OP */
	TXintsem = -1;
#ifndef EPI_HAVE_SEMTIMEDOP
	if (timeoutMsec >= 0)
		TXunsetalarm(TXsemopTimeoutHandler, &gotAlarm, NULL);
#endif /* !EPI_HAVE_SEMTIMED_OP */
	/* WTF retry on EINTR -- regardless of whether using semtimedop()
	 * or not -- if we have not reached `timeoutMsec': could be
	 * another unrelated alarm, SIGCLD etc.  Need to then remove
	 * (or coordinate with global `TXclearStuckSemaphoreAlarmed')
	 * monitor.c's clearStuckSemaphore?
	 */

	if (TxLockVerbose & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, func,
	 "sem" TIMED "op(id %d, ops { %s }, numops %d%s returned %d err %d=%s",
			       semid, preOpBuf, (int)numOps, timeBuf, ret,
			       (int)saveErr, TXgetOsErrName(saveErr, "?"));
		TX_POPERROR();
	}
	return(ret);
#undef PFX
#undef SFX
#undef TIMED
}

/******************************************************************/

static int newmutex(TXPMBUF *pmbuf, VOLATILE DBLOCK *dbl, int timeoutMsec);


int
semlock(pmbuf, dblock, create)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dblock;
TXbool		create;	/* ok to create */
/* Returns -1 on error, 0 on success.
 */
{
	int rc;
	TXERRTYPE	rcErr;
	struct sembuf s;
	TXSEMUN	su;
	long	ntries = 0;

	if (TxLockVerbose & 0x40)
		txpmbuf_putmsg(pmbuf, MINFO, NULL,
			       "%s() begin for database `%s'", __FUNCTION__,
			       TX_DBLOCK_PATH(dblock));

	s.sem_num = 0;
	s.sem_op = -1;
	s.sem_flg = SEM_UNDO;

try_sem:
	TXunprotect(dblock);
	ntries ++;
	rc = 0;
	rcErr = 0;
	/* su.val = SEMCTL(pmbuf, dblock->idbl->semid, 0, GETVAL, su); */
	if(dblock->idbl->curpid == TXgetpid(0))
	{
	/*	Typically means we were interrupted while we hold the
		lock, and the signal handler is trying to do locking
	*/
		su.val = SEMCTL(pmbuf, dblock->idbl->semid, 0, GETVAL, su);
		if(su.val == 0)
		{
			if (!TxNoLockYap)    /* KNG 010619 convertsysindex */
				txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
					"Already have lock for database `%s'",
					       TX_DBLOCK_PATH(dblock));
			rc = -1;
			goto finally;
		}
	}
	if (TXsemtimedop(pmbuf, __FUNCTION__, dblock->idbl->semid, &s, 1, -1)
	    == -1)
	{
		int	merr;

		merr = errno;
		rc = -1;
		rcErr = TXgeterror();
		switch(merr)
		{
#ifndef __sgi		/* EINVAL ambiguous on SGI */
			case EINVAL:	/* Semaphore does not exist */
#endif
#ifdef EIDRM
#if EIDRM!=EINVAL
			case EIDRM:	/* Semaphore was removed */
#endif
#endif
				if (!create || newmutex(pmbuf,dblock,-1) == -1)
					/* Retain original err for putmsg() */
					break;
				goto try_sem;
			case EAGAIN:	/* Semaphore currently locked */
                                        /* wtf or TXsemtimedop() timed out? */
			case EINTR:	/* Semaphore stuck locked for 5 secs */
				/* For dumplock, do not retry: */
				if (!create && TXclearStuckSemaphoreAlarmed)
					break;
				goto try_sem;
			case ENOSPC:
				if(ntries > 10)
				{
					txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__, "The OS will not allow more processes to access the semaphore for database `%s'",
						       TX_DBLOCK_PATH(dblock));
					break;
				}
				SLEEP(pmbuf, 1);
				goto try_sem;
#ifdef __sgi		/* EINVAL ambiguous on SGI */
			case EINVAL:
#  error dead SGI code
#endif /* __sgi */
			default:
                              /* MAW 06-23-94 - putmsg() not perror() */
		                txpmbuf_putmsg(pmbuf, MERR + LKE, __FUNCTION__,
					  "Semop on database `%s' failed: %s",
					       TX_DBLOCK_PATH(dblock),
					       TXstrerror(rcErr));
				break;
		}
	}
	if(rc == 0)
	{
		dblock->lsemid = dblock->idbl->semid;
		dblock->idbl->curpid = TXgetpid(0);
		/* For testing stuck lock removal: */
		if (TxLockVerbose & 0x10000000)
		{
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
				       "Pausing after semlock for 60 sec");
			TXsleepmsec(60*1000, 1);
		}
	}
	if(rc == -1)
		txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
			 "Unable to obtain semaphore for database `%s': %s%s",
			       TX_DBLOCK_PATH(dblock), TXstrerror(rcErr),
			       (TXclearStuckSemaphoreAlarmed ?
				" (and timeout)" : ""));
finally:
	if (TxLockVerbose & 0x80)
		txpmbuf_putmsg(pmbuf, MINFO, NULL,
			       "%s() end (%s) for database `%s'", __FUNCTION__,
			       (rc == 0 ? "success" : "failed"),
			       TX_DBLOCK_PATH(dblock));
	if (rc == 0) SEMLOCK_END();
	return rc;
}

/******************************************************************/

int
semunlock(pmbuf, dblock)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dblock;
/* Returns 0 on success, -1 on error.
 */
{
	TXSEMID	semid;
	struct sembuf s;
	int val, ret;
	TXSEMUN	su;

	if (TxLockVerbose & 0x40)
		txpmbuf_putmsg(pmbuf, MINFO, NULL,
			       "%s() begin for database `%s'", __FUNCTION__,
			       TX_DBLOCK_PATH(dblock));

	TXunprotect(dblock);
	semid = dblock->lsemid;
	s.sem_num = 0;
	s.sem_op = 1;
	s.sem_flg = SEM_UNDO;
	memset(&su, 0, sizeof(TXSEMUN));
	val = SEMCTL(pmbuf, semid, 0, GETVAL, su);
	if (val > 0) goto ok;
	if (TXsemtimedop(pmbuf, __FUNCTION__, semid, &s, 1, -1) == -1)
	{
                              /* MAW 06-23-94 - putmsg() not perror() */
		txpmbuf_putmsg(pmbuf, MERR + UKE, __FUNCTION__,
			     "semop() failed for lock structure for `%s': %s",
			       TX_DBLOCK_PATH(dblock), TXstrerror(TXgeterror()));
	}
	dblock->idbl->curpid = 0;
	TXprotect(dblock);
	if(TXunlocksig)
	{
		int sig = TXunlocksig;

		TXunlocksig = 0;        /* Reset */
		kill(TXgetpid(1), sig); /* Force real getpid to make sure */
	}
ok:
	ret = 0;
	if (TxLockVerbose & 0x80)
		txpmbuf_putmsg(pmbuf, MINFO, NULL,
			       "%s() end (%s) for database `%s'", __FUNCTION__,
			       (ret == 0 ? "success" : "failed"),
			       TX_DBLOCK_PATH(dblock));
	if (ret == 0) SEMUNLOCK_END();
	return(ret);
}

int
initmutex(pmbuf, dbl)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dbl;
{
	static CONST char	Fn[] = "initmutex";
	TXSEMUN	su;
	TXSEMID	semid;

	semid = SEMGET(pmbuf, IPC_PRIVATE, 1, 0777);
#ifdef NEVER
	txpmbuf_putmsg(pmbuf, 999, NULL, "%d Created semaphore %d",
		       TXgetpid(0), (int)semid);
#endif
	if (semid == -1)
	{
		txpmbuf_putmsg(pmbuf, MERR + FOE, Fn,
			       "Could not obtain semaphore: %s",
			       strerror(errno));
		return -1;
	}
	else if (TX_LOCKVERBOSELEVEL() >= 1)
		txpmbuf_putmsg(pmbuf, MINFO, Fn, "Created semaphore id %d",
			       (int)semid);
	su.val = 1;
	SEMCTL(pmbuf, semid, 0, SETVAL, su);
	dbl->idbl->semid = semid; /* Make sure it's ready before public */
	return 0;
}

int
openmutex(pmbuf, dbl, create)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dbl;
TXbool	create;
/* Returns -1 on error, 0 on success.
 */
{
	if(semlock(pmbuf, dbl, create) == -1)		/* failed */
		return -1;
	semunlock(pmbuf, dbl);
	return 0;
}

int
tx_delsem(pmbuf, semid)
TXPMBUF	*pmbuf;
int	semid;
{
#ifdef HAVE_SEMUN_UNION
	union semun	su;

	memset(&su, 0, sizeof(union semun));
#endif /* HAVE_SEMUN_UNION */

#ifdef HAVE_SEMUN_UNION
	if (SEMCTL(pmbuf, semid, 0, IPC_RMID, su) == -1)
#else /* !HAVE_SEMUN_UNION */
	if (SEMCTL3(pmbuf, semid, 0, IPC_RMID) == -1)
#endif /* !HAVE_SEMUN_UNION */
	{
		if (errno != EPERM)
			txpmbuf_putmsg(pmbuf, MWARN, NULL,
				"Could not remove semaphore id %d: %s",
				semid, strerror(errno));
		return -1;
	}
	else if (TX_LOCKVERBOSELEVEL() >= 1)
		txpmbuf_putmsg(pmbuf, MINFO, CHARPN,
			       "Deleted semaphore id %d", semid);
	return 0;
}

int
delmutex(pmbuf, dbl)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dbl;
{
	semlock(pmbuf, dbl, TXbool_True);
	return(tx_delsem(pmbuf, dbl->idbl->semid));
}

static int isvalidsem ARGS((TXPMBUF *pmbuf, TXSEMID semid));

static int
isvalidsem(pmbuf, semid)
TXPMBUF	*pmbuf;
TXSEMID	semid;
{
	int val;
	TXSEMUN	su;

	memset(&su, 0, sizeof(TXSEMUN));
	val = SEMCTL(pmbuf, semid, 0, GETVAL, su);
	if(val == -1) /* Bad semaphore */
		return 0;
	else
		return 1;
}

/******************************************************************/

static TXbool
TXlockCloseWrapper(TXPMBUF *pmbuf, const char *func, int fd)
/* Wrapper for close().
 * Returns false on error.
 */
{
	int	res;

	if (TxLockVerbose & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, func,
			       "close(%d) starting", fd);
        TXclearError();
	res = close(fd);
	if (TxLockVerbose & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, func,
			       "close(%d) returned %d err %d=%s",
			       fd, res, saveErr,
			       TXgetOsErrName(saveErr, "?"));
		TX_POPERROR();
	}
	return(res == 0);
}

static TXbool
TXlockUnlinkWrapper(TXPMBUF *pmbuf, const char *func, const char *path)
/* Wrapper for unlink().
 * Returns false on error (ENOENT is considered success).
 */
{
	int		res;
	TXERRTYPE	errNum;

	if (TxLockVerbose & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, func,
			       "unlink(`%s') starting", path);
        TXclearError();
	res = unlink(path);
	if (TxLockVerbose & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, func,
			       "unlink(`%s') returned %d err %d=%s",
			       path, res, saveErr,
			       TXgetOsErrName(saveErr, "?"));
		TX_POPERROR();
	}
	errNum = TXgeterror();
	return(res == 0 || TX_ERROR_IS_NO_SUCH_FILE(errNum));
}

static int
newmutex(pmbuf, dbl, timeoutMsec)
TXPMBUF		*pmbuf;
VOLATILE DBLOCK	*dbl;
int		timeoutMsec;	/* -1 == infinite */
/* Returns 0 on success, -1 on error.
 */
{
	TXSEMUN	su;
	char	tmpname[PATH_MAX];
	int	fd;
	EPI_STAT_S	stb;
	int	terrno, ret;
	const char	*path = TX_DBLOCK_PATH(dbl);
	double	deadlineFixedRate, nowFixedRate, sleepSec;

	if (timeoutMsec < 0)			/* infinite */
		deadlineFixedRate = -1.0;
	else
		deadlineFixedRate = TXgetTimeContinuousFixedRateOrOfDay() +
			((double)timeoutMsec)/(double)1000.0;

	if (strlen(path)
#ifdef USE_SHM
	    + 9					/* /SYSLOCKS */
#endif /* USE_SHM */
	    + 5 > sizeof(tmpname))		/* .LCK + nul */
	{
		txpmbuf_putmsg(pmbuf, MERR + MAE, __FUNCTION__,
			       "Path `%s' too long", TX_DBLOCK_PATH(dbl));
		goto err;
	}
	strcpy(tmpname, path);
#ifdef USE_SHM
	strcat(tmpname, PATH_SEP_S "SYSLOCKS");
#endif
	strcat(tmpname, ".LCK");

	while (!isvalidsem(pmbuf, dbl->idbl->semid))
	{
		if (TxLockVerbose & 0x10)
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			"open(`%s', O_RDWR | O_CREAT | OEXCL, 0666) starting",
				       tmpname);
                TXclearError();
		fd = open(tmpname, O_RDWR | O_CREAT | O_EXCL, 0666);
		if (TxLockVerbose & 0x20)
		{
			TX_PUSHERROR();
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
	   "open(`%s', O_RDWR | O_CREAT | OEXCL, 0666) returned %d err %d=%s",
				       tmpname, fd, saveErr,
				       TXgetOsErrName(saveErr, "?"));
			TX_POPERROR();
		}
		terrno = errno;
		if(isvalidsem(pmbuf, dbl->idbl->semid))
		{
			if(fd != -1)
			{
				TXlockCloseWrapper(pmbuf, __FUNCTION__, fd);
				if (!TXlockUnlinkWrapper(pmbuf, __FUNCTION__,
							 tmpname))
					txpmbuf_putmsg(pmbuf, MWARN + FDE,
						       __FUNCTION__,
						  "Could not delete `%s': %s",
						       tmpname,
						    TXstrerror(TXgeterror()));
			}
			goto ok;
		}
		if(fd == -1)
		{
			if(EPI_STAT(tmpname, &stb)!=-1)
			{	/* File exists.  Give other process 5 seconds
			           to generate valid semaphore */
				if((time(NULL) - stb.st_ctime) > 5)
				{
                                  if (!TXlockUnlinkWrapper(pmbuf,__FUNCTION__,
                                                           tmpname))
                                    txpmbuf_putmsg(pmbuf, MWARN + FDE,
                                                   __FUNCTION__,
						  "Could not delete `%s': %s",
                                                   tmpname,
                                                   TXstrerror(TXgeterror()));
				}
				sleepSec = 1.0;
                                /* Sleep for `sleepSec', or up to deadline: */
				if (timeoutMsec >= 0)
				{
				  nowFixedRate =
				      TXgetTimeContinuousFixedRateOrOfDay();
				  if (nowFixedRate + sleepSec >
                                      deadlineFixedRate)
                                    {
                                     sleepSec=(deadlineFixedRate-nowFixedRate);
                                     if (sleepSec < 0.0)
                                       {
                                         txpmbuf_putmsg(pmbuf, MERR,
                                                        __FUNCTION__,
                                              "Lock timeout on database `%s'",
                                                        path);
                                         goto err;
                                       }
                                    }
                                }
                                TXsleepForLocks(pmbuf, sleepSec*1000.0,
                                                __FUNCTION__, __LINE__);
			}
			else
			switch(terrno)
			{
			case EEXIST:
				break;
			case ENOENT:
				txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
				   "Database path `%s' no longer exists",
	                              dbl->idbl->path);
				goto err;
			default:
				txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__, "Could not create lockfile `%s': %s", tmpname, strerror(terrno));
				goto err;
			}
		}
		else
		{
			TXSEMID	semid;

			semid = SEMGET(pmbuf, IPC_PRIVATE, 1, 0777);
			if(semid == -1)
			{
				txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
				       "Could not create semaphore: %s",
				       strerror(errno));
				goto err;
			}
			su.val = 1;
			SEMCTL(pmbuf, semid, 0, SETVAL, su);
			dbl->idbl->semid = semid;
			TXlockCloseWrapper(pmbuf, __FUNCTION__, fd);
			if (!TXlockUnlinkWrapper(pmbuf, __FUNCTION__, tmpname))
                          txpmbuf_putmsg(pmbuf, MWARN + FDE, __FUNCTION__,
                                         "Could not delete `%s': %s",
                                         tmpname, TXstrerror(TXgeterror()));
		}
	}
ok:
	ret = 0;
	goto finally;

err:
	ret = -1;
finally:
	return(ret);
}

#endif	/* !USE_NTSHM */
#endif	/* !USE_POSIX_SEM */
#endif /* HAVE_SEM */
#endif /* !NOLOCK */
