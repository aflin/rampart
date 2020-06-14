/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef _WIN32
#  include <conio.h>
#  include <io.h>
#endif
#ifdef NEED_GETOPT_H
#  ifdef NEED_EPIGETOPT_H
#    include "epigetopt.h"
#  else /* !NEED_EPIGETOPT_H */
#    include <getopt.h>
#  endif /* !NEED_EPIGETOPT_H */
#endif
#if defined(sparc) && !defined(__SVR4)
extern int getopt ARGS((int argc,char **argv,char *opts));
extern char *optarg;
extern int optind, opterr, optopt;
#endif
#include "texint.h"
#include "passwd.h"
#include "cgi.h"				/* for htpf() */

static char *datasource = ".";
static char *uid = "";
static char *passwd = "";
static char *syspass = "";

static char stmnt[MAXINSZ];

STRBUF *readbuf;
STRBUF *current;

#define NAMESZ 80
char name[NAMESZ];

HENV    henv = NULL;
HDBC    hdbc = NULL;
HSTMT   hstmt = NULL;
static  FLDOP   *fo=NULL;

extern APICP *globalcp;
extern int  sqlhelp;

static char *lstmnt;

/******************************************************************/

#ifdef /*MEMDEBUG*/ NEVER

int epilocmsg(n)
int n;
{
	return 0;
}

#endif

char *getsql ARGS((char *, FILE *, int, int, int *pflineno));

/******************************************************************/

#ifdef NEVER
{
	if(!current)
		current=openstrbuf();
	clearstrbuf(current);
}
#endif
/******************************************************************/
static int
displayhead(HSTMT hstmt, int col, int width, int qt)
{
	LPSTMT lpstmt = (LPSTMT)hstmt;
	int widthout;

	if (!lpstmt->outtbl)
					return -1;
	if (col)
	{
		if(col>1)
			widthout = tup_sdisp_head(lpstmt->outtbl,col,qt);
		else
			widthout = tup_cdisp_head(lpstmt->outtbl,width);
	}
	else
	{
		widthout = tup_disp_head(lpstmt->outtbl,width);
	}
	return widthout;
}

/******************************************************************/
static void displayrow(HSTMT hstmt, int col, int width, int qt,
                       int showCounts);

static void
displayrow(hstmt, col, width, qt, showCounts)
HSTMT   hstmt;
int     col;
int     width;
int qt;
int     showCounts;
{
  LPSTMT lpstmt = (LPSTMT)hstmt;

  if (!lpstmt->outtbl)
          return;
  if (col)
	{
		if(col>1)
			tup_sdisp(lpstmt->outtbl, width>0?1:0, col, qt, fo);
		else
			tup_cdisp(lpstmt->outtbl, width, fo);
	} else {
  	tup_disp(lpstmt->outtbl, width, fo);
	}

	if (showCounts)
	{
		TXCOUNTINFO	countInfo;

		TXsqlGetCountInfo(hstmt, &countInfo);
                htpf("Matched min/max: %wd/%wd\tReturned min/max: %wd/%wd\n",
                     (EPI_HUGEINT)countInfo.rowsMatchedMin,
                     (EPI_HUGEINT)countInfo.rowsMatchedMax,
                     (EPI_HUGEINT)countInfo.rowsReturnedMin,
                     (EPI_HUGEINT)countInfo.rowsReturnedMax);
  }
/*
        dreason(lpstmt->query);
*/
}

/******************************************************************/

static void version ARGS((void));
static void
version()
{
#ifdef STEAL_LOCKS
	printf("Internal version.  Not for external use.\n");
#endif
        htpf(
"Texis Version %s(%aT) Copyright (c) 1988-2020 Thunderstone EPI\n\n",
                TXtexisver(), "%Y%m%d", (time_t)TxSeconds);
}

/******************************************************************/

static void usage ARGS((void));

