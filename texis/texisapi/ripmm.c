/* -=- kai-mode: John -=- */
/*
 *    JMT - Rework the code for relevancy
 */

/* PBR 5/1/95 added longest word in phrase processing */

#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include "dbquery.h"
#include "texint.h"
#include "unicode.h"

#if PM_FLEXPHRASE
#  ifndef NEW_PHRASEPROC
#    define NEW_PHRASEPROC
#  endif
#endif

/******************************************************************/

#define EL(x) mm->mme->el[x]
#define NELS  mm->mme->nels

static int CDECL
ripcmp(MMQI *a, MMQI *b)
{
	int cmp = strcmp((char *) a->s, (char *) b->s);

	if (!cmp)
		return (a->setno - b->setno);
	return (cmp);
}

/************************************************************************/

MMQL *
TXclosemmql(ql, frees)
MMQL *ql;
int frees;
{
	int i;
	if (ql != MMQLPN)
	{
		if (ql->lst != MMQIPN)
		{
#ifdef NEW_PHRASEPROC
			for(i=0; i < ql->n; i++)
			{
				if(ql->lst[i].words)
					free(ql->lst[i].words);
				if(ql->lst[i].lens)
					free(ql->lst[i].lens);
			}
#endif
			if(frees)
			{
				for(i=0; i < ql->n; i++)
					if(ql->lst[i].s)
						free(ql->lst[i].s);
			}
			free(ql->lst);
		}
		free(ql);
	}
	return (MMQLPN);
}

/************************************************************************/
	/* PBR 5/1/95 added longest word in phrase processing */

#define PHTMP struct temp_phrase_obj
PHTMP
{
	byte *s;
	int len;
	int wild;
	int suffixproc;
};
#define MAXPHWDS 20		/* maximium words in a phrase */

#ifndef NEW_PHRASEPROC
static byte *phraseproc ARGS((MMQI *));

static byte *
phraseproc(qi)
MMQI *qi;
{
	PHTMP tl[MAXPHWDS];
	int n = 0, maxlen = 0, maxi = 0;
	int needmm = 0;
	byte *p, *s;

	tl[0].wild = 0;		/* set wild (*) to off  */

	for (p = s = qi->s; n < MAXPHWDS; s++)
	{
		if (*s == '\0' || *s == ' ' || *s == '-' || *s == '*')	/* if phrase, wild or end */
		{
			tl[n].s = p;
			tl[n].len = s - p;
			tl[n].suffixproc = qi->suffixproc;

			if (*s == ' ' || *s == '-' || *s == '*')	/* Need mm on phrase */
				needmm = 1;
#ifdef MORE_BROKEN_SUFFIX
#ifdef BROKEN_SUFFIX
			if (*s == ' ' || *s == '-' || *s == '*')	/* make sure no suffix on phrase */
#else
			if (*s == '*')	/* make sure no suffix on wild-cards */
#endif
				qi->suffixproc = 0;
#else
			if (*s == ' ' || *s == '-' || *s == '*')	/* make sure no suffix on phrase */
				tl[n].suffixproc = 0;
#endif

			if (*s == '*')
				tl[n].wild = 1;

			if (tl[n].len > maxlen &&
			    (n == 0 || tl[n - 1].wild == 0))	/* Don't let back half of * be prefix */
			{
				maxlen = tl[n].len;
				maxi = n;
			}
			++n;
			tl[n].wild = 0;

			for (p = s; *p == ' ' || *p == '-' || *p == '*'; ++p);	/* eat the delim(s) */
			s = p;
			if (*s == '\0')
				break;
		}
	}
	qi->s = tl[maxi].s;
	qi->len = tl[maxi].len;
	qi->wild = tl[maxi].wild;
	qi->needmm = needmm;
#ifndef NEW_PHRASEPROC
	qi->suffixproc = tl[maxi].suffixproc;
#endif
	return (qi->s);
}
#undef PHTMP
#endif /* !NEW_PHRASEPROC */

/************************************************************************/

    /* strips those things that 3DB can do out of an existing mmapi */

