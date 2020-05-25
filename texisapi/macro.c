#ifdef mvs                      /* MAW 03-28-90 - must be first thing */
#  include "mvsfix.h"
#endif
/**********************************************************************/
/*#define SHOWBUF*/          /* display buffer contents on bad free() */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef EPI_HAVE_STDARG
#  include <stdarg.h>
#else
#  include <varargs.h>
#endif
#include <time.h>
#ifdef unix
#  include <sys/types.h>                                /* for size_t */
#  include <sys/stat.h>
#endif
#ifdef _WIN32
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <io.h>
#  include <direct.h>                                 /* for getcwd() */
#  include <process.h>                          /* for getpid() */
#endif
#ifdef macintosh
#  include <unix.h>
#endif
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "sizes.h"
#include "os.h"

#undef fullpath
#ifdef MSDOS                                          /* MAW 02-03-93 */
#  if defined(M_I86LM) && !defined(_MSC_VER)                 /* msc 5 */
#     define realfullpath(a,b,c) fullpath(a,b,c)
#  else
#     define realfullpath(a,b,c) _fullpath(a,b,c)
#  endif
#else
#  define realfullpath(a,b,c) fullpath(a,b,c)
#endif

#ifdef MEMDEBUG                                       /* MAW 04-19-90 */

#if 0
#  if defined(_WIN32) && !defined(MACMONO)
#     define MACMONO
#     include "windows.h"
#     ifndef vsprintf
#        define vsprintf WVSPRINTF
#     endif
#     define STATIC
#  else
#     define STATIC static
#  endif
#  ifdef MEMVISUAL
#     ifndef MACMONO
#        define MACMONO
#     endif
#     include "dos.h"
#  endif
#  ifdef MACMONO
#     include "mono.h"
#  endif
#else
#  define STATIC static
#endif

/************************************************************************/
#undef epi_malloc
#undef epi_calloc
#undef epi_realloc
#undef epi_remalloc
#undef epi_recalloc
#undef epi_free
#undef MALLOC
#undef CALLOC
#undef REALLOC
#undef REMALLOC
#undef RECALLOC
#undef FREE
#undef malloc
#undef calloc
#undef realloc
#undef remalloc
#undef recalloc
#undef free

/*#define malloc(a)       malloc(a)*/
/*#define calloc(a,b)     calloc(a,b)*/
/*#define realloc(a,b)    realloc(a,b)*/
#define remalloc(a,b)   realloc(a,b)
#define recalloc(a,b,c) realloc(a,b*c)
/*#define free(a)         free(a)*/

extern void *epi_malloc   ARGS((uint));
extern void *epi_calloc   ARGS((uint,uint));
extern void *epi_remalloc ARGS((void FAR *,uint));
extern void *epi_recalloc ARGS((void FAR *,uint,uint));
extern void *epi_free     ARGS((void FAR *));

#undef exit
#undef strdup
#undef getcwd
#undef fullpath
#undef time
#undef realpath

#ifdef unix
#ifndef sgi
   extern char *getcwd ARGS((char *,size_t));
#endif
#endif

                       /* mem test routine stuff */
#ifndef MAXMACMAS
#  ifdef SMALL_MEM
#    define MAXMACMAS  2000
#  else
#    define MAXMACMAS  100000   /* see help if changed */
#  endif
#endif /* !MAXMACMAS */

#define MAXSIZE 128
#define MINSIZE 8
/*#define ENDSIZE(x) ( (x)>MAXSIZE?MAXSIZE: ((x)<MINSIZE?MINSIZE:(x)) )*/
/* the variable length ENDSIZE stuff is broken in mac_remalloc() */
/*#define ENDSIZE(x) (8)*/               /* handle largest case alignment */
static int      EndSize = 8;            /* KNG 990810 settable size */
static int      EndStamp = 0;           /* if set, add name:line to endfill */
#define ENDSIZE(x)      EndSize

static byte     AllocFill = 0x01;
static byte     FreeFill = 0xfe;
static byte     End1Fill = 0xfc;
static byte     End2Fill = 0xfd;

typedef struct macma_tag
{
 byte *mem;                                           /* ptr to alloc */
 uint  n;                                                 /* how much */
 uint  size;                                               /* how big */
 CONST char *fn;                                /* fname alloced in */
 int   ln;                                             /* what line # */
 long  sum;                                      /* optional checksum */
 char  *memo;                                   /* optional memo (alloced) */
 byte  used;                                      /* free struct flag */
}
MACMA;
#define MACMAPN ((MACMA *)NULL)

typedef union
{
  size_t        sz;
  EPI_HUGEINT   h;
  double        d;
  char          bytes[TX_ALIGN_BYTES];
}
ALIGNBUF;
#define ALIGNSZ (sizeof(ALIGNBUF))
#define OVERHEAD        0                       /* ALIGNSZ if inc. overhead */

static size_t   TXmemCurrentAllocedBytes = 0;   /* including OVERHEAD */
static size_t   TXmemCurrentAllocedItems = 0;
static size_t   TXmemMaxAllocedBytes = 0;

STATIC MACMA macmalst_static[MAXMACMAS];
STATIC MACMA    *macmalst = macmalst_static;
STATIC size_t   MaxMacMas = MAXMACMAS;
#ifdef unix
   extern int end;
#endif
#define FMT_LONG        (1 << 0)
#define FMT_PTR         (1 << 1)
#define FMT_I           (1 << 2)
#define FMT_NSZ         (1 << 3)
static CONST char * CONST       MacmaFmt[] =
{
  "%s:%d: %s%.0s%.0s%s%s\n",                                /*      */
  "File %s line %3d %s%.0s%.0s%s%s\n",                      /*    L */
  "%s:%d: %s%.0s%.0s%s%s (ptr=0x%" EPI_VOIDPTR_HEX_FMT ")\n",   /*   P  */
  "File %s line %3d %s%.0s%.0s%s%s (ptr=0x%" EPI_VOIDPTR_HEX_FMT ")\n", /*   PL */
  "%s:%d: %s%.0s (i=%s)%s%s\n",                             /*  I   */
  "File %s line %3d %s%.0s (i=%s)%s%s\n",                   /*  I L */
  "%s:%d: %s%.0s (i=%s)%s%s (ptr=0x%" EPI_VOIDPTR_HEX_FMT ")\n",/*  IP  */
  "File %s line %3d %s%.0s (i=%s)%s%s (ptr=0x%" EPI_VOIDPTR_HEX_FMT ")\n", /*  IPL */
  "%s:%d: %s%s%.0s%s%s\n",                                  /* N    */
  "File %s line %3d %s%s%.0s%s%s\n",                        /* N  L */
  "%s:%d: %s%s%.0s%s%s (ptr=0x%" EPI_VOIDPTR_HEX_FMT ")\n",     /* N P  */
  "File %s line %3d %s%s%.0s%s%s (ptr=0x%" EPI_VOIDPTR_HEX_FMT ")\n", /* N PL */
  "%s:%d: %s%s (i=%s)%s%s\n",                               /* NI   */
  "File %s line %3d %s%s (i=%s)%s%s\n",                     /* NI L */
  "%s:%d: %s%s (i=%s)%s%s (ptr=0x%" EPI_VOIDPTR_HEX_FMT ")\n",  /* NIP  */
  "File %s line %3d %s%s (i=%s)%s%s (ptr=0x%" EPI_VOIDPTR_HEX_FMT ")\n",/* NIPL */
};
STATIC CONST char  *macmafmt = CHARPN;
static char     Ibuf[sizeof(long)*8/3 + 3 + 64];
#define I2S(i)  (sprintf(Ibuf, "%ld", (long)(i)), Ibuf)
static char     NSZbuf[2*sizeof(long)*8/3+12];
#define NSZ2S(n,sz)     \
  (sprintf(NSZbuf, " (n=%ld,sz=%ld)", (long)(n), (long)(sz)), NSZbuf)
STATIC size_t   lasti = 0;
STATIC size_t   fsum = (size_t)MAXMACMAS, lsum = (size_t)(-1);
STATIC int      MacCheckSum = 1;
STATIC int      MacCheckBounds = 1;
static int      MacCountsOnly = 0;
int mac_ton=1;                       /* MAW 07-05-90 - "test on" flag */
int mac_watch=0;  /* watch what macma routines are doing, set mac_ndx */
size_t mac_ndx = (size_t)(-1);  /* index to watch, output each time touched */
int mac_quiet=0;
#define PMACMA(s, ptr)	mac_print(macmafmt,fn,ln,s,NSZ2S(n,size),I2S(i), (memo ? " " : ""), (memo ? memo : ""), (EPI_VOIDPTR_UINT)(ptr))

/* Stuff to monitor possible re-entrancy of functions.  WTF thread-unsafe: */
CONST char      *mac_curfile = CHARPN;  /* current calling file */
int             mac_curline = 0;        /* current calling line */
CONST char      *mac_prevfile = CHARPN; /* previous current calling file */
int             mac_prevline = 0;       /* previous current calling line */
static CONST char       ReenterMsg[] =
"%s:%d: re-entering mem function still active at %s:%d\n";
#define MAC_ENTERFUNC(fn, ln)                   \
  mac_prevfile = mac_curfile;                   \
  mac_prevline = mac_curline;                   \
  mac_curfile = fn;                             \
  mac_curline = ln;                             \
  if (!MacOff && mac_prevfile != CHARPN)        \
    mac_print(ReenterMsg, mac_curfile, mac_curline, mac_prevfile, mac_prevline)
