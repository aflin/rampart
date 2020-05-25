#ifndef _TXPWD_H
#define _TXPWD_H

struct passwd *gettxpwname ARGS((DDIC *, char *));
/* thread-safe version of gettxpwname():  KNG 20070723 */
int     TXgettxpwname_r ARGS((DDIC *ddic, CONST char *name,
                              struct passwd *pwbuf));
struct passwd *gettxpwuid ARGS((DDIC *, int));
int TXverifypasswd ARGS((CONST char *clear, CONST char *encrypt));

#endif /* _TXPWD_H */
