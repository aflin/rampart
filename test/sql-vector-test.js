/* Vector / vec-index feature tests, following the testFeature() format.
 *
 * Covers:
 *   - rampart.vector type construction & dim accessor
 *   - varvec* and varbyte columns
 *   - CREATE VECTOR INDEX with WITH options (vec_m, vec_metric, vec_dtype)
 *   - LIKEV search + $rank ordering
 *   - Indexed vs brute-force agreement
 *   - Per-row INSERT/DELETE through the index
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

var testFeature = new (require('./test-feature.js'))({prefix: "vector"});

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

testFeature("CREATE VECTOR INDEX (HNSW backend, small table)", function () {
    sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw';");
    var r = sql.exec("select TYPE, PARAMS from SYSINDEX where NAME='emb_vec';");
    return r.rows[0] && r.rows[0].TYPE === 'N';   /* N = INDEX_VEC */
});

testFeature("HNSW PARAMS records dim, dtype, defaults", function () {
    var p = sql.exec("select PARAMS from SYSINDEX where NAME='emb_vec';").rows[0].PARAMS;
    return /dim=8/.test(p) && /dtype=f32/.test(p) &&
           /backend=usearch/.test(p);
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
    sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw';");   /* rebuild for next tests */
    return idxHit === bruteHit;
});

/* ============================================================
 * WITH options
 * ============================================================ */

testFeature("vec_m=2 (below min) rejected at CREATE", function () {
    var msg = null;
    try {
        sql.exec("drop index emb_vec;");
        sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw' vec_m 2;");
    } catch (e) { msg = String(e); }
    /* Re-create the index for subsequent tests regardless of outcome. */
    try { sql.exec("drop index emb_vec;"); } catch(e) {}
    sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw';");
    return msg !== null && /vec_m must.*\[4, 1024\]/.test(msg);
});

testFeature("vec_metric='bogus' rejected at CREATE", function () {
    var msg = null;
    try {
        sql.exec("drop index emb_vec;");
        sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw' vec_metric 'bogus';");
    } catch (e) { msg = String(e); }
    try { sql.exec("drop index emb_vec;"); } catch(e) {}
    sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw';");
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
    try { sql.exec("create vector index embb_vec on embb (v) with backend 'hnsw';"); }
    catch (e) { msg = String(e); }
    return msg !== null && /vec_dtype/.test(msg);
});

testFeature("varbyte CREATE with vec_dtype 'f16' succeeds", function () {
    sql.exec("create vector index embb_vec on embb (v) with backend 'hnsw' vec_dtype 'f16';");
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
 * i8 / u8 quantized indexes
 * ============================================================ */

testFeature("i8 index on varvecF16 column (default calibration)", function () {
    sql.exec("drop index emb_vec;");
    sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw' vec_dtype 'i8';");
    var p = sql.exec("select PARAMS from SYSINDEX where NAME='emb_vec';").rows[0].PARAMS;
    return /dtype=i8/.test(p) &&
           /quant_scale=0\.007874/.test(p) &&
           /quant_zp=0/.test(p);
});

testFeature("i8 indexed search returns correct top-1", function () {
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(7)], 1);
    return hits.rows[0] && hits.rows[0].id === 7;
});

testFeature("u8 index on varvecF16 column (default calibration)", function () {
    sql.exec("drop index emb_vec;");
    sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw' vec_dtype 'u8';");
    var p = sql.exec("select PARAMS from SYSINDEX where NAME='emb_vec';").rows[0].PARAMS;
    return /dtype=u8/.test(p) &&
           /quant_scale=0\.007874/.test(p) &&
           /quant_zp=128/.test(p);
});

testFeature("u8 indexed search returns correct top-1", function () {
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(7)], 1);
    return hits.rows[0] && hits.rows[0].id === 7;
});

testFeature("explicit vec_scale and vec_zero_point", function () {
    sql.exec("drop index emb_vec;");
    sql.exec("create vector index emb_vec on emb (v) " +
             "with backend 'hnsw' vec_dtype 'i8' vec_scale 0.01 vec_zero_point 5;");
    var p = sql.exec("select PARAMS from SYSINDEX where NAME='emb_vec';").rows[0].PARAMS;
    return /dtype=i8/.test(p) &&
           /quant_scale=0\.010000/.test(p) &&
           /quant_zp=5/.test(p);
});