MMQL *
mmrip(mm, isfdbi)
MMAPI   *mm;
int     isfdbi;                         /* nonzero: for INDEX_MM/INDEX_FULL */
{
	int i, j, k, n;
	int cvlog = 0;
	MMQL *ql = (MMQL *) calloc(1, sizeof(MMQL));

	if (ql != MMQLPN)
	{
/* Make nice */
		if (!mm->mme)
		{
			free(ql);
			return NULL;
		}
		if (mm->mme->nands == 0 && (NELS == mm->mme->intersects))
			cvlog++;
		for (i = 0; i < NELS; i++)	/* perform initial count */
		{
			switch (EL(i)->pmtype)
			{
			case PMISPPM:
				ql->n += EL(i)->lstsz;
				break;
			case PMISSPM:
				ql->n += 1;
				break;
			default:
				ql->dkpm += 1;
				break;
			}
		}
		if (ql->n &&
		   (ql->lst = (MMQI *) calloc(ql->n, sizeof(MMQI)))
		            == (MMQI *) NULL)
		{
			free(ql);
			return (MMQLPN);
		}
		for (n = i = 0; i < NELS; i++)	/* perform initial count */
		{
			byte **lst = (byte **) (EL(i)->lst);

			switch ((EL(i)->pmtype))
			{
			case PMISPPM:
				for (j = 0; j < EL(i)->lstsz; j++, n++)
				{
					ql->lst[n].s = lst[j];
					ql->lst[n].setno = i;
#ifdef PM_NEWLANG
					ql->lst[n].suffixproc = TX_PPM_TERM_IS_LANGUAGE_QUERY(EL(i)->ps, j) && EL(i)->mm3s->suffixproc;
#else
					ql->lst[n].suffixproc = EL(i)->lang && mm->acp->suffixproc;
#endif
					ql->lst[n].wild = 0;
					if (!cvlog || (EL(i)->logic != LOGISET))
						ql->lst[n].logic = EL(i)->logic;
					else
						ql->lst[n].logic = LOGIAND;
					ql->lst[n].orpos = EL(i)->orpos;
#ifdef NEW_PHRASEPROC
					ql->lst[n].len = strlen((char *)lst[j]);
					ql->lst[n].setno = i;
					if (TX_PPM_PHRASE(EL(i)->ps, j))/* Time to drill further */
                                          ql->lst[n].nwords = TX_PPM_PHRASE(EL(i)->ps, j)->nterms;
					else
						ql->lst[n].nwords = 1;
					ql->lst[n].words = (byte **)calloc(ql->lst[n].nwords, sizeof(byte *));
					ql->lst[n].lens = (size_t *)calloc(ql->lst[n].nwords, sizeof(size_t));
					if (TX_PPM_PHRASE(EL(i)->ps, j))/* Time to drill further */
					{
						PMPHR *tph;

						/* KNG 20051017 see 000112 */
						if (!isfdbi) ql->lst[n].needmm = 1;
						for(tph = TX_PPM_PHRASE(EL(i)->ps, j); tph && tph->prev; tph = tph->prev);
						for(k=0; k < ql->lst[n].nwords && tph; k++, tph = tph->next)
						{
							ql->lst[n].words[k] = tph->term;
							ql->lst[n].lens[k] = tph->len;
						}
					}
					else
					{
						ql->lst[n].words[0] = ql->lst[n].s;
						ql->lst[n].lens[0] = strlen((char *)ql->lst[n].s);
					}
#endif
				}
				break;
			case PMISSPM:
				ql->lst[n].s = lst[0];
				ql->lst[n].setno = i;
#ifdef PM_NEWLANG
				ql->lst[n].suffixproc = EL(i)->ss->lang & EL(i)->mm3s->suffixproc;
#else
				ql->lst[n].suffixproc = EL(i)->lang & mm->acp->suffixproc;
#endif
				if (EL(i)->ss->next != (SPMS *) NULL)
				{                       /* KNG eg. 'aaa*bbb' */
					ql->lst[n].wild = MMRIP_WILDMIDTRAIL;
					ql->lst[n].suffixproc = 0;
					/* KNG 20051017 note that we may not
					 * need MM; see openfdbif() sslinear:
					 */
					ql->lst[n].needmm = 1;
				}
				else
					ql->lst[n].wild = 0;
				if (!cvlog || (EL(i)->logic != LOGISET))
					ql->lst[n].logic = EL(i)->logic;
				else
					ql->lst[n].logic = LOGIAND;
				ql->lst[n].orpos = EL(i)->orpos;
#ifdef NEW_PHRASEPROC
				ql->lst[n].len = strlen((char *)lst[0]);
				if(lst[0][ql->lst[n].len-1] == '*')
				{			/* KNG eg. `aaa*' */
/* Don't need to set needmm here, as it is we already have the match */
					ql->lst[n].wild |= MMRIP_WILDMIDTRAIL;
					ql->lst[n].suffixproc = 0;
					ql->lst[n].len--;
				}
				if(isfdbi && lst[0][0] == '*')
				{	                /* leading wildcard */
					/* openfdbif() should skip the `*' */
					/* KNG 20040712 actually it doesn't
					 * but uses SPM directly, same effect
					 */
					ql->lst[n].wild |= MMRIP_WILDLEAD;
					ql->lst[n].suffixproc = 0;
					/* don't set needmm: w/o wildsufmatch/
					 * wildoneword this is linear anyway,
					 * and w/them we don't needmm for this
					 */
				}
				if(EL(i)->ss->phrase)/* Time to drill further */
					ql->lst[n].nwords = EL(i)->ss->phrase->nterms;
				else
					ql->lst[n].nwords = 1;
				ql->lst[n].words = (byte **)calloc(ql->lst[n].nwords, sizeof(byte *));
				ql->lst[n].lens = (size_t *)calloc(ql->lst[n].nwords, sizeof(size_t));
				if(EL(i)->ss->phrase)/* Time to drill further */
				{
					PMPHR *tph;

                                        /* KNG 000112 With INDEX_MM/INDEX_FULL
                                         * we don't needmm just because there's
                                         * a phrase; let openfdbif() determine:
                                         */
					if (!isfdbi) ql->lst[n].needmm = 1;
					for(tph = EL(i)->ss->phrase; tph && tph->prev; tph = tph->prev);
					for(k=0; k < ql->lst[n].nwords && tph; k++, tph = tph->next)
					{
						ql->lst[n].words[k] = tph->term;
						ql->lst[n].lens[k] = tph->len;
					}
				}
				else
				{
					ql->lst[n].words[0] = ql->lst[n].s;
					/* KNG 20040712 if prefix wildcard,
					 * length is 0, not patlen:
					 */
					ql->lst[n].lens[0] =
			((ql->lst[n].wild & MMRIP_WILDLEAD) ? 0 : EL(i)->ss->patlen);
				}
#endif
				n++;
				break;
			default:
				break;
			}
		}
#ifndef NEW_PHRASEPROC
		/* PBR 5/1/95 changed to add better phrase stuff */
		for (i = 0; i < n; i++)		/* check for phrases and wildcards + get length  */
			phraseproc(&ql->lst[i]);

#endif
		qsort(ql->lst, ql->n, sizeof(MMQI), (int (CDECL *) ARGS((CONST void *, CONST void *))) ripcmp);
	}
	return (ql);
}


/************************************************************************/

MMQL *
mmripq(query)
char *query;
{
	/* Similar sep as Metamorph query parser?  But we are actually
	 * parsing a document (for LIKEIN)?:
	 */
	static const char	sep[] = " \t\r\n\v\f";
#define SEP_LEN	(sizeof(sep) - 1)
	TXPMBUF *pmbuf = TXPMBUFPN;
	MMQL *ql = NULL;
	const char	*item, *queryEnd = query + strlen(query);
	size_t	numItems = 0, numItemsAlloced = 0, itemLen;

	ql = TX_NEW(pmbuf, MMQL);
	if (!ql) goto err;

	/* Count and parse in one pass, to avoid Bug 8048: */
	for (item = query; *item; item += itemLen)
	{
		/* Get next whitespace-separated `item'/`itemLen': */
		item += TXstrspnBuf(item, queryEnd, sep, SEP_LEN);
		itemLen = TXstrcspnBuf(item, queryEnd, sep, SEP_LEN);

		/* Skip leading `-' and/or `+': */
		for (;
		     item < queryEnd && itemLen > 0 &&
			     (*item == '-' || *item == '+');
		     item++, itemLen--)
			;
		if (itemLen <= 0) continue;     /* empty item */

                /* Add the item to `ql': */
		if (!TX_INC_ARRAY(pmbuf, &ql->lst, numItems, &numItemsAlloced))
			goto err;
		ql->lst[numItems].s = (byte *)TXstrndup(pmbuf, __FUNCTION__,
                                                        item, itemLen);
		if (!ql->lst[numItems].s) goto err;
                ql->n++;                        /* after item alloced */
		ql->lst[numItems].len = itemLen;
		ql->lst[numItems].setno = numItems;
		ql->lst[numItems].suffixproc = 0;
		ql->lst[numItems].wild = 0;
		ql->lst[numItems].logic = LOGISET;
		ql->lst[numItems].orpos = numItems;
		ql->lst[numItems].nwords = 1;
		ql->lst[numItems].words = (byte **)TXcalloc(pmbuf,__FUNCTION__,
                                                            1, sizeof(byte *));
                if (!ql->lst[numItems].words) goto err;
		ql->lst[numItems].lens = (size_t *)TXcalloc(pmbuf,__FUNCTION__,
                                                            1, sizeof(size_t));
                if (!ql->lst[numItems].lens) goto err;
		ql->lst[numItems].words[0] = ql->lst[numItems].s;
		ql->lst[numItems].lens[0] = ql->lst[numItems].len;
		numItems++;

                /* Add the item's negation to `ql': */
		if (!TX_INC_ARRAY(pmbuf, &ql->lst, numItems, &numItemsAlloced))
			goto err;
                ql->lst[numItems].s = (byte *)TXstrcat2("-",
                                               (char *)ql->lst[numItems-1].s);
                if (!ql->lst[numItems].s) goto err;
                ql->n++;                        /* after item alloced */
                ql->lst[numItems].len = strlen((char *)ql->lst[numItems].s);
                ql->lst[numItems].setno = numItems;
                ql->lst[numItems].suffixproc = 0;
                ql->lst[numItems].wild = 0;
                ql->lst[numItems].logic = LOGINOT;
                ql->lst[numItems].orpos = numItems;
                ql->lst[numItems].nwords = 1;
                ql->lst[numItems].words=(byte **)TXcalloc(pmbuf, __FUNCTION__,
                                                          1, sizeof(byte *));
                if (!ql->lst[numItems].words) goto err;
                ql->lst[numItems].lens =(size_t *)TXcalloc(pmbuf, __FUNCTION__,
                                                           1,sizeof(size_t));
                if (!ql->lst[numItems].lens) goto err;
                ql->lst[numItems].words[0] = ql->lst[numItems].s;
                ql->lst[numItems].lens[0] = ql->lst[numItems].len;
                numItems++;
        }

        qsort(ql->lst, ql->n, sizeof(MMQI),
              (int (CDECL *)(const void *, const void *)) ripcmp);
        goto finally;

err:
	ql = TXclosemmql(ql, 1);
finally:
	return (ql);
#undef SEP_LEN
}


