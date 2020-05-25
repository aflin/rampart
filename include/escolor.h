#ifndef ESCOLOR_H
#define ESCOLOR_H
/**********************************************************************/
#ifndef OS_H
#  include "os.h"
#endif
                                                /* setup colors, etc. */

extern WINDOW *es_curwin, *es_swin[10];/* up to user not to overflow */
extern int es_sx[10], es_sy[10], es_sndx;

#define wnoutfresh(w) wnoutrefresh(es_curwin=w)
#define wfresh(w)     wrefresh(es_curwin=w)
#define fresh()       wfresh(stdscr)
/* SunOs 4.0 can't refresh during nodelay() mode, Harris port 9/21/89 */
/* seems this is going to showup elsewhere too. do it for all. MAW 10/19/89 */
#ifdef MSDOS                        /* MAW 04-18-90 - not for DOS tho */
#  define nwfresh(w)  wfresh(w)
#  define ndoupdate() doupdate()
#else
#  define nwfresh(w) {int odelay=es_curdelay;if(odelay!=FALSE)es_nodelay(w,FALSE);wfresh(w);if(odelay!=FALSE)es_nodelay(w,odelay);}
#  define ndoupdate() {int odelay=es_curdelay;if(odelay!=FALSE)es_nodelay(stdscr,FALSE);doupdate();if(odelay!=FALSE)es_nodelay(stdscr,odelay);}
#endif


#if (!defined(bsd) && !defined(linux)) || defined(__FreeBSD__) || defined(__APPLE__)
#  ifdef unix
#     define C_NORM  A_NORMAL
#     define C_BOLD  A_BOLD
#     define C_REV   A_REVERSE
#     define C_STAND A_REVERSE
#     define C_BLINK (A_BOLD|A_BLINK)
#  else
#     ifdef MSDOS
#        define DEF_C_NORM  A_NORMAL
#        define DEF_C_BOLD  (A_BOLD|A_NORMAL)
#        define DEF_C_REV   A_REVERSE
#        define DEF_C_STAND A_REVERSE
#        define DEF_C_BLINK (A_BLINK|A_BOLD|A_NORMAL)
         extern int C_NORM;
         extern int C_BOLD;
         extern int C_REV;
         extern int C_STAND;
         extern int C_BLINK;
#     else
         stop.
#     endif
#  endif
#  define wnorm(w)   wattrset(w,C_NORM)
#  define wbold(w)   wattrset(w,C_BOLD)
#  define wrev(w)    wattrset(w,C_REV)
#  define wstand(w)  wattrset(w,C_STAND)
#  define wblink(w)  wattrset(w,C_BLINK)
#else                                                          /* bsd */
#  define C_NORM  0x0100    /* don't matter, just so C_NORM is diff */
#  define C_BOLD  0x0200        /* than others and low byte is free */
#  define C_REV   0x0300
#  define C_STAND 0x0400
#  define C_BLINK 0x0500
#  define wnorm(w)   wstandend(w)
#  define wbold(w)   wstandout(w)
#  define wrev(w)    wstandout(w)
#  define wstand(w)  wstandout(w)
#  define wblink(w)  wstandout(w)
#  define wattrset(w,a) (a==C_NORM?wstandend(w):wstandout(w))

#  define wnoutrefresh(a) wfresh(a)
#  define doupdate()
#  define idlok(w,f)
#endif                                                        /* !bsd */
#ifdef __convex__
#	define cbreak() crmode()
#	define nocbreak() nocrmode()
#endif
#ifndef MSDOS                   /* MAW 01-24-91 - they're vars on DOS */
#  ifndef ACS_HLINE
#    define ACS_HLINE '-'
#  endif
#  ifndef ACS_VLINE
#    define ACS_VLINE '|'
#  endif
#endif
/**********************************************************************/
#if defined(hpux) || defined(_AIX) || defined(__osf__)/* MAW - 07-16-93 */
/* may scroll any window if print to its bot right corner */
/* detect flavors here instead of all over the code (from 09-09-92) */
#  define SCROLLBUG1
#endif
/**********************************************************************/
WINDOW *es_savepos ARGS((void)), *es_restpos ARGS((void));
int es_open ARGS((void));
void es_close ARGS((void)), es_suspend ARGS((void)), es_continue ARGS((void));
void es_sttykeys ARGS((void)), es_rttykeys ARGS((void));
/**********************************************************************/
#endif                                                   /* ESCOLOR_H */
