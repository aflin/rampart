#ifndef OTREE_H
#define OTREE_H
/* changed to an 2-3-4 tree structure (Sedgewick) PBR 11-08-91 */
/**********************************************************************/
#define OTBR struct otbr_struct
#define OTBRPN (OTBR *)NULL
OTBR
{
 OTBR *h;                                                  /* hi node */
 OTBR *l;                                                 /* low node */
 char red;                                            /* PBR 11-08-91 */
 void *s;                                                     /* data */
};

#define OTREE struct otree_struct
#define OTREEPN (OTREE *)NULL
OTREE
{
 OTBR   *root;                                /* the root of the tree */
 OTBR   *z;                                           /* PBR 11-08-91 */
 OTBR    zdummy;                                      /* PBR 11-10-91 */
 ulong   cnt;                                 /* number of live nodes */
 ulong   dupcnt;                  /* number of duplicates encountered */
 int     allowdups;                 /* allow duplications in the tree */
 int     allowTreeMods;    /* allow new nodes / rebalancing on insert */
 int   (*cmp)ARGS((void *s1,void *s2));        /* comparison function */
 int   (*afunc)ARGS((void **cur,void *darg));    /* user add function */
 void   *aarg;                                  /* argument for above */
 int   (*dfunc)ARGS((void **cur,void *newv,void *darg));/* user dup function */
 void   *darg;                                  /* argument for above */
 int   (*wfunc)ARGS((void *cur,void *warg));    /* user walk function */
 void   *warg;                                  /* argument for above */
};
                               /* return values for user dup function */
#define OTR_ERROR  (-1)                                /* error, halt */
#define OTR_ADD    1                                /* add it to tree */
#define OTR_IGNORE 2                                     /* ignore it */
/**********************************************************************/
#define setotafunc(o,u) ((o)->afunc=(u))
#define setotaarg(o,u)  ((o)->aarg=(void *)(u))
#define setotdfunc(o,u) ((o)->dfunc=(u))
#define setotdarg(o,u)  ((o)->darg=(void *)(u))
#define setotwfunc(o,u) ((o)->wfunc=(u))
#define setotwarg(o,u)  ((o)->warg=(void *)(u))
#define setotdups(o,f)  ((o)->allowdups=(f))
#define getotcnt(o)     ((o)->cnt)
#define getotdupcnt(o)  ((o)->dupcnt)

OTREE *closeotree ARGS((OTREE *tr));
OTREE *openotree  ARGS((int (*cmp)ARGS((void *s1,void *s2)),void *low));
#define addotree(t,s) addotree2(t,s,(void **)NULL)
int    addotree2  ARGS((OTREE *tr,void *s,void **stored));
void  *findotree  ARGS((OTREE *tr,void *s));
int    walkotree  ARGS((OTREE *tr));
void **otree2lst  ARGS((OTREE *tr,int *cnt));
int countTree ARGS((OTREE *tr));
int dumpAndWalkTree  ARGS((OTREE *tr));

/**********************************************************************/
#endif                                                     /* OTREE_H */
