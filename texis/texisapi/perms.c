/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#if defined(unix) || defined(__unix)
#include <unistd.h>
#include <pwd.h>
#else
#include "pwdtx.h"
#endif
#include "os.h"
#include "mmsg.h"
#include "dbquery.h"
#include "texint.h"
#include "txpwd.h"
#include "cgi.h"				/* for htsnpf() */

/******************************************************************/

int
permslogoff(ddic)
DDIC *ddic;
{
	if(ddic->perms)
	{
		free(ddic->perms);
		ddic->perms = NULL;
	}
	return 0;
}

/******************************************************************/

int
TXverifypasswd(clear, encrypt)
CONST char *clear;	/* The password typed in */
CONST char *encrypt;	/* The encrypted password from the Passwd file */
/* Returns 1 if match, 0 if not.
 */
{
	char	*cryptRet;
	int	ret;

	if (!encrypt || encrypt[0] == '\0')
	{
		if(clear && clear[0])
			return 0;
		return 1;
	}
	if (!encrypt || strlen(encrypt) < (size_t)3 || !clear)
		return 0;
	/* Bug 7398 use TXpwEncrypt(); works around some crypt() failures: */
	/* Do not truncate `encrypt' to 2 chars: salt could be longer: */
	cryptRet = TXpwEncrypt(clear, encrypt);
	if (!cryptRet) return(0);		/* error */
	ret = (strcmp(cryptRet, encrypt) == 0);
	cryptRet = TXfree(cryptRet);
	return(ret);
}

#define MAX_FLD_LEN(fld)	\
	((TXfldType(fld) & DDVARBIT) ? EPI_OS_SIZE_T_MAX : (fld)->n)

/******************************************************************/
/*	Get a password entry.  This is analogous to the unix
 *	getpwuid call.  Currently only fills in a few fields.
 */

struct passwd *gettxpwuid ARGS((DDIC *, int));

struct passwd *
gettxpwuid(ddic, wuid)
DDIC	*ddic;
int	wuid;
{
	static	char Fn[] = "gettxpwname";
	static  struct	passwd	pwd;
	FLD	*nf, *pf, *uf, *gf;
	TBL	*userstbl;

#ifndef NO_CACHE_TABLE
	makevalidtable(ddic, SYSTBL_USERS);
	userstbl = ddic->usrtblcache->tbl;
#else
	userstbl = ddic->userstbl;
#endif
	if (!userstbl)
	{
		putmsg(MERR, Fn, "SYSUSERS does not exist: Cannot verify user id");
		return NULL;
	}
	nf = nametofld(userstbl, "U_NAME");
	pf = nametofld(userstbl, "U_PASSWD");
	uf = nametofld(userstbl, "U_UID");
	gf = nametofld(userstbl, "U_GID");
	if (!nf || !pf || !uf || !gf)
	{
		putmsg(MERR, Fn, "Corrupt SYSUSERS structure");
		return NULL;
	}
	if (TXlocksystbl(ddic, SYSTBL_USERS, R_LCK, NULL) == -1)
		return(NULL);
	rewindtbl(userstbl);
	while (TXrecidvalid(gettblrow(userstbl, NULL)))
	{
		char	*cname, *pw;
		int	uid, gid;
		size_t	len;

		uid = *(int *)getfld(uf, &len);
		if (uid == wuid)
		{
		/* Found our user.  Fill in pwd, and return. */
			cname = getfld(nf, &len);
			pw = getfld(pf, &len);
			gid = *(int *)getfld(gf, &len);
			pwd.pw_name = cname;
			pwd.pw_passwd = pw;
			pwd.pw_uid = uid;
			pwd.pw_gid = gid;
			TXunlocksystbl(ddic, SYSTBL_USERS, R_LCK);
			return &pwd;
		}
	}
	TXunlocksystbl(ddic, SYSTBL_USERS, R_LCK);
	return NULL;
}

/******************************************************************/
/*	Get a password entry.  This is analogous to the unix
 *	getpwname call.  Currently only fills in a few fields.
 */