/************************************************************************/

MMTBL *closemmtbl ARGS((MMTBL *));

MMTBL *
closemmtbl(mt)
MMTBL *mt;
{
	if (mt != MMTBLPN)
	{
		if (mt->cp != APICPPN)
			mt->cp = closeapicp(mt->cp);
#if 0
		if (mt->mm != MMAPIPN)
			mt->mm = closemmapi(mt->mm);
#endif
		if (mt->ql != MMQLPN)
			mt->ql = TXclosemmql(mt->ql, 0);
		if (mt->bt != BTREEPN)
			mt->bt = closebtree(mt->bt);
		if (mt->bdbf != DBFPN)
			mt->bdbf = closedbf(mt->bdbf);
		if (mt->tl != TTLPN)
			mt->tl = closettl(mt->tl);
		free(mt);
	}
	return (MMTBLPN);
}

/************************************************************************/
		     /* compare callback for btree */

static int mmbtcmp ARGS((void *, size_t, void *, size_t, void *));

static int
mmbtcmp(a, al, b, bl, usr)
void *a;
size_t al;
void *b;
size_t bl;
void *usr;
{
	int rc, tocmp;

	tocmp = al < bl ? al : bl;
	rc = memcmp(a, b, tocmp);
	if (rc == 0)
		rc = al - bl;
	if (rc == 0)
		rc = 1;
	return rc;
}

/************************************************************************/

MMTBL *
openmmtbl(tblnm)
char *tblnm;
{
	MMTBL *mt = (MMTBL *) calloc(1, sizeof(MMTBL));

	if (mt != MMTBLPN)
	{
		char bdfn[1024];

		TXstrncpy(bdfn, tblnm, 1019);
		strcat(bdfn, ".blb");

		mt->query = (char *) NULL;
		mt->cp = APICPPN;
		mt->mm = MMAPIPN;
		mt->ql = MMQLPN;
		mt->bt = BTREEPN;
		mt->bdbf = DBFPN;
		mt->tl = TTLPN;

		if ((mt->cp = TXopenapicp()) != APICPPN)
		{
			mt->cp->prefixproc = 0;
			mt->cp->keepnoise = 0;
		}

		if (mt->cp == APICPPN ||
/*
   (mt->mm=openmmapi((char *)NULL,mt->cp))==MMAPIPN ||
 */
		    (mt->bdbf = opendbf(TXPMBUFPN, bdfn, O_WTF)) == DBFPN ||
		 (mt->bt = openbtree(tblnm, TTBTPSZ, TTBTCSZ, 0, O_RDWR)) == BTREEPN
			)
			return (closemmtbl(mt));
		btsetcmp(mt->bt, mmbtcmp);
	}
	return (mt);
}

/************************************************************************/

static TTL *ormerge ARGS((TTL **, int, int));

static TTL *
ormerge(inl, intersects, n)
TTL **inl;
int intersects;
int n;
{
	int i, nt, sets, f = 0, maxsel = 0;
	ulong *token, low, nlow, prev;
	TTL *rc = openttl();

/* Isinfinite handling is interesting here.  If intersects is 0 then
   return an infinite.  If not, look for intersects-1 in the remaining? */

	token = (ulong *)calloc(n, sizeof(ulong));
	if(!token)
		return NULL;
	if (!rc)
		goto zerr;
	low = (ulong)~0L;
	for (i = 0; i < n; i++)	/* reset the piles */
	{
		if (inl[i])
		{
#ifndef NO_INFINITY
			if (TXisinfinite(inl[i]))
			{
				if (intersects > 1)
				{
					token[i] = 0;
					intersects--;
					continue;
				}
			}
#endif
			rewindttl(inl[i]);
			f++;
			maxsel = i + 1;
			if (!getttl(inl[i], &token[i]))
			{
				token[i] = 0;
				inl[i] = closettl(inl[i]);
				f--;
			}
			else
			{
				if (token[i] < low)
					low = token[i];
			}
		}
		else
			token[i] = 0;
	}
#if DEBUG
	DBGMSG(1, (999, NULL, "ormerge of %d - %d", intersects, maxsel));
#endif
	if (!f)
	{
		rc = closettl(rc);
		goto zerr;
	}
	prev = 0;
	nt = maxsel;
	while (f)
	{
		nlow = (ulong)~0L;
#ifdef DEBUG
		DBGMSG(4, (999, NULL, "ormerge found %d of %d", nt, intersects));
#endif
		if (nt <= intersects)
			break;	/* not enough piles for intersects - all done */
		sets = 0;
		for (i = 0; i < maxsel; i++)
		{		/* see how many of same token */
			if (token[i] == low)
			{
				sets++;
				if (sets == 1 + intersects)
				{	/* got enough to satisfy intersects */
					if (putttl(rc, low) == 0)
						goto zerr;
				}
				if (!getttl(inl[i], &token[i]))
				{
					token[i] = 0;
					inl[i] = closettl(inl[i]);
					f--;
					nt--;
				}
				else
				{
					if (token[i] < nlow)
						nlow = token[i];
				}
			}
			else if (token[i] && (token[i] < nlow))
				nlow = token[i];
		}
		prev = low;
		low = nlow;
	}
	if (rc->orun != 0)	/* spit the run out */
	{
		if (rc->orun != 1)
		{
			if (!TXoutputVariableSizeLong(TXPMBUFPN, &rc->pp,
						      (ulong) 0, NULL))
				goto zerr;
		}
		if (!TXoutputVariableSizeLong(TXPMBUFPN, &rc->pp, rc->orun,
					      NULL))
			goto zerr;
		rc->orun = 0;
	}
      zerr:
	if(token)
		free(token);
	return (rc);
}

/************************************************************************/

static TTL *ormerge2 ARGS((TTL **, TTL *, BTREE *, int));

