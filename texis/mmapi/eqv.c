/* wtf - implement alphrase? */
/***********************************************************************
** eqv.c - new equiv lookup routines - MAW 02-12-92
** MAW 11-30-92 - don't do eqprep() if no equiv file
***********************************************************************/
/*
   MAW 02-05-99 - add builtin ram main equiv
   MAW 06-01-98 - add query permissions
   MAW 11-28-95 - added the ability to use , in an equiv/qry
                  e.g. "willson, mark"
                  you can't look it up as a root in eqv file tho, because its
                  escaped in the file and strcmp("b, a","b\, a")!=0
                  i'm not sure how to handle this speedily
                  also can't do ("a, b","c, d")
*/
#include "txcoreconfig.h"
#include <sys/types.h>
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"
#include "errno.h"
#ifdef MSDOS
#  define LOWIO
#endif
#include "os.h"
#include "unicode.h"
#include "mmsg.h"
#ifndef WORDSONLY
#  include "presuf.h"
#  include "mmprep.h"
#endif
/*#define DEBUG 0*/
#include "eqvint.h"
#ifndef NO_RAM_EQUIVS
extern CONST byte equivs[];                                 /* MAW 02-02-99 */
extern CONST int nequivs;                                   /* MAW 02-02-99 */
#endif
/*#define LOOKBACKWARDS*/
#define RMNOISEEQVS

#ifndef max
#  define max(a,b) ((a)>(b)?(a):(b))
#endif
#ifdef VMS
#define BINARYREADMODE "rb","ctx=bin","ctx=stm"
#else
#define BINARYREADMODE "rb"
#endif

#ifdef TEST
   int cahit, camiss;
#  define TST(a) a
   static void prteql ARGS((EQVLST *eql,int eqfmt));
#else
#  define TST(a)
#endif

static CONST char Copyright[]=
 "@(#)Equiv Access 2 Copyright (C) Thunderstone EPI 1992,1998 - By Mark Willson";

#ifdef KRYPTONITE                                /* use en/decryption */
/***********************************************************************
**
** @(#)from mmcrypt.c - en/decryption function - PBR 9/88
** MAW 01-21-91 - renamed mmcrypt() to strweld() for obscurity
**
***********************************************************************/
void
strweld(byte *buf, int n, word seed)
{
static byte mmckey1[145]={
  35, 174, 132, 225, 108, 198, 174,  66, 128,  73, 225, 225, 171, 233, 235,
 163, 166, 203,  44, 135,  12,  46, 137,  36,  78,  13,  12,   6, 167,  71,
 206, 163,  64, 238,  64, 104,  38, 237,   2,  73,  34, 230, 142, 109,  73,
 204, 173,  79,   4, 226,  68,  64, 102, 192, 107, 196,  32, 167,  34,  43,
 161,  34, 230,  34, 129, 141, 225, 139,  15, 202, 160, 202, 137,   2, 169,
  98, 141,  73,  44, 128, 110, 197, 137, 197, 233, 128, 162, 234, 201, 204,
  67, 175, 103, 198, 175,   4, 198, 110,  45, 204, 142, 102, 131, 239,  71,
  73,  97, 239, 105, 143,  97, 205, 164,  73, 110,  37, 207,  34,  34, 228,
 204, 207, 195, 128,  45,  72, 195, 143, 101, 230, 201,  13,  42, 229, 192,
 231,  43, 104, 129, 135,  68,  14,  79,  64, 0
};

static byte mmckey2[143]={
 196,  97, 141, 174, 107,   5,   5,   7,  43,  35, 130,  15,   8,  96, 130,
 202, 100,  68, 206, 161, 133,  46, 105,   5, 232,  70, 106,   4, 134,  99,
  14, 201,   6,  47, 103, 104, 196, 231,  74,  74, 192,  71, 104, 102, 234,
   6, 171,   1, 173, 174,  36, 136, 105, 238,  66, 203,  37,  67, 229,  44,
 228,  69, 195, 200,  40, 206,  11, 229, 197,  96,  73,  45, 135,  39, 138,
  73, 102,  45, 192, 194, 201, 205, 104, 196,  73, 106, 105,  37,   8,  97,
  64,   4, 161,  43, 106, 165,   1,  40, 193, 140, 198, 169,  11, 135, 135,
 140,  47, 225,   5,  13, 138, 133, 193, 139, 225, 192, 110, 233, 168, 138,
 167, 134, 194, 165,  68, 175, 138, 231, 201,  35, 237, 168,  79, 104, 230,
  98, 107,   4, 137, 205, 195,  13, 0
};

byte *ck1=NULL;
byte *ck2;
byte *end;

#if !(defined(__sgi) && defined(EPI_OS_MAJOR) && EPI_OS_MAJOR == 6)
/* KNG 021112 gcc Irix 6 thinks this is true, and segfaults in strcpy(): */
   if(&n==(int *)mmckey2){/* MAW 01-21-91 - unreached code for obscurity */
      for(ck2=buf;*ck2!='\0';ck2++) ;
      ck1=(byte *)strcpy((char *)ck1+(int)(ck2-buf+1),(char *)buf);
      end=ck1+(int)(ck2-buf);
      for(;ck1<end;ck1++,buf++) *ck1= *buf;
      buf=ck2-17;
   }
#endif
   seed&=0xffff;                             /* limit seed to 16 bits */
   seed%=140;                             /* initial index into mckey */
   for(ck1=mmckey1+seed,ck2=mmckey2+seed,end=buf+n;buf<end;buf++){
      if(*buf & 0x10){
         if(*ck1==(byte)'\0') ck1=mmckey1;
         *buf^= *ck1++;
      }else{
         if(*ck2==(byte)'\0') ck2=mmckey2;
         *buf^= *ck2++;
      }
   }
}                                                    /* end strweld() */
/**********************************************************************/
#endif                                                  /* KRYPTONITE */

static void strshl ARGS((char *s));
/**********************************************************************/
static void
strshl(d)
char *d;
{
char *s;

   for(s=d+1;*s!='\0';s++,d++) *d= *s;
   *d='\0';
}                                                     /* end strshl() */
/**********************************************************************/

int eqvseek ARGS((EQV *,off_t,int));            /* KNG 990426 use off_t */
/**********************************************************************/
int eqvseek(eq,off,whence)                            /* MAW 02-02-99 */
EQV *eq;
off_t off;
int whence;
{
   if(eq->ram==BYTEPN)
      return(FSEEKO(eq->fp,off,whence));        /* KNG 990426 use FSEEKO */
   switch(whence)
   {
    case SEEK_CUR: eq->ramptr+=off; break;
    case SEEK_END: eq->ramptr=eq->ram+eq->ramsz+off; break;
    default:
    case SEEK_SET: eq->ramptr=eq->ram+off; break;
   }
   return(0);
}
/**********************************************************************/

/**********************************************************************/
#ifdef __stdc__
int
eqvwriteb(
byte *val,
int n,
EQV* eq,
word key)                 /* write 8 bit val to fp encrypted with key */
#else
int
eqvwriteb(val,n,eq,key)   /* write 8 bit val to fp encrypted with key */
byte *val;
int n;
EQV* eq;
word key;
#endif
{
int rc;

   mmcrypt(val,n,key);
   if(fwrite((char *)val,1,n,eq->fp)!=n) rc=(-1);
   else                                  rc=0;
   mmcrypt(val,n,key);                   /* leave callers data intact */
   return(rc);
}                                                  /* end eqvwriteb() */
/**********************************************************************/

/**********************************************************************/
#ifdef __stdc__
int
eqvreadb(
byte *val,
int n,
EQV *eq,
word key)                /* read 8 bit val from fp ecnrypted with key */
#else
int
eqvreadb(val,n,eq,key)   /* read 8 bit val from fp ecnrypted with key */
byte *val;
int n;
EQV *eq;
word key;
#endif
{
   if(eq->ram==BYTEPN)
   {
      if(fread((char *)val,1,n,eq->fp)!=n) return(-1);
   }
   else
   {
      memcpy((char *)val,eq->ramptr,n);
      eq->ramptr+=n;
   }
   mmcrypt(val,n,key);
   return(0);
}                                                   /* end eqvreadb() */
/**********************************************************************/

/**********************************************************************/
#ifdef __stdc__
int
eqvwritew(
word *val,
int n,
EQV *eq,
word key)                /* write 16 bit val to fp encrypted with key */
#else
int
eqvwritew(val,n,eq,key)  /* write 16 bit val to fp encrypted with key */
word *val;
int n;
EQV *eq;
word key;
#endif
{
int rc;
byte PFbuf[2];

   for(rc=0;rc==0 && n>0;n--,val++){
      PFbuf[0]=(byte)(*val);
      PFbuf[1]=(byte)((*val)>>8);
      mmcrypt(PFbuf,2,key);
      if(fwrite((char *)PFbuf,1,2,eq->fp)!=2) rc=(-1);
      mmcrypt(PFbuf,2,key);              /* leave callers data intact */
   }
   return(rc);
}                                                  /* end eqvwritew() */
/**********************************************************************/

/**********************************************************************/
#ifdef __stdc__
int
eqvreadw(
word *val,
int n,
EQV *eq,
word key)               /* read 16 bit val from fp encrypted with key */
#else
int
eqvreadw(val,n,eq,key)  /* read 16 bit val from fp encrypted with key */
word *val;
int n;
EQV *eq;
word key;
#endif
{
int rc;
byte PFbuf[2];

   for(rc=0;rc==0 && n>0;n--,val++){
      if(eq->ram==BYTEPN)
      {
         if(fread((char *)PFbuf,1,2,eq->fp)!=2) return(-1);
      }
      else
      {
         memcpy((char *)PFbuf,eq->ramptr,2);
         eq->ramptr+=2;
      }
      mmcrypt(PFbuf,2,key);
      *val=((word)PFbuf[0])|(((word)PFbuf[1])<<8);
   }
   return(rc);
}                                                   /* end eqvreadw() */
/**********************************************************************/

/**********************************************************************/
#ifdef __stdc__
int
eqvwritedw(
dword *val,
int n,
EQV *eq,
word key)                /* write 32 bit val to fp encrypted with key */
#else
int
eqvwritedw(val,n,eq,key) /* write 32 bit val to fp encrypted with key */
dword *val;
int n;
EQV *eq;
word key;
#endif
{
int rc;
byte PFbuf[4];

   for(rc=0;rc==0 && n>0;n--,val++){
      PFbuf[0]=(byte)*val        ;
      PFbuf[1]=(byte)((*val)>>8 );
      PFbuf[2]=(byte)((*val)>>16);
      PFbuf[3]=(byte)((*val)>>24);
      mmcrypt(PFbuf,4,key);
      if(fwrite((char *)PFbuf,1,4,eq->fp)!=4) rc=(-1);
      mmcrypt(PFbuf,4,key);              /* leave callers data intact */
   }
   return(rc);
}                                                 /* end eqvwritedw() */
/**********************************************************************/

