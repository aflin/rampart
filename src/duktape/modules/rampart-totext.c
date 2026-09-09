#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#ifdef __linux__
#include <sys/vfs.h>      /* statfs: is the spill directory tmpfs (i.e. RAM)? */
#endif

#define RP_STRING_IMPLEMENTATION
#include "rampart.h"
#include "rp_zip.h"
#include "libdeflate.h"

/* the vendored RFC 5322 + MIME parser -- extern/libetpan, a subset */
#include <libetpan/mailmime.h>
#include <libetpan/mailimf.h>
#include <libetpan/mailmime_decode.h>
#include <libetpan/mailmime_content.h>

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
    /* images: text comes from a rampart-ocr reader */
    FT_PNG,
    FT_JPEG,
    FT_TIFF,
    FT_GIF,
    FT_BMP,
    FT_PNM,
    FT_PSD,
    FT_HDR,
    /* messages: RFC 5322 + MIME, and its containers */
    FT_EMAIL,
    FT_MBOX,
    FT_MHTML,
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
    "png",
    "jpeg",
    "tiff",
    "gif",
    "bmp",
    "pnm",
    "psd",
    "hdr",
    "email",
    "mbox",
    "mhtml",
};

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
    "image/png",                  /* FT_PNG */
    "image/jpeg",                 /* FT_JPEG */
    "image/tiff",                 /* FT_TIFF */
    "image/gif",                  /* FT_GIF */
    "image/bmp",                  /* FT_BMP */
    "image/x-portable-anymap",    /* FT_PNM */
    "image/vnd.adobe.photoshop",  /* FT_PSD */
    "image/vnd.radiance",         /* FT_HDR */
    "message/rfc822",             /* FT_EMAIL */
    "application/mbox",           /* FT_MBOX */
    "multipart/related",          /* FT_MHTML */
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
    if(!strcasecmp(dot, "png"))   return FT_PNG;
    if(!strcasecmp(dot, "jpg") || !strcasecmp(dot, "jpeg")) return FT_JPEG;
    if(!strcasecmp(dot, "tif") || !strcasecmp(dot, "tiff")) return FT_TIFF;
    if(!strcasecmp(dot, "gif"))   return FT_GIF;
    if(!strcasecmp(dot, "bmp"))   return FT_BMP;
    if(!strcasecmp(dot, "pnm") || !strcasecmp(dot, "ppm") ||
       !strcasecmp(dot, "pgm") || !strcasecmp(dot, "pbm")) return FT_PNM;
    if(!strcasecmp(dot, "psd"))   return FT_PSD;
    if(!strcasecmp(dot, "hdr"))   return FT_HDR;
    if(!strcasecmp(dot, "pptx"))  return FT_PPTX;
    if(!strcasecmp(dot, "xlsx"))  return FT_XLSX;
    if(!strcasecmp(dot, "odp"))   return FT_ODP;
    if(!strcasecmp(dot, "ods"))   return FT_ODS;
    if(!strcasecmp(dot, "eml") || !strcasecmp(dot, "emlx")) return FT_EMAIL;
    if(!strcasecmp(dot, "mbox") || !strcasecmp(dot, "mbx")) return FT_MBOX;
    if(!strcasecmp(dot, "mht") || !strcasecmp(dot, "mhtml")) return FT_MHTML;

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
    if(fname_len == 19 && !memcmp(fname, "[Content_Types].xml", 19))
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

            /* SECURITY (F10): the memcmps below read up to fl bytes of fn; the
               loop guard only bounds the fixed header, so bound fn here too. */
            if(pos + 30 + (size_t)fl > len) break;

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

    if(rem >= 14 && !strncasecmp((const char*)p, "<!doctype html", 14))
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

/* Image signatures.  All but BMP are unambiguous multi-byte magics; "BM" is
   two letters any text file could start with, so it is checked against the
   header's own size fields. */
static filetype_t identify_image(const unsigned char *buf, size_t len)
{
    if(len >= 8 && !memcmp(buf, "\x89PNG\r\n\x1a\n", 8)) return FT_PNG;
    if(len >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) return FT_JPEG;
    if(len >= 6 && (!memcmp(buf, "GIF87a", 6) || !memcmp(buf, "GIF89a", 6))) return FT_GIF;
    if(len >= 4 && ((buf[0] == 'I' && buf[1] == 'I' && (buf[2] == 42 || buf[2] == 43) && buf[3] == 0) ||
                    (buf[0] == 'M' && buf[1] == 'M' && buf[2] == 0 && (buf[3] == 42 || buf[3] == 43))))
        return FT_TIFF;
    if(len >= 4 && !memcmp(buf, "8BPS", 4)) return FT_PSD;
    if(len >= 10 && (!memcmp(buf, "#?RADIANCE", 10) || !memcmp(buf, "#?RGBE", 6))) return FT_HDR;
    if(len >= 4 && buf[0] == 'P' && buf[1] >= '1' && buf[1] <= '6' && isspace(buf[2]))
    {
        /* "P3 is a chip" is prose; a real header's next token is the width */
        size_t i = 2;
        while(i < len && (isspace(buf[i]) || buf[i] == '#'))
        {
            if(buf[i] == '#') while(i < len && buf[i] != '\n') i++;
            else i++;
        }
        if(i < len && isdigit(buf[i])) return FT_PNM;
    }
    if(len >= 30 && buf[0] == 'B' && buf[1] == 'M')
    {
        uint32_t fsize = buf[2] | (buf[3] << 8) | (buf[4] << 16) | ((uint32_t)buf[5] << 24);
        uint32_t off   = buf[10] | (buf[11] << 8) | (buf[12] << 16) | ((uint32_t)buf[13] << 24);
        uint32_t dib   = buf[14] | (buf[15] << 8) | (buf[16] << 16) | ((uint32_t)buf[17] << 24);
        if((fsize == len || fsize == 0) && off < len &&
           (dib == 12 || dib == 40 || dib == 52 || dib == 56 || dib == 64 || dib == 108 || dib == 124))
            return FT_BMP;
    }
    return FT_UNKNOWN;
}

/* defined with the email converter, far below; identify_content() is where
   they are used */
static int has_email_signature(const unsigned char *buf, size_t len);
static int has_mbox_signature(const unsigned char *buf, size_t len);

static filetype_t identify_content(const unsigned char *buf, size_t len,
                                   const char *filename)
{
    if(len == 0)
        return identify_from_extension(filename);

    /* A UTF-8 BOM belongs to no signature below, and the whitespace skip in
       has_html_signature() does not step over one.  A BOM'd HTML document
       handed to convert() -- where there is no extension to fall back on --
       came out unidentified and was returned as raw markup. */
    if(len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
    {
        buf += 3;
        len -= 3;
    }

    if(len >= 5 && !memcmp(buf, "%PDF-", 5))
        return FT_PDF;

    {
        filetype_t it = identify_image(buf, len);
        if(it != FT_UNKNOWN)
            return it;
    }

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

    /* Everything above this point is a SIGNATURE -- bytes that only one
       format produces -- and rightly outranks a lying extension: a PDF
       named .txt is still a PDF.  The two probes below are not
       signatures, they are statistical guesses between two kinds of
       TEXT, and mistaking prose for either is lossy: the "conversion"
       joins wrapped lines and eats structural characters.  Measured,
       five of 9,822 RFCs -- plain .txt, hard-wrapped, full of ASCII
       box-drawing -- scored as markdown off their '****' rules and '#'
       column labels, and their stored text came out as run-on
       paragraphs.  The LaTeX probe is the same kind of guess and was
       the same kind of wrong: it hunts command-looking substrings
       anywhere in the first kilobyte, so a .txt that merely MENTIONS
       \section{}, or a .py holding "\\section{%s}" in a string, was
       converted as LaTeX -- and a stray % then ate the rest of its
       line as a comment.  So when the file EXPLICITLY says it is text,
       believe it, and keep the guesswork for files that say nothing. */
    {
        filetype_t ext = identify_from_extension(filename);
        if(ext == FT_TEXT || ext == FT_PLAINTEXT)
            return ext;
    }

    /* mbox first: it is the more specific of the two (a "From " line AND a
       header block).  Both are guesses, which is why they sit here rather
       than up with the signatures. */
    if(has_mbox_signature(buf, len))
        return FT_MBOX;

    if(has_email_signature(buf, len))
        return FT_EMAIL;

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
    if(rp_stat(filename, &st) != 0)
        return NULL;

    size_t fsize = (size_t)st.st_size;
    FILE *f = rp_fopen(filename, "rb");
    if(!f)
        return NULL;

    unsigned char *buf = NULL;
    REMALLOC(buf, fsize + 1);

    size_t nread = fread(buf, 1, fsize, f);
    rp_fclose(f);

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
    /* SECURITY (F18): absolute ceiling so a tiny gzip declaring a huge trailer
       size cannot drive a multi-GB allocation (decompression bomb). */
    if(alloc_size > ((size_t)512 << 20))
        alloc_size = (size_t)512 << 20;

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
        if(alloc_size > ((size_t)512 << 20)) /* F18: keep the growth bounded */
            alloc_size = (size_t)512 << 20;
    }

    libdeflate_free_decompressor(d);
    return NULL;
}

/* strip .gz from a filename to get the inner name for extension-based
   identification.  The caller owns the buffer: a static one here would be
   shared by every conversion running in every thread of a server. */
