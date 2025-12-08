#include "txcoreconfig.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"
#include "sys/types.h"
#include "errno.h"
#ifdef MSDOS
#  define LOWIO
#endif
#include "os.h"
#include "mmsg.h"
#include "eqvint.h"

/* MAW 08-25-94 - add "(eqlist)" handling, EQV_CPPM */
/* MAW 01-19-96 - fix eqvfmt() to not escape backslash in rex */
/**********************************************************************/

static CONST char logics[]={ EQV_CSET, EQV_CAND, EQV_CNOT, EQV_CISECT, '\0' };/* logic chars, first is default */
static CONST char allowpunct[]={ EQV_CREX, EQV_CNPM, EQV_CPPM, '\0' };/* special pm trigger chars */
#define isspecial(a) ((a)==EQV_CREX||(a)==EQV_CXPM||(a)==EQV_CNPM)

static CONST char       CannotAllocMem[] = "Cannot allocate memory";
/* EmptyString must not be CONST as it will be touched */
static char             EmptyString[] = "";

/**********************************************************************/

static char *fixlogic ARGS((CONST char *a_s));
/**********************************************************************/
static char *
fixlogic(a_s)              /* standardize the logic for a word/phrase */
CONST char *a_s;
{
  CONST char    *s;
char *str, *d;
int n, addlogic;

   if(a_s==CHARPN){                             /* make an empty item */
      str=(char *)malloc(1);
      if(str!=CHARPN) *str='\0';
   }else{
      n=strlen(a_s)+1;                            /* count mem needed */
      if(n==1 || strchr(logics,*a_s)==CHARPN){/* have to add logic char */
         addlogic=1;
         n++;
         s=a_s;
      }else{
         addlogic=0;
         s=a_s+1;
      }
      if(*s==EQV_CXPM){                 /* guarantee 2 digits for xpm */
         n+=2;
         ++s;
         if(isdigit(*s)) n--;
         ++s;
         if(isdigit(*s)) n--;
      }
      str=(char *)malloc(n);
      if(str!=CHARPN){                            /* store fixed item */
         s=a_s;
         d=str;
         if(addlogic){                              /* add logic char */
            *(d++)=logics[0];
         }else{
            *(d++)= *(s++);
         }
         if(*s==EQV_CXPM){              /* guarantee 2 digits for xpm */
            *(d++)= *(s++);
            if(isdigit(*s)){
               if(!isdigit(*(s+1))){             /* prepend leading 0 */
                  *(d++)='0';
               }
            }else{                       /* insert default percentage */
               *(d++)='0'+EQV_XPMDEFPCT/10;/* wtp - assume contiguous 0-9 */

               *(d++)='0'+EQV_XPMDEFPCT%10;
            }
         }
         strcpy(d,s);                          /* copy rest of string */
      }
   }
   return(str);
}                                                   /* end fixlogic() */
/**********************************************************************/

/*
** LIT is a coding convience for eqvpq() so it doesn't need to know
** anything about it. eqvpq() just makes an array of ptrs to LIT
** and calls fixlogic() to create/handle internals of LIT.
*/
#define LIT   char                                  /* List Item Type */
#define LITPN (LIT *)NULL

/**********************************************************************/
          /* MAW 11-27-95 - differentiate end punct from middle punct */
#define ISPUNC(a) ((a)==',')
#define ISEPUNC(a) (ISPUNC(a) || (a)=='.' || (a)=='?')
#define ISDELIM(a) (a < 128 && (isspace((byte)(a)) || ISPUNC(a)))

