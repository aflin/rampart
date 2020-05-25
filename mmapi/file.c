#include "txcoreconfig.h"             /* for HAVE_UNISTD_H   -KNG 971205 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#ifdef macintosh
# ifndef THINK_C
#  include <unistd.h>
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <sys/errno.h>
# else
#  include "types.h"
# endif
#  include "unix.h"
#else
#  include <sys/types.h>
#  include <sys/stat.h>
#endif
#ifdef _WIN32
#  include <io.h>
#  include <direct.h>
#endif
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "os.h"
#include "txtypes.h"
#include "dirio.h"
#ifndef _LARGEFILE_SOURCE    /* MAW 04-06-01 - solaris 7 macro's stat */
#undef stat/* MAW 06-15-93 - use system default stat() times not used */
#endif
#if defined(i386) && !defined(__STDC__)
extern char *getenv ARGS((char *));
#endif


#ifdef _WIN32
   static char profdir[64]="c:\\morph3\0                                                     ";
#else                                                       /* !_WIN32 */
#  ifdef unix
      static char profdir[64]="/usr/local/morph3\0                                             ";
#  else                                                      /* !unix */
#     ifdef macintosh
         static char profdir[64]="Macintosh HD:morph3\0                                           ";
#     else
#        if defined(AOSVS) || defined(VMS)
            static char profdir[64]="\0                                                              ";
#        else
            stop.
#        endif                                           /* macintosh */
#     endif                                                  /* AOSVS */
#  endif                                                      /* unix */
#endif                                                       /* !_WIN32 */

/**********************************************************************/
int
fisdir(fn)
char *fn;
{
#ifdef _WIN32
/* MAW 02-02-94 - borland 4.0 stat will say dir for y:\tmp\* instead of failing */
DWORD rc;

   rc=GetFileAttributes(fn);
   if(rc==0xffffffff) return(0);
   return(rc&FILE_ATTRIBUTE_DIRECTORY);
#else
struct stat st;

   return(stat(fn,&st)==0 && (st.st_mode&S_IFMT)==S_IFDIR);
#endif
}                                                     /* end fisdir() */
/**********************************************************************/

#ifdef _WIN32
static int hasExecutableExt ARGS((CONST char *path));
static int
hasExecutableExt(path)
CONST char      *path;
{
  CONST char    *s, *pathExt, *ext, *last, *e;

  /* Get file extension: */
  for (ext = last = path + strlen(path); ext > path; ext--)
    if (*ext == '.' || TX_ISPATHSEP(*ext) || *ext == ':')
      break;
  if (*ext != '.') ext = last;

  /* Check for common extensions: */
  if (strcmpi(ext, ".exe") == 0 ||
      strcmpi(ext, ".com") == 0 ||
      strcmpi(ext, ".bat") == 0)
    return(1);

  /* Check PATHEXT environment: */
  if ((pathExt = getenv("PATHEXT")) != CHARPN)
    {
      for (s = pathExt; *s != '\0'; s = e, s += strspn(s, " ;"))
        {
          e = s + strcspn(s, " ;");
          if (strnicmp(ext, s, (e - s)) == 0 && ext[e - s] == '\0')
            return(1);
        }
    }

  return(0);                                    /* no valid extension found */
}
#endif /* _WIN32 */

/**********************************************************************/
int
fexecutable(fn)
char *fn;
{
#ifdef _WIN32
   /* Windows does not support 01 as an access mode */
  return(access(fn, 0) == 0 && hasExecutableExt(fn));
#else
   return(access(fn,01)==0);
#endif
}                                                /* end fexecutable() */
/**********************************************************************/

/**********************************************************************/
int
fwritable(fn)                   /* file is writable or can be created */
char *fn;
{
int fh, se;

   if(access(fn,02)==0) return(1);                        /* writable */
   se=errno;
   if(access(fn,00)==0){
      errno=se;
      return(0);                           /* exists, so not writable */
   }
#ifdef macintosh
   if((fh=creat(fn))==(-1)) return(0);        /* can't create it */
#else
   if((fh=creat(fn,0777))==(-1)) return(0);        /* can't create it */
#endif
   close(fh);
   unlink(fn);
   return(1);
}                                                  /* end fwritable() */
/**********************************************************************/

/**********************************************************************/
int
freadable(fn)                                     /* file is readable */
char *fn;
{
   return(access(fn,04)==0);
}                                                  /* end freadable() */
/**********************************************************************/

