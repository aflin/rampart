#ifndef VERSION_H
#define VERSION_H


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef TX_VERSION_NUM
#  error Must define TX_VERSION_NUM in global.mkf
#endif
#ifndef TX_VERSION_MAJOR
#  error Must define TX_VERSION_MAJOR in global.mkf
#endif
#ifndef TX_VERSION_MINOR
#  error Must define TX_VERSION_MINOR in global.mkf
#endif
#ifndef TX_VERSION_REV
#  error Must define TX_VERSION_REV in global.mkf
#endif

#define TX_COMPATIBILITY_VERSION_MAJOR_IS_AT_LEAST(app, ver)    \
  (((app) ? (app)->compatibilityVersionMajor : TX_VERSION_MAJOR) >= (ver))

  /* Version 8.00 features: ----------------------------------------------- */
  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   * These features cannot be rolled back via compatibilityversion 7:
   */
/* note: EPI_ENABLE_VORTEX_WATCHPATH enables (see {unix,windows}.mkf): */
#define EPI_VORTEX_WATCHPATH    1
#if TX_VERSION_MAJOR >= 8
  /* NOTE: See also unix.mkf/windows.mkf for version 8+ features */
  /* Bug 857: DROP TABLE tableName IF EXISTS: */
#  define TX_SQL_IF_EXISTS      1
  /* Bug 4365: Support two-arg form of "assignment" options,
   * e.g. `--install-dir /dir' in addition to `--install-dir=/dir'.
   * Note: See also TX_VERSION_MAJOR check in docs for this:
   */
#  define EPI_TWO_ARG_ASSIGN_OPTS       1
  /* blobz: */
#  define WITH_BLOBZ            1
#  define EPI_MONITOR_SERVER_LICENSE_INFO       1
#  define EPI_LICENSE_AWS       1
#  define EPI_ENABLE_SQL_MOD    1 /* note: docs hard-coded to say version 8 */
  /* NOTE: see webtestIPvN01.vs notes when version 8 goes live: */
#  define EPI_ENABLE_IPv6       1
  /* allow MD5/SHA passwords, use SHA-512 by default, upgrade SYSUSERS: */
#  define EPI_ENABLE_PWHASH_METHODS     1
#  define EPI_ENABLE_PRAGMA_IF          1
#  define EPI_ENABLE_NULL_EMPTY         1
#  define EPI_PUTMSG_DATE_PID_THREAD    1
#  define EPI_ENABLE_SQL_HEX_CONSTANTS  1
#  define EPI_ENABLE_VORTEX_TRANSLATE_FROM_VERSION      1
  /* see also TXAPP.metaCookiesDefault, {unix,windows}.mkf */
  /* see also http.h */
#endif /* TX_VERSION_MAJOR >= 8 */

#ifdef EPI_TWO_ARG_ASSIGN_OPTS
#  define EPI_TWO_ARG_ASSIGN_OPT_USAGE_STR      "{=| }"
#  define EPI_TWO_ARG_ASSIGN_OPT_USAGE_LEN      5
#else /* !EPI_TWO_ARG_ASSIGN_OPTS */
#  define EPI_TWO_ARG_ASSIGN_OPT_USAGE_STR      "="
#  define EPI_TWO_ARG_ASSIGN_OPT_USAGE_LEN      1
#endif /* !EPI_TWO_ARG_ASSIGN_OPTS */

  /* Version 7.05 features: ----------------------------------------------- */
#if TX_VERSION_MAJOR >=8 || (TX_VERSION_MAJOR == 7 && TX_VERSION_MINOR >= 5)
#  define TX_SQL_URL_FUNCTIONS  1
#  define TX_SQL_WITH_HINTS     1
#  define TX_PAC                1
#endif /* version 7.05 */

  /* Version 7.04 features: ----------------------------------------------- */
  /* EPI_AUTH_NEGOTIATE defined in unix.mkf and windows.mkf */

  /* Version 7.02 features: ----------------------------------------------- */
  /* EPI_MIME_API defined in unix.mkf and windows.mkf */

  /* Version 7.01 features: ----------------------------------------------- */
#if TX_VERSION_MAJOR >=8 || (TX_VERSION_MAJOR == 7 && TX_VERSION_MINOR >= 1)
#  define TX_PER_SCHEMA_LICENSE_LIMITS  1
#endif /* version 7.01 */

  /* Version 7.00 features: ----------------------------------------------- */
#define TX_COMPATIBILITY_VERSION_IS_7_PLUS(app) \
  TX_COMPATIBILITY_VERSION_MAJOR_IS_AT_LEAST((app), 7)
  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   * These features can be rolled back via compatibilityversion 6:
   */
  /* Bug 3677: for default IN behavior of subset not intersect: */
