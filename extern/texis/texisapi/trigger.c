/* -=- kai-mode: john -=- */
/*
 * WTF: get field names correct
 * WTF: on before insert, make sure we get rows if inserting from query
 * WTF: allow changes to table to be reflected back to operation if before
 */

#include "txcoreconfig.h"
#ifdef _WIN32
#  include <io.h>
#endif
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "dbquery.h"
#include "texint.h"

#ifndef SZCMDLINE
#define SZCMDLINE	4096
#endif

static char cmdline[SZCMDLINE];

int TXexttrig = 0;

/******************************************************************/

int
createtrigtbl(ddic)
DDIC *ddic;
{
	static CONST char Fn[] = "createtrigtbl";
	DD *dd;
	TBL *tbl;
	DBTBL *dbtbl;
	char	fname[PATH_MAX];

	if (ddic->trigtbl)
		return 0;
	dd = opendd();
	(void)ddsettype(dd, TEXIS_FAST_TABLE);
	if (!dd)
	{
		putmsg(MERR + MAE, Fn, strerror(ENOMEM));
		return -1;
	}
	putdd(dd, "TR_NAME", "varchar", 20, 0);
	putdd(dd, "TR_TIME", "varchar", 20, 0);
	putdd(dd, "TR_EVENT", "varchar", 20, 0);
	putdd(dd, "TBNAME", "varchar", 20, 0);
	putdd(dd, "TR_ORDER", "int", 1, 0);
	putdd(dd, "TR_REFS", "varchar", 20, 0);
	putdd(dd, "TR_TYPE", "int", 1, 0);
	putdd(dd, "TR_ACTION", "varchar", 80, 0);
	putdd(dd, "TR_EXTRA", "varbyte", 80, 0);
#if 0
	strcpy(fname, ddic->pname);
	strcat(fname, TEXISFNSYSTRIG);
#else
	TXstrncpy(fname, TEXISFNSYSTRIG, sizeof(fname));
#endif
	dbtbl =
		createdbtbl(ddic, dd, fname, TEXISSYSTRIG, "Texis Triggers",
			    'S');
	dbtbl = closedbtbl(dbtbl);
	if(!ddic->trigtbl)
	{
		char	*full;

		full = TXstrcat2(ddic->pname, TEXISFNSYSTRIG);
		if (full)
		{
			tbl = opentbl(ddic->pmbuf, full);
			ddic->trigtbl = tbl;
			full = TXfree(full);
		}
	}
	dd = closedd(dd);
	return 0;
}

/******************************************************************/

