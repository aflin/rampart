/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#if defined(__MSDOS__) && !defined(MSDOS)
#  define MSDOS __MSDOS__
#endif
#include "stdio.h"
#include <string.h>
#include "stdlib.h"
#ifdef unix
#  include <unistd.h>
#endif
#if defined(MSDOS)
#include "io.h"                                         /* for access() */
#endif
#include "sys/types.h"
#include <limits.h>
#include <sys/stat.h>
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"				/* for htsnpf() */
#include "httpi.h"
#ifdef JOHN
#include <fcntl.h>
#endif

/************************************************************************/

/* this is the list of valid field types. For each item in this list
there is automatically created a VAR version as well as a NON_NULL
version. The high two bits of the TYPE # are manipulated by the
look-up functions.

CAVEAT: if you point to one of these things, be aware that
the value in TYPE # is not constant!!!!!!!!

CAVEAT #2:  in the size field, use sizeof(byte) if you have a variable
size structure, and then use the actual size in putfld.  You will only
be able to use the var.... form.

KNG 20060220 Note that all type names must have "var" prefix, so that
it can be returned (or ignored) as part of the type name when needed.
*/

static DDFT ddtype[DDTYPES]=
{
/*
   NAME        , TYPE #        , SIZE OF 1 OF THIS TYPE
*/
  { "",         (FTN)0, 0 },                    /* type 0 */
#undef I
#define I(tok, name, size, nextLargerType, flags)	\
  { "var" name, FTN_##tok, size },
TX_FTN_SYMBOLS_LIST
#undef I
  { NULL, 0, 0 }                                /* end of list */
};

const TXFTNF	TXftnFlags[DDTYPEBITS + 1] =
  {
    (TXFTNF)0,                                  /* no FTN value 0 */
#undef I
#define I(tok, name, size, nextLargerType, flags)	flags,
TX_FTN_SYMBOLS_LIST
#undef I
  };

const FTN	TXftnNextLargerType[DDTYPEBITS + 1] =
  {
    (FTN)0,                                     /* no FTN value 0 */
#undef I
#define I(tok, name, size, nextLargerType, flags)	nextLargerType,
TX_FTN_SYMBOLS_LIST
#undef I
  };


/************************************************************************/
/* free a dd */

DD *
closedd(dd)
DD	*dd;
{
	dd = TXfree((void *)dd);
	return(DDPN);
}

/************************************************************************/
/* create a new dd

   This is the old style call.  It will create a table with only DDFIELDS
   available slots.  Hence the table size is still limited here.
*/

DD *
opendd()
{
	return opennewdd(-1);
}

/************************************************************************/
/* create a new dd

   This is the newer create function, which takes an argument specifying
   how many fields to create.  It the number of fields is less than one
   it will use a default value.
*/

DD *
opennewdd(maxfields)
int	maxfields;
{
	static CONST char	fn[] = "opennewdd";
	DD *dd;
	size_t	nsz;

	if(maxfields < 1)
		maxfields = DDFIELDS;
	nsz = sizeof(DD) + (maxfields*sizeof(DDFD));
	dd = (DD *)TXcalloc(TXPMBUFPN, fn, 1, nsz);
	if(dd!=DDPN)
	{
		dd->magic = DD_MAGIC;
		dd->version = DD_CURRENT_VERSION;
		dd->size = nsz;
		dd->slots = maxfields + 1;
		dd->n=0;
		dd->varpos=0;
		dd->ivar=0;
		dd->blobs=0;
		dd->tbltype=0;
	}
	return(dd);
}

DD *
TXdupDd(dd)
DD	*dd;
/* Returns duplicate copy of `dd'.
 */
{
	static CONST char	fn[] = "TXdupDd";
	DD			*newDd;

	newDd = (DD *)TXcalloc(TXPMBUFPN, fn, 1, dd->size);
	if (!newDd) return(NULL);
	memcpy(newDd, dd, dd->size);
	return(newDd);
}

/************************************************************************/
/* finds the field type when given a type string, and returns a ptr to it
   or NULL if invalid */

DDFT *
getddft(type)
char	*type;
{
	DDFT	*tp;
	int	var=0;

	if(!strncmp(type,"var",3))	/* Do we want the var version? */
	{
		var=1;
		type+=3;
	}
	// -ajf 2025-11-19: vec always var.
	if(!strncmp(type,"vec",3))
		var=1;
	for (tp = ddtype; tp->name; tp++)
		if (*tp->name && strcmp(type, tp->name + 3) == 0)
		{                  /* see I told you these weren't constant! */
			if(var) tp->type|= DDVARBIT;
			else    tp->type&=~DDVARBIT;
			return(tp);
		}
	if (strncmp(type, "internal:", 9) == 0)
	{					/* caller looks up subtype */
		tp = &ddtype[FTN_INTERNAL];
		if(var) tp->type|= DDVARBIT;
		else    tp->type&=~DDVARBIT;
		return(tp);
	}
	return(DDFTPN);
}

/************************************************************************/
/*
 * returns a pointer to the  DDFT that is referenced by typen
 */

DDFT *ddftype ARGS((int));
DDFT *
ddftype(typen)
int	typen;
{
	DDFT	*tp;
	unsigned	tn = typen & DDTYPEBITS;

	tp = ddtype + tn;
	if (tp->name && *tp->name)
	{
		tp->type = typen;
		return(tp);
	}
	return(DDFTPN);
}

/************************************************************************/
/*
 * Returns size of type.  Returns -1 on error.
 */

size_t
ddftsize(typen)
int	typen;
{
	DDFT	*tp;
	unsigned	tn = typen & DDTYPEBITS;

	tp = ddtype + tn;
	if (tp->name && *tp->name)
		return tp->size;
	return -1;
}

/************************************************************************/
/*
 * returns a pointer to the typename that is referenced by typen
 * Note that this will include the 'var' if needed
 */

char *
ddfttypename(typen)
int	typen;
{
	DDFT	*tp;
	unsigned	tn = typen & DDTYPEBITS;

	tp = ddtype + tn;
	if (tp->name && *tp->name)
	{
		tp->type=typen;
		return(tp->name + ((tp->type & DDVARBIT) ? 0 : 3));
	}
	return (char *)NULL;
}

/******************************************************************/

CONST char *
TXfldtypestr(fld)
FLD	*fld;
/* Returns a (static) pointer to a type name for `fld'.
 * Will include "var" prefix and/or ":subtype" suffix if applicable.
 * Thread-unsafe.
 */
{
  static int    i = 0;
  static char   name[2][128];
  char          *s, *d, *tn;
  CONST char    *st;
  ft_internal   *fti;
  size_t        sz;

  if ((tn = ddfttypename(fld->type)) != CHARPN)
    {
      if ((fld->type & DDTYPEBITS) == FTN_INTERNAL &&
          (fti = (ft_internal *)getfld(fld, NULL)) != ft_internalPN &&
          TX_FTI_VALID(fti))
        {
          s = d = name[i];
          i = (i + 1) % (sizeof(name)/sizeof(name[0]));
          strcpy(d, tn);
          sz = strlen(d);
          d += sz;
          *(d++) = ':';
          sz++;
          st = tx_fti_type2str(tx_fti_gettype(fti));
          TXstrncpy(d, st, sizeof(name[0]) - sz);
          return(s);
        }
      return(tn);
    }
  s = d = name[i];
  i = 1 - i;
  sprintf(d, "[%d]", fld->type);
  return(s);
}

/******************************************************************/

static int addtodd ARGS((DD *, DDFD *, char *));

static int
addtodd(dd, fd, name)
DD	*dd;
DDFD	*fd;
char	*name;
{
	int	i;

	if(dd->n+1 >= dd->slots)         /* We have no more room at the inn */
	{
		return(0);
	}

	for(i=0;i<dd->n;i++)       /* make sure name does not already exist */
		if(!strncmp(dd->fd[i].name,name, DDNAMESZ-1))
			return(0);

	fd->num = dd->n;	/* Which numbered insert is it */
	fd->order = 0;		/* By default use ascending order */
	if(fd->type&DDVARBIT)
	{
		fd->pos=dd->n - dd->ivar;
			/* position number in var fields */
		dd->fd[dd->n]=*fd;
		TXstrncpy(dd->fd[dd->n].name,name, DDNAMESZ);
#ifdef NEVER
		dd->fd[dd->n].name[DDNAMESZ-1] = '\0';
#endif
			/* give it the new name */
	}
	else
	{
		int	j;
		size_t	pos;

		dd->varpos+=fd->size;        /* push the variable fields over */
		for(j=dd->n;j>dd->ivar;j--)  /* insert a hole before var fd's */
			dd->fd[j]=dd->fd[j-1];
		for(pos=j=0;j<dd->ivar;j++)/* get its position within the rec */
			pos+=dd->fd[j].size;
		fd->pos=pos;
		dd->fd[dd->ivar]=*fd;                  /* copy it in */
		TXstrncpy(dd->fd[dd->ivar].name,name, DDNAMESZ);
				  /* give it new name */
#ifdef NEVER
		dd->fd[dd->ivar].name[DDNAMESZ - 1] = '\0';
#endif
		dd->ivar+=1;
			/* raise the var field index by one */
	}
	dd->n+=1;               /* inc the number of fields */
	return dd->n;
}

/******************************************************************/

static int ddft2ddfd ARGS((DDFT *, int, int, char *, DDFD *));

static int
ddft2ddfd(DDFT *tp, int n, int nonull, char *name, DDFD *fd)
{
	if (!fd)
		return -1;

	memset(fd, 0, sizeof(DDFD));
	TXstrncpy(fd->name,name,DDNAMESZ);
	fd->type = (tp->type | (nonull ? FTN_NotNullableFlag : 0));

	/*
	size imposes no limitations on the variable length fields.
	It is only used to provide an initial buffer size for the
	field when the table is being read into memory.
	gettbl() will automatically resize a buffer that is too small
	to hold an incoming field.
	*/

	fd->size=(size_t)n*tp->size;  /* size in bytes */
	/* KNG 20081113 sanity check: strlst has a min size due to struct;
	 * otherwise malloc(fd->size) later for putfld() will under-alloc:
	 */
	if ((fd->type & DDTYPEBITS) == FTN_STRLST &&
	    fd->size < (size_t)TX_STRLST_MINSZ)
		fd->size = TX_STRLST_MINSZ;

	fd->elsz=(size_t)tp->size;    /* size of one element */
	return 0;
}

TXbool
TXftnToDdfdQuick(FTN type, size_t n, DDFD *fd)
/* Quick fakery to help init a field fast.  KNG 981111
 * Sets `*fd'.
 * Returns TXbool_False on error.
 */
{
  DDFT	*ft;

  memset(fd, 0, sizeof(DDFD));
  if ((ft = ddftype(type)) == (DDFT *)NULL) return(TXbool_False);
  fd->type = type;
  fd->size = n*ft->size;
  fd->elsz = ft->size;
  return(TXbool_True);
}

/******************************************************************/

int
getddfd(char *type, int n, int nonull, char *name, DDFD *ddfd)
{
	DDFT	*tp;

	tp = getddft(type);	/* Get the base type */
	if(!tp)
		return -1;
	return ddft2ddfd(tp, n, nonull, name, ddfd);
}

/******************************************************************/

int
getddfdnum(int type, int n, int nonull, char *name, DDFD *ddfd)
{
	DDFT	*tp;

	tp = ddftype(type);	/* Get the base type */
	if(!tp)
		return -1;
	return ddft2ddfd(tp, n, nonull, name, ddfd);
}

/******************************************************************/

DDFD *
closeddfd(ddfd)
DDFD	*ddfd;
{
	ddfd = TXfree(ddfd);
	return NULL;
}

/************************************************************************/
/*
 * adds a new field to an existing DD.
 * returns 0 on error or number of fields if ok
 */

int
putdd(dd,name,type,n,nonull)
DD   *dd;                                 /* The DD to add the field to */
char *name;                      /* name of the variable being declared */
char *type;                                    /* type of this variable */
int   n;           /* max or number of elements depending on var prefix */
int   nonull;                               /* is this a non null field */
{
	DDFD	fd;
	int	rc = 0;
	const char	*s;

	if((0 == getddfd(type, n, nonull, name, &fd)))
	{
		s = type;
		/* Bug 4390: "varblob" is blob too: */
		if (strnicmp(s, "var", 3) == 0) s += 3;
		if(!strnicmp(s, "blob", 4)) /* Any type beginning with */
			dd->blobs=1;          /* blob is a blob */

		rc = addtodd(dd, &fd, name);
	}
	return rc;
}

/************************************************************************/
/* copy's the data def for a field from another table into a new DD */
/* Should probably be written to use putdd, as it borrows heavily wtf */

int
copydd(dd,name,table,oname,novar)            /*  destination  ,  source */
DD   *dd;                             /* the dd you're putting it in to */
char *name;                      /* name of the variable being declared */
TBL  *table;                            /* the table we're copying from */
char *oname;                  /* the name of the variable we're copying */
int   novar;                                      /* Don't allow VARBIT */
{
	static CONST char	fn[] = "copydd";
	int	i;
	DD	*odd=table->dd;
	DDFD	fd;
	char	*fname;
        char    *tname;
	int	freefname = 0;

	if(strchr(oname, '\\'))
	{
		fname = TXstrdup(TXPMBUFPN, fn, oname);
		freefname = 1;
		strtok(fname, "\\");
	} else if ((strstr(oname, ".$.")) || (strstr(oname, ".$[")))
        {
		fname = TXstrdup(TXPMBUFPN, fn, oname);
		freefname = 1;
                tname = strstr(fname, ".$.");
                if (!tname)
                   tname = strstr(fname, ".$[");
                if(tname)
                  *tname = '\0';

        } else
	{
		fname = oname;
	}
	for(i=0;i<odd->n;i++)          /* get the fd from the original */
	{
		if(!strcmp(odd->fd[i].name,fname))
		{
			fd=odd->fd[i];               /* copy the struct */

			if(novar)
				fd.type &= ~DDVARBIT;
			if(freefname)
				fname = TXfree(fname);
			return addtodd(dd, &fd, name);
		}
	}
	if(freefname)
		fname = TXfree(fname);
	return(0);
}

/************************************************************************/
/*	This allows the user to add a new type to the server.  It does
 *	require an argument of type number.  If no other type is using
 *	this number, the current data will be added.
 *
 *	If the same type has the same number it still returns success.
 */

int
dbaddtype(name, number, size)
char	*name;	/* Name of the type */
int	number;	/* Number for this type.  (32-63) */
int	size;	/* Bytes occupied by one of this type */
{
	static CONST char	fn[] = "dbaddtype";
	int i;

	if((number > DDTYPEBITS) || (number < 32))
	{
		putmsg(MWARN, NULL,
			"Invalid type number.  Must be in the range 32-%d",
			DDTYPEBITS);
		return -1;
	}
	i = number;
	{
		if (ddtype[i].type == number)
		{
			if (ddtype[i].size == size &&
			    !strcmp(name, ddtype[i].name + 3))
				return 0;
			else
				return -1;
		}
		if (ddtype[i].type == 0 &&
		    (!ddtype[i].name || ddtype[i].name[0] == '\0'))
		{
			ddtype[i].name = (char *)TXmalloc(TXPMBUFPN, fn, strlen(name) + 4);
			strcpy(ddtype[i].name, "var");
			strcpy(ddtype[i].name + 3, name);
			ddtype[i].type = number ;
			ddtype[i].size = size;
			return 0;
		}
	}
	return -1;
}

/************************************************************************/
/* Get the position of the field that was added nth */

int
ddgetorign(dd, n)
DD	*dd;
int	n;	/* field number, in user-added (not actual/internal) order */
{
	int	i;

	for (i=0; i < dd->n; i++)
		if(dd->fd[i].num == n)
			return i;
	return -1;
}

/******************************************************************/

int
ddfindname(dd, fname)
DD	*dd;
char	*fname;
{
	int	i;

	for (i=0; i < dd->n; i++)
	{
		DBGMSG(1, (999, NULL, "Comparing %s and %s", dd->fd[i].name, fname));
		if(!strncmp(dd->fd[i].name, fname, DDNAMESZ-1))
			return i;
	}
	return -1;
}

/******************************************************************/

int
ddsetordern(dd, fname, order)
DD		*dd;
const char	*fname;
TXOF		order;
{
	int	i;

	for (i=0; i < dd->n; i++)
		if(!strcmp(dd->fd[i].name, fname))
		{
			dd->fd[i].order = order;
			return 0;
		}
	return -1;
}

int
TXddSetOrderFlagsByIndex(DD *dd, size_t index, TXOF orderFlags)
/* Returns 0 on error.
 */
{
	if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(index) ||
	    index >= (size_t)dd->n)
		return(0);
	dd->fd[index].order = orderFlags;
	return(1);
}

