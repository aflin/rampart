#include "texint.h"
#include <errno.h>

static const char       WhiteSpace[] = " \t\r\n\v\f";

static int
TXinetParseIPv4(TXPMBUF *pmbuf, const char *ipStr, TXsockaddr *inet)
/* Parses IPv4 address `ipStr', and sets `*inet'.  Reports errors to `pmbuf'.
 * Returns number of IPv4 bytes (not values, e.g. `258' is 2 bytes) parsed.
 */
{
  static const char     invalidIPv4[] = "Invalid IPv4 address";
  const char            *s;
  char                  *e;
  EPI_UINT32            ipVal, vals[4];
  int                   numVals, valIdx, errNum, valShift, extraBytes;
#define NUM_IPv4_BYTES  sizeof(struct in_addr)

  s = ipStr;
  if (sizeof(EPI_UINT32) != NUM_IPv4_BYTES)
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__, "Internal error");
      goto err;
    }

  /* Parse up to 4 numbers separated by `.': */
  for (numVals = 0; numVals < (int)NUM_IPv4_BYTES; numVals++)
    {
      const char *valStart = s, *valEnd;

      valStart += strspn(valStart, WhiteSpace);
      if (numVals > 0 && *valStart == '.') valStart++;
      valStart += strspn(valStart, WhiteSpace);
      if (!*valStart) break;                    /* no more values */
      valEnd = valStart + strcspn(valStart, ".");
      vals[numVals] = TXstrtoui32(valStart, valEnd, &e,
                                  (0 | TXstrtointFlag_ConsumeTrailingSpace |
                                       TXstrtointFlag_TrailingSourceIsError),
                                  &errNum);
      if (errNum)                               /* invalid/out-of-range # */
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                         "%s `%s': %s number at `%s'",
                         invalidIPv4, ipStr,
                         (errNum == ERANGE ? "Out-of-range" : "Invalid"),
                         valStart);
          goto err;
        }
      s = e;
    }
  if (numVals <= 0)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     "%s `%s': No numbers given", invalidIPv4, ipStr);
      goto err;
    }

  /* Construct IP `ipVal' from `vals': */
  ipVal = 0;
  /* The first N-1 numbers must be 0-255: */
  valShift = TX_BITS_PER_BYTE*NUM_IPv4_BYTES;
  for (valIdx = 0; valIdx < numVals - 1; valIdx++)
    {
      valShift -= TX_BITS_PER_BYTE;
      if (vals[valIdx] > 255)
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                         "%s `%s': Only last number may be greater than 255",
                         invalidIPv4, ipStr);
          goto err;
        }
      ipVal |= (vals[valIdx] << valShift);
    }

  /* The last number may be 5-numVals bytes in size: */
  if ((EPI_HUGEUINT)vals[valIdx] >= ((EPI_HUGEUINT)1 << valShift))
    {
      int       numRemaining = NUM_IPv4_BYTES - valIdx;

      if (numRemaining == 1)
        txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                       "%s `%s': Last number too large for remaining byte",
                       invalidIPv4, ipStr);
      else
        txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                      "%s `%s': Last number too large for remaining %d bytes",
                       invalidIPv4, ipStr, numRemaining);
      goto err;
    }
  valShift -= TX_BITS_PER_BYTE;
  /* Reduce shift -- and increase number of bytes counted -- for each
   * extra byte (i.e. beyond 255) in last number:
   */
  for (extraBytes = 0; extraBytes < (int)NUM_IPv4_BYTES-numVals; extraBytes++)
    if (vals[valIdx] < (1U << (extraBytes + 1)*TX_BITS_PER_BYTE))
      break;
  valShift -= extraBytes*TX_BITS_PER_BYTE;
  numVals += extraBytes;
  ipVal |= (vals[valIdx] << valShift);

  /* Set return value: */
  TXsockaddrInit(inet);
  ipVal = htonl(ipVal);
  if (!TXsockaddrSetFamilyAndIPBytes(pmbuf, inet, TXaddrFamily_IPv4,
                                     (byte *)&ipVal, sizeof(ipVal)))
    goto err;
  goto finally;

err:
  TXsockaddrInit(inet);
  numVals = 0;
finally:
  return(numVals);
}

int
TXinetparse(TXPMBUF *pmbuf, TXtraceDns traceDns, const char *inetStr,
            TXsockaddr *inet)
