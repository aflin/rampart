/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"


/******************************************************************/

PROJ *
closeproj(proj)
PROJ *proj;
{
	int i;

	if (proj != (PROJ *)NULL)
	{
		if (proj->preds != (PRED **)NULL)
		{
			for (i=0; i < proj->n; i++)
				if (proj->preds[i] != (PRED *)NULL)
					closepred(proj->preds[i]);
			proj->preds = TXfree(proj->preds);
		}
		proj = TXfree(proj);
	}
	return (PROJ *)NULL;
}

/******************************************************************/

PROJ *
dupproj(p)
PROJ	*p;
{
	PROJ	*proj;
	int	i;

	proj = (PROJ *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sizeof(PROJ));
	if (proj == (PROJ *)NULL)
		return proj;
	proj->n = p->n;
	proj->p_type = p->p_type;
	proj->preds = (PRED **)TXcalloc(TXPMBUFPN, __FUNCTION__, proj->n,
					sizeof(PRED *));
	for (i=0; i < p->n; i++)
	{
		proj->preds[i] = duppred(p->preds[i]);
	}
	return proj;
}

/******************************************************************/

void
nullmms(pred)
PRED *pred;
{
	if (pred == (PRED *)NULL)
		return;
	if (pred->lt == 'P')
		nullmms(pred->left);
	if (pred->rt == 'P')
		nullmms(pred->right);
	if (pred->rt == FIELD_OP && (TXismmop(pred->op, NULL)))
	{
		closefld(pred->right);
		pred->rt = 0;
	}
}

/******************************************************************/

PRED *
closepred(pred)
PRED *pred;
{
	FLD	*f;

	if (pred == (PRED *)NULL)
		return pred;
	if (pred->lt == 'P')
		closepred(pred->left);
	if (pred->rt == 'P')
		closepred(pred->right);
	if (pred->lt == NAME_OP)
        {
		TXfree(pred->left);
        }
	if (pred->rt == NAME_OP)
		TXfree(pred->right);
	if (pred->lt == FIELD_OP)
	{
#ifndef NO_TRY_FAST_CONV
		if ((pred->dff & 0x1) == 0)
#endif
		{
			f = (FLD *)pred->left;
			if (f) freeflddata(f);
			closefld(pred->left);
		}
		if(pred->lat == FIELD_OP)
		{
			if(pred->altleft && !(pred->dff & 0x4))
				closefld(pred->altleft);
			pred->altleft = FLDPN;
		}
	}
	if (pred->rt == FIELD_OP)
	{
#ifndef NO_TRY_FAST_CONV
		if ((pred->dff & 0x2) == 0)
#endif
		{
			f = (FLD *)pred->right;
			if(TXismmop(pred->op, NULL))
			{
				void	*v;

				v = getfld(f, NULL);
				v = closeddmmapi(v);
				putfld(f, v, 0);
			}
			else
			{
				freeflddata(f);
			}
			closefld(f);
		}
		if(pred->rat == FIELD_OP)
		{
			if(pred->altright && !(pred->dff & 0x8))
				closefld(pred->altright);
			pred->altright = FLDPN;
		}
	}
	if (pred->rt == SUBQUERY_OP)
	{
		if(pred->rat == FIELD_OP)
		{
			if(pred->altright && !(pred->dff & 0x8))
				closefld(pred->altright);
			pred->altright = FLDPN;
		}
#if defined(NEVER)
		putmsg(999, "closepred", "Going to close subquery %d", pred->dff);
		closeqnode(pred->right);
#endif
	}
	pred->edisplay = TXfree(pred->edisplay);
	pred->idisplay = TXfree(pred->idisplay);
#ifndef HAVE_DUPPRED2
	if(pred->refc == 2)
	{
		pred->itype = TXfree(pred->itype);
		if(pred->iname)
		{
			int i;

			for(i = 0; i < pred->indexcnt; i++)
				pred->iname[i] = TXfree(pred->iname[i]);
			pred->iname = TXfree(pred->iname);
		}
	}
#endif
	if(pred->resultfld)
		pred->resultfld = closefld(pred->resultfld);
	pred = TXfree(pred);
	return NULL;
}

/******************************************************************/

int
TXqnodeCountNames(qtree)
QNODE *qtree;
{
	if (qtree == (QNODE *)NULL)
		return 0;
	if (qtree->op != (unsigned)LIST_OP)
		return 1;
	return TXqnodeCountNames(qtree->right) +
		TXqnodeCountNames(qtree->left);
}

/******************************************************************/

static int countnodes ARGS((QNODE *));

static int
countnodes(qtree)
QNODE *qtree;
{
	if (qtree == (QNODE *)NULL)
		return 1;
	if (qtree->op == PARAM_OP)
		return 1;
	return 1 + countnodes(qtree->right) + countnodes(qtree->left);
}


/******************************************************************/

static int countlengths ARGS((QNODE *));

static int
countlengths(q)
QNODE	*q;
{
	PARAM	*p;

	switch(q->op)
	{
	case FIELD_OP:
		return ((FLD *)q->tname)->size;
	case PARAM_OP:
		p = q->tname;
		if (p->fld)
		{
			return p->fld->size;
		}
		return 0;
	case LIST_OP:
		return countlengths(q->left) + countlengths(q->right);
	default:
		break;
	}
	return 0;
}

/******************************************************************/

static byte *walknadd ARGS((QNODE *, byte *, size_t));