int
eqvpq(in_query, a_lst, a_n, originalPrefixedTerms, isects, a_setqoffs, a_setqlens)
char *in_query;
LIT ***a_lst;
char    ***originalPrefixedTerms;  /* (out, opt.) alloc'd list, size `*a_n' */
int *a_n, *isects;
int     **a_setqoffs;   /* (out) alloc'd list of sets' query offsets */
int     **a_setqlens;   /* (out) alloc'd list of sets' query lengths */
/* Parses a query.  Always (?) called with query stripped of `w/...' ops.
 * `*originalPrefixedTerms' will be list of original terms from query,
 * including their set logic, tilde, open-parenthesis, and/or
 * pattern-matcher-char prefixes (if any).
 */
{
  /* MAW 03-10-99 change all to byte so all isspace work */
static CONST char       fn[] = "eqvpq";
int n=0;                   /* # of items in lst not counting terminator */
int     inDoubleQuotes, inParens, i;
LIT **lst=(LIT **)NULL;                /* array of ptrs to List Items */
char    **orgTerms = CHARPPN;
int     *setqoffs = INTPN, *setqlens = INTPN, delimSkip;
byte *query=(byte *)in_query;
byte *s, *d, *qy=BYTEPN, *w, *tokStart;

   for(;ISDELIM(*query);query++) ;
   delimSkip = query - (byte *)in_query;
   qy=(byte *)strdup((char *)query);
   if(qy==BYTEPN) goto zerr;
   for(i=0;i<2;i++){                  /* 2 passes: 0==count, 1==store */
      if(i==1){                                         /* store pass */
         lst=(LIT **)calloc(n+1,sizeof(LIT *));
         if(lst==(LIT **)NULL) goto zerr;
         orgTerms = (char **)calloc(n + 1, sizeof(char *));
         if (orgTerms == CHARPPN) goto zerr;
         if(isects!=(int *)NULL) *isects=(-1);
         setqoffs = (int *)calloc(n + 1, sizeof(int));
         if (setqoffs == INTPN)
           {
             putmsg(MERR + MAE, fn, CannotAllocMem);
             goto zerr;
           }
         setqlens = (int *)calloc(n + 1, sizeof(int));
         if (setqlens == INTPN)
           {
             putmsg(MERR + MAE, fn, CannotAllocMem);
             goto zerr;
           }
      }
      inDoubleQuotes = 0;
      inParens = 0;
      for(tokStart=s=query,d=w=qy,n=0;*s!='\0';s++){
         switch(*s){
         case '\\': if(*(s+1)=='"' || *(s+1)==' '){
                        /* \ only effects " and space, otherwise keep */
                       ++s;
                    }else if(*(s+1)!='\0'){              /* watch EOS */
                       *(d++)= *(s++);
                    }
                    *(d++)= *s;
                    break;
         case '"':  if (inDoubleQuotes) inDoubleQuotes = 0;
                    else inDoubleQuotes = 1;
                    break;
         case EQV_CPPM:                               /* `(' MAW 08-25-94 */
                    if(d!=w && (d!=w+1 || strchr(logics,*w)==CHARPN)){
                       goto zregular;        /* only at begin of term */
                    }
                    if (!inParens) inParens = 1;
                    *(d++)= *s;
                    break;
         case EQV_CEPPM:                              /* `)' MAW 08-25-94 */
                    if (inParens)
                      {
                        inParens = 0;
                        break;
                      }
                    /* nobreak; */
zregular:;
         default:   if(ISDELIM(*s)){
                       if (inDoubleQuotes || inParens) *(d++) = *s;
                       else{                              /* word sep */
                          if(ISPUNC(*s)){
                          /* don't do punct processing in non-english */
                             if(strchr(allowpunct,*w)!=CHARPN ||
                                (strchr(logics,*w)!=CHARPN &&
                                 strchr(allowpunct,*(w+1))!=CHARPN)
                             ){
                                *(d++)= *s;
                                break;
                             }
                          }
                          *d='\0';
                          if(*w==EQV_CISECT && isdigit((int)(*(w+1))) && isects!=(int *)NULL){
                                   /* special handling for intersects */
                             *isects=atoi((char *)w+1);
                          }else{
                             if(i==1){                  /* store pass */
                                orgTerms[n] = strdup((char *)w);
                                if (orgTerms[n] == CHARPN) goto zerr;
                                lst[n]=fixlogic((char *)w);
                                if(lst[n]==LITPN) goto zerr;
                                /* KNG 20080903 store offset into original
                                 * query of this set, for APICP.setqoffs:
                                 */
                                setqoffs[n] = delimSkip + (tokStart - query);
                                setqlens[n] = s - tokStart;
                             }
                             n++;
                          }
                          w=d;                /* ptr to begin of word */
                                            /* skip intervening space */
                          for(++s;ISDELIM(*s);s++) ;
                          tokStart = s;
                          s--;              /* account for ++ of loop */
                       }
                    }else{
                       *(d++)= *s;
                    }
                    break;
         }
      }
      *d='\0';
      if(*w!='\0'){
         if(*w==EQV_CISECT && isdigit((int)(*(w+1))) && isects!=(int *)NULL){
                                   /* special handling for intersects */
            *isects=atoi((char *)w+1);
         }else{
            if(i==1){                                   /* store pass */
               orgTerms[n] = strdup((char *)w);
               if (orgTerms[n] == CHARPN) goto zerr;
               lst[n]=fixlogic((char *)w);
               if(lst[n]==LITPN) goto zerr;
               /* KNG 20080903 store offset into original
                * query of this set, for APICP.setqoffs:
                */
               setqoffs[n] = delimSkip + (tokStart - query);
               setqlens[n] = s - tokStart;
            }
            n++;
         }
      }
   }
   free(qy);
   if(n>0 && strchr(allowpunct,lst[n-1][1])==CHARPN)
   {  /* MAW 11-27-95 - check for end punct on last term only if word */
      i=strlen(lst[n-1])-1;
      // this whole section should be re-examined.  What is the point of removing trailing punct for "wtf?" and not "wtf? foo" --ajf 2025-12-08
      if(ISEPUNC(lst[n-1][i]))
      {
         if(i<2) // remove single trailing punctuation like "wtf ?" --ajf 2025-12-08
         {
             free(lst[n-1]);
             lst[n-1]=lst[n];
             n--;
         }
         else
             lst[n-1][i]='\0';
      }
   }
   lst[n]=fixlogic(CHARPN);                 /* make a list terminator */
   if(lst[n]==LITPN) goto zerr;
   *a_lst=lst;
   if(a_n!=(int *)NULL) *a_n=n;
   if (originalPrefixedTerms != CHARPPPN)
     *originalPrefixedTerms = orgTerms;
   else
     {
       for (i = 0; i < n; i++)
         if (orgTerms[i] != CHARPN) free(orgTerms[i]);
       free(orgTerms);
     }
   orgTerms = CHARPPN;
   if (a_setqoffs != INTPPN) *a_setqoffs = setqoffs;
   else free(setqoffs);
   setqoffs = INTPN;
   if (a_setqlens != INTPPN) *a_setqlens = setqlens;
   else free(setqlens);   
   setqlens = INTPN;
   return(0);
zerr:
   if(lst!=(LIT **)NULL){
      for(--n;n>=0;n--) free((char *)lst[n]);
      free((char *)lst);
   }
   if (orgTerms != CHARPPN)
     {
       for (i = 0; i < n; i++)
         if (orgTerms[i] != CHARPN) free(orgTerms[i]);
       free(orgTerms);
       orgTerms = CHARPPN;
     }
   if (setqoffs != INTPN) free(setqoffs);
   if (setqlens != INTPN) free(setqlens);
   if (originalPrefixedTerms != CHARPPPN)
     *originalPrefixedTerms = CHARPPN;
   if (a_setqoffs != INTPPN) *a_setqoffs = INTPN;
   if (a_setqlens != INTPPN) *a_setqlens = INTPN;
   if(qy!=BYTEPN) free(qy);
   return(-1);
}                                                   /* end eqvpq() */

