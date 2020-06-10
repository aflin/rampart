#ifndef FLST_H
#define FLST_H
/***********************************************************************
** ignore and allow usage:
** given a filename:
**    if(the allow list s not empty &&
**       a filename match is NOT in the allow list) don't index it;
**    if(a filename match IS in the ignore list) don't index it;
**    therefore if both lists are empty, any file will be accepted
***********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
/* these are only used internally */
#ifndef WILD
#  define WILD void
#endif
#ifndef LSR
#  define LSR void
#endif
/**********************************************************************/
#define FLWLST struct flwlst_struct
#define FLWLSTPN (FLWLST *)NULL
FLWLST {
   WILD **lst;
   char  *lstflg;
};

#define FLST struct flst_struct
#define FLSTPN (FLST *)NULL
FLST {
   char  **paths;
   int     ip;
   FLWLST *alw;
   FLWLST *ign;
   LSR    *ls;
   uint    descend:1;
   uint    superr_perm:1;
   uint    hiddir:1;         /* wtf - ni - currently returns all dirs */
   uint    hidfile:1;
   struct stat st;
};
/**********************************************************************/
extern FLST *flstclose  ARGS((FLST *fl));
extern FLST *flstopen   ARGS((char **paths));
extern FLST *flstallow  ARGS((FLST *fl,char **allow));
extern FLST *flstignore ARGS((FLST *fl,char **ignore));
extern char *flstgetst  ARGS((FLST *fl,struct stat *st));
#define flstget(a) flstgetst((a),(struct stat *)NULL)
#define flstdescend(a,b) ((a)->descend=(b))
#define flstsuperr_perm(a,b) ((a)->superr_perm=(b))
#define flsthiddir(a,b) ((a)->hiddir=(b))
#define flsthidfile(a,b) ((a)->hidfile=(b))
#define flsthid(a,b) ((a)->hidfile=(a)->hiddir=(b))
/**********************************************************************/
#endif                                                      /* FLST_H */
