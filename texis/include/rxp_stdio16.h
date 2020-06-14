#ifndef STDIO16_H
#define STDIO16_H

#ifdef NEVER_MAW
#include <stdarg.h>
#include <stdio.h>
#ifdef HAVE_LIBZ
#include "zlib.h"
#endif
#include "rxp_charset.h"
#endif

typedef struct _FILE16 FILE16;

extern STD_API FILE16 *Stdin, *Stdout, *Stderr;

STD_API FILE16 *MakeFILE16FromFILE ARGS((FILE *f, CONST char *type));
STD_API FILE16 *MakeFILE16FromString ARGS((void *buf, long size, CONST char *type));
#ifdef WIN32
#ifdef SOCKETS_IMPLEMENTED
STD_API FILE16 *MakeFILE16FromWinsock ARGS((int sock, CONST char *type));
#endif
#endif
#ifdef HAVE_LIBZ
STD_API FILE16 *MakeFILE16FromGzip ARGS((gzFile file, CONST char *type));
#endif

STD_API int Readu ARGS((FILE16 *file, unsigned char *buf, int max_count));
STD_API int Writeu ARGS((FILE16 *file, unsigned char *buf, int count));
STD_API int Fclose ARGS((FILE16 *file));
STD_API int Fflush ARGS((FILE16 *file));
STD_API int Fseek ARGS((FILE16 *file, long offset, int ptrname));

STD_API FILE *GetFILE ARGS((FILE16 *file));
STD_API void SetCloseUnderlying ARGS((FILE16 *file, int cu));
STD_API void SetFileEncoding ARGS((FILE16 *file, CharacterEncoding encoding));
STD_API CharacterEncoding GetFileEncoding ARGS((FILE16 *file));

STD_API int Printf ARGS((CONST char *format, ...));
STD_API int Fprintf ARGS((FILE16 *file, CONST char *format, ...));
#ifdef EPI_HAVE_STDARG
STD_API int Vfprintf ARGS((FILE16 *file, CONST char *format, va_list args));
STD_API int Vprintf ARGS((CONST char *format, va_list args));
STD_API int Vsprintf ARGS((void *buf, CharacterEncoding enc, CONST char *format, va_list args));
#else
STD_API int Vfprintf ARGS((FILE16 *file, CONST char *format, ...));
STD_API int Vprintf ARGS((CONST char *format, ...));
STD_API int Vsprintf ARGS((void *buf, CharacterEncoding enc, CONST char *format, ...));
#endif

STD_API int Sprintf ARGS((void *buf, CharacterEncoding enc, CONST char *format, ...));

STD_API void init_stdio16 ARGS((void));

#endif /* STDIO16_H */
