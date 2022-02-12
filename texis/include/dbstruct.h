/* -=- kai-mode: John -=- */
#ifndef _DBSTRUCT_H
#define _DBSTRUCT_H


/******************************************************************/
/*	Size of buffers used internally when dealing with query */
#define MAXINSZ		8192
#define MAX_INDEX_EXPS	16

/******************************************************************/
/* Defines to select operations within the parsed tree */

typedef enum QUERY_OP
{
	Q_OFFSET	= 0x01000000,
	Q_SELECT,
	Q_PROJECT,
	Q_CPRODUCT,
	Q_RENAME,
	Q_UNION,
	Q_DIFFERENCE,
	Q_APPEND,
	Q_DELETE,
	Q_UPDATE,
	Q_ORDER,
	Q_GROUP,
	Q_DISTINCT,
	Q_PROXIM,
	Q_PROP,
	Q_DEMUX
} QUERY_OP;

/******************************************************************/
/* Defines for operations etc. in parse tree */
/* Everything < 0x20 and > 0x7f < 0xa0 is for fldmath */
/* Keep in ASCII order to avoid contention */

/*
X X    XX  X                X XXX XX XXXXXX  XXX XXXXXX      X
!"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^
                              _`abcdefghijklmnopqrstuvwxyz{|}~
			        X XX XX      XXX XXXXX
*/

typedef enum QNODE_OP
{
	QNODE_OP_UNKNOWN = 0,	/* some code might assume this is 0? */
	/* NOTE: FLDMATH_... ops must correspond to FOP_... ops: */
	FLDMATH_ADD = 1,
	FLDMATH_SUB,
	FLDMATH_MUL,
	FLDMATH_DIV,
	FLDMATH_MOD,
	FLDMATH_CNV,
	FLDMATH_ASN,
	FLDMATH_AND = 13,
	FLDMATH_OR,
	FLDMATH_MM = 16,
	FLDMATH_NMM,
	FLDMATH_MAT,
	FLDMATH_RELEV,
	FLDMATH_PROXIM,
	PRED_OP = 'P',
	FLDMATH_EQ = 0x88,
	FLDMATH_LT,
	FLDMATH_LTE,
	FLDMATH_GT,
	FLDMATH_GTE,
	FLDMATH_NEQ = 0x8f,
	FLDMATH_IN = FOP_IN,
	FLDMATH_COM = FOP_COM,
	FLDMATH_MMIN = FOP_MMIN,
	FLDMATH_TWIXT = FOP_TWIXT,
	FLDMATH_IS_SUBSET = FOP_IS_SUBSET,
	FLDMATH_INTERSECT = FOP_INTERSECT,
	FLDMATH_INTERSECT_IS_EMPTY = FOP_INTERSECT_IS_EMPTY,
	FLDMATH_INTERSECT_IS_NOT_EMPTY = FOP_INTERSECT_IS_NOT_EMPTY,
	SELECT_OP	= 0x02000001,
	PROJECT_OP,
	LPAREN,
	RPAREN,
	DROP_OP,
	LIST_OP,
	EQUAL_OP,
	PARAM_OP,
	PRODUCT_OP,
	AGG_FUN_OP,	/* aggregate function: avg/min/max/sum/count */
	COLUMN_OP,
	DEL_SEL_OP,
	FIELD_OP,
	GRANT_OP,
	HAVING_OP,
	TX_QNODE_NUMBER,
	NNUMBER	,
	INSERT_OP,
	CREATE_OP,
	NAME_OP	,
	UNION_OP,
	GROUP_BY_OP,
	RENAME_OP,
	STRING_OP,
	TABLE_OP,
	UPD_SEL_OP,
	VALUE_OP,
	VIEW_OP	,
	NOT_OP	,
	TABLE_AS_OP,
	COUNTER_OP,
	DEL_ALL_OP,
	REG_FUN_OP,	/* regular function */
	REVOKE_OP,
	INDEX_OP,
	ORDER_OP,
	PROP_OP	,
	FLOAT_OP,
	DISTINCT_OP,
	TRIGGER_OP,
	UPD_ALL_OP,
	CONVERT_OP,
	EXISTS_OP,
	SUBQUERY_OP,
	ORDERNUM_OP,
	NAMENUM_OP,
	NO_OP,
	DEMUX_OP,
	ALTER_OP,
	ALL_OP,
	NULL_OP,
  HINT_OP,
#ifdef TX_USE_ORDERING_SPEC_NODE
  ORDERING_SPEC_OP,
#endif /* TX_USE_ORDERING_SPEC_NODE */
	ARRAY_OP,
	BUFFER_OP,
	LOCK_TABLES_OP,
	UNLOCK_TABLES_OP,
	INFO_OP,
} QNODE_OP;

