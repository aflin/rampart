rampart.globalize(rampart.utils);

load.Sql;
load.crypto;

var tmpdir = process.scriptPath + '/tmp-test';

if (!stat(tmpdir)) mkdir(tmpdir);

var _hasShell = !!stat('/bin/bash');

function rm_rf_dir(path) {
    if (_hasShell) {
        shell("rm -rf " + path);
        return;
    }
    if (!stat(path)) return;
    var files = readdir(path);
    for (var i = 0; i < files.length; i++) {
        try { rmFile(path + "/" + files[i]); } catch(e) {}
    }
    try { rmdir(path); } catch(e) {}
}

var sql=Sql.connect(tmpdir+"/testdb",true);//create if doesn't exist

/* check for quicktest, make if necessary */
var res=sql.exec("select * from SYSTABLES where NAME='quicktest'");
if(res.rows.length==0) {
    res=sql.exec("create table quicktest ( I int, Text varchar(16) );");
    sql.exec("insert into quicktest values(2,'just a test');");
    sql.exec("create index quicktest_I_x on quicktest(I);");
}


var sha256=crypto.sha256;
var md5=crypto.md5;

var testFeature = new (require('./test-feature.js'))({prefix: "sql"});

/*
This one belongs elsewhere
testFeature("sha256 and hexify/dehexify", function(){
  var sha_res_buf=sha256("hello",true); //true = return buffer with binary
  var sha_res_upper=hexify(sha_res_buf,true); // true = upper case A-F
  var sha_res_lower=hexify(sha_res_buf);

  var sha_res1=dehexify(sha_res_lower);
  var sha_res2=dehexify(sha_res_upper);

  sha_res1=hexify(sha_res1);
  sha_res2=hexify(sha_res2);

  return sha_res2 = sha_res1;
});
*/


sql.query("drop table urls;");
sql.query("drop table urls2;");

testFeature ("create a sql table", function(){
  sql.exec("create table urls ( Md5 byte(16), Url varchar(16) );");
  var ret=sql.exec("select * from SYSTABLES where NAME='urls'");
  return ret.rows.length != 0;
});


var urls=["http://bing.com/","http://google.com/","http://yahoo.com/","http://wikipedia.org/", "http://wikipedia.org/"];

testFeature("insert of urls and binary md5 hash", function(){
  for (var i=0; i<urls.length; i++)
  {
      var u=urls[i];
      var buf=md5(u,true);//true means keep it binary in a buffer
      sql.exec("insert into urls values(?,?);",[buf,u]);
  }
  ret=sql.exec("select * from urls");
  return ret.rows.length == 5;
});    


testFeature("making unique index on non-unique data", function(){
  var ret=sql.query("create unique index urls_Md5_ux on urls(Md5);");
  return Sql.rex(/non-unique=/,sql.errMsg).length != 0;
});

testFeature("select md5 hash, hexify, nested sql", function() {
  var ret=true;
  var res1=sql.exec("create table urls2 ( Md5 byte(16), Url varchar(16) );");
  sql.exec("select * from urls",
      function(res){
          var md5sum=hexify(res.Md5);
          var md5comp=crypto.md5(res.Url);

          ret = ret && (md5sum==md5comp);
          sql.exec("insert into urls2 values(?,?)",[res.Md5,res.Url]);
      }
  );
  var res=sql.exec("select * from urls2");
  return ret && res.rows.length == 5;
});


testFeature("checking hashes in copied table", function(){
  var ret=true
  sql.exec("select * from urls2",
      function(res){
          var md5sum=hexify(res.Md5);
          var md5comp=crypto.md5(res.Url);
          
          ret = ret && (md5sum==md5comp);
      }
  );
  return ret;
});

testFeature("insert dup into table with unique index", function(){
  var res=sql.query("insert into urls values (?,?);",[md5(urls[0],true),urls[0]]);
  if ( Sql.rex(/duplicate=/,sql.errMsg).length )
    return true;

  return false;
});  
   
