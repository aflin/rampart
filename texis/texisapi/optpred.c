/*
 * $Log$
 * Revision 1.17  2003/04/10 22:06:42  john
 * Since we throw out alt, don't cache stuff.
 *
 * Revision 1.16  2001-12-28 17:19:57-05  john
 * Use config.h
 *
 */

#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifdef USE_EPI
#include "os.h"
#endif
#include "dbquery.h"

/******************************************************************/
/*	Duplicate a predicate.
 */

PRED *
duppred(p)
PRED *p;
{
	PRED *n;

	if(!p)
		return p;
	n = (PRED *)calloc(1, sizeof(PRED));
	*n = *p;
#ifndef HAVE_DUPPRED2
	n->refc = 0;
#endif
	if(p->edisplay)
		n->edisplay = strdup(p->edisplay);
	if(p->idisplay)
		n->idisplay = strdup(p->idisplay);
	switch (n->lt)
	{
		case 'P' :
			n->left = duppred(p->left);
			break;
		case NAME_OP :
			n->left = strdup(p->left);
			break;
		case FIELD_OP :
			n->left = dupfld(p->left);
			break;
		default: /* Anything to dup? */
			break;
	}
	switch (n->rt)
	{
		case 'P' :
			n->right = duppred(p->right);
			break;
		case NAME_OP :
			n->right = strdup(p->right);
			break;
		case FIELD_OP :
			switch (n->op)
			{
				case FLDMATH_MM:
				case FLDMATH_NMM:
				case FLDMATH_RELEV:
				case FLDMATH_PROXIM:
#ifdef NEVER
					n->right = p->right;
					break;
#endif
				default:
					n->right = dupfld(p->right);
					break;
			}
			break;
		default: /* Anything to dup? */
			break;
	}
	n->lat = 0;
	n->altleft = NULL;
	n->rat = 0;
	n->altright = NULL;
	n->dff = 0;
	n->resultfld = 0;
	n->fldmathfunc = NULL;
	return n;
}

/******************************************************************/
/*	This converts any predicate to become a set of anded or's
 */

