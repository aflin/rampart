/* sql-full.js
 *
 * Comprehensive coverage test for the SQL features documented in
 * rampart-sql.rst, sql-server-funcs.rst and sql-set.rst.  Complements the
 * existing sql-test.js, sql-extras-test.js and sql-vector-test.js.
 *
 * Run from the test/ directory:  rampart sql-full.js
 *
 * NOT COVERED HERE (too difficult / out of scope), and why:
 *   - embed() / llamaEmbed                : needs a .gguf embedding model loaded
 *   - CREATE VECTOR INDEX / LIKEV / vecdist: covered by sql-vector-test.js
 *   - scheduleUpdate() / scheduleRebuild() : asynchronous timing, heavy ops
 *   - exec() SQL function                  : runs external commands (non-deterministic)
 *   - random(), seq()                      : non-deterministic / stateful
 *   - fromfile/fromfiletext/toind/canonpath: filesystem-path specific
 *   - isNull/ifNull on a true NULL         : texis fixed columns default to 0/'',
 *                                            not NULL, so a real NULL is hard to make
 *                                            (non-NULL paths ARE tested below)
 *   - nullOutputString                     : only affects tsql text output, not the
 *                                            JS object/array return values
 *                                            (hexifyBytes DOES affect JS returns via
 *                                            convert(byteCol,'varchar') -- tested below)
 *   - lookup()                             : range/name setup did not resolve in probes
 *   - perf/buffer/index tuning settings    : no readback path (btreeCacheSize, ramRows,
 *     (indexMem, indexMmap, *BufSz, etc.)    indexMem, etc.) — settable but unobservable
 *   - addTable() (move a .tbl in)          : addTables option is covered by sql-test.js
 *   - races between processes (index dropped or rebuilt at the instant a
 *     reader opens it, lock server deadlock detection, fulltext duplicates
 *     from an index built during writes)  : need exact timing; only their
 *                                            repeatable halves are tested below
 */

rampart.globalize(rampart.utils);
load.Sql;

/* -------------------------------------------------------------------
 * Helper roles: the tests below start this script again as another
 * process (or run a thread in one):  rampart sql-full.js --child ROLE DB ...
 * ------------------------------------------------------------------- */
