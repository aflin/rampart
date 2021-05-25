#include "texint.h"

#define NODE_DATA_MANIPULATION 1
#define NODE_DATA_DEFINITION 2

#ifdef LOCK_SERVER
extern int TXInLockBlock;
#endif

static int
node_type(QNODE *q)
{
  int isdd = 0;
  if(!q) return 0;
  switch(q->op) {
    case PROJECT_OP:
  	case SELECT_OP:
  	case RENAME_OP:
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
  	case PRED_OP:
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
    case HAVING_OP:
  	case TX_QNODE_NUMBER:
  	case NNUMBER :
    case UNION_OP:
  	case STRING_OP:
    case VALUE_OP:
  	case NOT_OP  :
    case COUNTER_OP:
  	case FLOAT_OP:
  	case DISTINCT_OP:
    case CONVERT_OP:
  	case EXISTS_OP:
  	case SUBQUERY_OP:
  	case NAMENUM_OP:
  	case NO_OP:
  	case PARAM_OP:
  	case NAME_OP:
  	case ORDER_OP:
  	case ORDERNUM_OP:
  	case GROUP_BY_OP:
  	case DEMUX_OP:
    case ALL_OP:
  	case AGG_FUN_OP:
  	case REG_FUN_OP:
  	case FIELD_OP:
  	case NULL_OP:
  	case HINT_OP:
  #ifdef TX_USE_ORDERING_SPEC_NODE
  	case ORDERING_SPEC_OP:
  #endif /* TX_USE_ORDERING_SPEC_NODE */
  	case ARRAY_OP:
    case BUFFER_OP:
  	case QNODE_OP_UNKNOWN:
      break;
    case ALTER_OP:
  	case DROP_OP:
    case PROP_OP:
    case GRANT_OP:
    case CREATE_OP:
    case TABLE_OP:
    case TABLE_AS_OP:
    case VIEW_OP :
    case REVOKE_OP:
    case INDEX_OP:
    case TRIGGER_OP:
      isdd = NODE_DATA_DEFINITION;
      break;
    case DEL_ALL_OP:
  	case DEL_SEL_OP:
    case INSERT_OP:
    case UPD_ALL_OP:
    case UPD_SEL_OP:
    	isdd = NODE_DATA_MANIPULATION;
      break;
  }
  return isdd;
}

typedef int (*QnodeCallback)(QNODE *q, void *usr);

static int
TXqnode_type_callback(QNODE *q, void *usr)
{
  if(!q) return 0;
  *(int *)usr |= node_type(q);
  return 0;
}

#define TRAVERSE_SKIP_LEFT 0x01
#define TRAVERSE_SKIP_RIGHT 0x02
#define TRAVERSE_SKIP_ALL 0x03

static int
TXqnode_tablename_callback(QNODE *q, void *usr)
{
  if(!q) return 0;
  switch(q->op) {
    case SELECT_OP:
      return TRAVERSE_SKIP_LEFT;
    case PROJECT_OP:
      return TRAVERSE_SKIP_RIGHT;
    case NAME_OP: // printf(" %s", q->tname);
      break;
    default:
      // printf("\nOP: %s ", TXqnodeOpToStr(q->op, NULL, 0));
      break;
  }
  return 0;
}

static int
TXqnode_lock_tables_callback(QNODE *q, void *usr)
{
  if(!q) return 0;
  switch(q->op) {
    case ORDER_OP:
      return TRAVERSE_SKIP_LEFT;
    case SELECT_OP:
    case PROJECT_OP:
      return TRAVERSE_SKIP_RIGHT;
    case NAME_OP:
      if(q->q && q->q->out && q->q->out->rname) {
        TXlockindex(q->q->out, INDEX_READ, NULL);
        TXlocktable(q->q->out, R_LCK);
      }
    default:
      return 0;
  }
}

static int
TXqnode_unlock_tables_callback(QNODE *q, void *usr)
{
  if(!q) return 0;
  switch(q->op) {
    case ORDER_OP:
      return TRAVERSE_SKIP_LEFT;
    case SELECT_OP:
    case PROJECT_OP:
      return TRAVERSE_SKIP_RIGHT;
    case NAME_OP:
      if(q->q && q->q->out && q->q->out->rname)
      {
        TXunlocktable(q->q->out, R_LCK);
        TXunlockindex(q->q->out, INDEX_READ, NULL);
      }
    default:
      return 0;
  }
}

