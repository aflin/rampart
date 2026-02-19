#include "txcoreconfig.h"
#include <sys/types.h>
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"
#include "errno.h"
#ifdef EPI_HAVE_UNISTD_H
#include <unistd.h>
#endif
#if defined(MSDOS) || defined(__MINGW32__)
#  define LOWIO
#endif
#include "os.h"
#include "mmsg.h"
/*#define DEBUG 0*/
#include "eqvint.h"

/* WTF avoid dragging in texint.h and (not-yet-create) ntexis.h: */
char   *TXtempnam ARGS((CONST char *dir, CONST char *prefix, CONST char *ext));

#define BIGBACKREF
/**********************************************************************/
#define SRCFILE struct srcfile_struct
#define SRCFILEPN (SRCFILE *)NULL
SRCFILE {
   char    *fn;            /* filename - remember for msging purposes */
   FILE    *fp;                                           /* src file */
   ulong    pline, lline;              /* physical line, logical line */
   char    *buf;                                 /* line input buffer */
   int      bufsz;                                     /* size of buf */
   byte     freebuf;                            /* free the above buf */
};
#define DEFBUFSZ 5192

static SRCFILE *closesf ARGS((SRCFILE *sf));
/**********************************************************************/
static SRCFILE *
closesf(sf)
SRCFILE *sf;
{
   if(sf->fp!=FILEPN) fclose(sf->fp);
   if(sf->fn!=CHARPN) free(sf->fn);
   if(sf->freebuf && sf->buf!=CHARPN) free(sf->buf);
   free(sf);
   return(SRCFILEPN);
}                                                    /* end closesf() */
/**********************************************************************/

static SRCFILE *opensf  ARGS((char *fn,char *buf,int bufsz));
/**********************************************************************/
static SRCFILE *
opensf(fn,buf,bufsz)
char *fn;
char *buf;
int bufsz;
{
SRCFILE *sf;
static CONST char Fn[]="opensf";

   if((sf=(SRCFILE *)calloc(1,sizeof(SRCFILE)))==SRCFILEPN){
      putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
   }else{
      if(bufsz<=0) bufsz=DEFBUFSZ;
      sf->pline=sf->lline=0;
      sf->bufsz=bufsz;
      sf->fp=FILEPN;
      if(buf==CHARPN){
         sf->buf=(char *)malloc(sf->bufsz);
         sf->freebuf=1;
      }else{
         sf->buf=buf;
         sf->freebuf=0;
      }
      if((sf->fn=strdup(fn))==CHARPN ||
         sf->buf==CHARPN
      ){
         putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
         goto zerr;
      }
      if((sf->fp=fopen(fn,"r"))==FILEPN){
         putmsg(MERR+FOE,Fn,"Can't open input %s: %s",fn,sysmsg(errno));
         goto zerr;
      }
   }
   return(sf);
zerr:
   return(closesf(sf));
}                                                     /* end opensf() */
/**********************************************************************/

static int getsf ARGS((SRCFILE *sf));
/**********************************************************************/
static int
getsf(sf)
SRCFILE *sf;
{
int l;
char *b=sf->buf;

   if(fgets(b,sf->bufsz,sf->fp)==CHARPN){
      if(ferror(sf->fp)){
         putmsg(MERR+FRE,CHARPN,"Can't read input:line %ld: %s",
                sf->pline+1,sysmsg(errno));
         return(-1);
      }else return(1);
   }
   sf->lline++;
   sf->pline++;
   l=strlen(b)-1;
   if(b[l]=='\n'){
      for(l--;l>=0 && isspace(b[l]);l--) ;     /* trim trailing space */
      b[++l]='\0';
   }else{
      putmsg(MERR+FRE,CHARPN,"Line too long or missing newline:line %ld",sf->pline);
      return(-1);
   }
   return(0);
}                                                      /* end getsf() */
/**********************************************************************/

/**********************************************************************/
/**********************************************************************/
/**********************************************************************/

#define BREF struct bref_struct
#define BREFPN (BREF *)NULL
BREF {
   int      bufsz;                      /* buffer size for line input */
   ulong    totlines;                 /* total # lines after finished */
   int      level;                               /* backref'ing level */
   int      dump;                                    /* generate dump */
   SALLOC  *sp;                      /* cheaper memory pool for words */
   OTREE   *wrdtr;                              /* tree of uniq words */
   OTREE   *eqtr;                            /* tree of equiv entries */
   EQVX    *eqx;          /* equiv index for writing during tree walk */
   char    *chainfn;       /* MAW 05-28-92 - chainto filename or NULL */
};
#define setbrlevel(b,l) ((b)->level=(l))
#define setbrdump(b,d) ((b)->dump=(d))
/**********************************************************************/

#ifndef min
#  define min(a,b) ((a)<(b)?(a):(b))
#endif

#define WBSZ  10240
#define WBDEC 2048
#define WBMIN 2048

static CONST char * CONST eqsnull = "";
static CONST EQVLST eqlstnil={
  (char **)&eqsnull, (char **)&eqsnull, "",
   EQV_CINVAL,0,0
};

