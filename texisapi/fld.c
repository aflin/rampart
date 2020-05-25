/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include "stdio.h"
#include <string.h>
#include <errno.h>
#include "stdlib.h"
#ifdef unix
#  include <unistd.h>
#endif
#ifdef _WIN3
#  include <io.h>                               /* for access() */
#endif
#include "sys/types.h"
#include <limits.h>
#include <sys/stat.h>
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"				/* for HTBUF */
#ifdef JOHN
#include <fcntl.h>
#endif


static const char       TXfldNullOutputStringDefault[] = "NULL";
static char     *TXfldNullOutputString = (char *)TXfldNullOutputStringDefault;

char *
TXfldGetNullOutputString(void)
{
  return(TXfldNullOutputString);
}

int
TXfldSetNullOutputString(const char *s)
/* Returns 0 on error.
 */
{
  static const char     fn[] = "TXfldSetNullOutputString";

  if (TXfldNullOutputString != TXfldNullOutputStringDefault)
    TXfldNullOutputString = TXfree(TXfldNullOutputString);
  TXfldNullOutputString = TXstrdup(TXPMBUFPN, fn, s);
  return(TXfldNullOutputString ? 1 : 0);
}

#ifdef NEVER
#define shadow shadw
#endif
/************************************************************************/
/*
 *	createfld()
 *
 *	Allows for the creation of fields of a named type.
 *	KNG 20060220 added "internal:subtype" support
 *
 */

FLD *
createfld(type,n,nonull)
char *type;                                    /* type of this variable */
int   n;                /* max or n of elements depending on var prefix */
int   nonull;                               /* is this a non null field */
{
	static CONST char	fn[] = "createfld";
	DDFD  fd;
	FLD		*fld = FLDPN;
	CONST char	*s;
	char		*e;
	FTI		subtype;
	ft_internal	*fti = ft_internalPN, *ftilast=ft_internalPN, *ftinew;
	int		i;

	if(0 == getddfd(type, n, nonull, "", &fd))
	{
		fld = openfld(&fd);
		/* If it's ft_internal, parse and set subtype: */
		if (fld != FLDPN &&
		    (fld->type & DDTYPEBITS) == FTN_INTERNAL &&
		    (s = strchr(type, ':')) != CHARPN)
		{
			s++;
			subtype = (FTI)strtol(s, &e, 10);
			if (e == s || *e != '\0')
				subtype = tx_fti_str2type(s);
			if (subtype == FTI_UNKNOWN)
				putmsg(MWARN + UGE, fn,
				"Unknown FTN_INTERNAL subtype `%s'", s);
			else
			{	/* Open with no data; just hold subtype: */
				for (i = 0; i < n; i++, ftilast = ftinew)
				{
					ftinew = tx_fti_open(subtype, CHARPN,
								0);
					if (ftinew == ft_internalPN) break;
					if (i > 0)
					{
						if (!tx_fti_append(ftilast,
								   ftinew))
							break;
					}
					else
						fti = ftinew;
				}
				setfldandsize(fld, fti,
					FT_INTERNAL_ALLOC_SZ + 1, FLD_FORCE_NORMAL);
			}
		}
	}
	return(fld);
}

/******************************************************************/

void *
TXfreefldshadow(f)
FLD *f;
{
#ifdef NEVER /* TX_DEBUG */
	if((f->shadow!=(void *)NULL) && (f->frees == FREESHADOW))
#else
	if(f->shadow!=(void *)NULL)
#endif
	{
		/* KNG 20120120 was freeing ft_blobi.memdata here,
		 * regardless of `f->frees == FREESHADOW' or not.
		 * why?  let TXftnFreeData() do it, only for FREESHADOW?:
		 */
		if(f->frees == FREESHADOW)
		{
			if (f->v == f->shadow)
				f->v = NULL;
			TXftnFreeData(f->shadow, f->n, f->type, 1);
			f->shadow = NULL;
			f->frees = 0;
		}
	}
	return NULL;
}

/* ------------------------------------------------------------------------ */

void
TXfreefldshadownotblob(f)
FLD *f;
/* Frees shadow of `f' if `f' owns it, else leaves `f' unchanged.
 * Does not free blob data like TXfreefldshadow(), but does clear `f->alloced'.
 */
{
	if (f->shadow != NULL)
	{
		/* free blob info here */
		if (f->frees == FREESHADOW)
		{
			if (f->v == f->shadow)
				f->v = NULL;
			TXftnFreeData(f->shadow, f->n, f->type, 0);
			f->shadow = NULL;
			f->frees = 0;
			f->alloced = 0;
		}
	}
}

/************************************************************************/
                 /* free's memory from a field struct */

void
releasefld(f)
FLD     *f;
/* Does all the work of closefld() except freeing the FLD struct itself.
 * Used by code that alloc and free the same field a lot with different types.
 */
{
  if (f == FLDPN) return;

  TXfreefldshadow(f);

/*
 * We will now clear the pointers in the field just in case you try
 * to use it again
 */

  f->v = f->shadow = (void *)NULL;
  f->type = 0;
  if (f->fldlist != FLDPPN)
  {
    if((f->kind == TX_FLD_COMPUTED_JSON) && (f->vfc >= 2))
    {
      f->fldlist[1] = closefld(f->fldlist[1]);
    }
    f->fldlist = TXfree(f->fldlist);
  }
#ifndef NO_HAVE_DDBF
  if (f->storage != FLDPN) f->storage = closefld(f->storage);
  f->memory = NULL;                             /* wtf? KNG 981111 */
#endif
  if (f->dsc.ptrsalloced > 0 && f->dsc.allocedby == f)
  {
    f->dsc.dptrs.strings = TXfree(f->dsc.dptrs.strings);
    f->dsc.ptrsalloced = 0;
    f->dsc.ptrsused = 0;
    f->dsc.allocedby = NULL;
  }
}


FLD *
closefld(f)
FLD	*f;
{
  if (f != FLDPN)
    {
      releasefld(f);
      f = TXfree(f);
    }
  return(FLDPN);
}


/************************************************************************/

static int initfldfd ARGS((FLD *f, DDFD *fd, int noshadow));
static int
initfldfd(f, fd, noshadow)
FLD     *f;
DDFD    *fd;
int     noshadow;
/* Does initialization of `f' using `fd'.  Returns 0 on error.
 */
{
  static CONST char     fn[] = "initfldfd";

  memset(f, 0, sizeof(*f));

  f->type = fd->type;
  f->v = (void *)NULL;

  f->size = fd->size;
  if ((f->elsz = fd->elsz) <= (size_t)0)        /* sanity check */
    {
      putmsg(MERR, fn, "Invalid elsz %ld for field", (long)fd->elsz);
      return(0);
    }
  f->n = f->size/f->elsz;               /* this is needed for fixed stuff */
  if (noshadow ||                       	/* KNG 981112 */
      (f->type & DDTYPEBITS) == FTN_INTERNAL)
    {
      f->alloced = 0;
      f->shadow = NULL;
    }
  else
    {            /* wtf KNG 000427 proper strlst alloc? other places too... */
      switch (f->type & DDTYPEBITS)
        {
        case FTN_STRLST:
          f->alloced = sizeof(ft_strlst) + 1;
          break;
        case FTN_BLOBI:
          /* Bug 4522: was under-allocing varblobi; size was old ft_blobi
           * size (before len/otype/memdata/ndfree added), from a varblobi
           * field erroneously (Bug 4390) stored in a KDBF schema instead
           * of blob.  (Normally varblobi appears in schema via TXbddc(),
           * which sets current/proper size):
           */
          f->alloced = sizeof(ft_blobi) + 1;
          break;
        default:
          f->alloced = f->size + 1;       /* +1 for '\0' terminator */
          break;
        }
      if ((f->shadow = TXcalloc(TXPMBUFPN, fn, 1, f->alloced)) == (void *)NULL)
        return(0);
      else
        f->frees = FREESHADOW;
    }
  return(1);
}

        /* creates a new field and allocates memory to hold it */

FLD *
openfld(fd)
DDFD *fd;
{
  static CONST char     fn[] = "openfld";
#ifdef NEVER
	DDFT	*tp=ddftype(fd->type);

	if(tp!=DDFTPN)
	{
#endif /* NEVER */
          FLD	*f=(FLD *)TXmalloc(TXPMBUFPN, fn, sizeof(FLD));

		if(f!=FLDPN)
		{
                  if (!initfldfd(f, fd, 0)) f = closefld(f);
		}
		return(f);
#ifdef NEVER
	}
	return(FLDPN);
#endif /* NEVER */
}

int
initfld(f, type, n)
FLD     *f;
int     type;
size_t  n;
/* Initializes `f' to FTN `type', without allocating shadow field for speed.
 * Used in conjunction with releasefld().  Returns 0 on error.
 */
{
  DDFD  *fd;

  if ((fd = ftn2ddfd_quick(type, n)) == (DDFD *)NULL) return(0);
  return(initfldfd(f, fd, 1));
}