/******************************************************************/
/* Aggregate function numbers */

#define AGG_MAX		1
#define AGG_MIN		2
#define AGG_SUM		3
#define AGG_AVG		4
#define AGG_COUNT	5
#define AGG_COUNTDIS	6

/******************************************************************/
/* Permissions flags */

#define	PM_ALTER	0x001
#define PM_DELETE	0x002
#define PM_INDEX	0x004
#define PM_INSERT	0x008
#define PM_SELECT	0x010
#define PM_UPDATE	0x020
#define PM_REFERENCES	0x040
#define PM_GRANT	0x080
#define PM_GRANTOPT	0x100
#define PM_ALLPERMS	0x1FF
/* KNG 040428 additional perms for license: */
#define PM_OPEN		0x200
#define PM_CREATE	0x400

/******************************************************************/
/* Trigger Flags */

#define TRG_STATEMENT	0x001
#define TRG_ROW		0x002
#define TRG_EXTERNAL	0x004
#define TRG_INTERNAL	0x008
#define TRG_EXTERNAL2	0x010

/******************************************************************/
/* Table types from SYSTABLES.TYPE */

#define TEXIS_LINK	'L'
#define TEXIS_SYS_TABLE	'S'
#define TEXIS_TEMP_TABLE 't'
#define TEXIS_TABLE	'T'
#define TEXIS_VIEW	'V'
#define TEXIS_DEL_TABLE 'D'
#define TEXIS_BTREEINDEX_TABLE  INDEX_BTREE

/******************************************************************/
/* Index Types FROM SYSINDEX */

#define INDEX_3DB	'3'
#define INDEX_BTREE	'B'
#define INDEX_3CR	'C'
#define INDEX_DEL	'D'
#define INDEX_TEMP	'T'
#define INDEX_UNIQUE	'u'
#define INDEX_INV	'v'
#define INDEX_3DB2	'P'
#define INDEX_FULL	'F' /* WTF : Change name ? */
#define INDEX_FULLCR	'f'
#define INDEX_MM        'M'     /* INDEX_FULL without position data */
#define INDEX_MMCR      'm'
#define INDEX_CR	'c' /* Regular index create */

/******************************************************************/
/* Systable IDs */

typedef enum SYSTBL_tag
  {
    SYSTBL_UNKNOWN,
    SYSTBL_INDEX,
    SYSTBL_PERMS,
    SYSTBL_USERS,
    SYSTBL_TRIGGER,
    SYSTBL_TABLES,
    SYSTBL_COLUMNS,
    SYSTBL_LINK,
    SYSTBL_NUM,                                 /* must be last */
  }
SYSTBL;

/******************************************************************/
/* Table types */

#define TEXIS_OLD_TABLE		0
#define TEXIS_FAST_TABLE	1
#define DBASE_TABLE		2
#define TEXIS_RAM_TABLE		3
#define TEXIS_BTREE_TABLE	4                 /* JMT 98-06-11 */
#define TEXIS_TMP_TABLE		5               /* JMT 1999-05-25 */
#define TEXIS_RAM_BTREE_TABLE	6               /* JMT 1999-10-26 */
#define TEXIS_NULL1_TABLE	7               /* JMT 2000-06-14 */

/******************************************************************/
/* Locking modes */

#define	LOCK_AUTOMATIC	0
#define	LOCK_MANUAL	1
#define	LOCK_NONE	2

/******************************************************************/
/* Subquery types */

typedef enum SUBQ_TYPE
{
	SUBQUERY_SINGLEVAL,
	SUBQUERY_MULTIVAL,
	SUBQUERY_EXISTSVAL
} SUBQ_TYPE;

/******************************************************************/

#define RECID_INVALID	-1
#define RECID_DELETED	-2

/******************************************************************/
/*
	Structures for texis.  These are used internally in texis.
*/
/******************************************************************/

/* Defined in texint.h: */
#ifndef TXA2INDPN
typedef struct TXA2IND_tag	TXA2IND;
#  define TXA2INDPN	((TXA2IND *)NULL)
#endif /* !TXA2INDPN */

/******************************************************************/
/* Hold the information required for a table in the BTREE style */

typedef struct BINDEX_tag
{
	BTREE	*btree;
	TBL	*table;
	TXA2IND	*a2i;				/* (opt.) */
} BINDEX;

