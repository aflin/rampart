/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <errno.h>
#include <stdio.h>
#if defined(unix) || defined(__unix)	/* MAW 02-15-94 wtf? groups on nt? */
#  include <grp.h>
#  include <unistd.h>
#endif /* unix || __unix */
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include "os.h"
#include "dbquery.h"
#include "texint.h"
#include "txlic.h"

static char tempbuf[1024];

static int TXstmthits = 0;

#if defined(unix) && !defined(_AIX)
int fchmod ARGS((int, mode_t));

#endif

#ifndef HAVE_STRERROR
#define strerror(a)  sys_errlist[a]
#endif

/******************************************************************/

static int revunixperms ARGS((QNODE *));

static int
revunixperms(query)
QNODE *query;
{
	(void)query;
#ifdef NEVER
	QUERY *q;
	int factor;
	int fd;
	int nperms;
	char *np;
	EPI_STAT_S stb;

	q = query->q;
	q->newmode = 0;
	if (!strcmp(query->tname, "PUBLIC"))
	{
		q->group = -1;
		factor = 9;
	}
	else
	{
#ifdef unix			/* MAW 02-15-94 wtf? groups on nt? */
		struct group *gr;

		gr = getgrnam(query->tname);
		if (gr == (struct group *) NULL)
		{
			putmsg(MERR, "Revoke",
			       "No such group %s", query->tname);
			return -1;
		}
		q->group = gr->gr_gid;
		factor = 8;
#else
		q->group = -1;
		factor = 9;
#endif
	}
	np = strtounix(query->left->tname);
	if (strchr(np, 'r'))
		q->newmode = q->newmode | 6;
/* Revoking read also revokes write */
	if (strchr(np, 'w'))
		q->newmode = q->newmode | 2;
	if (np)
		free(np);
	q->newmode *= factor;
	fd = getdbffh(q->in1->tbl->df);		/* MAW 04-04-94 replace drill */
	if (fd != (-1))
	{
		EPI_FSTAT(fd, &stb);
		if (q->group != -1 &&
		    q->group != stb.st_gid)
		{
			putmsg(MINFO, "Revoke",
			       "Specified group currently has no access to this table");
			return -1;
		}
		nperms = (int) (stb.st_mode & (~q->newmode));
#ifdef unix			/* MAW 02-15-94 wtf? groups on nt? */
		if (fchmod(fd, nperms) == -1)
		{
			putmsg(MERR, "Revoke",
			       "Error removing permissions %s",
			       strerror(errno));
		}
#endif
	}
#endif
	return 0;
}
/******************************************************************/

static int dounixperms ARGS((QNODE *));

static int
dounixperms(query)
QNODE *query;
{
	(void)query;
#ifdef NEVER
	QUERY *q;
	int factor;
	int fd;
	int nperms, t;
	char *np;
	EPI_STAT_S stb;

#ifdef unix
	struct group *gr;

#endif

	q = query->q;
	q->newmode = 0;
	if (!strcmp(query->tname, "PUBLIC"))
	{
		q->group = -1;
		factor = 1;
	}
	else
	{
#ifdef unix
		gr = getgrnam(query->tname);
		if (gr == (struct group *) NULL)
		{
			putmsg(MERR, "Grant", "No such group %s", query->tname);
			return -1;
		}
		q->group = gr->gr_gid;
		factor = 8;
#else /* MAW 02-15-94 wtf? groups on nt? */
		q->group = -1;
		factor = 1;
#endif
	}
	np = strtounix(query->left->tname);
	if (strchr(np, 'r'))
		q->newmode = q->newmode | 4;
	if (strchr(np, 'w'))
		q->newmode = q->newmode | 6;
	if (np)
		free(np);
	/* Granting write also grants read */
	q->newmode *= factor;
	fd = getdbffh(q->in1->tbl->df);		/* MAW 04-04-94 replace drill */
	if (fd != (-1))
	{
#ifdef unix			/* MAW 02-15-94 wtf? groups on nt? */
		EPI_FSTAT(fd, &stb);
		gr = getgrgid(stb.st_gid);
		if (q->group != -1 &&
		    q->group != stb.st_gid &&
		    (stb.st_mode & 070))
		{
			putmsg(MERR + UGE, "Grant",
			       "Group %s currently has access to this table.\nYou need to revoke their access first", gr->gr_name);
			return -1;
		}
#endif
		if (q->group != -1)
		{
#ifdef unix			/* MAW 02-15-94 wtf? groups on nt? */
			if (fchown(fd, -1, q->group) == -1)
			{
				putmsg(MERR, "Grant",
				       "Error giving permissions %s",
				       strerror(errno));
				return -1;
			}
#endif
		}
#ifdef NEVER
		else
			putmsg(MINFO, "Grant",
			       "Also granting permissions to group %s",
			       gr->gr_name);
#endif
		nperms = (int) (stb.st_mode | q->newmode);
		t = nperms & 07;	/* Get other's perms */
		t = t << 3;
		nperms |= t;	/* Give group other's perms */
		t = nperms & 070;	/* Get group's perms */
		t = t << 3;
		nperms |= t;	/* Give owner group's perms */
#ifdef unix			/* MAW 02-15-94 wtf? groups on nt? */
		if (fchmod(fd, nperms) == -1)
		{
			putmsg(MERR, "Grant", "Error giving permissions %s",
			       strerror(errno));
			return -1;
		}
		return 0;
#endif /* unix */
	}
	return -1;
#else
	return 0;
#endif
}

