/* -=- kai-mode: John -=- */
#ifndef OLD_LOCKING
#include "txcoreconfig.h"
#if defined(FORCENOLOCK) && !defined(NOLOCK)
#  define NOLOCK		/* MAW 08-03-95 - config.h turns if off */
#endif /* FORCENOLOCK && !NOLOCK */
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#ifdef EPI_HAVE_IO_H
#  include <io.h>
#endif
#ifdef EPI_HAVE_PROCESS_H
#  include <process.h>
#endif
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#include <sys/types.h>
#ifdef EPI_HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include "os.h"
#include "dbquery.h"		/* MAW 02-16-94 - instead of dblock.h */
#include "texint.h"
#ifdef HAVE_MMAP
#  include <sys/mman.h>
#endif /* HAVE_MMAP */
/*#include "dblock.h"*/
#ifndef USE_VOLATILE
#  define USE_VOLATILE
#endif /* USE_VOLATIVE */
#include "mmsg.h"


static const char	Ques[] = "?";

/* use TXsleepmsec() to avoid potential TXsetalarm() interference: */
#define SLEEP(pmbuf, sec)	\
	TXsleepForLocks(pmbuf, (sec)*1000, __FUNCTION__, __LINE__)

void
TXsleepForLocks(TXPMBUF *pmbuf, int msec, const char *func, int line)
{
	long	msUnslept, msSlept;

	if (TXgetlockverbose() & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, func,
			       "sleeping for %d.%03d seconds at line %d",
			       msec/1000, msec % 1000, line);
        msUnslept = TXsleepmsec(msec, 0);
	if (TXgetlockverbose() & 0x20)
	{
		msSlept = (msec - msUnslept);
		txpmbuf_putmsg(pmbuf, MINFO, func,
			       "slept for %d.%03d seconds at line %d",
			       (int)(msSlept / 1000L),
			       (int)(msSlept - 1000L*(msSlept / 1000L)), line);
	}
}

#if defined(FORCENOLOCK) && !defined(NOLOCK)
#  define NOLOCK		/* MAW 08-03-95 - config.h turns if off */
#endif /* FORCENOLOCK && !NOLOCK */
#ifdef NOLOCK
#  define semlock(a)	0
#  define semunlock(p, a)	0
#else /* !NOLOCK */
/* SHM abstraction macros doc: these are specific to this file, not generic
                               more work could prob. make a generic abstraction
   CRSHM() create new shm handle
   OPSHM() open existing shm handle
   ATSHM() make pointer to IDBLOCK from shm handle
   DISHM() discard partially setup stuff
   CLSHM() cleanup unneeded stuff
   DTSHM() detach from ATSHM() pointer
   RMSHM() delete shm handle
*/
#  ifdef USE_NTSHM
#    define CRSHM(a) createOrOpenShm(a, TXbool_True)
#    define OPSHM(a, create) createOrOpenShm(a, create)
#    define ATSHM(a) atshm(a)
#    define DISHM(a,b)
#    define CLSHM(a,b)
#    define DTSHM(a) UnmapViewOfFile(a)
#    define RMSHM(a)		/* No need for this in NT */

      /****************************************************************/
static TXLOCKOBJHANDLE
createOrOpenShm(char *path, TXbool create)
{
	char *p;
	HANDLE rc;

	for (p = path; *p; p++)
	{
		if (TX_ISPATHSEP(*p))
			*p = '_';
		else
			*p = toupper(*p);	/* WTF Makes DBName case insensitive */
	}

	if (create)
	    rc =
		TXCreateFileMapping(INVALID_HANDLE_VALUE, PAGE_READWRITE, 0,
				    sizeof(IDBLOCK), path,
				    (TX_LOCKVERBOSELEVEL() >= 1));
	else
	    rc =
		TXOpenFileMapping(path, (TX_LOCKVERBOSELEVEL() >= 1));
	if (TX_LOCKVERBOSELEVEL() >= 1 && rc != NULL)
	{
		TX_PUSHERROR();
		putmsg(MINFO, __FUNCTION__,
			"%s file mapping for `%s' with handle %p",
		       (!create || TXgeterror() == ERROR_ALREADY_EXISTS ?
			"Opened existing" : "Created"), path, (void *)rc);
		TX_POPERROR();
	}
	return(rc == NULL ? TXLOCKOBJHANDLE_INVALID_VALUE : rc);
}				/* end createOrOpenShm() */

static void *atshm ARGS((HANDLE h));
static void *
atshm(h)
HANDLE h;
{
	void *v;

	v = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, sizeof(IDBLOCK));
	if (!v)
		return (HANDLE) - 1;
	else
		return v;
}

      /****************************************************************/
#  else /* !USE_NTSHM */
#    ifdef USE_SHM		/* MAW 02-10-94 - add shared mem support */
key_t
TEXISLOCKSKEY(a)
char *a;
/* Returns -1 on error.
 */
{
	EPI_STAT_S stb;
	int rc;

	rc = EPI_STAT(a, &stb);
	if (rc != -1)
		return stb.st_ino;
	return -1;
}

#      ifdef hpux
#        define ATTACHONLYONCE
#      endif /* hpux */
#      include <sys/shm.h>
#      define MYSHM_RW (0666)	/* accessible by all */
#      define MYSHM_RW_STR	"0666"

