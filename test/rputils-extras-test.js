//first line
/*
 * Distribution test for the fs-extras additions to rampart.utils.
 * Covers: writeFile, appendFile, readLink, truncate, statVfs,
 *         exists, tmpDir, homeDir, mkdTemp,
 *         fopen handle f-methods (fstat/fsync/fdatasync/ftruncate/
 *           fchmod/fchown/fUtimes/fileNo),
 *         POSIX integer-fd API (open, close, read, write, lseek,
 *           fstatFd, fsyncFd, fdatasyncFd, ftruncateFd, fchmodFd,
 *           fchownFd, futimesFd, plus the rampart.utils.O constants),
 *         walkDir, cp, rm, glob, watch,
 *         lchown / lchmod / lUtimes.
 *
 * Modeled on rputils-test.js -- single sync flow, 52-char description
 * column, exits non-zero on any failure.  More exhaustive coverage
 * (including paranoid rm safety tests) lives in
 * build/src/new-rampart-utils/.
 */
rampart.globalize(rampart.utils);

var isWindows = /MSYS_NT/.test(rampart.buildPlatform);
var _hasShell = !!stat('/bin/bash');
var _isRoot   = _hasShell ? (trim(shell("whoami").stdout) === "root") : false;

/* lchmod(2) is a real syscall on macOS and the *BSDs but does NOT
 * exist on Linux (the kernel has no facility for changing the mode
 * of a symlink, and the bits aren't honored anyway).  Our impl
 * routes Linux symlink calls into an ENOSYS-style throw and falls
 * through to chmod() for non-symlinks; on platforms where the real
 * lchmod is available, the symlink call works as expected. */
var _lchmodOnSymlinkWorks = /Darwin|FreeBSD|NetBSD|OpenBSD|DragonFly/i
                            .test(rampart.buildPlatform);

chdir(process.scriptPath);

var tmpdir = process.scriptPath + '/tmp-extras-test';
if (!stat(tmpdir)) mkdir(tmpdir);

var _nfailed = 0;

function testFeature(name, test)
{
    var error = false;
    if (typeof test == 'function') {
        try {
            test = test();
        } catch (e) {
            error = e;
            test = false;
        }
    }
    printf("testing utils - %-52s - ", name);
    if (test)
        if (typeof test == 'string')
            printf("%s\n", test);
        else
            printf("passed\n");
    else {
        printf(">>>>> FAILED <<<<<\n");
        _nfailed++;
    }
    if (error) console.log(error);
}

/* ============================================================
 * Phase 1 -- path-based primitives
 * ============================================================ */

testFeature("writeFile / appendFile", function() {
    var fp = tmpdir + "/ax.txt";
    writeFile(fp, "hello ");
    appendFile(fp, "world");
    var ok = readFile(fp, {returnString: true}) === "hello world";
    rmFile(fp);
    return ok;
});

testFeature("writeFile - with mode option", function() {
    var fp = tmpdir + "/secret.txt";
    writeFile(fp, "shh", {mode: 0o600});
    var modeOk = (stat(fp).mode & 0o777) === 0o600;
    rmFile(fp);
    return modeOk;
});

testFeature("writeFile - Buffer payload", function() {
    var fp = tmpdir + "/bin.bin";
    writeFile(fp, new Uint8Array([1, 2, 3, 4, 5]));
    var b = readFile(fp);
    var ok = b.length === 5 && b[0] === 1 && b[4] === 5;
    rmFile(fp);
    return ok;
});

if (isWindows)
    testFeature("readLink", "skipping (Windows)");
else
    testFeature("readLink", function() {
        var fp = tmpdir + "/tgt.txt";
        var lp = tmpdir + "/lk";
        writeFile(fp, "x");
        symlink(fp, lp);
        var got = readLink(lp);
        rmFile(lp);
        rmFile(fp);
        return got === fp;
    });

testFeature("truncate", function() {
    var fp = tmpdir + "/tr.txt";
    writeFile(fp, "abcdefghij");
    truncate(fp, 4);
    var shrunk = readFile(fp, {returnString: true});
    truncate(fp, 8);
    var ext = stat(fp).size;
    rmFile(fp);
    return shrunk === "abcd" && ext === 8;
});

testFeature("statVfs", function() {
    var sv = statVfs(tmpdir);
    return sv && typeof sv.bsize === 'number' && sv.bsize > 0
        && typeof sv.blocks === 'number' && sv.blocks > 0
        && sv.totalBytes === sv.frsize * sv.blocks;
});

