/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"
#include "tin.h"

/************************************************************/
/*  The functions in this module are designed to give a means
 *  of dealing with ranges as found in TTLs.  This could
 *  dramatically speed up orttl, andttl, subttl etc.
 *
 *  They assume intimate knowledge of TTL structure.
 */

typedef struct TXRANGE
{
	tin_t	start;
	tin_t	end;
	tin_t	len;
}	RANGE;

static RANGE *closerange ARGS((RANGE *));

/******************************************************************/

static RANGE	*
closerange(a)
RANGE	*a;
{
	if(a)
		free(a);
	return NULL;
}

/************************************************************/
/*	Do the ranges intersect.
 */

static int intersect ARGS((RANGE *, RANGE *, int));

static int
intersect(a, b, ovlap)
RANGE	*a;
RANGE	*b;
int	ovlap;
{
	if(!a || !b)
		return 0;
	if((a->start >= b->start) && (a->start <= (b->end + ovlap)))
		return 1;
	if((b->start >= a->start) && (b->start <= (a->end + ovlap)))
		return 1;
	return 0;
}

/************************************************************/

static int duprange ARGS((RANGE *, RANGE *));

static int
duprange(ra, rc)
RANGE	*ra, *rc;
{
	if (rc && ra)
	{
		memcpy(rc, ra, sizeof(RANGE));
		return 0;
	}
	return -1;
}

/******************************************************************/

static RANGE *merge ARGS((RANGE *, RANGE *, RANGE *));

static RANGE *
merge(a, b, c)
RANGE	*a;
RANGE	*b;
RANGE	*c;
{
	static char Fn[] = "merge";

	if(!intersect(a, b, 1))
		return NULL;
	if (a->start < b->start)
		c->start = a->start;
	else
		c->start = b->start;
	if (a->end < b->end)
		c->end = b->end;
	else
		c->end = a->end;
	c->len = c->end - c->start;
	return c;
}

/************************************************************************/

/* returns next value in ttl list. returns 0 if there aren't any more */

static int getrange ARGS((TTL *, RANGE *));

static int
getrange(tl,r)
TTL	*tl;
RANGE	*r;
{
	tin_t	pval;
	if(tl->irun)
	{
RUN:
		r->start = tl->val + 1;
		r->len = tl->irun - 1;
		r->end = r->start + r->len;

		tl->val  += tl->irun;
		tl->irun = 0;
#ifdef DEBUG
		DBGMSG(9, (999, NULL, "getrange %d --> %d", r->start, r->end));
#endif
		return(1);
	}
	if(tl->gp<tl->end)
	{
		tl->gp=ivsl(tl->gp,&pval);
#ifdef DEBUG
		DBGMSG(9,(999, NULL, "1st pval = %d", pval));
#endif
		if(pval==0)
		{
			tl->gp=ivsl(tl->gp,&tl->irun);
#ifdef DEBUG
			DBGMSG(9,(999, NULL, "2nd pval = %d", pval));
#endif
			goto RUN;
		}
		pval+=tl->val;
		tl->val=pval;
		r->start = pval;
		r->end = pval;
		r->len = 0;
#ifdef DEBUG
		DBGMSG(9,(999, NULL, "getrange %d --> %d", r->start, r->end));
#endif
		return(1);
	}
	return(0);
}

/************************************************************************/

 /* puts a new value at the end of a ttl. the values must be added in
 ascending order. returns 1 if added, 0 if not added  */

static int putrange ARGS((TTL *, RANGE *));

static int
putrange(tl,r)
TTL	*tl;
RANGE	*r;
{                             /* 17 == 0 + orun-length + new vsl */
#ifdef DEBUG /* WTF */
	DBGMSG(9,(999, NULL, "Putrange %d --> %d", r->start, r->end));
#endif
	if(tl->pp+17>tl->end) /* there are three possible vsl's to be written */
	{
		byte  *new;   /* keep list integrity while increasing size */
		size_t bufsz=tl->bufsz;
		size_t off=tl->pp - tl->buf;
		bufsz+=TTLALLOCSZ;
		if((new=(byte *)malloc(bufsz))==(byte *)NULL)
			return(0);
		memcpy((void *)new,(void *)tl->buf,off);
		free(tl->buf);
		tl->gp=new+(tl->gp-tl->buf);    /* re-assign the get pointer */
		tl->pp=new+off;                 /* reassign the put pointer */
		tl->buf=new;
		tl->bufsz=bufsz;
		tl->end=new+bufsz;
	}

	if(r->start<tl->val)
	{
		if(r->start<(tl->val-tl->orun))
		{
			putmsg(MERR,"putttl","order error");
			return(0);		/* insertion out of order */
		}
		if (r->end > tl->val)
		{
			tl->orun += (r->end - tl->val);
			tl->count += (r->end - tl->val);
			tl->val = r->end;
		}
		return 1;
	}
	else if(tl->val==r->start)        /* duplicate insertion is ok */
	{
		tl->val = r->end;
		tl->orun += (r->len);
		tl->count += (r->len);
		return(1);
	}

	if(r->start-tl->val==1)              /* check for a run of ones */
	{
		tl->orun += 1;
		tl->orun += r->len;
	}
	else
	{
		if(tl->orun!=0)              /* spit the orun out */
		{
			if(tl->orun!=1)
				TXoutputVariableSizeLong(TXPMBUFPN, &tl->pp,
							 (ulong)0, NULL);
			TXoutputVariableSizeLong(TXPMBUFPN, &tl->pp, tl->orun,
						 NULL);
			tl->orun=0;
		}
		TXoutputVariableSizeLong(TXPMBUFPN, &tl->pp,
					 r->start - tl->val, NULL);
					/* ttls are stored in delta form */
		tl->orun=r->len;
	}
	tl->val=r->end;
	tl->count += (r->len + 1);
	return(1);
}