/**********************************************************************/
#ifdef __stdc__
int
eqvreaddw(
dword *val,
int n,
EQV *eq,
word key)               /* read 32 bit val from fp ecnrypted with key */
#else
int
eqvreaddw(val,n,eq,key) /* read 32 bit val from fp ecnrypted with key */
dword *val;
int n;
EQV *eq;
word key;
#endif
{
byte PFbuf[4];

   for(;n>0;n--,val++){
      if(eq->ram==BYTEPN)
      {
         if(fread((char *)PFbuf,1,4,eq->fp)!=4) return(-1);
      }
      else
      {
         memcpy((char *)PFbuf,eq->ramptr,4);
         eq->ramptr+=4;
      }
      mmcrypt(PFbuf,4,key);
      *val=( (dword)PFbuf[0])     |
           (((dword)PFbuf[1])<<8 )|
           (((dword)PFbuf[2])<<16)|
           (((dword)PFbuf[3])<<24);
   }
   return(0);
}                                                  /* end eqvreaddw() */
/**********************************************************************/

/**********************************************************************/
EQV *
closeeqv(eq)
EQV *eq;
{
int i;

   if(eq!=EQVPN){
      if(eq->myacp && eq->acp!=APICPPN)
         eq->acp=closeapicp(eq->acp);
      if(eq->chain!=EQVPN) closeeqv(eq->chain);
      for(i=0;i<NEQVFIXCACHE && eq->cache[i].buf!=CHARPN;i++){
         free(eq->cache[i].buf);
      }
      for(i=NEQVFIXCACHE;i<NEQVCACHE && eq->cache[i].buf!=CHARPN;i++){
         free(eq->cache[i].buf);
      }
      if(eq->rec.eql!=EQVLSTPN){
         eq->rec.eql->used=0;
         closeeqvlst(eq->rec.eql);
      }
      if(eq->rec.buf      !=CHARPN ) free(eq->rec.buf);
      if(eq->fp           !=FILEPN ) fclose(eq->fp);
      free((char *)eq);
   }
   return(EQVPN);
}                                                    /* end closeqv() */
/**********************************************************************/

static int rdeqvhdr ARGS((EQV *eq));
/**********************************************************************/
static int
rdeqvhdr(eq)
EQV *eq;
{
EQVHDR *hdr= &eq->hdr;
int rc=(-1);

                                                   /* read the header */
   if(eqvseek(eq,(off_t)-HDRSZ,SEEK_END)==0        &&
      eqvreaddw(&hdr->magic      ,1,eq,HDRKEY)==0
   ){
#ifdef EINVAL
      errno=EINVAL;                              /* for magic failure */
#endif
      if((hdr->magic==(dword)MAGICM || hdr->magic==(dword)MAGICU ||
          hdr->magic==(dword)MAGICM21 || hdr->magic==(dword)MAGICU21) &&
         eqvreadw (&hdr->maxwrdlen  ,1,eq,HDRKEY)==0 &&
         eqvreadw (&hdr->maxreclen  ,1,eq,HDRKEY)==0 &&
         eqvreadw (&hdr->maxwords   ,1,eq,HDRKEY)==0 &&
         eqvreaddw(&hdr->nrecs      ,1,eq,HDRKEY)==0 &&
         eqvreaddw(&hdr->dataoff    ,1,eq,HDRKEY)==0 &&
         eqvreaddw(&hdr->fixcacheoff,1,eq,HDRKEY)==0 &&
         eqvreadb (&hdr->nfixcache  ,1,eq,HDRKEY)==0
      ){
         if(hdr->magic==(dword)MAGICM || hdr->magic==(dword)MAGICU){/* old file */
            hdr->chainoff=0;
            hdr->chainlen=0;
            hdr->version=0x20;                                 /* 2.0 */
            rc=0;
         }else if(eqvseek(eq,(off_t)-HDRSZ21,SEEK_END)==0   &&
                  eqvreaddw(&hdr->chainoff,1,eq,HDRKEY)==0 &&
                  eqvreadb (&hdr->chainlen,1,eq,HDRKEY)==0 &&
                  eqvreadb (&hdr->version ,1,eq,HDRKEY)==0
         ){
            rc=0;
         }
      }
   }
   return(rc);
}                                                   /* end rdeqvhdr() */
/**********************************************************************/

static int rdeqvcache ARGS((EQV *eq));
/**********************************************************************/
static int
rdeqvcache(eq)
EQV *eq;
{
int i=0, nc=(int)eq->hdr.nfixcache;
EQVCACHE *ca=eq->cache;
dword off, recn;
byte len;

   if(eq->hdr.fixcacheoff!=0){
      if(eqvseek(eq,(off_t)eq->hdr.fixcacheoff,SEEK_SET)!=0) goto zerr;
      for(;i<nc;i++){
         if(eqvreaddw(&recn,1,eq,CACHEKEY)!=0) goto zerr;
         if(eqvreaddw(&off,1,eq,CACHEKEY)!=0) goto zerr;
         if(eqvreadb(&len,1,eq,CACHEKEY)!=0) goto zerr;
         if((ca[i].buf=(char *)malloc((int)len+1))==CHARPN) goto zerr;
         if(eqvreadb((byte *)ca[i].buf,(int)len,eq,CACHEKEY)!=0) goto zerr;
         ca[i].buf[len]='\0';
         ca[i].n=(long)recn;
         ca[i].off=(long)off;
         ca[i].lenw=(int)len;
      }
   }
   for(;i<NEQVFIXCACHE;i++){
      ca[i].n=(-1);
      ca[i].buf=CHARPN;
   }
   return(0);
zerr:
   return(1);
}                                                 /* end rdeqvcache() */
/**********************************************************************/

static int setupchain ARGS((EQV *eq));
/**********************************************************************/
static int
setupchain(eq)
EQV *eq;
{
char *fn;
int l=(int)eq->hdr.chainlen;

   if(l!=0){                         /* no chain from ram so no check */
      fn=eq->rec.buf;
      if(FSEEKO(eq->fp,(off_t)eq->hdr.chainoff,SEEK_SET)!=0 ||
         fread(fn,1,l,eq->fp)!=l
      ) return(-1);
      fn[l]='\0';
      if((eq->chain=openeqv(fn,eq->acp))==EQVPN){
         putmsg(MWARN,CHARPN,"Chain to \"%s\" ignored",fn);
         return(1);
      }
   }
   return(0);
}                                                 /* end setupchain() */
/**********************************************************************/

/**********************************************************************/
EQV *
openeqv(filename,acp)
char *filename;                                            /* main eq */
APICP *acp;
{
EQV *eq;
int i;
word maxwords;
static CONST char Fn[]="open equivs";

   if((eq=(EQV *)calloc(1,sizeof(EQV)))==EQVPN){
      putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
   }else{
      eqvdefaults(eq);
      eq->ram=BYTEPN;
      eq->rec.n=(-1);
      eq->fp=FILEPN;
      eq->rec.buf=CHARPN;
      eq->rec.eql=EQVLSTPN;
      eq->cache[0].buf=eq->cache[NEQVFIXCACHE].buf=CHARPN;
      eq->chain=EQVPN;
      if(acp==APICPPN){                               /* MAW 06-01-98 */
         eq->myacp=1;
         if((eq->acp=openapicp())==APICPPN)
         {
            putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
            goto zerr;
         }
      }else{
         eq->myacp=0;
         eq->acp=acp;
      }
      if(filename==CHARPN || *filename=='\0'){
         maxwords=1;
      }else{
#ifdef NO_RAM_EQUIVS
         if(strcmpi(filename,"builtin")==0){          /* MAW 02-02-99 */
#           ifdef unix
               filename="/usr/local/morph3/equivs";
#           else
               filename="c:\\morph3\\equivs";
#           endif
         }
#else
         if(strcmpi(filename,"builtin")==0){          /* MAW 02-02-99 */
            eq->ram=equivs;
            eq->ramsz=nequivs;
            eq->ramptr=eq->ram;
         }else
#endif
         {
            if((eq->fp=fopen(filename,BINARYREADMODE))==FILEPN)
            {
               putmsg(MERR+FOE,Fn,"Can't open %s: %s",filename,sysmsg(errno));
               goto zerr;
            }
         }
         if(rdeqvhdr(eq)!=0) goto zrerr;
         if((eq->rec.buf=(char *)malloc(max(eq->hdr.maxreclen,eq->hdr.chainlen)+1))==CHARPN){
            putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
            goto zerr;
         }
         if(rdeqvcache(eq)!=0) goto zrerr;
         for(i=NEQVFIXCACHE;i<NEQVCACHE;i++){
            if((eq->cache[i].buf=(char *)malloc(eq->hdr.maxwrdlen+1))==CHARPN){
               putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
               goto zerr;
            }
            eq->cache[i].n=(-1);                   /* indicate unused */
         }
         eq->nextc=NEQVFIXCACHE;
         if(setupchain(eq)<0) goto zrerr;
         maxwords=eq->hdr.maxwords;
      }
      if((eq->rec.eql=openeqvlst(maxwords))==EQVLSTPN){
         putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
         goto zerr;
      }
      free(eq->rec.eql->clas [0]);         /* don't really want these */
      free(eq->rec.eql->words[0]);            /* its just scratch pad */
   }
   return(eq);
zrerr:
   putmsg(MERR+FRE,Fn,"Can't read %s: %s",filename,sysmsg(errno));
zerr:
   return(closeeqv(eq));
}                                                    /* end openeqv() */
/**********************************************************************/

/**********************************************************************/
int
rdeqvndx(eq,ndx,n)
EQV *eq;
EQVNDX *ndx;
long n;
{
int rc;

   if(eqvseek(eq,n*NDXSZ,SEEK_SET)!=0 ||
      eqvreaddw(&ndx->off,1,eq,(word)n)!=0 ||
      eqvreadb (&ndx->len,1,eq,(word)n)!=0
   ){
      rc=(-1);
   }else{
      rc=0;
   }
   return(rc);
}                                                   /* end rdeqvndx() */
/**********************************************************************/

