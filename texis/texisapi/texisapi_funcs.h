#ifndef TEXISAPI_FUNCS_H
#define TEXISAPI_FUNCS_H
/************************************************************************/
TEXIS *texis_open(char *database, char *user, char *password);
TEXIS *texis_open_options(void *, void *, void *, char *database, char *user, char *group, char *password);
TEXIS *texis_dup(TEXIS *);
int texis_set(TEXIS *, char *property, char *value);
int texis_prepare(TEXIS *, char *sqlQuery);
int texis_execute(TEXIS *);
int texis_param(TEXIS *, int paramNum, void *data, long *dataLen, int cType, int sqlType);
FLDLST *texis_fetch(TEXIS *, int stringsFrom);
int texis_flush(TEXIS *tx);
int texis_flush_scroll(TEXIS *tx, int nrows);
char **texis_getresultnames(TEXIS *tx);
int *texis_getresultsizes(TEXIS *tx);
int texis_getrowcount(TEXIS *tx);
int	texis_getCountInfo (TEXIS *tx, TXCOUNTINFO *countInfo);
TEXIS *texis_close(TEXIS *);
/************************************************************************/
#endif /* TEXISAPI_FUNCS_H */
