#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "cgi.h"
#include "texint.h"
#include "http.h"

struct MIME_tag
{
  TXPMBUF       *pmbuf;                 /* (opt.) buffer for putmsgs */
  HTPFOBJ       *htpfobj;                 /* (opt.) for charset conv. and msgs*/
  CONST char    *buf, *bufEnd;
  CONST char    *msgHdrsStart;          /* (opt.) overall msg hdrs offset */
  CONST char    *msgBody;               /* overall msg body start */
  CGISL         *hdrs;
  CONST char    *hdrsStart;             /* (opt.) offset of current headers */
  CONST char    *body, *bodyEnd;        /* start/end of current part's body */
  FFS           *rex;                   /* REX for boundary expression */
  char          *boundaryExpr;          /* boundary REX expression */
  size_t        boundlen;               /* length of boundary string */
  char          *msgContentTypeValue;   /* overall-msg Content-Type, !params*/
  size_t        partCount;
  TXmimeId      *mimeId;                /* TXmimeId for current part */
  char          *startContentId;        /* (opt.) start-part content-id */
  char          *startContentType;      /* (opt.) start-part MIME type */
  /* Booleans: */
  byte          sawStart;               /* an earlier part was the start */
  byte          curPartIsStart;         /* current part is start of m/r */
  byte          isMultipartRelated;
};

#define CR      '\r'
#define LF      '\n'

static CONST char       DirSep[] = "/\\:" PATH_SEP_S;

/* ------------------------------------------------------------------------ */

int
TXmsgGetHeadersParams(htpfobj, hdrVals, mergedParams)
HTPFOBJ           *htpfobj;         /* (opt.) for messages and charset conv. */
CONST char      **hdrVals;      /* (in) raw header value(s) */
CGISL           **mergedParams; /* (out) parameters */
/* Sets `*mergedParams' to an alloc'd CGISL containing merged params from
 * all `hdrVals', or NULL if no params found.  Merges and decodes
 * RFC 2231/5987 continuation/charset info, to UTF-8.
 * Returns 0 on error (out of mem).
 */
{
  size_t        i, j, k;
  CGISL         *accumParams = CGISLPN, *params = CGISLPN;
  char          **paramValues;
  CONST char    *value, *valueEnd, *paramName;
  int           ret;

  for (i = 0; hdrVals[i] != CHARPN && *hdrVals[i] != '\0'; i++)
    {                                           /* for each header found */
      value = cgiparsehdr(htpfobj, hdrVals[i], &valueEnd, &params);
      if (!value) goto err;
      if (accumParams == CGISLPN)               /* first params obtained */
        {
          accumParams = params;
          params = CGISLPN;
        }
      else if (params != CGISLPN)               /* 2nd+ params obtained */
        {
          /* Merge `params' into `accumParams' to prevent dup names: */
          for (j = 0;                           /* for each param */
               (paramName = cgislvar(params, j, &paramValues)) != CHARPN;
               j++)
            {
              for (k = 0; paramValues[k] && *paramValues[k]; k++)
                if (!cgisladdvar(accumParams, paramName, paramValues[k]))
                  goto err;
            }
          params = closecgisl(params);
        }
    }
  ret = 1;
  goto done;

err:
  ret = 0;
  accumParams = closecgisl(accumParams);
done:
  *mergedParams = accumParams;
  accumParams = CGISLPN;
  params = closecgisl(params);
  return(ret);
}

int
TXmsgParseHeaders(buf, bufEnd, hdrs, bodyStart)
CONST char      *buf;           /* (in) buffer to parse */
CONST char      *bufEnd;        /* (in, opt.) end of `buf' */
CGISL           *hdrs;          /* (out) headers */
CONST char      **bodyStart;    /* (out) start of body (after headers) */
/* Parses Internet message format `buf' (ending at `bufEnd') for headers,
 * adding them to `hdrs' and returning body start in `*bodyStart'.
 * `hdrs' should have cgislsetcmp(hdrs, strnicmp) set.
 * Returns 0 on error.
 */
{
  CONST char    *hdr, *hdrEnd, *name, *nameEnd, *value, *valueEnd, *nextLine;

  if (bufEnd == CHARPN) bufEnd = buf + strlen(buf);
  nextLine = buf;
  for (hdr = buf; hdr < bufEnd; hdr = nextLine) /* for each header */
    {
      /* Seek `hdrEnd' to end of header, i.e. end of line, but allow
       * folded newlines for a long header:
       */
      for (hdrEnd = hdr; hdrEnd < bufEnd; hdrEnd = nextLine)
        {
          /* Seek `hdrEnd' to EOL: */
          for (; hdrEnd < bufEnd && *hdrEnd != CR && *hdrEnd != LF; hdrEnd++);
          /* Set `nextLine' to start of next line: */
          nextLine = hdrEnd;
          htskipeol((char **)&nextLine, (char *)bufEnd);
          /* If this is a blank line, this is end of all headers: */
          if (hdrEnd == hdr) break;
          /* If next line does not start with WSP, this is the end of
           * the header:
           */
          if (nextLine >= bufEnd || (*nextLine != ' ' && *nextLine != '\t'))
            break;
        }

      if (hdrEnd == hdr) break;                 /* blank line: headers end */
      /* Get end of header name: */
      name = hdr;
      nameEnd = name + TXstrcspnBuf(name, hdrEnd,
                                    ": \t\r\n", 6); /* 6: reject nul too */
      if (nameEnd >= hdrEnd || *nameEnd != ':') /* not a valid header name */
        {
          nextLine = hdr;                       /* bad header; part of body */
          break;
        }

      /* Get value start, after `:' and whitespace: */
      value = nameEnd + 1;                      /* skip the colon */
      while (value < hdrEnd && (*value == ' ' || *value == '\t' ||
                                *value == CR || *value == LF))
        value++;
      valueEnd = hdrEnd;
      if (TXcgislAddVarLenSz(hdrs, (char *)name, nameEnd - name,
                             (char *)value, valueEnd - value) == 0)
        goto err;
    }

  *bodyStart = nextLine;
  return(1);
err:
  *bodyStart = buf;
  return(0);
}

