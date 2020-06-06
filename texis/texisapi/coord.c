#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbquery.h"
#include "sizes.h"

/******************************************************************/

int
TXll2code(latv, lonv, code)
long	latv;
long	lonv;
long	*code;
/* Converts latitude/longitude `latv'/`lonv' (in DDDMMSS integer format)
 * to geocode value `*code'.
 * Returns 0 if ok, or -1 on error (and -1 for `*code').
 */
{
	long res, deg, min;
	double	decLat, decLon;

	deg = latv / 10000L;                      /* convert to decimal deg.*/
	latv -= deg*10000L;
	min = latv / 100L;
	latv -= min*100L;
	decLat = (double)deg + ((double)min)/(double)60.0 +
		((double)latv)/(double)(60.0*60.0);

	deg = lonv / 10000L;                      /* convert to decimal deg.*/
	lonv -= deg*10000L;
	min = lonv / 100L;
	lonv -= min*100L;
	decLon = (double)deg + ((double)min)/(double)60.0 +
		((double)lonv)/(double)(60.0*60.0);

	*code = res = TXlatlon2geocode(decLat, decLon);
	return(res == -1L ? -1 : 0);
}

/******************************************************************/

int
TXcode2ll(code, lat, lon)
long code;
long *lat;
long *lon;
/* Sets `*lat'/`*lon' to latitude/longitude (in DDDMMSS integer format)
 * of geocode value `code'.
 * Returns 0 if ok, -1 on error (and -1/-1 lat/lon WTF in valid range).
 */
{
	long latv, lonv, deg, min;
        double  decLat, decLon;

	if (!TXgeocodeDecode(code, &decLat, &decLon))
	{					/* error decoding */
		*lat = *lon = -1L;
		return(-1);			/* error */
	}

	/* Convert DDD.DDD decimal format to DDDMMSS integer format: */

	latv = (long)(decLat*(double)(60*60) +
		(decLat < (double)0.0 ? (double)-0.5 : (double)0.5));
	deg = (long)decLat;
	latv -= deg*(60L*60L);
	min = latv / 60L;
	latv -= min*60L;
        /*
	if (min < 0L) min = -min;
	if (latv < 0L) latv = -latv;
        */
	*lat=(deg*10000)+(min*100)+latv;

	lonv = (long)(decLon*(double)(60*60) +
		(decLon < (double)0.0 ? (double)-0.5 : (double)0.5));
	deg = (long)decLon;
	lonv -= deg*(60L*60L);
	min = lonv / 60L;
	lonv -= min*60L;
        /*
	if (min < 0L) min = -min;
	if (lonv < 0L) lonv = -lonv;
        */
	*lon=(deg*10000)+(min*100)+lonv;
	return 0;
}

/******************************************************************/
/*
 * TXcodes2box takes 2 codes, and returns the smallest box containing
 * the points.  Only points within the box can have codes between
 * code1 and code2.  This assumes that code1 is less than code2.
 */

int
TXcodes2box(code1, code2, llat, llon, hlat, hlon)
long	code1, code2;
long	*llat, *llon;
long	*hlat, *hlon;
{
	long	lcode = code1, hcode = code2;
	int	i = 0;

	while(lcode != hcode)
	{
		lcode >>=1;
		hcode >>=1;
		i++;
	}
	/* lcode and hcode have <sizeof(long) -i> bits in common. */
	while(i > 0)
	{
		lcode <<= 1;
		hcode = (hcode << 1) + 1;
		i--;
	}
#ifdef TEST
	printf("%5d %5d %5d %5d\n", code1, code2, lcode, hcode);
#else
	TXcode2ll(lcode, llat, llon);
	if(hcode == lcode)
	{
		*hlat = *llat;
		*hlon = *llon;
	}
	else
		TXcode2ll(hcode, hlat, hlon);
#endif
	return 0;
}

/*
 * Do the region defined by (c1l,c1h) contain the area (c2l,c2h) overlap?
 */

int
TXcodesintersect(c1l, c1h, c2l, c2h)
long c1l, c1h, c2l, c2h;
{
	long llat1, llon1, llat2, llon2;
	long hlat1, hlon1, hlat2, hlon2;
	static long lc2l, lllat2, lllon2;
	static long lc2h, lhlat2, lhlon2;

	TXcodes2box(c1l,c1h, &llat1, &llon1, &hlat1, &hlon1);
#ifdef NEVER
	TXcodes2box(c2l,c2h, &llat2, &llon2, &hlat2, &hlon2);
#endif

	if(lc2l != c2l)
	{
		TXcode2ll(c2l, &lllat2, &lllon2);
		lc2l = c2l;
	}
	llat2 = lllat2;
	llon2 = lllon2;
	if(lc2h != c2h)
	{
		TXcode2ll(c2h, &lhlat2, &lhlon2);
		lc2h = c2h;
	}
	hlat2 = lhlat2;
	hlon2 = lhlon2;
	if((llat1 <= hlat2) && (llat2 <= hlat1) &&
		(llon1 <= hlon2) && (llon2 <= hlon1))
	{
		return 1;
	}
	return 0;
}

/*
 * Do the region defined by (c1l,c1h) contain the area (c2l,c2h) overlap?
 */

int
TXcodesintersect1(c1, c2l, c2h)
long c1, c2l, c2h;
{
	long llat1, llon1, llat2, llon2;
	long hlat2, hlon2;
	static long lc2l, lllat2, lllon2;
	static long lc2h, lhlat2, lhlon2;
	static long compref;
	static int  comshift;
	int i;
	long lcode, hcode, ccode;

	if(lc2l != c2l || lc2h != c2h)
	{
		i = 0;
		lcode = c2l;
		hcode = c2h;
		while(lcode != hcode)
		{
			lcode >>=1;
			hcode >>=1;
			i++;
		}
		comshift = i;
		compref = lcode;
		if(lc2l != c2l)
		{
			TXcode2ll(c2l, &lllat2, &lllon2);
			lc2l = c2l;
		}
		if(lc2h != c2h)
		{
			TXcode2ll(c2h, &lhlat2, &lhlon2);
			lc2h = c2h;
		}
	}
	llat2 = lllat2;
	llon2 = lllon2;
	hlat2 = lhlat2;
	hlon2 = lhlon2;

	ccode = c1 >> comshift;
	if (ccode != compref)
		return 0;
	TXcode2ll(c1, &llat1, &llon1);

	if((llat1 <= hlat2) && (llat2 <= llat1) &&
		(llon1 <= hlon2) && (llon2 <= llon1))
	{
		return 1;
	}
	return 0;
}

#ifdef TEST

main()
{
	int i, j;
	for(i=0; i < 32; i++)
	{
		for(j=i; j < 32; j++)
			TXcodes2box(i, j, 0,0,0,0);
	}

}

#endif