/******************************************************************/

/* Some (but not all) info that goes in Metamorph index _P.tbl: */
typedef struct TXMMPARAMTBLINFO
{
	EPI_OFF_T	originalTableSize;	/**< size of tbl @index update*/
	EPI_HUGEINT		totalRowCount;		/**< sum(RowCount) all words */
	EPI_HUGEINT		totalOccurrenceCount;	/**< sum(OccurrenceCount) "" */
	EPI_HUGEINT		totalWords;		/**< #words in dictionary */
	size_t		maxWordLen;		/**< max length of a word */
}
TXMMPARAMTBLINFO;
#define TXMMPARAMTBLINFOPN	((TXMMPARAMTBLINFO *)NULL)

#define TX_INIT_TXMMPARAMTBLINFO(pti)			\
	((pti)->originalTableSize = (EPI_OFF_T)(-1),	\
	 (pti)->totalRowCount = (EPI_HUGEINT)(-1),		\
	 (pti)->totalOccurrenceCount = (EPI_HUGEINT)(-1),	\
	 (pti)->totalWords = (EPI_HUGEINT)(-1),		\
	 (pti)->maxWordLen = (size_t)(-1))

typedef struct A3DBI_tag
{
	MMTBL	*mm;
	TTBL	*ttbl;
	BTREE	*td;
	BTREE	*newrec;
	BTREE	*del;
	BTREE	*upd;
	ulong	lasttoken;
	char	**explist;
        char    *locale;
	int	textsearchmode;	/* TXCFF textsearchmode of index */
	char	*name;
	BTREE	*mnew;		/* These are new piles during update */
	BTREE	*mupd;
	BTREE	*mdel;
	int	creating;	/* Is this being created? */
	BTREE	*ct;		/* Count of terms BTREE */
	char	**noiselist;	/* Noise list for this index */
        int     type;           /* INDEX_... type (INDEX_3DB or INDEX_FULL) */
	int	auxsz;		/* Auxilary data size */
	DD	*auxdd;		/* Auxilary data DD */
	BINDEX	auxbi;		/* for TXadd2indsetup() */
        BINDEX  mauxbi;         /* for TXadd2indsetup() (index updating) */
	TXA2IND	*auxa2i;	/* TXA2IND * for TXadd2ind */
	TXA2IND	*mauxa2i;       /* TXA2IND * for TXadd2ind (index updating) */
        BTLOC   delloc;
        int     auxnewdel;      /* bits 0,1: del from new, new-new tree */
	long	version;	/* Version Number */
	TXMMPARAMTBLINFO	paramTblInfo;
}
A3DBI;
#define A3DBIPN ((A3DBI *)NULL)

/******************************************************************/
/* Blob structure - #define instead of typedef for ntexis.n compilability */

/* making BLOB a typedef seems to break stuff downstream, e.g. ntexis.n */
#define BLOB struct BLOB_tag
BLOB {
	char *filename;
	EPI_OFF_T off;
};
#define BLOBPN ((BLOB *)NULL)

/******************************************************************/
/* Parameters for SQL statements with substitutable variables */

typedef struct PARAM_tag
{
	int	num;
	FLD	*fld;
	int	prevneeddata, needdata;
	void	*usr;
}
PARAM;
#define PARAMPN ((PARAM *)NULL)

/******************************************************************/
/* Predicate.  Stores a predicate that can be evaluated to determine
   its value. */

typedef struct PRED_tag
{
	QNODE_OP	lt, rt;
	QNODE_OP	lat, rat;
	QNODE_OP 	op;
	void	*left, *right;
	FLD	*altleft, *altright;
	int	value;
	char	*idisplay;	/* Internal Display */
	char	*edisplay;	/* External Display */
	int	handled;
	int	dff;		/* Don't free field JMT 1999-03-04 */
	void	*lvt, *rvt; /*  Valid table pointer JMT 1999-05-20 */
	void	*lnvt, *rnvt; /*  Valid table pointer JMT 1999-05-21 */
	char	**iname; /* Index to resolve this pred *//* JMT 1999-05-25 */
	char	*itype; /* Index to resolve this pred *//* JMT 1999-05-25 */
	int	rev; /* Reversed index ?*//* JMT 1999-05-25 */
	int	refc; /* Ref. count *//* JMT 1999-07-07 */
	int	indexcnt; /* Count of indexes */
	int	assumetrue;
	int	is_distinct;
	FLD	*resultfld;
	fop_type fldmathfunc;
#ifdef TX_USE_ORDERING_SPEC_NODE
	TXOF	orderFlags;		/* OF_DESCENDING, OF_IGN_CASE */
#endif /* TX_USE_ORDERING_SPEC_NODE */
	byte	didPostProcLinearMsg;
}
PRED;
#define PREDPN  ((PRED *)NULL)

