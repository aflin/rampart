/* Test code for determining TX_ATOMIC_THREAD_SAFE; run by epi/Makefile
 * config_gen.h target.
 */

#define TX_ATOMIC_TESTS_C

#undef MEMDEBUG
#undef USE_EPI
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#ifndef _WIN32
#  include <sys/wait.h>
#  include <sys/ipc.h>
#  include <sys/shm.h>
#endif /* !_WIN32 */
#include "os.h"                                 /* for VOLATILE */
#include "mmsg.h"
#include "sizes.h"                              /* for EPI_INT... txtypes.h */
#include "txtypes.h"
#include "txthreads.h"

/* Thread code (normally compiled standalone): */
#include "../texisapi/txthreads.c"

#ifdef _WIN32
char *
TXstrerror(err)
int	err;
/* WTF thread-unsafe
 * Note: see also rex.c, atomicTests.c standalone implementations
 */
{
	static char	MsgBuffer[4][256];
	static TXATOMINT	bufn = 0;
        int             curBufIdx;
	char		*s, *buf, *end;

        curBufIdx = TX_ATOMIC_INC(&bufn);
	buf = MsgBuffer[curBufIdx & 3];
	end = buf + sizeof(MsgBuffer[0]);
	*buf = '\0';
	if (!FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, (DWORD)err,
                           0, buf, (DWORD)(end - buf), NULL) ||
	    *buf == '\0')
	{				/* FormatMessage() fails w/err 317 */
          sprintf(buf, "ERROR_%d", err);
	}
	/* Trim trailing CRLF and period: */
	for (s = buf + strlen(buf) - 1; s >= buf; s--)
		if (*s == '\r' || *s == '\n') *s = '\0';
		else break;
	if (s > buf && *s == '.' && s[-1] >= 'a' && s[-1] <= 'z') *s = '\0';
	return buf;
}
#endif /* _WIN32 */

/* Fallback atomic functions normally in source/texis/sysdep.c: */
#ifdef TX_ATOMIC_FALLBACK_FUNCS
TXATOMINT_NV
TXatomicadd(val, n)
TXATOMINT      *val;
TXATOMINT       n;
{
  TXATOMINT     ret;

  ret = *val;
  *val += n;
  return(ret);
}

TXATOMINT_NV
TXatomicsub(val, n)
TXATOMINT       *val;
TXATOMINT       n;
{
  TXATOMINT     ret;

  ret = *val;
  *val -= n;
  return(ret);
}

TXATOMINT_NV
TXatomicCompareAndSwap(TXATOMINT *valPtr, TXATOMINT oldVal, TXATOMINT newVal)
{
  TXATOMINT     ret;

  ret = *valPtr;
  if (ret == oldVal) *valPtr = newVal;
  return(ret);
}

#  ifdef TEST_WIDE
TXATOMINT_WIDE_NV
TXatomicCompareAndSwapWide(TXATOMINT_WIDE *valPtr, TXATOMINT_WIDE oldVal,
                           TXATOMINT_WIDE newVal)
{
  TXATOMINT_WIDE        ret;

  ret = *valPtr;
  if (ret == oldVal) *valPtr = newVal;
  return(ret);
}
#  endif /* TEST_WIDE */
#endif /* TX_ATOMIC_FALLBACK_FUNCS */

EPIPUTMSG(/* int n, CONST char *fn, CONST char *fmt, va_list args */)
{
  /* WTF `fmt' might use htpf()-only codes like `%wd'; we cannot link it: */
  if ((n/100)*100 == MERR)
    fprintf(stderr, "\nError: ");
  else if ((n/100)*100 == MWARN)
    fprintf(stderr, "\nWarning: ");
  else
    fprintf(stderr, "\nNote: ");

  vfprintf(stderr, fmt, args);
  if (fn) fprintf(stderr, " in the function %s", fn);
  fprintf(stderr, "\n");
  fflush(stderr);
  return(0);
}
}

/* ------------------------------------------------------------------------ */

/* Separate static volatile vars to avoid optimizer shenanigans: */
static TXATOMINT        x, *px;

#ifdef TEST_WIDE
static TXATOMINT_WIDE   atomWide, *atomWidePtr;
#endif /* TEST_WIDE */