static int
TXshmget(TXPMBUF *pmbuf, const char *func, const char *path, TXbool create,
	 TXbool exclusive)
 /* Returns -1 on error.
  */
{
	key_t	key;
	int	ret;

	key = TEXISLOCKSKEY((char *)path);
	if (key == (key_t)(-1))
	{
		txpmbuf_putmsg(pmbuf, MERR + FTE, __FUNCTION__,
		      "Cannot determine shared mem key for database `%s': %s",
			       path, TXstrerror(TXgeterror()));
		goto err;
	}
	if (TXgetlockverbose() & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, func,
	    "shmget(key 0x%wx, size %d, flags %s%s" MYSHM_RW_STR ") starting",
			       (EPI_HUGEINT)key, (int)sizeof(IDBLOCK),
			       (create ? "IPC_CREAT | " : ""),
			       (exclusive ? "IPC_EXCL | " : ""));
	ret = shmget(key, sizeof(IDBLOCK),
		     ((create ? IPC_CREAT : 0) | (exclusive ? IPC_EXCL : 0) |
		      MYSHM_RW));
	if (TXgetlockverbose() & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, func,
			       "shmget(key 0x%wx, size %d, flags %s%s" MYSHM_RW_STR ") returned id %d err %d=%s",
			       (EPI_HUGEINT)key, (int)sizeof(IDBLOCK),
			       (create ? "IPC_CREAT | " : ""),
			       (exclusive ? "IPC_EXCL | " : ""),
			       ret, (int)saveErr,
			       TXgetOsErrName(saveErr, Ques));
		TX_POPERROR();
	}
	goto finally;

err:
	ret = -1;
finally:
	return(ret);
}

static int crshm ARGS((char *path));
static int
crshm(path)
char	*path;
{
	int	rc;
	TXPMBUF	*pmbuf = TXPMBUFPN;

	rc = TXshmget(pmbuf, __FUNCTION__, path, TXbool_True /* create */,
		      TXbool_True /* exclusive */);
	if (TX_LOCKVERBOSELEVEL() >= 1 && rc != -1)
	{
		TX_PUSHERROR();
		putmsg(MINFO, __FUNCTION__,
			"Created shared mem segment for `%s' with id %d",
			path, (int)rc);
		TX_POPERROR();
	}
	return(rc);
}

#      define CRSHM(a) crshm(a)
#      define OPSHM(a, create)						\
  TXshmget(TXPMBUFPN, __FUNCTION__, a, (create), TXbool_False /* !exclusive */)
#      ifdef ATTACHONLYONCE	/* MAW 11-21-95 */
static int nattach = 0;
static void *memattach = (void *) NULL;

# error add TXgetlockverbose() tracing here
#        ifdef NEVER /* MAW 03-11-02 */
#          define ATSHM(a) ((memattach==(void *)NULL?(memattach=shmat((a),(void *)NULL,MYSHM_RW)):memattach)==(void *)NULL?(void *)NULL:(nattach++,memattach))
#          define DTSHM(a) ((--nattach==0)?(memattach=(void *)NULL,shmdt((void *)a)):0)
#        else /* !NEVER */
/* MAW 03-11-02 - need mult. different shm attachments (multiple open db's) */
#          define ATSHM(a) myatshm(a)
#          define DTSHM(a) mydtshm(a)
#          define MYMAXSHM 10
         static struct {
            int   id;    /* shm id */
            void *addr;  /* shm address */
            int   nuse;  /* number of uses */
         } myshminfo[MYMAXSHM];
         static int   myusedshm=0;/* how many myshminfo[] currently in use (including holes) */
         static void *myatshm(int id){        /* attach to shared mem */
            int i, inc=0, avail=(-1);
            for(i=0;i<myusedshm;i++){          /* only scan used ones */
               if(myshminfo[i].nuse==0){                      /* hole */
                  if(avail==(-1))                        /* first one */
                    avail=i;    /* remember for potential later reuse */
               }else if(myshminfo[i].id==id){       /* already exists */
                  myshminfo[i].nuse++;              /* bump use count */
                  return(myshminfo[i].addr);  /* give old val to user */
               }
            }
            if(avail!=(-1))                       /* a hole was found */
               i=avail;                                     /* use it */
            else if(i>=MYMAXSHM){               /* all full up -- eek */
               errno=EMFILE;
               return((void *)NULL);              /* cannot do no more */
            }else{
               inc=1;
            }
            /* request shm from OS */
            if((myshminfo[i].addr=shmat(id,(void *)NULL,MYSHM_RW))==(void *)NULL)
               return((void *)NULL);         /* OS failure, tell user */
            myshminfo[i].id=id;                        /* remember id */
            myshminfo[i].nuse=1;                     /* set use count */
            myusedshm+=inc;
            return(myshminfo[i].addr);        /* give new val to user */
         }
         static int mydtshm(void *addr){    /* detach from shared mem */
            int i, rc=(-1);
            errno=EINVAL;
            for(i=0;i<myusedshm;i++){          /* only scan used ones */
               if(myshminfo[i].addr==addr){                  /* found */
                  if(myshminfo[i].nuse>1){      /* others still using */
                     myshminfo[i].nuse-=1;           /* dec use count */
                     rc=0;
                  }else if(myshminfo[i].nuse>0){         /* last user */
                     myshminfo[i].id=0;             /* clear old vals */
                     myshminfo[i].addr=(void *)NULL;
                     myshminfo[i].nuse=0;          /* clear use count */
                     rc=shmdt(addr);         /* tell OS to delete shm */
                     if(i==myusedshm-1)     /* if last in in-use list */
                       myusedshm--;            /* shrink in-use count */
                  }
                  break;                             /* stop scanning */
               }
            }
            return(rc);
         }