static void
usage()
{
        version();
        puts("Usage: tsql [-a command] [-c [-w width]] [-l rows] [-hmqrv?]");
	puts("            [-d database] [-u username] [-p password] [-i file]");
	puts("            [-R profile] sql-statement");
	puts("Options:");
	puts("  --install-dir[-force]" EPI_TWO_ARG_ASSIGN_OPT_USAGE_STR
	     "dir    Alternate installation dir");
	fprintf(stdout, "                               (default is `%s')\n", TXINSTALLPATH_VAL);
	puts("  --texis-conf" EPI_TWO_ARG_ASSIGN_OPT_USAGE_STR
	     "file            Alternate " TX_TEXIS_INI_NAME " file");
        puts("  -a command       Enter Admin mode; respects -d -u -p");
        puts("      Commands:");
        puts("         (A)dd     add a user");
        puts("         (C)hange  change a password");
        puts("         (D)elete  delete a user");
        puts("  -h               Suppress column headings");
        puts("  -v               Display inserted/deleted rows");
        puts("  -n               Display total row count with -v");
        puts("  -q               Suppress SQL> prompt");
        puts("  -c               Format one field per line");
#ifdef MEMDEBUG
        puts("  -C               Do MEMDEBUG check");
        puts("  -M i             Set MEMDEBUG alloc index # for extra reporting");
        puts("  -W               Set MEMDEBUG mac_won()");
#endif /* MEMDEBUG */
#ifdef DEBUG
        puts("  -D i             Set TXDebugLevel i");
#endif /* DEBUG */
        puts("  -w width         Make field headings width characters long");
        puts("                   (0: align to longest heading)");
        puts("  -l rows          Limit output to rows");
        puts("  -s rows          Skip rows of output");
        puts("  -u username      Login as username");
        puts("                   (if -p not given, password will be prompted for)");
        puts("  -p password      Login using password");
        puts("  -P password      _SYSTEM password for admin");
        puts("  -d database      Use database as the data dictionary");
        puts("  -m               Create the database named with -d");
        puts("  -i file          Read SQL commands from file instead of the keyboard");
	puts("  -r               Read default profile");
	puts("  -R profile       Read specified profile");
        puts("  -f delim         Specify a delimiter or format; 1 or 2 chars:");
        puts("     t             same as -c option");
        puts("     c             default behavior");
        puts("     other char    field separator e.g. `-f ,' for CSV");
        puts("                   (follow with q/n to suppress quotes/newlines)");
        puts("  -t               Show timing information");
        puts("  -V               Increase Texis verbosity (may be used multiple times)");
	puts("  -x               Debug: do not capture ABEND etc. signals");
	puts("  --show-counts    Show min/max counts of rows matched and returned");
	puts("  --lockverbose n  Set lockverbose (tracing) level n");
	puts("  --timeout n[.n]  Set timeout of n[.n] seconds (-1 none; may cause corruption)");
        puts("  -?               Show this help");
        exit(TXEXIT_INCORRECTUSAGE);
}

/******************************************************************/

#ifdef _WIN32
static HANDLE hMainThread;
#endif

/******************************************************************/

#ifdef MEMDEBUG
extern int TXcloseabsrex ARGS((void));  /* WTF */
#endif

static void CDECL
TXtsqlTimeoutHandler(void *usr)
{
	/* Bug 3774
	 * WTF gracefully get out of locks
	 * WTF ignore timeout and/or finish up SQL if in write lock
	 * WTF return to main instead of exit()ing
	 */
	(void)usr;
	TX_PUSHERROR();

	if (TXgetSemlockCount() > 0)
	{
		/* We have a semlock and are thus modifying the lock
		 * structure, even if only for read locks.  Let it
		 * finish to avoid corrupting locks.
		 */
		putmsg(MWARN, __FUNCTION__, "Timeout: will exit soon");
		/* Set a callback (to us) to exit on semunlock(): */
		TXsetSemunlockCallback(TXtsqlTimeoutHandler, NULL);
	}
	else
	{
		putmsg(MERR, __FUNCTION__, "Timeout: exiting");
		_exit(TXEXIT_TIMEOUT);
	}
	TX_POPERROR();
}