static int TXatomRequiredTests ARGS((void));
static int
TXatomRequiredTests()
/* Tests required sysdep.c behaviors: add/sub/inc/dec work correctly
 * at least for single threads.
 * Returns 0 on error.
 */
{
  TXATOMINT             res;
  VOLATILE int          ret = 1, testOk;
  TXATOMINT             addVal, addValOrg, subVal, subValOrg;
#ifdef TEST_WIDE
  TXATOMINT_WIDE        resWide;
#endif /* TEST_WIDE */

  /* Pick random values for the add/subtract tests, for more stress: */
  srand((unsigned)time(NULL));
  do
    {
      addVal = (TXATOMINT_NV)rand();
      if (addVal > (TXATOMINT_NV)TXATOMINT_MAX)
        addVal %= (TXATOMINT_NV)TXATOMINT_MAX;
#if TXATOMINTBITS >= EPI_OS_LONG_BITS
      if (addVal > (TXATOMINT_NV)EPI_OS_LONG_MAX)
        addVal %= (TXATOMINT_NV)EPI_OS_LONG_MAX;
#endif /* TXATOMBITS >= EPI_OS_LONG_BITS */
    }
  while (addVal <= 1);
  addValOrg = addVal;
  do
    {
      subVal = (TXATOMINT_NV)rand();
      if (subVal > (TXATOMINT_NV)TXATOMINT_MAX)
        subVal %= (TXATOMINT_NV)TXATOMINT_MAX;
#if TXATOMINTBITS >= EPI_OS_LONG_BITS
      if (subVal > (TXATOMINT_NV)EPI_OS_LONG_MAX)
        subVal %= (TXATOMINT_NV)EPI_OS_LONG_MAX;
#endif /* TXATOMBITS >= EPI_OS_LONG_BITS */
    }
  while (subVal <= 1);
  subValOrg = subVal;

  px = &x;
  *px = 0;
  printf("Atomic required tests (implementation: %s)\n",
         TX_ATOMIC_IMPLEMENTATION_STR);
  printf("Start: atom x = %ld  add-val = %ld  sub-val = %ld\n",
         (long)*px, (long)addVal, (long)subVal);
  fflush(stdout);

  testOk = 1;
  res = TX_ATOMIC_INC(px);
  if (px != &x) { printf("*** inc modified px ***\n"); ret = testOk=0; px=&x;}
  printf("after inc atom x = %ld", (long)*px);
  if (*px != 1) { printf(" != 1 *** error ***"); ret = testOk = 0; }
  printf(" org = %ld", (long)res);
  if (res != 0) { printf(" != 0 *** error ***"); ret = testOk = 0; }
  printf("%s\n", (testOk ? " ok" : ""));
  fflush(stdout);

  testOk = 1;
  res = TX_ATOMIC_DEC(px);
  if (px != &x) { printf("*** dec modified px ***\n"); ret = testOk=0; px=&x;}
  printf("after dec atom x = %ld", (long)*px);
  if (*px != 0) { printf(" != 0 *** error ***"); ret = testOk = 0; }
  printf(" org = %ld", (long)res);
  if (res != 1) { printf(" != 1 *** error ***"); ret = testOk = 0; }
  printf("%s\n", (testOk ? " ok" : ""));
  fflush(stdout);

  /* ---------------------------------------------------------------------- */
  /* Add, then subtract more (e.g. result negative, overflow): may fail
   * with some implementations (Itanium):
   */
#define ADDVAL1 123
#define SUBVAL1 321
  testOk = 1;
  *px = (TXATOMINT_NV)0;
  res = TX_ATOMIC_ADD(px, ADDVAL1);
  if (px != &x) { printf("*** add modified px ***\n"); ret = testOk=0; px=&x;}
  printf("after add %ld atom x = %ld", (long)ADDVAL1, (long)*px);
  if (*px != ADDVAL1)
    {
      printf(" != %ld *** error ***", (long)ADDVAL1);
      ret = testOk = 0;
    }
  printf(" org = %ld", (long)res);
  if (res != 0) { printf(" != 0 *** error ***"); ret = testOk = 0; }
  printf("%s\n", (testOk ? " ok" : ""));
  fflush(stdout);

  testOk = 1;
  res = TX_ATOMIC_SUB(px, SUBVAL1);
  if (px != &x) { printf("*** sub modified px ***\n"); ret = testOk=0; px=&x;}
  printf("after sub %ld atom x = %ld", (long)SUBVAL1, (long)*px);
  if (*px != (ADDVAL1 - SUBVAL1))
    {
      printf(" != %ld *** error ***", (long)(ADDVAL1 - SUBVAL1));
      ret = testOk = 0;
    }
  printf(" org = %ld", (long)res);
  if (res != ADDVAL1)
    {
      printf(" != %ld *** error ***", (long)ADDVAL1);
      ret = testOk = 0;
    }
  printf("%s\n", (testOk ? " ok" : ""));
  fflush(stdout);

  /* ---------------------------------------------------------------------- */
  /* Add/subtract random values: */
  testOk = 1;
  *px = (TXATOMINT_NV)0;
  res = TX_ATOMIC_ADD(px, addVal);
  if (px != &x) { printf("*** add modified px ***\n"); ret = testOk=0; px=&x;}
  if (addVal != addValOrg)
    {
      printf("*** add modified addVal from %ld to %ld ***\n",
             (long)addValOrg, (long)addVal);
      addVal = addValOrg;
      ret = testOk = 0;
    }
  printf("after add %ld atom x = %ld", (long)addVal, (long)*px);
  if (*px != addVal)
    {
      printf(" != %ld *** error ***", (long)addVal);
      ret = testOk = 0;
    }
  printf(" org = %ld", (long)res);
  if (res != 0) { printf(" != 0 *** error ***"); ret = 0; }
  printf("%s\n", (testOk ? " ok" : ""));
  fflush(stdout);

  testOk = 1;
  res = TX_ATOMIC_SUB(px, subVal);
  if (px != &x) { printf("*** sub modified px ***\n"); ret = testOk=0; px=&x;}
  if (subVal != subValOrg)
    {
      printf("*** sub modified subVal from %ld to %ld ***\n",
             (long)subValOrg, (long)subVal);
      subVal = subValOrg;
      ret = testOk = 0;
    }
  printf("after sub %ld atom x = %ld", (long)subVal, (long)*px);
  if (*px != (addVal - subVal))
    {
      printf(" != %ld *** error ***", (long)(addVal - subVal));
      ret = testOk = 0;
    }
  printf(" org = %ld", (long)res);
  if (res != addVal)
    {
      printf(" != %ld *** error ***", (long)addVal);
      ret = testOk = 0;
    }
  printf("%s\n", (testOk ? " ok" : ""));
  fflush(stdout);

  /* ---------------------------------------------------------------------- */
  /* TX_ATOMIC_COMPARE_AND_SWAP() tests:
   */
#define ORGVAL  123
#define ORGVAL2 456
#define NEWVAL  789
  testOk = 1;
  *px = (TXATOMINT_NV)ORGVAL;
  printf("Start: atom x = %ld\n", (long)*px);
  res = TX_ATOMIC_COMPARE_AND_SWAP(px, ORGVAL, NEWVAL);
  if (px != &x)
    {
      printf("*** compare-and-swap modified px: px != &x ***\n");
      ret = testOk = 0;
      px = &x;
    }
  printf("after compare-and-swap(if %ld, set %ld) atom x = %ld",
         (long)ORGVAL, (long)NEWVAL, (long)*px);
  if (*px == NEWVAL)
    printf(" ok");
  else
    {
      printf(" != %ld *** error ***", (long)NEWVAL);
      ret = testOk = 0;
    }
  printf(" org = %ld", (long)res);
  if (res == ORGVAL)
    printf(" ok");
  else
    {
      printf(" != %ld *** error ***", (long)ORGVAL);
      ret = testOk = 0;
    }
  printf("\n");
  fflush(stdout);

  testOk = 1;
  *px = (TXATOMINT_NV)ORGVAL2;
  printf("Start: atom x = %ld\n", (long)*px);
  res = TX_ATOMIC_COMPARE_AND_SWAP(px, ORGVAL, NEWVAL);
  if (px != &x)
    {
      printf("*** compare-and-swap modified px: px != &x ***\n");
      ret = testOk = 0;
      px = &x;
    }
  printf("after compare-and-swap(if %ld, set %ld) atom x = %ld",
         (long)ORGVAL, (long)NEWVAL, (long)*px);
  if (*px == ORGVAL2)
    printf(" ok");
  else
    {
      printf(" != %ld *** error ***", (long)ORGVAL2);
      ret = testOk = 0;
    }
  printf(" org = %ld", (long)res);
  if (res == ORGVAL2)
    printf(" ok");
  else
    {
      printf(" != %ld *** error ***", (long)ORGVAL2);
      ret = testOk = 0;
    }
  printf("\n");
  fflush(stdout);
#undef ORGVAL
#undef ORGVAL2
#undef NEWVAL

#ifdef TEST_WIDE
  /* ---------------------------------------------------------------------- */
  /* TX_ATOMIC_COMPARE_AND_SWAP_WIDE() tests:
   */
  /* Having a negative value in the lower 32 bits of ORGGVAL also
   * tests for erroneous sign-bit-extension in the gcc 2.95 inline
   * assembly version of TX_ATOMIC_COMPARE_AND_SWAP_WIDE():
   */
#define ORGVAL  0x12345678abcdef01LL
#define ORGVAL2 0x187654321fedbca1LL
#define NEWVAL  0x7eadBeefFeedDeedLL
  atomWidePtr = &atomWide;
  testOk = 1;
  *atomWidePtr = (TXATOMINT_WIDE_NV)ORGVAL;
  printf("Start: atom wide = 0x%llx\n", (long long)*atomWidePtr);
  resWide = TX_ATOMIC_COMPARE_AND_SWAP_WIDE(atomWidePtr, ORGVAL, NEWVAL);
  if (atomWidePtr != &atomWide)
    {
      printf("*** compare-and-swap-wide modified pwide: pwide != &wide ***\n");
      ret = testOk = 0;
      atomWidePtr = &atomWide;
    }
  printf("after compare-and-swap-wide(if 0x%llx, set 0x%llx)\n"
         "  atom wide = 0x%llx",
         (long long)ORGVAL, (long long)NEWVAL, (long long)*atomWidePtr);
  if (*atomWidePtr == NEWVAL)
    printf(" ok");
  else
    {
      printf(" != 0x%llx *** error ***", (long long)NEWVAL);
      ret = testOk = 0;
    }
  printf(" org = 0x%llx", (long long)resWide);
  if (resWide == ORGVAL)
    printf(" ok");
  else
    {
      printf(" != 0x%llx *** error ***", (long long)ORGVAL);
      ret = testOk = 0;
    }
  printf("\n");
  fflush(stdout);

  testOk = 1;
  *atomWidePtr = (TXATOMINT_WIDE_NV)ORGVAL2;
  printf("Start: atom wide = 0x%llx\n", (long long)*atomWidePtr);
  resWide = TX_ATOMIC_COMPARE_AND_SWAP_WIDE(atomWidePtr, ORGVAL, NEWVAL);
  if (atomWidePtr != &atomWide)
    {
      printf("*** compare-and-swap-wide modified pWide: pWide != &wide ***\n");
      ret = testOk = 0;
      atomWidePtr = &atomWide;
    }
  printf("after compare-and-swap-wide(if 0x%llx, set 0x%llx)\n"
         "  atom wide = 0x%llx",
         (long long)ORGVAL, (long long)NEWVAL, (long long)*atomWidePtr);
  if (*atomWidePtr == ORGVAL2)
    printf(" ok");
  else
    {
      printf(" != 0x%llx *** error ***", (long long)ORGVAL2);
      ret = testOk = 0;
    }
  printf(" org = 0x%llx", (long long)resWide);
  if (resWide == ORGVAL2)
    printf(" ok");
  else
    {
      printf(" != 0x%llx *** error ***", (long long)ORGVAL2);
      ret = testOk = 0;
    }
  printf("\n");
  fflush(stdout);
#endif /* TEST_WIDE */

  /* ---------------------------------------------------------------------- */

  printf(ret ? "Required atom tests passed\n" :
         "*** Required atom tests failed ***\n");
  fflush(stdout);
  return(ret);
}