static TTL *
ormerge2(inl, nnl, bt, n)
TTL **inl;
TTL *nnl;
BTREE *bt;
int n;
{
	int i, nt, sets, f = 0, maxsel = 0, intersects;
	ulong *token, low, nlow, prev, ign = 0;
	TTL *rc = openttl();

/* Isinfinite handling is interesting here.  If intersects is 0 then
   return an infinite.  If not, look for intersects-1 in the remaining? */

	token = (ulong *)calloc(n, sizeof(ulong));
	if(!token)
		return NULL;
	if (!rc)
	{
		free(token);
		return rc;
	}
	low = (ulong)~0L;
	for (i = 0; i < n; i++)	/* reset the piles */
	{
		if (inl[i])
		{
			rewindttl(inl[i]);
			f++;
			maxsel = i + 1;
			if (!getttl(inl[i], &token[i]))
			{
				token[i] = 0;
				inl[i] = closettl(inl[i]);
				f--;
			}
			else
			{
				if (token[i] < low)
					low = token[i];
			}
		}
		else
			token[i] = 0;
	}
#if DEBUG
	DBGMSG(1, (999, NULL, "ormerge of %d - %d", intersects, maxsel));
#endif
	if (!f)
	{
		free(token);
		return closettl(rc);
	}
	prev = 0;
	nt = maxsel;
	if(nnl)
	{
		rewindttl(nnl);
		while(getttl(nnl, &ign))
		{
			if(ign >= low)
				break;
		}
	}
	while (f)
	{
		BTLOC nterms;

		nlow = (ulong)~0L;
		nterms = btsearch(bt, sizeof low, &low);
		DBGMSG(1, (999, "ormerge2", "NTerms = %d", nterms.off));
		intersects = nterms.off - 1;
#ifdef DEBUG
		DBGMSG(4, (999, NULL, "ormerge found %d of %d", nt, intersects));
#endif
		if (nt <= intersects)
			break;	/* not enough piles for intersects - all done */
		sets = 0;
		for (i = 0; i < maxsel; i++)
		{		/* see how many of same token */
			if (token[i] == low)
			{
				sets++;
				if (sets == 1 + intersects && (low != ign))
				{	/* got enough to satisfy intersects */
					if (putttl(rc, low) == 0)
						goto zerr;
				}
				if (!getttl(inl[i], &token[i]))
				{
					token[i] = 0;
					inl[i] = closettl(inl[i]);
					f--;
					nt--;
				}
				else
				{
					if (token[i] < nlow)
						nlow = token[i];
				}
			}
			else if (token[i] && (token[i] < nlow))
				nlow = token[i];
		}
		prev = low;
		low = nlow;
		while(nnl && ign < low && getttl(nnl, &ign))
		{
			if(ign >= low)
				break;
		}
	}
	if (rc->orun != 0)	/* spit the run out */
	{
		if (rc->orun != 1)
		{
			if (!TXoutputVariableSizeLong(TXPMBUFPN, &rc->pp,
						      (ulong) 0, NULL))
				goto zerr;
		}
		if (!TXoutputVariableSizeLong(TXPMBUFPN, &rc->pp, rc->orun,
					      NULL))
			goto zerr;
		rc->orun = 0;
	}
zerr:
	free(token);
	return (rc);
}

/************************************************************************/

unsigned long countttl ARGS((TTL *));

unsigned long
countttl(ttl)
TTL *ttl;
{
#ifdef DEBUG
	static char Fn[] = "countttl";
#endif
	unsigned long count, a;

	count = 0;
	rewindttl(ttl);
#ifdef NEVER
	if (ttl->stvalid)
		return ttl->count;
#endif
	while (getttl(ttl, &a))
		count++;
	rewindttl(ttl);
#ifndef NO_INFINITY
#ifdef DEBUG
	if (ttl->stvalid && (ttl->count != count))
		DBGMSG(1, (999, Fn, "Count is wrong, %d != %d", ttl->count, count));
#endif
#endif /* NO_INFINITY */
/*
   DBGMSG(2, (999, Fn, "I got %d, structure said %d", count, ttl->count));
 */
	return count;
}

/******************************************************************/
/*      Determine if ttl is infinite                              */

int TXinfthresh = -1;
int TXinfpercent = -1;

/******************************************************************/

int
TXsetinfinitythreshold(t)
int t;
{
	int r;

	r = TXinfthresh;
	TXinfthresh = t;
	return r;
}

/******************************************************************/

int
TXsetinfinitypercent(t)
int t;
{
	int r;

	r = TXinfpercent;
	TXinfpercent = t;
	return r;
}

/******************************************************************/

int
TXisinfinite(ttl)
TTL *ttl;
{
	if (TXinfthresh == -1)
		return 0;
	if (countttl(ttl) > (ulong)TXinfthresh)
	{
		DBGMSG(4, (999, NULL, "isinfinite succeeded"));
		return 1;
	}
	return 0;
}

/******************************************************************/

#define MAXREL	1000

/******************************************************************/

static int calcimport ARGS((TTL **, TTL **, TTL **, int,
			    unsigned long, short *));

static int
calcimport(tla, tln, tlo, nsets, maxtoken, import)
TTL **tla;
TTL **tln;
TTL **tlo;
int nsets;
unsigned long maxtoken;
short *import;
{
	int i;
	unsigned long counts[MAXSELS];

	for (i = 0; i < nsets; i++)
	{
		counts[i] = 0;
		import[i] = MAXREL;
		if (tla[i])
		{
			counts[i] = countttl(tla[i]);
			import[i] = MAXREL * (nsets - i) / nsets;
		}
		if (tln[i])
		{
			counts[i] = countttl(tln[i]);
			import[i] = -MAXREL * (nsets - i) / nsets;
		}
		if (tlo[i])
		{
			counts[i] = countttl(tlo[i]);
			import[i] = (short)(MAXREL * (maxtoken - counts[i]) /
				            maxtoken);
			if (import[i] < 0)
				import[i] = 0;
		}
	}
	return 0;
}

/******************************************************************/

static int calcrel ARGS((TTL **, TTL **, TTL **, int, unsigned long, int (*)(void *, long, long, short *), void *));

static int
calcrel(tla, tln, tlo, nsets, maxtoken, callback, usr)
TTL **tla;
TTL **tln;
TTL **tlo;
int nsets;
unsigned long maxtoken;
int (*callback) ARGS((void *, long, long, short *));
void *usr;
{
#ifdef DEBUG
	static char Fn[] = "calcrel";
#endif
	unsigned long counts[MAXSELS];
	unsigned long token[MAXSELS];
	short import[MAXSELS];
	int ok[MAXSELS];
	unsigned long cmin;
	long csum;
	int i;
	short maximport = 0;

/* If isinfinte() term we can just close the ttl */

	for (i = 0; i < nsets; i++)
	{
		ok[i] = 0;
		counts[i] = 0;
		import[i] = MAXREL;
		if (tla[i])
		{
			counts[i] = countttl(tla[i]);
/*
   WTF  what is the correct weighting on plus?  Do we ignore position,
   ignore frequency, or ?
 */
			import[i] = MAXREL /** (nsets - i) / nsets*/;
/*
   import[i] = MAXREL*(maxtoken-counts[i])/maxtoken;
 */
/*
   DBGMSG(3, (999, Fn, "This is AND"));
 */
		}
		if (tln[i])
		{
			counts[i] = countttl(tln[i]);
			import[i] = -MAXREL /** (nsets - i) / nsets*/;
/*
   DBGMSG(3, (999, Fn, "This is NOT"));
 */
		}
		if (tlo[i])
		{
			counts[i] = countttl(tlo[i]);
			import[i] = (short)(MAXREL
				* (maxtoken - counts[i]) / maxtoken);
			if (import[i] < 0)
				import[i] = 0;
/*
   DBGMSG(3, (999, Fn, "This is OR"));
 */
		}
		if ((counts[i] > 0) && (import[i] > maximport))
			maximport = import[i];
/*
   DBGMSG(2, (999, Fn, "Importance for set %d is %ld count = %ld of %ld", i, import[i], counts[i], maxtoken));
 */
	}
	if(TXlikermaxthresh > 0)
	{
		short thresh = MAXREL - (TXlikermaxthresh * MAXREL/100);

		if(maximport > thresh)
		{
			if(thresh > 0)
				maximport = thresh;
			else
				maximport = 0;
		}
	}
	if(TXlikermaxrows > 0)
	{
		short thresh;
		 
		if ((unsigned long)TXlikermaxrows < maxtoken)
			thresh = (short)(MAXREL *
					 (maxtoken - (unsigned long)TXlikermaxrows) / maxtoken);
		else
			thresh = 0;

		if(maximport > thresh)
		{
			if(thresh > 0)
				maximport = thresh;
			else
				maximport = 0;
		}
	}
	cmin = (ulong)~0L;
	if (nsets >= 5)
		maximport *= 2;
	else if (nsets >= 10)	/* Text2mm most likely */
		maximport *= 4;
	for (i = 0; i < nsets; i++)
	{
		if (tla[i])
			if (getttl(tla[i], &token[i]))
				ok[i] = 1;
		if (tln[i])
			if (getttl(tln[i], &token[i]))
				ok[i] = 1;
		if (tlo[i])
			if (getttl(tlo[i], &token[i]))
				ok[i] = 1;
		if (ok[i] && (token[i] < cmin))
			cmin = token[i];
	}
	while (cmin != (unsigned long)(-1))
	{
		csum = 0;
		for (i = 0; i < nsets; i++)
		{
			if (token[i] == cmin)
			{
				if (ok[i])
				{
					csum += import[i];
					if (tla[i])
					{
						if (getttl(tla[i], &token[i]))
							ok[i] = 1;
						else
							ok[i] = 0;
					}
					if (tln[i])
					{
						if (getttl(tln[i], &token[i]))
							ok[i] = 1;
						else
							ok[i] = 0;
					}
					if (tlo[i])
					{
						if (getttl(tlo[i], &token[i]))
							ok[i] = 1;
						else
							ok[i] = 0;
					}
				}
			}
		}
/*
   DBGMSG(2,(999,Fn,"Token was %d, relevance was %d", cmin, csum));
 */
/* Modify here to determine if we should look at this document */
#ifdef NEVER
		if (csum >= import[0])
#else
		if (csum >= maximport)
#endif
			if (callback(usr, cmin, csum, import) == -1)
				return 0;
		cmin = (ulong)~0L;
		for (i = 0; i < nsets; i++)
		{
			if (ok[i] && (token[i] < cmin))
				cmin = token[i];
		}
	}
	return 0;
}

