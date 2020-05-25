#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdarg.h>
#include "texint.h"
#include "txtypes.h" /* for TXstrerr() */
#include "sizes.h"
#include "cgi.h"

/* these are only used internally */
static double scaleLon(double lat);
static int getMethod(FLD *fld_method, const char *fn);

/* these two global variables are used to control whether "east" longitude
 * is considered positive.  The general concensus is that east should be
 * positive, however a number of sources, especially those dealing with
 * only North America, use west for positive out of convenience.
 * 
 * Our libs use east as positive by default, but they can be switched by
 * calling the c function TXsetEastPositive(0), and reversed via 
 * TXsetEastPositive(1).
 *
 * NEVER GET/SET THESE VARS DIRECTLY, use the access functions
 * TXsetEastPositive() and TXgetEastPositive() instead. */
#define TX_EASTPOSITIVE_DEFAULT 1               /* 1 or 0, true/false */
int TXeastPositive = TX_EASTPOSITIVE_DEFAULT;
/* if TXeastPositive is set to 0 via the TXsetEastPositive, TXlonSign will
 * be switched to -1. */
double TXlonSign=1;

/* the radius of the earth in miles */
#define TXGEO_EARTH_RADIUS 3956
/* possibly expand this? */
#define PI 3.14159265

#define MILES_PER_DEGREE 69

#define BITS_WANT       21              /* desired bits per ordinate */
#define LAT_MASK        ((1L << (BITS_WANT - 1)) - 1L)
#define LON_MASK        ((1L << BITS_WANT) - 1L)

#if EPI_OS_LONG_BITS >= 2*BITS_WANT             /* both will fit */
#  define BITS_PER      BITS_WANT
#  define PRESHIFT      0
#else
#  define BITS_PER      (EPI_OS_LONG_BITS/2)
#  define PRESHIFT      (BITS_WANT - BITS_PER)
#endif

#define TX_AZIMUTH2COMPASS_VERBOSITY_DEFAULT 1
#define TX_AZIMUTH2COMPASS_RESOLUTION_DEFAULT 2

#define TX_COMPASS_BRIEF_NUM (sizeof(TxCompassBrief)/sizeof(TxCompassBrief[0]))
const char *TxCompassBrief[] = {
  /*******/
  /* 0-7: N to E */
  "N", "NbE", "NNE", "NEbN", "NE", "NEbE", "ENE", "EbN",
  /* 8-15: E to S */
  "E", "EbS", "ESE", "SEbE", "SE", "SEbS", "SSE", "SbE",
  /* 16-23: S to W */
  "S", "SbW", "SSW", "SWbS", "SW", "SWbW", "WSW", "WbS",
  /* 24-31: W to N */
  "W", "WbN", "WNW", "NWbW", "NW", "NWbN", "NNW", "NbW"
};

#define TX_COMPASS_VERBOSE_NUM (sizeof(TxCompassVerbose)/sizeof(TxCompassVerbose[0]))
const char *TxCompassVerbose[] = {
  /* 0-7: N to E */
  "North", "North by east", "North-northeast", "Northeast by east",
  "Northeast", "Northeast by east", "East-northeast", "East by north",
  /* 8-15: E to S */
  "East", "East by south", "East-southeast", "Southeast by east",
  "Southeast", "Southeast by south", "South-southeast", "South by east",
  /* 16-23: S to W */
  "South", "South by west", "South-southwest", "Southwest by south",
  "Southwest", "Southwest by west", "West-southwest", "West by south",
  /* 24-31: W to N */
  "West", "West by north", "West-northwest", "Northwest by west",
  "Northwest", "Northwest by north", "North-northwest", "North by west"
};

static CONST char       Whitespace[] = " \t\r\n\v\f";

/* ------------------------------------------------------------------------ */

int
TXsetEastPositive(int eastPositiveParam)
/* `eastPositiveParam' is 1 for true, 0 for false, -1 for default value.
 * Returns 1 on success, 0 on failure.
 */
{
  if(eastPositiveParam==1)                      /* true */
    {
      TXeastPositive=1;
      TXlonSign=1.0;
      return(1);                                /* success */
    }
  if(eastPositiveParam==0)                      /* false */
    {
      TXeastPositive=0;
      TXlonSign=-1.0;
      return(1);                                /* success */
    }
  if(eastPositiveParam==-1)                     /* default */
    return(TXsetEastPositive(TX_EASTPOSITIVE_DEFAULT));
  return(0);                                    /* failure */
}

int
TXgetEastPositive()
{
  return TXeastPositive;
}

/******************************************************************/

int
TXfunc_azimuthgeocode(FLD *fld_geocode1, FLD *fld_geocode2, FLD *fld_method)
{
  static CONST char     fn[] = "TXfunc_azimuthgeocode";
  ft_long *geocode1, *geocode2;
  double lat1, lon1, lat2, lon2, *ret=NULL;
  size_t sz;
  int method;

  method = getMethod(fld_method,fn);

  if(!fld_geocode1) {
    putmsg(MERR + UGE, fn, "null FLD param");
    return(FOP_EINVAL);
  }
  if((fld_geocode1->type & DDTYPEBITS) != FTN_LONG) {
    putmsg(MERR + UGE, fn, "geocode1 not a long");
    return(FOP_EINVAL);
  }
  geocode1 = (ft_long *)getfld(fld_geocode1, &sz);

  if(!fld_geocode2) {
    putmsg(MERR + UGE, fn, "null FLD param");
    return(FOP_EINVAL);
  }
  if((fld_geocode2->type & DDTYPEBITS) != FTN_LONG) {
    putmsg(MERR + UGE, fn, "geocode2 not a long");
    return(FOP_EINVAL);
  }
  geocode2 = (ft_long *)getfld(fld_geocode2, &sz);

  if (!TXgeocodeDecode(*geocode1,&lat1,&lon1))
    {
      putmsg(MERR + UGE, fn, "Invalid geocode1 value %ld", (long)*geocode1);
      return(FOP_EINVAL);
    }
  if (!TXgeocodeDecode(*geocode2,&lat2,&lon2))
    {
      putmsg(MERR + UGE, fn, "Invalid geocode2 value %ld", (long)*geocode2);
      return(FOP_EINVAL);
    }
  
  ret = (ft_double*)TXcalloc(TXPMBUFPN, fn, 1, sizeof(ft_double));
  if(ret == NULL) return(FOP_EINVAL);

  *ret = TXazimuthlatlon(lat1, lon1, lat2, lon2, method);
  
  setfld(fld_geocode1, ret, sizeof(ft_double));
  fld_geocode1->size = sizeof(ft_double);
  fld_geocode1->elsz = sizeof(ft_double);
  fld_geocode1->type = FTN_DOUBLE;
  return 0;
}
                          
int
TXfunc_azimuthlatlon(FLD *fld_lat1, FLD *fld_lon1,
                     FLD *fld_lat2, FLD *fld_lon2,
                     FLD *fld_method)
{
  static CONST char     fn[] = "TXfunc_azimuthlatlon";
  double *lat1=NULL, *lon1=NULL, *lat2=NULL, *lon2=NULL, *ret=NULL;
  size_t sz;
  int method;

  method = getMethod(fld_method,fn);

  if(!fld_lat1)
    {
      putmsg(MERR + UGE, fn, "null FLD* fld_lat1 param");
      return(FOP_EINVAL);
    }
  if((fld_lat1->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "fld_lat1 not a double (%li vs %li)", 
             fld_lat1->type, FTN_DOUBLE);
      return(FOP_EINVAL);
    }
  lat1 = (ft_double *)getfld(fld_lat1, &sz);

  if(!fld_lon1)
    {
      putmsg(MERR + UGE, fn, "null FLD* fld_lon1 param");
      return(FOP_EINVAL);
    }
  if((fld_lon1->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "fld_lon1 not a double (%li vs %li)", 
             fld_lon1->type, FTN_DOUBLE);
      return(FOP_EINVAL);
    }
  lon1 = (ft_double *)getfld(fld_lon1, &sz);


  if(!fld_lat2)
    {
      putmsg(MERR + UGE, fn, "null FLD* fld_lat2 param");
      return(FOP_EINVAL);
    }
  if((fld_lat2->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "fld_lat2 not a double (%li vs %li)", 
             fld_lat2->type, FTN_DOUBLE);
      return(FOP_EINVAL);
    }
  lat2 = (ft_double *)getfld(fld_lat2, &sz);


  if(!fld_lon2)
    {
      putmsg(MERR + UGE, fn, "null FLD* fld_lon2 param");
      return(FOP_EINVAL);
    }
  if((fld_lon2->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "fld_lon2 not a double (%li vs %li)", 
             fld_lon2->type, FTN_DOUBLE);
      return(FOP_EINVAL);
    }
  lon2 = (ft_double *)getfld(fld_lon2, &sz);


  ret = (ft_double*)TXcalloc(TXPMBUFPN, fn, 1, sizeof(ft_double));
  if(ret == NULL) return(FOP_EINVAL);

  *ret = TXazimuthlatlon(*lat1, *lon1, *lat2, *lon2, method);

  setfld(fld_lat1, ret, sizeof(ft_double));
  fld_lat1->size = sizeof(ft_double);
  fld_lat1->elsz = sizeof(ft_double);
  fld_lat1->type = FTN_DOUBLE;
  return 0;
}

