#include "txcoreconfig.h"
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#ifdef EPI_HAVE_MMAP
#  include <sys/mman.h>
#endif /* EPI_HAVE_MMAP */
#include <fcntl.h>
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"


#ifdef MEMDEBUG
#  undef TXfree
#  undef TXdupStrList
#  undef TXfreeStrList
#  undef TXdupStrEmptyTermList
#  undef TXfreeStrEmptyTermList
#  undef TXfreeArrayOfStrLists
#  undef TXmalloc
#  undef TXcalloc
#  undef TXrealloc
#  undef TXstrdup
#  undef TXstrndup
#  undef TXexpandArray
#  undef TXallocProtectable
#  undef TXfreeProtectable
#  undef TXprotectMem
#  undef TXsharedBufOpen
#  undef TXsharedBufClose
#  define FUNC(non, debug)	debug
#else /* !MEMDEBUG */
#  define FUNC(non, debug)	non
#endif /* !MEMDEBUG */

static TXATOMINT                TXmemAllocFailures = 0;
#ifdef EPI_TRACK_MEM
EPI_HUGEINT                     TXmemCurTotalAlloced = 0;
#endif /* EPI_TRACK_MEM */
static TXATOMINT                TXmemSysFuncDepth = 0;
/* Could make a stack for `TXmemUsingFunc' in case of threads but
 * not worth the hassle; just know that value may be stomped by
 * other thread:
 */
static const char               *TXmemUsingFuncs[3];
#define TX_MEM_SYS_FUNC_ENTER(fn)                                       \
  {                                                                     \
    TXATOMINT   idx;                                                    \
    idx = TX_ATOMIC_INC(&TXmemSysFuncDepth);                            \
    if (idx >= 0 && (size_t)idx < TX_ARRAY_LEN(TXmemUsingFuncs))        \
      TXmemUsingFuncs[idx] = (fn);                                      \
  }
#define TX_MEM_SYS_FUNC_EXIT()                                          \
  {                                                                     \
    TXATOMINT   idx;                                                    \
    idx = TX_ATOMIC_DEC(&TXmemSysFuncDepth) - 1;                        \
    if (idx >= 0 && (size_t)idx < TX_ARRAY_LEN(TXmemUsingFuncs))        \
      TXmemUsingFuncs[idx] = NULL;                                      \
  }


#ifdef TX_ENABLE_MEM_PROTECT
/* ------------------------ Protected-mem routines ------------------------ */

/* Some old architectures have MAP_ANON not MAP_ANONYMOUS: */
#  if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#    define MAP_ANONYMOUS       MAP_ANON
#  endif

#  ifndef MAP_FAILED
#    define MAP_FAILED  ((void *)(-1))
#  endif

#  define TXMEMPROTECT_FILL_BYTE_BELOW  0xfc
#  define TXMEMPROTECT_FILL_BYTE_ABOVE  0xfd

typedef struct TXPMITEM_tag
{
  void                  *userPtr;               /* pointer returned to user */
  void                  *osPtr;                 /* OS-alloced pointer */
  size_t                osSz;                   /* OS-alloced size */
  TXMEMPROTECTFLAG      flags;                  /* alloc flags */
}
TXPMITEM;

static TXPMITEM *TXpmItems = NULL;
static size_t   TXnumPmItems = 0, TXnumAllocedPmItems = 0;
static size_t   TXpageSz = 0;
#  if defined(MAP_ANONYMOUS) || defined(_WIN32)
#    undef NEED_DEV_ZERO
#  else /* !MAP_ANONYMOUS && !_WIN32 */
static int      TXdevZeroFd = -1;
#    define NEED_DEV_ZERO       1
#  endif /* !MAP_ANONYMOUS && !_WIN32 */


static TXPMITEM *
TXpmFindProtectedBlock(void *userPtr)
/* Returns TXPMITEM * for user pointer `userPtr', or NULL if not found.
 */
{
  TXPMITEM      *item, *itemsEnd;

  if (!userPtr) return(NULL);
  for (item = TXpmItems, itemsEnd = TXpmItems + TXnumPmItems;
       item < itemsEnd && item->userPtr != userPtr;
       item++);
  if (item < itemsEnd) return(item);
  return(NULL);                                 /* not found */
}

