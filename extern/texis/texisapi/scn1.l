%{
/*
 * AT&T lex can't handle this lexer due to lex bugs.  It works with flex
 * 2.3.7, pclex 2.0.5, and MKS lex 3.1a.
 */

char *yy_inbuf;
size_t yy_inbuf_read;
char *datasrc;
extern int yydebug;
extern int yycontext;
int		sqlhelp = 0;

static int	maxout;

#include "txcoreconfig.h"
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#else
/* Prevent later attempt at unistd.h inclusion: */
#  define YY_NO_UNISTD_H
#endif
#ifdef _WIN32
#  include <io.h>                               /* for isatty() */
#endif /* _WIN32 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include "dbquery.h"
#include "texint.h"
#ifdef USE_STRBUF
static STRBUF	*outbuf;
#else
static char	*outbuf;
#endif

/* wtf why aren't these prototyped somewhere? */
extern int yylex ARGS((void));
extern int yyparse ARGS((void));
void CDECL genout ARGS((char *));

#define YY_INPUT(buf, result, max_size) \
	TXstrncpy((char *)buf, yy_inbuf + yy_inbuf_read, max_size); \
	result=strlen((char *)buf); \
	yy_inbuf[0]='\0'; \
	yy_inbuf_read += result;

/* undef macros that conflict with tokens in here */

#include "sql1.h"
#include "sqluge.h"
#ifdef NEVER
#undef USE_STDARG                                     /* wtf wtf why? */
#ifdef USE_STDARG
#  include	<stdarg.h>
#else
#  include	<varargs.h>
#endif
#endif /* NEVER */
DDIC *yyddic;

int lineno = 1;
void yyerror ARGS((char *s));
%}
	/* MKS needs the next line to increase the NFA table */
%e 1200
%option noyywrap

%%

	/* literal keyword tokens */

