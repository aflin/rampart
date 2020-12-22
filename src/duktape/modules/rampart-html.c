/* Copyright (C) 2020 Aaron Flin - All Rights Reserved
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
#include "../../rp.h"
#include "../core/duktape.h"


duk_ret_t duk_rp_html_finalizer(duk_context *ctx)
{
    TidyBuffer *errbuf;
    TidyDoc tdoc;

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("errbuf"));
    errbuf=duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    if (errbuf->bp)
        tidyBufFree( errbuf );
    free(errbuf);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("tdoc"));
    tdoc=duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    tidyRelease( tdoc );
    return 0;
}

#define htmlSetErr(e) do{\
    if(e<0) RP_THROW(ctx, "html.parse - %s", strerror(-e));\
    if(tidy_errbuf->size && e>0) {\
        duk_push_string(ctx, (char *)tidy_errbuf->bp);\
        duk_replace(ctx, err_idx);\
    }\
} while (0)

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

/*
TidyTag_UNKNOWN
TidyTag_A
TidyTag_ABBR
TidyTag_ACRONYM
TidyTag_ALIGN
TidyTag_APPLET
TidyTag_AREA
TidyTag_AUDIO
TidyTag_B
TidyTag_BASE
TidyTag_BASEFONT
TidyTag_BDO
TidyTag_BGSOUND
TidyTag_BIG
TidyTag_BLINK
TidyTag_BODY
TidyTag_BUTTON
TidyTag_CAPTION
TidyTag_CENTER
TidyTag_CITE
TidyTag_CODE
TidyTag_COL
TidyTag_COLGROUP
TidyTag_COMMENT
TidyTag_DEL
TidyTag_DFN
TidyTag_DIR
TidyTag_EM
TidyTag_EMBED
TidyTag_FONT
TidyTag_FRAME
TidyTag_FRAMESET
TidyTag_HEAD
TidyTag_HTML
TidyTag_I
TidyTag_IFRAME
TidyTag_ILAYER
TidyTag_IMG
TidyTag_INPUT
TidyTag_INS
TidyTag_ISINDEX
TidyTag_KBD
TidyTag_KEYGEN
TidyTag_LABEL
TidyTag_LAYER
TidyTag_LEGEND
TidyTag_LINK
TidyTag_LISTING
TidyTag_MAP
TidyTag_MATHML
TidyTag_MARQUEE
TidyTag_MENU
TidyTag_META
TidyTag_MULTICOL
TidyTag_NOBR
TidyTag_NOEMBED
TidyTag_NOFRAMES
TidyTag_NOLAYER
TidyTag_NOSAVE
TidyTag_OBJECT
TidyTag_OPTGROUP
TidyTag_OPTION
TidyTag_PARAM
TidyTag_PICTURE
TidyTag_PLAINTEXT
TidyTag_Q
TidyTag_RB
TidyTag_RBC
TidyTag_RP
TidyTag_RT
TidyTag_RTC
TidyTag_RUBY
TidyTag_S
TidyTag_SAMP
TidyTag_SCRIPT
TidyTag_SELECT
TidyTag_SERVER
TidyTag_SERVLET
TidyTag_SMALL
TidyTag_SPACER
TidyTag_SPAN
TidyTag_STRIKE
TidyTag_STRONG
TidyTag_STYLE
TidyTag_SUB
TidyTag_SUP
TidyTag_SVG
TidyTag_TT
TidyTag_U
TidyTag_VAR
TidyTag_WBR
TidyTag_XMP
TidyTag_NEXTID
TidyTag_BDI
TidyTag_COMMAND
TidyTag_DATALIST
TidyTag_MARK
TidyTag_MENUITEM
TidyTag_METER
TidyTag_OUTPUT
TidyTag_PROGRESS
TidyTag_SOURCE
TidyTag_SUMMARY
TidyTag_TEMPLATE
TidyTag_TIME
TidyTag_TRACK
*/

#define optAltText  1 << 0
#define optMetaDesc 1 << 1
#define optMetaKeyw 1 << 2
#define optEnumLsts 1 << 3
#define optTitleTxt 1 << 4
#define optALinks   1 << 5

#define testOpt(optOption) ( (optOption) & opts) 

