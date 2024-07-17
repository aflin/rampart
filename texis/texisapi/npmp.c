#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include "sizes.h"
#include "os.h"
#include "npmp.h"
#include "pm.h"
#define NPMAXTOKS 80
#define LEXBUFSZ  132

/* Licensed MIT, to allow inclusion in rampart.utils - PBR */

/* PBR OCT 29 93 added kb,mb,gb to tables */

#ifdef _WIN32 /* MAW 05-05-92 - can't debug w/static globals in dll */
#  define STATIC
#else
#  define STATIC static
#endif

/*
		      Numeric Pattern Matcher Parser
		  Copyright (c) 1990 P. Barton Richards

This thing will turn most text forms of numbers into a quantity or range.
It will recognize most of the ways REAL people put numbers into text.
It WILL NOT recognize odd or improper formations. It was hard enough
getting it to recognize the "normal" things without screwing up.

NOT RECOGNIZED PROPERLY (improper format):
one hundred twenty three thousand four hundred fifty six million

NORMAL FORMAT:
four hundred fifty six million one hundred and twenty three thousand
456  million
4.56 million
456,123,000.00
scientific notation too

*/

/************************************************************************/

#define UNK  0
#define QTY  2
#define SQTY 3
#define MUL  4
#define SMUL 5
#define MAG  6
#define RNG  7
#define LTOP 8
#define LEOP 9
#define GTOP 10
#define GEOP 11
#define VOID 12
#define SIOP 13
#define FROP 14 					 /* fraction op */
#define FSUF 15
#define NSUF 16
#define SQF  17
#define ANDA 18
#define HYPH 19
#define BUCK 20
#define SPAC 21
#define NOOP 22 					       /* no op */




STATIC TTF ziptf = {"", 0.0, NOOP, 0.0, 0, 0, 0}; /* empty for null comps */

