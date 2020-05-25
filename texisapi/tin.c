#include "txcoreconfig.h"
#include "stdio.h"
#ifdef EPI_HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef MSDOS
#include "io.h"                                           /* access() */
#endif
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include "dbquery.h"
#include "texint.h"
#include "tin.h"


#undef ERROR	/* WINNT defines ERROR */

/************************************************************************/

#ifdef OLD_MM_INDEX

long   tindictmem=TINDICTMEM;       /* how much xtree memory can it use */
long   tinmergmem=TINMERGMEM;       /* how much merge memory can it use */
char  *tintmpprefix=TINTMPPREFIX;/* prefix appended onto tmp file names */

static int  tincmp ARGS((void  *nothing,byte  *a,size_t  alen,byte  *b,size_t  blen));
static int  tinput ARGS((TIN *ti,byte  *buf,size_t  len));
static int  tinxtcb ARGS((TIN *ti,byte  *s,size_t  len,int  cnt, void *seq));

/************************************************************************/

TIN *
closetin(ti)
TIN *ti;
{
 if(ti!=TINPN)
    {
     if(ti->tmdpre!=(char *)NULL) free(ti->tmdpre);
     if(ti->omdpre!=(char *)NULL) free(ti->omdpre);
     if(ti->xt!=XTREEPN) closextree(ti->xt);
     if(ti->rl!=RLEXPN)  closerlex (ti->rl);
#ifdef OLD_MM_INDEX
     if(ti->md!=MDATPN ) closemdat (ti->md);
#endif
     free((void *)ti);
    }
 return(TINPN);
}

/************************************************************************/

TIN *
opentin(ofpre,explst,handle)
char   *ofpre;                /* the MDAT output file prefix to be used */
char **explst;                    /* a list of REX expressions to match */
tin_t  handle;       /* the handle to assign to the matched expressions */
{
 TIN *ti=(TIN *)calloc(1,sizeof(TIN));
 if(ti!=TINPN)
    {
     ti->dmem  =tindictmem;
     ti->mmem  =tinmergmem;
     ti->xt    =XTREEPN;
     ti->rl    =RLEXPN;
     ti->md    =MDATPN;
     ti->exprs =(byte **)explst;
     ti->th    =handle;
     ti->n     =0L;
     ti->omdpre=strdup(ofpre);
     ti->noise =(byte **)NULL;
     ti->tmdpre=tmpmdpre(tintmpprefix);
     if((ti->xt=openxtree(TXPMBUFPN, ti->dmem))==XTREEPN ||
        (ti->rl=openrlex((char **)ti->exprs))==RLEXPN  ||
        (ti->md=openmdat(ti->omdpre,"w"))==MDATPN
       ) return(closetin(ti));
    }
 return(ti);
}

/************************************************************************/

void
tinnoise(ti,lst)
TIN *ti;
char **lst;
{
 int i;
 ti->noise=(byte **)lst;
 if(ti->noise!=(byte **)NULL)
    {
     for(i=0;*ti->noise[i] && i<8;i++)
         {
           size_t len = strlen((char *)ti->noise[i]);
           if(len>1)        /* most users wont match <2 */
                putxtree(ti->xt,(byte *)ti->noise[i],len);
         }
    }
}

/************************************************************************/

/* call back function to write dictionary entries into the MDAT file */

static byte  *tincbbuf=(byte *)NULL;  /* used by the call back to construct a record WTF this is never freed */
static size_t tincbbufsz;                              /* size of above */
static int    tincbstatus=0;                           /* did the call back pass */

static int
tinxtcb(ti,s,len,cnt,seq)
TIN *ti;   /* this should be a void *, but Bill Gates' stuff is broken! */
byte     *s;
size_t  len;
int     cnt;
void   *seq;                     /* MAW 07-26-96 - insertion sequence */
{
 byte *p;
 size_t size=sizeof(tin_t)+len;
 if(tincbbufsz<size)                                /* manage my buffer */
    {
     tincbbufsz=0;
     if(tincbbuf!=(byte *)NULL)
         {
          free(tincbbuf);
          tincbbuf=(byte *)NULL;
         }
     if((tincbbuf=(byte *)malloc(size))==(byte *)NULL)
        {
         tincbstatus=0;
         return(0);
        }
     tincbbufsz=size;
    }
 p=tincbbuf;                                    /* construct the record */

 /* memcpy((void *)p,(void *)&ti->th,sizeof(tin_t));  replaced by below */
 *(tin_t *)p=ti->th;

 p+=sizeof(tin_t);
 memcpy((void *)p,(void *)s,len);
 if(putmdat(ti->md,tincbbuf,(int)size)<0)               /* write it out */
    {
     tincbstatus=0;
     return(0);
    }
 return(1);
}

/************************************************************************/

