#!/usr/bin/env rampart
/* vector-full.js
 *
 * Comprehensive coverage test for every function/call-pattern in
 * rampart-vector.rst: the typed Vector Object (new rampart.vector) and its
 * methods, and the rampart.vector.raw.* conversions, L2 normalization and
 * distance functions.
 * Run from the test/ directory:  rampart vector-full.js
 */
var t = new (require('./test-feature.js'))({prefix: "vector"});
rampart.globalize(rampart.utils);
var TMP = "/tmp/vector-full-work"; try { rampart.utils.mkdir(TMP); } catch(e){}


/* ##################### section v1 ##################### */
/* ============================================================
 * Typed Vector Object  (rampart-vector.rst lines ~165-340)
 * ============================================================ */

/* ---- new rampart.vector(): constructor forms & introspection ---- */

t("vector-ctor-ndim-all-types", function(){
    // ndim Number form -> zero-filled vector of given dimensionality
    var sizes = {f64:8, f32:4, f16:2, bf16:2, i8:1, u8:1};
    ["f64","f32","f16","bf16","i8","u8"].forEach(function(ty){
        var v = new rampart.vector(ty, 5);
        t.mustEq(v.type, ty, ty+" ndim ctor: type");
        t.mustEq(v.dim, 5, ty+" ndim ctor: dim");
        var nums = v.toNumbers();
        t.mustEq(nums.length, 5, ty+" ndim ctor: toNumbers length");
        nums.forEach(function(x){ t.must(x===0, ty+" ndim ctor: zero-filled"); });
        t.mustEq(v.byteLength(), sizes[ty]*5, ty+" ndim ctor: byteLength = elemsz*dim");
    });
});

t("vector-ctor-numbarr-all-types", function(){
    // Numbers Array form
    var arr = [0,1,2,3,4,5,6,7];
    ["f64","f32","f16","bf16","i8","u8"].forEach(function(ty){
        var v = new rampart.vector(ty, arr);
        t.mustEq(v.type, ty, ty+" array ctor: type");
        t.mustEq(v.dim, 8, ty+" array ctor: dim");
    });
});

t("vector-ctor-rawbuf-roundtrip", function(){
    // raw Buffer form (buffer produced by another vector's .toRaw())
    var sizes = {f64:8, f32:4, f16:2, bf16:2, i8:1, u8:1};
    ["f64","f32","f16","bf16","i8","u8"].forEach(function(ty){
        var a = new rampart.vector(ty, [0,1,2,3,4,5,6,7]);
        var raw = a.toRaw();
        t.must(raw && typeof raw.length === "number", ty+" toRaw returns Buffer with .length");
        t.mustEq(raw.length, sizes[ty]*8, ty+" toRaw byte length");
        var b = new rampart.vector(ty, raw);
        t.mustEq(b.type, ty, ty+" rawbuf ctor: type");
        t.mustEq(b.dim, 8, ty+" rawbuf ctor: dim (derived from buf len & type)");
        // round-trips through identical raw bytes -> identical numbers
        t.mustEq(b.toNumbers(), a.toNumbers(), ty+" rawbuf ctor: numbers preserved");
    });
});

t("vector-introspection-methods-present", function(){
    // Per doc introspection example: these methods exist on the Vector Object
    var v = new rampart.vector("f32", [0,1,2,3,4,5,6,7]);
    t.mustEq(v.type, "f32", "introspection: type");
    t.mustEq(v.dim, 8, "introspection: dim");
    ["toF64","toF32","toF16","toBf16","toI8","toU8","toNumbers",
     "l2Normalize","toRaw","byteLength","resize","copy","distance"].forEach(function(m){
        t.must(typeof v[m] === "function", "introspection: method "+m+" is a function");
    });
});

/* ---- Conversion Functions: self-conversion NULL-OP, others NEW ---- */

t("vector-self-conversion-nullop", function(){
    // each type has a conversion to its own type which is a NULL-OP returning
    // the SAME object (including bf16.toBf16()).
    var selfMap = {f64:"toF64", f32:"toF32", f16:"toF16", bf16:"toBf16", i8:"toI8", u8:"toU8"};
    Object.keys(selfMap).forEach(function(ty){
        var m = selfMap[ty];
        var v = new rampart.vector(ty, [1,2,3,4]);
        t.must(typeof v[m] === "function", ty+" has self-conv "+m);
        t.must(v[m]() === v, ty+"."+m+"() is NULL-OP returning SAME object");
    });
});

t("vector-conversion-matrix", function(){
    // Probe the actual conversion matrix. Doc explicitly says not every
    // conversion exists (e.g. i8 has no .toU8). Assert ACTUAL availability.
    // true = method present (returns a Vector Object), false = method absent.
    var expect = {
        f64:  {toF64:1, toF32:1, toF16:1, toBf16:1, toI8:1, toU8:1},
        f32:  {toF64:1, toF32:1, toF16:1, toBf16:1, toI8:1, toU8:1},
        f16:  {toF64:1, toF32:1, toF16:1, toBf16:0, toI8:1, toU8:1},
        bf16: {toF64:1, toF32:1, toF16:0, toBf16:1, toI8:0, toU8:0},
        i8:   {toF64:1, toF32:1, toF16:1, toBf16:0, toI8:1, toU8:0},
        u8:   {toF64:1, toF32:1, toF16:1, toBf16:0, toI8:1, toU8:1}   /* toI8 = u8->i8 rebase */
    };
    Object.keys(expect).forEach(function(ty){
        var v = new rampart.vector(ty, [0,1,2,3,4,5,6,7]);
        var row = expect[ty];
        Object.keys(row).forEach(function(m){
            var present = typeof v[m] === "function";
            t.must(present === !!row[m],
                ty+"."+m+" present="+present+" (expected "+!!row[m]+")");
            if (present && row[m]) {
                var o = v[m]();
                t.must(o && (o.type !== undefined),
                    ty+"."+m+"() returns a Vector Object");
            }
        });
    });
    // i8 has no toU8 (doc's explicit example)
    t.must(typeof (new rampart.vector("i8",[1,2,3])).toU8 === "undefined",
        "i8 Vector Object has no .toU8() (doc example)");
});

t("vector-cross-conversion-returns-new", function(){
    // non-self conversions return a NEW Vector Object (not the same)
    var v = new rampart.vector("f64", [0,1,2,3]);
    var f32 = v.toF32();
    t.must(f32 !== v, "f64.toF32() returns NEW object");
    t.mustEq(f32.type, "f32", "f64.toF32() result type");
    t.mustEq(f32.dim, 4, "f64.toF32() result dim preserved");
});

t("vector-toNumbers-roundtrip-float", function(){
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var arr = [0.1,-0.2,0.35,0.5,-0.7,0.9,-1.0,0.05];
    var tols = {f64:1e-9, f32:1e-4, f16:2e-2, bf16:1e-1};
    Object.keys(tols).forEach(function(ty){
        var v = new rampart.vector(ty, arr);
        var back = v.toNumbers();
        t.must(Array.isArray(back), ty+" toNumbers returns Array");
        t.mustEq(back.length, arr.length, ty+" toNumbers length");
        for (var i=0;i<arr.length;i++)
            t.must(closeTo(back[i], arr[i], tols[ty]),
                ty+" round-trip elem "+i+" "+back[i]+" ~ "+arr[i]);
    });
});

t("vector-toNumbers-roundtrip-quantized", function(){
    // i8/u8 are quantized: with default auto-scale, the largest-magnitude
    // element maps to full-scale (1.0). Assert structural/ordering properties
    // rather than exact value equality.
    var i8 = new rampart.vector("i8", [0.1,-0.2,0.35,0.5]);
    var bi = i8.toNumbers();
    t.mustEq(bi.length, 4, "i8 toNumbers length");
    // max-magnitude element (0.5) reconstructs to ~1.0 with default scale
    t.must(Math.abs(bi[3]-1.0) < 0.05, "i8 default-scale: max elem -> ~1.0");
    t.must(bi[1] < 0, "i8 preserves sign of negative element");

    var u8 = new rampart.vector("u8", [0,1,2,3,4,5,6,7]);
    var bu = u8.toNumbers();
    t.mustEq(bu.length, 8, "u8 toNumbers length");
    t.must(Math.abs(bu[7]-1.0) < 0.05, "u8 default-scale: max elem -> ~1.0");
    t.must(bu[0] >= 0 && bu[0] < 0.05, "u8 min elem -> ~0.0");
});