int
TXgettxpwname_r(ddic, name, pwbuf)
DDIC		*ddic;		/* (in) handle to read from */
CONST char	*name;		/* (in) user name to look up */
struct passwd	*pwbuf;		/* (out) return buffer to write to */
/* Thread-safe, iff `ddic'/Texis is.  `*pwbuf' values will point into `ddic'.
 * Note: deluser(), chpass(), TXdropuser(), changeuser() assume
 * ddic->userstbl->df still points to entry on return.
 * Returns 0 on success, -1 if not found or other error.
 */
{
	static CONST char	Fn[] = "TXgettxpwname_r";
	FLD			*nf, *pf, *uf, *gf;
	int			ret;

	memset(pwbuf, 0, sizeof(struct passwd));
	if (!ddic->userstbl)
	{
		if(!ddic->increate)
			putmsg(MERR, Fn,
			"SYSUSERS does not exist: Cannot verify user name");
		return -1;
	}
	nf = nametofld(ddic->userstbl, "U_NAME");
	pf = nametofld(ddic->userstbl, "U_PASSWD");
	uf = nametofld(ddic->userstbl, "U_UID");
	gf = nametofld(ddic->userstbl, "U_GID");
	if (!nf || !pf || !uf || !gf)
	{
		putmsg(MERR, Fn, "Corrupt SYSUSERS structure");
		return -1;
	}
	if (TXlocksystbl(ddic, SYSTBL_USERS, R_LCK, NULL) == -1)
		return(-1);
	rewindtbl(ddic->userstbl);
	while (TXrecidvalid(gettblrow(ddic->userstbl, NULL)))
	{
		char	*cname, *pw;
		int	uid, gid;
		size_t	len;

		cname = getfld(nf, &len);
		if (!strcmp(cname, name))
		{
		/* Found our user.  Fill in pwd, and return. */
			pw = getfld(pf, &len);
			uid = *(int *)getfld(uf, &len);
			gid = *(int *)getfld(gf, &len);
			pwbuf->pw_name = cname;
			pwbuf->pw_passwd = pw;
			pwbuf->pw_uid = uid;
			pwbuf->pw_gid = gid;
			ret = 0;		/* success */
			goto done;
		}
	}
	ret = -1;				/* not found */
done:
	TXunlocksystbl(ddic, SYSTBL_USERS, R_LCK);
	return ret;
}

struct passwd *
gettxpwname(ddic, name)
DDIC	*ddic;
char	*name;
/* Note: see TXgettxpwname_r() caveats.
 * Thread un-safe.
 */
{
	static struct passwd	pwbuf;

	if (TXgettxpwname_r(ddic, name, &pwbuf) == 0)
		return(&pwbuf);
	else
		return((struct passwd *)NULL);
}

/******************************************************************/
/*	Set the security to UNIX security.  This effectively
 *	disables the texis security on the open database.  Note
 *	that once permissions have been set on a database they
 *	can't currently be changed.
 *
 *	Returns 0 if successful, and -1 if there was an error.
 */

int
permsunix(ddic)
DDIC	*ddic;	/* Data dictionary to apply to */
{
#ifdef NEVER
	static	char Fn[]="permsunix";

	putmsg(MWARN, NULL, "Unix permissions no longer supported.  Attempting PUBLIC login");
#endif
	return permstexis(ddic, "PUBLIC", "");
}

/******************************************************************/
/*	Set up texis internal security on the database. */
int
permstexis(ddic, user, psswd)
DDIC	*ddic;	/* Data dictionary to apply to */
char	*user;	/* User requesting login */
char	*psswd;	/* Users passwd */
{
	static	char Fn[]="permstexis";
	struct	passwd *pwd;
	TXPERMS	*p;
	int	rc;

	if (ddic->perms)
	{
		putmsg(MERR+UGE, Fn,
			"Permissions are already set on this database");
		return -1;
	}
	p = (TXPERMS *)calloc(1, sizeof(TXPERMS));
	if (!p)
	{
		putmsg(MERR+MAE, Fn, strerror(ENOMEM));
		return -1;
	}
	p->unixperms = 0;
	p->texuid = -1;
	p->texgid = -1;
	p->texsuid = -1;
	p->texsgid = -1;
	pwd = gettxpwname(ddic, user);
	if (!pwd || !TXverifypasswd(psswd, pwd->pw_passwd))
	{
		if(!ddic->increate)
			putmsg(MERR, Fn, "Login failure");
		p->state = TX_PM_FAILED;
		rc = -1;
	}
	else
	{
		p->state = TX_PM_SUCCESS;
		rc = 0;
		p->texuid = pwd->pw_uid;
		p->texgid = pwd->pw_gid;
	}
	if(rc == 0)
		TXstrncpy(p->uname, user, sizeof p->uname);
	ddic->perms = p;
	return rc;
}

/******************************************************************/

typedef struct tagTBPERM
{
	long	perms;
	long	grant;
} TBPERM ;

/******************************************************************/

static TBPERM *getperms ARGS((DDIC *, DBTBL *));

