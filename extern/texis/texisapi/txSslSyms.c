#include "texint.h"
#include <string.h>

#ifdef HAVE_OPENSSL
#include "txlic.h"
#include "txSslSyms.h"
#include "openssl/ssl.h"
#include "openssl/err.h"
#include "openssl/rand.h"
#include "openssl/engine.h"
#include "openssl/hmac.h"
#include "openssl/x509v3.h"
#include "openssl/md4.h"
#include "openssl/bn.h"
#include "openssl/des.h"

int             TXcheckSslVersion = 1;
#undef I
#define I(ret, func, args)      NULL,
TXCRYPTOSYMBOLS TXcryptoSymbols = { TXCRYPTOSYMBOLS_LIST };
TXSSLSYMBOLS    TXsslSymbols = { TXSSLSYMBOLS_LIST };
#undef I

#ifndef EPI_STATIC_SSL
static const char * const       TXcryptoSymbolNames[] =
{
#  undef I
#  define I(ret, func, args)  #func,
  TXCRYPTOSYMBOLS_LIST
#  undef I
};
#  define TXCRYPTOSYMBOLS_NUM   \
  (sizeof(TXcryptoSymbolNames)/sizeof(TXcryptoSymbolNames[0]))
static const char * const       TXsslSymbolNames[] =
{
#  undef I
#  define I(ret, func, args)  #func,
  TXSSLSYMBOLS_LIST
#  undef I
};
#  define TXSSLSYMBOLS_NUM \
  (sizeof(TXsslSymbolNames)/sizeof(TXsslSymbolNames[0]))
#endif /* !EPI_STATIC_SSL */
static TXLIB            *TXcryptoLib = TXLIBPN, *TXsslLib = TXLIBPN;
int                     TXsslHtsktExDataIndex = -1;
#ifndef EPI_STATIC_SSL
static const char       TXcryptoLibName[] = SSLCRYLIBNAME;
static const char       TXsslLibName[] = SSLSSLLIBNAME;
#endif /* !EPI_STATIC_SSL */


/* ------------------------------------------------------------------------ */

#ifndef EPI_STATIC_SSL
static int
TXcheckFileSslVersion(TXPMBUF *pmbuf, int post, const char *file,
                      const char *path)
