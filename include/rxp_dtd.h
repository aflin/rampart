#ifndef DTD_H
#define DTD_H

#ifdef NEVER_MAW
#include "rxp_charset.h"
#include "rxp_util.h"
#endif

/* Typedefs */

typedef struct dtd *Dtd;

typedef struct entity *Entity;

typedef struct element_definition *ElementDefinition;
typedef struct content_particle *ContentParticle;
typedef struct fsm *FSM;
typedef struct fsm_edge *FSMEdge;
typedef struct fsm_node *FSMNode;

typedef struct attribute_definition *AttributeDefinition;

typedef struct notation_definition *NotationDefinition;

/* DTDs */

struct dtd {
    CONST Char *name;		/* The doctype name */
    Entity internal_part, external_part;
    Entity entities;
    Entity parameter_entities;
    Entity predefined_entities;
#ifdef FOR_LT
    NSL_Doctype_I *doctype;
#endif
    ElementDefinition *elements;
    int nelements, neltalloc;
    NotationDefinition notations;
};

/* Entities */

enum entity_type {ET_external, ET_internal};
typedef enum entity_type EntityType;

enum markup_language {ML_xml, ML_nsl, ML_unspecified};
typedef enum markup_language MarkupLanguage;

enum standalone_declaration {
    /* NB must match NSL's rmdCode */
    SDD_unspecified, SDD_no, SDD_yes, SDD_enum_count
};
typedef enum standalone_declaration StandaloneDeclaration;

extern CONST char8 *StandaloneDeclarationName[SDD_enum_count];


struct entity {
    /* All entities */

    CONST Char *name;		/* The name in the entity declaration */
    EntityType type;		/* ET_external or ET_internal */
    CONST char8 *base_url;	/* If different from expected */
    struct entity *next;	/* For chaining a document's entity defns */
    CharacterEncoding encoding;	/* The character encoding of the entity */
    Entity parent;		/* The entity in which it is defined */
    CONST char8 *url;		/* URL of entity */

    /* Internal entities */

    CONST Char *text;		/* Text of the entity */
    int line_offset;		/* Line offset of definition */
    int line1_char_offset;	/* Char offset on first line */ 
    int matches_parent_text;	/* False if might contain expanded PEs */
    int dont_free_text;         /* nonzero: don't free text  KNG 000524 */

    /* External entities */

    CONST char8 *systemid;	/* Declared public ID */
    CONST char8 *publicid;	/* Declared public ID */
    NotationDefinition notation; /* Binary entity's declared notation */
    MarkupLanguage ml_decl;	/* XML, NSL or not specified */
    CONST char8 *version_decl;	/* XML declarations found in entity, if any  */
    CharacterEncoding encoding_decl;
    StandaloneDeclaration standalone_decl;
    CONST char8 *ddb_filename;	/* filename in NSL declaration */
};

/* Elements */

#ifndef FOR_LT			/* This is also declared in nsl.h */

enum cp_type {
    CP_pcdata, CP_name, CP_seq, CP_choice, CP_enum_count
};
typedef enum cp_type CPType;

struct content_particle {
    enum cp_type type;
    char repetition;
    CONST Char *name;
    ElementDefinition element;
    int nchildren;
    struct content_particle **children;
};

#endif

extern XML_API CONST char8 *ContentParticleTypeName[CP_enum_count];


struct fsm {
    Vector(FSMNode, nodes);
    FSMNode start_node;
};

struct fsm_edge {
    void *label;
    struct fsm_node *source, *destination;
    int id;
};

struct fsm_node {
    FSM fsm;
    int mark;
    int end_node;
    int id;
    Vector(FSMEdge, edges);
};

enum content_type {
    /* NB this must match NSL's ctVals */
    CT_mixed, CT_any, CT_bogus1, CT_bogus2, CT_empty, CT_element, CT_enum_count
};
typedef enum content_type ContentType;

extern XML_API CONST char8 *ContentTypeName[CT_enum_count];

struct element_definition {
#ifdef FOR_LT
    NSL_Doctype_I *doctype;
    NSL_ElementSummary_I *eltsum;
#endif
    CONST Char *name;		/* The element name */
    int namelen;
    int tentative;
    ContentType type;		/* The declared content */
    Char *content;		/* Element content */
    struct content_particle *particle;
    FSM fsm;
    AttributeDefinition *attributes;
    int nattributes, nattralloc;
    AttributeDefinition id_attribute; /* ID attribute, if it has one */
    int eltnum;
};

/* Attributes */

enum default_type {
    /* NB this must match NSL's NSL_ADefType */
    DT_required, DT_bogus1, DT_implied, 
    DT_bogus2, DT_none, DT_fixed, DT_enum_count
};
typedef enum default_type DefaultType;

extern XML_API CONST char8 *DefaultTypeName[DT_enum_count];

enum attribute_type {
    /* NB this must match NSL's NSL_Attr_Dec_Value */
    AT_cdata, AT_bogus1, AT_bogus2, AT_nmtoken, AT_bogus3, AT_entity,
    AT_idref, AT_bogus4, AT_bogus5, AT_nmtokens, AT_bogus6, AT_entities,
    AT_idrefs, AT_id, AT_notation, AT_enumeration, AT_enum_count
};
typedef enum attribute_type AttributeType;

extern XML_API CONST char8 *AttributeTypeName[AT_enum_count];

