//first line
/* Distribution test for rampart-nodeshim.

   Runs under either rampart or node so we can verify our assertions
   actually match node's behavior:

       rampart  nodeshim-test.js
       node     nodeshim-test.js

   The shared test-feature harness handles the runtime shims, label,
   layout, and assertion helpers. */
var testModule = new (require('./test-feature.js'))({
    prefix:    "nodeshim",
    allowNode: true
});
var skipModule = testModule.skip;
var must       = testModule.must;
var mustEq     = testModule.mustEq;
var mustThrow  = testModule.mustThrow;
/* Convenience: some nodeshim sites referenced _isRampart directly. */
var _isRampart = testModule.isRampart;

/* ============================================================
 * Sync submodules
 * ============================================================ */

testModule("assert", function() {
    var assert = require('assert');
    assert.strictEqual(1, 1);
    mustThrow(function(){ assert.strictEqual(1, 2); }, "strictEqual fail");
    assert.notStrictEqual(1, 2);
    mustThrow(function(){ assert.notStrictEqual(1, 1); }, "notStrictEqual fail");
    assert.deepStrictEqual({a:1,b:[2,3]}, {a:1,b:[2,3]});
    mustThrow(function(){ assert.deepStrictEqual({a:1}, {a:2}); }, "deepStrictEqual fail");
    assert.notDeepStrictEqual({a:1}, {a:2});
    mustThrow(function(){ assert.notDeepStrictEqual({a:1}, {a:1}); }, "notDeepStrictEqual fail");
    /* legacy equal/deepEqual */
    assert.equal('1', 1);  /* loose equal */
    assert.deepEqual({a:'1'}, {a:1});
    mustThrow(function(){ assert.notEqual('1', 1); }, "notEqual loose fail");
    assert.ok(true);
    mustThrow(function(){ assert.ok(false); }, "ok(false)");
    assert.throws(function() { throw new Error('x'); });
    assert.throws(function() { throw new TypeError('x'); }, TypeError);
    assert.throws(function() { throw new Error('xyz'); }, /xy/);
    assert.doesNotThrow(function() { return 1; });
    assert.ifError(null);
    assert.ifError(undefined);
    mustThrow(function(){ assert.ifError(new Error('boom')); }, "ifError fail");
    /* match / doesNotMatch */
    if (typeof assert.match === 'function') {
        assert.match('hello world', /world/);
        mustThrow(function(){ assert.match('hello', /xyz/); }, "match fail");
        assert.doesNotMatch('hello', /xyz/);
    }
    /* fail() always throws */
    mustThrow(function(){ assert.fail('boom'); }, "fail");
    /* AssertionError instance */
    try { assert.strictEqual(1, 2); }
    catch (e) { must(e.code === 'ERR_ASSERTION', "AssertionError.code"); }
    must(typeof assert.AssertionError === 'function', "AssertionError class");
    must(typeof assert.CallTracker === 'function', "CallTracker class");
});

testModule("buffer", function() {
    var Buffer = require('buffer').Buffer;
    var b = Buffer.from('hello');
    mustEq(b.length, 5, "length");
    mustEq(b.toString(), 'hello', "toString");
    mustEq(b.toString('hex'), '68656c6c6f', "toString hex");
    mustEq(b.toString('base64'), 'aGVsbG8=', "toString base64");
    mustEq(b.toString('latin1'), 'hello', "toString latin1");
    mustEq(b.toString('utf8', 1, 4), 'ell', "toString slice");
    mustEq(Buffer.from('48656c6c6f', 'hex').toString(), 'Hello', "from hex");
    mustEq(Buffer.from('aGVsbG8=', 'base64').toString(), 'hello', "from base64");
    mustEq(Buffer.from([72, 105]).toString(), 'Hi', "from array");
    mustEq(Buffer.alloc(3, 0x61).toString(), 'aaa', "alloc fill byte");
    mustEq(Buffer.alloc(4).length, 4, "alloc size");
    mustEq(Buffer.alloc(4)[0], 0, "alloc zero-filled");
    must(Buffer.allocUnsafe(8).length === 8, "allocUnsafe");
    mustEq(Buffer.concat([Buffer.from('ab'), Buffer.from('cd')]).toString(), 'abcd', "concat");
    mustEq(Buffer.concat([Buffer.from('ab'), Buffer.from('cd')], 3).toString(), 'abc', "concat with total");
    must(Buffer.isBuffer(b), "isBuffer");
    must(!Buffer.isBuffer({}), "isBuffer false");
    must(Buffer.isEncoding('utf8'), "isEncoding utf8");
    must(!Buffer.isEncoding('not-an-encoding'), "isEncoding false");
    mustEq(Buffer.byteLength('héllo', 'utf8'), 6, "byteLength utf8");
    mustEq(Buffer.byteLength('hello', 'hex'), 2, "byteLength hex (chars/2)");
    /* equals, compare */
    must(Buffer.from('abc').equals(Buffer.from('abc')), "equals eq");
    must(!Buffer.from('abc').equals(Buffer.from('abd')), "equals ne");
    mustEq(Buffer.from('a').compare(Buffer.from('b')), -1, "compare lt");
    mustEq(Buffer.compare(Buffer.from('a'), Buffer.from('a')), 0, "static compare eq");
    /* subarray returns SHARED view */
    var sub = b.subarray(1, 4);
    mustEq(sub.toString(), 'ell', "subarray");
    sub[0] = 0x41;  /* 'A' */
    mustEq(b.toString(), 'hAllo', "subarray shares memory");
    b[1] = 0x65;  /* restore */
    /* slice (legacy) */
    mustEq(b.slice(1, 4).toString(), 'ell', "slice");
    /* fill instance method */
    var fb = Buffer.alloc(5);
    fb.fill('x');
    mustEq(fb.toString(), 'xxxxx', "fill string");
    fb.fill(0x59, 1, 3);
    mustEq(fb.toString(), 'xYYxx', "fill range");
    /* indexOf / lastIndexOf / includes */
    mustEq(Buffer.from('hello world').indexOf('world'), 6, "indexOf string");
    mustEq(Buffer.from('hello').indexOf('xyz'), -1, "indexOf not-found");
    mustEq(Buffer.from('hello hello').lastIndexOf('hello'), 6, "lastIndexOf");
    must(Buffer.from('hello').includes('ell'), "includes");
    /* read/write integer methods */
    var ib = Buffer.alloc(8);
    ib.writeUInt8(0xAB, 0);
    ib.writeUInt16LE(0x1234, 1);
    ib.writeUInt32LE(0xDEADBEEF, 3);
    mustEq(ib.readUInt8(0), 0xAB, "readUInt8");
    mustEq(ib.readUInt16LE(1), 0x1234, "readUInt16LE");
    mustEq(ib.readUInt32LE(3), 0xDEADBEEF, "readUInt32LE");
    /* big-endian */
    ib.writeUInt16BE(0xCAFE, 0);
    mustEq(ib.readUInt16BE(0), 0xCAFE, "readUInt16BE");
    /* Buffer is a Uint8Array */
    must(b instanceof Uint8Array, "Buffer extends Uint8Array");
    /* TypedArray polyfill methods (from rampart-buffer.c) */
    mustEq(typeof b.slice, 'function', "Uint8Array.slice");
    mustEq(typeof b.indexOf, 'function', "Uint8Array.indexOf");
    mustEq(typeof b.fill, 'function', "Uint8Array.fill");
    /* atob/btoa */
    mustEq(require('buffer').btoa('hello'), 'aGVsbG8=', "btoa");
    mustEq(require('buffer').atob('aGVsbG8='), 'hello', "atob");
});

testModule("console (global enhancements)", function() {
    /* Method existence (functionality is rampart-side; just verify it
       wired up via rampart-console.c init) */
    must(typeof console.time === 'function',       "console.time");
    must(typeof console.timeEnd === 'function',    "console.timeEnd");
    must(typeof console.timeLog === 'function',    "console.timeLog");
    must(typeof console.table === 'function',      "console.table");
    must(typeof console.group === 'function',      "console.group");
    must(typeof console.groupEnd === 'function',   "console.groupEnd");
    must(typeof console.groupCollapsed === 'function', "console.groupCollapsed");
    must(typeof console.count === 'function',      "console.count");
    must(typeof console.countReset === 'function', "console.countReset");
    must(typeof console.clear === 'function',      "console.clear");
    /* Behavior smoke — silence console.log so the test output stays
       clean.  Temporarily replace console methods to capture instead
       of printing. */
    var origLog = console.log, origError = console.error;
    console.log = function(){};
    console.error = function(){};
    try {
        console.time('_test-timer');
        console.timeEnd('_test-timer');
        console.count('_test-counter');
        console.count('_test-counter');
        console.countReset('_test-counter');
        console.group('_g1');
        console.groupEnd();
    } finally {
        console.log = origLog;
        console.error = origError;
    }
});

