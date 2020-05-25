#ifndef RLEX_H
#define RLEX_H

#ifndef MMAPI_H
#include "api3.h"
#endif

/*

REXLEX Copyright (C) 1993, P. Barton Richards

This set of functions comprise a pattern matcher that is functionally
similar to the REX call set except it matches multiple expressions
simultaneously. It will return pointers to matched expressions in the
order that they occur within the buffer. If two expressions are matched
at the same location, it will return the longer one first.

The open call is passed a list of pointers to REX expressions with the
last string in the list pointing to a '\0'.  The get call is exactly the
same as getrex() or getmmapi()

IE:
getrlex(RLEX *,byte *buf,byte *endbuf,SEARCHNEWBUF || CONTINUESEARCH);

There are two calls that are informative about a matched expression:

int lexlen(RLEX *rl);    returns the length of the matched expression

int rlexn (RLEX *rl);    returns an index into the expression list that
                         indicates which of the expressions was matched


The closerlex() call releases the memory for this object.

*/


/************************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif

/************************************************************************/

#define RLIT struct rexlexitem
#define RLITPN (RLIT *)NULL
RLIT
{
 FFS   *ex; /* the expression ptr */
 byte *hit; /* location of hit */
 byte *end; /* end of hit */
 int   len; /* length of hit */
 int index; /* array member of expression list passed on open */
 int    op; /* opcode SEARCHNEWBUF || CONTINUESEARCH */
};

/************************************************************************/

typedef struct rexlexobj RLEX;
#define RLEXPN (RLEX *)NULL
struct rexlexobj
{
 RLIT  *ilst;
 int     i,n; /* current item in list and number of items */
 int  (CDECL *cmp) ARGS((CONST void *a,CONST void *b));
 byte   *lastMatchEnd;
 TXbool gotEof;
};

/* Special REX object for rexlex for TX_REX_TOKEN_NO_MATCH (`\<nomatch\>'): */
#define TX_FFS_NO_MATCH         ((FFS *)1)

/************************************************************************/
/* prototypes go here */

extern RLEX  *closerlex ARGS((RLEX *rl));
extern void   resetrlex ARGS((RLEX *rl, int op));
extern RLEX  *openrlex(const char **explst, TXrexSyntax syntax);
extern RLEX  *openrlexadd ARGS((int n));                      /* KNG 990913 */
extern int    rlex_addexp(RLEX *rl, int i, const char *ex, TXrexSyntax syntax);
TXbool        TXrlexDoneAdding(RLEX *rlex);
extern int    rlexlen ARGS((RLEX *rl));
extern int    rlexn ARGS((RLEX *rl));
extern byte  *rlexhit ARGS((RLEX *rl));
extern byte  *rlexfirst ARGS((RLEX *rl));
extern int    rlexflen ARGS((RLEX *rl));
extern byte  *getrlex ARGS((RLEX *rl,byte  *buf,byte  *end,int  op));

int       vokrex ARGS((FFS *rex, char *expr));

/************************************************************************/
#endif /* RLEX_H */

