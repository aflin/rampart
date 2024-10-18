#include "txcoreconfig.h"
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include "dbquery.h"
#include "texint.h"

/******************************************************************/

static DD *dd32todd64 ARGS((DD *dd32));
static DBTBL *dbftodbtbl(DBF *dbf, char *tbname);
static int checkdbtbl(DBTBL *dbtbl);

/******************************************************************/

/*
	Convert a table from N bit file
*/

int
TXconverttbl(char *fname, int nbits)
{
	static CONST char tblext[] = ".tbl";
	static CONST char blbext[] = ".blb";
	int rc = 0, hasblob, ok;
	DBTBL *srctbl = NULL, *dsttbl = NULL;
	DD *dd64;

	if (EPI_OFF_T_BITS == nbits) return(0);		/* nothing to do */

	if (EPI_OFF_T_BITS == 64 && nbits == 32)
	{
		/* Nbits expanding, so we can do it */

		char full_fname[PATH_MAX];
		char directory[PATH_MAX];
		char *tempfile, *dot, *tdot;
		int  sTxKdbfQuickOpen;
		DBF *srck;
		FLDOP *fo = dbgetfo();

		fullpath(full_fname, fname, PATH_MAX);
		dot = strrchr(full_fname, '.');
		if (dot == CHARPN)
		{
			putmsg(MERR + UGE, CHARPN,
				"Filename must have an extension");
			rc = -1;
			goto end;
		}
		if (strcmp(dot, tblext) != 0)	/* assumed .tbl below */
		{
			putmsg(MERR + UGE, CHARPN,
				"Cannot convert a %s file from %d to %d bits",
				dot, nbits, EPI_OFF_T_BITS);
			rc = -1;
			goto end;
		}
		TXdirname(TXPMBUFPN, directory, sizeof(directory), full_fname);
		tempfile = TXtempnam(directory, CHARPN, CHARPN);
		if(!tempfile)
		{
			rc = -1;
			goto end;
		}
		sTxKdbfQuickOpen = TxKdbfQuickOpen;
		TxKdbfQuickOpen = 1;
		srck = opendbf(TXPMBUFPN, full_fname, O_RDONLY);
		TxKdbfQuickOpen = sTxKdbfQuickOpen;
		if(!srck)
		{
			rc = -1;
			putmsg(MERR + FOE, NULL, "Could not open table %s", full_fname);
			goto end;
		}
		ioctldbf(srck, (DBF_KAI | KDBF_IOCTL_IGNBADPTRS), (void *)1);
		*dot = '\0';			/* temp zap .tbl */
		srctbl = dbftodbtbl(srck, full_fname);
		*dot = '.';			/* restore .tbl */

		if(!srctbl)
		{
			rc = -1;
			putmsg(MERR + FOE, NULL, "Could not open table %s", full_fname);
			goto end;
		}
		if (srctbl->tbl->bf != NULL)	/* wtf drill */
			ioctldbf(srctbl->tbl->bf,
				(DBF_KAI | KDBF_IOCTL_IGNBADPTRS), (void *)1);
		if(0 == checkdbtbl(srctbl))
		{
			/* Can do straight copy */
			dsttbl = TXopentmpdbtbl(tempfile, "T", NULL, srctbl->tbl->dd, NULL);
			while(tup_read(srctbl, fo, SQL_FETCH_NEXT, 1, NULL,
					TXCOUNTINFOPN))
			{
				tup_write(dsttbl, srctbl, fo, 0);
			}
		}
		else	/* need to convert file offsets (size and value) */
		{
			dd64 = dd32todd64(srctbl->tbl->dd);
			if (dd64 == (DD *)NULL)
				rc = -1;
			else
			{
				dsttbl = TXopentmpdbtbl(tempfile, "T", NULL, dd64, NULL);
				while(tup_read(srctbl, fo, SQL_FETCH_NEXT, 1, NULL, TXCOUNTINFOPN))
				{
					tup_write(dsttbl, srctbl, fo, 0);
				}
				dd64 = closedd(dd64);
			}
		}
		dsttbl = closedbtbl(dsttbl);
		hasblob = (srctbl->tbl->bf != NULL ? 1 : 0);
		srctbl = closedbtbl(srctbl);
		tdot = tempfile + strlen(tempfile);
		if (rc < 0)		/* failed */
		{
			strcpy(tdot, tblext);
			deldbf(tempfile);
			if (hasblob)
			{
				strcpy(tdot, blbext);
				deldbf(tempfile);
			}
		}
		else if(deldbf(full_fname) != -1)
		{
			ok = 1;
			if (hasblob)
			{
				strcpy(dot, blbext);
				ok = (deldbf(full_fname) != -1);
				strcpy(dot, tblext);	/* restore */
			}
			if (ok)
			{
				strcpy(tdot, tblext);
				errno = 0;
				rc = rename(tempfile, full_fname);
				if (rc == 0 && hasblob)
				{
					strcpy(dot, blbext);
					strcpy(tdot, blbext);
					errno = 0;
					rc = rename(tempfile, full_fname);
				}
				if (rc != 0)
					putmsg(MERR + FNE, CHARPN,
						"Can't rename %s to %s: %s",
						tempfile, full_fname,
						strerror(errno));
			}
		}
		fo = foclose(fo);
	}
	else if (EPI_OFF_T_BITS == 32 && nbits == 64)
	{
		/* Nbits expanding, so we may have problems */
		/* WTF: Allow split into multiple files */
		putmsg(MERR + UGE, CHARPN,
			"Cannot convert %d-bit file to %d-bit",
			nbits, EPI_OFF_T_BITS);
		rc = -1;
	}
	else
	{
		putmsg(MERR + UGE, CHARPN,
			"Can't convert %d-bit table to %d-bit",
			nbits, EPI_OFF_T_BITS);
		rc = -1;
	}
end:
	dsttbl = closedbtbl(dsttbl);
	srctbl = closedbtbl(srctbl);
	return rc;
}

