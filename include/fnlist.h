#ifndef FN_H
#define FN_H
/**********************************************************************/
#define LISTOFILES '&'

#define FNS struct fn
FNS {
	char **av;
	FILE *fp;
	char buf[BUFSIZ];
};

#ifdef LINT_ARGS
	extern FNS *openfn(char **), *closefn(FNS *);
	extern char *getfn(FNS *);
#else
	extern FNS *openfn(), *closefn();
	extern char *getfn();
#endif
/**********************************************************************/
#endif                                                        /* FN_H */
