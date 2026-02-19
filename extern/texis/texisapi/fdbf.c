#include "txcoreconfig.h"
#if defined(__MSDOS__) && !defined(MSDOS)
#  define MSDOS __MSDOS__
#endif
#if defined(__MINGW32__) && !defined(MSDOS)
#  define MSDOS 1
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <errno.h>
#include "os.h"
#include "mmsg.h"
#include "texint.h"                             /* for TXproff_t() */
#include "fdbf.h"

/******************************************************************/
/* If defined, this will display all fdbf calls                   */

#ifdef DEVEL
#undef FDBFDISPLAY
#else
#undef FDBFDISPLAY
#endif

/******************************************************************/

#if FDBFHIGHIO

# ifdef MSDOS
#    define CIOMODE  "w+b"
#    define IOMODE   "r+b"
#    define IMODE    "rb"
#    define OMODE    "wb"
# else
#    define CIOMODE  "w+"
#    define IOMODE   "r+"
#    define IMODE    "r"
#    define OMODE    "w"
# endif

#define close(fh)          fclose(fh)
#define read(fh,buf,cnt)   fread(buf,sizeof(char),cnt,fh)
#define write(fh,buf,cnt)  fwrite(buf,sizeof(char),cnt,fh)
#define lseek(fh,off,mode) (fseek(fh,off,mode) ? -1L : ftell(fh))

#else
# ifdef MSDOS
# include <io.h>
#    define CIOFLAGS  O_RDWR|O_BINARY|O_CREAT
#    define CIOMODE   S_IREAD|S_IWRITE
#    define IOFLAGS   O_RDWR|O_BINARY
#    define IOMODE    S_IREAD|S_IWRITE
#    define IFLAGS    O_RDONLY|O_BINARY
#    define IMODE     S_IREAD
#    define OFLAGS    O_WRONLY|O_CREAT|O_TRUNC|O_BINARY
#    define OMODE     S_IREAD
# else
#    ifdef macintosh
#      define CIOFLAGS O_RDWR|O_CREAT
#      define CIOMODE  0
#      define IOFLAGS  O_RDWR
#      define IOMODE   0
#      define IFLAGS   O_RDONLY
#      define IMODE    0
#      define OFLAGS   O_WRONLY|O_CREAT|O_TRUNC
#      define OMODE    0
#    else
#      define CIOFLAGS O_RDWR|O_CREAT
#      define CIOMODE  S_IREAD|S_IWRITE
#      define IOFLAGS  O_RDWR
#      define IOMODE   S_IREAD|S_IWRITE
#      define IFLAGS   O_RDONLY
#      define IMODE    S_IREAD
#      define OFLAGS   O_WRONLY|O_CREAT|O_TRUNC
#      define OMODE    S_IREAD|S_IWRITE
#    endif
# endif
#endif

#define ltell(fh) lseek((fh),0L,SEEK_CUR)
#define CHECK() 0xA0 /* (((byte)(df->at+df->size+df->used)&0x0f)<<4) */

/************************************************************************/

static int      TxFdbfEnabled = -1;             /* -1 == not set yet */

int
TXfdbfIsEnabled(void)
{
  if (TxFdbfEnabled == -1)                      /* not set yet */
    {
      TxFdbfEnabled = 0;                        /* default value */
      if (TxConf != CONFFILEPN)
        {
          TxFdbfEnabled = getconfint(TxConf, "Texis", "Enable FDBF",
                                     TxFdbfEnabled);
          TxFdbfEnabled = (TxFdbfEnabled ? 1 : 0);
        }
    }
  return(TxFdbfEnabled);
}

static int tx_okfdbf ARGS((CONST char *file));
static int
tx_okfdbf(file)
CONST char      *file;
/* Checks if FDBF is enabled by texis.cnf. */
/* FDBF is old, and should only be needed for files created with Texis
 * versions before 1995-12-20.  FDBF files created with versions after
 * that are likely due to errors, eg. a partially created or corrupt
 * file that KDBF fails to open, improperly punting to FDBF.  Since
 * it's much more likely nowadays that calling FDBF is due to a corrupt
 * file than accessing a true FDBF file, disable FDBF and yap by default
 * to help prevent further corruption.  KNG 20060105
 */
{
  if (!TXfdbfIsEnabled())
    putmsg(MERR + UGE, CHARPN, "Probable corrupt KDBF file %s: FDBF disabled, enable in " TX_TEXIS_INI_NAME " only if known to be FDBF", file);
  return(TxFdbfEnabled);
}

/************************************************************************/