static const char *
TXprotectOsMem(void *p, size_t sz, TXMEMPROTECTPERM perms)
/* Wrapper for OS call to alter memory protection.
 * `p'/`sz' must already be page aligned.
 * Returns NULL if ok, else error message (e.g. strerror()).
 */
{
#  ifdef EPI_HAVE_MMAP
  {
    int mprot = PROT_NONE;

    if (perms & TXMEMPROTECTPERM_READ) mprot |= PROT_READ;
    if (perms & TXMEMPROTECTPERM_WRITE) mprot |= PROT_WRITE;
    if (perms & TXMEMPROTECTPERM_EXEC) mprot |= PROT_EXEC;
    if (mprotect(p, sz, mprot) != 0)
      return(TXstrerror(TXgeterror()));
  }
#  elif defined(_WIN32)
  {
    DWORD       prot = 0, oldProt;

    switch (perms)
      {
      case TXMEMPROTECTPERM_NONE:
        prot = PAGE_NOACCESS;           break;
      case TXMEMPROTECTPERM_READ:
        prot = PAGE_READONLY;           break;
      case TXMEMPROTECTPERM_WRITE | TXMEMPROTECTPERM_READ:
        prot = PAGE_READWRITE;          break;
      case TXMEMPROTECTPERM_EXEC:
        prot = PAGE_EXECUTE;            break;
      case TXMEMPROTECTPERM_EXEC | TXMEMPROTECTPERM_READ:
        prot = PAGE_EXECUTE_READ;       break;
      case TXMEMPROTECTPERM_EXEC | TXMEMPROTECTPERM_WRITE | TXMEMPROTECTPERM_READ:
        prot = PAGE_EXECUTE_READWRITE;  break;
      default:
        return("TXMEMPROTECTPERM_... combination unsupported on this platform");
      }
    if (!VirtualProtect(p, sz, prot, &oldProt))
      return(TXstrerror(TXgeterror()));
  }
#  else /* !EPI_HAVE_MMAP && !_WIN32 */
  return(TXunsupportedPlatform);
#  endif /* !EPI_HAVE_MMAP && !_WIN32 */
  return(NULL);                                 /* success */
}

void *
TXallocProtectable(TXPMBUF *pmbuf, const char *fn, size_t sz,
                   TXMEMPROTECTFLAG flags TXALLOC_PROTO)
/* Allocates a block of protectable memory: caller can read/write-protect
 * the returned block with TXprotectMem().  Must be freed with
 * TXfreeProtectable(), not TXfree().
 * Returns a read/write-enabled block, or NULL on error.
 */
{
  void          *osPtr, *userPtr;
  size_t        userSz;                         /* size available to user */
  size_t        userExtraSz;                    /*extra user did not ask for*/
  size_t        osSz;                           /* size we alloc from OS */
  size_t        osSzBeforeDeadPages;
  size_t        slackBelowSz, slackAboveSz;
  size_t        fragSz;
  TXPMITEM      *item = NULL;
  const char    *errReason;
#  ifdef NEED_DEV_ZERO
  char          errBuf[512];
#  endif /* NEED_DEV_ZERO */

  /* `|' is page alignment, '<' or '>' is not:
   *
   *   |    dead page below   || ... N live pages ... ||  dead page above   |
   *   |------------------------------- osSz -------------------------------|
   * If TXMEMPROTECTFLAG_ALIGN_BELOW:
   *   |--- slackBelowSz -----||--- userSz -----><--------- slackAboveSz ---|
   * If not TXMEMPROTECTFLAG_ALIGN_BELOW:
   *   <--- slackBelowSz ---------><----- userSz -----||--- slackAboveSz ---|
   *
   * User pointer starts at start of `userSz'.
   */

  if (TXpageSz == 0)                            /* not set yet */
    {
      TXpageSz = TXpagesize();
      if (TXpageSz == 0) TXpageSz = 8192;       /* WAG */
    }

  /* Align user request to calloc() standards.  Byte alignment
   * requires no alignment; nor does align-below, which already aligns
   * to page size:
   */
  userSz = sz;
  userExtraSz = 0;
  if (!(flags & (TXMEMPROTECTFLAG_BYTE_ALIGN | TXMEMPROTECTFLAG_ALIGN_BELOW)))
    {
      if ((fragSz = userSz % TX_ALIGN_BYTES) > (size_t)0)
        {
          userExtraSz = TX_ALIGN_BYTES - fragSz;
          userSz += userExtraSz;
        }
    }

  /* Round up allocation to next page size (OS requirement): */
  osSz = userSz;
  if ((fragSz = osSz % TXpageSz) > (size_t)0)
    osSz += TXpageSz - fragSz;

  /* Add dead pages below and above, which will be made inaccessible
   * to trap overruns, ala Electric Fence.  Also compute slack space:
   */
  osSzBeforeDeadPages = osSz;
  slackBelowSz = slackAboveSz = 0;
  if (!(flags & TXMEMPROTECTFLAG_NO_DEAD_PAGE_BELOW))
    {
      osSz += TXpageSz;
      slackBelowSz += TXpageSz;
    }
  if (!(flags & TXMEMPROTECTFLAG_NO_DEAD_PAGE_ABOVE))
    {
      osSz += TXpageSz;
      slackAboveSz += TXpageSz;
    }
  if (flags & TXMEMPROTECTFLAG_ALIGN_BELOW)
    slackAboveSz += osSzBeforeDeadPages - userSz;
  else
    slackBelowSz += osSzBeforeDeadPages - userSz;

  if (osSz > (size_t)EPI_OS_SIZE_T_MAX || osSz < userSz)
    {
      errReason = "Too large";
    cannotAlloc:
      txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
                     "Cannot alloc %wu bytes of protectable memory: %s",
                     (EPI_HUGEUINT)userSz, errReason);
      goto err;
    }

