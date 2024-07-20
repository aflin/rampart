/* 
    rp_tz.h
    copyright (c) 2024 Aaron Flin
    LICENSE: MIT
*/

#ifndef RP_TZ_H
#define RP_TZ_H

#include <stdint.h>

#define TZ_MAGIC "TZif"
#define TZ_MAX_TIMES 2000
#define TZ_MAX_TYPES 256
#define TZ_MAX_CHARS 50

#define RP_TIMEZONE_PATH "/usr/share/zoneinfo"

//warn on file load error.  There will be files in /usr/share/zoneinfo that aren't zone files
//so there will always be warnings if this line is uncommented
//#define WARN

// main macro for malloc uses realloc.  Set var to null to initiate
#define TZ_REMALLOC(s, t) do{                                     \
    (s) = realloc((s), (t));                                      \
    if ((char *)(s) == (char *)NULL)                              \
    {                                                             \
        fprintf(stderr, "error: realloc(var, %d) in %s at %d\n",  \
            (int)(t), __FILE__, __LINE__);                        \
        abort();                                                  \
    }                                                             \
} while(0)

#define TZ_CALLOC(s, t) do{                                       \
    (s) = calloc(1, (t));                                         \
    if ((char *)(s) == (char *)NULL)                              \
    {                                                             \
        fprintf(stderr, "error: calloc(var, %d) in %s at %d\n",   \
            (int)(t), __FILE__, __LINE__);                        \
        abort();                                                  \
    }                                                             \
} while(0)


/* info from timezone file header */
typedef struct {
    char magic[4];
    char version;
    char reserved[15];
    unsigned char tzh_ttisgmtcnt[4];
    unsigned char tzh_ttisstdcnt[4];
    unsigned char tzh_leapcnt[4];
    unsigned char tzh_timecnt[4];
    unsigned char tzh_typecnt[4];
    unsigned char tzh_charcnt[4];
} rp_tz_header;

typedef struct {
    int32_t gmtoff;          // offset from UTC in seconds
    unsigned char isdst;     // if Daylight Savings
    unsigned char abbrind;   // position of corresponding abbreviation in ->abbrs below
} rp_tz_type;

/* *****************\
|* rp_tz_zone_entry |
\ ******************/
typedef struct {
    char           *timezone_name;   // name from relative path
    uint32_t        flags;           // currently only to differentiate between entry and link
    int32_t         timecnt;         // number of transitions and types members
    int32_t         typecnt;         // number of ttinfo members
    int32_t         charcnt;         // size of abbrs
    int32_t        *transitions;     // date/time of transition between standard and daylight savings
    unsigned char  *types;           // type of transition - index into ttinfo
    rp_tz_type     *ttinfo;          // struct above 
    char           *abbrs;           // concatenated null terminated tz abbreviations, with index to each in tz_type above
    char           *filename;        // filename from which info was loaded
} rp_tz_zone_entry;

/* *****************\
|* rp_tz_zone_link  |
\ ******************/
typedef struct {
    char             *timezone_name; // name from relative path OF LINK
    uint32_t          flags;         // currently only to differentiate between entry and link
    rp_tz_zone_entry *entry;         // rp_tz_zone_entry above
} rp_tz_zone_link;


/* *************************\
|* rp_tz_zone               |
|* masking struct for above |
\***************************/
typedef struct {
    char             *timezone_name; // name from relative path of link or file
    uint32_t          flags;         // currently only to differentiate between entry and link
} rp_tz_zone;


/* ***********************************\
|*           rp_tz_abbr               |
|* a record for a unique abbreviation |
\*************************************/
typedef struct {
    const char     *abbr;       // the abbreviation
    int32_t        *gmtoffs;    // list of offsets
    rp_tz_zone    **tz_zones;   // origin of offsets
    unsigned char  *tz_indexes; // index of abbr in tz_zone of tz_zones
    uint32_t        nentries;   // number of gmtoffs, tz_zones and tz_indexes
    uint32_t        ambiguous;  // 0 if all gmtoffs are the same, otherwise 1
} rp_tz_abbr;

/* ***********************************************\
|*           rp_timezones                         |
|* holder of all timezones loaded from a dirctory |
\*************************************************/
typedef struct {
    rp_tz_zone **zones;
    int          nzones;
    rp_tz_abbr **abbreviations;
    int          nabbreviations;
} rp_timezones;

#define RPCAT(x, y) x ## y
#define RPCAT2(x, y) RPCAT(x, y)
#define RPITER RPCAT2(rp_iter_, __LINE__)


// iterate over timezones in an rp_timezones struct
#define rp_tz_foreach_zone(_tz,_zone)  \
    int RPITER = 0;                         \
    for(                                    \
    RPITER=0, _zone=_tz->zones[0];          \
    RPITER < _tz->nzones;                   \
    _zone=_tz->zones[RPITER++]              \
)

// iterate over abbreviation records in an rp_timezones struct
#define rp_tz_foreach_abbr(_tz,_abbr)  \
    int RPITER = 0;                         \
    for(                                    \
    RPITER=0, _abbr=_tz->abbreviations[0];  \
    RPITER <_tz->nabbreviations;            \
    _abbr=_tz->abbreviations[RPITER++]      \
)

/* 
    load all timezone infomation recursively from directory
    where:
        - path is the starting directory 
          or NULL to use /usr/share/zoneinfo
    returns pointer to rp_timezones struct
*/
rp_timezones *rp_tz_load_timezones(char *path);


/* free rp_timezones and all data within, returns NULL */
rp_timezones *rp_tz_timezones_free(rp_timezones *tz);


/* 
    rp_tz_zone       - represents one timezone loaded from a file or link
    rp_tz_zone_link  - represents a link, which contains a pointer to the actual rp_tz_zone_entry.
    rp_tz_zone_entry - represents the data contained in the corresponding timezone.

    Each of these structs has the member "timezone_name", a relative path - e.g. 'America/Los_Angeles'

    To retrieve an rp_tz_zone_entry (which has the information for that timezone):    
    
    char *name;
    rp_tz_zone_entry *zone_entry = rp_tz_get_entry(myzone, &name);

     - where:
         - zone is a pointer to an rp_tz_zone struct
         - name is a pointer to a string pointer - 
           the timezone_name of the zone (if link, the name from link, otherwise the name from filename)
*/
rp_tz_zone_entry *rp_tz_get_entry(rp_tz_zone *zone, char **name);

// test if rp_tz_zone is a link
#define rp_tz_zone_islink(_zone) (_zone->flags)

/* find an rp_tz_zone record in an rp_timezones struct 
   returns NULL if not found                            */
rp_tz_zone *rp_tz_find_zone(rp_timezones *tz, char *tzname);


/*
    same as above but returns an rp_timezone_entry record
    return:
       if found zone is a link, returns rp_tz_get_entry( rp_tz_find_zone(tz, tzname) , NULL)
           -i.e. found_link_record->entry
       if found zone is an entry return is (rp_timezone_entry *) rp_tz_find_zone(tz, tzname)
       if not found return is NULL
*/
rp_tz_zone_entry *rp_tz_find_zone_entry(rp_timezones *tz, char *tzname);


/* find an abbreviation record in an rp_timezones struct
   returns NULL if not found                              */
rp_tz_abbr *rp_tz_find_abbr(rp_timezones *tz, char *abbr);

/* find an abbreviation record matched from the beginning portion
   of "abbr".  i.e. "PSTxzy"
   if **matched is not NULL, it will be set to a malloc'd string containing the matched abbreviation
*/
rp_tz_abbr *rp_tz_find_abbr_match(rp_timezones *tz, char *abbr, char **matched);

#endif