t("vector-toI8-toU8-scale-arg", function(){
    // toI8/toU8 accept optional (scale[, zeroPoint])
    var v = new rampart.vector("f32", [1,2,3,4]);
    var i8a = v.toI8();        // auto scale
    var i8b = v.toI8(0.05);    // explicit scale
    t.mustEq(i8a.type, "i8", "toI8() default type");
    t.mustEq(i8b.type, "i8", "toI8(scale) type");
    // explicit scale changes the reconstruction vs auto-scale
    t.must(JSON.stringify(i8a.toNumbers()) !== JSON.stringify(i8b.toNumbers()),
        "toI8(scale) differs from auto-scale toI8()");
    var u8a = v.toU8();
    var u8b = v.toU8(0.05, 0);
    t.mustEq(u8a.type, "u8", "toU8() default type");
    t.mustEq(u8b.type, "u8", "toU8(scale,zeroPoint) type");
});

/* ---- Information Constants ---- */

t("vector-info-constants", function(){
    var v = new rampart.vector("f16", [9,8,7]);
    t.mustEq(v.type, "f16", "info const: type");
    t.mustEq(v.dim, 3, "info const: dim = number of elements");
});

/* ---- Utility Functions ---- */

t("vector-l2Normalize-inplace-unit", function(){
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var v = new rampart.vector("f64", [3,4]);  // |v| = 5
    var ret = v.l2Normalize();
    t.must(ret === v, "l2Normalize() returns SAME object (in-place)");
    var nums = v.toNumbers();
    var ss = 0; nums.forEach(function(x){ ss += x*x; });
    t.must(closeTo(ss, 1.0, 1e-9), "l2Normalize: unit length (sum of squares ~1)");
    t.must(closeTo(nums[0], 0.6, 1e-9) && closeTo(nums[1], 0.8, 1e-9),
        "l2Normalize: [3,4] -> [0.6,0.8]");
});

t("vector-toRaw-buffer", function(){
    var v = new rampart.vector("f32", [1,2,3,4]);
    var raw = v.toRaw();
    t.must(raw && typeof raw.length === "number", "toRaw returns Buffer");
    t.mustEq(raw.length, 16, "toRaw byte length = 4 elems * 4 bytes");
});

t("vector-copy-independent", function(){
    var orig = new rampart.vector("f64", [1,2,3]);
    var cp = orig.copy();
    t.must(cp !== orig, "copy() returns NEW object");
    t.mustEq(cp.type, "f64", "copy: type preserved");
    t.mustEq(cp.dim, 3, "copy: dim preserved");
    t.mustEq(cp.toNumbers(), orig.toNumbers(), "copy: contents equal");
    // independence: normalizing the copy must not affect the original
    cp.l2Normalize();
    t.mustEq(orig.toNumbers(), [1,2,3], "copy: original unchanged after copy mutated");
});

t("vector-resize-grow-truncate", function(){
    var orig = new rampart.vector("f64", [1,2,3]);
    var grow = orig.resize(5);
    t.must(grow !== orig, "resize() returns NEW object");
    t.mustEq(grow.dim, 5, "resize grow: dim = n");
    t.mustEq(grow.toNumbers(), [1,2,3,0,0], "resize grow: zero-fills new elements");
    var trunc = orig.resize(2);
    t.mustEq(trunc.dim, 2, "resize truncate: dim = n");
    t.mustEq(trunc.toNumbers(), [1,2], "resize truncate: drops trailing elements");
    // original untouched
    t.mustEq(orig.toNumbers(), [1,2,3], "resize: original unchanged");
});

t("vector-byteLength-per-type", function(){
    var sizes = {f64:8, f32:4, f16:2, bf16:2, i8:1, u8:1};
    Object.keys(sizes).forEach(function(ty){
        var v = new rampart.vector(ty, [0,1,2,3,4,5,6]); // dim 7
        t.mustEq(v.byteLength(), sizes[ty]*7,
            ty+" byteLength = "+sizes[ty]+" * dim(7)");
    });
});

/* ---- Distance Function (object method) ---- */

t("vector-distance-same-type-required", function(){
    var v1 = new rampart.vector("f32", [0,1,2,3,4,5,6,7]);
    var v2 = new rampart.vector("f64", [0,-1,-2,-3,-4,-5,-6,-7]);
    v1.l2Normalize();
    v2.l2Normalize();
    // mixed types must throw with the documented message
    var threw = false, msg = "";
    try { v1.distance(v2, "dot"); } catch(e){ threw = true; msg = e.message; }
    t.must(threw, "distance() throws on differing vector types");
    t.mustContain(msg, "vectors must be the same type, convert one first",
        "distance() throw message matches doc");
});

t("vector-distance-doc-example-opposite", function(){
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var v1 = new rampart.vector("f32", [0,1,2,3,4,5,6,7]);
    var v2 = new rampart.vector("f64", [0,-1,-2,-3,-4,-5,-6,-7]);
    v1.l2Normalize();
    v2.l2Normalize();
    // convert v2 to f32, then compare -> opposite normalized vectors -> dot ~ -1.0
    t.must(closeTo(v1.distance(v2.toF32(), "dot"), -1.0, 1e-3),
        "doc example: v1.distance(v2.toF32(),'dot') ~ -1.0");
    // OR convert v1 to f64
    t.must(closeTo(v1.toF64().distance(v2, "dot"), -1.0, 1e-6),
        "doc example: v1.toF64().distance(v2,'dot') ~ -1.0");
});

t("vector-distance-metrics-equal", function(){
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var a = new rampart.vector("f32", [1,2,3,4]);
    var b = new rampart.vector("f32", [1,2,3,4]);
    a.l2Normalize();
    b.l2Normalize();
    // identical normalized vectors: dot ~1, euclidean ~0, cosine ~0
    t.must(closeTo(a.distance(b, "dot"), 1.0, 1e-5),       "equal vectors: dot ~ 1.0");
    t.must(closeTo(a.distance(b, "euclidean"), 0.0, 1e-5), "equal vectors: euclidean ~ 0.0");
    t.must(closeTo(a.distance(b, "cosine"), 0.0, 1e-5),    "equal vectors: cosine ~ 0.0");
});


/* ##################### section v2 ##################### */
/* ==================================================================
 * rampart.vector.raw.* Numbers <-> raw buffer conversions
 * ================================================================== */

t("vector.raw.numbersToF64 - byte length", function(){
    var n = [0.5, -0.25, 0.125, 0, 1, -1];
    var b = rampart.vector.raw.numbersToF64(n);
    t.mustEq(b.length, n.length * 8, "f64 buffer = 8 bytes/element");
});

t("vector.raw.numbersToF64 - round trip (exact)", function(){
    var n = [0.5, -0.25, 0.125, 0, 1, -1, 3.14159265358979, -2.71828182845905];
    var out = rampart.vector.raw.f64ToNumbers(rampart.vector.raw.numbersToF64(n));
    t.mustEq(out.length, n.length, "length preserved");
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 1e-9, "f64 elem "+i+" "+out[i]+" ~ "+n[i]);
});

t("vector.raw.numbersToF32 - byte length", function(){
    var n = [0.5, -0.25, 0.125, 0, 1, -1];
    var b = rampart.vector.raw.numbersToF32(n);
    t.mustEq(b.length, n.length * 4, "f32 buffer = 4 bytes/element");
});

t("vector.raw.numbersToF32 - round trip (~1e-4)", function(){
    var n = [0.5, -0.25, 0.125, 0, 1, -1, 3.14159265, -2.71828183];
    var out = rampart.vector.raw.f32ToNumbers(rampart.vector.raw.numbersToF32(n));
    t.mustEq(out.length, n.length, "length preserved");
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 1e-4, "f32 elem "+i+" "+out[i]+" ~ "+n[i]);
});

t("vector.raw.numbersToF16 - byte length", function(){
    var n = [0.5, -0.25, 0.125, 0, 1, -1];
    var b = rampart.vector.raw.numbersToF16(n);
    t.mustEq(b.length, n.length * 2, "f16 buffer = 2 bytes/element");
});

t("vector.raw.numbersToF16 - round trip (~2e-2)", function(){
    var n = [0.5, -0.25, 0.125, 0, 1, -1, 0.75, -0.375];
    var out = rampart.vector.raw.f16ToNumbers(rampart.vector.raw.numbersToF16(n));
    t.mustEq(out.length, n.length, "length preserved");
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 2e-2, "f16 elem "+i+" "+out[i]+" ~ "+n[i]);
});

t("vector.raw.numbersToBf16 - byte length", function(){
    var n = [0.5, -0.25, 0.125, 0, 1, -1];
    var b = rampart.vector.raw.numbersToBf16(n);
    t.mustEq(b.length, n.length * 2, "bf16 buffer = 2 bytes/element");
});

t("vector.raw.numbersToBf16 - round trip (~1e-1)", function(){
    var n = [0.5, -0.25, 0.125, 0, 1, -1, 0.75, -0.375];
    var out = rampart.vector.raw.bf16ToNumbers(rampart.vector.raw.numbersToBf16(n));
    t.mustEq(out.length, n.length, "length preserved");
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 1e-1, "bf16 elem "+i+" "+out[i]+" ~ "+n[i]);
});