size_t
TXmsgCopyQuotedString(pmbuf, destBuf, destBufAllocedSz, srcBuf, srcBufEnd,
                      endCh, flags)
TXPMBUF         *pmbuf;                 /* (in, opt.) putmsg buffer */
char            **destBuf;              /* (in/out) buffer to write to */
size_t          *destBufAllocedSz;      /* (in/out) `*destBuf' alloced size */
CONST char      **srcBuf;               /* (in/out) buffer to parse */
CONST char      *srcBufEnd;             /* (in, opt.) end of `srcBuf' */
int             endCh;                  /* (in) ending-quote char */
int             flags;                  /* (in) flags */
/* Parses quoted string `*srcBuf' (assumed to point just after open quote)
 * and copies to `*destBuf', nul-terminating it.
 * Returns length of `*destBuf' written to (not including nul),
 * or -1 on error.  Undoes backslash escapes.  Advances `*srcBuf' past
 * parsed amount (including ending-quote char).
 * `flags':
 *   0x1:   Parse `\' escapes C-style, instead of just escaping the next char
 */
{
  const char    *s;
  char          *dBuf;
  int           charVal;
  size_t        dAllocedSz, dUsedSz;
#define ADD_BYTE(pmbuf, buf, usedSz, allocedSz, ch, err)                \
  {                                                                     \
    if (!TX_INC_ARRAY(pmbuf, &(buf), usedSz, &(allocedSz))) err;        \
    (buf)[(usedSz)++] = (ch);                                           \
  }

  s = *srcBuf;
  if (srcBufEnd == CHARPN) srcBufEnd = s + strlen(s);
  dBuf = *destBuf;
  dAllocedSz = *destBufAllocedSz;
  dUsedSz = 0;
  for ( ; s < srcBufEnd; s++)
    {
      if (*s == '\\')                           /* escaped char */
        {
          if (flags & 0x1)                      /* allow C escapes */
            {
              s++;                              /* skip `\' */
              TXparseCEscape(pmbuf, &s, srcBufEnd, &charVal);
              ADD_BYTE(pmbuf, dBuf, dUsedSz, dAllocedSz, charVal, goto err);
              s--;                              /* negate for-loop s++ */
            }
          else
            {
              if (s + 1 < srcBufEnd)
                s++;                            /* skip `\' */
              goto asIs;
            }
        }
      else if (*s == '\r' || *s == '\n')        /* multi-line fold: skip */
        ;
      else if (*s == endCh)
        {
          s++;                                  /* skip the end quote */
          break;
        }
      else
        {
        asIs:
          ADD_BYTE(pmbuf, dBuf, dUsedSz, dAllocedSz, *s, goto err);
        }
    }
  ADD_BYTE(pmbuf, dBuf, dUsedSz, dAllocedSz, '\0', goto err);
  dUsedSz--;                                    /* nul not counted */

  /* Check if the string was doubly-quoted, e.g. "'blah'" or '"blah"',
   * and remove the extra layer:
   */
checkDoublyQuoted:
  if (dUsedSz >= 2)                             /* at least 2 chars */
    switch (*dBuf)
      {
      case '"':
      case '\'':
        if (dBuf[dUsedSz - 1] != *dBuf) break;  /* non-matching quotes */
        memmove(dBuf, dBuf + 1, dUsedSz - 2);
        dUsedSz -= 2;                           /* 2 quotes less */
        dBuf[dUsedSz] = '\0';
        goto checkDoublyQuoted;
      }

  goto done;

err:
  dBuf = TXfree(dBuf);
  dAllocedSz = 0;
  dUsedSz = -1;
done:
  *destBuf = dBuf;
  *destBufAllocedSz = dAllocedSz;
  *srcBuf = s;
  return(dUsedSz);
#undef ADD_BYTE
}

