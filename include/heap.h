#ifndef HEAP_H
#define HEAP_H


/* ------------------------ Fixed-size-key heaps --------------------------- */

/* Type of callback function to compare 2 nodes: */
typedef int (FHCMP) ARGS((void *a, void *b, void *usr));
#define FHCMPPN         ((FHCMP *)NULL)

typedef enum HPF_tag                    /* heap flags */
{
  HPF_DUMMY    = (1 << 0)               /* placeholder */
}
HPF;
#define HPFPN   ((HPF *)NULL)

typedef struct FHEAP_tag        FHEAP;
struct FHEAP_tag                        /* no user-serviceable parts inside */
{
  void          **buf;                  /* array of heap elements */
  size_t        bufn;                   /* number of elements alloced */
  size_t        n;                      /* number of elements in heap */
  size_t        stktop;                 /* off-heap stack start, end */
  int           (*insert) ARGS((FHEAP *fh, void *key));
  void          (*deletetop) ARGS((FHEAP *fh));
  FHCMP         *cmp;                   /* element comparison function */
  void          *usr;                   /* user data passed to `cmp' */
  HPF           flags;                  /* flags */
};
#define FHEAPPN  ((FHEAP *)NULL)

#define FHEAP_PREALLOC  (128/sizeof(void *))    /* initial alloc WAG */

/* Internal use: */
typedef int (TXFHINSFUNC) ARGS((FHEAP *fh, void *key));
typedef void (TXFHDELTOPFUNC) ARGS((FHEAP *fh));

FHEAP   *TXfheapOpen ARGS((FHCMP *cmpFunc, TXFHINSFUNC *insertFunc,
                           TXFHDELTOPFUNC *deltopFunc, void *usr, HPF flags));
#define openfheap(cmp, usr, flags)      \
  TXfheapOpen((cmp), NULL, NULL, (usr), (flags))

FHEAP   *closefheap ARGS((FHEAP *fh));
FHEAP   *TXfheapDup(FHEAP *fh);
int     fheap_alloc ARGS((FHEAP *fh, size_t n));
#define fheap_insert(fh, key)   (fh)->insert((fh), (key))
#define fheap_deletetop(fh)     (fh)->deletetop(fh)
int     fheap_reheap ARGS((FHEAP *fh));
int     fheap_delelem ARGS((FHEAP *fh, int i));
#define fheap_clear(fh)         ((fh)->n = 0)
#define fheap_num(fh)           ((fh)->n)
#define fheap_top(fh)           (*(fh)->buf)    /* valid if fheap_num() > 0 */
#define fheap_elem(fh, i)       ((fh)->buf[(i)])
#define fheap_setcmp(fh, fn)    ((fh)->cmp = (fn))

int TXfheapInsertWtix ARGS((FHEAP *fh, void *key));
void TXfheapDeleteTopWtix ARGS((FHEAP *fh));

/* To use stack, fheap_stkreset() must be called after last initial insert.
 * From then on, each deletetop may optionally be followed by a push.
 * Each pop may be followed by an insert.  Do not call fheap_alloc().
 * Inserting more keys than popped may result in stack/heap collision.
 */
#define fheap_stkreset(fh)      ((fh)->stktop = (fh)->bufn)  /* clear stack */
#define fheap_stknum(fh)        ((fh)->bufn - (fh)->stktop)  /* # in stack */
#define fheap_stktop(fh)        ((fh)->buf[(fh)->stktop])    /* if stknum>0 */
#define fheap_stkpush(fh, key)  ((fh)->buf[--(fh)->stktop] = (key))
#define fheap_stkpop(fh)        ((fh)->stktop++)

/* --------------------------- Variable-sized heap ------------------------- */

#endif  /* !HEAP_H */
