#ifndef TIMPORT_H
#define TIMPORT_H
/**********************************************************************/
#define VERSION "2.11"
/**********************************************************************/
#include "csv.h"
/**********************************************************************/
#define MAXTBL 16
#if TX_VERSION_NUM >= 60000
#define MAXFLD 1000 /* match tisp TSPMAXARGS */
#else
#define MAXFLD (MAXTBL*DDFIELDS)
#endif

/* these structures are private - no drilling */
#define TBLD struct tbl_struct
#define TBLDPN (TBLD *)NULL
TBLD
{
   char   *tbname;                                      /* table name */
   int     noid;                     /* suppress addition of id field */
   int     drop;             /* MAW 10-03-96 - drop table before load */
   TX     *tx;                                      /* texis database */
   int     ipar;                              /* scratch param number */
   int     nflds;                          /* number of fields in tbl */
   char    query[2048];            /* insert stmt construction buffer */
};

/* these structures are private - no drilling */
#define FLDD struct fldd_struct
#define FLDDPN (FLDD *)NULL
FLDD
{                                           /* field description/data */
   int   nflds;                              /* number used in arrays */
   char *name [MAXFLD];                              /* name of field */
   int   type [MAXFLD];                        /* texis type of field */
   int   width[MAXFLD];      /* MAW 08-28-96 - width for create table */
   int   ff   [MAXFLD];               /* MAW 10-07-96 - fromfile flag */
   char *expr [MAXFLD+2]; /* tag matching expression (+recdelim+term) */
   char *def  [MAXFLD];                        /* field default value */
   char *data [MAXFLD];                  /* field data being imported */
   char *fdata[MAXFLD];            /* MAW 08-03-95 - free data[] flag */
   int   dlen [MAXFLD];                           /* len of data item */
   char *lbuf [MAXFLD];        /* MAW 02-27-96 - length string buffer */
   int   ss[2][MAXFLD];                         /* start/stop subexpr */
   TBLD *tbl  [MAXFLD];                            /* table name, etc */
   char *numbuf[MAXFLD];        /* MAW 08-29-96 - incrementing number */
#ifdef USEXML
   HTBUF   *xn[MAXFLD];                 /* MAW 04-26-99 - xml "field" */
#endif
};