static byte *
walknadd(q,v,n)
QNODE	*q;
byte	*v;
size_t	n;
{
	PARAM	*p;

	switch(q->op)
	{
	case FIELD_OP:
		memcpy(v, getfld(q->tname, NULL), n);
		v += n;
		return v;
	case PARAM_OP:
		p = q->tname;
		if (p->fld)
		{
			memcpy(v, getfld(p->fld, NULL), n);
			v+=n;
		}
		return v;
	case LIST_OP:
		v = walknadd(q->left, v, n);
		v = walknadd(q->right, v, n);
		return v;
	default:
		break;
	}
	return v;
}

/******************************************************************/

static char *walknaddstr ARGS((QNODE *q, char *v, byte *byteUsed));

static char *
walknaddstr(q, v, byteUsed)
QNODE	*q;
char	*v;
byte	*byteUsed;
{
	PARAM *p;
	size_t	n;
	char	*s, *d;
	FLD	*f;

	switch(q->op)
	{
	case PARAM_OP:
		p = q->tname;
		if (!p->fld) return(v);
		f = p->fld;
		goto copyFld;
	case FIELD_OP:
		f = q->tname;
	copyFld:
		/* Copy field to `v', noting in `byteUsed'
		 * which bytes occur, for later strlst-sep determination:
		 */
		for (s = getfld(f, &n), d = v; *s; s++, d++)
			byteUsed[(byte)(*d = *s)] = 1;
		*d = '\0';
		v += n;
		v++;
		return v;
	case LIST_OP:
		v = walknaddstr(q->left, v, byteUsed);
		v = walknaddstr(q->right, v, byteUsed);
		return v;
	default:
		break;
	}
	return v;
}

/******************************************************************/

static QNODE *convqnodetovarfld ARGS((QNODE *));

static QNODE *
convqnodetovarfld(q)
QNODE	*q;
{
	int	n, i;
	FLD	*nf = NULL;
	void	*v;
	QNODE	*nq;
	PARAM	*p;
	byte	byteUsed[256];

	if(q->op != LIST_OP)
		return q;
	n = TXqnodeCountNames(q);
	switch(q->right->op)
	{
	case FIELD_OP:
		nf = newfld(q->right->tname);
		break;
	case PARAM_OP:
		p = q->right->tname;
		if (!p->fld)
			return q;
		nf =  newfld(p->fld);
		break;
	default: return q;
	}
	if(!nf)
		return q;
	/* ---------- NOTE: see also convlisttovarfld() ---------- */
	nf->type |= DDVARBIT;
	if(nf->elsz != 1)
	{
		v = (void *)TXmalloc(TXPMBUFPN, __FUNCTION__, n * nf->elsz);
		walknadd(q, v, nf->elsz);
		putfld(nf, v, n);
	}
	else
	{
		size_t	tsz;
		char	*v1;
		ft_strlst	*sl;

		tsz = countlengths(q);		/* sum of items' lengths */
		tsz += n;			/* nul-term. each item */
		tsz ++;				/* nul-term. whole strlst */
		n = tsz + sizeof(ft_strlst);
		sl = (ft_strlst *)TXmalloc(TXPMBUFPN, __FUNCTION__, n + 1);/* +1 for fldmath nul-term. */
		((char *)sl)[n] = '\0';		/* for fldmath */
		memset(byteUsed, 0, sizeof(byteUsed));
		v = &sl->buf;
		v1 = walknaddstr(q, v, byteUsed);  /* copy items to `v' */
		/* KNG 20071108 was not including final nul in `nb': */
		*(v1++) = '\0';			/* nul-term. whole strlst */
		sl->nb = v1 - (char *)v;	/* `nb' includes strlst nul */
		/* KNG 20120227 use printable delim if possible: */
		for (i=0; i<256 && byteUsed[(byte)TxPrefStrlstDelims[i]]; i++);
		sl->delim = (i < 256 ? TxPrefStrlstDelims[i] : '\0');
		v = sl;
		nf->type = FTN_STRLST;
		setfldandsize(nf, v, n + 1, FLD_FORCE_NORMAL);	/* +1 for fldmath nul-term. */
	}
	if ((nq = openqnode(FIELD_OP)) == QNODEPN) return(QNODEPN);
	nq->tname = nf;
        /* KNG 20071107 Bug 1961: save original `q' instead of closing it,
	 * so TXunpreparetree() can restore it at next statement re-use,
	 * so we can re-convert new parameters:
	 */
        nq->org = q;
	return nq;
}

/******************************************************************/

static int
singlevalue(FLD *f)
/* WTF could almost use TXfldNumItems(f) == 1 here, except latter takes
 * strlst as multi-item.
 */
{
	if((f->elsz) > 1 && (f->n == 1))
		return 1;
/*
	switch(f->type & DDTYPEBITS)
	{
		case FTN_BYTE:
		case FTN_CHAR:
			return 1;
	}
*/
	return 0;
}

