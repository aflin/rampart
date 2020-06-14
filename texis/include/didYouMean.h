#ifndef DIDYOUMEAN_H
#define DIDYOUMEAN_H


#include "tstone.h"
#include "mmsg.h"

/* Symbols for flags: */
#define TX_DIDYOUMEAN_SYMBOLS_LIST      \
  I(SplitWords)                         \
  I(JoinWords)                          \
  I(MakePhrases)                        \
  I(UseHyphens)

/* Do not make phrases by default: more likely to return results, and
 * ranker will probably put the phrased results (if any) on top anyway:
 */
#define TXDYMF_DEFAULT_FLAGS    \
  (TXDYMF_SplitWords | TXDYMF_JoinWords)

/* Internal use: */
typedef enum TXDYMF_card_tag
{
#undef I
#define I(tok)  TXDYMF_card_##tok,
  TX_DIDYOUMEAN_SYMBOLS_LIST
#undef I
  TXDYMF_card_NUM                               /* must be last */
}
TXDYMF_card;

/* TXDYM flags: */
typedef enum TXDYMF_tag
{
#undef I
#define I(tok)  TXDYMF_##tok = (1 << TXDYMF_card_##tok),
  TX_DIDYOUMEAN_SYMBOLS_LIST
#undef I
  TXDYMF_ALL_FLAGS = ((1 << TXDYMF_card_NUM) - 1)
}
TXDYMF;
#define TXDYMFPN ((TXDYMF *)NULL)

/* Texis Did You Mean object: */
typedef struct TXDYM_tag        TXDYM;
#define TXDYMPN ((TXDYM *)NULL)

TXDYM   *TXdymClose ARGS((TXDYM *dym));
TXDYM   *TXdymOpen ARGS((TXPMBUF *pmbuf, TX *tx, CONST char *table,
                         CONST char *mmIndex));
int     TXdymSetFlags ARGS((TXDYM *dym, TXDYMF flags));
int     TXdymClearFlags ARGS((TXDYM *dym, TXDYMF flags));
TXDYMF  TXdymGetFlags ARGS((TXDYM *dym));
int     TXdymSetEquivIndexScaleFactor ARGS((TXDYM *dym,
                                            double equivIndexScaleFactor));
int     TXdymGetDidYouMeanQuery ARGS((TXDYM *dym, CONST char *query,
                                      char **didYouMeanQuery));

#endif /* !DIDYOUMEAN_H */
