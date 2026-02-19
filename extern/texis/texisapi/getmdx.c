#include "stdio.h"
#include <stdlib.h>
#ifdef MVS
#  if MVS_C_REL > 1
#     include <stddef.h>
#  else
#     include <stdefs.h>
#  endif
#else
#  include "fcntl.h"
#endif
#include "ctype.h"
#include "string.h"
#include "sizes.h"
#include "os.h"
#ifdef __MINGW32__
#  undef ERROR  /* wingdi.h defines ERROR as 0; conflicts with label */
#endif
#include "pm.h"
#include "mdx.h"



/* #define TEST 1 */
#ifdef LINT_ARGS
static char *strippath(char *);
int readmdx(MDXRS *,long);
#else
static char *strippath();
int readmdx();
#endif

#ifdef NOSEEKTEXT
#  ifdef LINT_ARGS
      static int NSTfseek(MDXRS *,long,int);
#  else
      static int NSTfseek();
#  endif
#else
#  define NSTfseek(a,b,c) fseek((a)->xfh,b,c)
#endif
/************************************************************************/
char *mdxpath=MDXPATH;	    /* path to use in writing files out */
char *mdxmsg;			 /* message coming from get mdx */
/************************************************************************/

static char *	/* returns a pointer to the filename without the path */
strippath(s)
char *s;
{
#ifdef MVS					    /* 11/27/89 - MAW */
			  /* possible name formats:		      */
			  /* 'name1'               name1              */
			  /* 'name1.name2'         name1.name2        */
			  /* 'name1(name2)'        name1(name2)       */
 register char *e;
 static char st[MDXPATHSZ];
 char *svp, sv;

 e=s+strlen(s)-1;
 if(*e==')' || *e=='\'')
    {
     if(*e=='\'')
	{
	 if(e-1>=s && *(e-1)==')') e--;
	}
     svp=e;
     sv= *e;
     for(*e='\0',e--;e>s && *e!='(' && *e!='\'' && *e!='.';e--)
	;
     if(*e=='(' || *e=='\'' || *e=='.')
	e++;
     strncpy(st,e,MDXPATHSZ-1);
     st[MDXPATHSZ-1]='\0';
     e=st;
     *svp=sv;
    }
 else
    {
     for(;e>s && *e!='.';e--)
	;
     if(*e=='.')
	e++;
    }
#else
 register char *e;

 for(e=s+strlen(s)-1;*e!=MDXPATHC;e--)
    if(e<=s) break;

 if(*e==MDXPATHC)
    ++e;
#endif
 return(e);
}

/************************************************************************/

MDXRS *
closermdx(rs)
MDXRS *rs;
{
 if(rs!=(MDXRS *)NULL)
   {
    if(rs->fh!=(FILE *)NULL)
	 fclose(rs->fh);
    if(rs->xfh!=(FILE *)NULL)
	 fclose(rs->xfh);
    free((char *)rs);
   }
 return((MDXRS *)NULL);
}

/************************************************************************/