double
TXazimuthlatlon(double lat1, double lon1, double lat2, double lon2, int method)
{
  static CONST char     fn[] = "TXfunc_azimuthlatlon";
  double deg2rad = PI / 180;
  double rad2deg = 180 / PI;

  if(method == TXGEO_GREAT_CIRCLE)
    {
      /* convert the parameters to radians */
      double ret;

      lat1 = deg2rad * lat1;
      lon1 = deg2rad * lon1;
      lat2 = deg2rad * lat2;
      lon2 = deg2rad * lon2;

      /*
       * Greater Circle Direction Formula taken from 
       * http://williams.best.vwh.net/avform.htm#Crs
       *
       * tc1=mod(atan2(sin(lon1-lon2)*cos(lat2),
       *    cos(lat1)*sin(lat2)-sin(lat1)*cos(lat2)*cos(lon1-lon2)), 2*pi)
       *
       * Note that the formula listed treats WEST as POSITIVE, so we have to
       * swap it around for our default.
       *
       * This formula also gives positive/negative values, so we add 2PI to
       * it before modding it by 2PI.
       */
      ret =rad2deg *
        fmod(atan2(sin(TXlonSign*(lon2-lon1))*cos(lat2),
                   cos(lat1)*sin(lat2)-sin(lat1)*cos(lat2)
                   * cos(TXlonSign*(lon2-lon1)))
             + 2 * PI,
             2 * PI
             );
  
      return ret;
    }
  else if(method == TXGEO_PYTHAG)
    {
      double ret, dLat, dLon, avgLat = (lat1+lat2)/2;

      /* these tests are for detecting "world wrap"; the distance between
       * longitude values -179 and 179 is actually 2, but pythag will see it
       * as 358.  If the differences between the lon values either way are
       * > 180, then it'll be close to "loop around" one of the values so the
       * travel is in the opposite direction. */
      if(lon2 - lon1 > 180)
        lon1 += 360;
      if(lon1 - lon2 > 180)
        lon2 += 360;

      dLat = (lat2 - lat1);
      /* the longitude gets scaled down based on the average latitude, to
       * simulate the fact that the degree->distance ratio for longiude
       * decreases as the latitude gets further from the equator. */
      dLon = (lon2 - lon1) * scaleLon(avgLat) / 100;
      
      ret = rad2deg * atan2(dLon, dLat);
      if(ret < 0)
        ret += 360;
      return ret;
    }
  else
    {
      putmsg(MERR, fn, "invalid method (%i) specified.", method);
      return -1;
    }
}

/**
 * This function provides the amount that the longitude should be scaled
 * based on the current latitude As the latitude moves away from the
 * equator, the ratio of degrees latitude -> miles decreases from 69M at the
 * equator to 0 at the pole.
 * 
 * This fudge is only necessary with "pythagorean" method, not with
 * greater circle.
 */
static double
scaleLon(double lat)
{
  double ret;

  /* positive & negative latitude values scale the same way */
  if(lat < 0)
    lat = -lat;

  /*
   * This was calcuated by taking 1-degree longitude measurements at various
   * latitudes using an external tool
   * (http://www.wcrl.ars.usda.gov/cec/java/lat-long.htm), plugging the data
   * set into excel, and fitting a trend line to it, which gave:
   *
   * y = 6E-05x3 - 0.0185x2 + 0.0609x + 99.799
   *
   * a 3rd degree polynomial was used because the 2nd degree had some sizable
   * errors around 0 and 90.  The 3rd degree still has problems with going
   * negative abouve 89, which is manually fudged.
   */
  if(lat < 89)
    {
      ret = .00006*pow(lat,3)-.0185*pow(lat,2)+.0609 * (lat) + 99.799;
    }
  else
    {
      /* the provided function gives .97748 at 89, .00749 at 89.5, and
       * ends up dippin into the negative shortly thereafter.  We'll manually
       * fudge to give a linear scale between 89-90.
       * No, this is not completely accurate.  If you want accuracy at the
       * poles then you NEED to use greater circle, not pythagorean. */
      ret = -.97874 * lat + 88.0866;
    }
  return ret;
}

/******************************************************************/

int
TXfunc_azimuth2compass(FLD *fld_azimuth, FLD *fld_resolution, FLD *fld_verbosity)
{
  static CONST char     fn[] = "TXfunc_azimuth2compass";
  size_t sz;
  double *azimuth;
  char *ret = NULL;
  int result;
  int resolution = TX_AZIMUTH2COMPASS_RESOLUTION_DEFAULT;
  int verbosity = TX_AZIMUTH2COMPASS_VERBOSITY_DEFAULT;

  if(!fld_azimuth)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_azimuth->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "azimuth not a double (%li vs %li)",
             (fld_azimuth->type & DDTYPEBITS), FTN_DOUBLE);
      return(FOP_EINVAL);
    }
  azimuth = (ft_double *)getfld(fld_azimuth, &sz);

  /* resolution is allowed to be NULL */
  if(fld_resolution != NULL)
    {
      int *resolutionPtr;
      if((fld_resolution->type & DDTYPEBITS) != FTN_INT)
        {
          putmsg(MERR + UGE, fn, "resolution not an int (%li vs %li)",
                 (fld_resolution->type & DDTYPEBITS), FTN_INT);
          return(FOP_EINVAL);
        }
      resolutionPtr = (ft_int *)getfld(fld_resolution, &sz);
      resolution = *resolutionPtr;
    }
  
  /* verbosity is allowed to be NULL */
  if(fld_verbosity != NULL)
    {
      int *verbosityPtr;
      if((fld_verbosity->type & DDTYPEBITS) != FTN_INT)
        {
          putmsg(MERR + UGE, fn, "verbosity not an int (%li vs %li)",
                 (fld_verbosity->type & DDTYPEBITS), FTN_INT);
          return(FOP_EINVAL);
      }
      verbosityPtr = (ft_int *)getfld(fld_verbosity, &sz);
      verbosity = *verbosityPtr;
    }
  
  result = TXazimuth2compass(&ret, *azimuth, resolution, verbosity);
  if(result != 0)
    return result;

  TXfreefldshadow(fld_azimuth);
  fld_azimuth->type = FTN_CHAR | DDVARBIT;
  fld_azimuth->elsz=sizeof(ft_char);
  setfldandsize(fld_azimuth, ret, sizeof(ft_char) * (strlen(ret)+1) , FLD_FORCE_NORMAL);
  return(0);
}

int
TXazimuth2compass(char **ret, double azimuth, int resolution, int verbosity)
{
  static CONST char     fn[] = "TXazimuth2compass";
  int index=0;
  int section = (int)floor( azimuth / 5.625 );;

  if( !(resolution >= 1 && resolution <= 4) )
    {
      putmsg(MERR, fn, "invalid resolution value(%i)", resolution);
      return FOP_EINVAL;
    }
  if( !(verbosity >= 1 && verbosity <= 2) )
    {
      putmsg(MERR, fn, "invalud verbosity value (%i)", verbosity);
      return FOP_EINVAL;
    }


  if(section > 63)
    section = (int)fmod(section, 64);

  if(section < 0)
    {
      /* we want to give section as many full positive revolutions (64's)
       * as it needs to be positive. */
      section += 64 * (int)ceil(-section / 64.0);
    }

  /* the index "snaps" to different values depending on how fine-grained
   * the resolution is.  If 1, then 0-7 beome 0, 8-25 become 16, etc. */
  switch(resolution)
    {
    case 1:
      index = (int) floor( (section+8) / 16) * 8; break;
    case 2:
      index = (int) floor( (section+4) / 8 ) * 4; break;
    case 3:
      index = (int) floor( (section+2) / 4 ) * 2; break;
    case 4:
      index = (int) floor( (section+1) / 2); break;
    }
  
  index = (int) fmod(index, 32);

  if(verbosity == 1)
    *ret = TXstrdup(TXPMBUFPN, fn, TxCompassBrief[index]);
  else
    *ret = TXstrdup(TXPMBUFPN, fn, TxCompassVerbose[index]);

  if(*ret == NULL) return FOP_ENOMEM;
  
  return 0;
}

/* ------------------------------------------------------------------------ */

static double
TXdm2dec(double dm)
/* Converts DDMM[.MM] `dm' to decimal degrees.
 */
{
  double        dec;

  dec = ((int)dm) / 100;                        /* convert degrees */
  dm -= dec * (double)100.0;
  dec += dm / 60.0;                             /* convert minutes */
  return(dec);
}

static double
TXgetLatSignMx(CONST char **sp)
/* Advances `*sp' past an optional latitude "sign", e.g. N/S, and returns it
 * as a multiplier (1 or -1), or 0 on error (not a sign, `*sp' not advanced).
 */
{
  CONST char    *s = *sp;
  double        sign = 0.0;

  switch (*s)
    {
    case 'n':
    case 'N':
      s += (strnicmp(s, "north", 5) == 0 ? 5 : 1);
      sign = 1;
      break;
    case 's':
    case 'S':
      s += (strnicmp(s, "south", 5) == 0 ? 5 : 1);
      sign = -1;                                /* north-positive */
      break;
    default:
      break;
    }
  *sp = s;
  return(sign);
}

static double
TXgetLonSignMx(CONST char **sp)
/* Advances `*sp' past an optional longitude "sign", e.g. E/W, and returns it
 * as a multiplier (1 or -1), or 0 on error (not a sign, `*sp' not advanced).
 */
{
  CONST char    *s = *sp;
  double        sign = 0.0;

  switch (*s)
    {
    case 'e':
    case 'E':
      s += (strnicmp(s, "east", 4) == 0 ? 4 : 1);
      sign = 1;
      break;
    case 'w':
    case 'W':
      s += (strnicmp(s, "west", 4) == 0 ? 4 : 1);
      sign = -1;                                /* east-positive */
      break;
    default:
      break;
    }
  *sp = s;
  return(sign);
}

