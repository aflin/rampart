#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#ifndef _WIN32
#  include <sys/time.h>
#  include <sys/resource.h>
#endif
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "sizes.h"
#include "dbquery.h"
#include "texint.h"

#define DATA_MIN    4096000
#define STACK_MIN   2048000
#define RSS_MIN     4096000
#define VMEM_MIN   10240000
#define AS_MIN     VMEM_MIN
#define NOFILE_MIN       64
#define CPU_MIN           0
#define FSIZE_MIN         0
#define NPROC_MIN         0
/*#define CORE_MIN          0*/

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif


#ifndef _WIN32
static CONST struct
{
  int         res;
  CONST char  *name;
}
TxResNames[] =
{
#ifdef RLIMIT_CPU
  { RLIMIT_CPU,         "CPU" },
#endif
#ifdef RLIMIT_FSIZE
  { RLIMIT_FSIZE,       "FSIZE" },
#endif
#ifdef RLIMIT_DATA
  { RLIMIT_DATA,        "DATA" },
#endif
#ifdef RLIMIT_STACK
  { RLIMIT_STACK,       "STACK" },
#endif
#ifdef RLIMIT_CORE
  { RLIMIT_CORE,        "CORE" },
#endif
#ifdef RLIMIT_RSS
  { RLIMIT_RSS,         "RSS" },
#endif
#ifdef RLIMIT_MEMLOCK
  { RLIMIT_MEMLOCK,     "MEMLOCK" },
#endif
#ifdef RLIMIT_NPROC
  { RLIMIT_NPROC,       "NPROC" },
#endif
#ifdef RLIMIT_NOFILE
  { RLIMIT_NOFILE,      "NOFILE" },
#endif
#ifdef RLIMIT_VMEM
  { RLIMIT_VMEM,        "VMEM" },
#endif
#ifdef RLIMIT_AS
  { RLIMIT_AS,          "AS" },
#endif
  { 0,                  CHARPN }
};
#endif  /* !_WIN32 */

int
TXrlimname2res(name)
CONST char      *name;
/* Returns RLIMIT_... resource value for string `name', or -1 if unknown.
 * Thread-safe.
 */
{
#ifndef _WIN32
  int   i;

  for (i = 0; TxResNames[i].name != CHARPN; i++)
    if (strcmpi(TxResNames[i].name, name) == 0)
      return(TxResNames[i].res);
#endif /* !_WIN32 */
  return(-1);           /* unknown */
}

CONST char *
TXrlimres2name(res)
int     res;
/* Returns RLIMIT_... resource name for value `res'.
 * NOTE: thread-unsafe.
 */
{
  static char   tmp[EPI_OS_INT_BITS];
  int           i;

#ifndef _WIN32
  for (i = 0; TxResNames[i].name != CHARPN; i++)
    if (TxResNames[i].res == res)
      return(TxResNames[i].name);
#endif /* !_WIN32 */
  i = errno;
  sprintf(tmp, "%d", res);
  errno = i;
  return(tmp);
}

int
TXgetrlimit(pmbuf, res, soft, hard)
TXPMBUF *pmbuf;
int     res;            /* RLIMIT_... resource */
EPI_HUGEINT     *soft, *hard;     /* current and maximum values returned */
/* getrlimit() wrapper.  Returns -1 if not implemented, 0 on error, 1 if ok.
 * EPI_HUGEINT_MAX for `*soft'/`*hard' means infinite.
 */
{
#ifdef _WIN32
  *soft = *hard = EPI_HUGEINT_MAX;
  return(-1);
#else   /* !_WIN32 */
  struct rlimit lim;

  if (getrlimit(res, &lim) != 0)
    {
      txpmbuf_putmsg(pmbuf, MWARN + MAE, CHARPN,
                     "Cannot get resource limit %s: %s",
                     TXrlimres2name(res), TXstrerror(TXgeterror()));
      *soft = *hard = EPI_HUGEINT_MAX;
      return(0);
    }
  else
    {
      *soft = (lim.rlim_cur == RLIM_INFINITY ? EPI_HUGEINT_MAX :
               (EPI_HUGEINT)lim.rlim_cur);
      *hard = (lim.rlim_max == RLIM_INFINITY ? EPI_HUGEINT_MAX :
               (EPI_HUGEINT)lim.rlim_max);
      return(1);
    }
#endif  /* !_WIN32 */
}

