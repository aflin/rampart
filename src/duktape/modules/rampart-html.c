/* Copyright (C) 2024  Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */


#include <stdio.h>
#include <string.h>
#include <tidy.h>
#include <tidybuffio.h>
//#include <forward.h>
//#include <tidy-int.h>
#include "rampart.h"

#include "tidy-int.h"
#include "clean.h"
#include "lexer.h"
#include "parser.h"
#include "attrs.h"
#include "message.h"
#include "tmbstr.h"
#include "utf8.h"

static void uniq_array_nodes(duk_context *ctx, duk_idx_t arr_idx);

duk_ret_t duk_rp_html_finalizer(duk_context *ctx)
{
    TidyBuffer *errbuf;
    TidyDoc tdoc;
    int i=0, len;
    Node* node;
    TidyDocImpl* doc;

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("errbuf"));
    errbuf=duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    if (errbuf->bp)
        tidyBufFree( errbuf );
    free(errbuf);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("tdoc"));
    tdoc=duk_get_pointer(ctx, -1);
    doc = tidyDocToImpl( tdoc );
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("dnodes"));
// if we need this, we have bigger problems
//    uniq_array_nodes(ctx, -1);
    len=duk_get_length(ctx, -1);

    for (i=0; i<len; i++)
    {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        node=(Node*)duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        TY_(FreeNode)( doc, node);
    }

    tidyRelease( tdoc );
    return 0;
}


int isBlockTag(TidyTagId id)
{
    switch(id)
    {
        case TidyTag_LI:
        case TidyTag_OL:
        case TidyTag_ADDRESS:
        case TidyTag_ARTICLE:
        case TidyTag_ASIDE:
        case TidyTag_BLOCKQUOTE:
        case TidyTag_CANVAS:
        case TidyTag_DETAILS:
        case TidyTag_DD:
        case TidyTag_DIALOG:
        case TidyTag_DIV:
        case TidyTag_DL:
        case TidyTag_DT:
        case TidyTag_FIELDSET:
        case TidyTag_FIGCAPTION:
        case TidyTag_FIGURE:
        case TidyTag_FOOTER:
        case TidyTag_FORM:
        case TidyTag_H1:
        case TidyTag_H2:
        case TidyTag_H3:
        case TidyTag_H4:
        case TidyTag_H5:
        case TidyTag_H6:
        case TidyTag_HEADER:
        case TidyTag_HGROUP:
        case TidyTag_HR:
        case TidyTag_MAIN:
        case TidyTag_NAV:
        case TidyTag_P:
        case TidyTag_SECTION:
        case TidyTag_TABLE:
        case TidyTag_TFOOT:
        case TidyTag_TR:
        case TidyTag_UL:
        case TidyTag_VIDEO:
        case TidyTag_BR:
        case TidyTag_PRE:
        case TidyTag_NOSCRIPT:
        case TidyTag_TBODY:
        case TidyTag_TD:
        case TidyTag_TEXTAREA:
        case TidyTag_TH:
        case TidyTag_THEAD:
        case TidyTag_TITLE:
            return 1;
        default:
            return 0;
    }
}

// These get an extra \n
int isSectionTag(TidyTagId id)
{
    switch(id)
    {
        case TidyTag_ARTICLE:
        case TidyTag_ASIDE:
        case TidyTag_BLOCKQUOTE:
        case TidyTag_FOOTER:
        case TidyTag_H1:
        case TidyTag_H2:
        case TidyTag_H3:
        case TidyTag_H4:
        case TidyTag_H5:
        case TidyTag_H6:
        case TidyTag_HR:
        case TidyTag_MAIN:
        case TidyTag_NAV:
        case TidyTag_P:
        case TidyTag_UL:
        case TidyTag_OL:
        case TidyTag_DL:
        case TidyTag_SECTION:
        case TidyTag_TABLE:
        case TidyTag_VIDEO:
        case TidyTag_PRE:
        case TidyTag_TEXTAREA:
        case TidyTag_TITLE:
            return 1;
        default:
            return 0;
    }
}

int isSingletonTag(TidyTagId id)
{
    switch(id)
    {
        case TidyTag_AREA:
        case TidyTag_BASE:
        case TidyTag_PARAM:
        case TidyTag_TRACK:
        case TidyTag_WBR:
        case TidyTag_BR:
        case TidyTag_COL:
        case TidyTag_INPUT:
        case TidyTag_KEYGEN:
        case TidyTag_LINK:
        case TidyTag_META:
            return 1;
        default:
            return 0;
    }
}


/* other non-block tags for reference:
TidyTag_UNKNOWN TidyTag_A TidyTag_ABBR TidyTag_ACRONYM TidyTag_ALIGN TidyTag_APPLET TidyTag_AREA TidyTag_AUDIO
TidyTag_B TidyTag_BASE TidyTag_BASEFONT TidyTag_BDO TidyTag_BGSOUND TidyTag_BIG TidyTag_BLINK TidyTag_BODY
TidyTag_BUTTON TidyTag_CAPTION TidyTag_CENTER TidyTag_CITE TidyTag_CODE TidyTag_COL TidyTag_COLGROUP TidyTag_COMMENT
TidyTag_DEL TidyTag_DFN TidyTag_DIR TidyTag_EM TidyTag_EMBED TidyTag_FONT TidyTag_FRAME TidyTag_FRAMESET
TidyTag_HEAD TidyTag_HTML TidyTag_I TidyTag_IFRAME TidyTag_ILAYER TidyTag_IMG TidyTag_INPUT TidyTag_INS
TidyTag_ISINDEX TidyTag_KBD TidyTag_KEYGEN TidyTag_LABEL TidyTag_LAYER TidyTag_LEGEND TidyTag_LINK TidyTag_LISTING
TidyTag_MAP TidyTag_MATHML TidyTag_MARQUEE TidyTag_MENU TidyTag_META TidyTag_MULTICOL TidyTag_NOBR TidyTag_NOEMBED
TidyTag_NOFRAMES TidyTag_NOLAYER TidyTag_NOSAVE TidyTag_OBJECT TidyTag_OPTGROUP TidyTag_OPTION TidyTag_PARAM TidyTag_PICTURE
TidyTag_PLAINTEXT TidyTag_Q TidyTag_RB TidyTag_RBC TidyTag_RP TidyTag_RT TidyTag_RTC TidyTag_RUBY
TidyTag_S TidyTag_SAMP TidyTag_SCRIPT TidyTag_SELECT TidyTag_SERVER TidyTag_SERVLET TidyTag_SMALL TidyTag_SPACER
TidyTag_SPAN TidyTag_STRIKE TidyTag_STRONG TidyTag_STYLE TidyTag_SUB TidyTag_SUP TidyTag_SVG TidyTag_TT
TidyTag_U TidyTag_VAR TidyTag_WBR TidyTag_XMP TidyTag_NEXTID TidyTag_BDI TidyTag_COMMAND TidyTag_DATALIST
TidyTag_MARK TidyTag_MENUITEM TidyTag_METER TidyTag_OUTPUT TidyTag_PROGRESS TidyTag_SOURCE TidyTag_SUMMARY TidyTag_TEMPLATE
TidyTag_TIME TidyTag_TRACK
*/

#define optAltText  1 << 0
#define optMetaDesc 1 << 1
#define optMetaKeyw 1 << 2
#define optEnumLsts 1 << 3
#define optTitleTxt 1 << 4
#define optALinks   1 << 5
#define optHRs      1 << 6
#define optImgLinks 1 << 7

#define testOpt(optOption) ( (optOption) & opts)

static void AddByte( Lexer *lexer, tmbchar ch )
{
    if ( lexer->lexsize + 2 >= lexer->lexlength )
    {
        tmbstr buf = NULL;
        uint allocAmt = lexer->lexlength;
        uint prev = allocAmt; /* Is. #761 */
        while ( lexer->lexsize + 2 >= allocAmt )
        {
            if ( allocAmt == 0 )
                allocAmt = 8192;
            else
                allocAmt *= 2;
            if (allocAmt < prev) /* Is. #761 - watch for wrap - and */
                TidyPanic(lexer->allocator, "\nPanic: out of internal memory!\nDocument input too big!\n");
        }
        buf = (tmbstr) TidyRealloc( lexer->allocator, lexer->lexbuf, allocAmt );
        if ( buf )
        {
          TidyClearMemory( buf + lexer->lexlength,
                           allocAmt - lexer->lexlength );
          lexer->lexbuf = buf;
          lexer->lexlength = allocAmt;
        }
    }

    lexer->lexbuf[ lexer->lexsize++ ] = ch;
    lexer->lexbuf[ lexer->lexsize ]   = '\0';  /* debug */
}


static uint addStringToLex(TidyDocImpl *doc, tmbstr str, uint len)
{
    Lexer *lexer = doc->lexer;
    uint c, ret=lexer->lexsize;

    /* need to skip translation in TY_(AddCharToLexer) */
    while( len-- && (c = (unsigned char) *str++ ))
        AddByte( lexer, c );

    return ret;
}

/* either we need to have a list of invalidated nodes
   or we need to never invalidate them until finalizer
   otherwise we could possibly end up inserting a valid
   node into a freed one.

   Here we take the easy way out and will just keep them
   around until finalizer is run
*/

static TidyNode detachNode(TidyDoc tdoc, TidyNode tnod, int freeNode)
{
//    TidyDocImpl* doc = tidyDocToImpl( tdoc );
    Node *node = tidyNodeToImpl( tnod );

    if (node)
    {
        TY_(RemoveNode)(node);
/*
        if(freeNode)
        {
            TY_(FreeNode)( doc, node);
            return NULL;
        }
*/
    }

    return tnod;
}
/*
static TidyNode cloneNode(TidyDoc tdoc, TidyNode tnod)
{
    TidyDocImpl* doc = tidyDocToImpl( tdoc );
    Node* node = tidyNodeToImpl( tnod );

    return (TidyNode) TY_(CloneNode)(doc, node);
}
*/

/* copy node from src into doc */

static Node *cloneNodeTree_ext(TidyDocImpl *doc, TidyDocImpl *src, Node* node)
{
    Node *retn = TY_(CloneNode)(doc, node);
    uint len = node->end - node->start;

    if(len>0)
    {
        uint offset = addStringToLex(doc,
              &(src->lexer->lexbuf[node->start]),
              len);
        retn->start = offset;
        retn->end = offset + len;
    }

    if(node->content != NULL)
    {
        Node *child = node->content;
        Node *rchild;

        rchild = cloneNodeTree_ext(doc, src, child);

        retn->content = rchild;
        rchild->parent = retn;

        child=child->next;
        while(child)
        {
            rchild->next = cloneNodeTree_ext(doc, src, child);
            rchild->next->parent=retn;
            rchild->next->prev = rchild;

            rchild = rchild->next;
            child = child->next;
        }

        retn->last=rchild;
    }

    return retn;

}

static TidyNode dupnode_ext(TidyDoc tdoc, TidyDoc sdoc, TidyNode tnod)
{
    TidyDocImpl* doc = tidyDocToImpl( tdoc );
    TidyDocImpl* srcdoc = tidyDocToImpl( sdoc );
    Node* node = tidyNodeToImpl( tnod );

    return (TidyNode) cloneNodeTree_ext(doc, srcdoc, node);
}