static double
TXgetLatLonUnitMx(CONST char **sp, int *dmFlag)
/* Advances `*sp' past an optional unit, e.g. degrees/minutes/seconds.
 * If `*dmFlag' is nonzero, allows `s'/`S' as seconds unit.
 * On return, sets `*dmFlag' nonzero if unit was `d'/`D'/`m'/`M'.
 * Returns unit size as a degree-multiplier, e.g. 1.0 for degrees,
 * or 0 on error (not a unit, `*sp' not advanced).
 */
{
  /* Added parens and commas in case parsing a location, so we stop
   * before coord separators:
   */
  static CONST char     seps[] = "(),.0123456789 \t\r\n\v\f";
  CONST char            *s = *sp, *e;
  double                unit = 0.0;
  int                   okS = *dmFlag;

  *dmFlag = 0;
  for (e = s; *e != '\0' && strchr(seps, *e) == CHARPN; e++);
  if (*(byte *)s == 0xb0)                       /* ISO-8859-1 degree sign */
    {
      unit = 1.0;
      s++;
    }
  else if (((byte *)s)[0] == 0xc2 && ((byte *)s)[1] == 0xb0)
    {                                           /* UTF-8 degree sign */
      unit = 1.0;
      s = s + 2;
    }
  else if (e - s >= 3 && strnicmp(s, "deg", 3) == 0)
    {
      if (e - s == 3 && s[3] == '.')            /* `deg.' */
        {
          unit = 1.0;
          s = e + 1;
        }
      else if (e - s == 3 ||                    /* `deg' */
               (e - s == 7 && strnicmp(s, "degrees", 7) == 0)) /* `degrees' */
        {
          unit = 1.0;
          s = e;
        }
    }
  else if (e - s == 1 && (*s == 'd' || *s == 'D'))
    {
      unit = 1.0;
      s = e;
      *dmFlag = 1;
    }
  else if (*s == '\'')                          /* minutes */
    {
      unit = 1.0/60.0;
      s++;
    }
  else if (e - s == 1 && (*s == 'm' || *s == 'M'))      /* minutes */
    {
      unit = 1.0/60.0;
      s = e;
      *dmFlag = 1;
    }
  else if (e - s >= 3 && strnicmp(s, "min", 3) == 0)
    {
      if (e - s == 3 && s[3] == '.')            /* `min.' */
        {
          unit = 1.0/60.0;
          s = e + 1;
        }
      else if (e - s == 3 ||                    /* `min' */
               (e - s == 7 && strnicmp(s, "minutes", 7) == 0)) /* `minutes' */
        {
          unit = 1.0/60.0;
          s = e;
        }
    }
  else if (*s == '"')                           /* seconds */
    {
      unit = 1.0/3600.0;
      s++;
    }
  else if (e - s == 1 && okS && (*s == 's' || *s == 'S'))
    {
      unit = 1.0/3600.0;
      s++;
      *dmFlag = 1;
    }
  else if (e - s >= 3 && strnicmp(s, "sec", 3) == 0)
    {
      if (e - s == 3 && s[3] == '.')            /* `sec.' */
        {
          unit = 1.0/3600.0;
          s = e + 1;
        }
      else if (e - s == 3 ||                    /* `sec' */
               (e - s == 7 && strnicmp(s, "seconds", 7) == 0)) /* `seconds' */
        {
          unit = 1.0/3600.0;
          s = e;
        }
    }
  *sp = s;
  return(unit);
}

static double
TXgeoStrtod(CONST char *buf, char **parseEnd)
/* TXstrtod() for geographical coordinates: does not take `d'/`D' as
 * exponent character (Windows strtod() will), which would be confused
 * with `degrees' abbreviation.  E.g. `12d3m' is 12 degrees 3 minutes,
 * not 12,000 minutes.  Allows `e'/`E': even though no exponent should
 * be in a coordinate, `%L' might print them, and since longitude is
 * last in a location, there should not ever be a number after `east'
 * in a longitude.
 */
{
  CONST char    *s;
  char          tmpBuf[512];
  char          *d, *tmpEnd = tmpBuf + sizeof(tmpBuf);
  double        ret;
  int           errnum;

  for (s = buf, d = tmpBuf;
       *s != '\0' && d < tmpEnd && *s != 'd' && *s != 'D';
       s++, d++)
    *d = *s;
  if (d >= tmpEnd) goto err;
  *d = '\0';
  ret = TXstrtod(tmpBuf, CHARPN, &d, &errnum);
  if (errnum != 0) goto err;
  s = buf + (d - tmpBuf);                       /* map `d' back to `buf' */
  goto done;

err:
  ret = 0.0;
  s = buf;                                      /* nothing parsed */
done:
  if (parseEnd != CHARPPN) *parseEnd = (char *)s;
  return(ret);
}

double
TXparseCoordinate(CONST char *buf, int flags, char **end)
/* Translates a latitude or longitude `buf' that is in one of various
 * formats, into decimal degrees.  Returns NaN if not parseable.
 * `flags' are bit flags:
 *   0x01  `buf' is a longitude; else it is a latitude
 *   0x02  Disallow hemisphere between degrees/minutes (helps location parse)
 * If `end' is non-NULL, sets `*end' to (one byte past) end of `buf' parsed,
 * ala strtol().
 * (We require that `buf' be definitively identified as a latitude or
 * longitude via `flags'; there is no "unknown" option.  This
 * avoids ambiguity, e.g. if "unknown" and no hemisphere given in `buf'.
 * Ambiguity in parsing is worse than loss of the minor convenience of
 * an "unknown" option.  Plus, caller almost certainly *has* to
 * already know if this is a lat or lon in order to know what to do
 * with it afterwards, e.g. which column to insert into.)
 */
{
  double                val, deg;
  CONST char            *s, *valBegin, *valEnd;
  char                  *e;
  size_t                n;
  double                signMx = 1.0, signMxToUse = 1.0, unitMx = 1.0;
  double                gotUnitMx;
  int                   gotSign = 0, dmFlag, wasDM = 0;
  double                (*getSignMxFunc)(CONST char **sp);
  double                (*getOtherSignMxFunc)(CONST char **sp);

  deg = 0.0;
  getSignMxFunc = ((flags & 0x1) ? TXgetLonSignMx : TXgetLatSignMx);
  getOtherSignMxFunc = ((flags & 0x1) ? TXgetLatSignMx : TXgetLonSignMx);

  s = buf;

  /* Handle optional leading sign (N/S/E/W): */
  s += strspn(s, Whitespace);
  signMx = getSignMxFunc(&s);
  if (signMx != 0.0)                            /* parsed a sign */
    {
      signMxToUse = signMx;
      gotSign = 1;
    }
  else if (getOtherSignMxFunc(&s) != 0.0)
    goto invalid;

  /* Get the first number, probably degrees: */
  valBegin = s;
  val = TXgeoStrtod(s, &e);
  valEnd = (CONST char *)e;
  if (e == s) goto invalid;                     /* a number is required */
  if (val < 0.0)                                /* 1st num can be negative */
    {
      val = -val;
      signMxToUse = -signMxToUse;
    }
  s = e;                                        /* past the number */
  /* Handle an optional unit (degrees etc.) after first number: */
  s += strspn(s, Whitespace);
  dmFlag = 0;
  unitMx = gotUnitMx = TXgetLatLonUnitMx(&s, &dmFlag);
  wasDM = (wasDM || dmFlag);
  if (unitMx == 0.0) unitMx = 1.0;              /* 1st num: default degrees */
  deg += val*unitMx;                            /* sign applied at end */

  /* Check if `val' is an ISO 6709 format: */
  if (!gotSign && gotUnitMx == 0.0)             /* unadorned # so far */
    {
      n = valEnd - valBegin;
      if (signMxToUse < 0.0) n--;               /* minus sign is not digit */
      if ((e = strchr(valBegin, '.')) != CHARPN &&
          e < valEnd)
        n -= valEnd - e;                        /* do not count decimals */
      if (((e = strchr(valBegin, 'e')) != CHARPN ||
           (e = strchr(valBegin, 'E')) != CHARPN) &&
          e < valEnd)
        n = 0;                                  /* do not use exponent fmt */
      /* Note that DDMM and DDDMMSS formats could be ambiguous:
       * is `12345' 1d 23m 45s or 123d 45m?  We assume leading zeros
       * for 1-digit degrees to disambiguate (ISO 6709 does too?),
       * but then try to correct if degrees looks out of bounds.
       * We also assume ISO 6709 east-positive, not deprecated Texis
       * DDDMMSS west-positive:
       */
      if (n == 4 || n == 5)                     /* DDMM[.MM] format */
        {
          deg = TXdm2dec(val);
          if (n == 5 && deg > ((flags & 0x1) ? 180.0 : 90.0)) goto dms;
          goto applySign;
        }
      if (n == 6 || n == 7)                     /* DDMMSS[.SS] format */
        {
        dms:
          deg = TXdms2dec(val);
          goto applySign;
        }
    }

  s += strspn(s, Whitespace);
  if (*s == ':')                                /* e.g. `12:55:55' */
    {
      s++;
      s += strspn(s, Whitespace);
    }

  /* Check for optional sign between numbers, e.g. "36N50'": */
  if (!(flags & 0x2) && !gotSign)
    {
      if ((signMx = getSignMxFunc(&s)) != 0.0)
        {
          signMxToUse = signMx;
          gotSign = 1;
        }
      else if (getOtherSignMxFunc(&s) != 0.0)
        goto invalid;
    }

  /* Get optional minutes: */
  s += strspn(s, Whitespace);
  if (*s == '\0') goto applySign;               /* minutes are optional */
  val = TXgeoStrtod(s, &e);
  if (e == s) goto applySign;                   /* minutes are optional */
  if (val < 0.0) goto invalid;                  /* 2nd+ # cannot be negative*/
  s = e;
  /* Handle an optional unit (minutes etc.) after second number: */
  s += strspn(s, Whitespace);
  dmFlag = 0;
  unitMx = TXgetLatLonUnitMx(&s, &dmFlag);
  wasDM = (wasDM || dmFlag);
  if (unitMx == 0.0) unitMx = 1.0/60.0;         /* 2nd num: default minutes */
  deg += val*unitMx;                            /* sign applied at end */

  /* Get optional seconds: */
  s += strspn(s, Whitespace);
  if (*s == ':')                                /* e.g. `12:55:55' */
    {
      s++;
      s += strspn(s, Whitespace);
    }
  if (*s == '\0') goto applySign;               /* seconds are optional */
  val = TXgeoStrtod(s, &e);
  if (e == s) goto applySign;                   /* seconds are optional */
  if (val < 0.0)                                /* 2nd+ # cannot be negative*/
    {
    invalid:
      TXDOUBLE_SET_NaN(deg);
      /* Without some code here (function call?) after the TXDOUBLE_SET_NaN()
       * macro call above, gcc 3.4.6 -O2 on ia64-unknown-linux2.6.9-64-64
       * will change `deg' to 0.0 here.  WTF KNG
       */
      TXbasename("");
      s = buf;                                  /* nothing parsed */
      goto done;
    }
  s = e;
  /* Handle an optional unit (seconds etc.) after third number: */
  s += strspn(s, Whitespace);
  dmFlag = wasDM;                               /* allow S iff D or M seen */
  unitMx = TXgetLatLonUnitMx(&s, &dmFlag);
  if (unitMx == 0.0) unitMx = 1.0/3600.0;       /* 3rd num: default seconds */
  deg += val*unitMx;                            /* sign applied at end */

  /* Check for optional sign after all numbers, e.g. `36 deg. 50' 10" S': */
applySign:
  s += strspn(s, Whitespace);
  if (!gotSign)
    {
      if ((signMx = getSignMxFunc(&s)) != 0.0)
        {
          signMxToUse = signMx;
          gotSign = 1;
        }
      else if (getOtherSignMxFunc(&s) != 0.0)
        goto invalid;
    }

  deg *= signMxToUse;                           /* apply sign last */

done:
  if (end != CHARPPN) *end = (char *)s;
  return(deg);
}

