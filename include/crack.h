/*
 * This program is copyright Alec Muffett 1991 except for some portions of
 * code in "crack-fcrypt.c" which are copyright Robert Baldwin, Icarus Sparry
 * and Alec Muffett.  The author(s) disclaims all responsibility or liability
 * with respect to it's usage or its effect upon hardware or computer
 * systems, and maintain copyright as set out in the "LICENCE" document which
 * accompanies distributions of Crack v4.0 and upwards.
 */

#include <stdio.h>
#include <ctype.h>
/*#include <pwd.h>*/
#include <signal.h>

#include "conf.h"

#define STRINGSIZE      256

#ifdef DEVELOPMENT_VERSION
#define BUILTIN_CLEAR
#undef BRAINDEAD6
#define CRACK_UNAME
#endif

extern void Trim ();
extern char *Reverse ();
extern char *Uppercase ();
extern char *Lowercase ();
extern char *Clone ();
extern char *Mangle ();

#ifdef FAST_TOCASE
#define CRACK_TOUPPER(x)        (toupper(x))
#define CRACK_TOLOWER(x)        (tolower(x))
#else
#define CRACK_TOUPPER(x)        (islower(x) ? toupper(x) : (x))
#define CRACK_TOLOWER(x)        (isupper(x) ? tolower(x) : (x))
#endif

#ifdef FCRYPT
#define crypt(a,b)              fcrypt(a,b)
#endif

#ifdef INDEX_NOT_STRCHR
#define strchr(a,b)             index(a,b)
#endif

/* include lyrics of "perfect circle" by R.E.M. at this point */

struct RULE
{
    struct RULE *next;
    char *rule;
};

#define STRCMP(x,y)             ( *(x) == *(y) ? strcmp((x),(y)) : -1 )

#include "crack-gl.h"
