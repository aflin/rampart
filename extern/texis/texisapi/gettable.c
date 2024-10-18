#include "txcoreconfig.h"
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"
#include "mmsg.h"


static CONST char	SysTablesCorruptedMsg[] =
	"SYSTABLES corrupted, cannot read";


/******************************************************************/

char *
TXddgetanytable(ddic, tname, type, reset)
DDIC *ddic;
CONST char *tname;	/* (in) logical name of table */
char	*type;		/* (in/out) SYSTABLES.TYPE of table */
int  reset;
/* Looks for table `tname' of type `*type' (any user-visible type if '\0';
 * any type if `\1').
 * Returns alloc'd file path to table, or NULL on error/not found.
 */
{
	static CONST char Fn[] = "TXddgetanytable";
        TBL *tbl;
        FLD *n, *w, *t;
        char *fname;
        size_t sz;
        BTLOC where;
	char	reqType = *type;

	/* Bug 3896: do not close and reopen all SYS... tables if `reset';
	 * was only doing it for Windows, no longer seems needed and it was
	 * potentially closing an in-use `ddic->indextbl' etc. in createdb()
	 */

#ifndef NO_CACHE_TABLE
	makevalidtable(ddic, SYSTBL_TABLES);
	tbl = ddic->tbltblcache->tbl;
	if (!tbl)
		return NULL;
#else
        tbl = ddic->tabletbl;
#endif
        if (ddic->tablendx)
                where = btsearch(ddic->tablendx, strlen(tname), (char *)tname);

        n = nametofld(tbl, "NAME");
        w = nametofld(tbl, "WHAT");
        t = nametofld(tbl, "TYPE");

	if(!n || !w || !t)
	{
		putmsg(MERR, Fn, SysTablesCorruptedMsg);
		return NULL;
	}

        if (ddic->tablendx == (BTREE *)NULL)
        {
                rewindtbl(tbl);
                while(TXrecidvalid(gettblrow(tbl, NULL)))
                {
                        if (strcmp(getfld(n, &sz), tname) == 0)
			{
                                type[0] = *(char *)getfld(t, &sz);
				if (reqType == '\1')	/* any type */
					goto gotIt;
				else if (reqType != '\0')
				{		/* specific type requested */
					if (type[0] == reqType) goto gotIt;
				}
				else switch (type[0])
				{
				case TEXIS_SYS_TABLE:
				case TEXIS_TABLE:
				case TEXIS_BTREEINDEX_TABLE:
				case TEXIS_LINK:
				case TEXIS_VIEW:
					goto gotIt;
				case TEXIS_TEMP_TABLE:
				case TEXIS_DEL_TABLE:
				default:
					/* not accessible tables */
					break;
				}
			}
                }
        }
        else					/* have ddic->tablendx */
                if(TXrecidvalid(gettblrow(tbl, &where)))
                        if (strcmp(getfld(n, &sz), tname) == 0)
                        {
                                type[0] = *(char *)getfld(t, &sz);
				if (reqType == '\1')	/* any type */
					goto gotIt;
				else if (reqType != '\0')
				{		/* specific type requested */
					if (type[0] == reqType) goto gotIt;
				}
				else switch (type[0])
				{
				case TEXIS_SYS_TABLE:
				case TEXIS_TABLE:
				case TEXIS_BTREEINDEX_TABLE:
				case TEXIS_LINK:
				case TEXIS_VIEW:
				gotIt:
					fname = getfld(w, &sz);
					if (type[0] == TEXIS_VIEW ||
#ifdef MSDOS
				   /* Legacy looser definition of absolute: */
					    (*fname == PATH_SEP ||
					     *fname == '/') ||
#endif /* MSDOS */
					    TX_ISABSPATH(fname))
						return(TXstrdup(TXPMBUFPN,
							Fn, fname));
					else
						return(TXstrcat2(ddic->pname,
							fname));
					break;
				case TEXIS_TEMP_TABLE:
				case TEXIS_DEL_TABLE:
				default:
					/* not accessible tables */
					break;
				}
                        }
        return (char *)NULL;
}

char *
ddgettable(ddic, tname, type, reset)
DDIC *ddic;
char *tname;	/* (in) logical name of table */
char *type;	/* (out) SYSTABLES.TYPE of table (e.g. TEXIS_TABLE) */
int  reset;
{
	char	orgType = *type;
	char	*ret;

	*type = '\0';				/* any user-visible type */
	ret = TXddgetanytable(ddic, tname, type, reset);
	/* WTF some callers (e.g. createdbtbl()) expect `*type' to remain
	 * unchanged if the call fails, so restore it if so:
	 */
	if (ret == CHARPN) *type = orgType;
	return(ret);
}

/******************************************************************/

char *
ddgettablecreator(ddic, tname)
DDIC *ddic;
char *tname;
{
	static CONST char Fn[] = "ddgettablecreator";
        TBL *tbl;
        FLD *n, *w;
        char *fname;
        size_t sz;
        BTLOC where;

#ifdef MSDOS
	if (!strncmp(tname, "SYS", 3))
		ddreset(ddic);
#endif
#ifndef NO_CACHE_TABLE
	makevalidtable(ddic, SYSTBL_TABLES);
	tbl = ddic->tbltblcache->tbl;
	if (!tbl)
		return NULL;
#else
        tbl = ddic->tabletbl;
#endif
        if (ddic->tablendx)
                where = btsearch(ddic->tablendx, strlen(tname), tname);

        n = nametofld(tbl, "NAME");
        w = nametofld(tbl, "CREATOR");

	if(!n || !w)
	{
		putmsg(MERR, Fn, SysTablesCorruptedMsg);
		return NULL;
	}


        if (ddic->tablendx == (BTREE *)NULL)
        {
                rewindtbl(tbl);
                while(TXrecidvalid(gettblrow(tbl, NULL)))
                {
                        if (strcmp(getfld(n, &sz), tname) == 0)
                        {
                                fname = getfld(w, &sz);
                                return strdup(fname);
                        }
                }
        }
        else
                if(TXrecidvalid(gettblrow(tbl, &where)))
                        if (strcmp(getfld(n, &sz), tname) == 0)
                        {
                                fname = getfld(w, &sz);
                                return strdup(fname);
                        }
        return (char *)NULL;
}

/******************************************************************/

int
TXinitenumtables(DDIC *ddic)
{
        TBL *tbl;

#ifndef NO_CACHE_TABLE
	makevalidtable(ddic, SYSTBL_TABLES);
	tbl = ddic->tbltblcache->tbl;
#else
        tbl = ddic->tabletbl;
#endif
	if (!tbl)
		return -1;
	rewindtbl(tbl);
	return 0;
}

/******************************************************************/

int
TXenumtables(DDIC *ddic, char *name, char *creator)
{
	static CONST char Fn[] = "TXenumtables";
        TBL *tbl;
        FLD *n, *w;

#ifndef NO_CACHE_TABLE
	tbl = ddic->tbltblcache->tbl;
#else
        tbl = ddic->tabletbl;
#endif

	if(TXrecidvalid(gettblrow(tbl, NULL)))
	{
		n = nametofld(tbl, "NAME");
		w = nametofld(tbl, "CREATOR");
		if(!n || !w)
		{
			putmsg(MERR, Fn, SysTablesCorruptedMsg);
			return -1;
		}

		TXstrncpy(name, getfld(n, NULL), DDNAMESZ+1);
		TXstrncpy(creator, getfld(w, NULL), DDNAMESZ+1);
		return 1;
	}
	return 0;
}

/******************************************************************/

