#ifndef EPIPDFTEXT_H
#define EPIPDFTEXT_H
/**********************************************************************/
#ifndef SLIST
#  include "slist.h"
#endif
/* get the version string, this is my buffer, don't mess with it */
CONST char *get_epipdf_version(void);
/* set the xpdfrc filepath/name, i will keep reading your string, don't
   free it until after the last call to pdf functions */
int set_epipdf_configfile(CONST char *cfg);                 /* 0==ok */
int set_epipdf_debug(int debug);                   /* return prev val */
const char *set_epipdf_installdir(const char *installdir);   /* return prev val */
int set_epipdf_pretty(int pretty);                 /* return prev val */
int set_epipdf_expandligatures(int expand);        /* return prev val */
CONST char *set_epipdf_outputencoding(CONST char *encoding);   /* return prev val */
/* encodings: "Latin1","ASCII7","UTF-8","UCS-2", + whatever's in xpdfrc file */
int set_epipdf_errorprint(int errorprint);         /* return prev val */
/* convert a file to text on stdout */
TXEXIT do_epipdf_file(const char *path, TXbool linear, TXbool showLinks,
                      SLIST *meta, const char *pass, TXPF PrFlags,
                      HTBUF *metaHdrsBuf, HTBUF *bodyHdrsBuf,
                      HTBUF *bodyTextBuf);

/**********************************************************************/
#endif                                                /* EPIPDFTEXT_H */