void
putfldinit(f, buf, n)
FLD     *f;     /* The field to add the value to */
void    *buf;   /* Pointer to the data to add to the field */
size_t  n;      /* The number of elements */
/* Like putfld(), but for FLDs created with initfld(): sets shadow too.
 */
{
  putfld(f, buf, n);
  if (f->v == buf && f->shadow == NULL)
    {
      f->shadow = f->v;
      f->frees = 0;
      f->alloced = f->size;
    }
}

/************************************************************************/

#ifndef NO_HAVE_DDBF
        /* creates a new field and allocates memory to hold it */

FLD *
openstfld(fd)
DDFD *fd;
{
	static CONST char	fn[] = "openstfld";
	FLD	*f;

	if(fd->stsize == 0)
		return NULL;
	f=(FLD *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(FLD));

	if(f!=FLDPN)
	{
#ifndef NEVER
		if(fd->size > 255)
			f->wasmemo = 1;
#endif
		f->type=fd->sttype;
		f->elsz=fd->stelsz;
		f->size=fd->stsize;
		f->n=f->size/f->elsz;   /* this is needed for fixed stuff */
		f->alloced=f->size+1;
		f->v=(void *)NULL;
		/* allocated + 1 to hold a 0 terminator */
		if((f->shadow=TXcalloc(TXPMBUFPN, fn, 1, f->size+1))==(void *)NULL)
			f=closefld(f);
		else
			f->frees = FREESHADOW;
	}
	return(f);
}

#endif
/************************************************************************/
/* put a value into a field.  This function puts the pointer in buf into
 * the field.  The data pointed to by buf must stay valid until the value
 * in field is no longer needed.  N is the number of elements in the buffer,
 * not the number of bytes.  For example if you had 2 integers pointed to
 * by buf, and f was an integer field, n should be set to 2.
 */

void
putfld(f,buf,n)
FLD	*f;	/* The field to add the value to */
void	*buf;	/* Pointer to the data to add to the field */
size_t	n;	/* The number of elements */
{
	static CONST char Fn[] = "putfld";
	size_t size= n*f->elsz; /* size in bytes of the output field */

	f->kind=TX_FLD_NORMAL;
	if(!buf)
	{
		f->v = NULL;
		f->n = 0;
		f->size = 0;
	}
	if ((f->type & DDTYPEBITS) == FTN_INTERNAL)
	{
		if (buf && !TX_FTI_VALID((ft_internal *)buf))
		{
			putmsg(MERR + UGE, Fn, TxCorruptFtiObj);
			buf = NULL;
			n = 0;
		}
		goto dovar;
	}
	else if (f->type & DDVARBIT)
	{
	dovar:
		f->v=buf;       /* point at the user's data */
		f->n=n;         /* just keep your paws off my structure ! */
		f->size=size;   /* adjust the size */
	}
	else /* its a fixed field , check its size */
	{
#ifdef DEVEL
		if(size<f->size)           /* users field is too short ! */
		{
			/* Can't just write into shadow */
			if(f->frees && f->shadow)
				f->shadow = TXfree(f->shadow);
			if ((f->shadow = (void *)TXcalloc(TXPMBUFPN, Fn, 1, f->size)) == NULL)
			  {
			    f->frees = 0;
			    return;			/* WTF error return */
			  }
			f->frees = FREESHADOW;
			memcpy(f->shadow,buf,size);
			f->v=f->shadow;
		}
#else /* !DEVEL */
		if(size<f->size)           /* users field is too short ! */
		{
                        if (f->shadow == NULL ||        /* KNG 981112 */
                            f->frees != FREESHADOW)     /* KNG 990601 */
                          {
			  alloc:
                            f->alloced = f->size + 1;
			    if ((f->shadow = TXmalloc(TXPMBUFPN, Fn, f->alloced)) == NULL)
			      {
				f->frees = 0;
				return;			/* WTF error return */
			      }
                            f->frees = FREESHADOW;
                          }
                        else if (f->alloced <= f->size)	/* KNG 010412 */
			  {
			    f->shadow = TXfree(f->shadow);
			    goto alloc;
			  }
			memcpy(f->shadow, buf, size);	/* copy to shadow */
			memset((void *)((char *)f->shadow + size), 0,
			       f->size - size);		/* zero pad it */
			f->v=f->shadow;
#  ifdef NEVER
			f->n = n;
			f->size = size;
#  endif /* NEVER */
		}
#endif /* !DEVEL */
		else f->v=buf;  /* it might be too long, but who cares! */
	}
}

/******************************************************************/
#ifndef NO_HAVE_DDBF

static int mkdbasefld ARGS((FLD *));

static int
mkdbasefld(f)
FLD *f;
/* Thread unsafe.
 */
{
	FLD	*x;
	FLDOP	*fo;
	static int	noc = 0;
	void  	*memv;
	void  	*storagev;

	if(noc)
		return 0;
	fo = dbgetfo();
	memv = f->v;
	if(!f->v)
	{
		setfldv(f);
		memset(f->v, 0, f->alloced);
	}
	fopush(fo, f);
	storagev = f->storage->v;
	if(!f->storage->v)
	{
		setfldv(f->storage);
	}
	fopush(fo, f->storage);
	noc = 1;
	if(foop(fo, FOP_ASN) < 0)
	{
		foclose(fo);
		/* Restore pointers so we know what's happening. */
		f->storage->v = storagev;
		f->v = memv;
		return -1;
	}
	x = fopop(fo);
	noc = 0;
	setfld(f, x->v, 2);
	x->frees = 0;
	x->storage = NULL;
	f->n = x->n;
	if((f->type & DDVARBIT) == FTN_CHAR)
		f->n = strlen(f->v);
	clearfld(x);
	closefld(x);
	foclose(fo);
	return 0;
}

#endif /* !NO_HAVE_DDBF */

/******************************************************************/

static CONST char vflddelim[] = "\n";
#define VFLDDELIMSZ	(sizeof(vflddelim) - 1)

/******************************************************************/

/*
    This function combines the constituent fields to create the
    virtual field.
*/

static int mkvirtual ARGS((FLD *));