#        endif /* !NEVER */
#      else /* !ATTACHONLYONCE */

static void *
TXshmat(TXPMBUF *pmbuf, const char *func, int shmid)
{
	void	*ret;

#        ifdef MEM_PROTECT
# 	   error change MYSHM_RW to SHM_RDONLY
#        endif

	if (TXgetlockverbose() & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, func,
		   "shmat(id %d, addr NULL, flags " MYSHM_RW_STR ") starting",
			       shmid);
	ret = shmat(shmid, NULL, MYSHM_RW);
	if (TXgetlockverbose() & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, func,
			       "shmat(id %d, addr NULL, flags " MYSHM_RW_STR ") returned address %p err %d=%s",
			       shmid, ret, (int)saveErr,
			       TXgetOsErrName(saveErr, Ques));
		TX_POPERROR();
	}
	return(ret);
}

static int
TXshmdt(TXPMBUF *pmbuf, const char *func, const void *addr)
{
	int	ret;

	if (TXgetlockverbose() & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, func,
			"shmdt(addr %p) starting", addr);
	TXclearError();			/* Linux may not clear it on success*/
	ret = shmdt(addr);
	if (TXgetlockverbose() & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, func,
			       "shmdt(addr %p) returned %d=%s err %d=%s",
			       addr, ret, (ret == 0 ? "Ok" : "err"),
			       (int)saveErr, TXgetOsErrName(saveErr, Ques));
		TX_POPERROR();
	}
	return(ret);
}

static int
TXshmctl(TXPMBUF *pmbuf, const char *func, int shmid, int cmd,
	struct shmid_ds *buf)
{
	int	ret;

	if (TXgetlockverbose() & 0x10)
		txpmbuf_putmsg(pmbuf, MINFO, func,
			       "shmctl(id %d, cmd %d=%s, buf %p) starting",
			       shmid, cmd,
			       (cmd == IPC_RMID ? "IPC_RMID" : Ques), buf);
	ret = shmctl(shmid, cmd, buf);
	if (TXgetlockverbose() & 0x20)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MINFO, func,
		     "shmctl(id %d, cmd %d=%s, buf %p) returned %d err %d=%s",
			       shmid, cmd,
			       (cmd == IPC_RMID ? "IPC_RMID" : Ques), buf,
			       ret, (int)saveErr,
			       TXgetOsErrName(saveErr, Ques));
		TX_POPERROR();
	}
	return(ret);
}

#        define ATSHM(a) TXshmat(TXPMBUFPN, __FUNCTION__, (a))
#        define DTSHM(a) TXshmdt(TXPMBUFPN, __FUNCTION__, (void *)(a))
#      endif /* !ATTACHONLYONCE */
#      define DISHM(a,b) TXshmctl(TXPMBUFPN, __FUNCTION__, (a), IPC_RMID, NULL)
#      define CLSHM(a,b) ((a)->shmid=(b))
#      define RMSHM(a)   TXshmctl(TXPMBUFPN, __FUNCTION__, (a)->shmid, IPC_RMID, NULL)
#    else /* !USE_SHM */
#  error add TXgetlockverbose() tracing here
#      define CRSHM(a) crshm(a)
#      define OPSHM(a, create) open((a),(O_RDWR|((create) ? O_CREAT : 0),0666)
#      define ATSHM(a) mmap((void *)0, sizeof(IDBLOCK), PROT_READ|PROT_WRITE, MAP_SHARED, (a), 0)
#      define DISHM(a,b) close(a),unlink(b)
#      define CLSHM(a,b) close(b)
#      define DTSHM(a) munmap((void *)(a),sizeof(IDBLOCK))
#      define RMSHM(a) unlink((a)->path)
      /****************************************************************/
static int crshm ARGS((char *path));
static int
crshm(path)
char *path;
{
	int fd;
	size_t	tot, wr;
	IDBLOCK	temp;

	fd = open(path, (O_RDWR | O_CREAT | O_EXCL), 0666);

	if (fd != (-1))			/* success */
	{
		if (chmod(path, 0666) != 0) goto err;
		/* Write nuls out to file.  Don't just seek to desired EOF
		 * and write 1 nul: leaves holes, mmap() will segfault:
		 */
		memset(&temp, 0, sizeof(IDBLOCK));
		for (tot = 0; tot < sizeof(IDBLOCK); tot += wr)
		{			/* repeat for partial writes */
			do		/* repeat for EINTR */
			{
				errno = 0;
				wr = (size_t)write(fd, ((char *)&temp) + tot,
						   sizeof(IDBLOCK) - tot);
			}
			while (wr == (size_t)(-1) && errno == EINTR);
			if (wr == (size_t)(-1) || wr == 0) goto err;
		}
	}
	if (TX_LOCKVERBOSELEVEL() >= 1 && fd != -1)
	{
		TX_PUSHERROR();
		putmsg(MINFO, __FUNCTION__,
			"Created file `%s' with descriptor %d",
			path, (int)fd);
		TX_POPERROR();
	}
	return (fd);
err:
	TX_PUSHERROR();
	close(fd);
	unlink(path);			/* clean up (safe: we are creator) */
	TX_POPERROR();
	return(-1);
}				/* end crshm() */

      /****************************************************************/
