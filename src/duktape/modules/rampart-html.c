/* Copyright (C) 2026  Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
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

//keep track of how many docs haven't been finalized
__thread int rp_html_newdocs=0;


duk_ret_t duk_rp_html_finalizer(duk_context *ctx)
{
    TidyBuffer *errbuf;
    TidyDoc tdoc;
    int i=0, len;
    Node* node;
    TidyDocImpl* doc;

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("validity"));
    duk_get_prop_string(ctx, -1, "valid");
    if(!duk_get_boolean(ctx, -1))
    {
        duk_pop_2(ctx);
        return 0;
    }
    duk_pop(ctx);

    duk_push_false(ctx);
    duk_put_prop_string(ctx, -2, "valid");
    duk_pop(ctx);



    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("errbuf"));
    errbuf=duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    if (errbuf && errbuf->bp)
        tidyBufFree( errbuf );
    free(errbuf);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("tdoc"));
    tdoc=duk_get_pointer(ctx, -1);
    doc = tidyDocToImpl( tdoc );
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("dnodes"));
    /* dnodes can hold a node twice, and TY_(FreeNode) twice is a double
       free -- valgrind caught it from fuzz.js. De-duplicate first. */
    uniq_array_nodes(ctx, -1);
    len=duk_get_length(ctx, -1);

    for (i=0; i<len; i++)
    {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        node=(Node*)duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        TY_(FreeNode)( doc, node);
    }

    tidyRelease( tdoc );
    rp_html_newdocs--;

    return 0;
}

duk_ret_t duk_rp_html_destroy(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_rp_html_finalizer(ctx);

    duk_del_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("tdoc"));
    duk_del_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("dnodes"));
    duk_del_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("nodes"));
    duk_del_prop_string(ctx, 0, "length");
    duk_del_prop_string(ctx, 0, "errMsg");
    duk_pull(ctx, 0);
    return 1;
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

/* NULL_ATTR_NAME: tidyAttrName() can be NULL, not just tidyAttrValue():
   malformed markup like "<A <?" yields a nameless attribute. Skip it in
   every tidyAttrFirst() loop or str[n]casecmp()/strlen() segfaults. */
const char *getAttr(TidyNode node, const char *name)
{
    TidyAttr attr;
    ctmbstr key;

    for ( attr=tidyAttrFirst(node); attr; attr=tidyAttrNext(attr) )
    {
        key=tidyAttrName(attr);
        if (!key) continue; /* nameless attr: malformed markup, see NULL_ATTR_NAME */
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
        if (!key) continue; /* nameless attr: malformed markup, see NULL_ATTR_NAME */
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
        /* av->attribute is what tidyAttrName() exposes, so it is NULL
           for a nameless attribute here too. See NULL_ATTR_NAME. */
        if (!av->attribute)
            continue;
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

    if (fpi && fpi->value && sys && sys->value)
    {
        tidyBufAppend(buf, " PUBLIC ", 8);
        tidyBufPutByte(buf, fpi->delim);
        tidyBufAppend(buf, fpi->value, strlen(fpi->value) );
        tidyBufPutByte(buf, fpi->delim);
        tidyBufAppend(buf, " ", 1);
        tidyBufPutByte(buf, sys->delim);
        tidyBufAppend(buf, sys->value, strlen(sys->value) );
        tidyBufPutByte(buf, sys->delim);
    }
    else if (fpi && fpi->value)
    {
        tidyBufAppend(buf, " PUBLIC ", 8);
        tidyBufPutByte(buf, fpi->delim);
        tidyBufAppend(buf, fpi->value, strlen(fpi->value) );
        tidyBufPutByte(buf, fpi->delim);
    }
    // bug fix: changed fpi->value to sys->value in SYSTEM doctype output - 2026-02-27
    else if (sys && sys->value)
    {
        tidyBufAppend(buf, " SYSTEM ", 8);
        tidyBufPutByte(buf, sys->delim);
        tidyBufAppend(buf, sys->value, strlen(sys->value) );
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

static void pushdoctype( duk_context *ctx, TidyDoc tdoc, TidyNode tnod, ctmbstr name)
{
    Node *node = tidyNodeToImpl( tnod );
    TidyDocImpl* doc = tidyDocToImpl( tdoc );
    AttVal* fpi = TY_(GetAttrByName)(node, "PUBLIC");
    AttVal* sys = TY_(GetAttrByName)(node, "SYSTEM");
    duk_uarridx_t i=0;

    duk_push_object(ctx);
    duk_push_string(ctx, "!DOCTYPE");
    duk_put_prop_string(ctx, -2, "type");

    duk_push_array(ctx);//attributes go in array here.
    duk_push_string(ctx, (const char*)name);
    duk_put_prop_index(ctx, -2, i++);

    if (fpi && fpi->value && !sys)
    {
        duk_push_string(ctx, "PUBLIC");
        duk_put_prop_index(ctx, -2, i++);
    }
    else if (sys && sys->value)
    {
        duk_push_string(ctx, "SYSTEM");
        duk_put_prop_index(ctx, -2, i++);
    }
    duk_put_prop_string(ctx, -2, "attributes");

    if (node->content)
    {
        Node *cont = node->content;
        duk_size_t len = (duk_size_t)(cont->end - cont->start);

        duk_push_array(ctx);
        duk_push_lstring(ctx, (const char *)&(doc->lexer->lexbuf[cont->start]), len);
        duk_put_prop_index(ctx, -2, 0);
        duk_put_prop_string(ctx, -2, "contents");
    }

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

        if(!k) continue; /* nameless attr: nothing to serialize, see NULL_ATTR_NAME */
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

void dumpHtmlObj(duk_context *ctx, TidyDoc doc, TidyNode node)
{
    TidyNode child=NULL;

    TidyNodeType type = tidyNodeGetType(node);
    //TidyTagId id= tidyNodeGetId(node);
    ctmbstr name = tidyNodeGetName(node);

    switch(type)
    {
        case TidyNode_Start:
        case TidyNode_StartEnd:
        {
            TidyAttr attr;

            duk_push_object(ctx);// return obj

            child = tidyGetChild(node);

            // { type: "div" }
            duk_push_string(ctx, (const char*)name);
            duk_put_prop_string(ctx, -2, "type");
            // { type: "div", attributes: [obj] }
            attr=tidyAttrFirst(node);
            if(attr)
            {
                duk_push_object(ctx);
                for (attr=tidyAttrFirst(node); attr; attr=tidyAttrNext(attr))
                {
                    const char *k = (const char *) tidyAttrName(attr);
                    const char *v = (const char *) tidyAttrValue(attr);

                    /* nameless attr cannot be an object key, see NULL_ATTR_NAME */
                    if(!k) continue;

                    if(v)
                        duk_push_string(ctx, v);
                    else
                        duk_push_string(ctx,k);

                    duk_put_prop_string(ctx, -2, k);
                }
                duk_put_prop_string(ctx, -2, "attributes");
            }
            // { type: "div", attributes: [obj], contents:[ary] }
            if(child)
            {
                duk_uarridx_t l=0;

                duk_push_array(ctx);

                for ( ; child; child = tidyGetNext(child) )
                {
                    dumpHtmlObj(ctx, doc, child);
                    duk_put_prop_index(ctx, -2, l++);
                }

                duk_put_prop_string(ctx, -2, "contents");
            }
            break;
        }
        case TidyNode_DocType:
            duk_push_object(ctx);
            duk_push_string(ctx, "document");
            duk_put_prop_string(ctx, -2, "type");
            duk_push_array(ctx);
            pushdoctype(ctx, doc, node, name);
            duk_put_prop_index(ctx, -2, 0);

            node=tidyGetNext(node);
            if(node)
            {
                dumpHtmlObj(ctx, doc, node);
                duk_put_prop_index(ctx, -2, 1);
            }
            duk_put_prop_string(ctx, -2, "contents");
            break;

        case TidyNode_Comment:
        case TidyNode_Text:
        {
            TidyBuffer tbuf;
            tidyBufInit(&tbuf);
            tidyNodeGetText(doc, node, &tbuf);
            /* empty buffer reads bp[-1]; size>0 is guarded elsewhere */
            if(tbuf.bp && tbuf.size)
                duk_push_lstring(ctx, (const char*)tbuf.bp, (duk_size_t)(tbuf.size - (tbuf.bp[tbuf.size-1]=='\n' ? 1 : 0)));
            else
                duk_push_string(ctx, "");
            tidyBufFree (&tbuf);
            break;
        }
        case TidyNode_Root:
            child = tidyGetChild(node);
            if(child)
                dumpHtmlObj(ctx, doc, child);
            break;
        default:
            break;
    }
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
                //size_t vlen=0;

                if(!k) continue; /* nameless attr: nothing to serialize, see NULL_ATTR_NAME */

                tidyBufAppend(buf, " ", 1);
                tidyBufAppend(buf, (void*)k, strlen(k));

                if(v)
                {
                     //vlen=strlen(v);
                     tidyBufAppend(buf, "=\"", 2);
                     while(*v)
                     {
                         if(*v=='"')
                             tidyBufAppend(buf, "&quot;",6);
                         else
                             tidyBufAppend(buf, (void*)v, 1);
                         v++;
                     }
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
            /* empty buffer reads bp[-1] */
            if(tbuf.bp && tbuf.size)
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

static char *singletons[] = {
        "br",
        "input",
        "link",
        "meta",
        "!doctype",
        "col",
        "area",
        "base",
        "param",
        "track",
        "wbr",
        "keygen",
        NULL
};

static int is_type_singleton(const char *tag)
{
    char **s=singletons;
    while(*s!=NULL)
    {
        if(!strcasecmp(*s,tag))
            return 1;
        s++;
    }
    return 0;
}

TidyBuffer * obj_to_html_inner(duk_context *ctx, duk_idx_t obj_idx, TidyBuffer *buf) {

    duk_uarridx_t i=0, len;
    int issingleton=0, isdoc=0;
    const char *type=NULL, *key, *val;
    duk_size_t slen, tlen=0;

    if( !duk_is_object(ctx, obj_idx) || duk_is_array(ctx, obj_idx) || duk_is_function(ctx, obj_idx) )
        return buf;

    obj_idx = duk_normalize_index(ctx, obj_idx);
    /* Non-string "type" treated as absent: duk_get_lstring() returns
       NULL for a number/bool/null/array and strcasecmp() then segvs.
       Not a throw -- callers own a TidyBuffer a longjmp would leak. */
    if(duk_get_prop_string(ctx, obj_idx, "type") && duk_is_string(ctx, -1))
    {
        type=duk_get_lstring(ctx, -1, &tlen);

        isdoc = !strcasecmp("document",type);
        if(!isdoc)
        {
            issingleton = is_type_singleton(type);

            tidyBufAppend(buf, "<", 1);
            tidyBufAppend(buf, (void*)type, (uint)tlen);

            if(duk_get_prop_string(ctx, obj_idx, "attributes"))
            {
                if (duk_is_array(ctx, -1)) {
                    len = duk_get_length(ctx, -1);
                    for(i=0;i<len;i++){
                        duk_get_prop_index(ctx, -1, i);
                        if(duk_is_string(ctx, -1))
                        {
                            tidyBufAppend(buf, " ", 1);
                            key = duk_get_lstring(ctx, -1, &slen);
                            tidyBufAppend(buf, (void*)key, (uint)slen);
                        }
                        duk_pop(ctx);
                    }
                }
                else if (duk_is_object(ctx, -1) && !duk_is_function(ctx, -1))
                {
                    duk_enum(ctx, -1, 0);
                    while(duk_next(ctx,-1,1))
                    {
                        key=duk_get_lstring(ctx, -2, &slen);
                        val=duk_to_string(ctx, -1);
                        tidyBufAppend(buf, " ", 1);
                        tidyBufAppend(buf, (void*)key, (uint)slen);

                        tidyBufAppend(buf, "=\"", 2);
                        while(*val)
                        {
                            if(*val=='"')
                                tidyBufAppend(buf, "&quot;",6);
                            else
                                tidyBufAppend(buf, (void*)val, 1);
                            val++;
                        }
                        tidyBufAppend(buf, "\"", 1);

                        duk_pop_2(ctx);
                    }
                    duk_pop(ctx);//enum
                }
            }
            tidyBufAppend(buf,">",1);
            duk_pop(ctx);
        }
    }
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, obj_idx, "contents"))
    {
        if(duk_is_array(ctx, -1))
        {
            len=duk_get_length(ctx, -1);
            for(i=0;i<len;i++)
            {
                duk_get_prop_index(ctx, -1, i);
                if(duk_is_string(ctx, -1))
                {
                    val=duk_get_lstring(ctx, -1, &slen);
                    tidyBufAppend(buf, (void*)val, (uint)slen);
                }
                else
                    buf = obj_to_html_inner(ctx, -1, buf);
                duk_pop(ctx);
            }
        }
    }
    duk_pop(ctx);
    if(type && !isdoc && !issingleton)
    {
        tidyBufAppend(buf, "</", 2);
        tidyBufAppend(buf, (void*)type, (uint)tlen);
        tidyBufAppend(buf, ">", 1);
    }
    return buf;
}

static inline duk_ret_t _obj_to_html(duk_context *ctx, duk_idx_t obj_idx)
{
    duk_uarridx_t i=0, len;
    TidyBuffer buf, *ret;

    ret=&buf;
    tidyBufInit(ret);

    if(duk_is_array(ctx, obj_idx))
    {
        len = duk_get_length(ctx, obj_idx);
        for(i=0;i<len;i++)
        {
            duk_get_prop_index(ctx, obj_idx, i);
            ret=obj_to_html_inner(ctx, -1, ret);
        }
    } else if ( duk_is_object(ctx, obj_idx) && !duk_is_function(ctx, obj_idx) ){
        ret=obj_to_html_inner(ctx, obj_idx, ret);
    }
    else
        RP_THROW(ctx, "html.objToHtml - argument must be an Object or an Array of Objects");

    duk_push_string(ctx, (const char *)ret->bp);
    tidyBufFree(ret);

    return 1;
}

static duk_ret_t obj_to_html(duk_context *ctx)
{
    return _obj_to_html(ctx, 0);
}

TidyBuffer *dumpText(TidyDoc doc, TidyNode start, TidyBuffer *buf, int listno, int listind, int tag_end_nl, int opts, int *inpre)
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
                /* empty buffer reads bp[-1] */
                if(tbuf.bp && tbuf.size)
                    tidyBufAppend(
                        buf,
                        tbuf.bp,
                        tbuf.size - (tbuf.bp[tbuf.size-1]=='\n' ? !*inpre : 0)
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
    while(i--)tidyBufAppend(buf, "   ", 3);\
} while(0)

#define isBlockNotCell(id) ( isBlockTag((id)) && id!=TidyTag_TD && id!=TidyTag_TH )

                if (isBlockNotCell(id) && buf->size>0 && buf->bp[buf->size -1] != '\n')
                    tidyBufAppend(buf, "\n", 1);
                else if( (id == TidyTag_TD || id == TidyTag_TH) && (lastid == TidyTag_TD || lastid == TidyTag_TH) )
                    tidyBufAppend(buf, "   ", 3);

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
                        tidyBufAppend(buf, "   ", 3);
                    }
                    else if (id==TidyTag_DD && testOpt(optEnumLsts) )
                    {
                        tag_indent(listind);
                        tidyBufAppend(buf, "   ", 3);
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
                                    if(len > 1048576) len = 1048576; /* F18: bound stack VLA */
                                    char lbuf[len];
                                    char *tname = (metaType==1) ? "description: " : "keywords: ";

                                    snprintf(lbuf, len, "%s%s\n", tname, cont);
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
                            if(len > 1048576) len = 1048576; /* F18: bound stack VLA */
                            char lbuf[len];
                            char *space = testOpt(optImgLinks)? "" : " ";

                            snprintf(lbuf, len, "%s%s%s", space,alttext,space);
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
                                if(len > 1048576) len = 1048576; /* F18: bound stack VLA */
                                char lbuf[len];

                                snprintf(lbuf, len, "%s\n", title);
                                tidyBufAppend(buf, lbuf, len-1);
                            }
                        }
                    }

                    if(id == TidyTag_PRE)
                    {
                        *inpre=1;
                    }

                    // recurse for child nodes
                    buf=dumpText(doc, child, buf, listno, listind+indent, tag_end_nl, opts, inpre);

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
                                    if(len > 1048576) len = 1048576; /* F18: bound stack VLA */
                                    char lbuf[len];

                                    snprintf(lbuf, len, "(%s \"%s\")", src,title);
                                    tidyBufAppend(buf, lbuf, len-1);
                                }
                                else
                                {
                                    if(len > 1048576) len = 1048576; /* F18: bound stack VLA */
                                    char lbuf[len];

                                    snprintf(lbuf, len, "(%s)", src);
                                    tidyBufAppend(buf, lbuf, len-1);
                                }
                            }
                            else
                            {
                                int len = strlen(src)+3;
                                if(len > 1048576) len = 1048576; /* F18: bound stack VLA */
                                char lbuf[len];

                                snprintf(lbuf, len, "(%s)", src);
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
                                    if(len > 1048576) len = 1048576; /* F18: bound stack VLA */
                                    char lbuf[len];

                                    snprintf(lbuf, len, "(%s \"%s\")", href, title);
                                    tidyBufAppend(buf, lbuf, len-1);
                                }
                                else
                                {
                                    if(len > 1048576) len = 1048576; /* F18: bound stack VLA */
                                    char lbuf[len];

                                    snprintf(lbuf, len, "(%s)", href);
                                    tidyBufAppend(buf, lbuf, len-1);
                                }
                            }
                            else
                            {
                                int len = strlen(href)+3;
                                if(len > 1048576) len = 1048576; /* F18: bound stack VLA */
                                char lbuf[len];

                                snprintf(lbuf, len, "(%s)", href);
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
            case TidyNode_End:
                if(id == TidyTag_PRE)
                {
                    *inpre=0;
                }
                break;
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

static void *get_tdoc(duk_context *ctx, duk_idx_t this_idx)
{
    void *ret=NULL;
    duk_idx_t top=duk_get_top(ctx);

    this_idx=duk_normalize_index(ctx, this_idx);

    if(!duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("validity")))
        goto throw;
    if(!duk_get_prop_string(ctx, -1, "valid"))
        goto throw;

    if(!duk_get_boolean(ctx, -1))
        goto throw;

    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("tdoc"));
    ret=duk_get_pointer(ctx, -1);

    if(!ret)
        goto throw;

    duk_set_top(ctx, top);
    return ret;

    throw:

    RP_THROW(ctx, "html: error - the root html object was destroyed");
    return ret; // cuz compiler
}