/* Parses IP/network/mask `s', and sets `*net'.  inet form is:
 *
 * n == decimal/octal/hex number 0 to 255
 * b == decimal/octal/hex number 0 to 32
 * h == 1-4 hex digits
 * IPv4 == n[.n[.n[.n]]]
 *   note: rightmost n covers rightmost (5 - (#-of-n's)) bytes
 * IPv6 == h:[h...::]  (valid IPv6 address)
 * inet == {IPv4[{/b|:IPv4}]}|{IPv6[/n]}
 *   (second IPv4 is netmask e.g. 255.255.255.0)
 *
 * If mask is missing, calculated from A/B/C/D/E class rules iff ipv4
 * (but will be large enough to include all specified bytes of IP);
 * else netmask is host-only if IPv6.
 *
 * Modeled after PostgreSQL inet type, include right-padding of
 * missing bytes -- note that getaddrinfo() and hence
 * TXstringIPToSockaddr() do *left*-padding, per numbers-and-dots
 * notation.  E.g. PostgreSQL (and this function) take `192.168' as
 * `192.168.0.0', whereas TXstringIPToSockaddr() takes it as
 * `192.0.0.168'.  We accept this padding difference between the
 * parsers because the inet "type" is a network: in that context,
 * `192.168' would more strongly be considered `192.168.0.0' rather
 * than `192.0.0.168'.  Also, because inet is a network whereas
 * TXstringIPToSockaddr() takes a host, the syntaxes do not
 * necessarily have to agree; the latter would never accept an
 * explicit netmask for example.  Bug 7348 also discusses this.
 *
 * `*inet' set to network byte order.
 * Returns number of bits in netmask, or -1 on error.
 */
{
  /* Valid IP chars are decimal, `x' (for `0x') and `.' for IPv4;
   * hex, `.' and `:' for IPv6.
   * Note that `-' for negative single int for IPv4 is not accepted
   * by TXstringIPToSockaddr(); it should only occur for int2inet(),
   * and is handled special by that function.
   */
  static const char     okIPChars[] = "0123456789abcdefABCDEFxX.: \t\r\n\v\f";
  static const char     invalidInet[] = "Invalid inet value";
  const char            *s, *e;
  TXsockaddr            ipNetmask;
  TXaddrFamily          addrFamily;
  char                  firstSep, *numEnd;
  int                   bits, errNum, maxBits, numBits;
  int                   numIPv4BytesGiven = -1;
  size_t                len;
  byte                  *addrBytes;
  char                  addrBuf[TX_SOCKADDR_MAX_STR_SZ + 512];

  /* Copy the leading IP-like chars of `inetStr' to `addrBuf' for parsing.
   * This also excludes wildcard address `*', which we feel is not valid here:
   */
  firstSep = '\0';
  for (e = inetStr; *e && strchr(okIPChars, *e); e++)
    if (*e == '.' || *e == ':')
      {
        /* For IPv4, we allow netmask to be given as an IP -- after a
         * colon.  So we need to stop at colon iff it looks like an
         * IPv4 netmask follows, in order to parse the first IPv4
         * address alone.  We thus assume a colon is an IPv4 netmask
         * prefix iff the first separator we saw was a dot.  If this
         * is an IPv4-mapped IPv6 address instead, we will also see
         * both dots and colons -- but we see colons first, so we will
         * thus not stop at a later colon.
         */
        if (!firstSep)
          firstSep = *e;
        else if (firstSep == '.' && *e == ':')  /* probable IPv4 netmask */
          break;
      }
  /* Include trailing `%eth0' scope if IPv6 enabled: */
  if (*e == '%' && TX_IPv6_ENABLED(TXApp))
    {
      for (e++; *e && *e != ':' && *e != '/'; e++);
    }

  len = (size_t)(e - inetStr);
  if (len > sizeof(addrBuf) - 1)
    {
      /* wtf increase `addrBuf' size?  Should only be needed for spaces */
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     "%s `%s': Too long", invalidInet, inetStr);
      goto err;
    }
  if (len > 0) memcpy(addrBuf, inetStr, len);
  addrBuf[len] = '\0';

  /* Avoid cryptic parse error for empty IP string: */
  if (len == 0)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     "%s `%s': No IP prefix", invalidInet,
                     inetStr);
      goto err;
    }

  /* inet IPv4 syntax is different than TXstringIPToSockaddr()'s;
   * use TXinetParseIPv4():
   */
  if (firstSep == '.' || firstSep == '\0')      /* IPv4 */
    {
      numIPv4BytesGiven = TXinetParseIPv4(pmbuf, addrBuf, inet);
      if (numIPv4BytesGiven <= 0) goto err;
    }
  else if (!TXstringIPToSockaddr(pmbuf,
                                 TXbool_False,  /* do not suppress errors */
                                 traceDns, TXaddrFamily_Unspecified, addrBuf,
                                 TXbool_False,  /* no okIPv4WithIPv6Any */
                                 inet))
    goto err;

  addrFamily = TXsockaddrGetTXaddrFamily(inet);
  maxBits = TX_BITS_PER_BYTE*TXsockaddrGetIPBytesAndLength(pmbuf, inet, NULL);
  s = e;                                        /* strict: no whitespace */
  switch (*s)
    {
    case '/':                                   /* IP/N */
      s++;
      bits = (int)TXstrtos(s, NULL, &numEnd,
                           (0 | TXstrtointFlag_NoLeadZeroOctal |
                            TXstrtointFlag_ConsumeTrailingSpace |
                            TXstrtointFlag_TrailingSourceIsError), &errNum);
      if (errNum)
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                "%s `%s': Netmask bits number `%s' invalid; expected integer",
                         invalidInet, inetStr, s);
          goto err;
        }
      if (bits < 0 || bits > maxBits)
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                         "%s `%s': Netmask bits number `%s' out of range; expected 0 to %d for an %s address",
                         invalidInet, inetStr, s, (int)maxBits,
                         TXaddrFamilyToString(addrFamily));
          goto err;
        }
      s = numEnd;                               /* consumed netbits */
      break;
    case ':':                                   /* IP:IP */
      s++;
      /* IP netmask is legacy, thus only supported for IPv4: */
      if (addrFamily != TXaddrFamily_IPv4)
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                         "%s `%s': IP netmask only accepted for %s address",
                         invalidInet, inetStr,
                         TXaddrFamilyToString(addrFamily));
          goto err;
        }
      /* inet IPv4 syntax is different than TXstringIPToSockaddr()'s;
       * use TXinetParseIPv4():
       */
      if (TXinetParseIPv4(pmbuf, s, &ipNetmask) <= 0) goto err;
      /* Derive mask: contiguous number of most-significant 1 bits: */
      numBits = TX_BITS_PER_BYTE*TXsockaddrGetIPBytesAndLength(pmbuf, &ipNetmask,
                                                            &addrBytes);
      for (bits = 0;
           bits < numBits && (addrBytes[bits/TX_BITS_PER_BYTE] &
                      (1U << ((TX_BITS_PER_BYTE - 1) - (bits % TX_BITS_PER_BYTE))));
           bits++)
        ;
      s += strspn(s, okIPChars);                /* wtf assumed parsed */
      break;
    default:                                    /* IP (no netmask nor bits) */
      /* Determine netmask from Class A/B/C/D/E rules (iff IPv4): */
      numBits = TX_BITS_PER_BYTE*TXsockaddrGetIPBytesAndLength(pmbuf, inet,
                                                            &addrBytes);
      if (addrFamily == TXaddrFamily_IPv4)
        {
          if ((addrBytes[0] & 0x80) == 0)
            bits = 8;                           /* Class A */
          else if ((addrBytes[0] & 0x40) == 0)
            bits = 16;                          /* Class B */
          else if ((addrBytes[0] & 0x20) == 0)
            bits = 24;                          /* Class C */
          else
            bits = 32;                          /* Class D/E */
          /* Make sure netmask is large enough for all specified bytes.
           * Important for inetcontains('1.2.3/24', '1.2.3.4') to be true
           * (otherwise `1.2.3.4' is `1.2.3.4/8' due to Class A rule)
           */
          if (numIPv4BytesGiven >= 0 &&
              bits < TX_BITS_PER_BYTE*numIPv4BytesGiven)
            bits = TX_BITS_PER_BYTE*numIPv4BytesGiven;
        }
      else
        /* No classes in IPv6; use /128 because all bytes of address
         * are always given (and thus we want an IPv6 without
         * netmask/bits to be a single-host network, just like in
         * IPv4, e.g. for second arg of inetcontains()):
         */
        bits = 128;
      break;
    }
  if (*s != '\0')                               /* extra crap */
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     "%s `%s': Unknown trailing text `%s'",
                     invalidInet, inetStr, s);
      goto err;
    }
  goto finally;

