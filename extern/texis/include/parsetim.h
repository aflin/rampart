#ifndef PARSETIM_H
#define PARSETIM_H
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
extern time_t parsetime(const char *buf, size_t bufSz);
extern int TXresettimecache ARGS((void));
/**********************************************************************/
#endif                                                  /* PARSETIM_H */
