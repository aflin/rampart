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
#include "fldmath.h"

/******************************************************************/

#ifdef SHOW_STATS
static void
cdisp_stats(t)
DBTBL *t;
{
	unsigned int	i;
	TBL	*tbl = t->tbl;
	FLD	*fld;
	char	*fname;

	for (i=0; i < tbl->n; ++i)
	{
		fname = getfldname(tbl, i);
		fld = dbnametofld(t, fname);
		if (!strstr(fname, ".$recid"))
		printf("------------+");
	}
	printf("------------+\n");
	for (i=0; i < tbl->n; ++i)
	{
		fname = getfldname(tbl, i);
		fld = dbnametofld(t, fname);
		if (!strstr(fname, ".$recid"))
		printf("%12ld ", t->fldstats[i].count);
	}
	printf("%12s\n", "Count");
	for (i=0; i < tbl->n; ++i)
	{
		fname = getfldname(tbl, i);
		fld = dbnametofld(t, fname);
		if (!strstr(fname, ".$recid"))
		if ((fld->type & FTN_VarBaseTypeMask) == FTN_LONG)
			printf("%12g ", t->fldstats[i].sum);
		else
			printf("%12s ", "");
	}
	printf("%12s\n", "Sum");
	for (i=0; i < tbl->n; ++i)
	{
		fname = getfldname(tbl, i);
		fld = dbnametofld(t, fname);
		if (!strstr(fname, ".$recid"))
		if ((fld->type & FTN_VarBaseTypeMask) == FTN_LONG)
			printf("%12ld ", t->fldstats[i].min);
		else
			printf("%12s ", "");
	}
	printf("%12s\n", "Min");
	for (i=0; i < tbl->n; ++i)
	{
		fname = getfldname(tbl, i);
		fld = dbnametofld(t, fname);
		if (!strstr(fname, ".$recid"))
		if ((fld->type & FTN_VarBaseTypeMask) == FTN_LONG)
			printf("%12ld ", t->fldstats[i].max);
		else
			printf("%12s ", "");
	}
	printf("%12s\n", "Max");
	for (i=0; i < tbl->n; ++i)
	{
		fname = getfldname(tbl, i);
		fld = dbnametofld(t, fname);
		if (!strstr(fname, ".$recid"))
		{
		if ((fld->type & FTN_VarBaseTypeMask) == FTN_LONG)
			if (t->fldstats[i].count != 0)
				printf("%12g ",
				       (float)(t->fldstats[i].sum/t->fldstats[i].count));
			else
				printf("%12ld ", 0);
		else
			printf("%12s ", "");
		}
	}
	printf("%12s\n", "Average");
}
#endif

/******************************************************************/

int
tup_cdisp_head(DBTBL *tup, int width)
{
	int	i;
	char	*fname;
	TBL	*tbl = tup->tbl;
	size_t	maxlen = width;

	for (
		i=0, fname = getfldname(tbl, i);
		(size_t)i < tbl->n;
		fname = getfldname(tbl, ++i)
	)
	{
		if (!strstr(fname, ".$recid"))
			if (strlen(fname) > maxlen)
				maxlen = strlen(fname);
	}
	return maxlen;
}

/****************************************************************************/

void
tup_cdisp(tup, width, fo)
DBTBL *tup;
int	width;
FLDOP	*fo;
{
	unsigned i, j;
	FLD *f, *f1;
	char *fname;
	TBL *tbl = tup->tbl;

	f = createfld("varchar", 20, 0);
	setfldv(f);
	for (
		i=0, fname = getfldname(tbl, i), f1 = nametofld(tbl, fname);
		i < tbl->n;
		++i
	)
	{
		f->type = FTN_CHAR + DDVARBIT;
		freeflddata(f);
		fopush(fo, f1);
		fopush(fo, f);
		if (foop(fo, FOP_CNV)<0)
			putmsg(MERR, NULL, "Could not display %s", fname);
		closefld(f);
		f = fopop(fo);
		if (!strstr(fname, ".$recid")) {
		if (width == -1)
			printf("%s: ", fname);
		else
		{
			printf("%s", fname);
			for(j=strlen(fname); j < (unsigned)width; j++)
				printf(" ");
			printf(": ");
		}
		switch (f->type & FTN_VarBaseTypeMask)
		{
			case FTN_CHAR :
			case FTN_CHAR | DDVARBIT :
				switch(f1->type & DDTYPEBITS)
				{
					case FTN_BYTE:
					case FTN_SHORT :
					case FTN_SMALLINT :
					case FTN_WORD :
					case FTN_LONG | DDVARBIT :
					case FTN_LONG :
					case FTN_INT :
					case FTN_INTEGER :
					case FTN_DWORD :
					case FTN_DOUBLE :
					case FTN_FLOAT :
						printf("%12s\n", (char *)getfld(f,NULL));
						break;
					default:
						printf("%s\n", (char *)getfld(f,NULL));
						break;
				}
				break;
			default:
				printf("%12s\n",ddfttypename(f->type));
				break;
		}
		}
		fname = getfldname(tbl, i+1);
		if (fname)
			f1 = nametofld(tbl, fname);
	}
	if (f)
		closefld(f);
	printf("\n");
}