/* Check version to ensure binary compatibility.  Allow override so users
 * can replace the DLLs with third-party versions if desired.
 * Returns 2 if ok version, 1 if not ok version, 0 on other error.
 */
{
  static const char     fn[] = "TXcheckFileSslVersion";
  int                   ret = 2;
  unsigned long         vers;
#  ifdef _WIN32
  char                  **list;
  size_t                listn, i;
  DWORD                 infosz, dumhandle;
  CONST char            *f = file;
  void                  *buf = NULL;
  VS_FIXEDFILEINFO      *ffi;
  UINT                  ffilen;
#  else /* !_WIN32 */

  (void)path;
#  endif /* !_WIN32 */

  if (!TXcheckSslVersion) goto done;            /* user override: says ok */

  if (post)                                     /* after-load check */
    {
      if (strcmp(file, TXcryptoLibName) == 0)
        {
          vers = TXcryptoSymbols.OpenSSL_version_num();
          if (vers == OPENSSL_VERSION_NUMBER)   /* what we compiled with */
            ret = 2;                            /* ok */
          else if (TX_SSL_GET_MAJOR_MINOR_NUM(OPENSSL_VERSION_NUMBER) == 9 &&
                   TX_SSL_GET_FIX_NUM(OPENSSL_VERSION_NUMBER) == 7)
            {                                   /* we compiled with 0.9.7 */
              if (vers == 0x907003L ||          /* OpenSSL 0.9.7-beta3 */
                  vers == 0x907004L ||          /* OpenSSL 0.9.7-beta4 */
                  vers == 0x90705fL)            /* OpenSSL 0.9.7e */
                ret = 2;                        /* ok */
              else
                ret = 1;                        /* probably incompatible */
            }
          else if (TX_SSL_GET_MAJOR_MINOR_NUM(OPENSSL_VERSION_NUMBER) >= 0x100
                   &&
                   TX_SSL_GET_MAJOR_MINOR_NUM(OPENSSL_VERSION_NUMBER) ==
                   TX_SSL_GET_MAJOR_MINOR_NUM(vers))
            ret = 2;                            /* >=1.0.x and maj.min agree*/
          else
            ret = 1;                            /* incompatible */
          if (ret == 1)
            txpmbuf_putmsg(pmbuf, MERR, fn,
                          "%s is wrong OpenSSL version 0x%lx: expected 0x%lx",
                           file, vers, (unsigned long)OPENSSL_VERSION_NUMBER);
        }
      /* wtf also call some version number function in TXsslLibName? */
    }
  else                                          /* pre-load check */
#  ifdef _WIN32
    {
      int       fileMajorNum, fileMinorNum, fileFixNum, filePatchNum;

      if (path != CHARPN)
        {
          if ((listn = TXlib_expandpath(path, &list)) == 0) goto perr;
          for (i = 0; i < listn; i++)
            {
              if (list[i] == CHARPN) continue;  /* WTF %SYSLIBPATH% */
              f = epipathfindmode((char *)file, list[i], 0x8);
              if (f != CHARPN) break;
            }
          TX_PUSHERROR();
          TXlib_freepath(list, listn);
          TX_POPERROR();
          if (f == CHARPN) goto perr;
        }
      dumhandle = (DWORD)0;
      infosz = GetFileVersionInfoSize((LPTSTR)f, &dumhandle);
      if (infosz == (DWORD)0) goto perr;
      if ((buf = malloc(infosz)) == NULL) goto perr;
      dumhandle = (DWORD)0;
      if (!GetFileVersionInfo((LPTSTR)f, dumhandle, infosz, buf)) goto perr;
      if (!VerQueryValue(buf, "\\", &ffi, &ffilen)) goto perr;
      fileMajorNum = (ffi->dwFileVersionMS >> 16);
      fileMinorNum = (ffi->dwFileVersionMS & 0xffff);
      fileFixNum = (ffi->dwFileVersionLS >> 16);
      filePatchNum = (ffi->dwFileVersionLS & 0xffff);
#    if TX_SSL_GET_MAJOR_MINOR_NUM(OPENSSL_VERSION_NUMBER) < 0x100 /* <1.0.x*/
      if (fileMajorNum != TX_SSL_WINDOWS_FILEVERSION_MAJOR)
        {
          txpmbuf_putmsg(pmbuf, MERR, fn,
            "%s is wrong file version %d.%d.%d.%d: expected major version %d",
                         f, fileMajorNum, fileMinorNum, fileFixNum,
                         filePatchNum, (int)TX_SSL_WINDOWS_FILEVERSION_MAJOR);
          ret = 1;
        }
#    else /* >= 1.0.x */
      if (TX_SSL_GET_MAJOR_MINOR_NUM(OPENSSL_VERSION_NUMBER) !=
          ((fileMajorNum << 8) | (fileMinorNum)))
        {                                       /* major.minor disagree */
          txpmbuf_putmsg(pmbuf, MERR, fn,
           "%s is wrong file version %d.%d.%d.%d: expected version %d.%d.n.n",
                         f, fileMajorNum, fileMinorNum, fileFixNum,
                         filePatchNum,
                         (int)TX_SSL_GET_MAJOR_NUM(OPENSSL_VERSION_NUMBER),
                         (int)TX_SSL_GET_MINOR_NUM(OPENSSL_VERSION_NUMBER));
          ret = 1;
        }
#    endif /* >= 1.0.x */
      else
        ret = 2;
    }
  goto done;

perr:
  txpmbuf_putmsg(pmbuf, MERR, fn, "Cannot check file version of `%s': %s",
                 (f != CHARPN ? f : file), TXstrerror(TXgeterror()));
  ret = 0;
done:
  buf = TXfree(buf);
  if (f && f != file) f = TXfree((char *)f);
#  else /* !_WIN32 */
  ret = ret;                                    /* no-op */
done:
#  endif /* !_WIN32 */
  return(ret);
}
#endif /* !EPI_STATIC_SSL */

