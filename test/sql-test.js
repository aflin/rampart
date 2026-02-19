rampart.globalize(rampart.utils);

load.Sql;
load.crypto;

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

var sql=Sql.connect(process.scriptPath+"/testdb",true);//create if doesn't exist

/* check for quicktest, make if necessary */
var res=sql.exec("select * from SYSTABLES where NAME='quicktest'");
if(res.rows.length==0) {
    res=sql.exec("create table quicktest ( I int, Text varchar(16) );");
    sql.exec("insert into quicktest values(2,'just a test');");
    sql.exec("create index quicktest_I_x on quicktest(I);");
}


var sha256=crypto.sha256;
var md5=crypto.md5;

function testFeature(name,test)
{
    var error=false;
    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }
    printf("testing sql - %-54s - ", name);
    if(test)
        printf("passed\n")
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        if(error) console.log(error);
        rm_rf_dir(process.scriptPath + "/testdb");
        rm_rf_dir(process.scriptPath + "/testdb2");
        rm_rf_dir(process.scriptPath + "/testdb3");
        process.exit(1);
    }
    if(error) console.log(error);
}

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

fprintf(process.scriptPath+"/gettysburg.txt", '%s', getty);


testFeature ("searchFile", function() {
    var res = Sql.searchFile(
       "live",
       process.scriptPath+"/gettysburg.txt",
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


var sql1 = Sql.connect(process.scriptPath+"/wdb", true);

testFeature ("sql.importCsvFile", function() {
    var wiki = fopen("./wiki.csv", "w+");

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

    var ret=sql1.importCsvFile("./wiki.csv", {tableName:"wtext",hasHeaderRow: true});
    return ret==16;
});

function nestedcopy() {
    var sql2 = Sql.connect(process.scriptPath+"/wdb2", true);

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
    rm_rf_dir(process.scriptPath + "/testdb2");
    mkdir(process.scriptPath+"/testdb2")
    copyFile(process.scriptPath+"/testdb/quicktest.tbl", process.scriptPath+"/testdb2/quicktest.tbl", true);

    var sql2=new Sql.connection({
        path:      process.scriptPath+"/testdb2",
        addTables: true
    });
    var res=sql2.exec("select * from quicktest");

    return res.rowCount;
});

testFeature ("Create with addTables in existing db", function(){
    rm_rf_dir(process.scriptPath + "/testdb3");
    var sql3 = new Sql.connection(process.scriptPath+"/testdb3", true);
    sql3.close();

    copyFile(process.scriptPath+"/testdb/quicktest.tbl", process.scriptPath+"/testdb3/quicktest.tbl", true);

    var sql3=Sql.connect({
        path:      process.scriptPath+"/testdb3",
        addTables: true
    });

    var res=sql3.exec("select * from quicktest");

    return res.rowCount;
});

rm_rf_dir(process.scriptPath + "/testdb");
rm_rf_dir(process.scriptPath + "/testdb2");
rm_rf_dir(process.scriptPath + "/testdb3");
