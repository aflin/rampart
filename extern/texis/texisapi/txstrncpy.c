#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <ctype.h>
#include <string.h>  #ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif 
#include "os.h"
/*
#include "dbquery.h"
*/
#include "txtypes.h"

#ifdef RCS_ID
static const char RcsId[] = "$Id$";
#endif

#define U       TX_CTYPE_UPPER_MASK
#define L       TX_CTYPE_LOWER_MASK
#define D       TX_CTYPE_DIGIT_MASK
#define X       TX_CTYPE_XDIGIT_MASK
#define n       (D|X)
#define u       (U|X)
#define l       (L|X)
#define S       TX_CTYPE_SPACE_MASK
#define P       TX_CTYPE_PUNCT_MASK

const unsigned char     TXctypeBits[256] =
  {
    /* Only transform ASCII: inadvertent use on UTF-8 should be harmless: */
    /*   0-15  */  0, 0, 0, 0, 0, 0, 0, 0, 0, S, S, S, S, S, 0, 0,
    /*  16-31  */  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /*  32-47  */  S, P, P, P, P, P, P, P, P, P, P, P, P, P, P, P,
    /*  48-63  */  n, n, n, n, n, n, n, n, n, n, P, P, P, P, P, P,
    /*  64-79  */  P, u, u, u, u, u, u, U, U, U, U, U, U, U, U, U,
    /*  80-95  */  U, U, U, U, U, U, U, U, U, U, U, P, P, P, P, P,
    /*  96-111 */  P, l, l, l, l, l, l, L, L, L, L, L, L, L, L, L,
    /* 112-127 */  L, L, L, L, L, L, L, L, L, L, L, P, P, P, P, 0,
    /* 128-143 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 144-159 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 160-175 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 176-191 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 192-207 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 208-223 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 224-239 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 240-255 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
#undef U
#undef L
#undef D
#undef X
#undef n
#undef u
#undef l
#undef S
#undef P

const unsigned char     TXctypeToupperMap[256] =
  {
    /* Only transform ASCII: inadvertent use on UTF-8 should be harmless: */
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
     32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
     48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
     64,'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',
    'P','Q','R','S','T','U','V','W','X','Y','Z', 91, 92, 93, 94, 95,
     96,'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',
    'P','Q','R','S','T','U','V','W','X','Y','Z',123,124,125,126,127,
    128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
    144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
    160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
    176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
    192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
    208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
    224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
    240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,
  };

const unsigned char     TXctypeTolowerMap[256] =
  {
    /* Only transform ASCII: inadvertent use on UTF-8 should be harmless: */
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
     32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
     48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
     64,'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
    'p','q','r','s','t','u','v','w','x','y','z', 91, 92, 93, 94, 95,
     96,'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
    'p','q','r','s','t','u','v','w','x','y','z',123,124,125,126,127,
    128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
    144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
    160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
    176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
    192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
    208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
    224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
    240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,
  };

#ifdef MEMDEBUG
#  undef TXallocProtectableFunc
#  undef TXfreeProtectableFunc
#  undef TXprotectMemFunc
#endif /* MEMDEBUG */

#ifdef TX_ENABLE_MEM_PROTECT
/* These are initialized in TXinitapp() so non-Texis-lib functions can
 * use them.  They are function pointers to avoid having to link
 * epi/api/etc. libs with Texis lib.  WTF:
 */
void    *(*TXallocProtectableFunc)(void *pmbuf, const char *fn,
                      size_t sz, TXMEMPROTECTFLAG flags TXALLOC_PROTO) = NULL;
int    (*TXfreeProtectableFunc)(void *pmbuf, void *p TXALLOC_PROTO) = NULL;
int     (*TXprotectMemFunc)(void *pmbuf, void *p, TXMEMPROTECTPERM perms
                            TXALLOC_PROTO) = NULL;
#endif /* TX_ENABLE_MEM_PROTECT */

int
TXstrncpy(char *dest, const char *src, size_t n)
/* Like strncpy(), but doesn't nul-terminate the whole remaining buffer,
 * just 1 char, and copies at most n-1 chars, guaranteeing the result will
 * be nul-terminated if n > 0.
 * Returns nonzero if `src' completely copied and terminated, 0 if not.
 * Thread-safe.
 * Signal-safe.
 */
{
	char	*e;

	if (!dest) n = 0;
	if (n <= (size_t)0)
		return(0);
	for(e = dest + n - 1; dest < e && *src != '\0'; )
		*(dest++) = *(src++);
	*dest = '\0';
	return(*src == '\0');
}

