#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "texint.h"
#include "bitfuncs.h"
#include "http.h"


/* To avoid having to link in all the functions now, we make the
 * function pointer a string, since we just need to re-print this array:
 */
#define F(f)    ((int (*)(void))(#f))

/* NOTE: update VORTEX_OBJ_BASE_VERSION if this list changes,
 * for compiled SQL expressions:
 */
static FLDFUNC TXdbfldfuncsUnsorted[] =
 {
  { "abstract", F(TXsqlFuncs_abstract), 1, 5, (FTN_CHAR | DDVARBIT),
    { FTN_CHAR | DDVARBIT, FTN_LONG, (FTN_CHAR | DDVARBIT),
      (FTN_CHAR | DDVARBIT), (FTN_BYTE | DDVARBIT) } },
  { "acos", F(TXacos), 1, 1, FTN_DOUBLE, {FTN_DOUBLE, 0, 0, 0, 0}},
  { "asin", F(TXasin), 1, 1, FTN_DOUBLE, {FTN_DOUBLE, 0, 0, 0, 0}},
  { "atan", F(TXatan), 1, 1, FTN_DOUBLE, {FTN_DOUBLE, 0, 0, 0, 0}},
  { "atan2", F(TXatan2), 2, 2, FTN_DOUBLE, {FTN_DOUBLE, FTN_DOUBLE, 0,0,0}},
  { "azimuth2compass", F(TXfunc_azimuth2compass), 1, 3, FTN_CHAR | DDVARBIT,
    { FTN_DOUBLE, FTN_INT, FTN_INT, 0, 0 } },
  { "azimuthgeocode", F(TXfunc_azimuthgeocode), 2, 3, FTN_DOUBLE,
    { FTN_LONG, FTN_LONG, FTN_CHAR | DDVARBIT } },
  { "azimuthlatlon", F(TXfunc_azimuthlatlon), 4, 5, FTN_DOUBLE,
    { FTN_DOUBLE, FTN_DOUBLE, FTN_DOUBLE, FTN_DOUBLE, FTN_CHAR | DDVARBIT } },
  { "basename", F(TXsqlFunc_basename), 1, 1, (DDVARBIT | FTN_CHAR),
    { (DDVARBIT | FTN_CHAR), 0, 0, 0, 0 } },
  { "bexec", F(dobshell), 1, 5, FTN_BYTE | DDVARBIT, { 0, 0, 0, 0, 0 } },
#ifdef TX_HEXIFY_BYTES_FEATURES
  { "bintohex", F(TXsqlFunc_binToHex), 1, 2, (FTN_CHAR | DDVARBIT),
    { (FTN_BYTE | DDVARBIT), (FTN_CHAR | DDVARBIT), 0, 0, 0 } },
#endif /* TX_HEXIFY_BYTES_FEATURES */
  { "bitand",F(  txfunc_bitand),    2,2, (BITTYPE_FTN | DDVARBIT),
    {(BITTYPE_FTN | DDVARBIT),(BITTYPE_FTN | DDVARBIT),0,0,0 } },
  { "bitclear",F(  txfunc_bitclear),  2,2, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),(BITTYPE_FTN | DDVARBIT),0,0,0 } },
  { "bitcount",F(  txfunc_bitcount),  1,1, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),0,0,0,0 } },
  { "bitisset",F(  txfunc_bitisset),  2,2, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),(BITTYPE_FTN | DDVARBIT),0,0,0 } },
  { "bitlist",F(  txfunc_bitlist),    1,1, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),0,0,0,0 } },
  { "bitmax",F(  txfunc_bitmax),    1,1, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),0,0,0,0 } },
  { "bitmin",F(  txfunc_bitmin),    1,1, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),0,0,0,0 } },
  { "bitnot",F(  txfunc_bitnot),    1,1, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),0,0,0,0 } },
  { "bitor",F(  txfunc_bitor),    2,2, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),(BITTYPE_FTN | DDVARBIT),0,0,0 } },
  { "bitrotateleft",F(txfunc_bitrotateleft),  2,2, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),(BITTYPE_FTN | DDVARBIT),0,0,0 } },
  { "bitrotateright",F(txfunc_bitrotateright),2,2, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),(BITTYPE_FTN | DDVARBIT),0,0,0 } },
  { "bitset",F(  txfunc_bitset),    2,2, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),(BITTYPE_FTN | DDVARBIT),0,0,0 } },
  { "bitshiftleft",F(txfunc_bitshiftleft),  2,2, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),(BITTYPE_FTN | DDVARBIT),0,0,0 } },
  { "bitshiftright",F(txfunc_bitshiftright),  2,2, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),(BITTYPE_FTN | DDVARBIT),0,0,0 } },
  { "bitsize",F(  txfunc_bitsize),    1,1, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),0,0,0,0 } },
  { "bitxor",F(  txfunc_bitxor),    2,2, (BITTYPE_FTN | DDVARBIT),
    { (BITTYPE_FTN | DDVARBIT),(BITTYPE_FTN | DDVARBIT),0,0,0 } },
  { "canonpath",  F(TXfld_canonpath),  1, 2, FTN_CHAR | DDVARBIT,
    { FTN_CHAR | DDVARBIT, FTN_LONG, 0, 0, 0 } },
  { "ceil", F(TXceil), 1, 1, FTN_DOUBLE, {FTN_DOUBLE, 0, 0, 0, 0 } },
