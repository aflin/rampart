#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/time.h>
#ifdef __sgi
#include <bstring.h>
#endif
#ifdef _AIX
#  include <sys/select.h>
#endif
#ifdef EPI_HAVE_TIOCNOTTY
#  include <sys/ioctl.h>           /* for TIOCNOTTY */
#endif
#include "dbquery.h"
#include "texint.h"
#ifdef __sgi
extern int select  ARGS((int nfds, fd_set *readfds, fd_set *writefds,
	fd_set *exceptfds, struct timeval *timeout));
#endif


/* max file descriptor value possible to save: */
#define TX_MAX_SAVE_FD  1023
/* Note that we count on TxSaveFdBits being init'd to 0; C does that: */
static byte     TxSaveFdBits[(TX_MAX_SAVE_FD + 8)/8];


#if defined(EPI_HAVE_VFORK) && !defined(EPI_VFORK_BROKEN)
#  define VFORK vfork
#else
#  define VFORK fork
#endif

#ifdef NEED_GETDTABLESIZE_DECL
extern int getdtablesize ARGS((void));
#endif

#ifndef OLD_EXEC

typedef struct TXshell {
	int ifd, ofd;
	byte *result;
	size_t resultsz;
	size_t alloced;
} TXshell;

#endif
/******************************************************************/

int
TXregroup()                           /* disconnect from parent and tty */
/* Returns -1 on error, 0 on success.  Silent.
 */
{
  int   ret = 0;                                /* success */
#ifdef unix
   {
     /* setpgid() has more consistent args than setpgrp() */
#  ifdef EPI_HAVE_SETPGID
      if (setpgid(0, getpid()) == (-1)) ret = -1;
#  endif /* EPI_HAVE_SETPGID */
#  ifdef EPI_HAVE_TIOCNOTTY
      {
        int fd;
        if((fd=open("/dev/tty",O_RDWR))>=0)
          {
            ioctl(fd,TIOCNOTTY,0);           /* lose controlling terminal */
            close(fd);
          }
      }
#  endif /* EPI_HAVE_TIOCNOTTY */
   }
#endif /* unix */
   return(ret);
}                                                    /* end TXregroup() */
/**********************************************************************/

int
tx_savefd(fd)
int     fd;
/* Adds `fd' to list of open file descriptors to preserve at call of
 * TXclosedescriptors().
 * Returns 0 on error, 1 if ok.
 */
{
  if ((unsigned)fd > (unsigned)TX_MAX_SAVE_FD) return(0);
  TxSaveFdBits[((unsigned)fd)/8] |= (1 << ((unsigned)fd & 7));
  return(1);
}

int
tx_releasefd(fd)
int     fd;
/* Opposite of tx_savefd().
 * Returns 0 on error, 1 if ok.
 */
{
  if ((unsigned)fd > (unsigned)TX_MAX_SAVE_FD) return(0);
  TxSaveFdBits[((unsigned)fd)/8] &= ~(1 << ((unsigned)fd & 7));
  return(1);                                    /* success */
}

int
TXclosedescriptors(flags)
int     flags;  /* bit 0: close stdio  bit 1: close > stdio  bit 2: ign save*/
/* Closes (potentially) open file descriptors after fork(),
 * except those registered with tx_savefd().
 * Returns -1 on error, 0 if ok.
 */
{
#ifdef _WIN32
  return(0);                                    /* ok */
#else /* !_WIN32 */
  int           fd, n, i, ret = 0;
#  ifdef RLIMIT_NOFILE
  EPI_HUGEINT   soft, hard;

  if (TXgetrlimit(TXPMBUFPN, RLIMIT_NOFILE, &soft, &hard) != 1 ||
      soft > (EPI_HUGEINT)EPI_OS_INT_MAX)
    n = -1;
  else
    n = (int)soft;
#  else /* !RLIMIT_NOFILE */
  n = getdtablesize();
#  endif /* !RLIMIT_NOFILE */
  if (n < 0 || n > 1024) n = 1024;              /* sane guess/upper limit */

  if (flags & 0x2)                              /* close > stdio */
    for (fd = n - 1; fd > STDERR_FILENO; fd--)
      {
        if ((flags & 0x4) ||                    /* close saved fd's too */
            fd > TX_MAX_SAVE_FD ||              /* out of saved-fd range */
            !(TxSaveFdBits[fd/8] & (1 << (fd & 7))))    /* `fd' not saved */
          close(fd);                            /*may not be open: ign. ret */
      }
  if (flags & 0x1)                              /* close stdio */
    {
      i = open("/dev/null", O_RDWR, 0666);
      if (i != -1)
        {
          for (fd = STDERR_FILENO; fd >= 0; fd--)
            {
              if ((flags & 0x4) ||              /* close save fd's too */
                  fd > TX_MAX_SAVE_FD ||        /* out of saved-fd range */
                  !(TxSaveFdBits[fd/8] & (1 << (fd & 7)))) /* `fd' not saved */
                dup2(i, fd);
            }
          close(i);
        }
      else
        ret = -1;                               /* error */
    }
  return(ret);
#endif /* !_WIN32 */
}

