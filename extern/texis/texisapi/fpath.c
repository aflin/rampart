#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#ifdef HAVE_PWD_H
#  include <pwd.h>
#endif
#if defined EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <limits.h>                                   /* for PATH_MAX */
#include <sys/types.h>
#ifdef _WIN32
#  include <direct.h>                           /* for getcwd() */
#endif /* _WIN32 */
#define FPATH_C
#include "os.h"
#include "dirio.h"
#if defined(i386) && !defined(__STDC__)
extern char *getenv ARGS((char *));
extern char *getcwd ARGS((char *,int));
#endif
#ifdef TEST
#  define DEBUG(a) (debug?(void)a:(void)0)
   int debug=0;
#else
#  define DEBUG(a)
#endif
#define HOMECHAR   '~'
#define HOMEVAR    "HOME"
#define MACROCHAR  '$'
#define MSTARTCHAR '{'
#define MENDCHAR   '}'

static void ssr ARGS((char *,int,int));
/**********************************************************************/
static void
ssr(s,l,amt)
char *s;
int l, amt;
{
char *e;

	for(e=s+l-1;e>s;e--) *(e+amt)= *e;
	*(e+amt)= *e;                                  /* careful ptr math */
}                                                        /* end ssr() */
/**********************************************************************/

static int expandmacro ARGS((char *src,char *dest,int max));
/**********************************************************************/
static int
expandmacro(src,dest,max)                 /* mostly yanked from mm ui */
char *src;
char *dest;
int max;
{
int brc;
char *end, save, *s;

   for(;*src!='\0' && max>0;src++,dest++,max--){
      if(*src==MACROCHAR){                          /* get macro name */
			src++;
			if(*src==MSTARTCHAR){
				brc=1;
				for(end= ++src;*end!='\0' && *end!=MENDCHAR;end++) ;
			}else{
				brc=0;
				for(end=src;isalnum(*end) || *end=='_';end++) ;
			}
         if(end==src) *dest= *--src;                  /* pass it thru */
         else{                                          /* look it up */
            save= *end;
            *end='\0';
            s=getenv(src);                               /* check env */
            *end=save;
				if(brc && save==MENDCHAR) end++;
#ifndef KEEPUNKNOWNMACRO                       /* long enough for ya? */
            src= --end;                 /* account for ++ of for loop */
#endif
            if(s!=NULL){
#ifdef KEEPUNKNOWNMACRO
					src= --end;              /* account for ++ of for loop */
#endif
               for(end=s;*end!='\0' && max>0;end++,dest++,max--){
                  *dest= *end;
               }
               dest--;               /* account for ++,-- of for loop */
               max++;
               if(*end!='\0') break;
            }else{                                    /* pass it thru */
#ifdef KEEPUNKNOWNMACRO
               *dest= *--src;
#else
					dest--;                      /* account for ++ of loop */
#endif
            }
         }
      }else if(*src==PATH_ESC){
			src++;
			if(*src=='\0') src--;
			else           *dest= *src;
		}else{
         *dest= *src;
      }
   }
   *dest='\0';
   return(*src!='\0');
}                                                /* end expandmacro() */
/**********************************************************************/

static int expandhome ARGS((char *,int));
/**********************************************************************/
static int
expandhome(fn,len)
char *fn;
int len;
{
char *path = CHARPN;
char *f = CHARPN;

	if(*fn!=HOMECHAR) return(0);
	if(*(fn+1)==PATH_SEP){                                     /* "~/" */
		path=getenv(HOMEVAR);
		f=fn+1;
	}else{                                             /* "~username/" */
#ifdef unix
	char sv;
	struct passwd *pw;

		for(f=fn+1;*f!='\0' && *f!=PATH_SEP;f++) ;
		sv= *f;
		*f='\0';
		pw=getpwnam(fn+1);
		*f=sv;
		if(pw==(struct passwd *)NULL) path=CHARPN;
		else                          path=pw->pw_dir;
#endif
	}
	if(path!=CHARPN){
	size_t pl=strlen(path);
	size_t fl=strlen(f)+1;

		if(pl+fl>(size_t)len) return(1);
		ssr(f,fl,pl-(f-fn));
		memcpy(fn,path,pl);
	}
   return(0);
}                                                 /* end expandhome() */
/**********************************************************************/

