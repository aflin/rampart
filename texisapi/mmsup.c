/* -=- kai-mode: john -=- */
# include "txcoreconfig.h"
# include <stdio.h>
# include <string.h>
# include <errno.h>
# include <sys/types.h>
# include <sys/stat.h>
#ifdef EPI_HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <stdlib.h>
#ifdef HAVE_MMAP
# include <sys/mman.h>
#endif
#ifdef max
#undef max
#undef min
#endif
#include "dbquery.h"
#include "texint.h"
#ifdef EPI_HAVE_STDARG
# include <stdarg.h>
#else
# include <varargs.h>
#endif


#define MMBUFSIZ 32000

/******************************************************************/

int
TXismmop(QNODE_OP op, FOP *fop)
{
	int rc;

	switch(op)
	{
	case FLDMATH_MM:    if(fop) *fop=FOP_MM; break;
	case FLDMATH_NMM:   if(fop) *fop=FOP_NMM; break;
	case FLDMATH_RELEV: if(fop) *fop=FOP_RELEV; break;
	case FLDMATH_PROXIM:if(fop) *fop=FOP_PROXIM; break;
	case FLDMATH_MMIN:  if(fop) *fop=FOP_MMIN; break;
	default:
		return 0;
	}
	return 1;
}

/******************************************************************/
/* Make sure each "term" in data is in query */

int
TXlikein(op1, op2)
FLD *op1, *op2;
{
#ifndef NO_EMPTY_LIKEIN
#else
	char *p;
#endif
	char *query, *data;
	int rc = 1;
	DDMMAPI *ddmmapi;

	ddmmapi = getfld(op2, NULL);
	if(ddmmapi)
		ddmmapi = ddmmapi->self;
	if (!ddmmapi) return 0;

	query = ddmmapi->query;
	if(!query)
		return 0;
        data = TXfldToMetamorphQuery(op1);
#ifndef NO_EMPTY_LIKEIN
	if(!setmmapi(ddmmapi->mmapi, data, TXbool_False))
		return 1;
	if(getmmapi(ddmmapi->mmapi, (byte *)ddmmapi->query, (byte *)ddmmapi->query + strlen(ddmmapi->query), SEARCHNEWBUF))
	{
		return 1;
	}
	else
		return 0;
#else
	p = strtok(data, " "); /* Delimiter String */
	while(p)
	{
		if(*p == '-')
		{
			if(strstr(query, p+1))
			{
				rc = 0; /* Does occur */
				break;
			}
		}
		else
		{
			if(!strstr(query, p))
			{
				rc = 0; /* Does occur */
				break;
			}
		}
		p = strtok(NULL, " ");
	}
#endif
	free(data);
	return rc;
}

/******************************************************************/

int
metamorphop(op1, op2)
FLD *op1, *op2;
/* If found, returns rank (if able) or 1.  If not found, returns 0.
 */
{
        unsigned char *data;
        int   flen, ret;
        char *hit;
        DDMMAPI *ddmmapi;
        MMAPI *mmapi;
        RPPM    *rp;

	ddmmapi = getfld(op2, NULL);
	if(ddmmapi)
		ddmmapi = ddmmapi->self;
	if (!ddmmapi) return 0;
        if (ddmmapi->query == (char *)NULL)
                return 1;
        mmapi = ddmmapi->mmapi;
	if (!mmapi || !ddmmapi->mmapiIsPrepped) return 0;

        data = getfld(op1, NULL);
        flen = op1->size;
        if (ddmmapi->buffer)
        {
                if (ddmmapi->mmaplen)
#ifdef HAVE_MMAP                                      /* MAW 02-15-94 */
                        munmap(ddmmapi->buffer, ddmmapi->mmaplen);
#else
                        ;
#endif
                else if (ddmmapi->freebuf)
                        free(ddmmapi->buffer);
        }
        ddmmapi->buffer=data;
        ddmmapi->mmaplen=0;

#ifdef NEVER
	putmsg(999, NULL, "Searching %s(%d) for %s", data, flen, ddmmapi->query);
#endif
        hit = getmmapi(mmapi, data, data + flen, SEARCHNEWBUF);
        if (hit != CHARPN)
	{
#ifdef NEVER
		putmsg(999, NULL, "Found at offset %d", (char *)hit - (char *)data);
#endif
                ret = 1;
                /* If we're ranking for LIKE, then do rank here,
                 * while we have the mminfo: KNG 980701
                 */
                if (ddmmapi->bt != NULL &&
                    (rp = ((PROXBTREE *)ddmmapi->bt)->r) != RPPMPN &&
                    (rp->flags & RPF_RANKMMINFO))
                  {
                    TXsetrecid(&rp->curRecid, 0x0);     /* for RPPM tracing */
                    ret = rppm_rankbuf(rp, mmapi, data, data+flen, SIZE_TPN);
                    if (ret <= 0) ret = 1;
                  }
	}
        else
	{
#ifdef NEVER
		putmsg(999, NULL, "Not Found ");
#endif
                ret = 0;
	}

/* WTF - Why do I free this, cause usually I'm calling from FLDMATH stack */
/*
        if (op1->shadow && (op1->shadow!=op1->v))
                free(op1->shadow);
*/
#if !defined(ALLOW_MISSING_FIELDS)
	/* KNG 20060215 TXfreefldshadow... for OO: */
	if(FLD_IS_COMPUTED(op1)) TXfreefldshadownotblob(op1);
#endif

        return(ret);
}