/******************************************************************/

#ifdef PIPE_MAX
#if PIPE_MAX > 8192
#define BUF_INCR	8192
#else
#define BUF_INCR	PIPE_MAX
#endif
#else
#define BUF_INCR	1024
#endif

#ifndef OLD_EXEC

static int
dumpout (FLD *fld, TXshell *ex, int isBinaryShell)
{
	fd_set rb, wb, xb;
	char *p, *c;
	int nfd, ifd, ofd, ret;
	size_t nw, nr;
	size_t tow;

	if(!fld)
	{
		c = "";			/* Nothing to output */
		tow = 0;
	}
	else
	{
		if(isBinaryShell && (fld->type & DDTYPEBITS) == FTN_BYTE)
		{
			c = getfld(fld, &tow);
		}
		else
		{
			p = fldtostr(fld);
			c = p;
			tow = strlen (c);
		}
	}
	if (TXverbosity >= 2)
		putmsg(MINFO, CHARPN, "Writing %wd bytes to command: [%s]",
		       (EPI_HUGEINT)tow, c);
	FD_ZERO(&rb);
	FD_ZERO(&wb);
	FD_ZERO(&xb);
	ifd = ex->ifd;
	ofd = ex->ofd;
	if(ifd > ofd)
		nfd = ifd + 1;
	else
		nfd = ofd + 1;
	while(1)
	{
		FD_SET(ifd, &rb);
		FD_SET(ifd, &xb);
		while(isBinaryShell ? tow : (size_t)*c)
		{
			FD_SET(ifd, &xb);
			FD_SET(ofd, &wb);
			FD_SET(ofd, &xb);
			ret=select(nfd, &rb, &wb, &xb, (struct timeval *)NULL);
			if(ret < 1 || FD_ISSET(ifd, &xb) || FD_ISSET(ofd, &xb))
			{
				break;
			}
			if(FD_ISSET(ofd, &wb))
			{
				nw = write(ofd, c,
					(isBinaryShell ? tow : strlen(c)));
				if(nw == (size_t)(-1) || nw == 0)
					break;
				c += nw;
				tow -= nw;
			}
		}
		if(FD_ISSET(ifd, &rb))
		{
			/* KNG 20120118 Bug 4030: always nul-terminate (not
			 * just for varchar doshell), and make sure allocated:
			 */
			if(ex->alloced <= ex->resultsz + 1)
			{
				ex->alloced += BUF_INCR;
				if(ex->result)
					ex->result=(byte *)realloc(ex->result, ex->alloced);
				else
					ex->result=(byte *)malloc(ex->alloced);
				if(!ex->result)
				{
					break;
				}
			}
			do
			{
				nr = read(ifd, ex->result+ex->resultsz,
					ex->alloced - (ex->resultsz + 1));
			} while (nr == (size_t)-1 && errno == EINTR);
			if (nr != (size_t)(-1)) ex->resultsz += nr;
			ex->result[ex->resultsz] = '\0';	/* Bug 4030 */
			if(nr == (size_t)(-1) || nr == 0)
				break;
		}
	}
	return 0;
}

#else /* OLD_EXEC */

static int dumpout ARGS((FLD *, int));

static int
dumpout(fld, fd)
FLD	*fld;
int	fd;
{
	char	*p, *c, *end;
	int	wr;

	if(!fld)
		return 0;
	p = fldtostr(fld);
	c = p;
	end = c + strlen(c);
	while(c < end)
	{
		do
		{
			errno = 0;
			wr = write(fd, c, (size_t)(end - c));
		}
		while (wr == -1 && errno == EINTR);
		if (wr == -1 || wr == 0) break;	/* error */
		c += wr;
	}
	if (c < end)
		putmsg(MERR + FWE, "dumpout",
		   "Can't write %d bytes to subprocess: %s", strerror(errno));
	return 0;
}

/******************************************************************/

