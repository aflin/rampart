/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"				/* for htpf() */

/******************************************************************/

#ifndef EPI_HAVE_STDARG
wtf;
#endif

static void CDECL
prIndented(HTBUF *buf, int depth, CONST char *fmt, ...)
/* Substitute for printf() that is Vortex-safe, and indents `depth'.
 */
{
	va_list	argp;
	int	len, i;
	char	tmp[1024];

	va_start(argp, fmt);

	/* Use TxMeterOut(), as it is safe for Vortex: */
	for (i = 0; i < depth; i++)
		if (buf)
			htbuf_write(buf, "  ", 2);
		else
			TxMeterOut(NULL, "  ", 2);
	if (buf)
		htbuf_vpf(buf, fmt, strlen(fmt), (HTPFF)0, argp, HTPFARGPN,
			  SIZE_TPN);
	else
	{
		len = htvsnpf(tmp, sizeof(tmp), fmt, (HTPFF)0, TXFMTCPPN,
			      TXFMTSTATEPN, argp, HTPFARGPN, SIZE_TPN,
			      TXPMBUFPN);
		if (len >= (int)sizeof(tmp))
		{
			TxMeterOut(NULL, tmp, sizeof(tmp) - 1);
			TxMeterOut(NULL, "...", 3);
		}
		else
			TxMeterOut(NULL, tmp, len);
	}
}

static const char *
TXprojTypeStr(PROJ_TYPE type)
{
	switch (type)
	{
	case PROJ_AGG_END:	return("aggregateEnd");
	case PROJ_AGG_DONE:	return("aggregateDone");
	case PROJ_UNSET:	return("unset");
	case PROJ_SINGLE:	return("single");
	case PROJ_AGG:		return("aggregate");
	case PROJ_AGG_CALCED:	return("aggregateCalculated");
	case PROJ_SINGLE_END:	return("singleEnd");
	case PROJ_CLEANUP:	return("cleanup");
	}
	return("unknown");
}

