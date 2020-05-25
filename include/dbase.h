#define MDBF_DBASEIII	1
#define MDBF_DBASEIV	2
#define MDBF_FOXPRO	3

/******************************************************************/

DBF	*openmemofile ARGS((char *, int mode));
DBF	*opendbasefile ARGS((char *));