static int
mkvirtual(f)
FLD *f;
/* Concantenates `f->fldlist' component fields into virtual field `f' value.
 * Returns -1 on error, 0 on success.
 */
{
	int	i, ret;
	byte	*p, *np;
	size_t	needed, prevneeded;
	EPI_STAT_S sb;
	size_t sz;
	FILE *fh = NULL;
	size_t nr, avail;
	void *v2;
	CONST char	*s;
	HTBUF	*accumBuf = NULL;
	FLDOP	*fldOp = FLDOPPN;
	FLD	*cnvFld = FLDPN, *resFld = FLDPN;
	TXPMBUF	*pmbuf = TXPMBUFPN;
	ft_blobi	*blobi = NULL;
	void	*largestBlobiData = NULL;
	size_t	largestBlobiDataSz = 0;
	size_t	largestBlobiDataOffset = 0;
	int	largestBlobiIdx = -1;
	char	tmp[8192];

	/* First iterate over component fields and determine needed mem.
	 * Also maybe cache the largest blobi, for re-use as merge buffer:
	 */
	needed = (size_t)0;
	for(i=0; i < f->vfc; i++)
	{
		FLD	*curFld = f->fldlist[i];

		if (!curFld) continue;

                if(FLD_IS_COMPUTED(curFld))
                  getfld(curFld, NULL);
		/* See TXfldIsNull(): any component makes `f' NULL: */
		if (TXfldIsNull(curFld))
		{
			ret = (TXfldSetNull(f) ? 0 : -1);
			goto done;
		}
#ifdef MEMDEBUG
		if(curFld == (FLD *)0xfefefefe)
			mac_wptr(f->fldlist);
#endif /* MEMDEBUG */
		prevneeded = needed;
		switch (TXfldbasetype(curFld))
		{
		case FTN_INDIRECT:
			if(EPI_STAT(curFld->v, &sb) == 0)
				needed += (size_t)sb.st_size;
			break;
		case FTN_BLOBI:
			blobi = (ft_blobi *)curFld->v;
			v2 = TXblobiGetPayload(blobi, &sz);

			if (v2 && (sz == 1) && (*(char *)v2 == '\0'))
			{
				/* Bug 7593: also do this in first pass */
				TXblobiFreeMem(blobi);
				v2 = NULL;
			}

			if (!v2) break;

			/* Eliminate trailing nuls: */
			/* Bug 7593: also do this in first pass */
			np = (byte *)v2 + sz - 1;
			while (np >= (byte *)v2)
			{
				if (*np == '\0') np--;
				else break;
			}
			sz = (np + 1) - (byte *)v2;

			/* Save the largest blobi's data, and re-use
			 * it as the merge buffer below.  This might
			 * be able to save needing 2x the size of a
			 * large blob in mem (source plus merge buffer):
			 */
			if (sz > largestBlobiDataSz &&
			    !accumBuf &&/* won't save mem if separate merge */
			    (v2 = TXblobiRelinquishMem(blobi, NULL)) != NULL)
			{
				TXfree(largestBlobiData);
				largestBlobiData = v2;
				largestBlobiDataSz = sz;
				largestBlobiDataOffset = needed;
				largestBlobiIdx = i;
			}
			needed += sz;
			TXblobiFreeMem(blobi);
			v2 = NULL;
			break;
		case FTN_INTERNAL:
			s = tx_fti_obj2str((ft_internal *)curFld->v);
			if (s != CHARPN) needed += strlen(s);
			break;
		case FTN_CHAR:
		case FTN_BYTE:
			needed += curFld->size;
			break;
		/* Unlike Metamorph indexing, in a virtual field we
		 * *do* convert strlst to varchar to get the delims;
		 * virtual fields already stick in a newline between
		 * fields anyway, and leaving a nul embedded in a
		 * varchar result can be bad:  KNG 20080319
		 */
		default:
			/* Will need to convert to varchar.
			 * Save time and only do it once, below:
			 */
			if (!accumBuf && !(accumBuf = openhtbuf()))
				goto err;
			if (largestBlobiData)
			{
				/* Since we are using a separate merge
				 * buffer `accumBuf' anyway, our
				 * save-large-blob optimization is useless:
				 */
				largestBlobiData = TXfree(largestBlobiData);
				largestBlobiDataSz =largestBlobiDataOffset = 0;
				largestBlobiIdx = -1;
			}
			break;
		}
		if (needed >= (size_t)EPI_OS_SIZE_T_MAX || needed < prevneeded)
			goto merr;
		prevneeded = needed;
		needed += (size_t)VFLDDELIMSZ;
		if (needed >= (size_t)EPI_OS_SIZE_T_MAX || needed < prevneeded)
			goto merr;
	}

	/* Second pass: alloc for and assemble the data: */
	if (accumBuf)			/* will accumulate in `accumBuf' */
		setfldandsize(f, NULL, 0, FLD_KEEP_KIND);	/* save some mem */
	else
	{
		byte	*mem;

		if (largestBlobiData)
		{
			/* Use largest blobi's data -- likely to be the
			 * majority of the merge, and already alloced --
			 * as the merge buffer, to save copying it.
			 * Resize it and move blob data to correct offset:
			 */
			mem = TXrealloc(pmbuf, __FUNCTION__, largestBlobiData,
					needed+1);
			if (!mem)
			{
#ifndef EPI_ALLOC_FAIL_SAFE
				largestBlobiData = NULL;
#endif /* !EPI_ALLOC_FAIL_SAFE */
				goto err;
			}
			memmove(mem + largestBlobiDataOffset, mem,
				largestBlobiDataSz);
			largestBlobiData = NULL;
			/* save ...Idx and ...DataSz */
		}
		else
		{
			mem = TXcalloc(pmbuf, __FUNCTION__, 1, needed + 1);
			if (!mem) goto err;
		}
		setfldandsize(f, mem, needed + 1, FLD_KEEP_KIND);
	}
	/* `f' type should already be varchar or varbyte, from nametofld() */
	if (!accumBuf && !f->v)
	{
	merr:
		putmsg(MWARN+MAE, __FUNCTION__,
		       "Could not allocate enough space");
		goto err;
	}
	p = f->v;

	for(i=0; i < f->vfc; i++)
	{
		void	*v;
		FLD	*curFld = f->fldlist[i];

		if (!curFld) continue;
		v = curFld->v;
		if (!v) continue;

		/* Space available in merge buffer.  Only applies if
		 * `!accumBuf':
		 */
		avail = needed - (p - (byte *)f->v);

		switch (TXfldbasetype(curFld))
		{
		case FTN_INDIRECT:
			if (!*(char *)v) break;
			fh = fopen(v, "rb");
			if (!fh) break;
			if (accumBuf)
			{
				do
				{
					nr = fread(tmp, 1, sizeof(tmp), fh);
					if (!htbuf_write(accumBuf, tmp, nr))
						goto err;
				}
				while (nr > 0);
			}
			else
			{
				nr = fread(p, 1, avail, fh);
				p += nr;
			}
			fclose(fh);
			fh = NULL;
			break;
		case FTN_BLOBI:
			blobi = (ft_blobi *)v;
			if (i == largestBlobiIdx && !accumBuf)
			{
				/* Data is already in `p' at the right
				 * place; confirm:
				 */
				if ((size_t)(p - (byte *)f->v) !=
				    largestBlobiDataOffset)
				{
					putmsg(MERR, __FUNCTION__,
					       "Internal error: largest blobi member of virtual field has data at wrong merge buffer offset (expected %+wd got %+wd)",
					       (EPI_HUGEINT)largestBlobiDataOffset,
					       (EPI_HUGEINT)(p - (byte *)f->v));
					goto err;
				}
				sz = largestBlobiDataSz;
				goto zapBlobiNuls;
			}
			v2 = TXblobiGetPayload(blobi, &sz);
			if (v2 && (sz == 1) && (*(char *)v2 == '\0'))
			{
				/* Bug 7593: also do this in first pass */
				TXblobiFreeMem(blobi);
				v2 = NULL;
			}
			if (!v2) break;
			if (accumBuf)
			{
				if (!htbuf_write(accumBuf, v2, sz)) goto err;
				nr = htbuf_getdata(accumBuf,
						   (char **)(void *)&p, 0);
				p += nr - sz;
			}
			else
			{
				if (sz > avail)
				{
					putmsg(MERR, __FUNCTION__,
					   "Internal error: Truncating blob");
					sz = avail;
				}
				memcpy(p, v2, sz);
			}
		zapBlobiNuls:
			/* Eliminate trailing nuls: */
			/* Bug 7593: also do this in first pass */
			np = p + sz - 1;
			while (np >= p)		/* Bug 7593 add `=' */
			{
				if(*np == '\0')
				np--;
				else
				break;
			}
			if (accumBuf)
				htbuf_addused(accumBuf, np - (p + sz - 1));
			p = np + 1;
			TXblobiFreeMem(blobi);
			v2 = NULL;
			break;
		case FTN_INTERNAL:
			s = tx_fti_obj2str((ft_internal *)v);
			if (!s) break;
			sz = strlen(s);
			if (accumBuf)
			{
				if (!htbuf_write(accumBuf, s, sz)) goto err;
			}
			else
			{
				if (sz > avail)
				{
					putmsg(MERR, __FUNCTION__,
				   "Internal error: Truncating FTN_INTERNAL");
					sz = avail;
				}
				memcpy(p, s, sz);
				p += sz;
			}
			break;
		case FTN_CHAR:
		case FTN_BYTE:
			if (accumBuf)
			{
				if (!htbuf_write(accumBuf, v, curFld->size))
					goto err;
			}
			else
			{
				sz = curFld->size;
				if (sz > avail)
				{
					putmsg(MERR, __FUNCTION__,
				"Internal error: Truncating char/byte field");
					sz = avail;
				}
				memcpy(p, v, sz);
				p += sz;
			}
			break;
		default:
			/* Convert to varchar: */
			if (!accumBuf) break;
			if (fldOp == FLDOPPN &&
			    (fldOp = dbgetfo()) == FLDOPPN)
			{
				putmsg(MERR + MAE, __FUNCTION__,
				       "Cannot open FLDOP");
				goto err;
			}
			if (cnvFld == FLDPN &&
			    (cnvFld = createfld("varchar", 1, 0)) == FLDPN)
			{
				putmsg(MERR + MAE, __FUNCTION__,
				       "Cannot open FLD");
				goto err;
			}
			putfld(cnvFld, "", 0);
			if (fopush(fldOp, curFld) != 0 ||
			    fopush(fldOp, cnvFld) != 0 ||
			    foop(fldOp, FOP_CNV) != 0 ||
			    (resFld = fopop(fldOp)) == FLDPN)
			{
				putmsg(MERR, __FUNCTION__,
	  "Cannot convert field type %s to varchar for virtual field",
				       TXfldtypestr(curFld));
				goto err;
			}
			v2 = getfld(resFld, &sz);
			if (!htbuf_write(accumBuf, v2, sz)) goto err;
			resFld = closefld(resFld);
			break;
		}
		if (accumBuf)
		{
			if (!htbuf_write(accumBuf, vflddelim, VFLDDELIMSZ))
				goto err;
		}
		else
		{
			sz = VFLDDELIMSZ;
			if (sz > avail)
			{
				putmsg(MERR + MAE, __FUNCTION__,
			"Internal error: Truncating virtual field delimiter");
				sz = avail;
			}
			memcpy(p, vflddelim, sz);
			p += sz;
		}
	}
	if (accumBuf)
	{
		if (!htbuf_write(accumBuf, "", 0)) goto err;	/* nul-term. */
		sz = htbuf_getdata(accumBuf, (char **)(void *)&p, 0x3);
		setfldandsize(f, p, sz + 1, FLD_KEEP_KIND);
	}
	else
		*p = '\0';
	ret = 0;				/* success */
	goto done;

err:
	ret = -1;				/* error */
done:
	if (accumBuf) accumBuf = closehtbuf(accumBuf);
	if (fldOp) fldOp = foclose(fldOp);
	if (cnvFld) cnvFld = closefld(cnvFld);
	if (resFld) resFld = closefld(resFld);
	if (fh)
	{
		fclose(fh);
		fh = NULL;
	}
	largestBlobiData = TXfree(largestBlobiData);
	largestBlobiDataSz = largestBlobiDataOffset = 0;
	largestBlobiIdx = -1;
	return(ret);
}

