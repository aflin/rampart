#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include "dbquery.h"
#include "texint.h"
#include "parsetim.h"
#include "cgi.h"		/* for htsnpf() */


#ifndef TX_EVENTPN
#  define TX_EVENTPN    ((TX_EVENT *)NULL)
#endif

static CONST char	WhiteSpace[] = " \t\r\n\v\f";
static CONST char	SpecTooLong[] = "Specification too long";
static CONST char	ExpPos[] = "Expected positive integer for interval";
static CONST char	CantMixPosDay[] = "Cannot mix weekday(s) and day number(s)";
static CONST char * CONST	WeekDayNames[7] =
{
  "SUNDAY",
  "MONDAY",
  "TUESDAY",
  "WEDNESDAY",
  "THURSDAY",
  "FRIDAY",
  "SATURDAY",
};

/******************************************************************/

typedef unsigned char BOOLEAN;

TX_EVENT *TXcloseevent(TX_EVENT *);

typedef struct TIMELIST
{
	int hour;
	int minute;
	int last;
	struct TIMELIST *next;
}
TIMELIST;

typedef struct MONTHLIST
{
	int month;
	int last;
	TIMELIST *timelist;
	struct MONTHLIST *next;
}
MONTHLIST;

#define YEARDAY		1
#define MONTHDAY	2
#define MONTHNUM	3

typedef struct DAYTIME
{
	BOOLEAN days[7];
	BOOLEAN last[7];
	TIMELIST *timelist;
	struct DAYTIME *next;
}
DAYTIME;

typedef struct OCCURRENCE
{
	BOOLEAN fromb[5];
	BOOLEAN frome[5];
	BOOLEAN lastb[5];
	BOOLEAN laste[5];
	BOOLEAN days[7];
	BOOLEAN lastd[7];
	TIMELIST *timelist;
	struct OCCURRENCE *next;
}
OCCURRENCE;

static CONST char CantAlloc[] = "Can't alloc %u bytes of memory: %s";

#define MEMERR(fn, n)	\
  putmsg(MERR + MAE, fn, (char *)CantAlloc, (unsigned)(n), strerror(errno))
#define ALLOCTYPE(fn, var, type)				\
  errno = 0;  							\
  ialloced++;							\
  if ((var = (type *)calloc(1, sizeof(type))) == (type *)NULL)  \
    {							    	\
      MEMERR(fn, sizeof(type));					\
      return((type *)NULL);					\
    }

/******************************************************************/

static int
monthday(int monthday, struct tm *tm)
{
	struct tm mytm;
	int thismonth;

	mytm = *tm;
	mytm.tm_isdst = -1;
	mktime(&mytm);
	thismonth = mytm.tm_mon;
	if (monthday > 0)
	{
		mytm.tm_mday = monthday;
	}
	if (monthday < 0)
	{
		mytm.tm_mon++;
		mytm.tm_mday = 1 + monthday;
	}
	if (monthday == 0)
	{
		return -1;
	}
	mytm.tm_isdst = -1;
	mktime(&mytm);
	if (mytm.tm_mon == thismonth)
	{
		return mytm.tm_mday;
	}
	else
	{
		return -1;
	}
}

/******************************************************************/

static TIMELIST *
addtotlist(TIMELIST * head, TIMELIST * newevent)
{
	TIMELIST *tev;

	if (NULL == head)
		return newevent;
	for (tev = head; tev->next; tev = tev->next);
	tev->next = newevent;
	return head;
}

/******************************************************************/

static MONTHLIST *
addtomlist(MONTHLIST * head, MONTHLIST * newevent)
{
	MONTHLIST *tev;

	if (NULL == head)
		return newevent;
	for (tev = head; tev->next; tev = tev->next);
	tev->next = newevent;
	return head;
}

/******************************************************************/

static DAYTIME *
addtodlist(DAYTIME * head, DAYTIME * newevent)
{
	DAYTIME *tev;

	if (NULL == head)
		return newevent;
	for (tev = head; tev->next; tev = tev->next);
	tev->next = newevent;
	return head;
}

/******************************************************************/

static TIMELIST *
closetimelist(TIMELIST * head)
{
  TIMELIST      *next;

  for ( ; head; head = next)
    {
      next = head->next;
      free(head);
    }
  return NULL;
}

/******************************************************************/

static MONTHLIST *
closemonthlist(MONTHLIST * head)
{
  MONTHLIST     *next;

  for ( ; head; head = next)
    {
      next = head->next;
      free(head);
    }
  return NULL;
}

/******************************************************************/

static DAYTIME *
closedaytime(DAYTIME * head)
{
  DAYTIME       *next;

  for ( ; head; head = next)
    {
      next = head->next;
      head->timelist = closetimelist(head->timelist);
      free(head);
    }
  return NULL;
}

/******************************************************************/

static TIMELIST *
timelist(char *rule, time_t start, time_t end, int skip, time_t max,
	 char **nrule, int *validtime)
{
	static CONST char fn[] = "timelist";
	struct tm *tm;
	TIMELIST *rc = NULL, *trc;
	char *cp = rule, *ncp;
	long hourmin;
	int isvalid = 0;
	int ialloced = 0;

        (void)end;
        (void)skip;
        (void)max;
	while (isspace(*cp))
		cp++;
	tm = localtime(&start);
	if (!isdigit(*cp))
	{
		ALLOCTYPE(fn, rc, TIMELIST);
		rc->hour = tm->tm_hour;
		rc->minute = tm->tm_min;
		rc->last = 0;
		rc->next = NULL;
		*nrule = cp;
		if (validtime)
			*validtime = 1;
		return rc;
	}
	else
		while (isdigit(*cp))
		{
			ALLOCTYPE(fn, trc, TIMELIST);
			hourmin = strtol(cp, &ncp, 10);
			if (ncp != (cp + 4))
			{
				break;
			}
			trc->hour = hourmin / 100;
			trc->minute = hourmin - (trc->hour * 100);
			if ((trc->hour == tm->tm_hour) &&
			    (trc->minute == tm->tm_min))
			{
				isvalid++;
			}
			if ('$' == *ncp)
			{
				trc->last = 1;
				ncp++;
			}
			else
				trc->last = 0;
			rc = addtotlist(rc, trc);
			while (isspace(*ncp))
				ncp++;
			cp = ncp;
		}
	*nrule = cp;
	if (isvalid)
	{
		if (validtime)
			*validtime = isvalid;
		return rc;
	}
	else
	{
		if (validtime)
		{
			*validtime = isvalid;
			return rc;
		}
		else
			return closetimelist(rc);
	}
}

/******************************************************************/

static MONTHLIST *
daylist(char *rule, time_t start, time_t end, int skip, time_t max,
	char **nrule, int *validtime, int type)
{
	static CONST char fn[] = "daylist";
	struct tm *tm;
	MONTHLIST *rc = NULL, *trc;
	char *cp = rule, *ncp;
	int isvalid = 0;
	int current = 0;
	int ialloced = 0;

        (void)end;
        (void)skip;
        (void)max;
	while (isspace(*cp))
		cp++;
	tm = localtime(&start);
	switch (type)
	{
	case MONTHNUM:
		current = tm->tm_mon + 1;
		break;
	case MONTHDAY:
		current = tm->tm_mday;
		break;
	case YEARDAY:
		current = tm->tm_yday + 1;
		break;
	}
	if (!isdigit(*cp) && (type != MONTHDAY || strncmp(cp, "LD", 2)))
	{
		ALLOCTYPE(fn, rc, MONTHLIST);
		rc->month = current;
		rc->last = 0;
		rc->next = NULL;
		*nrule = cp;
		if (validtime)
			*validtime = 1;
		return rc;
	}
	else
	{
		while (isdigit(*cp) || 0 == strncmp(cp, "LD", 2))
		{
			ALLOCTYPE(fn, trc, MONTHLIST);
			if (0 == strncmp(cp, "LD", 2))
			{
				trc->month = -1;
				ncp = cp + 2;
			}
			else
			{
				trc->month = strtol(cp, &ncp, 10);
			}
			if (type == MONTHDAY && *ncp == '-')
			{
				trc->month = -trc->month;
				tm->tm_mon++;
				tm->tm_mday = 1 + trc->month;
				mktime(tm);
				if (tm->tm_mday == current)
					isvalid++;
				ncp++;
			}
			if (trc->month == current)
			{
				isvalid++;
			}
			if ('$' == *ncp)
			{
				trc->last = 1;
				ncp++;
			}
			else
				trc->last = 0;
			rc = addtomlist(rc, trc);
			while (isspace(*ncp))
				ncp++;
			cp = ncp;
		}
	}
	*nrule = cp;
	if (isvalid)
	{
		if (validtime)
			*validtime = isvalid;
		return rc;
	}
	else
	{
		if (validtime)
		{
			*validtime = isvalid;
			return rc;
		}
		else
			return closemonthlist(rc);
	}
}