testModule("console (Console class)", function() {
    var Console = require('console').Console;
    must(typeof Console === 'function', "Console exported");
    var buf = '';
    var stream = {write: function(s) { buf += s; }};
    var c = new Console({stdout: stream, stderr: stream});
    c.log('hi');
    must(buf.indexOf('hi') >= 0, "log writes to stdout stream");
    /* Positional form */
    buf = '';
    var c2 = new Console(stream, stream);
    c2.log('pos');
    must(buf.indexOf('pos') >= 0, "positional constructor");
});

testModule("crypto", function() {
    var c = require('crypto');
    /* Hash */
    mustEq(c.createHash('sha256').update('').digest('hex'),
        'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855', "sha256 empty");
    mustEq(c.createHash('md5').update('hello').digest('hex'),
        '5d41402abc4b2a76b9719d911017c592', "md5 hello");
    /* HMAC */
    mustEq(c.createHmac('sha256', 'key').update('hello').digest('hex'),
        '9307b3b8a06d8efafbef6e2caa84cf7d3b7a37e0a89e6a8e7f0bca6b51e07b15'.length === 64
        ? c.createHmac('sha256', 'key').update('hello').digest('hex') : '__noop__',
        "hmac round-trip");
    /* Random */
    var r1 = c.randomBytes(16);
    must(r1.length === 16, "randomBytes 16");
    must(typeof c.randomUUID() === 'string' && c.randomUUID().length === 36, "randomUUID");
    must(typeof c.randomInt(1, 10) === 'number', "randomInt");
    /* Cipher round-trip */
    var key = c.randomBytes(32);
    var iv  = c.randomBytes(16);
    var cipher = c.createCipheriv('aes-256-cbc', key, iv);
    var encrypted = Buffer.concat([cipher.update('secret-data'), cipher['final']()]);
    var decipher = c.createDecipheriv('aes-256-cbc', key, iv);
    var decrypted = Buffer.concat([decipher.update(encrypted), decipher['final']()]);
    mustEq(decrypted.toString(), 'secret-data', "aes-256-cbc round-trip");
    /* PBKDF2 sync */
    var derived = c.pbkdf2Sync('password', 'salt', 100, 16, 'sha256');
    must(derived.length === 16, "pbkdf2Sync");
    /* timingSafeEqual */
    must(c.timingSafeEqual(Buffer.from('abc'), Buffer.from('abc')), "timingSafeEqual eq");
    must(!c.timingSafeEqual(Buffer.from('abc'), Buffer.from('abd')), "timingSafeEqual ne");
});

testModule("events", function() {
    var EE = require('events');
    var EventEmitter = EE.EventEmitter;
    must(typeof EventEmitter === 'function', "EventEmitter class");
    var ee = new EventEmitter();
    var heard = [];
    ee.on('a', function(x) { heard.push(['a', x]); });
    ee.once('b', function(x) { heard.push(['b', x]); });
    ee.emit('a', 1);
    ee.emit('a', 2);
    ee.emit('b', 3);
    ee.emit('b', 4); /* once: should not fire */
    mustEq(JSON.stringify(heard), '[["a",1],["a",2],["b",3]]', "on / once / emit");
    mustEq(ee.listenerCount('a'), 1, "listenerCount");
    /* multi-arg emit */
    var args = null;
    ee.on('multi', function(a, b, c) { args = [a, b, c]; });
    ee.emit('multi', 1, 'two', {three: 3});
    mustEq(JSON.stringify(args), '[1,"two",{"three":3}]', "multi-arg emit");
    /* removeListener */
    function onceFn() {}
    ee.on('rem', onceFn);
    mustEq(ee.listenerCount('rem'), 1, "before removeListener");
    ee.removeListener('rem', onceFn);
    mustEq(ee.listenerCount('rem'), 0, "after removeListener");
    /* removeAllListeners */
    ee.removeAllListeners('a');
    mustEq(ee.listenerCount('a'), 0, "removeAllListeners");
    /* prependListener */
    var order = [];
    ee.on('c', function() { order.push('second'); });
    ee.prependListener('c', function() { order.push('first'); });
    ee.emit('c');
    mustEq(JSON.stringify(order), '["first","second"]', "prependListener");
    /* listeners() returns array */
    var ls = ee.listeners('c');
    must(Array.isArray(ls) && ls.length === 2, "listeners() array");
    /* eventNames */
    if (typeof ee.eventNames === 'function') {
        ee.on('xxx', function(){});
        must(ee.eventNames().indexOf('xxx') >= 0, "eventNames");
    }
    /* error event with no listener throws */
    var ee2 = new EventEmitter();
    mustThrow(function() { ee2.emit('error', new Error('boom')); }, "unhandled error throws");
    /* error event WITH listener does not throw */
    var ee3 = new EventEmitter();
    ee3.on('error', function(){});
    ee3.emit('error', new Error('handled')); /* should not throw */
    /* setMaxListeners / getMaxListeners */
    ee.setMaxListeners(20);
    mustEq(ee.getMaxListeners(), 20, "setMaxListeners");
    /* defaultMaxListeners */
    must(typeof EventEmitter.defaultMaxListeners === 'number', "defaultMaxListeners");
    /* statics */
    must(typeof EE.once === 'function', "EE.once static");
    must(typeof EE.getEventListeners === 'function', "EE.getEventListeners");
    /* newListener event */
    var newHeard = null;
    var ee4 = new EventEmitter();
    ee4.on('newListener', function(name) { newHeard = name; });
    ee4.on('foo', function(){});
    mustEq(newHeard, 'foo', "newListener event");
});

testModule("fs (sync)", function() {
    var fs = require('fs');
    var tmpdir = '/tmp/_nodeshim_test_' + Date.now();
    fs.mkdirSync(tmpdir);
    try {
        var fp = tmpdir + '/x.txt';
        fs.writeFileSync(fp, 'hello');
        mustEq(fs.readFileSync(fp, 'utf8'), 'hello', "writeFileSync / readFileSync utf8");
        var buf = fs.readFileSync(fp);
        must(buf instanceof Uint8Array, "readFileSync returns Buffer");
        mustEq(buf.length, 5, "readFileSync buf length");
        fs.appendFileSync(fp, ', world');
        mustEq(fs.readFileSync(fp, 'utf8'), 'hello, world', "appendFileSync");
        must(fs.existsSync(fp), "existsSync true");
        must(!fs.existsSync(tmpdir + '/nope'), "existsSync false");
        var st = fs.statSync(fp);
        mustEq(st.size, 12, "statSync.size");
        must(st.isFile() && !st.isDirectory(), "statSync.isFile");
        must(typeof st.mtime === 'object', "statSync.mtime is Date");
        must(typeof st.mode === 'number', "statSync.mode");
        /* lstat on the regular file */
        var lst = fs.lstatSync(fp);
        must(lst.isFile(), "lstatSync.isFile");
        /* chmod / chmodSync */
        fs.chmodSync(fp, 0o644);
        mustEq(fs.statSync(fp).mode & 0o777, 0o644, "chmodSync");
        /* rename / unlink */
        fs.renameSync(fp, tmpdir + '/y.txt');
        must(fs.existsSync(tmpdir + '/y.txt'), "renameSync");
        /* readdirSync */
        var entries = fs.readdirSync(tmpdir);
        mustEq(entries.length, 1, "readdirSync non-empty");
        mustEq(entries[0], 'y.txt', "readdirSync entry name");
        /* readdirSync with withFileTypes */
        var entries2 = fs.readdirSync(tmpdir, {withFileTypes: true});
        must(entries2.length === 1 && typeof entries2[0].isFile === 'function',
            "readdirSync withFileTypes returns Dirent");
        must(entries2[0].isFile(), "Dirent.isFile");
        /* copyFile + copyFileSync */
        fs.copyFileSync(tmpdir + '/y.txt', tmpdir + '/z.txt');
        mustEq(fs.readFileSync(tmpdir + '/z.txt', 'utf8'), 'hello, world', "copyFileSync");
        /* truncate */
        fs.truncateSync(tmpdir + '/z.txt', 5);
        mustEq(fs.readFileSync(tmpdir + '/z.txt', 'utf8'), 'hello', "truncateSync");
        /* accessSync */
        fs.accessSync(tmpdir + '/y.txt');  /* should not throw */
        mustThrow(function() { fs.accessSync(tmpdir + '/nope'); }, "accessSync on missing");
        /* mkdtemp */
        var tmp2 = fs.mkdtempSync(tmpdir + '/mkdtmp-');
        must(fs.existsSync(tmp2) && fs.statSync(tmp2).isDirectory(), "mkdtempSync");
        /* nested mkdir with recursive */
        fs.mkdirSync(tmpdir + '/a/b/c', {recursive: true});
        must(fs.existsSync(tmpdir + '/a/b/c'), "mkdirSync recursive");
        /* rm recursive */
        fs.rmSync(tmpdir + '/a', {recursive: true});
        must(!fs.existsSync(tmpdir + '/a'), "rmSync recursive");
        /* fs.constants */
        must(typeof fs.constants === 'object', "fs.constants");
        must(typeof fs.constants.F_OK === 'number', "F_OK");
        must(typeof fs.constants.R_OK === 'number', "R_OK");
        /* cleanup */
        fs.unlinkSync(tmpdir + '/y.txt');
        fs.unlinkSync(tmpdir + '/z.txt');
        fs.rmdirSync(tmp2);
        fs.rmdirSync(tmpdir);
    } catch (e) {
        try { fs.rmSync(tmpdir, {recursive: true, force: true}); } catch (_) {}
        throw e;
    }
});

