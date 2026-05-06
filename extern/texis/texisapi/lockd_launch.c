#include "texint.h"
#include <errno.h>
#include <unistd.h>
#include <limits.h>

/* Optional resolver hook -- see typedef in texint.h.  rampart-sql.so installs
   this when it detects an SFX-style bundled rampart that contains a
   texislockd entry; otherwise it stays NULL and the standard search runs. */
tx_lockd_resolver_t tx_lockd_resolver_hook = NULL;

/******************************************************************/

/**
 * Run lock daemon
 *
 * @param ddic Handle to data dictionary

 * @return pid of lock daemon, or -1 on error
 **/

PID_T
TXrunlockdaemon(DDIC *ddic)
{
	char *thepath = NULL;
	int	i;
	PID_T	dbMonPid = 0;			/* ok, nothing exec'd */
#  ifdef _WIN32
	char *cmdline = NULL, *arg;
	HTBUF	*buf = HTBUFPN;
	int	q;
	STARTUPINFO siStartInfo;
	PROCESS_INFORMATION piProcInfo;
	HANDLE hNullF;
#  else	/* !_WIN32 */
	PID_T pid;
#    define MAXARGS	64
	char	*args[MAXARGS];
	int	nargs;
	char	idle_arg[32];
#  endif /* !_WIN32 */

  int n;
  char **list;
  char hookpath_buf[PATH_MAX];
  int hook_idle_secs = 0;
  int hook_used = 0;

  /* Resolver hook first -- bundled rampart provides its own extracted
     texislockd binary path (and an idle timeout to use). */
  if (tx_lockd_resolver_hook &&
      tx_lockd_resolver_hook(hookpath_buf, sizeof(hookpath_buf), &hook_idle_secs) == 0)
  {
    thepath = hookpath_buf;
    hook_used = 1;
  }
  else
  {
    n = TXlib_expandpath("%EXEDIR%" PATH_DIV_S "%BINDIR%", &list);
    for (i = 0; i < n; i++) {
      if (!list[i]) continue;  /* %SYSLIBPATH%, not relevant here */
      thepath = epipathfindmode("texislockd", list[i], 0x8 /* or maybe 0x9 not sure */);
    }
  }
	if(!thepath) {
		thepath = epipathfindmode("texislockd", getenv("PATH"), 0x8);
	}

	errno = 0;
	if (!fexecutable(thepath))	/* check _before_ exec KNG */
	{		/* WTF warning or error? */
		txpmbuf_putmsg(ddic->pmbuf, MWARN + PRM, CHARPN, "Cannot exec database monitor %s: %s", thepath, strerror(errno));
		goto err;
	}
	else
	{
#ifndef _WIN32
		char	cmdArgs[PATH_MAX];

		htsnpf(cmdArgs, sizeof(cmdArgs), "%s", thepath);
		pid = TXfork(ddic->pmbuf, "Lock Server", cmdArgs, 0xe);
		switch (pid)
		{
			case -1:	/* error */
				dbMonPid = (PID_T)(-1);
				break;
			case 0:	/* child */
				chdir("/");	/* don't sit on NFS */
				nargs = 0;
				args[nargs++] = thepath;
				if (hook_used) {
					if (hook_idle_secs > 0) {
						htsnpf(idle_arg, sizeof(idle_arg), "%d", hook_idle_secs);
						args[nargs++] = "-i";
						args[nargs++] = idle_arg;
					}
					args[nargs++] = "-u"; /* unlink-self at startup */
				}
				args[nargs] = CHARPN;
				execv(thepath, args);
				_exit(TXEXIT_CANNOTEXECMON);	/* last resort if failure */
		}
		/* Add to misc. process list so Vortex wait() knows it: */
		dbMonPid = pid;
#else /* _WIN32 */
		hNullF = CreateFile("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
		ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
		siStartInfo.cb = sizeof(STARTUPINFO);
		siStartInfo.lpReserved = NULL;
		siStartInfo.lpReserved2 = NULL;
		siStartInfo.lpTitle = NULL;
		siStartInfo.cbReserved2 = 0;
		siStartInfo.lpDesktop = NULL;
		siStartInfo.hStdInput = hNullF;
		siStartInfo.hStdOutput = hNullF;
		siStartInfo.hStdError = hNullF;
		siStartInfo.dwFlags =	(STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES);
		siStartInfo.wShowWindow = SW_HIDE;

		if ((buf = openhtbuf()) == HTBUFPN) goto err;
		htbuf_setpmbuf(buf, ddic->pmbuf, 0x3);
    /* Quote path if needed */
		q = (int)thepath[strcspn(thepath, " \t\r\n\v\f")];
		htbuf_pf(buf, "%s%s%s", (q ? "\"" : ""), thepath, (q ? "\"" : ""));
		htbuf_getdata(buf, &cmdline, 1);
		if (cmdline)
		{
			size_t cmdlnsz = strlen(cmdline);
			if(cmdline[cmdlnsz-1] == '\"' && cmdline[cmdlnsz-2] == '\\')
			{
				cmdline[cmdlnsz-1] = '\0';
				cmdline[cmdlnsz-2] = '\"';
			}
		}
		if (CreateProcess(thepath, cmdline, NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP, NULL, NULL, &siStartInfo, &piProcInfo))
		{			/* success */
			CloseHandle(piProcInfo.hThread);
			CloseHandle(piProcInfo.hProcess);
			dbMonPid = (PID_T)piProcInfo.dwProcessId;
		}
		else		/* CreateProcess() failed */
			dbMonPid = (PID_T)(-1);
#endif /* _WIN32 */
	}
	goto done;

err:
	dbMonPid = (PID_T)(-1);
done:
#ifdef _WIN32
	buf = closehtbuf(buf);
	cmdline = TXfree(cmdline);
#endif /* _WIN32 */
	return(dbMonPid);
}

/******************************************************************/