testFeature("built in sql bintohex()", function(){
  var res=sql.exec("select bintohex(Md5) Md5, Url from urls",{max:1});
  var md5comp=md5(res.rows[0].Url);
  return md5comp == res.rows[0].Md5;
});


sql.exec("drop table urls");
sql.exec("drop table urls2");




var getty = 
`THE GETTYSBURG ADDRESS:

Four score and seven years ago our fathers brought forth on this continent, a new nation, conceived in Liberty, and dedicated to the proposition that all men are created equal.

Now we are engaged in a great civil war, testing whether that nation, or any nation so conceived and so dedicated, can long endure. We are met on a great battle-field of that war. We have come to dedicate a portion of that field, as a final resting place for those who here gave their lives that that nation might live. It is altogether fitting and proper that we should do this.

But, in a larger sense, we can not dedicate -- we can not consecrate -- we can not hallow -- this ground. The brave men, living and dead, who struggled here, have consecrated it, far above our poor power to add or detract. The world will little note, nor long remember what we say here, but it can never forget what they did here. It is for us the living, rather, to be dedicated here to the unfinished work which they who fought here have thus far so nobly advanced. It is rather for us to be here dedicated to the great task remaining before us -- that from these honored dead we take increased devotion to that cause for which they gave the last full measure of devotion -- that we here highly resolve that these dead shall not have died in vain -- that this nation, under God, shall have a new birth of freedom -- and that government of the people, by the people, for the people, shall not perish from the earth.
`;

fprintf(tmpdir+"/gettysburg.txt", '%s', getty);


testFeature ("searchFile", function() {
    var res = Sql.searchFile(
       "live",
       tmpdir+"/gettysburg.txt",
       { minwordlen:3 }
    );
    return res.length==3 && res[0].offset;
});

testFeature ("searchText", function() {
    var res = Sql.searchText(
       "live",
       getty,
       { minwordlen:3 }
    );
    return res.length==3 && res[0].offset;
});


var sql1 = Sql.connect(tmpdir+"/wdb", true);

testFeature ("sql.importCsvFile", function() {
    var wiki = fopen(tmpdir+"/wiki.csv", "w+");

    fprintf(wiki, "Title, Text\n");

    Sql.rexFile('>><doc=!title*title\\="\\P=[^"]+[^>]+>=!</doc+', process.scriptPath+ "/wiki_00",
        function (match, sub, index) {
            var title = sub.submatches[3];
            var text = sub.submatches[6];
            text = Sql.sandr('"=', '\\\\"', text);

            fprintf(wiki, '%s,"%s"\n', title, text);
        }
    );

    fclose(wiki);


    if(sql1.one("select * from SYSTABLES where NAME='wtext'"))
        sql1.one("drop table wtext");

    sql1.one("create table wtext (Title varchar(16), Text varchar(4096))");

    var ret=sql1.importCsvFile(tmpdir+"/wiki.csv", {tableName:"wtext",hasHeaderRow: true});
    return ret==16;
});

function nestedcopy() {
    var sql2 = Sql.connect(tmpdir+"/wdb2", true);

    if(sql2.one("select * from SYSTABLES where NAME='wtext'"))
        sql2.one("drop table wtext");

    sql2.one("create table wtext (Title varchar(16), Text varchar(4096))");

    sql1.exec("select * from wtext", {maxRows:-1}, function(row,i) {
        sql2.exec("insert into wtext values(?Title, ?Text)", row);
    });

    
    var res = sql2.exec("select count(*) cnt from wtext",{maxRows: -1});

    return res.rows[0].cnt==16;
}

testFeature ("Multiple handles, nested select/insert copy", nestedcopy);

var thr = new rampart.thread();

thr.exec(nestedcopy, function(ret) {
    testFeature ("Multiple handles, nested, in thread", ret);
});

testFeature ("Create Text Index", function(){
    sql1.set({indexaccess: true});
    sql1.one(`create FULLTEXT index wtext_Text_ftx on wtext(Text) 
            WITH WORDEXPRESSIONS ('[\\alnum\\x80-\\xFF]{2,99}', '[\\.\\alnum]{2,99}')
            INDEXMETER 'off'`);
    var res = sql1.one(`select Word from wtext_Text_ftx where Word = '0.9'`);
    return !!res;
});