/******************************************************************/
/* Structure used when building products.  Takes two arrays of
   fields, and produces a third array that is there product. */

typedef struct PROD_tag
{
	FLD	**in1, **in2, **out;
	int	n1, n2, nout;
}
PROD;
#define PRODPN  ((PROD *)NULL)

/******************************************************************/
/* Hold the statistics for the fields seen */

typedef struct FLDSTAT_tag
{
	char	collect;
	FLD	*count;
	FLD	*max;
	FLD	*min;
	FLD	*sum;
}
FLDSTAT;
#define FLDSTATPN	((FLDSTAT *)NULL)

/******************************************************************/

typedef struct NFLDSTAT_tag NFLDSTAT;
struct NFLDSTAT_tag
{
	PRED	*pred;        /* The predicate to build the field */
	FLD	*fld;           /* The field that is getting data */
	NFLDSTAT *next;         /* The next one in line */
	int	needfield;        /* Do we actually need to field */
	int	inuse;   /* Are we using this in this invocations */
	int	isdup;	                 /* If it is a dup struct */
	BINDEX	unique_values;    /* BTREE to hold unique values */
};

/******************************************************************/
/* Hold the information required for a table in the inverted
 * BTREE style
 */

typedef struct BINVDX_tag
{
	int	rev;		/* Reversed */
	BTREE	*btree;
	FLD	*fld;
}
BINVDX;

/******************************************************************/
/* Hold projection information.  Holds the predicates which
   evaluate the functions that produce the desired results */

typedef enum PROJ_TYPE
{
	/* Note that order of PROJ enum is significant; see tup_project(): */
	PROJ_AGG_END = -2,	/* -2 - End of agg */
	PROJ_AGG_DONE = -1,	/* -1 - End of agg */
	PROJ_UNSET = 0,
	/* KNG 20130528 PROJ_SINGLE apparent usage: only a single
	 * returned row: aggregate function(s).  May also be used for
	 * SELECT, if left child is GROUP BY with *no* aggregate functions?
	 */
	PROJ_SINGLE,		/* 1 - No Aggregates */
	/* KNG 20130528 PROJ_AGG apparent usage: default projection
	 * type; *no* aggregate functions:
	 */
	PROJ_AGG,		/* 2 - Aggregates */
	PROJ_AGG_CALCED,	/* 3 - Aggregate functions calculated */
	PROJ_SINGLE_END,	/* 4 - After end of input, no aggregates */
	PROJ_CLEANUP		/* 5 - Need Cleanup */
} PROJ_TYPE;

typedef struct PROJ_tag
{
	int 		n;
	PROJ_TYPE	p_type;     /* Do we have aggregate functions, or all rows */
	PRED		**preds;            /* Predicate to evaluate the columns */
}
PROJ;

/******************************************************************/

typedef struct DDIC_tag	DDIC;
#define DDICPN  ((DDIC *)NULL)
#define DDICPPN ((DDIC **)NULL)
typedef struct IIITRIGGER_tag IIITRIGGER;
struct IIITRIGGER_tag
{
	int	type;
	char	*action;
	char	*when;
	IIITRIGGER	*next;
	IIITRIGGER	*prev;
	DDIC    *ddic;
};

/******************************************************************/

typedef struct IITRIGGER_tag
{
	IIITRIGGER	*row;
	IIITRIGGER	*statement;
} IITRIGGER ;

/******************************************************************/

typedef struct ITRIGGER_tag
{
	IITRIGGER	*before;
	IITRIGGER	*after;
	IITRIGGER	*instead;
} ITRIGGER ;

/******************************************************************/

typedef struct TRIGGER_tag
{
	ITRIGGER	*insert;
	ITRIGGER	*update;
	ITRIGGER	*deltrg;
} TRIGGER ;

/******************************************************************/
/* Holds all the information about a table while it is current
   within the database */

#ifndef DBTBLPN
typedef struct DBTBL_tag DBTBL;
#  define DBTBLPN ((DBTBL *)NULL)
#endif /* !DBTBLPN */

/******************************************************************/

typedef struct DDCACHE_tag	DDCACHE;

/******************************************************************/

typedef int	TXERR;
#define	MAXTXERR	100

/******************************************************************/