/* ------------------------------------------------------------------------- */

#define ATOM_TOTAL      100000000

static void do_nothing ARGS((TXATOMINT *val));
static void
do_nothing(val)
TXATOMINT       *val;
/* Dummy function used to throw off optimizers
 */
{
  (void)val;
}

#ifdef TEST_WIDE
static void
do_nothing_wide(TXATOMINT_WIDE *val)
/* Dummy function used to throw off optimizers
 */
{
}
#endif /* TEST_WIDE */

static TXTHREADRET TXTHREADPFX atom_thread ARGS((void *arg));
static TXTHREADRET TXTHREADPFX
atom_thread(arg)
void    *arg;
{
  int           i;
  TXATOMINT     *val = (TXATOMINT *)arg;

  /* Net change of 0: */
  for (i = 0; i < ATOM_TOTAL/4; i++)
    {
      (void)TX_ATOMIC_INC(val);
      do_nothing(val);
      (void)TX_ATOMIC_DEC(val);
      do_nothing(val);
    }

  /* Net change of +ATOM_TOTAL/4: */
  for (i = 0; i < ATOM_TOTAL/4; i++)
    {
      (void)TX_ATOMIC_INC(val);
      do_nothing(val);
      if (i % (ATOM_TOTAL/10) == 0)     /* progress meter */
        {
          printf("b");
          fflush(stdout);
        }
    }

  /* Net change of 0: */
  for (i = 0; i < ATOM_TOTAL/4; i++)
    {
      (void)TX_ATOMIC_ADD(val, 37);
      do_nothing(val);
      (void)TX_ATOMIC_SUB(val, 37);
      do_nothing(val);
    }

  /* Net change of +ATOM_TOTAL/4: */
  for (i = 0; i < ATOM_TOTAL/20; i++)
    {
      (void)TX_ATOMIC_ADD(val, 5);
      do_nothing(val);
      if (i % (ATOM_TOTAL/10) == 0)     /* progress meter */
        {
          printf("b");
          fflush(stdout);
        }
    }

  return(0);
}