/******************************************************************/

#ifdef NEVER
static int propagatepred ARGS((QNODE *, PRED *, FLDOP *));

static int
propagatepred(query, pred, fo)
QNODE *query;
PRED *pred;
FLDOP *fo;
{
#if defined(CACHE_IINODE)
	IINODE *temp;
	PRED *vp;
	char *x;

	switch(query->op)
	{
	case PRODUCT_OP:
		x = disppred(pred, 0, 0);
		putmsg(999, NULL, "Given %s", x);
		if(x) free(x);
		break;
	case NAME_OP:
		x = disppred(pred, 0, 0);
		putmsg(999, NULL, "Given %s", x);
		if(x) free(x);
		vp = TXmakepredvalid(pred, query->q->out, 0, 0, 1);
		if(query->q && query->q->out)
		{
			x = disppred(vp, 0, 0);
			putmsg(999, NULL, "Valid is %s", x);
			if(x) free(x);
		}
		if(!vp)
			return 0;
		temp = TXpredtoiinode(query->q->out, vp, NULL, fo, 1, 1);
		if(!temp->index)
		{
			putmsg(999, NULL, "Denied");
			temp = TXcloseiinode(temp);
		}
		if(temp && temp != query->q->out->cacheinode)
		{
			if(query->q->out->cacheinode)
			{
				closepred(query->q->out->cacheinode->gpred);
				query->q->out->cacheinode =
				    TXcloseiinode(query->q->out->cacheinode);
			}
			temp->gpred = duppred(temp->gpred);
			temp->cached++;
			query->q->out->cacheinode = temp;
		}
	default:
		return 0;
	}
#else
	if (query->op != PRODUCT_OP)
		return 0;
#endif
	if (query->q && query->q->pred == NULL)
	{
		query->q->pred = duppred(TXmakepredvalid(pred, query->q->out, 0, 0, 1));
	}
	propagatepred(query->left, pred, fo);
	propagatepred(query->right, pred, fo);
	return 0;
}
#endif /* NEVER */

/******************************************************************/

#ifdef HAVE_VIEWS
static char *showqnop  ARGS((DDIC *, QNODE *, FLDOP *));
static char *treetosql ARGS((DDIC *, QNODE *, FLDOP *));

static char *
showqnop(ddic, query, fo)
DDIC *ddic;
QNODE *query;
FLDOP *fo;
{
	putmsg(999, NULL, "query->op = %d", query->op);
	switch(query->op)
	{
	case NAME_OP:
		putmsg(999, NULL, "%s", query->tname);
		break;
	case PROJECT_OP:
		putmsg(999, NULL, "select ");
		showqnop(ddic, query->left, fo);
	case SELECT_OP:
		putmsg(999, NULL, "select ");
		showqnop(ddic, query->left, fo);
		putmsg(999, NULL, "where ");
		putmsg(999, NULL, "%s", disppred(TXtreetopred(ddic, query->right, 0, fo, NULL), 0, 0));
		break;
	}
	return NULL;
}

static char *
treetosql(ddic, query, fo)
DDIC *ddic;
QNODE *query;
FLDOP *fo;
{
	showqnop(ddic, query, fo);
	return strdup("SELECT * FROM test1 union SELECT * from test2;");
}
#endif

#ifndef NO_KEEP_STATS

static char *querytotext ARGS((QNODE *query));
static char *
querytotext(query)
QNODE *query;
{

	switch(query->op)
	{
	case LIST_OP:
		strcat(tempbuf, ",");
		querytotext(query->left);
		querytotext(query->right);
		break;
	case NAME_OP:
		if(strlen(tempbuf) && tempbuf[strlen(tempbuf)-1] != ',')
			strcat(tempbuf, " ");
		strcat(tempbuf, query->tname);
		break;
	default:
		break;
	}
	return tempbuf;
}
#endif
/******************************************************************/

static DBTBL *irepreparetree ARGS((DDIC *, QNODE *, QNODE *, FLDOP *, int, int *, int));

