/* sql-update-test.js
 *
 * Coverage test for sql.scheduleUpdate() / sql.scheduleRebuild() and the
 * index-updater daemon (js_modules/rampart-sqlUpdate.js, launched from the
 * Sql.connection constructor in rampart-sql.c, progress written by
 * extern/texis/texisapi/sysupdate.c).
 *
 * Runs at real wall-clock cadence (the daemon polls every 60 s and the
 * minimum interval is 60 s), so the full run takes roughly 30 minutes.
 * Run from the test/ directory:  rampart sql-update-test.js
 *
 * Groups:  A argument parsing / SYSUPDATE writes    (no daemon activity)
 *          B autostart, duplicate prevention, relaunch after kill
 *          C threshold gate, correctness, timing, periodic runs
 *          D kill / crash during an operation, concurrent access
 *          E lifecycle: DROP INDEX / DROP TABLE / bad SYSUPDATE schema
 *          F a cycle longer than the 60 s tick must not queue ticks
 *
 * Companion: sql-update-child.js (subprocess helper).
 */

rampart.globalize(rampart.utils);
var Sql = require('rampart-sql');

var testFeature = new (require('./test-feature.js'))({prefix: "sql-update", width: 110});

var tmpdir  = process.scriptPath + '/tmp-sqlupdate';
var child   = process.scriptPath + '/sql-update-child.js';
var rp      = process.installPathExec;
var DIM     = 64;

function rm_rf(p) { try { shell("rm -rf " + p); } catch (e) {} }

/* ---------------------------------------------------------------- *
 * helpers
 * ---------------------------------------------------------------- */

function now() { return Math.floor(Date.now() / 1000); }

function waitFor(pred, secs, step) {
    var end = Date.now() + secs * 1000;
    step = step || 0.1;
    var r;
    while (Date.now() < end) {
        r = pred();
        if (r) return r;
        sleep(step);
    }
    return pred();
}

/* pids of "rampart indexUpdater <realpath db>" processes */
function daemons(db) {
    var real = realPath(db);
    var out = exec("ps", "-eo", "pid,ppid,args").stdout.split('\n');
    var ret = [];
    for (var i = 0; i < out.length; i++) {
        var m = /^\s*(\d+)\s+(\d+)\s+rampart indexUpdater (.*)$/.exec(out[i]);
        if (m && m[3] === real) ret.push({pid: parseInt(m[1]), ppid: parseInt(m[2])});
    }
    return ret;
}

/* sql_helper children of a given pid */
function helpersOf(pid) {
    var out = exec("ps", "-eo", "pid,ppid,args").stdout.split('\n');
    var ret = [];
    for (var i = 0; i < out.length; i++) {
        var m = /^\s*(\d+)\s+(\d+)\s+rampart sql_helper/.exec(out[i]);
        if (m && parseInt(m[2]) === pid) ret.push(parseInt(m[1]));
    }
    return ret;
}

function killDaemons(db, sig) {
    var d = daemons(db);
    for (var i = 0; i < d.length; i++) kill(d[i].pid, sig || 'SIGTERM');
    waitFor(function () { return daemons(db).length === 0; }, 10);
}

function readPidFile(db) {
    try { return parseInt(readFile(db + '/updater.pid', true)); } catch (e) { return -1; }
}

function logText(db) {
    try { return readFile(db + '/update.log', true); } catch (e) { return ''; }
}

/* admin handle that never autostarts the daemon (one per db: polling
   must not reconnect on every read) */
var _admins = {};
function admin(db) {
    if (!_admins[db]) _admins[db] = Sql.connect({path: db, user: '_SYSTEM', noUpdater: true});
    return _admins[db];
}

function suRow(db, name) {
    return admin(db).one("select * from SYSUPDATE where NAME=?", [name]);
}
function suRows(db) {
    return admin(db).exec("select * from SYSUPDATE", {maxRows: -1}).rows;
}

/* A schedule call ends with a plain Sql.connect(), which launches the
   daemon when none is running; the new daemon runs its first cycle at
   once.  So the fast pattern for a "now" test is: killDaemons(db), then
   schedule, then waitDaemon(db).  A schedule written while a daemon is
   already running is picked up on its next 60 s poll instead. */
function waitDaemon(db) {
    return waitFor(function () { var d = daemons(db); return d.length ? d[0].pid : 0; }, 10);
}

function vec(id) {
    var a = new Array(DIM), s = 0;
    for (var i = 0; i < DIM; i++) { var u = Math.sin(id * 31 + i * 17) + 0.001; a[i] = u; s += u * u; }
    var inv = 1 / Math.sqrt(s);
    for (var i = 0; i < DIM; i++) a[i] *= inv;
    return new rampart.vector('f32', a);
}
var WORDS = ["alpha","bravo","charlie","delta","echo","foxtrot","golf","hotel","india","juliet",
             "kilo","lima","mike","november","oscar","papa","quebec","romeo","sierra","tango"];
function doc(id, nw) {
    var s = [];
    for (var i = 0; i < (nw || 30); i++) s.push(WORDS[(id * 7 + i * 13) % WORDS.length] + (id % 97));
    return s.join(' ');
}

/* table t + fulltext / hnsw / ivfpq indexes on `nrows` rows; returns admin handle */
function fixture(db, nrows, nw) {
    rm_rf(db);
    delete _admins[db];
    var s = Sql.connect({path: db, create: true, noUpdater: true});
    s.exec("create table t (id int, txt varchar(4000), v varvecF32(" + DIM + "))");
    for (var i = 0; i < nrows; i++) s.exec("insert into t values (?,?,?)", [i, doc(i, nw), vec(i)]);
    s.exec("create fulltext index t_txt on t (txt)");
    s.exec("create vector index t_hnsw on t (v) with backend 'hnsw'");
    s.exec("create vector index t_ivf on t (v) with backend 'ivfpq' " +
           "vec_pq_nlist 16 vec_pq_m 8 vec_pq_min_points_per_centroid 1");
    return s;
}

function addRows(s, from, n, nw) {
    for (var i = from; i < from + n; i++) s.exec("insert into t values (?,?,?)", [i, doc(i, nw), vec(i)]);
}

/* fold check: after a successful OPTIMIZE the delta must be empty */
function newRowsZero(s, index) {
    s.exec("alter index " + index + " optimize having count(NewRows) > 0");
    var r = s.one("select COMMENTS from SYSUPDATE where NAME=?", [index]);
    return r && /NewRows = 0\)/.test(r.COMMENTS);
}

