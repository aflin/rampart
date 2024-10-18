/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"
#include "fldmath.h"

/****************************************************************************/
/*
 *	Generate a table to hold the result of a Cartesian Product.
 */

DBTBL *
TXtup_product_setup(tbl1, tbl2, rewindtbls, slist)
DBTBL *tbl1;
DBTBL *tbl2;
int    rewindtbls;	/* Should we rewind the tables */
SLIST *slist;		/* List of fields we might be interested in */
{
	DD *dd;
	DBTBL *dbt;
	int  i;
	char *rc = NULL;
	int found = 0;
	char *fname, fqname[80];

	i = ddgetnfields(tbl1->tbl->dd) + ddgetnfields(tbl2->tbl->dd) + 3;
	dd = opennewdd(i);
        if(!dd)
         return (DBTBL *)NULL;
	dbt = (DBTBL *)calloc(1, sizeof(DBTBL));
	(void)ddsettype(dd, 1);
	if(rewindtbls)
	{
		TXrewinddbtbl(tbl1);
		TXrewinddbtbl(tbl2);
	}
	i = 0;
	if(tbl1->lname && tbl1->lname[0])
	{
		strcpy(fqname, tbl1->lname);
		strcat(fqname, ".$recid");
		DBGMSG(1, (999, NULL, "Adding field %s", fqname));
		if (!putdd(dd, fqname, "long", 1, 1))
		{
			putmsg(MERR, NULL, "Could not add field %s, you may have too long table/field names.  Try aliasing table names.", fqname);
			closedd(dd);
			free(dbt);
			return NULL;
		}
	}
	fname = getfldname(tbl1->tbl, i++);
	while (fname != (char *)NULL)
	{
		if(slist)
		{
			char *x;
			x = strchr(fname, '.');
			if(x)
				x++;
			else
				x = fname;
			found = 0;
			rc = slfind(slist, x);
			if(rc)
				found ++;
		}
		else
			found = 1;
		if (strchr(fname, '.'))
			strcpy(fqname, fname);
		else
		{
			strcpy(fqname, tbl1->lname);
			strcat(fqname, ".");
			strcat(fqname, fname);
		}
		DBGMSG(1, (999, NULL, "Adding field %s", fqname));
		if(slist)
			rc = slfind(slist, fqname);
		if(rc)
			found ++;
		if(found)
		if(!copydd(dd, fqname, tbl1->tbl, fname, 0))
		{
			putmsg(MERR, NULL, "Could not add field %s, you may have too long table/field names.  Try aliasing table names.", fqname);
			closedd(dd);
			free(dbt);
			return NULL;
		}
		fname = getfldname(tbl1->tbl, i++);
	}
	i = 0;
	if (tbl2->lname && tbl2->lname[0])
	{
		strcpy(fqname, tbl2->lname);
		strcat(fqname, ".$recid");
		DBGMSG(1, (999, NULL, "Adding field %s", fqname));
		if(!putdd(dd, fqname, "long", 1, 1))
		{
			putmsg(MERR, NULL, "Could not add field %s, you may have too long table/field names.  Try aliasing table names.", fqname);
			closedd(dd);
			free(dbt);
			return NULL;
		}
	}
	fname = getfldname(tbl2->tbl, i++);
	while (fname != (char *)NULL)
	{
		if(slist)
		{
			char *x;
			x = strchr(fname, '.');
			if(x)
				x++;
			else
				x = fname;
			found = 0;
			rc = slfind(slist, x);
			if(rc)
				found ++;
		}
		else
			found = 1;
		if (strchr(fname, '.'))
			strcpy(fqname, fname);
		else
		{
			strcpy(fqname, tbl2->lname);
			strcat(fqname, ".");
			strcat(fqname, fname);
		}
		DBGMSG(1, (999, NULL, "Adding field %s", fqname));
		if(slist)
		{
			rc = slfind(slist, fqname);
			if(rc)
				found++;
		}
		if(found)
		if(!copydd(dd, fqname, tbl2->tbl, fname, 0))
		{
			putmsg(MERR, NULL, "Could not add field %s, you may have too long table/field names.  Try aliasing table names.", fqname);
			closedd(dd);
			free(dbt);
			return NULL;
		}
		fname = getfldname(tbl2->tbl, i++);
	}
	/* KNG wtf changing `lname' to a descriptive name affects
	 * Texis test577:
	 */
	dbt->lname = strdup("");

	dbt->rname = NULL;
	dbt->tbl = createtbl(dd, NULL);
	dbt->type = 'T';
	dbt->ddic = tbl1->ddic;
	dbt->frecid = createfld("recid", 1, 0);
	dbt->tblid = -1;
	putfld(dbt->frecid, &dbt->recid, 1);
	closedd(dd);
	return dbt;
}

/****************************************************************************/
/*
 *	Perform a cartesion Product
 */

