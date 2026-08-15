/* rampart.utils.toUtf8 / detectCharset / bufferToString(charset)
 *
 * A String in rampart holds UTF-8, and bufferToString() used to hand back
 * whatever bytes it was given.  A String carrying an invalid byte looks
 * normal and then throws a bare "internal error" from the first String
 * operation that touches it, naming neither the cause nor the input.
 *
 * Found on a corpus of RFCs: rfc2166.txt is 75 KB of 7-bit ASCII with
 * eighteen Windows-1252 smart quotes in it, and it killed a 599-document
 * ingest at document 71 with that message and nothing else.
 *
 * Every case below therefore checks TWO things: that the text came out
 * right, and that the result survives a String operation.  The second is
 * the one that was actually broken.
 */
rampart.globalize(rampart.utils);

var testFeature = new (require('./test-feature.js'))({prefix: "charset"});

var must      = testFeature.must;
var mustEq    = testFeature.mustEq;
var mustThrow = testFeature.mustThrow;

/* bytes as a buffer, which is what these functions take */
function bytes(arr) {
    var b = new Uint8Array(arr.length);
    for (var i = 0; i < arr.length; i++) b[i] = arr[i];
    return b.buffer;
}

/* the check that matters: a String that cannot be operated on is not a
   usable String, however correct it looks when printed */
function usable(s) {
    try { String(s).replace(/\s+/g, " "); return true; }
    catch (e) { return false; }
}

/* ------------------------------------------------------------------ *
 * detection
 * ------------------------------------------------------------------ */

testFeature("detect ascii and valid utf-8 as utf-8", function() {
    var d = detectCharset(bytes([0x68, 0x69]));
    mustEq(d.charset, "UTF-8", "ascii charset");
    mustEq(d.ascii, true, "ascii flag");

    /* 'a' + U+00E9 as real utf-8 */
    d = detectCharset(bytes([0x61, 0xC3, 0xA9]));
    mustEq(d.charset, "UTF-8", "utf-8 multibyte charset");
    mustEq(d.source, "utf-8", "utf-8 multibyte source");
    mustEq(d.ascii, false, "multibyte is not ascii");
});

testFeature("byte order marks decide outright", function() {
    var d = detectCharset(bytes([0xEF, 0xBB, 0xBF, 0x68]));
    mustEq(d.charset, "UTF-8", "utf-8 bom charset");
    mustEq(d.source, "bom", "utf-8 bom source");

    mustEq(detectCharset(bytes([0xFF, 0xFE, 0x68, 0x00])).charset,
           "UTF-16LE", "utf-16le bom");
    mustEq(detectCharset(bytes([0xFE, 0xFF, 0x00, 0x68])).charset,
           "UTF-16BE", "utf-16be bom");
});

testFeature("a stray high byte means windows-1252", function() {
    /* the rfc2166 shape: ascii with one cp1252 quote in it */
    var d = detectCharset(bytes([0x61, 0x93, 0x62]));
    mustEq(d.charset, "WINDOWS-1252", "assumed charset");
    mustEq(d.source, "assumed", "assumed source");
});

testFeature("a declared label beats inspection", function() {
    mustEq(detectCharset(bytes([0x61, 0x93]), "iso-8859-1").source,
           "declared", "declared source");
});

testFeature("invalid utf-8 forms are rejected, not accepted", function() {
    /* An accepting decoder is HOW invalid text reaches a String in the
       first place, so each of these must fall out of utf-8. */
    mustEq(detectCharset(bytes([0xC0, 0xAF])).charset, "WINDOWS-1252",
           "overlong '/'");
    mustEq(detectCharset(bytes([0xED, 0xA0, 0x80])).charset, "WINDOWS-1252",
           "lone surrogate");
    mustEq(detectCharset(bytes([0xF5, 0x80, 0x80, 0x80])).charset,
           "WINDOWS-1252", "beyond U+10FFFF");
});

/* ------------------------------------------------------------------ *
 * conversion
 * ------------------------------------------------------------------ */

testFeature("toUtf8 turns windows-1252 quotes into real quotes", function() {
    var s = toUtf8(bytes([0x61, 0x93, 0x62, 0x94, 0x63]));
    mustEq(s, "a“b”c", "converted text");
    must(usable(s), "result survives a String operation");
});