testFeature ("Full Text Search", function(){
    var res=sql1.exec("select Title, abstract(Text,230,'querybest','lincoln') abs from wtext where Text likep 'lincoln'");
    return res.rowCount==2;
});

testFeature ("Create with addTables", function(){
    rm_rf_dir(tmpdir + "/testdb2");
    mkdir(tmpdir+"/testdb2")
    copyFile(tmpdir+"/testdb/quicktest.tbl", tmpdir+"/testdb2/quicktest.tbl", true);

    var sql2=new Sql.connection({
        path:      tmpdir+"/testdb2",
        addTables: true
    });
    var res=sql2.exec("select * from quicktest");

    return res.rowCount;
});

testFeature ("Create with addTables in existing db", function(){
    rm_rf_dir(tmpdir + "/testdb3");
    var sql3 = new Sql.connection(tmpdir+"/testdb3", true);
    sql3.close();

    copyFile(tmpdir+"/testdb/quicktest.tbl", tmpdir+"/testdb3/quicktest.tbl", true);

    var sql3=Sql.connect({
        path:      tmpdir+"/testdb3",
        addTables: true
    });

    var res=sql3.exec("select * from quicktest");

    return res.rowCount;
});

/* ----- Sql.list() — explicit IN-list parameter wrapper ----- */

testFeature("Sql.list - setup test table", function() {
    if (sql1.one("select * from SYSTABLES where NAME='listtest'"))
        sql1.one("drop table listtest");
    sql1.one("create table listtest ( id int, val double, name varchar(64) )");
    var rows = [
        [1, 1.5, 'alpha'],
        [2, 2.5, 'beta'],
        [3, 3.5, 'gamma'],
        [4, 4.5, 'delta'],
        [5, 5.5, 'epsilon']
    ];
    for (var i = 0; i < rows.length; i++)
        sql1.exec("insert into listtest values(?,?,?)", rows[i]);
    return sql1.one("select count(*) c from listtest").c == 5;
});

testFeature("Sql.list - numeric IN against int column", function() {
    var ids = sql1.exec("select id from listtest where id in (?) order by id",
                        [Sql.list([1,3,5])]).rows.map(function(r){return r.id});
    return JSON.stringify(ids) === "[1,3,5]";
});

testFeature("Sql.list - numeric IN with no matches", function() {
    var rs = sql1.exec("select id from listtest where id in (?)", [Sql.list([99])]).rows;
    return rs.length === 0;
});

testFeature("Sql.list - single-element numeric list", function() {
    var ids = sql1.exec("select id from listtest where id in (?)",
                        [Sql.list([1])]).rows.map(function(r){return r.id});
    return JSON.stringify(ids) === "[1]";
});

testFeature("Sql.list - numeric IN against double column", function() {
    var ids = sql1.exec("select id from listtest where val in (?) order by id",
                        [Sql.list([1.5, 3.5])]).rows.map(function(r){return r.id});
    return JSON.stringify(ids) === "[1,3]";
});

testFeature("Sql.list - ints do not coerce to non-equal doubles", function() {
    /* val column has 1.5,2.5,...; passing [2,4] should not match */
    var rs = sql1.exec("select id from listtest where val in (?)",
                       [Sql.list([2,4])]).rows;
    return rs.length === 0;
});

testFeature("Sql.list - string IN against varchar (strlst)", function() {
    var ids = sql1.exec("select id from listtest where name in (?) order by id",
                        [Sql.list(['alpha','gamma','epsilon'])]).rows.map(function(r){return r.id});
    return JSON.stringify(ids) === "[1,3,5]";
});

testFeature("Sql.list - string IN with no matches", function() {
    var rs = sql1.exec("select id from listtest where name in (?)",
                       [Sql.list(['nonexistent'])]).rows;
    return rs.length === 0;
});

