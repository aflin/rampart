/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>

#ifdef USE_EPI
#include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"

static	char	tempbuf[PATH_MAX];

/******************************************************************/

static int fieldmatch ARGS((char *, char *));

static int
fieldmatch(sfield, ifield)
char *sfield;
char *ifield;
{
	char	*x, *y;

	x = strstr(sfield, ifield);
	if(!x)
		return 0;
	if(x[strlen(ifield)] == '\x0' ||
	   TX_ISSPACE((int)((unsigned char *)x)[strlen(ifield)]))
	{
		for(y=sfield; y < x; y++)
		{
			if((*y != ',') &&
			   !TX_ISSPACE((int)*(unsigned char *)y))
				return 0;
		}
		return 1;
	}
	if(!strcmp(sfield, ifield))
		return 1;
	return 0;
}

/******************************************************************/

int
ddgetindex(ddic, tname, fldn, itype, paths, fields, sysindexParamsVals)
DDIC *ddic;		/* The data dictionary */
char *tname;		/* (in, opt.) Table name */
char *fldn;		/* (in, opt.) Field name */
char **itype;		/* (out, opt., alloc'd) Index types */
char ***paths;		/* (out, opt., alloc'd) Paths (sans extensions) */
char ***fields;		/* (out, opt., alloc'd) Field names */
char ***sysindexParamsVals; /* (out, opt., alloc'd) SYSINDEX.PARAMS values */
/* Gets alloc'd arrays of info about indexes matching `tname' and `fldn'.
 * Returns number of matching indexes found, or -1 on error.
 */
{
	TBL *tbl;
	FLD *tbnameFld, *fnameFld, *fieldsFld, *typeFld, *paramsFld;
	size_t sz;
#ifdef NO_CACHE_TABLE
	int i;
#endif
	int num = 0, n1 = 0;
	RECID *rc;
	TXPMBUF	*pmbuf = ddic->pmbuf;

	if (itype) *itype = NULL;
	if (paths) *paths = NULL;
	if (fields) *fields = NULL;
	if (sysindexParamsVals) *sysindexParamsVals = NULL;

	if (TXverbosity > 1)
		txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			       "Looking for index on %s (%s)",
			       (tname ? tname : "any table"),
			       (fldn ? fldn : "any field"));
#ifndef NO_CACHE_TABLE
	makevalidtable(ddic, SYSTBL_INDEX);
	tbl = ddic->indtblcache->tbl;
	if (!tbl) goto finally;
#else
	tbl = ddic->indextbl;
#endif

	tbnameFld =  ddic->indtblcache->flds[0];	/* TBNAME */
	fnameFld =  ddic->indtblcache->flds[1];		/* FNAME */
	fieldsFld =  ddic->indtblcache->flds[2];	/* FIELDS */
	typeFld =  ddic->indtblcache->flds[3];		/* TYPE */
	paramsFld = ddic->indtblcache->flds[6];		/* PARAMS */

	num = 0;
#ifdef NO_CACHE_TABLE
	if(TXlocksystbl(ddic, SYSTBL_INDEX, R_LCK, NULL)==-1)
		goto err;
#endif
	rewindtbl(tbl);
	rc = gettblrow(tbl, NULL); 
#ifdef NO_CACHE_TABLE
	TXunlocksystbl(ddic, SYSTBL_INDEX, R_LCK);
