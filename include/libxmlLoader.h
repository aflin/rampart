#ifndef LIBXML_H
#define LIBXML_H

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlreader.h>
#include <libxml/xmlwriter.h>
#include <libxml/xmlversion.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h> /* xmlXPathRegisterNS in here, dunno why */
#include <libxslt/transform.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* libxml prototypes, for when we dynamically load libxml2.dll */

/* call xmlCheckVersion to make sure this matches up */
/* "LIBXML_VERSION" is used by libxml itself */
#define TX_LIBXML_VERSION 20623

#ifdef _WIN32
#  define LIBXML_EXTERN_API(__type)  extern _declspec(dllexport) CDECL __type
#  define LIBXML_EXPORT_API(__type)         _declspec(dllexport) CDECL __type
#  define LIBXML_EXTERN_DATA(__type) extern _declspec(dllexport)       __type
#  define LIBXML_EXPORT_DATA(__type)        _declspec(dllexport)       __type
#else /* !_WIN32 */
#  define LIBXML_EXTERN_API(__type)  extern                            __type
#  define LIBXML_EXPORT_API(__type)                                    __type
#  define LIBXML_EXTERN_DATA(__type) extern                            __type
#  define LIBXML_EXPORT_DATA(__type)                                   __type
#endif /* !_WIN32 */

/*
*/