STATIC TTF tfa[]={
{"0",           0.0,     VOID,    0.0,     1, 0, 0 },
{"1",           1.0,     VOID,    0.0,     1, 0, 0 },
{"2",           2.0,     VOID,    0.0,     1, 0, 0 },
{"3",           3.0,     VOID,    0.0,     1, 0, 0 },
{"4",           4.0,     VOID,    0.0,     1, 0, 0 },
{"5",           5.0,     VOID,    0.0,     1, 0, 0 },
{"6",           6.0,     VOID,    0.0,     1, 0, 0 },
{"7",           7.0,     VOID,    0.0,     1, 0, 0 },
{"8",           8.0,     VOID,    0.0,     1, 0, 0 },
{"9",           9.0,     VOID,    0.0,     1, 0, 0 },
{"+",           1.0,     VOID,    0.0,     1, 0, 0 },
{"-",          -1.0,     HYPH,    0.0,     1, 0, 0 },
{"and an ",     1.0,     ANDA,    0.0,     0, 0, 0 },
{"and a ",      1.0,     ANDA,    0.0,     0, 0, 0 },
{"and ",        1.0,     ANDA,    0.0,     0, 0, 0 },
{"& ",          1.0,     ANDA,    0.0,     0, 0, 0 },
{"a ",          1.0,     NOOP,    0.0,     1, 0, 0 },
{"an ",         1.0,     NOOP,    0.0,     1, 0, 0 },
{"less than",   0.0,     LTOP,    0.0,     1, 0, 0 },
{"greater than",0.0,     GTOP,    0.0,     1, 0, 0 },
{"more than",   0.0,     GTOP,    0.0,     1, 0, 0 },
{"positive",    1.0,     SIOP,    0.0,     1, 0, 0 },
{"negative",   -1.0,     SIOP,    0.0,     1, 0, 0 },
{"/",           0.0,     FROP,    0.0,     0, 0, 0 },
{">",           0.0,     GTOP,    0.0,     1, 0, 0 },
{">=",          0.0,     GEOP,    0.0,     1, 0, 0 },
{"<",           0.0,     LTOP,    0.0,     1, 0, 0 },
{"<=",          0.0,     LEOP,    0.0,     1, 0, 0 },
{"a few",       3.0,     RNG,     6.0,     1, 0, 0 },
{"several",     3.0,     RNG,    10.0,     1, 0, 0 },
{"partial",     1.0e-32, RNG,     0.999999,1, 0, 0 },
{"a couple",    2.0,     SQTY,    0.0,     1, 0, 0 },
{"zero",        0.0,     SQTY,    0.0,     1, 0, 0 },
{"one",         1.0,     SQTY,    0.0,     1, 0, 0 },
{"two",         2.0,     SQTY,    0.0,     1, 0, 0 },
{"half",        2.0,     SQF,     0.0,     1, 0, 0 },
{"second",      2.0,     SQTY,    0.0,     1, 0, 0 },
{"three",       3.0,     SQTY,    0.0,     1, 0, 0 },
{"third",       3.0,     SQF,     0.0,     0, 0, 0 },
{"four",        4.0,     SQTY,    0.0,     1, 0, 0 },
{"fourth",      4.0,     SQF,     0.0,     0, 0, 0 },
{"forth",       4.0,     SQF,     0.0,     0, 0, 0 },
{"quarter",     4.0,     SQF,     0.0,     0, 0, 0 },
{"five",        5.0,     SQTY,    0.0,     1, 0, 0 },
{"fifth",       5.0,     SQF,     0.0,     0, 0, 0 },
{"six",         6.0,     SQTY,    0.0,     1, 0, 0 },
{"sixth",       6.0,     SQF,     0.0,     0, 0, 0 },
{"seven",       7.0,     SQTY,    0.0,     1, 0, 0 },
{"seventh",     7.0,     SQF,     0.0,     0, 0, 0 },
{"eight",       8.0,     SQTY,    0.0,     1, 0, 0 },
{"eighth",      8.0,     SQF,     0.0,     0, 0, 0 },
{"nine",        9.0,     SQTY,    0.0,     1, 0, 0 },
{"ninth",       9.0,     SQF,     0.0,     0, 0, 0 },
{"ten",         10.0,    SQTY,    0.0,     1, 0, 0 },
{"tenth",       10.0,    SQF,     0.0,     0, 0, 0 },
{"eleven",      11.0,    SQTY,    0.0,     1, 0, 0 },
{"eleventh",    11.0,    SQF,     0.0,     0, 0, 0 },
{"twelve",      12.0,    SQTY,    0.0,     1, 0, 0 },
{"twelfth",     12.0,    SQF,     0.0,     0, 0, 0 },
{"thirteen",    13.0,    SQTY,    0.0,     1, 0, 0 },
{"thirteenth",  13.0,    SQF,     0.0,     0, 0, 0 },
{"fourteen",    14.0,    SQTY,    0.0,     1, 0, 0 },
{"fourteenth",  14.0,    SQF,     0.0,     0, 0, 0 },
{"fifteen",     15.0,    SQTY,    0.0,     1, 0, 0 },
{"fifteenth",   15.0,    SQF,     0.0,     0, 0, 0 },
{"sixteen",     16.0,    SQTY,    0.0,     1, 0, 0 },
{"sixteenth",   16.0,    SQF,     0.0,     0, 0, 0 },
{"seventeen",   17.0,    SQTY,    0.0,     1, 0, 0 },
{"seventeenth", 17.0,    SQF,     0.0,     0, 0, 0 },
{"eighteen",    18.0,    SQTY,    0.0,     1, 0, 0 },
{"eighteenth",  18.0,    SQF,     0.0,     0, 0, 0 },
{"nineteen",    19.0,    SQTY,    0.0,     1, 0, 0 },
{"nineteenth",  19.0,    SQF,     0.0,     0, 0, 0 },
{"twenty",      20.0,    SQTY,    0.0,     1, 0, 0 },
{"twentieth",   20.0,    SQF,     0.0,     0, 0, 0 },
{"thirty",      30.0,    SQTY,    0.0,     1, 0, 0 },
{"thirtieth",   30.0,    SQF,     0.0,     0, 0, 0 },
{"fourty",      40.0,    SQTY,    0.0,     1, 0, 0 },
{"forty",       40.0,    SQTY,    0.0,     1, 0, 0 },
{"fortieth",    40.0,    SQF,     0.0,     0, 0, 0 },
{"fourtieth",   40.0,    SQF,     0.0,     0, 0, 0 },
{"fifty",       50.0,    SQTY,    0.0,     1, 0, 0 },
{"fiftieth",    50.0,    SQF,     0.0,     0, 0, 0 },
{"sixty",       60.0,    SQTY,    0.0,     1, 0, 0 },
{"sixtieth",    60.0,    SQF,     0.0,     0, 0, 0 },
{"seventy",     70.0,    SQTY,    0.0,     1, 0, 0 },
{"seventieth",  70.0,    SQF,     0.0,     0, 0, 0 },
{"eighty",      80.0,    SQTY,    0.0,     1, 0, 0 },
{"eightieth",   80.0,    SQF,     0.0,     0, 0, 0 },
{"ninety",      90.0,    SQTY,    0.0,     1, 0, 0 },
{"ninetieth",   90.0,    SQF,     0.0,     0, 0, 0 },
{"scores",      20.0,    MAG,   100.0,     1, 0, 0 },  /* MAW,PBR 10-28-93 */
{"dozens",      12.0,    MAG,    48.0,     1, 0, 0 },  /* MAW,PBR 10-28-93 */
{"hundreds",     1.0e2,  MAG,     1.0e3,   1, 0, 0 },
{"thousands",    1.0e3,  MAG,     1.0e6,   1, 0, 0 },
{"millions",     1.0e6,  MAG,     1.0e9,   1, 0, 0 },
{"billions",     1.0e9,  MAG,     1.0e12,  1, 0, 0 },
{"trillions",    1.0e12, MAG,     1.0e15,  1, 0, 0 },
{"score",        20.0,   SMUL,    0.0,     0, 0, 0 },
{"dozen",        12.0,   SMUL,    0.0,     1, 0, 0 },
{"gross",       144.0,   SMUL,    0.0,     1, 0, 0 },
{"hundred",      1.0e2,  SMUL,    0.0,     1, 0, 0 },
{"thousand",     1.0e3,  SMUL,    0.0,     1, 0, 0 },
{"million",      1.0e6,  SMUL,    0.0,     1, 0, 0 },
{"billion",      1.0e9,  SMUL,    0.0,     1, 0, 0 },
{"trillion",     1.0e12, SMUL,    0.0,     1, 0, 0 },
{"hundredth",    1.0e2,  SQF,     0.0,     0, 0, 0 },
{"thousandth",   1.0e3,  SQF,     0.0,     0, 0, 0 },
{"millionth",    1.0e6 , SQF,     0.0,     0, 0, 0 },
{"billionth",    1.0e9 , SQF,     0.0,     0, 0, 0 },
{"trillionth",   1.0e12, SQF,     0.0,     0, 0, 0 },
{"percent",      1.0e-2, SMUL,    0.0,     0, 0, 0 },
{"%",            1.0e-2, SMUL,    0.0,     0, 0, 0 },
{"dollars",      1.0   , BUCK,    0.0,     0, 0, 0 },
{"dollar",       1.0   , BUCK,    0.0,     0, 0, 0 },
{"cents",        1.0e-2, BUCK,    0.0,     0, 0, 0 },
{"cent",         1.0e-2, BUCK,    0.0,     0, 0, 0 },
{"deci",         1.0e-1, SMUL,    0.0,     0, 0, 0 },
{"centi",        1.0e-2, SMUL,    0.0,     0, 0, 0 },
{"milli",        1.0e-3, SMUL,    0.0,     0, 0, 0 },
{"micro",        1.0e-6, SMUL,    0.0,     0, 0, 0 },
{"nano",         1.0e-9, SMUL,    0.0,     0, 0, 0 },
{"pico",         1.0e-12,SMUL,    0.0,     0, 0, 0 },
{"femto",        1.0e-15,SMUL,    0.0,     0, 0, 0 },
{"atto",         1.0e-18,SMUL,    0.0,     0, 0, 0 },
{"deca",         1.0e1,  SMUL,    0.0,     1, 0, 0 },
{"deka",         1.0e1,  SMUL,    0.0,     1, 0, 0 },
{"hecto",        1.0e2  ,SMUL,    0.0,     0, 0, 0 },
/* `kilobyte' and `kb' clearly refer to bytes and thus have the potential
 * decimal/binary ambiguity.  But `kiloAnythingElse' is definitly decimal:
 */
{"kilo",         1.0e3  ,SMUL,    0.0,     0, 0, 0 },
{"kilobyte",     1.0e3  ,SMUL,    0.0,     0, 0, 1 },
{"kb"          , 1.0e3  ,SMUL,   0.0,     0, 0, 1 },
/* `k ' alone is usually referring to bytes: */
{"k "          , 1.0e3  ,SMUL,   0.0,     0, 0, 1 },
/* `kibi' would match `kibitz' etc.; seems only to be used with `byte' */
{"kibibyte",    1024.0  ,SMUL,   0.0,     0, 0, 0 },
{"kib ",        1024.0  ,SMUL,   0.0,     0, 0, 0 },
{"mega",         1.0e6  ,SMUL,    0.0,     0, 0, 0 },
{"megabyte",     1.0e6  ,SMUL,    0.0,     0, 0, 2 },
{"meg"         , 1.0e6  ,SMUL,   0.0,     0, 0, 2 },
{"mb"          , 1.0e6  ,SMUL,   0.0,     0, 0, 2 },
/* see `kibibyte' reasoning */
{"mebibyte", 1024.0*1024.0, SMUL, 0.0,    0, 0, 0 },
{"mib ",     1024.0*1024.0, SMUL, 0.0,    0, 0, 0 },
{"giga",         1.0e9  ,SMUL,    0.0,     0, 0, 0 },
{"gigabyte",     1.0e9  ,SMUL,    0.0,     0, 0, 3 },
{"gig"         , 1.0e9  ,SMUL,   0.0,     0, 0, 3 },
{"gb"          , 1.0e9  ,SMUL,   0.0,     0, 0, 3 },
/* see `mebibyte'/`mib' reasoning */
{"gibibyte", 1024.0*1024.0*1024.0, SMUL, 0.0, 0, 0, 0 },
{"gib ",     1024.0*1024.0*1024.0, SMUL, 0.0, 0, 0, 0 },
{"tera",         1.0e12 ,SMUL,    0.0,     0, 0, 0 },
{"terabyte",     1.0e12 ,SMUL,    0.0,     0, 0, 4 },
{"tb",           1.0e12 ,SMUL,    0.0,     0, 0, 4 },
/* see `mebibyte'/`mib' reasoning */
{"tebibyte", 1024.0*1024.0*1024.0*1024.0, SMUL, 0.0, 0, 0, 0 },
{"tib ",     1024.0*1024.0*1024.0*1024.0, SMUL, 0.0, 0, 0, 0 },
{"peta",         1.0e15 ,SMUL,    0.0,     0, 0, 0 },
{"petabyte",     1.0e15 ,SMUL,    0.0,     0, 0, 5 },
{"pb",           1.0e15 ,SMUL,    0.0,     0, 0, 5 },
/* see `mebibyte'/`mib' reasoning */
{"pebibyte", 1024.0*1024.0*1024.0*1024.0*1024.0, SMUL, 0.0, 0, 0, 0 },
{"pib ",     1024.0*1024.0*1024.0*1024.0*1024.0, SMUL, 0.0, 0, 0, 0 },
/* commented out cause it matches weird stuff
 * {"exa",          1.0e18 ,SMUL,    0.0,     0, 0, 6 },
 */
/* wtf why does this not work? KNG 20070111
 * {"exabyte",      1.0e18 ,SMUL,    0.0,     0, 0, 6 },
 */
/* exabyte; may erroneously match other stuff?:
 * {"eb",          1.0e18 ,SMUL,    0.0,     0, 0, 6 },
 * exbibyte
 */
{"zetta",        1.0e21 ,SMUL,    0.0,     0, 0, 0 },
{"zettabyte",    1.0e21 ,SMUL,    0.0,     0, 0, 7 },
{"zb",           1.0e21 ,SMUL,    0.0,     0, 0, 7 },
/* see `mebibyte'/`mib' reasoning */
{"zebibyte", 1024.0*1024.0*1024.0*1024.0*1024.0*1024.0*1024.0, SMUL, 0.0,0,0, 0},
{"zib ",     1024.0*1024.0*1024.0*1024.0*1024.0*1024.0*1024.0, SMUL, 0.0,0,0, 0},
{"yotta",        1.0e24 ,SMUL,    0.0,     0, 0, 0 },
{"yottabyte",    1.0e24 ,SMUL,    0.0,     0, 0, 8 },
{"yb",           1.0e24 ,SMUL,    0.0,     0, 0, 8 },
/* see `mebibyte'/`mib' reasoning */
{"yobibyte", 1024.0*1024.0*1024.0*1024.0*1024.0*1024.0*1024.0*1024.0, SMUL, 0.0, 0, 0, 0},
{"yib ",     1024.0*1024.0*1024.0*1024.0*1024.0*1024.0*1024.0*1024.0, SMUL, 0.0, 0, 0, 0},
{"zillion",      1.0e32 ,SMUL,    0.0,     0, 0, 0 },
{"ittybitty",    1.0e-32,SMUL,    0.0,     0, 0, 0 },
{"thirty secondth",  32.0,SQF,    0.0,     0, 0, 0 },
{"sixty fourth",     64.0,SQF,    0.0,     0, 0, 0 },
                                                      /* PBR 10-28-93 */
{"", 0.0, UNK, 0.0, 0, 0, 0 }
};

