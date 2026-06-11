/* Self-contained test for rampart-gm.
 *
 * No external image files are required.  The test embeds two tiny PNGs
 * (a 16x16 red square and a 24x16 green rectangle) as base64 strings,
 * decodes them to disk under TMPDIR at startup, exercises each method
 * documented in rampart-gm.rst that currently works in the bound
 * module, and cleans up before exit.
 *
 * Multi-image paths (open(array), add, multi-image GIF save) are
 * currently skipped — see "open known bugs" section below.
 */

var testFeature = new (require('./test-feature.js'))({
    prefix: "gm",
    onFail: function() { cleanup(); process.exit(1); }
});

rampart.globalize(rampart.utils);

/* rampart-graphicsmagick requires a system GraphicsMagick install
   (we don't bundle libGraphicsMagick + its codec dependencies any
   more -- libheif/libx265's GPL clashes with the proprietary
   rampart-sql).  If the require fails, the system GM isn't present;
   skip the test set rather than failing run_tests.sh. */
var gm;
try { gm = require("rampart-gm"); }
catch (e) {
    fprintf(stderr,
        "Could not load rampart-gm: %s\nSKIPPING GM TESTS\n",
        e.message);
    process.exit(0);
}

/* --- temp workspace ----------------------------------------------- */
var TMPDIR = "/tmp/rampart-gm-test-" + process.getpid();
mkdir(TMPDIR, true);

/* base64-decode -> ArrayBuffer (atob is a global in rampart). */
function b64ToBuf(s) {
    var bin = atob(s);
    var u8  = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i);
    return u8.buffer;
}

/* Two known-good PNGs generated offline (deterministic content). */
var RED_PNG_B64   = "iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAIAAACQkWg2AAAAFklEQVR4nGM4YWREEmIY1TCqYfhqAAAUBCwQ4b89uwAAAABJRU5ErkJggg==";
var GREEN_PNG_B64 = "iVBORw0KGgoAAAANSUhEUgAAABgAAAAQCAIAAACDRijCAAAAGklEQVR4nGMwOmFEFcQwatCoQaMGjRpEGQIA0K7CED0k5toAAAAASUVORK5CYII=";

var RED_PATH   = TMPDIR + "/red.png";    /* 16x16 PNG, mostly red   */
var GREEN_PATH = TMPDIR + "/green.png";  /* 24x16 PNG, mostly green */

fwrite(RED_PATH,   b64ToBuf(RED_PNG_B64));
fwrite(GREEN_PATH, b64ToBuf(GREEN_PNG_B64));

function cleanup() {
    try { rampart.utils.rm(TMPDIR, {recursive: true}); } catch (e) {}
}

/* ================================================================
 * 1. module loads + open()
 * ============================================================== */

testFeature("module loads", function () {
    return typeof gm === "object" && typeof gm.open === "function";
});

testFeature("open(path) returns an image object", function () {
    var img = gm.open(RED_PATH);
    return typeof img === "object" && img !== null
        && typeof img.identify === "function"
        && typeof img.mogrify  === "function";
});

testFeature("open() throws on a nonexistent file", function () {
    var threw = false;
    try { gm.open(TMPDIR + "/nope.png"); }
    catch (e) { threw = true; }
    return threw;
});

/* ================================================================
 * 2. identify()
 * ============================================================== */

testFeature("identify() reports format/width/height/filename", function () {
    var info = gm.open(RED_PATH).identify();
    testFeature.mustEq(info.magick, "PNG", "magick");
    testFeature.mustEq(info.width,  16,    "width");
    testFeature.mustEq(info.height, 16,    "height");
    testFeature.must(typeof info.filename === "string", "filename is a string");
});

testFeature("identify() distinguishes 24x16 green from 16x16 red", function () {
    var info = gm.open(GREEN_PATH).identify();
    return info.width === 24 && info.height === 16;
});

