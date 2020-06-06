/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include "txcoreconfig.h"
#ifdef HAVE_MMAP
# include <sys/mman.h>
#endif
#include "dbquery.h"
#include "texint.h"
#include "mmsg.h"

static CONST char * CONST	TxmsmNames[TXMSM_NUM] =
{
#undef I
#define I(tok)	#tok,
	TX_METAMORPH_STRLST_SYMBOLS_LIST
#undef I
};

APICP *globalcp = (APICP *)NULL;

/**********************************************************************/

char **
_freelst(slst)
char **slst;
{
int n;

   if(slst==(char **)NULL) return((char **)NULL);
   for(n=0;slst[n] && *slst[n]!='\0';n++)
   	free(slst[n]);
   if(slst[n])
	   free(slst[n]);
   free(slst);
   return NULL;
}                                                     /* end duplst() */
/**********************************************************************/

char **
_duplst(slst)
char **slst;
{
static char Fn[] = "duplst";
char **lst;
int i, n;

   if(slst==(char **)NULL) return((char **)NULL);
   for(n=0;*slst[n]!='\0';n++) ;
   n++;
   if((lst=(char **)calloc(n,sizeof(char *)))!=(char **)NULL){
      for(i=0;i<n;i++){
         if((lst[i]=strdup(slst[i]))==(char *)NULL){
            for(i--;i>=0;i--) free(lst[i]);
            free(lst);
            return((char **)NULL);
         }
      }
   }
   else
   	putmsg(MERR+MAE, Fn, strerror(ENOMEM));
   return(lst);
}                                                     /* end duplst() */
#ifdef NEVER
/**********************************************************************/
static char *dupstr ARGS((char *s));

static char *
dupstr(s)
char *s;
{
 if(s==(char *)NULL) return((char *)NULL);
 return(strdup(s));
}                                                     /* end dupstr() */
/******************************************************************/
APICP *dupapicp ARGS((APICP *cp));

APICP *
dupapicp(oacp)
APICP *oacp;
{
 APICP *acp;

 if(oacp==APICPPN) return(APICPPN);
 if((acp=(APICP *)calloc(1,sizeof(APICP)))!=APICPPN)
    {
     acp->suffixproc=oacp->suffixproc;
     acp->prefixproc=oacp->prefixproc;
     acp->rebuild   =oacp->rebuild;
     acp->incsd     =oacp->incsd;
     acp->inced     =oacp->inced;
     acp->withinproc=oacp->withinproc;
     acp->suffixrev =oacp->suffixrev;
     acp->minwordlen=oacp->minwordlen;
     acp->intersects=oacp->intersects;
     acp->sdexp     =(byte *)dupstr((char *)oacp->sdexp);
     acp->edexp     =(byte *)dupstr((char *)oacp->edexp);
     acp->query     =(byte *)NULL;
     acp->set       =(byte **)NULL;
     acp->suffix    =(byte **)duplst((char **)oacp->suffix);
     acp->suffixeq  =(byte **)duplst((char **)oacp->suffixeq);
     acp->prefix    =(byte **)duplst((char **)oacp->prefix);
     acp->noise     =(byte **)duplst((char **)oacp->noise);
     acp->eqprefix  =(byte *)dupstr((char *)oacp->eqprefix);
     acp->ueqprefix =(byte *)dupstr((char *)oacp->ueqprefix);
     acp->see       =oacp->see;
     acp->keepeqvs  =oacp->keepeqvs;
     acp->keepnoise =oacp->keepnoise;
     acp->eqedit    =oacp->eqedit;
     acp->eqedit2   =oacp->eqedit2;

     acp->database  =(byte *)dupstr((char *)oacp->database);
     acp->lowtime   =oacp->lowtime;
     acp->hightime  =oacp->hightime;
     acp->filespec  =(byte *)dupstr((char *)oacp->filespec);
     acp->enablemm  =oacp->enablemm;
     acp->buflen    =oacp->buflen;
     acp->worddef   =duplst(oacp->worddef);
     acp->blockdelim=duplst(oacp->blockdelim);
     acp->blocksz   =oacp->blocksz;
     acp->blockmax  =oacp->blockmax;
     acp->maxsimult =oacp->maxsimult;
     acp->adminmsgs =oacp->adminmsgs;
     acp->allow     =duplst(oacp->allow);
     acp->ignore    =duplst(oacp->ignore);

     acp->maxselect =oacp->maxselect;
     acp->profile   =(byte *)dupstr((char *)oacp->profile);
     acp->usr       =oacp->usr;
                                             /* ck the allocated ptrs */
#define BADB(a)  (oacp->a!=(byte  *)NULL && acp->a==(byte  *)NULL)
#define BADBL(a) (oacp->a!=(byte **)NULL && acp->a==(byte **)NULL)
#define BADCL(a) (oacp->a!=(char **)NULL && acp->a==(char **)NULL)
     if(BADB(sdexp)        ||
        BADB(edexp)        ||
        BADBL(suffix)      ||
        BADBL(suffixeq)    ||
        BADBL(prefix)      ||
        BADBL(noise)       ||
        BADB(eqprefix)     ||
        BADB(ueqprefix)    ||

        BADB(database)     ||
        BADB(filespec)     ||
        BADCL(worddef)     ||
        BADCL(blockdelim)  ||
        BADCL(allow)       ||
        BADCL(ignore)      ||

        BADB(profile)
     ){
        acp=closeapicp(acp);
     }
#undef BADB
#undef BADBL
#undef BADCL
    }
 return(acp);
}                                                   /* end dupapicp() */
/**********************************************************************/
#endif