#undef ISDELIM
/**********************************************************************/

#ifdef WORDSONLY
/**********************************************************************/
int
eqvparserec(rec)
EQVREC *rec;
{
   rec->eql->logic=EQV_CINVAL;
   rec->eql->words[0]=rec->buf;
   rec->eql->words[1]=rec->buf+rec->lena;
   return(1);
}                                                /* end eqvparserec() */
/**********************************************************************/
#else
/**********************************************************************/
int
eqvparserec(rec)      /* parse the read in record into the words list */
EQVREC *rec;
{
int wlen=0, clen=0, n;
char *p, *d, *w, op=EQV_CINVAL, sop;
char *c=EmptyString;

   rec->eql->logic=EQV_CINVAL;
   for(w=p=d=rec->buf,n=0;*p!='\0';p++,d++){
      switch(*p){
      case EQV_CESC:
         *d= *(++p);
         break;
      case EQV_CCLAS:
         wlen=(int)(d-w);
         d=p;
         c=p+1;
         clen=(-1);
         break;
      case EQV_CREPL:
         if(!isalnum(*(p+1))){         /* take rest of line literally */
            if(clen==(-1)) clen=(int)(d-c);
            else           wlen=(int)(d-w);
            sop= *p;
            w[wlen]='\0';
            c[clen]='\0';
            rec->eql->words[n]=w;
            rec->eql->clas [n]=c;
            rec->eql->op[n]=op;
            n++;
            op=sop;
            w= ++p;
            for(;*p!='\0';p++) ;                       /* skip to end */
            p--;                               /* cause loop to break */
            d=p;
            break;
         }
         /*nobreak;*/
      case EQV_CADD :
      case EQV_CDEL :
         if(clen==(-1)) clen=(int)(d-c);
         else           wlen=(int)(d-w);
         sop= *p;
         w[wlen]='\0';
         c[clen]='\0';
         rec->eql->words[n]=w;
         rec->eql->clas [n]=c;
         rec->eql->op[n]=op;
         n++;
         op=sop;
         w=p+1;
         d=p;
         break;
      default:
         *d= *p;
         break;
      }
   }
   if(clen==(-1)) clen=(int)(d-c);
   else           wlen=(int)(d-w);
   w[wlen]='\0';
   c[clen]='\0';
   rec->eql->words[n]=w;
   rec->eql->clas [n]=c;
   rec->eql->op[n]=op;
   n++;
   rec->eql->words[n]=rec->eql->clas [n]=p;
   rec->eql->used=n+1;
   return(0);
}                                                /* end eqvparserec() */
/**********************************************************************/
#endif                                                   /* WORDSONLY */