t("vector.raw.numbersToI8 - byte length", function(){
    var n = [0.5, -0.25, 0.125, 0, 1, -1];
    var b = rampart.vector.raw.numbersToI8(n);
    t.mustEq(b.length, n.length * 1, "i8 buffer = 1 byte/element");
});

t("vector.raw.numbersToI8 - round trip default (auto-scale)", function(){
    // auto-scale normalizes by max abs value
    var n = [0, 2, -4, 8];
    var i8 = rampart.vector.raw.numbersToI8(n);
    var out = rampart.vector.raw.i8ToNumbers(i8);
    t.mustEq(out.length, n.length, "length preserved");
    // auto-scale maps max-abs (8) -> 1.0, so compare ratios
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i] - n[i]/8) < 1e-2, "i8 auto elem "+i+" "+out[i]+" ~ "+(n[i]/8));
});

t("vector.raw.numbersToI8 - round trip explicit scale 1/127", function(){
    var n = [0.5, -0.25, 0.125, 0, 1, -1];
    var i8 = rampart.vector.raw.numbersToI8(n, 1/127);
    var out = rampart.vector.raw.i8ToNumbers(i8, 1/127);
    t.mustEq(out.length, n.length, "length preserved");
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 1e-2, "i8 elem "+i+" "+out[i]+" ~ "+n[i]);
});

t("vector.raw.numbersToI8 - holds negatives", function(){
    var n = [-1, -0.5, 0.5, 1];
    var out = rampart.vector.raw.i8ToNumbers(rampart.vector.raw.numbersToI8(n, 1/127), 1/127);
    t.must(out[0] < 0 && out[1] < 0, "negatives preserved as negative");
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 1e-2, "i8 neg elem "+i+" "+out[i]+" ~ "+n[i]);
});

t("vector.raw.numbersToI8 - explicit scale + zeroPoint", function(){
    // zeroPoint -100 (in -128..127); scale 1 maps integer values directly
    var n = [10, 11, 12, 13];
    var i8 = rampart.vector.raw.numbersToI8(n, 1, -100);
    var out = rampart.vector.raw.i8ToNumbers(i8, 1, -100);
    t.mustEq(out.length, n.length, "length preserved");
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 1e-6, "i8 zp elem "+i+" "+out[i]+" ~ "+n[i]);
});

t("vector.raw.numbersToU8 - byte length", function(){
    var n = [0, 1, 2, 3, 4, 5];
    var b = rampart.vector.raw.numbersToU8(n);
    t.mustEq(b.length, n.length * 1, "u8 buffer = 1 byte/element");
});

t("vector.raw.numbersToU8 - round trip default", function(){
    var n = [0, 0.25, 0.5, 0.75, 1];
    var out = rampart.vector.raw.u8ToNumbers(rampart.vector.raw.numbersToU8(n));
    t.mustEq(out.length, n.length, "length preserved");
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 1e-2, "u8 def elem "+i+" "+out[i]+" ~ "+n[i]);
});

t("vector.raw.numbersToU8 - round trip explicit scale 1/255", function(){
    var n = [0, 0.25, 0.5, 0.75, 1];
    var u8 = rampart.vector.raw.numbersToU8(n, 1/255);
    var out = rampart.vector.raw.u8ToNumbers(u8, 1/255);
    t.mustEq(out.length, n.length, "length preserved");
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 1e-2, "u8 elem "+i+" "+out[i]+" ~ "+n[i]);
});

t("vector.raw.numbersToU8 - holds 0..255 with explicit scale+zeroPoint", function(){
    // scale 1, zeroPoint 10: values map directly into uint8 range
    var n = [10, 11, 12, 13, 255];
    var u8 = rampart.vector.raw.numbersToU8(n, 1, 0);
    var out = rampart.vector.raw.u8ToNumbers(u8, 1, 0);
    t.mustEq(out.length, n.length, "length preserved");
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 1e-6, "u8 range elem "+i+" "+out[i]+" ~ "+n[i]);
    t.must(out[4] === 255, "u8 holds 255");
});

t("vector.raw.numbersToU8 - zeroPoint round trip", function(){
    var n = [10, 11, 12, 13];
    var u8 = rampart.vector.raw.numbersToU8(n, 1, 10);
    var out = rampart.vector.raw.u8ToNumbers(u8, 1, 10);
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 1e-6, "u8 zp elem "+i+" "+out[i]+" ~ "+n[i]);
});

t("vector.raw.f64ToNumbers - converts double buffer", function(){
    var n = [1.5, 2.5, 3.5];
    var out = rampart.vector.raw.f64ToNumbers(rampart.vector.raw.numbersToF64(n));
    t.mustEq(out, n, "f64ToNumbers exact round trip");
});

t("vector.raw.f32ToNumbers - converts float buffer", function(){
    var n = [1.5, 2.5, 3.5];
    var out = rampart.vector.raw.f32ToNumbers(rampart.vector.raw.numbersToF32(n));
    t.mustEq(out, n, "f32ToNumbers exact round trip (half-int values)");
});

t("vector.raw.f16ToNumbers - converts half buffer", function(){
    var n = [1.5, 2.5, 3.5];
    var out = rampart.vector.raw.f16ToNumbers(rampart.vector.raw.numbersToF16(n));
    t.mustEq(out.length, n.length, "length preserved");
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 2e-2, "f16ToNumbers elem "+i);
});

t("vector.raw.bf16ToNumbers - converts bf16 buffer", function(){
    var n = [1.5, 2.5, 3.5];
    var out = rampart.vector.raw.bf16ToNumbers(rampart.vector.raw.numbersToBf16(n));
    t.mustEq(out.length, n.length, "length preserved");
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 1e-1, "bf16ToNumbers elem "+i);
});

t("vector.raw.u8ToNumbers - default scale 1/255", function(){
    // doc: default scale 1.0/255, zeroPoint 0. numbersToU8 default uses same.
    var n = [0, 0.5, 1];
    var out = rampart.vector.raw.u8ToNumbers(rampart.vector.raw.numbersToU8(n));
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 1e-2, "u8ToNumbers def elem "+i);
});

t("vector.raw.i8ToNumbers - default scale 1/127", function(){
    // doc: default scale 1.0/127, zeroPoint 0.
    var n = [-1, 0, 1];
    var out = rampart.vector.raw.i8ToNumbers(rampart.vector.raw.numbersToI8(n, 1/127));
    for (var i=0; i<n.length; i++)
        t.must(Math.abs(out[i]-n[i]) < 1e-2, "i8ToNumbers def elem "+i);
});


/* ##################### section v3 ##################### */

/* ========================================================================
   rampart.vector.raw.* buffer-to-buffer conversions (doc lines ~569-832)
   ======================================================================== */

/* ---- from f64 ---- */

t("vector.raw.f64ToF32", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[-1.5, -0.25, 0, 0.5, 1.0, 3.14159, -42.0, 100.0];
    var src=rampart.vector.raw.numbersToF64(vals);
    var res=rampart.vector.raw.f64ToF32(src);
    t.must(res.length===vals.length*4, "byte length 4/elem (got "+res.length+")");
    var back=rampart.vector.raw.f32ToNumbers(res);
    t.must(arraysClose(back,vals,1e-4), "f64->f32 roundtrip approx equal: "+JSON.stringify(back));
    return true;
});

t("vector.raw.f64ToF16", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[-1.0, -0.5, 0, 0.25, 0.5, 1.0];
    var src=rampart.vector.raw.numbersToF64(vals);
    var res=rampart.vector.raw.f64ToF16(src);
    t.must(res.length===vals.length*2, "byte length 2/elem (got "+res.length+")");
    var back=rampart.vector.raw.f16ToNumbers(res);
    t.must(arraysClose(back,vals,2e-2), "f64->f16 roundtrip approx equal: "+JSON.stringify(back));
    return true;
});

t("vector.raw.f64ToBf16", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[-1.0, -0.5, 0, 0.25, 0.5, 1.0];
    var src=rampart.vector.raw.numbersToF64(vals);
    var res=rampart.vector.raw.f64ToBf16(src);
    t.must(res.length===vals.length*2, "byte length 2/elem (got "+res.length+")");
    var back=rampart.vector.raw.bf16ToNumbers(res);
    t.must(arraysClose(back,vals,1e-1), "f64->bf16 roundtrip approx equal: "+JSON.stringify(back));
    return true;
});

t("vector.raw.f64ToI8 (auto-scale default)", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[-1.0, -0.5, 0, 0.25, 0.5, 1.0];
    var src=rampart.vector.raw.numbersToF64(vals);
    var res=rampart.vector.raw.f64ToI8(src);
    t.must(res.length===vals.length*1, "byte length 1/elem (got "+res.length+")");
    // auto-scale: dequantize with same auto-derived scale (max abs /127)
    var scale=1.0/127.0;
    var back=rampart.vector.raw.i8ToNumbers(res, scale);
    t.must(arraysClose(back,vals,2e-2), "f64->i8 auto-scale roundtrip approx: "+JSON.stringify(back));
    return true;
});