typedef struct SwapInfo_tag
{
  TXATOMINT     *valPtr;
  TXATOMINT     incCount;
  char          letter;
}
SwapInfo;

static TXTHREADRET TXTHREADPFX
atom_thread_compare_and_swap(void *arg)
{
  int           i;
  SwapInfo      *info = (SwapInfo *)arg;
  TXATOMINT     myOrgGuess = 0, res;

  for (i = 0; *info->valPtr < ATOM_TOTAL; i++)
    {
      /* Use compare-and-swap like an increment: */
      res = TX_ATOMIC_COMPARE_AND_SWAP(info->valPtr, myOrgGuess,
                                       myOrgGuess + 1);
      if (res == myOrgGuess) info->incCount++;  /* we inc'd it */
      /* Set new guess.
       *   If we inc'd it, guess is `myOrgGuess + 1' == `res + 1'.
       *   If other thread inc'd it, guess is `res + 1'.
       *   If nobody inc'd it (we guessed wrong), guess is `res + 0'.
       *   Split the difference by alternating between +1 and +0:
       */
      myOrgGuess = res + (i % 2);
      /* Bugfix: do not inc past ATOM_TOTAL, or test fails: */
      if (myOrgGuess >= ATOM_TOTAL)
        myOrgGuess = ATOM_TOTAL - 1;

      do_nothing(info->valPtr);
      if (i % (ATOM_TOTAL/10) == 0)             /* progress meter */
        {
          printf("%c", info->letter);
          fflush(stdout);
        }
    }
  return(0);
}