static char *readin ARGS((int));

static char *
readin(int fd)
{
	char	*p = NULL;
	size_t	cursz, freesp, totsz = 0;
	int	nr = 0;

	cursz = 0;
	do
	{
		totsz += BUF_INCR;
		if(p)
			p = (char *)realloc(p, totsz);
		else
			p = (char *)malloc(totsz);
		if(!p)
		{
			return p;
		}
		freesp = totsz - cursz - 1; /* Allow for terminator */
		while(freesp)
		{
			do
			{
				errno = 0;
				nr = read(fd, p+cursz, freesp);
			}
			while (nr == -1 && errno == EINTR);
			if(nr <= 0)
				break;
			cursz += nr;
			freesp -= nr;
		}
	} while(nr > 0);
	if(p)
		p[cursz] = '\0';
	return p;
}

#endif /* OLD_EXEC */
/******************************************************************/

int
doshell(cmd, arg1, arg2, arg3, arg4)
FLD	*cmd, *arg1, *arg2, *arg3, *arg4;
{
	int	rc, cid, inpipe[2], outpipe[2], stat;
	char	**av = NULL, *cmdstr, *nbuf;
	static	CONST char Fn[] = "doshell";
#ifndef OLD_EXEC
	TXshell *ex;
	int	flag;
#endif

	cmdstr = strdup(fldtostr(cmd));
	if (TXverbosity >= 1)
		putmsg(MINFO, Fn, "Running command: %s", cmdstr);
	av = TXcreateargv(cmdstr, NULL);
	if (pipe(inpipe) != 0 ||
            pipe(outpipe) != 0)
          {
            putmsg(MERR, __FUNCTION__, "Cannot create pipes for `%s': %s",
                   cmdstr, TXstrerror(TXgeterror()));
            goto bail1;
          }
	cid = VFORK();
	if(cid == -1)				/* error */
	{
		putmsg(MERR + MAE, Fn, "Cannot create subprocess for %s: %s",
			av[0], TXstrerror(TXgeterror()));
        bail1:
		free(cmdstr);
		free(av);
		return(FOP_ENOMEM);
	}
	if(!cid)			/* child */
	{
		rc = close(inpipe[1]);
		close(outpipe[0]);
		if(inpipe[0] != STDIN_FILENO)
		{
			dup2(inpipe[0], STDIN_FILENO);
			close(inpipe[0]);
		}
		if(outpipe[1] != STDOUT_FILENO)
		{
			dup2(outpipe[1], STDOUT_FILENO);
			close(outpipe[1]);
		}
		execvp(av[0], av);
		/* Note that if vfork() used, we're still in parent's mem.
		 * putmsg() should be ok, but use _exit() instead of exit():
		 */
		/* KNG 20060407 putmsg() is not ok; it is flushing buffers
		 * twice (in child and parent).  Might be worse w/vfork():
		 */
		/* putmsg(MWARN, Fn, strerror(errno)); */
		_exit(TXEXIT_CANNOTEXECSUBPROCESS);
	}
	else					/* parent */
	{
		rc = close(inpipe[0]);
		close(outpipe[1]);
#ifndef OLD_EXEC
		ex = (TXshell *)calloc(1, sizeof(TXshell));
		if(!ex)
		{
			putmsg(MWARN+MAE, "exec", strerror(ENOMEM));
			free(cmdstr);
			free(av);
			return -1;
		}

/* We don't care to delay for read while writing data.
   Just grab anything that is available. */
		flag = fcntl(outpipe[0], F_GETFL);
		if (flag != -1)
			fcntl(outpipe[0], F_SETFL, flag|O_NDELAY);

		ex->ifd = outpipe[0];
		ex->ofd = inpipe[1];
		dumpout(arg1, ex, 0);
		dumpout(arg2, ex, 0);
		dumpout(arg3, ex, 0);
		dumpout(arg4, ex, 0);
		ex->ofd = close(inpipe[1]);

/* Turn delay mode on again.  Want to make sure that we wait for all the
   output from the command. */

		fcntl(outpipe[0], F_SETFL, flag);

		dumpout(NULL, ex, 0);
		nbuf = (char *)ex->result;
		nbuf[ex->resultsz] = '\0';
		free(ex);
#else /* OLD_EXEC */
		dumpout(arg1, inpipe[1]);
		dumpout(arg2, inpipe[1]);
		dumpout(arg3, inpipe[1]);
		dumpout(arg4, inpipe[1]);
		rc = close(inpipe[1]);
		nbuf = readin(outpipe[0]);
#endif /* OLD_EXEC */
		close(outpipe[0]);
		TXsetresult(cmd, nbuf);
		do
			errno = 0;
		while ((rc = waitpid(cid, &stat, 0)) == -1 && errno == EINTR);
		if (rc == -1)			/* error */
		{
			/* ECHILD may mean Vortex sig handler got it;
			 * WTF use TXaddproc() etc.
			 */
			if (errno != ECHILD)
				putmsg(MWARN+EXE, Fn, "waitpid() failed: %s",
					TXstrerror(TXgeterror()));
		}
		else if (rc != cid)
			;			/* some other process wtf */
		else if (WIFEXITED(stat))
		{
			if (WEXITSTATUS(stat) != 0)
				putmsg(MWARN + EXE, Fn,
				"Process %s returned exit code %d",
					av[0], WEXITSTATUS(stat));
		}
		else if (WIFSIGNALED(stat))
			putmsg(MWARN + EXE, Fn,
				"Process %s received signal %d", av[0],
				WTERMSIG(stat));
		free(cmdstr);
		free(av);
		return 0;
	}
	return 0;
}