t("vector.raw.f64ToI8 (explicit scale)", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[-10, -5, 0, 5, 10, 50, -100];
    var scale=1.0; // 1 unit per int8 step; values fit in [-128,127]
    var src=rampart.vector.raw.numbersToF64(vals);
    var res=rampart.vector.raw.f64ToI8(src, scale);
    t.must(res.length===vals.length*1, "byte length 1/elem (got "+res.length+")");
    var back=rampart.vector.raw.i8ToNumbers(res, scale);
    t.must(arraysClose(back,vals,1e-6), "f64->i8 explicit-scale exact for integers: "+JSON.stringify(back));
    return true;
});

t("vector.raw.f64ToU8 (auto-scale default)", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[0, 0.1, 0.25, 0.5, 0.75, 1.0];
    var src=rampart.vector.raw.numbersToF64(vals);
    var res=rampart.vector.raw.f64ToU8(src);
    t.must(res.length===vals.length*1, "byte length 1/elem (got "+res.length+")");
    var scale=1.0/255.0;
    var back=rampart.vector.raw.u8ToNumbers(res, scale);
    t.must(arraysClose(back,vals,1e-2), "f64->u8 auto-scale roundtrip approx: "+JSON.stringify(back));
    return true;
});

t("vector.raw.f64ToU8 (explicit scale+zeroPoint)", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[0, 1, 2, 5, 10, 100, 250];
    var scale=1.0, zp=0;
    var src=rampart.vector.raw.numbersToF64(vals);
    var res=rampart.vector.raw.f64ToU8(src, scale, zp);
    t.must(res.length===vals.length*1, "byte length 1/elem (got "+res.length+")");
    var back=rampart.vector.raw.u8ToNumbers(res, scale, zp);
    t.must(arraysClose(back,vals,1e-6), "f64->u8 explicit-scale exact for integers: "+JSON.stringify(back));
    return true;
});

/* ---- to f64 ---- */

t("vector.raw.f32ToF64", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[-1.5, -0.25, 0, 0.5, 1.0, 3.14159, -42.0, 100.0];
    var src=rampart.vector.raw.numbersToF32(vals);
    var res=rampart.vector.raw.f32ToF64(src);
    t.must(res.length===vals.length*8, "byte length 8/elem (got "+res.length+")");
    var back=rampart.vector.raw.f64ToNumbers(res);
    t.must(arraysClose(back,vals,1e-4), "f32->f64 roundtrip approx equal: "+JSON.stringify(back));
    return true;
});

t("vector.raw.f16ToF64", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[-1.0, -0.5, 0, 0.25, 0.5, 1.0];
    var src=rampart.vector.raw.numbersToF16(vals);
    var res=rampart.vector.raw.f16ToF64(src);
    t.must(res.length===vals.length*8, "byte length 8/elem (got "+res.length+")");
    var back=rampart.vector.raw.f64ToNumbers(res);
    t.must(arraysClose(back,vals,2e-2), "f16->f64 roundtrip approx equal: "+JSON.stringify(back));
    return true;
});

t("vector.raw.bf16ToF64", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[-1.0, -0.5, 0, 0.25, 0.5, 1.0];
    var src=rampart.vector.raw.numbersToBf16(vals);
    var res=rampart.vector.raw.bf16ToF64(src);
    t.must(res.length===vals.length*8, "byte length 8/elem (got "+res.length+")");
    var back=rampart.vector.raw.f64ToNumbers(res);
    t.must(arraysClose(back,vals,1e-1), "bf16->f64 roundtrip approx equal: "+JSON.stringify(back));
    return true;
});

t("vector.raw.i8ToF64", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[-10, -5, 0, 5, 10, 50, -100];
    var scale=1.0;
    var src=rampart.vector.raw.numbersToI8(vals, scale);
    var res=rampart.vector.raw.i8ToF64(src, scale);
    t.must(res.length===vals.length*8, "byte length 8/elem (got "+res.length+")");
    var back=rampart.vector.raw.f64ToNumbers(res);
    t.must(arraysClose(back,vals,1e-6), "i8->f64 explicit-scale exact for integers: "+JSON.stringify(back));
    return true;
});

t("vector.raw.u8ToF64", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[0, 1, 2, 5, 10, 100, 250];
    var scale=1.0, zp=0;
    var src=rampart.vector.raw.numbersToU8(vals, scale, zp);
    var res=rampart.vector.raw.u8ToF64(src, scale, zp);
    t.must(res.length===vals.length*8, "byte length 8/elem (got "+res.length+")");
    var back=rampart.vector.raw.f64ToNumbers(res);
    t.must(arraysClose(back,vals,1e-6), "u8->f64 explicit-scale exact for integers: "+JSON.stringify(back));
    return true;
});

/* ---- from f32 ---- */

t("vector.raw.f32ToF16", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[-1.0, -0.5, 0, 0.25, 0.5, 1.0];
    var src=rampart.vector.raw.numbersToF32(vals);
    var res=rampart.vector.raw.f32ToF16(src);
    t.must(res.length===vals.length*2, "byte length 2/elem (got "+res.length+")");
    var back=rampart.vector.raw.f16ToNumbers(res);
    t.must(arraysClose(back,vals,2e-2), "f32->f16 roundtrip approx equal: "+JSON.stringify(back));
    return true;
});

t("vector.raw.f32ToBf16", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[-1.0, -0.5, 0, 0.25, 0.5, 1.0];
    var src=rampart.vector.raw.numbersToF32(vals);
    var res=rampart.vector.raw.f32ToBf16(src);
    t.must(res.length===vals.length*2, "byte length 2/elem (got "+res.length+")");
    var back=rampart.vector.raw.bf16ToNumbers(res);
    t.must(arraysClose(back,vals,1e-1), "f32->bf16 roundtrip approx equal: "+JSON.stringify(back));
    return true;
});

t("vector.raw.f32ToI8 (auto-scale default)", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[-1.0, -0.5, 0, 0.25, 0.5, 1.0];
    var src=rampart.vector.raw.numbersToF32(vals);
    var res=rampart.vector.raw.f32ToI8(src);
    t.must(res.length===vals.length*1, "byte length 1/elem (got "+res.length+")");
    var scale=1.0/127.0;
    var back=rampart.vector.raw.i8ToNumbers(res, scale);
    t.must(arraysClose(back,vals,2e-2), "f32->i8 auto-scale roundtrip approx: "+JSON.stringify(back));
    return true;
});

t("vector.raw.f32ToI8 (explicit scale)", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[-10, -5, 0, 5, 10, 50, -100];
    var scale=1.0;
    var src=rampart.vector.raw.numbersToF32(vals);
    var res=rampart.vector.raw.f32ToI8(src, scale);
    t.must(res.length===vals.length*1, "byte length 1/elem (got "+res.length+")");
    var back=rampart.vector.raw.i8ToNumbers(res, scale);
    t.must(arraysClose(back,vals,1e-6), "f32->i8 explicit-scale exact for integers: "+JSON.stringify(back));
    return true;
});

t("vector.raw.f32ToU8 (auto-scale default)", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[0, 0.1, 0.25, 0.5, 0.75, 1.0];
    var src=rampart.vector.raw.numbersToF32(vals);
    var res=rampart.vector.raw.f32ToU8(src);
    t.must(res.length===vals.length*1, "byte length 1/elem (got "+res.length+")");
    var scale=1.0/255.0;
    var back=rampart.vector.raw.u8ToNumbers(res, scale);
    t.must(arraysClose(back,vals,1e-2), "f32->u8 auto-scale roundtrip approx: "+JSON.stringify(back));
    return true;
});

t("vector.raw.f32ToU8 (explicit scale)", function(){
    function arraysClose(a,b,tol){ if(a.length!==b.length) return false;
        for(var i=0;i<a.length;i++){ if(Math.abs(a[i]-b[i])>tol) return false; } return true; }
    var vals=[0, 1, 2, 5, 10, 100, 250];
    var scale=1.0, zp=0;
    var src=rampart.vector.raw.numbersToF32(vals);
    var res=rampart.vector.raw.f32ToU8(src, scale, zp);
    t.must(res.length===vals.length*1, "byte length 1/elem (got "+res.length+")");
    var back=rampart.vector.raw.u8ToNumbers(res, scale, zp);
    t.must(arraysClose(back,vals,1e-6), "f32->u8 explicit-scale exact for integers: "+JSON.stringify(back));
    return true;
});


/* ##################### section v4 ##################### */

/* ========================================================================
 * BUFFER CONVERSIONS (to-float-back and re-quantize round trips)
 * ====================================================================== */

