/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include "os.h"
/* #include "mmsg.h"
*/
#include "dbquery.h"
#include "texint.h"
#include "ramdbf.h"


#ifndef NO_DBF_IOCTL
static size_t	ramblocklimit = 10000;
static size_t	ramsizelimit = 0;
#endif

char    TxRamDbfName[] = "(temp RAM DBF)";

/************************************************************************/

int
TXsetramblocks(n)
int n;
{
	int rc;

	rc = ramblocklimit;
	ramblocklimit = n;
	return rc;
}

/******************************************************************/

int
TXsetramsize(n)
int n;
{
	int rc;

	rc = ramsizelimit;
	ramsizelimit = n;
	return rc;
}

/******************************************************************/

RDBF *
closerdbf(df)
RDBF *df;
{
 RDBFB *rp,*next;
 if(df!=RDBFPN)
    {
     for(rp=df->base;rp!=RDBFBPN;rp=next)
         {
          next=rp->next;
	  if(next == rp)
	  	break;
          rp = TXfree(rp);
         }
     df->name = TXfree(df->name);
     df = TXfree(df);
    }
 return(RDBFPN);
}

/************************************************************************/

static RDBFB *TXramdbfNewbuf ARGS((size_t sz));

static RDBFB *
TXramdbfNewbuf(sz)
size_t sz;
{
  static CONST char     fn[] = "TXramdbfNewbuf";
 RDBFB *rb;

 rb=(RDBFB *)TXmalloc(TXPMBUFPN, fn, sizeof(RDBFB)+sz);
 if(!rb)
     return((RDBFB *)NULL);
 rb->sz=sz;

#if EPI_OFF_T_BITS < EPI_OS_VOIDPTR_BITS
 /* EPI_OFF_T is smaller than void *: make sure the pointer value
  * fits into an EPI_OFF_T; probably not, but give it a chance.
  * Note that we will allow negative EPI_OFF_T values -- even
  * though they are not legal "file offsets" -- because if
  * EPI_OFF_T were the same size as void * (as is often the case)
  * we allow them too, as pointers are often "negative":
  */
 if ((EPI_VOIDPTR_UINT)rb &
     ~(((EPI_VOIDPTR_UINT)1 << EPI_OFF_T_BITS) - (EPI_VOIDPTR_UINT)1))
   {
     putmsg(MERR + MAE, fn,
            "Cannot use new RDBF block %p: Out of EPI_OFF_T range", rb);
     rb = TXfree(rb);
   }
#endif /* EPI_OFF_T_BITS < EPI_OS_VOIDPTR_BITS */

 return(rb);
}

/************************************************************************/

RDBF *
openrdbf()
{
  static CONST char     fn[] = "openrdbf";

  RDBF *df=(RDBF *)TXcalloc(TXPMBUFPN, fn, 1,sizeof(RDBF));
 if(df!=RDBFPN)
    {
     if((df->base=TXramdbfNewbuf((size_t)0))==RDBFBPN)
         return(closerdbf(df));
     df->end=df->base;
     df->base->next=df->base->prev=RDBFBPN;
     df->current=df->base->next;
     df->ramsizelimit =ramsizelimit;
     df->ramblocklimit =ramblocklimit;
    }
 return(df);
}

/************************************************************************/

int
freerdbf(df,at)
RDBF *df;
EPI_OFF_T at;
{
 RDBFB *rb;

 if(at==0) rb=df->current=df->base->next;
 /* EPI_VOIDPTR_INT cast avoids warning; range checked in TXramdbfNewbuf(): */
 else      rb=(RDBFB *)(EPI_VOIDPTR_INT)at;
 if(rb->prev!=RDBFBPN)
     rb->prev->next=rb->next;
 if(rb->next!=RDBFBPN)
     rb->next->prev=rb->prev;
 df->current=rb->next;
/* JMT 4/27/94, Make sure we keep end pointer up to date */
 if(rb==df->end)
 	df->end=rb->prev;
 df->nblocks --;
 df->size -= rb->sz;
 rb = TXfree(rb);
 return(1);
}

/************************************************************************/