static Node *cloneNodeTree(TidyDocImpl *doc, Node* node)
{
    Node *retn = TY_(CloneNode)(doc, node);
    retn->start=node->start;
    retn->end=node->end;
    //printf("start=%d, end=%d\n",(int)retn->start, (int)retn->end);

    if(node->content != NULL)
    {
        Node *child = node->content;
        Node *rchild;

        rchild = cloneNodeTree(doc, child);

        retn->content = rchild;
        rchild->parent = retn;

        child=child->next;
        while(child)
        {
            rchild->next = cloneNodeTree(doc, child);
            rchild->next->parent=retn;
            rchild->next->prev = rchild;

            rchild = rchild->next;
            child = child->next;
        }

        retn->last=rchild;
    }
    return retn;
}
/*
static TidyNode dupnode(TidyDoc tdoc, TidyDoc sdoc, TidyNode tnod)
{
    TidyDocImpl* doc = tidyDocToImpl( tdoc );
    TidyDocImpl* srcdoc = tidyDocToImpl( sdoc );
    Node* node = tidyNodeToImpl( tnod );

    return (TidyNode) cloneNodeTree(doc, srcdoc, node);
}
*/

static void InsertNodeBeforeElement(Node *element, Node *node)
{
    Node *parent;

    parent = element->parent;
    node->parent = parent;
    node->next = element;
    node->prev = element->prev;
    element->prev = node;

    if (node->prev)
        node->prev->next = node;

    /* added parent NULL check */
    if (parent && parent->content == element)
        parent->content = node;
}


static TidyNode appendNode(TidyDoc tdoc, TidyDoc sdoc, TidyNode tnod, TidyNode anod)
{
    Node *node = tidyNodeToImpl(tnod);
    Node *anode = tidyNodeToImpl(anod);
    Node *clone;
    TidyDocImpl* doc = tidyDocToImpl(tdoc);

    clone = cloneNodeTree(doc, anode);
    TY_(InsertNodeAtEnd)(node, clone);
    return (TidyNode)clone;
}

static TidyNode prependNode(TidyDoc tdoc, TidyDoc sdoc, TidyNode tnod, TidyNode anod)
{
    Node *node = tidyNodeToImpl(tnod);
    Node *anode = tidyNodeToImpl(anod);
    Node *clone;
    TidyDocImpl* doc = tidyDocToImpl(tdoc);

    clone = cloneNodeTree(doc, anode);
    TY_(InsertNodeAtStart)(node, clone);
    return (TidyNode)clone;
}

static TidyNode afterNode(TidyDoc tdoc, TidyDoc sdoc, TidyNode tnod, TidyNode anod)
{
    Node *node = tidyNodeToImpl(tnod);
    Node *anode = tidyNodeToImpl(anod);
    Node *clone;
    TidyDocImpl* doc = tidyDocToImpl(tdoc);

    clone = cloneNodeTree(doc, anode);
    TY_(InsertNodeAfterElement)(node, clone);
    return (TidyNode)clone;
}

static TidyNode beforeNode(TidyDoc tdoc, TidyDoc sdoc, TidyNode tnod, TidyNode anod)
{
    Node *node = tidyNodeToImpl(tnod);
    Node *anode = tidyNodeToImpl(anod);
    Node *clone;
    TidyDocImpl* doc = tidyDocToImpl(tdoc);

    clone = cloneNodeTree(doc, anode);
    InsertNodeBeforeElement(node, clone);
    return (TidyNode)clone;
}

static TidyNode replaceNode(TidyDoc tdoc, TidyDoc sdoc, TidyNode tnod, TidyNode anod)
{
    Node *node = tidyNodeToImpl(tnod);
    Node *anode = tidyNodeToImpl(anod);
    Node *clone;
    TidyDocImpl* doc = tidyDocToImpl(tdoc);

    clone = cloneNodeTree(doc, anode);
    TY_(InsertNodeAfterElement)(node, clone);
    TY_(RemoveNode)(node);
    //TY_(DiscardElement)(doc, node);
    return (TidyNode)clone;
}

static TidyNode appendNode_ext(TidyDoc tdoc, TidyDoc sdoc, TidyNode tnod, TidyNode anod)
{
    Node *node = tidyNodeToImpl(tnod);
    Node *anode = tidyNodeToImpl(anod);
    Node *clone;
    TidyDocImpl* doc = tidyDocToImpl(tdoc);
    TidyDocImpl* srcdoc = tidyDocToImpl(sdoc);

    clone = cloneNodeTree_ext(doc, srcdoc, anode);
    TY_(InsertNodeAtEnd)(node, clone);
    return (TidyNode)clone;
}

static TidyNode prependNode_ext(TidyDoc tdoc, TidyDoc sdoc, TidyNode tnod, TidyNode anod)
{
    Node *node = tidyNodeToImpl(tnod);
    Node *anode = tidyNodeToImpl(anod);
    Node *clone;
    TidyDocImpl* doc = tidyDocToImpl(tdoc);
    TidyDocImpl* srcdoc = tidyDocToImpl(sdoc);

    clone = cloneNodeTree_ext(doc, srcdoc, anode);
    TY_(InsertNodeAtStart)(node, clone);
    return (TidyNode)clone;
}

static TidyNode afterNode_ext(TidyDoc tdoc, TidyDoc sdoc, TidyNode tnod, TidyNode anod)
{
    Node *node = tidyNodeToImpl(tnod);
    Node *anode = tidyNodeToImpl(anod);
    Node *clone;
    TidyDocImpl* doc = tidyDocToImpl(tdoc);
    TidyDocImpl* srcdoc = tidyDocToImpl(sdoc);

    clone = cloneNodeTree_ext(doc, srcdoc, anode);
    TY_(InsertNodeAfterElement)(node, clone);
    return (TidyNode)clone;
}

static TidyNode beforeNode_ext(TidyDoc tdoc, TidyDoc sdoc, TidyNode tnod, TidyNode anod)
{
    Node *node = tidyNodeToImpl(tnod);
    Node *anode = tidyNodeToImpl(anod);
    Node *clone;
    TidyDocImpl* doc = tidyDocToImpl(tdoc);
    TidyDocImpl* srcdoc = tidyDocToImpl(sdoc);

    clone = cloneNodeTree_ext(doc, srcdoc, anode);
    InsertNodeBeforeElement(node, clone);
    return (TidyNode)clone;
}

static TidyNode replaceNode_ext(TidyDoc tdoc, TidyDoc sdoc, TidyNode tnod, TidyNode anod)
{
    Node *node = tidyNodeToImpl(tnod);
    Node *anode = tidyNodeToImpl(anod);
    Node *clone;
    TidyDocImpl* doc = tidyDocToImpl(tdoc);
    TidyDocImpl* srcdoc = tidyDocToImpl(sdoc);

    clone = cloneNodeTree_ext(doc, srcdoc, anode);
    TY_(InsertNodeAfterElement)(node, clone);
    TY_(RemoveNode)(node);
    //TY_(DiscardElement)(doc, node);
    return (TidyNode)clone;
}

typedef TidyNode (*pendfunc)(TidyDoc tdoc, TidyDoc sdoc, TidyNode tnod, TidyNode anod);

static pendfunc pfunc[5][2] = {
    { &appendNode,  &appendNode_ext  },
    { &prependNode, &prependNode_ext },
    { &afterNode,   &afterNode_ext   },
    { &beforeNode,  &beforeNode_ext  },
    { &replaceNode, &replaceNode_ext }
};


const char *getAttr(TidyNode node, const char *name)
{
    TidyAttr attr;
    ctmbstr key;

    for ( attr=tidyAttrFirst(node); attr; attr=tidyAttrNext(attr) )
    {
        key=tidyAttrName(attr);
        if (!strcasecmp(key, name))
            return (const char *)tidyAttrValue(attr);
    }
    return (const char *)NULL;
}

const char *getnAttr(TidyNode node, const char *name, size_t len)
{
    TidyAttr attr;
    ctmbstr key;

    for ( attr=tidyAttrFirst(node); attr; attr=tidyAttrNext(attr) )
    {
        key=tidyAttrName(attr);
        if (!strncasecmp(key, name, len))
            return (const char *)tidyAttrValue(attr);
    }
    return (const char *)NULL;
}

static void addAttr( TidyDoc tdoc, TidyNode tnod, const char *attkey, const char *attval )
{
    TidyDocImpl* doc = tidyDocToImpl( tdoc );
    Node* node = tidyNodeToImpl( tnod );
    AttVal *av;

    for ( av = node->attributes; av != NULL; av = av->next )
    {
        if (!strcasecmp(av->attribute, attkey))
        {
            tmbstr newval;

            newval = (tmbstr) TidyDocAlloc(doc, strlen(attval)+1);
            TidyDocFree(doc, av->value);
            strcpy (newval, attval);
            av->value=newval;
            return;
        }
    }

    av = TY_(NewAttributeEx)( doc, attkey, attval, '"');
    TY_(InsertAttributeAtStart)( node, av );
}

static void putdoctype( TidyDoc tdoc, TidyNode tnod, TidyBuffer *buf, ctmbstr name)
{
    Node *node = tidyNodeToImpl( tnod );
    TidyDocImpl* doc = tidyDocToImpl( tdoc );
    AttVal* fpi = TY_(GetAttrByName)(node, "PUBLIC");
    AttVal* sys = TY_(GetAttrByName)(node, "SYSTEM");

    tidyBufAppend(buf, "<!DOCTYPE ", 10);

    tidyBufAppend(buf, (void*)name, strlen(name) );

    if (fpi && fpi->value && !sys)
    {
        tidyBufAppend(buf, " PUBLIC ", 8);
        tidyBufPutByte(buf, fpi->delim);
        tidyBufAppend(buf, fpi->value, strlen(fpi->value) );
        tidyBufPutByte(buf, fpi->delim);
    }
    else if (sys && sys->value)
    {
        tidyBufAppend(buf, " SYSTEM ", 8);
        tidyBufPutByte(buf, sys->delim);
        tidyBufAppend(buf, fpi->value, strlen(fpi->value) );
        tidyBufPutByte(buf, sys->delim);
    }

    if (node->content)
    {
        Node *cont = node->content;
        int len = cont->end - cont->start;

        tidyBufAppend(buf, "[<!", 3);
        tidyBufAppend(buf, &(doc->lexer->lexbuf[cont->start]), len);
        tidyBufAppend(buf, ">]", 3);
    }

    tidyBufPutByte(buf, '>');
}

