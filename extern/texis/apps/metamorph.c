/**********************************************************************

                                mmex1.c

Compile with:

MSDOS: (Microsoft C 5.1 or above)
  cl /Ml /Ox mmex1.c /link lapi3.lib/NOE/ST:3072/PACKCODE/F/CP:1

OS/2: (Microsoft C 6.0 or above)
  cl /Lp /Ml /Ox mmex1.c /link lapi3.lib/NOE/ST:3072/PACKCODE/F

Unix:
  cc -O mmex1.c -lapi3 -lm -o mmex1

This program will serve to illustrate the various calls to the Metamorph
API.  It will act as a fully functional Metamorph 3 search program.

    Usage:mmex1 [-option=value [...]] "query" filename(s)
    Where:"query" is any valid Metamorph query
          filename is the name of the file(s) to be searched. (default stdin)
          -option=value will change APICP variables
          the option is the name of the APICP variable
          the value should match the type of APICP variable
          list values are specified with commas: e.g. "-noise=was,were,and"

Please note : This code is written for clarity and doesn't pay a lot
              of attention to efficiency or error checking.

**********************************************************************/

#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef EPI_HAVE_STDARG
#  include <stdarg.h>
#else
#  include <varargs.h>
#endif
#define MMSG_C/* indicate that i'm writing my own putmsg() is this file */
#include "api3.h"                                /* the api header file */

#define TXTBUFSZ  30000     /* this is the size of the text read buffer */

/************************************************************************/

/* This will be our putmsg function for this program. All the functions
within Metamorph report their actions/error through this function, so
unless we wish to use the library fuction, it must be declared.

Every call to putmsg has the same first three parameters, and then
optional parameters if the call is a printf type call:

1: The (int)  message number : these come from mmmsg.h (may be  <0, if so don't
   process it).
2: A function name : This may be a (char *) NULL , if so don't process it.
3: The format string/message content. This may be a (char *) NULL , if so don't process it.
*/

EPIPUTMSG()             /* declares for stdarg or varargs; see mmsg.h */
/*{*/

 /* we are only going to issue messages here if they are error or
 warning level messages */
 if(n<0 || n>=200)
    {
     va_end(args);
     return(0);
    }

 if( n>=0   && n<100 ) fputs("ERROR: ",stderr);
 else
 if( n>=100 && n<200 ) fputs("WARNING: ",stderr);

 if(fmt!=(char *)NULL)
#ifdef __TSC__            /* topspeed C 3.01 vfprintf() does not work */
    {
     char buf[256];
     vsprintf(buf,fmt,args);
     fputs(buf,stderr);
    }
#else
     vfprintf(stderr,fmt,args);            /* print the message content */
#endif

 if(fn!=(char *)NULL)              /* print the function if its present */
     fprintf(stderr," In the function: %s ",fn);

 fputc('\n',stderr);                              /* print the new line */

 va_end(args);                /* terminate the argument list processing */

 return(0);                              /* false means OK in this case */
}                                                       /* end putmsg() */

/************************************************************************/
static int nulleqedit ARGS((APICP *acp));

static int
nulleqedit(acp) /* this is a null eqedit function to point to in init */
APICP *acp;
{
/* NOTE: this function is example code only, and is not intended to be used
         as is in an app. error checking has been abandoned for clarity. */
/* set string format:
   xrootword[;class][,equiv[;class]][...]
where:
   x        is the logic character (+ - =)
   rootword is the user's word/phrase/pattern
   class    is the optional classification for this and following words
   equiv    is an equivalent word/phrase
*/
int i;
byte **set, *p, *c, *wrd, *clas;

 puts("**************** eqedit ****************");
 for(i=0,set=acp->set;*set[i]!='\0';i++){
    printf("set[%d]=\"%s\"\n",i,set[i]);      /* display whole string */
                                         /* break out parts of string */
    printf("  logic: %c\n",*set[i]);
    clas=(byte *)strdup("");
    for(wrd=p=set[i]+1,c=BYTEPN;*p!='\0';p++){
       if(*p=='\\') p++;                                /* escapement */
       else if(*p==';') c=p;                             /* new class */
       else if(*p==','){                                  /* new word */
          *p='\0';
          if(c!=BYTEPN){
             free(clas);
             clas=(byte *)strdup((char *)c+1);
             *c='\0';
          }
          printf("  %s)%s\n",clas,wrd);
                                       /* important to restore string */
          *p=',';
          if(c!=BYTEPN) *c=';',c=BYTEPN;
          wrd=p+1;
       }
    }
    free(clas);
 }
 puts("************ end eqedit ****************");
 return(0);
}