testFeature("exists", function() {
    var fp = tmpdir + "/ex.txt";
    writeFile(fp, "x");
    var yes = exists(fp);
    rmFile(fp);
    var no  = exists(fp);
    return yes === true && no === false;
});

testFeature("tmpDir", function() {
    var t = tmpDir();
    return typeof t === 'string' && t.length > 0
        && (t === '/' || t[t.length - 1] !== '/');
});

testFeature("homeDir", function() {
    var h = homeDir();
    return typeof h === 'string' && h.length > 0 && h[0] === '/';
});

testFeature("mkdTemp", function() {
    var d = mkdTemp(tmpdir + "/mk-");
    var ok = d.indexOf(tmpdir + "/mk-") === 0
          && stat(d).isDirectory === true;
    /* Plain rmdir(d) -- the dir is empty.  Avoid the legacy
       rmdir(d, true) form which walks UP the path and would try to
       delete ancestor directories. */
    rmdir(d);
    return ok;
});

/* ============================================================
 * Phase 2 -- fopen handle f-methods
 * ============================================================ */

testFeature("fopen handle - fstat / fileNo", function() {
    var fp = tmpdir + "/fh.bin";
    var fh = fopen(fp, "w");
    fh.fwrite("hello");
    fh.fflush();
    var st = fh.fstat();
    var fd = fh.fileNo();
    fh.fclose();
    rmFile(fp);
    return st.size === 5 && st.isFile === true
        && typeof fd === 'number' && fd >= 0;
});

testFeature("fopen handle - fsync / fdatasync", function() {
    var fp = tmpdir + "/fs.bin";
    var fh = fopen(fp, "w");
    fh.fwrite("durable");
    fh.fsync();
    fh.fdatasync();
    fh.fclose();
    rmFile(fp);
    return true;
});

testFeature("fopen handle - ftruncate", function() {
    var fp = tmpdir + "/ft.bin";
    var fh = fopen(fp, "w");
    fh.fwrite("0123456789");
    fh.fflush();
    fh.ftruncate(4);
    var sz = fh.fstat().size;
    fh.fclose();
    rmFile(fp);
    return sz === 4;
});

testFeature("fopen handle - fchmod", function() {
    var fp = tmpdir + "/fcm.bin";
    var fh = fopen(fp, "w");
    fh.fchmod(0o600);
    var modeOk = (fh.fstat().mode & 0o777) === 0o600;
    fh.fclose();
    rmFile(fp);
    return modeOk;
});

if (_isRoot)
    testFeature("fopen handle - fchown", function() {
        var fp = tmpdir + "/fcw.bin";
        var fh = fopen(fp, "w");
        /* Change to uid/gid 100/101, then verify via fstat. */
        fh.fchown(100, 101);
        var st = fh.fstat();
        fh.fclose();
        rmFile(fp);
        return st.uid === 100 && st.gid === 101;
    });
else
    testFeature("fopen handle - fchown", "skipping (not root)");

testFeature("fopen handle - fUtimes", function() {
    var fp = tmpdir + "/fut.bin";
    var fh = fopen(fp, "w");
    fh.fUtimes(1700000000, 1600000000);
    var st = fh.fstat();
    fh.fclose();
    rmFile(fp);
    return Math.floor(st.atime.getTime() / 1000) === 1700000000
        && Math.floor(st.mtime.getTime() / 1000) === 1600000000;
});

/* ============================================================
 * Phase 2 -- POSIX integer-fd API
 * ============================================================ */

testFeature("open flag constants (rampart.utils.O)", function() {
    var O = rampart.utils.O;
    return O.RDONLY === 0 && O.WRONLY === 1 && O.RDWR === 2
        && typeof O.CREAT === 'number' && O.CREAT > 0
        && typeof O.EXCL === 'number'  && O.EXCL > 0
        && typeof O.TRUNC === 'number'
        && O.SEEK_SET === 0 && O.SEEK_CUR === 1 && O.SEEK_END === 2;
});

testFeature("open / close / read / write", function() {
    var O = rampart.utils.O;
    var fp = tmpdir + "/fd.bin";
    var fd = open(fp, O.WRONLY | O.CREAT | O.TRUNC, 0o644);
    var n = write(fd, "hello world");
    close(fd);
    fd = open(fp, O.RDONLY);
    var buf = read(fd, 11);
    close(fd);
    rmFile(fp);
    return n === 11 && bufferToString(buf) === "hello world";
});

