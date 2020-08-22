/* stolen directly from mm2 source */
/**********************************************************************/
#include "txcoreconfig.h"
#include "stdio.h"
#include "stdlib.h"
#include "time.h"
#include "os.h"
#include "mmprep.h"
#undef time               /* MAW 06-16-93 - use system default time() */
/*#define TESTEQ 1 */
/*#define TESTCNT 1 */
/*#define MAKECNT 1 */

#define byte unsigned char


/**********************************************************************/
#ifdef USECNT
			   /* for term count */
#ifdef MSDOS
# define MMDEFCNT   "c:\\morph\\carrie.ask"
#define RDMODE "rb"
#define WRMODE "wb"
#endif
#if UNIX || unix
#define RDMODE "r"
#define WRMODE "w"
# define MMDEFCNT   "/usr/local/metamorph/carrie.ask"
#endif

#define CNTFWAIT     5	   /* how long should I wait for the count file */
#define UNLIMITED  200 /* number of terminals that constitute unlimited */
#define RSTLIMIT     5		    /* number of resets before it locks */
#define CFBUFSZ    200			      /* size of the count file */

#define CNTPTR	   100		   /* pointer to the count of terminals */
#define MAXPTR	   110			/* pointer to the max terminals */
#define RSTPTR	   120				       /* reset pointer */
#define CNTVER	   130				 /* verification of CNT */
#define MAXVER	   140				 /* verification of MAX */
#define RSTVER	   150				  /* reset verification */
#endif                                                      /* USECNT */
/**********************************************************************/
			/* for eq access count */
#define ACCSB4DELAY	5
#define DELAYTIME	5
#define DELAYSB4EXIT	10
#define ACCEXIT 	DELAYSB4EXIT
//static char eqps[5]="az09"; /* replace with "ovrd" to turn off eqprep() */
static char eqps[5]="ovrd";
/**********************************************************************/
void
eqprepstr(s)
char *s;
{
int i;

   if(s!=(char *)NULL){              /* MAW 11-22-93 - check for NULL */
       /* MAW 11-22-93 - copy the data so the arg pointer can go away */
      for(i=0;s[i]!='\0' && eqps[i]!='\0';i++) eqps[i]=s[i];
      for(;eqps[i]!='\0';i++) eqps[i]='\0';
   }
}                                                  /* end eqprepstr() */
/**********************************************************************/

/************************************************************************/
int
eqprep()
{
 return(0);
 static time_t ltime=0L;
 static int    accnt=0;
 static int    delays=0;
 time_t ctime;
 if(eqps[0]=='o' &&       /* MAW 11-30-92 - allow override via string */
    eqps[1]=='v' &&
    eqps[2]=='r' &&
    eqps[3]=='d' &&
    eqps[4]=='\0') return(0);
 time(&ctime);
 if(&ctime== &ltime){  /* MAW 01-21-91 - unreached code for obscurity */
    char *buf=(char *)malloc((int)(ctime-ltime)*accnt+delays);
    for(accnt=(int)(ctime-ltime)*accnt+delays;accnt>0;accnt--){
       buf[accnt]=(char)(delays-accnt);
       ctime-=accnt/delays;
       ltime+=accnt/delays;
    }
 }
 if((ctime-ltime)<DELAYTIME)
    ++accnt;
 else accnt=0;
 ltime=ctime;
 if(accnt<ACCSB4DELAY)
    return(0);
 for(;(ctime-ltime)<DELAYTIME;time(&ctime));		/* delay access */
 ltime=ctime;
 --accnt;
 if(++delays>=DELAYSB4EXIT)
#   ifdef _WINDLL                    /* MAW 11-25-91 - a dll can't exit */
       return(1);
#   else
       exit(ACCEXIT);
#   endif
  return(0);
}


/************************************************************************/

