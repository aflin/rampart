
char *
TXdecodestr(dest, destSz, src)
char            *dest;
size_t          destSz;
CONST char      *src;
/* Decodes obfuscated string `src' to `dest', that was encoded by encode.c at
 * compile time.  `src' may be same as `dest', else at least as large.
 * Yaps if `dest' is too small.
 * Thread-safe.  Signal-safe.
 * Returns `dest'.
 */
{
#ifdef NO_ENCODE_STRINGS
  if (dest != src)
    {
      TXstrncpy(dest, src, destSz);
      if (strlen(src) >= destSz)
#  ifdef FOR_SYSDEP_C
        putmsg(MERR + MAE, CHARPN,
#  else /* !FOR_SYSDEP_C */
        puts(
#  endif /* !FOR_SYSDEP_C */
               "Internal error: Buffer too small copying string");
    }
#else /* !NO_ENCODE_STRINGS */
  byte  n, i;
  char  *d, *destEnd = dest + destSz;

  for (n = 3, d = dest; *src != '\0'; d++, src++)
    {
      if (d >= destEnd)
        {
#  ifdef FOR_SYSDEP_C
          putmsg(MERR + MAE, CHARPN,
#  else /* !FOR_SYSDEP_C */
          puts(
#  endif /* !FOR_SYSDEP_C */
                 "Internal error: Buffer too small copying string");
          if (destSz >= 4) strcpy(destEnd - 4, "...");
          break;
        }
      i = (((*src) & 7) | 1);
      *d = (((byte)(*src) >> (8 - n)) | ((byte)(*src) << n));
      n = i;
    }
  if (d < destEnd)
    *d = '\0';
  else if (destEnd > dest)
    destEnd[-1] = '\0';
#endif /* !NO_ENCODE_STRINGS */
  return(dest);
}