#    endif /* !USE_SHM */
#  endif /* !USE_NTSHM */
#endif /* !NOLOCK */

/******************************************************************/

/* MAX Length 11 */
#ifndef NEVER
static char lockver[] = "03.01.1423";
#else
static char lockver[] = "01.04.2016";
#endif

/******************************************************************/

ft_counter ccounter ARGS((DBLOCK *));

#if defined(__sgi) && defined(NEVER) /* In unistd.h now */
int sginap ARGS((long));
#endif

/******************************************************************/

#ifndef NOLOCK			/* MAW 08-03-95 */
static int mkseq ARGS((char *, char *));

static int
mkseq(inpath, outpath)
char *inpath;
char *outpath;
{
#  ifdef USE_NTSHM
	char *a, *b;

	for (a = inpath, b = outpath; *a; a++, b++)
	{
		if (*a == '_')
			*b = PATH_SEP;
		else
			*b = *a;
	}
	*b = *a;
	strcat(outpath, ".SEQ");
	return 0;
#  else /* !USE_NTSHM */
	strcpy(outpath, inpath);
#    ifdef USE_SHM
	strcat(outpath, PATH_SEP_S "SYSLOCKS");
#    endif /* USE_SHM */
	strcat(outpath, ".SEQ");
	return 0;
#  endif /* !USE_NTSHM */
}
#endif /* !NOLOCK */

/******************************************************************/

int
TXinitshared(rc)
IDBLOCK *rc;
{
	int i;

#ifndef NEVER
	for (i = 0; i < NSERVERS; i++)
	{
		rc->servers[i].pid = 0;
		rc->servers[i].hlock = -1;
		rc->servers[i].tlock = -1;
	}

	/* For each table, check status, lock chain, times */

	for (i = 0; i < LTABLES; i++)
	{
		memset(&rc->tblock[i], 0, sizeof(LTABLE));
		rc->tblock[i].hlock = -1;
		rc->tblock[i].tlock = -1;
		rc->tblock[i].usage = 0;
	}

	for (i = 0; i < MAX_LOCKS; i++)
	{
		memset(&rc->locks[i], 0, sizeof(LOCK));
		rc->locks[i].nsl = -1;
		rc->locks[i].psl = -1;
		rc->locks[i].ntl = -1;
		rc->locks[i].ptl = -1;
		rc->locks[i].tbl = -1;
	}
#endif
	return 0;
}

/******************************************************************/

/* ------------------------------------------------------------------------ */

static IDBLOCK *
TXungetLockObject(TXPMBUF *pmbuf, IDBLOCK *idbl, TXLOCKOBJHANDLE *handle)
{
	(void)pmbuf;
	if (!idbl) goto finally;

#ifndef NOLOCK
	DTSHM(idbl);
#endif /* !NOLOCK */
#ifdef USE_NTSHM
	CloseHandle(*handle);
#endif /* USE_NTSHM */
finally:
	if (handle) *handle = TXLOCKOBJHANDLE_INVALID_VALUE;
	return(NULL);
}

DBLOCK *
ungetshared(pmbuf, l, readOnly)
TXPMBUF *pmbuf; /* (in, opt.) for messages */
DBLOCK *l;
TXbool	readOnly;	/* no shm/sem create nor locks mods (dumplock) */
{
	int i = -1;
	int fd;
	char rpath[PATH_MAX];
	char spath[PATH_MAX];
	USE_VOLATILE IDBLOCK *id;
	IDBLOCK temp;

	if (!l)
		return l;
	id = l->idbl;
#ifdef NOLOCK
	l = TXfree(l);
#else /* !NOLOCK */
	if (readOnly)
	{
		i = id->nservers;
		goto detach;
	}

	if (semlock(pmbuf, l, TXbool_True) == -1)
	{
#  ifdef DEVEL
		txpmbuf_putmsg(pmbuf, 999, NULL, "Semlock failed");
#  endif
	}
	i = id->nservers;
	memcpy(&temp, (void *) id, sizeof temp);

	/* Write `id->lcount' to SYSLOCKS.SEQ: */
	TXstrncpy(rpath, id->path, sizeof(rpath));
	mkseq(rpath, spath);		/* spath = db/SYSLOCKS.SEQ */
	if ((fd = open(spath, O_RDWR | O_CREAT, 0666)) != -1)
	{
		chmod(spath, 0666);
		write(fd, (void *) &(id->lcount), sizeof(id->lcount));
		close(fd);
	}

#  ifdef REMOVE_SYSLOCKS_STRUCTURE
	if (i == 0)
		RMSHM(id);
#  else
	if (i == 0)
	{
		TXinitshared(id);
	}
#  endif
	id->curpid = 0;
#  if defined(USE_POSIX_SEM)
	if (semunlock(pmbuf, l) == -1)
	{
#    ifdef DEVEL
		txpmbuf_putmsg(pmbuf, 999, NULL, "Semunlock failed");
#    endif /* DEVEL */
	}
#  endif /* USE_POSIX_SEM */

detach:
	l->idbl = TXungetLockObject(pmbuf, id, &l->hFileMap);
	if (readOnly) goto finally;

	l->idbl = &temp;
#  if !defined(USE_POSIX_SEM)
	if (semunlock(pmbuf, l) == -1)
	{
#    ifdef DEVEL
		txpmbuf_putmsg(pmbuf, 999, NULL, "Semunlock failed");
#    endif /* DEVEL */
	}
#  endif /* USE_POSIX_SEM */
	if (i == 0)
	{
		delmutex(pmbuf, l);
	}
	else if (TX_LOCKVERBOSELEVEL() >= 2)
		txpmbuf_putmsg(pmbuf, MINFO, CHARPN,
			"Will not delete mutex: %d servers attached", i);
#endif /* !NOLOCK */
finally:
	return (DBLOCK *) NULL;
}