static TXbool   TXnpmBytePowersBinary = TXbool_False;
static TXbool   TXnpmInitBytePowers = TXbool_True;

TXbool
TXnpmSetBytePowersBinary(TXbool binary)
{
  binary = !!binary;                            /* standardize value */
  if (binary != TXnpmBytePowersBinary)          /* value is changing */
    {
      TXnpmBytePowersBinary = binary;
      TXnpmInitBytePowers = TXbool_True;        /* need to init at search */
    }
  return(TXbool_True);
}

TXbool
TXnpmGetBytePowersBinary(void)
{
  return(TXnpmBytePowersBinary);
}

/************************************************************************/

STATIC char **_nptlst=(char **)NULL;
STATIC char *_nptbuf=(char *)NULL;
STATIC int _nptuse=0;
STATIC byte _nct[DYNABYTE];


char **
mknptlst()
{
 char *bp,*tp;
 char **lp;
 int i,n,sz;

 if(_nptlst!=(char **)NULL)
   {
    _nptuse++;
    return(_nptlst);
   }

 for(sz=n=i=0;tfa[i].type!=UNK;i++)
    if(tfa[i].start)
	 {
	  ++n;
	  sz+=strlen(tfa[i].s)+1;
	 }
 _nptlst=lp=(char **)calloc(n+2,sizeof(char *));
 if(lp==(char **)NULL) return(lp);
 _nptbuf=bp=(char *)malloc(sz+2);
 if(bp==(char *)NULL) {free(lp); return((char **)NULL);}
 for(n=i=0;tfa[i].type!=UNK;i++)
    if(tfa[i].start)
	 {
	  lp[n]=bp;				     /* copy the string */
	  for(tp=tfa[i].s;*tp!='\0';tp++,bp++)
	      *bp= *tp;
	  *bp='\0';
	  ++bp;
	  ++n;
	 }
 *bp='\0';
 lp[n]=bp;				 /* the null of the prev string */
 _nptuse++;
 return(lp);
}