function spawn(mode, db, a, b) {
    var args = [rp, child, mode, db];
    if (a !== undefined) args.push(String(a));
    if (b !== undefined) args.push(String(b));
    args.push({background: true});
    return exec.apply(null, args).pid;
}
function run(mode, db, a, b) {
    var args = [rp, child, mode, db];
    if (a !== undefined) args.push(String(a));
    if (b !== undefined) args.push(String(b));
    args.push({timeout: 600000});
    return exec.apply(null, args);
}

/* parse "<idx> update started:<%c %z>, completed:<%c %z>" log lines */
function runsFromLog(db) {
    var lines = logText(db).split('\n'), ret = [];
    for (var i = 0; i < lines.length; i++) {
        var m = /^(\S+ \S+) - (\S+) update started:(.*), completed:(.*)$/.exec(lines[i]);
        if (!m) continue;
        var st = autoScanDate(m[3]), en = autoScanDate(m[4]);
        ret.push({index: m[2], start: st ? Math.floor(st.date.getTime() / 1000) : -1,
                  end: en ? Math.floor(en.date.getTime() / 1000) : -1, line: lines[i]});
    }
    return ret;
}

/* ---------------------------------------------------------------- *
 * setup / teardown
 * ---------------------------------------------------------------- */

var allDbs = [];
function newDb(name) { var p = tmpdir + '/' + name; allDbs.push(p); return p; }

function cleanup() {
    for (var i = 0; i < allDbs.length; i++) killDaemons(allDbs[i], 'SIGKILL');
    rm_rf(tmpdir);
}

/* leftovers from an earlier aborted run */
(function () {
    var out = exec("ps", "-eo", "pid,args").stdout.split('\n');
    for (var i = 0; i < out.length; i++) {
        var m = /^\s*(\d+)\s+rampart indexUpdater (.*)$/.exec(out[i]);
        if (m && m[2].indexOf(tmpdir) === 0) kill(parseInt(m[1]), 'SIGKILL');
    }
})();
rm_rf(tmpdir);
mkdir(tmpdir);

var t0 = Date.now();
function elapsed() { return sprintf("%.0fs", (Date.now() - t0) / 1000); }

/* ================================================================ *
 * A. argument parsing and SYSUPDATE writes
 * ================================================================ */

var A = newDb('a');
var asql = fixture(A, 400);
asql.exec("create index t_id on t (id)");

testFeature("A: rejects unknown index / non-text index / bad args", function () {
    testFeature.mustThrow(function () { asql.scheduleUpdate("nope", "now", "daily"); }, "unknown index");
    testFeature.mustThrow(function () { asql.scheduleUpdate("t_id", "now", "daily"); }, "btree index");
    testFeature.mustThrow(function () { asql.scheduleUpdate(42, "now", "daily"); }, "non-string index");
    testFeature.mustThrow(function () { asql.scheduleUpdate("t_txt", "not a date at all", "daily"); }, "bad date");
    testFeature.mustThrow(function () { asql.scheduleUpdate("t_txt", "now", "fortnightly"); }, "bad interval word");
    testFeature.mustThrow(function () { asql.scheduleUpdate("t_txt", "now", "59 seconds"); }, "seconds unit");
    testFeature.mustThrow(function () { asql.scheduleUpdate("t_txt", "now", 59); }, "interval < 60");
    testFeature.mustThrow(function () { asql.scheduleUpdate("t_txt", "now", {}); }, "interval object");
    testFeature.mustThrow(function () { asql.scheduleUpdate("t_txt", "now", "daily", 0); }, "minRows 0");
    testFeature.mustThrow(function () { asql.scheduleUpdate("t_txt", "now", "daily", "5"); }, "minRows string");
    testFeature.mustEq(suRows(A).filter(function (r) { return r.NEXT >= 0; }).length, 0, "no schedule written by rejected calls");
});

testFeature("A: interval strings map to INTV seconds", function () {
    var far = new Date(Date.now() + 3600 * 1000 * 24 * 30);   /* a month out: daemon idles */
    var cases = [["hourly", 3600], ["daily", 86400], ["weekly", 604800], ["every third day", 259200],
                 ["120 minutes", 7200], ["2 weeks", 1209600], ["every 2 hours", 7200], ["1 hour", 3600],
                 ["every minute", 60], ["30 minutes", 1800], [86400, 86400], [60, 60], [3661, 3661]];
    for (var i = 0; i < cases.length; i++) {
        asql.scheduleUpdate("t_txt", far, cases[i][0]);
        testFeature.mustEq(suRow(A, "t_txt").INTV, cases[i][1], "interval " + JSON.stringify(cases[i][0]));
    }
});

testFeature("A: startTime forms produce the documented NEXT epoch", function () {
    var d = new Date(2030, 0, 2, 3, 4, 5);
    asql.scheduleUpdate("t_txt", d, "daily");
    testFeature.mustEq(suRow(A, "t_txt").NEXT, Math.floor(d.getTime() / 1000), "Date object");

    asql.scheduleUpdate("t_txt", 1900000000, "daily");
    testFeature.mustEq(suRow(A, "t_txt").NEXT, 1900000000, "numeric epoch");

    asql.scheduleUpdate("t_txt", "2030-01-02 03:04:05", "daily");
    testFeature.mustEq(suRow(A, "t_txt").NEXT, Math.floor(new Date(2030, 0, 2, 3, 4, 5).getTime() / 1000),
                       "string without zone is local time");

    asql.scheduleUpdate("t_txt", "2030-01-02 03:04:05 UTC", "daily");
    testFeature.mustEq(suRow(A, "t_txt").NEXT, Math.floor(Date.UTC(2030, 0, 2, 3, 4, 5) / 1000),
                       "string with UTC zone");

    asql.scheduleUpdate("t_txt", "2030-01-02 03:04:05 -0500", "daily");
    testFeature.mustEq(suRow(A, "t_txt").NEXT, Math.floor(Date.UTC(2030, 0, 2, 8, 4, 5) / 1000),
                       "string with numeric offset");

    /* last: a 'now' row is run by the daemon's first cycle, which would
       race the reads above */
    asql.scheduleUpdate("t_txt", "now", "daily");
    testFeature.mustEq(suRow(A, "t_txt").NEXT, 0, "'now' -> 0");
    killDaemons(A);
});