/******************************************************************/

char	*
ddgetname(dd, n)
DD	*dd;
int	n;
{
	if(n < 0 || n > dd->n)
		return NULL;
	return dd->fd[n].name;
}

/******************************************************************/

int
ddgetnum(dd, n)
DD	*dd;
int	n;
{
	if(n < 0 || n > dd->n)
		return -1;
	return dd->fd[n].num;
}

/******************************************************************/

DD *
TXexpanddd(odd, n)
DD *odd;
int n;
{
	static CONST char	fn[] = "TXexpanddd";
	DD *dd = NULL;
	size_t nsz;
	int i;

	if(odd->magic == (int)DD_MAGIC) /* Already valid */
	{
		nsz = sizeof(DD) + ((odd->n-1+n)*sizeof(DDFD));
		dd = (DD *)TXcalloc(TXPMBUFPN, fn, 1, nsz);
		if(!dd)
			return dd;
		dd->magic    = odd->magic;
		dd->version  = DD_CURRENT_VERSION;
		dd->size     = nsz;
		dd->slots    = odd->n + 1 + n;
		dd->n       = odd->n;
		dd->varpos  = odd->varpos;
		dd->ivar    = odd->ivar;
		dd->blobs   = odd->blobs;
		dd->tbltype = odd->tbltype;
		for(i=0; i < dd->n; i++)
			dd->fd[i] = odd->fd[i];

		return dd;
	}
	return NULL;
}