static duk_ret_t tohtmlobj(duk_context *ctx)
{
    TidyNode start;
    TidyDoc doc;
    int i=0;
    duk_idx_t this_idx;

    duk_push_this(ctx);
    this_idx=duk_get_top_index(ctx);

    doc=get_tdoc(ctx, this_idx);

    duk_push_array(ctx);

    /* loop over nodes, extract text, append to buffer/array */
    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("nodes"));
    duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
    while(duk_next(ctx, -1, 1))
    {
        start=duk_get_pointer(ctx, -1);
        duk_pop_2(ctx);

        dumpHtmlObj(ctx, doc, start);
        duk_put_prop_index(ctx, -4, i);
        i++;
    }
    duk_pop_2(ctx);

    return 1;
}

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

    doc=get_tdoc(ctx, this_idx);
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
    int makearray=1, inpre=0;
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

    doc=get_tdoc(ctx, this_idx);

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

        ret=dumpText(doc, start, ret, 0, 0, 0, opts, &inpre);

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

/* ---------------- toMarkdown ----------------
   dumpMarkdown() walks the tree.  Blocks render into MdOut buffers which are
   prefixed or placed by their container.  Inline content collects in an
   MdRun, bytes tagged by kind, and is escaped at the end of the run, when what
   follows each character is known (even across elements). */

#define mdHeadings   (1U << 0)
#define mdEmphasis   (1U << 1)
#define mdCode       (1U << 2)
#define mdQuote      (1U << 3)
#define mdTables     (1U << 4)
#define mdEscape     (1U << 5)
#define mdLinks      (1U << 6)
#define mdImgLinks   (1U << 7)
#define mdImgAlt     (1U << 8)
#define mdMetaDesc   (1U << 9)
#define mdMetaKeyw   (1U << 10)
#define mdTitleTxt   (1U << 11)
#define mdLinkSpans  (1U << 12)

#define MD_FALLBACK_TEXT 0
#define MD_FALLBACK_SKIP 1

#define MD_MAX_RECURSE 512    /* past this a subtree is flattened to plain text */
#define MD_MAX_PREFIX  32     /* no further quote/list prefix past this nesting */
#define MD_MAX_COLS    1000
#define MD_MAX_ROWSPAN 10000

typedef struct {
    size_t from, to;
    char *href;
} MdSpan;

typedef struct {
    char *p;
    size_t len, cap;
    MdSpan *sp;
    size_t nsp, capsp;
} MdOut;

typedef struct {
    TidyDoc doc;
    unsigned int opts;
    int fallback, blank, plevel, ddlevel;
    int taskok;   /* nothing yet in this list item: a checkbox is a task marker */
} MdCtx;

/* byte kinds in a run */
#define MK_TEXT  0   /* escapable text */
#define MK_LTEXT 1   /* escapable, and inside [...] so brackets always are */
#define MK_SYN   2   /* generated syntax */
#define MK_EM    3   /* generated emphasis marker */
#define MK_CODE  4   /* verbatim */
#define MK_DROP  5   /* removed at the end of the run */

#define MQ_BOLD   0
#define MQ_ITAL   1
#define MQ_STRIKE 2   /* GFM strikethrough */
#define MQ_LINK   3
#define MQ_NEM    3   /* emphasis kinds: the ones before MQ_LINK */

static const char *md_emark[MQ_NEM] = { "**", "*", "~~" };

typedef struct {
    size_t open, close;
    int len, closed;
} MdEm;

typedef struct {
    char *p;
    unsigned char *k;
    size_t len, cap;
    MdSpan *sp;
    size_t nsp, capsp;
    MdEm *em;
    size_t nem, capem;
    int q[4], nq;          /* markers waiting for the first visible character */
    int pspace, pbr;
    int emdepth[MQ_NEM], inlink, ltext, plainlink;
    long emidx[MQ_NEM];      /* open emphasis, index into em */
    long lastclose[MQ_NEM];  /* index of a close marker ending at len, or -1 */
    int single, cell, heading;
    long linkspan;
} MdRun;

static void md_out_init(MdOut *o)
{
    memset(o, 0, sizeof(*o));
}

static void md_out_free(MdOut *o)
{
    size_t i;

    for (i = 0; i < o->nsp; i++)
        free(o->sp[i].href);
    free(o->sp);
    free(o->p);
    md_out_init(o);
}

static void md_put(MdOut *o, const char *s, size_t n)
{
    if (o->len + n + 1 > o->cap)
    {
        o->cap = (o->len + n + 1) * 2 + 64;
        REMALLOC(o->p, o->cap);
    }
    if (n)
        memcpy(o->p + o->len, s, n);
    o->len += n;
    o->p[o->len] = '\0';
}

static void md_add_span(MdOut *o, size_t from, size_t to, const char *href)
{
    if (o->nsp == o->capsp)
    {
        o->capsp = o->capsp ? o->capsp * 2 : 8;
        REMALLOC(o->sp, o->capsp * sizeof(MdSpan));
    }
    o->sp[o->nsp].from = from;
    o->sp[o->nsp].to = to;
    o->sp[o->nsp].href = strdup(href);
    o->nsp++;
}

/* append a piece; sep newlines first if there is anything to separate from */
static void md_add(MdOut *o, const MdOut *piece, int sep)
{
    size_t base, i;

    if (!piece->len && !piece->nsp)
        return;
    if (o->len && piece->len)
        while (sep-- > 0)
            md_put(o, "\n", 1);
    base = o->len;
    md_put(o, piece->p ? piece->p : "", piece->len);
    for (i = 0; i < piece->nsp; i++)
        md_add_span(o, base + piece->sp[i].from, base + piece->sp[i].to, piece->sp[i].href);
}

/* prefix every line.  Blank lines get blank, the first line gets first */
static void md_prefix(MdOut *o, const MdOut *src, const char *first, const char *rest, const char *blank)
{
    size_t i = 0, fi = 0, ti = 0, base;
    size_t flen = strlen(first), rlen = strlen(rest), blen = strlen(blank);
    int atstart = 1, isfirst = 1;
    size_t *nf = NULL, *nt = NULL;

    if (src->nsp)
    {
        REMALLOC(nf, src->nsp * sizeof(size_t));
        REMALLOC(nt, src->nsp * sizeof(size_t));
    }
    base = o->len;
    for (;; i++)
    {
        if (atstart)
        {
            int isblank = (i >= src->len || src->p[i] == '\n');
            if (isfirst)
                md_put(o, first, flen);
            else if (isblank)
                md_put(o, blank, blen);
            else
                md_put(o, rest, rlen);
            atstart = 0;
            isfirst = 0;
        }
        /* a span starting here starts after the prefix, one ending here before it */
        while (fi < src->nsp && src->sp[fi].from == i)
            nf[fi++] = o->len - base;
        while (ti < src->nsp && src->sp[ti].to == i)
            nt[ti++] = o->len - base;
        if (i >= src->len)
            break;
        md_put(o, src->p + i, 1);
        if (src->p[i] == '\n')
            atstart = 1;
    }
    for (i = 0; i < src->nsp; i++)
        md_add_span(o, base + (i < fi ? nf[i] : 0), base + (i < ti ? nt[i] : 0), src->sp[i].href);
    free(nf);
    free(nt);
}

/* Node text as UTF-8.  Input from JS strings can hold CESU-8 surrogate pairs,
   which come through tidy untouched; bytes and offsets must not depend on that. */
static char *md_utf8(const char *s, size_t n, size_t *outlen)
{
    size_t i, len = n;
    char *conv = duk_rp_cesu8_to_utf8(s, n, &len), *ret = conv;

    if (!ret)
    {
        REMALLOC(ret, n + 1);
        if (n)
            memcpy(ret, s, n);
        ret[n] = '\0';
        len = n;
    }
    /* a lone surrogate has no valid encoding */
    for (i = 0; i + 2 < len; i++)
    {
        if ((unsigned char)ret[i] == 0xED && ((unsigned char)ret[i + 1] & 0xE0) == 0xA0)
        {
            ret[i] = (char)0xEF;
            ret[i + 1] = (char)0xBF;
            ret[i + 2] = (char)0xBD;
        }
    }
    *outlen = len;
    return ret;
}