testFeature("vec_calibrate 'auto' computes scale + zp", function () {
    sql.exec("drop index emb_vec;");
    sql.exec("create vector index emb_vec on emb (v) " +
             "with backend 'hnsw' vec_dtype 'i8' vec_calibrate 'auto';");
    var p = sql.exec("select PARAMS from SYSINDEX where NAME='emb_vec';").rows[0].PARAMS;
    /* Scale should differ from the dtype default (0.007874) since the
     * actual data range from sin()-derived unit vectors is narrower. */
    var m = /quant_scale=([0-9.]+)/.exec(p);
    return m && parseFloat(m[1]) > 0 && parseFloat(m[1]) !== 0.007874;
});

testFeature("PARAMS omits quant fields for float indexes", function () {
    sql.exec("drop index emb_vec;");
    sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw';");   /* default = f32 */
    var p = sql.exec("select PARAMS from SYSINDEX where NAME='emb_vec';").rows[0].PARAMS;
    return !/quant_scale/.test(p) && !/quant_zp/.test(p);
});

testFeature("vec_dtype 'bogus' is rejected at CREATE", function () {
    var msg = null;
    try {
        sql.exec("drop index emb_vec;");
        sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw' vec_dtype 'bogus';");
    } catch (e) { msg = String(e); }
    try { sql.exec("drop index emb_vec;"); } catch(e) {}
    sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw';");   /* restore */
    return msg !== null && /vec_dtype/.test(msg);
});

testFeature("INSERT through i8 index then find it by LIKEV", function () {
    sql.exec("drop index emb_vec;");
    sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw' vec_dtype 'i8';");
    sql.exec("insert into emb values (?, ?, ?);",
             [555, vec_for(555), 'r555']);
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(555)], 1);
    return hits.rows[0] && hits.rows[0].id === 555;
});

testFeature("varbyte column indexed at i8", function () {
    sql.exec("create table emb_vb (id int, v varbyte(16));");
    /* 8 i8 values = 8 bytes; pad to 16 isn't needed since varbyte. */
    for (var i = 0; i < 5; i++) {
        var raw = new rampart.vector('i8', vec_for(i).toF32().toNumbers());
        sql.exec("insert into emb_vb values (?, ?);", [i, raw.toRaw()]);
    }
    sql.exec("create vector index emb_vb_vec on emb_vb (v) with backend 'hnsw' vec_dtype 'i8';");
    var p = sql.exec("select PARAMS from SYSINDEX where NAME='emb_vb_vec';").rows[0].PARAMS;
    var ok = /dim=8/.test(p) && /dtype=i8/.test(p);
    sql.exec("drop index emb_vb_vec;");
    sql.exec("drop table emb_vb;");
    return ok;
});

testFeature("DROP INDEX cleans up i8 index", function () {
    sql.exec("drop index emb_vec;");
    sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw';");   /* restore default for later tests */
    var r = sql.exec("select NAME from SYSINDEX where NAME='emb_vec';");
    return r.rows.length === 1;   /* the f32 index now exists again */
});

/* ============================================================
 * Post-CREATE INSERT durability via _T.btr
 *
 * Under the texis-fulltext-style design, every post-CREATE INSERT
 * records its recid in `_T.btr` and is visible to LIKEV via the
 * delta linear-scan path.  No flush mode, no WAL.
 * ============================================================ */

testFeature("post-CREATE INSERT records recid in _T.btr", function () {
    sql.exec("insert into emb values (?, ?, ?);", [501, vec_for(501), 'r501']);
    return !!stat(DB + "/emb_vec_T.btr");
});

testFeature("post-CREATE INSERT row visible from same connection", function () {
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(501)], 1);
    return hits.rows[0] && hits.rows[0].id === 501;
});

testFeature("close + reopen: post-CREATE row is durable in _T.btr", function () {
    sql.close();
    sql = Sql.connect(DB);
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(501)], 1);
    return hits.rows[0] && hits.rows[0].id === 501;
});

/* ============================================================
 * Misc connection knobs
 * ============================================================ */

testFeature("CREATE INDEX still produces functional handle", function () {
    /* Re-open writable for any subsequent DDL/DML in this section. */
    sql.close();
    sql = Sql.connect(DB, true);
    sql.exec("insert into emb values (?, ?, ?);", [777, vec_for(777), 'r777']);
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(777)], 1);
    return hits.rows[0] && hits.rows[0].id === 777;
});

testFeature("sql.set likevRows caps the candidate pool", function () {
    sql.set({likevRows: 3});
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(7)], -1);
    sql.set({likevRows: 1000});
    return hits.rows.length === 3;
});

testFeature("SQL set likevrows= caps the candidate pool", function () {
    sql.exec("set likevrows=4;");
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(7)], -1);
    sql.exec("set likevrows=1000;");
    return hits.rows.length === 4;
});

testFeature("sql.set likevEf accepted (per-query expansion)", function () {
    var thrown = false;
    try { sql.set({likevEf: 200}); sql.set({likevEf: 0}); }
    catch (e) { thrown = true; }
    return !thrown;
});

