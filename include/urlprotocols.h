#ifndef URLPROTOCOLS_H
#define URLPROTOCOLS_H

/* Protocol information:
 * I(tok, protStr, textStr, port, isDefOn, isDefLinkOn, isFilePath)
 * `UNKNOWN' must be first:
 */

/* FTP codes/ports, from the spec: */
#define STD_FTPCTRL_PORT                21
#define STD_FTPDATA_PORT                20

#define STD_HTTPS_PORT  443

#define HTPROT_SYMBOLS_LIST                                             \
I(UNKNOWN,      "unknown",      "Unknown",      0,      0, 1, 0)        \
I(HTTP,         "http",         "HTTP",         80,     1, 1, 1)        \
I(FTP,          "ftp",          "FTP", STD_FTPCTRL_PORT,1, 1, 1)        \
I(GOPHER,       "gopher",       "Gopher",       70,     1, 1, 1)        \
I(JAVASCRIPT,   "javascript",   "JavaScript",   0,      1, 1, 0)        \
I(HTTPS,        "https",        "HTTPS", STD_HTTPS_PORT,1, 1, 1)        \
I(FILE,         "file",         "file://",      0,      0, 1, 1)

typedef enum HTPROT_tag         /* protocols we are aware of */
{
#undef I
#define I(tok, protStr, textStr, port, isDefOn, isDefLinkOn, isFilePath)    \
  HTPROT_##tok,
  HTPROT_SYMBOLS_LIST
#undef I
  HTPROT_NUM                    /* must be last */
}
HTPROT;
#define HTPROTPN        ((HTPROT *)NULL)

extern const char       TXDefProtocolMask[HTPROT_NUM];
extern const char       TXDefLinkProtMask[HTPROT_NUM];

#endif /* URLPROTOCOLS_H */