int
createtrigger(ddic, name, tr_time, tr_event, tbname, order, ref, action)
DDIC *ddic;			/* The Data Dictionary */
char *name;			/* Name of the trigger */
char *tr_time;			/* When the trigger should take place */
char *tr_event;			/* Event causing the trigger */
char *tbname;			/* Table to apply the table to */
char *order;			/* Resolver for multiple triggers at same time. */
char *ref;			/* References field */
char *action;			/* What to do */
{
	static CONST char Fn[] = "create trigger";
	FLD *fname, *ftim, *fevent, *ftable, *forder, *frefs, *ftype,

		*faction;
	FLD *fextra;
	TBL *trigtbl;
	ft_int type = (ft_int)0;
	size_t sz;
	char *p, *t;

	if (!ddic->trigtbl)
		if (-1 == createtrigtbl(ddic))
			return -1;
	trigtbl = ddic->trigtbl;
	fname = nametofld(trigtbl, "TR_NAME");
	ftim = nametofld(trigtbl, "TR_TIME");
	fevent = nametofld(trigtbl, "TR_EVENT");
	ftable = nametofld(trigtbl, "TBNAME");
	forder = nametofld(trigtbl, "TR_ORDER");
	frefs = nametofld(trigtbl, "TR_REFS");
	ftype = nametofld(trigtbl, "TR_TYPE");
        if (ftype == FLDPN || (ftype->type & FTN_VarBaseTypeMask) != FTN_INT)
	{
		putmsg(MERR + UGE, Fn,
			"SYSTRIG(TR_TYPE) has unexpected type %d",
			(int)ftype->type);
		return(-1);
	}
	faction = nametofld(trigtbl, "TR_ACTION");
	fextra = nametofld(trigtbl, "TR_EXTRA");
	if (TXlocksystbl(ddic, SYSTBL_TRIGGER, R_LCK, NULL) == -1)
		return(-1);
	rewindtbl(trigtbl);
	while (TXrecidvalid(gettblrow(trigtbl, NULL)))
	{
		if (!strcmp(name, getfld(fname, &sz)))
		{
			putmsg(MWARN + UGE, "create trigger",
			       "Trigger %s already exists", name);
			TXunlocksystbl(ddic, SYSTBL_TRIGGER, R_LCK);
			return -1;
		}
	}
	TXunlocksystbl(ddic, SYSTBL_TRIGGER, R_LCK);
	putfld(fname, name, strlen(name));
	putfld(ftim, tr_time, strlen(tr_time));
	putfld(fevent, tr_event, strlen(tr_event));
	putfld(ftable, tbname, strlen(tbname));
	putfld(forder, order, strlen(order));
	putfld(frefs, ref, strlen(ref));

	if (!strncmp(action, "ROW ", 4))
	{
		type |= (ft_int)TRG_ROW;
		p = action + 4;
	}
	else if (!strncmp(action, "STATEMENT ", 10))
	{
		type |= (ft_int)TRG_STATEMENT;
		p = action + 10;
		if ((tr_event[0] != 'I') || (tr_time[0] != 'B'))
		{
			putmsg(MWARN + UGE, "create trigger",
			       "<Not Yet Implemented> use FOR EACH ROW or BEFORE INSERT");
			return -1;
		}
	}
	else
	{
		return -1;
	}
	if (!strncmp(p, "SHELL ", 6))
	{
		p += 7;
		/* sco optimizer can't do p[strlen(p)-1]='\0' */
		for (t = p; *t != '\0'; t++);
		t--;
		*t = '\0';
		type |= (ft_int)TRG_EXTERNAL;
		if (TXexttrig)
			type |= (ft_int)TRG_EXTERNAL2;
	}
	else
	{
		type |= (ft_int)TRG_INTERNAL;
	}
	putfld(ftype, &type, 1);
	putfld(faction, p, strlen(p));
	putfld(fextra, "", 0);
	if (TXlocksystbl(ddic, SYSTBL_TRIGGER, W_LCK, NULL) == -1)
		return(-1);
	puttblrow(ddic->trigtbl, NULL);
	TXunlocksystbl(ddic, SYSTBL_TRIGGER, W_LCK);
	return 0;
}

/******************************************************************/

int
TXdroptrigger(ddic, name)
DDIC *ddic;
char *name;
{
	TBL *trigtbl;
	FLD *fldn;
	size_t sz;

	if (!ddic)
		return -1;
	trigtbl = ddic->trigtbl;
	if (!trigtbl)
		return -1;
	fldn = nametofld(trigtbl, "TR_NAME");
	if (TXlocksystbl(ddic, SYSTBL_TRIGGER, W_LCK, NULL) == -1)
		return(-1);
	rewindtbl(trigtbl);
	while (TXrecidvalid(gettblrow(trigtbl, NULL)))
	{
		if (!strcmp(name, getfld(fldn, &sz)))
		{
			freedbf(trigtbl->df, telldbf(trigtbl->df));
			TXunlocksystbl(ddic, SYSTBL_TRIGGER, W_LCK);
			return 0;
		}
	}
	TXunlocksystbl(ddic, SYSTBL_TRIGGER, W_LCK);
	putmsg(MWARN + UGE, "drop trigger", "%s does not appear to exist",
	       name);
	return -1;
}

/******************************************************************/

static IITRIGGER *closeiitrig ARGS((IITRIGGER *));

static IITRIGGER *
closeiitrig(trig)
IITRIGGER *trig;
{
	IIITRIGGER *p, *n;

	if (trig)
	{
		p = trig->row;
		while (p)
		{
			n = p->next;
			if (p->when)
				free(p->when);
			if (p->action)
				free(p->action);
			free(p);
			p = n;
		}
		p = trig->statement;
		while (p)
		{
			n = p->next;
			if (p->when)
				free(p->when);
			if (p->action)
				free(p->action);
			free(p);
			p = n;
		}
		free(trig);
	}
	return NULL;
}

/******************************************************************/

static ITRIGGER *closeitrig ARGS((ITRIGGER *));