int
TXsetrlimit(pmbuf, res, soft, hard)
TXPMBUF *pmbuf;
int     res;            /* RLIMIT_... resource */
EPI_HUGEINT     soft, hard;       /* current and maximum values to set */
/* setrlimit() wrapper.  Returns -1 if not implemented, 0 on error, 1 if ok.
 * EPI_HUGEINT_MAX for `soft'/`hard' means infinite.
 */
{
#ifdef _WIN32
  return(-1);
#else   /* !_WIN32 */
  struct rlimit lim;

  /* indexmem/indexmmapbufsz depend on rlimit; force recompute: */
  if (0
#  ifdef RLIMIT_DATA
      || res == RLIMIT_DATA
#  endif /* RLIMIT_DATA */
#  ifdef RLIMIT_AS
      || res == RLIMIT_AS
#  endif /* RLIMIT_AS */
      )
    TXindexmmapbufsz_val = 0;
#ifdef OPEN_MAX
/*
 * If OPEN_MAX is set then some systems may not allow setting
 * RLIM_INFINITY.  From man setrlimit:
 *
 * COMPATIBILITY
 *   setrlimit() now returns with errno set to EINVAL in places that historically succeeded.  It no longer
 *   accepts "rlim_cur = RLIM_INFINITY" for RLIM_NOFILE.  Use "rlim_cur = min(OPEN_MAX, rlim_max)".
 *
 */

  if((res == RLIMIT_NOFILE) && (soft > OPEN_MAX))
    soft = OPEN_MAX;
#endif /* OPEN_MAX */
  lim.rlim_cur = (soft == EPI_HUGEINT_MAX ? RLIM_INFINITY : (rlim_t)soft);
  lim.rlim_max = (hard == EPI_HUGEINT_MAX ? RLIM_INFINITY : (rlim_t)hard);
  if (setrlimit(res, &lim) != 0)
    {
      txpmbuf_putmsg(pmbuf, MWARN + MAE, CHARPN,
                     "Cannot set resource limit %s to %wkd/%wkd: %s",
                     TXrlimres2name(res), soft, hard,
                     TXstrerror(TXgeterror()));
      return(0);
    }
  return(1);
#endif  /* !_WIN32 */
}

#ifndef _WIN32
static int chkset ARGS((TXPMBUF *pmbuf, int lim, int min, int justmin));
/**********************************************************************/
static int
chkset(pmbuf, lim, min, justmin)
TXPMBUF *pmbuf;
int     lim;
int     min;
int     justmin;
{
  EPI_HUGEINT   soft, hard, set;
  int           rc = 1;

  switch (TXgetrlimit(pmbuf, lim, &soft, &hard))
    {
    case 1:                                                     /* ok */
      if (soft < hard)                  /* increase limit to max */
        {
          set = hard;
          if (justmin)                  /* just asking for minimum */
            {
              if (soft >= (EPI_HUGEINT)min) break;      /* already have min */
              set = (EPI_HUGEINT)min;
            }
          if (TXsetrlimit(pmbuf, lim, set, set) != 1)
            rc = 0;
          else
            soft = set;
      }
      if (soft < (EPI_HUGEINT)min)      /* still below required minimum */
        {
          txpmbuf_putmsg(pmbuf, MWARN + MAE, CHARPN,
                         "Resource limit too low: %s = %wkd, want %wkd",
                         TXrlimres2name(lim), hard, (EPI_HUGEINT)min);
          rc = 0;
        }
      break;
    case 0:                                                     /* failed */
      rc = 0;
      break;
    }
  return(rc);
}

/**********************************************************************/
#endif

