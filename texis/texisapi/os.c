/**********************************************************************/
#ifdef mvs
#  include "mvsfix.h"
#endif
/**********************************************************************/
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef unix
#  include <ctype.h>
#  include <errno.h>
#  include <sys/types.h>
#  ifdef UMALLOC3X
#     include <malloc.h>
#  endif
#  define USE_ARG(a)
#else
#  include <errno.h>
#  include <time.h>
#  define USE_ARG(a) (a)
#endif
#ifdef macintosh
#  include <ctype.h>
#  include <files.h>                                   /* for PB...() */
#endif
#ifdef EPI_HAVE_UNISTD_H /* JMT 96-03-29 */
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#ifdef MSDOS
#  include <stddef.h>
#endif
#ifdef MVS
#  if MVS_C_REL > 1
#     include <stddef.h>
#  else
#     include <stdefs.h>
#  endif
#  include <ctype.h>
#endif                                                         /* MVS */
#include "os.h"
#ifdef bsd
#ifndef EPI_HAVE_STDARG                                     /* MAW 03-13-02 */
#  include <varargs.h>
#endif
#endif
#ifdef MVS
#  include "variab.h"
#  include "libman.h"
#endif

#ifndef CPNULL
#  define CPNULL ((char *)NULL)
#endif

#ifndef unix  /* get a real system - in the meantime simulate popen() */
#ifndef ENOMEM
#  define ENOMEM 0
#endif
#ifndef EINVAL
#  define EINVAL 0
#endif
#if !defined(__TSC__) && !defined(macintosh)
/**********************************************************************/
unsigned int
sleep(sec)
unsigned int sec;
{
#ifdef _WIN32
  Sleep(sec*1000);
  return 0;
#else
time_t stime, etime;

  time(&stime); etime=stime+(time_t)sec;
  do{
    time(&stime);
  }while(stime<etime);
  return(sec);
#endif
}                                                      /* end sleep() */
/**********************************************************************/
#endif                                                     /* __TSC__ */

/**********************************************************************/
#ifndef _WIN32
                       /* MAW 07-19-90 - added popen()/pclose() stuff */
#ifdef MVS/* MAW 02-07-91 - use systm() instead or system() in popen/pclose */
#  include "mmsys.h"
#endif

#define PIPETO struct pipeto
PIPETO {
   FILE *fp;                                  /* tmpfile, ret to user */
   char *fn;                                          /* tmpfile name */
   char *cmdname;     /* just name part of command - only used by MVS */
   char *cmd;                                /* user cmd +"<>tmpfile" */
   int  rc;              /* return code from cmd returned by pclose() */
   PIPETO *next;                                       /* linked list */
};