#define MAC_EXITFUNC()                          \
  mac_curfile = mac_prevfile;                   \
  mac_curline = mac_prevline;                   \
  mac_prevfile = CHARPN;                        \
  mac_prevline = 0

static void dumpmacma    ARGS((size_t i, void *mem, CONST char *fn, int ln));

static int      GotMemDebug = 0;
static int      MacSummary = 1, MacLeak = 1, MacOff = 0, MacNoFree = 0;
static int      MacPrEnd = 0, MacPid = 0, MacFixBad = 1, MacTrace = 0;
static int      MacFill = 1;
static int      MacPrintLeakData = 0, MacPrIncMaxMacmas = 0;
/* WTF global for monitor.c:tx_savefd(), because we cannot -ltexis here? */
FILE            *TXmacFh = FILEPN;
#define MACFH   (TXmacFh != FILEPN ? TXmacFh : stderr)

static int getmemdebug_func ARGS((void));
static int
getmemdebug_func()
{
  char  *s, *t, *e;
  int   fmt = (FMT_I | FMT_NSZ), n;
  char  tmp[1024];

  GotMemDebug = 1;
  macmafmt=MacmaFmt[0];
  if ((s = getenv("SCRIPT_NAME")) != CHARPN) {MacOff = 1;MacTrace=0;} /* CGI default off */
  if ((s = getenv("MEMDEBUG")) == CHARPN) return(1);
  if (strstr(s, "trace") != CHARPN) { MacTrace = 1; goto setoff; }
  if (strstr(s, "off") != CHARPN)
    {
      MacTrace = 0;
    setoff:
      mac_quiet = 1;
      MacSummary = MacLeak = MacPrEnd = MacPrintLeakData = mac_watch = 0;
      mac_ndx = (size_t)(-1);
      MacNoFree = MacPid = 0;
      MacOff = 1;
    }
  if (strstr(s, "quiet") != CHARPN) mac_quiet = 0;
  if (strstr(s, "pid") != CHARPN) MacPid = 1;
  if (strstr(s, "nosummary") != CHARPN) MacSummary = 0;
  if (strstr(s, "noleak") != CHARPN) MacLeak = 0;
  if (strstr(s, "checksum") != CHARPN) MacCheckSum = 1;
  if (strstr(s, "nochecksum") != CHARPN) MacCheckSum = 0;
  if (strstr(s, "checkbounds") != CHARPN) MacCheckBounds = 1;
  if (strstr(s, "nocheckbounds") != CHARPN) MacCheckBounds = 0;
  if (strstr(s, "fill") != CHARPN) MacFill = 1;
  if (strstr(s, "nofill") != CHARPN) MacFill = 0;
  if (strstr(s, "countsonly") != CHARPN) MacCountsOnly = 1;
  if (strstr(s, "leaksonly") != CHARPN)
    {
      MacCheckBounds = MacCheckSum = MacSummary = MacFill = 0;
    }
  if (strstr(s, "printend") != CHARPN) MacPrEnd = 1;
  if ((t = strstr(s, "printleakdata")) != CHARPN)
    {
      t += 13;
      if (*t == '=' && (n = (int)strtol(t + 1, &e, 0)) > 0 && e > t + 1)
        MacPrintLeakData = n;
      else
        MacPrintLeakData = 16;                  /* arbitrary default */
    }
  if (strstr(s, "ptr") != CHARPN) fmt |= FMT_PTR;
  if (strstr(s, "nofix") != CHARPN) MacFixBad = 0;
  if (strstr(s, "watch") != CHARPN) mac_watch = 1;
  if ((t = strstr(s, "allocfill")) != CHARPN)
    {
      t += 9;
      if (*t == '=') t++;
      AllocFill = (byte)atoi(t);
    }
  if ((t = strstr(s, "freefill")) != CHARPN)
    {
      t += 8;
      if (*t == '=') t++;
      FreeFill = (byte)atoi(t);
    }
  if ((t = strstr(s, "end1fill")) != CHARPN)
    {
      t += 8;
      if (*t == '=') t++;
      End1Fill = (byte)atoi(t);
    }
  if ((t = strstr(s, "end2fill")) != CHARPN)
    {
      t += 8;
      if (*t == '=') t++;
      End2Fill = (byte)atoi(t);
    }
  if ((t = strstr(s, "maxmacmas")) != CHARPN)
    {
      t += 9;
      if (*t == '=') t++;
      n = atoi(t);
      if (n <= 0)
        {
          write(STDERR_FILENO, "Bad maxmacmas value\n", 20);
          _exit(1);
        }
      MaxMacMas = (size_t)n;
      fsum = (size_t)n;
      if ((macmalst = (MACMA *)calloc(MaxMacMas, sizeof(MACMA))) ==
          (MACMA *)NULL)
        {
          write(2, "Cannot alloc mem for maxmacmas\n", 31);
          _exit(2);
        }
    }
  if ((t = strstr(s, "index")) != CHARPN)
    {
      t += 5;
      if (*t == '=') t++;
      mac_ndx = atoi(t);
    }
  if ((t = strstr(s, "endsize")) != CHARPN)
    {
      t += 7;
      if (*t == '=') t++;
      EndSize = atoi(t);
      if (EndSize < 0) EndSize = 0;
    }
  if (strstr(s, "endstamp") != CHARPN)          /* WTF broken in mac_cend() */
    {
      EndStamp = 1;
      if (EndSize < 32) EndSize = 32;
    }
  if (strstr(s, "nofree") != CHARPN) MacNoFree = 1;
  if (strstr(s, "longloc") != CHARPN) fmt |= FMT_LONG;
  if (strstr(s, "noi") != CHARPN) fmt &= ~FMT_I;
  if (strstr(s, "nonsz") != CHARPN) fmt &= ~FMT_NSZ;
  if ((t = strstr(s, "logfile")) != CHARPN)
    {
      t += 7;
      if (*(t++) == '=')
        {
          n = strcspn(t, ";,");                 /* get length of filename */
          if (n < sizeof(tmp))                  /* not too big */
            {
              memcpy(tmp, t, n);
              tmp[n] = '\0';
              if (TXmacFh != FILEPN && TXmacFh != stderr) fclose(TXmacFh);
              TXmacFh = fopen(tmp, "a");
            }
        }
    }

  if (strstr(s, "help") != CHARPN)
    {
      static char *help[] =
      {
     "MEMDEBUG environment variable can contain 1 or more of:\n",
     "off             Turn completely off, don't track or modify mem\n",
     "leaksonly       Only trace mem leaks (faster)\n",
     "countsonly      Just track current/max mem usage (faster, no macmas limit)\n"
     "trace           Just print allocs/frees, don't track or modify mem\n",
     "quiet           Don't print msgs\n",
     "nosummary       Don't print summary at exit\n",
     "noleak          Don't report on unfreed pointers (mem leaks) at exit\n",
     "[no]checksum    [Do not] checksum blocks to detect corruption\n",
   "[no]checkbounds [Do not] check for over/underflow corruption of blocks\n",
     "[no]fill        [Do not] fill buffers with allocfill/freefill/endfill\n",
     "printend        Print stomp data for under/overflows\n",
     "printleakdata=n Print n bytes of each mem leak block\n",
     "ptr             Print pointer values\n",
     "nofix           Do not fix under/overflows after reporting\n",
     "watch           Print info about every alloc/free\n",
     "index=N         Print info about pointer with index i=N\n",
     "endsize=N       Use end fill size N (default 8)\n",
     "nofree          Don't free any mem, so index=N pointer not re-used\n",
     "endstamp        Put file:line string at end of mem blocks (broken)\n",
     "longloc         Print long File/line style\n",
     "noi             Do not print i value in msgs\n",
     "nonsz           Do not print n, size values in msgs\n",
     "logfile=/file;  Print to /file instead of stderr\n",
     "allocfill=N     Byte to fill malloc() buffers with (0x01)\n",
     "freefill=N      Byte to fill free()'d buffers with (0xfe)\n",
 "end1fill=N      Byte to mark lower slack space of alloc'd buffers (0xfc)\n",
 "end2fill=N      Byte to mark upper slack space of alloc'd buffers (0xfd)\n",
     "maxmacmas=N     Alloc room for N pointers (default 100000)\n",
     "pid             Print PID with each message\n",
     "help            Print this help and exit\n",
      CHARPN
      };
      int       i;

      for (i = 0; help[i] != CHARPN; i++)
        write(2, help[i], strlen(help[i]));
      _exit(1);
    }
  macmafmt = MacmaFmt[fmt];
  return(1);
}

#define getmemdebug()   (GotMemDebug ? 1 : getmemdebug_func())

/* ------------------------------------------------------------------------ */