static int writecache ARGS((FDBF *df));
/* dumps the write cache out */
static int
writecache(df)
FDBF *df;
{
 int rc=1;
 off_t link;

 /* go to eof
    make sure eof == where i think the cache block goes
    get the link
    write the data
    get the new cache base offset
    rewrite the link
    reset the cache pointer
*/
 if(!df->csz) return(1);

 if(lseek(df->fh,-(off_t)sizeof(off_t),SEEK_END)!=df->coff  ||
    read(df->fh,(void *)&link,sizeof(off_t))!=sizeof(off_t) ||
    lseek(df->fh,-(off_t)sizeof(off_t),SEEK_END)!=df->coff  ||
    (size_t)write(df->fh, (void *)df->cache, df->csz) != df->csz ||
    (df->coff=lseek(df->fh,0L,SEEK_CUR))<=0L                ||
    write(df->fh,(void *)&link,sizeof(off_t))!=sizeof(off_t)
   ) rc=0;

 df->csz=0;
 return(rc);
}

/************************************************************************/

static int cachefdbf ARGS((FDBF *df,byte *buf,size_t n));

static int
cachefdbf(df,buf,n)
FDBF *df;
byte *buf;
size_t n;
{
  static const char     fn[] = "cachefdbf";
  static const char     badUsedSize[] = "Bad used and/or size value";
 FDBFALL  u;                          /* a union of all the header types */
 size_t sz;
 byte *cp;

 if((df->csz+n)>=FDBFCACHESZ && !writecache(df) )      /* will it fit ? */
    return(0);

 if(n >= FDBFCACHESZ)  /* JMT - won't fit at all. */
 	return 0;

 df->at=df->coff+df->csz;
 cp=df->cache+df->csz;

 df->type&=FDBFTYBITS;                         /* mask off the old stuff */
 df->type|=CHECK(); /* checksum it */
 *cp++ =df->type;

 switch((int)df->type&FDBFTYBITS)            /* determine structure type */
    {
     case FDBFNIBBLE :           /* assertions prevent oversized writes! */
          if (!(df->used<=FDBFNMAX && df->size<=FDBFNMAX))
            {
              putmsg(MERR, fn, badUsedSize);
              return(0);
            }
          u.n.used_size = (byte)(df->used<<4);
          u.n.used_size|= (byte)(df->size);
#ifdef MSDOS
	  sz = 1;
#else
          sz = sizeof(FDBFN);
#endif
          break;
     case FDBFBYTE   :
          if (!(df->used<=FDBFBMAX && df->size<=FDBFBMAX))
            {
              putmsg(MERR, fn, badUsedSize);
              return(0);
            }
          u.b.used_size[0]=(byte)df->used;
          u.b.used_size[1]=(byte)df->size;
          sz=sizeof(FDBFB);
          break;
     case FDBFWORD   :
          if (!(df->used<=FDBFWMAX && df->size<=FDBFWMAX))
            {
              putmsg(MERR, fn, badUsedSize);
              return(0);
            }
          u.w.used_size[0]=(word)df->used;
          u.w.used_size[1]=(word)df->size;
          sz=sizeof(FDBFW);
          break;
     case FDBFSTYPE  :
          if (!(df->used<=FDBFSMAX && df->size<=FDBFSMAX))
            {
              putmsg(MERR, fn, badUsedSize);
              return(0);
            }
          u.s.used_size[0]=(size_t)df->used;
          u.s.used_size[1]=(size_t)df->size;
          sz=sizeof(FDBFS);
          break;
    default:                    /* can't happen... */
          sz = 0;
          putmsg(MERR, fn, "Unknown type");
          return(0);
    }

 memcpy((void *)cp,(void *)&u,sz);
 cp+=sz;
 memcpy((void *)cp,(void *)buf,n);
 if (n < sizeof(off_t))
    cp+=sizeof(off_t);
 else
    cp+=n;
 df->csz=cp-df->cache;
 return(1);
}

/************************************************************************/
/* 1 if ok,  0 if not ok */

static int readhead   ARGS((FDBF *df, off_t at));