testFeature("toUtf8 passes valid text through untouched", function() {
    mustEq(toUtf8(bytes([0x68, 0x69])), "hi", "ascii");
    mustEq(toUtf8(bytes([0x61, 0xC3, 0xA9])), "aé", "utf-8 multibyte");
    mustEq(toUtf8(bytes([0xEF, 0xBB, 0xBF, 0x68, 0x69])), "hi", "bom stripped");
});

testFeature("toUtf8 honours a declared charset", function() {
    mustEq(toUtf8(bytes([0xC0, 0xC1]), {charset: "iso-8859-1"}), "ÀÁ",
           "iso-8859-1 high bytes");

    /* KOI8-R shares byte values with the Latin sets and can only be got
       right by being told */
    var ru = toUtf8(bytes([0xF0, 0xD2, 0xC9]), {charset: "koi8-r"});
    mustEq(ru.length, 3, "koi8-r length");
    must(ru.charCodeAt(0) > 0x400, "koi8-r produced cyrillic");

    /* a multi-byte legacy encoding */
    mustEq(toUtf8(bytes([0x81, 0x62]), {charset: "shift_jis"}), "｜",
           "shift_jis double byte");
});

testFeature("toUtf8 details report what was decided", function() {
    var d = toUtf8(bytes([0x61, 0x93]), {details: true});
    mustEq(d.charset, "WINDOWS-1252", "details charset");
    mustEq(d.source, "assumed", "details source");
    mustEq(d.tail, 0, "no tail");
    must(usable(d.text), "details text is usable");
});

testFeature("undecodable bytes are replaced and counted", function() {
    /* 0xFD is unassigned in shift_jis.  0x81 0x62 would NOT work here --
       it is a valid shift_jis character, which an earlier version of
       this test got wrong. */
    var d = toUtf8(bytes([0x61, 0xFD, 0x62]),
                   {charset: "shift_jis", details: true});
    mustEq(d.repairs, 1, "one repair");
    must(d.text.indexOf("�") > 0, "replacement character present");
    must(usable(d.text), "repaired text is usable");
});

testFeature("fatal throws instead of replacing", function() {
    mustThrow(function() {
        toUtf8(bytes([0x61, 0xFD, 0x62]), {charset: "shift_jis", fatal: true});
    }, "undecodable shift_jis");
    mustThrow(function() {
        toUtf8(bytes([0x61, 0xFF, 0x62]), {charset: "utf-8", fatal: true});
    }, "invalid utf-8");
});

testFeature("a truncated final sequence is handled, not passed on", function() {
    /* Valid utf-8 right up to where it stops mid character.  This is a
       chunk boundary, not another encoding -- but a String ending mid
       character still throws on first use, so it cannot be handed back
       as-is.  An earlier version of this code called it "valid utf-8"
       and copied it through, reproducing the original bug. */
    var d = toUtf8(bytes([0x61, 0xC3]), {details: true});
    mustEq(d.repairs, 1, "truncated tail replaced");
    mustEq(d.tail, 0, "nothing held back");
    must(usable(d.text), "result is usable");

    /* three-byte character cut after two */
    d = toUtf8(bytes([0x61, 0xE2, 0x80]), {details: true});
    mustEq(d.repairs, 1, "truncated 3-byte sequence replaced");
    must(usable(d.text), "result is usable");

    /* streaming keeps the partial sequence for the next chunk instead */
    d = toUtf8(bytes([0x61, 0xC3]), {details: true, stream: true});
    mustEq(d.tail, 1, "tail held back");
    mustEq(d.repairs, 0, "nothing repaired when streaming");
    mustEq(d.text.length, 1, "only the complete part returned");
    must(usable(d.text), "streamed result is usable");
});

/* ------------------------------------------------------------------ *
 * bufferToString — the function that carried the footgun
 * ------------------------------------------------------------------ */

testFeature("bufferToString repairs by default", function() {
    var s = bufferToString(bytes([0x61, 0x93, 0x62]));
    mustEq(s, "a“b", "converted");
    must(usable(s), "result survives a String operation");
});

testFeature("bufferToString leaves valid text alone", function() {
    mustEq(bufferToString(bytes([0x68, 0x69])), "hi", "ascii");
    mustEq(bufferToString(bytes([0x61, 0xC3, 0xA9])), "aé", "utf-8");
});

