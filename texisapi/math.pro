
/******************************************************************/

int
fxxxxx(f1)
FLD *f1;
{
	ft_double *e, r;

	if (TXfldIsNull(f1))
		return(TXfldmathReturnNull(f1, f1));

	e = getfld(f1, NULL);
	r = xxxxx(*e);
	*e = r;
	return 0;
}

/******************************************************************/