/************************************************************************/
                      /* get a value from a field */
void *
getfld(f,pn)
FLD	*f;
size_t	*pn;   /* number of items in the struct */
{
  switch(f->kind)
  {
    case TX_FLD_VIRTUAL:
      mkvirtual(f);
      break;
    case TX_FLD_COMPUTED_JSON:
      if(!f->v)
        TXmkComputedJson(f);
      break;
    default:
      break;
  }
#ifndef NO_HAVE_DDBF
  if(!f->v && f->storage)
  {
    mkdbasefld(f);
  }
#endif
  if(pn)
    *pn = f->n;
  return(f->v);
}

void *
TXfldTakeAllocedData(fld, pn)
FLD     *fld;   /* (in/out) field */
size_t  *pn;    /* (out) number of elements */
/* Like getfld(), but returned data is guaranteed to be alloced and owned
 * by caller; may "take" the data from `fld' (and clear `fld').
 * Returns alloced data of `fld', dup'ing if needed.
 */
{
  void          *allocedData;
  size_t        n;

  getfld(fld, &n);                              /* build value if virtual */
  if (TXfldIsNull(fld))
    {
      allocedData = NULL;
      n = 0;
    }
  else if (fld->shadow &&                       /* have shadow data */
           fld->frees == FREESHADOW &&          /* it is alloced */
           fld->v == fld->shadow)               /* it is the current value */
    {                                           /* then take it */
      allocedData = fld->shadow;
      fld->shadow = fld->v = NULL;
      fld->frees = 0;
    }
  else if (fld->v)                              /* dup the data */
    {
      allocedData = TXftnDupData(fld->v, fld->n, fld->type, fld->size, NULL);
    }
  else                                          /* no data */
    goto err;
  goto done;

err:
  n = 0;
  allocedData = NULL;
done:
  *pn = n;
  return(allocedData);
}

/************************************************************************/

FLD *
emptyfld(type, n)              /* create a minimal (empty) field of type */
FTN	type;
int	n;	/* (in) number of elements */
{
	DDFD fd;

	if(0 == getddfdnum(type, n, 1, "", &fd))
	{
		FLD  *f;
		f = openfld(&fd);
		if(f!=FLDPN)
			f->v=f->shadow;
		return(f);
	}
	 return(FLDPN);
}                                                   /* end emptyfld() */
/**********************************************************************/

FLD *
dupfld(f)
FLD *f;
{
	static CONST char	fn[] = "dupfld";
FLD *fd;

   if(!f) return f;
   if((fd=(FLD *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(FLD)))!=FLDPN){
       *fd= *f;
       if (TXfldIsNull(f) && !f->shadow)
         {
           fd->shadow = NULL;
           fd->frees = 0;
         }
       else                                     /* `f' is not NULL */
         {
           if (!(fd->shadow = TXftnDupData((f->v ? f->v : f->shadow), f->n,
                                           f->type, f->size, &fd->alloced)))
             fd = TXfree(fd);
           else
             {
               fd->frees = FREESHADOW;
               fd->v = (f->v ? fd->shadow : NULL);
             }
         }
#ifndef NO_HAVE_DDBF
       fd->storage = NULL;
       fd->memory = NULL;
#endif
       if(fd->vfc)
       {
         if (fd->kind == TX_FLD_COMPUTED_JSON)
         {
           fd->vfc = 0;
           fd->fldlist = NULL;
           fd->kind = TX_FLD_NORMAL;
         }
         else
         {
          int i;

          fd->fldlist = (FLD **)TXcalloc(TXPMBUFPN, fn, fd->vfc, sizeof(FLD *));
	  for(i=0; i < fd->vfc; i++)
          {
	      fd->fldlist[i] = f->fldlist[i];
          }
        }
       }
   }
   return(fd);
}

/**********************************************************************/

FLD *
newfld(f)
FLD *f;
{
  static CONST char       fn[] = "newfld";
FLD *fd;

   if((fd=(FLD *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(FLD)))!=FLDPN){
	if ((f->type & DDTYPEBITS) == FTN_INTERNAL)
	{
		memset(fd, 0, sizeof(FLD));
		fd->type = f->type;
		fd->v = fd->shadow =tx_fti_copy4read((ft_internal*)f->v,f->n);
		fd->frees = FREESHADOW;
		fd->n = f->n;
		fd->size = f->size;
		fd->elsz = f->elsz;
		return(fd);
	}
       *fd= *f;
       if(fldisvar(fd))
       {
	       fd->size=f->elsz;
	       fd->n=1;
       }
#ifndef NO_HAVE_DDBF
       fd->storage = NULL;
       fd->memory = NULL;
#endif
       if (!(fd->v = fd->shadow = TXcalloc(TXPMBUFPN, fn, 1, fd->size + 1))) {
          fd = TXfree(fd);
       }else{
         /* Make sure data is not garbage, e.g. for TXfldmathverb msgs: */
          switch (fd->type & DDTYPEBITS)
	  {
          case FTN_BLOBI:
            {
	    /* Make sure we don't try and free these later */
              /* already cleared by calloc() */
            }
            break;
          case FTN_DOUBLE:
            if (fd->size >= sizeof(ft_double)) *(ft_double *)fd->v = 1;
            break;
          case FTN_FLOAT:
            if (fd->size >= sizeof(ft_float)) *(ft_float *)fd->v = 1;
            break;
	  }
          fd->frees=FREESHADOW;
          fd->alloced=fd->size+1;
          ((char *)fd->shadow)[fd->size]='\0';
       }
       if(fd->vfc)
       {
          int i;

          fd->fldlist = (FLD **)TXcalloc(TXPMBUFPN, fn, fd->vfc, sizeof(FLD *));
	  for(i=0; i < fd->vfc; i++)
          {
	      fd->fldlist[i] = f->fldlist[i];
          }
       }
   }
   return(fd);
}

/**********************************************************************/

int
clearfld(f)
FLD	*f;
{
	f->type = 0;
	f->shadow = NULL;
	f->alloced = 0;
	f->frees = 0;
	return 0;
}

/******************************************************************/

int
copyfld(nf, f)
FLD	*nf;	/* (out) destination field */
FLD	*f;	/* (in) source field */
/* Copies `f' to `nf', completely replacing type etc. of `nf'.
 * Returns 0 if ok, else FOP_E... error.
 */
{
	static CONST char	fn[] = "copyfld";
	void	*shadow, *newbuf;
	size_t	alloced;
	int	frees = 0;

	if ((f->type & DDTYPEBITS) == FTN_INTERNAL)
	{
		memset(nf, 0, sizeof(FLD));
		nf->type = f->type;
		nf->v = nf->shadow =tx_fti_copy4read((ft_internal*)f->v,f->n);
		if (!nf->v) return(FOP_ENOMEM);
		nf->frees = FREESHADOW;
		nf->n = f->n;
		nf->size = f->size;
		nf->elsz = f->elsz;
		return(0);
	}

	shadow = nf->shadow;
	alloced = nf->alloced;
	frees = nf->frees;
	if(alloced<f->size+1)
	{
		if((nf->frees != FREESHADOW) || (alloced==0))
		{
			shadow = TXmalloc(TXPMBUFPN, fn, f->size + 1);
			frees = FREESHADOW;
		}
		else
		{
			newbuf = TXrealloc(TXPMBUFPN, fn, shadow, f->size + 1);
#ifdef EPI_REALLOC_FAIL_SAFE
			if (!newbuf && shadow) shadow = TXfree(shadow);
#endif /* EPI_REALLOC_FAIL_SAFE */
			shadow = newbuf;
		}
		if(shadow==(void *)NULL)
			return(FOP_ENOMEM);
		alloced=f->size+1;
	}
	if(f->v)
          {
            memcpy(shadow,f->v,f->size);
            /* Bug 4031: dup ft_blobi.memdata if needed: */
            if ((f->type & DDTYPEBITS) == FTN_BLOBI)
              {
                ft_blobi        *destBlobi = (ft_blobi *)shadow;
                char            *memdata, *destMem;
                size_t          destSz;

                if (TXblobiIsInMem(destBlobi))
                  {
                    memdata =(char*)TXmalloc(TXPMBUFPN, fn, destBlobi->len+1);
                    if (!memdata) return(FOP_ENOMEM);   /* wtf memleak? */
                    destMem = TXblobiGetMem(destBlobi, &destSz);
                    memcpy(memdata, destMem, destSz);
                    memdata[destSz] = '\0';
                    /* WTF why assign to `destBlobi': already set? leak here?*/
                    TXblobiSetMem(destBlobi, memdata, destSz, 1);
                  }
              }
          }
	*((char *)shadow+f->size)='\0';
	*nf= *f;
	if(f->v)
		nf->v=nf->shadow=shadow;
	else
	{
		nf->shadow=shadow;
		nf->v = NULL;
	}
	nf->alloced=alloced;
	nf->frees = frees;
	return 0;
}