/******************************************************************/

static int locstrncmp ARGS((char *, char *, size_t, size_t, int, int));

static int
locstrncmp(s1, s2, len1, len2, wild, suffixproc)
char *s1, *s2;
size_t len1, len2;
int wild, suffixproc;
{
	int rc;

	if (len2 < len1)
	{
		rc = memcmp(s1, s2, len2);
		if (rc == 0)
			return 1;
		return rc;
	}
	if (suffixproc)
		return strcmp(s1, s2);
	else if (wild)
		return memcmp(s1, s2, len1);
	else
	{
		rc = memcmp(s1, s2, len1);
		if ((rc == 0) && (len1 == len2))
			return 0;
		else
			return 1;
	}
}

/******************************************************************/
/* Note for now maxtoken is only used for Relevancy ranking */

#ifdef NEW_PHRASEPROC
static TTL *phrasetottl ARGS((MMTBL *, int, byte **, size_t *, int, int,
			      int *, int, TXCFF textsearchmode));
#endif
static TTL *wordtottl ARGS((MMTBL *, byte *, size_t, int, int, int, int,
			    TXCFF textsearchmode));
static long setmmitbl ARGS((MMTBL *, unsigned long, int (*)(void *, long, long, short *), void *, int *, short *, int));

static long
setmmitbl(mt, maxtoken, callback, usr, indg, import, op)
MMTBL *mt;
unsigned long maxtoken;
int (*callback) ARGS((void *, long, long, short *));
void *usr;
int *indg;
short *import;
int op;
{
	TTL *tla[MAXSELS];
	TTL *tlo[MAXSELS];
	TTL *tln[MAXSELS];
	TTL *tlt[MAXSELS];
	TTL ***tlt2 = NULL;
	int tlt2p[MAXSELS];
	int tlt2l[MAXSELS];
	TTL *tl1;
	int i, j;
	int intersects;
	int usemerge;
	int nsets;
	int ignset;

	*indg = 0;
	if (mt->mm == MMAPIPN ||
	    (mt->ql = mmrip(mt->mm, 0)) == MMQLPN ||
	    mt->ql->n == 0
		)
		return (-1L);

/* WTF The number 4 was picked out of thin air by JMT, with very little
   justification or experimentation.  Raising this will opt for normal
   or's, lowering it will tilt it towards merge. */
#define AVERAGE_EQUIVS_TO_MERGE	4
	if (mt->ql->n > (mt->mm->mme->nels * AVERAGE_EQUIVS_TO_MERGE))
		usemerge = 1;
	else
		usemerge = 0;
	if (usemerge)
		tlt2 = (TTL ***) calloc(MAXSELS, sizeof(TTL **));
	for (i = 0; i < MAXSELS; i++)
	{
		tla[i] = TTLPN;
		tln[i] = TTLPN;
		tlo[i] = TTLPN;
		tlt[i] = TTLPN;
		if (usemerge)
		{
			tlt2p[i] = 0;
			tlt2l[i] = -1;
			tlt2[i] = (TTL **) calloc(MAXSELS, sizeof(TTL *));
		}
	}
	if (TXinfpercent != -1)
	{
		if (maxtoken < 1000)
			TXinfthresh = (maxtoken * 100) / TXinfpercent;
		else
			TXinfthresh = (maxtoken / TXinfpercent) * 100;
	}
	intersects = mt->mm->mme->intersects;
	intersects -= mt->ql->dkpm;
	if (intersects < 0)
		intersects = 0;
	nsets = mt->ql->n;

/*
   Allow index optimiztion under the following conditions:
   - We don't have an unindexed pattern matcher (REX, NPM, XPM)
   - No within checking required.
   - One term or
   - No Delimiter.
 */

	if ((nsets == 1 ||
	     (*mt->mm->mme->sdexp == '\0' && *mt->mm->mme->edexp == '\0')) &&
	    mt->ql->dkpm == 0)
		*indg = 1;

	for (i = 0; i < mt->ql->n; i++)
	{
		byte suffixproc = mt->ql->lst[i].suffixproc;
		byte logic = mt->ql->lst[i].logic;
		byte wild = mt->ql->lst[i].wild;
		int defsuffrm = mt->mm->mme->defsuffrm;
		byte *key = (byte *) mt->ql->lst[i].s;
		size_t len = mt->ql->lst[i].len;
		int setno = mt->ql->lst[i].orpos;
		int keychanged = 0;

		if (mt->ql->lst[i].needmm || logic == LOGINOT)
			*indg = 0;
		/* WTF - handle words left with trailing "'s" */
		if (len > 2 && key[len - 2] == '\'' && key[len - 1] == 's')
		{
			len -= 2;
			keychanged = key[len];
			key[len] = '\0';
		}
#ifdef NEW_PHRASEPROC
		tl1 = phrasetottl(mt, mt->ql->lst[i].nwords, mt->ql->lst[i].words, mt->ql->lst[i].lens, suffixproc, wild, &ignset, defsuffrm, mt->cp->textsearchmode);
		if (ignset || len < 2)
#else
		if (len < 2)
#endif
		{
			/*
			   We are blowing off a set here, because it is shorter than
			   what would be indexed.  This requires a metamorph search to
			   handle, as well as reducing intersects etc.  This needs to
			   be improved to then block all other terms in same set.
			 */

			*indg = 0;
			if (intersects > 0)
				intersects -= 1;
			nsets--;
			if (nsets <= 0)
			{
				for (i = 0; i < MAXSELS; i++)
				{
					if (tla[i])
						tla[i] = closettl(tla[i]);
					if (tln[i])
						tln[i] = closettl(tln[i]);
					if (tlo[i])
						tlo[i] = closettl(tlo[i]);
					if (tlt[i])
						tlt[i] = closettl(tlt[i]);
					if (tlt2)
					{
						if (tlt2[i])
						{
							free(tlt2[i]);
							tlt2[i] = NULL;
						}
					}
				}
				if (tlt2)
					free(tlt2);
				if (keychanged)
					key[len] = keychanged;
				return -1;
			}
			if (keychanged)
				key[len] = keychanged;
			continue;
		}
#ifndef NEW_PHRASEPROC
		tl1 = wordtottl(mt, key, len, suffixproc, wild, 0,
				mt->cp->textsearchmode);
#endif
		if(!tl1)
			continue;
		if (tl1 != TTLPN)
		{
			if (!usemerge)
			{
				switch (logic)
				{
				case LOGIAND:
					if (tla[setno] != TTLPN)
					{
						tla[setno] = orttl(tla[setno], tl1);
						rewindttl(tla[setno]);
					}
					else
						tla[setno] = tl1;
					break;
				case LOGISET:
					if (tlo[setno] != TTLPN)
					{
						tlo[setno] = orttl(tlo[setno], tl1);
						rewindttl(tlo[setno]);
					}
					else
						tlo[setno] = tl1;
					break;
				case LOGINOT:
					if (tln[setno] != TTLPN)
					{
						tln[setno] = orttl(tln[setno], tl1);
						rewindttl(tln[setno]);
					}
					else
						tln[setno] = tl1;
					break;
				}
			}
			else
			{
				tlt2l[setno] = logic;
				tlt2[setno][tlt2p[setno]++] = tl1;
				if (tlt2p[setno] >= MAXSELS)
				{
					tl1 = ormerge(tlt2[setno], 0, MAXSELS);
					tlt2[setno][0] = tl1;
					tlt2p[setno] = 1;
				}
			}
		}
		if (keychanged)
			key[len] = keychanged;
	}

	if (usemerge)
	{
		for (i = 0; i < MAXSELS; i++)
		{
			switch (tlt2l[i])
			{
			case LOGIAND:
				tla[i] = ormerge(tlt2[i], 0, MAXSELS);
				break;
			case LOGISET:
				tlo[i] = ormerge(tlt2[i], 0, MAXSELS);
				break;
			case LOGINOT:
				tln[i] = ormerge(tlt2[i], 0, MAXSELS);
				break;
			default:
				break;
			}
			if (tlt2[i])
				free(tlt2[i]);
		}
		free(tlt2);
	}
	if (mt->tl != TTLPN)
		mt->tl = closettl(mt->tl);
	if (import)
	{
		calcimport(tla, tln, tlo, mt->mm->mme->nels, maxtoken, import);
	}
	if (callback)
	{
		calcrel(tla, tln, tlo, mt->mm->mme->nels, maxtoken, callback, usr);
		for (i = 0; i < MAXSELS; i++)
		{
			if (tla[i])
				tla[i] = closettl(tla[i]);
			if (tln[i])
				tln[i] = closettl(tln[i]);
			if (tlo[i])
				tlo[i] = closettl(tlo[i]);
		}
		return 1;
	}
	j = -1;
	for (i = 0; i < MAXSELS; i++)
	{
		if ((tlo[i] == TTLPN) && (j == -1))
		{
			j = i;
			continue;
		}
		if ((tlo[i] != TTLPN) && (j != -1))
		{
			tlo[j] = tlo[i];
			tlo[i] = TTLPN;
			j = -1;
			i = 0;
		}
	}
	for (i = 0; i < MAXSELS; i++)
		if (tla[i] == TTLPN)
		{
			tla[i] = ormerge(tlo, intersects, MAXSELS);
			break;
		}
	for (i = 0; i < MAXSELS; i++)
	{
		if (tln[i])
			tln[i] = closettl(tln[i]);
		if (tlo[i])
			tlo[i] = closettl(tlo[i]);
	}
	for (i = 0; i < mt->NELS; i++)
		if (tla[i] != TTLPN)
		{
			mt->tl = tla[i];
			break;
		}

	if (mt->tl == TTLPN)
	{
		mt->tl = openttl();
		rewindttl(mt->tl);
	}
	for (i++; i < mt->NELS; i++)
	{
		if (mt->tl == TTLPN)
			return (-1L);
		if (tla[i] != TTLPN)
			mt->tl = andttl(mt->tl, tla[i]);
		rewindttl(mt->tl);
	}
	/* WTF this is supposta be the number of elements in the vsl */
	if (mt->tl == TTLPN)
		return -1L;
	else
	{
		rewindttl(mt->tl);
		return (1L);
	}
#ifdef NEVER /* This code is never reached, MemLeak? */
	mt->tl = NULL;
	for (i = 0; i < MAXSELS; i++)
	{
		if (tla[i])
			tla[i] = closettl(tla[i]);
		if (tlo[i])
			tlo[i] = closettl(tlo[i]);
		if (tln[i])
			tln[i] = closettl(tln[i]);
		if (tlt[i])
			tlt[i] = closettl(tlt[i]);
	}
	return -1L;
#endif
}