static PIPETO pipeto_base={                          /* base reserved */
   (FILE *)NULL, CPNULL, CPNULL, CPNULL, -1, (PIPETO *)NULL
};
/**********************************************************************/
FILE *popen(cmd,mode)
char *cmd, *mode;/* use caller's mode to open tmp file (allow "rb" etc) */
{
PIPETO *p, *t=(PIPETO *)NULL;                           /* prev, this */
int serrno;

   if(*mode!='w' && *mode!='r') goto zerr;
                                                /* add to end of list */
   for(p= &pipeto_base;p->next!=(PIPETO *)NULL;p=p->next) ;
   p->next=(PIPETO *)calloc(1,sizeof(PIPETO));
   if(p->next==(PIPETO *)NULL){ errno=ENOMEM; goto zerr; }
   t=p->next;
   t->fp=(FILE *)NULL;                             /* clear structure */
   t->fn=t->cmd=CPNULL;
   t->rc=(-1);
   t->next=(PIPETO *)NULL;
   t->fn=TXtempnam(NULL,"pipe",CHARPN);         /* make tmpfile name */
   if(t->fn==NULL) goto zerr;
#  ifdef MVS
      t->cmdname=CPNULL;
#  endif
   t->cmd=malloc(strlen(cmd)+2+strlen(t->fn)+1);     /* build command */
   if(t->cmd==CPNULL){ errno=ENOMEM; goto zerr; }
#  ifdef MVS                                          /* MAW 02-07-91 */
   {
   int i;

      for(i=0;cmd[i]!=' ' && cmd[i]!='\0';i++) ;     /* get name part */
      t->cmdname=malloc(i+1);
      if(t->cmdname==CPNULL){ errno=ENOMEM; goto zerr; }
      strncpy(t->cmdname,cmd,i);
      t->cmdname[i]='\0';
      for(;cmd[i]==' ';i++) ;                      /* skip whitespace */
      strcpy(t->cmd,&cmd[i]);
   }
#  else
      strcpy(t->cmd,cmd);
#  endif
   strcat(t->cmd,*mode=='w'?"<":">");
   strcat(t->cmd,t->fn);
   if(*mode=='w'){         /* let user write to tmpfile till pclose() */
      t->fp=fopen(t->fn,mode);
      if(t->fp==(FILE *)NULL) goto zerr;
   }else{                /* gather output of command for user to read */
#     ifdef MVS
         t->rc=systm(t->cmdname,t->cmd,(char *)NULL);
#     else
         t->rc=system(t->cmd);
#     endif
      if(t->rc<0) goto zerr;
      t->fp=fopen(t->fn,mode);
      if(t->fp==(FILE *)NULL) goto zerr;
#     ifndef MVS
         mac_off();
#     endif
      free(t->fn); t->fn=CPNULL;
#     ifndef MVS
         mac_on();
#     endif
      free(t->cmd); t->cmd=CPNULL;
   }
   return(t->fp);
zerr:
   if(t!=(PIPETO *)NULL){                         /* remove from list */
      serrno=errno;
      p->next=t->next;
      if(t->fp!=(FILE *)NULL) fclose(t->fp);
#     ifndef MVS
         mac_off();
#     endif
      if(t->fn!=CPNULL) free(t->fn);
#     ifndef MVS
         mac_on();
#     endif
#     ifdef MVS
         if(t->cmdname!=CPNULL) free(t->cmdname);
#     endif
      if(t->cmd!=CPNULL) free(t->cmd);
      free((char *)t);
      errno=serrno;
   }
   return((FILE *)NULL);
}                                                      /* end popen() */
/**********************************************************************/

/**********************************************************************/
int pclose(fp)
FILE *fp;
{
PIPETO *p, *t=(PIPETO *)NULL;
int serrno, rc=(-1);

                                                   /* find fp in list */
   for(p= &pipeto_base;p->next!=(PIPETO *)NULL;p=p->next){
      if(p->next->fp==fp){
         t=p->next; break;
      }
   }
   if(t==(PIPETO *)NULL){ serrno=EINVAL; goto zerr; }
   if(t->cmd!=CPNULL){                                 /* was writing */
      fclose(t->fp); t->fp=(FILE *)NULL;
#ifdef MVS
      rc=systm(t->cmdname,t->cmd,(char *)NULL);
#else
      rc=system(t->cmd);               /* run command against tmpfile */
#endif
      serrno=errno;
   }else{                                              /* was reading */
      serrno=errno;
      fclose(t->fp);
      rc=t->rc;
   }
   unlink(t->fn);                                       /* rm tmpfile */
zerr:
   if(t!=(PIPETO *)NULL){                         /* remove from list */
      p->next=t->next;
      if(t->fp!=(FILE *)NULL) fclose(t->fp);
#     ifndef MVS
         mac_off();
#     endif
      if(t->fn!=CPNULL) free(t->fn);
#     ifndef MVS
         mac_on();
#     endif
#     ifdef MVS
         if(t->cmdname!=CPNULL) free(t->cmdname);
#     endif
      if(t->cmd!=CPNULL) free(t->cmd);
      free((char *)t);
   }
   errno=serrno;
   return(rc);
}                                                     /* end pclose() */
/**********************************************************************/
#endif                                                   /* !_WIN32 */
#endif                                                       /* !unix */