METAMORPH	{ return TDB; }
metamorph	{ return TDB; }
FULLTEXT	{
#ifndef TX_INDEX_OPTIONS
	if (!getenv("TX_INDEX_OPTIONS"))
	{
		putmsg(MERR + UGE, NULL, "FULLTEXT token not supported yet");
		return(0);			/* wtf force error */
	}
#endif /* !TX_INDEX_OPTIONS */
	return(TDB);
}
fulltext	{
#ifndef TX_INDEX_OPTIONS
	if (!getenv("TX_INDEX_OPTIONS"))
	{
		putmsg(MERR + UGE, NULL, "FULLTEXT token not supported yet");
		return(0);			/* wtf force error */
	}
#endif /* !TX_INDEX_OPTIONS */
	return(TDB);
}
ADA		{ return ADA; }
ada		{ return ADA; }
ALL		{ return ALL; }
all		{ return ALL; }
AND		{ return AND; }
and		{ return AND; }
AVG		{ return AMMSC; }
avg		{ return AMMSC; }
MIN		{ return AMMSC; }
min		{ return AMMSC; }
MAX		{ return AMMSC; }
max		{ return AMMSC; }
SUM		{ return AMMSC; }
sum		{ return AMMSC; }
COUNT		{ return AMMSC; }
count		{ return AMMSC; }
AFTER		{ return AFTER; }
after		{ return AFTER; }
ALTER		{ return ALTER; }
alter		{ return ALTER; }
ANY		{ return ANY; }
any		{ return ANY; }
AS		{ return AS; }
as		{ return AS; }
ASC		{ return ASC; }
asc		{ return ASC; }
AUTHORIZATION	{ return AUTHORIZATION; }
authorization	{ return AUTHORIZATION; }
BEFORE		{ return BEFORE; }
before		{ return BEFORE; }
BETWEEN		{ return BETWEEN; }
between		{ return BETWEEN; }
BLOB		{ return TX_BLOB; }
blob		{ return TX_BLOB; }
BY		{ return BY; }
by		{ return BY; }
BYTE		{ return TX_BYTE; }
byte		{ return TX_BYTE; }
CHAR(ACTER)?	{ return CHARACTER; }
char(acter)?	{ return CHARACTER; }
CHECK		{ return CHECK; }
check		{ return CHECK; }
CLOSE		{ return CLOSE; }
close		{ return CLOSE; }
COBOL		{ return COBOL; }
cobol		{ return COBOL; }
COMMIT		{ return COMMIT; }
commit		{ return COMMIT; }
CONNECT		{ return CONNECT; }
connect		{ return CONNECT; }
CONTAINS	{ return LIKE; }
contains	{ return LIKE; }
CONTINUE	{ return CONTINUE; }
continue	{ return CONTINUE; }
CONVERT		{ return CONVERT; }
convert		{ return CONVERT; }
COUNTER		{ return COUNTER; }
counter		{ return COUNTER; }
CREATE		{ return CREATE; }
create		{ return CREATE; }
CTIME		{ return TX_DATE; }
ctime		{ return TX_DATE; }
CURRENT		{ return CURRENT; }
current		{ return CURRENT; }
CURSOR		{ return CURSOR; }
cursor		{ return CURSOR; }
DATABASE	{ return DATABASE; }
database	{ return DATABASE; }
DATE		{ return TX_DATE; }
date		{ return TX_DATE; }
DECIMAL		{ return TX_DECIMAL; }
decimal		{ return TX_DECIMAL; }
DECLARE		{ return DECLARE; }
declare		{ return DECLARE; }
DEFAULT		{ return DEFAULT; }
default		{ return DEFAULT; }
DELETE		{ return TX_DELETE; }
delete		{ return TX_DELETE; }
DESC		{ return DESC; }
desc		{ return DESC; }
DISTINCT	{ return DISTINCT; }
distinct	{ return DISTINCT; }
DOUBLE		{ return TX_DOUBLE; }
double		{ return TX_DOUBLE; }
DROP		{ return DROP; }
drop		{ return DROP; }
EACH		{ return EACH; }
each		{ return EACH; }
EMPTY		{ return EMPTY; }
empty		{ return EMPTY; }
ESCAPE		{ return ESCAPE; }
escape		{ return ESCAPE; }
EXISTS		{ return EXISTS; }
exists		{ return EXISTS; }
FETCH		{ return FETCH; }
fetch		{ return FETCH; }
FLOAT		{ return TX_FLOAT; }
float		{ return TX_FLOAT; }
FOR		{ return FOR; }
for		{ return FOR; }
FOREIGN		{ return FOREIGN; }
foreign		{ return FOREIGN; }
FORTRAN		{ return FORTRAN; }
fortran		{ return FORTRAN; }
FOUND		{ return FOUND; }
found		{ return FOUND; }
FROM		{ return FROM; }
from		{ return FROM; }
GO[ \t]*TO	{ return GOTO; }
go[ \t]*to	{ return GOTO; }
GRANT		{ return GRANT; }
grant		{ return GRANT; }
GROUP		{ return TX_GROUP; }
group		{ return TX_GROUP; }
HAVING		{ return HAVING; }
having		{ return HAVING; }
IDENTIFIED	{ return IDENTIFIED; }
identified	{ return IDENTIFIED; }
IF	{
#ifndef TX_SQL_IF_EXISTS
	if (!getenv("TX_SQL_IF_EXISTS"))
	{
		putmsg(MERR + UGE, NULL, "IF token not supported yet");
		return(0);			/* wtf force error */
	}
#endif /* !TX_SQL_IF_EXISTS */
	return(TX_IF);
}
if	{
#ifndef TX_SQL_IF_EXISTS
	if (!getenv("TX_SQL_IF_EXISTS"))
	{
		putmsg(MERR + UGE, NULL, "IF token not supported yet");
		return(0);			/* wtf force error */
	}
#endif /* !TX_SQL_IF_EXISTS */
	return(TX_IF);
}
IN		{ return TX_IN; }
in		{ return TX_IN; }
INDEX		{ return INDEX; }
index		{ return INDEX; }
INDICATOR	{ return INDICATOR; }
indicator	{ return INDICATOR; }
INDIRECT	{ return INDIRECT; }
indirect	{ return INDIRECT; }
INSERT		{ return INSERT; }
insert		{ return INSERT; }
INSTEAD		{ return INSTEAD; }
instead		{ return INSTEAD; }
INT(EGER)?	{ return INTEGER; }
int(eger)?	{ return INTEGER; }
INTERSECT	{ return TX_INTERSECT; }
intersect	{ return TX_INTERSECT; }
INTO		{ return INTO; }
into		{ return INTO; }
INVERTED	{ return INVERTED; /* JMT 6/27/94 */}
inverted	{ return INVERTED; /* JMT 6/27/94 */}
IS		{ return IS; }
is		{ return IS; }
KEY		{ return KEY; }
key		{ return KEY; }
LANGUAGE	{ return LANGUAGE; }
language	{ return LANGUAGE; }
LIKE		{ return LIKE; }
like		{ return LIKE; }
LIKEIN		{ return LIKEIN; }
likein		{ return LIKEIN; }
LIKE3		{ return LIKEA; }
like3		{ return LIKEA; }
LIKEP		{ return LIKEP; }
likep		{ return LIKEP; }
LIKER		{ return LIKER; }
liker		{ return LIKER; }
LINK		{ return LINK; }
link		{ return LINK; }
LOCK		{ return TX_LOCK; }
lock		{ return TX_LOCK; }
MATCHES		{ return MATCHES; }
matches		{ return MATCHES; }
MODULE		{ return MODULE; }
module		{ return MODULE; }
NEW		{ return NEW; }
new		{ return NEW; }
NOCASE		{ return NOCASE; }
nocase		{ return NOCASE; }
NOT		{ return NOT; }
not		{ return NOT; }
NULL		{ return NULLX; }
null		{ return NULLX; }
NUMERIC		{ return NUMERIC; }
numeric		{ return NUMERIC; }
OF		{ return OF; }
of		{ return OF; }
OLD		{ return OLD; }
old		{ return OLD; }
ON		{ return ON; }
on		{ return ON; }
OPEN		{ return OPEN; }
open		{ return OPEN; }
OPTION		{ return OPTION; }
option		{ return OPTION; }
OR		{ return OR; }
or		{ return OR; }
ORDER		{ return ORDER; }
order		{ return ORDER; }
PASCAL		{ return TX_PASCAL; }
pascal		{ return TX_PASCAL; }
PLI		{ return PLI; }
pli		{ return PLI; }
PRECISION	{ return PRECISION; }
precision	{ return PRECISION; }
PRIMARY		{ return PRIMARY; }
primary		{ return PRIMARY; }
PRIVILEGES	{ return PRIVILEGES; }
privileges	{ return PRIVILEGES; }
PROCEDURE	{ return PROCEDURE; }
procedure	{ return PROCEDURE; }
PUBLIC		{ return PUBLIC; }
public		{ return PUBLIC; }
READ		{ return READ; }
read		{ return READ; }
REAL		{ return REAL; }
real		{ return REAL; }
REFERENCES	{ return REFERENCES; }
references	{ return REFERENCES; }
REFERENCING	{ return REFERENCING; }
referencing	{ return REFERENCING; }
REVOKE		{ return REVOKE; }
revoke		{ return REVOKE; }
ROLLBACK	{ return ROLLBACK; }
rollback	{ return ROLLBACK; }
ROW		{ return ROW; }
row		{ return ROW; }
SCHEMA		{ return SCHEMA; }
schema		{ return SCHEMA; }
SELECT		{ return SELECT; }
select		{ return SELECT; }
SET		{ return SET; }
set		{ return SET; }
SHELL		{ return SHELL; }
shell		{ return SHELL; }
SHOW    { return SHOW; }
show    { return SHOW; }
SMALLINT	{ return SMALLINT; }
smallint	{ return SMALLINT; }
SOME		{ return SOME; }
some		{ return SOME; }
SQLCODE		{ return SQLCODE; }
sqlcode		{ return SQLCODE; }
STATEMENT	{ return STATEMENT; }
statement	{ return STATEMENT; }
STRLST    	{ return STRLST; }
strlst    	{ return STRLST; }
SUBSET		{ return TX_SUBSET; }
subset		{ return TX_SUBSET; }
TABLE		{ return TABLE; }
table		{ return TABLE; }
TABLES  { return TABLES; }
tables  { return TABLES; }
TO		{ return TO; }
to		{ return TO; }
TRIGGER		{ return TX_TRIGGER; }
trigger		{ return TX_TRIGGER; }
UNION		{ return UNION; }
union		{ return UNION; }
UNIQUE		{ return UNIQUE; }
unique		{ return UNIQUE; }
UNLOCK    { return TX_UNLOCK; }
unlock    { return TX_UNLOCK; }
UNSIGNED	{ return UNSIGNED; /* JMT 06/27/94 */}
unsigned	{ return UNSIGNED; }
UPDATE		{ return TX_UPDATE; }
update		{ return TX_UPDATE; }
USER		{ return USER; }
user		{ return USER; }
USING		{ return USING; }
using		{ return USING; }
VALUES		{ return VALUES; }
values		{ return VALUES; }
VARBYTE		{ return VARBYTE; }
varbyte		{ return VARBYTE; }
VARCHAR		{ return VARCHAR; }
varchar		{ return VARCHAR; }
VIEW		{ return VIEW; }
view		{ return VIEW; }
WHEN		{ return WHEN; }
when		{ return WHEN; }
WHENEVER	{ return WHENEVER; }
whenever	{ return WHENEVER; }
WHERE		{ return WHERE; }
where		{ return WHERE; }
WITH		{ return WITH; }
with		{ return WITH; }
WORK		{ return WORK; }
work		{ return WORK; }
WRITE   { return WRITE; }
write   { return WRITE; }

	/* punctuation */