static char *md_node_value(TidyDoc doc, TidyNode node, size_t *len)
{
    TidyBuffer tbuf;
    char *ret;

    tidyBufInit(&tbuf);
    tidyNodeGetValue(doc, node, &tbuf);
    ret = md_utf8(tbuf.bp ? (const char *)tbuf.bp : "", tbuf.size, len);
    tidyBufFree(&tbuf);
    return ret;
}

static char *md_attr(TidyNode node, const char *name)
{
    size_t len;
    const char *v = getAttr(node, name);

    return v ? md_utf8(v, strlen(v), &len) : NULL;
}

/* All text under a node, walked without recursion.  With flat, block
   elements separate their text with a space. */
static void md_collect_text(MdCtx *c, TidyNode node, MdOut *o, int flat)
{
    TidyNode n = tidyGetChild(node), next;

    while (n)
    {
        TidyNodeType type = tidyNodeGetType(n);
        TidyTagId id = tidyNodeGetId(n);
        int descend = 0;

        if (type == TidyNode_Text)
        {
            size_t len;
            char *s = md_node_value(c->doc, n, &len);

            md_put(o, s, len);
            free(s);
        }
        else if (type == TidyNode_Start || type == TidyNode_StartEnd)
        {
            if (id == TidyTag_BR)
                md_put(o, "\n", 1);
            else if (id != TidyTag_SCRIPT && id != TidyTag_STYLE)
            {
                if (flat && isBlockTag(id))
                    md_put(o, " ", 1);
                descend = 1;
            }
        }
        if (descend && (next = tidyGetChild(n)))
        {
            n = next;
            continue;
        }
        while (n != node && !(next = tidyGetNext(n)))
        {
            n = tidyGetParent(n);
            if (flat && n != node && isBlockTag(tidyNodeGetId(n)))
                md_put(o, " ", 1);
        }
        n = (n == node) ? NULL : next;
    }
}

static void md_run_init(MdRun *r)
{
    int i;

    memset(r, 0, sizeof(*r));

    for (i = 0; i < MQ_NEM; i++)
        r->emidx[i] = r->lastclose[i] = -1;
    r->linkspan = -1;
}

static void md_run_free(MdRun *r)
{
    size_t i;

    for (i = 0; i < r->nsp; i++)
        free(r->sp[i].href);
    free(r->sp);
    free(r->p);
    free(r->k);
    free(r->em);
}

static void md_run_put(MdRun *r, const char *s, size_t n, int kind)
{
    if (r->len + n + 1 > r->cap)
    {
        r->cap = (r->len + n + 1) * 2 + 64;
        REMALLOC(r->p, r->cap);
        REMALLOC(r->k, r->cap);
    }
    if (n)
    {
        memcpy(r->p + r->len, s, n);
        memset(r->k + r->len, kind, n);
    }
    r->len += n;
    r->lastclose[0] = r->lastclose[1] = r->lastclose[2] = -1;
}

static void md_run_puts(MdRun *r, const char *s, int kind)
{
    md_run_put(r, s, strlen(s), kind);
}

/* write the pending whitespace and markers: called before visible output */
static void md_run_flushq(MdRun *r)
{
    int i;

    if (r->pbr)
    {
        if (r->len)
            md_run_puts(r, "\\\n", MK_SYN);
        r->pbr = 0;
        r->pspace = 0;
    }
    else if (r->pspace)
    {
        if (r->len)
            md_run_puts(r, " ", MK_TEXT);
        r->pspace = 0;
    }
    for (i = 0; i < r->nq; i++)
    {
        int t = r->q[i];

        if (t == MQ_LINK)
        {
            if (!r->plainlink)
                md_run_puts(r, "[", MK_SYN);
            r->sp[r->linkspan].from = r->len;
        }
        else
        {
            int mlen = (int)strlen(md_emark[t]);

            if (r->lastclose[t] >= 0)
            {
                /* <b>a</b><b>b</b>: reopen rather than write **a****b** */
                r->emidx[t] = r->lastclose[t];
                r->em[r->emidx[t]].closed = 0;
                r->len -= mlen;
                r->lastclose[0] = r->lastclose[1] = r->lastclose[2] = -1;
                continue;
            }
            if (r->nem == r->capem)
            {
                r->capem = r->capem ? r->capem * 2 : 8;
                REMALLOC(r->em, r->capem * sizeof(MdEm));
            }
            r->em[r->nem].open = r->len;
            r->em[r->nem].len = mlen;
            r->em[r->nem].closed = 0;
            r->emidx[t] = (long)r->nem++;
            md_run_put(r, md_emark[t], mlen, MK_EM);
        }
    }
    r->nq = 0;
}

