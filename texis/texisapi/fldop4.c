/* -=- kai-mode: John -=- */
/*
   template 4: formatting to human readable char type
*/

/**********************************************************************/
int
fochxx(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_char	*mem, *mp, *mpe;
	ft_xx *vp2, *vp2save, *e;
	size_t n1, n2, na;
	int var1;

	if (op == FOP_CNV)
	{
		return (foxxch(f2, f1, f3, FOP_ASN));
	}
	if (op != FOP_ASN)
		return (FOP_EINVAL);
	if (TXfldIsNull(f2))
		return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */
	getfld(f1, &n1);
	vp2save = (ft_xx *) getfld(f2, &n2);
	var1 = fldisvar(f1);
	na = (bpxx + 1) * n2;
tryagain:
	vp2 = vp2save;
	if (!var1)
	{
		if (n1 < na)
			return FOP_ERANGE;	/* does not fit */
		else
			na = n1;
	}
	if (na == 0) na = 1;			/* avoid malloc(0) */
	mem = (ft_char *) malloc(na);
	if (mem == (ft_char *) NULL)
		return (FOP_ENOMEM);
	for (e = vp2 + n2, mp = mem, mpe = mem + na; vp2 < e; vp2++)
	{
#ifdef GCC_ALIGN_FIX
		volatile ft_xx val2;
		volatile byte *vpp2;

		vpp2 = (byte *)vp2;
		memcpy((void *)&val2, (void *)vpp2, sizeof(ft_xx));
#endif /* GCC_ALIGN_FIX */
		if (mp != mem)			/* comma-separate values */
		{
			if (mp < mpe) *mp = ',';
			mp++;
		}
#ifdef GCC_ALIGN_FIX
		mp += fmtxx(mp, (mp < mpe ? mpe - mp : 0), val2);
#else /* GCC_ALIGN_FIX */
		mp += fmtxx(mp, (mp < mpe ? mpe - mp : 0), *vp2);
#endif /* GCC_ALIGN_FIX */
	}
	if (mp >= mpe)				/* buffer too small */
	{
		free(mem);
		na = (mp - mem) + 1;
		goto tryagain;
	}
	if (n2 == 0 && !TX_SIZE_T_VALUE_LESS_THAN_ZERO(na))
		*mem = '\0';			/* nul-terminate empty buf */
	f3->type = f1->type;
	f3->elsz = f1->elsz;
	setfld(f3, mem, na);
	if (var1)
	{
		f3->n = f3->size = mp - mem;
	}
	else
	{
		for (n2 = mp - mem; n2 < n1; n2++)
			mem[n2] = ' ';
		mem[n2 - 1] = '\0';
	}
	return (0);
}				/* end fochxx() */

/**********************************************************************/

/**********************************************************************/
int
foxxch(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_xx *vp3;
	ft_char *vp2;
	size_t n3, n2, i;
	char	*e;
	EXTRAVARS

	switch (op)
	{
	case FOP_CNV:
		return (fochxx(f2, f1, f3, FOP_ASN));
	case FOP_ASN:
		if (TXfldIsNull(f2))
			return(TXfldmathReturnNull(f1, f3));	/* Bug 5395 */
		TXmakesimfield(f1, f3);
		vp3 = (ft_xx *) getfld(f3, &n3);
		*vp3 = (ft_xx)0;
		vp2 = (ft_char *) getfld(f2, &n2);
		if (!vp2) return(FOP_EINVAL);
		if (n3 > 0 && cvtxx(vp2, vp3) == 0)	/* JMT 961219 - check convert */
		{
			*vp3 = (ft_xx)0;
		}
		/* KNG 20060623 f1 could be multi-value; trim or clear: */
		if (f3->type & DDVARBIT)
		{
			if (n3 > 1)
			{
				f3->n = 1;
				f3->size = f3->n*f3->elsz;
			}
		}
		else
		{
			for (i = 1; i < n3; i++)
				vp3[i] = (ft_xx)0;
		}
		return 0;
	default:
		return (FOP_EINVAL);
	}
}				/* end foxxch() */

/**********************************************************************/

/* undef all macros now for safety; this file included multiple times: */
#undef bpxx
#undef cvtxx
#undef fmtxx
#undef fochxx
#undef foxxch
#undef ft_xx
