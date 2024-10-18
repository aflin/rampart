#ifndef TX_PUTMSG_FLAGS_H
#define TX_PUTMSG_FLAGS_H
/* putmsg flags: */
typedef enum
{
  VXPMF_DEFAULT = (1 << 0),             /* act as default (non-Vortex) pm */
  VXPMF_ERRHDR  = (1 << 1),             /* if n < MINFO, print HTML hdr */
  VXPMF_ShowPid  = (1 << 2),            /* show PID with messages */
  VXPMF_NATIVEIO= (1 << 3),             /* Windows: use native I/O */
  VXPMF_REOPEN  = (1 << 4),             /* re-open log file every putmsg() */
  VXPMF_CACHE   = (1 << 5),             /* cache msgs */
  VXPMF_ShowNonMainThreadId = (1 << 6), /* show thread ID if not `main' */
  VXPMF_ShowDateAlways = (1 << 7)       /* show date always in messages,
                                         *   not just sometimes in log only
                                         */
}
TXPUTMSGFLAGS;

#define VXPMFPN ((TXPUTMSGFLAGS *)NULL)

#define vx_getpmflags() (TXApp?(TXApp->putmsgFlags):0)   /* async-signal-safe */
int tx_setpmflags(TXPUTMSGFLAGS flags, int on);
extern int      PMnoecho;
#ifdef _WIN32
extern HANDLE   VxErrFh;
#else
extern int      VxErrFd;
#endif

#endif /* TX_PUTMSG_FLAGS_H */
