#ifndef MDPAR_H
#define MDPAR_H
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
#if defined(i386) && !defined(__STDC__)
#  define size_t int
#endif
#define MDP struct mm_3db_parse
#define MDPPN (MDP *)NULL
MDP
{
 size_t qsz;                               /* size of dbq/mmq buffers */
 char *dbq;                                              /* 3db query */
 char *mmq;                                               /* mm query */
 /* KNG 20170609
  *   mmisets: # of intersection (`=') sets
  *   dbisets: # of intersection (`=') sets that are also
  *     non-special-pattern-matcher and non-space-containing:
  */
 int  dbisets,mmisets;                   /* db & mm intersection sets */
 int  intersects;                      /* number of intersections set */
 char *sdexp,*edexp;               /* start and end delim expressions */
 int  incsd,inced;                                   /* include flags */
 int  isdbable;        /* MAW 07-01-93 - can 3db be used on the query */
 int  withincount;                       /* JMT 2004-02-27 N from w/N */
};

#define MDPDLM struct mdpdlm_struct
#define MDPDLMPN (MDPDLM *)NULL
MDPDLM {                                          /* named delimiters */
   char *name;
   char *expr;
   int   incsd;
   int   inced;
};
/**********************************************************************/
extern MDP    *freemdp ARGS((MDP *mdp));
extern MDP    *mdpar   (APICP *cp, const char *s);
extern MDPDLM *mdpstd  ARGS((MDPDLM *nd));
extern MDPDLM *mdpusr  ARGS((MDPDLM *nd));
/**********************************************************************/
#endif                                                     /* MDPAR_H */