TXEXIT CDECL
TXtsqlmain(argc, argv, argcstrip, argvstrip)
int argc, argcstrip;
char **argv, **argvstrip;
{
	static const char	requiresArgFmt[] =
	"Option `%s' requires argument (-? for help)\n";
	FILE    *sfile;
	int     closesfile = 0;
#ifdef _WIN32
	int     showprompt = _isatty(_fileno(stdin));
#else
	int     showprompt = isatty(fileno(stdin));
#endif
	int     makeit = 0;
	int     showhead = 1;
	int     havestmnt = 0;
	int     colm = 0;
	int     width = -1;
	int			argswidth = -1;
	int     rowlim = -1;
	int	rowskip = 0;
	int     nrows = 0;
#ifdef MEMDEBUG
	int	memcheck = 0;
#endif
	int     c;
	int     ndpass = 0;
	int	gotspass = 0;
	int     dbgmode = 0;
	int     verbose = 0;
	int     showrows;
	int	rescode = SQL_ERROR;
	int	showplan = 0;
	int	displaynrows = 0;
	int	dotiming = 0;
	int	dqt = 1;
	int     flineno = 1, *pflineno;
	char    *admin = NULL;
	SIGTYPE	(CDECL *old_signal) ARGS((int));
	TXRESOURCESTATS	pstart, start, end;
	int	gotPstart = 0, showCounts = 0;
	char    **argvWordOptsStripped = NULL, **argSrc, **argDest;
	char	*optionArg, *option;
	TXPMBUF *pmbuf = TXPMBUFPN;
	TXEXIT  ret = TXEXIT_OK;
	double	timeoutVal = -1.0;
	TXbool	timeoutSet = TXbool_False;

	argc = argcstrip;
	argv = argvstrip;

#ifdef _MSC_VER
	__try {
#endif /* _MSC_VER */
		tx_setgenericsigs();
		TXsetSigProcessName(pmbuf, "tsql");

		pflineno = (showprompt ? INTPN : &flineno);
		if (*TXgetlocale() == '\0') TXsetlocale("");
#ifdef DEBUG
		TXDebugLevel = 0;
#endif

#ifdef _WIN32
		DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
		GetCurrentProcess(), &hMainThread, 0, 0,
		DUPLICATE_SAME_ACCESS);
#endif
		txmaxrlim(TXPMBUFPN);   /* MAW 06-12-97 - crank up ulimits */
		globalcp = TXopenapicp();

		sfile = stdin;

		/* Parse and strip word options first, since getopt() does not
		* handle them.  Global word options like --texis-cnf already done:
		*/
		argvWordOptsStripped = (char **)TXcalloc(pmbuf, __FUNCTION__,
			argc + 1, sizeof(char *));
		if (!argvWordOptsStripped) goto memErr;
		argSrc = argv;
		argDest = argvWordOptsStripped;
		if (*argSrc) *(argDest++) = *(argSrc++);        /* program name */

		#define GET_OPTION_ARG() { if (!*++argSrc) {			\
					fprintf(stderr, requiresArgFmt, option);		\
					ret = TXEXIT_INCORRECTUSAGE; goto finally; } else	\
					optionArg = *argSrc; }
		for ( ; (option = *argSrc) != NULL; argSrc++)
		{
			if (strcmp(option, "--show-counts") == 0)
				showCounts = 1;
			else if (strcmp(option, "--lockverbose") == 0)
			{
				int	intVal, errnum;

				GET_OPTION_ARG();
				intVal = TXstrtoi(optionArg, NULL, NULL,
					(0|TXstrtointFlag_ConsumeTrailingSpace |
						TXstrtointFlag_TrailingSourceIsError),
						&errnum);
				if (errnum)
				{
					fprintf(stderr,
						"Invalid argument `%s' for `%s': Expected hex, decimal, or octal integer\n",
						optionArg, option);
					ret = TXEXIT_INCORRECTUSAGE;
					goto finally;
				}
				if (!TXsetlockverbose(intVal))
				{
					ret = TXEXIT_INCORRECTUSAGE;
					goto finally;
				}
			}
			/* Quick hack version of Bug 3774; wtf can corrupt: */
			else if (strcmp(option, "--timeout") == 0)
			{
				int		errnum;
				double	doubleVal;
				char	*e;

				GET_OPTION_ARG();
				doubleVal = TXstrtod(optionArg, NULL, &e, &errnum);
				e += strspn(e, " \t\r\n\v\f");
				if (errnum || *e)
				{
					fprintf(stderr,
						"Invalid argument `%s' for `%s': Expected double or integer\n",
						optionArg, option);
						ret = TXEXIT_INCORRECTUSAGE;
					goto finally;
				}
				timeoutVal = doubleVal;
			}
			else if (strcmp(option, "--bufferout") == 0)
			{
				GET_OPTION_ARG();
				if(strcmp(optionArg, "line") == 0)
				{
					setlinebuf(stdout);
				}
			}
			else if (strcmp(option, "--nomonitorstart") == 0)
			{
				if(TXApp)
					TXApp->NoMonitorStart = 1;
			}
			else
				*(argDest++) = *argSrc;
		}
		*argDest = NULL;
		argv = argvWordOptsStripped;
		argc = argDest - argvWordOptsStripped;

#ifdef MEMDEBUG
	#  define TSQL_MEMDEBUG_OPTS	"CM:W"
#else
	#  define TSQL_MEMDEBUG_OPTS	""