static int
TXprepMatchesExpression(TXPMBUF *pmbuf, PRED *pred, FLD *exprFld)
/* Returns 0 on error.
 */
{
  void          *v;
  char          *expr = NULL;
  size_t        sz;
  ft_internal   *fti;
  FLD           *matchesFld = NULL;

  v = getfld(exprFld, &sz);

  /* Bug 3677: explicitly disallow multi-item RHS of MATCHES until we
   * support it properly.  See also fochsl(), foslsl() FOP_MAT:
   */

  switch (exprFld->type & DDTYPEBITS)
    {
    case FTN_INTERNAL:
      fti = (ft_internal *)v;
      if (tx_fti_gettype(fti) == FTI_matches)
        {
          fti = tx_fti_copy4read(fti,1);
          break;
        }
      /* else fall through to bad type: */
    case FTN_STRLST:
      txpmbuf_putmsg(pmbuf, MERR + UGE, NULL,
             "Unsupported type %s for MATCHES expression `%s'",
             TXfldtypestr(exprFld), fldtostr(exprFld));
      goto err;
    default:
      if (exprFld->n != 1)
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, NULL,
       "Unsupported multi-value field of type %s for MATCHES expression `%s'",
                 TXfldtypestr(exprFld), fldtostr(exprFld));
          goto err;
        }
      /* else fall through and convert to varchar: */
    case FTN_BYTE:
    case FTN_CHAR:
    case FTN_BLOB:
    case FTN_BLOBI:
    case FTN_BLOBZ:
      expr = fldtostr(exprFld);
      fti = tx_fti_open(FTI_matches, expr, strlen(expr));
      break;
    }

  if (!fti)
    txpmbuf_putmsg(pmbuf, MERR + UGE, NULL,
                   "MATCHES: failed to open expression `%s'", expr);
  matchesFld = createfld("varinternal", 1, 0);
  if (!matchesFld) goto err;
  setfldandsize(matchesFld, fti, FT_INTERNAL_ALLOC_SZ + 1, FLD_FORCE_NORMAL);
  pred->right = matchesFld;
  pred->rt = FIELD_OP;
  return(1);                                    /* success */

err:
  return(0);                                    /* error */
}

static TXbool
TXaddAltValueWithCooked(TXPMBUF *pmbuf, PRED *pred, TXbool right)
/* Sets `pred->alt{left,right}' (per `right') to TXftiValueWithCooked wrapper
 * of left/right field, which is presumably a parameter to e.g. lookup()
 * that can use this type to hang a cooked object off of for later calls
 * (optimization).
 * Returns false on error.
 */
{
	ft_internal		*fti = NULL;
	TXbool			ret;
	FLD			*cookedFld = NULL, *srcFld;
	TXftiValueWithCooked	*valueWithCooked;
	void			*v;
	size_t			n;

	srcFld = (FLD *)(right ? pred->right : pred->left);
	v = getfld(srcFld, &n);

	/* Create `cookedFld' with FTI: */
	/* non-NULL `usr' so TXftiValueWithCooked created: */
	fti = tx_fti_open(FTI_valueWithCooked, "dummy", 0);
	if (!fti) goto err;
	valueWithCooked = tx_fti_getobj(fti);
	if (!TXftiValueWithCooked_SetValue(pmbuf, valueWithCooked, v,
					   TXfldType(srcFld), n,
					   srcFld->size,
					   /* wtf could avoid duping? */
					   TXdup_DupIt))
		goto err;

	cookedFld = createfld("varinternal", 1, 0);
	if (!cookedFld) goto err;
	setfldandsize(cookedFld, fti, FT_INTERNAL_ALLOC_SZ + 1, FLD_FORCE_NORMAL);
	fti = NULL;				/* `cookedFld' owns it */

	/* Hang `cookedFld' off the appropriate PRED alt: */
	if (right)
	{
		if (pred->rat == FIELD_OP &&
		    pred->altright &&
		    !(pred->dff & 0x8))
			closefld(pred->altright);
		pred->altright = cookedFld;
		pred->rat = FIELD_OP;
		pred->dff &= ~0x8;
	}
	else
	{
		if (pred->lat == FIELD_OP &&
		    pred->altleft &&
		    !(pred->dff & 0x4))
			closefld(pred->altleft);
		pred->altleft = cookedFld;
		pred->lat = FIELD_OP;
		pred->dff &= ~0x4;
	}
	cookedFld = NULL;			/* owned by `pred' */

	ret = TXbool_True;
	goto finally;

err:
	ret = TXbool_False;
finally:
	fti = tx_fti_close(fti, 1);
	cookedFld = closefld(cookedFld);
	return(ret);
}

/******************************************************************/
/*	Convert a parse tree into a predicate.
 */

typedef enum TXtreeToPredFlag_tag
{
	TXtreeToPredFlag_DiscardUnsetParameterClauses	= (1 << 0),
	TXtreeToPredFlag_MakeFieldsValueWithCooked	= (1 << 1),
}
TXtreeToPredFlag;

