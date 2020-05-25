/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#  include <io.h>
#endif /* _WIN32 */
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"				/* for htsnpf() */
#include "kdbfi.h"				/* WTF for KDBF drill */

#ifdef unix
#  define writeall(a, b, c, d, e) TXwriteall((a), (c), (d), (e))
#endif
#ifdef _WIN32
#  define writeall(a, b, c, d, e) TXwriteall((a), (b), (c), (d), (e))
#endif


int     TxLoadTblRetries = 30;          /* make lower for monitor */

/******************************************************************/

int
#ifdef unix
TXwriteall(fd, buf, sz, usewrite)
#endif
#ifdef _WIN32
TXwriteall(fd, handle, buf, sz, usewrite)
HANDLE handle;
#endif
int fd;
char *buf;
size_t sz;
int usewrite;
{
	static CONST char	Fn[] = "TXwriteall";
	size_t towrite = sz;
	int nwrote, totwrote = 0;

	while(towrite > 0)
	{
#ifdef _WIN32
		if (fd < 0)			/* use `handle' */
		{
			DWORD	dnwrote;
			if (WriteFile(handle, buf, towrite, &dnwrote, NULL))
				nwrote = (int)dnwrote;
			else			/* error */
				nwrote = -1;
		}
		else
#endif /* _WIN32 */
		{
			/* NOTE: check `usewrite' and use write() if
			 * write function does not support non-sockets:
			 */
			(void)usewrite;
			if (TXezWaitForSocketWritability(TXPMBUFPN, HtTraceSkt,
							 __FUNCTION__,
							 fd, 10.0) < 1)
				nwrote = -1;
			else
				nwrote = TXezSocketWrite(TXPMBUFPN, HtTraceSkt,
							 __FUNCTION__, fd, NULL, buf,
							 towrite, TXbool_False,
							 NULL);
		}
		if(nwrote < 0)
			return totwrote;
		if (TXtracedumptable & 0x8)
		{
			putmsg(TXTRACEDUMPTABLE_MSG_AFTER, Fn,
				"Sent %wd (0x%wx) bytes raw data",
				(EPI_HUGEINT)nwrote, (EPI_HUGEINT)nwrote);
			tx_hexdumpmsg(TXPMBUFPN, TXTRACEDUMPTABLE_MSG_AFTER,
					CHARPN, (byte *)buf, nwrote, 1);
		}
		totwrote += nwrote;
		buf += nwrote;
		towrite -= nwrote;
	}
	return totwrote;
}

/******************************************************************/
/* Handle the most common conversions from text.  Save fldmath */

static char *totext ARGS((FLD *, size_t *));

static char *
totext(fld, len)
FLD *fld;
size_t *len;
{
	static CONST char	fn[] = "totext";
	char *rc = NULL;

	if(fld->n > 1)
		return rc;
	switch(fld->type)
	{
	case FTN_COUNTER:
		rc = TXmalloc(TXPMBUFPN, fn, EPI_OS_LONG_BITS/2 + 9);
		if(rc)
		{
			ft_counter *res = getfld(fld, NULL);
			sprintf(rc, "%08lx%lx", (long)res->date, (long)res->seq);
		}
		break;
	case FTN_INT:
	case FTN_INTEGER:
		rc = TXmalloc(TXPMBUFPN, fn, EPI_OS_LONG_BITS/3+3);
		if(rc)
		{
			ft_int *res = getfld(fld, NULL);
			sprintf(rc, "%ld", (long)*res);
		}
		break;
	case FTN_DATE:
		rc = TXmalloc(TXPMBUFPN, fn, EPI_OS_LONG_BITS/3+3);
		if(rc)
		{
			ft_date *res = getfld(fld, NULL);
			sprintf(rc, "%ld", (long)*res);
		}
		break;
	case FTN_LONG:
		rc = TXmalloc(TXPMBUFPN, fn, EPI_OS_LONG_BITS/3+3);
		if(rc)
		{
			ft_long *res = getfld(fld, NULL);
			sprintf(rc, "%ld", (long)*res);
		}
		break;
	}
	if(rc)
		*len = strlen(rc);
	return rc;
}

/******************************************************************/

static byte *portfldtobuf ARGS((TBL *, FLDOP *));

