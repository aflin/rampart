#ifndef NFNLIST_H
#define NFNLIST_H
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif                                                        /* ARGS */
/**********************************************************************/
#define LISTOFILES '&'

#define NFNS struct fn
#define NFNSPN (NFNS *)NULL
NFNS {
   int  ac;
   char **av;
   FILE *fp;
   char buf[BUFSIZ];
   int  rcase;           /* MAW 01-02-97 - to prevent strlwr on MSDOS */
};

extern NFNS  *opennfn  ARGS((int,char **));
extern NFNS  *closenfn ARGS((NFNS *));
extern char  *getnfn   ARGS((NFNS *));
extern int    rcasenfn ARGS((NFNS *nf,int respectcase));/* MAW 01-02-97 */
/**********************************************************************/
#endif                                                   /* NFNLIST_H */
