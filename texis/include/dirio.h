#ifndef DIRIO_H
#define DIRIO_H
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif                                                        /* ARGS */
/**********************************************************************/
#ifdef PATH_SEP               /* MAW 02-24-94 - override defs in os.h */
#  undef PATH_SEP
#endif
#ifdef PATH_SEP_S             /* MAW 02-24-94 - override defs in os.h */
#  undef PATH_SEP_S
#endif
/**********************************************************************/
#ifdef unix
#  ifdef BSDFS
#     if defined(BSDSYSVMUT) || defined(ultrix) || defined(i386) || defined(__x86_64)
#        ifndef hpux                                  /* MAW 12-13-91 */
                       /* MAW 12-13-91 - does anything here need this? */
/*#           include <sys/dir.h>*/             /* sys V w/bsd filesystem */
#        endif
#        include <dirent.h>/* MAW 04-05-91 - for readdir() for 88Open */
#     else
#        if defined(__convex__) || defined(__osf__) || defined(_SVR4) || defined(__SVR4) /* MAW 07-24-91 Convex machine *//* MAW 03-09-93 dec alpha *//* MAW 12-19-95 svr4 JMT 01-30-96 */
#           include <dirent.h>
#        else
#           if defined(__MACH__) || defined(sun) || defined(__sgi)/* MAW 05-10-91 NeXT machine */ || defined(__ia64) || defined(__linux__)
                        /* MAW 10-02-91 - "|| defined(sun)" for sparc */
                        /* MAW 12-16-92 - "|| defined(__sgi)" for sgi */
#              if defined(__MACH__)            /* MAW 12-04-92 - NeXT */
#                 undef ushort
#              endif
#              include <sys/dir.h>
#           else
#              include <dir.h>                                 /* bsd */
#           endif
#        endif
#     endif
#     if defined(gould) && defined(sysv)
#        define READDIRTYPE struct dirent
#     else
#        ifdef __MACH__                        /* MAW 09-08-93 - NeXT */
#           define READDIRTYPE struct direct
#        else
                                               /* __DCC__ from 88Open */
#           if defined(_AIX) || defined(__DCC__) || defined(__convex__) || defined(hpux) || defined(__STDC__) || defined(__sgi) || defined(i386) || defined(__osf__) || defined(DGUX)
#              define READDIRTYPE struct dirent
#           else
#              define READDIRTYPE struct direct
#           endif
#        endif
#     endif                                          /* gould && sysv */
#  else                                                     /* !BSDFS */
#     include <sys/dir.h>
#     define DIR FILE
#     define opendir(a)   fopen(a,"r")
#     define closedir(a)  fclose(a)
#     define telldir(a)   ftell(a)
#     define seekdir(a,b) fseek(a,b,0)
#     define rewinddir(a) rewind(a)
      extern struct direct *readdir ARGS((DIR *));
#     define READDIRTYPE struct direct
#     define MAXNAMLEN DIRSIZ
#  endif                                                     /* BSDFS */
#  define PATH_SEP   '/'
#  define PATH_SEP_S "/"
#  define PATH_ESC   '\\'                           /* RE escape char */
#  define PATH_ESC_S "\\"                           /* RE escape char */
#  define ALLPATTERN "*"
#  define PATH_DIV   ':'          /* MAW 05-20-91 - separator in PATH */
#  define PATH_DIV_S ":"          /* MAW 05-20-91 - separator in PATH */
#  define TX_PATH_DIV_REX_CHAR_CLASS    "[:]"   /* bracketed, sans repeat op*/
#  define TX_PATH_DIV_REX_CHAR_CLASS_LEN        3
#  define TX_PATH_DIV_REPLACE_EXPR      ":"
#  define TX_PATH_DIV_REPLACE_EXPR_LEN  1
#  ifndef PATH_MAX                           /* MAW 12-04-92 - set it */
#     ifdef _POSIX_PATH_MAX
#        define PATH_MAX _POSIX_PATH_MAX
#     else
#        define PATH_MAX 256
#     endif
#  endif
#endif                                                        /* unix */
/**********************************************************************/
/**********************************************************************/
#ifdef MSDOS
/*#  include <dos.h>*/
#  if !defined(MAX_PATH) && defined(_MAX_PATH)
#     define MAX_PATH _MAX_PATH
#  endif
#  ifndef ushort
#     define ushort unsigned short
#  endif
#  ifndef uint
#     define uint unsigned int
#  endif
#  ifndef ulong
#     define ulong unsigned long
#  endif
#  define PATH_SEP   '\\'
#  define PATH_SEP_S "\\"
#  define PATH_ESC   '/'                            /* RE escape char */
#  define PATH_ESC_S "/"                            /* RE escape char */
#  define ALLPATTERN "*.*"
#  define PATH_DIV   ';'          /* MAW 05-20-91 - separator in PATH */
#  define PATH_DIV_S ";"          /* MAW 05-20-91 - separator in PATH */
#  define TX_PATH_DIV_REX_CHAR_CLASS    "[;]"   /* bracketed, sans repeat op*/
#  define TX_PATH_DIV_REX_CHAR_CLASS_LEN        3
#  define TX_PATH_DIV_REPLACE_EXPR      ";"
#  define TX_PATH_DIV_REPLACE_EXPR_LEN  1
#  ifndef PATH_MAX
#    ifdef MAX_PATH
#     define PATH_MAX MAX_PATH
#    else
#     define PATH_MAX 128
#    endif
#  endif

