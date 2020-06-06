%{

#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include "texint.h"
#include "txlic.h"              /* KNG 010622 obfuscate stxal...() names */
void CDECL genout ARGS((char *));
int	yycontext = -1;
#ifdef unix
#ifdef i386
#  define _MALLOC_H                /* MAW 10-19-94 - prevent malloc.h */
#endif
#endif

/* from scn1.c  wtf get these from a lex header? */
extern int yylex ARGS((void));
extern void yyerror ARGS((char *s));

extern int yyparse ARGS((void));

char *valsplit ARGS((char *));
char *stripquote ARGS((char *));
char *selection_string, *tst;
extern char *yytext;
extern char *datasrc;
extern DDIC *yyddic;
extern APICP *globalcp;
/* prevent macro redef on tokens */

#if defined(_WIN32)
#  ifndef __STDC__
#    define __STDC__
#  endif
#endif

static CONST char	TxSubsetIntersectNotSupported[] =
	"Syntax error: SUBSET/INTERSECT operators not supported yet";
#define CHECK_SUBSET_INTERSECT_ENABLED()	\
	if (!TXApp->subsetIntersectEnabled)	\
	{					\
		putmsg(MERR + UGE, CHARPN, TxSubsetIntersectNotSupported); \
		YYERROR;			\
	}

static CONST char	TxIntersectTokenStr[] = " } ";
#define TX_INTERSECT_TOKEN_STR_LEN	(sizeof(TxIntersectTokenStr) - 1)
static CONST char	TxIntersectLost[] =
	"Internal error: INTERSECT operator lost during parse";
#define CHECK_INTERSECT_PREFIXED(s)		\
	if (strnicmp((s),TxIntersectTokenStr,TX_INTERSECT_TOKEN_STR_LEN) != 0)\
	{					\
		putmsg(MERR, CHARPN, TxIntersectLost);	\
		YYERROR;			\
	}

static const char	TXpasswordNonStringLiteralNotSupported[] =
	"Syntax error: Non-string-literal passwords not supported yet";
#define CHECK_PASSWORD_NON_STRING_LITERAL_ENABLED()		\
	do {							\
	    if (!TX_PWENCRYPT_METHODS_ENABLED(TXApp))		\
	    {							\
		putmsg(MERR + UGE, CHARPN,			\
		       TXpasswordNonStringLiteralNotSupported); \
		YYERROR;					\
	    }							\
	} while (0)

#ifdef EPI_ENABLE_SQL_HEX_CONSTANTS
#  define CHECK_HEX_CONSTANTS_ENABLED()	void
#else /* !EPI_ENABLE_SQL_HEX_CONSTANTS */
static const char       TXnoHexConstants[] =
	"Syntax error: Hexadecimal constants not supported yet";
#  define CHECK_HEX_CONSTANTS_ENABLED()					\
	do 								\
	{								\
		static int	ok = -1;				\
		if (ok < 0) ok = !!getenv("EPI_ENABLE_SQL_HEX_CONSTANTS"); \
		if (!ok)						\
		{							\
			putmsg(MERR + UGE, NULL, TXnoHexConstants);	\
			YYERROR;					\
		}							\
	}								\
	while (0)
#endif /* !EPI_ENABLE_SQL_HEX_CONSTANTS */

static int txalcrtbl =1;
static int txalcrndx =1;
static int txalcrtrig=1;
static int txaldelete=1;
static int txalupdate=1;
static int txalinsert=1;
static int txalselect=1;
static int txalgrant =1;
static int txalrevoke=1;

int stxalcrtbl (int f) {int o=txalcrtbl;  txalcrtbl =f; return(o);}
int stxalcrndx (int f) {int o=txalcrndx;  txalcrndx =f; return(o);}
int stxalcrtrig(int f) {int o=txalcrtrig; txalcrtrig=f; return(o);}
int stxaldelete(int f) {int o=txaldelete; txaldelete=f; return(o);}
int stxalupdate(int f) {int o=txalupdate; txalupdate=f; return(o);}
int stxalinsert(int f) {int o=txalinsert; txalinsert=f; return(o);}
int stxalselect(int f) {int o=txalselect; txalselect=f; return(o);}
int stxalgrant (int f) {int o=txalgrant;  txalgrant =f; return(o);}
int stxalrevoke(int f) {int o=txalrevoke; txalrevoke=f; return(o);}

%}
	/* symbolic tokens */

%union {
	int intval;
	double floatval;
	char *strval;
	int subtok;
}

%token NAME
%token STRING
%token INTNUM APPROXNUM HEXCONST

	/* operators */

/* Operators and their associativity, in increasing precedence: */
%left OR
%left AND
%left NOT
%left TX_INTERSECT
%left <subtok> COMPARISON /* = <> < > <= >= */
%left '+' '-'
%left '*' '/' '%'
%left '\\'
%nonassoc UMINUS

	/* literal keyword tokens */

%token ALL ALTER AMMSC ANY AS ASC AUTHORIZATION BEFORE BETWEEN BY
%token CHARACTER CHECK CLOSE COMMIT CONNECT CONTINUE CONVERT COUNTER CREATE
%token CURRENT
%token CURSOR DATABASE TX_DATE TX_DECIMAL DECLARE DEFAULT TX_DELETE DESC DISTINCT TX_DOUBLE
%token EMPTY ESCAPE EXISTS FETCH TX_FLOAT FOR FOREIGN FOUND FROM GOTO
%token GRANT TX_GROUP HAVING IDENTIFIED TX_IN INDICATOR INDIRECT INSERT INTEGER INTO
%token IS KEY LANGUAGE LIKE LIKEA LIKEIN LIKER LIKEP LINK MATCHES MODULE NULLX
%token NUMERIC OF ON OPEN OPTION ORDER PRECISION PRIMARY PRIVILEGES PROCEDURE
%token PUBLIC REAL REFERENCES ROLLBACK SCHEMA SELECT SET QSTRING
%token SMALLINT SOME SQLCODE SQLERROR STRLST TX_SUBSET TABLE TO UNION
%token UNIQUE TX_UPDATE USER USING VALUES VIEW WHENEVER WHERE WITH WORK
%token COBOL FORTRAN TX_PASCAL PLI ADA INVERTED UNSIGNED NOCASE
%token VARCHAR INDEX TDB TX_BLOB DROP REVOKE TX_BYTE VARBYTE REFERENCING TX_TRIGGER
%token AFTER INSTEAD OLD NEW EACH STATEMENT ROW WHEN SHELL TX_IF

%type <strval>	selection table_exp scalar_exp_commalist from_clause
%type <strval>	table_ref_commalist table_ref_hint table_ref table hintlist
%type <strval>  scalar_exp
%type <strval>	literal column_ref cname search_condition predicate
%type <strval>	comparison_predicate between_predicate like_predicate
%type <strval>	test_for_null in_predicate
/*
%type <strval>  all_or_any_predicate
*/
%type <strval>	subset_predicate
%type <strval>	intersect_exp intersect_predicate
%type <strval>	existence_test where_clause comparison_pref range_variable
%type <strval>	base_table_element base_table_element_commalist
%type <strval>	column_def column table_constraint_def data_type
%type <strval>	opt_column_commalist column_commalist values_or_query_spec
%type <strval>	insert_atom insert_atom_commalist query_spec function_ref
%type <strval>	ammscst opt_where_clause asshead assignment_commalist
%type <strval>	assignment update_statement_searched fmstart
%type <strval>	opt_index_type subquery opt_order_by_clause privileges
%type <strval>	opt_group_by_clause ordering_spec_commalist ordering_spec
%type <strval>	opt_having_clause opt_asc_desc grantee_commalist
%type <strval>	operation_commalist operation grantee user opt_all_distinct
%type <strval>	parameter parameter_ref opt_with_grant_option trigger
%type <strval>	trigger_action_time trigger_event opt_trigger_order
%type <strval>	opt_trigger_references old_new_alias_list old_new_alias
%type <strval>	triggered_action stmt_or_row opt_when_clause executable_code
%type <strval>	manipulative_statement column_def_opt column_def_opt_list
%type <strval>	close_statement commit_statement delete_statement_positioned
%type <strval>	delete_statement_searched fetch_statement insert_statement
%type <strval>	open_statement rollback_statement select_statement atom
%type <strval>	update_statement_positioned select_body
%type <strval>	cursor opt_for_each opt_table_type stringlit atom_commalist
%type <strval>	create_table_as_head subset_predicate_head
%type <strval>	opt_ign_case query_exp query_term opt_with_check_option
%type <strval>	procedure module target target_commalist alter_list
%type <strval>	index_or_all opt_on_table alter_index_action_options
%type <strval>	create_index_option
%type <strval>	create_index_option_list opt_create_index_options
%type <strval>  opt_if_exists
%type <strval>  json_subpath