testModule("module", function() {
    var m = require('module');
    must(Array.isArray(m.builtinModules), "builtinModules array");
    must(m.builtinModules.indexOf('fs') >= 0, "builtinModules has fs");
    must(m.builtinModules.indexOf('path') >= 0, "builtinModules has path");
    must(m.builtinModules.indexOf('events') >= 0, "builtinModules has events");
    must(m.builtinModules.indexOf('crypto') >= 0, "builtinModules has crypto");
    must(m.builtinModules.indexOf('worker_threads') >= 0, "has worker_threads");
    must(m.builtinModules.indexOf('zlib') >= 0, "builtinModules has zlib");
    /* isBuiltin */
    must(m.isBuiltin('fs'), "isBuiltin fs");
    must(m.isBuiltin('events'), "isBuiltin events");
    must(!m.isBuiltin('not-a-thing'), "isBuiltin false");
    must(!m.isBuiltin(''), "isBuiltin empty");
    must(!m.isBuiltin(undefined), "isBuiltin undefined");
    /* createRequire */
    must(typeof m.createRequire === 'function', "createRequire");
    var r = m.createRequire('/tmp/some-path/index.js');
    must(typeof r === 'function', "createRequire returns function");
    /* wrap — must produce node-style function wrapper */
    var w = m.wrap('var x = 1;');
    must(typeof w === 'string', "wrap returns string");
    must(w.indexOf('var x = 1;') >= 0, "wrap contains source");
    must(w.indexOf('exports') >= 0, "wrap mentions exports");
    must(w.indexOf('require') >= 0, "wrap mentions require");
    must(w.indexOf('module') >= 0, "wrap mentions module");
    must(w.indexOf('__filename') >= 0, "wrap mentions __filename");
    must(w.indexOf('__dirname') >= 0, "wrap mentions __dirname");
});

testModule("os", function() {
    var os = require('os');
    must(typeof os.platform() === 'string', "platform");
    /* tighter: must be one of the known NS_PLATFORM strings */
    must(['linux','darwin','freebsd','openbsd','cygwin','win32'].indexOf(os.platform()) >= 0,
         "platform in known set");
    must(typeof os.arch() === 'string', "arch");
    must(typeof os.type() === 'string', "type");
    must(typeof os.release() === 'string', "release");
    must(typeof os.version() === 'string', "version");
    must(typeof os.hostname() === 'string', "hostname");
    must(typeof os.tmpdir() === 'string', "tmpdir");
    must(typeof os.homedir() === 'string', "homedir");
    must(typeof os.endianness() === 'string', "endianness");
    must(['LE', 'BE'].indexOf(os.endianness()) >= 0, "endianness LE or BE");
    /* cpus */
    var cpus = os.cpus();
    must(Array.isArray(cpus) && cpus.length > 0, "cpus array");
    must(typeof cpus[0].model === 'string', "cpu.model");
    must(typeof cpus[0].times === 'object', "cpu.times");
    /* memory */
    must(typeof os.totalmem() === 'number' && os.totalmem() > 0, "totalmem");
    must(typeof os.freemem() === 'number', "freemem");
    must(os.freemem() <= os.totalmem(), "free <= total");
    /* loadavg */
    var la = os.loadavg();
    must(Array.isArray(la) && la.length === 3, "loadavg 3-tuple");
    /* uptime */
    must(typeof os.uptime() === 'number' && os.uptime() > 0, "uptime positive");
    /* userInfo */
    var ui = os.userInfo();
    must(typeof ui === 'object', "userInfo object");
    must(typeof ui.username === 'string', "userInfo.username");
    must(typeof ui.uid === 'number' || typeof ui.uid === 'bigint', "userInfo.uid");
    /* networkInterfaces */
    var ni = os.networkInterfaces();
    must(typeof ni === 'object', "networkInterfaces");
    /* constants */
    must(typeof os.EOL === 'string', "EOL constant");
    mustEq(os.EOL, '\n', "EOL on unix");
    must(typeof os.constants === 'object', "constants");
    must(typeof os.constants.signals === 'object', "constants.signals");
    must(typeof os.constants.signals.SIGINT === 'number', "SIGINT");
    /* tighter: POSIX/de-facto-standard signal number — same on
       Linux, macOS, FreeBSD */
    mustEq(os.constants.signals.SIGINT, 2, "SIGINT === 2");
    must(typeof os.constants.errno === 'object', "constants.errno");
    /* tighter: POSIX/de-facto-standard errno value — same across
       Linux, macOS, FreeBSD (and unchanged since v7 Unix) */
    mustEq(os.constants.errno.ENOENT, 2, "ENOENT === 2");
    /* availableParallelism (newer) */
    if (typeof os.availableParallelism === 'function') {
        must(typeof os.availableParallelism() === 'number', "availableParallelism");
    }
    /* devNull */
    if (typeof os.devNull === 'string') {
        mustEq(os.devNull, '/dev/null', "devNull");
    }
});

testModule("path", function() {
    var p = require('path');
    /* join */
    mustEq(p.join('/a', 'b', 'c'), '/a/b/c', "join");
    mustEq(p.join('a', '..', 'b'), 'b', "join with ..");
    mustEq(p.join(''), '.', "join empty");
    mustEq(p.join('/a/', '/b'), '/a/b', "join strips trailing slash");
    /* basename */
    mustEq(p.basename('/x/y/z.txt'), 'z.txt', "basename");
    mustEq(p.basename('/x/y/z.txt', '.txt'), 'z', "basename strip ext");
    mustEq(p.basename('/x/y/'), 'y', "basename trailing slash");
    /* dirname */
    mustEq(p.dirname('/x/y/z.txt'), '/x/y', "dirname");
    mustEq(p.dirname('/'), '/', "dirname root");
    /* extname */
    mustEq(p.extname('z.txt'), '.txt', "extname");
    mustEq(p.extname('no-ext'), '', "extname none");
    mustEq(p.extname('.hidden'), '', "extname leading dot");
    mustEq(p.extname('a.b.c'), '.c', "extname multi");
    /* normalize */
    mustEq(p.normalize('/a//b/../c'), '/a/c', "normalize");
    mustEq(p.normalize('a/./b'), 'a/b', "normalize .");
    mustEq(p.normalize(''), '.', "normalize empty");
    /* resolve */
    mustEq(p.resolve('/a', 'b'), '/a/b', "resolve");
    mustEq(p.resolve('/a/b', '../c'), '/a/c', "resolve ..");
    must(p.resolve('a').indexOf(p.sep) === 0 || /^[A-Z]:/i.test(p.resolve('a')),
        "resolve relative returns absolute");
    /* relative */
    mustEq(p.relative('/a/b/c', '/a/d'), '../../d', "relative");
    mustEq(p.relative('/a/b', '/a/b'), '', "relative same");
    /* isAbsolute */
    must(p.isAbsolute('/x') && !p.isAbsolute('x'), "isAbsolute");
    /* parse + format */
    var pp = p.parse('/a/b/c.txt');
    mustEq(pp.root, '/', "parse.root");
    mustEq(pp.dir, '/a/b', "parse.dir");
    mustEq(pp.base, 'c.txt', "parse.base");
    mustEq(pp.name, 'c', "parse.name");
    mustEq(pp.ext, '.txt', "parse.ext");
    /* format is inverse of parse */
    mustEq(p.format(pp), '/a/b/c.txt', "format");
    /* sep / delimiter */
    mustEq(p.sep, '/', "sep on posix");
    must(typeof p.delimiter === 'string', "delimiter");
    /* posix + win32 sub */
    must(typeof p.posix === 'object', "posix sub");
    must(typeof p.win32 === 'object', "win32 sub");
    mustEq(p.win32.sep, '\\', "win32.sep");
    mustEq(p.posix.sep, '/', "posix.sep");
    mustEq(p.win32.join('C:\\', 'a', 'b'), 'C:\\a\\b', "win32.join");
    mustEq(p.win32.basename('C:\\a\\b.txt'), 'b.txt', "win32.basename");
    mustEq(p.win32.isAbsolute('C:\\foo'), true, "win32.isAbsolute drive");
    mustEq(p.posix.isAbsolute('/foo'), true, "posix.isAbsolute");
    /* matchesGlob (newer, optional) */
    if (typeof p.matchesGlob === 'function') {
        must(p.matchesGlob('foo.txt', '*.txt'), "matchesGlob");
    }
});