/************************************************************************/

static int nulleqedit2 ARGS((APICP *acp,EQVLST ***eql));

static int
nulleqedit2(acp,eqlp)/* this is a null eqedit2 function to point to in init */
APICP *acp;
EQVLST ***eqlp;
{
/* this is called before acp->eqedit, and acp->set is not setup yet */
/* you may make a new array and set *eqlp=new array */
/* NOTE: this function is example code only, and is not intended to be used
         as is in an app. error checking has been abandoned for clarity. */
int i, j;
EQVLST **eql= *eqlp;
char **w, **c;

 (void)acp;
 puts("**************** eqedit2 ****************");
 for(i=0;eql[i]->words!=CHARPPN;i++){
    w=eql[i]->words;
    c=eql[i]->clas ;
    printf("logic: %c\n",eql[i]->logic);
    printf("%s)%s\n",c[0],w[0]);
    for(j=1;*w[j]!='\0';j++){
       printf("  %s)%s\n",c[j],w[j]);
    }
 }
 puts("************ end eqedit2 ****************");
 return(0);
}

/************************************************************************/
static void puttext ARGS((char *s,int n));

static void
puttext(s,n)          /* print some number of bytes of s + \n to stdout */
char *s;
int   n;
{
#ifdef macintosh
 for(;n;n--,s++)
    if(*s=='\r') putchar('\n');
    else         putchar(*s);
#else
 for(;n;n--,s++)
    putchar(*s);
#endif
 putchar('\n');
}

/************************************************************************/
int apitest ARGS((struct mm_api *mm,FILE *fh,char *fname,int verbose));

int               /* I return the number of located hits or -1 if error */
apitest(mm,fh,fname,verbose)
MMAPI *mm;                     /* A pointer to the opened API structure */
FILE  *fh;                                         /* the opened file */
char *fname;                    /* the name of the file for reference */
int verbose;                             /* print info about the hits */
{
 char *Fn="API TEST FUNCTION";     /* the english name of this function */
 char    *buf;                     /* the buffer we will read data into */
 int    nread;  /* the number of bytes returned from the read operation */
 int  nhits=0;               /* a count of the number of hits to return */
 long bufbase;                    /* the file offset of the buffer base */

                /* allocate memory for the read buffer */

 if((buf=(char *)malloc(TXTBUFSZ))==(char *)NULL)
    {
     putmsg(MERR+MAE,Fn,"Can't allocate memory for a buffer!");
     return(-1);                                               /* error */
    }


     /* The outer loop reads in buffer aligned text from the file */
   /* bufbase keeps track of the relative file offset of the buffer */

 for(bufbase=0L,nread=rdmmapi((byte *)buf,TXTBUFSZ,fh,mm); /* seed read */
     nread>0;                            /* is there more to look at ?? */
                                                      /* read some more */
     bufbase+=nread,nread=rdmmapi((byte *)buf,TXTBUFSZ,fh,mm)
    )
    {
     char    *hit;         /* the offset within the buffer of the "hit" */

             /* this loop locates the hits within the buffer */

                                                      /* find first hit */
     for(hit=getmmapi(mm,(byte *)buf,(byte *)buf+nread,SEARCHNEWBUF);
         hit!=(char *)NULL;                       /* was there a hit ?? */
                                                       /* find next hit */
         hit=getmmapi(mm,(byte *)buf,(byte *)buf+nread,CONTINUESEARCH)
        )
         {
          int   index;                           /* the info index item */
          char *where;                          /* where was it located */
          char  *what;                         /* what item was located */
          int  length;                /* the length of the located item */

               /* we found a hit so count and print it */
          nhits++;
          printf("there's a hit in %s at offset: %ld\n",fname,bufbase+(hit-buf));
          if(!verbose)             /* don't print hit context or info */
             continue;

            /* the zero index for infommapi is the whole hit */
            /* the one  index for infommapi is the start delimiter */
            /* the two  index for infommapi is the end delimiter */

          for(index=0;infommapi(mm,index,&what,&where,&length)>0;index++)
              {
               switch(index){
               case 0 : printf("HIT     (%s)\n" ,what); break;
               case 1 : printf("S-DELIM (%s) :" ,what); break;
               case 2 : printf("E-DELIM (%s) :" ,what); break;
               default: printf("SUB-HIT (%s) : ",what); break;
               }
               puttext(where,length);
              }

          for(index=0;index<mm->mme->nels;index++)
              {
               SEL *sp=mm->mme->el[index];
               if(sp->pmtype==PMISXPM &&
                  sp->member)
                   {
                    XPMS *xs=sp->xs;
                    printf("thresh %u, maxthresh %u, thishit %d, maxhit %u, maxstr %s\n",
                            xs->thresh,xs->maxthresh,xs->thishit,xs->maxhit,xs->maxstr);
                   }
              }
         }
    }

 free(buf);                               /* deallocate the data buffer */
 return(nhits);                 /* return the number of hits we located */
}


