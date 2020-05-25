#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <winsock.h>
#else /* !_WIN32 */
#  include <netinet/in.h>
#endif /* !_WIN32 */
#include "dbquery.h"
#undef QUERY                                 /* arpa/nameser.h collision */
#include "texint.h"
#include "inetfuncs.h"
#include "http.h"
#include "htdns.h"

/* IP address Texis SQL functions
 * NOTE: see also vortex/vread.c for <urlutil> versions
 */


/* ------------------------------------------------------------------------ */


/* Start a unary func.  Sets `a'/`an' to first field's data/nels.
 * `s'/`e' are scratch:
 */
#define UNARYVARS                               \
  char                  *a;                     \
  size_t                an;                     \
  TXsockaddr            inet;                   \
  int                   netbits;                \
  TXPMBUF               *pmbuf = TXPMBUFPN;     \
  char                  *s, tmp[TX_MAX(128, TX_SOCKADDR_MAX_STR_SZ)]

#define UNARYINIT()                             \
  if (f1 == FLDPN ||                            \
      (f1->type & DDTYPEBITS) != FTN_CHAR ||    \
      (a = (char *)getfld(f1, &an)) == CHARPN)  \
    return(FOP_EINVAL)

/* wtf tracedns? */
#define PARSEINET()                                             \
  netbits = TXinetparse(pmbuf, TXtraceDns_None, a, &inet);      \
  if (netbits < 0)                                              \
    {                                                           \
      *tmp = '\0';                                              \
      goto setval;                                              \
    }

#define SETCHARVAL()                                            \
  if (!(s = TXstrdup(pmbuf, __FUNCTION__, tmp)))                \
    return(FOP_ENOMEM);                                         \
  f1->type = ((f1->type & ~DDTYPEBITS) | FTN_CHAR | DDVARBIT);  \
  f1->elsz = 1;                                                 \
  setfldandsize(f1, s, strlen(s) + 1, FLD_FORCE_NORMAL)


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int
txfunc_inetabbrev(f1)
FLD     *f1;
/* inetabbrev(inet):  shortest representation of `inet'.
 * Includes all contiguous MSBs of network and non-zero bytes of host.
 * Returns varchar.
 */
{
  UNARYVARS;

  UNARYINIT();
  PARSEINET();
  if (!TXinetabbrev(pmbuf, tmp, sizeof(tmp), &inet, netbits, TXbool_False))
    *tmp = '\0';
setval:
  SETCHARVAL();
  return(0);
}

int
txfunc_inetcanon(f1)
FLD     *f1;
/* inetcanon(inet):  canonical representation of `inet'.
 * Returns varchar.
 */
{
  UNARYVARS;

  UNARYINIT();
  PARSEINET();
  if (!TXinetabbrev(pmbuf, tmp, sizeof(tmp), &inet, netbits, TXbool_True))
    *tmp = '\0';
setval:
  SETCHARVAL();
  return(0);
}

int
txfunc_inetnetwork(f1)
FLD     *f1;
/* inetnetwork(inet):  network-bits-only IP of `inet'.
 * Returns varchar.
 */
{
  TXsockaddr    netmask;
  byte          *inetBytes, *netmaskBytes;
  size_t        numBytes, byteIdx;
  UNARYVARS;

  UNARYINIT();
  PARSEINET();

  netmask = inet;
  if (!TXsockaddrSetNetmask(pmbuf, &netmask, netbits))
    {
      *tmp = '\0';
      goto setval;
    }
  numBytes = TXsockaddrGetIPBytesAndLength(pmbuf, &inet, &inetBytes);
  TXsockaddrGetIPBytesAndLength(pmbuf, &netmask, &netmaskBytes);
  for (byteIdx = 0; byteIdx < numBytes; byteIdx++)
    inetBytes[byteIdx] &= netmaskBytes[byteIdx];

  if (!TXsockaddrToStringIP(pmbuf, &inet, tmp, sizeof(tmp)))
    return(FOP_EUNKNOWN);
setval:
  SETCHARVAL();
  return(0);
}

int
txfunc_inethost(f1)
FLD     *f1;
/* inethost(inet):  host-bits-only IP of `inet'.
 * Returns varchar.
 */
{
  TXsockaddr    netmask;
  byte          *inetBytes, *netmaskBytes;
  size_t        numBytes, byteIdx;
  UNARYVARS;

  UNARYINIT();
  PARSEINET();

  netmask = inet;
  if (!TXsockaddrSetNetmask(pmbuf, &netmask, netbits))
    {
      *tmp = '\0';
      goto setval;
    }
  numBytes = TXsockaddrGetIPBytesAndLength(pmbuf, &inet, &inetBytes);
  TXsockaddrGetIPBytesAndLength(pmbuf, &netmask, &netmaskBytes);
  for (byteIdx = 0; byteIdx < numBytes; byteIdx++)
    inetBytes[byteIdx] &= ~netmaskBytes[byteIdx];

  if (!TXsockaddrToStringIP(pmbuf, &inet, tmp, sizeof(tmp)))
    return(FOP_EUNKNOWN);
setval:
  SETCHARVAL();
  return(0);
}