long
TXparseLocation(CONST char *buf, char **end, double *lat, double *lon)
/* Parses a lat/lon coordinate pair, or a geocode value.
 * If `end' is non-NULL, sets `*end' to just past parsed value.
 * If non-NULL, sets `*lat'/`*lon' to lat/lon location.
 * Returns the geocode value, or -1 on error.
 */
{
  CONST char    *s, *afterOpen;
  char          *e;
  double        latVal, lonVal;
  long          ret;

  /* First check for single geocode value: */
  ret = strtol(buf, &e, 10);
  e += strspn(e, Whitespace);
  if (ret >= 0L && *e == '\0')                  /* got a geocode */
    {
      s = e;
      latVal = TXgeocode2lat(ret);
      lonVal = TXgeocode2lon(ret);
      goto done;
    }

  s = buf;

  /* Skip optional open parenthesis: */
  s += strspn(s, Whitespace);
  if (*s == '(') s++;                           /* e.g. `(lat,lon)' */
  afterOpen = s;

  /* Get the latitude.  Disallow hemisphere-between-deg-min (e.g. `30N40');
   * this helps avoid confusion with a location like `30N 40W' where
   * otherwise the `40' would get parsed as latitude's minutes, not
   * the longitude (below):
   */
  latVal = TXparseCoordinate(s, 0x2, &e);
  if (TXDOUBLE_IS_NaN(latVal)) goto checkAltFormat;
  s = e;

  /* Skip optional comma separator: */
  s += strspn(s, Whitespace);
  if (*s == ',') s++;                           /* e.g. `(lat,lon)' */

  /* Get the longitude: */
  lonVal = TXparseCoordinate(s, 0x3, &e);
  if (TXDOUBLE_IS_NaN(lonVal))                  /* invalid longitude */
    {
    checkAltFormat:
      /* Could be `+30 -20' and latitude parse ate the `-20' as minutes: */
      s = afterOpen;
      latVal = TXgeoStrtod(s, &e);
      if (e <= s) goto invalid;
      s = e;
      s += strspn(s, Whitespace);
      if (*s == ',') s++;
      lonVal = TXgeoStrtod(s, &e);
      if (e <= s) goto invalid;
    }
  s = e;

  /* Skip optional closing parenthesis: */
  s += strspn(s, Whitespace);
  if (*s == ')') s++;

  /* Compute value: */
  ret = TXlatlon2geocode(latVal, lonVal);
  goto done;

invalid:
  ret = -1L;
  TXDOUBLE_SET_NaN(latVal);
  TXDOUBLE_SET_NaN(lonVal);
  s = buf;                                      /* we did not parse */
done:
  if (end != CHARPPN) *end = (char *)s;
  if (lat != NULL) *lat = latVal;
  if (lon != NULL) *lon = lonVal;
  return(ret);
}

int
TXfunc_parselatitude(FLD *latitude)
/* SQL function parselatitude().
 * Returns 0 on success, else FOP_... error.
 */
{
  static CONST char     fn[] = "TXfunc_parselatitude";
  char                  *s;
  size_t                sz;
  TXPMBUF               *pmbuf = TXPMBUFPN;
  ft_double             *res = NULL;

  if (latitude == FLDPN ||
      (s = (char *)getfld(latitude, &sz)) == CHARPN)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "NULL argument or value");
      return(FOP_EINVAL);
    }
  /* +1 for fldmath nul: */
  res = (ft_double *)TXcalloc(TXPMBUFPN, fn, 2, sizeof(ft_double));
  if (!res)
    return(FOP_ENOMEM);

  /* For flexibility, we do not care if not all of `s' is parsed: */
  *res = (ft_double)TXparseCoordinate(s, 0, CHARPPN);

  releasefld(latitude);                         /* before changing type */
  latitude->type = FTN_DOUBLE;
  latitude->elsz = sizeof(ft_double);
  setfldandsize(latitude, res, sizeof(ft_double) + 1, FLD_FORCE_NORMAL);
  return(0);                                    /* success */
}

int
TXfunc_parselongitude(FLD *longitude)
/* SQL function parselongitude().
 * Returns 0 on success, else FOP_... error.
 */
{
  static CONST char     fn[] = "TXfunc_parselongitude";
  char                  *s;
  size_t                sz;
  TXPMBUF               *pmbuf = TXPMBUFPN;
  ft_double             *res = NULL;

  if (longitude == FLDPN ||
      (s = (char *)getfld(longitude, &sz)) == CHARPN)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "NULL argument or value");
      return(FOP_EINVAL);
    }
  /* +1 for fldmath nul: */
  res = (ft_double *)TXcalloc(TXPMBUFPN, fn, 2, sizeof(ft_double));
  if (!res)
    return(FOP_ENOMEM);

  /* For flexibility, we do not care if not all of `s' is parsed: */
  *res = (ft_double)TXparseCoordinate(s, 1, CHARPPN);

  releasefld(longitude);                        /* before changing type */
  longitude->type = FTN_DOUBLE;
  longitude->elsz = sizeof(ft_double);
  setfldandsize(longitude, res, sizeof(ft_double) + 1, FLD_FORCE_NORMAL);
  return(0);                                    /* success */
}

/* ------------------------------------------------------------------------ */

static int
TXlatlon2geocodeGuts(FLD *fldLat, FLD *fldLon, ft_double *lat, ft_double *lon,
                     ft_long *geocode)
