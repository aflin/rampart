#ifndef WILD_H
#define WILD_H

#ifndef TX_CONFIG_H
#  include "txcoreconfig.h"
#endif


/***********************************************************************
** @(#)wild.h - shell-like wildcard matching functions - MAW - 04-02-91
** '?'  means any char
** '*'  means any chars
** '[]' means any of a set of chars
***********************************************************************/
#define WILD void
#define WILDPN ((WILD *)NULL)

/**********************************************************************/

typedef enum TXwildFlag_tag
{
  TXwildFlag_None       = 0x0,
  TXwildFlag_TrailStar  = 0x1,          /* map trailing `*.*' to `*' */
  TXwildFlag_IgnoreCase = 0x2,
}
TXwildFlag;

extern WILD *openwild  ARGS((char *expr));
extern WILD *openwild2  ARGS((CONST char *expr, CONST char *end,
                     CONST char *fixedEnd, char escChar, TXwildFlag flags));
#if defined(MSDOS)
#  ifdef EPI_CASE_INSENSITIVE_PATHS
#    define TX_WILD_DEFAULT_FLAGS       \
  (TXwildFlag_TrailStar | TXwildFlag_IgnoreCase)
#  else
#    define TX_WILD_DEFAULT_FLAGS       TXwildFlag_TrailStar
#  endif
#else /* !MSDOS */
#  ifdef EPI_CASE_INSENSITIVE_PATHS
#    define TX_WILD_DEFAULT_FLAGS       TXwildFlag_IgnoreCase
#  else
#    define TX_WILD_DEFAULT_FLAGS       TXwildFlag_None
#  endif
#endif /* !MSDOS */

extern WILD *closewild ARGS((WILD *w));
extern int  wildmatch  ARGS((WILD *w,char *fn));
extern WILD *wilddup   ARGS((WILD *w));
extern CONST char *TXwildFindFirstWildcard ARGS((CONST char *pathname,
                                CONST char *pathnameEnd, int ignorePathSep));

/**********************************************************************/
#endif                                                      /* WILD_H */