/******************************************************************/

int
fmetamorphop(op1, op2)
FLD *op1, *op2;
/* If found, returns rank (if able) or 1.  If not found, returns 0.
 */
{
        ft_indirect     *filedata;
        unsigned char   *data;
        off_t            flen;
        char            *hit;
        FILE            *fd;
        MMAPI           *mmapi;
        DDMMAPI         *ddmmapi;
        int             ret;
        RPPM            *rp;
#ifndef HAVE_MMAP
	int		buflen, nread;
	off_t		toread;
#endif

	ddmmapi = getfld(op2, NULL);
	if(ddmmapi)
		ddmmapi = ddmmapi->self;
	if (!ddmmapi) return 0;
        mmapi = ddmmapi->mmapi;

/* PBR WTF */
/* Regular file reads should use freadex in a loop */

        filedata = getfld(op1, NULL);
        errno = 0;
        fd = fopen(filedata, "rb");
        if (fd == (FILE *)NULL)
        {
          if (*(char *)filedata != '\0')
            putmsg(MERR+FOE, "metamorph","Can't open indirect file %s: %s",
                   filedata, strerror(errno));
          return 0;
        }
	fseek(fd, 0L, SEEK_END);
	flen = ftell(fd);
	fseek(fd, 0L, SEEK_SET);
        if (ddmmapi->buffer)
        {
                if (ddmmapi->mmaplen)
#ifdef HAVE_MMAP
                        munmap(ddmmapi->buffer, ddmmapi->mmaplen);
#else
                        ;
#endif
                else
			if(ddmmapi->freebuf)
				free(ddmmapi->buffer);
        }
#ifdef HAVE_MMAP
        data = (unsigned char *)mmap((void *)NULL, flen, PROT_READ|PROT_WRITE,
                        MAP_PRIVATE, fileno(fd), 0);
        ddmmapi->buffer = data;
        ddmmapi->mmaplen = flen;
	ddmmapi->freebuf = 0;
	if(data == (void *)-1)
	{
		data = (unsigned char *)calloc(1, 1);
		ddmmapi->buffer = data;
		ddmmapi->mmaplen = 0;
		ddmmapi->freebuf = 1;
		flen = 0;
	}
        fclose(fd);
        hit = getmmapi(mmapi, data, data + flen, SEARCHNEWBUF);
        if (hit != CHARPN)
          {
            ret = 1;
            /* If we're ranking for LIKE, then do rank here,
             * while we have the mminfo: KNG 980701
             */
            if (ddmmapi->bt != NULL &&
                (rp = ((PROXBTREE *)ddmmapi->bt)->r) != RPPMPN &&
                (rp->flags & RPF_RANKMMINFO))
              {
                ret = rppm_rankbuf(rp, mmapi, data, data + flen, SIZE_TPN);
                if (ret <= 0) ret = 1;
              }
          }
        else
          ret = 0;
#else   /* !HAVE_MMAP */
/* Make a loop here */
/* Need to read as many buffers of size ? till we get to flen */
	buflen = TXgetblockmax();
	toread = flen;
        data = (unsigned char *)malloc(buflen);
	ddmmapi->buffer = data;
	ddmmapi->mmaplen = 0;
	ddmmapi->freebuf = 1;
        ret = 0;
	while(toread > 0)
	{
		nread = fread(data, 1, buflen, fd);
		hit = getmmapi(mmapi, data, data + nread, SEARCHNEWBUF);
		if (hit != CHARPN)
                  {
                    ret = 1;
                    /* If we're ranking for LIKE, then do rank here,
                     * while we have the mminfo: KNG 980701
                     */
                    if (ddmmapi->bt != NULL &&
                        (rp = ((PROXBTREE *)ddmmapi->bt)->r) != RPPMPN &&
                        (rp->flags & RPF_RANKMMINFO))
                      {
                        ret = rppm_rankbuf(rp, mmapi, data, data + nread,
					   SIZE_TPN);
                        if (ret <= 0) ret = 1;
                      }
                    break;
                  }
		else
                  toread -= nread;
	}
        fclose(fd);
#endif  /* !HAVE_MMAP */
        return(ret);
}