/**********************************************************************/
#ifndef WORDSONLY
static int rdeqvrec  ARGS((EQV *eq,EQVREC *rec,long n,int full));
static
#endif
int
rdeqvrec(eq,rec,n,full)
EQV *eq;
EQVREC *rec;
long n;
int full;
{
EQVNDX ndx1, ndx2;
int ci, readingndx=1;
EQVCACHE *ca;
static CONST char Fn[]="read equiv record";

   for(ca=eq->cache,ci=0;ci<NEQVCACHE;ci++){
      if(ca[ci].n==n){
         rec->off=ca[ci].off;
         rec->lenw=ca[ci].lenw;
         break;
      }
   }
   if(ci==NEQVCACHE){                                    /* not found */
      if(rdeqvndx(eq,&ndx1,n)!=0) goto zerr;
      rec->off=(long)ndx1.off;
      rec->lenw=(int)ndx1.len;
      TST(camiss++);
   }else{
      TST(cahit++);
   }
   if(full){
      if(rdeqvndx(eq,&ndx2,n+1)!=0) goto zerr;
      rec->lena=(int)((long)ndx2.off-rec->off);
      readingndx=0;
      if(eqvseek(eq,rec->off,SEEK_SET)!=0) goto zerr;
      if(eqvreadb((byte *)rec->buf,rec->lena,eq,(word)n)!=0) goto zerr;
      rec->buf[rec->lena]='\0';
      eqvparserec(rec);
   }else{
      if(ci==NEQVCACHE){                                 /* not found */
         readingndx=0;
         if(eqvseek(eq,rec->off,SEEK_SET)!=0) goto zerr;
         if(eqvreadb((byte *)rec->buf,rec->lenw,eq,(word)n)!=0) goto zerr;
         rec->buf[rec->lenw]='\0';
         ca[eq->nextc].n=n;
         ca[eq->nextc].off=rec->off;
         ca[eq->nextc].lenw=rec->lenw;
         strcpy(ca[eq->nextc].buf,rec->buf);
         eq->nextc+=1;
         if(eq->nextc==NEQVCACHE){             /* at end, wrap around */
            eq->nextc=NEQVFIXCACHE; /* keep the first few permanently */
         }
      }else{
         strcpy(rec->buf,ca[ci].buf);
         rec->off=ca[ci].off;
         rec->lenw=ca[ci].lenw;
      }
   }
   rec->n=n;
   return(0);
zerr:
   rec->n=(-1);
   putmsg(MERR+FRE,Fn,"Error reading %s: %s",
          readingndx?"index":"database",sysmsg(errno));
   return(-1);
}                                                   /* end rdeqvrec() */
/**********************************************************************/

#define eqcmp(s1,s2) strcmpi((s1),(s2))
#define eqncmp(s1,s2,n) strnicmp((s1),(s2),(n))

/**********************************************************************/
int
epi_findrec(eq,str,isutf8)
EQV *eq;
char *str;
int     isutf8;
{
long i, j, k, pi=0;
int cmp=0;
EQVREC *rec= &eq->rec;
CONST char      *tmpA, *tmpB;
#define EQCMP(s1, s2)           \
  (isutf8 ? (tmpA = (s1), tmpB = (s2), TXunicodeStrFoldCmp(&tmpA, -1, &tmpB, \
              -1, TXCFF_TEXTSEARCHMODE_DEFAULT_NEW)) : eqcmp(s1, s2))

   debug(1,putmsg(999,CHARPN,"at epi_findrec(,%s)",str));
   j=0;
   k=(long)eq->hdr.nrecs-1;
#ifndef EQVCACHE                        /* destroys benefits of cache */
   if(rec->n!=(-1)){              /* use last search to init this one */
      i=rec->n;
      cmp=EQCMP(str,rec->buf);
      if(cmp==0)     return(0);
      else if(cmp<0) k=i-1;
      else           j=i+1;
   }
#endif                                                   /* !EQVCACHE */
   while(j<=k){                                    /* bsearch for str */
      i=(j+k)>>1;
      if(rdeqvrec(eq,&eq->rec,i,0)!=0) return(-1);
      pi=i;
      cmp=EQCMP(str,rec->buf);
      if(cmp==0)     return(0);
      else if(cmp<0) k=i-1;
      else           j=i+1;
   }
#ifdef LOOKBACKWARDS
                                         /* get after insertion point */
   if(cmp>0 && pi<eq->hdr.nrecs && rdeqvrec(eq,&eq->rec,pi+1,0)!=0) return(-1);
#else
                                        /* get before insertion point */
   if(cmp<0 && pi>0 && rdeqvrec(eq,&eq->rec,pi-1,0)!=0) return(-1);
#endif
   return(1);
#undef EQCMP
}                                                    /* end epi_findrec() */
/**********************************************************************/

static char *bldphrase ARGS((char **lst,int *n));
/**********************************************************************/
static char *
bldphrase(lst,n)                  /* build a phrase from lst of words */
char **lst;
int *n;                        /* how many words to use/used from lst */
{
char *str, *p, *s;
int l, i, m;

             /* each word has extra logic char on front that we strip */
   m= *n;
               /* MAW 10-25-99 - don't bundle non-= terms into phrase */
   for(l=i=0;i<m && (i==0 || (*(lst[i]+1)!='~' && *lst[i]==EQV_CSET));i++) l+=strlen(lst[i]);
   *n=m=i;
   if((str=(char *)malloc(l))!=CHARPN){
      for(s=str,i=0,m--;;i++){
         p=lst[i]+1;
         if(*p==EQV_CINVERT) p++;
         for(;*p!='\0';p++,s++) *s= *p;
         if(i==m){
            *s='\0';
            break;
         }else *(s++)=' ';
      }
   }
   return(str);
}                                                  /* end bldphrase() */
/**********************************************************************/

static char **freelst ARGS((char **lst));
/**********************************************************************/
static char **
freelst(lst)
char **lst;
{
int i;

   if(lst!=CHARPPN){
      for(i=0;lst[i]!=CHARPN && *lst[i]!='\0';i++){
         free(lst[i]);
      }
      if(lst[i]!=CHARPN) free(lst[i]);
      free((char *)lst);
   }
   return(CHARPPN);
}                                                    /* end freelst() */
/**********************************************************************/

/**********************************************************************/
EQVLST **
closeeqvlst2lst(eql)                        /* free a list of EQVLSTs */
EQVLST **eql;
{
int i;

   if(eql!=EQVLSTPPN){
      for(i=0;eql[i]!=EQVLSTPN && eql[i]->words!=(char **)NULL;i++){
         closeeqvlst2(eql[i]);
      }
      if(eql[i]!=EQVLSTPN) free((char *)eql[i]);
      free((char *)eql);
   }
   return(EQVLSTPPN);
}                                            /* end closeeqvlst2lst() */
/**********************************************************************/

#ifndef WORDSONLY

static void rmdups ARGS((EQVLST *eql,int respclas ,int fre));
/**********************************************************************/
static void
rmdups(eql,respclas ,fre)
EQVLST *eql;
int respclas ;
int fre;
{
int i, j, n;
char **w, **c, *o;

   w=eql->words;
   c=eql->clas ;
   o=eql->op;
   for(n=i=1;*w[i]!='\0';i++){
      for(j=0;j<n;j++){                               /* look for dup */
         if(eqcmp(w[i],w[j])==0 &&
            (!respclas  || (eqcmp(c[i],c[j])==0 && o[i]==o[j]))
         ) break;
      }
      if(j==n){                                             /* no dup */
         w[n]=w[i];
         c[n]=c[i];
         if(respclas ) o[n]=o[i];
         else          o[n]=EQV_CADD;
         n++;
      }else{                                                   /* dup */
         debug(2,putmsg(999,CHARPN,"dup in %s: %s,%s",
               w[0],w[i],respclas ?c[i]:""));
         if(fre){
            free(w[i]);
            free(c[i]);
         }
      }
   }
   w[n]=w[i];
   c[n]=c[i];
   o[n]=o[i];
   eql->used=n+1;
}                                                     /* end rmdups() */
/**********************************************************************/

/**********************************************************************/
void
rmdupeql(eql)
EQVLST *eql;
{
   rmdups(eql,0,1);
}                                                   /* end rmdupeql() */
/**********************************************************************/

/**********************************************************************/
void
rmdupeqls(eql)
EQVLST **eql;
{
int i;

   for(i=0;eql[i]->words!=(char **)NULL;i++){
      rmdups(eql[i],0,1);
   }
}                                                  /* end rmdupeqls() */
/**********************************************************************/

static EQVLST *mergeeql ARGS((EQVLST *eql1,EQVLST *eql2,char *clas ));
/**********************************************************************/
static EQVLST *
mergeeql(eql,eql2,clas )   /* merge see refs in eql2 with main in eql */
EQVLST *eql;
EQVLST *eql2;
char *clas ;
{
char **w, **c, *o, **w2, **c2, *o2, *s, *tw, *tc, to;
int i, j, n;

                            /* see how many needed to hold both lists */
   w=eql->words;
   w2=eql2->words;
   for(n=0;*w[n]!='\0';n++) ;
   for(j=0;*w2[j]!='\0';j++) ;
   i=n+j;
                                /* alloc new list big enough for both */
   if((w=(char **)calloc(i,sizeof(char *)))==CHARPPN) goto zerr;
   if((c=(char **)calloc(i,sizeof(char *)))==CHARPPN){
      free((char *)w);
      goto zerr;
   }
   if((o=(char *)malloc(i))==CHARPN){
      free((char *)c);
      free((char *)w);
      goto zerr;
   }
                                   /* copy first list to its new home */
   for(j=0,w2=eql->words,c2=eql->clas ,o2=eql->op;*w2[j]!='\0';j++){
      w[j]=w2[j];
      c[j]=c2[j];
      o[j]=o2[j];
   }
   tw=w2[j];                                      /* save terminators */
   tc=c2[j];
   to=o2[j];
                                              /* free old unused list */
   free((char *)eql->words);
   free((char *)eql->clas );
   free((char *)eql->op);
   eql->words=w;
   eql->clas =c;
   eql->op=o;
   eql->sz=i;
                                                 /* merge second list */
   w2=eql2->words;
   c2=eql2->clas ;
   o2=eql2->op;
   free(w2[0]);                                    /* throw away root */
   free(c2[0]);
   for(i=1;*w2[i]!='\0';i++,n++){        /* each new word except root */
      if(*w2[i]==EQV_CSEE) strshl(w2[i]);       /* remove see trigger */
      if(clas ==CHARPN){
         s=c2[i];
      }else{
         if((s=strdup(clas ))==CHARPN){
            for(;*w2[i]!='\0';i++){
               free(w2[i]);
               free(c2[i]);
            }
            eql=closeeqvlst2(eql);
            break;
         }
         free(c2[i]);
      }
      w[n]=w2[i];
      c[n]=s;
      o[n]=o2[i];
   }
   w[n]=tw;                                        /* add terminators */
   c[n]=tc;
   o[n]=to;
   eql->used=n+1;
   closeeqvlst(eql2);                        /* free remnants of eql2 */
   return(eql);
zerr:
   closeeqvlst2(eql2);
   return(closeeqvlst2(eql));
}                                                   /* end mergeeql() */
/**********************************************************************/