/* ------------------------------------------------------------------------ */

static IDBLOCK *
TXgetLockObject(TXPMBUF *pmbuf, char *rpath, size_t rpathSz, TXbool create,
		int *amCreator, TXLOCKOBJHANDLE *handle)
/* Opens existing, or creates (if `create') lock file/mem/mapping
 * object.  Internal use.  Sets `*amCreator' nonzero if lock object
 * created.  Sets `*handle'.  May modify `rpath', e.g. append
 * "/SYSLOCKS".  NOTE: `rpath' is assumed to be PATH_MAX in size, and
 * absolute'd.
 * Returns NULL on error.
 */
{
	static const char	objDesc[] =
#  ifdef USE_SHM
			       "shared mem"
#  elif defined(USE_NTSHM)
			       "SYSLOCKS file mapping"
#  else /* !USE_SHM && !USE_NTSHM */
			       "SYSLOCKS"
#  endif /* !USE_SHM && !USE_NTSHM */
		;
#ifdef _WIN32
#  define STRSYSERR()	\
	(errno ? strerror(errno) : TXstrerror(TXgeterror()))
#else /* !_WIN32 */
#  define STRSYSERR()	strerror(errno)
#endif /* !_WIN32 */
	IDBLOCK	*rc = NULL;
	int creator = 0;
	int ntries = 0;
	TXLOCKOBJHANDLE	fd = TXLOCKOBJHANDLE_INVALID_VALUE;
#ifdef USE_SHM
	(void)rpathSz;
#else /* !USE_SHM */
	char	*d;

	d = rpath + strlen(rpath);
	if (d + 1 + strlen(TEXISLOCKSFILE) >= rpath + rpathSz) goto tooLong;
	if (d > rpath && !TX_ISPATHSEP(d[-1]))
		*(d++) = PATH_SEP;
	strcpy(d, TEXISLOCKSFILE);
#endif /* !USE_SHM */

	/* Safety check for strcpy() in getshared(): */
	if (strlen(rpath) >= sizeof(rc->path))
	{
#ifndef USE_SHM
	tooLong:
#endif /* !USE_SHM */
		txpmbuf_putmsg(pmbuf, MERR + MAE, __FUNCTION__,
			       "Path too long");
		goto err;
	}

#ifndef USE_NTSHM
getexcl:
#endif /* !USE_NTSHM */
	TXclearError();
	if (!create)				/* e.g. dumplock */
	{
		fd = OPSHM(rpath, create);
		goto checkFdFinal;
	}
	fd = CRSHM(rpath);	/* try to create locks file */
#ifdef USE_NTSHM
	/* Windows CRSHM() is both create or open-existing */
	if (fd != NULL)
	{
		DWORD LastError;

		LastError = GetLastError();
		if (LastError == ERROR_ALREADY_EXISTS)
			creator = 0;
		else
			creator = 1;
	}
	else
		fd = TXLOCKOBJHANDLE_INVALID_VALUE;
#else /* !USE_NTSHM */
	if (fd != TXLOCKOBJHANDLE_INVALID_VALUE)
		creator = 1;
	else if (errno == EEXIST)
	{
		creator = 0;
		TXclearError();
		/* KNG not sure if `create' true is truly needed here,
		 * as CRSHM() (ours or another process') should have
		 * created it.  But that is historically what we do:
		 */
		fd = OPSHM(rpath, create);	/* open/create locks shm */
		if (fd == TXLOCKOBJHANDLE_INVALID_VALUE && errno == ENOENT)
		{
			/* Must have been deleted between our CRSHM()
			 * above and this OPSHM(); retry:
			 */
			goto getexcl;	/* Fix race condition */
		}
	}
	else
	{
		ntries++;
		/* KNG 20130425 fails on first try occasionally on m3;
		 * do not normally yap for first failure:
		 */
		if (ntries > 1 || TX_LOCKVERBOSELEVEL() >= 2)
			txpmbuf_putmsg(pmbuf, (ntries < 5 ? MWARN : MERR),
				       __FUNCTION__,
				       "Open %s failed: %s%s", objDesc,
			       STRSYSERR(),
			       (ntries < 5 ? "; trying again" : ""));
		if (ntries < 5)
		{
			SLEEP(pmbuf, ntries);
			goto getexcl;
		}
	}
#endif /* !USE_NTSHM */
checkFdFinal:
	if (fd == TXLOCKOBJHANDLE_INVALID_VALUE) /* MAW 02-08-94 - check for fail */
	{
		TXERRTYPE	errNum;

		errNum = TXgeterror();
		txpmbuf_putmsg(pmbuf, MERR + FOE, __FUNCTION__,
			       "Cannot open %s for database `%s': %s",
			       objDesc, rpath,
			       /* Avoid `No such file or directory' message;
				* might imply it referes to db path:
				*/
			       (TX_ERROR_IS_NO_SUCH_FILE(errNum) ?
				"Object does not exist" : TXstrerror(errNum)));
		goto err;
	}
	TXclearError();
	rc = (IDBLOCK *) ATSHM(fd);	/* create mem ptr from SHM */
	while (rc == (IDBLOCK *) - 1L)
	{  /* MAW 02-08-94 - putmsg() not perror(), close/ret not fall thru */
#ifdef USE_SHM
		txpmbuf_putmsg(pmbuf, MERR + FOE, __FUNCTION__,
			       "Cannot attach to shared mem: %s",
			       STRSYSERR());
#else /* !USE_SHM */
		if (errno == ENXIO)
		{
			/* Try once more, just to make sure */
			SLEEP(pmbuf, 2);/* Give the other guy a chance. */
			TXclearError();
			rc = (IDBLOCK *) ATSHM(fd);	/* Try again */
			if (rc == (IDBLOCK *) - 1L)
				txpmbuf_putmsg(pmbuf, MERR + FOE, __FUNCTION__,
					       "Old versions of Texis are running on this database.  Wait till they have finished.");
			else
				break;
		}
		else
			txpmbuf_putmsg(pmbuf, MERR + FOE, __FUNCTION__,
				       "Cannot"
#  ifdef USE_NTSHM
				       " MapViewOfFile"
#  else /* !USE_NTSHM */
				       " mmap"
#  endif /* !USE_NTSHM */
				       " lock structure: %s", STRSYSERR());
#endif /* !USE_SHM */
		if (!create) DISHM(fd, rpath);	/* discard halfway done stuff */
		fd = TXLOCKOBJHANDLE_INVALID_VALUE;
		goto err;
	}
	goto done;

err:
	rc = NULL;
done:
	if (amCreator) *amCreator = creator;
	if (handle) *handle = fd;
	return(rc);
#undef STRSYSERR
}

