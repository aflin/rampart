#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"

/* wtf - these should be dynamic allocted strings that
         can grow as needed, and keep track of end so not need strcat() */
static char tbuf[MAXINSZ];

static	char	*tempbuf, *curpos;
static	size_t	bufsz, buflen;
int     TxDispPredParen = 0;

/******************************************************************/

static int
addstr(const char *x, size_t maxsz)
{
	static	char Fn[] = "addstr";
	size_t	sl;
	char	*newbuf;

	if(!x)
		return 0;
	if(!tempbuf)
	{
		tempbuf = malloc(MAXINSZ);
		if(!tempbuf)
		{
			putmsg(MWARN+MAE, Fn, "Out of memory");
			return -1;
		}
		curpos = tempbuf;
		*tempbuf = '\0';
		buflen = 0;
		bufsz = MAXINSZ;
	}
	sl = strlen(x);
	while((buflen + sl + 1) > bufsz)
	{
		if(tempbuf == tbuf)
		{
			putmsg(MWARN+MAE, Fn, "Out of memory");
			return -1;
		}
		bufsz += MAXINSZ;
		newbuf = realloc(tempbuf, bufsz);
		if(!newbuf)
		{
			putmsg(MWARN+MAE, Fn, "Out of memory");
#ifdef EPI_REALLOC_FAIL_SAFE
			if (tempbuf) free(tempbuf);
#endif /* EPI_REALLOC_FAIL_SAFE */
			tempbuf = CHARPN;
			buflen = 0;
			bufsz = 0;
			return -1;
		}
		tempbuf = newbuf;
		curpos = tempbuf + buflen;
	}
	strcpy(curpos, x);
	curpos += sl;
	buflen += sl;
	if(maxsz && (buflen > maxsz))
		return -1;
	return 0;
}

/******************************************************************/

static int
showop(QNODE_OP op, size_t maxsz)
{
	char	unkBuf[100];

  	switch(op)
	{
		/* TXqnodeOpToStr() prints a string for `op', but
		 * XML-safe; we want SQL-like for some ops:
		 */
#ifndef NEVER   /* WTF Why blocked out? */
		case FOP_LT :
			return addstr(" < ", maxsz);
			break;
		case FOP_LTE :
			return addstr(" <= ", maxsz);
			break;
		case FOP_GT :
			return addstr(" > ", maxsz);
			break;
		case FOP_GTE :
			return addstr(" >= ", maxsz);
			break;
		case FOP_EQ :
			return addstr(" = ", maxsz);
			break;
		case FOP_NEQ :
			return addstr(" != ", maxsz);
			break;
		case FOP_ADD :
			return addstr(" + ", maxsz);
			break;
		case FOP_SUB :
			return addstr(" - ", maxsz);
			break;
		case FOP_MUL :
			return addstr(" * ", maxsz);
			break;
		case FOP_DIV :
			return addstr(" / ", maxsz);
			break;
		case FOP_MOD :
			return addstr(" % ", maxsz);
			break;
		case FOP_INTERSECT_IS_EMPTY :
			return addstr(" INTERSECT IS EMPTY WITH ", maxsz);
			break;
		case FOP_INTERSECT_IS_NOT_EMPTY :
			return addstr(" INTERSECT IS NOT EMPTY WITH ", maxsz);
			break;
		case FOP_IS_SUBSET :
			return addstr(" IS SUBSET OF ", maxsz);
			break;
		case LIST_OP :
		case FOP_CNV :
			return addstr(", ", maxsz);
			break;
#endif /* NEVER */
		case REG_FUN_OP :
		case AGG_FUN_OP :
			return addstr("(", maxsz);
			break;
		case 0:
			break;
		default:
			if (addstr(" ", maxsz) < 0) goto err;
			if (addstr(TXqnodeOpToStr(op, unkBuf, sizeof(unkBuf)),
				   maxsz) < 0)
				goto err;
			if (addstr(" ", maxsz) < 0) goto err;
			break;
	}
	return 0;
err:
	return(-1);
}

/******************************************************************/
/*	Produce an ASCII dump of a predicate.  Useful mainly for
 *	debug.
 */

static int idisppred ARGS((PRED *, size_t maxsz));