static ITRIGGER *
closeitrig(trig)
ITRIGGER *trig;
{
	if (trig)
	{
		if (trig->before)
			closeiitrig(trig->before);
		if (trig->after)
			closeiitrig(trig->after);
		if (trig->instead)
			closeiitrig(trig->instead);
		free(trig);
	}
	return NULL;
}

/******************************************************************/

TRIGGER *
closetrigger(trig)
TRIGGER *trig;
{
	if (trig)
	{
		if (trig->insert)
			closeitrig(trig->insert);
		if (trig->update)
			closeitrig(trig->update);
		if (trig->deltrg)
			closeitrig(trig->deltrg);
		free(trig);
	}
	return NULL;
}

/******************************************************************/

static IITRIGGER *triginsert2 ARGS((IITRIGGER *, IIITRIGGER *));

static IITRIGGER *
triginsert2(it, iit)
IITRIGGER *it;
IIITRIGGER *iit;
{
	IIITRIGGER *p;

	if (!it)
	{
		it = (IITRIGGER *) calloc(1, sizeof(IITRIGGER));
		if (!it)
			return it;
	}
	if (iit->type & TRG_STATEMENT)
	{
		p = it->statement;
		if (!p)
		{
			iit->next = NULL;
			iit->prev = NULL;
			it->statement = iit;
			return it;
		}
	}
	else if (iit->type & TRG_ROW)
	{
		p = it->row;
		if (!p)
		{
			iit->next = NULL;
			iit->prev = NULL;
			it->row = iit;
			return it;
		}
	}
	else
		return NULL;
	return it;
}

/******************************************************************/

static int triginsert ARGS((ITRIGGER *, IIITRIGGER *));

static int
triginsert(it, iit)
ITRIGGER *it;
IIITRIGGER *iit;
{
	switch (iit->when[0])
	{
	case 'B':
		it->before = triginsert2(it->before, iit);
		break;
	case 'A':
		it->after = triginsert2(it->after, iit);
		break;
	case 'I':
		it->instead = triginsert2(it->instead, iit);
		break;
	}
	return 0;
}

/******************************************************************/
/*
 *	Table - Insert
 *		Update
 *		Delete
 *		Select?
 *
 *	Op    -	Row
 *	 	Statement
 *
 *	RS    - Linked list of triggers.  (type, action)
 */

int
opentrigger(ddic, tb)
DDIC *ddic;
DBTBL *tb;
{
	TBL *ttbl;
	FLD *f_name, *f_event, *f_action, *f_type, *f_when;
	size_t sz;

	if (tb->trigger)
		tb->trigger = closetrigger(tb->trigger);
	if (ddic->options[DDIC_OPTIONS_NO_TRIGGERS])
		return 0;
	ttbl = ddic->trigtbl;
	if (!ttbl)
		return 0;
	f_name = nametofld(ttbl, "TBNAME");
	f_event = nametofld(ttbl, "TR_EVENT");
	f_action = nametofld(ttbl, "TR_ACTION");
	f_type = nametofld(ttbl, "TR_TYPE");
	f_when = nametofld(ttbl, "TR_TIME");
	tb->trigger = (TRIGGER *) calloc(1, sizeof(TRIGGER));
	if (!tb->trigger)
		return -1;
	if (TXlocksystbl(ddic, SYSTBL_TRIGGER, R_LCK, NULL) == -1)
		return(-1);
	rewindtbl(ttbl);
	while (TXrecidvalid(gettblrow(ttbl, NULL)))
	{
		if (!strcmp(tb->rname, getfld(f_name, &sz)))
		{
			char *event;
			ITRIGGER *t;
			IIITRIGGER *it;

			event = getfld(f_event, &sz);
			switch (event[0])
			{
			case 'I':
				t = tb->trigger->insert;
				if (!t)
				{
					t = tb->trigger->insert =
						(ITRIGGER *) calloc(1,
								    sizeof
								    (ITRIGGER));
					if (!t)
					{
						TXunlocksystbl(ddic,
							       SYSTBL_TRIGGER,
							       R_LCK);
						return -1;
					}
				}
				break;
			case 'U':
				t = tb->trigger->update;
				if (!t)
				{
					t = tb->trigger->update =
						(ITRIGGER *) calloc(1,
								    sizeof
								    (ITRIGGER));
					if (!t)
					{
						TXunlocksystbl(ddic,
							       SYSTBL_TRIGGER,
							       R_LCK);
						return -1;
					}
				}
				break;
			case 'D':
				t = tb->trigger->deltrg;
				if (!t)
				{
					t = tb->trigger->deltrg =
						(ITRIGGER *) calloc(1,
								    sizeof
								    (ITRIGGER));
					if (!t)
					{
						TXunlocksystbl(ddic,
							       SYSTBL_TRIGGER,
							       R_LCK);
						return -1;
					}
				}
				break;
			default:
				TXunlocksystbl(ddic, SYSTBL_TRIGGER, R_LCK);
				return -1;
				break;
			}
			it = (IIITRIGGER *) calloc(1, sizeof(IIITRIGGER));
			if (!it)
			{
				TXunlocksystbl(ddic, SYSTBL_TRIGGER, R_LCK);
				return -1;
			}
			it->action = strdup(getfld(f_action, &sz));
			it->when = strdup(getfld(f_when, &sz));
			it->type = *(ft_int *) (getfld(f_type, &sz));
			it->ddic = ddic;
			triginsert(t, it);
		}
	}
	TXunlocksystbl(ddic, SYSTBL_TRIGGER, R_LCK);
	return 0;
}