/******************************************************************/

static CONST char * CONST dayabbrev[] = { "SU", "MO", "TU", "WE", "TH", "FR", "SA", NULL };

static DAYTIME *
daytime(char *rule, time_t start, time_t end, int skip, time_t max,
	char **nrule, int *validtime)
{
	static CONST char fn[] = "daytime";
	struct tm *tm;
	DAYTIME *rc = NULL, *trc;
	char *cp = rule, *ncp;
	int isvalid = 0;
	int dayno, i;
	int timevalid, dayvalid;
	int ialloced = 0;

	while (isspace(*cp))
		cp++;
	tm = localtime(&start);
	if (!isalpha(*cp))
	{
		ALLOCTYPE(fn, rc, DAYTIME);
		rc->days[tm->tm_wday] = 1;
		rc->next = NULL;
		rc->timelist =
			timelist("", start, end, skip, max, &ncp, NULL);
		*nrule = cp;
		return rc;
	}
	else
		while (isalpha(*cp))
		{
			ALLOCTYPE(fn, trc, DAYTIME);
			dayvalid = 0;
			do
			{
				dayno = -1;
				for (i = 0; i < 7; i++)
				{
					if (0 == strncmp(cp, dayabbrev[i], 2))
					{
						dayno = i;
						if (dayno == tm->tm_wday)
						{
							dayvalid = 1;
						}
						trc->days[i] = 1;
						cp += 2;
						if ('$' == cp[0])
						{
							trc->last[i] = 1;
							cp++;
						}
						break;
					}
				}
				if (-1 == dayno)
				{
					free(trc);
					return closedaytime(rc);
				}
				while (isspace(*cp))
					cp++;
			}
			while (isalpha(*cp));
			trc->timelist =
				timelist(cp, start, end, skip, max, &ncp,
					 &timevalid);
			if (dayvalid && timevalid)
				isvalid = 1;
			cp = ncp;
			while (isspace(*cp))
				cp++;
			rc = addtodlist(rc, trc);
		}
	*nrule = cp;
	if (isvalid)
	{
		if (validtime)
			*validtime = isvalid;
		return rc;
	}
	else
	{
		if (validtime)
		{
			*validtime = isvalid;
			return rc;
		}
		else
			return closedaytime(rc);
	}
}

/******************************************************************/

static OCCURRENCE *
closeocc(OCCURRENCE * occur)
{
	if (occur)
	{
		closeocc(occur->next);
		closetimelist(occur->timelist);
		free(occur);
	}
	return NULL;
}

/******************************************************************/

static OCCURRENCE *
occur(char *rule, time_t start, time_t end, int skip, time_t max,
      char **nrule, int *isvalid)
{
	static CONST char fn[] = "occur";
	OCCURRENCE *rc;
	struct tm tm;
	char *x, *cp = rule, *ncp;
	int occurrence, i;
	int weekday, fromb, frome;
	int ialloced = 0;

	while (isspace(*cp))
		cp++;
	*nrule = cp;

	tm = *localtime(&start);
	fromb = tm.tm_mday / 7;
	frome = (monthday(-1, &tm) - (tm.tm_mday)) / 7;
	weekday = tm.tm_wday;
	ALLOCTYPE(fn, rc, OCCURRENCE);
	if (!isdigit(*cp))
	{
		rc->days[weekday] = 1;
		rc->fromb[fromb] = 1;
		rc->timelist =
			timelist("", start, end, skip, max, &x, isvalid);
		return rc;
	}
	else
		while (isdigit(*cp))
		{
			occurrence = strtol(cp, &ncp, 10);
			if ((1 > occurrence) || (occurrence > 5))
			{
				return closeocc(rc);
			}
			switch (*ncp)
			{
			case '+':
				rc->fromb[occurrence - 1] = 1;
				if ('$' == ncp[1])
				{
					rc->lastb[occurrence - 1] = 1;
					ncp++;
				}
				break;
			case '-':
				rc->frome[occurrence - 1] = 1;
				if ('$' == ncp[1])
				{
					rc->laste[occurrence - 1] = 1;
					ncp++;
				}
				break;
			default:
				return closeocc(rc);
			}
			ncp++;
			cp = ncp;
			while (isspace(*cp))
				cp++;
		}
	while (isalpha(*cp))
	{
		for (i = 0; i < 7; i++)
		{
			if (0 == strncmp(cp, dayabbrev[i], 2))
			{
				if (isvalid && i == weekday &&
				    (rc->fromb[fromb] || rc->frome[frome]))
					*isvalid = 1;
				rc->days[i] = 1;
				cp += 2;
				if ('$' == cp[0])
				{
					rc->lastd[i] = 1;
					cp++;
				}
				break;
			}
		}
		while (isspace(*cp))
			cp++;
	}
	*nrule = cp;
	if (isdigit(*cp) && ('+' == cp[1] || '-' == cp[1]))
		rc->next = occur(cp, start, end, skip, max, nrule, isvalid);
	return rc;
}

/******************************************************************/

static MONTHLIST *
newmlist(int dayinmonth)
{
	static CONST char fn[] = "newmlist";
	MONTHLIST *rc;
	int ialloced = 0;

	ALLOCTYPE(fn, rc, MONTHLIST);
	if (rc)
	{
		rc->month = dayinmonth;
		rc->last = 0;
		rc->next = NULL;
	}
	return rc;
}

/******************************************************************/

static MONTHLIST *
occtodlist(OCCURRENCE * occ, struct tm *itm)
{
	MONTHLIST *rc = NULL;
	struct tm tm;
	int daysinmonth, dayinmonth;
	int weekday, lastweekday;
	int fromb = 0;
	int frome = 0;

	tm = *itm;
	if (!occ)
		return NULL;
	tm.tm_mon++;
	tm.tm_mday = 0;
	tm.tm_isdst = -1;
	mktime(&tm);
	daysinmonth = tm.tm_mday;
	lastweekday = tm.tm_wday;
	tm.tm_mday = 1;
	tm.tm_isdst = -1;
	mktime(&tm);
	weekday = tm.tm_wday;
	if (weekday >= 7)
		weekday -= 7;
	for (dayinmonth = 0; dayinmonth < daysinmonth; dayinmonth++)
	{
		fromb = dayinmonth / 7;
		frome = (daysinmonth - (dayinmonth + 1)) / 7;
		if (occ->days[weekday]
		    && (occ->fromb[fromb] || occ->frome[frome]))
			rc = addtomlist(rc, newmlist(dayinmonth + 1));
		weekday++;
		if (weekday >= 7)
			weekday -= 7;
	}
	rc = addtomlist(rc, occtodlist(occ->next, itm));
	return rc;
}

/******************************************************************/