static int
readhead(df,at)
FDBF  *df;
off_t at;
{
 static CONST char Fn[]="readhead";
 FDBFALL u;                           /* a union of all the header types */

 df->at=at;         /* record the location so I can get back here later */

 if(df->csz && at >= df->coff)                /* ensure reads will work */
    writecache(df);

 if(lseek(df->fh,at,SEEK_SET)<0L ||        /* move to loc and read type */
    read(df->fh,(void *)&df->type,(size_t)sizeof(byte))!=sizeof(byte)
   ) return(0);


 switch((int)df->type&FDBFTYBITS)            /* determine structure type */
    {
     case FDBFNIBBLE :
#ifdef MSDOS
          if(read(df->fh,(void *)&u,1)!=1)
#else
          if(read(df->fh,(void *)&u,sizeof(FDBFN))!=sizeof(FDBFN))
#endif
              return(0);
          df->used=(size_t)((u.n.used_size)>>4);
          df->size=(size_t)((u.n.used_size)&0x0f);
          df->end=at+df->size+sizeof(FDBFN)+1;
#ifdef MSDOS
          df->end=at+df->size+1+1;
#endif
          break;
     case FDBFBYTE   :
          if(read(df->fh,(void *)&u,sizeof(FDBFB))!=sizeof(FDBFB))
              return(0);
          df->used=(size_t)u.b.used_size[0];
          df->size=(size_t)u.b.used_size[1];
          df->end=at+df->size+sizeof(FDBFB)+1;
          break;
     case FDBFWORD   :
          if(read(df->fh,(void *)&u,sizeof(FDBFW))!=sizeof(FDBFW))
              return(0);
          df->used=(size_t)u.w.used_size[0];
          df->size=(size_t)u.w.used_size[1];
          df->end=at+df->size+sizeof(FDBFW)+1;
          break;
     case FDBFSTYPE  :
          if(read(df->fh,(void *)&u,sizeof(FDBFS))!=sizeof(FDBFS))
              return(0);
          df->used=(size_t)u.s.used_size[0];
          df->size=(size_t)u.s.used_size[1];
          df->end=at+df->size+sizeof(FDBFS)+1;
          break;
    }

 if((df->type&0xf0)!=CHECK())
    {
      putmsg(MERR,Fn,"Corrupt operation in FDBF file %s",df->fn);
      return(0);
    }

 if(ltell(df->fh)<0L)          /* prepare for any operation */
    return(0);
 return(1);
}

/************************************************************************/

static int writehead  ARGS((FDBF *df));

static int    /* 1 if ok,  0 if not ok */
writehead(df)
FDBF  *df;
{
  static const char     fn[] = "writehead";
  static const char     badUsedSize[] = "Bad used or size value";
 FDBFALL u;                           /* a union of all the header types */
 byte obuf[sizeof(byte)+sizeof(FDBFALL)];                /* PBR 07-01-93 */
 size_t sz;

 df->type&=FDBFTYBITS;                         /* mask off the old stuff */
 df->type|=CHECK(); /* checksum it */
 obuf[0]=df->type;

 switch((int)df->type&FDBFTYBITS)            /* determine structure type */
    {
     case FDBFNIBBLE :           /* assertions prevent oversized writes! */
          if (!(df->used<=FDBFNMAX && df->size<=FDBFNMAX))
            {
              putmsg(MERR, fn, badUsedSize);
              return(0);
            }
          u.n.used_size = (byte)(df->used<<4);
          u.n.used_size|= (byte)(df->size);
#ifdef MSDOS
	  sz=1;
#else
          sz=sizeof(FDBFN);
#endif
          break;
     case FDBFBYTE   :
          if (!(df->used<=FDBFBMAX && df->size<=FDBFBMAX))
            {
              putmsg(MERR, fn, badUsedSize);
              return(0);
            }
          u.b.used_size[0]=(byte)df->used;
          u.b.used_size[1]=(byte)df->size;
          sz=sizeof(FDBFB);
          break;
     case FDBFWORD   :
          if (!(df->used<=FDBFWMAX && df->size<=FDBFWMAX))
            {
              putmsg(MERR, fn, badUsedSize);
              return(0);
            }
          u.w.used_size[0]=(word)df->used;
          u.w.used_size[1]=(word)df->size;
          sz=sizeof(FDBFW);
          break;
     case FDBFSTYPE  :
          if (!(df->used<=FDBFSMAX && df->size<=FDBFSMAX))
            {
              putmsg(MERR, fn, badUsedSize);
              return(0);
            }
          u.s.used_size[0]=(size_t)df->used;
          u.s.used_size[1]=(size_t)df->size;
          sz=sizeof(FDBFS);
          break;
     default:                           /* can't happen */
       sz = 0;
       putmsg(MERR, fn, "Unknown type");
       return(0);
    }

/* I put everything together so I could do it in one write */

 memcpy((void *)(obuf+1),(void *)&u,sz++);        /* sz+1 to include type */

 if(lseek(df->fh,df->at,SEEK_SET)<0L ||                  /* move to loc */
    (size_t)write(df->fh, (void *)obuf, sz) != sz
   ) return(0);

 return(1);
}