static int
idisppred(PRED *p, size_t maxsz)
{
	int rc = 0;
	FLD	*fld;
	const char	*s;

	if (!p)
		return 0;
	if (TxDispPredParen && p->op != 0)
	{
		rc = addstr("(", maxsz);
		if (rc == -1)
			return -1;
	}
	if (p->op == FOP_CNV)
	{
		rc = addstr("CONVERT(", maxsz);
	}
	if (p->op == FOP_OR)
	{
		rc = addstr("(", maxsz);
	}
#ifdef _WIN32
	if (p->op == SELECT_OP)
	{
		rc = addstr("(SELECT ", maxsz);
	}
	if (p->op == PROJECT_OP)
	{
		rc = addstr("(PROJECT ", maxsz);
	}
#endif /* _WIN32 */
	if (rc == -1)
		return -1;
	switch (p->lt)
	{
		case 'P' :
			rc = idisppred(p->left, maxsz);
			break;
		case FIELD_OP :
			rc = addstr(fldtostr(p->left), maxsz);
			break;
		case NAME_OP :
			if(p->is_distinct)
				rc = addstr("DISTINCT ", maxsz);
			if(strcmp(p->left, "$star"))
				rc = addstr(p->left, maxsz);
			else
				rc = addstr("*", maxsz);
			break;
		default:
			break;
	}
	if (rc == -1)
		return -1;
	rc = showop(p->op, maxsz);
	if (rc == -1)
		return -1;
	switch(p->op)
	{
		case FOP_MM:
		case FOP_NMM:
		case FOP_RELEV:
		case FOP_PROXIM:
			if (p->rt == FIELD_OP)
			{
				DDMMAPI	*ddmmapi;
				size_t	sz;

				ddmmapi = getfld((FLD *)p->right, &sz);
				if (ddmmapi)
				{
					if(ddmmapi->qtype == 'N')
						rc = addstr(ddmmapi->qdata, maxsz);
					else
					{
						addstr("'", maxsz);
						addstr(ddmmapi->query, maxsz);
						rc = addstr("'", maxsz);
					}
				}
			}
			break;
		case FOP_CNV:
			addstr("'", maxsz);
			addstr(ddfttypename(((FLD *)p->right)->type), maxsz);
			rc = addstr("')", maxsz);
			break;
		default:
			switch (p->rt)
			{
				case 'P' :
					rc = idisppred(p->right, maxsz);
					break;
				case FIELD_OP :
					if (p->rat == FIELD_OP &&
					    p->altright)
						fld = (FLD *)p->altright;
					else
						fld = (FLD *)p->right;
					if (TXismmop(p->op, NULL))
					{
						if (fld)
						{
						    DDMMAPI	*ddmmapi;
						    size_t	sz;

						    ddmmapi = getfld(fld, &sz);
						    if (!ddmmapi)
							    s = "NULL";
						    else if (sz != sizeof(DDMMAPI))
							    s = "invalidSizeDDMMAPI";
						    else if (ddmmapi->qtype == 'N')
							    s = ddmmapi->qdata;
						    else
						    {
							    if (addstr("'", maxsz) < 0) goto err;
							    if (addstr(ddmmapi->query, maxsz) < 0) goto err;
							    rc = addstr("'", maxsz);
							    break;
						    }
						}
						else
							s = "";
							
					}
					else	/* not Metamorph op */
					{
						if (fld)
							s = fldtostr(fld);
						else
							s = "";
					}
					rc = addstr(s, maxsz);
					break;
				case NAME_OP :
					if(strcmp(p->right, "$star"))
						rc = addstr(p->right, maxsz);
					else
						rc = addstr("*", maxsz);
					break;
				default:
					break;
			}
	}
	if (rc == -1)
		return -1;
	if (p->op == AGG_FUN_OP || p->op == REG_FUN_OP || p->op == FOP_OR)
	{
		rc = addstr(")", maxsz);
	}
	if (TxDispPredParen && p->op != 0)
		rc = addstr(")", maxsz);
	return rc;
err:
	return(-1);
}

/******************************************************************/

char *
TXdisppred(p, ext, nomalloc, maxsz)
PRED	*p;
int	ext;
int	nomalloc;
int	maxsz;
{
	if (p)
	{
		if(ext)
		{
			if(p->edisplay)
			{
				if(nomalloc)
					return p->edisplay;
				else
					return strdup(p->edisplay );
			}
		}
		else
		{
			if(p->idisplay)
			{
				if(nomalloc)
					return strdup(p->idisplay);
				else
					return(strdup(p->idisplay));
			}
		}
		if(nomalloc)
			tempbuf = tbuf;
		else
			tempbuf = malloc(MAXINSZ);
		if(!tempbuf)
		{
			putmsg(MWARN+MAE, "disppred", strerror(ENOMEM));
			return NULL;
		}
		tempbuf[0] = '\0';
		curpos = tempbuf;
		bufsz = MAXINSZ;
		buflen = 0;
		idisppred(p, maxsz);
		return tempbuf;
	}
	return strdup("");
}

/******************************************************************/
/*	Produce an list of fields in a predicate.  
 */

static void ipredflds ARGS((PRED *, size_t));

static void
ipredflds(PRED *p, size_t maxsz)
{
	if(!p)
		return;
#ifdef NEVER
	if(p->op == 0) /* Why */
#endif
	switch(p->op)
	{
	case REG_FUN_OP:
	case AGG_FUN_OP:
		break;
	default:
		switch (p->lt)
		{
			case 'P' :
				ipredflds(p->left, maxsz);
				break;
			case NAME_OP :
				if(strcmp(p->left, "$star"))
					addstr(p->left, maxsz);
				else
					addstr("*", maxsz);
				addstr(",", maxsz);
				break;
			default:
				break;
		}
	}
	switch (p->rt)
	{
		case 'P' :
			ipredflds(p->right, maxsz);
			break;
		case NAME_OP :
			if(strcmp(p->right, "$star"))
				addstr(p->right, maxsz);
			else
				addstr("*", maxsz);
			addstr(",", maxsz);
			break;
		case FIELD_OP:
			if(TXismmop(p->op, NULL))
			{
				DDMMAPI *ddmmapi;

				ddmmapi = getfld(p->right, NULL);
				if(ddmmapi && (ddmmapi->qtype == NAME_OP))
				{
					addstr(ddmmapi->qdata, maxsz);
					addstr(",", maxsz);
				}
			}
			break;
		default:
			break;
	}
}


/******************************************************************/

char *
TXpredflds(p)
PRED *p;
{
	char *x;

	tempbuf = malloc(MAXINSZ);
	tempbuf[0] = '\0';
	curpos = tempbuf;
	bufsz = MAXINSZ;
	buflen = 0;
	ipredflds(p, 0);
	for(x=tempbuf; *x; x++)
		if(*x == '\\') *x=',';
	return tempbuf;
}
