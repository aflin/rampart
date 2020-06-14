/*
 * $Log$
 * Revision 1.4  2001/03/20 16:10:53  mark
 * add stdlib for malloc()
 *
 * Revision 1.3  2001-03-19 09:53:25-05  john
 * *** empty log message ***
 *
 * Revision 1.2  2001-03-13 14:00:48-05  kai
 * Bugfix: don't loop past 2nd/3rd arg ends
 *
 * Revision 1.1  2001-03-13 13:38:39-05  kai
 * Initial revision
 *
 */

#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include "dbquery.h"

/******************************************************************/

int
rmcommon(char *a, char *b, int maxrm, int minrm, char **nstart, size_t *len)
{
	byte *wordc = pm_getwordc();
	char *ta, *tb, *tc, *te;
	int i;

	if(!a || !b || !nstart || !len)
		return -1;
	*nstart = a;
	for(i = 0;
	    (maxrm < 0 || i < maxrm) && a[i] && (a[i] == b[i]);
	    i++)
	{
		if(minrm <= i)
			*nstart = a+i+1;
	}
	while(*nstart > a)
	{
		if(wordc[*(byte *)(*nstart-1)])
			*nstart = *nstart - 1;
		else
			break;
	}
	if('\0' == **nstart)
	{
		*len = 0;
		return 0;
	}
	tc = te = ta = *nstart + strlen(*nstart) - 1;
	tb = b + strlen(b) - 1;
	i = 0;
	while (tc > *nstart && *tc == *tb && (0 > maxrm || i < maxrm))
	{
		if(minrm <= i)
			ta--;
		tc--;
		tb--;
		i++;
	}
	while (ta < te)
	{
		if(wordc[*(byte *)(ta+1)])
			ta++;
		else
			break;
	}
	*len = ta - *nstart + 1;
	return 0;
}

/******************************************************************/

char **
VXrmcommon(int argc, char **argv[])
{
	static char *Fn = "VXrmcommon";
	char **rc = NULL, *trc;
	char **data, **vtemplate, **maxrm = CHARPPN;
	char *otemplate = "";
	int i, wc, template_done = 0, maxrm_done = 0, omaxrm = MAXINT;
	size_t len;

	if(2 > argc || argc > 3)
	{
		putmsg(MERR,NULL,"Usage: <rmcommon $data $template [$maxrm]>");
		return NULL;
	}
	data = argv[0];
	vtemplate = argv[1];
	if(argc == 3)
		maxrm = argv[2];
	for(wc = 0; data[wc]; wc++);
	rc = (char **)calloc(wc+1, sizeof(char *));
	if(rc == CHARPPN)
	{
		len = (wc+1)*sizeof(char *);
	merr:
		putmsg(MERR + MAE, Fn, "Can't alloc %d bytes of memory", len);
		return(NULL);
	}
	for(i = 0; data[i]; i++)
	{
		if(template_done || !vtemplate[i])
		{
			template_done = 1;
		}
		else
		{
			otemplate = vtemplate[i];
		}
		if(maxrm_done || 2 >= argc || !maxrm[i])
		{
			maxrm_done = 1;
		}
		else
		{
			omaxrm = atoi(maxrm[i]);
		}
		rmcommon(data[i], otemplate, omaxrm, 0, &trc, &len);
		if ((rc[i] = malloc(++len)) == CHARPN)
		{
			for (i--; i >= 0; i--) free(rc[i]);
			free(rc);
			goto merr;
		}
		TXstrncpy(rc[i], trc, len);
	}
	return rc;
}

/******************************************************************/

#ifdef TEST

char *first[] = {"Here is some random text that ends with it.",
	         "And now for something completely different.",
		 NULL
		 };

char *second[] = {"Here is some more text that also ends with it.",
	         "Dead Parrots",
		 NULL
		 };

char *third[] = {"10", NULL};

main()
{
	char **a, **b, *c;
	char ***x;
	size_t len;

	for(a=first, b = second; *a; a++, b++)
	{
		rmcommon(*a, *b, 0, &c, &len);
		printf("MAX=0  A=%s\nB=%s\nC=%.*s\n", *a, *b, len, c);
		rmcommon(*a, *b, 10, &c, &len);
		printf("MAX=10 A=%s\nB=%s\nC=%.*s\n", *a, *b, len, c);
	}
	printf("VXRMCOMMON\n");
	x = (char ***) calloc(4, sizeof(char **));
	x[0] = first;
	x[1] = second;
	a = VXrmcommon(2, x);
	for(b = a; *b; b++)
		printf("B=|%s|\n", *b);
	x[2] = third;
	a = VXrmcommon(3, x);
	for(b = a; *b; b++)
		printf("B=|%s|\n", *b);
}


#endif
