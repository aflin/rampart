#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#ifdef EPI_HAVE_NETDB_H
#  include <netdb.h>
#endif /* EPI_HAVE_NETDB_H */
#include "texint.h"

/* List of h_errno names: */

const TXCODEDESC        TXh_errnoNames[] =
{
  /* no EOK: make err 0=Ok across platforms for consistent tests */
  { 0, "Ok" },
#ifdef HOST_NOT_FOUND               /* Authoritative Answer Host not found */
  TXCODEDESC_ITEM(HOST_NOT_FOUND),
#endif
#ifdef TRY_AGAIN        /* Non-Authoritative Host not found, or SERVERFAIL */
  TXCODEDESC_ITEM(TRY_AGAIN),
#endif
#ifdef NO_RECOVERY     /* Non recoverable errors, FORMERR, REFUSED, NOTIMP */
  TXCODEDESC_ITEM(NO_RECOVERY),
#endif
#ifdef NO_DATA              /* Valid name, no data record of requested type */
  TXCODEDESC_ITEM(NO_DATA),
#endif
  {0, NULL }
};
