#ifndef WAS_H
#define WAS_H
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
#define WAS struct was_struct
#define WASPN (WAS *)NULL
WAS;

#define WASI struct wasi_struct
#define WASIPN (WASI *)NULL
WASI                          /* wide area search item - internal use */
{
   int     id;                              /* unique id of this item */
   SERVER *se;                     /* server this item is logged into */
   SRCH   *sr;                                       /* opened search */
   WAS    *wa;                                /* point back to parent */
   int     fini;                            /* is the search finished */
};

WAS                                               /* wide area search */
{
   WASI *wi;                                     /* array of searches */
   int   ni;                            /* number of items used in wi */
   int   wisz;                       /* number of items alloced in wi */
   void *usr;                                /* WAS user callback ptr */
   int (*cb)();                         /* WAS user callback function */
};
/**********************************************************************/
#define numwas(a) ((a)->ni)
WAS *closewas  ARGS((WAS *wa));
WAS *openwas   ARGS((char *hostlistfn));
int  resetwas  ARGS((WAS *wa,char *hostlistfn));
int  readwas   ARGS((WAS *wa,char *hostlistfn));
int  addwas    ARGS((WAS *wa,char *url));
int  reghitwas ARGS((WAS *wa,void *usr,int (*hitcb)() ));
int  searchwas ARGS((WAS *wa,char *qry));
/**********************************************************************/
#endif                                                       /* WAS_H */