/**********************************************************************/
/**********************************************************************/

#ifdef MVS
int sys_nerr=0;
char *sys_errlist[]={
  "No error"
};

#if MVS_C_REL < 2
/**********************************************************************/
#define addr(a)  (char *)(base+(a*bsize))
#define cpy(a,b) memcpy(a,b,bsize)
/**********************************************************************/
void
qsort(base,nel,bsize,cmp)
char *base;
size_t nel, bsize;
#ifdef LINT_ARGS
int (*cmp)(char *,char *);
#else
int (*cmp)();
#endif
{
int chg, x, y;
char *buf;

   if((buf=(char *)malloc(bsize))!=NULL){
      do{                                    /* ascending bubble sort */
                              /* this really sucks, but was expedient */
                              /* until the new compiler came along    */
         chg=0;
         for(y=nel-1,x=y-1;x>=0;x--,y--){           /* from bottom up */
            if((*cmp)(addr(x),addr(y))>0){                 /* swap em */
               cpy(buf,addr(y));
               cpy(addr(y),addr(x));
               cpy(addr(x),buf);
               chg++;
            }
         }
      }while(chg!=0);
      free(buf);
   }
   return;
}                                                      /* end qsort() */
/**********************************************************************/
#undef addr
#undef cpy
/**********************************************************************/
#endif                                               /* MVS_C_REL < 2 */

/**********************************************************************/
int
unlink(file)
char *file;
{
int rc;
char *s1, *s2, sv;
LBM *lm;

   s1=strchr(file,'(');
   if(s1==NULL) rc=remove(file);                      /* seq. dataset */
   else{                                             /* member of pds */
      rc=(-1);                                        /* assume error */
      if(*file=='\''){       /* MAW 02-07-91 - surround w/' as needed */
         *s1='\'';
         s1++;
         sv= *s1;
      }
      *s1='\0';                             /* cut at end of pds name */
                           /* let's hope these names are uniq enough! */
      lm=lmopen("U@Q1A9Y7","U@Q1A9Y8",file,LBM_OUTPUT);
      if(*file=='\''){                                /* MAW 02-07-91 */
         *s1=sv;
         s1--;
      }
      if(lm!=NULL){
         if((s2=strchr(s1+1,')'))!=NULL) *s2='\0';/* cut at end of member name */
         if(lmmdel(lm,s1+1)==LBMOK) rc=0;                   /* say OK */
         if(s2!=NULL) *s2=')';                             /* restore */
         lmclose(lm);
      }
      *s1='(';                                             /* restore */
   }
   return(rc);
}                                                     /* end unlink() */
/**********************************************************************/

/**********************************************************************/
int stat(file,st)    /* mostly a dummy call to let unix/DOS code work */
char *file;
struct stat *st;
{
int rc=(-1);
char lrecl[VNAMELEN+1], recfm[VNAMELEN+1], group[VNAMELEN+1];
char *s1, *s2, sv;
LBM *lm;
static char cnorc[VNAMELEN+1];
static ISPFVAR v_cnorc={ VCHAR,   0,  8,VFUNCTION,"zlcnorc",cnorc };

