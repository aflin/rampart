/*
    almanac.h

    Copyright (c) 2024 Aaron Flin
    MIT LICENSED
    https://opensource.org/license/mit
*/

#ifndef RP_ALMANAC
#define RP_ALMANAC

#include <stdint.h>
#include "astronomy.h"

#define PI        3.1415926535897932384

// 64 bit time_t on 32 bit systems
typedef int64_t rp_time_t;

// julian date type
typedef double rp_jd_t;


/* ************************\
|*    CONVERSIONS          |
\**************************/

// conversions between julian dates, struct tm and rp_time_t

rp_jd_t        rp_time_to_jd(rp_time_t t);
rp_jd_t        rp_cal_to_jd(int y, int m, int d, int h, int M, int s);
rp_jd_t        rp_tm_to_jd(struct tm *d);

rp_time_t      rp_jd_to_time(rp_jd_t jd);
rp_time_t      rp_tm_to_time(struct tm *d);

struct tm    * rp_jd_to_tm(rp_jd_t jd, struct tm *ret);
struct tm    * rp_time_to_tm(rp_time_t t, struct tm *tm);

#define rp_tm_to_local(tm, offset) do{  \
  if(offset!=0.0){                      \
    rp_time_t t=rp_tm_to_time(tm);      \
    t-= (rp_time_t)( offset * 3600.0 ); \
    tm=rp_time_to_tm(t, tm);            \
  }                                     \
} while(0)

#define rp_tm_from_local(tm, offset) rp_tm_to_local(tm, -1 * offset)

/* ************************\
|*          SUN            |
\**************************/

typedef struct {
    struct tm rise;             // sunrise
    struct tm set;              // sunset
    struct tm civ_start;        // civil twilight start
    struct tm civ_end;          // civil twilight end
    struct tm naut_start;       // nautical twilight start
    struct tm naut_end;         // nautical twilight end
    struct tm astr_start;       // astronomical twilight start
    struct tm astr_end;         // astronomical twilight end
    struct tm solar_noon;       // sunrise + (sunset-sunrise)/2
    double    rise_az;          // compass bearing at sunrise
    double    set_az;           // compass bearing at sunset
    double daylen;              // length of the day
    double civlen;              // length of day + civil twilight
    double nautlen;             // length of day + nautical twilight
    double astrlen;             // length of day + astronomical twilight
} rp_sun_times;

// get sunrise, sunset, twilight for a given day
rp_sun_times *rp_sun_gettimes(struct tm *tm, double lat, double lon, rp_sun_times *times);


/* ************************\
|*         MOON            |
\**************************/

typedef struct {
    struct tm rise;             // moonrise
    double    rise_az;          // moonrise azimuth
    struct tm set;              // moonset
    double    set_az;           // moonset azimuth
    double    phase;            // current phase 0.0 <1.0
    double    illumination;     // current illumination
    struct tm new;              // newmoon
    struct tm first;            // first quarter
    struct tm full;             // full
    struct tm last;             // last quarter
} rp_moon_times;

rp_moon_times *rp_moon_gettimes(struct tm *tm, double lat, double lon, rp_moon_times *times);


/* ************************\
|*       PLANETS           |
\**************************/

typedef struct {
    struct tm rise;             // planet rise
    double    rise_az;          // planet rise azimuth
    struct tm set;              // planet set
    double    set_az;           // planet set azimuth
    double    cur_az;           // current azimuth  (as set in struct tm)
    double    cur_alt;          // current altitude
    double    cur_ra;           // current right ascension
    double    cur_dec;          // current delcination
} rp_body_times;

//astro_body_t = [ BODY_SUN | BODY_MOON | BODY_MERCURY | BODY_VENUS | BODY_MARS |
//                 BODY_JUPITER | BODY_SATURN | BODY_URANUS | BODY_NEPTUNE | BODY_PLUTO ]
rp_body_times *rp_body_gettimes(struct tm *tm, astro_body_t body, double lat, double lon, rp_body_times *times);


/* ************************\
|*         EASTER          |
\**************************/

// find official easter using gauss computus
rp_time_t find_easter(int year);


/* ************************\
|*        SEASONS          |
\**************************/

typedef struct {
    struct tm spring;
    struct tm summer;
    struct tm autumn;
    struct tm winter;
} rp_seasons;

rp_seasons * rp_get_seasons(int year, rp_seasons *seasons);


/* ************************\
|*      US HOLIDAYS        |
\**************************/

#define nholidays           12

#define easter              0
#define newyear             1
#define mlkjr               2
#define martinlutherkingday 2
#define washington          3
#define memorial            4
#define juneteenth          5
#define independence        6
#define independenceday     6
#define laborday            7
#define columbus            8
#define indigenous          8
#define veterans            9
#define veteransday         9
#define thanksgiving        10
#define christmas           11

rp_time_t rp_find_holiday_usa(int year, int holiday);

#endif