#ifdef OLDRMDXOPEN /* pbr 8/1/90  to fix dev problem */
MDXRS *
openrmdx(fn)
char *fn;
{
 extern char *mdxmsg;
 FILE *fh=(FILE *)NULL;
 MDXRS *rs=(MDXRS *)NULL;
 char pbuf[MDXPATHSZ];
 extern char *mdxpath;
 char *sfn;					     /* short file name */
 int i;
 int mplen;					  /* mindex path length */

 mplen=strlen(mdxpath);

 if(!QCALLOC(rs,1,MDXRS))
    {
     mdxmsg="can't allocate memory for index";
     return(rs);
    }

 if(!QFOPEN(rs->xfh,fn,MDXTRMODE))
   {
    mdxmsg="can't open the indexed file";
    return(closermdx(rs));
   }
#ifdef NOSEEKTEXT				      /* MAW 03-22-90 */
 rs->xfpos=0L;
 rs->xftellpos=ftell(rs->xfh);
#endif

 sfn=strippath(fn);			  /* create the index file name */
#ifdef MVS
 if( (mplen+strlen(sfn)+4) > MDXPATHSZ) 		     /* ".)'" */
#else
 if( (mplen+strlen(sfn)+2) > MDXPATHSZ) 		       /* "/" */
#endif
    {
     mdxmsg="path+filename too long";
     return(closermdx(rs));
    }
 strcpy(pbuf,mdxpath);
#ifdef MVS
 if(pbuf[mplen-1]=='\'')          /* prefix may have quotes around it */
    {
     pbuf[--mplen]='\0';
    }
#endif
 if(pbuf[mplen-1]!=MDXPATHC)	  /* append trailing pathdelim if none */
   {
    pbuf[mplen]=MDXPATHC;
    pbuf[mplen+1]='\0';
   }
 strcat(pbuf,sfn);
#ifdef MVS
 if(pbuf[0]=='\'')
    strcat(pbuf,")'");
 else
    strcat(pbuf,")");
#endif

 if(!QFOPEN(fh,pbuf,MDXIRMODE))
   {
    mdxmsg="index not located for file";
    return(closermdx(rs));
   }
 rs->fh=fh;				      /* assign the file handle */
 if(!fread((char *)rs->explst,sizeof(MDXER),MDXEXPRS,fh))
   {
    mdxmsg="can't read index file";
    return(closermdx(rs));
   }				      /* sort them by relative location */
 for(i=0;rs->explst[i].pos!='\0';i++)
    if(rs->explst[i].pos=='+')
	 ++rs->nfwd;			/* number of forward references */
 rs->expcnt=i;			   /* assign the number of reqd entries */
 return(rs);
}

#else

MDXRS *
openrmdx(fn)
char *fn;
{
 extern char *mdxmsg;
 FILE *fh=(FILE *)NULL;
 MDXRS *rs=(MDXRS *)NULL;
 char pbuf[MDXPATHSZ];
 extern char *mdxpath;
 char *sfn;					     /* short file name */
 int i;
 int mplen;					  /* mindex path length */

 mplen=strlen(mdxpath);

 if(!QCALLOC(rs,1,MDXRS))
    {
     mdxmsg="can't allocate memory for index";
     return(rs);
    }

#ifdef NOSEEKTEXT				      /* MAW 03-22-90 */
 rs->xfpos=0L;
 rs->xftellpos=ftell(rs->xfh);
#endif

 sfn=strippath(fn);			  /* create the index file name */
#ifdef MVS
 if( (mplen+strlen(sfn)+4) > MDXPATHSZ) 		     /* ".)'" */
#else
 if( (mplen+strlen(sfn)+2) > MDXPATHSZ) 		       /* "/" */
#endif
    {
     mdxmsg="path+filename too long";
     return(closermdx(rs));
    }
 strcpy(pbuf,mdxpath);
#ifdef MVS
 if(pbuf[mplen-1]=='\'')          /* prefix may have quotes around it */
    {
     pbuf[--mplen]='\0';
    }
#endif
 if(pbuf[mplen-1]!=MDXPATHC)	  /* append trailing pathdelim if none */
   {
    pbuf[mplen]=MDXPATHC;
    pbuf[mplen+1]='\0';
   }
 strcat(pbuf,sfn);
#ifdef MVS
 if(pbuf[0]=='\'')
    strcat(pbuf,")'");
 else
    strcat(pbuf,")");
#endif

 if(!QFOPEN(fh,pbuf,MDXIRMODE))
   {
    mdxmsg="index not located for file";
    return(closermdx(rs));
   }
 rs->fh=fh;				      /* assign the file handle */
 if(!fread((char *)rs->explst,sizeof(MDXER),MDXEXPRS,fh))
   {
    mdxmsg="can't read index file";
    return(closermdx(rs));
   }				      /* sort them by relative location */
 for(i=0;rs->explst[i].pos!='\0';i++)
    if(rs->explst[i].pos=='+')
	 ++rs->nfwd;			/* number of forward references */
 rs->expcnt=i;			   /* assign the number of reqd entries */

 if(!QFOPEN(rs->xfh,fn,MDXTRMODE))
   {
    mdxmsg="can't open the indexed file";
    return(closermdx(rs));
   }

 return(rs);
}
#endif
/************************************************************************/

