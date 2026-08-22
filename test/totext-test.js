
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

testFeature("md-looking .txt is identified as text", function(){
    var art = "#  Chart 1\n" +
              "****************************\n" +
              "**  boxes  **  and rules  **\n" +
              "****************************\n" +
              "#  see ```figure``` above\n";
    fprintf(testdir + "mdish.txt", "%s", art);
    var asTxt = totext.identify(testdir + "mdish.txt");
    /* same bytes, no extension to trust: the sniffer may guess */
    fprintf(testdir + "mdish.data", "%s", art);
    var asData = totext.identify(testdir + "mdish.data");
    rmFile(testdir + "mdish.txt");
    rmFile(testdir + "mdish.data");
    if(asTxt !== "text") {
        printf("\n  .txt identified as '%s'\n", asTxt);
        return false;
    }
    return asData === "markdown";
});

testFeature("pdf named .txt is still a pdf", function(){
    var buf = readFile(testdir + "test.pdf");
    fprintf(testdir + "liar.txt", "%s", buf);
    var got = totext.identify(testdir + "liar.txt");
    rmFile(testdir + "liar.txt");
    return got === "pdf";
});

testFeature.exit();
