#ifndef CSV_H
#define CSV_H
/**********************************************************************/
#define CSV_TNONE  0
#define CSV_TTEXT  1
#define CSV_TINT   2
#define CSV_TFLOAT 3
#define CSV_TDATE  4
#define CSVFLD struct csvfld_struct
#define CSVFLDPN (CSVFLD *)NULL
CSVFLD
{
   char *name;
   int   type;
   int   col;                            /* start column for col mode */
   int   width;                        /* max width seen during guess */
   char *data;
   int   datalen;
};
#define CSV struct csv_struct
#define CSVPN (CSV *)NULL
CSV
{
   char   *fn;
   FILE   *fp;
   char   *fsep;
   int     fseplen;
   int     nflds;                       /* drill here after csvread() */
   int     aflds;
   int     nnames;                      /* drill here after csvread() */
   CSVFLD *fld;                         /* drill here after csvread() */
   int     gotnames;
   int     getnames;
   char   *buf;
   int     bufsz;
   int     bufused;
   int     bufi;
   int     rquote; /* MAW 01-08-97 - respect quotes, else ignore them */
   int     equote; /* MAW 02-25-99 - respect escaped quotes           */
   char   *ubuf;/* MAW 08-04-97 - for reading buffers instead of files */
   int     ubufsz;                                    /* MAW 08-04-97 */
   int     ubufi;                                     /* MAW 08-04-97 */
   int     ateof;  /* MAW 12-11-97 - eof encountered during last read */
};
/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
CSV *csvopen   ARGS((char *filename,char *fsep));
int  csvset    ARGS((CSV *cs,char *filename));
int  csvsetbuf ARGS((CSV *cs,char *buf,int bufsz));
int  csvquote  ARGS((CSV *cs,int respectquotes));
int  csvescquote ARGS((CSV *cs,int respectescapedquotes));
int  csvgnames ARGS((CSV *cs,int tf));
int  csvguess  ARGS((CSV *cs));
int  csvread   ARGS((CSV *cs));
CSV *csvclose  ARGS((CSV *cs));
/**********************************************************************/
#endif                                                       /* CSV_H */
