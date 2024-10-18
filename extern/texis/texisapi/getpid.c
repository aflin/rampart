#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef _WIN32
#  include <process.h>
#endif

PID_T   TXpid = 0;

PID_T
TXgetpid(nocache)
int nocache;
/* NOTE: might be called from signal handler.
 */
{
	if(!TXpid || nocache)
		TXpid=getpid();
	return TXpid;
}

