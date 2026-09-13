
rampart.globalize(rampart.utils);

var totext = require("rampart-totext");
var testdir = process.scriptPath + "/convtest/";

var testFeature = new (require('./test-feature.js'))({prefix: "totext"});

/* ---- identification tests ---- */
var expected = {
    "test.txt":   "text",
    "test.html":  "html",
    "test.xml":   "xml",
    "test.md":    "markdown",
    "test.latex": "latex",
    "test.rtf":   "rtf",
    "test.1.gz":     "man",
    "test.pdf":   "pdf",
    "test.docx":  "docx",
    "test.odt":   "odt",
    "test.epub":  "epub",
    "test.doc":   "doc",
    "test.pptx":  "pptx",
    "test.xlsx":  "xlsx",
    "test.odp":   "odp",
    "test.ods":   "ods",
};

for(var file in expected) {
    var exp = expected[file];
    testFeature("identify " + file, function(){
        var got = totext.identify(testdir + file);
        if(got !== exp) {
            printf("\n  expected '%s', got '%s'\n", exp, got);
            return false;
        }
        return true;
    });
}

/* ---- conversion tests ---- */

var has_pdftotext = exec("which", "pdftotext");
var has_catdoc = exec("which", "catdoc");
var has_textutil = exec('which', "textutil");
var has_pdftoppm = exec("which", "pdftoppm");

has_pdftotext = has_pdftotext.exitStatus ===0 ? !!has_pdftotext.stdout : false;
has_textutil = has_textutil.exitStatus ===0 ? !!has_textutil.stdout : false;
has_catdoc = has_catdoc.exitStatus ===0 ? !!has_catdoc.stdout : false;
has_pdftoppm = has_pdftoppm.exitStatus === 0 ? !!has_pdftoppm.stdout : false;

var convertible = [
    "test.txt",
    "test.html",
    "test.xml",
    "test.md",
    "test.latex",
    "test.rtf",
    "test.1.gz",
    "test.docx",
    "test.odt",
    "test.epub",
    "test.pptx",
    "test.xlsx",
    "test.odp",
    "test.ods",
];

/* external tool dependencies - skip with message if not installed */
var ext_tool_tests = {
    "test.pdf": {available: has_pdftotext, msg: "pdftotext not installed"},
    "test.doc": {available: has_catdoc || has_textutil, msg: "catdoc not installed"},
};

function skipOrTest(name, file, fn) {
    var ext = ext_tool_tests[file];
    if (ext && !ext.available) {
        testFeature.skip(name, ext.msg);
        return;
    }
    testFeature(name, fn);
}

/* test all convertible files */
var all_convertible = convertible.slice();
for(var f in ext_tool_tests)
    all_convertible.push(f);

for(var i = 0; i < all_convertible.length; i++) {
    var file = all_convertible[i];
    skipOrTest("convert " + file, file, function(){
        var txt = totext.convertFile(testdir + file);
        if(typeof txt !== 'string') {
            printf("\n  expected string, got %s\n", typeof txt);
            return false;
        }
        if(txt.length < 50) {
            printf("\n  output too short: %d chars\n", txt.length);
            return false;
        }
        return true;
    });
}

/* check that known phrases appear in converted output */
var phrase_tests = [
    ["test.txt",   "Squish the Stack"],
    ["test.html",  "Squish the Stack"],
    ["test.xml",   "Squish the Stack"],
    ["test.md",    "Squish the Stack"],
    ["test.latex", "Squish the Stack"],
    ["test.html",  "Raspberry Pi Zero"],
    ["test.rtf",   "Squish the Stack"],
    ["test.1.gz",     "manual pager"],
    ["test.docx",  "Squish the Stack"],
    ["test.odt",   "Squish the Stack"],
    ["test.epub",  "Squish the Stack"],
    ["test.pdf",   "Squish the Stack"],
    ["test.doc",   "Squish the Stack"],
];

for(var i = 0; i < phrase_tests.length; i++) {
    var file = phrase_tests[i][0];
    var phrase = phrase_tests[i][1];
    skipOrTest("phrase '" + phrase.substring(0,20) + "' in " + file, file, function(){
        var txt = totext.convertFile(testdir + file);
        if(txt.indexOf(phrase) < 0) {
            printf("\n  phrase '%s' not found in output\n", phrase);
            printf("  output starts: %.200s\n", txt);
            return false;
        }
        return true;
    });
}

/* check paragraph separation: double newlines should exist in output */
testFeature("paragraphs preserved in html output", function(){
    var txt = totext.convertFile(testdir + "test.html");
    return txt.indexOf("\n\n") >= 0;
});

testFeature("no stray html tags in output", function(){
    var txt = totext.convertFile(testdir + "test.html");
    return txt.indexOf("</p>") < 0 && txt.indexOf("<script") < 0
        && txt.indexOf("<style") < 0 && txt.indexOf("</html>") < 0;
});

/* toText() extracts only VISIBLE text by default.  For a search index that
 * discards real authored prose: an image's alt text and a page's own
 * description of itself.  Scoring 300 .md files against pandoc put a number
 * on it -- README badge alt text alone was 12 of the 300 files.  The
 * formatting options stay off: aLinks/imgLinks append URLs after text already
 * extracted, and enumerateLists prepends the "1." numbering the LibreOffice
 * differential shows we are right not to emit. */
testFeature("html alt text and meta description are extracted", function(){
    var doc = '<html><head>' +
              '<meta name="description" content="MetaSummaryText">' +
              '<meta name="keywords" content="MetaKeywordText">' +
              '</head><body><p>VisibleBody.</p>' +
              '<img src="secret-src.png" alt="AltDescriptionText"></body></html>';
    var txt = totext.convert(doc);
    var want = ["VisibleBody", "AltDescriptionText", "MetaSummaryText", "MetaKeywordText"];
    for(var i = 0; i < want.length; i++)
        if(txt.indexOf(want[i]) < 0) {
            printf("\n  '%s' missing from: %J\n", want[i], txt);
            return false;
        }
    /* the src is an address, not text, and stays out */
    if(txt.indexOf("secret-src.png") >= 0) {
        printf("\n  image src leaked into text: %J\n", txt);
        return false;
    }
    return true;
});

testFeature("markdown image alt text survives", function(){
    var txt = totext.convert("# T\n\n![BadgeAltText](https://img.example/b.svg)\n\nTail.\n");
    if(txt.indexOf("BadgeAltText") < 0) {
        printf("\n  alt text dropped: %J\n", txt);
        return false;
    }
    return txt.indexOf("img.example") < 0;
});

testFeature("no stray xml tags in output", function(){
    var txt = totext.convertFile(testdir + "test.xml");
    return txt.indexOf("<para") < 0 && txt.indexOf("<link") < 0;
});

/* ---- convert() with buffer/string tests ---- */

testFeature("convert() with buffer", function(){
    var buf = readFile(testdir + "test.html");
    var txt = totext.convert(buf);
    return typeof txt === 'string' && txt.indexOf("Squish the Stack") >= 0;
});

testFeature("convert() with string", function(){
    var str = readFile(testdir + "test.rtf", true);
    var txt = totext.convert(str);
    return typeof txt === 'string' && txt.indexOf("Squish the Stack") >= 0;
});

testFeature("convert() with gzipped buffer", function(){
    var buf = readFile(testdir + "test.1.gz");
    var txt = totext.convert(buf);
    return typeof txt === 'string' && txt.indexOf("manual pager") >= 0;
});

skipOrTest("convert() pdf buffer via stdin", "test.pdf", function(){
    var buf = readFile(testdir + "test.pdf");
    var txt = totext.convert(buf);
    return typeof txt === 'string' && txt.indexOf("Squish the Stack") >= 0;
});

skipOrTest("convert() doc buffer via stdin", "test.doc", function(){
    var buf = readFile(testdir + "test.doc");
    var txt = totext.convert(buf);
    return typeof txt === 'string' && txt.indexOf("Squish the Stack") >= 0;
});

/* ---- identify() with buffer test ---- */

testFeature("identify() with buffer", function(){
    var buf = readFile(testdir + "test.docx");
    return totext.identify(buf) === "docx";
});

/* ---- details option tests ---- */

testFeature("convertFile() with details=true", function(){
    var ret = totext.convertFile(testdir + "test.html", true);
    return typeof ret === 'object' && ret.mimeType === "text/html"
        && typeof ret.text === 'string' && ret.text.length > 0;
});

testFeature("convertFile() with {details:true}", function(){
    var ret = totext.convertFile(testdir + "test.docx", {details:true});
    return ret.mimeType === "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
        && ret.text.indexOf("Squish the Stack") >= 0;
});

testFeature("convert() with details=true", function(){
    var buf = readFile(testdir + "test.odt");
    var ret = totext.convert(buf, true);
    return ret.mimeType === "application/vnd.oasis.opendocument.text"
        && ret.text.indexOf("Squish the Stack") >= 0;
});

testFeature("convertFile() without details returns string", function(){
    var ret = totext.convertFile(testdir + "test.html");
    return typeof ret === 'string';
});

/* details reports what the bytes were decoded FROM, for the formats whose
   text comes from the file itself.  Absent for extractor-sourced formats --
   there is no source encoding to report for a PDF. */
testFeature("details reports charset and charsetSource", function(){
    var html = totext.convertFile(testdir + "test.html", true);
    if(typeof html.charset !== 'string' || typeof html.charsetSource !== 'string') {
        printf("\n  charset=%s charsetSource=%s\n", html.charset, html.charsetSource);
        return false;
    }
    return true;
});

skipOrTest("details omits charset for pdf", "test.pdf", function(){
    var pdf = totext.convertFile(testdir + "test.pdf", true);
    return pdf.charset === undefined && pdf.charsetSource === undefined;
});

/* ================================================================
   details: text / documents[] / metaData / title
   ================================================================

   The contract, which holds for every format:

     - details.text is byte for byte what convertFile() returns without
       details.  A caller never has to wonder which one it got.
     - documents[] is ALWAYS there and ALWAYS has at least one entry, so
       there is one code path whether a file held one document or five
       hundred.  Every format today yields exactly one; email and mbox will
       add more without changing the shape.
     - details.text === documents.map(d => d.text).join(" ").
     - top-level ocr/pages/charset/charsetSource describe documents[0] --
       the PRIMARY document -- and are the SAME objects, not copies. */

var contract_files = ["test.txt", "test.html", "test.xml", "test.md", "test.latex",
                      "test.rtf", "test.1.gz", "test.docx", "test.odt", "test.epub",
                      "test.pptx", "test.xlsx", "test.odp", "test.ods"];

testFeature("details.text is identical to the plain string", function(){
    for(var i = 0; i < contract_files.length; i++) {
        var f = contract_files[i];
        if(totext.convertFile(testdir + f, {details:true}).text
           !== totext.convertFile(testdir + f)) {
            printf("\n  %s: details.text differs from convertFile()\n", f);
            return false;
        }
    }
    return true;
});