"="	|
"<>" 	|
"!=" 	|
"<"	|
">"	|
"<="	|
">="		{ return COMPARISON; }

[-+*/%:(),.;?\\]	{ return yytext[0]; }

	/* names */

[_A-Za-z][A-Za-z0-9_]*	{ return NAME; }

	/* numbers */

[0-9]+	|
[0-9]+"."[0-9]* |
"."[0-9]*	{ return INTNUM; }

0[xX][0-9a-fA-F]+  { return HEXCONST; }

[0-9]+[eE][+-]?[0-9]+	|
[0-9]+"."[0-9]+[eE][+-]?[0-9]+ |
"."[0-9]+[eE][+-]?[0-9]+	{ return APPROXNUM; }

	/* strings */

'[^']*$	{
		putmsg(MERR+UGE,"lex","Unterminated character string");
		return STRING;
	}

'[^']*'	{	/* ' <- quote to unconfuse editor highlighting */
		return STRING;
                /* Note: see vx_parseargs() if this expression changes */
	}

\"[^"]*\" { /* " balance highlighting */ return QSTRING; /* JMT 97-12-05 */
                /* Note: see vx_parseargs() if this expression changes */
          }

\n		lineno++;

[ \t\r]+	;	/* white space */

"--".*$		;	/* comment *//* Note: see vx_parseargs() if this expression changes */