PRED *
optpred(p, c)
PRED *p;
int *c;
{
	PRED *t1, *t2;
	PRED *left, *right;
	PRED *left2, *right2;

	if(!p)
		return p;
	if (p->op == FLDMATH_AND)
	{
		left = (PRED *)p->left;
		right = (PRED *)p->right;
		if (left->op != FLDMATH_OR)
		{
			if (right->op == FLDMATH_AND)
			{
/* Convert a & (b & c) to (a & b) & c */
				t1 = right;
				t2 = t1->left;
				right->left = p;
				p->right = t2;
				*c = 1;
				return t1;
			}
			if (p->rt == 'P' && !*c)
				p->right = optpred(p->right, c);
			if (p->lt == 'P' && !*c)
				p->left = optpred(p->left, c);
			return p;
		}
		if (left->op == FLDMATH_OR)
		{
			if (right->op == FLDMATH_AND)
			{
 /* Convert (a | b) & (b & c) to (b & c) & (a | b) */
				t1 = right;
				p->right = p->left;
				p->left = t1;
				*c = 1;
				return p;
			}
			if (p->rt == 'P' && !*c)
				p->right = optpred(p->right, c);
			if (p->lt == 'P' && !*c)
				p->left = optpred(p->left, c);
			return p;
		}
	}
	if (p->op == FLDMATH_OR)
	{
		left = (PRED *)p->left;
		right = (PRED *)p->right;
		if (left->op == FLDMATH_AND)
		{
			if (right->op != FLDMATH_AND)
			{
/* (a & b) | c => (a | c) & (b | c) */
				t2 = (PRED *)calloc(1, sizeof(PRED));
				t2->op = FLDMATH_AND;
				t2->lt = t2->rt = 'P';
				t2->right = (PRED *)calloc(1, sizeof(PRED));
				right2 = (PRED *)t2->right;
				right2->op = FLDMATH_OR;
				right2->lt = left->rt;
				right2->left = left->right;
				right2->rt = p->rt;
				right2->right = duppred(right);
				t2->left = (PRED *)calloc(1, sizeof(PRED));
				left2 = (PRED *)t2->left;
				left2->op = FLDMATH_OR;
				left2->lt = left->lt;
				left2->left = left->left;
				left2->rt = p->rt;
#if 0
				left2->right = duppred(right);
#else
				left2->right = right;
#endif
				free(left);
				*c = 1;
				return t2;
			}
			else
			{
/* (a & b) | (c & d) => (((a | c) & (a | d)) & (b | c)) & (b | d) */
				t2 = (PRED *)calloc(1, sizeof(PRED));
				t2->op = FLDMATH_AND;
				t2->lt = t2->rt = 'P';

				t2->right = (PRED *)calloc(1, sizeof(PRED));
				right2 = (PRED *)t2->right;
				right2->op = FLDMATH_OR;
				right2->lt = left->rt;
				right2->left = duppred(left->right);
				right2->rt = right->rt;
				right2->right = duppred(right->right);
/* (b | d) */
				t2->left = (PRED *)calloc(1, sizeof(PRED));
				left2 = (PRED *)t2->left;
				left2->op = FLDMATH_AND;
				left2->lt = left2->rt = 'P';
				left2->right = (PRED *)calloc(1, sizeof(PRED));
				right2 = (PRED *)left2->right;
				right2->op = FLDMATH_OR;
				right2->lt = left->rt;
#if 0
				right2->left = duppred(left->right);
#else
				right2->left = left->right;
#endif
				right2->rt = right->lt;
				right2->right = duppred(right->left);
/* (b | c) */
				left2->left = (PRED *)calloc(1, sizeof(PRED));
				left2 = (PRED *)left2->left;
				left2->op = FLDMATH_AND;
				left2->lt = left2->rt = 'P';
				left2->right = (PRED *)calloc(1, sizeof(PRED));
				right2 = (PRED *)left2->right;
				right2->op = FLDMATH_OR;
				right2->lt = left->lt;
				right2->left = duppred(left->left);
				right2->rt = right->rt;
#if 0
				right2->right = duppred(right->right);
#else
				right2->right = right->right;
#endif
/* (a | d) */
				left2->left = (PRED *)calloc(1, sizeof(PRED));
				left2 = (PRED *)left2->left;
				left2->op = FLDMATH_OR;
				left2->lt = left->lt;
#if 0
				left2->left = duppred(left->left);
#else
				left2->left = left->left;
#endif
				left2->rt = right->lt;
#if 0
				left2->right = duppred(right->left);
#else
				left2->right = right->left;
#endif
/* (a | c) */
				free(left);
				free(right);
				*c = 1;
				return t2;
			}
		}
		if (right->op == FLDMATH_AND)
		{
			if (left->op != FLDMATH_AND)
			{
/* a | (b & c) => (a | b) & (a | c) */
				t2 = (PRED *)calloc(1, sizeof(PRED));
				t2->op = FLDMATH_AND;
				t2->lt = t2->rt = 'P';
				t2->right = (PRED *)calloc(1, sizeof(PRED));
				right2 = (PRED *)t2->right;
				right2->op = FLDMATH_OR;
				right2->lt = right->rt;
				right2->left = right->right;
				right2->rt = p->lt;
				right2->right = duppred(left);
				t2->left = (PRED *)calloc(1, sizeof(PRED));
				left2 = (PRED *)t2->left;
				left2->op = FLDMATH_OR;
				left2->lt = right->lt;
				left2->left = right->left;
				left2->rt = p->lt;
#if 0
				left2->right = duppred(left);
#else
				left2->right = left;
#endif
				free(right);
				*c = 1;
				return t2;
			}
			if (p->rt == 'P' && !*c)
				p->right = optpred(p->right, c);
			if (p->lt == 'P' && !*c)
				p->left = optpred(p->left, c);
			return p;
		}
	}
	if (p->rt == 'P' && !*c)
		p->right = optpred(p->right, c);
	if (p->lt == 'P' && !*c)
		p->left = optpred(p->left, c);
	return p;
}

/******************************************************************/
/*	This converts any predicate to become a set of ored and's
 */

