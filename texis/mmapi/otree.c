#include "stdio.h"
#include "stdlib.h"
#include "errno.h"
#include "string.h"
#include "ctype.h"
#ifdef MSDOS
#else
#  ifdef UMALLOC3X
#     include <malloc.h>
#  endif
#endif
#include "os.h"
#include "otree.h"
#define TSCMP(a) (*tr->cmp)(s,(a))

#define BALANCE 1                                  /* use balanced tree */

/*
#define TEST 1
*/

/*

              jack
       be              nimble
         candle  jumped      quick
                          over   the

*/

/**********************************************************************/
#ifdef TEST
#define DTMAX   0x1000
#define DTBITS  13

int tdepth;
int tcnt;

howdeep(x,z)
OTBR *x,*z;
{
 ++tcnt;
 if(tcnt>tdepth) tdepth=tcnt;
 if(x->l!=z) howdeep(x->l,z);
 if(x->h!=z) howdeep(x->h,z);
 --tcnt;
}

/**********************************************************************/

int
onbits(b)
int b;
{
 int i,cnt=0;
 for(i=1;i<DTMAX+1;i<<=1) if(i&b) ++cnt;
 return(cnt);
}

/**********************************************************************/
/*
                                          0
            011                             1
            110                           2   3
            111                          4 5 6 7
            100                         8 9 a b c

           7     |       1
           3  7  |   2       3
           1  3  | 4   5   6   7
           0  1  |8 9 a b c e f g
  @@
*/

void
prtnode(x,z,v)
OTBR *x;
OTBR *z;
int v;
{
 int i;
 unsigned long n=(unsigned long)v;
 for(i=DTBITS;(n&DTMAX)==0 && n;n<<=1,i--);
 for(;i && n;n<<=1,i--)
    {
     if(n&DTMAX)  x=x->h;
     else         x=x->l;
     if(x==z) break;
    }
 fputs(x->s,stdout);
}

/**********************************************************************/

static int nodump=0;

void
dumptree(tr,x)
OTREE *tr;
OTBR *x;
{
 int i,j,nodes,bits;
 if(nodump) return;
 tdepth=0;
 howdeep(tr->root,tr->z);
 tdepth--;
 nodes=1<<tdepth;
 for(i=0;i<nodes;i++)
    {
     if(bits=onbits(i)==1)
         {
          putchar('\n');
          tdepth--;
          for(j=1;j<(1<<tdepth);j++)
              putchar(' ');
         }
     else
         {
          for(j=1;j<(1<<(tdepth+1));j++)
              putchar(' ');
         }
     prtnode(x,tr->z,i);
    }
 putchar('\n');
}

#else
# define dumptree(x,y)
#endif                                                        /* TEST */
/**********************************************************************/

static void freets ARGS((OTREE *tr,OTBR *ts));

static void
freets(tr,ts)
OTREE *tr;
OTBR *ts;
{
 if(ts!=tr->z)
    {
     if(ts->h!=ts) freets(tr,ts->h);
     if(ts->l!=ts) freets(tr,ts->l);
     free(ts);
    }
}

/**********************************************************************/

OTREE *
closeotree(tr)
OTREE *tr;
{
 if(tr!=OTREEPN)
    {
     if(tr->root!=OTBRPN)
         freets(tr,tr->root);
     free(tr);
    }
 return(OTREEPN);
}

/**********************************************************************/

OTREE *
openotree(cmp,low)
int (*cmp)ARGS((void *,void *));
void *low;  /* will compare low to anything that may be added to tree */
{
 OTREE *tr;

 errno=ENOMEM;
 if((tr=(OTREE *)calloc(1,sizeof(OTREE)))==OTREEPN) return(OTREEPN);
 if((tr->root=(OTBR *)calloc(1,sizeof(OTBR)))==OTBRPN){
    free(tr);
    return(OTREEPN);
 }

 tr->cnt=tr->dupcnt=0;
 tr->allowdups=1;
 tr->allowTreeMods=1;
 tr->cmp=cmp;
 tr->afunc=NULL;
 tr->dfunc=NULL;
 tr->wfunc=NULL;

 tr->z=&tr->zdummy;
 tr->z->l=tr->z->h=tr->z;                             /* PBR 11-08-91 */
 tr->z->red=0;
#ifdef TEST
 tr->z->s=(void *)"*";               /* only used for debug printouts */
#else
 tr->z->s=low;                       /* only used for debug printouts */
#endif

 tr->root->h=tr->root->l=tr->z;                       /* PBR 11-08-91 */
 tr->root->s=low;
 tr->root->red=0;

 return(tr);
}

