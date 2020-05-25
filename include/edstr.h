#ifndef EDSTR_H
#define EDSTR_H
/***********************************************************************
** Header for the "edstr" library
** includes curses based routines for enhanced &| simplified
** keyboard and screen handling
**
** edstr reserves the symbol prefixes "ES_", "es_", and "KF_" to itself
** therefore your program should not use them
***********************************************************************/

/**********************************************************************
** @(#)es_edstr() - edit a string
**           use:
**               term_char=wedstr(w,s,expr,max_len,flags,hlp);
**
**               WINDOW *w;         -- window to read/write
**               char *s;           -- ptr to editing string
**               char *expr;        -- simple set expression of allowable chars
**                                     0 is never allowed!
**               int  max_len;      -- maximum screen length of string
**                                     actual len will never exceed this
**                                     but may be smaller because of ctrl chars
**               int  flags;        -- control flags
**               int  hlp           -- flags to es_getkey (for help)
**
**               int  term_char;    -- what key terminated entry
**                                     (-1) if param error
**
**
**           Assumes that the cursor points to the place where
**           the string is to be edited. The string need not be
**           be printed. If string is longer that max_len it will
**           be truncated on entry. Strings should not be longer
**           than a screen line or span across lines.
**
** @(#)es_setgfunc() - set get a key function for es_waitfor() & es_edstr()
**           use:
**               es_setgfunc(gfunc)
**
**               int  (*gfunc)(WINDOW *,int) -- function to call to get a key
**                                           -- called like es_getkey()
**                                           -- returns KF_* keys
**
** @(#)es_setifunc() - set insert mode indacator function
**           use:
**               es_setifunc(ifunc)
**
**               void (*ifunc)(int) -- function to call when insmode is toggled
**                                     or NULL for default.
**                                     called with 1/0/-1 for on/off/clear
***********************************************************************/

/**********************************************************************
** @(#)es_predstr() - print a string for es_edstr()
**                 use:
**                    stop=es_predstr(w,s,max_len,r,flags,l);
**
**                    WINDOW *w;   -- window to write
**                    char *s;     -- string to print
**                    int max_len; -- same as in es_edstr()
**                    int r;       -- attrib to use, -1 is default
**                    int flags    -- same as in es_edstr()
**                    int *l       -- displayed len of string
**
**                assumes caller has cursor already positioned
**                leaves cursor at end of string/padding
**                does no screen update if ES_REFRESH flag is not set
**                entire string must fit on one line. max_len will be
**                truncated to COLS-curcol.
**                stop==s on error
***********************************************************************/

/**********************************************************************
** @(#)es_waitfor() - wait for a single character
**                 use:
**
**                    key=es_waitfor(win,s,hlp)
**
**                    WINDOW *win; -- what window to read from
**                    char *s;     -- what character(s) to wait for
**                    int hlp;     -- passed to es_getkey()
**                    int key;     -- key that terminated func
**
**            s contains a list of characters, any one of which
**            will cause waitfor() to terminate.
**            example:  key=es_waitfor(stdscr,"YyNn",H_DEF);
**            returns character that terminated function
**            will return 0 immediately if s is empty.
**
**            if s is NULL then will accept anything
**            will always check for and return KF_ABORT if pressed
***********************************************************************/
#ifndef OS_H
stop./* you should include os.h before this  - and no i won't do it for you! */
#endif
#include "eskeys.h" /* internal use header - not used directly by you */

#if defined(bsd) || defined(__linux)
#if defined(__linux) || defined(__FreeBSD__) || defined(__bsdi__) || defined(__APPLE__)
#  define SGTTY struct termios
#endif
   extern SGTTY _TmP_tty;
/* KNG 20071126 GNU libc 2.3+ has nodelay() prototype: */
#  if !defined(__GLIBC__) || !defined(__GLIBC_MINOR__) || __GLIBC__ < 2 || __GLIBC_MINOR__ < 3
   extern int nodelay();
#  endif
   extern int beep();
#if !defined(__FreeBSD__) && !defined(__APPLE__)         /* it has these */
#  define resetterm() resetty()
#if defined(__linux) || defined(__bsdi__) || defined(__APPLE__)
#ifndef USELINUXOLDIOCTL
#  define saveterm()  ((void)tcgetattr(0,&_TmP_tty))
#  define fixterm()   ((void)tcsetattr(0,TCSADRAIN,&_TmP_tty))
#else
#  define saveterm()  ((void)tcgetattr(_tty_ch,&_TmP_tty))
#  define fixterm()   ((void)tcsetattr(_tty_ch,TCSADRAIN,&_TmP_tty))
#endif
#else
#  define saveterm()  ((void)gtty(_tty_ch,&_TmP_tty))
#  define fixterm()   ((void)stty(_tty_ch,&_TmP_tty))
#endif
#endif                                                 /* __FreeBSD__ */
#  define typeahead(a)
#  ifdef sparc/* MAW 10-02-91 - sunos has changed longname drastically */
#     define longname() ttytype
#  endif
#endif                                                         /* bsd */
#if defined(__linux) || defined(__bsdi__) || defined(__APPLE__)/* JMT 07-17-95 - linux has changed longname drastically */
#     define longname() ttytype
#endif
/**********************************************************************/
                             /* flags for es_edstr() and es_predstr() */
#define ES_REFRESH   0x001                        /* draw before edit */
#define ES_BEEP      0x002                           /* beep on error */
#define ES_SECURE    0x004         /* display all chars as _securechr */
#define ES_LOCKED    0x008                    /* disallow any editing */
#define ES_RET_FILL  0x010/* return when string is filled, or moved out of */
#define ES_REQUIRED  0x020             /* must have at least one byte */
#define ES_UC        0x040                         /* force uppercase */
#define ES_LC        0x080                         /* force lowercase */
#define ES_RETINVAL  0x100           /* return if invalid key pressed */
#define ES_GOEND     0x200          /* start editing at end of string */
/**********************************************************************/
                                              /* flags for curs_set() */
