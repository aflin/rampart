#include "txcoreconfig.h"
#include "stdio.h"
#include "sys/types.h"
#include "stdlib.h"
#include "ctype.h"
#include "string.h"
#include "errno.h"
#ifdef macintosh
#  include "console.h"
#  include "api3.h"
#else
#include "os.h"
#include "sizes.h"
#include "salloc.h"
#include "otree.h"
#include "mmsg.h"
#include "eqv.h"

/* WTF these are from texint.h: */
#ifdef WITH_TEXIS
extern char TXInstallPath[];
#define TXINSTALLPATH_VAL       (TXInstallPath + 16)
#define TX_TEXIS_INI_NAME       "conf" PATH_SEP_S "texis.ini"
#endif

#ifndef API3EQPREFIX
#ifdef MSDOS
#define API3EQPREFIX "c:\\morph3\\equivs"
#else
#define API3EQPREFIX "/usr/local/morph3/equivs"
#endif
#endif
#endif

char *Prog;

int use ARGS((void));
/**********************************************************************/
int
use()
{
  int  gap, len;

  fprintf(stderr,"Backref - Equiv backreferencer - Copyright(C) 1993,1997 Thunderstone EPI Inc.\n");
  fprintf(stderr, "Release: %s\n", TXtexisver());
   fprintf(stderr,"Use: %s [-l#] [-b#] [-d] input_file output_file\n",Prog);
#ifdef WITH_TEXIS
   fprintf(stderr,"     --install-dir[-force]" EPI_TWO_ARG_ASSIGN_OPT_USAGE_STR
           "dir    Alternate installation dir.\n");
   len = strlen(TXINSTALLPATH_VAL);
   gap = 61 - len;
   if (gap < 0) gap = 0;
   else if (gap > 22) gap = 22;
   fprintf(stderr, "    %*s(default is `%s')\n", gap, "", TXINSTALLPATH_VAL);
   fprintf(stderr,"     --texis-conf" EPI_TWO_ARG_ASSIGN_OPT_USAGE_STR
           "file            Alternate " TX_TEXIS_INI_NAME " file.\n");
#endif /* WITH_TEXIS */
   fprintf(stderr,"     -l#    = set backreferencing level to #. values are 0-3. default is 2.\n");
   fprintf(stderr,"     -b#    = set max input line size to #. default is 5192\n");
   fprintf(stderr,"     -d     = dump backreferenced listing to standard output.\n");
   fprintf(stderr,"     input_file is your ASCII equiv source filename.\n");
   fprintf(stderr,"     output_file is the backreferenced & indexed binary file.\n");
   return(TXEXIT_INCORRECTUSAGE);
}                                                        /* end use() */
/**********************************************************************/

#ifdef macintosh
int BRmain ARGS((void));
/**********************************************************************/
int
BRmain()
{
int argc;
char **argv;
#else                                                    /* macintosh */
int BRmain ARGS((int argc,char **argv, int argcstrip, char **argvstrip));
/**********************************************************************/
int
BRmain(argc, argv, argcstrip, argvstrip)
int argc, argcstrip;
char **argv, **argvstrip;
{
#endif                                                   /* macintosh */
int level=BREF_LDEFAULT, bufsz=5192, dump=0, rc;
char *src, *bin=CHARPN;
static char *ldesc[]={
   "Syntax check only",
   "Index only",
   "Back reference once",
   "Fully back reference"
};

   argc = argcstrip;
   argv = argvstrip;

#  ifdef macintosh
      argc=ccommand(&argv);
#  endif
   Prog=argv[0];
   for(--argc,++argv;argc>0 && **argv=='-';argc--,argv++){
      switch(*++*argv){
      case 'l': level=atoi(++*argv);
                if(level<0 || level>3) level=BREF_LDEFAULT;
                break;
      case 'b': bufsz=atoi(++*argv);
                if(bufsz<256) bufsz=5192;
                break;
      case 'd': dump=1; break;
      default:  return(use());
      }
   }
   if(argc<1 || (level>BREF_LSYNTAX && argc<2)) return(use());
   src=argv[0];
   if(argc>1) bin=argv[1];
   putmsg(MREPT,CHARPN,"Level: %d, %s",level,ldesc[level]);
   putmsg(MREPT,CHARPN,"Max line size: %d",bufsz);
   rc = eqvmkndx(src,bin,level,bufsz,dump);
zbye:
   return(rc);
}
/**********************************************************************/
