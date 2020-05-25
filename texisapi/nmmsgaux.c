#include "stdio.h"
#include "os.h"

static int locmsg=0;                          /* flag for epiputmsg() */
extern int epilocmsg ARGS((int f));
/**********************************************************************/
int
epilocmsg(f)
int f;
{
int prev=locmsg;

   if(f!=(-1)) locmsg=f;
   return(prev);
}                                                  /* end epilocmsg() */
/**********************************************************************/