testFeature("pread / pwrite via position arg", function() {
    var O = rampart.utils.O;
    var fp = tmpdir + "/pf.bin";
    var fd = open(fp, O.WRONLY | O.CREAT | O.TRUNC, 0o644);
    write(fd, "AAAAAAAAAA");
    close(fd);

    fd = open(fp, O.RDWR);
    write(fd, "Z", 5);                   /* pwrite at offset 5 */
    var part = read(fd, 3, 4);           /* pread 3 bytes at offset 4 */
    close(fd);
    rmFile(fp);
    return bufferToString(part) === "AZA";
});

testFeature("lseek", function() {
    var O = rampart.utils.O;
    var fp = tmpdir + "/ls.bin";
    var fd = open(fp, O.WRONLY | O.CREAT | O.TRUNC, 0o644);
    write(fd, "0123456789");
    close(fd);

    fd = open(fp, O.RDONLY);
    var endpos = lseek(fd, 0, "SEEK_END");
    var resetpos = lseek(fd, 0, "SEEK_SET");
    close(fd);
    rmFile(fp);
    return endpos === 10 && resetpos === 0;
});

testFeature("fstatFd", function() {
    var O = rampart.utils.O;
    var fp = tmpdir + "/fst.bin";
    var fd = open(fp, O.WRONLY | O.CREAT | O.TRUNC, 0o644);
    write(fd, "hello");
    close(fd);
    fd = open(fp, O.RDONLY);
    var st = fstatFd(fd);
    close(fd);
    rmFile(fp);
    return st.size === 5 && st.isFile === true;
});

testFeature("fsyncFd / fdatasyncFd", function() {
    var O = rampart.utils.O;
    var fp = tmpdir + "/sy.bin";
    var fd = open(fp, O.WRONLY | O.CREAT | O.TRUNC, 0o644);
    write(fd, "x");
    fsyncFd(fd);
    fdatasyncFd(fd);
    close(fd);
    rmFile(fp);
    return true;
});

testFeature("ftruncateFd / fchmodFd", function() {
    var O = rampart.utils.O;
    var fp = tmpdir + "/ftf.bin";
    var fd = open(fp, O.WRONLY | O.CREAT | O.TRUNC, 0o644);
    write(fd, "0123456789");
    ftruncateFd(fd, 5);
    fchmodFd(fd, 0o600);
    close(fd);
    var final = stat(fp);
    rmFile(fp);
    return final.size === 5 && (final.mode & 0o777) === 0o600;
});

if (_isRoot)
    testFeature("fchownFd", function() {
        var O = rampart.utils.O;
        var fp = tmpdir + "/fcwfd.bin";
        var fd = open(fp, O.WRONLY | O.CREAT | O.TRUNC, 0o644);
        fchownFd(fd, 100, 101);
        var st = fstatFd(fd);
        close(fd);
        rmFile(fp);
        return st.uid === 100 && st.gid === 101;
    });
else
    testFeature("fchownFd", "skipping (not root)");

testFeature("futimesFd", function() {
    var O = rampart.utils.O;
    var fp = tmpdir + "/fut2.bin";
    var fd = open(fp, O.WRONLY | O.CREAT | O.TRUNC, 0o644);
    futimesFd(fd, 1500000000, 1400000000);
    var st = fstatFd(fd);
    close(fd);
    rmFile(fp);
    return Math.floor(st.atime.getTime() / 1000) === 1500000000
        && Math.floor(st.mtime.getTime() / 1000) === 1400000000;
});

testFeature("atomic write pattern (O_EXCL + fsync)", function() {
    var O = rampart.utils.O;
    var real = tmpdir + "/atomic.txt";
    var tmp  = real + ".tmp." + process.pid;
    if (exists(tmp))  rmFile(tmp);
    if (exists(real)) rmFile(real);
    var fd = open(tmp, O.WRONLY | O.CREAT | O.EXCL | O.TRUNC, 0o644);
    write(fd, "durable\n");
    fsyncFd(fd);
    close(fd);
    rename(tmp, real);
    var content = readFile(real, {returnString: true});
    rmFile(real);
    return content === "durable\n";
});

/* ============================================================
 * Phase 3 -- recursive helpers
 * ============================================================ */