/****************************************************************************/
static void soutfld ARGS((char *,int,int));

static void
soutfld(s,rmnl,qt)
char *s;
int rmnl;
int qt;
{
 	if(qt)
		putchar('"');
	for(;*s!='\0';s++)
		if(rmnl && *s=='\n')
			putchar(' ');
		else
		{
			if(qt && *s=='"')
				putchar('\\');
			putchar(*s);
		}
	if(qt)
		putchar('"');
}

/****************************************************************************/
/*
 * tup_sdisp - separated fields, e.g. CSV
*/

int
tup_sdisp_head(tup,delim,qt)
DBTBL	*tup;
int qt;
int delim;
{
	int i = 0, j = 0, r;
	char *name, *sname, *tname;

	while ((name = getfldname(tup->tbl, i++)) != (char *)NULL)
	{
		if (!strstr(name, ".$recid"))
		{
			sname = strrchr(name, '.');
			if (sname != (char *)NULL)
			{
				tname = dbnametoname(tup, sname + 1, FTNPN,
						     INTPN);
				if (tname != (char *)NULL)
					name = sname+1;
			}
                        if(i>1) putchar(delim);
			soutfld(name, 0, qt);
			j++;
		}
	}
	printf("\n");
	r = j;
	return r;
}

/****************************************************************************/
void
tup_sdisp(tup, rmnl, delim, qt, fo)
DBTBL *tup;
int	rmnl;
int	delim;
int	qt;
FLDOP	*fo;
{
	unsigned i;
	FLD *f, *f1;
	char *fname;
	TBL *tbl = tup->tbl;

	f = createfld("varchar", 20, 0);
	setfldv(f);
	for (
		i=0, fname = getfldname(tbl, i), f1 = nametofld(tbl, fname);
		i < tbl->n;
		++i
	)
	{
		f->type = FTN_CHAR + DDVARBIT;
		freeflddata(f);
		fopush(fo, f1);
		fopush(fo, f);
		if (foop(fo, FOP_CNV)<0)
			putmsg(MERR, NULL, "Could not display %s", fname);
		closefld(f);
		f = fopop(fo);
		if (!strstr(fname, ".$recid")) {
                if(i>0)
                     putchar(delim);
		switch (f->type & FTN_VarBaseTypeMask)
		{
			case FTN_CHAR :
			case FTN_CHAR | DDVARBIT :
				switch(f1->type & DDTYPEBITS)
				{
					case FTN_BYTE:
					case FTN_SHORT :
					case FTN_SMALLINT :
					case FTN_WORD :
					case FTN_LONG | DDVARBIT :
					case FTN_LONG :
					case FTN_INT :
					case FTN_INTEGER :
					case FTN_DWORD :
					case FTN_DOUBLE :
					case FTN_FLOAT :
						soutfld(getfld(f,NULL),rmnl,qt);
						break;
					default:
						soutfld(getfld(f,NULL),rmnl,qt);
						break;
				}
				break;
			default:
				soutfld(ddfttypename(f->type),rmnl,qt);
				break;
		}
		}
		fname = getfldname(tbl, i+1);
		if (fname)
			f1 = nametofld(tbl, fname);
	}
	if (f)
		closefld(f);
	printf("\n");
}

/****************************************************************************/