testFeature("bufferToString takes a charset", function() {
    mustEq(bufferToString(bytes([0xC0, 0xC1]), "iso-8859-1"), "ÀÁ",
           "string form");
    mustEq(bufferToString(bytes([0xC0, 0xC1]), {charset: "iso-8859-1"}), "ÀÁ",
           "object form");
});

/* ------------------------------------------------------------------ *
 * TextDecoder — the same machinery behind a standard API
 * ------------------------------------------------------------------ */

testFeature("TextDecoder decodes legacy encodings", function() {
    mustEq(new TextDecoder("windows-1252").decode(bytes([0x93, 0x94])),
           "“”", "windows-1252");
    mustEq(new TextDecoder("koi8-r").decode(bytes([0xF0, 0xD2, 0xC9])),
           "При", "koi8-r");
    mustEq(new TextDecoder("shift_jis").decode(bytes([0x81, 0x62])),
           "｜", "shift_jis");
});

testFeature("TextDecoder utf-8 handling is unchanged", function() {
    mustEq(new TextDecoder().decode(bytes([0x68, 0x69])), "hi", "utf-8");
    mustEq(new TextDecoder().decode(bytes([0x61, 0xFF])), "a�",
           "invalid utf-8 replaced");
    mustEq(new TextDecoder("utf-8").encoding, "utf-8", "encoding property");
});

testFeature("TextDecoder still rejects what is not an encoding", function() {
    mustThrow(function() { new TextDecoder("not-an-encoding"); },
              "unknown label");
    mustThrow(function() {
        new TextDecoder("shift_jis", {fatal: true}).decode(bytes([0xFD]));
    }, "fatal decode");
});

/* ------------------------------------------------------------------ *
 * rampart-totext, which decodes before any converter sees the bytes
 * ------------------------------------------------------------------ */

testFeature("totext reports the encoding it decoded from", function() {
    var totext = require("rampart-totext");
    var tmpdir = process.scriptPath + '/tmp-test';
    if (!stat(tmpdir)) mkdir(tmpdir);
    var fn = tmpdir + '/charset-sample.txt';

    /* A FILE, not a bare buffer.  With no filename to go on, totext's
       content identification has no text heuristic -- even pure ascii
       prose comes back as "unknown" and is routed to the binary
       run-extractor, which is a pre-existing limitation unrelated to
       charsets.  The extension is what makes this FT_TEXT, and the
       filename path is what every real caller uses. */
    /* RAW bytes 0x93/0x94, not "\u0093" in a JS string -- stringToBuffer
       would encode that as UTF-8 (c2 93) and the file would be perfectly
       valid, testing nothing. */
    var head = "Notes on the widget\n\nSee the ";
    var mid  = "Frame Formats";
    var tail = " section.\n";
    var raw  = [], i;
    for (i = 0; i < head.length; i++) raw.push(head.charCodeAt(i));
    raw.push(0x93);
    for (i = 0; i < mid.length; i++)  raw.push(mid.charCodeAt(i));
    raw.push(0x94);
    for (i = 0; i < tail.length; i++) raw.push(tail.charCodeAt(i));
    writeFile(fn, bytes(raw));
    var r = totext.convertFile(fn, true);
    mustEq(r.charset, "WINDOWS-1252", "charset reported");
    mustEq(r.charsetSource, "assumed", "source reported");
    must(usable(r.text), "extracted text is usable");
    testFeature.mustContain(r.text, "\u201cFrame Formats\u201d",
                            "quotes converted, not dropped");

    writeFile(fn, stringToBuffer("Plain ascii notes about the widget.\n"));
    r = totext.convertFile(fn, true);
    mustEq(r.charset, "UTF-8", "ascii reported as utf-8");
    mustEq(r.charsetSource, "utf-8", "ascii source");

    rmFile(fn);
});

/* ------------------------------------------------------------------ *
 * the file this was all found on, when it is available
 * ------------------------------------------------------------------ */

var RFC = "/home/aaron/corpora/rfc/rfc2166.txt";

if (stat(RFC)) {
    testFeature("rfc2166: 18 stray cp1252 bytes in 75 KB of ascii", function() {
        var out = toUtf8(readFile(RFC), {details: true});
        mustEq(out.charset, "WINDOWS-1252", "detected charset");
        must(out.text.length > 70000, "whole file converted");
        must(usable(out.text), "converted file survives a String operation");
        testFeature.mustContain(out.text, "“Frame Formats”",
                                "quotes converted rather than dropped");
    });
}

testFeature.exit();