testFeature("identify(true) returns more detail than identify()", function () {
    var img    = gm.open(RED_PATH);
    var basic  = img.identify();
    var detail = img.identify(true);
    return Object.keys(detail).length > Object.keys(basic).length
        && detail.width === basic.width;
});

/* ================================================================
 * 3. getCount() + list() for the basic single-image case
 * ============================================================== */

testFeature("getCount() returns 1 for a single image", function () {
    return gm.open(RED_PATH).getCount() === 1;
});

testFeature("list() returns an array", function () {
    var l = gm.open(RED_PATH).list();
    return Array.isArray(l) && l.length === 1;
});

/* ================================================================
 * 4. mogrify() — three calling conventions, chaining
 * ============================================================== */

testFeature("mogrify('resize NxM!') resizes the image", function () {
    var img = gm.open(RED_PATH).mogrify("-resize 8x8!");
    var info = img.identify();
    return info.width === 8 && info.height === 8;
});

testFeature("mogrify(name, value) — two-arg form", function () {
    var img = gm.open(RED_PATH).mogrify("resize", "10x10!");
    var info = img.identify();
    return info.width === 10 && info.height === 10;
});

testFeature("mogrify({key: value}) — object form", function () {
    var img = gm.open(RED_PATH).mogrify({ resize: "12x12!" });
    var info = img.identify();
    return info.width === 12 && info.height === 12;
});

testFeature("mogrify() is chainable", function () {
    var img = gm.open(RED_PATH)
                .mogrify({ resize: "20x20!" })
                .mogrify({ resize: "4x4!" });
    var info = img.identify();
    return info.width === 4 && info.height === 4;
});

testFeature("mogrify() with leading '-' on option name is optional", function () {
    var info = gm.open(RED_PATH).mogrify("blur", "0x1").identify();
    return info.width === 16 && info.height === 16;   /* no resize, blur leaves dims */
});

/* ================================================================
 * 5. save() — single-image output
 * ============================================================== */

testFeature("save() writes a PNG", function () {
    var out = TMPDIR + "/saved.png";
    gm.open(RED_PATH).save(out);
    var st = stat(out);
    testFeature.must(st && st.size > 0, "file exists and non-empty");
    var info = gm.open(out).identify();
    return info.magick === "PNG";
});

testFeature("save() converts format from file extension", function () {
    var out = TMPDIR + "/saved.jpg";
    gm.open(RED_PATH).save(out);
    var info = gm.open(out).identify();
    return info.magick === "JPEG" || info.magick === "JPG";
});

testFeature("save() preserves mogrify changes", function () {
    var out = TMPDIR + "/resized.png";
    gm.open(RED_PATH).mogrify({ resize: "4x4!" }).save(out);
    var info = gm.open(out).identify();
    return info.width === 4 && info.height === 4;
});

/* ================================================================
 * 6. toBuffer() — multiple formats, format-name case-insensitive
 * ============================================================== */

testFeature("toBuffer('PNG') returns valid PNG bytes", function () {
    var buf = gm.open(RED_PATH).toBuffer("PNG");
    var a   = new Uint8Array(buf);
    /* PNG signature: 89 50 4E 47 0D 0A 1A 0A */
    return a.length > 16
        && a[0] === 0x89 && a[1] === 0x50 && a[2] === 0x4E && a[3] === 0x47
        && a[4] === 0x0D && a[5] === 0x0A && a[6] === 0x1A && a[7] === 0x0A;
});

testFeature("toBuffer('JPG') returns valid JPEG bytes", function () {
    var buf = gm.open(RED_PATH).toBuffer("JPG");
    var a   = new Uint8Array(buf);
    /* JPEG SOI: FF D8 FF */
    return a.length > 16 && a[0] === 0xFF && a[1] === 0xD8 && a[2] === 0xFF;
});

testFeature("toBuffer() format name is case-insensitive", function () {
    var buf = gm.open(RED_PATH).toBuffer("png");
    var a   = new Uint8Array(buf);
    return a[0] === 0x89 && a[1] === 0x50;
});