/******************************************************************/

int
setfld(f, v, n)
/* Sets value of `f' to `v'.  `v' is assumed alloc'd and `f' will own and
 * free it, unless TXsetshadownonalloc() called immediately after this.
 * `v' is `n' bytes (including nul).  Does not set size/n.
 * Returns 0 if ok, -1 on error.
 */
FLD	*f;
void	*v;
size_t	n;
{
	TXfreefldshadownotblob(f);
	if (n <= (size_t)0 || !v)		/* sanity check */
	{
		/* wtf cannot free `v' because it may not be alloced,
		 * if they call TXsetshadownonalloc() after this
		 */
		v = NULL;
		f->alloced = n = 0;
		f->frees = 0;
	}
	else
	{
		f->alloced = n--;		/* -1 for nul */
		f->frees = FREESHADOW;
	}
	f->v = f->shadow = v;
	f->kind = TX_FLD_NORMAL;
#ifdef NEVER
	f->size = n;
	if(f->elsz)
		f->n = n/f->elsz;
	else
		f->n = n;
#endif
	return 0;
}

int
setfldandsize(FLD *f, void *v, size_t n, TXbool forceNormal)
/* n (in) alloced byte size of `v', including nul */
/* Like setfld(), but also sets `f->size' and `f->n'.
 * Returns 0 if ok, -1 on error.
 */
{
	TXfreefldshadownotblob(f);
	if (n <= (size_t)0 || !v)		/* sanity check */
	{
		/* wtf cannot free `v' because it may not be alloced,
		 * if they call TXsetshadownonalloc() after this
		 */
		v = NULL;
		f->alloced = n = 0;
		f->frees = 0;
	}
	else
	{
		f->alloced = n--;		/* -1 for nul */
		f->frees = FREESHADOW;
	}
	f->v = f->shadow = v;
        if(forceNormal)
        {
           if((f->kind == TX_FLD_COMPUTED_JSON) && (f->vfc == 2))
           {
              f->fldlist[1] = closefld(f->fldlist[1]);
              f->vfc = 1;
           }
           f->kind = TX_FLD_NORMAL;
        }

	f->size = n;
	if(f->elsz)
		f->n = n/f->elsz;
	else
		f->n = n;
	return 0;
}

/******************************************************************/

int
fldisset(FLD *f)
{
	if(f->v)
		return 1;
	return 0;
}

/******************************************************************/

void *
TXftnDupData(data, n, type, size, alloced)
void	*data;			/* (in) data to dup */
size_t	n;			/* (in) # elements in `data' */
int	type;			/* (in) FTN type */
size_t  size;                   /* (in) size of `data' */
size_t  *alloced;               /* (out, opt.) alloced size */
/* Returns alloc'd dup of `data', or NULL on error.
 */
{
  static CONST char     fn[] = "TXftnDupData";
  void                  *ret;
  ft_blobi              *blobi = NULL;

  if (alloced) *alloced = 0;
  switch (type & DDTYPEBITS)
    {
    case FTN_INTERNAL:
      ret = tx_fti_copy4read((ft_internal *)data, n);
      break;
    case FTN_BLOBI:
      blobi = (ft_blobi *)data;
      /* Bug 4031: If `blobi->ndfree', dup `blobi->memdata' to avoid
       * double-free of it when the FLD that `data' belongs to is
       * closed and then our (`ret') FLD is closed.  WTF would like a
       * ref count instead.  WTF can we assume that `blobi->memdata'
       * is valid through both closefld()s if `!blobi->ndfree'?:
       */
      if (TXblobiIsInMem(blobi))
        {
          void          *blobMem;
          ft_blobi      *retBlobi;
          char          *dupBlobiMem = CHARPN;
          size_t        dupBlobiMemSz = 0;

          blobMem = TXblobiGetMem(blobi, &dupBlobiMemSz);
          dupBlobiMem = (char *)TXmalloc(TXPMBUFPN, fn, dupBlobiMemSz + 1);
          if (!dupBlobiMem) return(NULL);
          memcpy(dupBlobiMem, blobMem, dupBlobiMemSz);
          dupBlobiMem[dupBlobiMemSz] = '\0';
          /* Now do regular FLD data copy, using OO blob funcs: */
          ret = TXcalloc(TXPMBUFPN, fn, 1, sizeof(ft_blobi) + 1);
          retBlobi = (ft_blobi *)ret;
          TXblobiSetMem(retBlobi, dupBlobiMem, dupBlobiMemSz, 1);
          TXblobiSetDbf(retBlobi, TXblobiGetDbf(blobi)); /* wtf ref count */
          dupBlobiMem = NULL;                   /* `retBlobi' owns it now */
          dupBlobiMemSz = 0;
          if (alloced) *alloced = sizeof(ft_blobi) + 1;
          break;
        }
      /* fall through */	/* that exact comment shuts up gcc 7 */
    default:
      ret = TXmalloc(TXPMBUFPN, fn, size + 1);
      if (ret)
        {
          memcpy(ret, data, size);
          ((char *)ret)[size] = '\0';
          if (alloced) *alloced = size + 1;
        }
      break;
    }
  return(ret);
}

int
TXftnFreeData(data, n, type, blobiMemdataToo)
void    *data;                  /* (in) data to free */
size_t  n;                      /* (in) # elements in `data' */
int     type;                   /* (in) FTN type */
int     blobiMemdataToo;        /* (in) nonzero: free ft_blobi.memdata too */
/* Frees FLD-like `data' (# elements `n') of FTN type `type'.
 * Note: also add to Vortex VDATF_FTN{INTERNAL,BLOBI,...} special types if
 * special-case types changes here.
 * Returns 0 on error.
 */
{
  ft_blobi  *blobi;

  if (!data) return(1);

  switch (type & DDTYPEBITS)
    {
    case FTN_INTERNAL:
      tx_fti_close((ft_internal *)data, n);
      data = NULL;
      break;
    case FTN_BLOBI:
      blobi = (ft_blobi *)data;
      if (blobiMemdataToo) TXblobiFreeMem(blobi);
      /* fall through */	/* that exact comment shuts up gcc 7 */
    default:
      data = TXfree(data);
      break;
    }
  return(1);
}

/* ------------------------------------------------------------------------ */

static int
TXemptyblobi(TXPMBUF *pmbuf, ft_blobi *vb)
/* Returns 0 on error.
 */
{
	static const char	fn[] = "emptyblobi";

	if (vb)
	{
		void	*mem;

		mem = TXstrdup(pmbuf, fn, "");
		if (!mem) return(0);
		TXblobiSetMem(vb, mem, 0, 1);
		TXblobiSetDbf(vb, NULL);
	}
        return(1);
}

/* ------------------------------------------------------------------------ */

int
TXftnInitDummyData(TXPMBUF *pmbuf, FTN type, void *data, size_t sz,
                   int forFldMath)
