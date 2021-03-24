#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef _WIN32
#  include <conio.h>
#elif defined(EPI_HAVE_UNISTD_H)
#  include <unistd.h>
#endif
#include "txcoreconfig.h"
#include "dbquery.h"
#include "texint.h"
#include "passwd.h"

/******************************************************************/

int
administer(cmd, dsource, user, passwd, spass)
char	*cmd;
char	*dsource;
char	*user;
char	*passwd;
char	*spass;
{
	static char Fn[] ="administer";
	DDIC	*ddic;
	int	rc = -1;

	ddic = ddopen(dsource);
	if (!ddic)
	{
		putmsg(MERR+FOE, Fn, "Could not open database %s", dsource);
		return rc;
	}
	switch(cmd[0]){
		case 'a':
		case 'A':
			rc = adduser(ddic, user, passwd, spass);
			break;
		case 'd':
		case 'D':
			rc = deluser(ddic, user, spass);
			break;
		case 'c':
		case 'C':
		{
			char *npass;
			char *hash;

			npass = TXgetstrne("New password:");
			if(!npass)
				break;
			hash = TXpwHash(npass, NULL);
                        if (!hash) break;
			rc = chpass(ddic, user, passwd, hash);
                        hash = TXfree(hash);
			break;
		}
		default:
			putmsg(MWARN, Fn, "Not yet implemented");
	}
	ddclose(ddic);
	return rc;
}

