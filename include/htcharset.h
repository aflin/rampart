#ifndef HTCHARSET_H
#define HTCHARSET_H
/* HTML parse utils: */

char    *html2esc ARGS((unsigned ch, char *buf, size_t sz, TXPMBUF *pmbuf));
size_t   TXtransbuf(HTPFOBJ *obj, HTCHARSET charset, const char *charsetbuf, HTCHARSETFUNC *func, int flags, UTF utfExtra, const char *buf, size_t sz, char **ret);

extern CONST char       HtmlNoEsc[];
#define HTNOESC(ch)     HtmlNoEsc[(byte)(ch)]

size_t htencode ARGS((char *d, size_t dlen, CONST char *s, size_t slen,
                      CONST byte *htsfFlags));
char *htesc2html ARGS((CONST char *s, CONST char *e, int do8bit, size_t *szp,
                       int *valp, char *buf, size_t bufsz));

size_t htiso88591_to_utf8 ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
size_t htusascii_to_utf8 ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
size_t htutf8_to_iso88591 ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
size_t htutf8_to_usascii ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
size_t htutf8_to_utf8 ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
size_t htiso88591_to_iso88591 ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
size_t htutf16_to_utf8 ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
size_t htutf8_to_utf16 ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
size_t htiso88591_to_quotedprintable ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
size_t htquotedprintable_to_iso88591 ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
size_t htencodebase64 ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
size_t htdecodebase64 ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
size_t TXencodedWordToUtf8 ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));
size_t TXutf8ToEncodedWord ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htpfobj, TXPMBUF *pmbuf));


#endif /* HTCHARSET_H */
