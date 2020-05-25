#ifndef PRO_H
#define PRO_H
/* MAW 02-04-93 - temporary itemwise interface to cp files */
/**********************************************************************/
#ifndef CP_H
#include "cp.h"
#endif
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
#define CPDAT struct cpdat_struct
#define CPDATPN (CPDAT *)NULL
CPDAT {
   union {
      uchar uc;
      char   c;
      int    i;
      uint  ui;
      long   l;
      ulong ul;
      float  f;
      void  *p;
   } d;
   REQUESTPARM rqp[2];
	int freeit;
   CPDAT *next;
};

#define PRO struct pro_struct
#define PROPN (PRO *)NULL
PRO {
   CP    *cpp;
   CPDAT *d;
};
/**********************************************************************/
extern PRO  *openpro  ARGS((char *filename));
extern PRO  *closepro ARGS((PRO *pro));
extern PRO  *writepro ARGS((PRO *pro));
extern void *setpro   ARGS((PRO *pro,char *varname,char *datatype,void *data));
extern void *gettpro  ARGS((PRO *pro,char *varname,char **datatype));

#define getpro(a,b) gettpro((a),(b),CHARPPN)
/**********************************************************************/
#endif                                                       /* PRO_H */