/******************************************************************/

static int isnoise ARGS((char **l,char *w));

static int
isnoise(l,w)                           /* is w a noise word */
char **l, *w;
{
	for(;**l!='\0';l++)
		if(strcmpi(w,*l)==0) /* use strcmp, should already be lower */
				     /* NOT!!! Will be as use typed. */
			return(1);
	return(0);
}                                                    /* end isnoise() */

/******************************************************************/

#ifdef NEW_PHRASEPROC
static TTL *
phrasetottl(mt, nwords, key, len, suffixproc, wild, ignset, defsuffrm,
	    textsearchmode)
MMTBL *mt;
int nwords;
byte **key;
size_t *len;
int suffixproc;
int wild;
int *ignset;
int defsuffrm;
TXCFF textsearchmode;
{
	int i, nsets = 0;
	int gotword = 0;
	TTL **ttls, *rc;
	int minwordlen = mt->mm->mme->minwordlen;

	ttls = (TTL **)calloc(nwords, sizeof(TTL *));
	for(i = 0; i < nwords; i++)
	{
		if(len[i] > 1 && (mt->mm->acp->keepnoise || (!isnoise((char **)mt->mm->acp->noise, (char *)key[i]))))
		{
			size_t	llen;
			byte    *lkey;
			byte	keychanged;

			lkey = key[i];
			llen = len[i];
			keychanged = 0;
			if(strlen((char *)lkey) < llen)
				llen = strlen((char *)lkey);
			if (llen > 2 && lkey[llen - 2] == '\'' && lkey[llen - 1] == 's')
			{
				lkey[llen - 2] = '\0';
				keychanged = '\'';
				llen -= 2;
			}
			else
				gotword++;
			if(i < (nwords - 1))
				ttls[i]=wordtottl(mt, lkey, llen, 0, wild, 0, defsuffrm, textsearchmode);
			else
#ifdef NEVER
				ttls[i]=wordtottl(mt, lkey, llen, suffixproc, wild, defsuffrm, textsearchmode);
#else
				ttls[i]=wordtottl(mt, lkey, llen, suffixproc, wild, minwordlen, defsuffrm, textsearchmode);
#endif
			if(keychanged)
			{
				lkey[llen] = keychanged;
			}
			nsets++;
		}
		minwordlen -= (len[i]+1);
		if(minwordlen < 1) minwordlen = 1;
	}
	if(gotword)
		*ignset = 0;
	else
		*ignset = 1;
	if(nsets == 1)
	{
		for(i=0; i < nwords; i++)
			if(ttls[i])
			{
				rc = ttls[i];
				ttls[i] = NULL;
				free(ttls);
				return rc;
			}
	}
	rc = ormerge(ttls, nsets -1, nwords);
	for(i=0; i < nwords; i++)
		ttls[i] = closettl(ttls[i]);
	free(ttls);
	return rc;
}
#endif

