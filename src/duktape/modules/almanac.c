/*
    almanac.c

    Copyright (c) 2024 Aaron Flin
    MIT LICENSED
    https://opensource.org/license/mit


    for test program:
    cc -DTEST -Wall -o almanac almanac.c astronomy.c -lm
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include "almanac.h"


rp_time_t rp_jd_to_time(double jd)
{
    return (rp_time_t) ( (jd-2440587.5) * 86400.0);
}    

rp_jd_t rp_time_to_jd(rp_time_t t)
{
    return ((double)t / 86400.0) + 2440587.5;
}

rp_jd_t rp_cal_to_jd(int y, int m, int d, int h, int M, int s)
{
    int gplus=0;
    rp_jd_t ret;

    if (y > 1582 || (y == 1582 && m > 10) || (y == 1582 && m == 10 && d > 14))
        gplus = 1;

    y+=8000;
    if(m<3) { y--; m+=12; }

    if (gplus)
        ret = -0.5 + (rp_jd_t)( (y*365) +(y/4) -(y/100) +(y/400) - 1200820
              +(m*153+3)/5-92
              +d-1);
    else
        ret = -0.5 + (rp_jd_t)( (y*365) +(y/4) - 1200882
              +(m*153+3)/5-92
              +d-1);

    ret += ( (rp_jd_t)( h*3600 + M*60 +s) )/ 86400.0 ;

    return ret;
}

rp_jd_t rp_tm_to_jd(struct tm *d)
{
    return rp_cal_to_jd(d->tm_year + 1900, d->tm_mon + 1, d->tm_mday, d->tm_hour, d->tm_min, d->tm_sec);
}

rp_time_t rp_tm_to_time(struct tm *d)
{
    return rp_jd_to_time(rp_tm_to_jd(d));
}

struct tm * rp_jd_to_tm(rp_jd_t jd, struct tm *ret)
{
    int jdi, a,alpha,b,c,d,e;
    double f,q;
    int month;

    if (jd < 0) {
        return NULL;
    } else {
        jd += 0.5;
        jdi = (int)jd;
        f = jd - (double)jdi;

        if (jdi < 2299161) //julian
        {
            a = jdi;
        }
        else //gregorian
        {
            alpha = (int)(((double)jdi - 1867216.25) / 36524.25);
            a = jdi + 1 + alpha - (alpha/4);
        }

        b = a + 1524;
        c = (int)(((double)b - 122.1) / 365.25);
        d = (int)((double)c * 365.25);
        e = (int)(((double)b - (double)d) / 30.6001);
        q = (double)(b - d - (int)((double)e * 30.6001)) + f;

        if(e<14)
            month = ret->tm_mon=(int)(e-1);
        else
            month = (int)(e-13);

        if(month<3)
            ret->tm_year=c-6615;
        else
            ret->tm_year=c-6616;

        ret->tm_mday = (int)q;

        q = (q - (double)ret->tm_mday) * 86400.0;
        ret->tm_hour = (int)((q / 3600.0) + 0.0);

        q -= (double)ret->tm_hour * 3600.0;
        ret->tm_min = (int)((q / 60.0) + 0.0);

        // added 0.5 -ajf
        ret->tm_sec = (int)(0.5 + (q - (0.5 + ((double)ret->tm_min * 60.0))));

        ret->tm_wday = ((int)jd + 1) % 7;

        ret->tm_mon=month-1;

        ret->tm_yday = (int)(rp_cal_to_jd(ret->tm_year+1900, month, ret->tm_mday,0,0,0) - rp_cal_to_jd(ret->tm_year+1900, 1, 1,0,0,0));
    }
    return ret;
}


struct tm * rp_time_to_tm(rp_time_t t, struct tm *tm)
{
    return rp_jd_to_tm( rp_time_to_jd(t), tm );
}

rp_time_t rp_astro_to_time(astro_time_t *t)
{
    return (rp_time_t)(86400.0 * (t->ut + 10957.5));
}

struct tm * rp_astro_to_tm(astro_time_t *t, struct tm *tm)
{
    return rp_jd_to_tm( (t->ut + 2451545.0), tm);
}

/* is this accurate for pre 1582? */
astro_time_t rp_tm_to_astro(struct tm *tm)
{
    return Astronomy_MakeTime(
        tm->tm_year + 1900,
        tm->tm_mon + 1,
        tm->tm_mday,
        tm->tm_hour,
        tm->tm_min,
        tm->tm_sec
    );
}

astro_time_t rp_time_to_astro(rp_time_t t)
{
    return Astronomy_TimeFromDays(
        0.5 + ((double)t)/86400.0 - 10958
    );
}

// same as timegm without the pre-1900 problems
// same as rp_tm_to_time
rp_time_t rp_timegm(struct tm *tm)
{
    return rp_jd_to_time( rp_tm_to_jd(tm) );
}


// Constants for the moon phase calculation
#define SYNODIC_MONTH 29.53058867
#define NEW_MOON_2000 2451550.25974 // Julian date of the New Moon on 2000 January 6 18:14:00 gmt
#define JD_2000 2451545.0
#define toRad 0.01745329251994329547437168
#define GREGORIAN_START -12219292800

#define mod360(d) fmod(d, 360.0)

static double constrain(double d){
    double t = mod360(d);
    return t;
}

/* just for testing
char * getdatestr(rp_time_t t)
{
    struct tm ts={0};
    char *tbuf = calloc(128,1);
    rp_time_to_tm(t, &ts);
    strftime(tbuf, 127, "%c", &ts);
    return tbuf;
}
*/

/* modified from https://www.celestialprogramming.com/meeus-illuminated_fraction_of_the_moon.html */
double moon_phase_precise(rp_time_t t)
{
    rp_jd_t jd = rp_time_to_jd(t);

    double T = (jd-JD_2000)/36525.0;
    double D = constrain(297.8501921 + 445267.1114034*T - 0.0018819*T*T + 1.0/545868.0*T*T*T - 1.0/113065000.0*T*T*T*T)*toRad; //47.2
    double M = constrain(357.5291092 + 35999.0502909*T - 0.0001536*T*T + 1.0/24490000.0*T*T*T)*toRad; //47.3
    double Mp = constrain(134.9633964 + 477198.8675055*T + 0.0087414*T*T + 1.0/69699.0*T*T*T - 1.0/14712000.0*T*T*T*T)*toRad; //47.4

    //48.4
    double i=constrain(180 - D*180/PI - 6.289 * sin(Mp) + 2.1 * sin(M) -1.274 * sin(2*D - Mp) -0.658 * sin(2*D) 
        -0.214 * sin(2*Mp) -0.11 * sin(D))*toRad;

    i=i/(2*PI) + 0.5;

    if(i>1.0)
        i-=1.0;

    return i;
}