testFeature("Sql.list - single-element string list", function() {
    var ids = sql1.exec("select id from listtest where name in (?)",
                        [Sql.list(['alpha'])]).rows.map(function(r){return r.id});
    return JSON.stringify(ids) === "[1]";
});

/* error paths — each should throw */
function _listShouldThrow(arg) {
    try { Sql.list(arg); return false; }
    catch(e) { return true; }
}
function _listShouldThrowNoArg() {
    try { Sql.list(); return false; }
    catch(e) { return true; }
}

testFeature("Sql.list - throws on no argument",          _listShouldThrowNoArg);
testFeature("Sql.list - throws on non-array argument",   function(){ return _listShouldThrow(123) && _listShouldThrow('abc') && _listShouldThrow({}); });
testFeature("Sql.list - throws on empty array",          function(){ return _listShouldThrow([]); });
testFeature("Sql.list - throws on mixed types",          function(){ return _listShouldThrow([1,'a',2]); });
testFeature("Sql.list - throws on NaN/Infinity",         function(){ return _listShouldThrow([NaN]) && _listShouldThrow([Infinity]); });
testFeature("Sql.list - throws on null/boolean elements",function(){ return _listShouldThrow([null]) && _listShouldThrow([true]); });

testFeature("Sql.list - bare-array (deprecated) still works", function() {
    var ids = sql1.exec("select id from listtest where id in (?) order by id",
                        [[1,3,5]]).rows.map(function(r){return r.id});
    return JSON.stringify(ids) === "[1,3,5]";
});

/* ------------------------------------------------------------------ *
 * udate — signed int64 microseconds since 1970-01-01 UTC
 * ------------------------------------------------------------------ */

var sqlu = Sql.connect(tmpdir+"/udatedb", true);
var US   = 1786117587123456;          /* 2026-08-07 ...  .123456 */

testFeature("udate - create column", function(){
    sqlu.exec("create table ut ( id int, u udate );");
    var r = sqlu.exec("select NAME,TYPE from SYSCOLUMNS where TBNAME='ut' and NAME='u'");
    return r.rows.length == 1 && r.rows[0].TYPE == 'udate';
});

testFeature("udate - a javascript Number means SECONDS", function(){
    /* Both integral and fractional Numbers mean seconds.  rampart-sql
       binds integral values as int64 and fractional ones as double, so
       if the two disagreed on units the same instant would land in
       different centuries depending on whether a fraction was present. */
    sqlu.exec("insert into ut values(1,?);", [US/1e6]);
    var r = sqlu.exec("select convert(u,'int64') v from ut where id=1");
    return Number(r.rows[0].v) === US;
});

testFeature("udate - integral and fractional Numbers agree on units", function(){
    var secs = 1786117587;
    sqlu.exec("insert into ut values(90,?);", [secs]);       /* -> int64  */
    sqlu.exec("insert into ut values(91,?);", [secs + 0.5]); /* -> double */
    var a = Number(sqlu.exec("select convert(u,'int64') v from ut where id=90").rows[0].v);
    var b = Number(sqlu.exec("select convert(u,'int64') v from ut where id=91").rows[0].v);
    return a === secs*1000000 && b === secs*1000000 + 500000;
});

testFeature("udate - out-of-range seconds warns instead of overflowing", function(){
    /* passing microseconds where seconds are meant would overflow int64;
       it must not wrap into a bogus year */
    sqlu.exec("insert into ut values(92,?);", [US]);
    var v = Number(sqlu.exec("select convert(u,'int64') v from ut where id=92").rows[0].v);
    return v === 0;
});

testFeature("udate - convert to double gives SECONDS (round-trips)", function(){
    var v = sqlu.exec("select convert(u,'double') v from ut where id=1").rows[0].v;
    if (Math.abs(v - US/1e6) > 1e-6 || Math.round(v*1e6) !== US) return false;
    /* seconds out, seconds in: a double round-trips through the column */
    sqlu.exec("insert into ut values(93,?);", [v]);
    return Number(sqlu.exec("select convert(u,'int64') v from ut where id=93").rows[0].v) === US;
});

