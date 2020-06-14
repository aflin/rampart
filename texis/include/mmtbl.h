#ifndef MMTBL_H
#define MMTBL_H

#include "tin.h"
#include "api3.h"

#define MMQI struct mm_query_item
#define MMQIPN (MMQI *)NULL
MMQI
{
 byte suffixproc;
 byte wild;
 TXLOGITYPE logic;
 int  orpos;
 int  len; /* compare length for phrases */
 int  setno;
 byte *s;
 int  needmm;	/* Need metamorph to resolve */
 byte **words;
 size_t  *lens;
 int  nwords;
};

#define MMQL struct mm_query_list
#define MMQLPN (MMQL *)NULL
MMQL
{
 MMQI *lst;
 int	 n;
 int     dkpm;
};


#define MMTBL struct  mm_table
#define MMTBLPN (MMTBL *)NULL
MMTBL
{
 char  *query;
 APICP *cp;
 MMAPI *mm;
 MMQL  *ql;
 BTREE *bt;
 DBF   *bdbf;
 TTL   *tl;
};

#endif

MMTBL *openmmtbl ARGS((char *));
MMTBL *closemmtbl ARGS((MMTBL *));
/************************************************************************/