/************************************************************************/

/* reads in the block. Assumes position is correct for the read */
static int readblk    ARGS((FDBF *df));

static int
readblk(df)
FDBF *df;
{
 size_t nread;

#ifdef ASSERT_DEL_BLOCK
 if (df->used == 0)     /* attempt to read in a free block is illegal */
   {
     putmsg(MERR, "readblk",
            "Illegal attempt to read free block from FDBF file `%s'",
            df->fn);
     return(0);
   }
#else
 if(df->used == 0)
 	return 0;
#endif

 if(df->blksz<df->used)  /* memory needed too big! realloc the buffer */
    {
     if(df->blk!=(void *)NULL)  /* release old memory */
         free(df->blk);
     if((df->blk=malloc(df->used))==(void *)NULL)    /* try to get more */
          return(0);
     df->blksz=df->used;                /* adjust block allocation size */
    }
 if((nread=read(df->fh,df->blk,df->used))!=df->used)
    return(0);
 return(1);
}

/************************************************************************/

#ifdef NEVER
/* writes the block. Assumes position is correct for the operation */
static int writeblk   ARGS((FDBF *df));

static int
writeblk(df)
FDBF *df;
{
 return(write(df->fh,df->blk,df->used)==df->used);
}
#endif  /* NEVER */

/************************************************************************/

/* just puts the header and the data out at the specified df->at, if buf
is NULL then it seeks forward as if a write was performed */
static int writealloc ARGS((FDBF *df,void *buf,size_t n));

static int
writealloc(df,buf,n)
FDBF *df;
void *buf;           /* PBR 05-31-93 added buf to avoid replicate writes*/
size_t n;
{

 df->used=n;  /* assigns the correct size */

 if(!writehead(df))
    return(0);

 if(buf==(void *)NULL)
     {
      if(lseek(df->fh,(off_t)n,SEEK_CUR)>=0L)
           return(1);
      return(0);
     }

 if ((size_t)write(df->fh, buf, n) != n)
      return(0);

 return(1);
}

/************************************************************************/

EPI_OFF_T
fdbfalloc(df,buf,bufsz)
FDBF *df;
void *buf;           /* PBR 05-31-93 added buf to avoid replicate writes*/
size_t bufsz;
{
  static const char     fn[] = "fdbfalloc";
 off_t prev, here, first_free, ff_addr;
 byte  type;
 size_t n;

 if (!tx_okfdbf(df->fn)) return((EPI_OFF_T)(-1));

#ifdef FDBFDISPLAY
 putmsg(999, NULL, "Fdbfalloc %s, %d bytes", df->fn, bufsz);
#endif
 if(df->overalloc)            /* overallocate the block so it can grow */
    {
     n=bufsz>>df->overalloc;
     n+=bufsz;
    }
 else n=bufsz;

 if(n<FDBFNMAX)      type=FDBFNIBBLE;  /* get the size type of this alloc */
 else if(n<FDBFBMAX) type=FDBFBYTE  ;
 else if(n<FDBFWMAX) type=FDBFWORD  ;
 else if(n<FDBFSMAX) type=FDBFSTYPE ;
 else               {putmsg(MERR + MAE, fn, "Size too large"); return(-1);}
     /* too big to allocate */

 if(df->new && df->cache_on)
 {
   df->type=type;
   df->size=n;
   df->used=bufsz;

   /* make sure a block is always big enough to hold a free list pointer */
   if(df->size<sizeof(off_t)) df->size=sizeof(off_t);
   if(cachefdbf(df,buf,n))
       return((EPI_OFF_T)df->at);
 }

 /* last long in file is a pointer to the first free block */

 if((ff_addr=prev=lseek(df->fh,-(off_t)sizeof(off_t),SEEK_END))<0L  ||
     read(df->fh,(void *)&df->next,sizeof(off_t))!=sizeof(off_t)
   ) return((EPI_OFF_T)(-1L));

 first_free=df->next;

 while(df->next!=-1L)
    {
     if(!readhead(df,df->next) ||          /* read in free block header */
         (here=ltell(df->fh))<0L ||
         read(df->fh,(void *)&df->next,sizeof(off_t))!=sizeof(off_t)/* and next */
       ) return((EPI_OFF_T)(-1L));              /* damnit! */

     if(df->used != 0)
     {
     	     putmsg(MERR, NULL, "Free list is corrupt.  Run copydbf on FDBF file %s", df->fn);
             return(-1);
     }
     if((df->type&FDBFTYBITS)==type &&    /* it is the same block type ? */
         df->size>=n                               /* is it big enough? */
       ) {
          if(
             lseek(df->fh,prev,SEEK_SET)!=prev ||   /* remove from list */
             /* relink the free list */
              write(df->fh,(void *)&df->next,sizeof(off_t))!=sizeof(off_t) ||
             !writealloc(df,buf,bufsz) /* rewrite header & then write data */
            ) return((EPI_OFF_T)(-1L));
          return((EPI_OFF_T)df->at);
         }
     prev=here;
    }
/* theres nothing big enough in the free list, so lets make one */
 /* NEW: */

 df->type=type;
 df->size=n;
 df->used=bufsz;

 /* make sure a block is always big enough to hold a free list pointer */
 if(df->size<sizeof(off_t)) df->size=sizeof(off_t);


 if(df->cache_on)
    {
     if(cachefdbf(df,buf,n))
         return((EPI_OFF_T)df->at);
#ifdef NEVER
     else return((EPI_OFF_T)(-1L));
#endif
    }

 df->at=ff_addr;       /* use the first_free address for the new record */

 if( !writealloc(df,buf,bufsz)   ||
/* JMT Try and write the right size block */
 (df->coff = lseek(df->fh, df->size - bufsz, SEEK_CUR)) < 0L ||
      write(df->fh,(void *)&first_free,sizeof(off_t))!=sizeof(off_t)
   ) return((EPI_OFF_T)(-1L));

 return((EPI_OFF_T)df->at);
}