static byte *
portfldtobuf(tb, fo)
TBL *tb;
FLDOP *fo;
{
	static CONST char Fn[] = "portfldtobuf";
	unsigned int i;
	size_t	needed, len;
	byte	*rec, *x, *orec;
	int	ivar;
	int	ftype;
	FLD *fconv = FLDPN;
	byte	fvsl[5], *fvslPtr;

	if(!fconv)
		fconv = createfld("varchar", 20, 0);
	setfldv(fconv);
	ivar = ddgetivar(tb->dd);
	if (TXtracedumptable & 0x1)
	{
		char	*s, *d, buf[8192];
#define EOB	(buf + sizeof(buf))

		d = buf;
		for (i = 0; i < tb->n && d < EOB-1; i++)
		{
			s = fldtostr(tb->field[i]);
			if (TXtracedumptable & 0x4)
			{
				for ( ; *s && d < EOB-1; s++, d++)
					*d = (*s>=' ' && *s<='~' ? *s : '.');
			}
			else
			{
				for ( ; *s && d < EOB-1; s++, d++)
					*d = *s;
			}
			if (d < EOB-1 && i + 1 < tb->n) *(d++) = '|';
		}
		*d = '\0';
		putmsg(TXTRACEDUMPTABLE_MSG_BEFORE, Fn,
			"Sending row: %s%s",
			buf, (d < EOB ? "" : "..."));
#undef EOB
	}
	for(needed=i=0;i<tb->n;i++)
	{
		ftype = tb->field[i]->type & DDTYPEBITS;
		switch(ftype)
		{
		case FTN_BLOB:
		case FTN_BLOBI:
			fconv->type = FTN_BYTE + DDVARBIT;
			freeflddata(fconv);
			fopush(fo, tb->field[i]);
			fopush(fo, fconv);
			if(foop(fo, FOP_CNV) == -1)
			{
				putmsg(MERR, Fn,
		"Could not convert field %s type %s to %s (%d to %d)",
					ddgetname(tb->dd, i),
					TXfldtypestr(tb->field[i]),
					ddfttypename(fconv->type),
					tb->field[i]->type, fconv->type);
				goto err;
			}
			closefld(fconv);
			fconv = fopop(fo);
			x=getfld(fconv, &len);
                        fvslPtr = fvsl;
                        if (!TXoutputVariableSizeLong(TXPMBUFPN, &fvslPtr, len,
                                                      "Field size"))
                          goto err;
			needed += len + 1 + (fvslPtr - fvsl);
			if(x != fconv->shadow)
				TXfree(x);	/* save `x' for below? wtf */
			fconv->v = fconv->shadow;
			break;
		case FTN_CHAR:
		case FTN_BYTE:
		case FTN_INDIRECT:
			fvslPtr = fvsl;
			if (!TXoutputVariableSizeLong(TXPMBUFPN, &fvslPtr,
						      tb->field[i]->size+1,
						      "Field size"))
				goto err;
			needed += tb->field[i]->size + 1 + (fvslPtr - fvsl);
			break;
		default:
			x = (byte *)totext(tb->field[i], &len);
			if(!x)
			{
				fconv->type = FTN_CHAR + DDVARBIT;
				freeflddata(fconv);
				fopush(fo, tb->field[i]);
				fopush(fo, fconv);
				if(foop(fo, FOP_CNV) == -1)
				{
					putmsg(MERR, Fn,
		"Could not convert field %s type %s to %s (%d to %d)",
						ddgetname(tb->dd, i),
						TXfldtypestr(tb->field[i]),
						ddfttypename(fconv->type),
						tb->field[i]->type,
						fconv->type);
					goto err;
				}
				closefld(fconv);
				fconv = fopop(fo);
				x=getfld(fconv, &len);
			}
			fvslPtr = fvsl;
			if (!TXoutputVariableSizeLong(TXPMBUFPN, &fvslPtr, len,
						      "Converted field size"))
				goto err;
			needed += len + 1 + (fvslPtr - fvsl);
			if(x != fconv->shadow)
				TXfree(x);	/* save `x' for below? wtf */
			fconv->v = fconv->shadow;
		}
	}
	x = fvsl;
	if (!TXoutputVariableSizeLong(TXPMBUFPN, &x, needed, "Needed-size"))
		goto err;
	len = x - fvsl;
	x = fvsl;
	if (!TXoutputVariableSizeLong(TXPMBUFPN, &x, needed + len,
				      "Needed-size-plus-len"))
		goto err;
	len = x - fvsl;
	needed+=len; /* Enough room for VSL Length */
	if((orec=(byte *)TXmalloc(TXPMBUFPN, Fn, needed))==(byte *)NULL)
		goto err;
	rec = orec;
	if (!TXoutputVariableSizeLong(TXPMBUFPN, &rec, needed, "Needed-size"))
		goto err;
	for(i=0;i<tb->n;i++)
	{
		FLD *f=tb->field[i];
		f->size=f->n*f->elsz;
		ftype = f->type & DDTYPEBITS;
		switch(ftype)
		{
		case FTN_BLOB:
		case FTN_BLOBI:
			fconv->type = FTN_BYTE + DDVARBIT;
			freeflddata(fconv);
			fopush(fo, tb->field[i]);
			fopush(fo, fconv);
			if(foop(fo, FOP_CNV) == -1)
			{
				putmsg(MERR, Fn,
		"Could not convert field %s type %s to %s (%d to %d)",
					ddgetname(tb->dd, i),
					TXfldtypestr(tb->field[i]),
					ddfttypename(fconv->type),
					tb->field[i]->type, fconv->type);
				goto err;
			}
			closefld(fconv);
			fconv = fopop(fo);
			x=getfld(fconv, &len);
			if (!TXoutputVariableSizeLong(TXPMBUFPN, &rec, len,
						      "Field size"))
				goto err;
			memcpy((void *)rec,x,len);
			rec += len;
			if(x != fconv->shadow)
				TXfree(x);
			fconv->v = fconv->shadow;
			break;
		case FTN_CHAR:
		case FTN_BYTE:
		case FTN_INDIRECT:
			if (!TXoutputVariableSizeLong(TXPMBUFPN, &rec,
						      f->size+1, "Field size"))
				goto err;
			memcpy((void *)rec,getfld(f, NULL),f->size);
			rec+=f->size;
			break;
		default:
			x = (byte *)totext(f, &len);
			if(!x)
			{
				if((tb->field[i]->type & DDTYPEBITS) == FTN_BLOBI)
					fconv->type = FTN_BYTE + DDVARBIT;
				else
					fconv->type = FTN_CHAR + DDVARBIT;
				freeflddata(fconv);
				fopush(fo, tb->field[i]);
				fopush(fo, fconv);
				if(foop(fo, FOP_CNV) == -1)
				{
					putmsg(MERR, Fn,
		"Could not convert field %s type %s to %s (%d to %d)",
						ddgetname(tb->dd, i),
						TXfldtypestr(f),
						ddfttypename(fconv->type),
						f->type,
						fconv->type);
					goto err;
				}
				closefld(fconv);
				fconv = fopop(fo);
				x = getfld(fconv, &len);
			}
			if (!TXoutputVariableSizeLong(TXPMBUFPN, &rec, len,
						      "Converted field size"))
				goto err;
			memcpy((void *)rec,x,len);
			rec += len;
			if(x != fconv->shadow)
				TXfree(x);
			fconv->v = fconv->shadow;
			break;
		}
		*rec = '\0';
		rec++;
	}
	len = rec-orec;

	if (TXtracedumptable & 0x2)
	{
		putmsg(TXTRACEDUMPTABLE_MSG_BEFORE,
			Fn, "Sending %wd (0x%wx) byte buffer",
			(EPI_HUGEINT)len, (EPI_HUGEINT)len);
		tx_hexdumpmsg(TXPMBUFPN, TXTRACEDUMPTABLE_MSG_BEFORE, CHARPN,
			orec, len, 1);
	}

	if(len != needed)
	{
		putmsg(MERR, Fn, "Internal logic error (Len %d != Needed %d)", len, needed);
		goto err;
	}
        goto done;

err:
        orec = NULL;
done:
        closefld(fconv);
	return orec;
}