int
txfunc_inetbroadcast(f1)
FLD     *f1;
/* inetbroadcast(inet): broadcast IP of `inet'.
 * Returns varchar.
 */
{
  TXsockaddr    netmask;
  byte          *inetBytes, *netmaskBytes;
  size_t        numBytes, byteIdx;
  UNARYVARS;

  UNARYINIT();
  PARSEINET();

  netmask = inet;
  if (!TXsockaddrSetNetmask(pmbuf, &netmask, netbits))
    {
      *tmp = '\0';
      goto setval;
    }
  numBytes = TXsockaddrGetIPBytesAndLength(pmbuf, &inet, &inetBytes);
  TXsockaddrGetIPBytesAndLength(pmbuf, &netmask, &netmaskBytes);
  for (byteIdx = 0; byteIdx < numBytes; byteIdx++)
    {
      inetBytes[byteIdx] &= netmaskBytes[byteIdx];
      inetBytes[byteIdx] |= ~netmaskBytes[byteIdx];
    }

  if (!TXsockaddrToStringIP(pmbuf, &inet, tmp, sizeof(tmp)))
    return(FOP_EUNKNOWN);
setval:
  SETCHARVAL();
  return(0);
}

int
txfunc_inetnetmask(f1)
FLD     *f1;
/* inetnetmask(inet):  netmask IP of `inet'.
 * Returns varchar.
 */
{
  UNARYVARS;

  UNARYINIT();
  PARSEINET();

  if (!TXsockaddrSetNetmask(pmbuf, &inet, netbits))
    {
      *tmp = '\0';
      goto setval;
    }

  if (!TXsockaddrToStringIP(pmbuf, &inet, tmp, sizeof(tmp)))
    return(FOP_EUNKNOWN);
setval:
  SETCHARVAL();
  return(0);
}

int
txfunc_inetnetmasklen(f1)
FLD     *f1;
/* inetnetmasklen(inet):  length in bits of netmask of `inet'.
 * Returns long.
 */
{
  char                  *a;
  size_t                an;
  TXsockaddr            inet;
  int                   netbits;
  ft_long               *val;
  TXPMBUF               *pmbuf = TXPMBUFPN;

  UNARYINIT();
  val = (ft_long *)TXcalloc(pmbuf, __FUNCTION__, 2, sizeof(ft_long));
  if (!val) return(FOP_ENOMEM);
  netbits = TXinetparse(pmbuf, TXtraceDns_None, a, &inet);
  if (netbits < 0)
    *val = (ft_long)(-1L);
  else
    *val = (ft_long)netbits;
  f1->type = ((f1->type & ~(DDTYPEBITS | DDVARBIT)) | FTN_LONG);
  f1->kind = TX_FLD_NORMAL;
  f1->elsz = sizeof(ft_long);
  setfld(f1, val, 1);
  f1->n = 1;
  return(0);
}

int
txfunc_inetcontains(f1, f2)
FLD     *f1;
FLD     *f2;
/* inetcontains(inetA, inetB):  1 if `inetA' contains `inetB', 0 if not, -1
 * on error.
 * Returns long.
 */
{
  char                  *a, *b;
  size_t                an, bn;
  TXsockaddr            ineta, inetb;
  int                   netbitsa, netbitsb;
  ft_long               *val;
  TXPMBUF               *pmbuf = TXPMBUFPN;

  if (f1 == FLDPN ||
      (f1->type & DDTYPEBITS) != FTN_CHAR ||
      (a = (char *)getfld(f1, &an)) == CHARPN ||
      f2 == FLDPN ||
      (f2->type & DDTYPEBITS) != FTN_CHAR ||
      (b = (char *)getfld(f2, &bn)) == CHARPN)
    return(FOP_EINVAL);
  val = (ft_long *)TXcalloc(pmbuf, __FUNCTION__, 2, sizeof(ft_long));
  if (!val) return(FOP_ENOMEM);
  if ((netbitsa = TXinetparse(pmbuf, TXtraceDns_None, a, &ineta)) < 0)
    *val = (ft_long)(-1);
  else if ((netbitsb = TXinetparse(pmbuf, TXtraceDns_None, b, &inetb)) < 0)
    *val = (ft_long)(-1);
  else
    *val = (ft_long)(TXsockaddrNetContainsSockaddrNet(pmbuf,
                                                      &ineta, netbitsa,
                                                      &inetb, netbitsb,
                                                      /* Do not map IPv4;
                                                       * user should call
                                                       * inetToIPv6():
                                                       */
                                                      TXbool_False) ? 1 : 0);
  f1->type = ((f1->type & ~(DDTYPEBITS | DDVARBIT)) | FTN_LONG);
  f1->kind = TX_FLD_NORMAL;
  f1->elsz = sizeof(ft_long);
  setfld(f1, val, 1);
  f1->n = 1;
  return(0);
}