/******************************************************************/
/*	Here we just want to copy every single item from the
 *	original dbf to the new dbf.  Note that it will be compacted,
 *	i.e no free list.  Or we copy just a single row, if `row'.
 */

static char *copydbtbl ARGS((DBTBL *, FLDOP * fo, int row));

static char *
copydbtbl(dbtbl, fo, row)
DBTBL *dbtbl;
FLDOP *fo;
int	row;	/* non-zero: one row only */
{
	static CONST char	Fn[] = "copydbtbl";
	DBTBL *t = NULL;
	DD *dd = DDPN;
	DBF *df = NULL, *bf = NULL;
	TBL *tbl;
	RECID where;
	char *x = CHARPN, *bfname = CHARPN, *ts = CHARPN;
	RECID *v;
	int	i;

	if ((dd = TXbiddc(tbldd(dbtbl->tbl))) == DDPN) goto err;

	/* KNG 20060109 Do not re-try forever.  Also let DBF do the
	 * raw file create/open, not us:
	 */
	for (i = 0; i < 25; i++, free(ts), ts = CHARPN,
		free(x), x = CHARPN, free(bfname), bfname = CHARPN)
	{
		if ((ts = tempfn(NULL, "trig")) == CHARPN) goto err;
		if ((x = TXstrcat2(ts, ".tbl")) == CHARPN) goto err;
		if ((bfname = TXstrcat2(ts, ".blb")) == CHARPN) goto err;
		/* While the O_EXCL flag to opendbf() will atomically
		 * prevent us from stomping on an existing file,
		 * it will also yap, so try to avoid it silently w/access().
		 * Also, even if we don't have blobs, there shouldn't be
		 * an existing .blb file to avoid confusion:
		 */
		if (access(x, 0) == 0 || access(bfname, 0) == 0) continue;
		if (ddgetblobs(dd))		/* we have blob(s) */
		{
			bf = opendbf(dbtbl->ddic->pmbuf, bfname,
				     (O_RDWR | O_CREAT | O_EXCL));
			if(!bf)	continue;	/* not exclusively created */
		}
		df = opendbf(dbtbl->ddic->pmbuf, x,
			     (O_RDWR | O_CREAT | O_EXCL));
		if(!df)	goto cont1;		/* not exclusively created */
		tbl = TXcreatetbl_dbf(dd, df, bf);
		if(!tbl)
		{
			df = closedbf(df);
			deldbf(x);		/* we created it */
		cont1:
			if(bf)
			{
				bf = closedbf(bf);
				deldbf(bfname);	/* we created it */
			}
			continue;
		}
		df = bf = NULL;			/* owned by `tbl' now */
		t = TXopentmpdbtbl_tbl(tbl, NULL, NULL, dbtbl->ddic);
		if (!t)
		{
			putmsg(MWARN+FME, NULL, "Unable to create temp file");
			tbl = closetbl(tbl);
			goto err;
		}
		tbl = NULL;			/* owned by `t' */
		break;				/* success */
	}
	dd = closedd(dd);
	if (x == CHARPN)			/* failed after 25 tries */
	{
		putmsg(MERR + FOE, Fn,
			"Cannot create unique temp trigger table");
		goto err;
	}
	free(ts);
	ts = CHARPN;
	where = dbtbl->recid;
	if (row)				/* single row only */
	{
		if (TXgetoff(&where) == (EPI_OFF_T)0) goto done;
		tup_write(t, dbtbl, fo, 1);
	}
	else					/* whole table */
	{
		TXrewinddbtbl(dbtbl);
		v = getdbtblrow(dbtbl);
		while (v)
		{
			tup_write(t, dbtbl, fo, 1);
			v = getdbtblrow(dbtbl);
		}
		if (TXgetoff(&where) == (EPI_OFF_T)0)
			TXrewinddbtbl(dbtbl);
		else
			TXmygettblrow(dbtbl, &where);
	}
	goto done;				/* success */

err:
	if (x != CHARPN) free(x);
	x = CHARPN;
done:
	t = closedbtbl(t);
	df = closedbf(df);
	bf = closedbf(bf);
	if (bfname != CHARPN) free(bfname);
	if (ts != CHARPN) free(ts);
	dd = closedd(dd);
	return x;
}

