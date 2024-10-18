/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifdef USE_EPI
#include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"

/******************************************************************/

char **
TXgetupdfields(t, u)
DBTBL	*t;
UPDATE	*u;
{
	char **rc;
	int nfields;
	UPDATE *tu;

	for(tu = u, nfields = 0; tu; tu = tu->next)
		nfields++;
	if (nfields == 0)
		return 0;
	rc = (char **)calloc(nfields + 2, sizeof(char *));
	if(!rc) return rc;
	for(tu = u, nfields = 0; tu; tu = tu->next)
	{
		rc[nfields++] = tu->field;
		if(tu->fld && isddvar(tu->fld->type))
		{
/* If we're updating a var field the size could change, so we may
   need to update all indices */
			free(rc);
			return NULL;
		}
	}
	qsort(rc, nfields, sizeof(char *), TXqstrcmp);
	rc[nfields++] = "";
	rc[nfields++] = NULL;
	return rc;
}

/******************************************************************/

#ifdef NEVER
int
showupdfields(t, u)
DBTBL	*t;
UPDATE	*u;
{
	static	char	Fn[] = "update";
	int rc = 0;

	if (u == (UPDATE *)NULL)
		return 0;
	if(isddvar(u->fld->type))
		putmsg(MINFO, Fn ,"All fields may be updated");
	else
		putmsg(MINFO, Fn ,"Field %s will be updated", u->field);
	if (u->next != (UPDATE *)NULL)
		showupdfields(t, u->next);
	return rc;
}
#endif

/******************************************************************/
/* Do a precomputation to get the field pointers from field names
 * for all the fields that will be updated.
 */

int
setupdfields(t, u)
DBTBL	*t;
UPDATE	*u;
{
	static	char	Fn[] = "update";
	int rc = 0;

/* WTF - add a flag if we've already done this ? */
	if (u == (UPDATE *)NULL)
		return 0;
	u->fld = dbnametofld(t, u->field);
	if((!u->fld) && (!t->ddic->options[DDIC_OPTIONS_IGNORE_MISSING_FIELDS]))
	{
		putmsg(MWARN, Fn ,"Field %s does not exist", u->field);
		rc = -1;
	}
	if (u->next != (UPDATE *)NULL)
	{
		if(setupdfields(t, u->next)==-1)
			rc = -1;
	}
	return rc;
}

/******************************************************************/
/* Update fields in t with values obtained from the UPDATE
 * structure u. u->expr contains the expression that will evaluate
 * to the new value.
 */

int
updatefields(t, u, fo, updchar)
DBTBL	*t;
UPDATE	*u;
FLDOP	*fo;
int	*updchar;
{
	FLD	*r;
	void	*v;
	PRED	*p;
	int	rc = 0;

	if (u == (UPDATE *)NULL)
		return 0;
	p = substpred(u->expr, t);
	if(updchar && u->fld &&
		      (((u->fld->type & DDTYPEBITS) == FTN_CHAR) ||
		       ((u->fld->type & DDTYPEBITS) == FTN_INDIRECT) ||
		       ((u->fld->type & DDTYPEBITS) == FTN_BLOBI))) 
		*updchar = 1;
	if(pred_eval(t, p, fo) == -1)
	{
		closepred(p);
		fodisc(fo);
		return -1;
	}
	r = fopop(fo);
	if ((r != (FLD *)NULL) && (u->fld != (FLD *)NULL))
	{
		if (r->type == FTN_COUNTERI && u->fld->type == FTN_COUNTER)
		{
			v = getcounter(t->ddic);
			memcpy(u->fld->v, v, sizeof(ft_counter));
			free(v);
		}
		else
		{
			if (TXfldbasetype(u->fld) == FTN_BLOBI)
			{
				ft_blobi *v = getfld(u->fld, NULL);
				u->delblob = *v;
				TXblobiSetDbf(v, NULL);
				TXblobiSetMem(v, NULL, 0, 0);
				v->off = 0;
			}
			rc = _fldcopy(r, NULL, u->fld, NULL, fo);
		}
		closefld(r);
	}
	closepred(p);
	if (u->next != (UPDATE *)NULL)
		return rc + updatefields(t, u->next, fo, updchar);
	return rc;
}