int
TXmsgParseNameAddress(pmbuf, buf, bufEnd, name, address)
TXPMBUF         *pmbuf;         /* (in, opt.) putmsg buffer */
CONST char      *buf;           /* (in) buffer to parse */
CONST char      *bufEnd;        /* (in, opt.) end of `buf' */
char            **name;         /* (out) alloc'd name */
char            **address;      /* (out) alloc'd address */
/* Parses mailbox value `buf' for display name and email address.  An
 * address with no name gets an empty name and vice versa.  Unfolds
 * and decodes encoded words.  Backslash escapes are unescaped.
 * Returns 0 on error.
 */
{
  static const char     fn[] = "TXmsgParseNameAddress";
  /* We include close-only quote chars `)' and `>' in `whitespaceSep'
   * because if they are encountered unbalanced (without an open char)
   * they are probably ignorable.  Note that we thus do *not* include
   * these close-only chars in `unquotedEndChars':
   */
  static CONST char     whitespaceSep[] = ")> \t\r\n\v\f";
  /* `unquotedEndChars' are chars that end an unquoted string.
   * Do not include single-quote: may be in use as an apostrophe.
   * Do not include comma/semicolon: we are given only a single mailbox:
   */
  static CONST char     unquotedEndChars[] = "\"(<";
  size_t                n, ret;
  typedef enum ITEM_tag
    {
      ITEM_NAME,
      ITEM_ADDR,
      ITEM_NUM                                  /* must be last */
    }
  ITEM;
  char                  *values[ITEM_NUM] = { NULL, NULL };
  CONST char            *s, *e, *quoteParseEnd;
  char                  *tmpBuf = CHARPN, *val, *decodedVal;
  size_t                tmpBufAllocedSz = 0;
  int                   itemRank[ITEM_NUM], endCh;
  int                   thisRank;
  ITEM                  itemIdx;
  HTBUF                 *decodeBuf = NULL;
  /* Adds an empty string to `list[idx]' if we have not got that item: */
#define ADD_EMPTY_IF_NEEDED(idx)                                \
  if (!values[(idx)] && values[1 - (idx)])                      \
    {                                                           \
      if (!(values[(idx)] = TXstrdup(pmbuf, fn, ""))) goto err; \
    }

  /* Forms to parse:
     "Elvis Presley" <presley123@somehost.com.ru>
     Bob Loblaw <bob@loblaw.com>
     help@thunderstone.com
     <postmaster@site.com>
     "=?utf-8?Q?Word=20Phrase?=" <no-reply@mail.site.com>
     "Bob"
     =?UTF-8?B??= <support@master.com>
     "Toys\"R\"Us" <toysrus@em.toysrus.com>
     Joe@JoeBlow.com (Joe Blow)
     "My Name (Some Comment)" <nobody@nowhere.com>
     'single-quoted name' <user@foo.com>
     "'doubly-quotedNameOrEmail'" <user@foo.com>
     '"doubly-quotedNameOrEmail"' <user@foo.com>
     Joe <user@domain.com>
     <user@domain.com> Joe
     <user@domain.com> Joe Blow
   */
  if (bufEnd == CHARPN) bufEnd = buf + strlen(buf);

  itemRank[ITEM_NAME] = itemRank[ITEM_ADDR] = 0;
  for (s = buf; s < bufEnd; )
    {
      s += TXstrspnBuf(s, bufEnd, whitespaceSep, -1);
      if (s >= bufEnd) break;
      switch (*s)
        {
        case '"':                               /* probably a name */
          endCh = '"';
          itemIdx = ITEM_NAME;
          s++;                                  /* skip opening quote */
          quoteParseEnd = bufEnd;
          thisRank = 2;
          goto getItem;
        case '\'':                              /* probably a name */
          endCh = '\'';
          itemIdx = ITEM_NAME;
          s++;                                  /* skip opening quote */
          quoteParseEnd = bufEnd;
          thisRank = 2;
          goto getItem;
        case '(':                               /* comment: treat as name */
          endCh = ')';
          itemIdx = ITEM_NAME;
          s++;                                  /* skip opening `(' */
          quoteParseEnd = bufEnd;
          thisRank = 2;
          goto getItem;
        case '<':                               /* probably an address */
          endCh = '>';
          itemIdx = ITEM_ADDR;
          s++;                                  /* skip opening `<' */
          quoteParseEnd = bufEnd;
          thisRank = 2;
          goto getItem;
        default:                                /* unquoted item */
          /* End of unquoted item is next occurrence of `unquotedEndChars': */
          e = s + TXstrcspnBuf(s, bufEnd, unquotedEndChars, -1);
          /* Back off trailing whitespace: */
          while (e > s && strchr(whitespaceSep, e[-1]) != CHARPN) e--;
          if (e <= s) break;                    /* nothing left */
          /* Try to guess whether `s' is a name or address: */
          if (s + TXstrcspnBuf(s, e, whitespaceSep, -1) == e && /* no wspace*/
              (s + 2 > e || *s != '=' || s[1] != '?')) /* not enc-word */
            itemIdx = ITEM_ADDR;
          else
            itemIdx = ITEM_NAME;
          endCh = '\0';                         /* i.e. no end quote char */
          quoteParseEnd = e;                    /* parse only to here */
          thisRank = 0;                         /* unsure what this is */
        getItem:
          if (values[itemIdx])                  /* already have this item */
            {
              if (thisRank > itemRank[itemIdx]) /* but current is better */
                {                               /*   then swap */
                  values[1 - itemIdx] = TXfree(values[1 - itemIdx]);
                  values[1 - itemIdx] = values[itemIdx];
                  values[itemIdx] = CHARPN;
                  itemRank[1 - itemIdx] = itemRank[itemIdx];
                  itemRank[itemIdx] = 0;
                }
              else if (itemRank[itemIdx] > thisRank)
                {                               /* other guy is better */
                  itemIdx = 1 - itemIdx;        /* then switch ours */
                }
              else
                {
                  ADD_EMPTY_IF_NEEDED(1 - itemIdx); /* finish the pair */
                  itemRank[ITEM_NAME] = itemRank[ITEM_ADDR] = 0;
                }
            }
          n = TXmsgCopyQuotedString(pmbuf, &tmpBuf, &tmpBufAllocedSz, &s,
                                    quoteParseEnd, endCh, 0x0);
          if (n == (size_t)(-1)) goto err;
          /* Decode any encoded words: */
          if (!decodeBuf) decodeBuf = openhtbuf();
          if (!decodeBuf) goto err;
          htbuf_clear(decodeBuf);
          if (!htbuf_pf(decodeBuf, "%!|W", tmpBuf)) goto err;
          htbuf_getdata(decodeBuf, &decodedVal, 0);
          /* Set `values[itemIdx]': */
          if (!(val = TXstrdup(pmbuf, fn, decodedVal))) goto err;
          itemRank[itemIdx] = thisRank;
          values[itemIdx] = val;
          val = NULL;
          break;
        }
    }
  /* Finish current pair: */
  ADD_EMPTY_IF_NEEDED(ITEM_NAME);
  ADD_EMPTY_IF_NEEDED(ITEM_ADDR);
  /* Set up return values: */
  *name = values[ITEM_NAME];
  values[ITEM_NAME] = NULL;                     /* `*name' owns it */
  *address = values[ITEM_ADDR];
  values[ITEM_ADDR] = NULL;                     /* `*address' owns it */
  ret = 1;
  goto done;

err:
  ret = 0;                                      /* error */
  *name = *address = NULL;
done:
  tmpBuf = TXfree(tmpBuf);
  for (itemIdx = (ITEM)0; itemIdx < ITEM_NUM; itemIdx++)
    values[itemIdx] = TXfree(values[itemIdx]);
  decodeBuf = closehtbuf(decodeBuf);
  return(ret);
#undef ADD_EMPTY_IF_NEEDED
}

