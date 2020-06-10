#ifndef TX_STRCAT_H
#define TX_STRCAT_H

#include "mmsg.h"                               /* for TXPMBUF */

#ifndef TXALLOC_PROTO
#ifdef MEMDEBUG
#  define TXALLOC_PROTO	, CONST char *file, int line, CONST char *memo
#else /* MEMDEBUG */
#  define TXALLOC_PROTO
#endif /* MEMDEBUG */
#endif /* TXALLOC_PROTO */

char	*TXstrcat2 ARGS((CONST char *a, CONST char *b  TXALLOC_PROTO));
char	*TXstrcat3 ARGS((CONST char *a, CONST char *b, CONST char *c TXALLOC_PROTO));
char	*TXstrcat4 ARGS((CONST char *a, CONST char *b, CONST char *c, CONST char *d TXALLOC_PROTO));
char	* CDECL TXstrcatN ARGS((TXPMBUF *pmbuf, CONST char *fn, ...));
char	* CDECL TXstrNcatN(TXPMBUF *pmbuf, const char *fn, ...);

#ifdef MEMDEBUG
#  define TXstrcat2(a, b)       TXstrcat2(a, b, __FILE__, __LINE__, CHARPN)
#  define TXstrcat3(a, b, c)    TXstrcat3(a, b, c, __FILE__, __LINE__, CHARPN)
#  define TXstrcat4(a, b, c, d) TXstrcat4(a, b, c, d, __FILE__, __LINE__, CHARPN)
#endif /* MEMDEBUG */

char *valsplit ARGS((char *s));
char *stripquote ARGS((char *si));
int   jtstrcmp ARGS((char *a, char *b, int c, char *d));

#endif /* TX_STRCAT_H */
