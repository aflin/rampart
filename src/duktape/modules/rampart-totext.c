#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#define RP_STRING_IMPLEMENTATION
#include "rampart.h"
#include "libdeflate.h"

/* file types we can identify */
typedef enum {
    FT_UNKNOWN = 0,
    FT_TEXT,
    FT_PLAINTEXT,  /* source code, config, etc. — return as-is */
    FT_HTML,
    FT_XML,
    FT_MARKDOWN,
    FT_LATEX,
    FT_RTF,
    FT_MAN,
    FT_PDF,
    FT_DOCX,
    FT_ODT,
    FT_EPUB,
    FT_DOC,
    FT_PPTX,
    FT_XLSX,
    FT_ODP,
    FT_ODS,
} filetype_t;

static const char *filetype_names[] = {
    "unknown",
    "text",
    "plaintext",
    "html",
    "xml",
    "markdown",
    "latex",
    "rtf",
    "man",
    "pdf",
    "docx",
    "odt",
    "epub",
    "doc",
    "pptx",
    "xlsx",
    "odp",
    "ods",
};

/* ================================================================
   FILE TYPE IDENTIFICATION
   ================================================================ */

static filetype_t identify_from_extension(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename)
        return FT_UNKNOWN;

    dot++;

    /* prose text — normalize paragraphs (undo hard line wrapping) */
    if(!strcasecmp(dot, "txt"))
        return FT_TEXT;

    /* structured/code plaintext — return as-is */
    if(!strcasecmp(dot, "csv")  || !strcasecmp(dot, "tsv")  ||
       !strcasecmp(dot, "json") || !strcasecmp(dot, "yaml") || !strcasecmp(dot, "yml") ||
       !strcasecmp(dot, "log")  || !strcasecmp(dot, "cfg")  || !strcasecmp(dot, "ini") ||
       !strcasecmp(dot, "conf") || !strcasecmp(dot, "toml") || !strcasecmp(dot, "env") ||
       !strcasecmp(dot, "sh")   || !strcasecmp(dot, "bash") || !strcasecmp(dot, "zsh") ||
       !strcasecmp(dot, "py")   || !strcasecmp(dot, "rb")   || !strcasecmp(dot, "pl")  ||
       !strcasecmp(dot, "js")   || !strcasecmp(dot, "ts")   || !strcasecmp(dot, "jsx") ||
       !strcasecmp(dot, "tsx")  || !strcasecmp(dot, "css")  || !strcasecmp(dot, "scss") ||
       !strcasecmp(dot, "c")    || !strcasecmp(dot, "h")    || !strcasecmp(dot, "cpp") ||
       !strcasecmp(dot, "hpp")  || !strcasecmp(dot, "java") || !strcasecmp(dot, "go")  ||
       !strcasecmp(dot, "rs")   || !strcasecmp(dot, "swift") || !strcasecmp(dot, "sql") ||
       !strcasecmp(dot, "r")    || !strcasecmp(dot, "lua")  || !strcasecmp(dot, "php") ||
       !strcasecmp(dot, "diff") || !strcasecmp(dot, "patch") || !strcasecmp(dot, "rst"))
        return FT_PLAINTEXT;
    if(!strcasecmp(dot, "html") || !strcasecmp(dot, "htm"))  return FT_HTML;
    if(!strcasecmp(dot, "xml") || !strcasecmp(dot, "docbook")) return FT_XML;
    if(!strcasecmp(dot, "md") || !strcasecmp(dot, "markdown")) return FT_MARKDOWN;
    if(!strcasecmp(dot, "tex") || !strcasecmp(dot, "latex"))   return FT_LATEX;
    if(!strcasecmp(dot, "rtf"))   return FT_RTF;
    if(!strcasecmp(dot, "pdf"))   return FT_PDF;
    if(!strcasecmp(dot, "docx"))  return FT_DOCX;
    if(!strcasecmp(dot, "odt"))   return FT_ODT;
    if(!strcasecmp(dot, "epub"))  return FT_EPUB;
    if(!strcasecmp(dot, "doc"))   return FT_DOC;
    if(!strcasecmp(dot, "pptx"))  return FT_PPTX;
    if(!strcasecmp(dot, "xlsx"))  return FT_XLSX;
    if(!strcasecmp(dot, "odp"))   return FT_ODP;
    if(!strcasecmp(dot, "ods"))   return FT_ODS;

    if(strlen(dot) == 1 && dot[0] >= '1' && dot[0] <= '9')
        return FT_MAN;

    return FT_UNKNOWN;
}

static filetype_t identify_zip_subtype(const unsigned char *buf, size_t len)
{
    if(len < 38)
        return FT_UNKNOWN;

    unsigned int fname_len = buf[26] | (buf[27] << 8);
    unsigned int extra_len = buf[28] | (buf[29] << 8);
    size_t fname_offset = 30;

    if(fname_offset + fname_len > len)
        return FT_UNKNOWN;

    const char *fname = (const char *)buf + fname_offset;

    /* ODF formats: first entry is uncompressed "mimetype" */
    if(fname_len == 8 && !memcmp(fname, "mimetype", 8))
    {
        size_t content_offset = fname_offset + fname_len + extra_len;
        if(content_offset + 20 <= len)
        {
            if(!memcmp(buf + content_offset, "application/epub+zip", 20))
                return FT_EPUB;
        }
        if(content_offset + 47 <= len)
        {
            const char *mt = (const char *)buf + content_offset;
            if(!memcmp(mt, "application/vnd.oasis.opendocument.text", 39))
                return FT_ODT;
            if(!memcmp(mt, "application/vnd.oasis.opendocument.presentation", 48))
                return FT_ODP;
            if(!memcmp(mt, "application/vnd.oasis.opendocument.spreadsheet", 46))
                return FT_ODS;
        }
        return FT_UNKNOWN;
    }

    /* OOXML formats: distinguish by first entry path or [Content_Types].xml */
    if(fname_len >= 4 && !memcmp(fname, "ppt/", 4))
        return FT_PPTX;

    if(fname_len >= 3 && !memcmp(fname, "xl/", 3))
        return FT_XLSX;

    if(fname_len >= 5 && !memcmp(fname, "word/", 5))
        return FT_DOCX;

    /* [Content_Types].xml — this is an OOXML file but we don't know which type.
       Scan subsequent local file headers for ppt/, xl/, or word/ entries. */
    if(fname_len >= 18 && !memcmp(fname, "[Content_Types].xml", 19 < fname_len ? 19 : fname_len))
    {
        unsigned int comp_size = buf[18] | (buf[19] << 8) | (buf[20] << 16) | (buf[21] << 24);
        size_t pos = fname_offset + fname_len + extra_len + comp_size;

        /* scan next few local file headers */
        for(int scan = 0; scan < 10 && pos + 30 < len; scan++)
        {
            if(buf[pos] != 'P' || buf[pos+1] != 'K' || buf[pos+2] != 3 || buf[pos+3] != 4)
                break;
            unsigned int fl = buf[pos+26] | (buf[pos+27] << 8);
            unsigned int el = buf[pos+28] | (buf[pos+29] << 8);
            unsigned int cs = buf[pos+18] | (buf[pos+19] << 8) | (buf[pos+20] << 16) | (buf[pos+21] << 24);
            const char *fn = (const char *)buf + pos + 30;

            if(fl >= 4 && !memcmp(fn, "ppt/", 4)) return FT_PPTX;
            if(fl >= 3 && !memcmp(fn, "xl/", 3))  return FT_XLSX;
            if(fl >= 5 && !memcmp(fn, "word/", 5)) return FT_DOCX;

            pos += 30 + fl + el + cs;
        }

        /* couldn't determine — caller will fall through to extension */
        return FT_UNKNOWN;
    }

    return FT_UNKNOWN;
}

static int has_html_signature(const unsigned char *buf, size_t len)
{
    size_t i = 0;
    while(i < len && isspace(buf[i])) i++;
    if(i >= len) return 0;

    size_t rem = len - i;
    const unsigned char *p = buf + i;

    if(rem >= 15 && !strncasecmp((const char*)p, "<!doctype html", 14))
        return 1;
    if(rem >= 5 && !strncasecmp((const char*)p, "<html", 5))
        return 1;
    if(rem >= 6 && !strncasecmp((const char*)p, "<head>", 6))
        return 1;
    if(rem >= 6 && !strncasecmp((const char*)p, "<head ", 6))
        return 1;

    return 0;
}

static int has_xml_signature(const unsigned char *buf, size_t len)
{
    size_t i = 0;
    while(i < len && isspace(buf[i])) i++;
    if(i >= len) return 0;

    size_t rem = len - i;
    const unsigned char *p = buf + i;

    if(rem >= 5 && !memcmp(p, "<?xml", 5))
        return 1;

    if(p[0] == '<')
    {
        const char *docbook_tags[] = {
            "<para", "<article", "<book", "<chapter", "<section",
            "<info", "<simpara", "<formalpara", NULL
        };
        for(int t = 0; docbook_tags[t]; t++)
        {
            size_t tlen = strlen(docbook_tags[t]);
            if(rem >= tlen + 1 && !strncasecmp((const char*)p, docbook_tags[t], tlen)
               && (p[tlen] == '>' || p[tlen] == ' ' || p[tlen] == '\n'))
                return 1;
        }
    }

    return 0;
}

static int has_man_signature(const unsigned char *buf, size_t len)
{
    if(len < 2) return 0;

    if(buf[0] == '\'' && buf[1] == '\\')  return 1;
    if(buf[0] == '.'  && buf[1] == '\\')  return 1;
    if(buf[0] == '.'  && buf[1] == 'T' && len >= 3 && buf[2] == 'H') return 1;
    if(buf[0] == '.'  && buf[1] == 'S' && len >= 3 && buf[2] == 'H') return 1;
    if(buf[0] == '.'  && buf[1] == 's' && len >= 3 && buf[2] == 'o') return 1;
    if(buf[0] == '.'  && buf[1] == 'D' && len >= 3 && buf[2] == 'D') return 1;

    return 0;
}