/************************************************************************/

void
rmnptlst()   /* rm np tok list - free's memory allocated in the above */
{
 if(_nptuse>0) _nptuse--;
 if(_nptuse>0) return;
 if(_nptlst!=(char **)NULL) free(_nptlst);
 if(_nptbuf!=(char *)NULL)  free(_nptbuf);
 _nptlst=(char **)NULL;
 _nptbuf=(char *)NULL;
}

/************************************************************************/

int
CDECL
ttfcmp(vx,vy)
CONST void *vx,*vy;
{
 TTF *x=(TTF *)vx,*y=(TTF *)vy;
 byte *a,*b;
 a=(byte *)x->s;
 b=(byte *)y->s;
 for(;*a && *b && _nct[*a]==_nct[*b];a++,b++);
 return((int)_nct[*a]-_nct[*b]);
}

/************************************************************************/

TTF *
ntlst(s)
byte *s;	      /* MAW 05-21-92 - byte not char for nct[] index */
{
 static int n= -1;
 int i,j,k,maxi,max;
 byte *a,*b,c;

/* init the thing ****/

// fix for test and allow usage in rampart.utils - ajf 2024-07-13
#if TEST | RP_USING_DUKTAPE
 if(n== -1)                                   /* init not done */
    {
     for(n=0;n<DYNABYTE;n++)			  /* init the cmp table */
	 {
           if(isspace((byte)n)) _nct[n]=(byte)' ';
	  else
            if(isupper((byte)n)) _nct[n]=(byte)tolower((byte)n);
	  else _nct[n]=(byte)n;
	 }
     for(n=0;tfa[n].type!=UNK;n++);
     qsort((char *)tfa,n,sizeof(TTF),ttfcmp);
    }

#else
 static int locale_serial = -1;

 if(n== -1 ||                                   /* init not done */
    locale_serial < TXgetlocaleserial())        /* locale changed on us */
    {
     for(n=0;n<DYNABYTE;n++)			  /* init the cmp table */
	 {
           if(isspace((byte)n)) _nct[n]=(byte)' ';
	  else
            if(isupper((byte)n)) _nct[n]=(byte)tolower((byte)n);
	  else _nct[n]=(byte)n;
	 }
     for(n=0;tfa[n].type!=UNK;n++);
     qsort((char *)tfa,n,sizeof(TTF),ttfcmp);
     locale_serial = TXgetlocaleserial();
    }

#endif

  if (TXnpmInitBytePowers)
    {
      TTF       *item;
      double    base = (TXnpmBytePowersBinary ? 1024.0 : 1000.0);
      int       itemIdx, power;

      for (itemIdx = 0; itemIdx < n; itemIdx++)
        {
          item = &tfa[itemIdx];
          if (item->decimalBinaryPower)
            {
              item->x = 1.0;
              for (power = 0; power < item->decimalBinaryPower; power++)
                item->x *= base;
            }
        }
      TXnpmInitBytePowers = TXbool_False;
    }

/** end of init *****/


/* would be another function, but wanted speed */

c=_nct[*s];
for(j=0,k=n-1,i=k>>1;j<=k;i=(j+k)>>1)
   {
    int cmp= c-_nct[*(byte *)tfa[i].s];
    if(cmp==0)
	 {
	  for(--i;i>=0 && c==_nct[*(byte *)tfa[i].s];i--);
	  ++i;
	  goto SEARCH;
	 }
    if(cmp<0) k=i-1;
    else      j=i+1;
   }

return(&tfa[n]);			  /* bsrch failed return unkown */

SEARCH:
 for(max=maxi=0;_nct[*(byte *)tfa[i].s]==_nct[*s];i++)
    {
     for(a=(byte *)tfa[i].s,b=(byte *)s,j=0;
	 *a && *b && _nct[*a]==_nct[*b];
	 a++,b++,j++
	);
     if(*a=='\0' && j>max )
	 {
	  max=j;
	  maxi=i;
	 }
    }
 if(max!=0)  return(&tfa[maxi]);
 return(&tfa[n]);
}