testFeature("walkDir - pre/post-order, depth", function() {
    var base = tmpdir + "/wd";
    mkdir(base + "/sub");
    writeFile(base + "/a.txt", "a");
    writeFile(base + "/sub/b.txt", "b");

    var pre = [], post = [];
    walkDir(base, function(p, type, depth) {
        pre.push(p.slice(base.length) + ":" + type + ":" + depth);
    });
    walkDir(base, function(p, type) {
        post.push(p.slice(base.length) + ":" + type);
    }, {postOrder: true});

    rm(base, {recursive: true, force: true});

    /* pre-order: root before children; post-order: root LAST */
    return pre[0] === ":dir:0"
        && post[post.length - 1] === ":dir";
});

testFeature("cp - single file", function() {
    var src = tmpdir + "/cps.txt";
    var dst = tmpdir + "/cpd.txt";
    writeFile(src, "copy-me");
    cp(src, dst);
    var ok = readFile(dst, {returnString: true}) === "copy-me";
    rmFile(src); rmFile(dst);
    return ok;
});

testFeature("cp - recursive directory tree", function() {
    var s = tmpdir + "/cp-src", d = tmpdir + "/cp-dst";
    mkdir(s + "/sub");
    writeFile(s + "/x.txt", "X");
    writeFile(s + "/sub/y.txt", "Y");
    cp(s, d, {recursive: true});
    var ok = exists(d + "/x.txt") && exists(d + "/sub/y.txt")
          && readFile(d + "/sub/y.txt", {returnString: true}) === "Y";
    rm(s, {recursive: true, force: true});
    rm(d, {recursive: true, force: true});
    return ok;
});

testFeature("rm - single file", function() {
    var fp = tmpdir + "/rm1.txt";
    writeFile(fp, "x");
    rm(fp);
    return !exists(fp);
});

if (isWindows)
    testFeature("rm - recursive force + symlink safety", "skipping (Windows)");
else
    testFeature("rm - recursive force + symlink safety", function() {
        /* Stage a tree containing a symlink that points OUTSIDE the
         * tree we're about to recursively delete.  Verify rm(-rf)
         * unlinks the link but does NOT chase it. */
        var inside  = tmpdir + "/rm-in";
        var outside = tmpdir + "/rm-out";
        mkdir(inside);
        mkdir(outside);
        writeFile(outside + "/survive.txt", "must-survive");
        writeFile(inside + "/inner.txt", "inner");
        symlink(outside, inside + "/danger-link");

        rm(inside, {recursive: true, force: true});

        var outsideOk = exists(outside + "/survive.txt")
            && readFile(outside + "/survive.txt", {returnString: true}) === "must-survive";

        /* Also verify {force:true} silences ENOENT on missing path */
        var enoentSilent = true;
        try { rm(tmpdir + "/no-such-thing", {force: true}); }
        catch (e) { enoentSilent = false; }

        rm(outside, {recursive: true, force: true});

        return !exists(inside) && outsideOk && enoentSilent;
    });

testFeature("glob - wildcards, **, char classes", function() {
    var base = tmpdir + "/g";
    mkdir(base + "/sub");
    writeFile(base + "/a.txt", "");
    writeFile(base + "/b.txt", "");
    writeFile(base + "/c.js",  "");
    writeFile(base + "/sub/d.txt", "");

    var allTxt  = glob("**/*.txt", {cwd: base});
    var jsOnly  = glob("*.js",     {cwd: base});
    var classed = glob("[ab].txt", {cwd: base});

    rm(base, {recursive: true, force: true});
    return allTxt.length === 3 && jsOnly.length === 1 && classed.length === 2;
});

/* ============================================================
 * Phase 4 -- watch (basic shape, no event-loop wait)
 * ============================================================ */

if (isWindows)
    testFeature("watch (returns watcher object)", "skipping (Windows)");
else
    testFeature("watch (returns watcher object)", function() {
        var fp = tmpdir + "/wat.txt";
        writeFile(fp, "x");
        var w = watch(fp, function() {});
        var ok = w && typeof w.close === 'function'
              && (w.backend === 'inotify' || w.backend === 'polling')
              && w.path === fp;
        w.close();
        rmFile(fp);
        return ok;
    });

/* ============================================================
 * Symlink-aware variants
 * ============================================================ */

