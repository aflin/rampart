/* Vector / vec-index feature tests, following the testFeature() format.
 *
 * Covers:
 *   - rampart.vector type construction & dim accessor
 *   - varvec* and varbyte columns
 *   - CREATE VECTOR INDEX with WITH options (vec_m, vec_metric, vec_dtype, flush)
 *   - LIKEV search + $rank ordering
 *   - Indexed vs brute-force agreement
 *   - Per-row INSERT/DELETE through the index
 *   - flush 'manual' WAL table lifecycle (created at CREATE, dropped at DROP)
 *   - Defer-mode + close-flush cycle
 *   - Connection-scoped sql.set vecAutoFlush
 *   - Multi-process via two rampart.thread workers
 *
 * Doesn't depend on any external database; creates a fresh one under
 * test/tmp-vector-test and removes it at the end.  Total run time ~5-10s.
 */

rampart.globalize(rampart.utils);
load.Sql;

var tmpdir = process.scriptPath + '/tmp-vector-test';
if (!stat(tmpdir)) mkdir(tmpdir);

var _hasShell = !!stat('/bin/bash');
function rm_rf_dir(path) {
    if (_hasShell) { shell("rm -rf " + path); return; }
    if (!stat(path)) return;
    var files = readdir(path);
    for (var i = 0; i < files.length; i++) {
        try { rmFile(path + "/" + files[i]); } catch(e) {}
    }
    try { rmdir(path); } catch(e) {}
}

var _nfailed = 0;
function testFeature(name, test) {
    var error = false;
    if (typeof test == 'function') {
        try { test = test(); } catch (e) { error = e; test = false; }
    }
    printf("testing vector - %-51s - ", name);
    if (test) printf("passed\n");
    else      { printf(">>>>> FAILED <<<<<\n"); _nfailed++; }
    if (error) console.log(error);
}

/* Convenience: build a unit-norm vector of given dim with seeded values
 * derived from the integer id so they're reproducible. */
function vec_for(id, dim, dtype) {
    if (dim === undefined) dim = 8;
    if (dtype === undefined) dtype = 'f32';
    var arr = new Array(dim), s = 0;
    for (var i = 0; i < dim; i++) {
        var u = Math.sin(id * 31 + i * 17) + 0.001;
        arr[i] = u; s += u * u;
    }
    var inv = 1 / Math.sqrt(s);
    for (var i = 0; i < dim; i++) arr[i] *= inv;
    return new rampart.vector(dtype, arr);
}

var DB = tmpdir + '/db';
rm_rf_dir(DB);   /* fresh state if a prior failed run left a db */
var sql = Sql.connect(DB, true);

/* ============================================================
 * rampart.vector basics
 * ============================================================ */

testFeature("rampart.vector('f32', array) creates an f32 vector", function () {
    var v = new rampart.vector('f32', [0.1, 0.2, 0.3]);
    return v.type === 'f32' && v.dim === 3;
});

testFeature("rampart.vector('f16', array) creates an f16 vector", function () {
    var v = new rampart.vector('f16', [0.1, 0.2, 0.3]);
    return v.type === 'f16' && v.dim === 3;
});

testFeature("vector toF32().toNumbers() round-trips", function () {
    var v = new rampart.vector('f32', [0.5, -0.5, 0.25]);
    var n = v.toF32().toNumbers();
    return n.length === 3 &&
           Math.abs(n[0] - 0.5)  < 1e-6 &&
           Math.abs(n[1] - -0.5) < 1e-6 &&
           Math.abs(n[2] - 0.25) < 1e-6;
});

/* ============================================================
 * Table + column types
 * ============================================================ */

testFeature("create table with varvecF32 column", function () {
    sql.exec("create table emb (id int, v varvecF32, label varchar(16));");
    var r = sql.exec("select NAME, TYPE from SYSCOLUMNS where TBNAME=? and NAME=?;",
                     ["emb", "v"]);
    return r.rows[0] && r.rows[0].TYPE === 'varvecF32';
});