testFeature("documents[] is always present with at least one entry", function(){
    for(var i = 0; i < contract_files.length; i++) {
        var f = contract_files[i];
        var r = totext.convertFile(testdir + f, {details:true});
        if(!Array.isArray(r.documents) || r.documents.length < 1) {
            printf("\n  %s: documents=%J\n", f, r.documents);
            return false;
        }
    }
    return true;
});

testFeature("text equals the documents joined by a space", function(){
    for(var i = 0; i < contract_files.length; i++) {
        var f = contract_files[i];
        var r = totext.convertFile(testdir + f, {details:true});
        var join = r.documents.map(function(d){ return d.text; }).join(" ");
        if(join !== r.text) {
            printf("\n  %s: join(documents) !== text\n", f);
            return false;
        }
    }
    return true;
});

testFeature("top level and documents[0] share the same objects", function(){
    /* references, not copies -- mutating one must be visible in the other */
    var r = totext.convertFile(testdir + "test.docx", {details:true});
    if(r.documents[0].metaData !== r.metaData) {
        printf("\n  metaData was copied, not referenced\n");
        return false;
    }
    return r.documents[0].text === r.text
        && r.documents[0].mimeType === r.mimeType
        && r.documents[0].title === r.title;
});

testFeature("ocr is always present, even when false", function(){
    var r = totext.convertFile(testdir + "test.html", {details:true});
    if(typeof r.ocr !== "boolean" || r.ocr !== false) {
        printf("\n  ocr=%J (%s)\n", r.ocr, typeof r.ocr);
        return false;
    }
    /* and pages is absent when no recognition happened */
    return r.pages === undefined && r.documents[0].pages === undefined;
});

/* metaData: one common schema, so a consumer keying off `title`/`author`
 * works whatever the document was.  Keys appear only when the format
 * supplies them, so presence is a meaningful test. */
testFeature("metaData uses one schema across formats", function(){
    var want = {
        "test.docx": ["title", "author", "created"],
        "test.odt":  ["title", "created"],
        "test.epub": ["title", "author", "language"],
        "test.html": ["title", "author", "description"],
        "test.1.gz": ["title", "section", "date"]
    };
    for(var f in want) {
        var m = totext.convertFile(testdir + f, {details:true}).metaData;
        if(typeof m !== "object") { printf("\n  %s: no metaData\n", f); return false; }
        for(var i = 0; i < want[f].length; i++)
            if(typeof m[want[f][i]] !== "string") {
                printf("\n  %s: metaData.%s missing; got %J\n", f, want[f][i], m);
                return false;
            }
    }
    return true;
});

testFeature("metaData is empty, not absent, when a format has none", function(){
    var r = totext.convertFile(testdir + "test.txt", {details:true});
    if(typeof r.metaData !== "object") return false;
    var n = 0; for(var k in r.metaData) n++;
    return n === 0;
});

/* title is DERIVED for a database Title field, and is not the same thing as
 * metaData.title -- which is only what the document claimed. */
testFeature("title comes from the document's own metadata", function(){
    var r = totext.convertFile(testdir + "test.docx", {details:true});
    return r.title === r.metaData.title && r.title.length > 0;
});

testFeature("title falls back to the file's basename", function(){
    /* test.txt has no metadata of any kind */
    var r = totext.convertFile(testdir + "test.txt", {details:true});
    if(r.title !== "test.txt") {
        printf("\n  title=%J, expected \"test.txt\"\n", r.title);
        return false;
    }
    return true;
});

testFeature("title is absent when a buffer offers nothing to derive it from", function(){
    /* convert() has no filename to fall back on */
    var r = totext.convert("just some plain prose with no title anywhere in it.\n",
                           {details:true});
    if(r.title !== undefined) {
        printf("\n  title=%J, expected undefined\n", r.title);
        return false;
    }
    /* but a buffer that DOES carry a title still gets one */
    var h = totext.convert("<html><head><title>BufferTitle</title></head>" +
                           "<body><p>x</p></body></html>", {details:true});
    return h.title === "BufferTitle";
});

/* Scratch space for the tests below, which have to CREATE a file to name it
   themselves.  It is deliberately not convtest/: that holds the fixtures and,
   in an installed tree, belongs to root -- writing there fails for an ordinary
   user, and dirties the install when it succeeds.  Made here, removed at the
   end, and every file removed in a `finally` so a test that fails or throws
   still leaves nothing behind. */
var scratch = (process.env.TMPDIR || "/tmp") + "/rampart-totext-test-" + process.getpid();
mkdir(scratch);

function scratchFile(name) { return scratch + "/" + name; }
function unlinkQuiet(p)    { try { rmFile(p); } catch(e) {} }

/* write content to a scratch file, run fn(path), always clean up */
function withFile(name, content, fn) {
    var p = scratchFile(name);
    try {
        fprintf(p, "%s", content);
        return fn(p);
    } finally {
        unlinkQuiet(p);
    }
}

/* ================================================================
   IDENTIFICATION: a guess must never outrank an explicit extension
   ================================================================ */

/* The markdown probe is a statistical guess between two kinds of text, and it
 * used to outrank the extension: five of 9,822 RFCs -- plain hard-wrapped
 * .txt, full of ASCII box-drawing -- scored as markdown off their '****'
 * rules and '#' column labels, and cmark then joined their lines and ate
 * their structural characters.  An explicit text extension now wins over the
 * guess; a file with NO meaningful extension still gets it.  Binary
 * signatures (a PDF named .txt) are unaffected -- those are signatures, not
 * statistics. */

testFeature("md-looking .txt is identified as text", function(){
    var art = "#  Chart 1\n" +
              "****************************\n" +
              "**  boxes  **  and rules  **\n" +
              "****************************\n" +
              "#  see ```figure``` above\n";
    var txt  = scratchFile("mdish.txt");
    var data = scratchFile("mdish.data");
    try {
        fprintf(txt, "%s", art);
        var asTxt = totext.identify(txt);
        /* same bytes, no extension to trust: the sniffer may guess */
        fprintf(data, "%s", art);
        var asData = totext.identify(data);
        if(asTxt !== "text") {
            printf("\n  .txt identified as '%s'\n", asTxt);
            return false;
        }
        return asData === "markdown";
    } finally {
        unlinkQuiet(txt);
        unlinkQuiet(data);
    }
});

/* The LaTeX probe is the same kind of guess -- it hunts command-looking
 * substrings anywhere in the first kilobyte -- and it used to sit ABOVE the
 * extension guard with the real signatures.  A .txt that merely MENTIONS
 * \section{} was converted as LaTeX; worse, a .py holding "\\section{%s}" in
 * a string was too, and a stray % then ate the rest of its line as a comment,
 * so `return TEMPLATE % t` came back as `return TEMPLATE`. */

testFeature("latex-looking .txt is identified as text", function(){
    var prose = "Meeting notes\n\n" +
                "The report will use \\section{Results} and \\href{u}{link} markup.\n";
    return withFile("notes.txt", prose, function(p){
        var id = totext.identify(p);
        if(id !== "text") { printf("\n  identified as '%s'\n", id); return false; }
        /* and the markup must survive as literal text, not be "converted" */
        return totext.convertFile(p).indexOf("\\section{Results}") >= 0;
    });
});

testFeature("latex-looking .py is plaintext and comes back verbatim", function(){
    var src = "# helper\nTEMPLATE = \"\\\\section{%s}\"\ndef emit(t):\n    return TEMPLATE % t\n";
    return withFile("code.py", src, function(p){
        var id = totext.identify(p);
        if(id !== "plaintext") { printf("\n  identified as '%s'\n", id); return false; }
        var txt = totext.convertFile(p);
        if(txt !== src) {
            printf("\n  plaintext was not returned verbatim:\n  %J\n", txt);
            return false;
        }
        return true;
    });
});

testFeature("pdf named .txt is still a pdf", function(){
    var liar = scratchFile("liar.txt");
    try {
        fprintf(liar, "%s", readFile(testdir + "test.pdf"));
        return totext.identify(liar) === "pdf";
    } finally {
        unlinkQuiet(liar);
    }
});

/* A UTF-8 BOM belongs to no signature, and the whitespace skip in the HTML
 * sniffer did not step over one.  With a filename the extension rescued it;
 * through convert(), where there is no extension, a BOM'd HTML document came
 * back unidentified and was returned as raw markup. */
testFeature("a UTF-8 BOM does not defeat content identification", function(){
    var doc = "<!DOCTYPE html>\n<html><body><p>BomBody</p></body></html>\n";
    /* U+FEFF, which is the three bytes ef bb bf once encoded as UTF-8 */
    var bom = "﻿" + doc;
    if(totext.identify(stringToBuffer(bom)) !== "html") {
        printf("\n  BOM'd html identified as '%s'\n", totext.identify(stringToBuffer(bom)));
        return false;
    }
    var txt = totext.convert(bom);
    if(txt.indexOf("BomBody") < 0 || txt.indexOf("<html") >= 0) {
        printf("\n  converted to: %J\n", txt);
        return false;
    }
    return true;
});

/* ================================================================
   DOCX: the main document part, not the app metadata
   ================================================================

   docx_find_document_path() looked for the bare substring "officeDocument"
   in _rels/.rels.  That substring is also in the XML namespace of every
   OTHER OOXML relationship type
   (".../officeDocument/2006/relationships/extended-properties"), so it
   matched the first <Relationship> in the file whatever its type, and the
   Target taken from a byte window around it was that element's -- usually
   docProps/app.xml.  That entry extracts perfectly well, so the fallback to
   word/document.xml never ran: 50 of the 83 documents in the pandoc corpus
   converted to their app metadata ("Normal.dotm 2 1 67 388 Microsoft Office
   Word ...") instead of their text, and every test here passed anyway
   because metadata is a non-empty string too. */

var docxdir = testdir + "pandoc-docx/";

testFeature("docx converts the body, not docProps metadata", function(){
    var files = readDir(docxdir).filter(function(f){ return /\.docx$/.test(f); });
    var bad = [];

    files.forEach(function(f){
        var txt;
        try { txt = totext.convertFile(docxdir + f); }
        catch(e) { bad.push(f + " (threw: " + e.message.split("\n")[0] + ")"); return; }
        /* strings that only ever appear in docProps/app.xml or core.xml */
        if(/Microsoft Office Word|Normal\.dotm|LibreOffice|OpenOffice/.test(txt))
            bad.push(f);
    });

    if(bad.length) {
        printf("\n  %d of %d docx converted to metadata; first few:\n", bad.length, files.length);
        printf("    %s\n", bad.slice(0, 6).join("\n    "));
        return false;
    }
    return files.length > 0;
});