static PRED *
ctreetopred(DDIC *ddic, QNODE *qtree, TXtreeToPredFlag flags, int *pconst,
            FLDOP *fo, DBTBL *dbtbl)
{
	PRED *pred;
	int wasconst=0, setLong = 0;
	FLD	*fld, *newFld = FLDPN;
	long	lcode, hcode;
	void	*v;
	size_t	sz;
	ft_strlst	*sl;
	char	*s;
	TXPMBUF	*pmbuf = (ddic ? ddic->pmbuf : TXPMBUFPN);

	/* Bug 6974 optimization: we wrap lookup() parameters in
	 * TXftiValueWithCooked (ft_internal) objects so lookup() can
	 * squirrel away some data in between calls; prep for that
	 * here, as parameters get converted before the function
	 * (REG_FUN_OP):
	 */
	if (ddic->optimizations[OPTIMIZE_SQL_FUNCTION_PARAMETER_CACHE])
	    switch (qtree->op)
	    {
	    case REG_FUN_OP:
		if (qtree->left &&
		    qtree->left->op == NAME_OP &&
		    qtree->left->tname &&
		    strcmp(qtree->left->tname, "lookup") == 0)
			flags |= TXtreeToPredFlag_MakeFieldsValueWithCooked;
		else
			/* see `default:' comments */
			flags &= ~TXtreeToPredFlag_MakeFieldsValueWithCooked;
		break;
	    case LIST_OP:
	    case FIELD_OP:
	    case PARAM_OP:
		/* preserve ...MakeFields... if passed in */
		break;
	    default:
		/* We do not wrap parameters of nested calls; e.g. for
		 * `lookup(func(foo))', do not wrap `foo' (since `func'
		 * does not take wrapped params, unlike `lookup'):
		 */
		flags &= ~TXtreeToPredFlag_MakeFieldsValueWithCooked;
		break;
	    }

	pred = TX_NEW(pmbuf, PRED);
	if (!pred) goto err;

	if (qtree->op == DISTINCT_OP)
	{
		pred->is_distinct = 1;
		qtree = qtree->right;
	}
	else
	{
		pred->is_distinct = 0;
	}
	if (qtree->op == RENAME_OP)
	{
		if(qtree->tname && (strlen(qtree->tname) > DDNAMESZ))
		{
			txpmbuf_putmsg(pmbuf, MWARN, NULL,
				       "Column alias too long");
			goto err;
		}
		pred->edisplay = TXstrdup(pmbuf, __FUNCTION__, qtree->tname);
	}
	switch (qtree->op)
	{
	case FOP_IN:
	case FOP_IS_SUBSET:
	case FOP_INTERSECT:
	case FOP_INTERSECT_IS_EMPTY:
	case FOP_INTERSECT_IS_NOT_EMPTY:
	case FOP_TWIXT:
		/* NOTE: see also ireadnode(): */
		if (qtree->right && qtree->right->op == LIST_OP)
			qtree->right = convqnodetovarfld(qtree->right);
		/* KNG SUBSET/INTERSECT may have a LHS list: */
		if (qtree->left && qtree->left->op == LIST_OP)
			qtree->left = convqnodetovarfld(qtree->left);
		break;
	case ARRAY_OP:
		if(qtree->left && qtree->left->op == LIST_OP)
			qtree->left = convqnodetovarfld(qtree->left);
	default:
		break;
	}

	/* KNG 20071107 canonicalize geocode box to SW/NE,
	 * which is what internal functions and index expect,
	 * so we can support NE/SW, SE/NW, NW/SE boxes too:
	 * KNG 20071108 convert to FTN_LONG first:
	 */
	if (qtree->op == FOP_TWIXT && qtree->right->op == FIELD_OP)
	{
		fld = (FLD *)qtree->right->tname;
		v = getfld(fld, &sz);
		if (sz >= 2)		/* should have 2 values */
			switch (fld->type & DDTYPEBITS)
			{
			case FTN_STRLST:
				/* Since we know LHS is a geocode,
				 * we can convert lat/lon pair too
				 * (via TXparseLocation()); not just
				 * integral string (fldmath below):
				 */
				sl = (ft_strlst *)v;
				if (sz >= TX_STRLST_MINSZ && sl->nb > 1)
				{
					char	*e;

					s = sl->buf;
					lcode = TXparseLocation(s, &e,
								NULL, NULL);
					s += strlen(s) + 1;
					hcode = TXparseLocation(s, &e,
								NULL, NULL);
				}
				else
					lcode = hcode = -1L;
				setLong = 1;
				goto doSetLong;
			default:
				/* Convert to long first.  Allows
				 * canon-geocode check below, and may
				 * save on-the-fly conv during eval?
				 * Ok since FOP_TWIXT is defined for
				 * long (geocode) only.
				 */
				if (!(newFld = createfld("varlong", 2, 0)) ||
				    fopush(fo, newFld) != 0 ||
				    fopush(fo, fld) != 0 ||
				    foop(fo, FOP_ASN) != 0)
					goto convFailed;
				newFld = closefld(newFld);
				if ((newFld = fopop(fo)) == FLDPN ||
				    (newFld->type & DDTYPEBITS) != FTN_LONG ||
				    newFld->n < 2)
				{
				convFailed:
					txpmbuf_putmsg(pmbuf, MERR + MAE,
						       __FUNCTION__,
			       "Could not convert BETWEEN bounds to varlong");
					newFld = closefld(newFld);
					break;
				}
				closefld((FLD *)qtree->right->tname);
				qtree->right->tname = fld = newFld;
				v = getfld(fld, &sz);
				/* fall through and canonicalize: */
			case FTN_LONG:
				lcode = ((ft_long *)v)[0];
				hcode = ((ft_long *)v)[1];
			doSetLong:
				if (!TXcanonicalizeGeocodeBox(&lcode, &hcode)||
				    setLong)
				{	/* was changed */
					if (!(v = TXcalloc(pmbuf, __FUNCTION__,
							 3, sizeof(ft_long))))
						;
					else
					{
						((ft_long *)v)[0] = lcode;
						((ft_long *)v)[1] = hcode;
						if (setLong)
						{
							releasefld(fld);
							fld->type = FTN_LONG;
							fld->elsz = sizeof(ft_long);
						}
						setfldandsize(fld, v,
						       2*sizeof(ft_long) + 1, FLD_FORCE_NORMAL);
					}
				}
				break;
			}
	}

	if (qtree->op == NAME_OP)
	{
		if(dbtbl)
		{
			FLD *f;

			f = dbnametofld(dbtbl, qtree->tname);
			if(f)
			{
				pred->left = dupfld(f);
				pred->lt = FIELD_OP;
				pred->op = 0;
				goto finally;
			}
		}
		pred->left = TXstrdup(pmbuf, __FUNCTION__, qtree->tname);
		pred->lt = qtree->op;
		pred->op = 0;
		goto finally;
	}
	if (qtree->op == FIELD_OP)
	{
#ifndef NO_TRY_FAST_CONV
		pred->left = qtree->tname;
		pred->dff |= 0x1;
#else
		pred->left = dupfld(qtree->tname);
#endif
		pred->lt = qtree->op;
		pred->op = 0;
		*pconst = 1;
		if (flags & TXtreeToPredFlag_MakeFieldsValueWithCooked)
			TXaddAltValueWithCooked(pmbuf, pred, TXbool_False);
		goto finally;
	}
	if(qtree->op == ARRAY_OP && qtree->left && qtree->left->op == FIELD_OP)
	{
		pred->left = qtree->left->tname;
		pred->dff |= 0x1;
		pred->lt = qtree->left->op;
		pred->op = 0;
		*pconst = 1;
		if (flags & TXtreeToPredFlag_MakeFieldsValueWithCooked)
			TXaddAltValueWithCooked(pmbuf, pred, TXbool_False);
		goto finally;
	}
	if (qtree->op == PARAM_OP)
	{
		PARAM	*p = qtree->tname;

		if (!p->fld) goto err;
#ifndef NO_TRY_FAST_CONV
		pred->left = p->fld;
		pred->dff |= 0x1;
#else
		pred->left = dupfld(p->fld);
#endif
		pred->lt = FIELD_OP;
		pred->op = 0;
		*pconst = 1;
		if (flags & TXtreeToPredFlag_MakeFieldsValueWithCooked)
			TXaddAltValueWithCooked(pmbuf, pred, TXbool_False);
		goto finally;
	}
	if(qtree->op == SUBQUERY_OP)
	{
		pred->op = SUBQUERY_OP;
		pred->left = qtree->left;
		goto finally;
	}
	if (!qtree->left)
	{
		pred = closepred(pred);
		goto finally;
	}
	if (qtree->left->op == NAME_OP)
	{
		if(dbtbl)
		{
			FLD *f;

			f = dbnametofld(dbtbl, qtree->left->tname);
			if(f)
			{
				pred->left = dupfld(f);
				pred->lt = FIELD_OP;
			}
			else goto nolfield;
		}
		else
		{
nolfield:
			pred->left = TXstrdup(pmbuf, __FUNCTION__,
					      qtree->left->tname);
			pred->lt = qtree->left->op;
		}
	}
	else if (qtree->left->op == FIELD_OP)
	{
#ifndef NO_TRY_FAST_CONV
		pred->left = qtree->left->tname;
		pred->dff |= 0x1;
#else
		pred->left = dupfld(qtree->left->tname);
#endif
		pred->lt = qtree->left->op;
		if (flags & TXtreeToPredFlag_MakeFieldsValueWithCooked)
			TXaddAltValueWithCooked(pmbuf, pred, TXbool_False);
	}
	else if (qtree->left->op == PARAM_OP)
	{
		PARAM	*prm = qtree->left->tname;
		if(!prm->needdata && prm->fld)
		{
#ifndef NO_TRY_FAST_CONV
			pred->left = prm->fld;
			pred->dff |= 0x1;
#else
			pred->left = dupfld(prm->fld);
#endif
			pred->lt = FIELD_OP;
			if (flags & TXtreeToPredFlag_MakeFieldsValueWithCooked)
			    TXaddAltValueWithCooked(pmbuf, pred, TXbool_False);
		}
		else
		{
			pred->left = NULL;
			pred->lt = 'P';
		}
	}
	else if(qtree->left->op == EXISTS_OP)
	{
		pred->left = ctreetopred(ddic, qtree->left, flags,
					 &wasconst, fo, dbtbl);
		pred->lt = 'P';
#ifdef ALLOW_MM_PRED
		if(wasconst)
		{
			FLD *f;
			FLDOP *fo = dbgetfo();

			pred_eval(NULL, pred->left, fo);
			f = fopop(fo);
			pred->left = f;
			pred->lt = FIELD_OP;
			foclose(fo);
		}
#endif
	}
	else if(qtree->left->op == SUBQUERY_OP)
	{
		pred->lt = SUBQUERY_OP;
		pred->left = qtree->left;
	}
	else
	{
		pred->left = ctreetopred(ddic, qtree->left, flags,
					 &wasconst, fo, dbtbl);
		pred->lt = 'P';
#ifdef ALLOW_MM_PRED
		if (wasconst)
		{
			FLD *f;
			FLDOP *fo = dbgetfo();

			pred_eval(NULL, pred->left, fo);
			f = fopop(fo);
			pred->left = f;
			pred->lt = FIELD_OP;
			foclose(fo);
		}
#endif
	}
	if (qtree->right)
	{
		if (qtree->right->op == NAME_OP ||
		    qtree->right->op == FIELD_OP)
		{
                        FOP fldOp;
			if (TXismmop(qtree->op, &fldOp))
			{
				DDMMAPI *ddmmapi;
				FLD	*f;
				ddmmapi = openddmmapi(qtree->right->op,
						      qtree->right->tname,
						      fldOp);
				if (!ddmmapi)
				{
					/* KNG 20150902 source/html/search4.src looks for this message: */
					txpmbuf_putmsg(pmbuf, MWARN, NULL, "Metamorph open failed.");
#ifdef NEVER
					goto err;
#endif
				}
				f = createfld("char",sizeof(DDMMAPI),0);
				putfld(f, ddmmapi, sizeof(DDMMAPI));
				pred->right = f;
				pred->rt = FIELD_OP;
			}
			else if (qtree->op == FOP_MAT &&
				 qtree->right->op == FIELD_OP)
			{
				if (!TXprepMatchesExpression(pmbuf, pred,
                                                         qtree->right->tname))
					goto err;
			}
			else
			{
				pred->rt = qtree->right->op;
				if (pred->rt == NAME_OP)
				{
					if(dbtbl)
					{
						FLD *f;

						f = dbnametofld(dbtbl,
							 qtree->right->tname);
						if (f)
						{
							pred->right =dupfld(f);
							pred->rt = FIELD_OP;
						}
						else
							goto norfield;
					}
					else
					{
norfield:
						pred->right = TXstrdup(pmbuf,
					   __FUNCTION__, qtree->right->tname);
					}
				}
				else
				{
#ifndef NO_TRY_FAST_CONV
					pred->right = qtree->right->tname;
					pred->dff |= 0x2;
#else
					pred->right =
						dupfld(qtree->right->tname);
#endif
					if (flags & TXtreeToPredFlag_MakeFieldsValueWithCooked)
						TXaddAltValueWithCooked(pmbuf,
							   pred, TXbool_True);
				}
			}
		}
		else if (qtree->right->op == PARAM_OP)
		{
			PARAM	*prm;
			prm = qtree->right->tname;
			if(prm->needdata || !prm->fld)
			{
				pred->right = NULL;
				pred->rt = 'P';
			} else {
                                FOP fldOp;

				if (TXismmop(qtree->op, &fldOp))
				{
					DDMMAPI *ddmmapi;
					FLD	*f;
					ddmmapi = openddmmapi(FIELD_OP,
							 prm->fld, fldOp);
					if (!ddmmapi)
					{
						/* KNG 20150902 source/html/search4.src looks for this message: */
						txpmbuf_putmsg(pmbuf, MWARN,
							       NULL, "Metamorph open failed.");
						if ((flags & TXtreeToPredFlag_DiscardUnsetParameterClauses) && !prm->fld)
							goto err;

					}
					f=createfld("char",sizeof(DDMMAPI),0);
					putfld(f, ddmmapi, sizeof(DDMMAPI));
					pred->right = f;
					pred->rt = FIELD_OP;
				}
				else if(qtree->op == FOP_MAT)
				{
                                  if (!TXprepMatchesExpression(pmbuf, pred,
                                                               prm->fld))
					  goto err;
				}
				else
				{
#ifndef NO_TRY_FAST_CONV
					pred->right = prm->fld;
					pred->dff |= 0x2;
#else
					pred->right = dupfld(prm->fld);
#endif
					pred->rt = FIELD_OP;
					if (flags & TXtreeToPredFlag_MakeFieldsValueWithCooked)
						TXaddAltValueWithCooked(pmbuf,
							   pred, TXbool_True);
				}
                       }
		}
		else if(qtree->right->op == SUBQUERY_OP)
		{
			pred->rt = SUBQUERY_OP;
			pred->right = qtree->right;
			pred->altright = TXqueryfld(ddic, NULL, qtree->right,
						    fo, 1, 0);
			if(pred->altright)
				pred->rat = FIELD_OP;
		}
		else
		{
                        FOP fldOp;

			if(TXismmop(qtree->op, &fldOp))
			{
				PRED *p;
				DDMMAPI *ddmmapi;
				FLD	*f;

				p=ctreetopred(ddic, qtree->right,flags,
					      &wasconst, fo, dbtbl);
#ifdef ALLOW_MM_PRED
				if (wasconst)
				{
					FLDOP *fo = dbgetfo();
					pred_eval(NULL, p, fo);
					f = fopop(fo);
					ddmmapi = openddmmapi(FIELD_OP, f, fldOp);
					f = closefld(f);
					foclose(fo);
				}
				else
#endif
					ddmmapi = openddmmapi('P', p, fldOp);
				if(!ddmmapi)
				{
					/* KNG 20150902 source/html/search4.src looks for this message: */
					txpmbuf_putmsg(pmbuf, MWARN, NULL,
						    "Metamorph open failed.");
					if ((flags & TXtreeToPredFlag_DiscardUnsetParameterClauses) && !p)
						goto err;
				}
				f=createfld("char",sizeof(DDMMAPI),0);
				putfld(f, ddmmapi, sizeof(DDMMAPI));
				pred->right = f;
				pred->rt = FIELD_OP;
			}
			else
			{
				pred->right = ctreetopred(ddic, qtree->right,
						 flags, &wasconst, fo, dbtbl);
				pred->rt = 'P';
#ifdef ALLOW_MM_PRED
				if(wasconst)
				{
					FLD *f;
					FLDOP *fo = dbgetfo();

					pred_eval(NULL, pred->right, fo);
					f = fopop(fo);
					pred->right = f;
					pred->rt = FIELD_OP;
					foclose(fo);
				}
#endif
			}
		}
	}
	pred->op = qtree->op;
	if(pred->op == FOP_IN)
	{
		if(pred->rt == FIELD_OP)
		{
			/* KNG 20110103 Do not change the PRED.op
			 * if compiling SQL expressions for Vortex
			 * (i.e. OPTIMIZE_PRED is off):
			 */
			if(ddic->optimizations[OPTIMIZE_PRED] &&
			   /* Bug 3677: turn off optimization for now;
			    * `=' and `IN' redefined for multi-item,
			    * and bubble-up can handle single-item IN:
			    */
			   !TXApp->strlstRelopVarcharPromoteViaCreate &&
			   singlevalue((FLD *)pred->right))
				pred->op = FLDMATH_EQ;
			else
				TXfld_optimizeforin((FLD *)pred->right, ddic);
		}
		else if (pred->rat == FIELD_OP)
		{
			if(ddic->optimizations[OPTIMIZE_PRED] &&
			   /* Bug 3677: turn off optimization for now;
			    * `=' and `IN' redefined for multi-item,
			    * and bubble-up can handle single-item IN:
			    */
			   !TXApp->strlstRelopVarcharPromoteViaCreate &&
			   singlevalue((FLD *)pred->altright))
				pred->op = FLDMATH_EQ;
			else
				TXfld_optimizeforin((FLD *)pred->altright,
						    ddic);
		}
	}
	if (pred->lt == FIELD_OP &&
	    pred->rt == FIELD_OP &&
	    pred->op != LIST_OP)
		*pconst = 1;

/*
	These lines really mess up parameters.  They free the read
	in QNODE early.
	free(qtree->right);
	qtree->right = (QNODE *)NULL;
	free(qtree->left);
	qtree->left = (QNODE *)NULL;
*/
	if (flags & TXtreeToPredFlag_DiscardUnsetParameterClauses)
	{
		if(pred->op == FOP_AND ||
		   pred->op == FOP_OR)
		{
			if(pred->rt == 'P' && !pred->right)
			{
				if(pred->lt == 'P' && pred->left)
				{
					PRED	*rc;
					rc = pred->left;
					pred = TXfree(pred);
					pred = rc;
					goto finally;
				}
			}
			if(pred->lt == 'P' && !pred->left)
			{
				if(pred->rt == 'P' && pred->right)
				{
					PRED	*rc;
					rc = pred->right;
					pred = TXfree(pred);
					pred = rc;
					goto finally;
				}
				if(pred->rt == 'P')
				{
					pred = TXfree(pred);
					goto finally;
				}
			}
		}
		else if ((pred->rt == 'P' && !pred->right) ||
			 (pred->lt == 'P' && !pred->left))
			goto err;
	}
	goto finally;

err:
	pred = closepred(pred);
finally:
	return(pred);
}