size_t
TXmemGetCurrentAllocedBytes()
/* Returns current total amount of alloced memory, in bytes.
 */
{
  return(TXmemCurrentAllocedBytes);
}

/************************************************************************/

void mac_print ARGS((CONST char *fmt, ...));

#ifdef EPI_HAVE_STDARG
void
mac_print(CONST char *fmt, ...)
{
va_list argp;
char *d;
char buf[512];

   getmemdebug();
   if(mac_quiet || MacOff) return;
   if (MacPid)
     {
       sprintf(buf, "(%u) ", (unsigned)getpid());
       d = buf + strlen(buf);
     }
   else
     d = buf;
   va_start(argp, fmt);
   vsprintf(d,fmt,argp);
#  if 0
      m2other();
      monoputs(buf);
      m2curr();
#  else
      fputs(buf,MACFH);
#  endif
   va_end(argp);
   fflush(MACFH);
}
#else /* EPI_HAVE_STDARG */
void
mac_print(va_alist)
va_dcl
{
va_list argp;
CONST char *fmt;
char    *d;
char buf[512];

   getmemdebug();
   if(mac_quiet || MacOff) return;
   if (MacPid)
     {
       sprintf(buf, "(%u) ", (unsigned)getpid());
       d = buf + strlen(buf);
     }
   else
     d = buf;
   va_start(argp);
   fmt=va_arg(argp,CONST char *);
   vsprintf(d,fmt,argp);
#  if 0
      m2other();
      monoputs(buf);
      m2curr();
#  else
      fputs(buf,MACFH);
#  endif
   va_end(argp);
   fflush(MACFH);
}
#if !defined(_INTELC32_) && !defined(__BORLANDC__) && !defined(unix)
void CDECL mac_print ARGS((char *,...));
#endif
#endif /* EPI_HAVE_STDARG */

static void mac_hexdump ARGS((CONST byte *buf, size_t sz));
static void
mac_hexdump(buf, sz)
CONST byte      *buf;
size_t          sz;
{
  static CONST char     hexch[] = "0123456789ABCDEF";
  size_t                n, off = (size_t)0, i;
#define BYTES_PER_LINE  16
#define CHAROFF         (6 + 3*BYTES_PER_LINE + 2)
#define flags           1
  char                  *hexd, *chard;
  char                  line[CHAROFF + BYTES_PER_LINE + 1];

  while (sz > (size_t)0)
    {
      n = (sz < BYTES_PER_LINE ? sz : BYTES_PER_LINE);
      sprintf(line, "%04X: ", (unsigned)off);
      sz -= n;
      off += n;
      for (hexd = line + 6, chard = line + CHAROFF, i = 0; i < n; i++, buf++)
        {
          *(hexd++) = hexch[(unsigned)(*buf) >> 4];
          *(hexd++) = hexch[(unsigned)(*buf) & 0xF];
          *(hexd++) = ((flags & 2) && i == BYTES_PER_LINE/2 - 1 ? '|' : ' ');
          *(chard++) = (*buf >= (byte)' ' && *buf <= (byte)'~' ? *buf : '.');
        }
      for ( ; hexd < line + CHAROFF; hexd++) *hexd = ' ';
      *chard = '\0';
      fprintf(MACFH, "%s\n", line + ((flags & 1) ? 0 : 6));
    }
  fflush(MACFH);
#undef flags
#undef BYTES_PER_LINE
#undef CHAROFF
}

/************************************************************************/

static void mac_zend ARGS((size_t i, CONST char *fn, int ln));

static void
mac_zend(i, fn, ln)
size_t i;
CONST char      *fn;
int     ln;
{
 uint n, size;
 int j, e, fsz;
 byte *p;
 char   *d;
 char   buf[64];

 getmemdebug();
 if (!MacFill) return;

 n=macmalst[i].n;
 size=ENDSIZE(macmalst[i].size);
 p=macmalst[i].mem;
 for(j=(-1),e=j-size;j>e;j--)
    {
     p[j]=End1Fill;
    }
 for(j=(n*macmalst[i].size),e=j+size;j<e;j++)
    {
     p[j]=End2Fill;
    }
 if (EndStamp)                                          /* KNG 990810 */
   {
     for (d = buf + sizeof(buf); d > buf; d--)          /* safe itoa() */
       {
         *(--d) = '0' + (ln % 10);
         ln /= 10;
         if (ln == 0) break;
       }
     if (d > buf) *(--d) = ':';
     fsz = strlen(fn);
     if (fsz > (d - buf)) fsz = d - buf;
     memcpy(d - fsz, fn, fsz);
     d -= fsz;
     fsz = (buf + sizeof(buf)) - d;
     if (fsz < (int)size)
       {
         j = -((int)size) + ((int)size - fsz)/2;
         memcpy(p + j, d, fsz);
         j = (n*macmalst[i].size) + ((int)size - fsz)/2;
         memcpy(p + j, d, fsz);
       }
     /* WTF now ignore this stuff in mac_cend() */
   }
}

/************************************************************************/

static int
mac_cend(size_t i)
{
 uint n, size;
 int j, e, ret = 0;
 byte *p;

 getmemdebug();
 if (!MacFill) goto done;

 n=macmalst[i].n;
 size=ENDSIZE(macmalst[i].size);
 p=macmalst[i].mem;
 for(j=(-1),e=j-size;j>e;j--)
    {
     if(p[j]!=End1Fill) ret |= 1;                         /* underflow */
    }
 for(j=(n*macmalst[i].size),e=j+size;j<e;j++)
    {
     if(p[j]!=End2Fill) ret |= 2;                        /* overflow */
    }
done:
 return(ret);
}

/************************************************************************/

void
mac_doovchk(fn,ln)
CONST char *fn;
int ln;
{
 int j, e, r;
 size_t i;
 uint n, size, sz;
 byte *p;
 MACMA  *macma;

 MAC_ENTERFUNC(fn, ln);
 getmemdebug();
 if (MacOff || !MacCheckBounds || !MacFill || MacCountsOnly) goto done;

 for(i=0;i<MaxMacMas;i++)
    {
      if(!macmalst[i].used) continue;
      r = mac_cend(i);
      if (r & 1)                                /* underflow */
	{
                   n=macmalst[i].n;
                   size=macmalst[i].size;
                   { CONST char *memo = macmalst[i].memo;
                     PMACMA("underflow", macmalst[i].mem);}
		   if (MacPrEnd)
		     {
		       sz = ENDSIZE(macmalst[i].size);
		       p = macmalst[i].mem;
		       for (j = -((int)sz); j < 0; j++)
			 if (p[j] == End1Fill)
			   fprintf(MACFH, " ..");
			 else
			   fprintf(MACFH, " %02X", (unsigned)p[j]);
		       fprintf(MACFH, "\n");
                       fflush(MACFH);
		     }
		   if (MacFixBad)
		     {
		       sz = ENDSIZE(macmalst[i].size);
		       p = macmalst[i].mem;
		       for (j = -((int)sz); j < 0; j++) p[j] = End1Fill;
		     }
	}
      if (r & 2)                               	/* overflow */
	{
                   n=macmalst[i].n;
                   size=macmalst[i].size;
                   { CONST char *memo = macmalst[i].memo;
                     PMACMA("overflow", macmalst[i].mem);}
		   if (MacPrEnd)
		     {
		       sz = ENDSIZE(macmalst[i].size);
		       p = macmalst[i].mem;
		       for (j = n*size, e = j + sz; j < e; j++)
			 if (p[j] == End2Fill)
			   fprintf(MACFH, " ..");
			 else
			   fprintf(MACFH, " %02X", (unsigned)p[j]);
		       fprintf(MACFH, "\n");
                       fflush(MACFH);
		     }
		   if (MacFixBad)
		     {
		       sz = ENDSIZE(macmalst[i].size);
		       p = macmalst[i].mem;
		       for (j = n*size, e = j + sz; j < e; j++)
			 p[j] = End2Fill;
		     }
	}
      macma = macmalst + i;
      if (r)
	mac_print(macmafmt, macma->fn,macma->ln,
		  "<- under/overflow alloced here",
                NSZ2S(macma->n,macma->size),I2S(i),
                  (macma->memo ? " " : ""), (macma->memo ? macma->memo : ""),
                  (EPI_VOIDPTR_UINT)macma->mem);
    }
done:
 MAC_EXITFUNC();
}

/************************************************************************/

void
mac_dosum(p,fn,ln)
void *p;
CONST char *fn;
int ln;
{
 int n=0, size=0;
 size_t i;
 long j;
 byte *mem=(byte *)p;

 getmemdebug();
 if (MacOff || !mac_ton || (mac_ndx == (size_t)(-1) && !MacCheckSum) || MacCountsOnly)
   goto done;

 for(i=0;i<MaxMacMas;i++)
    {
     if(mem==macmalst[i].mem)
         {
          if(i==mac_ndx)
            dumpmacma(i,"mac_sum",fn,ln);
          if (!MacCheckSum) goto done;
          if(macmalst[i].used)
              {
               macmalst[i].sum=0;
               for(j=(long)macmalst[i].n*(long)macmalst[i].size;j>0;j--,mem++)
                  {
                   macmalst[i].sum^= *mem;
                  }
               if(i<fsum) fsum=i;
               if (lsum == (size_t)(-1) || i > lsum) lsum = i;
		 mac_dovsum(fn,ln);
               goto done;
              }
         }
    }
 { CONST char *memo = CHARPN;
   PMACMA("invalid pointer", mem);}
done:
    return;
}