/******************************************************************/

IDBLOCK *
getshared(pmbuf, dbl, path, readOnly)
TXPMBUF	*pmbuf;
DBLOCK *dbl;
const char *path;
TXbool	readOnly;		/* for dumplock: no create shm/sem, no mods */
/* NOTE: `path' must be absolute/canonical, for consistent file mapping
 * under Windows etc.
 * NOTE: if `readOnly', caller should check `dbl->semid' in case no semaphore.
 */
{
	IDBLOCK *rc = NULL;
	char rpath[PATH_MAX];
	char spath[PATH_MAX];
	int creator = 0;
	TXLOCKOBJHANDLE	fd = TXLOCKOBJHANDLE_INVALID_VALUE;
	int fdseq;
	size_t i;

#ifdef NOLOCK
	rc = (IDBLOCK *) TXcalloc(pmbuf, __FUNCTION__, 1, sizeof(IDBLOCK));
	goto finally;
#else
getexcl:
	rpath[sizeof(rpath) - 1] = 'x';
	TXstrncpy(rpath, path, sizeof(rpath));	/* build the locks filename */
	if (rpath[sizeof(rpath) - 1] != 'x')	/* possible truncation */
	{
		txpmbuf_putmsg(pmbuf, MERR + MAE, __FUNCTION__,
			       "Path `%s' too long",
			       path);
		goto finally;
	}

	/* this may modify `rpath' e.g. append `/SYSLOCKS': */
	rc = TXgetLockObject(pmbuf, rpath, sizeof(rpath), !readOnly,
			     &creator, &fd);
	if (!rc) goto finally;

	dbl->idbl = rc;
#  if defined(NEVER) && defined(USE_POSIX_SEM)
	dbl->semid = &rc->semid;
#  endif /* USE_POSIX_SEM */
	if (creator)		/* create initial settings */
	{
		if (readOnly)
		{
			txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
				       "Internal error: somehow ended up creator of lock object without giving create flag");
			goto detach;
		}
		strcpy(rc->path, rpath);
		if (initmutex(pmbuf, dbl) == -1)
		{
			DISHM(fd, rpath);
			/* wtf shmdt too? */
			fd = TXLOCKOBJHANDLE_INVALID_VALUE;
			rc = NULL;
			goto finally;
		}
		TXinitshared(rc);
		strcpy(rc->verstr, lockver);
		mkseq(rpath, spath);
		if ((fdseq = open(spath, O_RDONLY, 0666)) != -1)
		{
			read(fdseq, &(rc->lcount), sizeof(rc->lcount));
			close(fdseq);
			fdseq = -1;
		}
	}
	else			/* wait for creator to fill it in */
	{
		do
		{
			if (*rc->verstr != '\0')
				break;
			SLEEP(pmbuf, 1);
		}
		while (0);
		if (strcmp(lockver, rc->verstr) != 0)
		{
			if (!readOnly)
			{
			    for (i = 0; i < strlen(lockver); i++)
				if (rc->verstr[i] < '.'
				    || rc->verstr[i] > ':')
				{
					txpmbuf_putmsg(pmbuf, MERR,
						       __FUNCTION__,
				    "Fixing lock structure for database `%s'",
						       path);
					strcpy(rc->verstr, lockver);
					RMSHM(rc);
					DTSHM(rc);
					rc = NULL;
					/* WTF should this be closed?: */
					fd = TXLOCKOBJHANDLE_INVALID_VALUE;
					goto getexcl;
				}
			}
			txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
	   "A%s version of Texis locks (%.*s) is accessing the database `%s'",
			       strcmp(lockver,
				      rc->verstr) > 0 ? "n older" : " newer",
				       (int)sizeof(rc->verstr), rc->verstr,
				       rpath);
			goto detach;
		}
		if (rc->nservers == 0 || strcmp(rc->path, rpath) != 0)
		{
			if (!readOnly) strcpy(rc->path, rpath);
		}
		/* If `readOnly' (e.g. dumplock), this may fail if sem
		 * does not exist.  Make a note and let dumplock continue:
		 */
		if (openmutex(pmbuf, dbl, !readOnly) == -1)
		{
			if (readOnly)
			{
				dbl->semid = (TXSEMID)(-1);/* note for caller */
				goto finally;
			}
			else
				goto detach;
		}
	}
