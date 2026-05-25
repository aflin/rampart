/* Extended rampart-crypto coverage.  Originally created for the
 * Tier 1 / Tier 2 additions in crypto-todo.md (timingSafeEqual,
 * pbkdf2, hkdf, EC family, AES-GCM, AES-KW, RSA-PSS, RSA-OAEP),
 * then expanded to include Tier 3 (X25519/Ed25519/scrypt),
 * published KATs (NIST/RFC test vectors), and coverage-gap fills
 * for older functions (e.g. rsa_import_priv_key matrix).
 *
 * Modeled after crypto-test.js — same harness, separate file so
 * the larger surface doesn't bloat the original.
 */

rampart.globalize(rampart.utils);

var crypto = require("rampart-crypto");

var tmpdir = process.scriptPath + '/tmp-test';
if (!stat(tmpdir)) mkdir(tmpdir);

function cleanup() {}

var testFeature = new (require('./test-feature.js'))({prefix: "crypto2"});

/* rampart-crypto convention: byte-producing functions return a plain
 * duktape buffer (shows as Uint8Array, not Node Buffer).  Use this
 * single check whether we got bytes back. */
function _isU8(x) { return x instanceof Uint8Array; }


/* ---------------------------------------------------------------
 * 1.3  timingSafeEqual
 * --------------------------------------------------------------- */

testFeature("timingSafeEqual equal buffers → true",
    crypto.timingSafeEqual(Buffer.from("hello"), Buffer.from("hello")) === true);

testFeature("timingSafeEqual differing buffers → false",
    crypto.timingSafeEqual(Buffer.from("hello"), Buffer.from("hellx")) === false);

testFeature("timingSafeEqual string vs buffer (same bytes)",
    crypto.timingSafeEqual("abcdef", Buffer.from("abcdef")) === true);

testFeature("timingSafeEqual mismatched length throws", function() {
    try { crypto.timingSafeEqual(Buffer.from("abc"), Buffer.from("abcd")); return false; }
    catch (e) { return /same length/i.test(e.message); }
});


/* ---------------------------------------------------------------
 * 1.2  PBKDF2
 *
 * RFC 6070 test vector #2:
 *   pass="password" salt="salt" iter=2 hash=sha1 length=20
 *   → ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957
 * --------------------------------------------------------------- */

testFeature("pbkdf2 sha1 RFC 6070 vec #2", function() {
    var d = crypto.pbkdf2({
        pass: "password",
        salt: Buffer.from("salt"),
        iter: 2,
        length: 20,
        hash: "sha1",
        returnType: "hex"
    });
    return d === "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957";
});

testFeature("pbkdf2 sha256 length=32 deterministic", function() {
    var a = crypto.pbkdf2({pass:"pw", salt:Buffer.from("s"), iter:1000, length:32, hash:"sha256", returnType:"hex"});
    var b = crypto.pbkdf2({pass:"pw", salt:Buffer.from("s"), iter:1000, length:32, hash:"sha256", returnType:"hex"});
    return a === b && a.length === 64;
});

testFeature("pbkdf2 sha512 default returnType is Uint8Array", function() {
    var d = crypto.pbkdf2({pass:"x", salt:Buffer.from("y"), iter:100, length:64, hash:"sha512"});
    return _isU8(d) && d.length === 64;
});

testFeature("pbkdf2 returnType:buffer yields Node Buffer", function() {
    var d = crypto.pbkdf2({pass:"x", salt:Buffer.from("y"), iter:100, length:16, hash:"sha256", returnType:"buffer"});
    return Buffer.isBuffer(d) && d.length === 16;
});

testFeature("pbkdf2 unknown hash throws", function() {
    try { crypto.pbkdf2({pass:"a", salt:Buffer.from("b"), iter:100, length:16, hash:"bogus"}); return false; }
    catch (e) { return /unsupported hash/i.test(e.message); }
});


/* ---------------------------------------------------------------
 * 2.2  HKDF
 *
 * RFC 5869 Test Case 1:
 *   IKM   = 0x0b * 22
 *   salt  = 0x000102030405060708090a0b0c
 *   info  = 0xf0f1f2f3f4f5f6f7f8f9
 *   L     = 42
 *   hash  = sha256
 *   OKM   = 3cb25f25faacd57a90434f64d0362f2a
 *           2d2d0a90cf1a5a4c5db02d56ecc4c5bf
 *           34007208d5b887185865
 * --------------------------------------------------------------- */

testFeature("hkdf sha256 RFC 5869 Test Case 1", function() {
    var ikm  = Buffer.alloc(22, 0x0b);
    var salt = Buffer.from([0,1,2,3,4,5,6,7,8,9,10,11,12]);
    var info = Buffer.from([0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9]);
    var okm = crypto.hkdf({ikm:ikm, salt:salt, info:info, length:42, hash:"sha256", returnType:"hex"});
    return okm === "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865";
});

testFeature("hkdf default returnType is Uint8Array", function() {
    var d = crypto.hkdf({ikm: Buffer.from("ikm"), info: Buffer.from("info"), length: 32, hash: "sha256"});
    return _isU8(d) && d.length === 32;
});

testFeature("hkdf returnType:buffer yields Node Buffer", function() {
    var d = crypto.hkdf({ikm: Buffer.from("ikm"), length: 32, hash: "sha256", returnType:"buffer"});
    return Buffer.isBuffer(d) && d.length === 32;
});

testFeature("hkdf no salt + no info still works", function() {
    var d = crypto.hkdf({ikm: Buffer.from("ikm"), length: 32, hash: "sha256", returnType:"hex"});
    return typeof d === "string" && d.length === 64;
});


/* ---------------------------------------------------------------
 * 1.1  AES-GCM (with tag append / AAD)
 * --------------------------------------------------------------- */

testFeature("aes-256-gcm encrypt/decrypt round-trip", function() {
    var key = crypto.rand(32, {returnType:"buffer"});
    var iv  = crypto.rand(12, {returnType:"buffer"});
    var pt  = Buffer.from("Some plaintext to round-trip through AES-GCM.");
    var ct  = crypto.encrypt({cipher:"aes-256-gcm", key:key, iv:iv, data:pt});
    var dt  = crypto.decrypt({cipher:"aes-256-gcm", key:key, iv:iv, data:ct});
    return bufferToString(dt) === bufferToString(pt);
});

testFeature("aes-256-gcm output = ciphertext + 16-byte tag", function() {
    var key = crypto.rand(32, {returnType:"buffer"});
    var iv  = crypto.rand(12, {returnType:"buffer"});
    var pt  = Buffer.from("12345678");           /* 8 bytes */
    var ct  = crypto.encrypt({cipher:"aes-256-gcm", key:key, iv:iv, data:pt});
    /* GCM is stream-mode: ciphertext = plaintext length; +16 tag at end */
    return ct.length === pt.length + 16;
});

testFeature("aes-256-gcm with AAD round-trip", function() {
    var key = crypto.rand(32, {returnType:"buffer"});
    var iv  = crypto.rand(12, {returnType:"buffer"});
    var aad = Buffer.from("authenticated metadata");
    var pt  = Buffer.from("plaintext");
    var ct  = crypto.encrypt({cipher:"aes-256-gcm", key:key, iv:iv, aad:aad, data:pt});
    var dt  = crypto.decrypt({cipher:"aes-256-gcm", key:key, iv:iv, aad:aad, data:ct});
    return bufferToString(dt) === "plaintext";
});

testFeature("aes-256-gcm AAD mismatch → throws (auth fail)", function() {
    var key = crypto.rand(32, {returnType:"buffer"});
    var iv  = crypto.rand(12, {returnType:"buffer"});
    var ct  = crypto.encrypt({cipher:"aes-256-gcm", key:key, iv:iv,
                              aad:Buffer.from("original"), data:Buffer.from("hi")});
    try {
        crypto.decrypt({cipher:"aes-256-gcm", key:key, iv:iv,
                        aad:Buffer.from("tampered"), data:ct});
        return false;
    } catch (e) {
        return /authentication tag verification failed/i.test(e.message);
    }
});

testFeature("aes-256-gcm tampered ciphertext → throws", function() {
    var key = crypto.rand(32, {returnType:"buffer"});
    var iv  = crypto.rand(12, {returnType:"buffer"});
    var ct  = crypto.encrypt({cipher:"aes-256-gcm", key:key, iv:iv, data:Buffer.from("abcdefgh")});
    ct[0] ^= 1;  /* flip a bit */
    try {
        crypto.decrypt({cipher:"aes-256-gcm", key:key, iv:iv, data:ct});
        return false;
    } catch (e) {
        return /tag verification/i.test(e.message);
    }
});