#define ES_CINVIS 0                               /* invisible cursor */
#define ES_CNORM  1                                  /* normal cursor */
#define ES_CVVIS  2                                   /* block cursor */
/**********************************************************************/
#define ES_SETKEY  1                         /* modes for es_setkey() */
#define ES_SETNAME 2
#define ES_SETFUNC 4
/**********************************************************************/
struct es_spkey {                                /* programmable keys */
   int meta;                                         /* meta key flag */
   int key;                                    /* actual key, 0==none */
   int ckey;                                      /* curses alias key */
   char *name;                                  /* string name of key */
   int (*func) ARGS((int,int));          /* function to call when hit */
           /* if((*func)(key,hlp)!=0) return(key); else get_next_key; */
};
struct es_skeyname {                          /* names of curses keys */
   int key;
   char name[7];                           /* must be <= ESKEYLEN-5+1 */
};
#define ES_KEYLEN 11                                 /* "Meta-[....]" */

          /* this list of KF_ keys MUST stay in order, add to the end */
#define KF_NONE    (-1)                   /* (-1) reserved for curses */
#define KF_META    (-2)                               /* command keys */
#define KF_EOL     (-3)             /* subtract from 0 and subtract 2 */
#define KF_UNDO    (-4)                   /* for index into es_keys[] */
#define KF_DOWN    (-5)
#define KF_UP      (-6)
#define KF_LEFT    (-7)
#define KF_RIGHT   (-8)
#define KF_HOME    (-9)
#define KF_BKSP    (-10)
#define KF_DELCH   (-11)
#define KF_INS     (-12)
#define KF_NPAGE   (-13)
#define KF_PPAGE   (-14)
#define KF_ENTER   (-15)
#define KF_END     (-16)
#define KF_HELP    (-17)
#define KF_TAB     (-18)
#define KF_BTAB    (-19)
#define KF_ABORT   (-20)
#define KF_REDRAW  (-21)
#define KF_CASE    (-22)
#define KF_ESC     (-23)
#define KF_TOP     (-24)
#define KF_USER0   (-25)
#define KF_USER(a) (KF_USER0-a)/* space for total of 10 user keys (0-9) */
#define KF_INVALID (-10000)          /* a key that cannot be returned */

#define NUM_KF    33                    /* number of valid "KF_" keys */
#define META_KEY  (es_keys[0].key)
 /* convert index into es_keys[] to "KF_" key definition & vice-versa */
#define KF_NUM(a) ((-(a))-2)                   /* these are the same, */
#define KF_NDX(a) (-((a)+2))     /* but both are already heavily used */
#define KF_KEY(a) (es_keys[KF_NUM(a)].key)
/**********************************************************************/
#ifndef EDSTR_C                        /* defined by edstr lib source */
       /* following 3 chars are expected to be 1 byte long on display */
extern int es_padchr;   /* default padding character for prompt lines */
extern int es_securechr;                /* default "secure" character */
extern int es_highbitchr;             /* default "high bit" character */
extern int es_insmode;                      /* insert/overstrike flag */
extern int es_curdelay;                  /* current nodelay() setting */
extern struct es_spkey es_keys[NUM_KF+1];        /* programmable keys */
extern struct es_skeyname es_names[];         /* names of curses keys */
#endif                                                     /* EDSTR_C */
/**********************************************************************/
#ifdef unix
   int es_flushinp ARGS((void));
   int es_getch    ARGS((WINDOW *));
	int es_keypad   ARGS((WINDOW *,int));
   int es_nodelay  ARGS((WINDOW *,int));
#else                                                        /* !unix */
#  define es_flushinp()   flushinp()
#  define es_getch(w)     wgetch(w)
#  define es_keypad(w,f)  keypad(w,f)
#  define es_nodelay(w,f) (es_curdelay=f,nodelay(w,f))
#endif                                                        /* unix */

#define es_drawkey(w,k) waddstr((w),es_key2str((k),(char *)NULL))
/**********************************************************************/
int    es_addch    ARGS((WINDOW *,int));
int    es_chrlen   ARGS((int,int));
int    es_edstr    ARGS((WINDOW *,char *,char *,int,int,int));
int  (*es_getgfunc ARGS((void)))ARGS((WINDOW *,int));
void (*es_getifunc ARGS((void)))ARGS((int));
int    es_getkey   ARGS((WINDOW *,int));
int    es_inikeys  ARGS((void));
void   es_ininames ARGS((void));
char  *es_key2str  ARGS((int,char *));
int    es_peekkey  ARGS((WINDOW *));
char  *es_predstr  ARGS((WINDOW *,char *,int,int,int,int *));
void   es_setgfunc ARGS((int (*)ARGS((WINDOW *,int))));
void   es_setifunc ARGS((void (*)ARGS((int))));
int    es_setkey   ARGS((int,int,int,char *,int (*)ARGS((int,int)),int));
void   es_setname  ARGS((int,char *));
int    es_waitfor  ARGS((WINDOW *,char *,int));
#if !defined(MSDOS) && !defined(BUTTON_RELEASED)/* DOS has it already */
                                 /* MAW 10-19-91 - svr4 curses has it */
#if !defined(__FreeBSD__) && !defined(__APPLE__) && __GNU_LIBRARY__ < 2      /* MAW 02-23-99 */
void   curs_set    ARGS((int));
#endif
#endif
/**********************************************************************/
#endif                                                     /* EDSTR_H */