/* flushes the current contents of the xtree in a tin into the
mdat file. Then it resumes with a new mdat. returns 1 if ok , 0 if not */

int
nexttin(ti,handle)
TIN  *ti;
tin_t handle;
{
 if(ti->noise!=(byte **)NULL)
     delxtreesl(ti->xt,ti->noise);

 tincbstatus=1;

                                   /* write out & delete the dictionary */
 rmwalkxtree(ti->xt,(int (*)ARGS((void *, byte *, size_t, int,void *)))tinxtcb,(void *)ti);
 tinnoise(ti,(char **)ti->noise);

#ifdef NEVER
#ifdef ZEROTHETREE
 zeroxtree(ti->xt);   /* reset the counters on all the dictionary items */
#else
 ti->xt=closextree(ti->xt);
 if((ti->xt=openxtree(TXPMBUFPN, ti->dmem))==XTREEPN)
    return(0);
 tinnoise(ti,ti->noise);
#endif
#endif /* NEVER */


 if(nextmdat(ti->md)!=MDATPN &&                 /* create the next mdat */
    tincbstatus
   ) {
      ti->n+=1;
      ti->th=handle;
      return(1);
     }
 return(0);
}

/************************************************************************/
/* matches all the rexes in the buffer and puts them into the xtree.
returns how many were matched or -1L on error */

long
puttin(ti,buf,end)
TIN  *ti;
byte *buf;
byte *end;
{
 byte *p,*s,*t;
 long cnt=0;
 size_t len;
 int rc;

 for(p=getrlex(ti->rl,buf,end,SEARCHNEWBUF);
     p!=BPNULL;
     p=getrlex(ti->rl,buf,end,CONTINUESEARCH)
    ){
      len=(size_t)rlexlen(ti->rl);

      for(s=p,t=p+len;s<t;s++) *s=tolower(*s);        /* lower the case */

      rc=putxtree(ti->xt,p,len);             /* shove it out to the file */

      if(rc<0)                                /* the dictionary is full */
         {
          if(!nexttin(ti,ti->th))        /* write out the current stuff */
              return(-1L);
#ifdef NEVER
          closextree(ti->xt);         /* release the current dictionary */
          if((ti->xt=openxtree(TXPMBUFPN, ti->dmem))==XTREEPN)   /* make a new one */
              return(-1L);
#endif
         }
      else if(rc==0)
         return(-1L);
      ++cnt;
     }
 return(cnt);
}

/************************************************************************/
    /* compares two TIN MDAT recs together for the merge operation */

static int
tincmp(nothing,a,alen,b,blen)
void *nothing; /* I dont use this data in tin */
byte   *a;
size_t  alen;
byte   *b;
size_t  blen;
{                                   /* see rec format description above */
 size_t len= (alen<blen ? alen : blen)-sizeof(tin_t);
 int rc;
 if(a==(byte *)NULL)
    {
     if(b==(byte *)NULL) return(0);
     else return(-1);
    }
 if(b==(byte *)NULL)
    return(1);
 rc=memcmp((void *)(a+sizeof(tin_t)),(void *)(b+sizeof(tin_t)),len);
 if(rc==0)
    {
     rc= alen>blen ? 1 : blen==alen ? 0 : -1;
    }
 if(rc==0)          /* the strings are the same, so compare the handles */
    {
     long  ttrc=*(tin_t *)a-*(tin_t *)b;
     rc= ttrc > 0 ? 1 : ttrc<0 ? -1 : 0;
    }
 return(rc);
}

/************************************************************************/

                     /* call the user put function */

static int tinput ARGS((TIN *, byte *, size_t));

static int
tinput(ti,buf,len)
TIN *ti;                     /* should be void * but MS-C sucks at this */
byte *buf;
size_t len;
{
 len-=sizeof(tin_t);
 return((*ti->usrput)(ti->usr,*(tin_t *)buf,buf+sizeof(tin_t),len));
}

/************************************************************************/

/* merges all the tin's MDAT files into one MDAT  or users put function */
                /* implicitly closes down the TI handle */

int
tinmerge(ti,usrput,usr)
TIN *ti;
int (*usrput) ARGS((void *, tin_t, byte *, size_t));
void *usr;
{
 int rc=0;
 if(nexttin(ti,ti->th))                  /* flush the current stuff out */
    {
     if(ti->md!=MDATPN)  ti->md=closemdat(ti->md);    /* close the mdat */
     if(ti->xt!=XTREEPN) ti->xt=closextree(ti->xt);    /* free up space */
     if(ti->rl!=RLEXPN)  ti->rl=closerlex(ti->rl);     /* free up space */
     ti->usrput=usrput;
     ti->usr   =usr;
     if(usrput==(int (*)())NULL)
          rc=mergemdat(ti->omdpre,ti->tmdpre,tincmp,(int (*)())NULL,(void *)NULL,ti->mmem);
     else
     {
          rc=mergemdat(ti->omdpre,ti->tmdpre,tincmp,
	  	(int (*)ARGS((void *, byte *, size_t)))tinput,
		(void *)ti,ti->mmem);
	  (*usrput)(ti->usr, 0, (byte *)"", 0);
     }
#ifdef NEVER
     closetin(ti);
#endif
    }
 return(rc);
}

