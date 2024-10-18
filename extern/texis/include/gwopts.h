#ifndef GWOPTS_H
#define GWOPTS_H


/**********************************************************************/
extern int saveflag ARGS((char *name));
extern int saveint  ARGS((char *name,long val));
extern int savestr  ARGS((char *name,char *val));
extern int saveslst ARGS((char *name,SLIST *lst,int rev));
extern void  setgwoptts      ARGS((TSQL *ts));
extern void  setgwoptprofile ARGS((char *profile));
extern TSQL *getgwoptts      ARGS((void));
extern char *getgwoptprofile ARGS((void));
/**********************************************************************/
#endif                                                    /* GWOPTS_H */