/* Guts of SQL functions latlon2geocode[area](): sets `*lat'/`*lon'/
 * `*geocode' from fields `fldLat'/`fldLon'.
 * If only 1 arg given (`fldLat'), must be a string, and must have
 * both lat and lon (i.e. TXparseLocation()-parseable value).
 * Returns 2 on success if both args used, 1 on success if only `fldLat'
 * used (has longitude too), 0 if geocode -1 (w/non-FOP_EINVAL),
 * else FOP_... error code.
 */
{
  static CONST char     fn[] = "TXlatlon2geocodeGuts";
  char                  *s, *e;
  int                   gotLonFromFldLat = 0, gotGeocode = 0;
  size_t                sz;
  void                  *v;
  double                tmpLat, tmpLon;
  ft_double             tmpFtDouble;

  if (!fldLat)
    {
      putmsg(MERR + UGE, fn, "NULL latitude value");
      goto invalid;
    }
  v = getfld(fldLat, &sz);
  if (!v || (sz <= 0 && (fldLat->type & DDTYPEBITS) != FTN_CHAR))
    {
      putmsg(MERR + UGE, fn, "%s%slatitude value", (!v ? "NULL " : ""),
             (sz <= 0 ? "empty " : ""));
      goto invalid;
    }
  /* We declared the function to take any type, so that we can cast
   * a single `lat lon' varchar ourselves and get both values (if only
   * 1 arg given). Downside is we have to handle all types explicitly:
   */
  switch (fldLat->type & DDTYPEBITS)
    {
    case FTN_DOUBLE:
      /* WTF on sparc solaris 2.5.1 `v' may be unaligned for double: */
      memcpy(&tmpFtDouble, v, sizeof(ft_double));
      if (TXDOUBLE_IS_NaN(tmpFtDouble)) goto parseErr;
      *lat = tmpFtDouble;
      break;
    case FTN_FLOAT:
      if (TXFLOAT_IS_NaN(*(ft_float *)v)) goto parseErr;
      *lat = *(ft_float *)v;
      break;
    case FTN_LONG:      *lat = *(ft_long *)v;    break;
    case FTN_INT:       *lat = *(ft_int *)v;     break;
    case FTN_INTEGER:   *lat = *(ft_integer *)v; break;
    case FTN_DWORD:     *lat = *(ft_dword *)v;   break;
    case FTN_WORD:      *lat = *(ft_word *)v;    break;
    case FTN_SMALLINT:  *lat = *(ft_smallint *)v;break;
    case FTN_SHORT:     *lat = *(ft_short *)v;   break;
#ifdef EPI_INT64_SQL
    case FTN_INT64:     *lat = *(ft_int64 *)v;   break;
    case FTN_UINT64:    *lat = *(ft_uint64 *)v;  break;
#endif /* EPI_INT64_SQL */
    case FTN_CHAR:
      s = (char *)v;
      /* Try for a full location first: */
      *geocode = TXparseLocation(s, &e, &tmpLat, &tmpLon);
      if (*geocode != -1L)                      /* success: got location */
        {
          *lat = tmpLat;
          *lon = tmpLon;
          gotLonFromFldLat = gotGeocode = 1;
          break;
        }
      /* Not a full lat+lon location.  Try for just latitude: */
      *lat = TXparseCoordinate(s, 0, &e);
      if (e == s || TXDOUBLE_IS_NaN(*lat)) goto parseErr; /* no/bad number */
      break;
    default:
      goto invalid;                             /* unsupported arg type */
    }

  if (!gotLonFromFldLat)                        /* do not have lon yet */
    {
      if (!fldLon) goto parseErr;
      v = getfld(fldLon, &sz);
      if (!v || (sz <= 0 && (fldLon->type & DDTYPEBITS) != FTN_CHAR))
        {
          putmsg(MERR + UGE, fn, "%s%slongitude value", (!v ? "NULL " : ""),
                 (sz <= 0 ? "empty " : ""));
          goto invalid;
        }
      switch (fldLon->type & DDTYPEBITS)
        {
        case FTN_DOUBLE:
          /* WTF on sparc solaris 2.5.1 `v' may be unaligned for double: */
          memcpy(&tmpFtDouble, v, sizeof(ft_double));
          if (TXDOUBLE_IS_NaN(tmpFtDouble)) goto parseErr;
          *lon = tmpFtDouble;
          break;
        case FTN_FLOAT:
          if (TXFLOAT_IS_NaN(*(ft_float *)v)) goto parseErr;
          *lon = *(ft_float *)v;
          break;
        case FTN_LONG:      *lon = *(ft_long *)v;    break;
        case FTN_INT:       *lon = *(ft_int *)v;     break;
        case FTN_INTEGER:   *lon = *(ft_integer *)v; break;
        case FTN_DWORD:     *lon = *(ft_dword *)v;   break;
        case FTN_WORD:      *lon = *(ft_word *)v;    break;
        case FTN_SMALLINT:  *lon = *(ft_smallint *)v;break;
        case FTN_SHORT:     *lon = *(ft_short *)v;   break;
#ifdef EPI_INT64_SQL
        case FTN_INT64:     *lon = *(ft_int64 *)v;   break;
        case FTN_UINT64:    *lon = *(ft_uint64 *)v;  break;
#endif /* EPI_INT64_SQL */
        case FTN_CHAR:
          s = (char *)v;
          *lon = TXparseCoordinate(s, 0x1, &e);
          if (e == s || TXDOUBLE_IS_NaN(*lon))  /* no/bad number */
            {
            parseErr:
              /* For parse errors, set field to a -1 (error) value.
               * Do not return FOP_EINVAL, so that user gets this value
               * and can do their own (silent) error checking:
               */
              *lat = *lon = 0.0;
              *geocode = -1L;
              return(0);                        /* -1 geocode */
            }
          break;
        default:
        invalid:
          *lat = *lon = 0.0;
          *geocode = -1L;
          return(FOP_EINVAL);                   /* unsupported arg type */
        }
    }
  if (!gotGeocode) *geocode = TXlatlon2geocode(*lat, *lon);
  return(gotLonFromFldLat ? 1 : 2);             /* success */
}

/* ------------------------------------------------------------------------ */

int
TXfunc_latlon2geocode(FLD *fld_lat, FLD *fld_lon)
/* SQL function latlon2geocode(fld_lat [, fld_lon]).
 * If only 1 arg given (`fld_lat'), must be a string, and must have
 * both lat and lon (eg. `40N 80W' `40 -80' `40, -80').
 */
{
  static CONST char     fn[] = "TXfunc_latlon2geocode";
  int                   rc;
  ft_double             lat = 0.0, lon = 0.0;
  ft_long               *res = (ft_long *)NULL;

  /* +1 for fldmath nul: */
  res = (ft_long *)TXcalloc(TXPMBUFPN, fn, 2, sizeof(ft_long));
  if (res == (ft_long *)NULL) return(FOP_ENOMEM);

  rc = TXlatlon2geocodeGuts(fld_lat, fld_lon, &lat, &lon, res);
  switch (rc)
    {
    case 1:                                     /* success; 1 arg used */
      /* We got longitude too from `fldLat'; if `fldLon', too many args: */
      if (fld_lon)
        {
          txpmbuf_putmsg(TXPMBUFPN, MERR + UGE, fn,
                         "Syntax error: Longitude already specified in 1st arg; 2nd longitude arg is redundant");
          res = TXfree(res);
          return(FOP_EINVAL);
        }
      /* fall through: */
    case 2:                                     /* success; 2 args used */
      /* ...Guts() already gave us geocode in `*res' */
      break;
    case 0:                                     /* geocode -1 */
      *res = -1L;
      break;
    default:                                    /* other error */
      res = TXfree(res);
      return(rc);
    }

  releasefld(fld_lat);                          /* before changing type */
  fld_lat->type = FTN_LONG;
  fld_lat->elsz = sizeof(ft_long);
  setfldandsize(fld_lat, res, sizeof(ft_long) + 1, FLD_FORCE_NORMAL);
  return 0;
}

/* ------------------------------------------------------------------------ */

int
TXfunc_latlon2geocodearea(FLD *fld_lat, FLD *fld_lon, FLD *radius)
/* SQL function latlon2geocodearea(fld_lat[, fld_lon], radius).
 * If `fld_lon' not given, `fld_lat' must be a string, and must have
 * both lat and lon (eg. `40N 80W' `40 -80' `40, -80').
 */
{
  static CONST char     fn[] = "TXfunc_latlon2geocodearea";
  int                   rc, numVals = 2;
  char                  *s, *e;
  ft_double             lat = 0.0, lon = 0.0, rad = 0.0;
  ft_double             latCorner, lonCorner, tmpFtDouble;
  ft_long               *res = (ft_long *)NULL, geocode;
  void                  *v;
  size_t                sz;
  TXPMBUF               *pmbuf = TXPMBUFPN;

  /* +1 for fldmath nul: */
  res = (ft_long *)TXcalloc(pmbuf, fn, numVals + 1, sizeof(ft_long));
  if (res == (ft_long *)NULL) return(FOP_ENOMEM);

  rc = TXlatlon2geocodeGuts(fld_lat, fld_lon, &lat, &lon, &geocode);
  switch (rc)
    {
    case 1:                                     /* success; 1 arg used */
      /* Longitude obtained from `fld_lat'; `fld_lon' (if given) is radius: */
      if (fld_lon)
        {
          if (radius)
            {
              txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "Syntax error: Longitude already specified in 1st arg; 2nd longitude arg is redundant");
              goto invalid;
            }
          radius = fld_lon;
          fld_lon = FLDPN;
        }
      /* fall through: */
    case 2:                                     /* success; 2 args used */
      break;
    case 0:                                     /* geocode -1 */
      *res = -1;
      numVals = 1;
      goto returnIt;
    default:                                    /* other error */
      res = TXfree(res);
      return(rc);
    }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   * Get the radius value:
   */
  if (!radius)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
                     "Syntax error: Missing radius argument");
      goto invalid;
    }
  v = getfld(radius, &sz);
  switch (radius->type & DDTYPEBITS)
    {
    case FTN_DOUBLE:
      /* WTF on sparc solaris 2.5.1 `v' may be unaligned for double: */
      memcpy(&tmpFtDouble, v, sizeof(ft_double));
      rad = tmpFtDouble;
      break;
    case FTN_FLOAT:     rad = *(ft_float *)v;   break;
    case FTN_LONG:      rad = *(ft_long *)v;    break;
    case FTN_INT:       rad = *(ft_int *)v;     break;
    case FTN_INTEGER:   rad = *(ft_integer *)v; break;
    case FTN_DWORD:     rad = *(ft_dword *)v;   break;
    case FTN_WORD:      rad = *(ft_word *)v;    break;
    case FTN_SMALLINT:  rad = *(ft_smallint *)v;break;
    case FTN_SHORT:     rad = *(ft_short *)v;   break;
#ifdef EPI_INT64_SQL
    case FTN_INT64:     rad = *(ft_int64 *)v;   break;
    case FTN_UINT64:    rad = *(ft_uint64 *)v;  break;
#endif /* EPI_INT64_SQL */
    case FTN_CHAR:
      s = (char *)v;
      rad = TXgeoStrtod(s, &e);
      if (e == s || *e != '\0')                 /* no/bad number */
        {
          /* Return -1 silently on parse error, like TXfunc_latlon2geocode: */
          *res = -1;
          numVals = 1;
          goto returnIt;
        }
      break;
    default:                                    /* unsupported type */
    invalid:
      res = TXfree(res);
      return(FOP_EINVAL);
    }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   * Compute the box; derived from VXgeo2code() radius logic:
   */
  if (rad < 0.0) rad = -rad;

  latCorner = lat - rad;                    /* lower-left box corner */
  lonCorner = lon - rad;                    /* "" */
  if (latCorner < -90.0) latCorner = -90.0;   /* range safety */
  if (lonCorner < -180.0) lonCorner = -180.0; /* "" */
  res[0] = TXlatlon2geocode(latCorner, lonCorner);

  latCorner = lat + rad;                    /* upper-right box corner */
  lonCorner = lon + rad;                    /* "" */
  if (latCorner > 90.0) latCorner = 90.0;   /* range safety */
  if (lonCorner > 180.0) lonCorner = 180.0; /* "" */
  res[1] = TXlatlon2geocode(latCorner, lonCorner);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   * Return the value:
   */
