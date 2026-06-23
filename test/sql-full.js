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
 *   - hexifyBytes, nullOutputString        : only affect tsql text output, not the
 *                                            JS object/array return values
 *   - lookup()                             : range/name setup did not resolve in probes
 *   - perf/buffer/index tuning settings    : no readback path (btreeCacheSize, ramRows,
 *     (indexMem, indexMmap, *BufSz, etc.)    indexMem, etc.) — settable but unobservable
 *   - addTable() (move a .tbl in)          : addTables option is covered by sql-test.js
 */

rampart.globalize(rampart.utils);
load.Sql;

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
 * Items intentionally skipped (visible in output) — see header comment
 * =================================================================== */
t.skip("embed() / llamaEmbed",            "needs a .gguf embedding model");
t.skip("CREATE VECTOR INDEX / LIKEV",     "covered by sql-vector-test.js");
t.skip("scheduleUpdate / scheduleRebuild","asynchronous timing");
t.skip("random() / seq()",                "non-deterministic / stateful");
t.skip("exec() SQL function",             "runs external commands");
t.skip("fromfile / toind / canonpath",    "filesystem-path specific");
t.skip("isNull/ifNull true-NULL path",    "texis fixed cols default to 0/'' not NULL");
t.skip("hexifyBytes / nullOutputString",  "tsql text-output only, not JS returns");
t.skip("lookup()",                        "range/name setup unresolved");
t.skip("perf/buffer/index tuning sets",   "no readback path");

rm_rf(tmpdir);
t.exit();