TidyBuffer *dumpTag(TidyNode node, TidyBuffer *buf)
{
    TidyAttr attr;
    TidyNode child;
    TidyNodeType type = tidyNodeGetType(node);
    TidyTagId id;
    ctmbstr name;

    if(type != TidyNode_Start && type != TidyNode_StartEnd)
        return buf;

    child = tidyGetChild(node);
    name = tidyNodeGetName(node);
    id = tidyNodeGetId(node);

    tidyBufAppend(buf, "<", 1);
    tidyBufAppend(buf, (void*)name, strlen(name) );
    for (attr=tidyAttrFirst(node); attr; attr=tidyAttrNext(attr))
    {
        const char *k = (const char *) tidyAttrName(attr);
        const char *v = (const char *) tidyAttrValue(attr);
        size_t vlen=0;

        if(v) vlen=strlen(v);

        tidyBufAppend(buf, " ", 1);
        tidyBufAppend(buf, (void*)k, strlen(k));
        if (vlen)
        {
            if ( strchr(v,'"') )
            {
                tidyBufAppend(buf, "='", 2);
                tidyBufAppend(buf, (void*)v, vlen);
                tidyBufAppend(buf, "'", 1);
            } else {
                tidyBufAppend(buf, "=\"", 2);
                tidyBufAppend(buf, (void*)v, vlen);
                tidyBufAppend(buf, "\"", 1);
            }
        }
    }
    if(child || !isSingletonTag(id))
    {
        tidyBufAppend(buf, ">", 1);
    }
    else
    {
        tidyBufAppend(buf, " />", 3);
    }

    return buf;
}

TidyBuffer *dumpHtml(TidyDoc doc, TidyNode start, TidyBuffer *buf, int indent, int indented, int printself);

TidyBuffer *dumpNode(TidyNode node, TidyDoc doc, TidyBuffer *buf, int indent, int indented)
{
    TidyNode child=NULL;

    TidyNodeType type = tidyNodeGetType(node);
    TidyTagId id= tidyNodeGetId(node);
    ctmbstr name = tidyNodeGetName(node);
/*
    char indtext[indented+1];

    if(indent)
    {
        int i=0;
        for(;i<indented;i++)
            indtext[i]=' ';
        indtext[i]='\0';
    }
*/
    switch(type)
    {
        case TidyNode_Start:
        case TidyNode_StartEnd:
        {
            TidyAttr attr;

            //if(indent)
            //    tidyBufAppend(buf, indtext, indented);

            child = tidyGetChild(node);
            tidyBufAppend(buf, "<", 1);
            tidyBufAppend(buf, (void*)name, strlen(name) );
            for (attr=tidyAttrFirst(node); attr; attr=tidyAttrNext(attr))
            {
                const char *k = (const char *) tidyAttrName(attr);
                const char *v = (const char *) tidyAttrValue(attr);
                size_t vlen=0;

                if(v) vlen=strlen(v);

                tidyBufAppend(buf, " ", 1);
                tidyBufAppend(buf, (void*)k, strlen(k));
                if (vlen)
                {
                    tidyBufAppend(buf, "=\"", 2);
                    tidyBufAppend(buf, (void*)v, vlen);
                    tidyBufAppend(buf, "\"", 1);
                }
            }
            if(child || !isSingletonTag(id))
            {
                /*
                TidyNodeType ctype = tidyNodeGetType(child);

                if(indent && ctype!=TidyNode_Text)
                {
                    tidyBufAppend(buf, ">\n", 2);
                    buf=dumpHtml(doc, node, buf, indent, indent+indented, 0);
                    tidyBufAppend(buf, indtext, indented);
                }
                else
                {*/
                    tidyBufAppend(buf, ">", 1);
                    buf=dumpHtml(doc, node, buf, indent, indent+indented, 0);
                //}
                tidyBufAppend(buf, "</", 2);
                tidyBufAppend(buf, (void*)name, strlen(name) );
                tidyBufAppend(buf, ">", 1);
            }
            else
            {
                tidyBufAppend(buf, " />", 3);
            }
            //if(indent)
            //    tidyBufAppend(buf, "\n", 1);
            break;
        }
        case TidyNode_DocType:
            putdoctype(doc, node, buf, name);
            break;

        case TidyNode_Comment:
        case TidyNode_Text:
        {
            TidyBuffer tbuf;
            tidyBufInit(&tbuf);
            tidyNodeGetText(doc, node, &tbuf);
            tidyBufAppend(
                buf,
                tbuf.bp,
                tbuf.size - (tbuf.bp[tbuf.size-1]=='\n' ? 1 : 0)
            );
            tidyBufFree (&tbuf);
            break;
        }
        case TidyNode_Root:
            buf=dumpHtml(doc, node, buf, indent, indent+indented, 0);
            break;
        default:
/*
            if(type==TidyNode_Comment)
            {
                TidyBuffer tbuf;
                tidyBufInit(&tbuf);
                tidyNodeGetText(doc, node, &tbuf);
                fprintf(stderr,"%s\n",(char*)tbuf.bp);
                tidyBufFree (&tbuf);
            }

            fprintf(stderr,"got something else: (%d)(%d)  ", type, TidyNode_StartEnd);
            if(type==TidyNode_Root)
                fprintf(stderr, "Root\n");

            if(type==TidyNode_DocType) 	
                fprintf(stderr, "DOCTYPE.\n");

            if(type==TidyNode_Comment)	
                fprintf(stderr, "Comment.\n");

            if(type==TidyNode_ProcIns) 	
                fprintf(stderr, "Processing Instruction.\n");

            if(type==TidyNode_Text)
                fprintf(stderr, "Text.\n");

            if(type==TidyNode_Start )	
                fprintf(stderr, "Start Tag.\n");

            if(type==TidyNode_End 	)
                fprintf(stderr, "End Tag.\n");

            if(type==TidyNode_StartEnd) 	
                fprintf(stderr, "Start/End (empty) Tag.\n");

            if(type==TidyNode_CDATA )	
                fprintf(stderr, "Unparsed Text.\n");

            if(type==TidyNode_Section) 	
                fprintf(stderr, "XML Section.\n");

            if(type==TidyNode_Asp)
                fprintf(stderr, "ASP Source.\n");

            if(type==TidyNode_Jste)
                fprintf(stderr, "JSTE Source.\n");

            if(type==TidyNode_Php)
                fprintf(stderr, "PHP Source.\n");

            if(type==TidyNode_XmlDecl)
                fprintf(stderr, "XML Declaration.\n");
*/
            break;
    }
    return buf;
}

TidyBuffer *dumpHtml(TidyDoc doc, TidyNode start, TidyBuffer *buf, int indent, int indented, int printself)
{
    TidyNode child;
//    TidyNodeType prevtype = TidyNode_Start;
    if(printself)
    {
        dumpNode(start, doc, buf, indent, indented);
        return buf;
    }

    for ( child = tidyGetChild(start); child; child = tidyGetNext(child) )
    {
/*
revisit indentation later
        if(prevtype == TidyNode_Text && !isBlockTag(tidyNodeGetId( child )))
        {
            TidyNode next = tidyGetNext(child);

            buf=dumpNode(child, doc, buf, 0, 0);
            if(
                indent &&
                next &&
                tidyNodeGetType(next) !=  TidyNode_Text &&
                isBlockTag(tidyNodeGetId(next))
            )
                tidyBufAppend(buf, "\n", 1);

        }
        else
*/
            buf=dumpNode(child, doc, buf, indent, indented);

        //prevtype = tidyNodeGetType(child);
    }
    return buf;
}

