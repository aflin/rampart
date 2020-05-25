#ifndef XMLPARSER_H
#define XMLPARSER_H

#ifdef NEVER_MAW
#include "rxp_dtd.h"
#include "rxp_input.h"
#include "rxp_util.h"

#ifdef FOR_LT
#include "lt-hash.h"
typedef HashTab *HashTable;
#else
#include "rxp_hash.h"
#endif
#endif

/* Typedefs */

typedef struct parser_state *Parser;
typedef struct attribute *Attribute;
typedef struct xbit *XBit;
typedef void CallbackProc ARGS((XBit bit, void *arg));
typedef InputSource EntityOpenerProc ARGS((Entity e, void *arg));

/* Bits */

enum xbit_type {
    XBIT_dtd,
    XBIT_start, XBIT_empty, XBIT_end, XBIT_eof, XBIT_pcdata,
    XBIT_pi, XBIT_comment, XBIT_cdsect,
    XBIT_error, XBIT_warning, XBIT_none,
    XBIT_enum_count
};
typedef enum xbit_type XBitType;

extern XML_API CONST char8 *XBitTypeName[XBIT_enum_count];

struct attribute {
    AttributeDefinition definition; /* The definition of this attribute */
    Char *value;		/* The (possibly normalised) value */
    int quoted;			/* Was it quoted? */
    struct attribute *next;	/* The next attribute or null */
};

struct xbit {
    Entity entity;
    int byte_offset;
    enum xbit_type type;
    char8 *s1, *s2;
    Char *S1, *S2;
    int i1, i2;
    Attribute attributes;
    ElementDefinition element_definition;
#ifndef FOR_LT
    int nchildren;
    struct xbit *parent;
    struct xbit **children;
#endif
};

#define pcdata_chars S1

#define pi_name S1
#define pi_chars S2

#define comment_chars S1

#define cdsect_chars S1

#define error_message s1

/* Parser flags */

enum parser_flag {
    ExpandCharacterEntities,
    ExpandGeneralEntities,
    XMLPiEnd,
    XMLEmptyTagEnd,
    XMLPredefinedEntities,
    ErrorOnUnquotedAttributeValues,
    NormaliseAttributeValues,
    NormalizeAttributeValues,
    ErrorOnBadCharacterEntities,
    ErrorOnUndefinedEntities,
    ReturnComments,
    CaseInsensitive,
    ErrorOnUndefinedElements,
#if 0
    WarnOnUndefinedElements,
#endif
    ErrorOnUndefinedAttributes,
#if 0
    WarnOnUndefinedAttributes,
#endif
    WarnOnRedefinitions,
    TrustSDD,
    XMLExternalIDs,
    ReturnDefaultedAttributes,
    MergePCData,
    XMLMiscWFErrors,
    XMLStrictWFErrors,
    AllowMultipleElements,
    CheckEndTagsMatch,
    IgnoreEntities,
    XMLLessThan,
    IgnorePlacementErrors,
    Validate,
    ErrorOnValidityErrors
};
typedef enum parser_flag ParserFlag;

/* Parser */

enum parse_state 
    {PS_prolog1, PS_prolog2, PS_validate_dtd, 
     PS_body, PS_validate_final, PS_epilog, PS_end, PS_error};

struct element_info {
    ElementDefinition definition;
    Entity entity;
    FSMNode context;
};

struct parser_state {
    enum parse_state state;
    Entity document_entity;
    int have_dtd;		/* True if dtd has been processed */
    StandaloneDeclaration standalone;
    struct input_source *source;
    Char *name, *pbuf, *save_pbuf;
    int namelen, pbufsize, pbufnext, save_pbufsize, save_pbufnext;
    struct xbit xbit;
    int peeked;
    Dtd dtd;			/* The document's DTD */
    CallbackProc *dtd_callback;
    CallbackProc *warning_callback;
    EntityOpenerProc *entity_opener;
    unsigned int flags;
    struct element_info *element_stack;
    int element_stack_alloc;
    int element_depth;
    void *callback_arg;
    int external_pe_depth;	/* To keep track of whether we're in the */
				/* internal subset: 0 <=> yes */
    HashTable id_table;
};

XML_API int init_parser ARGS((void));
XML_API Parser NewParser ARGS((void));
XML_API void FreeParser ARGS((Parser p));

XML_API Entity ParserRootEntity ARGS((Parser p));
XML_API InputSource ParserRootSource ARGS((Parser p));

XML_API XBit ReadXBit ARGS((Parser p));
XML_API XBit PeekXBit ARGS((Parser p));
XML_API void FreeXBit ARGS((XBit xbit));

#ifndef FOR_LT
XBit ReadXTree ARGS((Parser p));
void FreeXTree ARGS((XBit tree));
#endif

XML_API XBit ParseDtd ARGS((Parser p, Entity e));

XML_API void ParserSetWarningCallback ARGS((Parser p, CallbackProc cb));
XML_API void ParserSetDtdCallback ARGS((Parser p, CallbackProc cb));
XML_API void ParserSetEntityOpener ARGS((Parser p, EntityOpenerProc opener));
XML_API void ParserSetCallbackArg ARGS((Parser p, void *arg));

XML_API int ParserPush ARGS((Parser p, InputSource source));
XML_API void ParserPop ARGS((Parser p));

XML_API void ParserSetFlag ARGS((Parser p,  ParserFlag flag, int value));
#define ParserGetFlag(p, flag) ((p)->flags & (1 << (flag)))

XML_API void ParserPerror ARGS((Parser p, XBit bit));

#endif /* XMLPARSER_H */