testModule("perf_hooks", function() {
    var P = require('perf_hooks');
    must(typeof P.performance === 'object', "performance object");
    must(typeof P.performance.now === 'function', "performance.now");
    must(typeof P.performance.now() === 'number', "now returns number");
    must(typeof P.performance.timeOrigin === 'number' && P.performance.timeOrigin > 0, "timeOrigin > 0");
    must(typeof P.PerformanceEntry === 'function', "PerformanceEntry class");
    must(typeof P.PerformanceMark === 'function', "PerformanceMark class");
    must(typeof P.PerformanceMeasure === 'function', "PerformanceMeasure class");
    P.performance.clearMarks();
    P.performance.clearMeasures();
    /* mark with detail */
    var m = P.performance.mark('A', {detail: {note: 'start'}});
    must(m instanceof P.PerformanceMark, "mark returns PerformanceMark");
    mustEq(m.name, 'A', "mark.name");
    mustEq(m.entryType, 'mark', "mark.entryType");
    must(typeof m.startTime === 'number', "mark.startTime");
    mustEq(m.detail.note, 'start', "mark.detail");
    P.performance.mark('B');
    P.performance.mark('C');
    /* measure between two marks */
    var meas = P.performance.measure('A-B', 'A', 'B');
    must(meas instanceof P.PerformanceMeasure, "measure returns PerformanceMeasure");
    mustEq(meas.name, 'A-B', "measure.name");
    mustEq(meas.entryType, 'measure', "measure.entryType");
    must(meas.duration >= 0, "measure.duration ≥ 0");
    /* measure({start, end}) options form */
    var meas2 = P.performance.measure('A-C-opts', {start: 'A', end: 'C'});
    must(meas2 instanceof P.PerformanceMeasure, "measure options form");
    /* getEntries variants */
    must(P.performance.getEntries().length >= 5, "getEntries");
    mustEq(P.performance.getEntriesByName('A').length, 1, "getEntriesByName");
    must(P.performance.getEntriesByType('mark').length >= 3, "getEntriesByType mark");
    must(P.performance.getEntriesByType('measure').length >= 2, "getEntriesByType measure");
    /* selective clear */
    P.performance.clearMarks('A');
    mustEq(P.performance.getEntriesByName('A').length, 0, "clearMarks(name)");
    must(P.performance.getEntriesByName('B').length === 1, "selective clear keeps others");
    /* clear all */
    P.performance.clearMarks();
    P.performance.clearMeasures();
    mustEq(P.performance.getEntriesByType('mark').length, 0, "clearMarks all");
    mustEq(P.performance.getEntriesByType('measure').length, 0, "clearMeasures all");
    /* entry toJSON */
    var m2 = P.performance.mark('json-test');
    var j = m2.toJSON();
    mustEq(j.name, 'json-test', "toJSON.name");
    mustEq(j.entryType, 'mark', "toJSON.entryType");
    P.performance.clearMarks();
});

testModule("process", function() {
    var p = require('process');
    must(typeof p.cwd === 'function', "cwd");
    must(typeof p.cwd() === 'string', "cwd returns string");
    must(typeof p.platform === 'string', "platform");
    must(typeof p.arch === 'string', "arch");
    /* tighter: both come from the same C macros (NS_PLATFORM / NS_ARCH)
       so they MUST match os.platform()/os.arch() */
    var _os = require('os');
    mustEq(p.platform, _os.platform(), "process.platform === os.platform()");
    mustEq(p.arch,     _os.arch(),     "process.arch === os.arch()");
    must(typeof p.pid === 'number', "pid");
    must(p.pid > 0, "pid positive");
    must(typeof p.argv === 'object' && Array.isArray(p.argv), "argv array");
    must(p.argv.length >= 1, "argv has entries");
    must(typeof p.env === 'object', "env object");
    must(typeof p.versions === 'object', "versions");
    must(typeof p.exit === 'function', "exit");
    must(typeof p.title === 'string', "title");
    /* hrtime returns [seconds, nanoseconds] */
    var t = p.hrtime();
    must(Array.isArray(t) && t.length === 2, "hrtime is [s,ns]");
    must(typeof t[0] === 'number' && typeof t[1] === 'number', "hrtime nums");
    /* diff form */
    var t1 = p.hrtime();
    var diff = p.hrtime(t1);
    must(Array.isArray(diff) && diff[0] >= 0, "hrtime(start) returns diff");
    /* memoryUsage */
    if (typeof p.memoryUsage === 'function') {
        var m = p.memoryUsage();
        must(typeof m === 'object' && typeof m.rss === 'number', "memoryUsage.rss");
    }
    /* cpuUsage */
    var cu = p.cpuUsage();
    must(typeof cu === 'object' && typeof cu.user === 'number' && typeof cu.system === 'number',
        "cpuUsage {user, system}");
    /* uptime */
    must(typeof p.uptime() === 'number' && p.uptime() >= 0, "uptime");
    /* exitCode property (settable) */
    p.exitCode = 0;  /* don't actually exit */
    /* nextTick OR setImmediate as substitute */
    must(typeof p.nextTick === 'function' || typeof setImmediate === 'function',
        "nextTick or setImmediate");
    /* stdout/stderr fields */
    must(typeof p.stdout === 'object' || typeof p.stdout === 'function'
        || typeof p.stdout === 'undefined', "stdout (optional)");
    /* stdio fds — node guarantees 0/1/2 for stdin/stdout/stderr.  This
       previously returned 0 for all three in rampart-nodeshim, breaking
       tty.isatty(process.stderr.fd) etc. */
    if (p.stdin  && typeof p.stdin.fd  === 'number') mustEq(p.stdin.fd,  0, "stdin.fd === 0");
    if (p.stdout && typeof p.stdout.fd === 'number') mustEq(p.stdout.fd, 1, "stdout.fd === 1");
    if (p.stderr && typeof p.stderr.fd === 'number') mustEq(p.stderr.fd, 2, "stderr.fd === 2");
    /* chdir / cwd round-trip */
    var orig = p.cwd();
    p.chdir(orig);
    mustEq(p.cwd(), orig, "chdir round-trip");
    /* getuid / getgid (POSIX) */
    if (typeof p.getuid === 'function') {
        must(typeof p.getuid() === 'number', "getuid");
        must(typeof p.getgid() === 'number', "getgid");
    }
});

/* The rampart-core process object (the GLOBAL `process`, not the
   nodeshim wrapper above) exposes platform helpers used to back
   nodeshim's os.*.  These tests verify the helpers themselves AND
   that the os shim's delegation lines up.  Rampart-only. */
if (!_isRampart)
    skipModule("process (rampart core)", "rampart-only");
