#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef EPI_HAVE_STDARG
#  include <stdarg.h>
#endif /* EPI_HAVE_STDARG */
#include "os.h"
#include "mmsg.h"
#include "strcat.h"
#include "texint.h"
#include "cgi.h"


#ifdef MEMDEBUG
#  undef TXstrcat2
#  undef TXstrcat3
#  undef TXstrcat4
#  define FUNC(name)	mac_##name
#  define DEBUGARGS	, file, line, memo
#  define DEBUGDECL	CONST char *file; int line; CONST char *memo;
#else /* !MEMDEBUG */
#  define FUNC(name)	name
#  define DEBUGARGS
#  define DEBUGDECL
#endif /* !MEMDEBUG */


/******************************************************************/

char *
TXstrcat2(a, b DEBUGARGS)
CONST char	*a;
CONST char	*b;
DEBUGDECL
{
	size_t	needed, aLen, bLen;
	char *d, *ret;

	needed = (aLen = strlen(a)) + (bLen = strlen(b)) + 1;
	ret = (char *)FUNC(malloc)(needed DEBUGARGS);
	if(!ret)
		return ret;
	d = ret;
	memcpy(d, a, aLen);
	d += aLen;
	memcpy(d, b, bLen + 1);
	return ret;
}

/******************************************************************/

char *
TXstrcat3(a, b, c DEBUGARGS)
CONST char	*a;
CONST char	*b;
CONST char	*c;
DEBUGDECL
{
	size_t	needed, aLen, bLen, cLen;
	char *d, *ret;

	needed = (aLen = strlen(a)) + (bLen = strlen(b)) + (cLen = strlen(c))
		+ 1;
	ret = (char *)FUNC(malloc)(needed DEBUGARGS);
	if(!ret)
		return ret;
	d = ret;
	memcpy(d, a, aLen);
	d += aLen;
	memcpy(d, b, bLen);
	d += bLen;
	memcpy(d, c, cLen + 1);
	return ret;
}

/******************************************************************/

char *
TXstrcat4(a, b, c, d DEBUGARGS)
CONST char	*a;
CONST char	*b;
CONST char	*c;
CONST char	*d;
DEBUGDECL
{
	size_t	needed, aLen, bLen, cLen, dLen;
	char *dest, *ret;

	needed = (aLen = strlen(a)) + (bLen = strlen(b)) + (cLen = strlen(c))
		+ (dLen = strlen(d)) + 1;
	ret = (char *)FUNC(malloc)(needed DEBUGARGS);
	if(!ret)
		return ret;
	dest = ret;
	memcpy(dest, a, aLen);
	dest += aLen;
	memcpy(dest, b, bLen);
	dest += bLen;
	memcpy(dest, c, cLen);
	dest += cLen;
	memcpy(dest, d, dLen + 1);
	return ret;
}

/******************************************************************/

char * CDECL
TXstrcatN(TXPMBUF *pmbuf, CONST char *fn, ...)
/* Takes a NULL-terminated list of strings to concatenate together.
 */
{
	va_list	argp;
#define MAX_STRS	64
	char	*strs[MAX_STRS];
	size_t	needed, strlens[MAX_STRS], numStrs, i;
	char	*dest, *ret;

	va_start(argp, fn);
	/* Save the string args into an array, since some platforms do
	 * not like traversing a va_list more than once:
	 */
	for (numStrs = 0; numStrs < MAX_STRS; numStrs++)
		if (!(strs[numStrs] = va_arg(argp, char *))) break;
	if (numStrs >= MAX_STRS && va_arg(argp, char *))
	{
		txpmbuf_putmsg(pmbuf, MERR + MAE, fn, "Too many strings");
		return(CHARPN);
	}

	needed = 1;				/* for nul-termination */
	for (i = 0; i < numStrs; i++)
		needed += (strlens[i] = strlen(strs[i]));

	/* Due to varargs we cannot make a MEMDEBUG macro version of
	 * TXstrcatN() to automagically pass in __FILE__/__LINE__.
	 * So at least stuff `fn' into the memo field:
	 */
#ifdef MEMDEBUG
	{
		char	tmp[256];

		htsnpf(tmp, sizeof(tmp),
		       "(alloced in TXstrcatN() called from %s)", fn);
		ret = (char *)mac_malloc(needed, __FILE__, __LINE__, tmp);
	}
#else /* !MEMDEBUG */
	ret = (char *)TXmalloc(pmbuf, fn, needed);
#endif /* !MEMDEBUG */
	if (!ret) return(CHARPN);
	dest = ret;
	for (i = 0; i < numStrs; i++)
	{
		memcpy(dest, strs[i], strlens[i]);
		dest += strlens[i];
	}
	*dest = '\0';
	return(ret);
}

/* ------------------------------------------------------------------------ */