/**********************************************************************/

void
mac_dovsum(fn,ln)
CONST char *fn;
int ln;
{
 int rc=0;
 size_t i;
 long sum, j;
 byte *mem;

 getmemdebug();
 if (MacOff || !mac_ton || !MacCheckSum || MacCountsOnly) goto done;

 if (lsum != (size_t)(-1))
  for(i=fsum;i<=lsum;i++)
    {
     if(macmalst[i].used && macmalst[i].sum!=0)
        {
         sum=0;
         mem=(byte *)macmalst[i].mem;
         for(j=(long)macmalst[i].n*(long)macmalst[i].size;j>0;j--,mem++)
            {
             sum^= *mem;
            }
         if(sum!=macmalst[i].sum)
            {
             int n=macmalst[i].n, size=macmalst[i].size;
             CONST char *memo = macmalst[i].memo;
             PMACMA("memory corrupted", macmalst[i].mem);
             rc++;
            }
       }
    }

 if(rc>0) abort();

done:
 return;
}

/**********************************************************************/



static void
dumpmacma(i,s,fn,ln)
size_t i;
void *s;
CONST char *fn;
int ln;
{
  mac_print("%s[%ld] mem=0x%" EPI_VOIDPTR_HEX_FMT ",n=%u,size=%u,fn=%s,ln=%d,used=%d,sum=%ld at %s:%d\n",s,(long)i,
    (EPI_VOIDPTR_UINT)macmalst[i].mem,macmalst[i].n ,macmalst[i].size,
    macmalst[i].fn ,macmalst[i].ln,macmalst[i].used,
    macmalst[i].sum,
    fn,ln);
}

/************************************************************************/

static int addmacma ARGS((void *mem, uint n, uint size, CONST char *fn,
                          int ln, CONST char *memo));

static int
addmacma(mem, n, size, fn, ln, memo)
void *mem;
uint n,size;
CONST char *fn;
int ln;
CONST char      *memo;          /* (in, opt.) memo for msgs */
{
  static size_t j = (size_t)(-1);
  size_t        i;
 MACMA  *macma;

 if(!mac_ton) return(1);                              /* MAW 07-05-90 */
 for (i = (j + (size_t)1 < MaxMacMas ? j + (size_t)1 : 0);
      i != j;
      i = (i + (size_t)1 < MaxMacMas ? i + (size_t)1 : 0))
   {
     macma = macmalst + i;
    if(!macma->used)
         {
          if (macma->memo != CHARPN)
            {
              if (MacFill)
                memset(macma->memo, FreeFill, strlen(macma->memo) + 1);
              free(macma->memo);
              macma->memo = CHARPN;
            }
          if (memo != CHARPN) macma->memo = strdup(memo);
          macma->mem   = (byte *)mem;
          macma->n     = n;
          macma->size  = size;
          macma->fn    = fn;
          macma->ln    = ln;
          macma->used  = 1;
          macma->sum   = 0;
          mac_zend(i, fn, ln);
          TXmemCurrentAllocedBytes += (size_t)n*(size_t)size;
          if (TXmemCurrentAllocedBytes > TXmemMaxAllocedBytes)
            TXmemMaxAllocedBytes = TXmemCurrentAllocedBytes;
          TXmemCurrentAllocedItems++;
          lasti=j=i;
          if(mac_watch || i==mac_ndx)
            dumpmacma(mac_ndx=i,"addmacma",fn,ln);
          return(1);
         }
   }
 if (!MacPrIncMaxMacmas)
   {
     mac_print("Out of room for mallocs increase MAXMACMAS via MEMDEBUG in: %s\n",__FILE__);
     MacPrIncMaxMacmas = 1;
   }
 return(0);
}

/************************************************************************/

static int  delmacma     ARGS((void *mem, CONST char *fn, int ln, uint *n,
                               uint *sz));

static int
delmacma(mem,fn,ln,an,asz)
void *mem;
CONST char *fn;
int ln;
uint *an, *asz;
{
 int r, sz, j, e;
 size_t i, refree = (size_t)(-1);
 uint n, size;
 byte *p;
 MACMA  *macma;

 getmemdebug();

 if(!mac_ton) return(1);                              /* MAW 07-05-90 */
 if(mem==VOIDPN) return(0);
 for(i=0;i<MaxMacMas;i++)
    {
     if(mem==(void *)macmalst[i].mem)
         {
          if(mac_watch || i==mac_ndx)
            dumpmacma(i,"delmacma",fn,ln);
          if(!macmalst[i].used) refree=i;
          else
              {
               *an=n=macmalst[i].n;
               *asz=size=macmalst[i].size;
               r = mac_cend(i);
               if (r & 1)
                   {
                     CONST char *memo = macmalst[i].memo;

		     PMACMA("underflow", macmalst[i].mem);
		     if (MacPrEnd)
		       {
			 sz = ENDSIZE(macmalst[i].size);
			 p = macmalst[i].mem;
			 for (j = -sz; j < 0; j++)
			   if (p[j] == End1Fill)
			     fprintf(MACFH, " ..");
			   else
			     fprintf(MACFH, " %02X", (unsigned)p[j]);
			 fprintf(MACFH, "\n");
                         fflush(MACFH);
		       }
		     if (MacFixBad)
		       {
			 sz = ENDSIZE(macmalst[i].size);
			 p = macmalst[i].mem;
			 for (j = -sz; j < 0; j++)
			   p[j] = End1Fill;
		       }
		   }
	       if (r & 2)
		 {
                   CONST char *memo = macmalst[i].memo;

                        PMACMA("overflow", macmalst[i].mem);
			if (MacPrEnd)
			  {
			    sz = ENDSIZE(macmalst[i].size);
			    p = macmalst[i].mem;
			    for (j = n*size, e = j + sz; j < e; j++)
			      if (p[j] == End2Fill)
				fprintf(MACFH, " ..");
			      else
				fprintf(MACFH, " %02X", (unsigned)p[j]);
			    fprintf(MACFH, "\n");
                            fflush(MACFH);
			  }
			if (MacFixBad)
			  {
			    sz = ENDSIZE(macmalst[i].size);
			    p = macmalst[i].mem;
			    for (j = n*size, e = j + sz; j < e; j++)
			      p[j] = End2Fill;
			  }
		 }
               macma = macmalst + i;
	       if (r)
                        mac_print(macmafmt,
                          macma->fn,macma->ln,
                          "<- under/overflow alloced here",
                          NSZ2S(macma->n,macma->size),I2S(i),
                                  (macma->memo ? " " : ""),
                                  (macma->memo ? macma->memo : ""),
                                  (EPI_VOIDPTR_UINT)macma->mem);
               macma->used=0;
               TXmemCurrentAllocedBytes-=(size_t)macma->n*(size_t)macma->size;
               TXmemCurrentAllocedItems--;
               lasti=i;
               return(1);
              }
         }
    }
 if (refree != (size_t)(-1))
    {
      mac_print(macmafmt, fn, ln, "re-free of freed memory", "", I2S(refree),
                "", "", (EPI_VOIDPTR_UINT)mem);
      macma = macmalst + refree;
     mac_print(macmafmt,
               macma->fn,macma->ln,
               "<- re-free alloced from here",
               NSZ2S(macma->n,macma->size),I2S(refree),
               (macma->memo ? " " : ""), (macma->memo ? macma->memo : ""),
               (EPI_VOIDPTR_UINT)macma->mem);
    }
 return(0);
}


/************************************************************************/

void
mac_ptrwatch(mem,fn,ln)
void *mem;
CONST char *fn;
int ln;
{
  int           n = 0, size = 0;
  size_t        i;

 MAC_ENTERFUNC(fn, ln);
 getmemdebug();
 if (MacOff || !mac_ton || MacCountsOnly) goto done;

 mac_dovsum(fn,ln);
 for(i=0;i<MaxMacMas;i++)
    {
     if(macmalst[i].used && mem==(void *)macmalst[i].mem)
        {
         mac_ndx=i;
         dumpmacma(i,"ptrwatch",fn,ln);
         goto done;
        }
    }
 { CONST char *memo = CHARPN;
   PMACMA("invalid pointer", mem);}
done:
 MAC_EXITFUNC();
}

/************************************************************************/

void
mac_dodump(fn,ln)
CONST char *fn;
int ln;
{
  size_t        i;

 MAC_ENTERFUNC(fn, ln);
 if(!mac_ton || MacCountsOnly) goto done;
 mac_dovsum(fn,ln);
 for(i=0;i<MaxMacMas;i++)
    {
     if(macmalst[i].used)
        {
         mac_ndx=i;
         dumpmacma(i,"dump",fn,ln);
        }
    }
done:
 MAC_EXITFUNC();
}

/************************************************************************/