int
txmaxrlim(pmbuf)                   /* MAW 06-12-97 - crank up ulimits */
TXPMBUF *pmbuf;
/* NOTE: thread-unsafe */
{
#ifdef _WIN32
   return(1);
#else
static int didit = 0;
int rc=1;

if (didit) return(rc);           /* KNG 011218 save time */

#ifdef RLIMIT_DATA
   chkset(pmbuf, RLIMIT_DATA  ,DATA_MIN, 0);
#endif
#ifdef RLIMIT_STACK
   chkset(pmbuf, RLIMIT_STACK ,STACK_MIN, 1);
#endif
#ifdef RLIMIT_RSS
   chkset(pmbuf, RLIMIT_RSS   ,RSS_MIN, 0);
#endif
#ifdef RLIMIT_VMEM
   chkset(pmbuf, RLIMIT_VMEM  ,VMEM_MIN, 0);
#endif
#ifdef RLIMIT_AS                                      /* another vmem */
   chkset(pmbuf, RLIMIT_AS    ,AS_MIN, 0);
#endif
#ifdef RLIMIT_NOFILE
   chkset(pmbuf, RLIMIT_NOFILE,NOFILE_MIN, 0);
#endif
#ifdef RLIMIT_CPU
   chkset(pmbuf, RLIMIT_CPU   ,CPU_MIN, 0);
#endif
#ifdef RLIMIT_FSIZE
   chkset(pmbuf, RLIMIT_FSIZE ,FSIZE_MIN, 0);
#endif
#ifdef RLIMIT_NPROC
   chkset(pmbuf, RLIMIT_NPROC ,NPROC_MIN, 0);
#endif
/* don't care about core file
#ifdef RLIMIT_CORE
   chkset(pmbuf, RLIMIT_CORE  ,CORE_MIN, 0);
#endif
*/
   didit++;
   return(rc);
#endif                                                  /* !_WIN32 */
}

void
TXmaximizeCoreSize(void)
{
#ifndef _WIN32
  EPI_HUGEINT   soft, hard;
  int           res, lims[2];
  size_t        i;

  /* Max core/file limits; help ensure we get core: */
  lims[0] = RLIMIT_CORE;
  lims[1] = RLIMIT_FSIZE;
  for (i = 0; i < TX_ARRAY_LEN(lims); i++)
    {
      res = TXgetrlimit(TXPMBUFPN, lims[i], &soft, &hard);
      if (res == 1 && soft < hard)
        TXsetrlimit(TXPMBUFPN, lims[i], hard, hard);
    }
#endif /* !_WIN32 */
}

#ifdef TEST
/**********************************************************************/
static void
prtall()
{
struct rlimit rl;
#define prt(a,b) \
   if(getrlimit((a),&rl)==0) \
      printf("%-8s: %10ld %10ld\n",b,(long)rl.rlim_cur,(long)rl.rlim_max)
   prt(RLIMIT_CPU,"CPU");
   prt(RLIMIT_FSIZE,"FSIZE");
   prt(RLIMIT_DATA,"DATA");
   prt(RLIMIT_STACK,"STACK");
   prt(RLIMIT_CORE,"CORE");
#ifdef RLIMIT_RSS
   prt(RLIMIT_RSS,"RSS");
#endif
#ifdef RLIMIT_VMEM
   prt(RLIMIT_VMEM,"VMEM");
#endif
#ifdef RLIMIT_AS
   prt(RLIMIT_AS,"AS");
#endif
#ifdef RLIMIT_NPROC
   prt(RLIMIT_NPROC,"NPROC");
#endif
#ifdef RLIMIT_NOFILE
   prt(RLIMIT_NOFILE,"NOFILE");
#endif
}
/**********************************************************************/
void
main()
{
   puts("--- before ---");
   prtall();
   puts("");
   if(txmaxrlim(NULL))
      puts("Limits set succeeded");
   else
      puts("Limits set failed");
   puts("--- after ---");
   prtall();
}
/**********************************************************************/
#endif                                                        /* TEST */