#endif
		while ((c = getopt(argc, argv, "a:cD:d:f:hi:l:mnP:p:qR:rs:tu:Vvw:x?" TSQL_MEMDEBUG_OPTS)) != -1)
		{
			switch(c)
			{
#ifdef DEBUG
				case 'D' :
					TXDebugLevel = atoi(optarg);
					break;
#endif
#ifdef MEMDEBUG
				case 'C' :
					memcheck ++;
					break;
				case 'M' :
					mac_ndx = atoi(optarg);
					break;
				case 'W' :
					mac_won();
					break;
#endif
				case 'P' :
					gotspass++;
					syspass = optarg;
					break;
				case 'p' :
					ndpass--;
					passwd = optarg;
					break;
				case 'u' :
					ndpass++;
					uid = optarg;
					break;
				case 'd' :
					datasource = optarg;
					break;
				case 'q' :
					showprompt = 0;
					goto clearShowhead;	/* avoid gcc 7 warning */
				case 'h' :
					clearShowhead:
					showhead = 0;
					break;
				case 't' :
					dotiming++;
					if (!gotPstart)
					{
						TXgetResourceStats(TXPMBUFPN,
							TXRUSAGE_SELF,
							&pstart);
							gotPstart = 1;
					}
					break;
				case 'x' :
					dbgmode++;
#ifdef _MSC_VER
					TXexceptionbehaviour=EXCEPTION_CONTINUE_SEARCH;
#endif
					break;
				case 'l' :
					rowlim = atoi(optarg);
					break;
				case 'w' :
					argswidth = atoi(optarg);
					break;
				case 'c' :
					colm=1;
					break;
				case 'f' :           /* MAW 10-03-95 - format */
					showprompt = 0;
					switch(*optarg)
					{
						case 't':                   /* tagged */
						colm=1;
						break;
						case 'c':                 /* columnar */
						colm=0;
						break;
						default:                       /* sdf */
						{
							char *p;
							colm= *optarg;   /* delimiter */
							for(p=optarg+1;*p!='\0';p++)
							switch(*p){
								/* strip newlines */
								case 'n': width=1; break;
								/* no quotes */
								case 'q': dqt=0; break;
							}
						}
						break;
					}
				case 'v' :
					verbose++;
					break;
				case 'V' :
					TXsetVerbose(++showplan);
					break;
				case 'a' :
					admin = optarg;
					break;
				case 'i' : /* MAW 06-08-94 */
					sfile=fopen(optarg,"r");
					if(sfile==(FILE *)NULL)
					{
						putmsg(MERR+FOE,(char *)NULL,
						"can't open %s: %s",
						optarg,strerror(errno));
						ret = TXEXIT_CANNOTOPENINPUTFILE;
						goto finally;
					} else {
						closesfile = 1;
					}
					showprompt = 2;
					pflineno = &flineno;
					break;
				case 'm' : /* MAW 06-08-94 - add make option */
					makeit=1;
					break;
				case 'n' :
					displaynrows++;
					break;
				case 'r' :
					readapicp(globalcp, "profile.mm3");
					break;
				case 'R' :
					readapicp(globalcp, optarg);
					break;
				case 's' :
					rowskip = atoi(optarg);
					break;
				case '?' :
					default:
					usage();
			}
		}

		if (dbgmode) tx_unsetgenericsigs(1);

		fo = dbgetfo();
		sqlhelp = 1;
		if (showprompt)
			version();

		if (timeoutVal >= 0.0)
			timeoutVal = TXsetalarm(TXtsqlTimeoutHandler, NULL, timeoutVal,
			TXbool_False /* !inSig */);

		if(makeit)                     /* MAW 06-09-94 - add make opt */
		{
			if(!createdb(datasource))
			{
				putmsg(MERR+FME,(char *)NULL,
				"couldn't make the database %s",datasource);
				ret = TXEXIT_DBOPENFAILED;
				goto finally;
			}
		}
		if(ndpass > 0 && !admin)
		{
			old_signal = signal(SIGINT, SIG_IGN);
			passwd = getpass("User Password: ");
			signal(SIGINT, old_signal);
		}
		if (!passwd)
		{
			ret = TXEXIT_PERMSFAILED;
			goto finally;
		}
		if (admin)
		{
			char *tp = NULL;
			char *sp = NULL;

			if (passwd)
			tp = strdup(passwd);
			if(admin[0] != 'c')
			{
				if(!gotspass)
				{
					old_signal = signal(SIGINT, SIG_IGN);
					syspass = getpass("_SYSTEM Password: ");
					signal(SIGINT, old_signal);
				}
				if(!syspass)
				{
					ret = TXEXIT_PERMSFAILED;
					goto finally;
				}
			}
			if (syspass)
			sp = strdup(syspass);
			administer(admin, datasource, uid, tp, sp);
			if (tp)
			free(tp);
			if (sp)
			free(sp);
			ret = TXEXIT_OK;
			goto finally;
		}
		/* MAW 08-29-94 - let "-" mean stdin for windows redirection */
		if (optind < argc && strcmp(argv[optind],"-")!=0)
		{
			TXstrncpy(stmnt, argv[optind], sizeof(stmnt));
			havestmnt = 1;
		}