static void md_run_text(MdRun *r, const char *s, size_t n)
{
    size_t i, start = 0;
    int kind = r->ltext ? MK_LTEXT : MK_TEXT;

    for (i = 0; i <= n; i++)
    {
        if (i == n || s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r' || s[i] == '\f')
        {
            if (i > start)
            {
                if (r->pspace || r->pbr || r->nq)
                    md_run_flushq(r);
                md_run_put(r, s + start, i - start, kind);
            }
            if (i < n)
                r->pspace = 1;
            start = i + 1;
        }
    }
}

static int md_q_remove(MdRun *r, int t)
{
    int i;

    for (i = 0; i < r->nq; i++)
    {
        if (r->q[i] == t)
        {
            for (; i + 1 < r->nq; i++)
                r->q[i] = r->q[i + 1];
            r->nq--;
            return 1;
        }
    }
    return 0;
}

static void md_em_open(MdRun *r, int t)
{
    int *depth = &r->emdepth[t];

    if ((*depth)++ == 0)
        r->q[r->nq++] = t;
}

static void md_em_close(MdRun *r, int t)
{
    int *depth = &r->emdepth[t];
    long idx;

    if (--(*depth) != 0)
        return;
    if (md_q_remove(r, t))  /* nothing visible inside: emit nothing */
        return;
    idx = r->emidx[t];
    r->em[idx].close = r->len;
    r->em[idx].closed = 1;
    md_run_put(r, md_emark[t], strlen(md_emark[t]), MK_EM);
    r->lastclose[t] = idx;
}

/* ](dest "title") */
static void md_run_dest(MdRun *r, const char *dest, const char *title)
{
    const char *s;
    int angle = 0, parens = 0;

    for (s = dest; *s && !angle; s++)
    {
        if ((unsigned char)*s <= ' ' || *s == 0x7f)
            angle = 1;
        else if (*s == '\\' && s[1])
            s++;
        else if (*s == '(')
            parens++;
        else if (*s == ')' && --parens < 0)
            angle = 1;
    }
    if (parens)
        angle = 1;
    md_run_puts(r, "](", MK_SYN);
    if (angle)
    {
        md_run_puts(r, "<", MK_SYN);
        for (s = dest; *s; s++)
        {
            if (*s == '<' || *s == '>')
                md_run_puts(r, "\\", MK_SYN);
            md_run_put(r, s, 1, MK_SYN);
        }
        md_run_puts(r, ">", MK_SYN);
    }
    else
        md_run_puts(r, dest, MK_SYN);
    if (title)
    {
        md_run_puts(r, " \"", MK_SYN);
        for (s = title; *s; s++)
        {
            if (*s == '"' || *s == '\\')
                md_run_puts(r, "\\", MK_SYN);
            md_run_put(r, s, 1, MK_SYN);
        }
        md_run_puts(r, "\"", MK_SYN);
    }
    md_run_puts(r, ")", MK_SYN);
}

static void md_inline_code(MdCtx *c, MdRun *r, TidyNode node)
{
    MdOut t;
    size_t i, run = 0, longest = 0;
    int visible = 0, pad;
    char ticks[66];

    md_out_init(&t);
    md_collect_text(c, node, &t, 0);
    for (i = 0; i < t.len; i++)
    {
        if (t.p[i] == '\n' || t.p[i] == '\r')
            t.p[i] = ' ';
        if (t.p[i] != ' ')
            visible = 1;
        if (t.p[i] == '`')
        {
            if (++run > longest)
                longest = run;
        }
        else
            run = 0;
    }
    if (!visible)
    {
        if (t.len)
            r->pspace = 1;
        md_out_free(&t);
        return;
    }
    if (longest > 63)
        longest = 63;
    memset(ticks, '`', longest + 1);
    ticks[longest + 1] = '\0';
    pad = (t.p[0] == '`' || t.p[t.len - 1] == '`' || (t.p[0] == ' ' && t.p[t.len - 1] == ' '));
    md_run_flushq(r);
    md_run_puts(r, ticks, MK_SYN);
    if (pad)
        md_run_puts(r, " ", MK_SYN);
    md_run_put(r, t.p, t.len, MK_CODE);
    if (pad)
        md_run_puts(r, " ", MK_SYN);
    md_run_puts(r, ticks, MK_SYN);
    md_out_free(&t);
}

static void md_inline_children(MdCtx *c, MdRun *r, TidyNode node, int depth);

static int md_is_block(TidyTagId id)
{
    switch (id)
    {
        case TidyTag_BR:
            return 0;
        case TidyTag_BODY:
        case TidyTag_HTML:
        case TidyTag_CAPTION:
        case TidyTag_CENTER:
        case TidyTag_DIR:
        case TidyTag_MENU:
        case TidyTag_SUMMARY:
            return 1;
        default:
            return isBlockTag(id);
    }
}

static void md_img(MdCtx *c, MdRun *r, TidyNode node)
{
    char *alt = md_attr(node, "alt"), *src = md_attr(node, "src");
    char *title = (c->opts & mdTitleTxt) ? md_attr(node, "title") : NULL;

    if ((c->opts & mdImgLinks) && src)
    {
        md_run_flushq(r);
        md_run_puts(r, "![", MK_SYN);
        if (alt)
        {
            r->ltext++;
            md_run_text(r, alt, strlen(alt));
            r->ltext--;
            r->pspace = 0;
        }
        md_run_dest(r, src, title);
    }
    else if (alt && ((c->opts & mdImgAlt) || r->inlink))
        md_run_text(r, alt, strlen(alt));
    free(alt);
    free(src);
    free(title);
}

static void md_link_open(MdCtx *c, MdRun *r, char *href)
{
    if (r->nsp == r->capsp)
    {
        r->capsp = r->capsp ? r->capsp * 2 : 8;
        REMALLOC(r->sp, r->capsp * sizeof(MdSpan));
    }
    r->sp[r->nsp].from = r->sp[r->nsp].to = 0;
    r->sp[r->nsp].href = href;
    r->linkspan = (long)r->nsp++;
    r->inlink = 1;
    r->plainlink = !(c->opts & mdLinks);
    r->ltext = !r->plainlink;
    r->q[r->nq++] = MQ_LINK;
}

static void md_link_close(MdCtx *c, MdRun *r, TidyNode node)
{
    MdSpan *s = &r->sp[r->linkspan];

    r->inlink = r->ltext = 0;
    if (md_q_remove(r, MQ_LINK))  /* nothing visible: no [](href) */
        s->from = s->to = r->len;
    else
    {
        s->to = r->len;
        if (c->opts & mdLinks)
        {
            char *title = (c->opts & mdTitleTxt) ? md_attr(node, "title") : NULL;

            md_run_dest(r, s->href, title);
            free(title);
        }
    }
    r->linkspan = -1;
}

static void md_text_title(MdCtx *c, MdRun *r, TidyNode node, TidyTagId id)
{
    char *title;

    if (!(c->opts & mdTitleTxt) || id == TidyTag_IMG)
        return;
    if (id == TidyTag_A && (c->opts & mdLinks) && getAttr(node, "href"))
        return;
    title = md_attr(node, "title");
    if (title)
    {
        md_run_text(r, title, strlen(title));
        r->pspace = 1;
        free(title);
    }
}

/* past the recursion limit: keep the text, lose the structure */
static void md_flat_text(MdCtx *c, MdRun *r, TidyNode node)
{
    MdOut t;

    md_out_init(&t);
    md_collect_text(c, node, &t, 1);
    r->pspace = 1;
    md_run_text(r, t.p ? t.p : "", t.len);
    r->pspace = 1;
    md_out_free(&t);
}

/* for boolean attributes: getAttr() gives NULL for <input checked> */
static int md_has_attr(TidyNode node, const char *name)
{
    TidyAttr attr;
    ctmbstr key;

    for (attr = tidyAttrFirst(node); attr; attr = tidyAttrNext(attr))
    {
        key = tidyAttrName(attr);
        if (!key) continue; /* nameless attr: malformed markup, see NULL_ATTR_NAME */
        if (!strcasecmp(key, name))
            return 1;
    }
    return 0;
}

static void md_inline_node(MdCtx *c, MdRun *r, TidyNode child, int depth)
{
    TidyNodeType type = tidyNodeGetType(child);
    TidyTagId id = tidyNodeGetId(child);

    if (type == TidyNode_Text)
    {
        size_t len;
        char *s = md_node_value(c->doc, child, &len);

        md_run_text(r, s, len);
        free(s);
        return;
    }
    if (type != TidyNode_Start && type != TidyNode_StartEnd)
        return;

    switch (id)
    {
        case TidyTag_SCRIPT:
        case TidyTag_STYLE:
        case TidyTag_HEAD:
        case TidyTag_TITLE:
            return;
        case TidyTag_BR:
            if (r->single)
                r->pspace = 1;
            else
                r->pbr = 1;
            return;
        case TidyTag_IMG:
            md_img(c, r, child);
            return;
        case TidyTag_INPUT:
        {
            /* a checkbox opening a list item: a GFM task */
            const char *type = getAttr(child, "type");

            if (c->taskok && !r->len && type && !strcasecmp(type, "checkbox"))
            {
                md_run_puts(r, md_has_attr(child, "checked") ? "[x]" : "[ ]", MK_SYN);
                r->pspace = 1;
                c->taskok = 0;
            }
            return;
        }
        default:
            break;
    }

    if (depth > MD_MAX_RECURSE)
    {
        md_flat_text(c, r, child);
        return;
    }
    md_text_title(c, r, child, id);

    if ((id == TidyTag_CODE || id == TidyTag_PRE) && (c->opts & mdCode))
    {
        md_inline_code(c, r, child);
    }
    else if ((id == TidyTag_B || id == TidyTag_STRONG) && (c->opts & mdEmphasis))
    {
        md_em_open(r, MQ_BOLD);
        md_inline_children(c, r, child, depth + 1);
        md_em_close(r, MQ_BOLD);
    }
    else if ((id == TidyTag_I || id == TidyTag_EM) && (c->opts & mdEmphasis))
    {
        md_em_open(r, MQ_ITAL);
        md_inline_children(c, r, child, depth + 1);
        md_em_close(r, MQ_ITAL);
    }
    else if ((id == TidyTag_DEL || id == TidyTag_S || id == TidyTag_STRIKE) && (c->opts & mdEmphasis))
    {
        md_em_open(r, MQ_STRIKE);
        md_inline_children(c, r, child, depth + 1);
        md_em_close(r, MQ_STRIKE);
    }
    else if (id == TidyTag_A && !r->inlink && getAttr(child, "href") && (c->opts & (mdLinks | mdLinkSpans)))
    {
        md_link_open(c, r, md_attr(child, "href"));
        md_inline_children(c, r, child, depth + 1);
        md_link_close(c, r, child);
    }
    else if (id == TidyTag_TABLE && c->fallback == MD_FALLBACK_SKIP)
    {
        ;
    }
    else if (md_is_block(id))
    {
        r->pspace = 1;
        md_inline_children(c, r, child, depth + 1);
        r->pspace = 1;
    }
    else
        md_inline_children(c, r, child, depth + 1);
}

static void md_inline_children(MdCtx *c, MdRun *r, TidyNode node, int depth)
{
    TidyNode child;

    for (child = tidyGetChild(node); child; child = tidyGetNext(child))
        md_inline_node(c, r, child, depth);
}

#define MD_WS(ch)    ((ch) == 0 || (ch) == ' ' || (ch) == '\n')
#define MD_PUNCT(ch) ((ch) < 0x80 && ispunct(ch))

static unsigned char md_prev(const MdRun *r, size_t i)
{
    while (i > 0)
    {
        i--;
        if (r->k[i] != MK_DROP && r->k[i] != MK_EM)
        {
            if ((unsigned char)r->p[i] == 0xA0 && i > 0 && (unsigned char)r->p[i - 1] == 0xC2)
                return ' ';
            return (unsigned char)r->p[i];
        }
    }
    return 0;
}

static unsigned char md_next(const MdRun *r, size_t i)
{
    for (; i < r->len; i++)
    {
        if (r->k[i] != MK_DROP && r->k[i] != MK_EM)
        {
            if ((unsigned char)r->p[i] == 0xC2 && i + 1 < r->len && (unsigned char)r->p[i + 1] == 0xA0)
                return ' ';
            return (unsigned char)r->p[i];
        }
    }
    return 0;
}

/* CommonMark flanking.  md_prev/next look past emphasis markers */
static void md_flank(const MdRun *r, size_t i, size_t j, int *left, int *right, unsigned char *before, unsigned char *after)
{
    unsigned char b = md_prev(r, i), a = md_next(r, j);

    *left = !MD_WS(a) && (!MD_PUNCT(a) || MD_WS(b) || MD_PUNCT(b));
    *right = !MD_WS(b) && (!MD_PUNCT(b) || MD_WS(a) || MD_PUNCT(a));
    *before = b;
    *after = a;
}

#define MD_ISTEXT(i) (r->k[(i)] == MK_TEXT || r->k[(i)] == MK_LTEXT)

/* emphasis the flanking rules would refuse is left unmarked: see the spec */
static void md_drop_bad_em(MdRun *r)
{
    size_t i;
    int l, rt;
    unsigned char b, a;

    for (i = 0; i < r->nem; i++)
    {
        MdEm *e = &r->em[i];
        int ok = e->closed;

        if (ok)
        {
            md_flank(r, e->open, e->open + e->len, &l, &rt, &b, &a);
            ok = l;
        }
        if (ok)
        {
            md_flank(r, e->close, e->close + e->len, &l, &rt, &b, &a);
            ok = rt;
        }
        if (!ok)
        {
            memset(r->k + e->open, MK_DROP, e->len);
            if (e->closed)
                memset(r->k + e->close, MK_DROP, e->len);
        }
    }
}

/* mark delimiter runs of ch that could pair up.  ~ is GFM strikethrough */
static void md_mark_delims(MdRun *r, unsigned char *esc, char ch)
{
    long firstco = -1, lastcc = -1;
    int pass, syn = 0;
    size_t i, j;

    if (ch == '*' || ch == '~')
    {
        for (i = 0; i < r->len; i++)
            if (r->k[i] == MK_EM && r->p[i] == ch)
                syn = 1;
    }
    for (pass = 0; pass < 2; pass++)
    {
        for (i = 0; i < r->len; i = j)
        {
            int l, rt, co, cc;
            unsigned char b, a;

            j = i + 1;
            if (r->p[i] != ch || !MD_ISTEXT(i))
                continue;
            while (j < r->len && r->p[j] == ch && MD_ISTEXT(j))
                j++;
            /* touching a generated marker: it would merge with or block it */
            if (ch != '_' && ((i > 0 && r->k[i - 1] == MK_EM) || (j < r->len && r->k[j] == MK_EM)))
            {
                if (pass)
                    memset(esc + i, 1, j - i);
                continue;
            }
            if (ch == '~' && j - i > 2)
                continue;
            md_flank(r, i, j, &l, &rt, &b, &a);
            co = l;
            cc = rt;
            if (ch == '_')
            {
                co = l && (!rt || MD_PUNCT(b));
                cc = rt && (!l || MD_PUNCT(a));
            }
            /* renderers disagree on whether ~ is punctuation: assume the worst */
            if (a == '~')
                co = 1;
            if (b == '~')
                cc = 1;
            if (!pass)
            {
                if (co && firstco < 0)
                    firstco = (long)i;
                if (cc)
                    lastcc = (long)i;
            }
            else if ((co && (syn || (long)i < lastcc)) || (cc && (syn || (long)i > firstco)))
            {
                if (firstco < 0 && !syn)
                    continue;
                memset(esc + i, 1, j - i);
            }
        }
    }
}

static int md_is_entity(const MdRun *r, size_t i)
{
    size_t j = i + 1, n = 0;

    if (j < r->len && r->p[j] == '#')
    {
        j++;
        if (j < r->len && (r->p[j] == 'x' || r->p[j] == 'X'))
        {
            j++;
            while (j < r->len && n < 7 && isxdigit((unsigned char)r->p[j]))
                j++, n++;
        }
        else
            while (j < r->len && n < 8 && isdigit((unsigned char)r->p[j]))
                j++, n++;
    }
    else
    {
        if (j >= r->len || !isalpha((unsigned char)r->p[j]))
            return 0;
        while (j < r->len && n < 40 && isalnum((unsigned char)r->p[j]))
            j++, n++;
    }
    return n && j < r->len && r->p[j] == ';';
}

/* rules that only apply to the first characters of a line */
static void md_mark_linestart(MdRun *r, unsigned char *esc, size_t i, int firstline)
{
    size_t e = i, j, n = 0;
    char ch = r->p[i];
    int same = 1, nchar = 0;

    while (e < r->len && r->p[e] != '\n')
        e++;
    /* a line of only one of - = * _ (spaces allowed) is a rule or setext underline */
    if (ch == '-' || ch == '=' || ch == '*' || ch == '_')
    {
        for (j = i; j < e; j++)
        {
            if (r->p[j] == ch)
                nchar++;
            else if (r->p[j] != ' ')
                same = 0;
        }
        /* two *'s are enough: after a list's "* " marker the line is a rule */
        if (same && ((ch == '=' || ch == '-') ? (!firstline || (ch == '-' && nchar >= 3))
                                              : nchar >= (ch == '*' ? 2 : 3)))
            esc[i] = 1;
    }
    /* :---: or |---| under another line is a GFM table delimiter row */
    if (!firstline && (ch == ':' || ch == '|' || ch == '-'))
    {
        int dash = 0;

        same = 1;
        for (j = i; j < e; j++)
        {
            if (r->p[j] == '-')
                dash = 1;
            else if (r->p[j] != ':' && r->p[j] != '|' && r->p[j] != ' ')
                same = 0;
        }
        if (same && dash)
            esc[i] = 1;
    }
    if (ch == '#')
    {
        while (i + n < e && r->p[i + n] == '#')
            n++;
        if (n <= 6 && (i + n == e || r->p[i + n] == ' '))
            esc[i] = 1;
    }
    else if (ch == '>')
        esc[i] = 1;
    else if (ch == '-' || ch == '+' || ch == '*')
    {
        if (i + 1 == e || r->p[i + 1] == ' ')
            esc[i] = 1;
    }
    else if (isdigit((unsigned char)ch))
    {
        while (i + n < e && n < 10 && isdigit((unsigned char)r->p[i + n]))
            n++;
        if (n <= 9 && i + n < e && (r->p[i + n] == '.' || r->p[i + n] == ')') &&
            (i + n + 1 == e || r->p[i + n + 1] == ' '))
            esc[i + n] = 1;
    }
    else if (ch == '`' || ch == '~')
    {
        while (i + n < e && r->p[i + n] == ch)
            n++;
        if (n >= 3)
            memset(esc + i, 1, n);  /* all: a shorter run left over could still pair */
    }
    else if (ch == '[' && i + 2 < e && r->p[i + 2] == ']' &&
             (r->p[i + 1] == ' ' || r->p[i + 1] == 'x' || r->p[i + 1] == 'X') &&
             (i + 3 == e || r->p[i + 3] == ' '))
    {
        /* [ ] or [x] opening a list item is a GFM task checkbox */
        esc[i] = 1;
    }
    else if (ch == '[')
    {
        for (j = i + 1; j < e; j++)
        {
            if (r->p[j] == ']')
            {
                if (j + 1 < e && r->p[j + 1] == ':')
                    esc[i] = 1;
                break;
            }
        }
    }
}

static void md_mark_escapes(MdRun *r, unsigned char *esc)
{
    size_t i, n;
    long lasttick[65], lastrun = -1;
    int firstline = 1, tickpair = 0;
    size_t *opens = NULL, nopen = 0, capopen = 0;

#define bracket_open(pos) do{\
    if (nopen == capopen)\
    {\
        capopen = capopen ? capopen * 2 : 16;\
        REMALLOC(opens, capopen * sizeof(size_t));\
    }\
    opens[nopen++] = (pos);\
} while(0)

    md_mark_delims(r, esc, '*');
    md_mark_delims(r, esc, '_');
    md_mark_delims(r, esc, '~');

    /* Backtick runs of equal length pair up.  A backslash can't protect a run
       from an earlier opener, and splits it into shorter runs, so once any two
       could pair every run but the last is escaped. */
    memset(lasttick, -1, sizeof(lasttick));
    for (i = 0; i < r->len; i += n ? n : 1)
    {
        n = 0;
        while (i + n < r->len && r->p[i + n] == '`' && r->k[i + n] != MK_CODE)
            n++;
        if (n)
        {
            if (lasttick[n > 64 ? 64 : n] >= 0)
                tickpair = 1;
            lasttick[n > 64 ? 64 : n] = (long)i;
            lastrun = (long)i;
        }
    }

    for (i = 0; i < r->len; i++)
    {
        char ch = r->p[i];
        char nx = (i + 1 < r->len) ? r->p[i + 1] : '\0';

        if (!MD_ISTEXT(i))
            continue;
        if (!r->single && (i == 0 || r->p[i - 1] == '\n'))
        {
            if (i && r->p[i - 1] == '\n')
                firstline = 0;
            md_mark_linestart(r, esc, i, firstline);
        }
        switch (ch)
        {
            case '\\':
                if (nx && (unsigned char)nx < 0x80 && ispunct((unsigned char)nx))
                    esc[i] = 1;
                break;
            case '[':
                if (r->k[i] == MK_LTEXT)
                    esc[i] = 1;
                else
                    bracket_open(i);
                break;
            case ']':
                if (r->k[i] == MK_LTEXT)
                    esc[i] = 1;
                else if (nopen)
                {
                    /* ]( or ][ could complete a link.  Every open [ goes:
                       this ] would otherwise close the next one out */
                    if (nx == '(' || nx == '[')
                    {
                        while (nopen)
                            esc[opens[--nopen]] = 1;
                    }
                    else
                        nopen--;
                }
                break;
            case '!':
                if (nx == '[' && r->k[i + 1] == MK_SYN)  /* would turn a link into an image */
                    esc[i] = 1;
                break;
            case '<':
                if (isalpha((unsigned char)nx) || nx == '/' || nx == '!' || nx == '?')
                    esc[i] = 1;
                break;
            case '&':
                if (md_is_entity(r, i))
                    esc[i] = 1;
                break;
            case '`':
                n = 1;
                while (i + n < r->len && r->p[i + n] == '`' && MD_ISTEXT(i + n))
                    n++;
                if (tickpair && lastrun >= (long)(i + n))
                    memset(esc + i, 1, n);
                i += n - 1;
                break;
            default:
                break;
        }
    }
    free(opens);
#undef bracket_open
    if (r->heading)
    {
        /* ## text ## : a trailing # run would be taken as a closing sequence */
        i = r->len;
        while (i > 0 && r->p[i - 1] == '#')
            i--;
        if (i < r->len && MD_ISTEXT(i) && (i == 0 || r->p[i - 1] == ' '))
            esc[i] = 1;
    }
}