/**********************************************************************/
EQVLST *
eqvparse(buf, forceLit)
char *buf;
int forceLit;
{
EQVLST *eql;
int wlen=0, clen=0;
char *p, *d, *w, op=EQV_CINVAL, sop;
char *c=EmptyString;
CONST char *msg="";
static CONST char no_word[]="No word", no_clas []="No class";
static CONST char Fn[]="eqvparse";

   if((eql=openeqvlst(0))==EQVLSTPN) goto zmemerr;
   for(w=p=d=buf;*p!='\0' && *p!='\n';p++,d++){
      switch(*p){
      case EQV_CESC:
         if(forceLit){
            /* Include the backslash character with the following character
             * so we sort the same way as texis; see bug 4516  comment #3 / #4 */
            *d = *p; 
            p++; d++;
            *d = *p;
         }else{
           *d= *(++p); /* skip the backslash, take next character literally */
         }
         break;
      case EQV_CCLAS:
         wlen=(int)(d-w);
         d=p;
         c=p+1;
         clen=(-1);
         if(wlen==0){ msg=no_word; goto zsynerr; }
         break;
      case EQV_CREPL:
         if(!isalnum(*(p+1))){         /* take rest of line literally */
            if(clen==(-1)){
               clen=(int)(d-c);
               if(clen==0){ msg=no_clas ; goto zsynerr; }
            }else{
               wlen=(int)(d-w);
               if(wlen==0){ msg=no_word; goto zsynerr; }
            }
            sop= *p;
            w[wlen]='\0';
            c[clen]='\0';
            if(addeqvlst(eql,w,c,op)<0) goto zmemerr;
            op=sop;
            w= ++p;
            for(;*p!='\0' && *p!='\n';p++) ;        /* skip to end */
            p--;                            /* cause loop to break */
            if(p<w){
               msg="Empty replacement";
               goto zsynerr;
            }
            d=p;
            break;
         }
         /*nobreak;*/
      case EQV_CADD :
      case EQV_CDEL :
         if(clen==(-1)){
            clen=(int)(d-c);
            if(clen==0){ msg=no_clas ; goto zsynerr; }
         }else{
            wlen=(int)(d-w);
            if(wlen==0){ msg=no_word; goto zsynerr; }
         }
         sop= *p;
         w[wlen]='\0';
         c[clen]='\0';
         if(addeqvlst(eql,w,c,op)<0) goto zmemerr;
         op=sop;
         w=p+1;
         d=p;
         break;
      default:
         *d= *p;
         break;
      }
   }
   if(clen==(-1)){
      clen=(int)(d-c);
      if(clen==0){ msg=no_clas ; goto zsynerr; }
   }else{
      wlen=(int)(d-w);
      if(wlen==0){ msg=no_word; goto zsynerr; }
   }
   w[wlen]='\0';
   c[clen]='\0';
   if(addeqvlst(eql,w,c,op)<0) goto zmemerr;
   return(eql);
zsynerr:
   putmsg(MERR+UGE,Fn,"Equiv syntax error: %s",msg);
   goto zerr;
zmemerr:
   putmsg(MERR+MAE,Fn,sysmsg(ENOMEM));
zerr:
   if(eql!=EQVLSTPN) closeeqvlst(eql);
   return(EQVLSTPN);
}                                                   /* end eqvparse() */
/**********************************************************************/