/******************************************************************/

DD	*
convertdd(odd, sz)
void	*odd;
size_t	sz;
/* Returns alloc'd new DD.
 */
{
	static CONST char	fn[] = "convertdd";
	DD	*dd = NULL, *dd0;
	ODD	*dd1;
	NDD	*dd2;
	int	i, j;
	size_t	nsz, xsz;

	if(((DD *)odd)->magic == (int)DD_MAGIC) /* Already valid */
	{
		dd0 = odd;
		nsz = sizeof(DD) + ((dd0->n-1)*sizeof(DDFD));
		dd = (DD *)TXcalloc(TXPMBUFPN, fn, 1, nsz);
		if (!dd) goto err;
		dd->magic    = dd0->magic;
		dd->version  = DD_CURRENT_VERSION;
		dd->size     = nsz;
		dd->slots    = dd0->n + 1;
		dd->n       = dd0->n;
		dd->varpos  = dd0->varpos;
		dd->ivar    = dd0->ivar;
		dd->blobs   = dd0->blobs;
		dd->tbltype = dd0->tbltype;
		xsz = (sizeof(DD)-sizeof(DDFD) + (dd0->slots * sizeof(ODDFD)));
		if(xsz == sz)
		{
			ODDFD	*oddf;

			oddf = (ODDFD *)&dd0->fd[0];
			for(i=0; i < dd->n; i++)
			{
				memcpy(&dd->fd[i], &oddf[i], sizeof(ODDFD));
				if(TXisblob(dd->fd[i].type))
				{
					dd->fd[i].sttype = dd->fd[i].type;
					dd->fd[i].stelsz = dd->fd[i].elsz;
					dd->fd[i].stsize = dd->fd[i].size;
					dd->fd[i].type = FTN_BLOBI | DDVARBIT;
					dd->fd[i].elsz = sizeof(ft_blobi);
					dd->fd[i].size = sizeof(ft_blobi);
				}
			}
		}
		else
		{
			for(i=0; i < dd->n; i++)
			{
				dd->fd[i] = dd0->fd[i];
				if(TXisblob(dd->fd[i].type))
				{
					dd->fd[i].sttype = dd->fd[i].type;
					dd->fd[i].stelsz = dd->fd[i].elsz;
					dd->fd[i].stsize = dd->fd[i].size;
					dd->fd[i].type = FTN_BLOBI | DDVARBIT;
					dd->fd[i].elsz = sizeof(ft_blobi);
					dd->fd[i].size = sizeof(ft_blobi);
				}
			}
		}
	}
	else if ((sz == sizeof(NDD)) || (sz == sizeof(ODD)))
	{
		dd1 = odd;
		dd2 = odd;
		nsz = sizeof(DD) + ((dd1->n-1)*sizeof(DDFD));
		dd = (DD *)TXcalloc(TXPMBUFPN, fn, 1,
					sizeof(DD) + (dd1->n*sizeof(DDFD)));
		if (!dd) goto err;
		dd->magic    = DD_MAGIC;
		dd->version  = DD_CURRENT_VERSION;
		dd->size     = nsz;
		dd->slots    = dd1->n + 1;
		dd->n       = dd1->n;
		dd->varpos  = dd1->varpos;
		dd->ivar    = dd1->ivar;
		dd->blobs   = dd1->blobs;
		if(sz == sizeof(NDD))
			dd->tbltype = dd2->tbltype;
		else
			dd->tbltype = 0;
		for(i=0; i < dd->n; i++)
			memcpy(&dd->fd[i], &dd1->fd[i], sizeof(ODDFD));
	}
#ifndef NO_HAVE_DDBF
	else if ((*((char *)odd) == 3) || (*((char *)odd) == (char)0x8B))
	{
		int	x;
		int	n;
		byte	b2;
		char	sttype;
		byte	stsize;
		char	*fname;

		dd = opennewdd(sz/32);

		x = 32;
		dd->blobs = *((char *)odd) == (char)0x8B ? 1 : 0;
		while ((size_t)(x + 32) < sz)
		{
			sttype = *((char *)odd + x + 11);
			stsize = *((byte *)odd + x + 16);
			fname  = (char *)odd + x;
			switch(sttype)
			{
				case 'C':
				{
					n = putdd(dd,fname,"varchar",stsize,1);
					j = ddgetorign(dd, n-1);
					dd->fd[j].sttype = FTN_CHAR;
					dd->fd[j].stsize = stsize;
					dd->fd[j].stelsz = 1;
					break;
					n = putdd(dd, fname, "char", stsize, 1);
					break;
				}
				case 'D':
				{
					n = putdd(dd, fname, "date", 1, 1);
					j = ddgetorign(dd, n-1);
					dd->fd[j].sttype = FTN_CHAR;
					dd->fd[j].stsize = stsize;
					dd->fd[j].stelsz = 1;
					break;
				}
				case 'F':
				case 'N':
				{
					b2 = *((byte *)odd+x+17);
					if(b2 == 0)
					{
						n = putdd(dd,fname,"int",1,1);
					}
					else
					{
						n = putdd(dd,fname,"float",1,1);
					}
					j = ddgetorign(dd, n-1);
					dd->fd[j].sttype = FTN_CHAR;
					dd->fd[j].stsize = stsize;
					dd->fd[j].stelsz = 1;
					break;
				}
				case 'L':
				{
					n = putdd(dd, fname, "char", stsize, 1);
					break;
				}
				case 'M':
				{
					n = putdd(dd, fname, "varchar", 256, 1);
					j = ddgetorign(dd, n-1);
					dd->fd[j].sttype = FTN_CHAR;
					dd->fd[j].stsize = stsize;
					dd->fd[j].stelsz = 1;
					break;
				}
				default:
				{
					putmsg(MERR, fn, "Invalid field type (0x%02x)", (int)sttype);
					break;
				}
			}
			x += 32;
		}
		dd->tbltype = 2;
	}
#endif /* HAVE_DDBF */
	else
		goto err;
	goto finally;

err:
	dd = closedd(dd);
finally:
	return(dd);
}