/************************************************************/

TTL	*
orttl(a, b)
TTL	*a;
TTL	*b;
{
	RANGE	rra, rrb, rrc, *ra, *rb, *rc;
	TTL	*c;
	int	aok, bok;

	ra = &rra;
	rb = &rrb;
	rc = &rrc;
#ifndef NO_INFINITY
	if(TXisinfinite(a))
	{
		closettl(b);
		return a;
	}
	if(TXisinfinite(b))
	{
		closettl(a);
		return b;
	}
#endif /* NO_INFINITY */
	c = openttl();
	if(!c)
		return c;

	rewindttl(a);
	rewindttl(b);
#ifdef DEBUG
	DBGMSG(9,(999, NULL, "In orttl"));
#endif
	aok = getrange(a, ra);
	bok = getrange(b, rb);
	while(aok && bok)
	{
		if(intersect(ra, rb, 1))
		{
			merge(ra, rb, rc);
			putrange(c, rc);
			aok = getrange(a, ra);
			bok = getrange(b, rb);
			continue;
		}
		if(rra.start < rrb.start)
		{
			putrange(c, ra);
			aok = getrange(a, ra);
		}
		else
		{
			putrange(c, rb);
			bok = getrange(b, rb);
		}
	}
	while(aok)
	{
		putrange(c, ra);
		aok = getrange(a, ra);
	}
	while(bok)
	{
		putrange(c, rb);
		bok = getrange(b, rb);
	}
	closettl(a);
	closettl(b);
	if(c->orun!=0)                             /* spit the run out */
	{
		if(c->orun!=1)
			TXoutputVariableSizeLong(TXPMBUFPN, &c->pp, (ulong)0,
						 NULL);
		TXoutputVariableSizeLong(TXPMBUFPN, &c->pp, c->orun, NULL);
		c->orun=0;
	}
	return c;
}

/************************************************************/

#ifdef DEVEL
typedef struct TXHRANGE
{
	RANGE	range;
	int	ttl;
} HRANGE;

int hrcmp ARGS((CONST HRANGE *, CONST HRANGE *));

int
hrcmp(ha, hb)
CONST HRANGE	*ha, *hb;
{
	CONST RANGE	*ra, *rb;

	ra = &ha->range;
	rb = &hb->range;
	if(ra->start < rb->start)
		return -1;
	if(rb->start < ra->start)
		return 1;
	if(ra->end < rb->end)
		return 1;
	if(ra->end > rb->end)
		return -1;
	return 0;
}

/******************************************************************/

static int downheap ARGS((HRANGE *, int, int));

static int
downheap(hr, k, n)
HRANGE	*hr;
int	k, n;
{
	HRANGE	v;
	int	j;

	v = hr[k];
	while (k <= ((n-1)>>1))
	{
		j = k << 1;
		if(j < (n-1))
		{
			if(hrcmp(&hr[j], &hr[j+1]) == 1)
				j++;
		}
		if(hrcmp(&v, &hr[j]) != 1)
			break;
		hr[k] = hr[j];
		k = j;
	}
	hr[k] = v;
	return 0;
}

/******************************************************************/

static int heapremove ARGS((HRANGE *, int *));

static int heapremove(hr, maxsel)
HRANGE	*hr;
int	*maxsel;
{
	hr[0] = hr[*maxsel-1];
	downheap(hr, 0, *maxsel);
	*maxsel = *maxsel-1;
	return 0;
}
/******************************************************************/