t("raw.f16ToF32 - round trip & byte length", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var src = [0.0, 0.5, -0.25, 1.0, -1.0, 0.123, -0.875, 3.5];
    var f16 = raw.numbersToF16(src);
    var f32 = raw.f16ToF32(f16);
    t.mustEq(f32.length, src.length*4, "f32 buffer is 4 bytes/elem");
    var back = raw.f32ToNumbers(f32);
    t.mustEq(back.length, src.length, "element count preserved");
    for (var i=0;i<src.length;i++)
        t.must(closeTo(back[i], src[i], 2e-2), "f16ToF32 elem "+i+" "+back[i]+" vs "+src[i]);
    return true;
});

t("raw.bf16ToF32 - round trip & byte length", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var src = [0.0, 0.5, -0.25, 1.0, -1.0, 0.125, -0.75, 2.0];
    var bf16 = raw.numbersToBf16(src);
    var f32 = raw.bf16ToF32(bf16);
    t.mustEq(f32.length, src.length*4, "f32 buffer is 4 bytes/elem");
    var back = raw.f32ToNumbers(f32);
    for (var i=0;i<src.length;i++)
        t.must(closeTo(back[i], src[i], 1e-1), "bf16ToF32 elem "+i+" "+back[i]+" vs "+src[i]);
    return true;
});

t("raw.u8ToF32 - round trip & byte length", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    // u8 default scale 1.0/255, zeroPoint 0 => values in [0,1]
    var src = [0.0, 0.25, 0.5, 0.75, 1.0, 0.1, 0.9, 0.333];
    var u8 = raw.numbersToU8(src);
    var f32 = raw.u8ToF32(u8);
    t.mustEq(f32.length, src.length*4, "f32 buffer is 4 bytes/elem");
    t.mustEq(u8.length, src.length, "u8 buffer is 1 byte/elem");
    var back = raw.f32ToNumbers(f32);
    for (var i=0;i<src.length;i++)
        t.must(closeTo(back[i], src[i], 1e-2), "u8ToF32 elem "+i+" "+back[i]+" vs "+src[i]);
    return true;
});

t("raw.i8ToF32 - round trip & byte length", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    // i8 default scale 1.0/127, zeroPoint 0 => values in [-1,1]
    var src = [0.0, 0.25, -0.5, 0.75, -1.0, 1.0, -0.333, 0.111];
    var i8 = raw.numbersToI8(src);
    var f32 = raw.i8ToF32(i8);
    t.mustEq(f32.length, src.length*4, "f32 buffer is 4 bytes/elem");
    t.mustEq(i8.length, src.length, "i8 buffer is 1 byte/elem");
    var back = raw.f32ToNumbers(f32);
    for (var i=0;i<src.length;i++)
        t.must(closeTo(back[i], src[i], 2e-2), "i8ToF32 elem "+i+" "+back[i]+" vs "+src[i]);
    return true;
});

t("raw.f16ToI8 - round trip & byte length", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var src = [0.0, 0.25, -0.5, 0.75, -1.0, 1.0, -0.25, 0.5];
    var f16 = raw.numbersToF16(src);
    // explicit scale/zeroPoint so the i8->f32 reconstruction matches
    var i8 = raw.f16ToI8(f16, 1.0/127.0, 0);
    t.mustEq(i8.length, src.length, "i8 buffer is 1 byte/elem");
    var f32 = raw.i8ToF32(i8, 1.0/127.0, 0);
    var back = raw.f32ToNumbers(f32);
    for (var i=0;i<src.length;i++)
        t.must(closeTo(back[i], src[i], 3e-2), "f16ToI8 elem "+i+" "+back[i]+" vs "+src[i]);
    return true;
});

t("raw.f16ToU8 - round trip & byte length", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var src = [0.0, 0.25, 0.5, 0.75, 1.0, 0.1, 0.9, 0.5];
    var f16 = raw.numbersToF16(src);
    var u8 = raw.f16ToU8(f16, 1.0/255.0, 0);
    t.mustEq(u8.length, src.length, "u8 buffer is 1 byte/elem");
    var f32 = raw.u8ToF32(u8, 1.0/255.0, 0);
    var back = raw.f32ToNumbers(f32);
    for (var i=0;i<src.length;i++)
        t.must(closeTo(back[i], src[i], 2e-2), "f16ToU8 elem "+i+" "+back[i]+" vs "+src[i]);
    return true;
});

t("raw.i8ToF16 - round trip & byte length", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var src = [0.0, 0.25, -0.5, 0.75, -1.0, 1.0, -0.333, 0.111];
    var i8 = raw.numbersToI8(src, 1.0/127.0, 0);
    var f16 = raw.i8ToF16(i8, 1.0/127.0, 0);
    t.mustEq(f16.length, src.length*2, "f16 buffer is 2 bytes/elem");
    var back = raw.f16ToNumbers(f16);
    for (var i=0;i<src.length;i++)
        t.must(closeTo(back[i], src[i], 3e-2), "i8ToF16 elem "+i+" "+back[i]+" vs "+src[i]);
    return true;
});

t("raw.u8ToF16 - round trip & byte length", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var src = [0.0, 0.25, 0.5, 0.75, 1.0, 0.1, 0.9, 0.333];
    var u8 = raw.numbersToU8(src, 1.0/255.0, 0);
    var f16 = raw.u8ToF16(u8, 1.0/255.0, 0);
    t.mustEq(f16.length, src.length*2, "f16 buffer is 2 bytes/elem");
    var back = raw.f16ToNumbers(f16);
    for (var i=0;i<src.length;i++)
        t.must(closeTo(back[i], src[i], 2e-2), "u8ToF16 elem "+i+" "+back[i]+" vs "+src[i]);
    return true;
});

/* ========================================================================
 * L2 NORMALIZATION (all in-place, return the input, result is unit length)
 * ====================================================================== */

// All l2Normalize* functions normalize IN PLACE and return the input
// vector/buffer (per the documented "transforms the input and returns it").
t("raw.l2NormalizeNumbers - in-place, unit length, returns input", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var arr = [3, 0, 4, 0]; // magnitude 5
    var res = raw.l2NormalizeNumbers(arr);
    t.must(res === arr, "returns the (same) input array");
    // in-place: the input array now holds the normalized values
    t.mustEq(arr.length, 4, "length preserved");
    var ss = 0;
    for (var i=0;i<arr.length;i++) ss += arr[i]*arr[i];
    t.must(closeTo(ss, 1.0, 1e-9), "sum of squares ~1, got "+ss);
    t.must(closeTo(arr[0], 0.6, 1e-9) && closeTo(arr[2], 0.8, 1e-9), "3,4 -> 0.6,0.8");
    return true;
});

t("raw.l2NormalizeF64 - in-place, unit length", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var buf = raw.numbersToF64([1, 2, 2, 4]); // magnitude 5
    var blen = buf.length;
    var res = raw.l2NormalizeF64(buf);
    t.must(res === buf, "returns the (same) input buffer");
    t.mustEq(buf.length, blen, "buffer byte length preserved (in-place)");
    var back = raw.f64ToNumbers(buf);
    var ss = 0;
    for (var i=0;i<back.length;i++) ss += back[i]*back[i];
    t.must(closeTo(ss, 1.0, 1e-12), "sum of squares ~1, got "+ss);
    return true;
});

t("raw.l2NormalizeF32 - in-place, unit length", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var buf = raw.numbersToF32([1, 2, 2, 4]);
    var blen = buf.length;
    var res = raw.l2NormalizeF32(buf);
    t.must(res === buf, "returns the (same) input buffer");
    t.mustEq(buf.length, blen, "buffer byte length preserved (in-place)");
    var back = raw.f32ToNumbers(buf);
    var ss = 0;
    for (var i=0;i<back.length;i++) ss += back[i]*back[i];
    t.must(closeTo(ss, 1.0, 1e-5), "sum of squares ~1, got "+ss);
    return true;
});

t("raw.l2NormalizeF16 - in-place, unit length", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var buf = raw.numbersToF16([1, 2, 2, 4]);
    var blen = buf.length;
    var res = raw.l2NormalizeF16(buf);
    t.must(res === buf, "returns the (same) input buffer");
    t.mustEq(buf.length, blen, "buffer byte length preserved (in-place)");
    var back = raw.f16ToNumbers(buf);
    var ss = 0;
    for (var i=0;i<back.length;i++) ss += back[i]*back[i];
    t.must(closeTo(ss, 1.0, 2e-2), "sum of squares ~1 (f16 lossy), got "+ss);
    return true;
});

/* ========================================================================
 * RAW VECTOR DISTANCE FUNCTION
 *   rampart.vector.raw.distance(v1, v2 [, metric [, vecType]])
 *   metric: dot|cosine|euclidean (default dot)
 *   vecType: numbers|f64|f32|f16|bf16|i8|u8 (default f16)
 * ====================================================================== */

