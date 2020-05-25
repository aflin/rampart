#ifndef MMPROT_H
#define MMPROT_H
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
/*#define USECNT*/                       /* include termcounting code */

extern int  eqprep    ARGS((void));
extern int  dotermcnt ARGS((char *,int));
extern int  chktcnt   ARGS((int));
extern void eqprepstr ARGS((char *s));
/**********************************************************************/
#define eqprot() eqprep()
/**********************************************************************/
#endif                                                    /* MMPROT_H */