#  ifdef NEED_DEV_ZERO
  /* Without MAP_ANONYMOUS, we need a file handle to pass to mmap(): */
  if (TXdevZeroFd < 0)
    {
      TXdevZeroFd = open("/dev/zero", O_RDWR, 0666);
      if (TXdevZeroFd < 0)
        {
          htsnpf(errBuf, sizeof(errBuf),
                 "Cannot open /dev/zero read/write: %s",
                 TXstrerror(TXgeterror()));
          errReason = errBuf;
          goto cannotAlloc;
        }
    }
#  endif /* NEED_DEV_ZERO */

#  define TXexpandArray(pmbuf, fn, array, allocedNum, incNum, elSz)     \
TXexpandArray(pmbuf, fn, array, allocedNum, incNum, elSz TXALLOC_ARGS_DEFAULT)
  /* We need to track the size of the block for later protect/frees,
   * and separately from the block because user may read-protect it:
   */
  if (!TX_INC_ARRAY(pmbuf, &TXpmItems, TXnumPmItems,&TXnumAllocedPmItems))
    goto err;
  item = TXpmItems + TXnumPmItems++;
#  undef TXexpandArray

#  ifdef EPI_HAVE_MMAP
  osPtr = (void *)mmap(0, osSz, (PROT_READ | PROT_WRITE),
#    ifdef MAP_ANONYMOUS
                       (MAP_PRIVATE | MAP_ANONYMOUS), -1,
#    else /* !MAP_ANONYMOUS */
                       MAP_PRIVATE, TXdevZeroFd,
#    endif /* !MAP_ANONYMOUS */
                       0);
  if (osPtr == (void *)MAP_FAILED)
    {
      errReason = TXstrerror(TXgeterror());
      goto cannotAlloc;
    }
#  elif defined(_WIN32)
  osPtr = VirtualAlloc(NULL, osSz, MEM_COMMIT, PAGE_READWRITE);
  if (osPtr == NULL)
    {
      errReason = TXstrerror(TXgeterror());
      goto cannotAlloc;
    }
#  else /* !EPI_HAVE_MMAP && !_WIN32 */
  errReason = TXunsupportedPlatform;
  goto cannotAlloc;
#  endif /* !EPI_HAVE_MMAP && !_WIN32 */

  /* Mark up slack space so user might see corruption if attempting access
   * of slack space portion that is not dead:
   */
  if (slackBelowSz > (size_t)0)
    memset(osPtr, TXMEMPROTECT_FILL_BYTE_BELOW, slackBelowSz);
  userPtr = (byte *)osPtr + slackBelowSz;
  if (userExtraSz > (size_t)0)
    memset((byte *)userPtr + userSz - userExtraSz,
           TXMEMPROTECT_FILL_BYTE_ABOVE, userExtraSz);
  if (slackAboveSz > (size_t)0)
    memset((byte *)osPtr + osSz - slackAboveSz, TXMEMPROTECT_FILL_BYTE_ABOVE,
           slackAboveSz);

  /* Protect the dead pages, to cause segfault on any access: */
  if (!(flags & TXMEMPROTECTFLAG_NO_DEAD_PAGE_BELOW))
    {
      errReason = TXprotectOsMem((byte *)osPtr, TXpageSz,
                                 TXMEMPROTECTPERM_NONE);
      if (errReason)
        txpmbuf_putmsg(pmbuf, MWARN + MAE, fn,
                       "Cannot protect dead page below protectable memory: %s; rest of alloc succeeded",
                       errReason);
    }
  if (!(flags & TXMEMPROTECTFLAG_NO_DEAD_PAGE_ABOVE))
    {
      errReason = TXprotectOsMem((byte *)osPtr + osSz - TXpageSz, TXpageSz,
                                 TXMEMPROTECTPERM_NONE);
      if (errReason)
        txpmbuf_putmsg(pmbuf, MWARN + MAE, fn,
                       "Cannot protect dead page above protectable memory: %s; rest of alloc succeeded",
                       errReason);
    }

  /* Fill in items array: */
  item->userPtr = userPtr;
  item->osPtr = osPtr;
  item->osSz = osSz;
  item->flags = flags;

  return(userPtr);

err:
  if (item) TXnumPmItems--;
  return(NULL);
}