/******************************************************************/

static DBTBL *
dbftodbtbl(DBF *dbf, char *tbname)
{
	static const char	fn[] = "dbftodbtbl";
	DBTBL *rc = NULL;
	TBL *tbl;

	tbl = opentbl_dbf(dbf, tbname);
	if(!tbl) goto end;

	rc = (DBTBL *)TXcalloc(dbf->pmbuf, fn, 1, sizeof(DBTBL));
	if(!rc) goto end;

	rc->tbl = tbl;
	rc->type = 'T';
	rc->rname = NULL;
end:
	return rc;
}

/******************************************************************/

static DD *
dd32todd64(dd32)
DD	*dd32;
/* Creates and returns a 64-bit-file copy of 32-bit-file `*dd32'
 * (ie. blob/off_t types fixed).  Assumes dd32 is result of convertdd().
 */
{
	static CONST char	Fn[] = "dd32todd64";
	DD	*dd64;
	DDFD	*fd;
	int	i;

	dd64 = (DD *)calloc(1, sizeof(DD) + (dd32->n*sizeof(DDFD)));
	if (dd64 == (DD *)NULL)
	{
		putmsg(MERR + MAE, (char *)Fn, "Out of memory");
		return(NULL);
	}
	memcpy(dd64, dd32, sizeof(DD) + ((dd32->n - 1)*sizeof(DDFD)));
	for (i = 0; i < dd64->n; i++)
	{
		fd = dd64->fd + i;
		switch (fd->type & DDTYPEBITS)
		{
		case FTN_BLOBI:
			if (!TXisblob(fd->sttype) || fd->stelsz <= 0)
				break;
			fd->stsize = (fd->stsize/fd->stelsz)*sizeof(ft_blob);
			fd->stelsz = sizeof(ft_blob);
			/* elsz/size set by convertdd() */
			break;
		case FTN_HANDLE:
			fd->size = (fd->size/fd->elsz)*sizeof(ft_handle);
			fd->elsz = sizeof(ft_handle);
			break;
		}
	}
	return(dd64);
}

/******************************************************************/

static int
checkdbtbl(DBTBL *dbtbl)
/* Returns 1 if `dbtbl' has any EPI_OFF_T_BITS-sensitive fields,
 * 0 if not.
 */
{
	int j;
	TBL *tbl;
	FLD *f;

	if(!dbtbl)
		return 0;
	tbl = dbtbl->tbl;

	if(tbl->bf)		/* blob offsets are EPI_OFF_T */
		return 1;

	for (j = 0; j < tbl->n; j++)
	{
		f = TXgetrfldn(tbl, j, NULL);
		if(!f)
			continue;
		if((f->type & DDTYPEBITS) == FTN_HANDLE)
			return 1;
	}
	return 0;
}


/******************************************************************/


#ifdef TEST

int
main(int argc, char *argv[])
{
	if(argc < 3)
		return -1;
	TXconverttbl(argv[2], atoi(argv[1]));
}

#endif