/**********************************************************************/

#if BALANCE

static OTBR *rotate ARGS((OTREE *tr,void *s,OTBR *x));

static OTBR *
rotate(tr,s,x)                               /* page 225 sedgewick v2 */
OTREE *tr;
void *s;
OTBR *x;
{
 OTBR *c,*gc;

 if(TSCMP(x->s)<0) c=x->l;
 else              c=x->h;

 if(TSCMP(c->s)<0) { gc=c->l;c->l=gc->h;gc->h=c; }
 else              { gc=c->h;c->h=gc->l;gc->l=c; }

 if(TSCMP(x->s)<0) x->l=gc;
 else              x->h=gc;

 return(gc);
}

/**********************************************************************/

static OTBR *split ARGS((OTREE *tr,void *s,OTBR *gg,OTBR *g,OTBR *p,OTBR *x));

static OTBR *
split(tr,s,gg,g,p,x)                              /* page 226 sedgewick v2 */
OTREE *tr;
void *s;
OTBR *gg,*g,*p,*x;
{
 x->red=1;x->l->red=0;x->h->red=0;
 if(p->red)
    {
     dumptree(tr,tr->root);
     g->red=1;
     if((TSCMP(g->s)<0) != (TSCMP(p->s)<0))
         {
                  
          p=rotate(tr,s,g);
          dumptree(tr,tr->root);
         }
     x=rotate(tr,s,gg);
     x->red=0;
     dumptree(tr,tr->root);
    }
 tr->root->h->red=0;
 return(x);
}

#endif
/**********************************************************************/

void *
findotree(tr,s)
OTREE *tr;
void *s;
{
 int cmp;
 OTBR *x=tr->root, *z=tr->z;
 do {
     cmp=TSCMP(x->s);
     if(cmp==0) return(x->s);
     else
     if(cmp<0)  x=x->l;
     else       x=x->h;
    } while(x!=z);
 return(VOIDPN);
}

/**********************************************************************/

int
addotree2(tr,s,ptr)                          /* page 221 sedgewick v2 */
OTREE *tr;
void  *s;
void **ptr;
{
 OTBR *gg,*g,*p,*z,*x,*nx;
 int cmp;

 errno=ENOMEM;                                          /* MAW 04-18-91 */
 z=tr->z;
 g=p=x=tr->root;

 do {
     gg=g;g=p;p=x;
     cmp=TSCMP(x->s);
     if(cmp==0)
         {
          tr->dupcnt+=1;
          if(tr->dfunc!=NULL)
              {
               switch((*tr->dfunc)(&x->s,s,tr->darg))
                   {
                    case OTR_ERROR: return(-1);
                    case OTR_IGNORE:
                         {
                          if(ptr!=(void **)NULL) *ptr=x->s;
                          return(0);
                         }
                   }
              }
          else if(!tr->allowdups)
              {
               if(ptr!=(void **)NULL) *ptr=x->s;
               return(0);
              }
          x=x->h;
          break;                                            /* add it */
         }
     else
     if(cmp<0)  x=x->l;
     else       x=x->h;
#    if  BALANCE
          if(tr->allowTreeMods && x->l->red && x->h->red)
             x=split(tr,s,gg,g,p,x);
#    endif
    } while(x!=z);

 if(!tr->allowTreeMods)
    {
     /* trying to create new node when new nodes aren't allowed */
     return(-1);
    }
 if((nx=(OTBR *)calloc(1,sizeof(OTBR)))==OTBRPN)/* alloc and init new node */
    return(-1);
 if(tr->afunc!=NULL)
    {
     switch((*tr->afunc)(&s,tr->aarg))
         {
          case OTR_ERROR: free((char *)nx); return(-1);
          case OTR_IGNORE:
              {
               if(ptr!=(void **)NULL) *ptr=s;
               free((char *)nx);
               return(0);
              }
         }
    }
 nx->l=z;
 nx->h=x;
 nx->s=s;
 nx->red=0;
 if(ptr!=(void **)NULL) *ptr=s;
 x=nx;
 if(cmp<0) p->l=x;
 else      p->h=x;
 tr->cnt+=1;
#if  BALANCE
    x=split(tr,x->s,gg,g,p,x);
#endif
 dumptree(tr,tr->root);
 return(1);
}