/************************************************************************/

static void fdbf_erange ARGS((CONST char *fn, FDBF *df, EPI_OFF_T epi_at));
static void
fdbf_erange(fn, df, epi_at)
CONST char      *fn;
FDBF            *df;
EPI_OFF_T       epi_at;
{
  putmsg(MERR + FSE, fn,
         "Cannot seek to offset %s in FDBF file %s: off_t range exceeded",
         TXproff_t(epi_at), df->fn);
}

#define CHECK_RANGE(fn, df, epi_at, at, err)                    \
 if (epi_at > (EPI_OFF_T)EPI_OS_OFF_T_MAX || epi_at < (EPI_OFF_T)(-1)) \
   {                                                            \
     fdbf_erange(fn, df, epi_at);                               \
     err;                                                       \
   }                                                            \
 at = (off_t)epi_at

/************************************************************************/

int
freefdbf(df,epi_at)
FDBF *df;
EPI_OFF_T epi_at;
{
  static CONST char     fn[] = "freefdbf";
 off_t prev, at;

 if (!tx_okfdbf(df->fn)) return(0);
 CHECK_RANGE(fn, df, epi_at, at, return(0));

#ifdef FDBFDISPLAY
 putmsg(999, NULL, "Freefdbf %s, at %d", df->fn, at);
#endif
 if(at==-1L) at=df->at;              /* PBR 06-09-93 free current block */

 if(df->csz && at >= df->coff)                /* ensure reads will work */
    writecache(df);

 if((prev=lseek(df->fh,-(off_t)sizeof(off_t),SEEK_END))<0L  ||
     read(df->fh,(void *)&df->next,sizeof(off_t))!=sizeof(off_t) ||
     lseek(df->fh,prev,SEEK_SET)!=prev ||
     write(df->fh,(void *)&at,sizeof(off_t))!=sizeof(off_t) ||
     !readhead(df,at)
   ){
      OH_NO:
     /* something's wrong! try to put the free list back */
     lseek(df->fh,-(off_t)sizeof(off_t),SEEK_END);
     write(df->fh,(void *)&df->next,sizeof(off_t));
     return(0);
    }
 df->used=0;
 df->at=at;
 if(!writehead(df) ||
     write(df->fh,(void *)&df->next,sizeof(off_t))!=sizeof(off_t)
   ) goto OH_NO;

 df->new=0;                                   /* now theres free blocks */
 return(1);                                                    /* whew! */
}


/************************************************************************/

FDBF *
closefdbf(df)
FDBF *df;
{
 if(df!=FDBFPN)
    {
     if(df->csz)               writecache(df);
#if FDBFHIGHIO
     if(df->fh!=(FILE *)NULL)  close(df->fh);
#else
     if(df->fh>=0)             close(df->fh);
#endif
     if(df->tmp)               unlink(df->fn);
     if(df->fn!=(char *)NULL)
     {
#ifdef FDBFDISPLAY
         putmsg(999, NULL, "Closefdbf %s", df->fn);
#endif
     	 free(df->fn);
     }
     if(df->blk!=(void *)NULL) free(df->blk);
     free((void *)df);
    }
 return(FDBFPN);
}

/************************************************************************/