testFeature("docx body text is the document's own", function(){
    var txt = totext.convertFile(docxdir + "block_quotes.docx");
    if(txt.indexOf("Some block quotes") < 0) {
        printf("\n  body phrase missing; got: %.120s\n", txt);
        return false;
    }
    return true;
});

/* ================================================================
   OOXML runs: a word split across runs is still one word
   ================================================================

   convert_xml() emitted a separator space at every non-block tag boundary so
   that generic inline markup ("<b>a</b><i>b</i>") would not concatenate.
   Word splits words across <w:r>/<w:t> runs all the time -- spell check,
   rsid tracking, language tagging, tracked changes -- so that space landed
   INSIDE words: "Hello" became "Hel lo", which for a search index means the
   term is not there at all. */

testFeature("ooxml runs are joined, not spaced apart", function(){
    var xml = '<?xml version="1.0"?>' +
              '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">' +
              '<w:body><w:p>' +
              '<w:r><w:rPr><w:b/></w:rPr><w:t>Hel</w:t></w:r>' +
              '<w:r><w:t>lo</w:t></w:r>' +
              '<w:r><w:t xml:space="preserve"> world </w:t></w:r>' +
              '<w:r><w:t>Ram</w:t></w:r><w:r><w:t>part</w:t></w:r>' +
              '</w:p></w:body></w:document>';
    var txt = totext.convert(xml).replace(/\s+/g, " ").trim();
    if(txt !== "Hello world Rampart") {
        printf("\n  got %J, expected \"Hello world Rampart\"\n", txt);
        return false;
    }
    return true;
});

/* Both of these were found by scoring the corpora against LibreOffice rather
 * than against assertions written here.
 * The first is the cost of the exemption above: a tag that MEANS whitespace
 * has to be named, or the no-space rule swallows it.  <w:br> was in the block
 * list; its DrawingML twin <a:br> was not, so a pptx line break ran the text
 * on either side of it together ("This is a subtitleA. M."). */
testFeature("drawingml line breaks separate words", function(){
    var xml = '<?xml version="1.0"?>' +
              '<a:p xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">' +
              '<a:r><a:t>subtitle</a:t></a:r><a:br /><a:br />' +
              '<a:r><a:t>A. M.</a:t></a:r></a:p>';
    var txt = totext.convert(xml);
    if(txt.indexOf("subtitleA") >= 0) {
        printf("\n  line break swallowed: %J\n", txt);
        return false;
    }
    return txt.indexOf("subtitle") >= 0 && txt.indexOf("A. M.") >= 0;
});

/* <w:sym> is a character, not markup: Word stores a symbol-font glyph that
 * way, and skipping the tag dropped the character. */
testFeature("w:sym contributes its character", function(){
    var xml = '<?xml version="1.0"?>' +
              '<w:p xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">' +
              '<w:r><w:t>costs 10.</w:t></w:r>' +
              '<w:r><w:sym w:font="Symbol" w:char="00DA"/></w:r>' +
              '<w:r><w:sym w:font="Symbol" w:char="F0DA"/></w:r></w:p>';
    var txt = totext.convert(xml);
    if(txt.indexOf("Ú") < 0) {
        printf("\n  w:char=00DA dropped: %J\n", txt);
        return false;
    }
    /* F000-F0FF is the private-use block symbol fonts map glyphs into; there
       is no text to recover there, and a replacement character would be worse */
    return !/[-]/.test(txt);
});

testFeature("generic inline xml tags still separate words", function(){
    /* the other half of the trade: <b>a</b><i>b</i> must not become "ab" */
    var xml = '<?xml version="1.0"?><para>one<b>two</b><i>three</i></para>';
    var txt = totext.convert(xml).replace(/\s+/g, " ").trim();
    if(txt.indexOf("twothree") >= 0 || txt.indexOf("onetwo") >= 0) {
        printf("\n  words concatenated: %J\n", txt);
        return false;
    }
    return true;
});

/* ================================================================
   XML: CDATA and DOCTYPE internal subsets contain '>'
   ================================================================ */

testFeature("cdata content survives intact", function(){
    var xml = '<?xml version="1.0"?>\n<root><para>' +
              '<![CDATA[if (a > b) { keepThis(); }]]>' +
              '</para></root>\n';
    var txt = totext.convert(xml).replace(/\s+/g, " ").trim();
    /* the old skip-to-'>' stopped inside the section: everything before the
       '>' was lost and the "]]>" delimiter leaked into the output */
    if(txt !== "if (a > b) { keepThis(); }") {
        printf("\n  got %J\n", txt);
        return false;
    }
    return true;
});

testFeature("doctype internal subset is not mistaken for text", function(){
    var xml = '<?xml version="1.0"?>\n' +
              '<!DOCTYPE root [ <!ENTITY x "y"> <!ELEMENT root (#PCDATA)> ]>\n' +
              '<root><para>RealBody</para></root>\n';
    var txt = totext.convert(xml).replace(/\s+/g, " ").trim();
    if(txt !== "RealBody") {
        printf("\n  got %J\n", txt);
        return false;
    }
    return true;
});

/* ================================================================
   man: macro arguments are the page, not markup
   ================================================================

   Every macro that was not in the paragraph-break list fell through to
   skip_to_eol(), which discards the macro's ARGUMENTS -- and for .B .I .BR
   .IR .SM .SB those arguments are the content.  On grep.1 that silently
   removed every option name, the whole SYNOPSIS, and the subject of most
   sentences: DESCRIPTION read "searches for in each is one or more
   patterns".  Macro definition bodies (.de ... ..) went the other way and
   were emitted AS text. */

var man_src =
    '.TH TTTEST 1 "2026-09-05" "rampart" "Test Pages"\n' +
    '.SH NAME\n' +
    'tttest \\- exercise the man converter\n' +
    '.SH SYNOPSIS\n' +
    '.B tttest\n' +
    '.RI [ options ]\n' +
    '.SH DESCRIPTION\n' +
    '.B grepish\n' +
    'searches for\n' +
    '.I PATTERNS\n' +
    'in each\n' +
    '.IR FILE .\n' +
    '.TP\n' +
    '.BR \\-i ", " \\-\\-ignore\\-case\n' +
    'Ignore case distinctions.\n' +
    '.de ZZmac\n' +
    'MACROBODYLEAK \\\\$1\n' +
    '..\n' +
    '.SH "REPORTING BUGS"\n' +
    'Report bugs at\n' +
    '.URL "https://github.com/example/proj/issues" "" "."\n' +
    '.SH AUTHOR\n' +
    'Written by A. Person <pzz@apevzner\\.com>.\n';

testFeature("man: .B/.I argument text is kept", function(){
    return withFile("tttest.1", man_src, function(p){
        var txt = totext.convertFile(p);
        var want = ["grepish", "PATTERNS", "FILE", "tttest"];
        for(var i = 0; i < want.length; i++) {
            if(txt.indexOf(want[i]) < 0) {
                printf("\n  '%s' missing from:\n%s\n", want[i], txt);
                return false;
            }
        }
        return true;
    });
});

testFeature("man: alternating-font macros concatenate their arguments", function(){
    return withFile("tttest.1", man_src, function(p){
        var txt = totext.convertFile(p);
        /* .BR \-i ", " \-\-ignore\-case  renders "-i, --ignore-case" */
        if(txt.indexOf("-i, --ignore-case") < 0) {
            printf("\n  option string not joined correctly:\n%s\n", txt);
            return false;
        }
        /* .RI [ options ]  renders "[options]" */
        return txt.indexOf("[options]") >= 0;
    });
});

testFeature("man: macro definition bodies are not emitted", function(){
    return withFile("tttest.1", man_src, function(p){
        var txt = totext.convertFile(p);
        if(txt.indexOf("MACROBODYLEAK") >= 0) {
            printf("\n  .de body leaked into the text\n");
            return false;
        }
        return true;
    });
});

testFeature("man: heading escapes are decoded, not copied raw", function(){
    /* .TH/.SH text used to be copied byte for byte, so roff string
       interpolations like \*(Dt appeared verbatim in the output */
    return withFile("esc.1", '.TH ESC 1 "\\*(Dt" "v"\n.SH NA\\*(XXME\nbody text\n',
        function(p){
            var txt = totext.convertFile(p);
            if(txt.indexOf("\\*(") >= 0) {
                printf("\n  raw escape in output: %J\n", txt);
                return false;
            }
            return txt.indexOf("body text") >= 0;
        });
});

/* Both found by scoring 500 pages of /usr/share/man against pandoc rather
 * than against fixtures written here. */
testFeature("man: link macro arguments are kept", function(){
    /* .URL's argument IS the address; it went the way of every other macro
       argument, so REPORTING BUGS sections had no address in them at all */
    return withFile("tttest.1", man_src, function(p){
        var txt = totext.convertFile(p);
        if(txt.indexOf("https://github.com/example/proj/issues") < 0) {
            printf("\n  URL lost:\n%s\n", txt);
            return false;
        }
        return true;
    });
});

testFeature("man: an escaped period is a period", function(){
    /* \. is the literal character; dropping the escaped char merged the words
       on either side, turning "pzz@apevzner\.com" into "apeveznercom" */
    return withFile("tttest.1", man_src, function(p){
        var txt = totext.convertFile(p);
        if(txt.indexOf("apevzner.com") < 0) {
            printf("\n  escaped period dropped: %J\n",
                   txt.substring(txt.indexOf("Person") - 10, txt.length));
            return false;
        }
        return true;
    });
});

testFeature("man: the no-break control character is a macro line", function(){
    /* a line starting with "'" invokes the macro exactly as "." does, just
       without a break first.  Recognizing only "." left "'br" in the text --
       found by scoring 500 pages against pandoc, in CA.pl.1ssl */
    return withFile("nb.1",
        ".TH NB 1 \"d\" \"v\"\n.SH NAME\n'br\nreal body text\n'ti 0\nmore body text\n",
        function(p){
            var txt = totext.convertFile(p);
            if(txt.indexOf("br") >= 0 || txt.indexOf("ti 0") >= 0) {
                printf("\n  control line emitted as text: %J\n", txt);
                return false;
            }
            return txt.indexOf("real body text") >= 0
                && txt.indexOf("more body text") >= 0;
        });
});

testFeature("man: a real page keeps its options and synopsis", function(){
    /* the fixture is a Danish man(1); any real page exercises the same paths */
    var txt = totext.convertFile(testdir + "test.1.gz");
    if(txt.indexOf("\\*(") >= 0) {
        printf("\n  raw roff escapes in output\n");
        return false;
    }
    return txt.length > 1000;
});

/* ================================================================
   markdown: code is literal
   ================================================================

   preprocess_markdown() deleted any brace group containing a '.', '#' or
   '=', with no idea what a fenced block or a code span is.  A whole ```json
   block holding { "user.name": "alice" } vanished from the document, and
   `export FLAGS="{ debug = true }"` came out as `export FLAGS=""`. */