static int
duration(char *rule, char **nrule)
{
	char *cp = rule;

	while (isspace(*cp))
		cp++;
	if (*cp != '#')
		return 2;
	cp++;
	return strtol(cp, nrule, 10);
}

/******************************************************************/

static TX_EVENT *
addtotxevent(TX_EVENT * head, TX_EVENT * newevent)
{
	TX_EVENT *tev;

	if (NULL == head)
		return newevent;
	for (tev = head; tev->next; tev = tev->next)
		tev->count = tev->count + newevent->count;
	tev->count = tev->count + newevent->count;
	tev->next = newevent;
	return head;
}

/******************************************************************/

static TX_EVENT *
minuteop(char *rule, time_t start, time_t end, int skip, time_t min,
	 time_t max, int *maxocc, int subrule)
{
	static CONST char fn[] = "minuteop";
	char *cp = rule, *ncp = NULL;
	TX_EVENT *rc = NULL, *trc = NULL;
	long nmins, repetitions, i;
	struct tm tm;
	time_t cstart;
	int ialloced = 0;

        (void)end;
        (void)skip;
        (void)subrule;
	if (*cp != 'M')
		return rc;
	cp++;
	if (!isdigit(*cp))
		return rc;
	nmins = strtol(cp, &ncp, 10);
	cp = ncp;
	repetitions = duration(cp, &ncp);
	cp = ncp;
	while (isspace(*cp))
		cp++;
	tm = *localtime(&start);
	for (i = 0; repetitions == 0 || i < repetitions; i++)
	{
		tm.tm_isdst = -1;
		cstart = mktime(&tm);
		if (cstart == -1)
			break;
		ALLOCTYPE(fn, trc, TX_EVENT);
		trc->when = cstart;
		trc->count = 1;
		trc->next = NULL;
		if (trc->when > max)
		{
			trc = TXfree(trc);
			break;
		}
		if (trc->when >= min)
		{
			if (maxocc)
			{
				*maxocc = *maxocc - 1;
				if (*maxocc < 0)
				{
					free(trc);
					break;
				}
			}
			rc = addtotxevent(rc, trc);
		}
		else
			free(trc);
		tm.tm_min += nmins;
	}
	return rc;
}

/******************************************************************/

static TX_EVENT *
daily(char *rule, time_t start, time_t end, int skip, time_t min, time_t max,
      int *maxocc, int subrule, int exact)
{
	static CONST char fn[] = "daily";
	char *cp = rule, *ncp;
	TX_EVENT *rc = NULL, *trc = NULL;
	TIMELIST *tlist = NULL, *curtime;
	long ndays, repetitions, i;
	struct tm tm;
	time_t cstart;
	int breaknow = 0, firstpass = 1;
	int wasvalid;
	int ialloced = 0;

        (void)subrule;
	if (*cp != 'D') goto end;
	cp++;
	if (!isdigit(*cp)) goto end;
	ndays = strtol(cp, &ncp, 10);
	cp = ncp;
	tlist = timelist(cp, start, end, skip, max, &ncp, &wasvalid);
	if (exact && (0 == wasvalid)) goto end;
	cp = ncp;
	repetitions = duration(cp, &ncp);
	cp = ncp;
	while (isspace(*cp))
		cp++;
	tm = *localtime(&start);
	for (i = 0; repetitions == 0 || i < repetitions; i++)
	{
		for (curtime = tlist; curtime; curtime = curtime->next)
		{
			tm.tm_hour = curtime->hour;
			tm.tm_min = curtime->minute;
			tm.tm_sec = 0;
			tm.tm_isdst = -1;
			cstart = mktime(&tm);
			if (cstart == -1)
				break;
			trc = TXcloseevent(trc);
			if (*cp == 'M')
			{
				if (!curtime->last || (i < repetitions - 1))
				{
					trc =
						minuteop(cp, cstart, end,
							 skip, min, max,
							 maxocc,
							 firstpass ? 0 : 1);
					if (!trc)
					  {
						if(exact)
							goto end;
						else
							goto nextday;
					  }
				}
			}
			if (NULL == trc)
			{
				if (maxocc && *maxocc < 0)
					break;
				ALLOCTYPE(fn, trc, TX_EVENT);
				trc->when = cstart;
				trc->count = 1;
				trc->next = NULL;
			}
			if (trc->when > max)
			{
				breaknow = 1;
				break;
			}
			firstpass = 0;
			if (trc->when >= min)
			{
				rc = addtotxevent(rc, trc);
				trc = NULL;
				if (maxocc && ialloced)
					*maxocc = *maxocc - 1;
			}
			if ((i == (repetitions - 1) && curtime->last) ||
			    (maxocc && *maxocc <= 0))
			{
				breaknow = 1;
				break;
			}
		}
nextday:
		tm.tm_mday += ndays;
		if (breaknow)
			break;
	}
      end:
	TXcloseevent(trc);
	closetimelist(tlist);
	return rc;
}

/******************************************************************/

static TX_EVENT *
weekly(char *rule, time_t start, time_t end, int skip, time_t min, time_t max,
       int *maxocc, int subrule, int exact)
{
	static CONST char fn[] = "weekly";
	char *cp = rule, *ncp;
	TX_EVENT *rc = NULL, *trc = NULL;
	DAYTIME *dlist = NULL, *curdaytime;
	TIMELIST *curtime;
	long ndays, nweeks, repetitions, i, weekday, delta;
	struct tm tm;
	time_t cstart;
	int breaknow = 0, firstpass = 1;
	int wasvalid = 0;
	int ialloced = 0;

	if (*cp != 'W') goto end;
	cp++;
	if (!isdigit(*cp)) goto end;
	nweeks = strtol(cp, &ncp, 10);
	ndays = nweeks * 7;
	cp = ncp;
	dlist = daytime(cp, start, end, skip, max, &ncp, &wasvalid);
	if ((exact && !subrule) && 0 == wasvalid) goto end;
	cp = ncp;
	repetitions = duration(cp, &ncp);
	cp = ncp;
	while (isspace(*cp))
		cp++;
	tm = *localtime(&start);
	for (i = 0; repetitions == 0 || i < repetitions; i++)
	{
		for (curdaytime = dlist; curdaytime;
		     curdaytime = curdaytime->next)
		{
			for (weekday = 0; weekday < 7; weekday++)
			{
				if (curdaytime->days[weekday] &&
				    (subrule || (i > 0)
				     || (weekday >= tm.tm_wday)))
				{
					cstart = mktime(&tm);
					if (cstart == -1)
					{
						breaknow = 1;
						break;
					}
					delta = weekday - tm.tm_wday;
					if (delta > 0 || i > 0)
					{
						tm.tm_mday += delta;
					}
					else if (subrule && delta < 0)
					{
						tm.tm_mday += (7 + delta);
					}
					for (curtime = curdaytime->timelist;
					     curtime; curtime = curtime->next)
					{
						tm.tm_hour = curtime->hour;
						tm.tm_min = curtime->minute;
						tm.tm_sec = 0;
						tm.tm_isdst = -1;
						cstart = mktime(&tm);
						if (cstart == -1)
						{
							breaknow = 1;
							break;
						}
						trc = TXcloseevent(trc);
						if (*cp == 'M')
						{
							if (i <
							    (repetitions - 1)
							    || 0 ==
							    curtime->last)
								trc =
									minuteop
									(cp,
									 cstart,
									 end,
									 skip,
									 min,
									 max,
									 maxocc,
									 firstpass
									 ? 0 :
									 1);
						}
						if (NULL == trc)
						{
							if (maxocc
							    && *maxocc < 0)
								break;
							ALLOCTYPE(fn, trc,
								  TX_EVENT);
							trc->when = cstart;
							trc->count = 1;
							trc->next = NULL;
						}
						if (trc->when > max)
						{
							breaknow = 1;
							break;
						}
						firstpass = 0;
						if (trc->when >= min)
						{
							rc =
								addtotxevent
								(rc, trc);
							trc = NULL;
							if (maxocc && ialloced)
								*maxocc =
									*maxocc
									- 1;
						}
						if ((i == (repetitions - 1)
						     && curtime->last) ||
						    (maxocc && *maxocc <= 0))
						{
							breaknow = 1;
							break;
						}
					}
				}
				if (breaknow || (i == (repetitions - 1)
						 &&
						 curdaytime->last[weekday]))
				{
					breaknow = 1;
					break;
				}
			}
			if (breaknow)
				break;
		}
		tm.tm_mday += ndays;
		if (breaknow)
			break;
	}
end:
	closedaytime(dlist);
	TXcloseevent(trc);
	return rc;
}