testFeature("A: minRows defaults 1000 (fulltext) / 10000 (vec); KIND labels; explicit override", function () {
    var far = new Date(Date.now() + 30 * 86400 * 1000);
    asql.scheduleUpdate("t_txt", far, "daily");
    asql.scheduleUpdate("t_hnsw", far, "daily");
    asql.scheduleUpdate("t_ivf", far, "daily");
    var ft = suRow(A, "t_txt"), hn = suRow(A, "t_hnsw"), iv = suRow(A, "t_ivf");
    testFeature.mustEq(ft.THRESH, 1000, "fulltext default");
    testFeature.mustEq(hn.THRESH, 10000, "hnsw default");
    testFeature.mustEq(iv.THRESH, 10000, "ivfpq default");
    testFeature.mustEq(ft.KIND, "fulltext", "fulltext kind");
    testFeature.mustEq(hn.KIND, "vec-hnsw", "hnsw kind");
    testFeature.mustEq(iv.KIND, "vec-ivfpq", "ivfpq kind");
    testFeature.mustEq(ft.TBNAME, "t", "TBNAME");
    asql.scheduleUpdate("t_txt", far, "daily", 5);
    testFeature.mustEq(suRow(A, "t_txt").THRESH, 5, "explicit minRows");
    testFeature.mustEq(JSON.parse(suRow(A, "t_txt").PARAMS).action, "optimize", "PARAMS.action");
});

testFeature("A: rescheduling replaces the row; scheduleRebuild/scheduleUpdate flip PARAMS.action", function () {
    var far = new Date(Date.now() + 30 * 86400 * 1000);
    asql.scheduleUpdate("t_ivf", far, "daily", 7);
    asql.scheduleUpdate("t_ivf", far, "weekly", 9);
    var rows = suRows(A).filter(function (r) { return r.NAME === "t_ivf"; });
    testFeature.mustEq(rows.length, 1, "one row per index");
    testFeature.mustEq(rows[0].INTV, 604800, "replaced INTV");
    testFeature.mustEq(rows[0].THRESH, 9, "replaced THRESH");
    asql.scheduleRebuild("t_ivf", far, "daily", 3);
    rows = suRows(A).filter(function (r) { return r.NAME === "t_ivf"; });
    testFeature.mustEq(rows.length, 1, "still one row after scheduleRebuild");
    testFeature.mustEq(JSON.parse(rows[0].PARAMS).action, "rebuild", "action=rebuild");
    testFeature.mustEq(rows[0].THRESH, 3, "rebuild THRESH");
    asql.scheduleUpdate("t_ivf", far, "daily", 4);
    testFeature.mustEq(JSON.parse(suRow(A, "t_ivf").PARAMS).action, "optimize", "back to optimize");
});

testFeature("A: schedule preserves texis tracking columns (PREVIOUS/COMMENTS from CREATE INDEX)", function () {
    var before = suRow(A, "t_hnsw");
    testFeature.must(before.PREVIOUS > 0 && /completed at/.test(before.COMMENTS), "CREATE INDEX wrote tracking row");
    asql.scheduleUpdate("t_hnsw", new Date(Date.now() + 30 * 86400 * 1000), "daily");
    var after = suRow(A, "t_hnsw");
    testFeature.mustEq(after.PREVIOUS, before.PREVIOUS, "PREVIOUS kept");
    testFeature.mustEq(after.COMMENTS, before.COMMENTS, "COMMENTS kept");
});

testFeature("A: schedule with no tracking row inserts one (PREVIOUS=-1)", function () {
    admin(A).exec("delete from SYSUPDATE where NAME='t_hnsw'");
    testFeature.must(!suRow(A, "t_hnsw"), "row removed");
    asql.scheduleUpdate("t_hnsw", new Date(Date.now() + 30 * 86400 * 1000), "daily");
    var r = suRow(A, "t_hnsw");
    testFeature.must(!!r, "row inserted");
    testFeature.mustEq(r.PREVIOUS, -1, "PREVIOUS -1");
    testFeature.mustEq(r.KIND, "vec-hnsw", "KIND");
    testFeature.mustEq(r.TBNAME, "t", "TBNAME");
});

testFeature("A: -1 / 'never' / 'delete' unschedule: NEXT/INTV/THRESH=-1, tracking row retained", function () {
    /* rampart-sqlUpdate.js deliberately keeps the row so COMMENTS history
       survives; rampart-sql.rst ("the record for the index is deleted") is
       out of date on this point. */
    var forms = [-1, "never", "delete", "NEVER"];
    for (var i = 0; i < forms.length; i++) {
        asql.scheduleUpdate("t_txt", new Date(Date.now() + 30 * 86400 * 1000), "daily", 5);
        asql.scheduleUpdate("t_txt", forms[i]);
        var r = suRow(A, "t_txt");
        testFeature.must(!!r, "row retained for " + JSON.stringify(forms[i]));
        testFeature.mustEq(r.NEXT, -1, "NEXT for " + JSON.stringify(forms[i]));
        testFeature.mustEq(r.INTV, -1, "INTV");
        testFeature.mustEq(r.THRESH, -1, "THRESH");
        testFeature.must(!r.PARAMS, "PARAMS cleared");
    }
    /* unscheduling an index that was never scheduled is a no-op, not an error */
    asql.scheduleUpdate("t_ivf", -1);
    asql.scheduleUpdate("t_hnsw", -1);
    testFeature.mustEq(suRows(A).filter(function (r) { return r.NEXT >= 0; }).length, 0, "nothing scheduled");
});

testFeature("A: scheduleUpdate() launched the updater; unscheduling everything makes it exit", function () {
    /* the far-future schedules above already launched a daemon */
    var d = waitFor(function () { return daemons(A).length; }, 5);
    testFeature.mustEq(d, 1, "one daemon after scheduleUpdate()");
    /* all rows are NEXT=-1 now: next cycle sees nothing and exits */
    var gone = waitFor(function () { return daemons(A).length === 0; }, 130, 1);
    testFeature.must(gone, "daemon exited within two cycles");
    testFeature.mustContain(logText(A), "SYSUPDATE is empty - exiting", "exit logged");
    Sql.connect(A);
    sleep(2);
    testFeature.mustEq(daemons(A).length, 0, "connect does not relaunch with nothing scheduled");
});
printf("-- group A done (%s)\n", elapsed());

/* ================================================================ *
 * B. autostart and duplicate prevention
 * ================================================================ */

var B = newDb('b');
var bsql = fixture(B, 300);
var far = new Date(Date.now() + 30 * 86400 * 1000);

testFeature("B: CREATE INDEX tracking rows alone (NEXT=-1) do not autostart", function () {
    testFeature.must(suRows(B).length === 3, "three tracking rows");
    var s = Sql.connect(B);
    sleep(2);
    testFeature.mustEq(daemons(B).length, 0, "no daemon");
    testFeature.must(!s.errMsg, "no errMsg");
});

testFeature("B: db with no SYSUPDATE table does not autostart and sets no errMsg", function () {
    var E = newDb('empty');
    var s = Sql.connect(E, true);
    s.exec("create table x (i int)");
    testFeature.must(!s.one("select * from SYSTABLES where NAME='SYSUPDATE'"), "no SYSUPDATE");
    s = Sql.connect(E);
    sleep(1);
    testFeature.mustEq(daemons(E).length, 0, "no daemon");
    testFeature.must(!s.errMsg, "no errMsg");
});

