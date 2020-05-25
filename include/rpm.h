#ifndef RPM_H
#define RPM_H
#ifndef FFS
#include "pm.h"
#endif
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif



#define RPM struct rdpms
#define RPMPN (RPM *)NULL
RPM
{
 char *fname;
 FILE *fh;
 long base;
 int  nread;
 byte *buf;
 byte *end;
 int  bufsz;
 FFS  *ex;
 FFS  *edx;
 byte *hit;
 byte *eoh;
 byte  eohc;
};

/**********************************************************************/
/* MAW 01-22-93 - added, eofrpm() only correct immediately after getrpm() */
#define eofrpm(r) ((r)->nread==0)

RPM  *closerpm  ARGS((RPM *rpm));
long  getrpm    ARGS((RPM *rpm,int *sz));
RPM  *openrpm   ARGS((char *exp,int bufsz));
int   reopenrpm ARGS((RPM *rpm,char *fname));
int   seekrpm   ARGS((RPM *rpm,off_t off));
int   setrpm    ARGS((RPM *rpm,char *fname));

#endif
