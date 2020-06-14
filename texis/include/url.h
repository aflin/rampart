#ifndef URL_H
#define URL_H
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
extern int   isurl     ARGS((char *s));
extern char *urltype   ARGS((char *url));
extern char *urlhost   ARGS((char *url));
extern char *urlport   ARGS((char *url));
extern char *urluser   ARGS((char *url));
extern char *urlgroup  ARGS((char *url));
extern char *urlpass   ARGS((char *url));
extern char *urlfn     ARGS((char *url));
extern long *urloffs   ARGS((char *url,int *npairs));
extern char *urlrest   ARGS((char *url));
/**********************************************************************/
#endif                                                       /* URL_H */
