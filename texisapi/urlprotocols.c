#include "txcoreconfig.h"
#include "texint.h"

static CONST unsigned   StdPort[HTPROT_NUM] =
{
#undef I
#define I(tok, protStr, textStr, port, isDefOn, isDefLinkOn, isFilePath)    \
  port,
  HTPROT_SYMBOLS_LIST
#undef I
};
static CONST char * CONST       ProtName[HTPROT_NUM] =
{
#undef I
#define I(tok, protStr, textStr, port, isDefOn, isDefLinkOn, isFilePath)    \
  protStr,
  HTPROT_SYMBOLS_LIST
#undef I
};
static CONST char * CONST       ProtTextName[HTPROT_NUM] =
{
#undef I
#define I(tok, protStr, textStr, port, isDefOn, isDefLinkOn, isFilePath)    \
  textStr,
  HTPROT_SYMBOLS_LIST
#undef I
};
const char       TXDefProtocolMask[HTPROT_NUM] =
{
#undef I
#define I(tok, protStr, textStr, port, isDefOn, isDefLinkOn, isFilePath)    \
  isDefOn,
  HTPROT_SYMBOLS_LIST
#undef I
};
const char       TXDefLinkProtMask[HTPROT_NUM] =
{
#undef I
#define I(tok, protStr, textStr, port, isDefOn, isDefLinkOn, isFilePath)    \
  isDefLinkOn,
  HTPROT_SYMBOLS_LIST
#undef I
};
static CONST char       ProtIsFilePath[HTPROT_NUM] =
{
#undef I
#define I(tok, protStr, textStr, port, isDefOn, isDefLinkOn, isFilePath)    \
  isFilePath,
  HTPROT_SYMBOLS_LIST
#undef I
};

HTPROT
htstr2protocol(const char *s, const char *e)
{
  HTPROT        p;

  if (e == CHARPN) e = s + strlen(s);
  for (p = (HTPROT)1; p < HTPROT_NUM; p++)
    if (strnicmp(ProtName[p], s, e - s) == 0 &&
        ProtName[p][e - s] == '\0')
      return(p);
  return(HTPROT_UNKNOWN);
}

const char *
htprotocol2str(HTPROT prot)
{
  if ((unsigned)prot >= (unsigned)HTPROT_NUM) prot = HTPROT_UNKNOWN;
  return(ProtName[prot]);
}

const char *TXfetchProtocolToText (HTPROT prot);
const char *
TXfetchProtocolToText(HTPROT prot)
{
  if ((unsigned)prot >= (unsigned)HTPROT_NUM) prot = HTPROT_UNKNOWN;
  return(ProtTextName[prot]);
}

TXbool
TXfetchSchemeHasFilePaths(const char *scheme, size_t  len)
/* Returns true if URL `scheme' has paths which are really
 * files, e.g. http/ftp; false if not.  `len' is length of `scheme'; -1
 * for strlen().
 */
{
  HTPROT        prot;

  if (!scheme) return(TXbool_True);             /* assume http */
  if (len == (size_t)(-1) ||
      TX_SIZE_T_VALUE_LESS_THAN_ZERO(len))
    len = strlen(scheme);
  if (len == 0) return(TXbool_True);            /* assume http */
  prot = htstr2protocol(scheme, scheme + len);
  if (prot == HTPROT_UNKNOWN) return(strnicmp(scheme, "file", len) == 0);
  return((TXbool)ProtIsFilePath[prot]);
}

unsigned
htstdport(HTPROT prot)
{
  if ((unsigned)prot >= (unsigned)HTPROT_NUM) return(0);
  return(StdPort[prot]);
}