static DBTBL *
irepreparetree(ddic, query, parentquery, fo, preq, success, prepq)
DDIC *ddic;		/* Database */
QNODE *query;		/* Query we are preparing */
QNODE *parentquery;	/* Parent node, if any */
FLDOP *fo;		/* A fldmath struct for luck */
int preq;		/* Pre-requisite permissions.  Add to this as needed */
int *success;		/* Where to store a success code */
int prepq;		/* When to prepare the query */
{
	static char Fn[] = "irepreparetree";
	QUERY *q;
	DBTBL *rc;
	int op;

#ifdef MEMDEBUG
	mac_ovchk();
#endif
	q = query->q;
	q->nrows = 0;
	query->state = QS_PREPED;
	q->state = QS_PREPED;
	*success = 0;
	if(parentquery)
		q->priorproj = parentquery->q->priorproj;
	else
		q->priorproj = 0;
#ifdef NEVER
	putmsg(999, NULL, "Processing %d", query->op);
#endif
	switch (query->op)
	{
	case NAME_OP:
		rc = q->out;
		if (!preq)
			preq |= PM_SELECT;
		if (rc == (DBTBL *) NULL || rc->tbl == (TBL *) NULL)
		{
			putmsg(MWARN + UGE, NULL, "No such table: %s",
			       query->tname);
			return (DBTBL *) NULL;
		}
		if(preq == PM_SELECT && !isramdbtbl(rc))
		{
			/* If we haven't done it yet this statement, but
			   we have in the program */
			if(!TXstmthits++ && TXlicensehits++)
			{
				if(!TXlic_addhit())
					return closedbtbl(rc);
				/* check license */
			}
		}
		if (!permcheck(rc, preq))
		{
			putmsg(MERR, Fn, "Insufficient permissions on %s", query->tname);
			return closedbtbl(rc);
		}
		TXrewinddbtbl(rc);
		return rc;
	case RENAME_OP:
		q->op = Q_RENAME;
		q->out = irepreparetree(ddic, query->left, query, fo, preq, success, prepq);
		return q->out;
	case PRODUCT_OP:
		preq |= PM_SELECT;
		q->in1 = irepreparetree(ddic, query->left, query, fo, preq, success, prepq);
		if (q->in1 == NULL)
			return (DBTBL *) NULL;
		q->in2 = irepreparetree(ddic, query->right, query, fo, preq, success, prepq);
		if (q->in2 == NULL)
			return (DBTBL *) NULL;
		q->pred = (PRED *) NULL;
		q->proj = (PROJ *) NULL;
		q->op = Q_CPRODUCT;
		preparequery(query, fo, 1);
		if (!q->out || (q->out->tbl == NULL))
			return (DBTBL *) NULL;
		return q->out;
	case UNION_OP:
		q->in1 = irepreparetree(ddic, query->left, query, fo, preq, success, prepq);
		if (q->in1 == NULL)
			return (DBTBL *) NULL;
		q->in2 = irepreparetree(ddic, query->right, query, fo, preq, success, prepq);
		if (q->in2 == NULL)
			return (DBTBL *) NULL;
		q->pred = (PRED *) NULL;
		q->proj = (PROJ *) NULL;
		q->op = Q_UNION;
		preparequery(query, fo, 1);
		if (q->out->tbl == NULL)
			return (DBTBL *) NULL;
		return q->out;
	case PROJECT_OP:
		preq |= PM_SELECT;
		q->pred = (PRED *) NULL;
#ifdef NEVER
		q->proj = treetoproj(ddic, query->right, fo);
#endif
		if(q->proj->p_type != PROJ_AGG)
			q->proj->p_type = PROJ_SINGLE;
		q->priorproj = 1;
		q->in1 = irepreparetree(ddic, query->left, query, fo, preq, success, prepq);
		if (q->in1 == NULL)
			return (DBTBL *) NULL;
		q->in2 = (DBTBL *) NULL;
		op = Q_PROJECT;
		if (query->left->op == SELECT_OP && q->proj->p_type == PROJ_SINGLE)
			q->proj->p_type = PROJ_AGG_DONE;
#ifndef NO_ORDERED_GROUP
		if (query->left->op == GROUP_BY_OP && q->proj->p_type == PROJ_AGG)
		{
#ifdef NEVER
			putmsg(MINFO, NULL, "Unexpected use of GROUP BY.  No aggregate functions");
#endif
			q->proj->p_type = PROJ_SINGLE;
		}
#endif
		q->op = op;
#ifdef NEVER
		preparequery(query, fo, 1);
#endif
		if (q->out == NULL || q->out->tbl == NULL)
			return (DBTBL *) NULL;
		return q->out;
	case SELECT_OP:
		preq |= PM_SELECT;
		if (query->left->op == PRODUCT_OP)
		{
			q->in1 = irepreparetree(ddic, query->left->left, query, fo, preq, success, prepq);
			if (q->in1 == NULL)
				return (DBTBL *) NULL;
			q->in2 = irepreparetree(ddic, query->left->right, query, fo, preq, success, prepq);
			if (q->in2 == NULL)
				return (DBTBL *) NULL;
			op = Q_CPRODUCT;
			query->left->state = QS_PREPED;
			if(query->left->q)
				query->left->q->state = QS_PREPED;
		}
		else
		{
			q->in1 = irepreparetree(ddic, query->left, query, fo, preq, success, prepq);
			if (q->in1 == NULL)
				return (DBTBL *) NULL;
			q->in2 = (DBTBL *) NULL;
			op = Q_SELECT;
		}
#ifdef NEVER
		if (prepq)
		{
			q->pred = TXtreetopred(ddic, query->right,
                                          TXgetDiscardUnsetParameterClauses(),
                                               fo, NULL);
			if (op == Q_CPRODUCT)
				propagatepred(query->left, q->pred, fo);
		}
#else
		if(prepq)
		{
			q->pred = closepred(q->pred);
			q->pred = TXtreetopred(ddic, query->right,
                                          TXgetDiscardUnsetParameterClauses(),
                                               fo, NULL);
		}
#endif
		q->proj = (PROJ *) NULL;
		q->op = op;
#ifdef NEVER
		preparequery(q->in1, fo, 1);
#endif
		TXsettablepred(query, q->in1, q->pred, q->order, fo, 1, NULL, NULL);
		if(!q->out)
			return(q->out);
		TXrewinddbtbl(q->out);
#ifdef NEVER
		if (!prepq)
		{
			q->pred = TXtreetopred(ddic, query->right,
                                          TXgetDiscardUnsetParameterClauses(),
                                               fo, NULL);
			if (op == Q_CPRODUCT)
				propagatepred(query->left, q->pred, fo);
		}
#else
		if (!prepq)
		{
			q->pred = closepred(q->pred);
			q->pred = TXtreetopred(ddic, query->right,
                                          TXgetDiscardUnsetParameterClauses(),
                                               fo, NULL);
		}
#endif
#ifndef ALLOW_MISSING_FIELDS
		if(TXmakepredvalid(q->pred, q->out, 1, 1, 1) != q->pred)
		{
			q->out = closedbtbl(q->out);
			return NULL;
		}
#endif
		if (q->out->tbl == NULL)
			return (DBTBL *) NULL;
		return q->out;
	case TABLE_AS_OP:
		{
			char *fn;
			DD *tdd;

			if(((char *)query->right)[0] != 'R')
			{
				fn = (char *) malloc(PATH_MAX);
				strcpy(fn, ddic->tbspc);
				strcat(fn, query->tname);
			}
			else
				fn = NULL;
			rc = irepreparetree(ddic, query->left, query, fo, preq, success, prepq);
			if (!rc)
				return rc;
			/* Bug 4390: convert blobi back to blob (via
			 * TXbiddc()) when creating a table with
			 * `CREATE TABLE dest AS SELECT * FROM src':
			 */
			tdd = TXbiddc(tbldd(rc->tbl));
			if (!tdd)
				return closedbtbl(rc);
			q->out = createdbtbl(ddic, tdd, fn,
					     query->tname, "", TEXIS_TABLE);
                        tdd = closedd(tdd);
			if (query->left->op != NAME_OP &&
			    query->left->op != SELECT_OP &&
			    query->left->op != FLDMATH_PROXIM)
			{
				q->in1 = q->out;
				query->left->q->out = q->out;
/* WTF - hack to not double delete */ q->op = Q_RENAME;
				closedbtbl(rc);
			}
			else
			{
				q->in1 = rc;
			}
			rc = q->out;
			free(fn);
			return rc;
		}
		return q->out;
	case TABLE_OP:
		{
			char *fn;
			char ttype;
			DD *dd = (DD *)query->left;

			ttype = 'T';
			switch(dd->tbltype)
			{
				case TEXIS_RAM_TABLE:
					fn = NULL;
					dd->tbltype=1;
					break;
				case TEXIS_BTREE_TABLE:
					ttype = 'b';
					dd->tbltype = 0;
				default:
					fn = (char *) malloc(PATH_MAX);
					strcpy(fn, ddic->tbspc);
					strcat(fn, query->tname);
			}
			rc = createdbtbl(ddic, dd, fn, query->tname, "", ttype);
			if(fn) free(fn);
			return rc;
		}
	case INSERT_OP:
		preq |= PM_INSERT;
		q->in1 = irepreparetree(ddic, query->left, query, fo, preq, success, prepq);
		if (q->in1 == (DBTBL *) NULL)
			return q->in1;
/*
   WTF: Maybe we need to do some more figgering out here what we want
   to do. If we assume the lack of NULLs then the question becomes are
   we doing a union from a table, or from values in the tree here. If
   we are doing it from values in the tree then the toughest part is
   creating the correct tuple to add in, and then also doing the writes,
   and getting the index updated.
 */
		switch (query->right->op)
		{
		case LIST_OP:
			{
				DD *dd;

				q->in2 = (DBTBL *) calloc(1, sizeof(DBTBL));
				q->in2->ddic = ddic;
				q->in2->type = 'T';
				q->in2->tblid = -1;
				dd = TXbddc(tbldd(q->in1->tbl));
				q->in2->tbl = createtbl(dd, NULL);
				closedd(dd);
				listtotbl(query->right, q->in2, fo);
				break;
			}
		case COLUMN_OP:
			{
				DD *dd;

				q->in2 = (DBTBL *) calloc(1, sizeof(DBTBL));
				q->in2->ddic = ddic;
				q->in2->type = 'T';
				q->in2->tblid = -1;
				dd = TXbddc(tbldd(q->in1->tbl));
				q->in2->tbl = createtbl(dd, NULL);
				closedd(dd);
				columntotbl(query->right, q->in2, fo);
				break;
			}
		case VALUE_OP:
			{
				DD *dd;

				q->in2 = (DBTBL *) calloc(1, sizeof(DBTBL));
				if(!q->in2)
					return NULL;
				q->in2->ddic = ddic;
				q->in2->type = 'T';
				q->in2->tblid = -1;
				dd = TXbddc(tbldd(q->in1->tbl));
				q->in2->tbl = createtbl(dd, NULL);
				closedd(dd);
				if (valuestotbl(query->right, q->in2, fo) == -1)
					return NULL;
				break;
			}
		default:
			q->in2 = irepreparetree(ddic, query->right, query, fo, 0, success, prepq);
			/* Set preq back to 0, as different subquery */
			break;
		}
		q->op = Q_APPEND;
		if ((q->in1 == NULL) || (q->in2 == NULL))
			return (DBTBL *) NULL;
		if (q->in1->lname && q->in2->lname &&
		    !strcmp(q->in1->lname, q->in2->lname))
		{
			putmsg(MERR + UGE, (char *) NULL,
			       "Can't insert a table into itself.");
			return (DBTBL *) NULL;
		}
		preparequery(query, fo, 1);
		if (q->out == (DBTBL *) NULL)
			return (DBTBL *) NULL;
		if (q->out->tbl == (TBL *) NULL)
			return (DBTBL *) NULL;
		TXrewinddbtbl(q->out);
		return q->out;
	case COLUMN_OP:
#if 0
		fprintf(stderr, "\nTime for some funky assignments\n");
#endif
		putmsg(MWARN, Fn, "This function not supported yet");
		q->in1 = irepreparetree(ddic, query->left, query, fo, preq, success, prepq);
		q->in2 = irepreparetree(ddic, query->right, query, fo, preq, success, prepq);
#if 0
		disptable(q->in2, fo);
#endif
		return (DBTBL *) NULL;
	case DEL_SEL_OP:
		preq |= PM_DELETE;
		q->in1 = irepreparetree(ddic, query->left, query, fo, preq, success, prepq);
		if (q->in1 == (DBTBL *) NULL)
			return (DBTBL *) NULL;
		q->in2 = (DBTBL *) NULL;
		q->pred = closepred(q->pred);
		if (query->right)
			q->pred = TXtreetopred(ddic, query->right,
                                          TXgetDiscardUnsetParameterClauses(),
                                               fo, NULL);
		else
			q->pred = (PRED *) NULL;
		q->proj = (PROJ *) NULL;
		q->op = Q_DELETE;
		preparequery(query, fo, 1);
		if (q->out != (DBTBL *) NULL)
		{
			TXrewinddbtbl(q->out);
			if (q->out->tbl == (TBL *) NULL)
				return (DBTBL *) NULL;
		}
		return q->out;
	case UPD_SEL_OP:
		q->update = treetoupd(ddic, query->left, fo);
		if (q->update == (UPDATE *) NULL)	/*if(!q->update) */
			return (DBTBL *) NULL;
		preq |= PM_UPDATE;
		q->in1 = irepreparetree(ddic, query->tname, query, fo, preq, success, prepq);
		if (q->in1 == (DBTBL *) NULL)
			return (DBTBL *) NULL;
		q->in2 = (DBTBL *) NULL;
		q->pred = closepred(q->pred);
		if (query->right)
			q->pred = TXtreetopred(ddic, query->right,
                                         TXgetDiscardUnsetParameterClauses(),
                                               fo, NULL);
		else
			q->pred = (PRED *) NULL;
		q->op = Q_UPDATE;
		preparequery(query, fo, 1);
		if (q->out != (DBTBL *) NULL)
		{
			if (q->out->tbl == (TBL *) NULL)
				return (DBTBL *) NULL;
			TXrewinddbtbl(q->out);
		}
		return q->out;
	case ORDER_OP:
		q->op = Q_ORDER;
		q->in1 = irepreparetree(ddic, query->right, query, fo, preq, success, 0);
		if (q->in1 == (DBTBL *) NULL)
			return q->in1;
#ifdef NEVER /* Was NO_OPTIMIZE ORDER - fails with query */
		if (parentquery && parentquery->q && (parentquery->q->pred == NULL) && parentquery->q->proj && parentquery->q->proj->type == 1)
		{
			q->out = q->in1;
			q->in1 = NULL;
			return q->out;
		}
#endif
#ifdef NEVER
		q->proj = treetoproj(ddic, query->left, fo);
		switch (query->right->op)
		{
		case SELECT_OP:
		case GROUP_BY_OP:
			{
				QNODE	*qnode1 = query->right;
				QUERY	*q1 = qnode1->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				if (q1->op == Q_SELECT)
				{
					TXsettablepred(qnode1, q1->in1, (PRED *) NULL,
						       (PROJ *) NULL, fo, 1, NULL);
					TXsettablepred(qnode1, q1->in1, q1->pred,
						       q->order, fo, 1, NULL);
				}
				break;
			}
		case FOP_PROXIM:
			{
				QNODE	*qnode1 = query->right->left;
				QUERY	*q1 = qnode1->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				if (q1->op == Q_SELECT)
				{
					TXsettablepred(qnode1, q1->in1, (PRED *) NULL,
						       (PROJ *) NULL, fo, 1, NULL);
					TXsettablepred(qnode1, q1->in1, q1->pred,
						       q->order, fo, 1, NULL);
				}
				break;
			}
		case NAME_OP:
			{
				QNODE	*qnode1 = query->right;
				QUERY	*q1 = qnode1->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				TXsettablepred(qnode1, q1->out, (PRED *) NULL,
					       (PROJ *) NULL, fo, 1, NULL);
				TXsettablepred(qnode1, q1->out, q1->pred,
					       q->order, fo, 1, NULL);
				break;
			}
		case PROJECT_OP:
		case DISTINCT_OP:
			break;	/* Can happen.  Not bad? */
		default:
			putmsg(MERR, Fn, "Operation %d was unexpected while ordering",
			       query->right->op);
			break;
		}
		preparequery(query, fo, 1);
#endif
		if(q->in2->type == 'B')
		{
			btreeclear(q->in2->index.btree);
		}
		dbresetstats(q->out);
		return q->out;

	case GROUP_BY_OP:
		q->op = Q_GROUP;
		switch(query->right->op)
		{
			case SELECT_OP:
			case ORDER_OP:
			case NAME_OP:
/* Don't setup indices yet, as we will do it ourselves */
				q->in1 = irepreparetree(ddic, query->right,
				             query, fo, preq, success, 0);
				break;
			default:
/* Setup indices yet, as we can't do it ourselves yet (too deep in tree) */
				q->in1 = irepreparetree(ddic, query->right,
					     query, fo, preq, success, 1);
				break;
		}
		if (q->in1 == (DBTBL *) NULL)
			return q->in1;
#ifdef NEVER			/* Not appropriate for group by ? */
		if (parentquery && parentquery->q && parentquery->q->proj && parentquery->q->proj->type == 1)
		{
			q->out = q->in1;
			q->in1 = NULL;
			return q->out;
		}
		q->proj = treetoproj(ddic, query->left, fo);
		switch (query->right->op)
		{
		case SELECT_OP:
		case ORDER_OP:
			{
				QNODE	*qnode1 = query->right;
				QUERY	*q1 = qnode1->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				if (q1->op == Q_SELECT)
				{
					TXsettablepred(qnode1, q1->in1, (PRED *) NULL,
						       (PROJ *) NULL, fo, 1, NULL);
					TXsettablepred(qnode1, q1->in1, q1->pred,
						       q->order, fo, 1, NULL);
				}
				break;
			}
		case FOP_PROXIM:
			{
				QUERY *q1 = query->right->left->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				if (q1->op == Q_SELECT)
				{
					TXsettablepred(q1->in1, (PRED *) NULL,
						       (PROJ *) NULL, fo, 1, NULL);
					TXsettablepred(q1->in1, q1->pred,
						       q->order, fo, 1, NULL);
				}
				break;
			}
		case NAME_OP:
			{
				QUERY *q1 = query->right->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				TXsettablepred(q1->out, (PRED *) NULL,
					       (PROJ *) NULL, fo, 1, NULL);
				TXsettablepred(q1->out, q1->pred,
					       q->order, fo, 1, NULL);
				break;
			}
		default:
			putmsg(MERR, Fn, "Operation %d was unexpected while grouping",
			       query->right->op);
			break;
		}
		preparequery(query, fo, 1);
#endif
/* WTF - need to remove all index entries from q->in2 */
		if(q->in2->type == 'B')
		{
			btreeclear(q->in2->index.btree);
		}
		dbresetstats(q->out);
		return q->out;
	case DISTINCT_OP:
		q->op = Q_DISTINCT;
		switch(query->right->op)
		{
			case SELECT_OP:
			case ORDER_OP:
			case NAME_OP:
/* Don't setup indices yet, as we will do it ourselves */
				q->in1 = irepreparetree(ddic, query->right,
				             query, fo, preq, success, 0);
			default:
/* Setup indices yet, as we can't do it ourselves yet (too deep in tree) */
				q->in1 = irepreparetree(ddic, query->right,
					     query, fo, preq, success, 1);
		}
		if (q->in1 == (DBTBL *) NULL)
			return q->in1;
#ifdef NEVER			/* Not appropriate for group by ? */
		if (parentquery && parentquery->q && parentquery->q->proj && parentquery->q->proj->type == 1)
		{
			q->out = q->in1;
			q->in1 = NULL;
			return q->out;
		}
#endif
		q->proj = TXddToProj(q->in1);
		switch (query->right->op)
		{
		case SELECT_OP:
		case ORDER_OP:
			{
				QNODE	*qnode1 = query->right;
				QUERY	*q1 = qnode1->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				if (q1->op == Q_SELECT)
				{
					TXsettablepred(qnode1, q1->in1, (PRED *) NULL,
						       (PROJ *) NULL, fo, 1, NULL, NULL);
					TXsettablepred(qnode1, q1->in1, q1->pred,
						       q->order, fo, 1, NULL, NULL);
				}
				break;
			}
		case NAME_OP:
			{
				QNODE	*qnode1 = query->right;
				QUERY	*q1 = qnode1->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				TXsettablepred(qnode1, q1->out, (PRED *) NULL,
					       (PROJ *) NULL, fo, 1, NULL, NULL);
				TXsettablepred(qnode1, q1->out, q1->pred,
					       q->order, fo, 1, NULL, NULL);
				break;
			}
		default:
/*
   putmsg(MERR, Fn, "Operation %d was unexpected in distinct",
   query->right->op);
 */
			break;
		}
		preparequery(query, fo, 1);
		return q->out;

	case GRANT_OP:
		{
			long pf;

			pf = strtoperms(query->left->tname);
			preq |= pf;
			preq |= PM_GRANT;
			q->in1 = irepreparetree(ddic, query->right, query, fo, preq, success, prepq);
			if (q->in1)
			{
				if (permgrant(ddic, q->in1, query->tname, pf) == 1)
				{
					if (dounixperms(query) == -1)
						return NULL;
				}
				setindexperms(q->in1);
			}
			*success = 1;
			return (DBTBL *) NULL;
		}
	case REVOKE_OP:
		{
			long pf;

			pf = strtoperms(query->left->tname);
			preq |= pf;
			preq |= PM_GRANT;

			q->in1 = irepreparetree(ddic, query->right, query, fo, preq, success, prepq);
			if (q->in1 == (DBTBL *) NULL ||
			    q->in1->tbl == (TBL *) NULL)
				return (DBTBL *) NULL;
			if (permrevoke(ddic, q->in1, query->tname, pf) == 1)
				if (revunixperms(query) == -1)
					return (DBTBL *) NULL;
			setindexperms(q->in1);
			*success = 1;
			return (DBTBL *) NULL;
		}
	case FOP_PROXIM:
	{
		q->in1 = irepreparetree(ddic, query->left, query, fo, preq, success, prepq);
		if (q->in1 == (DBTBL *) NULL || q->in1->tbl == (TBL *) NULL)
			return (DBTBL *) NULL;
		q->out = q->in1;
		q->op = Q_PROXIM;
		return q->out;
	}
	case 0:
		return (DBTBL *) NULL;
	case PROP_OP:					/* JMT 97-11-24 */
	{
		int rc2;
		char *prop = NULL;
		PRED *pred;
		char *val = NULL;
		FLD *f;

		if(query->right && query->right->left &&
		   query->right->left->op == NAME_OP)
			prop = query->right->left->tname;
		if(!prop)
			return NULL;
		pred=TXtreetopred(ddic, query->right->right, 0, fo, NULL);
		if(!pred)
			return NULL;
		pred_eval(NULL, pred, fo);
		f = fopop(fo);
		pred=closepred(pred);
		if(f)
		{
			if((f->type & DDTYPEBITS) == FTN_CHAR)
			{
				val=getfld(f, NULL);
			}
			else
			{
				FLD *f2;

				f2=createfld("varchar", 20, 0);
				fopush(fo, f);
				fopush(fo, f2);
				foop(fo, FOP_CNV);
				f = closefld(f);
				f2= closefld(f2);
				f=fopop(fo);
				if(!f)
					return NULL;
				if((f->type & DDTYPEBITS) == FTN_CHAR)
					val=getfld(f, NULL);
			}
		}
		/*
		putmsg(MINFO, NULL, "Set %s to %s", prop, val);
		*/
		rc2 = setprop(ddic, prop, val);
		f = closefld(f);
		*success = rc2 >= 0 ? 1 : 0;
		return NULL;
	}
#ifdef HAVE_VIEWS
	case VIEW_OP:/* JMT 98-02-16 */
		{
			char *fn=NULL;
			DD *tdd;

			rc = irepreparetree(ddic, query->left, query, fo, preq, success, prepq);
			if (!rc)
				return rc;
			fn = treetosql(ddic, query->left, fo);
			KNG wtf see Bug 4390 above: use TXbiddc()?;
			tdd = tbldd(rc->tbl);
			if (!tdd)
				return closedbtbl(rc);
			q->out = createdbtbl(ddic, tdd, fn,
					     query->tname, "", 'V');
			if (query->left->op != NAME_OP &&
			    query->left->op != SELECT_OP &&
			    query->left->op != FOP_PROXIM)
			{
				q->in1 = q->out;
				query->left->q->out = q->out;
/* WTF - hack to not double delete */ q->op = Q_RENAME;
				closedbtbl(rc);
			}
			else
			{
				q->in1 = rc;
			}
			rc = q->out;
			*success=1;
			free(fn);
			return rc;
		}
		return q->out;
#endif
#ifndef NO_KEEP_STATS
	case CREATE_OP:
		if(!strcmp(query->tname, "index"))
		{
			int	rc;
			char *fields, *indexName, *tableName, *indexType;
			QNODE	*optionsSubList;
			TXindOpts	*options = NULL;

			/* NOTE: see also ipreparetree() */
			/* <CREATE what="index">
			 *   <list>
			 *     indexName, tableName, indexType[, option ...]
			 *   </list>
			 *   <list> field[, field ...] </list>
			 * </CREATE>
                         */
			tempbuf[0] = '\0';
			fields=querytotext(query->right);
			indexName = TXqnodeListGetItem(query->left, 0);
			tableName = TXqnodeListGetItem(query->left, 1);
			indexType = TXqnodeListGetItem(query->left, 2);
			if (!fields || !indexName || !tableName || !indexType)
			{				/* failed */
				*success = 0;
				return(NULL);
			}
			options = TXindOptsOpen(ddic);	/* global defaults */
			if (!options) goto err1;
			optionsSubList = TXqnodeListGetSubList(query->left, 3);
			if (optionsSubList &&	/* has WITH options */
			    !TXindOptsGetRawOptions(options, optionsSubList))
			{
			err1:
				rc = -1;
				goto afterCreateIndex;
			}
			rc = createindex(ddic,
					 indexName,	/* index file */
					 indexName,
					 tableName,
					 fields,
					 (indexType[0] == INDEX_UNIQUE),
					 indexType[0], options);
		afterCreateIndex:
			options = TXindOptsClose(options);
			if(rc != -1) 
				*success=1;
			else
				*success=0;
		}
		else
		{
			/* WTF implement */
			putmsg(MERR + UGE,__FUNCTION__,
			       "Unhandled CREATE `%s'", query->tname);
			*success = 0;
		}
		return NULL;
#endif /* !NO_KEEP_STATS */
	case ALTER_OP:
		/* `query->left' NAME_OP's `q->in1' was probably already
		 * closed by TXcompactTable(), so bail here WTF:
		 */
		putmsg(MERR + UGE, Fn,
	  "ALTER TABLE|INDEX|USER not supported on re-prepped statement");
		return(DBTBLPN);
	default:
		putmsg(MERR + UGE, Fn,
		       "Unimplemented feature QNODE_OP #%d (%s)",
		       (int)query->op, TXqnodeOpToStr(query->op, NULL, 0));
		return (DBTBL *) NULL;
	}
}

/******************************************************************/

DBTBL *
TXrepreparetree(ddic, query, fo, success, dbtbl)
DDIC *ddic;
QNODE *query;
FLDOP *fo;
int *success;
DBTBL *dbtbl;
{
	TXstmthits = 0;
#ifdef ALLOW_EXPIRE_TIME
	static char ed[] = "@(#)wxyzFFFFFFFF";
	char *np;

	np = ed + 8;
	sscanf(np, "%08lx", &et);
	if ((unsigned long) time(NULL) > et)
	{
		putmsg(MWARN, NULL, "This Texis has expired");
		*success = 0;
		return NULL;
	}
#endif
	(void)dbtbl;
        {
		char	msg[1024];

		if(TXchecklicense(LIC_EXPIRE_TIME, 0, msg, sizeof(msg)) < 0)
		{
			putmsg(MWARN, NULL, "License violation: %s", msg);
			*success = 0;
			return NULL;
		}
        }
	TXstmthits = 0;
#ifdef NEVER
	putmsg(999, NULL, "Reprep");
#endif
	return irepreparetree(ddic, query, NULL, fo, 0, success, 1);
}

/******************************************************************/