/******************************************************************/

static	int	predopttype = 0;

int
TXpredopttype(a)
int	a;
{
	predopttype = a;
	return 0;
}

/* ------------------------------------------------------------------------ */

char *
TXpredToFieldOrderSpec(PRED *pred)
/* Returns an alloced field name (with trailing `-' `^' flags)
 * for `pred'.  Only single-node PREDs supported, ala doorder().
 */
{
	char	*spec = NULL, *d;
	size_t	numToAlloc, fldLen;

	if (!pred || pred->op != 0 || pred->lt != NAME_OP) goto err;
	fldLen = strlen((char *)pred->left);
	numToAlloc = fldLen + 1;
#ifdef TX_USE_ORDERING_SPEC_NODE
	if (pred->orderFlags & OF_DESCENDING) numToAlloc++;
	if (pred->orderFlags & OF_IGN_CASE) numToAlloc++;
#endif /* TX_USE_ORDERING_SPEC_NODE */
	spec = (char *)TXmalloc(TXPMBUFPN, __FUNCTION__, numToAlloc);
	if (!spec) goto err;
	memcpy(spec, pred->left, fldLen);
	d = spec + fldLen;
#ifdef TX_USE_ORDERING_SPEC_NODE
	if (pred->orderFlags & OF_DESCENDING) *(d++) = '-';
	if (pred->orderFlags & OF_IGN_CASE) *(d++) = '^';
#endif /* TX_USE_ORDERING_SPEC_NODE */
	*d = '\0';
	goto finally;

err:
	spec = TXfree(spec);
finally:
	return(spec);
}

