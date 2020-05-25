/* prototypes for xmlapi.c, the SQL-based LIBXML2 API functions */
#include <libxml/tree.h>

int TXfunc_xmlReaderGetAllContent(FLD *fld);
int TXfunc_xmlReaderGetAttribute(FLD *fld, FLD *fld_name, FLD *fld_ns_URI);
int TXfunc_xmlReaderGetAttributeCount(FLD *fld);
int TXfunc_xmlReaderGetAttributeNumber(FLD *fld, FLD *fld_number);
int TXfunc_xmlReaderGetBytesConsumed(FLD *fld);
int TXfunc_xmlReaderGetColumn(FLD *fld);
int TXfunc_xmlReaderGetContent(FLD *fld);
int TXfunc_xmlReaderGetDepth(FLD *fld);
int TXfunc_xmlReaderGetEncoding(FLD *fld);
int TXfunc_xmlReaderGetLang(FLD *fld);
int TXfunc_xmlReaderGetLine(FLD *fld);
int TXfunc_xmlReaderGetLocalName(FLD *fld);
int TXfunc_xmlReaderGetName(FLD *fld);
int TXfunc_xmlReaderGetNsPrefix(FLD *fld);
int TXfunc_xmlReaderGetNsURI(FLD *fld);
int TXfunc_xmlReaderGetType(FLD *fld);
int TXfunc_xmlReaderGetVersion(FLD *fld);

int TXfunc_xmlReaderIsEmptyElement(FLD *fld);

int TXfunc_xmlReaderMoveToAttribute(FLD *fld, FLD *fld_name, FLD *fld_URI);
int TXfunc_xmlReaderMoveToAttributeNumber(FLD *fld, FLD *fld_number);
int TXfunc_xmlReaderMoveToFirstAttribute(FLD *fld);
int TXfunc_xmlReaderMoveToNextAttribute(FLD *fld);
int TXfunc_xmlReaderMoveToElement(FLD *fld);

int TXfunc_xmlReaderNewFromFile(FLD *fld, FLD *fld_opt1, FLD *fld_opt2);
int TXfunc_xmlReaderNewFromString(FLD *fld, FLD *fld_opt1, FLD *fld_opt2);

int TXfunc_xmlReaderRead(FLD *fld);

int TXfunc_xmlTreeAddChild(FLD *fld, FLD *child);
int TXfunc_xmlTreeAddChildList(FLD *fld, FLD *fld_child);
int TXfunc_xmlTreeAddContent(FLD *fld, FLD *fld_content);
int TXfunc_xmlTreeAddNextSibling(FLD *fld, FLD *child);
int TXfunc_xmlTreeAddPrevSibling(FLD *fld, FLD *child);
int TXfunc_xmlTreeAddSibling(FLD *fld, FLD *child);

int TXfunc_xmlTreeCleanup(FLD *fld, FLD *fld_options);

int TXfunc_xmlTreeClearNs(FLD *fld);

int TXfunc_xmlTreeCopyAttribute(FLD *fld, FLD *fld_attr);
int TXfunc_xmlTreeCopyAttributeList(FLD *fld, FLD *fld_attr);
int TXfunc_xmlTreeCopyDoc(FLD *fld);
int TXfunc_xmlTreeCopyNode(FLD *fld, FLD *fld_node, FLD *fld_recursive);
int TXfunc_xmlTreeCopyNodeList(FLD *fld, FLD *fld_node);

int TXfunc_xmlTreeDumpNode(FLD *fld, FLD *fld_opt1);

int TXfunc_xmlTreeGetAllContent(FLD *fld);
int TXfunc_xmlTreeGetAttributeContent(FLD *fld, FLD *fld_name, FLD *fld_URI);
int TXfunc_xmlTreeGetAttributes(FLD *fld, FLD *fld_name, FLD *fld_ns);
int TXfunc_xmlTreeGetChildren(FLD *fld, FLD *fld_name, FLD *fld_URI);
int TXfunc_xmlTreeGetChildrenContent(FLD *fld, FLD *fld_name, FLD *fld_URI);
int TXfunc_xmlTreeGetContent(FLD *fld, FLD *fld_opt);
int TXfunc_xmlTreeGetDoc(FLD *fld);
int TXfunc_xmlTreeGetEncoding(FLD *fld);
int TXfunc_xmlTreeGetEntityType(FLD *fld);
int TXfunc_xmlTreeGetExternalID(FLD *fld);
int TXfunc_xmlTreeGetExternalSubset(FLD *fld);
int TXfunc_xmlTreeGetFirstAttribute(FLD *fld);
int TXfunc_xmlTreeGetFirstChild(FLD *fld);
int TXfunc_xmlTreeGetInternalSubset(FLD *fld);
int TXfunc_xmlTreeGetLine(FLD *fld);
int TXfunc_xmlTreeGetName(FLD *fld);
int TXfunc_xmlTreeGetNext(FLD *fld);
int TXfunc_xmlTreeGetNs(FLD *fld);
int TXfunc_xmlTreeGetNsDef(FLD *fld);
int TXfunc_xmlTreeGetNsURI(FLD *fld);
int TXfunc_xmlTreeGetNsPrefix(FLD *fld);
int TXfunc_xmlTreeGetParent(FLD *fld);
int TXfunc_xmlTreeGetPrevious(FLD *fld);
int TXfunc_xmlTreeGetRootElement(FLD *fld);
int TXfunc_xmlTreeGetSystemID(FLD *fld);
int TXfunc_xmlTreeGetType(FLD *fld);
int TXfunc_xmlTreeGetVersion(FLD *fld);

