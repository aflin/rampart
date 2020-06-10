#ifndef ORACLE_H
#define ORACLE_H
/**********************************************************************/
typedef enum ORACLE_TYPE
{
	ORACLE_TNONE  =  0,
	ORACLE_TTEXT  =  1,
	ORACLE_TINT   =  2,
	ORACLE_TFLOAT =  3,
	ORACLE_TBLOB  =  8,
	ORACLE_TDATE  = 12,
	ORACLE_SPACER = 31,
	ORACLE_TCHAR  = 96,
	ORACLE_TCLOB  =  112,
	ORACLE_SPACER2 = 178
} ORACLE_TYPE;

#define ORACLEFLD struct oraclefld_struct
#define ORACLEFLDPN (ORACLEFLD *)NULL
ORACLEFLD
{
   char *name;
   ORACLE_TYPE   type;
   int   col;                            /* start column for col mode */
   int   width;                        /* max width seen during guess */
   char *data;
   int   datalen;
};
#define ORACLE struct oracle_struct
#define ORACLEPN (ORACLE *)NULL
ORACLE
{
   char   *fn;
   FILE   *fp;
   char   *fsep;
   int     fseplen;
   int     nflds;                       /* drill here after oracleread() */
   int     aflds;
   int     nnames;                      /* drill here after oracleread() */
   ORACLEFLD *fld;                         /* drill here after oracleread() */
   int     gotnames;
   int     getnames;
   char   *buf;
   int     bufsz;
   int     bufused;
   int     bufi;
   int     rquote; /* MAW 01-08-97 - respect quotes, else ignore them */
   char   *ubuf;/* MAW 08-04-97 - for reading buffers instead of files */
   int     ubufsz;                                    /* MAW 08-04-97 */
   int     ubufi;                                     /* MAW 08-04-97 */
   int     ateof;  /* MAW 12-11-97 - eof encountered during last read */
   int	   hasextradata;/* JMT 2000-09-28 */
   char   *tablename;
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
ORACLE *oracleopen   ARGS((char *filename, char *tablename));
int  oracleset    ARGS((ORACLE *cs,char *filename, char *tablename));
int  oraclesetbuf ARGS((ORACLE *cs,char *buf,int bufsz, char *tablename));
int  oraclequote  ARGS((ORACLE *cs,int respectquotes));
int  oraclegnames ARGS((ORACLE *cs,int tf));
int  oracleguess  ARGS((ORACLE *cs));
int  oracleread   ARGS((ORACLE *cs));
ORACLE *oracleclose  ARGS((ORACLE *cs));
/**********************************************************************/
#endif                                                       /* ORACLE_H */