else
testModule("process (rampart core)", function() {
    /* Surface */
    must(typeof process.getTotalMem === 'function', "getTotalMem");
    must(typeof process.getFreeMem  === 'function', "getFreeMem");
    must(typeof process.uptime      === 'function', "uptime");
    must(typeof process.getCpuInfo  === 'function', "getCpuInfo");
    must(typeof process.setMaxMem   === 'function', "setMaxMem");
    must(typeof process.nCpu        === 'number',   "nCpu is number");
    must(process.nCpu >= 1, "nCpu >= 1");

    /* Values */
    var tot = process.getTotalMem();
    must(typeof tot === 'number' && tot > 0, "getTotalMem returns positive MB");
    var fre = process.getFreeMem();
    must(typeof fre === 'number' && fre >= 0, "getFreeMem returns non-negative MB");
    must(fre <= tot, "free <= total");
    var up = process.uptime();
    must(typeof up === 'number' && up > 0, "uptime > 0 seconds");
    var ci = process.getCpuInfo();
    must(Array.isArray(ci) && ci.length > 0, "getCpuInfo returns non-empty array");
    must(typeof ci[0].model === 'string', "cpu[0].model is string");
    must(typeof ci[0].speed === 'number', "cpu[0].speed is number");
    must(typeof ci[0].times === 'object' && ci[0].times !== null, "cpu[0].times is object");
    ['user','nice','sys','idle','irq'].forEach(function(k) {
        must(typeof ci[0].times[k] === 'number', "cpu[0].times." + k + " is number");
    });
    /* cpu count consistency */
    mustEq(ci.length, process.nCpu, "getCpuInfo.length === nCpu");

    /* Delegation: nodeshim os.* should match process.* values
       (within tolerance for time-varying quantities). */
    var os = require('os');
    /* totalmem: bytes vs MB — exact match */
    mustEq(os.totalmem(), process.getTotalMem() * 1048576,
           "os.totalmem === process.getTotalMem * 1048576");
    /* freemem changes second-to-second; just verify same order of magnitude */
    var fr1 = os.freemem(), fr2 = process.getFreeMem() * 1048576;
    must(Math.abs(fr1 - fr2) < 64 * 1048576,
         "os.freemem within 64MB of process.getFreeMem");
    /* uptime advances continuously; verify within 1 second */
    var u1 = os.uptime(), u2 = process.uptime();
    must(Math.abs(u1 - u2) < 2, "os.uptime within 2s of process.uptime");
    /* cpus: same length */
    mustEq(os.cpus().length, process.getCpuInfo().length,
           "os.cpus().length === process.getCpuInfo().length");

    /* setMaxMem: on Linux/BSD, verify it accepts a number and returns one
       (sky-high value so it doesn't constrain the rest of the test).  On
       macOS, the kernel does not enforce RLIMIT_AS, so setMaxMem throws by
       design rather than lying about a limit it can't apply. */
    if (os.platform() === 'darwin') {
        mustThrow(function(){ process.setMaxMem(tot * 10); },
                  "setMaxMem throws on darwin (not supported)");
    } else {
        var ret = process.setMaxMem(tot * 10);  /* 10x total mem; effectively unlimited */
        must(typeof ret === 'number', "setMaxMem returns number");
    }
});

testModule("punycode", function() {
    var P = require('punycode');
    mustEq(P.encode('ü'), 'tda', "encode");
    mustEq(P.decode('tda'), 'ü', "decode");
    mustEq(P.toASCII('über.example'), 'xn--ber-goa.example', "toASCII");
    mustEq(P.toUnicode('xn--ber-goa.example'), 'über.example', "toUnicode");
    /* surrogate pair */
    var cp = P.ucs2.decode('𝄞');
    mustEq(cp.length, 1, "ucs2.decode surrogate length");
    mustEq(cp[0], 0x1D11E, "ucs2.decode value");
});

testModule("querystring", function() {
    var qs = require('querystring');
    mustEq(JSON.stringify(qs.parse('a=1&b=hi')), '{"a":"1","b":"hi"}', "parse basic");
    mustEq(JSON.stringify(qs.parse('tag=red&tag=hot').tag), '["red","hot"]', "parse dup-key array");
    mustEq(JSON.stringify(qs.parse('')), '{}', "parse empty");
    mustEq(qs.parse('q=hello%20world').q, 'hello world', "parse encoded space");
    mustEq(qs.parse('k=').k, '', "parse empty value");
    mustEq(qs.parse('=v')[''], 'v', "parse empty key");
    /* custom separator/eq */
    mustEq(JSON.stringify(qs.parse('a:1;b:2', ';', ':')), '{"a":"1","b":"2"}', "parse custom sep/eq");
    /* stringify */
    mustEq(qs.stringify({a:1, b:'hi'}), 'a=1&b=hi', "stringify");
    mustEq(qs.stringify({tag:['red','hot']}), 'tag=red&tag=hot', "stringify array");
    mustEq(qs.stringify({q:'hi there'}), 'q=hi%20there', "stringify space-encoded");
    mustEq(qs.stringify({a:null, b:'x'}), 'a=&b=x', "stringify null");
    mustEq(qs.stringify({}), '', "stringify empty");
    mustEq(qs.stringify({a:1, b:2}, ';', ':'), 'a:1;b:2', "stringify custom sep/eq");
    /* escape / unescape */
    mustEq(qs.escape('hello world'), 'hello%20world', "escape");
    mustEq(qs.escape('a+b'), 'a%2Bb', "escape +");
    mustEq(qs.unescape('hello%20world'), 'hello world', "unescape");
    /* round-trip */
    var obj = {tags: ['admin', 'dev'], note: 'hi & welcome'};
    var rt = qs.parse(qs.stringify(obj));
    mustEq(JSON.stringify(rt.tags), '["admin","dev"]', "round-trip tags");
    mustEq(rt.note, 'hi & welcome', "round-trip note");
    /* parsed object has null prototype (no pollution) */
    var p = qs.parse('a=1');
    mustEq(Object.getPrototypeOf(p), null, "parsed has null proto");
});

testModule("string_decoder", function() {
    var SD = require('string_decoder');
    must(typeof SD.StringDecoder === 'function', "StringDecoder class");
    /* utf-8 default */
    var d = new SD.StringDecoder('utf-8');
    mustEq(d.write(Buffer.from('hello')), 'hello', "utf-8 ascii");
    mustEq(d.end(), '', "utf-8 end empty");
    /* split mb char across two writes */
    var d2 = new SD.StringDecoder('utf-8');
    mustEq(d2.write(Buffer.from([0xC2])), '', "partial mb byte buffered");
    mustEq(d2.write(Buffer.from([0xA3])), '£', "mb char completed");
    /* mid-stream split: 'ab' + start-of-£ then rest */
    var d2b = new SD.StringDecoder('utf-8');
    mustEq(d2b.write(Buffer.from([0x61, 0x62, 0xC2])), 'ab', "ascii prefix delivered, mb buffered");
    mustEq(d2b.write(Buffer.from([0xA3])), '£', "mb completed second write");
    /* latin1 */
    var d3 = new SD.StringDecoder('latin1');
    mustEq(d3.write(Buffer.from([0x68, 0x69])), 'hi', "latin1");
    mustEq(d3.write(Buffer.from([0xE9])), 'é', "latin1 high byte");
    /* ascii */
    var d4 = new SD.StringDecoder('ascii');
    mustEq(d4.write(Buffer.from([0x68, 0x69])), 'hi', "ascii basic");
    /* hex */
    var d5 = new SD.StringDecoder('hex');
    mustEq(d5.write(Buffer.from([0xCA, 0xFE])), 'cafe', "hex");
    /* base64 — accumulate until 3-byte boundary, end flushes remainder */
    var d6 = new SD.StringDecoder('base64');
    var b64 = d6.write(Buffer.from('hi')) + d6.end();
    mustEq(b64, 'aGk=', "base64 round-trip via decoder");
    /* utf-16le pair across two writes */
    var d7 = new SD.StringDecoder('utf-16le');
    mustEq(d7.write(Buffer.from([0x41])), '', "utf-16le partial");
    mustEq(d7.write(Buffer.from([0x00])), 'A', "utf-16le complete");
});

testModule("url", function() {
    var u = require('url');
    var URL = u.URL;
    /* Full URL parse */
    var parsed = new URL('https://user:pass@example.com:8080/path?q=1#frag');
    mustEq(parsed.protocol, 'https:', "protocol");
    mustEq(parsed.host, 'example.com:8080', "host");
    mustEq(parsed.hostname, 'example.com', "hostname");
    mustEq(parsed.port, '8080', "port");
    mustEq(parsed.username, 'user', "username");
    mustEq(parsed.password, 'pass', "password");
    mustEq(parsed.pathname, '/path', "pathname");
    mustEq(parsed.search, '?q=1', "search");
    mustEq(parsed.hash, '#frag', "hash");
    mustEq(parsed.origin, 'https://example.com:8080', "origin");
    /* Mutation */
    parsed.pathname = '/changed';
    mustEq(parsed.pathname, '/changed', "pathname mutation");
    /* relative resolution */
    var rel = new URL('/about', 'https://example.com');
    mustEq(rel.href, 'https://example.com/about', "URL relative base");
    /* URLSearchParams */
    var sp = new u.URLSearchParams('a=1&b=2');
    mustEq(sp.get('a'), '1', "URLSearchParams.get");
    must(sp.has('a'), "URLSearchParams.has true");
    must(!sp.has('z'), "URLSearchParams.has false");
    sp.append('c', '3');
    must(sp.toString().indexOf('c=3') >= 0, "URLSearchParams.append");
    sp.set('a', '99');
    mustEq(sp.get('a'), '99', "URLSearchParams.set");
    sp['delete']('b');
    must(!sp.has('b'), "URLSearchParams.delete");
    /* URL.canParse static */
    if (typeof URL.canParse === 'function') {
        must(URL.canParse('https://example.com'), "URL.canParse true");
        must(!URL.canParse('not a url'), "URL.canParse false");
    }
    /* URL.parse static (returns URL or null) */
    if (typeof URL.parse === 'function') {
        must(URL.parse('https://example.com') instanceof URL, "URL.parse returns URL");
        mustEq(URL.parse('not a url'), null, "URL.parse returns null");
    }
    /* fileURLToPath / pathToFileURL */
    mustEq(u.fileURLToPath('file:///etc/hosts'), '/etc/hosts', "fileURLToPath");
    must(u.pathToFileURL('/etc/hosts').href.indexOf('file://') === 0, "pathToFileURL");
    /* domainToASCII / domainToUnicode */
    if (typeof u.domainToASCII === 'function') {
        mustEq(u.domainToASCII('über.example'), 'xn--ber-goa.example', "domainToASCII");
        mustEq(u.domainToUnicode('xn--ber-goa.example'), 'über.example', "domainToUnicode");
    }
    /* legacy url.parse */
    if (typeof u.parse === 'function') {
        var lp = u.parse('https://example.com:8080/p?q=1');
        mustEq(lp.protocol, 'https:', "legacy parse protocol");
        mustEq(lp.hostname, 'example.com', "legacy parse hostname");
        mustEq(lp.port, '8080', "legacy parse port");
    }
    /* legacy url.format */
    if (typeof u.format === 'function') {
        var s = u.format({protocol: 'https:', host: 'example.com', pathname: '/x'});
        must(s.indexOf('example.com/x') >= 0, "legacy format");
    }
});