#define TX_IN_MODE_IS_SUBSET_DEFAULT(app)       \
  TX_COMPATIBILITY_VERSION_IS_7_PLUS(app)
  /* Bug 3677: for strlst RELOP varchar (and vice-versa), promote `varchar',
   * via TXVSSEP_CREATE (not TXVSSEP_LASTCHAR), and be consistent with
   * strlst RELOP strlst, for consistent behavior if varchar is a
   * param from arrayconvert, regardless of # of array values;
   * `=' on strlst now does whole-strlst compare, not intersect:
   */
#define TX_STRLST_RELOP_VARCHAR_PROMOTE_VIA_CREATE_DEFAULT(app)    \
  TX_COMPATIBILITY_VERSION_IS_7_PLUS(app)
  /* Bug 3677: use stringcomparemode for strlst compares
   * (instead of memcmp()):
   */
#define TX_USE_STRINGCOMPAREMODE_FOR_STRLST_DEFAULT(app)        \
  TX_COMPATIBILITY_VERSION_IS_7_PLUS(app)
  /* Bug 4064 (Bug 3677): de-dup multi-item (e.g. strlst) index results: */
#define TX_DE_DUP_MULTI_ITEM_RESULTS_DEFAULT(app)       \
  TX_COMPATIBILITY_VERSION_IS_7_PLUS(app)
  /* Bug 4064 (Bug 3677): multivaluetomultirow now defaults off,
   * for consistency w/strlst mods:
   */
#define TX_MULTI_VALUE_TO_MULTI_ROW_DEFAULT(app)        \
  (!TX_COMPATIBILITY_VERSION_IS_7_PLUS(app))
  /* Bug 3931: <sum> numeric-vs.-string behavior determined by format arg: */
#define TX_VX_SUM_FMT_SETS_BEHAVIOR(app)        \
  TX_COMPATIBILITY_VERSION_IS_7_PLUS(app)
  /* Bug 4027: Do not parse HTML entities first in <sql output=xml>;
   * also default to :noutf8:
   */
#define TX_VX_NO_SQL_OUTPUT_XML_ENTITY_PARSE(app)       \
  TX_COMPATIBILITY_VERSION_IS_7_PLUS(app)
  /* Bug 2921: "create metamorph index" defaults to inverted
   * (wordpositions on):
   */
#define TX_METAMORPH_DEFAULT_INVERTED(app)      \
  TX_COMPATIBILITY_VERSION_IS_7_PLUS(app)
  /* Bug 4036: convert(varbyteData, 'varchar') will not hexify by
   * default, unless `set hexifybytes=1'.  tsql will still hexify by
   * default, so that `select * from SYS...'  does not flummox the
   * command-line tty.  All others default off.
   * NOTE: See also TX_HEXIFY_BYTES_FEATURES below:
   */
#define TX_HEXIFY_BYTES_DEFAULT(app, progName)  \
    (TX_COMPATIBILITY_VERSION_IS_7_PLUS(app) ?  \
     (strnicmp(TXbasename(progName), "tsql", 4) == 0 ? 1 : 0) : 1)
  /* Bug 4162: varchartostrlstsep default is 'create' not 'lastchar': */
#define TXVSSEP_BUILTIN_DEFAULT(app)    \
 (TX_COMPATIBILITY_VERSION_IS_7_PLUS(app) ? TXc2s_create_delimiter : TXc2s_trailing_delimiter)
  /* Bug 4162: 'create'-mode conversion of empty string yields
   * empty-strlst not one-empty-string strlst:
   */
#define TXVSSEP_CREATE_EMPTY_STR_TO_EMPTY_STRLST(app)   \
  TX_COMPATIBILITY_VERSION_IS_7_PLUS(app)
  /* Bug 4359: <vxcp putmsg call|log|print exceptiononly>: */
#define TX_VX_PUTMSG_EXCEPTIONONLY_ENABLED(app) \
  TX_COMPATIBILITY_VERSION_IS_7_PLUS(app)

  /*Note: other features may also check TX_COMPATIBILITY_VERSION_IS_7_PLUS()*/

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   * These features cannot be rolled back via compatibilityversion 6:
   */
#if TX_VERSION_MAJOR >= 7
  /* Bug 3625: <vxcp tracevortex> functionality: */
#  define TXVX_TRACEVORTEX_ENABLED 1
  /* Bug 3525: Respect empty-element (<tag/>) syntax in Vortex: */