static TBPERM *
getperms(ddic, db)
DDIC	*ddic;
DBTBL	*db;
{
	TBPERM *tbp;

	static char Fn[] = "getperms";
	int	uid;
	int	gid;
	char	*tbname;
	FLD	*uf, *gf, *nf, *pm, *gr;
	TBL	*pt = TBLPN;
	TXPERMS	*p;

#ifndef NO_CACHE_TABLE
	if(!isramdbtbl(db)) /* Don't need SYSPERMS for RAM Table */
	{
		makevalidtable(ddic, SYSTBL_PERMS);
		pt = ddic->prmtblcache->tbl;
		if (!pt)
		{
			putmsg(MERR, Fn, "Could not read SYSPERMS");
			return 0;
		}
	}
#else
	pt = ddic->permstbl;
	if(!pt)
	{
		putmsg(MERR, Fn, "Could not read SYSPERMS");
		return NULL;
	}
#endif
	tbp = (TBPERM *)calloc(1, sizeof(TBPERM));
	if (!tbp)
	{
		putmsg(MERR+MAE, Fn, strerror(ENOMEM));
		return tbp;
	}
	if(isramdbtbl(db))  /* Local temp table */
	{
		tbp->perms=-1;
		tbp->grant=-1;
		return tbp;
	}
	p = ddic->perms;
	uid = p->texuid;
	gid = p->texgid;
	tbname = db->rname;
	uf = nametofld(pt, "P_UID");
	gf = nametofld(pt, "P_GID");
	nf = nametofld(pt, "P_NAME");
	pm = nametofld(pt, "P_PERM");
	gr = nametofld(pt, "P_GRANT");
	if (!uf || !gf || !nf || !pm || !gr)
	{
		putmsg(MERR, Fn, "SYSPERMS Corrupted.  No permissions granted");
		return tbp;
	}
	if (!tbname)
	{
		putmsg(MERR, Fn, "No table name");
		return tbp;
	}
#ifdef NO_CACHE_TABLE
	if (TXlocksystbl(ddic, SYSTBL_PERMS, R_LCK, NULL) == -1)
		return(tbp);
#endif
	rewindtbl(pt);
	while(TXrecidvalid(gettblrow(pt, NULL)))
	{
		int	ru, rg;
		char	*rn;
		size_t	sz;

		ru = *(int *)getfld(uf, &sz);
		rg = *(int *)getfld(gf, &sz);
		rn = getfld(nf, &sz);
		if(((uid == ru) || (gid == rg) || (ru == TX_PUBLIC_UID)) &&
			(!strcmp(rn, tbname)))
		{
			tbp->perms |= *(long *)getfld(pm, &sz);
			tbp->grant |= *(long *)getfld(gr, &sz);
#ifdef NEVER
			putmsg(999, NULL, "Perms on %s now %x", db->lname, tbp->perms);
#endif
		}
	}
#ifdef NO_CACHE_TABLE
	TXunlocksystbl(ddic, SYSTBL_PERMS, R_LCK);
#endif
	return tbp;	/* Currently returning some perms */
}

/******************************************************************/

int
dbgetperms(tbl, ddic)
DBTBL	*tbl;
DDIC	*ddic;
{
	static char Fn[]="dbgetperms";
	TXPERMS	*p;
	TBPERM	*tbp;

	p = ddic->perms;
	if (!p)
	{
#ifdef NEVER
		putmsg(MINFO, NULL, "No permissions had been set.  Assuming PUBLIC");
#endif
		permsunix(ddic);
		p = ddic->perms;
		if(!p)
			return -1;
	}
	if (p->state == TX_PM_FAILED)
	{
#ifdef NEVER
		putmsg(999, NULL, "You are not logged in");
#endif
		tbp = (TBPERM *)calloc(1, sizeof(TBPERM));
		if (!tbp)
		{
			putmsg(MERR+MAE, Fn, strerror(ENOMEM));
			return 0;
		}
		tbp->perms = 0;
		tbp->grant = 0;
		tbl->perms = tbp;
		return 0;
	}
	if (p->unixperms)
		return 0;
	tbp = getperms(ddic, tbl);
	if (p->texuid == TX_SYSTEM_UID)
	{
		tbp->perms = -1;
		tbp->grant = -1;
	}
	if(tbl->perms)
		free(tbl->perms);
	tbl->perms = tbp;
	if (!tbl->perms)
		return -1 ;
	return 0;
}

/******************************************************************/

int
permcheck(dbt, preq)
DBTBL	*dbt;
int	preq;
{
#ifdef NEVER
	static char Fn[] = "permcheck";
#endif
	TBPERM	*tbp;

	tbp = dbt->perms;
	if (!tbp)
		return 1;
	if (preq & PM_GRANTOPT)
	{
		preq -= PM_GRANTOPT;
		return (preq & tbp->perms & tbp->grant & PM_ALLPERMS) ==
			(preq & PM_ALLPERMS);
	}
	else
	{
		if((preq & tbp->perms & PM_ALLPERMS) != (preq & PM_ALLPERMS))
		{
#ifdef NEVER
			putmsg(999, Fn, "Wanted %x, got %x on %s", preq, tbp->perms, dbt->lname);
#endif
		}
		return (preq & tbp->perms & PM_ALLPERMS) == (preq & PM_ALLPERMS);
	}
}

/******************************************************************/

