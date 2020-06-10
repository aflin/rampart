#ifndef TDBINDEX_H
#define TDBINDEX_H

#define TX_TEXTSEARCHMODESTR_SZ		14
/* WAG at max text needed for Metamorph SYSINDEX.PARAMS text: */
#define MM_INDEX_PARAMS_TEXT_MAXSZ	\
	(TX_TEXTSEARCHMODESTR_SZ + TX_TXCFFTOSTR_MAXSZ + 40)

int	TXcreateAndWriteDbiParamsTable ARGS((CONST char *tblPath, A3DBI *dbi));
A3DBI	*TX3dbiOpen ARGS((int type));
A3DBI	*TXcreate3dbiForIndexUpdate ARGS((char *name, A3DBI *srcDbi,
                                          int flags));
int	TXtextParamsTo3dbi(A3DBI *dbi, const char *buf,
                           const char *indexPath, int flags);
size_t	TX3dbiParamsToText ARGS((char *buf, size_t bufSz, A3DBI *dbi));
int	TX3dbiScoreIndex(int indexType, const char *sysindexParams,
                         const int *ddicOptions,
                         const char *indexPath, QNODE_OP op);
A3DBI	*open3dbi ARGS((char *name, int mode, int type,
                        CONST char *sysindexParams));
A3DBI	*close3dbi ARGS((A3DBI *));
BTREE	*TXset3dbi ARGS((A3DBI *dbi, FLD *infld, char *fname, DBTBL *dbtbl,
                         int nopre, EPI_HUGEUINT *nhits, int *nopost,
                         short *import, int op));
BTREE	*setr3dbi ARGS((A3DBI *dbi, FLD *infld, char *fname, DBTBL *dbtbl,
                        EPI_HUGEUINT *nhits));
BTREE	*setp3dbi ARGS((A3DBI *dbi, FLD *infld, char *fname, DBTBL *dbtbl,
                        EPI_HUGEUINT *nhits));
BTREE	*setp3dbi2 ARGS((A3DBI *dbi, FLD *infld, char *fname, DBTBL *dbtbl,
                        EPI_HUGEUINT *nhits));
int	setf3dbi (DBI_SEARCH *dbi_search);
RECID	*put3dbi ARGS((A3DBI *, BTLOC *));
RECID	*put3dbiu ARGS((A3DBI *, BTLOC *, BTLOC *));
RECID	*addto3dbi ARGS((DBTBL *dbtbl, A3DBI *dbi, BTLOC *loc));
int     delfromnew3dbi ARGS((DBTBL *dbtbl, A3DBI *dbi, BTLOC *loc));
int     addtodel3dbi ARGS((DBTBL *dbtbl, A3DBI *dbi, BTLOC *loc));/*KNG000217*/
int	_openupd3dbi ARGS((A3DBI *));

int     delfromfdbi ARGS((DBTBL *dbtbl, FDBI *fi, BTLOC *loc));
int     addtofdbi ARGS((DBTBL *dbtbl, FDBI *fi, BTLOC *loc));
int     TXfdbiChangeLoc ARGS((DBTBL *dbtbl, FDBI *fdbi, BTLOC newLoc));

#endif /* TDBINDEX_H */
