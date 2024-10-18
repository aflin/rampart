/* prototypes for xsltapi.c, the SQL-based LIBXSLT API functions */

#ifdef LIBXSLTAPI

#include <libxsltLoader.h>

int TXfunc_xsltApplyStylesheet(FLD *fld, FLD *fld_stylesheet,
                               FLD *fld_param_names, FLD *fld_param_vals);
int TXfunc_xsltParseStylesheetDoc(FLD *fld);
int TXfunc_xsltParseStylesheetFile(FLD *fld);
int TXfunc_xsltParseStylesheetString(FLD *fld);

int xsltTxStylesheetGetRef(xsltStylesheetPtr node);
int xsltTxStylesheetIncRef(xsltStylesheetPtr node);

#endif