static rp_time_t _next_moon_phase_precise(rp_time_t rpt, double phase)
{
    rp_jd_t jd = rp_time_to_jd(rpt) - JD_2000 + SYNODIC_MONTH -10;

    double correction=0.0;

    double k = floor(12.3685*jd/365.2425) + phase;

    double T = k/1236.85; //49.3
    double JDE = 2451550.09766 + 29.530588861*k + 0.00015437*T*T - 0.000000150*T*T*T + 0.00000000073*T*T*T*T; //49.1

    double E = 1 - 0.002516*T - 0.0000074*T*T; //47.6

    double M = mod360(2.5534 + 29.10535670*k - 0.0000014*T*T - 0.00000011*T*T*T)*toRad; //49.4
    double Mp = mod360(201.5643 + 385.81693528*k + 0.0107582*T*T + 0.00001238*T*T*T - 0.000000058*T*T*T*T)*toRad; //49.5
    double F = mod360(160.7108 + 390.67050284*k - 0.0016118*T*T - 0.00000227*T*T*T + 0.000000011*T*T*T*T)*toRad; //49.6
    double Om = mod360(124.7746 - 1.56375588*k + 0.0020672*T*T + 0.00000215*T*T*T)*toRad; //49.7

    //P351-352
    double A1 = mod360(299.77 + 0.107408*k - 0.009173*T*T)*toRad;
    double A2 = mod360(251.88 + 0.016321*k)*toRad;
    double A3 = mod360(251.83 + 26.651886*k)*toRad;
    double A4 = mod360(349.42 + 36.412478*k)*toRad;
    double A5 = mod360(84.66 + 18.206239*k)*toRad;
    double A6 = mod360(141.74 + 53.303771*k)*toRad;
    double A7 = mod360(207.14 + 2.453732*k)*toRad;
    double A8 = mod360(154.84 + 7.306860*k)*toRad;
    double A9 = mod360(34.52 + 27.261239*k)*toRad;
    double A10 = mod360(207.19 + 0.121824*k)*toRad;
    double A11 = mod360(291.34 + 1.844379*k)*toRad;
    double A12 = mod360(161.72 + 24.198154*k)*toRad;
    double A13 = mod360(239.56 + 25.513099*k)*toRad;
    double A14 = mod360(331.55 + 3.592518*k)*toRad;

    if (phase == 0) 
    {
        correction = 0.00002*sin(4*Mp) + -0.00002*sin(3*Mp + M) + -0.00002*sin(Mp - M - 2*F) + 0.00003*sin(Mp - M + 2*F) + -0.00003*sin(Mp + M + 2*F) +
            0.00003*sin(2*Mp + 2*F) + 0.00003*sin(Mp + M - 2*F) + 0.00004*sin(3*M) + 0.00004*sin(2*Mp - 2*F) + -0.00007*sin(Mp + 2*M) + -0.00017*sin(Om) +
            -0.00024*E*sin(2*Mp - M) + 0.00038*E*sin(M - 2*F) + 0.00042*E*sin(M + 2*F) + -0.00042*sin(3*Mp) + 0.00056*E*sin(2*Mp + M) + -0.00057*sin(Mp + 2*F) +
            -0.00111*sin(Mp - 2*F) + 0.00208*E*E*sin(2*M) + -0.00514*E*sin(Mp + M) + 0.00739*E*sin(Mp - M) + 0.01039*sin(2*F) + 0.01608*sin(2*Mp) +
            0.17241*E*sin(M) + -0.40720*sin(Mp);
    } 
    else if ((phase == 0.25) || (phase == 0.75))
    {
        correction = -0.00002*sin(3*Mp + M) + 0.00002*sin(Mp - M + 2*F) + 0.00002*sin(2*Mp - 2*F) + 0.00003*sin(3*M) + 0.00003*sin(Mp + M - 2*F) + 0.00004*sin(Mp - 2*M) +
            -0.00004*sin(Mp + M + 2*F) + 0.00004*sin(2*Mp + 2*F) + -0.00005*sin(Mp - M - 2*F) + -0.00017*sin(Om) + 0.00027*E*sin(2*Mp + M) + -0.00028*E*E*sin(Mp + 2*M) +
            0.00032*E*sin(M - 2*F) + 0.00032*E*sin(M + 2*F) + -0.00034*E*sin(2*Mp - M) + -0.00040*sin(3*Mp) + -0.00070*sin(Mp + 2*F) + -0.00180*sin(Mp - 2*F) +
            0.00204*E*E*sin(2*M) + 0.00454*E*sin(Mp - M) + 0.00804*sin(2*F) + 0.00862*sin(2*Mp) + -0.01183*E*sin(Mp + M) + 0.17172*E*sin(M) + -0.62801*sin(Mp);    

        double W = 0.00306 - 0.00038*E*cos(M) + 0.00026*cos(Mp) - 0.00002*cos(Mp - M) + 0.00002*cos(Mp + M) + 0.00002*cos(2*F);
        if (phase == 0.25){
            correction += W;
        } else {
            correction -= W;
        }

    } 
    else if (phase == 0.5) 
    {
        correction = 0.00002*sin(4*Mp) + -0.00002*sin(3*Mp + M) + -0.00002*sin(Mp - M - 2*F) + 0.00003*sin(Mp - M + 2*F) + -0.00003*sin(Mp + M + 2*F) + 0.00003*sin(2*Mp + 2*F) +
            0.00003*sin(Mp + M - 2*F) + 0.00004*sin(3*M) + 0.00004*sin(2*Mp - 2*F) + -0.00007*sin(Mp + 2*M) + -0.00017*sin(Om) + -0.00024*E*sin(2*Mp - M) +
            0.00038*E*sin(M - 2*F) + 0.00042*E*sin(M + 2*F) + -0.00042*sin(3*Mp) + 0.00056*E*sin(2*Mp + M) + -0.00057*sin(Mp + 2*F) + -0.00111*sin(Mp - 2*F) +
            0.00209*E*E*sin(2*M) + -0.00514*E*sin(Mp + M) + 0.00734*E*sin(Mp - M) + 0.01043*sin(2*F) + 0.01614*sin(2*Mp) + 0.17302*E*sin(M) + -0.40614*sin(Mp);
    }

    JDE+=correction;

    //Additional corrections P 252
    correction = 0.000325*sin(A1) + 0.000165*sin(A2) + 0.000164*sin(A3) + 0.000126*sin(A4) + 0.000110*sin(A5) + 0.000062*sin(A6) + 0.000060*sin(A7) +
        0.000056*sin(A8) + 0.000047*sin(A9) + 0.000042*sin(A10) + 0.000040*sin(A11) + 0.000037*sin(A12) + 0.000035*sin(A13) + 0.000023*sin(A14);
    
    JDE += correction;

    return rp_jd_to_time(JDE);
}