char * CDECL
TXstrNcatN(TXPMBUF *pmbuf, CONST char *fn, ...)
/* Takes a NULL-terminated list of strings and lengths to concatenate together.
 * E.g. TXstrNcatN(pmbuf, __FUNCTION__, "str", -1, "buf", 3, NULL);
 */
{
	va_list	argp;
#define MAX_STRS	64
	char	*strs[MAX_STRS];
	size_t	needed, strlens[MAX_STRS], numStrs, i;
	char	*dest, *ret;

	va_start(argp, fn);
	/* Save the string args into an array, since some platforms do
	 * not like traversing a va_list more than once:
	 */
	for (numStrs = 0; numStrs < MAX_STRS; numStrs++)
	{
		if (!(strs[numStrs] = va_arg(argp, char *))) break;
		strlens[numStrs] = va_arg(argp, size_t);
		if (strlens[numStrs] == (size_t)(-1))
			strlens[numStrs] = strlen(strs[numStrs]);
	}
	if (numStrs >= MAX_STRS && va_arg(argp, char *))
	{
		txpmbuf_putmsg(pmbuf, MERR + MAE, fn, "Too many strings");
		return(CHARPN);
	}

	needed = 1;				/* for nul-termination */
	for (i = 0; i < numStrs; i++)
		needed += strlens[i];

	/* Due to varargs we cannot make a MEMDEBUG macro version of
	 * TXstrNcatN() to automagically pass in __FILE__/__LINE__.
	 * So at least stuff `fn' into the memo field:
	 */
#ifdef MEMDEBUG
	{
		char	tmp[256];

		htsnpf(tmp, sizeof(tmp),
		       "(alloced in TXstrNcatN() called from %s)", fn);
		ret = (char *)mac_malloc(needed, __FILE__, __LINE__, tmp);
	}
#else /* !MEMDEBUG */
	ret = (char *)TXmalloc(pmbuf, fn, needed);
#endif /* !MEMDEBUG */
	if (!ret) return(CHARPN);
	dest = ret;
	for (i = 0; i < numStrs; i++)
	{
		memcpy(dest, strs[i], strlens[i]);
		dest += strlens[i];
	}
	*dest = '\0';
	return(ret);
}

/******************************************************************/

static char *
readfunctionargs(char *c)
{
	int x;
	int readfuncargs = 1;
	int allowedspace = 0;
	int inquotes = 0;

	do
	{
		x = *(++c);
		if(x == '\'')
		{
			if(!inquotes)
				inquotes = 1;
			else
			{
				x = *(++c);
				if(x != '\'')
					inquotes = 0;
			}
		}
		if(!inquotes && (isspace(x) || x == ','))
		{
			if(readfuncargs)
			{
				x = *(++c);
				allowedspace++;
				while(x && (x == ','))
				{
					allowedspace++;
					x=*(++c);
				}
				switch(x)
				{
				case '\'': inquotes ++; break;
				case '_':
				{
					switch(*(c+1))
					{
						case 'v':
							allowedspace+=2;
							break;
						case 'f':
						case 'F':
							c = readfunctionargs(c);
							break;
					}
				}
				}
				readfuncargs--; /* We're done counting */
			}
			if(allowedspace)
				allowedspace--;
			else
				break;
		}
	} while (x);
	return c-1;
}

/******************************************************************/

char *
valsplit(s)
char *s;
{
	static char *p;
	char *c, *r;
	int  x;
	int  allowedspace=0;
	int  inquotes = 0;

	if (s != (char *)NULL)
		p = s;
	c = p;
	x = *c;
	if (!x) return (char *)NULL;
	while (x && (isspace(x) || x == ','))
		x = *(++c);
	r = c;
	if (x && x == '\'')
	{
more_string:
		do x = *(++c);
		while (x && x != '\'');
		x = *(++c);
		if(x == '\'')
			goto more_string;
	}
	else
	{
		if(*c=='_')
		{
			switch(*(c+1))
			{
				case 'v':
					allowedspace=2;
					break;
				case 'f':
				case 'F':
					c = readfunctionargs(c);
					break;
			}
		}
		do
		{
			x = *(++c);
			if(x == '\'')
			{
				if(!inquotes)
					inquotes = 1;
				else
				{
					x = *(++c);
					if(x != '\'')
						inquotes = 0;
				}
			}
			if(!inquotes && (isspace(x) || x == ','))
			{
				if(allowedspace)
					allowedspace--;
				else
					break;
			}
		} while (x);
	}
	*c = '\0';
	if (x)
		p = c+1;
	else
		p = c;
	return r;
}

/******************************************************************/

char *
stripquote(si)
char *si;
{
	char	*rc;
	char	*s, *d;

	rc = malloc(strlen(si));
	if(!rc)
		return rc;
	s = si;
	d = rc;

	for(s=si+1, d = rc; *s; s++, d++)
	{
		if(*s == '\'')
		{
			if(*(s+1) == '\'')
			{
				s++;
			}
			else
				break;
		}
		*d = *s;
	}
	*d = '\0';
	return rc;
}

/******************************************************************/

int
jtstrcmp(a, b, c, d)
char *a;
char *b;
int   c;
char *d;
{
#ifdef NEVER
	putmsg(999, "strcmp", "%d in %s", c, d);
#endif
	(void)c;
	(void)d;
	return strcmp(a, b);
}