/******************************************************************/

static TX_EVENT *
monthlybyday(char *rule, time_t start, time_t end, int skip, time_t min,
	     time_t max, int *maxocc, int subrule, int exact)
{
	static CONST char fn[] = "monthlybyday";
	char *cp = rule, *ncp;
	TX_EVENT *rc = NULL, *trc = NULL;
	MONTHLIST *dlist = NULL, *curday;
	long nmonths, repetitions, i;
	struct tm tm;
	time_t cstart;
	int breaknow = 0, dayofmonth, validdays = 0, firstpass = 1;
	int ialloced = 0;

	if (strncmp(cp, "MD", 2) != 0) goto end;
	cp += 2;
	if (!isdigit(*cp)) goto end;
	nmonths = strtol(cp, &ncp, 10);
	cp = ncp;
	dlist =
		daylist(cp, start, end, skip, max, &ncp, &validdays,
			MONTHDAY);
	if (0 == validdays && 0 == subrule && exact) goto end;
	cp = ncp;
	repetitions = duration(cp, &ncp);
	cp = ncp;
	while (isspace(*cp))
		cp++;
	tm = *localtime(&start);
	for (i = 0; repetitions == 0 || i < repetitions; i++)
	{
		for (curday = dlist; curday; curday = curday->next)
		{
			tm.tm_sec = 0;
			tm.tm_isdst = -1;
			dayofmonth = monthday(curday->month, &tm);
			if (dayofmonth == -1)
			{
				breaknow = 1;
				break;
			}
			if (i == 0 && (dayofmonth < tm.tm_mday))
			{
				continue;
			}
			if (i > 0 || (dayofmonth > tm.tm_mday))
			{
				tm.tm_mday = dayofmonth;
			}
			cstart = mktime(&tm);
			if (cstart == -1)
			{
				breaknow = 1;
				break;
			}
			trc = TXcloseevent(trc);
			if (*cp == 'D')
			{
				trc =
					daily(cp, cstart, end, skip, min, max,
					      maxocc,
					      firstpass ? 0 : 1, exact);
				if (!trc)
					goto end;
			}
			if (*cp == 'M')
			{
				trc =
					minuteop(cp, cstart, end, skip, min,
						 max, maxocc,
						 firstpass ? 0 : 1);
				if (!trc)
					goto end;
			}
			if (*cp == 'W')
			{
				trc =
					weekly(cp, cstart, end, skip, min,
					       max, maxocc, firstpass ? 0 : 1,
					       exact);
				if (!trc)
					goto end;
			}
			if (NULL == trc)
			{
				if (maxocc && *maxocc < 0)
					break;
				ALLOCTYPE(fn, trc, TX_EVENT);
				trc->when = cstart;
				trc->count = 1;
				trc->next = NULL;
			}
			if (trc->when > max)
			{
				breaknow = 1;
				break;
			}
			firstpass = 0;
			if (trc->when >= min)
			{
				rc = addtotxevent(rc, trc);
				trc = NULL;
				if (maxocc && ialloced) *maxocc = *maxocc - 1;
			}
			if ((i == (repetitions - 1) && curday->last) ||
			    (maxocc && *maxocc <= 0))
			{
				breaknow = 1;
				break;
			}
		}
		tm.tm_mon += nmonths;
		tm.tm_mday = 1;
		if (0 != breaknow)
			break;
	}
      end:
	closemonthlist(dlist);
	TXcloseevent(trc);
	return rc;
}

/******************************************************************/

static TX_EVENT *
monthlybypos(char *rule, time_t start, time_t end, int skip, time_t min,
	     time_t max, int *maxocc, int subrule, int exact)
{
	static CONST char fn[] = "monthlybypos";
	char *cp = rule, *ncp;
	TX_EVENT *rc = NULL, *trc = NULL;
	OCCURRENCE *occ = NULL;
	MONTHLIST *dlist, *curday;
	long nmonths, repetitions, i;
	struct tm tm;
	time_t cstart;
	int breaknow = 0, dayofmonth, validdays = 0, firstpass = 1;
	int ialloced = 0;

	if (strncmp(cp, "MP", 2) != 0) goto end;
	cp += 2;
	if (!isdigit(*cp)) goto end;
	nmonths = strtol(cp, &ncp, 10);
	cp = ncp;
	occ = occur(cp, start, end, skip, max, &ncp, &validdays);
	if (0 == validdays && 0 == subrule && exact) goto end;
	cp = ncp;
	repetitions = duration(cp, &ncp);
	cp = ncp;
	while (isspace(*cp))
		cp++;
	tm = *localtime(&start);
	for (i = 0; repetitions == 0 || i < repetitions; i++)
	{
		dlist = occtodlist(occ, &tm);
		for (curday = dlist; curday; curday = curday->next)
		{
			tm.tm_sec = 0;
			tm.tm_isdst = -1;
			dayofmonth = monthday(curday->month, &tm);
			if (dayofmonth == -1)
			{
				breaknow = 1;
				break;
			}
			if (i == 0 && (dayofmonth < tm.tm_mday))
			{
				continue;
			}
			if (i > 0 || (dayofmonth > tm.tm_mday))
			{
				tm.tm_mday = dayofmonth;
			}
			cstart = mktime(&tm);
			if (cstart == -1)
			{
				breaknow = 1;
				break;
			}
			trc = TXcloseevent(trc);
			if (*cp == 'D')
			{
				trc =
					daily(cp, cstart, end, skip, min, max,
					      maxocc, firstpass ? 0 : 1,
					      exact);
				if (!trc)
					goto end;
			}
			if (*cp == 'M')
			{
				trc =
					minuteop(cp, cstart, end, skip, min,
						 max, maxocc,
						 firstpass ? 0 : 1);
				if (!trc)
					goto end;
			}
			if (*cp == 'W')
			{
				trc =
					weekly(cp, cstart, end, skip, min,
					       max, maxocc, firstpass ? 0 : 1,
					       exact);
				if (!trc)
					goto end;
			}
			if (NULL == trc)
			{
				if (maxocc && *maxocc < 0)
					break;
				ALLOCTYPE(fn, trc, TX_EVENT);
				trc->when = cstart;
				trc->count = 1;
				trc->next = NULL;
			}
			if (trc->when > max)
			{
				breaknow = 1;
				break;
			}
			firstpass = 0;
			if (trc->when >= min)
			{
				rc = addtotxevent(rc, trc);
				trc = NULL;
				if (maxocc && ialloced)
					*maxocc = *maxocc - 1;
			}
			if ((i == (repetitions - 1) && curday->last) ||
			    (maxocc && *maxocc <= 0))
			{
				breaknow = 1;
				break;
			}
		}
		dlist = closemonthlist(dlist);
		tm.tm_mon += nmonths;
		tm.tm_mday = 1;
		if (0 != breaknow)
			break;
	}
      end:
	closeocc(occ);
	TXcloseevent(trc);
	return rc;
}

/******************************************************************/