/******************************************************************/
/* Handle the most common conversions from text.  Save fldmath */

static byte *fromtext ARGS((char *buf, size_t len, int ftype, size_t *pna));

static byte *
fromtext(buf, len, ftype, pna)
char *buf;
size_t len;
int ftype;
size_t	*pna;	/* (out) byte size alloced */
{
	static CONST char	fn[] = "fromtext";
	void *rc = NULL;

	(void)len;

	switch(ftype)
	{
	case FTN_COUNTER:
		rc = TXmalloc(TXPMBUFPN, fn, *pna = sizeof(ft_counter) + 1);
		if(rc)
		{
			ft_counter *res = rc;
			sscanf(buf, "%08lx%lx", &res->date, &res->seq);
		}
		break;
	case FTN_INT:
	case FTN_INTEGER:
		rc = TXmalloc(TXPMBUFPN, fn, *pna = sizeof(ft_int) + 1);
		if(rc)
		{
			ft_int *res = rc;
			*res = atol(buf);
		}
		break;
	case FTN_DATE:
		rc = TXmalloc(TXPMBUFPN, fn, *pna = sizeof(ft_date) + 1);
		if(rc)
		{
			ft_date *res = rc;
			*res = atol(buf);
		}
		break;
	case FTN_LONG:
		rc = TXmalloc(TXPMBUFPN, fn, *pna = sizeof(ft_long) + 1);
		if(rc)
		{
			ft_long *res = rc;
			*res = atol(buf);
		}
		break;
	default: return(NULL);
	}
	if (rc) ((char *)rc)[*pna - 1] = '\0';
	return rc;
}

/******************************************************************/
/* No alignment is assumed here, as it is supposed to be portable,
   and we can't predict future alignment.  Also everything is either
   char or byte, so maybe we can just point at it */

static int portbuftofld ARGS((byte *, TBL *, byte *, FLDOP *));

