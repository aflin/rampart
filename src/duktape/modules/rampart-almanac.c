/* Copyright (C) 2024  Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include "rampart.h"
#include "almanac.h"
#include "../core/module.h"

static struct tm * date_to_tm(duk_context *ctx, duk_idx_t idx, struct tm *tm)
{
    rp_time_t t;

    duk_push_string(ctx, "getTime");
    duk_call_prop(ctx, idx, 0);

    t = (rp_time_t)( (duk_get_number(ctx, -1)/1000.0) );
    duk_pop(ctx);

    if(t < -12219292800) // Oct 15 1582
    {
        if(t > -12220156801) //unadjusted Oct 4th 23:59:59
            RP_THROW(ctx, "almanac: Invalid Gregorian Date: 10/4/1582 was immediately followed by 10/15/1582.  Dates between are invalid.\n");
        t += 86400 * 10;
    }
        
    return rp_time_to_tm(t, tm); 
} 

static void push_tm_to_date(duk_context *ctx, struct tm *tm)
{
    duk_get_global_string(ctx, "Date");
    duk_push_number(ctx, 1000.0 * (double) rp_tm_to_time(tm) );
    duk_new(ctx, 1);
}

duk_ret_t get_seasons(duk_context *ctx)
{
    int year = REQUIRE_INT(ctx, 0, "almanac.seasons() - argument must be an Int (year)");
    rp_seasons seasons_s={0}, *seasons;

    seasons = rp_get_seasons(year, &seasons_s);

    duk_push_object(ctx);

    push_tm_to_date(ctx, &seasons->spring);
    duk_put_prop_string(ctx, -2, "spring");

    push_tm_to_date(ctx, &seasons->summer);
    duk_put_prop_string(ctx, -2, "summer");

    push_tm_to_date(ctx, &seasons->autumn);
    duk_put_prop_string(ctx, -2, "autumn");

    push_tm_to_date(ctx, &seasons->winter);
    duk_put_prop_string(ctx, -2, "winter");

    return 1;
}

duk_ret_t get_sun(duk_context *ctx)
{
    double lon, lat;
    struct tm tm_s={0}, *tm=&tm_s;
    rp_sun_times sr_s={0}, *sr=&sr_s;
    duk_idx_t date_idx=0;

    int type = rp_gettype(ctx, 0);
    if(type==RP_TYPE_DATE)
        date_idx=0;
    else if(type==RP_TYPE_STRING)
    {
        duk_push_c_function(ctx, rp_auto_scandate, 1);
        duk_dup(ctx, 0);
        duk_call(ctx, 1);

        if(duk_is_null(ctx, -1))
            RP_THROW(ctx, "almanac.suntimes() - Invalid date string '%s'", duk_get_string(ctx, 0) );

        if(duk_get_prop_string(ctx, -1, "errMsg"))
            RP_THROW(ctx, "almanac.suntimes() - Invalid date string - %s", duk_get_string(ctx, -1));
        duk_pop(ctx);

        duk_get_prop_string(ctx, -1, "date");
        date_idx=duk_normalize_index(ctx, -1);
    }
    else
        RP_THROW(ctx, "almanac.suntimes() - First argument must be a Date or a String");

    lat = REQUIRE_NUMBER(ctx, 1, "almanac.suntimes() - Second argument must be a Number (latitude)");
    if(lat > 90.0 || lat < -90.0)
        RP_THROW(ctx, "almanac.suntimes() - Invalid latitude '%s'", duk_to_string(ctx, 1) );

    lon = REQUIRE_NUMBER(ctx, 2, "almanac.suntimes() - Third argument must be a Number (longitude)");
    if(lon > 180.0 || lon < -180.0)
        RP_THROW(ctx, "almanac.suntimes() - Invalid longitude '%s'", duk_to_string(ctx, 2) );

    tm=date_to_tm(ctx, date_idx, tm);

    sr = rp_sun_gettimes(tm,lat,lon,sr);

    duk_push_object(ctx);

    duk_push_number(ctx, sr->daylen);
    duk_put_prop_string(ctx, -2, "daylightHours");

    duk_push_number(ctx, sr->civlen);
    duk_put_prop_string(ctx, -2, "civilTwilightHours");

    duk_push_number(ctx, sr->nautlen);
    duk_put_prop_string(ctx, -2, "nauticalTwilightHours");

    duk_push_number(ctx, sr->astrlen);
    duk_put_prop_string(ctx, -2, "astronomicalTwilightHours");

    push_tm_to_date(ctx, &sr->solar_noon);
    duk_put_prop_string(ctx, -2, "solarNoon");

    push_tm_to_date(ctx, &sr->rise);
    duk_put_prop_string(ctx, -2, "sunrise");

    push_tm_to_date(ctx, &sr->set);
    duk_put_prop_string(ctx, -2, "sunset");

    push_tm_to_date(ctx, &sr->civ_start);
    duk_put_prop_string(ctx, -2, "civilTwilightStart");

    push_tm_to_date(ctx, &sr->civ_end);
    duk_put_prop_string(ctx, -2, "civilTwilightEnd");

    push_tm_to_date(ctx, &sr->naut_start);
    duk_put_prop_string(ctx, -2, "nauticalTwilightStart");

    push_tm_to_date(ctx, &sr->naut_end);
    duk_put_prop_string(ctx, -2, "nauticalTwilightEnd");

    push_tm_to_date(ctx, &sr->astr_start);
    duk_put_prop_string(ctx, -2, "astronomicalTwilightStart");

    push_tm_to_date(ctx, &sr->astr_end);
    duk_put_prop_string(ctx, -2, "astronomicalTwilightEnd");

    duk_push_number(ctx, sr->rise_az);
    duk_put_prop_string(ctx, -2, "sunriseAzimuth");

    duk_push_number(ctx, sr->set_az);
    duk_put_prop_string(ctx, -2, "sunsetAzimuth");

    return 1;
}


duk_ret_t get_moon(duk_context *ctx)
{
    double lon, lat;
    struct tm tm_s={0}, *tm=&tm_s;
    rp_moon_times mr_s={0}, *mr=&mr_s;
    duk_idx_t date_idx=0;

    int type = rp_gettype(ctx, 0);
    if(type==RP_TYPE_DATE)
        date_idx=0;
    else if(type==RP_TYPE_STRING)
    {
        duk_push_c_function(ctx, rp_auto_scandate, 1);
        duk_dup(ctx, 0);
        duk_call(ctx, 1);

        if(duk_is_null(ctx, -1))
            RP_THROW(ctx, "almanac.moontimes() - Invalid date string '%s'", duk_get_string(ctx, 0) );

        if(duk_get_prop_string(ctx, -1, "errMsg"))
            RP_THROW(ctx, "almanac.moontimes() - Invalid date string - %s", duk_get_string(ctx, -1));
        duk_pop(ctx);

        duk_get_prop_string(ctx, -1, "date");
        date_idx=duk_normalize_index(ctx, -1);
    }
    else
        RP_THROW(ctx, "almanac.moontimes() - First argument must be a Date or a String");

    lat = REQUIRE_NUMBER(ctx, 1, "almanac.moontimes() - Second argument must be a Number (latitude)");
    if(lat > 90.0 || lat < -90.0)
        RP_THROW(ctx, "almanac.moontimes() - Invalid latitude '%s'", duk_to_string(ctx, 1) );

    lon = REQUIRE_NUMBER(ctx, 2, "almanac.moontimes() - Third argument must be a Number (longitude)");
    if(lon > 180.0 || lon < -180.0)
        RP_THROW(ctx, "almanac.moontimes() - Invalid longitude '%s'", duk_to_string(ctx, 2) );

    tm=date_to_tm(ctx, date_idx, tm);

    mr = rp_moon_gettimes(tm,lat,lon,mr);

    duk_push_object(ctx);

    push_tm_to_date(ctx, &mr->rise);
    duk_put_prop_string(ctx, -2, "moonrise");

    push_tm_to_date(ctx, &mr->set);
    duk_put_prop_string(ctx, -2, "moonset");

    push_tm_to_date(ctx, &mr->new);
    duk_put_prop_string(ctx, -2, "newMoon");

    push_tm_to_date(ctx, &mr->first);
    duk_put_prop_string(ctx, -2, "firstQuarter");
    
    push_tm_to_date(ctx, &mr->full);
    duk_put_prop_string(ctx, -2, "fullMoon");

    push_tm_to_date(ctx, &mr->last);
    duk_put_prop_string(ctx, -2, "lastQuarter");

    duk_push_number(ctx, mr->rise_az);
    duk_put_prop_string(ctx, -2, "moonriseAzimuth");

    duk_push_number(ctx, mr->set_az);
    duk_put_prop_string(ctx, -2, "moonsetAzimuth");

    duk_push_number(ctx, mr->phase);
    duk_put_prop_string(ctx, -2, "moonPhase");

    duk_push_number(ctx, mr->illumination);
    duk_put_prop_string(ctx, -2, "moonIllumination");

    return 1;
}

static char *celnames[]={
    "sun", "moon", "mercury", "venus", "mars",
    "jupiter", "saturn", "uranus", "neptune", "pluto"
};

duk_ret_t get_planets(duk_context *ctx){

    static const astro_body_t body[] = {
        BODY_SUN, BODY_MOON, BODY_MERCURY, BODY_VENUS, BODY_MARS,
        BODY_JUPITER, BODY_SATURN, BODY_URANUS, BODY_NEPTUNE, BODY_PLUTO
    };
    int i=0, num_bodies = sizeof(body) / sizeof(body[0]);
    rp_body_times times_s={0}, *times=&times_s;
    struct tm tm_s={0}, *tm=&tm_s;
    double lat=0, lon=0;
    duk_idx_t date_idx=0;

    int type = rp_gettype(ctx, 0);
    if(type==RP_TYPE_DATE)
        date_idx=0;
    else if(type==RP_TYPE_STRING)
    {
        duk_push_c_function(ctx, rp_auto_scandate, 1);
        duk_dup(ctx, 0);
        duk_call(ctx, 1);

        if(duk_is_null(ctx, -1))
            RP_THROW(ctx, "almanac.celestials() - Invalid date string '%s'", duk_get_string(ctx, 0) );

        if(duk_get_prop_string(ctx, -1, "errMsg"))
            RP_THROW(ctx, "almanac.celestials() - Invalid date string - %s", duk_get_string(ctx, -1));
        duk_pop(ctx);

        duk_get_prop_string(ctx, -1, "date");
        date_idx=duk_normalize_index(ctx, -1);
    }
    else
        RP_THROW(ctx, "almanac.celestials() - First argument must be a Date or a String");

    lat = REQUIRE_NUMBER(ctx, 1, "almanac.celestials() - Second argument must be a Number (latitude)");
    if(lat > 90.0 || lat < -90.0)
        RP_THROW(ctx, "almanac.celestials() - Invalid latitude '%s'", duk_to_string(ctx, 1) );

    lon = REQUIRE_NUMBER(ctx, 2, "almanac.celestials() - Third argument must be a Number (longitude)");
    if(lon > 180.0 || lon < -180.0)
        RP_THROW(ctx, "almanac.celestials() - Invalid longitude '%s'", duk_to_string(ctx, 2) );

    tm=date_to_tm(ctx, date_idx, tm);

    duk_push_object(ctx);
    //printf("BODY           RA      DEC       AZ      ALT\n");
    for (i=0; i < num_bodies; ++i)
    {
        duk_push_object(ctx);
        times = rp_body_gettimes(tm, body[i], lat, lon, times);

        duk_push_number(ctx, times->cur_ra);
        duk_put_prop_string(ctx, -2, "currentRightAscension");

        duk_push_number(ctx, times->cur_dec);
        duk_put_prop_string(ctx, -2, "currentDeclination");

        duk_push_number(ctx, times->cur_az);
        duk_put_prop_string(ctx, -2, "currentAzimuth");

        duk_push_number(ctx, times->cur_alt);
        duk_put_prop_string(ctx, -2, "currentAltitude");

        push_tm_to_date(ctx, &times->rise);
        duk_put_prop_string(ctx, -2, "nextRise");

        push_tm_to_date(ctx, &times->set);
        duk_put_prop_string(ctx, -2, "nextSet");

        duk_put_prop_string(ctx, -2, celnames[i]);
    }

    return 1;
}

duk_ret_t holiday_err(duk_context *ctx)
{
    RP_THROW(ctx, "The rampart-date-holidays.js file was not found or did not compile.");
    return 0;
}

/* **************************************************
   Initialize module
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_object(ctx);
    duk_push_c_function(ctx, get_seasons, 1);
    duk_put_prop_string(ctx, -2, "seasons");

    duk_push_c_function(ctx, get_sun, 3);
    duk_put_prop_string(ctx, -2, "suntimes");

    duk_push_c_function(ctx, get_moon, 3);
    duk_put_prop_string(ctx, -2, "moontimes");

    duk_push_c_function(ctx, get_planets, 3);
    duk_put_prop_string(ctx, -2, "celestials");

    int ret = duk_rp_resolve(ctx, "rampart-date-holidays.js");

    if(ret==1)
    {
        duk_get_prop_string(ctx, -1, "exports");
        duk_put_prop_string(ctx, -3, "holidays");
    }
    else    
    {
        duk_push_c_function(ctx, holiday_err, 0);
        duk_put_prop_string(ctx, -3, "holidays");
    }

    duk_pop(ctx);


    return 1;
}