/* Initializes `data' (byte size `sz') with dummy data for `type'.
 * If `forFldMath', tries to avoid values that would cause fldmath issues.
 * Returns 0 on error.
 */
{
  static const char     fn[] = "TXftnInitDummyData";
  ft_strlst             *sl;
  ft_datetime           *dt;
  int                   numericVal = (forFldMath ? 1 : 0), ret;

  if (sz < ddftsize(type)) goto tooSmall;
  switch (type & DDTYPEBITS)
    {
    case FTN_DOUBLE:
      *(ft_double *)data = (ft_double)numericVal;
      break;
    case FTN_FLOAT:
      *(ft_float *)data = (ft_float)numericVal;
      break;
      /* KNG 20111122 for consistent verbose tracing across
       * big/little-endian, init all numeric and counter types
       * correct-endianly:
       */
    case FTN_INT:
      *(ft_int *)data = (ft_int)numericVal;
      break;
    case FTN_INTEGER:
      *(ft_integer *)data = (ft_integer)numericVal;
      break;
    case FTN_LONG:
      *(ft_long *)data = (ft_long)numericVal;
      break;
    case FTN_SHORT:
      *(ft_short *)data = (ft_short)numericVal;
      break;
    case FTN_SMALLINT:
      *(ft_smallint *)data = (ft_smallint)numericVal;
      break;
    case FTN_WORD:
      *(ft_word *)data = (ft_word)numericVal;
      break;
    case FTN_DWORD:
      *(ft_dword *)data = (ft_dword)numericVal;
      break;
    case FTN_INT64:
      *(ft_int64 *)data = (ft_int64)numericVal;
      break;
    case FTN_UINT64:
      *(ft_uint64 *)data = (ft_uint64)numericVal;
      break;
    case FTN_COUNTER:
      ((ft_counter *)data)->date = (forFldMath ? 3 : 0);
      ((ft_counter *)data)->seq = 0;
      break;
    case FTN_BLOBI:
      if (!TXemptyblobi(pmbuf, data)) goto err;
      break;
    case FTN_STRLST:
      if (sz < (size_t)TX_STRLST_MINSZ)
        {
        tooSmall:
          txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
                         "Field data size %wd too small for type `%s'",
                         (EPI_HUGEINT)sz, ddfttypename(type));
          goto err;
        }
      memset(data, 0, sizeof(ft_strlst));
      sl = (ft_strlst *)data;
      sl->nb = 0;
      sl->delim = TxPrefStrlstDelims[0];
      sl->buf[0] = '\0';
      break;
    case FTN_DATE:
      /* KNG 020619 64-bit big-endian HPUX localtime() segfault on
       * large date:
       */
      *(ft_date *)data = (forFldMath ? 3 : 0);
      break;
    case FTN_DATETIME:
      dt = (ft_datetime *)data;
      /* Set a sane, convertible-to-date value: */
      dt->year = (forFldMath ? 2000 : 1970);
      dt->month = dt->day = 1;
      dt->hour = dt->minute = dt->second = 0;
      dt->fraction = 0;
      break;
    case FTN_INTERNAL:
      /* FLD value (but not sub-obj) already set by createfld(): */
      break;
    default:
      if (forFldMath)
        *(byte *)data = '0';
      else
        *(byte *)data = '\0';
      break;
    }
  ret = 1;                                      /* success */
  goto finally;

err:
  ret = 0;
finally:
  return(ret);
}

int
freeflddata(f)
FLD	*f;
{
	if (f->v && f->v != f->shadow)
	{
		TXftnFreeData(f->v, f->n, f->type, 1);
		f->v = NULL;
	}
	f->v = f->shadow;
	return 0;
}

EPI_SSIZE_T
TXprintHexCounter(char *buf, EPI_SSIZE_T bufSz, const ft_counter *ctr)
/* Prints `*ctr' to `buf' in hex, nul-terminating if room.
 * Returns would-be strlen() of `buf': may exceed `bufSz'
 * (will not write past).
 */
{
  /* We must print with exactly the same-sized type as the
   * corresponding ft_counter field: any smaller, and we truncate
   * data; any larger, and we bloat with leading `fff...'  for
   * negative values:
   */
#if TX_FT_COUNTER_DATE_BITS == EPI_OS_LONG_BITS
  static const char     counterPrFmt8[] = "%08lx%lx";
#  if TX_FT_COUNTER_DATE_BITS >= 64
  static const char     counterPrFmt16[] = "%016lx%lx";
#  endif /* TX_FT_COUNTER_DATE_BITS >= 32 */
#elif TX_FT_COUNTER_DATE_BITS == EPI_HUGEINT_BITS
  static const char     counterPrFmt8[] = "%08wx%wx";
#  if TX_FT_COUNTER_DATE_BITS >= 64
  static const char     counterPrFmt16[] = "%016wx%wx";
#  endif /* TX_FT_COUNTER_DATE_BITS >= 32 */
#else
  error error error pick a format code;
#endif
#if TX_FT_COUNTER_DATE_BITS != TX_FT_COUNTER_SEQ_BITS
  error error error date-size == seq-size assumed in counterPrFmt...;
#endif /* TX_FT_COUNTER_DATE_BITS != TX_FT_COUNTER_SEQ_BITS */

#if TX_FT_COUNTER_DATE_BITS == 32
  if (bufSz <= (EPI_SSIZE_T)0) return(0);
  return((EPI_SSIZE_T)htsnpf(buf, bufSz, counterPrFmt8, ctr->date, ctr->seq));
#elif TX_FT_COUNTER_DATE_BITS == 64
  TXft_counterDate      dateVal = ctr->date;
  const char            *fmt;

  if (bufSz <= (EPI_SSIZE_T)0) return(0);
  if (dateVal >  (TXft_counterDate)0x7fffffff ||
      dateVal < -(TXft_counterDate)0x80000000 ||
      ctr->seq > (TXft_counterSeq) 0xffffffffU)
    fmt = counterPrFmt16;
  else
    {
      fmt = counterPrFmt8;
      /* Bug 4705: For negative dates that can be represented in 32
       * bits, do not let the negative sign cause leading `ffffffff'
       * because our date type is 64-bit: print the same value
       * consistently across platforms, letting 32-bit boxes parse it:
       */
      if (dateVal < (TXft_counterDate)0)
        /* we know `dateVal' is >= -0x80000000; was checked above */
        dateVal &= (TXft_counterDate)0xffffffff;
    }
  return((EPI_SSIZE_T)htsnpf(buf, bufSz, fmt, dateVal, ctr->seq));
#else /* TX_FT_COUNTER_DATE_BITS != 32 && TX_FT_COUNTER_DATE_BITS != 64 */
  error error error add new logic;
#endif /* TX_FT_COUNTER_DATE_BITS != 32 && TX_FT_COUNTER_DATE_BITS != 64 */
}

int
TXparseHexCounter(ft_counter *ctr, const char *s, const char *e)
/* Parses `s' (ending at `e', or s + strlen(s) if NULL) for a hex counter,
 * setting `*ctr'.  Returns 0 on error (and zeros out `*ctr').
 */
{
  size_t        sLen, dateLen;
  char          *parseEnd, *seqStart;
  int           errnum;
#define BITS_PER_HEX_DIGIT      4

  sLen = (e ? (size_t)(e -s) : strlen(s));
  if (sLen == 0)                                /* Bug 5443 */
    {
      TX_ZERO_COUNTER(ctr);
      goto ok;
    }
  /* We print counters as 8 date digits plus 1-8 seq digits when both
   * values are 31 bits or less (regardless of sizeof(date|long)), for
   * portable counter strings.  When either value is over 31 bits, we
   * print the date with 16 digits.  So a hex counter over 16 digits
   * indicates a 16-digit date string:
   */
  if (sLen > 16)                                /* 64-bit date in string */
    {
#if TX_FT_COUNTER_DATE_BITS >= 16*BITS_PER_HEX_DIGIT
      dateLen = 16;
#else                                           /* date type not big enough */
      goto err;
#endif
    }
  else
    dateLen = 8;
  /* Use a parse function with the right-sized type: too small and we
   * might overflow the type and lose data, too large and we might
   * fail to detect overflow when assigning the ft_counter field from
   * the parsed value.  Use an unsigned parser for both date and seq
   * -- even though ft_counter.date might be signed -- because the hex
   * values we parse are unsigned; otherwise negative-date hex values
   * might cause TXstrto...() to overflow:
   */
#if TX_FT_COUNTER_DATE_BITS == EPI_OS_LONG_BITS
#  define FUNC TXstrtoul
#elif TX_FT_COUNTER_DATE_BITS == EPI_HUGEINT_BITS
#  define FUNC TXstrtouh
#else
  error error pick a TXstrto... function;
#endif
  ctr->date = (TXft_counterDate)
    FUNC(s, s + TX_MIN(dateLen, sLen), &parseEnd, 16, &errnum);
  if (parseEnd <= s || errnum != 0) goto err;   /* parse error or overflow */
  /* Bug 4705: A 32-bit negative-date string will parse into a 64-bit
   * date type as a positive not negative number.  Negate it properly
   * so that e.g. counter `987654321' is the same (negative) date on
   * 32- and 64-bit-date platforms:
   */
  if (TX_FT_COUNTER_DATE_BITS > dateLen*BITS_PER_HEX_DIGIT &&
      ctr->date >= ((TXft_counterDate)1 << (dateLen*BITS_PER_HEX_DIGIT - 1)))
    ctr->date |= ~(((TXft_counterDate)1 << (dateLen*BITS_PER_HEX_DIGIT - 1)) -
                   (TXft_counterDate)1);
  /* If there are no more digits, let seq be 0.  E.g. `123' becomes
   * date 0x123, seq 0:
   */
  seqStart = parseEnd;
  if (parseEnd >= s + sLen)                     /* no more digits to parse */
    ctr->seq = (TXft_counterSeq)0;
  else
    {
      ctr->seq = (TXft_counterSeq)FUNC(seqStart, s + sLen, &parseEnd, 16,
                                       &errnum);
      if (parseEnd <= seqStart || errnum != 0) goto err;
    }
ok:
  return(1);                                    /* success */
err:
  TX_ZERO_COUNTER(ctr);
  return(0);                                    /* error */
#undef FUNC
#undef BITS_PER_HEX_DIGIT
}

