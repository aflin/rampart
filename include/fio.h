#ifndef FIO_H
#define FIO_H
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif                                                        /* ARGS */
/**********************************************************************/
#define FIO struct fio_struct
#define FIOPN (FIO *)NULL
FIO {
   int   fh;
   int   bsz;
   char *buf;
   char *p;
   char *end;
#ifdef NEVER
	int   nnl;
   char *nl;
#endif
};
#define FIO_EOF (-1)
#define FIO_ERR (-2)
#define FIO_NUL (-3)
/**********************************************************************/
#define fifileno(f) ((f)->fh)
#define finbuf(f) ((f)->end-(f)->p)
extern FIO *fidopen  ARGS((int fd));
extern FIO *fidclose ARGS((FIO *fi));
extern FIO *ficlose  ARGS((FIO *fi));
extern int  figet    ARGS((FIO *fi,char *buf,int bsz));
extern int  figetc   ARGS((FIO *fi));
extern int  fipeek   ARGS((FIO *fi));
extern int  figets   ARGS((FIO *fi,char *buf,int bsz));
extern int  figetsa  ARGS((FIO *fi,char **buf));
extern int  fiput    ARGS((FIO *fi,char *buf,int bsz));
extern int  fiputs   ARGS((FIO *fi,char *buf));
#ifdef NEVER
extern int  fisetnl  ARGS((FIO *fi,char *nl,int nnl));
#endif
/**********************************************************************/
#endif                                                       /* FIO_H */