static int wreqvhdr ARGS((FILE *fp,EQVHDR *hdr));
/**********************************************************************/
static int
wreqvhdr(fp,hdr)
FILE *fp;
EQVHDR *hdr;
{
int rc=0;
EQV eq;

   eq.ram=BYTEPN;
   eq.fp=fp;
   hdr->magic=MAGIC21;
   hdr->version=EQVERSION;
   if(FSEEKO(fp,(off_t)0,SEEK_END)!=0               || /* KNG 990426 FSEEKO */
      eqvwritedw(&hdr->chainoff   ,1,&eq,HDRKEY)!=0 ||
      eqvwriteb (&hdr->chainlen   ,1,&eq,HDRKEY)!=0 ||
      eqvwriteb (&hdr->version    ,1,&eq,HDRKEY)!=0 ||
      eqvwritedw(&hdr->magic      ,1,&eq,HDRKEY)!=0 ||
      eqvwritew (&hdr->maxwrdlen  ,1,&eq,HDRKEY)!=0 ||
      eqvwritew (&hdr->maxreclen  ,1,&eq,HDRKEY)!=0 ||
      eqvwritew (&hdr->maxwords   ,1,&eq,HDRKEY)!=0 ||
      eqvwritedw(&hdr->nrecs      ,1,&eq,HDRKEY)!=0 ||
      eqvwritedw(&hdr->dataoff    ,1,&eq,HDRKEY)!=0 ||
      eqvwritedw(&hdr->fixcacheoff,1,&eq,HDRKEY)!=0 ||
      eqvwriteb (&hdr->nfixcache  ,1,&eq,HDRKEY)!=0
   ){
      rc=(-1);
   }
   return(rc);
}                                                   /* end wreqvhdr() */
/**********************************************************************/

static int wreqvndx ARGS((FILE *fp,EQVNDX *ndx,word key));
/**********************************************************************/
#ifdef __stdc__                     /* i really hate ansi, don't you? */
static int
wreqvndx(
FILE *fp,
EQVNDX *ndx,
word key)
#else
static int
wreqvndx(fp,ndx,key)
FILE *fp;
EQVNDX *ndx;
word key;
#endif
{
int rc;
EQV eq;

   eq.ram=BYTEPN;
   eq.fp=fp;
   if(eqvwritedw(&ndx->off,1,&eq,key)!=0 ||
      eqvwriteb (&ndx->len,1,&eq,key)!=0
   ){
      rc=(-1);
   }else{
      rc=0;
   }
   return(rc);
}                                                   /* end wreqvndx() */
/**********************************************************************/

static int spliteqv ARGS((EQVX *eqx,int *n,int level,long j,long k));
/**********************************************************************/
static int
spliteqv(eqx,n,level,j,k)
EQVX *eqx;
int *n, level;
long j, k;
{
int rc;
long i;
off_t   where;
dword recn;
FILE *fp=eqx->ofp;
static EQVNDX ndx;
EQV eq;

   eq.ram=BYTEPN;
   eq.fp=fp;
   if(*n==0 || level>NEQVFIXCALEV || j>k) return(1);
   where=FTELLO(fp);                             /* save our position */
   i=(j+k)>>1;
   recn=(dword)i;
   if(rdeqvndx(&eq,&ndx,i)!=0) goto zerr;
   if(FSEEKO(fp,(off_t)ndx.off,SEEK_SET)!=0 ||
      eqvreadb((void *)eqx->buf,(int)ndx.len,&eq,(word)i)!=0
   ) goto zerr;
   eqx->buf[ndx.len]='\0';
   FSEEKO(fp,where,SEEK_SET);                       /* get back there */
   if(eqvwritedw(&recn,1,&eq,CACHEKEY)!=0    ||
      eqvwritedw(&ndx.off,1,&eq,CACHEKEY)!=0 ||
      eqvwriteb (&ndx.len,1,&eq,CACHEKEY)!=0 ||
      eqvwriteb ((void *)eqx->buf,(int)ndx.len,&eq,CACHEKEY)!=0
   ) goto zerr;
   --*n;
   if((rc=spliteqv(eqx,n,level+1,j,i-1))>=0){                 /* left */
      rc=spliteqv(eqx,n,level+1,i+1,k);                      /* right */
   }
   return(rc);
zerr:
   return(-1);
}                                                   /* end spliteqv() */
/**********************************************************************/

static int wreqvcache ARGS((EQVX *eqx));
/**********************************************************************/
static int
wreqvcache(eqx)
EQVX *eqx;
{
long j, k;
int n=NEQVFIXCACHE, rc;

   eqx->hdr.fixcacheoff=(dword)FTELLO(eqx->ofp);
   j=0;
   k=(long)eqx->hdr.nrecs-1;
   rc=spliteqv(eqx,&n,1,j,k);
   if(rc>=0){
      rc=0;
      eqx->hdr.nfixcache=(byte)(NEQVFIXCACHE-n);
   }
   return(rc);
}                                                 /* end wreqvcache() */
/**********************************************************************/

