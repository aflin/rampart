#include "txcoreconfig.h"
#ifdef unix
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_OPENSSL
#include "openssl/des.h"
#include "openssl/md5.h"
#include "openssl/sha.h"
#endif
#include "os.h"
#include "passwd.h"
#include "mmsg.h"
#include "txSslSyms.h"
#include "dbquery.h"
#include "cgi.h"
#include "texint.h"

#ifdef HAVE_OPENSSL
/* Different array than Base64 encoding, kept for compatibility
 * with existing salts
 */

 /*
  * i64c - convert an integer to a radix 64 character
  */
static char Lookup[] =
	"./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static int
i64c(unsigned i)
{
	if (i > 63)
		return ('z');
	return Lookup[i];
}

static char *
TXpwEncryptMD5(TXPMBUF *pmbuf,
	       const char *clearPass,
	       const char *salt,		/* after `$id$' */
	       size_t saltLen)
/* Implements crypt() ID 1 style -- MD5 (TXpwEncryptMethod_MD5).
 * Adapted from OpenSSL passwd.c.
 * Returns alloc'd encrypted passwd-style salted password.
 */
{
	static const char	idStr[] = "$1$";
#define ID_STR_LEN	(sizeof(idStr) - 1)
#define MAX_SALT_LEN	8			/* per OpenSSL passwd.c */
	/* `$1$...salt...$...MD5-hash...': */
	char		outBuf[ID_STR_LEN + MAX_SALT_LEN + 1 + 24 + 2];
	byte		md5Buf[MD5_DIGEST_LENGTH];
	byte		md5MappedBuf[MD5_DIGEST_LENGTH];
	char		*d = outBuf, *e = outBuf + sizeof(outBuf);
	char		*ret = NULL;
	EVP_MD_CTX	*mdCtx1 = NULL, *mdCtx2 = NULL;
	size_t		clearPassLen, i, n, srcIdx, destIdx;

	clearPassLen = strlen(clearPass);

	/* Start constructing output: */
	memcpy(d, idStr, ID_STR_LEN);
	d += ID_STR_LEN;

	/* Stop at `$' in salt if present; (earlier) encrypt/hash follows.
	 * And max length is MAX_SALT_LEN:
	 */
	saltLen = TXstrcspnBuf(salt, salt + saltLen, "$", 1);
	saltLen = TX_MIN(saltLen, MAX_SALT_LEN);
	memcpy(d, salt, saltLen);
	d += saltLen;

	/* Do hash algorithm: */
	if (!TXsslLoadLibrary(pmbuf)) goto err;

	if (!(mdCtx1 = TXcryptoSymbols.EVP_MD_CTX_new()) ||
	    !TXcryptoSymbols.EVP_DigestInit_ex(mdCtx1,
					       TXcryptoSymbols.EVP_md5(),
					       NULL) ||
	    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx1, clearPass,
					      clearPassLen) ||
	    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx1, idStr, ID_STR_LEN) ||
	    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx1, salt, saltLen))
		goto cryptErr;

	if (!(mdCtx2 = TXcryptoSymbols.EVP_MD_CTX_new()) ||
	    !TXcryptoSymbols.EVP_DigestInit_ex(mdCtx2,
					       TXcryptoSymbols.EVP_md5(),
					       NULL) ||
	    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx2, clearPass,
					      clearPassLen) ||
	    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx2, salt, saltLen) ||
	    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx2, clearPass,
					      clearPassLen) ||
	    !TXcryptoSymbols.EVP_DigestFinal_ex(mdCtx2, md5Buf, NULL))
		goto cryptErr;

	for (i = clearPassLen; i > sizeof(md5Buf); i -= sizeof(md5Buf))
		if (!TXcryptoSymbols.EVP_DigestUpdate(mdCtx1, md5Buf,
						      sizeof(md5Buf)))
			goto cryptErr;
	if (!TXcryptoSymbols.EVP_DigestUpdate(mdCtx1, md5Buf, i))
		goto cryptErr;

	for (n = clearPassLen; n > 0; n >>= 1)
		if (!TXcryptoSymbols.EVP_DigestUpdate(mdCtx1,
					     ((n & 1) ? "\0" : clearPass), 1))
			goto cryptErr;
	if (!TXcryptoSymbols.EVP_DigestFinal_ex(mdCtx1, md5Buf, NULL))
		goto cryptErr;

	for (i = 0; i < 1000; i++)
	{
		if (!TXcryptoSymbols.EVP_DigestInit_ex(mdCtx2,
				     TXcryptoSymbols.EVP_md5(), NULL))
			goto cryptErr;
		if (!TXcryptoSymbols.EVP_DigestUpdate(mdCtx2,
				 ((i & 1) ? (byte *)clearPass : md5Buf),
				 ((i & 1) ? clearPassLen : sizeof(md5Buf))))
			goto cryptErr;
		if ((i % 3) &&
		    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx2, salt, saltLen))
			goto cryptErr;
		if ((i % 7) &&
		    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx2, clearPass,
						      clearPassLen))
			goto cryptErr;
		if (!TXcryptoSymbols.EVP_DigestUpdate(mdCtx2,
				  ((i & 1) ? md5Buf : (byte *)clearPass),
				  ((i & 1) ? sizeof(md5Buf) : clearPassLen)))
			goto cryptErr;
		if (!TXcryptoSymbols.EVP_DigestFinal_ex(mdCtx2, md5Buf, NULL))
		{
		cryptErr:
			txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
				       "MD5 hash function failed");
			goto err;
		}
	}

	/* Map to output: */
	if (MD5_DIGEST_LENGTH != 16) goto lenErr;
	for (srcIdx = destIdx = 0;
	     destIdx < 14;
	     destIdx++, srcIdx = (srcIdx + 6) % 17)
		md5MappedBuf[destIdx] = md5Buf[srcIdx];
	md5MappedBuf[14] = md5Buf[5];
	md5MappedBuf[15] = md5Buf[11];

	*(d++) = '$';
	for (i = 0; i < 15; i += 3)
	{
		*(d++) = i64c(md5MappedBuf[i + 2] & 0x3f);
		*(d++) = i64c(((md5MappedBuf[i + 1] & 0xf) << 2) |
			       (md5MappedBuf[i + 2] >> 6));
		*(d++) = i64c(((md5MappedBuf[i] & 3) << 4) |
			       (md5MappedBuf[i + 1] >> 4));
		*(d++) = i64c(md5MappedBuf[i] >> 2);
	}
	if (i != 15) goto lenErr;

	*(d++) = i64c(md5MappedBuf[i] & 0x3f);
	*(d++) = i64c(md5MappedBuf[i] >> 6);
	*d = '\0';
	if (d >= e)
	{
	lenErr:
		txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
			       "Length check failed");
		goto err;
	}

	/* And dup for return: */
	if (!(ret = TXstrdup(pmbuf, __FUNCTION__, outBuf))) goto err;
	goto finally;