/************************************************************************/
/************************************************************************/
/****Begin of Tin Type List handling functions **************************/
/************************************************************************/
/************************************************************************/

/* PBR 06-25-93 I copied the VSL functions that are in dbtable just in case
they might have to change.  I thought that these might need to be
independent from dbtable.
*/

/*
VSL stands for Variable Sized Long

This is for storing and retrieving unsigned long values in the minimum
number of bytes required to hold the given value.

The highest value that may be stored in one of these is
2^30 or about 1 billion ( I hope thats big enough )

*/

#define VSL1MAX 0x3F
#define VSL2MAX 0x3FFF
#define VSL3MAX 0x3FFFFF
#define VSL4MAX 0x3FFFFFFF

#endif /* OLD_MM_INDEX */

/************************************************************************/

TTL *
closettl(tl)
TTL *tl;
{
 if(tl!=TTLPN)
    {
     if(tl->buf!=(byte *)NULL) free(tl->buf);
     free((void *)tl);
    }
 return(TTLPN);
}

/************************************************************************/

TTL *
openttl()
{
 TTL *tl=(TTL *)calloc(1,sizeof(TTL));
 if(tl!=TTLPN)
    {
     tl->gp=tl->pp=tl->buf=(byte *)malloc(TTLALLOCSZ);   /* seed buffer */
     tl->end=tl->buf+TTLALLOCSZ;
     if(tl->buf==(byte *)NULL)
         return(closettl(tl));
     tl->bufsz=TTLALLOCSZ;
     tl->val=0;
     tl->orun=tl->irun=0;
     tl->stvalid = 1;
    }
 return(tl);
}

/************************************************************************/

/* returns next value in ttl list. returns 0 if there aren't any more */

int
getttl(tl,pval)
TTL   *tl;
tin_t *pval;      /* a pointer to a tin_t that you wish to have stuffed */
{
 if(tl->irun)
    {
     RUN:
     tl->val  += 1;
     tl->irun -= 1;
     *pval=tl->val;
     return(1);
    }
 if(tl->gp<tl->end)
    {
     tl->gp=ivsl(tl->gp,pval);
     if(*pval==0)
        {
         tl->gp=ivsl(tl->gp,&tl->irun);
         goto RUN;
        }
     *pval+=tl->val;
     tl->val=*pval;
     return(1);
    }
 return(0);
}

/************************************************************************/

 /* puts a new value at the end of a ttl. the values must be added in
 ascending order. returns 1 if added, 0 if not added  */

int
putttl(tl,val)
TTL *tl;
tin_t val;
{                                     /* 17 == 0 + orun-length + new vsl */
 if(tl->pp+17>tl->end)  /* there are three possible vsl's to be written */
    {
     byte  *new;           /* keep list integrity while increasing size */
     size_t bufsz=tl->bufsz;
     size_t off=tl->pp - tl->buf;
     bufsz+=TTLALLOCSZ;
     if((new=(byte *)malloc(bufsz))==(byte *)NULL)
        return(0);
     memcpy((void *)new,(void *)tl->buf,off);
     free(tl->buf);
     tl->gp=new+(tl->gp-tl->buf);          /* re-assign the get pointer */
     tl->pp=new+off;                        /* reassign the put pointer */
     tl->buf=new;
     tl->bufsz=bufsz;
     tl->end=new+bufsz;
    }

 if(val<tl->val)
    {
     putmsg(MERR,"putttl","order error");
     return(0);                               /* insertion out of order */
    }
 else if(tl->val==val)                     /* duplicate insertion is ok */
    return(1);

 if(val-tl->val==1)                          /* check for a run of ones */
 {
    tl->orun+=1;
 }
 else
    {
     if(tl->orun!=0)                                 /* spit the orun out */
         {
          if(tl->orun!=1)
            TXoutputVariableSizeLong(TXPMBUFPN, &tl->pp, (ulong)0, NULL);
          TXoutputVariableSizeLong(TXPMBUFPN, &tl->pp, tl->orun, NULL);
          tl->orun=0;
         }
     TXoutputVariableSizeLong(TXPMBUFPN, &tl->pp,val-tl->val, NULL);  /* ttls are stored in delta form */
    }
 tl->count+=1;
 tl->val=val;
 return(1);
}

/************************************************************************/

/* resets the condition of a TTL to empty */

void
resetttl(tl)
TTL *tl;
{
 tl->gp=tl->pp=tl->buf;
 tl->val=0L;
 tl->handle= -1L;
}