static int rootsmatch ARGS((EQV *eq,char *root,char *wrd));
/**********************************************************************/
static int rootsmatch(eq,root,wrd)/* can wrd be stripped down to root? */
EQV *eq;
char *root, *wrd;
{
char *mem, *wrk;
int match=0, ol, l, x;
char **suf=eq->suflst;
int nsuf=eq->nsuf, minwl=eq->minwl;

   l=strlen(wrd);
   if((mem=wrk=(char *)malloc(l+1))==CHARPN) return(-1);
   strcpy(wrk,wrd);
   x=RM_INIT;
   do{
      if(eqcmp(root,wrk)==0){
         match=1;
         break;
      }
      ol=l;
      l=rm1suffix(&wrk,suf,nsuf,minwl,&x,eq->rmdef,0,eq->acp->textsearchmode);
   }while(l!=ol);
   free(mem);
   return(match);
}                                                 /* end rootsmatch() */
/**********************************************************************/

#ifdef LOOKBACKWARDS
#include "lookback.c"
#else                                               /* !LOOKBACKWARDS */
static int huntword ARGS((EQV *,char *));
/**********************************************************************/
static int
huntword(eq,wrd)                                   /* forward version */
EQV *eq;
char *wrd;
{
int olen, len, x, cmp, rc;
char **suf=eq->suflst;
int nsuf=eq->nsuf, minwl=eq->minwl;
long lastrec=(long)eq->hdr.nrecs-1;

   len=strlen(wrd);
   x=RM_INIT;                               /* init rm1suffix() state */
   while(1){                             /* chop one suffix at a time */
      olen=len;
      len=rm1suffix(&wrd,suf,nsuf,minwl,&x,eq->rmdef,0,eq->acp->textsearchmode);
      if(len==olen) break;                        /* nothing stripped */
      rc=epi_findrec(eq,wrd,0);
      if(rc<0) return(-1);                                   /* error */
      if(rc==0) return(1);                                   /* found */
      do{                                   /* scan all similar roots */
         cmp=rootsmatch(eq,wrd,eq->rec.buf);
         if(cmp<0) return(-1);
         if(cmp) return(1);
         if(eq->rec.n==lastrec) break;                         /* EOF */
         eq->rec.n++;                           /* forward one record */
         if(rdeqvrec(eq,&eq->rec,eq->rec.n,0)!=0) return(-1);
      }while(eqncmp(wrd,eq->rec.buf,len)==0);
   }
   return(0);                                            /* not found */
}                                                   /* end huntword() */
/**********************************************************************/

static int huntphrase ARGS((EQV *,char **,int *,char *,int,EQVLST **));
/**********************************************************************/
static int
huntphrase(eq,lst,retn,phrase,logic,eql)           /* forward version */
EQV *eq;
char **lst;
int *retn;                   /* return number used from lst in phrase */
char *phrase;
int logic;
EQVLST **eql;
{
char *e, *p = CHARPN;
int rc, invert;
static CONST char Fn[]="huntphrase";

                                    /* chop phrase one word at a time */
   debug(2,putmsg(999,CHARPN,"at huntphrase(,,,\"%s\",,) dict=\"%s\", lst[0]=\"%s\"",
         phrase,eq->rec.buf,lst[0]));
   if(lst[0][1]==EQV_CINVERT) invert=1;
   else                       invert=0;
   e=phrase+strlen(phrase);
   *eql=EQVLSTPN;
   while(1){
      if(eq->ueq!=EQV_UEQPN){                           /* ueq lookup */
         eq->rec.eql->logic=(char)logic;
         if(invert){
            if((p=(char *)malloc((int)(e-phrase+2)))==CHARPN){
               putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
               return(-1);
            }
            *p=EQV_CINVERT;
            strcpy(p+1,phrase);
            eq->rec.eql->words[0]=p;
         }else{
            eq->rec.eql->words[0]=phrase;
         }
         eq->rec.eql->clas [0]="";
         eq->rec.eql->words[1]=eq->rec.eql->clas [1]="";
#ifdef TX_DEBUG
	 eq->rec.eql->op[1]=EQV_CINVAL;/* Make sure var init'd *//* JMT 1999-07-29 */
#endif
         *eql=(*eq->ueq)(eq->rec.eql,eq->ueqarg);
         if(invert) free(p);
         if(*eql!=EQVLSTPN){
            return(1);                                 /* found it */
         }
      }
      if(*retn==1) break;                         /* down to one word */
      --*retn;
      e-=strlen(lst[*retn]);
      *e='\0';                                       /* remove a word */
      debug(2,putmsg(999,CHARPN,"phrase=\"%s\"",phrase));
      rc=epi_findrec(eq,phrase,0);
      if(rc<0) return(-1);                                   /* error */
      if(rc==0) return(1);                                /* found it */
   }
   return(0);                                            /* not found */
}                                                 /* end huntphrase() */
/**********************************************************************/
#endif                                               /* LOOKBACKWARDS */

static void rmeqvs ARGS((EQVLST *));
/**********************************************************************/
static void
rmeqvs(eql)                            /* free all but first and term */
EQVLST *eql;
{
int i;
char **w, **c;

   for(i=1,w=eql->words,c=eql->clas ;*w[i]!='\0';i++){
      free(w[i]);
      free(c[i]);
   }
   w[1]=w[i];                                         /* move term up */
   c[1]=c[i];
   eql->used=2;
}                                                     /* end rmeqvs() */
/**********************************************************************/

static EQVLST *chk4see ARGS((EQV *eq,EQVLST *eql,int mane));
static EQVLST *igeteqv ARGS((EQV *eq,char **lst,int *retn,int dophrase,int keep,int mane));
/**********************************************************************/
static EQVLST *
chk4see(eq,eql,mane)
EQV *eq;
EQVLST *eql;
int mane;      /* wtf: i don't like this main eq editing itself stuff */
{
int i, n;
char *lst[2];
EQVLST *teql;
static CONST char Fn[]="chk4see";

   if(!eq->kpsee){                              /* check for see refs */
      lst[1]="";
      for(i=0;*eql->words[i]!='\0';i++){
         if(*eql->words[i]==EQV_CSEE){                /* is a see ref */
            strshl(eql->words[i]);              /* rm leading trigger */
            if(eq->see){                               /* lookup refs */
                                                        /* "=~word\0" */
               if((lst[0]=(char *)malloc(strlen(eql->words[i])+3))==CHARPN){
                  goto zmemerr;
               }
               *lst[0]=EQV_CINVAL;          /* indicate see ref/exact */
               if(eq->kpeqvs) n=1;
               else{
                /* chk4see() not called if(!kpeqvs for this word)     */
                /* so if(!kpeqvs) add invert flag to force keep again */
                  lst[0][1]=EQV_CINVERT;
                  n=2;
               }
               strcpy(lst[0]+n,eql->words[i]);
               n=1;
               if((teql=igeteqv(eq,lst,&n,0,1,mane))==EQVLSTPN){
                  free(lst[0]);
                  goto zerr;
               }
                                /* see refs keep their original class */
               if((eql=mergeeql(eql,teql,CHARPN/*eql->clas [i]*/))==EQVLSTPN){
                  free(lst[0]);
                  goto zmemerr;
               }
               free(lst[0]);
            }
         }
      }
   }
   return(eql);
zmemerr:
   putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
zerr:
   if(eql!=EQVLSTPN) closeeqvlst2(eql);
   return(EQVLSTPN);
}                                                    /* end chk4see() */
/**********************************************************************/

static EQVLST *iediteq ARGS((EQVLST *eql,EQVLST *eqle));
/**********************************************************************/
/*
   all adds done before deletes so the following user eq will give
      power
   not
      power,electricity

   electricity,power,voltage
   power~electricity

   does not mess with its first argument
   closes its second argument
   returns new alloced list
*/
/**********************************************************************/
static EQVLST *
iediteq(eql,eqle)
EQVLST *eql, *eqle;
{
int i, n, rc;
char **w, **c, *o;
static CONST char Fn[]="iediteq";
static CONST char msg[]="Out of memory for equiv edits";

   if(*eqle->words[1]!='\0' && eqle->op[1]==EQV_CREPL){
      free(eqle->words[0]);                    /* throw away old root */
      free(eqle->clas [0]);
      w=eqle->words;
      c=eqle->clas ;
      o=eqle->op;
      eqle->used-=1;
      for(i=0;i<eqle->used;i++){     /* shift the whole list left one */
         w[i]=w[i+1];
         c[i]=c[i+1];
         o[i]=o[i+1];
      }
      eql=eqle;
   }else{
      if((eql=dupeqvlst(eql))==EQVLSTPN){
         closeeqvlst2(eqle);
         putmsg(MERR+MAE,Fn,msg);
         return(EQVLSTPN);
      }
      w=eqle->words;
      c=eqle->clas ;
      o=eqle->op;
      free(w[0]);
      free(c[0]);
      for(i=1,rc=0;rc>=0 && *w[i]!='\0';i++){              /* do adds */
         switch(o[i]){
         case EQV_CADD:
            debug(2,putmsg(999,CHARPN,"add: %s,%s",w[i],c[i]));
            if((rc=addeqvlst(eql,w[i],*c[i]=='\0'?CHARPN:c[i],EQV_CADD))<0){
               eql=closeeqvlst2(eql);
               for(;*w[i]!='\0';i++){
                  free(w[i]);
                  free(c[i]);
               }
               putmsg(MERR+MAE,Fn,msg);
            }else if(rc==0){              /* not added, already there */
               debug(2,putmsg(999,CHARPN,"already there: %s,%s",w[i],c[i]));
               free(w[i]);
               free(c[i]);
            }else{                                        /* added it */
               if(*c[i]=='\0') free(c[i]);
            }
            break;
         case EQV_CDEL:
            break;
         default:
            debug(o[i]==EQV_CDEL?99:0,putmsg(999,Fn,"unknown: o[%d]==0x%02x=='%c'",i,o[i],o[i]));
            free(w[i]);
            free(c[i]);
            break;
         }
      }
      n=i;                /* MAW 11-23-93 - remember how many in list */
            /* can't use *w[i]=='\0' as above because it may be freed */
      for(i=1;rc>=0 && i<n;i++){                        /* do deletes */
         switch(o[i]){
         case EQV_CADD:
            break;
         case EQV_CDEL:
            debug(2,putmsg(999,CHARPN,"remove: %s,%s",w[i],c[i]));
            rmeqvlst2(eql,w[i],*c[i]=='\0'?CHARPN:c[i]);
            debug(o[i]==EQV_CDEL?99:0,putmsg(999,Fn,"unknown: o[%d]==0x%02x=='%c'",i,o[i],o[i]));
            free(w[i]);
            free(c[i]);
         default:
            break;
         }
      }
      closeeqvlst(eqle);
   }
   return(eql);
}                                                    /* end iediteq() */
/**********************************************************************/

