#ifndef SANDR_H
#define SANDR_H
/**********************************************************************/
extern int   sandrnbuf ARGS((int fixed,int n,char **lst,byte *in,size_t insz,byte **out,size_t *outsz));
extern int   sandrn    ARGS((int fixed,int n,char **lst,FILE *in,FILE *out));
extern int   sandrnip  ARGS((int n,char **lst,FILE *inout));
extern char *sandrdlm  ARGS((char *));
/**********************************************************************/
#endif                                                     /* SANDR_H */
