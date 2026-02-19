#include "txcoreconfig.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sys/types.h"
#if defined(MSDOS) || defined(__MINGW32__)
#  define LOWIO
#endif
#include "os.h"
#include "eqvint.h"

#define EQVLSTSZ  8
#define EQVLSTINC 4

/**********************************************************************/
EQVLST *
closeeqvlst(eql)
EQVLST *eql;
{
  size_t        i;

  if (eql == EQVLSTPN) goto done;

   if(eql->op   !=CHARPN ) free(eql->op);
   if(eql->clas !=CHARPPN){
      if(eql->used>0 && eql->clas [eql->used-1]!=CHARPN) free(eql->clas [eql->used-1]);
      free((char *)eql->clas );
   }
   if(eql->words!=CHARPPN){
      if(eql->used>0 && eql->words[eql->used-1]!=CHARPN) free(eql->words[eql->used-1]);
      free((char *)eql->words);
   }
   if (eql->originalPrefix != CHARPN)
     {
       free(eql->originalPrefix);
       eql->originalPrefix = CHARPN;
     }
   if (eql->sourceExprs != CHARPPN)
     {
       for (i = 0; eql->sourceExprs[i] != CHARPN; i++)
         {
           free(eql->sourceExprs[i]);
           eql->sourceExprs[i] = CHARPN;
         }
       free(eql->sourceExprs);
       eql->sourceExprs = CHARPPN;
     }
   free((char *)eql);
done:
   return(EQVLSTPN);
}                                                /* end closeeqvlst() */
/**********************************************************************/

/**********************************************************************/
EQVLST *
closeeqvlst2(eql)
EQVLST *eql;
{
char **w, **c;
int i;

   w=eql->words;
   c=eql->clas ;
   if(w==CHARPPN){
      if(c!=CHARPPN){
         for(i=0;*c[i]!='\0';i++){
            free(c[i]);
         }
      }
   }else{
      for(i=0;*w[i]!='\0';i++){
         free(w[i]);
         if(c!=CHARPPN) free(c[i]);
      }
   }
   return(closeeqvlst(eql));
}                                               /* end closeeqvlst2() */
/**********************************************************************/

/**********************************************************************/
EQVLST *
openeqvlst(n)
int n;
{
EQVLST *eql;

   if(n<1) n=EQVLSTSZ;
   else    n++;                             /* add one for terminator */
   if((eql=(EQVLST *)calloc(1,sizeof(EQVLST)))!=EQVLSTPN){
      eql->used=0;
      eql->sz=n;
      eql->words=eql->clas =CHARPPN;
      eql->op=CHARPN;
      eql->qoff = eql->qlen = -1;               /* unknown for now */
      if((eql->words=(char **)calloc(eql->sz,sizeof(char *)))==CHARPPN ||
         (eql->clas =(char **)calloc(eql->sz,sizeof(char *)))==CHARPPN ||
         (eql->op   =(char *)malloc(eql->sz))==CHARPN
      ){
         goto zerr;
      }
      if((eql->words[0]=(char *)malloc(1))==CHARPN) goto zerr;
      if((eql->clas [0]=(char *)malloc(1))==CHARPN){
         free(eql->words[0]);
         eql->words[0]=CHARPN;
         goto zerr;
      }
      *eql->words[0]= *eql->clas [0]='\0';
      eql->op[0]=EQV_CINVAL;
      eql->used=1;
   }
   return(eql);
zerr:
   return(closeeqvlst2(eql));
}                                                 /* end openeqvlst() */
/**********************************************************************/

#ifdef __BORLANDC__     /* MAW 11-09-92 - optimizer screws up realloc */
   #pragma option -Od
#endif
/**********************************************************************/
int
addeqvlst(eql,w,c,o)
EQVLST *eql;
char *w, *c;
int o;
{
int i, used=eql->used;
char **words=eql->words, **clas =eql->clas , *op=eql->op;

   for(i=0;i<used;i++){
      if(op[i]==(char)o && eqcmp(words[i],w)==0 && (c==CHARPN || eqcmp(clas [i],c)==0)){
         return(0);                                  /* already there */
      }
   }
   if(used==eql->sz){
      eql->sz+=EQVLSTINC;
      if((eql->words=(char **)realloc((char *)eql->words,eql->sz*sizeof(char *)))==CHARPPN ||
         (eql->clas =(char **)realloc((char *)eql->clas ,eql->sz*sizeof(char *)))==CHARPPN ||
         (eql->op   =(char *)realloc(eql->op   ,eql->sz))==CHARPN
      ){
         return(-1);
      }
   }
   if(c==CHARPN){
#ifdef NEVER                               /* MAW 10-27-92 - wtf why? */
      if((c=strdup(eql->clas [used-2]))==CHARPN) return(-1);
#else
      if((c=strdup(""))==CHARPN) return(-1);
#endif
   }
   i=used-1;
   eql->words[used]=eql->words[i];                /* shift terminator */
   eql->clas [used]=eql->clas [i];
   eql->op[used]=eql->op[i];
   eql->words[i]=w;
   eql->clas [i]=c;
   eql->op[i]=(char)o;
   eql->used+=1;
   return(1);
}                                                  /* end addeqvlst() */
/**********************************************************************/
#ifdef __BORLANDC__
   #pragma option -O2                 /* restore to full optimization */
#endif