static int
portbuftofld(rec, tb, end, fo)
byte *rec;
TBL *tb;
byte *end;
FLDOP *fo;
/* Returns 0 if ok, -1 on error.
 */
{
	static CONST char Fn[] = "portbuftofld";
	unsigned long blen, clen;
	int ftype, ret;
	unsigned int	i;
	size_t len, na = 0;
	byte *crec, *x;
	FLD *fconv = FLDPN;

	if (TXtracedumptable & 0x20000)
	{
		putmsg(TXTRACEDUMPTABLE_MSG_BEFORE,
			Fn, "Receiving %wd (0x%wx) byte buffer",
			(EPI_HUGEINT)(end - rec),
			(EPI_HUGEINT)(end - rec));
		tx_hexdumpmsg(TXPMBUFPN, TXTRACEDUMPTABLE_MSG_BEFORE, CHARPN,
			rec, (size_t)(end - rec), 1);
	}

	if(!fconv)
		fconv = createfld("varchar", 20, 0);
	crec = rec;
	clen = end - rec;
	crec = ivsl(crec, &blen);		/* total len including VSL */
	if (blen != clen)
	{
		/* Should not happen, as caller should do this first */
		putmsg(MERR, Fn, "Buffer size problem, wanted %wd, got %wd",
		       (EPI_HUGEINT)blen, (EPI_HUGEINT)clen);
		goto err;
	}
	for(i=0;i<tb->n;i++)
	{
		FLD *f=tb->field[i];
		ftype = f->type & DDTYPEBITS;
		if (crec >= end)
		{
		trunc:
			putmsg(MERR, Fn, "Truncated data");
			goto err;
		}
		switch(ftype)
		{
		case FTN_CHAR:
		case FTN_BYTE:
		case FTN_INDIRECT:
			crec = ivsl(crec, &blen);
			if (crec >= end) goto trunc;
			if ((long)blen <= 0L)
			{
			badlen:
				putmsg(MERR, Fn,
					"Bad field size %wd for field `%s'",
				       (EPI_HUGEINT)blen,
				       ddgetname(tb->dd, i));
				goto err;
			}
			if (crec + blen > end) goto trunc;
			TXfreefldshadow(f);
			setfldandsize(f, crec, blen, FLD_FORCE_NORMAL);
			TXsetshadownonalloc(f);
			crec+=f->size;		/* skip to end of data */
			break;
		default:
			crec = ivsl(crec, &blen);
			if (crec >= end) goto trunc;
			if ((long)blen < 0L) goto badlen;  /* WTF <= 0L? */
			if (crec + blen > end) goto trunc;
			if((x=fromtext((char*)crec, blen, f->type,&na))==NULL)
			{
			  TXfreefldshadow(fconv);/* _before_ type change */
			  if(ftype == FTN_BLOBI)
				fconv->type = FTN_BYTE + DDVARBIT;
			  else
				fconv->type = FTN_CHAR + DDVARBIT;
			  /* KNG 20060628 failure to change elsz caused
			   * setfldandsize() to set wrong n:
			   */
			  fconv->elsz = sizeof(ft_char);
			  setfldandsize(fconv, crec, blen+1, FLD_FORCE_NORMAL);
			  TXsetshadownonalloc(fconv);
			  fopush(fo, fconv);
			  fopush(fo, tb->field[i]);
			  if(foop(fo, FOP_CNV) == -1)
			  {
				putmsg(MERR, Fn,
	"Could not convert type %s data to field %s type %s (%d to %d)",
					ddfttypename(fconv->type),
					ddgetname(tb->dd, i),
					TXfldtypestr(f),
					fconv->type,
					f->type);
				goto err;
			  }
			  closefld(fconv);
			  fconv = fopop(fo);
			  /* KNG 20070410 +1, and nul-terminate: */
			  x = TXmalloc(TXPMBUFPN, Fn, na = fconv->size + 1);
			  if (!x) goto err;
			  memcpy(x, getfld(fconv, &len), fconv->size);
			  ((char *)x)[fconv->size] = '\0';
			  /* KNG 20070627 `f' is about to own `x' (at
			   * setfldandsize(f, x, ...) below).  If FTN_BLOBI,
			   * `f' will own `x's ft_blobi.off malloc too.
			   * So make sure that the next TXfreefldshadow(fconv)
			   * (above or via closefld) does not free `f's
			   * ft_blobi.off, by freeing shadow-not-blob here:
			   */
			  if (ftype == FTN_BLOBI)
				TXfreefldshadownotblob(fconv);
			}
			else len=1;
			TXfreefldshadow(f);
			/* KNG 20070410 set size too, e.g. multi-val ints: */
			setfldandsize(f, x, na, FLD_FORCE_NORMAL);
			if(ftype == FTN_BLOBI)
				_fldcopy(f, tb, f->storage, tb, fo);
			crec += blen;
		}
		crec++;				/* skip nul-terminator */
	}
	/* WTF `crec' should be equal to `end' here? */

	if (TXtracedumptable & 0x10000)
	{
		char	*s, *d, buf[8192];
#define EOB	(buf + sizeof(buf))

		d = buf;
		for (i = 0; i < tb->n && d < EOB-1; i++)
		{
			s = fldtostr(tb->field[i]);
			if (TXtracedumptable & 0x40000)
			{
				for ( ; *s && d < EOB-1; s++, d++)
					*d = (*s>=' ' && *s<='~' ? *s : '.');
			}
			else
			{
				for ( ; *s && d < EOB-1; s++, d++)
					*d = *s;
			}
			if (d < EOB-1 && i + 1 < tb->n) *(d++) = '|';
		}
		*d = '\0';
		putmsg(TXTRACEDUMPTABLE_MSG_BEFORE, Fn,
			"Receiving row: %s%s",
			buf, (d < EOB ? "" : "..."));
#undef EOB
	}
        ret = 0;                                /* success */
        goto done;

err:
        ret = -1;                               /* error */
done:
        closefld(fconv);
	return ret;
}

/******************************************************************/

int
TXdumptable(ddic, localTableName, remoteTableName, fo, rowlimit, fd
#ifdef _WIN32
, handle, usewrite, metertype, rowsSent)
HANDLE handle;
#else
, usewrite, metertype, rowsSent)
#endif
DDIC *ddic;	/* Data dictionary */
CONST char *localTableName;	/* (in) Name of local table to send */
CONST char *remoteTableName;	/* (in, opt.) Name of remote table */
FLDOP *fo;	/* FLDOP fer calculating */
EPI_OFF_T rowlimit;	/* How many rows to send, -1 for all */
int fd;		/* Which file to write the stuff to */
int usewrite;	/* Nonzero: use write() instead of ezswrite() */
TXMDT metertype;	/* Meter type to display (TXMDT_NONE for none) */
EPI_OFF_T *rowsSent;    /* (out, opt.) number of rows sent */
/* If NULL, `remoteTableName' defaults to `localTableName'.
 * Returns 0 if ok, -1 on error.
 */
{
	static CONST char	fn[] = "TXdumptable";
	size_t sz;
	byte *v;
	unsigned long l;
	DBTBL *dbtbl = NULL;
	EPI_OFF_T rowsDone = (EPI_OFF_T)0, ret;
	char *x;
	METER	*meter = METERPN;
	EPI_STAT_S	st;
	RECID	*recidp;
	char	desc[128];

	if(!localTableName)
		goto err;
	if (remoteTableName == CHARPN)
		remoteTableName = localTableName;
	dbtbl = opendbtbl(ddic, (char *)localTableName);
	if(dbtbl)
	{
		/* Get raw meter limit: */
#ifdef _WIN32
		if (EPI_STAT(getdbffn(dbtbl->tbl->df), &st) != 0)
#else /* !_WIN32 */
		if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) != 0)