var md_src =
    "# API\n\n" +
    "Request body:\n\n" +
    "```json\n" +
    '{ "user.name": "alice", "id": 7 }\n' +
    "```\n\n" +
    "Shell:\n\n" +
    "```sh\n" +
    'export FLAGS="{ debug = true }"\n' +
    "```\n\n" +
    "Inline: use `{ mode = \"fast\" }` in config.\n\n" +
    "<div align=\"center\">\n" +
    "  <sub>RawHtmlBlockText stays in the document.<br>Second line.</sub>\n" +
    "</div>\n\n" +
    "Prose tail.\n";

testFeature("markdown keeps braced code in fences and spans", function(){
    return withFile("api.md", md_src, function(p){
        var txt = totext.convertFile(p);
        var want = ['"user.name"', "alice", "debug = true", 'mode = "fast"', "Prose tail"];
        for(var i = 0; i < want.length; i++) {
            if(txt.indexOf(want[i]) < 0) {
                printf("\n  '%s' was eaten; output:\n%s\n", want[i], txt);
                return false;
            }
        }
        return true;
    });
});

/* cmark drops raw HTML blocks unless asked not to -- it substitutes
 * "<!-- raw HTML omitted -->" -- so a README that centres its blurb in a
 * <div><sub>...</sub></div>, which is most of them, lost that text.  Found by
 * scoring 300 real .md files against pandoc. */
testFeature("markdown keeps text inside raw HTML blocks", function(){
    return withFile("api.md", md_src, function(p){
        var txt = totext.convertFile(p);
        if(txt.indexOf("RawHtmlBlockText") < 0 || txt.indexOf("Second line") < 0) {
            printf("\n  raw HTML block dropped:\n%s\n", txt);
            return false;
        }
        /* it is the TEXT we want, not the markup around it */
        return txt.indexOf("<div") < 0 && txt.indexOf("<sub") < 0;
    });
});

testFeature("markdown still strips pandoc attribute spans", function(){
    /* the other half of the trade: real attribute spans must still go */
    var txt = totext.convert("# Head {#id .cls}\n\nText {.warn} here.\n");
    if(txt.indexOf("{#id") >= 0 || txt.indexOf("{.") >= 0) {
        printf("\n  attribute span left in output: %J\n", txt);
        return false;
    }
    return txt.indexOf("Head") >= 0 && txt.indexOf("Text") >= 0;
});

testFeature("no markdown syntax in test.md output", function(){
    var txt = totext.convertFile(testdir + "test.md");
    return txt.indexOf("::::") < 0 && txt.indexOf("{.") < 0;
});

/* ================================================================
   LaTeX
   ================================================================

   Eight guards compared a cmd_len against a number that was not the length
   of the literal beside it, so the branch could never fire for the command
   it named.  Each fell through to the permissive tail, which EMITS braced
   arguments -- so image filenames, length values, bibliography style names
   and whole tikz diagrams were dumped into the extracted text. */

var tex_src =
    "\\documentclass{article}\n" +
    "\\begin{document}\n" +
    "Alpha \\includegraphics[width=3in]{secret-figure.png} Beta\n" +
    "\\pandocbounded{\\includegraphics[keepaspectratio]{wrapped.png}}\n" +
    "\\setlength{\\parindent}{0pt}\n" +
    "\\bibliographystyle{plainnat}\n" +
    "\\begin{tikzpicture}\n" +
    "\\draw (0,0) -- (5,5) node {DiagramInnards};\n" +
    "\\end{tikzpicture}\n" +
    "Quote\\textquotesingle{}s test.\n" +
    "\\underline{UnderlinedText}\n" +
    "\\end{document}\n";

testFeature("latex does not leak skipped command arguments", function(){
    return withFile("doc.tex", tex_src, function(p){
        var txt = totext.convertFile(p);
        var leaks = ["secret-figure.png", "width=3in", "wrapped.png", "keepaspectratio",
                     "0pt", "plainnat", "DiagramInnards", "article"];
        for(var i = 0; i < leaks.length; i++) {
            if(txt.indexOf(leaks[i]) >= 0) {
                printf("\n  '%s' leaked into output:\n%s\n", leaks[i], txt);
                return false;
            }
        }
        return true;
    });
});

testFeature("latex keeps the text around those commands", function(){
    return withFile("doc.tex", tex_src, function(p){
        var txt = totext.convertFile(p);
        var want = ["Alpha", "Beta", "UnderlinedText"];
        for(var i = 0; i < want.length; i++)
            if(txt.indexOf(want[i]) < 0) {
                printf("\n  '%s' lost; output:\n%s\n", want[i], txt);
                return false;
            }
        /* \textquotesingle must produce the apostrophe, not drop it */
        if(txt.indexOf("Quote's test") < 0) {
            printf("\n  apostrophe dropped: %J\n", txt);
            return false;
        }
        return true;
    });
});

testFeature("no latex commands in test.latex output", function(){
    var txt = totext.convertFile(testdir + "test.latex");
    return txt.indexOf("\\section") < 0 && txt.indexOf("\\href") < 0
        && txt.indexOf("keepaspectratio") < 0 && txt.indexOf("includegraphics") < 0;
});

/* ================================================================
   RTF
   ================================================================ */

var B = "\\";   /* one literal backslash */
var rtf_src =
    "{" + B + "rtf1" + B + "ansi" + B + "ansicpg1252\n" +
    "{" + B + "*" + B + "generator SecretGenerator 1.0}\n" +
    "PageOne" + B + "sect PageTwo" + B + "par\n" +
    "Curly: " + B + "'93quoted" + B + "'94" + B + "par\n" +
    "Uni: " + B + "u8220" + B + "'93q" + B + "u8221" + B + "'94" + B + "par\n" +
    "}\n";

/* "\*" marks a destination that a reader which does not understand it must
 * DROP.  Only a hardcoded list was skipped, so {\*\generator ...} and the URL
 * inside {\field{\*\fldinst{HYPERLINK ...}}} landed in the text -- the
 * project's own test.rtf converted 3 KB larger than the same document as
 * .docx purely from that. */
testFeature("rtf drops unknown \\* destinations", function(){
    return withFile("t.rtf", rtf_src, function(p){
        var txt = totext.convertFile(p);
        if(txt.indexOf("SecretGenerator") >= 0) {
            printf("\n  destination leaked: %J\n", txt);
            return false;
        }
        return true;
    });
});

testFeature("rtf test fixture has no destination leakage", function(){
    /* test.rtf carries 28 {\field{\*\fldinst{HYPERLINK "..."}}} groups; the
       field instruction and its URL are markup, only the \fldrslt text is
       content.  ("generator" is not usable as a marker here -- the document
       body legitimately says "pseudo random number generator".) */
    var txt = totext.convertFile(testdir + "test.rtf");
    if(txt.indexOf("HYPERLINK") >= 0 || txt.indexOf("fldinst") >= 0) {
        printf("\n  field instruction text in output\n");
        return false;
    }
    return txt.indexOf("\\par") < 0 && txt.indexOf("\\f0") < 0;
});

/* \sect was guarded by "cw_len == 12" against a 4-character literal, so it
 * never fired -- and sections were joined with NO separator at all, running
 * the last word of one into the first word of the next. */
testFeature("rtf \\sect breaks the section", function(){
    return withFile("t.rtf", rtf_src, function(p){
        var txt = totext.convertFile(p);
        if(txt.indexOf("PageOnePageTwo") >= 0) {
            printf("\n  sections concatenated: %J\n", txt);
            return false;
        }
        return txt.indexOf("PageOne") >= 0 && txt.indexOf("PageTwo") >= 0;
    });
});

/* \'hh in an \ansi document is Windows-1252, not Latin-1.  Encoding the byte
 * as its own codepoint turned every smart quote, dash, ellipsis and bullet
 * Word emits into an invisible C1 control character. */
testFeature("rtf \\'hh decodes as windows-1252", function(){
    return withFile("t.rtf", rtf_src, function(p){
        var txt = totext.convertFile(p);
        if(txt.indexOf("\u201c") < 0 || txt.indexOf("\u201d") < 0) {
            printf("\n  curly quotes missing: %J\n", txt);
            return false;
        }
        if(/[\u0080-\u009f]/.test(txt)) {
            printf("\n  C1 control characters in output: %J\n", txt);
            return false;
        }
        return true;
    });
});

/* \ucN says how many fallback characters follow a \uN.  Assuming a literal
 * '?' was wrong for the common case -- Word writes a \'hh escape there -- so
 * every unicode character came out followed by a stray one. */
testFeature("rtf \\uN does not also emit its fallback character", function(){
    return withFile("t.rtf", rtf_src, function(p){
        var txt = totext.convertFile(p);
        var m = txt.match(/Uni: (.*)/);
        if(!m) { printf("\n  no Uni line in %J\n", txt); return false; }
        if(m[1].replace(/\s+$/, "") !== "\u201cq\u201d") {
            printf("\n  got %J, expected \"\u201cq\u201d\"\n", m[1]);
            return false;
        }
        return true;
    });
});

/* ================================================================
   EPUB: spine order
   ================================================================

   Content documents were concatenated in ZIP order, which is whatever the
   producer happened to write.  test.epub stores them alphabetically, so
   nav.xhtml (which is not in the spine at all) came first and the title page
   came last, after both chapters. */

/* A store-only ZIP writer, enough to build an EPUB whose archive order is the
   REVERSE of its reading order.  test.epub cannot prove this on its own: its
   entries happen to be stored in nearly spine order already, so only the
   nav.xhtml exclusion below distinguishes the two. */
function zipStore(entries) {
    var out = [], central = [], offset = 0;

    function u16(a, v) { a.push(v & 255, (v >> 8) & 255); }
    function u32(a, v) { a.push(v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255); }
    function bytes(a, s) { for(var i = 0; i < s.length; i++) a.push(s.charCodeAt(i) & 255); }

    entries.forEach(function(e){
        var crc = crc32(e.data), n = e.data.length, lh = [];

        u32(lh, 0x04034b50); u16(lh, 20); u16(lh, 0); u16(lh, 0);
        u16(lh, 0); u16(lh, 0);
        u32(lh, crc); u32(lh, n); u32(lh, n);
        u16(lh, e.name.length); u16(lh, 0);
        bytes(lh, e.name);
        bytes(lh, e.data);

        u32(central, 0x02014b50); u16(central, 20); u16(central, 20);
        u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0);
        u32(central, crc); u32(central, n); u32(central, n);
        u16(central, e.name.length); u16(central, 0); u16(central, 0);
        u16(central, 0); u16(central, 0); u32(central, 0);
        u32(central, offset);
        bytes(central, e.name);

        offset += lh.length;
        out = out.concat(lh);
    });

    var cdoff = out.length, eocd = [];
    out = out.concat(central);
    u32(eocd, 0x06054b50); u16(eocd, 0); u16(eocd, 0);
    u16(eocd, entries.length); u16(eocd, entries.length);
    u32(eocd, central.length); u32(eocd, cdoff); u16(eocd, 0);

    return new Uint8Array(out.concat(eocd));
}

