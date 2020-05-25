/*#define DEBUG 1*/
/*#define TESTXPM  1*/
/**********************************************************************/
#include "txcoreconfig.h"
#include "stdio.h"
#include "sys/types.h"
#include <stdlib.h>
#ifdef MVS
#  if MVS_C_REL > 1
#     include <stddef.h>
#  else
#     include <stdefs.h>
#  endif
#endif
#include "ctype.h"
#include "string.h"
#include "sizes.h"
#include "os.h"
#include "pm.h"


/************************************************************************/

XPMS *
closexpm(xs)
XPMS *xs;
{
 int i;
 if(xs)
    {
     for(i=0;xs->xa[i];i++)
         free(xs->xa[i]);
     free((char *)xs);
    }
  return((XPMS *)NULL);
}

/************************************************************************/

XPMS *
openxpm(s,threshold)
char *s;
int threshold; /* expressed as a percentage */
{
 XPMS *xs=(XPMS *)calloc(1,sizeof(XPMS));
 register int i,j,ps;

 if(xs==(XPMS *)NULL)
    return(xs);

 ps=strlen(s);
 if((xs->patsize=(byte)ps)==0)            /* zero length string */
    return(closexpm(xs));

 for(i=0;                                  /* allocate memory for table */
     i<ps
     && (xs->xa[i]=(byte *)calloc(DYNABYTE,sizeof(byte)))!=(byte *)NULL;
     i++);

 if(i<ps)
    return(closexpm(xs));

 xs->maxthresh=ps*ps;

                 /* do thresholds && make sure valid */
 threshold= (threshold <1 || threshold>100) ? 90 : threshold;    /* 90% */
 xs->thresh=(word)((float)xs->maxthresh*((float)threshold * .01));

 for(j=0;j<ps;j++)
    for(i=0;i<ps;i++)
        {
         byte *am= &xs->xa[i][tolower((int)(((unsigned char *)s)[j]))];
         int v=j-i;                                    /* calc distance */
         v=v < 0 ? -v : v;
         v=ps-v;                                    /* inverse distance */
         if(*am<(byte)v)
             {
              *am=(byte)v;
              xs->xa[i][toupper((int)(((unsigned char *)s)[j]))]=(byte)v;         /* PBR 07-12-93 */
             }

        }


#ifdef DEBUG
 for(j=0;j<DYNABYTE;j++)
   {
    if(isprint(j)) printf("  %c: ",j);
    else           printf("%03d: ",j);
    for(i=0;i<ps;i++)
         printf("%d ",xs->xa[i][j]);
    putchar('\n');
   }
#endif

 return(xs);
}

/************************************************************************/

byte *
getxpm(xs,buf,end,operation)
register XPMS *xs;
register byte *buf,*end;
TXPMOP operation;
{
 byte *bp;                                                   /* buf ptr */
 word sum;                                                 /* the total */
 word thresh=xs->thresh;
 word maxhit=xs->maxhit;
 unsigned int i;
 unsigned int patsize=xs->patsize;
 byte **xa;

 if(operation==CONTINUESEARCH)
   {
    if(xs->hit<buf || xs->hit >end)
         return(BPNULL);
    buf=xs->hit+1;
   }
 if((unsigned int)(end-buf)<patsize)/* prevent rollunder (ala windows) MAW - 11-08-93*/
    return(BPNULL);
#ifdef WHATITWAS
 for(end-=patsize;buf<=end;buf++)
    {
     for(i=0,sum=0,bp=buf;i<patsize; i++,bp++)
           sum+=xs->xa[i][tolower((int)(*bp))];
     if(sum>=thresh)
         return(xs->hit=buf);
    }
#else /* WHATITIS */                                   /* PBR 07-12-93 */
 for(end-=patsize;buf<=end;buf++)
    {
     for(i=0,sum=0,bp=buf,xa=xs->xa;i<patsize; i++,bp++,xa++)
          sum+= (*xa)[*bp];
     if(sum>maxhit)
         {
          xs->maxhit=sum;
          memcpy((void *)xs->maxstr,buf,patsize);
          xs->maxstr[patsize]='\0';
         }
     if(sum>=thresh)
        {
         xs->thishit=sum;
         return(xs->hit=buf);
        }
    }
#endif
 return(BPNULL);
}

/************************************************************************/

#ifdef TESTXPM

#define LNSZ 4096

char line[LNSZ];

main(argc,argv)
int argc;
char *argv[];
{
 XPMS *xs;
 if(argc<3)
   {
    puts("Use: xpm string threshold-percentage");
    exit(1);
   }
 if((xs=openxpm(argv[1],atoi(argv[2])))==(XPMS *)NULL)
   exit(1);

 while(gets(line))
    {
     byte *hit;
     int i;
     if((hit=getxpm(xs,(byte *)line,(byte *)line+strlen(line),SEARCHNEWBUF))!=BPNULL)
         {

#ifndef MWSPELL
                    printf("thresh %u, maxthresh %u, thishit %d, maxhit %u, maxstr %s\n",
                            xs->thresh,xs->maxthresh,xs->thishit,xs->maxhit,xs->maxstr);
          printf("%4.2f ",(float)xs->thishit/(float)xs->maxthresh);
          for(i=0;i<xs->patsize;i++)
              putchar(*hit++);
          putchar(':');
#endif
          puts(line);
         }
    }
 printf("max hit:\n\t%4.2f  %s\n",(float)xs->maxhit/(float)xs->maxthresh,xs->maxstr);
 closexpm(xs);
 exit(0);
}

#endif
/************************************************************************/
