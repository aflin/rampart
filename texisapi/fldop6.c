/* -=- kai-mode: John -=- */

/* template 6: date <-> number ops
 * Macros defined by #includer:
 *   ft_xx	Numeric ft_... type
 *   fodaxx	Name of foda.. function
 *   foxxda	Name of fo..da function
 */

/* ------------------------------------------------------------------------ */

int
fodaxx(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_date	*vp1, *vp3;
	ft_xx	*vp2;
	size_t	n1, n2;

	vp1 = getfld(f1, &n1);
	vp2 = getfld(f2, &n2);
	switch(op)
	{
		case FOP_CNV:
			return foxxda(f2, f1, f3, FOP_ASN);
		case FOP_ASN:
			TXmakesimfield(f1, f3);
			vp3 = getfld(f3, &n1);
			*vp3 = (ft_date)*vp2;
			break;
		case FOP_SUB:			/* fodada would return ft_xx*/
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				return(TXfldmathReturnNull(f1, f3));
			TXmakesimfield(f1, f3);
			vp3 = getfld(f3, &n1);
			*vp3 = *vp1 - (ft_date)*vp2;
			break;
		case FOP_ADD:
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				return(TXfldmathReturnNull(f1, f3));
			TXmakesimfield(f1, f3);
			vp3 = getfld(f3, &n1);
			*vp3 = *vp1 + (ft_date)*vp2;
			break;
		default:
			return FOP_EINVAL;
	}
	return 0;
}

/* ------------------------------------------------------------------------ */

int
foxxda(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_xx  *vp1;
	ft_date *vp2;
	ft_xx  *vp3;
	size_t	n1, n2, n3, i, n;

	vp1 = getfld(f1, &n1);
	vp2 = getfld(f2, &n2);
	switch(op)
	{
		case FOP_CNV:
			return fodaxx(f2, f1, f3, FOP_ASN);
		case FOP_ASN:
			TXmakesimfield(f1, f3);
			vp3 = getfld(f3, &n3);
			/* WTF expand f3's mem if var and n2 > n3 */
			n = MIN(n2, n3);
			for (i = 0; i < n; i++)
				vp3[i] = (ft_xx)vp2[i];
			for ( ; i < n3; i++)
				vp3[i] = (ft_xx)0;
			if ((f3->type & DDVARBIT) && (n3 > n2))
			{
				f3->n = n2;
				f3->size = f3->n*f3->elsz;
			}
			return 0;
		case FOP_ADD:			/* fodada would return date */
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				return(TXfldmathReturnNull(f1, f3));
			TXmakesimfield(f1, f3);
			vp3 = getfld(f3, NULL);
			*vp3 = *vp1 + (ft_xx)(*vp2);
			break;
		case FOP_SUB:			/* fodada would return ft_xx*/
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				return(TXfldmathReturnNull(f1, f3));
			TXmakesimfield(f1, f3);
			vp3 = getfld(f3, NULL);
			*vp3 = *vp1 - (ft_xx)(*vp2);
			break;
		default:
			return FOP_EINVAL;
	}
	return(0);
}

/* undefine macros for safety next time; this file included multiple times: */
#undef ft_xx
#undef fodaxx
#undef foxxda