/******************************************************************/
/*	Convert a parse tree into an optimized predicate.
 */

PRED *
TXtreetopred(ddic, qtree, discparam, fo, dbtbl)
DDIC *ddic;
QNODE *qtree;
int discparam; /* Discard NULL parameters */
FLDOP *fo;
DBTBL *dbtbl;
{
	PRED *pred;
	int c;
	int wasconst = 0;
#ifdef TX_USE_ORDERING_SPEC_NODE
	TXOF	orderFlags = (TXOF)0;
	char	*s;
	TXtreeToPredFlag	treeToPredFlags = (TXtreeToPredFlag)0;
	TXPMBUF	*pmbuf = (ddic ? ddic->pmbuf : TXPMBUFPN);

	if (discparam)
	    treeToPredFlags |= TXtreeToPredFlag_DiscardUnsetParameterClauses;
	if (qtree->op == ORDERING_SPEC_OP)
	{
		/* See also TXpredToFieldOrderSpec(): */
		for (s = (char *)qtree->tname; s && *s; s++)
		{
			switch (*s)
			{
			case '^':
				orderFlags |= OF_IGN_CASE;
				break;
			case '-':
				orderFlags |= OF_DESCENDING;
				break;
			default:
				txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
		 "Internal warning: Unknown flag `%c' in order spec; ignored",
				       *s);
				break;
			}
		}
		qtree = qtree->left;
	}