DDMMAPI *
openddmmapi(QNODE_OP qnodeOp, void *data, FOP mmOp)
{
	DDMMAPI *mapi;
	APICP	*cp;
	TXPMBUF *pmbuf = TXPMBUFPN;
	TXbool	isRankedQuery = (mmOp == FOP_RELEV || mmOp == FOP_PROXIM);

	mapi = TX_NEW(pmbuf, DDMMAPI);
	if (!mapi) goto err;
	mapi->self = mapi;
	if (globalcp)
		cp = globalcp;
	else
	{
		cp = TXopenapicp();
		globalcp = cp;
	}
	mapi->cp = dupapicp(cp);
	if(!mapi->cp) goto err;
	if(mmOp == FOP_NMM) /* Like is the only one to care about delims */
	{
		mapi->cp->sdexp[0] = '\0';
		mapi->cp->edexp[0] = '\0';
	}
	mapi->mmapi = openmmapi(NULL, isRankedQuery, mapi->cp);
	if(mapi->mmapi == (MMAPI *)NULL)
	{
		putmsg(MWARN, NULL, "Openmmapi Failed");
		goto err;
	}
	switch(qnodeOp)
	{
	case FIELD_OP:
		mapi->query = TXfldToMetamorphQuery(data);
#if DEBUG
		DBGMSG(6,(999, NULL, "Setting query to %s", mapi->query));
		DBGMSG(9,(999, NULL, "STRCASECMP('power', 'reuniting') = %d",
			strcasecmp("power", "reuniting")));
#endif
		if(mmOp != FOP_MMIN) /* Don't NULL mapi->mmapi JMT 97-10-16 */
		{
			if (!setmmapi(mapi->mmapi, mapi->query,
				      isRankedQuery))
			{
				putmsg(MWARN, NULL, "Setmmapi %s Failed", mapi->query);
				goto err;
			}
			mapi->mmapiIsPrepped = TXbool_True;
		}
		else                            /* LIKEIN */
		{
			if (!setmmapi(mapi->mmapi, "placeholder LIKEIN query",
				      isRankedQuery))
			{
				goto err;
			}
			/* do not set `mmapiIsPrepped' true: not true query? */
		}
		break;
	case NAME_OP:
	default:
		break;
	}
	mapi->qtype = qnodeOp;
	mapi->qdata = data;
	mapi->buffer = NULL;
	mapi->self = mapi;
        goto finally;

err:
        mapi = closeddmmapi(mapi);
finally:
	return(mapi);
}

DDMMAPI *
closeddmmapi(mapi)
DDMMAPI *mapi;
{
        if (mapi == (DDMMAPI *)NULL)
                return mapi;
	if (mapi == mapi->self)
	{
		if (mapi->query != (char *)NULL)
			free(mapi->query);
		if (mapi->mmapi != (MMAPI *)NULL)
			closemmapi(mapi->mmapi);
		if (mapi->cp != (APICP *)NULL)
		{
			closeapicp(mapi->cp);
		}
#ifdef NEVER
		if (mapi->qtype == FIELD_OP)
			if (mapi->qdata != (void *)NULL)
				closefld(mapi->qdata);
#endif
		if(mapi->qtype != FIELD_OP && mapi->qtype != NAME_OP)
		{
			closepred(mapi->qdata);
		}
			if (mapi->buffer != (void *)NULL)
			{
				if (mapi->mmaplen)
#ifdef HAVE_MMAP
				munmap(mapi->buffer, mapi->mmaplen);
#else
				;
#endif
			else if(mapi->freebuf)
				free(mapi->buffer);
		}
		TXcloseproxbtree(mapi->bt);
		_freelst(mapi->wordlist);
		free(mapi);
	}
        return (DDMMAPI *)NULL;
}

/******************************************************************/

int
setddmmapi(DBTBL *tbl,		/* Table associated with query */
	   DDMMAPI *ddmmapi,	/* DDMMAPI structure to set */
	   FOP mmOp)
