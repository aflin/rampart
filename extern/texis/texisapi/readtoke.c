/* -=- kai-mode: John -=- */

#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#ifdef USE_EPI
#  include "os.h"
#endif /* USE_EPI */
#include "dbquery.h"
#include "texint.h"

/* see texint.h for bits: */
int	TXtraceSqlParse = 0;

/******************************************************************/

#define BUFSIZE MAXINSZ

#ifndef OBJECT_READTOKEN
char *zztext;
static char *inbuf = NULL;
static unsigned bufend = 0;
static unsigned curpos = 0;
static char restore = 0;
#endif

int	TXansilike = 0;	  /* If set makes LIKE and MATCHES switch */
/******************************************************************/

#ifndef OBJECT_READTOKEN
int
setparsestring(s)
char	*s;
{
	inbuf = s;
	bufend = strlen(s);
	curpos = 0;
	restore = 0;
	if (TXtraceSqlParse & 0x0010)
		putmsg(MINFO, __FUNCTION__, "Parsing SQL token string `%s'",
		       inbuf);
	return bufend;
}
#else
TX_READ_TOKEN *
setparsestring(TX_READ_TOKEN *toke, char *s)
{
	if(!toke)
	{
		toke = calloc(1, sizeof(TX_READ_TOKEN));
		if (!toke)
		{
			return toke;
		}
	}
	toke->inbuf = s;
	toke->bufend = strlen(s);
	toke->curpos = 0;
	toke->restore = 0;
	toke->c = 0;
	if (TXtraceSqlParse & 0x0010)
		putmsg(MINFO, __FUNCTION__, "Parsing SQL token string `%s'",
		       toke->inbuf);
	return toke;
}
#endif

/******************************************************************/
/*	This is and hand coded scanner for my internal language.
 */

