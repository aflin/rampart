#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "os.h"
#include "mmsg.h"
#include "heap.h"


static void maerr ARGS((CONST char *fn, size_t sz));
static void
maerr(fn, sz)
CONST char      *fn;
size_t          sz;
{
  putmsg(MERR + MAE, fn, "Cannot alloc %lu bytes: %s",
         (unsigned long)sz, strerror(errno));
}

#undef CMP
#undef FUNC
#define CMP(fh, a, b, usr)      fh->cmp(a, b, usr)
#define FUNC(a) a##Cmp
#include "fheapaux.c"
#undef CMP
#undef FUNC

FHEAP *
TXfheapOpen(cmpFunc, insertFunc, deltopFunc, usr, flags)
FHCMP           *cmpFunc;       /* (in) compare func (NULL for ins/del fns) */
TXFHINSFUNC     *insertFunc;    /* (in) NULL (or fheap_insert_wtix) */
TXFHDELTOPFUNC  *deltopFunc;    /* (in) NULL (or fheap_deletetop_wtix) */
void            *usr;           /* (in) user data */
HPF             flags;          /* (in) flags */
/* `cmp' is element comparison function.
 * `usr' is optional user data pointer, passed to `cmp'.
 * If `cmp' is NULL, custom `insertFunc'/`deltopFunc' functions are used.
 * Normally called with macro openfheap(cmpFunc, usr, flags).
 */
{
  static CONST char     fn[] = "openfheap";
  FHEAP                 *fh;

  if ((fh = (FHEAP *)calloc(1, sizeof(FHEAP))) == FHEAPPN)
    {
      maerr(fn, sizeof(FHEAP));
      goto done;
    }
  fh->flags = flags;
  if (cmpFunc == FHCMPPN)
    {
      fh->insert = insertFunc;
      fh->deletetop = deltopFunc;
    }
  else
    {
      fh->insert = TXfheapInsertCmp;
      fh->deletetop = TXfheapDeleteTopCmp;
    }
  fh->cmp = cmpFunc;
  fh->usr = usr;
  /* rest cleared by calloc() */

done:
  return(fh);
}

FHEAP *
closefheap(fh)
FHEAP   *fh;
{
  if (fh == FHEAPPN) goto done;

  if (fh->buf != (void **)NULL) free(fh->buf);
  free(fh);

done:
  return(FHEAPPN);
}

FHEAP *
TXfheapDup(FHEAP *fh)
{
  FHEAP *newFh = NULL;

  newFh = (FHEAP *)calloc(1, sizeof(FHEAP));
  if (!newFh)
    {
      maerr(__FUNCTION__, sizeof(FHEAP));
      goto err;
    }
  *newFh = *fh;
  /* Could be stack in use too; copy all of `fh->buf': */
  newFh->buf = (void **)malloc(fh->bufn*sizeof(void *));
  if (!newFh->buf)
    {
      newFh->bufn = newFh->n = newFh->stktop = 0;
      maerr(__FUNCTION__, fh->bufn*sizeof(void *));
      goto err;
    }
  if (fh->bufn > 0) memcpy(newFh->buf, fh->buf, fh->bufn*sizeof(void *));
  goto finally;

err:
  newFh = closefheap(newFh);
finally:
  return(newFh);
}

int
fheap_alloc(fh, n)
FHEAP   *fh;
size_t  n;
/* Makes sure heap has at least enough room for `n' total elements.
 * Public use optional; saves realloc()'s when final heap size is
 * known in advance.  Returns 0 on error.
 */
{
  static CONST char     fn[] = "fheap_alloc";
  size_t                sz, inc;
  void                  **newbuf;

  if (n <= fh->bufn) goto ok;           /* already enough space in buf */

  inc = n - fh->bufn;
  sz = (fh->bufn >> 1);                 /* alloc() expensive; grab a lot */
  if (inc < sz) inc = sz;
  if (inc < FHEAP_PREALLOC) inc = FHEAP_PREALLOC;
  sz = (fh->bufn + inc)*sizeof(void *);
  if ((newbuf = (void **)malloc(sz)) == NULL)
    {
      maerr(fn, sz);
      return(0);
    }
  if (fh->buf != NULL)
    {
      if (fh->n > 0) memcpy(newbuf, fh->buf, fh->n*sizeof(fh->buf[0]));
      free(fh->buf);
    }
  fh->buf = newbuf;
  /* Clear stack, since we haven't preserved it.  Also takes care of
   * some instances when user doesn't call fheap_stkreset():
   */
  fh->bufn = fh->stktop = (sz/sizeof(void *));

ok:
  return(1);
}

