#ifndef CHECKSUM_H
#define CHECKSUM_H

/* Prototypes for working with checksums */
#define SUM_LEN 40
#define SUM_MAGIC_KEY "thunderstone_file_sha1: "


#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <io.h>		/* setmode() */
#include <fcntl.h>	/* O_BINARY, used in setmode() */

#define TSTONE_ALG CALG_SHA1
#define SUM_PROVIDER HCRYPTPROV
#define SUM_HASH HCRYPTHASH

#else
#include <openssl/evp.h>

#define WINAPI 
#define TSTONE_ALG EVP_sha1()
/* OpenSSL doesn't use any kind of "provider", so make it a
 * char* that will be passed around as NULL so it can use the
 * same prototypes as the win32 versions. */
#define SUM_PROVIDER char*
#define SUM_HASH EVP_MD_CTX*
#endif /* _WIN32 */


SUM_HASH HashInit(SUM_PROVIDER prov);
int HashUpdate(SUM_HASH hHash, char *lpData, size_t cbData);
int HashFinal(SUM_HASH hHash, char* szOut, long cchOut);

int WINAPI makeChecksumBuffer(char *buffer, size_t bufferSz, char *sum);
int WINAPI makeChecksumBufferKey(char *buffer, size_t bufferSz, char *sumKey, char *sum);

int WINAPI extractChecksumBuffer(char *buffer, size_t bufferSz, char *sum);
int WINAPI extractChecksumBufferKey(char *buffer, size_t bufferSz,
                                    char *sumKey, char *sum);

int WINAPI doChecksum(char *filename, char *sumFile, char *sumComputed);
int WINAPI doChecksumKey(char *filename, char *sumKey, char *sumFile, char *sumComputed);
int WINAPI verifyChecksum(char *filename);
int WINAPI updateChecksum(char *filename);
int WINAPI updateChecksumKey(char *filename, char *sumKey);

#endif /* !CHECKSUM_H */
