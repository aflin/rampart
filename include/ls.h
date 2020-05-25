#ifdef __cplusplus
extern "C" {
#endif
#ifndef LS_H
#define LS_H
/**********************************************************************/
#ifndef LSDOMATCH
#  define LSDOMATCH 1
#endif
#if LSDOMATCH
#  ifndef WILD
#     include "wild.h"
#  endif
#endif                                                   /* LSDOMATCH */
#ifndef uint
#  define uint unsigned int
#endif
/**********************************************************************/
#define LSR struct lsr
#define LSRPN (LSR *)NULL
LSR {
   char *path;
#if LSDOMATCH
   WILD *expr;
#endif
   uint flags;
   DIR *d;
   LSR *sub;                                                /* subdir */
};

#define LS_DIRFIRST 1/* when recursive: return dirname before traversing */
               /* see ls.c for caveat before setting LS_DIRFIRST to 0 */

#define LS_RECUR  0x01                              /* walk down tree */
#if LSDOMATCH
#define LS_NOT    0x02                 /* invert meaning of file spec */
#endif
#define LS_DIRNMS 0x04                      /* return directory names */
/**********************************************************************/
extern void  (*lserror) ARGS((int e,char *msg));

extern int   lsshowerr ARGS((int yes));                 /* KNG 971219 */
extern LSR  *lsclose    ARGS((LSR *ls));
extern int   lsishidden ARGS((char *fn));
extern LSR  *lsopen     ARGS((char *spec,uint flags));
extern char *lsread     ARGS((LSR *ls,struct stat *st));
#ifdef EPI_NEED_BASENAME_PROTO/* JMT 2000-04-17 */
extern EPI_BASENAME_CONST char *basename   ARGS((EPI_BASENAME_CONST char *));
#endif
#ifndef HAVE_DIRNAME_PROTO
extern char *dirname    ARGS((char *));               /* MAW 06-26-97 */
#endif
#define lsrecurs(a,b) ((b)?((a)->flags|=LS_RECUR ):((a)->flags&=~LS_RECUR ))
#if LSDOMATCH
#define lsinvert(a,b) ((b)?((a)->flags|=LS_NOT   ):((a)->flags&=~LS_NOT   ))
#endif
#define lsdirnms(a,b) ((b)?((a)->flags|=LS_DIRNMS):((a)->flags&=~LS_DIRNMS))
/**********************************************************************/
#endif                                                        /* LS_H */
#ifdef __cplusplus
}
#endif