/* ------------------------------------------------------------------------ */

TXmimeId *
TXmimeIdClose(mimeId)
TXmimeId        *mimeId;
{
  if (mimeId == TXmimeIdPN || --mimeId->refCount > 0) goto done;

  mimeId->contentType = TXfree(mimeId->contentType);
  mimeId->contentId = TXfree(mimeId->contentId);
  mimeId->contentLocation = TXfree(mimeId->contentLocation);
  mimeId->msgFilename = TXfree(mimeId->msgFilename);
  mimeId->safeFilename = TXfree(mimeId->safeFilename);
  mimeId = TXfree(mimeId);

done:
  return(TXmimeIdPN);
}

static char *copyFilenameSafe ARGS((TXPMBUF *pmbuf, CONST char *val));
static char *
copyFilenameSafe(pmbuf, val)
TXPMBUF         *pmbuf;
CONST char      *val;
/* Dups filename part of `val', zapping bad chars and dir.
 * Returns NULL on error or bad filename (e.g. device).
 */
{
  static CONST char     fn[] = "copyFilename";
  char                  *s, *filename = CHARPN;

  val = TXstrrcspn(val, DirSep);                /* sans dir/drive */
  if (*val == '\0') return(CHARPN);
  if ((filename = TXstrdup(pmbuf, fn, val)) == CHARPN)
    return(CHARPN);
  /* Zap potentially bad chars: */
  for (s = filename; *s != '\0'; s++)
    if (*s < ' ' || *s == ':') *s = '_';
  /* Check for device, on any platform so our names are more consistent: */
  if (TXfilenameIsDevice(filename, 1))
    filename = TXfree(filename);
  return(filename);
}