testFeature("epub is assembled in spine order, not archive order", function(){
    function doc(t) { return '<html><body><p>' + t + '</p></body></html>'; }
    var opf = '<package xmlns="http://www.idpf.org/2007/opf" version="3.0"><manifest>' +
              '<item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/>' +
              '<item id="c2" href="c2.xhtml" media-type="application/xhtml+xml"/>' +
              '<item id="c3" href="c3.xhtml" media-type="application/xhtml+xml"/>' +
              '<item id="nv" href="nav.xhtml" media-type="application/xhtml+xml"/>' +
              '</manifest><spine><itemref idref="c1"/><itemref idref="c2"/>' +
              '<itemref idref="c3"/></spine></package>';
    var container =
        '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">' +
        '<rootfiles><rootfile full-path="OEBPS/book.opf" ' +
        'media-type="application/oebps-package+xml"/></rootfiles></container>';

    /* chapters written to the archive in REVERSE order on purpose */
    var epub = zipStore([
        {name: "mimetype",               data: "application/epub+zip"},
        {name: "META-INF/container.xml", data: container},
        {name: "OEBPS/book.opf",         data: opf},
        {name: "OEBPS/nav.xhtml",        data: doc("NAVONLYMARKER")},
        {name: "OEBPS/c3.xhtml",         data: doc("CHAPTERTHREE")},
        {name: "OEBPS/c2.xhtml",         data: doc("CHAPTERTWO")},
        {name: "OEBPS/c1.xhtml",         data: doc("CHAPTERONE")}
    ]);

    var p = scratchFile("ordered.epub");
    try {
        var fh = fopen(p, "w");
        fwrite(fh, epub);
        fclose(fh);

        if(totext.identify(p) !== "epub") {
            printf("\n  fixture identified as '%s'\n", totext.identify(p));
            return false;
        }
        var txt = totext.convertFile(p);
        var a = txt.indexOf("CHAPTERONE"),
            b = txt.indexOf("CHAPTERTWO"),
            c = txt.indexOf("CHAPTERTHREE");
        if(a < 0 || b < 0 || c < 0) {
            printf("\n  chapters missing: %d %d %d in %J\n", a, b, c, txt);
            return false;
        }
        if(!(a < b && b < c)) {
            printf("\n  archive order won: one=%d two=%d three=%d\n", a, b, c);
            return false;
        }
        /* nav.xhtml is in the manifest but not the spine */
        if(txt.indexOf("NAVONLYMARKER") >= 0) {
            printf("\n  non-spine document included\n");
            return false;
        }
        return true;
    } finally {
        unlinkQuiet(p);
    }
});

testFeature("epub skips documents that are not in the spine", function(){
    /* nav.xhtml is the navigation document, not reading content */
    var txt = totext.convertFile(testdir + "test.epub");
    if(txt.indexOf("Title Page") >= 0) {
        printf("\n  navigation document included in output\n");
        return false;
    }
    return true;
});

/* ================================================================
   gzip
   ================================================================

   Whether to hand pdftotext the on-disk path was decided by comparing the
   stripped filename with the original, which only differs when the name
   actually ended in .gz.  A gzip-compressed PDF that was NOT named .gz
   identified as a pdf (the bytes are decompressed before identification) but
   pdftotext was then given the still-compressed file and failed. */

skipOrTest("gzipped file not named .gz still converts", "test.pdf", function(){
    var gz = gzip(readFile(testdir + "test.pdf"));
    return withFile("compressed-but-named.pdf", gz, function(p){
        if(totext.identify(p) !== "pdf") {
            printf("\n  identified as '%s'\n", totext.identify(p));
            return false;
        }
        var txt = totext.convertFile(p);
        if(txt.indexOf("Squish the Stack") < 0) {
            printf("\n  conversion lost the text: %.120s\n", txt);
            return false;
        }
        return true;
    });
});

testFeature("gzipped man page still converts", function(){
    var txt = totext.convertFile(testdir + "test.1.gz");
    return txt.indexOf("manual pager") >= 0;
});

/* ================================================================
   XLSX: a spreadsheet is mostly not strings
   ================================================================

   This converter read xl/sharedStrings.xml and nothing else -- a
   deduplicated table of every distinct STRING in the workbook.  So every
   number was lost (numbers never enter the string table), every sheet name
   was lost, repeated values collapsed to one, and rows and columns were
   gone: a workbook of invoices came back as the column headings and the
   customer names, with not one amount, date or quantity in it.
   Measured against LibreOffice, a data workbook scored 0.432 recall.

   Nothing caught it for three separate reasons, and the third is the
   interesting one:

     - no test ever asserted xlsx CONTENT.  It appeared in the suite twice:
       an identify() expectation, and a list whose only check was
       "length >= 50".  6,371 characters of the wrong text passes that.
     - xlsx was the one format never compared against another
       implementation: pandoc cannot read it, and the LibreOffice filter
       used for the others is a Writer filter that does not apply to Calc.
     - and test.xlsx could not have revealed it anyway.  It is a typography
       demo exported to xlsx -- a spreadsheet fixture with no spreadsheet
       data in it.  The numbers it does lose are 0-9 and other small
       integers whose digits appear elsewhere in its prose, so even a token
       comparison scored it 0.991 while the format was broken.

   Hence data.xlsx, which contains what a spreadsheet actually contains. */

testFeature("xlsx keeps numbers, not just strings", function(){
    var txt = totext.convertFile(testdir + "data.xlsx");
    var want = ["249.95", "4249.15", "1875", "30994.6", "428", "12.75"];
    for(var i = 0; i < want.length; i++)
        if(txt.indexOf(want[i]) < 0) {
            printf("\n  %s missing; numbers are not in sharedStrings\n%s\n", want[i], txt);
            return false;
        }
    return true;
});

testFeature("xlsx resolves date-formatted cells", function(){
    /* a date in a workbook is a NUMBER; only the cell's format says
       otherwise, so without reading xl/styles.xml this is "46095" */
    var txt = totext.convertFile(testdir + "data.xlsx");
    if(txt.indexOf("2026-03-14") < 0) {
        printf("\n  date not resolved; got: %J\n", txt.substring(0, 200));
        return false;
    }
    return txt.indexOf("46095") < 0;
});

testFeature("xlsx keeps sheet names and row structure", function(){
    var txt = totext.convertFile(testdir + "data.xlsx");
    if(txt.indexOf("data") < 0) { printf("\n  sheet name lost\n"); return false; }
    /* a row's cells must stay on one line: "INV-90210" means nothing
       without the amount beside it */
    var row = txt.split("\n").filter(function(l){ return l.indexOf("INV-90210") >= 0; })[0];
    if(!row || row.indexOf("Acme Corporation") < 0 || row.indexOf("4249.15") < 0) {
        printf("\n  row adjacency lost; row=%J\n", row);
        return false;
    }
    return true;
});

testFeature("xlsx sheet names survive in the shipped fixture too", function(){
    var txt = totext.convertFile(testdir + "test.xlsx");
    return txt.indexOf("Sheet1") >= 0 && txt.indexOf("Chart Sheet") >= 0;
});

/* ================================================================
   EMAIL: .eml, mbox
   ================================================================

   Parsing is the vendored libetpan subset; what is tested here is the
   POLICY layered on it -- which alternative to keep, what becomes a
   document, and that an attachment goes back through the ordinary
   converters rather than a second implementation of them.

   The fixtures are built here rather than checked in: an .eml is text, and
   a message assembled in the test says what it is testing. */

function b64lines(buf) {
    var b = Duktape.enc("base64", buf), out = [], i;
    for(i = 0; i < b.length; i += 76) out.push(b.substring(i, i + 76));
    return out.join("\n");
}

/* a multipart/mixed of [ multipart/alternative(plain, html), <attachment> ] */
function mkEmail(subject, plain, html, attName, attMime, attBuf) {
    var m = "";
    m += "Return-Path: <admin@rampart.dev>\n";
    m += "Message-ID: <fixture-1@rampart.dev>\n";
    m += "Date: Sat, 06 Sep 2026 10:00:00 -0700\n";
    m += "From: Aaron Flin <admin@rampart.dev>\n";
    m += "To: Someone Else <someone@example.com>\n";
    m += "Subject: " + subject + "\n";
    m += "MIME-Version: 1.0\n";
    m += 'Content-Type: multipart/mixed; boundary="OUTER"\n\n';
    m += "--OUTER\n";
    m += 'Content-Type: multipart/alternative; boundary="INNER"\n\n';
    m += "--INNER\nContent-Type: text/plain; charset=\"utf-8\"\n";
    m += "Content-Transfer-Encoding: quoted-printable\n\n" + plain + "\n\n";
    m += "--INNER\nContent-Type: text/html; charset=\"utf-8\"\n\n" + html + "\n\n";
    m += "--INNER--\n\n";
    if(attBuf) {
        m += "--OUTER\n";
        m += 'Content-Type: ' + attMime + '; name="' + attName + '"\n';
        m += 'Content-Disposition: attachment; filename="' + attName + '"\n';
        m += "Content-Transfer-Encoding: base64\n\n" + b64lines(attBuf) + "\n\n";
    }
    m += "--OUTER--\n";
    return m;
}

/* "caf=C3=A9" is quoted-printable for "café"; the RFC 2047 subject is
   "Réunion" in base64.  Both must come back decoded. */
var eml_src = mkEmail("=?utf-8?B?UsOpdW5pb24gdHJpbWVzdHJpZWxsZQ==?=",
                      "PLAINBODYMARKER with a caf=C3=A9 accent.",
                      "<html><body><p>HTMLBODYMARKER rich</p></body></html>",
                      "quarterly.pdf", "application/pdf",
                      readFile(testdir + "test.pdf"));

testFeature("email is identified and converts to a plain string", function(){
    return withFile("msg.eml", eml_src, function(p){
        if(totext.identify(p) !== "email") {
            printf("\n  identified as '%s'\n", totext.identify(p));
            return false;
        }
        var s = totext.convertFile(p);
        return typeof s === "string" && s.indexOf("PLAINBODYMARKER") >= 0;
    });
});

testFeature("email quoted-printable and RFC 2047 are decoded", function(){
    return withFile("msg.eml", eml_src, function(p){
        var r = totext.convertFile(p, {details:true});
        if(r.documents[0].text.indexOf("café") < 0) {
            printf("\n  quoted-printable not decoded: %J\n",
                   r.documents[0].text.substring(0, 60));
            return false;
        }
        if(r.metaData.subject !== "Réunion trimestrielle") {
            printf("\n  subject not decoded: %J\n", r.metaData.subject);
            return false;
        }
        return true;
    });
});