err:
	ret = TXfree(ret);
finally:
	if (mdCtx1)
	{
		TXcryptoSymbols.EVP_MD_CTX_free(mdCtx1);
		mdCtx1 = NULL;
	}
	if (mdCtx2)
	{
		TXcryptoSymbols.EVP_MD_CTX_free(mdCtx2);
		mdCtx2 = NULL;
	}
	return(ret);
#undef ID_STR_LEN
#undef MAX_SALT_LEN
}

static char *
TXpwEncryptSHA(TXPMBUF *pmbuf,
	       TXpwEncryptMethod method,	/* -256 or -512 */
	       const char *clearPass,
	       const char *salt,		/* e.g. `[rounds=n$]..salt' */
	       size_t saltLen)
/* Implements crypt() ID 5/6 styles -- SHA (TXpwEncryptMethod_SHA{256,512}).
 * Adapted from OpenSSL passwd.c.
 * Returns alloc'd encrypted passwd-style salted password.
 */
{
	static const char	sha256idStr[] = "$5$";
	static const char	sha512idStr[] = "$6$";
#define MAX_ID_STR_LEN	(TX_MAX(sizeof(sha256idStr), sizeof(sha512idStr)) - 1)
	const char		*idStr = NULL;
	size_t			idStrLen = 0;
	static const char	roundsStr[] = "rounds=";
#define MAX_SALT_LEN	16			/* per OpenSSL passwd.c */
	/* `$6$[rounds=n$]...salt...$...SHA-hash...': */
	char		outBuf[MAX_ID_STR_LEN +
	               (sizeof(roundsStr) + TX_PWENCRYPT_ROUNDS_MAX_DIGITS) +
		               (MAX_SALT_LEN + 1) + 86 + 2];
	byte	shaBuf[TX_MAX(SHA256_DIGEST_LENGTH, SHA512_DIGEST_LENGTH)];
	byte	shaBuf2[TX_MAX(SHA256_DIGEST_LENGTH, SHA512_DIGEST_LENGTH)];
	size_t		digestSz = 0;
	byte		*passDupBuf = NULL, *pd, *saltDupBuf = NULL;
	char		*d = outBuf, *e = outBuf + sizeof(outBuf);
	char		*ret = NULL;
	EVP_MD_CTX	*mdCtx1 = NULL, *mdCtx2 = NULL;
	const EVP_MD	*sha = NULL;
	size_t		clearPassLen, i, n;
	unsigned	rounds = (TXApp ?
				  TXApp->defaultPasswordEncryptionRounds :
				  TX_PWENCRYPT_ROUNDS_DEFAULT);

	clearPassLen = strlen(clearPass);
	if (!TXsslLoadLibrary(pmbuf)) goto err;

	/* Set method-dependent values: */
	switch (method)
	{
	case TXpwEncryptMethod_SHA256:
		sha = TXcryptoSymbols.EVP_sha256();
		digestSz = SHA256_DIGEST_LENGTH;
		idStr = sha256idStr;
		break;
	case TXpwEncryptMethod_SHA512:
		sha = TXcryptoSymbols.EVP_sha512();
		digestSz = SHA512_DIGEST_LENGTH;
		idStr = sha512idStr;
		break;
	default:
	unsupMethod:
		txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
			       "Unsupported method %d", (int)method);
		goto err;
	}
	idStrLen = strlen(idStr);

	/* Get rounds, if given: */
	if (saltLen >= sizeof(roundsStr) - 1 &&
	    strncmp(salt, roundsStr, sizeof(roundsStr) - 1) == 0)
	{
		unsigned	val;
		char		*ep;
		int		errNum;

		val = TXstrtou(salt + sizeof(roundsStr) - 1, salt + saltLen,
			       &ep, 10, &errNum);
		if (errNum || *ep != '$')
		{
			txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
			     "Unparseable/unterminated rounds value in salt");
			goto err;
		}
		if (val < TX_PWENCRYPT_ROUNDS_MIN)
			val = TX_PWENCRYPT_ROUNDS_MIN;
		else if (val > TX_PWENCRYPT_ROUNDS_MAX)
			val = TX_PWENCRYPT_ROUNDS_MAX;
		rounds = val;
		ep++;				/* skip `$' after rounds */
		saltLen = (salt + saltLen) - ep;
		salt = ep;			/* skip `rounds=N$' */
	}

	/* Start constructing output: */
	memcpy(d, idStr, idStrLen);
	d += idStrLen;

	/* Print rounds if different from the *standard* default,
	 * not if different from *our* (texis.ini) default:
	 */
	if (rounds != TX_PWENCRYPT_ROUNDS_DEFAULT)
	{
	    char tmp[sizeof(roundsStr) + TX_PWENCRYPT_ROUNDS_MAX_DIGITS + 1];

		htsnpf(tmp, sizeof(tmp), "%s%u$", roundsStr, rounds);
		memcpy(d, tmp, strlen(tmp));
		d += strlen(tmp);
	}

	/* Stop at `$' in salt if present; (earlier) encrypt/hash follows.
	 * And max salt length is MAX_SALT_LEN:
	 */
	saltLen = TXstrcspnBuf(salt, salt + saltLen, "$", 1);
	saltLen = TX_MIN(saltLen, MAX_SALT_LEN);
	memcpy(d, salt, saltLen);
	d += saltLen;

	/* Do hash algorithm: */
	if (!(mdCtx1 = TXcryptoSymbols.EVP_MD_CTX_new()) ||
	    !TXcryptoSymbols.EVP_DigestInit_ex(mdCtx1, sha, NULL) ||
	    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx1, clearPass,
					      clearPassLen) ||
	    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx1, salt, saltLen))
		goto cryptErr;

	if (!(mdCtx2 = TXcryptoSymbols.EVP_MD_CTX_new()) ||
	    !TXcryptoSymbols.EVP_DigestInit_ex(mdCtx2, sha, NULL) ||
	    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx2, clearPass,
					      clearPassLen) ||
	    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx2, salt, saltLen) ||
	    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx2, clearPass,
					      clearPassLen) ||
	    !TXcryptoSymbols.EVP_DigestFinal_ex(mdCtx2, shaBuf, NULL))
		goto cryptErr;

	for (i = clearPassLen; i > digestSz; i -= digestSz)
		if (!TXcryptoSymbols.EVP_DigestUpdate(mdCtx1, shaBuf,
						      digestSz))
			goto cryptErr;
	if (!TXcryptoSymbols.EVP_DigestUpdate(mdCtx1, shaBuf, i))
		goto cryptErr;

	for (n = clearPassLen; n > 0; n >>= 1)
		if (!TXcryptoSymbols.EVP_DigestUpdate(mdCtx1,
					((n & 1) ? shaBuf : (byte *)clearPass),
					((n & 1) ? digestSz : clearPassLen)))
			goto cryptErr;
	if (!TXcryptoSymbols.EVP_DigestFinal_ex(mdCtx1, shaBuf, NULL))
		goto cryptErr;

	/* P sequence: */
	if (!TXcryptoSymbols.EVP_DigestInit_ex(mdCtx2, sha, NULL))
		goto cryptErr;
	for (n = clearPassLen; n > 0; n--)
		if (!TXcryptoSymbols.EVP_DigestUpdate(mdCtx2, clearPass,
						      clearPassLen))
			goto cryptErr;
	if (!TXcryptoSymbols.EVP_DigestFinal_ex(mdCtx2, shaBuf2, NULL))
		goto cryptErr;
	if (!(passDupBuf = TXcalloc(pmbuf, __FUNCTION__, clearPassLen, 1)))
		goto err;
	for (pd = passDupBuf, n = clearPassLen;
	     n > digestSz;
	     n -= digestSz, pd += digestSz)
		memcpy(pd, shaBuf2, digestSz);
	memcpy(pd, shaBuf2, n);

	/* S sequence: */
	if (!TXcryptoSymbols.EVP_DigestInit_ex(mdCtx2, sha, NULL))
		goto cryptErr;
	for (n = shaBuf[0] + 16; n > 0; n--)
		if (!TXcryptoSymbols.EVP_DigestUpdate(mdCtx2, salt, saltLen))
			goto cryptErr;
	if (!TXcryptoSymbols.EVP_DigestFinal_ex(mdCtx2, shaBuf2, NULL))
		goto cryptErr;
	if (!(saltDupBuf = TXcalloc(pmbuf, __FUNCTION__, saltLen, 1)))
		goto err;
	for (pd = saltDupBuf, n = saltLen;
	     n > digestSz;
	     n -= digestSz, pd += digestSz)
		memcpy(pd, shaBuf2, digestSz);
	memcpy(pd, shaBuf2, n);

	for (i = 0; i < rounds; i++)
	{
		if (!TXcryptoSymbols.EVP_DigestInit_ex(mdCtx2, sha, NULL))
			goto cryptErr;
		if (!TXcryptoSymbols.EVP_DigestUpdate(mdCtx2,
				((i & 1) ? passDupBuf : shaBuf),
				((i & 1) ? clearPassLen : digestSz)))
			goto cryptErr;
		if ((i % 3) &&
		    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx2, saltDupBuf,
						      saltLen))
			goto cryptErr;
		if ((i % 7) &&
		    !TXcryptoSymbols.EVP_DigestUpdate(mdCtx2, passDupBuf,
						      clearPassLen))
			goto cryptErr;
		if (!TXcryptoSymbols.EVP_DigestUpdate(mdCtx2,
				  ((i & 1) ? shaBuf : passDupBuf),
				  ((i & 1) ? digestSz : clearPassLen)))
			goto cryptErr;
		if (!TXcryptoSymbols.EVP_DigestFinal_ex(mdCtx2, shaBuf, NULL))
		{
		cryptErr:
			txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
				       "SHA hash function failed");
			goto err;
		}
	}

	/* Map to output: */