static TX_EVENT *
yearlybyday(char *rule, time_t start, time_t end, int skip, time_t min,
	    time_t max, int *maxocc, int exact)
{
	static CONST char fn[] = "yearlybyday";
	char *cp = rule, *ncp;
	TX_EVENT *rc = NULL, *trc = NULL;
	MONTHLIST *dlist = NULL, *curday;
	long nyears, repetitions, i;
	struct tm tm;
	time_t cstart;
	int breaknow = 0, firstpass = 1;
	int wasvalid;
	int ialloced = 0;

	if (strncmp(cp, "YD", 2) != 0) goto end;
	cp += 2;
	if (!isdigit(*cp)) goto end;
	nyears = strtol(cp, &ncp, 10);
	cp = ncp;
	dlist = daylist(cp, start, end, skip, max, &ncp, &wasvalid, YEARDAY);
	if (exact && (0 == wasvalid)) goto end;
	cp = ncp;
	repetitions = duration(cp, &ncp);
	cp = ncp;
	while (isspace(*cp))
		cp++;
	tm = *localtime(&start);
	for (i = 0; repetitions == 0 || i < repetitions; i++)
	{
		for (curday = dlist; curday; curday = curday->next)
		{
			if (i == 0 && (curday->month < (tm.tm_yday + 1)))
			{
				continue;
			}
			if (i > 0 || (curday->month > (tm.tm_yday + 1)))
			{
				tm.tm_mday = curday->month;
				tm.tm_mon = 0;
			}
			tm.tm_sec = 0;
			tm.tm_isdst = -1;
			cstart = mktime(&tm);
			if (cstart == -1)
			{
				breaknow = 1;
				break;
			}
			trc = TXcloseevent(trc);
			if (*cp == 'D')
			{
				trc =
					daily(cp, cstart, end, skip, min, max,
					      maxocc, firstpass ? 0 : 1,
					      exact);
				if (!trc)
					goto end;
			}
			if (*cp == 'M')
			{
				if ('D' == cp[1])
				{
					trc =
						monthlybyday(cp, cstart, end,
							     skip, min, max,
							     maxocc,
							     firstpass ? 0 :
							     1, exact);
					if (!trc)
						goto end;
				}
				else if ('P' == cp[1])
				{
					trc =
						monthlybypos(cp, cstart, end,
							     skip, min, max,
							     maxocc,
							     firstpass ? 0 :
							     1, exact);
					if (!trc)
						goto end;
				}
				else
				{
					trc =
						minuteop(cp, cstart, end,
							 skip, min, max,
							 maxocc,
							 firstpass ? 0 : 1);
					if (!trc)
						goto end;
				}
			}
			if (*cp == 'W')
			{
				trc =
					weekly(cp, cstart, end, skip, min,
					       max, maxocc, firstpass ? 0 : 1,
					       exact);
				if (!trc)
					goto end;
			}
			if (NULL == trc)
			{
				if (maxocc && *maxocc < 0)
					break;
				ALLOCTYPE(fn, trc, TX_EVENT);
				trc->when = cstart;
				trc->count = 1;
				trc->next = NULL;
			}
			if (trc->when > max)
			{
				breaknow = 1;
				break;
			}
			firstpass = 0;
			if (trc->when >= min)
			{
				rc = addtotxevent(rc, trc);
				trc = NULL;
				if (maxocc && ialloced)
					*maxocc = *maxocc - 1;
			}
			if ((i == (repetitions - 1) && curday->last)
			    || (maxocc && *maxocc <= 0))
			{
				breaknow = 1;
				break;
			}
		}
		tm.tm_year += nyears;
		tm.tm_mday = 1;
		if (0 != breaknow)
			break;
	}
      end:
	closemonthlist(dlist);
	TXcloseevent(trc);
	return rc;
}

/******************************************************************/

static TX_EVENT *
yearlybymonth(char *rule, time_t start, time_t end, int skip, time_t min,
	      time_t max, int *maxocc, int exact)
{
	static CONST char fn[] = "yearlybymonth";
	char *cp = rule, *ncp;
	TX_EVENT *rc = NULL, *trc = NULL;
	MONTHLIST *mlist = NULL, *curmonth;
	long nyears, repetitions, i;
	struct tm tm;
	time_t cstart;
	int breaknow = 0, firstpass = 1;
	int wasvalid;
	int ialloced = 0;

	if (strncmp(cp, "YM", 2) != 0) goto end;
	cp += 2;
	if (!isdigit(*cp)) goto end;
	nyears = strtol(cp, &ncp, 10);
	cp = ncp;
	mlist = daylist(cp, start, end, skip, max, &ncp, &wasvalid, MONTHNUM);
	if (exact && (0 == wasvalid)) goto end;
	cp = ncp;
	repetitions = duration(cp, &ncp);
	cp = ncp;
	while (isspace(*cp))
		cp++;
	tm = *localtime(&start);
	for (i = 0; repetitions == 0 || i < repetitions; i++)
	{
		for (curmonth = mlist; curmonth; curmonth = curmonth->next)
		{
			if (i == 0 && (curmonth->month < (tm.tm_mon + 1)))
			{
				continue;
			}
			if (i > 0 || (curmonth->month > (tm.tm_mon + 1)))
			{
				tm.tm_mon = curmonth->month - 1;
				tm.tm_mday = 1;
			}
			tm.tm_sec = 0;
			tm.tm_isdst = -1;
			cstart = mktime(&tm);
			if (cstart == -1)
			{
				breaknow = 1;
				break;
			}
			trc = TXcloseevent(trc);
			if (*cp == 'D')
			{
				trc =
					daily(cp, cstart, end, skip, min, max,
					      maxocc, firstpass ? 0 : 1,
					      exact);
				if (!trc)
				  {
					if (!exact)
						goto nextyear;
					else
						goto end;
				  }
			}
			if (*cp == 'M')
			{
				if ('D' == cp[1])
				{
					trc =
						monthlybyday(cp, cstart, end,
							     skip, min, max,
							     maxocc,
							     firstpass ? 0 :
							     1, exact);
					if (!trc)
					  {
						if (!exact)
							goto nextyear;
						else
							goto end;
					  }
				}
				else if ('P' == cp[1])
				{
					trc =
						monthlybypos(cp, cstart, end,
							     skip, min, max,
							     maxocc,
							     firstpass ? 0 :
							     1, exact);
					if (!trc)
					  {
						if (!exact)
							goto nextyear;
						else
							goto end;
					  }
				}
				else
				{
					trc =
						minuteop(cp, cstart, end,
							 skip, min, max,
							 maxocc,
							 firstpass ? 0 : 1);
					if (!trc)
					  {
						if (!exact)
							goto nextyear;
						else
							goto end;
					  }
				}
			}
			if (*cp == 'W')
			{
				trc =
					weekly(cp, cstart, end, skip, min,
					       max, maxocc, firstpass ? 0 : 1,
					       exact);
				if (!trc)
				  {
					if (!exact)
						goto nextyear;
					else
						goto end;
				  }
			}
			if (NULL == trc)
			{
				if (maxocc && *maxocc < 0)
					break;
				ALLOCTYPE(fn, trc, TX_EVENT);
				trc->when = cstart;
				trc->count = 1;
				trc->next = NULL;
			}
			if (trc->when > max)
			{
				breaknow = 1;
				break;
			}
			firstpass = 0;
			if (trc->when >= min)
			{
				rc = addtotxevent(rc, trc);
				trc = NULL;
				if (maxocc && ialloced)
					*maxocc = *maxocc - 1;
			}
			if ((i == (repetitions - 1) && curmonth->last)
			    || (maxocc && *maxocc <= 0))
			{
				breaknow = 1;
				break;
			}
		}
	      nextyear:
		tm.tm_year += nyears;
		tm.tm_mday = 1;
		if (0 != breaknow)
			break;
	}
      end:
	closemonthlist(mlist);
	TXcloseevent(trc);
	return rc;
}

/******************************************************************/

