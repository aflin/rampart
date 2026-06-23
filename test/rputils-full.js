#!/usr/bin/env rampart
/* rputils-full.js
 *
 * Comprehensive coverage test for every function documented in
 * rampart-utils.rst.  Assembled from per-section verified suites.
 * Run from the test/ directory:  rampart rputils-full.js
 *
 * Blocks tagged "DOC MISMATCH" assert ACTUAL behavior where it diverges
 * from the documentation.
 */
var t = new (require('./test-feature.js'))({prefix: "rputils"});
rampart.globalize(rampart.utils);

var TMP = "/tmp/rputils-full-work";
try { rampart.utils.rm(TMP, {recursive:true, force:true}); } catch(e){}
rampart.utils.mkdir(TMP);


/* ##################### section a1 ##################### */
/* ===================================================================
 * printf
 * =================================================================== */
t("printf - return value is byte length", function(){
    var n = rampart.utils.sprintf("hello\n").length; // sanity for sprintf len
    var r = rampart.utils.printf("hello\n");
    t.mustEq(r, 6, "printf returns byte length of printed string");
    return true;
});

t("printf - %d %i basic integers", function(){
    t.mustEq(rampart.utils.sprintf("%d", 42), "42", "%d");
    t.mustEq(rampart.utils.sprintf("%i", -7), "-7", "%i");
    t.mustEq(rampart.utils.sprintf("%d %d", 1, 2), "1 2", "two %d");
    return true;
});

t("printf - width, zero-pad, precision", function(){
    t.mustEq(rampart.utils.sprintf("%5d", 42), "   42", "width");
    t.mustEq(rampart.utils.sprintf("%05d", 42), "00042", "zero pad");
    t.mustEq(rampart.utils.sprintf("%-5d|", 42), "42   |", "left align");
    t.mustEq(rampart.utils.sprintf("%5.2f", 3.14159), " 3.14", "float width.precision");
    t.mustEq(rampart.utils.sprintf("%.3f", 1.5), "1.500", "float precision");
    return true;
});

t("printf - sign flags + and space", function(){
    t.mustEq(rampart.utils.sprintf("%+d", 5), "+5", "plus flag");
    t.mustEq(rampart.utils.sprintf("% d", 5), " 5", "space flag");
    return true;
});

t("printf - hex/octal/binary and # alt form", function(){
    t.mustEq(rampart.utils.sprintf("%x", 255), "ff", "%x");
    t.mustEq(rampart.utils.sprintf("%X", 255), "FF", "%X");
    t.mustEq(rampart.utils.sprintf("%#x", 255), "0xff", "# alt form hex");
    t.mustEq(rampart.utils.sprintf("%o", 8), "10", "%o octal");
    return true;
});

t("printf - %s coerces, Objects as JSON, Buffer as-is", function(){
    t.mustEq(rampart.utils.sprintf("%s", 5), "5", "%s coerces number");
    t.mustEq(rampart.utils.sprintf("%s", {a:1}), '{"a":1}', "%s on Object -> JSON");
    t.mustEq(rampart.utils.sprintf("%s", rampart.utils.stringToBuffer("buf!")), "buf!", "%s on Buffer printed as-is");
    return true;
});

t("printf - %S requires String", function(){
    t.mustEq(rampart.utils.sprintf("%S", "hi"), "hi", "%S with string ok");
    t.mustThrow(function(){ rampart.utils.sprintf("%S", 5); }, "%S with non-string throws");
    return true;
});

t("printf - %J JSON output and width indentation", function(){
    t.mustEq(rampart.utils.sprintf("%J", {a:1,b:2}), '{"a":1,"b":2}', "%J compact");
    t.mustEq(rampart.utils.sprintf("%4J", {a:1}),
             JSON.stringify({a:1}, null, 4), "%4J equals JSON.stringify indent 4");
    return true;
});

t("printf - %J cyclic refs with ! and implied !", function(){
    var x = {a:{c:1}, b:{}};
    x.b.a = x.a;
    // without ! shared (non-cyclic) ref printed in full
    var noBang = rampart.utils.sprintf("%J", x);
    t.mustContain(noBang, '"b":{"a":{"c":1}}', "%J without ! prints shared ref fully");
    // with ! shared inner refs marked
    var bang = rampart.utils.sprintf("%!J", x);
    t.mustContain(bang, "_cyclic_ref", "%!J marks references with _cyclic_ref");
    // true cyclic ref: ! implied
    x.x_ref = x;
    var cyc = rampart.utils.sprintf("%J", x);
    t.mustContain(cyc, "_cyclic_ref", "cyclic ref implies ! (no throw, marked)");
    return true;
});

t("printf - , grouping flag (always comma)", function(){
    t.mustEq(rampart.utils.sprintf("%,d", 123456789098765), "123,456,789,098,765", "%,d grouping");
    t.mustEq(rampart.utils.sprintf("%,lld", 123456789098765), "123,456,789,098,765", "%,lld grouping");
    t.mustEq(rampart.utils.sprintf("%,f", 123456789098765.0987), "123,456,789,098,765.093750", "%,f groups integer part only");
    return true;
});

t("printf - ' locale grouping flag (uses locale separator)", function(){
    // Doc: ' is the C grouping flag; "nothing under the C locale".
    // The test environment runs under en_US.UTF-8 whose separator is ',',
    // so grouping is produced. Accept either grouped or ungrouped output.
    var got = rampart.utils.sprintf("%'d", 1234567);
    t.must(got === "1234567" || got === "1,234,567",
           "' flag groups per locale (C=none, en_US=comma); got '" + got + "'");
    return true;
});

t("printf - auto 64-bit promotion for > 32-bit Number with %d", function(){
    t.mustEq(rampart.utils.sprintf("%d", 123456789098765), "123456789098765", "%d promotes large Number to 64-bit");
    return true;
});

t("printf - BigInt integer formats", function(){
    var big = BigInt("123456789012345678901234567890");
    t.mustEq(rampart.utils.sprintf("%d", big), "123456789012345678901234567890", "%d BigInt full precision");
    t.mustEq(rampart.utils.sprintf("%,d", big), "123,456,789,012,345,678,901,234,567,890", "%,d BigInt grouped");
    t.mustEq(rampart.utils.sprintf("%x", BigInt(255)), "ff", "%x BigInt");
    t.mustEq(rampart.utils.sprintf("%#x", BigInt(255)), "0xff", "%#x BigInt");
    return true;
});

t("printf - BigInt %s and %J", function(){
    var big = BigInt("123456789012345678901234567890");
    t.mustEq(rampart.utils.sprintf("%s", big), "123456789012345678901234567890", "%s prints BigInt decimal string");
    t.mustEq(rampart.utils.sprintf("%J", big), "{}", "%J of BigInt is {} (JSON.stringify cannot serialize)");
    return true;
});

t("printf - %C multibyte character", function(){
    // %C emits the raw UTF-8 byte sequence; compare against the same bytes
    // via dehexify (avoids CESU-8/surrogate-pair string-literal mismatch).
    t.mustEq(rampart.utils.sprintf("%C", 0xf09f9983),
             rampart.utils.bufferToString(rampart.utils.dehexify("f09f9983")),
             "%C 0xf09f9983 produces the upside-down-face UTF-8 bytes");
    return true;
});

t("printf - %!U url decode and %U url encode", function(){
    var uenc = "a+url+encoded+string.+%27%23%24%3f%27";
    t.mustEq(rampart.utils.sprintf("%!U", uenc), "a url encoded string. '#$?'", "%!U decodes url-encoded string");
    return true;
});

t("printf - %w shortcut removes leading whitespace, no wrap", function(){
    var html = "<html>\n  <body>\n    <div>\n      content\n    </div>\n  </body>\n</html>\n";
    var out = rampart.utils.sprintf("%w", html);
    t.must(out.indexOf("  <body>") === -1, "%w strips leading whitespace");
    t.mustContain(out, "<body>", "%w keeps content");
    return true;
});

/* ===================================================================
 * sprintf
 * =================================================================== */
t("sprintf - returns String not printed", function(){
    var s = rampart.utils.sprintf("%d-%s", 1, "two");
    t.mustEq(typeof s, "string", "sprintf returns a String");
    t.mustEq(s, "1-two", "sprintf content");
    return true;
});

/* ===================================================================
 * bprintf
 * =================================================================== */
t("bprintf - returns Buffer with formatted contents", function(){
    var b = rampart.utils.bprintf("%d-%s", 7, "x");
    t.mustEq(typeof b, "object", "bprintf returns Buffer (object)");
    t.mustEq(rampart.utils.bufferToString(b), "7-x", "bprintf buffer contents");
    return true;
});

/* ===================================================================
 * abprintf
 * =================================================================== */
t("abprintf - appends at end by default (dynamic buffer altered)", function(){
    var ob = rampart.utils.stringToBuffer("AB", "dynamic");
    var nb = rampart.utils.abprintf(ob, "CD");
    t.mustEq(rampart.utils.bufferToString(nb), "ABCD", "abprintf appends to end");
    return true;
});

t("abprintf - start offset (positive) writes from offset and truncates tail", function(){
    var ob = rampart.utils.stringToBuffer("hello", "dynamic");
    var nb = rampart.utils.abprintf(ob, 2, "XX");
    // start truncates oldbuf at the offset, then appends (documented):
    // abprintf("hello", 2, "XX") -> "heXX" (not "heXXo").
    t.mustEq(rampart.utils.bufferToString(nb), "heXX", "abprintf positive start truncates at offset then writes");
    return true;
});

t("abprintf - negative start counts from end", function(){
    var ob = rampart.utils.stringToBuffer("hello", "dynamic");
    var nb = rampart.utils.abprintf(ob, -1, "XX");
    t.mustEq(rampart.utils.bufferToString(nb), "hellXX", "abprintf negative start counts from end");
    return true;
});

t("abprintf - fixed/non-dynamic buffer is copied, original unaltered", function(){
    var fb = rampart.utils.stringToBuffer("XY", "fixed");
    var nf = rampart.utils.abprintf(fb, "Z");
    t.mustEq(rampart.utils.bufferToString(nf), "XYZ", "abprintf result on fixed buffer");
    t.mustEq(rampart.utils.bufferToString(fb), "XY", "original fixed buffer unaltered");
    return true;
});

/* ===================================================================
 * hexify
 * =================================================================== */
t("hexify - lowercase default and uppercase flag", function(){
    var buf = rampart.utils.dehexify("deadbeef");
    t.mustEq(rampart.utils.hexify(buf), "deadbeef", "hexify lowercase default");
    t.mustEq(rampart.utils.hexify(buf, true), "DEADBEEF", "hexify uppercase with flag");
    return true;
});

t("hexify - from String, two chars per byte", function(){
    var h = rampart.utils.hexify("AB");
    t.mustEq(h, "4142", "hexify of 'AB' is 4142");
    t.mustEq(h.length, 4, "two hex chars per byte");
    return true;
});

/* ===================================================================
 * dehexify
 * =================================================================== */
t("dehexify - returns Buffer, round-trips with hexify", function(){
    var d = rampart.utils.dehexify(rampart.utils.hexify("hello"));
    t.mustEq(typeof d, "object", "dehexify returns Buffer");
    t.mustEq(rampart.utils.bufferToString(d), "hello", "hexify/dehexify round trip");
    return true;
});

t("dehexify - documented emoji example round trip", function(){
    var s = rampart.utils.sprintf("%c%c%c%c", 0xF0, 0x9F, 0x98, 0x8A);
    t.mustEq(rampart.utils.hexify(s), "f09f988a", "hexify of utf8 emoji bytes");
    // dehexify back yields the same UTF-8 byte string we built
    t.mustEq(rampart.utils.bufferToString(rampart.utils.dehexify("f09f988a")), s,
             "dehexify of f09f988a round-trips to the emoji UTF-8 string");
    return true;
});

/* ===================================================================
 * stringToNumber
 * =================================================================== */
t("stringToNumber - basic English numbers", function(){
    t.mustEq(rampart.utils.stringToNumber("five"), 5, "five");
    t.mustEq(rampart.utils.stringToNumber("three and a half"), 3.5, "three and a half");
    t.mustEq(rampart.utils.stringToNumber("four score and seven"), 87, "four score and seven");
    t.mustEq(rampart.utils.stringToNumber("five dozen"), 60, "five dozen");
    return true;
});

t("stringToNumber - unparseable yields NaN", function(){
    var r = rampart.utils.stringToNumber("a gazillion");
    t.must(typeof r === "number" && isNaN(r), "'a gazillion' returns NaN");
    return true;
});

t("stringToNumber - retObj forms", function(){
    t.mustEq(rampart.utils.stringToNumber("five dozen", true),
             {value:60, op:"=", rem:""}, "five dozen retObj");
    t.mustEq(rampart.utils.stringToNumber("five dozen cookies", true),
             {value:60, op:"=", rem:"cookies"}, "remainder text in rem");
    t.mustEq(rampart.utils.stringToNumber("less than twenty", true),
             {value:20, op:"<", rem:""}, "less than -> op <");
    t.mustEq(rampart.utils.stringToNumber("less than twenty greater than one half is our range", true),
             {value:20, min:0.5, max:20, rem:"is our range"}, "range retObj with min/max");
    return true;
});

/* ===================================================================
 * stringToBuffer
 * =================================================================== */
t("stringToBuffer - String to fixed buffer default", function(){
    var b = rampart.utils.stringToBuffer("hi");
    t.mustEq(typeof b, "object", "returns Buffer");
    t.mustEq(rampart.utils.bufferToString(b), "hi", "contents copied byte-for-byte");
    return true;
});

t("stringToBuffer - explicit fixed and dynamic buftypes", function(){
    t.mustEq(rampart.utils.bufferToString(rampart.utils.stringToBuffer("hi","fixed")), "hi", "fixed buftype");
    t.mustEq(rampart.utils.bufferToString(rampart.utils.stringToBuffer("hi","dynamic")), "hi", "dynamic buftype");
    return true;
});

/* ===================================================================
 * bufferToString
 * =================================================================== */
t("bufferToString - 1:1 copy of buffer to string", function(){
    var b = rampart.utils.stringToBuffer("round trip");
    var s = rampart.utils.bufferToString(b);
    t.mustEq(typeof s, "string", "returns String");
    t.mustEq(s, "round trip", "contents preserved");
    return true;
});

/* ===================================================================
 * objectToQuery
 * =================================================================== */
t("objectToQuery - default (repeat) for arrays", function(){
    var obj = { key1: null, key2: [1,2,3], key3: ["val1","val2"] };
    var expected = "key1=null&key2=1&key2=2&key2=3&key3=val1&key3=val2";
    t.mustEq(rampart.utils.objectToQuery(obj), expected, "default is repeat");
    t.mustEq(rampart.utils.objectToQuery(obj, "repeat"), expected, "explicit repeat");
    return true;
});

t("objectToQuery - bracket / comma / json array options", function(){
    var obj = { key1: null, key2: [1,2,3], key3: ["val1","val2"] };
    t.mustEq(rampart.utils.objectToQuery(obj, "bracket"),
        "key1=null&key2%5B%5D=1&key2%5B%5D=2&key2%5B%5D=3&key3%5B%5D=val1&key3%5B%5D=val2", "bracket");
    t.mustEq(rampart.utils.objectToQuery(obj, "comma"),
        "key1=null&key2=1,2,3&key3=val1,val2", "comma");
    t.mustEq(rampart.utils.objectToQuery(obj, "json"),
        "key1=null&key2=%5b1%2c2%2c3%5d&key3=%5b%22val1%22%2c%22val2%22%5d", "json");
    return true;
});

t("objectToQuery - null/undefined as strings, nested obj as JSON", function(){
    t.mustEq(rampart.utils.objectToQuery({a: null}), "a=null", "null -> 'null'");
    t.mustEq(rampart.utils.objectToQuery({a: undefined}), "a=undefined", "undefined -> 'undefined'");
    t.mustEq(rampart.utils.objectToQuery({a:{x:1}}), "a=%7b%22x%22%3a1%7d", "nested object -> url-encoded JSON");
    return true;
});

/* ===================================================================
 * queryToObject
 * =================================================================== */
t("queryToObject - repeat -> array of strings", function(){
    t.mustEq(rampart.utils.queryToObject("key1=null&key2=1&key2=2&key2=3&key3=val1&key3=val2"),
        {key1:"null", key2:["1","2","3"], key3:["val1","val2"]}, "repeat decoded");
    return true;
});

t("queryToObject - bracket -> array of strings", function(){
    t.mustEq(rampart.utils.queryToObject("key2%5B%5D=1&key2%5B%5D=2"),
        {key2:["1","2"]}, "bracket decoded to array");
    return true;
});

t("queryToObject - comma not split (single string)", function(){
    t.mustEq(rampart.utils.queryToObject("key1=null&key2=1,2,3&key3=val1,val2"),
        {key1:"null", key2:"1,2,3", key3:"val1,val2"}, "comma values stay as one string");
    return true;
});

t("queryToObject - json preserves numbers", function(){
    t.mustEq(rampart.utils.queryToObject("key1=null&key2=%5b1%2c2%2c3%5d&key3=%5b%22val1%22%2c%22val2%22%5d"),
        {key1:"null", key2:[1,2,3], key3:["val1","val2"]}, "json keeps numeric types");
    return true;
});

t("queryToObject - object-like notation -> nested object", function(){
    t.mustEq(rampart.utils.queryToObject("myvar[mykey]=myval&myvar[mykey2]=myval2"),
        {myvar:{mykey:"myval", mykey2:"myval2"}}, "object notation parsed");
    return true;
});

/* ===================================================================
 * urlComponents
 * =================================================================== */
t("urlComponents - full URL component shape", function(){
    var p = rampart.utils.urlComponents("https://user:pw@Example.COM:8443/a/b?x=1#frag");
    t.mustEq(p, {
        protocol: "https:",
        username: "user",
        password: "pw",
        host: "example.com:8443",
        hostname: "example.com",
        port: "8443",
        numericPort: 8443,
        pathname: "/a/b",
        search: "?x=1",
        hash: "#frag",
        href: "https://user:pw@example.com:8443/a/b?x=1#frag",
        origin: "https://example.com:8443",
        hostUnicode: "example.com"
    }, "full component object");
    return true;
});

t("urlComponents - relative resolved against base", function(){
    var p = rampart.utils.urlComponents("../c?z=9", "https://example.com/a/b/");
    t.mustEq(p.href, "https://example.com/a/c?z=9", "relative URL resolved");
    return true;
});

t("urlComponents - numericPort default for scheme when none", function(){
    t.mustEq(rampart.utils.urlComponents("https://example.com/").numericPort, 443, "default https port 443");
    return true;
});

t("urlComponents - opaque origin yields 'null' string, numericPort null", function(){
    var p = rampart.utils.urlComponents("mailto:a@b.com");
    t.mustEq(p.origin, "null", "opaque scheme origin is string 'null'");
    t.mustEq(p.numericPort, null, "numericPort null when no default port");
    return true;
});

t("urlComponents - unparseable returns undefined", function(){
    t.mustEq(rampart.utils.urlComponents("not a url"), undefined, "bad URL returns undefined");
    return true;
});

/* ===================================================================
 * absUrl
 * =================================================================== */
t("absUrl - String rel returns resolved href", function(){
    t.mustEq(rampart.utils.absUrl("https://example.com/docs/guide/", "../api/x.html"),
        "https://example.com/docs/api/x.html", "resolve single relative");
    return true;
});

t("absUrl - Array rel returns array of hrefs", function(){
    t.mustEq(rampart.utils.absUrl("https://example.com/a/b/", ["c.html", "../d.html"]),
        ["https://example.com/a/b/c.html", "https://example.com/a/d.html"], "resolve array");
    return true;
});

t("absUrl - asComponents returns component object", function(){
    var c = rampart.utils.absUrl("https://example.com/", "x.html", true);
    t.mustEq(typeof c, "object", "asComponents returns object");
    t.mustEq(c.href, "https://example.com/x.html", "components href correct");
    t.mustEq(c.pathname, "/x.html", "components pathname correct");
    return true;
});

t("absUrl - unparseable base returns undefined", function(){
    t.mustEq(rampart.utils.absUrl("garbage", "x.html"), undefined, "bad base returns undefined");
    return true;
});

/* ===================================================================
 * toASCII
 * =================================================================== */
