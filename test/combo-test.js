/* make printf et. al. global */
rampart.globalize(rampart.utils);

var crypto=require("rpcrypto");
var Sql=require("rpsql");
var sql=new Sql.init("./testdb",true);//create if doesn't exist

var sha256=crypto.sha256;
var md5=crypto.md5;

function die()
{
    printf("test failed\n");
    process.exit(1);
}

function verbsert(cond)
{
    if(cond)
        printf("success\n");
    else
        die();
}
function assert(cond)
{
    if(!cond)
        die();
}

printf("test of sha256 and hexify/dehexify\n");
var sha_res_buf=sha256("hello",true); //true = return buffer with binary
var sha_res_upper=hexify(sha_res_buf,true); // true = upper case A-F
var sha_res_lower=hexify(sha_res_buf);

var sha_res1=dehexify(sha_res_lower);
var sha_res2=dehexify(sha_res_upper);

sha_res1=hexify(sha_res1);
sha_res2=hexify(sha_res2);

verbsert(sha_res2 = sha_res1)

sql.exec("drop table urls;");
sql.exec("drop table urls2;");

printf("test create a table:\n     create table urls ( Md5 byte(16), Url varchar(16) );\n");
sql.exec("create table urls ( Md5 byte(16), Url varchar(16) );");
var ret=sql.exec("select * from SYSTABLES where NAME='urls'");
verbsert(ret.results.length)


var urls=["http://dogpile.com/","http://google.com/","http://free-images.com/","http://snappygoat.com/", "http://snappygoat.com/"];
printf("test insert of urls and binary md5 hash\n     %J\n",urls);
for (var i=0; i<urls.length; i++)
{
    var u=urls[i];
    var buf=md5(u,true);//true means keep it binary in a buffer
    sql.exec("insert into urls values(?,?);",[buf,u]);
}
ret=sql.exec("select * from urls");
verbsert(ret.results.length==5);
    


printf("create unique index on non-unique data:\n     create unique index urls_Md5_ux on urls(Md5);\n");
var ret=sql.exec("create unique index urls_Md5_ux on urls(Md5);");
printf("sql.lastErr:\n     %s", Sql.sandr(/>>\n=\F.=/,"\n     ",sql.lastErr) );
verbsert(Sql.rex(/non-unique=/,sql.lastErr).length);

printf("select md5 hash, convert to hex and compare to md5 of url.  Also do nested sql copy to table urls2\n");
sql.exec("create table urls2 ( Md5 byte(16), Url varchar(16) );");
sql.exec("select * from urls",
    function(res){
        var md5sum=hexify(res.Md5);
        var md5comp=crypto.md5(res.Url);

        assert(md5sum==md5comp)
        sql.exec("insert into urls2 values(?,?)",[res.Md5,res.Url]);
    }
);
ret=sql.exec("select * from urls2");
verbsert(ret.results.length==5);


printf("checking hashes in url2\n");
sql.exec("select * from urls2",
    function(res){
        var md5sum=hexify(res.Md5);
        var md5comp=crypto.md5(res.Url);
        
        assert(md5sum==md5comp);
    }
);
printf("success\n");


printf("inserting a duplicate into table with unique index\n");
ret=sql.exec("insert into urls values (?,?);",[md5(urls[0],true),urls[0]]);
if ( Sql.rex(/duplicate=/,sql.lastErr).length )
    console.log(sql.lastErr);
else
    die();
  
    
printf('JSON.stringify(sql.exec("select bintohex(Md5) Md5, Url from urls"),null,2)\n%s\n',
  JSON.stringify(
    sql.exec("select bintohex(Md5) Md5, Url from urls"),
  null,2)
);

sql.exec("drop table urls");
sql.exec("drop table urls2");

printf("all tests succeeded\n");