int
txfunc_inetclass(f1)
FLD     *f1;
/* inetclass(inet):  returns "A", "B", "C", "D", "E", "classless", or
 * empty string on error.
 * Returns varchar.
 */
{
  UNARYVARS;

  UNARYINIT();
  PARSEINET();
  TXstrncpy(tmp, TXinetclass(pmbuf, &inet, netbits), sizeof(tmp));
setval:
  SETCHARVAL();
  return(0);
}

int
txfunc_inet2int(f1)
FLD     *f1;
/* inet2int(inet):  returns integer version of IP of `inet' (hardware order).
 * Returns varint -- 0 on error, 1 int for IPv4, 4 ints for IPv6.
 */
{
  char          *a;
  size_t        an, intIdx, numBytes, numInts;
  byte          *inetBytes;
  TXsockaddr    inet;
  int           netbits, ret;
  ft_int        *val, errVal;
  TXPMBUF       *pmbuf = TXPMBUFPN;

  /* Parse IP and get its bytes: */
  UNARYINIT();
  netbits = TXinetparse(pmbuf, TXtraceDns_None, a, &inet);
  if (netbits < 0)                              /* error */
    {
      errVal = htonl(-1);
      inetBytes = (byte *)&errVal;
      /* version 8+ returns varint(0); version 7- returned -1: */
      numBytes = (TX_IPv6_ENABLED(TXApp) ? 0 : sizeof(errVal));
    }
  else
    numBytes = TXsockaddrGetIPBytesAndLength(pmbuf, &inet, &inetBytes);

  /* We assume IP byte size is a multiple of sizeof(ft_int):*/
  if (numBytes % sizeof(ft_int) != 0)
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
              "Internal error: IP byte length not a multiple of ft_int size");
      ret = FOP_EUNKNOWN;
      goto finally;
    }
  numInts = numBytes/sizeof(ft_int);

  /* Alloc and set return value: */
  val = TX_NEW_ARRAY(pmbuf, numInts + 1, ft_int);
  if (!val)
    {
      ret = FOP_ENOMEM;
      goto finally;
    }
  for (intIdx = 0; intIdx < numInts; intIdx++)
    {
      memcpy(&val[intIdx], &(((int *)inetBytes)[intIdx]), sizeof(ft_int));
      val[intIdx] = ntohl(val[intIdx]);
    }

  if (!TXsqlSetFunctionReturnData(__FUNCTION__, f1, val,
                              (TX_IPv6_ENABLED(TXApp) ? FTN_varINT : FTN_INT),
                                  FTI_UNKNOWN, sizeof(ft_int), numInts, 0))
    ret = FOP_EUNKNOWN;
  else
    ret = FOP_EOK;

finally:
  return(ret);
}

int
txfunc_int2inet(f1)
FLD     *f1;
/* int2inet(int):  returns IP version of `int' (assumes hardware order).
 * Returns varchar.
 */
{
  size_t        numVals, valIdx, numBytes;
  TXsockaddr    inet;
  char          *s;
  char          tmp[TX_SOCKADDR_MAX_STR_SZ];
  ft_int        *vals;
  TXPMBUF       *pmbuf = TXPMBUFPN;
  EPI_INT32     netVals[TX_IP_MAX_BYTE_SZ/sizeof(EPI_INT32)];

  if (f1 == FLDPN ||
      (f1->type & DDTYPEBITS) != FTN_INT ||
      !(vals = (ft_int *)getfld(f1, &numVals)))
    return(FOP_EINVAL);

  /* Directly assign TXsockaddr bytes from the integers.  Avoids
   * TXinetparse() issues like not taking negative ints (invalid
   * numbers-and-dots form), or left-shifting a small int, or IPv4
   * vs. IPv6 string syntax, or dealing with multiple ints.
   */
  for (valIdx = 0; valIdx < TX_MIN(TX_ARRAY_LEN(netVals), numVals); valIdx++)
    netVals[valIdx] = htonl(vals[valIdx]);

  switch (numBytes = numVals*sizeof(EPI_INT32))
    {
    case TX_IPv4_BYTE_SZ:
      if (!TXsockaddrSetFamilyAndIPBytes(pmbuf, &inet, TXaddrFamily_IPv4,
                                         (byte *)netVals, numBytes))
        return(FOP_EILLEGAL);
      break;
    case TX_IPv6_BYTE_SZ:
      if (!TX_IPv6_ENABLED(TXApp)) goto wrongSz;
      if (!TXsockaddrSetFamilyAndIPBytes(pmbuf, &inet, TXaddrFamily_IPv6,
                                         (byte *)netVals, numBytes))
        return(FOP_EILLEGAL);
      break;
    default:
    wrongSz:
      if (TX_IPv6_ENABLED(TXApp))
        txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                       "Invalid int2inet value `%s': Wrong number of ints: Expected %d for IPv4 or %d for IPv6",
                       fldtostr(f1),
                       (int)(TX_IPv4_BYTE_SZ/sizeof(EPI_INT32)),
                       (int)(TX_IPv6_BYTE_SZ/sizeof(EPI_INT32)));
      else
        txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                       "Invalid int2inet value `%s': Wrong number of ints: Expected %d for IPv4",
                       fldtostr(f1),
                       (int)(TX_IPv4_BYTE_SZ/sizeof(EPI_INT32)));
      return(FOP_EILLEGAL);
    }

  if (!TXsockaddrToStringIP(pmbuf, &inet, tmp, sizeof(tmp)))
    return(FOP_EUNKNOWN);
  SETCHARVAL();
  return(0);
}

