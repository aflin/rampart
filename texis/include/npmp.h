#ifndef NPMP_H
#define NPMP_H 1

#define TTF struct text_to_float
TTF
{
 char *s;
 double x;
 int  type;			    /* quantity, multiplier , magnitude */
 double y;		     /* this is for those defined as magnitudes */
 char start;				      /* is it a starting token */
 int len;				  /* length of the matched item */
};


char **mknptlst    ARGS((void));
void   rmnptlst    ARGS((void));
int    CDECL ttfcmp      ARGS((CONST void *x,CONST void *y));
TTF   *ntlst       ARGS((byte *s));
double tenpow      ARGS((double));
int    ctoi        ARGS((int c));
int    diglexy(char **sp, char *e, double *xp);
int    npmlex(byte *s, byte *e, TTF *tl, int n);
double nxtmul      ARGS((TTF *nl,int n));
int    npmy        ARGS((TTF *nl,int n,double *px,double *py,char *op));
char  *ttod(char *s, char *e, double *px, double *py, char *op);
void   npmtypedump ARGS((FILE *fh));

#endif                                                      /* NPMP_H */