#endif /* TX_USE_ORDERING_SPEC_NODE */

	pred = ctreetopred(ddic, qtree, treeToPredFlags, &wasconst, fo, dbtbl);
	if(!pred)
		return pred;
#ifdef TX_USE_ORDERING_SPEC_NODE
	pred->orderFlags = orderFlags;
#endif /* TX_USE_ORDERING_SPEC_NODE */
#ifdef ALLOW_MM_PRED
	if(wasconst && pred->op)
	{
		FLD *f;
		FLDOP *fo = dbgetfo();

		if(pred_eval(NULL, pred, fo)==1)
		{
			f = fopop(fo);
			pred->left = f;
			pred->lt = FIELD_OP;
			pred->right = NULL;
			pred->rt = 0;
			pred->op = 0;
		}
		foclose(fo);
	}
#endif

/* WTF (Do we need any predicate optimization.)  Removed as it is the
	reverse of Access predicate optimization.) Added new optimiztion
	same as Access.
*/
	if(TXismmop(pred->op, NULL) && pred->rt == FIELD_OP)
	{
		DDMMAPI *ddmmapi;
		ddmmapi = getfld(pred->right, NULL);
		if(ddmmapi)
			ddmmapi->lonely=1;
	}
	do {
		c = 0;
		switch(predopttype)
		{
		    case 0:
		    	break;
		    case 1:
			pred = optpred(pred, &c);
			break;
		    case 2:
			pred = optpred2(pred, &c);
			break;
		}
	} while (c);
	return pred;
}