   st->st_dev  =0;
   st->st_ino  =2;
   st->st_mode =S_IFREG|S_IREAD|S_IWRITE|S_IEXEC;
   st->st_nlink=1;
   st->st_uid  =0;
   st->st_gid  =0;
   st->st_rdev =0;
   st->st_size =0L;
   st->st_atime=0;
   st->st_mtime=0;
   st->st_ctime=0;
   s1=strchr(file,'(');
   if(s1==NULL){                                 /* can't stat seq ds */
      rc=0;
   }else{
      if(inivar(&v_cnorc)==VOK){
         if(*file=='\'') sv= *(s1+1),*s1='\'',*(s1+1)='\0';
         else            *s1='\0';          /* cut at end of pds name */
                           /* let's hope these names are uniq enough! */
         lm=lmopen("U@Q1A9Y7","U@Q1A9Y8",file,LBM_INPUT);
         if(*file=='\'') *(s1+1)=sv;
         if(lm!=NULL){
            if((s2=strchr(s1+1,')'))!=NULL) *s2='\0';/* cut at end of member name */
            if(lmmfind(lm,s1+1,lrecl,recfm,group)==LBMOK){
               if(recfm[0]!='U'){
                  vgetstr(&v_cnorc);
                  st->st_size=atol(cnorc)*atol(lrecl);
               }
               rc=0;                                        /* say OK */
            }
            if(s2!=NULL) *s2=')';                          /* restore */
            lmclose(lm);
         }
         delvar(&v_cnorc);
      }
      *s1='(';                                             /* restore */
   }
   return(rc);
}                                                       /* end stat() */
/**********************************************************************/
#endif                                                         /* MVS */

#ifdef macintosh                                      /* MAW 07-20-92 */
#ifdef THINK_C                  /* JMT 96-03-29  Metrowerks has these */
int mkdir ARGS((char *fn));
/**********************************************************************/
int
mkdir(fn)
char *fn;
{
HFileParam hfpb;
Str255 dirName;

   dirName[0]=strlen(fn);
   strcpy((char *)dirName+1,fn);
   hfpb.ioNamePtr=dirName;
   hfpb.ioVRefNum=0;
   hfpb.ioDirID=0;
   errno=PBDirCreate(&hfpb,0);              /* 0==wait for completion */
   return(errno==0?0:(-1));
}                                                      /* end mkdir() */
/**********************************************************************/

int rmdir ARGS((char *fn));
/**********************************************************************/
int
rmdir(fn)
char *fn;
{
FileParam fpb;
Str255 dirName;

   dirName[0]=strlen(fn);
   strcpy((char *)dirName+1,fn);
   fpb.ioNamePtr=dirName;
   fpb.ioVRefNum=0;
   fpb.ioFVersNum=0;
   errno=PBDelete(&fpb,0);                  /* 0==wait for completion */
   return(errno==0?0:(-1));
}                                                      /* end rmdir() */
/**********************************************************************/

int stat ARGS((char *fn,struct stat *st));
/**********************************************************************/
int
stat(fn,st)
char *fn;
struct stat *st;
{
FileParam fpb;
Str255 name;

   name[0]=strlen(fn);
   strcpy((char *)name+1,fn);
   fpb.ioNamePtr=name;
   fpb.ioVRefNum=0;
   fpb.ioFVersNum=0;
   fpb.ioFDirIndex=0;
   errno=PBGetFInfo(&fpb,0);                /* 0==wait for completion */
   if(errno==0){
      st->st_dev  =fpb.ioFRefNum;
      st->st_ino  =fpb.ioFlNum;
      st->st_mode =S_IREAD|S_IEXEC;
      if(!(fpb.ioFlAttrib&0x01)) st->st_mode|=S_IWRITE;
      if(fpb.ioFlAttrib&0x10) st->st_mode|=S_IFDIR;
      else                    st->st_mode|=S_IFREG;
      st->st_nlink=1;
      st->st_uid  =0;
      st->st_gid  =0;
      st->st_rdev =0;
      st->st_size =fpb.ioFlLgLen;
      st->st_atime=fpb.ioFlMdDat;
      st->st_mtime=fpb.ioFlMdDat;
      st->st_ctime=fpb.ioFlCrDat;
      return(0);
   }else{
      return(-1);
   }
}                                                       /* end stat() */
/**********************************************************************/
#endif /* THINK_C */

