/* MAW 08-27-90 - converted fread() to read() for MSDOS */
/**********************************************************************/
#include "txcoreconfig.h"
#include "stdio.h"
#ifdef MVS
#  if MVS_C_REL > 1
#     include <stddef.h>
#  else
#     include <stdefs.h>
#  endif
#else
#  include "fcntl.h"
#endif
#include "string.h"
#include "errno.h"

#define FHTYPE FILE *
#define FHTYPEPN (FILE *)NULL
#ifdef _WIN32
#   define LOWIO
#include <sys/types.h>
#endif

#include "txcoreconfig.h"
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>           /* for lseek() KNG 981105 */
#endif
#include "sizes.h"
#include "os.h"
#include "pm.h"
#include "mmsg.h"

#ifndef CPNULL
#  define CPNULL (char *)NULL
#endif
#ifdef unix
#  define CHKPIPE        /* MAW 09-10-92 - check for pipe via lseek() */
#endif

#ifdef LINT_ARGS
static void strip8(byte *,int);
#else
static void strip8();
#endif

/*

 freadex() tries to read in "len" bytes:
    if the actual number read is less than "len"
         it returns "len"
    if the actual number read equals "len"
         it looks for the last occurance of the expression "ex"
              if no "ex"
                   it returns -1
              else
                   it returns the number of bytes up to and including "ex"

  The purpose of this is to guarantee no expression crosses
  a buffer boundry. (pretty cool eh?)

*/

/************************************************************************/

             /* this was added to Support Wordstar && PFS */

TXbool freadex_strip8 = TXbool_False;           /* PBR 8-29-90 */

static void
strip8(buf,n)                                 /* strips off the 8th bit */
register byte *buf;
int n;
{
 register byte *end;
 for(end=buf+n;buf<end;buf++)
    *buf&=0x7f;
}

/************************************************************************/

           /* this routine may only be used by one function at a time */

/* calling this routine with a NULL fh or reading to eof will reset it */

int                                                   /* n bytes read */
pipereadex(fh,buf,len,ex)
FHTYPE fh;
register byte *buf;
int  len;
FFS  *ex;
{
 static byte  *end;
 static int tailsz;
 static FHTYPE fhold=FHTYPEPN;
 static char Fn[]="pipereadex";


 char *loc;
 int nactr;                                   /* number actually read */
 int nr;

 if(fhold!=fh)
   {
    fhold=fh;
    tailsz=0;
   }
 else if(fh==FHTYPEPN)                                       /* reset */
   {
    tailsz=0;
    fhold=FHTYPEPN;
    return(0);
   }


 if(tailsz>0)
   {
    if(len>=tailsz)          /* remainder in buffer less than request */
         {
          memcpy((char *)buf,(char *)end,tailsz);/* move remainder from last read to begin of buf */
          len-=tailsz;               /* subtract amt moved from total */
         }
    else                  /* remainder in buffer greater than request */
         {
          memcpy((char *)buf,(char *)end,len);
          tailsz-=len;                         /* update the pointers */
          end+=len;
          return(len);            /* no read needed to satify request */
         }
   }
 else tailsz=0;                    /* MAW 03-29-90 - make sure not -1 */

/* MAW 04-15-93 - !@#$%^& - S5R4 will return 5k at most per read from a pipe */
/* MAW 08-03-93 - fixed pipe read, i messed it up the first time (04-15-94),
                  it overflowed the buffer on pipes, but not redirection */
    {
     int toread=len;
     char *p=(char *)buf+tailsz;

     do{ /* MAW 04-15-93  - when reading from a pipe, loop to fill buffer */
         errno = 0;
         nr=fread(p,sizeof(byte),toread,fh);/* do a read for the rest */
         if(nr<0)                                     /* MAW 08-03-93 */
              {
               putmsg(MWARN+FRE,Fn,"Can't read pipe: %s", strerror(errno));
               return(0);
              }
         toread-=nr;
         p+=nr;
     }
     while(toread>0 && nr>0);
     nactr=len-toread;
    }

#ifdef MVS
                            /* strip trailing newlines from ASA files */
 if(feof(fh) && nactr>0)
    {
     byte *p;
     for(p=buf+tailsz+nactr-1;p>=(buf+tailsz) && *p=='\n';p--,nactr--);
     if(p>=(buf+tailsz))                         /* keep last newline */
        {
         ++p;
         ++nactr;
        }
    }
#endif                                                         /* MVS */

 if(nactr==0 && tailsz==0)        /* end of file and no buffered data */
    return(0);
 else if(nactr<len)                  /* data read less than requested */
    {
     nactr+=tailsz;
     tailsz=0;
     if(freadex_strip8)  strip8(buf,nactr);           /* MAW 04-15-93 */
     return(nactr);
    }
 nactr+=tailsz;
 loc=(char *)getrex(ex,buf,buf+nactr,BSEARCHNEWBUF);
 if(loc==(char *)NULL)             /* no expression located in buffer */
   {
    tailsz=0;
    putmsg(MWARN,Fn,"no end delimiter located within buffer");/* MAW 04-15-93 */
    if(freadex_strip8)  strip8(buf,end-buf);          /* MAW 04-15-93 */
    return(nactr);
   }
 else
 if(loc==(char *)buf)                  /* MAW 10-17-95 - handle w/all */
   {
    loc=(char *)buf+nactr;
   }
 end=(byte *)loc+rexsize(ex);          /* make end point past the hit */
 tailsz=(buf+nactr)-end;                            /* set the tailsz */
 if(freadex_strip8)  strip8(buf,end-buf);                /* PBR 8-29-90 */
 return(end-buf);                     /* the number read ending on ex */
}

