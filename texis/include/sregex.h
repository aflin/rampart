#ifndef SREGEX_H
#define SREGEX_H
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif                                                        /* ARGS */
/**********************************************************************/
extern size_t sreglen    ARGS((char *));
#define TX_REGEXP_DEFAULT_ESC_CHAR      '\\'
extern char  *sregcmp    ARGS((CONST char *expr, int escChar));
extern char  *sregex     ARGS((char *,char *));
extern char  *sregdup    ARGS((char *));
extern int    sregprefix ARGS((char *a, char *b, size_t n, size_t *sz, int));
/**********************************************************************/
#endif                                                    /* SREGEX_H */