#define LIBXMLSYMBOLS_LIST                                        \
I(xmlTextReaderPtr,     xmlReaderForFile,       (const char *path, const char *encoding, int options)) \
I(xmlTextReaderPtr,     xmlReaderForMemory,     (const char * buffer, int size, const char * URL, const char * encoding, int options)) \
I(const char *,         xmlTextReaderConstEncoding,             (xmlTextReaderPtr reader)) \
I(const char *,         xmlTextReaderConstXmlLang,              (xmlTextReaderPtr reader)) \
I(const char *,         xmlTextReaderConstXmlVersion,           (xmlTextReaderPtr reader)) \
I(int,                  xmlTextReaderRead,                      (xmlTextReaderPtr reader)) \
I(xmlNodePtr,           xmlTextReaderExpand,                    (xmlTextReaderPtr reader)) \
I(int,                  xmlTextReaderNodeType,                  (xmlTextReaderPtr reader)) \
I(const char *,         xmlTextReaderConstName,                 (xmlTextReaderPtr reader)) \
I(const char *,         xmlTextReaderConstLocalName,            (xmlTextReaderPtr reader)) \
I(const char *,         xmlTextReaderConstValue,                (xmlTextReaderPtr reader)) \
I(char *,               xmlTextReaderValue,                     (xmlTextReaderPtr reader)) \
I(int,                  xmlTextReaderIsEmptyElement,            (xmlTextReaderPtr reader)) \
I(int,                  xmlTextReaderAttributeCount,            (xmlTextReaderPtr reader)) \
I(char *,               xmlTextReaderGetAttribute,              (xmlTextReaderPtr reader, const char *name)) \
I(char *,               xmlTextReaderGetAttributeNs,            (xmlTextReaderPtr reader, const char *name, const char *href)) \
I(char *,               xmlTextReaderGetAttributeNo,            (xmlTextReaderPtr reader, int number)) \
I(int,                  xmlTextReaderMoveToAttribute,           (xmlTextReaderPtr reader, const char *name)) \
I(int,                  xmlTextReaderMoveToAttributeNs,         (xmlTextReaderPtr reader, const char *name, const char *href)) \
I(int,                  xmlTextReaderMoveToAttributeNo,         (xmlTextReaderPtr reader, int number)) \
I(int,                  xmlTextReaderMoveToFirstAttribute,      (xmlTextReaderPtr reader)) \
I(int,                  xmlTextReaderMoveToNextAttribute,       (xmlTextReaderPtr reader)) \
I(int,                  xmlTextReaderMoveToElement,             (xmlTextReaderPtr reader)) \
I(const char *,         xmlTextReaderConstNamespaceUri,         (xmlTextReaderPtr reader)) \
I(const char *,         xmlTextReaderConstPrefix,               (xmlTextReaderPtr reader)) \
I(int,                  xmlTextReaderGetParserColumnNumber,     (xmlTextReaderPtr reader)) \
I(int,                  xmlTextReaderGetParserLineNumber,       (xmlTextReaderPtr reader)) \
I(int,                  xmlTextReaderByteConsumed,              (xmlTextReaderPtr reader)) \
I(int,                  xmlTextReaderDepth,                     (xmlTextReaderPtr reader)) \
I(void,                 xmlFreeTextReader,                      (xmlTextReaderPtr reader)) \
I(void,                 xmlCleanupParser,       (void)) \
I(xmlDocPtr,            xmlReadFile,            (const char *filename, const char *encoding, int options)) \
I(xmlDocPtr,            xmlReadMemory,          (const char *buffer, int size, const char *URL, const char *encoding, int options)) \
I(xmlDocPtr,            xmlNewDoc,              (const char* version)) \
I(xmlDocPtr,            xmlCopyDoc,             (xmlDocPtr doc, int recursive)) \
I(xmlNodePtr,           xmlDocGetRootElement,   (xmlDocPtr doc)) \
I(xmlNodePtr,           xmlDocSetRootElement,   (xmlDocPtr doc, xmlNodePtr root)) \
I(void,                 xmlDocDumpFormatMemory, (xmlDocPtr doc, char **mem, int *size, int format)) \
I(void,                 xmlDocDumpFormatMemoryEnc, (xmlDocPtr doc, char **mem, int *size, const char *encoding, int format)) \
I(int,                  xmlSaveFormatFileEnc,   (const char *filename, xmlDocPtr doc, const char *encoding, int format)) \
I(void,                 xmlFreeDoc,             (xmlDocPtr doc)) \
I(char *,               xmlGetNodePath,         (xmlNodePtr node)) \
I(xmlNodePtr,           xmlNewNode,             (xmlNsPtr ns, const char *name)) \
I(xmlNodePtr,           xmlNewDocRawNode,       (xmlDocPtr doc, xmlNsPtr ns, const char *name, const char *content)) \
I(xmlNodePtr,           xmlAddChild,            (xmlNodePtr parent, xmlNodePtr cur)) \
I(xmlNodePtr,           xmlAddChildList,        (xmlNodePtr parent, xmlNodePtr cur)) \
I(xmlNodePtr,           xmlAddPrevSibling,      (xmlNodePtr cur, xmlNodePtr elem)) \
I(xmlNodePtr,           xmlAddNextSibling,      (xmlNodePtr cur, xmlNodePtr elem)) \
I(xmlNodePtr,           xmlAddSibling,          (xmlNodePtr cur, xmlNodePtr elem)) \
I(xmlNodePtr,           xmlNewText,             (const char *content)) \
I(xmlNodePtr,           xmlNewChild,            (xmlNodePtr parent, xmlNsPtr ns, const char *name, const char *content)) \
I(xmlNodePtr,           xmlNewTextChild,        (xmlNodePtr parent, xmlNsPtr ns, const char *name, const char *content)) \
I(xmlNodePtr,           xmlDocCopyNode,         (const xmlNodePtr node, xmlDocPtr doc, int extended)) \
I(xmlNodePtr,           xmlDocCopyNodeList,     (xmlDocPtr doc, const xmlNodePtr node)) \
I(char *,               xmlEncodeEntitiesReentrant, (xmlDocPtr doc, const char *input)) \
I(char *,               xmlNodeListGetString,   (xmlDocPtr doc, xmlNodePtr list, int inlineArg)) \
I(char *,               xmlNodeGetContent,      (xmlNodePtr node)) \
I(int,                  xmlNodeDump,            (xmlBufferPtr buffer, xmlDocPtr doc, xmlNodePtr node, int level, int format)) \
I(void,                 xmlNodeAddContent,      (xmlNodePtr cur, const char *content)) \
I(void,                 xmlNodeSetContent,      (xmlNodePtr node, const char *content)) \
I(void,                 xmlNodeSetName,         (xmlNodePtr node, const char *name)) \
I(int,                  xmlIsBlankNode,         (xmlNodePtr node)) \
I(void,                 xmlUnlinkNode,          (xmlNodePtr node)) \
I(void,                 xmlFreeNode,            (xmlNodePtr node)) \
I(void,                 xmlFreeNodeList,        (xmlNodePtr node)) \
I(xmlNsPtr,             xmlNewNs,               (xmlNodePtr node, char *href, char *prefix)) \
I(xmlNsPtr,             xmlSearchNs,            (xmlDocPtr doc, xmlNodePtr node, const char *prefix)) \
I(xmlNsPtr,             xmlSearchNsByHref,      (xmlDocPtr doc, xmlNodePtr node, const char *href)) \
I(void,                 xmlSetNs,               (xmlNodePtr node, xmlNsPtr ns)) \
I(xmlNsPtr,             xmlCopyNamespace,       (xmlNsPtr ns)) \
I(void,                 xmlFreeNs,              (xmlNsPtr ns)) \
I(xmlXPathContextPtr,   xmlXPathNewContext,     (xmlDocPtr doc)) \
I(xmlXPathObjectPtr,    xmlXPathEval,           (const char *str, xmlXPathContextPtr ctxt)) \
I(int,                  xmlXPathRegisterNs,     (xmlXPathContextPtr ctxt, const char *prefix, const char *uri)) \
I(void,                 xmlXPathFreeContext,    (xmlXPathContextPtr ctxt)) \
I(void,                 xmlXPathFreeObject,     (xmlXPathObjectPtr obj)) \
I(xmlAttrPtr,           xmlNewProp,             (xmlNodePtr node, const char *name, const char *value)) \
I(xmlAttrPtr,           xmlNewDocProp,          (xmlDocPtr doc,   const char *name, const char *value)) \
I(xmlAttrPtr,           xmlHasProp,             (xmlNodePtr node, const char *name)) \
I(xmlAttrPtr,           xmlHasNsProp,           (xmlNodePtr node, const char *name, const char *href)) \
I(xmlAttrPtr,           xmlSetProp,             (xmlNodePtr node, const char *name, const char *value)) \
I(xmlAttrPtr,           xmlCopyProp,            (xmlNodePtr target, xmlAttrPtr cur)) \
I(xmlAttrPtr,           xmlCopyPropList,        (xmlNodePtr target, xmlAttrPtr cur)) \
I(char *,               xmlGetNsProp,           (xmlNodePtr node, const char *name, const char *href)) \
I(char *,               xmlGetProp,             (xmlNodePtr node, const char *name)) \
I(xmlDtdPtr,            xmlCreateIntSubset,     (xmlDocPtr doc, const char *name, const char *ExternalID, const char *SystemID)) \
I(xmlDtdPtr,            xmlParseDTD,            (const char *ExternalID, const char *SystemID)) \
I(xmlEntityPtr,         xmlAddDocEntity,        (xmlDocPtr doc, const char *name, int type, const char *ExternalID, const char *SystemID, const char *content)) \
I(xmlEntityPtr,         xmlAddDtdEntity,        (xmlDocPtr doc, const char *name, int type, const char *ExternalID, const char *SystemID, const char *content)) \
I(xmlNodePtr,           xmlNewCDataBlock,       (xmlDocPtr doc, const char *content, int len)) \
I(xmlNodePtr,           xmlNewDocPI,            (xmlDocPtr doc, const char *name, const char *content)) \
I(xmlNodePtr,           xmlNewDocComment,       (xmlDocPtr doc, const char *comment)) \
I(xmlDictPtr,           xmlDictCreate,          (void)) \
I(int,                  xmlDictOwns,            (xmlDictPtr dict, const char *str)) \
I(const char *,         xmlDictLookup,          (xmlDictPtr dict, const char *name, int len)) \
I(int,                  xmlGetLineNo,           (xmlNodePtr node)) \
I(xmlTextWriterPtr,     xmlNewTextWriterFilename,       (const char *uri, int compression)) \
I(xmlTextWriterPtr,     xmlNewTextWriterMemory,         (xmlBufferPtr buffer, int compression)) \
I(int,                  xmlTextWriterStartAttribute,    (xmlTextWriterPtr writer, const char *name)) \
I(int,                  xmlTextWriterStartAttributeNS,  (xmlTextWriterPtr writer, const char *prefix, const char *name, const char *href)) \
I(int,                  xmlTextWriterStartCDATA,        (xmlTextWriterPtr writer)) \
I(int,                  xmlTextWriterStartComment,      (xmlTextWriterPtr writer)) \
I(int,                  xmlTextWriterStartDocument,     (xmlTextWriterPtr writer, const char * version, const char * encoding, const char * standalone)) \
I(int,                  xmlTextWriterSetIndent,         (xmlTextWriterPtr writer, int indent)) \
I(int,                  xmlTextWriterSetIndentString,   (xmlTextWriterPtr writer, const char *str)) \
I(int,                  xmlTextWriterStartElement,      (xmlTextWriterPtr writer, const char * name)) \
I(int,                  xmlTextWriterStartElementNS,    (xmlTextWriterPtr writer, const char *prefix, const char * name, const char *href)) \
I(int,                  xmlTextWriterStartPI,           (xmlTextWriterPtr writer, const char *target)) \
I(int,                  xmlTextWriterWriteAttribute,    (xmlTextWriterPtr writer, const char *name, const char *content)) \
I(int,                  xmlTextWriterWriteAttributeNS,  (xmlTextWriterPtr writer, const char *prefix, const char *name, const char *href, const char *content)) \
I(int,                  xmlTextWriterWriteCDATA,        (xmlTextWriterPtr writer, const char *content)) \
I(int,                  xmlTextWriterWriteComment,      (xmlTextWriterPtr writer, const char *content)) \
I(int,                  xmlTextWriterWriteElement,      (xmlTextWriterPtr writer, const char *name, const char *content)) \
I(int,                  xmlTextWriterWriteElementNS,    (xmlTextWriterPtr writer, const char *prefix, const char *name, const char *href, const char *content)) \
I(int,                  xmlTextWriterWritePI,           (xmlTextWriterPtr writer, const char *target, const char *content)) \
I(int,                  xmlTextWriterWriteRaw,          (xmlTextWriterPtr writer, const char *content)) \
I(int,                  xmlTextWriterWriteString,       (xmlTextWriterPtr writer, const char *content)) \
I(int,                  xmlTextWriterEndAttribute,      (xmlTextWriterPtr writer)) \
I(int,                  xmlTextWriterEndCDATA,          (xmlTextWriterPtr writer)) \
I(int,                  xmlTextWriterEndComment,        (xmlTextWriterPtr writer)) \
I(int,                  xmlTextWriterEndDocument,       (xmlTextWriterPtr writer)) \
I(int,                  xmlTextWriterEndElement,        (xmlTextWriterPtr writer)) \
I(int,                  xmlTextWriterEndPI,             (xmlTextWriterPtr writer)) \
I(int,                  xmlTextWriterFlush,             (xmlTextWriterPtr writer)) \
I(void,                 xmlFreeTextWriter,              (xmlTextWriterPtr writer)) \
I(xmlBufferPtr,         xmlBufferCreate,        (void)) \
I(void,                 xmlBufferFree,          (xmlBufferPtr buffer)) \
I(char *,               xmlStrdup,              (char *str)) \
I(int,                  xmlMemGet,              (xmlFreeFunc * freeFunc, xmlMallocFunc * mallocFunc, xmlReallocFunc * reallocFunc, xmlStrdupFunc * strdupFunc)) \
I(int,                  xmlKeepBlanksDefault,   (int val)) \
I(void,                 xmlLineNumbersDefault,  (int val)) \
I(void,                 xmlSetStructuredErrorFunc, (void *ctxt, xmlStructuredErrorFunc handler)) \
I(void,                 xmlSetGenericErrorFunc, (void *ctxt, xmlGenericErrorFunc handler)) \
I(int,                  xmlGetLibxmlVersion,    (void)) \
I(void,                 xmlTxSetFuncs,          (xmlTxFuncs_type newFuncs))