/* finish a run into o (appending): escape and map spans */
static void md_run_finish(MdCtx *c, MdRun *r, MdOut *o)
{
    size_t i, fi = 0, ti = 0, base = o->len;
    unsigned char *esc = NULL;
    size_t *nf = NULL, *nt = NULL;

    REMALLOC(esc, r->len + 1);
    memset(esc, 0, r->len + 1);

    if (r->len)
        c->taskok = 0;
    md_drop_bad_em(r);
    if (c->opts & mdEscape)
        md_mark_escapes(r, esc);
    if (r->cell)  /* structural: applies to every kind, and with escape off */
        for (i = 0; i < r->len; i++)
            if (r->p[i] == '|' && r->k[i] != MK_DROP)
                esc[i] = 1;

    if (r->nsp)
    {
        REMALLOC(nf, r->nsp * sizeof(size_t));
        REMALLOC(nt, r->nsp * sizeof(size_t));
    }
    for (i = 0;; i++)
    {
        while (fi < r->nsp && r->sp[fi].from <= i)
            nf[fi++] = o->len - base;
        while (ti < r->nsp && r->sp[ti].to <= i)
            nt[ti++] = o->len - base;
        if (i >= r->len)
            break;
        if (r->k[i] == MK_DROP)
            continue;
        if (esc[i])
            md_put(o, "\\", 1);
        md_put(o, r->p + i, 1);
    }
    for (i = 0; i < r->nsp; i++)
        if (c->opts & mdLinkSpans)
            md_add_span(o, base + nf[i], base + nt[i], r->sp[i].href);
    free(nf);
    free(nt);
    free(esc);
}

static void md_blocks(MdCtx *c, TidyNode node, int self, MdOut *out, int depth, int em, int tight);

/* only U+00A0: content within text, but alone it would be a whitespace-only line */
static void md_drop_if_blank(MdOut *o)
{
    size_t i;

    for (i = 0; i + 1 < o->len; i += 2)
        if ((unsigned char)o->p[i] != 0xC2 || (unsigned char)o->p[i + 1] != 0xA0)
            return;
    if (i == o->len)
        o->len = 0;
}

/* a paragraph or similar: close the run and append it to out */
static void md_run_end(MdCtx *c, MdRun *r, MdOut *out, int em, int sep)
{
    MdOut piece;

    int i;

    for (i = 0; i < MQ_NEM; i++)
        if (em & (1 << i))
            md_em_close(r, i);
    md_out_init(&piece);
    md_run_finish(c, r, &piece);
    md_drop_if_blank(&piece);
    md_add(out, &piece, sep);
    md_out_free(&piece);
    md_run_free(r);
    md_run_init(r);
    for (i = 0; i < MQ_NEM; i++)
        if (em & (1 << i))
            md_em_open(r, i);
}

static int md_has_tag(TidyNode node, int blockonly, TidyTagId want, int depth)
{
    TidyNode child;

    if (depth > MD_MAX_RECURSE)
        return 0;
    for (child = tidyGetChild(node); child; child = tidyGetNext(child))
    {
        TidyTagId id = tidyNodeGetId(child);

        if (tidyNodeGetType(child) == TidyNode_Text)
            continue;
        if (blockonly ? md_is_block(id) : (id == want))
            return 1;
        if (md_has_tag(child, blockonly, want, depth + 1))
            return 1;
    }
    return 0;
}

static void md_heading(MdCtx *c, TidyNode node, MdOut *out, int level)
{
    MdRun r;
    MdOut piece, line;
    char hashes[8];

    c->taskok = 0;

    md_run_init(&r);
    r.single = r.heading = 1;
    md_inline_children(c, &r, node, 0);
    md_out_init(&piece);
    md_out_init(&line);
    md_run_finish(c, &r, &piece);
    md_drop_if_blank(&piece);
    if (piece.len)
    {
        memset(hashes, '#', level);
        hashes[level] = ' ';
        hashes[level + 1] = '\0';
        md_prefix(&line, &piece, hashes, "", "");
        md_add(out, &line, c->blank + 1);
    }
    md_out_free(&piece);
    md_out_free(&line);
    md_run_free(&r);
}

static void md_pre(MdCtx *c, TidyNode node, MdOut *out)
{
    MdOut t, piece;
    size_t i, run = 0, longest = 2, flen;
    char *cls = md_attr(node, "class"), *lang = NULL, ticks[66], info[64];
    TidyNode child;

    c->taskok = 0;
    info[0] = '\0';
    if (!cls)
    {
        for (child = tidyGetChild(node); child && !cls; child = tidyGetNext(child))
            if (tidyNodeGetId(child) == TidyTag_CODE)
                cls = md_attr(child, "class");
    }
    if (cls)
    {
        char *s = cls;

        /* language-x or lang-x as a whole word of the class */
        while (*s)
        {
            while (*s == ' ')
                s++;
            if (!strncmp(s, "language-", 9))
                lang = s + 9;
            else if (!strncmp(s, "lang-", 5))
                lang = s + 5;
            if (lang)
                break;
            while (*s && *s != ' ')
                s++;
        }
        if (lang)
        {
            size_t n = 0;

            while (lang[n] && lang[n] != ' ' && lang[n] != '`' && n < sizeof(info) - 1)
            {
                info[n] = lang[n];
                n++;
            }
            info[n] = '\0';
        }
        free(cls);
    }

    md_out_init(&t);
    md_out_init(&piece);
    md_collect_text(c, node, &t, 0);
    /* one newline is the closing fence's; more are content */
    if (t.len && t.p[t.len - 1] == '\n')
        t.len--;
    if (t.len && t.p[t.len - 1] == '\r')
        t.len--;
    if (!t.len)
    {
        md_out_free(&t);
        return;
    }
    for (i = 0; i < t.len; i++)
    {
        if (t.p[i] == '`')
        {
            if (++run > longest)
                longest = run;
        }
        else
            run = 0;
    }
    if (longest > 63)
        longest = 63;
    flen = longest + 1;
    memset(ticks, '`', flen);
    ticks[flen] = '\0';
    md_put(&piece, ticks, flen);
    md_put(&piece, info, strlen(info));
    md_put(&piece, "\n", 1);
    md_put(&piece, t.p, t.len);
    md_put(&piece, "\n", 1);
    md_put(&piece, ticks, flen);
    md_add(out, &piece, c->blank + 1);
    md_out_free(&t);
    md_out_free(&piece);
}

/* container rendered to its own buffer, then prefixed */
static void md_prefixed(MdCtx *c, TidyNode node, MdOut *out, int depth, int em, int sep,
                        const char *first, const char *rest, const char *blank)
{
    MdOut sub, piece;

    md_out_init(&sub);
    md_out_init(&piece);
    if (tidyNodeGetId(node) != TidyTag_LI)
        c->taskok = 0;
    c->plevel++;
    md_blocks(c, node, 0, &sub, depth + 1, em, tidyNodeGetId(node) == TidyTag_LI);
    c->plevel--;
    if (!sub.len || c->plevel >= MD_MAX_PREFIX)
        md_add(out, &sub, sep);
    else
    {
        md_prefix(&piece, &sub, first, rest, blank);
        md_add(out, &piece, sep);
    }
    md_out_free(&sub);
    md_out_free(&piece);
}

static void md_list(MdCtx *c, TidyNode node, MdOut *out, int depth, int em, int tight)
{
    MdOut lst;
    TidyNode child;
    int ordered = (tidyNodeGetId(node) == TidyTag_OL);
    long num = 1, startnum;

    md_out_init(&lst);
    if (ordered)
    {
        const char *st = getAttr(node, "start");

        if (st)
        {
            num = strtol(st, NULL, 10);
            if (num < 0 || num > 999999999L)
                num = 1;
        }
    }
    startnum = num;
    for (child = tidyGetChild(node); child; child = tidyGetNext(child))
    {
        char marker[16], indent[16];
        size_t before = lst.len + lst.nsp;
        int mlen;

        if (tidyNodeGetType(child) != TidyNode_Start)
            continue;
        if (ordered)
            mlen = snprintf(marker, sizeof(marker), "%ld. ", num);
        else
            mlen = snprintf(marker, sizeof(marker), "* ");
        memset(indent, ' ', mlen);
        indent[mlen] = '\0';
        c->taskok = 1;
        md_prefixed(c, child, &lst, depth, em, 1, marker, indent, "");
        c->taskok = 0;
        if (ordered && lst.len + lst.nsp != before)
            num++;
    }
    /* only 1. can interrupt a paragraph, so another start needs a blank line */
    md_add(out, &lst, (tight && startnum == 1) ? 1 : c->blank + 1);
    md_out_free(&lst);
}

typedef struct {
    MdOut t;
    int th, cs, rs, placed;
    TidyNode node;
} MdCell;

typedef struct {
    size_t first, n;      /* cells[first..first+n) */
    int section;
    TidyNode tr;
} MdRow;

typedef struct {
    MdCell *cells;
    size_t ncells, capcells;
    MdRow *rows;
    size_t nrows, caprows;
} MdTable;

static int md_span_attr(TidyNode node, const char *name, int zero)
{
    const char *v = getAttr(node, name);
    long n;

    if (!v)
        return 1;
    n = strtol(v, NULL, 10);
    if (n == 0 && zero)
        return zero;
    if (n < 1)
        return 1;
    return n > MD_MAX_ROWSPAN ? MD_MAX_ROWSPAN : (int)n;
}

static void md_table_row(MdCtx *c, MdTable *t, TidyNode tr, int section)
{
    TidyNode td;
    MdRow *row;

    if (t->nrows == t->caprows)
    {
        t->caprows = t->caprows ? t->caprows * 2 : 16;
        REMALLOC(t->rows, t->caprows * sizeof(MdRow));
    }
    row = &t->rows[t->nrows++];
    row->first = t->ncells;
    row->n = 0;
    row->section = section;
    row->tr = tr;

    for (td = tidyGetChild(tr); td; td = tidyGetNext(td))
    {
        TidyTagId id = tidyNodeGetId(td);
        MdCell *cell;
        MdRun r;

        if (id != TidyTag_TD && id != TidyTag_TH)
            continue;
        if (t->ncells == t->capcells)
        {
            t->capcells = t->capcells ? t->capcells * 2 : 32;
            REMALLOC(t->cells, t->capcells * sizeof(MdCell));
        }
        cell = &t->cells[t->ncells++];
        md_out_init(&cell->t);
        cell->th = (id == TidyTag_TH);
        cell->cs = md_span_attr(td, "colspan", 0);
        cell->rs = md_span_attr(td, "rowspan", MD_MAX_ROWSPAN);
        cell->node = td;
        cell->placed = 0;
        row->n++;
        md_run_init(&r);
        r.single = r.cell = 1;
        md_inline_children(c, &r, td, 0);
        md_run_finish(c, &r, &cell->t);
        md_drop_if_blank(&cell->t);
        md_run_free(&r);
    }
}

/* rows in output order: thead, then tbody and bare rows, then tfoot */
static void md_table_rows(MdCtx *c, MdTable *t, TidyNode table)
{
    int pass, section = 0;
    TidyNode ch, tr;

    for (pass = 0; pass < 3; pass++)
    {
        for (ch = tidyGetChild(table); ch; ch = tidyGetNext(ch))
        {
            TidyTagId id = tidyNodeGetId(ch);
            int mine = (pass == 0) ? (id == TidyTag_THEAD)
                     : (pass == 2) ? (id == TidyTag_TFOOT)
                     : (id == TidyTag_TBODY || id == TidyTag_TR);

            if (!mine)
                continue;
            if (id == TidyTag_TR)
                md_table_row(c, t, ch, -1);
            else
            {
                section++;
                for (tr = tidyGetChild(ch); tr; tr = tidyGetNext(tr))
                    if (tidyNodeGetId(tr) == TidyTag_TR)
                        md_table_row(c, t, tr, section);
            }
        }
    }
}

static void md_table_free(MdTable *t)
{
    size_t i;

    for (i = 0; i < t->ncells; i++)
        md_out_free(&t->cells[i].t);
    free(t->cells);
    free(t->rows);
}

/* Lay out rows on the grid.  line[] gets a cell index per column, -1 if empty.
   With o, write the table; without, measure it. */