int
TXfreeProtectable(TXPMBUF *pmbuf, void *p TXALLOC_PROTO)
/* Returns 0 on error.
 */
{
  static const char     fn[] = "TXfreeProtectable";
  int                   ok;
  TXPMITEM              *item, *itemsEnd;
  const char            *errReason = NULL;

  if (!p) return(1);
  item = TXpmFindProtectedBlock(p);
  if (!item)
    {
      txpmbuf_putmsg(pmbuf, MERR, fn,
                     "Cannot free %p: Not a valid protectable memory block",
                     p);
      return(0);
    }
#  ifdef EPI_HAVE_MMAP
  ok = (munmap(item->osPtr, item->osSz) == 0);
  if (!ok) errReason = TXstrerror(TXgeterror());
#  elif defined(_WIN32)
  ok = VirtualFree(item->osPtr, 0, MEM_RELEASE);
  if (!ok) errReason = TXstrerror(TXgeterror());
#  else /* !EPI_HAVE_MMAP && !_WIN32 */
  ok = 0;
  errReason = TXunsupportedPlatform;
#  endif /* !EPI_HAVE_MMAP && !_WIN32 */
  if (!ok)
    txpmbuf_putmsg(pmbuf, MERR, fn,
                   "Cannot free protectable memory block %p: %s",
                   p, errReason);
  /* Remove `item' from array: */
  itemsEnd = TXpmItems + TXnumPmItems;
  if (item < itemsEnd)
    memmove(item, item + 1, ((itemsEnd - item) - 1)*sizeof(TXPMITEM));
  TXnumPmItems--;
  return(ok);
}

int
TXprotectMem(TXPMBUF *pmbuf, void *p, TXMEMPROTECTPERM perms TXALLOC_PROTO)
/* Protects memory block `p' by only allowing `perms'.  `p' must have
 * previously been allocated with TXallocProtectable().
 * Returns 0 on error.
 */
{
  static const char     fn[] = "TXprotectMem";
  TXPMITEM              *item;
  const char            *errReason;
  byte                  *ptr;
  size_t                sz;

  item = TXpmFindProtectedBlock(p);
  if (!item)
    {
      errReason = "Not a valid protectable memory block";
      goto reportErr;
    }
  /* Change protection on all but the dead pages: */
  ptr = (byte *)item->osPtr;
  sz = item->osSz;
  if (!(item->flags & TXMEMPROTECTFLAG_NO_DEAD_PAGE_BELOW))
    ptr += TXpageSz;
  if (!(item->flags & TXMEMPROTECTFLAG_NO_DEAD_PAGE_ABOVE))
    sz -= TXpageSz;
  errReason = TXprotectOsMem(ptr, sz, perms);
  if (errReason)
    {
    reportErr:
      txpmbuf_putmsg(pmbuf, MERR, fn,
                     "Cannot protect memory block %p to allow %s/%s/%s: %s",
                     p, ((perms & TXMEMPROTECTPERM_READ) ? "read" : ""),
                     ((perms & TXMEMPROTECTPERM_WRITE) ? "write" : ""),
                     ((perms & TXMEMPROTECTPERM_EXEC) ? "exec" : ""),
                     errReason);
      return(0);                                /* failure */
    }
  return(1);                                    /* success */
}
#endif /* TX_ENABLE_MEM_PROTECT */

/* ------------------------- Normal alloc wrappers: ----------------------- */

void *
TXfree(void *p TXALLOC_PROTO)
/* Note: vxPutmsgBuf.c may call this in a critical section, and thus
 * assumes no putmsg() calls here.
 */
{
#ifdef TX_ENABLE_MEM_PROTECT
  static const char     fn[] = "TXfree";
  TXPMBUF               *pmbuf = TXPMBUFPN;     /* wtf parameter */
#endif /* TX_ENABLE_MEM_PROTECT */

	if(p)
	{
#ifdef TX_ENABLE_MEM_PROTECT
          /* TXallocProtectable() mem must be freed with TXfreeProtectable: */
          if (TXnumPmItems > 0 && TXpmFindProtectedBlock(p))
            {
              txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
                             "Cannot free %p: protectable memory block, use TXfreeProtectable()",
                             p);
              return(NULL);
            }
#endif /* TX_ENABLE_MEM_PROTECT */
#ifdef EPI_TRACK_MEM
#  ifdef _WIN32
		TXmemCurTotalAlloced -= (EPI_HUGEINT)_msize(p);
#  endif /* _WIN32 */
#endif /* EPI_TRACK_MEM */
                TX_MEM_SYS_FUNC_ENTER(__FUNCTION__);
		FUNC(free(p), mac_free(p, (char *)file, line, memo));
                TX_MEM_SYS_FUNC_EXIT();
	}
	return NULL;
}

size_t
TXcountStrList(list)
char    **list;         /* (in) NULL-terminated list to count */
{
  size_t        n;

  if (!list) return(0);
  for (n = 0; list[n] != CHARPN; n++);
  return(n);
}