testFeature("aes-256-gcm tagLength=12 round-trip", function() {
    var key = crypto.rand(32, {returnType:"buffer"});
    var iv  = crypto.rand(12, {returnType:"buffer"});
    var pt  = Buffer.from("hello");
    var ct  = crypto.encrypt({cipher:"aes-256-gcm", key:key, iv:iv, data:pt, tagLength:12});
    if (ct.length !== pt.length + 12) return false;
    var dt  = crypto.decrypt({cipher:"aes-256-gcm", key:key, iv:iv, data:ct, tagLength:12});
    return bufferToString(dt) === "hello";
});

testFeature("aes-256-gcm password round-trip", function() {
    /* Password mode: IV is derived from pass+salt internally (no user
     * iv).  Output prefix is "Salted__" + 8-byte salt, then ciphertext,
     * then 16-byte tag.  Decrypt reads the prefix to re-derive the IV. */
    var pt  = Buffer.from("password-encrypted GCM message");
    var ct  = crypto.encrypt({cipher:"aes-256-gcm", pass:"secret", data:pt});
    var dt  = crypto.decrypt({cipher:"aes-256-gcm", pass:"secret", data:ct});
    return bufferToString(dt) === bufferToString(pt);
});

testFeature("aes-128-gcm round-trip", function() {
    var key = crypto.rand(16, {returnType:"buffer"});
    var iv  = crypto.rand(12, {returnType:"buffer"});
    var pt  = Buffer.from("128-bit key test");
    var ct  = crypto.encrypt({cipher:"aes-128-gcm", key:key, iv:iv, data:pt});
    var dt  = crypto.decrypt({cipher:"aes-128-gcm", key:key, iv:iv, data:ct});
    return bufferToString(dt) === bufferToString(pt);
});


/* ---------------------------------------------------------------
 * ChaCha20-Poly1305 (RFC 8439)
 * --------------------------------------------------------------- */

testFeature("chacha20-poly1305 round-trip", function() {
    var key = crypto.rand(32);
    var nonce = crypto.rand(12);
    var pt = Buffer.from("ChaCha20-Poly1305 interop plaintext");
    var ct = crypto.encrypt({cipher:"chacha20-poly1305", key:key, iv:nonce, data:pt});
    if (ct.length !== pt.length + 16) return "tag length wrong";
    var dt = crypto.decrypt({cipher:"chacha20-poly1305", key:key, iv:nonce, data:ct});
    return bufferToString(dt) === bufferToString(pt);
});

testFeature("chacha20-poly1305 with AAD round-trip", function() {
    var key = crypto.rand(32);
    var nonce = crypto.rand(12);
    var aad = "authenticated metadata";
    var pt = Buffer.from("hello");
    var ct = crypto.encrypt({cipher:"chacha20-poly1305", key:key, iv:nonce, aad:aad, data:pt});
    var dt = crypto.decrypt({cipher:"chacha20-poly1305", key:key, iv:nonce, aad:aad, data:ct});
    return bufferToString(dt) === "hello";
});

testFeature("chacha20-poly1305 AAD mismatch → throws", function() {
    var key = crypto.rand(32);
    var nonce = crypto.rand(12);
    var ct = crypto.encrypt({cipher:"chacha20-poly1305", key:key, iv:nonce,
                             aad:Buffer.from("original"), data:Buffer.from("hi")});
    try {
        crypto.decrypt({cipher:"chacha20-poly1305", key:key, iv:nonce,
                        aad:Buffer.from("tampered"), data:ct});
        return false;
    } catch(e) { return /tag verification/i.test(e.message); }
});

