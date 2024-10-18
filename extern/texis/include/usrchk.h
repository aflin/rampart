#ifndef USRCHK_H
#define USRCHK_H
/**********************************************************************/
#ifndef ARGS
#  if defined(LINT_ARGS) || defined(__STDC__)
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
extern int userok ARGS((CGI *,int *tm));
/**********************************************************************/
#endif                                                    /* USRCHK_H */