int			  /* reads in all the mindex entries for offset */
readmdx(rs,offset)
MDXRS *rs;			    /* read struct assigned by openrmdx */
long offset;		    /* offset of the hit you want to know about */
{
 extern char *mdxmsg;
 int i;
 int nread;
 int nfwdrefs;
 long coff;
 char xpos[MDXEXPRS];	     /* holds the position markers for the exps */

 if(fseek(rs->fh,(long)sizeof(MDXER)*MDXEXPRS,SEEK_SET)!=0)
   {
    mdxmsg="Can't rewind index file";
    return(0);
   }

 for(i=0;i<rs->expcnt;i++)
   {
    xpos[i]=rs->explst[i].pos;		     /* assign position markers */
    rs->exploc[i].offset= -1L;	/* records w/ -1L offsets were not found */
   }
 nfwdrefs=rs->nfwd;

 for(coff=0L,nread=fread(rs->blk,sizeof(MDXHR),MDXBLKSZ,rs->fh);
     nread;
     nread=fread(rs->blk,sizeof(MDXHR),MDXBLKSZ,rs->fh)
    )
    {
     MDXHR *cr; 				/* get negative entries */
    /* MAW 03-28-90 - check cr->expsz>0 - MVS pads file with 0 at eof */
     for(i=0,cr=rs->blk;i<nread && cr->offset<=offset && cr->expsz>0;i++,cr++)
	 {
	  coff=cr->offset;			      /* current offset */
	  if(xpos[cr->expn]=='-')
	      rs->exploc[cr->expn]= *cr;
	 }
     if(cr->offset<offset)
	 continue;
     for(;i<nread && nfwdrefs && cr->expsz>0;i++,cr++)	/* get positive entries */
	 {
	  if(xpos[cr->expn]=='+')
	      {
	       rs->exploc[cr->expn]= *cr;
	       xpos[cr->expn]='\0';
	       nfwdrefs--;
	      }
	 }
    }
 return(1);
}

/************************************************************************/

char *		 /* works like fgets to get index entries for an offset */
getmdx(s,n,offset,rs)
char *s;			   /* pointer to buffer to place string */
int n;							   /* size of s */
long offset;				 /* offset of the desired index */
MDXRS *rs;			    /* read struct assigned by openrmdx */
{
 static long lastoff=(-1);/* MAW 03-22-90 - make sure first read performed */
 static int expn;
 MDXHR *cr = NULL;
 MDXER *er = NULL;
 char tbuf[MDXMAXSZ];


 if(rs==(MDXRS *)NULL)
     return((char *)NULL);

 if(offset!=lastoff)
    {
     lastoff=offset;
     if(!readmdx(rs,offset))
	 goto ERROR;
     expn=0;
    }

	       /* records w/ -1L offsets were not found */
 for(;expn<rs->expcnt;expn++)
    {
     cr= &rs->exploc[expn];	       /* alias to the current record */
     er= &rs->explst[cr->expn];        /* alias to the expression rec */
     if(cr->offset!= -1L)			      /* valid record */
	 break;
    }
 if(expn>=rs->expcnt)
    return((char *)NULL);
 ++expn;				      /* advance for next get */

 if (!cr || !er || NSTfseek(rs, cr->offset, SEEK_SET) != 0)
   {
    mdxmsg="can't seek in indexed file";
    goto ERROR;
   }
 if(cr->expsz>=MDXMAXSZ)
    cr->expsz=MDXMAXSZ-1;

 if((strlen((char *)er->fs)+cr->expsz)>=n)
    cr->expsz=(byte)(n-strlen((char *)er->fs));
 if(!fread(tbuf,sizeof(char),cr->expsz,rs->xfh))
   {
    mdxmsg="can't read indexed file";
    goto ERROR;
   }
 tbuf[cr->expsz]='\0'; /* null the record */
 sprintf(s,(char *)er->fs,tbuf);
 return(s);
 ERROR:
 *s='\0';
 expn=rs->expcnt;
 return((char *)NULL);
}