t("raw.distance - numbers: dot/cosine/euclidean properties", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var a = [0.6, 0.8, 0, 0];          // already unit length
    var aOpp = [-0.6, -0.8, 0, 0];
    raw.l2NormalizeNumbers(a);
    raw.l2NormalizeNumbers(aOpp);
    var dEq  = raw.distance(a, a,   'dot', 'numbers');
    var dOpp = raw.distance(a, aOpp,'dot', 'numbers');
    t.must(closeTo(dEq, 1.0, 1e-9), "dot(equal) ~1, got "+dEq);
    t.must(closeTo(dOpp, -1.0, 1e-9), "dot(opposite) ~-1, got "+dOpp);
    var eEq = raw.distance(a, a, 'euclidean', 'numbers');
    t.must(closeTo(eEq, 0.0, 1e-9), "euclidean(equal) ~0, got "+eEq);
    var cEq = raw.distance(a, a, 'cosine', 'numbers');
    t.must(closeTo(cEq, 0.0, 1e-9), "cosine(equal) ~0, got "+cEq);
    // 1 - cosineScore == dotDistance for L2-normalized inputs
    var b = [0.1, 0.5, 0.2, 0.83]; raw.l2NormalizeNumbers(b);
    var dab = raw.distance(a, b, 'dot', 'numbers');
    var cab = raw.distance(a, b, 'cosine', 'numbers');
    t.must(closeTo(1.0 - cab, dab, 1e-6), "1-cosine == dot, "+(1-cab)+" vs "+dab);
    return true;
});

t("raw.distance - f64: dot/cosine/euclidean properties", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var an = [0.6, 0.8, 0, 0]; raw.l2NormalizeNumbers(an);
    var anOpp = an.map(function(x){return -x;});
    var v = raw.numbersToF64(an), vOpp = raw.numbersToF64(anOpp);
    t.must(closeTo(raw.distance(v, v, 'dot', 'f64'), 1.0, 1e-9), "dot(equal)~1");
    t.must(closeTo(raw.distance(v, vOpp, 'dot', 'f64'), -1.0, 1e-9), "dot(opposite)~-1");
    t.must(closeTo(raw.distance(v, v, 'euclidean', 'f64'), 0.0, 1e-9), "euclidean(equal)~0");
    t.must(closeTo(raw.distance(v, v, 'cosine', 'f64'), 0.0, 1e-9), "cosine(equal)~0");
    return true;
});

t("raw.distance - f32: dot/cosine/euclidean properties", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var an = [0.6, 0.8, 0, 0]; raw.l2NormalizeNumbers(an);
    var anOpp = an.map(function(x){return -x;});
    var v = raw.numbersToF32(an), vOpp = raw.numbersToF32(anOpp);
    t.must(closeTo(raw.distance(v, v, 'dot', 'f32'), 1.0, 1e-5), "dot(equal)~1");
    t.must(closeTo(raw.distance(v, vOpp, 'dot', 'f32'), -1.0, 1e-5), "dot(opposite)~-1");
    t.must(closeTo(raw.distance(v, v, 'euclidean', 'f32'), 0.0, 1e-5), "euclidean(equal)~0");
    t.must(closeTo(raw.distance(v, v, 'cosine', 'f32'), 0.0, 1e-5), "cosine(equal)~0");
    return true;
});

t("raw.distance - f16: dot/cosine/euclidean properties", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var an = [0.6, 0.8, 0, 0]; raw.l2NormalizeNumbers(an);
    var anOpp = an.map(function(x){return -x;});
    var v = raw.numbersToF16(an), vOpp = raw.numbersToF16(anOpp);
    t.must(closeTo(raw.distance(v, v, 'dot', 'f16'), 1.0, 2e-2), "dot(equal)~1");
    t.must(closeTo(raw.distance(v, vOpp, 'dot', 'f16'), -1.0, 2e-2), "dot(opposite)~-1");
    t.must(closeTo(raw.distance(v, v, 'euclidean', 'f16'), 0.0, 2e-2), "euclidean(equal)~0");
    t.must(closeTo(raw.distance(v, v, 'cosine', 'f16'), 0.0, 2e-2), "cosine(equal)~0");
    return true;
});

t("raw.distance - bf16: dot/cosine/euclidean properties", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var an = [0.6, 0.8, 0, 0]; raw.l2NormalizeNumbers(an);
    var anOpp = an.map(function(x){return -x;});
    var v = raw.numbersToBf16(an), vOpp = raw.numbersToBf16(anOpp);
    t.must(closeTo(raw.distance(v, v, 'dot', 'bf16'), 1.0, 5e-2), "dot(equal)~1");
    t.must(closeTo(raw.distance(v, vOpp, 'dot', 'bf16'), -1.0, 5e-2), "dot(opposite)~-1");
    t.must(closeTo(raw.distance(v, v, 'euclidean', 'bf16'), 0.0, 5e-2), "euclidean(equal)~0");
    t.must(closeTo(raw.distance(v, v, 'cosine', 'bf16'), 0.0, 5e-2), "cosine(equal)~0");
    return true;
});

t("raw.distance - i8: dot/cosine/euclidean properties (quantized)", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var an = [0.6, 0.8, 0, 0]; raw.l2NormalizeNumbers(an);
    var anOpp = an.map(function(x){return -x;});
    var v = raw.numbersToI8(an), vOpp = raw.numbersToI8(anOpp);
    // For i8/u8, 'dot' returns the scale-invariant normalized similarity
    // (== 1 - cosine_distance), giving the SAME -1..1 range as float 'dot' on
    // normalized vectors -- so f32/f16 -> i8 is a drop-in for scoring.
    t.must(closeTo(raw.distance(v, v, 'dot', 'i8'), 1.0, 2e-2), "i8 dot(equal)~1");
    t.must(closeTo(raw.distance(v, vOpp, 'dot', 'i8'), -1.0, 2e-2), "i8 dot(opposite)~-1");
    // cosine/euclidean normalize internally, so documented properties hold:
    t.must(closeTo(raw.distance(v, v, 'euclidean', 'i8'), 0.0, 5e-2), "euclidean(equal)~0");
    t.must(closeTo(raw.distance(v, v, 'cosine', 'i8'), 0.0, 5e-2), "cosine(equal)~0");
    t.must(closeTo(raw.distance(v, vOpp, 'cosine', 'i8'), 2.0, 5e-2), "cosine(opposite)~2");
    // dot similarity == 1 - cosine distance for i8
    t.must(closeTo(raw.distance(v, vOpp, 'dot', 'i8'),
                   1.0 - raw.distance(v, vOpp, 'cosine', 'i8'), 2e-2),
           "i8 dot == 1 - cosine");
    return true;
});

t("raw.distance - u8: dot/cosine/euclidean properties (quantized)", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    // u8 holds non-negative values; use a positive unit-length vector
    var an = [0.6, 0.8, 0, 0]; raw.l2NormalizeNumbers(an);
    var v = raw.numbersToU8(an);
    // u8 'dot' is the normalized similarity too (-1..1), so equal vectors ~1.
    t.must(closeTo(raw.distance(v, v, 'dot', 'u8'), 1.0, 2e-2), "u8 dot(equal)~1");
    // cosine/euclidean normalize internally => documented properties hold:
    t.must(closeTo(raw.distance(v, v, 'cosine', 'u8'), 0.0, 5e-2), "cosine(equal)~0");
    t.must(closeTo(raw.distance(v, v, 'euclidean', 'u8'), 0.0, 5e-2), "euclidean(equal)~0");
    return true;
});

t("raw.distance - DEFAULTS: metric=dot, vecType=f16", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var an = [0.6, 0.8, 0, 0]; raw.l2NormalizeNumbers(an);
    var anOpp = an.map(function(x){return -x;});
    var v = raw.numbersToF16(an), vOpp = raw.numbersToF16(anOpp);
    // no metric, no vecType: should behave as dot on f16
    var dDefault = raw.distance(v, v);
    var dExplicit = raw.distance(v, v, 'dot', 'f16');
    t.must(closeTo(dDefault, dExplicit, 1e-6), "default == dot/f16, "+dDefault+" vs "+dExplicit);
    t.must(closeTo(dDefault, 1.0, 2e-2), "default dot(equal)~1, got "+dDefault);
    var dOppDefault = raw.distance(v, vOpp);
    t.must(closeTo(dOppDefault, -1.0, 2e-2), "default dot(opposite)~-1, got "+dOppDefault);
    // metric only (vecType defaults to f16)
    var cDefaultType = raw.distance(v, v, 'cosine');
    t.must(closeTo(cDefaultType, 0.0, 2e-2), "cosine w/ default vecType f16 ~0, got "+cDefaultType);
    return true;
});