typedef struct LIBXMLSYMBOLS_tag
{
#undef I
#define I(ret, func, args)      ret (CDECL *func) ARGS(args);
  LIBXMLSYMBOLS_LIST
#undef I
}
LIBXMLSYMBOLS;
#define LIBXMLSYMBOLSPN   ((LIBXMLSYMBOLS *)NULL)

static CONST char * CONST       libxmlSymbolNames[] =
{
#  undef I
#  define I(ret, func, args)    #func,
  LIBXMLSYMBOLS_LIST
#  undef I
};
#  define NUM_LIBXMLSYMBOLS (sizeof(libxmlSymbolNames)/sizeof(libxmlSymbolNames[0]))
int loadlibxmlsymbols(void);

/**
 * a linked list of nodes used by an xmlDoc to keep track of nodes
 * that have been unlinked.  Used for eventual freeing. */
typedef struct _xmlTxDocOrphan xmlTxDocOrphan;
struct _xmlTxDocOrphan
{
  xmlNodePtr node;
  xmlTxDocOrphan *next;
};

/**
 * the xmlDoc needs to keep track of nodes that have been unlinked,
 * as they still get allocated/freed along with the tree.
 * it also keeps track of a stylesheet that may have been used
 * to create this doc, as that is needed when outputting it.
 */
typedef struct _xmlTxDocPrivate xmlTxDocPrivate;
struct _xmlTxDocPrivate
{
  int refCount;
  xmlTxDocOrphan *next;
  xsltStylesheetPtr style;
};

/* our own xmlFree() wrapper due to odd linking isues */
void xmlTxFree(void *data);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LIBXML_H */
