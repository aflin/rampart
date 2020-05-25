/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"

/******************************************************************/

DD *
TXcreateSysusersDd(void)
{
	DD	*dd;
	char	*strType;

        /* Bug 7398: change to varchar to accomodate longer user/pass: */
        strType = (TX_PWENCRYPT_METHODS_ENABLED(TXApp) ? "varchar" : "char");
	if (!(dd = opendd()) ||
	    !ddsettype(dd, TEXIS_FAST_TABLE) ||
	    !putdd(dd, "U_NAME", strType, 20, 0) ||
	    !putdd(dd, "U_UID", "int", 1, 0) ||
	    !putdd(dd, "U_GID", "int", 1, 0) ||
	    !putdd(dd, "U_PASSWD", strType, 20, 0))
	{
		putmsg(MERR, __FUNCTION__, "Cannot create SYSUSERS schema");
		goto err;
	}
	goto finally;

err:
	dd = closedd(dd);
finally:
	return(dd);
}

TBL *
createusertbl(ddic)
DDIC	*ddic;
/* Ensures `ddic->userstbl' points to an open SYSUSERS handle,
 * creating if needed.  Should be called by anything that writes to
 * SYSUSERS.  Note: upgrade handled in monitor.c.
 */
{
	DD	*dd = NULL;
	TBL	*ret = NULL;
	DBTBL	*dbtbl = NULL;
	char	*fname = NULL;

	if (ddic->userstbl) goto ok;		/* already open */

	/* SYSUSERS does not exist; create it: */
	if (!(dd = TXcreateSysusersDd())) goto err;
	dbtbl = createdbtbl(ddic, dd, TEXISFNSYSUSERS, TEXISSYSUSERS,
			    "Texis Users", 'S');
	if (!dbtbl) goto err;
	dbtbl = closedbtbl(dbtbl);

	/* Now open it: */
	fname = TXstrcatN(TXPMBUFPN, __FUNCTION__, ddic->pname,
			  TEXISFNSYSUSERS, NULL);
	if (!fname) goto err;
	ddic->userstbl = opentbl(ddic->pmbuf, fname);
ok:
	ret = ddic->userstbl;
	goto finally;

err:
	ret = closetbl(ret);
finally:
	dd = closedd(dd);
	fname = TXfree(fname);
	return(ret);
}

/******************************************************************/

TBL *createpermtbl ARGS((DDIC *));

TBL *
createpermtbl(ddic)
DDIC	*ddic;
{
	static char Fn[] = "createpermtbl";
	DD	*dd;
	TBL	*tbl;
	DBTBL	*dbtbl;
	char	*fname;

	if (ddic->permstbl)
		return ddic->permstbl;
	dd = opendd();
	(void)ddsettype(dd, 1);
	fname = malloc(PATH_MAX);
	if (!dd || !fname)
	{
		if(dd) closedd(dd);
		if(fname) free(fname);
		putmsg(MERR+MAE, Fn, strerror(ENOMEM));
		return NULL;
	}
	putdd(dd, "P_UID", "int", 1, 0);
	putdd(dd, "P_GID", "int", 1, 0);
	putdd(dd, "P_NAME", "char", 40, 0);
	putdd(dd, "P_PERM", "long", 1, 0);
	putdd(dd, "P_GRANT", "long", 1, 0);
	putdd(dd, "P_GUID", "int", 1, 0);
#if 0
	strcpy(fname, ddic->pname);
	strcat(fname, TEXISFNSYSPERMS);
#else
	strcpy(fname, TEXISFNSYSPERMS);
#endif
	dbtbl = createdbtbl(ddic, dd, fname, TEXISSYSPERMS, "Texis Permissions", 'S');
	dbtbl = closedbtbl(dbtbl);
	/* KNG 20110524 a recent ddreset() may have already opened `permstbl',
	 * so close it first to avoid orphaning:
	 */
	if (ddic->permstbl != TBLPN)
		ddic->permstbl = closetbl(ddic->permstbl);
	strcpy(fname, ddic->pname);
	strcat(fname, TEXISFNSYSPERMS);
	tbl = opentbl(ddic->pmbuf, fname);
	ddic->permstbl = tbl;
	dd = closedd(dd);
	fname = TXfree(fname);
	return tbl;
}

/******************************************************************/

#ifdef TEST

int
main(argc, argv)
int	argc;
char	*argv[];
{
	char	*dpath = ".";
	DDIC	*ddic;

	if (argc > 1)
		dpath = argv[1];
	ddic = ddopen(dpath);
	if (!ddic)
	{
		putmsg(MERR, NULL, "Could not open database %s", dpath);
		exit (1);
	}
	ddic->userstbl = createusertbl(ddic);
	ddic->permstbl = createpermtbl(ddic);
	ddclose(ddic);
}

#endif

