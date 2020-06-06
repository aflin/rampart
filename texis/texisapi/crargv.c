#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <ctype.h>

/**********************************************************************/

char **
TXcreateargv(cmdline, ac)
char	*cmdline;
int	*ac;
{
	int q, n = 0, i;
	char *opt=(char *)NULL, *p, *s, *d, **argv=(char **)NULL;

	if(ac)
		*ac=0;
	for(i=0;i<2;i++)
	{
		opt = cmdline;
		if(i==1)
		{
			if((argv=(char **)calloc(n+2,sizeof(char *)))==(char **)NULL)
			{
				goto zmemerr;
			}
		}
		for(p=opt,n=0;*p!='\0';p++,n++)
		{
			for(;isspace((int)*(unsigned char *)p);p++) ;
			for(s=d=p,q=0;*p!='\0';p++)
			{
				if(*p=='"')
				{
					q=!q;
				}
				else
				{
					if(*p=='\\' && *(p+1)=='"')
					{
						if(i == 1)
							*(d++)='"';
						p++;
					}
					else
					{
						if(!q && isspace((int)*(unsigned char *)p))
							break;
						else
							if(i == 1)
								*(d++)= *p;
					}
				}
			}
			if(p==s)
				break;
			if(*p=='\0')
				p--;
			if(i==1)
			{
				*d='\0';
				argv[n]=s;
			}
		}
	}
	argv[n]=NULL;
	if(ac)
		*ac=n;
	return(argv);
	zmemerr:
	if(argv!=(char **)NULL)
		free(argv);
	return((char **)NULL);
}                                                /* end TXcreateargv() */

/******************************************************************/