TidyBuffer *dumpText(TidyDoc doc, TidyNode start, TidyBuffer *buf, int listno, int listind, int tag_end_nl, int opts)
{
    TidyNode child;
    TidyNodeType type;
    TidyTagId lastid = 0;

    for ( child = tidyGetChild(start); child; child = tidyGetNext(child) )
    {
        TidyTagId id = tidyNodeGetId( child );

        type = tidyNodeGetType( child );
        switch (type)
        {
            case TidyNode_Text:
            {
                TidyBuffer tbuf;
                tidyBufInit(&tbuf);
                tidyNodeGetValue(doc, child, &tbuf);
                tidyBufAppend(
                    buf,
                    tbuf.bp,
                    tbuf.size- (tbuf.bp[tbuf.size-1]=='\n' ? 1 : 0)
                );
                tidyBufFree (&tbuf);
                tag_end_nl=0;
                break;
            }
            case TidyNode_StartEnd:
            case TidyNode_Start:
            {
                int indent=0;

#define tag_indent(x) do {\
    int i=(x);\
    while(i--)tidyBufAppend(buf, "\t", 1);\
} while(0)

#define isBlockNotCell(id) ( isBlockTag((id)) && id!=TidyTag_TD && id!=TidyTag_TH )

                if (isBlockNotCell(id) && buf->size>0 && buf->bp[buf->size -1] != '\n')
                    tidyBufAppend(buf, "\n", 1);
                else if( (id == TidyTag_TD || id == TidyTag_TH) && (lastid == TidyTag_TD || lastid == TidyTag_TH) )
                    tidyBufAppend(buf, "\t", 1);

                if( isSectionTag(id) && buf->size>1 && buf->bp[buf->size -2] != '\n')
                    tidyBufAppend(buf, "\n", 1);

                tag_end_nl=0;
                if ( id==TidyTag_SCRIPT || id==TidyTag_STYLE)
                {
                    break;
                }
                else
                {
                    if (id==TidyTag_OL)
                    {
                        /* listno is a flag and a counter */
                        listno=1;
                        indent=1;
                    }
                    else if (id==TidyTag_UL || id==TidyTag_DL)
                    {
                        listno=0;
                        indent=1;
                    }
                    /* number ordered lists and put "* " in front of unordered lists */
                    else if (id==TidyTag_LI && testOpt(optEnumLsts) )
                    {
                        tag_indent(listind);
                        if(listno)
                        {
                            char lbuf[16];
                            int len=snprintf(lbuf, 16, "%d. ", listno++);

                            tidyBufAppend(buf, lbuf, len);
                        }
                        else
                        {
                            tidyBufAppend(buf, "* ", 2);
                        }
                    }
                    /*  indent dd in dt */
                    else if (id==TidyTag_DT && testOpt(optEnumLsts) )
                    {
                        tag_indent(listind-1);
                        tidyBufAppend(buf, "\t", 1);
                    }
                    else if (id==TidyTag_DD && testOpt(optEnumLsts) )
                    {
                        tag_indent(listind);
                        tidyBufAppend(buf, "\t", 1);
                    }
                    /* get meta name=keywords|description */
                    else if (id==TidyTag_META)
                    {
                        const char *name=getAttr(child, "name");
                        if (name)
                        {
                            int metaType = 0;
                            if ( testOpt(optMetaDesc) && !strcasecmp(name,"description") )
                                metaType = 1;
                            else if ( testOpt(optMetaKeyw) && !strcasecmp(name,"keywords") )
                                metaType=2;
                            if( metaType )
                            {
                                const char *cont=getAttr(child, "content");
                                if(cont)
                                {
                                    int len = strlen(cont)+16;
                                    char lbuf[len];
                                    char *tname = (metaType==1) ? "description: " : "keywords: ";

                                    sprintf(lbuf, "%s%s\n", tname, cont);
                                    tidyBufAppend(buf, lbuf, strlen(lbuf));
                                }

                            }
                        }
                    }
                    /* get alt text from images */
                    else if ( id==TidyTag_IMG && (testOpt(optAltText) || testOpt(optImgLinks)) )
                    {
                        const char *alttext=getAttr(child, "alt");
                        if(testOpt(optImgLinks) )
                            tidyBufAppend(buf,"![",2);

                        if(alttext &&  testOpt(optAltText))
                        {
                            int len = strlen(alttext)+3;
                            char lbuf[len];
                            char *space = testOpt(optImgLinks)? "" : " ";

                            sprintf(lbuf, "%s%s%s", space,alttext,space);
                            tidyBufAppend(buf, lbuf, len-1 - (testOpt(optImgLinks)? 2:0));
                        }
                    }


                    /* seperate A tags from surrounding text, add brackets if optALinks */
                    if(id == TidyTag_A && buf->size>0 && buf->bp[buf->size -1] != '\n' && buf->bp[buf->size -1] != ' ')
                        tidyBufAppend(buf, testOpt(optALinks)? " [":" ", testOpt(optALinks)? 2:1);
                    else if (id == TidyTag_A && testOpt(optALinks))
                        tidyBufAppend(buf,"[",1);

                    // we need to put title text before child node text unless A or IMG. So check if
                    // an A or IMG link.  If not, append it here.
                    if( testOpt(optTitleTxt) )
                    {
                        if( !(
                              (id == TidyTag_IMG && testOpt(optImgLinks) && getAttr(child, "src")) ||
                              (id == TidyTag_A   && testOpt(optALinks)   && getAttr(child, "href"))
                            )
                        )
                        {
                            const char *title = getAttr(child, "title");

                            if(title)
                            {
                                int len = strlen(title)+2;
                                char lbuf[len];

                                sprintf(lbuf, "%s\n", title);
                                tidyBufAppend(buf, lbuf, len-1);
                            }
                        }
                    }

                    // recurse for child nodes
                    buf=dumpText(doc, child, buf, listno, listind+indent, tag_end_nl, opts);

                    /* seperate A tags from surrounding text, add brackets if optALinks */
                    if(id == TidyTag_A && buf->size>0 && buf->bp[buf->size -1] != '\n' && buf->bp[buf->size -1] != ' ')
                        tidyBufAppend(buf, testOpt(optALinks)? "]":" ", 1);
                    else if (id == TidyTag_A && testOpt(optALinks))
                        tidyBufAppend(buf,"]",1);

                    /* do image links in markdown style */
                    if(id == TidyTag_IMG && testOpt(optImgLinks) )
                    {
                        tidyBufAppend(buf,"]",1);
                        const char *src = getAttr(child, "src");

                        if(src)
                        {
                            if( testOpt(optTitleTxt) )
                            {
                                const char *title = getAttr(child, "title");
                                int len = strlen(src)+3;

                                if(title)
                                {
                                    len += strlen(title)+3;
                                    char lbuf[len];

                                    sprintf(lbuf, "(%s \"%s\")", src,title);
                                    tidyBufAppend(buf, lbuf, len-1);
                                }
                                else
                                {
                                    char lbuf[len];

                                    sprintf(lbuf, "(%s)", src);
                                    tidyBufAppend(buf, lbuf, len-1);
                                }
                            }
                            else
                            {
                                int len = strlen(src)+3;
                                char lbuf[len];

                                sprintf(lbuf, "(%s)", src);
                                tidyBufAppend(buf, lbuf, len-1);
                            }
                        }
                    }


                    /* print anchor links */
                    if(id==TidyTag_A && testOpt(optALinks) )
                    {
                        const char *href = getAttr(child, "href");

                        if(href)
                        {
                            if( testOpt(optTitleTxt) )
                            {
                                const char *title = getAttr(child, "title");
                                int len = strlen(href)+3;

                                if(title)
                                {
                                    len += strlen(title)+3;
                                    char lbuf[len];

                                    sprintf(lbuf, "(%s \"%s\")", href, title);
                                    tidyBufAppend(buf, lbuf, len-1);
                                }
                                else
                                {
                                    char lbuf[len];

                                    sprintf(lbuf, "(%s)", href);
                                    tidyBufAppend(buf, lbuf, len-1);
                                }
                            }
                            else
                            {
                                int len = strlen(href)+3;
                                char lbuf[len];

                                sprintf(lbuf, "(%s)", href);
                                tidyBufAppend(buf, lbuf, len-1);
                            }
                        }
                    }


                    if(id == TidyTag_HR && testOpt(optHRs))
                    {
                        tidyBufAppend(buf, "---\n", 4);
                    }

                    if( isBlockNotCell(id) && !tag_end_nl && buf->size>0 && buf->bp[buf->size -1] != '\n')
                        tidyBufAppend(buf, "\n", 1);

                    if( isSectionTag(id) && buf->size>1 && buf->bp[buf->size -2] != '\n')
                        tidyBufAppend(buf, "\n", 1);

                   tag_end_nl=1;

                    break;
                }
            }
            default:
                break;
        }
        lastid=id;
    }

    return buf;
}

#define setoptbool(optname,optflag) do{\
    if(duk_get_prop_string(ctx, 0, optname))\
    {\
        if(REQUIRE_BOOL(ctx, -1, "html.toText - option %s requires a boolean",optname)) \
            opts |= optflag;\
        else\
            opts &= ~ (optflag);\
    }\
    duk_pop(ctx);\
} while (0)


static duk_ret_t _tohtml(duk_context *ctx)
{
    TidyNode start;
    TidyDoc doc;
    TidyBuffer buf, *ret;
    int i=0, indent=0;
//    int makearray=-1;
    int makearray=1;
    duk_idx_t this_idx;

    ret=&buf;
    tidyBufInit(ret);

    if (duk_is_object(ctx, 0))
    {
        if(duk_get_prop_string(ctx, 0, "concatenate"))
        {
            if(REQUIRE_BOOL(ctx, -1, "html.toHtml - option concatenate requires a boolean"))
                makearray=0;
            else
                makearray=1;
        }
        duk_pop(ctx);
        /*
        if(duk_get_prop_string(ctx, 0, "indent"))
        {
            indent = REQUIRE_INT(ctx, -1, "html.toHtml - option indent requires a number");
        }
        duk_pop(ctx);
        */
    }

    duk_push_this(ctx);
    this_idx=duk_get_top_index(ctx);

    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("tdoc"));
    doc=duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    /*
    if(makearray==-1)
    {
        int len;

        duk_get_prop_string(ctx, this_idx, "length");
        len=duk_get_int(ctx, -1);
        duk_pop(ctx);
        if(len > 1)
            makearray=1;
        else
            makearray=0;
    }
    */
    if(makearray)
        duk_push_array(ctx);

    /* loop over nodes, extract text, append to buffer/array */
    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("nodes"));
    duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
    while(duk_next(ctx, -1, 1))
    {
        if(i)
        {
            if(!makearray)
                tidyBufAppend(ret, "\n", 1);
            else
                tidyBufInit(ret);
        }

        start=duk_get_pointer(ctx, -1);
        duk_pop_2(ctx);
//        if(tidyNodeGetType(start) == TidyNode_DocType)
//            continue;

        ret=dumpHtml(doc, start, ret, indent, 0, 1);

        if(makearray)
        {
            if(ret->size)
                duk_push_string(ctx, (const char *)ret->bp);
            else
                duk_push_string(ctx, "");
            duk_put_prop_index(ctx, -4, i);
            tidyBufFree(ret);
            i++;
        }
        else
            i++;
    }
    duk_pop_2(ctx);

    if(!makearray)
    {
        if(ret->size)
            duk_push_string(ctx, (const char *)ret->bp);
        else
            duk_push_string(ctx, "");
        tidyBufFree(ret);
    }
    return 1;
}

duk_ret_t duk_rp_html_totext(duk_context *ctx)
{
    TidyNode start;
    TidyDoc doc;
    TidyBuffer buf, *ret;
    int i=0, opts=0; //optAltText|optMetaDesc|optMetaKeyw|optEnumLsts;
    int makearray=1;
    duk_idx_t this_idx;

    ret=&buf;
    tidyBufInit(ret);


    if (duk_is_object(ctx, 0))
    {
        setoptbool("imgAltText", optAltText);
        setoptbool("imageAltText", optAltText);
        setoptbool("metaDescription",optMetaDesc);
        setoptbool("metaKeywords",optMetaKeyw);
        setoptbool("titleText",optTitleTxt);
        setoptbool("aLinks",optALinks);
        setoptbool("enumerateLists",optEnumLsts);
        setoptbool("showHRTags",optHRs);
        setoptbool("imgLinks",optImgLinks);

        if(duk_get_prop_string(ctx, 0, "concatenate"))
        {
            if(REQUIRE_BOOL(ctx, -1, "html.toText - option concatenate requires a boolean"))
                makearray=0;
            else
                makearray=1;
        }
        duk_pop(ctx);

    }

    duk_push_this(ctx);
    this_idx=duk_get_top_index(ctx);

    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("tdoc"));
    doc=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    /*
    if(makearray==-1)
    {
        int len;

        duk_get_prop_string(ctx, this_idx, "length");
        len=duk_get_int(ctx, -1);
        duk_pop(ctx);
        if(len > 1)
            makearray=1;
        else
            makearray=0;
    }
    */
    if(makearray)
        duk_push_array(ctx);

    /* loop over nodes, extract text, append to buffer/array */
    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("nodes"));
    duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
    while(duk_next(ctx, -1, 1))
    {
        if(i)
        {
            if(!makearray)
                tidyBufAppend(ret, "\n", 1);
            else
                tidyBufInit(ret);
        }

        start=duk_get_pointer(ctx, -1);
        duk_pop_2(ctx);

        if(tidyNodeGetType(start) == TidyNode_DocType)
            continue;

        ret=dumpText(doc, start, ret, 0, 0, 0, opts);

        if(makearray)
        {
            if(ret->size)
                duk_push_string(ctx, (const char *)ret->bp);
            else
                duk_push_string(ctx, "");

            duk_put_prop_index(ctx, -4, i);
            tidyBufFree(ret);
        }
        i++;
    }
    duk_pop_2(ctx);

    if(!makearray)
    {
        if(ret->size)
            duk_push_string(ctx, (const char *)ret->bp);
        else
            duk_push_string(ctx, "");
        tidyBufFree(ret);
    }
    return 1;
}

duk_ret_t duk_rp_html_tohtml(duk_context *ctx)
{
/*
    duk_push_this(ctx);
    if(duk_get_prop_string(ctx, -1, "html"))
        return 1;

    duk_pop(ctx);
*/
    return _tohtml(ctx);
}

#define findTag 0
#define findAttr 1
#define findClass 2


static int findfunc_tag (TidyNode node, const char **txt, const char **txt2, int ntxt){
    int i=0;
    ctmbstr name = tidyNodeGetName(node);

    if(!name)
        return 0;

    for (;i<ntxt;i++)
    {
        if(!strcasecmp(txt[i],name))
            return 1;
    }

    return 0;
}

