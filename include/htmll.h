#ifndef HTMLL_H
#define HTMLL_H


/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/

/* >>> NOTE:  Make sure these do not conflict with ones in vortexi.h.
 * >>> NOTE:  must be in case-insensitive ascending order by string name.
 * >>> NOTE:  update JDOM_VERSION if this list changes/expands.
 * I(token, stringName)
 */
#define HATTR_NAME_LIST                         \
I(ACTION,               "ACTION")               \
I(ALIGN,                "ALIGN")                \
I(ALINK,                "ALINK")                \
I(ALT,                  "ALT")                  \
I(BACKGROUND,           "BACKGROUND")           \
I(BGCOLOR,              "BGCOLOR")              \
I(BORDER,               "BORDER")               \
I(CELLPADDING,          "CELLPADDING")          \
I(CELLSPACING,          "CELLSPACING")          \
I(CHECKED,              "CHECKED")              \
I(CLASS,                "CLASS")                \
I(CODE,                 "CODE")                 \
I(CODEBASE,             "CODEBASE")             \
I(COLOR,                "COLOR")                \
I(COLS,                 "COLS")                 \
I(COLSPAN,              "COLSPAN")              \
I(COMPACT,              "COMPACT")              \
I(CONTENT,              "CONTENT")              \
I(COORDS,               "COORDS")               \
I(DATA,                 "DATA")                 \
I(DISABLED,             "DISABLED")             \
I(DYNSRC,               "DYNSRC")               \
I(ENCODING,             "ENCODING")             \
I(ENCTYPE,              "ENCTYPE")              \
I(FIXED,                "FIXED")                \
I(FRAMEBORDER,          "FRAMEBORDER")          \
I(HEIGHT,               "HEIGHT")               \
I(HREF,                 "HREF")                 \
I(HSPACE,               "HSPACE")               \
I(HTTP_EQUIV,           "HTTP-EQUIV")           \
I(ID,                   "ID")                   \
I(ISMAP,                "ISMAP")                \
I(LANG,                 "LANG")                 \
I(LANGUAGE,             "LANGUAGE")             \
I(LINK,                 "LINK")                 \
I(LOWSRC,               "LOWSRC")               \
I(MAXLENGTH,            "MAXLENGTH")            \
I(METHOD,               "METHOD")               \
I(METHODS,              "METHODS")              \
I(MULTIPLE,             "MULTIPLE")             \
I(N,                    "N")                    \
I(NAME,                 "NAME")                 \
I(NOHREF,               "NOHREF")               \
I(NORESIZE,             "NORESIZE")             \
I(NOSHADE,              "NOSHADE")              \
I(NOWRAP,               "NOWRAP")               \
I(onAbort,              "ONABORT")              \
I(onAfterPrint,         "ONAFTERPRINT")         \
I(onBeforePrint,        "ONBEFOREPRINT")        \
I(onBeforeUnload,       "ONBEFOREUNLOAD")       \
I(onBlur,               "ONBLUR")               \
I(onChange,             "ONCHANGE")             \
I(onClick,              "ONCLICK")              \
I(onClose,              "ONCLOSE")              \
I(onDblClick,           "ONDBLCLICK")           \
I(onDragDrop,           "ONDRAGDROP")           \
I(onError,              "ONERROR")              \
I(onFocus,              "ONFOCUS")              \
I(onHelp,               "ONHELP")               \
I(onKeyDown,            "ONKEYDOWN")            \
I(onKeyPress,           "ONKEYPRESS")           \
I(onKeyUp,              "ONKEYUP")              \
I(onLoad,               "ONLOAD")               \
I(onMouseDown,          "ONMOUSEDOWN")          \
I(onMouseMove,          "ONMOUSEMOVE")          \
I(onMouseOut,           "ONMOUSEOUT")           \
I(onMouseOver,          "ONMOUSEOVER")          \
I(onMouseUp,            "ONMOUSEUP")            \
I(onMove,               "ONMOVE")               \
I(onReset,              "ONRESET")              \
I(onResize,             "ONRESIZE")             \
I(onScroll,             "ONSCROLL")             \
I(onSelect,             "ONSELECT")             \
I(onSubmit,             "ONSUBMIT")             \
I(onUnload,             "ONUNLOAD")             \
I(PLUGINSPAGE,          "PLUGINSPAGE")          \
I(PLUGINURL,            "PLUGINURL")            \
I(REL,                  "REL")                  \
I(REV,                  "REV")                  \
I(ROWS,                 "ROWS")                 \
I(ROWSPAN,              "ROWSPAN")              \
I(SCROLL,               "SCROLL")               \
I(SCROLLING,            "SCROLLING")            \
I(SELECTED,             "SELECTED")             \
I(SHAPE,                "SHAPE")                \
I(SIZE,                 "SIZE")                 \
I(SRC,                  "SRC")                  \
I(STYLE,                "STYLE")                \
I(TARGET,               "TARGET")               \
I(TEXT,                 "TEXT")                 \
I(TITLE,                "TITLE")                \
I(TYPE,                 "TYPE")                 \
I(URN,                  "URN")                  \
I(VALIGN,               "VALIGN")               \
I(VALUE,                "VALUE")                \
I(VLINK,                "VLINK")                \
I(VSPACE,               "VSPACE")               \
I(WIDTH,                "WIDTH")