typedef struct TXTBLCACHE_tag
{
	ft_counter	lastr;
	TBL		*tbl;
	FLD		*flds[7];
} TXTBLCACHE;

TXTBLCACHE *TXtblcacheClose(TXTBLCACHE *tblCache);

typedef enum TX_MESSAGES_tag
{
	MESSAGES_DUPLICATE,
	MESSAGES_INDEXUSE,
	MESSAGES_FAILED_DELETE,
	MESSAGES_TIME_FDBI,
	MESSAGES_DUMP_QNODE,
	MESSAGES_DUMP_QNODE_FETCH,
	MESSAGES_SQL_PREPARE_CONVERT,
	NUM_MESSAGES				/* must be last */
}
TX_MESSAGES;
#define TX_MESSAGESPN	((TX_MESSAGES *)NULL)

typedef enum TX_OPTIMIZE_TYPE
{
 OPTIMIZE_JOIN
,OPTIMIZE_COMPOUND_INDEX
,OPTIMIZE_COPY
,OPTIMIZE_COUNT_STAR
,OPTIMIZE_MINIMAL_LOCKING
,OPTIMIZE_GROUP
,OPTIMIZE_FASTSTATS
,OPTIMIZE_READLOCK
,OPTIMIZE_ANALYZE
,OPTIMIZE_SKIPAHEAD
,OPTIMIZE_AUXDATALEN
,OPTIMIZE_LIKEPLONELY
,OPTIMIZE_INDEXONLY
,OPTIMIZE_MMIDXUPDATE
,OPTIMIZE_INDEXDATAGROUP
,OPTIMIZE_LIKE_AND
,OPTIMIZE_LIKE_AND_NOINV
,OPTIMIZE_LIKE_WITH_NOTS
,OPTIMIZE_SHORTCUTS
,OPTIMIZE_INDEX_BATCH_BUILD
,OPTIMIZE_LINEAR_RANK_INDEX_EXPS
,OPTIMIZE_PTRS_TO_STRLSTS
,OPTIMIZE_SORTED_VARFLDS
,OPTIMIZE_PRED					/* may change PRED ops etc. */
,OPTIMIZE_INDEX_VIRTUAL_FIELDS
,OPTIMIZE_INDEX_DATA_ONLY_CHECK_PREDICATES
,OPTIMIZE_GROUP_BY_MEM
,OPTIMIZE_LIKE_HANDLED
,OPTIMIZE_SQL_FUNCTION_PARAMETER_CACHE
,OPTIMIZE_COUNT /* KEEP_AS_LAST -- must be last item in enum */
} TX_OPTIMIZE_TYPE;

/******************************************************************/

struct DDIC_tag {
#ifndef OLD_LOCKING
	long	tid;		/* SYSTABLES ID */
	long	cid;		/* SYSCOLUMNS ID */
	long	iid;		/* SYSINDEX ID */
	long	uid;		/* SYSUSERS ID */
	long	pid;		/* SYSPERMS ID */
	long	rid;		/* SYSTRIG  ID */
	long	lid;		/* SYSLINK  ID */
#else
	short	tid;		/* SYSTABLES ID */
	short	cid;		/* SYSCOLUMNS ID */
	short	iid;		/* SYSINDEX ID */
	short	uid;		/* SYSUSERS ID */
	short	pid;		/* SYSPERMS ID */
	short	rid;		/* SYSTRIG  ID */
	short	lid;		/* SYSLINK  ID */
#endif
	int	sid;		/* Server ID */
	void	*perms;		/* Permissions structure */
	char	*pname;		/* Path to data_dictionary */
	char	*epname;	/* Path to data_dictionary for executes */
	TBL	*tabletbl;	/* SYSTABLES */
	TBL	*coltbl;	/* SYSCOLUMNS */
	TBL	*indextbl;	/* SYSINDEX */
	TBL	*userstbl;	/* SYSUSERS */
	TBL	*permstbl;	/* SYSPERMS */
	TBL	*trigtbl;	/* SYSTRIGGERS */
	TBL	*linktbl;	/* SYSLINK */
	BTREE	*tablendx;
	BTREE	*colndx;
	BTREE	*coltblndx;
	BTREE	*indexndx;
	DBLOCK	*dblock;	/* Handle to lock region */
	DDCACHE	*ddcache;	/* Cache of arbitrary tables */
	char	*tbspc;		/* Path to create tables in */
	char	*indspc;	/* Path in which to create indices */
	char	*indrctspc;	/* Path for indirects *//* JMT 1999-12-14 */
	int	increate;	/* Are we creating the database */
	int	nerrs;		/* Number of errors */
	TXERR	errstack[MAXTXERR];	/* Stack of errors */
	int	state;		/* State Flag */
	time_t	starttime;	/* Start-Time */
	int	nolocking;	/* Perform no locking */
	int	manuallocking;	/* Perform Manual locking */
#ifndef NO_CACHE_TABLE
	TXTBLCACHE	*tbltblcache;	/* SYSTABLES cache copy */
	TXTBLCACHE	*indtblcache;	/* SYSINDEX cache copy */
	TXTBLCACHE	*usrtblcache;	/* SYSUSERS cache copy */
	TXTBLCACHE	*prmtblcache;	/* SYSPERMS cache copy */
	TXTBLCACHE	*lnktblcache;	/* SYSLINK cache copy */
#endif
	byte	systablesChanged;	/* SYSTABLES change detected */
	byte	sysindexChanged;	/* SYSINDEX change detected */
	int	no_bubble;
	void	*dbc;		/* Pointer to DBC we were allocated in */
	void	*ihstmt;	/* Pointer to HSTMT for internal use */
	int	ihstmtIsInUse;  /* `ihstmt' in use (recursion detection) */
	int	optimizations[OPTIMIZE_COUNT];	/* Optimizations */
	int	ch;		/* Should we counthits */
	int	messages[NUM_MESSAGES];	/* Messages to issue */
        int     dbcalloced;
	int	options[10];	/* DDIC options */
	int	rlocks, wlocks;	/* Total index+table read/write locks */
	TXPMBUF	*pmbuf;		/* (opt.) owned/cloned putmsg buffer */
	struct LOCKTABLES_ENTRY *locktables_entry;
};