#ifdef TX_CONVERT_MODE_ENABLED
  { "convert", F(TXsqlFunc_convert), 2, 3, FTN_STRLST | DDVARBIT,
    { 0, FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, 0, 0 } },
#endif /* TX_CONVERT_MODE_ENABLED */
  { "cos", F(TXcos), 1, 1, FTN_DOUBLE, {FTN_DOUBLE, 0, 0, 0, 0 } },
  { "cosh", F(TXcosh), 1, 1, FTN_DOUBLE, {FTN_DOUBLE, 0, 0, 0, 0 } },
  { "dayname", F(TXdayname), 1, 1, FTN_CHAR | DDVARBIT, {FTN_DATE,0,0,0,0 } },
  { "dayofmonth", F(TXdayofmonth), 1, 1, FTN_INT, {FTN_DATE, 0, 0, 0, 0 } },
  { "dayofweek", F(TXdayofweek), 1, 1, FTN_INT, {FTN_DATE, 0, 0, 0, 0 } },
  { "dayofyear", F(TXdayofyear), 1, 1, FTN_INT, {FTN_DATE, 0, 0, 0, 0 } },
  { "dayseq", F(TXdayseq), 1, 1, FTN_INT, {FTN_DATE, 0, 0, 0, 0 } },
  { "dec2dms", F(TXfunc_dec2dms), 1, 1, FTN_DOUBLE,
    { FTN_DOUBLE, 0, 0, 0, 0 } },
  { "dirname", F(TXsqlFunc_dirname), 1, 1, (DDVARBIT | FTN_CHAR),
    { (DDVARBIT | FTN_CHAR), 0, 0, 0, 0 } },
  { "distgeocode", F(TXfunc_distGeocode), 2, 3, FTN_DOUBLE,
    { FTN_LONG, FTN_LONG, 0, 0, 0 } },
  { "distlatlon", F(TXfunc_distlatlon), 4, 5, FTN_DOUBLE,
    { FTN_DOUBLE, FTN_DOUBLE, FTN_DOUBLE, FTN_DOUBLE, 0 } },
  { "dms2dec", F(TXfunc_dms2dec), 1, 1, FTN_DOUBLE,
    { FTN_DOUBLE, 0, 0, 0, 0 } },
  { "exec", F(doshell), 1, 5, FTN_CHAR | DDVARBIT, {0, 0, 0, 0, 0 } },
  { "exp", F(TXexp), 1, 1, FTN_DOUBLE, {FTN_DOUBLE, 0, 0, 0, 0 } },
  { "fabs", F(TXfabs), 1, 1, FTN_DOUBLE, {FTN_DOUBLE, 0, 0, 0, 0 } },
  { "fileext", F(TXsqlFunc_fileext), 1, 1, (DDVARBIT | FTN_CHAR),
    { (DDVARBIT | FTN_CHAR), 0, 0, 0, 0 } },
  { "floor", F(TXfloor), 1, 1, FTN_DOUBLE, {FTN_DOUBLE, 0, 0, 0, 0 } },
  { "fmod", F(TXfmod), 2, 2, FTN_DOUBLE, {FTN_DOUBLE, FTN_DOUBLE, 0, 0, 0 } },
  { "fromfile", F(TXsqlFunc_fromfile), 1, 3, FTN_BYTE | DDVARBIT,
    { 0, FTN_LONG, FTN_LONG, 0, 0 } },
  { "fromfiletext", F(TXsqlFunc_fromfiletext), 1, 3, FTN_CHAR | DDVARBIT,
    { 0, FTN_LONG, FTN_LONG, 0, 0 } },
  { "geocode2lat", F(TXfunc_geocode2lat), 1, 1, FTN_DOUBLE,
    {FTN_LONG, 0, 0, 0, 0 } },
  { "geocode2lon", F(TXfunc_geocode2lon), 1, 1, FTN_DOUBLE,
    {FTN_LONG, 0, 0, 0, 0 } },
  { "greatcircle", F(TXfunc_greatCircle), 4, 4, FTN_DOUBLE,
    {FTN_DOUBLE, FTN_DOUBLE, FTN_DOUBLE, FTN_DOUBLE, 0 } },
  { "hasFeature",
    F(TXsqlFunc_hasFeature), 1, 1,
    FTN_INT, { (DDVARBIT | FTN_CHAR), 0, 0, 0, 0 }},
