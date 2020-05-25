/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#ifdef __BORLANDC__
#  undef HAVE_GETPASS_DECL
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef EPI_HAVE_PWD_H
#  include <pwd.h>
#else
#  include "pwdtx.h"
#endif
#include "os.h"
#include "dbquery.h"
#include "passwd.h"
#include "txpwd.h"
#include "dbquery.h"
#include "texint.h"
#ifdef NEED_GETOPT_H
#  ifdef NEED_EPIGETOPT_H
#    include "epigetopt.h"
#  else /* !NEED_EPIGETOPT_H */
#    include <getopt.h>
#  endif /* !NEED_EPIGETOPT_H */
#else /* !NEED_GETOPT_H */
/*
extern int getopt ARGS((int, char **, char *));
extern char *optarg;
extern int  optind, opterr, optopt;
*/
#endif /* !NEED_GETOPT_H */

/******************************************************************/
#define MAXPASS	80  /* Longest userid */

static char *getstr ARGS((char *));

static char *
getstr(prompt)
char *prompt;
{
	char	inbuf[MAXPASS];
	char	*x;

	printf("%s", prompt);
	fgets(inbuf, sizeof inbuf, stdin);
	if ((x = strchr(inbuf, '\n')) != NULL)
		*x = '\0';
	return(TXstrdup(TXPMBUFPN, __FUNCTION__, inbuf));
}

/******************************************************************/

char *
TXgetstrne(prompt)
char *prompt;
{
	char	*retval1;
	char	*retval2;
	SIGTYPE	(CDECL   *old_signal)(int);

#ifdef SIGINT
	old_signal = signal(SIGINT, SIG_IGN);
#endif
	retval1 = getpass(prompt);
#ifdef SIGINT
	signal(SIGINT, old_signal);
#endif
	retval2 = getpass(prompt);
	if(strcmp(retval1, retval2))
	{
		putmsg(MWARN, NULL, "Passwords do not agree");
		return NULL;
	}
	if (!retval1)
		return retval1;
	return(TXstrdup(TXPMBUFPN, __FUNCTION__, retval1));
}

/******************************************************************/

static int getint ARGS((char *));

static int
getint(prompt)
char *prompt;
{
	char	inbuf[MAXPASS];

	printf("%s", prompt);
	fgets(inbuf, sizeof inbuf, stdin);
	return atoi(inbuf);
}

/******************************************************************/

int
adduser(ddic, puname, ppword, spass)
DDIC	*ddic;
char	*puname;
char	*ppword;
char	*spass;
/* Returns -1 on error.
 */
{
	static  char Fn[] = "adduser";
	char	*penc = NULL;
	int	uid, gid, ret;
	FLD	*uf, *pf, *df, *gf;
	TBL	*tbl;
	char	*uname = NULL;
	char	*pword;
	TXPMBUF	*pmbuf = TXPMBUFPN;

	tbl = createusertbl(ddic);
	if (!tbl)
	{
		putmsg(MERR, NULL, "Could not create SYSUSERS");
		goto err;
	}
	if(permstexis(ddic, "_SYSTEM", spass) == -1)
		goto err;
	if (!puname || !puname[0])
		uname = getstr  ("New User name: ");
	else
		uname = TXstrdup(TXPMBUFPN, __FUNCTION__, puname);
	if (gettxpwname(ddic, uname))
	{
		putmsg(MWARN, Fn, "User %s already exists", uname);
		goto err;
	}
	if (!ppword || !ppword[0])
		pword = TXgetstrne("New password : ");
	else
		pword = TXstrdup(pmbuf, __FUNCTION__, ppword);
	if(!pword) goto err;
	if (strlen(pword) > 0)
	{
		penc = TXpwEncrypt(pword, NULL);
		if (!penc)
		{
			putmsg(MERR, __FUNCTION__,
			       "Cannot encrypt password for user `%s'", uname);
			goto err;
		}
	}
	else if (!(penc = TXstrdup(pmbuf, __FUNCTION__, "")))
		goto err;
	pword = TXfree(pword);
	uid   = getint  ("New User id  : ");
	if (gettxpwuid(ddic, uid))
	{
		putmsg(MWARN, Fn, "User id %d already exists", uid);
		goto err;
	}
	gid   = getint  ("New Group id : ");

	uf = nametofld(tbl, "U_NAME");
	pf = nametofld(tbl, "U_PASSWD");
	df = nametofld(tbl, "U_UID");
	gf = nametofld(tbl, "U_GID");
	if (!uf || !pf || !df || !gf)
	{
		putmsg(MERR, NULL, "SYSUSERS is corrupt");
		goto err;
	}

	putfld(uf, uname, strlen(uname));
	putfld(pf, penc, strlen(penc));
	putfld(df, &uid, 1);
	putfld(gf, &gid, 1);
	puttblrow(tbl, NULL);
	ret = 0;
	goto finally;

err:
	ret = -1;
finally:
	uname = TXfree(uname);
	penc = TXfree(penc);
	return(ret);
}

/******************************************************************/

#ifdef TEST
int
main(argc, argv)
int	argc;
char	*argv[];
{
	char	*dname = ".";
	char	*uname, *pword;
	char	*penc;
	DDIC	*ddic;
	int	c;
	int	uid, gid;
	FLD	*uf, *pf, *df, *gf;
	TBL	*tbl;

	mac_won();
	while ((c = getopt(argc, argv, "d:")) != -1)
	{
		switch(c)
		{
			case 'd' : dname = optarg; break;
		}
	}
	ddic = ddopen(dname);
	if (!ddic)
	{
		fprintf(stderr, "Could not open database %s\n", dname);
		exit (TXEXIT_DBOPENFAILED);
	}
	adduser(ddic, NULL, NULL);
	ddclose(ddic);
	exit(TXEXIT_OK);
}

#endif
