#ifndef TXSCHED_H
#define TXSCHED_H


/**********************************************************************/
#define SCHED struct sched_struct
#define SCHEDPN (SCHED *)NULL
SCHED
{
   int hour, min;
   int mult;
   int day;
   int every;
   int unit;
   time_t initsleep;
   time_t eachsleep;
   time_t prev;
   int    verbose;
};
/**********************************************************************/
SCHED *closesched    ARGS((SCHED *sc));
SCHED *opensched     ARGS((char *s));
int    waitsched     ARGS((SCHED *sc));
time_t schedinit     ARGS((SCHED *sc));
time_t schedrepeat   ARGS((SCHED *sc));
int    schedverbose  ARGS((SCHED *sc,int verbose));
/**********************************************************************/
#endif                                                     /* TXSCHED_H */
