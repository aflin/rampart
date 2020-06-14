#include "ctype.h"
/* Convert a string to lower case */
/* This does an inplace lowering of the string */
/* returns a pointer to the string */

char *
strlwr(s)
char *s;	/* The string to change */
{
	unsigned char *p;

	for(p = (unsigned char *)s; *p; p++)
		*p = tolower((int)*p);
	return s;
}
