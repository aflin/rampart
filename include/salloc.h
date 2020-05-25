#ifndef SALLOC_H
#define SALLOC_H
/**********************************************************************/
#define SALLOC struct string_alloc
#define SALLOCPN  (SALLOC *)NULL
SALLOC
{
 char *buf;
 char *end;
 char *p;
 int sz;
 SALLOC *nxt;
};
/**********************************************************************/
SALLOC *opensalloc  ARGS((int sz));
SALLOC *closesalloc ARGS((SALLOC *sp));
void   *salloc      ARGS((SALLOC *sp,int n));
void    freesalloc  ARGS((SALLOC *sp,void *vp));
/**********************************************************************/
#endif                                                    /* SALLOC_H */
