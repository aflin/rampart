/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"


/******************************************************************/
/*
	Takes a query, and generates a field with the results.

	Three types, return single row, return multiple rows,
	check for existence.
*/
/******************************************************************/

#ifdef NEVER
extern int VXpmIgnore;
#endif

FLD *
TXqueryfld(DDIC *ddic, DBTBL *dbtbl, QNODE *query, FLDOP *fo, SUBQ_TYPE type, int verbose)
{
	DBTBL *outtbl;
	FLD *rc = NULL, *f = FLDPN;
	int   success, nfields;
	unsigned long nrows=0;
	size_t sz;
	HTBUF *buf = HTBUFPN;
	void *v;
	int	och;

	if(!verbose)
		TXverbosepredvalid = 0;
	if(query->op == SUBQUERY_OP)
		query = query->left;
	och = ddic->ch; /* Don't count subquery as Texis transaction */
	ddic->ch = 0;
	outtbl = TXpreparetree(ddic, query, fo, &success, dbtbl);
	ddic->ch = och;
	if(!verbose)
		TXverbosepredvalid = 1;
	if(!outtbl)
	{
		TXunpreparetree(query);
		return rc;
	}
	nfields = ddgetnfields(outtbl->tbl->dd);
	if(nfields == 1)
	{
		buf = openhtbuf();
		f = outtbl->tbl->field[0];
		if((f->type & DDTYPEBITS) == FTN_CHAR)
		{
			ft_strlst sl;
			size_t slsz;

			sl.nb=0;
			sl.delim='\0';
			sl.buf[0]='\0';
			slsz = sl.buf - (char *)&sl;
			htbuf_write(buf, (char *)&sl, slsz);
		}
		while(TXdotree(query, fo, SQL_FETCH_NEXT, 1) >= 0)
		{
			v = getfld(f, &sz);
			if((f->type & DDTYPEBITS) == FTN_CHAR)
			{
				sz++;
			}
			htbuf_write(buf, v, sz*f->elsz);
			nrows++;
			if(type == SUBQUERY_EXISTSVAL)
				break;
		}
	}
#ifdef NEVER /* nrows must be 0 */
	else
		return rc;
#endif
	if(type == SUBQUERY_EXISTSVAL)
	{
		rc = emptyfld(FTN_INT, 1);
		setfldv(rc);
		*(ft_int *)rc->v = nrows;
	}
	else
	{
		if(nrows != 0)
		{
			if(nrows == 1)
			{
				rc = emptyfld(f->type, 1);
			}
			else
			{
				if((f->type & DDTYPEBITS) == FTN_CHAR)
					rc = emptyfld((FTN_STRLST|DDVARBIT),1);
				else
					rc = emptyfld((f->type | DDVARBIT), 1);
			}
			sz = htbuf_getdata(buf, (char **)&v, 0x3);
			if((f->type & DDTYPEBITS) == FTN_CHAR)
			{
				ft_strlst *sl;
				size_t slsz;

				sl = v;
				slsz = sz - (sl->buf - (char *)sl);
				sl->nb = slsz;
				if(nrows == 1)
				{
					memmove(v, sl->buf, slsz);
					slsz = strlen(v);
				}
			}
			setfld(rc, v, sz);
			rc->n = sz/rc->elsz;
			rc->size = sz;
		}
	}
	closehtbuf(buf);
	closedbtbl(outtbl);
	TXunpreparetree(query);
	return rc;
}