long
strtoperms(pstr)
char *pstr;
{
	long	pf = 0;
	char	*p;

	if (pstr)
		for (p=pstr; *p; p++)
			switch (*p)
			{
				case 'a' : pf |= PM_ALTER; break;
				case 'd' : pf |= PM_DELETE; break;
				case 'i' : pf |= PM_INSERT; break;
				case 's' : pf |= PM_SELECT; break;
				case 'u' : pf |= PM_UPDATE; break;
				case 'r' : pf |= PM_REFERENCES; break;
				case 'x' : pf |= PM_INDEX; break;
				case 'g' : pf |= PM_GRANTOPT; break;
				case 'z' : pf |= (-1)&(~PM_GRANTOPT); break;
			}
	return pf;
}

/******************************************************************/

char *
strtounix(pstr)
char *pstr;
{
	char	*p;
	char	res[80];

	res[0] = '\0';
	if (pstr)
		for (p=pstr; *p; p++)
			switch (*p)
			{
				case 'a' : strcat(res,"w"); break;
				case 'd' : strcat(res,"w"); break;
				case 'i' : strcat(res,"w"); break;
				case 's' : strcat(res,"r"); break;
				case 'u' : strcat(res,"rw"); break;
				case 'r' : strcat(res,"r"); break;
				case 'x' : strcat(res,"r"); break;
				case 'g' : strcat(res,"rw"); break;
				case 'z' : strcat(res,"rw"); break;
			}
	return strdup(res);
}

/******************************************************************/
/*
 *	Returns 0 on success, -1 on failure, 1 need to do unix perms
 */

int
permgrant(ddic, table, user, perms)
DDIC	*ddic;
DBTBL	*table;
char	*user;
long	perms;
{
	static char Fn[] = "permgrant";
	struct passwd *pwd;
	int	uid, gid;
	TBL	*pt;
	TBPERM	*tbp;
	FLD	*uf, *gf, *pm, *nf, *gr, *gt;
	RECID	*at;
	char	*tbname;
	TXPERMS	*p;
	long	grper;
	int	rc;

	p = ddic->perms;
	if (p->unixperms)
		return 1;
	tbp = table->perms;
	if (perms & PM_GRANTOPT)
		grper = perms;
	else
		grper = 0;
	perms &= tbp->grant; /* Here's what we can grant */
	grper &= tbp->grant; /* Here's what we can grant */
	pwd = gettxpwname(ddic, user);
	if (!pwd)
	{
		putmsg(MERR, Fn, "No such user `%s'", user);
		return -1;
	}
	uid = pwd->pw_uid;
	gid = -1;
	pt = ddic->permstbl;
	if(!pt)
	{
		if (!ddic->increate)
			putmsg(MERR, Fn, "Could not read SYSPERMS");
		return -1;
	}
	tbname = table->rname;
	uf = nametofld(pt, "P_UID");
	gf = nametofld(pt, "P_GID");
	nf = nametofld(pt, "P_NAME");
	pm = nametofld(pt, "P_PERM");
	gr = nametofld(pt, "P_GRANT");
	gt = nametofld(pt, "P_GUID");
	if (!uf || !gf || !nf || !pm || !gr || !gt)
	{
		putmsg(MERR, Fn, "SYSPERMS Corrupted.  No permissions granted");
		return -1;
	}
	if (!tbname)
	{
		putmsg(MERR, Fn, "No table name");
		return -1;
	}
	if (TXlocksystbl(ddic, SYSTBL_PERMS, W_LCK, NULL) == -1)
		return(-1);
	rewindtbl(pt);
	while(TXrecidvalid(at = gettblrow(pt, NULL)))
	{
		int	ru, guid;
		char	*rn;
		size_t	sz;

		ru = *(int *)getfld(uf, &sz);
		guid = *(int *)getfld(gt, &sz);
		rn = getfld(nf, &sz);
		if((uid == ru) &&           /* Same user */
		   (p->texuid ==  guid) &&  /* Did we give them? */
		   (!strcmp(rn, tbname)))   /* This table */
		{
			perms |= *(long *)getfld(pm, &sz);
			grper |= *(long *)getfld(gr, &sz);
			putfld(pm, &perms, 1);
			putfld(gr, &grper, 1);
			rc = TXrecidvalid(puttblrow(pt, at));
			TXunlocksystbl(ddic, SYSTBL_PERMS, W_LCK);
			if(rc)
				return 0;
			putmsg(MWARN, "GRANT", "Could not write to table.");
			return -1;
		}
	}
	putfld(uf, &uid, 1);
	putfld(gf, &gid, 1);
	putfld(nf, tbname, strlen(tbname));
	putfld(pm, &perms, 1);
	putfld(gr, &grper, 1);
	putfld(gt, &p->texuid, 1);
	rc = TXrecidvalid(puttblrow(pt, at));
	TXunlocksystbl(ddic, SYSTBL_PERMS, W_LCK);
	if(rc)
		return 0;
	putmsg(MWARN, "GRANT", "Could not write to table.");
	return -1;
}

/******************************************************************/
/*
 *	Returns 0 on success, -1 on failure, 1 need to do unix perms
 */