/**********************************************************************/
int
writeeqvx(eqx,eql)
EQVX *eqx;
EQVLST *eql;
{
int lenw, lena, nwrds;
char *p;
EQVNDX ndx;
EQV eq;

   eq.ram=BYTEPN;
   eq.fp=eqx->datafp;
   if((p=eqvfmti(eql,&lenw,&lena,&nwrds,1))==CHARPN){
      putmsg(MERR+MAE,CHARPN,sysmsg(ENOMEM));
      goto zerr;
   }
   lenw--;                          /* don't count leading logic char */
   lena--;
   eqx->totwrds+=nwrds;
   if(lenw >(int)eqx->hdr.maxwrdlen) eqx->hdr.maxwrdlen=lenw;
   if(lena >(int)eqx->hdr.maxreclen) eqx->hdr.maxreclen=lena;
   if(nwrds>(int)eqx->hdr.maxwords ) eqx->hdr.maxwords =nwrds;
   ndx.off=(dword)FTELLO(eqx->datafp);
   ndx.len=(byte)lenw;
   if(wreqvndx(eqx->ndxfp,&ndx,(word)eqx->hdr.nrecs)!=0 ||
      eqvwriteb((void *)(p+1),lena,&eq,(word)eqx->hdr.nrecs)!=0
   ){
      free(p);
      putmsg(MERR+FWE,CHARPN,"Can't write: %s",sysmsg(errno));
      goto zerr;
   }
   free(p);
   eqx->hdr.nrecs++;
   return(0);
zerr:
   return(-1);
}                                                  /* end writeeqvx() */
/**********************************************************************/

/**********************************************************************/
EQVX *
closeeqvx(eqx)
EQVX *eqx;
{
   if(eqx!=EQVXPN){
      if(eqx->chainfn!=CHARPN) free(eqx->chainfn);
      if(eqx->ofp   !=FILEPN) fclose(eqx->ofp   );
      if(eqx->datafp!=FILEPN) fclose(eqx->datafp);
      if(eqx->ndxfp !=FILEPN) fclose(eqx->ndxfp );
      if(eqx->datafn!=CHARPN){
         unlink(eqx->datafn);
         free(eqx->datafn);
      }
      if(eqx->ndxfn!=CHARPN){
         unlink(eqx->ndxfn);
         free(eqx->ndxfn);
      }
      if(eqx->ofn!=CHARPN){
         if(eqx->rmofn) unlink(eqx->ofn);
         free(eqx->ofn);
      }
      if(eqx->buf   !=CHARPN) free(eqx->buf     );
      free((char *)eqx);
   }
   return(EQVXPN);
}                                                  /* end closeeqvx() */
/**********************************************************************/

static char *allocbuf ARGS((int *,int,int));
/**********************************************************************/
static char *
allocbuf(size,mini,dec)
int *size, mini, dec;
{
char *p = CHARPN;

   for(;*size>=mini && (p=(char *)malloc(*size))==CHARPN;*size-=dec) ;
   return(p);
}                                                   /* end allocbuf() */
/**********************************************************************/

/**********************************************************************/
EQVX *
openeqvx(ofn)
char *ofn;
{
EQVX *eqx;
static CONST char Fn[]="openeqvx";

   if((eqx=(EQVX *)calloc(1,sizeof(EQVX)))==EQVXPN){
      putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
      goto zerr;
   }
   eqx->ofp=eqx->datafp=eqx->ndxfp=FILEPN;
   eqx->chainfn=CHARPN;
   eqx->totwrds=0;
   eqx->rmofn=1;
   eqx->hdr.maxwrdlen=0;
   eqx->hdr.maxreclen=0;
   eqx->hdr.maxwords=0;
   eqx->hdr.nrecs=0;
   eqx->bufsz=WBSZ;
   eqx->buf=allocbuf(&eqx->bufsz,WBMIN,WBDEC);
   eqx->ofn=strdup(ofn);
   eqx->ndxfn=TXtempnam(CHARPN, CHARPN, ".eqx");
   if(eqx->ndxfn!=CHARPN) eqx->ndxfp=fopen(eqx->ndxfn,"w+b");
   eqx->datafn=TXtempnam(CHARPN, CHARPN, ".eqx");
   if(eqx->buf==CHARPN ||
      eqx->ofn==CHARPN ||
      eqx->ndxfn==CHARPN ||
      eqx->datafn==CHARPN
   ){
      putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
      goto zerr;
   }
   if(eqx->ndxfp==FILEPN){
      putmsg(MERR+FOE,Fn,"Can't open tmp file: %s: %s",eqx->ndxfn,sysmsg(errno));
      goto zerr;
   }
   if((eqx->datafp=fopen(eqx->datafn,"w+b"))==FILEPN){
      putmsg(MERR+FOE,Fn,"Can't open tmp file: %s: %s",eqx->datafn,sysmsg(errno));
      goto zerr;
   }
   if((eqx->ofp=fopen(ofn,"w+b"))==FILEPN){
      putmsg(MERR+FOE,Fn,"Can't open output: %s: %s",eqx->ofn,sysmsg(errno));
      goto zerr;
   }
   return(eqx);
zerr:
   return(closeeqvx(eqx));
}                                                   /* end openeqvx() */
/**********************************************************************/