/******************************************************************/

int
TXisblob(int ftype)
{
	FTN ftntype;

	ftntype = ftype & DDTYPEBITS;
	switch(ftntype)
	{
	case FTN_BLOB:
	case FTN_BLOBZ:
		return 1;
	default:
		return 0;
	}
}

/******************************************************************/

/*
        Create a new DD with BLOBI instead of BLOB.  Typically
        used for in memory versions of tables etc.
*/

DD *
TXbddc(dd)
DD *dd;
{
        static CONST char Fn[] = "bddc";
        DD      *rdd;
        int     i;
        int     nfields;
        size_t  rddsz;

        rddsz = dd->size;
        rdd = (DD *)TXcalloc(TXPMBUFPN, Fn, 1, rddsz);
        if (rdd == (DD *)NULL) return(rdd);
        memcpy(rdd, dd, rddsz);
        nfields=ddgetnfields(dd);
        for (i=0; i < nfields; i++)
        {
		FTN origtype;

		origtype = ddgetftype(rdd, i) & DDTYPEBITS;

		switch(origtype)
		{
		case FTN_BLOB:
		case FTN_BLOBZ:
                        ddsetftype(rdd, i, FTN_BLOBI | DDVARBIT);
                        rdd->fd[i].elsz = 1;
			break;
		default:
			break;
                }
        }
        (void)ddsettype(rdd, TEXIS_NULL1_TABLE);
        return rdd;
}