#endif
	while (TXrecidvalid(rc))
	{
		if ((!tname || strcmp(getfld(tbnameFld, &sz), tname) == 0) &&
		    (fldn==(char *)NULL || fieldmatch(getfld(fieldsFld, &sz), fldn)))
			num++;
#ifdef NO_CACHE_TABLE
		if(TXlocksystbl(ddic, SYSTBL_INDEX, R_LCK, NULL)==-1)
			goto err;
#endif
		rc = gettblrow(tbl, NULL); 
#ifdef NO_CACHE_TABLE
		TXunlocksystbl(ddic, SYSTBL_INDEX, R_LCK);
#endif
	}
	if (num == 0)
		goto finally;
	if (itype == NULL && paths == NULL && fields == NULL &&
	    sysindexParamsVals == CHARPPPN)
		goto finally;
	if (itype)
	{
		*itype = (char *)TXcalloc(pmbuf, __FUNCTION__, num+1,
					  sizeof(char));
		if (*itype == (char *)NULL)
			goto err;
	}
	if(paths)
	{
		*paths = (char **)TXcalloc(pmbuf, __FUNCTION__, num+1,
					   sizeof(char *));
		if (*paths == (char **)NULL)
			goto err;
	}
	if(fields)
	{
		*fields = (char **)TXcalloc(pmbuf, __FUNCTION__, num+1,
					    sizeof(char *));
		if (*fields == (char **)NULL)
			goto err;
	}
	if (sysindexParamsVals != CHARPPPN)
	{
		*sysindexParamsVals = (char **)TXcalloc(pmbuf, __FUNCTION__,
							num+1, sizeof(char *));
		if (*sysindexParamsVals == CHARPPN)
			goto err;
	}
#ifdef NO_CACHE_TABLE
	if(TXlocksystbl(ddic, SYSTBL_INDEX, R_LCK, NULL)==-1)
		goto err;
#endif
	rewindtbl(tbl);
	rc = gettblrow(tbl, NULL); 
#ifdef NO_CACHE_TABLE
	TXunlocksystbl(ddic, SYSTBL_INDEX, R_LCK);
#endif
	while (TXrecidvalid(rc))
	{
		if ((!tname || strcmp(getfld(tbnameFld, &sz), tname) == 0) &&
		    (fldn==(char *)NULL || fieldmatch(getfld(fieldsFld, &sz), fldn)))
		{
			void	*v;

			if (itype)
			{
				v = getfld(typeFld, &sz);
				(*itype)[n1] = *(char *)(v ? v : "");
			}
			if (paths) (*paths)[n1] = TXstrdup(pmbuf, __FUNCTION__,
                              TXddicfname(ddic, getfld(fnameFld, NULL)));
			if (fields) (*fields)[n1] = TXstrdup(pmbuf,
							     __FUNCTION__, 
                                   getfld(fieldsFld, NULL));
			if (sysindexParamsVals != CHARPPPN)
				(*sysindexParamsVals)[n1] = TXstrdup(pmbuf,
						__FUNCTION__, paramsFld ?
						getfld(paramsFld, NULL) : "");
			n1++;
		}
#ifdef NO_CACHE_TABLE
		if(TXlocksystbl(ddic, SYSTBL_INDEX, R_LCK, NULL)==-1)
			goto err;
#endif
		rc = gettblrow(tbl, NULL); 
#ifdef NO_CACHE_TABLE
		TXunlocksystbl(ddic, SYSTBL_INDEX, R_LCK);
#endif
	}
	goto finally;

err:
	if (itype)
		*itype = TXfree(*itype);
	if (paths)
		*paths = TXfreeStrList(*paths, num);
	if (fields)
		*fields = TXfreeStrList(*fields, num);
	if (sysindexParamsVals)
		*sysindexParamsVals = TXfreeStrList(*sysindexParamsVals, num);
finally:
	return(num);
}

/******************************************************************/

int
TXddgetindexinfo(ddic, tname, fldn, itype, iunique, names, files, fields,
		 params, tableNames)