static int has_latex_signature(const unsigned char *buf, size_t len)
{
    size_t scan = len < 1024 ? len : 1024;

    const char *latex_cmds[] = {
        "\\documentclass", "\\usepackage", "\\begin{document}",
        "\\section{", "\\subsection{", "\\title{",
        "\\href{", "\\textbf{", "\\textit{",
        "\\includegraphics", "\\phantomsection",
        "\\hypertarget{", "\\emph{", "\\paragraph{",
        NULL
    };

    for(int c = 0; latex_cmds[c]; c++)
    {
        size_t clen = strlen(latex_cmds[c]);
        if(clen > scan) continue;
        for(size_t i = 0; i <= scan - clen; i++)
        {
            if(!memcmp(buf + i, latex_cmds[c], clen))
                return 1;
        }
    }

    return 0;
}

static filetype_t identify_content(const unsigned char *buf, size_t len,
                                   const char *filename)
{
    if(len == 0)
        return identify_from_extension(filename);

    if(len >= 5 && !memcmp(buf, "%PDF-", 5))
        return FT_PDF;

    if(len >= 4 && buf[0] == 0xD0 && buf[1] == 0xCF && buf[2] == 0x11 && buf[3] == 0xE0)
        return FT_DOC;

    if(len >= 5 && !memcmp(buf, "{\\rtf", 5))
        return FT_RTF;

    if(len >= 4 && !memcmp(buf, "PK\x03\x04", 4))
    {
        filetype_t zt = identify_zip_subtype(buf, len);
        if(zt != FT_UNKNOWN)
            return zt;
        return identify_from_extension(filename);
    }

    if(has_man_signature(buf, len))
        return FT_MAN;

    if(has_html_signature(buf, len))
        return FT_HTML;

    if(has_xml_signature(buf, len))
        return FT_XML;

    if(has_latex_signature(buf, len))
        return FT_LATEX;

    {
        size_t scan = len < 2048 ? len : 2048;
        int md_score = 0;
        for(size_t i = 0; i < scan; i++)
        {
            int bol = (i == 0 || buf[i-1] == '\n');
            if(buf[i] == '#' && bol)
                md_score += 2;
            if(i + 2 < scan && buf[i] == '!' && buf[i+1] == '[')
                md_score += 3;
            if(i + 2 < scan && bol && buf[i] == '`' && buf[i+1] == '`' && buf[i+2] == '`')
                md_score += 3;
            if(i + 3 < scan && bol && !memcmp(buf + i, "::::", 4))
                md_score += 3;
            if(i + 1 < scan && buf[i] == '{' && (buf[i+1] == '.' || buf[i+1] == '#'))
                md_score += 2;
            if(i + 3 < scan && buf[i] == '*' && buf[i+1] == '*' && buf[i+2] != ' ')
                md_score += 2;
        }
        if(md_score >= 5)
            return FT_MARKDOWN;
    }

    return identify_from_extension(filename);
}

/* ================================================================
   UTILITY: read file, normalize output
   ================================================================ */

static unsigned char *read_file_contents(const char *filename, size_t *out_len)
{
    struct stat st;
    if(stat(filename, &st) != 0)
        return NULL;

    size_t fsize = (size_t)st.st_size;
    FILE *f = fopen(filename, "rb");
    if(!f)
        return NULL;

    unsigned char *buf = NULL;
    REMALLOC(buf, fsize + 1);

    size_t nread = fread(buf, 1, fsize, f);
    fclose(f);

    buf[nread] = 0;
    *out_len = nread;
    return buf;
}

/* ================================================================
   GZIP DECOMPRESSION
   Gzip magic bytes: 0x1f 0x8b
   Uses libdeflate_gzip_decompress to inflate in memory.
   ================================================================ */

static int is_gzip(const unsigned char *buf, size_t len)
{
    return (len >= 2 && buf[0] == 0x1f && buf[1] == 0x8b);
}

/* decompress gzip data in memory.  Returns malloc'd buffer or NULL.
   The last 4 bytes of a gzip stream store the uncompressed size (mod 2^32).
   We use that as a hint but fall back to progressively larger buffers. */
static unsigned char *gunzip(const unsigned char *buf, size_t len, size_t *out_len)
{
    struct libdeflate_decompressor *d = libdeflate_alloc_decompressor();
    if(!d) return NULL;

    /* read the uncompressed size hint from the gzip trailer (last 4 bytes) */
    size_t alloc_size = 0;
    if(len >= 4)
        alloc_size = (size_t)buf[len-4] | ((size_t)buf[len-3] << 8) |
                     ((size_t)buf[len-2] << 16) | ((size_t)buf[len-1] << 24);

    /* the stored size is mod 2^32, so for files >4GB or if 0, use a heuristic */
    if(alloc_size == 0 || alloc_size > len * 100)
        alloc_size = len * 4;
    if(alloc_size < 4096)
        alloc_size = 4096;

    for(int tries = 0; tries < 4; tries++)
    {
        unsigned char *out = NULL;
        REMALLOC(out, alloc_size + 1);

        size_t actual_out = 0;
        enum libdeflate_result r = libdeflate_gzip_decompress(
            d, buf, len, out, alloc_size, &actual_out);

        if(r == LIBDEFLATE_SUCCESS)
        {
            libdeflate_free_decompressor(d);
            out[actual_out] = 0;
            *out_len = actual_out;
            return out;
        }

        free(out);
        if(r != LIBDEFLATE_INSUFFICIENT_SPACE)
            break;

        alloc_size *= 4;
    }

    libdeflate_free_decompressor(d);
    return NULL;
}

/* strip .gz from a filename to get the inner name for extension-based identification.
   Returns a pointer to a static buffer — not thread safe but fine for our use. */
static const char *strip_gz_ext(const char *filename)
{
    static char stripped[PATH_MAX];
    size_t flen = strlen(filename);
    if(flen >= 4 && !strcasecmp(filename + flen - 3, ".gz"))
    {
        size_t copy = flen - 3;
        if(copy >= sizeof(stripped)) copy = sizeof(stripped) - 1;
        memcpy(stripped, filename, copy);
        stripped[copy] = 0;
        return stripped;
    }
    return filename;
}