testFeature("toBuffer() reflects mogrify changes", function () {
    var buf = gm.open(RED_PATH).mogrify({ resize: "4x4!" }).toBuffer("PNG");
    /* round-trip: write the buffer back to disk and identify it */
    var tmp = TMPDIR + "/from-buffer.png";
    fwrite(tmp, buf);
    var info = gm.open(tmp).identify();
    return info.width === 4 && info.height === 4;
});

/* ================================================================
 * 7. close() — no-throw + does not crash
 * ============================================================== */

testFeature("close() does not throw", function () {
    var img = gm.open(RED_PATH);
    img.close();
    return true;
});

testFeature("operations on a closed handle throw cleanly", function () {
    var img = gm.open(RED_PATH);
    img.close();
    var threw = false;
    try { img.identify(); } catch (e) { threw = true; }
    return threw;
});

/* ================================================================
 * 8. multi-image paths — image-object form of add() works
 * ============================================================== */

testFeature("add(imageObject) appends another image", function () {
    var a = gm.open(RED_PATH);
    var b = gm.open(GREEN_PATH);
    a.add(b);
    return a.getCount() === 2;
});

testFeature("add([imageObject, imageObject]) appends multiple", function () {
    var a = gm.open(RED_PATH);
    a.add([gm.open(GREEN_PATH), gm.open(RED_PATH)]);
    return a.getCount() === 3;
});

testFeature("select(N) picks the Nth image from a multi-image object", function () {
    /* red is 16 wide, green is 24 wide — distinguish frames by width */
    var a = gm.open(RED_PATH);
    a.add(gm.open(GREEN_PATH));
    var first  = a.select(0).identify();
    var second = a.select(1).identify();
    return first.width === 16 && second.width === 24;
});

testFeature("select(-1) picks the last image", function () {
    var a = gm.open(RED_PATH);
    a.add(gm.open(GREEN_PATH));
    return a.select(-1).identify().width === 24;
});

testFeature("save() of multi-image object writes animated GIF", function () {
    /* docs' canonical animated-gif pattern: mogrify each frame with its
       delay, add the second to the first, set loop, save. */
    var imgs   = gm.open(RED_PATH).mogrify({ delay: 20 });
    var second = gm.open(GREEN_PATH).mogrify({ delay: 60 });
    imgs.add(second);
    imgs.mogrify({ loop: 5 });

    var out = TMPDIR + "/animated.gif";
    imgs.save(out);

    var info = gm.open(out).identify();
    return info.magick === "GIF" && info.sceneCount === 2;
});

/* ================================================================
 * 9. Forms of add()/open() — string/buffer/array-of-paths
 * ============================================================== */

testFeature("open([a, b]) opens multiple from an array of paths", function () {
    var imgs = gm.open([RED_PATH, GREEN_PATH]);
    return imgs.getCount() === 2
        && imgs.select(0).identify().width === 16
        && imgs.select(1).identify().width === 24;
});

testFeature("add(path) — string-path form", function () {
    var a = gm.open(RED_PATH);
    a.add(GREEN_PATH);
    return a.getCount() === 2
        && a.select(1).identify().width === 24;
});

testFeature("add(buffer) — Buffer form", function () {
    var a   = gm.open(RED_PATH);
    var buf = gm.open(GREEN_PATH).toBuffer("PNG");
    a.add(buf);
    return a.getCount() === 2
        && a.select(1).identify().width === 24;
});

testFeature("add([path, path]) — array-of-paths form", function () {
    var a = gm.open(RED_PATH);
    a.add([GREEN_PATH, RED_PATH]);
    return a.getCount() === 3
        && a.select(1).identify().width === 24
        && a.select(2).identify().width === 16;
});

/* ================================================================
 * cleanup + exit
 * ============================================================== */

cleanup();
testFeature.exit();