#  ifndef USE_NTSHM
#    ifndef USE_POSIX_SEM
	dbl->semid = rc->semid;
#    endif
#  else /* USE_NTSHM */
	dbl->hFileMap = fd;
#  endif
	if (!readOnly) CLSHM(rc, fd);		/* cleanup */

	if (semlock(pmbuf, dbl, !readOnly) == -1)
	{
#  ifdef DEVEL
		txpmbuf_putmsg(pmbuf, 999, NULL, "Semlock failed");
#  endif
		goto detach;
	}
	if (semunlock(pmbuf, dbl) == -1)
	{
#  ifdef DEVEL
		txpmbuf_putmsg(pmbuf, 999, NULL, "Semunlock failed");
#  endif
	detach:
		DTSHM(rc);
		rc = NULL;
	}
#endif /* NOLOCK */
finally:
	return rc;
}

/******************************************************************/

TXEXIT
TXdumpshared(TXPMBUF *pmbuf, const char *database, const char *outPath)
{
	IDBLOCK *rc = NULL;
	char rpath[PATH_MAX];
	int creator = 0, outFd = -1;
	TXEXIT	ret = TXEXIT_UNKNOWNERROR;
	TXLOCKOBJHANDLE	fd = TXLOCKOBJHANDLE_INVALID_VALUE;

	/* Open output file: */
	if (strcmp(outPath, "-") == 0)
	{
		outFd = STDOUT_FILENO;
		outPath = NULL;
	}
	else if ((outFd = TXrawOpen(pmbuf, __FUNCTION__, "output file",
				    outPath, TXrawOpenFlag_None,
				    (O_RDWR | TX_O_BINARY | O_CREAT | O_TRUNC),
				    0666)) < 0)
	{
		ret = TXEXIT_CANNOTOPENOUTPUTFILE;
		goto err;
	}

	/* Attach to shared mem: */
	rpath[sizeof(rpath) - 1] = 'x';
	TXstrncpy(rpath, database, sizeof(rpath));/* build the locks filename */
	if (rpath[sizeof(rpath) - 1] != 'x')
	{
		putmsg(MERR + MAE, __FUNCTION__, "Database path too long");
		ret = TXEXIT_DBOPENFAILED;
		goto err;
	}
	rc = TXgetLockObject(pmbuf, rpath, sizeof(rpath),
			     TXbool_False /* !create */, &creator, &fd);
	if (!rc)
	{
		ret = TXEXIT_LOCKOPENFAILED;
		goto err;
	}

	/* Save the shared mem: */
	if (tx_rawwrite(pmbuf, outFd, (outPath ? outPath : "stdout"),
			!outPath /* pathIsDescription */, (byte *)rc,
			sizeof(IDBLOCK), TXbool_False /* !inSig */)
	    != sizeof(IDBLOCK))
	{
		ret = TXEXIT_CANNOTWRITETOFILE;
		goto err;
	}
	ret = TXEXIT_OK;
	goto finally;

err:
	if (ret == TXEXIT_OK) ret = TXEXIT_UNKNOWNERROR;
finally:
	if (outFd >= 0)
	{
		if (outPath) close(outFd);
		outFd = -1;
	}
	rc = TXungetLockObject(pmbuf, rc, &fd);
	return(ret);
}

/******************************************************************/

ft_counter *
getcounter(ddic)
DDIC *ddic;
/* Returns NULL on error, else a malloc'd new unique counter.
 */
{
	static CONST char	fn[] = "getcounter";
	ft_counter *rc;

	if ((rc = (ft_counter *) TXcalloc(TXPMBUFPN, fn, 1, sizeof(ft_counter))) == NULL)
		return(NULL);
	else if (rgetcounter(ddic, rc, 1) <= -2)	/* complete failure */
		rc = TXfree(rc);
	return rc;
}