static int findfunc_attr (TidyNode node, const char **txt, const char **txt2, int ntxt){
    int i=0;

    for (;i<ntxt;i++)
    {
        size_t len;
        const char *s;

        s = strchr(txt[i], '=');

        /* backup to non-whitespace before '=' */
        while ( s>txt[i] && isspace(*(s-1)) ) s--;

        if(s)
            len=(size_t) ( s - txt[i]);
        else
            len=strlen(txt[i]);

        s = getnAttr(node, txt[i], len);

        if(s)
        {
            if( txt2[i] )
            {
                if(!strcmp(s,txt2[i]))
                    return 1;
            }
            else
                return 1;
        }
    }

    return 0;
}

static int findfunc_class (TidyNode node, const char **txt, const char **txt2, int ntxt){
    const char *classes=getAttr(node,"class");
    int i=0;

    if(!classes)
        return 0;

    for (;i<ntxt;i++)
    {
        const char *class = strstr(classes, txt[i]);

        while (class)
        {
            const char *end = class+strlen(txt[i]);
            /* check begin of string */
            if(class == classes || *(class-1) == ' ')
            {
                //* check end of string */
                if(*end=='\0' || *end==' ')
                    return 1;
            }
            class = strstr(end, txt[i]);
        }
    }

    return 0;
}


typedef int (*findfunc)(TidyNode node, const char **txt, const char **txt2, int ntxt);

static findfunc ffunc[3] = {
    &findfunc_tag,
    &findfunc_attr,
    &findfunc_class
};

static void _find_(
    duk_context *ctx,
    TidyDoc doc,
    TidyNode start,
    duk_idx_t arr_idx,
    const char **txt,
    const char **txt2,
    int ntxt,
    int findType ,
    int filter    )
{
    TidyNode child;
    TidyNodeType type;

    if(filter)
    {
        type = tidyNodeGetType(start);
        if(type == TidyNode_Start || type == TidyNode_StartEnd )
        {
            if( (ffunc[findType])(start, txt, txt2, ntxt) )
            {
                duk_uarridx_t len  = (duk_uarridx_t) duk_get_length(ctx, arr_idx);
                if(filter==2) /* for hasXxx */
                    duk_push_true(ctx);
                else /* for filterXxx */
                    duk_push_pointer(ctx, (void*)start);
                duk_put_prop_index(ctx, arr_idx, len);
            }
            else if(filter==2)
            {
                duk_uarridx_t len  = (duk_uarridx_t) duk_get_length(ctx, arr_idx);

                duk_push_false(ctx);
                duk_put_prop_index(ctx, arr_idx, len);
            }
        }
        return;
    }

    for ( child = tidyGetChild(start); child; child = tidyGetNext(child) )
    {
        type = tidyNodeGetType(child);
        if(type == TidyNode_Start || type == TidyNode_StartEnd )
        {
            if( (ffunc[findType])(child, txt, txt2, ntxt) )
            {
                duk_uarridx_t len  = (duk_uarridx_t) duk_get_length(ctx, arr_idx);

                duk_push_pointer(ctx, (void*)child);
                duk_put_prop_index(ctx, arr_idx, len);
            }
            if(type == TidyNode_Start)
                _find_(ctx, doc, child, arr_idx, txt, txt2, ntxt, findType, filter);
        }
    }
}


static void _findtxts(duk_context *ctx, duk_idx_t arr_idx, const char **txts, int ntxts, int findtype, int filter)
{
    TidyNode start;
    TidyDoc doc;
    const char **txt2=NULL;

    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("tdoc"));
    doc=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("nodes"));
    duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);

    if(findtype==findAttr)
    {
        int i=0;
        REMALLOC(txt2, ntxts * sizeof(const char *));

        for(;i<ntxts;i++)
        {
            const char *val = strchr(txts[i], '=');
            txt2[i]=NULL;

            if(val)
            {
                val++;
                while (isspace(*val)) val++;
                txt2[i] = val;
            }
        }
    }

    while(duk_next(ctx, -1, 1))
    {
        start=duk_get_pointer(ctx, -1);
        duk_pop_2(ctx);
        _find_(ctx, doc, start, arr_idx, txts, txt2, ntxts, findtype, filter);
    }

    duk_pop_3(ctx); /* this, nodes, enum */

    if(findtype==findAttr)
        free(txt2);
}

duk_ret_t duk_rp_html_findtag(duk_context *ctx);
duk_ret_t duk_rp_html_findattr(duk_context *ctx);
duk_ret_t duk_rp_html_findclass(duk_context *ctx);
duk_ret_t duk_rp_html_filtertag(duk_context *ctx);
duk_ret_t duk_rp_html_filterattr(duk_context *ctx);
duk_ret_t duk_rp_html_filterclass(duk_context *ctx);
duk_ret_t duk_rp_html_hastag(duk_context *ctx);
duk_ret_t duk_rp_html_hasattr(duk_context *ctx);
duk_ret_t duk_rp_html_hasclass(duk_context *ctx);
duk_ret_t duk_rp_html_addclass(duk_context *ctx);
duk_ret_t duk_rp_html_delclass(duk_context *ctx);
duk_ret_t duk_rp_html_slice(duk_context *ctx);
duk_ret_t duk_rp_html_eq(duk_context *ctx);
duk_ret_t duk_rp_html_getattr(duk_context *ctx);
duk_ret_t duk_rp_html_getallattr(duk_context *ctx);
duk_ret_t duk_rp_html_attr(duk_context *ctx);
duk_ret_t duk_rp_html_delattr(duk_context *ctx);
duk_ret_t duk_rp_html_parent(duk_context *ctx);
duk_ret_t duk_rp_html_children(duk_context *ctx);
duk_ret_t duk_rp_html_next(duk_context *ctx);
duk_ret_t duk_rp_html_prev(duk_context *ctx);
duk_ret_t duk_rp_html_getelem(duk_context *ctx);
duk_ret_t duk_rp_html_getelemname(duk_context *ctx);
duk_ret_t duk_rp_html_detach(duk_context *ctx);
duk_ret_t duk_rp_html_delete(duk_context *ctx);
duk_ret_t duk_rp_html_append(duk_context *ctx);
duk_ret_t duk_rp_html_prepend(duk_context *ctx);
duk_ret_t duk_rp_html_after(duk_context *ctx);
duk_ret_t duk_rp_html_before(duk_context *ctx);
duk_ret_t duk_rp_html_replace(duk_context *ctx);
duk_ret_t duk_rp_html_add(duk_context *ctx);
duk_ret_t duk_rp_html_getdocument(duk_context *ctx);

static void pushfuncs(duk_context *ctx)
{
    duk_push_c_function(ctx, duk_rp_html_totext, 1);
    duk_put_prop_string(ctx, -2, "toText");

    duk_push_c_function(ctx, duk_rp_html_tohtml, 1);
    duk_put_prop_string(ctx, -2, "toHtml");

    duk_push_c_function(ctx, duk_rp_html_getelem, 0);
    duk_put_prop_string(ctx, -2, "getElement");

    duk_push_c_function(ctx, duk_rp_html_getelemname, 0);
    duk_put_prop_string(ctx, -2, "getElementName");

    duk_push_c_function(ctx, duk_rp_html_slice, 2);
    duk_put_prop_string(ctx, -2, "slice");

    duk_push_c_function(ctx, duk_rp_html_eq, 1);
    duk_put_prop_string(ctx, -2, "eq");

    duk_push_c_function(ctx, duk_rp_html_getattr, 1);
    duk_put_prop_string(ctx, -2, "getAttr");

    duk_push_c_function(ctx, duk_rp_html_getallattr, 1);
    duk_put_prop_string(ctx, -2, "getAllAttr");

    duk_push_c_function(ctx, duk_rp_html_attr, 2);
    duk_put_prop_string(ctx, -2, "attr");

    duk_push_c_function(ctx, duk_rp_html_delattr, 1);
    duk_put_prop_string(ctx, -2, "removeAttr");

    duk_push_c_function(ctx, duk_rp_html_findtag, 1);
    duk_put_prop_string(ctx, -2, "findTag");

    duk_push_c_function(ctx, duk_rp_html_findattr, 1);
    duk_put_prop_string(ctx, -2, "findAttr");

    duk_push_c_function(ctx, duk_rp_html_findclass, 1);
    duk_put_prop_string(ctx, -2, "findClass");

    duk_push_c_function(ctx, duk_rp_html_filtertag, 1);
    duk_put_prop_string(ctx, -2, "filterTag");

    duk_push_c_function(ctx, duk_rp_html_filterattr, 1);
    duk_put_prop_string(ctx, -2, "filterAttr");

    duk_push_c_function(ctx, duk_rp_html_filterclass, 1);
    duk_put_prop_string(ctx, -2, "filterClass");

    duk_push_c_function(ctx, duk_rp_html_hastag, 1);
    duk_put_prop_string(ctx, -2, "hasTag");

    duk_push_c_function(ctx, duk_rp_html_hasattr, 1);
    duk_put_prop_string(ctx, -2, "hasAttr");

    duk_push_c_function(ctx, duk_rp_html_hasclass, 1);
    duk_put_prop_string(ctx, -2, "hasClass");

    duk_push_c_function(ctx, duk_rp_html_addclass, 1);
    duk_put_prop_string(ctx, -2, "addClass");

    duk_push_c_function(ctx, duk_rp_html_delclass, 1);
    duk_put_prop_string(ctx, -2, "removeClass");

    duk_push_c_function(ctx, duk_rp_html_parent, 0);
    duk_put_prop_string(ctx, -2, "parent");

    duk_push_c_function(ctx, duk_rp_html_children, 1);
    duk_put_prop_string(ctx, -2, "children");

    duk_push_c_function(ctx, duk_rp_html_next, 0);
    duk_put_prop_string(ctx, -2, "next");

    duk_push_c_function(ctx, duk_rp_html_prev, 0);
    duk_put_prop_string(ctx, -2, "prev");

    duk_push_c_function(ctx, duk_rp_html_detach, 0);
    duk_put_prop_string(ctx, -2, "detach");

    duk_push_c_function(ctx, duk_rp_html_delete, 0);
    duk_put_prop_string(ctx, -2, "delete");

    duk_push_c_function(ctx, duk_rp_html_append, 1);
    duk_put_prop_string(ctx, -2, "append");

    duk_push_c_function(ctx, duk_rp_html_prepend, 1);
    duk_put_prop_string(ctx, -2, "prepend");

    duk_push_c_function(ctx, duk_rp_html_after, 1);
    duk_put_prop_string(ctx, -2, "after");

    duk_push_c_function(ctx, duk_rp_html_before, 1);
    duk_put_prop_string(ctx, -2, "before");

    duk_push_c_function(ctx, duk_rp_html_replace, 1);
    duk_put_prop_string(ctx, -2, "replace");

    duk_push_c_function(ctx, duk_rp_html_add, 1);
    duk_put_prop_string(ctx, -2, "add");

    duk_push_c_function(ctx, duk_rp_html_getdocument, 0);
    duk_put_prop_string(ctx, -2, "getDocument");

}

/* push new array of the unique nodes
   in array at arr_idx            */