TXbool
TXfldIsMultipleItemType(fld, mainType, itemType)
FLD     *fld;
FTN     *mainType;      /* (out, opt.) main type (may != fld->type) */
FTN     *itemType;      /* (out, opt.) item type */
/* Returns true if `fld' is a type that can have a multiple-item value.
 * Sets `*type' to overall FTN type: if FTI_valueWithCooked, its FTN type,
 * else `fld->type'.
 * Sets `*itemType' to FTN type that individual items would have, regardless.
 * Note that `fld' may not necessarily *contain* multiple items;
 * use TXfldNumItems(fld) for that.
 * Drills into FTN_INTERNAL.FTI_valueWithCooked type.
 * NOTE: should correspond with TXfldGetNextItem().
 */
{
  FTN                   type;
  size_t                numEls = 0;
  TXbool                gotNumEls = TXbool_False;
  ft_internal           *fti;
  TXftiValueWithCooked  *valueWithCooked;

  /* Delay getfld() as long as possible: may invoke mkvirtual() at bad
   * time (e.g. with source fields pointing into freed data):
   */
  type = TXfldType(fld);
  if ((type & DDTYPEBITS) == FTN_INTERNAL)
    {
      fti = (ft_internal *)getfld(fld, &numEls);
      gotNumEls = TXbool_True;
      if (fti &&
          tx_fti_gettype(fti) == FTI_valueWithCooked &&
          (valueWithCooked = tx_fti_getobj(fti)) != NULL)
        {
          TXftiValueWithCooked_GetValue(valueWithCooked, &type, &numEls, NULL);
          fld = NULL;
        }
    }

  if (mainType) *mainType = type;

  switch (type & DDTYPEBITS)
    {
    case FTN_STRLST:
      if (itemType) *itemType = (FTN)(DDVARBIT | FTN_CHAR);
      return(TXbool_True);
    case FTN_DECIMAL:
    case FTN_DOUBLE:
    case FTN_DATE:
    case FTN_FLOAT:
    case FTN_INT:
    case FTN_INTEGER:
    case FTN_LONG:
    case FTN_SHORT:
    case FTN_SMALLINT:
    case FTN_WORD:
    case FTN_HANDLE:
    case FTN_DWORD:
    case FTN_COUNTER:
    case FTN_DATESTAMP:
    case FTN_TIMESTAMP:
    case FTN_DATETIME:
    case FTN_COUNTERI:
    case FTN_RECID:
    case FTN_INT64:
    case FTN_UINT64:
      /* Delay getfld() as much as possible; see above comment: */
      if (type & DDVARBIT) goto isMulti;
      if (!gotNumEls)
        {
          getfld(fld, &numEls);
          gotNumEls = TXbool_True;
        }
      if (numEls > 1)
        {
        isMulti:
          /* Item type is fixed-size: */
          if (itemType) *itemType = (type & DDTYPEBITS);
          return(TXbool_True);
        }
      if (itemType) *itemType = (FTN)type;
      return(TXbool_False);
    case FTN_BYTE:
    case FTN_CHAR:
    case FTN_BLOB:
    case FTN_INDIRECT:
    case FTN_BLOBI:
    case FTN_BLOBZ:
    case FTN_INTERNAL:
    default:                                    /* assume 1 item */
      if (itemType) *itemType = (FTN)type;
      return(TXbool_False);
  }
}

size_t
TXfldNumItems(fld)
FLD     *fld;
/* Returns number of items in `fld' value.
 * Note that `fld' may be of a type that *can* hold multiple items,
 * but has only 1 currently; use TXfldIsMultipleItemType(fld) for that.
 * Drills into FTN_INTERNAL.FTI_valueWithCooked type.
 * NOTE: should correspond with TXfldGetNextItem().
 */
{
  FTN                   type;
  void                  *value;
  size_t                numEls, size;
  ft_internal           *fti;
  TXftiValueWithCooked  *valueWithCooked;
  FLD                   tmpFld;
  size_t                n;
  ft_strlst             sl;
  char                  *buf, *bufEnd, *s;

  type = TXfldType(fld);
  value = getfld(fld, &numEls);
  size = fld->size;
  if ((type & DDTYPEBITS) == FTN_INTERNAL &&
      (fti = (ft_internal *)value) != NULL &&
      tx_fti_gettype(fti) == FTI_valueWithCooked &&
      (valueWithCooked = tx_fti_getobj(fti)) != NULL)
    {
      value = TXftiValueWithCooked_GetValue(valueWithCooked, &type, &numEls,
                                            &size);
      fld = NULL;
    }

  switch (type & DDTYPEBITS)
    {
    case FTN_STRLST:
      if (!fld)
        {
          memset(&tmpFld, 0, sizeof(FLD));
          tmpFld.type = type;
          tmpFld.v = value;
          tmpFld.elsz = TX_STRLST_ELSZ;
          tmpFld.n = numEls;
          tmpFld.size = size;
          buf = TXgetStrlst(&tmpFld, &sl);
        }
      else
        buf = TXgetStrlst(fld, &sl);
      bufEnd = buf + sl.nb;
      if (bufEnd > buf && !bufEnd[-1]) bufEnd--; /* ignore strlst-term. nul */
      for (n = 0, s = buf; s < bufEnd; s++)
        if (!*s) n++;
      if (s > buf && s[-1]) n++;                /* last item unterminated */
      break;
    case FTN_DECIMAL:
    case FTN_DOUBLE:
    case FTN_DATE:
    case FTN_FLOAT:
    case FTN_INT:
    case FTN_INTEGER:
    case FTN_LONG:
    case FTN_SHORT:
    case FTN_SMALLINT:
    case FTN_WORD:
    case FTN_HANDLE:
    case FTN_DWORD:
    case FTN_COUNTER:
    case FTN_DATESTAMP:
    case FTN_TIMESTAMP:
    case FTN_DATETIME:
    case FTN_COUNTERI:
    case FTN_RECID:
    case FTN_INT64:
    case FTN_UINT64:
      n = numEls;
      break;
    case FTN_BYTE:
    case FTN_CHAR:
    case FTN_BLOB:
    case FTN_INDIRECT:
    case FTN_BLOBI:
    case FTN_BLOBZ:
    case FTN_INTERNAL:
    default:                                    /* assume 1 item */
      n = 1;
      break;
  }
  return(n);
}

void *
TXfldGetNextItem(fld, prevItem, prevItemLen, itemLen)
FLD	*fld;		/* (in) FLD */
void	*prevItem;	/* (in) previous item; NULL if first call */
size_t	prevItemLen;	/* (in) its element length */
size_t	*itemLen;	/* (out) number of elements in returned item */
/* Returns pointer to next item in multi-item `fld', and sets `*itemLen'
 * to number of elements in it (generally 1, unless returning varchar
 * for strlst).  Call again with `prevItem' equal to return value and
 * `prevItemLen' equal to `*itemLen' to get next item.
 * Drills into FTN_INTERNAL.FTI_valueWithCooked type.
 * Returns NULL if no more items.
 * NOTE: Do not set or alter field between calls.
 * NOTE: should correspond with TXfldIsMultipleItemType(), TXfldNumItems(),
 * TXfldGetNextItemStr().
 */
{
	FTN		type;
	void		*v, *vEnd;
	size_t		n, size, elsz;
	ft_internal	*fti;
	TXftiValueWithCooked	*valueWithCooked;
	FLD		tmpFld;
	ft_strlst	slHdr;
	char		*s, *e, *slEnd;
	DDFD		*fd;

	type = TXfldType(fld);
	v = getfld(fld, &n);
	size = fld->size;
	elsz = fld->elsz;
	if ((type & DDTYPEBITS) == FTN_INTERNAL &&
	    (fti = (ft_internal *)v) != NULL &&
	    tx_fti_gettype(fti) == FTI_valueWithCooked &&
	    (valueWithCooked = tx_fti_getobj(fti)) != NULL)
	{
		v = TXftiValueWithCooked_GetValue(valueWithCooked, &type, &n,
						  &size);
		fd = ftn2ddfd_quick(type, n);
		elsz = (fd ? fd->elsz : 1);
		fld = NULL;
	}
	vEnd = (byte *)v + size;

	/* NOTE: See also TXbtreeIsOnMultipleItemType(): */
	switch (type & DDTYPEBITS)
	{
	case FTN_STRLST:
		if (!fld)
		{
			memset(&tmpFld, 0, sizeof(FLD));
			tmpFld.type = type;
			tmpFld.v = v;
			tmpFld.elsz = TX_STRLST_ELSZ;
			tmpFld.n = n;
			tmpFld.size = size;
			s = TXgetStrlst(&tmpFld, &slHdr);
		}
		else
			s = TXgetStrlst(fld, &slHdr);
		if (s == CHARPN) goto eof;
		slEnd = s + slHdr.nb;
		if (slEnd > s && !slEnd[-1]) slEnd--;	/* list-term. nul */
		if (prevItem != NULL)
			s = (char *)prevItem + prevItemLen + 1;
		if (s >= slEnd) goto eof;
		for (e = s; e < slEnd && *e; e++);
		*itemLen = e - s;
		return(s);
	case FTN_DECIMAL:
	case FTN_DOUBLE:
	case FTN_DATE:
	case FTN_FLOAT:
	case FTN_INT:
	case FTN_INTEGER:
	case FTN_LONG:
	case FTN_SHORT:
	case FTN_SMALLINT:
	case FTN_WORD:
	case FTN_HANDLE:
	case FTN_DWORD:
	case FTN_COUNTER:
	case FTN_DATESTAMP:
	case FTN_TIMESTAMP:
	case FTN_DATETIME:
	case FTN_COUNTERI:
	case FTN_RECID:
	case FTN_INT64:
	case FTN_UINT64:
		/* Fixed-size-element types: */
		if (prevItem != NULL)
		{
			v = (byte *)prevItem + prevItemLen*elsz;
			if ((byte *)v + elsz > (byte *)vEnd) goto eof;
		}
		*itemLen = 1;
		return(v);
	case FTN_BYTE:
	case FTN_CHAR:
	case FTN_BLOB:
	case FTN_INDIRECT:
	case FTN_BLOBI:
	case FTN_BLOBZ:
	case FTN_INTERNAL:
	default:				/* assume 1 item */
		if (prevItem != NULL)		/* single item already seen */
		{
		eof:
			*itemLen = 0;
			return(NULL);
		}
		*itemLen = n;
		return(v);
	}
}