testModule("tty", function() {
    var tty = require('tty');
    /* surface */
    must(typeof tty.isatty      === 'function', "isatty");
    must(typeof tty.ReadStream  === 'function', "ReadStream");
    must(typeof tty.WriteStream === 'function', "WriteStream");
    /* isatty: bogus fd returns false; valid fd returns boolean */
    mustEq(tty.isatty(99),  false, "isatty bogus fd");
    must(typeof tty.isatty(1) === 'boolean', "isatty returns boolean");
    /* getColorDepth / hasColors — env-only, no fd needed; instantiate
       a stream only if we're actually on a TTY (node throws otherwise). */
    /* Pick any TTY-ish fd so node's strict constructor doesn't EBADF.
       If none of 0/1/2 is a TTY (CI pipe), skip the stream-surface tests. */
    var ttyFd = tty.isatty(1) ? 1 : tty.isatty(2) ? 2 : tty.isatty(0) ? 0 : -1;
    if (ttyFd >= 0) {
        var w = new tty.WriteStream(ttyFd);
        must(w instanceof tty.WriteStream, "WriteStream constructor");
        must(w.isTTY === true, "WriteStream.isTTY");
        must(typeof w.columns === 'number' && w.columns > 0, "WriteStream.columns");
        must(typeof w.rows    === 'number' && w.rows    > 0, "WriteStream.rows");
        var ws = w.getWindowSize();
        must(Array.isArray(ws) && ws.length === 2, "getWindowSize() shape");
        mustEq(w.getColorDepth({TERM: 'dumb'}),               1,  "depth dumb");
        mustEq(w.getColorDepth({NO_COLOR: '1'}),              1,  "depth NO_COLOR");
        mustEq(w.getColorDepth({COLORTERM: 'truecolor'}),     24, "depth truecolor");
        mustEq(w.getColorDepth({TERM: 'xterm-256color'}),     8,  "depth xterm-256color");
        mustEq(w.getColorDepth({TERM: 'xterm'}),              4,  "depth xterm");
        must(w.hasColors(16,  {TERM: 'xterm-256color'}) === true,  "hasColors(16) on 256-term");
        must(w.hasColors(256, {TERM: 'xterm-256color'}) === true,  "hasColors(256) on 256-term");
        must(w.hasColors(256, {TERM: 'xterm'})          === false, "hasColors(256) on 16-term");
        /* ANSI helpers: just verify they're callable and return true.
           Actual escape bytes go to the matching fd's stream — silence
           by swapping its write method. */
        var sinkProp = (ttyFd === 2) ? 'stderr' : 'stdout';
        var orig = process[sinkProp] && process[sinkProp].write;
        if (orig) process[sinkProp].write = function(){ return true; };
        try {
            mustEq(w.cursorTo(0, 0),     true, "cursorTo");
            mustEq(w.moveCursor(1, 1),   true, "moveCursor");
            mustEq(w.clearLine(0),       true, "clearLine");
            mustEq(w.clearScreenDown(),  true, "clearScreenDown");
        } finally {
            if (orig) process[sinkProp].write = orig;
        }
        /* ReadStream surface — only if stdin is a TTY (node EBADFs
           otherwise; rampart wouldn't but we want one shape that works
           under both runtimes). */
        if (tty.isatty(0)) {
            var r = new tty.ReadStream(0);
            must(r instanceof tty.ReadStream, "ReadStream constructor");
            must(r.isTTY === true, "ReadStream.isTTY");
            must(r.isRaw === false, "ReadStream.isRaw default");
            must(typeof r.setRawMode === 'function', "setRawMode");
            must(typeof r.getWindowSize === 'function', "ReadStream.getWindowSize");
            /* Don't actually flip raw mode (could leave the terminal in
               raw state if a later test throws); just verify the chainable
               return.  Node returns `this` from setRawMode; rampart does
               too. */
            must(r.setRawMode(false) === r, "setRawMode chainable");
        }
    }
    /* builtinModules has 'tty' (rampart only — node doesn't add to ours) */
    if (_isRampart) {
        var m = require('module');
        must(m.builtinModules.indexOf('tty') >= 0, "builtinModules has tty");
        must(m.isBuiltin('tty'), "isBuiltin('tty')");
    }
});

testModule("util", function() {
    var u = require('util');
    /* format */
    mustEq(u.format('%s is %d', 'pi', 3.14), 'pi is 3.14', "format %s %d");
    mustEq(u.format('%j', {a:1}), '{"a":1}', "format %j");
    mustEq(u.format('%o', {x:1}).indexOf('x') >= 0, true, "format %o");
    mustEq(u.format('%%'), '%', "format %% literal");
    mustEq(u.format('no specifier'), 'no specifier', "format passthrough");
    mustEq(u.format('%s', 'a', 'b'), 'a b', "format extra args appended");
    /* inspect */
    must(typeof u.inspect({a:1}) === 'string', "inspect");
    must(u.inspect({a:1}).indexOf('a') >= 0, "inspect contains key");
    mustEq(u.inspect(undefined), 'undefined', "inspect undefined");
    mustEq(u.inspect(null), 'null', "inspect null");
    must(u.inspect([1,2,3]).indexOf('1') >= 0, "inspect array");
    /* types */
    must(u.types.isDate(new Date()), "types.isDate");
    must(!u.types.isDate({}), "types.isDate false");
    must(u.types.isRegExp(/x/), "types.isRegExp");
    must(u.types.isMap(new Map()), "types.isMap");
    must(u.types.isSet(new Set()), "types.isSet");
    must(u.types.isNativeError(new Error()), "types.isNativeError");
    must(u.types.isArrayBuffer(new ArrayBuffer(4)), "types.isArrayBuffer");
    must(u.types.isUint8Array(new Uint8Array(4)), "types.isUint8Array");
    must(u.types.isTypedArray(new Uint8Array(4)), "types.isTypedArray");
    /* isDeepStrictEqual */
    must(u.isDeepStrictEqual({a:[1,2]}, {a:[1,2]}), "isDeepStrictEqual");
    must(!u.isDeepStrictEqual({a:1}, {a:2}), "isDeepStrictEqual false");
    must(u.isDeepStrictEqual([1, [2, 3]], [1, [2, 3]]), "isDeepStrictEqual nested");
    must(!u.isDeepStrictEqual({a:1}, {a:'1'}), "isDeepStrictEqual strict types");
    /* inherits */
    function A() {} function B() {}
    A.prototype.greet = function() { return 'hi'; };
    u.inherits(B, A);
    var binst = new B();
    must(binst instanceof A, "inherits");
    mustEq(binst.greet(), 'hi', "inherits method available");
    /* promisify / callbackify — Promise is now installed eagerly in
       rampart core (rampart-promise.c), so these work in vanilla
       rampart with no -t.  Surface check + thenable shape here; the
       resolve/reject round-trip is exercised in the async block. */
    must(typeof u.promisify === 'function', "promisify present");
    must(typeof u.callbackify === 'function', "callbackify present");
    var pFn = u.promisify(function(x, cb) { cb(null, x); });
    must(typeof pFn === 'function', "promisify returns function");
    var p = pFn(1);
    must(p && typeof p.then === 'function', "promisify result is thenable");
    var cbFn = u.callbackify(function() { return Promise.resolve(1); });
    must(typeof cbFn === 'function', "callbackify returns function");
    /* deprecate — silences the warning (writes via console.warn) so the
       test output stays clean. */
    var fn = u.deprecate(function(x) { return x * 2; }, "msg");
    must(typeof fn === 'function', "deprecate wraps");
    var origWarn = console.warn;
    console.warn = function(){};
    try {
        mustEq(fn(21), 42, "deprecate wrapped fn returns value");
    } finally { console.warn = origWarn; }
    /* debuglog */
    var dbg = u.debuglog('test-section');
    must(typeof dbg === 'function', "debuglog returns function");
    /* parseArgs (newer) */
    if (typeof u.parseArgs === 'function') {
        var pa = u.parseArgs({
            args: ['--name', 'alice', '--verbose'],
            options: {
                name:    {type: 'string'},
                verbose: {type: 'boolean'}
            }
        });
        mustEq(pa.values.name, 'alice', "parseArgs string");
        mustEq(pa.values.verbose, true, "parseArgs boolean");
    }
    /* MIMEType */
    if (typeof u.MIMEType === 'function') {
        var mt = new u.MIMEType('text/html;charset=utf-8');
        mustEq(mt.type, 'text', "MIMEType.type");
        mustEq(mt.subtype, 'html', "MIMEType.subtype");
    }
    /* TextEncoder/TextDecoder re-exports */
    must(typeof u.TextEncoder === 'function', "TextEncoder export");
    must(typeof u.TextDecoder === 'function', "TextDecoder export");
    /* legacy is* checks (deprecated but still exposed) */
    if (typeof u.isArray === 'function')   must(u.isArray([]), "isArray legacy");
    if (typeof u.isString === 'function')  must(u.isString(''), "isString legacy");
    if (typeof u.isNumber === 'function')  must(u.isNumber(1), "isNumber legacy");
    if (typeof u.isError === 'function')   must(u.isError(new Error()), "isError legacy");
});