int
permgrantdef(ddic, table)
DDIC	*ddic;
DBTBL	*table;
{
	static char Fn[] = "permgrant";
	int	uid, gid;
	TBL	*pt;
	FLD	*uf, *gf, *pm, *nf, *gr, *gt;
	RECID	*at;
	char	*tbname;
	TXPERMS	*p;
	long	grper;
	long	perms;
	int	rc;

	p = ddic->perms;
	if (!p)
	{
		putmsg(MINFO, NULL,
		              "No permissions had been set.  Assuming PUBLIC");
		permsunix(ddic);
		p = ddic->perms;
	}
	if (p->unixperms)
		return 1;
	perms = ~0;
	grper = ~0;
	uid = p->texuid;
	gid = p->texgid;
	pt = ddic->permstbl;
	if(!pt)
	{
		if(!ddic->increate)
		putmsg(MERR, Fn, "Could not read SYSPERMS");
		return -1;
	}
	tbname = table->rname;
	uf = nametofld(pt, "P_UID");
	gf = nametofld(pt, "P_GID");
	nf = nametofld(pt, "P_NAME");
	pm = nametofld(pt, "P_PERM");
	gr = nametofld(pt, "P_GRANT");
	gt = nametofld(pt, "P_GUID");
	if (!uf || !gf || !nf || !pm || !gr || !gt)
	{
		putmsg(MERR, Fn, "SYSPERMS Corrupted.  No permissions granted");
		return -1;
	}
	if (!tbname)
	{
		putmsg(MERR, Fn, "No table name");
		return -1;
	}
	if (TXlocksystbl(ddic, SYSTBL_PERMS, W_LCK, NULL) == -1)
		return(-1);
	rewindtbl(pt);
	while(TXrecidvalid(at = gettblrow(pt, NULL)))
	{
		int	ru, guid;
		char	*rn;
		size_t	sz;

		ru = *(int *)getfld(uf, &sz);
		guid = *(int *)getfld(gt, &sz);
		rn = getfld(nf, &sz);
		if((uid == ru) &&           /* Same user */
		   ((p->texuid == TX_SYSTEM_UID) ||
		   (p->texuid ==  guid)) &&  /* Did we give them? */
		   (!strcmp(rn, tbname)))   /* This table */
		{
			perms |= *(long *)getfld(pm, &sz);
			grper |= *(long *)getfld(gr, &sz);
			putfld(pm, &perms, 1);
			putfld(gr, &grper, 1);
			rc = TXrecidvalid(puttblrow(pt, at));
			TXunlocksystbl(ddic, SYSTBL_PERMS, W_LCK);
			if(rc)
				return 0;
			putmsg(MWARN, "GRANT", "Could not write to table.");
			return -1;
		}
	}
	putfld(uf, &uid, 1);
	putfld(gf, &gid, 1);
	putfld(nf, tbname, strlen(tbname));
	putfld(pm, &perms, 1);
	putfld(gr, &grper, 1);
	putfld(gt, &p->texuid, 1);
	rc = TXrecidvalid(puttblrow(pt, at));
	TXunlocksystbl(ddic, SYSTBL_PERMS, W_LCK);
	if(rc)
		return 0;
	putmsg(MWARN, "GRANT", "Could not write to table.");
	return -1;
}

/******************************************************************/
/*
 *	Returns 0 on success, -1 on failure, 1 need to do unix perms
 */

int
permrevoke(ddic, table, user, perms)
DDIC	*ddic;
DBTBL	*table;
char	*user;
long	perms;
{
	static char Fn[] = "permrevoke";
	struct passwd *pwd;
	int	uid;
	TBL	*pt;
	FLD	*uf, *gf, *pm, *nf, *gr, *gt;
	RECID	*at;
	char	*tbname;
	TXPERMS	*p;
	long	grper = 0;

	if (perms & PM_GRANTOPT)
	{
		grper = perms;
		perms = 0;
	}
	else
	{
		perms |= PM_GRANTOPT;
		grper = perms;
	}
	p = ddic->perms;
	if (p->unixperms)
		return 1;
	pwd = gettxpwname(ddic, user);
	if (!pwd)
	{
		putmsg(MERR, Fn, "No such user `%s'", user);
		return -1;
	}
	uid = pwd->pw_uid;
	pt = ddic->permstbl;
	if(!pt)
	{
		putmsg(MERR, Fn, "Could not read SYSPERMS");
		return -1;
	}
	tbname = table->rname;
	uf = nametofld(pt, "P_UID");
	gf = nametofld(pt, "P_GID");
	nf = nametofld(pt, "P_NAME");
	pm = nametofld(pt, "P_PERM");
	gr = nametofld(pt, "P_GRANT");
	gt = nametofld(pt, "P_GUID");
	if (!uf || !gf || !nf || !pm || !gr || !gt)
	{
		putmsg(MERR, Fn, "SYSPERMS Corrupted.  No permissions revoked");
		return -1;
	}
	if (!tbname)
	{
		putmsg(MERR, Fn, "No table name");
		return -1;
	}
	if (TXlocksystbl(ddic, SYSTBL_PERMS, W_LCK, NULL) == -1)
		return(-1);
	rewindtbl(pt);
	while(TXrecidvalid(at = gettblrow(pt, NULL)))
	{
		int	ru, guid;
		char	*rn;
		size_t	sz;

		ru = *(int *)getfld(uf, &sz);
		guid = *(int *)getfld(gt, &sz);
		rn = getfld(nf, &sz);
		if((uid == ru) &&           /* Same user */
		   ((p->texuid ==  guid) ||
		    (p->texuid == TX_SYSTEM_UID)) &&  /* Did we give them? */
		   (!strcmp(rn, tbname)))   /* This table */
		{
			long	t1, t2;
			t1 = *(long *)getfld(pm, &sz);
			t2 = *(long *)getfld(gr, &sz);
			t1&=(~perms);
			t2&=(~grper);
			if (!t1)
			{
				if(!deltblrow(pt, at))
				{
					putmsg(MWARN, "REVOKE",
					 "Could not write to table SYSPERMS.");
				}
			}
			else
			{
				putfld(pm, &t1, 1);
				putfld(gr, &t2, 1);
				if(!TXrecidvalid(puttblrow(pt, at)))
				{
					putmsg(MWARN, "REVOKE",
					 "Could not write to table SYSPERMS.");
				}
			}
		}
	}
	TXunlocksystbl(ddic, SYSTBL_PERMS, W_LCK);
	return 0;
}