#ifdef USECNT
int
dotermcnt(buf,n)
char *buf;
int n;	     /* negative subtract 1 user, positive add 1 user , 0 reset */
{
 int max;
 int cnt;
 int rst;
 int retcd=1;
 int i;

 if(   buf[MAXVER]!=buf[buf[MAXPTR]]		 /* check  verification */
    || buf[CNTVER]!=buf[buf[CNTPTR]]
    || buf[RSTVER]!=buf[buf[RSTPTR]]
   ) return(0); 		    /* somebody has screwed w/ the file */

 max=buf[MAXVER];
 cnt=buf[CNTVER];
 rst=buf[RSTVER];

 if(n<0)  cnt= cnt==0 ? 0 : cnt-1;
 else if(n>0)
   {
    if(max==UNLIMITED)	  retcd=1;
    else if(cnt+1<=max)   ++cnt;
    else		  retcd=0;
   }
 else
   {
    if(++rst<=RSTLIMIT)
       cnt=0;
   }

 srand((int)time((long* )NULL));    /* seed the random number generator */
 for(i=0;i<CFBUFSZ;i++)
    buf[i]=(byte)(rand()%100);


 if(buf[MAXPTR]==buf[CNTPTR] || 	    /* ptrs point to same place */
    buf[CNTPTR]==buf[RSTPTR] ||
    buf[RSTPTR]==buf[MAXPTR]
   )
    {
     buf[MAXPTR]=9;
     buf[CNTPTR]=27;
     buf[RSTPTR]=86;
    }

 buf[MAXVER]=max;
 buf[CNTVER]=cnt;
 buf[RSTVER]=rst;
 buf[buf[MAXPTR]]=max;
 buf[buf[CNTPTR]]=cnt;
 buf[buf[RSTPTR]]=rst;
 return(retcd);
}

/************************************************************************/

char *_mmtcntfn=MMDEFCNT;	       /* in case you want to change it */
char *_mmtcnterr="";

int
chktcnt(n)
int n;
{
 extern char *_mmtcntfn;
 extern char *_mmtcnterr;
 FILE *cfh;
 long stime,ctime;
 byte buf[CFBUFSZ];


 if((cfh=fopen(_mmtcntfn,RDMODE))==(FILE *)NULL)
   {
    _mmtcnterr="Can't access terminal count file";
    return(0);
   }
 if(fread(buf,sizeof(byte),CFBUFSZ,cfh)!=CFBUFSZ)
   {
    _mmtcnterr="Invalid terminal count file size";
    return(0);
   }
 fclose(cfh);
 if(dotermcnt(buf,n))
   {
    if((cfh=fopen(_mmtcntfn,WRMODE))==(FILE *)NULL)
	{
	 _mmtcnterr="Can't open terminal count file to write";
	 return(0);
	}
    if(fwrite(buf,sizeof(byte),CFBUFSZ,cfh)>=CFBUFSZ)
	{
	 fclose(cfh);
	 return(1);
	}
    else _mmtcnterr="Can't write terminal count file";
   }
 else _mmtcnterr="Too many terminals active, try again later.";
 fclose(cfh);
 return(0);
}
#endif                                                      /* USECNT */
/************************************************************************/

#ifdef TESTEQ
main(argc,argv)
{
 char line[80];
 while(1)
    {
     puts("?");
     gets(line);
     eqprep();
     puts("ok");
    }
}
#endif
#ifdef TESTCNT
main(argc,argv)
int argc;
char *argv[];
{
 while(1)
    {
     switch(getchar())
	 {
	  case '+':
	      if(chktcnt(1))   puts("open ok!");
	      else	       puts("open no!");
	      break;
	  case '-':
	      if(chktcnt(-1))	puts("close ok!");
	      else		puts("close no!");
	      break;
	  default : exit(0);
	 }
    }
}
/************************************************************************/
#endif
#ifdef MAKECNT
main(argc,argv)
int argc;
char *argv[];
{
 int n,i;
 FILE *cfh;
 byte buf[CFBUFSZ];
 printf("How many terminals:");
 scanf("%d",&n);

 if((cfh=fopen(_mmtcntfn,WRMODE))==(FILE *)NULL)
     {
      puts("Can't open terminal count file to write");
      return(0);
     }
 for(i=0;i<CFBUFSZ;i++)
    buf[i]=(byte)(rand()%100);

 if(buf[MAXPTR]==buf[CNTPTR] || 	    /* ptrs point to same place */
    buf[CNTPTR]==buf[RSTPTR] ||
    buf[RSTPTR]==buf[MAXPTR]
   )
    {
     buf[MAXPTR]=9;
     buf[CNTPTR]=27;
     buf[RSTPTR]=86;
    }

 buf[MAXVER]=n;
 buf[CNTVER]=0;
 buf[RSTVER]=0;
 buf[buf[MAXPTR]]=n;
 buf[buf[CNTPTR]]=0;
 buf[buf[RSTPTR]]=0;

 if(fwrite(buf,sizeof(byte),CFBUFSZ,cfh)>=CFBUFSZ)
     {
      fclose(cfh);
      return(1);
     }
 else puts("Can't write terminal count file");
 fclose(cfh);
}
#endif