testFeature("B: first schedule launches exactly one detached daemon; pid file and log agree", function () {
    bsql.scheduleUpdate("t_txt", far, "daily");
    var d = waitFor(function () { var d = daemons(B); return d.length ? d : null; }, 5);
    testFeature.must(d && d.length === 1, "one daemon");
    testFeature.must(d[0].ppid !== process.getpid(), "detached from launcher");
    testFeature.must(waitFor(function () { return readPidFile(B) === d[0].pid; }, 5), "updater.pid holds daemon pid");
    testFeature.mustContain(logText(B), "updater started with pid " + d[0].pid, "log line");
    var tmp = exec("sh", "-c", "ls " + B + "/updater.pid.tmp.* 2>/dev/null").stdout;
    testFeature.mustEq(tmp, "", "no leftover updater.pid.tmp.* files");
});

testFeature("B: repeated connects (same process, threads, child processes) never add a daemon", function () {
    for (var i = 0; i < 5; i++) Sql.connect(B);
    var thr = [];
    for (var i = 0; i < 4; i++) {
        var th = new rampart.thread();
        th.exec(function (db) { require('rampart-sql').connect(db); return 1; }, B);
        thr.push(th);
    }
    var pids = [];
    for (var i = 0; i < 6; i++) pids.push(spawn('connect', B));
    waitFor(function () { for (var i = 0; i < pids.length; i++) if (kill(pids[i], 0)) return false; return true; }, 30);
    sleep(2);
    testFeature.mustEq(daemons(B).length, 1, "still one daemon");
});

testFeature("B: SIGTERM'd daemon is relaunched by plain connect, not by noUpdater connect", function () {
    var old = daemons(B)[0].pid;
    killDaemons(B, 'SIGTERM');
    testFeature.mustEq(daemons(B).length, 0, "killed");
    admin(B);
    sleep(2);
    testFeature.mustEq(daemons(B).length, 0, "noUpdater connect does not relaunch");
    Sql.connect(B);
    var d = waitFor(function () { var d = daemons(B); return d.length ? d : null; }, 5);
    testFeature.must(d && d.length === 1 && d[0].pid !== old, "relaunched with new pid");
    testFeature.must(waitFor(function () { return readPidFile(B) === d[0].pid; }, 5), "pid file updated");
});

testFeature("B: SIGKILL'd daemon is relaunched", function () {
    killDaemons(B, 'SIGKILL');
    Sql.connect(B);
    var d = waitFor(function () { var d = daemons(B); return d.length ? d : null; }, 5);
    testFeature.must(d && d.length === 1, "relaunched");
});

testFeature("B: stale updater.pid with a dead pid is replaced", function () {
    killDaemons(B, 'SIGKILL');
    fprintf(B + '/updater.pid', "%d", 3999999);
    Sql.connect(B);
    var d = waitFor(function () { var d = daemons(B); return d.length ? d : null; }, 5);
    testFeature.must(d && d.length === 1, "relaunched");
    testFeature.must(waitFor(function () { return readPidFile(B) === d[0].pid; }, 5), "pid file replaced");
});

testFeature("B: stale updater.pid naming a live unrelated process does not block relaunch", function () {
    /* pid reuse: the file names a process that is alive but is not an updater */
    killDaemons(B, 'SIGKILL');
    var sleeper = exec("sleep", "600", {background: true}).pid;
    fprintf(B + '/updater.pid', "%d", sleeper);
    Sql.connect(B);
    var d = waitFor(function () { var d = daemons(B); return d.length ? d : null; }, 5);
    kill(sleeper, 'SIGKILL');
    testFeature.must(d && d.length === 1, "daemon launched despite foreign live pid in updater.pid");
});

testFeature("B: 8 simultaneous first-connects race to exactly one daemon", function () {
    killDaemons(B, 'SIGKILL');
    try { rmFile(B + '/updater.pid'); } catch (e) {}
    var pids = [];
    for (var i = 0; i < 8; i++) pids.push(spawn('connect', B));
    waitFor(function () { for (var i = 0; i < pids.length; i++) if (kill(pids[i], 0)) return false; return true; }, 30);
    sleep(3);
    var d = daemons(B);
    testFeature.mustEq(d.length, 1, "one daemon");
    testFeature.mustEq(readPidFile(B), d[0].pid, "pid file names the survivor");
    var tmp = exec("sh", "-c", "ls " + B + "/updater.pid.tmp.* 2>/dev/null").stdout;
    testFeature.mustEq(tmp, "", "no leftover tmp pid files");
});

testFeature("B: two databases get independent daemons", function () {
    var B2 = newDb('b2');
    var s2 = fixture(B2, 300);
    s2.scheduleUpdate("t_hnsw", far, "daily");
    var d2 = waitFor(function () { var d = daemons(B2); return d.length ? d : null; }, 5);
    testFeature.must(d2 && d2.length === 1, "second daemon");
    testFeature.mustEq(daemons(B).length, 1, "first still one");
    killDaemons(B2, 'SIGKILL');
    sleep(1);
    testFeature.mustEq(daemons(B).length, 1, "first unaffected");
});

testFeature("B: launcher process killed right after connect: daemon survives", function () {
    killDaemons(B, 'SIGKILL');
    var hp = spawn('hold', B, 30);
    var d = waitFor(function () { var d = daemons(B); return d.length ? d : null; }, 10);
    kill(hp, 'SIGKILL');
    sleep(1);
    testFeature.must(d && daemons(B).length === 1 && daemons(B)[0].pid === d[0].pid, "daemon outlives launcher");
    testFeature.must(daemons(B)[0].ppid !== hp, "not a child of the launcher");
});
printf("-- group B done (%s)\n", elapsed());

/* ================================================================ *
 * C. gate, correctness, timing
 * ================================================================ */

var C = newDb('c');
var csql = fixture(C, 2000);
var createPrev = {};
suRows(C).forEach(function (r) { createPrev[r.NAME] = r.PREVIOUS; });

testFeature("C: fulltext below minRows: skipped note, PREVIOUS unchanged, NEXT advanced by INTV", function () {
    addRows(csql, 500, 10);
    killDaemons(C);
    var t = now();
    csql.scheduleUpdate("t_txt", "now", "hourly", 50);
    waitDaemon(C);
    var r = waitFor(function () { var r = suRow(C, "t_txt"); return /skipped/.test(r.COMMENTS) ? r : null; }, 20);
    testFeature.must(!!r, "skipped comment appeared");
    testFeature.mustContain(r.COMMENTS, "HAVING condition not met (NewRows = 10)", "count in note");
    testFeature.mustEq(r.PREVIOUS, createPrev.t_txt, "PREVIOUS unchanged");
    testFeature.must(r.NEXT >= t + 3600 - 5 && r.NEXT <= t + 3600 + 30, "NEXT ~ now+3600, got " + (r.NEXT - t));
    testFeature.must(!r.ACTION && r.STAGE === 0, "row idle");
});

