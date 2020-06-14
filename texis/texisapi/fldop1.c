/* -=- kai-mode: John -=- */
/*
 * template 1: handle homogeneous types
 * Macros to set when #including this file:
 *   foxxxx         Function to create, for xx and xx
 *   ft_xx          Type of arg1 xx
 *   fonomodulo     Define if FOP_MOD should not be supported
 *   GCC_ALIGN_FIX  Define to work around gcc alignment bus error
 */

/**********************************************************************/
int
foxxxx(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_xx *vp1, *vp2, *vp3 = (ft_xx *)NULL;
	size_t n1, n2, n3, i, na, nMin;
	int var1, var2, retVal;
	FOP	rc = FOP_EINVAL;
#ifdef GCC_ALIGN_FIX
	ft_xx val1, val2;
#  ifdef __GNUC__
	/* gcc tries very hard to optimize this memcpy() into an
	 * inline ft_xx assignment, with possible bus error results on
	 * Sparc.  It appears we need true byte-pointer args to memcpy()
	 * to force a byte-wise copy.  See also FDBI_TXALIGN_RECID_COPY():
	 */
	volatile byte	*itemSrc, *itemDest;
#    define ITEM1(i)	(itemSrc = (volatile byte *)&vp1[(i)],		\
			 itemDest = (volatile byte *)&val1,		\
			 memcpy((void *)itemDest, (void *)itemSrc, sizeof(ft_xx)),	\
			 val1)
#    define ITEM2(i)	(itemSrc = (volatile byte *)&vp2[(i)],		\
			 itemDest = (volatile byte *)&val2,		\
			 memcpy((void *)itemDest, (void *)itemSrc, sizeof(ft_xx)),	\
			 val2)