EPI_OFF_T
rdbfalloc(df,buf,n)
RDBF  *df;
void  *buf;
size_t n;
{
 RDBFB *rb=TXramdbfNewbuf(n);
 void  *bp;
 if(rb!=RDBFBPN)
    {
                                        /* point to memory after struct */
     df->nblocks++;
     df->size += n;
     bp=(void *)((char *)rb+sizeof(RDBFB));
     if(n)
     	memcpy(bp,buf,n);    /* copy data into the region after the struct */
     rb->next=RDBFBPN;       /* link this node into the end of the list */
     rb->prev=df->end;
     df->end->next=rb;
     df->end=df->current=rb;
#ifndef NO_DBF_IOCTL
     if(df->dfover)
     {
	     DBGMSG(1, (999, NULL, "NBLOCKS = %d", df->nblocks));
     }
     if((df->ramblocklimit && df->nblocks > df->ramblocklimit) ||
        (df->ramsizelimit && df->size > df->ramsizelimit))
     {
     	df->overlimit = 1;
     	if(df->dfover)
	{
		return ioctldbf(df->dfover, DBF_MAKE_FILE, rb);
  /* EPI_VOIDPTR_INT cast avoids warning; range checked in TXramdbfNewbuf(): */
		return (EPI_OFF_T)(EPI_VOIDPTR_INT)rb;
	}
	else
	{
	     DBGMSG(1, (999, NULL, "NBLOCKS = %d", df->nblocks));
	}		
     }
     else
     	df->overlimit = 0;
#endif
    }
 if(rb==RDBFBPN)/* Failed alloc is always an error *//* JMT 2000-05-25 */
 	return(-1);
 else if(rb==df->base->next)
 	return(0);
 else
 /* EPI_VOIDPTR_INT cast avoids warning; range checked in TXramdbfNewbuf(): */
        return((EPI_OFF_T)(EPI_VOIDPTR_INT)rb);
}

/************************************************************************/

static EPI_OFF_T rdbftalloc ARGS((RDBF*, void *, size_t));

static EPI_OFF_T
rdbftalloc(df,buf,n)
RDBF  *df;
void  *buf;
size_t n;
{
 RDBFB *rb=TXramdbfNewbuf(n);
 void  *bp;
 if(rb!=RDBFBPN)
    {
                                        /* point to memory after struct */
     df->nblocks++;
     df->size += n;
     bp=(void *)((char *)rb+sizeof(RDBFB));
     memcpy(bp,buf,n);    /* copy data into the region after the struct */
     rb->prev=df->base;      /* link this node into the head of the list */
     rb->next=df->base->next;
     if(rb->next)
     	rb->next->prev = rb;
     df->base->next=rb;
     df->current=rb;
    }
 if(rb==df->base->next) return(0);
 else if(rb==RDBFBPN  ) return(-1);
 /* EPI_VOIDPTR_INT cast avoids warning; range checked in TXramdbfNewbuf(): */
 else                   return((EPI_OFF_T)(EPI_VOIDPTR_INT)rb);
}

/************************************************************************/

EPI_OFF_T              /* supposedly alters the contents of an existing DBF */
putrdbf(df,at,buf,sz)
RDBF  *df;
EPI_OFF_T  at;
void  *buf;
size_t sz;
{
  if (at != (EPI_OFF_T)(-1))
 {
 /* EPI_VOIDPTR_INT cast avoids warning; range checked in TXramdbfNewbuf(): */
   RDBFB	*rdbfb = (RDBFB *)(EPI_VOIDPTR_INT)at;

    if(at == 0)
    	rdbfb = df->base->next;
    if(rdbfb->sz == sz)
    {
    	void *bp = (void *)((char *)rdbfb + sizeof(RDBFB));
    	memcpy(bp, buf, sz);
	df->current = rdbfb;
	return at;
    }
    else
    {
	    RDBFB *n, *p, *newrb;
	    void *bp;

	    n = rdbfb->next;
	    p = rdbfb->prev;
	    freerdbf(df,at);
	    newrb = TXramdbfNewbuf(sz);
            if (!newrb) return((EPI_OFF_T)(-1));
	    df->nblocks++;
	    df->size += sz;
            bp = (void *)((char *)newrb + sizeof(RDBFB));
            memcpy(bp, buf, sz);
	    newrb->next = n;
	    newrb->prev = p;
	    if(n) n->prev=newrb;
	    else
	    {
	    	df->end = newrb;
	    }
	    if(p) p->next=newrb;
 /* EPI_VOIDPTR_INT cast avoids warning; range checked in TXramdbfNewbuf(): */
	    return((EPI_OFF_T)(EPI_VOIDPTR_INT)newrb);
    }
 }
#ifndef NEVER /* Make sure that 0 ends up at front */
 if(at)
 {
         return rdbfalloc(df,buf,sz);
 }
 else
 {
	 return(rdbftalloc(df,buf,sz));
 }
#else
 return(rdbfalloc(df,buf,sz));
#endif
}

/************************************************************************/

int
validrdbf(df, at)
RDBF	*df;
EPI_OFF_T	at;
{
	(void)df;
	if(at)
		return 1;
	else
		return 0;
}

/************************************************************************/

/* get a ram block from the rdbf: returns a pointer to the memory
*  that contains the block. It is not allocated, so you have to make a copy
*  of it if you want to keep it.
*/

void *
getrdbf(df,at,psz)
RDBF   *df;
EPI_OFF_T   at;
size_t *psz;
{
 RDBFB *rb;
 if(at==-1L)
    {
     if(df->current!=RDBFBPN)
        df->current=df->current->next;
     rb=df->current;
    }
 else if(at==0) rb=df->current=df->base->next;
 /* EPI_VOIDPTR_INT cast avoids warning; range checked in TXramdbfNewbuf(): */
 else rb=df->current=(RDBFB *)(EPI_VOIDPTR_INT)at;	/* JMT 06/07/94, update current */
 if(rb!=RDBFBPN)
    {
     *psz=rb->sz;
     return((void *)((char *)rb+sizeof(RDBFB)));
    }
 *psz= 0;
 return((void *)NULL);
}