int
TXsslLoadLibrary(pmbuf)
TXPMBUF *pmbuf;         /* (in) (optional) */
/* Loads OpenSSL functions from shared library.  Returns 0 on error.
 * Called once per exec.  NOTE: see also htloadjdomsymbols().
 */
{
  static int            TXsslSerial = 0;  /* <0:fail-serial 0:no try >0: ok */
#ifndef EPI_STATIC_SSL
  char                  *path;
#endif /* !EPI_STATIC_SSL */

  /* Only attempt to load once, to save time/errors.  But if we failed
   * to load *and* the lib path has changed since the attempt, try again:
   */
  if (TXsslSerial < 0 && TXgetlibpathserial() > -TXsslSerial)
    TXsslSerial = 0;
  if (TXsslSerial) return(TXsslSerial > 0);

  if (!TXlic_okssl(1)) goto err;

#ifdef EPI_STATIC_SSL
  (void)pmbuf;
  memset(&TXcryptoSymbols, 0, sizeof(TXCRYPTOSYMBOLS));
#  undef I
#  define I(ret, func, args)    TXcryptoSymbols.func = func;
  TXCRYPTOSYMBOLS_LIST
#  undef I
  memset(&TXsslSymbols, 0, sizeof(TXSSLSYMBOLS));
#  undef I
#  define I(ret, func, args)    TXsslSymbols.func = func;
  TXSSLSYMBOLS_LIST
#  undef I
#else /* !EPI_STATIC_SSL */
  path = TXgetlibpath();
  /* libssl depends on libcrypto, so load libcrypto first.  However,
   * on some platforms we had to statically link libcrypto into libssl
   * to get libssl to resolve here at load time, which would bloat us with
   * an extra copy of libcrypto.  So make the libcrypto load optional
   * (and silent): if it fails, just try to load its symbols from libssl:
   */
  TXcryptoLib = TXopenlib(TXcryptoLibName, path, 0, pmbuf);
  if (TXcryptoLib != TXLIBPN)
    {
      if (TXcheckFileSslVersion(pmbuf, 0, TXcryptoLibName, path) != 2)
        goto err;
      if (TXlib_getaddrs(TXcryptoLib, pmbuf, TXcryptoSymbolNames,
                         /*`char *' cast avoids gcc strict-aliasing warning:*/
                         (void **)(char *)&TXcryptoSymbols,
                         TXCRYPTOSYMBOLS_NUM) != TXCRYPTOSYMBOLS_NUM)
        goto err;
      if (TXcheckFileSslVersion(pmbuf, 1, TXcryptoLibName, path) != 2)
        goto err;
    }
  TXsslLib = TXopenlib(TXsslLibName, path, 1, pmbuf);
  if (TXsslLib == TXLIBPN) goto err;
  if (TXcheckFileSslVersion(pmbuf, 0, TXsslLibName, path) != 2) goto err;
  if (TXcryptoLib == TXLIBPN)                   /* get missing symbols */
    {
      if (TXlib_getaddrs(TXsslLib, pmbuf, TXcryptoSymbolNames,
                         /*`char *' cast avoids gcc strict-aliasing warning:*/
                         (void **)(char *)&TXcryptoSymbols,
                         TXCRYPTOSYMBOLS_NUM) != TXCRYPTOSYMBOLS_NUM ||
          TXcheckFileSslVersion(pmbuf, 1, TXcryptoLibName, path) != 2)
        goto err;
    }
  if (TXlib_getaddrs(TXsslLib, pmbuf, TXsslSymbolNames,
                     /* `char *' cast avoids gcc strict-aliasing warning: */
                     (void **)(char *)&TXsslSymbols, TXSSLSYMBOLS_NUM) !=
      TXSSLSYMBOLS_NUM)
    goto err;
  if (TXcheckFileSslVersion(pmbuf, 1, TXsslLibName, path) != 2) goto err;
#endif /* !EPI_STATIC_SSL */

  /* - - - - - - - - - - - - Once-per-process init: - - - - - - - - - - - - */
  /* TXsslSymbols.OPENSSL_init_ssl(0, NULL); */
  /* wtf in OpenSSL `OPENSSL_add_all_algorithms_noconf()' is wrapped via
   * OpenSSL_add_all_algorithms() macro.  Call not needed for clients
   * (but improves <hash> algorithm support); not sure yet if actually
   * needed for servers:  KNG 20070504
   */
  TXcryptoSymbols.OPENSSL_init_crypto((OPENSSL_INIT_ADD_ALL_CIPHERS |
                                       OPENSSL_INIT_ADD_ALL_DIGESTS), NULL);
  TXsslSymbols.OPENSSL_init_ssl((OPENSSL_INIT_LOAD_SSL_STRINGS |
                                 OPENSSL_INIT_LOAD_CRYPTO_STRINGS), NULL);
  /* EGD no longer supported by default in OpenSSL 1.1.0+: */
  /* if (!TXcryptoSymbols.RAND_status() && */
  /*     (path = TXgetentropypipe()) != CHARPN && */
  /*     strcmp(path, "none") != 0) */
  /*   TXcryptoSymbols.RAND_egd_bytes(path, 255); */
  /* See also RAND_add() */
#ifdef EPI_HAVE_THREADS
  /* OpenSSL should have been configured with thread support.
   * Note that this is a compile-time check: we have no way of knowing
   * if the actual run-time loaded lib has thread support (nor if we got
   * the wrong "openssl/opensslconf.h" header above, e.g. native platform
   * headers vs. ours).  Yap, but stumble on for single-threaded use:
   */
#  ifndef OPENSSL_THREADS
  txpmbuf_putmsg(pmbuf, MWARN + UGE, __FUNCTION__,
                 "SSL library configured without thread support");
#  endif /* !OPENSSL_THREADS */
  /* KNG 20070628 CRYPTO_num_locks() returns 35; may vary? */
  /* KNG 20180809 OpenSSL says it is no longer necessary to set
   * locking callbacks for threads; OpenSSL uses a new threading API
   */
#endif /* EPI_HAVE_THREADS */
  /* Register for storage of user-data (HTSKT *) in SSL objects: */
  TXsslHtsktExDataIndex =
    TXcryptoSymbols.CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_SSL,
                                            0, NULL, NULL, NULL, NULL);

  TXsslSerial = TXgetlibpathserial();
  return(1);

  /* - - - - - - - - - - - - - - - Cleanup: - - - - - - - - - - - - - - - - */
err:
  memset(&TXsslSymbols, 0, sizeof(TXSSLSYMBOLS));
  TXsslLib = TXcloselib(TXsslLib);
  memset(&TXcryptoSymbols, 0, sizeof(TXCRYPTOSYMBOLS));
  TXcryptoLib = TXcloselib(TXcryptoLib);
  TXsslSerial = -TXgetlibpathserial();
  return(0);
}

#else
int             TXcheckSslVersion = -1;
#endif