testFeature("chacha20-poly1305 RFC 8439 §2.8.2 KAT", function() {
    var key   = dehexify("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
    var nonce = dehexify("070000004041424344454647");
    var aad   = dehexify("50515253c0c1c2c3c4c5c6c7");
    var pt    = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    var ct    = crypto.encrypt({cipher:"chacha20-poly1305", key:key, iv:nonce, aad:aad, data:pt});
    var want  = "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
              + "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
              + "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
              + "3ff4def08e4b7a9de576d26586cec64b6116"
              + "1ae10b594f09e26a7e902ecbd0600691";   /* ciphertext + tag */
    return hexify(ct) === want;
});


/* ---------------------------------------------------------------
 * AES-OCB (RFC 7253) — verify our generic AEAD path handles it
 * --------------------------------------------------------------- */

testFeature("aes-256-ocb round-trip", function() {
    var key = crypto.rand(32);
    var iv  = crypto.rand(12);
    var pt  = Buffer.from("AES-OCB interop plaintext");
    var ct = crypto.encrypt({cipher:"aes-256-ocb", key:key, iv:iv, data:pt});
    if (ct.length !== pt.length + 16) return "tag length wrong";
    var dt = crypto.decrypt({cipher:"aes-256-ocb", key:key, iv:iv, data:ct});
    return bufferToString(dt) === bufferToString(pt);
});

testFeature("aes-256-ocb with AAD round-trip", function() {
    var key = crypto.rand(32);
    var iv  = crypto.rand(12);
    var aad = Buffer.from("header");
    var ct  = crypto.encrypt({cipher:"aes-256-ocb", key:key, iv:iv, aad:aad, data:"plain"});
    var dt  = crypto.decrypt({cipher:"aes-256-ocb", key:key, iv:iv, aad:aad, data:ct});
    return bufferToString(dt) === "plain";
});

testFeature("aes-128-ocb round-trip", function() {
    var key = crypto.rand(16);
    var iv  = crypto.rand(12);
    var ct = crypto.encrypt({cipher:"aes-128-ocb", key:key, iv:iv, data:"x"});
    var dt = crypto.decrypt({cipher:"aes-128-ocb", key:key, iv:iv, data:ct});
    return bufferToString(dt) === "x";
});


/* ---------------------------------------------------------------
 * 2.3  AES-KW (RFC 3394)
 *
 * RFC 3394 Test Vector 4.1 (128-bit KEK, 128-bit data):
 *   KEK    = 000102030405060708090a0b0c0d0e0f
 *   Data   = 00112233445566778899aabbccddeeff
 *   Output = 1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5
 * --------------------------------------------------------------- */

testFeature("aes-128-wrap RFC 3394 vector 4.1", function() {
    var kek  = Buffer.from("000102030405060708090a0b0c0d0e0f", "hex");
    var data = Buffer.from("00112233445566778899aabbccddeeff", "hex");
    var out  = crypto.encrypt({cipher:"aes-128-wrap", key:kek, data:data});
    return hexify(out) === "1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5";
});

testFeature("aes-256-wrap round-trip", function() {
    var kek  = crypto.rand(32, {returnType:"buffer"});
    var data = crypto.rand(32, {returnType:"buffer"});   /* must be multiple of 8, >= 16 */
    var wrapped   = crypto.encrypt({cipher:"aes-256-wrap", key:kek, data:data});
    var unwrapped = crypto.decrypt({cipher:"aes-256-wrap", key:kek, data:wrapped});
    /* Wrapped is data + 8 bytes integrity overhead. */
    if (wrapped.length !== data.length + 8) return false;
    return crypto.timingSafeEqual(unwrapped, data);
});

testFeature("aes-wrap with iv option throws", function() {
    var kek  = crypto.rand(32, {returnType:"buffer"});
    var data = crypto.rand(32, {returnType:"buffer"});
    try {
        crypto.encrypt({cipher:"aes-256-wrap", key:kek, iv:crypto.rand(16,{returnType:"buffer"}), data:data});
        return false;
    } catch (e) {
        return /does not accept 'iv'/.test(e.message);
    }
});


/* ---------------------------------------------------------------
 * 1.4  RSA-PSS
 * --------------------------------------------------------------- */

var rsaKey = null;
testFeature("rsa_gen_key 2048 (setup for PSS/OAEP tests)", function() {
    /* No password — keeps the sign/decrypt calls below short. */
    rsaKey = crypto.rsa_gen_key(2048, "");
    return rsaKey && rsaKey.private && rsaKey.public;
});

testFeature("rsa_sign / rsa_verify PSS round-trip", function() {
    if (!rsaKey) return false;
    var msg = Buffer.from("data to sign with PSS");
    var sig = crypto.rsa_sign(msg, rsaKey.private, {padding:"pss", hash:"sha256"});
    return crypto.rsa_verify(msg, rsaKey.public, sig, {padding:"pss", hash:"sha256"}) === true;
});

testFeature("rsa PSS sign + PKCS1 verify → false", function() {
    if (!rsaKey) return false;
    var msg = Buffer.from("padding mismatch test");
    var sig = crypto.rsa_sign(msg, rsaKey.private, {padding:"pss", hash:"sha256"});
    return crypto.rsa_verify(msg, rsaKey.public, sig) === false;
});

testFeature("rsa_verify PSS rejects tampered message", function() {
    if (!rsaKey) return false;
    var sig = crypto.rsa_sign(Buffer.from("original"), rsaKey.private, {padding:"pss", hash:"sha256"});
    return crypto.rsa_verify(Buffer.from("tampered"), rsaKey.public, sig, {padding:"pss", hash:"sha256"}) === false;
});

testFeature("rsa PSS with saltLength=0 round-trip", function() {
    if (!rsaKey) return false;
    var msg = Buffer.from("zero-salt PSS");
    var sig = crypto.rsa_sign(msg, rsaKey.private, {padding:"pss", hash:"sha256", saltLength:0});
    return crypto.rsa_verify(msg, rsaKey.public, sig, {padding:"pss", hash:"sha256", saltLength:0}) === true;
});

testFeature("rsa PSS sha384 round-trip", function() {
    if (!rsaKey) return false;
    var msg = Buffer.from("sha384 PSS test");
    var sig = crypto.rsa_sign(msg, rsaKey.private, {padding:"pss", hash:"sha384"});
    return crypto.rsa_verify(msg, rsaKey.public, sig, {padding:"pss", hash:"sha384"}) === true;
});


/* ---------------------------------------------------------------
 * 1.5  RSA-OAEP
 * --------------------------------------------------------------- */

testFeature("rsa OAEP encrypt/decrypt round-trip (default hash)", function() {
    if (!rsaKey) return false;
    var pt = Buffer.from("OAEP roundtrip plaintext");
    var ct = crypto.rsa_pub_encrypt(pt, rsaKey.public, {padding:"oaep"});
    var dt = crypto.rsa_priv_decrypt(ct, rsaKey.private, {padding:"oaep"});
    return bufferToString(dt) === "OAEP roundtrip plaintext";
});

testFeature("rsa OAEP with sha256 explicit round-trip", function() {
    if (!rsaKey) return false;
    var pt = Buffer.from("oaep sha256");
    var ct = crypto.rsa_pub_encrypt(pt, rsaKey.public, {padding:"oaep", hash:"sha256"});
    var dt = crypto.rsa_priv_decrypt(ct, rsaKey.private, {padding:"oaep", hash:"sha256"});
    return bufferToString(dt) === "oaep sha256";
});

testFeature("rsa OAEP with label round-trip", function() {
    if (!rsaKey) return false;
    var pt    = Buffer.from("labeled oaep");
    var label = Buffer.from("my application context");
    var ct = crypto.rsa_pub_encrypt(pt, rsaKey.public, {padding:"oaep", hash:"sha256", label:label});
    var dt = crypto.rsa_priv_decrypt(ct, rsaKey.private, {padding:"oaep", hash:"sha256", label:label});
    return bufferToString(dt) === "labeled oaep";
});

testFeature("rsa OAEP label mismatch → throws", function() {
    if (!rsaKey) return false;
    var pt = Buffer.from("label test");
    var ct = crypto.rsa_pub_encrypt(pt, rsaKey.public, {padding:"oaep", hash:"sha256", label:Buffer.from("a")});
    try {
        crypto.rsa_priv_decrypt(ct, rsaKey.private, {padding:"oaep", hash:"sha256", label:Buffer.from("b")});
        return false;
    } catch (e) {
        return true;
    }
});


/* ---------------------------------------------------------------
 * 2.1  EC family — P-256, P-384, P-521
 * --------------------------------------------------------------- */

var ecCurves = ["P-256", "P-384", "P-521"];
var ecScalarSizes = {"P-256":32, "P-384":48, "P-521":66};
var ecRawPubSizes = {"P-256":65, "P-384":97, "P-521":133};  /* 04 || X || Y */

ecCurves.forEach(function(curve) {

    var keys = null;

    testFeature("ec_gen_key " + curve + " (positional)", function() {
        keys = crypto.ec_gen_key(curve);
        return keys && typeof keys.public === "string"
            && typeof keys.private === "string"
            && typeof keys.ec_private === "string"
            && /BEGIN PUBLIC KEY/.test(keys.public)
            && /BEGIN PRIVATE KEY/.test(keys.private)
            && /BEGIN EC PRIVATE KEY/.test(keys.ec_private);
    });

    testFeature("ec_components " + curve + " shape", function() {
        if (!keys) return false;
        var pub = crypto.ec_components(keys.public);
        var priv = crypto.ec_components(keys.private);
        return pub.curve === curve && /^[0-9A-F]+$/i.test(pub.x) && /^[0-9A-F]+$/i.test(pub.y)
            && priv.curve === curve && priv.x === pub.x && priv.y === pub.y
            && /^[0-9A-F]+$/i.test(priv.scalar);
    });

    /* duktape lacks String.prototype.padStart; tiny local pad-with-zeros. */
    function lpad(s, n) { while (s.length < n) s = "0" + s; return s; }

    testFeature("ec_import_pub_key " + curve + " raw round-trip", function() {
        if (!keys) return false;
        /* Build raw 04||X||Y from components, re-import, re-extract. */
        var c = crypto.ec_components(keys.public);
        var hexLen = ecScalarSizes[curve] * 2;
        var rawHex = "04" + lpad(c.x, hexLen) + lpad(c.y, hexLen);
        var raw = dehexify(rawHex);
        if (raw.length !== ecRawPubSizes[curve] || raw[0] !== 0x04) return "raw length/lead wrong";
        var pem2 = crypto.ec_import_pub_key(raw, curve);
        var c2 = crypto.ec_components(pem2);
        return c2.x === c.x && c2.y === c.y && c2.curve === curve;
    });

    testFeature("ec_import_priv_key " + curve + " raw round-trip", function() {
        if (!keys) return false;
        var c = crypto.ec_components(keys.private);
        var rawScalar = dehexify(lpad(c.scalar, ecScalarSizes[curve] * 2));
        var imported = crypto.ec_import_priv_key({key: rawScalar, curve: curve, format: "raw"});
        var c2 = crypto.ec_components(imported.private);
        return c2.scalar.replace(/^0+/,"") === c.scalar.replace(/^0+/,"");
    });

    testFeature("ecdsa_sign/verify " + curve + " DER (positional)", function() {
        if (!keys) return false;
        var msg = Buffer.from("message for " + curve);
        var sig = crypto.ecdsa_sign(msg, keys.private);
        return _isU8(sig) && sig.length > 0
            && crypto.ecdsa_verify(msg, keys.public, sig) === true;
    });

    testFeature("ecdsa_sign/verify " + curve + " P1363 (opts)", function() {
        if (!keys) return false;
        var msg = Buffer.from("p1363 test for " + curve);
        var sig = crypto.ecdsa_sign(msg, keys.private, {format:"p1363"});
        if (!_isU8(sig) || sig.length !== 2 * ecScalarSizes[curve]) return false;
        return crypto.ecdsa_verify(msg, keys.public, sig, {format:"p1363"}) === true;
    });

    testFeature("ecdsa cross-format " + curve, function() {
        if (!keys) return false;
        var msg = Buffer.from("cross-format check " + curve);
        var sigDer   = crypto.ecdsa_sign(msg, keys.private);
        var sigP1363 = crypto.ecdsa_sign(msg, keys.private, {format:"p1363"});
        return crypto.ecdsa_verify(msg, keys.public, sigDer) === true
            && crypto.ecdsa_verify(msg, keys.public, sigP1363, {format:"p1363"}) === true;
    });

    testFeature("ecdsa_verify " + curve + " rejects tampered message", function() {
        if (!keys) return false;
        var sig = crypto.ecdsa_sign(Buffer.from("a"), keys.private);
        return crypto.ecdsa_verify(Buffer.from("b"), keys.public, sig) === false;
    });

    testFeature("ecdh " + curve + " symmetry (alice ↔ bob)", function() {
        if (!keys) return false;
        var alice = crypto.ec_gen_key(curve);
        var bob   = crypto.ec_gen_key(curve);
        var sAB = crypto.ecdh(alice.private, bob.public);
        var sBA = crypto.ecdh(bob.private, alice.public);
        return _isU8(sAB) && sAB.length === ecScalarSizes[curve]
            && crypto.timingSafeEqual(sAB, sBA);
    });

    testFeature("ec_gen_key " + curve + " with password + decrypt round-trip", function() {
        var k = crypto.ec_gen_key(curve, "secret");
        if (!/BEGIN ENCRYPTED PRIVATE KEY/.test(k.private)) return "private not encrypted";
        /* signing requires the password */
        var sig = crypto.ecdsa_sign("x", k.private, "secret");
        return crypto.ecdsa_verify("x", k.public, sig) === true;
    });

    testFeature("ec_import_priv_key " + curve + " encrypted → re-encrypted (positional)", function() {
        var k = crypto.ec_gen_key(curve, "OLDpw");
        var k2 = crypto.ec_import_priv_key(k.private, "OLDpw", "NEWpw");
        if (!/BEGIN ENCRYPTED PRIVATE KEY/.test(k2.private)) return "private not re-encrypted";
        var sig = crypto.ecdsa_sign("x", k2.private, "NEWpw");
        return crypto.ecdsa_verify("x", k2.public, sig) === true;
    });
});

testFeature("ec_gen_key default curve is P-256", function() {
    var k = crypto.ec_gen_key();
    return crypto.ec_components(k.public).curve === "P-256";
});

testFeature("ec_import_priv_key wrong password throws", function() {
    var k = crypto.ec_gen_key("P-256", "right");
    try { crypto.ec_import_priv_key(k.private, "wrong"); return false; }
    catch (e) { return true; }
});

testFeature("pemToDer + derToPem round-trip preserves canonical PEM", function() {
    var k = crypto.ec_gen_key();
    var der = crypto.pemToDer(k.public);
    var pem = crypto.derToPem(der, "PUBLIC KEY");
    return k.public.trim() === pem.trim();
});


/* ---------------------------------------------------------------
 * pemToDer / derToPem — round-trip and interop coverage
 *
 * Verifies (a) PEM → DER → PEM round-trips byte-for-byte across all
 * the standard PEM types and (b) every key-consuming crypto function
 * accepts both the PEM string and the equivalent DER buffer for its
 * key argument.
 * --------------------------------------------------------------- */

/* (a) round-trips */
var _rsaKey = crypto.rsa_gen_key(2048);
var _rsaEnc = crypto.rsa_gen_key(2048, "pw");
var _ecKey  = crypto.ec_gen_key("P-256");
var _ecEnc  = crypto.ec_gen_key("P-256", "pw");
var _cert   = crypto.gen_cert("/CN=test.example.com");
var _csr    = crypto.gen_csr(_rsaKey.private, {name: "test.example.com"});

/* Note: encrypted SEC1 EC PRIVATE KEY (the legacy traditional form
 * with "Proc-Type: 4,ENCRYPTED" + "DEK-Info" header lines) stores
 * its cipher and IV in the PEM *headers*, not the DER body, so it
 * cannot be round-tripped through pemToDer/derToPem.  Encrypted
 * PKCS#8 (`-----BEGIN ENCRYPTED PRIVATE KEY-----`) embeds all of
 * that in the DER and round-trips fine — that's the modern form. */
[
    ["RSA PUBLIC KEY (rsa_public PKCS#1)",   _rsaKey.rsa_public,  "RSA PUBLIC KEY"],
    ["PUBLIC KEY (PKCS#8 SPKI)",             _rsaKey.public,      "PUBLIC KEY"],
    ["RSA PRIVATE KEY (rsa_private PKCS#1)", _rsaKey.rsa_private, "RSA PRIVATE KEY"],
    ["PRIVATE KEY (PKCS#8 unencrypted)",     _rsaKey.private,     "PRIVATE KEY"],
    ["ENCRYPTED PRIVATE KEY (PKCS#8)",       _rsaEnc.private,     "ENCRYPTED PRIVATE KEY"],
    ["EC PUBLIC KEY (SPKI)",                 _ecKey.public,       "PUBLIC KEY"],
    ["EC PRIVATE KEY (SEC1 unencrypted)",    _ecKey.ec_private,   "EC PRIVATE KEY"],
    ["EC PRIVATE KEY (PKCS#8 encrypted)",    _ecEnc.private,      "ENCRYPTED PRIVATE KEY"],
    ["CERTIFICATE",                          _cert.cert,          "CERTIFICATE"],
    ["CERTIFICATE REQUEST",                  _csr.pem,            "CERTIFICATE REQUEST"]
].forEach(function(spec) {
    var label = spec[0], pem = spec[1], type = spec[2];
    testFeature("pemToDer + derToPem round-trip: " + label, function() {
        var der = crypto.pemToDer(pem);
        if (!_isU8(der) || der.length < 1) return "der empty";
        var pem2 = crypto.derToPem(der, type);
        return pem.trim() === pem2.trim();
    });
});

/* (b) interop: every key-accepting function works with either form.
 *
 * EC functions accept PEM and DER directly (the new universal
 * loader).  RSA / cert functions accept only PEM, so the proof for
 * those is "PEM → DER → PEM yields a still-usable PEM" — i.e. the
 * conversion is functional and lossless. */

testFeature("interop: rsa PEM → DER → PEM still signs/verifies", function() {
    var pem2 = crypto.derToPem(crypto.pemToDer(_rsaKey.private), "PRIVATE KEY");
    var pub2 = crypto.derToPem(crypto.pemToDer(_rsaKey.public),  "PUBLIC KEY");
    var msg = "rsa round-trip interop";
    var sigOrig = crypto.rsa_sign(msg, _rsaKey.private);
    var sigConv = crypto.rsa_sign(msg, pem2);
    /* PKCS1 v1.5 sign is deterministic: identical key bytes → identical sig. */
    return crypto.timingSafeEqual(sigOrig, sigConv)
        && crypto.rsa_verify(msg, pub2, sigOrig) === true;
});

testFeature("interop: rsa PEM → DER → PEM still encrypts/decrypts", function() {
    var pubPem2  = crypto.derToPem(crypto.pemToDer(_rsaKey.public),  "PUBLIC KEY");
    var privPem2 = crypto.derToPem(crypto.pemToDer(_rsaKey.private), "PRIVATE KEY");
    var ct = crypto.rsa_pub_encrypt("hybrid-secret", pubPem2);
    var dt = crypto.rsa_priv_decrypt(ct, privPem2);
    return bufferToString(dt) === "hybrid-secret";
});

testFeature("interop: rsa_import_priv_key works on PEM → DER → PEM round-trip", function() {
    var pem2 = crypto.derToPem(crypto.pemToDer(_rsaKey.private), "PRIVATE KEY");
    var k = crypto.rsa_import_priv_key(pem2);
    var sig = crypto.rsa_sign("x", k.private);
    return crypto.rsa_verify("x", k.public, sig) === true;
});

testFeature("interop: ecdsa_sign accepts PEM and DER private keys (deterministic for same k? no — verify cross)", function() {
    var msg = "ecdsa_sign interop";
    var sig = crypto.ecdsa_sign(msg, _ecKey.private);
    var derPriv = crypto.pemToDer(_ecKey.private);
    var sig2 = crypto.ecdsa_sign(msg, derPriv);
    /* ECDSA is non-deterministic (random k), so byte-equality of two
     * sigs won't hold; but both must verify with the same public key. */
    return crypto.ecdsa_verify(msg, _ecKey.public, sig) === true
        && crypto.ecdsa_verify(msg, _ecKey.public, sig2) === true;
});

testFeature("interop: ecdsa_verify accepts PEM and DER public keys", function() {
    var msg = "ecdsa_verify interop";
    var sig = crypto.ecdsa_sign(msg, _ecKey.private);
    var derPub = crypto.pemToDer(_ecKey.public);
    return crypto.ecdsa_verify(msg, _ecKey.public, sig) === true
        && crypto.ecdsa_verify(msg, derPub, sig) === true;
});

testFeature("interop: ecdh accepts mixed PEM/DER private/public keys", function() {
    var alice = crypto.ec_gen_key("P-256");
    var bob   = crypto.ec_gen_key("P-256");
    var sharedAllPem = crypto.ecdh(alice.private, bob.public);
    var sharedAllDer = crypto.ecdh(crypto.pemToDer(alice.private), crypto.pemToDer(bob.public));
    var sharedMixed  = crypto.ecdh(crypto.pemToDer(alice.private), bob.public);
    return crypto.timingSafeEqual(sharedAllPem, sharedAllDer)
        && crypto.timingSafeEqual(sharedAllPem, sharedMixed);
});

testFeature("interop: ec_components accepts PEM and DER (same hex output)", function() {
    var derPub = crypto.pemToDer(_ecKey.public);
    var cPem = crypto.ec_components(_ecKey.public);
    var cDer = crypto.ec_components(derPub);
    return cPem.x === cDer.x && cPem.y === cDer.y && cPem.curve === cDer.curve;
});

testFeature("interop: ec_import_pub_key accepts PEM, DER and raw", function() {
    /* PEM input → canonical PEM out */
    var fromPem = crypto.ec_import_pub_key(_ecKey.public);
    /* DER input → same canonical PEM out */
    var fromDer = crypto.ec_import_pub_key(crypto.pemToDer(_ecKey.public));
    /* Raw 04||X||Y reconstructed from components */
    var c = crypto.ec_components(_ecKey.public);
    function lpad(s, n) { while (s.length < n) s = "0" + s; return s; }
    var raw = dehexify("04" + lpad(c.x, 64) + lpad(c.y, 64));
    var fromRaw = crypto.ec_import_pub_key(raw, "P-256");
    return fromPem.trim() === _ecKey.public.trim()
        && fromDer.trim() === _ecKey.public.trim()
        && fromRaw.trim() === _ecKey.public.trim();
});

testFeature("interop: ec_import_priv_key accepts PEM and DER", function() {
    var derPriv = crypto.pemToDer(_ecKey.private);
    var fromPem = crypto.ec_import_priv_key(_ecKey.private);
    var fromDer = crypto.ec_import_priv_key(derPriv);
    return fromPem.public.trim() === fromDer.public.trim()
        && fromPem.private.trim() === fromDer.private.trim();
});

testFeature("interop: rsa_components after PEM → DER → PEM matches original", function() {
    var pem2 = crypto.derToPem(crypto.pemToDer(_rsaKey.public), "PUBLIC KEY");
    var cPem = crypto.rsa_components(_rsaKey.public);
    var cRt  = crypto.rsa_components(pem2);
    return cPem.modulus === cRt.modulus && cPem.exponent === cRt.exponent;
});

testFeature("interop: cert_info after PEM → DER → PEM matches original", function() {
    var pem2 = crypto.derToPem(crypto.pemToDer(_cert.cert), "CERTIFICATE");
    var iPem = crypto.cert_info(_cert.cert);
    var iRt  = crypto.cert_info(pem2);
    return iPem.subject === iRt.subject && iPem.issuer === iRt.issuer;
});

testFeature("pemToDer rejects non-PEM input", function() {
    try { crypto.pemToDer("this is not a PEM block at all"); return false; }
    catch (e) { return /PEM/i.test(e.message); }
});

testFeature("derToPem of arbitrary type label produces parsable output", function() {
    /* Even if the type label is unusual, the output should be a
     * well-formed PEM block that pemToDer can decode back. */
    var bytes = crypto.rand(64);
    var pem = crypto.derToPem(bytes, "RAMPART OPAQUE BLOB");
    var back = crypto.pemToDer(pem);
    return /-----BEGIN RAMPART OPAQUE BLOB-----/.test(pem)
        && crypto.timingSafeEqual(bytes, back);
});


/* ---------------------------------------------------------------
 * cross-checks: HKDF + ECDH
 *
 * Common modern pattern: derive a symmetric key from an ECDH shared
 * secret by feeding it through HKDF.  Verifies the two primitives
 * compose cleanly.
 * --------------------------------------------------------------- */

testFeature("ECDH + HKDF compose: P-256 → AES-256 key", function() {
    var alice = crypto.ec_gen_key("P-256");
    var bob   = crypto.ec_gen_key("P-256");
    var sharedA = crypto.ecdh(alice.private, bob.public);
    var sharedB = crypto.ecdh(bob.private,   alice.public);
    var aesKeyA = crypto.hkdf({ikm:sharedA, info:Buffer.from("aes-256-gcm key"), length:32, hash:"sha256"});
    var aesKeyB = crypto.hkdf({ikm:sharedB, info:Buffer.from("aes-256-gcm key"), length:32, hash:"sha256"});
    return crypto.timingSafeEqual(aesKeyA, aesKeyB);
});


/* ---------------------------------------------------------------
 * 3.1  X25519 (key agreement) and Ed25519 (signing)
 * --------------------------------------------------------------- */

testFeature("x25519_gen_key returns PEM public/private", function() {
    var k = crypto.x25519_gen_key();
    return /BEGIN PUBLIC KEY/.test(k.public)
        && /BEGIN PRIVATE KEY/.test(k.private);
});

testFeature("x25519_components shape", function() {
    var k = crypto.x25519_gen_key();
    var pub = crypto.x25519_components(k.public);
    var priv = crypto.x25519_components(k.private);
    return pub.curve === "X25519"
        && /^[0-9A-F]{64}$/i.test(pub.public)
        && priv.curve === "X25519"
        && priv.public === pub.public
        && /^[0-9A-F]{64}$/i.test(priv.private);
});

testFeature("x25519_import_pub_key raw round-trip", function() {
    var k = crypto.x25519_gen_key();
    var rawPubHex = crypto.x25519_components(k.public).public;
    var pem2 = crypto.x25519_import_pub_key({key: dehexify(rawPubHex), format:"raw"});
    return crypto.x25519_components(pem2).public === rawPubHex;
});

testFeature("x25519_import_priv_key raw round-trip", function() {
    var k = crypto.x25519_gen_key();
    var rawPrivHex = crypto.x25519_components(k.private).private;
    var k2 = crypto.x25519_import_priv_key({key: dehexify(rawPrivHex), format:"raw"});
    return crypto.x25519_components(k2.private).private === rawPrivHex;
});

testFeature("x25519_derive symmetry (positional)", function() {
    var a = crypto.x25519_gen_key();
    var b = crypto.x25519_gen_key();
    var sAB = crypto.x25519_derive(a.private, b.public);
    var sBA = crypto.x25519_derive(b.private, a.public);
    return _isU8(sAB) && sAB.length === 32
        && crypto.timingSafeEqual(sAB, sBA);
});

testFeature("x25519 with password + decrypt round-trip", function() {
    var a = crypto.x25519_gen_key("secret");
    var b = crypto.x25519_gen_key();
    if (!/BEGIN ENCRYPTED PRIVATE KEY/.test(a.private)) return "private not encrypted";
    var s = crypto.x25519_derive(a.private, b.public, "secret");
    return _isU8(s) && s.length === 32;
});

testFeature("x25519_import_priv_key encrypted → re-encrypted (positional)", function() {
    var a = crypto.x25519_gen_key("OLDpw");
    var a2 = crypto.x25519_import_priv_key(a.private, "OLDpw", "NEWpw");
    if (!/BEGIN ENCRYPTED PRIVATE KEY/.test(a2.private)) return "not re-encrypted";
    var b = crypto.x25519_gen_key();
    var s = crypto.x25519_derive(a2.private, b.public, "NEWpw");
    return _isU8(s) && s.length === 32;
});

testFeature("ed25519_gen_key returns PEM public/private", function() {
    var k = crypto.ed25519_gen_key();
    return /BEGIN PUBLIC KEY/.test(k.public)
        && /BEGIN PRIVATE KEY/.test(k.private);
});

testFeature("ed25519_sign/verify round-trip (positional)", function() {
    var k = crypto.ed25519_gen_key();
    var msg = Buffer.from("ed25519 round-trip message");
    var sig = crypto.ed25519_sign(msg, k.private);
    if (!_isU8(sig) || sig.length !== 64) return false;
    return crypto.ed25519_verify(msg, k.public, sig) === true;
});

testFeature("ed25519_verify rejects tampered message", function() {
    var k = crypto.ed25519_gen_key();
    var sig = crypto.ed25519_sign(Buffer.from("original"), k.private);
    return crypto.ed25519_verify(Buffer.from("tampered"), k.public, sig) === false;
});

testFeature("ed25519_verify rejects wrong public key", function() {
    var k  = crypto.ed25519_gen_key();
    var k2 = crypto.ed25519_gen_key();
    var sig = crypto.ed25519_sign(Buffer.from("x"), k.private);
    return crypto.ed25519_verify(Buffer.from("x"), k2.public, sig) === false;
});

testFeature("ed25519 with password + decrypt round-trip", function() {
    var k = crypto.ed25519_gen_key("secret");
    var sig = crypto.ed25519_sign("x", k.private, "secret");
    return crypto.ed25519_verify("x", k.public, sig) === true;
});

testFeature("ed25519_import_priv_key encrypted → re-encrypted", function() {
    var k  = crypto.ed25519_gen_key("OLD");
    var k2 = crypto.ed25519_import_priv_key(k.private, "OLD", "NEW");
    if (!/BEGIN ENCRYPTED PRIVATE KEY/.test(k2.private)) return "not re-encrypted";
    var sig = crypto.ed25519_sign("x", k2.private, "NEW");
    return crypto.ed25519_verify("x", k2.public, sig) === true;
});

/* ---------------------------------------------------------------
 * KMAC (NIST SP 800-185)
 *
 * NIST KMAC test data taken from
 * test/recipes/30-test_evp_data/evpmac_common.txt in OpenSSL.
 * Key K = bytes 0x40..0x5F (32 bytes), used in all samples below.
 * --------------------------------------------------------------- */

(function() {
    var KKEY = (function() {
        var a = [];
        for (var i = 0x40; i <= 0x5F; i++) a.push(i);
        return Buffer.from(a);
    })();

    testFeature("kmac-128 NIST sample #1 (K=40..5F, X=00010203, no custom)", function() {
        var got = crypto.kmac(KKEY, Buffer.from([0,1,2,3]));
        return hexify(got).toUpperCase() ===
            "E5780B0D3EA6F7D3A429C5706AA43A00FADBD7D49628839E3187243F456EE14E";
    });

    testFeature("kmac-128 NIST sample #2 (with 'My Tagged Application' custom)", function() {
        var got = crypto.kmac(KKEY, Buffer.from([0,1,2,3]), "kmac-128",
                              {customization: "My Tagged Application"});
        return hexify(got).toUpperCase() ===
            "3B1FBA963CD8B0B59E8C1A6D71888B7143651AF8BA0A7070C0979E2811324AA5";
    });

    testFeature("kmac-256 NIST sample (with 'My Tagged Application' custom)", function() {
        var got = crypto.kmac(KKEY, Buffer.from([0,1,2,3]), "kmac-256",
                              {customization: "My Tagged Application"});
        return hexify(got).toUpperCase() ===
            "20C570C31346F703C9AC36C61C03CB64C3970D0CFC787E9B79599D273A68D2F7" +
            "F69D4CC3DE9D104A351689F27CF6F5951F0103F33F4F24871024D9C27773A8DD";
    });

    /* OpenSSL's KMAC enforces a minimum key length of 4 bytes
     * (32 bits); below that it throws "invalid key length". */
    testFeature("kmac-128 default output length is 32 bytes", function() {
        return crypto.kmac(KKEY, "d").length === 32;
    });

    testFeature("kmac-256 default output length is 64 bytes", function() {
        return crypto.kmac(KKEY, "d", "kmac-256").length === 64;
    });

    testFeature("kmac custom length override", function() {
        return crypto.kmac(KKEY, "d", "kmac-128", {length: 16}).length === 16
            && crypto.kmac(KKEY, "d", "kmac-256", {length: 8}).length === 8;
    });

    testFeature("kmac rejects key shorter than 4 bytes (OpenSSL minimum)", function() {
        try { crypto.kmac(Buffer.alloc(2, 0), "d"); return false; }
        catch (e) { return /key length/i.test(e.message); }
    });

    testFeature("kmac unknown variant throws", function() {
        try { crypto.kmac(KKEY, "d", "kmac-512"); return false; }
        catch (e) { return /unknown variant/i.test(e.message); }
    });
})();


/* ---------------------------------------------------------------
 * cSHAKE (NIST SP 800-185)
 * --------------------------------------------------------------- */

testFeature("cshake-128 NIST SP 800-185 sample #2 (S='Email Signature')", function() {
    /* X = bytes 0x00..0xC7 (200 bytes), N = "", S = "Email Signature", L = 256 bits */
    var X = []; for (var i = 0; i <= 0xC7; i++) X.push(i);
    var got = crypto.cshake128(Buffer.from(X), {length: 32, customization: "Email Signature"});
    return hexify(got).toUpperCase() ===
        "C5221D50E4F822D96A2E8881A961420F294B7B24FE3D2094BAED2C6524CC166B";
});

testFeature("cshake-128 with empty N and S equals shake-128", function() {
    return hexify(crypto.cshake128("hello")) === crypto.shake128("hello");
});

testFeature("cshake-256 with empty N and S equals shake-256", function() {
    var c = crypto.cshake256("hello", {length: 32});
    return hexify(c) === crypto.shake256("hello");
});

testFeature("cshake customization changes output", function() {
    var a = crypto.cshake128("hi", {customization: "A"});
    var b = crypto.cshake128("hi", {customization: "B"});
    return hexify(a) !== hexify(b);
});

testFeature("cshake default lengths (128 → 16, 256 → 32)", function() {
    return crypto.cshake128("x").length === 16
        && crypto.cshake256("x").length === 32;
});


/* ---------------------------------------------------------------
 * X448 / Ed448 (Curve448 family — same shape as 25519)
 * --------------------------------------------------------------- */

testFeature("x448_gen_key returns PEM public/private", function() {
    var k = crypto.x448_gen_key();
    return /BEGIN PUBLIC KEY/.test(k.public)
        && /BEGIN PRIVATE KEY/.test(k.private);
});

testFeature("x448_components shape (X448 raw = 56 bytes)", function() {
    var k = crypto.x448_gen_key();
    var pub = crypto.x448_components(k.public);
    var priv = crypto.x448_components(k.private);
    return pub.curve === "X448"
        && /^[0-9A-F]{112}$/i.test(pub.public)
        && priv.curve === "X448"
        && priv.public === pub.public
        && /^[0-9A-F]{112}$/i.test(priv.private);
});

testFeature("x448_derive symmetry + 56-byte shared secret", function() {
    var a = crypto.x448_gen_key();
    var b = crypto.x448_gen_key();
    var sAB = crypto.x448_derive(a.private, b.public);
    var sBA = crypto.x448_derive(b.private, a.public);
    return _isU8(sAB) && sAB.length === 56 && crypto.timingSafeEqual(sAB, sBA);
});

testFeature("x448 with password + import_priv_key re-encrypt", function() {
    var a = crypto.x448_gen_key("OLD");
    var a2 = crypto.x448_import_priv_key(a.private, "OLD", "NEW");
    var b = crypto.x448_gen_key();
    var s = crypto.x448_derive(a2.private, b.public, "NEW");
    return _isU8(s) && s.length === 56;
});

testFeature("ed448_gen_key + sign/verify (114-byte sig)", function() {
    var k = crypto.ed448_gen_key();
    var sig = crypto.ed448_sign("hello", k.private);
    return _isU8(sig) && sig.length === 114
        && crypto.ed448_verify("hello", k.public, sig) === true;
});

testFeature("ed448_verify rejects tampered message", function() {
    var k = crypto.ed448_gen_key();
    var sig = crypto.ed448_sign("original", k.private);
    return crypto.ed448_verify("tampered", k.public, sig) === false;
});

/* ---------------------------------------------------------------
 * ML-DSA (NIST FIPS 204 — post-quantum signatures)
 *
 * ML-DSA signing is non-deterministic (samples randomness internally),
 * so we don't have a byte-equal KAT through our API.  Tests cover
 * round-trip, variant sizes, tamper detection, and password support.
 * --------------------------------------------------------------- */

[
    ["ml-dsa-44", 1312, 2420],
    ["ml-dsa-65", 1952, 3309],
    ["ml-dsa-87", 2592, 4627]
].forEach(function(spec) {
    var variant = spec[0], rawPubBytes = spec[1], sigBytes = spec[2];

    testFeature("mldsa " + variant + " gen_key + sign/verify round-trip", function() {
        var k = crypto.mldsa_gen_key(variant);
        if (!/BEGIN PUBLIC KEY/.test(k.public) || !/BEGIN PRIVATE KEY/.test(k.private))
            return "PEM shape wrong";
        var msg = "ml-dsa interop " + variant;
        var sig = crypto.mldsa_sign(msg, k.private);
        if (!_isU8(sig) || sig.length !== sigBytes)
            return "sig length " + sig.length + " (expected " + sigBytes + ")";
        return crypto.mldsa_verify(msg, k.public, sig) === true;
    });

    testFeature("mldsa " + variant + " components has correct raw public size", function() {
        var k = crypto.mldsa_gen_key(variant);
        var comp = crypto.mldsa_components(k.public);
        return comp.variant === variant.toUpperCase().replace("ML-DSA-","ML-DSA-")
            && comp.public.length === rawPubBytes * 2;   /* hex chars */
    });

    testFeature("mldsa " + variant + " rejects tampered message", function() {
        var k = crypto.mldsa_gen_key(variant);
        var sig = crypto.mldsa_sign("original", k.private);
        return crypto.mldsa_verify("tampered", k.public, sig) === false;
    });

    testFeature("mldsa " + variant + " rejects wrong public key", function() {
        var k  = crypto.mldsa_gen_key(variant);
        var k2 = crypto.mldsa_gen_key(variant);
        var sig = crypto.mldsa_sign("x", k.private);
        return crypto.mldsa_verify("x", k2.public, sig) === false;
    });
});

testFeature("mldsa with password + decrypt round-trip", function() {
    var k = crypto.mldsa_gen_key("ml-dsa-65", "secret");
    if (!/BEGIN ENCRYPTED PRIVATE KEY/.test(k.private)) return "not encrypted";
    var sig = crypto.mldsa_sign("x", k.private, "secret");
    return crypto.mldsa_verify("x", k.public, sig) === true;
});

testFeature("mldsa unknown variant throws", function() {
    try { crypto.mldsa_gen_key("ml-dsa-128"); return false; }
    catch (e) { return /unknown variant/i.test(e.message); }
});


/* ---------------------------------------------------------------
 * ML-KEM (NIST FIPS 203 — post-quantum key encapsulation)
 *
 * Spec sizes:
 *   ML-KEM-512:  pub=800,  ciphertext=768,  shared=32
 *   ML-KEM-768:  pub=1184, ciphertext=1088, shared=32
 *   ML-KEM-1024: pub=1568, ciphertext=1568, shared=32
 * --------------------------------------------------------------- */

[
    ["ml-kem-512",  800,  768],
    ["ml-kem-768",  1184, 1088],
    ["ml-kem-1024", 1568, 1568]
].forEach(function(spec) {
    var variant = spec[0], rawPubBytes = spec[1], ctBytes = spec[2];

    testFeature("mlkem " + variant + " encapsulate/decapsulate round-trip", function() {
        var k = crypto.mlkem_gen_key(variant);
        if (!/BEGIN PUBLIC KEY/.test(k.public) || !/BEGIN PRIVATE KEY/.test(k.private))
            return "PEM shape wrong";
        var enc = crypto.mlkem_encapsulate(k.public);
        if (!_isU8(enc.ciphertext) || enc.ciphertext.length !== ctBytes)
            return "ciphertext size " + enc.ciphertext.length + " (expected " + ctBytes + ")";
        if (!_isU8(enc.sharedSecret) || enc.sharedSecret.length !== 32)
            return "shared-secret size " + enc.sharedSecret.length + " (expected 32)";
        var ss2 = crypto.mlkem_decapsulate(enc.ciphertext, k.private);
        return crypto.timingSafeEqual(enc.sharedSecret, ss2);
    });

    testFeature("mlkem " + variant + " components has correct raw public size", function() {
        var k = crypto.mlkem_gen_key(variant);
        var comp = crypto.mlkem_components(k.public);
        return comp.variant === variant.toUpperCase().replace("ML-KEM-","ML-KEM-")
            && comp.public.length === rawPubBytes * 2;
    });

    testFeature("mlkem " + variant + " decapsulate with wrong key fails (different shared)", function() {
        var k1 = crypto.mlkem_gen_key(variant);
        var k2 = crypto.mlkem_gen_key(variant);
        var enc = crypto.mlkem_encapsulate(k1.public);
        /* ML-KEM is IND-CCA2: decapsulating with the wrong key
         * doesn't throw — it returns a deterministically-derived
         * pseudo-shared-secret that won't match the original (this
         * is "implicit rejection", a core PQ KEM property). */
        var ss2 = crypto.mlkem_decapsulate(enc.ciphertext, k2.private);
        return _isU8(ss2) && ss2.length === 32
            && !crypto.timingSafeEqual(enc.sharedSecret, ss2);
    });
});

testFeature("mlkem with password + decapsulate round-trip", function() {
    var k = crypto.mlkem_gen_key("ml-kem-768", "secret");
    if (!/BEGIN ENCRYPTED PRIVATE KEY/.test(k.private)) return "not encrypted";
    var enc = crypto.mlkem_encapsulate(k.public);
    var ss2 = crypto.mlkem_decapsulate(enc.ciphertext, k.private, "secret");
    return crypto.timingSafeEqual(enc.sharedSecret, ss2);
});

testFeature("mlkem unknown variant throws", function() {
    try { crypto.mlkem_gen_key("ml-kem-128"); return false; }
    catch (e) { return /unknown variant/i.test(e.message); }
});


testFeature("ed448 RFC 8032 §7.4 Test 1 (empty msg KAT)", function() {
    /* RFC 8032 §7.4 — first published Ed448 test vector. */
    var sk = dehexify("6c82a562cb808d10d632be89c8513ebf"
                    + "6c929f34ddfa8c9f63c9960ef6e348a3"
                    + "528c8a3fcc2f044e39a3fc5b94492f8f"
                    + "032e7549a20098f95b");
    var pk = dehexify("5fd7449b59b461fd2ce787ec616ad46a"
                    + "1da1342485a70e1f8a0ea75d80e96778"
                    + "edf124769b46c7061bd6783df1e50f6c"
                    + "d1fa1abeafe8256180");
    var skKey = crypto.ed448_import_priv_key({key: sk, format:"raw"});
    var pkKey = crypto.ed448_import_pub_key({key: pk, format:"raw"});
    var sig = crypto.ed448_sign(Buffer.alloc(0), skKey.private);
    var want = dehexify(
        "533a37f6bbe457251f023c0d88f976ae"
      + "2dfb504a843e34d2074fd823d41a591f"
      + "2b233f034f628281f2fd7a22ddd47d78"
      + "28c59bd0a21bfd3980ff0d2028d4b18a"
      + "9df63e006c5d1c2d345b925d8dc00b41"
      + "04852db99ac5c7cdda8530a113a0f4db"
      + "b61149f05a7363268c71d95808ff2e65"
      + "2600");
    return crypto.timingSafeEqual(sig, want)
        && crypto.ed448_verify(Buffer.alloc(0), pkKey, sig) === true;
});


testFeature("ed25519 RFC 8032 Test 1 (all-zero key)", function() {
    /* RFC 8032 §7.1 Test 1:
     *   SK   = 9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60
     *   PK   = d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a
     *   msg  = (empty)
     *   sig  = e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555
     *          fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b
     */
    var sk = dehexify("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    var pk = dehexify("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    var skKey = crypto.ed25519_import_priv_key({key: sk, format:"raw"});
    var pkKey = crypto.ed25519_import_pub_key({key: pk, format:"raw"});
    var sig = crypto.ed25519_sign(Buffer.alloc(0), skKey.private);
    var want = dehexify("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    return crypto.timingSafeEqual(sig, want)
        && crypto.ed25519_verify(Buffer.alloc(0), pkKey, sig) === true;
});


/* ---------------------------------------------------------------
 * 3.2  scrypt
 *
 * RFC 7914 Test Vector 3:
 *   pass="pleaseletmein"  salt="SodiumChloride"  N=16384 r=8 p=1 len=64
 * --------------------------------------------------------------- */

testFeature("scrypt RFC 7914 test vector 3", function() {
    var dk = crypto.scrypt({
        pass: "pleaseletmein",
        salt: "SodiumChloride",
        N: 16384, r: 8, p: 1,
        length: 64,
        returnType: "hex"
    });
    return dk === "7023bdcb3afd7348461c06cd81fd38ebfda8fbba904f8e3ea9b543f6545da1f2"
                + "d5432955613f0fcf62d49705242a9af9e61e85dc0d651e40dfcf017b45575887";
});

testFeature("scrypt default returnType is Uint8Array", function() {
    var dk = crypto.scrypt({pass:"a", salt:"b", N:1024, r:8, p:1, length:32});
    return _isU8(dk) && dk.length === 32;
});

testFeature("scrypt non-power-of-two N throws", function() {
    try { crypto.scrypt({pass:"a", salt:"b", N:1000, r:8, p:1, length:32}); return false; }
    catch (e) { return /power of two/i.test(e.message); }
});


/* ---------------------------------------------------------------
 * Additional KATs (interop / spec-compliance proofs)
 * --------------------------------------------------------------- */

/* AES-256-GCM NIST SP 800-38D Test Case 13:
 *   K   = 0x00 * 32
 *   IV  = 0x00 * 12
 *   P   = empty
 *   A   = empty
 *   C   = empty
 *   T   = 530f8afbc74536b9a963b4f1c4cb738b
 * Our encrypt() returns ciphertext || tag, so with an empty plaintext
 * the entire output is the 16-byte tag. */
testFeature("aes-256-gcm NIST SP 800-38D Test Case 13", function() {
    var key = Buffer.alloc(32, 0);
    var iv  = Buffer.alloc(12, 0);
    var ct  = crypto.encrypt({cipher:"aes-256-gcm", key:key, iv:iv, data:Buffer.alloc(0)});
    return hexify(ct) === "530f8afbc74536b9a963b4f1c4cb738b";
});

/* X25519 RFC 7748 §5.2 — Alice + Bob's published key pair:
 *   alice priv  = 77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a
 *   alice pub   = 8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a
 *   bob   priv  = 5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb
 *   bob   pub   = de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f
 *   shared K    = 4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742
 */
testFeature("x25519_derive RFC 7748 §5.2 Alice⇄Bob", function() {
    var aliceKey = crypto.x25519_import_priv_key({
        key: dehexify("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a"),
        format: "raw"});
    var bobPub = crypto.x25519_import_pub_key({
        key: dehexify("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"),
        format: "raw"});
    var shared = crypto.x25519_derive(aliceKey.private, bobPub);
    var want = dehexify("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    return crypto.timingSafeEqual(shared, want);
});

/* ---------------------------------------------------------------
 * rsa_import_priv_key — call-form matrix + usability check
 *
 * The doc lists two calling shapes:
 *   crypto.rsa_import_priv_key(oldprivate_key[, opts])
 *   crypto.rsa_import_priv_key(oldprivate_key[, oldpass][, newpass])
 *
 * Where opts is {decryptPassword, encryptPassword}.  Combined with
 * the encrypted/unencrypted input choice, that's seven valid call
 * shapes; each must produce a four-key object (public, private,
 * rsa_public, rsa_private) whose private key is encrypted iff a
 * new password was supplied, and whose output keys round-trip
 * through sign/verify and pub-encrypt/priv-decrypt.
 * --------------------------------------------------------------- */

/* Source keypairs we'll feed back into rsa_import_priv_key. */
var rsaPlain = crypto.rsa_gen_key(2048);             /* unencrypted PEMs */
var rsaEnc   = crypto.rsa_gen_key(2048, "OLDpw");    /* private encrypted with "OLDpw" */

/* Helper: assert (a) the four expected fields are present, (b) the
 * private key has the expected encrypted-or-not header, and (c) the
 * keys actually work for sign/verify and pub-encrypt/priv-decrypt. */
function checkImportedShape(k, opts) {
    /* opts = {encrypted: bool, password: string or null} */
    if (!k.public || !k.private || !k.rsa_public || !k.rsa_private)
        return "missing one of public/private/rsa_public/rsa_private";
    if (!/-----BEGIN PUBLIC KEY-----/.test(k.public))
        return "public not PKCS#8 PEM";
    if (!/-----BEGIN RSA PUBLIC KEY-----/.test(k.rsa_public))
        return "rsa_public not PKCS#1 PEM";
    if (opts.encrypted) {
        if (!/-----BEGIN ENCRYPTED PRIVATE KEY-----/.test(k.private))
            return "expected ENCRYPTED PRIVATE KEY for pkcs8";
        if (!/Proc-Type: 4,ENCRYPTED/.test(k.rsa_private))
            return "expected encrypted RSA PRIVATE KEY";
    } else {
        if (!/-----BEGIN PRIVATE KEY-----/.test(k.private))
            return "expected unencrypted PRIVATE KEY for pkcs8";
        if (!/-----BEGIN RSA PRIVATE KEY-----/.test(k.rsa_private))
            return "expected unencrypted RSA PRIVATE KEY";
        if (/Proc-Type: 4,ENCRYPTED/.test(k.rsa_private))
            return "rsa_private should not be encrypted";
    }
    /* Sign + verify, both public-key forms. */
    var msg = "rsa_import_priv_key usability check";
    var sig = opts.password
        ? crypto.rsa_sign(msg, k.private, opts.password)
        : crypto.rsa_sign(msg, k.private);
    if (crypto.rsa_verify(msg, k.public, sig) !== true)
        return "verify(public) failed";
    if (crypto.rsa_verify(msg, k.rsa_public, sig) !== true)
        return "verify(rsa_public) failed";
    /* Encrypt with public, decrypt with private. */
    var pt = "small";
    var ct = crypto.rsa_pub_encrypt(pt, k.public);
    var dt = opts.password
        ? crypto.rsa_priv_decrypt(ct, k.private, null, opts.password)
        : crypto.rsa_priv_decrypt(ct, k.private);
    if (bufferToString(dt) !== pt) return "decrypt round-trip mismatch";
    return true;
}

testFeature("rsa_import_priv_key: unencrypted in, unencrypted out (no opts)", function() {
    var k = crypto.rsa_import_priv_key(rsaPlain.private);
    var r = checkImportedShape(k, {encrypted: false, password: null});
    return r === true ? true : r;
});

testFeature("rsa_import_priv_key: unencrypted in, encrypted out (positional newpass)", function() {
    var k = crypto.rsa_import_priv_key(rsaPlain.private, null, "NEWpw");
    var r = checkImportedShape(k, {encrypted: true, password: "NEWpw"});
    return r === true ? true : r;
});

testFeature("rsa_import_priv_key: unencrypted in, encrypted out (opts encryptPassword)", function() {
    var k = crypto.rsa_import_priv_key(rsaPlain.private, {encryptPassword: "NEWpw"});
    var r = checkImportedShape(k, {encrypted: true, password: "NEWpw"});
    return r === true ? true : r;
});

testFeature("rsa_import_priv_key: encrypted in, unencrypted out (positional oldpass only)", function() {
    var k = crypto.rsa_import_priv_key(rsaEnc.private, "OLDpw");
    var r = checkImportedShape(k, {encrypted: false, password: null});
    return r === true ? true : r;
});

testFeature("rsa_import_priv_key: encrypted in, unencrypted out (opts decryptPassword only)", function() {
    var k = crypto.rsa_import_priv_key(rsaEnc.private, {decryptPassword: "OLDpw"});
    var r = checkImportedShape(k, {encrypted: false, password: null});
    return r === true ? true : r;
});

testFeature("rsa_import_priv_key: encrypted in, re-encrypted out (positional oldpass + newpass)", function() {
    var k = crypto.rsa_import_priv_key(rsaEnc.private, "OLDpw", "NEWpw");
    var r = checkImportedShape(k, {encrypted: true, password: "NEWpw"});
    return r === true ? true : r;
});

testFeature("rsa_import_priv_key: encrypted in, re-encrypted out (opts decryptPassword + encryptPassword)", function() {
    var k = crypto.rsa_import_priv_key(rsaEnc.private,
        {decryptPassword: "OLDpw", encryptPassword: "NEWpw"});
    var r = checkImportedShape(k, {encrypted: true, password: "NEWpw"});
    return r === true ? true : r;
});

testFeature("rsa_import_priv_key: cross-check — re-encrypted output uses *new* password (old fails)", function() {
    var k = crypto.rsa_import_priv_key(rsaEnc.private, "OLDpw", "NEWpw");
    /* Sign with NEW password should succeed */
    var ok = false;
    try {
        var sig = crypto.rsa_sign("x", k.private, "NEWpw");
        ok = crypto.rsa_verify("x", k.public, sig);
    } catch (e) { return "sign with NEWpw threw: " + e.message; }
    if (!ok) return "sign+verify with NEWpw failed";
    /* Sign with OLD password should throw */
    try { crypto.rsa_sign("x", k.private, "OLDpw"); return "OLDpw should have failed"; }
    catch (e) { /* expected */ }
    return true;
});

testFeature("rsa_import_priv_key: wrong decrypt password throws", function() {
    try { crypto.rsa_import_priv_key(rsaEnc.private, "WRONG"); return false; }
    catch (e) { return true; }
});

testFeature("rsa_import_priv_key: re-imported key signs identically (deterministic check)", function() {
    /* PKCS1-v1.5 is deterministic, so the same key + same message
     * produces an identical signature.  Verifies the imported key is
     * mathematically identical to the source, not just structurally. */
    var k2 = crypto.rsa_import_priv_key(rsaPlain.private);
    var sig1 = crypto.rsa_sign("x", rsaPlain.private);
    var sig2 = crypto.rsa_sign("x", k2.private);
    return crypto.timingSafeEqual(sig1, sig2);
});


/* ECDH P-256 RFC 5903 §8.1 — published initiator/responder key pair:
 *   initiator priv =  C88F01F510D9AC3F70A292DAA2316DE544E9AAB8AFE84049C62A9C57862D1433
 *   responder pubX =  D12DFB5289C8D4F81208B70270398C342296970A0BCCB74C736FC7554494BF63
 *   responder pubY =  56FBF3CA366CC23E8157854C13C58D6AAC23F046ADA30F8353E74F33039872AB
 *   shared X (Z)   =  D6840F6B42F6EDAFD13116E0E12565202FEF8E9ECE7DCE03812464D04B9442DE
 * In ECDH the shared secret is the X-coordinate; the Y is discarded. */
testFeature("ecdh P-256 RFC 5903 §8.1", function() {
    var priv = crypto.ec_import_priv_key({
        key:    dehexify("c88f01f510d9ac3f70a292daa2316de544e9aab8afe84049c62a9c57862d1433"),
        curve:  "P-256",
        format: "raw"});
    /* Raw uncompressed point: 04 || X || Y */
    var pub = crypto.ec_import_pub_key(
        dehexify("04"
               + "d12dfb5289c8d4f81208b70270398c342296970a0bccb74c736fc7554494bf63"
               + "56fbf3ca366cc23e8157854c13c58d6aac23f046ada30f8353e74f33039872ab"),
        "P-256");
    var shared = crypto.ecdh(priv.private, pub);
    var want = dehexify("d6840f6b42f6edafd13116e0e12565202fef8e9ece7dce03812464d04b9442de");
    return crypto.timingSafeEqual(shared, want);
});


cleanup();
testFeature.exit();