testFeature("insert rows with vector values", function () {
    for (var i = 0; i < 50; i++)
        sql.exec("insert into emb values (?, ?, ?);",
                 [i, vec_for(i), 'row' + i]);
    return sql.exec("select count(*) as n from emb;").rows[0].n === 50;
});

/* ============================================================
 * CREATE VECTOR INDEX
 * ============================================================ */

testFeature("CREATE VECTOR INDEX (defaults)", function () {
    sql.exec("create vector index emb_vec on emb (v);");
    var r = sql.exec("select TYPE, PARAMS from SYSINDEX where NAME='emb_vec';");
    return r.rows[0] && r.rows[0].TYPE === 'N';   /* N = INDEX_VEC */
});

testFeature("PARAMS records dim, dtype, defaults", function () {
    var p = sql.exec("select PARAMS from SYSINDEX where NAME='emb_vec';").rows[0].PARAMS;
    return /dim=8/.test(p) && /dtype=f32/.test(p) &&
           /flush=auto/.test(p) && /state=clean/.test(p);
});

/* ============================================================
 * LIKEV search
 * ============================================================ */

testFeature("LIKEV with $rank: top-1 is self for known query", function () {
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(7)], 1);
    return hits.rows[0] && hits.rows[0].id === 7;
});

testFeature("LIKEV results ordered by $rank desc", function () {
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(7)], 5);
    if (hits.rows.length < 2) return false;
    for (var i = 1; i < hits.rows.length; i++)
        if (hits.rows[i].$rank > hits.rows[i-1].$rank) return false;
    return true;
});

testFeature("indexed top-1 == brute-force top-1", function () {
    var idxHit = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(13)], 1).rows[0].id;
    sql.exec("drop index emb_vec;");
    var bruteHit = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(13)], 1).rows[0].id;
    sql.exec("create vector index emb_vec on emb (v);");   /* rebuild for next tests */
    return idxHit === bruteHit;
});

/* ============================================================
 * WITH options
 * ============================================================ */

testFeature("vec_m=2 (below min) rejected at CREATE", function () {
    var msg = null;
    try {
        sql.exec("drop index emb_vec;");
        sql.exec("create vector index emb_vec on emb (v) with vec_m 2;");
    } catch (e) { msg = String(e); }
    /* Re-create the index for subsequent tests regardless of outcome. */
    try { sql.exec("drop index emb_vec;"); } catch(e) {}
    sql.exec("create vector index emb_vec on emb (v);");
    return msg !== null && /vec_m must.*\[4, 1024\]/.test(msg);
});

testFeature("vec_metric='bogus' rejected at CREATE", function () {
    var msg = null;
    try {
        sql.exec("drop index emb_vec;");
        sql.exec("create vector index emb_vec on emb (v) with vec_metric 'bogus';");
    } catch (e) { msg = String(e); }
    try { sql.exec("drop index emb_vec;"); } catch(e) {}
    sql.exec("create vector index emb_vec on emb (v);");
    return msg !== null && /vec_metric must/.test(msg);
});

/* ============================================================
 * Per-row INSERT / DELETE through the index
 * ============================================================ */

testFeature("freshly INSERTed row is findable via the index", function () {
    sql.exec("insert into emb values (?, ?, ?);",
             [999, vec_for(999), 'row999']);
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(999)], 1);
    return hits.rows[0] && hits.rows[0].id === 999;
});

testFeature("DELETEd row is no longer findable via the index", function () {
    sql.exec("delete from emb where id = 999;");
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(999)], 5);
    var ids = hits.rows.map(function (r) { return r.id; });
    return ids.indexOf(999) === -1;
});

/* ============================================================
 * varbyte column with vec_dtype
 * ============================================================ */

testFeature("varbyte CREATE without vec_dtype is rejected", function () {
    sql.exec("create table embb (id int, v varbyte(16));");
    /* 16 bytes = 8 f16s, dim=8 to match. */
    for (var i = 0; i < 5; i++)
        sql.exec("insert into embb values (?, ?);",
                 [i, vec_for(i, 8, 'f16').toRaw()]);
    var msg = null;
    try { sql.exec("create vector index embb_vec on embb (v);"); }
    catch (e) { msg = String(e); }
    return msg !== null && /vec_dtype/.test(msg);
});