.	{ return yytext[0]; }

%%

void
yyerror(s)
char *s;
{
	putmsg(MERR+UGE, NULL, "Line %d: %s at \"%s\"", lineno, s, yytext);
	if (sqlhelp && (yycontext >= 0))
		putmsg(MERR+UGE, NULL, "%s", use[yycontext].usage);
}

#ifdef STANDALONE
main(ac, av)
int ac;
char **av;
{
	extern int yydebug;

	yydebug = 0;
	if(ac > 1 && (yyin = fopen(av[1], "rb")) == NULL) {
		perror(av[1]);
		exit(1);
	}

	if(!yyparse())
	{
		exit (0);
	}
	else
	{
		exit (2);
	}
} /* main */
#endif

void CDECL
genout(char *fmt)
{
#ifdef USE_STRBUF
	addstrbuf(outbuf, fmt, -1);
#else /* !USE_STRBUF */
	strcat(outbuf, fmt);
#endif /* !USE_STRBUF */
}

int
clearout()
{
	if (outbuf)
	{
#ifdef USE_STRBUF
		resetstrbuf(outbuf);
#else
		outbuf[0] = '\0';
#endif
		return 0;
	}
	return -1;
}

static int TXyyInit = -1, TXyyStart = -1, TXgotYyInitializers = 0;

int
sqlconvert(in, datasource, ddic, out, obs)
char *in;
char *datasource;
DDIC *ddic;
#ifdef USE_STRBUF
STRBUF *out;
#else
char *out;
#endif
int obs;
{
	static const char	fn[] = "sqlconvert";
	int rc;

	/* Restore yy_init to its initial state, to force flex to
	 * re-initialize; otherwise flex 2.5.35 may hang.  But flex
	 * 2.5.4 starts with yy_init = 1, whereas flex 2.5.35 starts
	 * with yy_init = 0.  So we need to save the initial yy_init
	 * value (*before* any yyparse()/yylex() calls) for proper
	 * re-init (otherwise we may ABEND/hang with flex 2.5.35):
	 */
	if (!TXgotYyInitializers)
	{
		TXyyInit = yy_init;
		TXyyStart = yy_start;
		TXgotYyInitializers = 1;
	}
	/* WTF call yylex_init()? */
	yy_init = TXyyInit;

	maxout = obs;
	outbuf = out;
	clearout();
	if (TXtraceSqlParse & 0x0001)
		putmsg(MINFO, __FUNCTION__, "Parsing SQL string `%s'", in);
	yy_inbuf = TXstrdup(TXPMBUFPN, fn, in);
	if (!yy_inbuf) goto err;
	yy_inbuf_read = 0;
	datasrc = datasource;
	yyddic = ddic;
	yycontext = -1;
	TXresetparamcount();
	rc = yyparse();
	yy_inbuf = TXfree(yy_inbuf);
	yy_inbuf_read = 0;
	YY_NEW_FILE;
	return rc;
err:
	return(1);
}

int
closesqlparse()
{
#ifdef EPI_HAVE_YYLEX_DESTROY
	yylex_destroy();
#else /* !EPI_HAVE_YYLEX_DESTROY */
	if (YY_CURRENT_BUFFER)
		yy_delete_buffer(YY_CURRENT_BUFFER);
	if (TXgotYyInitializers)
	{
		yy_init = TXyyInit;
		yy_start = TXyyStart;
	}
#endif /* !EPI_HAVE_YYLEX_DESTROY */
	return(1);                                    /* MAW 04-28-94 */
}
