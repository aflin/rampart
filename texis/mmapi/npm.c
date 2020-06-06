#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include "sizes.h"
#include "os.h"
#include "pm.h"
#include "npmp.h"


/************************************************************************/

NPMS *
closenpm(np)
NPMS *np;
{
 if(np!=(NPMS *)NULL)
    {
     if(np->ps!=(PPMS *)NULL) closeppm(np->ps);
     rmnptlst();
     free(np);
    }
 return((NPMS *)NULL);
}

/************************************************************************/

NPMS *
opennpm(s)
char *s;
{
 double x1,y1,x2,y2;
 char op1,op2;
 char *s2;
 NPMS *np=(NPMS *)calloc(1,sizeof(NPMS));
 if(np==(NPMS *)NULL) return(np);

 np->tlst=(byte **)NULL;
 np->ps=  (PPMS *)NULL;
 op1=op2=0;

 if(*s!=NPARB)
    {
      char      *e;

      e = s + strlen(s);
      if ((s2 = ttod(s, e, &x1, &y1, &op1)) == s)       /* grab the string */
        return(closenpm(np));
      if (ttod(s2, e, &x2, &y2, &op2) == s2) op2 = 0;
    }


 if(op1==',')
    {
     if(op2) return(closenpm(np));
     np->x=x1;
     np->y=x2;
     np->xop='g';                                      /* g is gte (>=) */
     np->yop='l';                                      /* l is lte (<=) */
    }
 else
    {
     np->x=x1;
     np->y=x2;
     np->xop=op1;
     np->yop=op2;
    }

 np->tlst=(byte **)mknptlst();                        /* get token list */
 if(np->tlst==(byte **)NULL)
    return(closenpm(np));

 pm_hyeqsp(0);                                        /* hyphen control */
 np->ps=openppm(np->tlst);                                  /* open ppm */
 pm_hyeqsp(1);

 if(np->ps==(PPMS *)NULL)
    return(closenpm(np));


 if(np->xop && np->yop)
    {
     char top;
     double t;

     if(np->x>np->y)                               /* swap order around */
         {
          t=np->x;
          np->x=np->y;
          np->y=t;
          top=np->xop;
          np->xop=np->yop;
          np->yop=top;
         }

     if(np->x==np->y) return(closenpm(np));

                      /* chk for valid operations */
     if(np->xop=='l' ||
        np->xop=='<' ||
        np->yop=='g' ||
        np->yop=='>'
       ) return( closenpm(np) );
    }
 if(np->yop && np-> xop)
      sprintf(np->ss,"X%c%lg and X%c%lg",np->xop,np->x,np->yop,np->y);
 else if(np->xop)
      sprintf(np->ss,"X%c%lg",np->xop,np->x);
 else sprintf(np->ss,"ANY X");



 return(np);
}

/************************************************************************/

byte *
getnpm(np,buf,end,op)
NPMS *np;
byte *buf;
byte *end;
int op;
{
 byte *eoh;
 char hitop;
 pm_hyeqsp(0);

 if(op==CONTINUESEARCH)
    {
     buf=np->hit+np->hitsz;
     op=SEARCHNEWBUF;
    }
 for(np->hit=getppm(np->ps,buf,end,op);
     buf<end && np->hit!=(byte *)NULL;
     np->hit=getppm(np->ps,buf,end,op)
    )
    {
     if(np->hit>buf && isalpha(*(np->hit-1)))
         {
          op=SEARCHNEWBUF;
          for(buf=np->hit;buf<end && ( isalnum(*buf) || *buf=='-');buf++);
          continue;
         }
     eoh = (byte *)ttod((char*)np->hit, (char*)end, &np->hx, &np->hy, &hitop);
     if (eoh > np->hit)
         {
          int ok=0;
          np->hitsz=eoh-np->hit;
          buf=eoh;
          op=SEARCHNEWBUF;
          switch(np->xop)
              {
               case  0  : ++ok;break;
               case '>' : if(np->hx>np->x)   ok++;break;
               case 'g' : if(np->hx>=np->x)  ok++;break;
               case '<' : if(np->hx<np->x)   ok++;break;
               case 'l' : if(np->hx<=np->x)  ok++;break;
               case '=' : if(np->hx==np->x)  ok++;break;
              }
          switch(np->yop)
              {
               case  0  : ++ok;break;
               case '>' : if(np->hx>np->y)   ok++;break;
               case 'g' : if(np->hx>=np->y)  ok++;break;
               case '<' : if(np->hx<np->y)   ok++;break;
               case 'l' : if(np->hx<=np->y)  ok++;break;
               case '=' : if(np->hx==np->y)  ok++;break;
              }
          if(ok==2) goto END;
         }
     else op=CONTINUESEARCH;
    }
 END:
 pm_hyeqsp(1);
 return(np->hit);
}

/************************************************************************/

#if TEST

#define LNSZ 8192
char ln[LNSZ];

main(argc,argv)
int argc;
char *argv[];
{
 int i;
 NPMS *ns;
 FILE *fh;
 if(argc==1)
    {
     npmtypedump(stdout);
     goto OOPS;
    }
 if(argc>1)
    {
     ns=opennpm(argv[1]);
     if(ns==(NPMS *)NULL) goto OOPS;
    }
 if(argc>2)
    {
      fh=fopen(argv[2],"r");
      if(fh==(FILE *)NULL)
         {
          puts("fopen err");
          exit(1);
         }
    }
  else   fh=stdin;
 while(fgets(ln,8192,fh))
    {
     int op=SEARCHNEWBUF;
     while(getnpm(ns,ln,ln+strlen(ln),op)!=(byte *)NULL)
        {
         printf("%lg: %s",ns->hx,ln);
         op=CONTINUESEARCH;
        }
    }
 closenpm(ns);
 exit(0);
 OOPS:
 puts("..\nuse npm \"[><]qty[[<>]qty]\" [fname]");
 exit(1);
}
#endif /* TEST */
/************************************************************************/