if (isWindows) {
    testFeature("lchown", "skipping (Windows)");
    testFeature("lchmod", "skipping (Windows)");
    testFeature("lUtimes", "skipping (Windows)");
} else {
    if (_isRoot)
        testFeature("lchown", function() {
            var fp = tmpdir + "/lc.txt";
            var lp = tmpdir + "/lc-link";
            writeFile(fp, "x");
            symlink(fp, lp);
            lchown(lp, 100, 101);
            var linkInfo = lstat(lp);
            var targetInfo = stat(fp);
            rmFile(lp); rmFile(fp);
            /* Link itself should reflect the new uid/gid; target must
             * still belong to the original owner (lchown doesn't follow). */
            return linkInfo.uid === 100 && linkInfo.gid === 101
                && targetInfo.uid !== 100;
        });
    else
        testFeature("lchown", "skipping (not root)");

    testFeature("lchmod - regular file (chmod fallthrough)", function() {
        var fp = tmpdir + "/lcm.txt";
        writeFile(fp, "x", {mode: 0o644});
        lchmod(fp, 0o600);
        var modeOk = (stat(fp).mode & 0o777) === 0o600;
        rmFile(fp);
        return modeOk;
    });

    if (_lchmodOnSymlinkWorks)
        testFeature("lchmod - actual symlink", function() {
            /* On macOS/*BSD, lchmod can set mode bits on the symlink
             * itself.  The TARGET's mode must stay unchanged. */
            var fp = tmpdir + "/lcm-tgt.txt";
            var lp = tmpdir + "/lcm-link";
            writeFile(fp, "x", {mode: 0o644});
            symlink(fp, lp);
            lchmod(lp, 0o600);
            var linkMode = lstat(lp).mode & 0o777;
            var tgtMode  = stat(fp).mode  & 0o777;
            rmFile(lp); rmFile(fp);
            return linkMode === 0o600 && tgtMode === 0o644;
        });
    else
        testFeature("lchmod - actual symlink", "skipping (Linux: no syscall)");

    testFeature("lUtimes", function() {
        var fp = tmpdir + "/lu.txt";
        var lp = tmpdir + "/lu-link";
        writeFile(fp, "x");
        symlink(fp, lp);
        lUtimes(lp, 1500000000, 1400000000);
        var link = lstat(lp);
        var ok = Math.floor(link.atime.getTime() / 1000) === 1500000000
              && Math.floor(link.mtime.getTime() / 1000) === 1400000000;
        rmFile(lp); rmFile(fp);
        return ok;
    });
}

/* ============================================================
 * TextEncoder / TextDecoder (rampart-textencoding.c -- WHATWG fixes
 * to duktape's globals)
 * ============================================================ */

testFeature("TextDecoder canonical encoding labels", function() {
    return new TextDecoder('utf-8').encoding === 'utf-8'
        && new TextDecoder('latin1').encoding === 'iso-8859-1'
        && new TextDecoder('binary').encoding === 'iso-8859-1'
        && new TextDecoder('ascii').encoding === 'us-ascii'
        && new TextDecoder('utf-16le').encoding === 'utf-16le'
        && new TextDecoder('utf-16be').encoding === 'utf-16be';
});

testFeature("TextDecoder unknown label throws RangeError", function() {
    var threw = false, isRange = false;
    try { new TextDecoder('windows-1252'); }
    catch (e) { threw = true; isRange = (e instanceof RangeError); }
    return threw && isRange;
});

testFeature("TextDecoder latin1 / utf-16le / utf-16be", function() {
    return new TextDecoder('latin1').decode(new Uint8Array([0xe9])) === "é"
        && new TextDecoder('utf-16le').decode(new Uint8Array([0x68,0,0x69,0])) === "hi"
        && new TextDecoder('utf-16be').decode(new Uint8Array([0,0x68,0,0x69])) === "hi";
});

testFeature("TextDecoder BOM stripping + ignoreBOM:true", function() {
    var bytes = new Uint8Array([0xEF,0xBB,0xBF,0x68,0x69]);
    return new TextDecoder('utf-8').decode(bytes) === "hi"
        && new TextDecoder('utf-8', {ignoreBOM: true}).decode(bytes) === "﻿hi";
});

testFeature("TextDecoder fatal:true throws on invalid utf-8", function() {
    var threw = false;
    try { new TextDecoder('utf-8', {fatal: true}).decode(new Uint8Array([0xC0])); }
    catch (e) { threw = true; }
    return threw;
});