#  define TX_VX_RESPECT_EMPTY_ELEMENTS
  /* Bug 4120: <!-- pragma whatever PUSH|POP on|off -->;
   * also reset pragmas each <script> block:
   */
#  define TX_VX_PRAGMA_STACKS   1
  /* Bug 2873: <write output|flags=> flags: */
#  define TX_VX_WRITE_OUTPUT    1
  /* Bug 2891: ALTER INDEX: */
#  define TX_ALTER_INDEX        1
  /* Bug 2922: CREATE INDEX ... WITH options; FULLTEXT alias for METAMORPH: */
#  define TX_INDEX_OPTIONS      1
  /* Bug 2084: no infinite loop <sandr>, and match like <rex>: */
#  define TX_NO_INFINITE_LOOP_SANDR             1
  /* Bug 4349: <vxcp timeouttext "text">: */
#  define TX_VXCP_TIMEOUTTEXT   1
  /* Bug 4378: allow <write|exec skiponfail>, default to off: */
#  define TX_VX_SKIPONFAIL      1
  /* hextobin(), bintohex(), `set hexifybytes=...' available.
   * NOTE: See also TX_HEXIFY_BYTES_DEFAULT() above:
   */
#  define TX_HEXIFY_BYTES_FEATURES      1
  /* convert() third arg (varchartostrlstsep mode) enabled: */
#  define TX_CONVERT_MODE_ENABLED       1
  /* Bug 4352: do not return redundant empty hits from REX: */
#  define TX_NO_REDUNDANT_EMPTY_REX_HITS        1
#endif /* TX_VERSION_MAJOR >= 7 */
  /* Bug 4065 (Bug 3677): enable SUBSET and INTERSECT operators: */
#define TX_SUBSET_INTERSECT_ENABLED_DEFAULT     (TX_VERSION_MAJOR >= 7)
  /* end Version 7 features ----------------------------------------------- */

#define VX_EXEC_USER
#define VX_SQL_PROVIDER
#define HT_UNICODE
#define TX_LICVER5 1
#define HT_NTLMKEEPALIVE

#define EPI_ENABLE_TEXTSEARCHMODE     1
#define EPI_ENABLE_STYLE_HIGHLIGHTING 1
#define EPI_ENABLE_ABSTRACT_QUERYMULTIPLE     1
#define EPI_INT64_SQL                 1
#define EPI_ENABLE_INT64_SQL          1
#define EPI_SQL_INT_N                 1
#define EPI_ENABLE_SQL_INT_N          1
#define LIBXML2API                    1
#define LIBXML2API_SKIP_ENV_CHECK     1
/* LIBXSLTAPI may not be defined for some platforms that we do not support,
 * so make LIBXSLTAPI_SKIP_ENV_CHECK conditional on it; see global.mkf:
 */
#ifdef LIBXSLTAPI
#  define LIBXSLTAPI_SKIP_ENV_CHECK   1
#endif /* LIBXSLTAPI */
#define EPI_ENABLE_APICP_6            1
#define EPI_ENABLE_TEXISINI_APICP     1
#define EPI_ENABLE_CONF_TEXIS_INI             1
#define EPI_REX_SET_SUBTRACT          1
#define EPI_FTP_RELATIVE_PATHS        1
#define EPI_ENABLE_CHARSET_CONFIG     1
#define EPI_DEFAULT_VORTEX_SOURCE_EXTENSION_VS 1
#define EPI_VORTEX_SQL_OKVARS         1
#define EPI_ENABLE_VORTEX_SQL_OKVARS  1
#define EPI_VORTEX_EXEC_QUOTEARGS     1
#define EPI_ENABLE_SQLCP_ARRAYCONVERT 1
#define EPI_ENABLE_SQLRESULT_VARS     1
#define EPI_ENABLE_SERVER_SSL         1
#define EPI_ENABLE_SECURE                     1
#define EPI_ENABLE_MAX_TX_VERSION_NUM_CHECK   1
#define EPI_LICENSE_6                 1
#define EPI_DELAY_FMT_DB_OPEN         1

  /* Bug 4425 fix: */
#define TX_USE_ORDERING_SPEC_NODE       1

  /* Linking JavaScript statically avoids potential wrong-version
   * issues, and more importantly, makes core dumps more useful:
   */
#define EPI_STATIC_JAVASCRIPT   1

/* See also global.mkf, unix.mkf, windows.mkf GLOBALCFLAGS */

/* in texis/gitCommit.c: */
extern const long TxSeconds;
extern const char *TXbuildId, *TXversionSuffix;
char *TXtexisver();

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !VERSION_H */