/************************************************************************/

/* allocating version of the above function ( this means that you own the
data block, and are required to free it */

void *
agetrdbf(df,at,psz)
RDBF   *df;
EPI_OFF_T   at;
size_t *psz;
{
  static CONST char     fn[] = "agetrdbf";
 void *vp=getrdbf(df,at,psz);
 void *new=(void *)NULL;
 if(vp!=(void *)NULL)
    {
      new=TXmalloc(TXPMBUFPN, fn, *psz + 1);    /* Bug 4030: nul-terminate */
     if(new!=(void *)NULL)
       {
         memcpy(new,vp,*psz);
         ((char *)new)[*psz] = '\0';            /* KNG nul-term. like KDBF */
       }
    }
 return(new);
}

/************************************************************************/

/* reads in the info from a RDBF directly into the user's buffer.  This is
NOT really intended for general use, but saves time when you know what
you're doing.  */

size_t
readrdbf(df,at,off,buf,sz)
RDBF  *df;
EPI_OFF_T  at;
size_t *off;    /* KNG 980226 used in KDBF; wtf here for compatibility */
void  *buf;
size_t sz;
{
 RDBFB *rb;
 void *vp;

 (void)off;
 if(at==0) rb=df->current=df->base->next;
 /* EPI_VOIDPTR_INT cast avoids warning; range checked in TXramdbfNewbuf(): */
 else      rb=(RDBFB *)(EPI_VOIDPTR_INT)at;
 vp=(void *)((char *)rb+sizeof(RDBFB));
 if (sz > rb->sz) sz = rb->sz;
 memcpy(buf,vp,sz);
 df->current=rb;
 return(sz);    /* KNG 980226 now returns overal block size (off == NULL) */
}

/************************************************************************/

EPI_OFF_T
tellrdbf(df)
RDBF *df;
{
 if(df->current==df->base->next) return(0);
 /* EPI_VOIDPTR_INT cast avoids warning; range checked in TXramdbfNewbuf(): */
 else return((EPI_OFF_T)(EPI_VOIDPTR_INT)df->current);
}

/************************************************************************/

char *
getrdbffn(df)
RDBF *df;
{
 return(df->name ? df->name : TxRamDbfName);/* return something semi-useful */
}

/************************************************************************/

int
getrdbffh(df)
RDBF *df;
{
 (void)df;
 return(-1);
}

/************************************************************************/

void                                                   /* means nothing */
setrdbfoveralloc(df,n)
RDBF *df;
int n;
{
 (void)df;
 (void)n;
}

/************************************************************************/

#ifndef NO_DBF_IOCTL
int
ioctlrdbf(df, ioctl, data)
RDBF	*df;
int	ioctl;
void	*data;
{
	char	*newName = NULL;

	if((ioctl & 0xFFFF0000) != DBF_RAM)
		return -1;
	switch(ioctl)
	{
		case RDBF_SETOVER:
			df->dfover = data;
			return 0;
		case RDBF_TOOBIG:
			return df->overlimit;
		case RDBF_BLCK_LIMIT:
			df->ramblocklimit = (size_t)(EPI_VOIDPTR_INT)data;
			return 0;
		case RDBF_SIZE_LIMIT:
			df->ramsizelimit = (size_t)(EPI_VOIDPTR_INT)data;
		case RDBF_SIZE:
			return df->size;
		case RDBF_SET_NAME:
			if (data)
			{
				newName = TXstrdup(TXPMBUFPN, __FUNCTION__,
						   (char *)data);
				if (!newName) return(-1);
			}
			else
				newName = NULL;
			df->name = TXfree(df->name);
			df->name = newName;
			newName = NULL;
			return(0);		/* success */
		default:
			return -1;
	}
}
#endif

/******************************************************************/

#ifdef TEST

#define LNSZ 512
char ln[LNSZ];

int
main(argc,argv)
int argc;
char *argv[];
{
 int i;
 RDBF *df;
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
 if((df=openrdbf())!=RDBFPN)
    {
     if(load)
         {
          while(
                gets(ln) &&
                putrdbf(df,-1L,(void *)ln,strlen(ln)+1)>=0L
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
               case 'p' : printf("%08lX\n",putrdbf(df,-1L,(void *)ln,strlen(ln)+1));break;
               case 'g' : puts(getrdbf(df,strtol(ln+1,&dummy,16),&len));break;
               case 'f' : puts(freerdbf(df,strtol(ln+1,&dummy,16)) ? "ok" : "fail");break;
               case 'd' : for(o=0L;(dummy=getrdbf(df,o,&len))!=NULL;o=-1L)
                             printf("\"%s\"\n",dummy);
                          break;
               case 'q' : goto END;
              }
         }
     END:
     closerdbf(df);
    }
 exit(0);
}

#endif /* test */
/************************************************************************/