enum HATTR_tag
{
  HATTR_UNKNOWN = -1,   /* must be first */
#undef I
#define I(name, s)      HATTR_##name,
HATTR_NAME_LIST
#undef I
  /* must be last: */
  HATTR_NUM
};
#ifndef HATTRPN
typedef enum HATTR_tag  HATTR;
#  define HATTRPN       ((HATTR *)NULL)
#endif /* !HATTRPN */

typedef enum HEF_tag            /* HTML Element Flags  KNG 971022 */
{
  HEF_LQUOTE    = 0x001,        /* has left single quote */
  HEF_RQUOTE    = 0x002,        /* has right single quote */
  HEF_LDQUOTE   = 0x004,        /* has left double quote */
  HEF_RDQUOTE   = 0x008,        /* has right double quote */
  /* these 4 must be same order as above: */
  HEF_VLQUOTE   = 0x010,        /* value has left single quote */
  HEF_VRQUOTE   = 0x020,
  HEF_VLDQUOTE  = 0x040,
  HEF_VRDQUOTE  = 0x080,
  HEF_VALUE     = 0x100,        /* has = sign, eg. value */
  HEF_RBRACKET  = 0x200,        /* tag has closing > */
  HEF_RSLASH    = 0x400         /* tag has closing `/>' eg. <p/> */
}
HEF;

#undef HTA                                      /* HPUX 11.11 conflict */

typedef struct HTA_tag
{
  char                 *name;   /* alloced */
  char                 *value;  /* alloced */
   struct HTA_tag       *next;
   HATTR                attr;   /* HATTR for `name', or -1 KNG 960708 */
   HEF                  flags;  /* HEF flags  KNG 971022 */
   int                  bufnum; /* buffer number (0: sethtl, >0: htl_push) */
  size_t                chunkIndex;
  size_t                chunkOffset;
  size_t                length; /* byte length in source */
   int                  linenum;/* line number of start of attribute name */
   int                  bytenum;/* byte number (on `linenum') */
  char                  *srcFile;       /* (opt.) alloced linemarker file */
  size_t                srcLine;        /* (opt.) source line (linemarker) */
}
HTA;
#define HTAPN   ((HTA *)NULL)
#define HTAPPN  ((HTA **)NULL)

typedef enum HTE__tag
{
  HTE_TAG = 1,
  HTE_ETAG,
  HTE_WORD,
  HTE_PUNCT,
  HTE_SPACE,
  HTE_TAB,
  HTE_NEWLINE,
  HTE_WHITE,
  HTE_COMMENT,
  HTE_SCRIPT,           /* KNG 960422 */
  HTE_CDATA             /* KNG 010411 */
}
HTE_;                   /* avoid conflict with HTE struct KNG 970130 */