testFeature("C: fulltext above minRows: completed, delta folded, new rows searchable", function () {
    killDaemons(C);
    var t = now();
    csql.scheduleUpdate("t_txt", "now", "hourly", 5);
    waitDaemon(C);
    /* texis writes COMMENTS inside ALTER; the daemon writes NEXT after it returns */
    var r = waitFor(function () { var r = suRow(C, "t_txt"); return (/completed at/.test(r.COMMENTS) && r.NEXT > 0) ? r : null; }, 60);
    testFeature.must(!!r, "completed");
    testFeature.must(r.PREVIOUS >= t - 1, "PREVIOUS set to run time");
    testFeature.must(r.NEXT > t, "NEXT in the future");
    testFeature.must(newRowsZero(csql, "t_txt"), "delta folded (NewRows = 0)");
    var c = csql.one("select count(*) c from t where txt like ?", [WORDS[(505 * 7) % WORDS.length] + (505 % 97)]).c;
    testFeature.must(c >= 1, "row inserted before optimize is found");
    testFeature.mustContain(logText(C), "t_txt update started:", "run logged");
});

testFeature("C: fulltext minRows counts inserts only (deletes ignored)", function () {
    csql.exec("delete from t where id >= 480 and id < 510");
    killDaemons(C);
    csql.scheduleUpdate("t_txt", "now", "hourly", 5);
    waitDaemon(C);
    var r = waitFor(function () { var r = suRow(C, "t_txt"); return /skipped/.test(r.COMMENTS) ? r : null; }, 20);
    testFeature.must(!!r && /NewRows = 0\)/.test(r.COMMENTS), "30 deletes count as 0 new rows: " + (r && r.COMMENTS));
});

function vecGate(index, backend) {
    testFeature("C: " + backend + " UPDATE counts twice; gate is strictly > minRows (8 skip, 10 skip, 12 run)", function () {
        /* earlier deletes are tombstones for the vec indexes: fold first */
        csql.exec("alter index " + index + " optimize");
        testFeature.must(newRowsZero(csql, index), "delta empty before test");
        for (var i = 0; i < 4; i++) csql.exec("update t set v=? where id=?", [vec(9000 + i), i]);
        killDaemons(C);
        csql.scheduleUpdate(index, "now", "hourly", 10);
        waitDaemon(C);
        var r = waitFor(function () { var r = suRow(C, index); return /skipped/.test(r.COMMENTS) ? r : null; }, 20);
        testFeature.must(!!r && /NewRows = 8\)/.test(r.COMMENTS), "4 updates -> NewRows = 8: " + (r && r.COMMENTS));
        var prev = r.PREVIOUS;
        /* boundary: exactly minRows pending does not trigger */
        csql.exec("update t set v=? where id=?", [vec(9004), 4]);
        killDaemons(C);
        csql.scheduleUpdate(index, "now", "hourly", 10);
        waitDaemon(C);
        r = waitFor(function () { var r = suRow(C, index); return /NewRows = 10\)/.test(r.COMMENTS) ? r : null; }, 20);
        testFeature.must(!!r, "5 updates -> NewRows = 10 still skipped: " + (r && r.COMMENTS));
        csql.exec("update t set v=? where id=?", [vec(9005), 5]);
        killDaemons(C);
        var t = now();
        csql.scheduleUpdate(index, "now", "hourly", 10);
        waitDaemon(C);
        r = waitFor(function () { var r = suRow(C, index); return (/completed at/.test(r.COMMENTS) && r.NEXT > 0) ? r : null; }, 60);
        testFeature.must(!!r, "completed after 6 updates (NewRows = 12)");
        /* PREVIOUS has 1 s resolution: on a fast host the whole sequence
           can land in one second, so compare with the schedule time */
        testFeature.must(r.PREVIOUS >= prev && r.PREVIOUS >= t, "PREVIOUS set by the run");
        testFeature.must(r.NEXT > t, "NEXT in the future after a run that absorbed rows (got " + (r.NEXT - t) + ")");
        testFeature.must(!r.ACTION && r.STAGE === 0 && r.NSTAGES === 0, "row idle");
        testFeature.must(newRowsZero(csql, index), "delta folded");
        var top = csql.exec("select id from t where v likev ?", [vec(9005)], 1).rows;
        testFeature.must(top.length === 1 && top[0].id === 5, backend + " likev finds the updated vector");
    });
}
vecGate("t_hnsw", "hnsw");
vecGate("t_ivf", "ivfpq");

testFeature("C: scheduleRebuild(ivfpq): ACTION/NSTAGES visible during run, completes, action retained", function () {
    /* 2000 -> 8000 rows so the rebuild lasts long enough to sample
       (texis refuses insert-select from a table into itself) */
    csql.exec("create table cstage (id int, txt varchar(4000), v varvecF32(" + DIM + "))");
    csql.exec("insert into cstage select id, txt, v from t");
    csql.exec("insert into t select id + 100000, txt, v from cstage");
    csql.exec("insert into t select id + 200000, txt, v from cstage");
    csql.exec("insert into t select id + 300000, txt, v from cstage");
    killDaemons(C);
    var t = now();
    csql.scheduleRebuild("t_ivf", "now", "hourly", 1);
    var seen = null;
    var r = waitFor(function () {
        var r = suRow(C, "t_ivf");
        if (r.ACTION && !seen) seen = r;
        return /completed at/.test(r.COMMENTS) && !r.ACTION ? r : null;
    }, 300, 0.02);
    testFeature.must(!!r, "completed");
    /* deterministic oracle: the daemon logs texis's stage messages */
    var log = waitFor(function () { var l = logText(C); return /REBUILD stage \d\/5/.test(l) ? l : null; }, 10);
    testFeature.must(!!log, "REBUILD with 5 stages reported in update.log");
    var run = runsFromLog(C).filter(function (x) { return x.index === 't_ivf' && x.start >= t - 1; }).pop();
    if (seen) {
        testFeature.mustEq(seen.ACTION, "rebuild", "ACTION during run");
        testFeature.mustEq(seen.NSTAGES, 5, "ivfpq rebuild has 5 stages");
        testFeature.must(seen.STAGE >= 1 && seen.STAGE <= 5 && seen.STARTED > 0, "STAGE/STARTED set");
    } else {
        /* only acceptable when the run was too short to sample */
        testFeature.must(run && run.end - run.start < 2, "in-progress state observed (run took " + (run ? run.end - run.start : "?") + " s)");
        printf("    (rebuild finished in under 2 s; in-progress columns not sampled)\n");
    }
    testFeature.mustEq(JSON.parse(r.PARAMS).action, "rebuild", "action retained after run");
    testFeature.must(newRowsZero(csql, "t_ivf"), "delta folded by rebuild");
});