testFeature("varbyte CREATE with vec_dtype 'f16' succeeds", function () {
    sql.exec("create vector index embb_vec on embb (v) with vec_dtype 'f16';");
    var p = sql.exec("select PARAMS from SYSINDEX where NAME='embb_vec';").rows[0].PARAMS;
    return /dim=8/.test(p) && /dtype=f16/.test(p);
});

testFeature("varbyte top-1 = self for raw-bytes query", function () {
    var bytes = vec_for(2, 8, 'f16').toRaw();
    var hits = sql.exec(
        "select id, $rank from embb where v likev ? order by 2 desc;",
        [bytes], 1);
    return hits.rows[0] && hits.rows[0].id === 2;
});

testFeature("DROP INDEX on varbyte index", function () {
    sql.exec("drop index embb_vec;");
    sql.exec("drop table embb;");
    var r = sql.exec("select NAME from SYSINDEX where NAME='embb_vec';");
    return r.rows.length === 0;
});

/* ============================================================
 * Manual flush mode + WAL table
 * ============================================================ */

testFeature("CREATE INDEX with flush 'manual' creates WAL table", function () {
    sql.exec("drop index emb_vec;");
    sql.exec("create vector index emb_vec on emb (v) with flush 'manual';");
    var r = sql.exec("select NAME from SYSTABLES where NAME='emb_vec_wal';");
    return r.rows.length === 1;
});

testFeature("defer-mode INSERT sets PARAMS state=dirty", function () {
    sql.exec("insert into emb values (?, ?, ?);", [501, vec_for(501), 'r501']);
    var p = sql.exec("select PARAMS from SYSINDEX where NAME='emb_vec';").rows[0].PARAMS;
    return /state=dirty/.test(p);
});

testFeature("defer-mode INSERT writes a row to the WAL table", function () {
    var r = sql.exec("select count(*) as n from emb_vec_wal;");
    return r.rows[0].n >= 1;
});

testFeature("defer-mode row visible from same connection", function () {
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(501)], 1);
    return hits.rows[0] && hits.rows[0].id === 501;
});

testFeature("close + reopen flushes WAL; row is durable", function () {
    sql.close();
    sql = Sql.connect(DB);
    var p = sql.exec("select PARAMS from SYSINDEX where NAME='emb_vec';").rows[0].PARAMS;
    var n = sql.exec("select count(*) as n from emb_vec_wal;").rows[0].n;
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(501)], 1);
    return /state=clean/.test(p) && n === 0 &&
           hits.rows[0] && hits.rows[0].id === 501;
});

testFeature("DROP INDEX also drops the WAL table", function () {
    /* re-open writable */
    sql.close();
    sql = Sql.connect(DB, true);
    sql.exec("drop index emb_vec;");
    var r = sql.exec("select NAME from SYSTABLES where NAME='emb_vec_wal';");
    return r.rows.length === 0;
});

/* ============================================================
 * sql.set({vecAutoFlush}) connection knob
 * ============================================================ */

testFeature("sql.set vecAutoFlush=false defers, =true flushes", function () {
    sql.exec("create vector index emb_vec on emb (v);");   /* auto-flush index */
    sql.set({vecAutoFlush: false});
    sql.exec("insert into emb values (?, ?, ?);", [777, vec_for(777), 'r777']);
    var p_dirty = sql.exec("select PARAMS from SYSINDEX where NAME='emb_vec';").rows[0].PARAMS;
    sql.set({vecAutoFlush: true});
    var p_clean = sql.exec("select PARAMS from SYSINDEX where NAME='emb_vec';").rows[0].PARAMS;
    return /state=dirty/.test(p_dirty) && /state=clean/.test(p_clean);
});