testFeature("email yields one document per part", function(){
    return withFile("msg.eml", eml_src, function(p){
        var r = totext.convertFile(p, {details:true});
        if(r.documents.length !== 2) {
            printf("\n  documents=%d, expected body + attachment\n", r.documents.length);
            return false;
        }
        /* the invariant still holds when there is more than one */
        var join = r.documents.map(function(d){ return d.text; }).join(" ");
        if(join !== r.text || r.text !== totext.convertFile(p)) {
            printf("\n  text invariant broken with multiple documents\n");
            return false;
        }
        return r.mimeType === "message/rfc822"
            && r.documents[0].mimeType === "text/plain";
    });
});

/* the payoff: an attachment goes back through identify_content() and
   do_convert(), so a PDF inside a message is converted by the same code
   that converts a PDF on disk */
skipOrTest("email attachments are converted, not just listed", "test.pdf", function(){
    return withFile("msg.eml", eml_src, function(p){
        var r = totext.convertFile(p, {details:true});
        var att = r.documents[1];
        if(att.text.indexOf("Squish the Stack") < 0) {
            printf("\n  pdf attachment not converted: %J\n", att.text.substring(0, 60));
            return false;
        }
        return att.mimeType === "application/pdf" && att.title === "quarterly.pdf";
    });
});

testFeature("email title is the Subject, an attachment's is its filename", function(){
    return withFile("msg.eml", eml_src, function(p){
        var r = totext.convertFile(p, {details:true});
        return r.title === "Réunion trimestrielle"
            && r.documents[0].title === "Réunion trimestrielle"
            && r.documents[1].title === "quarterly.pdf";
    });
});

testFeature("email metaData carries the headers", function(){
    return withFile("msg.eml", eml_src, function(p){
        var m = totext.convertFile(p, {details:true}).metaData;
        if(m.from.indexOf("admin@rampart.dev") < 0 || m.to.indexOf("someone@example.com") < 0
           || m.messageId.indexOf("fixture-1") < 0 || m.date.indexOf("2026-09-06") !== 0) {
            printf("\n  metaData=%J\n", m);
            return false;
        }
        /* an attachment with no metadata of its own references the message's */
        var r = totext.convertFile(p, {details:true});
        return r.documents[1].metaData.subject === "Réunion trimestrielle";
    });
});

/* RFC 2046 orders alternatives worst to best; taking both would index every
   word twice, so exactly one is kept */
testFeature("multipart/alternative keeps one part, text by default", function(){
    return withFile("msg.eml", eml_src, function(p){
        var d = totext.convertFile(p, {details:true});
        var h = totext.convertFile(p, {details:true, prefer:"html"});
        if(d.text.indexOf("HTMLBODYMARKER") >= 0) {
            printf("\n  both alternatives were kept\n");
            return false;
        }
        return d.documents[0].text.indexOf("PLAINBODYMARKER") >= 0
            && h.documents[0].text.indexOf("HTMLBODYMARKER") >= 0;
    });
});

testFeature("attachments:false still lists the part, with no text", function(){
    return withFile("msg.eml", eml_src, function(p){
        var r = totext.convertFile(p, {details:true, attachments:false});
        if(r.documents.length !== 2) return false;
        /* the caller must still learn the part was there */
        return r.documents[1].text === ""
            && r.documents[1].title === "quarterly.pdf";
    });
});

testFeature("mbox yields a document per message", function(){
    var mbox = "";
    ["First message", "Second message", "Third message"].forEach(function(s, i){
        mbox += "From MAILER-DAEMON Sat Sep  6 10:0" + i + ":00 2026\n";
        mbox += "Message-ID: <m" + i + "@rampart.dev>\nMIME-Version: 1.0\n";
        mbox += "From: a@example.com\nSubject: " + s + "\n";
        mbox += "Content-Type: text/plain\n\nBODYMARKER" + i + " here.\n\n";
    });
    return withFile("box.mbox", mbox, function(p){
        if(totext.identify(p) !== "mbox") {
            printf("\n  identified as '%s'\n", totext.identify(p));
            return false;
        }
        var r = totext.convertFile(p, {details:true});
        if(r.documents.length !== 3) {
            printf("\n  documents=%d, expected 3\n", r.documents.length);
            return false;
        }
        /* the "From " line is mbox framing and must not leak into the text */
        if(r.text.indexOf("MAILER-DAEMON") >= 0) {
            printf("\n  From_ separator leaked into the text\n");
            return false;
        }
        return r.documents[0].title === "First message"
            && r.documents[2].text.indexOf("BODYMARKER2") >= 0;
    });
});

/* the email probe is a guess, not a signature, so it must sit BELOW the
 * extension guard -- the same rule the markdown and LaTeX probes follow */
testFeature("a .txt that looks like a message stays text", function(){
    var looksLike = "Subject: quarterly review\nFrom: the desk of the chairman\n\n" +
                    "Body of an ordinary memo, hard wrapped\nacross several lines.\n";
    return withFile("memo.txt", looksLike, function(p){
        var id = totext.identify(p);
        if(id !== "text") { printf("\n  identified as '%s'\n", id); return false; }
        return totext.convertFile(p).indexOf("Subject: quarterly review") >= 0;
    });
});

testFeature("a malformed message does not throw", function(){
    /* enough headers to be identified, then garbage */
    var junk = "Message-ID: <x@y>\nMIME-Version: 1.0\n" +
               'Content-Type: multipart/mixed; boundary="b"\n\n--b\n\x00\x01\x02 broken';
    return withFile("bad.eml", junk, function(p){
        var r = totext.convertFile(p, {details:true});
        return typeof r.text === "string" && r.documents.length >= 1;
    });
});

/* ================================================================
   Streaming callback, and deciding before decoding
   ================================================================

   convertFile(f, cb) hands one document to the callback and releases it
   before building the next, so nothing accumulates.  It exists because
   documents[] plus the joined text holds every byte of extracted text
   twice: fine for a report, not fine for a 2 GB mbox on a small machine.

   Measured on a real 2.13 GB mailbox (28,365 documents): the old code could
   not convert it AT ALL -- the whole file went into a duktape buffer, which
   caps just under 2 GiB, so it failed with "buffer too long".  With the
   input mapped instead, array mode peaks at 343 MB of unevictable memory
   and streaming at 194 MB, and streaming completes under a hard 400 MB cap
   with swap disabled. */

testFeature("callback: all three call forms return a count", function(){
    return withFile("msg.eml", eml_src, function(p){
        var a = 0, b = 0, c = 0;
        var na = totext.convertFile(p, function(d){ a++; });
        var nb = totext.convertFile(p, true, function(d){ b++; });
        var nc = totext.convertFile(p, {details:true}, function(d){ c++; });
        if(typeof na !== "number") {
            printf("\n  returned %s, expected a Number\n", typeof na);
            return false;
        }
        return na === nb && nb === nc && na === a && a === b && b === c;
    });
});

testFeature("callback: convert() streams a buffer too", function(){
    var n = totext.convert(stringToBuffer(eml_src), function(d){});
    return n === 2;
});

testFeature("callback: a single-document file calls back once", function(){
    var seen = 0, mt = "";
    var n = totext.convertFile(testdir + "test.docx", function(d){
        seen++; mt = d.mimeType;
    });
    if(n !== 1 || seen !== 1) {
        printf("\n  n=%d seen=%d, expected 1 and 1\n", n, seen);
        return false;
    }
    /* the entry has the same shape as a documents[] entry */
    return mt.indexOf("wordprocessingml") > 0;
});

testFeature("callback: the two modes agree", function(){
    /* the guarantee that replaces the text invariant when streaming:
       joining what the callback received reproduces convertFile(f) */
    var files = ["test.docx", "test.xlsx", "test.epub", "test.html", "data.xlsx"];
    var i, ok = true;
    files.forEach(function(f){
        var parts = [];
        var n = totext.convertFile(testdir + f, function(d){ parts.push(d.text); });
        var det = totext.convertFile(testdir + f, true);
        if(parts.join(" ") !== det.text || n !== det.documents.length) {
            printf("\n  %s: join or count differs (n=%d docs=%d)\n",
                   f, n, det.documents.length);
            ok = false;
        }
    });
    if(!ok) return false;
    return withFile("msg.eml", eml_src, function(p){
        var parts = [];
        var n = totext.convertFile(p, function(d){ parts.push(d.text); });
        var det = totext.convertFile(p, true);
        return parts.join(" ") === det.text && n === det.documents.length
            && parts.join(" ") === totext.convertFile(p);
    });
});

testFeature("callback: returning false stops the conversion", function(){
    var mbox = "";
    for(var i = 0; i < 6; i++) {
        mbox += "From MAILER-DAEMON Sat Sep  6 10:0" + i + ":00 2026\n";
        mbox += "Message-ID: <s" + i + "@x>\nMIME-Version: 1.0\nFrom: a@b.c\n";
        mbox += "Subject: Message " + i + "\nContent-Type: text/plain\n\nBODY" + i + "\n\n";
    }
    return withFile("box.mbox", mbox, function(p){
        var seen = 0;
        var n = totext.convertFile(p, function(d){ seen++; return seen < 3 ? true : false; });
        if(seen !== 3 || n !== 3) {
            printf("\n  saw %d documents, returned %d; expected 3 and 3\n", seen, n);
            return false;
        }
        /* and without the abort, all six arrive */
        return totext.convertFile(p, function(d){}) === 6;
    });
});

/* A callback that throws unwinds through the MIME walk, and everything the
   walk owns in C has to survive that: mailmime_parse() builds a tree whose
   mailmime_free() sat AFTER the walk, so the longjmp went straight past it
   and leaked the whole tree -- 1,700 bytes in 79 allocations PER throwing
   conversion, growing without bound in a server that hits a bad callback.
   Ownership now sits on the value stack, which unwinds. */
testFeature("callback: throwing repeatedly does not accumulate", function(){
    return withFile("boom.eml", eml_src, function(p){
        var caught = 0, i;
        for(i = 0; i < 200; i++) {
            try { totext.convertFile(p, function(d){ throw new Error("BOOM"); }); }
            catch(e) { if(/BOOM/.test(e.message)) caught++; }
        }
        /* the leak itself is only visible to a sanitizer; what is checkable
           here is that 200 unwinds in a row stay correct and do not crash */
        if(caught !== 200) { printf("\n  caught %d of 200\n", caught); return false; }
        var r = totext.convertFile(p, true);       /* still works afterwards */
        return r.documents.length > 0 && r.text.length > 0;
    });
});

testFeature("callback: an exception propagates out of convertFile", function(){
    return withFile("msg.eml", eml_src, function(p){
        try { totext.convertFile(p, function(d){ throw new Error("BOOM"); }); }
        catch(e) { return /BOOM/.test(e.message); }
        printf("\n  the callback's exception was swallowed\n");
        return false;
    });
});

