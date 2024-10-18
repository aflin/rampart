#ifndef PLIST_H
#define PLIST_H


/**********************************************************************/
#define PLIST struct plist
#define PLISTPN (PLIST *)NULL
PLIST {
   void **s;
   int  cnt;
   int  max;
};
/**********************************************************************/
#define plcnt(a) ((a)->cnt)
#define plarr(a) ((a)->s)

PLIST *plopen    ARGS((void));
PLIST *plclose   ARGS((PLIST *));
void   plwipe    ARGS((PLIST *));
void  *pladd     ARGS((PLIST *,void *));
void  *pladdclst ARGS((PLIST *,int,void **));
void  *pldel     ARGS((PLIST *,void *));
/**********************************************************************/
#endif                                                     /* PLIST_H */