testFeature("TextEncoder.encodeInto fits + truncates", function() {
    var te = new TextEncoder();
    var buf = new Uint8Array(10);
    var r1 = te.encodeInto('hi', buf);
    var truncBuf = new Uint8Array(3);
    var r2 = te.encodeInto('hello', truncBuf);
    /* exact 4-byte cp truncation: 'a😀' into 3 bytes -> only 'a' fits */
    var r3 = te.encodeInto('a😀', new Uint8Array(3));
    return r1.read === 2 && r1.written === 2
        && r2.read === 3 && r2.written === 3
        && r3.read === 1 && r3.written === 1;
});

/* ============================================================
 * Uint8Array (and all TypedArrays) -- polyfilled methods from
 * rampart-buffer.c: slice (copy), fill, copyWithin, forEach, map,
 * filter, reduce, reduceRight, some, every, find, findIndex,
 * findLast, findLastIndex, at, reverse, sort, join, indexOf,
 * lastIndexOf, includes, entries, keys, values.
 * ============================================================ */

testFeature("TypedArray prototype: methods present", function() {
    var p = Uint8Array.prototype;
    var need = ['slice','fill','copyWithin','forEach','map','filter','reduce',
                'reduceRight','some','every','find','findIndex','findLast',
                'findLastIndex','at','reverse','sort','join','indexOf',
                'lastIndexOf','includes','entries','keys','values'];
    for (var i = 0; i < need.length; i++)
        if (typeof p[need[i]] !== 'function') return 'missing ' + need[i];
    return true;
});

testFeature("Uint8Array.slice returns a COPY (not a view)", function() {
    var u = new Uint8Array([1,2,3,4,5]);
    var s = u.slice(1, 4);
    s[0] = 99;
    return u[1] === 2 && s[0] === 99 && s.length === 3
        && s.constructor === Uint8Array;
});

testFeature("Uint8Array.fill / map / filter / reduce", function() {
    var f = new Uint8Array(4); f.fill(7);
    var u = new Uint8Array([1,2,3,4,5]);
    var d = u.map(function(x){return x*2;});
    var e = u.filter(function(x){return x%2===0;});
    var s = u.reduce(function(a,b){return a+b;}, 0);
    return f[0]===7 && f[3]===7
        && d[0]===2 && d[4]===10 && d.constructor===Uint8Array
        && e.length===2 && e[0]===2 && e[1]===4
        && s===15;
});

testFeature("Uint8Array.find / findIndex / findLast / at", function() {
    var u = new Uint8Array([1,2,3,4,5]);
    return u.find(function(x){return x>3;}) === 4
        && u.findIndex(function(x){return x>3;}) === 3
        && u.findLast(function(x){return x<4;}) === 3
        && u.at(-1) === 5
        && u.at(0)  === 1;
});

testFeature("Uint8Array.sort default is numeric", function() {
    var s = new Uint8Array([5,1,4,2,3]);
    s.sort();
    return s[0]===1 && s[1]===2 && s[2]===3 && s[3]===4 && s[4]===5;
});

testFeature("Uint8Array iterators (values / entries)", function() {
    var u = new Uint8Array([10,20,30]);
    var vals = [], pairs = [], step;
    var v = u.values();
    while (!(step = v.next()).done) vals.push(step.value);
    var e = u.entries();
    while (!(step = e.next()).done) pairs.push(step.value.join(':'));
    return vals.join(',') === '10,20,30'
        && pairs.join(',') === '0:10,1:20,2:30';
});

testFeature("Other TypedArrays inherit (Int16/Float32)", function() {
    var i16 = new Int16Array([100,200,300]);
    var m   = i16.map(function(x){return x+1;});
    var f32 = new Float32Array([1.5,2.5,3.5]);
    var f   = f32.filter(function(x){return x > 2;});
    return m[0]===101 && m.constructor === Int16Array
        && f.length === 2 && f[0] === 2.5 && f.constructor === Float32Array;
});

testFeature("Buffer-specific overrides still win", function() {
    /* Buffer.prototype.slice returns a SHARED view (matches node);
     * Buffer.indexOf accepts strings.  Both should be unaffected by
     * the typed-array polyfill. */
    var b = Buffer.from([10,20,30,40,50]);
    var bs = b.slice(1,4); bs[0] = 99;
    return b[1] === 99   /* slice shared a view (Buffer-specific) */
        && Buffer.from('abc').indexOf('b') === 1;  /* string overload */
});