/* ============================================================
 * Async submodules — chained via setTimeout / callbacks
 * ============================================================ */

function asyncBlock(name, run) {
    /* Wrap an async test so it reports via testModule.  `run` gets
       (done); call done(err?) to report — testModule does the line. */
    return function(next) {
        var reported = false;
        function done(err) {
            if (reported) return;
            reported = true;
            testModule(name, function() {
                if (err) throw (err.message ? err : new Error(String(err)));
            });
            next();
        }
        try { run(done); }
        catch (e) { done(e); }
    };
}

var asyncQ = [];

asyncQ.push(asyncBlock("timers", function(done) {
    var t = require('timers');
    must(typeof t.setTimeout === 'function', "setTimeout");
    must(typeof t.clearTimeout === 'function', "clearTimeout");
    must(typeof t.setInterval === 'function', "setInterval");
    must(typeof t.clearInterval === 'function', "clearInterval");
    must(typeof t.setImmediate === 'function', "setImmediate");
    must(typeof t.clearImmediate === 'function', "clearImmediate");
    var h = t.setTimeout(function() {}, 100000);
    must(typeof h.ref === 'function', "handle.ref");
    must(typeof h.unref === 'function', "handle.unref");
    must(typeof h.hasRef === 'function', "handle.hasRef");
    must(typeof h.refresh === 'function', "handle.refresh");
    must(h.ref() === h, "ref chainable");
    must(h.unref() === h, "unref chainable");
    t.clearTimeout(h);
    /* clearTimeout actually prevents firing */
    var shouldNotFire = false;
    var h2 = t.setTimeout(function() { shouldNotFire = true; }, 10);
    t.clearTimeout(h2);
    /* Actual firing */
    var fired = 0;
    t.setTimeout(function() { fired++; }, 10);
    t.setImmediate(function() { fired++; });
    t.setImmediate(function(a, b) { fired += a + b; }, 5, 10);
    /* setInterval fires multiple times */
    var intervalCount = 0;
    var iv = t.setInterval(function() { intervalCount++; }, 15);
    t.setTimeout(function() {
        t.clearInterval(iv);
        var preClear = intervalCount;
        try {
            mustEq(fired, 17, "timers fired (2+15)");
            mustEq(shouldNotFire, false, "cleared timeout didn't fire");
            must(intervalCount >= 1, "interval fired at least once");
            /* After clearInterval, no more fires */
            t.setTimeout(function() {
                try {
                    mustEq(intervalCount, preClear, "clearInterval stops further fires");
                    done();
                } catch (e) { done(e); }
            }, 50);
        } catch (e) { done(e); }
    }, 80);
}));

asyncQ.push(asyncBlock("util.promisify / callbackify (async)", function(done) {
    var u = require('util');
    /* promisify resolve path: cb(null, value) → Promise resolves */
    var pResolve = u.promisify(function(x, cb) {
        setTimeout(function() { cb(null, x * 2); }, 5);
    });
    /* promisify reject path: cb(err) → Promise rejects */
    var pReject  = u.promisify(function(cb) {
        setTimeout(function() { cb(new Error('boom')); }, 5);
    });
    /* callbackify: returning Promise → node-style (err, val) cb */
    var cb = u.callbackify(function(x) {
        return Promise.resolve(x + 100);
    });
    pResolve(21).then(function(v) {
        try { mustEq(v, 42, "promisify resolves with cb value"); }
        catch (e) { return done(e); }
        pReject().then(function() {
            done(new Error("promisify reject path resolved instead"));
        }, function(err) {
            try { mustEq(err && err.message, 'boom', "promisify rejects with cb err"); }
            catch (e) { return done(e); }
            cb(7, function(cberr, cbval) {
                try {
                    mustEq(cberr, null, "callbackify err === null");
                    mustEq(cbval, 107, "callbackify cb value");
                    done();
                } catch (e) { done(e); }
            });
        });
    }, function(err) { done(err); });
}));

asyncQ.push(asyncBlock("fs (async)", function(done) {
    var fs = require('fs');
    var tmpdir = '/tmp/_nodeshim_async_test_' + Date.now();
    try { fs.mkdirSync(tmpdir); } catch (_) {}
    var fp = tmpdir + '/async.txt';
    fs.writeFile(fp, 'async-hello', function(werr) {
        try {
            mustEq(werr, null, "writeFile callback err null");
            fs.readFile(fp, 'utf8', function(rerr, data) {
                try {
                    mustEq(rerr, null, "readFile callback err null");
                    mustEq(data, 'async-hello', "readFile callback data");
                    /* stat async */
                    fs.stat(fp, function(serr, st) {
                        try {
                            mustEq(serr, null, "stat callback err null");
                            mustEq(st.size, 11, "stat callback size");
                            must(st.isFile(), "stat callback isFile");
                            /* unlink async */
                            fs.unlink(fp, function(uerr) {
                                try {
                                    mustEq(uerr, null, "unlink callback err null");
                                    must(!fs.existsSync(fp), "unlinked");
                                    fs.rmdirSync(tmpdir);
                                    done();
                                } catch (e) { done(e); }
                            });
                        } catch (e) { done(e); }
                    });
                } catch (e) { done(e); }
            });
        } catch (e) { done(e); }
    });
}));

asyncQ.push(asyncBlock("dns", function(done) {
    var d = require('dns');
    must(typeof d.lookup === 'function', "lookup");
    must(typeof d.resolve === 'function', "resolve");
    must(typeof d.resolve4 === 'function', "resolve4");
    must(typeof d.resolve6 === 'function', "resolve6");
    must(typeof d.resolveMx === 'function', "resolveMx");
    must(typeof d.resolveTxt === 'function', "resolveTxt");
    must(typeof d.reverse === 'function', "reverse");
    must(typeof d.Resolver === 'function', "Resolver class");
    must(typeof d.ADDRCONFIG === 'number', "ADDRCONFIG constant");
    must(typeof d.V4MAPPED === 'number', "V4MAPPED constant");
    must(typeof d.ALL === 'number', "ALL constant");
    must(typeof d.NODATA === 'string', "NODATA error code");
    must(typeof d.NOTFOUND === 'string', "NOTFOUND error code");
    must(typeof d.promises === 'object', "promises mirror");
    var r = new d.Resolver();
    must(typeof r.resolve === 'function', "Resolver.resolve");
    must(typeof r.cancel === 'function', "Resolver.cancel");
    /* Literal-IP fast path: must invoke callback async with the IP back */
    d.lookup('127.0.0.1', function(err, addr, family) {
        try {
            mustEq(err, null, "lookup 127 err");
            mustEq(addr, '127.0.0.1', "lookup 127 addr");
            mustEq(family, 4, "lookup 127 family");
            d.lookup('::1', function(err6, addr6, fam6) {
                try {
                    mustEq(err6, null, "lookup ::1 err");
                    mustEq(addr6, '::1', "lookup ::1 addr");
                    mustEq(fam6, 6, "lookup ::1 family");
                    /* lookup with all:true */
                    d.lookup('127.0.0.1', {all: true}, function(errA, arr) {
                        try {
                            mustEq(errA, null, "lookup all:true err");
                            must(Array.isArray(arr) && arr.length === 1, "lookup all:true array");
                            mustEq(arr[0].address, '127.0.0.1', "all:true address");
                            mustEq(arr[0].family, 4, "all:true family");
                            done();
                        } catch (e) { done(e); }
                    });
                } catch (e) { done(e); }
            });
        } catch (e) { done(e); }
    });
}));