#define DDIC_OPTIONS_NO_TRIGGERS	0
#define DDIC_OPTIONS_INDEX_CACHE	1
#define DDIC_OPTIONS_MAX_INDEX_TEXT	2
#define DDIC_OPTIONS_SEMS_PER_ID	3
#define DDIC_OPTIONS_MAX_ROWS		4
#define DDIC_OPTIONS_IGNORE_MISSING_FIELDS	5
#define DDIC_OPTIONS_LOG_SQL_STATEMENTS	6
/* MAX:9 */

/******************************************************************/

typedef enum DBIDX_MEMT
{
	DBIDX_MEMUNK,
	DBIDX_NATIVE,	/* Disk file */
	DBIDX_MEMORY,	/* RAM index */
	DBIDX_CACHE	/* Cached */
} DBIDX_MEMT;

typedef enum DBIDX_IDXT
{
	DBIDX_IDXUNK,
	DBIDX_BTREE,	/* A Btree, of some form */
	DBIDX_RECLIST,	/* A list of record ids. */
	DBIDX_KEYREC	/* A list of keys (BTLOC), and recids */
} DBIDX_IDXT;

typedef struct DBIDX_tag
{
	BTREE	*btree;
	void	*keyrec;
	int	nrank;	/* Number of rank terms */
#ifndef NO_BUBBLE_INDEX
	EPI_HUGEUINT	nrecs;
	EPI_HUGEINT	rowsReturned;	/* != -1: number of recs after likeprows */
	DBIDX_MEMT	type;
	int	himark, lomark;
	int	inclo, inchi;
	char	*lobuf, *hibuf;
	size_t	losz, hisz;
	size_t	lsz;
	int	gotit, abvlo;
	DBTBL	*dbtbl;
	char	*iname;
	char	*sysindexParams;		/* SYSINDEX.PARAMS for iname*/
	ft_counter	lread;
	char	lbuf[8192];
	RECID	lrecid;
#endif /* !NO_BUBBLE_INDEX */
	DBIDX_IDXT	itype;
	int	alloced;

	int	keepcached;
	/* indexdataonly: nonzero if all the fields we need are in the
	 * index, ie. we will not need to read the table too:
	 */
	int	indexdataonly;	/* JMT 2000-06-14 */
	/* indexdbtbl: DBTBL for the index's data, when `indexdataonly': */
	DBTBL	*indexdbtbl;/* JMT 2000-06-18 */
	byte	deDupRecids;	/* nonzero: de-dup adjacent recids */
} DBIDX ;
#define DBIDXPN	((DBIDX *)NULL)

/******************************************************************/