#ifdef DEVEL
		if(TXatmem()==-1)
		{
			putmsg(MERR, NULL, "Could not attach to shared memory");
			ret = TXEXIT_LOCKOPENFAILED;
			goto finally;
		}
#endif
		if (SQLAllocEnv(&henv) != SQL_SUCCESS)
		{
			ret = TXEXIT_SQLSTATEMENTFAILED;
			goto finally;
		}
		if (SQLAllocConnect(henv, &hdbc) != SQL_SUCCESS)
		{
			SQLFreeEnv(henv);
			henv = NULL;
			globalcp = closeapicp(globalcp);
			ret = TXEXIT_SQLSTATEMENTFAILED;
			goto finally;
		}
		if (SQLAllocStmt(hdbc, &hstmt) != SQL_SUCCESS)
		{
			SQLFreeConnect(hdbc);
			SQLFreeEnv(henv);
			hdbc = NULL;
			henv = NULL;
			globalcp = closeapicp(globalcp);
			ret = TXEXIT_SQLSTATEMENTFAILED;
			goto finally;
		}
		if (SQLConnect(hdbc,
			(UCHAR *)datasource, (SWORD) strlen(datasource),
			(UCHAR *)uid, (SWORD) strlen(uid),
			(UCHAR *)passwd, (SWORD) strlen(passwd)) != SQL_SUCCESS)
			{
				putmsg(MERR,"SQLConnect","Couldn't connect to %s",datasource);
				SQLFreeStmt(hstmt, SQL_DROP);
				SQLFreeConnect(hdbc);
				SQLFreeEnv(henv);
				hstmt = NULL;
				hdbc = NULL;
				henv = NULL;
				globalcp = closeapicp(globalcp);
				ret = TXEXIT_SQLSTATEMENTFAILED;
				goto finally;
			}
			while((lstmnt = getsql(stmnt, sfile, showprompt, havestmnt, pflineno)) != CHARPN)
			{
				static CONST char	horzwhite[] = " \t";
				char	*s;
				nrows = 0;
				showrows = verbose;

				width = argswidth;
				/* KNG 20120910 might be leading comments before SELECT: */
				for (s = lstmnt, s += strspn(s, horzwhite);
				s[0] == '-' && s[1] == '-';
				s += strspn(s, horzwhite))
				{
					for (s += 2; *s != '\r' && *s != '\n'; s++);
					htskipeol(&s, CHARPN);
				}

				if(!strnicmp(s, "select", 6))
					showrows++;
				if(dotiming > 1)
					TXgetResourceStats(TXPMBUFPN, TXRUSAGE_SELF, &start);
				if (SQLExecDirect(hstmt, (UCHAR *)lstmnt, strlen(lstmnt)) == SQL_SUCCESS)
				{
					TXERR	txe;
					int trowskip;

					if (showhead)
					{
						if (width == 0)
							width = displayhead(hstmt, colm, width, dqt);
						else
							width = displayhead(hstmt, colm, width, dqt);
					}
					trowskip = rowskip;
					#ifndef NEVER
					if(rowskip)
					{
						SQLFetchScroll(hstmt, SQL_FETCH_RELATIVE,
							rowskip);
							trowskip = 0;
						}
						#endif
						while ((rowlim == -1 || nrows < rowlim) &&
						((rescode = SQLFetch(hstmt)) == SQL_SUCCESS))
						{
							nrows++;
							if(showrows && (nrows > trowskip))
							displayrow(hstmt, colm, width, dqt,
								showCounts);
								#ifdef MEMDEBUG
								if(memcheck)
								mac_check(0);
								#endif
								do
								{
									txe = texispoperr(((LPDBC)hdbc)->ddic);
									if(txe == MAKEERROR(MOD_LOCK, LOCK_TIMEOUT))
									{
										putmsg(999, NULL, "Timeout");
										break;
									}
								} while(txe);
							}
							if(rescode == SQL_ERROR)
							{
								UCHAR	sqlstate[10];
								UCHAR	errmsg[80];
								SDWORD	naterror;
								SWORD	errmsglen;

								SQLError(NULL, NULL, hstmt, sqlstate,
									&naterror, errmsg, sizeof(errmsg),
									&errmsglen);
									if (!strcmp((char *)sqlstate, "S1T00"))
									putmsg(999, NULL, "Timeout");
								}
								do
								{
									txe = texispoperr(((LPDBC)hdbc)->ddic);
									if(txe == MAKEERROR(MOD_LOCK, LOCK_TIMEOUT))
									{
										putmsg(999, NULL, "Timeout");
										break;
									}
								} while(txe);
								if(displaynrows && nrows)
								printf("\nNumber of rows %d\n", nrows);
								if(displaynrows > 1 && TXindcnt && nrows)
								htpf("Number of index rows %wd\n",
								(EPI_HUGEUINT)TXindcnt);
							}
							else
							{                 /* MAW 06-15-94 - talk about errors */
								putmsg(MERR,(char *)NULL,"SQL failed");
							}
							if(dotiming > 1)
							{
								#ifdef TX_DEBUG
								byte *data;
								size_t recsz;
								size_t count;

								TXgetcachedindexdata(&data, &recsz, &count);
								#endif
								TXgetResourceStats(TXPMBUFPN, TXRUSAGE_SELF, &end);
								TXshowproctimeinfo(&start, &end);
							}
							/* blank line between statements if -i: */
							if (showprompt >=2) fputs("\n", stdout);
						}
			goto finally;

			memErr:
			ret = TXEXIT_OUTOFMEMORY;
			finally:
			if (timeoutSet)
			{
				TXunsetalarm(TXtsqlTimeoutHandler, NULL, NULL);
				timeoutSet = TXbool_False;
			}

			if (fo)
			fo = foclose(fo);
			if(closesfile)
			{
				fclose(sfile);
				closesfile = 0;
				sfile = NULL;
			}
			#ifdef MEMDEBUG /* WTFWTF */
			mac_ovchk();
			#endif
			if (hstmt)
			{
				SQLFreeStmt(hstmt, SQL_DROP);
				hstmt = NULL;
			}
			if (hdbc)
			{
				SQLDisconnect(hdbc);
				SQLFreeConnect(hdbc);
				hdbc = NULL;
			}
			if (henv)
			{
				SQLFreeEnv(henv);
				henv = NULL;
			}
