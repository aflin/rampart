#ifndef LIBXSLT_H
#define LIBXSLT_H

#include <libxml/tree.h>  /* for xmlDocPtr */

#include <libxslt/xslt.h>
#include <libxslt/transform.h>

/* libxslt prototypes, for when we dynamically load libxslt.dll */

/* call xsltCheckVersion to make sure this matches up */
/* "LIBXSLT_VERSION" is used by libxslt itself */
#define TX_LIBXSLT_VERSION 10122

#ifdef _WIN32
#  define LIBXSLT_EXTERN_API(__type)  extern _declspec(dllexport) CDECL __type
#  define LIBXSLT_EXPORT_API(__type)         _declspec(dllexport) CDECL __type
#  define LIBXSLT_EXTERN_DATA(__type) extern _declspec(dllexport)       __type
#  define LIBXSLT_EXPORT_DATA(__type)        _declspec(dllexport)       __type
#else /* !_WIN32 */
#  define LIBXSLT_EXTERN_API(__type)  extern                            __type
#  define LIBXSLT_EXPORT_API(__type)                                    __type
#  define LIBXSLT_EXTERN_DATA(__type) extern                            __type
#  define LIBXSLT_EXPORT_DATA(__type)                                   __type
#endif /* !_WIN32 */

/*
*/

#define LIBXSLTSYMBOLS_LIST                                        \
I(xsltStylesheetPtr,    xsltParseStylesheetFile,    (char *filename)) \
I(xsltStylesheetPtr,    xsltParseStylesheetDoc,     (xmlDocPtr doc)) \
I(void,                 xsltFreeStylesheet,         (xsltStylesheetPtr style)) \
I(xsltTransformContextPtr, xsltNewTransformContext, (xsltStylesheetPtr style, xmlDocPtr doc)) \
I(void,                 xsltFreeTransformContext,   (xsltTransformContextPtr trans)) \
I(xmlDocPtr,            xsltApplyStylesheet,        (xsltStylesheetPtr style, xmlDocPtr doc, const char ** params)) \
I(xmlDocPtr,            xsltApplyStylesheetUser,    (xsltStylesheetPtr style, xmlDocPtr doc, const char ** params, const char *output, FILE * profile, xsltTransformContextPtr userCtxt)) \
I(void,                 xsltSetGenericErrorFunc,    (void *ctx, xmlGenericErrorFunc handler)) \
I(void,                 xsltSetTransformErrorFunc,  (xsltTransformContextPtr ctxt, void *ctx, xmlGenericErrorFunc handler)) \
I(int,                  xsltGetLibxsltVersion,      (void)) \
I(int,                  xsltSaveResultToString,     (char **output, int *outputSz, xmlDocPtr doc, xsltStylesheetPtr style)) \
 I(int,                  xsltSaveResultToFilename,   (char *URL, xmlDocPtr doc, xsltStylesheetPtr style, int compression))


typedef struct LIBXSLTSYMBOLS_tag
{
#undef I
#define I(ret, func, args)      ret (CDECL *func) ARGS(args);
  LIBXSLTSYMBOLS_LIST
#undef I
}
LIBXSLTSYMBOLS;
#define LIBXSLTSYMBOLSPN   ((LIBXSLTSYMBOLS *)NULL)

static CONST char * CONST       libxsltSymbolNames[] =
{
#  undef I
#  define I(ret, func, args)    #func,
  LIBXSLTSYMBOLS_LIST
#  undef I
};
#  define NUM_LIBXSLTSYMBOLS (sizeof(libxsltSymbolNames)/sizeof(libxsltSymbolNames[0]))

int loadlibxsltsymbols(void);
void xsltTxGenericErrorFunc(void *ctx, const char *msg, ...);

int xsltTxStylesheetGetRef(xsltStylesheetPtr style);
int xsltTxStylesheetIncRef(xsltStylesheetPtr style);
int xsltTxStylesheetDecRef(xsltStylesheetPtr style);

#endif /* LIBXSLT_H */
