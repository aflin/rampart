/* url.h	-- Henry Thompson
 */

#ifndef _URL_H
#define _URL_H

#ifdef NEVER
#include <stdio.h>
#include "rxp_stdio16.h"
#include "rxp_charset.h"
#endif

extern STD_API char8 * EXPRT 
    url_merge ARGS((CONST char8 *url, CONST char8 *base,
	      char8 **scheme, char8 **host, int *port, char8 **path));
extern STD_API FILE16 *url_open ARGS((CONST char8 *url, CONST char8 *base, 
			    CONST char8 *type, char8 **merged_url));
extern STD_API char8 *EXPRT default_base_url ARGS((void));

#endif