#endif /* !_WIN32 */
		{				/* stat() failure */
			st.st_size = (EPI_OFF_T)0;
			metertype = TXMDT_NONE;
		}
		else if ((void *)dbtbl->tbl->df->get == (void *)kdbf_get)
			st.st_size -= 2*sizeof(EPI_OFF_T);

		/* Open meter: */
		if (metertype != TXMDT_NONE)
		{
			htsnpf(desc, sizeof(desc), "Sending local table %s:",
			       localTableName);
			meter = openmeter(desc, metertype, MDOUTFUNCPN,
				MDFLUSHFUNCPN, NULL, (EPI_HUGEINT)st.st_size);
		}

		x = TXmalloc(TXPMBUFPN, fn, strlen(ddic->epname) + strlen("\n")+1);
		if(!x)
			goto err;
		strcpy(x, ddic->epname);
		strcat(x, "\n");
		writeall(fd, handle, x, strlen(x), usewrite);
		x = TXfree(x);

		x = TXmalloc(TXPMBUFPN, fn, strlen(remoteTableName)+strlen("\n")+1);
		if(!x)
			goto err;
		strcpy(x, remoteTableName);
		strcat(x, "\n");
		writeall(fd, handle, x, strlen(x), usewrite);
		x = TXfree(x);
		x = TXddtotext(dbtbl->tbl->dd);
		if(!x)
			goto err;
		writeall(fd, handle, x, strlen(x), usewrite);
		while((recidp = getdbtblrow(dbtbl), TXrecidvalid(recidp)) &&
		      (rowlimit < (EPI_OFF_T)0 || rowsDone < rowlimit))
		{
			/* Update meter before doing current recid,
			 * because now we are done up to its offset:
			 */
			if (meter)
				METER_UPDATEDONE(meter,
					(EPI_HUGEINT)TXgetoff2(recidp));
			v = portfldtobuf(dbtbl->tbl, fo);
			if(v)
			{
				ivsl(v, &l);
				sz = l;
				writeall(fd, handle, (char *)v, sz, usewrite);
				rowsDone++;
				v = TXfree(v);
			}
		}
		v = NULL;
		writeall(fd, handle, (char *)&v, 1, usewrite);
		/* Update meter for final row, unless there's a row limit,
		 * in which case meter_end() below will fill it out w/dashes:
		 */
		if (meter &&
		    rowlimit < (EPI_OFF_T)0 &&
		    (void *)dbtbl->tbl->df->get == (void *)kdbf_get &&
		    ((KDBF *)dbtbl->tbl->df->obj)->lasterr == 0)
			meter_updatedone(meter, (EPI_HUGEINT)st.st_size);
		ret = 0;			/* success */
	}
	else					/* table open failed */
		goto err;
	goto done;

err:
	ret = -1;
done:
	if (meter)
	{
		meter_end(meter);
		closemeter(meter);
	}
	if (dbtbl) closedbtbl(dbtbl);
	if (rowsSent) *rowsSent = rowsDone;
	return(ret);
}

/******************************************************************/

typedef struct LOADTABLE {
	int fd, nfields, tbltype;
	unsigned long datasz;
	char *dbname;
	char *remoteTableName;
	byte *v, *cp;
	size_t bufsz;
	DD *dd;
	DBTBL *dbtbl;
#ifdef _WIN32
	HANDLE handle;
#endif
} LOADTABLE;

/******************************************************************/

static LOADTABLE *closeloadtable ARGS((LOADTABLE *));

static LOADTABLE *
closeloadtable(lt)
LOADTABLE *lt;
{
	if(lt)
	{
		lt->v = TXfree(lt->v);
		lt->dd = closedd(lt->dd);
		lt->dbtbl = closedbtbl(lt->dbtbl);
		lt->dbname = TXfree(lt->dbname);
		lt->remoteTableName = TXfree(lt->remoteTableName);
		lt = TXfree(lt);
	}
	return NULL;
}

/******************************************************************/

static size_t readmore ARGS((LOADTABLE *lt));

static size_t
readmore(lt)
LOADTABLE *lt;
{
	size_t x;
#ifdef _WIN32
	DWORD nread;
#endif

	if (lt->datasz >= lt->bufsz)
	  {
	    putmsg(MERR + MAE, "readmore", "Buffer full");
	    return((size_t)0);
	  }
#ifdef _WIN32

	if(lt->fd == -1 && lt->handle != INVALID_HANDLE_VALUE)
	{
		ReadFile(lt->handle, lt->v+lt->datasz,
			(lt->bufsz - lt->datasz), &nread, NULL);
		return nread;
	}
#endif
	if (TXezWaitForSocketReadability(TXPMBUFPN, HtTraceSkt, __FUNCTION__,
					 lt->fd, 10.0) < 1)
		x = (size_t)(-1);
	else
		x = TXezSocketRead(TXPMBUFPN, HtTraceSkt, __FUNCTION__,
				   lt->fd, NULL, lt->v+lt->datasz,
				   (lt->bufsz - lt->datasz), TXbool_False,
				   NULL, TXezSR_IgnoreNoErrs);
	/* NULL-terminate for safety of some string functions later: */
	if ((int)x >= 0 && x != (size_t)(-1))
	{
		lt->v[lt->datasz + x] = '\0';
		if (x > (size_t)0 && (TXtracedumptable & 0x80000))
		{
			putmsg(TXTRACEDUMPTABLE_MSG_AFTER, __FUNCTION__,
				"Received %wd (0x%wx) bytes raw data",
				(EPI_HUGEINT)x, (EPI_HUGEINT)x);
			tx_hexdumpmsg(TXPMBUFPN, TXTRACEDUMPTABLE_MSG_AFTER,
					CHARPN, lt->v + lt->datasz, x, 1);
		}
	}
	return x;
}
/******************************************************************/