testFeature("C: three indexes due together are serialized, all complete", function () {
    /* NEXT = now+30: the launch cycle sees them as not yet due, the
       next poll (~60 s later) runs all three in one cycle */
    addRows(csql, 700, 40);
    killDaemons(C);
    var due = new Date((now() + 30) * 1000);
    csql.scheduleUpdate("t_txt", due, "hourly", 1);
    csql.scheduleUpdate("t_hnsw", due, "hourly", 1);
    csql.scheduleRebuild("t_ivf", due, "hourly", 1);
    var concurrent = false, t = now();
    waitDaemon(C);
    var done = waitFor(function () {
        var rows = suRows(C), active = 0, fin = 0;
        for (var i = 0; i < rows.length; i++) {
            if (rows[i].ACTION) active++;
            if (rows[i].PREVIOUS >= t && !rows[i].ACTION) fin++;
        }
        if (active > 1) concurrent = true;
        return fin === 3;
    }, 180, 0.02);
    testFeature.must(done, "all three completed");
    testFeature.must(!concurrent, "never more than one ACTION at a time");
    /* latest logged run per index since t (no slack: a run from the
       previous test can share t's second) */
    var runs = waitFor(function () {
        var last = {};
        runsFromLog(C).forEach(function (x) { if (x.start >= t) last[x.index] = x; });
        var r = ['t_txt', 't_hnsw', 't_ivf'].map(function (n) { return last[n]; });
        return (r[0] && r[1] && r[2]) ? r : null;
    }, 10);
    testFeature.must(!!runs, "three runs logged");
    testFeature.must(runs[0].start === runs[1].start && runs[1].start === runs[2].start, "same cycle");
});

testFeature("C: a running daemon notices a new schedule within one cycle (no restart)", function () {
    var pid = daemons(C)[0].pid;
    addRows(csql, 800, 10);
    csql.scheduleUpdate("t_hnsw", "now", "hourly", 1);
    var t = now();
    var r = waitFor(function () { var r = suRow(C, "t_hnsw"); return r.PREVIOUS >= t ? r : null; }, 75, 0.5);
    testFeature.must(!!r, "ran within 75 s");
    testFeature.mustEq(daemons(C)[0].pid, pid, "same daemon");
});

testFeature("C: timed schedule runs in [NEXT, NEXT+65s]; new NEXT == NEXT + INTV exactly", function () {
    addRows(csql, 900, 10);
    var start = now() + 90;
    csql.scheduleUpdate("t_txt", new Date(start * 1000), "5 minutes", 1);
    var t = now();
    testFeature.mustEq(suRow(C, "t_txt").NEXT, start, "NEXT written");
    testFeature.mustEq(suRow(C, "t_txt").INTV, 300, "INTV written");
    var r = waitFor(function () { var r = suRow(C, "t_txt"); return r.PREVIOUS >= t ? r : null; }, 170, 0.5);
    testFeature.must(!!r, "ran");
    testFeature.must(r.PREVIOUS >= start && r.PREVIOUS <= start + 65, "ran at +" + (r.PREVIOUS - start) + "s");
    testFeature.mustEq(r.NEXT, start + 300, "NEXT advanced by exactly INTV");
    var early = runsFromLog(C).filter(function (x) { return x.index === 't_txt' && x.start >= t && x.start < start - 1; });
    testFeature.mustEq(early.length, 0, "did not run before NEXT");
});

testFeature("C: 'every minute' schedule runs on consecutive cycles", function () {
    /* the daemon logs every run, skipped or not: use the log for cadence */
    killDaemons(C);
    sleep(1);
    var t = now();
    csql.scheduleUpdate("t_hnsw", "now", "every minute", 1);
    waitDaemon(C);
    function hnswRuns() { return runsFromLog(C).filter(function (r) { return r.index === 't_hnsw' && r.start >= t - 1; }); }
    var r1 = waitFor(function () { var r = hnswRuns(); return r.length >= 1 ? r : null; }, 30, 0.5);
    testFeature.must(!!r1, "first run within 30 s");
    var r2 = waitFor(function () { var r = hnswRuns(); return r.length >= 2 ? r : null; }, 130, 0.5);
    testFeature.must(!!r2, "second run");
    var gap = r2[1].start - r2[0].start;
    testFeature.must(gap >= 55 && gap <= 70, "gap ~60 s, got " + gap);
    var row = suRow(C, "t_hnsw");
    testFeature.must(row.NEXT > r2[1].start, "NEXT beyond the last run");
    csql.scheduleUpdate("t_hnsw", -1);
});

testFeature("C: a slot missed by more than a minute is skipped, not run late (current behaviour)", function () {
    killDaemons(C);
    csql.scheduleUpdate("t_ivf", new Date((now() - 600) * 1000), "daily", 1);
    var before = suRow(C, "t_ivf");
    Sql.connect(C);
    waitFor(function () { return daemons(C).length === 1; }, 10);
    sleep(70);
    var after = suRow(C, "t_ivf");
    testFeature.mustEq(after.PREVIOUS, before.PREVIOUS, "did not run");
    csql.scheduleUpdate("t_ivf", -1);
});

testFeature("C: ALTER INDEX failure is logged and the daemon keeps running", function () {
    csql.scheduleUpdate("t_txt", -1);
    csql.scheduleUpdate("t_hnsw", -1);
    csql.scheduleUpdate("t_ivf", -1);
    csql.exec("create table u (id int, txt varchar(64))");
    for (var i = 0; i < 50; i++) csql.exec("insert into u values (?,?)", [i, doc(i, 5)]);
    csql.exec("create fulltext index u_txt on u (txt)");
    for (var i = 50; i < 60; i++) csql.exec("insert into u values (?,?)", [i, doc(i, 5)]);
    csql.close();
    killDaemons(C);
    shell("mv " + C + "/u_txt.dat " + C + "/u_txt.dat.bak");
    csql = admin(C);
    csql.scheduleUpdate("u_txt", "now", "hourly", 1);
    var pid = waitDaemon(C);
    var logged = waitFor(function () { return /update error/.test(logText(C)); }, 30);
    shell("mv " + C + "/u_txt.dat.bak " + C + "/u_txt.dat");
    testFeature.must(logged, "error logged");
    testFeature.must(kill(pid, 0), "daemon alive after error");
    csql.scheduleUpdate("u_txt", -1);
});
printf("-- group C done (%s)\n", elapsed());