/**********************************************************************/
                                   /* MAW 11-27-95 */
static int wrdlen ARGS((char *,int));
static int
wrdlen(s,lit)
char *s;
int lit;
{
int l;

   for(l=0;*s!='\0';s++,l++)
      if(!lit && (*s==EQV_CADD || *s==EQV_CCLAS || *s==EQV_CESC))
         l++;
   return(l);
}

static void wrdcpy ARGS((char *,char *,int));
static void
wrdcpy(d,s,lit)
char *d, *s;
int lit;
{
   for(;*s!='\0';s++,d++)
   {
      if(!lit && (*s==EQV_CADD || *s==EQV_CCLAS || *s==EQV_CESC))
         *(d++)=EQV_CESC;
      *d= *s;
   }
}

/**********************************************************************/
char *
eqvfmti(eql,a_lenw,a_lena,a_nwrds,forceLit)
EQVLST *eql;
int *a_lenw, *a_lena, *a_nwrds;
int forceLit;
{
char **w, **c, *o, *pc, *buf = CHARPN;
byte **bw;
int i, j, lit;
int lenw, lena=0, nwrds;

#ifdef WORDSONLY
   lena=strlen(eql->words[0])+2;
   if((buf=malloc(lena))!=CHARPN){
      *buf=eql->logic;
      strcpy(buf+1,eql->words[0]);
      nwrds=1;
      lena-=1;
      lenw=lena;
      if(a_lenw!=(int *)NULL) *a_lenw=lenw;
      if(a_lena!=(int *)NULL) *a_lena=lena;
      if(a_nwrds!=(int *)NULL) *a_nwrds=nwrds;
   }
   return(buf);
#else
   w=eql->words;
   bw = (byte **)w;
   c=eql->clas ;
   o=eql->op;
   /* opreate literally if requested, or if it's bob=/pattern */
   if(forceLit || (bw[1][0] != '\0' && o[1]==EQV_CREPL && !isalnum(*bw[1])))
      lit=1;
   else
      lit=0;
   for(j=0;j<2;j++){               /* 0 to count, 1 to alloc and doit */
      if(j==1){
         if((buf=(char *)malloc(lena+1))==CHARPN) return(CHARPN);
         *buf=eql->logic;
/* MAW 01-19-96 - check to do literal on first term for rex backslash etc. */
         wrdcpy(buf+1,w[0],(forceLit || (!isalnum(*bw[0])&&*w[1]=='\0'))?1:0);
      }
      lenw=lena=wrdlen(w[0],(forceLit || (!isalnum(*bw[0])&&*w[1]=='\0'))?1:0)+1;
      if((*w[1]!='\0' || isalnum(*bw[0])) && *c[0]!='\0'){
         if(j==1){
            buf[lena]=EQV_CCLAS;
            strcpy(&buf[lena+1],c[0]);
         }
         lena+=strlen(c[0])+1;
      }
      for(pc=c[0],i=1,nwrds=1;*w[i]!='\0';i++,nwrds++){
         if(j==1){
            buf[lena]=o[i];
            wrdcpy(&buf[lena+1],w[i],lit);
         }
         lena+=wrdlen(w[i],lit)+1;
         if(*c[i]!='\0' && eqcmp(c[i],pc)!=0){
            if(j==1){
               buf[lena]=EQV_CCLAS;
               strcpy(&buf[lena+1],c[i]);
            }
            lena+=strlen(pc=c[i])+1;
         }
      }
   }
   buf[lena]='\0';
   if(a_lenw !=(int *)NULL) *a_lenw=lenw;
   if(a_lena !=(int *)NULL) *a_lena=lena;
   if(a_nwrds!=(int *)NULL) *a_nwrds=nwrds;
   return(buf);
#endif                                                   /* WORDSONLY */
}                                                    /* end eqvfmti() */
/**********************************************************************/