/**********************************************************************/
char *
strlwr(s)                                             /* MAW 09-14-92 */
char *s;
{
char *p;

   for(p=s;*p!='\0';p++) *p=tolower((int)(*(byte *)p));
   return(s);
}                                                     /* end strlwr() */
/**********************************************************************/

/**********************************************************************/
char *
strupr(s)                                             /* MAW 09-14-92 */
char *s;
{
char *p;

   for(p=s;*p!='\0';p++) *p=toupper((int)(*(byte *)p));
   return(s);
}                                                     /* end strupr() */
/**********************************************************************/

#endif                                                   /* macintosh */

#if defined(MVS) || defined(macintosh)/* MAW 07-20-92 - add macintosh */
/**********************************************************************/
int access(file,mode)
char *file;
int mode;
{
int rc=(-1), serr;
FILE *fp;

  if(mode==0){                                     /* check for exist */
      fp=fopen(file,"r");
      serr=errno;
      if(fp!=NULL) fclose(fp);
      errno=serr;
      if(fp==NULL) goto err;
  }else{
      if(mode&4){                                   /* check for read */
         fp=fopen(file,"r");
         serr=errno;
         if(fp!=NULL) fclose(fp);
         errno=serr;
         if(fp==NULL) goto err;
      }
      if(mode&2){                                  /* check for write */
         fp=fopen(file,"a");           /* don't destroy existent file */
         serr=errno;
         if(fp!=NULL) fclose(fp);
         errno=serr;
         if(fp==NULL) goto err;
      }
   }
   rc=0;                                             /* all passed ok */
err:
   return(rc);
}                                                     /* end access() */
/**********************************************************************/
#endif                                            /* MVS || macintosh */

#ifdef unix        /* this may need to be moved into the big if below */
/**********************************************************************/
char *
strrev(us)                            /* MAW 12-01-92 - from presuf.c */
char *us;
{
char *s,*e,t;

	s=us;
	e=s+strlen(s);
	for(--e;e>s;s++,e--){
		t= *s;
		*s= *e;
		*e=t;
	}
	return(us);
}                                                     /* end strrev() */
/**********************************************************************/

#if !defined(__stdc__) && !defined(__GNUC__)   /* MAW 07-12-94 - GNUC */
/**********************************************************************/
char *
strstr(s1,s2)                                         /* MAW 12-14-92 */
CONST char *s1, *s2;
{
int l2=strlen(s2);

	for(;*s1!='\0' && strncmp(s1,s2,l2)!=0;s1++) ;
	return(*s1=='\0'?CHARPN:s1);
}                                                     /* end strstr() */
/**********************************************************************/
#endif
#endif                                                        /* unix */

/**********************************************************************/
char *
strstri(s1,s2)                                        /* MAW 12-14-92 */
CONST char *s1, *s2;
{
size_t l2=strlen(s2);

	for(;*s1!='\0' && strnicmp(s1,s2,l2)!=0;s1++) ;
	return(*s1=='\0'?CHARPN:(char *)s1);
}                                                    /* end strstri() */
/**********************************************************************/

#if defined(sparc) && !defined(__stdc__)              /* MAW 09-28-93 */
/**********************************************************************/
char *
memmove(d,s,n)
char *d, *s;
int n;
{
char *rc=d;

	if(d<s){
		for(;n>0;n--) *(d++)= *(s++);
	}else{
		d+=n-1;
		s+=n-1;
		for(;n>0;n--) *(d--)= *(s--);
	}
	return(d);
}
/**********************************************************************/
#endif                                          /* sparc && !__stdc__ */

