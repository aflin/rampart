/*
 * $Log$
 * Revision 1.4  2013/07/29 18:50:47  kai
 * Renamed some PPM fields for clarity
 *
 * Revision 1.3  2013/06/27 21:49:56  kai
 * Struct field name change
 *
 * Revision 1.2  2008/08/21 03:33:28  kai
 * Private: Bug=2317, Enhancement, Metamorph, Low: Use textsearchmode in
 *   prefix/suffix processing
 *
 * Revision 1.1  2001/04/23 22:15:42  john
 * Initial revision
 *
 */
/**********************************************************************/
#include "stdio.h"
#ifdef MVS
#  if MVS_C_REL > 1
#     include <stddef.h>
#  else
#     include <stdefs.h>
#  endif
#  include <time.h>					/* for mm3e.h */
#endif
#include "sys/types.h"
#include "sizes.h"
#include "os.h"
#include "pm.h"
#include "cp.h"
#include "mdx.h"
#include "fnlist.h"                                     /* for mm3e.h */
#include "mm3e.h"
#include "mmsg.h"
#include "presuf.h"
#ifndef CPNULL
#  define CPNULL (char *)NULL
#endif

void
mmreport(ms)	 /* writes a report on the search */
MM3S *ms;
{
 int i,j;

 putmsg(MREPT,CPNULL,"sfx %d pfx %d rbld %d fltr %d istrtdlm %d ienddlm %d",
        ms->suffixproc,ms->prefixproc,ms->rebuild,ms->filter_mode,
        ms->incsd,ms->inced);

 putmsg(MREPT+1,CPNULL,"mnwrdln %d intrscts %d sdx \"%s\" edx \"%s\"",
        ms->minwordlen,ms->intersects,ms->sdexp,ms->edexp);

 for(i=0;i<ms->nels;i++)
    {
     putmsg(MREPT+2,CPNULL,CPNULL);
#if PM_NEWLANG                                        /* MAW 03-05-97 */
     putmsg(-1,CPNULL,"set %d:",i);
          if(ms->el[i]->pmtype==PMISSPM)
        putmsg(-1,CPNULL," morphproc=%d",ms->el[i]->ss->lang);
     else if(ms->el[i]->pmtype==PMISPPM)
     {
        putmsg(-1,CPNULL," morphproc=");
        for (j = 0; j < TX_PPM_NUM_STRS(ms->el[i]->ps); j++)
          putmsg(-1, CPNULL, "%s%d", (j == 0 ? "" : ","),
                 (int)TX_PPM_TERM_IS_LANGUAGE_QUERY(ms->el[i]->ps, j));
     }
     else
        putmsg(-1,CPNULL," morphproc=%d",ms->el[i]->lang);
     putmsg(-1,CPNULL," members=%03d ",ms->el[i]->lstsz);
#else
     putmsg(-1,CPNULL,"set %d: morphproc=%d members=%03d ",
            i,ms->el[i]->lang,ms->el[i]->lstsz);
#endif
     switch(ms->el[i]->pmtype)
	 {
	  case PMISSPM: putmsg(-1,CPNULL,"SPM ");break;
	  case PMISREX: putmsg(-1,CPNULL,"REX ");break;
	  case PMISPPM: putmsg(-1,CPNULL,"PPM ");break;
	  case PMISXPM: putmsg(-1,CPNULL,"XPM ");break;
	  case PMISNPM: putmsg(-1,CPNULL,"NPM ");break;
          default:      putmsg(-1, NULL, "? ");  break;
	 }
     switch(ms->el[i]->logic)
	 {
	  case LOGIAND: putmsg(-1,CPNULL,"AND LOGIC ;");break;
	  case LOGISET: putmsg(-1,CPNULL,"SET LOGIC ;");break;
	  case LOGINOT: putmsg(-1,CPNULL,"NOT LOGIC ;");break;
	  case LOGIPROP:putmsg(-1,CPNULL,"SETTING   ;");break;
	 }
     for(j=0;j<ms->el[i]->lstsz;j++)
	 putmsg(-1,CPNULL,"%s;",ms->el[i]->lst[j]);
     putmsg(-1,CPNULL,CPNULL);                             /* put EOL */
    }
 initsuffix((char **)ms->suffix, ms->textsearchmode);
 putmsg(MREPT+3,CPNULL,CPNULL);
 putmsg(-1,CPNULL,"suffixes=%03d :",ms->nsuf);
 for(i=0;i<ms->nsuf;i++)
    putmsg(-1,CPNULL,"%s;",ms->suffix[i]);
 putmsg(-1,CPNULL,CPNULL);                                 /* put EOL */
 initsuffix((char **)ms->suffix, ms->textsearchmode);
 putmsg(MREPT+4,CPNULL,CPNULL);
 putmsg(-1,CPNULL,"prefixes=%03d :",ms->npre);
 for(i=0;i<ms->npre;i++)
    putmsg(-1,CPNULL,"%s;",ms->prefix[i]);
 putmsg(-1,CPNULL,CPNULL);                                 /* put EOL */
}