/**********************************************************************/
int
finisheqvx(eqx)
EQVX *eqx;
{
int l;
long li, dsz;
EQVNDX ndx;
static CONST char cant_write[]="Can't write %s: %s";
static CONST char cant_read[]="Can't read %s: %s";
EQV eq;

   eq.ram=BYTEPN;
   eq.fp=eqx->ndxfp;
   putmsg(MINFO,CHARPN,"%lu records",(ulong)eqx->hdr.nrecs);
                  /* write one extra index record for alorithmic ease */
   ndx.off=(dword)FTELLO(eqx->datafp);
   ndx.len=0;
   if(wreqvndx(eqx->ndxfp,&ndx,(word)eqx->hdr.nrecs)!=0){
      putmsg(MERR+FWE,CHARPN,cant_write,eqx->ndxfn,sysmsg(errno));
      goto zerr;
   }
                      /* combine index and data files to final output */
             /* rewrite index first adjusting data offsets past index */
   putmsg(MINFO,CHARPN,"Writing index...");
   eqx->hdr.dataoff=(dword)FTELLO(eqx->ndxfp);
   rewind(eqx->ndxfp);
   for(li=0;li<=(long)eqx->hdr.nrecs;li++){
      if(rdeqvndx(&eq,&ndx,li)!=0){
         putmsg(MERR+FRE,CHARPN,cant_read,eqx->ndxfn,sysmsg(errno));
         goto zerr;
      }
      ndx.off+=eqx->hdr.dataoff;
      if(wreqvndx(eqx->ofp,&ndx,(word)li)!=0) goto zwerr;
   }
                                         /* raw copy data after index */
   dsz=FTELLO(eqx->datafp);
   rewind(eqx->datafp);
   putmsg(MINFO,CHARPN,"Writing data (%ldk)...",dsz/1024+1);
   while((l=fread(eqx->buf,1,eqx->bufsz,eqx->datafp))>0){
      if(fwrite(eqx->buf,1,l,eqx->ofp)!=l) goto zwerr;
   }
   if(ferror(eqx->datafp)){
      putmsg(MERR+FRE,CHARPN,cant_read,eqx->datafn,sysmsg(errno));
      goto zerr;
   }
                                                 /* write fixed cache */
   putmsg(MINFO,CHARPN,"Writing cache...");
   if(eqx->bufsz<(int)eqx->hdr.maxreclen){
      eqx->bufsz=eqx->hdr.maxreclen;
      if((eqx->buf=(char *)realloc(eqx->buf,eqx->bufsz))==CHARPN){
         putmsg(MERR+MAE,CHARPN,sysmsg(ENOMEM));
         goto zerr;
      }
   }
   if(wreqvcache(eqx)!=0) goto zwerr;
   if(eqx->chainfn!=CHARPN){
      eqx->hdr.chainoff=FTELLO(eqx->ofp);
      l=strlen(eqx->chainfn);
      eqx->hdr.chainlen=(byte)l;
      if(fwrite(eqx->chainfn,1,l,eqx->ofp)!=l) goto zwerr;
   }else{
      eqx->hdr.chainoff=0;
      eqx->hdr.chainlen=0;
   }
                                               /* write header at EOF */
   putmsg(MINFO,CHARPN,"Writing header...");
   if(wreqvhdr(eqx->ofp,&eqx->hdr)!=0) goto zwerr;
#  ifdef DEBUG
      putmsg(MTOTALS,CHARPN,"Maxwrdlen  : %ld",(long)eqx->hdr.maxwrdlen  );
      putmsg(MTOTALS,CHARPN,"Maxreclen  : %ld",(long)eqx->hdr.maxreclen  );
      putmsg(MTOTALS,CHARPN,"Maxwords   : %ld",(long)eqx->hdr.maxwords   );
      putmsg(MTOTALS,CHARPN,"Nrecs      : %ld",(long)eqx->hdr.nrecs      );
      putmsg(MTOTALS,CHARPN,"Dataoff    : %ld",(long)eqx->hdr.dataoff    );
      putmsg(MTOTALS,CHARPN,"Fixcacheoff: %ld",(long)eqx->hdr.fixcacheoff);
      putmsg(MTOTALS,CHARPN,"Nfixcache  : %ld",(long)eqx->hdr.nfixcache  );
      putmsg(MTOTALS,CHARPN,"Chainoff   : %ld",(long)eqx->hdr.chainoff   );
      putmsg(MTOTALS,CHARPN,"Chainlen   : %ld",(long)eqx->hdr.chainlen   );
      putmsg(MTOTALS,CHARPN,"Totwords   : %ld",(long)eqx->totwrds        );
#  else
      putmsg(MTOTALS,CHARPN,"Number of entries: %ld",(long)eqx->hdr.nrecs);
      putmsg(MTOTALS,CHARPN,"Number of words  : %ld",(long)eqx->totwrds  );
#  endif
   eqx->rmofn=0;
   return(0);
zwerr:
   putmsg(MERR+FWE,CHARPN,cant_write,eqx->ofn,sysmsg(errno));
zerr:
   return(-1);
}                                                 /* end finisheqvx() */
/**********************************************************************/