#if defined(MVS) || defined(macintosh) || defined(AOSVS)
#if !defined(sparc) && !defined(sgi) && !defined(_AIX) && !defined(hpux) && !defined(linux)/* MAW 08-11-95 */
/**********************************************************************/
int
strcmpi(s1,s2)                 /* compares string 1 to 2 without case */
register CONST char *s1,*s2;
{
#ifndef unix
static char ss1[2]=" ", ss2[2]=" ";
#endif

   for(;*s1 && *s2 && tolower((int)(*(byte *)s1))==tolower((int)(*(byte *)s2));s1++,s2++) ;
#  ifdef MVS
      ss1[0]=tolower((int)(*(byte *)s1)); ss2[0]=tolower((int)(*(byte *)s2));
      return(strncmp(ss1,ss2,1));            /* use native comparison */
#  else
      return((tolower((int)(*(byte *)s1))&0xff)-(tolower((int)(*(byte *)s2))&0xff));
#  endif
}
/**********************************************************************/

/**********************************************************************/
#if !defined(EPI_HAVE_STRNICMP) && !defined(EPI_HAVE_STRNCASE_CMP)
int
strnicmp(s1,s2,n)              /* compares string 1 to 2 without case */
register CONST char *s1,*s2;
register size_t n;
{
#  ifndef unix                                                 /* ascii */
static char ss1[2]=" ", ss2[2]=" ";
#  endif /* !unix */

   for(;n && *s1 && *s2 && tolower((int)(*(byte *)s1))==tolower((int)(*(byte *)s2));s1++,s2++,n--);
#  ifdef MVS
      if(n==0) return(0);
      else{
         ss1[0]=tolower((int)(*(byte *)s1)); ss2[0]=tolower((int)(*(byte *)s2));
         return(strncmp(ss1,ss2,1));         /* use native comparison */
      }
#  else /* !MVS */
      if(n==0) return(0);
      else     return((tolower((int)(*(byte *)s1))&0xff)-(tolower((int)(*(byte *)s2))&0xff));
#  endif /* !MVS */
}
/**********************************************************************/
#endif /* !EPI_HAVE_STRNICMP && !EPI_HAVE_STRNCASE_CMP */
#endif                                   /* !sparc  && !sgi  && !_AIX */

#if defined(bsd) || defined(macintosh) /* MAW 07-20-92 - add macintosh */
#undef strdup
/**********************************************************************/
char *
strdup(s)
char *s;
{
char *c;

   if((c=malloc(strlen(s)+1))!=(char *)NULL) strcpy(c,s);
   return(c);
}                                                     /* end strdup() */
/**********************************************************************/
#endif                                                         /* bsd */

#if defined(sysv) && !defined(__STDC__) && !defined(__stdc__)
/**********************************************************************/
int rename(old,new)                         /* rename file old to new */
char *old, *new;
{
int rc=0;

   if(link(old,new)==(-1)) rc=(-1);             /* connect old to new */
   else if(unlink(old)==(-1)){                              /* rm old */
      unlink(new);                    /* failure, rm new, leaving old */
      rc=(-1);
   }
   return(rc);
}                                                     /* end rename() */
/**********************************************************************/
#endif                                       /* !bsd && !MSDOS && !MVS*/

#ifdef __MACH__
#undef getcwd
/**********************************************************************/
char *
getcwd(buf,size)
char *buf;
int size;
{
FILE *fp=FILEPN;
int mine=0, nr;

	if(size==0) errno=EINVAL;
	else if(buf==CHARPN && ((mine=1),((buf=(char *)malloc(size))==CHARPN))) errno=ENOMEM;
	else{
		if((fp=popen("/bin/pwd","r"))!=FILEPN){
			if((nr=fread(buf,1,size,fp))>=0){
				if(nr==size) errno=ERANGE;
				else{
					if(nr>0 && buf[nr-1]=='\n') nr--;
					buf[nr]='\0';
					pclose(fp);
					return(buf);
				}
			}
			nr=errno;
			pclose(fp);
			errno=nr;
		}
		if(mine) free(buf);
	}
	return(CHARPN);
}
/**********************************************************************/
#endif                                                    /* __MACH__ */