TXmimeId *
TXmimeIdOpen(htpfobj, contentTypeHdr, hdrs)
HTPFOBJ           *htpfobj;                 /* (opt.) for msgs and charset conv.*/
CONST char      *contentTypeHdr;        /* (in, opt.) */
CGISL           *hdrs;                  /* (in, opt.) part headers */
/* `contentTypeHdr' (if non-NULL) takes precedence over `hdrs' Content-Type.
 */
{
  static CONST char     fn[] = "TXmimeIdOpen";
  TXmimeId              *mimeId;
  int                   contentLocationIsCidUrl = 0;
  char                  **values, **values2;
  CONST char            *val, *pfx, *valEnd, *conTypeExt = CHARPN;
  CONST char            *conType = CHARPN, *conTypeEnd = CHARPN, *s, *e;
  size_t                valSz, n;
  URL                   *url = URLPN;
  CGISL                 *conTypeParams = CGISLPN, *conDispoParams = CGISLPN;
  TXPMBUF               *pmbuf;
  char                  tmp[256];

  pmbuf = htpfgetpmbuf(htpfobj);
  if ((mimeId = TXcalloc(pmbuf, fn, 1, sizeof(TXmimeId))) == TXmimeIdPN)
    goto err;
  mimeId->refCount = 1;
  /* rest cleared by calloc() */

  /* Get Content-Type: - - - - - - - - - - - - - - - - - - - - - - - - - - */
  val = CHARPN;
  if (contentTypeHdr != CHARPN)
    val = contentTypeHdr;
  else if (hdrs != CGISLPN &&
           (values = getcgisl(hdrs, "Content-Type")) != CHARPPN &&
           *values != CHARPN &&
           **values != '\0')
    val = *values;
  if (val != CHARPN)
    conType = cgiparsehdr(htpfobj, val, &conTypeEnd, &conTypeParams);
  if (conType != CHARPN &&
      (mimeId->contentType = TXstrndup(pmbuf, fn, conType,
                                       conTypeEnd - conType)) == CHARPN)
    goto err;
  if (conType != CHARPN)
    conTypeExt = TXfetchMimeTypeToExt(conType, conTypeEnd);

  /* Get content-id: same-name header is first priority: - - - - - - - - - */
  if (hdrs != CGISLPN &&
      (values = getcgisl(hdrs, "Content-ID")) != CHARPPN &&
      *values != CHARPN &&
      **values != '\0')
    {
      val = *values;
      valSz = strlen(val);
      /* Remove `<'/`>' brackets, if any: */
      if (*val == '<')
        {
          val++;
          valSz--;
        }
      if (valSz > 0 && val[valSz - 1] == '>') valSz--;
      if ((mimeId->contentId = TXstrndup(pmbuf, fn, val, valSz)) == CHARPN)
        goto err;
    }

  /* Get content-location, and content-id if not set: - - - - - - - - - - - */
  if (hdrs != CGISLPN &&
      (values = getcgisl(hdrs, "Content-Location")) != CHARPPN &&
      *values != CHARPN &&
      **values != '\0')
    {
      /* Caller must make this absolute if possible, by recursively
       * absolutizing relative to containing Content-Location URLs.
       * See TXrmmMakeContentLocationAbsolute() in source/vortex/mail.c.
       */
      val = *values;
      mimeId->contentLocation = TXstrdup(pmbuf, fn, val);
      if (mimeId->contentLocation == CHARPN) goto err;
      /* A Content-Location `cid:' URL is secondary source for content-id: */
      if (mimeId->contentId == CHARPN &&        /* not set yet */
          strnicmp(val, "cid:", 4) == 0)
        {
          contentLocationIsCidUrl = 1;
          val += 4;                             /* skip initial `cid:' */
          valSz = strlen(val);
          mimeId->contentId = (char *)TXmalloc(pmbuf, fn, valSz + 1);
          if (mimeId->contentId == CHARPN) goto err;
          /* URL-decode rest of URL to get content-id: */
          n = urlstrncpy(mimeId->contentId, valSz, val, valSz, 0);
          if (n > valSz)
            {
              txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
                             "Internal error: URL decode out of mem");
              goto err;
            }
          mimeId->contentId[n] = '\0';
        }
    }

  /* Get the message filename: - - - - - - - - - - - - - - - - - - - - - - - -
   * 1st choice: path of non-`cid:' Content-Location URL:
   */
  if (mimeId->msgFilename == CHARPN &&          /* not set yet */
      mimeId->contentLocation != CHARPN &&
      /* `cid:' URLs are unlikely to be filenames: */
      !contentLocationIsCidUrl)
    {
      if ((url = openurl(mimeId->contentLocation)) == URLPN) goto err;
      if (url->path != CHARPN)
        mimeId->msgFilename = TXstrdup(pmbuf, fn, url->path);
    }

  /* 2nd choice: Content-Type `name' parameter, if file-like: */
  if (mimeId->msgFilename == CHARPN &&          /* not set yet */
      conTypeParams != CGISLPN &&
      (values = getcgisl(conTypeParams, "name")) != CHARPPN &&
      *values != CHARPN &&
      **values != '\0')
    mimeId->msgFilename = TXstrdup(pmbuf, fn, *values);

  /* 3rd choice: Content-Disposition `filename' parameter: */
  if (mimeId->msgFilename == CHARPN &&          /* not set yet */
      hdrs != CGISLPN &&
      (values = getcgisl(hdrs, "Content-Disposition")) != CHARPPN &&
      *values != CHARPN &&
      **values != '\0' &&
      cgiparsehdr(htpfobj, *values, &valEnd, &conDispoParams) != CHARPN &&
      (values2 = getcgisl(conDispoParams, "filename")) != CHARPPN &&
      *values2 != CHARPN &&
      **values2 != '\0')
    mimeId->msgFilename = TXstrdup(pmbuf, fn, *values2);

  /* Set `safeFilename': - - - - - - - - - - - - - - - - - - - - - - - - - */
  /* Note that we do not make `safeFilename' distinct/unique; it is up
   * to the caller to do that since it may need to be unique among
   * several MIME object layers.
   */
  /* 1st choice: official `msgFilename', if it has a (non-dir) file part: */
  if (mimeId->safeFilename == CHARPN &&         /* not set yet */
      mimeId->msgFilename != CHARPN)
    mimeId->safeFilename = copyFilenameSafe(pmbuf, mimeId->msgFilename);

  /* 2nd/3rd choice: last dir part of `msgFilename', plus extension
   * derived from Content-Type (or `.bin' if unknown).
   * E.g. dir `/foo/bar/baz/' becomes `baz.html':
   */
  if (mimeId->safeFilename == CHARPN &&
      mimeId->msgFilename != CHARPN)
    {
      e = mimeId->msgFilename + strlen(mimeId->msgFilename);
      while (e > mimeId->msgFilename && strchr(DirSep, e[-1]) != CHARPN)
        e--;                                    /* strip trailing `/' etc. */
      for (s = e;
           s > mimeId->msgFilename && strchr(DirSep, s[-1]) == CHARPN;
           s--)                                 /* find last dir part start */
        ;
      if (e > s &&                              /* have a last dir part */
          htsnpf(tmp, sizeof(tmp), "%.*s.%s", (int)(e - s), s,
                 (conTypeExt ? conTypeExt : "bin")) < (int)sizeof(tmp))
        mimeId->safeFilename = copyFilenameSafe(pmbuf, tmp);
    }

  /* 4th choice: make up a name based on Content-Type-based extension: */
  if (mimeId->safeFilename == CHARPN &&
      conType != CHARPN &&
      conTypeExt != CHARPN)
    {
      if (strnicmp(conType, "image/", 6) == 0)
        pfx = "image";
      else
        /* NOTE: see TXmimeEntityAddMimeId() if "part" changes: */
        pfx = "part";
      htsnpf(tmp, sizeof(tmp), "%s.%s", pfx, conTypeExt);
      mimeId->safeFilename = copyFilenameSafe(pmbuf, tmp);
      mimeId->safeFilenameIsMadeUp = 1;         /* completely made up */
    }

  /* 5th choice: make up a name: */
  if (mimeId->safeFilename == CHARPN)
    {
      /* NOTE: see <readmailmsg> if "part" changes: */
      mimeId->safeFilename = copyFilenameSafe(pmbuf, "part.bin");
      mimeId->safeFilenameIsMadeUp = 1;         /* completely made up */
    }

  goto done;