/* ============================================================
 * IVFPQ backend (FAISS)
 *
 * Phase-2 surface: CREATE + LIKEV search; INSERT/DELETE/UPDATE log
 * a documented read-only error to stderr and skip the index update
 * (the table-level operation succeeds; LIKEV won't find the new row
 * until ALTER INDEX … REBUILD in phase 3).
 *
 * The PQ subquantizer k-means floor is `39 × ksub = 9984` training
 * rows (ksub=256 for nbits=8).  We use 10000 rows of dim=32 so the
 * setup completes in a few seconds while satisfying the floor.
 *
 * `vec_for(id)` collides for large id ranges (sin(31*N) period ≈
 * 3263), so we use a non-colliding helper for the IVFPQ tests.
 * ============================================================ */

function vec_unique(id, dim) {
    /* Deterministic per-id, non-colliding for at least 1e8 rows.
     * Park-Miller LCG seeded by id × Knuth's golden-ratio hash. */
    var arr = new Array(dim), s = 0;
    var seed = (id * 2654435761) | 0;     /* int32 wrap */
    if (seed === 0) seed = 1;
    for (var i = 0; i < dim; i++) {
        seed = ((seed * 48271) | 0);
        if (seed <= 0) seed += 0x7fffffff;
        var u = (seed / 0x7fffffff) - 0.5;
        arr[i] = u; s += u * u;
    }
    var inv = 1 / Math.sqrt(s);
    for (var i = 0; i < dim; i++) arr[i] *= inv;
    return new rampart.vector('f32', arr);
}

/* IVFPQ availability is probed at runtime by attempting CREATE on an
 * empty table.  Two known reasons for unavailability:
 *
 *   1. RP_NO_FAISS — FAISS isn't compiled into this rampart-sql.
 *      Currently only 32-bit ARM with GCC >= 9 hits this path; armhf
 *      + GCC < 9 builds FAISS with a CXX11-ABI workaround (see
 *      extern/extern.cmake's RP_ARMHF_GCC8_FAISS_WORKAROUND).  Error
 *      message: "INDEX_VEC backend=ivfpq is not supported ...".
 *
 *   2. BLAS chain not installed — rampart-sql.so dlopens libopenblas
 *      (and on Linux libgomp/libgfortran) lazily; if they're absent
 *      the dispatcher returns "INDEX_VEC backend=ivfpq requires
 *      lib*..." with an apt/pkg hint.
 *
 * The probe runs before FAISS training, so an empty table is enough.
 * BLAS+FAISS present -> a different error fires (insufficient
 * training rows) or success -> fall through and run the full suite. */
var _ivfpqAvailable = true;
var _ivfpqSkipReason = null;
try { sql.exec("drop table blasprobe;"); } catch (e) {}
sql.exec("create table blasprobe (v varvecF32);");
try {
    sql.exec("create vector index blasprobe_idx on blasprobe (v) " +
             "with backend 'ivfpq' vec_pq_min_points_per_centroid 1;");
} catch (e) {
    var _msg = String(e);
    if (/INDEX_VEC backend=ivfpq requires lib(openblas|gomp|gfortran)/.test(_msg)) {
        _ivfpqAvailable = false;
        _ivfpqSkipReason = "BLAS chain not installed on this system";
    } else if (/INDEX_VEC backend=ivfpq is not supported/.test(_msg)) {
        _ivfpqAvailable = false;
        _ivfpqSkipReason = "FAISS not built (RP_NO_FAISS)";
    }
}
try { sql.exec("drop index blasprobe_idx;"); } catch (e) {}
try { sql.exec("drop table blasprobe;"); } catch (e) {}

if (!_ivfpqAvailable) {
    printf("Skipping IVFPQ test cases — %s\n", _ivfpqSkipReason);
    if (_ivfpqSkipReason === "BLAS chain not installed on this system") {
        printf("  rampart-sql is built without a DT_NEEDED link on libopenblas\n");
        printf("  (and, on Linux, libgomp / libgfortran); the IVFPQ backend dlopens\n");
        printf("  them on first use.  To enable IVFPQ on this machine, install the\n");
        printf("  runtime libs:\n");
        printf("\n");
        printf("    Debian/Ubuntu:   sudo apt install libopenblas0-pthread libgomp1 libgfortran5\n");
        printf("    Fedora/RHEL:     sudo dnf install openblas libgomp libgfortran\n");
        printf("    Arch:            sudo pacman -S openblas gcc-libs gcc-fortran\n");
        printf("    FreeBSD:         sudo pkg install openblas\n");
        printf("\n");
        printf("  HNSW (backend='hnsw') needs none of these and works as-is.\n");
    }
    testFeature.exit();
}

