#ifndef STRING16_H
#define STRING16_H

#ifdef NEVER_MAW
#include "rxp_charset.h"
#include <stddef.h>		/* for size_t */
#endif

/* String functions */

/* Don't want to include string.h while testing */

int strcmp ARGS((CONST char *, CONST char *));
WIN_IMP int strncmp ARGS((CONST char *, CONST char *, size_t));
int strcasecmp ARGS((CONST char *, CONST char *));
size_t strlen ARGS((CONST char *));
WIN_IMP char *strchr ARGS((CONST char *, int));
char *strcpy ARGS((char *, CONST char *));
WIN_IMP char *strncpy ARGS((char *, CONST char *, size_t));
char *strcat ARGS((char *, CONST char *));
WIN_IMP char *strstr ARGS((CONST char *, CONST char *));
int memcmp ARGS((CONST void *, CONST void *, size_t));
void *memcpy ARGS((void *, CONST void *, size_t));
void *memset ARGS((void *, int, size_t));
WIN_IMP size_t strspn ARGS((CONST char *, CONST char *));
WIN_IMP size_t strcspn ARGS((CONST char *, CONST char *));

STD_API char8 *strdup8 ARGS((CONST char8 *s));
#define strchr8(s, c) strchr((s), c)
#define strlen8(s) strlen((s))
#define strcmp8(s1, s2) strcmp((s1), (s2))
#define strncmp8(s1, s2, n) strncmp((s1), (s2), n)
#define strcpy8(s1, s2) strcpy((s1), (s2))
#define strncpy8(s1, s2, n) strncpy((s1), (s2), n)

#define strcat8(s1, s2) strcat((s1), (s2))
STD_API int strcasecmp8 ARGS((CONST char8 *, CONST char8 *));
STD_API int strncasecmp8 ARGS((CONST char8 *, CONST char8 *, size_t));
#define strstr8(s1, s2) strstr(s1, s2)

STD_API char16 *strdup16 ARGS((CONST char16 *s));
STD_API char16 *strchr16 ARGS((CONST char16 *, int));
STD_API size_t strlen16 ARGS((CONST char16 *));
STD_API int strcmp16 ARGS((CONST char16 *, CONST char16 *));
STD_API int strncmp16 ARGS((CONST char16 *, CONST char16 *, size_t));
STD_API char16 *strcpy16 ARGS((char16 *, CONST char16 *));
STD_API char16 *strncpy16 ARGS((char16 *, CONST char16 *, size_t));
STD_API char16 *strcat16 ARGS((char16 *, CONST char16 *));
STD_API int strcasecmp16 ARGS((CONST char16 *, CONST char16 *));
STD_API int strncasecmp16 ARGS((CONST char16 *, CONST char16 *, size_t));
STD_API char16 *strstr16 ARGS((CONST char16 *, CONST char16 *));

STD_API char16 *char8tochar16 ARGS((CONST char8 *s));
STD_API char8 *char16tochar8 ARGS((CONST char16 *s));

#if CHAR_SIZE == 8

#define Strdup strdup8
#define Strchr strchr8
#define Strlen strlen8
#define Strcmp strcmp8
#define Strncmp strncmp8
#define Strcpy strcpy8
#define Strncpy strncpy8
#define Strcat strcat8
#define Strcasecmp strcasecmp8
#define Strncasecmp strncasecmp8
#define Strstr strstr8

#define char8toChar(x) (x)
#define Chartochar8(x) (x)

#else

#define Strdup strdup16
#define Strchr strchr16
#define Strlen strlen16
#define Strcmp strcmp16
#define Strncmp strncmp16
#define Strcpy strcpy16
#define Strncpy strncpy16
#define Strcat strcat16
#define Strcasecmp strcasecmp16
#define Strncasecmp strncasecmp16
#define Strstr strstr16

#define char8toChar char8tochar16
#define Chartochar8 char16tochar8

#endif

#endif /* STRING16_H */