/******************************************************************/
/*
        Create a new DD with BLOB instead of BLOBI.  Typically
        used for on disk versions of tables, e.g. triggers  etc.
*/

DD *
TXbiddc(dd)
DD *dd;
{
        static CONST char Fn[] = "bddc";
        DD      *rdd;
        int     i;
        int     nfields;
        size_t  rddsz;

        rddsz = dd->size;
        rdd = (DD *)TXcalloc(TXPMBUFPN, Fn, 1, rddsz);
        if (rdd == (DD *)NULL) return(rdd);
        memcpy(rdd, dd, rddsz);
        nfields=ddgetnfields(dd);
        for (i=0; i < nfields; i++)
        {
                if ((ddgetftype(rdd, i) & DDTYPEBITS) == FTN_BLOBI)
                {
                        ddsetftype(rdd, i, FTN_BLOB);
                        rdd->fd[i].elsz = sizeof(ft_blob);
			rdd->fd[i].size = sizeof(ft_blob); /* Bug 4390 */
			rdd->fd[i].sttype = 0;
			rdd->fd[i].stelsz = 0;
			rdd->fd[i].stsize = 0;
                }
        }
#if defined(NULLABLE_TEXIS_TABLE) && defined(NEVER) /* Why would we do this? */
        (void)ddsettype(rdd, TEXIS_NULL1_TABLE);
#else
        (void)ddsettype(rdd, TEXIS_FAST_TABLE);
#endif
        return rdd;
}