FDBF *
openfdbf(fn)
char *fn;
{
 FDBF *df;
#ifdef MEMDEBUG
 mac_ovchk();
#endif
#ifdef FDBFDISPLAY
 putmsg(999, NULL, "Openfdbf %s", fn);
#endif
 if (!tx_okfdbf(fn)) return(FDBFPN);
 df=(FDBF *)calloc(1,sizeof(FDBF));
 if(df!=FDBFPN)
    {
     df->blksz= 0;
     df->blk  = (void *)NULL;
#if FDBFHIGHIO
     df->fh=(FILE *)NULL;
#else
     df->fh   = -1;
#endif
     df->tmp  = 0;
     df->at   = -1L;
     df->size = df->used=0;
     df->next = -1L;
     df->csz  =   0;
     df->cache_on=0;

     if(fn==(char *)NULL || *fn=='\0')
         {
          df->tmp=1;
          df->fn = TXtempnam(CHARPN, CHARPN, CHARPN);
         }
     else df->fn=strdup(fn);
#if FDBFHIGHIO
     if((df->fh=fopen(df->fn,IOMODE)) ==(FILE *)NULL &&
        (df->fh=fopen(df->fn,IMODE)) ==(FILE *)NULL &&
        (df->fh=fopen(df->fn,CIOMODE)) ==(FILE *)NULL
       )  return(closefdbf(df));
#else
     if((df->fh=open(df->fn,IOFLAGS,IOMODE))<0 &&
        (df->fh=open(df->fn,IFLAGS,IMODE))<0 &&
        (df->fh=open(df->fn,CIOFLAGS,CIOMODE))<0)
         return(closefdbf(df));
#endif
     if(lseek(df->fh,0L,SEEK_END)==0L) /* new file */
        {
         off_t next= -1L;               /* put free list pointer at end */
         if(write(df->fh,(void *)&next,sizeof(off_t))!=sizeof(off_t))
              return(closefdbf(df));
          df->new=1;
        }
     else df->new=0;

     if((df->coff=lseek(df->fh,-(off_t)sizeof(off_t),SEEK_END))<0L)
         return(closefdbf(df));
    }
 return(df);
}

/************************************************************************/

/* reposition the fdbf file to the start of a block and read in the header.
if "at" is -1L it will seek to the next block. returns the block handle or
-1L if error or eof */
EPI_OFF_T seekfdbf ARGS((FDBF *df,EPI_OFF_T epi_at));

EPI_OFF_T
seekfdbf(df,epi_at)
FDBF    *df;
EPI_OFF_T   epi_at;                     /* the handle, or -1 for next block */
{
  static CONST char     fn[] = "seekfdbf";
  off_t at;

  if (!tx_okfdbf(df->fn)) return((EPI_OFF_T)(-1));
  CHECK_RANGE(fn, df, epi_at, at, return((EPI_OFF_T)(-1)));

#ifdef FDBFDISPLAY
 putmsg(999, NULL, "Seekfdbf %s to %d", df->fn, at);
#endif
 if(at== -1L)
    {
     off_t eof;

     /* find eof */
     /* position to next record */
     /* see if past eof */
     /* read next block header */

     if(df->csz)
     	writecache(df);
     if(
        (eof=lseek(df->fh,-(off_t)sizeof(off_t),SEEK_END))<0L ||
        (at=lseek(df->fh,df->end,SEEK_SET))<0L ||
        at>=eof ||
        !readhead(df,at)
       ) return(-1L);


     while(df->used==0)            /* if it's a free block keep looking */
         {
          if(
             (at=lseek(df->fh,(off_t)df->size,SEEK_CUR))<0L  ||
             at>=eof ||
             !readhead(df,at)
            ) return(-1L);
         }
     return(df->at);
    }

 if(!readhead(df,at))
    return(-1L);

 if(df->used==0)
    {
#ifdef ASSERT_DEL_BLOCK
      putmsg(MERR, fn,
             "Illegal attempt to seek to a free block in FDBF file `%s'",
             df->fn);    /* attempt to seek to a free fdbf block */
#endif
     return(-1L);
    }

 return(df->at);
}


/************************************************************************/
/* Is Dbf readable at this location */

int
validfdbf(df,epi_at)
FDBF    *df;
EPI_OFF_T   epi_at;                     /* the handle, or -1 for next block */
{
  static CONST char     fn[] = "validfdbf";
  off_t at;

  if (!tx_okfdbf(df->fn)) return(0);
  CHECK_RANGE(fn, df, epi_at, at, return(0));

#ifdef FDBFDISPLAY
 putmsg(999, NULL, "Validfdbf %s,  %d", df->fn, at);
#endif
 if(at== -1L)
    return 1;

 if(!readhead(df,at))
    return(0);

 if(df->used==0)
    return(0);

 return(1);
}