%%

sql_list:
		sql ';'
/*
	|	sql
	|	sql_list sql ';'
*/
	;

	/* schema definition language */
	/* Note: other ``sql:'' rules appear later in the grammar */
sql:		schema
	|	schema_element
	;

schema:
		CREATE SCHEMA AUTHORIZATION grantee opt_schema_element_list
	;

opt_schema_element_list:
		/* empty */
	|	schema_element_list
	;

schema_element_list:
		schema_element
	|	schema_element_list schema_element
	;

schema_element:
		base_table_def
	|	view_def
	|	privilege_def
	|	index_def
	|	trigger_def
	|	user_def
	|	base_table_drop
	|	index_drop
	|	trigger_drop
	|	user_drop
	|	user_change
	|	link_def
	|	link_drop
	|	table_change
	|	index_change
	;

opt_if_exists:
		/* empty */
		{
			$$ = TXstrdup(TXPMBUFPN, CHARPN, "");
		}
	|	TX_IF EXISTS
		{
			$$ = TXstrdup(TXPMBUFPN, CHARPN, "ifexists");
		}
	;

base_table_drop:	DROP TABLE {yycontext = 8;} table opt_if_exists
		{
			if(!txalcrtbl)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			genout("_* table ");
			genout($4);
			free($4);
			if ($5 && *$5)
			{
				genout(" ");
				genout($5);
			}
			$5 = TXfree($5);
		}
		;

index_drop:	DROP INDEX {yycontext = 7;} table
		{
			if(!txalcrndx)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			genout("_* index ");
			genout($4);
			free($4);
		}
		;

trigger_drop:	DROP TX_TRIGGER trigger
		{
			if(!txalcrtrig)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			genout("_* trigger ");
			genout($3);
			free($3);
		}
		;

user_drop:	DROP USER user
		{
			genout("_* user ");
			genout($3);
			free($3);
		}
		;

link_drop:	DROP DATABASE LINK cname
		{
#ifdef HAVE_LINK_TABLES
			genout("_* link ");
			genout($4);
			free($4);
#else
			free($4);
			putmsg(MERR+UGE, NULL, "REMOTE LINKS not implemented yet");
#endif
		}

link_def:	CREATE DATABASE LINK cname CONNECT TO grantee USING stringlit
		{
#ifdef HAVE_LINK_TABLES
			genout("_M link ");
			genout($4);
			genout(" , ");
			genout($7);
			genout(" ");
			genout($9);
			free($4);
			free($7);
			free($9);
#else
			putmsg(MERR+UGE, NULL, "REMOTE LINKS not implemented yet");
#endif
		}
		;

user_def:	CREATE USER user IDENTIFIED BY cname
		{
			int	res;

			res = createuser(yyddic, $3, $6);
			$3 = TXfree($3);
			$6 = TXfree($6);
			if (res != 0) YYERROR;
		}
		|
		CREATE USER user IDENTIFIED BY stringlit
		{
			char *t;
			int	res;

			t = stripquote($6);
			res = createuser(yyddic, $3, t);
			$3 = TXfree($3);
			$6 = TXfree($6);
			t = TXfree(t);
			if (res != 0) YYERROR;
		}
		|
		CREATE USER user IDENTIFIED BY atom
		{
			CHECK_PASSWORD_NON_STRING_LITERAL_ENABLED();
			genout("_M user ");
			genout($3);
			genout(" ");
			genout($6);
			$3 = TXfree($3);
			$6 = TXfree($6);
		}
		;

user_change:	ALTER USER grantee IDENTIFIED BY cname
		{
			int	res;

			res = changeuser(yyddic, $3, $6);
			$3 = TXfree($3);
			$6 = TXfree($6);
			if (res != 0) YYERROR;
		}
		|
		ALTER USER grantee IDENTIFIED BY stringlit
		{
			char *t;
			int	res;

			t = stripquote($6);
			res = changeuser(yyddic, $3, t);
			$3 = TXfree($3);
			$6 = TXfree($6);
			t = TXfree(t);
			if (res != 0) YYERROR;
		}
		|
		ALTER USER grantee IDENTIFIED BY atom
		{
			CHECK_PASSWORD_NON_STRING_LITERAL_ENABLED();
			genout("_A user ");
			genout($3);		/* user */
			genout(" ");
			genout($6);		/* pass */
			$3 = TXfree($3);
			$6 = TXfree($6);
		}
		;