/******************************************************************/

static char *
gencmdline(trig, rmfn, fn, table)
IIITRIGGER *trig;
int rmfn;
char *fn;
TBL *table;
{
	char *t, *d;
	char *rc = NULL;
	char *lname;
	char *user;
	DD *dd = NULL;
	DBTBL dbtbl;

	if (table)
		dd = table->dd;
	if (trig->type & TRG_EXTERNAL2)
	{
		if (rmfn)
		{
			user = TXgetusername(trig->ddic);
			for (t = fn; strstr(t, PATH_SEP_S); t++);
			lname = strdup(t);
			strstr(lname, ".tbl")[0] = '\0';
			for (t = lname; *t; t++)
				if (*t == '.')
					*t = '_';
			d = strdup(fn);
			strstr(d, ".tbl")[0] = '\0';
			addtable(trig->ddic, lname, user,
				 "Temporary Trigger Table", d, dd, 0, 'T');
			if (d)
				free(d);
			dbtbl.tbl = table;
			dbtbl.rname = lname;
			permgrantdef(trig->ddic, &dbtbl);
			for (t = trig->action, d = cmdline; *t; t++, d++)
			{
				if (!strncmp(t, "$db", 3))
				{
					strcpy(d, trig->ddic->pname);
					t += 2;
					d += strlen(trig->ddic->pname) - 1;
				}
				else if (!strncmp(t, "$table", 6))
				{
					strcpy(d, lname);
					t += 5;
					d += strlen(lname) - 1;
				}
				else
					*d = *t;
			}
			*d = '\0';
			rc = lname;
		}
	}
	else
	{
		TXstrncpy(cmdline, trig->action, SZCMDLINE);
		if (fn)
		{
			strcat(cmdline, " ");
			strcat(cmdline, fn);
		}
	}
	return rc;
}

/******************************************************************/

int
trigexec(trig, dbtbl, fo)
IIITRIGGER *trig;
DBTBL *dbtbl;
FLDOP *fo;
{
	char *fn;
	char *tbname = NULL;
	int rmfn = 0;
	IIITRIGGER *c;
	TBL *table = NULL;

	c = trig;
	if (dbtbl)
		table = dbtbl->tbl;
	if (!table)
	{
		fn = NULL;
	}
	else
	{
		fn = getdbffn(table->df);
		if (c && (c->type & TRG_STATEMENT))
		{
			fn = copydbtbl(dbtbl, fo, 0);
			if (!fn)
				return -1;
			rmfn++;
		}
		if (c && (c->type & TRG_ROW))
		{
			fn = copydbtbl(dbtbl, fo, 1);
			if (!fn)
				return -1;
			rmfn++;
		}
	}
	while (c)
	{
		tbname = gencmdline(trig, rmfn, fn, table);
		if (trig->type & TRG_EXTERNAL)
		{
			fflush(stdout);
			fflush(stderr);
			TXsystem(cmdline);
			fflush(stdout);
			fflush(stderr);
		}
		c = c->next;
	}
	if (rmfn && fn)
	{
		if (tbname)
		{
			TXdropdtable(trig->ddic, tbname);
			free(tbname);
		}
		deldbf(fn);
		free(fn);
	}
	return 0;
}
