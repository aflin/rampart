#ifndef MERGE_H
#define MERGE_H

#ifndef PILE_H
#  include "pile.h"
#endif
#ifndef METER_H
#  include "meter.h"
#endif


/* ---------------------------------- Merge -------------------------------- */

typedef int (MERGECMP) ARGS((PILE *a, PILE *b, void *usr));
#define MERGECMPPN              ((MERGECMP *)NULL)
#define MERGECMP_WTIX           ((MERGECMP *)0x1)

typedef struct MERGE_tag        MERGE;
#define MERGEPN                 ((MERGE *)NULL)

MERGE  *openmerge ARGS((MERGECMP *cmp, void *usr, size_t memsz,
                        PILEOPENFUNC *interopen));
MERGE  *closemerge ARGS((MERGE *m));
int     merge_setmeter ARGS((MERGE *m, char *intermsg, char *finalmsg,
                             METER *prevmeter, TXMDT type, MDOUTFUNC *out,
                             MDFLUSHFUNC *flush, void *usr));
int     merge_incdone ARGS((MERGE *m, EPI_HUGEINT nitems));
int     merge_addpile ARGS((MERGE *m, PILE *p));
int     merge_newpile ARGS((MERGE *m));
int     merge_newitem ARGS((MERGE *m, byte *blk, size_t sz));
int     merge_finish(MERGE *m, PILE *out, EPI_HUGEINT outMergeAddItems);
size_t  TXmergeGetMemUsed ARGS((MERGE *m));

extern int      TxMergeFlush;

#endif  /* !MERGE_H */
