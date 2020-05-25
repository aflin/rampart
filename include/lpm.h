#ifndef LPM_H
#define LPM_H

#ifndef FSEEKCHAR
#define FSEEKCHAR '@'
#endif

#ifndef HUGEFILESZ
#define HUGEFILESZ 0x7fffffff
#endif

#define LPMDELIM "\\n"

#define LPM struct rdpms
#define LPMPN (LPM *)NULL
LPM
{
 char *fname;
 FILE  *fh;
 long base;
 long endoff;
 int  nread;
 byte *buf;
 byte *end;
 int  bufsz;
 PPMS  *ex;
 FFS   *edx;
 byte *hit;
};

/**********************************************************************/

#ifdef LINT_ARGS
LPM  *closelpm(LPM *lpm);
long  getlpm(LPM *lpm,int *sz);
LPM  *openlpm(char **lst,int bufsz);
int   reopenlpm(LPM *lpm,char *fname);
int   seeklpm(LPM *lpm,long off);
int   setlpm(LPM *lpm,char *fname);
#else
LPM  *closelpm();
long  getlpm();
LPM  *openlpm();
int   reopenlpm();
int   seeklpm();
int   setlpm();
#endif

#endif
