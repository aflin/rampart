#ifndef BUGOFF_H
#define BUGOFF_H
/***********************************************************************
** @(#)bugoff.h - MAW 01-29-91 - macros to help prevent debugging
** compile with -DPROTECT_IT to activate otherwise does nothing
** compile with -DNBUGOFF to disable these macros
**
** bugoff() will cause any subsequent break point or single step
**          interrupts to lock the machine
**          compile with -DBUGNICE to cause divide by zero interrupt
**          instead of lock
**
** bugon()  will undo bugoff()
**
** on unix SIGTRAP is used and will cause an exec(ls) then _exit()
***********************************************************************/
#ifdef PROTECT_IT
#ifdef MSDOS
   /* vector address is 4 times the vector (each is a 4 byte pointer) */
                                    /* single step vector address (1) */
static void FAR * FAR *bugstep=(void FAR * FAR *)4;
                                    /* break point vector address (3) */
static void FAR * FAR *bugbrkp=(void FAR * FAR *)12;

static void FAR *obugstep;  /* storage for old debug vector addresses */
static void FAR *obugbrkp;

/*
** stepping onto these macros will cause a crash because the
** vectors are MOVed one word at a time, which causes the vector
** to be invalid after the first half is MOVed but before the second
** half is MOVed
*/

                                                /* disallow debugging */
#ifdef BUGNICE
                                  /* division by 0 vector address (0) */
static void FAR * FAR *bugdiv0=(void FAR * FAR *)0;

#define bugoff() { \
   obugstep= *bugstep; \
   obugbrkp= *bugbrkp; \
   *bugstep= *bugdiv0; \
   *bugbrkp= *bugdiv0; \
}
#else                                                     /* !BUGNICE */
static void FAR bugstr(void){for(;(void FAR **)bugstr!= &obugstep;);}/* lock the machine */

#define bugoff() { \
   obugstep= *bugstep; \
   obugbrkp= *bugbrkp; \
   *bugstep= (void FAR *)bugstr; \
   *bugbrkp= (void FAR *)bugstr; \
}
#endif                                                     /* BUGNICE */

                                                 /* reallow debugging */
#define bugon() { \
   *bugstep=obugstep; \
   *bugbrkp=obugbrkp; \
}
/**********************************************************************/
#else                                                       /* !MSDOS */
/**********************************************************************/
#include <signal.h>
#ifdef SIGTRAP_never  /* MAW 07-13-93 - this doesn't protect anything */
static void (*bugstep)ARGS((int));
static void bugstr ARGS((int sigunused)){execlp("ls","ls",(char*)NULL);_exit(255);}/* kill the process */

#define bugoff() { \
   bugstep=signal(SIGTRAP,bugstr); \
}
#define bugon() { \
   signal(SIGTRAP,bugstep); \
}
#else                                                     /* !SIGTRAP */
#define bugon()
#define bugoff()
#endif                                                     /* SIGTRAP */
#endif                                                       /* MSDOS */
/**********************************************************************/
#ifdef NBUGOFF                    /* let me debug my own code please! */
#  undef bugoff
#  undef bugon
#  define bugon()
#  define bugoff()
#endif                                                     /* NBUGOFF */
/**********************************************************************/
#else                                                  /* !PROTECT_IT */
#define bugon()
#define bugoff()
#endif                                                  /* PROTECT_IT */
#endif                                                    /* BUGOFF_H */