/************************************************************************/
/************************************************************************/

/*

IMPORTANT NOTE:

All (byte *) and (byte **) variables within the APICP structure are
individually allocated.  If you wish to change their contents for any
reason, you MUST FREE AND REALLOCATE them in the same manner as they were
created.  Otherwise, the closeapicp() function will TRASH your program by
free()ing unallocated data.

*/


/************************************************************************/
/************************************************************************/
static byte *argstr ARGS((char *s,byte *var));

   /* this function changes the contents of an APICP byte * variable */

static byte *                     /* return a pointer to the new string */
argstr(s,var)
char *s;                            /* the new contents of the variable */
byte *var;                        /* the original APICP byte * variable */
{
 char *Fn="Change string argument";
 if(var!=(byte *)NULL) free(var);   /* check and deallocate the old var */
 var=(byte *)malloc(strlen(s)+1);   /* allocate memory for new contents */
 if(var==(byte *)NULL)
    {
     putmsg(MERR+MAE,Fn,"Unable to allocate memory for the string \"%s\"",s);
     return(var);
    }
 strcpy((char *)var,s);   /* copy the contents of the new variable over */
 return(var);
}

/************************************************************************/
static byte **arglst ARGS((char *s,byte **var));

  /* this function changes the contents of an APICP byte ** variable */

static byte **               /* return a pointer to the new string list */
arglst(s,var)
char *s;                            /* the new contents of the variable */
byte **var;                       /* the original APICP byte * variable */
{
 char *Fn="Change list argument";
 char *p;
 int i;

 if(var!=(byte **)NULL)               /* check the lst pointer for NULL */
    {
             /* An empty string is ALWAYS the end of lst */
     for(i=0;*var[i]!='\0';i++)
          free(var[i]);            /* free the individial string memory */
     free(var[i]);             /* free the memory from the empty string */
     free(var);                                 /* free the list memory */
    }

 for(p=s,i=1;*p;p++)        /* count the number of strings in this list */
     if(*p==',') ++i;     /* we have list members delineated by a comma */

 ++i;                        /* add one for the empty string at the end */

 var=(byte **)calloc(i,sizeof(byte *));/* allocate the list of pointers */
 if(var==(byte **)NULL)
    {
     putmsg(MERR+MAE,Fn,"Unable to allocate list memory");
     return(var);
    }
          /* copy and allocate the list into the new ptr list */
 for(p=s,i=0;*p;i++)
    {
     for(s=p;*p!=',' && *p;p++);                      /* get the length */
     var[i]=(byte *)malloc(p-s+1);                          /* alloc it */
     if(var[i]==(byte *)NULL)        /* out of mem, free the whole list */
        {
         putmsg(MERR+MAE,Fn,"Unable to allocate list memory");
         for(--i;i>=0;--i) free((char *)var[i]);
         free((char *)var);
         var=(byte **)NULL;
         break;
        }
     if(*p==',')
        {
         *p='\0';                                       /* terminate it */
         p++;
        }
     strcpy((char *)var[i],s);                               /* copy it */
    }
 var[i]=(byte *)malloc(1);                  /* alloc list terminator "" */
 if(var[i]==(byte *)NULL)            /* out of mem, free the whole list */
    {
     putmsg(MERR+MAE,Fn,"Unable to allocate list memory");
     for(--i;i>=0;--i) free((char *)var[i]);
     free((char *)var);
     var=(byte **)NULL;
    }
 else *var[i]='\0';
 return(var);
}


