#ifndef EQSED_H
#define EQSED_H
/**********************************************************************/
/* user equiv source edit */
#define EQEDLST struct eqedlst_struct
#define EQEDLSTPN (EQEDLST *)NULL
EQEDLST {
#ifdef EQSEDHARDWAY
   int       written;
#endif
   EQVLST   *eql;
   EQEDLST  *next;
};
#define EQSED struct eqsed_struct
#define EQSEDPN (EQSED *)NULL
EQSED {
   char *fn;
#ifdef EQSEDHARDWAY
   FILE *fp;
   char *buf;
   int   bufsz;
#endif
   EQEDLST *eqel;
};
/**********************************************************************/
extern EQSED *closeeqsed ARGS((EQSED *eqse));
extern EQSED *openeqsed  ARGS((char *fn));
extern int    addeqsed   ARGS((EQSED *eqse,char *root,char *wrd,char *class,int op));
extern int    clreqsed   ARGS((EQSED *eqse,char *root));
extern int    disceqsed  ARGS((EQSED *eqse));
extern int    applyeqsed ARGS((EQSED *eqse));
/**********************************************************************/
#endif                                                     /* EQSED_H */
