/* -=- kai-mode: john -=- */

/* foinsl() and foslin() functions; included multiple times for other
 * numeric types.
 * Macros:
 * foxxsl	numeric-strlst func name eg. foinsl
 * foslxx	strlst-numeric func name eg. foslin
 * foslxx_fn	string name eg. "foslin"
 * ft_xx	ft_... type eg. ft_int
 * ctype	C type for printf arg eg. int
 * fmt		htsnpf() format string for `ctype' eg. "%d"
 * cvtxx(a, b)	eg. (*(b) = (ft_xx)strtol((a), &e, -1), e > (char *)(a))
 * strsz	max string length of a `type' value eg EPI_OS_INT_BITS/3 + 3
 */

int
foxxsl(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static const char	fn[] = foxxsl_fn;
	char *t, *e, *es;
	ft_strlst *vp2;
	ft_xx *vp3;
	size_t n1, n2, n3;
	EXTRAVARS

	vp2 = (ft_strlst *) getfld(f2, &n2);
	switch (op)
	{
	case FOP_CNV:
		return(foslxx(f2, f1, f3, FOP_ASN));
	case FOP_ASN:
		n1 = n3 = 0;
		/* KNG 20060614 buffer might be shorter than strlst struct: */
		t = vp2->buf;			/* safe: just pointer math */
		if (n2 < sizeof(ft_strlst))	/* too short */
			es = t;
		else
		{
			es = t + vp2->nb - 1;
			if (es > (char *)vp2 + n2) es = (char *)vp2 + n2;
		}
		for ( ; t < es; t++)
			if (*t == '\0')
				n3++;
		/* putmsg(MINFO, Fn, "Found %d strings", n3); */
		/* KNG 20041020 do not alloc 0: */
		/* KNG 20160318 alloc +1 for nul-term. for setfldandsize(): */
		vp3 = (ft_xx *) TXcalloc(TXPMBUFPN, fn, n3 + 1,
					sizeof(ft_xx));
		for (t = vp2->buf; t < es; t++)
		{
			/* WTF fail if cvtxx fails?  for now set value 0 */
			if (!cvtxx(t, vp3 + n1)) vp3[n1] = (ft_xx)0;
			n1++;
			/* putmsg(MINFO, Fn, "Converting %s strings", t); */
			t += strlen(t);
		}
		TXmakesimfield(f1, f3);
		f3->type |= DDVARBIT;
		setfldandsize(f3, vp3, n3*sizeof(ft_xx) + 1, FLD_FORCE_NORMAL);
		return 0;
	default:
		return FOP_EINVAL;
	}
}

int
foslxx(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static CONST char	fn[] = foslxx_fn;
	ft_strlst		*vp3;
	ft_xx			*i2;
	size_t			n2, na, i;
	char			*d, buf[strsz];

	switch (op)
	{
	case FOP_CNV:
		return foxxsl(f2, f1, f3, FOP_ASN);
	case FOP_ASN:				/* f3 = (strlst)f2 */
		i2 = (ft_xx *)getfld(f2, &n2);
		if (!i2) n2 = 0;
		na = TX_STRLST_MINSZ + 2;	/* +2: term-str term-fld */
		for (i = 0; i < n2; i++)	/* determine size needed */
			na += htsnpf(buf, sizeof(buf), fmt, (ctype)i2[i])+1;
		if (na < sizeof(ft_strlst) + 1) na = sizeof(ft_strlst) + 1;
		vp3 = (ft_strlst *)TXcalloc(TXPMBUFPN, fn, na, 1);
		if (!vp3)
			return(FOP_ENOMEM);
		for (d = vp3->buf, i = 0; i < n2; i++)
			d += htsnpf(d, (char*)vp3+na-d, fmt, (ctype)i2[i])+1;
		*(d++) = '\0';			/* add terminating string */
		vp3->nb = d - vp3->buf;
		vp3->delim = TxPrefStrlstDelims[0];
		TXmakesimfield(f1, f3);		/* set f3's type to strlst */
		setfldandsize(f3, vp3, na, FLD_FORCE_NORMAL);
		return(0);
	default:
		return FOP_EINVAL;
	}
}

/* this file #included multiple times; avoid redefinition warnings: */
#undef foxxsl
#undef foslxx
#undef foslxx_fn
#undef foxxsl_fn
#undef ft_xx
#undef ctype
#undef fmt
#undef cvtxx
#undef strsz