char *
TXfldGetNextItemStr(fld, itemPtr, itemLen)
FLD	*fld;		/* (in) FLD */
void	**itemPtr;	/* (in/out) actual `fld' item; NULL if first call */
size_t	*itemLen;	/* (in/out) `*itemPtr' length */
/* Returns pointer to next item in multi-item `fld', as a string, and
 * sets `*itemPtr'/`*itemLen' to actual item and its length.  Call
 * again with returned `*itemPtr'/`*itemLen' values to get next  item.
 * Drills into FTN_INTERNAL.FTI_valueWithCooked type.
 * Returns NULL if no more items.
 * NOTE: Do not set or alter field between calls.  Copy return value
 * immediately, as it may be static fldtostr() data.
 * NOTE: should correspond with TXfldIsMultipleItemType(), TXfldNumItems(),
 * TXfldGetNextItem().
 */
{
	FTN		type;
	void		*v, *vEnd;
	size_t		n, size, elsz;
	ft_internal	*fti;
	TXftiValueWithCooked	*valueWithCooked;
	FLD		tmpFld;
	ft_strlst	slHdr;
	char		*s, *e, *slEnd;
	DDFD		*fd;
	FLD		itemFld;

	type = TXfldType(fld);
	v = getfld(fld, &n);
	size = fld->size;
	elsz = fld->elsz;
	if ((type & DDTYPEBITS) == FTN_INTERNAL &&
	    (fti = (ft_internal *)v) != NULL &&
	    tx_fti_gettype(fti) == FTI_valueWithCooked &&
	    (valueWithCooked = tx_fti_getobj(fti)) != NULL)
	{
		v = TXftiValueWithCooked_GetValue(valueWithCooked, &type, &n,
						  &size);
		fd = ftn2ddfd_quick(type, n);
		elsz = (fd ? fd->elsz : 1);
		fld = NULL;
	}
	vEnd = (byte *)v + size;

	/* NOTE: See also TXbtreeIsOnMultipleItemType(): */
	switch (type & DDTYPEBITS)
	{
	case FTN_STRLST:
		if (!fld)
		{
			memset(&tmpFld, 0, sizeof(FLD));
			tmpFld.type = type;
			tmpFld.v = v;
			tmpFld.elsz = TX_STRLST_ELSZ;
			tmpFld.n = n;
			tmpFld.size = size;
			s = TXgetStrlst(&tmpFld, &slHdr);
		}
		else
			s = TXgetStrlst(fld, &slHdr);
		if (s == CHARPN) goto eof;
		slEnd = s + slHdr.nb;
		if (slEnd > s && !slEnd[-1]) slEnd--;	/* list-term. nul */
		if (*itemPtr != NULL)
			s = (char *)(*itemPtr) + *itemLen + 1;
		if (s >= slEnd) goto eof;
		for (e = s; e < slEnd && *e; e++);
		*itemPtr = s;
		*itemLen = e - s;
		return(s);
	case FTN_DECIMAL:
	case FTN_DOUBLE:
	case FTN_DATE:
	case FTN_FLOAT:
	case FTN_INT:
	case FTN_INTEGER:
	case FTN_LONG:
	case FTN_SHORT:
	case FTN_SMALLINT:
	case FTN_WORD:
	case FTN_HANDLE:
	case FTN_DWORD:
	case FTN_COUNTER:
	case FTN_DATESTAMP:
	case FTN_TIMESTAMP:
	case FTN_DATETIME:
	case FTN_COUNTERI:
	case FTN_RECID:
	case FTN_INT64:
	case FTN_UINT64:
		/* Fixed-size-element types: */
		if (*itemPtr != NULL)
		{
			v = (byte *)(*itemPtr) + (*itemLen)*elsz;
			if ((byte *)v + elsz > (byte *)vEnd) goto eof;
		}
		/* Make a fake field with just the item, for fldtostr(): */
		memset(&itemFld, 0, sizeof(FLD));
		itemFld.type = (type & DDTYPEBITS);
		itemFld.v = v;
		itemFld.n = 1;
		itemFld.size = itemFld.elsz = 1;	/* WTF */
		*itemPtr = v;
		*itemLen = 1;
		/* note: we assume fldtostr() will not return a pointer
		 * to anything in `itemFld' for these types, as `itemFld'
		 * is local and destroyed on this return:
		 */
		return(fldtostr(&itemFld));
	case FTN_BYTE:
	case FTN_CHAR:
	case FTN_INDIRECT:
		if (*itemPtr) goto eof;		/* single item already seen */
		*itemPtr = v;
		*itemLen = n;
		return(v);
	case FTN_BLOB:
	case FTN_BLOBI:
	case FTN_BLOBZ:
	case FTN_INTERNAL:
	default:
		if (*itemPtr)			/* single item already seen */
		{
		eof:
			*itemLen = 0;
			return(NULL);
		}
		*itemPtr = v;
		*itemLen = n;
		if (!fld)
		{
			memset(&tmpFld, 0, sizeof(FLD));
			tmpFld.type = type;
			tmpFld.v = v;
			tmpFld.elsz = TX_STRLST_ELSZ;
			tmpFld.n = n;
			tmpFld.size = size;
			return(fldtostr(&tmpFld));
		}
		else
			return(fldtostr(fld));
	}
}

int
TXfldMoveFld(FLD *dest, FLD *src)
/* Moves `src' field type and data to `dest', freeing `dest' data
 * first.  `src' will have no type nor data afterwards.
 * Returns 0 on error.
 */
{
	releasefld(dest);
	*dest = *src;
	memset(src, 0, sizeof(FLD));
	return(1);
}

int
TXfldSetNull(FLD *fld)
/* Sets `fld' to SQL NULL, preserving type and virtualness.
 * Returns 0 on error.
 */
{
	TXfreefldshadownotblob(fld);
	fld->v = fld->shadow = NULL;
	fld->alloced = 0;
	fld->frees = 0;
	fld->n = fld->size = 0;
	return(1);
}

int
TXfldIsNull(FLD *fld)
/* Returns nonzero if `fld' is a SQL NULL fld, zero if not.
 */
{
	/* Could just call getfld() to ensure `fld->v' is set, for
	 * OOness (e.g. if virtual and `fld->v' not set yet).  But
	 * getfld()/mkvirtual() will always change `fld->v' if
	 * virtual, so we could be freeing an earlier getfld() value
	 * that the caller might've gotten.  Just look directly at
	 * `fld', to avoid disturbing `fld->v'.  (Also lets us call
	 * TXfldIsNull() in mkvirtual() without infinite recursion.)
	 */
	if (FLD_IS_VIRTUAL(fld))
	{
		int	i;

		/* Since a virtual field concatenates its components,
		 * any NULL component makes the field NULL:
		 */
		for (i = 0; i < fld->vfc; i++)
			if (fld->fldlist[i] && TXfldIsNull(fld->fldlist[i]))
				return(1);	/* NULL component: NULL fld */
		return(0);
	}
  if (FLD_IS_COMPUTED(fld) && !fld->v)
  {
    (void)getfld(fld, NULL);
  }
	return(fld->v ? 0 : 1);
}

/****************************************************************************/
/* TXfld_optimizeforin: (FLD *f)                                            */
/****************************************************************************/

int
TXfld_optimizeforin(FLD *f, DDIC *ddic)
{
	if(!f)
		return 0;
	if(f->issorted)
		return 0;
	if((f->type & DDTYPEBITS) == FTN_STRLST)
	{
		if(!ddic || ddic->optimizations[OPTIMIZE_SORTED_VARFLDS])
			TX_fldSortStringList(f);
	}
	return 0;
}
