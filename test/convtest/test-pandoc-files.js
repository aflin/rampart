/* Sweep the pandoc-generated office corpora (83 docx, 52 odt, 51 pptx).
 *
 * This script used to assert only that conversion returned a non-empty string
 * with a paragraph break in it.  Metadata satisfies that: when
 * docx_find_document_path() resolved to docProps/app.xml instead of the
 * document body, 50 of the 83 docx files converted to "Normal.dotm 2 1 67 388
 * Microsoft Office Word ..." and every check here still passed.
 *
 * So the real check is now an ORACLE.  For each file we go into the archive
 * ourselves, find the part that holds the text, and pull out the longest word
 * in it -- concatenating the text nodes of each block with NO separator, the
 * way the format itself defines them.  That word must appear in the converted
 * output.  It fails if the wrong archive member was read, if the text was
 * dropped, and -- because a word split across runs is only one word when the
 * runs are joined -- if a separator was inserted inside a word.
 */

rampart.globalize(rampart.utils);

var totext = require("rampart-totext");
var testdir = process.scriptPath + "/";
var passed = 0, failed = 0, errors = [];

/* files that contain no extractable text (images only, formulas, empty styled paragraphs, etc.)
   These should produce empty output — if they don't, something is wrong. */
var expect_empty = {
    "pandoc-odt/formula.odt": true,
    "pandoc-odt/hiddenTextByVariable.odt": true,
    "pandoc-odt/imageRelative.odt": true,
    "pandoc-odt/variable.odt": true,
    "pandoc-odt/image.odt": true,
    "pandoc-odt/horizontalRule.odt": true,
};

/* ---------------- archive-side oracle ---------------- */

function entText(file, name) {
    try { return bufferToString(zipGet(file, name)); } catch(e) { return null; }
}

function decodeEnt(s) {
    return s.replace(/&#x([0-9a-fA-F]+);/g, function(_, h){
                return String.fromCharCode(parseInt(h, 16)); })
            .replace(/&#(\d+);/g, function(_, d){
                return String.fromCharCode(parseInt(d, 10)); })
            .replace(/&lt;/g, "<").replace(/&gt;/g, ">")
            .replace(/&quot;/g, '"').replace(/&apos;/g, "'")
            .replace(/&amp;/g, "&");
}

function longestIn(text, best) {
    var words = text.split(/[^A-Za-z0-9]+/);
    for(var i = 0; i < words.length; i++)
        if(words[i].length > best.length) best = words[i];
    return best;
}

/* OOXML: each <w:p>/<a:p> is a block; inside it, <w:t>/<a:t> runs concatenate
   with nothing between them, while <w:br>/<w:tab> are whitespace. */
function ooxmlLongestWord(xml, blockTag, textTag) {
    var blocks = xml.split(new RegExp("</" + blockTag + ">"));
    var best = "";

    for(var b = 0; b < blocks.length; b++) {
        var re = new RegExp("<" + textTag + "\\b[^>]*>([\\s\\S]*?)</" + textTag + ">"
                            + "|<[wa]:(?:br|tab|cr)\\b[^>]*>", "g");
        var txt = "", m;
        while((m = re.exec(blocks[b])) !== null)
            txt += (m[1] === undefined) ? " " : decodeEnt(m[1]);
        best = longestIn(txt, best);
    }
    return best;
}

/* ODF: each <text:p>/<text:h> is a block.  Only inline formatting joins the
   text on either side of it into one word -- a <text:span> is the two halves
   of a word, but a footnote nested in the middle of a paragraph is its own
   block and must not be concatenated onto the word in front of it. */
function odfLongestWord(xml) {
    var re = /<text:(p|h)\b[^>]*>([\s\S]*?)<\/text:\1>/g, m, best = "";

    while((m = re.exec(xml)) !== null) {
        var inner = m[2]
            .replace(/<\/?text:(?:span|a|sequence)\b[^>]*>/g, "")
            .replace(/<[^>]*>/g, " ");
        best = longestIn(decodeEnt(inner), best);
    }
    return best;
}

/* the docx main part, resolved the way the format says: the relationship
   whose Type ENDS in /officeDocument */
