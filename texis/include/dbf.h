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
#define aputdbf(o,at,buf,sz) ((o)->aput?(*(o)->put)((o)->obj,at,buf,sz):(*(o)->put)((o)->obj,at,buf,sz),TXfree(buf))
#define getdbf(o,at,psz)     ((*(o)->get)  ((o)->obj,at,psz))
#define agetdbf(o,at,psz)    ((*(o)->aget) ((o)->obj,at,psz))
#define readdbf(o,at,buf,sz) ((*(o)->read) ((o)->obj,at,(size_t *)NULL,buf,sz))
#define telldbf(o)           ((*(o)->tell) ((o)->obj))
#define getdbffn(o)          ((*(o)->getfn)((o)->obj))
#define getdbffh(o)          ((*(o)->getfh)((o)->obj))
#define setdbfoveralloc(o,v) ((*(o)->setoveralloc)((o)->obj,v))
#define validdbf(o,at)       ((*(o)->valid)((o)->obj,at))
#define TXdbfGetObj(dbf)        ((dbf)->obj)

/******************************************************************/

#define OP_IOCTL_MASK   0x00007FFF      /* bits reserved for individual op */
#define TYPE_IOCTL_MASK 0xFFFF8000      /* type bits */

#define BTREE_IOCTL     0x00008000      /* B-tree ioctl type  KNG 971016 */
#define DBF_RAM		0x00010000
#define DBF_FILE	0x00020000
#define DBF_KAI		0x00040000
#define DBF_DBASE	0x00080000
#define DBF_MEMO	0x00100000
#ifdef HAVE_JDBF
#define DBF_JMT		0x00200000
#endif
#define DBF_NOOP        0x00400000
#define DBF_RINGBUFFER  0x00800000

#define DBF_MAKE_FILE	0x00000001
#define DBF_AUTO_SWITCH	0x00000002
#define DBF_SIZE	0x00000005

#define RDBF_SETOVER	DBF_RAM | 0x00000001
#define RDBF_TOOBIG	DBF_RAM | 0x00000002
#define RDBF_BLCK_LIMIT	DBF_RAM | 0x00000003
#define RDBF_SIZE_LIMIT	DBF_RAM | 0x00000004
#define RDBF_SIZE	DBF_RAM | DBF_SIZE
/* Separate ioctl RDBF_SET_NAME to set RAM DBF "file" name, because the
 * normal way to set a DBF name -- at opendbf() -- we can only pass NULL,
 * since that is the way to indicate RAM DBF:
 */
#define RDBF_SET_NAME	(DBF_RAM | 0x00000006)

#define RINGBUFFER_DBF_SET_NAME	(DBF_RINGBUFFER | 0x00000001)
#define RINGBUFFER_DBF_HAS_DATA	(DBF_RINGBUFFER | 0x00000002)
#define RINGBUFFER_DBF_HAS_SPACE	(DBF_RINGBUFFER | 0x00000003)

#define	isramdbtbl(a)	((a) && (a)->tbl && (a)->tbl->df && ((a)->tbl->df->dbftype & DBF_RAM) == DBF_RAM)
#define	isramtbl(a)	((a) && (a)->df && ((a)->df->dbftype & DBF_RAM) == DBF_RAM)

/* ------------------------------ noopdbf.c: ------------------------------ */

typedef struct TXNOOPDBF_tag    TXNOOPDBF;
#define TXNOOPDBFPN     ((TXNOOPDBF *)NULL)

/* "Filename" to pass to opendbf() to indicate TXNOOPDBF: */
#define TXNOOPDBF_PATH  ((char *)1)

#define TXNOOPDBF_IOCTL_SEEKSTART       0x1

TXNOOPDBF *TXnoOpDbfClose(TXNOOPDBF *df);
TXNOOPDBF *TXnoOpDbfOpen(void);
int     TXnoOpDbfFree(TXNOOPDBF *df, EPI_OFF_T at);
EPI_OFF_T TXnoOpDbfAlloc(TXNOOPDBF *df, void *buf, size_t n);
EPI_OFF_T TXnoOpDbfPut(TXNOOPDBF *df, EPI_OFF_T at, void *buf, size_t sz);
int     TXnoOpDbfBlockIsValid(TXNOOPDBF *df, EPI_OFF_T at);
void    *TXnoOpDbfGet(TXNOOPDBF *df, EPI_OFF_T at, size_t *psz);
void    *TXnoOpDbfAllocGet(TXNOOPDBF *df, EPI_OFF_T at, size_t *psz);
size_t  TXnoOpDbfRead(TXNOOPDBF *df, EPI_OFF_T at, size_t *off, void *buf,
                      size_t sz);
EPI_OFF_T TXnoOpDbfTell(TXNOOPDBF *df);
char    *TXnoOpDbfGetFilename(TXNOOPDBF *df);
int     TXnoOpDbfGetFileDescriptor(TXNOOPDBF *df);
void    TXnoOpDbfSetOverAlloc(TXNOOPDBF *noOpDbf, int ov);
int     TXnoOpDbfIoctl(TXNOOPDBF *noOpDbf, int ioctl, void *data);
int     TXnoOpDbfSetPmbuf(TXNOOPDBF *noOpDbf, TXPMBUF *pmbuf);

int     TXinitNoOpDbf(DBF *df);

#include "bufferdbf.h"

typedef enum TX_DBF_TYPE {
  TX_DBF_RAMDBF = DBF_RAM,
  TX_DBF_NOOPDBF = DBF_NOOP,
  TX_DBF_RINGDBF = DBF_RINGBUFFER
} TX_DBF_TYPE;

DBF *opendbf ARGS((TXPMBUF *pmbuf, char *fn, int oflags));
DBF *opendbfinternal(TXPMBUF *pmbuf, TX_DBF_TYPE dbftype);
int     TXdbfSetPmbuf(DBF *dbf, TXPMBUF *pmbuf);
DBF *closedbf ARGS((DBF *df));
#ifndef NO_DBF_IOCTL
int  ioctldbf ARGS((DBF *, int, void *));
#endif
/************************************************************************/
#endif                                                       /* DBF_H */