typedef struct IINODE_tag	IINODE;
#define IINODEPN	((IINODE *)NULL)
typedef struct QNODE	QNODE;
#define QNODEPN		((QNODE *)NULL)
#ifndef FDBIPN
/* this is in fdbi.h, but including it here is hairy:  -KNG */
typedef struct FDBI_tag FDBI;
#  define FDBIPN        ((FDBI *)NULL)
#endif

struct DBTBL_tag {
	char	type;                          /* What type is the table */
	char	*indexAsTableSysindexParams;/*SYSINDEX.PARAMS if indexaccess*/
	char	itype;                         /* What type is the index */
	int	indguar;			   /* Guaranteed index ? */
	BTLOC	recid;                              /* Current record id */
#ifndef OLD_LOCKING
	long	tblid;                     /* The table id from locking. */
#else
	int	tblid;                     /* The table id from locking. */
#endif
	void	*perms;           /* The permission flags for this table */
	char	*lname;                              /* The logical name */
	char	*rname;    /* The "real" logical name (for index lookup) */
	TBL	*tbl;                                  /* The tbl for it */
	FLD	*frecid;                     /* The record id as a field */
	BINDEX	*indexes;                      /* Array of BTREE indexes */
	char	**indexNames;			/* parallel names to "" */
	char	**indexFldNames;		/* parallel fields to "" */
	char	**indexParams;			/* parallel SYSINDEX.PARAMS */
	int	nindex;                       /* Number of BTREE indexes */
	A3DBI	**dbies;                         /* Array of 3db indexes */
	int	ndbi;                           /* Number of 3db indexes */
	DBIDX	index;	                          /* Primary BTREE index */
	A3DBI	*dbi;                               /* Primary 3db index */
	DDIC	*ddic;                                /* Data dictionary */
	PRED	*ipred;       /* If this predicate fails no more records */
	PRED	*pred;                         /* Does this record match */
	PROJ	*order;           /* Current ordering of tuples in table */
	TRIGGER	*trigger;	    /* Triggers that apply to this table */
#ifdef OLD_STATS
	FLDSTAT	ofldstats[DDFIELDS];
#endif
	int	ninv;
	BINVDX	*invidx;
	char	**invertedIndexNames;		/* parallel names to "" */
	char	**invertedIndexFldNames;	/* parallel fields to "" */
	char	**invertedIndexParams;		/* parallel SYSINDEX.PARAMS */
	int	needstats;                  /* Do we need to keep stats */
	time_t	idxtime;	    /* When was the index table changed */
	int	rlock;			/* Number of read locks we hold */
	int	wlock;		       /* Number of write locks we hold */
	EPI_HUGEUINT	indcnt;			  /* Index record count */
	ft_int	rank;			/* somebody points a fld at this */
	int	nireadl;	                 /* Type of index locks */
	int	niwrite;	                 /* Type of index locks */
	ft_counter	ireadc;			/* Last index read time */
	ft_counter	iwritec;	       /* Last index write time */
	void	*btcache;
	NFLDSTAT *nfldstat;
	DBIDX	rankindex;                    /* Keep rank values here. */
#ifdef STEAL_LOCKS
	long	lockid;		    /* The lock we are currently useing */
#endif
        FDBI    *fi;                                /* FDBI index  -KNG */
	QNODE   *qnode;			            /* QNODE for a view */
#ifdef CACHE_IINODE
	IINODE	*cacheinode;                  /* Cached iinode for pred */
#endif
	char	**indexfields; /* Fields for which I have an open index */
	int	nfldstatisdup;
	FDBI	**fdbies;                       /* Array of fdb indexes */
	char	**fdbiIndexNames;		/* Parallel names for "" */
	char	**fdbiIndexFldNames;		/* Parallel fields for "" */
	char	**fdbiIndexParams;		/* parallel SYSINDEX.PARAMS */
	int	nfdbi;                         /* Number of fdb indexes */
	int	nfldstatcountonly;
	FLD	**projfldcache;
	PROJ	*cachedproj;
	void	*extra;		                   /* Extra Data needed */
};

/******************************************************************/
/* Holds information on how to update fields */

typedef struct UPDATE_tag	UPDATE;
struct UPDATE_tag {
	char	*field;
	FLD	*fld;
	PRED	*expr;
	UPDATE	*next;
	ft_blobi delblob;
};

/******************************************************************/
/* Possible Query States */

typedef enum Q_STATE
{
	QS_UNPREPPED = -1,
	QS_PREPED,
	QS_ACTIVE,
	QS_PCOMMIT,
	QS_FAILED,
	QS_ABORT,
	QS_COMMITED,
	QS_NOMOREROWS
} Q_STATE;

