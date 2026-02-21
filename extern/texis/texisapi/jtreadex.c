/*
 * $Log$
 * Revision 1.4  2000/07/27 21:28:22  john
 * Add config.h
 *
 * Revision 1.3  1999-12-21 12:02:07-05  john
 * Edit Fn to reflect function name.
 *
 */

/* MAW 08-27-90 - converted fread() to read() for MSDOS */
/**********************************************************************/
#ifdef mvs			/* MAW 03-28-90 - must be first thing */
#  include "mvsfix.h"
#endif
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
#if defined(MSDOS)
#  define LOWIO
#endif
#include "string.h"
#include "sys/types.h"
#include "sizes.h"
#include "os.h"
#include "pm.h"
#include "mmsg.h"
#include "jtreadex.h"
#ifndef CPNULL
#  define CPNULL (char *)NULL
#endif
#ifdef unix
#	define CHKPIPE        /* MAW 09-10-92 - check for pipe via lseek() */
#  include "errno.h"
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
filereadex(frx)
FREADX	*frx;
{
 byte	*end = frx->end;
 int	len = frx->len;
 int	tailsz = frx->tailsz;
 FILE	*fh = frx->fh;
 register byte *buf = frx->buf;
 FFS	*ex = frx->ex;
 static char Fn[]="filereadex";


 char *loc;
 int nactr;                                   /* number actually read */
 int nr;

 if(fh==(FILE *)NULL)                                   /* reset */
   {
    frx->tailsz = 0;
    frx->end = frx->buf;
    frx->fh = fh;
    return(0);
   }


 if(tailsz>0)
   {
    if(len>=tailsz)          /* remainder in buffer less than request */
         {
	             /* move remainder from last read to begin of buf */
          memmove((char *)buf,(char *)end,tailsz);
          len-=tailsz;               /* subtract amt moved from total */
         }
    else                  /* remainder in buffer greater than request */
         {
          memmove((char *)buf,(char *)end,len);
          tailsz-=len;                         /* update the pointers */
          end+=len;
	  frx->end = end;
	  frx->tailsz = tailsz;
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
         nr=fread(p,sizeof(byte),toread,fh);/* do a read for the rest */
         if(nr<0)                                     /* MAW 08-03-93 */
              {
               putmsg(MWARN+FRE,Fn,"can't read pipe");
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
     frx->end = end;
     frx->tailsz = tailsz;
     return(nactr);
    }
 nactr+=tailsz;
 loc=(char *)getrex(ex,buf,buf+nactr,BSEARCHNEWBUF);
 if(loc==(char *)NULL)             /* no expression located in buffer */
   {
    tailsz=0;
    putmsg(MWARN,Fn,"no end delimiter located within buffer");/* MAW 04-15-93 */
    if(freadex_strip8)  strip8(buf,end-buf);          /* MAW 04-15-93 */
    frx->tailsz = tailsz;
    frx->end = end;
    return(nactr);
   }
 end=(byte *)loc+rexsize(ex);          /* make end point past the hit */
 tailsz=(buf+nactr)-end;                            /* set the tailsz */
 if(freadex_strip8)  strip8(buf,end-buf);                /* PBR 8-29-90 */
 frx->tailsz = tailsz;
 frx->end = end;
 return(end-buf);                     /* the number read ending on ex */
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