/**********************************************************************/
static EQVLST *
igeteqv(eq,lst,retn,dophrase,keep,mane)
EQV *eq;
char **lst;
int *retn;                   /* return number used from lst in phrase */
int dophrase;                                  /* do phrase searching */
int keep;                 /* return valid entry even if nothing found */
int mane;      /* wtf: i don't like this main eq editing itself stuff */
{
int n, rc, found=0, kpeqvs, invert;
EQVLST *eql=EQVLSTPN;
char *phrase=CHARPN;
static CONST char Fn[]="get equiv";

   if(*(lst[0]+1)==EQV_CINVERT){
      kpeqvs=!eq->kpeqvs;
      invert=1;
   }else{
      kpeqvs=eq->kpeqvs;
      invert=0;
   }
   if(!eq->acp->alequivs && kpeqvs)
   {
      if(acpdeny(eq->acp,"equivs")) goto zerr;
      goto zkeep;
   }
   if(eq->fp==FILEPN && eq->ram==BYTEPN) goto zkeep;/* no equiv file, act as if not found */
   if(eq->chain && *lst[0]!=EQV_CINVAL){    /* chained, not a see ref */
                  /* MAW 05-28-92 - god forgive me for this mess, but */
                  /* binary chaining was not in my original plan      */
      eqvcpy(eq->chain,eq);                  /* copy current settings */
      if((eql=igeteqv(eq->chain,lst,retn,dophrase,0,mane))!=EQVLSTPN){
         rc=epi_findrec(eq,eql->words[0],0);
         if(rc<0) goto zerr;
         else if(rc==0){
         EQVLST *eql2;

            if(rdeqvrec(eq,&eq->rec,eq->rec.n,1)!=0) goto zerr;
            eq->rec.eql->logic=eql->logic;
            if((eql2=dupeqvlst(eq->rec.eql))==EQVLSTPN) goto zmemerr;
                       /* don't perform edits yet, just collect em up */
            if(*eql2->words[1]!='\0' && eql2->op[1]==EQV_CREPL){
                                        /* but keep replace by itself */
               closeeqvlst2(eql);
               eql=eql2;
            }else{
               if((eql=mergeeql(eql,eql2,CHARPN))==EQVLSTPN) goto zmemerr;
            }
         }
         goto zueq;
      }
   }
   n= *retn;
   if((phrase=bldphrase(lst,&n))==CHARPN) goto zmemerr;
   rc=epi_findrec(eq,phrase,0);
   if(rc<0) goto zerr;
   if(rc==0) found=1;
   else if(*lst[0]!=EQV_CINVAL){                     /* not a see ref */
      if(dophrase && (found=huntphrase(eq,lst,&n,phrase,*lst[0],&eql))<0){
         goto zerr;
      }
      if(!found && eq->sufproc && eq->suflst!=CHARPPN){/* suffix proc */
         n=1;
         if((found=huntword(eq,phrase))<0) goto zerr;
      }
   }
   free(phrase),phrase=CHARPN;
   if(found){
      *retn=n;
      if(eql==EQVLSTPN){                      /* not found by user eq */
         if(rdeqvrec(eq,&eq->rec,eq->rec.n,1)!=0) goto zerr;
         eq->rec.eql->logic= *lst[0];
         if((eql=dupeqvlst(eq->rec.eql))==EQVLSTPN) goto zmemerr;
         if(*lst[0]!=EQV_CINVAL){                    /* not a see ref */
            if(kpeqvs && (eql=chk4see(eq,eql,mane))==EQVLSTPN) goto zerr;
            if(mane){/* MAW 04-30-92 - in main eq perform edits on yourself */
            EQVLST e;
            char *w[2];
            char *c[2];
            char o[2];
                    /* make a fake entry with just the root to "edit" */
#              ifdef EQVKEEPFOUNDROOT                 /* MAW 04-20-95 */
                  w[0]=eql->words[0];
#              else                                   /* MAW 04-20-95 */
                /* return root that was asked for, not what was found */
                  if(n==1)
                  {
              /* each word has extra logic char on front that we skip */
                     w[0]=lst[0]+1;
                     if(*w[0]==EQV_CINVERT) ++w[0];         /* skip ~ */
                  }
                  else                         /* phrase will be same */
                     w[0]=eql->words[0];
#              endif
               c[0]=eql->clas [0];
               o[0]=eql->op[0];
               w[1]=c[1]="";
	       o[1]=EQV_CINVAL;/* Make sure var init'd *//* JMT 1999-07-29 */
               memset(&e, 0, sizeof(EQVLST));
               e.words=w;
               e.clas =c;
               e.op=o;
               e.logic=eql->logic;
               e.sz=2;
               e.used=2;
               if((eql=iediteq(&e,eql))==EQVLSTPN) goto zmemerr;
            }
zueq:
            if(eq->ueq!=EQV_UEQPN){ /* always do ueq lookup, repl etc */
            EQVLST *neql;
            char *p = CHARPN, *sw = CHARPN;

               if(invert){
                  sw=eql->words[0];
                  if((p=(char *)malloc(strlen(sw)+2))==CHARPN) goto zmemerr;
                  *p=EQV_CINVERT;
                  strcpy(p+1,sw);
                  eql->words[0]=p;
               }
               neql=(*eq->ueq)(eql,eq->ueqarg);           /* ueq edit */
               if(invert){
                  eql->words[0]=sw;
                  free(p);
               }
               if(neql!=EQVLSTPN){
                  closeeqvlst2(eql);
                  eql=neql;
               }
            }
         }
      }
      if(kpeqvs) rmdups(eql,1,1);
      else       rmeqvs(eql);            /* no equivs, just root word */
   }else if(keep){/* no matches found at all, keep the word by itself */
   char *w;
      debug(1,putmsg(999,Fn,"%s not found",lst[0]+1));
zkeep:
      w=lst[0]+1;
      if(*w==EQV_CINVERT) w++;
      if(*w==EQV_CPPM){     /* MAW 08-25-94 - add "(eqlist)" handling */
      EQVLST *teql;
        if((teql=eqvparse(w+1, 0))==EQVLSTPN) goto zerr;
         teql->logic= *lst[0];
         if((eql=dupeqvlst(teql))==EQVLSTPN){
            closeeqvlst(teql);
            goto zmemerr;
         }
         closeeqvlst(teql);
      }else{
         eq->rec.eql->logic= *lst[0];
         eq->rec.eql->words[0]=w;
         eq->rec.eql->clas [0]="";
         eq->rec.eql->words[1]=eq->rec.eql->clas [1]="";
         eq->rec.eql->used=2;
         if((eql=dupeqvlst(eq->rec.eql))==EQVLSTPN) goto zmemerr;
      }
      *retn=1;
   }
   return(eql);
zmemerr:
   putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
zerr:
   if(phrase!=CHARPN) free(phrase);
   if(eql!=EQVLSTPN) closeeqvlst2(eql);
   return(EQVLSTPN);
}                                                    /* end igeteqv() */
/**********************************************************************/