/******************************************************************/

int
deluser(ddic, user, psswd)
DDIC	*ddic;
char	*user;
char	*psswd;
{
	static char Fn[] = "deluser";
	struct passwd *pwd;

	if (!createusertbl(ddic)) return(-1);

	pwd = gettxpwname(ddic, TEXIS_SUPERUSER);
	if (!pwd || !TXverifypasswd(psswd, pwd->pw_passwd))
	{
		putmsg(MERR, Fn, "Need to supply administrator password");
		return -1;
	}
	pwd = gettxpwname(ddic, user);
	if (!pwd)
	{
		putmsg(MERR, Fn, "Could not find user `%s'", user);
		return -1;
	}
/* We should still be sitting at the right spot. */
/* WTF need to remove all permissions granted to that user and possibly
   all his tables */
	freedbf(ddic->userstbl->df, telldbf(ddic->userstbl->df));
	return 0;
}

static void
tooLongMsg(TXPMBUF *pmbuf, const char *fn, const char *db, TXbool isPasswd)
{
	txpmbuf_putmsg(pmbuf, MERR + MAE, fn, "%s update failed: would be too long for current SYSUSERS schema in `%s'%s",
		       (isPasswd ? "Password hash" : "User name"),
		       db,
		       (TX_PWENCRYPT_METHODS_ENABLED(TXApp) ?
			": Set [Monitor] Upgrade SYSTEM Tables nonzero and restart Texis Monitor" : ""));
}

/******************************************************************/

int
chpass(ddic, user, opsswd, npsswd)
DDIC	*ddic;
char	*user;
char	*opsswd;	/* cleartext old/existing password */
char	*npsswd;	/* encrypted new password */
{
	static char Fn[] = "chpass";
	struct passwd *pwd;
	int	invalid = 1;
	TBL	*tbl;
	FLD	*f;
	size_t	newPassLen;

	tbl = createusertbl(ddic);
	if (!tbl) return(-1);

	pwd = gettxpwname(ddic, TEXIS_SUPERUSER);
	if (pwd && TXverifypasswd(opsswd, pwd->pw_passwd))
		invalid = 0;
	pwd = gettxpwname(ddic, user);
	if (!pwd)
	{
		putmsg(MERR, Fn, "Could not find user `%s'", user);
		return -1;
	}
	if (invalid && !TXverifypasswd(opsswd, pwd->pw_passwd))
	{
		putmsg(MERR, Fn, "No valid password supplied");
		return -1;
	}
	f = nametofld(tbl, "U_PASSWD");
	newPassLen = strlen(npsswd);
	if (newPassLen > MAX_FLD_LEN(f))
	{
		tooLongMsg(TXPMBUFPN, __FUNCTION__, ddic->epname, TXbool_True);
		return(-1);
	}
	putfld(f, npsswd, newPassLen);
	/* wtf assume we are still pointing at `user''s entry: */
	puttblrow(tbl, telltbl(tbl));
	return 0;
}

/******************************************************************/

char	*
TXgetusername(ddic)
DDIC	*ddic;
{
	TXPERMS	*p;

	p = ddic->perms;
	if (!p)
	{
#ifdef NEVER
		putmsg(MINFO, NULL, "No permissions had been set.  Assuming PUBLIC");
#endif
		permsunix(ddic);
		p = ddic->perms;
		if(!p)
			return NULL;
	}
	return p->uname;
}