int
fheap_reheap(fh)
FHEAP   *fh;
/* Re-organizes `fh' correctly, after all/most of its elements' values
 * were changed in-place via fheap_elem() access or fheap_delelem().
 * Generally faster than removing all elements with fheap_deletetop()
 * and re-inserting them with fheap_insert().  Returns 0 on error.
 */
{
  void  **ptr, **end;

  if (fh->n <= 1) return(1);                    /* nothing to do */

  /* Since the heap uses the buf[] array consecutively from the left,
   * we can just clear the heap, and re-insert the values from the
   * array in-place, left to right.  Skip the first, because it would
   * be placed first anyway (first insert into an empty heap):
   */
  for (ptr = fh->buf, end = ptr + fh->n, fh->n = 1, ptr++; ptr < end; ptr++)
    if (!fheap_insert(fh, *ptr)) return(0);
  return(1);
}

int
fheap_delelem(fh, i)
FHEAP   *fh;
int     i;
/* Deletes item `i' from heap, moving later (array, not heap) elements
 * down one.  Returns 0 on error.  `i' is usually from an unordered
 * walk of elements via fheap_elem().  Assumes caller will free
 * underlying key object, if any.
 * NOTE: Caller MUST call fheap_reheap() when done deleting.
 */
{
  if ((unsigned)i >= (unsigned)fh->n)
    {
      putmsg(MERR + UGE, "fheap_delelem",
             "Internal error: Out-of-bounds index %d for %d-element heap",
             i, fh->n);
      return(0);
    }
  if ((size_t)i + 1 < fh->n)
    memmove(fh->buf + i, fh->buf + i + 1,
            ((fh->n - i) - 1)*sizeof(fh->buf[0]));
  fh->n--;
  return(1);
}

/* ------------------------------------------------------------------------- */

#ifdef TEST
static int fstrcmp ARGS((void *a, void *b, void *usr));
static int
fstrcmp(a, b, usr)
void    *a, *b, *usr;
{
  return(strcmp((char *)a, (char *)b));
}

static int fstrcmpi ARGS((void *a, void *b, void *usr));
static int
fstrcmpi(a, b, usr)
void    *a, *b, *usr;
{
  return(strcmpi((char *)a, (char *)b));
}

static int fnumcmp ARGS((void *a, void *b, void *usr));
static int
fnumcmp(a, b, usr)
void    *a, *b, *usr;
{
  return(atoi((char *)a) - atoi((char *)b));
}

static void usage ARGS((void));
static void
usage()
{
  printf("Usage: fheap [options]\nSorts stdin to stdout.  Options are:\n");
  printf("   -n      Numeric comparisions\n");
  printf("   -i      Ignore case\n");
  printf("   -f str  Flush N items (default all) when given line str [N]\n");
  printf("   -h      Show this help\n");
  exit(1);
}

static void flushit ARGS((FHEAP *fh, int n));
static void
flushit(fh, n)
FHEAP   *fh;
int     n;
{
  char  *s;

  while (fheap_num(fh) > 0 && n != 0)
    {
      puts(s = (char *)fheap_top(fh));
      free(s);
      fheap_deletetop(fh);
      n--;
    }
}

void
main(argc, argv)
int     argc;
char    *argv[];
{
  FHEAP *fh = FHEAPPN;
  FHCMP *cmp = fstrcmp;
  HPF   flags = (HPF)0;
  int   i, flen = 0;
  char  *flush = CHARPN, *e;
  char  line[8192];

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-n") == 0) cmp = fnumcmp;
      else if (strcmp(argv[i], "-i") == 0) cmp = fstrcmpi;
      else if (strcmp(argv[i], "-f") == 0)
        {
          i++;
          if (i >= argc) usage();
          flush = argv[i];
          flen = strlen(flush);
        }
      else usage();
    }

  if ((fh = openfheap(cmp, NULL, flags)) == FHEAPPN) exit(1);

  while (!feof(stdin))
    {
      if (fgets(line, sizeof(line), stdin) != line)
        {
          if (ferror(stdin))
            putmsg(MERR+FRE, CHARPN, "Can't read stdin: %s", strerror(errno));
          break;
        }
      e = line + strcspn(line, "\r\n");
      if (*e == '\0' && e == line + sizeof(line) - 1)
        {
          putmsg(MWARN + MAE, CHARPN, "Line too long (truncated)");
          while (!feof(stdin) && fgetc(stdin) != '\n');
        }
      *e = '\0';
      if ((e = strdup(line)) == CHARPN)
        {
          putmsg(MERR + MAE, CHARPN, "Out of memory: %s", strerror(errno));
          break;
        }
      if (flush != CHARPN && strncmp(e, flush, flen) == 0)
        {
          i = atoi(line + flen);
          if (i == 0) i = -1;
          flushit(fh, i);
          continue;
        }
      if (!fheap_insert(fh, e)) break;
    }
  flushit(fh, -1);

  closefheap(fh);
}
#endif  /* TEST */