#define BASE64(b2, b1, b0, nVal)					\
	do								\
	{								\
		unsigned	w = (((b2) << 16) | ((b1) << 8) | (b0));\
		int		_i;					\
		for (_i = (nVal); _i > 0; _i--)				\
		{							\
			*(d++) = i64c(w & 0x3f);			\
			w >>= 6;					\
		}							\
	}								\
	while (0)

	*(d++) = '$';
	switch (method)
	{
	case TXpwEncryptMethod_SHA256:
		BASE64(shaBuf[0], shaBuf[10], shaBuf[20], 4);
		BASE64(shaBuf[21], shaBuf[1], shaBuf[11], 4);
		BASE64(shaBuf[12], shaBuf[22], shaBuf[2], 4);
		BASE64(shaBuf[3], shaBuf[13], shaBuf[23], 4);
		BASE64(shaBuf[24], shaBuf[4], shaBuf[14], 4);
		BASE64(shaBuf[15], shaBuf[25], shaBuf[5], 4);
		BASE64(shaBuf[6], shaBuf[16], shaBuf[26], 4);
		BASE64(shaBuf[27], shaBuf[7], shaBuf[17], 4);
		BASE64(shaBuf[18], shaBuf[28], shaBuf[8], 4);
		BASE64(shaBuf[9], shaBuf[19], shaBuf[29], 4);
		BASE64(0, shaBuf[31], shaBuf[30], 3);
		break;
	case TXpwEncryptMethod_SHA512:
		BASE64(shaBuf[0], shaBuf[21], shaBuf[42], 4);
		BASE64(shaBuf[22], shaBuf[43], shaBuf[1], 4);
		BASE64(shaBuf[44], shaBuf[2], shaBuf[23], 4);
		BASE64(shaBuf[3], shaBuf[24], shaBuf[45], 4);
		BASE64(shaBuf[25], shaBuf[46], shaBuf[4], 4);
		BASE64(shaBuf[47], shaBuf[5], shaBuf[26], 4);
		BASE64(shaBuf[6], shaBuf[27], shaBuf[48], 4);
		BASE64(shaBuf[28], shaBuf[49], shaBuf[7], 4);
		BASE64(shaBuf[50], shaBuf[8], shaBuf[29], 4);
		BASE64(shaBuf[9], shaBuf[30], shaBuf[51], 4);
		BASE64(shaBuf[31], shaBuf[52], shaBuf[10], 4);
		BASE64(shaBuf[53], shaBuf[11], shaBuf[32], 4);
		BASE64(shaBuf[12], shaBuf[33], shaBuf[54], 4);
		BASE64(shaBuf[34], shaBuf[55], shaBuf[13], 4);
		BASE64(shaBuf[56], shaBuf[14], shaBuf[35], 4);
		BASE64(shaBuf[15], shaBuf[36], shaBuf[57], 4);
		BASE64(shaBuf[37], shaBuf[58], shaBuf[16], 4);
		BASE64(shaBuf[59], shaBuf[17], shaBuf[38], 4);
		BASE64(shaBuf[18], shaBuf[39], shaBuf[60], 4);
		BASE64(shaBuf[40], shaBuf[61], shaBuf[19], 4);
		BASE64(shaBuf[62], shaBuf[20], shaBuf[41], 4);
		BASE64(0, 0, shaBuf[63], 2);
		break;
	default:
		goto unsupMethod;
	}
	*d = '\0';
	if (d >= e)
	{
		txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
			       "Length check failed");
		goto err;
	}

	/* And dup for return: */
	if (!(ret = TXstrdup(pmbuf, __FUNCTION__, outBuf))) goto err;
	goto finally;