const char *getAttr(TidyNode node, const char *name)
{
    TidyAttr attr;
    ctmbstr key;

    for ( attr=tidyAttrFirst(node); attr; attr=tidyAttrNext(attr) ) {
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

    for ( attr=tidyAttrFirst(node); attr; attr=tidyAttrNext(attr) ) {
        key=tidyAttrName(attr);
        if (!strncasecmp(key, name, len))
            return (const char *)tidyAttrValue(attr);
    }
    return (const char *)NULL;
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
            tidyBufAppend(buf, "=\"", 2);
            tidyBufAppend(buf, (void*)v, vlen);
            tidyBufAppend(buf, "\"", 1);
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

TidyBuffer *dumpText(TidyDoc doc, TidyNode start, TidyBuffer *buf, int listno, int tag_start_addnl, int opts)
{
    TidyNode child;
    TidyNodeType type;

    for ( child = tidyGetChild(start); child; child = tidyGetNext(child) )
    {
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
                tag_start_addnl=0; /* flag that last round ended in text */
                break;
            }
            case TidyNode_Start:
            {
                TidyTagId id = tidyNodeGetId( child );
                if ( id==TidyTag_SCRIPT || id==TidyTag_STYLE)
                {
                    break;
                }
                else
                {
                    int addnl=0;

                    if(isBlockTag(id))
                    {
                        addnl=1;
                        /* only if last was text node */
                        tag_start_addnl = !tag_start_addnl;
                    }
                    else
                    {
                        tag_start_addnl = 0;
                    }

                    if (tag_start_addnl && buf->size>0 && buf->bp[buf->size -1] != '\n')
                        tidyBufAppend(buf, "\n", 1);
                    
                    /* mark next round as ending with a tag */
                    tag_start_addnl=addnl;

                    if (id==TidyTag_OL)
                    {
                        /* listno is a flag and a counter */
                        listno=1;
                    }
                    else if (id==TidyTag_UL)
                    {
                        listno=0;
                    }
                    /* number ordered lists and put "* " in front of unordered lists */
                    else if (id==TidyTag_LI && testOpt(optEnumLsts) )
                    {
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
                    else if (id==TidyTag_DD && testOpt(optEnumLsts) )
                    {
                        tidyBufAppend(buf, "    ", 4);
                    }
                    /* get alt text from images */
                    else if ( id==TidyTag_IMG && testOpt(optAltText) )
                    {
                        const char *alttext=getAttr(child, "alt");
                        if(alttext)
                        {
                            int len = strlen(alttext)+3;
                            char lbuf[len];

                            sprintf(lbuf, " %s ", alttext);
                            tidyBufAppend(buf, lbuf, len-1);
                        }
                    }
                    /* get meta name=keywords|description */
                    else if (id==TidyTag_META)
                    {
                        const char *name=getAttr(child, "name");
                        if (name)
                        {
                            if( 
                                (testOpt(optMetaDesc) && !strcasecmp(name,"description"))
                                ||
                                (testOpt(optMetaKeyw) && !strcasecmp(name,"keywords"))
                            )
                            {
                                const char *cont=getAttr(child, "content");
                                if(cont)
                                {
                                    int len = strlen(cont)+3;
                                    char lbuf[len];

                                    sprintf(lbuf, "\n%s\n", cont);
                                    tidyBufAppend(buf, lbuf, len-1);
                                }
                                
                            }
                        }
                    }

                    /* print title text (viewable if mouse hovers) */
                    if (testOpt(optTitleTxt))
                    {
                        const char *title = getAttr(child, "title");

                        if(title)
                        {
                            int len = strlen(title)+3;
                            char lbuf[len];

                            sprintf(lbuf, " %s ", title);
                            tidyBufAppend(buf, lbuf, len-1);
                        }
                    }

                    /* seperate a tags from surrounding text */
                    if(id == TidyTag_A && buf->size>0 && buf->bp[buf->size -1] != '\n' && buf->bp[buf->size -1] != ' ')
                        tidyBufAppend(buf, " ", 1);

                    buf=dumpText(doc, child, buf, listno, tag_start_addnl, opts);

                    /* seperate a tags from surrounding text */
                    if(id == TidyTag_A && buf->size>0 && buf->bp[buf->size -1] != '\n' && buf->bp[buf->size -1] != ' ')
                        tidyBufAppend(buf, " ", 1);

                    /* print anchor links */
                    if(id==TidyTag_A && testOpt(optALinks) )
                    {
                        const char *href = getAttr(child, "href");

                        if(href)
                        {
                            int len = strlen(href)+4;
                            char lbuf[len];

                            sprintf(lbuf, " (%s)", href);
                            tidyBufAppend(buf, lbuf, len-1);
                        }
                    }
                    
                    if (addnl && buf->size>0 && buf->bp[buf->size -1] != '\n')
                        tidyBufAppend(buf, "\n", 1);

                    break;
                }
            }
            default:
                break;
        }

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
    int makearray=-1;
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
        ret=dumpHtml(doc, start, ret, indent, 0, 1);

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

duk_ret_t duk_rp_html_totext(duk_context *ctx)
{
    TidyNode start;
    TidyDoc doc;
    TidyBuffer buf, *ret;
    int i=0, opts=optAltText|optMetaDesc|optMetaKeyw|optEnumLsts;
    int makearray=-1;
    duk_idx_t this_idx;

    ret=&buf;
    tidyBufInit(ret);


    if (duk_is_object(ctx, 0))
    {
        setoptbool("imageAltText", optAltText);
        setoptbool("metaDescription",optMetaDesc);
        setoptbool("metaKeywords",optMetaKeyw);
        setoptbool("titleText",optTitleTxt);
        setoptbool("aLinks",optALinks);
        setoptbool("enumerateLists",optEnumLsts);

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
        ret=dumpText(doc, start, ret, 0, 0, opts);

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
        if(type == TidyNode_Start) 
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
        if(type == TidyNode_Start) 
        {
            if( (ffunc[findType])(child, txt, txt2, ntxt) )
            {
                duk_uarridx_t len  = (duk_uarridx_t) duk_get_length(ctx, arr_idx);

                duk_push_pointer(ctx, (void*)child);
                duk_put_prop_index(ctx, arr_idx, len);
            }
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
        DUKREMALLOC(ctx, txt2, ntxts * sizeof(const char *));
        
        for(;i<ntxts;i++)
        {
            const char *val = strchr(txts[i], '=');
            
            if(val) val++;
            txt2[i] = val;
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
duk_ret_t duk_rp_html_slice(duk_context *ctx);
duk_ret_t duk_rp_html_eq(duk_context *ctx);
duk_ret_t duk_rp_html_getattr(duk_context *ctx);
duk_ret_t duk_rp_html_parent(duk_context *ctx);
duk_ret_t duk_rp_html_children(duk_context *ctx);
duk_ret_t duk_rp_html_next(duk_context *ctx);
duk_ret_t duk_rp_html_prev(duk_context *ctx);
duk_ret_t duk_rp_html_gettag(duk_context *ctx);

static void pushfuncs(duk_context *ctx)
{
    duk_push_c_function(ctx, duk_rp_html_totext, 1);
    duk_put_prop_string(ctx, -2, "toText");

    duk_push_c_function(ctx, duk_rp_html_tohtml, 1);
    duk_put_prop_string(ctx, -2, "toHtml");

    duk_push_c_function(ctx, duk_rp_html_gettag, 0);
    duk_put_prop_string(ctx, -2, "getTag");

    duk_push_c_function(ctx, duk_rp_html_slice, 2);
    duk_put_prop_string(ctx, -2, "slice");

    duk_push_c_function(ctx, duk_rp_html_eq, 1);
    duk_put_prop_string(ctx, -2, "eq");

    duk_push_c_function(ctx, duk_rp_html_getattr, 1);
    duk_put_prop_string(ctx, -2, "getAttr");

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

    duk_push_c_function(ctx, duk_rp_html_parent, 0);
    duk_put_prop_string(ctx, -2, "parent");

    duk_push_c_function(ctx, duk_rp_html_children, 1);
    duk_put_prop_string(ctx, -2, "children");

    duk_push_c_function(ctx, duk_rp_html_next, 0);
    duk_put_prop_string(ctx, -2, "next");

    duk_push_c_function(ctx, duk_rp_html_prev, 0);
    duk_put_prop_string(ctx, -2, "prev");
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

    duk_pull(ctx, arr_idx);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("nodes"));


    duk_get_prop_string(ctx, -2, "root");
    duk_put_prop_string(ctx, -2, "root");

}
duk_ret_t duk_rp_html_gettag(duk_context *ctx)
{
    TidyNode node;
    TidyBuffer buf, *ret;
    int i=0;
    duk_idx_t this_idx;

    ret=&buf;

    duk_push_this(ctx);
    this_idx=duk_get_top_index(ctx);

    duk_push_array(ctx);

    /* loop over nodes, create tag, append to array */
    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("nodes"));
    duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
    while(duk_next(ctx, -1, 1))
    {
        tidyBufInit(ret);

        node=duk_get_pointer(ctx, -1);
        duk_pop_2(ctx);

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
    int start=REQUIRE_INT(ctx, 0, "html.slice - first argument must be an int (start)");
    int end=REQUIRE_INT(ctx, 1, "html.slice - second argument must be an int (end)");
    int i=0,j=0,len;

    duk_push_this(ctx);
    duk_push_array(ctx);
    duk_get_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("nodes"));

    len=(int)duk_get_length(ctx, 4);
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
        DUKREMALLOC(ctx, txts, sizeof(const char *));
        txts[0]=duk_get_string(ctx,0);
    }
    else if (duk_is_array(ctx,0))
    {
        int i=0;

        ntxt=(int)duk_get_length(ctx, 0);
        DUKREMALLOC(ctx, txts, ntxt * sizeof(const char *));

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

duk_ret_t duk_rp_htmlparse(duk_context *ctx)
{
//    const char *html = REQUIRE_STRING(ctx, 0, "html.parse: first argument must be a string (html document)");
    const char *html=NULL;
    int Terr=0;    
    duk_idx_t err_idx;
    TidyDoc tdoc;
    TidyBuffer output = {0};
    TidyBuffer *tidy_errbuf = NULL;
    TidyNode root;
    duk_size_t size=0;

    if(duk_is_buffer(ctx, 0))
        html = (const char *) duk_get_buffer(ctx, 0, &size);
    else if (duk_is_string(ctx, 0) )
        html = duk_get_string(ctx, 0);
    else
        RP_THROW(ctx, "html.parse: first argument must be a string or buffer(html document)");

    tidy_errbuf = calloc( 1, sizeof(TidyBuffer));

    duk_push_object(ctx);
    duk_push_string(ctx,"");
    err_idx=duk_get_top_index(ctx);

    tdoc = tidyCreate();
    tidyOptSetBool(tdoc, TidyForceOutput, yes);
    tidyOptSetBool(tdoc, TidyMark, no);
    tidySetErrorBuffer( tdoc, tidy_errbuf );

    if(duk_is_object(ctx, 1) && !duk_is_function(ctx, 1) && !duk_is_array(ctx, 1) )
    {
        duk_enum(ctx, 1, 0);
        while(duk_next(ctx, -1, 1))
        {
            const char *key=duk_get_string(ctx, -2);
            const char *val=duk_safe_to_string(ctx, -1);
            int ret=tidyOptParseValue(tdoc, (ctmbstr)key, (ctmbstr)val);
            if(!ret)
                RP_THROW(ctx, "html.parse - error setting '%s' to '%s' - %s", key, val,tidy_errbuf->bp);            
            duk_pop_2(ctx);
        }
        duk_pop(ctx);
    }
    else if (!duk_is_undefined(ctx, 1))
        RP_THROW(ctx, "html.parse - second argument must be an object (tidy options)");

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
    
    tidySaveBuffer(tdoc, &output);    

    duk_put_prop_string(ctx, -2, "errMsg");

    duk_push_string(ctx, (char *)output.bp);
    duk_put_prop_string(ctx, -2, "prettyHtml");

    duk_push_pointer(ctx, (void *)tdoc);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("tdoc"));

    duk_push_pointer(ctx, (void *)tidy_errbuf);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("errbuf"));
    
    root=tidyGetRoot(tdoc);

    duk_push_array(ctx);
    duk_push_pointer(ctx, (void *)root);
    duk_put_prop_index(ctx, -2, 0);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("nodes"));

    if (output.bp)
        tidyBufFree( &output );
    
    duk_push_c_function(ctx, duk_rp_html_finalizer, 1);
    duk_set_finalizer(ctx, -2);

    duk_push_number(ctx, 1);
    duk_put_prop_string(ctx, -2, "length");

    pushfuncs(ctx);

    duk_dup(ctx, -1);
    duk_put_prop_string(ctx, -2, "root");
    return 1;
    
}



/* **************************************************
   Initialize module 
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
  duk_push_object(ctx); // the return object

  duk_push_c_function(ctx, duk_rp_htmlparse, 2);

  duk_put_prop_string(ctx, -2, "parse");

  return 1;
}