static int wrdtrcmp ARGS((void *,void *));
/**********************************************************************/
static int
wrdtrcmp(p1,p2)
void *p1, *p2;
{
char *s1=(char *)p1, *s2=(char *)p2;

   return(eqcmp(s1,s2));
}                                                   /* end wrdtrcmp() */
/**********************************************************************/

static int wrdtradd ARGS((void **p1,void *arg));
/**********************************************************************/
static int
wrdtradd(p1,arg)
void **p1, *arg;
{
char **s=(char **)p1, *p;
SALLOC *sp=(SALLOC *)arg;

   if((p=(char *)salloc(sp,strlen(*s)+1))==CHARPN) return(OTR_ERROR);
   strcpy(p,*s);
   *s=p;
   return(OTR_ADD);
}                                                   /* end wrdtradd() */
/**********************************************************************/

static int eqtrcmp ARGS((void *,void *));
/**********************************************************************/
static int
eqtrcmp(p1,p2)
void *p1, *p2;
{
EQVLST *eql1=(EQVLST *)p1, *eql2=(EQVLST *)p2;

   return(eqcmp(eql1->words[0],eql2->words[0]));
}                                                    /* end eqtrcmp() */
/**********************************************************************/

static int eqtrdup ARGS((void **cur,void *new,void *arg));
/**********************************************************************/
static int
eqtrdup(cur,new,argunused)
void **cur, *new, *argunused;
{
EQVLST *eql= *(EQVLST **)cur, *e=(EQVLST *)new;
char **words, **clas , *op;
int i;

   words=e->words;
   clas =e->clas ;
   op   =e->op;
   for(i=1;*words[i]!='\0';i++){                       /* merge lists */
      switch(op[i]){
      case EQV_CREPL: clreqvlst(eql); break;
      case EQV_CDEL : rmeqvlst(eql,words[i],clas [i]); break;
      /* MAW 10-27-92 - don't store dup words in list, just keep last */
      default       : rmeqvlst(eql,words[i],clas [i]); break;/* MAW 10-27-92 */
      }
      if(addeqvlst(eql,words[i],clas [i],op[i])<0) return(OTR_ERROR);
   }
   return(OTR_IGNORE);
}                                                    /* end eqtrdup() */
/**********************************************************************/

static int eqtrfre ARGS((void *,void *));
/**********************************************************************/
static int
eqtrfre(cur,argunused)
void *cur, *argunused;
{
EQVLST *eql=(EQVLST *)cur;

   closeeqvlst(eql);
   return(1);
}                                                    /* end eqtrfre() */
/**********************************************************************/

/**********************************************************************/
/* debugging function, not used in normal program exection.  Useful as
 * dumpAndWalkTree's wfunc */
static int
printEqvlstWords(cur,arg)
void *cur, *arg;
{
  EQVLST *eql=(EQVLST *)cur;
  int i;
  char *str;
  for(i=0; i<eql->used; i++) {
    str = *(eql->words+i);
    if(strcmp("", str) == 0) {
      break;
    }
    if(i!=0) {
      fputs(",", stdout);
    }
    fputs(str, stdout);
  }
  fputs("\n", stdout);
  return 1;
}

static int addwords ARGS((OTREE *tr,EQVLST *eql));
/**********************************************************************/
static int
addwords(tr,eql)
OTREE *tr;
EQVLST *eql;
{
int i, rc;
char **words, **clas , *p;
static CONST char Fn[]="addwords";

   words=eql->words;
   clas =eql->clas ;
   for(i=0;*words[i]!='\0';i++){
      rc=addotree2(tr,words[i],(void **)(void *)&p);
      if(rc<0) goto zerr;
      words[i]=p;
      rc=addotree2(tr,clas [i],(void **)(void *)&p);
      if(rc<0) goto zerr;
      clas [i]=p;
   }
   return(0);
zerr:
   putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
   return(-1);
}                                                   /* end addwords() */
/**********************************************************************/

static BREF *closebref ARGS((BREF *));
/**********************************************************************/
static BREF *
closebref(br)
BREF *br;
{
   if(br->chainfn!=CHARPN) free(br->chainfn);
   if(br->eqtr!=OTREEPN){
      setotwfunc(br->eqtr,eqtrfre);
      walkotree(br->eqtr);
      closeotree(br->eqtr);
   }
   if(br->wrdtr!=OTREEPN){
      closeotree(br->wrdtr);
   }
   if(br->sp!=SALLOCPN){
      closesalloc(br->sp);
   }
   free((char *)br);
   return(BREFPN);
}                                                  /* end closebref() */
/**********************************************************************/

#define DEFSALLOCSZ 10240