static LOADTABLE *initloadtable ARGS((int fd));
static LOADTABLE *
initloadtable(fd)
int	fd;
/* Reads initial db/table/schema data from over-the-wire socket `fd'.
 */
{
	static CONST char Fn[] = "initloadtable";
	static CONST char	newline[] = "\n", space[] = " ";
	int fn, nonnull, i;
	char *xread, *fname, *ftype, *e;
	size_t x, xro;
	LOADTABLE *rc;

	rc = (LOADTABLE *)TXcalloc(TXPMBUFPN, Fn, 1, sizeof(LOADTABLE));
	if(!rc)
		return rc;

	rc->bufsz = 60000;
	rc->datasz = 0;
	/* Add 1 for nul-termination in readmore(), and VSL_MAXSZ for
	 * ivsl() in loadtable:
	 */
	rc->v = TXmalloc(TXPMBUFPN, Fn, rc->bufsz + VSL_MAXSZ + 1);
	if(!rc->v)
		return closeloadtable(rc);
	rc->fd = fd;

	if ((rc->datasz = readmore(rc)) == 0)
          {
            /* JMT sez sometimes a return of 0 occurs on a valid descriptor.
	     * (this doesn't happen under Windows?).  However, it's often
             * dead instead, but we get no error and select() says it's
             * readable.  Sleep and retry only a few times, then stop:
	     * KNG 000519
             * KNG 2011-06-20 Bug 3807 sleep is causing monitor timeouts,
             * and should not be needed in a network protocol.  Remove sleeps
             * and fix the underlying issue if/when it crops up again.
	     */
            return closeloadtable(rc);
	  }
	;
	if((int)rc->datasz < 0 || rc->datasz == (unsigned long)((size_t)(-1)))
		return closeloadtable(rc);

	while(!memchr(rc->v, '\n', rc->datasz))	/* read database path */
	{
		x = readmore(rc);
		if ((int)x <= 0 || x == (size_t)(-1))
		{
			putmsg(MERR + FRE, Fn, "Read failed: %s, %d, %d at %d",
				strerror(errno), rc->datasz, rc->bufsz, __LINE__);
			return closeloadtable(rc);
		}
		rc->datasz += x;
	}

	xread = (char *)rc->v;			/* parse database path */
	xro = strcspn(xread, newline);
	if(xro == 0 || xread[xro] == '\0')
		return closeloadtable(rc);
	xread[xro++] = '\0';
	rc->dbname = TXstrdup(TXPMBUFPN, Fn, xread);
	if (!rc->dbname) return(closeloadtable(rc));
	{					/* strip trailing slash(es) */
		char	*d;

		for (d = rc->dbname + strlen(rc->dbname) - 1;
		     d > rc->dbname && TX_ISPATHSEP(*d);
		     d--)
			*d = '\0';
	}
	while(xro >= rc->datasz ||		/* read table name */
	      !memchr(rc->v + xro, '\n', rc->datasz - xro))
	{
		x = readmore(rc);
		if ((int)x <= 0 || x == (size_t)(-1))
		{
			putmsg(MERR + FRE, Fn, "Read failed: %s, %d, %d at %d",
				strerror(errno), rc->datasz, rc->bufsz, __LINE__);
			return closeloadtable(rc);
		}
		rc->datasz += x;
	}

	xread = (char *)rc->v + xro;		/* parse table name */
	x = strcspn(xread, newline);
	if(x == 0 || xread[x] == '\0')
		return closeloadtable(rc);
	xread[x] = '\0';
	xro += x + 1;
	rc->remoteTableName = TXstrdup(TXPMBUFPN, Fn, xread);

	while(xro >= rc->datasz ||		/* read table type */
	      !memchr(rc->v + xro, '\n', rc->datasz - xro))
	{
		x = readmore(rc);
		if ((int)x <= 0 || x == (size_t)(-1))
		{
			putmsg(MERR + FRE, Fn, "Read failed: %s, %d, %d at %d",
				strerror(errno), rc->datasz, rc->bufsz, __LINE__);
			return closeloadtable(rc);
		}
		rc->datasz += x;
	}

	xread = (char *)rc->v + xro;		/* parse table type */
	x = strcspn(xread, newline);
	if(x == 0 || xread[x] == '\0')
		return closeloadtable(rc);
	xread[x] = '\0';
	xro += x + 1;
	rc->tbltype = strtol(xread, &e, 10);
	e += strspn(e, space);
	if (*e != '\0')
	{
		putmsg(MERR + UGE, Fn,
		   "Garbled table type `%s' while processing remote table %s",
		       xread, rc->remoteTableName);
		return closeloadtable(rc);
	}

	while(xro >= rc->datasz ||		/* read nfields */
	      !memchr(rc->v + xro, '\n', rc->datasz - xro))
	{
		x = readmore(rc);
		if ((int)x <= 0 || x == (size_t)(-1))
		{
			putmsg(MERR + FRE, Fn, "Read failed: %s, %d, %d at %d",
				strerror(errno), rc->datasz, rc->bufsz, __LINE__);
			return closeloadtable(rc);
		}
		rc->datasz += x;
	}

	xread = (char *)rc->v + xro;		/* parse nfields */
	x = strcspn(xread, newline);
	if(x == 0 || xread[x] == '\0')
		return closeloadtable(rc);
	xread[x] = '\0';
	xro += x + 1;
	rc->nfields = strtol(xread, &e, 10);
	e += strspn(e, space);
	if (rc->nfields < 0 || *e != '\0')
	{
		putmsg(MERR + UGE, Fn,
		       "Garbled nfields `%s' for remote table %s",
		       xread, rc->remoteTableName);
		return closeloadtable(rc);
	}
	if ((rc->dd = opennewdd(rc->nfields)) == NULL)
	{
		putmsg(MERR, Fn, "openewdd() failed");
		return closeloadtable(rc);
	}
	(void)ddsettype(rc->dd, TEXIS_FAST_TABLE);

	for(i = 0; i < rc->nfields; i++)	/* read fname type sz nnull */
	{
		while(xro >= rc->datasz ||
		      !memchr(rc->v + xro, '\n', rc->datasz - xro))
		{
			x = readmore(rc);
			if ((int)x <= 0 || x == (size_t)(-1))
			{
				putmsg(MERR + FRE, Fn, "Read failed: %s, %d, %d at %d",
					strerror(errno), rc->datasz, rc->bufsz, __LINE__);
				return closeloadtable(rc);
			}
			rc->datasz += x;
		}
		xread = (char *)rc->v + xro;	/* terminate line */
		x = strcspn(xread, newline);
		if(x == 0 || xread[x] == '\0')
			return closeloadtable(rc);
		xread[x] = '\0';
		xro += x + 1;

		xread += strspn(xread, space);	/* parse field name */
		fname = xread;
		xread += strcspn(xread, space);
		if (xread == fname)
		{
			putmsg(MERR, Fn, "Missing field name in data");
			return closeloadtable(rc);
		}
		if (*xread == '\0')		/* parse field type */
		{
		notype:
			putmsg(MERR, Fn, "Missing field `%s' type in data",
				fname);
			return closeloadtable(rc);
		}
		*(xread++) = '\0';
		xread += strspn(xread, space);
		ftype = xread;
		xread += strcspn(xread, space);
		if (xread == ftype) goto notype;
		if (*xread == '\0')		/* parse field len */
		{
			putmsg(MERR, Fn, "Missing field `%s' len in data",
				fname);
			return closeloadtable(rc);
		}
		*(xread++) = '\0';
		xread += strspn(xread, space);
		fn = strtol(xread, &e, 10);
		if (e <= xread)
		{
			putmsg(MERR, Fn,"Garbled field `%s' len `%s' in data",
				fname, xread);
			return closeloadtable(rc);
		}
		xread += strcspn(xread, space);
		if (*xread == '\0')		/* parse field non-null */
		{
			putmsg(MERR, Fn,
				"Missing field `%s' non-null flag in data",
				fname);
			return closeloadtable(rc);
		}
		xread += strspn(xread, space);
		nonnull = strtol(xread, &e, 10);
		if (e <= xread)
		{
			putmsg(MERR, Fn,
			    "Garbled field `%s' non-null flag `%s' in data",
				fname, xread);
			return closeloadtable(rc);
		}
		putdd(rc->dd, fname, ftype, fn, nonnull);
	}
	if ((size_t)rc->datasz > xro)		/* delete used data */
	{
		rc->datasz -= xro;
		memmove(rc->v, rc->v + xro, rc->datasz);
	}
	else
	{
		if ((size_t)rc->datasz < xro)
			putmsg(MWARN, Fn, "Unexpected datasz %wu < used %wu",
			       (EPI_HUGEINT)rc->datasz, (EPI_HUGEINT)xro);
		rc->datasz = 0L;
	}
	rc->cp = rc->v;
	return rc;
}

