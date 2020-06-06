/* Routines to convert an ascii time to a time_t */
/* Will accept mm/dd/yy only. There is currently */
/* No support for other formats.                 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WINDLL
int __cdecl sscanf(const char *, const char *, ...);
#endif

time_t *
strtodate(s)
char	*s;
{
	int	month, day, year, deltad;
	time_t	tt, *ttp;
	struct tm	t, *tp;

	time(&tt);
	tp = localtime(&tt);
	t = *tp;
	if(!strncmp(s, "today", 5))
	{
		deltad = atoi(s+5);
		t.tm_sec = 0;
		t.tm_min = 0;
		t.tm_hour = 0;
		ttp = (time_t *)calloc(1, sizeof(time_t));
		*ttp = mktime(&t);
		*ttp += deltad*24*60*60;
		tp = localtime(ttp);
		t = *tp;
		if(t.tm_hour == 1)
			t.tm_hour = 0;
		if(t.tm_hour == 23)
		{
			t.tm_hour = 0;
			t.tm_mday += 1;
		}
		t.tm_sec = 0;
		t.tm_min = 0;
		t.tm_hour = 0;
		*ttp = mktime(&t);
		return ttp;
	}
	else if(!strncmp(s, "now", 3))
	{
		deltad = atoi(s+3);
		ttp = (time_t *)calloc(1, sizeof(time_t));
		*ttp = mktime(&t);
		*ttp += deltad;
		return ttp;
	}
	else
	{
		int n;

		deltad = 0;
		n = sscanf(s, "%d/%d/%d", &month, &day, &year);
		if (year > 1900)
			year -= 1900;
		if (year < 70)
			year += 100;
		if (n > 0)
			t.tm_mon = month-1;
		if (n > 1)
			t.tm_mday = day;
		if (n > 2)
			t.tm_year = year;
		t.tm_sec = 0;
		t.tm_min = 0;
		t.tm_hour = 0;
		ttp = (time_t *)calloc(1, sizeof(time_t));
		*ttp = mktime(&t);
		tp = localtime(ttp);
		t = *tp;
		if(t.tm_hour == 1)
			t.tm_hour = 0;
		if(t.tm_hour == 23)
		{
			t.tm_hour = 0;
			t.tm_mday += 1;
		}
		t.tm_sec = 0;
		t.tm_min = 0;
		t.tm_hour = 0;
		*ttp = mktime(&t);
		return ttp;
	}
}

#ifdef TEST

main()
{
	char	buf[80];
	time_t	*t;

	while (fgets(buf, 80, stdin))
	{
		t = strtodate(buf);
		printf("%ld -> %s", *t, ctime(t));
	}
}

#endif