int
txfunc_inetToIPv4(FLD *f1)
/* inetToIPv4(inet): returns canon IPv4 version of `inet' iff IPv4-mapped
 * (and mapped netmask is in IPv4 size), as-is if other IPv6 or IPv4,
 * empty string on error.  Returns varchar.
 */
{
  UNARYVARS;
  TXsockaddr    inetIPv4;
  int           res;

  if (!TX_IPv6_ENABLED(TXApp)) return(FOP_EILLEGAL);

  UNARYINIT();
  PARSEINET();
  res = TXsockaddrToIPv4(pmbuf, &inet, &inetIPv4);
  if (!res) goto sqlErr;
  if (res == 2 &&                               /* changed IPv6 to IPv4 */
      /* Do not map e.g. ::ffff:1.2.3.4/10 where even converted,
       * netmask would be too large:
       */
      netbits >= (TX_IPv6_BYTE_SZ - TX_IPv4_BYTE_SZ)*TX_BITS_PER_BYTE)
    {
      netbits -= (TX_IPv6_BYTE_SZ - TX_IPv4_BYTE_SZ)*TX_BITS_PER_BYTE;
      inet = inetIPv4;
    }
  if (!TXinetabbrev(pmbuf, tmp, sizeof(tmp), &inet, netbits, TXbool_True))
    {
    sqlErr:
      *tmp = '\0';
    }
setval:
  SETCHARVAL();
  return(FOP_EOK);
}

int
txfunc_inetToIPv6(FLD *f1)
/* inetToIPv6(inet): returns canon IPv6 version of `inet' iff IPv4, as-is if
 * IPv6, empty string on error.  Returns varchar.
 */
{
  UNARYVARS;
  TXsockaddr    inetIPv6;
  int           res;

  if (!TX_IPv6_ENABLED(TXApp)) return(FOP_EILLEGAL);

  UNARYINIT();
  PARSEINET();
  res = TXsockaddrToIPv6(pmbuf, &inet, &inetIPv6);
  if (!res) goto sqlErr;
  if (res == 2)                                 /* changed IPv4 to IPv6 */
    {
      netbits += (TX_IPv6_BYTE_SZ - TX_IPv4_BYTE_SZ)*TX_BITS_PER_BYTE;
      inet = inetIPv6;
    }
  if (!TXinetabbrev(pmbuf, tmp, sizeof(tmp), &inet, netbits, TXbool_True))
    {
    sqlErr:
      *tmp = '\0';
    }
setval:
  SETCHARVAL();
  return(FOP_EOK);
}

int
txfunc_inetAddressFamily(FLD *f1)
/* inetAddressFamily(inet): returns `IPv4', `IPv6', or empty string.
 */
{
  UNARYVARS;
  TXaddrFamily  addrFamily;

  if (!TX_IPv6_ENABLED(TXApp)) return(FOP_EILLEGAL);

  UNARYINIT();
  PARSEINET();
  addrFamily = TXsockaddrGetTXaddrFamily(&inet);
  switch (addrFamily)
    {
    case TXaddrFamily_Unspecified:
    case TXaddrFamily_Unknown:
      *tmp = '\0';
      break;
    default:
      TXstrncpy(tmp, TXaddrFamilyToString(addrFamily), sizeof(tmp));
      break;
    }

setval:
  SETCHARVAL();
  return(FOP_EOK);
}