/**********************************************************************/
int
rmeqvlst2(eql,w,c)
EQVLST *eql;
char *w, *c;
{
int i, j, rc, used=eql->used;
char **words=eql->words, **clas =eql->clas , *op=eql->op;

   for(i=1,rc=0;i<used;i++){
      if(eqcmp(words[i],w)==0 && (c==CHARPN || eqcmp(clas [i],c)==0)){
         used--;
         eql->used--;
         free(words[i]);
         free(clas [i]);
         for(j=i;j<used;j++){
            words[j]=words[j+1];
            clas [j]=clas [j+1];
            op[j]   =op[j+1];
         }
         i--;
         rc++;
      }
   }
   return(rc);                                      /* number removed */
}                                                  /* end rmeqvlst2() */
/**********************************************************************/

/**********************************************************************/
int
rmeqvlst(eql,w,c)
EQVLST *eql;
char *w, *c;
{
int i, j, rc, used=eql->used;
char **words=eql->words, **clas =eql->clas , *op=eql->op;

   for(i=1,rc=0;i<used;i++){
      if(eqcmp(words[i],w)==0 && (c==CHARPN || eqcmp(clas [i],c)==0)){
         used--;
         eql->used--;
         for(j=i;j<used;j++){
            words[j]=words[j+1];
            clas [j]=clas [j+1];
            op[j]   =op[j+1];
         }
         i--;
         rc++;
      }
   }
   return(rc);                                      /* number removed */
}                                                   /* end rmeqvlst() */
/**********************************************************************/

/**********************************************************************/
void
clreqvlst(eql)
EQVLST *eql;
{
int u=eql->used-1;

	eql->words[1]=eql->words[u];
	eql->clas [1]=eql->clas [u];
	eql->op[1]=eql->op[u];
	eql->used=2;
}                                                  /* end clreqvlst() */
/**********************************************************************/

/**********************************************************************/
void
clreqvlst2(eql)
EQVLST *eql;
{
char **w, **c, *o;
int i, u;

	w=eql->words;
	c=eql->clas ;
	o=eql->op;
	u=eql->used-1;
	for(i=1;i<u;i++){
		free(c[i]);
		free(w[i]);
	}
	w[1]=w[i];
	c[1]=c[i];
	o[1]=o[i];
	eql->used=2;
}                                                 /* end clreqvlst2() */
/**********************************************************************/

/**********************************************************************/
EQVLST *
dupeqvlst(e)
EQVLST *e;
{
EQVLST *eql;
int i;
size_t  j;

   if((eql=(EQVLST *)calloc(1,sizeof(EQVLST)))!=EQVLSTPN){
      eql->logic=e->logic;
      for(i=0;*(e->words[i])!='\0';i++) ;
      i++;
      eql->used=0;
      eql->words=eql->clas =CHARPPN;
      eql->op=CHARPN;
      if((eql->words=(char **)calloc(i,sizeof(char *)))==CHARPPN ||
         (eql->clas =(char **)calloc(i,sizeof(char *)))==CHARPPN ||
         (eql->op   =(char *)malloc(i))==CHARPN
      ){
         goto zerr2;
      }
      eql->sz=i;
      for(i=0;*e->words[i]!='\0';i++){
         if((eql->words[i]=strdup(e->words[i]))==CHARPN ||
            (eql->clas [i]=strdup(e->clas [i]))==CHARPN
         ){
            goto zerr1;
         }
         eql->op[i]=e->op[i];
      }
      if((eql->words[i]=strdup(e->words[i]))==CHARPN ||
         (eql->clas [i]=strdup(e->clas [i]))==CHARPN
      ){
         goto zerr1;
      }
      eql->op[i]=e->op[i];
      eql->used=i+1;
      eql->qoff = e->qoff;
      eql->qlen = e->qlen;
      if (e->originalPrefix != CHARPN)
        eql->originalPrefix = strdup(e->originalPrefix);
      if (e->sourceExprs != CHARPPN)
        {
          for (j = 0; e->sourceExprs[j] != CHARPN; j++);
          if ((eql->sourceExprs = (char **)calloc(j + 1, sizeof(char *))) ==
              CHARPPN)
            goto zerr1;
          for (j = 0; e->sourceExprs[j] != CHARPN; j++)
            if ((eql->sourceExprs[j] = strdup(e->sourceExprs[j])) == CHARPN)
              goto zerr1;
        }
   }
   return(eql);
zerr1:
   if(eql->words[i]!=CHARPN) free(eql->words[i]);
   if(eql->clas [i]!=CHARPN) free(eql->clas [i]);
   for(--i;i>=0;i--){
      free(eql->words[i]);
      free(eql->clas [i]);
   }
   if (eql->sourceExprs != CHARPPN)
     {
       for (j = 0; eql->sourceExprs[j] != CHARPN; j++)
         {
           free(eql->sourceExprs[j]);
           eql->sourceExprs[j] = CHARPN;
         }
       free(eql->sourceExprs);
       eql->sourceExprs = CHARPPN;
     }
zerr2:
   return(closeeqvlst(eql));
}                                                  /* end dupeqvlst() */
/**********************************************************************/

/**********************************************************************/
EQVLST *
dupeqvstru(e)
EQVLST *e;
{
EQVLST *eql;
int i;

   for(i=0;*(e->words[i])!='\0';i++) ;
   if((eql=openeqvlst(i))!=EQVLSTPN){
      eql->words[i]=eql->words[0];                 /* move terminator */
      eql->clas [i]=eql->clas [0];
      eql->op[i]   =eql->op[0];
      eql->used=i+1;
      eql->logic=e->logic;
      for(i--;i>=0;i--){                             /* copy old ptrs */
         eql->words[i]=e->words[i];
         eql->clas [i]=e->clas [i];
         eql->op[i]   =e->op[i];
      }
   }
   return(eql);
}                                                 /* end dupeqvstru() */
/**********************************************************************/

