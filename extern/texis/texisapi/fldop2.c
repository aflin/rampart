

/* template 2: handles large arg1 and small arg2
 * Macros to set when #including this file:
 *   foxxyy      Function to create, for large xx and small yy
 *   ft_xx       Type of arg1 xx
 *   ft_yy       Type of arg2 yy
 *   demote      fld2...() demote function to FTN type of yy
 *   promote     fld2...() promote function to FTN type of xx
 *   fonomodulo  Define if FOP_MOD should not be supported
 *   GCC_ALIGN_FIX  Define to work around gcc alignment bus error
 */

/**********************************************************************/
int
foxxyy(f1,f2,f3,op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
#ifdef GCC_ALIGN_FIX
ft_xx val1;
#  ifdef __GNUC__
	/* gcc tries very hard to optimize this memcpy() into an
	 * inline ft_xx assignment, with possible bus error results on
	 * Sparc.  It appears we need true byte-pointer args to memcpy()
	 * to force a byte-wise copy.  See also FDBI_TXALIGN_RECID_COPY():
	 */
	volatile byte	*itemSrc, *itemDest;
#    define ITEM1(i)	(itemSrc = (volatile byte *)&vp1[(i)],		\
			 itemDest = (volatile byte *)&val1,		\
			 memcpy(itemDest, itemSrc, sizeof(ft_xx)),	\
			 val1)