#  if defined(_WIN32) && defined(MAX_PATH)
#     define MAXNAMLEN MAX_PATH
#     define MAXDIRLEN MAX_PATH
#  else
#     define MAXNAMLEN 12
#     define MAXDIRLEN   128
#  endif
   struct direct {
      ulong  d_ino;                                   /* inode number */
      ushort d_reclen;                       /* length of this record */
      ushort d_namlen;                  /* length of string in d_name */
      char   d_name[MAXNAMLEN+1]; /* name must be no longer than this */
   };
   typedef struct {
     int cur_ent;
     int restart;
     uint flag;                                  /* file type filters */
     char name[MAXDIRLEN];
     struct direct e;                      /* sep struct for each one */
#    if defined(_WIN32) && defined(MAX_PATH)
       HANDLE          fh;
       WIN32_FIND_DATA fi;
       int             forcelower;                    /* MAW 08-26-94 */
#    endif
   } DIR;
#  define telldir(p)   (long)((p)->cur_ent)
#  define seekdir(p,l) (((p)->cur_ent<0?0:(p)->cur_ent=(int)l),(p)->restart=1)
#  define rewinddir(p) seekdir(p,0)

   extern DIR *opendir ARGS((char *));
   extern int closedir ARGS((DIR *));
   extern struct direct *readdir ARGS((DIR *));
#  define READDIRTYPE struct direct
#endif                                                       /* MSDOS */
/**********************************************************************/
/**********************************************************************/
#ifdef MVS
#  include "variab.h"
#  include "libman.h"
#  define PATH_SEP   '.'
#  define PATH_SEP_S "."
#  define PATH_ESC   '/'                            /* RE escape char */
#  define PATH_ESC_S "/"                            /* RE escape char */
#  define ALLPATTERN "*"

#  define MAXNAMLEN 8             /* name must be no longer than this */
   struct direct {
      char d_ino;
      char d_name[MAXNAMLEN+1];
   };
#  define DIR LBM
   extern DIR           *opendir ARGS((char *));
   extern int           closedir ARGS((DIR *));
   extern struct direct *readdir ARGS((DIR *));
#  define READDIRTYPE struct direct
#endif                                                         /* MVS */
/**********************************************************************/
/**********************************************************************/
#ifdef macintosh                                      /* MAW 07-20-92 */
#  define PATH_SEP   ':'
#  define PATH_SEP_S ":"
#  define PATH_ESC   '\\'                           /* RE escape char */
#  define PATH_ESC_S "\\"                           /* RE escape char */
#  define PATH_DIV   ';'                               /* dummy value */
#  define PATH_DIV_S ";"                               /* dummy value */
#  define TX_PATH_DIV_REX_CHAR_CLASS    "[;]"   /* bracketed, sans repeat op*/
#  define TX_PATH_DIV_REX_CHAR_CLASS_LEN        3
#  define TX_PATH_DIV_REPLACE_EXPR      ";"
#  define TX_PATH_DIV_REPLACE_EXPR_LEN  1
#  define DIR void
#  define MAXNAMLEN 31
   struct direct {
      char d_ino;
      char d_name[MAXNAMLEN+1];
   };
#  define READDIRTYPE struct direct
   extern DIR *opendir(char *);
   extern int  closedir(DIR *);
   extern READDIRTYPE *readdir(DIR *);
#endif                                                   /* macintosh */
/**********************************************************************/
/**********************************************************************/
#ifdef VMS
#  define PATH_SEP   ':'
#  define PATH_SEP_S ":"
#endif
/**********************************************************************/
/**********************************************************************/
#endif                                                     /* DIRIO_H */