static void md_table_grid(MdTable *t, int width, int repeat, MdOut *o, int *wout, size_t *copies)
{
    int rem[MD_MAX_COLS], at[MD_MAX_COLS], line[MD_MAX_COLS];
    size_t ri, i;
    int col, maxw = 0, section = -2, wrote = 0, wantsep = 0;

    memset(rem, 0, sizeof(rem));
    *copies = 0;
    for (i = 0; i < t->ncells; i++)
        t->cells[i].placed = 0;

    for (ri = 0; ri < t->nrows; ri++)
    {
        MdRow *row = &t->rows[ri];
        int used = 0, visible = 0;

        if (row->section != section)
        {
            memset(rem, 0, sizeof(rem));
            section = row->section;
        }
        for (col = 0; col < MD_MAX_COLS; col++)
        {
            line[col] = -1;
            if (rem[col] > 0)
            {
                line[col] = at[col];
                rem[col]--;
                used = col + 1;
            }
        }
        col = 0;
        for (i = 0; i < row->n; i++)
        {
            size_t ci = row->first + i;
            MdCell *cell = &t->cells[ci];
            int k;

            while (col < MD_MAX_COLS && line[col] >= 0)
                col++;
            for (k = 0; k < cell->cs && col + k < MD_MAX_COLS; k++)
            {
                if (line[col + k] >= 0)
                    break;
                line[col + k] = (int)ci;
                if (cell->rs > 1)
                {
                    rem[col + k] = cell->rs - 1;
                    at[col + k] = (int)ci;
                }
            }
            col += k;
            if (col > used)
                used = col;
        }
        if (used > maxw)
            maxw = used;

        for (col = 0; col < used; col++)
        {
            if (line[col] >= 0)
            {
                MdCell *cell = &t->cells[line[col]];

                if (cell->t.len)
                    visible = 1;
                if (!col || line[col - 1] != line[col])
                    *copies += cell->t.len;
            }
        }
        if (!o || !visible)
            continue;

        /* header: the first row if all <th>, otherwise an empty one */
        if (!wrote)
        {
            wantsep = (row->n > 0);
            for (i = 0; i < row->n; i++)
                if (!t->cells[row->first + i].th)
                    wantsep = 0;
            if (!wantsep)
            {
                for (col = 0; col < width; col++)
                    md_put(o, "|   ", 4);
                md_put(o, "|\n", 2);
                for (col = 0; col < width; col++)
                    md_put(o, "|---", 4);
                md_put(o, "|\n", 2);
            }
            wrote = 1;
        }
        else
            md_put(o, "\n", 1);

        for (col = 0; col < width; col++)
        {
            MdCell *cell = (line[col] >= 0) ? &t->cells[line[col]] : NULL;

            /* a colspan's content goes in its first column only: a pipe table
               cannot span, and repeating it only duplicates the text */
            if (cell && cell->t.len && (repeat || !cell->placed) &&
                (!col || line[col - 1] != line[col]))
            {
                size_t base, s;

                md_put(o, "| ", 2);
                base = o->len;
                md_put(o, cell->t.p, cell->t.len);
                for (s = 0; s < cell->t.nsp; s++)
                    md_add_span(o, base + cell->t.sp[s].from, base + cell->t.sp[s].to, cell->t.sp[s].href);
                md_put(o, " ", 1);
                cell->placed = 1;
            }
            else
                md_put(o, "|   ", 4);
        }
        md_put(o, "|", 1);
        if (wantsep)
        {
            md_put(o, "\n", 1);
            for (col = 0; col < width; col++)
                md_put(o, "|---", 4);
            md_put(o, "|", 1);
            wantsep = 0;
        }
    }
    *wout = maxw;
}

/* one line per row: tables off, or too sparse to lay out within the limits */
static void md_table_text(MdCtx *c, MdTable *t, MdOut *out)
{
    size_t ri;
    MdOut lines;

    md_out_init(&lines);
    for (ri = 0; ri < t->nrows; ri++)
    {
        MdRun r;
        MdOut one;

        md_run_init(&r);
        md_out_init(&one);
        r.single = 1;
        md_inline_children(c, &r, t->rows[ri].tr, 0);
        md_run_finish(c, &r, &one);
        md_add(&lines, &one, 1);
        md_out_free(&one);
        md_run_free(&r);
    }
    md_add(out, &lines, c->blank + 1);
    md_out_free(&lines);
}

static void md_table(MdCtx *c, TidyNode node, MdOut *out, int depth, int em)
{
    MdTable t;
    TidyNode ch;
    size_t i, uniq = 0, copies, budget;
    int width = 0, visible = 0;
    MdOut tb;

    c->taskok = 0;
    memset(&t, 0, sizeof(t));
    md_table_rows(c, &t, node);

    for (i = 0; i < t.ncells; i++)
    {
        uniq += t.cells[i].t.len;
        if (t.cells[i].t.len)
            visible = 1;
    }
    if (!visible)
    {
        md_table_free(&t);
        return;
    }

    /* a single cell around another table is layout, not data */
    if (t.ncells == 1 && md_has_tag(t.cells[0].node, 0, TidyTag_TABLE, 0))
    {
        TidyNode inner = t.cells[0].node;

        md_table_free(&t);
        md_blocks(c, inner, 0, out, depth + 1, em, 0);
        return;
    }

    for (ch = tidyGetChild(node); ch; ch = tidyGetNext(ch))
    {
        if (tidyNodeGetId(ch) == TidyTag_CAPTION)
        {
            MdRun r;

            md_run_init(&r);
            r.single = 1;
            md_inline_children(c, &r, ch, 0);
            md_run_end(c, &r, out, 0, c->blank + 1);
            md_run_free(&r);
        }
    }

    if (!(c->opts & mdTables))
    {
        md_table_text(c, &t, out);
        md_table_free(&t);
        return;
    }

    md_table_grid(&t, 0, 1, NULL, &width, &copies);
    budget = 16 * (uniq + 8 * t.ncells) + 65536;
    md_out_init(&tb);
    if (copies + (size_t)width * t.nrows * 6 <= budget)
        md_table_grid(&t, width, 1, &tb, &width, &copies);
    else if (uniq + (size_t)width * t.nrows * 6 <= budget)
        md_table_grid(&t, width, 0, &tb, &width, &copies);
    else
        md_table_text(c, &t, out);
    md_add(out, &tb, c->blank + 1);
    md_out_free(&tb);
    md_table_free(&t);
}

static void md_meta(MdCtx *c, TidyNode head, MdOut *out)
{
    TidyNode child;

    for (child = tidyGetChild(head); child; child = tidyGetNext(child))
    {
        const char *name;
        const char *tname = NULL;

        if (tidyNodeGetId(child) != TidyTag_META || !(name = getAttr(child, "name")))
            continue;
        if ((c->opts & mdMetaDesc) && !strcasecmp(name, "description"))
            tname = "description: ";
        else if ((c->opts & mdMetaKeyw) && !strcasecmp(name, "keywords"))
            tname = "keywords: ";
        if (tname)
        {
            char *cont = md_attr(child, "content");

            if (cont)
            {
                MdRun r;

                md_run_init(&r);
                md_run_text(&r, tname, strlen(tname));
                r.pspace = 1;
                md_run_text(&r, cont, strlen(cont));
                md_run_end(c, &r, out, 0, c->blank + 1);
                md_run_free(&r);
                free(cont);
            }
        }
    }
}

/* the children of node, or with self just node */
static void md_blocks(MdCtx *c, TidyNode node, int self, MdOut *out, int depth, int em, int tight)
{
    TidyNode child;
    MdRun r;
    int i;

    if (depth > MD_MAX_RECURSE)
    {
        md_run_init(&r);
        md_flat_text(c, &r, node);  /* self is only ever at depth 0 */
        md_run_end(c, &r, out, 0, c->blank + 1);
        md_run_free(&r);
        return;
    }
    md_run_init(&r);
    for (i = 0; i < MQ_NEM; i++)
        if (em & (1 << i))
            md_em_open(&r, i);

#define MD_FLUSH md_run_end(c, &r, out, em, c->blank + 1)

    for (child = self ? node : tidyGetChild(node); child; child = self ? NULL : tidyGetNext(child))
    {
        TidyNodeType type = tidyNodeGetType(child);
        TidyTagId id = tidyNodeGetId(child);

        if (type != TidyNode_Start && type != TidyNode_StartEnd)
        {
            if (type == TidyNode_Text)
                md_inline_node(c, &r, child, depth);
            continue;
        }

        if (!md_is_block(id))
        {
            int flag = 0;

            if (id == TidyTag_SCRIPT || id == TidyTag_STYLE || id == TidyTag_TITLE)
                continue;
            if (id == TidyTag_HEAD)
            {
                md_meta(c, child, out);
                continue;
            }
            /* emphasis cannot span blocks: carry it into each one */
            if (c->opts & mdEmphasis)
            {
                if (id == TidyTag_B || id == TidyTag_STRONG)
                    flag = 1;
                else if (id == TidyTag_I || id == TidyTag_EM)
                    flag = 2;
                else if (id == TidyTag_DEL || id == TidyTag_S || id == TidyTag_STRIKE)
                    flag = 4;
            }
            if (id != TidyTag_A && md_has_tag(child, 1, 0, 0))
            {
                MD_FLUSH;
                md_blocks(c, child, 0, out, depth + 1, em | flag, 0);
            }
            else
                md_inline_node(c, &r, child, depth);
            continue;
        }

        if (id == TidyTag_TITLE)
            continue;
        MD_FLUSH;
        md_text_title(c, &r, child, id);
        MD_FLUSH;
        switch (id)
        {
            case TidyTag_H1: case TidyTag_H2: case TidyTag_H3:
            case TidyTag_H4: case TidyTag_H5: case TidyTag_H6:
                if (c->opts & mdHeadings)
                {
                    md_heading(c, child, out,
                               id == TidyTag_H1 ? 1 : id == TidyTag_H2 ? 2 : id == TidyTag_H3 ? 3 :
                               id == TidyTag_H4 ? 4 : id == TidyTag_H5 ? 5 : 6);
                    break;
                }
                md_blocks(c, child, 0, out, depth + 1, em, 0);
                break;
            case TidyTag_PRE:
                if (c->opts & mdCode)
                    md_pre(c, child, out);
                else
                    md_blocks(c, child, 0, out, depth + 1, em, 0);
                break;
            case TidyTag_BLOCKQUOTE:
                if (c->opts & mdQuote)
                    md_prefixed(c, child, out, depth, em, c->blank + 1, "> ", "> ", ">");
                else
                    md_blocks(c, child, 0, out, depth + 1, em, 0);
                break;
            case TidyTag_UL: case TidyTag_OL: case TidyTag_MENU: case TidyTag_DIR:
                md_list(c, child, out, depth, em, tight);
                break;
            case TidyTag_DD:
                /* the indent is cosmetic, and nested it would reach 4 spaces,
                   an indented code block: only the outermost <dd> gets it */
                c->ddlevel++;
                if (c->ddlevel == 1)
                    md_prefixed(c, child, out, depth, em, c->blank + 1, "  ", "  ", "");
                else
                    md_blocks(c, child, 0, out, depth + 1, em, 0);
                c->ddlevel--;
                break;
            case TidyTag_HR:
            {
                MdOut hr;

                md_out_init(&hr);
                md_put(&hr, "---", 3);
                md_add(out, &hr, c->blank + 1);
                md_out_free(&hr);
                break;
            }
            case TidyTag_TABLE:
                md_table(c, child, out, depth, em);
                break;
            default:
                md_blocks(c, child, 0, out, depth + 1, em, 0);
                break;
        }
    }
    MD_FLUSH;
#undef MD_FLUSH
    md_run_free(&r);
}

static void dumpMarkdown(MdCtx *c, TidyNode start, MdOut *o)
{
    TidyNodeType type = tidyNodeGetType(start);

    /* a node from findTag() is rendered itself, the document through its children */
    md_blocks(c, start, type != TidyNode_Root, o, 0, 0, 0);
    if (o->len)
        md_put(o, "\n", 1);
}

#define md_setopt(name, flag) do{\
    if(duk_get_prop_string(ctx, 0, name))\
    {\
        if(REQUIRE_BOOL(ctx, -1, "html.toMarkdown - option %s requires a boolean", name))\
            c.opts |= (flag);\
        else\
            c.opts &= ~(flag);\
    }\
    duk_pop(ctx);\
} while(0)

/* push {from,to,href,kind} for each span into the array at the top of the stack */
static void md_push_spans(duk_context *ctx, const MdOut *o, size_t base, duk_uarridx_t *ai)
{
    size_t i, j;

    for (i = 0; i < o->nsp; i++)
    {
        const char *h = o->sp[i].href;
        int ext = (h[0] == '/' && h[1] == '/');

        /* scheme ":" , case insensitive */
        if (!ext && isalpha((unsigned char)h[0]))
        {
            for (j = 1; isalnum((unsigned char)h[j]) || h[j] == '+' || h[j] == '.' || h[j] == '-'; j++)
                ;
            ext = (h[j] == ':');
        }
        duk_push_object(ctx);
        duk_push_number(ctx, (double)(base + o->sp[i].from));
        duk_put_prop_string(ctx, -2, "from");
        duk_push_number(ctx, (double)(base + o->sp[i].to));
        duk_put_prop_string(ctx, -2, "to");
        duk_push_string(ctx, h);
        duk_put_prop_string(ctx, -2, "href");
        duk_push_string(ctx, ext ? "external" : "internal");
        duk_put_prop_string(ctx, -2, "kind");
        duk_put_prop_index(ctx, -2, (*ai)++);
    }
}