/************************************************************************/

/* reposition the get pointer to the begin of list. This has A BIG effect
on the put pointer...  */                    /*FIX THIS!!! PBR 07-07-93 */

void
rewindttl(tl)
TTL *tl;
{
 tl->gp=tl->buf;
 tl->end=tl->pp;
 tl->val=0L;
 tl->irun=0;
}

/************************************************************************/

TTL *
subttl(a,b)
TTL *a,*b;
{
 TTL *c=openttl();
 tin_t av,bv;
 int aok,bok;
 if(c==TTLPN) return(c);
 rewindttl(a);
 rewindttl(b);
 aok=getttl(a,&av);
 bok=getttl(b,&bv);
 if(!bok)
 {
 	closettl(c);
	return a;
 }

 while(aok && bok)
     {
      if(av==bv)
         {
          aok=getttl(a,&av);
          bok=getttl(b,&bv);
         }
      else if(av<bv)
      {
           if(!putttl(c,av)) goto ERROR;
           aok=getttl(a,&av);
      }
      else bok=getttl(b,&bv);
     }
 while(aok)
 {
	if(!putttl(c,av)) goto ERROR;
	aok=getttl(a,&av);
 }
 closettl(a);
 if(c->orun!=0)                             /* spit the run out */
    {
     if(c->orun!=1)
       TXoutputVariableSizeLong(TXPMBUFPN, &c->pp, (ulong)0, NULL);
     TXoutputVariableSizeLong(TXPMBUFPN, &c->pp, c->orun, NULL);
     c->orun=0;
    }
 return(c);
 ERROR :
 return(closettl(c));
}

/************************************************************************/
#ifdef NEVER

/* or's the list in a with the list in b and places the output in c */

TTL *
orttl(a,b)
TTL *a,*b;
{
 TTL *c=openttl();
 tin_t av,bv;
 int aok,bok;
 if(c==TTLPN) return(c);
 rewindttl(a);
 rewindttl(b);
 aok=getttl(a,&av);                                             /* seed */
 bok=getttl(b,&bv);
 do  {
      if(aok && bok)
          {
           if(av<=bv)
               {
                if(!putttl(c,av)) goto ERROR;
                if(av==bv)                           /* flush b's value */
                   bok=getttl(b,&bv);
                aok=getttl(a,&av);
               }
           else
               {
                if(!putttl(c,bv)) goto ERROR;
                bok=getttl(b,&bv);
               }
          }
       else if(aok)
          {
           if(!putttl(c,av)) goto ERROR;
           aok=getttl(a,&av);
          }
       else if(bok)
          {
           if(!putttl(c,bv)) goto ERROR;
           bok=getttl(b,&bv);
          }
     }
 while(aok || bok); /* while there's stuff in one of the two lists */
 closettl(a);
 closettl(b);
 if(c->orun!=0)                             /* spit the run out */
    {
     if(c->orun!=1)
       TXoutputVariableSizeLong(TXPMBUFPN, &c->pp, (ulong)0, NULL);
     TXoutputVariableSizeLong(TXPMBUFPN, &c->pp, c->orun, NULL);
     c->orun=0;
    }
 return(c);
 ERROR :
 return(closettl(c));
}

#endif /* NEVER */
/************************************************************************/

/* and's the list in a with the list in b and places the output in c */

TTL *
andttl(a,b)
TTL *a,*b;
{
 TTL *c=openttl();
 tin_t av,bv;
 int aok,bok;
 if(c==TTLPN) return(c);
#ifndef NO_INFINITY
	if (TXisinfinite(a))
	{
		closettl(c);
		closettl(a);
		return b;
	}
	if (TXisinfinite(b))
	{
		closettl(c);
		closettl(b);
		return a;
	}
#endif
 rewindttl(a);
 rewindttl(b);
 aok=getttl(a,&av);
 bok=getttl(b,&bv);

 while(aok && bok)
     {
      if(av==bv)
         {
          if(!putttl(c,bv)) goto ERROR;
          aok=getttl(a,&av);
          bok=getttl(b,&bv);
         }
      else if(av<bv)
           aok=getttl(a,&av);
      else bok=getttl(b,&bv);
     }

 closettl(a);
 closettl(b);
 if(c->orun!=0)                             /* spit the run out */
    {
     if(c->orun!=1)
       TXoutputVariableSizeLong(TXPMBUFPN, &c->pp, (ulong)0, NULL);
     TXoutputVariableSizeLong(TXPMBUFPN, &c->pp, c->orun, NULL);
     c->orun=0;
    }
 return(c);
 ERROR :
 return(closettl(c));
}


/************************************************************************/

/* gets a TTL from a dbf file, and returns a pointer to the new TTL
struct or NULL on error/eof */