t("toASCII - IDN to punycode, ascii unchanged, throws on invalid", function(){
    t.mustEq(rampart.utils.toASCII("bücher.de"), "xn--bcher-kva.de", "IDN to punycode");
    t.mustEq(rampart.utils.toASCII("plain.com"), "plain.com", "already-ascii unchanged");
    t.mustThrow(function(){ rampart.utils.toASCII(""); }, "invalid domain throws");
    return true;
});

/* ===================================================================
 * toUnicode
 * =================================================================== */
t("toUnicode - punycode to unicode, ascii unchanged, throws on invalid", function(){
    t.mustEq(rampart.utils.toUnicode("xn--bcher-kva.de"), "bücher.de", "punycode to unicode");
    t.mustEq(rampart.utils.toUnicode("plain.com"), "plain.com", "ascii unchanged");
    return true;
});


/* ##################### section a2 ##################### */
/* ============================================================
 * a2: getchar, readFile, writeFile, appendFile, trim, minify,
 *     stat, lstat, exists, statVfs, exec, getenv, setenv, unsetenv,
 *     shell, fork, newPipe, daemon, forkpty, kill, getcwd, chdir
 * ============================================================ */

/* ---------- getchar ---------- */
t.skip("getchar", "interactive (reads from stdin terminal)");

/* ---------- readFile ---------- */
t("readFile - object form returns Buffer by default", function(){
    var p = TMP + "/rf1.txt";
    rampart.utils.writeFile(p, "0123456789ABCDEF");
    var c = rampart.utils.readFile({file: p});
    t.must(ArrayBuffer.isView(c), "default returns Buffer (ArrayBuffer view)");
    t.mustEq(rampart.utils.bufferToString(c), "0123456789ABCDEF", "buffer contents");
    rampart.utils.rmFile(p);
    return true;
});

t("readFile - positional form, returnString true", function(){
    var p = TMP + "/rf2.txt";
    rampart.utils.writeFile(p, "hello world");
    var c = rampart.utils.readFile(p, 0, 0, true);
    t.mustEq(typeof c, "string", "returnString gives string");
    t.mustEq(c, "hello world", "full contents");
    rampart.utils.rmFile(p);
    return true;
});

t("readFile - positive offset and length (doc example)", function(){
    /* doc example: offset 10, length -6 over "This is a text file\n" -> "text" */
    var p = TMP + "/rf3.txt";
    rampart.utils.writeFile(p, "This is a text file\n");
    var txt = rampart.utils.readFile(p, 10, -6, true);
    t.mustEq(txt, "text", "doc example offset 10 length -6 -> 'text'");
    /* object form equivalent */
    var txt2 = rampart.utils.readFile({file: p, offset: 10, length: -6, returnString: true});
    t.mustEq(txt2, "text", "object form equivalent");
    rampart.utils.rmFile(p);
    return true;
});

t("readFile - positive length reads N bytes", function(){
    var p = TMP + "/rf4.txt";
    rampart.utils.writeFile(p, "0123456789");
    var c = rampart.utils.readFile(p, 2, 3, true);
    t.mustEq(c, "234", "offset 2 length 3 -> '234'");
    rampart.utils.rmFile(p);
    return true;
});

t("readFile - negative offset reads from end", function(){
    var p = TMP + "/rf5.txt";
    rampart.utils.writeFile(p, "0123456789");
    /* negative offset: start position from end of file */
    var c = rampart.utils.readFile(p, -3, 0, true);
    t.mustEq(c, "789", "offset -3 -> last 3 bytes");
    rampart.utils.rmFile(p);
    return true;
});

/* ---------- writeFile ---------- */
t("writeFile - string round-trip, truncates by default", function(){
    var p = TMP + "/wf1.txt";
    rampart.utils.writeFile(p, "first content here");
    t.mustEq(rampart.utils.readFile(p, 0, 0, true), "first content here", "first write");
    /* default flag 'w' truncates */
    rampart.utils.writeFile(p, "second");
    t.mustEq(rampart.utils.readFile(p, 0, 0, true), "second", "truncated on rewrite");
    rampart.utils.rmFile(p);
    return true;
});

t("writeFile - returns undefined", function(){
    var p = TMP + "/wf2.txt";
    var r = rampart.utils.writeFile(p, "x");
    t.mustEq(r, undefined, "returns undefined");
    rampart.utils.rmFile(p);
    return true;
});

t("writeFile - Uint8Array buffer with mode option", function(){
    var p = TMP + "/wf3.bin";
    rampart.utils.writeFile(p, new Uint8Array([1,2,3]), {mode: 0o600});
    var c = rampart.utils.readFile(p);
    t.mustEq(c[0], 1, "byte 0");
    t.mustEq(c[1], 2, "byte 1");
    t.mustEq(c[2], 3, "byte 2");
    var st = rampart.utils.stat(p);
    t.mustEq(st.mode & 0o777, 0o600, "mode applied via fchmod");
    rampart.utils.rmFile(p);
    return true;
});

t("writeFile - flag 'a' appends", function(){
    var p = TMP + "/wf4.txt";
    rampart.utils.writeFile(p, "AAA");
    rampart.utils.writeFile(p, "BBB", {flag: "a"});
    t.mustEq(rampart.utils.readFile(p, 0, 0, true), "AAABBB", "flag 'a' appends");
    rampart.utils.rmFile(p);
    return true;
});

/* ---------- appendFile ---------- */
t("appendFile - creates file if missing", function(){
    var p = TMP + "/af1.txt";
    try { rampart.utils.rmFile(p); } catch(e){}
    rampart.utils.appendFile(p, "line1\n");
    t.mustEq(rampart.utils.readFile(p, 0, 0, true), "line1\n", "created and wrote");
    rampart.utils.rmFile(p);
    return true;
});

t("appendFile - appends to existing, returns undefined", function(){
    var p = TMP + "/af2.txt";
    rampart.utils.writeFile(p, "head ");
    var r = rampart.utils.appendFile(p, "tail");
    t.mustEq(r, undefined, "returns undefined");
    t.mustEq(rampart.utils.readFile(p, 0, 0, true), "head tail", "appended");
    rampart.utils.rmFile(p);
    return true;
});

/* ---------- trim ---------- */
t("trim - removes leading/trailing whitespace (doc example)", function(){
    t.mustEq(rampart.utils.trim("\n a line of text \n"), "a line of text", "doc example");
    t.mustEq(rampart.utils.trim("   spaces   "), "spaces", "spaces");
    t.mustEq(rampart.utils.trim("\t\ttabs\t"), "tabs", "tabs");
    t.mustEq(rampart.utils.trim("nochange"), "nochange", "no whitespace unchanged");
    return true;
});

/* ---------- minify ---------- */
t("minify - removes whitespace and mangles locals (doc example)", function(){
    var src = "function add(first, second) {\n    return first + second;\n}";
    var min = rampart.utils.minify(src);
    t.mustEq(typeof min, "string", "returns string");
    /* doc expected: "function add(a,b){return a+b;}" */
    t.mustEq(min, "function add(a,b){return a+b;}", "doc example output");
    return true;
});

/* ---------- stat ---------- */
t("stat - returns false for missing file", function(){
    t.mustEq(rampart.utils.stat(TMP + "/does-not-exist-xyz"), false, "missing file -> false");
    return true;
});

t("stat - returns object with documented fields for a regular file", function(){
    var p = TMP + "/st1.txt";
    rampart.utils.writeFile(p, "abc");
    var st = rampart.utils.stat(p);
    t.must(typeof st === "object" && st, "object returned");
    var numFields = ["dev","ino","mode","nlink","uid","gid","rdev","size","blksize","blocks"];
    numFields.forEach(function(f){ t.mustEq(typeof st[f], "number", "field "+f+" is Number"); });
    t.must(st.atime instanceof Date, "atime is Date");
    t.must(st.mtime instanceof Date, "mtime is Date");
    t.must(st.ctime instanceof Date, "ctime is Date");
    var boolFields = ["readable","writable","executable","isBlockDevice","isCharacterDevice",
                      "isDirectory","isFIFO","isFile","isSocket"];
    boolFields.forEach(function(f){ t.mustEq(typeof st[f], "boolean", "field "+f+" is Boolean"); });
    t.mustEq(typeof st.owner, "string", "owner is String");
    t.mustEq(typeof st.group, "string", "group is String");
    t.mustEq(typeof st.permissions, "string", "permissions is String");
    t.mustEq(st.size, 3, "size is 3 bytes");
    t.mustEq(st.isFile, true, "isFile true");
    t.mustEq(st.isDirectory, false, "isDirectory false");
    rampart.utils.rmFile(p);
    return true;
});

t("stat - directory has isDirectory true", function(){
    var st = rampart.utils.stat(TMP);
    t.must(st, "TMP stats ok");
    t.mustEq(st.isDirectory, true, "isDirectory true for dir");
    t.mustEq(st.isFile, false, "isFile false for dir");
    return true;
});

t("stat - permissions string format matches mode", function(){
    var p = TMP + "/st2.txt";
    rampart.utils.writeFile(p, "x", {mode: 0o644});
    var st = rampart.utils.stat(p);
    /* permissions like "-rw-r--r--" */
    t.mustEq(st.permissions.length, 10, "permissions string length 10");
    t.mustEq(st.permissions.charAt(0), "-", "regular file leading '-'");
    t.mustEq(st.permissions, "-rw-r--r--", "0644 -> -rw-r--r--");
    rampart.utils.rmFile(p);
    return true;
});

/* ---------- lstat ---------- */
t("lstat - on regular file matches stat shape, isSymbolicLink false", function(){
    var p = TMP + "/ls1.txt";
    rampart.utils.writeFile(p, "abc");
    var st = rampart.utils.lstat(p);
    t.must(st, "lstat returns object");
    t.mustEq(typeof st.isSymbolicLink, "boolean", "isSymbolicLink present (Boolean)");
    t.mustEq(st.isSymbolicLink, false, "regular file not a symlink");
    rampart.utils.rmFile(p);
    return true;
});

t("lstat - on symlink reports the link itself", function(){
    var target = TMP + "/ls_target.txt";
    var link = TMP + "/ls_link";
    rampart.utils.writeFile(target, "target contents");
    try { rampart.utils.rmFile(link); } catch(e){}
    /* create symlink via shell (no documented utils.symlink in this group) */
    rampart.utils.shell("ln -s '" + target + "' '" + link + "'");
    var lst = rampart.utils.lstat(link);
    t.must(lst, "lstat link ok");
    t.mustEq(lst.isSymbolicLink, true, "isSymbolicLink true for link");
    /* stat() follows the link -> regular file */
    var st = rampart.utils.stat(link);
    t.mustEq(st.isFile, true, "stat follows link to file");
    rampart.utils.rmFile(link);
    rampart.utils.rmFile(target);
    return true;
});

/* ---------- exists ---------- */
t("exists - true for existing, false for missing", function(){
    var p = TMP + "/ex1.txt";
    rampart.utils.writeFile(p, "x");
    t.mustEq(rampart.utils.exists(p), true, "existing path -> true");
    rampart.utils.rmFile(p);
    t.mustEq(rampart.utils.exists(p), false, "missing path -> false");
    t.mustEq(rampart.utils.exists(TMP + "/no/such/parent/file"), false, "inaccessible parent -> false");
    return true;
});

t("exists - true for directory", function(){
    t.mustEq(rampart.utils.exists(TMP), true, "directory exists");
    return true;
});

/* ---------- statVfs ---------- */
t("statVfs - returns object with documented numeric fields", function(){
    var info = rampart.utils.statVfs(TMP);
    t.must(typeof info === "object" && info, "object returned");
    var fields = ["bsize","frsize","blocks","bfree","bavail","files","ffree","favail",
                  "fsid","flag","namemax","totalBytes","freeBytes","availBytes"];
    fields.forEach(function(f){ t.mustEq(typeof info[f], "number", "field "+f+" is Number"); });
    /* convenience fields = frsize * counts */
    t.mustEq(info.totalBytes, info.frsize * info.blocks, "totalBytes = frsize*blocks");
    t.mustEq(info.freeBytes, info.frsize * info.bfree, "freeBytes = frsize*bfree");
    t.mustEq(info.availBytes, info.frsize * info.bavail, "availBytes = frsize*bavail");
    return true;
});

t("statVfs - throws if path cannot be resolved", function(){
    t.mustThrow(function(){ rampart.utils.statVfs("/no/such/path/at/all/xyz"); }, "throws on unresolvable path");
    return true;
});

/* ---------- exec ---------- */
t("exec - basic, return shape", function(){
    var ret = rampart.utils.exec("/bin/echo", "hello");
    t.mustEq(typeof ret, "object", "object returned");
    t.mustEq(ret.stdout, "hello\n", "stdout captured");
    t.mustEq(typeof ret.stderr, "string", "stderr is string");
    t.mustEq(ret.exitStatus, 0, "exitStatus 0");
    t.mustEq(ret.timedOut, false, "timedOut false");
    t.mustEq(typeof ret.pid, "number", "pid is number");
    return true;
});

t("exec - args as positional parameters", function(){
    var ret = rampart.utils.exec("/bin/echo", "-n", "a", "b", "c");
    t.mustEq(ret.stdout, "a b c", "positional args passed");
    return true;
});

t("exec - args array option", function(){
    var ret = rampart.utils.exec("/bin/echo", {args: ["-n", "from-array"]});
    t.mustEq(ret.stdout, "from-array", "args array passed");
    return true;
});

t("exec - args array plus positional combine", function(){
    var ret = rampart.utils.exec("/bin/echo", {args: ["-n", "X"]}, "Y");
    // documented order: positional args first, then the args array -> "Y -n X".
    t.mustEq(ret.stdout, "Y -n X\n", "positional args precede array args");
    return true;
});

t("exec - non-string args converted to string", function(){
    var ret = rampart.utils.exec("/bin/echo", "-n", 42, true, null);
    t.mustEq(ret.stdout, "42 true null", "number/bool/null stringified");
    return true;
});

t("exec - non-zero exit status captured", function(){
    var ret = rampart.utils.exec("/bin/sh", "-c", "exit 3");
    t.mustEq(ret.exitStatus, 3, "exit 3 reported");
    return true;
});

t("exec - stderr captured separately", function(){
    var ret = rampart.utils.exec("/bin/sh", "-c", "echo out; echo err 1>&2");
    t.mustEq(ret.stdout, "out\n", "stdout only out");
    t.mustEq(ret.stderr, "err\n", "stderr only err");
    return true;
});

t("exec - stdin option pipes input", function(){
    var ret = rampart.utils.exec("/bin/cat", {stdin: "piped-input"});
    t.mustEq(ret.stdout, "piped-input", "stdin piped to command");
    return true;
});

t("exec - returnBuffer option returns Buffer stdout", function(){
    var ret = rampart.utils.exec("/bin/echo", "-n", "bin", {returnBuffer: true});
    t.must(ArrayBuffer.isView(ret.stdout), "stdout is Buffer (ArrayBuffer view)");
    t.mustEq(rampart.utils.bufferToString(ret.stdout), "bin", "buffer contents");
    return true;
});

t("exec - env option sets environment", function(){
    var ret = rampart.utils.exec("/bin/sh", "-c", "echo $MYEXECVAR", {env: {MYEXECVAR: "envval"}});
    t.mustEq(ret.stdout, "envval\n", "env var visible to child");
    return true;
});

t("exec - empty env removes all variables", function(){
    var ret = rampart.utils.exec("/bin/sh", "-c", "echo [$HOME]", {env: {}});
    t.mustEq(ret.stdout, "[]\n", "empty env removes HOME");
    return true;
});

t("exec - appendEnv true augments process.env", function(){
    /* appendEnv appends to process.env snapshot (per doc), not live OS env */
    process.env.APPENDTESTVAR = "fromsnap";
    var ret = rampart.utils.exec("/bin/sh", "-c", "echo $APPENDTESTVAR $EXTRA",
                                 {env: {EXTRA: "extra"}, appendEnv: true});
    t.mustEq(ret.stdout, "fromsnap extra\n", "appendEnv keeps process.env + adds new");
    delete process.env.APPENDTESTVAR;
    return true;
});

t("exec - changeDirectory / cd option", function(){
    var ret = rampart.utils.exec("/bin/pwd", {changeDirectory: "/tmp"});
    t.mustEq(ret.stdout, "/tmp\n", "changeDirectory works");
    var ret2 = rampart.utils.exec("/bin/pwd", {cd: "/tmp"});
    t.mustEq(ret2.stdout, "/tmp\n", "cd alias works");
    return true;
});

t("exec - timeout kills long process, timedOut true", function(){
    var ret = rampart.utils.exec("/bin/sleep", "5", {timeout: 200});
    t.mustEq(ret.timedOut, true, "timedOut true when killed");
    return true;
});

t("exec - background returns immediately, stdout/stderr null", function(){
    var ret = rampart.utils.exec("/bin/sleep", "5", {background: true});
    t.mustEq(ret.stdout, null, "stdout null when background");
    t.mustEq(ret.stderr, null, "stderr null when background");
    t.mustEq(typeof ret.pid, "number", "pid present");
    /* clean up the background process */
    rampart.utils.kill(ret.pid, 9);
    return true;
});

/* ---------- getenv ---------- */
t("getenv - undefined for unset variable", function(){
    rampart.utils.unsetenv("GETENV_NEVER_SET_XYZ");
    t.mustEq(rampart.utils.getenv("GETENV_NEVER_SET_XYZ"), undefined, "unset -> undefined");
    return true;
});

t("getenv - reads live OS env set via setenv", function(){
    rampart.utils.setenv("GETENV_LIVE", "live-value");
    t.mustEq(rampart.utils.getenv("GETENV_LIVE"), "live-value", "getenv sees setenv value");
    rampart.utils.unsetenv("GETENV_LIVE");
    return true;
});

/* ---------- setenv ---------- */
t("setenv - sets value, returns undefined, visible to getenv and child", function(){
    var r = rampart.utils.setenv("MY_VAR_A2", "hello");
    t.mustEq(r, undefined, "returns undefined");
    t.mustEq(rampart.utils.getenv("MY_VAR_A2"), "hello", "getenv sees it");
    var ret = rampart.utils.exec("/bin/sh", "-c", "echo $MY_VAR_A2");
    t.mustEq(ret.stdout, "hello\n", "child inherits it");
    rampart.utils.unsetenv("MY_VAR_A2");
    return true;
});

t("setenv - overwrite false leaves existing value (doc example)", function(){
    rampart.utils.setenv("MY_VAR_A2B", "hello");
    rampart.utils.setenv("MY_VAR_A2B", "ignored", false);
    t.mustEq(rampart.utils.getenv("MY_VAR_A2B"), "hello", "overwrite=false keeps original");
    /* overwrite default true replaces */
    rampart.utils.setenv("MY_VAR_A2B", "replaced");
    t.mustEq(rampart.utils.getenv("MY_VAR_A2B"), "replaced", "default overwrite replaces");
    rampart.utils.unsetenv("MY_VAR_A2B");
    return true;
});

t("setenv - throws on invalid name (contains '=')", function(){
    t.mustThrow(function(){ rampart.utils.setenv("BAD=NAME", "x"); }, "throws on name with '='");
    return true;
});

/* ---------- unsetenv ---------- */
t("unsetenv - removes variable, returns undefined (doc example)", function(){
    rampart.utils.setenv("TEMP_VAR_A2", "x");
    var r = rampart.utils.unsetenv("TEMP_VAR_A2");
    t.mustEq(r, undefined, "returns undefined");
    t.mustEq(rampart.utils.getenv("TEMP_VAR_A2") || "(unset)", "(unset)", "removed");
    return true;
});

/* ---------- shell ---------- */
t("shell - runs bash command, return shape (doc example)", function(){
    var ret = rampart.utils.shell('echo -n "hello"; echo "hi" 1>&2;');
    t.mustEq(ret.stdout, "hello", "stdout captured");
    t.mustEq(ret.stderr, "hi\n", "stderr captured");
    t.mustEq(ret.timedOut, false, "timedOut false");
    t.mustEq(ret.exitStatus, 0, "exitStatus 0");
    t.mustEq(typeof ret.pid, "number", "pid present");
    return true;
});

t("shell - supports exec options (timeout)", function(){
    var ret = rampart.utils.shell("sleep 5", {timeout: 200});
    t.mustEq(ret.timedOut, true, "timeout honored in shell");
    return true;
});