duk_ret_t duk_rp_html_tomarkdown(duk_context *ctx)
{
    TidyNode start;
    MdCtx c;
    MdOut all;
    int concatenate = 0, i = 0;
    duk_idx_t this_idx, text_idx, links_idx = 0;
    duk_uarridx_t ai = 0;
    size_t sep = 0;

    memset(&c, 0, sizeof(c));
    c.opts = mdHeadings | mdEmphasis | mdCode | mdQuote | mdTables | mdEscape | mdLinks;
    c.fallback = MD_FALLBACK_TEXT;
    c.blank = 1;

    if (duk_is_object(ctx, 0))
    {
        md_setopt("headings", mdHeadings);
        md_setopt("emphasis", mdEmphasis);
        md_setopt("code", mdCode);
        md_setopt("blockquote", mdQuote);
        md_setopt("tables", mdTables);
        md_setopt("escape", mdEscape);
        md_setopt("links", mdLinks);
        md_setopt("imgLinks", mdImgLinks);
        md_setopt("imgAltText", mdImgAlt);
        md_setopt("imageAltText", mdImgAlt);
        md_setopt("metaDescription", mdMetaDesc);
        md_setopt("metaKeywords", mdMetaKeyw);
        md_setopt("titleText", mdTitleTxt);
        md_setopt("linkSpans", mdLinkSpans);

        if (duk_get_prop_string(ctx, 0, "concatenate"))
            concatenate = REQUIRE_BOOL(ctx, -1, "html.toMarkdown - option concatenate requires a boolean");
        duk_pop(ctx);

        if (duk_get_prop_string(ctx, 0, "maxBlankLines"))
        {
            c.blank = REQUIRE_INT(ctx, -1, "html.toMarkdown - option maxBlankLines requires a number");
            if (c.blank < 1)
                RP_THROW(ctx, "html.toMarkdown - option maxBlankLines must be 1 or more");
            if (c.blank > 8)
                c.blank = 8;
        }
        duk_pop(ctx);

        if (duk_get_prop_string(ctx, 0, "tableFallback"))
        {
            const char *f = REQUIRE_STRING(ctx, -1, "html.toMarkdown - option tableFallback requires a string");

            /* no "html": raw HTML is dropped by a Markdown renderer by default */
            if (!strcmp(f, "text"))
                c.fallback = MD_FALLBACK_TEXT;
            else if (!strcmp(f, "skip"))
                c.fallback = MD_FALLBACK_SKIP;
            else
                RP_THROW(ctx, "html.toMarkdown - option tableFallback must be \"text\" or \"skip\"");
        }
        duk_pop(ctx);
    }

    duk_push_this(ctx);
    this_idx = duk_get_top_index(ctx);
    c.doc = get_tdoc(ctx, this_idx);

    if (c.opts & mdLinkSpans)
    {
        duk_push_array(ctx);
        links_idx = duk_get_top_index(ctx);
    }
    if (!concatenate)
        duk_push_array(ctx);
    text_idx = duk_get_top_index(ctx);

    md_out_init(&all);
    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("nodes"));
    duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
    while (duk_next(ctx, -1, 1))
    {
        MdOut one;

        start = duk_get_pointer(ctx, -1);
        duk_pop_2(ctx);
        if (tidyNodeGetType(start) == TidyNode_DocType)
            continue;

        md_out_init(&one);
        dumpMarkdown(&c, start, &one);
        if (concatenate)
        {
            if (all.len && one.len)
                md_put(&all, "\n", 1);
            sep = all.len;
            md_put(&all, one.p ? one.p : "", one.len);
            if (c.opts & mdLinkSpans)
            {
                duk_dup(ctx, links_idx);
                md_push_spans(ctx, &one, sep, &ai);
                duk_pop(ctx);
            }
        }
        else
        {
            duk_push_lstring(ctx, one.p ? one.p : "", one.len);
            duk_put_prop_index(ctx, text_idx, i);
            if (c.opts & mdLinkSpans)
            {
                duk_uarridx_t oi = 0;

                duk_push_array(ctx);
                md_push_spans(ctx, &one, 0, &oi);
                duk_put_prop_index(ctx, links_idx, i);
            }
        }
        md_out_free(&one);
        i++;
    }
    duk_pop_2(ctx);

    if (concatenate)
        duk_push_lstring(ctx, all.p ? all.p : "", all.len);
    else
        duk_dup(ctx, text_idx);
    md_out_free(&all);

    if (c.opts & mdLinkSpans)
    {
        duk_push_object(ctx);
        duk_pull(ctx, -2);
        duk_put_prop_string(ctx, -2, "text");
        duk_dup(ctx, links_idx);
        duk_put_prop_string(ctx, -2, "links");
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

//s2 can contain a '\' and it will be ignored/skipped over unless it matches
static int strncmp_no_bs(const char *s1, const char *s2, size_t n) {
    size_t i = 0, j=0;
    while (i < n && s1[i] != '\0' && s2[j] != '\0')
    {
        if(s2[j] == '\\')
            j++;

        if (s1[i] != s2[j])
        {
                return (unsigned char)s1[i] - (unsigned char)s2[j];
        }
        i++;
        j++;
    }

    if (i < n) {
        if (s1[i] != s2[j]) {
            return (unsigned char)s1[i] - (unsigned char)s2[j];
        }
    }

    return 0;
}

static int findfunc_attr (TidyNode node, const char **txt, const char **txt2, int ntxt){
    int i=0;

    for (;i<ntxt;i++)
    {
        size_t len;
        const char *s, *attr=txt[i];

        while(isspace(*attr)) attr++;
        s = strchr(attr, '=');

        /* backup to non-whitespace before '=' */
        while ( s>attr && isspace(*(s-1)) ) s--;

        if(s)
            len=(size_t) ( s - attr);
        else
            len=strlen(attr);

        s = getnAttr(node, attr, len);

        if(s)
        {
            if( txt2[i] )
            {
                int len,bss=0;
                const char *val = txt2[i];

                while(isspace(*val)) val++;
                len = strlen(val);
                /* len>0 first: val[-1] read on an empty value */
                while(len>0 && isspace(val[len-1])) len--;

                if(*val == '"')
                {
                    const char *p;

                    val++;
                    p=val;
                    len=0;
                    while(*p && (*p!='"'||*(p-1) =='\\') )
                    {
                        p++;
                        len++;
                    }
                }
                else if (*val == '\'')
                {
                    const char *p;

                    val++;
                    p=val;
                    len=0;
                    while(*p && (*p!='\''||*(p-1) =='\\') )
                    {
                        len++;
                        p++;
                    }
                }

                // count backslashes
                {
                    int j=0,lastslash=0;
                    for(;j<len;j++)
                    {
                        if(val[j]=='\\')
                        {
                            if(!lastslash) bss++;
                            lastslash=!lastslash;
                        }
                        else
                            lastslash=0;
                    }
                }

                // non-glob match
                {
                    int vl=len-bss;
                    int sl=strlen(s);
                    if( !strncmp_no_bs(s,val,(sl>vl?sl:vl)) )
                        return 1;
                }

                //match a glob too
                if(len>1 && val[len-1] == '*' && val[len-2] != '\\')
                {
                    if(!strncmp_no_bs(s,val,len-(1+bss)))
                        return 1;
                }
                if(*val== '*')
                {
                    const char *sstart;

                    val++;
                    len--;
                    sstart = &s[strlen(s) - (len-bss)];
                    if(!strncmp_no_bs(sstart,val,len-(1+bss)))
                        return 1;
                }
            }
            else
                return 1;
        }
    }

    return 0;
}

static int findfunc_class (TidyNode node, const char **txt, const char **txt2, int ntxt){
    const char *e, *p, *val, *classes=getAttr(node,"class");
    int i=0, len, vallen=0, bss=0;
    int matchdir=0; //direction of glob match, or 0 for no glob

    if(!classes)
        return 0;


    for (;i<ntxt;i++)
    {
        p=classes;

        while(isspace(*p))p++;

        val = txt[i];
        while(isspace(*val))val++;
        vallen=strlen(val);
        /* vallen>0 first: val[-1] read on an empty value */
        while(vallen>0 && isspace(val[vallen-1]))vallen--;

        matchdir=0;

        // count backslashes
        bss=0;
        {
            int j=0,lastslash=0;
            for(;j<vallen;j++)
            {
                if(val[j]=='\\')
                {
                    if(!lastslash) bss++;
                    lastslash=!lastslash;
                }
                else
                    lastslash=0;
            }
        }

        vallen -= bss;  //backslashes are skipped in strncmp_no_bs, len is minus num of backslashes
        //match a glob too
        if(vallen>1 && val[vallen-1] == '*' && val[vallen-2]!='\\')
        {
            matchdir=1;
            vallen--;
        }
        else if (*val == '*')
        {
            vallen--;
            val++;
            matchdir=-1;
        }

        e=p;
        while(1)
        {
            while(!isspace(*e) && *e!='\0') e++;
            len=(int)(e-p);

            if(matchdir<0)
            {
                int diff= (len - vallen);

                if(diff>0)
                {
                    p+=diff;
                    len = vallen;
                }
            }
            else if(matchdir>0)
                len=vallen;
            else if(len < vallen) //if non-glob, use larger of len and vallen
                len=vallen;

            if(len>0)
            {
                //printf("strncmp_no_bs('%s', '%s', %d)\n",p, val, len);
                if(!strncmp_no_bs(p, val, len))
                    return 1;
            }
            while(isspace(*e)) e++;

            if(*e=='\0') break;
            p=e;
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

    doc=get_tdoc(ctx, -1);

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
duk_ret_t duk_rp_html_node_pp(duk_context *ctx);
static void pushfuncs(duk_context *ctx)
{
    duk_push_c_function(ctx, duk_rp_html_totext, 1);
    duk_put_prop_string(ctx, -2, "toText");

    duk_push_c_function(ctx, duk_rp_html_tomarkdown, 1);
    duk_put_prop_string(ctx, -2, "toMarkdown");

    duk_push_c_function(ctx, duk_rp_html_tohtml, 1);
    duk_put_prop_string(ctx, -2, "toHtml");

    duk_push_c_function(ctx,  tohtmlobj, 0);
    duk_put_prop_string(ctx, -2, "toObj");

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

    duk_push_c_function(ctx, duk_rp_html_destroy, 0);
    duk_put_prop_string(ctx, -2, "destroy");

    duk_push_c_function(ctx, duk_rp_html_getdocument, 0);
    duk_put_prop_string(ctx, -2, "getDocument");

    duk_push_c_function(ctx, duk_rp_html_node_pp, 2);
    duk_put_prop_string(ctx, -2, "prettyPrint");

}

/* push new array of the unique nodes
   in array at arr_idx            */
/* Small counts stay on the stack and allocate nothing, as the original
   VLAs did; only larger ones go to the heap. 64 nodes is 512 bytes. */
#define UNIQ_NODES_ONSTACK 64
#define PEND_NODES_ONSTACK 64

static void uniq_array_nodes(duk_context *ctx, duk_idx_t arr_idx)
{
    int len;
    TidyNode onstack[UNIQ_NODES_ONSTACK];
    TidyNode *node=NULL;
    duk_idx_t buf_idx=0;
    int pushed=0;

    /* normalize first: a buffer pushed below would shift a negative
       caller index */
    arr_idx = duk_normalize_index(ctx, arr_idx);
    len = (int)duk_get_length(ctx, arr_idx);

    if(len)
    {
        if(len <= UNIQ_NODES_ONSTACK)
            node = onstack;
        else
        {
            /* Value stack, not malloc: the loop can throw and duktape
               frees this on unwind. Below the result array so the loop's
               -2 still names it; removed at the end. */
            node = (TidyNode *) duk_push_fixed_buffer(ctx,
                       (duk_size_t)len * sizeof(TidyNode));
            buf_idx = duk_normalize_index(ctx, -1);
            pushed = 1;
        }
    }

    duk_push_array(ctx);

    if(len)
    {
        TidyNode cur;
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
        /* drop the buffer, leaving only the new array for the caller */
        if(pushed)
            duk_remove(ctx, buf_idx);
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

    duk_get_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("validity"));
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("validity"));

    duk_get_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("errbuf"));
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("errbuf"));

    pushfuncs(ctx);

//    duk_pull(ctx, arr_idx);
    uniq_array_nodes(ctx, arr_idx);

    /* `length` must be counted AFTER de-duplication.  uniq_array_nodes() leaves
       a NEW array holding only the unique nodes on the stack, and that array --
       not arr_idx -- is what gets stored as "nodes" just below.  Taking the
       length from arr_idx beforehand counted duplicates, so a selection like
       parent() reported .length == 4 while toHtml() and getElement() returned
       the 1 node it actually held.
       Stack here is [ this, obj, uniq_arr ], so the object is at -3. */
    duk_push_number(ctx, (double) duk_get_length(ctx, -1));
    duk_put_prop_string(ctx, -3, "length");

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

    /* An empty name would spin forever below: strlen() is 0, so end==class
       and strstr(end,"") returns end again. Nothing can match it anyway. */
    if(!classname || !*classname)
    {
        *classattr=classes;
        if(classpos)
            *classpos=NULL;
        return 0;
    }

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

    /* isvalname("") is true, and appending it would leave a trailing space
       in the class attribute. No-op, as removeClass("") does. */
    if(!*cname)
        return 1;

    tdoc=get_tdoc(ctx, -1);

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
                /* heap: both lengths are caller/document controlled */
                char *newattr=NULL;

                REMALLOC(newattr, strlen(classattr) + strlen(cname) + 2);
                strcpy(newattr, classattr);
                strcat(newattr, " ");
                strcat(newattr, cname);
                addAttr(tdoc, node, "class", newattr);
                free(newattr);
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
        *cn = REQUIRE_STRING(ctx, 0, "html.removeClass - first argument must be a string (attr name)");
    int i=0, len;
    TidyNode node;
    TidyDoc tdoc;
    int cname_len;
    char *cname=NULL;

    while(isspace(*cn))cn++;
    cname_len=strlen(cn);
    while(isspace(cn[cname_len - 1]) && cname_len>0) cname_len--;

    duk_push_this(ctx);

    if(!cname_len)
        return 1;

    cname=strndup(cn, cname_len);

    tdoc=get_tdoc(ctx, -1);

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

                /* heap: the class attribute is document controlled */
                char *newattr=NULL;

                REMALLOC(newattr, strlen(classattr) + 1);
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
                free(newattr);
            }
        }

    }
    free(cname);
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
        duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("tdoc"));
        srcdoc=duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("nodes"));
    }
    else
    {
        /* the nodes to insert */
        if( !duk_is_object(ctx,0) || !duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("nodes")))
            RP_THROW(ctx, "html.append - first argument must be an html object or html text");

        srcdoc=get_tdoc(ctx, 0);
    }

    duk_remove(ctx, 0);

    ilen=duk_get_length(ctx, 0);

    duk_push_this(ctx);
    tdoc=get_tdoc(ctx, -1);

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
        /* Large counts on the value stack, not malloc: the block can
           throw and duktape frees it on unwind. Left there -- the return
           value is the top, and _pend indexes only INSERTNODES..RETNODES. */
        TidyNode ins_onstack[PEND_NODES_ONSTACK];
        TidyNode *ins_nodes;

        if(ilen <= PEND_NODES_ONSTACK)
            ins_nodes = ins_onstack;
        else
            ins_nodes = (TidyNode *) duk_push_fixed_buffer(ctx,
                            (duk_size_t)ilen * sizeof(TidyNode));

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
        /* no free: stack array, or value-stack buffer the frame frees */
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

    tdoc=get_tdoc(ctx, -3);

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
    const char *aname = REQUIRE_STRING(ctx, 0, "html.removeAttr - first argument must be a string (attr name)");
    int i=0, len;
    TidyNode node;
    TidyDoc tdoc;
    TidyAttr attr;
    ctmbstr key;
    int aname_len;

    while(isspace(*aname))aname++;
    aname_len=strlen(aname);
    while(isspace(aname[aname_len - 1]) && aname_len>0) aname_len--;

    duk_push_this(ctx);

    if(!aname_len)
        return 1;

    tdoc=get_tdoc(ctx, -1);

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
            if (!key) continue; /* nameless attr: malformed markup, see NULL_ATTR_NAME */
            if (!strncasecmp(key, aname, aname_len))
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

    tdoc=get_tdoc(ctx, -1);

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

    tdoc=get_tdoc(ctx, this_idx);

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
            ctmbstr aname = tidyAttrName(attr);

            /* No name, no object key: duk_put_prop_string() only
               asserts non-NULL, so release builds key it "null". */
            if (!aname) continue;

            duk_push_string(ctx, (const char *) tidyAttrValue(attr));
            duk_put_prop_string(ctx, -2, (const char *) aname);
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

