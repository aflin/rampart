/* make printf et. al. global */
rampart.globalize(rampart.utils);

var crypto=require("rampart-crypto");
var Sql=require("rampart-sql");
var sql=new Sql.init("./testdb",true);//create if doesn't exist

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
    printf("testing %-40s - ", name);
    if(test)
        printf("passed\n")
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        if(error) console.log(error);
        process.exit(1);
    }
    if(error) console.log(error);
}



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

sql.exec("drop table urls;");
sql.exec("drop table urls2;");

testFeature ("create a sql table", function(){
  sql.exec("create table urls ( Md5 byte(16), Url varchar(16) );");
  var ret=sql.exec("select * from SYSTABLES where NAME='urls'");
  return ret.results.length != 0;
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
  return ret.results.length == 5;
});    


testFeature("making unique index on non-unique data", function(){
  var ret=sql.exec("create unique index urls_Md5_ux on urls(Md5);");
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
  return ret && res.results.length == 5;
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
  var res=sql.exec("insert into urls values (?,?);",[md5(urls[0],true),urls[0]]);
  if ( Sql.rex(/duplicate=/,sql.errMsg).length )
    return true;

  return false;
});  
   
testFeature("built in sql bintohex()", function(){
  var res=sql.exec("select bintohex(Md5) Md5, Url from urls",{max:1});
  var md5comp=md5(res.results[0].Url);
  return md5comp == res.results[0].Md5;
});


sql.exec("drop table urls");
sql.exec("drop table urls2");