#if defined(bsd) && !defined(__MACH__) && !defined(__STDC__) && !defined(__stdc__) && !defined(sun) && !defined(__osf__)
/**********************************************************************
**  vsprintf.c  vsprintf() like function.
**********************************************************************/
#ifndef BUFSIZ
#  define BUFSIZ  512
#endif

#define  T_INT    1
#define  T_FLT    2
#define  T_CHAR   3
#define  T_CHAR_P 4

static int issym();

int    ia;                                             /* integer arg */
long   la;                                        /* long integer arg */
char   *cpa;                                     /* char  pointer arg */
char   ca;                                        /* regular char arg */
double da;                                         /* double type arg */
/**********************************************************************/
/*VARARGS*/
int
vsprintf(va_alist)
va_dcl
{
char *__pwp;
char *fmt;
char buf[BUFSIZ], hold[16], format[BUFSIZ];
char *fmt1;
va_list stack;
register int a_flag, fmt_cnt, lflag;

  va_start(stack);
  __pwp=va_arg(stack,char *);
  *__pwp='\0';
  fmt = va_arg(stack,char *);
  stack=(va_list)va_arg(stack,va_list *);

  fmt1 = fmt;
  fmt_cnt = 0;
  a_flag = lflag = 0;
  while(*fmt1 != '\0'){
    if(*fmt1 == '%'){
      ++fmt1;
      ++fmt_cnt;
      if(*fmt1 == '%'){
        ++fmt1;
        ++fmt_cnt;
        continue;
      }
      while(*fmt1 != '\0'){
        if((a_flag=issym(*fmt1))==0){
          if(*fmt1 == 'l')
            lflag = 1;
          ++fmt1;
          ++fmt_cnt;
        }else break;
      }
    }else{
      ++fmt1;
      ++fmt_cnt;
      continue;
    }
    if(!a_flag) continue;
    sprintf(hold, "%%.%ds", ++fmt_cnt);
    sprintf(format, hold, fmt);
    switch(a_flag){
    case T_INT:
      if(lflag){
        la = va_arg(stack,long);
        sprintf(buf,format,la);
        strcat(__pwp,buf);
      }else{
        ia = va_arg(stack,int);
        sprintf(buf,format,ia);
        strcat(__pwp,buf);
      }
      break;
    case T_FLT:
      /*
       * lflag doesn't work here, floats are pushed
       * as long floats (doubles).
       */
      da = va_arg(stack,double);
      sprintf(buf,format,da);
      strcat(__pwp,buf);
      break;
    case T_CHAR:
                                          /* chars are pushed as ints */
      hold[0] = va_arg(stack,int);
      ca = (hold[0] == '\0' ? hold[1]:hold[0]);
      sprintf(buf,format,ca);
      strcat(__pwp,buf);
      break;
    case T_CHAR_P:
      cpa = va_arg(stack,char *);
      sprintf(buf,format,cpa);
      strcat(__pwp,buf);
      break;
    default:                                   /* should never happen */
      va_end(stack); return(-1);
    }
    ++fmt1;
    fmt = fmt1;
    a_flag = lflag = fmt_cnt = 0;
  }
  va_end(stack);
  if(fmt_cnt == 0) return(0);
  sprintf(hold, "%%.%ds", ++fmt_cnt);
  sprintf(format,hold,fmt);
  strcat(__pwp,format);
  return(0);
}                                                   /* end vsprintf() */
/**********************************************************************/

/**********************************************************************/
static int
issym(c)
register char c;
{
  switch(c) {
  case  'd': case  'o': case  'u': case  'x': case  'X': return(T_INT);
  case  'f': case  'e': case  'E': case  'g': case  'G': return(T_FLT);
  case  'c': return(T_CHAR);
  case  's': return(T_CHAR_P);
  default:   return(0);
  }
}                                                      /* end issym() */
/**********************************************************************/
#endif                                            /* bsd && !__MACH__ */
#endif                                                 /* unix || MVS */