#ifdef DEVEL
			ndbfcleanup();
			TXdtmem();
#endif
			globalcp = closeapicp(globalcp);
			closetmpfo();
#ifdef MEMDEBUG /* WTFWTF */
			mac_ovchk();
			TXresetexpressions();
			TXcloseabsrex();
#endif
			if(dotiming)
			{
				#ifdef TX_DEBUG
				byte *data;
				size_t recsz;
				size_t count;

				TXgetcachedindexdata(&data, &recsz, &count);
				#endif
				TXgetResourceStats(TXPMBUFPN, TXRUSAGE_SELF, &end);
				TXshowproctimeinfo(&pstart, &end);
				if(dotiming > 2)
				TXshowmaxmemusage();
			}

			argvWordOptsStripped = TXfree(argvWordOptsStripped);
			TXcloseapp();
			argv = NULL;
			argc = 0;
#ifdef _MSC_VER
		}
		__except(TXgenericExceptionHandler(_exception_code(), _exception_info()))
		{
			/* TXgenericExceptionHandler() exits */
		}
#endif /* _MSC_VER */
	return(ret);
}

#ifdef NEVER
#include "stdarg.h"
#define MMSG_C
#include "mmsg.h"

/**********************************************************************/
int CDECL
epiputmsg(int n,char *fn,char *fmt,...)
{
        va_list args;

        va_start(args,fmt);
        if(n>=0)
        {
                fprintf(stderr, "%03d ",n);
        }
        if(fmt!=(char *)NULL)
        {
                vfprintf(stderr, fmt,args);
        }
        if(fn!=(char *)NULL)
        {
                fprintf(stderr, " in the function: %s",fn);
        }
	fprintf(stderr, "\n");
        va_end(args);
        return(0);
}                                                  /* end epiputmsg() */
/**********************************************************************/
#endif                                                    /* _WIN32 */
