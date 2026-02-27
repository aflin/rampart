/*
    rp_tz.c
    copyright (c) 2026 Aaron Flin
    LICENSE: MIT

    to compile as a command line utility:
    gcc -DTEST -o rp_tz rp_tz.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "rp_tz.h"

#define rp_tz_zone_setlink(_zone) do{ _zone->flags=1; } while (0)


rp_tz_zone_entry *rp_tz_get_entry(rp_tz_zone *z, char **name)
{
    if(rp_tz_zone_islink(z))
    {
        rp_tz_zone_link *link = (rp_tz_zone_link *)z;
        if(name)
            *name = link->timezone_name; //set name to link name, not entry name;
        return link->entry;          //return entry
    }

    if(name)
        *name = z->timezone_name;
    return (rp_tz_zone_entry *)z;
}

static rp_tz_zone *free_tz_zone(rp_tz_zone *zone) {
    if(!zone)
        return zone;

    if(zone->timezone_name)
        free(zone->timezone_name);

    if(!rp_tz_zone_islink(zone))
    {
        rp_tz_zone_entry *ezone = rp_tz_get_entry(zone,NULL);

        if(ezone->transitions)
            free(ezone->transitions);
        if(ezone->types)
            free(ezone->types);
        if(ezone->ttinfo)
            free(ezone->ttinfo);
        if(ezone->abbrs)
            free(ezone->abbrs);
        if(ezone->filename)
            free(ezone->filename);
    }
    free(zone);
    return NULL;
}

// bug fix: cast to uint32_t before shifting to avoid undefined behavior - 2026-02-27
static inline int32_t read_int32_from_chars(const unsigned char *chars) {
    return ((uint32_t)chars[0] << 24) | ((uint32_t)chars[1] << 16) | ((uint32_t)chars[2] << 8) | (uint32_t)chars[3];
}


static int32_t read_int32(FILE *file) {
    unsigned char buf[4];
    if (fread(buf, 1, 4, file) != 4) {
        perror("Error reading file");
        exit(EXIT_FAILURE);
    }
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

static rp_tz_zone * extract_timezone_info(char *path, int base_path_len) {
    rp_tz_header header;
    FILE *file;

    file = fopen(path, "rb");

    if (file == NULL) {
        return NULL;
    }

    rp_tz_zone_entry *zone=NULL;

    TZ_CALLOC(zone,sizeof(rp_tz_zone_entry));

    zone->timezone_name=strdup(path+base_path_len);
    zone->filename = realpath(path,NULL); //malloc

    fread(&header, 1, sizeof(header), file);

    if (strncmp(header.magic, TZ_MAGIC, 4) != 0)
    {
#ifdef WARN
        fprintf(stderr, "could not load file %s - bad magic\n", path);
#endif
        goto error;
    }

    zone->timecnt = read_int32_from_chars(header.tzh_timecnt);
    zone->typecnt = read_int32_from_chars(header.tzh_typecnt);
    zone->charcnt = read_int32_from_chars(header.tzh_charcnt);

    if(
        zone->typecnt < 0 || zone->typecnt > TZ_MAX_TYPES ||
        zone->timecnt < 0 || zone->timecnt > TZ_MAX_TIMES ||
        zone->charcnt < 0 || zone->charcnt > TZ_MAX_CHARS
    )
    {
#ifdef WARN
        fprintf(stderr, "could not load file %s - count over limit\n", path);
#endif
        goto error;
    }


    TZ_REMALLOC(zone->transitions, zone->timecnt * sizeof(int32_t));
    TZ_REMALLOC(zone->types,       zone->timecnt * sizeof(unsigned char));
    TZ_REMALLOC(zone->ttinfo,      zone->typecnt * sizeof(rp_tz_type));
    TZ_REMALLOC(zone->abbrs,       zone->charcnt * sizeof(char));

    for (int i = 0; i < zone->timecnt; i++) {
        zone->transitions[i] = read_int32(file);
    }

    fread(zone->types, 1, zone->timecnt, file);

    for (int i = 0; i < zone->typecnt; i++) {
        zone->ttinfo[i].gmtoff = read_int32(file);
        fread(&zone->ttinfo[i].isdst, 1, 1, file);
        fread(&zone->ttinfo[i].abbrind, 1, 1, file);
    }

    fread(zone->abbrs, 1, zone->charcnt, file);

    fclose(file);

    return (rp_tz_zone *)zone;

    error:
    if(zone)
        free_tz_zone((rp_tz_zone*)zone);
    fclose(file);
    return NULL;
}


static inline void insert_abbr(rp_timezones *tz, char *abbr, int32_t off, rp_tz_zone *zone, int abbr_idx)
{
    int i=0;
    rp_tz_abbr *abbr_s=NULL, *cur;

    for(;i<tz->nabbreviations;i++)
    {
        cur=tz->abbreviations[i];
        if(!strcmp(cur->abbr,abbr))
        {
            abbr_s=cur;
            break;
        }
    }

    if(!abbr_s)
    {
        TZ_REMALLOC(tz->abbreviations, (1 + tz->nabbreviations) * sizeof(rp_tz_abbr *));
        TZ_CALLOC(tz->abbreviations[tz->nabbreviations], sizeof(rp_tz_abbr));
        abbr_s = tz->abbreviations[tz->nabbreviations];
        abbr_s->abbr=abbr;
        tz->nabbreviations++;
    }

    TZ_REMALLOC(abbr_s->gmtoffs,    (1+abbr_s->nentries) * sizeof(uint32_t));
    TZ_REMALLOC(abbr_s->tz_zones,   (1+abbr_s->nentries) * sizeof(rp_tz_zone *));
    TZ_REMALLOC(abbr_s->tz_indexes, (1+abbr_s->nentries) * sizeof(unsigned char));
    abbr_s->gmtoffs[abbr_s->nentries]=off;
    abbr_s->tz_zones[abbr_s->nentries]=zone;
    abbr_s->tz_indexes[abbr_s->nentries]=(unsigned char)abbr_idx;
    abbr_s->nentries++;
}

static inline void insert_abbrs(rp_timezones *tz, int idx)
{
    int i=0;

    rp_tz_zone_entry *zone = rp_tz_get_entry(tz->zones[idx],NULL);

    for (i = 0; i < zone->typecnt; i++) {
        char *abbr=&zone->abbrs[zone->ttinfo[i].abbrind];
        insert_abbr(tz, abbr, zone->ttinfo[i].gmtoff, tz->zones[idx], i);
    }
}

static int cmp_abbr(const void *a, const void *b)
{
    return strcmp( (*(rp_tz_abbr **)a)->abbr, (*(rp_tz_abbr **)b)->abbr);
}

static void make_abbreviations(rp_timezones *tz)
{
    int i=0, j=0;
    int32_t lastoff=-1;

    for(i=0; i<tz->nzones; i++)
        insert_abbrs(tz, i);

    for(i=0; i<tz->nabbreviations; i++)
    {
        rp_tz_abbr *abbr_s = tz->abbreviations[i];

        lastoff=-INT32_MAX;
        for(j=0;j<abbr_s->nentries;j++)
        {
            if(lastoff != -INT32_MAX && lastoff != abbr_s->gmtoffs[j])
            {
                abbr_s->ambiguous=1;
                break;
            }
            lastoff=abbr_s->gmtoffs[j];
        }
    }
    qsort(tz->abbreviations, tz->nabbreviations, sizeof(rp_tz_abbr*), cmp_abbr);
}

static rp_timezones * _proc_links(rp_timezones *tz, const char *path, int base_path_len)
{
    struct dirent *entry;
    DIR *dp = opendir(path);
    char fullpath[PATH_MAX];

    if (dp == NULL) {
        return tz;
    }

    while ((entry = readdir(dp)))
    {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        if (entry->d_type == DT_DIR)
        {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            {
                tz=_proc_links(tz, fullpath, base_path_len);
            }
        }
        else if (entry->d_type == DT_LNK)
        {
            char rpath[PATH_MAX];
            rp_tz_zone_link *link=NULL;
            rp_tz_zone *entry=NULL;
            int i=0;

            // bug fix: added realpath() NULL check with continue - 2026-02-27
            if(!realpath(fullpath, rpath))
                continue;

            for(;i<tz->nzones;i++)
            {
                if(!rp_tz_zone_islink(tz->zones[i]) && !strcmp( ((rp_tz_zone_entry *)tz->zones[i])->filename, rpath))
                {
                    entry=tz->zones[i];
                    break;
                }
            }

            if(entry)
            {

                TZ_REMALLOC(link, sizeof(rp_tz_zone_link));
                rp_tz_zone_setlink(link);
                link->timezone_name=strdup(fullpath+base_path_len);
                link->entry=(rp_tz_zone_entry*)entry;

                TZ_REMALLOC(tz->zones, (tz->nzones+1) * sizeof(rp_tz_zone *));
                tz->zones[tz->nzones]=(rp_tz_zone*)link;
                tz->nzones++;
            }
        }
    }
    closedir(dp);
    return tz;
}

static rp_timezones * _extract_all(rp_timezones *tz, const char *path, int base_path_len)
{
    struct dirent *entry;
    DIR *dp = opendir(path);
    char fullpath[PATH_MAX];

    if (dp == NULL) {
        return tz;
    }

    while ((entry = readdir(dp)))
    {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        if (entry->d_type == DT_DIR)
        {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            {
                tz=_extract_all(tz, fullpath, base_path_len);
            }
        }
        else if (entry->d_type == DT_REG)
        {
            rp_tz_zone *entry = extract_timezone_info(fullpath, base_path_len);
            if(entry)
            {
                TZ_REMALLOC(tz->zones, (tz->nzones+1) * sizeof(rp_tz_zone *));
                tz->zones[tz->nzones]=entry;
                tz->nzones++;
            }
        }
    }
    closedir(dp);
    return tz;

}

static int cmp_tz(const void *a, const void *b)
{
    return strcmp( (*(rp_tz_zone **)a)->timezone_name, (*(rp_tz_zone **)b)->timezone_name);
}

rp_timezones *rp_tz_load_timezones(char *path)
{
    rp_timezones *ret = NULL;

    if(!path)
        path=RP_TIMEZONE_PATH;

    TZ_CALLOC(ret, sizeof(rp_timezones));

    ret = _extract_all(ret, path, strlen(path)+1);
    ret = _proc_links(ret, path, strlen(path)+1);
    make_abbreviations(ret);
    qsort(ret->zones, ret->nzones, sizeof(rp_tz_zone*), cmp_tz);
    return ret;
}

static void free_abbr(rp_tz_abbr *a)
{
    if(!a)
        return;

    //abbr and tzzones[x] (but not tzzones) are freed in free_tz_zone
    if(a->gmtoffs)
        free(a->gmtoffs);

    if(a->tz_zones)
        free(a->tz_zones);

    if(a->tz_indexes)
        free(a->tz_indexes);

    free(a);
}

rp_timezones *rp_tz_timezones_free(rp_timezones *tz)
{
    int i=0;
    if(!tz)
        return tz;
    for(i=0; i<tz->nzones; i++)
        free_tz_zone(tz->zones[i]);

    free(tz->zones);

    for(i=0;i<tz->nabbreviations;i++)
        free_abbr(tz->abbreviations[i]);

    free(tz->abbreviations);

    free(tz);
    return NULL;
}

rp_tz_abbr *rp_tz_find_abbr(rp_timezones *tz, char *abbr)
{
    rp_tz_abbr search, **res, *searchp=&search;

    search.abbr=abbr;
    res=bsearch(&searchp, tz->abbreviations, tz->nabbreviations, sizeof(rp_tz_abbr*), cmp_abbr);

    if(!res)
        return NULL;
    return *res;
}

#define MAX_ABBR_LEN 5
#define MIN_ABBR_LEN 3

rp_tz_abbr *rp_tz_find_abbr_match(rp_timezones *tz, char *abbr, char **matched)
{
    rp_tz_abbr search, **res=NULL, *searchp=&search;
    char *s = strndup(abbr, MAX_ABBR_LEN);
    int i=strlen(s);

    search.abbr=s;
    for (; i>=MIN_ABBR_LEN && !res; i--)
    {
        s[i]='\0';
        //printf("searching for '%s'\n", search.abbr);
        res=bsearch(&searchp, tz->abbreviations, tz->nabbreviations, sizeof(rp_tz_abbr*), cmp_abbr);
    }

    if(!res)
    {
        free(s);
        if(matched)
            *matched=NULL;
        return NULL;
    }

    if(matched)
        *matched=s;
    else
        free(s);

    return *res;
}


rp_tz_zone *rp_tz_find_zone(rp_timezones *tz, char *tzname)
{
    rp_tz_zone search, **res, *searchp=&search;

    search.timezone_name=tzname;
    res=bsearch(&searchp, tz->zones, tz->nzones, sizeof(rp_tz_zone*), cmp_tz);
    if(!res)
        return NULL;
    return *res;
}

rp_tz_zone_entry *rp_tz_find_zone_entry(rp_timezones *tz, char *tzname)
{
    rp_tz_zone *res = rp_tz_find_zone(tz, tzname);

    if(!res)
        return NULL;

    return rp_tz_get_entry(res, NULL);
}


#ifdef RP_USING_DUKTAPE
#undef TEST
#endif

#ifdef TEST
void printabbr(rp_tz_abbr *abbr_s)
{
    int j=0;
    printf("%s (%sambiguous):\n", abbr_s->abbr, abbr_s->ambiguous?"":"un");
    for(j=0;j<abbr_s->nentries;j++)
    {
        printf("    %d from %s\n",
            abbr_s->gmtoffs[j], abbr_s->tz_zones[j]->timezone_name);
    }
}

void printabbrs(rp_timezones *tz)
{
    rp_tz_abbr *abbr_s;
    printf("\n--------------\n%d abbreviations\n", tz->nabbreviations);
    rp_tz_foreach_abbr(tz, abbr_s)
    {
        printabbr(abbr_s);
    }
}


void printzone(rp_tz_zone *mzone)
{
    char *name;
    rp_tz_zone_entry *zone =  rp_tz_get_entry(mzone, &name);


    if(strcmp(name, zone->timezone_name))
        printf("Timezone: %s -> %s\n", name, zone->timezone_name);
    else
        printf("Timezone: %s\n", name);

    printf("Number of transitions: %d\n", zone->timecnt);
    printf("Number of types: %d\n", zone->typecnt);

    for (int i = 0; i < zone->timecnt; i++) {
        time_t t = zone->transitions[i];
        struct tm *tm_zone = gmtime(&t);
        printf("Transition %d(%d): %s", i, zone->types[i], asctime(tm_zone));
    }

    for (int i = 0; i < zone->typecnt; i++) {
        printf("Type %d: GMT offset: %d, DST: %d, Abbreviation: %s\n",
               i, zone->ttinfo[i].gmtoff, zone->ttinfo[i].isdst, &zone->abbrs[zone->ttinfo[i].abbrind]);
    }
}


#define usage() do{\
    fprintf(stderr, "%s:\n\
    -d                   - dump all\n\
    -l [\"zones\"|\"abbrs\"] - \n\
    -a abbr              - e.g. -a 'PST'\n\
    -z zone_name         - e.g. -z 'America/Los_Angeles'\n",argv[0]);\
    exit(1);\
}while(0)

int main(int argc, char *argv[]) {

    rp_timezones *tz = rp_tz_load_timezones(NULL);

    if(argc > 1)
    {
        if(*argv[1] != '-')
            usage();
        switch (argv[1][1])
        {
            case 'l':
            {
                if(!strcmp(argv[2], "zones"))
                {
                    rp_tz_zone *zone;
                    rp_tz_foreach_zone(tz, zone)
                    {
                        printf("%s\n", zone->timezone_name);
                    }
                }
                else if(!strcmp(argv[2], "abbrs"))
                {
                    rp_tz_abbr *abbr_s;
                    rp_tz_foreach_abbr(tz, abbr_s)
                    {
                        printf("%s\n", abbr_s->abbr);
                    }
                }
                else
                    usage();
                break;
            }
            case 'a':
            {
                rp_tz_abbr *res = rp_tz_find_abbr(tz, argv[2]);
                if(res)
                    printabbr(res);
                else
                    printf("time zone abbreviation '%s' not found\n", argv[2]);
                break;
            }
            case 'd':
            {
                rp_tz_zone *zone;
                printf("%d zones\n", (int)tz->nzones);

                rp_tz_foreach_zone(tz,zone)
                    printzone(zone);

                printabbrs(tz);
                break;
            }
            case '?':
            case 'h':
                usage();
            case 'z':
            {
                rp_tz_zone *res = rp_tz_find_zone(tz, argv[2]);
                if(res)
                    printzone(res);
                else
                    printf("time zone '%s' not found\n", argv[2]);
                break;
            }
            default:
                usage();
        }
    }
    else
        usage();

    rp_tz_timezones_free(tz);

    return EXIT_SUCCESS;
}

#endif