/************************************************************************/

#define TPTABSZ 10
double						 /* rets a power of ten */
tenpow(y)
double y;
{
 static float tptab[TPTABSZ];
 static int init=0;
 int neg=0;

 if(!init)
    {
     int i;
     init=1;
     for(i=1,tptab[0]=(float)1.0;i<TPTABSZ;i++)
	 tptab[i]=(float)10.0*tptab[i-1];
    }
 if(y<0.0)
    {
     neg=1;
     y= -y;
    }
 if((int)y<TPTABSZ)
    {
     if(neg) return(1.0/(double)tptab[(int)y]);
     else    return((double)tptab[(int)y]);
    }
 if(neg) return(pow(10.0,-y));
 return(pow(10.0,y));
}

/************************************************************************/
		    /* done this way to be portable */

#undef ctoi

int					    /* rets 0-9 || -1 if failed */
ctoi(c) 			    /* converts a char number to an int */
int c;
{
 char *diglst="0123456789abcdef";
 char *p;
 c = tolower((byte)c);
 for(p=diglst;*p!='\0' ;p++)
    if(*p==(char)c)
	 return(p-diglst);
 return(-1);
}

/************************************************************************/

/* this is a parser and a lex 1.23e9 or 1,000,000.00 type numbers if takes
a pointer to a string and a pointer to the float to be assigned. it
returns true if it locates a valid number and leaves *sp at the first
invalid char, or false if it couldn't match anything (and leaves *sp
alone) */






