/* -=- kai-mode: John -=- */

/******************************************************************/

int
fxxxxx(f1, f2)
FLD *f1;
FLD *f2;
{
	ft_double *p, *e, r;

	if (TXfldIsNull(f1) || TXfldIsNull(f2))
		return(TXfldmathReturnNull(f1, f1));
	e = getfld(f1, NULL);
	p = getfld(f2, NULL);
	r = xxxxx(*e, *p);
	*e = r;
	return 0;
}

/******************************************************************/