void *
mac_calloc(n, size, fn, ln, memo)
uint n,size;
CONST char *fn;
int ln;
CONST char      *memo;  /* (in, opt.) memo for msgs */
{
 byte *p;
 int i = -1;
 static int dumped=0;

 MAC_ENTERFUNC(fn, ln);
 getmemdebug();

 if (MacCountsOnly)
   {
     n *= size;
     if (n > (size_t)(EPI_OS_SIZE_T_MAX - ALIGNSZ))
       {
         p = NULL;
         goto done;
       }
     p = calloc(ALIGNSZ + n, 1);
     if (!p) goto done;
     ((ALIGNBUF *)p)->sz = n;
     TXmemCurrentAllocedBytes += OVERHEAD + n;
     if (TXmemCurrentAllocedBytes > TXmemMaxAllocedBytes)
       TXmemMaxAllocedBytes = TXmemCurrentAllocedBytes;
     TXmemCurrentAllocedItems++;
     p = p + ALIGNSZ;
     goto done;
   }

 if (MacTrace)
   {
     p = (byte *)calloc(n, size);
     fprintf(MACFH, "%s:%d: +%ld bytes calloc(%ld, %ld) = 0x%"
             EPI_VOIDPTR_HEX_FMT " - 0x%" EPI_VOIDPTR_HEX_FMT "\n",
             fn, ln, ((long)n)*((long)size), (long)n, (long)size,
             (EPI_VOIDPTR_UINT)p,
             (EPI_VOIDPTR_UINT)((byte *)p + (n*size ? n*size - (size_t)1 :
                                         (size_t)0)));
     fflush(MACFH);
     goto done;
   }
 if (MacOff) { p = (byte *)calloc(n, size); goto done; }

 mac_dovsum(fn,ln);
 if (n == 0 || size == 0)
   {
     mac_print(macmafmt, fn, ln, "attempt to calloc 0 bytes", NSZ2S(n, size),
               "", (memo ? " " : ""), (memo ? memo : ""), (EPI_VOIDPTR_UINT)0);
   }

 p=(byte *)malloc((n*size)+(2*ENDSIZE(size)));
 if(p==BYTEPN)
    {
     PMACMA("calloc out of memory", p);
     if(!dumped)
        {
         dumped=1;
         mac_visual();
        }
    }
 else
    {
     p+=ENDSIZE(size);
        {
         byte *s, *e;
         s=p;
         e=p+(n*size);
         for(;s<e;s++) *s=0;                           /* zero buffer */
        }
        addmacma(p, n, size, fn, ln, memo);
     mac_dovsum(fn,ln);
    }
done:
 MAC_EXITFUNC();
 return((void *)p);
}

/************************************************************************/

void *
mac_malloc(n, fn, ln, memo)
uint n;
CONST char *fn;
int ln;
CONST char      *memo;  /* (in, opt.) memo for msgs */
{
 byte *p;
 uint size=1, i = -1;
 static int dumped=0;

 MAC_ENTERFUNC(fn, ln);
 getmemdebug();

 if (MacCountsOnly)
   {
     if (n > (size_t)(EPI_OS_SIZE_T_MAX - ALIGNSZ))
       {
         p = NULL;
         goto done;
       }
     p = malloc(ALIGNSZ + n);
     if (!p) goto done;
     ((ALIGNBUF *)p)->sz = n;
     TXmemCurrentAllocedBytes += OVERHEAD + n;
     if (TXmemCurrentAllocedBytes > TXmemMaxAllocedBytes)
       TXmemMaxAllocedBytes = TXmemCurrentAllocedBytes;
     TXmemCurrentAllocedItems++;
     p = p + ALIGNSZ;
     goto done;
   }

 if (MacTrace)
   {
     p = (byte *)malloc(n);
     fprintf(MACFH, "%s:%d: +%ld bytes malloc(%ld) = 0x%" EPI_VOIDPTR_HEX_FMT
             " - 0x%" EPI_VOIDPTR_HEX_FMT "\n",
             fn, ln, (long)n, (long)n, (EPI_VOIDPTR_UINT)p,
             (EPI_VOIDPTR_UINT)((byte *)p + (n ? n - (size_t)1 : (size_t)0)));
     fflush(MACFH);
     goto done;
   }
 if (MacOff) { p = (byte *)malloc(n); goto done; }

 mac_dovsum(fn,ln);
 if (n == 0)
   {
     mac_print(macmafmt, fn, ln, "attempt to malloc 0 bytes", NSZ2S(n, 1),
               "", (memo ? " " : ""), (memo ? memo : ""), (EPI_VOIDPTR_UINT)0);
   }

 p=(byte *)malloc(n+(2*ENDSIZE(size)));
 if(p==BYTEPN)
    {
     PMACMA("malloc out of memory", p);
     if(!dumped)
        {
         dumped=1;
         mac_visual();
        }
    }
 else
    {
     p+=ENDSIZE(size);
     if(mac_ton && MacFill)
        {
         byte *s, *e;
         s=p;
         e=p+(n*size);
         for(;s<e;s++) *s=AllocFill;                  /* stomp buffer */
        }
     addmacma(p, n, size, fn, ln, memo);
     mac_dovsum(fn,ln);
    }
done:
 MAC_EXITFUNC();
 return((void *)p);
}

/************************************************************************/

void *
mac_remalloc(p, n, fn, ln, memo)
void *p;
uint n;
CONST char *fn;
int ln;
CONST char      *memo;  /* (in, opt.) memo for msgs */
{
 uint size=1, on, osize, i = -1;
 byte *ptr=(byte *)p;

 MAC_ENTERFUNC(fn, ln);
 getmemdebug();

 if (MacCountsOnly)
   {
     size_t        oldN;

     if (!p)
       {
         MAC_EXITFUNC();
         ptr = mac_malloc(n, fn, ln, memo);
         MAC_ENTERFUNC(fn, ln);
         goto done;
       }
     p = (char *)p - ALIGNSZ;
     oldN = ((ALIGNBUF *)p)->sz;
     if (n == 0)
       {
         MAC_EXITFUNC();
         ptr = mac_free(p, fn, ln, memo);
         MAC_ENTERFUNC(fn, ln);
         goto done;
       }
     ptr = realloc(p, ALIGNSZ + n);
     if (!ptr) goto done;
     TXmemCurrentAllocedBytes -= OVERHEAD + oldN;
     TXmemCurrentAllocedBytes += OVERHEAD + n;
     if (TXmemCurrentAllocedBytes > TXmemMaxAllocedBytes)
       TXmemMaxAllocedBytes = TXmemCurrentAllocedBytes;
     /* TXmemCurrentAllocedItems stays the same */
     ((ALIGNBUF *)ptr)->sz = n;
     ptr = ptr + ALIGNSZ;
     goto done;
   }

 if (MacTrace)
   {
     ptr = (byte *)realloc(p, n);
     fprintf(MACFH, "%s:%d: ? bytes realloc(0x%" EPI_VOIDPTR_HEX_FMT
             ", %ld) = 0x%" EPI_VOIDPTR_HEX_FMT " - 0x%" EPI_VOIDPTR_HEX_FMT "\n",
             fn, ln, (EPI_VOIDPTR_UINT)p, (long)n, (EPI_VOIDPTR_UINT)ptr,
             (EPI_VOIDPTR_UINT)((byte *)ptr + (n ? n - (size_t)1 : (size_t)0)));
     fflush(MACFH);
     goto done;
   }
 if (MacOff) { ptr = (byte *)realloc(p, n); goto done; }

 mac_dovsum(fn,ln);
 if(!delmacma(ptr,fn,ln,&on,&osize))
    {
     mac_print("invalid realloc pointer 0x%" EPI_VOIDPTR_HEX_FMT "\n",
               (EPI_VOIDPTR_UINT)p);
     PMACMA("realloc pointer was never alloced", p);
     ptr = BYTEPN;
     goto done;
    }
 ptr-=ENDSIZE(osize);
 ptr=(byte *)realloc(ptr,n+(2*ENDSIZE(1)));
 if(ptr==BYTEPN)
    {
     PMACMA("realloc failed", ptr);
    }
 else
    {
     ptr+=ENDSIZE(size);
     if(ENDSIZE(size)<ENDSIZE(osize))
        {
         byte *s, *e, *d;
         s=ptr-ENDSIZE(size)+ENDSIZE(osize);
         e=s+(on*osize);
         d=ptr;
         for(;s<e;s++,d++) *d= *s;
        }
     else
     if(ENDSIZE(size)>ENDSIZE(osize))
        {
         byte *s, *e, *d;
         e=ptr-ENDSIZE(size)+ENDSIZE(osize)-1;
         s=e+(on*osize);
         d=ptr+(n*size)-1;
         for(;s>e;s--,d--) *d= *s;
        }
     if(mac_ton && MacFill)
        {
         byte *s, *e;
         s=ptr+on*osize;
         e=ptr+n*size;
         for(;s<e;s++) *s=FreeFill;             /* stomp new region */
        }
     addmacma(ptr, n, size, fn, ln, memo);
     mac_dovsum(fn,ln);
    }
done:
 MAC_EXITFUNC();
 return((void *)ptr);
}

/************************************************************************/

