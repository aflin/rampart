int dbfimport ARGS((char *, char *, char *));
int dbfverbose ARGS((int));
int dbftick ARGS((int));

int dbftranslate ARGS((int));
#define DBFNOTRANSLATE	0 /* Leave data alone  */
#define DBFDOSCP2ISOL1	1 /* Convert Dos Code-Page to ISO-Latin 1 */