/******************************************************************/

static int loadtable ARGS((LOADTABLE *lt, FLDOP *fo));
static int
loadtable(lt, fo)
LOADTABLE *lt;
FLDOP *fo;
/* Reads over-the-wire table row data into `lt'.
 */
{
	static CONST char	Fn[] = "loadtable";
	byte *nv, *ep = BYTEPN;
	size_t x, nbufsz;
	ulong blen = 0;

nextrec:
	do
	{
		/* If we have some data, find out how much data to expect */
		if(lt->datasz > 0)
		{
		  /* we may walk past lt->datasz here, which is garbage,
		   * but safe to read since we alloced for it:  KNG 010404
		   */
			ep = ivsl(lt->cp, &blen);
		}

		/* If we don't have any data, or the length was more than
		   our data, read some more data */
		if((lt->datasz == 0) || (ep > lt->cp + lt->datasz))
		{
			blen = 0;
			x = readmore(lt);
			if ((int)x <= 0 || x == (size_t)(-1))
			{
				putmsg(MERR + FRE, Fn, "Read failed: %s, %d, %d at %d",
				      strerror(errno), lt->datasz, lt->bufsz, __LINE__);
				return -1;
			}
			lt->datasz += x;
		}
		else if(blen ==0) /* No more data */
			return 0;
	} while((lt->datasz == 0) || (ep > lt->cp + lt->datasz) || (blen == 0));
	if(blen <= lt->datasz)
	{
		portbuftofld(lt->cp, lt->dbtbl->tbl, lt->cp+blen, fo);
		putdbtblrow(lt->dbtbl, NULL);
	}
	else
	{
		if(blen < lt->bufsz)
		{
			memmove(lt->v, lt->cp, lt->datasz);
			lt->cp = lt->v;
			while(blen > lt->datasz)
			{
				x = readmore(lt);
				if ((int)x <= 0 || x == (size_t)(-1))
				{
					putmsg(MERR + FRE, Fn, "Read failed: %s, %d, %d at %d", strerror(errno), lt->datasz, lt->bufsz, __LINE__);
					return -1;
				}
				lt->datasz += x;
			}
		}
		else
		{
			/* grow by 50%, plus more if needed: */
			nbufsz = (lt->bufsz + (lt->bufsz >> 1));
			if (nbufsz < (size_t)blen) nbufsz = (size_t)blen;
			/* round up to 64K: */
			nbufsz = ((nbufsz + ((size_t)1 << 16) - 1) &
					~(((size_t)1 << 16) - 1));
			/* Add 1 for nul-termination in readmore(), and
			 * VSL_MAXSZ for ivsl() in loadtable:
			 */
			nv = TXmalloc(TXPMBUFPN, Fn, nbufsz + VSL_MAXSZ + 1);
			if (!nv)
				return -1;
			memmove(nv, lt->cp, lt->datasz);
			lt->v = TXfree(lt->v); /* Free the old one */
			lt->v = nv;
			lt->cp = lt->v;
			lt->bufsz = nbufsz;
			while(blen > lt->datasz)
			{
				x = readmore(lt);
				if ((int)x <= 0 || x == (size_t)(-1))
				{
					putmsg(MERR + FRE, Fn, "Read failed: %s, %d, %d at %d", strerror(errno), lt->datasz, lt->bufsz, __LINE__);
					return -1;
				}
				lt->datasz += x;
			}
		}
		goto nextrec;
	}
	lt->cp += blen;
	lt->datasz -= blen;
	while(lt->datasz < 4)
	{
		memmove(lt->v, lt->cp, lt->datasz);
		lt->cp = lt->v;
		while(4 > lt->datasz)
		{
			if(lt->datasz == 1 && *lt->v == 0)
			{
#ifndef TX_DEBUG
				/* WTF KNG why is this wait here?
				 * reduced to 1 second, as it seems
				 * superfluous
				 */
				if (TXezWaitForSocketReadability(TXPMBUFPN,
								 HtTraceSkt,
								 __FUNCTION__,
					lt->fd, 1.0) < 1) /* Wait for true end */
#endif
					return 0;
			}
			x = readmore(lt);
			if ((int)x < 0 || x == (size_t)(-1))
			{
				putmsg(MERR + FRE, Fn, "Read failed: %s, %d, %d at %d", strerror(errno), lt->datasz, lt->bufsz, __LINE__);
				return -1;
			}
			if (x == 0)
				return 0;
			lt->datasz += x;
		}
	}
	goto nextrec;
}

