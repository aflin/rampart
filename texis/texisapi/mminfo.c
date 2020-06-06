/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"

#define MMINFO_300	1
#define MMINFO_NOTEXT	2
#define MMINFO_HITS	4
#define MMINFO_NHITS	8
#define MMINFO_QUERYOFFSETS     16

/******************************************************************/

extern APICP	*globalcp;

/******************************************************************/

static  APICP	*cp = NULL;
static  MMAPI	*mmapi=NULL;
static  char *lquery=NULL;

int
TXclosemminfo(void)
{
	if(lquery)
	{
		free(lquery);
		lquery = NULL;
	}
	if(mmapi)
		mmapi=closemmapi(mmapi);
	if(cp)
		cp = closeapicp(cp);
	return 0;
}

/******************************************************************/
int
mminfo(f1, f2, f3, f4, f5)
FLD	*f1;	/* Query */
FLD	*f2;	/* Data */
FLD	*f3;	/* No. hits */
FLD	*f4;	/* Which info */
FLD	*f5;	/* Include 300 messages */
{
/*	Lets start with a real stupid version, that we'll be able to add to */
	char	sbuf[256];
	char	*query = CHARPN;
	int	queryAlloced = 0;
	byte	*data;
	char	*hit;
	char	*output = NULL;
	size_t	datasz;
	ft_long	incmsg = 0, nhits = 0, maxhits = 0;
	HTBUF	*htbuf;
	int	i, ret;

	(void)f4;
	htbuf = openhtbuf();
	if(!htbuf) goto err;
	query = TXfldToMetamorphQuery(f1);
	queryAlloced = 1;
	data = getfld(f2, &datasz);
	if(f3)
	{
		maxhits = *(ft_long *)getfld(f3, NULL);
	}
	if(f5)
	{
		incmsg = *(ft_long *)getfld(f5, NULL);
	}
	if(!lquery || strcmp(lquery, query))
	{
		TXclosemminfo();
		if (queryAlloced)
		{
			lquery = query;
			queryAlloced = 0;
		}
		else
			lquery = strdup(query);
	}
	if(!mmapi)
	{
		cp = dupapicp(globalcp);
		cp->alintersects = 1;/* JMT 1999-05-21 */
		cp->alwithin = 1;/* JMT 1999-05-21 */
		mmapi = openmmapi(query, TXbool_False /* !isRankedQuery */,
                                  cp);
	}
	/* KNG 20080903 print offsets in original query of each set: */
	if (mmapi && (incmsg & MMINFO_QUERYOFFSETS))
	{
		for (i = 0; mmapi->acp->set[i] && *mmapi->acp->set[i]; i++)
			htbuf_pf(htbuf, "Set %d offset/len in query: %d %d\n",
				 i + 1, mmapi->acp->setqoffs[i],
				 mmapi->acp->setqlens[i]);
	}

	/* Inadvertently being passed NULL `data' sometimes due to Bug 5405;
	 * when fixed, can do proper SQL NULL support here?
	 */
	if (mmapi && data)
		for (hit = getmmapi(mmapi, data, data + datasz, SEARCHNEWBUF);
		     hit;
		     hit = getmmapi(mmapi, data, data + datasz, CONTINUESEARCH))
	{
		char	*what, *where;
		int	size;

		nhits++;
		if((maxhits > 0) && (nhits > maxhits))
			break;
		if(infommapi(mmapi, 0, &what, &where, &size) == 1)
		{
			char	*tt, *tw;
			int	ts, index;

			if(incmsg & MMINFO_300)
			{
				sprintf(sbuf, "%03d <Data from Texis>", MHIT);
				htbuf_write(htbuf, sbuf, strlen(sbuf));
				tt = what; tw = where; ts = size;
				index = 3;
				do
				{
					if(index > 3)
						tw -= (where - (char *)data);
					htsnpf(sbuf, sizeof(sbuf), " %wd %wd",
					     (EPI_HUGEINT)(tw - (char *)data),
					       (EPI_HUGEINT)ts);
					htbuf_write(htbuf, sbuf, strlen(sbuf));
				} while(infommapi(mmapi, index++,
					&tt, &tw, &ts)==1);
				htbuf_write(htbuf, "\n", strlen("\n"));
			}
			if((incmsg & MMINFO_NOTEXT) == 0)
			{
				htbuf_write(htbuf, where, (size_t)size);
				if(incmsg & MMINFO_300)
					htbuf_write(htbuf, "\n", strlen("\n"));
			}
			if(incmsg & MMINFO_300)
			{
				sprintf(sbuf, "%03d End of Metamorph hit\n", MENDHIT);
				htbuf_write(htbuf, sbuf, strlen(sbuf));
			}
		}
	}
	if(incmsg & MMINFO_HITS)
	{
		htsnpf(sbuf, sizeof(sbuf), "%03d hits: %wd\n",
		       MFILEINFO, (EPI_HUGEINT)nhits);
		htbuf_write(htbuf, sbuf, strlen(sbuf));
	}
	if(incmsg & MMINFO_NHITS)
	{
		htsnpf(sbuf, sizeof(sbuf), "%10wd", (EPI_HUGEINT)nhits);
		htbuf_write(htbuf, sbuf, strlen(sbuf));
	}
#ifdef NEVER
	closemmapi(mmapi);
	closeapicp(cp);
#endif
	htbuf_getdata(htbuf, &output, 0x3);
	closehtbuf(htbuf);
	if(output)
		TXsetresult(f1, output);
	else
		TXsetresult(f1, strdup(""));
	ret = 1;
	goto done;

err:
	ret = 0;
done:
	if (queryAlloced) free(query);
	return(ret);
}

/******************************************************************/

int
TXsetresult(f1, buf)
FLD	*f1;
char	*buf;
{
	size_t	sz;

	sz = strlen(buf);
	f1->type = FTN_CHAR | DDVARBIT ;
	f1->elsz = 1;
	setfldandsize(f1, buf, sz + 1, FLD_FORCE_NORMAL);
	return 0;
}