rp_time_t next_moon_phase_precise(rp_time_t rpt, double phase)
{
    rp_time_t ret = _next_moon_phase_precise(rpt, phase);
    
    if(ret < rpt)
    {
        return _next_moon_phase_precise( rpt + (28 * 86400), phase);
    }
    else if (ret > rpt + (SYNODIC_MONTH * 86400) )
    {
        time_t last = _next_moon_phase_precise( rpt - (28 * 86400), phase);

        // compare precise, since SYNODIC_MONTH can differ slightly from actual next one
        if(last < rpt)
            return ret;

        return last;
    }

    return ret;
}


//#define NEW_MOON_2000 2451549.9293

// Function to calculate the moon phase as a fraction
static double moon_phase_from_jd(rp_jd_t jd) {
    rp_jd_t days_since_new = jd - NEW_MOON_2000;
    rp_jd_t new_moons = days_since_new / SYNODIC_MONTH;

    return (double) (new_moons - floor(new_moons));
}

double moon_phase(rp_time_t t)
{
    return moon_phase_from_jd(rp_time_to_jd(t));
}

/* phase is 0.0 to 1.0, from new moon to new moon 
   0.5 is full
   perc is the desired phase

   returns the next date that of "perc" phase
*/
rp_time_t next_moon_phase(rp_time_t t, double perc)
{
    double cur =  moon_phase(t);
    double diff = perc - cur;

    if(diff<0.0) diff = 1+diff;

    return t + (diff * SYNODIC_MONTH*86400);
}


// Function to calculate the illuminated fraction of the moon
double illuminated_fraction(double phase)
{
    return (1 - cos(phase * 2 * M_PI)) / 2;
}

const char* phase_name(double phase) {
    if (phase < 0.02 || phase > 0.98) return "New Moon";
    else if (phase < 0.23) return "Waxing Crescent";
    else if (phase < 0.27) return "First Quarter";
    else if (phase < 0.48) return "Waxing Gibbous";
    else if (phase < 0.52) return "Full Moon";
    else if (phase < 0.73) return "Waning Gibbous";
    else if (phase < 0.77) return "Last Quarter";
    else return "Waning Crescent";
}

#define printastro(atime) do{\
    astro_utc_t u = Astronomy_UtcFromTime(atime);\
    printf("%d/%d/%d %02d:%02d:%02.0f\n", u.month, u.day, u.year, u.hour, u.minute, u.second);\
} while(0)

#define printtm(tmtime) do{\
    char buf[128];\
    strftime(buf, 128, "%c", tmtime);\
    printf("%s\n", buf);\
} while(0)


static double get_az(astro_body_t body, astro_time_t *atm, astro_observer_t observer, 
    double*azimuth, double *altitude, double *ra, double *dec)
{
    astro_equatorial_t equ_2000, equ_ofdate;
    astro_horizon_t hor;

    equ_2000 = Astronomy_Equator(body, atm, observer, EQUATOR_J2000, ABERRATION);
    if (equ_2000.status != ASTRO_SUCCESS)
        return -1.0;

    equ_ofdate = Astronomy_Equator(body, atm, observer, EQUATOR_OF_DATE, ABERRATION);
    if (equ_ofdate.status != ASTRO_SUCCESS)
        return -1.0;

    hor = Astronomy_Horizon(atm, observer, equ_ofdate.ra, equ_ofdate.dec, REFRACTION_NORMAL);
    //printf("%-8s %8.2lf %8.2lf %8.2lf %8.2lf\n", Astronomy_BodyName(body[i]), equ_2000.ra, equ_2000.dec, hor.azimuth, hor.altitude);

    if(altitude)
        *altitude=hor.altitude;

    if(azimuth)
        *azimuth=hor.azimuth;

    if(ra)
        *ra=equ_2000.ra;

    if(dec)
        *dec=equ_2000.dec;
        
    return hor.azimuth;
}



rp_body_times *rp_body_gettimes(struct tm *tm, astro_body_t body, double lat, double lon, rp_body_times *times)
{
    rp_time_t t, t2;
    astro_observer_t observer={0};
    astro_time_t atm;
    astro_search_result_t ares;

    memset(times, 0, sizeof(rp_body_times) );

    atm=rp_tm_to_astro(tm);

    observer.latitude=lat;
    observer.longitude=lon;

    (void) get_az(body, &atm, observer, &times->cur_az, &times->cur_alt, &times->cur_ra, &times->cur_dec);

    printastro(atm);

    ares  = Astronomy_SearchRiseSet(body,  observer, DIRECTION_RISE, atm, 300.0);
    t  = rp_astro_to_time(&ares.time);
    rp_time_to_tm(t, &times->rise);
    //azimuth
    times->rise_az=get_az(body, &ares.time, observer, NULL, NULL, NULL, NULL);


    ares  = Astronomy_SearchRiseSet(body,  observer, DIRECTION_SET, atm, 300.0);
    t2  = rp_astro_to_time(&ares.time);
    if((t2-t)<0)
    {
        t2 =t;
        astro_time_t atm2 = rp_time_to_astro(t2);
        ares  = Astronomy_SearchRiseSet(body,  observer, DIRECTION_SET, atm2, 300.0);
        t2  = rp_astro_to_time(&ares.time);
    }
    rp_time_to_tm(t2, &times->set);
    //azimuth
    times->set_az=get_az(body, &ares.time, observer, NULL, NULL, NULL, NULL);


    return times;
}