testFeature("udate - convert to date floors to the second", function(){
    var v = sqlu.exec("select convert(u,'date') v from ut where id=1").rows[0].v;
    return (v instanceof Date) &&
           Math.floor(v.getTime()/1000) === Math.floor(US/1e6);
});

testFeature("udate - convert to char keeps 6 fractional digits", function(){
    var v = sqlu.exec("select convert(u,'char') v from ut where id=1").rows[0].v;
    return typeof v === 'string' && /\.123456$/.test(v);
});

testFeature("udate - accepts a date string with fractional seconds", function(){
    sqlu.exec("insert into ut values(2,?);", ["2026-08-07 14:23:45.123456"]);
    var v = sqlu.exec("select convert(u,'int64') v from ut where id=2").rows[0].v;
    return String(v).slice(-6) === '123456';
});

testFeature("udate - accepts a javascript Date exactly", function(){
    var d = new Date(Date.UTC(2026,7,7,14,23,45,789));
    sqlu.exec("insert into ut values(3,?);", [d]);
    var v = sqlu.exec("select convert(u,'int64') v from ut where id=3").rows[0].v;
    return Number(v) === d.getTime() * 1000;
});

testFeature("udate - reads back into javascript as a Date (lossy ms)", function(){
    var v = sqlu.exec("select u from ut where id=1").rows[0].u;
    return (v instanceof Date) && v.getTime() === Math.floor(US/1000);
});

testFeature("udate - negative (pre-1970) stored exactly", function(){
    /* -1.5 SECONDS = 1969-12-31 23:59:58.5 */
    sqlu.exec("insert into ut values(4,?);", [-1.5]);
    var v = sqlu.exec("select convert(u,'int64') v from ut where id=4").rows[0].v;
    return Number(v) === -1500000;
});

testFeature("udate - narrowing floors toward the past, not toward zero", function(){
    /* -1500000us is 1969-12-31 23:59:58.5; the second CONTAINING it is
       -2, not -1.  C truncation would give -1 and split behaviour
       either side of the epoch. */
    var v = sqlu.exec("select convert(u,'date') v from ut where id=4").rows[0].v;
    return Math.floor(v.getTime()/1000) === -2;
});

testFeature("udate - ordering and comparison are exact", function(){
    sqlu.exec("create table ut2 ( id int, u udate );");
    for (var i=0; i<50; i++)
        sqlu.exec("insert into ut2 values(?,?);", [i, 1700000000 + i]);   /* seconds */
    /* bind the pivot in the same units it was written in: 'double' is the
       seconds view, and the one that round-trips */
    var pivot = sqlu.exec("select convert(u,'double') v from ut2 where id=25").rows[0].v;

    /* maxRows defaults to 10 -- without this the 50-row set is
       truncated and the membership check below fails */
    var gt = sqlu.exec("select id from ut2 where u > ? order by id",
                       [pivot], {maxRows:-1}).rows.map(function(r){return r.id});
    var desc = sqlu.exec("select id from ut2 order by u desc",
                         {maxRows:-1}).rows.map(function(r){return r.id});

    /* membership, not just count: a wrong comparator can return the
       right number of rows */
    if (gt.length !== 24) return false;
    for (var i=0; i<gt.length; i++) if (gt[i] !== 26+i) return false;
    return desc[0] === 49 && desc[desc.length-1] === 0;
});

testFeature("udate - 'now' has microsecond resolution", function(){
    /* parsetime() returns a time_t, so 'now' routed through it would
       always land on a whole second and defeat the point of the type.
       Five samples: at least one must carry a sub-second part. */
    sqlu.exec("create table ut3 ( id int, u udate );");
    for (var i=0; i<5; i++) sqlu.exec("insert into ut3 values(?,'now');", [i]);
    var frac = 0, res = sqlu.exec("select convert(u,'int64') v from ut3",
                                  {maxRows:-1});
    res.rows.forEach(function(r){ if (Number(r.v) % 1000000) frac++; });
    return res.rows.length === 5 && frac > 0;
});