/************************************************************************/

#ifdef TEST
main(argc,argv)
int argc;
char *argv[];
{
 long off;
 int i;
 char buf[MDXMAXSZ];
 MDXRS *rs;

 for(i=1;i<argc;i++)
    {
     rs=openrmdx(argv[i]);
     while(gets(buf))
	 {
	  off=atol(buf);
	  while(getmdx(buf,MDXMAXSZ,off,rs))
	      puts(buf);
	 }
     closermdx(rs);
    }
}
#endif /* TEST */
/************************************************************************/

#ifdef NOSEEKTEXT				      /* MAW 03-22-90 */
#ifdef MVS					  /* TOTALLY_HOSED_OS */
/**********************************************************************/
/* MAW 06-12-90 - no seeking of any kind on any file for anyone
**		  on any !@#$%^&*( day of the week!!!!!!!!!!!!!!!!!!! */
/**********************************************************************/
int NSTfseek(rs,offset,base)		  /* a text mode seek for MVS */
MDXRS *rs;
long offset;
int base;	      /* WARNING: based ignored - assumed to SEEK_SET */
{
int rc, cnt;

   rewind(rs->xfh);			     /* reseek from beginning */
   if(offset>0){
      for(rc=1,cnt=(int)((offset-rs->xfpos)/(long)BUFSIZ);cnt>0;cnt--){
	 if((rc=fread(rs->seekbuf,BUFSIZ,1,rs->xfh))!=1) break;
      }
      if(rc==1){
	 cnt=(int)((offset-rs->xfpos)%(long)BUFSIZ);
	 if(cnt>0){
	    rc=fread(rs->seekbuf,cnt,1,rs->xfh);
	 }
      }
      if(rc==1) rc=0;
      else	rc=1;
   }else{
      if(offset==0) rc=0;
      else	    rc=1;			     /* bad offset */
   }
   return(rc);
}						    /* end NSTfseek() */
/**********************************************************************/
#else							       /* MVS */
/**********************************************************************/
int NSTfseek(rs,offset,base)		  /* a text mode seek for MVS */
MDXRS *rs;
long offset;
int base;	      /* WARNING: based ignored - assumed to SEEK_SET */
{
int rc, cnt;

   if(offset<rs->xfpos){			    /* back it up joe */
		    /* reseek from beginning - one level of recursion */
      rewind(rs->xfh);
      rs->xfpos=0L;
      rs->xftellpos=ftell(rs->xfh);
      if(offset>0){
	 rc=NSTfseek(rs,offset,base);
      }else{
	 if(offset==0) rc=0;
	 else	       rc=1;				/* bad offset */
      }
   }else{
      if((rc=fseek(rs->xfh,rs->xftellpos,SEEK_SET))==0){/* get to known pos */
	 if(offset>rs->xfpos){			   /* read up to new pos */
	    for(rc=1,cnt=(int)((offset-rs->xfpos)/(long)BUFSIZ);cnt>0;cnt--){
	       if((rc=fread(rs->seekbuf,BUFSIZ,1,rs->xfh))!=1) break;
	    }
	    if(rc==1){
	       cnt=(int)((offset-rs->xfpos)%(long)BUFSIZ);
	       if(cnt>0){
		  rc=fread(rs->seekbuf,cnt,1,rs->xfh);
	       }
	    }
	    if(rc==1) rc=0;
	    else      rc=1;
	 }/* else already there */
      }
   }
   return(rc);
}						    /* end NSTfseek() */
/**********************************************************************/
#endif							       /* MVS */
/**********************************************************************/
#endif							/* NOSEEKTEXT */