int TXfunc_xmlTreeIsBlankNode(FLD *fld);

int TXfunc_xmlTreeLookupNsURI(FLD *fld, FLD *fld_URI);
int TXfunc_xmlTreeLookupNsPrefix(FLD *fld, FLD *fld_prefix);

int TXfunc_xmlTreeNewAttribute(FLD *fld, FLD *fld_name, FLD *fld_content, FLD *fld_ns);
int TXfunc_xmlTreeNewCDATA(FLD *fld, FLD *fld_data);
int TXfunc_xmlTreeNewComment(FLD *fld, FLD *fld_comment);
int TXfunc_xmlTreeNewDoc(FLD *fld_version);
int TXfunc_xmlTreeNewDocFromFile(FLD *fld_filename, FLD *fld_opt1, FLD *fld_opt2);
int TXfunc_xmlTreeNewDocFromString(FLD *fld_filename, FLD *fld_opt1, FLD *fld_opt2);
int TXfunc_xmlTreeNewElement(FLD *fld, FLD *fld_name, FLD *fld_content, FLD *fld_ns);
int TXfunc_xmlTreeNewNs(FLD *fld, FLD *fld_prefix, FLD *fld_URI);
int TXfunc_xmlTreeNewPI(FLD *fld, FLD *fld_name, FLD *fld_content);
int TXfunc_xmlTreeNewText(FLD *fld, FLD *fld_text);
int TXfunc_xmlTreeNewXPath(FLD *fld);

int TXfunc_xmlTreeQuickXPath(FLD *fld, FLD *fld_xpath, FLD *fld_xmlns);

int TXfunc_xmlTreePrintDoc(FLD *fld, FLD *fld_opt1, FLD *fld_opt2);
int TXfunc_xmlTreeSaveDoc(FLD *fld_doc, FLD *fld_filename, FLD *fld_opt1, FLD *fld_opt2);

int TXfunc_xmlTreeSetContent(FLD *fld, FLD *fld_content);
int TXfunc_xmlTreeSetName(FLD *fld, FLD *fld_name);
int TXfunc_xmlTreeSetNs(FLD *fld, FLD *fld_ns);
int TXfunc_xmlTreeSetNsPrefix(FLD *fld, FLD *fld_prefix);
int TXfunc_xmlTreeSetNsURI(FLD *fld, FLD *fld_URI);
int TXfunc_xmlTreeSetRootElement(FLD *fld, FLD *fld_root);

int TXfunc_xmlTreeUnlinkNode(FLD *fld);

int TXfunc_xmlTreeXPathExecute(FLD *fld, FLD *fld_query);
int TXfunc_xmlTreeXPathRegisterNs(FLD *fld, FLD *fld_prefix, FLD *fld_URI);
int TXfunc_xmlTreeXPathSetContext(FLD *fld, FLD *fld_node);

int TXfunc_xmlWriterEndAttribute(FLD *fld);
int TXfunc_xmlWriterEndCDATA(FLD *fld);
int TXfunc_xmlWriterEndComment(FLD *fld);
int TXfunc_xmlWriterEndDocument(FLD *fld);
int TXfunc_xmlWriterEndElement(FLD *fld);
int TXfunc_xmlWriterEndPI(FLD *fld);

int TXfunc_xmlWriterGetContent(FLD *fld);

int TXfunc_xmlWriterNewToFile(FLD *fld);
int TXfunc_xmlWriterNewToString(FLD *fld);

int TXfunc_xmlWriterSetIndent(FLD *fld, FLD *fld_indent, FLD *fld_indentString);

int TXfunc_xmlWriterStartAttribute(FLD *fld, FLD *fld_name, FLD *fld_prefix, FLD *fld_URI);
int TXfunc_xmlWriterStartCDATA(FLD *fld);
int TXfunc_xmlWriterStartComment(FLD *fld);
int TXfunc_xmlWriterStartDocument(FLD *fld, FLD *fld_version, FLD *fld_encoding, FLD *fld_standalone);
int TXfunc_xmlWriterStartElement(FLD *fld, FLD *fld_name, FLD *fld_prefix, FLD *fld_URI);
int TXfunc_xmlWriterStartPI(FLD *fld, FLD *fld_target);

int TXfunc_xmlWriterWriteAttribute(FLD *fld, FLD *fld_name, FLD *fld_content, FLD *fld_prefix, FLD *fld_URI);
int TXfunc_xmlWriterWriteCDATA(FLD *fld, FLD *fld_content);
int TXfunc_xmlWriterWriteComment(FLD *fld, FLD *fld_content);
int TXfunc_xmlWriterWriteElement(FLD *fld, FLD *fld_name, FLD *fld_content, FLD *fld_prefix, FLD *fld_URI);
int TXfunc_xmlWriterWritePI(FLD *fld, FLD *fld_target, FLD *fld_content);
int TXfunc_xmlWriterWriteRaw(FLD *fld, FLD *fld_content);
int TXfunc_xmlWriterWrite(FLD *fld, FLD *fld_content);


int xmlTxAddOrphan(xmlNodePtr node);
int xmlTxRmOrphan(xmlNodePtr node);
int xmlTxWalkRef(xmlNodePtr node);

void *xmlTxGetFld(const char *fn,FLD *fld, int type, FTI ft_type,
                  int allowNull, ft_strlst *strlstHdr);
int xmlTxSetFld(const char *fn, FLD *fld, void *obj, FTN type, FTI fti_type,
                int elsz, int n);