/* ---- decide before decoding ----
 *
 * maxAttachment used to be checked AFTER the part was decoded, so a 400 MB
 * base64 video was fully materialised and then discarded for being too big:
 * the cap bounded nothing.  The decision is now taken from the ENCODED
 * length and the declared type, before any memory is spent -- but the
 * declared type is still only allowed to say NO, because mailers label real
 * documents application/octet-stream. */

testFeature("a non-convertible attachment is listed but never decoded", function(){
    /* 2 MB of "video" -- if this were decoded the part would hold it */
    var fake = new Array(700000).join("MP4");
    var src = mkEmail("With a video", "BODYTEXT here.", "<html><body>x</body></html>",
                      "clip.mp4", "video/mp4", stringToBuffer(fake));
    return withFile("vid.eml", src, function(p){
        var r = totext.convertFile(p, {details:true});
        if(r.documents.length !== 2) {
            printf("\n  documents=%d, expected body + listed attachment\n",
                   r.documents.length);
            return false;
        }
        var att = r.documents[1];
        if(att.text !== "") {
            printf("\n  video attachment produced %d chars of text\n", att.text.length);
            return false;
        }
        /* the caller must still learn the part was there */
        return att.title === "clip.mp4";
    });
});

skipOrTest("a mislabelled attachment is still sniffed and converted", "test.pdf",
function(){
    /* application/octet-stream with a meaningless name: the declared type
       must not be believed, or every real document sent by a lazy mailer
       would be skipped */
    var src = mkEmail("Mislabelled", "BODYTEXT here.", "<html><body>x</body></html>",
                      "attachment.dat", "application/octet-stream",
                      readFile(testdir + "test.pdf"));
    return withFile("mis.eml", src, function(p){
        var r = totext.convertFile(p, {details:true});
        var att = r.documents[1];
        if(att.text.indexOf("Squish the Stack") < 0) {
            printf("\n  sniffing failed; got %J\n", att.text.substring(0, 60));
            return false;
        }
        return att.mimeType === "application/pdf";
    });
});

testFeature("maxAttachment is applied before the decode", function(){
    var big = new Array(400000).join("PDFDATA");
    var src = mkEmail("Big one", "BODYTEXT here.", "<html><body>x</body></html>",
                      "big.pdf", "application/pdf", stringToBuffer(big));
    return withFile("big.eml", src, function(p){
        var r = totext.convertFile(p, {details:true, maxAttachment: 1024});
        return r.documents.length === 2 && r.documents[1].text === ""
            && r.documents[1].title === "big.pdf";
    });
});

/* ================================================================
   LARGE-ATTACHMENT ARENA
   ================================================================

   A part whose decoded size exceeds TT_ARENA_MIN (4 MB) is decoded in
   TT_ARENA_CHUNK (1 MB) pieces straight into a file-backed MAP_SHARED
   mapping, instead of into one heap block.  Heap memory is anonymous and
   cannot be reclaimed without swap, so the old path OOM-killed on a large
   attachment where this one only gets slower: measured on a 200 MB .docx
   attachment, peak RssAnon went from 200 MB to 1 MB, and under
   MemoryMax=128M with swap off the heap build is killed while this one
   completes.

   The risk the chunking introduces is SEAMS.  A base64 quad or a
   quoted-printable "=XX" straddling a 1 MB boundary must be resumed, not
   flushed as a partial token -- so these check the decoded bytes EXACTLY,
   not just the length.  Replacing the partial parse with the ordinary one
   makes the first two fail at the first seam.

   The fixtures have to be built at test time (they are far too big to
   commit) and 48 is divisible by 3, which is what makes that cheap: base64
   carries no state across 3-byte groups, so btoa(UNIT.repeat(n)) is exactly
   btoa(UNIT).repeat(n).  A 5 MB body is then two C-speed repeats -- 19 ms
   rather than the ~6 s an actual encode of megabytes costs in duktape. */

var ARENA_UNIT  = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJ=L";  /* 48, one '=' */
var ARENA_PLAIN = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKL";  /* 48, no '='  */
var ARENA_CHUNK = 1024 * 1024;      /* TT_ARENA_CHUNK in rampart-totext.c */

function arenaUnits(bytes) { return Math.ceil(bytes / 48); }
function arenaText(units)  { return ARENA_UNIT.repeat(units); }
function arenaB64(units)   { return (btoa(ARENA_UNIT) + "\n").repeat(units); }

function arenaPart(units, ctype, fname) {
    var h = "Content-Type: " + (ctype || "text/plain") + "\n";
    if(fname) h += 'Content-Disposition: attachment; filename="' + fname + '"\n';
    return h + "Content-Transfer-Encoding: base64\n\n" + arenaB64(units) + "\n";
}

function arenaMsg(parts) {
    var m = "From: a@b\nTo: c@d\nSubject: arena\nMIME-Version: 1.0\n" +
            'Content-Type: multipart/mixed; boundary="AB"\n\n';
    parts.forEach(function(p){ m += "--AB\n" + p; });
    return m + "--AB--\n";
}

/* differences show up as a diverging byte, so report the offset rather than
   dumping megabytes into the test log */
function arenaSame(got, want, label) {
    if(got === want) return true;
    var i = 0, n = Math.min(got.length, want.length);
    while(i < n && got.charAt(i) === want.charAt(i)) i++;
    printf("\n  %s: got %d chars, want %d; first difference at %d (seam %d)\n" +
           "    got  %s\n    want %s\n",
           label, got.length, want.length, i, Math.floor(i / ARENA_CHUNK),
           JSON.stringify(got.substr(i, 24)), JSON.stringify(want.substr(i, 24)));
    return false;
}

testFeature("arena: 5MB base64 part crosses decode seams intact", function(){
    var u = arenaUnits(5 * 1024 * 1024);
    return withFile("arena-b64.eml", arenaMsg([arenaPart(u)]), function(p){
        var r = totext.convertFile(p, true);
        return r.documents.length === 1
            && arenaSame(r.documents[0].text, arenaText(u), "base64 5MB");
    });
});

testFeature("arena: a quoted-printable escape split across a seam", function(){
    /* '=' lands on the last byte of chunk 0, so "3D" begins chunk 1: the
       decoder must carry the escape across, not emit a stray '3D'. */
    var head = ARENA_PLAIN.repeat(arenaUnits(ARENA_CHUNK)).substring(0, ARENA_CHUNK - 1);
    var tail = ARENA_PLAIN.repeat(arenaUnits(3.5 * 1024 * 1024));
    var body = "From: a@b\nTo: c@d\nSubject: qp\nMIME-Version: 1.0\n" +
               "Content-Type: text/plain\nContent-Transfer-Encoding: quoted-printable\n\n" +
               head + "=3D" + tail;
    return withFile("arena-qp.eml", body, function(p){
        var r = totext.convertFile(p, true);
        return arenaSame(r.documents[0].text, head + "=" + tail, "qp seam");
    });
});

testFeature("arena: 8bit part above the threshold", function(){
    var u = arenaUnits(5 * 1024 * 1024), txt = arenaText(u);
    var body = "From: a@b\nTo: c@d\nSubject: 8bit\nMIME-Version: 1.0\n" +
               "Content-Type: text/plain\nContent-Transfer-Encoding: 8bit\n\n" + txt;
    return withFile("arena-8bit.eml", body, function(p){
        var r = totext.convertFile(p, true);
        return arenaSame(r.documents[0].text, txt, "8bit 5MB");
    });
});

testFeature("arena: reused across parts without bleed or truncation", function(){
    /* 5, then 9 (which GROWS the mapping), then 5 again.  The third part is
       shorter than the second and sits in front of its leftovers, so a
       missing length or terminator shows up as the third document running
       on into the second's bytes. */
    var a = arenaUnits(5 * 1024 * 1024),
        b = arenaUnits(9 * 1024 * 1024);
    var src = arenaMsg([arenaPart(a), arenaPart(b), arenaPart(a)]);
    return withFile("arena-reuse.eml", src, function(p){
        var r = totext.convertFile(p, true);
        if(r.documents.length !== 3) {
            printf("\n  expected 3 documents, got %d\n", r.documents.length);
            return false;
        }
        return arenaSame(r.documents[0].text, arenaText(a), "part 1 (5MB)")
            && arenaSame(r.documents[1].text, arenaText(b), "part 2 (9MB, grows)")
            && arenaSame(r.documents[2].text, arenaText(a), "part 3 (5MB, reuse)");
    });
});

testFeature("arena: the streaming callback takes the same path", function(){
    var a = arenaUnits(5 * 1024 * 1024), got = [];
    var src = arenaMsg([arenaPart(a), arenaPart(a)]);
    return withFile("arena-stream.eml", src, function(p){
        var n = totext.convertFile(p, function(doc){ got.push(doc.text); });
        return n === 2 && got.length === 2
            && arenaSame(got[0], arenaText(a), "streamed doc 0")
            && arenaSame(got[1], arenaText(a), "streamed doc 1");
    });
});

testFeature("arena: a token-free window does not truncate the part", function(){
    /* 2 MB of line breaks in the middle of a base64 body: that window holds
       no complete quad, so the decoder reports no progress.  Treating that as
       end-of-part loses everything after it -- silently, and at exactly half
       the content, which is the kind of loss no length check on its own
       would flag as suspicious. */
    var u = arenaUnits(5 * 1024 * 1024), half = Math.floor(u / 2);
    var line = btoa(ARENA_UNIT) + "\n";
    var body = line.repeat(half) + "\n".repeat(2 * 1024 * 1024) + line.repeat(u - half);
    var src = "From: a@b\nTo: c@d\nSubject: gap\nMIME-Version: 1.0\n" +
              "Content-Type: text/plain\nContent-Transfer-Encoding: base64\n\n" + body;
    return withFile("arena-gap.eml", src, function(p){
        var r = totext.convertFile(p, true);
        return arenaSame(r.documents[0].text, arenaText(u), "token-free window");
    });
});

/* maxAttachment used to DEFAULT to 32 MB, so a 33 MB attachment came back
   with empty text and nothing to say why.  A size limit existed because a
   large attachment had to be held in anonymous memory; the arena removed
   that reason, so the default is now unlimited and only an explicit
   maxAttachment (tested above) skips anything. */
testFeature("arena: a 33MB attachment is no longer dropped by default", function(){
    var u = arenaUnits(33 * 1024 * 1024);
    var src = arenaMsg([arenaPart(u, "application/octet-stream", "big.txt")]);
    return withFile("arena-33mb.eml", src, function(p){
        var r = totext.convertFile(p, true);
        return r.documents.length === 1
            && arenaSame(r.documents[0].text, arenaText(u), "33MB attachment");
    });
});