TTL *
getdbfttl(df,at)
DBF *df;
EPI_OFF_T at;
{
#if 0
 static long sumsz = 0, count = 0;
#endif

 TTL *tl=(TTL *)calloc(1,sizeof(TTL));
 if(tl!=TTLPN)
    {
     if((tl->buf=(byte *)agetdbf(df,at,&tl->bufsz))==(byte *)NULL)
         return(closettl(tl));
#if 0
     sumsz+=tl->bufsz;
     count++;
	putmsg(999, NULL, "Read a ttl of %d bytes (avg = %d)", tl->bufsz, sumsz/count);
#endif /* 0 */
     tl->val=0L;
     tl->end=tl->pp=tl->buf+tl->bufsz;
     tl->gp=tl->buf;
     tl->handle=telldbf(df);
     tl->orun=tl->irun=0;
    }
 return(tl);
}

/************************************************************************/
/* puts a ttl into a dbf file. if "at" is -1L then it will create a new
one. It returns a dbf handle or -1L on error . */

EPI_OFF_T
putdbfttl(df,at,tl)
DBF *df;
EPI_OFF_T at;
TTL *tl;
{
                /* make sure a any oruns are outputted before the write */
 if(tl->orun!=0)                                    /* spit the run out */
    {
     if(tl->orun!=1)
       TXoutputVariableSizeLong(TXPMBUFPN, &tl->pp, (ulong)0, NULL);
     TXoutputVariableSizeLong(TXPMBUFPN, &tl->pp, tl->orun, NULL);
     tl->orun=0;
    }
                    /* MAW 04-04-94 - replace drill w/setdbfoveralloc() */
 setdbfoveralloc(df,8);                        /* overallocate by 1/8th */
 return(putdbf(df,at,tl->buf,(size_t)(tl->pp-tl->buf)));
}

/************************************************************************/
/*               END of Tin Type List handling functions                */
/************************************************************************/

/************************************************************************/
/*                    BEGIN TIN TABLE HANDLERS                          */
/************************************************************************/

/* WTF open/closettbl() now only used in 3dbindex.c  KNG 980520 */
TTBL *
closettbl(tt)
TTBL *tt;
{
 if(tt!=TTBLPN)
    {
#ifdef OLD_MM_INDEX
     if(tt->ti!=TINPN) closetin(tt->ti);
     if(tt->tl!=TTLPN) closettl(tt->tl);
     if(tt->exc!=TTLPN) closettl(tt->exc);
     if(tt->bdbf!=DBFPN) closedbf(tt->bdbf);
#endif /* OLD_MM_INDEX */
     if(tt->bt!=(BTREE *)NULL) closebtree(tt->bt);
     free(tt);
    }
 return(TTBLPN);
}

/************************************************************************/

extern TTBL *
openttbl(tblnm,explst)
char *tblnm;
char **explst;
{
 char tbuf[1024];
 TTBL *tt=(TTBL *)calloc(1,sizeof(TTBL));

 TXstrncpy(tbuf, tblnm, 1019);
 strcat(tbuf, ".blb");

 if(tt!=TTBLPN)
    {
#ifdef OLD_MM_INDEX
     tt->ti=TINPN;
     tt->tl=TTLPN;
     tt->exc=TTLPN;
     tt->handle=0;
     if(access(tbuf, 0))
         tt->newdb = 1;
     else
         tt->newdb = 0;
#endif /* OLD_MM_INDEX */
     if (
#ifdef OLD_MM_INDEX
         (tt->ti=opentin(tblnm,explst,tt->handle)) == TINPN ||
         (tt->tl=openttl()) == TTLPN         ||
         (tt->exc=openttl()) == TTLPN        ||
         (tt->bdbf=opendbf(tbuf))==DBFPN   ||
#endif /* OLD_MM_INDEX */
         (tt->bt=openbtree(tblnm, TTBTPSZ, TTBTCSZ, BT_UNIQUE, O_RDWR))==(BTREE *)NULL
        ) return(closettbl(tt));
    }
 return(tt);
}

/************************************************************************/

extern TTBL *
openrttbl(tblnm,explst)
char *tblnm;
char **explst;
{
 char tbuf[1024];
 TTBL *tt=(TTBL *)calloc(1,sizeof(TTBL));

 TXstrncpy(tbuf, tblnm, 1019);
 strcat(tbuf, ".blb");

 if(tt!=TTBLPN)
    {
#ifdef OLD_MM_INDEX
     tt->ti=TINPN;
     tt->tl=TTLPN;
     tt->exc=TTLPN;
     tt->handle=0;
     if(access(tbuf, 0))
         tt->newdb = 1;
     else
         tt->newdb = 0;
#endif /* OLD_MM_INDEX */
     if (
#ifdef OLD_MM_INDEX
         (tt->bdbf=opendbf(tbuf))==DBFPN   ||
#endif /* OLD_MM_INDEX */
         (tt->bt=openbtree(tblnm, TTBTPSZ, TTBTCSZ, BT_UNIQUE, O_RDWR))==(BTREE *)NULL
        ) return(closettbl(tt));
    }
 return(tt);
}