// turn thisOption into this-option
static char * fixkey(const char *key)
{
    char *ret = NULL;
    int i=0;

    REMALLOC(ret, strlen(key) * 2 + 1); /* F-low: +1 so the NUL fits even for an empty key */
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



int tidyDocSaveStreamFrom( TidyDocImpl* doc, StreamOut* out, Node *node )
{
    Bool showMarkup  = cfgBool( doc, TidyShowMarkup );
    Bool forceOutput = cfgBool( doc, TidyForceOutput );
    Bool outputBOM   = ( cfgAutoBool(doc, TidyOutputBOM) == TidyYesState );
    Bool smartBOM    = ( cfgAutoBool(doc, TidyOutputBOM) == TidyAutoState );
    Bool xmlOut      = cfgBool( doc, TidyXmlOut );
    Bool xhtmlOut    = cfgBool( doc, TidyXhtmlOut );

    Bool dropComments = cfgBool(doc, TidyHideComments);
    Bool makeClean    = cfgBool(doc, TidyMakeClean);
    Bool asciiChars   = cfgBool(doc, TidyAsciiChars);
    Bool makeBare     = cfgBool(doc, TidyMakeBare);
    Bool ppWithTabs   = cfgBool(doc, TidyPPrintTabs);
    TidyAttrSortStrategy sortAttrStrat = cfg(doc, TidySortAttributes);
    TidyConfigChangeCallback callback = doc->pConfigChangeCallback;
    doc->pConfigChangeCallback = NULL;

    if (ppWithTabs)
        TY_(PPrintTabs)();
    else
        TY_(PPrintSpaces)();

    if (dropComments)
        TY_(DropComments)(doc, node);

    if (makeClean)
    {
        /* noop */
        TY_(DropFontElements)(doc, node, NULL);
    }

    if ((makeClean && asciiChars) || makeBare)
        TY_(DowngradeTypography)(doc, node);

    if (makeBare)
        /* Note: no longer replaces &nbsp; in */
        /* attribute values / non-text tokens */
        TY_(NormalizeSpaces)(doc->lexer, node);
    else
        TY_(ReplacePreformattedSpaces)(doc, node);

    TY_(SortAttributes)(doc, node, sortAttrStrat);

    if ( showMarkup && (doc->errors == 0 || forceOutput) )
    {
        /* Output a Byte Order Mark if required */
        if ( outputBOM || (doc->inputHadBOM && smartBOM) )
            TY_(outBOM)( out );

        /* No longer necessary. No DOCTYPE == HTML 3.2,
        ** which gives you only the basic character entities,
        ** which are safe in any browser.
        ** if ( !TY_(FindDocType)(doc) )
        **    TY_(SetOptionBool)( doc, TidyNumEntities, yes );
        */

        doc->docOut = out;
        if ( xmlOut && !xhtmlOut )
            TY_(PPrintXMLTree)( doc, NORMAL, 0, node );
        else
            TY_(PPrintTree)( doc, NORMAL, 0, node );

        TY_(PFlushLine)( doc, 0 );
        doc->docOut = NULL;
    }

    TY_(ResetConfigToSnapshot)( doc );
    doc->pConfigChangeCallback = callback;

    if ( doc->errors > 0 )
        return 2;
    if ( doc->warnings > 0 || doc->accessErrors > 0 )
        return 1;
    return 0;
}

int tidySaveBufferFrom ( TidyDoc tdoc, TidyBuffer* outbuf, Node *node )
{
    TidyDocImpl* doc = tidyDocToImpl( tdoc );

    int status = -1;
    if ( outbuf )
    {
        uint outenc = cfg( doc, TidyOutCharEncoding );
        uint nl = cfg( doc, TidyNewline );
        StreamOut* out = TY_(BufferOutput)( doc, outbuf, outenc, nl );

        status = tidyDocSaveStreamFrom( doc, out, node );
        TidyDocFree( doc, out );
    }
    return status;
}

duk_ret_t _html_node_pp(duk_context *ctx, TidyDoc tdoc, Node *node)
{
    TidyBuffer output = {0};
    TidyBuffer *tidy_errbuf;

    duk_push_this(ctx);

    tdoc=get_tdoc(ctx, -1);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("nodes"));

    /* get the first node */
    if(!duk_get_prop_index(ctx , -1, 0))
        RP_THROW(ctx, "html.prettyPrint - html Object contains no nodes");
    node = (Node*)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("errbuf"));
    tidy_errbuf=duk_get_pointer(ctx, -1);

    if(duk_is_number(ctx,0))
    {
        (void)REQUIRE_POSINT(ctx, 0, "html.prettyPrint - first argument must be an object of options or a positive int (indent)");
        int ret=tidyOptParseValue(tdoc, "indent", "1");
        if(!ret)
            RP_THROW(ctx, "html.prettyPrint - error setting 'indent' to '1' - %s",tidy_errbuf->bp);

        ret = tidyOptParseValue(tdoc, "indent-spaces", duk_to_string(ctx, 0));

        if(!ret)
            RP_THROW(ctx, "html.prettyPrint - error setting 'indent-spaces' to '%s' - %s", duk_to_string(ctx, 0),tidy_errbuf->bp);

        if(duk_is_number(ctx, 1))
        {
            (void)REQUIRE_POSINT(ctx, 1, "html.prettyPrint - second argument must be a positive int (wrap)");
            ret = tidyOptParseValue(tdoc, "wrap", duk_to_string(ctx, 1));
            if(!ret)
                RP_THROW(ctx, "html.prettyPrint - error setting 'indent-spaces' to '%s' - %s", duk_to_string(ctx, 0),tidy_errbuf->bp);
        }

    }
    else if(duk_is_object(ctx, 0) && !duk_is_function(ctx, 0) && !duk_is_array(ctx, 0) )
    {
        duk_enum(ctx, 0, 0);
        while(duk_next(ctx, -1, 1))
        {
            const char *key=duk_get_string(ctx, -2);
            const char *val=duk_safe_to_string(ctx, -1);
            char *dashedKey = fixkey(key);
            int ret=tidyOptParseValue(tdoc, (ctmbstr)dashedKey, (ctmbstr)val);
            free(dashedKey);
            if(!ret)
                RP_THROW(ctx, "html.prettyPrint - error setting '%s' to '%s' - %s", key, val,tidy_errbuf->bp);
            duk_pop_2(ctx);
        }
        duk_pop(ctx);
    }
    else if (!duk_is_undefined(ctx, 0))
        RP_THROW(ctx, "html.prettyprint - first argument, if present, must be an object of options or a positive int (indent)");

    if(node)
        tidySaveBufferFrom(tdoc, &output, node);
    else
        tidySaveBuffer(tdoc, &output);

    duk_push_string(ctx, (char *)output.bp);

    if (output.bp)
        tidyBufFree( &output );

    return 1;
}

duk_ret_t duk_rp_html_node_pp(duk_context *ctx)
{
    TidyDoc tdoc;
    Node *node=NULL;

    duk_push_this(ctx);

    tdoc=get_tdoc(ctx, -1);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("nodes"));

    /* get the first node */
    if(!duk_get_prop_index(ctx , -1, 0))
        RP_THROW(ctx, "prettyPrint - html Object contains no nodes");
    node = (Node*)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);

    return _html_node_pp(ctx, tdoc, node);
}

duk_ret_t duk_rp_html_pp(duk_context *ctx)
{
    TidyDoc tdoc;

    duk_push_this(ctx);

    tdoc=get_tdoc(ctx, -1);

    return _html_node_pp(ctx, tdoc, NULL);
}

#define htmlSetErr(e) do{\
    if(e<0) RP_THROW(ctx, "html.newDocument() - %s", strerror(-e));\
    if(tidy_errbuf->size && e>0) {\
        duk_push_string(ctx, (char *)tidy_errbuf->bp);\
        duk_replace(ctx, err_idx);\
    }\
} while (0)

duk_ret_t duk_rp_htmlparse(duk_context *ctx)
{
//    const char *html = REQUIRE_STRING(ctx, 0, "html.newDocument: first argument must be a string (html document)");
    const char *html="";
    int Terr=0;
    duk_idx_t err_idx, obj_idx=-1, html_idx=0;
    TidyDoc tdoc;
    TidyBuffer *tidy_errbuf = NULL;
    TidyNode root;
    duk_size_t size=0;

    // garbage collection and finalization isn't happening
    // until end of large for loops with html.newDocument() in it
    // but garbage collection is very slow.  Better to use hdoc.destroy()
    rp_html_newdocs++;
    if(rp_html_newdocs > 499)
    {
        duk_gc(ctx, 0);
        rp_html_newdocs=0;
    }

    if(duk_is_object(ctx, 1))
        obj_idx = 1;


    if(duk_is_buffer_data(ctx, html_idx))
        html = (const char *) duk_get_buffer_data(ctx, html_idx, &size);
    else if (duk_is_string(ctx, html_idx) )
        html = duk_get_string(ctx, html_idx);
    else if (duk_is_object(ctx, html_idx))
    {
        _obj_to_html(ctx, html_idx);
        html = duk_get_string(ctx, -1);
    }
    else if (!duk_is_undefined(ctx, html_idx))
        RP_THROW(ctx, "html.newDocument: first argument must be a string, buffer or object produced by html.toObj() (i.e., the text/html)");

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

        /* tidyBufAttach takes a 32 bit uint, so a larger document would
           be silently truncated and half parsed. Refuse it instead. */
        if (size > (duk_size_t)UINT_MAX)
            RP_THROW(ctx, "html.newDocument - document too large (%llu bytes, max %u)",
                     (unsigned long long)size, (unsigned)UINT_MAX);

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

    duk_push_c_function(ctx, duk_rp_html_pp, 2);
    duk_put_prop_string(ctx, -2, "prettyPrint");

    root=tidyGetRoot(tdoc);

    // a marker copied into every new html object created from this one
    // i.e. when using var b=h.findTag("body");
    // so we know when the doc has been destroyed.
    duk_push_object(ctx);
    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "valid");
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("validity"));

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

  duk_push_c_function(ctx, obj_to_html, 1);
  duk_put_prop_string(ctx, -2, "objToHtml");

  return 1;
}
