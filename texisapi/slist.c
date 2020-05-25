/***********************************************************************
** @(#)slist.c - MAW 10/90 - string list manager
**             - handles variable length lists of variable length strings
**             - stored list is both counted and "" terminated
**             - the count includes the "" terminator
***********************************************************************/
#include "stdio.h"
#include "string.h"
#include "errno.h"
#include "stdlib.h"
#include "os.h"
#include "slist.h"
#include "strcat.h"

#ifndef CPNULL
#  define CPNULL (char *)NULL
#endif

#define higher(a,b) ((a)>(b)?a:b)
/**********************************************************************/
SLIST *slopen()
{
SLIST *sl;

   if((sl=(SLIST *)calloc(1,sizeof(SLIST)))==SLISTPN){
      errno=ENOMEM;
   }else if((sl->s=(char **)calloc(20,sizeof(char *)))==(char **)NULL){
      free(sl);
      sl=SLISTPN;
      errno=ENOMEM;
   }else if((sl->buf=(char *)malloc(256))==CPNULL){
      free((char *)sl->s);
      free(sl);
      sl=SLISTPN;
      errno=ENOMEM;
   }else{
      *sl->buf='\0';
      sl->s[0]=sl->buf;
      sl->p=sl->buf+1;
      sl->cnt=1;
      sl->max=20;
      sl->bused=1;
      sl->bsz=256;
   }
   return(sl);
}                                                     /* end slopen() */
/**********************************************************************/
/* JMT 98-12-08 */
SLIST *sldup(osl)
SLIST *osl;
{
   SLIST *sl;
   int i;

   if(!osl)
   	return slopen();
   if((sl=(SLIST *)calloc(1,sizeof(SLIST)))==SLISTPN){
      errno=ENOMEM;
   }else if((sl->s=(char **)calloc(osl->max,sizeof(char *)))==(char **)NULL){
      free(sl);
      sl=SLISTPN;
      errno=ENOMEM;
   }else if((sl->buf=(char *)malloc(osl->bsz))==CPNULL){
      free((char *)sl->s);
      free(sl);
      sl=SLISTPN;
      errno=ENOMEM;
   }else{
      memcpy(sl->buf, osl->buf, osl->bused);
      for(i=0; i < osl->cnt; i++)
        sl->s[i] = sl->buf + (osl->s[i] - osl->buf);
      sl->p=sl->buf+(osl->p-osl->buf);
      sl->cnt=osl->cnt;
      sl->max=osl->max;
      sl->bused=osl->bused;
      sl->bsz=osl->bsz;
   }
   return(sl);
}

/**********************************************************************/
SLIST *slclose(sl)
SLIST *sl;
{
   if(sl->buf!=CPNULL)      free(sl->buf);
   if(sl->s!=(char **)NULL) free((char *)sl->s);
   free(sl);
   return(SLISTPN);
}                                                    /* end slclose() */
/**********************************************************************/

/**********************************************************************/
void slwipe(sl)
SLIST *sl;
{
   sl->p=sl->buf+1;
   sl->cnt=1;
   sl->bused=1;
}                                                     /* end slwipe() */
/**********************************************************************/

/**********************************************************************/
char *sldel(sl,s)                                     /* MAW 04-29-97 */
SLIST *sl;
char *s;
{
int i, j, n;
char *rc=CPNULL;

   n=sl->cnt-1;
   for(i=0;i<n;i++)
   {
      if(strcmp(sl->s[i],s)==0)
      {
         rc=s;
         sl->cnt-=1;
         n--;
         for(j=i;j<sl->cnt;j++)
            sl->s[j]=sl->s[j+1];
         /* wtf - might want to reclaim buffer space too if speed not matter */
         i--;
      }
   }
   return(rc);
}                                                      /* end sldel() */
/**********************************************************************/

/**********************************************************************/
char *slfind(sl,s)                                    /* MAW 05-07-97 */
SLIST *sl;
char *s;
{
int i, n;

   for(i=0,n=sl->cnt-1;i<n;i++)
   {
      if(strcmp(sl->s[i],s)==0)
         return(sl->s[i]);
   }
   return(CPNULL);
}                                                     /* end slfind() */
/**********************************************************************/

