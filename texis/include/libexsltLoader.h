#ifndef LIBEXSLT_H
#define LIBEXSLT_H

#include <libexslt/exslt.h>

/* libexslt prototypes, for when we dynamically load libexslt.dll */

#define LIBEXSLTSYMBOLS_LIST \
I(void, exsltRegisterAll, (void))

typedef struct LIBEXSLTSYMBOLS_tag
{
#undef I
#define I(ret, func, args)      ret (CDECL *func) ARGS(args);
  LIBEXSLTSYMBOLS_LIST
#undef I
}
LIBEXSLTSYMBOLS;
#define LIBEXSLTSYMBOLSPN   ((LIBEXSLTSYMBOLS *)NULL)

static CONST char * CONST       libexsltSymbolNames[] =
{
#  undef I
#  define I(ret, func, args)    #func,
  LIBEXSLTSYMBOLS_LIST
#  undef I
};
#  define NUM_LIBEXSLTSYMBOLS (sizeof(libexsltSymbolNames)/sizeof(libexsltSymbolNames[0]))
int loadlibexsltsymbols(void);

#endif /* LIBEXSLT_H */