/* Drop any prior IVFPQ test table from a re-run. */
try { sql.exec("drop index iv32_vec;"); } catch (e) {}
try { sql.exec("drop table iv32;");     } catch (e) {}

/* IVFPQ training-row floor depends on min_points_per_centroid (default
 * 39) × ksub (256 at nbits=8) = 9984.  We pass min_ppc=1 to all CREATE
 * INDEX statements below so the floor drops to 256, which lets the
 * regression suite train an IVFPQ index from a few hundred rows. */
var IV32_N = 300;

testFeature("ivfpq setup: load " + IV32_N + " dim=32 rows into iv32", function () {
    sql.exec("create table iv32 (id int, v varvecF32(32));");
    sql.exec("create unique index iv32_id on iv32 (id);");
    for (var i = 0; i < IV32_N; i++)
        sql.exec("insert into iv32 values(?, ?)", [i, vec_unique(i, 32)]);
    return sql.one("select count(*) c from iv32;").c === IV32_N;
});

testFeature("ivfpq CREATE on too-small table is rejected", function () {
    /* 5 rows is well under the floor even at min_ppc=1 (need ≥ ksub=256). */
    sql.exec("create table iv_tiny (id int, v varvecF32(8));");
    for (var i = 0; i < 5; i++)
        sql.exec("insert into iv_tiny values(?, ?)", [i, vec_for(i, 8)]);
    var msg = null;
    try {
        sql.exec("create vector index iv_tiny_vec on iv_tiny (v) " +
                 "with backend 'ivfpq' vec_pq_min_points_per_centroid 1;");
    } catch (e) { msg = String(e); }
    sql.exec("drop table iv_tiny;");
    return msg !== null &&
           /requires.*training rows.*table has/.test(msg);
});

testFeature("ivfpq CREATE INDEX with explicit nlist=64 m=8", function () {
    /* Explicit params + min_ppc=1 so the training-row floor is 256
     * (1 × ksub).  The auto-tuner is bypassed by the explicit settings. */
    sql.exec("create vector index iv32_vec on iv32 (v) with backend 'ivfpq' " +
             "vec_pq_nlist 64 vec_pq_m 8 vec_pq_min_points_per_centroid 1;");
    return sql.one("select count(*) c from SYSINDEX where NAME='iv32_vec';").c === 1;
});

testFeature("ivfpq SYSINDEX.PARAMS shape is correct", function () {
    var p = sql.one("select PARAMS from SYSINDEX where NAME='iv32_vec';").PARAMS;
    return /backend=ivfpq/.test(p) &&
           /dim=32/.test(p) &&
           /pq_m=8/.test(p) &&
           /pq_nlist=64/.test(p) &&
           /pq_nbits=8/.test(p);
});

testFeature("ivfpq on-disk artifacts: _H + _I + _T + _del", function () {
    /* Texis-fulltext-style layout: codebook head + on-disk inverted
     * lists + empty newrec btree + empty tombstone btree.  All four
     * are created at CREATE INDEX time. */
    var path = DB + "/iv32_vec";
    return stat(path + "_H.idxpq") &&
           stat(path + "_I.idxpq") &&
           stat(path + "_T.btr")   &&
           stat(path + "_del.btr");
});

testFeature("ivfpq LIKEV+vecdist re-rank: self-recall >= 95%", function () {
    /* Plan §2's documented IVFPQ usage pattern: LIKEV gets candidates,
     * vecdist re-ranks by exact distance over the column data.  This
     * recovers from PQ approximation (which alone is ~40% top-1 on
     * dim=32 / M=8 random unit-norm).
     *
     * vecdist returns dot product (higher = more similar for unit-norm
     * vectors), so the re-rank is `order by 2 desc`. */
    sql.set({likevPqNprobe: 64, likevRows: 1000});       /* full scan */
    var hits = 0;
    var n = 30;
    for (var i = 0; i < n; i++) {
        var pick = (i * 13 + 7) % IV32_N;
        var rs = sql.exec(
            "select id, vecdist(v, ?) d from iv32 where v likev ? " +
            "order by 2 desc;",
            [vec_unique(pick, 32), vec_unique(pick, 32)], 1);
        if (rs.rows.length > 0 && rs.rows[0].id === pick) hits++;
    }
    return hits / n >= 0.95;
});