/* ##################### binary quantization (b8) ##################### */

t("raw.*ToBit - byte length and sign-bit packing", function(){
    var raw = rampart.vector.raw;
    // signs of [+,-,+,-,+,-,+,-] -> bits 0,2,4,6 set -> 0x55
    var f = [1,-1,0.5,-0.5,2,-2,0.1,-0.1];
    var b = raw.f32ToBit(raw.numbersToF32(f));
    t.mustEq(b.length, 1, "8 dims -> ceil(8/8)=1 byte");
    t.mustEq(rampart.utils.hexify(b), "55", "sign bits packed (0x55)");
    // numbersToBit matches f32ToBit (default cutoff 0)
    t.mustEq(rampart.utils.hexify(raw.numbersToBit(f)), "55", "numbersToBit default cutoff 0");
    // all float/int producers agree on the sign pattern
    t.mustEq(rampart.utils.hexify(raw.f64ToBit(raw.numbersToF64(f))), "55", "f64ToBit");
    t.mustEq(rampart.utils.hexify(raw.f16ToBit(raw.numbersToF16(f))), "55", "f16ToBit");
    t.mustEq(rampart.utils.hexify(raw.bf16ToBit(raw.numbersToBf16(f))), "55", "bf16ToBit");
    t.mustEq(rampart.utils.hexify(raw.i8ToBit(raw.numbersToI8(f, 1/127))), "55", "i8ToBit");
    return true;
});

t("raw.*ToBit - padding bits in final byte are zero", function(){
    var raw = rampart.vector.raw;
    var b = raw.numbersToBit([1,1,1]);   // 3 dims -> 1 byte, only low 3 bits meaningful
    t.mustEq(b.length, 1, "3 dims -> 1 byte");
    t.mustEq(rampart.utils.hexify(b), "07", "bits 0,1,2 set, padding zero (0x07)");
    var b2 = raw.numbersToBit([1,1,1,1,1,1,1,1,1]); // 9 dims -> 2 bytes
    t.mustEq(b2.length, 2, "9 dims -> 2 bytes");
    t.mustEq(rampart.utils.hexify(b2), "ff01", "8 + 1 bits set, rest padding zero");
    return true;
});

t("raw.u8ToBit - default cutoff 128, value 128 -> bit 0", function(){
    var raw = rampart.vector.raw;
    // raw u8 bytes via explicit scale=1, zeroPoint=0
    var u = raw.numbersToU8([200,50,255,0,130,127,128,129], 1, 0);
    // > 128 set: 200(b0),255(b2),130(b4),129(b7); 128 is NOT >128 -> bit5 clear
    t.mustEq(rampart.utils.hexify(raw.u8ToBit(u)), "95", "u8 default cutoff 128 (0x95)");
    return true;
});

t("raw.*ToBit - explicit cutoff overrides default", function(){
    var raw = rampart.utils, v = rampart.vector.raw;
    // cutoff 1.5: only values > 1.5 -> [2] at index 4 -> bit 4 -> 0x10
    var bit = v.numbersToBit([1,-1,0.5,-0.5,2,-2,0.1,-0.1], 1.5);
    t.mustEq(raw.hexify(bit), "10", "numbersToBit cutoff 1.5 -> only the 2.0 sets a bit");
    return true;
});

t("vector b8 - toBit() returns a b8 Vector Object", function(){
    var v = new rampart.vector("f32", [1,-1,0.5,-0.5,2,-2,0.1,-0.1]);
    var bv = v.toBit();
    t.mustEq(bv.type, "b8", "type is b8");
    t.mustEq(bv.dim, 8, "dim preserved (bit count)");
    t.mustEq(bv.byteLength(), 1, "byteLength = ceil(dim/8)");
    t.mustEq(bv.toNumbers(), [1,0,1,0,1,0,1,0], "toNumbers unpacks to 0/1 by sign");
    t.mustEq(rampart.utils.hexify(bv.toRaw()), "55", "toRaw is the packed buffer");
    return true;
});

t("vector b8 - toBit(cutoff) override", function(){
    var v = new rampart.vector("f32", [1,-1,0.5,-0.5,2,-2,0.1,-0.1]);
    t.mustEq(v.toBit(1.5).toNumbers(), [0,0,0,0,1,0,0,0], "toBit(1.5) only the 2.0 -> 1");
    return true;
});

t("vector b8 - copy is independent, distance defaults to hamming", function(){
    var a = new rampart.vector("f32", [1,1,1,1,-1,-1,-1,-1]).toBit();
    var b = new rampart.vector("f32", [1,1,-1,-1,-1,-1,1,1]).toBit();
    // a bits: 1111 0000 ; b bits: 1100 0011 -> differ in 4 positions
    t.mustEq(a.distance(b), 4, "default metric is hamming -> 4 differing bits");
    t.mustEq(a.distance(b, "hamming"), 4, "explicit hamming -> 4");
    t.mustEq(a.distance(a), 0, "hamming(equal) -> 0");
    t.must(Math.abs(a.distance(b, "jaccard") - 0.6666667) < 1e-3, "jaccard ~0.667");
    var cp = a.copy();
    t.mustEq(cp.type, "b8", "copy type b8");
    t.mustEq(cp.dim, 8, "copy dim preserved");
    t.mustEq(cp.distance(a), 0, "copy equals original");
    return true;
});

t("vector b8 - constructors (bit array, b8 rawbuf, b8 ndim)", function(){
    // from an array of values (binarized by sign, optional cutoff)
    var fromArr = new rampart.vector("bit", [1,0,1,0,1,0,1,0]);
    t.mustEq(fromArr.dim, 8, "bit-array ctor dim");
    t.mustEq(fromArr.toNumbers(), [1,0,1,0,1,0,1,0], "bit-array ctor values");
    // from a raw packed buffer
    var fromRaw = new rampart.vector("b8", rampart.vector.raw.numbersToBit([1,0,1,0,1,0,1,0]));
    t.mustEq(fromRaw.dim, 8, "b8 rawbuf ctor dim (sz*8)");
    t.mustEq(fromRaw.toNumbers(), [1,0,1,0,1,0,1,0], "b8 rawbuf ctor values");
    // zero-filled by dim
    var zero = new rampart.vector("b8", 16);
    t.mustEq(zero.dim, 16, "b8 ndim ctor dim");
    t.mustEq(zero.byteLength(), 2, "b8 ndim ctor byteLength = ceil(16/8)");
    t.mustEq(zero.toNumbers(), [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0], "b8 ndim ctor zero-filled");
    return true;
});

/* ##################### b8 reconstruction (asymmetric) ##################### */

t("raw.bitToF* - reconstructs unit ±1/√D vectors", function(){
    var R = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var bits = [1,0,1,0,1,0,1,0];            // D=8 -> 1/√8 ≈ 0.353553
    var b8 = R.numbersToBit(bits);
    ["F64","F32","F16","Bf16"].forEach(function(ty){
        var n = R[ (ty==="F64"?"f64":ty==="F32"?"f32":ty==="F16"?"f16":"bf16") + "ToNumbers" ](R["bitTo"+ty](b8));
        var tol = (ty==="F64"||ty==="F32") ? 1e-4 : (ty==="F16"?2e-2:1e-1);
        var ss = 0; for (var i=0;i<n.length;i++) ss += n[i]*n[i];
        t.must(closeTo(ss, 1.0, 2e-2), ty+" reconstruction is unit length (ss="+ss+")");
        t.must(closeTo(Math.abs(n[0]), 1/Math.sqrt(8), tol), ty+" value magnitude ≈ 1/√D");
        t.must(n[0] > 0 && n[1] < 0, ty+" bit 1 -> +, bit 0 -> -");
    });
    return true;
});

t("raw.bitToI8 - ±127 sign reconstruction (+ explicit zeroPoint)", function(){
    var R = rampart.vector.raw;
    var b8 = R.numbersToBit([1,0,1,0,1,0,1,0]);
    var i8 = R.bitToI8(b8);                   // default zeroPoint 0
    t.mustEq(i8.length, 8, "1 byte b8 -> 8 i8 bytes");
    t.mustEq(R.i8ToNumbers(i8, 1), [127,-127,127,-127,127,-127,127,-127], "±127 at zeroPoint 0");
    // explicit zeroPoint shifts both: +127+10 clamps to 127, -127+10 = -117
    t.mustEq(R.i8ToNumbers(R.bitToI8(b8, 10), 1),
        [127,-117,127,-117,127,-117,127,-117], "bitToI8(b8,10) shifts by zeroPoint (clamped)");
    return true;
});