/******************************************************************/

#ifndef OLD_EXEC
int
dobshell(cmd, arg1, arg2, arg3, arg4)
FLD	*cmd, *arg1, *arg2, *arg3, *arg4;
{
	int	cid, inpipe[2], outpipe[2];
	char	**av = NULL, *cmdstr, *nbuf;
	static	CONST char Fn[] = "exec";
	TXshell *ex;
	int	flag;

	cmdstr = strdup(fldtostr(cmd));
	av = TXcreateargv(cmdstr, NULL);
	if (pipe(inpipe) != 0 ||
            pipe(outpipe) != 0)
          {
            putmsg(MERR, __FUNCTION__, "Cannot create pipes for `%s': %s",
                   cmdstr, TXstrerror(TXgeterror()));
            goto bail1;
          }
	cid = VFORK();
	if(cid == -1)
	{
		putmsg(MERR + MAE, Fn, "Cannot create subprocess for %s: %s",
			av[0], TXstrerror(TXgeterror()));
        bail1:
		free(cmdstr);
		free(av);
		return(FOP_ENOMEM);
	}
	if(!cid)			/* child */
	{
		close(inpipe[1]);
		close(outpipe[0]);
		if(inpipe[0] != STDIN_FILENO)
		{
			dup2(inpipe[0], STDIN_FILENO);
			close(inpipe[0]);
		}
		if(outpipe[1] != STDOUT_FILENO)
		{
			dup2(outpipe[1], STDOUT_FILENO);
			close(outpipe[1]);
		}
		execvp(av[0], av);
		/* Note that if vfork() used, we're still in parent's mem.
		 * putmsg() should be ok, but use _exit() instead of exit():
		 */
		/* KNG 20060407 not ok; double-output of buffers: */
		/* putmsg(MWARN, Fn, strerror(errno)); */
		_exit(TXEXIT_CANNOTEXECSUBPROCESS);
	}
	else
	{
		if(cmdstr)
			free(cmdstr);
		if(av)
			free(av);
		close(inpipe[0]);
		close(outpipe[1]);
		ex = (TXshell *)calloc(1, sizeof(TXshell));

/* We don't care to delay for read while writing data.
   Just grab anything that is available. */
		flag = fcntl(outpipe[0], F_GETFL);
		if (flag != -1)
			fcntl(outpipe[0], F_SETFL, flag|O_NDELAY);

		ex->ifd = outpipe[0];
		ex->ofd = inpipe[1];
		dumpout(arg1, ex, 1);
		dumpout(arg2, ex, 1);
		dumpout(arg3, ex, 1);
		dumpout(arg4, ex, 1);
		ex->ofd = close(inpipe[1]);

/* Turn delay mode on again.  Want to make sure that we wait for all the
   output from the command. */

		fcntl(outpipe[0], F_SETFL, flag);

		dumpout(NULL, ex, 1);
		nbuf = (char *)ex->result;
		close(outpipe[0]);
/*
		TXsetresult(cmd, nbuf);
*/
		cmd->type = FTN_BYTE | DDVARBIT ;
		cmd->elsz = 1;
		setfldandsize(cmd, nbuf, ex->resultsz + 1, FLD_FORCE_NORMAL);
		wait(NULL); /* Get rid of zombies */
		ex = TXfree(ex);
		return 0;
	}
	return 0;
}

#endif
/******************************************************************/