DDIC *ddic;		/* (int) The data dictionary */
char *tname;		/* (in, opt.) Table name */
char *fldn;		/* (in, opt.) Field name */
char **itype;		/* (out, opt.) Index type */
char **iunique;		/* (out, opt.) Index non-uniqueness */
char ***names;		/* (out, opt.) Where to put the index names */
char ***files;	        /* (out, opt.) Where to put the file names */
char ***fields;		/* (out, opt.) SYSINDEX.FIELDS */
char ***params;		/* (out, opt.) SYSINDEX.PARAMS (empty if !present) */
char ***tableNames;	/* (out, opt.) SYSINDEX.TBNAME */
/* Gets alloc'd index info for indexes matching `tname' and `fldn'.
 * Returns number of matched indexes, or -1 on error.
 */
{
	static const char	fn[] = "TXddgetindexinfo";
	TBL *tbl;
	FLD *tbnameFld, *nameFld, *fieldsFld, *typeFld, *nonUniqueFld;
	FLD	*fnameFld, *paramsFld;
	size_t sz;
	int i, num, n1 = 0;
	RECID *rc;
	TXPMBUF	*pmbuf = ddic->pmbuf;

#ifndef NO_CACHE_TABLE
	makevalidtable(ddic, SYSTBL_INDEX);
	tbl = ddic->indtblcache->tbl;
	if (!tbl)
		return 0;
#else
	tbl = ddic->indextbl;			     /* Alias pointer */
#endif

	tbnameFld = ddic->indtblcache->flds[0];		/* TBNAME */
	fnameFld = ddic->indtblcache->flds[1];		/* FNAME */
	fieldsFld = ddic->indtblcache->flds[2];		/* FIELDS */
	typeFld = ddic->indtblcache->flds[3];		/* TYPE */
	nonUniqueFld = ddic->indtblcache->flds[4];	/* NON_UNIQUE */
	nameFld = ddic->indtblcache->flds[5];		/* NAME */
	paramsFld = ddic->indtblcache->flds[6];		/* PARAMS */

	num = 0;				   /* Initialization */
	if(names) *names = (char **)NULL;
	if(files)   *files = (char **)NULL;
	if(fields)  *fields = (char **)NULL;
	if(params)  *params = (char **)NULL;
	if(itype)   *itype = (char *)NULL;
	if(iunique) *iunique = (char *)NULL;
	if (tableNames) *tableNames = NULL;

	/* Count the number of indices that pertain to this table */

#ifdef NO_CACHE_TABLE
	if(TXlocksystbl(ddic, SYSTBL_INDEX, R_LCK, NULL)==-1)
		return -1;
#endif
	rewindtbl(tbl);		  /* Make sure we look at all indices */
	rc = gettblrow(tbl, NULL); 
#ifdef NO_CACHE_TABLE
	TXunlocksystbl(ddic, SYSTBL_INDEX, R_LCK);
#endif

	while (TXrecidvalid(rc))
	{
		if ((!tname || !strcmp(getfld(tbnameFld, &sz), tname)) &&
		    (fldn==(char *)NULL || fieldmatch(getfld(fieldsFld, &sz), fldn)))
			num++;
#ifdef NO_CACHE_TABLE
		if(TXlocksystbl(ddic, SYSTBL_INDEX, R_LCK, NULL)==-1)
			return -1;
#endif
		rc = gettblrow(tbl, NULL); 
#ifdef NO_CACHE_TABLE
		TXunlocksystbl(ddic, SYSTBL_INDEX, R_LCK);
#endif
	}

	if (num == 0)					 /* No indices */
		return 0;

	if(names)
	{
		*names = (char **)TXcalloc(pmbuf, fn, num, sizeof(char *));
		if (*names == (char **)NULL)
			return -1;
	}

	if(fields)
	{
		*fields = (char **)TXcalloc(pmbuf, fn, num, sizeof(char *));
		if (*fields == (char **)NULL)
		{
			if(names) *names = TXfree(*names);
			return -1;
		}
	}

	if(params)
	{
		*params = (char **)TXcalloc(pmbuf, fn, num, sizeof(char *));
		if (*params == (char **)NULL)
		{
			if(names) *names = TXfree(*names);
			if(fields) *fields = TXfree(*fields);
			return -1;
		}
	}

	if (tableNames)
	{
		*tableNames = (char **)TXcalloc(pmbuf, fn, num,
						sizeof(char *));
		if (!*tableNames)
		{
			if (params) *params = TXfree(*params);
			if (names) *names = TXfree(*names);
			if (fields) *fields = TXfree(*fields);
			return(-1);
		}
	}

	if(itype)
	{
		*itype = (char *)TXcalloc(pmbuf, fn, num, sizeof(char));
		if (*itype == (char *)NULL)
		{
			if(names) *names = TXfree(*names);
			if(fields) *fields = TXfree(*fields);
			if(params) *params = TXfree(*params);
			if (tableNames) *tableNames = TXfree(tableNames);
			return -1;
		}
	}

	if(iunique)
	{
		*iunique = (char *)TXcalloc(pmbuf, fn, num, sizeof(char));
		if (*iunique == (char *)NULL)
		{
			if(names) *names = TXfree(*names);
			if(fields) *fields = TXfree(*fields);
			if(params) *params = TXfree(*params);
			if (tableNames) *tableNames = TXfree(tableNames);
			if(itype) *itype = TXfree(*itype);
			return -1;
		}
	}

	if(files)
	{
		*files = (char **)TXcalloc(pmbuf, fn, num, sizeof(char *));
		if (*files == (char **)NULL)
		{
			if(names) *names = TXfree(*names);
			if(fields) *fields = TXfree(*fields);
			if(params) *params = TXfree(*params);
			if (tableNames) *tableNames = TXfree(tableNames);
			if(itype) *itype = TXfree(*itype);
			if(iunique) *iunique = TXfree(*iunique);
			return -1;
		}
	}

#ifdef NO_CACHE_TABLE
	if(TXlocksystbl(ddic, SYSTBL_INDEX, R_LCK, NULL)==-1)
	{
		if(names) *names = TXfree(*names);
		if(fields) *fields = TXfree(*fields);
		if(params) *params = TXfree(*params);
		if (tableNames) *tableNames = TXfree(tableNames);
		if(itype) *itype = TXfree(*itype);
		if(iunique) *iunique = TXfree(*iunique);
		if(files) *files = TXfree(*files);
		return -1;
	}
#endif
	rewindtbl(tbl);
	rc = gettblrow(tbl, NULL); 
#ifdef NO_CACHE_TABLE
	TXunlocksystbl(ddic, SYSTBL_INDEX, R_LCK);
#endif

	for(i=0; i < num; i++)
	{
		if(files)
			(*files)[i]   = NULL;
		if(names)
			(*names)[i] = NULL;
		if(fields)
			(*fields)[i]  = NULL;
		if(params)
			(*params)[i]  = NULL;
		if (tableNames)
			(*tableNames)[i] = NULL;
	}

	while (TXrecidvalid(rc))
	{
		if ((!tname || !strcmp(getfld(tbnameFld, &sz), tname)) &&
		    (fldn==(char *)NULL || fieldmatch(getfld(fieldsFld, &sz), fldn)))
		{
			if(files)
				(*files)[n1] = TXstrdup(pmbuf, fn,
					  TXddicfname(ddic, getfld(fnameFld, NULL)));
			if(names)
				(*names)[n1] = TXstrdup(pmbuf, fn,
							  getfld(nameFld, &sz));
			if(fields)
				(*fields)[n1] = TXstrdup(pmbuf, fn,
							 getfld(fieldsFld, &sz));
			if(params)
			{
				(*params)[n1] = TXstrdup(pmbuf, fn,
						   (paramsFld ? getfld(paramsFld, &sz) : ""));
			}
			if (tableNames)
			{
				char	*tbName;
				tbName = (char *)getfld(tbnameFld, &sz);
				(*tableNames)[n1] = TXstrdup(pmbuf, fn,
						      (tbName ? tbName : ""));
			}
			if(itype)
				(*itype)[n1] = *(char *)getfld(typeFld, &sz);
			if(iunique)
				(*iunique)[n1] = *(char *)getfld(nonUniqueFld, &sz);
			n1++;
		}
#ifdef NO_CACHE_TABLE
		if(TXlocksystbl(ddic, SYSTBL_INDEX, R_LCK, NULL) == -1)
		{
			for(i=0; i < n1; i++)
			{
				if(files)
				{
					(*files)[i]=TXfree((*files)[i]);
				}
				if(names)
				{
					(*names)[i]=TXfree((*names)[i]);
				}
				if(fields)
				{
					(*fields)[i]=TXfree((*fields)[i]);
				}
				if(params)
				{
					(*params)[i]=TXfree((*params)[i]);
				}
				if (tableNames)
					(*tableNames)[i] =
						TXfree((*tableNames)[i]);
			}
			if(files)
			{
				*files = TXfree(*files);
			}
			if(names)
			{
				*names = TXfree(*names);
			}
			if(fields)
			{
				*fields = TXfree(*fields);
			}
			if(params)
			{
				*params = TXfree(*params);
			}
			if (tableNames)
				*tableNames = TXfree(*tableNames);
			if(itype)
			{
				*itype = TXfree(*itype);
			}
			if(iunique)
			{
				*iunique = TXfree(*iunique);
			}
			return -1;
		}
#endif
		rc = gettblrow(tbl, NULL); 
#ifdef NO_CACHE_TABLE
		TXunlocksystbl(ddic, SYSTBL_INDEX, R_LCK);
#endif
	}
	return num;
}