/**********************************************************************/
int
fexists(fn)                                            /* file exists */
char *fn;
{
   return(access(fn,0)==0);
}                                                    /* end fexists() */
/**********************************************************************/

/**********************************************************************/
int
fcopyperms(sfn,dfn)
char *sfn, *dfn;
{
#ifdef macintosh
   return(-1);
#else
struct stat st;
int rc;

   if(!fexists(dfn)){
      if((rc=creat(dfn,0777))==(-1)) return(-1);
      close(rc);
   }
   if(stat(sfn,&st)==0 &&
      chmod(dfn,st.st_mode)==0
#ifndef _WIN32
      && chown(dfn,st.st_uid,st.st_gid)==0
#endif
   ) rc=0;
   else rc=(-1);
   return(rc);
#endif                                                   /* macintosh */
}                                                 /* end fcopyperms() */
/**********************************************************************/

/**********************************************************************/
char *
pathcat(path,fn)
char *path, *fn;
{
char *dest;
int lsep, lpath, lfn;

   lpath=strlen(path);
   lfn=strlen(fn);
   if(lpath>0 && !TX_ISPATHSEP(path[lpath-1])) lsep=1;
   else                                   lsep=0;
   if((dest=(char *)malloc(lpath+lfn+lsep+1))!=CHARPN){/* leave one for terminator */
      strcpy(dest,path);
      path=dest+lpath;
      if(lsep>0){
         *path=PATH_SEP;
         path++;
      }
      strcpy(path,fn);
   }
   return(dest);
}                                                    /* end pathcat() */
/**********************************************************************/

/**********************************************************************/
char *
epipathfind(fn,path)
char *fn;
char *path;
{
char *p, *s, *buf;
int done;

   for (s = fn; *s && !TX_ISPATHSEP(*s); s++);
   if(*s) {                                      /* has a path separator */
      if((buf=strdup(fn))==CHARPN) errno=ENOMEM;
   }else{
      if(path==CHARPN) path=getenv("PATH");
      if(path==CHARPN){
         buf=CHARPN;
         errno=ENOENT;
      }else{
         if((buf=(char *)malloc(strlen(path)+strlen(fn)+2))==CHARPN){/* max possible size */
            errno=ENOMEM;
         }else{
#           ifdef _WIN32
               strcpy(buf,fn);
               if(fexists(buf)) return(buf);           /* current dir */
#           endif
            for(s=path,done=0;!done;){
               for(p=s;*s!='\0' && *s!=PATH_DIV;s++) ;
               if(*s==PATH_DIV) *(s++)='\0';
               else             done=1;
               if(*p=='\0') strcpy(buf,".");   /* empty means current */
               else         strcpy(buf,p);
               if(!done) *(s-1)=PATH_DIV;  /* restore caller's string */
               strcat(buf,PATH_SEP_S);
               strcat(buf,fn);
               if(fexists(buf)) return(buf);
            }
				free(buf);
				buf=(char *)NULL;
				errno=ENOENT;
         }
      }
   }
   return(buf);
}                                                /* end epipathfind() */
/**********************************************************************/

static int
TXepiPathFindMode_AccessAndStat(const char *path, int mode, int checkfile)
/* Does access() and stat() check, setting errno/TXseterror().
 * Internal use.
 * Returns 0 on error.
 */
{
  EPI_STAT_S    st;

  if (mode != -1)
    {
      if (access(path, mode) < 0) goto err;
      if (checkfile)
        {
          if (EPI_STAT(path, &st) != 0) goto err;
          if (!S_ISREG(st.st_mode))
            {
#ifdef WIN32
              TXseterror(ERROR_INVALID_PARAMETER);  /* wtf file-specific */
#endif /* WIN32 */
              if (S_ISDIR(st.st_mode))
                errno = EISDIR;
              else
                errno = EINVAL;                 /* wtf file-specific err */
              goto err;
            }
        }
    }
  return(1);
err:
  return(0);
}

