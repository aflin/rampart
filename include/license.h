#ifndef LICENSE_H
#define LICENSE_H

#ifndef HTTP_H
#  include "http.h"
#endif

#define licensecheck    initvars      /* security through obscurity */
int   licensecheck ARGS((char *what, char *vers, char *db, int argc,
                         char **argv, char *host, HTPAGE **pgp));
#define vx_printlicstats      mkshmorama
int   vx_printlicstats ARGS((int html, int tstone, char *remaddr));

#endif  /* LICENSE_H */