TTL *
hormerge(inl)
TTL	**inl;
{
	HRANGE	ranges[MAXSELS];
	RANGE	currang, temp;
	int	maxsel, nsets = 0, i, ok[MAXSELS];
	TTL	*c;

	c = openttl();
	if(!c)
		return c;


	for(i=0; i < MAXSELS; i++)
	{
		if(inl[i])
		{
			rewindttl(inl[i]);
			ok[i] = getrange(inl[i], &ranges[i].range);
			ranges[i].ttl = i;
		}
		else
			ok[i] = 0;
		if(ok[i])
		{
			nsets++;
			maxsel = i+1;
		}
	}
	if(nsets == 0)
		return closettl(c);
	qsort(ranges, maxsel, sizeof(HRANGE), (int(*)(const void *, const void *))hrcmp);
	do {
		putrange(c, &ranges[0].range);
		if(!getrange(inl[ranges[0].ttl], &ranges[0].range))
		{
			heapremove(ranges, &maxsel);
		}
		else
			downheap(ranges, 0, maxsel);
	} while(maxsel);
	if(c->orun!=0)                             /* spit the run out */
	{
		if(c->orun!=1)
			TXoutputVariableSizeLong(TXPMBUFPN, &c->pp, (ulong)0,
						 NULL);
		TXoutputVariableSizeLong(TXPMBUFPN, &c->pp, c->orun, NULL);
		c->orun=0;
	}
	return c;
}

/******************************************************************/

TTL *
rormerge(inl)
TTL	**inl;
{
	RANGE	ranges[MAXSELS];
	RANGE	currang;
	int	ok[MAXSELS];
	int	minbeg = -1;
	int	minran = -1;
	int	justread, lastgr = -1;
	int	i, nsets = 0;
	int	maxsel = 0;
	TTL	*c;

	c = openttl();
	if(!c)
		return c;

/* Initialization */

	for(i=0; i < MAXSELS; i++)
	{
/* Get initial range. */
		if(inl[i])
			ok[i] = getrange(inl[i], &ranges[i]);
		else
			ok[i] = 0;
		if(ok[i])
		{
/* Count number of sets */
			nsets++;
/* Get smallest beginning */
			if(ranges[i].start < minbeg || minbeg == -1)
			{
				minbeg = ranges[i].start;
				minran = i;
			}
/* Keep track of how far we need to look  */
			maxsel = i;
		}
	}

/* Do it */

	while(nsets > 0)
	{
/* This should be the minimum beginning */
		i = minran;
		duprange(&ranges[i], &currang);
		ok[i] = getrange(inl[i], &ranges[i]);
		if(ok[i])
		{
			minbeg = ranges[i].start;
		}
		else
		{
			minbeg = -1;
			nsets --;
		}
		lastgr = i;
		do {
			i++;
			justread = 0;
			if(i > maxsel)
				i = 0;
			if(ok[i] && intersect(&currang, &ranges[i], 1))
			{
				RANGE	ncur;

				duprange(&currang, &ncur);
				merge(&ncur, &ranges[i], &currang);
				ok[i] = getrange(inl[i], &ranges[i]);
				if(!ok[i])
				{
					nsets --;
				}
				lastgr = i;
				if(i == minran)
					minbeg = -1;
				justread++;
			}
/* Should this be the new beginning */
			if((ranges[i].start < minbeg || minbeg == -1) &&
			   (ok[i] && ranges[i].start))
			{
				minbeg = ranges[i].start;
				minran = i;
			}
		} while (justread || (i != lastgr));
		putrange(c, &currang);
	}
	if(c->orun!=0)                             /* spit the run out */
	{
		if(c->orun!=1)
			TXoutputVariableSizeLong(TXPMBUFPN, &c->pp, (ulong)0,
						 NULL);
		TXoutputVariableSizeLong(TXPMBUFPN, &c->pp, c->orun, NULL);
		c->orun=0;
	}
	return c;
}

/******************************************************************/

#ifdef TEST

main()
{
	TTL	*a, *b, *c, *d;
	RANGE	ra;

	a = openttl();
	putttl(a, 1);
	putttl(a, 3);
	putttl(a, 5);
	putttl(a, 6);
	putttl(a, 7);
	putttl(a, 8);
	putttl(a, 10);
	putttl(a, 11);
	if(a->orun!=0)                             /* spit the run out */
	{
		if(a->orun!=1)
			TXoutputVariableSizeLong(TXPMBUFPN, &a->pp, (ulong)0,
						 NULL);
		TXoutputVariableSizeLong(TXPMBUFPN, &a->pp, a->orun, NULL);
		a->orun=0;
	}
	rewindttl(a);
	while(getrange(a, &ra))
	{
		printf("Got range %d -> %d (%d)\n", ra.start, ra.end, ra.len);
	}
}

#endif
#endif