DBTBL *
tup_product(qnode, t1, t2, tout, pred, proj, fo)
QNODE	*qnode;	/* (out) rowsMatchedMax etc. stats updated */
DBTBL *t1;
DBTBL *t2;
DBTBL *tout;
PRED *pred;
PROJ *proj;
FLDOP *fo;
{
	DBTBL *tproj = NULL;
	PRED *pred1;
	FLD **in1, **in2, **fout;
	int  i, n1, n2, nout, j;
	size_t sz;
	void *v;
	char *name, fqname[80];

/*	Open Tables */

	TXrewinddbtbl(t1);
	TXrewinddbtbl(t2);
	TXrewinddbtbl(tout);
	if (proj != (PROJ *)NULL)
	{
		tproj = tout;
		tout = TXtup_product_setup(t1, t2, 0, NULL);
		/* Don't need to rewind, as did it earlier */
		if (!tout)
			return tout;
		TXrewinddbtbl(tout);
	}

/*	Allocate field pointers */

	n1 = ntblflds(t1->tbl);
	in1 = (FLD **)calloc(n1+1, sizeof(FLD *));
	n2 = ntblflds(t2->tbl);
	in2 = (FLD **)calloc(n2+1, sizeof(FLD *));
	nout = ntblflds(tout->tbl);
	fout = (FLD **)calloc(n1+n2+2, sizeof(FLD *));

/*	Set up field pointers */

	j = 0;
	for (i = 0;
		(name = getfldname(t1->tbl, i)) != (char *)NULL;
		i++)
	{
		in1[i] = dbnametofld(t1, name);
		fout[j] = dbnametofld(tout, name);
		if (fout[j] == (FLD *) NULL)
		{
			strcpy(fqname, t1->lname);
			strcat(fqname, ".");
			strcat(fqname, name);
			fout[j] = dbnametofld(tout, fqname);
		}
		j++;
	}
	if (t1->lname)
	{
		n1++;
		in1[i]=dbnametofld(t1, "$recid");
		strcpy(fqname, t1->lname);
		strcat(fqname, ".$recid");
		fout[j++] = dbnametofld(tout, fqname);
	}
	for (i = 0;
		(name = getfldname(t2->tbl, i)) != (char *)NULL;
		i++)
	{
		in2[i] = dbnametofld(t2, name);
		fout[j] = dbnametofld(tout, name);
		if (fout[j] == (FLD *) NULL)
		{
			strcpy(fqname, t2->lname);
			strcat(fqname, ".");
			strcat(fqname, name);
			fout[j] = dbnametofld(tout, fqname);
		}
		j++;
	}
	if (t2->lname)
	{
		n2++;
		in2[i]=dbnametofld(t2, "$recid");
		strcpy(fqname, t2->lname);
		strcat(fqname, ".$recid");
		fout[j++] = dbnametofld(tout, fqname);
	}

/*	Get the product */

	TXrewinddbtbl(t1);
	TXsettablepred(qnode, t1, pred, (PROJ *)NULL, fo, 1, NULL, NULL);
	while (tup_read(t1, fo, SQL_FETCH_NEXT, 1, NULL, TXCOUNTINFOPN)
		!= DBTBLPN)
	{
		DBGMSG(1, (999, NULL, "Read following row from %s", t1->lname));
#ifdef DEBUG
#ifdef NEVER
		tup_disp(t1, fo);
#endif
#endif
		for (i=0; i < n1; i++)
		{
			if (fout[i])
			{
				v = getfld(in1[i], &sz);
				putfld(fout[i], v, sz);
			}
		}
		TXrewinddbtbl(t2);
		pred1 = substpred(pred, t1);
		TXsettablepred(qnode, t2, pred1, (PROJ *)NULL, fo, 1, NULL, NULL);
		while (tup_read(t2, fo, SQL_FETCH_NEXT, 1, NULL, TXCOUNTINFOPN)
			!= DBTBLPN)
		{
			for (i=0; i < n2; i++)
			{
				if (fout[i+n1])
				{
					v = getfld(in2[i], &sz);
					putfld(fout[i+n1], v, sz);
				}
			}
			if (tup_match(tout, pred, fo)>0)
			{
				if (proj)
				{
					tup_project(tout, tproj, proj, fo);
				}
				else
				{
					putdbtblrow(tout, NULL);
				}
			}
		}
		TXsettablepred(qnode, t2, (PRED *)NULL, (PROJ *)NULL, fo, 1, NULL, NULL);
		closepred(pred1);
	}

/*	Cleanup */

	TXsettablepred(qnode, t1, (PRED *)NULL, (PROJ *)NULL, fo, 1, NULL, NULL);
	free(fout);
	free(in2);
	free(in1);
	if (proj != (PROJ *)NULL)
	{
		deltable(tout);
		return tproj;
	}
	else
		return tout;
}

/****************************************************************************/