/**********************************************************************/
char *
epipathfindmode(name, path, mode)
char    *name, *path;
int     mode;
/* Returns malloc'd, absolute path (including drive letter for MSDOS)
 * for file `name', searching shell-type `path' string.  If `mode' is
 * non-zero, path will be checked with the access() [RWXF]_OK mode
 * bits (NOTE: bit 3 means `name' must be a file).
 * If `path' is NULL, no path checking is done (eg. just
 * absolute the path if needed).  Returns NULL on error or not found, with
 * errno/TXseterror() set (no putmsg).  If `mode' is -1, `path' is taken as
 * cwd, but as a file, not directory, and mode is not checked.
 */
{
  char          *s, *buf = CHARPN, *d, *p;
  int           len, isabs, slen, checkfile;
  char          tmp[PATH_MAX];

  if (mode != -1 && (mode & 0x8))
    {
      checkfile = 1;
      mode &= ~0x8;
    }
  else
    checkfile = 0;

#ifdef _WIN32
  /* Windows access() does not support X_OK, and will ABEND in Win64.
   * All we can do is check for existence (below), and check the extension:
   */
  if (mode != -1 && (mode & 0x1))                       /* X_OK specified */
    {
      if (!hasExecutableExt(name))
        {
          TXseterror(ERROR_INVALID_PARAMETER);
          errno = EINVAL;
          goto err;
        }
      mode &= ~0x1;                                     /* avoid ABEND */
    }
#endif /* _WIN32 */

  if (TX_ISABSPATH(name))
    {
      if (!TXepiPathFindMode_AccessAndStat(name, mode, checkfile))
        goto err;
      if ((buf = strdup(name)) == CHARPN)
        {
        merr:
          errno = ENOMEM;
          goto err;
        }
      goto done;
    }
#ifdef _WIN32
  else if (TX_ISPATHSEP(name[0]))               /* relative to drive */
    {
      /* see getcwd() comment below */
      if (mode == -1 && path != CHARPN && *path != '\0' && path[1] == ':')
        {
          *buf = *path;
          buf[1] = path[1];
        }
      else
        {
          if (_getcwd(tmp, sizeof(tmp)) == CHARPN ||
              (buf = (char *)malloc(strlen(name) + 3)) == CHARPN)
            goto merr;
          *buf = *tmp;
          buf[1] = tmp[1];
        }
      strcpy(buf + 2, name);                            /* just use drive */
      if (!TXepiPathFindMode_AccessAndStat(buf, mode, checkfile))
        goto err;
      goto done;
    }
  {
    char        *ia;
    for (ia = name; *ia && !TX_ISPATHSEP(*ia); ia++);
    isabs = (*ia != '\0');
  }
  if (mode != -1)
    {
#else /* !_WIN32 */
  else if (strchr(name, PATH_SEP) != CHARPN && mode != -1) /* cwd relative */
    {
      isabs = 1;
#endif /* !_WIN32 */
      /* KNG 001121 Linux 2.2.12-20 libc.so.6 has broken getcwd(): will
       * (properly) alloc `length' bytes if given NULL buffer, but then
       * (erroneously) reallocs it back down to strlen(wd) + 1 bytes:
       */
      if (getcwd(tmp, sizeof(tmp)) == CHARPN ||
          (buf = (char *)malloc(strlen(tmp) + strlen(name) + 2)) == CHARPN)
        goto merr;
      strcpy(buf, tmp);
      s = buf + strlen(buf);
      if (s > buf && !TX_ISPATHSEP(s[-1]))
        *(s++) = PATH_SEP;
      strcpy(s, name);
      if (!TXepiPathFindMode_AccessAndStat(buf, mode, checkfile))
        {
          if (isabs) goto err;
          free(buf);
          buf = CHARPN;
        }
      else
        goto done;
    }
#ifndef _WIN32
  else
#endif /* !_WIN32 */
    {                                                   /* check path */
      if (path == CHARPN)
        {
#ifdef _WIN32
          TXseterror(ERROR_PATH_NOT_FOUND);
#endif /* _WIN32 */
          errno = ENOENT;
          goto err;
        }

      /* Optimization: avoid getcwd() if possible: */
      if (mode != -1 || !TX_ISABSPATH(path))
        {
          if (getcwd(tmp, sizeof(tmp)) == CHARPN) goto merr;
        }
      else
        *tmp = '\0';
      buf = (char *)malloc(strlen(tmp) + strlen(path) + strlen(name) + 4);
      if (buf == CHARPN) goto merr;
      for (s = path; *s != '\0'; )                      /* each path element */
        {
          len = slen = strcspn(s, PATH_DIV_S);
          if (len == 0)                                 /* "" implies "." */
            {
              p = ".";
              len = 1;
            }
          else
            p = s;
          d = buf;
          if (!TX_ISABSPATH(p))
            {
              strcpy(d, tmp);
#ifdef _WIN32
              if (TX_ISPATHSEP(p[0]))
                d += 2;                                 /* only use drive */
              else
#endif /* _WIN32 */
                {
                  d += strlen(d);
                  if (d > buf && !TX_ISPATHSEP(d[-1]))
                    *(d++) = PATH_SEP;
                }
            }
          memcpy(d, p, len);
          d += len;
          if (mode == -1)
            {
              for (d--; d >= buf; d--)
                if (TX_ISPATHSEP(*d)
#ifdef _WIN32
                    || *d == ':'
#endif /* _WIN32 */
                    ) break;
              d++;
            }
          if (d > buf && !TX_ISPATHSEP(d[-1]))
            *(d++) = PATH_SEP;
          strcpy(d, name);
          if (TXepiPathFindMode_AccessAndStat(buf, mode, checkfile))
            goto done;
          s += slen;
          if (*s == PATH_DIV) s++;
        }
#ifdef _WIN32
          TXseterror(ERROR_FILE_NOT_FOUND);
#endif /* _WIN32 */
      errno = ENOENT;
      goto err;
    }
  goto done;

err:
  TX_PUSHERROR();
  if (buf != CHARPN) free(buf);
  buf = CHARPN;
  TX_POPERROR();
done:
#ifdef MEMDEBUG
  TX_PUSHERROR();
  mac_ovchk();
  TX_POPERROR();
#endif /* MEMDEBUG */
  return(buf);
}                                            /* end epipathfindmode() */
/**********************************************************************/

/**********************************************************************/
FILE *
pathopen(fn,mode,path)
char *fn;
char *mode;
char *path;
{
FILE *fp=FILEPN;
char *p;

   if((p=epipathfind(fn,path))!=CHARPN){
      fp=fopen(p,mode);              /* MAW 07-25-96 - p was fn! duh! */
      free(p);
   }
   return(fp);
}                                                   /* end pathopen() */
/**********************************************************************/

/**********************************************************************/
char *
proffind(fn)                              /* current,home,path,morph3 */
char *fn;
{
char *p;

   for (p = fn; *p && !TX_ISPATHSEP(*p); p++);
   if (*p) {
      if((p=strdup(fn))==CHARPN) goto zerr;
   }else{
      if((p=pathcat(".",fn))==CHARPN) goto zerr;
      if(fexists(p)) goto zgotit;
      free(p);
      if((p=getenv("HOME"))!=CHARPN){
         if((p=pathcat(p,fn))==CHARPN) goto zerr;
         if(fexists(p)) goto zgotit;
         free(p);
      }
      if((p=epipathfind(fn,CHARPN))!=CHARPN) goto zgotit;
      if((p=pathcat(profdir,fn))==CHARPN) goto zerr;
      if(fexists(p)) goto zgotit;
		free(p);
		p=(char *)NULL;
      errno=ENOENT;
   }
zgotit:
   return(p);
zerr:
   errno=ENOMEM;
   return(CHARPN);
}                                                   /* end proffind() */
/**********************************************************************/

/**********************************************************************/
FILE *
profopen(fn,mode)                         /* current,home,path,morph3 */
char *fn;
char *mode;
{
FILE *fp=FILEPN;
char *p;

   if((p=proffind(fn))!=CHARPN){
      fp=fopen(p,mode);
      free(p);
   }
   return(fp);
}                                                   /* end profopen() */
/**********************************************************************/

#ifdef KAITEST
void
main(int argc, char *argv[])
{
  char  *res;

  res = epipathfindmode(argv[1], argv[2], atoi(argv[3]));
  if (res == CHARPN)
    printf("%s not found: %s\n", argv[1], strerror(errno));
  else
    printf("%s found: %s\n", argv[1], res);
}
#endif  /* KAITEST */

#ifdef TEST
void main ARGS((int argc,char **argv));
/**********************************************************************/
void
main(argc,argv)
int argc;
char **argv;
{
struct stat st;
char *p;

   printf("fexists  freadable  fwritable  fexecutable  mode    filename : found\n");
   for(argc--,argv++;argc>0;argc--,argv++){
      if(stat(*argv,&st)==(-1)) st.st_mode=0;
      p=proffind(*argv);
      mac_ovchk();
      printf("   %d         %d          %d           %d       %06o  %s : %s\n",
             fexists(*argv),freadable(*argv),fwritable(*argv),
             fexecutable(*argv),st.st_mode,*argv,p==CHARPN?"NO":p
      );
      if(p!=CHARPN) free(p);
   }
   exit(0);
}                                                       /* end main() */
/**********************************************************************/
#endif                                                        /* TEST */