/******************************************************************/

int
ddgetindexbyname(ddic, tname, itype, nonUnique, paths, tableNames, fields,
		 sysindexParamsVals)
DDIC *ddic;		/* The data dictionary */
char *tname;		/* Index name */
char **itype;		/* Index type */
char **nonUnique;	/* (out, alloced) 1 if non-unique, 0 if unique */
char ***paths;		/* Where to put the paths */
char ***tableNames;	/* Where to put the table names */
char ***fields;		/* Where to put the field names */
char ***sysindexParamsVals;	/* (out, opt.) SYSINDEX.PARAMS values */
{
	static CONST char	fn[] = "ddgetindexbyname";
	TBL *tbl;
	FLD *nameFld, *fnameFld, *fieldsFld, *typeFld, *tbnameFld;
	FLD	*paramsFld = FLDPN, *nonUniqueFld = NULL;
	size_t sz;
	int num, n1 = 0;
	char	*fldVal;
	TXPMBUF	*pmbuf = ddic->pmbuf;

	if(itype) *itype = (char *)NULL;
	if (nonUnique) *nonUnique = NULL;
	if(paths) *paths = (char **)NULL;
	if(tableNames) *tableNames = (char **)NULL;
	if(fields) *fields = (char **)NULL;
	if (sysindexParamsVals != CHARPPPN) *sysindexParamsVals = CHARPPN;

#ifndef NO_CACHE_TABLE
	makevalidtable(ddic, SYSTBL_INDEX);
	tbl = ddic->indtblcache->tbl;
	if (!tbl)
		return 0;
#else
	tbl = ddic->indextbl;
#endif

	tbnameFld =  ddic->indtblcache->flds[0];		/* TBNAME */
	fnameFld =  ddic->indtblcache->flds[1];		/* FNAME */
	fieldsFld =  ddic->indtblcache->flds[2];		/* FIELDS */
	typeFld =  ddic->indtblcache->flds[3];		/* TYPE */
	nonUniqueFld =  ddic->indtblcache->flds[4];	/* NON_UNIQUE */
	nameFld =  ddic->indtblcache->flds[5];		/* NAME */
	paramsFld =  ddic->indtblcache->flds[6];	/* PARAMS */

	num = 0;
#ifdef NO_CACHE_TABLE
	if (TXlocksystbl(ddic, SYSTBL_INDEX, R_LCK, NULL) == -1)
		return(0);
#endif
	rewindtbl(tbl);
	while (TXrecidvalid(gettblrow(tbl, NULL)))
		if (!strcmp(getfld(nameFld, &sz), tname))
			num++;
#ifdef NO_CACHE_TABLE
	TXunlocksystbl(ddic, SYSTBL_INDEX, R_LCK);
#endif
	if (num == 0)
		return 0;
	if(paths)
	{
		*paths = (char **)TXcalloc(pmbuf, fn, num, sizeof(char *));
		if (*paths == (char **)NULL)
		{
			return -1;
		}
	}
	if(tableNames)
	{
		*tableNames = (char **)TXcalloc(pmbuf, fn, num, sizeof(char *));
		if (*tableNames == (char **)NULL)
		{
			if(paths) TXfree (*paths);
			return -1;
		}
	}
	if(fields)
	{
		*fields = (char **)TXcalloc(pmbuf, fn, num, sizeof(char *));
		if (*fields == (char **)NULL)
		{
			if(paths) TXfree (*paths);
			if(tableNames)
				TXfree (*tableNames);
			return -1;
		}
	}
	if (sysindexParamsVals != CHARPPPN)
	{
		*sysindexParamsVals = (char **)TXcalloc(pmbuf, fn, num,
							sizeof(char *));
		if (*sysindexParamsVals == CHARPPN) goto bail1;
	}
	if(itype)
	{
		*itype = (char *)TXcalloc(pmbuf, fn, num, sizeof(char));
			if (*itype == (char *)NULL)
			{
			bail2:
				if (sysindexParamsVals != CHARPPPN)
					*sysindexParamsVals =
						TXfree(*sysindexParamsVals);
			bail1:
				if (paths) TXfree(*paths);
				if (tableNames) TXfree(*tableNames);
				if (fields) TXfree(*fields);
				return -1;
			}
	}
	if (nonUnique)
	{
		*nonUnique = (char *)TXcalloc(TXPMBUFPN, fn, num,
					      sizeof(char));
		if (!*nonUnique)
		{
			if (itype) *itype = TXfree(*itype);
			goto bail2;
		}
	}
#ifdef NO_CACHE_TABLE
	if (TXlocksystbl(ddic, SYSTBL_INDEX, R_LCK, NULL) == -1)
		goto bail2;
#endif
	rewindtbl(tbl);
	while (TXrecidvalid(gettblrow(tbl, NULL)))
	{
		if (!strcmp(getfld(nameFld, &sz), tname))
		{
			if(paths)
				(*paths)[n1] = TXstrdup(pmbuf, fn,
					  TXddicfname(ddic, getfld(fnameFld,
								   NULL)));
			if(tableNames)
				(*tableNames)[n1] = TXstrdup(pmbuf, fn,
							 getfld(tbnameFld, &sz));
			if(fields)
				(*fields)[n1] = TXstrdup(pmbuf, fn,
							 getfld(fieldsFld, &sz));
			if (sysindexParamsVals != CHARPPPN)
				(*sysindexParamsVals)[n1] = TXstrdup(pmbuf,fn,
				      paramsFld ? getfld(paramsFld, &sz) : "");
			if(itype)
				(*itype)[n1] = *(char *)getfld(typeFld, &sz);
			if (nonUnique)
			{
				fldVal = (char *)getfld(nonUniqueFld, NULL);
				if (fldVal)
					(*nonUnique)[n1] = *fldVal;
			}
			n1++;
		}
	}
#ifdef NO_CACHE_TABLE
	TXunlocksystbl(ddic, SYSTBL_INDEX, R_LCK);
#endif
	return num;
}

char *
TXddicfname(ddic, fname)
DDIC *ddic;
char *fname;
{
	if (fname[0] == PATH_SEP ||
#if defined(MSDOS)
	    fname[1] == ':' ||
#endif
	    fname[0] == '~')
		strcpy(tempbuf, "");
	else
		strcpy(tempbuf, ddic->pname);
	strcat(tempbuf, fname);
	return tempbuf;
}
