#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef _WIN32
#  include <direct.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include "dbquery.h"
#include "texint.h"

#ifndef F_OK
#define F_OK 0
#define R_OK 4
#define W_OK 2
#endif


#ifdef MSDOS                                          /* MAW 02-15-94 */
static CONST char prefix[] = "turl" ;
#else
static CONST char prefix[] = ".turl" ;
#endif
static char *idir = NULL;

char *
dbgettemp()
{
	if(!idir)
#ifdef MSDOS
		idir = TXstrcat2(TXINSTALLPATH_VAL, "\\texis\\indirect");
#else
		idir = TXstrcat2(TXINSTALLPATH_VAL, "/texis/.indirect");
#endif
	return TXtempnam(idir, prefix, CHARPN);
}

/******************************************************************/

static int checkandmkdir ARGS((char *dir));
static int
checkandmkdir(dir)
char *dir;
{
	int rc;
#ifdef __MSDOS__
	int drive = _getdrive();

	if (dir[1] = ':')
	{
		_chdrive(toupper(dir[0]) - 'A' + 1);
		dir +=2;
	}
#endif

        errno = 0;
	rc = mkdir(dir
#ifndef _WIN32
                   , S_IREAD|S_IWRITE|S_IEXEC
#endif /* _WIN32 */
                   );
#ifdef __MSDOS__
	_chdrive(drive);
#endif
	if(!rc)
		return rc;
	switch(errno)
	{
		case EEXIST:
			return 0;
		default:
			putmsg(MERR, NULL, "Could not create directory %s: %s",
				dir, strerror(errno));
	}
	return rc;
}

/******************************************************************/

char *
TXgetindirectfname(ddic)
DDIC *ddic;
{
	static CONST char	trans[] = "0123456789abcdef";
	static char tempfn[PATH_MAX];
	ft_counter counter;
	int	fileno;
	char	*s, *e;

	s = ddic->indrctspc;
	tempfn[sizeof(tempfn) - 1] = 'x';		/* overflow detector*/
#ifdef MSDOS
	if (s[0] != '\0' && s[1] == ':')		/* C:... */
	{
		if (s[2] != PATH_SEP && s[2] != '/')	/* C:foo... */
			goto invalid;
		TXcatpath(tempfn, s, "");		/* C:\foo... */
	}
	else if (s[0] == PATH_SEP || s[0] == '/')	/* \foo... */
	{
	invalid:
		putmsg(MERR + UGE, CHARPN, "Invalid indirect path `%s'", s);
		return(CHARPN);
	}
#else /* !MSDOS */
	if (s[0] == PATH_SEP)
		TXcatpath(tempfn, s, "");		/* /foo... */
#endif /* !MSDOS */
	else						/* foo... */
		TXcatpath(tempfn, ddic->pname, s);	/* db-relative */
	e = tempfn + strlen(tempfn) - 1;
	if(ddic->pname == ddic->indrctspc)		/* default value */
	{
		if (e + 11 >= tempfn + sizeof(tempfn)) goto toolong;
		if (e >= tempfn && *e != PATH_SEP
#ifdef MSDOS
			&& *e != '/'
#endif /* MSDOS */
			)
			*(++e) = PATH_SEP;
		strcpy(++e, "indirects");
		e += strlen(e) - 1;
	}
	if (tempfn[sizeof(tempfn) - 1] != 'x')
	{
	toolong:
		putmsg(MERR + MAE, CHARPN, "Indirect path too long");
		return(CHARPN);
	}
	while (e >= tempfn && (*e == PATH_SEP || *e == '/'))
	{						/* chop trailing / */
		*e = '\0';
		e--;
	}
	if(checkandmkdir(tempfn) == -1) return NULL;
        strcat(tempfn, PATH_SEP_S);

	for(e = tempfn; *e; e++);

	/* We now have the root directory in tempfn for our files */

	rgetcounter(ddic, &counter, 1);

        /* Rest of path will be:
         *
         *   D0^S0 / D1 S1 / D3 D2 [S7 S6 [S5 S4 [S3]]] / counter.tmi
         *
         * where DN is nibble N of the counter.date, and SN is nibble
         * N of counter.seq.  This method attempts to ensure random
         * (even) distribution of files across the top 16 dirs (for
         * load balancing via symlinks), as well as avoiding too many
         * files/subdirs per dir, for both the short-term (fast) and
         * long-term creation of files.   KNG 000205
         */
#define NIBBLE(val, n)  (((unsigned)(val) >> ((n)*4)) & (unsigned)0xF)

	fileno = (NIBBLE(counter.date, 0) ^ NIBBLE(counter.seq, 0));
	*(e++) = trans[fileno]; *e = '\0';
	if(checkandmkdir(tempfn) == -1) return NULL;
	*(e++) = PATH_SEP; *e = '\0';

	*(e++) = trans[NIBBLE(counter.date, 1)];
	*(e++) = trans[NIBBLE(counter.seq, 1)];
        *e = '\0';
	if(checkandmkdir(tempfn) == -1) return NULL;
	*(e++) = PATH_SEP; *e = '\0';

	*(e++) = trans[NIBBLE(counter.date, 3)];
	*(e++) = trans[NIBBLE(counter.date, 2)];
        if (NIBBLE(counter.seq, 3) > 0)
          {
            if (NIBBLE(counter.seq, 4) > 0)
              {
                if (NIBBLE(counter.seq, 6) > 0)
                  {
                    *(e++) = trans[NIBBLE(counter.seq, 7)];
                    *(e++) = trans[NIBBLE(counter.seq, 6)];
                  }
                *(e++) = trans[NIBBLE(counter.seq, 5)];
                *(e++) = trans[NIBBLE(counter.seq, 4)];
              }
            *(e++) = trans[NIBBLE(counter.seq, 3)];
          }
        *e = '\0';
	if(checkandmkdir(tempfn) == -1) return NULL;
	*e = PATH_SEP; e++; *e = '\0';

	sprintf(e, "%08lx%lx.tmi", counter.date, counter.seq);
	return tempfn;
#undef NIBBLE
}

int
TXisindirect(path)
char *path;
{
	char *e, *t;
	int  possnewind = 0;

	if(!path)
		return 0;
	e = path + strlen(path);
	if((e-path) < 5)
		return 0;
	if(!strcmpi(e-4, ".tmi"))
	{
		possnewind = 1;
		for(t=e-5; t > path; t--)
		{
			if(*t == PATH_SEP)
				break;
			if(!isxdigit((int)*(unsigned char *)t))
			{
				possnewind = 0;
				break;
			}
		}
		return possnewind;
	}

/* Old style indirects; */
#ifdef MSDOS
	if (strstr(path, "\\turl") != (char *)NULL)
#else
	if (strstr(path, "/.turl") != (char *)NULL)
#endif
		return 1;
	return 0;
}
