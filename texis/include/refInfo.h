#ifndef TX_REFINFO_H
#define TX_REFINFO_H

/* Core (standalone) refInfo symbols.  Try not to #include anything
 * here, to avoid tangled dependencies in html vs. SQL code.
 */


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#define TX_REF_TYPE_SYMBOLS_LIST        \
I(link)                                 \
I(image)                                \
I(frame)                                \
I(iframe)                               \
I(strlink)

typedef enum TXrefType_tag
{
  TXrefType_unknown = -1,
#undef I
#define I(tok)  TXrefType_##tok,
TX_REF_TYPE_SYMBOLS_LIST
#undef I
  TXrefType_NUM
}
TXrefType;
#define TXrefTypePN    ((TXrefType *)NULL)

#define TX_REF_TYPE_FLAG(type)  ((TXrefTypeFlag)1 << (type))
typedef enum TXrefTypeFlag_tag
{
  TXrefTypeFlag_ALL = -1,
#undef I
#define I(tok)  TXrefTypeFlag_##tok = (1 << TXrefType_##tok),
TX_REF_TYPE_SYMBOLS_LIST
#undef I
}
TXrefTypeFlag;
#define TXrefTypeFlagPN         ((TXrefTypeFlag *)NULL)

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#define TX_REF_FLAG_SYMBOLS \
  I(dynamic)

#define TX_REF_FLAG(idx)        ((TXrefFlag)1 << (idx))
typedef enum TXrefFlagIter_tag
{
  TXrefFlagIter_unknown = -1,
#undef I
#define I(tok)  TXrefFlagIter_##tok,
TX_REF_FLAG_SYMBOLS
#undef I
  TXrefFlagIter_NUM
}
TXrefFlagIter;
#define TXrefFlagIterPN ((TXrefFlagIter *)NULL)

typedef enum TXrefFlag_tag
{
  TXrefFlag_ALL = -1,
#undef I
#define I(tok)  TXrefFlag_##tok = (1 << TXrefFlagIter_##tok),
TX_REF_FLAG_SYMBOLS
#undef I
}
TXrefFlag;
#define TXrefFlagPN     ((TXrefFlag *)NULL)

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Info about a ref (link, image, frame, iframe, etc.): */
typedef struct TXrefInfo_tag    TXrefInfo;
#define TXrefInfoPN     ((TXrefInfo *)NULL)

/* Fields in TXrefInfo that are alloc'd strings.
 * C/SQL access functions are auto-generated and made live from this list.
 * NOTE: update VORTEX_OBJ_BASE_VERSION if this list changes,
 * as TXdbfldfucns[] changes, so compiled SQL expressions change:
 */
#define TX_REFINFO_STRING_SYMBOLS       \
I(Url)                                  \
I(StrBaseUrl)                           \
I(LinkText)                             \
I(Description)

/* Fields in TXrefInfo that are EPI_SSIZE_T.
 * C/SQL access functions are auto-generated and made live from this list.
 * NOTE: update VORTEX_OBJ_BASE_VERSION if this list changes,
 * as TXdbfldfucns[] changes, so compiled SQL expressions change:
 */
#define TX_REFINFO_SSIZE_T_SYMBOLS      \
I(RawDocOffset)                         \
I(RawDocLength)                         \
I(ProcessedDocOffset)                   \
I(ProcessedDocLength)                   \
I(TextOffset)                           \
I(TextLength)

const char *TXrefTypeStr(TXrefType type, const char *ifUnknown);
TXrefType   TXrefTypeVal(const char *s, size_t n);
const char *TXrefFlagStr(TXrefFlag flag, const char *ifUnknown);
TXrefFlag   TXrefFlagVal(const char *s, size_t n);

#endif /* !TX_REFINFO_H */
