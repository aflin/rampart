#ifndef URLF_H
#define URLF_H
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS                                    /* MAW 01-10-91 */
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
#define URLF struct urlfile_struct
#define URLFPN (URLF *)NULL
URLF
{
   void *se;
   void *fh;
};
/**********************************************************************/
#ifndef URLFEXPLICIT
#ifdef FILE
#  undef FILE
#endif
#define FILE URLF
#define fclose(a)       urlfclose(a)
#define fopen(a,b)      urlfopen(a,b)
#define fread(a,b,c,d)  urlfread(a,b,c,d)
#define fwrite(a,b,c,d) urlfwrite(a,b,c,d)
#define fseek(a,b,c)    urlfseek(a,b,c)
#define ftell(a)        urlftell(a)
#define fgetc(a)        urlfgetc(a)
#define fputc(a,b)      urlfputc(a,b)
#define fgets(a,b,c)    urlfgets(a,b,c)
#define fputs(a,b)      urlfputs(a,b)
#endif
/**********************************************************************/
extern int    urlfclose ARGS((URLF *uf));
extern URLF  *urlfopen  ARGS((char *fn,char *mode));
extern size_t urlfread  ARGS((void *buf,size_t sz,size_t cnt,URLF *uf));
extern size_t urlfwrite ARGS((void *buf,size_t sz,size_t cnt,URLF *uf));
extern int    urlfseek  ARGS((URLF *uf,long offset,int origin));
extern long   urlftell  ARGS((URLF *uf));
/* following only supported minimally for compatibility. avoid if possible */
extern int    urlfgetc  ARGS((URLF *uf));
extern int    urlfputc  ARGS((int c,URLF *uf));
/* following not implemented for remote files. */
extern char  *urlfgets  ARGS((char *buf,int bufsz,URLF *uf));
extern int    urlfputs  ARGS((char *buf,URLF *uf));
/**********************************************************************/
#endif                                                   /* URLF_H */
