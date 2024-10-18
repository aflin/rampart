#include "texint.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef EPI_HAVE_UNISTD_H
#include <unistd.h>
#endif
#include "txSslSyms.h"


#define MAX_UUID_STR_SZ 38

static int
uuid_gen_v4(char *buffer, size_t buflen)
{
  union
	{
		struct
		{
			uint32_t time_low;
			uint16_t time_mid;
			uint16_t time_hi_and_version;
			uint8_t  clk_seq_hi_res;
			uint8_t  clk_seq_low;
			uint8_t  node[6];
		} vals;
		uint8_t __rnd[16];
	} uuid;
  int rc;

  if(buflen < MAX_UUID_STR_SZ) {
    return -1;
  }
#if defined (HAVE_OPENSSL)
  TXsslLoadLibrary(TXPMBUFPN);
  if(TXcryptoSymbols.RAND_bytes) {
    rc = TXcryptoSymbols.RAND_bytes(uuid.__rnd, sizeof(uuid));
  } else {
    return -1;
  }
#elif defined (HAVE_PROC_RANDOM_UUID)
  int fd = open("/proc/sys/kernel/random/uuid", O_RDONLY);
  if (fd == -1) {
    return fd;
  }
  rc = read(fd, buffer, 36);
  close(fd);
  buffer[36] = '\0';
  return 0;
#elif defined (HAVE_DEV_RANDOM)
  int fd = open("/dev/random", O_RDONLY);
  if (fd == -1) {
    return fd;
  }
  rc = read(fd, uuid.__rnd, sizeof(uuid));
  close(fd);
#endif

  /*
   * Refer Section 4.2 of RFC-4122
	 * https://tools.ietf.org/html/rfc4122#section-4.2
   */
	uuid.vals.clk_seq_hi_res = (uint8_t) ((uuid.vals.clk_seq_hi_res & 0x3F) | 0x80);
	uuid.vals.time_hi_and_version = (uint16_t) ((uuid.vals.time_hi_and_version & 0x0FFF) | 0x4000);

	snprintf(buffer, MAX_UUID_STR_SZ, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			uuid.vals.time_low, uuid.vals.time_mid, uuid.vals.time_hi_and_version,
			uuid.vals.clk_seq_hi_res, uuid.vals.clk_seq_low,
			uuid.vals.node[0], uuid.vals.node[1], uuid.vals.node[2],
			uuid.vals.node[3], uuid.vals.node[4], uuid.vals.node[5]);

	return rc;
}

static char *
alloc_uuid(void)
{
  char *ret = (char *)TXcalloc(TXPMBUFPN, __FUNCTION__, MAX_UUID_STR_SZ, sizeof(char));
  if(ret) {
    uuid_gen_v4(ret, MAX_UUID_STR_SZ);
  }
  return ret;
}

int
txfunc_generate_uuid(FLD *f)
{
  char *res;
  if(f == FLDPN) {
    return (FOP_EINVAL);
  }
  res = alloc_uuid();
  f->type = ((f->type & ~DDTYPEBITS) | FTN_CHAR | DDVARBIT);
  f->elsz = 1;
  if(res)
     setfldandsize(f, res, strlen(res) + 1, FLD_FORCE_NORMAL);
  else
     setfldandsize(f, res, 0, FLD_FORCE_NORMAL);
  return 0;
}