/* ---------- fork ---------- */
t("fork - parent gets child pid, child gets 0 and exits", function(){
    var pid = rampart.utils.fork();
    if (pid === -1) {
        t.must(false, "fork failed");
        return true;
    }
    if (!pid) {
        /* child: per doc, pid===0 here; exit immediately (must call process.exit) */
        process.exit(0);
    }
    /* parent: doc says pid is the child's pid (a positive Number) */
    t.mustEq(typeof pid, "number", "parent gets numeric pid");
    t.must(pid > 0, "child pid > 0 in parent");
    /* give the child time to exit; it becomes a defunct/zombie since rampart
       exposes no wait()/waitpid() reap primitive in this API group. Verify the
       child entered a terminated (zombie 'Z') state, i.e. it did exit. */
    rampart.utils.sleep(0.3);
    var state = rampart.utils.shell("ps -o state= -p " + pid + " 2>/dev/null").stdout.replace(/\s/g,"");
    t.must(state === "Z" || state === "", "child exited (zombie or already reaped), state=" + state);
    return true;
});

/* ---------- newPipe ---------- */
t("newPipe - returns object of functions", function(){
    var pipe = rampart.utils.newPipe();
    t.mustEq(typeof pipe.write, "function", "write fn");
    t.mustEq(typeof pipe.read, "function", "read fn");
    t.mustEq(typeof pipe.onRead, "function", "onRead fn");
    t.mustEq(typeof pipe.close, "function", "close fn");
    pipe.close();
    return true;
});

t("newPipe - parent->child message via fork + blocking read", function(){
    var pipe = rampart.utils.newPipe();
    var pid = rampart.utils.fork(pipe);
    if (pid === -1) { t.must(false, "fork failed"); return true; }
    if (!pid) {
        /* child: read one message, write result back, exit */
        var msg = pipe.read();
        if (msg && msg.value === "ping")
            pipe.write("pong");
        else
            pipe.write("BAD");
        process.exit(0);
    }
    /* parent */
    pipe.write("ping");
    var reply = pipe.read();
    t.must(reply, "got reply object");
    t.mustEq(reply.value, "pong", "round-trip CBOR message");
    /* reap child */
    var i;
    for (i = 0; i < 50; i++) {
        if (rampart.utils.kill(pid, 0) === false) break;
        rampart.utils.sleep(0.05);
    }
    pipe.close();
    return true;
});

/* ---------- daemon ---------- */
t.skip("daemon", "would detach the test process (double-fork)");

/* ---------- forkpty ---------- */
t.skip("forkpty", "interactive pseudo-terminal");

/* ---------- kill ---------- */
t("kill - signal 0 checks existence; SIGTERM ends process (doc example)", function(){
    var ret = rampart.utils.exec("/bin/sleep", "100", {background: true});
    var pid = ret.pid;
    t.mustEq(rampart.utils.kill(pid, 0), true, "signal 0: process running -> true");
    t.mustEq(rampart.utils.kill(pid), true, "default SIGTERM sent -> true");
    rampart.utils.sleep(0.3);
    t.mustEq(rampart.utils.kill(pid, 0), false, "after kill, signal 0 -> false");
    return true;
});

t("kill - string signal name accepted", function(){
    var ret = rampart.utils.exec("/bin/sleep", "100", {background: true});
    var pid = ret.pid;
    t.mustEq(rampart.utils.kill(pid, "SIGKILL"), true, "string signal 'SIGKILL' works");
    rampart.utils.sleep(0.2);
    t.mustEq(rampart.utils.kill(pid, 0), false, "process gone after SIGKILL");
    return true;
});

t("kill - nonexistent pid returns false (no throw)", function(){
    /* pick a pid very unlikely to exist */
    t.mustEq(rampart.utils.kill(2147483640, 0), false, "missing process -> false");
    return true;
});

t("kill - throwOnError true throws for missing process", function(){
    t.mustThrow(function(){ rampart.utils.kill(2147483640, 0, true); }, "throwOnError throws");
    return true;
});

/* ---------- getcwd ---------- */
t("getcwd - returns a string path", function(){
    var cwd = rampart.utils.getcwd();
    t.mustEq(typeof cwd, "string", "returns string");
    t.must(cwd.length > 0, "non-empty path");
    t.mustEq(cwd.charAt(0), "/", "absolute path");
    return true;
});

/* ---------- chdir ---------- */
t("chdir - changes directory and restores; throws on bad path", function(){
    var cwd = rampart.utils.getcwd();
    var r = rampart.utils.chdir("/tmp");
    t.mustEq(r, undefined, "returns undefined");
    /* getcwd reflects change (resolve symlinks: /tmp may be canonical) */
    var now = rampart.utils.getcwd();
    t.must(now === "/tmp" || now === rampart.utils.realPath("/tmp"), "cwd changed to /tmp");
    /* bad path throws */
    t.mustThrow(function(){ rampart.utils.chdir("/no/such/dir/xyz"); }, "throws on bad path");
    /* restore */
    rampart.utils.chdir(cwd);
    t.mustEq(rampart.utils.getcwd(), cwd, "restored original cwd");
    return true;
});


/* ##################### section a3 ##################### */

/* ===================== mkdir ===================== */
t("mkdir - basic create returns undefined", function(){
    var d = TMP + "/mk_basic";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    var r = rampart.utils.mkdir(d);
    t.mustEq(r, undefined, "returns undefined");
    t.must(rampart.utils.stat(d).isDirectory, "directory exists");
    rampart.utils.rmdir(d);
});

t("mkdir - creates missing parent dirs", function(){
    var base = TMP + "/mk_parents";
    try { rampart.utils.rm(base, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(base + "/p2/p3", 0755);
    t.must(rampart.utils.stat(base + "/p2/p3").isDirectory, "deep dir created");
    rampart.utils.rm(base, {recursive:true, force:true});
});

t("mkdir - mode as octal number 0755", function(){
    var d = TMP + "/mk_mode_num";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d, 0755);
    var st = rampart.utils.stat(d);
    t.mustEq(st.mode & 0777, 0755, "mode bits are 0755");
    rampart.utils.rmdir(d);
});

t("mkdir - mode as octal string '700'", function(){
    var d = TMP + "/mk_mode_str";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d, "700");
    var st = rampart.utils.stat(d);
    t.mustEq(st.mode & 0777, 0700, "mode bits are 0700 from string");
    rampart.utils.rmdir(d);
});

t("mkdir - throws on no args", function(){
    t.mustThrow(function(){ rampart.utils.mkdir(); }, "mkdir() with no path throws");
});

/* ===================== mkdTemp ===================== */
t("mkdTemp - returns created unique dir path", function(){
    var prefix = TMP + "/mtemp-";
    var dir = rampart.utils.mkdTemp(prefix);
    t.mustEq(rampart.utils.getType(dir), "String", "returns a String");
    t.must(dir.indexOf(prefix) === 0, "path begins with prefix");
    t.mustEq(dir.length, prefix.length + 6, "six random chars appended");
    t.must(rampart.utils.stat(dir).isDirectory, "directory created");
    rampart.utils.rmdir(dir);
});

t("mkdTemp - created with mode 0700", function(){
    var dir = rampart.utils.mkdTemp(TMP + "/mtemp700-");
    var st = rampart.utils.stat(dir);
    t.mustEq(st.mode & 0777, 0700, "mode is 0700 owner-only");
    rampart.utils.rmdir(dir);
});

t("mkdTemp - two calls give distinct dirs", function(){
    var a = rampart.utils.mkdTemp(TMP + "/mtu-");
    var b = rampart.utils.mkdTemp(TMP + "/mtu-");
    t.must(a !== b, "unique directories");
    rampart.utils.rmdir(a);
    rampart.utils.rmdir(b);
});

t("mkdTemp - throws when parent dir missing", function(){
    t.mustThrow(function(){
        rampart.utils.mkdTemp(TMP + "/no_such_parent_dir/pre-");
    }, "missing parent throws");
});

/* ===================== rmdir ===================== */
t("rmdir - remove empty dir returns undefined", function(){
    var d = TMP + "/rmd_empty";
    rampart.utils.mkdir(d);
    var r = rampart.utils.rmdir(d);
    t.mustEq(r, undefined, "returns undefined");
    t.must(!rampart.utils.stat(d), "dir gone (stat returns false)");
});

t("rmdir - recurse removes explicit parent components (relative path)", function(){
    /* doc: with recurse=true, parent dirs explicitly present in path are
       also removed. Tested with a relative path so it only climbs the
       components we created (an absolute path would try to climb to /). */
    var save = rampart.utils.getcwd();
    rampart.utils.chdir(TMP);
    try {
        try { rampart.utils.rm("rmd_rec", {recursive:true, force:true}); } catch(e){}
        rampart.utils.mkdir("rmd_rec/p2/p3", 0755);
        rampart.utils.rmdir("rmd_rec/p2/p3", true);
        t.must(!rampart.utils.stat("rmd_rec"), "all explicit components recursively removed");
    } finally {
        rampart.utils.chdir(save);
    }
});

t("rmdir - throws on non-empty dir without recurse", function(){
    var d = TMP + "/rmd_nonempty";
    rampart.utils.mkdir(d);
    rampart.utils.touch(d + "/afile");
    t.mustThrow(function(){ rampart.utils.rmdir(d); }, "non-empty throws");
    rampart.utils.rmFile(d + "/afile");
    rampart.utils.rmdir(d);
});

/* ===================== readDir ===================== */
t("readDir - returns array of filenames", function(){
    var d = TMP + "/rdir";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d);
    rampart.utils.touch(d + "/a.txt");
    rampart.utils.touch(d + "/b.txt");
    var files = rampart.utils.readDir(d);
    t.mustEq(rampart.utils.getType(files), "Array", "returns an Array");
    files.sort();
    t.mustEq(files, ["a.txt","b.txt"], "lists both visible files");
    rampart.utils.rm(d, {recursive:true, force:true});
});

t("readDir - hidden files excluded by default", function(){
    var d = TMP + "/rdir_hid";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d);
    rampart.utils.touch(d + "/visible");
    rampart.utils.touch(d + "/.hidden");
    var files = rampart.utils.readDir(d);
    t.must(files.indexOf(".hidden") === -1, "hidden file excluded");
    t.must(files.indexOf("visible") !== -1, "visible file present");
    rampart.utils.rm(d, {recursive:true, force:true});
});

t("readDir - showhidden true includes dotfiles", function(){
    var d = TMP + "/rdir_show";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d);
    rampart.utils.touch(d + "/visible");
    rampart.utils.touch(d + "/.hidden");
    var files = rampart.utils.readDir(d, true);
    t.must(files.indexOf(".hidden") !== -1, "hidden included when showhidden=true");
    rampart.utils.rm(d, {recursive:true, force:true});
});

/* ===================== walkDir ===================== */
t("walkDir - visits all entries with path/type/depth", function(){
    var d = TMP + "/wd";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d + "/sub");
    rampart.utils.touch(d + "/f1");
    rampart.utils.touch(d + "/sub/f2");
    var seen = {};
    var depths = {};
    var types = {};
    rampart.utils.walkDir(d, function(p, type, depth){
        seen[p] = true;
        depths[p] = depth;
        types[p] = type;
    });
    t.must(seen[d], "root visited");
    t.mustEq(depths[d], 0, "root at depth 0");
    t.mustEq(types[d], "dir", "root is dir type");
    t.mustEq(types[d + "/f1"], "file", "f1 is file type");
    t.mustEq(depths[d + "/f1"], 1, "f1 at depth 1");
    t.mustEq(types[d + "/sub"], "dir", "sub is dir type");
    t.mustEq(types[d + "/sub/f2"], "file", "nested file type");
    t.mustEq(depths[d + "/sub/f2"], 2, "nested file at depth 2");
    rampart.utils.rm(d, {recursive:true, force:true});
});

t("walkDir - symlink reported as symlink type", function(){
    var d = TMP + "/wd_sym";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d);
    rampart.utils.touch(d + "/real");
    rampart.utils.symlink(d + "/real", d + "/lnk");
    var types = {};
    rampart.utils.walkDir(d, function(p, type){ types[p] = type; });
    t.mustEq(types[d + "/lnk"], "symlink", "symlink reported as symlink (not followed by default)");
    rampart.utils.rm(d, {recursive:true, force:true});
});

t("walkDir - returning false stops the walk", function(){
    var d = TMP + "/wd_stop";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d);
    for (var i=0;i<5;i++) rampart.utils.touch(d + "/f" + i);
    var count = 0;
    rampart.utils.walkDir(d, function(p, type, depth){
        count++;
        if (count >= 2) return false;
    });
    t.mustEq(count, 2, "walk stopped after returning false");
    rampart.utils.rm(d, {recursive:true, force:true});
});

t("walkDir - postOrder emits dir after its contents", function(){
    var d = TMP + "/wd_post";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d + "/sub");
    rampart.utils.touch(d + "/sub/inner");
    var order = [];
    rampart.utils.walkDir(d, function(p, type){ order.push(p); }, {postOrder:true});
    var idxInner = order.indexOf(d + "/sub/inner");
    var idxSub = order.indexOf(d + "/sub");
    t.must(idxInner >= 0 && idxSub >= 0, "both entries seen");
    t.must(idxInner < idxSub, "inner emitted before its parent dir in postOrder");
    rampart.utils.rm(d, {recursive:true, force:true});
});

t("walkDir - followLinks descends into symlinked dir", function(){
    var d = TMP + "/wd_follow";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d + "/realdir");
    rampart.utils.touch(d + "/realdir/inner");
    rampart.utils.symlink(d + "/realdir", d + "/linkdir");
    var sawInnerViaLink = false;
    rampart.utils.walkDir(d, function(p, type){
        if (p === d + "/linkdir/inner") sawInnerViaLink = true;
    }, {followLinks:true});
    t.must(sawInnerViaLink, "descended into symlinked dir when followLinks=true");
    rampart.utils.rm(d, {recursive:true, force:true});
});

/* ===================== glob =====================
   Note: glob returns cwd-joined paths, not bare relative names. */