static BREF *openbref ARGS((int bufsz));
/**********************************************************************/
static BREF *
openbref(bufsz)
int bufsz;
{
BREF *br;
static CONST char Fn[]="openbr";

   br=(BREF *)calloc(1,sizeof(BREF));
   if(br==BREFPN){
      putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
   }else{
      br->chainfn=CHARPN;
      br->bufsz=bufsz;
      br->totlines=0;
      br->level=BREF_LDEFAULT;
      br->sp=opensalloc(DEFSALLOCSZ);
      br->wrdtr=openotree(wrdtrcmp,"");
      br->eqtr=openotree(eqtrcmp, (EQVLST *)&eqlstnil);
      if(br->sp==SALLOCPN ||
         br->wrdtr==OTREEPN ||
         br->eqtr==OTREEPN
      ){
         putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
         goto zerr;
      }
      setotdups(br->wrdtr,0);
      setotafunc(br->wrdtr,wrdtradd);
      setotaarg(br->wrdtr,br->sp);
      setotdfunc(br->eqtr,eqtrdup);
   }
   return(br);
zerr:
   return(closebref(br));
}                                                   /* end openbref() */
/**********************************************************************/

static int eqtrprt ARGS((void *cur,void *arg));
/**********************************************************************/
static int
eqtrprt(cur,arg)
void *cur, *arg;
{
EQVLST *eql=(EQVLST *)cur;
BREF *br=(BREF *)arg;
static CONST char Fn[]="eqtrprt";
int     res;

   if(writeeqvx(br->eqx,eql)!=0) return(0);                  /* error */
   if(br->dump){
     res = eqvsfmt(eql, stdout);
     fflush(stdout);
     if (res < 0)
       {
         putmsg(MERR+FWE,Fn,sysmsg(errno));
         return(0);                                          /* error */
       }
   }
   return(1);                                                   /* ok */
}                                                    /* end eqtrprt() */
/**********************************************************************/

static int backref1 ARGS((BREF *br,EQVLST *eql));
/**********************************************************************/
static int
backref1(br,eql)       /* add each eq as root w/root as eq to eq tree */
BREF *br;
EQVLST *eql;
{
int rc, i;
EQVLST *teql=EQVLSTPN;
char **words, **clas , *op, *p;

                   /* MAW 09-04-92 - don't  backref "replace"s at all */
   if(*eql->words[1]!='\0' && eql->op[1]!=EQV_CREPL){
      words=eql->words;
      clas =eql->clas ;
      op=eql->op;
      for(i=1;*words[i]!='\0';i++){
         if(op[i]!=EQV_CADD) continue;
         if((teql=openeqvlst(2))==EQVLSTPN) goto zerr;
                 /* boy we're drill'n now! - move the terminator over */
         teql->words[teql->used+2-1]=teql->words[teql->used-1];
         teql->clas [teql->used+2-1]=teql->clas [teql->used-1];
         teql->op[teql->used+2-1]=teql->op[teql->used-1];
         teql->used+=2;
         if(*words[i]==EQV_CSEE){                 /* back ref see ref */
            teql->words[0]=words[i]+1;
            if((teql->words[1]=(char *)malloc(strlen(words[0])+2))==CHARPN) goto zerr;
            *teql->words[1]=EQV_CSEE;
            strcpy(teql->words[1]+1,words[0]);
            rc=addotree2(br->wrdtr,teql->words[1],(void **)(void *)&p);
            free(teql->words[1]);
            teql->words[1]=p;
            if(rc<0) goto zerr;
         }else{
            teql->words[0]=words[i];
            teql->words[1]=words[0];
         }
         teql->clas [0]=clas [i];
         teql->clas [1]=clas [i];
         teql->op[1]=EQV_CADD;
         if((rc=addotree(br->eqtr,teql))<0 || rc==0){ /* error or dup */
            teql=closeeqvlst(teql);
            if(rc<0) goto zerr;
         }
      }
   }
   return(0);
zerr:
   if(teql!=EQVLSTPN) closeeqvlst(teql);
   return(-1);
}                                                   /* end backref1() */
/**********************************************************************/

static void rmother ARGS((EQVLST *eql));
/**********************************************************************/
static void
rmother(eql)                          /* remove words with non-add op */
EQVLST *eql;
{
int s, d;
char **w, **c, *o;

   w=eql->words;
   c=eql->clas ;
   o=eql->op;
   for(s=d=1;*w[s]!='\0';s++){
      if(o[s]==EQV_CADD){
         w[d]=w[s];
         c[d]=c[s];
         o[d]=o[s];
         d++;
      }
   }
   w[d]=w[s];
   c[d]=c[s];
   o[d]=o[s];
   eql->used=d+1;
}                                                    /* end rmother() */
/**********************************************************************/