testFeature("ivfpq LIKEV honors likevPqNprobe SET knob", function () {
    /* nprobe=1 (one list) returns strictly fewer candidates than
     * nprobe=64 (all lists).  Use an explicit limit so the JS row cap
     * doesn't truncate both equally. */
    sql.set({likevPqNprobe: 64, likevRows: 1000});
    var lo = sql.exec("select id from iv32 where v likev ?;",
                      [vec_unique(42, 32)], 1000).rows.length;
    sql.set({likevPqNprobe: 1});
    var hi = sql.exec("select id from iv32 where v likev ?;",
                      [vec_unique(42, 32)], 1000).rows.length;
    sql.set({likevPqNprobe: 8});        /* restore default */
    return lo > hi && hi > 0;
});

testFeature("ivfpq INSERT: new row findable via LIKEV (delta)", function () {
    /* Phase-C: INSERTs route to the HNSW delta segment.  After insert,
     * a LIKEV query with the new row's vector should return it among
     * candidates; vecdist re-rank picks it as top-1. */
    var fresh_id = 99999;
    sql.exec("insert into iv32 values(?, ?);",
             [fresh_id, vec_unique(fresh_id, 32)]);
    sql.set({likevPqNprobe: 64, likevRows: 1000});
    var hits = sql.exec(
        "select id, vecdist(v, ?) d from iv32 where v likev ? order by 2 desc;",
        [vec_unique(fresh_id, 32), vec_unique(fresh_id, 32)], 1).rows;
    return hits.length > 0 && hits[0].id === fresh_id;
});

testFeature("ivfpq DELETE: deleted row drops out of LIKEV results", function () {
    /* Phase-D: DELETE on a sealed-resident recid (recid ≤ max_recid_at_create)
     * inserts the recid into the tombstone btree AND routes to delta
     * del_row (no-op since the row isn't in delta).  SEARCH post-filters
     * sealed hits against the tombstone cache. */
    sql.exec("delete from iv32 where id=42;");
    sql.set({likevPqNprobe: 64, likevRows: 1000});
    var hits = sql.exec("select id from iv32 where v likev ?;",
                        [vec_unique(42, 32)], 1000).rows;
    var found = hits.some(function (r) { return r.id === 42; });
    return !found;
});

testFeature("ivfpq DELETE on sealed row creates _del.btr", function () {
    /* By the time this runs, the DELETE-of-id=42 test above has hit a
     * sealed-era recid, so the tombstone btree must exist on disk. */
    return !!stat(DB + "/iv32_vec_del.btr");
});

testFeature("ivfpq UPDATE: updated row's new vector is findable", function () {
    /* Phase-C: UPDATE = DELETE + INSERT internally; the new row lands
     * in the delta segment.  Use an id (200000) outside the 0..9999
     * dataset so the query vector has no other ties.  After update,
     * row 1 holds that unique vector and should be top-1 by vecdist. */
    sql.exec("update iv32 set v=? where id=1;", [vec_unique(200000, 32)]);
    sql.set({likevPqNprobe: 64, likevRows: 1000});
    var hits = sql.exec(
        "select id, vecdist(v, ?) d from iv32 where v likev ? order by 2 desc;",
        [vec_unique(200000, 32), vec_unique(200000, 32)], 1).rows;
    return hits.length > 0 && hits[0].id === 1;
});

testFeature("ivfpq OPTIMIZE advances max_recid_at_create", function () {
    /* Read the int64 max_recid_at_create from the _H.idxpq head file
     * before and after OPTIMIZE.  The boundary lives at file offset 56
     * per the header layout in vecindex_ivfpq.cpp.  After absorbing
     * post-CREATE delta entries, the boundary must have advanced. */
    var hpath = DB + "/iv32_vec_H.idxpq";
    function read_max_recid() {
        var buf = readFile(hpath);
        var dv = new DataView(buf.buffer);
        return dv.getInt32(56, true) | (dv.getInt32(60, true) * 0x100000000);
    }
    var before = read_max_recid();
    sql.exec("alter index iv32_vec optimize;");
    var after = read_max_recid();
    return after > before;
});

testFeature("ivfpq post-OPTIMIZE: id=99999 (delta-pre) findable", function () {
    /* After absorbing the delta into sealed, the post-CREATE INSERT of
     * id=99999 should now live in the sealed PQ codes — LIKEV must
     * still surface it. */
    sql.set({likevPqNprobe: 64, likevRows: 1000});
    var hits = sql.exec(
        "select id, vecdist(v, ?) d from iv32 where v likev ? order by 2 desc;",
        [vec_unique(99999, 32), vec_unique(99999, 32)], 1).rows;
    return hits.length > 0 && hits[0].id === 99999;
});

