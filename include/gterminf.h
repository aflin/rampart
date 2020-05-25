#ifndef GTERMINF_H
#define GTERMINF_H
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
extern void gtermrowcols ARGS((int *r,int *c));
extern int  gtermcols ARGS((void));
extern int  gtermrows ARGS((void));
/**********************************************************************/
#endif                                                  /* GTERMINF_H */