char *
TXqnodeOpToStr(op, buf, bufSz)
QNODE_OP	op;	/* (in) op to map to string */
char		*buf;	/* (out, opt.) scratch buffer if `op' unknown */
size_t		bufSz;	/* (in) "" size */
{
	switch(op)
	{
	case PROJECT_OP:	return("project");
	case SELECT_OP:		return("SELECT");
	case RENAME_OP:		return("rename");
	case FOP_ADD:		return("ADD");
	case FOP_SUB:		return("SUBTRACT");
	case FOP_MUL:		return("MULTIPLY");
	case FOP_DIV:		return("DIVIDE");
	case FOP_MOD:		return("MOD");
	case FOP_CNV:		return("CONVERT");
	case FOP_ASN:		return("assign");
	case FOP_EQ:		return("equals");
	case FOP_LT:		return("lessThan");
	case FOP_LTE:		return("lessThanOrEqualTo");
	case FOP_GT:		return("greaterThan");
	case FOP_GTE:		return("greaterThanOrEqualTo");
	case FOP_AND:		return("AND");
	case FOP_OR:		return("OR");
	case FOP_NEQ:		return("notEqualTo");
	case FOP_MM:		return("LIKE");
	case FOP_NMM:		return("LIKE3");
	case FOP_MAT:		return("MATCHES");
	case FOP_RELEV:		return("LIKER");
	case FOP_PROXIM:	return("LIKEP");
	case PRED_OP:		return("predicate");
	case FOP_IN:		return("IN");
	case FOP_IS_SUBSET:	return("SUBSET");
	case FOP_INTERSECT:			return("INTERSECT");
	case FOP_INTERSECT_IS_EMPTY:		return("intersectIsEmpty");
	case FOP_INTERSECT_IS_NOT_EMPTY:	return("intersectIsNotEmpty");
	case FOP_COM:		return("compare");
	case FOP_MMIN:		return("LIKEIN");
	case FOP_TWIXT:		return("BETWEEN");
	case LPAREN:		return("leftParen");
	case RPAREN:		return("rightParen");
	case DROP_OP:		return("DROP");
	case LIST_OP:		return("list");
	case EQUAL_OP:		return("equalOp");
	case PRODUCT_OP:	return("JOIN");
	case COLUMN_OP:		return("columnOp");
	case DEL_SEL_OP:	return("delSelOp");
	case GRANT_OP:		return("GRANT");
	case HAVING_OP:		return("HAVING");
	case TX_QNODE_NUMBER:	return("numberOp");
	case NNUMBER :		return("nNumberOp");
	case INSERT_OP:		return("INSERTINTO");
	case CREATE_OP:		return("CREATE");
	case UNION_OP:		return("UNION");
	case STRING_OP:		return("stringOp");
	case TABLE_OP:		return("tableOp");
	case UPD_SEL_OP:	return("updSelOp");
	case VALUE_OP:		return("VALUES");
	case VIEW_OP :		return("viewOp");
	case NOT_OP  :		return("NOT");
	case TABLE_AS_OP:	return("TABLEAS");
	case COUNTER_OP:	return("COUNTER");
	case DEL_ALL_OP:	return("delAllOp");
	case REVOKE_OP:		return("REVOKE");
	case INDEX_OP:		return("INDEX");
	case PROP_OP :		return("SET");
	case FLOAT_OP:		return("floatOp");
	case DISTINCT_OP:	return("DISTINCT");
	case TRIGGER_OP:	return("trigger");
	case UPD_ALL_OP:	return("updAllOp");
	case CONVERT_OP:	return("convertOp");
	case EXISTS_OP:		return("existsOp");
	case SUBQUERY_OP:	return("subQueryOp");
	case NAMENUM_OP:	return("nameNumOp");
	case NO_OP:		return("noOp");
	case PARAM_OP:		return("parameter");
	case NAME_OP:		return("name");
	case ORDER_OP:		return("ORDERBY");
	case ORDERNUM_OP:	return("ORDERBYnum");
	case GROUP_BY_OP:	return("GROUPBY");
	case DEMUX_OP:		return("demux");
	case ALTER_OP:		return("alter");
	case ALL_OP:		return("ALL");
	case AGG_FUN_OP:	return("aggregateFunction");
	case REG_FUN_OP:	return("function");
	case FIELD_OP:		return("field");
	case NULL_OP:		return("NULL");
	case HINT_OP:		return("withHint");
#ifdef TX_USE_ORDERING_SPEC_NODE
	case ORDERING_SPEC_OP:	return("orderingSpec");
#endif /* TX_USE_ORDERING_SPEC_NODE */
	case ARRAY_OP:		return("arrayOp[");
	case QNODE_OP_UNKNOWN:	return("QNODE_OP_UNKNOWN");
	}
	/* This could be the `default:' for the above switch(), but
	 * keep it separate so we get a `not handled in switch'
	 * compile warning above when new QNODE_OP values added:
	 */
	if (buf != CHARPN)
	{
		htsnpf(buf, bufSz, "OP_0x%x", (int)op);
		return(buf);
	}
	return("UnknownOp");
}