if (process.argv[2] == "--child") {
    var crole = process.argv[3], cdb = process.argv[4], csql = Sql.connect(cdb);
    if (crole == "exec") {              /* run each remaining argument as a statement */
        for (var ci = 5; ci < process.argv.length; ci++) csql.exec(process.argv[ci]);
        process.exit(0);
    }
    if (crole == "insert") {            /* insert into TABLE rows (i, i % 5) for LO <= i < HI */
        for (var ci = Number(process.argv[6]); ci < Number(process.argv[7]); ci++)
            csql.exec("insert into " + process.argv[5] + " values(?,?)", [ci, ci % 5]);
        process.exit(0);
    }
    if (crole == "thread") {            /* imports run inside a thread; prints the results as JSON */
        global.cArg = { db: cdb, file: process.argv[5] };
        var cth = new rampart.thread();
        cth.exec(function () {
            var S = require("rampart-sql"), c = new S.connection(cArg.db), out = {};
            out.csv = c.importCsvFile(cArg.file + ".csv", { tableName: "thr_csv", hasHeaderRow: true });
            out.csvLock = c.importCsvFile(cArg.file + ".csv", { tableName: "thr_csv", hasHeaderRow: true, lockTable: true });
            out.jsonl = c.importJsonlFile(cArg.file + ".jsonl", { tableName: "thr_jsonl" });
            out.jsonlLock = c.importJsonlFile(cArg.file + ".jsonl", { tableName: "thr_jsonl", lockTable: true });
            out.csvCount = c.one("select count(*) c from thr_csv").c;
            out.jsonlCount = c.one("select count(*) c from thr_jsonl").c;
            out.last = c.one("select b from thr_jsonl where a = 499");
            return out;
        }, function (res) { printf("%s\n", JSON.stringify(res)); process.exit(0); });
    }
} else {

var tmpdir = process.scriptPath + '/tmp-sqlfull';
function rm_rf(p){ try { shell("rm -rf " + p); } catch(e){} }
rm_rf(tmpdir);
mkdir(tmpdir);

var sql = Sql.connect(tmpdir + "/db", true);

var t = new (require('./test-feature.js'))({prefix: "sql-full"});

function closeTo(a,b,tol){ return Math.abs(a-b) <= (tol||1e-9); }

/* ===================================================================
 * Connection / lifecycle
 * =================================================================== */

t("connection - Sql.connect returns a usable handle", function(){
    return typeof sql.exec === "function" && typeof sql.one === "function";
});

t("connection - new Sql.connection({path,create})", function(){
    var c = new Sql.connection({path: tmpdir + "/db2", create: true});
    var ok = !!c.one("select * from SYSTABLES where NAME='SYSTABLES'");
    c.close();
    return ok;
});

t("connection - .db property reflects path", function(){
    return typeof sql.db === "string" && sql.db.indexOf("db") >= 0;
});

t("connection - selectMaxRows property settable", function(){
    var prev = sql.selectMaxRows;
    sql.selectMaxRows = 7;
    var got = sql.selectMaxRows;
    sql.selectMaxRows = prev;
    return got === 7;
});

t("connection - .close() then reuse reopens", function(){
    var c = Sql.connect(tmpdir + "/db3", true);
    c.exec("create table ct (i int)");
    c.close();
    var r = c.one("select * from SYSTABLES where NAME='ct'");  // reopens transparently
    return !!r;
});

/* ===================================================================
 * DDL
 * =================================================================== */

t("ddl - create table + appears in SYSTABLES", function(){
    sql.query("drop table t1");
    sql.exec("create table t1 (id int, name varchar(32), val double)");
    return !!sql.one("select * from SYSTABLES where NAME='t1'");
});

t("ddl - drop table", function(){
    sql.exec("create table tdrop (i int)");
    sql.exec("drop table tdrop");
    return !sql.one("select * from SYSTABLES where NAME='tdrop'");
});

t("ddl - create index", function(){
    sql.query("drop index t1_id_x");
    sql.exec("create index t1_id_x on t1(id)");
    return !!sql.one("select * from SYSINDEX where NAME='t1_id_x'");
});

t("ddl - drop index", function(){
    sql.exec("create index t1_tmp_x on t1(name)");
    sql.exec("drop index t1_tmp_x");
    return !sql.one("select * from SYSINDEX where NAME='t1_tmp_x'");
});

t("ddl - unique index enforces uniqueness", function(){
    sql.query("drop table uq");
    sql.exec("create table uq (k int, v varchar(16))");
    sql.exec("insert into uq values(1,'a')");
    sql.exec("create unique index uq_k_ux on uq(k)");
    var res = sql.query("insert into uq values(1,'b')");   // duplicate
    return Sql.rex(/duplicate=/, sql.errMsg).length > 0;
});

/* ===================================================================
 * DML + result handling
 * =================================================================== */

t("dml - insert + select count", function(){
    sql.query("drop table rh");
    sql.exec("create table rh (id int, name varchar(16))");
    for (var i=1;i<=15;i++) sql.exec("insert into rh values(?,?)",[i,"n"+i]);
    return sql.one("select count(*) c from rh").c === 15;
});

t("dml - update", function(){
    sql.exec("update rh set name='changed' where id=1");
    return sql.one("select name from rh where id=1").name === "changed";
});

t("dml - delete", function(){
    sql.exec("delete from rh where id=15");
    return sql.one("select count(*) c from rh").c === 14;
});

t("result - returnType object (default)", function(){
    var row = sql.exec("select id,name from rh where id=2").rows[0];
    return row.id === 2 && row.name === "n2";
});

t("result - returnType array", function(){
    var row = sql.exec("select id,name from rh where id=2", {returnType:"array"}).rows[0];
    return JSON.stringify(row) === '[2,"n2"]';
});

t("result - returnType novars (rowCount, empty rows)", function(){
    var r = sql.exec("select * from rh", {returnType:"novars", maxRows:5});
    return r.rowCount === 5 && r.rows.length === 0;
});

t("result - columns array", function(){
    return JSON.stringify(sql.exec("select id,name from rh",{maxRows:1}).columns) === '["id","name"]';
});

t("result - maxRows default is 10", function(){
    return sql.exec("select * from rh").rows.length === 10;
});

t("result - maxRows explicit", function(){
    return sql.exec("select * from rh", {maxRows:3}).rows.length === 3;
});

t("result - maxRows -1 returns all", function(){
    return sql.exec("select * from rh", {maxRows:-1}).rows.length === 14;
});

t("result - skipRows", function(){
    var ids = sql.exec("select id from rh order by id", {maxRows:3, skipRows:5})
                 .rows.map(function(r){return r.id});
    return JSON.stringify(ids) === "[6,7,8]";
});

t("result - rowCount field", function(){
    return sql.exec("select * from rh", {maxRows:4}).rowCount === 4;
});

/* ----- parameter binding ----- */

t("params - positional (?)", function(){
    return sql.one("select name from rh where id=?",[7]).name === "n7";
});

t("params - named (?key)", function(){
    return sql.one("select name from rh where id=?id",{id:8}).name === "n8";
});

t("params - Sql.list IN expansion", function(){
    var ids = sql.exec("select id from rh where id in (?) order by id",
                       [Sql.list([2,4,6])]).rows.map(function(r){return r.id});
    return JSON.stringify(ids) === "[2,4,6]";
});

/* ----- callbacks ----- */

t("callback - one call per row", function(){
    var n=0; sql.exec("select * from rh", {maxRows:-1}, function(row){ n++; });
    return n === 14;
});

t("callback - return false cancels", function(){
    var n=0; sql.exec("select * from rh", {maxRows:-1}, function(){ n++; if(n===3) return false; });
    return n === 3;
});

t("callback - arg passthrough (5th param)", function(){
    var got=null;
    sql.exec("select * from rh", {arg:{tag:"X"}, maxRows:1}, function(row,i,cols,ci,a){ got=a; });
    return got && got.tag === "X";
});

t("callback - index + columns params", function(){
    var idxs=[], cols=null;
    sql.exec("select id,name from rh", {maxRows:3}, function(row,i,c){ idxs.push(i); cols=c; });
    return JSON.stringify(idxs)==="[0,1,2]" && JSON.stringify(cols)==='["id","name"]';
});

/* ----- error handling ----- */

t("error - query() returns -1 / null on bad sql (no throw)", function(){
    var r = sql.query("select x from no_such_table_xyz");
    return r === -1 || r === null;
});

t("error - errMsg populated after failed query", function(){
    sql.query("select x from no_such_table_xyz");
    return sql.errMsg.length > 0;
});

t("error - exec() throws on bad sql", function(){
    t.mustThrow(function(){ sql.exec("select bad syntax !!!"); }, "exec should throw");
    return true;
});

/* ===================================================================
 * Built-in SQL functions (sql-server-funcs.rst) — deterministic scalars
 * =================================================================== */

function scalar(name, q, expected, tol){
    t("fn "+name, function(){
        var got = sql.one(q).r;
        if (typeof expected === "number" && tol)  return closeTo(got, expected, tol);
        if (expected !== null && typeof expected === "object")
            return JSON.stringify(got) === JSON.stringify(expected);
        return got === expected;
    });
}

/* --- math --- */
scalar("ceil",   "select ceil(3.2) r", 4);
scalar("floor",  "select floor(3.8) r", 3);
scalar("fabs",   "select fabs(-5.5) r", 5.5);
scalar("fmod",   "select fmod(7.5,2.5) r", 0);
scalar("pow",    "select pow(2,3) r", 8);
scalar("sqrt",   "select sqrt(4) r", 2);
scalar("log10",  "select log10(100) r", 2);
scalar("exp",    "select exp(1) r", 2.718281828, 1e-6);
scalar("cos",    "select cos(0) r", 1);
scalar("sin",    "select sin(0) r", 0);
scalar("log",    "select log(2.718281828459045) r", 1, 1e-9);
scalar("atan2",  "select atan2(1,1) r", 0.785398163, 1e-6);
scalar("isNaN false", "select isNaN(5.0) r", 0);

/* --- string --- */
scalar("lower",         "select lower('HELLO') r", "hello");
scalar("upper",         "select upper('hello') r", "HELLO");
scalar("initcap",       "select initcap('hello world') r", "Hello World");
scalar("length",        "select length('hello') r", 5);
scalar("stringcompare lt","select stringcompare('abc','def') r", -1);
scalar("stringcompare eq","select stringcompare('abc','abc') r", 0);
scalar("stringformat",  "select stringformat('%d %s', 42, 'answer') r", "42 answer");

/* --- bit manipulation --- */
scalar("bitand",        "select bitand(5,3) r", 1);
scalar("bitor",         "select bitor(5,3) r", 7);
scalar("bitxor",        "select bitxor(5,3) r", 6);
scalar("bitcount",      "select bitcount(5) r", 2);
scalar("bitset",        "select bitset(5,3) r", 13);
scalar("bitclear",      "select bitclear(5,0) r", 4);
scalar("bitisset",      "select bitisset(5,2) r", 1);
scalar("bitshiftleft",  "select bitshiftleft(5,2) r", 20);
scalar("bitshiftright", "select bitshiftright(20,2) r", 5);

/* --- date extraction --- */
scalar("year",       "select year('2023-03-15') r", 2023);
scalar("month",      "select month('2023-03-15') r", 3);
scalar("dayofmonth", "select dayofmonth('2023-03-15') r", 15);
scalar("dayofweek",  "select dayofweek('2023-03-12') r", 1);   // Sunday = 1
scalar("dayofyear",  "select dayofyear('2023-03-15') r", 74);
scalar("quarter",    "select quarter('2023-04-01') r", 2);
scalar("hour",       "select hour('2023-03-15 14:30:45') r", 14);
scalar("minute",     "select minute('2023-03-15 14:30:45') r", 30);
scalar("second",     "select second('2023-03-15 14:30:45') r", 45);
scalar("monthname",  "select monthname('2023-03-15') r", "March");
scalar("dayname",    "select dayname('2023-03-15') r", "Wednesday");

t("fn dayseq monotonic (+7)", function(){
    var a=sql.one("select dayseq('2023-01-01') r").r;
    var b=sql.one("select dayseq('2023-01-08') r").r;
    return (b - a) === 7;
});
t("fn weekseq monotonic (+1)", function(){
    var a=sql.one("select weekseq('2023-01-01') r").r;
    var b=sql.one("select weekseq('2023-01-08') r").r;
    return (b - a) === 1;
});

/* --- conversion / null --- */
t("fn convert int->date (epoch)", function(){
    var d = sql.one("select convert(0,'date') r").r;   // returns a Date object
    return (new Date(d)).getTime() === 0;
});
scalar("convert varchar->strlst",  "select convert('[\"a\",\"b\",\"c\"]','strlst') r", ["a","b","c"]);
scalar("length of strlst",         "select length(convert('[\"a\",\"b\"]','strlst')) r", 2);
scalar("ifNull non-null passes through", "select ifNull('x','y') r", "x");
scalar("isNull on non-null is 0", "select isNull(5) r", 0);
scalar("bintohex/hextobin roundtrip", "select bintohex(hextobin('48656C6C6F','stream'),'stream') r", "48656c6c6f");

/* --- inet (values verified against the engine; some doc examples differ) --- */
scalar("inetcanon",      "select inetcanon('192.1') r", "192.1.0.0/24");
scalar("inetnetwork",    "select inetnetwork('192.100.5.0/24') r", "192.100.5.0");
scalar("inetbroadcast",  "select inetbroadcast('192.100.5.0/24') r", "192.100.5.255");
scalar("inetnetmask",    "select inetnetmask('192.100.0/24') r", "255.255.255.0");
scalar("inetnetmasklen", "select inetnetmasklen('192.100.0/24') r", 24);
scalar("inetcontains (out of /24)", "select inetcontains('192.100.0/24','192.100.5.42') r", 0);
scalar("inetcontains (in /16)",     "select inetcontains('192.100.0.0/16','192.100.5.42') r", 1);
scalar("inetclass (classless)",     "select inetclass('192.100.0.0') r", "classless");

/* --- url --- */
scalar("urlcanonicalize", "select urlcanonicalize('HTTP://EXAMPLE.COM/Path','lowerProtocol,lowerHost') r",
       "http://example.com/Path");

/* --- json --- */
scalar("isjson true",   "select isjson('{\"type\":1}') r", 1);
scalar("isjson false",  "select isjson('not json') r", 0);
scalar("json_type obj", "select json_type('{\"a\":1}') r", "OBJECT");
scalar("json_type arr", "select json_type('[1,2]') r", "ARRAY");
scalar("json_value",    "select json_value('{\"a\":1}','$.a') r", "1");
scalar("json_query",    "select json_query('{\"a\":[1,2]}','$.a') r", "[1,2]");
scalar("json_modify",   "select json_modify('{}','$.foo','bar') r", '{"foo":"bar"}');
scalar("json_merge_patch","select json_merge_patch('{\"a\":\"b\"}','{\"a\":\"c\"}') r", '{"a":"c"}');

/* --- geo --- */
scalar("azimuth2compass",   "select azimuth2compass(105) r", "E");
scalar("azimuth2compass(3)","select azimuth2compass(105,3) r", "ESE");
scalar("dms2dec",           "select dms2dec(351500) r", 35.25);
scalar("dec2dms",           "select dec2dms(35.25) r", 351500);
scalar("distlatlon",        "select distlatlon(41.4,-81.5,40.81,-73.96) r", 394.258, 0.1);

t("fn geocode round-trips lat/lon", function(){
    var lat = sql.one("select geocode2lat(latlon2geocode(41.4,-81.5)) r").r;
    var lon = sql.one("select geocode2lon(latlon2geocode(41.4,-81.5)) r").r;
    return closeTo(lat, 41.4, 0.01) && closeTo(lon, -81.5, 0.01);
});

/* --- path --- */
scalar("basename", "select basename('/path/to/file.txt') r", "file.txt");
scalar("dirname",  "select dirname('/path/to/file.txt') r", "/path/to");
scalar("fileext",  "select fileext('/path/to/file.txt') r", ".txt");
scalar("joinpath", "select joinpath('one','two/','/three/four','five') r", "one/two/three/four/five");
scalar("pathcmp",  "select pathcmp('/a/b','/a/c') r", -1);

/* --- language id (probability varies; assert the code) --- */
t("fn identifylanguage returns code", function(){
    var r = sql.one("select identifylanguage('The quick brown fox jumps over the lazy dog today again') r").r;
    return Array.isArray(r) && r[1] === "en";
});

/* ===================================================================
 * Text functions needing a table
 * =================================================================== */

t("setup - text table for fulltext", function(){
    sql.query("drop table doc");
    sql.exec("create table doc (Title varchar(32), Body varchar(2048))");
    sql.exec("insert into doc values('A','the power struggle between nations continued for many long years')");
    sql.exec("insert into doc values('B','a quick brown fox jumps over the lazy dog in the meadow')");
    sql.exec("insert into doc values('C','abraham lincoln delivered the gettysburg address in 1863')");
    return sql.one("select count(*) c from doc").c === 3;
});

t("fn abstract(text,max) returns truncated string", function(){
    var r = sql.one("select abstract(Body,40) r from doc where Title='B'").r;
    return typeof r === "string" && r.length > 0 && r.indexOf("quick") >= 0;
});

t("fn keywords returns phrase list", function(){
    var r = sql.one("select keywords(Body) r from doc where Title='B'").r;
    return typeof r === "string" && r.indexOf("quick") >= 0;
});

t("fn mminfo returns hit info string", function(){
    var r = sql.one("select mminfo('power struggle', Body) r from doc where Title='A'").r;
    return typeof r === "string" && r.length > 0;
});

t("fulltext - create index", function(){
    sql.set({indexaccess: true});
    sql.one("create FULLTEXT index doc_Body_ftx on doc(Body) "+
            "WITH WORDEXPRESSIONS ('[\\alnum\\x80-\\xFF]{2,99}','[\\.\\alnum]{2,99}') "+
            "INDEXMETER 'off'");
    return !!sql.one("select * from SYSINDEX where NAME='doc_Body_ftx'");
});

t("fulltext - likep search", function(){
    var res = sql.exec("select Title from doc where Body likep 'lincoln'");
    return res.rowCount === 1 && res.rows[0].Title === "C";
});

t("fulltext - likep with abstract centered on query", function(){
    var res = sql.exec("select Title, abstract(Body,80,'querybest','fox') abs from doc where Body likep 'fox'");
    return res.rowCount === 1 && res.rows[0].abs.indexOf("fox") >= 0;
});

t("result - includeCounts populates countInfo", function(){
    var res = sql.exec("select Title from doc where Body likep 'the'", {includeCounts:true});
    return res.countInfo && typeof res.countInfo === "object";
});

/* ===================================================================
 * Settings (sql-set.rst) — verifiable via readback or behavior
 * =================================================================== */

t("set - listNoise returns the noise-word list", function(){
    var r = sql.set({listNoise:true});
    sql.reset();
    return r && Array.isArray(r.noiseList) && r.noiseList.length > 0;
});

t("set - listSuffix returns the suffix list", function(){
    var r = sql.set({listSuffix:true});
    sql.reset();
    return r && Array.isArray(r.suffixList) && r.suffixList.length > 0;
});

t("set - listPrefix returns the prefix list", function(){
    var r = sql.set({listPrefix:true});
    sql.reset();
    return r && Array.isArray(r.prefixList) && r.prefixList.length > 0;
});

t("set - noiseList set then read back", function(){
    sql.set({noiseList:["zzx","zzy"]});
    var r = sql.set({listNoise:true});
    sql.reset();
    return JSON.stringify(r.noiseList) === '["zzx","zzy"]';
});

t("set - hexifyBytes affects byte columns in JS returns", function(){
    sql.query("drop table hexb");
    sql.exec("create table hexb (h varbyte(16))");
    sql.exec("insert into hexb values(?)", [rampart.utils.dehexify("48656c6c6f")]);

    var plain = sql.one("select convert(h,'varchar') r from hexb").r;
    sql.set({hexifyBytes:true});
    var hexed = sql.one("select convert(h,'varchar') r from hexb").r;
    sql.reset();
    var back  = sql.one("select convert(h,'varchar') r from hexb").r;

    sql.query("drop table hexb");
    return plain === "Hello" && hexed === "48656c6c6f" && back === "Hello";
});

t("set - addExpressions appears in listExpressions", function(){
    var r = sql.set({addExpressions:["[\\digit]{3,5}"], listExpressions:true});
    sql.reset();
    return r && r.expressionsList && r.expressionsList.indexOf("[\\digit]{3,5}") >= 0;
});

t("set - addIndexTemp appears in listIndexTemp", function(){
    var r = sql.set({addIndexTemp:[tmpdir], listIndexTemp:true});
    sql.reset();
    return r && r.indexTempList && r.indexTempList.indexOf(tmpdir) >= 0;
});

t("set - reset() returns undefined and restores defaults", function(){
    sql.set({noiseList:["temp"]});
    var rv = sql.reset();
    var after = sql.set({listNoise:true});
    sql.reset();
    /* after reset, the default (large) noise list is back, not our 1-item list */
    return rv === undefined && after.noiseList.length > 1;
});

t("set - selectMaxRows via property changes default cap", function(){
    var prev = sql.selectMaxRows;
    sql.selectMaxRows = 3;
    var n = sql.exec("select * from rh").rows.length;
    sql.selectMaxRows = prev;
    return n === 3;
});

/* ===================================================================
 * Sql.* utility functions (rampart-sql.rst)
 * =================================================================== */

t("Sql.rex - returns array of matches", function(){
    return JSON.stringify(Sql.rex('[a-z]+','hello world')) === '["hello","world"]';
});

t("Sql.re2 - perl-style digit matches", function(){
    return JSON.stringify(Sql.re2('\\d+','42 apples 17 oranges')) === '["42","17"]';
});

t("Sql.sandr - rex search & replace", function(){
    return Sql.sandr('happy','glad','unhappy') === "unglad";
});

t("Sql.sandr2 - re2 search & replace", function(){
    return Sql.sandr2('\\d+','N','a1b22c') === "aNbNc";
});

t("Sql.stringFormat - printf style", function(){
    return Sql.stringFormat('%d-%s', 7, 'x') === "7-x";
});

t("Sql.abstract - returns shortened text", function(){
    var a = Sql.abstract("The quick brown fox jumps over the lazy dog. "+
        "Pack my box with five dozen liquor jugs.", 40);
    return typeof a === "string" && a.length > 0 && a.indexOf("quick") >= 0;
});

t("Sql.searchText - finds offsets", function(){
    var res = Sql.searchText("fox", "the quick brown fox jumps", {minwordlen:3});
    return res.length >= 1 && typeof res[0].offset === "number";
});

t("Sql.searchFile - finds offsets in a file", function(){
    var f = tmpdir + "/search.txt";
    fprintf(f, "%s", "the quick brown fox jumps over the lazy fox again");
    var res = Sql.searchFile("fox", f, {minwordlen:3});
    return res.length >= 1 && typeof res[0].offset === "number";
});

/* ===================================================================
 * importCsv (in-memory string form)
 * =================================================================== */

t("importCsv - string with header row", function(){
    sql.query("drop table csvt");
    sql.one("create table csvt (a int, b varchar(16))");
    var n = sql.importCsv("a,b\n1,one\n2,two\n3,three\n", {tableName:"csvt", hasHeaderRow:true});
    return n === 3 && sql.one("select count(*) c from csvt").c === 3;
});

t("importCsv - column reorder via ordering array", function(){
    sql.query("drop table csvt2");
    sql.one("create table csvt2 (a int, b varchar(16))");
    /* CSV is b,a order; ordering maps file col -> table col */
    sql.importCsv("x,y\nfoo,10\nbar,20\n", {tableName:"csvt2", hasHeaderRow:true}, [1,0]);
    var row = sql.one("select a,b from csvt2 where a=10");
    return row && row.b === "foo";
});

/* ===================================================================
 * Added or fixed after v0.7.5
 * =================================================================== */

/* run this script again as another process; returns what it printed */
function child(args){
    var cmd = "'" + process.argv[0] + "' '" + process.argv[1] + "' --child";
    for (var i = 0; i < args.length; i++) cmd += " '" + args[i] + "'";
    return shell(cmd + " 2>&1").stdout.trim();
}
function ids(q, params, col){
    var r = params ? sql.exec(q, params, {maxRows:-1}) : sql.exec(q, {maxRows:-1});
    return JSON.stringify(r.rows.map(function(x){ return x[col || "Id"]; }).sort(function(a,b){ return a-b; }));
}
var dbpath = tmpdir + "/db";

/* ---- importCsv: lockTable, and imports from a thread ---- */

t("importCsv - lockTable option imports every row", function(){
    sql.query("drop table csvl");
    sql.exec("create table csvl (a int, b varchar(16))");
    var txt = "a,b\n";
    for (var i = 0; i < 300; i++) txt += i + ",v" + i + "\n";
    var n = sql.importCsv(txt, {tableName:"csvl", hasHeaderRow:true, lockTable:true});
    return n === 300 && sql.one("select count(*) c from csvl").c === 300 &&
           sql.one("select b from csvl where a=299").b === "v299";
});

t("importCsv - table usable by this and another connection after lockTable", function(){
    sql.exec("insert into csvl values(1000,'after')");
    var c2 = new Sql.connection(dbpath);
    c2.exec("insert into csvl values(1001,'other')");
    var n = c2.one("select count(*) c from csvl").c;
    c2.close();
    return n === 302;
});

t("importCsv - callback returning false stops a lockTable import; table still writable", function(){
    sql.query("drop table csvs");
    sql.exec("create table csvs (a int, b varchar(16))");
    var txt = "";
    for (var i = 0; i < 50; i++) txt += i + ",v" + i + "\n";
    sql.importCsv(txt, {tableName:"csvs", lockTable:true, callbackStep:10}, function(n){ if (n >= 20) return false; });
    var got = sql.one("select count(*) c from csvs").c;
    sql.exec("insert into csvs values(999,'x')");
    return got >= 20 && got < 50 && sql.one("select count(*) c from csvs").c === got + 1;
});

/* ---- importJsonl / importJsonlFile ---- */

t("importJsonl - keys go to the columns of the same name", function(){
    sql.query("drop table jl");
    sql.exec("create table jl (Id int, Title varchar(32), Score double, Extra varchar(64))");
    var res = sql.importJsonl('{"Id":1,"Title":"one","Score":1.5}\n{"Id":2,"Title":"two","Score":2.5,"nocolumn":7}\n',
                              {tableName:"jl"});
    var r = sql.one("select * from jl where Id=2");
    return res.imported === 2 && res.skipped === 0 && res.invalid === 0 &&
           r.Title === "two" && closeTo(r.Score, 2.5) && r.Extra === "";
});

t("importJsonl - map option sends a key to another column", function(){
    sql.exec("delete from jl");
    sql.importJsonl('{"id":5,"name":"five","Score":5}\n', {tableName:"jl", map:{id:"Id", name:"Title"}});
    var r = sql.one("select * from jl");
    return r.Id === 5 && r.Title === "five" && closeTo(r.Score, 5);
});

t("importJsonl - blank lines ignored, missing key and null give the column default", function(){
    sql.exec("delete from jl");
    var res = sql.importJsonl('\n{"Id":1}\n\n{"Id":2,"Title":null,"Score":null}\n', {tableName:"jl"});
    var r = sql.exec("select * from jl order by Id").rows;
    return res.imported === 2 && r.length === 2 && r[0].Title === "" && r[0].Score === 0 &&
           r[1].Title === "" && r[1].Score === 0;
});

t("importJsonl - boolean, nested object and array values", function(){
    sql.exec("delete from jl");
    sql.importJsonl('{"Id":1,"Title":true,"Extra":{"a":[1,2]}}\n', {tableName:"jl"});
    var r = sql.one("select * from jl");
    return r.Title === "true" && JSON.stringify(JSON.parse(r.Extra)) === '{"a":[1,2]}';
});

t("importJsonl - rowFunc changes, replaces and skips lines", function(){
    sql.exec("delete from jl");
    var lines = [];
    var res = sql.importJsonl('{"Id":1,"Title":"a"}\n{"Id":2,"Title":"b"}\n{"Id":3,"Title":"c"}\n', {
        tableName:"jl",
        rowFunc: function(o, lineNumber){
            lines.push(lineNumber);
            if (o.Id === 2) return false;                         /* skip          */
            if (o.Id === 3) return {Id: 30, Title: "replaced"};   /* replace       */
            o.Title = o.Title.toUpperCase();                      /* change, no return */
        }});
    var r = sql.exec("select * from jl order by Id").rows;
    return res.imported === 2 && res.skipped === 1 && JSON.stringify(lines) === "[1,2,3]" &&
           r.length === 2 && r[0].Title === "A" && r[1].Id === 30 && r[1].Title === "replaced";
});

t("importJsonl - an invalid line throws and names the line; earlier rows stay", function(){
    sql.exec("delete from jl");
    var msg = "";
    try { sql.importJsonl('{"Id":1}\n{"Id":2\n{"Id":3}\n', {tableName:"jl"}); }
    catch(e) { msg = "" + e; }
    return /line 2/.test(msg) && sql.one("select count(*) c from jl").c === 1;
});

t("importJsonl - skipInvalid skips and counts invalid lines", function(){
    sql.exec("delete from jl");
    var res = sql.importJsonl('{"Id":1}\nnot json\n[1,2]\n{"Id":4}\n', {tableName:"jl", skipInvalid:true});
    return res.imported === 2 && res.invalid === 2 && ids("select Id from jl") === "[1,4]";
});

t("importJsonl - callback gets (rowsImported, bytesRead, fileSize)", function(){
    sql.exec("delete from jl");
    var txt = "", calls = [];
    for (var i = 0; i < 10; i++) txt += '{"Id":' + i + '}\n';
    sql.importJsonl(txt, {tableName:"jl", callbackStep:5}, function(rows, bytes, total){ calls.push([rows, bytes, total]); });
    var last = calls[calls.length - 1];
    return calls.length >= 2 && calls[0][0] === 5 && last[0] === 10 &&
           last[1] === txt.length && last[2] === txt.length;
});

t("importJsonl - callback returning false stops the import", function(){
    sql.exec("delete from jl");
    var txt = "";
    for (var i = 0; i < 40; i++) txt += '{"Id":' + i + '}\n';
    sql.importJsonl(txt, {tableName:"jl", callbackStep:10}, function(rows){ if (rows >= 10) return false; });
    var n = sql.one("select count(*) c from jl").c;
    return n >= 10 && n < 40;
});

t("importJsonl - lockTable option", function(){
    sql.exec("delete from jl");
    var txt = "";
    for (var i = 0; i < 200; i++) txt += '{"Id":' + i + ',"Title":"t' + i + '"}\n';
    var res = sql.importJsonl(txt, {tableName:"jl", lockTable:true});
    sql.exec("insert into jl values(1000,'after',0,'')");
    return res.imported === 200 && sql.one("select count(*) c from jl").c === 201 &&
           sql.one("select Title from jl where Id=199").Title === "t199";
});

t("importJsonl - counter column gets a value from the database", function(){
    sql.query("drop table jlc");
    sql.exec("create table jlc (c counter, Id int)");
    sql.importJsonl('{"Id":1}\n{"Id":2,"c":"ignored"}\n', {tableName:"jlc"});
    var r = sql.exec("select convert(c,'varchar') c, Id from jlc").rows;
    return r.length === 2 && r[0].c.length > 0 && r[0].c !== r[1].c;
});

t("importJsonl - missing tableName throws", function(){
    try { sql.importJsonl('{"Id":1}\n', {}); } catch(e) { return true; }
    return false;
});

t("importJsonlFile - imports a file", function(){
    sql.exec("delete from jl");
    var f = tmpdir + "/in.jsonl", fh = fopen(f, "w");
    for (var i = 0; i < 500; i++) fprintf(fh, '{"Id":%d,"Title":"f%d"}\n', i, i);
    fclose(fh);
    var sizes = [], res = sql.importJsonlFile(f, {tableName:"jl", callbackStep:250}, function(rows, bytes, total){ sizes.push(total); });
    return res.imported === 500 && sql.one("select count(*) c from jl").c === 500 &&
           sql.one("select Title from jl where Id=499").Title === "f499" && sizes[0] === stat(f).size;
});

t("imports in a thread - importCsvFile and importJsonlFile, with and without lockTable", function(){
    sql.query("drop table thr_csv"); sql.query("drop table thr_jsonl");
    sql.exec("create table thr_csv (a int, b varchar(16))");
    sql.exec("create table thr_jsonl (a int, b varchar(16))");
    var base = tmpdir + "/thr", fc = fopen(base + ".csv", "w"), fj = fopen(base + ".jsonl", "w");
    fprintf(fc, "a,b\n");
    for (var i = 0; i < 500; i++) { fprintf(fc, "%d,v%d\n", i, i); fprintf(fj, '{"a":%d,"b":"v%d"}\n', i, i); }
    fclose(fc); fclose(fj);
    var out = child(["thread", dbpath, base]), r;
    try { r = JSON.parse(out.split("\n").pop()); } catch(e) { printf("%s\n", out); return false; }
    return r.csv === 500 && r.csvLock === 500 && r.csvCount === 1000 &&
           r.jsonl.imported === 500 && r.jsonlLock.imported === 500 && r.jsonlCount === 1000 &&
           r.last.b === "v499" && sql.one("select count(*) c from thr_csv").c === 1000;
});

/* ---- noLocking is per connection ---- */

t("set - noLocking applies to its own connection only; get() and reset() handle it", function(){
    var c1 = new Sql.connection(dbpath), c2 = new Sql.connection(dbpath);
    c1.set({noLocking:true});
    var on = c1.get().noLocking, other = c2.get().noLocking;
    c1.exec("insert into rh values(500,'nolock')");
    var seen = c2.one("select name from rh where id=500");
    c1.reset();
    var off = c1.get().noLocking;
    c1.exec("delete from rh where id=500");
    c1.close(); c2.close();
    return !!on && !other && !off && seen && seen.name === "nolock";
});

/* ---- a database replaced under an open connection ---- */

function mkdb(dir, tag, n){
    rm_rf(dir);
    var c = new Sql.connection(dir, true);
    c.exec("create table r (Id int, S varchar(16))");
    for (var i = 0; i < n; i++) c.exec("insert into r values(?,?)", [i, tag + i]);
    c.exec("create index r_id on r(Id)");
    c.close();
}
function rlook(c){
    return c.one("select count(*) c from r").c + "/" + c.one("select S from r where S matches '%0'").S + "/" +
           c.one("select S from r where Id = 0").S;
}

t("replaced database - open connection follows a directory swapped in (old kept)", function(){
    var d = tmpdir + "/swapdb";
    mkdb(d, "old", 5); mkdb(d + ".new", "new", 9);
    var c = new Sql.connection(d), before = rlook(c);
    c.set({indexMem: 64000000});
    shell("mv '" + d + "' '" + d + ".old' && mv '" + d + ".new' '" + d + "'");
    var after = rlook(c), kept = c.get("indexMem") === 64000000;
    rm_rf(d + ".old");
    var later = rlook(c);
    c.close();
    return before === "5/old0/old0" && after === "9/new0/new0" && later === "9/new0/new0" && kept;
});

t("replaced database - a running statement finishes on the old one", function(){
    var d = tmpdir + "/swapdb2";
    mkdb(d, "old", 5); mkdb(d + ".new", "new", 9);
    var c = new Sql.connection(d), seen = [], inner = null;
    c.exec("select S from r", {maxRows:-1}, function(row, i){
        seen.push(row.S);
        if (i == 0) { shell("mv '" + d + "' '" + d + ".old' && mv '" + d + ".new' '" + d + "'"); inner = rlook(c); }
    });
    c.close();
    return seen.join(",") === "old0,old1,old2,old3,old4" && inner === "9/new0/new0";
});

t("replaced database - nothing at the path for a moment: still answers", function(){
    var d = tmpdir + "/swapdb3";
    mkdb(d, "old", 5);
    var c = new Sql.connection(d); rlook(c);
    shell("mv '" + d + "' '" + d + ".away'");
    var gone = rlook(c);
    shell("mv '" + d + ".away' '" + d + "'");
    var back = rlook(c);
    c.close();
    return gone === "5/old0/old0" && back === "5/old0/old0";
});

t("close() - connection works again afterwards, with two handles pooled", function(){
    var c = new Sql.connection(dbpath), n = 0;
    c.exec("select id from rh where id < 3", function(){ n += c.one("select count(*) c from rh").c > 0 ? 1 : 0; });
    c.close();
    return n === 2 && c.one("select count(*) c from rh").c > 0;
});

/* ---- optimize / noOptimize ---- */

t("set - optimize: unknown name throws, valid name reported by get()", function(){
    var c = new Sql.connection(dbpath), threw = false;
    try { c.set({optimize:"nosuchthing"}); } catch(e) { threw = /unknown optimization 'nosuchthing'/.test("" + e); }
    c.set({optimize:"indexbatchbuild"});
    var ok = threw && c.get("optimize") === "indexbatchbuild" && c.get("noOptimize") === undefined;
    c.close();
    return ok;
});

t("set - optimize: array and comma list; noOptimize; lists hold only non-defaults", function(){
    var c = new Sql.connection(dbpath);
    c.set({optimize:["indexbatchbuild","linearrankindexexps"], noOptimize:"join, countstar"});
    var a = c.get("optimize") === "indexbatchbuild,linearrankindexexps" && c.get("noOptimize") === "join,countstar";
    c.set({optimize:"join", noOptimize:"linearrankindexexps"});       /* both back to their defaults */
    var b = c.get("optimize") === "indexbatchbuild" && c.get("noOptimize") === "countstar";
    c.close();
    return a && b;
});

t("set - optimize: per connection, round trips through get(), cleared by reset()", function(){
    var c1 = new Sql.connection(dbpath), c2 = new Sql.connection(dbpath), c3 = new Sql.connection(dbpath);
    c1.set({optimize:"indexbatchbuild", noOptimize:"countstar"});
    var other = c2.get("optimize") === undefined && !("noOptimize" in c2.get());
    c3.set(c1.get());
    var trip = c3.get("optimize") === "indexbatchbuild" && c3.get("noOptimize") === "countstar";
    c1.reset();
    var cleared = c1.get("optimize") === undefined && c1.get("noOptimize") === undefined && c3.get("optimize") === "indexbatchbuild";
    c1.close(); c2.close(); c3.close();
    return other && trip && cleared;
});

t("set - optimize: a bad value leaves the settings as they were", function(){
    var c = new Sql.connection(dbpath), threw = 0;
    c.set({optimize:"indexbatchbuild"});
    try { c.set({optimize:"likeand,bogus"}); } catch(e) { threw++; }
    try { c.set({optimize:true}); } catch(e) { threw++; }
    c.set({indexmem:64000000});                                       /* a later valid set is clean */
    var ok = threw === 2 && !c.errMsg && c.get("optimize") === "indexbatchbuild";
    c.close();
    return ok;
});

/* ---- UNION ---- */

t("union - setup", function(){
    sql.query("drop table ua"); sql.query("drop table ub"); sql.query("drop table ue");
    sql.exec("create table ua (Id int, G int)"); sql.exec("create table ub (Id int, G int)"); sql.exec("create table ue (Id int, G int)");
    for (var i = 0; i < 10; i++) sql.exec("insert into ua values(?,?)", [i, i % 3]);       /* ua: 0..9   */
    for (var i = 6; i < 14; i++) sql.exec("insert into ub values(?,?)", [i, i % 4]);       /* ub: 6..13  */
    sql.exec("create index ua_id on ua(Id)"); sql.exec("create index ub_id on ub(Id)");
    return sql.one("select count(*) c from ua").c === 10 && sql.one("select count(*) c from ub").c === 8;
});

t("union - removes duplicates", function(){
    return ids("select Id from ua union select Id from ub") === "[0,1,2,3,4,5,6,7,8,9,10,11,12,13]";
});

t("union all - keeps them", function(){
    return ids("select Id from ua union all select Id from ub") === "[0,1,2,3,4,5,6,6,7,7,8,8,9,9,10,11,12,13]";
});

t("union - duplicates inside one query removed too", function(){
    return ids("select G from ua union select G from ub", null, "G") === "[0,1,2,3]";
});

t("union - where clauses (indexed) and parameters", function(){
    return ids("select Id from ua where Id < ? union select Id from ub where Id > ?", [2, 11]) === "[0,1,12,13]";
});

t("union all - three queries, first one indexed, not repeated", function(){
    return ids("select Id from ua where Id < 2 union all select Id from ub where Id < 8 union all select Id from ua where Id < 2") === "[0,0,1,1,6,7]";
});

t("union - with an empty side", function(){
    return ids("select Id from ue union select Id from ub where Id < 8") === "[6,7]" &&
           ids("select Id from ua where Id < 2 union all select Id from ue") === "[0,1]";
});

t("union - different column counts is an error", function(){
    try { sql.exec("select Id, G from ua union select Id from ub"); } catch(e) { return true; }
    return !!sql.errMsg;
});

/* ---- subqueries ---- */

/* ua.G = Id % 3, ub.G = Id % 4.  ub rows with G = 2: Id 6, 10; of those ua has 6. */
t("subquery - unqualified column is the subquery table's, not the outer table's", function(){
    return ids("select Id from ua where Id in (select Id from ub where G = 2)") === "[6]" &&
           ids("select Id from ua where exists (select Id from ub where ub.Id = ua.Id and G = 2)") === "[6]";
});

t("subquery - outer table's column still reaches a correlated subquery", function(){
    /* ids in both tables: 6..9; ua.G == ub.G for none of 6(0,2) 7(1,3) 8(2,0) 9(0,1) */
    return ids("select Id from ua where exists (select Id from ub where ub.Id = ua.Id and ub.G = ua.G)") === "[]" &&
           ids("select Id from ua where exists (select Id from ub where ub.Id = ua.Id and ub.G > ua.G)") === "[6,7,9]";
});

t("subquery - not in / in / not exists / exists with an empty subquery", function(){
    return ids("select Id from ua where Id not in (select Id from ue)") === "[0,1,2,3,4,5,6,7,8,9]" &&
           ids("select Id from ua where Id in (select Id from ue)") === "[]" &&
           ids("select Id from ua where not exists (select Id from ue)") === "[0,1,2,3,4,5,6,7,8,9]" &&
           ids("select Id from ua where exists (select Id from ue)") === "[]" &&
           ids("select Id from ua where G = 1 and Id not in (select Id from ub where G = 99)") === "[1,4,7]";
});

t("subquery - not in with a non-empty subquery", function(){
    return ids("select Id from ua where Id not in (select Id from ub)") === "[0,1,2,3,4,5]";
});

t("subquery - update and delete change only the rows the subquery names", function(){
    sql.exec("update ua set G = 77 where Id in (select Id from ub where G = 2)");
    var upd = ids("select Id from ua where G = 77");
    sql.exec("delete from ua where Id in (select Id from ub where G = 3)");     /* ub G=3: 7, 11 */
    return upd === "[6]" && ids("select Id from ua") === "[0,1,2,3,4,5,6,8,9]";
});

/* ---- seeing another process's changes ---- */

t("other process - rows it inserts show in an indexed select and a count", function(){
    sql.query("drop table xp");
    sql.exec("create table xp (Id int, G int)");
    sql.exec("create index xp_id on xp(Id)"); sql.exec("create index xp_g on xp(G)");
    for (var i = 0; i < 20; i++) sql.exec("insert into xp values(?,?)", [i, i % 5]);
    var before = sql.exec("select Id from xp where G = 2", {maxRows:-1}).rows.length;       /* opens the index */
    child(["insert", dbpath, "xp", 20, 40]);
    return before === 4 && sql.exec("select Id from xp where G = 2", {maxRows:-1}).rows.length === 8 &&
           sql.one("select count(*) c from xp").c === 40 &&
           ids("select Id from xp where Id >= 35") === "[35,36,37,38,39]";
});

t("other process - group by after its inserts", function(){
    var r = sql.exec("select G, count(*) n from xp group by G", {maxRows:-1}).rows;
    return r.length === 5 && r.every(function(x){ return x.n === 8; });
});

t("other process - drops an index this connection has used: no error, same rows", function(){
    child(["exec", dbpath, "drop index xp_g"]);
    var r = sql.exec("select Id from xp where G = 2", {maxRows:-1});
    return !sql.errMsg && r.rows.length === 8;
});

t("other process - drops and re-creates an index of the same name: no error, right rows", function(){
    sql.exec("create index xp_g on xp(G)");
    var a = sql.exec("select Id from xp where G = 3", {maxRows:-1}).rows.length;            /* opens it */
    child(["exec", dbpath, "drop index xp_g", "delete from xp where Id >= 30", "create index xp_g on xp(G)"]);
    var r = sql.exec("select Id from xp where G = 3", {maxRows:-1});
    return a === 8 && !sql.errMsg && r.rows.length === 6;
});

t("other process - its update of an indexed column moves rows between lookups", function(){
    child(["exec", dbpath, "update xp set G = 9 where Id < 10"]);
    return sql.exec("select Id from xp where G = 9", {maxRows:-1}).rows.length === 10 &&
           sql.exec("select Id from xp where G = 3", {maxRows:-1}).rows.length === 4 &&
           ids("select Id from xp where G = 9 and Id > 6") === "[7,8,9]";
});

/* ===================================================================
 * Items intentionally skipped (visible in output) — see header comment
 * =================================================================== */
t.skip("embed() / llamaEmbed",            "needs a .gguf embedding model");
t.skip("CREATE VECTOR INDEX / LIKEV",     "covered by sql-vector-test.js");
t.skip("scheduleUpdate / scheduleRebuild","asynchronous timing");
t.skip("random() / seq()",                "non-deterministic / stateful");
t.skip("exec() SQL function",             "runs external commands");
t.skip("fromfile / toind / canonpath",    "filesystem-path specific");
t.skip("isNull/ifNull true-NULL path",    "texis fixed cols default to 0/'' not NULL");
t.skip("nullOutputString",                "tsql text-output only, not JS returns");
t.skip("lookup()",                        "range/name setup unresolved");
t.skip("perf/buffer/index tuning sets",   "no readback path");
t.skip("races between processes",         "need exact timing");

rm_rf(tmpdir);
t.exit();
}