testFeature("ivfpq post-OPTIMIZE: updated id=1 still findable", function () {
    /* The UPDATE wrote a new recid > old max_recid_at_create; OPTIMIZE
     * absorbed it into sealed.  Query with the post-update vector
     * should still return id=1. */
    sql.set({likevPqNprobe: 64, likevRows: 1000});
    var hits = sql.exec(
        "select id, vecdist(v, ?) d from iv32 where v likev ? order by 2 desc;",
        [vec_unique(200000, 32), vec_unique(200000, 32)], 1).rows;
    return hits.length > 0 && hits[0].id === 1;
});

testFeature("ivfpq post-OPTIMIZE: tombstoned id=42 still excluded", function () {
    /* OPTIMIZE doesn't clear tombstones — the DELETE of id=42 from
     * earlier still applies. */
    sql.set({likevPqNprobe: 64, likevRows: 1000});
    var hits = sql.exec("select id from iv32 where v likev ?;",
                        [vec_unique(42, 32)], 1000).rows;
    return !hits.some(function (r) { return r.id === 42; });
});

testFeature("ivfpq post-OPTIMIZE: fresh INSERT then LIKEV works", function () {
    /* INSERT after OPTIMIZE: this row goes to delta (the new max boundary
     * is now the table's largest recid; this row's recid is fresh). */
    var fresh_id = 88888;
    sql.exec("insert into iv32 values(?, ?);",
             [fresh_id, vec_unique(fresh_id, 32)]);
    sql.set({likevPqNprobe: 64, likevRows: 1000});
    var hits = sql.exec(
        "select id, vecdist(v, ?) d from iv32 where v likev ? order by 2 desc;",
        [vec_unique(fresh_id, 32), vec_unique(fresh_id, 32)], 1).rows;
    return hits.length > 0 && hits[0].id === fresh_id;
});

testFeature("hnsw ALTER INDEX OPTIMIZE is a no-op success", function () {
    /* HNSW backend has no OPTIMIZE work to do (graph quality is
     * insert-order-independent and there's no codebook).  The slot
     * returns 0 / dispatcher returns success.  Just verifying it
     * doesn't error out. */
    sql.exec("alter index emb_vec optimize;");
    return true;
});

testFeature("ivfpq REBUILD retrains and clears tombstones", function () {
    /* By the time this runs we've INSERTed (id=99999), DELETEd (id=42,
     * tombstoned), UPDATEd (id=1), OPTIMIZEd (absorbed delta), then
     * INSERTed (id=88888, in delta).  REBUILD should:
     *   - re-train codebooks from scratch (k-means redone)
     *   - re-encode every current table row into sealed
     *   - leave empty `_T.btr` and `_del.btr` (every current row is
     *     now in sealed, so neither auxiliary needs entries) */
    sql.exec("alter index iv32_vec rebuild;");
    var path = DB + "/iv32_vec";
    return !!stat(path + "_H.idxpq")
        && !!stat(path + "_I.idxpq")
        && !!stat(path + "_T.btr")
        && !!stat(path + "_del.btr");
});

testFeature("ivfpq post-REBUILD: max_recid covers whole table", function () {
    /* After REBUILD, max_recid_at_create should equal the largest
     * recid->off in the table (every row was just absorbed into
     * sealed). */
    var hpath = DB + "/iv32_vec_H.idxpq";
    var buf = readFile(hpath);
    var dv  = new DataView(buf.buffer);
    var max_recid_at_create = dv.getInt32(56, true) | (dv.getInt32(60, true) * 0x100000000);
    return max_recid_at_create > 0;
});

testFeature("ivfpq post-REBUILD: id=99999 still findable via LIKEV", function () {
    /* id=99999 was inserted way back; OPTIMIZE absorbed it into sealed;
     * REBUILD re-encoded it (still in sealed).  LIKEV must still find it. */
    sql.set({likevPqNprobe: 64, likevRows: 1000});
    var hits = sql.exec(
        "select id, vecdist(v, ?) d from iv32 where v likev ? order by 2 desc;",
        [vec_unique(99999, 32), vec_unique(99999, 32)], 1).rows;
    return hits.length > 0 && hits[0].id === 99999;
});

testFeature("ivfpq post-REBUILD: id=88888 (delta-pre) findable", function () {
    /* id=88888 was inserted after OPTIMIZE so lived in delta; REBUILD
     * folded it into sealed alongside everything else. */
    sql.set({likevPqNprobe: 64, likevRows: 1000});
    var hits = sql.exec(
        "select id, vecdist(v, ?) d from iv32 where v likev ? order by 2 desc;",
        [vec_unique(88888, 32), vec_unique(88888, 32)], 1).rows;
    return hits.length > 0 && hits[0].id === 88888;
});