#ifdef TEST_WIDE
typedef struct SwapInfoWide_tag
{
  TXATOMINT_WIDE        *valPtr;
  TXATOMINT             incCount;
  char                  letter;
}
SwapInfoWide;

static TXTHREADRET TXTHREADPFX
atom_thread_compare_and_swap_wide(void *arg)
{
  int                   i;
  SwapInfoWide          *info = (SwapInfoWide *)arg;
  TXATOMINT_WIDE        myOrgGuess = 0, res;

  for (i = 0; *info->valPtr < (TXATOMINT_WIDE_NV)ATOM_TOTAL; i++)
    {
      /* Use compare-and-swap like an increment: */
      res = TX_ATOMIC_COMPARE_AND_SWAP_WIDE(info->valPtr, myOrgGuess,
                                            myOrgGuess + 1);
      if (res == myOrgGuess) info->incCount++;  /* we inc'd it */
      /* Set new guess.
       *   If we inc'd it, guess is `myOrgGuess + 1' == `res + 1'.
       *   If other thread inc'd it, guess is `res + 1'.
       *   If nobody inc'd it (we guessed wrong), guess is `res + 0'.
       *   Split the difference by alternating between +1 and +0:
       */
      myOrgGuess = res + ((TXATOMINT_WIDE_NV)i % (TXATOMINT_WIDE_NV)2);
      /* Bugfix: do not inc past ATOM_TOTAL, or test fails: */
      if (myOrgGuess >= ATOM_TOTAL)
        myOrgGuess = (TXATOMINT_WIDE)(ATOM_TOTAL - 1);

      do_nothing_wide(info->valPtr);
      if (i % (ATOM_TOTAL/10) == 0)             /* progress meter */
        {
          printf("%c", info->letter);
          fflush(stdout);
        }
    }
  return(0);
}
#endif /* TEST_WIDE */

