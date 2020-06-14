#ifndef JDOMI_H
#define JDOMI_H


/* Internal JavaScript/DOM objects
 *
 * >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> NOTE <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 * >> If any structs, etc. change here or elsewhere, update JDOM_VERSION <<
 * >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> NOTE <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 */

typedef enum HJF_tag
{
  HJF_MEMEXCEEDED       = (1 << 0),     /* mem limit was exceeded */
  HJF_INTERRUPTSET      = (1 << 1),     /* JS interrupt set */
  HJF_TIMEOUT           = (1 << 2),     /* scripttimeout reached */
  HJF_MAXPGSIZE         = (1 << 3),     /* maxpgsize reached */
  HJF_MAJORERROR        = (1 << 4),     /* reporting major error */
  HJF_TIMEOUTSET        = (1 << 5),     /* timeout has been set */
  HJF_STRLINKS          = (1 << 6),     /* check Strings for URLs */
  HJF_INTERRUPTCALLED   = (1 << 7),     /* JS interrupt called */
  HJF_ASSERTION_FAILED  = (1 << 8),     /* JS_ASSERT() failed */
  HJF_OUT_OF_MEMORY_REPORTED = (1 << 9)
}
HJF;
#define HJFPN   ((HJF *)NULL)

#ifndef HTJDPN
typedef struct HTJD_tag HTJD;
#  define HTJDPN        ((HTJD *)NULL)
#endif

struct HTJD_tag                 /* JavaScript/DOM state object for a page */
{
  TXPMBUF       *pmbuf;
  JSRuntime     *rt;
  JSContext     *cx;
  JSObject      *glob;          /* our global object (Window) */
  JSObject      *doc;           /* our document object */
  JSObject      *event;         /* event object (if used) */
  char          *domainbuf, *domainval; /* document.domain buffer/value */
  HTOBJ         *obj;           /* (temp per call) object WTF make perm.? */
  HTBUF         *docbuf;        /* output buffer for document object */
  size_t        allocbytes;     /* mem alloced so far */
  size_t        outputbytes;    /* output written so far (inc. orig. page) */
  HJF           flags;          /* internal state flags */
  int           errnum;         /* temp error number, if HJF_MAJORERROR */
  double        timeleft;       /* time left to run (before scripttimeout) */
  double        orgTimeLeft;    /* original time left (i.e. scripttimeout) */
  size_t        scriptmem;      /* from HTOBJ; wtf update on HTOBJ change */
  int           tracescript;    /* from HTOBJ; wtf update on HTOBJ change */
  byte          htsfFlags[HTSF_BYTE_ARRAY_SZ];  /* from HTOBJ; "" */
  HTERR         *hterrno;
  HTERR         myHterrno;      /* `hterrno' points here iff NULL `obj' */
  CONST char    *abendpfx;      /* (abend info) message prefix */
  CONST char    *pgurl;         /* (abend info) page URL */
  CONST char    *script;        /* (abend info) script URL/file */
  int           startline;      /* (abend info) starting script line number */
};

#define JD_DUMPVAL(cx, val)     \
  putmsg(999, CHARPN, "%s = [%s]", JS_GetTypeName(cx,JS_TypeOfValue(cx,val)),\
         JS_GetStringBytes(JS_ValueToString(cx, val)))
#define JD_PR(jd, fmt, arg)                             \
{ char _t[1024];                                        \
  JdImports.htsnpf(_t, sizeof(_t), fmt "\n", arg);      \
  JdImports.htbuf_write((jd)->docbuf, _t, strlen(_t)); }

/* True if `jd' is potentially corrupt, and no routines should be called: */
#define JD_CORRUPT(jd)  ((jd)->flags & (HJF_MEMEXCEEDED | HJF_TIMEOUT))

/* ----------------------------- jdmain.c: -------------------------------- */

int     jd_getfileline ARGS((JSContext *cx, CONST char **file, int *linenum));
size_t  jd_abendloccb ARGS((char *buf, size_t sz, void *usr));
int     jd_begintimeout(HTJD *jd, HTPAGE *pg, const char *scriptUrl);
int     jd_endtimeout(HTJD *jd, HTPAGE *pg, const char *scriptUrl);