err:
  mimeId = TXmimeIdClose(mimeId);
done:
  url = closeurl(url);
  conTypeParams = closecgisl(conTypeParams);
  conDispoParams = closecgisl(conDispoParams);
  return(mimeId);
}

TXmimeId *
TXmimeIdClone(mimeId)
TXmimeId        *mimeId;        /* (in/out) id to clone */
/* Clones `mimeId', returning a "new" one (really the same one).
 * Close with TXmimeIdClose().
 */
{
  mimeId->refCount++;
  return(mimeId);
}

/* ------------------------------------------------------------------------ */

MIME *
TXmimeOpen(htpfobj, msgHdrs, msgContentType, msg, msgEnd, msgPathForMsgs, flags)
HTPFOBJ           *htpfobj;                 /* (opt.) for msgs and charset conv.*/
CGISL           *msgHdrs;               /* (in, opt.) message headers */
CONST char      *msgContentType;        /* (in, opt.) message Content-Type */
CONST char      *msg;                   /* (in) start of msg hdrs or body */
CONST char      *msgEnd;                /* (in) end of message */
CONST char      *msgPathForMsgs;        /* (in, opt.) `msg' path for putmsgs*/
int             flags;                  /* (in) bit flags */
/* Opens a MIME object for parsing a mail message (MIME or not).  If
 * `msgContentType' or `msgHdrs' are non-NULL, they are parsed (in that
 * order) for the Content-Type, and `msg' must be the start of the
 * message body (and no headers are returned for the first
 * TXmimeGetPartHeaders()).  If `msgContentType' and `msgHdrs' are NULL,
 * `msg' must be the start of the message headers, which are parsed
 * (and returned in the first part's TXmimeGetPartHeaders()).
 * `flags':
 *   0x0001     Open as single RFC 822 message, no multipart
 *   0x0002     Fail (w/putmsg) if `msg' does not look like email
 *              (only if `msgContentType'/`msgHdrs' not given)
 * `msgPathForMsgs', if non-NULL, is the file path that `msg' was read
 * from, for putmsgs.
 * Returned MIME object will not own or use `msgHdrs'/`msgContentType'
 * after returning from here.
 */
{
  static CONST char     fn[] = "TXmimeOpen";
  MIME                  *mime;
  char                  *d, **values;
  CONST char            *contentTypeValue, *contentTypeValueEnd, *boundary;
  CONST char            *s, *orgMsgContentType;
  size_t                n;
  CGISL                 *contentTypeParams = CGISLPN, *orgMsgHdrs;
  TXPMBUF               *pmbuf;

  pmbuf = htpfgetpmbuf(htpfobj);
  orgMsgContentType = msgContentType;
  orgMsgHdrs = msgHdrs;
  if ((mime = (MIME *)TXcalloc(pmbuf, fn, 1, sizeof(MIME))) == MIMEPN)
    goto err;
  mime->pmbuf = txpmbuf_open(pmbuf);
  if (htpfobj) mime->htpfobj = duphtpfobj(htpfobj);
  /* rest cleared by calloc() */

  mime->buf = msg;
  mime->bufEnd = msgEnd;
  mime->hdrsStart = CHARPN;                     /* until we know we have 'em*/
  if (msgContentType != CHARPN)
    goto parseContentType;
  else if (msgHdrs != CGISLPN)
    goto getContentType;
  else
    {
      /* Nothing pre-parsed; `msg' must be start of headers: */
      if ((mime->hdrs = opencgisl()) == CGISLPN ||
          !cgislsetcmp(mime->hdrs, strnicmp))   /* hdrs are case-insensitive*/
        goto err;
      if (!TXmsgParseHeaders(msg, msgEnd, mime->hdrs, &mime->buf)) goto err;
      if (mime->buf == msg && (flags & 0x2))    /* no headers found */
        {
          static const char     cannotOpen[] = "Cannot open email message";
          static const char     noHdrs[] = "No headers found at start";
          const char            *possibleMbox = "";

          if (msgEnd - msg >= 5 && strncmp(msg, "From ", 5) == 0)
            possibleMbox = "; possible Unix mbox";
          if (msgPathForMsgs)
            txpmbuf_putmsg(pmbuf, MERR + FOE, fn, "%s file `%s': %s%s",
                           cannotOpen, msgPathForMsgs, noHdrs, possibleMbox);
          else
            txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "%s string: %s%s",
                           cannotOpen, noHdrs, possibleMbox);
          goto err;
        }
      msgHdrs = mime->hdrs;
      mime->hdrsStart = mime->msgHdrsStart = msg;
    getContentType:
      values = getcgisl(msgHdrs, "Content-Type");
      if (values != CHARPPN && *values != CHARPN)  /* has a Content-Type */
        {
          msgContentType = *values;
        parseContentType:
          contentTypeValue = cgiparsehdr(htpfobj, msgContentType,
                                    &contentTypeValueEnd, &contentTypeParams);
          if (contentTypeValue == CHARPN) goto err;
          mime->msgContentTypeValue = TXstrndup(mime->pmbuf, fn,
                    contentTypeValue, contentTypeValueEnd - contentTypeValue);
          if (mime->msgContentTypeValue == CHARPN) goto err;
          if (!(flags & 0x1) &&
              strnicmp(mime->msgContentTypeValue, "multipart/", 10) == 0 &&
              contentTypeParams != CGISLPN)
            {                                   /* is multipart */
              values = getcgisl(contentTypeParams, "boundary");
              if (values != CHARPPN && *values != CHARPN &&
                  **values != '\0')             /* has a boundary param */
                {
                  boundary = *values;
                  /* Compute size of REX expression for exact search: */
                  n = 7;                        /* 7 = "\R\L--" + '\0' */
                  for (s = boundary; *s != '\0'; s++)
                    if (*s == '\\' && s[1] == 'L') n += 4;
                  n += (s - boundary);
                  mime->boundlen = (size_t)(s - boundary);
                  mime->boundaryExpr = (char *)TXmalloc(mime->pmbuf, fn, n);
                  if (mime->boundaryExpr == CHARPN) goto err;
                  /* Create REX expression: */
                  d = mime->boundaryExpr;
                  strcpy(d, "\\R\\L--");
                  d += 6;
                  for (s = boundary; *s != '\0'; )
                    if ((*d++ = *s++) == '\\' && *s == 'L')
                      {
                        strcpy(d, "\\L\\L");
                        d += 4;
                      }
                  *d++ = '\0';
                  if ((mime->rex = openrex((byte*)mime->boundaryExpr,
                                           TXrexSyntax_Rex)) == FFSPN)
                    goto err;
                  /* If multipart/related, take note of any `start'
                   * and/or `type' parameter, so we can find the start
                   * part later:
                   */
                  if (strcmpi(mime->msgContentTypeValue, "multipart/related")
                      == 0)
                    {
                      mime->isMultipartRelated = 1;
                      if ((values = getcgisl(contentTypeParams, "start"))
                          != CHARPPN &&
                          *values != CHARPN &&
                          **values != '\0')
                        mime->startContentId = TXstrdup(mime->pmbuf, fn,
                                                        *values);
                      /* `type' parameter is required for
                       * multipart/related (per RFC 2387); it gives
                       * the Content-Type of the start body part,
                       * which is just supposed to save you the effort
                       * of parsing ahead for it.  But MSIE Save-As
                       * *.mht files usually lack a `start' parameter
                       * -- which is more precise -- so `type' may be
                       * the only hint at what the start body part is
                       * for *.mht files:
                       */
                      if ((values = getcgisl(contentTypeParams, "type"))
                          != CHARPPN &&
                          *values != CHARPN &&
                          **values != '\0')
                        mime->startContentType = TXstrdup(mime->pmbuf, fn,
                                                          *values);
                    }
                }
            }
        }
      mime->mimeId = TXmimeIdOpen(htpfobj, orgMsgContentType,
                                  (orgMsgHdrs ? orgMsgHdrs : mime->hdrs));
      if (mime->mimeId == TXmimeIdPN) goto err;
    }
  mime->msgBody = mime->buf;
  goto done;