err:
  TXsockaddrInit(inet);
  bits = -1;
finally:
  return(bits);
}

TXbool
TXinetabbrev(TXPMBUF *pmbuf, char *d, size_t dlen, const TXsockaddr *inet,
             int netBits, TXbool canon)
/* Writes shortest representation of `inet/netBits' to `d' (includes
 * netmask iff not single-host network).  This is always the canonical
 * representation iff `inet' is not IPv4, which for IPv6 will include
 * `::' truncation of zeroes.  Otherwise, for IPv4, the abbreviated
 * representation will include all contiguous MSBs of network and
 * non-zero bytes of host.  If `canon', will not trim IP bytes.
 * Returns false on error.
 */
{
  int           mbits;
  char          *orgD = (dlen > 0 ? d : NULL);
  TXbool        ret;
  byte          *ipBytes;
  size_t        lenPrinted, byteIdx, numMsbSet, numIPBytes;

  numIPBytes = TXsockaddrGetIPBytesAndLength(pmbuf, inet, &ipBytes);

  if (TXsockaddrGetTXaddrFamily(inet) != TXaddrFamily_IPv4)
    {
      /* Canon form of IPv6 address is the abbreviated form: */
      if (!TXsockaddrToStringIP(pmbuf, inet, d, dlen)) goto err;
      lenPrinted = strlen(d);
      if (lenPrinted >= dlen) goto err;         /* not enough space */
      d += lenPrinted;
      dlen -= lenPrinted;
    }
  else                                          /* IPv4 */
    {
      /* Count number of MSB bytes that are set (non-zero): */
      for (numMsbSet = 0; numMsbSet < numIPBytes; numMsbSet++)
        if (ipBytes[numMsbSet] == 0) break;

      /* Note that printing at least `netBits' worth of the IP is also
       * necessary for TXinetparse() reciprocity if we suppress
       * netmask below:
       */
      mbits = (canon ? (int)numIPBytes*TX_BITS_PER_BYTE : netBits);
      for (byteIdx = 0; byteIdx < (size_t)numIPBytes; )
        {
          lenPrinted = htsnpf(d, dlen, "%s%u", (byteIdx > 0 ? "." : ""),
                              (unsigned)ipBytes[byteIdx]);
          if (lenPrinted >= dlen) goto err;     /* not enough space */
          d += lenPrinted;
          dlen -= lenPrinted;
          byteIdx++;
          if ((int)byteIdx*TX_BITS_PER_BYTE >= mbits &&    /* got netmask and */
              byteIdx >= numMsbSet)             /* remainder of IP is 0 */
            break;
        }
    }

  /* We do not print netmask if this is a single-host network, for
   * brevity like PostgreSQL.  This still maintains TXinetparse()
   * reciprocity: since above we also printed all bytes covered by the
   * netmask, we know we printed a full IP address, so a parse of that
   * without a netmask will properly get us the same single-host netmask.
   */
  if ((size_t)netBits != numIPBytes*TX_BITS_PER_BYTE)  /* not single-host net */
    {
      lenPrinted = htsnpf(d, dlen, "/%d", netBits);
      if (lenPrinted >= dlen) goto err;         /* not enough space */
      d += lenPrinted;
      dlen -= lenPrinted;
    }

  ret = TXbool_True;
  goto finally;

err:
  if (orgD) *orgD = '\0';
  ret = TXbool_False;
finally:
  return(ret);
}

const char *
TXinetclass(TXPMBUF *pmbuf, const TXsockaddr *inet, int netBits)
/* Returns class of `inet/netBits': "A", "B", "C", "D", "E" or "classless".
 */
{
  size_t        numBytes;
  byte          *bytes;

  if (TXsockaddrGetTXaddrFamily(inet) != TXaddrFamily_IPv4)
    return("classless");
  numBytes = TXsockaddrGetIPBytesAndLength(pmbuf, inet, &bytes);
  if (numBytes <= 0) return("");                /* error */

  if ((bytes[0] & 0x80) == 0)
    {
      if (netBits == 8) return("A");
    }
  else if ((bytes[0] & 0x40) == 0)
    {
      if (netBits == 16) return("B");
    }
  else if ((bytes[0] & 0x20) == 0)
    {
      if (netBits == 24) return("C");
    }
  else if ((bytes[0] & 0x10) == 0)
    {
      if (netBits == 32) return("D");
    }
  else if ((bytes[0] & 0x08) == 0)
    {
      if (netBits == 32) return("E");
    }
  return("classless");
}