typedef enum HL_tag             /* HTML Lexer flags for individual tags */
{
  /* these flags are mutually exclusive: */
  HL_0          = 0,            /* no end tag (void element) */
  HL_1          = 1,            /* text elements with an end tag (<B>, <I>) */
  HL_2          = 2,            /* one-level blocks (<PRE>) */
  HL_3          = 3,            /* 2nd-level block (<TR>) */
  HL_4          = 4,            /* 3rd-level block (<TABLE>) */
#define HL_LEVEL_MASK   7
  /* these flags are OR'd with the above: */
  HL_ID         = (1 << 3),     /* ID attribute ok */
  HL_NAME       = (1 << 4),     /* NAME attribute ok */
  HL_FLOW       = (1 << 5),     /* open tag closes stk top */
  HL_FGCOLOR    = (1 << 6),     /* tag has foreground COLOR attribute */
  HL_BGCOLOR    = (1 << 7)      /* tag has background BGCOLOR attribute */
}
HL;
#define HLPN    ((HL *)NULL)

/* Known tags.   I(token, stringName, flags)
 * wtf a few of these are namespace-prefixed XML tags; we just assume
 * the prefix is the correct namespace until formatter handles namespaces.
 * >>> NOTE:  Make sure these do not conflict with ones in vortexi.h.
 * >>> NOTE:  must be in case-insensitive ascending order by tag name.
 * >>> NOTE:  update JDOM_VERSION if this list changes/expands.
 */
