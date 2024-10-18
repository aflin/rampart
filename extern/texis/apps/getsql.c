/*
 * $Log$
 * Revision 1.9  2006/10/23 17:26:40  kai
 * Private: Fix, Low, SQL: tsql: leading spaces at start of SQL statement
 *   caused later lines to be truncated by that amount, or duplicated
 *
 * Revision 1.8  2006/06/17 03:20:33  kai
 * Minor fix: print newline if blank line
 *
 * Revision 1.7  2006/06/17 03:03:54  kai
 * Suppress blank lines from -i
 *
 * Revision 1.6  2005/11/14 18:43:52  kai
 * Compile warning fixes
 * Added `pflineno' parameter to getsql(): track and print file lineno
 *
 * Revision 1.5  2002/05/03 16:27:40  kai
 * End out with a newline if printing prompts
 *
 * Revision 1.4  2001-12-28 17:16:29-05  john
 * Use config.h
 *
 */

/* Get a sql statement.  Needs to allow:

	- reading from file/standard in.
	- being given a buffer.
	- return a single sql query at a time
*/

#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#ifdef unix
#include <unistd.h>
#endif
#ifdef MSDOS
#include <conio.h>
#include <io.h>
#endif
#include "dbquery.h"
#include "texint.h"

static char s1[MAXINSZ];
static  STRBUF *text = NULL;
extern int lineno; /* For lex */

int
TXclosesqlbuf()
{
	text=closestrbuf(text);
	return(0);
}

char *
getsql(init, sfile, showprompt, havestmnt, pflineno)
char *init; /* Initializer */
FILE *sfile;
int showprompt;
int havestmnt;
int	*pflineno;	/* (in/out) non-NULL: file lineno (print and use) */
{
        int     llineno=1, inq, inc, snd;
        static  int seenstmnt = 0;
	char	*s2 = CHARPN;
	static	int textoff = 0;
	char	*rc, *t, *lsc, *t1;
	size_t	len;
	static	char	tc, *wtc;
	static  int lninc = 0;

	lineno += lninc;
	lninc = 0;
	if(!tc)
	{
		text = closestrbuf(text);
		textoff = 0;
	}
	if(!text)
		text = openstrbuf();

	if(tc && wtc)
	{
		*wtc = tc;
		wtc = NULL;
		tc = '\0';
	}
        if (havestmnt)
        {
                if (!seenstmnt)
                {
			addstrbuf(text, init, -1);
			seenstmnt++;
		}
		else
			if(!text->data)
				return 0;
        }
	rc = text->data + textoff;
	while(rc && *rc && (isspace(*rc) || *rc == ';')) rc ++;	/* Chomp whitespace */
	lsc = rc;
	do {
		if(lsc)
			t = strchr(lsc, ';');
		else
			t = NULL;
		if(!t)
		{
			if(havestmnt)
			{
				addstrbuf(text, ";", 1);
			}
			else
			{
				if (showprompt)
				{
					if (pflineno != INTPN)
						printf("SQL %d line %d> ",
							llineno, *pflineno);
					else
						printf("SQL %d>", llineno);
				}
				if(fgets(s1, sizeof(s1), sfile) == (char *)NULL)
				{
					if (rc && (strlen(rc) > (size_t)0))
					{
						text->len -= strlen(rc);
						rc[0] = '\0' ;
						llineno = 1;
						puts("");
						continue;
					}
					if (showprompt) puts("");  /* KNG */
					return 0;
				}
				else if (pflineno != INTPN)
					(*pflineno)++;
				s2 = s1;
				if(llineno == 1)
					for ( ; *s2 && isspace(*s2); s2++)
					{
						if(*s2 == '\n')
							lineno++;
					};
				addstrbuf(text, s2, -1);
				rc = text->data + textoff;
/*
				while(rc && *rc && (isspace(*rc) || *rc == ';')) rc ++;	*/ /* Chomp whitespace */
				lsc = rc;
				len = strlen(s2);
				if(showprompt>1)  /* MAW 06-08-94 - echo */
				{
					fputs(s2, stdout);
					if (len == 0 || s2[len - 1] != '\n')
						fputs("\n", stdout);
				}
				if(len > 0)
				{
					/*
					addstrbuf(text, "\n", -1);
					*/
					llineno++;
				}
				else
					addstrbuf(text, "\n", -1);
			}
		}
		else
		{
			inq = 0;
			snd = 0;
			inc = 0;
			for(t1 = rc; t1 < t; t1++)
			{
				switch(*t1)
				{
				case '\'':
					if(!inc)
						inq = 1 - inq;
					break;
				case '\n':
					inc = 0;
					break;
				case '-':
					if(!inq)
						snd++;
					if(snd == 2)
						inc = 1;
					break;
				default:
					snd = 0;
				}
			}
			if(inq || inc)
			{
                                if (havestmnt && *(t+1)=='\0') break;
				lsc = t+1;
				t = NULL; 
			}
		}
        }
        while(!t);
	for(wtc=t+1; *wtc && isspace(*wtc); wtc++)
	{
		if(*wtc == '\n') lninc++;
		*wtc = '\0';
	}
	if(!*wtc)
	{
		wtc = NULL;
		tc = '\0';
		textoff = 0;
	}
	else
	{
		tc = *wtc;
		*wtc = '\0';
		textoff = wtc - text->data;
	}
	while(rc && *rc && (isspace(*rc) || *rc == ';')) rc ++;	/* Chomp whitespace */
        return rc;
}

#ifdef TEST
main(argc, argv)
int argc;
char **argv;
{
	char *rc;

	if(argc ==1)
		while(rc = getsql(NULL, stdin, 1, 0))
			puts(rc);
	else
		while(rc = getsql(argv[1], NULL, 1, 1))
			puts(rc);
}

#endif
