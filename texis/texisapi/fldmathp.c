#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifdef USE_EPI
#  include "sizes.h"
#  include "os.h"
#else
#  ifdef __BORLANDC__
#    define off_t long
#  endif
#endif
#include "dbquery.h"
#include "texint.h"	/* for setfldandsize() */

#undef promote
#undef probit
#undef protype
#define promote fld2byte
#define probit  FTN_BYTE
#define protype ft_byte
#include "fldpro.c"

#undef promote
#undef probit
#undef protype
#define promote fld2char
#define probit  FTN_CHAR
#define protype ft_char
#include "fldpro.c"

#undef promote
#undef probit
#undef protype
#define promote fld2double
#define probit  FTN_DOUBLE
#define protype ft_double
#include "fldpro.c"

#undef promote
#undef probit
#undef protype
#define promote fld2dword
#define probit  FTN_DWORD
#define protype ft_dword
#include "fldpro.c"

#undef promote
#undef probit
#undef protype
#define promote fld2float
#define probit  FTN_FLOAT
#define protype ft_float
#include "fldpro.c"

#undef promote
#undef probit
#undef protype
#define promote fld2int
#define probit  FTN_INT
#define protype ft_int
#include "fldpro.c"

#undef promote
#undef probit
#undef protype
#define promote fld2integer
#define probit  FTN_INTEGER
#define protype ft_integer
#include "fldpro.c"

#undef promote
#undef probit
#undef protype
#define promote fld2long
#define probit  FTN_LONG
#define protype ft_long
#include "fldpro.c"

#undef promote
#undef probit
#undef protype
#define promote fld2short
#define probit  FTN_SHORT
#define protype ft_short
#include "fldpro.c"

#undef promote
#undef probit
#undef protype
#define promote fld2smallint
#define probit  FTN_SMALLINT
#define protype ft_smallint
#include "fldpro.c"

#undef promote
#undef probit
#undef protype
#define promote fld2word
#define probit  FTN_WORD
#define protype ft_word
#include "fldpro.c"

#undef promote
#undef probit
#undef protype
#define promote fld2int64
#define probit  FTN_INT64
#define protype ft_int64
#include "fldpro.c"

#undef promote
#undef probit
#undef protype
#define promote fld2uint64
#define probit  FTN_UINT64
#define protype ft_uint64
#include "fldpro.c"

#undef promote
#undef probit
#undef protype
#define promote fld2handle
#define probit  FTN_HANDLE
#define protype ft_handle
#include "fldpro.c"