/************************************************************************/

#ifdef OLD_MM_INDEX
/*
Assigns the matched strings in a buffer to the specified handle.
(Actually, it manages the TIN additions to its MDAT file.)
It returns the number of matched strings, or -1L on error.
*/

long
addttbl(tt,buf,end,handle)
TTBL *tt;
byte *buf,*end;
tin_t handle;
{
 if(tt->handle==0)                            /* 0 is a reserved handle */
    tt->ti->th=tt->handle=handle;
 else
 if(handle!=tt->handle)  /* check to see if we're using the same handle */
    {
     if(!nexttin(tt->ti,handle))             /* nope, flush the TIN out */
         return(-1L);
     tt->handle=handle;
    }
 return(puttin(tt->ti,buf,end));       /* add the new data into the TIN */
}

/************************************************************************/

/* this is the call back routine for the merge phase of TIN */

static byte *ttcblast=(byte *)NULL;
size_t       ttcblastlen=0;
size_t       ttcbbufsz=0;
tin_t        ttcblasthandle;

static int
ttblnewmcb(tt,handle,buf,len)
TTBL *tt;           /* this is passed as a void *, but uSoft is broken */
tin_t handle;
byte *buf;
size_t len;
{

 if(len!=ttcblastlen ||
 	ttcblast==(byte *)NULL ||
	memcmp((void *)buf,(void *)ttcblast,len))
    {                               /* they're not same, so spit it out */
     if(ttcblast!=(byte *)NULL && *ttcblast)
         {                                        /* shove the list out */
          BTLOC rowh;
          if((TXsetrecid(&rowh,putdbfttl(tt->bdbf,-1L,tt->tl)))<0L)
              return(0);                                   /* put error */
          btspinsert(tt->bt, &rowh, ttcblastlen, ttcblast, 90);
         }
     resetttl(tt->tl);                                /* clear the list */
     if(len>ttcbbufsz)                            /* realloc the buffer */
         {
          if(ttcblast!=(byte *)NULL) free(ttcblast);
          if((ttcblast=(byte *)malloc(len))==(byte *)NULL)
             {
              ttcbbufsz=0;
              return(0);
             }
          ttcbbufsz=len;
         }
     memcpy((void *)ttcblast,(void *)buf,len);
     ttcblastlen=len;
    }

 if(handle!=ttcblasthandle && handle != 0)              /* add the token into the list */
    return(putttl(tt->tl,(tin_t)handle));
 return(1);
}


static int
ttblupdmcb(tt,handle,buf,len)
TTBL *tt;           /* this is passed as a void *, but uSoft is broken */
tin_t handle;
byte *buf;
size_t len;
{
 if(len!=ttcblastlen ||
 	ttcblast == (byte *)NULL ||
	memcmp((void *)buf,(void *)ttcblast,len))
    {                               /* they're not same, so spit it out */
     if(ttcblast!=(byte *)NULL && *ttcblast)
         {                                        /* shove the list out */
          BTLOC rowh;

          rowh=btsearch(tt->bt, ttcblastlen, ttcblast);
          if(tt->tl->orun!=0)                             /* spit the run out */
          {
             if(tt->tl->orun!=1)
               TXoutputVariableSizeLong(TXPMBUFPN, &tt->tl->pp, (ulong)0,
                                        NULL);
             TXoutputVariableSizeLong(TXPMBUFPN, &tt->tl->pp, tt->tl->orun,
                                      NULL);
             tt->tl->orun=0;
          }
          if (TXrecidvalid(&rowh))
          /*
              Get old list; merge it; rewrite it;
          */
          {
              TTL *tl=getdbfttl(tt->bdbf, TXgetoff(&rowh));
              TTL *dtl=subttl(tl, tt->exc);
              TTL *ntl=orttl(dtl, tt->tl);
              tt->tl = ntl;
              TXsetrecid(&rowh,putdbfttl(tt->bdbf,TXgetoff(&rowh), tt->tl));
/*
              rowh.loc=putdbfttl(tt->bdbf, -1, tt->tl);
*/
              if (!TXrecidvalid(&rowh))
                   return 0;
              btupdate(tt->bt, rowh);
          }
          else
          {
              if((TXsetrecid(&rowh,putdbfttl(tt->bdbf,-1L,tt->tl)))<0L)
                  return(0);                               /* put error */
              btinsert(tt->bt, &rowh, ttcblastlen, ttcblast);
          }
         }
     resetttl(tt->tl);                                /* clear the list */
     if(len>ttcbbufsz)                            /* realloc the buffer */
         {
          if(ttcblast!=(byte *)NULL) free(ttcblast);
          if((ttcblast=(byte *)malloc(len))==(byte *)NULL)
             {
              ttcbbufsz=0;
              return(0);
             }
          ttcbbufsz=len;
         }
     memcpy((void *)ttcblast,(void *)buf,len);
     ttcblastlen=len;
    }

 if(handle!=ttcblasthandle && handle != 0)   /* add the token into the list */
    return(putttl(tt->tl,(tin_t)handle));
 return(1);
}