int
diglexy(sp,e, xp)    /* this bitch is supposed to parse non-spelled qtys */
char **sp;                                      /* (in/out) buf to parse */
char    *e;                                     /* (in) end of `*sp' */
double *xp;
{
 double *dp;					      /* double pointer */
 double val=0.0;					   /* the value */
 double base=10.0;
 int	valsign=1;				   /* sign of the value */
 double expo=0.0;					/* the exponent */
 int	exsign=1;				/* sign of the exponent */
 int	dpcnt=0;	     /* count of places after the decimal point */
 TXbool vdigfound = TXbool_False;	/* value  digits found flag */
 TXbool edigfound = TXbool_False;	      /* exponent digit located */
 TXbool dpfound = TXbool_False;		   /* decimal point found flag */
 TXbool exfound = TXbool_False;		 /* exponent found flag */
 TXbool sigfound = TXbool_False;		   /* sign located flag */
 char pc;						   /* prev char */
 char *s;						  /* the string */

 dp= &val;
 for (s = *sp, pc = (char)' '; s < e; pc = *s, s++)
    {
     if(*s==' ')
	 {
           byte nc = (s + 1 < e ? s[1] : '\0');
	  if(isdigit((byte)pc))
	      {
                if (s + 1 < e && isdigit((byte)nc)) break;
	      }
	  else
	  switch(pc)
	      {
	       case '.' :
	       case '+' :
	       case '-' :
	       case 'E' :
	       case 'e' : break;
	       default	: goto EOXY;
	      }
	 }
     else
     if(*s=='.')
	 {					 /* valid decimal point */
					      /* invalid decimal point */
	  if(exfound || dpfound) goto EOXY;   /* no doubles or . in exp */
	    /* rule below rmed because of .25 like qtys */
	  if(/*!isdigit(pc) || */ s + 1 >= e || !isdigit((byte)(*(s+1))))
            goto EOXY;
	  dpfound = TXbool_True;
	 }
     else
     if(*s=='-' || *s=='+' )
	 {
	  if(!sigfound) 	  /* this gets turned off if E is found */
	      {
	       sigfound = TXbool_True;
	       if(*s=='-')
		   {
		    if(exfound)   exsign= -1;
		    else	  valsign= -1;
		   }
	      }
	  else goto EOXY;
	 }
     else
     if(isdigit(*(byte *)s) ||
	(base == 16.0 && ((*s >= 'a' && *s <= 'f') || (*s>='A' && *s<='F'))))
	 {
	   if(dp== &expo)  { edigfound = TXbool_True; base=10.0; }
	  else vdigfound = TXbool_True;
	  sigfound = TXbool_True; /* it's too late now to put a sign in  */
	  *dp*=base;
	  *dp+=(double)ctoi(*(byte *)s);
	  if(dpfound)  ++dpcnt; 		   /* bump over decimal */
	  if (s == *sp && *s == '0' && s + 1 < e)
	    switch (s[1])
	      {
	      case '0': base = 8.0; s++; break;
	      case 'x':
	      case 'X': base = 16.0; s++; break;
	      }
	  if (s + 1 < e && s[1] == ',' && s + 2 < e &&
              isdigit(*((byte *)s + 2))) ++s;      /* remove commas */
	 }
     else
       if (s < e && (*s == 'e' || *s == 'E'))
	 {
	  if(!vdigfound) goto EOXY;
	  if(exfound)	 goto EOXY;		    /* invalid exponent */
	  dpfound = TXbool_False;
	  exfound = TXbool_True;
	  sigfound = TXbool_False;
	  dp= &expo;
	 }
     else break;
    }
 EOXY:
 if(!vdigfound) return(0);
 val*=(double)valsign;
 val*=tenpow((double)-dpcnt);
 if(edigfound)
    {
     if(expo>128.0) expo=128.0;
     expo*=(double)exsign;
     val*=tenpow(expo);
    }
 *sp=s;
 *xp=val;
 return(1);
}


/************************************************************************/

#ifdef __BORLANDC__
   /* MAW 05-12-92 - screws up "++i; tl[i].len=0;" */
   #pragma option -O1
#endif

 /* turns a char string into a string of valid tokens terminated w/
 UNK . it returns the number of matched tokens */

int
npmlex(s, e, tl, n)
byte *s;	      /* MAW 05-21-92 - byte not char for nct[] index */
byte    *e;                                     /* end of text buffer */
TTF *tl;
int n;
{
 int i;
 byte *t;
 int spaces=0;

 for (i = 0, tl[i].len = 0; i < n - 1 && s < e; )
    {
     t=s;
     tl[i].s=(char *)s;
     if(*s=='-' && i && tl[i-1].type==QTY) /* PBR 06-05-92 hyphens between numbers */
	 {
	  tl[i].type=HYPH;
	  tl[i].len=1;
	  ++s;
	  ++i;
	 }
     else
     if(isspace(*s))
	 {
	  byte *t;
	  tl[i].type=SPAC;
	  for (t = s + 1; t < e && isspace(*t); t++);
	  tl[i].len=t-s;
	  s=t;
	  ++i;
	 }
     else
       if (diglexy((char **)&t, (char *)e, &tl[i].x))
	 {
	  tl[i].len=t-s+spaces;
	  s=t;
	  tl[i].type=QTY;
	  tl[i].y=0.0;
	  ++i;
	  tl[i].len=0;
	  spaces=0;
	 }
     else
	 {
	  tl[i]= *ntlst(s);
	  tl[i].len+=spaces;
	  spaces=0;
	  if(tl[i].type==UNK)
	      {
	       tl[i].len=0;
	       return(i);
	      }
	  else
	      {
	       int l=strlen(tl[i].s);
	       s+=l;
	       tl[i].len+=l;
	      }
	  if(_nct[*s]=='s' && (tl[i].type==SQF || tl[i].type==SMUL))
	      {
	       tl[i].len+=1;
	       ++s;
	      }
	  ++i;
	  tl[i].len=0;
	 }
    }
 tl[i].type=UNK;
 return(i);
}