int
TXqnode_traverse(QNODE *q, void *usr, QnodeCallback cb)
{
  int rc;
  if(!q) return 0;
  rc = (*cb)(q, usr);
  if((rc & TRAVERSE_SKIP_LEFT) == 0) {
    switch(q->op) {
      case TABLE_OP: /* TABLE_OP uses left for DD */
      case PARAM_OP: /* PARAM_OP uses left for param number */
        break;
      default:
        TXqnode_traverse(q->left, usr, cb);
    }
  }
  if((rc & TRAVERSE_SKIP_RIGHT) == 0) {
    switch(q->op) {
      case TABLE_AS_OP: /* TABLE_AS_OP uses left for DD */
        break;
      default:
        TXqnode_traverse(q->right, usr, cb);
    }
  }
  return 0;
}

QNODE *
TXbuffer_node_init(QNODE *q)
{
  int node_type = 0;
  QNODE *rc;

  TXqnode_traverse(q, &node_type, TXqnode_type_callback);
  if(node_type != 0)
    return q;
//  printf("Could buffer");
//  TXqnode_traverse(q, &node_type, TXqnode_tablename_callback);
//  printf("\n");
  rc = openqnode(BUFFER_OP);
  rc->left = q;
  return rc;
}

DBTBL *
TXnode_buffer_prep(IPREPTREEINFO *prepinfo, QNODE *query, QNODE *parentquery, int *success)
{
  QUERY *q = query->q;

  if(prepinfo->analyze)
  {
    if(parentquery)
      query->pfldlist = parentquery->fldlist;
    if(!query->fldlist)
    {
      if(parentquery && parentquery->fldlist)
        query->fldlist = sldup(parentquery->fldlist);
    }
  }

  prepinfo->preq |= PM_SELECT;

  q->in1 = ipreparetree(prepinfo, query->left, query, success);
  if (q->in1 == NULL)
    return (DBTBL *) NULL;
  q->out = TXcreateinternaldbtblcopy(q->in1, TX_DBF_RINGDBF);
  query->countInfo = query->left->countInfo;
  return q->out;
}

int
TXnode_buffer_exec(QNODE *query, FLDOP *fo, int direction, int offset, int verbose)
{
  int r = 0, nrows = 0;
  QUERY *q = query->q;
  RECID *index;
  double start, now, stoptime;

  if(ioctldbtbl(q->out, RINGBUFFER_DBF_HAS_DATA, NULL) == 0)
  {
    TXqnode_traverse(query, &node_type, TXqnode_lock_tables_callback);
#ifdef LOCK_SERVER
    TXInLockBlock = 1;
#endif
    start = TXgettimeofday();
    now = start;
    if(TXApp->intSettings[TX_APP_INT_SETTING_LOCK_BATCH_TIME] > 0) {
      stoptime = start + (0.001 * TXApp->intSettings[TX_APP_INT_SETTING_LOCK_BATCH_TIME]);
    } else {
      stoptime = start + (0.001 * TXAppIntSettingDefaults[TX_APP_INT_SETTING_LOCK_BATCH_TIME]);
    }
    while(query->left->state != QS_NOMOREROWS && !r && now <= stoptime && ioctldbtbl(q->out, RINGBUFFER_DBF_HAS_SPACE, NULL) > 0) {
      r = TXdotree(query->left, fo, direction, offset);
      if(r == -1) {
        query->left->state = QS_NOMOREROWS;
      } else {
        tup_write(q->out, q->in1, fo, 0);
        nrows++;
      }
      now = TXgettimeofday();
    }
//    printf("Read %d rows in %lg seconds\n", nrows, (end - start));
#ifdef LOCK_SERVER
    TXInLockBlock = 0;
#endif
    TXqnode_traverse(query, &node_type, TXqnode_unlock_tables_callback);
    query->countInfo = query->left->countInfo;
  }
  index =  getdbtblrow(q->out);
  return TXrecidvalid(index) ? 0 : -1;
}