/* WTF see also jsstr.c extern: */
int     jd_checkstringforurl ARGS((JSString *str));

char   *jd_GetStringUtf8 ARGS((JSContext *cx, JSString *str, size_t *len));
void    jd_SetError(HTJD *jd);

extern CONST char       JdPlatform[];
extern JDOMIMPORTS      JdImports;
extern HTPAGE           *JdCurPage;             /* internal use */
extern const char       JdMsgPrefix[];

/* ---------------------------- jdwindow.c: ------------------------------- */

int     jd_InitWindowClass(HTJD *jd, HTWINDOW *window);

/* ------------------------------ jdbar.c: -------------------------------- */

extern JSClass          JD_Bar_Class;

int     jd_InitBarClass(HTJD *jd);

/* ------------------------------ jdnav.c: -------------------------------- */

int     jd_InitNavigatorClass(HTJD *jd, HTPAGE *pg);

/* ----------------------------- jdmime.c: -------------------------------- */

int     jd_InitMimeTypeClass(HTJD *jd, JSObject *navigator);

/* ------------------------------ jddoc.c: -------------------------------- */

int     jd_InitDocumentClass(HTJD *jd, HTPAGE *pg);

/* ------------------------------ jdloc.c: -------------------------------- */

extern JSClass  JD_Location_Class;

int     jd_InitLocationClass(HTJD *jd);

/* ------------------------------ jdlink.c: ------------------------------- */

int     jd_InitLinkClass(HTJD *jd);

/* ------------------------------ jdimage.c: ------------------------------ */

int     jd_InitImageClass(HTJD *jd);

/* ---------------------------- jdelement.c: ------------------------------ */

JSBool jd_Element_GetProperty ARGS((JSContext *cx, JSObject *obj, jsval id,
                                    jsval *vp));
JSBool jd_Element_SetProperty ARGS((JSContext *cx, JSObject *obj, jsval id,
                                    jsval *vp));

typedef enum ELEMENT_tag        /* properties; see also init */
{
  ELEMENT_TAGNAME,
  ELEMENT_NUM                   /* must be last */
}
ELEMENT;

#define ELEMENT_PROPERTIES_LIST \
{ "tagName",  ELEMENT_TAGNAME, JSPROP_ENUMERATE | JSPROP_READONLY, NULL, NULL}

#define ELEMENT_METHODS_LIST    \
I(getAttribute,         1)      \
I(setAttribute,         2)      \
I(removeAttribute,      1)      \
I(getAttributeNode,     1)      \
I(setAttributeNode,     1)      \
I(removeAttributeNode,  1)      \
I(getElementsByTagName, 1)

#undef I
#define I(func, nargs)  JSBool jd_Element_##func \
  ARGS((JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval));
ELEMENT_METHODS_LIST
#undef I

int     jd_InitElementClass(HTJD *jd);

/* ---------------------------- jdanchor.c: ------------------------------- */

int     jd_InitAnchorClass(HTJD *jd);

/* ----------------------------- jdform.c: -------------------------------- */

int     jd_InitFormClass(HTJD *jd);

/* ----------------------------- jdinput.c: ------------------------------- */

int     jd_InitInputClass(HTJD *jd);

/* ----------------------------- jdoption.c: ------------------------------ */

int     jd_InitOptionClass(HTJD *jd);

/* ----------------------------- jdevent.c: ------------------------------- */

int     jd_InitEventClass(HTJD *jd);

/* ----------------------------- jdscreen.c: ------------------------------ */

extern JSClass  JD_Screen_Class;

int     jd_InitScreenClass(HTJD *jd);

/* ----------------------------- jdhistory.c: ----------------------------- */

extern JSClass  JD_History_Class;

int     jd_InitHistoryClass(HTJD *jd);

/* ------------------------------- jdbody.c: ------------------------------ */

extern JSClass  JD_Body_Class;

int     jd_InitBodyClass(HTJD *jd);

/* -------------------------- proxyAutoConfig.c: -------------------------- */

int     TXjsPAC_InitClass(HTJD *jd);

/* undo free() -> jd_free() aliasing in jstypes.h: */
#undef malloc
#undef realloc
#undef free

#endif /* !JDOMI_H */