PRED *
optpred2(p, c)
PRED *p;
int *c;
{
	PRED *t1, *t2;
	PRED *left, *right;
	PRED *left2, *right2;

	if (p->op == FLDMATH_OR)
	{
		left = (PRED *)p->left;
		right = (PRED *)p->right;
		if (left->op != FLDMATH_AND)
		{
			if (right->op == FLDMATH_OR)
			{
/* Convert a | (b | c) to (a | b) | c */
				t1 = right;
				t2 = t1->left;
				right->left = p;
				p->right = t2;
				*c = 1;
				return t1;
			}
			if (p->rt == 'P' && !*c)
				p->right = optpred(p->right, c);
			if (p->lt == 'P' && !*c)
				p->left = optpred(p->left, c);
			return p;
		}
		if (left->op == FLDMATH_AND)
		{
			if (right->op == FLDMATH_OR)
			{
 /* Convert (a & b) | (b | c) to (b | c) | (a & b) */
				t1 = right;
				p->right = p->left;
				p->left = t1;
				*c = 1;
				return p;
			}
			if (p->rt == 'P' && !*c)
				p->right = optpred(p->right, c);
			if (p->lt == 'P' && !*c)
				p->left = optpred(p->left, c);
			return p;
		}
	}
	if (p->op == FLDMATH_AND)
	{
		left = (PRED *)p->left;
		right = (PRED *)p->right;
		if (left->op == FLDMATH_OR)
		{
			if (right->op != FLDMATH_OR)
			{
/* (a | b) & c => (a & c) | (b & c) */
				t2 = (PRED *)calloc(1, sizeof(PRED));
				t2->op = FLDMATH_OR;
				t2->lt = t2->rt = 'P';
				t2->right = (PRED *)calloc(1, sizeof(PRED));
				right2 = (PRED *)t2->right;
				right2->op = FLDMATH_AND;
				right2->lt = left->rt;
				right2->left = left->right;
				right2->rt = p->rt;
				right2->right = duppred(right);
				t2->left = (PRED *)calloc(1, sizeof(PRED));
				left2 = (PRED *)t2->left;
				left2->op = FLDMATH_AND;
				left2->lt = left->lt;
				left2->left = left->left;
				left2->rt = p->rt;
#if 0
				left2->right = duppred(right);
#else
				left2->right = right;
#endif
				free(left);
				*c = 1;
				return t2;
			}
			else
			{
/* (a | b) & (c | d) => (((a & c) | (a & d)) | (b & c)) | (b & d) */
				t2 = (PRED *)calloc(1, sizeof(PRED));
				t2->op = FLDMATH_OR;
				t2->lt = t2->rt = 'P';

				t2->right = (PRED *)calloc(1, sizeof(PRED));
				right2 = (PRED *)t2->right;
				right2->op = FLDMATH_AND;
				right2->lt = left->rt;
				right2->left = duppred(left->right);
				right2->rt = right->rt;
				right2->right = duppred(right->right);
/* (b & d) */
				t2->left = (PRED *)calloc(1, sizeof(PRED));
				left2 = (PRED *)t2->left;
				left2->op = FLDMATH_OR;
				left2->lt = left2->rt = 'P';
				left2->right = (PRED *)calloc(1, sizeof(PRED));
				right2 = (PRED *)left2->right;
				right2->op = FLDMATH_AND;
				right2->lt = left->rt;
#if 0
				right2->left = duppred(left->right);
#else
				right2->left = left->right;
#endif
				right2->rt = right->lt;
				right2->right = duppred(right->left);
/* (b & c) */
				left2->left = (PRED *)calloc(1, sizeof(PRED));
				left2 = (PRED *)left2->left;
				left2->op = FLDMATH_OR;
				left2->lt = left2->rt = 'P';
				left2->right = (PRED *)calloc(1, sizeof(PRED));
				right2 = (PRED *)left2->right;
				right2->op = FLDMATH_AND;
				right2->lt = left->lt;
				right2->left = duppred(left->left);
				right2->rt = right->rt;
#if 0
				right2->right = duppred(right->right);
#else
				right2->right = right->right;
#endif
/* (a & d) */
				left2->left = (PRED *)calloc(1, sizeof(PRED));
				left2 = (PRED *)left2->left;
				left2->op = FLDMATH_AND;
				left2->lt = left->lt;
#if 0
				left2->left = duppred(left->left);
#else
				left2->left = left->left;
#endif
				left2->rt = right->lt;
#if 0
				left2->right = duppred(right->left);
#else
				left2->right = right->left;
#endif
/* (a & c) */
				free(left);
				free(right);
				*c = 1;
				return t2;
			}
		}
		if (right->op == FLDMATH_OR)
		{
			if (left->op != FLDMATH_OR)
			{
/* a & (b | c) => (a & b) | (a & c) */
				t2 = (PRED *)calloc(1, sizeof(PRED));
				t2->op = FLDMATH_OR;
				t2->lt = t2->rt = 'P';
				t2->right = (PRED *)calloc(1, sizeof(PRED));
				right2 = (PRED *)t2->right;
				right2->op = FLDMATH_AND;
				right2->lt = right->rt;
				right2->left = right->right;
				right2->rt = p->lt;
				right2->right = duppred(left);
				t2->left = (PRED *)calloc(1, sizeof(PRED));
				left2 = (PRED *)t2->left;
				left2->op = FLDMATH_AND;
				left2->lt = right->lt;
				left2->left = right->left;
				left2->rt = p->lt;
#if 0
				left2->right = duppred(left);
#else
				left2->right = left;
#endif
				free(right);
				*c = 1;
				return t2;
			}
			if (p->rt == 'P' && !*c)
				p->right = optpred(p->right, c);
			if (p->lt == 'P' && !*c)
				p->left = optpred(p->left, c);
			return p;
		}
	}
	if (p->rt == 'P' && !*c)
		p->right = optpred(p->right, c);
	if (p->lt == 'P' && !*c)
		p->left = optpred(p->left, c);
	return p;
}

/******************************************************************/

int
TXpredrtdist(PRED *pred)
{
	PRED *rtpred;

	if(pred && pred->rt == 'P')
	{
		rtpred = pred->right;
		if(rtpred && rtpred->is_distinct)
			return 1;
	}
	return 0;
}