char *
TXorderFlagsToStr(TXOF orderFlags, int verbose)
/* Returns alloced string.
 */
{
	HTBUF	*buf = NULL;
	char	*ret;

	if (!(buf = openhtbuf())) goto err;
	htbuf_write(buf, "", 0);		/* ensure non-NULL if empty */
#define OPT_SPACE	\
	{ if (htbuf_getdatasz(buf) > 0) htbuf_write(buf, " ", 1); }

	if (orderFlags & OF_DESCENDING)
	{
		OPT_SPACE;
		htbuf_pf(buf, "desc");
		orderFlags &= ~OF_DESCENDING;
	}
	else if (verbose)
	{
		OPT_SPACE;
		htbuf_pf(buf, "asc");
	}
	if (orderFlags & OF_IGN_CASE)
	{
		OPT_SPACE;
		htbuf_pf(buf, "ignCase");
		orderFlags &= ~OF_IGN_CASE;
	}
	if (orderFlags & OF_DONT_CARE)
	{
		OPT_SPACE;
		htbuf_pf(buf, "dontCare");
		orderFlags &= ~OF_DONT_CARE;
	}
	if (orderFlags & OF_PREFER_END)
	{
		OPT_SPACE;
		htbuf_pf(buf, "preferEnd");
		orderFlags &= ~OF_PREFER_END;
	}
	if (orderFlags & OF_PREFER_START)
	{
		OPT_SPACE;
		htbuf_pf(buf, "preferStart");
		orderFlags &= ~OF_PREFER_START;
	}
	if (orderFlags)
	{
		OPT_SPACE;
		htbuf_pf(buf, "OF_0x%x",
			    (int)orderFlags);
	}
	htbuf_getdata(buf, &ret, 0x3);
	goto finally;

err:
	ret = NULL;
finally:
	buf = closehtbuf(buf);
	return(ret);
#undef OPT_SPACE
}