rp_moon_times *rp_moon_gettimes(struct tm *tm, double lat, double lon, rp_moon_times *times)
{
    rp_time_t t, t2;
    astro_observer_t observer={0};
    astro_time_t atm;
    astro_search_result_t ares;
    astro_angle_result_t mphase;

    memset(times, 0, sizeof(rp_moon_times) );

    atm=rp_tm_to_astro(tm);

    observer.latitude=lat;
    observer.longitude=lon;

    //printastro(atm);

    ares  = Astronomy_SearchRiseSet(BODY_MOON,  observer, DIRECTION_RISE, atm, 300.0);
    t  = rp_astro_to_time(&ares.time);
    rp_time_to_tm(t, &times->rise);
    //azimuth
    times->rise_az=get_az(BODY_MOON, &ares.time, observer, NULL, NULL, NULL, NULL);


    ares  = Astronomy_SearchRiseSet(BODY_MOON,  observer, DIRECTION_SET, atm, 300.0);
    t2  = rp_astro_to_time(&ares.time);
    if((t2-t)<0)
    {
        t2 =t;
        astro_time_t atm2 = rp_time_to_astro(t2);
        ares  = Astronomy_SearchRiseSet(BODY_MOON,  observer, DIRECTION_SET, atm2, 300.0);
        t2  = rp_astro_to_time(&ares.time);
    }
    rp_time_to_tm(t2, &times->set);
    //azimuth
    times->set_az=get_az(BODY_MOON, &ares.time, observer, NULL, NULL, NULL, NULL);

    mphase=Astronomy_MoonPhase(atm);
    if(mphase.status != ASTRO_SUCCESS)
        times->phase = -1.0;
    else
    {
        times->phase = mphase.angle/360.0;
        times->illumination = illuminated_fraction( times->phase );
    }
        
    ares = Astronomy_SearchMoonPhase(180.0, atm, 30);
    t2  = rp_astro_to_time(&ares.time);
    rp_time_to_tm(t2, &times->full);

    ares = Astronomy_SearchMoonPhase(0.0, atm, 30);
    t2  = rp_astro_to_time(&ares.time);
    rp_time_to_tm(t2, &times->new);


    ares = Astronomy_SearchMoonPhase(90.0, atm, 30);
    t2  = rp_astro_to_time(&ares.time);
    rp_time_to_tm(t2, &times->first);


    ares = Astronomy_SearchMoonPhase(270.0, atm, 30);
    t2  = rp_astro_to_time(&ares.time);
    rp_time_to_tm(t2, &times->last);

    return times;
}


rp_sun_times *rp_sun_gettimes(struct tm *tm, double lat, double lon, rp_sun_times *times)
{
    rp_time_t t, t2, solarnoon_t;
    astro_observer_t observer={0};
    astro_time_t atm;
    astro_search_result_t ares;
    astro_hour_angle_t snoon;

    memset(times, 0, sizeof(rp_sun_times) );

    tm->tm_hour=0;
    tm->tm_min=0;
    tm->tm_sec=0;
    //printtm(tm);
    t=rp_tm_to_time(tm);

    // midnight(ish)
    t-= (rp_time_t) ( ((lon)/360.0)*86400 );

    atm=rp_time_to_astro(t);

    //printastro(atm);
    observer.latitude=lat;
    observer.longitude=lon;

    ares  = Astronomy_SearchRiseSet(BODY_SUN,  observer, DIRECTION_RISE, atm, 300.0);
    t  = rp_astro_to_time(&ares.time);
    rp_time_to_tm(t, &times->rise);
    solarnoon_t=t;

    //azimuth
    times->rise_az=get_az(BODY_SUN, &ares.time, observer, NULL, NULL, NULL, NULL);

    ares  = Astronomy_SearchRiseSet(BODY_SUN,  observer, DIRECTION_SET, atm, 300.0);
    t2  = rp_astro_to_time(&ares.time);
    if((t2-t)<0)
    {
        t2 =t;
        astro_time_t atm2 = rp_time_to_astro(t2);
        ares  = Astronomy_SearchRiseSet(BODY_SUN,  observer, DIRECTION_SET, atm2, 300.0);
        t2  = rp_astro_to_time(&ares.time);
    }

    //azimuth
    times->set_az=get_az(BODY_SUN, &ares.time, observer, NULL, NULL, NULL, NULL);

    rp_time_to_tm(t2, &times->set);
    times->daylen = (double)(t2-t)/3600.0;



    ares  = Astronomy_SearchAltitude(BODY_SUN,  observer, DIRECTION_RISE, atm, 300.0, -6.0);
    t  = rp_astro_to_time(&ares.time);
    rp_time_to_tm(t, &times->civ_start);

    ares  = Astronomy_SearchAltitude(BODY_SUN,  observer, DIRECTION_SET, atm, 300.0, -6.0);
    t2  = rp_astro_to_time(&ares.time);
    if((t2-t)<0)
    {
        t2 =t;
        astro_time_t atm2 = rp_time_to_astro(t2);
        ares  = Astronomy_SearchAltitude(BODY_SUN,  observer, DIRECTION_SET, atm2, 300.0, -6.0);
        t2  = rp_astro_to_time(&ares.time);
    }
    rp_time_to_tm(t2, &times->civ_end);
    times->civlen = (double)(t2-t)/3600.0;

    ares  = Astronomy_SearchAltitude(BODY_SUN,  observer, DIRECTION_RISE, atm, 300.0, -12.0);
    t  = rp_astro_to_time(&ares.time);
    rp_time_to_tm(t, &times->naut_start);

    ares  = Astronomy_SearchAltitude(BODY_SUN,  observer, DIRECTION_SET, atm, 300.0, -12.0);
    t2  = rp_astro_to_time(&ares.time);
    if((t2-t)<0)
    {
        t2 =t;
        astro_time_t atm2 = rp_time_to_astro(t2);
        ares  = Astronomy_SearchAltitude(BODY_SUN,  observer, DIRECTION_SET, atm2, 300.0, -12.0);
        t2  = rp_astro_to_time(&ares.time);
    }
    rp_time_to_tm(t2, &times->naut_end);
    times->nautlen = (double)(t2-t)/3600.0;

    ares  = Astronomy_SearchAltitude(BODY_SUN,  observer, DIRECTION_RISE, atm, 300.0, -18.0);
    t  = rp_astro_to_time(&ares.time);
    rp_time_to_tm(t, &times->astr_start);

    ares  = Astronomy_SearchAltitude(BODY_SUN,  observer, DIRECTION_SET, atm, 300.0, -18.0);
    t2  = rp_astro_to_time(&ares.time);
    if((t2-t)<0)
    {
        t2 =t;
        astro_time_t atm2 = rp_time_to_astro(t2);
        ares  = Astronomy_SearchAltitude(BODY_SUN,  observer, DIRECTION_SET, atm2, 300.0, -18.0);
        t2  = rp_astro_to_time(&ares.time);
    }
    rp_time_to_tm(t2, &times->astr_end);

    times->astrlen = (double)(t2-t)/3600.0;
    if(times->astrlen<0) times->astrlen=0;

    atm = rp_time_to_astro(solarnoon_t);
    snoon = Astronomy_SearchHourAngleEx(BODY_SUN,  observer, 0, atm,1);
    t  = rp_astro_to_time(&snoon.time);
    rp_time_to_tm(t, &times->solar_noon);

    return times;
}

