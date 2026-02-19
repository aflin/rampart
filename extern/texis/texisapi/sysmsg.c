#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#if defined(MSDOS) || defined(__MINGW32__)
#  include <stdlib.h>
#else
#if !defined(__FreeBSD__) && !defined(__APPLE__) && !defined(__CYGWIN__) && __GNU_LIBRARY__ < 2
   extern int  sys_nerr;
   extern char *sys_errlist[];
#endif
#endif
#include <errno.h>
#include "os.h"

#if defined(HAVE_STRERROR)
/**********************************************************************/
const char *sysmsg(e)            /* could be macro, but not for consistency */
int e;
{
   return(strerror(e));
}                                                     /* end sysmsg() */
/**********************************************************************/
#else
/**********************************************************************/
const char *sysmsg(e)
int e;                                                   /* errno val */
{
const char *rc;
static char s[]="Unknown system error ____";
/*               0123456789012345678901234                            */

   if(e>sys_nerr || e<0){
      if(e>9999) s[21]='\0';
      else       sprintf(&s[21],"%4d",e);
      rc=s;
   }else if(e==ENOMEM){
      rc="Out of memory";
   }else{
      rc=sys_errlist[e];
   }
   return(rc);
}                                                     /* end sysmsg() */
/**********************************************************************/
#endif                                               /* HAVE_STRERROR */