/* ============================================================
 * Multi-process via rampart.thread
 *
 * Each rampart.thread runs sql.exec through its own forked sql-helper
 * process, so this exercises real cross-process locking + the WAL
 * merge protocol without spawning shell children.
 *
 * This phase is asynchronous: we launch two thread workers, return
 * from the script, and let the event loop drain.  The completion
 * callback runs cleanup + process.exit.
 * ============================================================ */

/* Recreate the index in manual mode for the multi-process test. */
sql.exec("drop index emb_vec;");
sql.exec("create vector index emb_vec on emb (v) with flush 'manual';");
sql.close();      /* let the threads fight over the db without us */

/* The thread inherits module-scope globals via fork — DB is reachable
 * by name inside the worker. */
function worker(start_id) {
    rampart.globalize(rampart.utils);
    load.Sql;
    function vec_for_(id) {
        var dim = 8, arr = new Array(dim), s = 0;
        for (var i = 0; i < dim; i++) {
            var u = Math.sin(id * 31 + i * 17) + 0.001;
            arr[i] = u; s += u * u;
        }
        var inv = 1 / Math.sqrt(s);
        for (var i = 0; i < dim; i++) arr[i] *= inv;
        return new rampart.vector('f32', arr);
    }
    var s = Sql.connect(DB, true);
    for (var i = 0; i < 20; i++) {
        var id = start_id + i;
        s.exec("insert into emb values (?, ?, ?);",
               [id, vec_for_(id), 'mp' + id]);
    }
    s.close();    /* triggers WAL replay + flush */
    return start_id;
}

var mp_pending = 2;
var mp_threadErr = null;

function workerDone(value, err) {
    if (err) mp_threadErr = err;
    mp_pending--;
    if (mp_pending === 0) finishAll();
}

function finishAll() {
    var name = "two threads insert disjoint ids in defer mode";
    var pass = false;
    var verify_err = null;
    var diag = "";
    try {
        if (mp_threadErr) throw mp_threadErr;
        var s2 = Sql.connect(DB);
        var foundA = 0, foundB = 0;
        [10000, 10010, 10019].forEach(function (id) {
            var hits = s2.exec(
                "select id, $rank from emb where v likev ? order by 2 desc;",
                [vec_for(id)], 1);
            if (hits.rows[0] && hits.rows[0].id === id) foundA++;
        });
        [20000, 20010, 20019].forEach(function (id) {
            var hits = s2.exec(
                "select id, $rank from emb where v likev ? order by 2 desc;",
                [vec_for(id)], 1);
            if (hits.rows[0] && hits.rows[0].id === id) foundB++;
        });
        pass = (foundA === 3 && foundB === 3);
        if (!pass) {
            var nA = s2.exec("select count(*) as n from emb where id between 10000 and 10099;").rows[0].n;
            var nB = s2.exec("select count(*) as n from emb where id between 20000 and 20099;").rows[0].n;
            var nW = s2.exec("select count(*) as n from emb_vec_wal;").rows[0].n;
            var p  = s2.exec("select PARAMS from SYSINDEX where NAME='emb_vec';").rows[0].PARAMS;
            diag = "foundA=" + foundA + "/3 foundB=" + foundB + "/3 " +
                   "tableA=" + nA + "/20 tableB=" + nB + "/20 wal=" + nW + " params=" + p;
        }
        try { s2.exec("drop index emb_vec;"); } catch (e) {}
        try { s2.exec("drop table emb;"); }    catch (e) {}
        s2.close();
    } catch (e) { verify_err = e; pass = false; }

    printf("testing vector - %-51s - ", name);
    if (pass) printf("passed\n");
    else      { printf(">>>>> FAILED <<<<<\n"); _nfailed++; }
    if (diag) printf("  %s\n", diag);
    if (verify_err) console.log(verify_err);

    rm_rf_dir(tmpdir);
    process.exit(_nfailed ? 1 : 0);
}

/* Keep thread refs at module scope so they aren't GC'd. */
var thrA = new rampart.thread();
var thrB = new rampart.thread();
thrA.exec(worker, 10000, workerDone);
thrB.exec(worker, 20000, workerDone);