int
TXstrnispacecmp(a, an, b, bn, whitespace)
const char	*a;		/* (in) left-side string to compare */
size_t		an;		/* (in) length of `a' (-1 == strlen(a)) */
const char	*b;		/* (in) right-side string to compare */
size_t		bn;		/* (in) length of `b' (-1 == strlen(b)) */
const char	*whitespace;	/* (in, opt.) chars considered whitespace */
/* Compares up to `an' chars of `a' with up to `bn' chars of `b',
 * ignoring case and skipping whitespace.  Return value ala strcmp().
 * If `an' or `bn' is -1, strlen(a) or strlen(b) is assumed.
 */
{
	static const char	defWhitespace[] = " \t\r\n\v\f";
	const char		*ae, *be;
	int			ac, bc;

	if (!whitespace) whitespace = defWhitespace;
	ae = (an == (size_t)(-1) ? a + strlen(a) : a + an);
	be = (bn == (size_t)(-1) ? b + strlen(b) : b + bn);
	while (a < ae && b < be)
	{
		if (strchr(whitespace, *a)) { a++; continue; }
		if (strchr(whitespace, *b)) { b++; continue; }
		ac = TX_TOUPPER(*a);
		bc = TX_TOUPPER(*b);
		if (ac != bc) return(ac - bc);		/* differ */
		a++;
		b++;
	}
	while (a < ae && strchr(whitespace, *a)) a++;
	while (b < be && strchr(whitespace, *b)) b++;
	ac = (a < ae ? TX_TOUPPER(*a) : 0);
	bc = (b < be ? TX_TOUPPER(*b) : 0);
	return(ac - bc);
}

size_t
TXstrspnBuf(s, e, accept, acceptLen)
const char      *s;             /* (in) start of buffer */
const char      *e;             /* (in, opt.) end of buffer */
const char      *accept;        /* (in) acceptable characters */
size_t          acceptLen;      /* (in, opt.) length of `accept' */
/* strspn() for a string ending at `e'.
 * Thread-safe.
 */
{
  const char    *orgS = s, *acceptEnd;
  byte          ok[1 << 8];

  if (!e) e = s + strlen(s);
  /* Prep lookup table: */
  memset(ok, 0, sizeof(ok));
  if (acceptLen != (size_t)(-1))
    {
      for (acceptEnd = accept + acceptLen; accept < acceptEnd; accept++)
        ok[*(byte *)accept] = 1;
    }
  else
    {
      for ( ; *accept; accept++) ok[*(byte *)accept] = 1;
    }
  /* Do the scan: */
  for ( ; s < e && ok[*(byte *)s]; s++);
  return((size_t)(s - orgS));
}

size_t
TXstrcspnBuf(s, e, reject, rejectLen)
const char      *s;             /* (in) start of buffer */
const char      *e;             /* (in) end of buffer */
const char      *reject;        /* (in) reject characters */
size_t          rejectLen;     /* (in, opt.) length of `reject' */
/* strcspn() for a string ending at `e'.
 * Thread-safe.
 */
{
  const char    *orgS = s, *rejectEnd;
  byte          ok[1 << 8];

  if (!e) e = s + strlen(s);
  /* Prep lookup table: */
  memset(ok, 1, sizeof(ok));
  if (rejectLen != (size_t)(-1))
    {
      for (rejectEnd = reject + rejectLen; reject < rejectEnd; reject++)
        ok[*(byte *)reject] = 0;
    }
  else
    {
      for ( ; *reject; reject++) ok[*(byte *)reject] = 0;
    }
  /* Do the scan: */
  for ( ; s < e && ok[*(byte *)s]; s++);
  return((size_t)(s - orgS));
}

void
TXstrToLowerCase(char *s, size_t sz)
/* Locale-independent, ASCII-only.
 */
{
  char  *e;

  if (sz == (size_t)(-1))                       /* nul-terminated */
    {
      for ( ; *s; s++)
        *s = TX_TOLOWER(*s);
    }
  else
    {
      for (e = s + sz; s < e; s++)
        *s = TX_TOLOWER(*s);
    }
}

void
TXstrToUpperCase(char *s, size_t sz)
/* Locale-independent, ASCII-only.
 */
{
  char  *e;

  if (sz == (size_t)(-1))                       /* nul-terminated */
    {
      for ( ; *s; s++)
        *s = TX_TOUPPER(*s);
    }
  else
    {
      for (e = s + sz; s < e; s++)
        *s = TX_TOUPPER(*s);
    }
}

size_t
TXfindStrInList(char **list, size_t listLen, const char *s, size_t sLen,
                int flags)
/* Returns index of `s' in `list', or -1 if not found.
 * `sLen' is length of `s'; strlen(s) assumed if -1.  `listLen' is
 * number of elements in `list' (NULL elements ignored), or `list'
 * is NULL-terminated if `listLen' is -1.
 * Flags:
 *   0x01  Ignore case
 * Thread-safe.  Signal-safe.
 */
{
  char          **listPtr;
  size_t        listIdx;

  if (sLen == (size_t)(-1)) sLen = strlen(s);
  for (listPtr = list, listIdx = 0;
       (listLen == (size_t)(-1) ? (*listPtr != NULL) : listIdx < listLen);
       listPtr++, listIdx++)
    {
      if (!*listPtr) continue;
      if (((flags & 0x1) ? strnicmp(*listPtr, s, sLen) == 0 :
           strncmp(*listPtr, s, sLen) == 0) &&
          (*listPtr)[sLen] == '\0')
        return(listIdx);
    }
  return((size_t)(-1));
}
