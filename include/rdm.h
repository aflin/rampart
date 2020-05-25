#ifndef RDM_H
#define RDM_H
/**********************************************************************/
#define RDMMODE  "r"
#define RDMBUFSZ 1024
#define RDMEOF   (-1)
#define RDMERR   (-2)
#define RDMNON   (-3)
#define RDMPN    (RDM *)NULL
#define RDM struct rdm
RDM {
   FILE *fp;
   char buf[RDMBUFSZ];
};
/**********************************************************************/
extern RDM  *openrdm  ARGS((char *fn));
extern RDM  *closerdm ARGS((RDM *rm));
extern int   rdmsg    ARGS((RDM *rm,char **text));
extern int   rddatam  ARGS((RDM *rm,char *buf,int size));
/**********************************************************************/
#endif                                                       /* RDM_H */
