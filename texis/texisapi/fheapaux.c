/* This file included several times by fheap.c for different versions */

#undef PARENT
#undef LEFT
#undef RIGHT
#define PARENT(i)       (((i) - 1) >> 1)
#define LEFT(i)         (((i) << 1) + 1)
#define RIGHT(i)        (((i) << 1) + 2)

void FUNC(TXfheapDeleteTop) ARGS((FHEAP *fh));
void FUNC(
TXfheapDeleteTop)(fh)
FHEAP   *fh;
/* Removes top element of heap `fh', which is the smallest/first.
 * Assumes top element copied off with fheap_top() first.
 */
{
  int   i, l, last, lastp;
  void  **lloc, **rloc, **iloc, *key;

  if (fh->n <= 1)                               /* empty or 1 element */
    {
      fh->n = 0;
      return;
    }
  fh->n--;

  /* Moves `key' down the heap, starting at position `i' (assumed empty),
   * until correct location found.  Derived from downheap(), Sedgewick p. 134.
   */
  key = fh->buf[fh->n];
  iloc = fh->buf;
  last = fh->n - 1;                             /* last node in array */
  if (last <= 0) goto fin;                      /* only 1 node */
  lastp = PARENT(last);                         /* parent of last node */
  for (i = 0; i <= lastp; i = l, iloc = lloc)
    {
      l = LEFT(i);
      lloc = fh->buf + l;
      if (l < last)                             /* i has right child too */
        {
          rloc = lloc + 1;
          if (CMP(fh, *lloc, *rloc, fh->usr) > 0) /* right child smaller */
            {
              l++;
              lloc = rloc;
            }
        }
      if (CMP(fh, key, *lloc, fh->usr) <= 0)    /* heap ok here; stop */
        break;
      *iloc = *lloc;
    }
fin:
  *iloc = key;
}

int FUNC(TXfheapInsert) ARGS((FHEAP *fh, void *key));
int FUNC(
TXfheapInsert)(fh, key)
FHEAP   *fh;
void    *key;
/* Inserts `key' into heap `fh'.  Returns 0 on error.
 * Derived from upheap(), Sedgewick p. 132.
 */
{
  int   p, i;
  void  **ploc, **iloc;

  if (fh->n >= fh->bufn && !fheap_alloc(fh, fh->n + 1))
    return(0);

  for (i = fh->n, iloc = fh->buf + i; i > 0; i = p, iloc = ploc)
    {
      p = PARENT(i);
      ploc = fh->buf + p;
      if (CMP(fh, key, *ploc, fh->usr) >= 0)
        break;                                  /* i is the location for us */
      *iloc = *ploc;                            /* else bump parent down */
    }
  *iloc = key;
  fh->n += 1;
  return(1);
}

#undef PARENT
#undef LEFT
#undef RIGHT