/**********************************************************************/
/* helper function used by countTree */
int countTreeBranch(tr,ts)
OTREE *tr;
OTBR *ts;
{
  if(ts==tr->z)
    {
      return 0;
    }
  return 1
    + countTreeBranch(tr, ts->l)
    + countTreeBranch(tr, ts->h);
}

/* debugging function counts the number of nodes in a tree, not used
 * in normal program execution*/
int
countTree(tr)
OTREE *tr;
{
  if(tr->root->s==VOIDPN)
    {
      return 0;
    }
  return 0
    + countTreeBranch(tr, tr->root->l)
    + countTreeBranch(tr, tr->root->h);
}

/* helper function for dumpAndWalkTree */
static int
dumpAndWalkBranch(tr,ts, buffer, index)
OTREE *tr;
OTBR *ts;
char *buffer;
int index;
{
  int indexNext = index+1;
  if(ts==tr->z)
    return(1);
  buffer[index]='l';
  if(dumpAndWalkBranch(tr,ts->l, buffer, indexNext)==0)
    {
      buffer[index]=' ';
      return 0;
    }
  buffer[index]='*';
  if(ts->red)
    {
      printf("%s R ", buffer);
    }
  else 
    {
      printf("%s - ", buffer);
    }
  if((*tr->wfunc)(ts->s,tr->warg)==0)
    {
      buffer[index]=' ';
      return 0;
    }
  buffer[index]='h';
  if(dumpAndWalkBranch(tr,ts->h, buffer, indexNext)==0)
    {
      buffer[index]=' ';
      return(0);
    }
  buffer[index]=' ';
  return(1);
}

/* like walkotree, but also dumps the path to get to the node and
 * if it's red/black before invoking the wfunc on the node.
 * The output doesn't end in a newline, assumes the wfunc will print
 * some details about that node.  E.g. walking
 * a tree of EQVLST looks like:
 * hhlhhh*         R : msds,safety data sheet
 * hh*             - : national bioenergy center,nbc
 * hhhlll*         R : national center for photovoltaics,ncpv
 * hhhll*          - : national environmental policy act,nepa
 * hhhllh*         R : national wind technology center,nwtc
 */
int
dumpAndWalkTree(tr)
OTREE *tr;
{
  char buffer[1024];
  if(tr->root->s!=VOIDPN && tr->wfunc!=NULL)
    {
      strncpy(buffer, "               ", 16);
      /* l is always empty, just do h at depth 0 */
      if(dumpAndWalkBranch(tr,tr->root->h, buffer, 0)==0)
        {
          return(0);
        }
    }
  return(1);
}

/**********************************************************************/

static int wrtts ARGS((OTREE *tr,OTBR *ts));

static int
wrtts(tr,ts)
OTREE *tr;
OTBR *ts;
{
 if(ts==tr->z) return(1);
 if(wrtts(tr,ts->l)==0  ||
    (*tr->wfunc)(ts->s,tr->warg)==0 ||
    wrtts(tr,ts->h)==0) return(0);
 return(1);
}

/**********************************************************************/

int
walkotree(tr)
OTREE *tr;
{
 if(tr->root->s!=VOIDPN && tr->wfunc!=NULL)
   {
    if(wrtts(tr,tr->root->l)==0 ||
       wrtts(tr,tr->root->h)==0) return(0);
   }
 return(1);
}

/**********************************************************************/
#define OLS struct ols_struct
OLS {
 void **lst;
 int i;
};

static int addlst ARGS((void *cur,void *arg));

static int
addlst(cur,arg)
void *cur, *arg;
{
OLS *ols=(OLS *)arg;

 ols->lst[ols->i]=cur;
 ols->i+=1;
 return(1);
}

/**********************************************************************/

void **
otree2lst(tr,n)
OTREE *tr;
int *n;
{
OLS ols;
int (*owf)ARGS((void *,void *));
void *owa;

 ols.i=(int)tr->cnt;
 if((ulong)ols.i!=tr->cnt) return((void **)NULL);/* make sure tr->cnt<=MAXINT */
 if((ols.lst=(void **)calloc((int)tr->cnt+1,sizeof(void *)))!=(void **)NULL)
    {
     ols.i=0;
     owf=tr->wfunc;              /* save current walkotree() callback */
     owa=tr->warg;
     tr->wfunc=addlst;                             /* set my callback */
     tr->warg=(void *)&ols;
     if(!walkotree(tr))
         {
          free((char *)ols.lst);
          ols.lst=(void **)NULL;
         }
     else
         {
          ols.lst[ols.i]=tr->root->s;/* terminate w/their low value */
          if(n!=(int *)NULL) *n=ols.i;          /* but don't count it */
         }
     tr->wfunc=owf;                               /* restore callback */
     tr->warg=owa;
    }
 return(ols.lst);
}