void *
mac_recalloc(ptr, n, sz, fn, ln, memo)
void *ptr;
uint n;
uint sz;
CONST char *fn;
int ln;
CONST char      *memo;  /* (in, opt.) memo for msgs */
{
  return(mac_remalloc(ptr, n*sz, fn, ln, memo));
}

/************************************************************************/

void *
mac_free(p, fn, ln, memo)
void *p;
CONST char *fn;
int ln;
CONST char      *memo;  /* (in, opt.) memo for msgs */
{
 uint size, n;
 byte *ptr=(byte *)p;

 MAC_ENTERFUNC(fn, ln);
 getmemdebug();

 if (MacCountsOnly)
   {
     size_t        n;

     p = (char *)p - ALIGNSZ;
     n = ((ALIGNBUF *)p)->sz;
     free(p);
     TXmemCurrentAllocedBytes -= OVERHEAD + n;
     TXmemCurrentAllocedItems--;
     goto done;
   }

 if (MacTrace)
   {
     fprintf(MACFH, "%s:%d: ? bytes free(0x%" EPI_VOIDPTR_HEX_FMT ")\n",
             fn, ln, (EPI_VOIDPTR_UINT)p);
     fflush(MACFH);
     free(p);
     goto done;
   }
 if (MacOff) { free(p); goto done; }

 mac_dovsum(fn,ln);
 if(!delmacma(ptr,fn,ln,&n,&size))
   {
     /* KNG 20110110 free(NULL) is valid; do not report as bad: */
     if (p == NULL)
       {
         mac_print(macmafmt, fn, ln, "info: attempt to free NULL ptr", "",
                   "", (memo ? " " : ""), (memo ? memo : ""),
                   (EPI_VOIDPTR_UINT)ptr);
       }
     else
       {
         mac_print(macmafmt, fn, ln, "attempt to free unalloced ptr", "",
                   "", (memo ? " " : ""), (memo ? memo : ""),
                   (EPI_VOIDPTR_UINT)ptr);
#   ifdef SHOWBUF
         {
           int i;
           mac_print("   16 bytes:");
           for(i=0;i<16;i++,ptr++) mac_print((*ptr>32 && *ptr<127)?"%c":"[%02X]",0xff&*ptr);
           mac_print("\n");
         }
#   endif
       }
   }
 else
   {
    ptr-=ENDSIZE(size);
    if(mac_ton && MacFill)
       {
        byte *s, *e;
        s=ptr;
        e=ptr+((n*size)+(2*ENDSIZE(size)));
        for(;s<e;s++) *s=FreeFill;/* stomp the old data into oblivion */
        mac_dovsum(fn,ln);
       }
    if (!MacNoFree) free(ptr);
   }
done:
 MAC_EXITFUNC();
   return(VOIDPN);
}

/************************************************************************/

char *
mac_strdup(s, fn, ln, memo)
CONST char *s;
CONST char *fn;
int ln;
CONST char      *memo;  /* (in, opt.) memo for msgs */
{
 int size=1, n = -1, i=0;
 char *p;
 static int dumped=0;

 MAC_ENTERFUNC(fn, ln);
 getmemdebug();

 if (MacCountsOnly)
   {
     size_t     n;

     n = strlen(s) + 1;
     MAC_EXITFUNC();
     p = (char *)mac_malloc(n, fn, ln, memo);
     MAC_ENTERFUNC(fn, ln);
     if (!p) goto done;
     memcpy(p, s, n);
     goto done;
   }

 if (MacTrace)
   {
     p = strdup(s);
     fprintf(MACFH, "%s:%d: +%ld bytes strdup(0x%" EPI_VOIDPTR_HEX_FMT
             ") = 0x%" EPI_VOIDPTR_HEX_FMT " - 0x%" EPI_VOIDPTR_HEX_FMT "\n",
             fn, ln, (long)strlen(s) + 1L, (EPI_VOIDPTR_UINT)s, (EPI_VOIDPTR_UINT)p,
             (EPI_VOIDPTR_UINT)(p + strlen(p)));
     fflush(MACFH);
     goto done;
   }
 if (MacOff) { p = strdup(s); goto done; }

 if (s == (CONST char *)NULL)
   {
     mac_print(macmafmt, fn, ln, "attempt to strdup(NULL)", "", "",
               (memo ? " " : ""), (memo ? memo : ""), (EPI_VOIDPTR_UINT)s);
   }

 MAC_EXITFUNC();                    /* about to re-enter a public function */
 p=(char *)mac_malloc(strlen(s) + 1, fn, ln, memo);
 MAC_ENTERFUNC(fn, ln);
 if(p==CHARPN)
    {
     PMACMA("strdup out of memory", p);
     if(!dumped)
        {
         dumped=1;
         mac_visual();
        }
    }
 else
    {
     strcpy(p,s);
     mac_dovsum(fn,ln);
    }
done:
 MAC_EXITFUNC();
 return(p);
}

/************************************************************************/
#ifndef macintosh                                     /* MAW 01-20-93 */

char *
mac_getcwd(s, l, fn, ln, memo)
char *s;
int l;
CONST char *fn;
int ln;
CONST char      *memo;  /* (in, opt.) memo for msgs */
{
char *p;
int n, i, size;
static int dumped=0;

 MAC_ENTERFUNC(fn, ln);
 getmemdebug();

 if (MacCountsOnly)
   {
     char       *dup;

     p = getcwd(s, l);
     if (!p) goto done;
     if (!s)                                       /* `ret' alloced */
       {
         dup = mac_malloc((l <= 0 ? strlen(p) + 1 : l), fn, ln, memo);
         if (dup) strcpy(dup, p);
         free(p);
         p = dup;
       }
     goto done;
   }

 if (MacTrace)
   {
     p = getcwd(s, l);
     fprintf(MACFH, "%s:%d: +%ld bytes getcwd(0x%" EPI_VOIDPTR_HEX_FMT
             ", %d) = 0x%" EPI_VOIDPTR_HEX_FMT " - 0x%" EPI_VOIDPTR_HEX_FMT "\n",
             fn, ln, (s ? 0L : (l ? (long)l : (long)strlen(p) + 1L)),
             (EPI_VOIDPTR_UINT)s, l, (EPI_VOIDPTR_UINT)p,
             (EPI_VOIDPTR_UINT)(p + strlen(p)));
     fflush(MACFH);
     goto done;
   }
 if (MacOff) { p = getcwd(s, l); goto done; }

  mac_dovsum(fn,ln);
  p=getcwd(s,l);
  if(p==CHARPN){
    n=i=size=0;
    PMACMA("getcwd out of memory", p);
    if(!dumped){
      dumped=1;
      mac_visual();
    }
  }else if(s==CHARPN){
  char *tp;

  MAC_EXITFUNC();                   /* about to re-enter a public function */
    tp = mac_malloc((l <= 0 ? strlen(p) + 1 : l), fn, ln, memo);
    MAC_ENTERFUNC(fn, ln);
    strcpy(tp, p);
    mac_dovsum(fn,ln);
    if (!MacNoFree) free(p);
    p=tp;
  }
done:
  MAC_EXITFUNC();
  return(p);
}

/************************************************************************/

char *
mac_fullpath(b, f, l, fn, ln, memo)
char *b, *f;
int l;
CONST char *fn;
int ln;
CONST char      *memo;  /* (in, opt.) memo for msgs */
{
char *p;
int n, i, size;
static int dumped=0;

 MAC_ENTERFUNC(fn, ln);
 getmemdebug();

 if (MacCountsOnly)
   {
     char       *dup;

     MAC_EXITFUNC();                            /* avoid "re-entering" msg */
     p = realfullpath(b, f, l);
     MAC_ENTERFUNC(fn, ln);
     if (!p || b) goto done;
     MAC_EXITFUNC();
     dup = mac_strdup(p, fn, ln, memo);
     MAC_ENTERFUNC(fn, ln);
     free(p);
     p = dup;
     goto done;
   }

 if (MacTrace)
   {
     MAC_EXITFUNC();                            /* avoid "re-entering" msg */
     p = realfullpath(b, f, l);
     MAC_ENTERFUNC(fn, ln);
     fprintf(MACFH, "%s:%d: +%ld bytes fullpath(0x%" EPI_VOIDPTR_HEX_FMT
             ", 0x%" EPI_VOIDPTR_HEX_FMT ", %d) = 0x%" EPI_VOIDPTR_HEX_FMT
             " - 0x%" EPI_VOIDPTR_HEX_FMT "\n",
             fn, ln, 0L, (EPI_VOIDPTR_UINT)b, (EPI_VOIDPTR_UINT)f, l, (EPI_VOIDPTR_UINT)p,
             (EPI_VOIDPTR_UINT)(p + strlen(p)));
     fflush(MACFH);
     goto done;
   }
 if (MacOff)
   {
     MAC_EXITFUNC();                            /* avoid "re-entering" msg */
     p = realfullpath(b, f, l);
     MAC_ENTERFUNC(fn, ln);
     goto done;
   }

  mac_dovsum(fn,ln);
  MAC_EXITFUNC();                               /* avoid "re-entering" msg */
  p=realfullpath(b,f,l);
  MAC_ENTERFUNC(fn, ln);
  if(p==CHARPN){
    n=i=size=0;
    PMACMA("fullpath out of memory", p);
    if(!dumped){
      dumped=1;
      mac_visual();
    }
  }else if(b==CHARPN){
  char *tp;

  MAC_EXITFUNC();                   /* about to re-enter a public function */
    tp=mac_strdup(p, fn, ln, memo);
    mac_free(p, fn, ln, memo);         /* KNG 990810 mac_free() not free() */
    MAC_ENTERFUNC(fn, ln);
    p=tp;
  }
done:
  MAC_EXITFUNC();
  return(p);
}