/* ================================================================ *
 * D. kill during an operation, concurrent access
 * ================================================================ */

var D = newDb('d');
var dsql = fixture(D, 3000, 40);
/* staging copy for cheap large deltas */
dsql.exec("create table stage (id int, txt varchar(4000), v varvecF32(" + DIM + "))");
dsql.exec("insert into stage select id, txt, v from t");

function fileSet(db) {
    return exec("ls", db).stdout.split('\n').filter(function (f) {
        return f && !/^SYS|^update\.log$|^updater\.pid$/.test(f);
    }).sort().join(' ');
}

testFeature("D: SIGKILL during ivfpq REBUILD: index usable, relaunch completes, row returns to idle", function () {
    dsql.exec("insert into t select id + 3000, txt, v from stage");
    var files = fileSet(D);
    killDaemons(D);
    dsql.scheduleRebuild("t_ivf", "now", "hourly", 1);
    var pid = waitDaemon(D);
    var mid = waitFor(function () { var r = suRow(D, "t_ivf"); return (r.ACTION === 'rebuild' && r.STAGE >= 2) ? r : null; }, 60, 0.02);
    testFeature.must(!!mid, "caught rebuild in progress (stage " + (mid && mid.STAGE) + ")");
    kill(pid, 'SIGKILL');
    waitFor(function () { return daemons(D).length === 0; }, 10);
    var stuck = suRow(D, "t_ivf");
    printf("    (after kill: ACTION=%s STAGE=%d)\n", stuck.ACTION, stuck.STAGE);
    var q = dsql.exec("select id from t where v likev ?", [vec(10)], 5).rows;
    testFeature.must(q.length > 0, "likev works after kill");
    Sql.connect(D);
    var r = waitFor(function () { var r = suRow(D, "t_ivf"); return (/completed at/.test(r.COMMENTS) && !r.ACTION && r.STAGE === 0) ? r : null; }, 180, 0.5);
    testFeature.must(!!r, "relaunched daemon completed the rebuild");
    testFeature.must(newRowsZero(dsql, "t_ivf"), "delta folded");
    testFeature.mustEq(fileSet(D), files, "no stray files left in db dir");
});

testFeature("D: SIGKILL during fulltext OPTIMIZE: index usable, relaunch completes", function () {
    dsql.scheduleRebuild("t_ivf", -1);
    dsql.exec("insert into t select id + 6000, txt, v from stage");
    var files = fileSet(D);
    killDaemons(D);
    dsql.scheduleUpdate("t_txt", "now", "hourly", 1);
    var pid = waitDaemon(D);
    /* kill only once the Tnnnn temp index files exist: a Unix fulltext
       OPTIMIZE registers no INDEX_TEMP row, so this is the orphan case */
    var mid = waitFor(function () {
        return /(^|\s)T\d+[a-z]\.dat(\s|$)/.test(exec("ls", D).stdout.replace(/\n/g, ' ')) ? suRow(D, "t_txt") : null;
    }, 60, 0.02);
    testFeature.must(!!mid && mid.ACTION === 'optimize', "caught optimize in progress with temp files on disk");
    kill(pid, 'SIGKILL');
    waitFor(function () { return daemons(D).length === 0; }, 10);
    var c = dsql.one("select count(*) c from t where txt like ?", [WORDS[(6010 * 7) % WORDS.length] + (6010 % 97)]).c;
    testFeature.must(c >= 1, "like query works after kill");
    Sql.connect(D);
    var r = waitFor(function () { var r = suRow(D, "t_txt"); return (/completed at/.test(r.COMMENTS) && !r.ACTION) ? r : null; }, 180, 0.5);
    testFeature.must(!!r, "relaunched daemon completed the optimize");
    testFeature.must(newRowsZero(dsql, "t_txt"), "delta folded");
    testFeature.mustEq(fileSet(D), files, "no stray files left in db dir");
});

/* No sql_helper case: a standalone script (and so the daemon) runs texis
   in-process; the fork-transport helper only exists for server threads.
   The SIGKILL cases above cover death mid-operation. */

testFeature("D: manual ALTER INDEX on the same index while the daemon runs it: both succeed", function () {
    dsql.scheduleUpdate("t_hnsw", -1);
    dsql.exec("insert into t select id + 12000, txt, v from stage");
    killDaemons(D);
    dsql.scheduleRebuild("t_ivf", "now", "hourly", 1);
    var pid = waitDaemon(D);
    var mid = waitFor(function () { var r = suRow(D, "t_ivf"); return r.ACTION === 'rebuild' ? r : null; }, 60, 0.02);
    testFeature.must(!!mid, "daemon rebuild in progress");
    /* texis either waits for the lock or refuses with a clear message;
       it must never silently run two operations on one index */
    var res = run('sql', D, "alter index t_ivf optimize");
    testFeature.must(res.exitStatus === 0 || /being updated or rebuilt/.test(res.stdout),
                     "manual alter serialized or refused cleanly: " + res.stdout + res.stderr);
    var r = waitFor(function () { var r = suRow(D, "t_ivf"); return (/completed at/.test(r.COMMENTS) && !r.ACTION) ? r : null; }, 180, 0.5);
    testFeature.must(!!r, "daemon run completed");
    res = run('sql', D, "alter index t_ivf optimize");
    testFeature.mustEq(res.exitStatus, 0, "manual alter succeeds once the daemon is done: " + res.stdout);
    testFeature.must(!/update error/.test(logText(D).split("\n").slice(-3).join("\n")), "no daemon error");
    testFeature.must(dsql.exec("select id from t where v likev ?", [vec(20)], 5).rows.length > 0, "likev ok");
    testFeature.must(newRowsZero(dsql, "t_ivf"), "delta folded");
});

testFeature("D: rows inserted by another process during OPTIMIZE remain searchable", function () {
    dsql.scheduleRebuild("t_ivf", -1);
    dsql.exec("insert into t select id + 15000, txt, v from stage");
    killDaemons(D);
    dsql.scheduleUpdate("t_txt", "now", "hourly", 1);
    var pid = waitDaemon(D);
    waitFor(function () { return suRow(D, "t_txt").ACTION === 'optimize'; }, 60, 0.02);
    var ins = run('insert', D, 300, 'duringrun');
    testFeature.mustEq(ins.exitStatus, 0, "inserter ok: " + ins.stderr);
    var r = waitFor(function () { var r = suRow(D, "t_txt"); return (/completed at/.test(r.COMMENTS) && !r.ACTION) ? r : null; }, 180, 0.5);
    testFeature.must(!!r, "daemon run completed");
    testFeature.mustEq(dsql.one("select count(*) c from t where txt like 'duringrun'").c, 300, "all 300 found");
    /* 'duringrun299' has no prefix siblings (metamorph matches word prefixes) */
    testFeature.mustEq(dsql.one("select count(*) c from t where txt like 'duringrun299'").c, 1, "specific row found");
    dsql.scheduleUpdate("t_txt", -1);
});
printf("-- group D done (%s)\n", elapsed());

