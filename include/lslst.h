#ifndef LSLST_H
#define LSLST_H
/**********************************************************************/
#define LSLST struct lslst_struct
#define LSLSTPN (LSLST *)NULL
LSLST
{
   void *st;
   uint descend:1;
   uint superr_perm:1;
   uint hiddir:1;            /* wtf - ni - currently returns all dirs */
   uint hidfile:1;
   uint dirs:1;
   uint invert:1;
   uint getstat:1;
};
/**********************************************************************/
extern LSLST *lslopen  ARGS((void));
extern LSLST *lslclose ARGS((LSLST *ls));
extern char **lslget   ARGS((LSLST *ls,char *pathexp));
#define lslstats(a)         ((struct stat *)(a)->st)
#define lslstat(a,i)        ((struct stat *)(a)->st+i)
#define lsldescend(a,b)     ((a)->descend=(b))
#define lslsuperr_perm(a,b) ((a)->superr_perm=(b))
#define lslhiddir(a,b)      ((a)->hiddir=(b))
#define lslhidfile(a,b)     ((a)->hidfile=(b))
#define lslhid(a,b)         ((a)->hidfile=(a)->hiddir=(b))
#define lsldirs(a,b)        ((a)->dirs=(b))
#define lslinvert(a,b)      ((a)->invert=(b))
#define lslgetstat(a,b)     ((a)->getstat=(b))
/**********************************************************************/
#endif                                                     /* LSLST_H */