/* normalize_paragraphs:
   - collapse runs of whitespace (space/tab) within a line to single space
   - a single newline (within a paragraph) becomes a space
   - two or more consecutive newlines mark a paragraph boundary -> \n\n
   - trim leading/trailing whitespace from the result
*/
static void normalize_paragraphs(rp_string *out)
{
    if(!out || !out->str || out->len == 0)
        return;

    char *src = out->str;
    size_t slen = out->len;
    rp_string *tmp = rp_string_new(slen + 1);

    size_t i = 0;
    while(i < slen)
    {
        /* count consecutive newlines */
        if(src[i] == '\n')
        {
            int nlcount = 0;
            while(i < slen && (src[i] == '\n' || src[i] == '\r' || src[i] == ' ' || src[i] == '\t'))
            {
                if(src[i] == '\n')
                    nlcount++;
                i++;
            }
            if(i < slen)
            {
                if(nlcount >= 2)
                    rp_string_puts(tmp, "\n\n");
                else
                    rp_string_putc(tmp, ' ');
            }
            continue;
        }

        /* collapse horizontal whitespace runs */
        if(src[i] == ' ' || src[i] == '\t' || src[i] == '\r')
        {
            while(i < slen && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r'))
                i++;
            if(i < slen && src[i] != '\n')
                rp_string_putc(tmp, ' ');
            continue;
        }

        rp_string_putc(tmp, src[i]);
        i++;
    }

    /* swap content */
    free(out->str);
    out->str = tmp->str;
    out->len = tmp->len;
    out->cap = tmp->cap;
    tmp->str = NULL;
    free(tmp);

    rp_string_trim(out);
}


/* ================================================================
   HTML ENTITY DECODING
   ================================================================ */

typedef struct { const char *name; const char *value; } entity_t;

static const entity_t html_entities[] = {
    {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"},
    {"nbsp", " "}, {"ndash", "-"}, {"mdash", "--"}, {"lsquo", "'"},
    {"rsquo", "\xe2\x80\x99"}, {"ldquo", "\xe2\x80\x9c"}, {"rdquo", "\xe2\x80\x9d"},
    {"bull", "\xe2\x80\xa2"}, {"hellip", "..."}, {"copy", "(c)"},
    {"reg", "(R)"}, {"trade", "(TM)"}, {"laquo", "\xc2\xab"}, {"raquo", "\xc2\xbb"},
    {"cent", "\xc2\xa2"}, {"pound", "\xc2\xa3"}, {"yen", "\xc2\xa5"},
    {"euro", "\xe2\x82\xac"}, {"sect", "\xc2\xa7"}, {"para", "\xc2\xb6"},
    {"deg", "\xc2\xb0"}, {"plusmn", "\xc2\xb1"}, {"frac12", "\xc2\xbd"},
    {"frac14", "\xc2\xbc"}, {"frac34", "\xc2\xbe"}, {"times", "\xc3\x97"},
    {"divide", "\xc3\xb7"}, {"micro", "\xc2\xb5"},
    {NULL, NULL}
};

/* encode a unicode codepoint as utf-8 into buf, return bytes written */
static int utf8_encode(unsigned long cp, char *buf)
{
    if(cp < 0x80) {
        buf[0] = (char)cp;
        return 1;
    } else if(cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if(cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if(cp < 0x110000) {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/* decode &...; entity at src (pointing past the &), return bytes consumed including ;
   appends decoded text to out.  Returns 0 if not a valid entity. */
static size_t decode_entity(const char *src, size_t maxlen, rp_string *out)
{
    /* find the ; */
    size_t i = 0;
    while(i < maxlen && i < 10 && src[i] != ';' && src[i] != '\0')
        i++;
    if(i >= maxlen || src[i] != ';')
        return 0;

    size_t elen = i; /* length of entity name */

    /* numeric: &#123; or &#x1a; */
    if(src[0] == '#')
    {
        unsigned long cp = 0;
        if(src[1] == 'x' || src[1] == 'X')
            cp = strtoul(src + 2, NULL, 16);
        else
            cp = strtoul(src + 1, NULL, 10);

        if(cp > 0)
        {
            char u8[4];
            int n = utf8_encode(cp, u8);
            if(n > 0)
                rp_string_putsn(out, u8, n);
        }
        return elen + 1;
    }

    /* named entity */
    for(int e = 0; html_entities[e].name; e++)
    {
        if(strlen(html_entities[e].name) == elen && !memcmp(src, html_entities[e].name, elen))
        {
            rp_string_puts(out, html_entities[e].value);
            return elen + 1;
        }
    }

    return 0; /* unknown entity, leave as-is */
}

/* ================================================================
   JS-based converters (using rampart-html, rampart-cmark via duktape)

   Pattern: compile a JS function expression, push content arg, duk_pcall.
   ================================================================ */

/* helper: compile a JS function expression, call with one string arg,
   leave the result on top of the duktape stack. */
static void call_js_with_string(duk_context *ctx, const char *js_src,
                                const char *name, const char *arg, size_t arg_len)
{
    if(duk_pcompile_string(ctx, DUK_COMPILE_EVAL, js_src) != 0)
        RP_THROW(ctx, "convert %s: compile error: %s", name, duk_safe_to_string(ctx, -1));

    if(duk_pcall(ctx, 0) != 0)
        RP_THROW(ctx, "convert %s: eval error: %s", name, duk_safe_to_string(ctx, -1));

    duk_push_lstring(ctx, arg, arg_len);

    if(duk_pcall(ctx, 1) != 0)
        RP_THROW(ctx, "convert %s: %s", name, duk_safe_to_string(ctx, -1));
}

/* convenience: call with file content as a string */
static void call_js_converter(duk_context *ctx, const char *js_src,
                               const char *name,
                               const unsigned char *buf, size_t len)
{
    call_js_with_string(ctx, js_src, name, (const char *)buf, len);
}

/* convenience: call with file content as a binary buffer (for PDF, DOC, etc.) */
static void call_js_converter_buf(duk_context *ctx, const char *js_src,
                               const char *name,
                               const unsigned char *buf, size_t len)
{
    if(duk_pcompile_string(ctx, DUK_COMPILE_EVAL, js_src) != 0)
        RP_THROW(ctx, "convert %s: compile error: %s", name, duk_safe_to_string(ctx, -1));

    if(duk_pcall(ctx, 0) != 0)
        RP_THROW(ctx, "convert %s: eval error: %s", name, duk_safe_to_string(ctx, -1));

    void *b = duk_push_fixed_buffer(ctx, (duk_size_t)len);
    memcpy(b, buf, len);

    if(duk_pcall(ctx, 1) != 0)
        RP_THROW(ctx, "convert %s: %s", name, duk_safe_to_string(ctx, -1));
}

/* HTML -> text via rampart-html */
static const char html_convert_js[] =
    "(function(content) {"
    "  var html = require('rampart-html');"
    "  return html.newDocument(content).toText({concatenate:true});"
    "})";

/* Markdown -> HTML via rampart-cmark, then HTML -> text via rampart-html */
static const char md_convert_js[] =
    "(function(content) {"
    "  var cmark = require('rampart-cmark');"
    "  var html = require('rampart-html');"
    "  var h = cmark.toHtml(content);"
    "  return html.newDocument(h).toText({concatenate:true});"
    "})";

/* PDF -> text via pdftotext (takes filename as argument) */
static const char pdf_convert_file_js[] =
    "(function(filename) {"
    "  var exec = rampart.utils.exec;"
    "  var pdftotext = exec('which', 'pdftotext').stdout.trim();"
    "  if(!pdftotext)"
    "    throw new Error('convert pdf: pdftotext not found (install poppler-utils or xpdf)');"
    "  var res = exec(pdftotext, '-enc', 'UTF-8', filename, '-');"
    "  if(res.exitStatus)"
    "    throw new Error('convert pdf: pdftotext failed: ' + res.stderr);"
    "  return res.stdout;"
    "})";

/* PDF -> text via pdftotext from stdin (takes content as argument).
   Falls back to a temp file if pdftotext doesn't support stdin ('-'). */
static const char pdf_convert_buf_js[] =
    "(function(content) {"
    "  var exec = rampart.utils.exec;"
    "  var pdftotext = exec('which', 'pdftotext').stdout.trim();"
    "  if(!pdftotext)"
    "    throw new Error('convert pdf: pdftotext not found (install poppler-utils or xpdf)');"
    "  var res = exec(pdftotext, '-enc', 'UTF-8', '-', '-', {stdin:content});"
    "  if(!res.exitStatus)"
    "    return res.stdout;"
    "  var tmpf = (process.env.TMPDIR||'/tmp') + '/_rp_pdf_' + process.getpid() + '.pdf';"
    "  var fh = rampart.utils.fopen(tmpf,'w+');"
    "  rampart.utils.fwrite(fh, content);"
    "  rampart.utils.fclose(fh);"
    "  try {"
    "    res = exec(pdftotext, '-enc', 'UTF-8', tmpf, '-');"
    "  } finally {"
    "    try{rampart.utils.rmFile(tmpf);}catch(e){}"
    "  }"
    "  if(res.exitStatus)"
    "    throw new Error('convert pdf: pdftotext failed: ' + res.stderr);"
    "  return res.stdout;"
    "})";

/* DOC -> text via catdoc/textutil (takes filename as argument) */
static const char doc_convert_file_js[] =
    "(function(filename) {"
    "  var exec = rampart.utils.exec;"
    "  var catdoc = exec('which', 'catdoc').stdout.trim();"
    "  if(catdoc) {"
    "    var res = exec(catdoc, filename);"
    "    if(res.exitStatus)"
    "      throw new Error('convert doc: catdoc failed: ' + res.stderr);"
    "    return res.stdout;"
    "  }"
    "  var textutil = exec('which', 'textutil').stdout.trim();"
    "  if(textutil) {"
    "    var res = exec(textutil, '-convert', 'txt', '-stdout', filename);"
    "    if(res.exitStatus)"
    "      throw new Error('convert doc: textutil failed: ' + res.stderr);"
    "    return res.stdout;"
    "  }"
    "  throw new Error('convert doc: neither catdoc nor textutil found');"
    "})";

/* DOC -> text via catdoc/textutil from stdin (takes content as argument) */
static const char doc_convert_buf_js[] =
    "(function(content) {"
    "  var exec = rampart.utils.exec;"
    "  var catdoc = exec('which', 'catdoc').stdout.trim();"
    "  if(catdoc) {"
    "    var res = exec(catdoc, {stdin:content});"
    "    if(res.exitStatus)"
    "      throw new Error('convert doc: catdoc failed: ' + res.stderr);"
    "    return res.stdout;"
    "  }"
    "  var textutil = exec('which', 'textutil').stdout.trim();"
    "  if(textutil) {"
    "    var res = exec(textutil, '-convert', 'txt', '-stdout', '-stdin', {stdin:content});"
    "    if(res.exitStatus)"
    "      throw new Error('convert doc: textutil failed: ' + res.stderr);"
    "    return res.stdout;"
    "  }"
    "  throw new Error('convert doc: neither catdoc nor textutil found');"
    "})";

/* ================================================================
   CONVERTER: XML (Docbook and generic XML)
   - same approach as HTML: strip tags, decode entities
   ================================================================ */

static rp_string *convert_xml(const unsigned char *buf, size_t len)
{
    /* XML block-level elements (docbook + generic) */
    rp_string *out = rp_string_new(len);
    const char *s = (const char *)buf;
    size_t i = 0;

    const char *xml_blocks[] = {
        /* docbook */
        "para", "simpara", "formalpara", "title", "subtitle",
        "chapter", "section", "sect1", "sect2", "sect3",
        "article", "book", "part", "preface", "appendix",
        "listitem", "itemizedlist", "orderedlist", "variablelist",
        "varlistentry", "term",
        "blockquote", "programlisting", "screen", "literallayout",
        "note", "tip", "warning", "caution", "important",
        "table", "row", "entry", "thead", "tbody",
        "figure", "informalfigure",
        /* OOXML (docx/pptx/xlsx) */
        "w:p", "w:br", "w:tbl", "w:tr", "w:tc",
        "a:p",   /* DrawingML paragraph (pptx slides) */
        "si",    /* shared string item (xlsx) */
        /* ODF (odt/odp/ods) */
        "text:p", "text:h", "text:list-item",
        "text:list", "text:section",
        "table:table", "table:table-row", "table:table-cell",
        /* HTML (for epub xhtml passthrough) */
        "p", "div", "br", "hr", "h1", "h2", "h3", "h4", "h5", "h6",
        "li", "ul", "ol", "blockquote", "pre", "tr", "td", "th",
        NULL
    };



    while(i < len)
    {
        if(s[i] == '<')
        {
            size_t tag_start = i + 1;
            int closing = 0;

            if(tag_start < len && s[tag_start] == '/')
            {
                closing = 1;
                tag_start++;
            }
            (void)closing;

            size_t name_start = tag_start;
            while(tag_start < len && s[tag_start] != '>' && s[tag_start] != ' '
                  && s[tag_start] != '\t' && s[tag_start] != '\n'
                  && s[tag_start] != '/' && s[tag_start] != '\r')
                tag_start++;

            size_t name_len = tag_start - name_start;

            /* check for comment or CDATA */
            if(name_len >= 3 && !memcmp(s + name_start, "!--", 3))
            {
                const char *end = strstr(s + i, "-->");
                i = end ? (size_t)(end - s) + 3 : len;
                continue;
            }

            /* check if block-level */
            int handled = 0;
            for(int b = 0; xml_blocks[b]; b++)
            {
                if(strlen(xml_blocks[b]) == name_len &&
                   !strncasecmp(s + name_start, xml_blocks[b], name_len))
                {
                    rp_string_puts(out, "\n\n");
                    handled = 1;
                    break;
                }
            }

            /* for any non-block tag, ensure words don't concatenate:
               emit a space if the last output char isn't already whitespace */
            if(!handled && out->len > 0)
            {
                char last = out->str[out->len - 1];
                if(last != ' ' && last != '\n' && last != '\t')
                    rp_string_putc(out, ' ');
            }

            /* skip to end of tag */
            while(i < len && s[i] != '>')
                i++;
            if(i < len) i++;
            continue;
        }

        if(s[i] == '&')
        {
            size_t consumed = decode_entity(s + i + 1, len - i - 1, out);
            if(consumed > 0)
            {
                i += 1 + consumed;
                continue;
            }
        }

        rp_string_putc(out, s[i]);
        i++;
    }

    normalize_paragraphs(out);
    return out;
}

/* ================================================================
   CONVERTER: RTF
   ================================================================ */

static rp_string *convert_rtf(const unsigned char *buf, size_t len)
{
    rp_string *out = rp_string_new(len);
    const char *s = (const char *)buf;
    size_t i = 0;
    int depth = 0;
    int skip_group = 0;     /* depth at which we started skipping */
    int skip_depth = 0;

    /* groups to skip entirely: fonttbl, colortbl, stylesheet, info, pict, etc */
    const char *skip_groups[] = {
        "\\fonttbl", "\\colortbl", "\\stylesheet", "\\info",
        "\\pict", "\\*\\pn", "\\header", "\\footer",
        "\\headerl", "\\headerr", "\\footerl", "\\footerr",
        NULL
    };

    while(i < len)
    {
        if(s[i] == '{')
        {
            depth++;

            /* check if this group should be skipped */
            if(!skip_group)
            {
                for(int g = 0; skip_groups[g]; g++)
                {
                    size_t gl = strlen(skip_groups[g]);
                    if(i + 1 + gl <= len && !memcmp(s + i + 1, skip_groups[g], gl))
                    {
                        skip_group = 1;
                        skip_depth = depth;
                        break;
                    }
                }
            }
            i++;
            continue;
        }

        if(s[i] == '}')
        {
            if(skip_group && depth == skip_depth)
                skip_group = 0;
            depth--;
            i++;
            continue;
        }

        if(skip_group)
        {
            i++;
            continue;
        }

        /* control word */
        if(s[i] == '\\')
        {
            i++;
            if(i >= len) break;

            /* escaped literal characters */
            if(s[i] == '\\' || s[i] == '{' || s[i] == '}')
            {
                rp_string_putc(out, s[i]);
                i++;
                continue;
            }

            /* \' hex escape */
            if(s[i] == '\'')
            {
                if(i + 2 < len)
                {
                    char hex[3] = { s[i+1], s[i+2], 0 };
                    unsigned int ch = (unsigned int)strtoul(hex, NULL, 16);
                    if(ch > 0)
                    {
                        /* assume windows-1252, convert common chars to utf-8 */
                        if(ch < 0x80)
                            rp_string_putc(out, (char)ch);
                        else
                        {
                            /* simple latin1/win1252 to utf-8 */
                            char u8[4];
                            int n = utf8_encode(ch, u8);
                            if(n > 0)
                                rp_string_putsn(out, u8, n);
                        }
                    }
                    i += 3;
                }
                else
                    i++;
                continue;
            }

            /* \uN unicode escape */
            if(s[i] == 'u' && i + 1 < len && (isdigit(s[i+1]) || s[i+1] == '-'))
            {
                i++;
                long cp = strtol(s + i, NULL, 10);
                /* skip digits and optional minus */
                if(s[i] == '-') i++;
                while(i < len && isdigit(s[i])) i++;
                /* skip the replacement character that follows */
                if(i < len && s[i] == '?') i++;

                if(cp < 0) cp += 65536; /* RTF uses signed 16-bit */
                if(cp > 0)
                {
                    char u8[4];
                    int n = utf8_encode((unsigned long)cp, u8);
                    if(n > 0)
                        rp_string_putsn(out, u8, n);
                }
                continue;
            }

            /* read control word name */
            size_t cw_start = i;
            while(i < len && isalpha(s[i])) i++;
            size_t cw_len = i - cw_start;

            /* skip optional numeric parameter */
            if(i < len && (s[i] == '-' || isdigit(s[i])))
            {
                if(s[i] == '-') i++;
                while(i < len && isdigit(s[i])) i++;
            }

            /* skip single trailing space delimiter */
            if(i < len && s[i] == ' ') i++;

            /* handle known control words */
            if(cw_len == 3 && !memcmp(s + cw_start, "par", 3))
                rp_string_puts(out, "\n\n");
            else if(cw_len == 4 && !memcmp(s + cw_start, "line", 4))
                rp_string_putc(out, '\n');
            else if(cw_len == 3 && !memcmp(s + cw_start, "tab", 3))
                rp_string_putc(out, '\t');
            else if(cw_len == 6 && !memcmp(s + cw_start, "emdash", 6))
                rp_string_puts(out, "--");
            else if(cw_len == 6 && !memcmp(s + cw_start, "endash", 6))
                rp_string_puts(out, "-");
            else if(cw_len == 6 && !memcmp(s + cw_start, "bullet", 6))
                rp_string_puts(out, "\xe2\x80\xa2");
            else if(cw_len == 5 && !memcmp(s + cw_start, "lquot", 5))
                rp_string_puts(out, "\xe2\x80\x9c");
            else if(cw_len == 5 && !memcmp(s + cw_start, "rquot", 5))
                rp_string_puts(out, "\xe2\x80\x9d");
            else if(cw_len == 12 && !memcmp(s + cw_start, "sect", 4))
                rp_string_puts(out, "\n\n");

            continue;
        }

        /* newlines and carriage returns in RTF are just whitespace */
        if(s[i] == '\n' || s[i] == '\r')
        {
            i++;
            continue;
        }

        rp_string_putc(out, s[i]);
        i++;
    }

    normalize_paragraphs(out);
    return out;
}

/* ================================================================
   CONVERTER: MAN (troff/groff)
   ================================================================ */

/* skip to end of line, return position after newline */
static size_t skip_to_eol(const char *s, size_t len, size_t i)
{
    while(i < len && s[i] != '\n') i++;
    if(i < len) i++; /* skip newline */
    return i;
}

static rp_string *convert_man(const unsigned char *buf, size_t len)
{
    rp_string *out = rp_string_new(len);
    const char *s = (const char *)buf;
    size_t i = 0;
    int in_table = 0;

    while(i < len)
    {
        int bol = (i == 0 || (i > 0 && s[i-1] == '\n'));

        /* comments: .\" or '\" at start of line */
        if(bol && i + 1 < len &&
           ((s[i] == '.' && s[i+1] == '\\') || (s[i] == '\'' && s[i+1] == '\\')))
        {
            i = skip_to_eol(s, len, i);
            continue;
        }

        /* macro lines starting with . */
        if(bol && s[i] == '.')
        {
            i++; /* skip the dot */

            /* read macro name */
            size_t macro_start = i;
            while(i < len && s[i] != ' ' && s[i] != '\n' && s[i] != '\t' && s[i] != '"')
                i++;
            size_t macro_len = i - macro_start;

            /* skip whitespace after macro */
            while(i < len && (s[i] == ' ' || s[i] == '\t')) i++;

            /* macros that introduce a paragraph break */
            if((macro_len == 2 && (!memcmp(s + macro_start, "SH", 2) ||
                                   !memcmp(s + macro_start, "SS", 2) ||
                                   !memcmp(s + macro_start, "TH", 2) ||
                                   !memcmp(s + macro_start, "PP", 2) ||
                                   !memcmp(s + macro_start, "LP", 2) ||
                                   !memcmp(s + macro_start, "IP", 2) ||
                                   !memcmp(s + macro_start, "TP", 2) ||
                                   !memcmp(s + macro_start, "RS", 2) ||
                                   !memcmp(s + macro_start, "RE", 2) ||
                                   !memcmp(s + macro_start, "TS", 2) ||
                                   !memcmp(s + macro_start, "TE", 2)))
               || (macro_len == 2 && !memcmp(s + macro_start, "br", 2))
               || (macro_len == 2 && !memcmp(s + macro_start, "sp", 2)))
            {
                if(macro_len == 2 && !memcmp(s + macro_start, "TS", 2))
                    in_table = 1;
                if(macro_len == 2 && !memcmp(s + macro_start, "TE", 2))
                    in_table = 0;

                rp_string_puts(out, "\n\n");

                /* for SH/SS/TH, the rest of the line is the heading text */
                if(macro_len == 2 && (!memcmp(s + macro_start, "SH", 2) ||
                                      !memcmp(s + macro_start, "SS", 2) ||
                                      !memcmp(s + macro_start, "TH", 2)))
                {
                    /* strip quotes from heading */
                    if(i < len && s[i] == '"') i++;
                    while(i < len && s[i] != '\n')
                    {
                        if(s[i] == '"')
                        {
                            i++;
                            continue;
                        }
                        rp_string_putc(out, s[i]);
                        i++;
                    }
                    rp_string_puts(out, "\n\n");
                }
                else
                {
                    i = skip_to_eol(s, len, i);
                }
                continue;
            }

            /* table format lines: skip them */
            if(in_table && i < len && (s[i] == 'l' || s[i] == 'r' || s[i] == 'c'
                || s[i] == 'n' || s[i] == 's' || s[i] == 't'))
            {
                /* could be a tbl format spec, skip to ; or . */
                i = skip_to_eol(s, len, i);
                continue;
            }

            /* .pc, .nr, .ds, .de, .if, .ie, .el, etc - skip the line */
            i = skip_to_eol(s, len, i);
            continue;
        }

        /* inline font escapes: \fB, \fI, \fP, \fR etc */
        if(s[i] == '\\' && i + 1 < len)
        {
            char next = s[i+1];

            if(next == 'f' && i + 2 < len)
            {
                /* \fX - skip the font change */
                i += 3;
                /* \f(XX - two-char font name */
                if(i - 1 < len && s[i-1] == '(')
                    i += 2;
                continue;
            }

            /* \- is a literal hyphen */
            if(next == '-')
            {
                rp_string_putc(out, '-');
                i += 2;
                continue;
            }

            /* \& is a zero-width space (ignore) */
            if(next == '&')
            {
                i += 2;
                continue;
            }

            /* \| is a thin space (ignore) */
            if(next == '|')
            {
                i += 2;
                continue;
            }

            /* \e is a literal backslash */
            if(next == 'e')
            {
                rp_string_putc(out, '\\');
                i += 2;
                continue;
            }

            /* \(XX - special character (2 char) */
            if(next == '(' && i + 3 < len)
            {
                /* just skip most special chars for now */
                i += 4;
                continue;
            }

            /* \" - rest of line is comment */
            if(next == '"')
            {
                i = skip_to_eol(s, len, i);
                continue;
            }

            /* \n - interpolate number register, skip */
            if(next == 'n')
            {
                i += 2;
                if(i < len && s[i] == '(') i += 3;
                else if(i < len && s[i] == '[')
                {
                    while(i < len && s[i] != ']') i++;
                    if(i < len) i++;
                }
                else
                    i++;
                continue;
            }

            /* \* - string interpolation, skip */
            if(next == '*')
            {
                i += 2;
                if(i < len && s[i] == '(') i += 3;
                else if(i < len && s[i] == '[')
                {
                    while(i < len && s[i] != ']') i++;
                    if(i < len) i++;
                }
                else
                    i++;
                continue;
            }

            /* other \ escapes: skip the backslash and next char */
            i += 2;
            continue;
        }

        /* in table: @ is the column separator */
        if(in_table && s[i] == '@')
        {
            rp_string_putc(out, '\t');
            i++;
            continue;
        }

        /* T{ and T} are tbl macros for text blocks */
        if(in_table && s[i] == 'T' && i + 1 < len && (s[i+1] == '{' || s[i+1] == '}'))
        {
            i += 2;
            continue;
        }

        rp_string_putc(out, s[i]);
        i++;
    }

    normalize_paragraphs(out);
    return out;
}

/* ================================================================
   PREPROCESSOR: strip pandoc extensions from markdown before cmark
   Removes ::: fenced div lines and {.class #id ...} attribute spans.
   ================================================================ */

static rp_string *preprocess_markdown(const unsigned char *buf, size_t len)
{
    rp_string *out = rp_string_new(len + 1);
    const char *s = (const char *)buf;
    size_t i = 0;

    while(i < len)
    {
        int bol = (i == 0 || (i > 0 && s[i-1] == '\n'));

        /* ::: fenced div lines - skip entire line */
        if(bol && i + 2 < len && s[i] == ':' && s[i+1] == ':' && s[i+2] == ':')
        {
            while(i < len && s[i] != '\n') i++;
            if(i < len) i++;
            rp_string_putc(out, '\n');
            continue;
        }

        /* pandoc attribute spans: {.class}, {#id}, {style="..."}, {role="..."}, etc.
           May span multiple lines. Match { ... } where content looks like attributes
           (contains . # or = and no nested {). */
        if(s[i] == '{')
        {
            size_t j = i + 1;
            int has_attr_char = 0;
            while(j < len && s[j] != '}' && s[j] != '{')
            {
                if(s[j] == '.' || s[j] == '#' || s[j] == '=')
                    has_attr_char = 1;
                j++;
            }
            if(j < len && s[j] == '}' && has_attr_char)
            {
                i = j + 1;
                continue;
            }
        }

        /* escaped characters: pass through as-is (including \' \* etc) */
        rp_string_putc(out, s[i]);
        i++;
    }

    return out;
}

/* ================================================================
   CONVERTER: LATEX
   ================================================================ */

/* skip a balanced {...} group and return position after the closing }.
   Does NOT emit any text. */
static size_t skip_braced(const char *s, size_t len, size_t i)
{
    if(i >= len || s[i] != '{') return i;
    int depth = 1;
    i++;
    while(i < len && depth > 0)
    {
        if(s[i] == '{') depth++;
        else if(s[i] == '}') depth--;
        i++;
    }
    return i;
}

/* extract text from a balanced {...} group into out */
static size_t emit_braced(const char *s, size_t len, size_t i, rp_string *out);

/* forward declare convert_latex_range for recursive use */
static void convert_latex_range(const char *s, size_t start, size_t end, rp_string *out);

static size_t emit_braced(const char *s, size_t len, size_t i, rp_string *out)
{
    if(i >= len || s[i] != '{') return i;
    int depth = 1;
    size_t start = i + 1;
    i++;
    while(i < len && depth > 0)
    {
        if(s[i] == '{') depth++;
        else if(s[i] == '}') depth--;
        if(depth > 0) i++;
        else break;
    }
    /* recursively convert the content between braces */
    convert_latex_range(s, start, i, out);
    if(i < len) i++; /* skip closing } */
    return i;
}

static void convert_latex_range(const char *s, size_t start, size_t end, rp_string *out)
{
    size_t i = start;

    while(i < end)
    {
        /* comments: % to end of line */
        if(s[i] == '%')
        {
            while(i < end && s[i] != '\n') i++;
            if(i < end) i++;
            continue;
        }

        /* commands */
        if(s[i] == '\\')
        {
            i++;
            if(i >= end) break;

            /* escaped special chars */
            if(s[i] == '\\') { rp_string_puts(out, "\n\n"); i++; continue; }
            if(s[i] == '%')  { rp_string_putc(out, '%'); i++; continue; }
            if(s[i] == '&')  { rp_string_putc(out, '&'); i++; continue; }
            if(s[i] == '#')  { rp_string_putc(out, '#'); i++; continue; }
            if(s[i] == '$')  { rp_string_putc(out, '$'); i++; continue; }
            if(s[i] == '_')  { rp_string_putc(out, '_'); i++; continue; }
            if(s[i] == '{')  { rp_string_putc(out, '{'); i++; continue; }
            if(s[i] == '}')  { rp_string_putc(out, '}'); i++; continue; }
            if(s[i] == '~')  { rp_string_putc(out, '~'); i++; continue; }
            if(s[i] == ' ')  { rp_string_putc(out, ' '); i++; continue; }

            /* read command name */
            size_t cmd_start = i;
            while(i < end && isalpha(s[i])) i++;
            size_t cmd_len = i - cmd_start;

            if(cmd_len == 0)
            {
                /* not a letter command, skip char */
                i++;
                continue;
            }

            /* skip optional whitespace after command */
            while(i < end && (s[i] == ' ' || s[i] == '\t')) i++;

            /* commands that produce paragraph breaks */
            if((cmd_len == 7 && !memcmp(s + cmd_start, "section", 7)) ||
               (cmd_len == 10 && !memcmp(s + cmd_start, "subsection", 10)) ||
               (cmd_len == 13 && !memcmp(s + cmd_start, "subsubsection", 13)) ||
               (cmd_len == 7 && !memcmp(s + cmd_start, "chapter", 7)) ||
               (cmd_len == 4 && !memcmp(s + cmd_start, "part", 4)) ||
               (cmd_len == 9 && !memcmp(s + cmd_start, "paragraph", 9)))
            {
                rp_string_puts(out, "\n\n");
                /* skip optional * */
                if(i < end && s[i] == '*') i++;
                /* skip optional [...] */
                if(i < end && s[i] == '[')
                {
                    while(i < end && s[i] != ']') i++;
                    if(i < end) i++;
                }
                /* emit the {title} */
                if(i < end && s[i] == '{')
                    i = emit_braced(s, end, i, out);
                rp_string_puts(out, "\n\n");
                continue;
            }

            /* commands where we emit the braced argument as text */
            if((cmd_len == 6 && !memcmp(s + cmd_start, "textbf", 6)) ||
               (cmd_len == 6 && !memcmp(s + cmd_start, "textit", 6)) ||
               (cmd_len == 6 && !memcmp(s + cmd_start, "texttt", 6)) ||
               (cmd_len == 6 && !memcmp(s + cmd_start, "textsc", 6)) ||
               (cmd_len == 6 && !memcmp(s + cmd_start, "textsf", 6)) ||
               (cmd_len == 6 && !memcmp(s + cmd_start, "textrm", 6)) ||
               (cmd_len == 4 && !memcmp(s + cmd_start, "emph", 4)) ||
               (cmd_len == 11 && !memcmp(s + cmd_start, "underline", 9)) ||
               (cmd_len == 5 && !memcmp(s + cmd_start, "title", 5)))
            {
                if(i < end && s[i] == '{')
                    i = emit_braced(s, end, i, out);
                continue;
            }

            /* \href{url}{text} -> emit text */
            if(cmd_len == 4 && !memcmp(s + cmd_start, "href", 4))
            {
                if(i < end && s[i] == '{')
                    i = skip_braced(s, end, i); /* skip url */
                if(i < end && s[i] == '{')
                    i = emit_braced(s, end, i, out); /* emit text */
                continue;
            }

            /* \textquotesingle -> ' */
            if(cmd_len == 17 && !memcmp(s + cmd_start, "textquotesingle", 15))
            {
                rp_string_putc(out, '\'');
                continue;
            }

            /* \item -> paragraph break */
            if(cmd_len == 4 && !memcmp(s + cmd_start, "item", 4))
            {
                rp_string_puts(out, "\n\n");
                /* skip optional [...] */
                if(i < end && s[i] == '[')
                {
                    while(i < end && s[i] != ']') i++;
                    if(i < end) i++;
                }
                continue;
            }

            /* environments: \begin{env} and \end{env} */
            if(cmd_len == 5 && !memcmp(s + cmd_start, "begin", 5))
            {
                /* read environment name */
                if(i < end && s[i] == '{')
                {
                    size_t env_start = i + 1;
                    size_t env_end = env_start;
                    while(env_end < end && s[env_end] != '}') env_end++;
                    size_t elen = env_end - env_start;

                    i = (env_end < end) ? env_end + 1 : env_end;

                    /* skip environments that don't contain useful text */
                    if((elen == 6 && !memcmp(s + env_start, "figure", 6)) ||
                       (elen == 8 && !memcmp(s + env_start, "tikzpicture", 8)))
                    {
                        /* skip to matching \end{env} */
                        char endbuf[64];
                        int n = snprintf(endbuf, sizeof(endbuf), "\\end{%.*s}", (int)elen, s + env_start);
                        if(n > 0 && (size_t)n < sizeof(endbuf))
                        {
                            const char *p = strstr(s + i, endbuf);
                            if(p) i = (size_t)(p - s) + (size_t)n;
                        }
                        continue;
                    }

                    /* itemize/enumerate/description -> paragraph break */
                    rp_string_puts(out, "\n\n");
                }
                continue;
            }

            if(cmd_len == 3 && !memcmp(s + cmd_start, "end", 3))
            {
                rp_string_puts(out, "\n\n");
                if(i < end && s[i] == '{')
                    i = skip_braced(s, end, i);
                continue;
            }

            /* commands to skip entirely (with their braced args) */
            if((cmd_len == 5 && !memcmp(s + cmd_start, "label", 5)) ||
               (cmd_len == 17 && !memcmp(s + cmd_start, "phantomsection", 14)) ||
               (cmd_len == 11 && !memcmp(s + cmd_start, "hypertarget", 11)) ||
               (cmd_len == 17 && !memcmp(s + cmd_start, "includegraphics", 15)) ||
               (cmd_len == 14 && !memcmp(s + cmd_start, "pandocbounded", 13)) ||
               (cmd_len == 9 && !memcmp(s + cmd_start, "tightlist", 9)) ||
               (cmd_len == 10 && !memcmp(s + cmd_start, "setlength", 9)) ||
               (cmd_len == 9 && !memcmp(s + cmd_start, "pagestyle", 9)) ||
               (cmd_len == 14 && !memcmp(s + cmd_start, "bibliographystyle", 14)))
            {
                /* skip optional [...] then {...} args */
                while(i < end && s[i] == '[')
                {
                    while(i < end && s[i] != ']') i++;
                    if(i < end) i++;
                }
                while(i < end && s[i] == '{')
                    i = skip_braced(s, end, i);
                continue;
            }

            /* unknown command: skip any braced args, they might contain text
               but we don't know the semantics, so skip them */
            /* Actually, let's try to be permissive and emit braced content */
            while(i < end && s[i] == '{')
                i = emit_braced(s, end, i, out);

            continue;
        }

        /* bare braces (grouping) - emit content */
        if(s[i] == '{')
        {
            i = emit_braced(s, end, i, out);
            continue;
        }
        if(s[i] == '}')
        {
            i++;
            continue;
        }

        /* ~ is a non-breaking space */
        if(s[i] == '~')
        {
            rp_string_putc(out, ' ');
            i++;
            continue;
        }

        rp_string_putc(out, s[i]);
        i++;
    }
}

static rp_string *convert_latex(const unsigned char *buf, size_t len)
{
    rp_string *out = rp_string_new(len);
    convert_latex_range((const char *)buf, 0, len, out);
    normalize_paragraphs(out);
    return out;
}

/* ================================================================
   CONVERTER: TEXT (passthrough with normalization)
   ================================================================ */

/* prose text — normalize paragraphs (undo hard line wrapping) */
static rp_string *convert_text(const unsigned char *buf, size_t len)
{
    rp_string *out = rp_string_new(len + 1);
    rp_string_putsn(out, (const char *)buf, len);
    normalize_paragraphs(out);
    return out;
}

/* plaintext passthrough — return content as-is for code, config, etc. */
static rp_string *convert_plaintext(const unsigned char *buf, size_t len)
{
    rp_string *out = rp_string_new(len + 1);
    rp_string_putsn(out, (const char *)buf, len);
    return out;
}

/* ================================================================
   CONVERTER: UNKNOWN / BINARY — extract readable text chunks
   Scans for runs of valid ASCII/UTF-8 text, skips binary.
   Significant runs (>= min_chunk bytes) become paragraphs.
   ================================================================ */

#define EXTRACT_MIN_CHUNK 16  /* minimum text run length to keep */

/* return number of bytes in a valid utf-8 character starting at buf[0],
   or 0 if invalid */
static int valid_utf8_char(const unsigned char *buf, size_t remaining)
{
    unsigned char b = buf[0];

    /* ASCII printable + common whitespace */
    if(b >= 0x20 && b <= 0x7E) return 1;
    if(b == '\t' || b == '\n' || b == '\r') return 1;

    /* UTF-8 multibyte */
    if((b & 0xE0) == 0xC0 && remaining >= 2 &&
       (buf[1] & 0xC0) == 0x80)
        return 2;

    if((b & 0xF0) == 0xE0 && remaining >= 3 &&
       (buf[1] & 0xC0) == 0x80 && (buf[2] & 0xC0) == 0x80)
        return 3;

    if((b & 0xF8) == 0xF0 && remaining >= 4 &&
       (buf[1] & 0xC0) == 0x80 && (buf[2] & 0xC0) == 0x80 &&
       (buf[3] & 0xC0) == 0x80)
        return 4;

    return 0; /* binary / invalid */
}

static rp_string *extract_text_chunks(const unsigned char *buf, size_t len)
{
    rp_string *out = rp_string_new(len / 2 + 1);
    size_t i = 0;

    while(i < len)
    {
        /* find start of a text run */
        size_t run_start = i;
        while(i < len)
        {
            int charlen = valid_utf8_char(buf + i, len - i);
            if(charlen == 0)
                break;
            i += charlen;
        }

        size_t run_len = i - run_start;

        /* keep this chunk if it's significant */
        if(run_len >= EXTRACT_MIN_CHUNK)
        {
            if(out->len > 0)
                rp_string_puts(out, "\n\n");
            rp_string_putsn(out, (const char *)(buf + run_start), run_len);
        }

        /* skip binary bytes */
        while(i < len && valid_utf8_char(buf + i, len - i) == 0)
            i++;
    }

    rp_string_trim(out);
    return out;
}

/* ================================================================
   ZIP EXTRACTION (for DOCX, ODT, EPUB)
   Uses libdeflate for inflating compressed entries.
   ================================================================ */

/*
   ZIP reading using the central directory (handles data descriptor flag).

   End of central directory record (at end of file):
     offset 0:  4  signature (PK\x05\x06)
     offset 8:  2  number of entries
     offset 12: 4  size of central directory
     offset 16: 4  offset of central directory

   Central directory entry:
     offset 0:  4  signature (PK\x01\x02)
     offset 10: 2  compression method
     offset 20: 4  compressed size
     offset 24: 4  uncompressed size
     offset 28: 2  filename length
     offset 30: 2  extra field length
     offset 32: 2  comment length
     offset 42: 4  local header offset
     offset 46: N  filename
*/

#define ZIP_EOCD_SIG   0x06054b50
#define ZIP_CDIR_SIG   0x02014b50
#define ZIP_LOCAL_SIG  0x04034b50
#define ZIP_READ16(p)  ((unsigned)((p)[0]) | ((unsigned)((p)[1]) << 8))
#define ZIP_READ32(p)  ((unsigned)((p)[0]) | ((unsigned)((p)[1]) << 8) | \
                        ((unsigned)((p)[2]) << 16) | ((unsigned)((p)[3]) << 24))

/* find the end-of-central-directory record.  Returns offset or -1. */
static long zip_find_eocd(const unsigned char *zip, size_t zip_len)
{
    /* EOCD is at least 22 bytes, search backward from end */
    if(zip_len < 22) return -1;
    size_t search_start = (zip_len > 65557) ? zip_len - 65557 : 0;
    for(size_t i = zip_len - 22; i >= search_start; i--)
    {
        if(ZIP_READ32(zip + i) == ZIP_EOCD_SIG)
            return (long)i;
        if(i == 0) break;
    }
    return -1;
}

/* extract a named file from a ZIP archive in memory using the central directory.
   Returns malloc'd buffer with uncompressed data, or NULL. */
static unsigned char *zip_extract(const unsigned char *zip, size_t zip_len,
                                   const char *target_name, size_t *out_len)
{
    size_t target_len = strlen(target_name);

    long eocd_off = zip_find_eocd(zip, zip_len);
    if(eocd_off < 0) return NULL;

    unsigned cdir_size   = ZIP_READ32(zip + eocd_off + 12);
    unsigned cdir_offset = ZIP_READ32(zip + eocd_off + 16);

    if((size_t)cdir_offset + cdir_size > zip_len) return NULL;

    /* walk the central directory */
    size_t pos = cdir_offset;
    size_t cdir_end = cdir_offset + cdir_size;

    while(pos + 46 <= cdir_end)
    {
        if(ZIP_READ32(zip + pos) != ZIP_CDIR_SIG)
            break;

        unsigned method     = ZIP_READ16(zip + pos + 10);
        unsigned comp_size  = ZIP_READ32(zip + pos + 20);
        unsigned uncomp_sz  = ZIP_READ32(zip + pos + 24);
        unsigned fname_len  = ZIP_READ16(zip + pos + 28);
        unsigned extra_len  = ZIP_READ16(zip + pos + 30);
        unsigned comment_len= ZIP_READ16(zip + pos + 32);
        unsigned local_off  = ZIP_READ32(zip + pos + 42);

        const char *fname = (const char *)(zip + pos + 46);

        if(fname_len == target_len && !memcmp(fname, target_name, target_len))
        {
            /* found it - read from local header to get actual data offset */
            if(local_off + 30 > zip_len) return NULL;
            unsigned local_fname_len = ZIP_READ16(zip + local_off + 26);
            unsigned local_extra_len = ZIP_READ16(zip + local_off + 28);
            size_t data_offset = local_off + 30 + local_fname_len + local_extra_len;

            if(data_offset + comp_size > zip_len) return NULL;
            const unsigned char *comp_data = zip + data_offset;

            if(method == 0) /* stored */
            {
                unsigned char *out = NULL;
                REMALLOC(out, comp_size + 1);
                memcpy(out, comp_data, comp_size);
                out[comp_size] = 0;
                *out_len = comp_size;
                return out;
            }

            if(method == 8) /* deflate */
            {
                size_t alloc_size = uncomp_sz > 0 ? uncomp_sz : comp_size * 4;
                if(alloc_size < 4096) alloc_size = 4096;

                unsigned char *out = NULL;
                REMALLOC(out, alloc_size + 1);

                struct libdeflate_decompressor *d = libdeflate_alloc_decompressor();
                if(!d) { free(out); return NULL; }

                size_t actual_out = 0;
                enum libdeflate_result r = libdeflate_deflate_decompress(
                    d, comp_data, comp_size, out, alloc_size, &actual_out);

                libdeflate_free_decompressor(d);

                if(r != LIBDEFLATE_SUCCESS)
                {
                    free(out);
                    return NULL;
                }

                out[actual_out] = 0;
                *out_len = actual_out;
                return out;
            }

            return NULL; /* unsupported method */
        }

        pos += 46 + fname_len + extra_len + comment_len;
    }

    return NULL; /* not found */
}

/* iterate ZIP entries via central directory, calling a callback for matching filenames.
   Used by EPUB to collect all .xhtml/.html content files. */
typedef void (*zip_iter_cb)(const char *fname, size_t fname_len,
                            const unsigned char *zip, size_t zip_len, void *userdata);

static void zip_iterate(const unsigned char *zip, size_t zip_len, zip_iter_cb cb, void *ud)
{
    long eocd_off = zip_find_eocd(zip, zip_len);
    if(eocd_off < 0) return;

    unsigned cdir_size   = ZIP_READ32(zip + eocd_off + 12);
    unsigned cdir_offset = ZIP_READ32(zip + eocd_off + 16);
    if((size_t)cdir_offset + cdir_size > zip_len) return;

    size_t pos = cdir_offset;
    size_t cdir_end = cdir_offset + cdir_size;

    while(pos + 46 <= cdir_end)
    {
        if(ZIP_READ32(zip + pos) != ZIP_CDIR_SIG)
            break;

        unsigned fname_len   = ZIP_READ16(zip + pos + 28);
        unsigned extra_len   = ZIP_READ16(zip + pos + 30);
        unsigned comment_len = ZIP_READ16(zip + pos + 32);
        const char *fname    = (const char *)(zip + pos + 46);

        cb(fname, fname_len, zip, zip_len, ud);

        pos += 46 + fname_len + extra_len + comment_len;
    }
}

/* ================================================================
   CONVERTER: DOCX (extract word/document.xml, strip XML tags)
   ================================================================ */

/* find the main document path from _rels/.rels.
   Looks for Relationship with Type containing "officeDocument"
   and returns the Target value.  Returns NULL if not found. */
static const char *docx_find_document_path(const unsigned char *rels, size_t rels_len,
                                            char *pathbuf, size_t pathbuf_size)
{
    const char *s = (const char *)rels;
    const char *needle = "officeDocument";
    size_t needle_len = 14;

    /* find the Relationship element containing "officeDocument" in its Type */
    for(size_t i = 0; i + needle_len < rels_len; i++)
    {
        if(!memcmp(s + i, needle, needle_len))
        {
            /* found it — now find Target="..." nearby */
            /* search backward and forward within a reasonable range for Target= */
            size_t search_start = (i > 200) ? i - 200 : 0;
            size_t search_end = (i + 400 < rels_len) ? i + 400 : rels_len;

            const char *tgt = NULL;
            for(size_t j = search_start; j + 8 < search_end; j++)
            {
                if(!memcmp(s + j, "Target=", 7))
                {
                    tgt = s + j + 7;
                    break;
                }
            }
            if(!tgt) return NULL;

            /* skip the quote */
            char quote = *tgt;
            if(quote != '"' && quote != '\'') return NULL;
            tgt++;

            /* read until closing quote */
            const char *end = tgt;
            while(end < s + rels_len && *end != quote) end++;
            size_t plen = (size_t)(end - tgt);
            if(plen == 0 || plen >= pathbuf_size) return NULL;

            /* skip leading / if present */
            if(tgt[0] == '/')
            {
                tgt++;
                plen--;
            }

            memcpy(pathbuf, tgt, plen);
            pathbuf[plen] = 0;
            return pathbuf;
        }
    }
    return NULL;
}

static rp_string *convert_docx(const unsigned char *buf, size_t len)
{
    size_t xml_len = 0;
    unsigned char *xml = NULL;

    /* try to find the actual document path from _rels/.rels */
    size_t rels_len = 0;
    unsigned char *rels = zip_extract(buf, len, "_rels/.rels", &rels_len);
    if(rels)
    {
        char pathbuf[256];
        const char *doc_path = docx_find_document_path(rels, rels_len, pathbuf, sizeof(pathbuf));
        if(doc_path)
            xml = zip_extract(buf, len, doc_path, &xml_len);
        free(rels);
    }

    /* fallback to standard path */
    if(!xml)
        xml = zip_extract(buf, len, "word/document.xml", &xml_len);

    if(!xml)
        return NULL;

    rp_string *result = convert_xml(xml, xml_len);
    free(xml);
    return result;
}

/* ================================================================
   CONVERTER: ODT (extract content.xml, strip XML tags)
   ================================================================ */

static rp_string *convert_odt(const unsigned char *buf, size_t len)
{
    size_t xml_len = 0;
    unsigned char *xml = zip_extract(buf, len, "content.xml", &xml_len);
    if(!xml)
        return NULL;

    rp_string *result = convert_xml(xml, xml_len);
    free(xml);
    return result;
}

/* ================================================================
   CONVERTER: ODP/ODS (same as ODT — extract content.xml)
   ================================================================ */

/* ODP and ODS use the same content.xml structure as ODT */
#define convert_odp convert_odt
#define convert_ods convert_odt

/* ================================================================
   CONVERTER: PPTX (iterate ppt/slides/slide*.xml, extract text)
   ================================================================ */

static void pptx_slide_cb(const char *fname, size_t fname_len,
                           const unsigned char *zip, size_t zip_len, void *userdata)
{
    rp_string *all_text = (rp_string *)userdata;

    /* match ppt/slides/slide*.xml (not _rels or other subdirs) */
    if(fname_len < 16 || fname_len > 40) return;
    if(memcmp(fname, "ppt/slides/slide", 16) != 0) return;
    if(memcmp(fname + fname_len - 4, ".xml", 4) != 0) return;
    /* skip _rels files */
    if(memmem(fname, fname_len, "_rels", 5)) return;

    char name_buf[64];
    if(fname_len >= sizeof(name_buf)) return;
    memcpy(name_buf, fname, fname_len);
    name_buf[fname_len] = 0;

    size_t entry_len = 0;
    unsigned char *entry = zip_extract(zip, zip_len, name_buf, &entry_len);
    if(entry)
    {
        rp_string *slide_text = convert_xml(entry, entry_len);
        free(entry);
        if(slide_text && slide_text->len > 0)
        {
            if(all_text->len > 0)
                rp_string_puts(all_text, "\n\n");
            rp_string_putsn(all_text, slide_text->str, slide_text->len);
        }
        if(slide_text) rp_string_free(slide_text);
    }
}

static rp_string *convert_pptx(const unsigned char *buf, size_t len)
{
    rp_string *out = rp_string_new(len);
    zip_iterate(buf, len, pptx_slide_cb, out);
    rp_string_trim(out);
    return out;
}

/* ================================================================
   CONVERTER: XLSX (extract xl/sharedStrings.xml for string content)
   ================================================================ */

static rp_string *convert_xlsx(const unsigned char *buf, size_t len)
{
    rp_string *out = rp_string_new(len);

    /* primary text source: shared strings table */
    size_t ss_len = 0;
    unsigned char *ss = zip_extract(buf, len, "xl/sharedStrings.xml", &ss_len);
    if(ss)
    {
        rp_string *ss_text = convert_xml(ss, ss_len);
        free(ss);
        if(ss_text && ss_text->len > 0)
        {
            rp_string_putsn(out, ss_text->str, ss_text->len);
        }
        if(ss_text) rp_string_free(ss_text);
    }

    /* also check for inline strings in sheet XMLs — they contain <is><t>text</t></is> */
    /* The XML stripper will extract any text content from sheet files too */
    /* For now, sharedStrings covers most real-world XLSX files */

    rp_string_trim(out);
    return out;
}

/* ================================================================
   CONVERTER: EPUB (find and extract XHTML content files)
   EPUB is a ZIP with XHTML files. We look for the content in
   the OPF manifest, or fall back to extracting all .xhtml/.html files.
   For simplicity, we extract the OPF to find the spine order,
   then concatenate all content documents.
   ================================================================ */

/* callback for epub: collect .xhtml/.html files */
static void epub_collect_cb(const char *fname, size_t fname_len,
                            const unsigned char *zip, size_t zip_len, void *userdata)
{
    rp_string *all_html = (rp_string *)userdata;
    int is_content = 0;

    if(fname_len > 6 && !memcmp(fname + fname_len - 6, ".xhtml", 6))
        is_content = 1;
    else if(fname_len > 5 && !memcmp(fname + fname_len - 5, ".html", 5))
        is_content = 1;
    else if(fname_len > 4 && !memcmp(fname + fname_len - 4, ".htm", 4))
        is_content = 1;

    if(!is_content) return;

    char name_buf[512];
    if(fname_len >= sizeof(name_buf)) return;
    memcpy(name_buf, fname, fname_len);
    name_buf[fname_len] = 0;

    size_t entry_len = 0;
    unsigned char *entry = zip_extract(zip, zip_len, name_buf, &entry_len);
    if(entry)
    {
        rp_string_putsn(all_html, (const char *)entry, entry_len);
        rp_string_putc(all_html, '\n');
        free(entry);
    }
}

static void convert_epub(duk_context *ctx, const unsigned char *buf, size_t len)
{
    rp_string *all_html = rp_string_new(len);
    zip_iterate(buf, len, epub_collect_cb, all_html);

    if(all_html->len == 0)
    {
        rp_string_free(all_html);
        RP_THROW(ctx, "convert epub: no content files found");
    }

    call_js_converter(ctx, html_convert_js, "epub",
                      (const unsigned char *)all_html->str, all_html->len);
    rp_string_free(all_html);
}

/* ================================================================
   MAIN CONVERT DISPATCH
   ================================================================ */

/* MIME type strings for each file type */
static const char *filetype_mimes[] = {
    "application/octet-stream",   /* FT_UNKNOWN */
    "text/plain",                 /* FT_TEXT */
    "text/plain",                 /* FT_PLAINTEXT */
    "text/html",                  /* FT_HTML */
    "text/xml",                   /* FT_XML */
    "text/markdown",              /* FT_MARKDOWN */
    "application/x-latex",        /* FT_LATEX */
    "text/rtf",                   /* FT_RTF */
    "text/troff",                 /* FT_MAN */
    "application/pdf",            /* FT_PDF */
    "application/vnd.openxmlformats-officedocument.wordprocessingml.document", /* FT_DOCX */
    "application/vnd.oasis.opendocument.text",  /* FT_ODT */
    "application/epub+zip",       /* FT_EPUB */
    "application/msword",         /* FT_DOC */
    "application/vnd.openxmlformats-officedocument.presentationml.presentation", /* FT_PPTX */
    "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",         /* FT_XLSX */
    "application/vnd.oasis.opendocument.presentation",  /* FT_ODP */
    "application/vnd.oasis.opendocument.spreadsheet",   /* FT_ODS */
};

/* do_convert: pushes result string onto the duktape stack.
   If filename is non-NULL, PDF/DOC use file-based external tools.
   If filename is NULL (buffer mode), PDF/DOC use stdin-based tools. */
static void do_convert(const unsigned char *buf, size_t len,
                       filetype_t ft, duk_context *ctx,
                       const char *filename)
{
    rp_string *result = NULL;

    switch(ft)
    {
        case FT_HTML:
            call_js_converter(ctx, html_convert_js, "html", buf, len);
            return;

        case FT_MARKDOWN:
        {
            rp_string *cleaned = preprocess_markdown(buf, len);
            call_js_converter(ctx, md_convert_js, "markdown",
                              (const unsigned char *)cleaned->str, cleaned->len);
            rp_string_free(cleaned);
            return;
        }

        case FT_TEXT:      result = convert_text(buf, len);      break;
        case FT_PLAINTEXT: result = convert_plaintext(buf, len); break;
        case FT_XML:      result = convert_xml(buf, len);      break;
        case FT_LATEX:    result = convert_latex(buf, len);    break;
        case FT_RTF:      result = convert_rtf(buf, len);      break;
        case FT_MAN:      result = convert_man(buf, len);      break;

        case FT_DOCX:
            result = convert_docx(buf, len);
            if(!result) RP_THROW(ctx, "convert docx: could not extract word/document.xml");
            break;

        case FT_ODT:
            result = convert_odt(buf, len);
            if(!result) RP_THROW(ctx, "convert odt: could not extract content.xml");
            break;

        case FT_ODP:
            result = convert_odp(buf, len);
            if(!result) RP_THROW(ctx, "convert odp: could not extract content.xml");
            break;

        case FT_ODS:
            result = convert_ods(buf, len);
            if(!result) RP_THROW(ctx, "convert ods: could not extract content.xml");
            break;

        case FT_PPTX:
            result = convert_pptx(buf, len);
            break;

        case FT_XLSX:
            result = convert_xlsx(buf, len);
            break;

        case FT_EPUB:
            convert_epub(ctx, buf, len);
            return;

        case FT_PDF:
            if(filename)
                call_js_with_string(ctx, pdf_convert_file_js, "pdf", filename, strlen(filename));
            else
                call_js_converter_buf(ctx, pdf_convert_buf_js, "pdf", buf, len);
            return;

        case FT_DOC:
            if(filename)
                call_js_with_string(ctx, doc_convert_file_js, "doc", filename, strlen(filename));
            else
                call_js_converter_buf(ctx, doc_convert_buf_js, "doc", buf, len);
            return;

        case FT_UNKNOWN:
        default:
            result = extract_text_chunks(buf, len);
            break;
    }

    if(result)
    {
        duk_push_lstring(ctx, result->str, result->len);
        rp_string_free(result);
    }
    else
        duk_push_undefined(ctx);
}

/* check if arg at idx is a "details" request:
   true, or {details:true} */
static int want_details(duk_context *ctx, duk_idx_t idx)
{
    if(duk_is_boolean(ctx, idx))
        return duk_get_boolean(ctx, idx);

    if(duk_is_object(ctx, idx) && !duk_is_null(ctx, idx))
    {
        duk_get_prop_string(ctx, idx, "details");
        int ret = duk_to_boolean(ctx, -1);
        duk_pop(ctx);
        return ret;
    }

    return 0;
}

/* wrap the text result (on top of stack) in a details object if requested.
   Replaces the top-of-stack string with {text:"...", mimeType:"..."} */
static void push_details(duk_context *ctx, filetype_t ft)
{
    /* text string is on top of stack */
    duk_push_object(ctx);
    duk_pull(ctx, -2);  /* move text below the object, then set as property */
    duk_put_prop_string(ctx, -2, "text");
    duk_push_string(ctx, filetype_mimes[ft]);
    duk_put_prop_string(ctx, -2, "mimeType");
}

/* ================================================================
   JS INTERFACE
   ================================================================ */

/* identify(filename) or identify(buffer) */
static duk_ret_t rp_identify(duk_context *ctx)
{
    const char *data;
    size_t len = 0;
    unsigned char *freeme = NULL;
    const char *fname_hint = "";

    if(duk_is_string(ctx, 0))
    {
        /* treat as filename */
        const char *filename = duk_get_string(ctx, 0);
        freeme = read_file_contents(filename, &len);
        if(!freeme)
            RP_THROW(ctx, "identify: could not read file '%s'", filename);
        data = (const char *)freeme;
        fname_hint = filename;
    }
    else if(duk_is_buffer_data(ctx, 0))
    {
        data = (const char *)duk_get_buffer_data(ctx, 0, &len);
    }
    else
    {
        RP_THROW(ctx, "identify: argument 1 must be a string (filename) or buffer");
        return 0;
    }

    /* transparently decompress gzip */
    unsigned char *dec_buf = NULL;
    if(is_gzip((const unsigned char *)data, len))
    {
        size_t dec_len = 0;
        dec_buf = gunzip((const unsigned char *)data, len, &dec_len);
        if(freeme) free(freeme);
        if(!dec_buf)
            RP_THROW(ctx, "identify: could not decompress gzipped data");
        data = (const char *)dec_buf;
        len = dec_len;
        freeme = dec_buf;
        fname_hint = strip_gz_ext(fname_hint);
    }

    filetype_t ft = identify_content((const unsigned char *)data, len, fname_hint);
    if(freeme) free(freeme);

    duk_push_string(ctx, filetype_names[ft]);
    return 1;
}

/* convert(stringOrBuffer [, detailsFlag]) — convert in-memory content */
static duk_ret_t rp_convert(duk_context *ctx)
{
    duk_size_t sz = 0;
    const char *data = REQUIRE_STR_OR_BUF(ctx, 0, &sz,
        "convert: argument 1 must be a string or buffer");
    size_t len = (size_t)sz;
    int details = want_details(ctx, 1);

    /* copy data so we can potentially decompress */
    unsigned char *buf = NULL;
    REMALLOC(buf, len + 1);
    memcpy(buf, data, len);
    buf[len] = 0;

    /* transparently decompress gzip */
    if(is_gzip(buf, len))
    {
        size_t dec_len = 0;
        unsigned char *dec = gunzip(buf, len, &dec_len);
        free(buf);
        if(!dec)
            RP_THROW(ctx, "convert: could not decompress gzipped data");
        buf = dec;
        len = dec_len;
    }

    filetype_t ft = identify_content(buf, len, "");

    /* buffer mode: no filename, PDF/DOC use stdin */
    do_convert(buf, len, ft, ctx, NULL);
    free(buf);

    if(details)
        push_details(ctx, ft);

    return 1;
}

/* convertFile(filename [, detailsFlag]) — convert from a file path */
static duk_ret_t rp_convert_file(duk_context *ctx)
{
    const char *filename = REQUIRE_STRING(ctx, 0,
        "convertFile: argument 1 must be a string (filename)");
    int details = want_details(ctx, 1);

    size_t len = 0;
    unsigned char *buf = read_file_contents(filename, &len);
    if(!buf)
        RP_THROW(ctx, "convertFile: could not read file '%s'", filename);

    /* transparently decompress gzip */
    const char *effective_filename = filename;
    if(is_gzip(buf, len))
    {
        size_t dec_len = 0;
        unsigned char *dec = gunzip(buf, len, &dec_len);
        free(buf);
        if(!dec)
            RP_THROW(ctx, "convertFile: could not decompress gzipped file '%s'", filename);
        buf = dec;
        len = dec_len;
        effective_filename = strip_gz_ext(filename);
    }

    filetype_t ft = identify_content(buf, len, effective_filename);

    /* file mode: pass filename so PDF/DOC use file-based tools */
    do_convert(buf, len, ft, ctx, effective_filename);
    free(buf);

    if(details)
        push_details(ctx, ft);

    return 1;
}

/* **************************************************
   Initialize module
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_object(ctx);

    duk_push_c_function(ctx, rp_convert, 2);
    duk_put_prop_string(ctx, -2, "convert");

    duk_push_c_function(ctx, rp_convert_file, 2);
    duk_put_prop_string(ctx, -2, "convertFile");

    duk_push_c_function(ctx, rp_identify, 1);
    duk_put_prop_string(ctx, -2, "identify");

    return 1;
}
