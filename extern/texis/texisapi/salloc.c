#include "stdio.h"
#include "stdlib.h"
#include "os.h"
#include "salloc.h"
#ifndef max
#  define max(a,b) ((a)>(b)?(a):(b))
#endif

/************************************************************************/

SALLOC *
closesalloc(sp)
SALLOC *sp;
{
 SALLOC *tp;
 for(;sp!=SALLOCPN;sp=tp)
    {
     if(sp->buf!=(char *)NULL) free(sp->buf);
     tp=sp->nxt;
     free(sp);
    }
 return(SALLOCPN);
}

/************************************************************************/

SALLOC *
opensalloc(sz)
int sz;
{
 SALLOC *sp=(SALLOC *)calloc(1,sizeof(SALLOC));
 if(sp!=SALLOCPN)
    {
     sp->nxt=SALLOCPN;
     sp->buf=(char *)malloc(sz);
     if(sp->buf==(char *)NULL) return(closesalloc(sp));
     sp->end=sp->buf+sz;
     sp->p=sp->buf;
     sp->sz=sz;
    }
 return(sp);
}

/************************************************************************/

void *
salloc(sp,n)
SALLOC *sp;
int n;
{
 void *p=(void *)NULL;

 for(;(sp->end - sp->p)<n && sp->nxt!=SALLOCPN;sp=sp->nxt) ;
 if((sp->end - sp->p)<n)                   /* if theres not enough room */
    {
     if((sp->nxt=opensalloc(max(sp->sz,n)))!=SALLOCPN)
           sp=sp->nxt;
    }
 if((sp->end - sp->p)>=n)                     /* if theres  enough room */
    {
     p=(void *)sp->p;
     sp->p+=n;
    }
 return(p);
}

/************************************************************************/

void
freesalloc(sp,vp)
SALLOC *sp;
void *vp;
{
   sp; vp;
   /* to be written if actually needed */
}

/************************************************************************/