table_change:	ALTER TABLE table alter_list
		{
			if (!txalselect || !txalupdate)
			{
				putmsg(MERR + UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			genout("_A table ");
			genout($3);		/* table name */
			genout(" ");
			genout($4);		/* action ("COMPACT" etc.) */
			genout("\n");
			$3 = TXfree($3);
			$4 = TXfree($4);
		}
		;

index_or_all:	cname				/* wtf actually index name */
		{
			$$ = $1;
		}
		|
		ALL
		{
			$$ = TXstrdup(TXPMBUFPN, CHARPN, "_l");	/* ALL_OP */
		}
		;

opt_on_table:	ON table
		{
			/* Quote the table name, so readtoken() knows
			 * it is a NAME_OP and not ANY:
			 */
			$$ = TXstrcat3("\"", $2, "\"");
			$2 = TXfree($2);
		}
		|
		{
			$$ = TXstrdup(TXPMBUFPN, CHARPN, "_l"); /* ALL_OP */
		}
		;

alter_index_action_options:	cname
		{
			$$ = $1;
		}
	|	alter_index_action_options cname
		{
			$$ = TXstrcat4(", ", $1, " ", $2);
			$1 = TXfree($1);
			$2 = TXfree($2);
		}
	;

index_change:	ALTER INDEX {yycontext = 18;} index_or_all opt_on_table alter_index_action_options
		{
			if(!txalcrndx)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			genout("_A index ");
			genout($4);		/* "index-name" or ALL_OP */
			genout(" ");
			genout($5);		/* "table-name" or ALL_OP */
			genout(" ");
			genout($6);		/* actions */
			genout("\n");
			$4 = TXfree($4);
			$5 = TXfree($5);
			$6 = TXfree($6);
		}
		;

trigger_def:	CREATE TX_TRIGGER {yycontext = 16;} trigger trigger_action_time
		trigger_event ON table opt_trigger_order opt_trigger_references
		triggered_action
		{
			if(!txalcrtrig)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			createtrigger(yyddic, $4, $5, $6, $8, $9, $10, $11);
			free($4);
			free($5);
			free($6);
			free($8);
			free($9);
			free($10);
			free($11);
		}
		;

trigger_action_time:
			BEFORE
			{
				$$ = strdup("B");
			}
		|	AFTER
			{
				$$ = strdup("A");
			}
		|	INSTEAD OF
			{
				$$ = strdup("I");
			}
		;

trigger_event:		TX_DELETE
			{
				$$ = strdup("D");
			}
		|	INSERT
			{
				$$ = strdup("I");
			}
		|	TX_UPDATE
			{
				$$ = strdup("U");
			}
		;

opt_trigger_order:	ORDER	INTNUM
		{
			$$ = strdup(yytext);
		}
		|
		{
			$$ = strdup("");
		}
		;

opt_trigger_references: REFERENCING old_new_alias_list
		{
			$$ = $2;
		}
		|
		{
			$$ = strdup("");
		}
		;

old_new_alias_list:	old_new_alias
		|	old_new_alias old_new_alias_list
		{
			$$ = TXstrcat3($1, ",",  $2);
			free($1);
			free($2);
		}
		;

old_new_alias:		OLD	cname
		{
			$$ = TXstrcat3("OLD", " ", $2);
			free($2);
		}
		|	NEW	cname
		{
			$$ = TXstrcat3("NEW", " ", $2);
			free($2);
		}
		;

triggered_action:	opt_for_each opt_when_clause executable_code
		{
			$$ = TXstrcat3($1, $2, $3);
			free($1);
			free($2);
			free($3);
		}
		;

opt_for_each	:	FOR EACH stmt_or_row
		{
			$$ = $3;
		}
		|
		{
			$$ = strdup("STATEMENT ");
		}
		;
executable_code:	/* manipulative_statement_list
		{
			$$ = $1;
			clearout();
			putmsg(MWARN+UGE, NULL, "SQL statements are not yet supported.  Stay tuned");
		}
		|*/	SHELL stringlit
		{
			$$ = TXstrcat3("SHELL ", $2, "");
			free($2);
		}
		;

stmt_or_row:		STATEMENT
		{
			$$ = strdup ("STATEMENT ");
		}
		|	ROW
		{
			$$ = strdup ("ROW ");
		}
		;

opt_when_clause:
		{
			$$ = strdup("");
		}
		|	WHEN '(' search_condition ')'
		{
			$$ = $3;
		}
		;

opt_index_type:	TDB
		{
			char	tmp[2];

			tmp[0] = (TX_METAMORPH_DEFAULT_INVERTED(TXApp) ?
				  INDEX_FULL : INDEX_MM);
			tmp[1] = '\0';
			$$ = strdup(tmp);
		}
		|	TDB COUNTER
		{
			char	tmp[2];

			tmp[0] = INDEX_3DB2;
			tmp[1] = '\0';
			$$ = strdup(tmp);
		}
		|	TDB INVERTED
		{
			char	tmp[2];

			tmp[0] = INDEX_FULL;
			tmp[1] = '\0';
			$$ = strdup(tmp);
		}
		|	UNIQUE
		{
			char	tmp[2];

			tmp[0] = INDEX_UNIQUE;
			tmp[1] = '\0';
			$$ = strdup(tmp);
		}
		|	INVERTED
		{
			char	tmp[2];

			tmp[0] = INDEX_INV;
			tmp[1] = '\0';
			$$ = strdup(tmp);
		}
		|
		{
			char	tmp[2];

			tmp[0] = INDEX_BTREE;
			tmp[1] = '\0';
			$$ = strdup(tmp);
		}
		;

create_index_option:
		cname
		{	/* No value: just copy as-is */
			$$ = $1;
		}
	/* An option with a value we turn into an assignment (COLUMN_OP?),
	 * to keep the value associated with its option (and not
	 * misinterpreted as another option in the option list):
	 */
	|	cname atom
		{
			$$ = TXstrcat4("_C ", $1, " ", $2);
			$1 = TXfree($1);
			$2 = TXfree($2);
		}
	|	cname '(' atom_commalist ')'
		{
			$$ = TXstrcat4("_C ", $1, " ", $3);
			$1 = TXfree($1);
			$3 = TXfree($3);
		}
	;

create_index_option_list:
		create_index_option
		{
			$$ = $1;
		}
	|	create_index_option_list create_index_option
		{
			$$ = TXstrcat4(",", $1, " ", $2);
			$1 = TXfree($1);
			$2 = TXfree($2);
		}
	;

opt_create_index_options:
		/* empty */
		{
			$$ = TXstrdup(TXPMBUFPN, CHARPN, "");
		}
	|	WITH create_index_option_list
		{
			$$ = $2;
		}
	;

index_def:
		CREATE opt_index_type INDEX {yycontext = 3;} table ON table '(' ordering_spec_commalist ')' opt_create_index_options
		{
/*	WTF, need to namemangle the index name to get a file name,
particularly for MSDOS, and old Unix file systems with short filenames.
MSDOS	8.3
Unix	14 char

indexname can be up to ~18 chars, + need 2.3 for own use.
*/
			if(!txalcrndx)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
#ifndef NO_KEEP_STATS
			genout($11 && *$11 ? "_M index ,,," : "_M index ,,");
			genout($5);		/* index name */
			genout(" ");
			genout($7);		/* table */
			genout(" ");
			genout($2);		/* index type */
			if ($11 && *$11)
			{
				genout(" ");
				genout($11);	/* options */
			}
			genout(" ");
			genout($9);		/* fields */
#else
			was createindex call;
#endif
			free($2);
			free($5);
			free($7);
			free($9);
			$11 = TXfree($11);
		}
	;

opt_table_type:	cname
	{
		if (!strcmpi($1, "fast"))
		{
			$$ = strdup("F");
		}
		else if (!strcmpi($1, "ram"))
		{
			$$ = strdup("R");
		}
		else if (!strcmpi($1, "compact"))
		{
			$$ = strdup("C");
		}
#ifndef NO_HAVE_BTREE_TABLE
		else if (!strcmpi($1, "btree"))
		{
			$$ = strdup("B");
		}
		else if (!strcmpi($1, "rambtree"))
		{
			$$ = strdup("X");
		}
#endif /* HAVE_BTREE_TABLE */
		else
		{
			yyerror("Unknown table type for CREATE");
			YYERROR;
		}
		free($1);
	}
	|
	{
		$$ = strdup("F");	/* Fast */
	}
	;
base_table_def:
		CREATE opt_table_type TABLE table {yycontext = 4;} '(' base_table_element_commalist ')'
		{

			if(!txalcrtbl && strcmp($2, "R"))
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			genout("_T ");
			genout($2);		/* table type */
			genout(" ");
			genout($7);		/* schema */
			genout(" ");
			genout($4);		/* table name */
			genout("\n");
			free($2);
			free($4);
			free($7);
		}
	|	create_table_as_head query_spec
		{
			genout($2);
			genout(" ");
			genout($1);
			genout("\n");
			free($1);
			free($2);
		}
	;

create_table_as_head:
		CREATE opt_table_type TABLE table {yycontext = 4;} AS
		{
			if(!txalcrtbl)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			genout("_t ");
			genout($2);
			genout(" ");
			free($2);
			$$ = $4;
		}
	;

base_table_element_commalist:
		base_table_element
	|	base_table_element_commalist ',' base_table_element
		{
			$$ = TXstrcat3($1, " ", $3);
			free($1);
			free($3);
		}
	;

base_table_element:
		column_def
	|	table_constraint_def
	;

column_def:
		column data_type column_def_opt_list
		{
			char *t;

			t = TXstrcat4($1, " ", $2, " ");
			$$ = TXstrcat4(" _C " , t, $3, "  _C");
			free($1);
			free($2);
			free($3);
			free(t);
		}
	;

column_def_opt_list:
		/* empty */
	{
		$$=strdup("");
	}
	|	column_def_opt_list column_def_opt
	{
		$$ = TXstrcat3($1, " ", $2);
		free($1);
		free($2);
	}
	;

column_def_opt:
		NOT NULLX
		{
			$$ = strdup("N");
		}
	|	NOT NULLX UNIQUE
		{
			$$ = strdup("U");
		}
	|	NOT NULLX PRIMARY KEY
		{
			$$ = strdup("P");
		}
	|	NULLX
		{
			$$ = strdup("");
		}
	|	DEFAULT literal
		{
			$$ = TXstrcat2("D ", $2);
			free($2);
		}
	|	DEFAULT NULLX
		{
			$$ = strdup("n");
		}
	|	DEFAULT USER
		{
			$$ = strdup("u");
		}
	|	CHECK '(' search_condition ')'
		{
			$$ = strdup("");
		}
	|	REFERENCES table
		{
			$$ = strdup("");
		}
	|	REFERENCES table '(' column_commalist ')'
		{
			$$ = strdup("");
		}
	;

table_constraint_def:
		UNIQUE '(' column_commalist ')'
		{
			$$ = strdup("");
			free($3);
		}
	|	PRIMARY KEY '(' column_commalist ')'
		{
			$$ = strdup("");
			free($4);
		}
	|	FOREIGN KEY '(' column_commalist ')'
			REFERENCES table
		{
			$$ = strdup("");
			free($4);
		}
	|	FOREIGN KEY '(' column_commalist ')'
			REFERENCES table '(' column_commalist ')'
		{
			$$ = strdup("");
			free($4);
			free($9);
		}
	|	CHECK '(' search_condition ')'
		{
			$$ = strdup("");
		}
	;

column_commalist:
		column
	|	column_commalist ',' column
		{
			$$ = TXstrcat4(", ", $1, " ", $3);
			free($1);
			free($3);
		}
	;

view_def:
		CREATE VIEW {yycontext = 5;} table opt_column_commalist
		AS query_exp opt_with_check_option
		{
#ifdef HAVE_VIEWS
			genout("_W ");
			genout($4);
			genout($5);
			genout($7);
			free($4);
			free($5);
			free($7);
			free($8);
#else
			putmsg(MWARN, NULL, "CREATE VIEW not yet supported");
#endif
		}
	;

opt_with_check_option:
		/* empty */
		{
			$$=strdup("");
		}
	|	WITH CHECK OPTION
		{
			$$=strdup("");
		}
	;

opt_column_commalist:
		/* empty */
		{
			$$ = strdup("");
		}
	|	'(' column_commalist ')'
		{
			$$ = TXstrcat3("_C", " ", $2);
			free($2);
		}
	;

privilege_def:
		GRANT {yycontext = 10;} privileges ON table TO grantee_commalist
		opt_with_grant_option
		{
			if(!txalgrant)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			genout("_G ");
			genout($3);
			genout($8);
			genout(" ");
			genout($5);
			genout(" ");
			genout($7);
			free($3);
			free($5);
			free($7);
			free($8);
		}
		|
		REVOKE {yycontext = 13;} privileges ON table TO grantee_commalist
		opt_with_grant_option
		{
			if(!txalrevoke)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			genout("_g ");
			genout($3);
			genout($8);
			genout(" ");
			genout($5);
			genout(" ");
			genout($7);
			free($3);
			free($5);
			free($7);
			free($8);
		}
	;

opt_with_grant_option:
		/* empty */
	{
		$$ = strdup("");
	}
	|	WITH GRANT OPTION
	{
		$$ = strdup("g");
	}
	;

privileges:
		ALL PRIVILEGES
		{
			$$ = strdup("z");
		}
	|	ALL
		{
			$$ = strdup("z");
		}
	|	operation_commalist
		{
			$$ = $1;
		}
	;

operation_commalist:
		operation
		{
			$$ = $1;
		}
	|	operation_commalist ',' operation
		{
			$$ = TXstrcat3($1, "", $3);
			free($1);
			free($3);
		}
	;

operation:
		SELECT
		{
			$$ = strdup("s");
		}
	|	INSERT
		{
			$$ = strdup("i");
		}
	|	TX_DELETE
		{
			$$ = strdup("d");
		}
	|	TX_UPDATE opt_column_commalist
		{
			$$ = strdup("u");
			free($2);
		}
	|	REFERENCES opt_column_commalist
		{
			$$ = strdup("r");
			free($2);
		}
	;


grantee_commalist:
		grantee
		{
			$$ = $1;
		}
	|	grantee_commalist ',' grantee
		{
			$$ = $1;
		}
	;

grantee:
		PUBLIC
		{
			$$ = strdup("PUBLIC");
		}
	|	user
		{
			$$ = $1;
		}
	;

	/* module language */
/*
sql:		module_def
	;

module_def:
		MODULE opt_module
		LANGUAGE lang
		AUTHORIZATION grantee
		opt_cursor_def_list
		procedure_def_list
	;

opt_module:
		/* empty *//*
	|	module
	;


lang:
		COBOL
	|	FORTRAN
	|	TX_PASCAL
	|	PLI
	|	ADA
	|	cname
	;
*/

/*
opt_cursor_def_list:
		/* empty *//*
	|	cursor_def_list
	;

cursor_def_list:
		cursor_def
	|	cursor_def_list cursor_def
	;

cursor_def:
		DECLARE cursor CURSOR FOR query_exp opt_order_by_clause
	;
*/

opt_order_by_clause:
		/* empty */
		{
			$$ = strdup("");
		}
	|	ORDER BY ordering_spec_commalist
		{
			$$ = TXstrcat3(" _O ", $3, " ");
			free($3);
		}
	;

ordering_spec_commalist:
		ordering_spec
	|	ordering_spec_commalist ',' ordering_spec
		{
			$$ = TXstrcat4(",", $1, " ", $3);
			free($1);
			free($3);
		}
	;

ordering_spec:
		column_ref opt_asc_desc opt_ign_case
	{
#ifdef TX_USE_ORDERING_SPEC_NODE
		/* Bug 4425: clearly indicate ASC DESC IGNCASE etc.,
		 * via `_o' with optional `^' `-' flags appended;
		 * flags terminated by whitespace.  Might eventually
                 * become a CSV list of options, ala index WITH options:
		 */
		$$ = TXstrcatN(TXPMBUFPN, __FUNCTION__,
			       "_o", $2, $3, " ", $1, NULL);
#else /* !TX_USE_ORDERING_SPEC_NODE */
		$$ = TXstrcat3($1,$2,$3);
#endif /* !TX_USE_ORDERING_SPEC_NODE */
		free($1);
		free($2);
		free($3);
	}
	|	scalar_exp opt_asc_desc opt_ign_case
	{
		int alldigits=1;
		char *t;
		for(t=$1; *t && alldigits; t++)
			if(!isdigit(*t))
				alldigits=0;
#ifdef TX_USE_ORDERING_SPEC_NODE
		/* Bug 4425: */
		$$ = TXstrcatN(TXPMBUFPN, __FUNCTION__,
			       "_o", $2, $3, " ", (alldigits ? "_" : ""), $1,
			       NULL);
#else /* !TX_USE_ORDERING_SPEC_NODE */
		if(alldigits)
			$$ = TXstrcat4("_",$1,$2,$3);
		else
			$$ = TXstrcat3($1,$2,$3);
#endif /* !TX_USE_ORDERING_SPEC_NODE */
		free($1);
		free($2);
		free($3);
	}
	;

opt_asc_desc:
		/* empty */
	{
		$$ = strdup("");
	}
	|	ASC
	{
		$$ = strdup("");
	}
	|	DESC
	{
		$$ = strdup("-");
	}
	;

opt_ign_case:
		/* empty */
	{
		$$ = strdup("");
	}
	|	NOCASE
	{
		$$ = strdup("^");
	}
	;

/*
procedure_def_list:
		procedure_def
	|	procedure_def_list procedure_def
	;

procedure_def:
		PROCEDURE procedure parameter_def_list ';'
		manipulative_statement_list
	;

manipulative_statement_list:
		manipulative_statement
		{
			$$ = strdup("WTF");
		}
	|	manipulative_statement_list manipulative_statement
		{
			$$ = strdup("WTF");
		}
	;

parameter_def_list:
		parameter_def
	|	parameter_def_list parameter_def
	;

parameter_def:
		parameter data_type
	|	SQLCODE
	;
*/

	/* manipulative statements */

sql:		manipulative_statement
	{
	}
	;

manipulative_statement:
		close_statement
	|	commit_statement
	|	delete_statement_positioned
	|	delete_statement_searched
	|	fetch_statement
	|	insert_statement
	|	open_statement
	|	rollback_statement
	|	select_statement
	|	update_statement_positioned
	|	update_statement_searched
	;

close_statement:
		CLOSE cursor
	{
		$$ = TXstrcat3("CLOSE", " ", $2);
		free($2);
	}
	;

commit_statement:
		COMMIT WORK
	{
		$$ = strdup("COMMIT");
	}
	;

delete_statement_head:
		TX_DELETE {yycontext = 6;} FROM
		{
			if(!txaldelete)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
		}
	;

delete_statement_positioned:
		delete_statement_head table WHERE CURRENT OF cursor
		{
			if(!txaldelete)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
#ifdef NOTYET
			$$ = TXstrcat4("DELETE CURRENT ", $2, " ", $6);
#endif
			free($2);
			free($6);
		}
	;

delete_statement_searched:
		delete_statement_head table opt_where_clause
		{
			if(!txaldelete)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			if ($3 != (char *)NULL && $3[0] != '\0')
			{
				genout("_D ");
				genout($2);
				genout(" ");
				genout($3);
				genout("\n");
#ifdef NOTYET
				$$ = TXstrcat4("_D ", $2, " ", $3);
#endif
			}
			else
			{
				genout("_d ");
				genout($2);
				genout("\n");
#ifdef NOTYET
				$$ = TXstrcat3("_d", " ", $2);
#endif
			}
			free($2);
			free($3);
		}
	;

fetch_statement:
		FETCH cursor INTO target_commalist
		{
			$$ = strdup("WTF");
		}
	;

insert_statement:
		INSERT {yycontext = 11;} INTO table opt_column_commalist values_or_query_spec
		{
			if(!txalinsert)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			if ($5[0] == '\0')
				if ($6[1] == 'T')
				{
					genout("_I ");
					genout($4);
					genout(" ");
					genout($6+2);
					genout("\n");
				}
				else
				{
					genout("_I ");
					genout($4);
					genout(" ");
					genout($6);
					genout("\n");
				}
			else switch ($6[1])
			{
				case 'T' :
					genout("_I ");
					genout($4);
					genout(" ");
					genout($5);
					genout(" ");
					genout($6+2);
					genout("\n");
					break;
				case 'P' :
					genout("_I ");
					genout($4);
					genout(" ");
					genout($5);
					genout(" ");
					genout($6);
					genout("\n");
					break;
				default :
				{
					char **c;
					char **v;
					char *x, *y;
					int i = 0, j = 0, n;

					genout("_I ");
					genout($4);
					genout(" ");
					y = strdup($5);
					x = strtok(y, ", ");
					x = strtok(NULL, ", ");
					while (x)
					{
						i++;
						x = strtok(NULL, ", ");
					}
					free(y);
					c = (char **)calloc(i+1,sizeof(char *));
					i = 0;
					x = strtok($5, ", ");
					x = strtok(NULL, ", ");
					while (x)
					{
						c[i++] = strdup(x);
						x = strtok(NULL, ", ");
					}
					y=strdup($6);
					x = valsplit(y);
					x = valsplit(NULL);
					j = 0;
					while (x)
					{
						j++;
						x = valsplit(NULL);
					}
					free(y);
					v = (char **)calloc(j+1,sizeof(char *));
					x = valsplit($6);
					x = valsplit(NULL);
					j = 0;
					while (x)
					{
						v[j++] = strdup(x);
						x = valsplit(NULL);
					}
					if(i != j)
					{
						putmsg(MWARN, NULL, "Number of values does not agree with number of columns");
						free(c); free(v);
						YYERROR;
					}
					for (n=0 ; n < i-1; n++)
					{
						genout(", _C ");
						genout(c[n]);
						genout(" ");
						genout(v[n]);
						genout(" ");
						free(c[n]);
						free(v[n]);
					}
					genout("_C ");
					genout(c[i-1]);
					genout(" ");
					genout(v[i-1]);
					genout("\n");
					free(c[i-1]);
					free(v[i-1]);
					free(c);
					free(v);
				}
			}
			free($4);
			free($5);
			free($6);
		}
	;

values_or_query_spec:
		VALUES '(' insert_atom_commalist ')'
		{
			$$ = TXstrcat3("_V", " ", $3);
			free($3);
		}
	|	query_spec
		{
			$$ = $1;
		}
	;

insert_atom_commalist:
		insert_atom
		{
			$$ = $1;
		}
	|	insert_atom_commalist ',' insert_atom
		{
			$$ = TXstrcat4(", ", $1, " ", $3);
			free($1);
			free($3);
		}
	;

insert_atom:
/*	This allows any scalar to be inserted, expressions, funcs etc.
		atom
*/
		scalar_exp
		{
			$$ = $1;
		}
	|	NULLX
		{
			/* WTF Need a Better NULL Value */
			$$ = strdup("''");
		}
	;

open_statement:
		OPEN cursor
		{
			$$ = strdup("WTF");
		}
	;

rollback_statement:
		ROLLBACK WORK
		{
			$$ = strdup("WTF");
		}
	;

select_statement: query_exp
		{
			genout($1);
			free($1);
		}

opt_all_distinct:
		/* empty */
	{
		$$ = TXstrdup(TXPMBUFPN, __FUNCTION__, "");
	}
	|	ALL
	{
		$$ = TXstrdup(TXPMBUFPN, __FUNCTION__, "");
	}
	|	DISTINCT
	{
		$$ = TXstrdup(TXPMBUFPN, __FUNCTION__, "_s");
	}
	;

update_head:
		TX_UPDATE {yycontext = 15;
			if(!txalupdate)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
		}
	;

update_statement_positioned:
		update_head table SET assignment_commalist
		WHERE CURRENT OF cursor
		{
			$$ = strdup("WTF");
		}
	;

assignment_commalist:
		{
			$$ = strdup("");
		}
	|	assignment
		{
			$$ = $1;
		}
	|	assignment_commalist ',' assignment
		{
			$$ = TXstrcat4(", ", $1, " ", $3);
			free($1);
			free($3);
		}
	;

asshead:
		column COMPARISON
		{
			if (yytext[0] != '=')
			{
				yyerror("syntax error");
				YYERROR;
			}
		}
	;

assignment:
		asshead scalar_exp
		{
			$$ = TXstrcat4("_C ", $1, " ", $2);
			free($1);
			free($2);
		}
	|	asshead NULLX
		{
			$$ = TXstrcat4("_C ", $1, " ", "''");
			free($1);
		}
	;

update_statement_searched:
		update_head table SET assignment_commalist opt_where_clause
		{
			if(!txalupdate)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			if ($5 == (char *)NULL || $5[0] == '\0')
			{
				genout("_u ");
				genout($2);
				genout(" ");
				genout($4);
				genout("\n");
			}
			else
			{
				genout("_U ");
				genout($2);
				genout(" ");
				genout($4);
				genout(" ");
				genout($5);
				genout("\n");
			}
			free($2);
			free($4);
			free($5);
		}
	;

target_commalist:
		target
	|	target_commalist ',' target
	;

target:
		parameter_ref
	;

opt_where_clause:
		/* empty */
		{
			selection_string = (char *)NULL;
			$$ = strdup("");
		}
	|	where_clause
		{
			selection_string = strdup($1);
			free($1);
			$$ = selection_string;
		}
	;

	/* query expressions */

query_exp:
		query_term
	|	query_exp UNION query_term
	{
		$$=TXstrcat4(" _n ", $1, " ", $3);
		free($1);
		free($3);
	}
	|	query_exp UNION ALL query_term
	;

query_term:
		query_spec
	|	'(' query_exp ')'
	{
		$$=$2;
	}
	;

select_body:
	opt_all_distinct
	selection
	table_exp
	{
		char *t;

		tst = strdup($1);
		if ($2[0] != '\0')
		{
			t = TXstrcat3(tst, "", "_P");
			free(tst);
			tst = t;
		}
		t = TXstrcat3(tst, " ", $3);
		free(tst);
		tst = t;
		if ($2[0] != '\0')
		{
			t = TXstrcat3(tst, " ", $2);
			free(tst);
			tst = t;
		}
		$$ = tst;
		free($1);
		free($2);
		free($3);
	}
	;

query_spec:
		SELECT {yycontext=14;}
		select_body
		{
			if(!txalselect)
			{
				putmsg(MERR+UGE, NULL, "Feature Disabled");
				YYERROR;
			}
			$$ = $3;
		}
	;

selection:
		scalar_exp_commalist
	|	'*'
	{
		$$ = strdup("");
	}
	;

table_exp:
		from_clause
		opt_where_clause
		opt_group_by_clause
		opt_order_by_clause
		{
			if ($2 == (char *)NULL || $2[0] == '\0')
			{
				$$ = TXstrcat3($4, $3, $1);
			}
			else
			{
				char *t;
				t = TXstrcat4(" _S ", $1, " ", $2);
				$$ = TXstrcat3($4, $3, t);
				free(t);
			}
			free($1);
			free($2);
			free($3);
			free($4);
		}
	;

from_clause:
	   /* empty */
	   	{
			$$ = strdup("SYSDUMMY");
		}
	|
		FROM table_ref_commalist
		{
			$$ = $2;
		}
	;

table_ref_commalist:
		table_ref_hint
	|	table_ref_commalist ',' table_ref_hint
		{
			$$ = TXstrcat4("_J", $1, " ", $3);
			free($1);
			free($3);
		}
	;

table_ref_hint:
               table_ref
         |     table_ref WITH '(' hintlist ')'
               {
                  $$ = TXstrcat4("_h", $1, " ", $4);
                  free($4);
               }

hintlist:
               cname
            |  hintlist ',' cname
		{
			$$ = TXstrcat4(", ", $1, " ", $3);
			free($1);
			free($3);
		}

table_ref:
		table
	|	table range_variable
		{
			$$ = TXstrcat4("_R", $1, " ", $2);
			free($1);
			free($2);
		}
	|	table AS range_variable
		{
			$$ = TXstrcat4("_R", $1, " ", $3);
			free($1);
			free($3);
		}
	;

where_clause:
		WHERE search_condition
		{
			$$ = $2;
		}
	;

opt_group_by_clause:
		/* empty */
		{
			$$ = strdup("");
		}
/*
	|	TX_GROUP BY column_ref_commalist opt_having_clause
		{
			$$ = TXstrcat4($4, "_p ", $3, " ");
			free($3);
			free($4);
		}
*//* JMT 98-01-12 Change to allow expressions per Bart's insistence */
	|	TX_GROUP BY scalar_exp_commalist opt_having_clause
		{
			$$ = TXstrcat4($4, "_p ", $3, " ");
			free($3);
			free($4);
		}
	;

/*
column_ref_commalist:
		column_ref
	|	column_ref_commalist ',' column_ref
	{
		$$ = TXstrcat4(",",$1," ", $3);
		free($1);
		free($3);
	}
	;
*/

opt_having_clause:
		/* empty */
		{
			$$ = strdup("");
		}
	|	HAVING search_condition
		{
			$$ = TXstrcat3("_H ", $2, " ");
			free($2);
		}
	;

	/* search conditions */

search_condition:
		search_condition OR search_condition
		{
			$$ = TXstrcat4("|", $1, " ", $3);
			free($1);
			free($3);
		}
	|	search_condition AND search_condition
		{
			$$ = TXstrcat4("&", $1, " ", $3);
			free($1);
			free($3);
		}
	|	NOT search_condition
		{
			$$ = TXstrcat3("^", $2, "");
			free($2);
		}
	|	'(' search_condition ')'
		{
			$$ = $2;
		}
	|	predicate
	;

predicate:
		comparison_predicate
	|	between_predicate
	|	like_predicate
	|	test_for_null
	|	in_predicate
	|	subset_predicate
	|	intersect_predicate
/*
	|	all_or_any_predicate
*/
	|	existence_test
	;

comparison_pref:
		scalar_exp COMPARISON
		{
			$$ = TXstrcat3(yytext, " ", $1);
			free($1);
		}
	;

comparison_predicate:
		comparison_pref scalar_exp
		{
			$$ = TXstrcat3($1, " ", $2);
			free($1);
			free($2);
		}
/*
	|	comparison_pref subquery
		{
			$$ = TXstrcat3($1, " ", $2);
			free($1);
			free($2);
		}
*/
	;

between_predicate:
		scalar_exp NOT BETWEEN scalar_exp AND scalar_exp
		{
			char *t, *t1;

			t = TXstrcat3("|< ", $1, " ");
			t1 = TXstrcat3(t, $4, " > ");
			$$ = TXstrcat4(t1, $1, " ", $6);
			free(t);
			free(t1);
			free($1);
			free($4);
			free($6);
		}
	|	scalar_exp BETWEEN scalar_exp AND scalar_exp
		{
			char *t, *t1;

			t = TXstrcat3("&>= ", $1, " ");
			t1 = TXstrcat3(t, $3, " <= ");
			$$ = TXstrcat4(t1, $1, " ", $5);
			free(t);
			free(t1);
			free($1);
			free($3);
			free($5);
		}
	/* KNG note that leading `(' is needed to distinguish this
	 * as a FOP_TWIXT instead of a scalar/ordinary BETWEEN:
	 */
	|	scalar_exp BETWEEN '(' atom_commalist ')'
		{
			$$ = TXstrcat4("] ", $1, " ", $4);
			free($1);
			free($4);
		}
	/* KNG 20110228 support `BETWEEN (subquery)' as a halfway method
	 * of supporting `BETWEEN (latlon2geocodebox(latLon, radius))';
	 * JMT suggestion:
	 */
	|	scalar_exp BETWEEN subquery
	{
		$$ = TXstrcat4("] ", $1, " ", $3);
		free($1);
		free($3);
	}
	;

like_predicate:
		scalar_exp NOT LIKE scalar_exp opt_escape
		{
			$$ = TXstrcat4("^ ~", $1, " ", $4);
			free($1);
			free($4);
		}
	|	scalar_exp LIKE scalar_exp opt_escape
		{
			$$ = TXstrcat4("~", $1, " ", $3);
			free($1);
			free($3);
		}
	|	scalar_exp LIKEA scalar_exp opt_escape
		{
			$$ = TXstrcat4("_~", $1, " ", $3);
			free($1);
			free($3);
		}
	|	scalar_exp LIKEP scalar_exp opt_escape
		{
			$$ = TXstrcat4("_@", $1, " ", $3);
			free($1);
			free($3);
		}
	|	scalar_exp LIKEIN scalar_exp opt_escape
		{
			$$ = TXstrcat4("_#", $1, " ", $3);
			free($1);
			free($3);
		}
	|	scalar_exp LIKER scalar_exp opt_escape
		{
			$$ = TXstrcat4("@", $1, " ", $3);
			free($1);
			free($3);
		}
	|	scalar_exp MATCHES scalar_exp opt_escape
		{
			$$ = TXstrcat4("#", $1, " ", $3);
			free($1);
			free($3);
		}
	|	scalar_exp NOT MATCHES scalar_exp opt_escape
		{
			$$ = TXstrcat4("^#", $1, " ", $4);
			free($1);
			free($4);
		}
	;

opt_escape:
		/* empty */
	|	ESCAPE atom
	;

test_for_null:
		column_ref IS NOT NULLX
		{ /* WTF Nulls need a proper implementation */
			free($1);
			$$ = strdup("1 = 1");
		}
	|	column_ref IS NULLX
		{ /* WTF Nulls need a proper implementation */
			free($1);
			$$ = strdup("1 = 0");
		}
	;

in_predicate:
		scalar_exp NOT TX_IN subquery
	{
		$$ = TXstrcat4("^ [ ", $1, " ", $4);
		free($1);
		free($4);
	}
	|	scalar_exp TX_IN subquery
	{
		$$ = TXstrcat4("[ ", $1, " ", $3);
		free($1);
		free($3);
	}
	|	scalar_exp NOT TX_IN '(' atom_commalist ')'
	{
		$$ = TXstrcat4("^ [ ", $1, " ", $5);
		free($1);
		free($5);
	}
	|	scalar_exp TX_IN '(' atom_commalist ')'
	{
		$$ = TXstrcat4("[ ", $1, " ", $4);
		free($1);
		free($4);
	}
	|	scalar_exp TX_IN column_ref
	{
		$$ = TXstrcat4("[ ", $1, " ", $3);
		free($1);
		free($3);
	}
	;

subset_predicate_head:
		scalar_exp IS TX_SUBSET OF
	{
		$$ = TXstrcat3("{ ", $1, " ");
		$1 = TXfree($1);
	}
	|	scalar_exp IS NOT TX_SUBSET OF
	{
		$$ = TXstrcat3("^ { ", $1, " ");
		$1 = TXfree($1);
	}
	|	column_ref IS TX_SUBSET OF
	{
		$$ = TXstrcat3("{ ", $1, " ");
		$1 = TXfree($1);
	}
	|	column_ref IS NOT TX_SUBSET OF
	{
		$$ = TXstrcat3("^ { ", $1, " ");
		$1 = TXfree($1);
	}
	|	'(' atom_commalist ')' IS TX_SUBSET OF
	{
		$$ = TXstrcat3("{ ", $2, " ");
		$2 = TXfree($2);
	}
	|	'(' atom_commalist ')' IS NOT TX_SUBSET OF
	{
		$$ = TXstrcat3("^ { ", $2, " ");
		$2 = TXfree($2);
	}
	;

subset_predicate:
		subset_predicate_head subquery
	{
		CHECK_SUBSET_INTERSECT_ENABLED();
		$$ = TXstrcat3($1, " ", $2);
		$1 = TXfree($1);
		$2 = TXfree($2);
	}
	|	subset_predicate_head '(' atom_commalist ')'
	{
		CHECK_SUBSET_INTERSECT_ENABLED();
		$$ = TXstrcat3($1, " ", $3);
		$1 = TXfree($1);
		$3 = TXfree($3);
	}
	|	subset_predicate_head scalar_exp
	{
		CHECK_SUBSET_INTERSECT_ENABLED();
		$$ = TXstrcat3($1, " ", $2);
		$1 = TXfree($1);
		$2 = TXfree($2);
	}
	;

/* Due to grammar conflicts (with e.g. `x like ( 'foo' )') it seems we
 * cannot always allow scalar_exp *and* atom_commalist as INTERSECT args.
 * Try to allow at least some useful stuff from scalar_exp though:  WTF
 */
intersect_exp:
		scalar_exp TX_INTERSECT scalar_exp
	{
		CHECK_SUBSET_INTERSECT_ENABLED();
		$$ = TXstrcat4(TxIntersectTokenStr, $1, " ", $3);
		$1 = TXfree($1);
		$3 = TXfree($3);
	}
	|	scalar_exp TX_INTERSECT '(' atom_commalist ')'
	{
		CHECK_SUBSET_INTERSECT_ENABLED();
		$$ = TXstrcat4(TxIntersectTokenStr, $1, " ", $4);
		$1 = TXfree($1);
		$4 = TXfree($4);
	}
/* wtf this would break `col LIKE ('query')':
	|	'(' atom_commalist ')' TX_INTERSECT ...
	{
		...
	}
*/
	;

intersect_predicate:
	/* NOTE: `intersect_exp_head' already wrote TxIntersectTokenStr;
	 * we need to change it to ` }= ' or ` }! ', hence
	 * `$1 + TX_INTERSECT_TOKEN_STR_LEN' instead of `$1':
	 */
		intersect_exp IS EMPTY
	{
		CHECK_INTERSECT_PREFIXED($1);
		$$ = TXstrcat2(" }= ", $1 + TX_INTERSECT_TOKEN_STR_LEN);
		$1 = TXfree($1);
	}
	|	intersect_exp IS NOT EMPTY
	{
		CHECK_INTERSECT_PREFIXED($1);
		$$ = TXstrcat2(" }! ", $1 + TX_INTERSECT_TOKEN_STR_LEN);
		$1 = TXfree($1);
	}
	;

atom_commalist:
		atom
		{
			$$ = $1;
		}
	|	atom_commalist ',' atom
		{
			$$ = TXstrcat4(", ", $1, " ", $3);
			free($1);
			free($3);
		}
	;

/*
all_or_any_predicate:
		scalar_exp COMPARISON any_all_some subquery
	;

any_all_some:
		ANY
	|	ALL
	|	SOME
	;
*/

existence_test:
		EXISTS subquery
		{
			$$ = TXstrcat2("_e ", $2);
			free($2);
		}
	;

subquery:
		'(' SELECT select_body ')'
		{
			$$ = TXstrcat2("_Q ", $3);
			free($3);
		}
	;

	/* scalar expressions */

scalar_exp:
		scalar_exp '+' scalar_exp
		{
			char *t;
			t = TXstrcat3("+", " ", $1);
			$$ = TXstrcat3(t, " ", $3);
			free(t);
			free($1);
			free($3);
		}
	|	scalar_exp '-' scalar_exp
		{
			char *t;
			t = TXstrcat3("_-", " ", $1);
			$$ = TXstrcat3(t, " ", $3);
			free(t);
			free($1);
			free($3);
		}
	|	scalar_exp '*' scalar_exp
		{
			char *t ;
			t = TXstrcat3("*", " ", $1);
			$$ = TXstrcat3(t, " ", $3);
			free(t);
			free($1);
			free($3);
		}
	|	scalar_exp '/' scalar_exp
		{
			char *t;
			t = TXstrcat3("/", " ", $1);
			$$ = TXstrcat3(t, " ", $3);
			free(t);
			free($1);
			free($3);
		}
	|	scalar_exp '%' scalar_exp
		{
			char *t;

			if (TXApp->sqlModEnabled)
			{
				t = TXstrcat3("%", " ", $1);
				$$ = TXstrcat3(t, " ", $3);
				free(t);
				free($1);
				free($3);
			}
			else
				yyerror("SQL `%' mod operator not supported yet");

		}
	|	intersect_exp
	|	'+' scalar_exp %prec UMINUS
		{
			$$ = $2;
		}
	|	'-' scalar_exp %prec UMINUS
		{
			char	tmp[4];

			/* Bug 7462: emit `_r-1e2' not `-_r1e2': */
			if ($2[0] == '_' &&
			    ($2[1] == 'r' ||	/* _r APPROXNUM */
			     $2[1] == 'x'))	/* _x HEXCONST */
			{
				tmp[0] = $2[0];
				tmp[1] = $2[1];
				tmp[2] = '-';
				tmp[3] = '\0';
				$$ = TXstrcat2(tmp, $2 + 2);
			}
			else
				$$ = TXstrcat2("-", $2);
			free($2);
		}
	|	atom
	|	column_ref
	|	function_ref
	|	subquery
	| '[' atom_commalist ']'
		{
			$$ = TXstrcat2("_[",$2);
			TXfree($2);
		}
	|	'(' scalar_exp ')'
		{
			$$ = $2;
		}
	;

scalar_exp_commalist:
		scalar_exp
	|	scalar_exp_commalist ',' scalar_exp
		{
			$$ = TXstrcat4(",", $1, " ", $3);
			free($1);
			free($3);
		}
	|	scalar_exp range_variable
		{
			$$ = TXstrcat4("_R", $1, " ", $2);
			free($1);
			free($2);
		}
	|	scalar_exp AS range_variable
		{
			$$ = TXstrcat4("_R", $1, " ", $3);
			free($1);
			free($3);
		}
	|	scalar_exp_commalist ',' scalar_exp range_variable
		{
			char	*t;

			t = TXstrcat4("_R", $3, " ", $4);
			$$ = TXstrcat4(",", $1, " ", t);
			free($1);
			free($3);
			free($4);
			free(t);
		}
	|	scalar_exp_commalist ',' scalar_exp AS range_variable
		{
			char	*t;

			t = TXstrcat4("_R", $3, " ", $5);
			$$ = TXstrcat4(",", $1, " ", t);
			free($1);
			free($3);
			free($5);
			free(t);
		}
	;

atom:
		parameter_ref
	|	literal
/*
	|	USER
*/
	;

parameter_ref:
		parameter
/*
	|	parameter parameter
	|	parameter INDICATOR parameter
*/
	;

ammscst:
	AMMSC
	{
		$$ = TXstrcat3("_F", "", strlwr(yytext));
	}
	;
fmstart:
		cname
	{
		$$ = TXstrcat3("_f", "", $1);
		free($1);
	}
	|	TX_DATE
	{
		$$ = "_ftodate";
	}
	;

function_ref:
		ammscst '(' '*' ')'
		{
			$$=TXstrcat3($1, " ", "$star");
			free($1);
		}
	|	ammscst '(' opt_all_distinct scalar_exp ')'
		{
			char *t;

			t=TXstrcat3($1, " ", $3);
			$$=TXstrcat3(t, " ", $4);
			free($1);
			free($3);
			free($4);
			free(t);
		}
	|	ammscst '(' scalar_exp ')'
		{
			$$=TXstrcat3($1, " ", $3);
			free($1);
			free($3);
		}
	|	ammscst '(' search_condition ')'
		{
			$$=TXstrcat3($1, " ", $3);
			free($1);
			free($3);
		}
	|	fmstart '(' scalar_exp_commalist ')'
		{
			if (strcmp($1, "_ftodate"))
				$$=TXstrcat3($1, " ", $3);
			else
			{
				time_t *t;

				t = strtodate($3+1);
				$$ = (char *)malloc(12);
				sprintf($$, "%lu", *t);
				free(t);
			}
			free($1);
			free($3);
		}
	|	CONVERT '(' scalar_exp ',' stringlit ')'
	{
		$$ = TXstrcat4("_v ", $3, " ", $5);
		free($3);
		free($5);
	}
	|	CONVERT '(' scalar_exp ',' stringlit ',' scalar_exp ')'
	{
		/* Call convert() SQL function.  Note that second arg
		 * is assumed to be a string literal in predtype(),
		 * just as with two-arg convert():
		 */
		$$ = TXstrcatN(TXPMBUFPN, CHARPN,
			       "_fconvert ,,", $3, " ", $5, " ", $7, NULL);
		$3 = TXfree($3);
		$5 = TXfree($5);
		$7 = TXfree($7);
	}
	/* KNG 2014 temp to allow creation of NULL values for Bug 5395,
	 * until NULL fully supported (i.e. until NULL can be a scalar_exp):
	 */
	|	CONVERT '(' NULLX ',' stringlit ')'
	{
		$$ = TXstrcat2("_v _N ", $5);
		free($5);
	}
/*
	|	CONVERT '(' data_type ',' scalar_exp ')'
	{
		char *t;

		t = TXstrcat3("'", $3, "'");
		$$ = TXstrcat4("_v ", $5, " ", t);
		free($3);
		free($5);
		free(t);
	}
*/
	;

stringlit:
		STRING
		{
			$$ = strdup(yytext);
		}
	|	stringlit STRING
		{
			$$ = TXstrcat3($1, "", yytext);
			free($1);
		}
		;

literal:
		stringlit
		{
			$$ = $1;
		}
	|	INTNUM
		{
			$$ = strdup(yytext);
		}
	|	HEXCONST
		{
			CHECK_HEX_CONSTANTS_ENABLED();
			$$ = TXstrcat2("_x", yytext);
		}
	|	APPROXNUM
		{
			$$ = TXstrcat2("_r", yytext);
			/* see also `_r' fixup for '-' scalar_exp above */
		}
	|	COUNTER
		{
			$$ = strdup("_c");
		}
	;

	/* miscellaneous */

trigger:	cname
	;

	/* KNG alter_list is just a single arg for now e.g. "compact": */
alter_list:	cname
	;

table:
		cname
	|	cname '.' NAME
		{
/*	This is the new way
			$$ = TXstrcat3($1, ".", yytext);
*/
			free($1);
			$$ = strdup(yytext);
		}
	|	PUBLIC '.' NAME
		{
/*	This is the new way
			$$ = TXstrcat3($1, ".", yytext);
*/
			$$ = strdup(yytext);
		}
	|	cname '@' cname
	{
#ifdef HAVE_LINK_TABLES
			$$ = TXstrcat3($1, "@", $3);
			free($1);
			free($3);
#else
			yyerror("Link tables not supported yet");
#endif
	}
	;

cname : NAME
         {
            $$ = strdup(yytext);
         }
        /*
        | '$' NAME
        {
               $$ = TXstrcat2("$", yytext);
        }
        */
	| QSTRING
	{	/* JMT 97-12-05 */
		size_t sz;

		sz = strlen(yytext)-1;
		$$ = malloc(sz);
		strncpy($$, yytext+1, sz-1);
		$$[sz-1]='\0';
	}
	;

json_subpath: '.' cname
            {
               $$ = TXstrcat2(".", $2);
               free($2);
            }
         | '[' INTNUM
            {
							tst = strdup(yytext);
            } ']' {
							$$ = TXstrcat3("[", tst, "]");
							tst = TXfree(tst);
						}
         | json_subpath '.' cname
            {
               $$ = TXstrcat3($1, ".", $3);
               free($1);
               free($3);
            }
         | json_subpath '[' INTNUM
            {
							tst = strdup(yytext);
						} ']' {
               $$ = TXstrcat4($1, "[", tst, "]");
               TXfree($1);
							 tst = TXfree(tst);
            }
         | json_subpath '[' '*' ']'
            {
               $$ = TXstrcat2($1, "[*]");
               TXfree($1);
            }
         ;


column_ref:
	cname
	|	cname '.' cname
		{
			$$ = TXstrcat3($1, ".", $3);
			free($1);
			free($3);
		}
	|   '$' cname
        {
               $$ = TXstrcat2("$", $2);
               free($2);
        }
	|	cname '.' cname
		{
			$$ = TXstrcat3($1, ".", $3);
			free($1);
			free($3);
		}
	|	cname '.' '$' NAME
		{
			$$ = TXstrcat3($1, ".$", yytext);
			free($1);
		}
	|	cname '.' cname '.' NAME
		{
			$$ = TXstrcat3($3, ".", yytext);
			free($1);
			free($3);
		}
	|	PUBLIC '.' cname '.' cname
		{
			$$ = TXstrcat3($3, ".", $5);
			free($3);
			free($5);
		}
	|	column_ref '\\' column_ref
		{
			$$ = TXstrcat3($1, "\\", $3);
			free($1);
			free($3);
		}
        |       cname '.' '$' json_subpath
               {
			$$ = TXstrcat3($1, ".$", $4);
			free($1);
			free($4);
                        /*
                        */
               }
	;

		/* data types */

data_type:
		CHARACTER
		{
			$$ = strdup("char");
		}
	|	CHARACTER '(' INTNUM
		{
			tst = strdup(yytext);
		} ')'
		{
			$$ = TXstrcat3("char", " ", tst);
			free(tst);
		}
	|	VARCHAR '(' INTNUM
		{
			tst = strdup(yytext);
		} ')'
		{
			$$ = TXstrcat3("varchar", " ", tst);
			free(tst);
		}
	|
		TX_BYTE
		{
			$$ = strdup("byte");
		}
	|	TX_BYTE '(' INTNUM
		{
			tst = strdup(yytext);
		} ')'
		{
			$$ = TXstrcat3("byte", " ", tst);
			free(tst);
		}
	|	VARBYTE '(' INTNUM
		{
			tst = strdup(yytext);
		} ')'
		{
			$$ = TXstrcat3("varbyte", " ", tst);
			free(tst);
		}
	|	NUMERIC
		{
		/* WTF Numeric's and Decimals */

			$$ = strdup("double");
		}
	|	NUMERIC '(' INTNUM ')'
		{
			$$ = strdup("double");
		}
	|	NUMERIC '(' INTNUM ',' INTNUM ')'
		{
			$$ = strdup("double");
		}
	|	TX_DECIMAL
		{
			$$ = strdup("double");
		}
	|	TX_DECIMAL '(' INTNUM ')'
		{
			$$ = strdup("double");
		}
	|	TX_DECIMAL '(' INTNUM ',' INTNUM ')'
		{
			$$ = strdup("double");
		}
	|	INTEGER
		{
			$$ = strdup("int");
		}
	|	INTEGER '(' INTNUM
		{
			tst = strdup(yytext);
		} ')'
		{
			$$ = TXstrcat3("int", " ", tst);
			free(tst);
		}
	|	SMALLINT
		{
			$$ = strdup("smallint");
		}
	|	TX_FLOAT
		{
			$$ = strdup("float");
		}
	|	TX_FLOAT '(' INTNUM ')'
		{
			$$ = strdup("?");
		}
	|	REAL
		{
			$$ = strdup("float");
		}
	|	TX_DOUBLE
		{
			$$ = strdup("double");
		}
	|	TX_DOUBLE PRECISION
		{
			$$ = strdup("double");
		}
	|	INDIRECT
		{
			$$ = strdup("varind");
		}
	|	TX_DATE
		{
			$$ = strdup("date");
		}
	|	TX_BLOB
		{
			$$ = strdup("blob");
		}
	|	COUNTER
		{
			$$ = strdup("counter");
		}
	|	STRLST
		{
			$$ = strdup("varstrlst");
		}
	|	UNSIGNED INTEGER
		{
			$$ = strdup("dword");
		}
	|	UNSIGNED SMALLINT
		{
			$$ = strdup("word");
		}
	|	cname
		{
			if(strcmpi($1, "longvarchar") == 0)
			{
				$$ = strdup("varchar 10000");
				free($1);
			} else if(strcmpi($1, "longvarbyte") == 0)
			{
				$$ = strdup("varbyte 10000");
				free($1);
			} else if(strcmp($1, "ind") == 0)
			{
				$$ = strdup("varind");
				free($1);
			}
			else
				$$ = $1;
		}
	/* KNG 20060711 for varint(N), [var]{long|int64|uint64}(N): */
	|	cname '(' INTNUM
		{
			tst = strdup(yytext);
		} ')'
		{
			/* Only allow tested types for now: KNG 20060712 */
			if (strcmp($1, "varint") != 0 &&
			    strcmp($1, "byte") != 0 &&
			    strcmp($1, "varbyte") != 0 &&
			    strcmp($1, "char") != 0 &&
			    strcmp($1, "varchar") != 0 &&
			    strcmp($1, "long") != 0 &&
			    strcmp($1, "varlong") != 0 &&
			    strcmp($1, "int64") != 0 &&
			    strcmp($1, "varint64") != 0 &&
			    strcmp($1, "uint64") != 0 &&
			    strcmp($1, "varuint64") != 0)
			{
				putmsg(MERR + UGE, CHARPN,
		"Syntax error: Type %s may not be multi-item in this context",
					$1);
				YYERROR;
			}
			$$ = TXstrcat3($1, " ", tst);
			free($1);
			free(tst);
			tst = CHARPN;
		}
	;

	/* the various things you can name */

column:		cname
	;

cursor:		cname
	;

module:		cname
	;

parameter:
		'?'
		{
#ifndef OLDPARAM
			$$ = malloc(20);
			sprintf($$, "?%d", TXnextparamnum());
#else
			$$ = strdup("?");
#endif
		}
        |      ':' cname
        {
               $$ = TXstrcat2(":", $2);
               free($2);
        }
	;

procedure:	cname
	;

range_variable:	cname
	;

user:		cname
	;

sql:	SET {yycontext = 17;} assignment
	{
		genout("_q ");
		genout($3);
		free($3);
	}
	;

	/* embedded condition things */
sql:		WHENEVER NOT FOUND when_action
	|	WHENEVER SQLERROR when_action
	;

when_action:	GOTO NAME
	|	CONTINUE
	;
%%
