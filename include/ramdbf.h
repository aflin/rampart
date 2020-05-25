#ifndef RAMDBF_H
#define RAMDBF_H
/************************************************************************/

#define RDBFB struct rdbf_block
#define RDBFBPN (RDBFB *)NULL
RDBFB
{
 size_t   sz;
 RDBFB *next;
 RDBFB *prev;
#if defined(__hpux) || defined(__DGUX__) || defined(__sgi)
/* For those who require alignment to 8 bytes, but have small pointers */
 void  *dummy;
#endif
};

/************************************************************************/

#define RDBF struct ram_dbf
#define RDBFPN (RDBF *)NULL
RDBF
{
 RDBFB *base;
 RDBFB *end;
 RDBFB *current;
 size_t	size;		/* Total size of ramdbf */
 size_t nblocks;	/* Number of blocks */
 void  *dfover;		/* Controlling DBF structure */
 int	overlimit;
 size_t ramsizelimit;
 size_t	ramblocklimit;
 char   *name;          /* (opt.) human-readable name, as filename */
};

RDBF * closerdbf ARGS((RDBF *df));
RDBF * openrdbf  ARGS((void));
int    freerdbf  ARGS((RDBF *df, EPI_OFF_T at));
EPI_OFF_T  rdbfalloc ARGS((RDBF *df,void *buf, size_t n));
EPI_OFF_T  putrdbf   ARGS((RDBF *df, EPI_OFF_T at, void *buf, size_t sz));
void * getrdbf   ARGS((RDBF *df, EPI_OFF_T at, size_t *psz));
void * agetrdbf  ARGS((RDBF *df, EPI_OFF_T at, size_t *psz));
size_t readrdbf  ARGS((RDBF *df, EPI_OFF_T at, size_t *off, void *buf, size_t sz));
EPI_OFF_T  tellrdbf  ARGS((RDBF *df));
char  *getrdbffn ARGS((RDBF *df));
int    getrdbffh ARGS((RDBF *df));
void   setrdbfoveralloc ARGS((RDBF *df,int div));
int    validrdbf ARGS((RDBF *df, EPI_OFF_T at));
int    ioctlrdbf ARGS((RDBF *df, int ioctl, void *data));
/************************************************************************/
#endif                                                    /* RAMDBF_H */