returnIt:
  releasefld(fld_lat);                          /* before changing type */
  /* Do not return a varchar `(geocode, geocode)' SQL string like
   * <geo2code $lat $lon $radius>; return a varlong(2) so that it can
   * truly be a parameter to BETWEEN (not SQL inline code+literals):
   */
  fld_lat->type = (FTN_LONG | DDVARBIT);
  fld_lat->elsz = sizeof(ft_long);
  setfldandsize(fld_lat, res, numVals*sizeof(ft_long) + 1, FLD_FORCE_NORMAL);
  return(0);
}

long
TXlatlon2geocode(double lat, double lon)
/* Returns geocode value for (lat, lon), or -1 on error.
 * lat/lon are in DDD.DDD decimal degrees format.
 * This should be the *only* place geocode bit encoding takes place,
 * to avoid code maintenance issues.
 */
{
  long latsec, lonsec, res, mask;  

  /* Normalize and validate inputs.  We allow longitude to roll over
   * (ie. 190 east = -170 west), for linear ease of use near the
   * International Dateline, but only once (ie. limit is +/-360)
   * to help detect true bad inputs.  We do not allow latitude to
   * exceed +/-90, since unlike longitude there is no farther-north
   * or -south.  -1 is returned for error (and all negative values
   * should be invalid.  KNG 20071101
   */
  if (lat < (double)-90.0 || lat > (double)90.0) return(-1L);
  if (lon < (double)-360.0 || lon > (double)360.0) return(-1L);
  if (lon < (double)-180.0) lon += (double)360.0;
  else if (lon > (double)180.0) lon -= (double)360.0;

  lat *= (double)(60.0*60.0);                   /* convert to seconds */
  lon *= (double)(60.0*60.0);
  lat += (double)(90.0*60.0*60.0);              /* make non-negative */
  lon += (double)(180.0*60.0*60.0);
  /* Round up when converting double to long, so that lat/lon -> geocode
   * -> lat/lon does not change due to truncation (given at least ~0.5
   * second accuracy in the doubles for lat/lon):  KNG 20071109
   */
  latsec = (long)(lat + (double)(0.5));         /* convert to long */
  lonsec = (long)(lon + (double)(0.5));
  latsec = ((unsigned long)latsec & (unsigned long)LAT_MASK);
  lonsec = ((unsigned long)lonsec & (unsigned long)LON_MASK);
#if PRESHIFT > 0
  latsec >>= PRESHIFT;                          /* drop precision if needed */
  lonsec >>= PRESHIFT;
#endif
  /* Mingle the bits of latitude and longitude, alternating between
   * the two.  Note that latitude is most-significant, since it
   * takes 1 fewer bits and thus won't cause a negative encoding.
   * KNG 20071101 important now that we return -1 for error:
   */
  for (res = 0L, mask = (1L << (BITS_PER - 1)); mask > 0L; mask >>= 1)
    {
      res <<= 1;
      res |= ((latsec & mask) ? 1L : 0L);
      res <<= 1;
      res |= ((lonsec & mask) ? 1L : 0L);
    }
  return res;
}

int
TXcanonicalizeGeocodeBox(long *c1, long *c2)
/* Makes rectangular box defined by diagonally-opposite geocodes `*c1'/`*c2'
 * into a canonical box, ie. `*c1' is SW and `*c2' NE (c1->lat < c2->lat
 * and c1->lon < c2->lon), which is assumed by geocode indexes.
 * Returns 1 if box was originally canon, 0 if not (was modified).
 */
{
  unsigned long lat1, lat2, lon1, lon2, x;
  int           isCanon = 1;
#if EPI_OS_LONG_BITS == 32
#  define LAT_CODE_MASK       0xaaaaaaaaUL
#elif EPI_OS_LONG_BITS == 64
#  define LAT_CODE_MASK       0xaaaaaaaaaaaaaaaaUL
#else
  error define LAT_CODE_MASK;
#endif

  /* Separate into lat and lon bits.  Note that we do not need to
   * fully decode them; just enough to check lat1 < lat2 and lon1 < lon2:
   */
  lat1 = ((unsigned long)*c1 & LAT_CODE_MASK);
  lat2 = ((unsigned long)*c2 & LAT_CODE_MASK);
  lon1 = ((unsigned long)*c1 & (LAT_CODE_MASK >> 1));
  lon2 = ((unsigned long)*c2 & (LAT_CODE_MASK >> 1));
  if (lat1 > lat2)                              /* wrong order: swap */
    {
      isCanon = 0;
      x = lat1;
      lat1 = lat2;
      lat2 = x;
    }
  if (lon1 > lon2)                              /* wrong order: swap */
    {
      isCanon = 0;
      x = lon1;
      lon1 = lon2;
      lon2 = x;
    }
  if (!isCanon)
    {
      *c1 = (long)(lat1 | lon1);                /* reassemble correctly */
      *c2 = (long)(lat2 | lon2);
    }
  return(isCanon);
}

/******************************************************************/

int
TXfunc_geocode2lat(FLD *fld_geocode)
{
  static CONST char     fn[] = "TXfunc_geocode2lat";
  size_t sz;
  ft_long *geocode;
  ft_double *lat;

  if(!fld_geocode) {
    putmsg(MERR + UGE, fn, "null FLD param");
    return(FOP_EINVAL);
  }
  if((fld_geocode->type & DDTYPEBITS) != FTN_LONG) {
    putmsg(MERR + UGE, fn, "geocode not a long");
    return(FOP_EINVAL);
  }
  geocode = (ft_long *)getfld(fld_geocode, &sz);

  lat = (ft_double*)TXcalloc(TXPMBUFPN, fn, 1, sizeof(ft_double));
  if(lat == NULL)
    {
    return(FOP_ENOMEM);
  }

  *lat=TXgeocode2lat(*geocode);
  setfld(fld_geocode, lat, sizeof(ft_double));
  fld_geocode->elsz = sizeof(ft_double);
  fld_geocode->n = 1;
  fld_geocode->size = sizeof(ft_double);
  fld_geocode->type = FTN_DOUBLE;
  return 0;
}

double
TXgeocode2lat(long geocode)
/* Returns latitude of `geocode', or NaN if invalid.
 */
{
  double lat,lon;
  TXgeocodeDecode(geocode,&lat,&lon);
  return lat;
}

int
TXfunc_geocode2lon(FLD *fld_geocode)
{
  static CONST char     fn[] = "TXfunc_geocode2lon";
  size_t sz;
  ft_long *geocode;
  ft_double *lon;

  if(!fld_geocode) {
    putmsg(MERR + UGE, fn, "null FLD param");
    return(FOP_EINVAL);
  }
  if((fld_geocode->type & DDTYPEBITS) != FTN_LONG) {
    putmsg(MERR + UGE, fn, "geocode not a long");
    return(FOP_EINVAL);
  }
  geocode = (ft_long *)getfld(fld_geocode, &sz);

  if( (lon=TXmalloc(TXPMBUFPN, fn, sizeof(ft_double)+1)) == NULL )
    return(FOP_EINVAL);

  *lon=TXgeocode2lon(*geocode);
  setfld(fld_geocode, lon, sizeof(ft_double));
  fld_geocode->elsz = sizeof(ft_double);
  fld_geocode->n = 1;
  fld_geocode->size = sizeof(ft_double);
  fld_geocode->type = FTN_DOUBLE;
  return 0;
}

double
TXgeocode2lon(long geocode)
/* Returns longitude of `geocode', or NaN if invalid.
 */
{
  double lat,lon;
  TXgeocodeDecode(geocode, &lat,&lon);
  return lon;
}