static void uniq_array_nodes(duk_context *ctx, duk_idx_t arr_idx)
{
    int len = (int)duk_get_length(ctx, arr_idx);

    duk_push_array(ctx);

    if(len)
    {
        TidyNode node[len], cur;
        int i=0, j=0, ulen=0, newi=0;

        for(;i<len;i++)
        {
            duk_get_prop_index(ctx, arr_idx, (duk_uarridx_t)i);
            cur=duk_get_pointer(ctx, -1);

            for(j=0;j<ulen;j++)
            {
                if(cur == node[j])
                    break;
            }

            if(j==ulen)
            {
                node[ulen++]=cur;
                duk_put_prop_index(ctx, -2, (duk_uarridx_t)newi++);
            }
            else
                duk_pop(ctx);

        }
    }
}

static void new_ret_object(duk_context *ctx, duk_idx_t arr_idx)
{
    duk_push_this(ctx);
    duk_push_object(ctx);

//    duk_get_prop_string(ctx, -2, "errMsg");
//    duk_put_prop_string(ctx, -2, "errMsg");

    duk_get_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("tdoc"));
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("tdoc"));

//    duk_get_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("errbuf"));
//    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("errbuf"));

    duk_push_number(ctx, (double) duk_get_length(ctx, arr_idx));
    duk_put_prop_string(ctx, -2, "length");

    pushfuncs(ctx);

//    duk_pull(ctx, arr_idx);
    uniq_array_nodes(ctx, arr_idx);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("nodes"));


    duk_get_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("root"));
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("root"));

}



// [\-_a-zA-Z0-9\x80-\xff]
static const char VALID_CLASS_TABLE[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
/* not exactly proper for css, but will keep us out of trouble */
static int isvalname(const char *n)
{
    while ( (*n) && VALID_CLASS_TABLE[(unsigned char)*n])
	n++;
    return !(int)(*n);
}

duk_ret_t duk_rp_html_getdocument(duk_context *ctx)
{
    duk_push_this(ctx);
    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("root")))
        RP_THROW(ctx, "html.getDocument: error - document root not found");

    return 1;
}


/* check if node has class.  If class attribute exists, set classattr to it
   If classpos is not NULL, a pointer to position of classname in classattr is set
*/

static int hasclass (TidyNode node, const char *classname, const char **classattr, const char **classpos){
    const char
        *classes=getAttr(node,"class"),
        *class, *end;

    if(!classes)
    {
        *classattr=NULL;
        return 0;
    }

    *classattr=classes;
    if(classpos)
        *classpos=NULL;

    class = strstr(classes, classname);

    while (class)
    {
        end = class+strlen(classname);
        /* check begin of string */
        if(class == classes || *(class-1) == ' ')
        {
            //* check end of string */
            if(*end=='\0' || *end==' ')
            {
                if(classpos)
                    *classpos=class;
                return 1;
            }
        }
        class = strstr(end, classname);
    }

    return 0;
}

duk_ret_t duk_rp_html_addclass(duk_context *ctx)
{
    const char *classattr,
        *cname = REQUIRE_STRING(ctx, 0, "html.addClass - first argument must be a string (attr name)");
    int i=0, len;
    TidyNode node;
    TidyDoc tdoc;

    if(!isvalname(cname))
	RP_THROW(ctx, "html.addClass - '%s' invalid class name\n", cname);

    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("tdoc"));
    tdoc=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("nodes"));
    len=duk_get_length(ctx, -1);

    for(;i<len;i++)
    {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        node=(TidyNode)duk_get_pointer(ctx, -1);
        duk_pop(ctx);

        if(!hasclass(node, cname, &classattr, NULL))
        {
            if(classattr)
            {
                char newattr[strlen(classattr) + strlen(cname) + 2];

                strcpy(newattr, classattr);
                strcat(newattr, " ");
                strcat(newattr, cname);
                addAttr(tdoc, node, "class", newattr);
            }
            else
            {
                addAttr(tdoc, node, "class", cname);
            }
        }

    }
    duk_pull(ctx, 1);
    return 1;
}

duk_ret_t duk_rp_html_delclass(duk_context *ctx)
{
    const char *classattr, *cpos,
        *cname = REQUIRE_STRING(ctx, 0, "html.removeClass - first argument must be a string (attr name)");
    int i=0, len;
    TidyNode node;
    TidyDoc tdoc;

    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("tdoc"));
    tdoc=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("nodes"));
    len=duk_get_length(ctx, -1);

    for(;i<len;i++)
    {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        node=(TidyNode)duk_get_pointer(ctx, -1);
        duk_pop(ctx);

        if(hasclass(node, cname, &classattr, &cpos))
        {
            /* check if it is the only class */
            if(strlen(cname) == strlen(classattr))
            {
                addAttr(tdoc, node, "class", "");
            }
            else
            {
                int npos = cpos - classattr,
                    epos = npos + strlen(cname);

                char newattr[strlen(classattr) + 1];
                /* if the to be removed class is first */
                if(!npos)
                    strcpy(newattr, classattr + epos +1);
                else
                {
                    /* copy what comes after over it*/
                    strcpy(newattr, classattr);
                    if(*(classattr + epos))
                        strcpy(newattr + npos, classattr + epos +1);
                    else
                        newattr[npos-1]='\0';
                }

                addAttr(tdoc, node, "class", newattr);
            }
        }

    }
    duk_pull(ctx, 1);
    return 1;
}

/* html tidy makes anything parsed into a document.
   parse html, select body, then select elements in
   in body and put it on duktape stack in format
   understood by _pend below
*/
static void _htmlparsefrag(duk_context *ctx, const char *html)
{
    TidyDoc tdoc;
    TidyNode root, body, head, child;
    const char *tagbody="body", *taghead="head";
    duk_uarridx_t i=0;
    TidyBuffer errbuf = {0};

    duk_push_object(ctx);

    tdoc = tidyCreate();
    tidyOptSetBool(tdoc, TidyForceOutput, yes);
    tidyOptSetBool(tdoc, TidyMark, no);

    tidySetErrorBuffer( tdoc, &errbuf );

    tidyParseString(tdoc, html);
    tidyCleanAndRepair(tdoc);

    duk_push_pointer(ctx, (void *)tdoc);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("tdoc"));

    root=tidyGetRoot(tdoc);
    duk_push_array(ctx);

    _find_(ctx, tdoc, root, duk_get_top_index(ctx),
        &taghead, NULL, 1, findTag, 0);
    _find_(ctx, tdoc, root, duk_get_top_index(ctx),
        &tagbody, NULL, 1, findTag, 0);

    duk_get_prop_index(ctx, -1, 0);
    head=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_index(ctx, -1, 1);
    body=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    duk_push_array(ctx);
    duk_replace(ctx, -2);

    /* get head elements */
    child = tidyGetChild(head);
    while(child)
    {
        TidyTagId id= tidyNodeGetId(child);

        /* skip empty title */
        if( id != TidyTag_TITLE || tidyGetChild(child) )
        {
            duk_push_pointer(ctx, (void*)child);
            duk_put_prop_index(ctx, -2, i++);
        }
        child=tidyGetNext(child);
    }

    /* get body elements */
    child = tidyGetChild(body);
    while(child)
    {
        duk_push_pointer(ctx, (void*)child);
        duk_put_prop_index(ctx, -2, i++);
        child=tidyGetNext(child);
    }

    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("nodes"));
    tidyBufFree( &errbuf );
}

#define pendappend  0
#define pendprepend 1
#define pendafter   2
#define pendbefore  3
#define pendreplace 4
#define pendadd     5

#define INSERTNODES 0
#define THISOBJECT  1
#define THISNODES   2
#define RETNODES    3
#define DELNODES    4

static duk_ret_t _pend(duk_context *ctx, int type)
{
    int i=0, j=0, len, ilen, fromstring=0, src_is_ext=0, dlen=0;
    duk_uarridx_t rep_idx=0;
    TidyNode node, ret_node;
    TidyDoc tdoc, srcdoc;
    pendfunc pf;
    if(duk_is_string(ctx, 0))
    {
        const char *str=duk_get_string(ctx,0);

        _htmlparsefrag(ctx, str);
        fromstring=1;
        duk_remove(ctx, 0);
    }
    /* the nodes to insert */
    if( !duk_is_object(ctx,0) || !duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("nodes")))
        RP_THROW(ctx, "html.append - first argument must be an html object");

    /* get the doc pointer from the source nodes */
    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("tdoc"));
    srcdoc=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    duk_remove(ctx, 0);

    ilen=duk_get_length(ctx, 0);

    duk_push_this(ctx);

    duk_get_prop_string(ctx, 1, DUK_HIDDEN_SYMBOL("tdoc"));
    tdoc=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("nodes"));
    len=duk_get_length(ctx, -1);
    /* stack = [ nodes_to_insert this object_nodes ] */

    duk_push_array(ctx);
    /* stack = [ nodes_to_insert this object_nodes return_nodes ] */

    if(ilen==0)
    {
        /* nothing to do, return a copy */
        for(i=0;i<len;i++)
        {
            duk_get_prop_index(ctx, THISNODES, (duk_uarridx_t)i);
            duk_put_prop_index(ctx, RETNODES, (duk_uarridx_t)i);
        }
        new_ret_object(ctx, RETNODES);
        return 1;
    }

    if(!duk_get_prop_string(ctx, 1, DUK_HIDDEN_SYMBOL("root")))
        RP_THROW(ctx, "html: error - document root not found");
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("dnodes"));
    dlen=duk_get_length(ctx, -1);
    /* stack = [ nodes_to_insert this object_nodes return_nodes root dnodes ] */

    duk_remove(ctx, -2);
    /*                0            1      2              3         4
     * stack = [ nodes_to_insert this object_nodes return_nodes dnodes ] */

    /* if coming from another TidyDoc, use _ext function */
    if( tdoc != srcdoc)
        src_is_ext=1;

    /* appending list of nodes
       done here. no pfunc.    */
    if(type==pendadd)
    {

        /* push current nodes into new list */
        for (i=0;i<len;i++)
        {
            duk_get_prop_index(ctx, THISNODES, (duk_uarridx_t)i);
            duk_put_prop_index(ctx, RETNODES, (duk_uarridx_t)i);
        }

        if(src_is_ext)
        {
            /* copy nodes from another doc into new list*/
            for (j=0;j<ilen;j++)
            {
                duk_get_prop_index(ctx, INSERTNODES, (duk_uarridx_t)j);
                node=duk_get_pointer(ctx, -1);
                duk_pop(ctx);
                node=dupnode_ext(tdoc, srcdoc, node);

                /* the nodes are detached, so must go in the dnodes list */
                duk_push_pointer(ctx, (void*)node);
                duk_dup(ctx, -1);
                duk_put_prop_index(ctx, RETNODES, (duk_uarridx_t)i++);
                /* the dnodes array */
                duk_put_prop_index(ctx, DELNODES, (duk_uarridx_t)dlen++);
            }

        }
        else
        {
            /* push node pointers from this doc into new list*/
            for (j=0;j<ilen;j++)
            {
                duk_get_prop_index(ctx, INSERTNODES, (duk_uarridx_t)j);
                duk_put_prop_index(ctx, RETNODES, (duk_uarridx_t)i++);
            }
        }

        if(fromstring)
            tidyRelease(srcdoc);

        new_ret_object(ctx, RETNODES);
        return 1;
    }

    /* for everything not pendadd */

