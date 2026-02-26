#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include "os.h"
#include "txtypes.h"
#ifdef EPI_HAVE_STDARG
#  include <stdarg.h>
#else
#  include <varargs.h>
#endif
#define MMSG_C
#include "mmsg.h"

#ifndef CPNULL
#  define CPNULL (char *)NULL
#endif
#ifndef FPNULL
#  define FPNULL (FILE *)NULL
#endif

#ifdef MVS
#  define AMODE "w"
#  define EOLSTR "\n"
#  define samename(a,b) (strcmpi(a,b)==0)
#else
#  if defined(MSDOS)
#     define AMODE  "rb+"     /* "ab+" is rather inefficient on MSDOS */
#     define AMODE2 "wb"
#     define EOLSTR "\r\n"
#     define samename(a,b) (strcmpi(a,b)==0)
#  else
#     ifdef unix
#        define AMODE "a+"
#        define EOLSTR "\n"
#        define samename(a,b) (strcmp(a,b)==0)
#     else
#        ifdef macintosh
#           define AMODE "a+"
#           define EOLSTR "\n"
#           define samename(a,b) (strcmpi(a,b)==0)
#        else
            stop.                                   /* unknown system */
#        endif
#     endif
#  endif
#endif
#define MMSGFNAMESZ 128

FILE *mmsgfh=FPNULL;
char *mmsgfname=CPNULL;
static int shownum=1, showfunc=1, enabled=1, datamsgok=0;
/**********************************************************************/
void
msgconfig(sn,sf,en)
int sn, sf, en;
{
   if(sn>=0) shownum =sn;
   if(sf>=0) showfunc=sf;
   if(en>=0) enabled =en;
}                                                  /* end msgconfig() */
/**********************************************************************/

/**********************************************************************/
/* MAW 01-07-91 - use default buffering,
**                DOS: open "rb+" and seek to EOF
**                DOS: if "rb+" fails try "wb" to create file if not exist
*/
void
fixmmsgfh()
{
static char mmsgoldname[MMSGFNAMESZ]="";
static char Fn[]="fixmmsgfh";

   if(!enabled) return;
   if(mmsgfname!=CPNULL &&
      (mmsgfh==FPNULL || !samename(mmsgfname,mmsgoldname))
   ){
      datamsgok=0;
      if(mmsgfh!=FPNULL && mmsgfh!=stderr) fclose(mmsgfh);
      if((mmsgfh=fopen(mmsgfname,AMODE))==FPNULL
#if defined(MSDOS)  /* MAW 01-08-91 - try creating it */
         && (mmsgfh=fopen(mmsgfname,AMODE2))==FPNULL
#endif
      ){
         char *triedmmsgfname = mmsgfname; /* Remember for message */

         mmsgoldname[0]='\0';
         mmsgfname="";                 /* prevent constant reopens */
         mmsgfh=stderr;
         putmsg(MWARN+FOE,Fn,"can't open message file \"%s\": %s",
                triedmmsgfname,sysmsg(errno));
      }else{
         strncpy(mmsgoldname,mmsgfname,MMSGFNAMESZ);
         mmsgoldname[MMSGFNAMESZ-1]='\0';
#        if defined(MSDOS)
            fseek(mmsgfh,0L,SEEK_END);/* really opened rb+, so get to EOF */
#        endif
      }
   }
   if(mmsgfh==FPNULL) mmsgfh=stderr;
}                                                  /* end fixmmsgfh() */
/**********************************************************************/

/**********************************************************************/
void
closemmsg()
{
   if(mmsgfh!=FPNULL && mmsgfh!=stderr) fclose(mmsgfh);
   mmsgfh=FPNULL;
}                                                  /* end closemmsg() */
/**********************************************************************/

/***********************************************************************
** args: msgn,fn,fmt,printf args for fmt
**       to supress msgn: pass -1
**                  fn  : pass CPNULL
**                  fmt : pass CPNULL
**       to get just EOL: supress all of the above
**       suppress EOL   : set msgn=(-1) or set fmt=CPNULL
***********************************************************************/
EPIPUTMSG()
{
   TXbool       inSig = TXbool_False;

   if (n >= TX_PUTMSG_NUM_IN_SIGNAL)
     {
       n -= TX_PUTMSG_NUM_IN_SIGNAL;
       inSig = TXbool_True;
     }

   datamsgok=0;
   if(!enabled) return(0);
   fixmmsgfh();
   if(n<0 && fmt==CPNULL && fn==CPNULL){
      fputs(EOLSTR,mmsgfh);
      fflush(mmsgfh);/* MAW 01-08-91 - make sure all data avail for ui */
   }else{
      if(shownum && n>=0) fprintf(mmsgfh,"%03d ",n);
      if(fmt!=CPNULL)
#ifdef __TSC__            /* topspeed C 3.01 vfprintf() does not work */
      {
      char buf[256];
         vsprintf(buf,fmt,args);
         fputs(buf,stderr);
      }
#else
         vfprintf(mmsgfh,fmt,args);      /* print the message content */
#endif
      if(showfunc && fn!=CPNULL) fprintf(mmsgfh," in the function: %s",fn);
      if(n>=0 && fmt!=CPNULL){
         fputs(EOLSTR,mmsgfh);
         fflush(mmsgfh);/* MAW 01-08-91 - make sure all data avail for ui */
      }
   }
   va_end(args);
   return(ferror(mmsgfh)?-1:0);
}}                                                     /* end putmsg() */
/**********************************************************************/

/**********************************************************************/
void
okdmsg()
{
   datamsgok=1;
}                                                     /* end okdmsg() */
/**********************************************************************/

/**********************************************************************/
int
datamsg(eol,buf,sz,cnt)
int eol;
char *buf;
int sz;
int cnt;
{
   if(!enabled) return(0);
   if(datamsgok) fixmmsgfh(),datamsgok=0;
   else          putmsg(MDATA,CPNULL,"stdin 0 %ld",(long)sz*(long)cnt);
   fwrite(buf,sz,cnt,mmsgfh);
   if(eol){
      fputs(EOLSTR,mmsgfh);
      fflush(mmsgfh);/* MAW 01-08-91 - make sure all data avail for ui */
   }
   return(ferror(mmsgfh)?-1:0);
}                                                    /* end datamsg() */
/**********************************************************************/