/* 
    Gaussean Easter
    Adapted from:
    https://github.com/algorithm-archivists/algorithm-archive/blob/main/contents/computus/code/c/gauss_easter.c
    Copyright 2018 James Schloss et. al
    MIT license
*/

static void computus(int year, int *month, int *day)
{
    // Year's position on the 19 year metonic cycle
    int a = year % 19;

    // Century index
    int k = year / 100;

    //Shift of metonic cycle, add a day offset every 300 years
    int p = (13 + 8 * k) / 25;

    // Correction for non-observed leap days
    int q = k / 4;

    // Correction to starting point of calculation each century
    int M = (15 - p + k - q) % 30;

    // Number of days from March 21st until the full moon
    int d = (19 * a + M) % 30;

    // Finding the next Sunday
    // Century-based offset in weekly calculation
    int N = (4 + k - q) % 7;

    // Correction for leap days
    int b = year % 4;
    int c = year % 7;

    // Days from d to next Sunday
    int e = (2 * b + 4 * c + 6 * d + N) % 7;

    // Historical corrections for April 26 and 25
    if ((d == 29 && e == 6) || (d == 28 && e == 6 && a > 10))
        e = -1;

    *day=22 + d + e;
    if ( *day > 31)
    {
        *month=4;
        *day=d + e - 9;
    }
    else
        *month=3;
}

rp_time_t find_easter(int year)
{
    struct tm res_s={0}, *res=&res_s;
    rp_time_t t;
    int month, day;

    computus (year, &month, &day);

    res->tm_year=year-1900;
    res->tm_mon=month-1;
    res->tm_mday=day;

    t=rp_tm_to_time(res);

    return t;    
}

#define NYDF "01/01/%04d"
#define INDF "07/04/%04d"
#define JNTF "06/19/%04d"
#define CHRF "12/25/%04d"
#define VTNF "11/11/%04d"
#define OCTF "10/01/%04d"
#define NOVF "11/01/%04d"

//https://stackoverflow.com/questions/6054016/c-program-to-find-day-of-week-given-date
//gregorian only
static int set_wday(struct tm *tm)
{
    int d=tm->tm_mday, m=tm->tm_mon+1, y=tm->tm_year + 1900;
    tm->tm_wday = (d += m < 3 ? y-- : y - 2, 23*m/9 + d + 4 + y/4- y/100 + y/400)%7;
    return tm->tm_wday;
}

static inline int days_in_month(int month, int year)
{
    return (month == 2) ? 28 + (( !(year % 4) && (year % 100) ) || !(year % 400)) : 31 - (month - 1) % 7 % 2;
}

// nweeks must not be 0.  If looking for a monday, -1 is for last monday, 1 is for first monday.
static rp_time_t find_variable_holiday(int year, int startmon, int wday_match, int nweeks)
{
    int wday = 0;
    struct tm tm_s={0}, *tm=&tm_s;

    if (!nweeks)
        return 0;

    tm->tm_mday=1;
    tm->tm_mon=startmon-1;
    tm->tm_year=year-1900;
    if(nweeks < 0)
        tm->tm_mday=days_in_month(startmon, year);
    else
        nweeks--;

    wday=set_wday(tm);

    // if last day is the day of week, go back one less week
    if(nweeks < 0 && wday==wday_match)
        nweeks++;

    //go forward/back to desired week and add days to get our desired day of week
    tm->tm_mday += 7 * nweeks + ( wday_match - wday + 7) %7;

    return rp_tm_to_time(tm);
}

rp_time_t rp_find_holiday_usa(int year, int holiday)
{
    struct tm res_s={0}, *res=&res_s;
    char buf[64];

    switch (holiday)
    {    
        case easter:
        {
            return find_easter(year);
        }
        case newyear:
        {
            snprintf(buf, 64, NYDF, year);
            strptime(buf, "%m/%d/%Y", res);
            break;
        }
        case mlkjr:
        {
            rp_time_t t;
            int dow;

            snprintf(buf, 64, NYDF, year);
            strptime(buf, "%m/%d/%Y", res);
            t=rp_tm_to_time(res);
            dow=res->tm_wday;

            //find first monday
            while ( dow%7 != 1 )
            {
                dow++;
                t+=86400;
            }

            //two more mondays
            t+= 14*86400;
            return t;
        }
        case washington:
        {
            // 2=feb, 1=monday, 3=third monday
            return find_variable_holiday(year, 2, 1, 3);
        }
        case memorial:
        {
            // 5=may, 1=monday, -1=last monday
            return find_variable_holiday(year, 5, 1, -1);
        }
        case juneteenth:
        {
            snprintf(buf, 64, JNTF, year);
            strptime(buf, "%m/%d/%Y", res);
            break;
        }
        case independence:
        {
            snprintf(buf, 64, INDF, year);
            strptime(buf, "%m/%d/%Y", res);
            break;
        }
        case laborday:
        {
            // 9=sept, 1=monday, 1=first monday
            return find_variable_holiday(year, 9, 1, 1);
        }
        case indigenous:
        {
            // 10=oct, 1=monday, 2=second monday
            return find_variable_holiday(year, 10, 1, 2);
        }
        case veterans:
        {
            snprintf(buf, 64, VTNF, year);
            strptime(buf, "%m/%d/%Y", res);
            break;
        }
        case thanksgiving:
        {
            // 11=nov, 4=thursday, 3=third thursday
            return find_variable_holiday(year, 11, 4, 4);
        }
        case christmas:
        {
            snprintf(buf, 64, CHRF, year);
            strptime(buf, "%m/%d/%Y", res);
            break;
        }
    }

    return rp_tm_to_time(res);
}