QTOKEN
#ifndef OBJECT_READTOKEN
readtoken ARGS((void))
{
	static int c = 0;
	int	seenfloat = 0;
	QNODE_OP	ret;

	if (restore)          /* We messed with the buffer, so put it back */
	{
		inbuf[curpos] = c;
		if (restore == 2) curpos++;
		restore = 0;
	}
	while(bufend >= curpos)
	{
		if (bufend == curpos) goto err;
		c = ((unsigned char *)inbuf)[curpos];
		zztext = &inbuf[curpos];
		/*
		  ASCII ordered is graph meanings

			!: FLDMATH_NEQ
			": QUOTED_NAME
			#: FLDMATH_MAT / like
			$: NAME_OP
			%: FLDMATH_MOD
			&: FLDMATH_AND
			': STRING_OP
			(: ignore (whitespace)
			): ignore (whitespace)
			*: FLDMATH_MUL
			+: FLDMATH_ADD
			,: LIST_OP
			-: TX_QNODE_NUMBER
			.: FLOAT_OP
			/: FLDMATH_DIV
			0-9: TX_QNODE_NUMBER
			:: NAME_OP (valid sql?)
			;: NAME_OP (invalid sql)
			<: FLDMATH_LT
			=: FLDMATH_EQ
			>: FLDMATH_GT
			?: PARAM_OP
			@: FLDMATH_RELEV
			A-Z: NAME_OP
			[: FLDMATH_IN
			\: NAME_OP (invalid sql)
			]: FLDMATH_TWIXT
			^: NOT_OP
			_: SUBCOMMAND
			`: NAME_OP (invalid sql)
			a-z: NAME_OP
			{: FLDMATH_IS_SUBSET
			|: FLDMATH_OR
			}: FLDMATH_INTERSECT[*]
			~: like / FLDMATH_MAT

		*/
		switch (c)
		{
			case ',' :
				curpos++;
				ret = LIST_OP;
				goto finally;
			case '^' :
				curpos++;
				ret = NOT_OP;
				goto finally;
			case '<' :
				curpos++;
				if (inbuf[curpos] == '=')
				{
					curpos++;
					ret = FLDMATH_LTE;
					goto finally;
				}
				if (inbuf[curpos] == '>')
				{
					curpos++;
					ret = FLDMATH_NEQ;
					goto finally;
				}
				ret = FLDMATH_LT; goto finally;
			case '>' :
				curpos++;
				if (inbuf[curpos] == '=')
				{
					curpos++;
					ret = FLDMATH_GTE;
					goto finally;
				}
				ret = FLDMATH_GT;
				goto finally;
			case '+' :
				curpos++;
				ret = FLDMATH_ADD;
				goto finally;
			case '*' :
				curpos++;
				ret = FLDMATH_MUL;
				goto finally;
			case '/' :
				curpos++;
				ret = FLDMATH_DIV;
				goto finally;
			case '&' :
				curpos++;
				ret = FLDMATH_AND;
				goto finally;
			case '|' :
				curpos++;
				ret = FLDMATH_OR;
				goto finally;
			case '=' :
				curpos++;
				ret = FLDMATH_EQ;
				goto finally;
			case '%' :
				curpos++;
				ret = FLDMATH_MOD;
				goto finally;
			case '~' :
				curpos++;
				if(TXansilike)
					ret = FLDMATH_MAT;
				else
					ret = TXdefaultlike;
				goto finally;
			case '#' :
				curpos++;
				if(TXansilike)
					ret = TXdefaultlike;
				else
					ret = FLDMATH_MAT;
				goto finally;
			case '@' :
				curpos++;
				ret = FLDMATH_RELEV;
				goto finally;
			case '?' :
				curpos++;
#  ifndef OLDPARAM
				while(isdigit(((unsigned char *)inbuf)[curpos]))
					curpos++;
#  endif
				ret = PARAM_OP;
				goto finally;
			case '[' :
				curpos++;
				ret = FLDMATH_IN;
				goto finally;
			case ']' :
				curpos++;
				ret = FLDMATH_TWIXT;
				goto finally;
			case '{' :
				curpos++;
				ret = FLDMATH_IS_SUBSET;
				goto finally;
			case '}' :
				curpos++;
				switch (inbuf[curpos])
				{
				case '=':
					curpos++;
					ret = FLDMATH_INTERSECT_IS_EMPTY;
					goto finally;
				case '!':
					curpos++;
					ret = FLDMATH_INTERSECT_IS_NOT_EMPTY;
					goto finally;
				default:
					ret = FLDMATH_INTERSECT;
					goto finally;
				}
				break;
			case '_' :
				curpos++;
				/*
				ASCII ordered is graph usage:

				!:
				":
				#: FLDMATH_MMIN
				$:
				%:
				&:
				':
				(:
				):
				*: DROP_OP
				+:
				,:
				-: FLDMATH_SUB
				.:
				/:
				0-9: NAMENUM_OP
				::
				;:
				<:
				=:
				>:
				?:
				@: FLDMATH_PROXIM
				A: ALTER_OP
				B:
				C: COLUMN_OP
				D: DEL_SEL_OP
				E:
				F: AGG_FUN_OP
				G: GRANT_OP (g = REVOKE_OP)
				H: HAVING_OP
				I: INSERT_OP
				J: PRODUCT_OP (JOIN)
				K: LOCK_TABLES_OP (k = UNLOCK_TABLES_OP)
				L:
				M: CREATE_OP (MAKE)
				N: NULL_OP
				O: ORDER_OP
				P: PROJECT_OP
				Q: SUBQUERY_OP
				R: RENAME_OP
				S: SELECT_OP
				T: TABLE_OP
				U: UPD_SEL_OP
				V: VALUE_OP
				W: VIEW_OP
				X:
				Y:
				Z:
				[: ARRAY_OP
				\:
				]:
				^:
				_:
				`:
				a:
				b:
				c: COUNTER_OP
				d: DEL_ALL_OP
				e: EXISTS_OP
				f: REG_FUN_OP
				g: REVOKE_OP
				h: HINT_OP
				i: INFO_OP - internal / info table
				j:
				k: UNLOCK_TABLES_OP
				l: ALL_OP
				m:
				n: UNION_OP
				o: ORDERING_SPEC_OP
				p: GROUP_BY_OP
				q: PROP_OP
				r: FLOAT_OP
				s: DISTINCT_OP
				t: TABLE_AS_OP
				u: UPD_ALL_OP
				v: CONVERT_OP
				w:
				x: TX_QNODE_NUMBER (HEX Constant)
				y:
				z:
				{:
				|:
				}:
				~: FLDMATH_NMM

				*/
				switch(inbuf[curpos++])
				{
					case '#' :
						ret = FLDMATH_MMIN;
						goto finally;
					case '*' :
						ret = DROP_OP;
						goto finally;
					case '@' :
						ret = FLDMATH_PROXIM;
						goto finally;
					case '~' :
						ret = FLDMATH_NMM;
						goto finally;
					case '[':	ret = ARRAY_OP;
						goto finally;
					case '-' :
						ret = FLDMATH_SUB;
						goto finally;
					case '0' :
					case '1' :
					case '2' :
					case '3' :
					case '4' :
					case '5' :
					case '6' :
					case '7' :
					case '8' :
					case '9' :
						do
						{
							c = ((unsigned char *)inbuf)[curpos++];
						} while (isdigit(c)
#ifdef TX_USE_ORDERING_SPEC_NODE
		/* order flags put in separate ORDERING_SPEC_OP node now */
#else /* !TX_USE_ORDERING_SPEC_NODE */
						         ||c == '^' || c == '-'
#endif /* !TX_USE_ORDERING_SPEC_NODE */
							 );
						inbuf[--curpos] = '\0';
						restore = 1;
						ret = NAMENUM_OP;
						goto finally;
					case 'C' :
						ret = COLUMN_OP;
						goto finally;
					case 'D' :
						ret = DEL_SEL_OP;
						goto finally;
					case 'F' :
						ret = AGG_FUN_OP;
						goto finally;
					case 'G' :
						ret = GRANT_OP;
						goto finally;
					case 'H' :
						ret = HAVING_OP;
						goto finally;
					case 'I' :
						ret = INSERT_OP;
						goto finally;
					case 'J' :
						ret = PRODUCT_OP;
						goto finally;
					case 'K' :
						ret = LOCK_TABLES_OP;
						goto finally;
					case 'M' :
						ret = CREATE_OP;
						goto finally;
					case 'N' :
						ret = NULL_OP;
						goto finally;
					case 'O' :
						ret = ORDER_OP;
						goto finally;
					case 'P' :
						ret = PROJECT_OP;
						goto finally;
					case 'Q' :
						ret = SUBQUERY_OP;
						goto finally;
					case 'R' :
						ret = RENAME_OP;
						goto finally;
					case 'S' :
						ret = SELECT_OP;
						goto finally;
					case 'T' :
						ret = TABLE_OP;
						goto finally;
					case 'A' :
						ret = ALTER_OP;
						goto finally;
					case 'U' :
						ret = UPD_SEL_OP;
						goto finally;
					case 'V' :
						ret = VALUE_OP;
						goto finally;
					case 'W' :
						ret = VIEW_OP;
						goto finally;
					case 'c' :
						ret = COUNTER_OP;
						goto finally;
					case 'd' :
						ret = DEL_ALL_OP;
						goto finally;
					case 'e' :
						ret = EXISTS_OP;
						goto finally;
					case 'f' :
						ret = REG_FUN_OP;
						goto finally;
					case 'g' :
						ret = REVOKE_OP;
						goto finally;
					case 'h' :
						ret = HINT_OP;
						goto finally;
					case 'i' :
						ret = INFO_OP;
						goto finally;
					case 'k' :
						ret = UNLOCK_TABLES_OP;
						goto finally;
					case 'l' :
						ret = ALL_OP;
						goto finally;
					case 'n' :
						ret = UNION_OP;
						goto finally;
#ifdef TX_USE_ORDERING_SPEC_NODE
					case 'o':
						ret = ORDERING_SPEC_OP;
						/* Point `zztext' to spec,
						 * e.g. `^', `-':
						 */
						zztext = inbuf + curpos;
						while (curpos < bufend &&
						       !TX_ISSPACE(inbuf[curpos]))
							curpos++; /*skip spec*/
						goto finally;
#endif /* TX_USE_ORDERING_SPEC_NODE */
					case 'p' :
						ret = GROUP_BY_OP;
						goto finally;
					case 'q' :
						ret = PROP_OP;
						goto finally;
					case 'r' :
					{
						char *end;

						zztext += 2;	/* skip `_r' */
						strtod(&inbuf[curpos], &end);
						if (end)
						{
							c = *(unsigned char *)end;
							*end = '\0';
							curpos = end - inbuf;
						}
						ret = FLOAT_OP;
						goto finally;
					}
					case 's' :
						ret = DISTINCT_OP;
						goto finally;
					case 't' :
						ret = TABLE_AS_OP;
						goto finally;
					case 'u' :
						ret = UPD_ALL_OP;
						goto finally;
					case 'v' :
						ret = CONVERT_OP;
						goto finally;
					case 'x':	/* hex constant */
					{
						char	*e;
						TXbool	isNeg = TXbool_False;

						zztext += 2;	/* skip `_x' */
						e = zztext;
						isNeg = (*e == '-');
						if (isNeg) e++;
						if (*(e++) != '0') goto err;
						if (TX_TOLOWER(*e) != 'x')
							goto err;
						for (e++; TX_ISXDIGIT(*e);e++);
						c = *e;
						*e = '\0';
						curpos = e - inbuf;
						ret = (isNeg ? NNUMBER :
						       TX_QNODE_NUMBER);
					}
					goto finally;
				}
				goto err;
			case '!' :
				curpos++;
				if (inbuf[curpos] == '=')
				{
					curpos++;
					ret = FLDMATH_NEQ;
					goto finally;
				}
				goto err;
			case '\0' :
			case '(' :
			case ')' :
			case '\n' :
			case ' ' :
			case '\t' :
				curpos ++;
				break;
			case '\'' :
				curpos++;
				zztext = &inbuf[curpos];
				do
				{
					c = ((unsigned char *)inbuf)[curpos];
					curpos += 1;
					if (c == '\0')
						goto err;
					if((c=='\'')&&(inbuf[curpos]=='\''))
					{
						curpos++;
						c = '\\';
					}
				} while (c != '\'');
				inbuf[--curpos] = '\0';
				restore = 2;
				ret = STRING_OP;
				goto finally;
			case '-' :
			case '0' :
			case '1' :
			case '2' :
			case '3' :
			case '4' :
			case '5' :
			case '6' :
			case '7' :
			case '8' :
			case '9' :
			case '.' :
				do
				{
					if(c=='.') seenfloat ++;
					c = ((unsigned char *)inbuf)[curpos++];
				} while (isdigit(c) || c == '.' || c == '-');
				inbuf[--curpos] = '\0';
				restore = 1;
				if (seenfloat)
					ret = FLOAT_OP;
				else
					ret = (*zztext == '-' ? NNUMBER :
					       TX_QNODE_NUMBER);
				goto finally;
			case '"':		/* quoted table/index name */
				for (curpos++, zztext++; /* skip open quote */
				     curpos < bufend && inbuf[curpos] != '"';
				     curpos++)
					;
				c = inbuf[curpos];
				inbuf[curpos] = '\0';
				restore = 2;	/* restore and skip end quote*/
				ret = NAME_OP;
				goto finally;
			default :
				do
				{
					c = ((unsigned char *)inbuf)[curpos++];
				} while (isgraph(c) || c == '.');
				inbuf[--curpos] = '\0';
				restore = 1;
				ret = NAME_OP;
				goto finally;
		}
	}
	goto err;

err:
	ret = (QNODE_OP)0;
finally:
	if (TXtraceSqlParse & 0x0020)
		putmsg(MINFO, __FUNCTION__, "Returning SQL token %s `%s'",
		       TXqnodeOpToStr(ret, NULL, 0), zztext);
	return(ret);
}
#else /* OBJECT_READTOKEN */
readtoken (TX_READ_TOKEN *toke)
{
	int	seenfloat = 0;
	int zzlen;

#  error Add TXtraceSqlParse support

	if (toke->restore)    /* We messed with the buffer, so put it back */
	{
		toke->inbuf[toke->curpos] = toke->c;
		if (toke->restore == 2) toke->curpos++;
		toke->restore = 0;
	}
	while(toke->bufend >= toke->curpos)
	{
		if (toke->bufend == toke->curpos)
			return 0;
		toke->c = toke->inbuf[toke->curpos];
		toke->zztext = &toke->inbuf[toke->curpos];
		switch (toke->c)
		{
			int esc;

			case ',' :
				(toke->curpos)++;
				return (LIST_OP);
			case '^' :
				(toke->curpos)++;
				return (NOT_OP);
			case '<' :
				(toke->curpos)++;
				if (toke->inbuf[toke->curpos] == '=')
				{
					(toke->curpos)++;
					return FOP_LTE;
				}
				if (toke->inbuf[toke->curpos] == '>')
				{
					(toke->curpos)++;
					return FOP_NEQ;
				}
				return (FOP_LT);
			case '>' :
				(toke->curpos)++;
				if (toke->inbuf[toke->curpos] == '=')
				{
					(toke->curpos)++;
					return FOP_GTE;
				}
				return (FOP_GT);
			case '+' :
				(toke->curpos)++;
				return (FOP_ADD);
			case '*' :
				(toke->curpos)++;
				return (FOP_MUL);
			case '/' :
				(toke->curpos)++;
				return (FOP_DIV);
			case '&' :
				(toke->curpos)++;
				return (FOP_AND);
			case '|' :
				(toke->curpos)++;
				return (FOP_OR);
			case '=' :
				(toke->curpos)++;
				return (FOP_EQ);
			case '%' :
				(toke->curpos)++;
				return (FOP_MOD);
			case '~' :
				(toke->curpos)++;
				if(TXansilike)
					return (FOP_MAT);
				else
					return (TXdefaultlike);
			case '#' :
				(toke->curpos)++;
				if(TXansilike)
					return (TXdefaultlike);
				else
					return (FOP_MAT);
			case '@' :
				(toke->curpos)++;
				return (FOP_RELEV);
			case '?' :
				(toke->curpos)++;
#  ifndef OLDPARAM
				while(isdigit(toke->inbuf[toke->curpos]))
					(toke->curpos)++;
#  endif
				return (PARAM_OP);
			case '[' :
				(toke->curpos)++;
				return FOP_IN;
			case ']' :
				(toke->curpos)++;
				return FOP_TWIXT;
			case '{' :
				toke->curpos++;
				return FOP_IS_SUBSET;
			case '}' :
				(toke->curpos)++;
				switch (toke->inbuf[toke->curpos])
				{
				case '=':
					(toke->curpos)++;
					return(FOP_INTERSECT_IS_EMPTY);
				case '!':
					(toke->curpos)++;
					return(FOP_INTERSECT_IS_NOT_EMPTY);
				default:
					return(FOP_INTERSECT);
				}
				break;
			case '_' :
				(toke->curpos)++;
				switch(toke->inbuf[(toke->curpos)++])
				{
					case '#' :
						return FOP_MMIN;
					case '*' :
						return DROP_OP;
					case '@' :
						return FOP_PROXIM;
					case '~' :
						return FOP_NMM;
					case '-' :
						return FOP_SUB;
					case '0' :
					case '1' :
					case '2' :
					case '3' :
					case '4' :
					case '5' :
					case '6' :
					case '7' :
					case '8' :
					case '9' :
						do
						{
							toke->c = toke->inbuf[(toke->curpos)++];
						} while (isdigit(toke->c)
#ifdef TX_USE_ORDERING_SPEC_NODE
		/* order flags put in separate ORDERING_SPEC_OP node now */
#else /* !TX_USE_ORDERING_SPEC_NODE */
						         ||toke->c == '^'   ||
							 toke->c == '-'
#endif /* !TX_USE_ORDERING_SPEC_NODE */
							 );
						toke->inbuf[--(toke->curpos)] = '\0';
						toke->restore = 1;
						return NAMENUM_OP;
					case 'C' :
						return COLUMN_OP;
					case 'D' :
						return DEL_SEL_OP;
					case 'F' :
						return AGG_FUN_OP;
					case 'G' :
						return GRANT_OP;
					case 'H' :
						return HAVING_OP;
					case 'I' :
						return INSERT_OP;
					case 'J' :
						return PRODUCT_OP;
					case 'M' :
						return CREATE_OP;
					case 'N' :
						return NULL_OP;
					case 'O' :
						return ORDER_OP;
					case 'P' :
						return PROJECT_OP;
					case 'Q' :
						return SUBQUERY_OP;
					case 'R' :
						return RENAME_OP;
					case 'S' :
						return SELECT_OP;
					case 'T' :
						return TABLE_OP;
					case 'A' :
						return ALTER_OP;
					case 'U' :
						return UPD_SEL_OP;
					case 'V' :
						return VALUE_OP;
					case 'W' :
						return VIEW_OP;
					case 'c' :
						return COUNTER_OP;
					case 'd' :
						return DEL_ALL_OP;
					case 'e' :
						return EXISTS_OP;
					case 'f' :
						return REG_FUN_OP;
					case 'g' :
						return REVOKE_OP;
					case 'l' :
						return ALL_OP;
					case 'n' :
						return UNION_OP;
#ifdef TX_USE_ORDERING_SPEC_NODE
					case 'o':
						/* Point `zztext' to spec,
						 * e.g. `^', `-':
						 */
						toke->zztext = toke->inbuf +
							toke->curpos;
						while (toke->curpos < toke->bufend &&
						       !TX_ISSPACE(toke->inbuf[toke->curpos]))
							toke->curpos++; /*skip spec*/
						return ORDERING_SPEC_OP;
#endif /* TX_USE_ORDERING_SPEC_NODE */
					case 'p' :
						return GROUP_BY_OP;
					case 'q' :
						return PROP_OP;
					case 'r' :
					{
						char *end;

						toke->zztext += 2;
						strtod(&toke->inbuf[toke->curpos], &end);
						if (end)
						{
							toke->c = *end;
							*end = '\0';
							toke->curpos = end - toke->inbuf;
						}
						return FLOAT_OP;
					}
					case 's' :
						return DISTINCT_OP;
					case 't' :
						return TABLE_AS_OP;
					case 'u' :
						return UPD_ALL_OP;
					case 'v' :
						return CONVERT_OP;
					case 'x':	/* hex constant */
					{
						char	*e;
						TXbool	isNeg = TXbool_False;

						toke->zztext += 2;/*skip `_x'*/
						e = toke->zztext;
						isNeg = (*e == '-');
						if (isNeg) e++;
						if (*(e++) != '0') goto err;
						if (TX_TOLOWER(*e) != 'x')
							goto err;
						for (e++; TX_ISXDIGIT(*e);e++);
						toke->c = *e;
						*e = '\0';
						toke->curpos = e - inbuf;
						ret = (isNeg ? NNUMBER :
						       TX_QNODE_NUMBER);
					}
				}
				return 0;
			case '!' :
				(toke->curpos)++;
				if (toke->inbuf[toke->curpos] == '=')
				{
					(toke->curpos)++;
					return FOP_NEQ;
				}
				return (0);
			case '\0' :
			case '(' :
			case ')' :
			case '\n' :
			case ' ' :
			case '\t' :
				(toke->curpos)++;
				break;
			case '\'' :
				(toke->curpos)++;
				toke->zztext = &toke->inbuf[toke->curpos];
				do
				{
					toke->c = toke->inbuf[toke->curpos];
					toke->curpos += 1;
					if (toke->c == '\0')
						return 0;
					if((toke->c=='\'')&&(toke->inbuf[toke->curpos]=='\''))
					{
						(toke->curpos)++;
						toke->c = '\\';
					}
				} while (toke->c != '\'');
				toke->inbuf[--(toke->curpos)] = '\0';
				toke->restore = 2;
				return STRING_OP;
			case '-' :
			case '0' :
			case '1' :
			case '2' :
			case '3' :
			case '4' :
			case '5' :
			case '6' :
			case '7' :
			case '8' :
			case '9' :
			case '.' :
				do
				{
					if(toke->c=='.') seenfloat ++;
					toke->c = toke->inbuf[(toke->curpos)++];
				} while (isdigit(toke->c) || toke->c == '.' || toke->c == '-');
				toke->inbuf[--(toke->curpos)] = '\0';
				toke->restore = 1;
				if (seenfloat)
					return FLOAT_OP;
				else
					return *(toke->zztext)=='-'?NNUMBER:TX_QNODE_NUMBER;
			case '"':		/* quoted table/index name */
				for (toke->curpos++, toke->zztext++;
				     toke->curpos < toke->bufend &&
					     toke->inbuf[toke->curpos] != '"';
				     toke->curpos++)
					;
				toke->c = toke->inbuf[toke->curpos];
				toke->inbuf[toke->curpos] = '\0';
				toke->restore = 2; /* restore+skip end quote*/
				return(NAME_OP);
			default :
				do
				{
					toke->c = toke->inbuf[toke->curpos++];
				} while (isgraph(toke->c) || toke->c == '.');
				toke->inbuf[--(toke->curpos)] = '\0';
				toke->restore = 1;
				return NAME_OP;
		}
	}
	return 0;
}
#endif /* OBJECT_READTOKEN */