static int expanddir ARGS((char *,int));
/**********************************************************************/
static int
expanddir(fn,len)
char *fn;
int len;
{
char *path;
size_t fl;
size_t pl;

   if(*fn!=PATH_SEP){
      if((path=getcwd(CHARPN,PATH_MAX+1))==CHARPN) return(1);
		pl=strlen(path);
		fl=strlen(fn)+1;
		if(pl+1+fl>(size_t)len) return(1);
		ssr(fn,fl,pl+1);
		memcpy(fn,path,pl);
		*(fn+pl)=PATH_SEP;
		free(path);
   }
	return(0);
}                                                  /* end expanddir() */
/**********************************************************************/

static int fixpath ARGS((char *));
/**********************************************************************/
static int
fixpath(path)
char *path;
{
char *s, *d;

   for(s=d=path;*s!='\0';){
      if(*s==PATH_ESC){
			s++;
			if(*s!='\0') *(d++)= *(s++);
      }else if(*s=='.'){
         s++;
         if(d==path || *(d-1)==PATH_SEP){
			   if(*s=='\0'){
				   d--;
				}else if(*s==PATH_SEP){                          /* "/./" */
               s++;
            }else if(*s=='.' && *(s+1)==PATH_SEP){          /* "/../" */
					s++;
					if(path+2>d) d=path;               /* careful ptr math */
					else{
						for(d-=2;d>path && *d!=PATH_SEP;d--) ;
					}
            }else{                                          /* "/.s" */
               *(d++)='.';
            }
         }else{
            *(d++)='.';
         }
      }else if(*s==PATH_SEP){                 /* multiple separators */
         if(d==path || *(d-1)!=PATH_SEP) *(d++)=PATH_SEP;
         s++;
      }else{
         *(d++)= *(s++);
      }
   }
	*d='\0';
	return(0);
}                                                    /* end fixpath() */
/**********************************************************************/

/**********************************************************************/
#ifdef _WIN32
#  undef fullpath
#endif

char *
fullpath(buffer,fn,buflen)
char *buffer;
char *fn;
int buflen;
{
int mybuf=0;

   if(buffer==CHARPN){
      if((buffer=(char *)malloc(buflen=PATH_MAX+1))==CHARPN){
			errno=ENOMEM; goto zerr;
      }else mybuf=1;
   }
   if(expandmacro(fn,buffer,buflen)!=0) goto zerr;
	DEBUG(printf("after expandmacro: \"%s\"\n",buffer));
   if(expandhome(buffer,buflen)!=0) goto zerr;
	DEBUG(printf("after expandhome: \"%s\"\n",buffer));
   if(expanddir(buffer,buflen)!=0) goto zerr;
	DEBUG(printf("after expanddir: \"%s\"\n",buffer));
	if(fixpath(buffer)!=0) goto zerr;
	DEBUG(printf("after fixpath: \"%s\"\n",buffer));
   return(buffer);
zerr:
   if(mybuf && buffer != CHARPN) free(buffer);
   return(CHARPN);
}                                                   /* end fullpath() */
/**********************************************************************/

#ifdef TEST
/**********************************************************************/
int
main(argc,argv)
int argc;
char **argv;
{
char *fn;
char in[80];

	if(*argv[0]=='d') debug=1;
	if(argc>1){
		for(--argc,++argv;argc>0;argc--,argv++){
			printf("%-30s: ",*argv);
			fn=fullpath(CHARPN,*argv,0);
			if(fn==CHARPN) puts("Failed");
			else printf("%s\n",fn),free(fn);
		}
	}else{
		while(gets(in)!=CHARPN){
			printf("%-30s: ",in);
			fn=fullpath(CHARPN,in,0);
			if(fn==CHARPN) puts("Failed");
			else printf("%s\n",fn),free(fn);
		}
	}
   return(0);
}                                                       /* end main() */
/**********************************************************************/
#endif                                                        /* TEST */