t("raw.bitToU8 - zeroPoint default 127 -> {0,254}", function(){
    var R = rampart.vector.raw;
    var b8 = R.numbersToBit([1,0,1,0,1,0,1,0]);
    t.mustEq(rampart.utils.hexify(R.bitToU8(b8)), "fe00fe00fe00fe00", "default zp127 -> 254/0");
    // custom zeroPoint 128 -> {1,255}
    t.mustEq(rampart.utils.hexify(R.bitToU8(b8, 128)), "ff01ff01ff01ff01", "zp128 -> 255/1");
    return true;
});

t("raw.u8ToI8 - rebase undoes the zeroPoint offset", function(){
    var R = rampart.vector.raw;
    var b8 = R.numbersToBit([1,0,1,0,1,0,1,0]);
    var u8 = R.bitToU8(b8);                    // {254,0} at zp127
    t.mustEq(R.i8ToNumbers(R.u8ToI8(u8, 127), 1),
        [127,-127,127,-127,127,-127,127,-127], "u8(zp127)-127 = ±127");
    // default zeroPoint is 127 -> same result with no arg
    t.mustEq(R.i8ToNumbers(R.u8ToI8(u8), 1),
        [127,-127,127,-127,127,-127,127,-127], "u8ToI8 default zeroPoint == 127");
    // a different zeroPoint shifts accordingly: 254-200=54, 0-200 clamps to -128
    t.mustEq(R.i8ToNumbers(R.u8ToI8(u8, 200), 1),
        [54,-128,54,-128,54,-128,54,-128], "u8ToI8(u8,200) shifts and clamps low end to -128");
    return true;
});

t("vector b8 - typed reconstruction methods (all six types)", function(){
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var v = new rampart.vector("b8", [1,0,1,0,1,0,1,0]);
    // float targets: correct type, dim preserved, unit length, ±1/√D sign
    [["toF64",1e-4],["toF32",1e-4],["toF16",2e-2],["toBf16",1e-1]].forEach(function(p){
        var m = p[0], tol = p[1], exp = m.slice(2).toLowerCase();
        var o = v[m]();
        t.mustEq(o.type, exp, "b8."+m+"() -> "+exp+" object");
        t.mustEq(o.dim, 8, "b8."+m+"() dim preserved");
        var n = o.toNumbers(), ss = 0; for (var i=0;i<n.length;i++) ss += n[i]*n[i];
        t.must(closeTo(ss, 1.0, 3e-2), "b8."+m+"() unit length (ss="+ss+")");
        t.must(closeTo(Math.abs(n[0]), 1/Math.sqrt(8), tol*10), "b8."+m+"() |comp| ≈ 1/√D");
        t.must(n[0] > 0 && n[1] < 0, "b8."+m+"() bit 1 -> +, bit 0 -> -");
    });
    // integer targets: default zeroPoints (i8=0, u8=127)
    t.mustEq(v.toI8().type, "i8", "b8.toI8() -> i8 object");
    t.mustEq(v.toI8().dim, 8, "b8.toI8() dim preserved");
    t.mustEq(v.toI8().toNumbers(), [1,-1,1,-1,1,-1,1,-1], "b8.toI8() dequantizes to ±1");
    t.mustEq(v.toU8().type, "u8", "b8.toU8() -> u8 object");
    t.mustEq(v.toU8().dim, 8, "b8.toU8() dim preserved");
    t.mustEq(rampart.utils.hexify(v.toU8().toRaw()), "fe00fe00fe00fe00", "b8.toU8() default zp127 -> {254,0}");
    return true;
});

t("vector b8 - typed toI8/toU8 explicit zeroPoint", function(){
    var v = new rampart.vector("b8", [1,0,1,0,1,0,1,0]);
    // raw bytes (via toRaw) make the zeroPoint shift unambiguous
    t.mustEq(rampart.utils.hexify(v.toI8(10).toRaw()), "7f8b7f8b7f8b7f8b",
        "b8.toI8(10): +127 clamps to 0x7f, -127+10=-117=0x8b");
    t.mustEq(rampart.utils.hexify(v.toU8(128).toRaw()), "ff01ff01ff01ff01",
        "b8.toU8(128) -> {255,1}");
    return true;
});

t("vector u8 - toI8 rebase method (+ explicit zeroPoint)", function(){
    // construct from a raw buffer so the bytes are exactly {254,0} (the
    // number-array ctor would normalize max->255).
    var R = rampart.vector.raw;
    var u = new rampart.vector("u8", R.bitToU8(R.numbersToBit([1,0,1,0,1,0,1,0])));
    t.mustEq(u.toI8().type, "i8", "u8.toI8() -> i8 object");
    t.mustEq(u.toI8().dim, 8, "u8.toI8() dim preserved");
    // default zeroPoint 127: 254-127=127, 0-127 clamps to -127
    t.mustEq(rampart.utils.hexify(u.toI8().toRaw()), "7f817f817f817f81",
        "u8.toI8() default zp127 -> {127,-127}");
    // explicit zeroPoint matches default here
    t.mustEq(u.toI8(127).toRaw(), u.toI8().toRaw(), "u8.toI8(127) == u8.toI8()");
    // b8 -> u8 -> i8 should equal b8 -> i8
    var b8 = new rampart.vector("b8", [1,0,1,1,0,0,1,0]);
    t.mustEq(b8.toU8().toI8().toNumbers(), b8.toI8().toNumbers(), "b8->u8->i8 == b8->i8");
    return true;
});

t("vector u8 - cosine/dot honor zeroPoint (u8 'just works')", function(){
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    // The distance kernel rebases u8 by the symmetric zeroPoint 128, so
    // angle-based metrics on u8 match the f32 reference WITHOUT a manual
    // u8->i8 rebase.  (Before the fix, u8 cosine was off by ~0.7.)
    var A = new rampart.vector("f32",[0.6,-0.2,0.5,-0.3,0.4,-0.1,0.7,-0.2]); A.l2Normalize();
    var B = new rampart.vector("f32",[0.1,0.5,-0.4,0.3,-0.6,0.2,0.3,-0.5]); B.l2Normalize();
    var refCos = A.distance(B,"cosine"), refDot = A.distance(B,"dot");
    // dim=8 -> ~1% quantization noise; tolerance proves the fix vs the old ~0.7 error
    t.must(closeTo(A.toU8().distance(B.toU8(),"cosine"), refCos, 2.5e-2),
        "u8 cosine matches f32 ref without manual rebase");
    t.must(closeTo(A.toU8().distance(B.toU8(),"dot"), refDot, 2.5e-2),
        "u8 dot matches f32 ref without manual rebase");
    t.must(closeTo(A.toU8().distance(B.toU8(),"cosine"), A.toI8().distance(B.toI8(),"cosine"), 2.5e-2),
        "u8 cosine agrees with i8 cosine");
    // L2 is translation-invariant, so the rebase must NOT change it: u8 L2
    // equals the native u8-kernel result either way (here: just self-consistency)
    t.must(A.toU8().distance(B.toU8(),"euclidean") > 0, "u8 euclidean still computes");
    return true;
});

t("vector b8 - asymmetric scoring (f32 query vs b8 doc)", function(){
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    var q = new rampart.vector("f32", [1,0.2,1,0.1,1,0.3,1,0.2]); q.l2Normalize();
    var doc = new rampart.vector("b8", [1,0,1,0,1,0,1,1]);
    var rec = doc.toF32();   // unit ±1/√D
    var dot = q.distance(rec, "dot");
    var cos = q.distance(rec, "cosine");
    t.must(dot > 0 && dot <= 1.0001, "asymmetric dot in (0,1]");
    t.must(closeTo(1 - cos, dot, 1e-3), "1 - cosine ≈ dot (both unit length)");
    return true;
});

t("raw.distance - euclidean is true L2; l2sq/sqeuclidean are squared", function(){
    var raw = rampart.vector.raw;
    function closeTo(a,b,tol){ return Math.abs(a-b) <= tol; }
    // |[0.6,0.8,0,0] - [0.8,0.6,0,0]| = sqrt(0.08) ~ 0.2828427
    var a = raw.numbersToF32([0.6,0.8,0,0]), b = raw.numbersToF32([0.8,0.6,0,0]);
    var eu = raw.distance(a, b, "euclidean", "f32");
    var sq = raw.distance(a, b, "l2sq", "f32");
    t.must(closeTo(eu, 0.2828427, 1e-4), "euclidean = true L2 (sqrt), got "+eu);
    t.must(closeTo(sq, 0.08, 1e-4), "l2sq = squared L2, got "+sq);
    t.must(closeTo(eu*eu, sq, 1e-4), "euclidean^2 == l2sq");
    t.mustEq(raw.distance(a, b, "l2", "f32"), eu, "'l2' alias == euclidean (true L2)");
    t.mustEq(raw.distance(a, b, "sqeuclidean", "f32"), sq, "'sqeuclidean' alias == l2sq");
    t.must(closeTo(raw.distance(a, a, "euclidean", "f32"), 0, 1e-6), "euclidean(equal) ~ 0");
    return true;
});

t.exit();