#ifdef __BORLANDC__
   #pragma option -O2
#endif

/************************************************************************/

double
nxtmul(nl,n)		   /* finds the next multiplier in a token list */
TTF *nl;
int n;
{
 for(n++;nl[n].type!=UNK;n++)
    {
     if(nl[n].type==SMUL || nl[n].type==SQF)
	 return(nl[n].x);
    }
 return(-1.0e32);
}


/************************************************************************/


int
npmy(nl,n,px,py,op)					 /* the parser */
TTF *nl;
int n;
double *px;				/* i'll shove the value in here */
double *py;				/* i'll shove the value in here */
char   *op;
{
 TTF  *lastvtok= &ziptf;
 int i,j;
 TXbool qtyfound = TXbool_False;
 TXbool yqtyfound = TXbool_False;
 TXbool signfound = TXbool_False;
 double sign=1.0;
 double numerator = 0.0, prevmul,sum,accum;
 double ynumerator = 0.0, yprevmul,ysum,yaccum;	      /* copy for range */
 TXbool  frop = TXbool_False;						 /* fraction op */

 *op='=';
 *px=prevmul=sum=accum=0.0;
 *py=yprevmul=ysum=yaccum=0.0;


 for(i=0;i<n && nl[i].type!=UNK;i++)
    {
     switch(nl[i].type)
	 {
	  case NOOP : break;
	  case HYPH : if(i!=0) { nl[i].type=NOOP; } /* ignore middle hyphens */
	  case SPAC :
	  case ANDA :
		   if(i+3<n && nl[i+2].type==FROP)
			{
			 double x,y;char top;
			 npmy(&nl[i+1],3,&x,&y,&top);
			 i+=3;
			 sum+=x;
			 break;
			}
		   else break;
	  case SQF  :
		   {
		    nl[i].x=1.0/nl[i].x;
		    if(i && nl[i-1].type==ANDA)
			{
			 nl[i].type=QTY;
			 sum+=nl[i].x;
			 break;
			}
		    /* nl[i].type=SMUL; maybe */
		    goto MULTIPLIER;
		   }

	  case GEOP : if(i!=0) goto EOPAR; *op='g'; break;
	  case GTOP : if(i!=0) goto EOPAR; *op='>'; break;
	  case LTOP : if(i!=0) goto EOPAR; *op='<'; break;
	  case LEOP : if(i!=0) goto EOPAR; *op='l'; break;
	  case SIOP :
		   {
		    if(signfound || i!=0) goto EOPAR;
		    signfound = TXbool_True;
		    sign=nl[i].x;
		   } break;
	  case FROP :
		   {
		    if(frop) goto EOPAR;
		    frop = TXbool_True;
		    numerator= *px+sum+accum;
		    *px=sum=accum=0.0;
		    if(yqtyfound)
			{
			 ynumerator= *py+ysum+yaccum;
			 *py=ysum=yaccum=0.0;
			}
		   } break;
	  case MAG  :	     /* almost a dupe of SMUL and ch x's to y's */
		   {
		    /* multiplier and without any prefix qty (errorish) */
		    if(ysum==0.0 && yaccum==0.0)
			{
			 *py=nl[i].y;
			 yqtyfound = TXbool_True;
			 *op=','; /* op is a rng */
			}
		    if( yprevmul>nl[i].y )
			{
			 if(nxtmul(nl,i)>nl[i].y )
			     {
			      *py+=yaccum;
			      yaccum=0.0;
			     }
			 yaccum+=nl[i].y*ysum;
			 ysum=0.0;
			}
		    else /* prev mul > Y */
			{
			 yaccum+=ysum;
			 ysum=0.0;
			 yaccum*=nl[i].y;
			}
		    yprevmul=nl[i].y;
		   }
		       /* NO BREAK ! */
	  case BUCK :
	  case SMUL :
		   {
MULTIPLIER:
		    /* multiplier and without any prefix qty (errorish) */
                    if (sum == 0.0 && accum == 0.0 && !qtyfound) /* Bug 7540*/
		       {
			sum=1.0;
			qtyfound = TXbool_True;
		       }
		    if(
		       i==n-1 &&
		       nl[i].x<1.0 &&
		       i &&
		       nl[i].type==SMUL &&
		       nl[i].type!=BUCK
		      )
		       {
			accum+=sum;
			yaccum+=ysum;
			ysum=sum=0.0;
			accum*=nl[i].x;
			yaccum*=nl[i].x;
		       }
		    else
		    if( prevmul>nl[i].x )
			{
			 if(nxtmul(nl,i)>nl[i].x)
			     {
			      *py+=yaccum;
			      yaccum=0.0;
			      *px+=accum;
			      accum=0.0;
			     }

			  if(sum!=0.0)
			     {
			      yaccum+=nl[i].x*ysum;
			      ysum=0.0;
			      accum+=nl[i].x*sum;
			      sum=0.0;
			     }
			 else
			     {
			      yaccum*=nl[i].x;
			      accum*=nl[i].x;
			     }
			}
		    else /* prev mul < X */
			{
			 yaccum+=ysum;
			 ysum=0.0;
			 yaccum*=nl[i].x;
			 accum+=sum;
			 sum=0.0;
			 accum*=nl[i].x;
			}
		    prevmul=nl[i].x;
		   }  break;
	  case	QTY :
		   {
		    if(lastvtok->type==QTY) goto EOPAR;
		    qtyfound = TXbool_True; sum+=nl[i].x;
		   } break;
	  case	RNG : *op=','; yqtyfound = TXbool_True; ysum+=nl[i].y;
	  case SQTY :
		   {
		    qtyfound = TXbool_True;
		    if(i && nl[i-1].type==ANDA )
			{  /* spelled fractions */
			 for(j=i+1;j<n && nl[j].type!=SQF;j++);
			 if(nl[j].type==SQF)
			     {
			      *px+=accum+sum;
			      accum=sum=0.0;
			     }
			}
		    sum+=nl[i].x;
		   } break;
	  default   : goto EOPAR;
	 }
     if(nl[i].type!=SPAC && nl[i].type!=NOOP && nl[i].type!=ANDA)
	 lastvtok= &nl[i];
    }
 EOPAR:
 if(!qtyfound) return(0);
 *px= *px+accum+sum;
 if(yqtyfound)
    {
     *py= *py+yaccum+ysum;
     if(frop) /* see below comment */
	{
	 if(*py==0.0)
	    *py=ynumerator;
	 else  *py=ynumerator / *py;
	}
    }
 if(frop)
    {
     if(*px==0.0)	 /* not valid fraction, return numerator as val */
	*px=numerator;
     else  *px=numerator / *px;
    }
 if(yqtyfound && *op==',' && *px>*py )        /* swap so they are lo hi */
    {
     double t;
     t= *px;
     *px= *py;
     *py=t;
    }
 *px *= sign;
 *py *= sign;
 return(i);
}

