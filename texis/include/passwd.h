#ifndef GETPASS_H
#define GETPASS_H

#ifndef HAVE_GETPASS_DECL
char	*getpass ARGS((char *));
#endif
char	*pw_encrypt ARGS((char *, char *));

#endif /* GETPASS_H */