#define HTAG_SYMBOLS_LIST                                                 \
I(DOCTYPE,    "!DOCTYPE",     (HL_0                       ))              \
I(XML,        "?xml",         (HL_0                       ))              \
I(A,          "A",            (HL_1|                      HL_ID|HL_NAME)) \
I(ABBR,       "ABBR",         (HL_1|                      HL_ID))         \
I(ACRONYM,    "ACRONYM",      (HL_1|                      HL_ID))         \
I(ADDRESS,    "ADDRESS",      (HL_2|                      HL_ID))         \
I(APP,        "APP",          (HL_1|                      HL_ID))         \
I(APPLET,     "APPLET",       (HL_1|                      HL_ID|HL_NAME)) \
I(AREA,       "AREA",         (HL_0|                      HL_ID))         \
I(B,          "B",            (HL_1|                      HL_ID))         \
I(BASE,       "BASE",         (HL_0|                      HL_ID))         \
I(BASEFONT,   "BASEFONT",     (HL_0|                      HL_ID))         \
I(BDO,        "BDO",          (HL_1|                      HL_ID))         \
I(BGSOUND,    "BGSOUND",      (HL_0|                      HL_ID))         \
I(BIG,        "BIG",          (HL_1|                      HL_ID))         \
I(BLINK,      "BLINK",        (HL_1                       ))              \
I(BLOCKQUOTE, "BLOCKQUOTE",   (HL_2|                      HL_ID))         \
I(BODY,       "BODY",         (HL_4|                      HL_ID))         \
I(BR,         "BR",           (HL_0|                      HL_ID))         \
I(BUTTON,     "BUTTON",       (HL_2|                      HL_ID|HL_NAME)) \
I(CAPTION,    "CAPTION",      (HL_2| HL_FLOW|             HL_ID))         \
I(CDATA,      "CDATA",        (HL_0                       ))              \
I(CENTER,     "CENTER",       (HL_2|                      HL_ID))         \
I(CITE,       "CITE",         (HL_1|                      HL_ID))         \
I(CODE,       "CODE",         (HL_1|                      HL_ID))         \
I(COL,        "COL",          (HL_0|                      HL_ID))         \
I(COLGROUP,   "COLGROUP",     (HL_2| HL_FLOW|             HL_ID))         \
I(COMMENT,    "COMMENT",      (HL_2|                      HL_ID))         \
I(DD,         "DD",           (HL_2| HL_FLOW|             HL_ID))         \
I(DEL,        "DEL",          (HL_1|                      HL_ID))         \
I(DFN,        "DFN",          (HL_1|                      HL_ID))         \
I(DIR,        "DIR",          (HL_2|                      HL_ID))         \
I(DIV,        "DIV",          (HL_1|                      HL_ID))         \
I(DL,         "DL",           (HL_3|                      HL_ID))         \
I(DT,         "DT",           (HL_2| HL_FLOW|             HL_ID))         \
I(EM,         "EM",           (HL_1|                      HL_ID))         \
I(EMBED,      "EMBED",        (HL_0|                      HL_ID|HL_NAME)) \
I(ENCODED,    "encoded",      (HL_2                       ))              \
I(FEED,       "feed",         (HL_4                       ))              \
I(FIELDSET,   "FIELDSET",     (HL_2|                      HL_ID))         \
I(FONT,       "FONT",         (HL_1|         HL_FGCOLOR|  HL_ID))         \
I(FORM,       "FORM",         (HL_4|                      HL_ID|HL_NAME)) \
I(FRAME,      "FRAME",        (HL_4|                      HL_ID|HL_NAME)) \
I(FRAMESET,   "FRAMESET",     (HL_4|                      HL_ID|HL_NAME)) \
I(H1,         "H1",           (HL_2                       ))              \
I(H2,         "H2",           (HL_2                       ))              \
I(H3,         "H3",           (HL_2                       ))              \
I(H4,         "H4",           (HL_2                       ))              \
I(H5,         "H5",           (HL_2                       ))              \
I(H6,         "H6",           (HL_2                       ))              \
I(HEAD,       "HEAD",         (HL_4|                      HL_ID))         \
I(HR,         "HR",           (HL_0|                      HL_ID))         \
I(HTML,       "HTML",         (HL_4|                      HL_ID))         \
I(I,          "I",            (HL_1|                      HL_ID))         \
I(IFRAME,     "IFRAME",       (HL_3|                      HL_ID|HL_NAME)) \
I(IMG,        "IMG",          (HL_0|                      HL_ID|HL_NAME)) \
I(INPUT,      "INPUT",        (HL_0                       ))              \
I(INS,        "INS",          (HL_1|                      HL_ID))         \
I(ISINDEX,    "ISINDEX",      (HL_0|                      HL_ID))         \
I(KBD,        "KBD",          (HL_1|                      HL_ID))         \
I(LABEL,      "LABEL",        (HL_1|                      HL_ID))         \
I(LEGEND,     "LEGEND",       (HL_1|                      HL_ID))         \
I(LI,         "LI",           (HL_2| HL_FLOW|             HL_ID))         \
I(LINK,       "LINK",         (HL_0|                      HL_ID|HL_NAME)) \
I(LISTING,    "LISTING",      (HL_2|                      HL_ID))         \
I(MAP,        "MAP",          (HL_0|                      HL_ID|HL_NAME)) \
I(MARK,       "MARK",         (HL_1|                      HL_ID))         \
I(MENU,       "MENU",         (HL_2|                      HL_ID))         \
I(META,       "META",         (HL_0|                            HL_NAME)) \
I(NEXTID,     "NEXTID",       (HL_0|                      HL_ID))         \
I(NOBR,       "NOBR",         (HL_0|                      HL_ID))         \
I(NOFRAMES,   "NOFRAMES",     (HL_4|                      HL_ID))         \
I(NOSCRIPT,   "NOSCRIPT",     (HL_2|                      HL_ID))         \
I(OBJECT,     "OBJECT",       (HL_2|                      HL_ID|HL_NAME)) \
I(OL,         "OL",           (HL_3|                      HL_ID))         \
I(OPTGROUP,   "OPTGROUP",     (HL_1|                      HL_ID))         \
I(OPTION,     "OPTION",       (HL_2| HL_FLOW|             HL_ID))         \
I(P,          "P",            (HL_2| HL_FLOW|             HL_ID))         \
I(PARAM,      "PARAM",        (HL_0|                            HL_NAME)) \
I(PLAINTEXT,  "PLAINTEXT",    (HL_2                       ))              \
I(PRE,        "PRE",          (HL_2|                      HL_ID))         \
I(Q,          "Q",            (HL_1|                      HL_ID))         \
I(RSS,        "rss",          (HL_4                       ))              \
I(S,          "S",            (HL_1|                      HL_ID))         \
I(SAMP,       "SAMP",         (HL_2|                      HL_ID))         \
I(SCRIPT,     "SCRIPT",       (HL_4|                      HL_ID))         \
I(SELECT,     "SELECT",       (HL_3|                      HL_ID|HL_NAME)) \
I(SMALL,      "SMALL",        (HL_1|                      HL_ID))         \
I(SPAN,       "SPAN",         (HL_1|                      HL_ID))         \
I(STRIKE,     "STRIKE",       (HL_1|                      HL_ID))         \
I(STRONG,     "STRONG",       (HL_1|                      HL_ID))         \
I(STYLE,      "STYLE",        (HL_2|                      HL_ID))         \
I(SUB,        "SUB",          (HL_1|                      HL_ID))         \
I(SUP,        "SUP",          (HL_1|                      HL_ID))         \
I(TABLE,      "TABLE",        (HL_4|         HL_BGCOLOR|  HL_ID))         \
I(TBODY,      "TBODY",        (HL_3|                      HL_ID))         \
I(TD,         "TD",           (HL_2| HL_FLOW|HL_BGCOLOR|  HL_ID))         \
I(TEXTAREA,   "TEXTAREA",     (HL_2|                      HL_ID|HL_NAME)) \
I(TFOOT,      "TFOOT",        (HL_3|                      HL_ID))         \
I(TH,         "TH",           (HL_2| HL_FLOW|HL_BGCOLOR|  HL_ID))         \
I(THEAD,      "THEAD",        (HL_3|                      HL_ID))         \
I(TITLE,      "TITLE",        (HL_4|                      HL_ID))         \
I(TR,         "TR",           (HL_3| HL_FLOW|HL_BGCOLOR|  HL_ID))         \
I(TT,         "TT",           (HL_1|                      HL_ID))         \
I(U,          "U",            (HL_1|                      HL_ID))         \
I(UL,         "UL",           (HL_3|                      HL_ID))         \
I(VAR,        "VAR",          (HL_1|                      HL_ID))         \
I(VERB,       "VERB",         (HL_1                       ))              \
I(WBR,        "WBR",          (HL_0|                      HL_ID))         \
I(XMP,        "XMP",          (HL_1|                      HL_ID))