/************************************************************************/

char *
ttod(s, e, px, py, op)				      /* text to double */
char *s;                                        /* start of text buffer */
char *e;                                        /* end of text buffer */
double *px;							 /* min */
double *py;							 /* max */
char *op;			    /*	I'll shove a > < = or , in here */
{
 char *t;
 int i;
 TTF nl[NPMAXTOKS];
 int n = npmlex((byte *)s, (byte *)e, nl, NPMAXTOKS);
 int matched=npmy(nl,n,px,py,op);
 for(i=0,t=s;i<matched;i++) t+=nl[i].len;
 if (t > e) t = e;                              /* sanity check */
 return(t);
}

#ifndef _WINDLL
/************************************************************************/
void
npmtypedump(fh)
FILE *fh;
{
 int i;
 fprintf(fh,"%-20s %-10s %-10s first-token?\n","token","val","range");
 for(i=0;tfa[i].type!=UNK;i++)
    {
 fprintf(fh,"%-20s %-10lg %-10lg %-s\n"
     ,tfa[i].s,tfa[i].type==SQF ? 1.0/tfa[i].x:tfa[i].x,tfa[i].y,tfa[i].start ? "yes":"no");
    }
}

/************************************************************************/
#endif							     /* _WINDLL */

#if TEST


char *prompt="Enter numbers in the desired form or ? for types dump.";

int
main(int argc, char *argv[])
{
 double x,y;
 char s[256];
 char *t;
 char op;

 FILE *fh=(FILE *)NULL;
 if(argc==2)
    {
     fh=fopen(argv[1],"r");
    }
 else fh=stdin;
 if(fh==stdin)
    {
     puts("Numeric Pattern Matcher Parser");
     puts(prompt);
    }
 if(fh==(FILE *)NULL) exit(1);
 while(fgets(s,256,fh))
    {
     if(*s=='?')
	{
	 npmtypedump(stdout);
	 puts(prompt);
	}
     else
	 {
           t = ttod(s, s + strlen(s), &x, &y, &op);
	  fputs(s,stdout);
	  for(t--;t>=s;t--) putchar(' ');
	  puts("^");
	  printf("x=%15.15lg y=%15.15lg op=\"%c\"\n",x,y,op);
	 }
    }
 if(fh!=stdin)
    {
     fclose(fh);
    }
 exit(0);
}
#endif /* TEST */

/************************************************************************/