static const char *strip_gz_ext(const char *filename, char *stripped, size_t size)
{
    size_t flen = strlen(filename);
    if(flen >= 4 && !strcasecmp(filename + flen - 3, ".gz"))
    {
        size_t copy = flen - 3;
        if(copy >= size) copy = size - 1;
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

/* Resolve a helper program on PATH.  This used to be exec('which', ...) inside
   every snippet below -- a fork and an exec on each and every conversion, and
   the same per-call `which` that deadlocked cmodule loading against
   rampart-sql's SIGCHLD handler.  A stat() scan costs nothing and forks
   nothing.  No cache: the scan is microseconds, and a static one would be
   shared across threads. */
static int tt_find_tool(const char *name, char *out, size_t outsz)
{
    const char *path = getenv("PATH"), *p;
    size_t nlen = strlen(name);

    if(!path || !*path) path = "/usr/local/bin:/usr/bin:/bin";

    for(p = path; *p; )
    {
        const char *end = strchr(p, ':');
        size_t dlen = end ? (size_t)(end - p) : strlen(p);

        if(dlen && dlen + nlen + 2 <= outsz)
        {
            struct stat st;
            memcpy(out, p, dlen);
            out[dlen] = '/';
            memcpy(out + dlen + 1, name, nlen + 1);
            if(stat(out, &st) == 0 && S_ISREG(st.st_mode) && access(out, X_OK) == 0)
                return 1;
        }
        if(!end) break;
        p = end + 1;
    }
    out[0] = 0;
    return 0;
}

/* compile a JS function expression and leave the function on the stack */
static void tt_push_fn(duk_context *ctx, const char *js_src, const char *name)
{
    if(duk_pcompile_string(ctx, DUK_COMPILE_EVAL, js_src) != 0)
        RP_THROW(ctx, "convert %s: compile error: %s", name, duk_safe_to_string(ctx, -1));

    if(duk_pcall(ctx, 0) != 0)
        RP_THROW(ctx, "convert %s: eval error: %s", name, duk_safe_to_string(ctx, -1));
}

/* helper: compile a JS function expression, call with one string arg,
   leave the result on top of the duktape stack. */
static void call_js_with_string(duk_context *ctx, const char *js_src,
                                const char *name, const char *arg, size_t arg_len)
{
    tt_push_fn(ctx, js_src, name);

    duk_push_lstring(ctx, arg, arg_len);

    if(duk_pcall(ctx, 1) != 0)
        RP_THROW(ctx, "convert %s: %s", name, duk_safe_to_string(ctx, -1));
}

/* fn(tool, payload) where the payload is a string */
static void tt_call_tool_str(duk_context *ctx, const char *js_src, const char *name,
                             const char *tool, const char *arg, size_t alen)
{
    tt_push_fn(ctx, js_src, name);
    duk_push_string(ctx, tool);
    duk_push_lstring(ctx, arg, (duk_size_t)alen);
    if(duk_pcall(ctx, 2) != 0)
        RP_THROW(ctx, "convert %s: %s", name, duk_safe_to_string(ctx, -1));
}

/* fn(tool, payload) where the payload is the document's bytes */
static void tt_call_tool_buf(duk_context *ctx, const char *js_src, const char *name,
                             const char *tool, const unsigned char *buf, size_t len)
{
    void *b;
    tt_push_fn(ctx, js_src, name);
    duk_push_string(ctx, tool);
    b = duk_push_fixed_buffer(ctx, (duk_size_t)len);
    memcpy(b, buf, len);
    if(duk_pcall(ctx, 2) != 0)
        RP_THROW(ctx, "convert %s: %s", name, duk_safe_to_string(ctx, -1));
}

/* convenience: call with file content as a string */
static void call_js_converter(duk_context *ctx, const char *js_src,
                               const char *name,
                               const unsigned char *buf, size_t len)
{
    call_js_with_string(ctx, js_src, name, (const char *)buf, len);
}

/* HTML -> text via rampart-html.
 *
 * toText() extracts only VISIBLE text by default, which for a search index
 * throws away several kinds of real, human-authored content.  The options
 * turned on here are the ones that are text:
 *
 *   imgAltText      the description of an image is prose someone wrote
 *   metaDescription a page's own summary of itself
 *   metaKeywords    likewise
 *
 * The ones left off are formatting or addresses, which this module discards
 * by definition: aLinks and imgLinks append the href/src in markdown style
 * after text that has already been extracted, enumerateLists prepends "*" and
 * "1." (exactly the auto-numbering the LibreOffice differential shows we are
 * right not to emit), and showHRTags draws rules. */
#define TT_HTML_TOTEXT_OPTS \
    "{concatenate:true, imgAltText:true, metaDescription:true, metaKeywords:true}"

static const char html_convert_js[] =
    "(function(content) {"
    "  var html = require('rampart-html');"
    "  return html.newDocument(content).toText(" TT_HTML_TOTEXT_OPTS ");"
    "})";

/* Markdown -> HTML via rampart-cmark, then HTML -> text via rampart-html.
 *
 * `unsafe` is what keeps raw HTML blocks in the output.  cmark drops them by
 * default -- it replaces each with "<!-- raw HTML omitted -->" -- and a README
 * that centres its blurb in a <div><sub>...</sub></div>, which is most of
 * them, lost that text entirely.  The name is about rendering untrusted
 * markdown into a page; here the HTML never reaches a browser, it goes
 * straight into a tag stripper, so there is nothing to inject into.
 *
 * Passing an options object turns the extensions from default-on to explicit,
 * so they have to be named again or tables and strikethrough would go. */
static const char md_convert_js[] =
    "(function(content) {"
    "  var cmark = require('rampart-cmark');"
    "  var html = require('rampart-html');"
    "  var h = cmark.toHtml(content, {"
    "      unsafe: true, table: true, strikethrough: true,"
    "      autolink: true, tagfilter: true, tasklist: true"
    "  });"
    "  return html.newDocument(h).toText(" TT_HTML_TOTEXT_OPTS ");"
    "})";

/* How to install a missing converter, in the terms of THIS platform.
   The build knows which one it is; the message should not tell a mac
   operator to apt-get.  Commands as documented in rampart-totext.rst. */
#if defined(__APPLE__)
#  define TT_PDF_HINT "install it with: brew install poppler"
#  define TT_DOC_HINT "textutil is built in on macOS -- this should not happen"
#elif defined(__FreeBSD__)
#  define TT_PDF_HINT "install it with: pkg install poppler-utils"
#  define TT_DOC_HINT "install it with: pkg install catdoc"
#else
#  define TT_PDF_HINT "install it with: apt install poppler-utils, " \
                      "or dnf install poppler-utils"
#  define TT_DOC_HINT "install it with: apt install catdoc, " \
                      "or dnf install catdoc"
#endif

/* The converters below take the tool's absolute path as their first argument;
   C resolves it with tt_find_tool() so that nothing here has to fork `which`. */

/* PDF -> text via pdftotext (takes filename as argument) */
static const char pdf_convert_file_js[] =
    "(function(pdftotext, filename) {"
    "  var res = rampart.utils.exec(pdftotext, '-enc', 'UTF-8', filename, '-');"
    "  if(res.exitStatus)"
    "    throw new Error('convert pdf: pdftotext failed: ' + res.stderr);"
    "  return res.stdout;"
    "})";

/* PDF -> text via pdftotext from stdin (takes content as argument).
   Falls back to a temp file if pdftotext doesn't support stdin ('-'). */
static const char pdf_convert_buf_js[] =
    "(function(pdftotext, content) {"
    "  var exec = rampart.utils.exec;"
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

/* DOC -> text via catdoc (filename, then content-on-stdin) */
static const char doc_catdoc_file_js[] =
    "(function(catdoc, filename) {"
    "  var res = rampart.utils.exec(catdoc, filename);"
    "  if(res.exitStatus)"
    "    throw new Error('convert doc: catdoc failed: ' + res.stderr);"
    "  return res.stdout;"
    "})";

static const char doc_catdoc_buf_js[] =
    "(function(catdoc, content) {"
    "  var res = rampart.utils.exec(catdoc, {stdin:content});"
    "  if(res.exitStatus)"
    "    throw new Error('convert doc: catdoc failed: ' + res.stderr);"
    "  return res.stdout;"
    "})";

/* DOC -> text via textutil, the macOS built-in */
static const char doc_textutil_file_js[] =
    "(function(textutil, filename) {"
    "  var res = rampart.utils.exec(textutil, '-convert', 'txt', '-stdout', filename);"
    "  if(res.exitStatus)"
    "    throw new Error('convert doc: textutil failed: ' + res.stderr);"
    "  return res.stdout;"
    "})";

static const char doc_textutil_buf_js[] =
    "(function(textutil, content) {"
    "  var res = rampart.utils.exec(textutil, '-convert', 'txt', '-stdout', '-stdin',"
    "                               {stdin:content});"
    "  if(res.exitStatus)"
    "    throw new Error('convert doc: textutil failed: ' + res.stderr);"
    "  return res.stdout;"
    "})";

/* ================================================================
   CONVERTER: XML (Docbook and generic XML)
   - same approach as HTML: strip tags, decode entities
   ================================================================ */

/* value of name="..." between i and e; returns the value and its length, or
   NULL.  Used by the tag scanner below, the OOXML relationship parts and the
   EPUB package. */
static const char *xml_attr_in(const char *s, size_t i, size_t e,
                               const char *name, size_t *vlen)
{
    size_t nlen = strlen(name);

    for(; i + nlen + 2 < e; i++)
    {
        char q;
        size_t v, k;

        if(memcmp(s + i, name, nlen)) continue;
        /* must be a whole attribute name, not the tail of a longer one */
        if(i > 0 && (isalnum((unsigned char)s[i-1]) || s[i-1] == ':' || s[i-1] == '-'))
            continue;
        if(s[i + nlen] != '=') continue;

        q = s[i + nlen + 1];
        if(q != '"' && q != '\'') continue;

        v = i + nlen + 2;
        for(k = v; k < e && s[k] != q; k++)
            ;
        if(k >= e) return NULL;
        *vlen = k - v;
        return s + v;
    }
    return NULL;
}

/* The text of the first <name ...>...</name>: nested tags stripped, entities
   decoded.  Metadata elements are small and flat, so this is enough for
   dc:title and its cousins.  Returns 1 when found. */
static int xml_elem_text(const char *s, size_t len, const char *name, rp_string *out)
{
    size_t nlen = strlen(name), i, b, e, k;

    for(i = 0; i + nlen + 2 < len; i++)
    {
        if(s[i] != '<' || memcmp(s + i + 1, name, nlen)) continue;
        /* a whole element name, not a prefix of a longer one */
        if(s[i + 1 + nlen] != '>' && s[i + 1 + nlen] != '/' &&
           !isspace((unsigned char)s[i + 1 + nlen])) continue;

        for(b = i + 1 + nlen; b < len && s[b] != '>'; b++)
            ;
        if(b >= len || s[b-1] == '/') return 0;      /* self-closing: no text */
        b++;

        for(e = b; e + nlen + 2 < len; e++)
            if(s[e] == '<' && s[e+1] == '/' && !memcmp(s + e + 2, name, nlen))
                break;
        if(e + nlen + 2 >= len) return 0;

        for(k = b; k < e; )
        {
            if(s[k] == '<')
            {
                while(k < e && s[k] != '>') k++;
                if(k < e) k++;
                continue;
            }
            if(s[k] == '&')
            {
                size_t used = decode_entity(s + k + 1, e - k - 1, out);
                if(used) { k += 1 + used; continue; }
            }
            rp_string_putc(out, s[k]);
            k++;
        }
        return 1;
    }
    return 0;
}

/* Set a metaData property, trimmed; an empty value is simply not set, so a
   caller can test presence rather than emptiness. */
static void tt_meta_set(duk_context *ctx, duk_idx_t obj, const char *key,
                        const char *val, size_t len)
{
    size_t i = 0, e = len;

    if(!val) return;
    while(i < e && isspace((unsigned char)val[i])) i++;
    while(e > i && isspace((unsigned char)val[e-1])) e--;
    if(e == i) return;

    duk_push_lstring(ctx, val + i, (duk_size_t)(e - i));
    duk_put_prop_string(ctx, obj, key);
}

/* pull one XML element's text straight into a metaData key */
static void tt_meta_elem(duk_context *ctx, duk_idx_t obj, const char *key,
                         const char *xml, size_t xlen, const char *elem)
{
    rp_string *v;

    if(duk_has_prop_string(ctx, obj, key)) return;   /* first source wins */
    v = rp_string_new(64);
    if(xml_elem_text(xml, xlen, elem, v))
        tt_meta_set(ctx, obj, key, v->str, v->len);
    rp_string_free(v);
}

/* Office XML splits words across runs -- <w:t>Hel</w:t><w:t>lo</w:t> is one
   word, and Word produces that routinely from spell check, rsid tracking and
   language tagging.  The separator space that keeps generic inline markup from
   concatenating ("<b>a</b><i>b</i>") breaks those words instead, which for a
   search index destroys the term.  Namespaced office tags and the bare OOXML
   run leaves therefore get no separator; everything else still does. */
static int xml_tag_nospace(const char *name, size_t len)
{
    if(memchr(name, ':', len))
        return 1;
    if(len == 1 && (name[0] == 't' || name[0] == 'r')) return 1;
    if(len == 3 && !strncasecmp(name, "rPr", 3)) return 1;
    return 0;
}

/* the few office tags that DO mean whitespace.  Without these the no-space
   rule above would run the words on either side of a tab or a line break
   together. */
static int xml_tag_is_space(const char *name, size_t len)
{
    return (len == 5  && !strncasecmp(name, "w:tab", 5))
        || (len == 4  && !strncasecmp(name, "w:cr", 4))
        || (len == 8  && !strncasecmp(name, "text:tab", 8))
        || (len == 6  && !strncasecmp(name, "text:s", 6))
        || (len == 15 && !strncasecmp(name, "text:line-break", 15));
}

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
        "a:br",  /* ...and its line break: without this the no-space rule
                    below runs the text on either side of it together */
        "si",    /* shared string item (xlsx) */
        /* ODF (odt/odp/ods) */
        "text:p", "text:h", "text:list-item",
        "text:list", "text:section",
        /* a footnote or endnote is its own block, marker included: left
           inline it would glue its citation onto the preceding word */
        "text:note", "text:note-citation", "text:note-body",
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

            /* CDATA holds literal text and may contain '>', so the plain
               skip-to-'>' below would stop inside it, losing everything up to
               that point and leaving the "]]>" in the output. */
            if(i + 9 <= len && !memcmp(s + i, "<![CDATA[", 9))
            {
                size_t c = i + 9, e = c;
                while(e + 2 < len && !(s[e] == ']' && s[e+1] == ']' && s[e+2] == '>'))
                    e++;
                if(e + 2 >= len) e = len;
                rp_string_putsn(out, s + c, e - c);
                i = (e + 3 <= len) ? e + 3 : len;
                continue;
            }

            /* <!DOCTYPE ... [ ... ]> -- same problem: the internal subset
               contains '>' characters that do not end the declaration. */
            if(i + 2 < len && s[i+1] == '!' && s[i+2] != '-')
            {
                size_t e = i + 2;
                int insub = 0;
                while(e < len)
                {
                    if(s[e] == '[')      insub = 1;
                    else if(s[e] == ']') insub = 0;
                    else if(s[e] == '>' && !insub) break;
                    e++;
                }
                i = (e < len) ? e + 1 : len;
                continue;
            }

            if(tag_start < len && s[tag_start] == '/')
                tag_start++;

            size_t name_start = tag_start;
            while(tag_start < len && s[tag_start] != '>' && s[tag_start] != ' '
                  && s[tag_start] != '\t' && s[tag_start] != '\n'
                  && s[tag_start] != '/' && s[tag_start] != '\r')
                tag_start++;

            size_t name_len = tag_start - name_start;

            /* A few office tags ARE a character rather than markup.
               <w:sym w:char="00DA"/> is how Word stores a symbol-font glyph;
               it held the only "U" in the corpus's unicode.docx, and dropping
               the tag dropped the character with it. */
            if(name_len == 5 && !strncasecmp(s + name_start, "w:sym", 5))
            {
                size_t te = tag_start, clen = 0;
                const char *cv;

                while(te < len && s[te] != '>') te++;
                cv = xml_attr_in(s, name_start, te, "w:char", &clen);
                if(cv && clen && clen < 9)
                {
                    char hex[9];
                    unsigned long cp;
                    memcpy(hex, cv, clen);
                    hex[clen] = 0;
                    cp = strtoul(hex, NULL, 16);
                    /* F000-F0FF is the private-use block that symbol fonts
                       map their glyphs into: no text to recover there */
                    if(cp > 0 && cp < 0xF000)
                    {
                        char u8[4];
                        int n = utf8_encode(cp, u8);
                        if(n > 0)
                            rp_string_putsn(out, u8, n);
                    }
                }
                i = (te < len) ? te + 1 : len;
                continue;
            }

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

            if(!handled && xml_tag_is_space(s + name_start, name_len))
            {
                rp_string_putc(out, ' ');
                handled = 1;
            }

            /* a non-breaking hyphen is still a hyphen */
            if(!handled && name_len == 15
               && !strncasecmp(s + name_start, "w:noBreakHyphen", 15))
            {
                rp_string_putc(out, '-');
                handled = 1;
            }

            /* for any non-block tag, ensure words don't concatenate: emit a
               space if the last output char isn't already whitespace.  Office
               run structure is exempt -- see xml_tag_nospace(). */
            if(!handled && out->len > 0 && !xml_tag_nospace(s + name_start, name_len))
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

/* An \ansi RTF document's \'hh escapes are Windows-1252, which has printable
   characters where Latin-1 has C1 controls.  Encoding the byte as its own
   codepoint is Latin-1, and turned every smart quote, dash, ellipsis and
   bullet Word emits into an invisible control character. */
static const unsigned short cp1252_80_9f[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

static rp_string *convert_rtf(const unsigned char *buf, size_t len)
{
    rp_string *out = rp_string_new(len);
    const char *s = (const char *)buf;
    size_t i = 0;
    int depth = 0;
    int skip_group = 0;     /* depth at which we started skipping */
    int skip_depth = 0;
    int uc = 1;             /* \ucN: fallback characters following each \uN */

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
                /* "\*" marks a destination that a reader which does not
                   understand it must DROP -- and we understand none of them.
                   Without this, {\*\generator ...} and the URL inside
                   {\field{\*\fldinst{HYPERLINK ...}}} land in the text. */
                if(i + 2 < len && s[i+1] == '\\' && s[i+2] == '*')
                {
                    skip_group = 1;
                    skip_depth = depth;
                }
                else for(int g = 0; skip_groups[g]; g++)
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
                        /* windows-1252 to utf-8; 0x80-0x9f is the range where
                           it differs from latin-1, and it is exactly the range
                           the punctuation lives in */
                        if(ch >= 0x80 && ch <= 0x9F)
                            ch = cp1252_80_9f[ch - 0x80];
                        if(ch < 0x80)
                            rp_string_putc(out, (char)ch);
                        else
                        {
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
                int k;
                i++;
                long cp = strtol(s + i, NULL, 10);
                /* skip digits and optional minus */
                if(s[i] == '-') i++;
                while(i < len && isdigit(s[i])) i++;
                if(i < len && s[i] == ' ') i++;   /* delimiter */

                /* Skip the \ucN fallback characters that follow.  Assuming a
                   literal '?' was wrong for the common case: Word writes a
                   \'hh escape there, so the fallback used to be emitted as
                   well as the real character -- every unicode character came
                   out followed by a stray one. */
                for(k = 0; k < uc && i < len; k++)
                {
                    if(s[i] == '{' || s[i] == '}') break;
                    if(s[i] == '\\' && i + 1 < len)
                    {
                        if(s[i+1] == '\'' && i + 3 < len) { i += 4; continue; }
                        i += 2;
                        while(i < len && isalpha(s[i])) i++;
                        continue;
                    }
                    i++;
                }

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

            /* read the optional numeric parameter */
            long cw_param = 0;
            int has_param = 0;
            if(i < len && (s[i] == '-' || isdigit(s[i])))
            {
                has_param = 1;
                cw_param = strtol(s + i, NULL, 10);
                if(s[i] == '-') i++;
                while(i < len && isdigit(s[i])) i++;
            }

            /* skip single trailing space delimiter */
            if(i < len && s[i] == ' ') i++;

            /* handle known control words */
            if(cw_len == 2 && !memcmp(s + cw_start, "uc", 2))
            {
                if(has_param && cw_param >= 0 && cw_param < 64)
                    uc = (int)cw_param;
            }
            else if(cw_len == 3 && !memcmp(s + cw_start, "par", 3))
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
            else if(cw_len == 4 && !memcmp(s + cw_start, "sect", 4))
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

/* One inline character or escape sequence starting at i: appends its text to
   out and returns the next position.  The body loop, the headings and the font
   macros all decode through here, so all three treat escapes alike -- heading
   text used to be copied raw, which is why "\*(Dt" appeared verbatim in the
   converted output of grep.1. */
static size_t man_inline(const char *s, size_t len, size_t i,
                         rp_string *out, int in_table)
{
    if(s[i] == '\\' && i + 1 < len)
    {
        char next = s[i+1];

        /* font change: \fX, \f(XX, \f[name] */
        if(next == 'f')
        {
            if(i + 2 >= len) return len;
            if(s[i+2] == '(') return (i + 5 <= len) ? i + 5 : len;
            if(s[i+2] == '[')
            {
                i += 3;
                while(i < len && s[i] != ']') i++;
                return (i < len) ? i + 1 : i;
            }
            return i + 3;
        }

        if(next == '-') { rp_string_putc(out, '-');  return i + 2; }
        if(next == 'e') { rp_string_putc(out, '\\'); return i + 2; }
        if(next == '&' || next == '|') return i + 2;   /* zero-width, thin space */
        if(next == '"') return skip_to_eol(s, len, i);        /* comment to eol */
        if(next == '(') return (i + 4 <= len) ? i + 4 : len;  /* \(XX special */
        if(next == '[')                                       /* \[name] special */
        {
            i += 2;
            while(i < len && s[i] != ']') i++;
            return (i < len) ? i + 1 : i;
        }
        if(next == 'n' || next == '*')   /* number register / string, skip */
        {
            i += 2;
            if(i < len && s[i] == '(')
                return (i + 3 <= len) ? i + 3 : len;
            if(i < len && s[i] == '[')
            {
                while(i < len && s[i] != ']') i++;
                return (i < len) ? i + 1 : i;
            }
            return (i < len) ? i + 1 : i;
        }
        if(next == '0' || next == '~') { rp_string_putc(out, ' '); return i + 2; }

        /* conditional-block delimiters and zero-width break points: no text */
        if(next == '{' || next == '}' || next == '%' || next == ':' || next == '^')
            return i + 2;

        /* \X where X is punctuation is the literal character.  Dropping the
           escaped character instead merged the words on either side of it:
           "pzz@apevzner\.com" came out as one token, "apevznercom". */
        if(!isalnum((unsigned char)next))
        {
            rp_string_putc(out, next);
            return i + 2;
        }

        return i + 2;   /* unknown letter escape: drop it */
    }

    if(in_table && s[i] == '@') { rp_string_putc(out, '\t'); return i + 1; }
    if(in_table && s[i] == 'T' && i + 1 < len && (s[i+1] == '{' || s[i+1] == '}'))
        return i + 2;

    rp_string_putc(out, s[i]);
    return i + 1;
}

/* Emit a macro's arguments as text.  roff quotes group an argument containing
   spaces.  One-font macros (.B .I .SM .SB) join their arguments with a space;
   the alternating-font ones (.BR .RB .IR .RI .BI .IB) concatenate them, which
   is what makes
       .BR \-i ", " \-\-ignore\-case
   read "-i, --ignore-case".  These arguments are the CONTENT of a man page:
   discarding the line, as every unrecognized macro used to, took the option
   names, the SYNOPSIS and half of every DESCRIPTION with it. */
static size_t man_emit_args(const char *s, size_t len, size_t i,
                            rp_string *out, int in_table, int join)
{
    int argn = 0;

    while(i < len && s[i] != '\n')
    {
        while(i < len && (s[i] == ' ' || s[i] == '\t')) i++;
        if(i >= len || s[i] == '\n') break;

        if(argn++ && join)
            rp_string_putc(out, ' ');

        if(s[i] == '"')
        {
            i++;
            while(i < len && s[i] != '"' && s[i] != '\n')
                i = man_inline(s, len, i, out, in_table);
            if(i < len && s[i] == '"') i++;
        }
        else
        {
            while(i < len && s[i] != ' ' && s[i] != '\t' && s[i] != '\n')
            {
                if(s[i] == '\\' && i + 1 < len && s[i+1] == '"')
                    return skip_to_eol(s, len, i);
                i = man_inline(s, len, i, out, in_table);
            }
        }
    }
    return skip_to_eol(s, len, i);
}

static rp_string *convert_man(const unsigned char *buf, size_t len)
{
    rp_string *out = rp_string_new(len);
    const char *s = (const char *)buf;
    size_t i = 0;
    int in_table = 0;
    int in_def = 0;

    while(i < len)
    {
        int bol = (i == 0 || (i > 0 && s[i-1] == '\n'));

        /* A macro DEFINITION is not page content.  .de/.am/.ig bodies run
           until a lone ".." line, and without this grep.1's own definitions
           were emitted as text -- the "$**(la*(m1*(ra$*" in its output. */
        if(in_def)
        {
            if(bol && i + 1 < len && s[i] == '.' && s[i+1] == '.')
                in_def = 0;
            i = skip_to_eol(s, len, i);
            continue;
        }

        /* comments: .\" or '\" at start of line */
        if(bol && i + 1 < len &&
           ((s[i] == '.' && s[i+1] == '\\') || (s[i] == '\'' && s[i+1] == '\\')))
        {
            i = skip_to_eol(s, len, i);
            continue;
        }

        /* Macro lines.  A leading "'" is the no-break control character: it
           invokes the macro exactly as "." does, just without a line break
           first.  Recognizing only "." left lines like "'br" to be emitted as
           literal text. */
        if(bol && (s[i] == '.' || s[i] == '\''))
        {
            i++; /* skip the control character */

            /* read macro name */
            size_t macro_start = i;
            while(i < len && s[i] != ' ' && s[i] != '\n' && s[i] != '\t' && s[i] != '"')
                i++;
            size_t macro_len = i - macro_start;

            /* skip whitespace after macro */
            while(i < len && (s[i] == ' ' || s[i] == '\t')) i++;

            /* headings: the rest of the line is the heading text */
            if(macro_len == 2 && (!memcmp(s + macro_start, "SH", 2) ||
                                  !memcmp(s + macro_start, "SS", 2) ||
                                  !memcmp(s + macro_start, "TH", 2)))
            {
                rp_string_puts(out, "\n\n");
                i = man_emit_args(s, len, i, out, in_table, 1);
                rp_string_puts(out, "\n\n");
                continue;
            }

            /* macro definitions: the body is markup, not text */
            if((macro_len == 2 && (!memcmp(s + macro_start, "de", 2) ||
                                   !memcmp(s + macro_start, "am", 2) ||
                                   !memcmp(s + macro_start, "ig", 2)))
               || (macro_len == 3 && (!memcmp(s + macro_start, "de1", 3) ||
                                      !memcmp(s + macro_start, "am1", 3))))
            {
                in_def = 1;
                i = skip_to_eol(s, len, i);
                continue;
            }

            /* font macros: their arguments are page content */
            {
                static const char *one_font[] = { "B", "I", "SM", "SB", NULL };
                static const char *alt_font[] = { "BR","RB","IR","RI","BI","IB", NULL };
                /* Link macros: the address is the ARGUMENT, so it went the way
                   of every other macro argument.  ".URL \"https://...\"" in a
                   REPORTING BUGS section left the page with no address in it
                   at all -- the commonest residual against pandoc over 500
                   real pages. */
                static const char *link_mac[] = { "URL","UR","UE","MT","ME","LINK", NULL };
                int m, matched = 0;

                for(m = 0; one_font[m]; m++)
                    if(macro_len == strlen(one_font[m]) &&
                       !memcmp(s + macro_start, one_font[m], macro_len))
                    { matched = 1; break; }
                if(!matched)
                    for(m = 0; link_mac[m]; m++)
                        if(macro_len == strlen(link_mac[m]) &&
                           !memcmp(s + macro_start, link_mac[m], macro_len))
                        { matched = 1; break; }
                if(!matched)
                    for(m = 0; alt_font[m]; m++)
                        if(macro_len == strlen(alt_font[m]) &&
                           !memcmp(s + macro_start, alt_font[m], macro_len))
                        { matched = 2; break; }

                if(matched)
                {
                    i = man_emit_args(s, len, i, out, in_table, matched == 1);
                    rp_string_putc(out, ' ');
                    continue;
                }
            }

            /* .IP's first argument is the tag; a second one is an indent
               width, which is not text */
            if(macro_len == 2 && !memcmp(s + macro_start, "IP", 2))
            {
                rp_string_puts(out, "\n\n");
                if(i < len && s[i] == '"')
                {
                    i++;
                    while(i < len && s[i] != '"' && s[i] != '\n')
                        i = man_inline(s, len, i, out, in_table);
                    if(i < len && s[i] == '"') i++;
                }
                else
                {
                    while(i < len && s[i] != ' ' && s[i] != '\t' && s[i] != '\n')
                        i = man_inline(s, len, i, out, in_table);
                }
                rp_string_puts(out, "\n\n");
                i = skip_to_eol(s, len, i);
                continue;
            }

            /* structural breaks, no text of their own */
            if((macro_len == 1 && s[macro_start] == 'P')
               || (macro_len == 2 && (!memcmp(s + macro_start, "PP", 2) ||
                                      !memcmp(s + macro_start, "LP", 2) ||
                                      !memcmp(s + macro_start, "TP", 2) ||
                                      !memcmp(s + macro_start, "RS", 2) ||
                                      !memcmp(s + macro_start, "RE", 2) ||
                                      !memcmp(s + macro_start, "TS", 2) ||
                                      !memcmp(s + macro_start, "TE", 2) ||
                                      !memcmp(s + macro_start, "br", 2) ||
                                      !memcmp(s + macro_start, "sp", 2))))
            {
                if(macro_len == 2 && !memcmp(s + macro_start, "TS", 2))
                    in_table = 1;
                if(macro_len == 2 && !memcmp(s + macro_start, "TE", 2))
                    in_table = 0;

                rp_string_puts(out, "\n\n");
                i = skip_to_eol(s, len, i);
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

        i = man_inline(s, len, i, out, in_table);
    }

    normalize_paragraphs(out);
    return out;
}

/* ================================================================
   PREPROCESSOR: strip pandoc extensions from markdown before cmark
   Removes ::: fenced div lines and {.class #id ...} attribute spans.
   ================================================================ */

/* Does a brace group look like a pandoc attribute span -- {.class #id key=val}
   -- rather than ordinary text that happens to be braced?  The old test was
   "contains a . # or =", which is true of most code: a whole ```json block
   holding { "user.name": "alice" } was deleted from the document, and
   `export FLAGS="{ debug = true }"` came out as `export FLAGS=""`. */
static int md_is_attr_span(const char *s, size_t start, size_t end)
{
    size_t i = start;
    int tokens = 0;

    if(start >= end) return 0;

    while(i < end)
    {
        size_t t;

        while(i < end && isspace((unsigned char)s[i])) i++;
        if(i >= end) break;

        t = i;
        if(s[i] == '.' || s[i] == '#')
        {
            i++;
            while(i < end && (isalnum((unsigned char)s[i]) ||
                              s[i] == '-' || s[i] == '_')) i++;
            if(i == t + 1) return 0;          /* a bare . or # is not a class */
        }
        else if(isalpha((unsigned char)s[i]) || s[i] == '_')
        {
            while(i < end && (isalnum((unsigned char)s[i]) ||
                              s[i] == '-' || s[i] == '_')) i++;
            if(i >= end || s[i] != '=') return 0;      /* a key with no value */
            i++;
            if(i < end && (s[i] == '"' || s[i] == '\''))
            {
                char q = s[i++];
                while(i < end && s[i] != q) i++;
                if(i >= end) return 0;
                i++;
            }
            else
            {
                size_t v = i;
                while(i < end && !isspace((unsigned char)s[i])) i++;
                if(i == v) return 0;
            }
        }
        else
            return 0;

        tokens++;
    }

    return tokens > 0;
}

static rp_string *preprocess_markdown(const unsigned char *buf, size_t len)
{
    rp_string *out = rp_string_new(len + 1);
    const char *s = (const char *)buf;
    size_t i = 0;
    size_t fence = 0;       /* length of the open code fence, 0 if none */
    char fence_ch = 0;

    while(i < len)
    {
        int bol = (i == 0 || (i > 0 && s[i-1] == '\n'));

        /* Code is literal.  Everything this function strips -- the ::: lines
           and the attribute spans -- is markup, and markup does not exist
           inside a fence or a code span. */
        if(bol)
        {
            size_t j = i, ind = 0;
            while(j < len && (s[j] == ' ' || s[j] == '\t') && ind < 3) { j++; ind++; }
            if(j < len && (s[j] == '`' || s[j] == '~'))
            {
                char c = s[j];
                size_t run = 0, k = j;
                while(k < len && s[k] == c) { k++; run++; }
                if(run >= 3)
                {
                    if(!fence)                                { fence = run; fence_ch = c; }
                    else if(c == fence_ch && run >= fence)     { fence = 0; fence_ch = 0; }
                    rp_string_putsn(out, s + i, k - i);
                    i = k;
                    continue;
                }
            }
        }

        if(fence)
        {
            rp_string_putc(out, s[i]);
            i++;
            continue;
        }

        /* ::: fenced div lines - skip entire line */
        if(bol && i + 2 < len && s[i] == ':' && s[i+1] == ':' && s[i+2] == ':')
        {
            while(i < len && s[i] != '\n') i++;
            if(i < len) i++;
            rp_string_putc(out, '\n');
            continue;
        }

        /* an inline code span is literal too */
        if(s[i] == '`')
        {
            size_t run = 0, k = i, e;
            int closed = 0;

            while(k < len && s[k] == '`') { k++; run++; }
            for(e = k; e < len; )
            {
                if(s[e] == '`')
                {
                    size_t r2 = 0, m = e;
                    while(m < len && s[m] == '`') { m++; r2++; }
                    if(r2 == run) { e = m; closed = 1; break; }
                    e = m;
                    continue;
                }
                e++;
            }
            if(closed)
            {
                rp_string_putsn(out, s + i, e - i);
                i = e;
                continue;
            }
            /* unmatched backtick: an ordinary character */
        }

        /* pandoc attribute spans: {.class}, {#id}, {style="..."}, {role="..."} */
        if(s[i] == '{')
        {
            size_t j = i + 1;
            while(j < len && s[j] != '}' && s[j] != '{')
                j++;
            if(j < len && s[j] == '}' && md_is_attr_span(s, i + 1, j))
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
static size_t emit_braced(const char *s, size_t len, size_t i, rp_string *out, int nest);

/* forward declare convert_latex_range for recursive use */
static void convert_latex_range(const char *s, size_t start, size_t end,
                                rp_string *out, int nest);

/* emit_braced() and convert_latex_range() call each other once per brace
   level, so nesting depth is C stack depth: ~40 KB of "{{{{..." used to take
   the process down with SIGSEGV, on input that arrives from convert() without
   ever being trusted.  Past the limit the group is entered WITHOUT recursing
   -- the caller's own loop walks the content and drops the braces -- so the
   text still comes out and the stack stops growing. */
#define LATEX_MAX_NEST 200

static size_t emit_braced(const char *s, size_t len, size_t i, rp_string *out, int nest)
{
    if(i >= len || s[i] != '{') return i;
    if(nest >= LATEX_MAX_NEST) return i + 1;

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
    convert_latex_range(s, start, i, out, nest + 1);
    if(i < len) i++; /* skip closing } */
    return i;
}

static void convert_latex_range(const char *s, size_t start, size_t end,
                                rp_string *out, int nest)
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
                    i = emit_braced(s, end, i, out, nest);
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
               (cmd_len == 9 && !memcmp(s + cmd_start, "underline", 9)) ||
               (cmd_len == 5 && !memcmp(s + cmd_start, "title", 5)))
            {
                if(i < end && s[i] == '{')
                    i = emit_braced(s, end, i, out, nest);
                continue;
            }

            /* \href{url}{text} -> emit text */
            if(cmd_len == 4 && !memcmp(s + cmd_start, "href", 4))
            {
                if(i < end && s[i] == '{')
                    i = skip_braced(s, end, i); /* skip url */
                if(i < end && s[i] == '{')
                    i = emit_braced(s, end, i, out, nest); /* emit text */
                continue;
            }

            /* \textquotesingle -> ' */
            if(cmd_len == 15 && !memcmp(s + cmd_start, "textquotesingle", 15))
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
                       (elen == 11 && !memcmp(s + env_start, "tikzpicture", 11)))
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
               (cmd_len == 14 && !memcmp(s + cmd_start, "phantomsection", 14)) ||
               (cmd_len == 11 && !memcmp(s + cmd_start, "hypertarget", 11)) ||
               (cmd_len == 15 && !memcmp(s + cmd_start, "includegraphics", 15)) ||
               (cmd_len == 13 && !memcmp(s + cmd_start, "pandocbounded", 13)) ||
               (cmd_len == 9 && !memcmp(s + cmd_start, "tightlist", 9)) ||
               (cmd_len == 9 && !memcmp(s + cmd_start, "setlength", 9)) ||
               (cmd_len == 9 && !memcmp(s + cmd_start, "pagestyle", 9)) ||
               (cmd_len == 13 && !memcmp(s + cmd_start, "documentclass", 13)) ||
               (cmd_len == 17 && !memcmp(s + cmd_start, "bibliographystyle", 17)))
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
                i = emit_braced(s, end, i, out, nest);

            continue;
        }

        /* bare braces (grouping) - emit content */
        if(s[i] == '{')
        {
            i = emit_braced(s, end, i, out, nest);
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
    convert_latex_range((const char *)buf, 0, len, out, 0);
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

        /* SECURITY (F10): bound the variable-length fields before reading the
           filename at pos+46 (and before advancing pos by them below). */
        if(pos + 46 + (size_t)fname_len + extra_len + comment_len > cdir_end)
            break;

        const char *fname = (const char *)(zip + pos + 46);

        if(fname_len == target_len && !memcmp(fname, target_name, target_len))
        {
            /* found it - read from local header to get actual data offset.
               SECURITY (F10): compute offsets in size_t so a ~4GB local_off
               cannot wrap the 32-bit add and bypass the bounds checks. */
            if((size_t)local_off + 30 > zip_len) return NULL;
            unsigned local_fname_len = ZIP_READ16(zip + local_off + 26);
            unsigned local_extra_len = ZIP_READ16(zip + local_off + 28);
            size_t data_offset = (size_t)local_off + 30 + local_fname_len + local_extra_len;

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
                size_t alloc_size = uncomp_sz > 0 ? uncomp_sz : (size_t)comp_size * 4;
                if(alloc_size < 4096) alloc_size = 4096;
                /* the size is the archive's own claim: cap it so a crafted
                   entry cannot drive a multi-GB allocation.  Over the cap the
                   inflate simply fails and the entry is skipped. */
                if(alloc_size > ((size_t)512 << 20))
                    alloc_size = (size_t)512 << 20;

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

        /* SECURITY (F10): bound the variable-length fields before the callback
           reads fname[0..fname_len). */
        if(pos + 46 + (size_t)fname_len + extra_len + comment_len > cdir_end)
            break;

        const char *fname    = (const char *)(zip + pos + 46);

        cb(fname, fname_len, zip, zip_len, ud);

        pos += 46 + fname_len + extra_len + comment_len;
    }
}

/* ================================================================
   CONVERTER: DOCX (extract word/document.xml, strip XML tags)
   ================================================================ */

/* Find the main document part in _rels/.rels.
   The part wanted is the one whose relationship Type ENDS in
   "/officeDocument".  Matching the bare substring "officeDocument" instead
   matched the first <Relationship> in the file whatever its type, because
   that substring is also in the namespace of every other OOXML relationship
   ("http://schemas.openxmlformats.org/officeDocument/2006/relationships/
   extended-properties").  The Target then taken from a byte window around it
   was that wrong element's -- usually docProps/app.xml, which extracts
   perfectly well, so the fallback below never ran and 50 of the 83 documents
   in the pandoc test corpus converted to their app metadata. */
static const char *docx_find_document_path(const unsigned char *rels, size_t rels_len,
                                            char *pathbuf, size_t pathbuf_size)
{
    const char *s = (const char *)rels;
    size_t i = 0;

    while(i + 13 <= rels_len)
    {
        size_t e, tlen = 0, glen = 0;
        const char *type, *tgt;

        if(memcmp(s + i, "<Relationship", 13)) { i++; continue; }
        i += 13;

        for(e = i; e < rels_len && s[e] != '>'; e++)
            ;

        type = xml_attr_in(s, i, e, "Type", &tlen);
        tgt  = xml_attr_in(s, i, e, "Target", &glen);
        i = e;

        if(!type || !tgt || !glen) continue;
        if(tlen < 15 || memcmp(type + tlen - 15, "/officeDocument", 15)) continue;

        if(*tgt == '/') { tgt++; glen--; }
        if(!glen || glen >= pathbuf_size) return NULL;

        memcpy(pathbuf, tgt, glen);
        pathbuf[glen] = 0;
        return pathbuf;
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

/* The shared string table: index -> text.  A cell holding a string stores
   only an index into this, so it has to be read before any sheet. */
typedef struct { char **s; size_t n, cap; } xlsx_sst;

static void xlsx_sst_add(xlsx_sst *t, rp_string *v)
{
    if(t->n == t->cap)
    {
        t->cap = t->cap ? t->cap * 2 : 256;
        REMALLOC(t->s, t->cap * sizeof(char *));
    }
    t->s[t->n] = malloc(v->len + 1);
    if(t->s[t->n])
    {
        memcpy(t->s[t->n], v->str, v->len);
        t->s[t->n][v->len] = 0;
        t->n++;
    }
}

static void xlsx_sst_free(xlsx_sst *t)
{
    size_t i;
    for(i = 0; i < t->n; i++) free(t->s[i]);
    free(t->s);
    t->s = NULL;
    t->n = t->cap = 0;
}

/* every <t> between b and e, concatenated with NOTHING between them: rich
   text splits one cell's string across <r><t> runs, exactly as Word splits a
   word across <w:r> runs, and joining them with a space would break the
   word. */
static void xlsx_collect_t(const char *s, size_t b, size_t e, rp_string *out)
{
    while(b < e)
    {
        size_t tb, te;

        if(s[b] != '<' || b + 3 > e || s[b+1] != 't' ||
           (s[b+2] != '>' && s[b+2] != ' ' && s[b+2] != '/'))
        {
            b++;
            continue;
        }
        for(tb = b; tb < e && s[tb] != '>'; tb++)
            ;
        if(tb >= e) return;
        if(s[tb-1] == '/') { b = tb + 1; continue; }     /* <t/> */
        tb++;
        for(te = tb; te + 3 < e && !(s[te] == '<' && s[te+1] == '/' && s[te+2] == 't'); te++)
            ;
        if(te + 3 >= e) return;

        {
            size_t k = tb;
            while(k < te)
            {
                if(s[k] == '&')
                {
                    size_t used = decode_entity(s + k + 1, te - k - 1, out);
                    if(used) { k += 1 + used; continue; }
                }
                rp_string_putc(out, s[k]);
                k++;
            }
        }
        b = te + 3;
    }
}

static void xlsx_read_sst(const unsigned char *buf, size_t len, xlsx_sst *t)
{
    size_t sslen = 0, i = 0;
    unsigned char *ss = zip_extract(buf, len, "xl/sharedStrings.xml", &sslen);
    const char *s;

    memset(t, 0, sizeof *t);
    if(!ss) return;
    s = (const char *)ss;

    while(i + 4 <= sslen)
    {
        size_t b, e;
        rp_string *v;

        if(memcmp(s + i, "<si>", 4) && memcmp(s + i, "<si ", 4)) { i++; continue; }
        for(b = i; b < sslen && s[b] != '>'; b++)
            ;
        b++;
        for(e = b; e + 5 <= sslen && memcmp(s + e, "</si>", 5); e++)
            ;

        v = rp_string_new(64);
        xlsx_collect_t(s, b, e < sslen ? e : sslen, v);
        xlsx_sst_add(t, v);
        rp_string_free(v);

        i = (e + 5 <= sslen) ? e + 5 : sslen;
    }
    free(ss);
}

/* Cell formats.  A date in a workbook is a NUMBER -- days since 1899-12-30 --
   and only the format applied to the cell says it is a date.  Emitting the
   serial puts "46095" in the index where "2026-03-14" belongs, which is the
   difference between finding an invoice by its year and not. */
typedef struct { unsigned char *isdate; size_t n; } xlsx_styles;

static int xlsx_fmt_is_date(int id, const char *code, size_t clen)
{
    /* the built-in date and time formats */
    if((id >= 14 && id <= 22) || (id >= 45 && id <= 47)) return 1;
    if(!code) return 0;

    {   /* a custom format: look for date tokens outside quoted literals */
        size_t i;
        int qq = 0;
        for(i = 0; i < clen; i++)
        {
            char c = code[i];
            if(c == '"') { qq = !qq; continue; }
            if(qq) continue;
            if(c == '\\') { i++; continue; }
            if(c == 'y' || c == 'Y' || c == 'd' || c == 'D') return 1;
        }
    }
    return 0;
}

static void xlsx_read_styles(const unsigned char *buf, size_t len, xlsx_styles *st)
{
    size_t slen = 0, i = 0, xb, xe;
    unsigned char *sty = zip_extract(buf, len, "xl/styles.xml", &slen);
    const char *s;
    /* custom formats are sparse and small; a flat lookup is plenty */
    struct { int id; int isdate; } custom[128];
    int ncustom = 0, k;

    memset(st, 0, sizeof *st);
    if(!sty) return;
    s = (const char *)sty;

    while(i + 9 <= slen && ncustom < 128)
    {
        size_t e, ilen = 0, clen = 0;
        const char *idv, *code;

        if(memcmp(s + i, "<numFmt ", 8)) { i++; continue; }
        for(e = i; e < slen && s[e] != '>'; e++)
            ;
        idv  = xml_attr_in(s, i, e, "numFmtId", &ilen);
        code = xml_attr_in(s, i, e, "formatCode", &clen);
        i = e;
        if(!idv || !ilen) continue;
        {
            char b[16];
            if(ilen >= sizeof b) continue;
            memcpy(b, idv, ilen); b[ilen] = 0;
            custom[ncustom].id = (int)strtol(b, NULL, 10);
            custom[ncustom].isdate = xlsx_fmt_is_date(custom[ncustom].id, code, clen);
            ncustom++;
        }
    }

    /* <cellXfs> is the array a cell's s= attribute indexes into */
    for(xb = 0; xb + 9 <= slen && memcmp(s + xb, "<cellXfs", 8); xb++)
        ;
    if(xb + 9 > slen) { free(sty); return; }
    for(xe = xb; xe + 10 <= slen && memcmp(s + xe, "</cellXfs>", 10); xe++)
        ;

    i = xb;
    while(i + 4 <= xe)
    {
        size_t e, ilen = 0;
        const char *idv;
        int id = 0, isdate = 0;

        if(memcmp(s + i, "<xf ", 4)) { i++; continue; }
        for(e = i; e < xe && s[e] != '>'; e++)
            ;
        idv = xml_attr_in(s, i, e, "numFmtId", &ilen);
        i = e;
        if(idv && ilen && ilen < 16)
        {
            char b[16];
            memcpy(b, idv, ilen); b[ilen] = 0;
            id = (int)strtol(b, NULL, 10);
        }
        isdate = xlsx_fmt_is_date(id, NULL, 0);
        for(k = 0; k < ncustom; k++)
            if(custom[k].id == id) { isdate = custom[k].isdate; break; }

        REMALLOC(st->isdate, st->n + 1);
        st->isdate[st->n++] = (unsigned char)isdate;
    }
    free(sty);
}

/* an Excel serial to a date a person (and an index) can read */
static void xlsx_serial_date(const char *v, size_t vlen, rp_string *out)
{
    char buf[32], iso[40];
    double d;
    time_t t;
    struct tm tmv;
    int has_time;

    if(vlen >= sizeof buf) return;
    memcpy(buf, v, vlen);
    buf[vlen] = 0;
    d = strtod(buf, NULL);
    if(d < 1 || d > 2958465.0) return;          /* outside 1900..9999 */

    /* 25569 is 1970-01-01 in the 1900 date system */
    t = (time_t)((d - 25569.0) * 86400.0 + 0.5);
    has_time = (d - (double)(long)d) > 1e-9;
    if(!gmtime_r(&t, &tmv)) return;
    strftime(iso, sizeof iso, has_time ? "%Y-%m-%dT%H:%M:%S" : "%Y-%m-%d", &tmv);
    rp_string_puts(out, iso);
}

/* Target of a relationship id, from xl/_rels/workbook.xml.rels */
static int xlsx_rel_target(const char *rels, size_t rlen, const char *rid,
                           char *out, size_t outsz)
{
    size_t i = 0, ridlen = strlen(rid);

    while(i + 13 <= rlen)
    {
        size_t e, ilen = 0, tlen = 0;
        const char *id, *tgt;

        if(memcmp(rels + i, "<Relationship", 13)) { i++; continue; }
        i += 13;
        for(e = i; e < rlen && rels[e] != '>'; e++)
            ;
        id  = xml_attr_in(rels, i, e, "Id", &ilen);
        tgt = xml_attr_in(rels, i, e, "Target", &tlen);
        i = e;
        if(!id || !tgt || ilen != ridlen || memcmp(id, rid, ilen)) continue;
        if(!tlen || tlen >= outsz) return 0;
        memcpy(out, tgt, tlen);
        out[tlen] = 0;
        return 1;
    }
    return 0;
}

/* One worksheet: rows in order, cells separated by tabs.  Keeping the row
   structure is the point -- "INV-90210" and the number beside it mean
   nothing apart. */
static void xlsx_sheet_text(const char *s, size_t len, xlsx_sst *sst,
                            xlsx_styles *st, rp_string *out)
{
    size_t i = 0;
    int cell_on_row = 0;

    while(i < len)
    {
        if(s[i] == '<' && i + 5 <= len && !memcmp(s + i, "<row ", 5))
        {
            if(cell_on_row) rp_string_putc(out, '\n');
            cell_on_row = 0;
            i += 5;
            continue;
        }

        if(s[i] == '<' && i + 3 <= len && s[i+1] == 'c' &&
           (s[i+2] == ' ' || s[i+2] == '>'))
        {
            size_t e, tl = 0, sl = 0, vb, ve;
            const char *ty, *sv;
            char type = 'n';                    /* no t= means a number */
            int isdate = 0;
            rp_string *v;

            for(e = i; e < len && s[e] != '>'; e++)
                ;
            if(e >= len) break;
            ty = xml_attr_in(s, i, e, "t", &tl);
            if(ty && tl) type = ty[0];          /* s, str, inlineStr, b, e, n */

            /* s= indexes cellXfs, which is what says "this number is a date" */
            sv = xml_attr_in(s, i, e, "s", &sl);
            if(sv && sl && sl < 12 && st && st->n)
            {
                char b[12];
                long ix;
                memcpy(b, sv, sl); b[sl] = 0;
                ix = strtol(b, NULL, 10);
                if(ix >= 0 && (size_t)ix < st->n) isdate = st->isdate[ix];
            }

            if(s[e-1] == '/') { i = e + 1; continue; }   /* <c .../> empty cell */

            /* the cell's body, up to </c> */
            vb = e + 1;
            for(ve = vb; ve + 4 <= len && memcmp(s + ve, "</c>", 4); ve++)
                ;
            if(ve + 4 > len) ve = len;

            v = rp_string_new(32);
            if(type == 'i')                      /* inlineStr: <is><t>..</t></is> */
                xlsx_collect_t(s, vb, ve, v);
            else
            {
                /* <v>...</v> */
                size_t b = vb, tb, te;
                while(b + 3 <= ve && memcmp(s + b, "<v>", 3)) b++;
                if(b + 3 <= ve)
                {
                    tb = b + 3;
                    for(te = tb; te + 4 <= ve && memcmp(s + te, "</v>", 4); te++)
                        ;
                    if(type == 's')              /* an index into the string table */
                    {
                        char numbuf[24];
                        size_t n = te - tb;
                        long ix;
                        if(n < sizeof numbuf)
                        {
                            memcpy(numbuf, s + tb, n);
                            numbuf[n] = 0;
                            ix = strtol(numbuf, NULL, 10);
                            if(ix >= 0 && (size_t)ix < sst->n && sst->s[ix])
                                rp_string_puts(v, sst->s[ix]);
                        }
                    }
                    else if(isdate && type == 'n')
                        xlsx_serial_date(s + tb, te - tb, v);
                    else if(type != 'e')         /* numbers, strings, booleans */
                    {
                        size_t k = tb;
                        while(k < te)
                        {
                            if(s[k] == '&')
                            {
                                size_t used = decode_entity(s + k + 1, te - k - 1, v);
                                if(used) { k += 1 + used; continue; }
                            }
                            rp_string_putc(v, s[k]);
                            k++;
                        }
                    }
                }
            }

            if(v->len)
            {
                if(cell_on_row) rp_string_putc(out, '\t');
                rp_string_putsn(out, v->str, v->len);
                cell_on_row = 1;
            }
            rp_string_free(v);
            i = ve + 4;
            continue;
        }
        i++;
    }
    if(cell_on_row) rp_string_putc(out, '\n');
}

/* XLSX.
 *
 * This used to read xl/sharedStrings.xml and nothing else -- a deduplicated
 * table of every distinct STRING in the workbook.  That meant: every number
 * lost (numbers never enter the string table), every sheet name lost, every
 * repeat collapsed, rows and columns gone, and the order was the string
 * table's rather than the reading order.  A workbook of invoices came back
 * as the column headings and the customer names, with not one amount, date
 * or quantity in it.  Measured against LibreOffice on a data workbook, that
 * scored 0.432 recall.
 *
 * The comment that used to sit here said "for now, sharedStrings covers most
 * real-world XLSX files", and the documentation then described the loss as a
 * feature ("without duplicating repeated values"), which is how it survived.
 */
static rp_string *convert_xlsx(const unsigned char *buf, size_t len)
{
    rp_string *out = rp_string_new(len);
    xlsx_sst sst;
    xlsx_styles st;
    size_t wblen = 0, rlen = 0, i = 0;
    unsigned char *wb, *rels = NULL;
    const char *w;
    int sheets = 0;

    xlsx_read_sst(buf, len, &sst);
    xlsx_read_styles(buf, len, &st);

    wb = zip_extract(buf, len, "xl/workbook.xml", &wblen);
    if(wb)
    {
        rels = zip_extract(buf, len, "xl/_rels/workbook.xml.rels", &rlen);
        w = (const char *)wb;

        /* <sheet name="..." r:id="rIdN"/>, in workbook order */
        while(i + 7 <= wblen)
        {
            size_t e, nlen = 0, ilen = 0, slen = 0;
            const char *nm, *rid;
            char target[512], path[600];
            unsigned char *sheet;

            if(memcmp(w + i, "<sheet ", 7)) { i++; continue; }
            for(e = i; e < wblen && w[e] != '>'; e++)
                ;
            nm  = xml_attr_in(w, i, e, "name", &nlen);
            rid = xml_attr_in(w, i, e, "r:id", &ilen);
            if(!rid) rid = xml_attr_in(w, i, e, "id", &ilen);
            i = e;
            if(!rid || !ilen || ilen >= 64) continue;

            {
                char ridz[64];
                memcpy(ridz, rid, ilen);
                ridz[ilen] = 0;
                if(!rels || !xlsx_rel_target((const char *)rels, rlen, ridz,
                                             target, sizeof target))
                    continue;
            }

            /* Targets are relative to xl/ (and occasionally absolute) */
            if(target[0] == '/')
                snprintf(path, sizeof path, "%s", target + 1);
            else
                snprintf(path, sizeof path, "xl/%s", target);

            sheet = zip_extract(buf, len, path, &slen);
            if(!sheet) continue;

            if(out->len) rp_string_puts(out, "\n\n");
            if(nm && nlen)                       /* the sheet's name is content */
            {
                rp_string_putsn(out, nm, nlen);
                rp_string_puts(out, "\n\n");
            }
            xlsx_sheet_text((const char *)sheet, slen, &sst, &st, out);
            free(sheet);
            sheets++;
        }
        free(rels);
        free(wb);
    }

    /* No workbook part, or nothing resolved through it: fall back to the
       string table, which is what this converter used to be. */
    if(!sheets && sst.n)
    {
        size_t k;
        for(k = 0; k < sst.n; k++)
            if(sst.s[k] && *sst.s[k])
            {
                if(out->len) rp_string_puts(out, "\n\n");
                rp_string_puts(out, sst.s[k]);
            }
    }

    xlsx_sst_free(&sst);
    free(st.isdate);
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

/* append one archive entry's bytes to the collected HTML */
static void epub_append_entry(const unsigned char *zip, size_t zip_len,
                              const char *name, rp_string *all_html)
{
    size_t l = 0;
    unsigned char *e = zip_extract(zip, zip_len, name, &l);
    if(!e) return;
    rp_string_putsn(all_html, (const char *)e, l);
    rp_string_putc(all_html, '\n');
    free(e);
}

/* hrefs in the package are URL-escaped; ZIP entry names are not */
static void epub_unescape(char *p)
{
    char *w = p;
    while(*p)
    {
        if(p[0] == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2]))
        {
            char h[3] = { p[1], p[2], 0 };
            *w++ = (char)strtoul(h, NULL, 16);
            p += 3;
        }
        else
            *w++ = *p++;
    }
    *w = 0;
}

/* Collect the content documents in SPINE order -- the order the book is meant
   to be read in -- by way of META-INF/container.xml and the OPF package.
   Returns the number appended; 0 means the package could not be read, and the
   caller falls back to ZIP order, which is whatever the producer happened to
   write and puts the table of contents wherever it likes. */
/* The OPF package.  META-INF/container.xml names it; opfpath is filled in
   because hrefs inside are relative to the package's own directory. */
static unsigned char *epub_get_opf(const unsigned char *zip, size_t zip_len,
                                   char *opfpath, size_t pathsz, size_t *out_len)
{
    size_t clen = 0, vlen = 0;
    unsigned char *container;
    const char *v;

    container = zip_extract(zip, zip_len, "META-INF/container.xml", &clen);
    if(!container) return NULL;

    v = xml_attr_in((const char *)container, 0, clen, "full-path", &vlen);
    if(!v || !vlen || vlen >= pathsz) { free(container); return NULL; }
    memcpy(opfpath, v, vlen);
    opfpath[vlen] = 0;
    free(container);
    epub_unescape(opfpath);

    return zip_extract(zip, zip_len, opfpath, out_len);
}

static int epub_spine_collect(const unsigned char *zip, size_t zip_len, rp_string *all_html)
{
    size_t olen = 0, blen = 0, i;
    unsigned char *opf;
    char opfpath[512], full[1024];
    const char *s;
    int n = 0;

    opf = epub_get_opf(zip, zip_len, opfpath, sizeof opfpath, &olen);
    if(!opf) return 0;
    s = (const char *)opf;

    {   /* hrefs are relative to the package's own directory */
        char *sl = strrchr(opfpath, '/');
        blen = sl ? (size_t)(sl - opfpath) + 1 : 0;
    }

    for(i = 0; i + 8 < olen; i++)
    {
        size_t e, j, ilen = 0;
        const char *idref;

        if(memcmp(s + i, "<itemref", 8)) continue;
        for(e = i; e < olen && s[e] != '>'; e++)
            ;
        idref = xml_attr_in(s, i, e, "idref", &ilen);
        i = e;
        if(!idref || !ilen) continue;

        /* resolve the idref through the manifest */
        for(j = 0; j + 6 < olen; j++)
        {
            size_t k, idl = 0, hl = 0;
            const char *id, *href;

            if(memcmp(s + j, "<item", 5)) continue;
            if(!isspace((unsigned char)s[j+5])) continue;   /* <itemref, not <item */

            for(k = j; k < olen && s[k] != '>'; k++)
                ;
            id = xml_attr_in(s, j, k, "id", &idl);
            if(!id || idl != ilen || memcmp(id, idref, ilen)) { j = k; continue; }

            href = xml_attr_in(s, j, k, "href", &hl);
            if(href && hl && blen + hl < sizeof full)
            {
                memcpy(full, opfpath, blen);
                memcpy(full + blen, href, hl);
                full[blen + hl] = 0;
                epub_unescape(full);
                epub_append_entry(zip, zip_len, full, all_html);
                n++;
            }
            break;
        }
    }

    free(opf);
    return n;
}

static void convert_epub(duk_context *ctx, const unsigned char *buf, size_t len)
{
    rp_string *all_html = rp_string_new(len);

    if(epub_spine_collect(buf, len, all_html) == 0)
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
   METADATA
   ================================================================

   One common schema across every format, so a consumer that keys off
   `title`/`author`/`subject`/`date` works whatever the document was.  Keys
   are set only when the format actually supplies them, so presence is a
   meaningful test.  Format-specific extras (a man page's section, say) sit
   alongside the common ones.

   Only gathered when the caller asked for details -- the plain string path
   must not pay for a zip_extract or an exec it will never use. */

/* OOXML: docProps/core.xml */
static void tt_meta_ooxml(duk_context *ctx, duk_idx_t obj,
                          const unsigned char *buf, size_t len)
{
    size_t clen = 0;
    unsigned char *core = zip_extract(buf, len, "docProps/core.xml", &clen);
    const char *x;

    if(!core) return;
    x = (const char *)core;
    tt_meta_elem(ctx, obj, "title",       x, clen, "dc:title");
    tt_meta_elem(ctx, obj, "author",      x, clen, "dc:creator");
    tt_meta_elem(ctx, obj, "subject",     x, clen, "dc:subject");
    tt_meta_elem(ctx, obj, "description", x, clen, "dc:description");
    tt_meta_elem(ctx, obj, "keywords",    x, clen, "cp:keywords");
    tt_meta_elem(ctx, obj, "language",    x, clen, "dc:language");
    tt_meta_elem(ctx, obj, "created",     x, clen, "dcterms:created");
    tt_meta_elem(ctx, obj, "modified",    x, clen, "dcterms:modified");
    free(core);
}

/* ODF: meta.xml */
static void tt_meta_odf(duk_context *ctx, duk_idx_t obj,
                        const unsigned char *buf, size_t len)
{
    size_t mlen = 0;
    unsigned char *m = zip_extract(buf, len, "meta.xml", &mlen);
    const char *x;

    if(!m) return;
    x = (const char *)m;
    tt_meta_elem(ctx, obj, "title",       x, mlen, "dc:title");
    tt_meta_elem(ctx, obj, "author",      x, mlen, "dc:creator");
    tt_meta_elem(ctx, obj, "subject",     x, mlen, "dc:subject");
    tt_meta_elem(ctx, obj, "description", x, mlen, "dc:description");
    tt_meta_elem(ctx, obj, "keywords",    x, mlen, "meta:keyword");
    tt_meta_elem(ctx, obj, "language",    x, mlen, "dc:language");
    tt_meta_elem(ctx, obj, "created",     x, mlen, "meta:creation-date");
    tt_meta_elem(ctx, obj, "modified",    x, mlen, "dc:date");
    free(m);
}

/* EPUB: the OPF <metadata> block, which we already open for spine order */
static void tt_meta_epub(duk_context *ctx, duk_idx_t obj,
                         const unsigned char *buf, size_t len)
{
    size_t olen = 0;
    char opfpath[512];
    unsigned char *opf = epub_get_opf(buf, len, opfpath, sizeof opfpath, &olen);
    const char *x;

    if(!opf) return;
    x = (const char *)opf;
    tt_meta_elem(ctx, obj, "title",       x, olen, "dc:title");
    tt_meta_elem(ctx, obj, "author",      x, olen, "dc:creator");
    tt_meta_elem(ctx, obj, "subject",     x, olen, "dc:subject");
    tt_meta_elem(ctx, obj, "description", x, olen, "dc:description");
    tt_meta_elem(ctx, obj, "language",    x, olen, "dc:language");
    tt_meta_elem(ctx, obj, "created",     x, olen, "dc:date");
    tt_meta_elem(ctx, obj, "publisher",   x, olen, "dc:publisher");
    free(opf);
}

/* HTML: <title> and the <meta name=...> tags */
static void tt_meta_html(duk_context *ctx, duk_idx_t obj,
                         const char *s, size_t len)
{
    size_t i, scan = len < 65536 ? len : 65536;

    tt_meta_elem(ctx, obj, "title", s, scan, "title");

    for(i = 0; i + 5 < scan; i++)
    {
        size_t e, nlen = 0, clen = 0;
        const char *nm, *ct;

        if(memcmp(s + i, "<meta", 5) || !isspace((unsigned char)s[i+5])) continue;
        for(e = i; e < scan && s[e] != '>'; e++)
            ;
        nm = xml_attr_in(s, i, e, "name", &nlen);
        ct = xml_attr_in(s, i, e, "content", &clen);
        i = e;
        if(!nm || !ct || !clen) continue;

        if(nlen == 6 && !strncasecmp(nm, "author", 6))
            tt_meta_set(ctx, obj, "author", ct, clen);
        else if(nlen == 8 && !strncasecmp(nm, "keywords", 8))
            tt_meta_set(ctx, obj, "keywords", ct, clen);
        else if(nlen == 11 && !strncasecmp(nm, "description", 11))
            tt_meta_set(ctx, obj, "description", ct, clen);
    }
}

/* man: the .TH line -- name, section, date, source, manual */
static void tt_meta_man(duk_context *ctx, duk_idx_t obj,
                        const char *s, size_t len)
{
    static const char *keys[] = { "title", "section", "date", "source", "manual" };
    size_t i = 0;

    while(i < len)
    {
        int bol = (i == 0 || s[i-1] == '\n');
        int a;

        if(!bol || i + 3 > len || s[i] != '.' || s[i+1] != 'T' || s[i+2] != 'H')
        {
            i = skip_to_eol(s, len, i);
            continue;
        }

        i += 3;
        for(a = 0; a < 5 && i < len && s[i] != '\n'; a++)
        {
            rp_string *v = rp_string_new(32);

            while(i < len && (s[i] == ' ' || s[i] == '\t')) i++;
            if(i < len && s[i] == '"')
            {
                i++;
                while(i < len && s[i] != '"' && s[i] != '\n')
                    i = man_inline(s, len, i, v, 0);
                if(i < len && s[i] == '"') i++;
            }
            else
            {
                while(i < len && s[i] != ' ' && s[i] != '\t' && s[i] != '\n')
                    i = man_inline(s, len, i, v, 0);
            }
            tt_meta_set(ctx, obj, keys[a], v->str, v->len);
            rp_string_free(v);
        }
        return;
    }
}

/* PDF: pdfinfo.  Needs a file, so buffer input gets no PDF metadata. */
static const char pdf_info_js[] =
    "(function(pdfinfo, file) {"
    "  var res = rampart.utils.exec(pdfinfo, file);"
    "  if(res.exitStatus) return '';"
    "  return res.stdout;"
    "})";

static void tt_meta_pdf(duk_context *ctx, duk_idx_t obj, const char *filename)
{
    static const struct { const char *label; const char *key; } map[] = {
        {"Title",        "title"},    {"Author",   "author"},
        {"Subject",      "subject"},  {"Keywords", "keywords"},
        {"CreationDate", "created"},  {"ModDate",  "modified"},
        {NULL, NULL}
    };
    char tool[PATH_MAX];
    const char *out;
    duk_size_t olen = 0;
    size_t i = 0;

    if(!tt_find_tool("pdfinfo", tool, sizeof tool)) return;

    tt_push_fn(ctx, pdf_info_js, "pdf");
    duk_push_string(ctx, tool);
    duk_push_string(ctx, filename);
    if(duk_pcall(ctx, 2) != 0) { duk_pop(ctx); return; }   /* metadata is optional */

    out = duk_get_lstring(ctx, -1, &olen);
    if(!out) { duk_pop(ctx); return; }

    while(i < (size_t)olen)
    {
        size_t eol = i, colon;
        int m;

        while(eol < olen && out[eol] != '\n') eol++;
        for(colon = i; colon < eol && out[colon] != ':'; colon++)
            ;
        if(colon < eol)
        {
            size_t v = colon + 1;
            while(v < eol && (out[v] == ' ' || out[v] == '\t')) v++;
            for(m = 0; map[m].label; m++)
                if(colon - i == strlen(map[m].label) &&
                   !memcmp(out + i, map[m].label, colon - i))
                {
                    tt_meta_set(ctx, obj, map[m].key, out + v, eol - v);
                    break;
                }
        }
        i = eol + 1;
    }
    duk_pop(ctx);
}

/* Push the metaData object for this file. */
static void tt_push_metadata(duk_context *ctx, const unsigned char *buf, size_t len,
                             filetype_t ft, const char *filename)
{
    duk_idx_t obj;

    duk_push_object(ctx);
    obj = duk_get_top_index(ctx);

    switch(ft)
    {
        case FT_DOCX: case FT_PPTX: case FT_XLSX:
            tt_meta_ooxml(ctx, obj, buf, len);              break;
        case FT_ODT:  case FT_ODP:  case FT_ODS:
            tt_meta_odf(ctx, obj, buf, len);                break;
        case FT_EPUB:
            tt_meta_epub(ctx, obj, buf, len);               break;
        case FT_HTML:
            tt_meta_html(ctx, obj, (const char *)buf, len); break;
        case FT_MAN:
            tt_meta_man(ctx, obj, (const char *)buf, len);  break;
        case FT_PDF:
            if(filename)
                tt_meta_pdf(ctx, obj, filename);
            break;
        default:
            break;
    }
}

/* The derived title, for a database Title field.  First match wins:
     1. a message's Subject          (attaches here when the email types land)
     2. an attachment's filename     (likewise)
     3. the document's own title, whatever the format called it
     4. the source file's basename
   Leaves a string on the stack, or undefined when nothing is derivable --
   which only happens for convert(buffer), where there is no filename. */
static void tt_push_title(duk_context *ctx, duk_idx_t meta, const char *filename)
{
    if(duk_is_object(ctx, meta))
    {
        duk_get_prop_string(ctx, meta, "title");
        if(duk_is_string(ctx, -1)) return;
        duk_pop(ctx);
    }

    if(filename && *filename)
    {
        const char *b = strrchr(filename, '/');
        duk_push_string(ctx, b ? b + 1 : filename);
        return;
    }

    duk_push_undefined(ctx);
}

/* ================================================================
   MAIN CONVERT DISPATCH
   ================================================================ */


/* ================================================================
   CHARSET: file bytes -> UTF-8 before any converter touches them
   ================================================================

   WHY HERE.  convert_text() and convert_plaintext() copied file bytes
   straight into a duktape string, and the HTML and markdown paths push
   the raw buffer into JS.  A duktape string may only hold valid UTF-8;
   one that does not throws a bare "internal error" from the first
   String.replace() that touches it, naming neither the cause nor the
   document.  Found on a corpus of RFCs, where a single file carrying 18
   Windows-1252 quotes in 75 KB of ASCII killed a 599-document ingest at
   number 71.

   WHY NOT AT THE OUTPUT.  Because the declaration is here.  HTML says
   what it is in a <meta charset>, XML in its declaration, and only the
   code holding the raw bytes can read them.  A declaration beats any
   guess, so it has to be consulted before conversion, not after.

   Formats whose text comes from an extractor rather than from the file
   are left alone: pdftotext is invoked with -enc UTF-8, and the
   zip-based formats hold XML that carries its own declaration.  Passing
   their bytes through here would be transcoding a zip archive.
*/

/* Look for a charset declaration in the first 2 KB: HTML's
   <meta charset="..."> or <meta http-equiv content="...; charset=...">,
   and XML's <?xml ... encoding="..."?>.  Writes into the caller's buffer
   (a static one would be shared across threads) and returns it, or NULL. */
static const char *sniff_declared_charset(const unsigned char *buf, size_t len,
                                          filetype_t ft, char *found, size_t fsize)
{
    size_t n = len < 2048 ? len : 2048, i;
    const char *needle = (ft == FT_XML) ? "encoding" : "charset";
    size_t nlen = strlen(needle);

    if(ft != FT_HTML && ft != FT_XML) return NULL;

    for(i = 0; i + nlen + 2 < n; i++)
    {
        size_t j = 0, k;
        /* case-insensitive match of the keyword */
        while(j < nlen && tolower(buf[i + j]) == needle[j]) j++;
        if(j < nlen) continue;

        k = i + nlen;
        while(k < n && (buf[k] == ' ' || buf[k] == '\t')) k++;
        if(k >= n || buf[k] != '=') continue;
        k++;
        while(k < n && (buf[k] == ' ' || buf[k] == '\t' ||
                        buf[k] == '"'  || buf[k] == '\'')) k++;

        j = 0;
        while(k < n && j < fsize - 1 &&
              (isalnum(buf[k]) || buf[k] == '-' || buf[k] == '_'))
            found[j++] = (char)buf[k++];
        found[j] = 0;
        if(j) return found;
    }
    return NULL;
}

/* Which formats take their text from the file's own bytes. */
static int filetype_is_text_bytes(filetype_t ft)
{
    switch(ft)
    {
        case FT_TEXT: case FT_PLAINTEXT: case FT_HTML: case FT_XML:
        case FT_MARKDOWN: case FT_LATEX: case FT_MAN: case FT_RTF:
            return 1;
        default:
            return 0;
    }
}

/* ================================================================
   OCR: image files, and PDF pages without a text layer, through a
   rampart-ocr reader.  The reader is a handle from rampart-ocr.init();
   it is stored on the module object by setOcr(), or passed per call as
   {ocr: reader}.  rampart-ocr is never loaded unless setOcr(true|opts)
   asked for a default reader to be built on demand.
   ================================================================ */

#define TT_OCR_HINT "set one with totext.setOcr(reader) or totext.setOcr(true); " \
                    "rampart-ocr is part of the rampart-langtools package"
#define TT_OCR_READER_KEY "\xff" "ocrReader"
#define TT_OCR_OPTS_KEY   "\xff" "ocrOpts"
#define TT_OCR_MIN_WORDCHARS 20   /* fewer on a PDF page: no usable text layer */
#define TT_OCR_DPI "200"

typedef struct {
    duk_idx_t mod;      /* the module object (`this`) */
    duk_idx_t reader;   /* rampart-ocr reader on the stack, or -1 */
    duk_idx_t pages;    /* details: array collecting per-page results, or -1 */
    duk_idx_t meta;     /* details: the metaData object, or -1 */
    duk_idx_t docs;     /* a converter that produced documents[], or -1 */
    duk_idx_t opts;     /* the caller's options object, for per-format knobs */
    duk_idx_t cb;       /* streaming callback on the stack, or -1 */
    duk_uarridx_t ndocs;/* documents handed to the callback */
    int aborted;        /* the callback returned false */
    int details;        /* the caller asked for the details object */
    int always;         /* ocr:"always": OCR every PDF page */
    int lazy;           /* build a default reader on first need */
    int did_ocr;
} tt_ocr;

typedef struct { char *s; size_t len, cap; } tt_buf;

static void tt_buf_add(tt_buf *b, const char *s, size_t n)
{
    if(b->len + n + 1 > b->cap)
    {
        size_t c = b->cap ? b->cap * 2 : 4096;
        while(c < b->len + n + 1) c *= 2;
        REMALLOC(b->s, c);
        b->cap = c;
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}

static int is_ocr_reader(duk_context *ctx, duk_idx_t idx)
{
    int r;
    if(!duk_is_object(ctx, idx)) return 0;
    duk_get_prop_string(ctx, idx, "readText");
    r = duk_is_function(ctx, -1);
    duk_pop(ctx);
    return r;
}

/* Resolve reader and mode from the options object and the module.  Whatever
   it finds stays on the stack for the rest of the call. */
static void tt_ocr_begin(duk_context *ctx, duk_idx_t opts, int details, tt_ocr *oc)
{
    memset(oc, 0, sizeof *oc);
    oc->reader = -1;
    oc->pages = -1;
    oc->meta = -1;
    oc->docs = -1;
    oc->cb = -1;
    oc->ndocs = 0;
    oc->aborted = 0;
    oc->opts = opts;
    oc->details = details;

    duk_push_this(ctx);
    oc->mod = duk_get_top_index(ctx);

    if(duk_is_object(ctx, opts) && !duk_is_null(ctx, opts))
    {
        duk_get_prop_string(ctx, opts, "ocr");
        if(is_ocr_reader(ctx, -1))
            oc->reader = duk_get_top_index(ctx);
        else
        {
            if(duk_is_string(ctx, -1))
            {
                const char *m = duk_get_string(ctx, -1);
                if(!strcmp(m, "always"))     oc->always = 1;
                else if(strcmp(m, "auto"))
                    RP_THROW(ctx, "convert: ocr must be \"auto\", \"always\", true, or a rampart-ocr reader");
            }
            else if(duk_is_boolean(ctx, -1))
            {
                if(duk_get_boolean(ctx, -1)) oc->lazy = 1;
            }
            else if(!duk_is_undefined(ctx, -1))
                RP_THROW(ctx, "convert: ocr must be \"auto\", \"always\", true, or a rampart-ocr reader");
            duk_pop(ctx);
        }
    }

    if(oc->reader < 0 && duk_is_object(ctx, oc->mod))
    {
        duk_get_prop_string(ctx, oc->mod, TT_OCR_READER_KEY);
        if(is_ocr_reader(ctx, -1))
            oc->reader = duk_get_top_index(ctx);
        else
        {
            duk_pop(ctx);
            if(duk_has_prop_string(ctx, oc->mod, TT_OCR_OPTS_KEY))
                oc->lazy = 1;
        }
    }

    if(details)
    {
        duk_push_array(ctx);
        oc->pages = duk_get_top_index(ctx);
    }
}

/* The reader, building the default one if setOcr(true|opts) allowed it;
   throws when the input needs OCR and there is nothing to do it with. */
/* Should the lazily-built reader load the layout model?  Default yes; the
 * caller opts out with setOcr({layout:false}) (or per call, {ocr:{...}}). */
static int want_layout(duk_context *ctx, tt_ocr *oc)
{
    int yes = 1;
    if(!duk_is_object(ctx, oc->mod)) return yes;
    if(duk_get_prop_string(ctx, oc->mod, TT_OCR_OPTS_KEY))
    {
        if(duk_get_prop_string(ctx, -1, "layout"))
            yes = duk_to_boolean(ctx, -1);
        duk_pop(ctx);
    }
    duk_pop(ctx);
    return yes;
}

static duk_idx_t tt_ocr_reader(duk_context *ctx, tt_ocr *oc, const char *what)
{
    duk_idx_t base, models_idx;

    if(oc->reader >= 0) return oc->reader;
    if(!oc->lazy)
        RP_THROW(ctx, "convert %s: this input needs an OCR reader and none is set -- " TT_OCR_HINT, what);

    /* Building the reader takes several calls, each leaving its result on the
     * stack.  Collapse them at the end: a caller holding a stack index from
     * before this ran would otherwise find that scaffolding piled on top, and
     * the text it expects on top would be an intermediate object instead. */
    base = duk_get_top(ctx);

    /* require('rampart-ocr').init(require('rampart-models').ocrGet('ppocr-v5'), opts) */
    duk_get_global_string(ctx, "require");
    duk_push_string(ctx, "rampart-models");
    if(duk_pcall(ctx, 1) != 0)
        RP_THROW(ctx, "convert %s: cannot load rampart-models (%s) -- " TT_OCR_HINT,
                 what, duk_safe_to_string(ctx, -1));
    duk_push_string(ctx, "ocrGet");
    duk_push_string(ctx, "ppocr-v5");
    if(duk_pcall_prop(ctx, -3, 1) != 0)
        RP_THROW(ctx, "convert %s: rampart-models.ocrGet failed: %s", what, duk_safe_to_string(ctx, -1));
    models_idx = duk_get_top_index(ctx) - 1;   /* the models module, below paths */

    /* The layout model comes too, unless the caller opted out with
     * {layout:false}.  Without it a multi-column page is read ACROSS the
     * columns instead of down them: measured on OmniDocBench, reading order
     * over 415 pages scores 0.83 without layout against 0.94 with it, and
     * three-column pages 0.39 against 0.98.  A module whose job is to take
     * anything should not get that wrong by default.  The cost is the
     * download, 21 MB becoming 145 MB, which is what {layout:false} is for
     * when the caller knows the corpus is single-column. */
    if(want_layout(ctx, oc))
    {
        duk_push_string(ctx, "ocrGet");
        duk_push_string(ctx, "ppocr-layout");
        if(duk_pcall_prop(ctx, models_idx, 1) != 0)
        {
            /* Not fatal: a failed or declined 124 MB fetch should still leave a
             * working reader, just one that cannot order columns. */
            duk_pop(ctx);
        }
        else
        {
            if(duk_is_object(ctx, -1) && duk_get_prop_string(ctx, -1, "layout"))
                duk_put_prop_string(ctx, -3, "layout");   /* onto the paths object */
            else
                duk_pop(ctx);
            duk_pop(ctx);
        }
    }

    duk_get_global_string(ctx, "require");
    duk_push_string(ctx, "rampart-ocr");
    if(duk_pcall(ctx, 1) != 0)
        RP_THROW(ctx, "convert %s: cannot load rampart-ocr (%s) -- " TT_OCR_HINT,
                 what, duk_safe_to_string(ctx, -1));
    duk_push_string(ctx, "init");
    duk_dup(ctx, -3);
    if(duk_is_object(ctx, oc->mod)) duk_get_prop_string(ctx, oc->mod, TT_OCR_OPTS_KEY);
    else duk_push_object(ctx);
    if(!duk_is_object(ctx, -1)) { duk_pop(ctx); duk_push_object(ctx); }
    /* ocr.init merges paths and opts and reads `layout` as a PATH; a boolean
     * left here would overwrite the path set above.  Copy the opts without that
     * key rather than mutating the object the caller handed to setOcr. */
    if(duk_has_prop_string(ctx, -1, "layout"))
    {
        duk_idx_t src = duk_get_top_index(ctx);
        duk_push_object(ctx);
        duk_enum(ctx, src, DUK_ENUM_OWN_PROPERTIES_ONLY);
        while(duk_next(ctx, -1, 1))
        {
            if(!strcmp(duk_get_string(ctx, -2), "layout")) { duk_pop_2(ctx); continue; }
            duk_put_prop(ctx, -4);
        }
        duk_pop(ctx);
        duk_remove(ctx, src);
    }
    if(duk_pcall_prop(ctx, -4, 2) != 0)
        RP_THROW(ctx, "convert %s: rampart-ocr.init failed: %s", what, duk_safe_to_string(ctx, -1));

    if(duk_is_object(ctx, oc->mod))
    {
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, oc->mod, TT_OCR_READER_KEY);   /* reused by later calls */
    }
    duk_insert(ctx, base);          /* the reader down to base ... */
    duk_set_top(ctx, base + 1);     /* ... and drop the scaffolding above it */
    oc->reader = base;
    return oc->reader;
}

/* reader.readText(input, {page}) -> leaves the page's text on the stack.
   doc_page is the page number reported to the caller (a PDF page, or the
   TIFF directory); npages gets the input's page count. */
static void tt_ocr_read(duk_context *ctx, tt_ocr *oc, duk_idx_t input,
                        int page, int doc_page, int *npages)
{
    duk_idx_t res;

    duk_push_string(ctx, "readText");
    duk_dup(ctx, input);
    duk_push_object(ctx);
    duk_push_int(ctx, page);
    duk_put_prop_string(ctx, -2, "page");
    if(duk_pcall_prop(ctx, oc->reader, 2) != 0)
        RP_THROW(ctx, "convert: ocr: %s", duk_safe_to_string(ctx, -1));
    res = duk_get_top_index(ctx);

    duk_get_prop_string(ctx, res, "pages");
    *npages = duk_is_number(ctx, -1) ? duk_get_int(ctx, -1) : 1;
    duk_pop(ctx);

    if(oc->pages >= 0)
    {
        duk_push_int(ctx, doc_page);
        duk_put_prop_string(ctx, res, "page");
        duk_dup(ctx, res);
        duk_put_prop_index(ctx, oc->pages, (duk_uarridx_t)duk_get_length(ctx, oc->pages));
    }
    duk_get_prop_string(ctx, res, "text");
    if(!duk_is_string(ctx, -1)) { duk_pop(ctx); duk_push_string(ctx, ""); }
    duk_remove(ctx, res);
    oc->did_ocr = 1;
}

/* An image file: every page through the reader, pages separated by \f.  The
   bytes are handed over as a Buffer, so a gzipped image works like any other. */
static void tt_ocr_convert_image(duk_context *ctx, tt_ocr *oc,
                                 const unsigned char *buf, size_t len, filetype_t ft)
{
    duk_idx_t input, base = duk_get_top(ctx);
    tt_buf out = {0};
    int p = 0, npages = 1;
    void *b;

    tt_ocr_reader(ctx, oc, filetype_names[ft]);
    b = duk_push_fixed_buffer(ctx, (duk_size_t)len);
    memcpy(b, buf, len);
    input = duk_get_top_index(ctx);
    do
    {
        duk_size_t tl;
        const char *t;
        tt_ocr_read(ctx, oc, input, p, p, &npages);
        t = duk_get_lstring(ctx, -1, &tl);
        if(p) tt_buf_add(&out, "\f", 1);
        tt_buf_add(&out, t, tl);
        duk_pop(ctx);
    } while(++p < npages);
    duk_remove(ctx, input);
    duk_push_lstring(ctx, out.s ? out.s : "", (duk_size_t)out.len);
    duk_insert(ctx, base);          /* the text is the only thing we leave */
    duk_set_top(ctx, base + 1);
    free(out.s);
}

static size_t tt_wordchars(const char *s, size_t n)
{
    size_t i, c = 0;
    for(i = 0; i < n; i++)
    {
        unsigned char ch = (unsigned char)s[i];
        if(isalnum(ch) || ch >= 0x80) c++;
    }
    return c;
}

static int tt_write_tmp(const unsigned char *buf, size_t len, char *path, size_t pathlen)
{
    const char *d = getenv("TMPDIR");
    int fd;
    if(!d || !*d) d = "/tmp";
    snprintf(path, pathlen, "%s/_rp_totext_XXXXXX", d);
    fd = mkstemp(path);
    if(fd < 0) return -1;
    if(write(fd, buf, len) != (ssize_t)len) { close(fd); unlink(path); return -1; }
    close(fd);
    return 0;
}

/* One PDF page to a grayscale PGM in memory (pdftoppm writes to stdout when
   given no output prefix). */
static const char pdf_raster_js[] =
    "(function(pdftoppm, file, page) {"
    "  var res = rampart.utils.exec(pdftoppm, '-r', '" TT_OCR_DPI "', '-gray',"
    "                               '-f', page, '-l', page, file, {returnBuffer:true});"
    "  if(res.exitStatus)"
    "    throw new Error('pdftoppm failed on page ' + page + ': ' + rampart.utils.bufferToString(res.stderr||''));"
    "  return res.stdout;"
    "})";

/* How many raster images a PDF contains. */
static const char pdf_images_js[] =
    "(function(pdfimages, file) {"
    "  var res = rampart.utils.exec(pdfimages, '-list', file);"
    "  if(res.exitStatus) return -1;"
    "  var n = 0, lines = res.stdout.split('\\n');"
    "  for(var i = 2; i < lines.length; i++)"
    "    if(/^\\s*\\d+\\s+\\d+\\s+image\\b/.test(lines[i])) n++;"
    "  return n;"
    "})";

static void tt_call_js2(duk_context *ctx, const char *js, const char *tool,
                        const char *file, int page)
{
    tt_push_fn(ctx, js, "pdf");
    duk_push_string(ctx, tool);
    duk_push_string(ctx, file);
    duk_push_int(ctx, page);
    if(duk_pcall(ctx, 3) != 0)
        RP_THROW(ctx, "convert pdf: %s", duk_safe_to_string(ctx, -1));
}

/* Runs after pdftotext, whose output is on top of the stack.  pdftotext ends
   every page with \f.  With a reader: pages with no usable text layer (or
   all pages, mode "always") are rasterized and read, and the output keeps
   pdftotext's shape.  Without one: a PDF holding images but no text at all
   is a scan that cannot be converted, and says so. */
static void tt_ocr_after_pdf(duk_context *ctx, tt_ocr *oc,
                             const unsigned char *buf, size_t len, const char *filename)
{
    duk_idx_t txt = duk_get_top_index(ctx);
    duk_size_t tl;
    const char *text = duk_get_lstring(ctx, txt, &tl);
    int have = oc->reader >= 0 || oc->lazy;
    size_t i, start = 0;
    int page = 0, ocred = 0, have_tmp = 0;
    char tmp[PATH_MAX];
    const char *file = filename;
    struct stat st;
    tt_buf out = {0};

    if(!text) return;

    if(!have)
    {
        if(tt_wordchars(text, tl) == 0)
        {
            int nimg = -1;
            char itool[PATH_MAX];
            if(!file || stat(file, &st) != 0)
            {
                if(tt_write_tmp(buf, len, tmp, sizeof tmp) != 0)
                    RP_THROW(ctx, "convert pdf: could not create a temp file");
                file = tmp; have_tmp = 1;
            }
            if(tt_find_tool("pdfimages", itool, sizeof itool))
            {
                tt_call_js2(ctx, pdf_images_js, itool, file, 0);
                nimg = duk_get_int(ctx, -1);
                duk_pop(ctx);
            }
            if(have_tmp) unlink(tmp);
            if(nimg != 0)
                RP_THROW(ctx, "convert pdf: no text layer%s -- this looks like a scanned document "
                              "and needs an OCR reader -- " TT_OCR_HINT,
                         nimg > 0 ? " (images only)" : "");
        }
        return;
    }

    for(i = 0; i <= tl; i++)
    {
        size_t plen;
        if(i < tl && text[i] != '\f') continue;
        plen = i - start;
        if(i == tl && plen == 0) break;        /* nothing after the last \f */

        if(oc->always || tt_wordchars(text + start, plen) < TT_OCR_MIN_WORDCHARS)
        {
            duk_idx_t input;
            duk_size_t ol;
            const char *o;
            int np;
            char rtool[PATH_MAX];

            if(!file || stat(file, &st) != 0)
            {
                if(tt_write_tmp(buf, len, tmp, sizeof tmp) != 0)
                    RP_THROW(ctx, "convert pdf: could not create a temp file");
                file = tmp; have_tmp = 1;
            }
            if(!tt_find_tool("pdftoppm", rtool, sizeof rtool))
                RP_THROW(ctx, "convert pdf: pdftoppm not found (needed to rasterize "
                              "a PDF page for OCR) -- " TT_PDF_HINT);
            tt_ocr_reader(ctx, oc, "pdf");
            tt_call_js2(ctx, pdf_raster_js, rtool, file, page + 1);
            input = duk_get_top_index(ctx);
            tt_ocr_read(ctx, oc, input, 0, page, &np);
            o = duk_get_lstring(ctx, -1, &ol);
            tt_buf_add(&out, o, ol);
            duk_pop(ctx);
            duk_remove(ctx, input);
            ocred = 1;
        }
        else
            tt_buf_add(&out, text + start, plen);
        tt_buf_add(&out, "\f", 1);
        start = i + 1;
        page++;
    }
    if(have_tmp) unlink(tmp);

    if(ocred)
    {
        duk_push_lstring(ctx, out.s ? out.s : "", (duk_size_t)out.len);
        duk_replace(ctx, txt);
    }
    /* a reader built on demand above was pushed after txt: drop it so the text
     * is on top, which is where push_details looks for it */
    duk_set_top(ctx, txt + 1);
    free(out.s);
}

/* setOcr(reader | true | opts | false) */
static duk_ret_t rp_set_ocr(duk_context *ctx)
{
    duk_push_this(ctx);
    if(!duk_is_object(ctx, -1))
        RP_THROW(ctx, "setOcr: must be called as a method of the rampart-totext module");
    duk_del_prop_string(ctx, -1, TT_OCR_READER_KEY);
    duk_del_prop_string(ctx, -1, TT_OCR_OPTS_KEY);

    if(is_ocr_reader(ctx, 0))
    {
        duk_dup(ctx, 0);
        duk_put_prop_string(ctx, -2, TT_OCR_READER_KEY);
    }
    else if(duk_is_boolean(ctx, 0))
    {
        if(duk_get_boolean(ctx, 0))
        {
            duk_push_object(ctx);
            duk_put_prop_string(ctx, -2, TT_OCR_OPTS_KEY);
        }
    }
    else if(duk_is_object(ctx, 0) && !duk_is_null(ctx, 0) && !duk_is_function(ctx, 0))
    {
        duk_dup(ctx, 0);
        duk_put_prop_string(ctx, -2, TT_OCR_OPTS_KEY);
    }
    else if(!duk_is_undefined(ctx, 0) && !duk_is_null(ctx, 0))
        RP_THROW(ctx, "setOcr: argument must be a rampart-ocr reader, an options object for rampart-ocr.init(), true, or false");
    return 0;
}

/* ---------------------------------------------------------------------------
   LARGE-ATTACHMENT ARENA

   A big attachment decoded into the heap is ANONYMOUS memory: with no swap it
   cannot be reclaimed, so a 400 MB base64 PDF is an OOM kill rather than a
   slow conversion.  The same bytes in a file-backed MAP_SHARED mapping ARE
   reclaimable -- the kernel writes them back and evicts them.  That is the
   whole difference, and it is the reason the mmap'd input survives a 2.1 GB
   mbox under MemoryMax=256M while a heap copy of it does not.

   So a part whose DECODED SIZE exceeds TT_ARENA_MIN decodes into an unlinked
   temp file mapped MAP_SHARED, and everything smaller stays on the heap
   exactly as before.

   The arena is created ONCE PER CALL and reused for every large part, which
   is what makes it free.  Measured, per part of a given size:

       part      malloc/part   one heap arena   one mmap arena
        4 KB       0.2 us          0.1 us           0.2 us
       64 KB       3.2 us          3.2 us           3.3 us
        1 MB      80.2 us         78.5 us          80.7 us
       16 MB    2609.2 us       2450.0 us        2679.6 us

   A FRESH temp file per part instead costs 136 us (4 KB) to 2180 us (1 MB) in
   mkstemp/ftruncate/munmap alone -- ~40x the work of the decode it wraps.
   Reuse is not an optimisation here, it is the difference between this being
   free and being the slowest thing in the module.

   Sizing comes from the part's ENCODED byte span, which MIME gives us for
   free (parts are boundary-delimited, so the parser has already found the
   end) and which is an upper bound on the decoded length for every encoding.
   Header sizes are not used: RFC 2183 `size=' is advisory and appears on
   under 4% of real parts.  The file is sparse, so an overestimate -- the
   wild one you get from a truncated message with no closing boundary --
   costs address space and nothing else: pages materialise only as written.

   The cost of all this is writeback.  A long-lived dirty shared mapping gets
   flushed by the kernel (dirty_expire_centisecs), so bytes pushed through the
   arena do reach the disk asynchronously.  Rewriting a 64 MB arena flat out
   for 40 s measured 4065 MB/s against the heap's 5747 MB/s.  Two obvious
   ways to avoid it are both far worse on ext4, because each forces a range
   flush BEFORE discarding:

       leave it alone                        4385 MB/s     1280 MB written
       FALLOC_FL_PUNCH_HOLE between parts     131 MB/s     5248 MB written
       ftruncate(0) + regrow between parts    420 MB/s    16832 MB written

   So it is left alone.  Writeback is proportional to large-attachment volume,
   which is data we are converting anyway, and it is asynchronous. */

#define TT_ARENA_MIN   (4u * 1024u * 1024u)   /* decoded size worth a temp file */
#define TT_ARENA_CHUNK (1u * 1024u * 1024u)   /* encoded bytes decoded per pass */

typedef struct {
    int    fd;        /* unlinked temp file; -1 = not opened yet */
    char  *base;      /* the mapping, or NULL */
    size_t mapped;    /* bytes currently mapped */
    int    failed;    /* tried and could not: stay on the heap, never retry */
} tt_arena;

/* tmpfs IS memory, so spilling to it defeats the entire point.  Fall through
   to a real filesystem, and if there is none, decline the arena rather than
   pretend. */
static int tt_dir_is_ram(const char *d)
{
#ifdef __linux__
    struct statfs s;
    if(statfs(d, &s) == 0 && (unsigned long)s.f_type == 0x01021994UL) return 1;
#else
    (void)d;
#endif
    return 0;
}

static const char *tt_spill_dir(void)
{
    const char *e = getenv("TMPDIR");

    if(e && *e && !tt_dir_is_ram(e))  return e;
    if(!tt_dir_is_ram("/tmp"))        return "/tmp";
    if(!tt_dir_is_ram("/var/tmp"))    return "/var/tmp";
    return NULL;
}

/* A writable mapping of at least need+1 bytes, or NULL to use the heap.
   Grows to the call's high-water mark and never shrinks, so a second large
   part costs no syscalls at all.  Growth is lossless: the bytes live in the
   file, not the mapping, so ftruncate+remap preserves what is already there. */
static char *tt_arena_get(tt_arena *a, size_t need)
{
    long   pagesz = sysconf(_SC_PAGESIZE);
    size_t want;
    void  *p;

    if(a->failed || pagesz <= 0) return NULL;

    /* +1 for the readable NUL at [len] that tt_map_file() also guarantees */
    want = need + 1;
    want = ((want + (size_t)pagesz - 1) / (size_t)pagesz) * (size_t)pagesz;

    if(a->base && a->mapped >= want) return a->base;    /* reuse: no syscalls */

    if(a->fd < 0)
    {
        const char *d = tt_spill_dir();
        char path[PATH_MAX];

        if(!d) { a->failed = 1; return NULL; }
        snprintf(path, sizeof path, "%s/_rp_totext_att_XXXXXX", d);
        a->fd = mkstemp(path);
        if(a->fd < 0) { a->failed = 1; return NULL; }
        unlink(path);      /* anonymous: nothing left behind, even on a crash */
    }

    if(a->base) { munmap(a->base, a->mapped); a->base = NULL; a->mapped = 0; }

    if(ftruncate(a->fd, (off_t)want) != 0) { a->failed = 1; return NULL; }

    p = mmap(NULL, want, PROT_READ | PROT_WRITE, MAP_SHARED, a->fd, 0);
    if(p == MAP_FAILED) { a->failed = 1; return NULL; }

    a->base   = (char *)p;
    a->mapped = want;
    return a->base;
}

static void tt_arena_release(tt_arena *a)
{
    if(a->base)   munmap(a->base, a->mapped);
    if(a->fd >= 0) close(a->fd);
    a->base = NULL;
    a->mapped = 0;
    a->fd = -1;
}

#define TT_ARENA_BUF "\xff" "arenabuf"

static duk_ret_t tt_arena_finalizer(duk_context *ctx)
{
    tt_arena *a = NULL;

    if(duk_get_prop_string(ctx, 0, TT_ARENA_BUF))
        a = (tt_arena *)duk_get_buffer_data(ctx, -1, NULL);
    if(a) tt_arena_release(a);
    duk_pop(ctx);
    return 0;
}

/* The arena outlives every converter call beneath it and must be released
   even when one of them throws -- the same reason tt_map_file() hands its
   mapping to a stack object instead of holding it in C.  Leaves the holder
   on the value stack; the returned pointer is stable for its lifetime. */
static tt_arena *tt_arena_push(duk_context *ctx)
{
    tt_arena *a;

    duk_push_object(ctx);
    a = (tt_arena *)duk_push_fixed_buffer(ctx, (duk_size_t)sizeof(tt_arena));
    a->fd = -1;
    duk_put_prop_string(ctx, -2, TT_ARENA_BUF);
    duk_push_c_function(ctx, tt_arena_finalizer, 1);
    duk_set_finalizer(ctx, -2);
    return a;
}

/* ================================================================
   EMAIL: RFC 5322 / MIME, mbox, MHTML
   ================================================================

   Parsing is the vendored libetpan subset (extern/libetpan); everything
   here is policy, which no library can supply: which part of a
   multipart/alternative to keep, what counts as an attachment, and how the
   pieces become documents[] entries.

   An attachment's bytes go back through identify_content() and
   do_convert(), so a PDF inside a message is converted by the same code
   that converts a PDF on disk -- and its declared Content-Type is ignored
   in favour of sniffing the decoded bytes, because mailers label
   everything application/octet-stream. */

typedef struct {
    int       prefer_html;  /* multipart/alternative: take text/html not text/plain */
    int       attachments;  /* convert attachments at all */
    size_t    max_att;      /* caller's decoded ceiling; unlimited by default */
    int       max_depth;    /* nested multipart / message-in-message */
    tt_arena *arena;        /* large parts decode here instead of the heap */
} tt_email_opts;

/* do_convert() is below; an attachment's bytes go back through it */
static void do_convert(const unsigned char *buf, size_t len,
                       filetype_t ft, duk_context *ctx,
                       const char *filename, const char **charset_out,
                       const char **charset_src_out, tt_ocr *oc);

typedef struct {
    const unsigned char *buf;
    size_t               len;
    filetype_t           ft;
    tt_ocr              *oc;
} tt_att_call;

/* do_convert() inside duk_safe_call(): leaves the text on the stack */
static duk_ret_t tt_att_safe(duk_context *ctx, void *udata)
{
    tt_att_call *a = (tt_att_call *)udata;
    const char *cs = NULL, *cs_src = NULL;

    do_convert(a->buf, a->len, a->ft, ctx, NULL, &cs, &cs_src, a->oc);
    return 1;
}

#define TT_EMAIL_MAX_DEPTH  24

/* maxAttachment defaults to UNLIMITED.
 *
 * It used to default to 32 MB, which meant a 40 MB PDF attachment was
 * silently dropped -- the text simply was not there, with nothing to say so.
 * That is content loss decided by an arbitrary number, the same fault as the
 * document-count cap below.  A size limit existed because a large attachment
 * had to be held in anonymous memory; the arena removes that reason, so the
 * limit goes with it.
 *
 * The option remains for a caller who genuinely wants to skip big
 * attachments to save time.  That is their decision to make explicitly. */
#define TT_EMAIL_MAX_ATT    ((size_t)-1)
/* There is deliberately NO limit on the number of documents.
 *
 * There was one -- 20,000, and only applied when building the array -- and
 * it silently truncated a perfectly ordinary 28,365-message mailbox in one
 * mode while the other returned it whole.  Raising the number would only
 * have moved the arbitrary cutoff.
 *
 * Nothing needs it.  What actually has to be bounded is bounded: nesting by
 * max_depth, a single part by max_att, and the input by the size of the
 * file.  Streaming builds one document, hands it over and releases it, so N
 * documents cost O(1) memory -- the whole point of the callback -- and the
 * mbox scan always advances (i = eol + 1), so it terminates.  In array mode
 * the caller has explicitly asked for every document at once, and the cost
 * is proportional to what is actually in the file.
 *
 * A count limit would only ever lose data that the caller asked for. */

/* Not a signature -- bytes only one format produces -- but a guess, so this
   sits BELOW the extension guard with the markdown and LaTeX probes.  A
   header block alone is not enough: prose starts "Subject: quarterly review"
   often enough.  One header that never appears in prose is required. */
static int has_email_signature(const unsigned char *buf, size_t len)
{
    static const char *hard[] = { "received", "message-id", "return-path",
                                  "mime-version", "delivered-to", NULL };
    size_t i = 0, scan = len < 65536 ? len : 65536;
    int fields = 0, strong = 0, sawblank = 0, h;

    while(i < scan)
    {
        size_t n = i, c;

        if(buf[i] == '\r' || buf[i] == '\n') { sawblank = 1; break; }
        if(buf[i] == ' ' || buf[i] == '\t')          /* folded continuation */
        {
            while(n < scan && buf[n] != '\n') n++;
            i = n + 1;
            continue;
        }

        for(c = i; c < scan && buf[c] != ':' && buf[c] != '\n'; c++)
            if(!isalnum((unsigned char)buf[c]) && buf[c] != '-' && buf[c] != '_')
                return 0;                            /* not a header line */
        if(c >= scan || buf[c] != ':' || c == i)
            return 0;

        for(h = 0; hard[h]; h++)
            if(c - i == strlen(hard[h]) && !strncasecmp((const char *)buf + i, hard[h], c - i))
                strong = 1;
        fields++;

        while(c < scan && buf[c] != '\n') c++;
        i = c + 1;
    }

    return sawblank && strong && fields >= 2;
}

/* An mbox starts with a "From " line -- no colon -- and the message's own
   headers follow it. */
static int has_mbox_signature(const unsigned char *buf, size_t len)
{
    size_t i = 0;

    if(len < 6 || memcmp(buf, "From ", 5)) return 0;
    while(i < len && buf[i] != '\n') i++;
    if(i + 1 >= len) return 0;
    return has_email_signature(buf + i + 1, len - i - 1);
}

/* "text/plain" and friends, lowercased into out */
static void tt_ct_string(struct mailmime_content *c, char *out, size_t outsz)
{
    const char *t = "application", *s = "octet-stream";

    if(c && c->ct_type)
    {
        if(c->ct_type->tp_type == MAILMIME_TYPE_DISCRETE_TYPE)
            switch(c->ct_type->tp_data.tp_discrete_type->dt_type)
            {
                case MAILMIME_DISCRETE_TYPE_TEXT:        t = "text";        break;
                case MAILMIME_DISCRETE_TYPE_IMAGE:       t = "image";       break;
                case MAILMIME_DISCRETE_TYPE_AUDIO:       t = "audio";       break;
                case MAILMIME_DISCRETE_TYPE_VIDEO:       t = "video";       break;
                case MAILMIME_DISCRETE_TYPE_APPLICATION: t = "application"; break;
                default:
                    t = c->ct_type->tp_data.tp_discrete_type->dt_extension ?
                        c->ct_type->tp_data.tp_discrete_type->dt_extension : "application";
            }
        else
            switch(c->ct_type->tp_data.tp_composite_type->ct_type)
            {
                case MAILMIME_COMPOSITE_TYPE_MESSAGE:   t = "message";   break;
                case MAILMIME_COMPOSITE_TYPE_MULTIPART: t = "multipart"; break;
                default: t = "multipart";
            }
    }
    if(c && c->ct_subtype) s = c->ct_subtype;
    snprintf(out, outsz, "%s/%s", t, s);
    for(; *out; out++) *out = (char)tolower((unsigned char)*out);
}

/* RFC 2047 decode a header value; always leaves a string on the stack */
static void tt_hdr_push(duk_context *ctx, const char *v)
{
    size_t idx = 0;
    char *dec = NULL;

    if(!v) { duk_push_string(ctx, ""); return; }
    if(mailmime_encoded_phrase_parse("utf-8", v, strlen(v), &idx, "utf-8", &dec)
       == MAILIMF_NO_ERROR && dec)
    {
        duk_push_string(ctx, dec);
        free(dec);
        return;
    }
    duk_push_string(ctx, v);
}

static void tt_addr_append(rp_string *s, struct mailimf_mailbox *mb)
{
    if(!mb) return;
    if(s->len) rp_string_puts(s, ", ");
    if(mb->mb_display_name && *mb->mb_display_name)
    {
        rp_string_puts(s, mb->mb_display_name);
        if(mb->mb_addr_spec)
        {
            rp_string_puts(s, " <");
            rp_string_puts(s, mb->mb_addr_spec);
            rp_string_putc(s, '>');
        }
    }
    else if(mb->mb_addr_spec)
        rp_string_puts(s, mb->mb_addr_spec);
}

static void tt_mblist_set(duk_context *ctx, duk_idx_t obj, const char *key,
                          clist *mblist)
{
    rp_string *s = rp_string_new(128);
    clistiter *cur;

    for(cur = mblist ? clist_begin(mblist) : NULL; cur; cur = clist_next(cur))
        tt_addr_append(s, clist_content(cur));
    if(s->len)
    {
        tt_hdr_push(ctx, s->str);
        duk_put_prop_string(ctx, obj, key);
    }
    rp_string_free(s);
}

static void tt_adlist_set(duk_context *ctx, duk_idx_t obj, const char *key,
                          clist *adlist)
{
    rp_string *s = rp_string_new(128);
    clistiter *cur;

    for(cur = adlist ? clist_begin(adlist) : NULL; cur; cur = clist_next(cur))
    {
        struct mailimf_address *a = clist_content(cur);
        if(!a) continue;
        if(a->ad_type == MAILIMF_ADDRESS_MAILBOX)
            tt_addr_append(s, a->ad_data.ad_mailbox);
        else if(a->ad_data.ad_group && a->ad_data.ad_group->grp_mb_list)
        {
            clistiter *g;
            for(g = clist_begin(a->ad_data.ad_group->grp_mb_list->mb_list); g;
                g = clist_next(g))
                tt_addr_append(s, clist_content(g));
        }
    }
    if(s->len)
    {
        tt_hdr_push(ctx, s->str);
        duk_put_prop_string(ctx, obj, key);
    }
    rp_string_free(s);
}

/* the message's headers, in the module's common metaData schema */
static void tt_email_push_meta(duk_context *ctx, struct mailimf_fields *fields)
{
    duk_idx_t obj;
    clistiter *cur;

    duk_push_object(ctx);
    obj = duk_get_top_index(ctx);
    if(!fields) return;

    for(cur = clist_begin(fields->fld_list); cur; cur = clist_next(cur))
    {
        struct mailimf_field *f = clist_content(cur);
        if(!f) continue;

        switch(f->fld_type)
        {
            case MAILIMF_FIELD_SUBJECT:
                if(f->fld_data.fld_subject)
                {
                    tt_hdr_push(ctx, f->fld_data.fld_subject->sbj_value);
                    duk_put_prop_string(ctx, obj, "subject");
                }
                break;
            case MAILIMF_FIELD_FROM:
                if(f->fld_data.fld_from && f->fld_data.fld_from->frm_mb_list)
                    tt_mblist_set(ctx, obj, "from",
                                  f->fld_data.fld_from->frm_mb_list->mb_list);
                break;
            case MAILIMF_FIELD_TO:
                if(f->fld_data.fld_to && f->fld_data.fld_to->to_addr_list)
                    tt_adlist_set(ctx, obj, "to",
                                  f->fld_data.fld_to->to_addr_list->ad_list);
                break;
            case MAILIMF_FIELD_CC:
                if(f->fld_data.fld_cc && f->fld_data.fld_cc->cc_addr_list)
                    tt_adlist_set(ctx, obj, "cc",
                                  f->fld_data.fld_cc->cc_addr_list->ad_list);
                break;
            case MAILIMF_FIELD_ORIG_DATE:
                if(f->fld_data.fld_orig_date && f->fld_data.fld_orig_date->dt_date_time)
                {
                    struct mailimf_date_time *d = f->fld_data.fld_orig_date->dt_date_time;
                    char iso[40];
                    snprintf(iso, sizeof iso, "%04d-%02d-%02dT%02d:%02d:%02d",
                             d->dt_year, d->dt_month, d->dt_day,
                             d->dt_hour, d->dt_min, d->dt_sec);
                    duk_push_string(ctx, iso);
                    duk_put_prop_string(ctx, obj, "date");
                }
                break;
            case MAILIMF_FIELD_MESSAGE_ID:
                if(f->fld_data.fld_message_id && f->fld_data.fld_message_id->mid_value)
                {
                    duk_push_string(ctx, f->fld_data.fld_message_id->mid_value);
                    duk_put_prop_string(ctx, obj, "messageId");
                }
                break;
            default:
                break;
        }
    }
}

/* an attachment's filename: Content-Disposition first, then the Content-Type
   "name" parameter, both RFC 2047 decoded */
static char *tt_part_filename(struct mailmime *mime)
{
    clistiter *cur;

    if(mime->mm_mime_fields)
        for(cur = clist_begin(mime->mm_mime_fields->fld_list); cur; cur = clist_next(cur))
        {
            struct mailmime_field *f = clist_content(cur);
            clistiter *p;

            if(!f || f->fld_type != MAILMIME_FIELD_DISPOSITION) continue;
            if(!f->fld_data.fld_disposition) continue;
            for(p = clist_begin(f->fld_data.fld_disposition->dsp_parms); p;
                p = clist_next(p))
            {
                struct mailmime_disposition_parm *dp = clist_content(p);
                if(dp && dp->pa_type == MAILMIME_DISPOSITION_PARM_FILENAME)
                    return dp->pa_data.pa_filename;
            }
        }

    return mime->mm_content_type
         ? mailmime_content_param_get(mime->mm_content_type, "name") : NULL;
}

static int tt_part_encoding(struct mailmime *mime)
{
    clistiter *cur;

    if(mime->mm_mime_fields)
        for(cur = clist_begin(mime->mm_mime_fields->fld_list); cur; cur = clist_next(cur))
        {
            struct mailmime_field *f = clist_content(cur);
            if(f && f->fld_type == MAILMIME_FIELD_TRANSFER_ENCODING
               && f->fld_data.fld_encoding)
                return f->fld_data.fld_encoding->enc_type;
        }
    return MAILMIME_MECHANISM_8BIT;
}

/* How big a part will be once decoded, from its ENCODED length -- which is
   known before any memory is spent.  base64 is 4 bytes in, 3 out; the rest
   never grow. */
static size_t tt_decoded_estimate(size_t rawlen, int enc)
{
    if(enc == MAILMIME_MECHANISM_BASE64) return rawlen / 4 * 3 + 3;
    return rawlen;
}

/* The decoded bytes of one part: either an libetpan MMAPString or a window
   into the arena, which is freed with the arena rather than per part. */
typedef struct { char *p; size_t len; int in_arena; } tt_dec;

static void tt_dec_free(tt_dec *d)
{
    if(d->p && !d->in_arena) mmap_string_unref(d->p);
    d->p   = NULL;
    d->len = 0;
}

/* Decode one part.  Small parts go to the heap in a single call, exactly as
   before.  A part above TT_ARENA_MIN is decoded in chunks straight into the
   arena, so the peak ANONYMOUS cost is one chunk however big the attachment
   is -- libetpan still allocates each chunk, but only a chunk at a time.

   The loop is mailmime_part_parse_partial()'s contract: given a window it
   decodes up to the last COMPLETE token inside it and reports where it
   stopped, so a base64 quad or a quoted-printable "=XX" straddling the window
   edge is resumed rather than corrupted.  The final window must use the
   non-partial call, which flushes a trailing partial token.

   Falls back to the heap whenever the arena is unavailable (no non-tmpfs
   temp directory, mkstemp failed, ...): slower and riskier under a memory
   cap, but never wrong. */
static int tt_part_decode(tt_arena *ar, const char *raw, size_t rawlen,
                          int enc, size_t est, tt_dec *out)
{
    size_t idx = 0, got = 0, win = TT_ARENA_CHUNK;
    char  *dst;

    out->p = NULL;
    out->len = 0;
    out->in_arena = 0;

    if(est <= TT_ARENA_MIN || !ar || !(dst = tt_arena_get(ar, est)))
    {
        if(mailmime_part_parse(raw, rawlen, &idx, enc, &out->p, &out->len)
           != MAILIMF_NO_ERROR || !out->p)
            return -1;
        return 0;
    }

    while(idx < rawlen)
    {
        size_t end = idx + win, before = idx, clen = 0;
        char  *cp = NULL;
        int    r, last = 0;

        if(end >= rawlen) { end = rawlen; last = 1; }

        r = last ? mailmime_part_parse(raw, end, &idx, enc, &cp, &clen)
                 : mailmime_part_parse_partial(raw, end, &idx, enc, &cp, &clen);

        if(r != MAILIMF_NO_ERROR || !cp) return -1;

        if(clen)
        {
            /* est is an upper bound for every encoding, so this should not
               fire -- but grow rather than truncate if it ever does.  The
               bytes already written are in the FILE, so remapping keeps
               them. */
            if(got + clen + 1 > ar->mapped)
            {
                char *bigger = tt_arena_get(ar, got + clen);
                if(!bigger) { mmap_string_unref(cp); return -1; }
                dst = bigger;
            }
            memcpy(dst + got, cp, clen);
            got += clen;
        }
        mmap_string_unref(cp);

        if(last) break;

        /* No complete token in the window -- a run of line breaks or padding
           longer than the chunk.  Breaking here would silently truncate the
           part, so widen the window instead.  It only ever grows, and `end`
           is clamped to rawlen, so this terminates at the final window. */
        if(idx <= before) win += TT_ARENA_CHUNK;
        else              win  = TT_ARENA_CHUNK;
    }

    dst[got] = 0;                          /* the NUL converters expect */
    out->p        = dst;
    out->len      = got;
    out->in_arena = 1;
    return 0;
}

#define TT_SNIFF_RAW   8192   /* encoded bytes fed to the prefix decoder */
#define TT_SNIFF_BYTES 1024   /* below this, sniffing costs more than decoding */

/* A cheap NO from the declared type and the filename, before anything is
 * decoded.  This is only ever used to RULE OUT: mailers label real documents
 * application/octet-stream, so a yes here means "sniff it", never "convert
 * it blindly".  These media types are the ones that are never mislabelled in
 * the direction that matters -- nobody sends a PDF as video/mp4. */
static int tt_type_convertible(const char *ct, const char *fname)
{
    static const char *never[] = {
        "video/", "audio/", "model/", "font/",
        "application/zip", "application/gzip", "application/x-tar",
        "application/x-bzip", "application/x-7z", "application/x-rar",
        "application/octet-stream+encrypted", "application/pgp-encrypted",
        "application/pkcs7-mime", "application/pkcs7-signature",
        NULL
    };
    static const char *never_ext[] = {
        ".mp4", ".m4v", ".mov", ".avi", ".mkv", ".webm", ".mp3", ".m4a",
        ".aac", ".flac", ".wav", ".ogg", ".zip", ".gz", ".bz2", ".xz",
        ".7z", ".rar", ".exe", ".dll", ".dmg", ".iso", ".ttf", ".otf",
        ".woff", ".woff2", ".p7s", ".p7m", ".asc", ".sig",
        NULL
    };
    int i;

    for(i = 0; never[i]; i++)
        if(!strncasecmp(ct, never[i], strlen(never[i]))) return 0;

    if(fname)
    {
        const char *dot = strrchr(fname, '.');
        if(dot)
            for(i = 0; never_ext[i]; i++)
                if(!strcasecmp(dot, never_ext[i])) return 0;
    }
    return 1;
}

/* Having sniffed the bytes, is this a type we would get text out of?  An
 * image is only worth decoding when a reader has been set -- otherwise it
 * would decode megabytes to produce the "needs an OCR reader" error. */
static int tt_filetype_convertible(filetype_t ft, tt_ocr *oc)
{
    switch(ft)
    {
        case FT_PNG: case FT_JPEG: case FT_TIFF: case FT_GIF:
        case FT_BMP: case FT_PNM: case FT_PSD:  case FT_HDR:
            return (oc->reader >= 0 || oc->lazy);
        case FT_UNKNOWN:
            /* binary with no signature: extract_text_chunks() would scan the
               whole thing for stray ASCII, which is rarely worth the copy */
            return 0;
        default:
            return 1;
    }
}

/* text on top of the stack becomes a new documents[] entry */
/* Hand one finished document to the callback and release it.  Returns 0 when
   the callback asked to stop.  The document object is consumed either way, so
   nothing accumulates across the call. */
static int tt_deliver(duk_context *ctx, tt_ocr *oc)
{
    int keep = 1;

    duk_dup(ctx, oc->cb);
    duk_insert(ctx, -2);                    /* [ cb, doc ] */
    if(duk_pcall(ctx, 1) != 0)
        (void) duk_throw(ctx);              /* a throwing callback is the caller's */

    if(duk_is_boolean(ctx, -1) && !duk_get_boolean(ctx, -1))
        keep = 0;
    duk_pop(ctx);

    oc->ndocs++;
    if(!keep) oc->aborted = 1;
    return keep;
}

static void tt_doc_push(duk_context *ctx, duk_idx_t docs, const char *mime,
                        const char *title, duk_idx_t meta,
                        int did_ocr, duk_idx_t pages, tt_ocr *oc)
{
    duk_idx_t doc;

    duk_push_object(ctx);
    duk_insert(ctx, -2);                       /* [obj, text] */
    doc = duk_get_top_index(ctx) - 1;
    duk_put_prop_string(ctx, doc, "text");

    if(mime)
    {
        duk_push_string(ctx, mime);
        duk_put_prop_string(ctx, doc, "mimeType");
    }
    /* title precedence, per the spec: an attachment's filename, else the
       message's Subject -- which is what a body part gets. */
    if(title && *title)
    {
        tt_hdr_push(ctx, title);
        duk_put_prop_string(ctx, doc, "title");
    }
    else if(meta >= 0)
    {
        duk_get_prop_string(ctx, meta, "subject");
        if(duk_is_string(ctx, -1)) duk_put_prop_string(ctx, doc, "title");
        else                       duk_pop(ctx);
    }
    if(meta >= 0) duk_dup(ctx, meta);
    else          duk_push_object(ctx);
    duk_put_prop_string(ctx, doc, "metaData");

    duk_push_boolean(ctx, did_ocr);
    duk_put_prop_string(ctx, doc, "ocr");
    if(did_ocr && pages >= 0)
    {
        duk_dup(ctx, pages);
        duk_put_prop_string(ctx, doc, "pages");
    }

    if(docs < 0)
    {
        tt_deliver(ctx, oc);                /* streaming: out and gone */
    }
    else
    {
        duk_put_prop_index(ctx, docs, (duk_uarridx_t)duk_get_length(ctx, docs));
        oc->ndocs++;
    }
}

static void tt_mime_walk(duk_context *ctx, struct mailmime *mime, duk_idx_t docs,
                         tt_ocr *oc, tt_email_opts *opt, duk_idx_t msg_meta,
                         int depth);

/* One leaf part. */
static void tt_mime_leaf(duk_context *ctx, struct mailmime *mime, duk_idx_t docs,
                         tt_ocr *oc, tt_email_opts *opt, duk_idx_t msg_meta)
{
    char ct[128];
    const char *raw;
    size_t rawlen, est;
    tt_dec d;
    char *dec;
    size_t declen;
    char *fname = tt_part_filename(mime);
    int enc = tt_part_encoding(mime);

    tt_ct_string(mime->mm_content_type, ct, sizeof ct);

    if(!mime->mm_data.mm_single)
    {
        duk_push_string(ctx, "");
        tt_doc_push(ctx, docs, ct, fname, msg_meta, 0, -1, oc);
        return;
    }

    raw    = mime->mm_data.mm_single->dt_data.dt_text.dt_data;
    rawlen = mime->mm_data.mm_single->dt_data.dt_text.dt_length;

    if(!raw)
    {
        duk_push_string(ctx, "");
        tt_doc_push(ctx, docs, ct, fname, msg_meta, 0, -1, oc);
        return;
    }

    /* DECIDE BEFORE DECODING.
     *
     * Everything below this point had the whole part decoded into memory
     * first and the decision taken afterwards -- so a 400 MB base64 video
     * was fully materialised and then discarded for exceeding maxAttachment,
     * which meant the cap bounded nothing at all.  A part we are not going
     * to convert must never be copied into memory.
     *
     * Body parts, the "text" media type, are always wanted and skip all this. */
    est = tt_decoded_estimate(rawlen, enc);

    if(strncmp(ct, "text/", 5))
    {
        if(!opt->attachments || est > opt->max_att || !tt_type_convertible(ct, fname))
        {
            duk_push_string(ctx, "");
            tt_doc_push(ctx, docs, ct, fname, msg_meta, 0, -1, oc);
            return;
        }

        /* The declared type is not trusted -- mailers label everything
         * application/octet-stream -- so anything not ruled out above gets
         * sniffed: decode a prefix, identify from the bytes, and only then
         * decode the whole thing.  Every signature we have lives in the
         * first few hundred bytes. */
        if(est > TT_SNIFF_BYTES * 4)
        {
            size_t plen = rawlen < TT_SNIFF_RAW ? rawlen : TT_SNIFF_RAW;
            size_t pidx = 0, pdeclen = 0;
            char *pdec = NULL;

            if(mailmime_part_parse_partial(raw, plen, &pidx, enc, &pdec, &pdeclen)
               == MAILIMF_NO_ERROR && pdec)
            {
                filetype_t pft = identify_content((const unsigned char *)pdec, pdeclen,
                                                  fname ? fname : "");
                int keep = tt_filetype_convertible(pft, oc);
                mmap_string_unref(pdec);
                if(!keep)
                {
                    duk_push_string(ctx, "");
                    tt_doc_push(ctx, docs, ct, fname, msg_meta, 0, -1, oc);
                    return;
                }
            }
        }
    }

    if(tt_part_decode(opt->arena, raw, rawlen, enc, est, &d) != 0)
    {
        duk_push_string(ctx, "");
        tt_doc_push(ctx, docs, ct, fname, msg_meta, 0, -1, oc);
        return;
    }
    dec    = d.p;
    declen = d.len;

    /* text/plain and text/html are the message body: decode to UTF-8 with
       the part's declared charset, which is the whole reason MIME is nicer
       to work with than a bare file. */
    if(!strncmp(ct, "text/", 5))
    {
        char *cs = mime->mm_content_type
                 ? mailmime_content_charset_get(mime->mm_content_type) : NULL;
        rp_charset_result r;
        const char *err = NULL;
        const unsigned char *tb = (const unsigned char *)dec;
        size_t tl = declen;
        int converted = 0;

        if(rp_charset_to_utf8((const unsigned char *)dec, declen, cs, 0, &r, &err) == 0)
        {
            tb = (const unsigned char *)r.text;
            tl = r.len;
            converted = 1;
        }

        if(!strcmp(ct, "text/html"))
            call_js_converter(ctx, html_convert_js, "email html", tb, tl);
        else
        {
            rp_string *t = convert_text(tb, tl);
            duk_push_lstring(ctx, t->str, t->len);
            rp_string_free(t);
        }

        if(converted) rp_charset_result_free(&r);
        tt_dec_free(&d);
        tt_doc_push(ctx, docs, ct, fname, msg_meta, 0, -1, oc);
        return;
    }

    /* An attachment.  Its bytes go back through the ordinary machinery, and
       the DECLARED type is ignored: mailers label everything
       application/octet-stream, so sniff the bytes and use the filename only
       as the extension hint -- exactly the order used for files on disk.
       The attachments/maxAttachment/type decisions were all taken above,
       BEFORE the decode, so there is nothing left to check here. */
    {
        filetype_t aft = identify_content((const unsigned char *)dec, declen,
                                          fname ? fname : "");
        tt_att_call call;
        tt_ocr sub = *oc;
        duk_idx_t apages = -1;

        /* the attachment's own OCR state: per the spec its `ocr`/`pages`
           belong to ITS documents[] entry, and must not touch the parent's */
        sub.did_ocr = 0;
        sub.meta    = -1;
        sub.pages   = -1;
        sub.docs    = -1;
        if(oc->details)
        {
            duk_push_array(ctx);
            sub.pages = apages = duk_get_top_index(ctx);
        }

        call.buf = (const unsigned char *)dec;
        call.len = declen;
        call.ft  = aft;
        call.oc  = &sub;

        /* An attachment needing a converter we lack -- no pdftotext, no OCR
           reader -- must not take the whole message down with it.  The part
           is recorded with empty text and the rest of the mail converts. */
        if(duk_safe_call(ctx, tt_att_safe, &call, 0, 1) != DUK_EXEC_SUCCESS)
        {
            duk_pop(ctx);
            duk_push_string(ctx, "");
            sub.did_ocr = 0;
        }

        /* free BEFORE delivering: tt_doc_push() invokes the streaming
           callback, and a callback that throws would never come back here */
        tt_dec_free(&d);

        tt_doc_push(ctx, docs, filetype_mimes[aft], fname, msg_meta,
                    sub.did_ocr, apages, oc);
        if(apages >= 0) duk_remove(ctx, apages);
    }

    tt_dec_free(&d);
}

static void tt_mime_walk(duk_context *ctx, struct mailmime *mime, duk_idx_t docs,
                         tt_ocr *oc, tt_email_opts *opt, duk_idx_t msg_meta,
                         int depth)
{
    clistiter *cur;
    char ct[128];

    if(!mime || depth > opt->max_depth || oc->aborted) return;

    switch(mime->mm_type)
    {
        case MAILMIME_MESSAGE:
        {
            duk_idx_t meta = msg_meta;

            /* a nested message/rfc822 gets its own headers */
            if(mime->mm_data.mm_message.mm_fields)
            {
                tt_email_push_meta(ctx, mime->mm_data.mm_message.mm_fields);
                meta = duk_get_top_index(ctx);
            }
            tt_mime_walk(ctx, mime->mm_data.mm_message.mm_msg_mime, docs, oc, opt,
                         meta, depth + 1);
            break;
        }

        case MAILMIME_MULTIPLE:
        {
            tt_ct_string(mime->mm_content_type, ct, sizeof ct);

            /* RFC 2046 orders alternatives worst to best.  Taking both would
               index every word twice; take one. */
            if(!strcmp(ct, "multipart/alternative"))
            {
                struct mailmime *pick = NULL, *fallback = NULL;

                for(cur = clist_begin(mime->mm_data.mm_multipart.mm_mp_list); cur;
                    cur = clist_next(cur))
                {
                    struct mailmime *p = clist_content(cur);
                    char pct[128];
                    if(!p) continue;
                    fallback = p;                      /* last = richest */
                    tt_ct_string(p->mm_content_type, pct, sizeof pct);
                    if(!strcmp(pct, opt->prefer_html ? "text/html" : "text/plain"))
                        pick = p;
                }
                tt_mime_walk(ctx, pick ? pick : fallback, docs, oc, opt,
                             msg_meta, depth + 1);
                break;
            }

            /* signed: the first part is the content, the second the signature */
            if(!strcmp(ct, "multipart/signed"))
            {
                cur = clist_begin(mime->mm_data.mm_multipart.mm_mp_list);
                if(cur) tt_mime_walk(ctx, clist_content(cur), docs, oc, opt,
                                     msg_meta, depth + 1);
                break;
            }

            /* encrypted: the envelope is all we can read.  Record that the
               part existed rather than dumping base64 into the index. */
            if(!strcmp(ct, "multipart/encrypted"))
            {
                duk_push_string(ctx, "");
                tt_doc_push(ctx, docs, ct, NULL, msg_meta, 0, -1, oc);
                break;
            }

            for(cur = clist_begin(mime->mm_data.mm_multipart.mm_mp_list); cur;
                cur = clist_next(cur))
            {
                if(oc->aborted) break;
                tt_mime_walk(ctx, clist_content(cur), docs, oc, opt, msg_meta,
                             depth + 1);
            }
            break;
        }

        case MAILMIME_SINGLE:
            tt_mime_leaf(ctx, mime, docs, oc, opt, msg_meta);
            break;

        default:
            break;
    }
}

/* documents[].text joined with a single space -- built by construction, so
   the spec's `text === documents.map(d=>d.text).join(" ")` cannot drift. */
/* Join in C, and hand duktape ONE finished string.
 *
 * This used to push all N texts onto the value stack and duk_concat() them.
 * On an mbox that is tens of thousands of pushes -- it fails outright with
 * "cannot push beyond allocated stack" -- and every intermediate
 * concatenation allocated another duktape string.  Building the result in a
 * plain C buffer and pushing once means duktape sees the value only when it
 * is finished, which is the rule everywhere else in this module. */
static void tt_docs_join(duk_context *ctx, duk_idx_t docs)
{
    duk_uarridx_t i, n = (duk_uarridx_t)duk_get_length(ctx, docs);
    tt_buf out = {0};

    for(i = 0; i < n; i++)
    {
        duk_size_t tl = 0;
        const char *t;

        if(i) tt_buf_add(&out, " ", 1);
        duk_get_prop_index(ctx, docs, i);
        duk_get_prop_string(ctx, -1, "text");
        t = duk_get_lstring(ctx, -1, &tl);
        if(t && tl) tt_buf_add(&out, t, (size_t)tl);
        duk_pop_2(ctx);
    }

    duk_push_lstring(ctx, out.s ? out.s : "", (duk_size_t)out.len);
    free(out.s);
}

static void tt_email_opts_get(duk_context *ctx, duk_idx_t opts, tt_email_opts *o)
{
    o->prefer_html = 0;
    o->attachments = 1;
    o->max_att     = TT_EMAIL_MAX_ATT;
    o->max_depth   = TT_EMAIL_MAX_DEPTH;
    o->arena       = NULL;    /* convert_email() supplies it */

    if(!duk_is_object(ctx, opts) || duk_is_null(ctx, opts)) return;

    if(duk_get_prop_string(ctx, opts, "prefer"))
    {
        const char *p = duk_get_string(ctx, -1);
        if(p && !strcmp(p, "html")) o->prefer_html = 1;
        else if(p && strcmp(p, "text"))
            RP_THROW(ctx, "convert: prefer must be \"text\" or \"html\"");
    }
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, opts, "attachments"))
        o->attachments = duk_to_boolean(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, opts, "maxAttachment") && duk_is_number(ctx, -1))
    {
        double v = duk_get_number(ctx, -1);
        if(v >= 0) o->max_att = (size_t)v;
    }
    duk_pop(ctx);
}

/* Skip the leading byte-count line of an Apple Mail .emlx wrapper. */
static void tt_emlx_skip(const unsigned char **buf, size_t *len)
{
    size_t i = 0;
    const unsigned char *b = *buf;

    while(i < *len && isdigit(b[i])) i++;
    if(!i || i >= *len) return;
    while(i < *len && (b[i] == ' ' || b[i] == '\t' || b[i] == '\r')) i++;
    if(i < *len && b[i] == '\n')
    {
        *buf = b + i + 1;
        *len -= i + 1;
    }
}

#define TT_MIME_PTR "\xff" "mimeptr"

/* mailmime_parse() builds a tree that has to be freed on the way out -- and
   the way out includes a THROW.  A streaming callback that throws, or an html
   converter that does, longjmps straight past any mailmime_free() written
   after the walk, which leaked the whole tree.  So the tree is owned by a
   stack object and released on return AND on unwind, exactly as
   tt_map_file() owns its mapping and tt_arena_push() its temp file. */
static duk_ret_t tt_mime_finalizer(duk_context *ctx)
{
    struct mailmime *m = NULL;

    if(duk_get_prop_string(ctx, 0, TT_MIME_PTR))
        m = (struct mailmime *)duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    if(m) mailmime_free(m);
    return 0;
}

static void tt_mime_own(duk_context *ctx, struct mailmime *mime)
{
    duk_push_object(ctx);
    duk_push_pointer(ctx, mime);
    duk_put_prop_string(ctx, -2, TT_MIME_PTR);
    duk_push_c_function(ctx, tt_mime_finalizer, 1);
    duk_set_finalizer(ctx, -2);
}

/* One RFC 5322 message -> documents[] entries. */
static void tt_convert_message(duk_context *ctx, const unsigned char *buf, size_t len,
                               duk_idx_t docs, tt_ocr *oc, tt_email_opts *opt)
{
    struct mailmime *mime = NULL;
    size_t idx = 0;
    duk_idx_t base = duk_get_top(ctx);

    tt_emlx_skip(&buf, &len);

    if(mailmime_parse((const char *)buf, len, &idx, &mime) != MAILIMF_NO_ERROR || !mime)
    {
        /* Unparseable: keep whatever readable text is in there rather than
           losing the message entirely. */
        rp_string *t = extract_text_chunks(buf, len);
        duk_push_lstring(ctx, t->str, t->len);
        rp_string_free(t);
        tt_doc_push(ctx, docs, "message/rfc822", NULL, -1, 0, -1, oc);
        return;
    }

    tt_mime_own(ctx, mime);
    tt_mime_walk(ctx, mime, docs, oc, opt, -1, 0);
    /* drops the per-message metaData scaffolding AND the owner above, which
       frees the tree; an unwind past here does the same */
    duk_set_top(ctx, base);
}

/* One message out of an mbox.  The "From " line is mbox FRAMING, not an RFC
   5322 header -- handing it to the parser makes the whole header block
   unparseable and the message comes back as raw text. */
static void tt_mbox_msg(duk_context *ctx, const unsigned char *b, size_t n,
                        duk_idx_t docs, tt_ocr *oc, tt_email_opts *opt)
{
    if(n >= 5 && !memcmp(b, "From ", 5))
    {
        size_t k = 0;
        while(k < n && b[k] != '\n') k++;
        if(k < n) { b += k + 1; n -= k + 1; }
        else return;
    }
    if(n) tt_convert_message(ctx, b, n, docs, oc, opt);
}

static void convert_email(duk_context *ctx, const unsigned char *buf, size_t len,
                          filetype_t ft, tt_ocr *oc, duk_idx_t opts)
{
    tt_email_opts opt;
    duk_idx_t docs;

    tt_email_opts_get(ctx, opts, &opt);

    /* One arena for the whole call, reused by every large part.  It is only
       ever opened if a large part actually turns up, so an ordinary mailbox
       never touches a temp file.  Held by a stack object so a throw in any
       converter below still releases it. */
    opt.arena = tt_arena_push(ctx);

    /* streaming: no array at all -- each document goes straight out */
    if(oc->cb >= 0)
        docs = -1;
    else
    {
        duk_push_array(ctx);
        docs = duk_get_top_index(ctx);
    }

    if(ft == FT_MBOX)
    {
        /* Messages are separated by a "From " line at column 0.  All four
           mbox dialects differ only in how a literal "From " inside a body
           is escaped, and every one of them still starts a message this
           way, so splitting here reads all of them. */
        size_t start = 0, i = 0;

        while(i < len)
        {
            size_t eol = i;
            while(eol < len && buf[eol] != '\n') eol++;

            if(i > start && i + 5 <= len && !memcmp(buf + i, "From ", 5))
            {
                tt_mbox_msg(ctx, buf + start, i - start, docs, oc, &opt);
                start = i;
                if(oc->aborted) break;
            }
            i = eol + 1;
        }
        if(start < len && !oc->aborted)
            tt_mbox_msg(ctx, buf + start, len - start, docs, oc, &opt);
    }
    else
        tt_convert_message(ctx, buf, len, docs, oc, &opt);

    /* nothing parsed at all still yields one (empty) document: documents[]
       is never empty, and the callback is invoked at least once */
    if(oc->ndocs == 0 && !oc->aborted)
    {
        duk_push_string(ctx, "");
        tt_doc_push(ctx, docs, filetype_mimes[ft], NULL, -1, 0, -1, oc);
    }

    if(docs < 0)
    {
        duk_push_string(ctx, "");    /* placeholder; the caller returns a count */
        return;
    }

    oc->docs = docs;
    tt_docs_join(ctx, docs);         /* the text, on top, as the caller expects */
}

/* do_convert: pushes result string onto the duktape stack.
   If filename is non-NULL, PDF/DOC use file-based external tools.
   If filename is NULL (buffer mode), PDF/DOC use stdin-based tools. */
static void do_convert(const unsigned char *buf, size_t len,
                       filetype_t ft, duk_context *ctx,
                       const char *filename, const char **charset_out,
                       const char **charset_src_out, tt_ocr *oc)
{
    rp_string *result = NULL;
    rp_charset_result cs;
    int cs_used = 0;

    if(charset_out)     *charset_out = NULL;
    if(charset_src_out) *charset_src_out = NULL;

    /* Bytes from the file itself: decode to UTF-8 first, so that every
       converter below -- and every duktape string built from them --
       sees valid text. */
    if(filetype_is_text_bytes(ft) && len)
    {
        char csbuf[64];
        const char *declared = sniff_declared_charset(buf, len, ft, csbuf, sizeof csbuf);
        const char *cserr = NULL;
        if(rp_charset_to_utf8(buf, len, declared, 0, &cs, &cserr) == 0)
        {
            buf = (const unsigned char *)cs.text;
            len = cs.len;
            cs_used = 1;
            if(charset_out) *charset_out = cs.charset;
            if(charset_src_out)
            {
                switch(cs.source)
                {
                    case RP_CS_SRC_BOM:        *charset_src_out = "bom"; break;
                    case RP_CS_SRC_DECLARED:   *charset_src_out = "declared"; break;
                    case RP_CS_SRC_VALID_UTF8: *charset_src_out = "utf-8"; break;
                    case RP_CS_SRC_ASSUMED:    *charset_src_out = "assumed"; break;
                    default:                   *charset_src_out = "unknown"; break;
                }
            }
        }
        /* a failure here is not fatal: fall through with the original
           bytes and let the converter do what it always did */
    }

    /* metaData is read from the DECODED bytes, so a windows-1252 <title>
       cannot become invalid UTF-8 in a duktape string -- the same trap the
       charset layer above exists to avoid.  Only when details were asked
       for: the plain string path must not pay for a zip_extract or an exec
       whose result nobody will look at. */
    if(oc->details)
    {
        tt_push_metadata(ctx, buf, len, ft, filename);
        oc->meta = duk_get_top_index(ctx);
    }

    switch(ft)
    {
        case FT_HTML:
            call_js_converter(ctx, html_convert_js, "html", buf, len);
            goto cleanup;

        case FT_MARKDOWN:
        {
            rp_string *cleaned = preprocess_markdown(buf, len);
            call_js_converter(ctx, md_convert_js, "markdown",
                              (const unsigned char *)cleaned->str, cleaned->len);
            rp_string_free(cleaned);
            goto cleanup;
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
            goto cleanup;

        case FT_EMAIL:
        case FT_MBOX:
        case FT_MHTML:
            /* the only converter that can produce more than one document */
            convert_email(ctx, buf, len, ft, oc, oc->opts);
            goto cleanup;

        case FT_PDF:
        {
            char tool[PATH_MAX];
            if(!tt_find_tool("pdftotext", tool, sizeof tool))
                RP_THROW(ctx, "convert pdf: pdftotext not found -- " TT_PDF_HINT);
            if(filename)
                tt_call_tool_str(ctx, pdf_convert_file_js, "pdf", tool,
                                 filename, strlen(filename));
            else
                tt_call_tool_buf(ctx, pdf_convert_buf_js, "pdf", tool, buf, len);
            tt_ocr_after_pdf(ctx, oc, buf, len, filename);
            goto cleanup;
        }

        case FT_PNG: case FT_JPEG: case FT_TIFF: case FT_GIF:
        case FT_BMP: case FT_PNM:  case FT_PSD:  case FT_HDR:
            tt_ocr_convert_image(ctx, oc, buf, len, ft);
            goto cleanup;

        case FT_DOC:
        {
            char tool[PATH_MAX];
            int textutil = 0;
            if(!tt_find_tool("catdoc", tool, sizeof tool))
            {
                if(!tt_find_tool("textutil", tool, sizeof tool))
                    RP_THROW(ctx, "convert doc: neither catdoc nor textutil found -- "
                                  TT_DOC_HINT);
                textutil = 1;
            }
            if(filename)
                tt_call_tool_str(ctx, textutil ? doc_textutil_file_js : doc_catdoc_file_js,
                                 "doc", tool, filename, strlen(filename));
            else
                tt_call_tool_buf(ctx, textutil ? doc_textutil_buf_js : doc_catdoc_buf_js,
                                 "doc", tool, buf, len);
            goto cleanup;
        }

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

cleanup:
    /* the decoded copy, if one was made.  `buf' pointed into it, so
       nothing may touch buf after this. */
    if(cs_used) rp_charset_result_free(&cs);
}
/* ================================================================
   MAPPING THE INPUT
   ================================================================

   read_file_contents() + buf_to_stack() read the whole file and then copy
   it, so a conversion cost two full-size allocations with a 2x spike
   between them, and the copy was anonymous memory the kernel could not
   reclaim.  Worse, the duktape buffer caps at DUK_HBUFFER_MAX_BYTELEN
   (0x7ffffffe, just under 2 GiB): a 2.13 GB mbox did not merely run the
   machine out of memory, it failed outright with "buffer too long".

   Mapping the file read-only removes both copies.  The pages are clean and
   file-backed, so under pressure the kernel evicts and re-reads them
   instead of swapping, and nothing is subject to the duktape limit.

   THE TRAP: several parsers rely on the NUL that buf_to_stack() wrote --
   strstr() hunting "-->" and "\end{}", strtoul() in decode_entity().  A
   file mapping gives exactly the file's bytes; when the size is a multiple
   of the page size there is no readable slack after it and those calls walk
   into an unmapped page.  So the region is reserved as anonymous zeroed
   memory first and the file mapped over the front of it, which guarantees a
   readable zero byte at [len] whether or not the size is page-aligned. */

typedef struct { void *base; size_t maplen; } tt_map;

#define TT_MAP_PTR "\xff" "mapptr"
#define TT_MAP_LEN "\xff" "maplen"

/* munmap when the holder object goes away -- including when a converter
   throws and the value stack unwinds, which is the same reason
   buf_to_stack() put its buffer on the stack rather than in a malloc. */
static duk_ret_t tt_map_finalizer(duk_context *ctx)
{
    void *p = NULL;
    size_t l = 0;

    if(duk_get_prop_string(ctx, 0, TT_MAP_PTR)) p = duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    if(duk_get_prop_string(ctx, 0, TT_MAP_LEN)) l = (size_t)duk_get_number(ctx, -1);
    duk_pop(ctx);

    if(p && l) munmap(p, l);
    return 0;
}

/* Map filename read-only with a guaranteed zero byte at [*out_len].
   Returns the mapping, or NULL to let the caller fall back to reading. */
static const unsigned char *tt_map_file(duk_context *ctx, const char *filename,
                                        size_t *out_len)
{
    struct stat st;
    long pagesz = sysconf(_SC_PAGESIZE);
    size_t len, maplen;
    void *base, *p;
    int fd;

    if(pagesz <= 0) return NULL;
    if(rp_stat(filename, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)
        return NULL;

    len = (size_t)st.st_size;
    /* round up PAST len, so there is always at least one byte of slack even
       when the file size is an exact multiple of the page size */
    maplen = ((len + (size_t)pagesz) / (size_t)pagesz) * (size_t)pagesz;

    fd = open(filename, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return NULL;

    /* reserve zeroed address space, then lay the file over the front of it */
    base = mmap(NULL, maplen, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(base == MAP_FAILED) { close(fd); return NULL; }

    p = mmap(base, len, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0);
    close(fd);
    if(p == MAP_FAILED) { munmap(base, maplen); return NULL; }

    /* hand ownership to a stack object: released on return OR on throw */
    duk_push_object(ctx);
    duk_push_pointer(ctx, base);
    duk_put_prop_string(ctx, -2, TT_MAP_PTR);
    duk_push_number(ctx, (duk_double_t)maplen);
    duk_put_prop_string(ctx, -2, TT_MAP_LEN);
    duk_push_c_function(ctx, tt_map_finalizer, 1);
    duk_set_finalizer(ctx, -2);

    *out_len = len;
    return (const unsigned char *)base;
}

/* Move a malloc'd input buffer onto the value stack.  Every converter below
   may throw, and a throw unwinds the stack -- a plain malloc would leak. */
static unsigned char *buf_to_stack(duk_context *ctx, unsigned char *buf, size_t len)
{
    unsigned char *db = (unsigned char *)duk_push_fixed_buffer(ctx, (duk_size_t)len + 1);
    memcpy(db, buf, len);
    db[len] = 0;
    free(buf);
    return db;
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

/* Wrap the text result (on top of stack) in the details object.
 *
 *   {
 *     text, mimeType, title, metaData, documents:[ ... ],
 *     ocr, pages, charset, charsetSource
 *   }
 *
 * `documents` is ALWAYS present and ALWAYS holds at least one entry, so a
 * caller writes one code path whether the file held one document or five
 * hundred.  Every format here yields exactly one; email and mbox will add
 * more without changing the shape.
 *
 * Two rules from the spec are load-bearing:
 *
 *   - `text` is byte-for-byte what convertFile() returns without details,
 *     and equals documents.map(d => d.text).join(" ").  With one document
 *     that join is the identity, which is why this is trivially true here.
 *
 *   - top-level `ocr`, `pages`, `charset` and `charsetSource` describe
 *     documents[0] -- the PRIMARY document -- not the file as a whole, and
 *     are the SAME objects, not copies.  Today every file has exactly one
 *     document, so this is indistinguishable from what it has always meant;
 *     when an email's scanned attachment is documents[1], the top level
 *     keeps describing documents[0] and says nothing about the attachment.
 */
/* The documents[0] entry for every format that yields exactly one document.
   Consumes the text on top of the stack and leaves the entry in its place.
   Shared by push_details() and the streaming path, so the two cannot
   describe the same document differently. */
static void tt_build_doc0(duk_context *ctx, filetype_t ft, const char *charset,
                          const char *charset_src, tt_ocr *oc, const char *filename)
{
    duk_idx_t doc;

    duk_push_object(ctx);
    duk_insert(ctx, -2);                    /* [ doc, text ] */
    doc = duk_get_top_index(ctx) - 1;
    duk_put_prop_string(ctx, doc, "text");

    duk_push_string(ctx, filetype_mimes[ft]);
    duk_put_prop_string(ctx, doc, "mimeType");

    /* what the file itself said; built in do_convert(), where the decoded
       bytes were still alive */
    if(oc->meta >= 0) duk_dup(ctx, oc->meta);
    else              duk_push_object(ctx);
    duk_put_prop_string(ctx, doc, "metaData");

    /* DERIVED, for a database Title field -- distinct from metaData.title,
       which is only what the document claimed */
    duk_get_prop_string(ctx, doc, "metaData");
    tt_push_title(ctx, duk_get_top_index(ctx), filename);
    duk_remove(ctx, -2);
    if(duk_is_string(ctx, -1)) duk_put_prop_string(ctx, doc, "title");
    else                       duk_pop(ctx);

    /* What the bytes were decoded FROM.  Absent for formats whose text comes
       from an extractor rather than from the file's own bytes -- there is no
       source encoding to report for a PDF. */
    if(charset)
    {
        duk_push_string(ctx, charset);
        duk_put_prop_string(ctx, doc, "charset");
        duk_push_string(ctx, charset_src ? charset_src : "unknown");
        duk_put_prop_string(ctx, doc, "charsetSource");
    }

    duk_push_boolean(ctx, oc->did_ocr);
    duk_put_prop_string(ctx, doc, "ocr");
    if(oc->did_ocr && oc->pages >= 0)
    {
        duk_dup(ctx, oc->pages);
        duk_put_prop_string(ctx, doc, "pages");
    }
}

static void push_details(duk_context *ctx, filetype_t ft,
                         const char *charset, const char *charset_src, tt_ocr *oc,
                         const char *filename)
{
    duk_idx_t res, doc;

    /* text string is on top of stack */
    duk_push_object(ctx);
    duk_pull(ctx, -2);              /* move text back above the object */
    res = duk_get_top_index(ctx) - 1;
    duk_put_prop_string(ctx, res, "text");

    duk_push_string(ctx, filetype_mimes[ft]);
    duk_put_prop_string(ctx, res, "mimeType");

    /* Build documents[] first, then mirror documents[0] up to the top level.
       Doing it in that order means the single-document formats and the
       multi-document ones (email, mbox) take the SAME path, so the two
       cannot drift apart. */
    if(oc->docs < 0)
    {
        duk_push_array(ctx);
        duk_get_prop_string(ctx, res, "text");
        tt_build_doc0(ctx, ft, charset, charset_src, oc, filename);
        duk_put_prop_index(ctx, -2, 0);
        duk_put_prop_string(ctx, res, "documents");
    }
    else
    {
        duk_dup(ctx, oc->docs);
        duk_put_prop_string(ctx, res, "documents");
    }

    /* Top level mirrors documents[0], by reference.  metaData and title
       describe the FILE -- for an email that IS the primary message's
       headers -- while ocr/pages/charset describe the primary document and
       say nothing about an attachment further down the array. */
    duk_get_prop_string(ctx, res, "documents");
    duk_get_prop_index(ctx, -1, 0);
    doc = duk_get_top_index(ctx);
    {
        static const char *mirror[] = { "metaData", "title", "ocr", "pages",
                                        "charset", "charsetSource", NULL };
        int m;
        for(m = 0; mirror[m]; m++)
            if(duk_get_prop_string(ctx, doc, mirror[m]) && !duk_is_undefined(ctx, -1))
                duk_put_prop_string(ctx, res, mirror[m]);
            else
                duk_pop(ctx);
    }
    duk_pop_2(ctx);

    /* ocr is ALWAYS present, even when false */
    if(!duk_has_prop_string(ctx, res, "ocr"))
    {
        duk_push_boolean(ctx, 0);
        duk_put_prop_string(ctx, res, "ocr");
    }
    if(!duk_has_prop_string(ctx, res, "metaData"))
    {
        duk_push_object(ctx);
        duk_put_prop_string(ctx, res, "metaData");
    }
}

/* ================================================================
   JS INTERFACE
   ================================================================ */

/* Which stack slot holds the streaming callback, or -1.
 *
 *   convertFile(f, cb)                 shorthand -- details is implied
 *   convertFile(f, true, cb)
 *   convertFile(f, {details:true,...}, cb)
 *
 * `details` is meaningless alongside a callback (document objects are
 * delivered either way), so convertFile(f, false, cb) is not an error: the
 * callback wins and the flag is ignored. */
static duk_idx_t tt_callback_idx(duk_context *ctx)
{
    if(duk_is_function(ctx, 1)) return 1;
    if(duk_is_function(ctx, 2)) return 2;
    return -1;
}

/* Deliver the single document that every non-message format produces, then
   leave the count.  Consumes the text on top of the stack. */
static duk_ret_t tt_finish_streaming(duk_context *ctx, filetype_t ft,
                                     const char *charset, const char *charset_src,
                                     tt_ocr *oc, const char *filename)
{
    if(oc->ndocs == 0 && !oc->aborted)
    {
        /* a single-document format: build its entry exactly as the
           documents[0] path does, hand it over, and drop it */
        tt_build_doc0(ctx, ft, charset, charset_src, oc, filename);
        tt_deliver(ctx, oc);
    }
    else
        duk_pop(ctx);                    /* the placeholder from convert_email */

    duk_push_number(ctx, (duk_double_t)oc->ndocs);
    return 1;
}

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
    char gzname[PATH_MAX];
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
        fname_hint = strip_gz_ext(fname_hint, gzname, sizeof gzname);
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
    duk_idx_t cb = tt_callback_idx(ctx);
    int details = (cb >= 0) ? 1 : want_details(ctx, 1);
    const char *cs_name = NULL, *cs_src = NULL;

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

    buf = buf_to_stack(ctx, buf, len);
    filetype_t ft = identify_content(buf, len, "");
    tt_ocr oc;
    tt_ocr_begin(ctx, (cb == 1) ? 2 : 1, details, &oc);
    oc.cb = cb;

    /* buffer mode: no filename, PDF/DOC use stdin */
    do_convert(buf, len, ft, ctx, NULL, &cs_name, &cs_src, &oc);

    if(cb >= 0)
        return tt_finish_streaming(ctx, ft, cs_name, cs_src, &oc, NULL);

    if(details)
        push_details(ctx, ft, cs_name, cs_src, &oc, NULL);

    return 1;
}

/* convertFile(filename [, detailsFlag]) — convert from a file path */
static duk_ret_t rp_convert_file(duk_context *ctx)
{
    const char *filename = REQUIRE_STRING(ctx, 0,
        "convertFile: argument 1 must be a string (filename)");
    duk_idx_t cb = tt_callback_idx(ctx);
    /* a callback implies details: document objects are delivered regardless */
    int details = (cb >= 0) ? 1 : want_details(ctx, 1);

    size_t len = 0;
    const unsigned char *buf = NULL;
    unsigned char *heapbuf = NULL;
    const char *cs_name = NULL, *cs_src = NULL;
    const char *effective_filename = filename;
    char gzname[PATH_MAX];
    int was_gzipped = 0;

    /* Map it if we can.  This is what lets a file larger than duktape's
       ~2 GiB buffer ceiling be converted at all, and it keeps the bytes in
       reclaimable file-backed pages instead of the heap. */
    buf = tt_map_file(ctx, filename, &len);

    if(buf && is_gzip(buf, len))
    {
        /* compressed: the bytes we want do not exist on disk, so they have
           to be materialised.  The mapping is dropped by its finalizer. */
        size_t dec_len = 0;
        unsigned char *dec = gunzip(buf, len, &dec_len);
        if(!dec)
            RP_THROW(ctx, "convertFile: could not decompress gzipped file '%s'", filename);
        heapbuf = dec;
        len = dec_len;
        was_gzipped = 1;
        effective_filename = strip_gz_ext(filename, gzname, sizeof gzname);
    }

    if(!buf || heapbuf)
    {
        /* not mappable (a pipe, a /proc file, mmap refused) or decompressed */
        if(!heapbuf)
        {
            heapbuf = read_file_contents(filename, &len);
            if(!heapbuf)
                RP_THROW(ctx, "convertFile: could not read file '%s'", filename);
            if(is_gzip(heapbuf, len))
            {
                size_t dec_len = 0;
                unsigned char *dec = gunzip(heapbuf, len, &dec_len);
                free(heapbuf);
                if(!dec)
                    RP_THROW(ctx, "convertFile: could not decompress gzipped file '%s'",
                             filename);
                heapbuf = dec;
                len = dec_len;
                was_gzipped = 1;
                effective_filename = strip_gz_ext(filename, gzname, sizeof gzname);
            }
        }
        buf = buf_to_stack(ctx, heapbuf, len);
    }
    filetype_t ft = identify_content(buf, len, effective_filename);
    tt_ocr oc;
    tt_ocr_begin(ctx, (cb == 1) ? 2 : 1, details, &oc);
    oc.cb = cb;

    /* file mode: pass the filename so PDF/DOC use file-based tools -- unless
       the file was gzipped, in which case what is on disk is the COMPRESSED
       document and handing pdftotext that path just makes it fail.  Keyed off
       whether we decompressed, not off the name: a gzipped PDF is not always
       called .pdf.gz, and one that isn't used to be passed through. */
    do_convert(buf, len, ft, ctx, was_gzipped ? NULL : filename,
               &cs_name, &cs_src, &oc);

    /* streaming: every document has already gone to the callback; return the
       count, as Sql.exec() and importCsvFile() do */
    if(cb >= 0)
        return tt_finish_streaming(ctx, ft, cs_name, cs_src, &oc, filename);

    /* the real path, not the .gz-stripped one: the title's last-resort
       fallback is the name of the file the caller actually named */
    if(details)
        push_details(ctx, ft, cs_name, cs_src, &oc, filename);

    return 1;
}

/* **************************************************
   Initialize module
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_object(ctx);

    duk_push_c_function(ctx, rp_convert, 3);
    duk_put_prop_string(ctx, -2, "convert");

    duk_push_c_function(ctx, rp_convert_file, 3);
    duk_put_prop_string(ctx, -2, "convertFile");

    duk_push_c_function(ctx, rp_identify, 1);
    duk_put_prop_string(ctx, -2, "identify");

    duk_push_c_function(ctx, rp_set_ocr, 1);
    duk_put_prop_string(ctx, -2, "setOcr");

    return 1;
}