#ifndef WORDSONLY
#define OUTLNSZ 70
#define OUTC(c) fputc((c),fp)
#define OUTS(s) fputs((s),fp)
#ifdef MSDOS
#  define NEWLINE "\r\n"
#else
#  define NEWLINE "\n"
#endif
/**********************************************************************/
int
eqvsfmt(eql,fp)/* format eql to source file fp, return # of lines (-1)==err */
EQVLST *eql;
FILE *fp;
{
int nlines;
char **w, **c, *o, *pc;
int i, llen, l, nc;

   w=eql->words;
   c=eql->clas ;
   o=eql->op;
   OUTS(w[0]);                                         /* output root */
   llen=strlen(w[0]);
   if((*w[1]!='\0' || isalnum(*w[0])) && *c[0]!='\0'){/* and its class */
      OUTC(EQV_CCLAS);
      OUTS(c[0]);
      llen+=strlen(c[0])+1;
   }
   for(pc=c[0],nlines=0,i=1;*w[i]!='\0';i++){
      l=strlen(w[i])+1;
      if(*c[i]!='\0' && eqcmp(c[i],pc)!=0){            /* a new class */
         nc=1;
         l+=strlen(c[i])+1;
         pc=c[i];
      }else nc=0;
      if(i>1 && (llen+l)>OUTLNSZ){                /* start a new line */
         OUTS(NEWLINE);
         nlines++;
         OUTS(w[0]);                                  /* restate root */
         llen=strlen(w[0]);
         if((*w[1]!='\0' || isalnum(*w[0])) && *c[0]!='\0'){
            OUTC(EQV_CCLAS);
            OUTS(c[0]);
            llen+=strlen(c[0])+1;
         }
         if(*c[i]!='\0' && eqcmp(c[i],c[0])!=0){ /* re/undo new class */
            nc=1;
            pc=c[i];
         }else{
            nc=0;
            pc=c[0];
            l-=strlen(c[i])+1;
         }
      }
      OUTC(o[i]);
      OUTS(w[i]);
      if(nc){
         OUTC(EQV_CCLAS);
         OUTS(c[i]);
      }
      llen+=l;
   }
   OUTS(NEWLINE);
   nlines++;
   if(ferror(fp)) nlines=(-1);
   return(nlines);
}                                                    /* end eqvsfmt() */
/**********************************************************************/
#undef OUTLNSZ
#undef OUTC
#undef OUTS
#undef NEWLINE
#endif                                                  /* !WORDSONLY */

#ifdef TEST

void dopq ARGS((char *));
/**********************************************************************/
void
dope(e)
char *e;
{
EQVLST *eql;
char **w, **c, *o, *f;
int i;

   if((eql=eqvparse(e,1))==EQVLSTPN) puts("Error");
   else{
      f=eqvfmt(eql);
      printf("%s\n",f);
      free(f);
      printf("%d words\n",eql->used-1);
      w=eql->words;
      c=eql->clas ;
      o=eql->op;
      for(i=0;*w[i]!='\0';i++){
         printf("  %c%s)%s\n",o[i],c[i],w[i]);
      }
      closeeqvlst(eql);
   }
}                                                       /* end dope() */
/**********************************************************************/

void dopq ARGS((char *));
/**********************************************************************/
void
dopq(q)
char *q;
{
char **lst;
int n, i, isects;

   if(eqvpq(q,&lst,&n,&isects)!=0) puts("Error");
   else{
      printf("%d words in '%s' @%d\n",n,q,isects);
      for(i=0;i<n;i++){
         printf("  '%s'\n",lst[i]);
         if(lst[i][1]==EQV_CPPM) dope(lst[i]+2);
         free(lst[i]);
      }
      /* the list also has a terminator that is not counted in n,
      ** but must be freed
      */
      printf("  '%s' (terminator)\n",lst[i]);
      free(lst[i]);
      free((char *)lst);
   }
}                                                       /* end dopq() */
/**********************************************************************/

void main ARGS((int argc,char **argv));
/**********************************************************************/
void
main(argc,argv)
int argc;
char **argv;
{
int pq=1;
static char buf[128];

   for(--argc,++argv;argc>0 && **argv=='-';argc--,argv++){
      switch(*++*argv){
      case 'e': pq=0; break;
      }
   }
   if(argc>0){
      for(;argc>0;argc--,argv++){
         if(pq) dopq(*argv);
         else   dope(*argv);
      }
   }else{
      while(gets(buf)!=CHARPN){
         if(pq) dopq(buf);
         else   dope(buf);
      }
   }
   exit(0);
}                                                       /* end main() */
/**********************************************************************/
#endif                                                        /* TEST */