int
TXdumpPred(buf, pred, depth)
HTBUF	*buf;	/* (out, opt.) buffer to dump to instead of stdout */
PRED    *pred;	/* (in) predicate to dump */
int     depth;	/* (in) current depth (call with 0 initially) */
{
	CONST char	*s, *type = CHARPN, *side, *name;
	DDMMAPI		*ddmmapi;
	size_t		sz;
	FLD		*fld;
	char		scratchBuf[EPI_OS_INT_BITS/4 + 10], tmp[128];
	char		predPtrBuf[EPI_OS_VOIDPTR_BITS/4 + 20];
	char		*orderFlagsStr = NULL;

#ifdef TX_USE_ORDERING_SPEC_NODE
	if (pred)
	{
		/* Only top PRED node of PRED tree uses `orderFlags',
		 * but print for all just in case (wtf with verbose
		 * iff top PRED, to show `asc'; but `depth' is overall
		 * QNODE + PRED indent depth, not just PRED depth):
		 */
		orderFlagsStr = TXorderFlagsToStr(pred->orderFlags,
				  (pred->op == ORDERING_SPEC_OP) /* never */);
		if (orderFlagsStr && !*orderFlagsStr)
			orderFlagsStr = TXfree(orderFlagsStr);
	}
	else
#endif /* TX_USE_ORDERING_SPEC_NODE */
		orderFlagsStr = NULL;

	if (!pred) goto ok;

	if (pred->fldmathfunc)
		htsnpf(tmp, sizeof(tmp), " fldmathfunc=\"%s\"",
		       TXfldFuncName(pred->fldmathfunc));
	else
		*tmp = '\0';
	/* Print PRED pointer if verbose, for matchup with tup_match() msg: */
	if (TXverbosity > 2)
		htsnpf(predPtrBuf, sizeof(predPtrBuf), " ptr=\"%p\"",
		       pred);
	else
		*predPtrBuf = '\0';
	prIndented(buf, depth, "<pred op=\"%s\"%s%s%s%s%s%H%s%s>\n",
		   TXqnodeOpToStr(pred->op, scratchBuf, sizeof(scratchBuf)),
		   (pred->is_distinct ? " DISTINCT" : ""),
		   (pred->handled ? " handled" : ""),
		   (pred->assumetrue ? " assumetrue" : ""), tmp,
		   (orderFlagsStr ? " orderFlags=\"" : ""),
		   (orderFlagsStr ? orderFlagsStr : ""),
		   (orderFlagsStr ? "\"" : ""),
		   predPtrBuf);
	orderFlagsStr = TXfree(orderFlagsStr);

	switch (pred->lt)
	{
	case PRED_OP:
		TXdumpPred(buf, (PRED *)pred->left, depth + 1);
		break;
	case FIELD_OP:
		/* NOTE: see also pred_eval(): */
		if (pred->lat == FIELD_OP && pred->altleft)
		{
		altLeftFld:
			side = "altleft";
			fld = (FLD *)pred->altleft;
		}
		else
		{
			side = "left";
			fld = (FLD *)pred->left;
		}
		{
			const char	*nullVal = "";
			if (fld && TXfldIsNull(fld))
				nullVal = " sqlNull=\"sqlNull\"";
			prIndented(buf, depth + 1,
			   "<field side=\"%H\" type=\"%H\"%s%s>%H</field>\n",
			   side, (fld ? ddfttypename(fld->type) : "?"),
			   (fld ? "" : " nullFldPtr=\"nullFldPtr\""), nullVal,
				   (fld ? fldtostr(fld) : ""));
		}
		break;
	case NAME_OP:
		/* NOTE: see also pred_eval(): */
		name = (char *)pred->left;
		prIndented(buf, depth + 1, "<name>%H</name>\n",
			   (strcmp(name, "$star") == 0 ? "*" : name));
		if (pred->lat == FIELD_OP && pred->altleft)
			goto altLeftFld;
		break;
	default:
		prIndented(buf, depth + 1, "<unknown op=\"%H\"/>\n",
			   TXqnodeOpToStr(pred->lt, scratchBuf,
					  sizeof(scratchBuf)));
		break;
	}

	if (pred->idisplay)
		prIndented(buf, depth + 1, "<idisplay>%H</idisplay>\n",
			   pred->idisplay);
	if (pred->edisplay)
		prIndented(buf, depth + 1, "<edisplay>%H</edisplay>\n",
			   pred->edisplay);

	switch (pred->rt)
	{
	case 'P':
		TXdumpPred(buf, (PRED *)pred->right, depth + 1);
		break;
	case FIELD_OP:
		/* NOTE: see also pred_eval(): */
		if (pred->rat == FIELD_OP && pred->altright)
		{
		altRightFld:
			side = "altright";
			fld = (FLD *)pred->altright;
		}
		else
		{
			side = "right";
			fld = (FLD *)pred->right;
		}
		if (TXismmop(pred->op, NULL))
		{
			if (fld)
			{
				ddmmapi = getfld(fld, &sz);
				if (!ddmmapi)
					s = "NULL";
				else if (sz != sizeof(DDMMAPI))
					s = "invalidSizeDdmmapi";
				else if (ddmmapi->qtype == 'N')
					s = ddmmapi->qdata;
				else
					s = ddmmapi->query;
			}
			else
				s = "";
			type = "DDMMAPI";
		}
		else
		{
			if (fld)
			{
				s = fldtostr(fld);
				type = ddfttypename(fld->type);
			}
			else
			{
				s = "";
				type = "?";
			}
		}
		prIndented(buf, depth + 1,
			   "<field side=\"%H\" type=\"%H\"%s>%H</field>\n",
			   side, type, (fld ? "" : " null=\"null\""), s);
		break;
	case NAME_OP :
		name = (char *)pred->right;
		prIndented(buf, depth + 1, "<name>%H</name>\n",
			   (strcmp(name, "$star") == 0 ? "*" : name));
		if (pred->rat == FIELD_OP && pred->altright)
			goto altRightFld;
		break;
	default:
		prIndented(buf, depth + 1, "<unknown op=\"%H\"/>\n",
			   TXqnodeOpToStr(pred->rt, scratchBuf,
					  sizeof(scratchBuf)));
		break;
	}

	prIndented(buf, depth, "</pred>\n");
ok:
	orderFlagsStr = TXfree(orderFlagsStr);
	return(1);
}