/******************************************************************/

char *
TXddSchemaToStr(dd, orderToo)
CONST DD	*dd;		/* (in) schema to print */
int		orderToo;	/* (in) 1: print DESC etc. 2: print ASC too */
/* Prints schema of `dd' in human-readable form, as-created order,
 * e.g. `(column1 type(size), column2 type(size), ...)'.
 * Returns alloced string.
 */
{
	HTBUF		*buf = NULL;
	CONST DDFD	*fdFixed, *fdFixedEnd, *fdVar, *fdVarEnd, *fd;
	int		colCreatedIdx, n;
	const char	*quote;
	char		*ret;

	if (!(buf = openhtbuf())) goto err;
	htbuf_pf(buf, "(");
	fdFixed = dd->fd;			/* fixed-size fields first */
	fdFixedEnd = fdVar = dd->fd + dd->ivar;	/* followed by variable */
	fdVarEnd = dd->fd + dd->n;
	for (colCreatedIdx = 0; colCreatedIdx < dd->n; colCreatedIdx++)
	{					/* iterate in created order */
		if (fdFixed < fdFixedEnd &&	/* have fixed fields left & */
		    (fdVar >= fdVarEnd ||	/* (no more var fields or */
		     fdFixed->num < fdVar->num))/*  fixed field is next) */
			fd = fdFixed++;
		else
			fd = fdVar++;
		if (colCreatedIdx > 0) htbuf_pf(buf, ", ");
		quote = (fd->name[strcspn(fd->name, " ,()")] ? "\"" : "");
		htbuf_pf(buf, "%s%s%s %s", quote, fd->name,
			 quote, ddfttypename(fd->type));
		/* Print the number of elements, if appropriate: */
		n = (fd->size / (fd->elsz ? fd->elsz : 1));
		switch (fd->type & DDTYPEBITS)
		{
		case FTN_STRLST:
		case FTN_BLOB:
		case FTN_BLOBI:
		case FTN_BLOBZ:
		case FTN_INTERNAL:
		case FTN_INDIRECT:
			break;
		default:
			if ((fd->type & DDVARBIT) || n != 1)
				htbuf_pf(buf, "(%d)", n);
			break;
		}
		/* Print order flags, if requested: */
		if (orderToo)
		{
			char	*flagsStr;

			htbuf_pf(buf, " ");
			flagsStr = TXorderFlagsToStr(fd->order, (orderToo>=2));
			if (!flagsStr || !*flagsStr)
				/* undo space if no print */
				htbuf_addused(buf, -1);
			else
				htbuf_pf(buf, "%s", flagsStr);
			flagsStr = TXfree(flagsStr);
		}
	}
	htbuf_pf(buf, ")");
	htbuf_getdata(buf, &ret, 0x3);
	goto finally;

err:
	ret = NULL;
finally:
	buf = closehtbuf(buf);
	return(ret);
}