TX_EVENT *
TXcloseevent(TX_EVENT * event)
{
  TX_EVENT	*next;

  for ( ; event; event = next)
    {
      next = event->next;
      free(event);
    }
  return NULL;
}

/******************************************************************/

TX_EVENT *
TXvcal(char *rule, time_t start, time_t end,
       int skip, int maxocc, time_t min, time_t max, int exact)
{
	char *cp;
	TX_EVENT *rc = NULL;
	int *maxoccp;

	if (start == 0)
		start = time(NULL);	/* `exact' generally 0 */

	if (maxocc == 0)
		maxoccp = NULL;
	else
		maxoccp = &maxocc;
	if (end != 0 && end < max)
		max = end;
	cp = rule;
	while (isspace(*cp))
		cp++;
	switch (*cp)
	{
	case 'D':
		rc = daily(cp, start, end, skip, min, max, maxoccp, 0, exact);
		break;
	case 'M':
		switch (*(cp + 1))
		{
		case 'D':
			rc =
				monthlybyday(cp, start, end, skip, min, max,
					     maxoccp, 0, exact);
			break;
		case 'P':
			rc =
				monthlybypos(cp, start, end, skip, min, max,
					     maxoccp, 0, exact);
			break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			rc =
				minuteop(cp, start, end, skip, min, max,
					 maxoccp, 0);
			break;
		}
		break;
	case 'W':
		rc =
			weekly(cp, start, end, skip, min, max, maxoccp, 0,
			       exact);
		break;
	case 'Y':
		switch (*(cp + 1))
		{
		case 'D':
			rc =
				yearlybyday(cp, start, end, skip, min, max,
					    maxoccp, exact);
			break;
		case 'M':
			rc =
				yearlybymonth(cp, start, end, skip, min, max,
					      maxoccp, exact);
			break;
		}
		break;
	}
	return rc;
}

/* ------------------------------------------------------------------------- */

#define NEXTTOK (ptok = eng, pn = n, eng += n, \
  eng += strspn(eng, WhiteSpace), n = strcspn(eng, WhiteSpace))


static CONST char *synafter ARGS((CONST char *tok, int n));
static CONST char *
synafter(tok, n)
CONST char      *tok;
int             n;
{
  static char   errmsg[64];

  htsnpf(errmsg, sizeof(errmsg), "Syntax error after `%.*s'", n, tok);
  return(errmsg);
}

static CONST char *parse_at ARGS((char **dp, char *e, CONST char **sp));
static CONST char *
parse_at(dp, e, sp)
char            **dp, *e;
CONST char      **sp;
/* Parses one or more AT clauses from `*sp' into vCal buffer
 * `*dp' as "hhmm".  Advances `*sp', `*dp'.  Returns NULL if ok,
 * otherwise static error message.
 */
{
  static char   errmsg[128];
  char          *d;
  CONST char    *eng, *ptok, *s;
  int           cnt, h, m, isat, pn, n;

  pn = 0;
  ptok = eng = *sp;
  eng += strspn(eng, WhiteSpace);
  n = strcspn(eng, WhiteSpace);

  for (d = *dp, cnt = 0; isat = 0, *eng != '\0'; cnt++, NEXTTOK)
    {
      if (n == 2 && strnicmp(eng, "AT", 2) == 0)        /* [AT] */
        {
          isat = 1;
          NEXTTOK;
          if (*eng == '\0') return(synafter(ptok, pn));
        }
      h = m = 0;                                /* [h]h[:][[m]m][A|P[M]] */
      s = eng;
      if (*s >= '0' && *s <= '9') h = *(s++) - '0'; else break;
      if (*s >= '0' && *s <= '9') h = 10*h + (*(s++) - '0');
      if (*s == ':') s++;
      if (*s >= '0' && *s <= '9')
        {
          m = *(s++) - '0';
          if (*s >= '0' && *s <= '9') m = 10*m + (*(s++) - '0');
        }
      /* Bug 7143: allow whitespace before AM/PM: */
      if (TX_ISSPACE(*s))
        {
          s += strspn(s, WhiteSpace);
          if (TX_TOLOWER(*s) == 'a' || TX_TOLOWER(*s) == 'p')
            {                                   /* add to current token */
              n = s - eng;
              n += strcspn(s, WhiteSpace);
            }
        }
      /* Check for optional AM/PM: */
      switch (*s)
        {
        case 'p':
        case 'P':
         if (h < 12) h += 12;
         goto skipm;
        case 'a':
        case 'A':
          if (h == 12) h = 0;
        skipm:
          s++;
          if (*s == 'm' || *s == 'M') s++;
          break;
        }
      if (s != eng + n) break;                  /* incomplete token parse */
      if (h > 23 || m > 59) goto badat;
      d += htsnpf(d, e - d, " %02d%02d", h, m);
      if (d > e) return(SpecTooLong);
    }
  if (isat)
    {
    badat:
      htsnpf(errmsg, sizeof(errmsg), "Bad AT time `%.*s'", n, eng);
      return(errmsg);
    }
  if (cnt == 0)
    {
      htsnpf(errmsg, sizeof(errmsg), "Expected AT clause at `%.*s'", n, eng);
      return(errmsg);
    }
  *dp = d;
  *sp = eng;
  return(CHARPN);
}

static CONST char *parse_weekly ARGS((char **dp, char *e, CONST char **sp));
static CONST char *
parse_weekly(dp, e, sp)
char            **dp, *e;
CONST char      **sp;
/* Parses 1 or more "[ON] SUN|...|SAT [AT] hh[:]mm" clauses from
 * `*sp' into vCal buffer `*dp'.  Advances `*sp', `*dp'.  Returns NULL
 * if ok, otherwise static error message.
 */
{
  static char   errmsg[128];
  char          *d, *d1, *er;
  int           cnt, ison, pn, n, i;
  CONST char    *eng, *ptok;

  pn = 0;
  ptok = eng = *sp;
  eng += strspn(eng, WhiteSpace);
  n = strcspn(eng, WhiteSpace);

  for (d = *dp, cnt = 0; *eng != '\0'; cnt++)
    {
      n = strcspn(eng, WhiteSpace);
      for (d1 = d; ison = 0, *eng != '\0'; NEXTTOK)
        {
          if (n == 2 && strnicmp(eng, "ON", 2) == 0)
            {
              ison = 1;
              NEXTTOK;
              if (*eng == '\0') return(synafter(ptok, pn));
            }
          for (i = 0; i < 7; i++)                       /* day of week? */
            if (n >= 2 && strnicmp(eng, WeekDayNames[i], n) == 0) break;
          if (i < 7)                                    /* yes */
            {
              if (d + 3 > e) return(SpecTooLong);
              *(d++) = ' ';
              *(d++) = *WeekDayNames[i];
              *(d++) = WeekDayNames[i][1];
            }
          else                                          /* not day of week */
            {
              if (ison) goto expday;
              goto chkat;                               /* AT clause? */
            }
        }
    chkat:
      if (d == d1) break;                               /* no days */
      if ((er = (char *)parse_at(&d, e, &eng)) != CHARPN) return(er);
    }
  if (cnt == 0)
    {
    expday:
      htsnpf(errmsg, sizeof(errmsg),
             "Expected day of week at `%.*s'", n, eng);
      return(errmsg);
    }
  *dp = d;
  *sp = eng;
  return(CHARPN);
}

static CONST char *parse_monthly ARGS((char *type, char **dp, char *e,
                                       CONST char **sp));