/************************************************************************/

EPI_OFF_T
tellfdbf(df)
FDBF *df;
{
 return((EPI_OFF_T)df->at);
}

/************************************************************************/

char *
getfdbffn(df)            /* MAW 04-04-94 - add method to remove drill */
FDBF *df;
{
 return(df->fn);
}

/************************************************************************/

int
getfdbffh(df)            /* MAW 04-04-94 - add method to remove drill */
FDBF *df;
{
#if FDBFHIGHIO
 return(fileno(df->fh));
#else
 return(df->fh);
#endif
}

/************************************************************************/

void
setfdbfoveralloc(df,ov)  /* MAW 04-04-94 - add method to remove drill */
FDBF *df;
int ov;
{
 df->overalloc=ov;
}

/************************************************************************/

/* get a disk block from the fdbf: returns a pointer to the memory
*  that contains the block. It is not allocated, so you have to make a copy
*  of it if you want to keep it.
*/

void *
getfdbf(df,epi_at,psz)
FDBF    *df;
EPI_OFF_T   epi_at;
size_t *psz;
{
  static CONST char     fn[] = "getfdbf";
  off_t at;

  if (!tx_okfdbf(df->fn)) return(NULL);
  CHECK_RANGE(fn, df, epi_at, at, return(NULL));

 *psz=0;
#ifdef DEBUG
 putmsg(999, NULL, "In Getfdbf");
#endif
 if(seekfdbf(df,at)<(EPI_OFF_T)0L  ||
    !readblk(df)
   ) return((void *)NULL);
 *psz=df->used;
#ifdef FDBFDISPLAY
 putmsg(999, NULL, "Getfdbf %s, %d bytes at %d", df->fn, *psz, at);
#endif
 return(df->blk);
}

/************************************************************************/

/* allocating version of the above function ( this means that
you own the data block, and are required to free it */

void *
agetfdbf(df,epi_at,psz)
FDBF    *df;
EPI_OFF_T   epi_at;
size_t *psz;
{
  static CONST char     fn[] = "agetfdbf";
  off_t at;
 void *blk;

 if (!tx_okfdbf(df->fn)) return(NULL);
 CHECK_RANGE(fn, df, epi_at, at, return(NULL));

#ifdef FDBFDISPLAY
 putmsg(999, NULL, "Agetfdbf %s", df->fn);
#endif
 df->blksz=0;
 if(df->blk!=(void *)NULL)      /* this is to fake out the read routine */
    {
     free(df->blk);
     df->blk=(void *)NULL;
    }
 blk=getfdbf(df,at,psz);                         /* do it the normal way */
 df->blk=(void *)NULL;                           /* disown the buffer */
 df->blksz=0;
 return(blk);
}

/************************************************************************/


/* reads in the info from a FDBF directly into the user's buffer.  This is
NOT really intended for general use, but saves time when you know what
you're doing.  If the size you request not equal to the actual size of the
object this will return 0 (error) but will do the read anyway, but you
wont be able to tell it from a real error. */

size_t                            /* 1 == ok or 0 == failed/incorrect size */
readfdbf(df,epi_at,off,buf,sz)
FDBF    *df;
EPI_OFF_T   epi_at; /* the handle, or -1 for next block */
size_t  *off; /* relative offset into buffer (used by KDBF KNG 980226) */
void  *buf; /* your buffer */
size_t  sz; /* how many bytes you want */
{
  static CONST char     fn[] = "readfdbf";
  off_t at;

  if (!tx_okfdbf(df->fn)) return(0);
  CHECK_RANGE(fn, df, epi_at, at, return(0));

#ifdef DEBUG
 putmsg(999, NULL, "In readfdbf");
#endif
#ifdef FDBFDISPLAY
 putmsg(999, NULL, "Readfdbf %s, %d bytes at %d", df->fn, sz, at);
#endif
 if(at== -1L) /* read next block */
    {
     off_t eof;
     if(
        (at=ltell(df->fh))<0L ||
        (eof=lseek(df->fh,-(off_t)sizeof(off_t),SEEK_END))<=at ||
        lseek(df->fh,at,SEEK_SET)!=at
       ) return(0);
    }

 if(!readhead(df,at) ||
    (size_t)read(df->fh, buf, sz) != df->used
   ) return(0);

 return(df->used);              /* KNG 980226 now returns block size */
}

/************************************************************************/