testFeature("Buffer.subarray returns a SHARED view", function() {
    var b = Buffer.from([10,20,30,40,50]);
    var s = b.subarray(1, 4);
    s[0] = 99;
    return b[1] === 99
        && s instanceof Buffer
        && s.length === 3
        && s.toString('hex') === '631e28';
});

testFeature("Buffer.copyBytesFrom (node 18+)", function() {
    var src = new Uint8Array([10,20,30,40,50]);
    var cp1 = Buffer.copyBytesFrom(src);
    cp1[0] = 99;
    /* cp1 is a copy -- mutating it doesn't touch src */
    var isCopy = (src[0] === 10) && (cp1 instanceof Buffer) && cp1.length === 5;
    /* offset/length form */
    var cp2 = Buffer.copyBytesFrom(src, 1, 3);
    var sliceOk = cp2.length === 3 && cp2[0] === 20 && cp2[2] === 40;
    /* element-count semantics for Int16Array: 2 elements = 4 bytes */
    var i16 = new Int16Array([1000, 2000, 3000]);
    var cp3 = Buffer.copyBytesFrom(i16, 1, 2);
    return isCopy && sliceOk && cp3.length === 4;
});

testFeature("Buffer.prototype.inspect default + truncation", function() {
    var short = Buffer.from([0xde,0xad,0xbe,0xef]).inspect();
    var long  = Buffer.alloc(60, 0x42).inspect();
    return short === '<Buffer de ad be ef>'
        && /<Buffer( 42){50} \.\.\. 10 more bytes>/.test(long);
});


/* ============================================================
 * Tier 1 — rampart.utils zlib primitives (libdeflate-backed)
 * ============================================================ */

testFeature("gzip / gunzip roundtrip", function() {
    var s = 'Hello, World! '.repeat(20);
    var gz = gzip(s);
    /* gz is a duktape buffer (Uint8Array view); just check shape + roundtrip */
    return (gz instanceof Uint8Array)
        && gz.length < s.length
        && bufferToString(gunzip(gz)) === s;
});

testFeature("deflate / inflate roundtrip", function() {
    var s = 'abcabcabcabcabc';
    return bufferToString(inflate(deflate(s))) === s;
});

testFeature("deflateRaw / inflateRaw roundtrip", function() {
    var s = 'rampart!';
    var raw = deflateRaw(s);
    return bufferToString(inflateRaw(raw)) === s;
});

testFeature("gzip with level option (1 vs 9)", function() {
    /* highly compressible input -- level 9 should match or beat level 1 */
    var s = 'aaaaaaaaaaaa'.repeat(100);
    var a = gzip(s, 1), b = gzip(s, 9);
    /* Both must roundtrip cleanly */
    return bufferToString(gunzip(a)) === s
        && bufferToString(gunzip(b)) === s
        && b.length <= a.length;
});

testFeature("crc32 known value (RFC 3720)", function() {
    /* crc32 of "hello" using IEEE 802.3 polynomial = 0x3610a686 */
    return crc32('hello') === 0x3610a686
        && crc32('') === 0
        && crc32('hello', crc32('')) === 0x3610a686;   /* seed=0 idempotent */
});

testFeature("adler32 known value", function() {
    /* adler32 of "hello" with seed=1 = 0x062c0215 */
    return adler32('hello') === 0x062c0215
        && adler32('') === 1;   /* adler initial value is 1, not 0 */
});

testFeature("gunzip rejects bad data", function() {
    var threw = false;
    try { gunzip('not gzip data at all'); }
    catch (e) { threw = true; }
    return threw;
});

/* ============================================================
 * Tier 1 — rampart core globals
 * ============================================================ */

testFeature("setImmediate / clearImmediate exist", function() {
    return typeof setImmediate === 'function'
        && typeof clearImmediate === 'function';
});

testFeature("console.time / timeEnd / count present", function() {
    return typeof console.time === 'function'
        && typeof console.timeEnd === 'function'
        && typeof console.timeLog === 'function'
        && typeof console.count === 'function'
        && typeof console.countReset === 'function';
});

testFeature("console.table / group / clear present", function() {
    return typeof console.table === 'function'
        && typeof console.group === 'function'
        && typeof console.groupEnd === 'function'
        && typeof console.groupCollapsed === 'function'
        && typeof console.clear === 'function';
});

try { rmdir(tmpdir); } catch (e) {} /* remove if empty */

process.exit(_nfailed ? 1 : 0);
//lastline
