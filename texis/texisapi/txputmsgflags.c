#include "txcoreconfig.h"
#include <stdio.h>
#ifdef EPI_HAVE_UNISTD_H
#include <unistd.h>
#endif
#include "dbquery.h"
#include "texint.h"

int
tx_setpmflags(flags, on)
TXPUTMSGFLAGS   flags;
int     on;
{
  TXPUTMSGFLAGS ret;

  if(!TXApp)
    return 0;
  ret = TXApp->putmsgFlags;
  if (on)
    TXApp->putmsgFlags |= flags;
  else
    TXApp->putmsgFlags &= ~flags;
  return(1);
}