extern const HL TxHtagFlags[];

enum HTAG_tag
{
  HTAG_UNKNOWN = -1,    /* must be first */
#undef I
#define I(tok, name, flags)     HTAG_##tok,
  HTAG_SYMBOLS_LIST
#undef I
  /* must be last: */
  HTAG_NUM
};
#ifndef HTAGPN
typedef enum HTAG_tag   HTAG;
#  define HTAGPN        ((HTAG *)NULL)
#endif /* !HTAGPN */

typedef struct HTE_tag
{
   HTE_ type;
   char *data;
   int   dlen;
  char  *prefix;        /* (opt.) alloced XML NS prefix iff HF_XML */
   char *name;          /* alloced tag name, sans XML NS prefix iff HF_XML */
   HTA  *attr;
   HTAG	tag;            /* HTAG value for `name' (HTAG_UNKNOWN if unknown) */
   int  bufnum;         /* buffer number (0 == sethtl(), >0 == htl_push()) */
  EPI_SSIZE_T   rawDocOffset;   /* offset into original buf (or -1 if not) */
  size_t        processedDocOffset;     /* offset into sum of chunks */
  size_t        chunkIndex;     /* index of current chunk */
  size_t        chunkOffset;    /* offset into current chunk */
   int  linenum;        /* line number of start of tag KNG 960706 */
   int  bytenum;        /* byte number (on `linenum') */
  char  *srcFile;       /* (opt.) alloced linemarker file */
  size_t srcLine;       /* (opt.) linemarker line */
   int  nattrs;         /* number of attributes KNG 960715 */
   char *value;         /* '=' value, if any  KNG 960724 */
   HEF  flags;          /* HEF flags      KNG 971022 */
}
HTE;
#define HTEPN   ((HTE *)NULL)