/* these structures are private - no drilling */
#define TIMPORT struct lod_struct
#define TIMPORTPN (TIMPORT *)NULL
TIMPORT
{                                           /* overall loader control */
   /* begin schema */
   char   *schemafn;          /* name of supplied schema file for ref */
   char   *schemabuf;               /* copy of supplied schema buffer */
   /* make changes to schema are reflected in genschema() and doc()   */
   char   *host;                   /* what texis server to connect to */
   char   *port;                                               /* ... */
   char   *user;                                               /* ... */
   char   *group;                                              /* ... */
   char   *pass;                                               /* ... */
   char   *dbname;                                   /* database name */
   char   *recdelim;                     /* multiple record delimiter */
   char   *recexpr;       /* single expression matching entire record */
   char   *readexpr;           /* for freadex when not using rexdelim */
   char   *datefmt;                 /* format string describing dates */
   int     keepemptyrec;  /* forces filling "empty" rec with defaults */
   int     incstats;                /* include file stat fields (3db) */
   int     multiple;         /* read multiple records from input flag */
   int     firstmatch;      /* store first expr match instead of last */
   char   *allmatch;       /* MAW 11-01-96 - combine all expr matches */
   int     allmatchlen;          /* MAW 11-01-96 - length of allmatch */
   int     prevfldi;                                  /* MAW 11-01-96 */
   int     trimspace;       /* MAW 01-10-96 - trim spaces from fields */
   int     trimdollar;           /* MAW 11-01-96 - remove dollar sign */
   int     format;                      /* MAW 08-22-96 - std formats */
   int     dbfxlate;       /* MAW 10-07-96 - dbf codepage translation */
   char   *csvdelim;                /* MAW 08-22-96 - for std formats */
   int     csvquote;                 /* MAW 01-08-97 - process quotes */
   int     csvequote;       /* MAW 02-25-99 - process embedded quotes */
   int     keepfirst;                 /* keep first csv/col style row */
   int     createtbl;    /* MAW 08-22-96 - create the table if needed */
   FLDD    flds;                  /* list of fields being manipulated */
   TBLD    tbl[MAXTBL];                        /* table name/settings */
   int     ntbl;                                /* number used in tbl */
   int     override;           /* override tblname etc instead of add */
   size_t  recbufmax;                       /* amt to alloc to recbuf */
   /* end schema */
   int     firstfld;               /* first field in flds, not inc id */
   int     ifsize;                    /* fld index of file size field */
   int     iftime;                    /* fld index of file time field */
   ft_int64 vfsize;                      /* value for file size field */
   ft_date  vftime;                      /* value for file time field */
   char   *fname;                          /* current import filename */
   FILE   *recfp;                              /* current import file */
   int     ispipe;            /* is the import file a pipe (or stdin) */
   off_t   totread;      /* MAW 11-21-96 - total bytes read from file */
   char   *recbuf;             /* beginning of input record(s) region */
   char    recbufc;        /* saved char past recbuf for pipereadex() */
   char   *recend;                   /* end of input record(s) region */
   char   *recstart;                    /* beginning of single record */
   char   *recstop;                           /* end of single record */
   size_t  recbufsz;                         /* amt alloced to recbuf */
   FFS    *rxrecdelim;                           /* compiled recdelim */
   FFS    *rxrecexpr;                             /* compiled recexpr */
   int     nrxrecsexpr;/* MAW 12-03-97 - number of subexpr's in rxrecexpr */
   FFS   **rxrecsexpr;/* MAW 12-03-97 - pointers to subexpr's of rxrecexpr */
   FFS    *rxreadexpr;                           /* compiled readexpr */
   FFS    *rxendexpr;            /* alias to rxrecdelim or rxreadexpr */
   RLEX   *rx;                                /* lex for finding tags */
   int     restart;             /* read more records from buffer flag */
   char   *sbuf;                           /* schema file read buffer */
   char   *sp;                         /* schema file current pointer */
   int     eol;                   /* schema file eol encountered flag */
   SERVER *se;                                        /* texis server */
   ulong   nrows;                            /* number of rows loaded */
   ulong   fnrows;            /* number of rows loaded from this file */
   ulong   ndups;/* MAW 12-02-96 - number of rows duped via unique index */
   ulong   fndups;/* MAW 12-02-96 - number of rows duped via unique index */
   CSV    *cs;                                        /* MAW 08-23-96 */
#ifdef USEXML
   XDTD   *xd;                                        /* MAW 04-26-99 */
   int     xmlnohtml;                                 /* MAW 01-22-01 */
   int     xmlbase64;                               /* JMT 2004-11-12 */
   int     xmldatasetlevel;  /* the depth at which the dataset begins */
   XMLNS  *xmlns;               /* the list of XML prefix/value pairs */
#endif
   int     usejc;                  /* MAW 04-26-99 - use join counter */
   ft_counter *jc;               /* MAW 04-26-99 - join counter value */
   int     trapcore;
   int     verbose;
   int     showcalls;
   int     tic;
   int     dump;
   int     dumpabbrev;
   int     verschema;
   char  **files;                       /* MAW 11-18-96 - input files */
   int     nfiles;                                /* # of input files */
   int     afiles;                              /* # alloced in files */
   int     dupwarn;   /* MAW 08-04-97 - warn about dup field in input */
   char   *ubuf;      /* MAW 08-18-97 - read from buf instead of file */
   size_t  ubuflen;   /* MAW 08-18-97 - read from buf instead of file */
   int     needprep;             /* need to finish setting up structs */
   int     inonly;/* MAW 09-15-97 - don't care about db/tbl output stuff */
   FLDOP  *fo;            /* MAW 02-05-98 - for data type conversions */
   time_t now;                    /* MAW 07-06-99 - when load started */
   struct tm tnow;                /* MAW 07-06-99 - when load started */
};
/**********************************************************************/
TIMPORT *opentimport   ARGS((char *schema));    /* schema gets copied */
TIMPORT *closetimport  ARGS((TIMPORT *tp));
int      settschema    ARGS((TIMPORT *tp,char *schema));/* schema gets copied */
                         /* addtschema() may not be used with caching */
int      addtschema    ARGS((TIMPORT *tp,char *schema));/* schema must persist unchanged til close */
int      settfile      ARGS((TIMPORT *tp,char *filename));
            /* I won't noticably mangle the buffer given to settbuf() */
int      settbuf       ARGS((TIMPORT *tp,char *data,size_t len));
           /* finalize sanity check on schema and setup. may be skipped. */
                      /* it will be done automatically with first get */
int      preptimport   ARGS((TIMPORT *tp,char *filename,char *data,size_t len));
/* I own the data in these FLDLSTs. You can't "steal" it as with gettx() */
           /* they must be freed with freetfldlst(), not freefldlst() */
FLDLST  *gettfldinfo   ARGS((TIMPORT *tp));/* get just names and types */
FLDLST  *gettimport    ARGS((TIMPORT *tp));/* get the next parsed row */
FLDLST  *getasctimport ARGS((TIMPORT *tp));/* get the next parsed row in ascii */
FLDLST  *freetfldlst   ARGS((TIMPORT *tp,FLDLST *fl));
int      settinonly    ARGS((TIMPORT *tp,int onoff));
int      settdupwarn   ARGS((TIMPORT *lo,int level));
int      setctimport   ARGS((int onoff)); /* turn schema cache on/off */
void     clearctimport ARGS((int inUseToo));    /* clear schema cache */
/* you must call clearctimport() before exit() if you ever turned
   on caching with setctimport(), even if you turned if off  */
/**********************************************************************/
#endif                                                   /* TIMPORT_H */