/************************************************************************/


/* this routine allows multiple routines to use freadex at the same time */

int                                                   /* n bytes read */
freadex(fh,buf,len,ex)
FHTYPE fh;
register byte *buf;
int  len;
FFS  *ex;
{
 register int nread,nactr;  /* val returned, number actually returned */
 byte *loc;
 static char Fn[]="freadex";

#if defined(MVS) || defined(VMS)  /* MAW 02-20-90 - no seeking on mvs */
 return(pipereadex(fh,buf,len,ex));
#else
#ifndef _WIN32 /* wtf? */
 if(fh==stdin)                                /* NO SEEKING IN A PIPE */
     return(pipereadex(fh,buf,len,ex));
#endif/* !_WIN32 */
#ifdef CHKPIPE                                        /* MAW 09-10-92 */
 if(lseek(fileno(fh),(off_t)0,SEEK_CUR)==(off_t)(-1) && errno==ESPIPE)
     return(pipereadex(fh,buf,len,ex));
#endif

 errno = 0;
 nactr=fread((char *)buf,sizeof(byte),len,fh);           /* do a read */
 if(nactr<0)
    {
     putmsg(MWARN+FRE,Fn,"Can't read file: %s", strerror(errno));
     return(0);
    }
 nread=nactr;                                          /* end of buffer */
 if(nread && nread==len)               /* read ok && as big as possible */
    {
     loc=getrex(ex,buf,buf+nread,BSEARCHNEWBUF);
     if(loc==BPNULL)                   /* no expression within buffer */
        {
         putmsg(MWARN,Fn,"no end delimiter located within buffer");
         if(freadex_strip8)  strip8(buf,nread);          /* PBR 8-29-90 */
         return(nread);
        }
     else
     if(loc==buf)                      /* MAW 10-17-95 - handle w/all */
        {
         loc=buf+nread;
        }
     nread=(int)(loc-buf)+rexsize(ex);            /* just beyond expr */
     errno = 0;
     if (FSEEKO(fh, (off_t)(nread - nactr), SEEK_CUR) == -1)
        {
         putmsg(MWARN+FSE,Fn,"Can't seek to realign buffer: %s",
                strerror(errno));
         return(0);
        }
     if(freadex_strip8)  strip8(buf,nread);              /* PBR 8-29-90 */
     return(nread);
    }
 if(freadex_strip8)  strip8(buf,nactr);                  /* PBR 8-29-90 */
 return(nactr);                   /* read was smaller || eof || error */
#endif                                                         /* MVS */
}

/************************************************************************/

#ifdef TEST

#define BUFSZ 300
byte buffer[BUFSZ];

main(argc,argv)
int argc;
char *argv[];
{
 FILE *ifh;
 FILE *ofh;
 FFS *ex=openrex(argv[1]);
 int nread;
 ifh=fopen(argv[2],"rb");
 ofh=fopen(argv[3],"wb");

 while((nread=freadex(ifh,buffer,BUFSZ,ex))>0)
     fwrite(buffer,nread,sizeof(byte),ofh);
 fclose(ifh);
 fclose(ofh);
}

/************************************************************************/
#endif
