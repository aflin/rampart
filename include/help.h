#ifndef HELP_H
#define HELP_H
#ifdef _AIX
#  undef lines
#endif
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif                                                        /* ARGS */
/**********************************************************************/
#ifdef major         /* hp720 defines major() and minor() in a header */
#  undef major
#endif
#ifdef minor
#  undef minor
#endif
#ifndef uint
#  define uint unsigned int
#endif
                                               /* help version number */
#define HELPVMAJOR 1
#define HELPVMINOR 2

#define HELPBSZ 256

#define HELP struct help
HELP {
   FILE *fp;
   char *fn;            /* remembered filename if (flags&HELPODEMAND) */
   long toppos;                               /* offset of first help */
   uint flags;
   int  err;                                      /* internal "errno" */
   char buf[HELPBSZ];                                  /* read buffer */
};
#define HPNULL (HELP *)NULL
                                                       /* flag values */
#define HELPODEMAND 0x0001                /* only open file on demand */
#define HELPNOMAGIC 0x0002    /* flat help file, don't use MAGIC etc. */
#define HELPEOH     0x0100        /* internal use only - EO this help */

#define HELPMAGICLEN 13
#define HELPMAGIC1   "helphelphelp\n"
#define HELPMAGIC2   "plehplehpleh\n"
#define HELPSMAGIC struct helpsmagic
HELPSMAGIC {
   char magic[HELPMAGICLEN];                            /* HELPMAGIC1 */
};
#define HELPEMAGIC struct helpemagic
HELPEMAGIC {
   long bytes, lines;                       /* amount of data in help */
   long pos;                 /* pos of beginning of help magic string */
   long lastpos;                          /* pos of "end of help" tag */
   char magic[HELPMAGICLEN];                            /* HELPMAGIC2 */
   char major, minor;                                      /* version */
};
#define HELPTAGLEN   2
#define HELPTAG      ".@"
/**********************************************************************/
#define helperror(h) ((h)->err)
#define helpfp(h)    ((h)->fp)

extern HELP *helpopen  ARGS((char *filename,uint flags));
extern void  helpclose ARGS((HELP *h));
extern int   helpset   ARGS((HELP *h,char *tag));
extern char *helpget   ARGS((HELP *h));
extern long  helptell  ARGS((HELP *h));
extern int   helpseek  ARGS((HELP *h,long offset));
/**********************************************************************/
#endif                                                      /* HELP_H */
