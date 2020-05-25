#include "txcoreconfig.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#ifdef _WIN32
#  include "io.h"
#endif
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "limits.h"
#include "sys/types.h"
#include "dbquery.h"
#include "dirio.h"                                    /* for PATH_SEP */

/**********************************************************************/
char *
tempfn(dir,pfx)/* MAW 06-09-94 - like tempnam() but ignore TMP env var if dir set */
char *dir, *pfx;
{
int l;
char *fn, *sep, *num;
unsigned long cnt;
static char fmt[]="%s%s%.5sZZZ.ZZZ";

   num= &fmt[8];        /* first of 3 char alphabetic sequence string */
   if(dir==(char *)NULL){
      dir=getenv("TMP");
      if(dir==(char *)NULL){
#        ifdef MSDOS
            dir="c:";
#        else
            dir="/tmp";
#        endif
      }
   }
   if(*dir=='\0') dir=".";
   if(dir[strlen(dir)-1]==PATH_SEP) sep="";
   else                             sep=PATH_SEP_S;
   if(pfx==(char *)NULL) pfx="";
   l=strlen(dir)+1+12+1;                      /* "dir/uuuuuSSS.$$$\0" */
   if((fn=malloc(l))!=(char *)NULL){
      cnt=0;
      do{
                              /* increment sequence string w/rollover */
       /* there must be a better way to do this, but i stole the code */
         if(num[0]!='Z') num[0]++;
         else{
            num[0]='A';
            if(num[1]!='Z') num[1]++;
            else{
               num[1]='A';
               if(num[2]!='Z') num[2]++;
               else{
                  num[2]='A';
                  if(num[4]!='Z') num[4]++;
                  else{
                     num[4]='A';
                     if(num[5]!='Z') num[5]++;
                     else{
                        num[5]='A';
                        if(num[6]!='Z') num[6]++;
                        else{
                           num[6]='A';
                        }
                     }
                  }
               }
            }
         }
         sprintf(fn,fmt,dir,sep,pfx);
      }while(access(fn,0)==0 && ++cnt<308915776L);/* make sure not exist */
                                                              /* 26^6 */
   }
   return(fn);
}                                                     /* end tempfn() */
/**********************************************************************/