rp_seasons * rp_get_seasons(int year, rp_seasons *seasons)
{
    astro_seasons_t astro_seasons = Astronomy_Seasons(year);

    rp_astro_to_tm(&astro_seasons.mar_equinox, &seasons->spring);

    rp_astro_to_tm(&astro_seasons.jun_solstice, &seasons->summer);

    rp_astro_to_tm(&astro_seasons.sep_equinox, &seasons->autumn);

    rp_astro_to_tm(&astro_seasons.dec_solstice, &seasons->winter);

    return seasons;
}

#ifdef TEST

time_t systemlocaloffset=0;

static char * timestring(struct tm *tp, char *buf)
{
    struct tm tmp={0}, *tmpp=&tmp;
    rp_time_t t;
    char *p=buf, c='+';
    int off=systemlocaloffset;
//    int tmpyear, havetmpyear=0;

/*
    //timegm may not handle years before 1900
    if(tp->tm_year<0)
    {
        tmpyear=tp->tm_year;
        tp->tm_year=1;
        havetmpyear=1;
    }
*/
    t = rp_tm_to_time(tp);
    t+=systemlocaloffset;

    tmpp=rp_time_to_tm(t, tmpp);

/*
    if(havetmpyear)
    {
        tmpp->tm_year=tmpyear;
        tp->tm_year = tmpyear;
    }
*/
    p+=strftime(buf, 128, "%c", tmpp); 

    if(off<0)
    {
        c='-';
        off*=-1;
    }

    sprintf(p, " %c%02d%02d", c, off/3600, off-(off/3600)*3600); 
    return buf;
}

#define getmin(hours) ((int)(hours*60) - ((int)hours) * 60)


const char *cr16_names[]={
    "N","NNE","NE","ENE",
    "E","ESE","SE","SSE",
    "S","SSW","SW","WSW",
    "W","WNW","NW","NNW"
};

const char *cr16(double az)
{
    int idx=0;

    if(az>360 || az < 0)
        return "";

    idx = ((int)((az+11.25)/22.5 ) ) % 16;

    return cr16_names[idx];
}

int main_sun(int argc, char *argv[], char *prog)
{
    double lon, lat, tz;
    char *p, buf[128];
    struct tm tm_s={0}, *tm=&tm_s;
    rp_sun_times sr_s={0}, *sr=&sr_s;

    if(argc < 4)
    {
        printf("usage: %s sun mm/dd/yyyy lat lon [offset_hours]\n", prog);
        printf("   example: %s sun 07/22/2024 37.91 -122.3 -7\n", prog);
        exit(0);
    }

    p = strptime(argv[1], "%m/%d/%Y", tm);

    if(!p)
    {
        printf("could not parse date\n");
        printf("usage: %s sun mm/dd/yyyy lat lon [offset_hours]\n", prog);
        printf("   example: %s sun 07/22/2024 37.91 -122.3 -7\n", prog);
        exit(0);
    }

    sscanf(argv[2], "%lf", &lat);
    sscanf(argv[3], "%lf", &lon);

    if(argc > 4)
    {
        sscanf(argv[4], "%lf", &tz);
        systemlocaloffset = (time_t)(tz * 3600);
    }
    else
    {
        struct tm now, *now_p;
        time_t nowtime, gmt;

        time(&nowtime);
        now_p = gmtime_r(&nowtime, &now);

        gmt = timegm(now_p);
        systemlocaloffset = timegm(localtime(&gmt)) - gmt;
        //tz=(double)systemlocaloffset/3600.0;
    }

    //rp_tm_to_local(tm, tz);

    sr = rp_sun_gettimes(tm,lat,lon,sr);

    printf( "Day length:                      %2d hour%c %d minutes\n",  (int)sr->daylen,  ((int)sr->daylen)!=1?'s':' ',  getmin(sr->daylen) );
    printf( "With civil twilight              %2d hour%c %2d minutes\n", (int)sr->civlen,  ((int)sr->civlen)!=1?'s':' ',  getmin(sr->civlen) );
    printf( "With nautical twilight           %2d hour%c %2d minutes\n", (int)sr->nautlen, ((int)sr->nautlen)!=1?'s':' ', getmin(sr->nautlen) );
    printf( "With astronomical twilight       %2d hour%c %2d minutes\n", (int)sr->astrlen, ((int)sr->astrlen)!=1?'s':' ', getmin(sr->astrlen) );

    /* this makes no sense near the poles, where a daylen can be many days long
    double d = (sr->civlen-sr->daylen)/2.0;
    printf( "Length of twilight: civil        %2d hour%c %2d minutes\n", (int)d, ((int)d)!=1?'s':' ', getmin(d));

    d=(sr->nautlen-sr->daylen)/2.0;
    printf( "                    nautical     %2d hour%c %2d minutes\n", (int)d, ((int)d)!=1?'s':' ', getmin(d));

    d=(sr->astrlen-sr->daylen)/2.0;
    printf( "                    astronomical %2d hour%c %2d minutes\n", (int)d, ((int)d)!=1?'s':' ', getmin(d));
    */

    printf( "Solar noon:                   %s\n", timestring(&sr->solar_noon, buf) );

    printf( "Sunrise:                      %s\n", timestring(&sr->rise, buf) );
    printf( "Direction of Sunrise          %.2f degrees (%s)\n", sr->rise_az, cr16(sr->rise_az));
    printf( "Sunset:                       %s\n", timestring(&sr->set, buf) );
    printf( "Direction of Sunset           %.2f degrees (%s)\n", sr->set_az, cr16(sr->set_az));

    printf( "Civil Twilight Starts:        %s\n", timestring(&sr->civ_start, buf) );
    printf( "Civil Twilight Ends:          %s\n", timestring(&sr->civ_end,   buf) );

    printf( "Nautical Twilight Starts:     %s\n", timestring(&sr->naut_start, buf) );
    printf( "Nautical Twilight Ends:       %s\n", timestring(&sr->naut_end,   buf) );

    printf( "Astronomical Twilight Starts: %s\n", timestring(&sr->astr_start, buf) );
    printf( "Astronomical Twilight Ends:   %s\n", timestring(&sr->astr_end,   buf) );

    return 0;

}

