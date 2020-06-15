/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>

#ifdef _WINDLL
size_t __cdecl strftime(char *, size_t, const char *,
		const struct tm *);
int __cdecl printf(const char *, ...);
#endif

#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "fldmath.h"
#include "texint.h"

/******************************************************************/
#ifdef SHOW_STATS
static void
disp_stats(t)
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
			printf("%12lg ", t->fldstats[i].sum);
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
			printf("%12lg ", t->fldstats[i].min);
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
			printf("%12lg ", t->fldstats[i].max);
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
				printf("%12lg ",
				       (double)(t->fldstats[i].sum/t->fldstats[i].count));
			else
				printf("%12ld ", 0);
		else
			printf("%12s ", "");
		}
	}
	printf("%12s\n", "Average");
}
#endif
/********************************************************************/
/*
 *	Display all the tuples in a table.
 */

void
disptable(tb, fo)
DBTBL *tb;
FLDOP *fo;
{
	DBTBL *tup;
	int width=0;

	printf("\n");
	if (tb->tbl == (TBL *)NULL)
		return;
	TXrewinddbtbl(tb);
	width = tup_disp_head(tb, width);
	while((tup=tup_read(tb, NULL, SQL_FETCH_NEXT, 1, NULL, TXCOUNTINFOPN))!=(DBTBL *)NULL)
		tup_disp(tup, width, fo);
#ifdef SHOW_STATS
	disp_stats(tb);
#endif
	printf("\n");
}

/****************************************************************************/

void
printrowbreak(int width)
{
	while (width > 0)
	{
		putchar('-');
		width--;
	}
	putchar('+');
}

void
printcentered(char *s, int width)
{
	int padleft, padright, stringlength;
	size_t slen = strlen(s);

	stringlength = (int)slen;
	padleft = (width-stringlength)/2;
	if (padleft < 0) /* Not enough room */
	{
		printf("%*s", width, s);
		return;
	}
	padright = (width - padleft - stringlength);
	printf("%*s%*s%*s",padleft,"",stringlength,s,padright,"");
}

int
tup_disp_head(DBTBL *t, int width)
{
	int i = 0, j = 0, r;
	char *name, *sname, *tname;

	if(width <= 0)
	{
		width = 12; /* Our previous default, i.e. min width */
		for(i = 0; name=getfldname(t->tbl, i); i++)
		{
			if(strlen(name) > width)
				width = strlen(name);
		}
	}
	i = 0;
	while ((name = getfldname(t->tbl, i++)) != (char *)NULL)
	{
		if (!strstr(name, ".$recid"))
		{
			sname = strrchr(name, '.');
			if (sname != (char *)NULL)
			{
				tname = dbnametoname(t, sname + 1, FTNPN,
						     INTPN);
				if (tname != (char *)NULL)
					name = sname+1;
			}
			printcentered(name, width);
			if ((size_t)i < (t->tbl->n))
				putchar(' ');
			j++;
		}
	}
	printf("\n");
	r = j;
	while(j-->0)
		printrowbreak(width);
	printf("\n");
	return width;
}

/****************************************************************************/

void
tup_disp(DBTBL *tup, int width, FLDOP *fo)
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
		if (foop(fo, FOP_CNV)==-1)
			putmsg(MERR, NULL, "Could not display %s", fname);
		closefld(f);
		f = fopop(fo);
		if (!strstr(fname, ".$recid"))
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
#ifdef EPI_INT64_SQL
					case FTN_INT64:
					case FTN_UINT64:
#endif /* EPI_INT64_SQL */
					case FTN_DWORD :
					case FTN_DOUBLE :
					case FTN_FLOAT :
					case FTN_RECID :
						if(i == (tbl->n - 1U))
							printf("%*s", width, fldtostr(f));
						else
							printf("%*s ", width, fldtostr(f));
						break;
					default:
						if(i == (tbl->n - 1U))
							printf("%s", fldtostr(f));
						else
							printf("%-*s ", width, fldtostr(f));
						break;
				}
				break;
			default:
				switch(f1->type & DDTYPEBITS)
				{
				case FTN_BLOB:
				{
					EPI_OFF_T where;
					size_t sz;

					where = *(EPI_OFF_T *)getfld(f1, NULL);
					printf("%-*s ", width, (char *)getdbf(tbl->bf, where, &sz));
				}
				default:
				printf("%*s ", width, ddfttypename(f1->type));
/*
				printf("%12ld ", *(long *)(f->v));
				printf("%12s ", ctime(f->v));
*/
				}
				break;
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