char **
TXdupStrList(TXPMBUF *pmbuf, char **list, size_t n TXALLOC_PROTO)
{
  static const char     fn[] = "TXdupStrList";
  char                  **newList;
  size_t                i;

  if (n == (size_t)(-1) || TX_SIZE_T_VALUE_LESS_THAN_ZERO(n))
    for (n = 0; list[n] != CHARPN; n++);
  TX_MEM_SYS_FUNC_ENTER(__FUNCTION__);
  newList = (char **)FUNC(calloc(n + 1, sizeof(char *)),
                       mac_calloc(n + 1, sizeof(char *), (char *)file, line,
				  memo));
  TX_MEM_SYS_FUNC_EXIT();
  if (newList == CHARPPN)
    {
      TXputmsgOutOfMem(pmbuf, MERR + MAE, fn, n + 1, sizeof(char *));
      return(CHARPPN);
    }
  for (i = 0; i < n; i++)
    {
      if (list[i] == CHARPN)                    /* `list' has "holes" */
        newList[i] = CHARPN;
      else
        {
          TX_MEM_SYS_FUNC_ENTER(__FUNCTION__);
          newList[i] = FUNC(strdup(list[i]),
                            mac_strdup(list[i], (char *)file, line, memo));
          TX_MEM_SYS_FUNC_EXIT();
          if (newList[i] == CHARPN)
            {
              TXputmsgOutOfMem(pmbuf, MERR + MAE, fn, strlen(list[i]) + 1, 1);
              return(TXfreeStrList(newList, i TXALLOC_ARGS_PASSTHRU));
            }
        }
    }
  return(newList);
}

char **
TXfreeStrList(char **list, size_t n TXALLOC_PROTO)
{
	size_t	i;

	if (list != CHARPPN)
	{
		if (n == (size_t)(-1))		/* NULL-terminated list */
		{
			for (i = 0; list[i] != CHARPN; i++)
                          {
                            TX_MEM_SYS_FUNC_ENTER(__FUNCTION__);
                            FUNC(free(list[i]),
                                 mac_free(list[i], (char *)file, line, memo));
                            TX_MEM_SYS_FUNC_EXIT();
                          }
		}
		else
		{
			/* we were given `n'; do not stop at NULL, because
			 * list might have "holes" from removed elements:
			 */
			for (i = 0; i < n; i++)
                          if (list[i] != CHARPN)
                            {
                              TX_MEM_SYS_FUNC_ENTER(__FUNCTION__);
                              FUNC(free(list[i]),
                                 mac_free(list[i], (char *)file, line, memo));
                              TX_MEM_SYS_FUNC_EXIT();
                            }
		}
                TX_MEM_SYS_FUNC_ENTER(__FUNCTION__);
		FUNC(free(list), mac_free(list, (char *)file, line, memo));
                TX_MEM_SYS_FUNC_EXIT();
	}
	return(CHARPPN);
}

size_t
TXcountStrEmptyTermList(list)
char     **list;        /* (in) NULL/empty-terminated list to count */
{
  size_t        n;

  for (n = 0; list[n] != CHARPN && *list[n] != '\0'; n++);
  return(n);
}

char **
TXdupStrEmptyTermList(TXPMBUF *pmbuf, const char *fn, char **list,
                      size_t n TXALLOC_PROTO)
{
  char                  **newList;
  const char            *src;
  size_t                i;

  if (n == (size_t)(-1) || TX_SIZE_T_VALUE_LESS_THAN_ZERO(n))
    for (n = 0; list[n] != CHARPN && *list[n] != '\0'; n++);
  TX_MEM_SYS_FUNC_ENTER(fn);
  newList = (char **)FUNC(calloc(n + 2, sizeof(char *)),
                          mac_calloc(n + 2, sizeof(char *), file, line,
                                     memo));
  TX_MEM_SYS_FUNC_EXIT();
  if (newList == CHARPPN)
    {
      TXputmsgOutOfMem(pmbuf, MERR + MAE, fn, n + 2, sizeof(char *));
      return(CHARPPN);
    }
  for (i = 0; i <= n; i++)
    {
      if (i == n || list[i] == CHARPN || *list[i] == '\0')
        src = "";
      else
        src = list[i];
      TX_MEM_SYS_FUNC_ENTER(fn);
      newList[i] = FUNC(strdup(src), mac_strdup(src, file, line, memo));
      TX_MEM_SYS_FUNC_EXIT();
      if (newList[i] == CHARPN)
        {
          TXputmsgOutOfMem(pmbuf, MERR + MAE, fn, strlen(src) + 1, 1);
          return(TXfreeStrEmptyTermList(newList, i TXALLOC_ARGS_PASSTHRU));
        }
    }
  return(newList);
}