struct attribute_definition {
#ifdef FOR_LT
    NSL_AttributeSummary attrsum;
#endif
    CONST Char *name;		/* The attribute name */
    int namelen;
    AttributeType type;		/* The declared type */
    Char **allowed_values;	/* List of allowed values, argv style */
    DefaultType default_type;	/* The type of the declared default */
    CONST Char *default_value;	/* The declared default value */
    int attrnum;
};

/* Notations */

struct notation_definition {
    CONST Char *name;		/* The notation name */
    int tentative;
    CONST char8 *systemid;	/* System identifier */
    CONST char8 *publicid;	/* Public identifier */
    struct notation_definition *next;
};

/* Public functions */

XML_API Dtd NewDtd ARGS((void));
XML_API void FreeDtd ARGS((Dtd dtd));

XML_API Entity NewExternalEntity ARGS((CONST Char *name,
			  CONST char8 *publicid, CONST char8 *systemid,
			  NotationDefinition notation,
			  Entity parent));
XML_API Entity NewExternalEntityN ARGS((CONST Char *name, int namelen,
			  CONST char8 *publicid, CONST char8 *systemid,
			  NotationDefinition notation,
			  Entity parent));
XML_API Entity NewInternalEntityN ARGS((CONST Char *name, int namelen,
			  CONST Char *text, Entity parent,
			  int line_offset, int line1_char_offset, 
			  int matches_parent_text));
XML_API void FreeEntity ARGS((Entity e));

XML_API CONST char8 *EntityURL ARGS((Entity e));
XML_API CONST char8 *EntityDescription ARGS((Entity e));
XML_API void EntitySetBaseURL ARGS((Entity e, CONST char8 *url));
XML_API CONST char8 *EntityBaseURL ARGS((Entity e));

XML_API Entity DefineEntity ARGS((Dtd dtd, Entity entity, int pe));
XML_API Entity FindEntityN ARGS((Dtd dtd, CONST Char *name, int namelen, int pe));
XML_API Entity NextEntity ARGS((Dtd dtd, Entity previous));

#define NewInternalEntity(name, test, parent, l, l1, mat) \
    NewInternalEntityN(name, name ? Strlen(name) : 0, test, parent, l, l1, mat)
#define FindEntity(dtd, name, pe) FindEntityN(dtd, name, Strlen(name), pe)

XML_API ElementDefinition DefineElementN ARGS((Dtd dtd, CONST Char *name, int namelen,
				 ContentType type, Char *content,
					 ContentParticle particle));
XML_API ElementDefinition TentativelyDefineElementN ARGS((Dtd dtd, 
					    CONST Char *name, int namelen));
XML_API ElementDefinition RedefineElement ARGS((ElementDefinition e, ContentType type,
				  Char *content, ContentParticle particle));
XML_API ElementDefinition FindElementN ARGS((Dtd dtd, CONST Char *name, int namelen));
XML_API ElementDefinition NextElementDefinition ARGS((Dtd dtd, ElementDefinition previous));
XML_API void FreeElementDefinition ARGS((ElementDefinition e));
XML_API void FreeContentParticle ARGS((ContentParticle cp));

#define DefineElement(dtd, name, type, content, particle) \
    DefineElementN(dtd, name, Strlen(name), type, content, particle)
#define TentativelyDefineElement(dtd, name) \
    TentativelyDefineElementN(dtd, name, Strlen(name))
#define FindElement(dtd, name) FindElementN(dtd, name, Strlen(name))

XML_API AttributeDefinition DefineAttributeN ARGS((ElementDefinition element,
				     CONST Char *name, int namelen,
				     AttributeType type, Char **allowed_values,
				     DefaultType default_type, 
				     CONST Char *default_value));
XML_API AttributeDefinition FindAttributeN ARGS((ElementDefinition element,
				   CONST Char *name, int namelen));
XML_API AttributeDefinition NextAttributeDefinition ARGS((ElementDefinition element,
					    AttributeDefinition previous));
XML_API void FreeAttributeDefinition ARGS((AttributeDefinition a));

#define DefineAttribute(element, name, type, all, dt, dv) \
    DefineAttributeN(element, name, Strlen(name), type, all, dt, dv)
#define FindAttribute(element, name) \
    FindAttributeN(element, name, Strlen(name))

XML_API NotationDefinition DefineNotationN ARGS((Dtd dtd, CONST Char *name, int namelen,
				 CONST char8 *publicid, CONST char8 *systemid));
XML_API NotationDefinition TentativelyDefineNotationN ARGS((Dtd dtd,
					      CONST Char *name, int namelen));
XML_API NotationDefinition RedefineNotation ARGS((NotationDefinition n,
				 CONST char8 *publicid, CONST char8 *systemid));
XML_API NotationDefinition FindNotationN ARGS((Dtd dtd, CONST Char *name, int namelen));
XML_API void FreeNotationDefinition ARGS((NotationDefinition n));

#define DefineNotation(dtd, name, pub, sys) \
    DefineNotationN(dtd, name, Strlen(name), pub, sys)
#define TentativelyDefineNotation(dtd, name) \
    TentativelyDefineNotationN(dtd, name, Strlen(name))
#define FindNotation(dtd, name) FindNotationN(dtd, name, Strlen(name))

#endif /* DTD_H */