/******************************************************************/

static TTL *
wordtottl(mt, key, len, suffixproc, wild, ominwordlen, defsuffrm,
	 textsearchmode)
MMTBL *mt;
byte *key;
size_t len;
int suffixproc;
int wild;
int ominwordlen;
int defsuffrm;
TXCFF textsearchmode;
{
	RECID dbfh;
	TTL *tl1, *tlt[MAXSELS];
	int j;
	byte buf[BT_MAXPGSZ];
	byte *s = buf;
	size_t stlen;
	int tpos = 0;
	int rc;
	int nsuf = mt->mm->mme->nsuf;
	int minwordlen = mt->mm->mme->minwordlen;
	char **suflist = (char **) mt->mm->mme->suffix;

	if(ominwordlen > 0 && ominwordlen < minwordlen)
		minwordlen=ominwordlen;
	dbfh = btsearch(mt->bt, len, (void *) strlwr((char *) key));

	tl1 = TTLPN;
	stlen = sizeof(buf);	/* tell it how big it can be */
	s = buf;

	DBGMSG(1, (999, NULL, "Set # = %d/%d, Orpos = %d, Key = %s",
	mt->ql->lst[i].setno, mt->ql->n, mt->ql->lst[i].orpos, key));

	dbfh = btgetnext(mt->bt, &stlen, s, NULL);
	if (!TXrecidvalid(&dbfh))
		return NULL;
	if (!strcmp((char *) s, LASTTOKEN))
		return NULL;
	if (suffixproc)
		rmsuffix((char **) &s, suflist, nsuf, minwordlen, defsuffrm,
			 0, textsearchmode);
	if (!locstrncmp((char *) key, (char *) s, len, stlen, wild, suffixproc))
	{
		tl1 = getdbfttl(mt->bdbf, TXgetoff(&dbfh));
		if (tl1 == TTLPN)
			putmsg(MWARN, NULL, "TTL GET ERROR! %s\n", key);
	}
	for (j = 0; j < MAXSELS; j++)
		tlt[j] = NULL;
	tlt[0] = tl1;
	if (tl1)
		tpos = 1;
	while ((rc = strncmp((char *) key, (char *) s, len)), rc >= 0)
	{
		stlen = sizeof(buf);
		s = buf;
		dbfh = btgetnext(mt->bt, &stlen, s, NULL);
		DBGMSG(3, (999, NULL, "Got %s", s));
		if (!TXrecidvalid(&dbfh))
			break;
		if (!strcmp((char *) s, LASTTOKEN))
			continue;
		if (suffixproc)
			rmsuffix((char **) &s, suflist, nsuf, minwordlen,
				 defsuffrm, 0, textsearchmode);
		if (locstrncmp((char *) key, (char *) s, len, stlen, wild, suffixproc))
			continue;
		tlt[tpos++] = getdbfttl(mt->bdbf, TXgetoff(&dbfh));
/* WTF - was commented out.  Why? */
		if ((tpos == 1) && (tl1 == NULL))
			tl1 = tlt[0];

		if (tlt[tpos - 1] == TTLPN)
		{
			putmsg(MWARN, NULL, "TTL GET ERROR! %s\n", key);
			tpos--;
			continue;
		}
		DBGMSG(3, (999, NULL, "Got %s, %d", key, tpos));
		if (tpos >= MAXSELS)
		{
			DBGMSG(1, (999, NULL, "Calling inner ormerge (tpos = %d)", tpos));
			tl1 = ormerge(tlt, 0, MAXSELS);
			for (j = 1; j < MAXSELS; j++)
				if (tlt[j])
					tlt[j] = closettl(tlt[j]);
			tlt[0] = tl1;
			tpos = 1;
		}
		if (tl1)
			rewindttl(tl1);
	}
	if (tpos > 1)
	{
		DBGMSG(1, (999, NULL, "Calling ormerge (tpos = %d)", tpos));
		tl1 = ormerge(tlt, 0, MAXSELS);
		for (j = 0; j < MAXSELS; j++)
			tlt[j] = closettl(tlt[j]);
	}
	return tl1;
}

/******************************************************************/
/* Based off of a block copy from the function above. */
/* Only for LIKEIN */

static long setmmitbl2 ARGS((MMTBL *, unsigned long, int (*)(void *, long, long, short *), void *, int *, short *, int, DDMMAPI *));

