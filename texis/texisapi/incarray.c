#include "texint.h"

int
TXincarray(TXPMBUF *pmbuf, void **array, size_t num, size_t *anum, size_t elsz)
/* Expands `array' by one.  `num' is
 * current number of elements, `*anum' is actual alloced number (modified),
 * `elsz' is size of each element.  Returns 0 on error (array intact).
 */
{
  void                  *narray;

  if (num < *anum) goto ok;             /* nothing to do */

  *anum = 4 + num + (num >> 1);         /* 50% overalloc */
  narray = (void *)TXcalloc(pmbuf, __FUNCTION__, *anum, elsz);
  if (narray == NULL) return(0);
  if (*array != NULL)
    {
      if (num > 0) memcpy(narray, *array, num*elsz);
      *array = TXfree(*array);
    }
  *array = narray;
ok:
  return(1);
}