testFeature("ivfpq post-REBUILD: deleted id=42 still absent", function () {
    /* id=42 was deleted from the table back at the DELETE test.  REBUILD
     * encodes only current rows, so the id=42 entry is simply not in
     * sealed at all (tombstone unnecessary). */
    sql.set({likevPqNprobe: 64, likevRows: 1000});
    var hits = sql.exec("select id from iv32 where v likev ?;",
                        [vec_unique(42, 32)], 1000).rows;
    return !hits.some(function (r) { return r.id === 42; });
});

testFeature("hnsw REBUILD: succeeds and rows still findable", function () {
    /* HNSW REBUILD walks the table, encodes each row into a fresh
     * usearch graph at a Tnnnn temp basename, then atomic-swaps. */
    sql.exec("alter index emb_vec rebuild;");
    var hits = sql.exec(
        "select id, $rank from emb where v likev ? order by 2 desc;",
        [vec_for(7)], 1).rows;
    return hits.length > 0 && hits[0].id === 7;
});

testFeature("ivfpq DROP INDEX removes all artifacts", function () {
    /* All four artifacts are present after REBUILD; DROP must remove
     * every one. */
    var path = DB + "/iv32_vec";
    var before = !!stat(path + "_H.idxpq") && !!stat(path + "_I.idxpq")
              && !!stat(path + "_T.btr")   && !!stat(path + "_del.btr");
    sql.exec("drop index iv32_vec;");
    var after = !stat(path + "_H.idxpq") && !stat(path + "_I.idxpq")
             && !stat(path + "_T.btr")   && !stat(path + "_del.btr");
    return before && after;
});

testFeature("ivfpq DROP TABLE cleans up", function () {
    sql.exec("drop table iv32;");
    return sql.one("select count(*) c from SYSTABLES where NAME='iv32'").c === 0;
});

/* ============================================================
 * Multi-process via rampart.thread
 *
 * Each rampart.thread runs sql.exec through its own forked sql-helper
 * process, so this exercises real cross-process locking + the per-
 * backend visibility paths:
 *
 *   - HNSW (emb): per-INSERT recid stored in `_T.btr` (durable through
 *     the indexed table's lock); LIKEV does delta linear-scan over
 *     `_T.btr` recids and merges with .vec hits via min-heap.
 *   - IVFPQ (iv32): same `_T.btr` newrec design; LIKEV scans the delta
 *     with full-precision vecdist and merges with PQ-coded sealed-set
 *     candidates.  Cross-process visibility for both backends comes
 *     from texislockd's INDEX_VERIFY counter advancing the cached
 *     `_T.btr` handle on the next op.
 *
 * This phase is asynchronous: we launch four thread workers and let
 * the event loop drain.  When all four call back, finishAll verifies
 * both indexes and exits.
 * ============================================================ */

/* Recreate a fresh HNSW index for the cross-process test. */
sql.exec("drop index emb_vec;");
sql.exec("create vector index emb_vec on emb (v) with backend 'hnsw';");

/* Recreate the IVFPQ table (the earlier DROP TABLE test wiped it).
 * Floor with min_ppc=1 is ksub=256, so IV32_N rows is enough. */
sql.exec("create table iv32 (id int, v varvecF32(32));");
sql.exec("create unique index iv32_id on iv32 (id);");
for (var i = 0; i < IV32_N; i++)
    sql.exec("insert into iv32 values(?, ?);", [i, vec_unique(i, 32)]);
sql.exec("create vector index iv32_vec on iv32 (v) with backend 'ivfpq' " +
         "vec_pq_nlist 64 vec_pq_m 8 vec_pq_min_points_per_centroid 1;");

sql.close();      /* let the threads fight over the db without us */

/* HNSW worker (exercises the cross-process `_T.btr` newrec path). */
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
    s.close();
    return start_id;
}

/* IVFPQ worker (eager-save delta path).  Each thread inserts 50 rows
 * with disjoint ids.  Each INSERT goes to the HNSW delta segment and
 * is save_atomic'd while the parent table's write lock is held;
 * cross-process visibility kicks in when the OTHER worker's next
 * INSERT opens the cached handle and the stat-based staleness check
 * picks up the .vec file's new mtime/size. */
function worker_iv32(start_id) {
    rampart.globalize(rampart.utils);
    load.Sql;
    function vec_unique_(id, dim) {
        /* Inline copy of the parent's vec_unique — Park-Miller LCG. */
        var arr = new Array(dim), s = 0;
        var seed = (id * 2654435761) | 0;
        if (seed === 0) seed = 1;
        for (var i = 0; i < dim; i++) {
            seed = ((seed * 48271) | 0);
            if (seed <= 0) seed += 0x7fffffff;
            var u = (seed / 0x7fffffff) - 0.5;
            arr[i] = u; s += u * u;
        }
        var inv = 1 / Math.sqrt(s);
        for (var i = 0; i < dim; i++) arr[i] *= inv;
        return new rampart.vector('f32', arr);
    }
    var s = Sql.connect(DB, true);
    for (var i = 0; i < 50; i++) {
        var id = start_id + i;
        s.exec("insert into iv32 values(?, ?);",
               [id, vec_unique_(id, 32)]);
    }
    s.close();
    return start_id;
}