err:
  mime = TXmimeClose(mime);
done:
  contentTypeParams = closecgisl(contentTypeParams);
  return(mime);
}

MIME *
TXmimeClose(mime)
MIME    *mime;
{
  if (mime == MIMEPN) return(MIMEPN);
  if (mime->hdrs != CGISLPN) mime->hdrs = closecgisl(mime->hdrs);
  if (mime->rex != FFSPN) mime->rex = closerex(mime->rex);
  mime->boundaryExpr = TXfree(mime->boundaryExpr);
  mime->msgContentTypeValue = TXfree(mime->msgContentTypeValue);
  mime->mimeId = TXmimeIdClose(mime->mimeId);
  mime->startContentId = TXfree(mime->startContentId);
  mime->startContentType = TXfree(mime->startContentType);
  mime->htpfobj = closehtpfobj(mime->htpfobj);
  if (mime->pmbuf != TXPMBUFPN) mime->pmbuf = txpmbuf_close(mime->pmbuf);
  free(mime);
  return(MIMEPN);
}

int
TXmimeIsMultipartMsg(mime)
MIME    *mime;
/* Returns 1 if `mime' is a multipart MIME message *and* is opened as such,
 * 0 if not.
 */
{
  return(mime->rex != FFSPN);
}

char *
TXmimeGetMsgContentTypeValue(mime)
MIME    *mime;
/* Returns overall-message Content-Type value (sans params).
 */
{
  return(mime->msgContentTypeValue);
}