function docxMainPart(file) {
    var rels = entText(file, "_rels/.rels");
    if(rels) {
        var re = /<Relationship\b[^>]*>/g, m;
        while((m = re.exec(rels)) !== null) {
            var type = (m[0].match(/\bType="([^"]*)"/) || [])[1];
            var tgt  = (m[0].match(/\bTarget="([^"]*)"/) || [])[1];
            if(type && tgt && /\/officeDocument$/.test(type))
                return tgt.replace(/^\//, "");
        }
    }
    return "word/document.xml";
}

/* the word the conversion must contain, or "" if the document has no word
   long enough to be a meaningful marker */
function expectedWord(file, kind) {
    var xml, best = "";

    if(kind === "docx") {
        xml = entText(file, docxMainPart(file));
        if(xml) best = ooxmlLongestWord(xml, "w:p", "w:t");
    } else if(kind === "odt") {
        xml = entText(file, "content.xml");
        if(xml) best = odfLongestWord(xml);
    } else if(kind === "pptx") {
        var names = zipList(file);
        for(var i = 0; i < names.length; i++) {
            if(!/^ppt\/slides\/slide\d+\.xml$/.test(names[i])) continue;
            xml = entText(file, names[i]);
            if(xml) {
                var w = ooxmlLongestWord(xml, "a:p", "a:t");
                if(w.length > best.length) best = w;
            }
        }
    }
    return best.length >= 5 ? best : "";
}

/* strings that only ever appear in docProps/app.xml or docProps/core.xml */
var metadata_markers = /Microsoft Office Word|Normal\.dotm|Microsoft Macintosh Word/;

/* ---------------- the sweep ---------------- */

function testFile(file, expectedType) {
    var fullpath = testdir + file;
    try {
        var id = totext.identify(fullpath);
        if(id !== expectedType) {
            errors.push(file + ": identified as '" + id + "', expected '" + expectedType + "'");
            failed++;
            return;
        }
        var txt = totext.convertFile(fullpath);
        if(typeof txt !== 'string') {
            errors.push(file + ": convert returned " + typeof txt);
            failed++;
            return;
        }

        if(expect_empty[file]) {
            if(txt.length > 0) {
                errors.push(file + ": expected empty output, got " + txt.length + " bytes");
                failed++;
                return;
            }
            passed++;
            return;
        }

        if(txt.length < 1) {
            errors.push(file + ": empty output");
            failed++;
            return;
        }

        /* the document's own text, not its metadata */
        if(metadata_markers.test(txt)) {
            errors.push(file + ": converted to docProps metadata, not the body");
            failed++;
            return;
        }

        /* the oracle: a word taken from the archive must survive conversion */
        var want = expectedWord(fullpath, expectedType);
        if(want && txt.indexOf(want) < 0) {
            errors.push(file + ": '" + want + "' from the archive is missing from the"
                        + " conversion (wrong part, dropped text, or a word split)");
            failed++;
            return;
        }

        /* check for paragraph separation — only flag if document is long enough
           to reasonably expect multiple paragraphs */
        var has_paras = txt.indexOf("\n\n") >= 0 || txt.length < 5000;
        if(!has_paras) {
            errors.push(file + ": no paragraph breaks (len=" + txt.length + ")");
            failed++;
            return;
        }
        passed++;
    } catch(e) {
        errors.push(file + ": EXCEPTION: " + e.message);
        failed++;
    }
}

function sweep(subdir, ext, type) {
    var files = readDir(testdir + subdir);
    for(var i = 0; i < files.length; i++) {
        if(files[i].match(ext))
            testFile(subdir + "/" + files[i], type);
    }
}

sweep("pandoc-docx", /\.docx$/, "docx");
sweep("pandoc-odt",  /\.odt$/,  "odt");
sweep("pandoc-pptx", /\.pptx$/, "pptx");

printf("Results: %d passed, %d failed out of %d total\n", passed, failed, passed + failed);
if(errors.length > 0) {
    printf("\nFailures:\n");
    for(var i = 0; i < errors.length; i++)
        printf("  %s\n", errors[i]);
}

process.exit(failed ? 1 : 0);