/* ================================================================ *
 * E. lifecycle
 * ================================================================ */

var E = newDb('e');
var esql = fixture(E, 300);

testFeature("E: DROP INDEX removes its schedule; daemon survives; re-CREATE has no schedule", function () {
    esql.scheduleUpdate("t_hnsw", far, "daily");
    esql.scheduleUpdate("t_txt", far, "daily");
    var pid = waitFor(function () { var d = daemons(E); return d.length ? d[0].pid : 0; }, 5);
    testFeature.must(pid > 0, "daemon up");
    esql.exec("drop index t_hnsw");
    testFeature.must(!suRow(E, "t_hnsw"), "SYSUPDATE row removed");
    sleep(65);
    testFeature.must(kill(pid, 0), "daemon alive a cycle later");
    esql.exec("create vector index t_hnsw on t (v) with backend 'hnsw'");
    var r = suRow(E, "t_hnsw");
    testFeature.must(!!r && r.NEXT === -1, "recreated index is unscheduled");
});

testFeature("E: DROP TABLE cascade removes all rows; daemon exits", function () {
    esql.exec("drop table t");
    testFeature.mustEq(suRows(E).length, 0, "SYSUPDATE empty");
    var gone = waitFor(function () { return daemons(E).length === 0; }, 130, 1);
    testFeature.must(gone, "daemon exited");
    Sql.connect(E);
    sleep(2);
    testFeature.mustEq(daemons(E).length, 0, "not relaunched");
});

testFeature("E: scheduleUpdate after SYSUPDATE was dropped recreates it", function () {
    /* texis recreates SYSUPDATE on the next CREATE/ALTER INDEX; a schedule
       call in between must not fail with "Table SYSUPDATE not found" */
    var G = newDb('g');
    var s = fixture(G, 300);
    admin(G).exec("drop table SYSUPDATE");
    testFeature.must(!s.one("select * from SYSTABLES where NAME='SYSUPDATE'"), "dropped");
    s.scheduleUpdate("t_txt", far, "daily");
    var r = suRow(G, "t_txt");
    testFeature.must(!!r && r.INTV === 86400, "schedule written into a recreated SYSUPDATE");
    testFeature.must(waitFor(function () { return daemons(G).length === 1; }, 5), "daemon launched");
    killDaemons(G, 'SIGKILL');
});

testFeature("E: SYSUPDATE with a foreign schema: scheduleUpdate fails cleanly, no daemon", function () {
    var F = newDb('f');
    var s = Sql.connect({path: F, create: true, noUpdater: true});
    s.exec("create table SYSUPDATE (NAME varchar(16), X int)");
    s.exec("create table t (id int, txt varchar(64))");
    for (var i = 0; i < 20; i++) s.exec("insert into t values (?,?)", [i, doc(i, 5)]);
    s.exec("create fulltext index t_txt on t (txt)");
    var threw = false;
    try { s.scheduleUpdate("t_txt", "now", "daily"); } catch (e) { threw = true; }
    sleep(2);
    testFeature.must(threw, "throws");
    testFeature.mustEq(daemons(F).length, 0, "no daemon");
    testFeature.must(!!s.one("select * from t where txt like ?", [WORDS[0] + "0"]), "index still works");
});
printf("-- group E done (%s)\n", elapsed());

/* ================================================================ *
 * F. cycle longer than the tick
 * ================================================================ */

var Fd = newDb('long');
var fsql = fixture(Fd, 3000, 300);
fsql.exec("create table stage (id int, txt varchar(4000), v varvecF32(" + DIM + "))");
fsql.exec("insert into stage select id, txt, v from t");
fsql.exec("drop index t_hnsw");
printf("-- building 60k-row delta for group F (%s)\n", elapsed());
for (var k = 1; k <= 20; k++) fsql.exec("insert into t select id + ?, txt, v from stage", [k * 3000]);
printf("-- delta built (%s)\n", elapsed());

testFeature("F: a cycle longer than 60 s does not queue missed ticks", function () {
    killDaemons(Fd);
    var t = now();
    var due = new Date((t + 30) * 1000);
    fsql.scheduleUpdate("t_txt", due, "every minute", 1);
    fsql.scheduleRebuild("t_ivf", due, "every minute", 1);
    waitDaemon(Fd);
    var first = waitFor(function () {
        var rows = suRows(Fd), fin = 0;
        for (var i = 0; i < rows.length; i++) if (rows[i].PREVIOUS >= t && !rows[i].ACTION) fin++;
        return fin === 2;
    }, 900, 1);
    testFeature.must(first, "first cycle completed");
    var runs = runsFromLog(Fd).filter(function (r) { return r.start >= t - 1; });
    testFeature.must(runs.length >= 2, "both runs logged (got " + runs.length + " log lines)");
    var starts = {}; runs.forEach(function (r) { starts[r.start] = 1; });
    var cyc = Object.keys(starts).map(Number).sort(function (a, b) { return a - b; });
    var dur = Math.max.apply(null, runs.filter(function (r) { return r.start === cyc[0]; }).map(function (r) { return r.end; })) - cyc[0];
    printf("    (first cycle took %d s)\n", dur);
    if (dur < 61) { testFeature.skip("F: first cycle too fast on this host (" + dur + " s)", "inconclusive"); return true; }
    /* let two more cycles happen */
    sleep(150);
    runs = runsFromLog(Fd).filter(function (r) { return r.start >= t - 1; });
    starts = {}; runs.forEach(function (r) { starts[r.start] = 1; });
    cyc = Object.keys(starts).map(Number).sort(function (a, b) { return a - b; });
    testFeature.must(cyc.length >= 3, "at least three cycles logged, got " + cyc.length);
    for (var i = 1; i < cyc.length; i++)
        testFeature.must(cyc[i] - cyc[i - 1] >= 55, "cycle gap " + (cyc[i] - cyc[i - 1]) + " s (ticks queued?)");
    fsql.scheduleUpdate("t_txt", -1);
    fsql.scheduleRebuild("t_ivf", -1);
});
printf("-- group F done (%s)\n", elapsed());

cleanup();
testFeature.exit();
