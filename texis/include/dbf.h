#ifndef DBF_H
#define DBF_H
/************************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/************************************************************************/
#define DBF struct datablock_file
#define DBFPN (DBF *)NULL
DBF
{
 void   *obj;
 void   *(* close) ARGS((void *df));
 int     (* dbfree)ARGS((void *df, EPI_OFF_T at));
 EPI_OFF_T   (* alloc) ARGS((void *df, void *buf, size_t n));
 EPI_OFF_T   (* put)   ARGS((void *df, EPI_OFF_T at,  void *buf, size_t sz));
 void   *(* get)   ARGS((void *df, EPI_OFF_T at,  size_t *psz));
 void   *(* aget)  ARGS((void *df, EPI_OFF_T at,  size_t *psz));
 size_t  (*read) ARGS((void *df, EPI_OFF_T at, size_t *off, void *buf, size_t sz));
 EPI_OFF_T   (* tell)  ARGS((void *df));

 char   *(* getfn) ARGS((void *df));
 int     (* getfh) ARGS((void *df));
 void    (* setoveralloc) ARGS((void *df,int a));
 int	 (* valid) ARGS((void *df, EPI_OFF_T at));
#ifndef NO_DBF_IOCTL
 int	 (* ioctl) ARGS((void *df, int ioctl, void *data));
 long	 dbftype;
#endif
 TXPMBUF        *pmbuf;         /* (opt.) cloned buffer for messages */
};
/************************************************************************/
#define freedbf(o,at)        ((*(o)->dbfree)((o)->obj,at))
#define dbfalloc(o,buf,n)    ((*(o)->alloc)((o)->obj,buf,n))
#define putdbf(o,at,buf,sz)  ((*(o)->put)  ((o)->obj,at,buf,sz))
#define getdbf(o,at,psz)     ((*(o)->get)  ((o)->obj,at,psz))
#define agetdbf(o,at,psz)    ((*(o)->aget) ((o)->obj,at,psz))
#define readdbf(o,at,buf,sz) ((*(o)->read) ((o)->obj,at,(size_t *)NULL,buf,sz))
#define telldbf(o)           ((*(o)->tell) ((o)->obj))
#define getdbffn(o)          ((*(o)->getfn)((o)->obj))
#define getdbffh(o)          ((*(o)->getfh)((o)->obj))
#define setdbfoveralloc(o,v) ((*(o)->setoveralloc)((o)->obj,v))
#define validdbf(o,at)       ((*(o)->valid)((o)->obj,at))
#define TXdbfGetObj(dbf)        ((dbf)->obj)

DBF *opendbf ARGS((TXPMBUF *pmbuf, char *fn, int oflags));
int     TXdbfSetPmbuf(DBF *dbf, TXPMBUF *pmbuf);
DBF *closedbf ARGS((DBF *df));
#ifndef NO_DBF_IOCTL
int  ioctldbf ARGS((DBF *, int, void *));
#endif
/************************************************************************/
#endif                                                       /* DBF_H */
