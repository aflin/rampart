#ifndef SLIST_H
#define SLIST_H
/**********************************************************************/
#define SLIST struct slist
#define SLISTPN (SLIST *)NULL
SLIST {
   char **s;
   int  cnt;
   int  max;
   char *buf;
   char *p;
   int  bused;
   int  bsz;
};
/**********************************************************************/
#define slcnt(a) ((a)->cnt)
#define slarr(a) ((a)->s)

#ifdef __cplusplus
#ifndef ARGS
#  define ARGS(a) a
#endif
extern "C" {
#endif
SLIST *slopen    ARGS((void));
SLIST *sldup     ARGS((SLIST *));
SLIST *slclose   ARGS((SLIST *));
void   slwipe    ARGS((SLIST *));
char  *sladd     ARGS((SLIST *,char *));
char  *sldel     ARGS((SLIST *,char *));
char  *slfind    ARGS((SLIST *,char *));
char  *sladdclst ARGS((SLIST *,int,char **));
char  *sladdslst ARGS((SLIST *sl, SLIST *nsl, int unique));
SLIST *slistrename ARGS((SLIST *, char *));
#ifdef __cplusplus
}
#endif

/**********************************************************************/
#endif                                                     /* SLIST_H */
