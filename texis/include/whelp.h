#ifndef WHELP_H
#define WHELP_H
/**********************************************************************
** Use: ~x for mode where x is one of (NBRFS)
**      N=normal, B=bold, R=reverse, F=flash(blink), S=standout
**      the starting mode is normal
**
**  or: ~Kx for a special key. x is one of (ATHMPNUDLR)
**      A=KF_ABORT, T=KF_TOP,   H=KF_HELP, M=KF_META,
**      P=KF_PPAGE, N=KF_NPAGE, U=KF_UP,   D=KF_DOWN,
**      L=KF_LEFT,  R=KF_RIGHT
**      it is up to the programmer to make sure the key will
**      fit on the line.
**        (longest key==ES_KEYLEN=="META-[....]"==11 chars)
**
**  or: ~P to force page pause after the current line
**
**  or: ~~ for tilde(~)
***********************************************************************/
#define HELPESC struct helpesc
HELPESC {
	char code;
	char *replace;
};
/**********************************************************************/
extern int whelp ARGS((HELP *fhlp,char *tag,WINDOW *w,int rows,int cols,char *title,int border,int help,int hlp,HELPESC *he));
/**********************************************************************/
#endif                                                     /* WHELP_H */