/******************************************************************/

int
TXpushid(ddic, uid, gid)
DDIC	*ddic;
int	uid, gid;
{
	TXPERMS	*p;

	p = ddic->perms;
	if (!p)
	{
		permsunix(ddic);
		p = ddic->perms;
		if(!p)
			return -1;
	}
	if (p->pushcnt > 0)			/* in-progress TXpushid() */
	{
		/* It's a one-element "stack", but simulate multi-level with
		 * a ref count because we only su to _SYSTEM typically:
		 */
		if (uid != p->texuid || gid != p->texgid)
		{
			/* yap, but obscurely; this is perms: */
			putmsg(MERR + UGE, CHARPN,
				"Internal error: Unistack overflow");
			/* return(-1); */  /* caller probably ignores... */
		}
		goto done;			/* already saved */
	}

	p->texsuid = p->texuid;
	p->texsgid = p->texgid;
	TXstrncpy(p->suname, p->uname, sizeof(p->suname));
	p->texuid = uid;
	p->texgid = gid;
        if (uid == 0) TXstrncpy(p->uname, TEXIS_SUPERUSER, sizeof(p->uname));
done:
	p->pushcnt++;
	return 0;
}

/******************************************************************/

int
TXpopid(ddic)
DDIC	*ddic;
{
	TXPERMS	*p;

	p = ddic->perms;
	if (!p)
	{
		permsunix(ddic);
		p = ddic->perms;
		if(!p)
			return -1;
	}
	if(p->texsuid == -1 || p->pushcnt <= 0)
		return -1;
	if (--p->pushcnt > 0) return(0);	/* not all done yet */
	p->texuid = p->texsuid;
	p->texgid = p->texsgid;
	TXstrncpy(p->uname, p->suname, sizeof(p->uname));
        p->texsuid = p->texsgid = -1;
	return 0;
}

/******************************************************************/

static int iamsystem ARGS((DDIC *));

static int
iamsystem(ddic)
DDIC *ddic;
{
	TXPERMS	*p;

	p = ddic->perms;
	if (!p)
	{
		permsunix(ddic);
		p = ddic->perms;
		if(!p)
			return 0;
	}
	if(p->state == TX_PM_SUCCESS && p->texuid == 0)
		return 1;
	return 0;
}

/******************************************************************/

static int getnewuid ARGS((DDIC *));

static int
getnewuid(ddic)
DDIC *ddic;
{
	int i;
	/* WTF - grossly inefficient if many users already */

	for(i = 100; i < 9999; i++)
	{
		if(!gettxpwuid(ddic, i))
			return i;
	}
	return -1;
}

/******************************************************************/

int
createuser(ddic, user, pass)
DDIC *ddic;
char *user;
char *pass;
{
	struct passwd *pwd;
	int uid, ret;
	int gid;
	char *penc = NULL;
	TBL *tbl;
	FLD *uf, *pf, *df, *gf;
	TXPMBUF	*pmbuf = TXPMBUFPN;
	size_t	len;

	tbl = createusertbl(ddic);

	if(!iamsystem(ddic))
	{
		putmsg(MWARN+UGE, NULL, "You are not authorized to create users");
		goto err;
	}
	pwd = gettxpwname(ddic, user);
	if (pwd)
	{
		putmsg(MWARN+UGE, NULL, "User `%s' already exists", user);
		goto err;
	}
	uid = getnewuid(ddic);
	if(uid == -1)
	{
		putmsg(MWARN+UGE, NULL, "Too many users");
		goto err;
	}
	gid = 100;
	if (!tbl) goto err;

	uf = nametofld(tbl, "U_NAME");
	pf = nametofld(tbl, "U_PASSWD");
	df = nametofld(tbl, "U_UID");
	gf = nametofld(tbl, "U_GID");
	if (!uf || !pf || !df || !gf)
	{
		putmsg(MERR, NULL, "SYSUSERS is corrupt");
		goto err;
	}

	if (strlen(pass) > 0)
	{
		const char	*salt = NULL;

		/* Fall back to DES if SYSUSERS has not been upgraded,
		 * so that encrypted password fits:
		 */
		if (!fldisvar(pf))
		{
			TXpwEncryptMethod	prefMethod;

			salt = TX_PWENCRYPT_SALT_STR_DES;
			prefMethod = TXpwEncryptMethod_CURRENT(TXApp);
			if (prefMethod != TXpwEncryptMethod_DES)
				putmsg(MWARN, NULL, "%s password hash would be too long for current SYSUSERS schema in `%s'; using DES instead: Set [Monitor] Upgrade SYSTEM Tables nonzero and restart Texis Monitor",
				       TXpwEncryptMethodEnumToStr(prefMethod),
				       ddic->epname);
		}
		penc = TXpwEncrypt(pass, salt);
		if (!penc)
		{
			putmsg(MERR, __FUNCTION__,
			       "Could not encrypt password for user `%s'",
			       user);
			goto err;
		}
	}
	else if (!(penc = TXstrdup(pmbuf, __FUNCTION__, "")))
		goto err;

	len = strlen(user);
	if (len > MAX_FLD_LEN(uf))
	{
		tooLongMsg(pmbuf, __FUNCTION__, ddic->epname, TXbool_False);
		goto err;
	}
	putfld(uf, user, len);

	len = strlen(penc);
	if (len > MAX_FLD_LEN(pf))
	{
		tooLongMsg(pmbuf, __FUNCTION__, ddic->epname, TXbool_True);
		goto err;
	}
	putfld(pf, penc, len);

	putfld(df, &uid, 1);
	putfld(gf, &gid, 1);
	puttblrow(tbl, NULL);
	ret = 0;
	goto finally;

err:
	ret = -1;
finally:
	penc = TXfree(penc);
	return(ret);
}