int
TXgeocodeDecode(long geocode, double *lat, double *lon)
/* Sets `*lat'/`*lon' to latitude/longitude of geocode value `geocode'
 * (and returns 1), or NaN if invalid (and returns 0).
 * Lat/lon are returned in decimal degrees format.
 * This should be the *only* place geocode bit decoding takes place,
 * to avoid code maintenance issues.
 */
{
  long latsec, lonsec, mask;
  /* double lat, lon; */

  /* KNG 20071101 -1 is defined as error for geocode, and no negative
   * geocode values should be returned either, so map them as error:
   */
  if (geocode < 0L)                             /* invalid geocode */
    {
      /* wtf do not copy value, even to a local var; some platforms
       * (x86_64 linux2.6.9-64-64) modify the copy such that
       * TXDOUBLE_IS_NaN() fails:
       */
      TXDOUBLE_SET_NaN(*lat);
      TXDOUBLE_SET_NaN(*lon);
      return(0);
    }

  for (latsec = lonsec = 0L, mask = 1L; mask < (1L << BITS_PER); mask <<= 1)
    {
      if (geocode & 1L) lonsec |= mask;
      if (geocode & 2L) latsec |= mask;
      geocode >>= 2;
    }
#if PRESHIFT > 0
  latsec <<= PRESHIFT;
  lonsec <<= PRESHIFT;
#endif

  latsec -= 90L*60L*60L;    /* undo the "make non-negative */
  lonsec -= 180L*60L*60L;

  *lon = (double)lonsec;   /* we now need decimal places, assign to doubles */
  *lat = (double)latsec;

  *lat /= (double)(60*60); /* convert back from seconds to decimal degrees */
  *lon /= (double)(60*60);

  return(1);                                    /* ok */
}

/* this logic applies for both distGeocode and distlatlon, so it's
 * pulled out into a function */
static int
getMethod(FLD *fld_method, const char *fn)
{
  size_t sz;
  ft_char   *methodChar;
  ft_long   *methodLong;
  ft_double *methodDouble;
  int method;

  if(fld_method==NULL)
    {
      method=TXGEO_GREAT_CIRCLE;
    }
  else
    {
      switch(fld_method->type & DDTYPEBITS)
        {
        case FTN_CHAR:
          methodChar=(ft_char *)getfld(fld_method, &sz);
          if(strcmp(methodChar,"pythag")==0
             || strcmp(methodChar,"pythagorean")==0)
            {
              method=TXGEO_PYTHAG;
            }
          else if(strcmp(methodChar,"greatcircle")==0)
            {
              method=TXGEO_GREAT_CIRCLE;
            }
          else
            {
              putmsg(MWARN + UGE, fn, "invalid method string (%s): defaulting to great circle",methodChar);
              method=TXGEO_GREAT_CIRCLE;
            }
          break;
        case FTN_DOUBLE:
          methodDouble=(ft_double *)getfld(fld_method, &sz);
          method=(int)*methodDouble;
          break;
        case FTN_LONG:
          methodLong=(ft_long *)getfld(fld_method, &sz);
          method=(int)*methodLong;
          break;
        default:
          putmsg(MWARN + UGE, fn, "invalid method field type (%i): defaulting to great circle",fld_method->type & DDTYPEBITS);
          method=TXGEO_GREAT_CIRCLE;
          break;
        }
    }
  return method;
}

/** Just a wrapper to call a distance function based on 
 * the parameters. */
int
TXfunc_distGeocode(FLD *fld_geocode1, FLD *fld_geocode2, FLD *fld_method)
{
  static CONST char     fn[] = "TXfunc_distGeocode";
  size_t sz;
  ft_long *geocode1, *geocode2;
  int method;
  ft_double *distance;

  method = getMethod(fld_method,fn);
 
  if(!fld_geocode1)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_geocode1->type & DDTYPEBITS) != FTN_LONG)
    {
      putmsg(MERR + UGE, fn, "geocode1 not a long");
      return(FOP_EINVAL);
    }
  geocode1 = (ft_long *)getfld(fld_geocode1, &sz);

  if(!fld_geocode2)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_geocode2->type & DDTYPEBITS) != FTN_LONG)
    {
      putmsg(MERR + UGE, fn, "geocode2 not a long");
      return(FOP_EINVAL);
    }
  geocode2 = (ft_long *)getfld(fld_geocode2, &sz);

  distance = (ft_double*) TXcalloc(TXPMBUFPN, fn, 1, sizeof(ft_double)+1);
  if(distance == NULL)
      return(FOP_EINVAL);

  *distance = TXdistGeocode(*geocode1, *geocode2, method);
  setfld(fld_geocode1, distance, sizeof(ft_double));
  fld_geocode1->n = 1;
  fld_geocode1->elsz = sizeof(ft_double);
  fld_geocode1->size = sizeof(ft_double);
  fld_geocode1->type = FTN_DOUBLE;
  return 0;
}

double
TXdistGeocode(long geocode1, long geocode2, int method)
{
  static CONST char     fn[] = "TXdistGeocode";
  double lat1, lon1, lat2, lon2;

  if (!TXgeocodeDecode(geocode1,&lat1,&lon1))
    {
      putmsg(MERR + UGE, fn, "Invalid geocode1 value %ld", geocode1);
      return((double)(-1.0));
    }
  if (!TXgeocodeDecode(geocode2,&lat2,&lon2))
    {
      putmsg(MERR + UGE, fn, "Invalid geocode2 value %ld", geocode2);
      return((double)(-1.0));
    }
  return TXdistlatlon(lat1,lon1,lat2,lon2,method);
}

/**********************************************/

int
TXfunc_distlatlon(FLD *fld_lat1, FLD *fld_lon1, FLD *fld_lat2, FLD *fld_lon2,
                  FLD *fld_method)
{
  static CONST char     fn[] = "TXfunc_distlatlon";
  size_t sz;
  ft_double *lat1, *lon1, *lat2, *lon2;

  int method;

  method = getMethod(fld_method,fn);

  if(!fld_lat1)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_lat1->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "lat1 not a double");
      return(FOP_EINVAL);
    }
  lat1 = (ft_double *)getfld(fld_lat1, &sz);

  if(!fld_lon1)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_lon1->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "lon1 not a double");
      return(FOP_EINVAL);
    }
  lon1 = (ft_double *)getfld(fld_lon1, &sz);

  if(!fld_lat2)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_lat2->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "lat2 not a double");
      return(FOP_EINVAL);
    }
  lat2 = (ft_double *)getfld(fld_lat2, &sz);

  if(!fld_lon2)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_lon2->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "lon2 not a double");
      return(FOP_EINVAL);
    }
  lon2 = (ft_double *)getfld(fld_lon2, &sz);

  *lat1 = TXdistlatlon(*lat1, *lon1, *lat2, *lon2, method);
  return 0;
}

double
TXdistlatlon(double lat1, double lon1, double lat2, double lon2, int method)
{
  static CONST char     fn[] = "TXfunc_distlatlon";

  switch(method)
    {
    case TXGEO_PYTHAG:
      return TXpythagMiles(lat1, lon1, lat2, lon2);
    case TXGEO_GREAT_CIRCLE:
      return TXgreatCircle(lat1, lon1, lat2, lon2);
    default:
      putmsg(MWARN + UGE, fn, "invalid method value (%i): defaulting to great circle",method);
      return TXgreatCircle(lat1, lon1, lat2, lon2);
    }
}

/**
 * minimum function.  Yes a macro could be used instead,
 * but "a" in this case is a very long mathematical function
 * and we don't want to execute it twice.
 */ 
static double
minFunc(double a, double b)
{
  if(a<b)
    return a;
  else
    return b;
}

/******************************************************************/

int
TXfunc_dms2dec(FLD *fld_dms)
{
  static CONST char     fn[] = "TXfunc_dms2dec";
  size_t sz;
  ft_double *dms;
  
  if(!fld_dms)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_dms->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "dms not a double");
      return(FOP_EINVAL);
    }
  dms = (ft_double *)getfld(fld_dms, &sz);

  *dms = TXdms2dec(*dms);
  return 0;
}

/** converts DMS format (DDDMMSS) to decimal degrees */
double
TXdms2dec(double dms)
{
  double dec;
  dec = (int)dms / 10000;            /* convert degrees */
  dms -= dec * 10000;
  dec += ( (int) dms / 100 ) / 60.0; /* convert minutes */
  dms -= ( (int) dms / 100 ) * 100;
  dec += dms / (60*60);              /* convert seconds */
  return dec;
}

int
TXfunc_dec2dms(FLD *fld_dec)
{
  static CONST char     fn[] = "TXfunc_dec2dms";
  size_t sz;
  ft_double *dec;

  if(!fld_dec)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_dec->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "dec not a double");
      return(FOP_EINVAL);
    }
  dec = (ft_double *)getfld(fld_dec, &sz);

  *dec = TXdec2dms(*dec);
  return 0;
}