/**********************************************************************/
#ifdef TEST

int
cmp(p1,p2)              /* compare 2 objects in the style of strcmp() */
void *p1, *p2;
{
 char *s1=(char *)p1, *s2=(char *)p2;
 return(strcmpi(s1,s2));
}

/**********************************************************************/

int
wrt(cur,arg)                                       /* write an object */
void *cur;                                      /* object in question */
void *arg;                                 /* arbitrary user argument */
{
 char *s=(char *)cur;
 FILE *fp=(FILE *)arg;
 fputs(s,fp);
 fputc('\n',fp);
 return(!ferror(fp));
}

/**********************************************************************/

int
fre(cur,arg)                                        /* free an object */
void *cur;                                      /* object in question */
void *arg;                                 /* arbitrary user argument */
{
 char *s=(char *)cur;
 free(s);
 return(1);
}

/**********************************************************************/

static int dupmode=OTR_ADD;

int
dup(cur,newadd,arg)                     /* handle a duplicated object */
void **cur;                             /* object in tree being dup'd */
void *newadd;                        /* object attempting to be added */
void *arg;                                 /* arbitrary user argument */
{
 char **s=(char **)cur, *new=(char *)newadd;
 if(dupmode==OTR_IGNORE) free(new);
 return(dupmode);
}
/*setotdfunc(tr,dup);*/
/* if tou use the above method, don't free the object when addotree()==0 */

/**********************************************************************/

main(argc,argv)
int argc;
char *argv[];
{
 char s[80];
 OTREE *tr;
 FILE *fh;
 void **lst;
 int i, n, mklst=0;
 for(i=1;i<argc && *argv[i]=='-';i++)
    {
     switch(*(argv[i]+1))
         {
          case 'n': nodump=1; break;
          case 'd': dupmode=OTR_IGNORE; break;  /* allow dups in tree */
          case 'l': mklst=1; break;               /* test otree2lst() */
         }
    }
 if(i<argc)
    fh=fopen(argv[i],"r");
 else fh=stdin;

 if((tr=openotree(cmp,""))==OTREEPN) fputs("no mem for opentree()\n",stderr);
 else
    {
     if(dupmode==OTR_IGNORE) setotdups(tr,0);
     while(fgets(s,sizeof(s),fh)!=CHARPN)
        {
         char *p, *a, *ptr;
         int rc;
         rc=strlen(s)-1;
         if(s[rc]=='\n') s[rc]='\0';
         if((p=malloc(rc+2))==CHARPN)               /* a copy to keep */
             {
              fprintf(stderr,"out of mem for \"%s\": aborting",s);
              break;
             }
         strcpy(p,s);
         rc=addotree(tr,p);
         if(rc<0)
             {
              fprintf(stderr,"error in addotree() for \"%s\": aborting",p);
              break;
             }
         else if(rc==0)
             {
              free(p);
              /*fprintf(stderr,"dup: \"%s\"\n",s);*/
             }
        }
     printf("%d entries, %d dups\n",getotcnt(tr),getotdupcnt(tr));
     if(mklst)
         {
          if((lst=otree2lst(tr,&n))==(void **)NULL)
              {
               fprintf(stderr,"no mem for otree2lst()\n");
              }
          else
              {
               printf("%d entries\n",n);
               for(i=0;i<n;i++)         /* display and free the items */
                  {
                   puts((char *)lst[i]);
                   free((char *)lst[i]);
                  }
               puts((char *)lst[i]);         /* also print terminator */
               free((char *)lst);
              }
         }
     else
         {
                                     /* walk the tree to print it out */
          setotwfunc(tr,wrt);
          setotwarg(tr,(void *)stdout);
          walkotree(tr);
                                  /* walk the tree to free my objects */
          setotwfunc(tr,fre);
          walkotree(tr);
         }
     closeotree(tr);
    }
 if(fh!=stdin) fclose(fh);
 exit(0);
}

/**********************************************************************/
#endif                                                        /* TEST */