void printtime(char *s, struct tm *tm)
{
    char buf[128];
    strftime(buf, 128, "%c", tm);
    printf("%s %s\n",s,buf);
}

int main_moon(int argc, char *argv[], char *prog)
{
    double lon, lat, tz=0;
    char *p, buf[128];
    struct tm tm_s={0}, *tm=&tm_s;
    rp_moon_times mr_s={0}, *mr=&mr_s;

    if(argc < 4)
    {
        printf("usage: %s moon \"mm/dd/yyyy [HH:MM[:SS]]\" lat lon [offset_hours]\n", prog);
        printf("   example: %s moon \"07/22/2024 22:00\" 37.91 -122.3 -7\n", prog);
        return 0;
    }

    p = strptime(argv[1], "%m/%d/%Y %H:%M:%S", tm);
    if (p)
        goto gotdate;

    p = strptime(argv[1], "%m/%d/%Y %H:%M", tm);
    if (p)
        goto gotdate;

    p = strptime(argv[1], "%m/%d/%Y", tm);

    if(!p)
    {
        printf("could not parse date\n");
        printf("usage: %s moon \"mm/dd/yyyy [HH:MM[:SS]]\" lat lon [offset_hours]\n", prog);
        printf("   example: %s moon \"07/22/2024 22:00\" 37.91 -122.3 -7\n", prog);
        return 1;
    }

    gotdate:

    if(argc > 4)
    {
        sscanf(argv[4], "%lf", &tz);
        systemlocaloffset = (time_t)(tz * 3600);
    }
    else
    {
        struct tm now, *now_p;
        time_t nowtime, gmt;

        time(&nowtime);
        now_p = gmtime_r(&nowtime, &now);

        gmt = timegm(now_p);
        systemlocaloffset = timegm(localtime(&gmt)) - gmt;
        tz=(double)systemlocaloffset/3600.0;
    }

    rp_tm_to_local(tm, tz);

    sscanf(argv[2], "%lf", &lat);
    sscanf(argv[3], "%lf", &lon);

    

    mr = rp_moon_gettimes(tm,lat,lon,mr);

    printf( "New Moon:               %s\n",     timestring(&mr->new, buf) );
    printf( "First Quarter:          %s\n",     timestring(&mr->first, buf) );
    printf( "Full Moon:              %s\n",     timestring(&mr->full, buf) );
    printf( "Last  Quarter:          %s\n",     timestring(&mr->last, buf) );

    printf( "Moonrise:               %s\n",     timestring(&mr->rise, buf) );
    printf( "Direction of Moonrise   %.2f degrees\n", mr->rise_az);
    printf( "Moonset:                %s\n",     timestring(&mr->set, buf) );
    printf( "Direction of Moonset    %.2f degrees\n", mr->set_az);
    printf( "Moon Phase:            %%%.2f\n", mr->phase * 100.0);
    printf( "Moon Illumination:     %%%.2f\n", mr->illumination * 100.0);

    return 0;
}

int main_planets(int argc, char *argv[], char *prog)
{
    static const astro_body_t body[] = {
        BODY_SUN, BODY_MOON, BODY_MERCURY, BODY_VENUS, BODY_MARS,
        BODY_JUPITER, BODY_SATURN, BODY_URANUS, BODY_NEPTUNE, BODY_PLUTO
    };
    int i=0, num_bodies = sizeof(body) / sizeof(body[0]);
    rp_body_times times_s={0}, *times=&times_s;
    struct tm tm_s={0}, *tm=&tm_s;
    char *p, buf[128];
    double lat=0, lon=0, tz=0;

    if(argc < 4)
    {
        printf("usage: %s planets mm/dd/yyyy lat lon [offset_hours]\n", prog);
        printf("   example: %s planets 07/22/2024 37.91 -122.3 -7\n", prog);
        exit(0);
    }

    p = strptime(argv[1], "%m/%d/%Y %H:%M:%S", tm);
    if (p)
        goto gotdate;

    p = strptime(argv[1], "%m/%d/%Y %H:%M", tm);
    if (p)
        goto gotdate;

    p = strptime(argv[1], "%m/%d/%Y", tm);

    if(!p)
    {
        printf("could not parse date\n");
        printf("usage: %s planets mm/dd/yyyy lat lon [offset_hours]\n", prog);
        printf("   example: %s planets 07/22/2024 37.91 -122.3 -7\n", prog);
        return 1;
    }

    gotdate:

    sscanf(argv[2], "%lf", &lat);
    sscanf(argv[3], "%lf", &lon);

    if(argc > 4)
    {
        sscanf(argv[4], "%lf", &tz);
        systemlocaloffset = (time_t)(tz * 3600);
    }
    else
    {
        struct tm now, *now_p;
        time_t nowtime, gmt;

        time(&nowtime);
        now_p = gmtime_r(&nowtime, &now);

        gmt = timegm(now_p);
        systemlocaloffset = timegm(localtime(&gmt)) - gmt;
        tz=(double)systemlocaloffset/3600.0;
    }

    rp_tm_to_local(tm, tz);

    printf("BODY           RA      DEC       AZ      ALT\n");
    for (i=0; i < num_bodies; ++i)
    {
        times = rp_body_gettimes(tm, body[i], lat, lon, times);
        printf("%-8s %8.2lf %8.2lf %8.2lf %8.2lf\n", Astronomy_BodyName(body[i]),
            times->cur_ra, times->cur_dec, times->cur_az, times->cur_alt);

        printf("    next rise: %s\n", timestring(&times->rise, buf) );
        printf("    next set:  %s\n", timestring(&times->set, buf) );
    }

    return 0;
}

int main_julian_r(int argc, char *argv[], char *prog)
{
    struct tm d={0}, *dp=&d;
    rp_jd_t jd;
    char buf[128];

    if(argc < 2)
    {
        printf("usage: %s fromjulian \"nnnnnnn.nnnn\"\n", prog);
        exit(0);
    }

    sscanf(argv[1], "%lf", &jd);
    dp=rp_jd_to_tm(jd, dp);
    strftime(buf, 128, "%c", dp);
    printf("Julian day %f is %s\n", jd, buf);

    return 0;
}