char *
TXmimeGetNextPart(mime)
MIME            *mime;
/* Returns pointer to body of next MIME part; returns non-MIME
 * alternate "part" first (only part returned if non-multipart message).
 * Get part's body size with TXmimeGetPartBodySize(); part's headers
 * with TXmimeGetPartHeaders().  Returns NULL on EOF (no more parts).
 */
{
  CONST char    *hit, *s, *e, *next = CHARPN;
  size_t        n;

  if (mime->rex)                                /* if multipart */
    {
      hit = (char *)getrex(mime->rex, (byte *)mime->buf, (byte *)mime->bufEnd,
                           SEARCHNEWBUF);
      n = rexsize(mime->rex);
    }
  else                                          /* not multipart */
    {
      hit = NULL;
      n = 0;
    }
  if (!hit)                                     /* no boundary or not found */
    {
      /* mime->buf == mime->bufEnd means no more parts, but could also
       * mean the first part is truncated; still want the headers then,
       * so check if first part:
       */
      if (mime->buf >= mime->bufEnd && mime->partCount > 0)
        goto err;                               /* no more parts */
      hit = mime->bufEnd;
      n = 0;
    }

  /* Advance over end of boundary: */
  if (mime->rex)                                /* if multipart */
    {
      s = hit + n;                              /* end of boundary string */
      if (s + 1 < mime->bufEnd && *s == '-' && s[1] == '-')
        next = mime->bufEnd;                    /* boundary + "--" is EOF */
      else
        {
          /* EOL (if any) after the boundary is part of the boundary: */
          htskipeol((char **)&s, (char *)mime->bufEnd);/* skip `s' past EOL */
          next = s;
        }
    }
  else                                          /* not multipart */
    next = mime->bufEnd;

  /* Caller may want to see the 0th "part" -- the alternate for the
   * entire multipart message for non-MIME mail readers, e.g. `This is
   * a multi-part message in MIME format.' -- so we return it here.
   * But note that we may not have the headers for it, i.e. if caller
   * passed `msgHdrs' or `msgContentType' to TXmimeOpen():
   */
  if (mime->partCount == 0)                     /* 0th/non-MIME "part" */
    mime->body = mime->buf;
  else
    {
      /* Grab the headers: */
      mime->hdrs = closecgisl(mime->hdrs);
      if ((mime->hdrs = opencgisl()) == CGISLPN ||
          !cgislsetcmp(mime->hdrs, strnicmp))   /* hdrs are case-insensitive*/
        goto err;
      if (!TXmsgParseHeaders(mime->buf, hit, mime->hdrs, &mime->body))
        goto err;
      mime->hdrsStart = mime->buf;
      mime->mimeId = TXmimeIdClose(mime->mimeId);
      mime->mimeId = TXmimeIdOpen(mime->htpfobj, CHARPN, mime->hdrs);
      if (mime->mimeId == TXmimeIdPN) goto err;

      /* See if this is the start part.  Note that since the start
       * part may only be identified by Content-Type -- less specific
       * than Content-ID -- we only flag the first matching part as
       * the start, in case there are multiple for some reason (or no
       * id/type at all, in which case we take the first part):
       */
      mime->curPartIsStart = 0;
      if (!mime->sawStart && mime->isMultipartRelated)
        {
          if (mime->startContentId != CHARPN &&
              mime->mimeId->contentId != CHARPN &&
              strcmp(mime->mimeId->contentId, mime->startContentId) == 0)
            mime->curPartIsStart = 1;
          else if (mime->startContentType != CHARPN &&
                   mime->mimeId->contentType != CHARPN &&
                   strcmp(mime->mimeId->contentType,
                          mime->startContentType) == 0)
            mime->curPartIsStart = 1;
          /* If there is no start content-id or type, first part is start: */
          else if (mime->startContentId == CHARPN &&
                   mime->startContentType == CHARPN)
            mime->curPartIsStart = 1;
        }
      if (mime->curPartIsStart) mime->sawStart = 1;
    }

  /* Get true data end; back up a CRLF from the boundary: */
  s = mime->body;
  e = hit;
  if (e > s && mime->rex && (e[-1] == LF || e[-1] == CR))
    {
      if (--e > s && (e[-1] == CR || e[-1] == LF) && *e != e[-1])
        e--;
    }
  mime->bodyEnd = e;
  goto done;

err:
  mime->body = mime->bodyEnd = CHARPN;
done:
  mime->partCount++;
  mime->buf = next;
  return((char *)mime->body);
}

int
TXmimeGetPartIsLast(mime)
MIME    *mime;
/* Returns nonzero if current part is the last part.
 */
{
  return(mime->buf >= mime->bufEnd);
}

size_t
TXmimeGetPartBodySize(mime)
MIME    *mime;
/* Returns length of body data (after TXmimeGetNextPart() called).
 */
{
  return(mime->bodyEnd - mime->body);
}

char *
TXmimeGetPartHeadersStart(mime)
MIME    *mime;
/* Returns offset of start of headers for current part (after
 * TXmimeGetNextPart() called).  May be NULL if no headers passed to
 * TXmimeOpen().
 */
{
  return((char *)mime->hdrsStart);
}

CGISL *
TXmimeGetPartHeaders(mime, release)
MIME    *mime;
int     release;        /* (in) nonzero: caller will own the list */
/* Returns list of headers from last hit.  If `release' nonzero,
 * caller owns the list and must closecgisl() it.  Note that first/0th
 * part is the overall message headers, which may or may not be
 * returned here, depending on TXmimeOpen() args.
 */
{
  CGISL *sl;

  sl = mime->hdrs;
  if (release) mime->hdrs = CGISLPN;
  return(sl);
}

int
TXmimeGetPartIsStart(mime)
MIME    *mime;
/* Returns nonzero if current part is the start part (of multipart/related),
 * 0 if not.
 */
{
  return(mime->curPartIsStart);
}

TXmimeId *
TXmimeGetMimeId(mime, release)
MIME    *mime;          /* (in) MIME object */
int     release;        /* (in) nonzero: caller will own return value */
{
  TXmimeId      *ret = mime->mimeId;

  if (release) mime->mimeId = TXmimeIdPN;
  return(ret);
}

char *
TXmimeGetMessageHeadersStart(MIME *mime)
/* Returns start of headers for overall message (i.e. 0th part).
 * Might be NULL if initial buffer was body-only.
 */
{
  return((char *)mime->msgHdrsStart);
}

char *
TXmimeGetMessageBody(MIME *mime)
/* Returns start of body for overall message.
 */
{
  return((char *)mime->msgBody);
}

size_t
TXmimeGetMessageBodySize(MIME *mime)
/* Returns size of overall message body (starts at TXmimeGetMessageBody()).
 */
{
  return(mime->bufEnd - mime->msgBody);
}
