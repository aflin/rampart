#include "txcoreconfig.h"
#include "texint.h"

/**********************************************************************/

char **
freenlst(lst)
char **lst;
{
  char **l;

  if(!lst) return lst;

  for(l = lst; *l; l++) *l = TXfree(*l);
  TXfree(lst);
  return(CHARPPN);
}                                                   /* end freenlst() */
/**********************************************************************/
