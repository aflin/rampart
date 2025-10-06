#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include "dbquery.h"

#ifdef RAMPART_INCLUDE_TEXIS_USERFUNC
#  define INCLUDE_ADDUSERFUNCS
#    include RAMPART_INCLUDE_TEXIS_USERFUNC
#  undef INCLUDE_ADDUSERFUNCS
#else

void
adduserfuncs(fo)
FLDOP *fo;
{
}

#endif