#define addtodelnodes(node) do { \
    duk_push_pointer(ctx, (void*)(node));\
    duk_put_prop_index(ctx, DELNODES, (duk_uarridx_t)dlen++);\
}while(0)

#define addtoretnodes(node) do { \
    duk_push_pointer(ctx, (void*)(node));\
    duk_put_prop_index(ctx, RETNODES, rep_idx++);\
}while(0)

    {
        TidyNode ins_nodes[ilen];

        for (j=0; j<ilen; j++)
        {
            duk_get_prop_index(ctx, INSERTNODES, (duk_uarridx_t)j);
            ins_nodes[j]=(TidyNode)duk_get_pointer(ctx, -1);
            duk_pop(ctx);
        }

        for(i=0;i<len;i++)
        {
            duk_get_prop_index(ctx, THISNODES, (duk_uarridx_t)i);
            node=(TidyNode)duk_get_pointer(ctx, -1);
            duk_pop(ctx);

            /* set here because it will change for pendrepace below */
            pf = pfunc[type][src_is_ext];

            /* for append, prepend - only current node gets copied to ret list*/
            if(type==pendappend || type==pendprepend)
            {
                /* br et al can't have children but can have siblings*/
                if(isSingletonTag(tidyNodeGetId(node)))
                    continue;

                addtoretnodes(node);

                for (j=0; j<ilen; j++)
                {
                    /* if appending/prepending, always insert node regardless of isdetached,
                       since "node" will be the parent */
                    ret_node = (pf)(tdoc, srcdoc, node, ins_nodes[j]);

                    /* after first one is in place, the rest are appended after it*/
                    if (!j)
                        pf=pfunc[pendafter][src_is_ext];

                    node=ret_node;
                }
            }
            else
            /* after, before, replace */
            /* - for after, current node gets copied to new list next, and ins_nodes
                 subsequently get copied in j loop below.
               - for before, insert current node below, and insert ins_nodes
                 in j loop below
               - Don't copy current for replace */
            {
                TidyNode orignode=node, dnode;
                int isdetached=0;
                /* check if the current node is on the dnodes list */
                for (j=0; j<dlen; j++)
                {
                    duk_get_prop_index(ctx, DELNODES, (duk_uarridx_t)j);
                    dnode=(TidyNode)duk_get_pointer(ctx, -1);
                    if(dnode == node)
                    {
                        isdetached=1;
                        break;
                    }
                }
                if(type==pendafter)
                    addtoretnodes(node);
                else if(type==pendreplace && !isdetached)
                    addtodelnodes(node);

                for (j=0; j<ilen; j++)
                {
                    /* only insert if not detached
                       if putting before/after/replacing and is detached, there is no parent */
                    if(!isdetached)
                        ret_node = (pf)(tdoc, srcdoc, node, ins_nodes[j]);

                    else if (src_is_ext)
                    /* if detached and ins_nodes[j] is from another tree,
                       copy here because not doing (pf)() above             */
                    {
                        ret_node = dupnode_ext(tdoc, srcdoc, ins_nodes[j]);
                        /*if to a detached node, make new node detached too */
                        addtodelnodes(ret_node);
                    }

                    else
                        ret_node=ins_nodes[j];

                    /* after first one is in place, the rest are appended after it*/
                    if (!j)
                        pf=pfunc[pendafter][src_is_ext];

                    addtoretnodes(ret_node);
                    node=ret_node;
                }
                if(type==pendbefore)
                    addtoretnodes(orignode);
            }
        }
    }

    if(fromstring)
        tidyRelease(srcdoc);

    new_ret_object(ctx, RETNODES);
    return 1;
}

duk_ret_t duk_rp_html_add(duk_context *ctx)
{
    return _pend(ctx, pendadd);
}

duk_ret_t duk_rp_html_replace(duk_context *ctx)
{
    return _pend(ctx, pendreplace);
}

duk_ret_t duk_rp_html_before(duk_context *ctx)
{
    return _pend(ctx, pendbefore);
}

duk_ret_t duk_rp_html_after(duk_context *ctx)
{
    return _pend(ctx, pendafter);
}

duk_ret_t duk_rp_html_prepend(duk_context *ctx)
{
    return _pend(ctx, pendprepend);
}

duk_ret_t duk_rp_html_append(duk_context *ctx)
{
    return _pend(ctx, pendappend);
}

static duk_ret_t _detach_delete(duk_context *ctx, int delete)
{
    int i=0, len, dlen;
    TidyNode node;
    TidyDoc tdoc;

    duk_push_this(ctx);
    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("root")))
        RP_THROW(ctx, "html: error - document root not found");

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("dnodes"));
    dlen=duk_get_length(ctx, -1);

    duk_get_prop_string(ctx, -3, DUK_HIDDEN_SYMBOL("tdoc"));
    tdoc=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -3, DUK_HIDDEN_SYMBOL("nodes"));
    len=duk_get_length(ctx, -1);

    /* stack = [ this, root, dnodes nodes ] */

    for(;i<len;i++)
    {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        node=(TidyNode)duk_get_pointer(ctx, -1);

        detachNode(tdoc, node, delete);
/* detach and delete now do the same thing
   except that detach returns the nodes
   cleanup of deleted nodes now happens in finalizer
*/
//        if(!delete)
//        {
            duk_put_prop_index(ctx, -3, (duk_uarridx_t)dlen++);
//        }
//        else
//            duk_pop(ctx);
    }
    if(!delete)
    {
        duk_pull(ctx, 0);
        return 1;
    }
    return 0;
}

duk_ret_t duk_rp_html_detach(duk_context *ctx)
{
    return _detach_delete(ctx, 0);
}

duk_ret_t duk_rp_html_delete(duk_context *ctx)
{
    return _detach_delete(ctx, 1);
}


duk_ret_t duk_rp_html_delattr(duk_context *ctx)
{
    const char *aname = REQUIRE_STRING(ctx, 0, "html.delAttr - first argument must be a string (attr name)");
    int i=0, len;
    TidyNode node;
    TidyDoc tdoc;
    TidyAttr attr;
    ctmbstr key;

    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("tdoc"));
    tdoc=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("nodes"));
    len=duk_get_length(ctx, -1);

    for(;i<len;i++)
    {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        node=(TidyNode)duk_get_pointer(ctx, -1);
        duk_pop(ctx);

        for ( attr=tidyAttrFirst(node); attr; attr=tidyAttrNext(attr) )
        {
            key=tidyAttrName(attr);
            if (!strcasecmp(key, aname))
            {
                tidyAttrDiscard( tdoc, node, attr);
                break;
            }
        }
    }
    duk_pull(ctx, 1);
    return 1;
}

duk_ret_t duk_rp_html_attr(duk_context *ctx)
{
    const char *val, *aname = REQUIRE_STRING(ctx, 0, "html.attr - first argument must be a string (attr name)");
    int i=0, len;
    TidyNode node;
    TidyDoc tdoc;

    if (duk_is_undefined(ctx, 1) )
    {
        duk_pop(ctx);
        return duk_rp_html_getattr(ctx);
    }

    val = REQUIRE_STRING(ctx, 1, "html.attr - second argument must be a string (attr value)");

    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("tdoc"));
    tdoc=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("nodes"));
    len=duk_get_length(ctx, -1);

    for(;i<len;i++)
    {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        node=(TidyNode)duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        addAttr( tdoc, node, aname, val);
    }

    duk_pull(ctx, 2);

    return 1;
}


duk_ret_t duk_rp_html_getelem(duk_context *ctx)
{
    TidyNode node;
    TidyDoc tdoc;
    TidyBuffer buf, *ret;
    int i=0;
    duk_idx_t this_idx;

    ret=&buf;

    duk_push_this(ctx);
    this_idx=duk_get_top_index(ctx);

    duk_push_array(ctx);

    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("tdoc"));
    tdoc=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    /* loop over nodes, create tag, append to array */
    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("nodes"));
    duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
    while(duk_next(ctx, -1, 1))
    {
        tidyBufInit(ret);

        node=duk_get_pointer(ctx, -1);
        duk_pop_2(ctx);

        if(tidyNodeGetType(node) == TidyNode_DocType)
            putdoctype(tdoc, node, ret, tidyNodeGetName(node));
        else
            ret=dumpTag(node, ret);

        if(ret->size)
            duk_push_string(ctx, (const char *)ret->bp);
        else
            duk_push_string(ctx, "");

        duk_put_prop_index(ctx, -4, i);
        tidyBufFree(ret);

        i++;
    }
    duk_pop_2(ctx);

    return 1;
}

duk_ret_t duk_rp_html_getelemname(duk_context *ctx)
{
    TidyNode node;
    int i=0;
    duk_idx_t this_idx;

    duk_push_this(ctx);
    this_idx=duk_get_top_index(ctx);

    duk_push_array(ctx);

    /* loop over nodes, create tag, append to array */
    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("nodes"));
    duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
    while(duk_next(ctx, -1, 1))
    {

        node=duk_get_pointer(ctx, -1);
        duk_pop_2(ctx);

        duk_push_string(ctx, (const char *)tidyNodeGetName(node));

        duk_put_prop_index(ctx, -4, i);

        i++;
    }
    duk_pop_2(ctx);

    return 1;
}

duk_ret_t duk_rp_html_getallattr(duk_context *ctx)
{
    TidyNode node;
    int i=0;
    duk_idx_t this_idx;

    duk_push_this(ctx);
    this_idx=duk_get_top_index(ctx);

    duk_push_array(ctx);

    /* loop over nodes, create tag, append to array */
    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("nodes"));
    duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
    while(duk_next(ctx, -1, 1))
    {
        TidyAttr attr;

        node=duk_get_pointer(ctx, -1);
        duk_pop_2(ctx);

        duk_push_object(ctx);

        for ( attr=tidyAttrFirst(node); attr; attr=tidyAttrNext(attr) )
        {
            duk_push_string(ctx, (const char *) tidyAttrValue(attr));
            duk_put_prop_string(ctx, -2, (const char *) tidyAttrName(attr));
        }

        duk_put_prop_index(ctx, -4, i);

        i++;
    }
    duk_pop_2(ctx);

    return 1;
}

duk_ret_t duk_rp_html_children(duk_context *ctx)
{
    int i=0, j=0, len;
    TidyNode node, child;
    int onlytags=0;

    if(!duk_is_undefined(ctx,0))
        onlytags=(int)REQUIRE_BOOL(ctx, 0, "html.children - first argument must be a boolean (ret_tags_only)");

    duk_push_this(ctx);
    duk_push_array(ctx);
    duk_get_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("nodes"));
    len=duk_get_length(ctx, -1);

    for(;i<len;i++)
    {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        node=(TidyNode)duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        for ( child = tidyGetChild(node); child; child = tidyGetNext(child) )
        {
            if(!onlytags || tidyNodeGetType(child)==TidyNode_Start)
            {
                duk_push_pointer(ctx, (void*)child);
                duk_put_prop_index(ctx, 2, (duk_uarridx_t)j++);
            }
        }
    }
    new_ret_object(ctx, 2);
    return 1;
}