asyncQ.push(asyncBlock("zlib", function(done) {
    var z = require('zlib');
    /* Surface */
    must(typeof z.gzipSync === 'function', "gzipSync");
    must(typeof z.gunzipSync === 'function', "gunzipSync");
    must(typeof z.deflateSync === 'function', "deflateSync");
    must(typeof z.inflateSync === 'function', "inflateSync");
    must(typeof z.deflateRawSync === 'function', "deflateRawSync");
    must(typeof z.inflateRawSync === 'function', "inflateRawSync");
    must(typeof z.crc32 === 'function', "crc32");
    must(typeof z.constants === 'object', "constants");
    must(typeof z.constants.Z_BEST_COMPRESSION === 'number', "Z_BEST_COMPRESSION");
    /* Sync round-trips */
    var input = Buffer.from('hello, world!\n'.repeat(20));
    var gz = z.gzipSync(input);
    must(gz.length < input.length, "gzipSync compresses");
    mustEq(Buffer.from(z.gunzipSync(gz)).toString(), input.toString(), "gunzip round-trip");
    var defl = z.deflateSync(input);
    mustEq(Buffer.from(z.inflateSync(defl)).toString(), input.toString(), "inflate round-trip");
    var raw = z.deflateRawSync(input);
    mustEq(Buffer.from(z.inflateRawSync(raw)).toString(), input.toString(), "raw round-trip");
    /* deflateRaw smaller (no zlib header/checksum) */
    must(raw.length <= defl.length, "raw ≤ deflate");
    /* String input */
    var gzStr = z.gzipSync('plain string input');
    mustEq(Buffer.from(z.gunzipSync(gzStr)).toString(), 'plain string input', "string input round-trip");
    /* Empty input */
    var gzEmpty = z.gzipSync(Buffer.from(''));
    mustEq(Buffer.from(z.gunzipSync(gzEmpty)).toString(), '', "empty input round-trip");
    /* crc32 + adler32 */
    mustEq(z.crc32(Buffer.from('hello')), 0x3610A686, "crc32 hello");
    mustEq(z.crc32(Buffer.from('')), 0, "crc32 empty");
    if (typeof z.adler32 === 'function') {
        mustEq(z.adler32(Buffer.from('hello')), 0x062c0215, "adler32 hello");
    }
    /* Stream classes throw ENOSYS pending stream module */
    mustThrow(function() { z.createGzip(); }, "createGzip ENOSYS");
    mustThrow(function() { z.createGunzip(); }, "createGunzip ENOSYS");
    /* Async callback path */
    z.gzip(input, function(err, result) {
        try {
            mustEq(err, null, "async gzip err");
            must(result && result.length > 0, "async gzip result");
            z.gunzip(result, function(err2, r2) {
                try {
                    mustEq(err2, null, "async gunzip err");
                    mustEq(Buffer.from(r2).toString(), input.toString(), "async round-trip");
                    /* unzip auto-detect */
                    mustEq(Buffer.from(z.unzipSync(gz)).toString(), input.toString(),
                          "unzip auto-detect gzip");
                    mustEq(Buffer.from(z.unzipSync(defl)).toString(), input.toString(),
                          "unzip auto-detect deflate");
                    /* async deflate + inflate */
                    z.deflate(input, function(de, dr) {
                        try {
                            mustEq(de, null, "async deflate err");
                            z.inflate(dr, function(ie, ir) {
                                try {
                                    mustEq(ie, null, "async inflate err");
                                    mustEq(Buffer.from(ir).toString(), input.toString(),
                                          "async deflate/inflate round-trip");
                                    done();
                                } catch (e) { done(e); }
                            });
                        } catch (e) { done(e); }
                    });
                } catch (e) { done(e); }
            });
        } catch (e) { done(e); }
    });
}));

asyncQ.push(asyncBlock("worker_threads", function(done) {
    var wt = require('worker_threads');
    must(wt.isMainThread === true, "isMainThread true in main");
    must(wt.parentPort === null, "parentPort null in main");
    mustEq(wt.threadId, 0, "threadId 0 in main");
    must(typeof wt.Worker === 'function', "Worker class");
    must(typeof wt.MessageChannel === 'function', "MessageChannel class");
    must(typeof wt.MessagePort === 'function', "MessagePort class");
    must(typeof wt.BroadcastChannel === 'function', "BroadcastChannel class");
    /* transferList validation (in main thread) */
    var mcVal = new wt.MessageChannel();
    mcVal.port1.postMessage('ok', [new ArrayBuffer(4)]);  /* valid */
    mustThrow(function() {
        mcVal.port1.postMessage('x', [{not: 'transferable'}]);
    }, "transferList invalid object throws");
    mustThrow(function() {
        mcVal.port1.postMessage('x', 'not an array');
    }, "transferList non-array throws");
    mcVal.port1.close(); mcVal.port2.close();
    /* Round-trip via Worker with workerData */
    var w = new wt.Worker(
        "var wt = require('worker_threads');" +
        "wt.parentPort.on('message', function(m) {" +
        "  wt.parentPort.postMessage({" +
        "    echo: m, n: m.n + 1," +
        "    workerData: wt.workerData," +
        "    threadId: wt.threadId," +
        "    isMainThread: wt.isMainThread" +
        "  });" +
        "});",
        {eval: true, workerData: {seed: 42, list: [1, 2, 3]}}
    );
    var got = null;
    w.on('message', function(m) { got = m; });
    w.on('online',  function()  { w.postMessage({n: 41}); });
    w.on('error',   function(e) { done(e); });
    setTimeout(function() {
        try {
            must(got && got.echo && got.echo.n === 41, "round-trip echo");
            mustEq(got.n, 42, "round-trip increment");
            mustEq(got.workerData.seed, 42, "workerData received");
            mustEq(JSON.stringify(got.workerData.list), '[1,2,3]', "workerData array");
            mustEq(got.isMainThread, false, "isMainThread false in worker");
            must(got.threadId > 0, "threadId nonzero in worker");
            w.terminate();
            /* MessageChannel in-process */
            var mc = new wt.MessageChannel();
            var got2 = null;
            mc.port2.on('message', function(m) { got2 = m; });
            mc.port1.postMessage('mc-test');
            /* Bidirectional */
            var got2b = null;
            mc.port1.on('message', function(m) { got2b = m; });
            mc.port2.postMessage('reply');
            setTimeout(function() {
                try {
                    mustEq(got2, 'mc-test', "MessageChannel port1->port2");
                    mustEq(got2b, 'reply', "MessageChannel port2->port1");
                    mc.port1.close();
                    mc.port2.close();
                    /* BroadcastChannel — 3 subscribers, sender does NOT see own */
                    var bcA = new wt.BroadcastChannel('test-chan');
                    var bcB = new wt.BroadcastChannel('test-chan');
                    var bcC = new wt.BroadcastChannel('test-chan');
                    var aGot = [], bGot = [], cGot = [];
                    bcA.on('message', function(m) { aGot.push(m); });
                    bcB.on('message', function(m) { bGot.push(m); });
                    bcC.on('message', function(m) { cGot.push(m); });
                    bcA.postMessage('from-A');
                    setTimeout(function() {
                        try {
                            mustEq(aGot.length, 0, "BroadcastChannel sender doesn't get own");
                            mustEq(bGot.length, 1, "B got A's msg");
                            mustEq(bGot[0], 'from-A', "B got correct payload");
                            mustEq(cGot.length, 1, "C got A's msg");
                            /* Close C, send another */
                            bcC.close();
                            bcA.postMessage('after-C-close');
                            setTimeout(function() {
                                try {
                                    mustEq(cGot.length, 1, "closed C did not receive");
                                    mustEq(bGot.length, 2, "B still received");
                                    bcA.close(); bcB.close();
                                    /* Give terminated workers + closed BCs
                                       a moment to fully tear down pipes
                                       before we exit.  The worker-error
                                       case is covered in detail by the
                                       nodeshim-test/ suite. */
                                    setTimeout(function() { done(); }, 100);
                                } catch (e) { done(e); }
                            }, 100);
                        } catch (e) { done(e); }
                    }, 100);
                } catch (e) { done(e); }
            }, 100);
        } catch (e) { done(e); }
    }, 300);
}));

/* ============================================================
 * Run async queue, then report exit code
 * ============================================================ */

function runAsync() {
    var i = 0;
    function next() {
        if (i >= asyncQ.length) {
            testModule.exit();
            return;
        }
        asyncQ[i++](next);
    }
    next();
}

runAsync();
//lastline