#  else /* !__GNUC__ */
#    define ITEM1(i)	(memcpy(&val1, &vp1[(i)], sizeof(ft_xx)), val1)
#    define ITEM2(i)	(memcpy(&val2, &vp2[(i)], sizeof(ft_xx)), val2)
#  endif /* !__GNUC__ */
#else /* !GCC_ALIGN_FIX */
#  define ITEM1(i)	(vp1[(i)])
#  define ITEM2(i)	(vp2[(i)])
#endif /* !GCC_ALIGN_FIX */

	if (op == FOP_ASN && TXfldIsNull(f2))
		return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */

	vp1 = (ft_xx *) getfld(f1, &n1);
	vp2 = (ft_xx *) getfld(f2, &n2);
	nMin = TX_MIN(n1, n2);

	if ((op & FOP_CMP) &&
	    (TXfldIsNull(f1) || TXfldIsNull(f2)))
	{
		if (op == FOP_COM)
			return(fld2finv(f3, TX_FLD_NULL_CMP(vp1, vp2,
		      (n1 == 1 && n2 == 1 ? COM(ITEM1(0), ITEM2(0)) : -1))));
		return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */
	}

	var1 = fldisvar(f1);
	var2 = fldisvar(f2);

	/* Punt FOP_CNV to FOP_ASN, except for one case below that pre-dates
	 * this statement (?):  KNG 20060619
	 */
	if (op == FOP_CNV && (n1 > 1 || var1 || n2 != 1))
		return(foxxxx(f2, f1, f3, FOP_ASN));

	if (n1 > 1 || var1)
	{
		switch (op)
		{
		case FOP_EQ:
		case FOP_NEQ:
		case FOP_LT:
		case FOP_LTE:
		case FOP_GT:
		case FOP_GTE:
		case FOP_COM:
		doMultiCmp:
			/* Behavior change; wait for version 8: */
			if (!TX_IPv6_ENABLED(TXApp)) goto invalid;
			for (i = 0; i < nMin && ISEQ(ITEM1(i), ITEM2(i)); i++);
			if (i < nMin)		/* ITEM1(i) != ITEM2(i) */
				switch (op)
				{
				case FOP_EQ:
					retVal = 0;
					break;
				case FOP_NEQ:
					retVal = 1;
					break;
				case FOP_LT:
					retVal = ISLT(ITEM1(i), ITEM2(i));
					break;
				case FOP_LTE:
					retVal = ISLTE(ITEM1(i), ITEM2(i));
					break;
				case FOP_GT:
					retVal = ISGT(ITEM1(i), ITEM2(i));
					break;
				case FOP_GTE:
					retVal = ISGTE(ITEM1(i), ITEM2(i));
					break;
				case FOP_COM:
					retVal = COM(ITEM1(i), ITEM2(i));
					break;
				default:	goto invalid;
				}
			else			/* i >= nMin */
				/* First `nMin' elements are equal;
				 * f1 has more elements than f2 or vice versa
				 */
				switch (op)
				{
				case FOP_EQ:
					retVal = (n1 == n2);
					break;
				case FOP_NEQ:
					retVal = (n1 != n2);
					break;
				case FOP_LT:
					retVal = (n1 < n2);
					break;
				case FOP_LTE:
					retVal = (n1 <= n2);
					break;
				case FOP_GT:
					retVal = (n1 > n2);
					break;
				case FOP_GTE:
					retVal = (n1 >= n2);
					break;
				case FOP_COM:
					retVal = (n1 < n2 ? -1 :
						  (n1 > n2 ? 1 : 0));
					break;
				default:	goto invalid;
				}
			rc = fld2finv(f3, retVal);
			break;
		case FOP_ASN:
		flexasn:
			if (f3 == f2)		/* WTF why does this occur? */
			{
				/* TXmakesimfield() below might free `f2' data
				 * before we copy it, so we don't call it.
				 * But then we already have the data in the
				 * right field (f3), so just check that it's
				 * the right size:
				 */
				n3 = (var1 ? n2 : n1);	/* what we want */
				if ((na = n3*sizeof(ft_xx) + 1) > f3->alloced)
				{		/* not enough alloced */
					if ((vp3 =(ft_xx*)TXmalloc(TXPMBUFPN, __FUNCTION__, na)) == NULL)
					{
						rc = FOP_ENOMEM;
						break;
					}
					memcpy(vp3, vp2, n2*sizeof(ft_xx));
					setfldandsize(f3, vp3, na, FLD_FORCE_NORMAL);
				}
				else
				{
					vp3 = vp2;
					if (n3 != n2)
					{
						/* KNG 20110225 proper sz: */
						f3->n = n3;
						f3->size = n3*sizeof(ft_xx);
					}
				}
				f3->type = f1->type; /* set right DDVARBIT */
			}
			else			/* preferred method */
			{
				if (var1 && n2 != n1)
				{
					/* copy `f2' for its size: */
					TXmakesimfield(f2, f3);
					/* but we want `f1''s type: */
					f3->type = f1->type;
				}
				else
					TXmakesimfield(f1, f3);
				vp3 = (ft_xx *)getfld(f3, &n3);
				na = n3*sizeof(ft_xx) + 1;
				/* copy `f2' data to `f3': */
				memcpy(vp3, vp2, TX_MIN(n3, n2)*sizeof(ft_xx));
			}
			/* pad the rest of `f3' with 0s: */
			for (i = n2; i < n3; i++) vp3[i] = (ft_xx)0;
			/* nul-terminate for fldmath: */
			((char *)vp3)[na - 1] = '\0';
			rc = FOP_EOK;
			break;
		default:
		invalid:
			rc = FOP_EINVAL;
		}
	}					/* end (n1 > 1 || var1) */
	else if (n2 != 1)
	{
		if (op != FOP_ASN && !(op & FOP_CMP))
		{
			TXmakesimfield(f1, f3);
			vp3 = (ft_xx *) getfld(f3, &n3);
		}
		switch (op)
		{
		case FOP_ASN:
			goto flexasn;
		case FOP_IN:
		case FOP_IS_SUBSET:
		case FOP_INTERSECT_IS_EMPTY:
		case FOP_INTERSECT_IS_NOT_EMPTY:
		/* WTF support multi/var `f1' and subset/intersect
		 * properly; for now `f1' is single-item fixed
		 * (checked here), thus intersect and subset behave
		 * same:
		 */
			if (n1 != 1)
			{
				rc = FOP_EINVAL;
				break;
			}
			if(f2->issorted)
			{
				int l, r;
				l = 0;
				r = n2;
				while(l < r)
				{
					i = (l+r)/2;
					if (ISEQ(ITEM1(0), ITEM2(i)))
					{
						rc = fld2finv(f3,
						 (op!=FOP_INTERSECT_IS_EMPTY));
						return rc;
					}
					if (ISLT(ITEM1(0), ITEM2(i)))
						r = i;
					else
						l = i + 1;
				}
			}
			else for (i = 0; i < n2; i++)
			{
				if (ISEQ(ITEM1(0), ITEM2(i)))
				{
					rc = fld2finv(f3,
						(op!=FOP_INTERSECT_IS_EMPTY));
					return rc;
				}
			}
			rc = fld2finv(f3, (op == FOP_INTERSECT_IS_EMPTY));
			break;
		case FOP_INTERSECT:
		/* INTERSECT returns a set not a boolean; wtf support: */
			rc = FOP_EILLEGAL;
			break;
		case FOP_TWIXT:
			if ((f1->type & DDTYPEBITS) == FTN_LONG &&
			    (f2->type & DDTYPEBITS) == FTN_LONG)
			{
				if (n1 != 1 || n2 != 2)
					return(FOP_EINVAL);
				rc = TXcodesintersect1(ITEM1(0), ITEM2(0),
						       ITEM2(1));
				fld2finv(f3, rc);
				return 0;
			}
			rc = FOP_EINVAL;
			break;
		case FOP_ADD:
		case FOP_SUB:
		case FOP_MUL:
		case FOP_DIV:
#ifndef fonomodulo
		case FOP_MOD:
#endif /* fonomodulo */
			if (TXfldIsNull(f1) || TXfldIsNull(f2))	/* Bug 5395 */
				return(TXfldmathReturnNull(f1, f3));
			rc = FOP_EINVAL;
			break;
		case FOP_EQ:
		case FOP_NEQ:
		case FOP_LT:
		case FOP_LTE:
		case FOP_GT:
		case FOP_GTE:
		case FOP_COM:
			goto doMultiCmp;
		default:
			rc = FOP_EINVAL;
			break;
		}
	}			    /* end !(n1 > 1 || var1) && (n2 != 1) */
	else				/* !(n1 > 1 || var1) && !(n2 != 1) */
	{
		if (!(op & FOP_CMP))
		{
			TXmakesimfield(f1, f3);
			vp3 = (ft_xx *) getfld(f3, &n3);
		}
		switch (op)
		{
		case FOP_ADD:
			if (TXfldIsNull(f1) || TXfldIsNull(f2))	/* Bug 5395 */
				return(TXfldmathReturnNull(f1, f3));
			if (n1 != 1 || n2 != 1) break;
			*vp3 = ITEM1(0) + ITEM2(0);
			rc = FOP_EOK;
			break;
		case FOP_SUB:
			if (TXfldIsNull(f1) || TXfldIsNull(f2))	/* Bug 5395 */
				return(TXfldmathReturnNull(f1, f3));
			if (n1 != 1 || n2 != 1) break;
			*vp3 = ITEM1(0) - ITEM2(0);
			rc = FOP_EOK;
			break;
		case FOP_MUL:
			if (TXfldIsNull(f1) || TXfldIsNull(f2))	/* Bug 5395 */
				return(TXfldmathReturnNull(f1, f3));
			if (n1 != 1 || n2 != 1) break;
			*vp3 = ITEM1(0) * ITEM2(0);
			rc = FOP_EOK;
			break;
		case FOP_DIV:
			if (TXfldIsNull(f1) || TXfldIsNull(f2))	/* Bug 5395 */
				return(TXfldmathReturnNull(f1, f3));
			if (n1 != 1 || n2 != 1) break;
			if (ITEM2(0) == (ft_xx) 0)
			{
				/* Bug 6914: */
				if (strcmp(TX_STRINGIZE_VALUE_OF(ft_xx),
					   "ft_float") == 0)
					TXFLOAT_SET_NaN(*vp3);
				else if (strcmp(TX_STRINGIZE_VALUE_OF(ft_xx),
						"ft_double") == 0)
					TXDOUBLE_SET_NaN(*vp3);
				else
					TXfldSetNull(f3);
				rc = FOP_EDOMAIN;
			}
			else
			{
				*vp3 = ITEM1(0) / ITEM2(0);
				rc = FOP_EOK;
			}
			break;
#ifndef fonomodulo
		case FOP_MOD:
			if (TXfldIsNull(f1) || TXfldIsNull(f2))	/* Bug 5395 */
				return(TXfldmathReturnNull(f1, f3));
			if (n1 != 1 || n2 != 1) break;
			if (ITEM2(0) == (ft_xx) 0)
			{
				/* Bug 6914: */
				if (strcmp(TX_STRINGIZE_VALUE_OF(ft_xx),
					   "ft_float") == 0)
					TXFLOAT_SET_NaN(*vp3);
				else if (strcmp(TX_STRINGIZE_VALUE_OF(ft_xx),
						"ft_double") == 0)
					TXDOUBLE_SET_NaN(*vp3);
				else
					TXfldSetNull(f3);
				rc = FOP_EDOMAIN;
			}
			else
			{
				*vp3 = ITEM1(0) % ITEM2(0);
				rc = FOP_EOK;
			}
			break;
#endif
		case FOP_CNV:
			if (n1 != 1) break;
			if (var2)
				f3->type |= DDVARBIT;
			else
				f3->type &= ~DDVARBIT;
			*vp3 = ITEM1(0);
			rc = FOP_EOK;
			break;
		case FOP_ASN:
			if (n1 != 1 || n2 != 1) break;
			if(vp2)
			{
				*vp3 = ITEM2(0);
				rc = FOP_EOK;
			}
			break;
#define CHECK_MULTI_CMP				\
			if (n1 != 1 || n2 != 1)	\
			{			\
				if (TX_IPv6_ENABLED(TXApp)) goto doMultiCmp; \
				rc = FOP_EINVAL;\
				break;		\
			}
		case FOP_EQ:
			CHECK_MULTI_CMP;
#ifdef NEVER
			putmsg(999, NULL, "Comparing %d = %d", ITEM1(0), ITEM2(0));
#endif
			rc = fld2finv(f3, ISEQ(ITEM1(0), ITEM2(0)));
			break;
		case FOP_NEQ:
			CHECK_MULTI_CMP;
			rc = fld2finv(f3, !ISEQ(ITEM1(0), ITEM2(0)));
			break;
		case FOP_LT:
			CHECK_MULTI_CMP;
#ifdef NEVER
			putmsg(999, NULL, "Comparing %d < %d", ITEM1(0), ITEM2(0));
#endif
			rc = fld2finv(f3, ISLT(ITEM1(0), ITEM2(0)));
			break;
		case FOP_LTE:
			CHECK_MULTI_CMP;
			rc = fld2finv(f3, ISLTE(ITEM1(0), ITEM2(0)));
			break;
		case FOP_GT:
			CHECK_MULTI_CMP;
#ifdef NEVER
			putmsg(999, NULL, "Comparing %d > %d", ITEM1(0), ITEM2(0));
#endif
			rc = fld2finv(f3, ISGT(ITEM1(0), ITEM2(0)));
			break;
		case FOP_GTE:
			CHECK_MULTI_CMP;
			rc = fld2finv(f3, ISGTE(ITEM1(0), ITEM2(0)));
			break;
		case FOP_COM:
			CHECK_MULTI_CMP;
			rc = COM(ITEM1(0), ITEM2(0));
			rc = fld2finv(f3, rc);
			break;
		case FOP_IN:
		case FOP_IS_SUBSET:
		case FOP_INTERSECT_IS_EMPTY:
		case FOP_INTERSECT_IS_NOT_EMPTY:
		/* WTF support multi/var `f1' and subset/intersect
		 * properly; for now `f1' is single-item fixed
		 * (checked here), thus intersect and subset behave
		 * same:
		 */
			if (n1 != 1) break;
			for (i = 0; i < n2; i++)
			{
				if (ISEQ(ITEM1(0), ITEM2(i)))
				{
					rc = fld2finv(f3,
						(op!=FOP_INTERSECT_IS_EMPTY));
					return rc;
				}
			}
			rc = fld2finv(f3, (op == FOP_INTERSECT_IS_EMPTY));
			break;
		case FOP_INTERSECT:	/* returns set not boolean wtf */
			rc = FOP_EILLEGAL;
			break;
		default:
			rc = FOP_EINVAL;
			break;
		}
	}

	/* Bug 5609: (int, integer) ops use (integer, integer) func;
	 * but should get LHS base type; similar for smallint/short:
	 */
	if (rc == FOP_EOK && !(op & FOP_CMP))
	{
		FTN	desiredBaseType, curBaseType = (f3->type & DDTYPEBITS);

		switch (curBaseType)
		{
		case FTN_INT:
		case FTN_INTEGER:
		case FTN_SHORT:
		case FTN_SMALLINT:
			desiredBaseType = ((op == FOP_CNV ? f2->type :
					    f1->type) & DDTYPEBITS);
			if (desiredBaseType != curBaseType &&
			    (TXftnIsIntegral(desiredBaseType)) &&
			    ddftsize(desiredBaseType) == ddftsize(curBaseType))
				f3->type = (f3->type & ~DDTYPEBITS) |
					desiredBaseType;
			break;
		default:
			break;
		}
	}
	return (rc);
#undef CHECK_MULTI_CMP
}				/* end foxxxx() */
/**********************************************************************/

/* undefine macros now for safety; this file included multiple times: */
#undef foxxxx
#undef ft_xx
#undef GCC_ALIGN_FIX
#undef ITEM1
#undef ITEM2