int
TXdumpqnode(buf, ddic, query, parentquery, fo, depth)
HTBUF	*buf;
DDIC *ddic;
QNODE *query;
QNODE *parentquery;
FLDOP *fo;
int depth;
{
	CONST char	*opName, *s, *innerDesc = "";
	QUERY		*q;
	TXCOUNTINFO	*countInfo;
	int		i;
	char		*d, *e, *leftNodeStr = NULL, nodeAttrs[512];
	char		opNameBuf[EPI_OS_INT_BITS/4 + 10];
	char		schemaBuf[1024];
	char		*rightNodeStr = NULL, tableTypeBuf[128];

	(void)parentquery;
	*schemaBuf = '\0';
	if(!query)
	{
		/* Always print both left and right nodes, even if NULL,
		 * so we can tell which is which:
		 */
		prIndented(buf, depth, "<qnode op=\"nullQnode\"/>\n");
		return 0;
	}
	q = query->q;

	/* We want to print all nodes, and in left-to-right order,
	 * to aid in interpreting the `query' tree.
	 */

	switch (query->state)
	{
	case QS_PREPED:		s = "prepped";		break;
	case QS_ACTIVE:		s = "active";		break;
	case QS_PCOMMIT:	s = "pcommit";		break;
	case QS_FAILED:		s = "failed";		break;
	case QS_ABORT:		s = "abort";		break;
	case QS_COMMITED:	s = "committed";	break;
	case QS_UNPREPPED:	s = "unprepped";	break;
	default:		s = "?";		break;
	}

	d = nodeAttrs;
	e = nodeAttrs + sizeof(nodeAttrs);
	*d = '\0';
	if (query->state != QS_PREPED && d < e)
		d += htsnpf(d, e - d, " state=\"%s\"", s);
	countInfo = &query->countInfo;
	if (TX_ISVALIDCOUNT(countInfo->rowsMatchedMin) && d < e)
		d += htsnpf(d, e - d, " rowsMatchedMin=\"%wd\"",
			    (EPI_HUGEINT)countInfo->rowsMatchedMin);
	if (TX_ISVALIDCOUNT(countInfo->rowsMatchedMax) && d < e)
		d += htsnpf(d, e - d, " rowsMatchedMax=\"%wd\"",
			    (EPI_HUGEINT)countInfo->rowsMatchedMax);
	if (TX_ISVALIDCOUNT(countInfo->rowsReturnedMin) && d < e)
		d += htsnpf(d, e - d, " rowsReturnedMin=\"%wd\"",
			    (EPI_HUGEINT)countInfo->rowsReturnedMin);
	if (TX_ISVALIDCOUNT(countInfo->rowsReturnedMax) && d < e)
		d += htsnpf(d, e - d, " rowsReturnedMax=\"%wd\"",
			    (EPI_HUGEINT)countInfo->rowsReturnedMax);
	if (TX_ISVALIDCOUNT(countInfo->indexCount) && d < e)
		d += htsnpf(d, e - d, " indexCount=\"%wd\"",
			    (EPI_HUGEINT)countInfo->indexCount);
	if (query->ordered)
		d += htsnpf(d, e - d, " ordered=\"%d\"", (int)query->ordered);

	opName = TXqnodeOpToStr(query->op, opNameBuf, sizeof(opNameBuf));

	/* Note: No `default:' in this switch, so we get compiler
	 * warning about something we did not handle.  Unknown
	 * QNODE_OP handled below this.
	 */
	switch(query->op)
	{
		case PROJECT_OP:
			innerDesc = " leftChild=\"inrows\" rightChild=\"fields\"";
			goto doNode;
			/*
			if(query->fldlist->cnt > 1)
				prIndented(buf, depth, "<field>%s</field>", query->fldlist->s[0]);
			for(i=1; i < query->fldlist->cnt - 1; i++)
				prIndented(buf, depth, "<field>%s</field>", query->fldlist->s[i]);
			*/
		case SELECT_OP:
			innerDesc = " leftChild=\"inrows\" rightChild=\"criteria\"";
			goto doNode;
			/* WTF from qnode 
			if(query->fldlist)
			{
				if(query->fldlist->cnt > 1)
					prIndented(buf, depth, "%s", query->fldlist->s[0]);
				for(i=1; i < query->fldlist->cnt - 1; i++)
					prIndented(buf, depth, ", %s", query->fldlist->s[i]);
			}
			else
				prIndented(buf, depth, "*");
			*/
		case RENAME_OP:
			prIndented(buf, depth,
				   "<qnode op=\"%H\" to=\"%H\"%s>\n",
				   opName, query->tname, nodeAttrs);
			goto doLR;
		case CREATE_OP:
		case ALTER_OP:
		case DROP_OP:
			prIndented(buf, depth,
				   "<qnode op=\"%H\" what=\"%H\"%s%s>\n",
				   opName, query->tname, nodeAttrs, innerDesc);
			goto doLR;
		case TABLE_OP:
			{
			char	*sStr = NULL;

			innerDesc = " leftChild=\"schema\"";
			sStr = TXddSchemaToStr((DD *)query->left, 0);
			htsnpf(schemaBuf, sizeof(schemaBuf),
			       "<schema>%+.*H</schema>",
			       (int)(sizeof(schemaBuf) - 18), sStr);
			sStr = TXfree(sStr);
			}
			leftNodeStr = schemaBuf;
			goto doNode;
		case TABLE_AS_OP:
			innerDesc = " rightChild=\"type\"";
			htsnpf(tableTypeBuf, sizeof(tableTypeBuf),
			       "<type>%c</type>\n", *(char *)query->right);
			rightNodeStr = tableTypeBuf;
			goto doNode;
		case NAMENUM_OP:
			prIndented(buf, depth,
				   "<qnode op=\"%H\" num=\"%H\"%s%s>\n",
				   opName, query->tname, nodeAttrs, innerDesc);
			goto doLR;
#ifdef TX_USE_ORDERING_SPEC_NODE
		case ORDERING_SPEC_OP:
			prIndented(buf, depth,
				   "<qnode op=\"%H\" spec=\"%H\"%s%s>\n",
				   opName, query->tname, nodeAttrs, innerDesc);
			goto doLR;
#endif /* TX_USE_ORDERING_SPEC_NODE */
		case ARRAY_OP:
		case FOP_ADD:
		case FOP_SUB:
		case FOP_MUL:
		case FOP_DIV:
		case FOP_MOD:
		case FOP_CNV:
		case FOP_ASN:
		case FOP_EQ:
		case FOP_LT:
		case FOP_LTE:
		case FOP_GT:
		case FOP_GTE:
		case FOP_AND:
		case FOP_OR:
		case FOP_NEQ:
		case FOP_MM:
		case FOP_NMM:
		case FOP_MAT:
		case FOP_RELEV:
		case FOP_PROXIM:
		case FOP_IN:
		case FOP_IS_SUBSET:
		case FOP_INTERSECT:
		case FOP_INTERSECT_IS_EMPTY:
		case FOP_INTERSECT_IS_NOT_EMPTY:
		case FOP_COM:
		case FOP_MMIN:
		case FOP_TWIXT:
		case LPAREN:
		case RPAREN:
		case LIST_OP:
		case EQUAL_OP:
		case PRODUCT_OP:
		case COLUMN_OP:
		case DEL_SEL_OP:
		case GRANT_OP:
		case HAVING_OP:
		case TX_QNODE_NUMBER:
		case NNUMBER :
		case INSERT_OP:
		case UNION_OP:
		case STRING_OP:
		case UPD_SEL_OP:
		case VALUE_OP:
		case VIEW_OP :
		case NOT_OP  :
		case COUNTER_OP:
		case DEL_ALL_OP:
		case REVOKE_OP:
		case INDEX_OP:
		case PROP_OP :
		case FLOAT_OP:
		case DISTINCT_OP:
		case TRIGGER_OP:
		case UPD_ALL_OP:
		case CONVERT_OP:
		case EXISTS_OP:
		case SUBQUERY_OP:
		case NO_OP:
		case DEMUX_OP:
		case ALL_OP:
		case NULL_OP:
		case HINT_OP:
		case PRED_OP:
		case QNODE_OP_UNKNOWN:
		doNode:
			prIndented(buf, depth, "<qnode op=\"%H\"%s%s>\n",
				   opName, nodeAttrs, innerDesc);
		doLR:
			if (q)
			{
				TXdumpPred(buf, q->pred, depth + 1);
				if (q->proj && q->proj->preds)
				{
					prIndented(buf, depth + 1,
						   "<proj type=\"%H\">\n",
					      TXprojTypeStr(q->proj->p_type));
					for (i = 0; i < q->proj->n; i++)
						TXdumpPred(buf,
							   q->proj->preds[i],
							   depth + 2);
					prIndented(buf, depth + 1, "</proj>\n");
				}
				if (q->order && q->order->preds)
				{
					prIndented(buf, depth + 1, "<order>\n");
					for (i = 0; i < q->order->n; i++)
						TXdumpPred(buf,
							   q->order->preds[i],
							   depth + 2);
					prIndented(buf, depth + 1, "</order>\n");
				}
			}
			if (leftNodeStr)
				prIndented(buf, depth + 1, leftNodeStr);
			else
				TXdumpqnode(buf, ddic, query->left, query,
					    fo, depth + 1);
			if (rightNodeStr)
				prIndented(buf, depth + 1, rightNodeStr);
			else
				TXdumpqnode(buf, ddic, query->right, query,
					    fo, depth + 1);
			prIndented(buf, depth, "</qnode>\n");
			goto ok;
		case PARAM_OP:
			prIndented(buf, depth,
				   "<qnode op=\"%H\" number=\"%d\"%s>",
				   opName, (int)(EPI_VOIDPTR_INT)query->left,
				   nodeAttrs);
			/* wtf param value?  not set yet? */
			prIndented(buf, 0, "</qnode>\n");
			goto ok;
		case NAME_OP:
			prIndented(buf, depth,
				   "<qnode op=\"%H\"%s>%H</qnode>\n",
				   opName, nodeAttrs, query->tname);
			/* WTF
			prIndented(buf, depth, "<require>");
			if(parentquery && parentquery->fldlist &&
			   parentquery->fldlist->cnt > 1)
			{
				prIndented(buf, depth, "<field>%s</field>", parentquery->fldlist->s[0]);
				for(i=1; i < parentquery->fldlist->cnt - 1; i++)
					prIndented(buf, depth, "<field>%s</field>", parentquery->fldlist->s[i]);
			}
			prIndented(buf, depth, "</require>\n");
			*/
			goto ok;
		case ORDER_OP:
		case ORDERNUM_OP:
			prIndented(buf, depth,
				   "<qnode op=\"%H\" leftChild=\"ordering\" rightChild=\"inrows\"%s>\n",
				   opName, nodeAttrs);
			goto doLR;
		case GROUP_BY_OP:
			innerDesc = " leftChild=\"grouping\" rightChild=\"inrows\"";
			goto doNode;
		case AGG_FUN_OP:
		case REG_FUN_OP:
			innerDesc = " leftChild=\"name\" rightChild=\"args\"";
			goto doNode;
		case FIELD_OP:
			{
			FLD *f = query->tname;
			const char	*nullVal = "";

			if (TXfldIsNull(f)) nullVal = " nullVal=\"nullVal\"";
			prIndented(buf, depth,
				   "<qnode op=\"%H\" type=\"%s\"%s%s>",
				   opName, TXfldtypestr(f), nullVal,
				   nodeAttrs);
			prIndented(buf, 0, "%H</qnode>\n", fldtostr(f));
			}
			goto ok;
	}
	/* This is essentially `default:' for above switch, but
	 * outside it so that compiler detects unhandled QNODE_OP
	 * cases above:
	 */
	goto doNode;

ok:
	return 0;
}