/** converts decimal degrees to DMS format (DDDMMSS) */
double
TXdec2dms(double dec)
{
  double dms;
  int sign;
  dms = ( (int)dec ) * 10000;
  dec -=  (int)dec;

  dms += ( (int) (dec * 60.0) ) * 100;
  dec -= ( (int) (dec * 60.0) ) / 60.0;
  /* there can be some rounding issues - if you dec2dms
   * 33.8666667 it'll correctly give 335200, but if you dec2dms
   * 33.8666666, it'll incorrectly give  335160.  test to see if it
   * would end up with 60 seconds and handle approprioately. */

  /* we want to increase the magnitude by .001, so make sign negative
   * if dec is negative. */
  if(dec>0)
    sign=1;
  else
    sign=-1;

  if( (int)((dec+.0001*sign)*60) == 1*sign)
    {
      dms += 100*sign;
      /* if there were already 59 minutes, then we need
       * to roll over the minutes into degrees too */
      if( ((int)dms)%6000==0)
        {
          /* add enough to roll over the minutes into
           * 1 more degree */
          dms += 4000*sign;
        }
    }
  else
    dms += dec*60*60;

  return dms;
}

int
TXfunc_greatCircle(FLD *fld_lat1, FLD *fld_lon1, FLD *fld_lat2, FLD *fld_lon2)
{
  static CONST char     fn[] = "TXfunc_greatCircle";
  size_t sz;
  ft_double *lat1, *lon1, *lat2, *lon2;

  if(!fld_lat1)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_lat1->type & DDTYPEBITS) != FTN_DOUBLE) {
    putmsg(MERR + UGE, fn, "lat1 not a double");
    return(FOP_EINVAL);
  }
  lat1 = (ft_double *)getfld(fld_lat1, &sz);


  if(!fld_lon1)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_lon1->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "lon1 not a double");
      return(FOP_EINVAL);
    }
  lon1 = (ft_double *)getfld(fld_lon1, &sz);


  if(!fld_lat2)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_lat2->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "lat2 not a double");
      return(FOP_EINVAL);
    }
  lat2 = (ft_double *)getfld(fld_lat2, &sz);


  if(fld_lon2)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_lon2->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "lon2 not a double");
      return(FOP_EINVAL);
    }
  lon2 = (ft_double *)getfld(fld_lon2, &sz);

  *lat1 = TXgreatCircle(*lat1, *lon1, *lat2, *lon2);
  return 0;
}

/**
 * perform the  Haversine Formula formula to calculate the Great Circle distance
 * between latlon coordinates.
 */
double
TXgreatCircle(double lat1, double lon1, double lat2, double lon2)
{
  double radlat1 = (PI * lat1) / 180;
  double radlon1 = (PI * lon1) / 180;
  double radlat2 = (PI * lat2) / 180;
  double radlon2 = (PI * lon2) / 180;
  /*  formula: (from http://mathforum.org/library/drmath/view/51879.html)
               a = (sin(dlat/2))^2 + cos(radlat1) * cos(radlat2) * (sin(dlon/2))^2
               c = 2 * asin(min(1,sqrt(a)));
               distance = c * EARTH_RADIUS; */
  return TXGEO_EARTH_RADIUS * 2 * asin(minFunc(1,sqrt(pow(sin((radlat2-radlat1)/2),2) + cos(radlat1) * cos(radlat2) * pow(sin((radlon2-radlon1)/2),2))));
}

int
TXfunc_pythag(FLD *fld_x1, FLD *fld_y1, FLD *fld_x2, FLD *fld_y2)
{
  static CONST char     fn[] = "TXfunc_pythag";
  size_t sz;
  ft_double *x1, *y1, *x2, *y2;

  if(!fld_x1)
    {
      putmsg(MERR + UGE, fn, "null FLD param");
      return(FOP_EINVAL);
    }
  if((fld_x1->type & DDTYPEBITS) != FTN_DOUBLE)
    {
      putmsg(MERR + UGE, fn, "x2 not a double");
      return(FOP_EINVAL);
    }
  x1 = (ft_double *)getfld(fld_x1, &sz);

  if(!fld_y1) {
    putmsg(MERR + UGE, fn, "null FLD param");
    return(FOP_EINVAL);
  }
  if((fld_y1->type & DDTYPEBITS) != FTN_DOUBLE) {
    putmsg(MERR + UGE, fn, "y1 not a double");
    return(FOP_EINVAL);
  }
  y1 = (ft_double *)getfld(fld_y1, &sz);

  if(!fld_x2) {
    putmsg(MERR + UGE, fn, "null FLD param");
    return(FOP_EINVAL);
  }
  if((fld_x2->type & DDTYPEBITS) != FTN_DOUBLE) {
    putmsg(MERR + UGE, fn, "x2 not a double");
    return(FOP_EINVAL);
  }
  x2 = (ft_double *)getfld(fld_x2, &sz);

  if(!fld_y2) {
    putmsg(MERR + UGE, fn, "null FLD param");
    return(FOP_EINVAL);
  }
  if((fld_y2->type & DDTYPEBITS) != FTN_DOUBLE) {
    putmsg(MERR + UGE, fn, "y2 not a double");
    return(FOP_EINVAL);
  }
  y2 = (ft_double *)getfld(fld_y2, &sz);
  
  *x1 = TXpythag(4,*x1,*y1,*x2,*y2);
  if (*x1 < (double)0.0) return(FOP_EINVAL);
  return 0;
}

int
TXfunc_pythagMiles(FLD *fld_lat1, FLD *fld_lon1, FLD *fld_lat2, FLD *fld_lon2)
{
  static CONST char     fn[] = "TXfunc_pythagMiles";
  size_t sz;
  ft_double *lat1, *lon1, *lat2, *lon2;

  if(fld_lat1) {
    if((fld_lat1->type & DDTYPEBITS) == FTN_DOUBLE) {
      lat1 = (ft_double *)getfld(fld_lat1, &sz);
    } else {
      putmsg(MERR + UGE, fn, "lat1 not a double");
      return(FOP_EINVAL);
    }
  } else {
    putmsg(MERR + UGE, fn, "null FLD param");
    return(FOP_EINVAL);
  }
  if(fld_lon1) {
    if((fld_lon1->type & DDTYPEBITS) == FTN_DOUBLE) {
      lon1 = (ft_double *)getfld(fld_lon1, &sz);
    } else {
      putmsg(MERR + UGE, fn, "lon1 not a double");
      return(FOP_EINVAL);
    }
  } else {
    putmsg(MERR + UGE, fn, "null FLD param");
    return(FOP_EINVAL);
  }
  if(fld_lat2) {
    if((fld_lat2->type & DDTYPEBITS) == FTN_DOUBLE) {
      lat2 = (ft_double *)getfld(fld_lat2, &sz);
    } else {
      putmsg(MERR + UGE, fn, "lat2 not a double");
      return(FOP_EINVAL);
    }
  } else {
    putmsg(MERR + UGE, fn, "null FLD param");
    return(FOP_EINVAL);
  }
  if(fld_lon2) {
    if((fld_lon2->type & DDTYPEBITS) == FTN_DOUBLE) {
      lon2 = (ft_double *)getfld(fld_lon2, &sz);
    } else {
      putmsg(MERR + UGE, fn, "lon2 not a double");
      return(FOP_EINVAL);
    }
  } else {
    putmsg(MERR + UGE, fn, "null FLD param");
    return(FOP_EINVAL);
  }

  *lat1 = TXpythagMiles(*lat1, *lon1, *lat2, *lon2);
  return 0;
}

double TXpythagMiles(double lat1, double lon1, double lat2, double lon2) {
  double dLat, dLon;

  /* these tests are for detecting "world wrap"; the distance between
   * longitude values -179 and 179 is actually 2, but pythag will see it
   * as 358.  If the differences between the lon values either way are
   * > 180, then it'll be close to "loop around" one of the values so the
   * travel is in the opposite direction. */
  if(lon2 - lon1 > 180)
    lon1 += 360;
  if(lon1 - lon2 > 180)
    lon2 += 360;

  dLat = (lat2 - lat1);
  /* the longitude difference gets scaled down depending on what our
   * latitiude position is, which is done by scaleLon() */
  dLon = (lon2 - lon1) * scaleLon((lat1 + lat2)/2) / 100;
  
  return  MILES_PER_DEGREE * sqrt(pow(dLat,2) + pow(dLon,2));
}


struct node {
  struct node *next;
  double value;
};

/**
 * calculates the distance between 2 points in N-dimensions.
 * coordinates should be passed as (x1, y1, z1, ..., x2, y2, z2, ...)
 * \param count the number of integers that are being passed, a.k.a.
 * N*2 when caculating in N-dimensions.
 * Returns -1 on error.
 */
double TXpythag(int count,...)
{
  static CONST char     fn[] = "TXpythag";
  struct node *head, *data;
  va_list argp;
  int i;
  double sum=0, ret;

  va_start(argp, count);
  if(count%2!=0) {
    putmsg(MERR + UGE, fn, "count parameter must be an even number");
    goto err;
  }
  if( (head = TXmalloc(TXPMBUFPN, fn, sizeof(struct node))) == NULL )
    goto err;
  head->next=NULL;
  data = head;

  for(i=0;i<count;i++) {

    /* if we're in the first half, just
     * queue up the data */
    if(i < count/2) {
      data->value = va_arg(argp, double);
      if( (data->next = TXmalloc(TXPMBUFPN, fn, sizeof(struct node))) == NULL)
        goto err;
      data = data->next;
      data->next=NULL;
      continue;
    }

    /* if we just hit the half-way point, move 
     * data back to the beginning */
    if(i == count/2) {
      data = head;
    }
    /* start summing up the squares of the differences */
    sum += pow(data->value-va_arg(argp, double),2);
    data = data->next;
    TXfree(head);
    head = data;
  }
  ret = sqrt(sum);
  goto done;

err:
  ret = (double)(-1.0);
done:
  va_end(argp);
  return(ret);
}