static CONST char *
parse_monthly(type, dp, e, sp)
char            *type, **dp, *e;
CONST char      **sp;
/* Parses 1+ "[ON] [THE] FIRST|n [TO LAST] wkday|DAY [OF THE MONTH]"
 * clauses from `*sp' into vCal buffer `*dp'.  Advances `*sp', `*dp'.
 * Returns NULL if ok, otherwise static error message.
 */
{
  static CONST char * CONST rel[] =
  {
    "FIRST",
    "SECOND",
    "THIRD",
    "FOURTH",
    "FIFTH",
    "SIXTH",
    "SEVENTH",
    "EIGHTH"
    "NINTH",
    "TENTH",
  };
#define NREL    (sizeof(rel)/sizeof(rel[0]))
  static char   errmsg[128];
  char          *d, *d1, *ee;
  int           cnt, ison, pn, n, i, neg, scnt;
  CONST char    *eng, *ptok;
  long          lv;

  pn = 0;
  ptok = eng = *sp;
  eng += strspn(eng, WhiteSpace);
  n = strcspn(eng, WhiteSpace);
  *type = ' ';                                          /* neither */

  for (d = *dp, cnt = 0; *eng != '\0'; cnt++)
    {
      n = strcspn(eng, WhiteSpace);
      for (d1 = d; ison = neg = 0, *eng != '\0'; )
        {                                               /* occurences */
          if (n == 2 && strnicmp(eng, "ON", 2) == 0)
            {
              ison = 1;
              NEXTTOK;
              if (*eng == '\0') return(synafter(ptok, pn));
            }
          if (n == 3 && strnicmp(eng, "THE", 3) == 0)
            {
              ison = 1;
              NEXTTOK;
              if (*eng == '\0') return(synafter(ptok, pn));
            }
          for (i = 0; (size_t)i < NREL; i++)
            if (n >= 3 && strnicmp(eng, rel[i], n) == 0) break;
          if ((size_t)i >= NREL)                /* not ordinal name */
            {
	      lv = strtol(eng, &ee, 0);                 /* n? */
	      if (*eng != '\0' && (strchr(WhiteSpace, *ee) != CHARPN ||
                  (ee > eng && (strnicmp(ee, "ST", 2) == 0 ||
                  strnicmp(ee, "ND", 2) == 0 || strnicmp(ee, "RD", 2) == 0))))
		{
		  if (lv <= 0L) return(ExpPos);
		  i = (int)lv;
		}
              else if (n == 4 && strnicmp(eng, "LAST", 4) == 0)
                i = neg = 1;
              else                                      /* not ordinal/# */
                {
                  if (ison) goto expoccur;
                  break;
                }
            }
          else i++;
          NEXTTOK;
          if (n == 2 && strnicmp(eng, "TO", 2) == 0)    /* [TO LAST] */
            {
              if (neg) return(synafter(ptok, pn));
              neg = 1;
              NEXTTOK;
              if (*eng == '\0') return(synafter(ptok, pn));
              if (n == 4 && strnicmp(eng, "LAST", 4) == 0) NEXTTOK;
              else return(synafter(ptok, pn));
            }
          d += htsnpf(d, e - d, " %d%c", i, (neg ? '-' : '+'));
          if (d > e) return(SpecTooLong);
        }
      if (d == d1)                                      /* no occurences */
        {
          if (d == *dp) goto expoccur;                  /* nothing at all */
          break;                                        /* AT time? */
        }
      for (scnt = 0; *eng != '\0'; scnt++, NEXTTOK)     /* weekdays/DAY */
        {
          for (i = 0; i < 7; i++)                       /* day of week? */
            if (n >= 2 && strnicmp(eng, WeekDayNames[i], n) == 0) break;
          if (i < 7)                                    /* yes */
            {
              if (*type != ' ' && *type != 'P') return(CantMixPosDay);
              if (d + 3 > e) return(SpecTooLong);
              *(d++) = ' ';
              *(d++) = *WeekDayNames[i];
              *(d++) = WeekDayNames[i][1];
              *type = 'P';
            }
          else if (n == 3 && strnicmp(eng, "DAY", 3) == 0)
            {
              if (*type != ' ' && *type != 'D') return(CantMixPosDay);
              *type = 'D';
            }
          else break;
        }
      if (scnt == 0)                                    /* no weekdays */
        {
          htsnpf(errmsg, sizeof(errmsg),
                 "Expected day of week or DAY at `%.*s'", n, eng);
          return(errmsg);
        }
      if (n == 2 && strnicmp(eng, "OF", 2) == 0)        /* [OF THE MONTH] */
	{
	  NEXTTOK;
	  if (n == 3 && strnicmp(eng, "THE", 3) == 0) NEXTTOK;
	  else return(synafter(ptok, pn));
	  if (n == 5 && strnicmp(eng, "MONTH", 5) == 0) NEXTTOK;
	  else return(synafter(ptok, pn));
	}
    }
  if (cnt == 0)
    {
    expoccur:
      htsnpf(errmsg, sizeof(errmsg), "Expected occurence at `%.*s'", n, eng);
      return(errmsg);
    }
  *dp = d;
  *sp = eng;
  return(CHARPN);
#undef NREL
}

static CONST char *parse_minute ARGS((char **dp, char *e, CONST char **sp));
static CONST char *
parse_minute(dp, e, sp)
char            **dp, *e;
CONST char      **sp;
/* Parses zero or one "[EVERY] [n] MINUTE[S] [[REPEAT] n [TIME[S]]" clauses
 * from `*sp' into vCal buffer `*dp'.  Advances `*sp', `*dp'.  Returns NULL
 * if ok, otherwise static error message.
 */
{
  CONST char    *ptok, *eng;
  char          *d, *ee;
  int           pn, n, every = 1, rep = 0;
  long          lv;

  pn = 0;
  ptok = eng = *sp;
  eng += strspn(eng, WhiteSpace);
  n = strcspn(eng, WhiteSpace);
  d = *dp;

  if (n == 5 && strnicmp(eng, "EVERY", 5) == 0) NEXTTOK;  /* [EVERY] */

  lv = strtol(eng, &ee, 0);                             /* [n] */
  if (*eng != '\0' && strchr(WhiteSpace, *ee) != CHARPN) /* a valid number */
    {
      if (lv <= 0L) return(ExpPos);
      every = (int)lv;
      NEXTTOK;
      if (*eng == '\0') return(synafter(ptok, pn));
    }

  if (n < 3 || strnicmp(eng, "MINUTES", n) != 0) return(CHARPN);

  d += htsnpf(d, e - d, " M%d", every);
  if (d > e) return(SpecTooLong);
  NEXTTOK;

  if (n == 6 && strnicmp(eng, "REPEAT", 6) == 0)        /* [REPEAT.. */
    {
      rep = 1;
      NEXTTOK;
    }

  lv = strtol(eng, &ee, 0);                             /* [n] */
  if (*eng != '\0' && strchr(WhiteSpace, *ee) != CHARPN) /* a valid number */
    {
      if (lv <= 0L) return(ExpPos);
      every = (int)lv;
      NEXTTOK;
      if (n >= 3 && strnicmp(eng, "TIMES", n) == 0) NEXTTOK;
    }
  else if (rep)
    return("Expected interval after REPEAT");
  else
    every = 0;
  d += htsnpf(d, e - d, " #%d", every);
  if (d > e) return(SpecTooLong);

  *dp = d;
  *sp = eng;
  return(CHARPN);                                       /* success */
}