/* settings: */
#define HF_SYMBOLS_LIST \
I(UWHITE)       /* union all whitespace as HTE_SPACE */         \
I(UNWHITE)      /* union all nonwhitespace as HTE_WORD */       \
I(UTEXT)        /* union all nontags as HTE_WORD */             \
I(RETCOMMENT)   /* return comments instead of eating */         \
I(RETSCRIPT)    /* return scripts instead of eating -KNG */     \
I(PARSECOM)     /* parse comments for scripts KNG 960708 */     \
I(ALLOWPUNCT)   /* allow punct in attr names */                 \
I(VERBSCRIPT)   /* grok <VERB> elements as scripts 960813 */    \
I(STRICTCOMMENT)/* comment start requires dashes: <!-- ... */   \
I(NESTCOMMENT)  /* comments can nest */                         \
I(XML)          /* grok <?xml...?>, <![CDATA[...]]>, XML NS prefixes */ \
I(PARSESCRIPT)  /* parse <SCRIPT> bodies as normal HTML */      \
I(NoEmptyElements) /* ending `/' in `.../>' is part of name/val not tag */ \
I(RslashBindsToAttrNames) /*if !NoEmptyElements: `/' part of attr names too*/\
I(ParseLineMarkers)     /* parse `# NN "file"' preprocessor linemarkers */
/* Note that linemarkers are essentially incompatible with htl_push()
 * buffers (e.g. from JavaScript document.write()s)
 */

typedef enum HFint_tag                          /* internal header usage */
{
#undef I
#define I(tok)  HFint_##tok,
  HF_SYMBOLS_LIST
#undef I
}
HFint;
#define HFintPN ((HFint *)NULL)

typedef enum HF_tag
{
#undef I
#define I(tok)  HF_##tok = (1 << HFint_##tok),
  HF_SYMBOLS_LIST
#undef I
}
HF;
#define HFPN    ((HF *)NULL)

typedef struct HTLBUF_tag                       /* a saved buffer chunk */
{
  struct HTLBUF_tag     *next;                  /* next buffer in list */
  char                  *buf;                   /* actual buffer */
  int                   freebuf;                /* free `buf' when done */
}
HTLBUF;
#define HTLBUFPN        ((HTLBUF *)NULL)

typedef struct HTLCHK_tag                       /* a parsed chunk of buffer */
{
  struct HTLCHK_tag     *next;                  /* next in list */
  size_t                bufnum;                 /* buf # (0==sethtl) */
  CONST char            *buf;                   /* pointer into HTLBUFs */
  size_t                sz;                     /* size of `buf' area */
}
HTLCHK;
#define HTLCHKPN        ((HTLCHK *)NULL)

typedef enum TXtraceHtmlParse_tag
  {
    TXtraceHtmlParse_None               = 0,
    TXtraceHtmlParse_TagsParsed         = 0x0001,
#define    TX_PUTMSG_TagsParsed_Info    (MINFO)
    TXtraceHtmlParse_AttrsParsed        = 0x0002,
#define    TX_PUTMSG_AttrsParsed_Info   (MINFO + 1)
    TXtraceHtmlParse_CommentScriptCdata = 0x0004,
#define    TX_PUTMSG_CommentScriptCdata_Info (MINFO + 2)
    TXtraceHtmlParse_WordPunctWhite     = 0x0008,
#define    TX_PUTMSG_WordPunctWhite_Info (MINFO + 3)
    TXtraceHtmlParse_TagStack           = 0x0010,       /* HTML fmt'r only */
#define    TX_PUTMSG_TagStack_Info      (MINFO + 4)
    TXtraceHtmlParse_BufferStack        = 0x0020        /* push/pop buffers */
#define    TX_PUTMSG_BufferStack_Info   (MINFO + 5)
  }
  TXtraceHtmlParse;