char **
TXfreeStrEmptyTermList(char **list, size_t n TXALLOC_PROTO)
{
  size_t        i;

  if (list != CHARPPN)
    {
      if (n == (size_t)(-1) || TX_SIZE_T_VALUE_LESS_THAN_ZERO(n))
        {                                       /* NULL/empty term. list */
          for (i = 0; list[i] != CHARPN && *list[i] != '\0'; i++)
            {
              TX_MEM_SYS_FUNC_ENTER(__FUNCTION__);
              FUNC(free(list[i]), mac_free(list[i], file, line, memo));
              TX_MEM_SYS_FUNC_EXIT();
            }
          if (list[i] != CHARPN)
            {
              TX_MEM_SYS_FUNC_ENTER(__FUNCTION__);
              FUNC(free(list[i]), mac_free(list[i], file, line, memo));
              TX_MEM_SYS_FUNC_EXIT();
            }
        }
      else
        {
          /* We were given `n'; do not stop at NULL/empty, because
           * list might have "holes" from removed elements:
           */
          for (i = 0; i < n; i++)
            if (list[i] != CHARPN)
              {
                TX_MEM_SYS_FUNC_ENTER(__FUNCTION__);
                FUNC(free(list[i]), mac_free(list[i], file, line, memo));
                TX_MEM_SYS_FUNC_EXIT();
              }
        }
      TX_MEM_SYS_FUNC_ENTER(__FUNCTION__);
      FUNC(free(list), mac_free(list, file, line, memo));
      TX_MEM_SYS_FUNC_EXIT();
    }
  return(CHARPPN);
}

char ***
TXfreeArrayOfStrLists(char ***list, size_t n TXALLOC_PROTO)
{
  size_t        i;
  char          **listItem;

  if (!list) return(NULL);

  if (n == (size_t)(-1) || TX_SIZE_T_VALUE_LESS_THAN_ZERO(n))
    {                                           /* NULL-terminated list */
      for (i = 0; (listItem = list[i]) != NULL; i++)
        TXfreeStrList(listItem, -1 TXALLOC_ARGS_PASSTHRU);
    }
  else
    {
      /* we were given `n'; do not stop at NULL, because
       * list might have "holes" from removed elements:
       */
      for (i = 0; i < n; i++)
        if ((listItem = list[i]) != NULL)
          TXfreeStrList(listItem, -1 TXALLOC_ARGS_PASSTHRU);
    }
  TX_MEM_SYS_FUNC_ENTER(__FUNCTION__);
  FUNC(free(list), mac_free(list, file, line, memo));
  TX_MEM_SYS_FUNC_EXIT();
  return(NULL);
}

void
TXputmsgOutOfMem(TXPMBUF *pmBuf, int num, const char *fn,
                 size_t nElems, size_t elSize)
/* Generic out-of-memory reporting function.  Also increments
 * `TXmemAllocFailures'.
 * Pass -1 for both `nElems' and `elSize' if size is unknown.
 */
{
  TXERRTYPE     errNum;
#ifdef _WIN32
  int           errNo;
#endif /* _WIN32 */

  TX_ATOMIC_INC(&TXmemAllocFailures);

  errNum = TXgeterror();
#ifdef _WIN32
  errNo = errno;
#endif /* _WIN32 */
  if (nElems == (size_t)(-1) && elSize == (size_t)(-1))
    txpmbuf_putmsg(pmBuf, num, fn, "Cannot alloc memory%s%s"
#ifdef _WIN32
                   "%s%s%s"
#endif /* _WIN32 */
                   , (errNum
#ifdef _WIN32
                      || errNo
#endif /* _WIN32 */
                      ? ": " : "")
                   /* Windows and/or system error are not always set
                    * nonzero on alloc failure; to avoid confusion do
                    * not report e.g. `No error' or `success':
                    */
                   , (errNum ? TXstrerror(errNum) : "")
#ifdef _WIN32
                   , (errNo && errNum ? " (" : "")
                   , (errNo ? strerror(errNo) : "")
                   , (errNo && errNum ? ")" : "")
#endif /* _WIN32 */
                   );
  else
    txpmbuf_putmsg(pmBuf, num, fn, "Cannot alloc %wkd bytes of memory%s%s"
#ifdef _WIN32
                   "%s%s%s"
#endif /* _WIN32 */
                   , ((EPI_HUGEINT)nElems)*(EPI_HUGEINT)elSize
                   , (errNum
#ifdef _WIN32
                      || errNo
#endif /* _WIN32 */
                      ? ": " : "")
                   /* Windows and/or system error are not always set
                    * nonzero on alloc failure; to avoid confusion do
                    * not report e.g. `No error' or `success':
                    */
                   , (errNum ? TXstrerror(errNum) : "")
#ifdef _WIN32
                   , (errNo && errNum ? " (" : "")
                   , (errNo ? strerror(errNo) : "")
                   , (errNo && errNum ? ")" : "")
#endif /* _WIN32 */
                   );
}