#ifdef TX_HEXIFY_BYTES_FEATURES
  { "hextobin", F(TXsqlFunc_hexToBin), 1, 2, (FTN_BYTE | DDVARBIT),
    {(FTN_CHAR | DDVARBIT), (FTN_CHAR | DDVARBIT), 0, 0, 0 } },
#endif /* TX_HEXIFY_BYTES_FEATURES */
  { "hour", F(TXhour), 1, 1, FTN_INT, {FTN_DATE, 0, 0, 0, 0 } },
  { "identifylanguage", F(TXsqlFuncIdentifylanguage), 1, 3, FTN_varSTRLST,
    {(FTN_CHAR | DDVARBIT), (FTN_CHAR | DDVARBIT), FTN_INT64, 0, 0 } },
  { "ifNull", F(TXsqlFunc_ifNull), 2, 2, FTN_LONG, {0, 0, 0, 0, 0 } },
  { "inet2int", F(txfunc_inet2int), 1, 1,
#if TX_VERSION_MAJOR >= 8
    FTN_varINT,
#else /* TX_VERSION_MAJOR < 8 */
    /* see also fldmath.c fixup */
    FTN_INT,
#endif /* TX_VERSION_MAJOR < 8 */
    {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
  { "inetabbrev", F(txfunc_inetabbrev), 1, 1, FTN_CHAR | DDVARBIT,
    {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
  { "inetbroadcast", F(txfunc_inetbroadcast), 1, 1, FTN_CHAR | DDVARBIT,
    {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
  { "inetcanon", F(txfunc_inetcanon), 1, 1, FTN_CHAR | DDVARBIT,
    {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
  { "inetclass", F(txfunc_inetclass), 1, 1, FTN_CHAR | DDVARBIT,
    {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
  { "inetcontains", F(txfunc_inetcontains), 2, 2, FTN_LONG,
    {FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, 0, 0, 0 } },
  { "inethost", F(txfunc_inethost), 1, 1, FTN_CHAR | DDVARBIT,
    {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
  { "inetnetmask", F(txfunc_inetnetmask), 1, 1, FTN_CHAR | DDVARBIT,
    {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
  { "inetnetmasklen", F(txfunc_inetnetmasklen), 1, 1, FTN_LONG,
    {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
  { "inetnetwork", F(txfunc_inetnetwork), 1, 1, FTN_CHAR | DDVARBIT,
    {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
  { "inetToIPv4", F(txfunc_inetToIPv4), 1, 1, FTN_CHAR | DDVARBIT,
    {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
  { "inetToIPv6", F(txfunc_inetToIPv6), 1, 1, FTN_CHAR | DDVARBIT,
    {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
  { "inetAddressFamily", F(txfunc_inetAddressFamily), 1, 1, FTN_varCHAR,
    {FTN_varCHAR, 0, 0, 0, 0 } },
  { "initcap", F(TXsqlFunc_initcap), 1, 2, FTN_CHAR | DDVARBIT,
    {FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, 0, 0, 0 } },
  { "int2inet", F(txfunc_int2inet), 1, 1, FTN_CHAR | DDVARBIT,
    {FTN_varINT, 0, 0, 0, 0 } },
  { "isNaN", F(TXsqlFunc_isNaN), 1, 1, FTN_INT,
    {0, 0, 0, 0, 0 } },
  { "isNull", F(TXsqlFunc_isNull), 1, 1, FTN_LONG,
    {0, 0, 0, 0, 0 } },
  { "joinpath", F(TXsqlFunc_joinpath), 1, 5, (DDVARBIT | FTN_CHAR),
    {0, 0, 0, 0, 0 } },
  { "joinpathabsolute", F(TXsqlFunc_joinpathabsolute), 1, 5, FTN_varCHAR,
    {0, 0, 0, 0, 0 } },
  { "keywords", F(TXsqlFunc_keywords), 1, 2, FTN_CHAR | DDVARBIT,
    {0, FTN_LONG, 0, 0, 0 } },
  { "latlon2geocode", F(TXfunc_latlon2geocode), 1, 2, FTN_LONG,
    {0, 0, 0, 0, 0 } },
  { "latlon2geocodearea", F(TXfunc_latlon2geocodearea), 1, 3, FTN_varLONG,
    {0, 0, 0, 0, 0 } },
  { "length", F(TXsqlFunc_length), 1, 2, FTN_LONG,
    {0, 0, 0, 0, 0 } },
  { "log", F(TXlog), 1, 1, FTN_DOUBLE,
    {FTN_DOUBLE, 0, 0, 0, 0 } },
  { "log10", F(TXlog10), 1, 1, FTN_DOUBLE,
    {FTN_DOUBLE, 0, 0, 0, 0 } },
  { "lookup", F(TXsqlFunc_lookup), 2, 3,
    FTN_varSTRLST /* overridden by TXsqlFuncLookup_GetReturnType() */,
    {0, 0, 0, 0, 0 } },
  { "lookupCanonicalizeRanges", F(TXsqlFunc_lookupCanonicalizeRanges), 2, 2,
    FTN_varSTRLST, { 0, FTN_varCHAR, 0, 0, 0 } },
  { "lookupParseRange", F(TXsqlFunc_lookupParseRange), 2, 2,
    FTN_varSTRLST, {0, 0, 0, 0, 0 } },
  { "lower", F(TXsqlFunc_lower), 1, 2, FTN_CHAR | DDVARBIT,
    {FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, 0, 0, 0 } },
#ifdef EPI_METAPHONE
  { "metaphone", F(TXsqlFunc_metaphone), 1, 3, (DDVARBIT | FTN_STRLST),
   {FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT,
    0, 0 } },
#endif /* EPI_METAPHONE */
  { "minute", F(TXminute), 1, 1, FTN_INT,
    {FTN_DATE, 0, 0, 0, 0 } },
  { "mminfo", F(mminfo), 2, 5, (FTN_CHAR | DDVARBIT),
    {(FTN_CHAR | DDVARBIT), (FTN_CHAR | DDVARBIT), FTN_LONG, 0, FTN_LONG}},
  { "month", F(TXmonth), 1, 1, FTN_INT,
    {FTN_DATE, 0, 0, 0, 0 } },
  { "monthname", F(TXmonthname), 1, 1, FTN_CHAR | DDVARBIT,
    {FTN_DATE, 0, 0, 0, 0 } },
  { "monthseq", F(TXmonthseq), 1, 1, FTN_INT,
    {FTN_DATE, 0, 0, 0, 0 } },
  { "parselatitude", F(TXfunc_parselatitude), 1, 1, FTN_DOUBLE,
    {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
  { "parselongitude", F(TXfunc_parselongitude), 1, 1, FTN_DOUBLE,
    {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
  { "pathcmp",  F(TXsqlFunc_pathcmp),  2, 2, FTN_LONG,
    { FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, 0, 0, 0 } },
  { "pow", F(TXpow), 2, 2, FTN_DOUBLE,
    {FTN_DOUBLE, FTN_DOUBLE, 0, 0, 0 } },
  { "pythag", F(TXfunc_pythag), 4, 4, FTN_DOUBLE,
    {FTN_DOUBLE, FTN_DOUBLE, FTN_DOUBLE, FTN_DOUBLE, 0 } },
  { "pythagmiles", F(TXfunc_pythagMiles), 4, 4, FTN_DOUBLE,
    {FTN_DOUBLE, FTN_DOUBLE, FTN_DOUBLE, FTN_DOUBLE, 0 } },
  { "quarter", F(TXquarter), 1, 1, FTN_INT,
    {FTN_DATE, 0, 0, 0, 0 } },
  { "random", F(TXsqlFunc_random), 1, 2, FTN_INT,
    {FTN_LONG, FTN_LONG, 0, 0, 0 } },
  { "sandr", F(TXsqlFunc_sandr), 3, 3, FTN_CHAR | DDVARBIT,
    {FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, 0, 0 } },
  { "second", F(TXsecond), 1, 1, FTN_INT,
    {FTN_DATE, 0, 0, 0, 0 } },
  { "separator", F(TXsqlFunc_separator), 1, 1, FTN_CHAR | DDVARBIT,
    {FTN_STRLST, 0, 0, 0, 0 } },
  { "seq", F(TXsqlFunc_seq), 1, 2, FTN_INT,
    {FTN_LONG, FTN_LONG, 0, 0, 0 } },
  { "sin", F(TXsin), 1, 1, FTN_DOUBLE,
    {FTN_DOUBLE, 0, 0, 0, 0 } },
  { "sinh", F(TXsinh), 1, 1, FTN_DOUBLE,
    {FTN_DOUBLE, 0, 0, 0, 0 } },
  { "sqrt", F(TXsqrt), 1, 1, FTN_DOUBLE,
    {FTN_DOUBLE, 0, 0, 0, 0 } },
  { "stringcompare", F(TXfunc_stringcompare), 2, 3, FTN_INT,
    {(FTN_CHAR | DDVARBIT), (FTN_CHAR | DDVARBIT), (FTN_CHAR | DDVARBIT), 0, 0 } },
  { "stringformat", F(TXfunc_stringformat), 1, 5, (FTN_CHAR | DDVARBIT),
    {0, 0, 0, 0, 0 } },
  { "strtol",  F(TXsqlFunc_strtol),  1, 2, FTN_LONG,  {FTN_CHAR | DDVARBIT, FTN_INT, 0, 0, 0 } },
  { "strtoul", F(TXsqlFunc_strtoul), 1, 2, FTN_DWORD,
    {FTN_CHAR | DDVARBIT, FTN_INT, 0, 0, 0 } },
  { "tan", F(TXtan), 1, 1, FTN_DOUBLE,
    {FTN_DOUBLE, 0, 0, 0, 0 } },
  { "tanh", F(TXtanh), 1, 1, FTN_DOUBLE,
    {FTN_DOUBLE, 0, 0, 0, 0 } },
  { "text2mm", F(TXsqlFunc_text2mm), 1, 2, FTN_CHAR | DDVARBIT,
    {0, FTN_LONG, 0, 0, 0 } },
  { "texttomm", F(TXsqlFunc_text2mm), 1, 2, FTN_CHAR | DDVARBIT,
    {0, FTN_LONG, 0, 0, 0 } },
  { "toind", F(TXftoind), 1, 1, FTN_CHAR | DDVARBIT,
    {0, 0, 0, 0, 0 } },
  { "totext", F(TXsqlFunc_totext), 1, 2, FTN_CHAR | DDVARBIT,
    {0, FTN_CHAR | DDVARBIT, 0, 0, 0 } },
  { "upper", F(TXsqlFunc_upper), 1, 2, FTN_CHAR | DDVARBIT,
    {FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, 0, 0, 0 } },
#ifdef TX_SQL_URL_FUNCTIONS
  { "urlcanonicalize", F(TXsqlFunc_urlcanonicalize), 1, 2, FTN_CHAR | DDVARBIT,
    {(FTN_CHAR | DDVARBIT), (FTN_CHAR | DDVARBIT), 0, 0, 0 } },
#endif
  { "week", F(TXweek), 1, 1, FTN_INT,
    {FTN_DATE, 0, 0, 0, 0 } },
  { "weekseq", F(TXweekseq), 1, 1, FTN_INT,
    {FTN_DATE, 0, 0, 0, 0 } },
  { "year", F(TXyear), 1, 1, FTN_INT,
    { FTN_DATE, 0, 0, 0, 0 } },
    { "isjson", F(txfunc_isjson), 1, 1, FTN_LONG, {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
    { "json_type", F(txfunc_json_type), 1, 1, FTN_CHAR | DDVARBIT, {FTN_CHAR | DDVARBIT, 0, 0, 0, 0 } },
    { "json_value", F(txfunc_json_value), 2, 2, FTN_CHAR | DDVARBIT, {FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, 0, 0, 0 } },
    { "json_query", F(txfunc_json_query), 2, 2, FTN_CHAR | DDVARBIT, {FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, 0, 0, 0 } },
    { "json_modify", F(txfunc_json_modify), 3, 3, FTN_CHAR | DDVARBIT, {FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, 0, 0, 0 } },
    { "json_merge_patch", F(txfunc_json_merge_patch), 2, 2, FTN_CHAR | DDVARBIT, {FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, 0, 0, 0 } },
    { "json_merge_preserve", F(txfunc_json_merge_preserve), 2, 2, FTN_CHAR | DDVARBIT, {FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, 0, 0, 0 } },
    { "json_format", F(txfunc_json_format), 2, 2, FTN_CHAR | DDVARBIT, {FTN_CHAR | DDVARBIT, FTN_CHAR | DDVARBIT, 0, 0, 0 } },
    { "generate_uuid", F(txfunc_generate_uuid), 1, 1, FTN_CHAR | DDVARBIT, {0,0,0,0,0}},
    { "now", F(TXnow), 1, 1, FTN_DATE, {0,0,0,0,0}},
};

static int
funcNameCmp(const void *a, const void *b)
{
  const FLDFUNC *fa = (const FLDFUNC *)a;
  const FLDFUNC *fb = (const FLDFUNC *)b;

  return(TXfldopFuncNameCompare(fa->name, fb->name));
}

int
main(int argc, char *argv[])
{
  size_t        i, argIdx;
  FLDFUNC       *f;
  FILE 			*outFile = NULL;

  if(argc >= 2) {
	  if(!(outFile = fopen(argv[1], "wb"))) {
		  return -1;
	  }
  } else {
	  outFile = stdout;
  }
  qsort(TXdbfldfuncsUnsorted, TX_ARRAY_LEN(TXdbfldfuncsUnsorted),
        sizeof(FLDFUNC), funcNameCmp);
  fprintf(outFile, "/* This file automatically generated by fldFuncs.c */\n");
  fprintf(outFile, "\n");
  fprintf(outFile, "#define F(f)    ((int (*)(void))(f))\n");
  fprintf(outFile, "\n");
  fprintf(outFile, "const FLDFUNC  TXdbfldfuncs[] =\n");
  fprintf(outFile, "{\n");
  for (i = 0; i < TX_ARRAY_LEN(TXdbfldfuncsUnsorted); i++)
    {
      f = &TXdbfldfuncsUnsorted[i];
      fprintf(outFile, "  { \"%s\", F(%s), %d, %d, %d,\n", f->name, (char *)f->func,
             (int)f->minargs, (int)f->maxargs, (int)f->rettype);
      fprintf(outFile, "    {");
      for (argIdx = 0; argIdx < MAXFLDARGS; argIdx++)
        fprintf(outFile, "%s %d", (argIdx > 0 ? "," : ""), (int)f->types[argIdx]);
      fprintf(outFile, "} },\n");
    }
  fprintf(outFile, "};\n");
  fprintf(outFile, "\n");
  fprintf(outFile, "#undef F\n");
  return(0);
}