#ifdef RMNOISEEQVS
static void rmnoise ARGS((EQV *eq,EQVLST *eql));
/**********************************************************************/
static void
rmnoise(eq,eql)
EQV *eq;
EQVLST *eql;
{
int s, d;
char **w, **c, *o;
char **lst;
void *isnarg;
int (*isnoise)ARGS((char **lst,char *wrd,void *isnarg));

   w=eql->words;
   c=eql->clas ;
   o=eql->op;
   lst=eq->noise;
   isnarg=eq->isnarg;
   isnoise=eq->isnoise;
   for(s=d=0;*w[s]!='\0';s++){
      if((*isnoise)(lst,w[s],isnarg)){
         free(w[s]);
         free(c[s]);
      }else{
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
}                                                    /* end rmnoise() */
/**********************************************************************/
#else                                                 /* !RMNOISEEQVS */
#  define rmnoise(a,b)
#endif                                                 /* RMNOISEEQVS */

/**********************************************************************/
EQVLST *
geteqv(eq,wrd)
EQV *eq;
char *wrd;
{
EQVLST *eql;
char *lst[2];
int retn;
static CONST char Fn[]="geteqv";

   if((eq->fp!=FILEPN || eq->ram!=BYTEPN) && eqprep()!=0){  /* prevent ripping apart equiv */
      return(EQVLSTPN);              /* windows eqprep() can't exit() */
   }
   if((lst[0]=(char *)malloc(strlen(wrd)+2))==CHARPN){
      putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
      return(EQVLSTPN);
   }
   *lst[0]=EQV_CSET;
   strcpy(lst[0]+1,wrd);
   lst[1]="";
   retn=1;
   eql=igeteqv(eq,lst,&retn,1,1,1);
   if(!eq->kpnoise && eq->isnoise!=EQV_ISNOISEPN) rmnoise(eq,eql);
   free(lst[0]);
   return(eql);
}                                                     /* eng geteqv() */
/**********************************************************************/

static int chkqperms ARGS((EQV *eq, EQVLST *eql));
static int
chkqperms(eq,eql)                                     /* MAW 06-02-98 */
EQV *eq;
EQVLST *eql;
{
APICP *acp=eq->acp;
char *p;
/*static char lin[]={ EQV_CREX,EQV_CXPM,EQV_CNPM,'\0' };*/

   if(!acp->alnot && eql->logic==EQV_CNOT)
   {
      if(acpdeny(acp,"not logic")) return(-1);
      return(1);
   }
#ifdef OLD_ALLINEAR
   /* KNG 980916 Meaning of allinear changed: now means allow an
    * all-linear search (eg. no index-searchable terms at all).  Must
    * be verified in index-land, but we could still check for an
    * all-REX search somewhere in here...
    */
   if(!acp->allinear && strchr(lin,*eql->words[0])!=CHARPN)
   {
      if(acpdeny(acp,"linear matchers")) return(-1);
      return(1);
   }
#endif  /* OLD_ALLINEAR */
   if((p=strchr(eql->words[0],'*'))!=CHARPN)
   {
      if(!acp->alwild)
      {
         if(acpdeny(acp,"wildcards")) return(-1);
         return(1);
      }
      if(acp->qminprelen>0 && (p-eql->words[0])<acp->qminprelen)
      { /* MAW 03-10-99 - show the word */
       static char buf[]="prefixes less than 10000 characters (....................)";
         sprintf(buf,"prefixes less than %d characters (%.20s)",
                 acp->qminprelen,eql->words[0]);
         if(acpdeny(acp,buf)) return(-1);
         return(1);
      }
   }
   else
     if (acp->qminwordlen > 0 &&
         strlen(eql->words[0]) < (size_t)acp->qminwordlen)
   { /* MAW 03-10-99 - show the word */
    static char buf[]="words less than 10000 characters (....................)";
      sprintf(buf,"words less than %d characters (%.20s)",
              acp->qminwordlen,eql->words[0]);
      if(acpdeny(acp,buf)) return(-1);
      return(1);
   }
   return(0);
}

/**********************************************************************/
EQVLST **
geteqvs(eq,query,isects)
EQV *eq;
char *query;
int *isects;
{
  char **lst=CHARPPN, *s, *e;
  int n, cnt, i, ei, pis = 0, chkthis, useWords;
EQVLST **eql=EQVLSTPPN, *curEql;
int     *setqoffs = INTPN, *setqlens = INTPN;
char    **originalPrefixedTerms = CHARPPN, *d, *orgPrefixedPhrase = CHARPN;
size_t  sz, j;
static CONST char Fn[]="get equivs";

   TST(cahit=camiss=0);
   if((eq->fp!=FILEPN || eq->ram!=BYTEPN) && eqprep()!=0){  /* prevent ripping apart equiv */
      return(EQVLSTPPN);             /* windows eqprep() can't exit() */
   }
   if(eq->sufproc && eq->suflst!=CHARPPN){
      initsuffix(eq->suflst,eq->acp->textsearchmode);  /* reverse suffixes */
      for(i=0,lst=eq->suflst;*lst[i]!='\0';i++) ;
      eq->nsuf=i;
      lst=CHARPPN;
   }
   if(isects!=(int *)NULL) pis= *isects;
   if(eqvpq(query,&lst,&cnt,&originalPrefixedTerms,isects,&setqoffs,&setqlens)!=0){ /* break query into lst */
      goto zmemerr;
   }
   if(cnt<1) goto zerr;                                /* empty query */
   if(!eq->acp->alintersects && isects!=(int *)NULL && *isects!=pis){/* MAW 06-02-98 */
      if(acpdeny(eq->acp,"intersects")) goto zerr;
      *isects = pis;                            /* KNG 001024 drop query @ */
   }
   debug(1,putmsg(999,(char *)NULL,"query=\"%s\", cnt=%d, lst[0]=\"%s\"",query,cnt,lst[0]));
                                    /* alloc max possible equiv pairs */
   if((eql=(EQVLST **)calloc(cnt+1,sizeof(EQVLST *)))==EQVLSTPPN){
      goto zmemerr;
   }
   for(i=ei=0;i<cnt;i+=n){
      chkthis=(-1);
      if(eq->uparse!=EQV_UPARSEPN &&                    /* user check */
         (lst[i]=(*eq->uparse)(lst[i],eq->uparsearg))==CHARPN
      ){
         goto zerr;
      }
      if(lst[i][0]=='\0' || lst[i][1]=='\0' ||
         (lst[i][1]=='~' && lst[i][2]=='\0')
      ){
         putmsg(MERR+UGE,Fn,"Invalid query term %d: \"%s\"",ei+1,lst[i]);
         goto zerr;
      }
      if(eq->acp->alphrase)                           /* MAW 06-01-98 */
         n=cnt-i;
      else
         n=1;
      if((eql[ei]=curEql=igeteqv(eq,&lst[i],&n,1,1,1))==EQVLSTPN) goto zerr;

      /* Set eql[ei].qoff/qlen: the rubber-band span of `lst' items used: */
      curEql->qoff = setqoffs[i];
      curEql->qlen = (setqoffs[i+n-1] + setqlens[i+n-1]) - setqoffs[i];

      /* Create the original prefix and phrase, by merging the
       * original versions of the spanned `lst' items:
       */
      sz = 0;
      for (j = i; j < (size_t)(i + n); j++)
        sz += strlen(originalPrefixedTerms[j]) + 1;     /* +1 for space/nul */
      if ((orgPrefixedPhrase = (char *)malloc(sz)) == CHARPN)
        goto zmemerr;
      d = orgPrefixedPhrase;
      for (j = i; j < (size_t)(i + n); j++)
        {
          if (j > (size_t)i) *(d++) = ' ';
          strcpy(d, originalPrefixedTerms[j]);
          d += strlen(d);
        }

      /* Split off the prefix.  WTF should this be done earlier, maybe
       * in eqvpq()?
       */
      useWords = 0;
      s = e = orgPrefixedPhrase;
      switch (*e)                               /* skip set logic */
        {
        case EQV_CSET:                          /* = */
        case EQV_CAND:                          /* + */
        case EQV_CNOT:                          /* - */
        case EQV_CISECT:                        /* @ */
          e++;
          break;
        }
      switch (*e)                               /* skip pat. matcher char */
        {
        case EQV_CPPM:                          /* ( */
          /* Originally was a user-specified paren list, so the source
           * phrases are `curEql->words':
           */
          useWords = 1;
          /* fall through */
        case EQV_CINVERT:                       /* ~ */
        case EQV_CREX:                          /* / */
        case EQV_CXPM:                          /* % */
        case EQV_CNPM:                          /* # */
          e++;
          break;
        }
      if ((curEql->originalPrefix = (char *)malloc((e - s) + 1)) == CHARPN)
        goto zmemerr;
      memcpy(curEql->originalPrefix, s, e - s);
      curEql->originalPrefix[e - s] = '\0';

      /* Grab source expressions, either `curEql->words' (if paren list)
       * or remainder of `orgPrefixedPhrase':
       */
      if (useWords)                             /* copy `curEql->words' */
        {
          /* `curEql->used' is length of `curEql->words', including
           * empty string terminator (?):
           */
          curEql->sourceExprs = (char **)calloc(curEql->used, sizeof(char *));
          if (curEql->sourceExprs == CHARPPN) goto zmemerr;
          for (j = 0; j < (size_t)curEql->used - 1; j++)
            if ((curEql->sourceExprs[j] = strdup(curEql->words[j])) == CHARPN)
              goto zmemerr;
        }
      else                                      /* rest of orgPrefixedPhrase*/
        {
          curEql->sourceExprs = (char **)calloc(2, sizeof(char *));
          if (curEql->sourceExprs == CHARPPN) goto zmemerr;
          if (e == orgPrefixedPhrase)           /* was no prefix: use as-is */
            {
              curEql->sourceExprs[0] = orgPrefixedPhrase;
              orgPrefixedPhrase = CHARPN;       /* `sourceExprs' owns it */
            }
          else if ((curEql->sourceExprs[0] = strdup(e)) == CHARPN)
            goto zmemerr;
        }

      if (orgPrefixedPhrase != CHARPN)
        {
          free(orgPrefixedPhrase);
          orgPrefixedPhrase = CHARPN;
        }

      if(!eq->kpnoise && eq->isnoise!=EQV_ISNOISEPN){
         if((*eq->isnoise)(eq->noise,curEql->words[0],eq->isnarg)){
            eql[ei]=curEql=closeeqvlst2(curEql);
         }else{
            rmnoise(eq,curEql);
            chkthis=ei;
            ei++;
         }
      }else{
         chkthis=ei;
         ei++;
      }
      if(chkthis!=(-1))
      {
         switch(chkqperms(eq,eql[chkthis]))
         {
          case 0: break;                                    /* all ok */
          case 1:                                          /* warning */
            ei--;
            eql[ei]=closeeqvlst2(eql[ei]);
            break;
          case -1:                                           /* error */
            goto zerr;
         }
      }
      if(eq->acp->qmaxsets>0 && ei>eq->acp->qmaxsets)
      {
       static char buf[]="more than 10000 terms/sets";
         sprintf(buf,"more than %d terms/sets",eq->acp->qmaxsets);
         ei--;
         eql[ei]=closeeqvlst2(eql[ei]);
         if(acpdeny(eq->acp,buf)) goto zerr;
         break;
      }
   }
   freelst(lst);
   lst = CHARPPN;
   freelst(originalPrefixedTerms);
   originalPrefixedTerms = CHARPPN;
   if((eql[ei]=(EQVLST *)calloc(1,sizeof(EQVLST)))==EQVLSTPN) goto zmemerr;
   eql[ei]->words=eql[ei]->clas =CHARPPN;
   eql[ei]->op=CHARPN;
   eql[ei]->used=0;
   eql[ei]->qoff = eql[ei]->qlen = -1;
   if(setqoffs!=INTPN) free(setqoffs);
   if(setqlens!=INTPN) free(setqlens);
   if(eq->sufproc && eq->suflst!=CHARPPN) initsuffix(eq->suflst, eq->acp->textsearchmode);/* un-reverse suffixes */
   return(eql);
zmemerr:
   putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
zerr:
   freelst(lst);
   lst = CHARPPN;
   freelst(originalPrefixedTerms);
   originalPrefixedTerms = CHARPPN;
   if(setqoffs!=INTPN) free(setqoffs);
   if(setqlens!=INTPN) free(setqlens);
   if (orgPrefixedPhrase != CHARPN)
     {
       free(orgPrefixedPhrase);
       orgPrefixedPhrase = CHARPN;
     }
   if(eq->sufproc && eq->suflst!=CHARPPN) initsuffix(eq->suflst,eq->acp->textsearchmode);/* un-reverse suffixes */
   return(closeeqvlst2lst(eql));
}                                                    /* end geteqvs() */
/**********************************************************************/

/**********************************************************************/
EQVLST *
getueqv(eql,arg)
EQVLST *eql;
void *arg;
{
EQV *eq=(EQV *)arg;
EQVLST *eqle;
char *lst[2];
int n;
static CONST char Fn[]="getueqv";

              /* eql->words[0] may have leading ~ if user entered one */
   debug(2,putmsg(999,CHARPN,"at getueqv(%s,)",eql->words[0]));
   if((lst[0]=(char *)malloc(strlen(eql->words[0])+2))==CHARPN){
      putmsg(MWARN+MAE,Fn,sysmsg(ENOMEM));
      goto znone;
   }
   *lst[0]=eql->logic;
   strcpy(lst[0]+1,eql->words[0]);
   lst[1]="";
   n=1;
   eqle=igeteqv(eq,lst,&n,0,0,0);
   free(lst[0]);
   if(eqle==EQVLSTPN) goto znone;
   eql=iediteq(eql,eqle);
                             /* MAW 09-17-92 - strip leading ~ if any */
	if(*eql->words[0]==EQV_CINVERT) strshl(eql->words[0]);
   return(eql);
znone:
   return(EQVLSTPN);
}                                                    /* end getueqv() */
/**********************************************************************/

/**********************************************************************/
void
closeueqv(eq)
EQV *eq;
{
   if(eq->ueqarg!=VOIDPN) closeeqv((EQV *)eq->ueqarg);
   eqvueq(eq,EQV_UEQPN);
   eqvueqarg(eq,VOIDPN);
}                                                  /* end closeueqv() */
/**********************************************************************/

/**********************************************************************/
void
cpyeq2ueq(eq)
EQV *eq;
{
EQV *ueq=(EQV *)eq->ueqarg;

   ueq->see      =eq->see;
   ueq->sufproc  =eq->sufproc;
   ueq->suflst   =eq->suflst;
   ueq->minwl    =eq->minwl;
   ueq->kpsee    =eq->kpsee;
   ueq->kpeqvs   =eq->kpeqvs;
   ueq->kpnoise  =eq->kpnoise;
   ueq->noise    =eq->noise;
   ueq->isnoise  =eq->isnoise;
   ueq->isnarg   =eq->isnarg;
   ueq->uparse   =eq->uparse;
   ueq->uparsearg=eq->uparsearg;
   ueq->rmdef    =eq->rmdef;
}                                                  /* end cpyeq2ueq() */
/**********************************************************************/

/**********************************************************************/
EQV *
openueqv(eq,filename)
EQV *eq;
char *filename;
{
EQV *ueq;

   if((ueq=openeqv(filename,eq->acp))!=EQVPN){
      eqvueq(eq,getueqv);
      eqvueqarg(eq,(void *)ueq);
      cpyeq2ueq(eq);
   }
   return(ueq);
}                                                   /* end openueqv() */
/**********************************************************************/
#endif                                                  /* !WORDSONLY */

/**********************************************************************/
/**********************************************************************/
/**********************************************************************/
#ifdef TEST
#include "unistd.h"

static void dumprecs ARGS((EQV *eq,int n));
/**********************************************************************/
static void
dumprecs(eq,n)
EQV *eq;
int n;
{
long i, j, recn;

   recn=eq->rec.n;
   if((long)n>recn) i=0;
   else             i=recn-n;
   j=recn+n+1;
   if((ulong)j>eq->hdr.nrecs) j=(long)eq->hdr.nrecs;
   for(;i<j;i++){
      if(rdeqvrec(eq,&eq->rec,i,0)!=0){
         printf(" %5lu: --error--\n",i);
      }else{
         printf("%c%5lu: \"%s\"\n",(i==recn)?'*':' ',i,eq->rec.buf);
      }
   }
}                                                   /* end dumprecs() */
/**********************************************************************/

static void dumpallrecs ARGS((EQV *eq));
/**********************************************************************/
static void
dumpallrecs(eq)
EQV *eq;
{
long i, j;
char *p;

   for(i=0,j=eq->hdr.nrecs;i<j;i++){
      if(rdeqvrec(eq,&eq->rec,i,1)!=0){
         printf("%ld: --error--\n",i);
         break;
      }else{
         if((p=eqvfmt(eq->rec.eql))==CHARPN){
            puts("No mem");
            break;
         }
         puts(p);
         free(p);
      }
   }
}                                                   /* end dumprecs() */
/**********************************************************************/

/**********************************************************************/
static void
prteql(eql,eqfmt)
EQVLST *eql;
int eqfmt;
{
   debug(2,printf("sz=%d, used=%d\n",eql->sz,eql->used));
   if(eqfmt){
   char *p;

      if((p=eqvfmt(eql))==CHARPN){
         puts("No mem");
      }else{
         puts(p);
         free(p);
      }
   }else{
   int j;
   char **w, **c;

         w=eql->words;
         c=eql->clas ;
         printf("\n%c ",eql->logic);
         for(j=0;*w[j]!='\0';j++){
            printf("%s{%s},",w[j],c[j]);
         }
         putchar('\n');
   }
}                                                     /* end prteql() */
/**********************************************************************/

static void prteqls ARGS((EQVLST **eql,int eqfmt));
/**********************************************************************/
static void
prteqls(eql,eqfmt)
EQVLST **eql;
int eqfmt;
{
int i;

   for(i=0;eql[i]->words!=CHARPPN;i++){
      prteql(eql[i],eqfmt);
   }
}                                                    /* end prteqls() */
/**********************************************************************/

static char *suffix[]={
     "'", "able", "age", "aged", "ager", "ages", "al", "ally", "ance",
     "anced", "ancer", "ances", "ant", "ary", "at", "ate", "ated", "ater",
     "atery", "ates", "atic", "ed", "en", "ence", "enced", "encer", "ences",
     "end", "ent", "er", "ery", "es", "ess", "est", "ful", "ial", "ible",
     "ibler", "ic", "ical", "ice", "iced", "icer", "ices", "ics", "ide",
     "ided", "ider", "ides", "ier", "ily", "ing", "ion", "ious", "ise",
     "ised", "ises", "ish", "ism", "ist", "ity", "ive", "ived", "ives", "ize",
     "ized", "izer", "izes", "less", "ly", "ment", "ncy", "ness", "nt", "ory",
     "ous", "re", "red", "res", "ry", "s", "ship", "sion", "th", "tic", "tion",
     "ty", "ual", "ul", "ward",
     ""
};                                                    /* end suffix[] */
static char *altsuffix[]={
     "'", "s", "ies",
     ""
};                                                 /* end altsuffix[] */
char *noise[]={
     "a", "about", "after", "again", "ago", "all", "almost", "also",
     "always", "an", "and", "another", "any", "anyhow", "anyway", "as",
     "at", "away", "back", "became", "because", "before", "between", "but",
     "by", "came", "cannot", "come", "does", "doing", "done", "down",
     "each", "else", "even", "ever", "every", "everything", "for", "from",
     "front", "get", "getting", "go", "goes", "going", "gone", "got",
     "gotten", "having", "here", "if", "in", "into", "isn't", "it",
     "just", "last", "least", "left", "less", "let", "like", "make",
     "many", "may", "maybe", "mine", "more", "most", "much", "my",
     "never", "no", "none", "not", "now", "of", "off", "on",
     "one", "onto", "or", "out", "over", "per", "put", "putting",
     "same", "saw", "see", "seen", "shall", "she", "should", "so",
     "some", "stand", "such", "sure", "take", "than", "that", "the",
     "their", "them", "then", "there", "these", "this", "those", "through",
     "till", "to", "too", "two", "unless", "until", "up", "upon",
     "us", "very", "went", "whether", "while", "with", "within", "without",
     "yet",
     ""
};                                                     /* end noise[] */

static int isnoise ARGS((char **,char *,void *));
/**********************************************************************/
static int
isnoise(lst,wrd,argunused)
char **lst;
char *wrd;
void *argunused;
{
   for(;**lst!='\0';lst++){
      if(strcmpi(*lst,wrd)==0) return(1);
   }
   return(0);
}                                                    /* end isnoise() */
/**********************************************************************/

static char *myparse ARGS((char *str,void *arg));
/**********************************************************************/
static char *
myparse(str,arg)
char *str;
void *arg;
{
char *s;
static CONST char Fn[]="myparse";

   arg;
   if((s=strdup(str))==CHARPN) putmsg(MERR+MAE,Fn,"word is \"%s\"",str);
   else                        debug(3,putmsg(999,Fn,"word is \"%s\"",s));
   free(str);
   return(s);
}                                                    /* end myparse() */
/**********************************************************************/

static int ndx ARGS((char *src,char *bin));
/**********************************************************************/
static int
ndx(src,bin)
char *src, *bin;
{
EQVLST *eql=EQVLSTPN;
EQVX *eqx=EQVXPN;
FILE *fp=FILEPN;
char *buf=CHARPN;
int rc=(-1);
long ln, cnt;
static CONST char Fn[]="ndx";
#define NDXBUFSZ 5192

   if((buf=(char *)malloc(NDXBUFSZ))==CHARPN){
      putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
      goto zerr;
   }
   if((fp=fopen(src,"r"))==FILEPN){
      putmsg(MERR+FOE,Fn,"Can't open input %s: %s",src,sysmsg(errno));
      goto zerr;
   }
   if((eqx=openeqvx(bin))==EQVXPN){
      putmsg(MERR,Fn,"Can't open output %s",bin);
      goto zerr;
   }
   ln=cnt=0;
   while(fgets(buf,NDXBUFSZ,fp)!=CHARPN){
      ++ln;
      if(++cnt==100){
         fprintf(stderr,"%ld lines\r",ln);
         cnt=0;
      }
      debug(3,putmsg(999,CHARPN,"\"%s\"",buf));
      if((eql=eqvparse(buf,1))==EQVLSTPN){
         putmsg(MERR,Fn,"No memory to parse line %ld: \"%s\"",ln,buf);
         goto zerr;
      }
      if(writeeqvx(eqx,eql)!=0){
         putmsg(MERR,Fn,"Error writing line %ld: \"%s\"",ln,buf);
         goto zerr;
      }
      eql=closeeqvlst(eql);
   }
   if(ferror(fp)){
      putmsg(MERR+FRE,Fn,"Can't read input line %ld: %s",src,ln+1,sysmsg(errno));
      goto zerr;
   }
   rc=finisheqvx(eqx);
zerr:
   if(eql!=EQVLSTPN) closeeqvlst(eql);
   if(eqx!=EQVXPN) closeeqvx(eqx);
   if(fp!=FILEPN) fclose(fp);
   if(buf!=CHARPN) free(buf);
   if(rc!=0) unlink(bin);
   return(rc);
#undef NDXBUFSZ
}                                                        /* end ndx() */
/**********************************************************************/

static void use ARGS((void));
/**********************************************************************/
static void
use()
{
   puts("Use: eqv [-D] [-isrcfile] [-w] [-r] [-b[number]] [-u[usereq]] [-fflag=value ...] [eqfile]");
   puts(" or: eqv [-D] [-isrcfile] [-d] [eqfile]");
   puts(" or: eqv [-D] [-isrcfile] [-l] [eqfile]");
   puts(" or: eqv [-D] [-isrcfile] [-c] [eqfile]");
   exit(1);
}                                                        /* end use() */
/**********************************************************************/

static void doflags ARGS((EQV *eq,char **fl));
/**********************************************************************/
static void
doflags(eq,fl)
EQV *eq;
char **fl;
{
char *p;

   for(;**fl!='\0';fl++){
      if((p=strchr(*fl,'='))==CHARPN) printf("bad usage ignored: %s\n",*fl);
      else{
         *p='\0';
         p++;
         if(strcmp(*fl,"see")==0) eqvsee(eq,atoi(p));
         else if(strcmp(*fl,"minwl")==0) eqvminwl(eq,atoi(p));
         else if(strcmp(*fl,"kpsee")==0) eqvkpsee(eq,atoi(p));
         else if(strcmp(*fl,"kpeqvs")==0) eqvkpeqvs(eq,atoi(p));
         else if(strcmp(*fl,"sufproc")==0) eqvsufproc(eq,atoi(p));
         else if(strcmp(*fl,"kpnoise")==0) eqvkpnoise(eq,atoi(p));
         else if(strcmp(*fl,"rmdef")==0) eqvrmdef(eq,atoi(p));
         else printf("unknown flag ignored: %s\n",*fl);
      }
   }
}                                                    /* end doflags() */
/**********************************************************************/

/**********************************************************************/
static void
setdeny(acp,s)
APICP *acp;
char *s;
{
char *p;
int val;

   p=strchr(s,'=');
   *p='\0';
   val=atoi(p+1);
        if(strcmp(s,"denymode"    )==0) acp->denymode    =val;
   else if(strcmp(s,"alpostproc"  )==0) acp->alpostproc  =val;
   else if(strcmp(s,"allinear"    )==0) acp->allinear    =val;
   else if(strcmp(s,"alwild"      )==0) acp->alwild      =val;
   else if(strcmp(s,"alnot"       )==0) acp->alnot       =val;
   else if(strcmp(s,"alwithin"    )==0) acp->alwithin    =val;
   else if(strcmp(s,"alintersects")==0) acp->alintersects=val;
   else if(strcmp(s,"alequivs"    )==0) acp->alequivs    =val;
   else if(strcmp(s,"alphrase"    )==0) acp->alphrase    =val;
   else if(strcmp(s,"exactphrase" )==0) acp->exactphrase =val;
   else if(strcmp(s,"qminwordlen" )==0) acp->qminwordlen =val;
   else if(strcmp(s,"qminprelen"  )==0) acp->qminprelen  =val;
   else if(strcmp(s,"qmaxsets"    )==0 || strcmp(s,"qmaxterms") == 0)
     acp->qmaxsets   =val;
   else if(strcmp(s,"qmaxsetwords")==0) acp->qmaxsetwords=val;
   else if(strcmp(s,"qmaxwords"   )==0) acp->qmaxwords   =val;
}
/**********************************************************************/

/**********************************************************************/
void
timethis(acp,bin,input)
APICP *acp;
char *bin, *input;
{
EQV *eq;
EQVLST **eql;
FILE *fp=fopen(input,"r");
char buf[1024];
int isects;

   while(fgets(buf,1024,fp)!=CHARPN)
   {
      if((eq=openeqv(bin,acp))==EQVPN)
      {
         fprintf(stderr,"openeqv() failed\n");
         break;
      }
      eqprepstr("ovrd");
      eqvkpeqvs(eq,1);
      eqvsuflst(eq,altsuffix);
      eqvnoise(eq,noise);
      eqvisnoise(eq,isnoise);
      if((eql=geteqvs(eq,buf,&isects))!=EQVLSTPPN){
         closeeqvlst2lst(eql);
      }else{
         fprintf(stderr,"geteqvs() failed\n");
      }
      closeeqv(eq);
   }
   fclose(fp);
}
/**********************************************************************/

#ifdef MSDOS
#  define EQVS    "c:\\morph3\\equivs"
#  define EQVSUSR "c:\\morph3\\eqvsusr"
#  define EQVSSRC "../equivs/equivs.src"
#else
#  define EQVS    "/usr/local/morph3/equivs"
#  define EQVSUSR "/usr/local/morph3/eqvsusr"
#  define EQVSSRC "..\\equivs\\equivs.src"
#endif

void main ARGS((int argc,char **argv));
/**********************************************************************/
void
main(argc,argv)
int argc;
char **argv;
{
EQV *eq;
char *src=CHARPN, *bin=EQVS, *usr=CHARPN, *tfile=CHARPN;
int rc, ndump=(-1), nflags=0, list=0, dump=0, cdump=0, isects, eqfmt=1, wrds=0;
int rmdups=1, altsuf=0, kp=1, echo=0;
#define MAXFLAGS 4
char *flags[MAXFLAGS+1];
static char buf[80];
APICP *acp;

   flags[0]="";
   acp=openapicp();
   for(--argc,++argv;argc>0 && **argv=='-';argc--,argv++){
      switch(*++*argv){
      case 'E': echo=1; break;
      case 'k': kp=0; break;
      case 'l': list=1; break;
      case 'd': dump=1; break;
      case 'c': cdump=1; break;
      case 'e': eqfmt=0; break;
      case 'w': wrds=1; break;
      case 'r': rmdups=0; break;
      case 's': altsuf=1; break;
      case 'i': if(*++*argv=='\0') src=EQVSSRC;
                else               src= *argv;
                break;
      case 'b': ndump=atoi(++*argv); break;
      case 'f': if(nflags<MAXFLAGS){
                   flags[nflags+1]=flags[nflags];
                   flags[nflags]= ++*argv;
                   nflags++;
                }else puts("too many flags - extra ignored");
                break;
      case 'u': if(*++*argv=='\0') usr=EQVSUSR;
                else               usr= *argv;
                break;
      case 'B': if(*++*argv=='\0') bin=EQVS;
                else               bin= *argv;
                break;
      case 'T': tfile= ++*argv; break;
      case 'D': debuglevel=atoi(++*argv); break;
      case 'M': mac_off(); break;
      case 'p': setdeny(acp,++*argv); break;
#ifdef MEMDEBUG
      case 'I': mac_ndx=atoi(++*argv); break;
#endif
      default: use(); break;
      }
   }
   if(argc>1) use();
   else if(argc==1) bin= *argv;
   if(tfile!=CHARPN)
   {
      timethis(acp,bin,tfile);
      exit(0);
   }
   if(src!=CHARPN){                                  /* index eq file */
      if(ndx(src,bin)!=0) exit(1);
   }else if((eq=openeqv(bin,acp))!=EQVPN){
      eqvkpeqvs(eq,kp);
      if(dump){                     /* dump header info for debugging */
         printf("Equiv file : %s\n",bin);
         printf("Maxwrdlen  : %ld\n",(long)eq->hdr.maxwrdlen  );
         printf("Maxreclen  : %ld\n",(long)eq->hdr.maxreclen  );
         printf("Maxwords   : %ld\n",(long)eq->hdr.maxwords   );
         printf("Nrecs      : %ld\n",(long)eq->hdr.nrecs      );
         printf("Dataoff    : %ld\n",(long)eq->hdr.dataoff    );
         printf("Fixcacheoff: %ld\n",(long)eq->hdr.fixcacheoff);
         printf("Nfixcache  : %ld\n",(long)eq->hdr.nfixcache  );
         printf("Chainoff   : %ld\n",(long)eq->hdr.chainoff   );
         printf("Chainlen   : %ld\n",(long)eq->hdr.chainlen   );
         printf("Version    : %lx\n",(long)eq->hdr.version    );
         printf("Magic      : %lx\n",(long)eq->hdr.magic      );
      }else if(list){                          /* list entire eq file */
         dumpallrecs(eq);
      }else if(cdump){                    /* cache dump for debugging */
      int i;

         printf("equiv file: %s\n",bin);
         for(i=0;i<(int)eq->hdr.nfixcache;i++){
            printf("n=%5ld, off=%7ld, lenw=%2ld, buf=\"%s\"\n",
                   (long)eq->cache[i].n,
                   (long)eq->cache[i].off,
                   (long)eq->cache[i].lenw,
                   eq->cache[i].buf
            );
         }
      }else{                                /* perform normal lookups */
         printf("equiv file: %s\n",bin);
         eqvsuflst(eq,altsuf?altsuffix:suffix);
         eqvnoise(eq,noise);
         eqvisnoise(eq,isnoise);
         eqvuparse(eq,myparse);
         doflags(eq,flags);       /* handle the command line eq flags */
#        ifndef WORDSONLY
            if(usr!=CHARPN){
               if(openueqv(eq,usr)==EQVPN){/* optional user equiv */
                  printf("couldn't open user equiv: %s\n",usr);
               }else{
                  printf("user equiv: %s\n",usr);
               }
            }
#        endif
         if(echo) eqprepstr("ovrd");
         while(putchar('>'),fflush(stdout),gets(buf)!=CHARPN){
            if(echo) puts(buf);
            if(ndump>=0){ /* print dictionary words around found word */
               rc=epi_findrec(eq,buf,0);
               printf("epi_findrec(,%s)==%d\n",buf,rc);
               dumprecs(eq,ndump);
            }else{                                   /* normal lookup */
#              ifndef WORDSONLY
                  if(wrds){
                  EQVLST *eql;

                     if((eql=geteqv(eq,buf))!=EQVLSTPN){
                        debug(1,printf("cahit==%d, camiss=%d\n",cahit,camiss));
                        if(rmdups) rmdupeql(eql);
                        prteql(eql,eqfmt);
                        closeeqvlst2(eql);
                     }
                  }else{
                  EQVLST **eql;

                     if((eql=geteqvs(eq,buf,&isects))!=EQVLSTPPN){
                        debug(1,printf("cahit==%d, camiss=%d\n",cahit,camiss));
                        if(isects>=0) printf("requested intersects: %d\n",isects);
                        if(rmdups) rmdupeqls(eql);
                        prteqls(eql,eqfmt);
                        closeeqvlst2lst(eql);
                     }else{
                        printf("geteqvs() failed\n");
                     }
                  }
#              endif
            }
         }
         putchar('\n');
#        ifndef WORDSONLY
            closeueqv(eq);
#        endif
      }
      closeeqv(eq);
   }
   exit(0);
}                                                       /* end main() */
/**********************************************************************/

/**********************************************************************/
char *
TXtempnam(dir, prefix, ext)
CONST char      *dir;           /* (in) dir to use (NULL: pick one) */
CONST char      *prefix;        /* (in) prefix for name (NULL: pick one) */
CONST char      *ext;           /* (in) extension (w/.) (NULL: none) */
{
   return(tempnam(dir,prefix));
}
/**********************************************************************/

#ifdef DEFEAT
/**********************************************************************/
long time(v)
long *v;
{
static long t=0;

   t+=10;
   if(v!=NULL) *v=t;
   return(t);
}
/**********************************************************************/
#endif                                                      /* DEFEAT */
#endif                                                        /* TEST */