/******************************************************************/
int
rgetcounter(ddic, rc, lock)
DDIC *ddic;	/* (in/out) whose `dblock' structure to use */
ft_counter *rc;	/* (out) new counter */
int lock;	/* (in) nonzero: use semlock()  zero: singleuser mode */
/* Generates a new counter from `ddic->dblock' and returns it in `*rc'.
 * Returns 0 on success, -1 on partial failure (`*rc' still set but may be
 * somewhat incorrect), -2 on complete failure.
 */
{
	static ft_counter	lcount = { 0L, 0L };
	ft_counter *l, b4;
	int	res = 0;
	USE_VOLATILE DBLOCK *dblck;

	dblck = ddic->dblock;

	/* System call time() could consume some wall clock time or swap us
	 * out, so call it outside locks to minimize in-locks wall-clock time.
	 * Might get a slightly stale return value relative to shmem `lcount',
	 * but that's ok due to `rc->date <= l->date' check below:
	 */
	rc->date = time(NULL);

	/* Determine which lock counter to use, and get lock if needed: */
	if (!dblck) goto usestatic;		/* no locks (singleuser?) */
	if (lock && semlock(ddic->pmbuf, dblck, TXbool_True) == -1)
	{					/* lock failed */
		/* Lock failed, so do not try to update shmem counter to
		 * avoid potential corruption.  But at least read it if
		 * it's newer, so we might get close to the right value;
		 * then update to our static counter:
		 */
		b4 = dblck->idbl->lcount;
		if (b4.date > lcount.date ||
		    (b4.date == lcount.date && b4.seq > lcount.seq))
			lcount = b4;		/* copy shmem to static */
	usestatic:
		if (lock) res = -1;		/* wanted locks: partial err*/
		lock = 0;			/* do not use locks */
		l = &lcount;			/* use static counter */
	}
	else l = &dblck->idbl->lcount;		/* use global shmem counter */

	/* Now generate a new counter, and possibly update lock counter:
	 * JMT - 1997-06-20 - Fixed counters to handle time being set
	 *                  back and still guarantee uniqueness.  Time
	 *                  is set to the largest time ever seen.
	 */
	b4 = *l;				/* after lock, before mods */
	if (rc->date <= l->date)
	{
		l->seq++;
		rc->seq = l->seq;
		rc->date = l->date;
	}
	else
	{
		l->seq = 0;
		l->date = rc->date;
		rc->seq = 0;
	}

	/* Log the action if defined and enabled, while still locked: */
	if (!TXTRACELOCKS_IS_OFF())
	{
		CONST char	*act = "SUNLrgetcounter";/* SingleUser NoLock */
		if (dblck) act += 2;
		if (lock) act += 2;
		tx_loglockop(ddic, act, 0, NULL, NULL, &b4, l, &res);
	}

	/* Unlock and return: */
	if (lock)
		semunlock(ddic->pmbuf, dblck);
	return res;
}

/******************************************************************/

static int sleepparams[SLEEP_SIZEOFLIST] = {
	 20000,	/* SLEEP_MULTIPLIER */
	     1,	/* SLEEP_METHOD */
	 10000,	/* SLEEP_INCREMENT */
	 20000,	/* SLEEP_DECREMENT */
	100000,	/* SLEEP_MAXSLEEP */
	     1,	/* SLEEP_BACKOFF */
	     1	/* SLEEP_SLEEPTYPE */
};

#if 0
static int numcpus = 1;		/* When to start reducing sleep */
static int sleeptype = 1;	/* Factors involved */
#endif /* 0 */

/******************************************************************/

int
TXsetsleepparam(unsigned int param, int value)
{
	if (param < SLEEP_SIZEOFLIST)
	{
		sleepparams[param] = value;
		return 0;
	}
	return -1;
}

/******************************************************************/

int
TXsandman()
{
	sleepparams[SLEEP_MULTIPLIER] += sleepparams[SLEEP_INCREMENT];
	return sleepparams[SLEEP_MULTIPLIER];
}

/******************************************************************/

int
TXrooster()
{
	if (sleepparams[SLEEP_MULTIPLIER] > sleepparams[SLEEP_DECREMENT])
		sleepparams[SLEEP_MULTIPLIER] -= sleepparams[SLEEP_DECREMENT];
	else
		sleepparams[SLEEP_MULTIPLIER] = 1;
	return sleepparams[SLEEP_MULTIPLIER];
}

/******************************************************************/

/*
 * Sleep for (SLEEP_MULTIPLIER * n) milliseconds
 * max SLEEP_MAXSLEEP milliseconds
 */

int
TXshortsleep(n)
int n;
{
	struct timeval tv;
	int st;

	st = sleepparams[SLEEP_MULTIPLIER] * n;
	if (st > sleepparams[SLEEP_MAXSLEEP])
		st = sleepparams[SLEEP_MAXSLEEP];
#ifdef NEW_WAIT_LOCK
	if (n < sleepparams[SLEEP_BACKOFF])
	{
		st = st >> (sleepparams[SLEEP_BACKOFF] - n);
	}
#else
#endif
	if (st)
	{
		switch (sleepparams[SLEEP_METHOD])
		{
		case 1:
			TXsleepmsec(st/1000L, 0);
			break;
		case 0:
		default:
			tv.tv_sec = 0;
			tv.tv_usec = st;
			select(0, NULL, NULL, NULL, &tv);
			break;
		}
	}
	return 0;
}

/******************************************************************/
#endif /* OLD_LOCKING */