static int TXatomThreadSafenessTests ARGS((void));
static int
TXatomThreadSafenessTests()
/* Returns 0 on error.
 */
{
  int                   i, ret = 1, numCompareAndSwapTestRuns = 0;
  TXTHREAD              thread = TXTHREAD_NULL;
  TXTHREADRET           xit;
  PID_T                 pid = 0;
  int                   shmh = -1;
#ifndef _WIN32
  void                  *shmAddr = (void *)(-1);
  struct shmid_ds       ds;
#endif /* !_WIN32 */
  double                start, per;
  SwapInfo              swapFg, swapBg;
#ifdef TEST_WIDE
  SwapInfoWide          swapWideFg, swapWideBg;
#endif /* TEST_WIDE */

  printf("Atomic thread-safeness test (implementation: %s)\n",
         TX_ATOMIC_IMPLEMENTATION_STR);
  printf("Running...");
  fflush(stdout);
  start = (double)time(NULL);

  px = &x;
  *px = 0;

  /* WTF threads blow up? */
  if (!TXcreatethread(TXPMBUFPN, "atom", atom_thread, (void *)px,
                      (TXTHREADFLAG)0, 0, &thread))
    {
#ifdef _WIN32
      printf("*** Cannot create thread for atom tests ***\n");
      return(0);
#else /* !_WIN32 */
      printf("* Cannot create thread for atom tests, trying shm and fork *\n");
      fflush(stdout);
      shmh = shmget(0x12345678, 8192, (IPC_CREAT | IPC_EXCL | 0666));
      if (shmh == -1)
        {
          printf("*** shmget() failed: %s ***\n", strerror(errno));
          fflush(stdout);
          return(0);
        }
      shmAddr = shmat(shmh, NULL, 0666);
      if (shmAddr == (void *)(-1))
        {
          printf("*** shmat() failed: %s ***\n", strerror(errno));
          fflush(stdout);
          ret = 0;
          goto rmid;
        }
      px = (TXATOMINT *)shmAddr;
      *px = 0;
      switch (pid = fork())
        {
        case 0:         /* child */
          atom_thread((void *)px);
          _exit(0);
        case -1:        /* error */
          printf("*** fork() failed: %s ***\n", strerror(errno));
          fflush(stdout);
          ret = 0;
          shmdt(shmAddr);
        rmid:
          shmctl(shmh, IPC_RMID, &ds);
          return(ret);
        default:        /* parent */
          break;
        }
#endif /* !_WIN32 */
    }

  for (i = 0; i < ATOM_TOTAL/4; i++)
    {
      (void)TX_ATOMIC_INC(px);
      do_nothing(px);
      (void)TX_ATOMIC_DEC(px);
      do_nothing(px);
    }
  for (i = 0; i < ATOM_TOTAL/4; i++)
    {
      (void)TX_ATOMIC_INC(px);
      do_nothing(px);
      if (i % (ATOM_TOTAL/10) == 0)     /* progress meter */
        {
          printf("F");
          fflush(stdout);
        }
    }
  for (i = 0; i < ATOM_TOTAL/4; i++)
    {
      (void)TX_ATOMIC_ADD(px, 1);
      do_nothing(px);
      (void)TX_ATOMIC_SUB(px, 1);
      do_nothing(px);
    }
  for (i = 0; i < ATOM_TOTAL/4; i++)
    {
      (void)TX_ATOMIC_ADD(px, 1);
      do_nothing(px);
      if (i % (ATOM_TOTAL/10) == 0)     /* progress meter */
        {
          printf("F");
          fflush(stdout);
        }
    }

#ifndef _WIN32
  if (shmAddr != (void *)(-1))
    {
      waitpid(pid, &i, 0);
      x = *px;                          /* copy from shared to local */
      shmdt(shmAddr);
      shmctl(shmh, IPC_RMID, &ds);
    }
  else
#endif /* !_WIN32 */
    {
      TXwaitforthreadexit(TXPMBUFPN, thread, -1.0, &xit);
      x = *px;                          /* copy from shared to local */
    }
  per = (double)time(NULL) - start;
  if (per > (double)0.0) per = (((double)ATOM_TOTAL)/per)/(double)1000000.0;
  else per = (double)0.0;
  fflush(stdout);
  printf(" done (%1.0f M iterations/sec)\n", (double)per);
  if ((int)x != ATOM_TOTAL)
    {
      printf("*** End: atom x = %ld != expected %ld: error ***\n",
             (long)x, (long)ATOM_TOTAL);
      ret = 0;
    }
  else
    printf("End: atom x = %ld ok\n", (long)x);
  fflush(stdout);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  /* Compare-and-swap tests: */

doCompareAndSwapTest:
  if (numCompareAndSwapTestRuns++ >= 3)         /* sanity check */
    {
      printf("*** Too many compare-and-swap test runs ***\n");
      return(0);
    }

  start = (double)time(NULL);
  px = &x;
  *px = 0;

  /* WTF threads blow up? */
  printf("Compare-and-swap...");
  fflush(stdout);
  memset(&swapFg, 0, sizeof(SwapInfo));
  memset(&swapBg, 0, sizeof(SwapInfo));
  swapFg.valPtr = swapBg.valPtr = px;
  swapFg.incCount = swapBg.incCount = 0;
  swapFg.letter = 'F';  swapBg.letter = 'b';
  if (!TXcreatethread(TXPMBUFPN, "compareAndSwap",
                      atom_thread_compare_and_swap, (void *)&swapBg,
                      (TXTHREADFLAG)0, 0, &thread))
    {
#ifdef _WIN32
      printf("*** Cannot create second thread for atom tests ***\n");
      return(0);
#else /* !_WIN32 */
      printf("* Cannot create thread for atom tests, trying shm and fork *\n");
      fflush(stdout);
      shmh = shmget(0x12345678, 8192, (IPC_CREAT | IPC_EXCL | 0666));
      if (shmh == -1)
        {
          printf("*** shmget() failed: %s ***\n", strerror(errno));
          fflush(stdout);
          return(0);
        }
      shmAddr = shmat(shmh, NULL, 0666);
      if (shmAddr == (void *)(-1))
        {
          printf("*** shmat() failed: %s ***\n", strerror(errno));
          fflush(stdout);
          ret = 0;
          goto rmid2;
        }
      px = (TXATOMINT *)shmAddr;
      *px = 0;
      swapFg.valPtr = swapBg.valPtr = px;
      switch (pid = fork())
        {
        case 0:         /* child */
          atom_thread_compare_and_swap((void *)&swapBg);
          _exit(0);
        case -1:        /* error */
          printf("*** fork() failed: %s ***\n", strerror(errno));
          fflush(stdout);
          ret = 0;
          shmdt(shmAddr);
        rmid2:
          shmctl(shmh, IPC_RMID, &ds);
          return(ret);
        default:        /* parent */
          break;
        }
#endif /* !_WIN32 */
    }

  /* Do foreground part of test: */
  atom_thread_compare_and_swap((void *)&swapFg);

  /* Wait for background: */
#ifndef _WIN32
  if (shmAddr != (void *)(-1))
    {
      waitpid(pid, &i, 0);
      x = *px;                          /* copy from shared to local */
      shmdt(shmAddr);
      shmctl(shmh, IPC_RMID, &ds);
    }
  else
#endif /* !_WIN32 */
    {
      TXwaitforthreadexit(TXPMBUFPN, thread, -1.0, &xit);
      x = *px;                          /* copy from shared to local */
    }

  /* Report results: */
  per = (double)time(NULL) - start;
  if (per > (double)0.0) per = (((double)ATOM_TOTAL)/per)/(double)1000000.0;
  else per = (double)0.0;
  fflush(stdout);
  printf(" done (%1.0f M iterations/sec)\n", (double)per);
  if ((int)x != ATOM_TOTAL)
    {
      printf("*** End: atom x = %ld != expected %ld: error ***\n",
             (long)x, (long)ATOM_TOTAL);
      ret = 0;
    }
  else
    printf("End: atom x = %ld ok\n", (long)x);
  printf("increments: fg %ld + bg %ld = total %ld",
         (long)swapFg.incCount, (long)swapBg.incCount,
         (long)(swapFg.incCount + swapBg.incCount));
  if (swapFg.incCount + swapBg.incCount != ATOM_TOTAL)
    {
      printf(" != %ld *** error ***\n", (long)ATOM_TOTAL);
      ret = 0;
    }
  else
    printf(" = %ld ok\n", (long)ATOM_TOTAL);
  /* Make sure one thread didn't monopolize, thwarting concurrency.
   * If low concurrency, implementation might still be thread-safe; we
   * just don't know for sure.  Re-run rather than fail:
   */
  if (swapFg.incCount < ATOM_TOTAL/10)
    {
      printf("* fg count low: should be > ~%ld for concurrency; re-running *\n",
             (long)(ATOM_TOTAL/10));
      goto doCompareAndSwapTest;
    }
  if (swapBg.incCount < ATOM_TOTAL/10)
    {
      printf("* bg count low: should be > ~%ld for concurrency; re-running *\n",
             (long)(ATOM_TOTAL/10));
      goto doCompareAndSwapTest;
    }
  fflush(stdout);

#ifdef TEST_WIDE
  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  /* Compare-and-swap-wide thread-safeness tests: */

doCompareAndSwapWideTest:
  start = (double)time(NULL);
  atomWidePtr = &atomWide;
  atomWide = 0;

  /* WTF threads blow up? */
  printf("Compare-and-swap-wide...");
  fflush(stdout);
  memset(&swapWideFg, 0, sizeof(SwapInfoWide));
  memset(&swapWideBg, 0, sizeof(SwapInfoWide));
  swapWideFg.valPtr = swapWideBg.valPtr = atomWidePtr;
  swapWideFg.incCount = swapWideBg.incCount = 0;
  swapWideFg.letter = 'F';  swapWideBg.letter = 'b';
  if (!TXcreatethread(atom_thread_compare_and_swap_wide, (void *)&swapWideBg,
                      (TXTHREADFLAG)0, 0, &thread))
    {
#ifdef _WIN32
      printf("*** Cannot create second thread for atom tests ***\n");
      return(0);
#else /* !_WIN32 */
      printf("* Cannot create thread for atom tests, trying shm and fork *\n");
      fflush(stdout);
      shmh = shmget(0x12345678, 8192, (IPC_CREAT | IPC_EXCL | 0666));
      if (shmh == -1)
        {
          printf("*** shmget() failed: %s ***\n", strerror(errno));
          fflush(stdout);
          return(0);
        }
      shmAddr = shmat(shmh, NULL, 0666);
      if (shmAddr == (void *)(-1))
        {
          printf("*** shmat() failed: %s ***\n", strerror(errno));
          fflush(stdout);
          ret = 0;
          goto rmidSwapWide;
        }
      atomWidePtr = (TXATOMINT_WIDE *)shmAddr;
      *atomWidePtr = 0;
      swapWideFg.valPtr = swapWideBg.valPtr = atomWidePtr;
      switch (pid = fork())
        {
        case 0:         /* child */
          atom_thread_compare_and_swap_wide((void *)&swapWideBg);
          _exit(0);
        case -1:        /* error */
          printf("*** fork() failed: %s ***\n", strerror(errno));
          fflush(stdout);
          ret = 0;
          shmdt(shmAddr);
        rmidSwapWide:
          shmctl(shmh, IPC_RMID, &ds);
          return(ret);
        default:        /* parent */
          break;
        }
#endif /* !_WIN32 */
    }

  /* Do foreground part of test: */
  atom_thread_compare_and_swap_wide((void *)&swapWideFg);

  /* Wait for background: */
#ifndef _WIN32
  if (shmAddr != (void *)(-1))
    {
      waitpid(pid, &i, 0);
      atomWide = *atomWidePtr;          /* copy from shared to local */
      shmdt(shmAddr);
      shmctl(shmh, IPC_RMID, &ds);
    }
  else
#endif /* !_WIN32 */
    {
      TXwaitforthreadexit(TXPMBUFPN, thread, -1.0, &xit);
      atomWide = *atomWidePtr;          /* copy from shared to local */
    }

  /* Report results: */
  per = (double)time(NULL) - start;
  if (per > (double)0.0) per = (((double)ATOM_TOTAL)/per)/(double)1000000.0;
  else per = (double)0.0;
  fflush(stdout);
  printf(" done (%1.0f M iterations/sec)\n", (double)per);
  if ((int)atomWide != ATOM_TOTAL)
    {
      printf("*** End: atom wide = %ld != expected %ld: error ***\n",
             (long)atomWide, (long)ATOM_TOTAL);
      ret = 0;
    }
  else
    printf("End: atom wide = %ld ok\n", (long)atomWide);
  printf("increments: fg %ld + bg %ld = total %ld",
         (long)swapWideFg.incCount, (long)swapWideBg.incCount,
         (long)(swapWideFg.incCount + swapWideBg.incCount));
  if (swapWideFg.incCount + swapWideBg.incCount != ATOM_TOTAL)
    {
      printf(" != %ld *** error ***\n", (long)ATOM_TOTAL);
      ret = 0;
    }
  else
    printf(" = %ld ok\n", (long)ATOM_TOTAL);
  /* Make sure one thread didn't monopolize, thwarting concurrency.
   * If low concurrency, implementation might still be thread-safe; we
   * just don't know for sure.  Re-run rather than fail:
   */
  if (swapWideFg.incCount < ATOM_TOTAL/10)
    {
      printf("* fg count low: should be > ~%ld for concurrency; re-running *\n",
             (long)(ATOM_TOTAL/10));
      goto doCompareAndSwapWideTest;
    }
  if (swapWideBg.incCount < ATOM_TOTAL/10)
    {
      printf("* bg count low: should be > ~%ld for concurrency; re-running *\n",
             (long)(ATOM_TOTAL/10));
      goto doCompareAndSwapWideTest;
    }
  fflush(stdout);
#endif /* TEST_WIDE */

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  /* Final report: */

  if (ret)
    printf("Atomic thread-safeness tests passed\n");
  else
    printf("*** Atomic thread-safeness tests failed ***\n");

  return(ret);
}

/* ------------------------------------------------------------------------- */

int main ARGS((int argc, char *argv[]));
int
main(argc, argv)
int argc;
char *argv[];
/* Usage: atomicTests required|threadsafeness.
 * Returns 0 if specified tests fail.
 */
{
  int   res;

  if (argc != 2)
    {
      printf("Usage: %s required|threadsafeness\n", argv[0]);
      exit(1);
    }
  if (strcmp(argv[1], "required") == 0)
    res = TXatomRequiredTests();
  else if (strcmp(argv[1], "threadsafeness") == 0)
    res = TXatomThreadSafenessTests();
  else
    {
      printf("Unknown test `%s'\n", argv[1]);
      res = 0;
    }
  return(res ? 0 : 1);
}