int main_julian(int argc, char *argv[], char *prog)
{
    struct tm d={0}, *dp=&d;
    char *p, buf[128];

    if(argc < 2)
    {
        printf("usage: %s tojulian \"mm/dd/yyyy [hh:mm[:ss]]\"\n", prog);
        exit(0);
    }

    p = strptime(argv[1], "%m/%d/%Y %H:%M:%S", dp);
    if (p)
        goto gotdate;

    p = strptime(argv[1], "%m/%d/%Y %H:%M", dp);
    if (p)
        goto gotdate;

    p = strptime(argv[1], "%m/%d/%Y", dp);

    if(!p)
    {
        printf("bad date '%s' \nusage: %s tojulian \"mm/dd/yyyy [hh:mm[:ss]]\"\n", argv[1], prog);
        exit(1);
    }

    gotdate:

    strftime(buf, 128, "%c", dp);
    printf("Julian day for %s is %f\n", buf, rp_tm_to_jd(dp) );

    return 0;
}


int main_easter(int argc, char *argv[], char *prog)
{
    int year;
    rp_time_t t;
    struct tm res_s={0}, *res=&res_s;
    char buf[128];

    if(argc<2)
    {
        printf("usage %s easter dddd (year)\n", prog);
        return 0;
    }

    sscanf(argv[1], "%d", &year);
    
    t=find_easter(year);

    //easter
    res = rp_time_to_tm(t, res);

    strftime(buf, 128, "%A %B %d, %Y", res);
    printf("\nEaster:  %s\n", buf);

    return 0;
}

int tste[][3]={
    {1590,4,22},
    {1598,3,22},
    {1622,3,27},
    {1629,4,15},
    {1666,4,25},
    {1680,4,21},
    {1685,4,22},
    {1693,3,22},
    {1700,4,11},
    {1724,4,16},
    {1744,4,5},
    {1778,4,19},
    {1798,4,8},
    {1802,4,18},
    {1818,3,22},
    {1825,4,3},
    {1845,3,23},
    {1876,4,16},
    {1900,4,15},
    {1903,4,12},
    {1923,4,1},
    {1924,4,20},
    {1927,4,17},
    {1943,4,25},
    {1954,4,18},
    {1962,4,22},
    {1967,3,26},
    {1974,4,14},
    {2019,4,21},
    {2038,4,25},
    {2045,4,9},
    {2049,4,18},
    {2057,4,22},
    {2069,4,14},
    {2076,4,19},
    {2089,4,3},
//    {,,},
    {-1,-1,-1}
};

int test_easter()
{
    int *tsts, i=0;
    rp_time_t t;
    struct tm res_s, *res=&res_s;
    int fails=0;
    while(1)
    {
        tsts = tste[i];
        if(tsts[0]==-1)
            break;

        t=find_easter(tsts[0]);
        res=rp_time_to_tm(t,res);
        if( res->tm_mday!=tsts[2])
        {
            printf("%d-%d-%d\n", tsts[0], tsts[1], tsts[2]);
            printf("%d-%d-%d\n", 
                res->tm_year+1900, res->tm_mon+1, res->tm_mday);
            (void)find_easter(tsts[0]);
            putchar('\n');
            fails++;
        }
        i++;
    }
    if(!fails)
        printf("all paradoxical dates calculated correctly\n");
    else
    {
        printf("there were failures\n");
        exit(1);
    }

    return 0;
}

const char *usa_holiday_names[] = {
    "Easter",
    "New Years Day",
    "Martin Luther King Jr's Birthday (observed)",
    "Washington's Birthday (observed)",
    "Memorial Day",
    "Juneteenth National Independence Day",
    "Independence Day",
    "Labor Day",
    "Columbus Day / Indigenous Peoples' Day",
    "Veteran's Day",
    "Thanksgiving Day",
    "Christmas Day"
};

int main_holidays(int argc, char *argv[], char *prog)
{
    int year=0;
    char buf[128];
    struct tm res_s={0}, *res=&res_s;
    int i;
    rp_time_t t;

    if(argc<2)
    {
        printf("usage %s holidays year\n", prog);
        return 0;
    }

    sscanf(argv[1], "%d", &year);

    for (i=0; i<nholidays;i++)
    {
        t=rp_find_holiday_usa(year,i);
        res=rp_time_to_tm(t, res);
        strftime(buf, 128, "%A %B %d %Y", res);

        printf("%-45s is on %s\n", usa_holiday_names[i], buf);
    }
    return 0;    
}

int main_seasons(int argc, char *argv[], char *prog)
{
    int year;
    char buf[128];
    rp_seasons seasons_s={0}, *seasons=&seasons_s;

    if(argc<2)
    {
        printf("usage %s seasons year\n", prog);
        return 0;
    }

    sscanf(argv[1], "%d", &year);

    seasons = rp_get_seasons(year, seasons);

    strftime(buf, 128, "%c", &seasons->spring);
    printf("spring equinox:  %s\n", buf);

    strftime(buf, 128, "%c", &seasons->summer);
    printf("summer solstice: %s\n", buf);

    strftime(buf, 128, "%c", &seasons->autumn);
    printf("autumn equinox:  %s\n", buf);

    strftime(buf, 128, "%c", &seasons->winter);
    printf("winter solstice: %s\n", buf);

    
    return 0;
}        

int main(int argc, char **argv) {
    char *comm, *prog=argv[0];

    if(argc<2)
    {
        printf("usage %s [sun|moon|planets|seasons|easter|tojulian|fromjulian|holidays] ...\n");
        return 1;
    }
    comm=argv[1];
    argv++;
    argc--;

    if(strcmp("moon", comm)==0)
    {
        return main_moon(argc, argv, prog);
    }

    if(strcmp("sun", comm)==0)
    {
        return main_sun(argc, argv, prog);
    }

    if(strcmp("planets", comm)==0)
    {
        return main_planets(argc, argv, prog);
    }

    if(strcmp("seasons", comm)==0)
    {
        return main_seasons(argc, argv, prog);
    }

    if(strcmp("easter", comm)==0)
    {
        return main_easter(argc, argv, prog);
    }

    if(strcmp("eastertest", comm)==0)
    {
        return test_easter();
    }

    if(strcmp("tojulian", comm)==0)
    {
        return main_julian(argc, argv, prog);
    }

    if(strcmp("fromjulian", comm)==0)
    {
        return main_julian_r(argc, argv, prog);
    }

    if(strcmp("holidays", comm)==0)
    {
        return main_holidays(argc, argv, prog);
    }

    printf("unknown command '%s'\n", comm);

    return 1;
}

#endif