/* ================================================================
   rampart-thread
   ================================================================

   rampart-totext is used from a threaded server, so "does it convert" is
   only half the question.  Three things are checked here:

     - the module object survives being copied into a thread as a global,
       and a fresh require() inside a thread works too;
     - concurrent conversion produces the RIGHT answers.  A race in shared
       state shows up as one thread receiving another's result, which no
       crash test would notice, so every thread converts a different file
       and its output is compared against the single-threaded answer;
     - the libetpan MIME path survives being hammered.  mmapstring keeps a
       PROCESS-GLOBAL refcount hashtable that mmap_string_unref() touches
       for every decoded part.  Its mutex is compiled in only when
       extern/libetpan is built with LIBETPAN_REENTRANT and HAVE_PTHREAD_H;
       with those removed this test segfaults within a few hundred
       conversions, which is how it was found.

   Everything a thread needs must be a GLOBAL and must be set before the
   thread is created -- closures do not survive thr.exec(), and globals are
   copied at creation time.  Hence the top-level names below. */

var THREAD_FILES  = ["test.docx", "test.odt", "test.html", "test.1.gz",
                     "test.xlsx", "test.rtf", "test.xml", "test.md"];
var THREAD_EXPECT = {};
var THREAD_EML    = "";
var THREAD_EMLLEN = 0;
var THREAD_ITER   = 10;

function ttWorker(id) {
    var mine = {}, i, iter, f;
    for(iter = 0; iter < THREAD_ITER; iter++)
        for(i = 0; i < THREAD_FILES.length; i++) {
            /* stagger, so threads are on different files at the same moment */
            f = THREAD_FILES[(i + id) % THREAD_FILES.length];
            mine[f] = totext.convertFile(testdir + f).length;
        }
    rampart.thread.put("ttw" + id, mine);
    return id;
}

function ttFresh(id) {
    /* a module required INSIDE the thread, not inherited from the parent */
    var t = require("rampart-totext"), mine = {}, i;
    for(i = 0; i < THREAD_FILES.length; i++)
        mine[THREAD_FILES[i]] = t.convertFile(testdir + THREAD_FILES[i]).length;
    rampart.thread.put("ttf" + id, mine);
    return id;
}

function ttMail(id) {
    var bad = 0, i, r;
    for(i = 0; i < 40; i++) {
        r = totext.convertFile(THREAD_EML, true);
        if(r.text.length !== THREAD_EMLLEN) bad++;
        /* the alternative must still yield exactly one document per part */
        if(r.documents.length !== 2) bad++;
    }
    rampart.thread.put("ttm" + id, bad);
    return id;
}

function ttRun(prefix, fn, n, timeoutMs) {
    var threads = [], results = [], i;
    for(i = 0; i < n; i++) threads.push(new rampart.thread());
    for(i = 0; i < n; i++) threads[i].exec(fn, i);
    for(i = 0; i < n; i++)
        results.push(rampart.thread.getwait(prefix + i, timeoutMs || 120000));
    for(i = 0; i < n; i++) threads[i].close();
    return results;
}

/* built once, in the main thread, before any thread exists */
(function setupThreadFixtures(){
    var i;
    for(i = 0; i < THREAD_FILES.length; i++)
        THREAD_EXPECT[THREAD_FILES[i]] =
            totext.convertFile(testdir + THREAD_FILES[i]).length;

    THREAD_EML = scratchFile("threads.eml");
    fprintf(THREAD_EML, "%s",
        mkEmail("Threaded", "THREADBODY with a caf=C3=A9 accent.",
                "<html><body><p>HTMLALT</p></body></html>",
                "note.txt", "text/plain", stringToBuffer("ATTACHTEXT in a thread\n")));
    THREAD_EMLLEN = totext.convertFile(THREAD_EML).length;
})();

testFeature("thread: the module copied in as a global converts correctly", function(){
    var res = ttRun("ttw", ttWorker, 4), i, f;
    for(i = 0; i < res.length; i++) {
        if(!res[i]) { printf("\n  thread %d returned nothing\n", i); return false; }
        for(f in res[i])
            if(res[i][f] !== THREAD_EXPECT[f]) {
                printf("\n  thread %d: %s got %J, single-threaded gives %J\n",
                       i, f, res[i][f], THREAD_EXPECT[f]);
                return false;
            }
    }
    return true;
});

testFeature("thread: require() inside a thread converts correctly", function(){
    var res = ttRun("ttf", ttFresh, 4), i, f;
    for(i = 0; i < res.length; i++) {
        if(!res[i]) { printf("\n  thread %d returned nothing\n", i); return false; }
        for(f in res[i])
            if(res[i][f] !== THREAD_EXPECT[f]) {
                printf("\n  thread %d: %s got %J, expected %J\n",
                       i, f, res[i][f], THREAD_EXPECT[f]);
                return false;
            }
    }
    return true;
});

testFeature("thread: concurrent MIME parsing is not a data race", function(){
    /* 8 x 40 messages, each with several parts -- a few thousand calls
       through mmap_string_unref() and its global hashtable */
    var res = ttRun("ttm", ttMail, 8, 180000), i, bad = 0;
    for(i = 0; i < res.length; i++) {
        if(res[i] === undefined) { printf("\n  thread %d timed out\n", i); return false; }
        bad += res[i];
    }
    if(bad) printf("\n  %d wrong results across 8 threads\n", bad);
    return bad === 0;
});

unlinkQuiet(THREAD_EML);

/* ================================================================
   Crash safety
   ================================================================

   emit_braced() and convert_latex_range() call each other once per brace
   level, so nesting depth WAS C stack depth: about 40 KB of "{{{{..." killed
   the process with SIGSEGV, on input that arrives from convert() without ever
   being trusted.  If this test crashes the interpreter the bug is back --
   which is why it runs last of the non-OCR tests: a segfault here takes the
   whole run down and would otherwise hide everything after it. */

testFeature("deeply nested latex braces do not blow the stack", function(){
    var n = 100000;
    var deep = "Start " + new Array(n + 1).join("{") + "DeepText" +
               new Array(n + 1).join("}") + " End\n";
    return withFile("deep.tex", deep, function(p){
        var txt = totext.convertFile(p).replace(/\s+/g, " ").trim();
        if(txt !== "Start DeepText End") {
            printf("\n  got %J\n", txt.substring(0, 80));
            return false;
        }
        return true;
    });
});

/* ================================================================
   OCR: image files and scanned PDFs through rampart-ocr
   ================================================================
 *
 * rampart-ocr ships in the separate rampart-langtools package, so the tests
 * that need a reader are skipped, not failed, when it is not installed.
 * The fixtures (ocr-memo.png, ocr-memo-3pages.tif, ocr-scan.pdf) are the
 * synthetic page generated for the rampart-ocr test suite from
 * rampart-langtools/test_docs/mk/memo.ps: original text, base-14 fonts,
 * no third-party content, public domain. */

/* the recognizer sometimes drops the spaces in this all-caps heading on
   JPEG-sourced renders (the scan PDF wraps a JPEG), so accept either */
var ocr_heading = /RAMPART\s*LANGTOOLS\s*TEST\s*DOCUMENT/;

/* identification and the no-reader error need no rampart-ocr at all */
testFeature("identify image formats", function(){
    return totext.identify(testdir + "ocr-memo.png") === "png"
        && totext.identify(testdir + "ocr-memo-3pages.tif") === "tiff"
        && totext.identify(testdir + "ocr-scan.pdf") === "pdf";
});

testFeature("image without a reader throws with the setOcr hint", function(){
    /* outside the try on purpose: if setOcr is missing (an old rampart-totext),
       that must fail here, not be caught below and mistaken for the hint --
       "property 'setOcr' of [object Object]" would match /setOcr/ too */
    totext.setOcr(false);
    try { totext.convertFile(testdir + "ocr-memo.png"); }
    catch(e) {
        if(/needs an OCR reader/.test(e.message) && /setOcr/.test(e.message))
            return true;
        printf("\n  wrong error: %s\n", e.message.split("\n")[0]);
        return false;
    }
    printf("\n  did not throw\n");
    return false;
});

var ocr_reader = null, ocr_why = "rampart-ocr not installed";
try {
    var ocr_mod = require("rampart-ocr");
    var ocr_models = require("rampart-models");
    ocr_why = "rampart-ocr model not available";
    ocr_reader = ocr_mod.init(ocr_models.ocrGet("ppocr-v5"), {threads: 0});
} catch(e) {}

function ocrTest(name, fn, needPoppler) {
    if(!ocr_reader)
        testFeature.skip(name, ocr_why);
    else if(needPoppler && !has_pdftoppm)
        testFeature.skip(name, "pdftoppm not installed");
    else
        testFeature(name, fn);
}

ocrTest("ocr: png via setOcr(reader)", function(){
    totext.setOcr(ocr_reader);
    var txt = totext.convertFile(testdir + "ocr-memo.png");
    if(!ocr_heading.test(txt)) {
        printf("\n  heading not found; output starts: %.120s\n", txt);
        return false;
    }
    return true;
});

ocrTest("ocr: multi-page tiff, pages in details", function(){
    var ret = totext.convertFile(testdir + "ocr-memo-3pages.tif", {details: true});
    var pages = ret.text.split("\f");
    if(ret.mimeType !== "image/tiff" || ret.ocr !== true || pages.length !== 3
       || !ret.pages || ret.pages.length !== 3 || ret.pages[2].page !== 2) {
        printf("\n  mimeType=%s ocr=%s textPages=%d pages=%d\n",
               ret.mimeType, ret.ocr, pages.length, ret.pages ? ret.pages.length : -1);
        return false;
    }
    /* top-level ocr/pages describe documents[0] and are the SAME objects */
    if(ret.documents[0].pages !== ret.pages || ret.documents[0].ocr !== true) {
        printf("\n  documents[0] does not share the top-level ocr/pages\n");
        return false;
    }
    return pages.every(function(p){ return ocr_heading.test(p); });
});

ocrTest("ocr: scanned pdf is rasterized and read", function(){
    var ret = totext.convertFile(testdir + "ocr-scan.pdf", true);
    if(ret.ocr !== true || ret.mimeType !== "application/pdf"
       || !ret.pages || ret.pages.length !== 1 || ret.pages[0].page !== 0) {
        printf("\n  ocr=%s mimeType=%s pages=%d\n", ret.ocr, ret.mimeType,
               ret.pages ? ret.pages.length : -1);
        return false;
    }
    return ocr_heading.test(ret.text);
}, true);

ocrTest("ocr: per-call {ocr: reader} with none set", function(){
    totext.setOcr(false);
    var txt = totext.convertFile(testdir + "ocr-memo.png", {ocr: ocr_reader});
    return ocr_heading.test(txt);
});

if(ocr_reader) ocr_reader.destroy();
totext.setOcr(false);

/* the scratch dir is empty by now (each test unlinks in a finally); remove it
   whatever happened above, so a failed run leaves nothing in TMPDIR either */
try { rmdir(scratch); } catch(e) {}

testFeature.exit();