/*
puts a block of memory into the fdbf file:  If "at" is -1 then it will
allocate a new block on disk.  If "sz" of data will not fit in the
existing block then it will free that block and allocate a new one.  Don't
ignore the return value because it is not guaranteed that a disk block
wont move on a put operation unless the size of the block is the same as
the one on disk, and even then: "stuff happens".

The return value is a handle to new disk block, or -1L if error.
Don't try to directly read or write to this file. You'll trash it.

*/

EPI_OFF_T
putfdbf(df,epi_at,buf,sz)
FDBF    *df;
EPI_OFF_T   epi_at;
void   *buf;
size_t  sz;
{
  static CONST char     fn[] = "putfdbf";
  off_t at;
#ifdef NEVER
 size_t	tsz;
 void	*v;
#endif

 if (!tx_okfdbf(df->fn)) return((EPI_OFF_T)(-1));
 CHECK_RANGE(fn, df, epi_at, at, return((EPI_OFF_T)(-1)));

#ifdef FDBFDISPLAY
 putmsg(999, NULL, "Putfdbf %s, %d bytes at %d", df->fn, sz, at);
#endif
 if(at== -1L)                            /* -1 means allocate a new one */
 {
    at = (off_t)fdbfalloc(df,buf,sz);
#ifdef NEVER
    v = getfdbf(df, 0, &tsz);
    while(v)
       v = getfdbf(df, -1, &tsz);
#endif
    return at;
 }
 else if(at>=0L)
    {
     if(!readhead(df,at)) return(-1L);
     if(df->size<sz)              /* block too small now make a new one */
       {
        if(!freefdbf(df,at) ||                  /* free the existing one */
           (at=(off_t)fdbfalloc(df,buf,sz))<0L  /* allocate a new one */
          ) return(-1L);
       }
     else                           /* the existing block is big enough */
       {
        if(!writealloc(df,buf,sz))             /* just put the data out */
           return(-1L);
       }
    }
#ifdef NEVER
 v = getfdbf(df, 0, &tsz);
 while(v)
    v = getfdbf(df, -1, &tsz);
#endif
 return(at);
}

/************************************************************************/


#ifdef TEST
#ifdef EPI_HAVE_STDARG
#  include "stdarg.h"
int CDECL putmsg(int n,char *fn,char *fmt,...)
{
va_list args;

   va_start(args,fmt);
#else
#  include "varargs.h"
int CDECL
putmsg(va_alist)                                /* args(n,fn,fmt,...) */
va_dcl
{
 va_list args;
 int n;
 char *fn, *fmt;

 va_start(args);
 n  =va_arg(args,int   );                               /* msg number */
 fn =va_arg(args,char *);                            /* function name */
 fmt=va_arg(args,char *);                            /* printf format */
#endif
 if(n>=0) fprintf(stderr,"%03d ",n);
 if(fmt!=(char *)NULL)
    vfprintf(stderr,fmt,args);           /* print the message content */
 if(fn!=(char *)NULL) fprintf(stderr," in the function: %s",fn);
 fputs("\n",stderr);
 va_end(args);
 return(0);
}

/**********************************************************************/

#define LNSZ 512

char ln[LNSZ];

int
main(argc,argv)
int argc;
char *argv[];
{
 int i;
 FDBF *df;
 int load=0;
 for(i=1;i<argc && *argv[i]=='-';i++)
    {
     switch(*(++argv[i]))
         {
          case  'l' : load=1;break;
          default   : printf("%c invalid\n",*argv[i]);
          case  'h' : printf("USE %s your name here\n",argv[0]);
                      exit(1);
         }
    }
 if((df=openfdbf("tmp"))!=FDBFPN)
    {
     if(load)
         {
          while(
                gets(ln) &&
                putfdbf(df,-1L,(void *)ln,strlen(ln)+1)>=0L
               );
          goto END;
         }
     while(gets(ln))
         {
          char *dummy=NULL;
          size_t len;
          long o;
          switch(*ln)
              {
               case 'p' : printf("%08lX\n",putfdbf(df,-1L,(void *)ln,strlen(ln)+1));break;
               case 'g' : puts(getfdbf(df,strtol(ln+1,&dummy,16),&len));break;
               case 'f' : puts(freefdbf(df,strtol(ln+1,&dummy,16)) ? "ok" : "fail");break;
               case 'd' : for(o=0L;(dummy=getfdbf(df,o,&len))!=NULL;o=-1L)
                             puts(dummy);
                          break;
               case 'q' : goto END;
              }
         }
     END:
     closefdbf(df);
    }
 exit(0);
}

#endif /* test */
/************************************************************************/