#ifdef BIGBACKREF
static int backref2 ARGS((BREF *br,EQVLST *eql));
/**********************************************************************/
static int
backref2(br,eql)   /* add every word as root w/every other word as eq */
BREF *br;
EQVLST *eql;
{
char **w, **c, *o;
int i, arc, skip;
EQVLST teql;
static CONST char Fn[]="backref2";

                   /* MAW 09-04-92 - don't  backref "replace"s at all */
   if(*eql->words[1]!='\0' && eql->op[1]!=EQV_CREPL){
      if((eql=dupeqvstru(eql))==EQVLSTPN) return(-1); /* working copy */
      rmother(eql);                   /* remove words with non-add op */
      w=eql->words;
      c=eql->clas ;
      o=eql->op;
      if(*w[1]!='\0'){
         for(i=1;*w[i+1]!='\0';i++){
            if(*w[i]==EQV_CSEE) w[i]+=(skip=1);  /* rm see ref from root */
            else                skip=0;
            teql.words= &w[i];
            teql.clas = &c[i];
            teql.op= &o[i];
            arc=addotree(br->eqtr,&teql); /* add root and eqs to eq tree */
            if(arc>0){
               putmsg(MERR,Fn,"Internal tree error");
               return(-1);
            }
            if(arc<0 || backref1(br,&teql)!=0){
               w[i]-=skip;
               closeeqvlst(eql);
               return(-1);
            }
            w[i]-=skip;
		   }
      }
      closeeqvlst(eql);
   }
   return(0);
}                                                   /* end backref2() */
/**********************************************************************/
#endif                                                  /* BIGBACKREF */

static int eqbref ARGS((void *cur,void *arg));
/**********************************************************************/
static int
eqbref(cur,arg)
void *cur, *arg;
{
EQVLST *eql=(EQVLST *)cur;
BREF *br=(BREF *)arg;

#ifdef BIGBACKREF
   if(backref2(br,eql)!=0){                                  /* error */
      return(0);
   }
   return(1);                                                   /* ok */
#else
   if(backref1(br,eql)!=0){                                  /* error */
      return(0);
   }
   return(1);                                                   /* ok */
#endif
}                                                     /* end eqbref() */
/**********************************************************************/

static int ndxbreq ARGS((BREF *br,char *ofn));
/**********************************************************************/
static int
ndxbreq(br,ofn)                      /* write binary indexed eqv file */
BREF *br;
char *ofn;
{
int rc=(-1);
static CONST char Fn[]="ndxbreq";

   if(br->level>BREF_LBACKREF){                    /* 2nd backref pass*/
      /* don't allow rebalancing the tree while we iterate over it */
      br->eqtr->allowTreeMods=0;
      setotwfunc(br->eqtr,eqbref);
      setotwarg(br->eqtr,br);
      if(walkotree(br->eqtr)==0){                            /* error */
         br->eqtr->allowTreeMods=1;
         putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
         return(-1);
      }
      br->eqtr->allowTreeMods=1;
   }
   if((br->eqx=openeqvx(ofn))!=EQVXPN){
      setotwfunc(br->eqtr,eqtrprt);
      setotwarg(br->eqtr,br);
      if(walkotree(br->eqtr)!=0){
         br->eqx->chainfn=br->chainfn;
         rc=finisheqvx(br->eqx);
         br->eqx->chainfn=CHARPN;
      }
      closeeqvx(br->eqx);
   }
   if(rc!=0) unlink(ofn);
   return(rc);
}                                                    /* end ndxbreq() */

/**********************************************************************/

static int addeqs ARGS((BREF *br,EQVLST *eql));
/**********************************************************************/
static int
addeqs(br,eql)
BREF *br;
EQVLST *eql;
{
int arc;
static CONST char Fn[]="addeqs";

   if((arc=addotree(br->eqtr,eql))<0){ /* add root and eqs to eq tree */
      arc=0;
      goto zerr;
   }
   if(br->level>BREF_LINDEX){
      if(backref1(br,eql)!=0) goto zerr;
   }
   if(arc==0) closeeqvlst(eql);                       /* dup, free it */
   return(0);
zerr:
   putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
   if(arc==0) closeeqvlst(eql);                        /* dup, free it */
   return(-1);
}                                                     /* end addeqs() */
/**********************************************************************/

