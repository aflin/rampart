#ifndef TXMIME_H
#define TXMIME_H

/* ------------------------------- MIME ------------------------------------ */
/* KNG 961002 */

typedef struct MIME_tag MIME;           /* mime.c */
#define MIMEPN  ((MIME *)NULL)

/* A tuple for identifying a MIME part, by CID (Content-ID), URL and filename:
 */
typedef struct TXmimeId_tag
{
  EPI_SSIZE_T   refCount;               /* reference count */
  char          *contentType;           /* (opt.) no params */
  char          *contentId;             /* (opt.) decoded, no brackets */
  char          *contentLocation;       /* (opt.) URL */
  char          *msgFilename;           /* (opt.) no dir, unsafe chars */
  char          *safeFilename;          /* no dir, safe chars, always set */
  byte          safeFilenameIsMadeUp;   /* boolean */
}
TXmimeId;
#define TXmimeIdPN      ((TXmimeId *)NULL)
#define TXmimeIdPPN     ((TXmimeId **)NULL)

TXmimeId *TXmimeIdOpen ARGS((HTPFOBJ *htpfobj, CONST char *contentTypeHdr,
                             CGISL *hdrs));
TXmimeId *TXmimeIdClose ARGS((TXmimeId *mimeId));
TXmimeId *TXmimeIdClone ARGS((TXmimeId *mimeId));

int     TXmsgGetHeadersParams ARGS((HTPFOBJ *htpfobj, CONST char **hdrVals,
                                    CGISL **mergedParams));
int     TXmsgParseHeaders ARGS((CONST char *buf, CONST char *bufEnd,
                                CGISL *hdrs, CONST char **bodyStart));
size_t TXmsgCopyQuotedString ARGS((TXPMBUF *pmbuf, char **destBuf,
      size_t *destBufAllocedSz, CONST char **srcBuf, CONST char *srcBufEnd,
      int endCh, int flags));
int    TXmsgParseNameAddress(TXPMBUF *pmbuf, const char *buf,
                             const char *bufEnd, char **name, char **address);

MIME   *TXmimeOpen ARGS((HTPFOBJ *htpfobj, CGISL *msgHdrs,
                         CONST char *msgContentType, CONST char *msg,
                         CONST char *msgEnd, CONST char *msgPathForMsgs,
                         int flags));
MIME   *TXmimeClose ARGS((MIME *mime));
int     TXmimeIsMultipartMsg ARGS((MIME *mime));
char   *TXmimeGetMsgContentTypeValue ARGS((MIME *mime));
char   *TXmimeGetNextPart ARGS((MIME *mime));
int     TXmimeGetPartIsLast ARGS((MIME *mime));
size_t  TXmimeGetPartBodySize ARGS((MIME *mime));
char   *TXmimeGetPartHeadersStart ARGS((MIME *mime));
CGISL  *TXmimeGetPartHeaders ARGS((MIME *mime, int release));
int     TXmimeGetPartIsStart ARGS((MIME *mime));
TXmimeId *TXmimeGetMimeId ARGS((MIME *mime, int release));
char   *TXmimeGetMessageHeadersStart(MIME *mime);
char   *TXmimeGetMessageBody(MIME *mime);
size_t  TXmimeGetMessageBodySize(MIME *mime);



#endif /* TXMIME_H */