testFeature("udate - 'now' is the actual current time", function(){
    var v = Number(sqlu.exec("select convert(u,'int64') v from ut3",
                             {maxRows:1}).rows[0].v);
    return Math.abs(v/1e6 - Date.now()/1000) < 300;   /* within 5 minutes */
});

testFeature("udate - 'now' works in UPDATE", function(){
    sqlu.exec("insert into ut3 values(99,'2001-01-01 00:00:00');");
    var before = Number(sqlu.exec("select convert(u,'int64') v from ut3 where id=99")
                        .rows[0].v);
    sqlu.exec("update ut3 set u='now' where id=99;");
    var after = Number(sqlu.exec("select convert(u,'int64') v from ut3 where id=99")
                       .rows[0].v);
    return after > before && Math.abs(after/1e6 - Date.now()/1000) < 300;
});

testFeature("udate - convert(?,'udate') parses javascript date strings", function(){
    /* the tz-offset form a rampart Date prints, plus ISO-8601 */
    var forms = ["2026-08-07 11:11:27.472-07:00",
                 "2026-08-07T18:11:27.472Z",
                 "2026-08-07 18:11:27.472 UTC"];
    var want = Date.UTC(2026,7,7,18,11,27,472) * 1000;   /* microseconds */
    for (var i=0; i<forms.length; i++) {
        var r = sqlu.exec("select convert(convert(?,'udate'),'int64') v;",
                          [forms[i]]);
        if (!r.rows.length || Number(r.rows[0].v) !== want) return false;
    }
    return true;
});

testFeature("udate - a printed Date re-parses to the same instant", function(){
    sqlu.exec("insert into ut3 values(98,?);", [new Date()]);
    var printed = String(sqlu.exec("select u from ut3 where id=98").rows[0].u);
    sqlu.exec("insert into ut3 values(97,?);", [printed]);
    var a = Number(sqlu.exec("select convert(u,'int64') v from ut3 where id=98").rows[0].v);
    var b = Number(sqlu.exec("select convert(u,'int64') v from ut3 where id=97").rows[0].v);
    /* the printed form carries milliseconds, so they agree to the ms */
    return Math.floor(a/1000) === Math.floor(b/1000);
});

testFeature("udate - convert to uint64 gives microseconds", function(){
    var r = sqlu.exec("select convert(u,'uint64') a, convert(u,'int64') b " +
                      "from ut where id=1").rows[0];
    return Number(r.a) === US && String(r.a) === String(r.b);
});

testFeature("udate - uint64 in means SECONDS, like int64", function(){
    /* the two 64-bit integer types must not disagree on units */
    var secs = 1786117587;
    sqlu.exec("insert into ut values(80,convert(convert(?,'uint64'),'udate'));",
              [secs]);
    return Number(sqlu.exec("select convert(u,'int64') v from ut where id=80")
                  .rows[0].v) === secs*1000000;
});

testFeature("udate - uint64 out-of-range seconds warns, stores 0", function(){
    sqlu.exec("insert into ut values(81,convert(convert(?,'uint64'),'udate'));",
              [US]);
    return Number(sqlu.exec("select convert(u,'int64') v from ut where id=81")
                  .rows[0].v) === 0;
});

testFeature("udate - pre-1970 cannot be a uint64, warns and gives 0", function(){
    /* id=4 holds -1.5s; int64 keeps it, uint64 cannot represent it */
    var r = sqlu.exec("select convert(u,'int64') a, convert(u,'uint64') b " +
                      "from ut where id=4").rows[0];
    return Number(r.a) === -1500000 && Number(r.b) === 0;
});

testFeature("udate - btree index", function(){
    sqlu.exec("create index ut2_u_x on ut2(u);");
    return sqlu.exec("select NAME from SYSINDEX where NAME='ut2_u_x'").rows.length == 1;
});

rm_rf_dir(tmpdir);

testFeature.exit();