static long
setmmitbl2(mt, maxtoken, callback, usr, indg, import, op, ddmmapi)
MMTBL *mt;
unsigned long maxtoken;
int (*callback) ARGS((void *, long, long, short *));
void *usr;
int *indg;
short *import;
int op;
DDMMAPI *ddmmapi;
{
	TTL **tla;
	TTL **tlo;
	TTL **tln;
	TTL **tlt;
	int *tlt2p;
	int *tlt2l;
	TTL *tl1, *tl2;
	int i, j;
	int intersects;
	int nsets;
	int asize;
	int ignset;

	*indg = 0;
	if (mt->mm == MMAPIPN ||
	    (mt->ql = mmripq(ddmmapi->query)) == MMQLPN ||
	    mt->ql->n == 0
		)
		return (-1L);

/* WTF The number 4 was picked out of thin air by JMT, with very little
   justification or experimentation.  Raising this will opt for normal
   or's, lowering it will tilt it towards merge. */
#define AVERAGE_EQUIVS_TO_MERGE	4
	asize = mt->ql->n + 1;
	tla = (TTL **) calloc(asize, sizeof(TTL *));
	tln = (TTL **) calloc(asize, sizeof(TTL *));
	tlo = (TTL **) calloc(asize, sizeof(TTL *));
	tlt = (TTL **) calloc(asize, sizeof(TTL *));
	tlt2p = (int *) calloc(asize, sizeof(int));
	tlt2l = (int *) calloc(asize, sizeof(int));

	if (TXinfpercent != -1)
	{
		if (maxtoken < 1000)
			TXinfthresh = (maxtoken * 100) / TXinfpercent;
		else
			TXinfthresh = (maxtoken / TXinfpercent) * 100;
	}
	intersects = mt->mm->mme->intersects;
	intersects -= mt->ql->dkpm;
	if (intersects < 0)
		intersects = 0;
	nsets = mt->ql->n;

/*
   Allow index optimiztion under the following conditions:
   - We don't have an unindexed pattern matcher (REX, NPM, XPM)
   - No within checking required.
   - One term or
   - No Delimiter.
 */

	if ((nsets == 1 ||
	     (*mt->mm->mme->sdexp == '\0' && *mt->mm->mme->edexp == '\0')) &&
	    mt->ql->dkpm == 0)
		*indg = 1;

	for (i = 0; i < mt->ql->n; i++)
	{
		byte suffixproc = mt->ql->lst[i].suffixproc;
		byte logic = mt->ql->lst[i].logic;
		byte wild = mt->ql->lst[i].wild;
		int defsuffrm = mt->mm->mme->defsuffrm;
		byte *key = (byte *) mt->ql->lst[i].s;
		char *nkey;
		size_t len = mt->ql->lst[i].len;
		int setno = mt->ql->lst[i].orpos;
		int keychanged = 0;

		if (mt->ql->lst[i].needmm || logic == LOGINOT)
			*indg = 0;
		/* WTF - handle words left with trailing "'s" */
		if (len > 2 && key[len - 2] == '\'' && key[len - 1] == 's')
		{
			len -= 2;
			keychanged = key[len];
			key[len] = '\0';
		}
#ifdef NEW_PHRASEPROC
		tl1 = phrasetottl(mt, 1, &key, &len, suffixproc, wild, &ignset, defsuffrm, mt->cp->textsearchmode);
		if (ignset || len < 2)
#else
		if (len < 2)
#endif
		{
/*
   We are blowing off a set here, because it is shorter than
   what would be indexed.  This requires a metamorph search to
   handle, as well as reducing intersects etc.  This needs to
   be improved to then block all other terms in same set.
 */

			*indg = 0;
			if (intersects > 0)
				intersects -= 1;
			nsets--;
			if (nsets <= 0)
			{
				for (i = 0; i < asize; i++)
				{
					if (tla[i])
						tla[i] = closettl(tla[i]);
					if (tln[i])
						tln[i] = closettl(tln[i]);
					if (tlo[i])
						tlo[i] = closettl(tlo[i]);
					if (tlt[i])
						tlt[i] = closettl(tlt[i]);
				}
				return -1;
			}
			continue;
		}
#ifndef NEW_PHRASEPROC
		tl1 = wordtottl(mt, key, len, suffixproc, wild, 0,
				mt->cp->textsearchmode);
#endif
		if(!tl1)
			continue;
		nkey = malloc(len+2);
		strcpy(nkey, "-");
		strcat(nkey, (char *)key);
#ifdef NEW_PHRASEPROC
		{
			size_t len2 = len+1;
			tl2 = phrasetottl(mt, 1, (byte **)&nkey, &len2, suffixproc, wild, &ignset, defsuffrm, mt->cp->textsearchmode);
		}
#else
		tl2 = wordtottl(mt, (byte *)nkey, len+1, suffixproc, wild, 0,
				mt->cp->textsearchmode);
#endif
		free(nkey);
		if (tl1 != TTLPN)
		{
			if (tlo[setno] != TTLPN)
			{
				tlo[setno] = orttl(tlo[setno], tl1);
				rewindttl(tlo[setno]);
			}
			else
				tlo[setno] = tl1;
		}
		if(tl2 != TTLPN)
		{
			if (tln[setno] != TTLPN)
			{
				tln[setno] = orttl(tln[setno], tl2);
				rewindttl(tln[setno]);
			}
			else
				tln[setno] = tl2;
		}
		if (keychanged)
			key[len] = keychanged;
	}

	if (mt->tl != TTLPN)
		mt->tl = closettl(mt->tl);
	if (import)
	{
		calcimport(tla, tln, tlo, mt->mm->mme->nels, maxtoken, import);
	}
	if (callback)
	{
		calcrel(tla, tln, tlo, mt->mm->mme->nels, maxtoken, callback, usr);
		for (i = 0; i < asize; i++)
		{
			if (tla[i])
				tla[i] = closettl(tla[i]);
			if (tln[i])
				tln[i] = closettl(tln[i]);
			if (tlo[i])
				tlo[i] = closettl(tlo[i]);
		}
		return 1;
	}
	j = -1;
	for (i = 0; i < asize; i++)
	{
		if ((tlo[i] == TTLPN) && (j == -1))
		{
			j = i;
			continue;
		}
		if ((tlo[i] != TTLPN) && (j != -1))
		{
			tlo[j] = tlo[i];
			tlo[i] = TTLPN;
			j = -1;
			i = 0;
		}
	}
	for (i = 0; i < asize; i++)
		if (tla[i] == TTLPN)
		{
			if (op == FOP_MMIN)
				if (usr)
				{
					tl1 = ormerge(tln, 0, asize);
					tla[i] = ormerge2(tlo, tl1, usr, asize);
				}
				else
				{
					*indg = 0;
					tla[i] = ormerge(tlo, 0, asize);
				}
			else
				tla[i] = ormerge(tlo, intersects, asize);
			break;
		}
	for (i = 0; i < asize; i++)
	{
		if (tln[i])
			tln[i] = closettl(tln[i]);
		if (tlo[i])
			tlo[i] = closettl(tlo[i]);
	}
	for (i = 0; i < mt->NELS; i++)
		if (tla[i] != TTLPN)
		{
			mt->tl = tla[i];
			break;
		}

	if (mt->tl == TTLPN)
	{
		mt->tl = openttl();
		rewindttl(mt->tl);
	}
	for (i++; i < mt->NELS; i++)
	{
		if (mt->tl == TTLPN)
			return (-1L);
		if (tla[i] != TTLPN)
			mt->tl = andttl(mt->tl, tla[i]);
		rewindttl(mt->tl);
	}
	/* WTF this is supposta be the number of elements in the vsl */
	if (mt->tl == TTLPN)
		return -1L;
	else
	{
		rewindttl(mt->tl);
		return (1L);
	}
#ifdef NEVER /* Code not reached, MemLeak? */
	mt->tl = NULL;
	for (i = 0; i < asize; i++)
	{
		if (tla[i])
			tla[i] = closettl(tla[i]);
		if (tlo[i])
			tlo[i] = closettl(tlo[i]);
		if (tln[i])
			tln[i] = closettl(tln[i]);
		if (tlt[i])
			tlt[i] = closettl(tlt[i]);
	}
	return -1L;
#endif
}

/******************************************************************/

#ifdef TEST
long
setmmtbl(mt, q, maxtoken)
MMTBL *mt;
char *q;
unsigned long maxtoken;
{
	int rc;

	mt->query = q;
	if (mt->mm == MMAPIPN)
		mt->mm = openmmapi(q, mt->cp);
	else
		mt->mm = setmmapi(mt->mm, q);
	return setmmitbl(mt, maxtoken, NULL, NULL, &rc, NULL, 0);
}
#endif /* TEST */

/******************************************************************/

long
TXsetmmatbl(mt, ddmmapi, maxtoken, callback, usr, rc, import, op)
MMTBL *mt;
DDMMAPI *ddmmapi;
unsigned long maxtoken;
int (*callback) ARGS((void *, long, long, short *));
void *usr;
int *rc;
short *import;
int op;
{
	if (mt->mm != MMAPIPN)
		closemmapi(mt->mm);
	mt->mm = ddmmapi->mmapi;
	if (op == FOP_MMIN)
		return setmmitbl2(mt, maxtoken, callback, usr, rc, import, op, ddmmapi);
	else
		return setmmitbl(mt, maxtoken, callback, usr, rc, import, op);
}

/************************************************************************/
#ifdef TEST
main(argc, argv)
int argc;
char *argv[];
{
	int i, j;
	char ln[80];
	MMTBL *mt = openmmtbl("tmptin");

	if (mt != MMTBLPN)
	{
		while (gets(ln) != NULL)
		{
			if (setmmtbl(mt, ln) > 0L)
			{
				ulong v;

				while (getttl(mt->tl, &v))
					printf("%lu ", v);
				putchar('\n');
			}
			else
				puts("not found");
		}
	}
}
#endif
/************************************************************************/