/******************************************************************/

static int TXqnodeListGetItemAndCount ARGS((QNODE *query, int idx, int subList,
					    QNODE **item));
static int
TXqnodeListGetItemAndCount(query, idx, subList, item)
QNODE	*query;		/* (in) LIST_OP/NAME_OP tree to examine */
int	idx;		/* (in) #items to skip */
int	subList;	/* (in) nonzero: get sublist instead of item */
QNODE	**item;		/* (out) item value */
/* Sets `*item' to `idx'th (counting from 0) item value in `query'
 * LIST_OP/NAME_OP tree, or NULL if not found.
 * NOTE: may change some FIELD_OP members to NAME_OP.
 * Returns number of items traversed (including `idx'th if found).
 */
{
	static CONST char	fn[] = "TXqnodeListGetItemAndCount";
	int			numLeft, numRight;
	FLD			*fld;

	if (!query || idx < 0) goto err;	/* error; too far */
	if (idx == 0 && subList)		/* found sublist */
	{
		*item = query;
		return(1);
	}
	switch (query->op)
	{
	case LIST_OP:
		numLeft = TXqnodeListGetItemAndCount(query->left, idx,
						     subList, item);
		if (numLeft <= 0) goto err;
		if (numLeft > idx) return(numLeft);	/* got desired item */
		numRight = TXqnodeListGetItemAndCount(query->right,
						      idx - numLeft, subList,
						      item);
		if (numRight <= 0) goto err;
		return(numLeft + numRight);
	case FIELD_OP:
		if (idx != 0)			/* not our item yet */
		{
			*item = NULL;
			return(1);
		}
		fld = (FLD *)query->tname;
		query->tname = TXstrdup(TXPMBUFPN, fn, fldtostr(fld));
		fld = closefld(fld);
		query->op = NAME_OP;
		goto nameOp;			/* avoid gcc 7 warning */
	case NAME_OP:
	nameOp:
		*item = (idx == 0 ? query : NULL);
		return(1);			/* traversed 1 item */
	default:				/* should not happen */
		goto err;
	}

err:
	*item = NULL;
	return(0);				/* error */
}

char *
TXqnodeListGetItem(query, idx)
QNODE	*query;	/* (in) LIST_OP/NAME_OP tree to examine */
int	idx;	/* (in) item index */
/* Returns QNODE.tname value of `idx'th (counting from 0) item in
 * LIST_OP/NAME_OP `query' tree, or NULL if not found.
 */
{
	QNODE	*item;

	TXqnodeListGetItemAndCount(query, idx, 0, &item);
	return(item->tname);
}

QNODE *
TXqnodeListGetSubList(query, idx)
QNODE	*query;	/* (in) LIST_OP/NAME_OP tree to examine */
int	idx;	/* (in) item index */
/* Returns QNODE sub-tree of `idx'th (counting from 0) item in
 * LIST_OP/NAME_OP `query' tree, or NULL if not found.
 */
{
	QNODE	*item;

	TXqnodeListGetItemAndCount(query, idx, 1, &item);
	return(item);
}