struct HTL_tag                                  /* HTML lexical object */
{
  TXPMBUF       *pmbuf;                         /* message buffer */
   char *ibuf;                                  /* input buffer */
   char *iend;                                  /* input buffer end+1 */
   char *istart;                                /* starting parse point */
   char *icur;                                  /* current pos in ibuf */
   HTE  ret;                                    /* parsed out item */
   HF   flags;                                  /* settings KNG 960706 */
   int  state;                                  /* state flags KNG 960706 */
   int  bufcnt;                                 /* buffers seen so far */
   int  bufnum;                                 /* buf # (sethtl/htl_push) */
   int  linenum;                                /* current line KNG 960706 */
  char  *srcFile;                               /* (opt.) current linemarker*/
  size_t srcLine;                               /* (opt.) "" */
   char *sol;                                   /* start of current line */
   struct HTL_tag        *next;                 /* next buffer */
   HTLBUF       *bufs;                          /* sethtl + htl_push bufs */
   HTLCHK       *chks;                          /* parsed chunks */
  size_t        numChunks;                      /* # items in `chks' */
  size_t        prevParsedChunkBytes;
  TXtraceHtmlParse      traceHtmlParse;
};

#ifndef HTLPN
typedef struct HTL_tag  HTL;
#  define HTLPN ((HTL *)NULL)
#endif /* !HTLPN */

/**********************************************************************/
HTL  *closehtl   ARGS((HTL *ht));
int   sethtl     ARGS((HTL *ht, char *buf, size_t sz, int freeit));
int   htl_push ARGS((HTL *ht, char *buf, size_t sz, int freeit));
HTL  *openhtl    ARGS((TXPMBUF *pmbuf));
int   TXhtlSetPmbuf ARGS((HTL *htl, TXPMBUF *pmbuf));
TXPMBUF *TXhtlGetPmbuf ARGS((HTL *htl));
TXbool TXhtlSetTraceHtmlParse(HTL *htl, TXtraceHtmlParse trace);
TXtraceHtmlParse TXhtlGetTraceHtmlParse(HTL *htl);
char *TXstrSummary(char *buf, size_t bufSz, const char *s, size_t sLen);
HTE  *gethtl     ARGS((HTL *ht));
HF   htlsetflags ARGS((HTL *ht, HF flags, int set));
int   htl_getcurpos(HTL *ht, int *bufcnt, int *bufnum,
                    EPI_SSIZE_T *rawDocOffset, size_t *processedDocOffset,
                    size_t *chunkOffset, int *linenum, int *bytenum);
size_t htl_getnextchk ARGS((HTL *ht, CONST char **bufp, size_t *szp,
                            void **statep));

/* backwards compatibility: KNG 960706 */
#define uwhitehtl(ht, f)        htlsetflags(ht, HF_UWHITE, f)
#define unwhitehtl(ht, f)       htlsetflags(ht, HF_UNWHITE, f)
#define utexthtl(ht, f)         htlsetflags(ht, HF_UTEXT, f)
#define retcommenthtl(ht, f)    htlsetflags(ht, HF_RETCOMMENT, f)
#define retscriptshtl(ht, f)    htlsetflags(ht, HF_RETSCRIPT, f)
HTE  *duphte     ARGS((TXPMBUF *pmbuf, HTE *ht));
HTE  *freehte    ARGS((HTE *ht));
char    *htl_dumphte ARGS((char *buf, size_t sz, CONST HTE *he, int flags));

int htlname2int ARGS((CONST char *name, size_t namesz,
                      CONST char * CONST *list, CONST int *idx, int num));
int httag2int ARGS((CONST char *name, size_t namesz));
CONST char *htint2tag ARGS((HTAG tag));
int htattr2int ARGS((CONST char *name, size_t namesz));
CONST char *htint2attr ARGS((HATTR attr));

/**********************************************************************/
#endif                                                     /* HTMLL_H */
