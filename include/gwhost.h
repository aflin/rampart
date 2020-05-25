#ifndef GWHOST_H
#define GWHOST_H


/**********************************************************************/
extern int    gwhstartup ARGS((void));
extern void   gwhcleanup ARGS((void));
extern void   gwhprinfo  ARGS((void));
extern int    addext     ARGS((char *ext));
extern int    delext     ARGS((char *ext));
extern int    listext    ARGS((void));
extern int    adddomain  ARGS((char *domain));
extern int    addnet     ARGS((char *net,char *mask,int deny));
extern int    addhost    ARGS((char *host,char *port));
extern int    addhallow  ARGS((char *host,char *port,char *path));
extern int    addhdeny   ARGS((char *host,char *port,char *path));
extern int    addhrdeny  ARGS((char *host,char *port,char *path));
extern int    addallow   ARGS((char *url));
extern int    adddeny    ARGS((char *url));
extern int    addexquery ARGS((char *expr));
extern int    addrdeny   ARGS((char *url));
extern SLIST *denylst    ARGS((void));
extern void   gwrrobots  ARGS((int f));
extern void   gwallowcgibin ARGS((int f));
extern void   gwallvalid    ARGS((int f));
extern void   gwhlookupall  ARGS((int onoff));
extern void   gwhlookupnone ARGS((int onoff));
extern void   gwhstripquery ARGS((int onoff));
extern int    saveallow  ARGS((char *name));
extern int    savedeny   ARGS((char *name));
extern int    savedenyexp ARGS((char *name));
extern int    saveexquery ARGS((char *name));
extern int    saveext    ARGS((char *name));
extern int    savedomain ARGS((char *name));
extern int    savenet    ARGS((char *name));
extern char  *fixurlhost ARGS((char *url));

extern char *ok_url    ARGS((int cpy,char *url));
#define okurl(a)    ok_url(0,a)
#define okcpyurl(a) ok_url(1,a)
/**********************************************************************/
#endif                                                    /* GWHOST_H */