/************************************************************************/

/* Performes the Table creation step after you have done the add()'s.
returns 1 if ok , 0 if something bad happened */


int
putttbl(tt)
TTBL *tt;
{
 int rc;
 if (tt->newdb)
      rc=tinmerge(tt->ti,(int (*)ARGS((void *,tin_t,byte *,size_t)))ttblnewmcb,(void *)tt);         /* call the merge */
 else
      rc=tinmerge(tt->ti,(int (*)ARGS((void *,tin_t,byte *,size_t)))ttblupdmcb,(void *)tt);         /* call the merge */
 resetttl(tt->tl);
 if(ttcblast!=(byte *)NULL)
    {
     free(ttcblast);
     ttcblast=(byte *)NULL;
     ttcbbufsz=ttcblastlen=0;
    }
 return(rc);
}
#endif /* OLD_MM_INDEX */

/************************************************************************/
/*                      END TIN TABLE HANDLERS                          */
/************************************************************************/


#ifdef TEST

#ifdef MSDOS
#include <io.h>
#   define IOMODE   O_RDWR|O_BINARY|O_CREAT,S_IREAD|S_IWRITE
#   define IMODE    O_RDONLY|O_BINARY,S_IREAD
#   define OMODE    O_WRONLY|O_CREAT|O_TRUNC|O_BINARY,S_IREAD
#else
#   define IOMODE   O_RDWR|O_CREAT,S_IREAD|S_IWRITE
#   define IMODE    O_RDONLY
#   define OMODE    O_WRONLY|O_CREAT|O_TRUNC,S_IREAD|S_IWRITE
#endif

/*
char *expressions[]={
"[\\alpha\\_]=[\\alnum\\_]+",
"[\\alpha\\_]=[\\alnum\\_\\-\\>]+",
"\\digit+",
"0x=[0-9a-fA-F]+",
""};
*/

char *expressions[]={
"\\alpha{2,}",
""};


#define RDBUFSZ 32000
byte rdbuf[RDBUFSZ];


/************************************************************************/

#ifdef TESTA

static byte *last;
size_t       lastlen=0;

#include "string.h"

int
put(usr,handle,buf,len)
void *usr;
tin_t handle;
byte *buf;
size_t len;
{
 if(strcmp((char *)last,(char *)buf)==0)
    printf(",%lu",(unsigned long)handle);
 else
    {
     free(last);
     last=(byte *)strdup((char *)buf);
     printf("\n%s\t%lu",buf,(unsigned long)handle);
    }
 return(1);
}

/************************************************************************/

int
main(argc,argv)
int argc;
char *argv[];
{
 int i;
 TIN *ti;
 tin_t handle=1;
 int nread;
 long total=0L;
 char fname[1024];
 for(i=1;i<argc && *argv[i]=='-';i++)
    {
     switch(*(++argv[i]))
         {
          case  ' ' : break;
          default   : printf("%c invalid\n",*argv[i]);
          case  'h' : printf("USE %s your name here\n",argv[0]);
                      exit(1);
         }
    }

 rmmdat("tmptin");
 if((ti=opentin("tmptin",expressions,handle))!=TINPN)
    {
     tinnoise(ti,xtnoiselst);
     while(gets(fname)!=NULL)   /* get the fname from the file */
         {
          int fh=open(fname,IMODE);
          if(fh>0)
              {
               fprintf(stderr,"%s\t",fname);
               while((nread=read(fh,(void *)rdbuf,RDBUFSZ))>0)
                   {
                    fprintf(stderr,"%8ld\b\b\b\b\b\b\b\b",total+=nread);
                    if(puttin(ti,rdbuf,rdbuf+nread)<0L)
                       {
                        fputs("put error\n",stderr);
                        exit(0);
                       }
                     if(!nexttin(ti,++handle))
                        {
                         fputs("NEXT FAILED",stderr);
                         exit(0);
                        }
                   }
               fprintf(stderr,"\n");
               close(fh);
              }
         }
     fputs("merging...",stderr);
     last=(byte *)calloc(1,1);
     puts(tinmerge(ti,put,(void *)NULL) ? "MERGE OK" : "MERGE FAILED");
     free(last);
    }
 exit(0);
}
#else /* NOT TESTA */

/************************************************************************/

int
prefcmp(void *a, size_t al, void *b, size_t bl, void *usr)
{
        int rc, tocmp;

        tocmp = al < bl ? al : bl;
        rc = memcmp(a, b, tocmp);
        if (rc == 0)
                rc = al - bl;
        if (rc == 0)
                rc = 1;
        return rc;
}