/**********************************************************************/
char *sladdclst(sl,n,lst)                   /* add counted list to sl */
SLIST *sl;
int n;
char **lst;
{
int l, i;
char *ob, **a;

   if(sl->cnt+n>=sl->max){                           /* grow ptr list */
      sl->max+=higher(20,n);
      sl->s=(char **)realloc(sl->s,sl->max*sizeof(char *));
      if(sl->s==(char **)NULL) goto zerr;
   }
   for(i=l=0;i<n;i++){
      l+=strlen(lst[i])+1;
   }
   if(sl->bused+l>sl->bsz){                            /* grow buffer */
      ob=sl->buf;
      sl->bsz+=higher(l,256);
      sl->buf=(char *)realloc(sl->buf,sl->bsz);
      if(sl->buf==CPNULL) goto zerr;
      if(sl->buf!=ob){          /* readjust ptr list for moved buffer */
         for(i=0,a=sl->s;i<sl->cnt;i++,a++){
            *a=sl->buf+(*a-ob);
         }
         sl->p=sl->buf+(sl->p-ob);
      }
   }
   for(;n>0;n--,lst++,sl->cnt++){
      sl->s[sl->cnt-1]=sl->p;
      strcpy(sl->p,*lst);
      sl->p+=strlen(*lst)+1;
   }
   sl->s[sl->cnt-1]=sl->buf;/* the "" is always at the front of the buffer */
   sl->bused+=l;
   return(*(lst-1));                             /* last string added */
zerr:
   sl->cnt=sl->max=0;
   sl->bsz=sl->bused=0;
   errno=ENOMEM;
   return(CPNULL);
}                                                  /* end sladdclst() */
/**********************************************************************/

/**********************************************************************/
char *sladd(sl,s)
SLIST *sl;
char *s;
{
   return(sladdclst(sl,1,&s));
}                                                      /* end sladd() */
/**********************************************************************/

/**********************************************************************/

char *
sladdslst(sl, nsl, unique)
SLIST *sl;
SLIST *nsl;
int unique;
{
	char *rc = NULL;
	int i;

	if(!unique)
	{
		return sladdclst(sl, nsl->cnt - 1, nsl->s);
	}
	for(i=0; i < nsl->cnt-1; i++)
	{
		if(!slfind(sl, nsl->s[i]))
		{
			rc = sladdclst(sl, 1, &nsl->s[i]);
		}
	}
	return rc;
}

/**********************************************************************/

/*
 * Rename replaces leading string before "." with newname
 * Used by texis for Table.Column renames
 */

/**********************************************************************/

SLIST *
slistrename(olist, newname)
SLIST *olist;
char *newname;
{
	SLIST *rc;
	char *nn, *t;
	int i;

	if(!olist)
		return olist;
	rc = slopen();
	for(i = 0; i < olist->cnt - 1; i++)
	{
		t = strchr(olist->s[i], '.');
		if(t)
		{
			nn = TXstrcat2(newname, t);
			sladd(rc, nn);
			free(nn);
		}
		else
			sladd(rc, olist->s[i]);
	}
	return rc;
}

/**********************************************************************/
#ifdef TEST

int main ARGS((int argc,char **argv));
/**********************************************************************/
int main(argc,argv)
int argc;
char **argv;
{
SLIST *sl;
int i;
char **s;

   if((sl=slopen())==SLISTPN){
      perror("slopen");
   }else{
      if(sladdclst(sl,argc,argv)==CPNULL){
         perror("sladd");
      }else{
         for(;argc>0;argc--,argv++){
            if(sladd(sl,*argv)==CPNULL){
               perror("sladd");
               break;
            }
         }
         if(argc==0){
            i=slcnt(sl)-1;
            s=slarr(sl);
            for(;i>=0;i--){
               printf("\"%s\"\n",s[i]);
            }
            printf("--- delete some ---\n");
            sldel(sl,"slist");
            sldel(sl,"bob");
            sldel(sl,"last");
            sldel(sl,"first");
            sldel(sl,"");
            i=slcnt(sl)-1;
            s=slarr(sl);
            for(;i>=0;i--){
               printf("\"%s\"\n",s[i]);
            }
         }
      }
      slclose(sl);
   }
   return(0);
}                                                       /* end main() */
/**********************************************************************/
#endif                                                        /* TEST */