/******************************************************************/

int
TXdropuser(ddic, user)
DDIC *ddic;
char *user;
{
	struct passwd *pwd;

	if (!createusertbl(ddic)) return(-1);

	if(!iamsystem(ddic))
	{
		putmsg(MWARN+UGE, NULL, "You are not authorized to drop users");
		return -1;
	}

	pwd = gettxpwname(ddic, user);
	if (!pwd)
	{
		putmsg(MWARN, NULL, "Could not find user `%s'", user);
		return -1;
	}
/* We should still be sitting at the right spot. */
/* WTF need to remove all permissions granted to that user and possibly
   all his tables */
	freedbf(ddic->userstbl->df, telldbf(ddic->userstbl->df));
	return 0;
}

/******************************************************************/

int
changeuser(ddic, user, pass)
DDIC *ddic;
char *user;
char *pass;
{
	struct passwd *pwd;
	char	*npsswd;
	int	invalid = 1;
	TBL	*tbl;
	FLD	*f;
	size_t	len;
	const char	*salt;

	tbl = createusertbl(ddic);
	if (!tbl) return(-1);
	if(iamsystem(ddic))
		invalid = 0;
	pwd = gettxpwname(ddic, user);
	if (!pwd)
	{
		putmsg(MERR, __FUNCTION__, "Could not find user `%s'", user);
		return -1;
	}
	if (invalid && strcmp(TXgetusername(ddic), user))
	{
		putmsg(MERR, __FUNCTION__, "Not allowed to change password");
		return -1;
	}
	f = nametofld(tbl, "U_PASSWD");
	/* Fall back to DES if SYSUSERS has not been upgraded,
	 * so that encrypted password fits:
	 */
	salt = NULL;
	if (!fldisvar(f)) salt = TX_PWENCRYPT_SALT_STR_DES;
	npsswd = TXpwEncrypt(pass, salt);
	if (!npsswd) return(-1);
	len = strlen(npsswd);
	if (len > MAX_FLD_LEN(f))
	{
		tooLongMsg(TXPMBUFPN, __FUNCTION__, ddic->epname, TXbool_True);
		return(-1);
	}
	putfld(f, npsswd, len);
	/* wtf assume we are still pointing at `user''s entry: */
	puttblrow(tbl, telltbl(tbl));
	npsswd = TXfree(npsswd);
	return 0;
}

/******************************************************************/

char *
TXpermModeToStr(buf, sz, permMode)
char	*buf;		/* (out) buffer to write to */
size_t	sz;		/* (in) `buf' size */
int	permMode;	/* (in) PM_... permission mode flags */
/* Converts PM_... permission mode flags `permMode' to human-readable
 * format in `buf'.  Returns `buf'.
 */
{
	CONST char	fmt[] = "%s";
	char		*d, *e;

	e = buf + sz;
	d = buf;
#define CheckFlag(pm, s)			\
	if ((permMode & (pm)) && d < e)		\
	{					\
		if (d > buf) *(d++) = ',';	\
		d += htsnpf(d, e - d, fmt, s);	\
	}

	if ((permMode & PM_ALLPERMS) == PM_ALLPERMS && d < e)
		d += htsnpf(d, e - d, "full access");
	else
	{
		CheckFlag(PM_ALTER, "alter");
		CheckFlag(PM_DELETE, "delete");
		CheckFlag(PM_INDEX, "index");
		CheckFlag(PM_INSERT, "insert");
		CheckFlag(PM_SELECT, "select");
		CheckFlag(PM_UPDATE, "update");
		CheckFlag(PM_REFERENCES, "references");
		CheckFlag(PM_GRANT, "grant");
		CheckFlag(PM_GRANTOPT, "grantopt");
	}
	CheckFlag(PM_OPEN, "open");
	CheckFlag(PM_CREATE, "create");
	if (d >= e && sz > 3) strcpy(buf + sz - 4, "...");
	if (sz > (size_t)0) buf[sz - 1] = '\0';
	return(buf);
#undef CheckFlag
}
