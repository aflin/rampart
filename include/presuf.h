#ifndef PRESUF_H
#define PRESUF_H
/**********************************************************************/
#define RM_INIT 0      /* initial setting for last arg to rm1suffix() */
extern int initprefix ARGS((char **list, int /* TXCFF */ textsearchmode));
extern int initsuffix ARGS((char **list, int /* TXCFF */ textsearchmode));
extern int prefcmpi   ARGS((char *a, char **bp, int /*TXCFF*/ textsearchmode));
#ifdef USEBINSEARCH
extern int PSbsrch    ARGS((char *,char **,int, int /*TXCFF*/ textsearchmode));
#endif
extern int prefsz     ARGS((char **,int,char **strp,int,int,
                            int /* TXCFF */ textsearchmode));
extern int rmprefix   ARGS((char **,char **,int,int,
                            int /* TXCFF */ textsearchmode));
extern void rmsuffix   ARGS((char **,char **,int,int,int,int phraseproc,
                             int /* TXCFF */ textsearchmode));
extern int rm1suffix  ARGS((char **,char **,int,int,int *,int,int phraseproc,
                            int /* TXCFF */ textsearchmode));
/**********************************************************************/
#endif                                                    /* PRESUF_H */