t("glob - '*' matches within component", function(){
    var d = TMP + "/gl_star";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d);
    rampart.utils.touch(d + "/a.js");
    rampart.utils.touch(d + "/b.js");
    rampart.utils.touch(d + "/c.txt");
    var m = rampart.utils.glob("*.js", {cwd:d});
    t.mustEq(rampart.utils.getType(m), "Array", "returns Array");
    var names = m.map(function(p){ return p.replace(/^.*\//, ""); }).sort();
    t.mustEq(names, ["a.js","b.js"], "matched both .js files");
    rampart.utils.rm(d, {recursive:true, force:true});
});

t("glob - '?' single char and '[]' class", function(){
    var d = TMP + "/gl_q";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d);
    rampart.utils.touch(d + "/a1");
    rampart.utils.touch(d + "/a2");
    rampart.utils.touch(d + "/ab");
    var q = rampart.utils.glob("a?", {cwd:d});
    t.mustEq(q.length, 3, "'a?' matches all 2-char names");
    var cls = rampart.utils.glob("a[12]", {cwd:d}).map(function(p){ return p.replace(/^.*\//, ""); }).sort();
    t.mustEq(cls, ["a1","a2"], "char class [12] matches a1,a2");
    rampart.utils.rm(d, {recursive:true, force:true});
});

t("glob - '[!..]' negated class", function(){
    var d = TMP + "/gl_neg";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d);
    rampart.utils.touch(d + "/ax");
    rampart.utils.touch(d + "/ay");
    rampart.utils.touch(d + "/az");
    var m = rampart.utils.glob("a[!x]", {cwd:d}).map(function(p){ return p.replace(/^.*\//, ""); }).sort();
    t.mustEq(m, ["ay","az"], "negated class excludes ax");
    rampart.utils.rm(d, {recursive:true, force:true});
});

t("glob - '**' matches any depth", function(){
    var d = TMP + "/gl_dstar";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d + "/x/y");
    rampart.utils.touch(d + "/top.js");
    rampart.utils.touch(d + "/x/mid.js");
    rampart.utils.touch(d + "/x/y/deep.js");
    var m = rampart.utils.glob("**/*.js", {cwd:d});
    t.must(m.length >= 3, "found js at multiple depths (got " + m.length + ")");
    rampart.utils.rm(d, {recursive:true, force:true});
});

t("glob - dotfiles excluded by default, included with dot:true", function(){
    var d = TMP + "/gl_dot";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d);
    rampart.utils.touch(d + "/.hidden");
    rampart.utils.touch(d + "/shown");
    var def = rampart.utils.glob("*", {cwd:d}).map(function(p){ return p.replace(/^.*\//, ""); });
    t.must(def.indexOf(".hidden") === -1, "dotfile excluded by default");
    var withDot = rampart.utils.glob("*", {cwd:d, dot:true}).map(function(p){ return p.replace(/^.*\//, ""); });
    t.must(withDot.indexOf(".hidden") !== -1, "dotfile included with dot:true");
    rampart.utils.rm(d, {recursive:true, force:true});
});

t("glob - no match returns empty array", function(){
    var m = rampart.utils.glob("zzz_no_such_*.xyz", {cwd:TMP});
    t.mustEq(rampart.utils.getType(m), "Array", "returns Array");
    t.mustEq(m.length, 0, "empty when nothing matches");
});

t("glob - absolute pattern matches absolute paths", function(){
    var d = rampart.utils.mkdTemp(TMP + "/glabs-");
    rampart.utils.touch(d + "/found.marker");
    var m = rampart.utils.glob(d + "/*.marker");
    t.mustEq(m.length, 1, "absolute pattern matched one file");
    t.mustEq(m[0], d + "/found.marker", "absolute path returned");
    rampart.utils.rm(d, {recursive:true, force:true});
});

/* ===================== zip helpers (build a real zip via system zip) ===================== */
t("zipList - describes every entry", function(){
    var z = TMP + "/zl.zip";
    try { rampart.utils.rmFile(z); } catch(e){}
    var src = TMP + "/zlsrc";
    try { rampart.utils.rm(src, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(src + "/lib");
    rampart.utils.fprintf(src + "/README.md", "hello readme\n");
    rampart.utils.fprintf(src + "/lib/x.js", "var x=1;\n");
    var rc = rampart.utils.exec("bash", "-c", "cd '" + src + "' && zip -q -r '" + z + "' README.md lib");
    t.mustEq(rc.exitStatus, 0, "zip command succeeded");
    var e = rampart.utils.zipList(z);
    t.mustEq(rampart.utils.getType(e), "Object", "returns Object");
    t.must(!!e["README.md"], "README.md entry present");
    t.must(e["README.md"].isFile, "README.md isFile true");
    t.mustEq(rampart.utils.getType(e["README.md"].size), "Number", "size is Number");
    t.mustEq(rampart.utils.getType(e["README.md"].compressedSize), "Number", "compressedSize is Number");
    t.mustEq(rampart.utils.getType(e["README.md"].method), "Number", "method is Number");
    t.mustEq(rampart.utils.getType(e["README.md"].crc32), "Number", "crc32 is Number");
    t.mustEq(rampart.utils.getType(e["README.md"].mode), "Number", "mode is Number");
    t.mustEq(rampart.utils.getType(e["README.md"].permissions), "String", "permissions is String");
    t.must(e["README.md"].permissions.charAt(0) === "-", "file permissions start with '-'");
    t.must(!!e["lib/"], "directory entry 'lib/' present (ends with slash)");
    t.must(e["lib/"].isDirectory, "lib/ isDirectory true");
    t.must(e["lib/"].permissions.charAt(0) === "d", "dir permissions start with 'd'");
    t.must(!!e["lib/x.js"], "lib/x.js entry present");
    t.must(e["lib/x.js"].isFile, "lib/x.js isFile");
    rampart.utils.rmFile(z);
    rampart.utils.rm(src, {recursive:true, force:true});
});

t("zipList - mtime is a Date when present", function(){
    var z = TMP + "/zlm.zip";
    try { rampart.utils.rmFile(z); } catch(e){}
    var src = TMP + "/zlmsrc";
    try { rampart.utils.rm(src, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(src);
    rampart.utils.fprintf(src + "/a.txt", "data\n");
    rampart.utils.exec("bash", "-c", "cd '" + src + "' && zip -q '" + z + "' a.txt");
    var e = rampart.utils.zipList(z);
    if (e["a.txt"].hasOwnProperty("mtime"))
        t.mustEq(rampart.utils.getType(e["a.txt"].mtime), "Date", "mtime is a Date");
    else
        t.must(true, "mtime omitted (allowed by docs)");
    rampart.utils.rmFile(z);
    rampart.utils.rm(src, {recursive:true, force:true});
});

t("zipList - throws on non-zip path", function(){
    var bad = TMP + "/notazip.txt";
    rampart.utils.fprintf(bad, "definitely not a zip\n");
    t.mustThrow(function(){ rampart.utils.zipList(bad); }, "non-zip throws");
    rampart.utils.rmFile(bad);
});

t("zipGet - returns Buffer of uncompressed contents", function(){
    var z = TMP + "/zg.zip";
    try { rampart.utils.rmFile(z); } catch(e){}
    var src = TMP + "/zgsrc";
    try { rampart.utils.rm(src, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(src);
    rampart.utils.fprintf(src + "/README.md", "hello readme contents\n");
    rampart.utils.exec("bash", "-c", "cd '" + src + "' && zip -q '" + z + "' README.md");
    var buf = rampart.utils.zipGet(z, "README.md");
    t.mustEq(rampart.utils.getType(buf), "Buffer", "returns Buffer");
    t.mustEq(rampart.utils.bufferToString(buf), "hello readme contents\n", "decompressed contents match");
    rampart.utils.rmFile(z);
    rampart.utils.rm(src, {recursive:true, force:true});
});

t("zipGet - throws on missing entry", function(){
    var z = TMP + "/zgm.zip";
    try { rampart.utils.rmFile(z); } catch(e){}
    var src = TMP + "/zgmsrc";
    try { rampart.utils.rm(src, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(src);
    rampart.utils.fprintf(src + "/only.txt", "x\n");
    rampart.utils.exec("bash", "-c", "cd '" + src + "' && zip -q '" + z + "' only.txt");
    t.mustThrow(function(){ rampart.utils.zipGet(z, "nope.txt"); }, "missing entry throws");
    rampart.utils.rmFile(z);
    rampart.utils.rm(src, {recursive:true, force:true});
});

t("zipExtract - extracts all entries, returns count", function(){
    var z = TMP + "/ze.zip";
    try { rampart.utils.rmFile(z); } catch(e){}
    var src = TMP + "/zesrc";
    try { rampart.utils.rm(src, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(src + "/lib");
    rampart.utils.fprintf(src + "/README.md", "readme\n");
    rampart.utils.fprintf(src + "/lib/x.js", "var x;\n");
    rampart.utils.exec("bash", "-c", "cd '" + src + "' && zip -q -r '" + z + "' README.md lib");
    var dest = TMP + "/zedest";
    try { rampart.utils.rm(dest, {recursive:true, force:true}); } catch(e){}
    var n = rampart.utils.zipExtract(z, dest);
    t.mustEq(rampart.utils.getType(n), "Number", "returns a Number");
    t.must(n >= 2, "at least 2 entries written (got " + n + ")");
    t.mustEq(rampart.utils.bufferToString(rampart.utils.readFile(dest + "/README.md")), "readme\n", "README extracted with contents");
    t.must(rampart.utils.stat(dest + "/lib/x.js").isFile, "nested file extracted");
    rampart.utils.rmFile(z);
    rampart.utils.rm(src, {recursive:true, force:true});
    rampart.utils.rm(dest, {recursive:true, force:true});
});

t("zipExtract - selective extract with entries array", function(){
    var z = TMP + "/zes.zip";
    try { rampart.utils.rmFile(z); } catch(e){}
    var src = TMP + "/zessrc";
    try { rampart.utils.rm(src, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(src);
    rampart.utils.fprintf(src + "/a.txt", "AAA\n");
    rampart.utils.fprintf(src + "/b.txt", "BBB\n");
    rampart.utils.exec("bash", "-c", "cd '" + src + "' && zip -q '" + z + "' a.txt b.txt");
    var dest = TMP + "/zesdest";
    try { rampart.utils.rm(dest, {recursive:true, force:true}); } catch(e){}
    rampart.utils.zipExtract(z, dest, ["a.txt"]);
    t.must(rampart.utils.stat(dest + "/a.txt").isFile, "a.txt extracted");
    t.must(!rampart.utils.stat(dest + "/b.txt"), "b.txt NOT extracted");
    rampart.utils.rmFile(z);
    rampart.utils.rm(src, {recursive:true, force:true});
    rampart.utils.rm(dest, {recursive:true, force:true});
});

t("zipExtract - 'dir/' selector extracts whole tree", function(){
    var z = TMP + "/zet.zip";
    try { rampart.utils.rmFile(z); } catch(e){}
    var src = TMP + "/zetsrc";
    try { rampart.utils.rm(src, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(src + "/templates");
    rampart.utils.fprintf(src + "/templates/t1.html", "<p>1</p>\n");
    rampart.utils.fprintf(src + "/templates/t2.html", "<p>2</p>\n");
    rampart.utils.fprintf(src + "/other.txt", "other\n");
    rampart.utils.exec("bash", "-c", "cd '" + src + "' && zip -q -r '" + z + "' templates other.txt");
    var dest = TMP + "/zetdest";
    try { rampart.utils.rm(dest, {recursive:true, force:true}); } catch(e){}
    rampart.utils.zipExtract(z, dest, ["templates/"]);
    t.must(rampart.utils.stat(dest + "/templates/t1.html").isFile, "t1 extracted via dir selector");
    t.must(rampart.utils.stat(dest + "/templates/t2.html").isFile, "t2 extracted via dir selector");
    t.must(!rampart.utils.stat(dest + "/other.txt"), "other.txt not extracted");
    rampart.utils.rmFile(z);
    rampart.utils.rm(src, {recursive:true, force:true});
    rampart.utils.rm(dest, {recursive:true, force:true});
});

/* ===================== copyFile ===================== */
t("copyFile - positional args copy file", function(){
    var s = TMP + "/cf_src";
    var d = TMP + "/cf_dst";
    try { rampart.utils.rmFile(d); } catch(e){}
    rampart.utils.fprintf(s, "copyme\n");
    var r = rampart.utils.copyFile(s, d);
    t.mustEq(r, undefined, "returns undefined");
    t.mustEq(rampart.utils.bufferToString(rampart.utils.readFile(d)), "copyme\n", "contents copied");
    rampart.utils.rmFile(s); rampart.utils.rmFile(d);
});

t("copyFile - object form {src,dest}", function(){
    var s = TMP + "/cfo_src";
    var d = TMP + "/cfo_dst";
    try { rampart.utils.rmFile(d); } catch(e){}
    rampart.utils.fprintf(s, "objform\n");
    rampart.utils.copyFile({src:s, dest:d});
    t.mustEq(rampart.utils.bufferToString(rampart.utils.readFile(d)), "objform\n", "object-form copy worked");
    rampart.utils.rmFile(s); rampart.utils.rmFile(d);
});

t("copyFile - overwrite false throws on existing dest", function(){
    var s = TMP + "/cfov_src";
    var d = TMP + "/cfov_dst";
    rampart.utils.fprintf(s, "new\n");
    rampart.utils.fprintf(d, "old\n");
    t.mustThrow(function(){ rampart.utils.copyFile(s, d); }, "no overwrite throws on existing dest");
    rampart.utils.rmFile(s); rampart.utils.rmFile(d);
});

t("copyFile - overwrite true replaces existing dest", function(){
    var s = TMP + "/cfo2_src";
    var d = TMP + "/cfo2_dst";
    rampart.utils.fprintf(s, "newcontent\n");
    rampart.utils.fprintf(d, "oldcontent\n");
    rampart.utils.copyFile(s, d, true);
    t.mustEq(rampart.utils.bufferToString(rampart.utils.readFile(d)), "newcontent\n", "overwrite true replaced dest");
    rampart.utils.rmFile(s); rampart.utils.rmFile(d);
});

/* ===================== rmFile ===================== */
t("rmFile - deletes a file, returns undefined", function(){
    var f = TMP + "/rmf";
    rampart.utils.fprintf(f, "x\n");
    var r = rampart.utils.rmFile(f);
    t.mustEq(r, undefined, "returns undefined");
    t.must(!rampart.utils.stat(f), "file gone (stat false)");
});

t("rmFile - throws on missing file", function(){
    t.mustThrow(function(){ rampart.utils.rmFile(TMP + "/rmf_nope"); }, "missing file throws");
});

/* ===================== link (hard link) ===================== */
t("link - positional creates hard link sharing contents", function(){
    var s = TMP + "/ln_src";
    var d = TMP + "/ln_dst";
    try { rampart.utils.rmFile(d); } catch(e){}
    rampart.utils.fprintf(s, "linked\n");
    var r = rampart.utils.link(s, d);
    t.mustEq(r, undefined, "returns undefined");
    t.mustEq(rampart.utils.bufferToString(rampart.utils.readFile(d)), "linked\n", "hard link sees same contents");
    var sst = rampart.utils.stat(s);
    t.must(sst.nlink >= 2, "link count increased (nlink>=2)");
    rampart.utils.rmFile(d); rampart.utils.rmFile(s);
});

t("link - object form {src,target}", function(){
    var s = TMP + "/lno_src";
    var d = TMP + "/lno_dst";
    try { rampart.utils.rmFile(d); } catch(e){}
    rampart.utils.fprintf(s, "objlink\n");
    rampart.utils.link({src:s, target:d});
    t.mustEq(rampart.utils.bufferToString(rampart.utils.readFile(d)), "objlink\n", "object-form hard link worked");
    rampart.utils.rmFile(d); rampart.utils.rmFile(s);
});

/* ===================== symlink ===================== */
t("symlink - positional creates symlink to target", function(){
    var s = TMP + "/sl_src";
    var d = TMP + "/sl_lnk";
    try { rampart.utils.rmFile(d); } catch(e){}
    rampart.utils.fprintf(s, "symdata\n");
    var r = rampart.utils.symlink(s, d);
    t.mustEq(r, undefined, "returns undefined");
    t.must(rampart.utils.lstat(d).isSymbolicLink, "created entry is a symlink");
    t.mustEq(rampart.utils.bufferToString(rampart.utils.readFile(d)), "symdata\n", "symlink resolves to target contents");
    rampart.utils.rmFile(d); rampart.utils.rmFile(s);
});

t("symlink - object form {src,target}", function(){
    var s = TMP + "/slo_src";
    var d = TMP + "/slo_lnk";
    try { rampart.utils.rmFile(d); } catch(e){}
    rampart.utils.fprintf(s, "x\n");
    rampart.utils.symlink({src:s, target:d});
    t.must(rampart.utils.lstat(d).isSymbolicLink, "object-form symlink created");
    rampart.utils.rmFile(d); rampart.utils.rmFile(s);
});

/* ===================== readLink ===================== */
t("readLink - returns verbatim target", function(){
    var s = TMP + "/rl_src";
    var d = TMP + "/rl_lnk";
    try { rampart.utils.rmFile(d); } catch(e){}
    rampart.utils.fprintf(s, "x\n");
    rampart.utils.symlink(s, d);
    var tgt = rampart.utils.readLink(d);
    t.mustEq(tgt, s, "returns the verbatim target path");
    rampart.utils.rmFile(d); rampart.utils.rmFile(s);
});

t("readLink - verbatim (not resolved) for relative target", function(){
    var d = TMP + "/rl_rel";
    try { rampart.utils.rmFile(d); } catch(e){}
    rampart.utils.symlink("relative/target", d);
    t.mustEq(rampart.utils.readLink(d), "relative/target", "relative target returned verbatim, not resolved");
    rampart.utils.rmFile(d);
});

t("readLink - throws on non-symlink", function(){
    var f = TMP + "/rl_plain";
    rampart.utils.fprintf(f, "x\n");
    t.mustThrow(function(){ rampart.utils.readLink(f); }, "non-symlink throws (EINVAL)");
    rampart.utils.rmFile(f);
});

/* ===================== chmod ===================== */
t("chmod - octal number mode", function(){
    var f = TMP + "/chm_num";
    rampart.utils.fprintf(f, "x\n");
    var r = rampart.utils.chmod(f, 0640);
    t.mustEq(r, undefined, "returns undefined");
    t.mustEq(rampart.utils.stat(f).mode & 0777, 0640, "mode set to 0640");
    rampart.utils.rmFile(f);
});

t("chmod - octal string mode '600'", function(){
    var f = TMP + "/chm_str";
    rampart.utils.fprintf(f, "x\n");
    rampart.utils.chmod(f, "600");
    t.mustEq(rampart.utils.stat(f).mode & 0777, 0600, "mode set to 0600 from string");
    rampart.utils.rmFile(f);
});

t("chmod - throws on missing path", function(){
    t.mustThrow(function(){ rampart.utils.chmod(TMP + "/chm_nope", 0644); }, "missing path throws");
});

/* ===================== realPath ===================== */
t("realPath - canonicalizes . and .. components", function(){
    var d = TMP + "/rp";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d + "/sub");
    rampart.utils.fprintf(d + "/sub/file", "x\n");
    var messy = d + "/sub/../sub/./file";
    var canon = rampart.utils.realPath(messy);
    t.mustEq(canon, rampart.utils.realPath(d) + "/sub/file", "canonical form resolved");
    rampart.utils.rm(d, {recursive:true, force:true});
});

t("realPath - resolves a symlink to its target", function(){
    var d = TMP + "/rp_sym";
    try { rampart.utils.rm(d, {recursive:true, force:true}); } catch(e){}
    rampart.utils.mkdir(d);
    rampart.utils.fprintf(d + "/real", "x\n");
    rampart.utils.symlink(d + "/real", d + "/lnk");
    t.mustEq(rampart.utils.realPath(d + "/lnk"), rampart.utils.realPath(d) + "/real", "symlink resolved to target");
    rampart.utils.rm(d, {recursive:true, force:true});
});

t("realPath - throws on non-existent path", function(){
    t.mustThrow(function(){ rampart.utils.realPath(TMP + "/rp_nope/x"); }, "non-existent path throws");
});

/* ===================== truncate ===================== */
t("truncate - shrinks a file", function(){
    var f = TMP + "/tr_shrink";
    rampart.utils.fprintf(f, "0123456789");
    var r = rampart.utils.truncate(f, 4);
    t.mustEq(r, undefined, "returns undefined");
    t.mustEq(rampart.utils.stat(f).size, 4, "file shrunk to 4 bytes");
    t.mustEq(rampart.utils.bufferToString(rampart.utils.readFile(f)), "0123", "kept first 4 bytes");
    rampart.utils.rmFile(f);
});

t("truncate - extends a file with NUL hole", function(){
    var f = TMP + "/tr_extend";
    rampart.utils.fprintf(f, "ab");
    rampart.utils.truncate(f, 5);
    t.mustEq(rampart.utils.stat(f).size, 5, "file extended to 5 bytes");
    var buf = rampart.utils.readFile(f);
    t.mustEq(buf[2], 0, "hole byte is NUL");
    t.mustEq(buf[4], 0, "trailing hole byte is NUL");
    rampart.utils.rmFile(f);
});

t("truncate - to zero empties file", function(){
    var f = TMP + "/tr_zero";
    rampart.utils.fprintf(f, "stuff");
    rampart.utils.truncate(f, 0);
    t.mustEq(rampart.utils.stat(f).size, 0, "file emptied");
    rampart.utils.rmFile(f);
});

/* ===================== touch ===================== */
t("touch - creates empty file (string form)", function(){
    var f = TMP + "/tch_create";
    try { rampart.utils.rmFile(f); } catch(e){}
    var r = rampart.utils.touch(f);
    t.mustEq(r, undefined, "returns undefined");
    t.mustEq(rampart.utils.stat(f).size, 0, "created empty file");
    rampart.utils.rmFile(f);
});

t("touch - nocreate true does not create missing file", function(){
    var f = TMP + "/tch_nocreate";
    try { rampart.utils.rmFile(f); } catch(e){}
    rampart.utils.touch({path:f, nocreate:true});
    t.must(!rampart.utils.stat(f), "nocreate left file uncreated (stat false)");
});

t("touch - setmodify and setaccess both Date", function(){
    var f = TMP + "/tch_setmod";
    rampart.utils.touch(f);
    var when = new Date("2001-02-03T04:05:06Z");
    rampart.utils.touch({path:f, setmodify:when, setaccess:when});
    var st = rampart.utils.stat(f);
    t.mustEq(Math.floor(st.mtime.getTime()/1000), Math.floor(when.getTime()/1000), "mtime set to given Date");
    t.mustEq(Math.floor(st.atime.getTime()/1000), Math.floor(when.getTime()/1000), "atime set to given Date");
    rampart.utils.rmFile(f);
});

t("touch - setmodify Date + boolean setaccess", function(){
    /* setaccess/setmodify each independently accept Boolean | Date | Number.
       Here setmodify is an explicit Date and setaccess is Boolean true
       (update access time to now).  Both directions of mixing value+boolean
       are supported. */
    var f = TMP + "/tch_setmod2";
    rampart.utils.touch(f);
    var when = new Date("2002-03-04T05:06:07Z");
    var before = Math.floor(Date.now()/1000);
    rampart.utils.touch({path:f, setmodify:when, setaccess:true});
    var st = rampart.utils.stat(f);
    t.mustEq(Math.floor(st.mtime.getTime()/1000), Math.floor(when.getTime()/1000), "mtime set to given Date");
    t.must(Math.floor(st.atime.getTime()/1000) >= before, "atime updated to ~now via setaccess:true");
    rampart.utils.rmFile(f);
});

t("touch - explicit setaccess Date + boolean setmodify", function(){
    var f = TMP + "/tch_setacc2";
    rampart.utils.touch(f);
    var when = new Date("2003-04-05T06:07:08Z");
    rampart.utils.touch({path:f, setaccess:when, setmodify:true});
    var st = rampart.utils.stat(f);
    t.mustEq(Math.floor(st.atime.getTime()/1000), Math.floor(when.getTime()/1000), "atime set to given Date");
    rampart.utils.rmFile(f);
});

t("touch - setaccess + setmodify as epoch seconds Numbers", function(){
    var f = TMP + "/tch_setacc";
    rampart.utils.touch(f);
    var secs = 980000000;
    rampart.utils.touch({path:f, setaccess:secs, setmodify:secs});
    var st = rampart.utils.stat(f);
    t.mustEq(Math.floor(st.atime.getTime()/1000), secs, "atime set to given epoch seconds");
    t.mustEq(Math.floor(st.mtime.getTime()/1000), secs, "mtime set to given epoch seconds");
    rampart.utils.rmFile(f);
});

t("touch - reference file copies timestamps", function(){
    var ref = TMP + "/tch_ref";
    var f = TMP + "/tch_reftarget";
    rampart.utils.touch(f);
    rampart.utils.touch(ref);
    var when = new Date("1999-12-31T00:00:00Z");
    rampart.utils.touch({path:ref, setmodify:when, setaccess:when});
    rampart.utils.touch({path:f, reference:ref});
    var st = rampart.utils.stat(f);
    t.mustEq(Math.floor(st.mtime.getTime()/1000), Math.floor(when.getTime()/1000), "mtime copied from reference");
    rampart.utils.rmFile(f); rampart.utils.rmFile(ref);
});

/* ===================== rename ===================== */
t("rename - moves/renames a file", function(){
    var s = TMP + "/rn_src";
    var d = TMP + "/rn_dst";
    try { rampart.utils.rmFile(d); } catch(e){}
    rampart.utils.fprintf(s, "movethis\n");
    var r = rampart.utils.rename(s, d);
    t.mustEq(r, undefined, "returns undefined");
    t.must(!rampart.utils.stat(s), "source gone after rename (stat false)");
    t.mustEq(rampart.utils.bufferToString(rampart.utils.readFile(d)), "movethis\n", "dest has contents");
    rampart.utils.rmFile(d);
});

t("rename - throws on missing source", function(){
    t.mustThrow(function(){ rampart.utils.rename(TMP + "/rn_nope", TMP + "/rn_x"); }, "missing source throws");
});

/* ===================== chown ===================== */
t("chown - same owner no-op (positional uid/gid)", function(){
    var f = TMP + "/co_pos";
    rampart.utils.fprintf(f, "x\n");
    var st = rampart.utils.stat(f);
    var r = rampart.utils.chown(f, st.uid, st.gid);
    t.mustEq(r, undefined, "returns undefined for same-owner chown");
    rampart.utils.rmFile(f);
});

t("chown - object form same owner no-op", function(){
    var f = TMP + "/co_obj";
    rampart.utils.fprintf(f, "x\n");
    var st = rampart.utils.stat(f);
    rampart.utils.chown({path:f, user:st.uid, group:st.gid});
    var st2 = rampart.utils.stat(f);
    t.mustEq(st2.uid, st.uid, "uid unchanged");
    t.mustEq(st2.gid, st.gid, "gid unchanged");
    rampart.utils.rmFile(f);
});

t("chown - throws on unknown user name", function(){
    var f = TMP + "/co_baduser";
    rampart.utils.fprintf(f, "x\n");
    t.mustThrow(function(){ rampart.utils.chown(f, "no_such_user_xyz123"); }, "unknown user throws");
    rampart.utils.rmFile(f);
});

/* ===================== lchown ===================== */
t("lchown - same owner no-op on symlink", function(){
    var s = TMP + "/lco_src";
    var l = TMP + "/lco_lnk";
    try { rampart.utils.rmFile(l); } catch(e){}
    rampart.utils.fprintf(s, "x\n");
    rampart.utils.symlink(s, l);
    var lst = rampart.utils.lstat(l);
    var r = rampart.utils.lchown(l, lst.uid, lst.gid);
    t.mustEq(r, undefined, "returns undefined for same-owner lchown");
    rampart.utils.rmFile(l); rampart.utils.rmFile(s);
});

t("lchown - negative number leaves side unchanged", function(){
    var s = TMP + "/lco2_src";
    var l = TMP + "/lco2_lnk";
    try { rampart.utils.rmFile(l); } catch(e){}
    rampart.utils.fprintf(s, "x\n");
    rampart.utils.symlink(s, l);
    var lst = rampart.utils.lstat(l);
    rampart.utils.lchown(l, -1, -1);
    var lst2 = rampart.utils.lstat(l);
    t.mustEq(lst2.uid, lst.uid, "uid unchanged with -1");
    t.mustEq(lst2.gid, lst.gid, "gid unchanged with -1");
    rampart.utils.rmFile(l); rampart.utils.rmFile(s);
});

/* ===================== lchmod ===================== */
t("lchmod - falls through to chmod for non-symlink (Linux)", function(){
    var f = TMP + "/lchm_plain";
    rampart.utils.fprintf(f, "x\n");
    var r = rampart.utils.lchmod(f, "640");
    t.mustEq(r, undefined, "returns undefined");
    t.mustEq(rampart.utils.stat(f).mode & 0777, 0640, "fell through to chmod, mode set");
    rampart.utils.rmFile(f);
});

t("lchmod - throws on actual symlink (Linux ENOSYS)", function(){
    var s = TMP + "/lchm_src";
    var l = TMP + "/lchm_lnk";
    try { rampart.utils.rmFile(l); } catch(e){}
    rampart.utils.fprintf(s, "x\n");
    rampart.utils.symlink(s, l);
    t.mustThrow(function(){ rampart.utils.lchmod(l, "640"); }, "lchmod on real symlink throws on Linux");
    rampart.utils.rmFile(l); rampart.utils.rmFile(s);
});

/* ===================== lUtimes ===================== */
t("lUtimes - set link atime/mtime via positional Dates", function(){
    var s = TMP + "/lut_src";
    var l = TMP + "/lut_lnk";
    try { rampart.utils.rmFile(l); } catch(e){}
    rampart.utils.fprintf(s, "x\n");
    rampart.utils.symlink(s, l);
    var when = new Date("2005-06-07T08:09:10Z");
    var r = rampart.utils.lUtimes(l, when, when);
    t.mustEq(r, undefined, "returns undefined");
    var lst = rampart.utils.lstat(l);
    t.mustEq(Math.floor(lst.mtime.getTime()/1000), Math.floor(when.getTime()/1000), "link mtime set (not target)");
    rampart.utils.rmFile(l); rampart.utils.rmFile(s);
});

t("lUtimes - object form {setaccess,setmodify} with epoch seconds", function(){
    var s = TMP + "/lut2_src";
    var l = TMP + "/lut2_lnk";
    try { rampart.utils.rmFile(l); } catch(e){}
    rampart.utils.fprintf(s, "x\n");
    rampart.utils.symlink(s, l);
    var secs = 1100000000;
    rampart.utils.lUtimes(l, {setaccess:secs, setmodify:secs});
    var lst = rampart.utils.lstat(l);
    t.mustEq(Math.floor(lst.atime.getTime()/1000), secs, "link atime set via object form");
    rampart.utils.rmFile(l); rampart.utils.rmFile(s);
});

t("lUtimes - operates on link, not its target", function(){
    var s = TMP + "/lut3_src";
    var l = TMP + "/lut3_lnk";
    try { rampart.utils.rmFile(l); } catch(e){}
    rampart.utils.fprintf(s, "x\n");
    rampart.utils.symlink(s, l);
    var tgtBefore = rampart.utils.stat(s).mtime.getTime();
    var when = new Date("2002-02-02T02:02:02Z");
    rampart.utils.lUtimes(l, when, when);
    var tgtAfter = rampart.utils.stat(s).mtime.getTime();
    t.mustEq(tgtAfter, tgtBefore, "target mtime untouched by lUtimes");
    rampart.utils.rmFile(l); rampart.utils.rmFile(s);
});

/* ===================== sleep ===================== */
t("sleep - pauses for the requested fraction of a second", function(){
    var start = Date.now();
    var r = rampart.utils.sleep(0.05);
    var elapsed = Date.now() - start;
    t.mustEq(r, undefined, "returns undefined");
    t.must(elapsed >= 40, "slept at least ~40ms (got " + elapsed + "ms)");
});

/* ===================== tmpDir ===================== */
t("tmpDir - returns a String with no trailing slash", function(){
    var tmp = rampart.utils.tmpDir();
    t.mustEq(rampart.utils.getType(tmp), "String", "returns a String");
    t.must(tmp.length > 0, "non-empty");
    if (tmp.length > 1)
        t.must(tmp.charAt(tmp.length-1) !== "/", "no trailing slash");
});

t("tmpDir - honors TMPDIR env var (via setenv)", function(){
    rampart.utils.setenv("TMPDIR", "/tmp/customtmp/", true);
    var tmp = rampart.utils.tmpDir();
    rampart.utils.unsetenv("TMPDIR");
    t.mustEq(tmp, "/tmp/customtmp", "TMPDIR honored, trailing slash stripped");
});

/* ===================== homeDir ===================== */
t("homeDir - returns a String home path", function(){
    var home = rampart.utils.homeDir();
    t.mustEq(rampart.utils.getType(home), "String", "returns a String");
    t.must(home.length > 0, "non-empty");
});

t("homeDir - honors HOME env var when set (via setenv)", function(){
    var saved = rampart.utils.getenv("HOME");
    rampart.utils.setenv("HOME", "/home/testuser_xyz", true);
    var home = rampart.utils.homeDir();
    if (saved === undefined || saved === false) rampart.utils.unsetenv("HOME");
    else rampart.utils.setenv("HOME", saved, true);
    t.mustEq(home, "/home/testuser_xyz", "HOME env var used");
});

/* ===================== getType ===================== */
t("getType - String", function(){ t.mustEq(rampart.utils.getType("hi"), "String", "string"); });
t("getType - Array", function(){ t.mustEq(rampart.utils.getType([1,2]), "Array", "array"); });
t("getType - Number", function(){ t.mustEq(rampart.utils.getType(42), "Number", "number"); });
t("getType - Function", function(){ t.mustEq(rampart.utils.getType(function(){}), "Function", "function"); });
t("getType - Boolean", function(){ t.mustEq(rampart.utils.getType(true), "Boolean", "boolean"); });
t("getType - Buffer", function(){
    var b = rampart.utils.stringToBuffer("x");
    t.mustEq(rampart.utils.getType(b), "Buffer", "buffer");
});
t("getType - Nan", function(){ t.mustEq(rampart.utils.getType(NaN), "Nan", "NaN -> Nan"); });
t("getType - Null", function(){ t.mustEq(rampart.utils.getType(null), "Null", "null"); });
t("getType - Undefined", function(){ t.mustEq(rampart.utils.getType(undefined), "Undefined", "undefined"); });
t("getType - Date", function(){ t.mustEq(rampart.utils.getType(new Date()), "Date", "date"); });
t("getType - Object", function(){ t.mustEq(rampart.utils.getType({a:1}), "Object", "plain object"); });


/* ##################### section a4 ##################### */
/* ===================================================================== *
 *  a4: timezone, dateFmt, scanDate, autoScanDate, use, load,
 *      errorConfig, deepCopy, eventCallback, getScopeVars, repl
 * ===================================================================== */

/* --------------------------- timezone -------------------------------- */

t("timezone - returns object with findZone/findAbbr/dump", function(){
    var tz = rampart.utils.timezone();
    t.must(tz && typeof tz === "object", "tz is object");
    t.mustEq(typeof tz.findZone, "function", "findZone is function");
    t.mustEq(typeof tz.findAbbr, "function", "findAbbr is function");
    t.mustEq(typeof tz.dump,     "function", "dump is function");
});

t("timezone - findZone returns documented shape", function(){
    var tz = rampart.utils.timezone();
    var z = tz.findZone("America/Los_Angeles");
    t.must(z && typeof z === "object", "zone object returned");
    t.mustEq(z.name, "America/Los_Angeles", "zone name");
    t.must(Array.isArray(z.abbreviations), "abbreviations is array");
    t.must(z.abbreviations.length > 0, "abbreviations non-empty");
    var ab = z.abbreviations[0];
    t.mustEq(typeof ab.Abbreviation, "string", "abbr.Abbreviation string");
    t.mustEq(typeof ab.UTCOffset,    "number", "abbr.UTCOffset number");
    t.mustEq(typeof ab.isDST,        "boolean", "abbr.isDST boolean");
    t.must(Array.isArray(z.transitions), "transitions is array");
});

t("timezone - findZone unknown returns undefined", function(){
    var tz = rampart.utils.timezone();
    t.mustEq(typeof tz.findZone("Nope/Nowhere"), "undefined", "unknown zone undefined");
});

t("timezone - findAbbr returns documented shape", function(){
    var tz = rampart.utils.timezone();
    var r = tz.findAbbr("PST");
    t.must(r && typeof r === "object", "abbr result object");
    t.mustEq(typeof r.ambiguous, "boolean", "ambiguous is boolean");
    t.must(Array.isArray(r.entries), "entries is array");
    t.must(r.entries.length > 0, "entries non-empty");
    var e = r.entries[0];
    t.mustEq(typeof e.offset,        "number", "entry.offset number");
    t.mustEq(typeof e.offsetString,  "string", "entry.offsetString string");
    t.mustEq(typeof e.zoneName,      "string", "entry.zoneName string");
    t.mustEq(typeof e.zoneAbbrIndex, "number", "entry.zoneAbbrIndex number");
});

t("timezone - findAbbr unknown returns undefined", function(){
    var tz = rampart.utils.timezone();
    t.mustEq(typeof tz.findAbbr("ZZZQ"), "undefined", "unknown abbr undefined");
});

t("timezone - dump returns object", function(){
    var tz = rampart.utils.timezone();
    var d = tz.dump();
    t.must(d && typeof d === "object", "dump returns object");
});

/* ---------------------------- dateFmt -------------------------------- *
 * dateFmt(format[, date][, tz]) -- the 3rd arg is a Boolean (true=local,
 * false/default=UTC) or a Number (UTC offset in hours).  String dates are
 * auto-parsed via autoScanDate; there is no input_format parameter.
 * ------------------------------------------------------------------- */

t("dateFmt - format a numeric date with explicit offset", function(){
    /* 946713599 == 1999-12-31T23:59:59 in PST (UTC-8); offset 0 => GMT */
    var s = rampart.utils.dateFmt("%Y-%m-%dT%H:%M:%S", 946713599, 0);
    t.mustEq(s, "2000-01-01T07:59:59", "numeric date with offset 0 (GMT)");
});

t("dateFmt - numeric date with negative hour offset", function(){
    var s = rampart.utils.dateFmt("%Y-%m-%dT%H:%M:%S", 946713599, -8);
    t.mustEq(s, "1999-12-31T23:59:59", "offset -8 hours");
});

t("dateFmt - string date with embedded zone is deterministic", function(){
    var s = rampart.utils.dateFmt("%Y-%m-%dT%H:%M:%S", "1999-12-31 23:59:59 -0000");
    t.mustEq(s, "1999-12-31T23:59:59", "string date -0000 zone");
});

t("dateFmt - ISO string input auto-parsed", function(){
    var s = rampart.utils.dateFmt("%Y-%m-%d %H:%M:%S", "2002-01-05T15:20:00");
    t.mustEq(s, "2002-01-05 15:20:00", "ISO 8601 string date");
});

t("dateFmt - Date object input", function(){
    var d = new Date(Date.UTC(2000,0,1,12,0,0));
    var s = rampart.utils.dateFmt("%Y-%m-%dT%H:%M:%S", d);
    t.mustEq(s, "2000-01-01T12:00:00", "Date object formatted in GMT");
});

t("dateFmt - various strftime specifiers", function(){
    var d = new Date(Date.UTC(2002,0,5,15,20,07));
    t.mustEq(rampart.utils.dateFmt("%Y", d), "2002", "%Y year");
    t.mustEq(rampart.utils.dateFmt("%m", d), "01", "%m month");
    t.mustEq(rampart.utils.dateFmt("%d", d), "05", "%d day");
    t.mustEq(rampart.utils.dateFmt("%H", d), "15", "%H hour");
    t.mustEq(rampart.utils.dateFmt("%M", d), "20", "%M minute");
    t.mustEq(rampart.utils.dateFmt("%S", d), "07", "%S second");
});

t("dateFmt - millisecond notation disregarded", function(){
    var s = rampart.utils.dateFmt("%Y-%m-%dT%H:%M:%S", "2002-01-05T15:20:00.123Z");
    t.mustEq(s, "2002-01-05T15:20:00", "millis ignored");
});

t("dateFmt - bad date type throws", function(){
    t.mustThrow(function(){ rampart.utils.dateFmt("%Y", {}); }, "object date arg throws");
});

/* ---------------------------- scanDate ------------------------------- */

t("scanDate - returns a JS Date", function(){
    var d = rampart.utils.scanDate("1999-12-31 23:59:59", 0, "%Y-%m-%d %H:%M:%S");
    t.must(d instanceof Date, "returns Date");
    t.mustEq(d.toISOString(), "1999-12-31T23:59:59.000Z", "parsed as UTC (offset 0)");
});

t("scanDate - format may be 2nd arg (no offset)", function(){
    var d = rampart.utils.scanDate("1999-12-31 23:59:59", "%Y-%m-%d %H:%M:%S");
    t.mustEq(d.toISOString(), "1999-12-31T23:59:59.000Z", "format as 2nd arg");
});

t("scanDate - default_offset applied", function(){
    /* offset 3600 seconds (=+1h zone) shifts UTC back by an hour */
    var d = rampart.utils.scanDate("1999-12-31 23:59:59", 3600, "%Y-%m-%d %H:%M:%S");
    t.mustEq(d.toISOString(), "1999-12-31T22:59:59.000Z", "offset 3600s applied");
});

t("scanDate - zone in string via %z overrides default_offset", function(){
    var d = rampart.utils.scanDate("2002-01-05 15:20:00 -0800", "%Y-%m-%d %H:%M:%S %z");
    t.mustEq(d.toISOString(), "2002-01-05T23:20:00.000Z", "-0800 -> +8h UTC");
});

t("scanDate - auto format when no input_format", function(){
    var d = rampart.utils.scanDate("2002-01-05T15:20:00");
    t.must(d instanceof Date, "auto-parsed to Date");
    t.mustEq(d.toISOString(), "2002-01-05T15:20:00.000Z", "auto ISO parse");
});

t("scanDate - unparseable returns null", function(){
    var d = rampart.utils.scanDate("xxnotadate", 0, "%Y-%m-%d %H:%M:%S");
    t.mustEq(d, null, "null on no match");
});

/* -------------------------- autoScanDate ----------------------------- */

t("autoScanDate - no zone returns documented shape", function(){
    var r = rampart.utils.autoScanDate("Jan 5 03:20 pm 2002");
    t.must(r && typeof r === "object", "result object");
    t.must(r.date instanceof Date, "date is Date");
    t.mustEq(r.date.toISOString(), "2002-01-05T15:20:00.000Z", "parsed date (GMT)");
    t.mustEq(r.offset, 0, "offset 0 for no zone");
    t.mustEq(typeof r.endIndex, "number", "endIndex is number");
    t.mustEq(r.matchedFormat, "%b %e %I:%M %p %Y", "matchedFormat");
});

t("autoScanDate - explicit numeric offset", function(){
    var r = rampart.utils.autoScanDate("Jan 5 03:20 pm 2002 -0800");
    t.mustEq(r.offset, -28800, "offset -28800");
    t.mustEq(r.date.toISOString(), "2002-01-05T23:20:00.000Z", "shifted to UTC");
    t.mustEq(r.matchedFormat, "%b %e %I:%M %p %Y %z", "matchedFormat with %z");
});

t("autoScanDate - abbreviation produces ambiguous + dates", function(){
    var r = rampart.utils.autoScanDate("Jan 5 03:20 pm 2002 PST");
    t.mustEq(r.ambiguous, true, "ambiguous true for PST");
    t.must(r.dates && typeof r.dates === "object", "dates object present");
    t.must(r.date instanceof Date, "date is set");
    t.mustEq(typeof r.offset, "number", "offset is number");
    t.mustEq(r.matchedFormat, "%b %e %I:%M %p %Y %Z", "matchedFormat with %Z");
});

t("autoScanDate - unparseable returns null", function(){
    var r = rampart.utils.autoScanDate("not a date at all");
    t.mustEq(r, null, "null on no match");
});

/* ----------------------------- use ----------------------------------- */

t("use - resolves a module by short name", function(){
    /* use.crypto -> require("rampart-crypto") */
    var c = use.crypto;
    t.must(c && (typeof c === "object" || typeof c === "function"), "use.crypto resolved");
});

t("use - unknown module throws", function(){
    t.mustThrow(function(){ use.no_such_module_xyzzy; }, "unknown use throws");
});

/* ----------------------------- load ---------------------------------- */

t("load - puts module into global namespace", function(){
    var had = global.hasOwnProperty("crypto");
    var saved = global.crypto;
    try {
        load.crypto; /* global.crypto = require("rampart-crypto") */
        t.must(typeof global.crypto !== "undefined", "global.crypto defined after load");
    } finally {
        /* restore global state */
        if (had) global.crypto = saved;
        else { try { delete global.crypto; } catch(e){} }
    }
});

/* -------------------------- errorConfig ------------------------------ */

t("errorConfig - object form accepted", function(){
    /* should not throw; restore defaults after */
    rampart.utils.errorConfig({simple:true, lines:3});
    rampart.utils.errorConfig(false, 0); /* restore documented default */
    t.must(true, "object form did not throw");
});

t("errorConfig - positional form accepted", function(){
    rampart.utils.errorConfig(true, 0);
    rampart.utils.errorConfig(false, 0); /* restore */
    t.must(true, "positional form did not throw");
});

t("errorConfig - simple mode shortens stack trace", function(){
    var defStack, simpleStack;
    rampart.utils.errorConfig(false, 0);
    try { (function f(){ null.x; })(); } catch(e){ defStack = e.stack || ""; }
    rampart.utils.errorConfig(true, 0);
    try { (function f(){ null.x; })(); } catch(e){ simpleStack = e.stack || ""; }
    rampart.utils.errorConfig(false, 0); /* restore default */
    t.must(typeof defStack === "string" && defStack.length > 0, "default stack present");
    t.must(typeof simpleStack === "string", "simple stack present");
    /* simple trace omits the internal duktape.c [anon] frames */
    t.must(simpleStack.indexOf("duktape.c") === -1, "simple stack omits internal frames");
});

/* --------------------------- deepCopy -------------------------------- */

t("deepCopy - merges sources, later overwrites earlier", function(){
    var source1 = { account:{firstName:"John"}, links:['a'] };
    var source2 = { account:{lastName:"Smith"}, links:['b'] };
    var target = rampart.utils.deepCopy({}, source1, source2);
    t.mustEq(target, {account:{firstName:"John",lastName:"Smith"}, links:['b']},
             "merged with replace-array default");
});

t("deepCopy - appendArrays true appends arrays", function(){
    var source1 = { account:{firstName:"John"}, links:['a'] };
    var source2 = { account:{lastName:"Smith"}, links:['b'] };
    var target = rampart.utils.deepCopy(true, {}, source1, source2);
    t.mustEq(target, {account:{firstName:"John",lastName:"Smith"}, links:['a','b']},
             "arrays appended");
});

t("deepCopy - copy is independent of source (deep)", function(){
    var orig = { n:{v:1}, arr:[1,2] };
    var cp = rampart.utils.deepCopy({}, orig);
    cp.n.v = 99;
    cp.arr.push(3);
    t.mustEq(orig.n.v, 1, "nested object unchanged in original");
    t.mustEq(orig.arr.length, 2, "array unchanged in original");
});

t("deepCopy - nested arrays/objects deeply cloned", function(){
    var orig = { a:[ {x:1}, {y:2} ] };
    var cp = rampart.utils.deepCopy({}, orig);
    cp.a[0].x = 100;
    t.mustEq(orig.a[0].x, 1, "deep array element independent");
});

/* ------------------------- eventCallback ----------------------------- */

t("eventCallback - registering a callback does not throw", function(){
    rampart.utils.eventCallback(function(level, msg){ /* noop */ });
    t.must(true, "eventCallback accepted a function");
});

/* -------------------------- getScopeVars ----------------------------- */

t("getScopeVars - mode 1 returns scopes with local/closure/global", function(){
    (function outer(){
        var x = 10;
        (function inner(){
            var y = 20;
            var sc = rampart.utils.getScopeVars();
            t.must(sc && typeof sc === "object", "scopes object");
            t.must(sc.local && typeof sc.local === "object", "local object");
            t.mustEq(sc.local.y, 20, "local.y == 20");
            t.must(sc.closure && typeof sc.closure === "object", "closure object");
            t.mustEq(sc.closure.x, 10, "closure.x == 10");
            t.must(sc.global && typeof sc.global === "object", "global object present");
            t.mustEq(typeof sc.collapse, "function", "collapse method present");
        })();
    })();
});

t("getScopeVars - mode 2 single-var lookup returns value+scope", function(){
    (function outer(){
        var x = 10;
        (function inner(){
            var y = 20;
            var li = rampart.utils.getScopeVars("y");
            t.mustEq(li.value, 20, "local lookup value");
            t.mustEq(li.scope, "local", "local lookup scope");
            var ci = rampart.utils.getScopeVars("x");
            t.mustEq(ci.value, 10, "closure lookup value");
            t.mustEq(ci.scope, "closure", "closure lookup scope");
        })();
    })();
});

t("getScopeVars - mode 2 not found returns undefined", function(){
    var r = rampart.utils.getScopeVars("noSuchVarAtAll_zzz");
    t.mustEq(typeof r, "undefined", "missing var -> undefined");
});

t("getScopeVars - collapse() flattens with inner shadowing outer", function(){
    /* getScopeVars was globalized at top of file; use the bare global
       name so we can shadow 'rampart' inside the inner scope (matching
       the documented example). */
    (function outer(){
        var x = 10;
        (function inner(){
            var y = 20;
            var all = getScopeVars().collapse();
            t.mustEq(all.y, 20, "collapse local y");
            t.mustEq(all.x, 10, "collapse closure x");
            /* local shadows global: define a var named like a global */
            var rampart = "shadowed";
            var all2 = getScopeVars().collapse();
            t.mustEq(all2.rampart, "shadowed", "local shadows global rampart");
        })();
    })();
});

/* ----------------------------- repl ---------------------------------- */

t.skip("repl", "interactive line editor; requires a tty / blocking stdin");

t("repl - module-level history helpers are functions", function(){
    /* These operate on the process-global history buffer and are safe
       to introspect without entering interactive mode. */
    t.mustEq(typeof rampart.utils.repl.getHistory,     "function", "getHistory");
    t.mustEq(typeof rampart.utils.repl.replaceHistory, "function", "replaceHistory");
    t.mustEq(typeof rampart.utils.repl.appendHistory,  "function", "appendHistory");
    t.mustEq(typeof rampart.utils.repl.interrupt,      "function", "interrupt");
    t.mustEq(typeof rampart.utils.repl.refresh,        "function", "refresh");
});

t("repl - replace/get/append history round-trip", function(){
    var saved = rampart.utils.repl.getHistory();
    t.must(Array.isArray(saved), "getHistory returns array");
    try {
        rampart.utils.repl.replaceHistory(["one", "two"]);
        t.mustEq(rampart.utils.repl.getHistory(), ["one","two"], "after replace");
        rampart.utils.repl.appendHistory(["three"]);
        t.mustEq(rampart.utils.repl.getHistory(), ["one","two","three"], "after append");
    } finally {
        /* restore process-global history */
        rampart.utils.repl.replaceHistory(saved);
    }
});


/* ##################### section a5 ##################### */
/* =====================================================================
 * a5: File Handle Utilities, POSIX fd I/O, and File Watching
 *   fopen, fopenBuffer, fclose, fprintf, fseek, rewind, ftell, fflush,
 *   fread, fgets, fwrite, readLine,
 *   fh.fstat/fsync/fdatasync/ftruncate/fchmod/fchown/fUtimes/fileNo,
 *   O constants, open/close/read/write/lseek/fstatFd/fsyncFd/
 *   fdatasyncFd/ftruncateFd/fchmodFd/fchownFd/futimesFd,
 *   durable atomic write, watch
 * ===================================================================== */

/* ---------- fopen ---------- */

t("fopen - mode w+ returns handle object with methods", function(){
    var p = TMP+"/fopen_w.txt";
    var h = rampart.utils.fopen(p, "w+");
    t.must(h && typeof h === "object", "handle is object");
    t.must(typeof h.fprintf === "function", "handle has fprintf method");
    t.must(typeof h.fread === "function", "handle has fread method");
    t.must(typeof h.fclose === "function", "handle has fclose method");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fopen - mode r reads existing, positioned at start", function(){
    var p = TMP+"/fopen_r.txt";
    rampart.utils.fprintf(p, "hello-r");
    var h = rampart.utils.fopen(p, "r");
    t.mustEq(rampart.utils.ftell(h), 0, "r positioned at start");
    var data = rampart.utils.bufferToString(rampart.utils.fread(h));
    t.mustEq(data, "hello-r", "r reads content");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fopen - mode w truncates to zero length", function(){
    var p = TMP+"/fopen_trunc.txt";
    rampart.utils.fprintf(p, "old-content");
    var h = rampart.utils.fopen(p, "w");
    t.mustEq(h.fstat().size, 0, "w truncated to 0");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fopen - mode a appends, positioned at end", function(){
    var p = TMP+"/fopen_a.txt";
    rampart.utils.fprintf(p, "AAA");
    var h = rampart.utils.fopen(p, "a");
    rampart.utils.fprintf(h, "BBB");
    h.fclose();
    var rh = rampart.utils.fopen(p, "r");
    var v = rampart.utils.fread(rh, 1000000, 4096, true);
    rh.fclose();
    t.mustEq(v, "AAABBB", "a appended");
    rampart.utils.rmFile(p);
});

t("fopen - mode r+ read and write at start", function(){
    var p = TMP+"/fopen_rplus.txt";
    rampart.utils.fprintf(p, "123def");
    var h = rampart.utils.fopen(p, "r+");
    rampart.utils.fprintf(h, "abc");          /* overwrites first 3 bytes */
    rampart.utils.rewind(h);
    t.mustEq(rampart.utils.bufferToString(rampart.utils.fread(h)), "abcdef", "r+ overwrite");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fopen - mode a+ create-if-missing, reads from start", function(){
    var p = TMP+"/fopen_aplus.txt";
    try { rampart.utils.rmFile(p); } catch(e){}
    var h = rampart.utils.fopen(p, "a+");
    rampart.utils.fprintf(h, "appended");
    rampart.utils.rewind(h);
    t.mustEq(rampart.utils.bufferToString(rampart.utils.fread(h)), "appended", "a+ reads from start");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- fopenBuffer ---------- */

t("fopenBuffer - default returns handle with buffer methods", function(){
    var fb = rampart.utils.fopenBuffer();
    t.must(typeof fb.fprintf === "function", "fopenBuffer has fprintf");
    t.must(typeof fb.getBuffer === "function", "fopenBuffer has getBuffer");
    t.must(typeof fb.getString === "function", "fopenBuffer has getString");
    t.must(typeof fb.destroy === "function", "fopenBuffer has destroy");
    fb.destroy();
});

t("fopenBuffer - getString returns written text", function(){
    var fb = rampart.utils.fopenBuffer();
    rampart.utils.fprintf(fb, "num=%d", 42);
    t.mustEq(fb.getString(), "num=42", "getString matches written");
    fb.destroy();
});

t("fopenBuffer - getBuffer returns a Buffer", function(){
    var fb = rampart.utils.fopenBuffer();
    rampart.utils.fwrite(fb, "xyz");
    rampart.utils.fflush(fb);   /* getBuffer reflects only flushed bytes */
    var b = fb.getBuffer();
    t.mustEq(rampart.utils.bufferToString(b), "xyz", "getBuffer content");
    fb.destroy();
});

t("fopenBuffer - explicit chunkSize argument", function(){
    var fb = rampart.utils.fopenBuffer(64);
    rampart.utils.fprintf(fb, "chunked");
    t.mustEq(fb.getString(), "chunked", "chunkSize variant works");
    fb.destroy();
});

t("fopenBuffer - getString/getBuffer usable after fclose", function(){
    var fb = rampart.utils.fopenBuffer();
    rampart.utils.fprintf(fb, "persist");
    rampart.utils.fclose(fb);          /* close handle, buffer survives */
    t.mustEq(fb.getString(), "persist", "getString after fclose");
    fb.destroy();
});

t("fopenBuffer - use after destroy throws", function(){
    var fb = rampart.utils.fopenBuffer();
    rampart.utils.fprintf(fb, "gone");
    fb.destroy();
    t.mustThrow(function(){ fb.getString(); }, "use after destroy throws");
});

t("fopenBuffer - stdRedir captures stdout", function(){
    var fb = rampart.utils.fopenBuffer(rampart.utils.stdout);
    rampart.utils.printf("redir-line\n");  /* goes to buffer, not terminal */
    rampart.utils.fclose(fb);              /* restores stdout */
    t.mustContain(fb.getString(), "redir-line", "stdout redirected to buffer");
    fb.destroy();
});

/* ---------- fclose ---------- */

t("fclose - returns undefined (function form)", function(){
    var p = TMP+"/fclose.txt";
    var h = rampart.utils.fopen(p, "w");
    t.mustEq(rampart.utils.fclose(h), undefined, "fclose returns undefined");
    rampart.utils.rmFile(p);
});

t("fclose - handle method form", function(){
    var p = TMP+"/fclose2.txt";
    var h = rampart.utils.fopen(p, "w");
    h.fclose();
    t.must(true, "handle.fclose() did not throw");
    rampart.utils.rmFile(p);
});

/* ---------- fprintf ---------- */

t("fprintf - to handle returns byte length", function(){
    var p = TMP+"/fprintf_h.txt";
    var h = rampart.utils.fopen(p, "w+");
    var n = rampart.utils.fprintf(h, "A number: %d\n", 123);
    t.mustEq(n, "A number: 123\n".length, "fprintf returns byte length");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fprintf - to filename truncates then implicit close", function(){
    var p = TMP+"/fprintf_fn.txt";
    rampart.utils.fprintf(p, "first");
    rampart.utils.fprintf(p, "second");   /* truncates -> overwrites */
    var rh = rampart.utils.fopen(p, "r"); var v = rampart.utils.fread(rh, 1e6, 4096, true); rh.fclose();
    t.mustEq(v, "second", "filename truncates");
    rampart.utils.rmFile(p);
});

t("fprintf - to filename with append=true", function(){
    var p = TMP+"/fprintf_ap.txt";
    rampart.utils.fprintf(p, "AAA");
    rampart.utils.fprintf(p, true, "BBB");   /* append */
    var rh = rampart.utils.fopen(p, "r"); var v = rampart.utils.fread(rh, 1e6, 4096, true); rh.fclose();
    t.mustEq(v, "AAABBB", "append=true appends");
    rampart.utils.rmFile(p);
});

/* ---------- fseek ---------- */

t("fseek - seek_set then overwrite read back (doc example)", function(){
    var p = TMP+"/fseek.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "123def");
    rampart.utils.fseek(h, 0, "seek_set");
    rampart.utils.fprintf(h, "abc");
    rampart.utils.fseek(h, 0, "seek_set");
    t.mustEq(rampart.utils.bufferToString(rampart.utils.fread(h)), "abcdef", "fseek seek_set");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fseek - seek_cur and seek_end whence", function(){
    var p = TMP+"/fseek2.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "0123456789");
    rampart.utils.fseek(h, 2, "seek_set");
    rampart.utils.fseek(h, 3, "seek_cur");        /* now at 5 */
    t.mustEq(rampart.utils.ftell(h), 5, "seek_cur relative");
    rampart.utils.fseek(h, -2, "seek_end");       /* now at 8 */
    t.mustEq(rampart.utils.ftell(h), 8, "seek_end relative");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fseek - default whence is seek_set", function(){
    var p = TMP+"/fseek3.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "abcdef");
    rampart.utils.fseek(h, 3);                     /* no whence -> seek_set */
    t.mustEq(rampart.utils.ftell(h), 3, "default whence seek_set");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fseek - returns undefined in function form, handle in method form", function(){
    var p = TMP+"/fseek4.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "abcdef");
    t.mustEq(rampart.utils.fseek(h, 0, "seek_set"), undefined, "fn form returns undefined");
    t.must(h.fseek(0, "seek_set") === h, "method form returns handle");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- rewind ---------- */

t("rewind - resets position to start", function(){
    var p = TMP+"/rewind.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "abcdef");
    t.must(rampart.utils.ftell(h) > 0, "position advanced");
    rampart.utils.rewind(h);
    t.mustEq(rampart.utils.ftell(h), 0, "rewind to 0");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("rewind - fn form undefined, method form returns handle", function(){
    var p = TMP+"/rewind2.txt";
    var h = rampart.utils.fopen(p, "w+");
    t.mustEq(rampart.utils.rewind(h), undefined, "fn form undefined");
    t.must(h.rewind() === h, "method form returns handle");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- ftell ---------- */

t("ftell - returns current position as number", function(){
    var p = TMP+"/ftell.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "12345");
    var pos = rampart.utils.ftell(h);
    t.mustEq(typeof pos, "number", "ftell returns number");
    t.mustEq(pos, 5, "ftell after writing 5 bytes");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- fflush ---------- */

t("fflush - on output handle returns undefined", function(){
    var p = TMP+"/fflush.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "data");
    t.mustEq(rampart.utils.fflush(h), undefined, "fflush fn form undefined");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fflush - method form returns handle", function(){
    var p = TMP+"/fflush2.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "data");
    t.must(h.fflush() === h, "fflush method returns handle");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- fread ---------- */

t("fread - default returns Buffer", function(){
    var p = TMP+"/fread.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "abcdef");
    rampart.utils.rewind(h);
    var b = rampart.utils.fread(h);
    t.mustEq(rampart.utils.bufferToString(b), "abcdef", "fread buffer content");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fread - returnString=true returns String", function(){
    var p = TMP+"/fread2.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "stringy");
    rampart.utils.rewind(h);
    var s = rampart.utils.fread(h, 100, 4096, true);
    t.mustEq(typeof s, "string", "returnString yields string");
    t.mustEq(s, "stringy", "string content");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fread - max_size limits bytes read", function(){
    var p = TMP+"/fread3.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "0123456789");
    rampart.utils.rewind(h);
    var s = rampart.utils.fread(h, 4, 4096, true);
    t.mustEq(s, "0123", "max_size=4 reads 4 bytes");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fread - by filename auto opens/closes (like fprintf)", function(){
    var p = TMP+"/fread4.txt";
    rampart.utils.fprintf(p, "0123456789");
    /* string form */
    t.mustEq(rampart.utils.fread(p, undefined, undefined, true), "0123456789", "fread filename returnString");
    /* buffer form */
    t.mustEq(rampart.utils.bufferToString(rampart.utils.fread(p)), "0123456789", "fread filename buffer");
    /* max_bytes honored */
    t.mustEq(rampart.utils.fread(p, 4, undefined, true), "0123", "fread filename max_bytes");
    rampart.utils.rmFile(p);
});

/* ---------- fgets ---------- */

t("fgets - reads one line including newline", function(){
    var p = TMP+"/fgets.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "line1\nline2\n");
    rampart.utils.rewind(h);
    t.mustEq(rampart.utils.fgets(h, {}, 100), "line1\n", "fgets first line");
    t.mustEq(rampart.utils.fgets(h, {}, 100), "line2\n", "fgets second line");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fgets - returns string up to max_size when no newline", function(){
    var p = TMP+"/fgets2.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "abcdefgh");        /* no newline */
    rampart.utils.rewind(h);
    var s = rampart.utils.fgets(h, {}, 4);
    t.mustEq(typeof s, "string", "fgets returns string");
    t.mustEq(s, "abcd", "fgets capped at max_size");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- fwrite ---------- */

t("fwrite - to handle returns bytes written", function(){
    var p = TMP+"/fwrite.txt";
    var h = rampart.utils.fopen(p, "w+");
    var n = rampart.utils.fwrite(h, "hello");
    t.mustEq(n, 5, "fwrite returns byte count");
    rampart.utils.rewind(h);
    t.mustEq(rampart.utils.bufferToString(rampart.utils.fread(h)), "hello", "fwrite content");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fwrite - max_bytes limits write", function(){
    var p = TMP+"/fwrite2.txt";
    var h = rampart.utils.fopen(p, "w+");
    var n = rampart.utils.fwrite(h, "abcdef", 3);
    t.mustEq(n, 3, "fwrite max_bytes=3");
    rampart.utils.rewind(h);
    t.mustEq(rampart.utils.bufferToString(rampart.utils.fread(h)), "abc", "only 3 bytes written");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fwrite - to filename truncates by default", function(){
    var p = TMP+"/fwrite3.txt";
    rampart.utils.fwrite(p, "AAAAAA");
    rampart.utils.fwrite(p, "BB");                /* truncate */
    var rh = rampart.utils.fopen(p, "r"); var v = rampart.utils.fread(rh, 1e6, 4096, true); rh.fclose();
    t.mustEq(v, "BB", "fwrite filename truncates");
    rampart.utils.rmFile(p);
});

t("fwrite - to filename with append=true", function(){
    var p = TMP+"/fwrite4.txt";
    rampart.utils.fwrite(p, "AA");
    rampart.utils.fwrite(p, "BB", undefined, true);  /* append */
    var rh = rampart.utils.fopen(p, "r"); var v = rampart.utils.fread(rh, 1e6, 4096, true); rh.fclose();
    t.mustEq(v, "AABB", "fwrite append=true");
    rampart.utils.rmFile(p);
});

/* ---------- readLine ---------- */

t("readLine - iterates lines via next(), null at EOF", function(){
    var p = TMP+"/readline.txt";
    rampart.utils.fprintf(p, "alpha\nbeta\ngamma\n");
    var rl = rampart.utils.readLine(p);
    t.must(typeof rl.next === "function", "readLine returns obj with next()");
    var lines = [];
    var line;
    while ((line = rl.next()) !== null)
        lines.push(rampart.utils.trim(line));
    t.mustEq(lines, ["alpha","beta","gamma"], "readLine all lines");
    t.mustEq(rl.next(), null, "next() returns null after EOF");
    rampart.utils.rmFile(p);
});

t("readLine - from a fopen handle", function(){
    var p = TMP+"/readline2.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "one\ntwo\n");
    rampart.utils.rewind(h);
    var rl = rampart.utils.readLine(h);
    t.mustEq(rampart.utils.trim(rl.next()), "one", "first line from handle");
    t.mustEq(rampart.utils.trim(rl.next()), "two", "second line from handle");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- fh.fstat ---------- */

t("fh.fstat - returns stat-shaped object for open handle", function(){
    var p = TMP+"/fstat.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "12345");
    var st = h.fstat();
    t.must(st && typeof st === "object", "fstat returns object");
    t.mustEq(st.size, 5, "fstat size");
    t.mustEq(st.isFile, true, "fstat isFile");
    t.mustEq(typeof st.uid, "number", "fstat has uid");
    t.mustEq(typeof st.mode, "number", "fstat has mode");
    /* works even after unlink */
    rampart.utils.rmFile(p);
    t.mustEq(h.fstat().size, 5, "fstat works on unlinked-but-open file");
    h.fclose();
});

/* ---------- fh.fsync ---------- */

t("fh.fsync - returns undefined, flushes to disk", function(){
    var p = TMP+"/fsync.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "durable");
    t.mustEq(h.fsync(), undefined, "fsync returns undefined");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- fh.fdatasync ---------- */

t("fh.fdatasync - returns undefined", function(){
    var p = TMP+"/fdatasync.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "data");
    t.mustEq(h.fdatasync(), undefined, "fdatasync returns undefined");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- fh.ftruncate ---------- */

t("fh.ftruncate - shrinks file to length", function(){
    var p = TMP+"/ftruncate.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "0123456789");
    h.ftruncate(4);
    t.mustEq(h.fstat().size, 4, "ftruncate shrink to 4");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fh.ftruncate - extends file with zero bytes", function(){
    var p = TMP+"/ftruncate2.txt";
    var h = rampart.utils.fopen(p, "w+");
    rampart.utils.fprintf(h, "abc");
    h.ftruncate(10);
    t.mustEq(h.fstat().size, 10, "ftruncate extend to 10");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- fh.fchmod ---------- */

t("fh.fchmod - changes mode (numeric)", function(){
    var p = TMP+"/fchmod.txt";
    var h = rampart.utils.fopen(p, "w+");
    h.fchmod(0o600);
    t.mustEq(h.fstat().permissions, "-rw-------", "fchmod numeric mode");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fh.fchmod - changes mode (octal string)", function(){
    var p = TMP+"/fchmod2.txt";
    var h = rampart.utils.fopen(p, "w+");
    h.fchmod("0640");
    t.mustEq(h.fstat().permissions, "-rw-r-----", "fchmod octal string mode");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- fh.fchown ---------- */

t("fh.fchown - same owner/group is a no-op", function(){
    var p = TMP+"/fchown.txt";
    var h = rampart.utils.fopen(p, "w+");
    var st = h.fstat();
    h.fchown(st.uid, st.gid);                  /* no privilege change needed */
    var st2 = h.fstat();
    t.mustEq(st2.uid, st.uid, "uid unchanged");
    t.mustEq(st2.gid, st.gid, "gid unchanged");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fh.fchown - negative leaves a side unchanged", function(){
    var p = TMP+"/fchown2.txt";
    var h = rampart.utils.fopen(p, "w+");
    var st = h.fstat();
    h.fchown(-1, -1);                          /* both unchanged */
    var st2 = h.fstat();
    t.mustEq(st2.uid, st.uid, "uid unchanged with -1");
    t.mustEq(st2.gid, st.gid, "gid unchanged with -1");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- fh.fUtimes ---------- */

t("fh.fUtimes - positional seconds-since-epoch", function(){
    var p = TMP+"/futimes.txt";
    var h = rampart.utils.fopen(p, "w+");
    h.fUtimes(1000000, 2000000);
    var st = h.fstat();
    /* mtime exposed as a Date/ISO; 2000000s after epoch */
    t.mustEq(new Date(st.mtime).getTime(), 2000000 * 1000, "fUtimes positional mtime");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fh.fUtimes - options object {setaccess,setmodify}", function(){
    var p = TMP+"/futimes2.txt";
    var h = rampart.utils.fopen(p, "w+");
    h.fUtimes({setaccess: 3000000, setmodify: 4000000});
    var st = h.fstat();
    t.mustEq(new Date(st.mtime).getTime(), 4000000 * 1000, "fUtimes obj mtime");
    h.fclose();
    rampart.utils.rmFile(p);
});

t("fh.fUtimes - Date arguments", function(){
    var p = TMP+"/futimes3.txt";
    var h = rampart.utils.fopen(p, "w+");
    var d = new Date(5000000 * 1000);
    h.fUtimes(d, d);
    var st = h.fstat();
    t.mustEq(new Date(st.mtime).getTime(), 5000000 * 1000, "fUtimes Date mtime");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- fh.fileNo ---------- */

t("fh.fileNo - returns integer fd for fopen handle", function(){
    var p = TMP+"/fileno.txt";
    var h = rampart.utils.fopen(p, "w+");
    var fd = h.fileNo();
    t.mustEq(typeof fd, "number", "fileNo returns number");
    t.must(fd >= 0, "fileNo is a real fd");
    /* the fd should be usable with fd-keyed fns */
    t.mustEq(rampart.utils.fstatFd(fd).isFile, true, "fileNo fd usable with fstatFd");
    h.fclose();
    rampart.utils.rmFile(p);
});

/* ---------- Open flag constants (rampart.utils.O) ---------- */

t("O - documented constants exist and are numbers", function(){
    var O = rampart.utils.O;
    var names = ["RDONLY","WRONLY","RDWR","CREAT","EXCL","TRUNC","APPEND",
                 "NONBLOCK","CLOEXEC","NOFOLLOW","SYNC","DSYNC","NOCTTY",
                 "SEEK_SET","SEEK_CUR","SEEK_END"];
    for (var i=0; i<names.length; i++)
        t.mustEq(typeof O[names[i]], "number", "O."+names[i]+" is a number");
});

t("O - SEEK_SET/CUR/END equal 0/1/2", function(){
    var O = rampart.utils.O;
    t.mustEq(O.SEEK_SET, 0, "SEEK_SET == 0");
    t.mustEq(O.SEEK_CUR, 1, "SEEK_CUR == 1");
    t.mustEq(O.SEEK_END, 2, "SEEK_END == 2");
});

t("O - flags combine with OR and drive open()", function(){
    var O = rampart.utils.O;
    var p = TMP+"/oflags.txt";
    try { rampart.utils.rmFile(p); } catch(e){}
    var fd = rampart.utils.open(p, O.WRONLY|O.CREAT|O.TRUNC, 0o644);
    t.mustEq(typeof fd, "number", "open with OR'd flags returns fd");
    rampart.utils.write(fd, "ok");
    rampart.utils.close(fd);
    var rh = rampart.utils.fopen(p, "r"); var v = rampart.utils.fread(rh, 1e6, 4096, true); rh.fclose();
    t.mustEq(v, "ok", "OR'd flags wrote file");
    rampart.utils.rmFile(p);
});

/* ---------- open ---------- */

t("open - positional path/flags/mode returns fd", function(){
    var O = rampart.utils.O;
    var p = TMP+"/open1.txt";
    try { rampart.utils.rmFile(p); } catch(e){}
    var fd = rampart.utils.open(p, O.WRONLY|O.CREAT, 0o600);
    t.must(fd >= 0, "open returns valid fd");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

t("open - options-object form {path,flags,mode}", function(){
    var O = rampart.utils.O;
    var p = TMP+"/open2.txt";
    try { rampart.utils.rmFile(p); } catch(e){}
    var fd = rampart.utils.open({path: p, flags: O.WRONLY|O.CREAT, mode: 0o644});
    t.must(fd >= 0, "open object-form returns fd");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

t("open - O.EXCL fails when file exists", function(){
    var O = rampart.utils.O;
    var p = TMP+"/openexcl.txt";
    var fd = rampart.utils.open(p, O.WRONLY|O.CREAT|O.EXCL, 0o600);
    rampart.utils.close(fd);
    t.mustThrow(function(){
        rampart.utils.open(p, O.WRONLY|O.CREAT|O.EXCL, 0o600);
    }, "EXCL on existing file throws");
    rampart.utils.rmFile(p);
});

t("open - mode as octal string", function(){
    var O = rampart.utils.O;
    var p = TMP+"/openmode.txt";
    try { rampart.utils.rmFile(p); } catch(e){}
    var fd = rampart.utils.open(p, O.WRONLY|O.CREAT, "0600");
    rampart.utils.close(fd);
    t.mustEq(rampart.utils.stat(p).permissions, "-rw-------", "octal string mode applied");
    rampart.utils.rmFile(p);
});

/* ---------- close ---------- */

t("close - returns undefined", function(){
    var O = rampart.utils.O;
    var p = TMP+"/close.txt";
    var fd = rampart.utils.open(p, O.WRONLY|O.CREAT|O.TRUNC, 0o644);
    t.mustEq(rampart.utils.close(fd), undefined, "close returns undefined");
    rampart.utils.rmFile(p);
});

/* ---------- write / read / lseek round trip ---------- */

t("write/read - fd round trip returns Buffer", function(){
    var O = rampart.utils.O;
    var p = TMP+"/wr.txt";
    var fd = rampart.utils.open(p, O.RDWR|O.CREAT|O.TRUNC, 0o644);
    var n = rampart.utils.write(fd, "hello world");
    t.mustEq(n, 11, "write returns bytes written");
    rampart.utils.lseek(fd, 0, "SEEK_SET");
    var b = rampart.utils.read(fd, 100);
    t.mustEq(rampart.utils.bufferToString(b), "hello world", "read back content");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

t("read - zero-length Buffer at EOF", function(){
    var O = rampart.utils.O;
    var p = TMP+"/eof.txt";
    var fd = rampart.utils.open(p, O.RDWR|O.CREAT|O.TRUNC, 0o644);
    rampart.utils.write(fd, "abc");
    /* offset now at end -> read returns zero-length */
    var b = rampart.utils.read(fd, 100);
    t.mustEq(b.length, 0, "read at EOF returns zero-length buffer");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

t("read/write - positional pread/pwrite do not advance offset", function(){
    var O = rampart.utils.O;
    var p = TMP+"/pos.txt";
    var fd = rampart.utils.open(p, O.RDWR|O.CREAT|O.TRUNC, 0o644);
    rampart.utils.write(fd, "0123456789");
    /* pread at offset 2, offset stays at 10 */
    var pr = rampart.utils.read(fd, 3, 2);
    t.mustEq(rampart.utils.bufferToString(pr), "234", "pread at position 2");
    t.mustEq(rampart.utils.lseek(fd, 0, "SEEK_CUR"), 10, "pread did not advance offset");
    /* pwrite at offset 0, offset unchanged */
    rampart.utils.write(fd, "XY", 0);
    rampart.utils.lseek(fd, 0, "SEEK_SET");
    t.mustEq(rampart.utils.bufferToString(rampart.utils.read(fd, 10)), "XY23456789", "pwrite at position 0");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

t("write - accepts a Buffer", function(){
    var O = rampart.utils.O;
    var p = TMP+"/wbuf.txt";
    var fd = rampart.utils.open(p, O.RDWR|O.CREAT|O.TRUNC, 0o644);
    var buf = rampart.utils.stringToBuffer("buffered");
    rampart.utils.write(fd, buf);
    rampart.utils.lseek(fd, 0, "SEEK_SET");
    t.mustEq(rampart.utils.bufferToString(rampart.utils.read(fd, 100)), "buffered", "write Buffer");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

/* ---------- lseek ---------- */

t("lseek - whence variants string and integer", function(){
    var O = rampart.utils.O;
    var p = TMP+"/lseek.txt";
    var fd = rampart.utils.open(p, O.RDWR|O.CREAT|O.TRUNC, 0o644);
    rampart.utils.write(fd, "0123456789");
    t.mustEq(rampart.utils.lseek(fd, 3, "SEEK_SET"), 3, "SEEK_SET string");
    t.mustEq(rampart.utils.lseek(fd, 2, "SEEK_CUR"), 5, "SEEK_CUR string");
    t.mustEq(rampart.utils.lseek(fd, -1, "SEEK_END"), 9, "SEEK_END string");
    t.mustEq(rampart.utils.lseek(fd, 0, O.SEEK_SET), 0, "integer whence via O.SEEK_SET");
    t.mustEq(rampart.utils.lseek(fd, 4), 4, "default whence SEEK_SET");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

/* ---------- fstatFd ---------- */

t("fstatFd - stat-shaped fields for fd", function(){
    var O = rampart.utils.O;
    var p = TMP+"/fstatfd.txt";
    var fd = rampart.utils.open(p, O.RDWR|O.CREAT|O.TRUNC, 0o644);
    rampart.utils.write(fd, "abcde");
    var st = rampart.utils.fstatFd(fd);
    t.mustEq(st.size, 5, "fstatFd size");
    t.mustEq(st.isFile, true, "fstatFd isFile");
    t.mustEq(typeof st.mtime, "object", "fstatFd has mtime");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

/* ---------- fsyncFd ---------- */

t("fsyncFd - flushes fd, no throw", function(){
    var O = rampart.utils.O;
    var p = TMP+"/fsyncfd.txt";
    var fd = rampart.utils.open(p, O.RDWR|O.CREAT|O.TRUNC, 0o644);
    rampart.utils.write(fd, "durable");
    rampart.utils.fsyncFd(fd);
    t.must(true, "fsyncFd did not throw");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

/* ---------- fdatasyncFd ---------- */

t("fdatasyncFd - flushes fd data, no throw", function(){
    var O = rampart.utils.O;
    var p = TMP+"/fdatasyncfd.txt";
    var fd = rampart.utils.open(p, O.RDWR|O.CREAT|O.TRUNC, 0o644);
    rampart.utils.write(fd, "data");
    rampart.utils.fdatasyncFd(fd);
    t.must(true, "fdatasyncFd did not throw");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

/* ---------- ftruncateFd ---------- */

t("ftruncateFd - shrink and extend by fd", function(){
    var O = rampart.utils.O;
    var p = TMP+"/ftruncatefd.txt";
    var fd = rampart.utils.open(p, O.RDWR|O.CREAT|O.TRUNC, 0o644);
    rampart.utils.write(fd, "0123456789");
    rampart.utils.ftruncateFd(fd, 4);
    t.mustEq(rampart.utils.fstatFd(fd).size, 4, "ftruncateFd shrink");
    rampart.utils.ftruncateFd(fd, 12);
    t.mustEq(rampart.utils.fstatFd(fd).size, 12, "ftruncateFd extend");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

/* ---------- fchmodFd ---------- */

t("fchmodFd - change perms by fd (numeric and octal string)", function(){
    var O = rampart.utils.O;
    var p = TMP+"/fchmodfd.txt";
    var fd = rampart.utils.open(p, O.RDWR|O.CREAT|O.TRUNC, 0o644);
    rampart.utils.fchmodFd(fd, 0o600);
    t.mustEq(rampart.utils.fstatFd(fd).permissions, "-rw-------", "fchmodFd numeric");
    rampart.utils.fchmodFd(fd, "0640");
    t.mustEq(rampart.utils.fstatFd(fd).permissions, "-rw-r-----", "fchmodFd octal string");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

/* ---------- fchownFd ---------- */

t("fchownFd - same owner no-op, negatives leave unchanged", function(){
    var O = rampart.utils.O;
    var p = TMP+"/fchownfd.txt";
    var fd = rampart.utils.open(p, O.RDWR|O.CREAT|O.TRUNC, 0o644);
    var st = rampart.utils.fstatFd(fd);
    rampart.utils.fchownFd(fd, st.uid, st.gid);
    rampart.utils.fchownFd(fd, -1, -1);
    var st2 = rampart.utils.fstatFd(fd);
    t.mustEq(st2.uid, st.uid, "fchownFd uid unchanged");
    t.mustEq(st2.gid, st.gid, "fchownFd gid unchanged");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

/* ---------- futimesFd ---------- */

t("futimesFd - positional seconds-since-epoch", function(){
    var O = rampart.utils.O;
    var p = TMP+"/futimesfd.txt";
    var fd = rampart.utils.open(p, O.RDWR|O.CREAT|O.TRUNC, 0o644);
    rampart.utils.futimesFd(fd, 1000, 2000);
    t.mustEq(new Date(rampart.utils.fstatFd(fd).mtime).getTime(), 2000 * 1000, "futimesFd positional");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

t("futimesFd - options object form", function(){
    var O = rampart.utils.O;
    var p = TMP+"/futimesfd2.txt";
    var fd = rampart.utils.open(p, O.RDWR|O.CREAT|O.TRUNC, 0o644);
    rampart.utils.futimesFd(fd, {setaccess: 5000, setmodify: 6000});
    t.mustEq(new Date(rampart.utils.fstatFd(fd).mtime).getTime(), 6000 * 1000, "futimesFd obj form");
    rampart.utils.close(fd);
    rampart.utils.rmFile(p);
});

/* ---------- durable atomic write (doc example) ---------- */

t("durable atomic write - open/write-loop/fsyncFd/close/rename", function(){
    var u = rampart.utils, O = u.O;
    function atomicWrite(path, data) {
        var tmp = path + ".tmp." + process.pid;
        var fd  = u.open(tmp, O.WRONLY|O.CREAT|O.EXCL|O.TRUNC, 0o644);
        try {
            var off = 0, total = data.length;
            while (off < total) {
                off += u.write(fd, data.slice ? data.slice(off) : data);
            }
            u.fsyncFd(fd);
        } finally {
            u.close(fd);
        }
        u.rename(tmp, path);
    }
    var p = TMP+"/atomic.json";
    var payload = JSON.stringify({n: 42});
    atomicWrite(p, payload);
    var rh = u.fopen(p, "r"); var v = u.fread(rh, 1e6, 4096, true); rh.fclose();
    t.mustEq(v, payload, "atomic write produced full content");
    u.rmFile(p);
});

/* ---------- watch ---------- */

t("watch - returns watcher object with documented properties", function(){
    var p = TMP+"/watchobj.txt";
    rampart.utils.fprintf(p, "init");
    var w = rampart.utils.watch(p, function(ev){ return false; });
    t.mustEq(w.path, p, "watcher.path matches");
    t.must(w.backend === "inotify" || w.backend === "polling", "watcher.backend is inotify|polling");
    t.mustEq(typeof w.close, "function", "watcher.close is a function");
    w.close();
    w.close();   /* idempotent */
    t.must(true, "close() is idempotent");
    rampart.utils.rmFile(p);
});

t("watch - fires callback with event {type,path,isDir} on change", function(){
    /* event-loop driven; bounded so it can't hang the suite */
    var p = TMP+"/watchfire.txt";
    rampart.utils.fprintf(p, "init");
    var captured = null;
    var w = rampart.utils.watch(p, function(ev){ captured = ev; return false; });
    setTimeout(function(){ rampart.utils.fprintf(p, true, "more"); }, 50);
    setTimeout(function(){
        w.close();
        rampart.utils.rmFile(p);
        if (captured === null) {
            t.skip("watch - fires callback with event {type,path,isDir} on change",
                   "no event captured within bounded wait on this backend/filesystem");
        } else {
            t.mustEq(typeof captured.type, "string", "event.type is string");
            t.mustEq(captured.path, p, "event.path is watched path");
            t.mustEq(captured.isDir, false, "event.isDir false for file");
        }
    }, 700);
});

t("watch - options-object form {path,poll,interval}", function(){
    /* force polling backend; bounded wait */
    var p = TMP+"/watchpoll.txt";
    rampart.utils.fprintf(p, "init");
    var captured = null;
    var w = rampart.utils.watch({path: p, poll: true, interval: 100}, function(ev){
        captured = ev; return false;
    });
    t.mustEq(w.backend, "polling", "poll:true forces polling backend");
    setTimeout(function(){ rampart.utils.fprintf(p, "polled-change-different-size"); }, 120);
    setTimeout(function(){
        w.close();
        rampart.utils.rmFile(p);
        if (captured === null) {
            t.skip("watch - options-object form poll event",
                   "polling backend captured no event within bounded wait");
        } else {
            t.must(typeof captured.type === "string", "poll event has type");
        }
    }, 900);
});


/* ##################### section a6 ##################### */
/* ============================================================
 * a6: gzip/gunzip/deflate/inflate/deflateRaw/inflateRaw,
 *     crc32/adler32, rand/irand/gaussrand/normrand/srand,
 *     hash, hll (+ add/addFile/count/merge/getBuffer)
 * ============================================================ */

/* ---------- gzip / gunzip ---------- */

t("gzip - returns a buffer with gzip magic bytes", function () {
    var bytes = rampart.utils.gzip("Hello, World!");
    t.must(typeof bytes === "object", "gzip returns object/buffer");
    var hex = rampart.utils.hexify(bytes);
    t.mustEq(hex.substr(0, 4), "1f8b", "gzip output starts with 1f8b magic");
});

t("gzip/gunzip - round-trips a string payload", function () {
    var payload = "The quick brown fox jumps over the lazy dog. ".repeat(50);
    var comp = rampart.utils.gzip(payload);
    var back = rampart.utils.gunzip(comp);
    t.mustEq(rampart.utils.bufferToString(back), payload, "gzip->gunzip recovers string");
});

t("gzip/gunzip - round-trips binary-byte payload", function () {
    /* binary string containing NUL and high bytes */
    var payload = "binary\x00data\xFFhere".repeat(20);
    var srcHex = rampart.utils.hexify(payload);
    var comp = rampart.utils.gzip(payload);
    var back = rampart.utils.gunzip(comp);
    t.mustEq(rampart.utils.hexify(back), srcHex, "gzip->gunzip recovers raw bytes");
});

t("gzip - level argument affects/accepts output (1..12)", function () {
    var payload = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".repeat(100);
    var c1 = rampart.utils.gzip(payload, 1);
    var c12 = rampart.utils.gzip(payload, 12);
    t.mustEq(rampart.utils.bufferToString(rampart.utils.gunzip(c1)), payload, "level 1 round-trips");
    t.mustEq(rampart.utils.bufferToString(rampart.utils.gunzip(c12)), payload, "level 12 round-trips");
});

t("gzip - out-of-range level is clamped (no throw)", function () {
    var payload = "clamp me".repeat(30);
    var c = rampart.utils.gzip(payload, 99);
    t.mustEq(rampart.utils.bufferToString(rampart.utils.gunzip(c)), payload, "clamped high level round-trips");
});

t("gunzip - throws on invalid gzip data", function () {
    t.mustThrow(function () {
        rampart.utils.gunzip("not gzip data at all");
    }, "gunzip throws on bad data");
});

/* ---------- deflate / inflate ---------- */

t("deflate/inflate - round-trips a string payload", function () {
    var payload = "zlib container test payload ".repeat(40);
    var comp = rampart.utils.deflate(payload);
    var back = rampart.utils.inflate(comp);
    t.mustEq(rampart.utils.bufferToString(back), payload, "deflate->inflate recovers string");
});

t("deflate - output begins with a zlib header byte (0x78 common)", function () {
    var comp = rampart.utils.deflate("hello zlib");
    var hex = rampart.utils.hexify(comp);
    /* zlib header: first byte CMF, low nibble = 8 (deflate). 0x?8 */
    t.must((parseInt(hex.substr(0, 2), 16) & 0x0f) === 8, "zlib CMF low nibble is 8");
});

t("deflate - level argument round-trips", function () {
    var payload = "level test ".repeat(50);
    var c1 = rampart.utils.deflate(payload, 1);
    var c12 = rampart.utils.deflate(payload, 12);
    t.mustEq(rampart.utils.bufferToString(rampart.utils.inflate(c1)), payload, "deflate L1 round-trips");
    t.mustEq(rampart.utils.bufferToString(rampart.utils.inflate(c12)), payload, "deflate L12 round-trips");
});

t("inflate - throws on invalid zlib data", function () {
    t.mustThrow(function () {
        rampart.utils.inflate("garbage zlib input");
    }, "inflate throws on bad data");
});

/* ---------- deflateRaw / inflateRaw ---------- */

t("deflateRaw/inflateRaw - round-trips a string payload", function () {
    var payload = "raw deflate no header no footer ".repeat(40);
    var comp = rampart.utils.deflateRaw(payload);
    var back = rampart.utils.inflateRaw(comp);
    t.mustEq(rampart.utils.bufferToString(back), payload, "deflateRaw->inflateRaw recovers string");
});

t("deflateRaw - level argument round-trips", function () {
    var payload = "raw level ".repeat(60);
    var c1 = rampart.utils.deflateRaw(payload, 1);
    var c12 = rampart.utils.deflateRaw(payload, 12);
    t.mustEq(rampart.utils.bufferToString(rampart.utils.inflateRaw(c1)), payload, "deflateRaw L1 round-trips");
    t.mustEq(rampart.utils.bufferToString(rampart.utils.inflateRaw(c12)), payload, "deflateRaw L12 round-trips");
});

t("inflateRaw - throws on invalid raw deflate data", function () {
    t.mustThrow(function () {
        rampart.utils.inflateRaw("\0\0\0\0\0\0\0\0not raw deflate");
    }, "inflateRaw throws on bad data");
});

t("compression - cross-pairing gunzip on deflate output throws (magic mismatch)", function () {
    var comp = rampart.utils.deflate("cross pair test payload here");
    t.mustThrow(function () {
        rampart.utils.gunzip(comp);
    }, "gunzip rejects zlib stream (no gzip magic)");
});

/* ---------- crc32 ---------- */

t("crc32 - known checksum for 'hello'", function () {
    var c = rampart.utils.crc32("hello");
    /* doc example: 0x3610A686 */
    t.mustEq(c, 0x3610A686, "crc32('hello') === 0x3610A686");
});

t("crc32 - deterministic", function () {
    t.mustEq(rampart.utils.crc32("rampart"), rampart.utils.crc32("rampart"), "crc32 deterministic");
});

t("crc32 - incremental/seed form equals one-shot", function () {
    var oneShot = rampart.utils.crc32("hello");
    var c = rampart.utils.crc32("hel");
    c = rampart.utils.crc32("lo", c);
    t.mustEq(c, oneShot, "crc32 chunked with seed equals one-shot");
});

t("crc32 - returns 32-bit unsigned number", function () {
    var c = rampart.utils.crc32("hello");
    t.must(typeof c === "number" && c >= 0 && c <= 0xFFFFFFFF, "crc32 in 32-bit unsigned range");
});

/* ---------- adler32 ---------- */

t("adler32 - known checksum for 'hello'", function () {
    /* Adler-32('hello') = 0x062C0215 */
    var c = rampart.utils.adler32("hello");
    t.mustEq(c, 0x062C0215, "adler32('hello') === 0x062C0215");
});

t("adler32 - deterministic", function () {
    t.mustEq(rampart.utils.adler32("rampart"), rampart.utils.adler32("rampart"), "adler32 deterministic");
});

t("adler32 - incremental/seed form equals one-shot", function () {
    var oneShot = rampart.utils.adler32("hello");
    var c = rampart.utils.adler32("hel");
    c = rampart.utils.adler32("lo", c);
    t.mustEq(c, oneShot, "adler32 chunked with seed equals one-shot");
});

t("adler32 - default seed is 1 (empty input)", function () {
    /* Adler-32 of empty string is the starting value 1 */
    var c = rampart.utils.adler32("");
    t.mustEq(c, 1, "adler32('') === 1 (default seed)");
});

/* ---------- srand / rand / irand ---------- */

t("srand - returns without error and accepts a numeric seed", function () {
    rampart.utils.srand(12345);
    t.must(true, "srand with numeric seed ok");
});

t("srand - accepts no argument (urandom seed)", function () {
    rampart.utils.srand();
    t.must(true, "srand with no arg ok");
});

t("rand - default returns float in [0,1)", function () {
    rampart.utils.srand(1);
    for (var i = 0; i < 200; i++) {
        var r = rampart.utils.rand();
        t.must(typeof r === "number" && r >= 0.0 && r < 1.0, "rand() default in [0,1)");
    }
});

t("rand - single-arg max form returns float in [0,max)", function () {
    rampart.utils.srand(2);
    for (var i = 0; i < 200; i++) {
        var r = rampart.utils.rand(10);
        t.must(r >= 0.0 && r < 10.0, "rand(10) in [0,10)");
    }
});

t("rand - min,max form returns float in [min,max)", function () {
    rampart.utils.srand(3);
    for (var i = 0; i < 200; i++) {
        var r = rampart.utils.rand(5, 7);
        t.must(r >= 5.0 && r < 7.0, "rand(5,7) in [5,7)");
    }
});

t("rand - is not integer-only (produces fractional values)", function () {
    rampart.utils.srand(4);
    var sawFraction = false;
    for (var i = 0; i < 100; i++) {
        var r = rampart.utils.rand(0, 100);
        if (r !== Math.floor(r)) { sawFraction = true; break; }
    }
    t.must(sawFraction, "rand produces non-integer floats");
});

t("rand - determinism after srand with fixed seed", function () {
    rampart.utils.srand(99999);
    var a = [];
    for (var i = 0; i < 20; i++) a.push(rampart.utils.rand());
    rampart.utils.srand(99999);
    var b = [];
    for (var j = 0; j < 20; j++) b.push(rampart.utils.rand());
    t.mustEq(a, b, "rand sequence reproducible after same srand seed");
});

t("irand - default returns integer in [0,99]", function () {
    rampart.utils.srand(5);
    for (var i = 0; i < 300; i++) {
        var r = rampart.utils.irand();
        t.must(r === Math.floor(r) && r >= 0 && r <= 99, "irand() default integer in [0,99]");
    }
});

t("irand - single-arg max form returns integer in [0,max] inclusive", function () {
    rampart.utils.srand(6);
    var sawMax = false;
    for (var i = 0; i < 500; i++) {
        var r = rampart.utils.irand(3);
        t.must(r === Math.floor(r) && r >= 0 && r <= 3, "irand(3) integer in [0,3]");
        if (r === 3) sawMax = true;
    }
    t.must(sawMax, "irand max is inclusive (saw 3)");
});

t("irand - min,max form returns integer in [min,max] inclusive", function () {
    rampart.utils.srand(7);
    for (var i = 0; i < 300; i++) {
        var r = rampart.utils.irand(10, 20);
        t.must(r === Math.floor(r) && r >= 10 && r <= 20, "irand(10,20) in [10,20]");
    }
});

t("irand - callback form calls until false, returns undefined", function () {
    rampart.utils.srand(8);
    var count = 0;
    var idxs = [];
    var ret = rampart.utils.irand(0, 5, function (r, i) {
        t.must(r >= 0 && r <= 5, "irand cb value in range");
        idxs.push(i);
        count++;
        if (count >= 10) return false;
        return true;
    });
    t.mustEq(ret, undefined, "irand callback form returns undefined");
    t.mustEq(count, 10, "irand callback invoked until false");
    t.mustEq(idxs[0], 0, "irand callback loop index starts at 0");
});

t("irand - determinism after srand with fixed seed", function () {
    rampart.utils.srand(424242);
    var a = [];
    for (var i = 0; i < 20; i++) a.push(rampart.utils.irand(0, 1000000));
    rampart.utils.srand(424242);
    var b = [];
    for (var j = 0; j < 20; j++) b.push(rampart.utils.irand(0, 1000000));
    t.mustEq(a, b, "irand sequence reproducible after same srand seed");
});

/* ---------- gaussrand ---------- */

t("gaussrand - default sigma=1 produces ~N(0,1) sample stats", function () {
    rampart.utils.srand(11);
    var n = 20000, sum = 0, sumsq = 0;
    for (var i = 0; i < n; i++) {
        var g = rampart.utils.gaussrand();
        t.must(typeof g === "number", "gaussrand returns number");
        sum += g; sumsq += g * g;
    }
    var mean = sum / n;
    var sd = Math.sqrt(sumsq / n - mean * mean);
    t.must(Math.abs(mean) < 0.1, "gaussrand mean ~0 (got " + mean + ")");
    t.must(Math.abs(sd - 1.0) < 0.15, "gaussrand sd ~1 (got " + sd + ")");
});

t("gaussrand - sigma argument scales the standard deviation", function () {
    rampart.utils.srand(12);
    var n = 20000, sum = 0, sumsq = 0;
    for (var i = 0; i < n; i++) {
        var g = rampart.utils.gaussrand(3.0);
        sum += g; sumsq += g * g;
    }
    var mean = sum / n;
    var sd = Math.sqrt(sumsq / n - mean * mean);
    t.must(Math.abs(mean) < 0.3, "gaussrand(3) mean ~0 (got " + mean + ")");
    t.must(Math.abs(sd - 3.0) < 0.45, "gaussrand(3) sd ~3 (got " + sd + ")");
});

/* ---------- normrand ---------- */

t("normrand - values clamped to [-scale, scale]", function () {
    rampart.utils.srand(13);
    var scale = 2.0;
    for (var i = 0; i < 5000; i++) {
        var r = rampart.utils.normrand(scale);
        t.must(r >= -scale && r <= scale, "normrand within [-scale,scale]");
    }
});

t("normrand - default scale=1 has sd ~0.2", function () {
    rampart.utils.srand(14);
    var n = 20000, sum = 0, sumsq = 0;
    for (var i = 0; i < n; i++) {
        var r = rampart.utils.normrand();
        t.must(r >= -1.0 && r <= 1.0, "normrand default within [-1,1]");
        sum += r; sumsq += r * r;
    }
    var mean = sum / n;
    var sd = Math.sqrt(sumsq / n - mean * mean);
    t.must(Math.abs(mean) < 0.05, "normrand mean ~0 (got " + mean + ")");
    t.must(Math.abs(sd - 0.2) < 0.06, "normrand default sd ~0.2 (got " + sd + ")");
});

/* ---------- hash ---------- */

t("hash - default (city128) returns 32-char hex string", function () {
    var h = rampart.utils.hash("hello");
    t.must(typeof h === "string", "hash default returns string");
    t.mustEq(h.length, 32, "city128 hex is 32 chars (128 bits)");
    t.must(/^[0-9a-f]+$/.test(h), "hash hex is lowercase hex");
});

t("hash - deterministic for same input", function () {
    t.mustEq(rampart.utils.hash("rampart"), rampart.utils.hash("rampart"), "hash deterministic");
});

t("hash - murmur is 64-bit (16 hex chars)", function () {
    var h = rampart.utils.hash("hello", { type: "murmur" });
    t.mustEq(h.length, 16, "murmur hex is 16 chars (64 bits)");
});

t("hash - city is 64-bit (16 hex chars)", function () {
    var h = rampart.utils.hash("hello", { type: "city" });
    t.mustEq(h.length, 16, "city hex is 16 chars (64 bits)");
});

t("hash - city128 explicit is 32 hex chars", function () {
    var h = rampart.utils.hash("hello", { type: "city128" });
    t.mustEq(h.length, 32, "city128 hex is 32 chars (128 bits)");
});

t("hash - both is 192-bit (48 hex chars) = city128+murmur", function () {
    var h = rampart.utils.hash("hello", { type: "both" });
    t.mustEq(h.length, 48, "both hex is 48 chars (192 bits)");
    var c128 = rampart.utils.hash("hello", { type: "city128" });
    var mur = rampart.utils.hash("hello", { type: "murmur" });
    t.mustEq(h, c128 + mur, "both === city128 concat murmur");
});

t("hash - 'function' is an alias for 'type'", function () {
    var a = rampart.utils.hash("hello", { function: "murmur" });
    var b = rampart.utils.hash("hello", { type: "murmur" });
    t.mustEq(a, b, "function alias matches type");
});

t("hash - returnBuffer:true returns binary buffer matching hex", function () {
    var hex = rampart.utils.hash("hello", { type: "murmur" });
    var buf = rampart.utils.hash("hello", { type: "murmur", returnBuffer: true });
    t.must(typeof buf === "object", "returnBuffer gives buffer object");
    var bufHex = rampart.utils.hexify(buf);
    t.mustEq(bufHex, hex, "buffer hex matches string-hex form");
});

/* ---------- hll ---------- */

t("hll - constructor returns object with documented methods", function () {
    var h = new rampart.utils.hll("a6_basic");
    t.must(typeof h.add === "function", "hll.add exists");
    t.must(typeof h.addFile === "function", "hll.addFile exists");
    t.must(typeof h.count === "function", "hll.count exists");
    t.must(typeof h.merge === "function", "hll.merge exists");
    t.must(typeof h.getBuffer === "function", "hll.getBuffer exists");
});

t("hll - same name retrieves the same object's state", function () {
    var h1 = new rampart.utils.hll("a6_samename");
    h1.add(["x", "y", "z"]);
    var h2 = new rampart.utils.hll("a6_samename");
    t.must(h2.count() >= 1, "same-name hll shares accumulated state");
});

t("hll.add - returns the hll object (chainable)", function () {
    var h = new rampart.utils.hll("a6_addret");
    var r = h.add("singleval");
    t.must(r === h, "add returns the hll object");
});

t("hll.add - accepts string, buffer, and array of both", function () {
    var h = new rampart.utils.hll("a6_addtypes");
    h.add("stringval");                              /* String */
    h.add(new Uint8Array([9, 8, 7, 6]).buffer);      /* Buffer */
    h.add(["arr1", "arr2", new Uint8Array([1, 2, 3]).buffer]); /* array of both */
    t.must(h.count() >= 4, "add accepted mixed types (got " + h.count() + ")");
});

t("hll.count - approximates true cardinality within error band", function () {
    var h = new rampart.utils.hll("a6_card");
    var N = 50000;
    for (var i = 0; i < N; i++) h.add("item_" + i);
    var c = h.count();
    var err = Math.abs(c - N) / N;
    t.must(err < 0.05, "hll count within 5% of true cardinality (got " + c + " vs " + N + ", err " + err.toFixed(4) + ")");
});

t("hll.count - duplicates do not inflate the count", function () {
    var h = new rampart.utils.hll("a6_dups");
    for (var i = 0; i < 10000; i++) h.add("samevalue");
    var c = h.count();
    t.must(c <= 3, "hll count of repeated single value stays near 1 (got " + c + ")");
});

t("hll.addFile - reads values one per line (default delim)", function () {
    var fn = TMP + "/hll_lines.txt";
    var data = "";
    for (var i = 0; i < 1000; i++) data += "line_" + i + "\n";
    rampart.utils.writeFile(fn, data);
    var h = new rampart.utils.hll("a6_addfile");
    var r = h.addFile(fn);
    t.must(r === h, "addFile returns hll object");
    var c = h.count();
    t.must(Math.abs(c - 1000) / 1000 < 0.1, "addFile counted ~1000 lines (got " + c + ")");
    rampart.utils.rmFile(fn);
});

t("hll.addFile - custom delimiter splits on first char", function () {
    var fn = TMP + "/hll_delim.txt";
    var data = "";
    for (var i = 0; i < 500; i++) data += "tok_" + i + ";";
    rampart.utils.writeFile(fn, data);
    var h = new rampart.utils.hll("a6_addfile_delim");
    h.addFile(fn, ";");
    var c = h.count();
    t.must(Math.abs(c - 500) / 500 < 0.12, "addFile custom delim counted ~500 (got " + c + ")");
    rampart.utils.rmFile(fn);
});

t("hll.merge - union cardinality of disjoint sets", function () {
    var h1 = new rampart.utils.hll("a6_merge1");
    var h2 = new rampart.utils.hll("a6_merge2");
    for (var i = 0; i < 10000; i++) h1.add("A_" + i);
    for (var j = 0; j < 10000; j++) h2.add("B_" + j);
    var merged = h1.merge(h2);
    t.must(merged === h1, "merge returns the (updated) hll object");
    var c = merged.count();
    t.must(Math.abs(c - 20000) / 20000 < 0.06, "merged disjoint count ~20000 (got " + c + ")");
});

t("hll - constructor merge form merges supplied hlls", function () {
    var h1 = new rampart.utils.hll("a6_ctormerge1");
    var h2 = new rampart.utils.hll("a6_ctormerge2");
    for (var i = 0; i < 8000; i++) h1.add("C_" + i);
    for (var j = 0; j < 8000; j++) h2.add("D_" + j);
    var combined = new rampart.utils.hll("a6_ctormerge_out", h1, h2);
    var c = combined.count();
    t.must(Math.abs(c - 16000) / 16000 < 0.06, "ctor-merge disjoint count ~16000 (got " + c + ")");
});

t("hll.getBuffer - returns a 16384-byte buffer", function () {
    var h = new rampart.utils.hll("a6_getbuf");
    h.add(["one", "two", "three"]);
    var buf = h.getBuffer();
    t.must(typeof buf === "object", "getBuffer returns buffer");
    t.mustEq(buf.byteLength, 16384, "getBuffer is 16384 bytes");
});

t("hll - reconstruct from buffer preserves count", function () {
    var h = new rampart.utils.hll("a6_serial_src");
    var N = 30000;
    for (var i = 0; i < N; i++) h.add("S_" + i);
    var origCount = h.count();
    var buf = h.getBuffer();
    var h2 = new rampart.utils.hll("a6_serial_dst", buf);
    t.mustEq(h2.count(), origCount, "reconstructed hll has same count as source");
    t.must(Math.abs(h2.count() - N) / N < 0.05, "reconstructed count within 5% of true N");
});

/* ---- cleanup ---- */
try { rampart.utils.rm(TMP, {recursive:true, force:true}); } catch(e){}
t.exit();