err:
	ret = TXfree(ret);
finally:
	if (mdCtx1)
	{
		TXcryptoSymbols.EVP_MD_CTX_free(mdCtx1);
		mdCtx1 = NULL;
	}
	if (mdCtx2)
	{
		TXcryptoSymbols.EVP_MD_CTX_free(mdCtx2);
		mdCtx2 = NULL;
	}
	passDupBuf = TXfree(passDupBuf);
	saltDupBuf = TXfree(saltDupBuf);
	return(ret);
#undef MAX_ID_STR_LEN
#undef MAX_SALT_LEN
#undef BASE64
}

#endif /* HAVE_OPENSSL */

char *
TXpwEncrypt(const char *clearPass, const char *salt)
/* Encrypts `clearPass' with optional `salt' (generated if NULL or
 * empty after any initial `$id$').  Supports glibc2 `$id$salt$...'
 * extensions (Bug 7398); if `salt' is NULL or empty, uses global
 * default method instead (set with TXpwEncryptMethodSetDefault()).
 * Note that we differ from crypt() in that an empty salt -- including
 * after `$id$' -- causes us to generate a salt; crypt() either fails
 * (if DES mode) or uses the empty salt.
 * Note also that we support id `$0$' to mean DES.
 * Returns alloc'd encrypted passwd-style salted password.
 */
{
	char	*ret = NULL;
#ifdef HAVE_OPENSSL
	char	cipherBuf[TX_MAX(TXSSL_DES_FCRYPT_RET_BUF_SZ, 1024)];
	char	newsalt[17];
	char	*cp;
	TXPMBUF	*pmbuf = TXPMBUFPN;
	const char	*id;
	size_t	saltLen, idLen, i;
	TXpwEncryptMethod	method = TXpwEncryptMethod_Unknown;

	/* Bug 7398 comment #0 paragraph 3: use OpenSSL.  Always
	 * use it instead of crypt(), which is thread-unsafe, needs
	 * our ancient Windows implementation (also thread-unsafe),
	 * and might cause security warnings for linking with crypt():
	 */

	if (!TXsslLoadLibrary(pmbuf)) goto err;

	if (!salt) salt = "";
	saltLen = strlen(salt);

	/* Bug 7398: support `$id$salt$...': */
	if (*salt == '$')			/* method given */
	{
		id = ++salt;			/* skip initial `$' */
		idLen = strcspn(id, "$");
		salt = id + idLen;
		if (*salt != '$')
		{
			txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
				       /* don't leak potential encrypted
					* password in msg
					*/
				 "Unterminated encryption method id in salt");
			goto err;
		}
		/* Skip id in salt.  Note that MD5/SHA methods will know
		 * to stop at final `$' (if any), after parsing params
		 * (if any):
		 */
		saltLen = strlen(++salt);
		/* Map id to method: */
		if (idLen == 1 && strncmp(id, "0", idLen) == 0)
			method = TXpwEncryptMethod_DES;
		else if (idLen == 1 && strncmp(id, "1", idLen) == 0)
			method = TXpwEncryptMethod_MD5;
		else if (idLen == 1 && strncmp(id, "5", idLen) == 0)
			method = TXpwEncryptMethod_SHA256;
		else if (idLen == 1 && strncmp(id, "6", idLen) == 0)
			method = TXpwEncryptMethod_SHA512;
		else
			goto unkMethod;
	}
	else if (!*salt)
		method = TXpwEncryptMethod_CURRENT(TXApp);
	else					/* non-empty, non-'$' salt */
		method = TXpwEncryptMethod_DES;

	/* Create salt data if needed: */
	if (saltLen == 0)
	{
		switch (method)
		{
		case TXpwEncryptMethod_DES:	saltLen = 2;	break;
		case TXpwEncryptMethod_MD5:	saltLen = 8;	break;
		case TXpwEncryptMethod_SHA256:
		case TXpwEncryptMethod_SHA512:	saltLen = 16;	break;
		default:			goto unkMethod;
		}
		if (saltLen >= sizeof(newsalt))
		{
			txpmbuf_putmsg(pmbuf, MERR + MAE, __FUNCTION__,
				       "Internal error");
			goto err;
		}
		if (TXcryptoSymbols.RAND_bytes((byte *)newsalt, saltLen) <= 0)
		{
			int	bits;

			/* RAND_bytes() failed.  Try to stumble on with
			 * our own entropy rather than fail:
			 */
			bits = time(NULL);
			bits ^= clock();
			bits ^= TXgetpid(0);
			for (i = 0; i < saltLen; i++)
				newsalt[i] =
				       ((bits >> 8*(i % sizeof(int))) & 0xff);
		}
		for (i = 0; i < saltLen; i++)
			newsalt[i] = i64c(newsalt[i] & 0x3f);
		newsalt[i] = '\0';
		salt = newsalt;
	}

	/* Do the encryption: */
	if (method != TXpwEncryptMethod_DES &&
	    !TX_PWENCRYPT_METHODS_ENABLED(TXApp))
	{
		txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
		       "Encryption methods other than DES not yet supported");
		goto err;
	}

	switch (method)
	{
	case TXpwEncryptMethod_DES:
		cp = TXcryptoSymbols.DES_fcrypt(clearPass, salt, cipherBuf);
		if (!cp)
		{
			txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
				       "DES_fcrypt() failed");
			goto err;
		}
		ret = TXstrdup(pmbuf, __FUNCTION__, cipherBuf);
		break;
	case TXpwEncryptMethod_MD5:
		ret = TXpwEncryptMD5(pmbuf, clearPass, salt, saltLen);
		break;
	case TXpwEncryptMethod_SHA256:
	case TXpwEncryptMethod_SHA512:
		ret = TXpwEncryptSHA(pmbuf, method, clearPass, salt, saltLen);
		break;
	default:
	unkMethod:
		txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
			       "Unknown encryption method id in salt");
		goto err;
	}
	if (!ret) goto err;
	goto finally;

err:
	ret = TXfree(ret);
finally:
#endif /* HAVE_OPENSSL */
	return(ret);
}

TXpwEncryptMethod
TXpwEncryptMethodStrToEnum(const char *s)
{
	if (strcmpi(s, "DES") == 0) return(TXpwEncryptMethod_DES);
	if (strcmpi(s, "MD5") == 0) return(TXpwEncryptMethod_MD5);
	if (strcmpi(s, "SHA-256") == 0) return(TXpwEncryptMethod_SHA256);
	if (strcmpi(s, "SHA-512") == 0) return(TXpwEncryptMethod_SHA512);
	return(TXpwEncryptMethod_Unknown);
}

const char *
TXpwEncryptMethodEnumToStr(TXpwEncryptMethod method)
{
	switch (method)
	{
	case TXpwEncryptMethod_DES:	return("DES");
	case TXpwEncryptMethod_MD5:	return("MD5");
	case TXpwEncryptMethod_SHA256:	return("SHA-256");
	case TXpwEncryptMethod_SHA512:	return("SHA-512");
	default:			return(NULL);
	}
}