/******************************************************************/

static int hasagg ARGS((PRED *));

static int
hasagg(p)
PRED *p;
{
	int	rc = 0;
	int	here = 0;

	if(!p)
		return 0;
	if(p->op == AGG_FUN_OP)
		here = 1;
	if(p->lt == 'P')
	{
		rc += hasagg(p->left);
	}
	if(p->rt == 'P')
	{
		rc += hasagg(p->right);
	}
	if(here)
		rc = rc + here;
	else
		if(rc > 0)
			rc = 1;
	return rc;
}

/******************************************************************/
/*	Convert a parse tree into a projection structure.
 */

PROJ *
treetoproj(ddic, qtree, fo)
DDIC *ddic;
QNODE *qtree;
FLDOP *fo;
{
	PROJ *proj;
	QNODE **qstack, *c;
	int	qsptr = 0, pc = 0, n;
	TXPMBUF	*pmbuf = (ddic ? ddic->pmbuf : TXPMBUFPN);

#define QPUSH(a) qstack[qsptr++] = a
#define QPOP	 qstack[--qsptr]

	if (!qtree)
	{
		/* KNG 20060718 we can cascade here from failure to clean up
		 * after convert($param, 'UnknownType') failure in readnode:
		 */
		txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
			       "Internal error: NULL QNODE");
		return((PROJ *)NULL);
	}

	proj = (PROJ *)TXcalloc(pmbuf, __FUNCTION__, 1, sizeof(PROJ));
	if (proj == (PROJ *)NULL)
		return proj;
	proj->n = TXqnodeCountNames(qtree);
	n = (proj->n > 0 ? proj->n : 1);	/* avoid calloc(0) */
	proj->preds = (PRED **)TXcalloc(pmbuf, __FUNCTION__, n, sizeof(PRED *));
	n = countnodes(qtree);
	if (n <= 0) n = 1;			/* avoid calloc(0) */
	qstack = (QNODE **)TXcalloc(pmbuf, __FUNCTION__, n, sizeof(QNODE *));
	if (proj->preds == (PRED **)NULL || qstack == (QNODE **)NULL)
		return closeproj(proj);
	QPUSH(qtree);
	proj->p_type = PROJ_AGG;
	do {
		QNODE_OP	cop;

		c = QPOP;
		if(!c)
			continue;
		cop = c->op;
		if(cop == RENAME_OP)
		{
			if (!c->left)
			{
				/* KNG 20060718 more cascade from
				 * convert($param, 'UnknownType') failure:
				 */
				txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
			"Internal error: RENAME_OP missing QNODE.left");
				return(closeproj(proj));
			}
			cop = c->left->op;
		}
		switch(cop)
		{
			case LIST_OP :
				if (c->right != (QNODE *)NULL)
					QPUSH(c->right);
				if (c->left != (QNODE *)NULL)
					QPUSH(c->left);
				break;
			case AGG_FUN_OP :
			case NAME_OP :
			case FIELD_OP :
			case REG_FUN_OP :
			default :
				proj->preds[pc++] = TXtreetopred(ddic, c, 0, fo, NULL);
				if(!proj->preds[pc-1])
				{
					txpmbuf_putmsg(pmbuf, MERR, NULL, "Bad Syntax");
					return closeproj(proj);
				}
				switch(hasagg(proj->preds[pc-1]))
				{
					case 1:
						proj->p_type = PROJ_SINGLE;
					case 0:
						break;
					default:
						txpmbuf_putmsg(pmbuf, MWARN, NULL,
					"Can't nest aggregate functions");
						qstack = TXfree(qstack);
						return closeproj(proj);
				}
				break;
		}
	}
	while(qsptr != 0);
	qstack = TXfree(qstack);
	return proj;

#undef QPUSH
#undef QPOP
}

/******************************************************************/

UPDATE *
closeupdate(u)
UPDATE	*u;
{
	if(u)
	{
		if(u->next)
			u->next = closeupdate(u->next);
		u->field = TXfree(u->field);
		if(u->expr)
			closepred(u->expr);
		u = TXfree(u);
	}
	return NULL;
}

/******************************************************************/
/*	Convert parse tree into update structure.
 */

UPDATE *
treetoupd(ddic, q, fo)
DDIC *ddic;
QNODE *q;
FLDOP *fo;
{
	UPDATE *u;
	TXPMBUF	*pmbuf = (ddic ? ddic->pmbuf : TXPMBUFPN);

	u = TX_NEW(pmbuf, UPDATE);
	if (q->op == LIST_OP)
	{
		u->next = treetoupd(ddic, q->left, fo);
		if(u->next == (UPDATE *)NULL)/*if(!u->next)*/
		{
			u = TXfree(u);
			return (UPDATE *)NULL;
		}
		u->field = TXstrdup(pmbuf, __FUNCTION__, q->right->left->tname);
		u->expr = TXtreetopred(ddic, q->right->right, 0, fo, NULL);
		if(u->expr == (PRED *)NULL)/*if(!u->expr)*/
		{
			txpmbuf_putmsg(pmbuf, MWARN, NULL, "Not a valid replace expression");
			u = TXfree(u);
		}
	}
	else
	{
		u->next = NULL;
		u->field = TXstrdup(pmbuf, __FUNCTION__, q->left->tname);
		u->expr = TXtreetopred(ddic, q->right, 0, fo, NULL);
		if(u->expr == (PRED *)NULL)/*if(!u->expr)*/
		{
			txpmbuf_putmsg(pmbuf, MWARN, NULL, "Not a valid replace expression");
			u = TXfree(u);
		}
	}
	return u;
}