#endif                                                   /* macintosh */

#ifdef EPI_HAVE_REALPATH_ALLOC
char *
mac_realpath(path, resolvedPath, fn, ln, memo)
CONST char      *path;          /* (in) path to resolve */
char            *resolvedPath;  /* (in/out, opt.) output buffer */
CONST char      *fn;            /* (in, opt.) source function */
int             ln;             /* (in) source line */
CONST char      *memo;          /* (in, opt.) memo for msgs */
{
  int           size = 1, n = -1, i = 0;
  char          *ret = CHARPN, *dup = CHARPN;
  size_t        retSz;

  MAC_ENTERFUNC(fn, ln);
  getmemdebug();

  if (MacCountsOnly)
    {
      char      *dup;

      ret = realpath(path, resolvedPath);
      if (!ret || ret == resolvedPath) goto done;
      MAC_EXITFUNC();
      dup = mac_strdup(ret, fn, ln, memo);
      MAC_ENTERFUNC(fn, ln);
      free(ret);
      ret = dup;
      goto done;
    }

  if (MacTrace)
    {
      ret = realpath(path, resolvedPath);
      if (resolvedPath == CHARPN)               /* then `ret' is alloced */
        {
          fprintf(MACFH,
                  "%s:%d: +%ld bytes realpath(0x%" EPI_VOIDPTR_HEX_FMT
                  ", 0x%" EPI_VOIDPTR_HEX_FMT ") = 0x%" EPI_VOIDPTR_HEX_FMT
                  " - 0x%" EPI_VOIDPTR_HEX_FMT "\n",
                  fn, ln, (long)strlen(ret) + 1L, (EPI_VOIDPTR_UINT)path,
                  (EPI_VOIDPTR_UINT)resolvedPath, (EPI_VOIDPTR_UINT)ret,
                  (EPI_VOIDPTR_UINT)(ret + strlen(ret)));
          fflush(MACFH);
        }
      goto done;
    }
  if (MacOff)
    {
      ret = realpath(path, resolvedPath);
      goto done;
    }

  if (path == CHARPN)
    {
      mac_print(macmafmt, fn, ln, "attempt to realpath(NULL, ...)", "", "",
                (memo ? " " : ""), (memo ? memo : ""), (EPI_VOIDPTR_UINT)path);
   }

  ret = realpath(path, resolvedPath);
  if (ret == CHARPN)                            /* failed */
    {
      if (resolvedPath == CHARPN)               /* alloc requested */
        {
          PMACMA("realpath out of memory", ret);
        }
      else                                      /* buffer given */
        {
          PMACMA("realpath failed", ret);
        }
      goto done;
    }
  if (ret == resolvedPath)                      /* not alloced */
    goto done;

  /* Dup `ret', so mac_malloc() can add it to our list: */
  retSz = strlen(ret) + 1;
  MAC_EXITFUNC();                               /* for mac_... re-entry */
  dup = (char *)mac_malloc(retSz, fn, ln, memo);
  MAC_ENTERFUNC(fn, ln);
  if (dup == CHARPN)
    {
      PMACMA("realpath dup out of memory", dup);
      free(ret);                                /* real free(): realpath() */
      ret = CHARPN;
      goto done;
    }
  memcpy(dup, ret, retSz);
  free(ret);                                    /* real free(): realpath() */
  ret = dup;
  dup = CHARPN;
  mac_dovsum(fn,ln);

done:
  MAC_EXITFUNC();
  return(ret);
}
#endif /* EPI_HAVE_REALPATH_ALLOC */

/************************************************************************/

void
mac_docheck(fr,fn,ln)
int fr;
CONST char *fn;
int ln;
{
 int cnt=0;
 size_t sz, i;
 MACMA  *macma;

 MAC_ENTERFUNC(fn, ln);
 getmemdebug();

 if (MacCountsOnly) goto doSummary;
 if (MacOff) goto done;

 mac_dovsum(fn,ln);
 for(i=0;i<MaxMacMas;i++)
    {
     if(i==mac_ndx)
       dumpmacma(i,"chkmacma","",0);
     if(macmalst[i].used)
        {
         cnt++;
         if (MacLeak)
	   {
             macma = macmalst + i;
             mac_print(macmafmt, macma->fn, macma->ln,
                       "memory alloced here not freed",
                       NSZ2S(macma->n, macma->size), I2S(i),
                       (macma->memo ? " " : ""),
                       (macma->memo ? macma->memo : ""),
                       (EPI_VOIDPTR_UINT)macma->mem);
             if (MacPrintLeakData)
               {
                 sz = macma->n*macma->size;
                 mac_hexdump(macma->mem,
                             (sz > (size_t)MacPrintLeakData ?
                              (size_t)MacPrintLeakData : sz));
               }
	   }
#ifndef _INTELC32_   /* WTF Intel seems to have already freed it by now */
#ifdef NEVER
         if(fr)
            free(macmalst[i].mem);
#endif
#endif
       }
    }

doSummary:
 if (!MacSummary) goto done;
 mac_print("Max total memory allocated at once: %*" EPI_OS_SIZE_T_DEC_FMT " bytes\n",
           (EPI_OS_SIZE_T_BITS/3), TXmemMaxAllocedBytes);
#if defined(unix) && !defined(__APPLE__)
 mac_print("Max dynamic data space:             %*" EPI_VOIDPTR_DEC_FMT " bytes\n",
           (EPI_OS_SIZE_T_BITS/3), (EPI_VOIDPTR_UINT)((char *)sbrk(0)-(char *)&end));
#endif
 mac_print("%" EPI_OS_SIZE_T_DEC_FMT " pointers containing %" EPI_OS_SIZE_T_DEC_FMT " bytes not freed\n",
           (size_t)(MacCountsOnly ? TXmemCurrentAllocedItems : cnt),
           TXmemCurrentAllocedBytes);
done:
 MAC_EXITFUNC();
}

/**********************************************************************/

#if 1
void
mac_exit(n)
int n;
{
 getmemdebug();
 if (MacOff) goto done;

 mac_visual();
 mac_check(1);
done:
 exit(n);
}

/* the following allow routines not compiled MEMDEBUG to
** work with those that were
*/
/**********************************************************************/
void *
epi_calloc(n,sz)
uint n;
uint sz;
{
  return(mac_calloc(n,sz,"UNKNOWN",0,CHARPN));
}                                                 /* end epi_calloc() */
/**********************************************************************/

/**********************************************************************/
void *
epi_malloc(sz)
uint sz;
{
   return(mac_malloc(sz,"UNKNOWN",0,CHARPN));
}                                                 /* end epi_malloc() */
/**********************************************************************/

/**********************************************************************/
void *
epi_remalloc(p,sz)
void *p;
uint sz;
{
   return(mac_remalloc(p,sz,"UNKNOWN",0,CHARPN));
}                                               /* end epi_remalloc() */
/**********************************************************************/

/**********************************************************************/
void *
epi_recalloc(p,n,sz)                      /* wtf - clear the new mem? */
void *p;
uint n;
uint sz;
{
   return(mac_recalloc(p,n,sz,"UNKNOWN",0,CHARPN));
}                                               /* end epi_recalloc() */
/**********************************************************************/

/**********************************************************************/
void *
epi_free(p)
void *p;
{
  return(mac_free(p,"UNKNOWN",0,CHARPN));
}                                                   /* end epi_free() */
/**********************************************************************/

/**********************************************************************/
void
epi_exit(rc)
int rc;
{
	mac_exit(rc);
}                                                   /* end epi_exit() */
/**********************************************************************/

/**********************************************************************/
char *
epi_strdup(s)
char *s;
{
   return(mac_strdup(s,"UNKNOWN",0,CHARPN));
}                                                 /* end epi_strdup() */
/**********************************************************************/

#ifndef macintosh                                     /* MAW 01-20-93 */
/**********************************************************************/
char *
epi_getcwd(s,l)
char *s;
int l;
{
	return(mac_getcwd(s,l,"UNKNOWN",0,CHARPN));
}                                                 /* end epi_getcwd() */
/**********************************************************************/

/**********************************************************************/
char *
epi_fullpath(b,f,l)
char *b, *f;
int l;
{
	return(mac_fullpath(b,f,l,"UNKNOWN",0,CHARPN));
}                                               /* end epi_fullpath() */
/**********************************************************************/
#endif                                                   /* macintosh */

#endif                                                    /* _WIN32 */
#else                                                    /* !MEMDEBUG */
#if 1

#undef malloc
#undef calloc
#undef realloc
#undef remalloc
#undef recalloc
#undef free
#undef exit
#undef strdup
#undef getcwd
#undef fullpath

