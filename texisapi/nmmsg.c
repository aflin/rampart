#include "txcoreconfig.h"
#include "stdio.h"
#include "sys/types.h"
#include "stdlib.h"
#include "string.h"
#define MMSG_C
#include "api3.h"
#ifdef EPI_HAVE_STDARG
#  include "stdarg.h"
#else
#  include "varargs.h"
#endif
#include "txtypes.h"
#include "cgi.h"
#ifdef NCGBESERVER
#include "fio.h"
FIO *mmsgfio=FIOPN;
#endif
extern int epilocmsg ARGS((int f));

#ifdef unix
#  define samename(a,b) (strcmp(a,b)==0)
#else
#  define samename(a,b) (strcmpi(a,b)==0)
#endif
#define MMSGFNAMESZ 256
#define NOFILE (FILE *)NULL
#define DEFFILE stderr
FILE *mmsgfh=NOFILE;
char *mmsgfname=(char *)NULL;

/**********************************************************************/
#ifdef _WIN32

static int (*umfunc)(int,char *,char *,va_list)=(int (*)(int,char *,char *,va_list))NULL;

char *
setmmsgfname(nfn)
char *nfn;
{
	return(mmsgfname = nfn);
}

/******************************************************************/

void
setmmsg(func)
int (*func)(int,char *,char *,va_list);
{
   umfunc=func;
}                                                    /* end setmmsg() */
#endif
/**********************************************************************/
void FAR
fixmmsgfh()
{
static char mmsgoldname[MMSGFNAMESZ]="";
static char Fn[]="fixmmsgfh";

   if(mmsgfname!=(char *)NULL &&
      (mmsgfh==NOFILE || !samename(mmsgfname,mmsgoldname))
   ){
      if(mmsgfh!=NOFILE && mmsgfh!=DEFFILE) fclose(mmsgfh);
      if(*mmsgfname=='\0' || strcmp(mmsgfname,"-")==0){/* MAW 03-15-95 */
         mmsgfh=DEFFILE;
      }else{
         mmsgfh=fopen(mmsgfname,"a");
      }
      if(mmsgfh==NOFILE){
      int o=epilocmsg(1);
         mmsgoldname[0]='\0';
         mmsgfname="";                    /* prevent constant reopens */
         mmsgfh=DEFFILE;
         putmsg(MWARN+FOE,Fn,"can't open message file \"%s\"",mmsgfname);
         epilocmsg(o);
      }else{
         strncpy(mmsgoldname,mmsgfname,MMSGFNAMESZ);
         mmsgoldname[MMSGFNAMESZ-1]='\0';
      }
   }
   if(mmsgfh==NOFILE) mmsgfh=DEFFILE;
}                                                  /* end fixmmsgfh() */
/**********************************************************************/

/**********************************************************************/
void FAR
closemmsg()
{
   if(mmsgfh!=NOFILE && mmsgfh!=DEFFILE) fclose(mmsgfh);
   mmsgfh=NOFILE;
}                                                  /* end closemmsg() */
/**********************************************************************/

/***********************************************************************
** args: msgn,fn,fmt,printf args for fmt
**       to supress msgn: pass -1
**                  fn  : pass (char *)NULL
**                  fmt : pass (char *)NULL
***********************************************************************/
#define MSGBUFSZ 8192

EPIPUTMSG()
{
  TXbool        inSig;

  if (n >= TX_PUTMSG_NUM_IN_SIGNAL)
    {
      n -= TX_PUTMSG_NUM_IN_SIGNAL;
      inSig = TXbool_True;
    }
  else
    inSig = TXbool_False;

#ifdef NCGBESERVER
   if(!epilocmsg(-1)){
      char *msgp, *msgbufEnd;
      char msgbuf[MSGBUFSZ];

      msgbufEnd = msgbuf + sizeof(msgbuf);
      if(mmsgfio==FIOPN) return(-1);
      msgp=msgbuf;
      if(n>=0){
         sprintf(msgp,"!%03d ",n);
         msgp+=5;
      }
      if(fmt!=(char *)NULL){
        if (msgp < msgbufEnd)
          msgp += htvsnpf(msgp, msgbufEnd, fmt, (HTPFF)0, NULL, NULL, args,
                          NULL, NULL, TXPMBUFPN);
      }
      if(fn!=(char *)NULL && msgp < msgbufEnd){
         sprintf(msgp," in the function: %s",fn);
         msgp+=strlen(msgp);
      }
      fiputs(mmsgfio,msgbuf);
   }else
#endif
   {
#ifdef _WIN32
      if(umfunc!=(int (*)(int,char *,char *,va_list))NULL){
         int rc;
         rc=(*umfunc)(n, (char *)fn, (char *)fmt, args);
         va_end(args);
	 return rc;
      }
#endif
      /*if(mmsgfh==(FILE *)NULL) mmsgfh=stderr;*/
      fixmmsgfh();
      if(n>=0){
         fprintf(mmsgfh,"%03d ",n);
      }
      if(fmt!=(char *)NULL){
        htvfpf(mmsgfh, fmt, args);      /* print the message content */
      }
      if(fn!=(char *)NULL){
         fprintf(mmsgfh," in the function: %s",fn);
      }
      fprintf(mmsgfh,"\n");
      fflush(mmsgfh);                                 /* MAW 05-05-94 */
   }
   va_end(args);
   return(0);
}}                                                  /* end epiputmsg() */
/**********************************************************************/