size_t
TXddPrintFieldNames(buf, bufSz, dd)
char		*buf;		/* (out) buffer to write to */
EPI_SSIZE_T	bufSz;		/* (in) size of `buf' */
DD		*dd;		/* (in) DD to print */
/* Prints field names of `dd' in creation order, comma-separated.
 * Nul-terminates `buf' if `bufSz > 0'.
 * Returns number of characters that would be printed to `buf' (sans nul);
 * if > `bufSz', does not write past `bufSz' chars.
 */
{
	int	numFlds, i, orgIdx;
	char	*d, *e, *fldName;

	d = buf;
	e = buf + bufSz;
	numFlds = ddgetnfields(dd);
	for(i = 0; i < numFlds; i++)
	{
		if (i)
		{
			if (d < e) *d = ',';	d++;
			if (d < e) *d = ' ';	d++;
		}
		orgIdx = ddgetorign(dd, i);	/* in creation order */
		fldName = ddgetname(dd, orgIdx);
		if (d < e) TXstrncpy(d, fldName, e - d);
		d += strlen(fldName);
	}
	if (d < e) *d = '\0';
	else if (bufSz > 0) buf[bufSz - 1] = '\0';
	return(d - buf);
}

int
TXddOkForTable(TXPMBUF *pmbuf, DD *dd)
/* Returns 1 if `dd' is ok for its table type, 0 (and yaps) if not.
 */
{
	size_t	i;

	for (i = 0; i < (size_t)dd->n; i++)
	{
		switch (dd->fd[i].type & DDTYPEBITS)
		{
		case FTN_INTERNAL:
		case FTN_BLOBI:
		case FTN_COUNTERI:
		notSupported:
			txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN,
			       "Field type `%s' not supported in tables",
			       ddfttypename(dd->fd[i].type));
			goto err;
			/* Bug 4031: do not allow blob in user RAM
			 * tables until ft_blobi fully supported.
			 * This is checked here instead of in
			 * createtbl() etc. because we do allow blob
			 * in internal RAM DBFs (e.g. for in/out
			 * RAM-DBF TBL for an on-disk table):
			 */
		case FTN_BLOB:
			/* varblob not really defined yet; disallow: */
			if (dd->fd[i].type & DDVARBIT) goto notSupported;
			if (dd->tbltype == TEXIS_RAM_TABLE &&
			    !TXApp->allowRamTableBlob)
			{
				txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN,
		      "Field type `%s' not currently supported in RAM tables",
				       ddfttypename(dd->fd[i].type));
				goto err;
			}
			break;
		default:
			break;
		}
	}
	return(1);				/* success/ok */
err:
	return(0);				/* failure/unapproved */
}