/************************************************************************/
int counts=0;

void
dumpttbl(nm)
char *nm;
{
 BTREE *bt=openbtree(nm, TTBTPSZ, TTBTCSZ, 0);
 TTL *tl=openttl();
 DBF *bdbf;
 char key[1024], s[1024];

 btsetcmp(bt, prefcmp);
 TXstrncpy(key, nm, 80);
 strcat(key, ".blb");
 bdbf=opendbf(key);

 if(bt !=(BTREE *)NULL && tl!=TTLPN)
    {
     while(gets(key))
         {
          size_t sz;
          tin_t n = 0;
          int stlen;
          long   cnt=0;
          long  dbfh= btsearch(bt, strlen(key), (void *)strlwr(key)).loc;
          TTL *tl=TTLPN, *t1=TTLPN;
          stlen = 80;
          dbfh = btgetnext(bt, &stlen, s).loc;
          if(!strncmp(key, s, strlen(key)))
          {
                tl = getdbfttl(bdbf, dbfh);
                if(tl==TTLPN) printf("TTL GET ERROR! %s\n",key);
          }
          while(!strncmp(key, s, strlen(key)))
          {
                stlen = 80;
                dbfh = btgetnext(bt, &stlen, s).loc;
                if(strncmp(key, s, strlen(key)) || dbfh == -1)
                        break;
                t1 = getdbfttl(bdbf, dbfh);
                if(t1==TTLPN) printf("TTL GET ERROR! %s\n",key);
                tl = orttl(tl, t1);
                rewindttl(tl);
          }
          if(tl==TTLPN) printf("TTL GET ERROR! %s\n",key);
          else
              {
               tin_t n;
               printf("%s",key);
               while(getttl(tl,&n))
                   {
                    if(!counts)
                        printf(",%ld",n);
                    ++cnt;
                   }
               if(counts) printf("\t%ld",cnt);
               putchar('\n');
               closettl(tl);
              }
         }
    }
}

/************************************************************************/

int
main(argc,argv)
int argc;
char *argv[];
{
 int i;
 tin_t handle=0;
 int nread;
 long total=0L;
 char fname[255];
 TTBL *tt;

 for(i=1;i<argc && *argv[i]=='-';i++)
    {
     switch(*(++argv[i]))
         {
          case  'd' : dumpttbl("tmptin");exit(1);
          case  'c' : counts=1;break;
          default   : printf("%c invalid\n",*argv[i]);
          case  'h' : printf("USE %s your name here\n",argv[0]);
                      exit(1);
         }
    }

 rmmdat("tmptin");
 if((tt=openttbl("tmptin",expressions))!=TTBLPN)
    {
     tinnoise(tt->ti,xtnoiselst);
     while(gets(fname)!=NULL)   /* get the fname from the file */
         {
          int fh=open(fname,IMODE);
          if(fh>0)
              {
               fprintf(stderr,"%s\t",fname);
               while((nread=read(fh,(void *)rdbuf,RDBUFSZ))>0)
                   {
                    fprintf(stderr,"%11ld %11ld\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b",total+=nread, handle);
                    if(addttbl(tt,rdbuf,rdbuf+nread,++handle)<0L)
                       {
                        fputs("put error\n",stderr);
                        exit(0);
                       }
                   }
               fprintf(stderr,"\n");
               close(fh);
              }
         }
     fputs("merging...",stderr);
     puts(putttbl(tt) ? "OK" : "FAILED");
     closettbl(tt);
    }
 exit(0);
}

#endif /* NOT TESTA */

#endif /* test */
/************************************************************************/
#ifdef TTLTEST
/************************************************************************/
error(s)
char *s;
{
 fputs("ERROR: ",stderr);
 fputs(s,stderr);
 fputs("\n",stderr);
 exit(0);
}
/************************************************************************/

main()
{
 DBF *df=opendbf(NULL);
 TTL *tl;
 int i,j,k;
 for(i=0;i<100;i++)
    {
     tin_t base=1;
     tl=openttl();
     if(tl==TTLPN) error("openttl");

     for(j=0;j<100;j++)
         {
          base+=rand()%10;
          base++;
          if(!putttl(tl,base)) error("putttl");
          printf("%ld,",base);
         }
     putchar('\n');
     if(putdbfttl(df,-1L,tl)<0L) error("putdbfttl");
     tl=closettl(tl);
    }
for(tl=getdbfttl(df,0L);tl!=TTLPN;tl=getdbfttl(df,-1L))
    {
     tin_t n;
     while(getttl(tl,&n))
          printf(",%ld",n);
     putchar('\n');
     tl=closettl(tl);
    }
}

/************************************************************************/
#endif /* TTLTEST */