/******************************************************************/

int
bmetamorphop(op1, op2)
FLD *op1, *op2;
/* If found, returns rank (if able) or 1.  If not found, returns 0.
 */
{
	static const char	fn[] = "bmetamorphop";
        ft_blobi        *filedata = NULL;
        unsigned char   *data = NULL;
        size_t           flen;
        char            *hit = NULL;
        MMAPI           *mmapi;
        DDMMAPI         *ddmmapi;
        RPPM            *rp;
        int             ret;

        ddmmapi = getfld(op2, NULL);
	if (!ddmmapi)
		goto err;
        mmapi = ddmmapi->mmapi;

        filedata = getfld(op1, NULL);
	if (!filedata)
	{
		putmsg(MERR, fn, "Missing blobi data");
		goto err;
	}
	if (TXfldbasetype(op1) != FTN_BLOBI)
	{
		putmsg(MERR + UGE, fn, "Field op1 is %s, not expected %s",
		       TXfldtypestr(op1), ddfttypename(FTN_BLOBI));
		goto err;
	}
	data = TXblobiGetPayload(filedata, &flen);
	if(data)
		hit = getmmapi(mmapi, data, data + flen, SEARCHNEWBUF);
        if (hit != CHARPN)
          {
            ret = 1;
            /* If we're ranking for LIKE, then do rank here,
             * while we have the mminfo: KNG 980701
             */
            if (ddmmapi->bt != NULL &&
                (rp = ((PROXBTREE *)ddmmapi->bt)->r) != RPPMPN &&
                (rp->flags & RPF_RANKMMINFO))
              {
                ret = rppm_rankbuf(rp, mmapi, data, data + flen, SIZE_TPN);
                if (ret <= 0) ret = 1;
              }
          }
        else
	  goto err;
	goto finally;

err:
	ret = 0;
finally:
	if (filedata) TXblobiFreeMem(filedata);	/* save mem? */
        return(ret);
}

/******************************************************************/

#ifdef NEED_PUTMSG
#ifdef EPI_HAVE_STDARG
                                /* doesn't accept old format varargs! */

int putmsg(int n,char *fn,char *fmt,...)
{
va_list argp;

   va_start(argp,fmt);

#else                                                   /* !macintosh */

/*VARARGS*/
int
putmsg(va_alist)                                  /* args(n,fn,fmt,...) */
va_dcl
{
 va_list argp;
 int        n; /* the message number */
 char     *fn; /* the function name (may be NULL) */
 char    *fmt; /* the messgage content in a printf type format string  */


                         /* get the fixed args */
 va_start(argp);
 n  =va_arg(argp,int   );                                 /* msg number */
 fn =va_arg(argp,char *);                              /* function name */
 fmt=va_arg(argp,char *);                              /* printf format */

#endif                                                   /* macintosh */


 if( n>=0   && n<100 ) fputs("ERROR: ",stdout);
 else
 if( n>=100 && n<200 ) fputs("WARNING: ",stdout);

 if(fmt!=(char *)NULL)
     vfprintf(stdout,fmt,argp);            /* print the message content */

 if(fn!=(char *)NULL)              /* print the function if its present */
     fprintf(stdout," In the function: %s ",fn);

 fputc('\n',stdout);                              /* print the new line */

 va_end(argp);                /* terminate the argument list processing */

 return(0);                              /* false means OK in this case */
}                                                       /* end putmsg() */

#endif
