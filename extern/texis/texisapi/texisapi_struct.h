#ifndef TEXISAPI_STRUCT_H
#define TEXISAPI_STRUCT_H

#define FLDLSTMAX 1000
#define FLDLSTPN (FLDLST *)NULL
/** Container for results from texis_ calls
 *
 * Contains the results from texis_fetch etc.
 *
 * ndata contains the actual size of the data returned.
 * ondata contains the declared size of the field in the table, which may
 * be different than ndata if it is a var field.
 */
typedef struct FLDLST
{
   int      n;                 /**< number of fields in the lstst */
   int      type [FLDLSTMAX];  /**< Data types - of FTN_ type */
   void    *data [FLDLSTMAX];  /**< pointer to data */
   int      ndata[FLDLSTMAX];  /**< number of items in data, e.g. char length */
   char    *name [FLDLSTMAX];  /**< name of field */
   int      ondata[FLDLSTMAX]; /**< number of items in schema */
} FLDLST;
/************************************************************************/
#ifndef FLDOPPN
# define FLDOP void
# define FLD void
#endif
#ifndef DDFIELDS
# define DDFIELDS 50
#endif
#define TEXIS struct texis_struct
#define TEXISPN (TEXIS *)NULL
TEXIS {
   HENV     henv;
   HDBC     hdbc;
   HSTMT    hstmt;
   int      donullcb;
   FLDOP   *fo;/* MAW 10-05-94 - for doing conversions on result rows */
   FLD     *fld[DDFIELDS];          /* MAW 10-06-94 - for conversions */
	 FLDLST   fl;
   int	   nfld;   /* JMT 1999-03-04 - count fld, make free efficient */
   RETCODE      lastRetcode;    /* last SQLPrepare() etc. RETCODE */
};
/************************************************************************/
#endif /* TEXISAPI_STRUCT_H */