int
TXmemGetNumAllocFailures()
/* Returns number of reported memory allocation failures.
 */
{
  return((int)TXmemAllocFailures);
}

void *
TXmalloc(TXPMBUF *pmbuf, const char *fn, size_t sz TXALLOC_PROTO)
{
  void  *ret;

  TX_MEM_SYS_FUNC_ENTER(__FUNCTION__);
  ret = FUNC(malloc(sz), mac_malloc(sz, (char *)file, line, memo));
  TX_MEM_SYS_FUNC_EXIT();
  if (ret == NULL)
    TXputmsgOutOfMem(pmbuf, MERR + MAE, fn, sz, 1);
#ifdef EPI_TRACK_MEM
  else
    TXmemCurTotalAlloced += (EPI_HUGEINT)sz;
#endif /* EPI_TRACK_MEM */
  return(ret);
}

void *
TXcalloc(TXPMBUF *pmbuf, const char *fn, size_t n, size_t sz TXALLOC_PROTO)
{
  void  *ret;

  TX_MEM_SYS_FUNC_ENTER(fn);
  ret = FUNC(calloc(n, sz), mac_calloc(n, sz, (char *)file, line, memo));
  TX_MEM_SYS_FUNC_EXIT();
  if (ret == NULL)
    TXputmsgOutOfMem(pmbuf, MERR + MAE, fn, n, sz);
#ifdef EPI_TRACK_MEM
  else
    TXmemCurTotalAlloced += (EPI_HUGEINT)(n*sz);
#endif /* EPI_TRACK_MEM */
  return(ret);
}

void *
TXrealloc(TXPMBUF *pmbuf, const char *fn, void *p, size_t sz TXALLOC_PROTO)
{
  void  *ret;

  /* Use malloc() if `p' is NULL, in case system realloc() cannot
   * handle NULL:
   */
#ifdef EPI_TRACK_MEM
  size_t        prevSz = 0;
#  ifdef _WIN32
  if (p) prevSz = _msize(p);
#  endif /* _WIN32 */
#endif /* EPI_TRACK_MEM */
  if (p)
    {
#ifdef TX_ENABLE_MEM_PROTECT
      /* TXallocProtectable() mem must be freed with TXfreeProtectable: */
      if (TXnumPmItems > 0 && TXpmFindProtectedBlock(p))
        {
          txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
                         "Cannot realloc %p: protectable memory block",
                         p);
          return(NULL);
        }
#endif /* TX_ENABLE_MEM_PROTECT */
      TX_MEM_SYS_FUNC_ENTER(fn);
      ret = FUNC(realloc(p, sz), mac_remalloc(p, sz, (char *)file, line,
                                              memo));
      TX_MEM_SYS_FUNC_EXIT();
    }
  else
    {
      TX_MEM_SYS_FUNC_ENTER(fn);
      ret = FUNC(malloc(sz), mac_malloc(sz, (char *)file, line, memo));
      TX_MEM_SYS_FUNC_EXIT();
    }
  if (ret == NULL)
    TXputmsgOutOfMem(pmbuf, MERR + MAE, fn, sz, 1);
#ifdef EPI_TRACK_MEM
  else
    {
      TXmemCurTotalAlloced -= (EPI_HUGEINT)prevSz;
      TXmemCurTotalAlloced += (EPI_HUGEINT)sz;
    }
#endif /* EPI_TRACK_MEM */
  return(ret);
}

char *
TXstrdup(TXPMBUF *pmbuf, const char *fn, const char *s TXALLOC_PROTO)
{
  void  *ret;

  TX_MEM_SYS_FUNC_ENTER(fn);
  ret = FUNC(strdup(s), mac_strdup(s, (char *)file, line, memo));
  TX_MEM_SYS_FUNC_EXIT();
  if (ret == NULL)
    TXputmsgOutOfMem(pmbuf, MERR + MAE, fn, strlen(s) + 1, 1);
#ifdef EPI_TRACK_MEM
  else
    TXmemCurTotalAlloced += (EPI_HUGEINT)strlen(s) + 1;
#endif /* EPI_TRACK_MEM */
  return(ret);
}

char *
TXstrndup(TXPMBUF *pmbuf, const char *fn, const char *s, size_t n
          TXALLOC_PROTO)
{
	size_t	sz;
	char	*ret;

	for (sz = 0;(n == (size_t)(-1) || sz < n) && s[sz] != '\0'; sz++)
    ;
  TX_MEM_SYS_FUNC_ENTER(fn);
	ret = FUNC(malloc(sz + 1), mac_malloc(sz + 1, file, line, memo));
  TX_MEM_SYS_FUNC_EXIT();
	if (ret != CHARPN)
	{
		if (sz > 0) memcpy(ret, s, sz);
		ret[sz] = '\0';
	}
	else
		TXputmsgOutOfMem(pmbuf, MERR + MAE, fn, sz + 1, 1);
	return(ret);
}

