#ifndef DBTABLE_H
#define DBTABLE_H

#ifndef DBF_H
#include "dbf.h"
#endif

/************************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/************************************************************************/

#define TBLFNSZ 128                   /* the max length of a table name */

/************************************************************************/

typedef struct TBL_tag
{
 DBF *df;                      /* the file that actually holds the data */
 DD  *dd;                 /* the data definition struct for the records */
 FLD **field;
 unsigned int n;         /* how many field pointers have been allocated */
 byte  *orec;                                 /* outgoing record buffer */
 size_t orecsz;                          /* outgoing record buffer size */
 DBF *bf;                               /* blob file if there are blobs */
 int tbltype;                               /* what type of table is it */
 FLD *vfield[DDFIELDS];/* the collection of virtual fields in this table */
 char *vfname[DDFIELDS]; /* The name of the virtual fields */
 size_t irecsz;                                 /* incoming record size */
 int nvfield;                /* 1999-03-04 JMT How many vfields we have */
 int *rdd;	              /* reverse field lookup *//* JMT 99-05-20 */
 int prebufsz, postbufsz;    /* Bufspace for {K}DBF *//* JMT 1999-06-04 */
 void *irec, *ivarpos;   /* Incoming rec, var offset*//* JMT 1999-07-21 */
 size_t orecdatasz;      /* outgoing record data sz *//* JMT 1999-11-12 */
 int nnb;/* Not Null Bytes *//* JMT 2001-02-02 */
}
TBL;
#define TBLPN (TBL *)NULL

#define tbldd(tbl)	(tbl)->dd

TBL	*createtbl ARGS((DD *dd, char *tn));
TBL	*opentbl ARGS((TXPMBUF *pmbuf, char *tn));
TBL	*opentbl_dbf ARGS((DBF *dbf, char *tn));
TBL	*closetbl ARGS(( TBL *tb));
int	TXclosetblvirtualfields ARGS((TBL *tb));
int	TXtblReleaseFlds(TBL *tbl);
size_t	TXtblGetRowSize(TBL *tbl);
int	TXtblReleaseRow(TBL *tbl);
TBL *TXcreateinternaltbl(DD *dd, TX_DBF_TYPE dbftype);

/************************************************************************/
#endif /* DBTABLE_H */
/************************************************************************/