#ifdef unix
#ifndef sgi
   extern char *getcwd ARGS((char *,size_t));
#endif
#endif

/**********************************************************************/
void *
epi_calloc(n,sz)
uint n;
uint sz;
{
   return(calloc(n,sz));
}                                                 /* end epi_calloc() */
/**********************************************************************/

/**********************************************************************/
void *
epi_malloc(sz)
uint sz;
{
   return(malloc(sz));
}                                                 /* end epi_malloc() */
/**********************************************************************/

/**********************************************************************/
void *
epi_remalloc(p,sz)
void *p;
uint sz;
{
   return(realloc(p,sz));
}                                               /* end epi_remalloc() */
/**********************************************************************/

/**********************************************************************/
void *
epi_recalloc(p,n,sz)                      /* wtf - clear the new mem? */
void *p;
uint n;
uint sz;
{
   return(realloc(p,n*sz));
}                                               /* end epi_recalloc() */
/**********************************************************************/

/**********************************************************************/
void *
epi_free(p)
void *p;
{
   free(p);
   return(VOIDPN);
}                                                   /* end epi_free() */
/**********************************************************************/

/**********************************************************************/
void
epi_exit(rc)
int rc;
{
   exit(rc);
}                                                   /* end epi_exit() */
/**********************************************************************/

/**********************************************************************/
char *
epi_strdup(s)
char *s;
{
   return(strdup(s));
}                                                 /* end epi_strdup() */
/**********************************************************************/

#ifndef macintosh                                     /* MAW 01-20-93 */
/**********************************************************************/
char *
epi_getcwd(s,l)
char *s;
int l;
{
	return(getcwd(s,l));
}                                                 /* end epi_getcwd() */
/**********************************************************************/

/**********************************************************************/
char *
epi_fullpath(b,f,l)
char *b, *f;
int l;
{
	return(realfullpath(b,f,l));
}                                               /* end epi_fullpath() */
/**********************************************************************/
#endif                                                   /* macintosh */

#endif                                                   /* !_WIN32 */
#endif                                                    /* MEMDEBUG */

/************************************************************************/

#ifdef MEMVISUAL                                      /* MAW 07-03-90 */
#ifdef MSDOS
void
mac_visual()                            /* display heap map for MSDOS */
{
int rc;
int pflag=(-1);
uint cnt, l, s;
unsigned long size;
int *ptr;
struct _heapinfo hi;
int x=0, y=0;      /* 2 column display will most always fit on screen */
static int disp=1;

   mcls();
   hi._pentry=NULL;
   while(disp && (rc=_heapwalk(&hi))==_HEAPOK){
      if(hi._useflag!=pflag){
         if(pflag!=(-1)){
            mprintf(y,x,"%s:%6lu",pflag==_USEDENTRY?"Used":"Free",size);
            if(cnt>1) mprintf(y,x+11," n:%4u l:%5u s:%5u",cnt,l,s);
            y++;
            if(y>=25){
               y=0;
               x=40;
            }
         }
         pflag=hi._useflag;
         ptr=hi._pentry;
         size=0L;
         cnt=0;
         l=0;
         s=0xffff;
      }
      cnt++;
      size+=(long)hi._size;
      if(hi._size>l) l=hi._size;
      if(hi._size<s) s=hi._size;
   }
   if(pflag!=(-1)){
      mprintf(y,x,"%s:%6lu",pflag==_USEDENTRY?"Used":"Free",size);
      if(cnt>1) mprintf(y,x+11," n:%4u l:%5u s:%5u",cnt,l,s);
      y++;
      if(y>=25){
         y=0;
         x=40;
      }
   }
   if(disp && rc!=_HEAPEMPTY && rc!=_HEAPEND){
      mprintf(y,x,"Bad heap (rc==%d)",rc);
      y++;
      if(y>=25){
         y=0;
         x=40;
      }
   }
}                                                 /* end mac_visual() */
/**********************************************************************/
#else                                                       /* !MSDOS */
/**********************************************************************/
extern char *sbrk();
extern end, etext, edata;

#ifndef ulong
#  define ulong unsigned long
#endif
#ifndef uint
#  define uint  uint
#endif
#ifndef byte
#  define byte  byte
#endif

#define user2MALLOC(a) ((a)-4)
#define isused(a)      ((int)((*((ulong *)a))&1))
#define validptr(a)    ((char *)((ulong)(a)&~1))
#define nextblk(a)     validptr(*(char **)(a))
#define blksize(a)     ((ulong)(nextblk(a)-(a)-4))

/**********************************************************************/
void mac_visual()
{
char *n, *p, *stop, *ptr;
int i, pused=(-1);
ulong cnt, size, s=0xffffffff, l=0;

   stop=sbrk(0)-4;
   p=nextblk(stop);
   for(;p<stop;p=nextblk(p)){
      if(isused(p)!=pused){
         if(pused!=(-1)){
            mac_print("%s:%6lu",pused?"Used":"Free",size);
            if(cnt>1) mac_print(" n:%4lu l:%6lu s:%6lu",cnt,l,s);
            mac_print("\n");
         }
         pused=isused(p);
         ptr=p;
         size=0;
         cnt=0;
         l=0;
         s=0xffffffff;
      }
      cnt++;
      size+=blksize(p);
      if(blksize(p)>l) l=blksize(p);
      if(blksize(p)<s) s=blksize(p);
   }
   if(pused!=(-1)){
      mac_print("%s:%6lu",pused?"Used":"Free",size);
      if(cnt>1) mac_print(" n:%4lu l:%6lu s:%6lu",cnt,l,s);
      mac_print("\n");
   }
}                                                 /* end mac_visual() */
/**********************************************************************/
#endif                                                       /* MSDOS */
#endif                                                   /* MEMVISUAL */

#ifdef _WIN32
#  define RDWR_LEN_TYPE unsigned
#else /* !_WIN32 */
#  define RDWR_LEN_TYPE size_t
#endif /* !_WIN32 */

                         /* always provide these functions in the lib */
/**********************************************************************/
size_t
epi_fread(buf,sz,n,fp)
void *buf;
size_t sz, n;
FILE *fp;
{
RDWR_LEN_TYPE tr;
EPI_SSIZE_T     nr;

   tr = (RDWR_LEN_TYPE)(n*sz);
   nr=read(fileno(fp),buf,tr);
   if (nr == (EPI_SSIZE_T)(-1)) nr = 0;
   if (nr>0 && sz>1) nr /= (EPI_SSIZE_T)sz;
   return((size_t)nr);
}                                                  /* end epi_fread() */
/**********************************************************************/

/**********************************************************************/
size_t
epi_fwrite(buf,sz,n,fp)
void *buf;
size_t sz, n;
FILE *fp;
{
RDWR_LEN_TYPE   tw;
EPI_SSIZE_T     nw;

   tw = (RDWR_LEN_TYPE)(n*sz);
   nw=write(fileno(fp),buf,tw);
   if (nw == (EPI_SSIZE_T)(-1)) nw = 0;
   if(nw>0 && sz>1) nw /= (EPI_SSIZE_T)sz;
   return((size_t)nw);
}                                                 /* end epi_fwrite() */
/**********************************************************************/

/**********************************************************************/
int
epi_fseek(fp,off,orig)
FILE *fp;
long off;
int orig;
{
   return(lseek(fileno(fp),off,orig)==(-1L)?(-1):0);
}                                                  /* end epi_fseek() */
/**********************************************************************/

/**********************************************************************/
long
epi_ftell(fp)
FILE *fp;
{
   return(lseek(fileno(fp),0L,SEEK_CUR));
}                                                  /* end epi_ftell() */
/**********************************************************************/

/**********************************************************************/
int
epi_fflush(fpunused)
FILE *fpunused;
{
   return(0);
}                                                 /* end epi_fflush() */
/**********************************************************************/

#ifdef NEVER_MSDOS                       /* MAW 03-05-93 - see stat.c */
#undef stat
/**********************************************************************/
int
epi_stat(fn,st)                             /* MAW 02-05-93- wrote it */
CONST char *fn;
struct stat *st;
{
int rc;

   if((rc=stat(fn,st))==0){
      /* it turns out that no 2 dos compilers give the same time values! */
#  ifdef __TURBOC__                      /* handles turbo and borland */
      st->st_mtime+=10800;
      st->st_atime+=10800;
      st->st_ctime+=10800;
#  endif
#ifdef NEVER      /* MAW 01-26-93 - intel v1.1a agrees with microsoft */
#  ifdef _INTELC32_
      st->st_mtime-=3600;
      st->st_atime-=3600;
      st->st_ctime-=3600;
#  endif
#endif
#  ifdef __TSC__
      st->st_mtime+=25200;
      st->st_atime+=25200;
      st->st_ctime+=25200;
#  endif
#  if 0
      st->st_mtime-=2209075200;
      st->st_atime-=2209075200;
      st->st_ctime-=2209075200;
#  endif
   }
   return(rc);
}                                                   /* end epi_stat() */
/**********************************************************************/
#endif                                                 /* NEVER_MSDOS */