static int runbackref ARGS((BREF *br,SRCFILE *sf));
/**********************************************************************/
static int
runbackref(br,sf)
BREF *br;
SRCFILE *sf;
{
EQVLST *eql=EQVLSTPN;
int grc;
char *line;
static CONST char tag[]=";chain;";
int taglen=sizeof(tag)-1;
static CONST char Fn[]="runbackref";

   while((grc=getsf(sf))==0){
      for(line=sf->buf;isspace(*line);line++) ; /* trim leading space */
      if(sf->lline==1){
#ifdef EQV_CHAIN_SOURCE
         if(strncmp(line,tag,taglen)==0){
         SRCFILE *csf;

            if((csf=opensf(line+taglen,sf->buf,sf->bufsz))==SRCFILEPN){
               putmsg(MWARN,CHARPN,"Chain to \"%s\" ignored",line+taglen);
            }else{
               putmsg(MINFO,CHARPN,"Chained to %s",line+taglen);
               grc=runbackref(br,csf);
               closesf(csf);
               if(grc<0) goto zerr;
            }
            *line='\0';
         }
         putmsg(MFILEINFO,CHARPN,"Reading %s",sf->fn);
#else                                            /* !EQV_CHAIN_SOURCE */
         putmsg(MFILEINFO,CHARPN,"Reading %s",sf->fn);
         if(strncmp(line,tag,taglen)==0){
            if((br->chainfn=strdup(line+taglen))==CHARPN){
               putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
               goto zerr;
            }
            putmsg(MINFO,CHARPN,"Chained to %s",line+taglen);
            *line='\0';
         }
#endif                                            /* EQV_CHAIN_SOURCE */
      }
      if(*line!='\0'){
         if((eql=eqvparse(line,1))==EQVLSTPN){
            putmsg(MERR,CHARPN,"Error in line %ld: \"%s\"",sf->pline,line);
            goto zerr;
         }
         if(br->level>BREF_LSYNTAX){
            if(addwords(br->wrdtr,eql)!=0){
               closeeqvlst(eql);
               goto zerr;
            }
            if(addeqs(br,eql)!=0) goto zerr;
         }else closeeqvlst(eql);
      }
   }
   if(grc<0) goto zerr;
   br->totlines+=sf->lline;
   return(0);
zerr:
   return(-1);
}                                                 /* end runbackref() */
/**********************************************************************/

static int backref ARGS((BREF *br,char *fn));
/**********************************************************************/
static int
backref(br,fn)
BREF *br;
char *fn;
{
SRCFILE *sf;
int rc;

   if((sf=opensf(fn,CHARPN,br->bufsz))==SRCFILEPN) rc=(-1);
   else{
      rc=runbackref(br,sf);
      closesf(sf);
   }
   return(rc);
}                                                    /* end backref() */
/**********************************************************************/

/**********************************************************************/
#ifdef DEBUG
#ifdef unix
extern end;
static long space1, space2;
#define MEM1() {space1=sbrk(0)-(long)&end;}
#define MEM2() {\
   space2=(sbrk(0)-(long)&end)-space1; \
   putmsg(999,CHARPN,"dynamic data space: %ld",space2); }
#endif                                                        /* unix */

#ifdef MSDOS
extern size_t CDECL _memavl(void); /*#include <malloc.h>*/
#include <dos.h>
static uint nmem;
static ulong dmem, tmem1, tmem2;
#define MEM1() { \
   _dos_allocmem((unsigned)65535,&nmem); \
   dmem=(long)nmem*16L; \
   nmem=_memavl(); \
   tmem1=dmem+(ulong)nmem;}
#define MEM2() { \
   _dos_allocmem((unsigned)65535,&nmem);\
   dmem=(long)nmem*16L;\
   nmem=_memavl();\
   tmem2=dmem+(ulong)nmem;\
   putmsg(999,CHARPN,\
          "Memory available: DOS==%lu, Near=%u, Total=%lu, Used=%lu",\
          dmem,nmem,tmem2,tmem1-tmem2); }
#endif                                                       /* MSDOS */
#else                                                       /* !DEBUG */
#  define MEM1()
#  define MEM2()
#endif                                                       /* DEBUG */
/**********************************************************************/
int
eqvmkndx(src,bin,level,bufsz,dump)
char *src, *bin;
int level, bufsz, dump;
{
TXEXIT rc=TXEXIT_UNKNOWNERROR;
BREF *br;
FILE *fp=FILEPN;

   if(level>BREF_LSYNTAX && bin!=CHARPN &&
      (fp=fopen(bin,"w+b"))==FILEPN){           /* make sure writable */
      putmsg(MERR+FOE,CHARPN,"Cannot open output file: %s: %s",
             bin,sysmsg(errno));
      rc = TXEXIT_CANNOTOPENOUTPUTFILE;
   }else{
      if(fp!=FILEPN) fclose(fp);
      if((br=openbref(bufsz))!=BREFPN){
         MEM1();
         setbrlevel(br,level);
         setbrdump(br,dump);
         if(backref(br,src)==0){
            MEM2();
            if(level>BREF_LSYNTAX){
               putmsg(MTOTALS,CHARPN,"%lu Lines, %lu Words, %lu Entries",
                      br->totlines,getotcnt(br->wrdtr),getotcnt(br->eqtr));
               putmsg(MTOTALS,CHARPN,"%lu Dup words, %lu Dup entries",
                      getotdupcnt(br->wrdtr),getotdupcnt(br->eqtr));
               putmsg(MFILEINFO,CHARPN,"Writing %s",bin);
                        /* do pass2 backref & write binary file/index */
               rc = (ndxbreq(br,bin) != 0 ? TXEXIT_UNKNOWNERROR : TXEXIT_OK);
            }else{
               putmsg(MTOTALS,CHARPN,"%lu Lines",br->totlines);
               rc=TXEXIT_OK;
            }
         }
         closebref(br);
         MEM2();
      }
      if(rc!=TXEXIT_OK && level>BREF_LSYNTAX && bin!=CHARPN){
         unlink(bin);
      }
   }
   return((int)rc);                             /* TXEXIT_... code */
}                                                   /* end eqvmkndx() */
/**********************************************************************/