#  else /* !__GNUC__ */
#    define ITEM1(i)	(memcpy(&val1, &vp1[(i)], sizeof(ft_xx)), val1)
#  endif /* !__GNUC__ */
#else /* !GCC_ALIGN_FIX */
#  define ITEM1(i)	(vp1[(i)])
#endif /* !GCC_ALIGN_FIX */
ft_xx *vp1;
ft_yy *vp2;
ft_xx *vp3;
size_t n1, n2, n3;
int var1, var2;
int rc=0;

   vp1 = (ft_xx *)getfld(f1, &n1);
   vp2 = (ft_yy *)getfld(f2, &n2);

   if (TXfldIsNull(f1) || TXfldIsNull(f2))      /* Bug 5395 */
     {
       switch (op)
         {
         case FOP_ADD:
         case FOP_SUB:
         case FOP_MUL:
         case FOP_DIV:
         case FOP_MOD:
           return(TXfldmathReturnNull(f1, f3));
         case FOP_COM:
             return(fld2finv(f3, TX_FLD_NULL_CMP(vp1, vp2,
                    (n1 == 1 && n2 == 1 ? COM(ITEM1(0), (ft_xx)*vp2) : -1))));
         default:
           if (op & FOP_CMP)                    /* if comparison operator */
             return(TXfldmathReturnNull(f1, f3));
           break;
         }
     }

   if (!(op & FOP_CMP))
     TXmakesimfield(f1, f3);
   vp3=(ft_xx *)getfld(f3,&n3);
   var1=fldisvar(f1);
   var2=fldisvar(f2);
   if(n1>1 || var1){
      switch(op){
#ifdef NEVER
      case FOP_ADD: rc=promote(f2, f3);
                    if(rc==0) rc=varcat(f1,f2);/* wtf - trim if fixed?? */
                    break;
#endif /* NEVER */
      case FOP_CNV:
        rc = demote(f1, f3);                    /* f3 = (ft_yy)f1 */
        if (rc != 0) return(FOP_EINVAL);        /* error */
        if (var2)
          f3->type |= DDVARBIT;
        else
          {
            f3->type &= ~DDVARBIT;
            /* KNG 20110225 `f2' is not var, so `f3->n' must equal `f2->n': */
            if (f3->n > f2->n)                  /* truncate `f3' */
              {
                f3->n = f2->n;
                f3->size = f2->size;
              }
            else if (f3->n < f2->n)             /* expand `f3' */
              {
                ft_yy   *newMem;

                newMem = (ft_yy *)TXcalloc(TXPMBUFPN, __FUNCTION__,
                                           /* +1 for fldmath nul: */
                                           f2->n + 1, sizeof(ft_yy));
                if (newMem == (ft_yy *)NULL) return(FOP_ENOMEM);
                memcpy(newMem, f3->v, f3->n*sizeof(ft_yy));
                /* Clear out new members: */
                memset(newMem + f3->n, 0, ((f2->n - f3->n) +1)*sizeof(ft_yy));
                setfldandsize(f3, newMem, f2->n*sizeof(ft_yy) + 1, FLD_FORCE_NORMAL);
              }
          }
        break;
#ifdef NEVER
      case FOP_ASN: if(var1){
                       rc=promote(f2, f3);
                       if(rc==0) FLDSWPV(f1,f2);
                    }else{
                       n1=MIN(n1,n2);
                       for(i=0;i<n1;i++) vp3[i]=(ft_xx)vp2[i];
                    }
                    break;
#endif
      default     : rc=FOP_EINVAL;
      }
   }else{                                       /* n1 <= 1 && !var1 */
     /* Make sure we do not walk off `vp1' or `vp2' array ends: */
     if ((n1 == 1 || op == FOP_CNV || op == FOP_ASN) &&
         (n2 == 1 || op == FOP_CNV || op == FOP_IN || op == FOP_IS_SUBSET ||
          op == FOP_INTERSECT_IS_EMPTY || op == FOP_INTERSECT_IS_NOT_EMPTY ||
          op == FOP_INTERSECT))
      switch(op){
      case FOP_ADD: *vp3=ITEM1(0)+(ft_xx)*vp2; break;
      case FOP_SUB: *vp3=ITEM1(0)-(ft_xx)*vp2; break;
      case FOP_MUL: *vp3=ITEM1(0)*(ft_xx)*vp2; break;
      case FOP_DIV:
        if ((ft_xx)*vp2 == (ft_xx)0)
          {
            /* Bug 6914: */
            if (strcmp(TX_STRINGIZE_VALUE_OF(ft_xx), "ft_float") == 0)
              TXFLOAT_SET_NaN(*vp3);
            else if (strcmp(TX_STRINGIZE_VALUE_OF(ft_xx), "ft_double") == 0)
              TXDOUBLE_SET_NaN(*vp3);
            else
              TXfldSetNull(f3);
            rc = FOP_EDOMAIN;
          }
        else
          *vp3 = ITEM1(0) / (ft_xx)*vp2;
        break;
#     ifndef fonomodulo
      case FOP_MOD:
        if ((ft_xx)*vp2 == (ft_xx)0)
          {
            /* Bug 6914: */
            if (strcmp(TX_STRINGIZE_VALUE_OF(ft_xx), "ft_float") == 0)
              TXFLOAT_SET_NaN(*vp3);
            else if (strcmp(TX_STRINGIZE_VALUE_OF(ft_xx), "ft_double") == 0)
              TXDOUBLE_SET_NaN(*vp3);
            else
              TXfldSetNull(f3);
            rc = FOP_EDOMAIN;
          }
        else
          *vp3 = ITEM1(0) % (ft_xx)*vp2;
        break;
#     endif
      case FOP_CNV:
        {
          FTN   orgF2Type = f2->type;           /* save it; f3 may == f2 */

          /* Bug 5603: int64 -> varlong was returning long: */
          f3->type &= DDTYPEBITS;
          f3->type |= (orgF2Type & ~DDTYPEBITS);
          rc = demote(f1, f3);
          /* Bug 5603: fo..in() sometimes used for fo..ir(); fix type: */
          if (ddftsize(orgF2Type) == ddftsize(f3->type) &&  /* sanity check */
              ((orgF2Type & DDTYPEBITS) == FTN_INTEGER ||
               (orgF2Type & DDTYPEBITS) == FTN_SMALLINT))
            {
              f3->type &= ~DDTYPEBITS;
              f3->type |= (orgF2Type & DDTYPEBITS);
            }
        }
        break;
      case FOP_ASN: *vp3=(ft_xx)*vp2; break;
      case FOP_EQ : rc=fld2finv(f3, ISEQ(ITEM1(0), (ft_xx)*vp2));  break;
      case FOP_NEQ: rc=fld2finv(f3, !ISEQ(ITEM1(0), (ft_xx)*vp2)); break;
      case FOP_LT : rc=fld2finv(f3, ISLT(ITEM1(0), (ft_xx)*vp2));   break;
      case FOP_LTE: rc=fld2finv(f3, ISLTE(ITEM1(0), (ft_xx)*vp2)); break;
      case FOP_GT : rc=fld2finv(f3, ISGT(ITEM1(0), (ft_xx)*vp2));  break;
      case FOP_GTE: rc=fld2finv(f3, ISGTE(ITEM1(0), (ft_xx)*vp2)); break;
      case FOP_COM:
		    rc = COM(ITEM1(0), (ft_xx)*vp2);
		    rc = fld2finv(f3, rc);
		    break;
      case FOP_IN :
      case FOP_IS_SUBSET:
      case FOP_INTERSECT_IS_EMPTY :
      case FOP_INTERSECT_IS_NOT_EMPTY :
        /* WTF support multi/var `f1' and subset/intersect properly;
         * for now `f1' is single-item fixed (checked above), thus
         * intersect and subset behave same:
         */
      {
	size_t i;

	for(i=0; i < n2; i++)
	{
		if(ITEM1(0) == (ft_xx)vp2[i])
		{
			rc = fld2finv(f3, (op != FOP_INTERSECT_IS_EMPTY));
			return rc;
		}
	}
	rc = fld2finv(f3, (op == FOP_INTERSECT_IS_EMPTY));
	break;
      }
      case FOP_INTERSECT:
        /* INTERSECT returns a set not boolean; wtf support: */
        rc = FOP_EILLEGAL;
        break;
      default     : rc=FOP_EINVAL; break;
      }
     else
       rc = FOP_EINVAL;
   }
   return(rc);
}                                                     /* end foxxyy() */
/**********************************************************************/

/* Undefine macros used in this file: this file included multiple times: */
#undef ITEM1
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#undef fonomodulo
#undef GCC_ALIGN_FIX