#define typeparent 0
#define typenext 1
#define typeprev 2

static duk_ret_t _nextprevpar(duk_context *ctx, int type)
{
    int i=0, j=0, len;
    TidyNode node;

    duk_push_this(ctx);
    duk_push_array(ctx);
    duk_get_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("nodes"));
    len=duk_get_length(ctx, -1);

    for(;i<len;i++)
    {
        TidyNode retnode=NULL;

        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        node=(TidyNode)duk_get_pointer(ctx, -1);
        duk_pop(ctx);

        switch(type)
        {
            case typenext: retnode=tidyGetNext(node); break;
            case typeprev: retnode=tidyGetPrev(node); break;
            case typeparent: retnode=tidyGetParent(node); break;
        }

        if(retnode)
        {
            duk_push_pointer(ctx, (void*)retnode);
            duk_put_prop_index(ctx, 1, (duk_uarridx_t)j++);
        }
    }
    new_ret_object(ctx, 1);
    return 1;
}

duk_ret_t duk_rp_html_parent(duk_context *ctx)
{
    return _nextprevpar(ctx, typeparent);
}

duk_ret_t duk_rp_html_next(duk_context *ctx)
{
    return _nextprevpar(ctx, typenext);
}

duk_ret_t duk_rp_html_prev(duk_context *ctx)
{
    return _nextprevpar(ctx, typeprev);
}


duk_ret_t duk_rp_html_getattr(duk_context *ctx)
{
    const char *val, *aname = REQUIRE_STRING(ctx, 0, "html.getAttr - first argument must be a string (attr name)");
    int i=0, len;
    TidyNode node;

    duk_push_this(ctx);
    duk_push_array(ctx);
    duk_get_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("nodes"));
    len=duk_get_length(ctx, -1);

    for(;i<len;i++)
    {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        node=(TidyNode)duk_get_pointer(ctx, -1);
        duk_pop(ctx);

        val=getAttr(node, aname);
        if(val)
            duk_push_string(ctx,val);
        else
            duk_push_string(ctx,"");

        duk_put_prop_index(ctx, 2, (duk_uarridx_t)i);
    }
    duk_pull(ctx, 2);
    return 1;
}

duk_ret_t duk_rp_html_slice(duk_context *ctx)
{
    int start=0;
    int end=0;
    int i=0,j=0,len;

    duk_push_this(ctx);
    duk_push_array(ctx);
    duk_get_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("nodes"));

    len=(int)duk_get_length(ctx, 4);

    if(!duk_is_undefined(ctx,0))
        start=REQUIRE_INT(ctx, 0, "html.slice - first argument must be an int (start)");

    if(!duk_is_undefined(ctx,1))
        end=REQUIRE_INT(ctx, 1, "html.slice - second argument must be an int (end)");
    else
        end=len;

    if(end<0) end = len+end;
    if(start<0) start = len+start;

    if(len<end)end=len;

    for (i=start;i<end;i++)
    {
        duk_get_prop_index(ctx, 4, (duk_uarridx_t)i);
        duk_put_prop_index(ctx, 3, (duk_uarridx_t)j++);
    }
    new_ret_object(ctx, 3);
    return 1;
}

duk_ret_t duk_rp_html_eq(duk_context *ctx)
{
    int start=REQUIRE_INT(ctx, 0, "html.eq - first must be an int (index)");

    duk_push_int(ctx, start+1);
    return duk_rp_html_slice(ctx);
}


duk_ret_t duk_rp_html_find_(duk_context *ctx, int findtype, int filter)
{
    const char **txts=NULL;
    int ntxt=1;

    if(duk_is_string(ctx, 0))
    {
        REMALLOC(txts, sizeof(const char *));
        txts[0]=duk_get_string(ctx,0);
    }
    else if (duk_is_array(ctx,0))
    {
        int i=0;

        ntxt=(int)duk_get_length(ctx, 0);
        REMALLOC(txts, ntxt * sizeof(const char *));

        for (;i<ntxt;i++)
        {
            duk_get_prop_index(ctx, 0, (duk_uarridx_t)i);
            if(duk_is_string(ctx, -1))
                txts[i]=duk_get_string(ctx, -1);
            else
            {
                free(txts);
                RP_THROW(ctx, "html.find - first argument must be a string or array of strings");
            }
            duk_pop(ctx);
        }
    }
    else
        RP_THROW(ctx, "html.find - first argument must be a string or array of strings");

    /* the array to hold new node pointers */
    duk_push_array(ctx);
    _findtxts(ctx, 1, txts, ntxt, findtype, filter);

    if(filter!=2)
        new_ret_object(ctx, 1);

    free(txts);

    return 1;
}

duk_ret_t duk_rp_html_findtag(duk_context *ctx)
{
    return duk_rp_html_find_(ctx, findTag, 0);
}

duk_ret_t duk_rp_html_findattr(duk_context *ctx)
{
    return duk_rp_html_find_(ctx, findAttr, 0);
}

duk_ret_t duk_rp_html_findclass(duk_context *ctx)
{
    return duk_rp_html_find_(ctx, findClass, 0);
}

duk_ret_t duk_rp_html_filtertag(duk_context *ctx)
{
    return duk_rp_html_find_(ctx, findTag, 1);
}

duk_ret_t duk_rp_html_filterattr(duk_context *ctx)
{
    return duk_rp_html_find_(ctx, findAttr, 1);
}

duk_ret_t duk_rp_html_filterclass(duk_context *ctx)
{
    return duk_rp_html_find_(ctx, findClass, 1);
}

duk_ret_t duk_rp_html_hastag(duk_context *ctx)
{
    return duk_rp_html_find_(ctx, findTag, 2);
}

duk_ret_t duk_rp_html_hasattr(duk_context *ctx)
{
    return duk_rp_html_find_(ctx, findAttr, 2);
}

duk_ret_t duk_rp_html_hasclass(duk_context *ctx)
{
    return duk_rp_html_find_(ctx, findClass, 2);
}

duk_ret_t duk_rp_html_pp(duk_context *ctx)
{
    TidyBuffer output = {0};
    TidyDoc tdoc;

    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("tdoc"));
    tdoc=duk_get_pointer(ctx, -1);
    tidySaveBuffer(tdoc, &output);

    duk_push_string(ctx, (char *)output.bp);

    if (output.bp)
        tidyBufFree( &output );

    return 1;
}

#define htmlSetErr(e) do{\
    if(e<0) RP_THROW(ctx, "html.newDocument() - %s", strerror(-e));\
    if(tidy_errbuf->size && e>0) {\
        duk_push_string(ctx, (char *)tidy_errbuf->bp);\
        duk_replace(ctx, err_idx);\
    }\
} while (0)

// turn thisOption into this-option
static char * fixkey(const char *key)
{
    char *ret = NULL;
    int i=0;

    REMALLOC(ret, strlen(key) * 2);
    while(*key)
    {
        if(i && isupper(*key))
        {
            ret[i++] = '-';
            ret[i++] = tolower(*key);
        }
        else
            ret[i++] = *key;

        key++;
    }

    ret[i]='\0';
    return ret;
}

duk_ret_t duk_rp_htmlparse(duk_context *ctx)
{
//    const char *html = REQUIRE_STRING(ctx, 0, "html.newDocument: first argument must be a string (html document)");
    const char *html="";
    int Terr=0;
    duk_idx_t err_idx, obj_idx=-1, str_idx=0;
    TidyDoc tdoc;
    TidyBuffer *tidy_errbuf = NULL;
    TidyNode root;
    duk_size_t size=0;

    if(duk_is_object(ctx, 0))
    {
        obj_idx = 0;
        str_idx = 1;
    }
    else if(duk_is_object(ctx, 1))
    {
        obj_idx = 1;
        str_idx = 0;
    }

    if(duk_is_buffer_data(ctx, str_idx))
        html = (const char *) duk_get_buffer_data(ctx, str_idx, &size);
    else if (duk_is_string(ctx, str_idx) )
        html = duk_get_string(ctx, str_idx);
    else if (!duk_is_undefined(ctx, str_idx))
        RP_THROW(ctx, "html.newDocument: first argument must be a string or buffer(html document)");

    tidy_errbuf = calloc( 1, sizeof(TidyBuffer));

    duk_push_object(ctx);
    duk_push_string(ctx,"");
    err_idx=duk_get_top_index(ctx);

    tdoc = tidyCreate();
    tidyOptSetBool(tdoc, TidyForceOutput, yes);
    tidyOptSetBool(tdoc, TidyMark, no);
    tidyOptSetBool(tdoc, TidyDropEmptyElems, no);
    tidySetErrorBuffer( tdoc, tidy_errbuf );

    if(obj_idx > -1 && !duk_is_function(ctx, obj_idx) && !duk_is_array(ctx, obj_idx) )
    {
        duk_enum(ctx, obj_idx, 0);
        while(duk_next(ctx, -1, 1))
        {
            const char *key=duk_get_string(ctx, -2);
            const char *val=duk_safe_to_string(ctx, -1);
            char *dashedKey = fixkey(key);
            int ret=tidyOptParseValue(tdoc, (ctmbstr)dashedKey, (ctmbstr)val);
            free(dashedKey);
            if(!ret)
                RP_THROW(ctx, "html.newDocument - error setting '%s' to '%s' - %s", key, val,tidy_errbuf->bp);
            duk_pop_2(ctx);
        }
        duk_pop(ctx);
    }

    if (size)
    {
        TidyBuffer hbuf;

        tidyBufInit(&hbuf);
        tidyBufAttach(&hbuf, (byte *)html, (uint)size);
        Terr=tidyParseBuffer(tdoc, &hbuf);
        /* don't tidyBufFree(&hbuf).  Apparently tidyBufFree doesn't know what it has got*/
    }
    else
    {
        Terr=tidyParseString(tdoc, html);
        htmlSetErr(Terr);
    }

    Terr=tidyCleanAndRepair(tdoc);
    htmlSetErr(Terr);

    duk_put_prop_string(ctx, -2, "errMsg");


    duk_push_pointer(ctx, (void *)tdoc);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("tdoc"));

    duk_push_pointer(ctx, (void *)tidy_errbuf);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("errbuf"));

    duk_push_c_function(ctx, duk_rp_html_pp, 0);
    duk_put_prop_string(ctx, -2, "prettyPrint");

    root=tidyGetRoot(tdoc);

    duk_push_array(ctx);
    duk_push_pointer(ctx, (void *)root);
    duk_put_prop_index(ctx, -2, 0);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("nodes"));

    duk_push_array(ctx);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("dnodes"));

    duk_push_c_function(ctx, duk_rp_html_finalizer, 1);
    duk_set_finalizer(ctx, -2);

    duk_push_number(ctx, 1);
    duk_put_prop_string(ctx, -2, "length");

    pushfuncs(ctx);

    duk_dup(ctx, -1);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("root"));
    return 1;

}



/* **************************************************
   Initialize module
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
  duk_push_object(ctx); // the return object

  duk_push_c_function(ctx, duk_rp_htmlparse, 2);

  duk_put_prop_string(ctx, -2, "newDocument");

  return 1;
}