/************************************************************************/
int argcmp ARGS((char **,char *));

int argcmp(a1,a2)                     /* args: &"option=value","option" */
char **a1, *a2;
{
char *eq;

 eq=strchr(*a1,'=');
 if(eq!=(char *)NULL)                                      /* found '=' */
    {
     *eq='\0';                                            /* trim at '='*/
     if(strcmp(*a1,a2)==0)                      /* compare option names */
        {
         *eq='=';                                     /* restore string */
         *a1=eq+1;                     /* move pointer past option name */
         return(1);                                       /* is a match */
        }
     *eq='=';                                         /* restore string */
    }
 return(0);                                              /* not a match */
}
/************************************************************************/

#ifdef macintosh /* Changed harddisk to Macintosh HD JMT 96-03-29 */
int
main(void)
{
 char *Fn="Main";              /* the name of our function for putmsg() */
 int i;                                            /* gp index variable */
 int retcd=0;                          /* return code for this function */
 FILE  *fh;                           /* the file we'll be reading from */
 APICP *cp=APICPPN;                     /* control parameters structure */
 MMAPI *mm=MMAPIPN;                     /* the Metamorph API structure  */
 int verbose=1;                           /* print hit info and context */
static char qry[256];
char *av[]={
   "mmex1",
   qry,
   "Macintosh HD:morph3:text:alien",
   "Macintosh HD:morph3:text:constn",
   "Macintosh HD:morph3:text:declare",
   "Macintosh HD:morph3:text:events",
   "Macintosh HD:morph3:text:garden",
   "Macintosh HD:morph3:text:kids",
   "Macintosh HD:morph3:text:liberty",
   "Macintosh HD:morph3:text:qadhafi",
   "Macintosh HD:morph3:text:socrates"
};
char **argv=av;
int argc=sizeof(av)/sizeof(av[0]);
   puts("Enter query:");
   gets(qry);             /* gets() is good enough for a demo program */
   puts("Command line is:");
   for(i=0;i<argc;i++) fputs(argv[i],stdout),putchar(' ');
   putchar('\n');
#else
/**********************************************************************/
int main ARGS((int argc,char * *argv));

int
main(argc,argv)
int argc;
char *argv[];
{
 char *Fn="Main";              /* the name of our function for putmsg() */
 int i;                                            /* gp index variable */
 int retcd=0;                          /* return code for this function */
 FILE  *fh;                           /* the file we'll be reading from */
 APICP *cp=APICPPN;                     /* control parameters structure */
 MMAPI *mm=MMAPIPN;                     /* the Metamorph API structure  */
 int verbose=1;                           /* print hit info and context */
#endif                                                   /* macintosh */

 fputs(api3cpr,stderr);               /* display metamorph3 copyright */
 if(argc<2)
   {
    puts("Usage:\n\tmmex1 [-option=value [...]] \"query\" filename(s)");
    puts("Where:\n\t\"query\" is any valid Metamorph query");
    puts("\tfilename is the name of the file(s) to be searched. (default stdin)");
    return(0);
   }
 {char *l;
    l=setlocale(LC_ALL, "");
    if(l==NULL) printf("no locale set\n");
    else        printf("locale set to %s\n",l);
 }

    /* open a set of default control parameters and check for NULL */

 if((cp=openapicp())==APICPPN)
    {
     putmsg(MERR,Fn,"Could not create control parameters structure");
     return(255);
    }

 cp->eqedit=nulleqedit;
 cp->eqedit2=nulleqedit2;

  /* in the argument handler we'll modify any of the ACP vars as reqd */

 for(i=1;i<argc && *argv[i]=='-';i++)                /* cmd arg handler */
    {
     char *arg= ++argv[i];              /* move the pointer past the '-' */
     if     (argcmp(&arg,"alintersects")) cp->alintersects =atoi(arg);
     else if(argcmp(&arg,"suffixproc" )) cp->suffixproc =(byte)atoi(arg);
     else if(argcmp(&arg,"prefixproc" )) cp->prefixproc =(byte)atoi(arg);
     else if(argcmp(&arg,"defsuffrm"  )) cp->defsuffrm  =(byte)atoi(arg);
     else if(argcmp(&arg,"rebuild"    )) cp->rebuild    =(byte)atoi(arg);
     else if(argcmp(&arg,"incsd"      )) cp->incsd      =(byte)atoi(arg);
     else if(argcmp(&arg,"inced"      )) cp->inced      =(byte)atoi(arg);
     else if(argcmp(&arg,"withinproc" )) cp->withinproc =(byte)atoi(arg);
     else if(argcmp(&arg,"intersects" )) cp->intersects =atoi(arg);
     else if(argcmp(&arg,"minwordlen" )) cp->minwordlen =atoi(arg);
     else if(argcmp(&arg,"see"        )) cp->see        =(byte)atoi(arg);
     else if(argcmp(&arg,"keepeqvs"   )) cp->keepeqvs   =(byte)atoi(arg);
     else if(argcmp(&arg,"keepnoise"  )) cp->keepnoise  =(byte)atoi(arg);
     else if(argcmp(&arg,"eqprefix"   )) cp->eqprefix   =argstr(arg,cp->eqprefix);
     else if(argcmp(&arg,"ueqprefix"  )) cp->ueqprefix  =argstr(arg,cp->ueqprefix);
     else if(argcmp(&arg,"sdexp"      )) cp->sdexp      =argstr(arg,cp->sdexp);
     else if(argcmp(&arg,"edexp"      )) cp->edexp      =argstr(arg,cp->edexp);
     else if(argcmp(&arg,"suffix"     )) cp->suffix     =arglst(arg,cp->suffix);
     else if(argcmp(&arg,"prefix"     )) cp->prefix     =arglst(arg,cp->prefix);
     else if(argcmp(&arg,"noise"      )) cp->noise      =arglst(arg,cp->noise);
     else if(argcmp(&arg,"qmaxsets"   )) cp->qmaxsets=atoi(arg);
     else if(argcmp(&arg,"qmaxsetwords")) cp->qmaxsetwords=atoi(arg);
     else if(argcmp(&arg,"qmaxwords"  )) cp->qmaxwords=atoi(arg);
     else if(argcmp(&arg,"qminwordlen")) cp->qminwordlen=atoi(arg);
     else if(argcmp(&arg,"qminprelen" )) cp->qminprelen =atoi(arg);
     else if(argcmp(&arg,"verbose"    )) verbose        =atoi(arg);
     else if(argcmp(&arg,"hyeqsp"     )) pm_hyeqsp(atoi(arg));
     else if(argcmp(&arg,"wordc"      )) {
                byte *value = (byte *)arg;
                byte *wordc=pm_getwordc();
                int j;
                for(j=0;j<DYNABYTE;j++) wordc[j]=0;
                if(dorange(&value,wordc)<0) putmsg(MERR+UGE,Fn,"Bad range: %s at %s",arg,value);
     }
     else if(argcmp(&arg,"langc"      )) {
                byte *value = (byte *)arg;
                byte *langc=pm_getlangc();
                int j;
                for(j=0;j<DYNABYTE;j++) langc[j]=0;
                if(dorange(&value,langc)<0) putmsg(MERR+UGE,Fn,"Bad range: %s at %s",arg,value);
     }
     else {
           putmsg(MERR,Fn,"\"%s\" is not a valid command line option.",argv[i]);
           return(255);
          }
    }
 if ((mm = openmmapi(argv[i++], TXbool_False, cp)) == MMAPIPN)/* open API and do NULL chk */
    {
     putmsg(MERR,Fn,"Unable to open API");
     closeapicp(cp);                  /* cleanup the control parameters */
     return(255);
    }

 if(i>=argc)
    {
     fh=stdin;
     retcd=apitest(mm,fh,"stdin",verbose);/* run the API test on stdin */
     printf("total of %d hits\n",retcd);
    }
 else
    {
     for(;i<argc;i++)             /* run the api on the argv[...] files */
         {
          int nhits;
          if((fh=fopen(argv[i],"rb"))==(FILE *)NULL)
             {
              putmsg(MWARN+FOE,Fn,"Unable to open input file: %s",argv[i]);
              continue;
             }
          nhits=apitest(mm,fh,argv[i],verbose);/* run the API on this file */
          if(nhits>0) retcd+=nhits;
          fclose(fh);                                       /* close it */
         }
    }
 printf("total of %d hits\n",retcd);
 closemmapi(mm);                                     /* cleanup the API */
 closeapicp(cp);                      /* cleanup the control parameters */
 return(retcd);                                         /* return to OS */
}

/************************************************************************/