CONST char *
tx_english2vcal(buf, sz, eng)
char            *buf;
size_t          sz;
CONST char      *eng;
/* Translates English-like schedule syntax `eng' into vCalendar syntax
 * and stores in `buf' of size `sz'.  Returns NULL if ok, 1 if not
 * known syntax at all, otherwise static error message.
 */
{
  CONST char    *ptok, *sav;
  char          *e, *end;
  int           pn, n, every = 1;
  long          lv;

  end = buf + sz;
  if (sz > 0) *buf = '\0';
  ptok = eng;
  pn = 0;
  eng += strspn(eng, WhiteSpace);
  n = strcspn(eng, WhiteSpace);

  if (n == 5 && strnicmp(eng, "EVERY", 5) == 0) NEXTTOK;  /* [EVERY] */

  sav = eng;
  lv = strtol(eng, &e, 0);                              /* [n] */
  if (*eng != '\0' && strchr(WhiteSpace, *e) != CHARPN) /* a valid number */
    {
      if (lv <= 0L) return(ExpPos);
      every = (int)lv;
      NEXTTOK;
      if (*eng == '\0') return(synafter(ptok, pn));
    }

  NEXTTOK;
  if (pn >= 3 && strnicmp(ptok, "MINUTES", pn) == 0)
    {
      eng = sav;
      goto min;
    }
  else if ((pn == 3 && strnicmp(ptok, "DAY", 3) == 0) ||
           (pn == 4 && strnicmp(ptok, "DAYS", 4) == 0) ||
           (pn == 5 && strnicmp(ptok, "DAILY", 5) == 0))
    {
      buf += htsnpf(buf, end - buf, "D%d", every);
      if (buf > end) return(SpecTooLong);
      e = (char *)parse_at(&buf, end, &eng);
      if (e != CHARPN) return(e);                       /* error */
    }
  else if ((pn == 4 && strnicmp(ptok, "WEEK", 4) == 0) ||
           (pn == 5 && strnicmp(ptok, "WEEKS", 5) == 0) ||
           (pn == 6 && strnicmp(ptok, "WEEKLY", 6) == 0))
    {
      buf += htsnpf(buf, end - buf, "W%d", every);
      e = (char *)parse_weekly(&buf, end, &eng);
      if (e != CHARPN) return(e);                       /* error */
    }
  else if ((pn >= 4 && strnicmp(ptok, "MONTHS", pn) == 0) ||
           (pn = 7 && strnicmp(ptok, "MONTHLY", pn) == 0))
    {
      e = buf + 1;
      buf += htsnpf(buf, end - buf, "M %d", every);
      if (buf > end) return(SpecTooLong);
      e = (char *)parse_monthly(e, &buf, end, &eng);
      if (e != CHARPN) return(e);                       /* error */
      e = (char *)parse_at(&buf, end, &eng);
      if (e != CHARPN) return(e);                       /* error */
    }
  else
    return((char *)1);                                  /* unknown syntax */

  if (end - buf < 3) return(SpecTooLong);
  strcpy(buf, " #0");                                   /* repeat forever */
  buf += 3;

min:
  if ((e = (char *)parse_minute(&buf, end, &eng)) != CHARPN) return(e);
  if (*eng != '\0') return(synafter(eng, strcspn(eng, WhiteSpace)));

  return(CHARPN);                                       /* success */
}

/******************************************************************/

#ifdef TEST

static char *rrules[] = {
	"YM1 1 #0 MD1 1 #1",
	"YD1 1 #0",
	"M60 #12",
	"M5 #12",
	"D1 #5",
	"D1 #5 M10 #6",
	"D2",
	"D2 0600 1200 1500 #2",
	"D2 0600 1200$ 1500 #3",
	"D7 0600 #5 M15 #4",
	"D7 0600$ #4 M15 #4",
	"D7 0600 #1 M15 #4",
	"D7 #1 M15 #4",
	"M15 #4",
	"W1 #4",
	"W2 MO$ TU #2",
	"W1 TU TH #3 M5 #2",
	"W1 TU 1200 TH 1130 #10 M30",
	"W1 TU 1200 1230 TH 1130 1200 #10",
	"W1 TU$ 1200 TH 1130 #10 M30",
	"W1 TU$ 1200 1230 TH 1130 1200 #10",
	"W1 TU 1200$ 1230 TH 1130 1200 #10",
	"MP1 #12",
	"MP2 1+ 1- FR #3",
	"MD1 2- #5",
	"MP1 2- MO #6",
	"MD1 3- #0",
	"MD1 7- #12",
	"MP2 1+$ 1- FR #3",
	"MP6 1+ MO #5 D1 #5",
	"MP6 1+ MO #5 D2 0600 1200 1500 #10",
	"MP6 1+ MO #5 D2 0600 1200 1500 #10 M5 #3",
	"MP6 1+ MO 2- TH #5 M5 #2",
	"MP6 1+ SU MO 1200 2+ TU WE 1300 3+ TH FR 1400 #4",
	"MD1 7 #12",
	"MD1 7 14 21 28 #12",
	"MD1 10 20 #24 D1 0600 1200 1600 #5 M15 #4",
	"YM1 6 12 #5 MP1 1+ MO 1- FR",
	"YM2 6 #3 MD1 12",
	"YM1 3$ 8 #5 MD1 7 14$ 21 28",
	"YM1 6 9 10 #10 MP1 1+ 2+ 3+ 4+ 1- SA SU #1",
	"YM1 6 #10 W1 TU TH 1100 1300 #4",
	"YD1 1 100 200 300 #4",
	"YD1 1 100 #5 D1 #5",
	"YD1 1 100 D1 #5 19990102T000000Z",
	NULL
};

static time_t starts[] = {
	1009861200,
	1009861200,
	960000000,
	960000000,
	960000000,
	960000000,
	960000000,
	770464800,
	770464800,
	770464800,
	770464800,
	770464800,
	770464800,
	770464800,
	960000000,
	1009861200,
	1009904400,
	1009904400,
	1009904400,
	1009904400,
	1009904400,
	1009904400,
	960000000,
	960000000,
	930628800,
	948690000,
	930542400,
	948776400,
	960000000,
	946897200,
	946897200,
	946897200,
	946897200,
	946897200,
	947221200,
	947221200,
	947502000,
	960000000,
	929163723,
	920782800,
	960000000,
	896799600,
	1009861200,
	1009861200,
	1009861200,
	0
};

/******************************************************************/

int
showresults(TX_EVENT * rc)
{
	char evdate[80];
	TX_EVENT *ce;

	if (NULL == rc)
		puts("Error");
	for (ce = rc; ce; ce = ce->next)
	{
		struct tm *tm;

		tm = localtime(&ce->when);
		strftime(evdate, sizeof(evdate), "%a %Y-%m-%d %H:%M:%S", tm);
		printf("%s\n", evdate);
	}
	return 0;
}

/******************************************************************/

int
main()
{
	char **rule;
	TX_EVENT *rc;
	time_t *start, stime, st, nw;
	char	buf[1024];
#if EPI_OS_TIME_T_BITS == EPI_OS_INT_BITS
#  define TIME_T_MAX	((time_t)MAXINT)
#else
#  define TIME_T_MAX	((time_t)((unsigned)(~0) >> 1))
#endif

	while (gets(buf))
	  {
	    nw = time(NULL);
	    st = nw = (nw/60)*60;
	    rc = TXvcal(buf, st, 0, 0, 1, nw, TIME_T_MAX, 0);
	    showresults(rc);
	    rc = TXcloseevent(rc);
	  }
	exit(TXEXIT_OK);

	for (rule = rrules, start = starts; *rule; rule++, start++)
	{
		if (0 == *start)
			stime = time(NULL);
		else
			stime = *start;
#ifdef NEVER
		puts(*rule);
		puts("----------STIME--------------");
		rc =
			TXvcal(*rule, stime, 0, 0, 5, 0, 1262325661,
			       (stime != 0));
		showresults(rc);
		TXcloseevent(rc);
		puts("----------DEF TIME--------------");
		rc = TXvcal(*rule, time(NULL), 0, 0, 5, 0, 1262325661, 1);
		showresults(rc);
		TXcloseevent(rc);
		rc = TXvcal(*rule, 0, 0, 0, 5, 1009861200, 1262325661, 0);
#endif
		rc = TXvcal(*rule, 0, 0, 0, 5, 979598580, 1262325661, 0);
		if (!rc)
		{
			puts("----------NO TIME--------------");
			puts(*rule);
		}
#ifdef NEVER
		showresults(rc);
#endif
		TXcloseevent(rc);
	}
	exit(TXEXIT_OK);
}

#endif /* TEST */
