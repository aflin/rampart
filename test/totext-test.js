
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

has_pdftotext = has_pdftotext.exitStatus ===0 ? !!has_pdftotext.stdout : false;
has_textutil = has_textutil.exitStatus ===0 ? !!has_textutil.stdout : false;
has_catdoc = has_catdoc.exitStatus ===0 ? !!has_catdoc.stdout : false;

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

testFeature("no stray xml tags in output", function(){
    var txt = totext.convertFile(testdir + "test.xml");
    return txt.indexOf("<para") < 0 && txt.indexOf("<link") < 0;
});

testFeature("no markdown syntax in output", function(){
    var txt = totext.convertFile(testdir + "test.md");
    return txt.indexOf("::::") < 0 && txt.indexOf("{.") < 0;
});

testFeature("no latex commands in output", function(){
    var txt = totext.convertFile(testdir + "test.latex");
    return txt.indexOf("\\section") < 0 && txt.indexOf("\\href") < 0;
});

testFeature("no rtf commands in output", function(){
    var txt = totext.convertFile(testdir + "test.rtf");
    return txt.indexOf("\\par") < 0 && txt.indexOf("\\f0") < 0;
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

/* ---- a .txt that LOOKS like markdown stays text ----
 *
 * The markdown probe is a statistical guess between two kinds of text,
 * and it used to outrank the extension: five of 9,822 RFCs -- plain
 * hard-wrapped .txt, full of ASCII box-drawing -- scored as markdown
 * off their '****' rules and '#' column labels, and cmark then joined
 * their lines and ate their structural characters.  An explicit text
 * extension now wins over the guess; a file with NO meaningful
 * extension still gets it.  Binary signatures (a PDF named .txt) are
 * unaffected -- those are signatures, not statistics. */

/* Scratch space for the two tests below, which have to CREATE a file to name
   it themselves.  It is deliberately not convtest/: that holds the fixtures
   and, in an installed tree, belongs to root -- writing there fails for an
   ordinary user, and dirties the install when it succeeds.  Made here, removed
   at the end, and every file removed in a `finally` so a test that fails or
   throws still leaves nothing behind. */
var scratch = (process.env.TMPDIR || "/tmp") + "/rampart-totext-test-" + process.getpid();
mkdir(scratch);

function scratchFile(name) { return scratch + "/" + name; }
function unlinkQuiet(p)    { try { rmFile(p); } catch(e) {} }

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

testFeature("pdf named .txt is still a pdf", function(){
    var liar = scratchFile("liar.txt");
    try {
        fprintf(liar, "%s", readFile(testdir + "test.pdf"));
        return totext.identify(liar) === "pdf";
    } finally {
        unlinkQuiet(liar);
    }
});

/* ---- OCR: image files and scanned PDFs through rampart-ocr ----
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

var has_pdftoppm = exec("which", "pdftoppm");
has_pdftoppm = has_pdftoppm.exitStatus === 0 ? !!has_pdftoppm.stdout : false;

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

/* the scratch dir is empty by now (each test unlinks in a finally); remove it
   whatever happened above, so a failed run leaves nothing in TMPDIR either */
try { rmdir(scratch); } catch(e) {}

testFeature.exit();