int
TXexpandArray(TXPMBUF *pmbuf, const char *fn, void **array,
              size_t *allocedNum, size_t incNum, size_t elSz TXALLOC_PROTO)
/* Increases alloced size of `*array' by an arbitrary amount (at least
 * `incNum').
 * Returns 0 on error.
 */
{
  size_t        newNum;
  void          *newArray;

  newNum = (*allocedNum >> 2) + 16;             /* arbitrary increment */
  if (newNum < incNum) newNum = incNum;
  newNum += *allocedNum;
  newArray = TXrealloc(pmbuf, fn, *array, newNum*elSz TXALLOC_ARGS_PASSTHRU);
  if (newArray == NULL)                         /* failed */
    {
#ifndef EPI_REALLOC_FAIL_SAFE
      *array = NULL;
      *allocedNum = 0;
#endif /* EPI_REALLOC_FAIL_SAFE */
      return(0);
    }
  *array = newArray;
  *allocedNum = newNum;
  return(1);                                    /* success */
}

TXsharedBuf *
TXsharedBufOpen(TXPMBUF *pmbuf, byte *data, size_t dataSz,
                TXbool dataIsAlloced TXALLOC_PROTO)
/* Opens a TXsharedBuf with optional `data'.
 * Will take ownership of alloc'd `data' if `dataIsAlloced', else dups it.
 */
{
  TXsharedBuf   *buf = NULL;

  if (!(buf = (TXsharedBuf *)TXcalloc(pmbuf, __FUNCTION__, 1,
                                 sizeof(TXsharedBuf) TXALLOC_ARGS_PASSTHRU)))
    goto err;
  buf->refCnt = 1;
  if (data && dataSz > 0)
    {
      if (dataIsAlloced)
        buf->data = data;
      else
        {
          if (!(buf->data = (byte *)TXmalloc(pmbuf, __FUNCTION__, dataSz
                                             TXALLOC_ARGS_PASSTHRU)))
            goto err;
          memcpy(buf->data, data, dataSz);
        }
      buf->dataSz = dataSz;
    }
  goto finally;

err:
  buf = TXsharedBufClose(buf TXALLOC_ARGS_PASSTHRU);
finally:
  return(buf);
}

TXsharedBuf *
TXsharedBufClone(TXsharedBuf *buf)
{
  buf->refCnt++;
  return(buf);
}

TXsharedBuf *
TXsharedBufClose(TXsharedBuf *buf TXALLOC_PROTO)
{
  if (buf)
    {
      if (buf->refCnt <= 0 || --buf->refCnt <= 0)
        {
          buf->data = TXfree(buf->data TXALLOC_ARGS_PASSTHRU);
          buf->dataSz = 0;
          buf = TXfree(buf TXALLOC_ARGS_PASSTHRU);
        }
    }
  return(NULL);
}

int
TXgetSysMemFuncDepth(void)
/* Note that this only tracks TXmalloc()-etc. wrapper usage.
 * Thread-safe.
 * Signal-safe.
 */
{
  return(TXmemSysFuncDepth);
}

size_t
TXgetMemUsingFuncs(const char **funcs, size_t funcsLen)
/* Returns number of mem-using funcs (which may exceed `funcsLen'
 * and/or number of funcs known/copied, though unlikely).
 * Thread-safe (but potentially inconsistent, and funcs across threads).
 * Signal-safe.
 */
{
  size_t        i, depth = (size_t)TXmemSysFuncDepth, limit;

  limit = TX_MIN(depth, funcsLen);
  limit = TX_MIN(limit, TX_ARRAY_LEN(TXmemUsingFuncs));
  for (i = 0; i < limit; i++)
    funcs[i] = TXmemUsingFuncs[i];
  for ( ; i < funcsLen; i++)
    funcs[i] = NULL;
  return(depth);
}

void *
TXfreeObjectArray(void *array, size_t numItems, TXOBJCLOSEFUNC *objClose)
/* Frees alloced `array' of pointers to objects, each to be freed
 * with `objClose'.  Array has `numItems'; NULL-terminated if -1.
 * Returns NULL.
 */
{
  size_t        i;
  void          **list = (void **)array;

  if (!array) goto finally;
  if (numItems == (size_t)(-1))
    {
      for (i = 0; list[i]; i++)
        list[i] = objClose(list[i]);
    }
  else
    {
      for (i = 0; i < numItems; i++)
        list[i] = objClose(list[i]);
    }
  list = array = TXfree(array);

finally:
  return(NULL);
}