/******************************************************************/

DBTBL *
TXgettable(fd, db
#ifdef _WIN32
, handle)
HANDLE handle;
#else
)
#endif
int fd;
char **db;      /* (out, opt.) database for table */
/* Reads an over-the-wire table from `fd' and returns it, along with
 * its database path.
 */
{
	static CONST char	fn[] = "TXgettable";
	LOADTABLE *lt;
	FLDOP *fo;
	DBTBL *dbtbl;
	int rc;

	lt = initloadtable(fd);
	if(!lt)
		return NULL;
	lt->dbtbl = (DBTBL *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(DBTBL));
	if(!lt->dbtbl)
	{
		lt = closeloadtable(lt);
		return NULL;
	}
	lt->dbtbl->tbl = createtbl(lt->dd, NULL);
	lt->dbtbl->type = 'T';
	fo = dbgetfo();
	rc = loadtable(lt, fo);
	foclose(fo);
	dbtbl = lt->dbtbl;
	lt->dbtbl = NULL;
	if(db)
	{
		*db = lt->dbname;
		lt->dbname = NULL;
	}
	lt = closeloadtable(lt);
	return dbtbl;

}

/******************************************************************/

int
TXloadtable(ddic, fo, fd, localTableName)
DDIC *ddic;
FLDOP *fo;
int fd;
CONST char	*localTableName;	/* (in, opt.) local name of table */
/* Saves table `localTableName' locally from over-the-wire data read.
 * If NULL, `localTableName' defaults to the over-the-wire (i.e. remote)
 * table name.
 */
{
	LOADTABLE *lt;
	int rc;

	lt = initloadtable(fd);
	if(!lt)
		return -1;
	if (localTableName == CHARPN) localTableName = lt->remoteTableName;
	lt->dbtbl = createdbtbl(ddic, lt->dd, (char *)localTableName,
				(char *)localTableName, "", 'T');
	if(!lt->dbtbl)
	{
		putmsg(MERR + FME, CHARPN,
			"Could not create local table %s in database %s",
			localTableName, ddic->epname);
		return -1;
	}
	rc = loadtable(lt, fo);
	closeloadtable(lt);
	return rc;
}

/******************************************************************/

#ifdef TEST

int
main(argc, argv)
int argc;
char *argv[];
{
	size_t sz;
	unsigned long l;
	byte *v;
	TBL *tb, *otb;
	DDIC *ddic;
	FLDOP *fo;

	fo = dbgetfo();
#ifdef _WIN32
	ddic = ddopen("d:/tmp/junk");
#else
	ddic = ddopen("/usr/tmp/junk");
#endif
	if(argc == 1)
		loadtable(ddic, fo);
	else
		dumptable(ddic, argv[1], fo);
	return 0;


	tb=opentbl("/usr/tmp/junk/SYSCOLUMNS");
	otb=opentbl("/usr/tmp/junk/bob");
	if(tb && otb)
	{
		while(gettblrow(tb, NULL))
		{
			v = portfldtobuf(tb, fo);
			if(v)
			{
				ivsl(v, &l);
				sz = l;
#ifdef NEVER
				writeall(1, handle, v, sz);
#endif
				portbuftofld(v, otb, v+sz, fo);
				puttblrow(otb, NULL);
			}
		}
	}
	return 0;
}

#endif /* TEST */
