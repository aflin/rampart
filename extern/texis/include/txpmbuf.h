#ifndef TXPMBUF_H
#define TXPMBUF_H

/* KNG 20051101 putmsg-buffering object; see source/texis/pmbuf.c: */
/* NOTE: see also copy of this in os.h to avoid dragging in mmsg.h for API: */
#ifndef TXPMBUFPN
typedef struct TXPMBUF_tag      TXPMBUF;
#  define TXPMBUFPN             ((TXPMBUF *)NULL)

/* Pointer that causes txpmbuf_open() to make a new object, with default
 * flags.  Only valid to txpmbuf_open():
 */
#  define TXPMBUF_NEW           ((TXPMBUF *)1)

/* Pointer equivalent to a TXPMBUF with !(TXPMBUFF_PASS|TXPMBUFF_SAVE).
 * Valid for any function except txpmbuf_setflags(), may be returned from
 * txpmbuf_open() if passed:
 */
#  define TXPMBUF_SUPPRESS      ((TXPMBUF *)2)
#endif /* !TXPMBUFPN */

#endif /* TXPMBUF_H */