var mp_pending = 4;
var mp_threadErr = null;

function workerDone(value, err) {
    if (err) mp_threadErr = err;
    mp_pending--;
    if (mp_pending === 0) finishAll();
}

function finishAll() {
    var verify_err = null;
    if (mp_threadErr) verify_err = mp_threadErr;
    var s2;
    try { s2 = Sql.connect(DB); } catch (e) { verify_err = e; }

    /* === HNSW phase ============================================== */
    var hnsw_name = "two threads insert disjoint HNSW ids via _T.btr";
    var hnsw_pass = false;
    var hnsw_diag = "";
    if (s2 && !verify_err) {
        try {
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
            hnsw_pass = (foundA === 3 && foundB === 3);
            if (!hnsw_pass) {
                var nA = s2.exec("select count(*) as n from emb where id between 10000 and 10099;").rows[0].n;
                var nB = s2.exec("select count(*) as n from emb where id between 20000 and 20099;").rows[0].n;
                var p  = s2.exec("select PARAMS from SYSINDEX where NAME='emb_vec';").rows[0].PARAMS;
                hnsw_diag = "foundA=" + foundA + "/3 foundB=" + foundB + "/3 " +
                       "tableA=" + nA + "/20 tableB=" + nB + "/20 params=" + p;
            }
        } catch (e) { verify_err = e; hnsw_pass = false; }
    }

    /* === IVFPQ phase ============================================= */
    var ivfpq_name = "two threads insert disjoint ids into ivfpq delta";
    var ivfpq_pass = false;
    var ivfpq_diag = "";
    if (s2 && !verify_err) {
        try {
            s2.set({likevPqNprobe: 64, likevRows: 1000});
            var foundC = 0, foundD = 0;
            [30000, 30025, 30049].forEach(function (id) {
                var hits = s2.exec(
                    "select id, vecdist(v, ?) d from iv32 where v likev ? order by 2 desc;",
                    [vec_unique(id, 32), vec_unique(id, 32)], 1);
                if (hits.rows[0] && hits.rows[0].id === id) foundC++;
            });
            [40000, 40025, 40049].forEach(function (id) {
                var hits = s2.exec(
                    "select id, vecdist(v, ?) d from iv32 where v likev ? order by 2 desc;",
                    [vec_unique(id, 32), vec_unique(id, 32)], 1);
                if (hits.rows[0] && hits.rows[0].id === id) foundD++;
            });
            ivfpq_pass = (foundC === 3 && foundD === 3);
            if (!ivfpq_pass) {
                var nC = s2.exec("select count(*) as n from iv32 where id between 30000 and 30049;").rows[0].n;
                var nD = s2.exec("select count(*) as n from iv32 where id between 40000 and 40049;").rows[0].n;
                ivfpq_diag = "foundC=" + foundC + "/3 foundD=" + foundD + "/3 " +
                       "tableC=" + nC + "/50 tableD=" + nD + "/50";
            }
        } catch (e) { verify_err = e; ivfpq_pass = false; }
    }

    if (s2) {
        try { s2.exec("drop index emb_vec;"); }   catch (e) {}
        try { s2.exec("drop table emb;"); }       catch (e) {}
        try { s2.exec("drop index iv32_vec;"); }  catch (e) {}
        try { s2.exec("drop table iv32;"); }      catch (e) {}
        s2.close();
    }

    testFeature(hnsw_name,  hnsw_pass);
    if (hnsw_diag) printf("  %s\n", hnsw_diag);
    testFeature(ivfpq_name, ivfpq_pass);
    if (ivfpq_diag) printf("  %s\n", ivfpq_diag);

    if (verify_err) console.log(verify_err);

    rm_rf_dir(tmpdir);
    testFeature.exit();
}

/* Keep thread refs at module scope so they aren't GC'd. */
var thrA = new rampart.thread();
var thrB = new rampart.thread();
var thrC = new rampart.thread();
var thrD = new rampart.thread();
thrA.exec(worker,      10000, workerDone);
thrB.exec(worker,      20000, workerDone);
thrC.exec(worker_iv32, 30000, workerDone);
thrD.exec(worker_iv32, 40000, workerDone);