/* Returns -1 on error, 0 on success.
 */
{
	char *query = NULL;
	TXbool qalloced = TXbool_False;
	TXbool	isRankedQuery = (mmOp == FOP_RELEV || mmOp == FOP_PROXIM);
	int	ret;

	ddmmapi = ddmmapi->self;
	switch(ddmmapi->qtype)
	{
	case FIELD_OP:
		query = TXfldToMetamorphQuery(ddmmapi->qdata);
		qalloced = TXbool_True;
		break;
	case NAME_OP:
	{
		FLD	*f;
		char	*fname;

		fname = ddmmapi->qdata;
		f = dbnametofld(tbl, fname);
		if (f == (FLD *)NULL)
			/* Do not yap; this appears to happen during init?
			 * See Texis test061
			 */
			goto err;
		query = TXfldToMetamorphQuery(f);
		qalloced = TXbool_True;
		break;
	}
	default:
	{
		FLD	*f;
		FLDOP	*fo;

		fo = dbgetfo();
		pred_eval(tbl, ddmmapi->qdata, fo);
		f = fopop(fo);
		query = TXfldToMetamorphQuery(f);
		qalloced = TXbool_True;
		closefld(f);
		fo = foclose(fo);
		break;
	}
	}
	if (query == (char *)NULL)
	{
		putmsg(MWARN+UGE, __FUNCTION__, "No query specified");
		goto err;
	}
	if (ddmmapi->query != (char *)NULL &&
	    ddmmapi->mmapiIsPrepped &&
	    strcmp(query, ddmmapi->query) == 0)
		goto ok;			/* Query still the same */
	ddmmapi->mmapiIsPrepped =
		(setmmapi(ddmmapi->mmapi, query, isRankedQuery) != NULL);
	ddmmapi->query = TXfree(ddmmapi->query);
	if(!qalloced)
		ddmmapi->query = TXstrdup(TXPMBUFPN, __FUNCTION__, query);
	else
	{
		ddmmapi->query = query;
		qalloced = TXbool_False;
	}
ok:
	ret = 0;
	goto finally;

err:
	/* `ddmmapi->query' is wrong too: */
	ddmmapi->query = TXfree(ddmmapi->query);
	/* Most callers ignore our return code; set a flag for later use: */
	ddmmapi->mmapiIsPrepped = TXbool_False;
	ret = -1;
finally:
	if (qalloced)
	{
		query = TXfree(query);
		qalloced = TXbool_False;
	}
	return(ret);
}

TXMSM
TXstrToTxmsm(s)
CONST char      *s;
{
	TXMSM	msm;

	for (msm = (TXMSM)0; msm < TXMSM_NUM; msm++)
		if (strcmpi(s, TxmsmNames[msm]) == 0) return(msm);
	return(TXMSM_UNKNOWN);
}

CONST char *
TXmsmToStr(msm)
TXMSM	msm;
{
	if ((unsigned)msm < (unsigned)TXMSM_NUM)
		return(TxmsmNames[(unsigned)msm]);
	return("unknown");
}

char *
TXfldToMetamorphQuery(fld)
FLD	*fld;		/* (in) field */
/* Returns alloc'd Metamorph query derived from `fld', converted
 * according to current mode.
 */
{
	static CONST char	fn[] = "TXfldToMetamorphQuery";
	char			*ret = CHARPN, *s, *e, *d;
	ft_strlst		hdr;
	size_t			na;
	TXMSM			msm;

	if ((fld->type & DDTYPEBITS) != FTN_STRLST)
		return(strdup(fldtostr(fld)));

	s = TXgetStrlst(fld, &hdr);
	e = s + hdr.nb - 2;			/*w/o last item nul, EOL nul*/
	msm = (TXApp != TXAPPPN ? TXApp->metamorphStrlstMode:TXMSM_equivlist);
	switch (msm)
	{
	case TXMSM_allwords:			/* `one two bear arms' */
	case TXMSM_anywords:			/* `one two bear arms @0' */
		na = (e - s) + 4;		/* +4 for " @0" + nul */
		if ((ret = (char *)malloc(na)) == CHARPN) goto maerr;
		d = ret;
		for ( ; s < e; s++, d++)
			*d = (*s == '\0' ? ' ' : *s);
		if (msm == TXMSM_anywords)
		{
			strcpy(d, " @0");
			d += 3;
		}
		*(d++) = '\0';
		break;
	case TXMSM_allphrases:			/* `"one" "two" "bear arms"' */
	case TXMSM_anyphrases:			/*"one" "two" "bear arms" @0*/
		na = (e - s) + 3*TXgetStrlstLength(&hdr, s) + 4;
		if ((ret = (char *)malloc(na)) == CHARPN) goto maerr;
		d = ret;
		*(d++) = '"';
		for ( ; s < e; s++, d++)
		{
			if (*s == '\0')		/* end of a string item */
			{
				*(d++) = '"';
				*(d++) = ' ';
				*d = '"';
			}
			else
				*d = *s;
		}
		*(d++) = '"';
		if (msm == TXMSM_anyphrases)
		{
			strcpy(d, " @0");
			d += 3;
		}
		*(d++) = '\0';
		break;
	case TXMSM_equivlist:			/* `(one,two,bear arms)' */
	default:
		na = (e - s) + 3;
		if ((ret = (char *)malloc(na)) == CHARPN)
		{
		maerr:
			TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn, na, 1);
			goto err;
		}
		d = ret;
		*(d++) = '(';
		for ( ; s < e; s++, d++)
			*d = (*s == '\0' ? ',' : *s);
		*(d++) = ')';
		*(d++) = '\0';
		break;
	}
	goto done;

err:
	if (ret != CHARPN) free(ret);
	ret = CHARPN;
done:
	return(ret);
}