/******************************************************************/
/* Maximum number of metamorph handles to return */

#define	MAXMMLST	16
typedef struct MMLST_tag	MMLST;
#define MMLSTPN (MMLST *)NULL
struct MMLST_tag {
	int	n;
	void	*handle[MAXMMLST];                         /* MMAPI * */
        char    *buf[MAXMMLST];                       /* MAW 06-28-94 */
        char    *name[MAXMMLST];                      /* MAW 06-27-94 */
};

#include "texis_countinfo.h"
#define TX_CLEAR_COUNTINFO(ci)						\
	((ci)->rowsMatchedMin = (ci)->rowsReturnedMin =			\
	 (ci)->indexCount = (EPI_HUGEINT)(-1),				\
	 (ci)->rowsMatchedMax = (ci)->rowsReturnedMax =	(EPI_HUGEINT)(-2))

/******************************************************************/

typedef enum TXqueryFlag_tag
{
	TXqueryFlag_In1Populated	= (1 << 0)  /* `in1' was populated */
}
TXqueryFlag;

/* Query structure */

#undef QUERY                    /* potential arpa/nameser.h collision */
typedef struct QUERY_tag {
	QUERY_OP	op;	/* Requested operation */
	Q_STATE		state;	/* State of the query */
	int		nrows;	/* Number of rows affected */
	DBTBL 		*out;	/* The output of this operation */
	DBTBL		*in1;	/* First operand */
	DBTBL		*in2;	/* Second operand */
	PROJ 		*proj;	/* The projection required */
	PROJ 		*order;	/* The ordering required */
	PRED		*pred;	/* The predicate requested */
	PROD		*prod;	/* Join needed */
	UPDATE		*update;/* Information for updates */
	long		newmode;/* Mode requested for chmod */
	unsigned int	group;	/* Group for chmod */
	IIITRIGGER	*tr_before; /* Row triggers before execution */
	IIITRIGGER	*tr_after; /* Row triggers after execution */
	void		*usr;   /* Random space for stuff */
	int             priorproj; /* Do we have a project above? */
	BTLOC		lastread; /* Store last read, for update */
	PRED		*pred1;/* Subst pred for join *//* JMT 1999-05-27 */
	PRED		*pr1;  /* Subst pred for join *//* JMT 1999-07-08 */
	PRED		*pr2;  /* Subst pred for join *//* JMT 1999-07-08 */
	TXqueryFlag	flags;
}
QUERY;
#define QUERYPN ((QUERY *)NULL)

typedef struct BUFFER_NODE {
	int 		LockType;
	int 		Locked;
	double	LockTime;
} BUFFER_NODE;

/******************************************************************/
/* Node in parse tree */

struct QNODE {
	QNODE_OP op;
	Q_STATE	 state;
	int	 ordered;	/* Determined that the query is ordered */
	QNODE
		*org,		/* KNG 20071107 original version of node */
		*parentqn,	/* JMT 20100924 ability to walk up tree */
		*left,
		*right,
		*predicate_node; /** Predicate (condition) clause */
	QUERY	*q;
	void	*tname;
	SLIST	*fldlist;	/* Fields for this, and later operations */
	SLIST	*afldlist;	/* Available fields */
	SLIST	*pfldlist;	/* Fields later operations only */
	int	quitnow;	/* Quit Now */
	int	readanother;	/* Keep reading.  Don't return till end */
	int	analyzed;
	TXCOUNTINFO	countInfo;	/* Stats for Vortex */
	union {

	} extra;
};

/******************************************************************/
/* Structure to perform Metamorph searches */

typedef struct DDMMAPI_tag	DDMMAPI;
struct DDMMAPI_tag
{
	DDMMAPI	*self;	/* A pointer to ourself, copy protection */
	MMAPI	*mmapi;
	APICP	*cp;
	char	*query;
	TXbool	mmapiIsPrepped;	/* setmmapi() success */
	void	*qdata;
	int	qtype ;
	int	mmaplen;
        void    *buffer; /* a buffer with which i can hold data */
	int	freebuf; /* Should I free the buffer */
	void	*bt; /* Where to put PROXBTREE pointer */
	char	**wordlist; /* Wordlist for PMs */
	int	lonely;  /* Is this alone in the query */
};
#define DDMMAPIPN       ((DDMMAPI *)NULL)

/******************************************************************/

#define LASTTOKEN	"zz$epi$last"
#define LASTTOKENSZ	11

#define TEXIS_SUPERUSER	"_SYSTEM"

#endif /* _DBSTRUCT_H */
