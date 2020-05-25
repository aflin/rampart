/*
 * $Log$
 * Revision 1.4  2001/12/28 22:24:01  john
 * Use config.h
 *
 * Revision 1.3  2000-11-02 15:20:54-05  john
 * Handle ORDER BY and UNION.
 *
 * Revision 1.2  2000-01-28 16:55:45-05  john
 * Order by number gets bumped to top.
 *
 */

#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#ifdef USE_EPI
#include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"

/******************************************************************/

typedef struct TXREORG {
	QNODE *ho; /* An order by */
	QNODE *hg; /* A group by */
	QNODE *hh; /* Having node */
	QNODE *hd; /* Distinct node */
	QNODE *hp; /* Project node */
	QNODE *hu; /* Union node */
	QNODE *hon;/* Order by number */
} TXREORG;

static int needreorg ARGS((QNODE *, TXREORG *));

static int
needreorg(qnode, tx)
QNODE *qnode;
TXREORG *tx;
{
	switch(qnode->op)
	{
		case TABLE_OP:
			break;
		case GROUP_BY_OP:
			tx->hg = qnode;
			if(qnode->left)
				needreorg(qnode->left, tx);
			if(qnode->right)
				needreorg(qnode->right, tx);
			break;
		case ORDER_OP:
			tx->ho = qnode;
			if(qnode->left)
				needreorg(qnode->left, tx);
			if(qnode->right)
				needreorg(qnode->right, tx);
			break;
		case ORDERNUM_OP:
			tx->hon= qnode;
			if(qnode->left)
				needreorg(qnode->left, tx);
			if(qnode->right)
				needreorg(qnode->right, tx);
			break;
		case HAVING_OP:
			tx->hh = qnode;
			if(qnode->left)
				needreorg(qnode->left, tx);
			if(qnode->right)
				needreorg(qnode->right, tx);
			break;
		case DISTINCT_OP:
			tx->hd = qnode;
			if(qnode->left)
				needreorg(qnode->left, tx);
			if(qnode->right)
				needreorg(qnode->right, tx);
			break;
		case UNION_OP:
			tx->hu = qnode;
			if(qnode->left)
				needreorg(qnode->left, tx);
			if(qnode->right)
				needreorg(qnode->right, tx);
			break;
		case PROJECT_OP:
			tx->hp = qnode;
			if(qnode->left)
				needreorg(qnode->left, tx);
			if(qnode->right)
				needreorg(qnode->right, tx);
			break;
		case PARAM_OP:
			break;
		case SELECT_OP:
			if(qnode->left)
				needreorg(qnode->left, tx);
			if(qnode->right)
				needreorg(qnode->right, tx);
			break;
		case SUBQUERY_OP:
			if(qnode->left)
				qnode->left = TXreorgqnode(qnode->left);
			break;
		default:
			if(qnode->left)
				needreorg(qnode->left, tx);
			if(qnode->right)
				needreorg(qnode->right, tx);
			break;
	}

	if(tx->hg && (tx->ho || tx->hh))
	{
		/* A group by with having or order by */
		return 1;
	}

	if(tx->hd && tx->ho)
	{
		/* Distinct with order by */
		return 1;
	}
	if(tx->hu && tx->ho)
	{
		/* Union with order by */
		return 1;
	}
	if(tx->hon) /* Order by # */
		return 1;

	return 0;
}

/******************************************************************/

static int checkgrp ARGS((TXREORG *tx));

static int
checkgrp(tx)
TXREORG *tx;
{
#ifndef NEVER
	return 0;
#else
	if(!tx.hg)
		return 0;
	if(!tx.hp)
		return -1; /* For now */
	printf("Group by: ");
	for(i=0; i < tx.hg->proj->n; i++)
	{
		ts = disppred(tx.hg->proj->preds[i], 0, 0);
		printf(ts);
		free(ts);
	}
	printf("\nSelect: ");
	for(i=0; i < tx.hp->proj->n; i++)
	{
		ts = disppred(tx.hp->proj->preds[i], 0, 0);
		printf(ts);
		free(ts);
	}
	printf("\n");
#endif
}

/******************************************************************/

QNODE *
TXreorgqnode(qnode)
QNODE *qnode;
{
	TXREORG	tx;
	QNODE *hp, *ht;

	tx.ho = NULL;
	tx.hg = NULL;
	tx.hh = NULL;
	tx.hd = NULL;
	tx.hu = NULL;
	tx.hp = NULL;
	tx.hon= NULL;

	if(!qnode)
		return qnode;
	if(qnode->op == TABLE_AS_OP)
	{
		/* Leave TABLE AS alone at top of tree. */
		qnode->left = TXreorgqnode(qnode->left);
		return qnode;
	}
	if(needreorg(qnode, &tx))
	{
		checkgrp(&tx);
		hp = qnode;
		if(tx.hh)
		{
			ht = tx.hh->left;
			tx.hh->left = hp;
			tx.hh->op = SELECT_OP;
			if(hp->op == DISTINCT_OP || hp->op == UNION_OP)
			{
				if(hp->right->op == PROJECT_OP)
				{
					hp->right->left = ht;
				}
				else
				{

					hp->right = ht;
				}
			}
			else
				hp->left = ht;
			if(tx.ho)
				hp = tx.ho;
			else
				hp = tx.hh;
		}
		else if (tx.ho)/* Just get the order to the top */
		{
			ht = tx.ho->right;
			tx.ho->right = hp;
			if(hp->op == DISTINCT_OP || hp->op == UNION_OP)
			{
				if(hp->right->op == PROJECT_OP)
				{
					hp->right->left = ht;
				}
				else
				{

					hp->right = ht;
				}
			}
			else
				hp->left = ht;
			hp = tx.ho;
		}
		else if(tx.hon && tx.hon != hp)
		{
			ht = tx.hon->right;
			tx.hon->right = hp;
			if(hp->op == DISTINCT_OP || hp->op == UNION_OP)
			{
				if(hp->right->op == PROJECT_OP)
				{
					hp->right->left = ht;
				}
				else
				{

					hp->right = ht;
				}
			}
			else
				hp->left = ht;
			hp = tx.hon;
		}
		return hp;
	}
	else
	{
		checkgrp(&tx);
		return qnode;
	}
}

/******************************************************************/

