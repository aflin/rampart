/* make printf et. al. global */
rampart.globalize(rampart.utils);

/* load the http server module */
var server=require("rpserver");

/* load curl module */
var curl=require("rpcurl");


/* sql module can be loaded here (better) or in callback functions (minor check overhead).
     If used to create database, the overhead is not minor, and should be done here rather
     than repeatedly in a callback
*/
var Sql=require("rpsql");

var sql= new Sql.init(process.scriptPath+"/testdb",true); /* true means create the database if it doesn't exist *

/* ******************************************************
    Setup of tables for server callback function tests 
********************************************************* */

/* check if our quicktest table exists.  If not, make it */
var res=sql.exec("select * from SYSTABLES where NAME='quicktest'");
if(res.results.length==0) {
    res=sql.exec("create table quicktest ( I int, Text varchar(16) );");
    sql.exec("insert into quicktest values(2,'just a test');");
    sql.exec("create index quicktest_I_x on quicktest(I);");
}

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
        printf(">>>>> FAILED <<<<<\n");
    if(error) console.log(error);
}


try {
    stat("sample-cert.pem");
    stat("sample-key.pem");
} catch (e)
{
    shell("openssl req -x509 -nodes -days 365 -newkey rsa:2048 -keyout sample-key.pem -out sample-cert.pem -config sample-cert.conf");
}

var pid=server.start(
{
    bind: "127.0.0.1:8087",

    //user: "nobody",

    scriptTimeout: 1.0, /* max time to spend in JS */
    connectTimeout:20.0, /* how long to wait before client sends a req or server can send a response */
    useThreads: true, /* make server multi-threaded. */
    daemon: true,

    secure:true,
    sslKeyFile:  "./sample-key.pem",
    sslCertFile: "./sample-cert.pem",

    /* sslMinVersion (ssl3|tls1|tls1.1|tls1.2). "tls1.2" is default*/
    // sslMinVersion: "tls1.2",

    notFoundFunc: function(req){
        return {
            status:404,
            text: "notfound"
        }
    },    

    /* **********************************************************
       map urls to functions or paths on the filesystem 
       If it ends in a '/' then matches everything in that path
       except a more specific ('/something.html') path
       ********************************************************** */
    map:
    {
        /*
            filesystem mappings are always folders.  
             "/tetris"    becomes  "/tetris/
             "./mPurpose" becomes  "./mPurpose/"
        */
        "/":                "./mPurpose",
        "/sample":          function(req){return "test";},
        "/modtest/":	    {modulePath:"./servermods/"},
        "/timeout":         function(){
                                for (var i=0;i<1000000000;i++);
                                return("done");
                            }
    }
});

// if daemon==true then we get the pid of the detached process
// otherwise server.start() never returns

/* give the forked server a chance to print its info*/
rampart.utils.sleep(0.2);

testFeature("server is running", rampart.utils.kill(pid,0) );

testFeature("curl secure request/redirect/follow", function() {
    var res=curl.fetch("https://google.com/");
    var res2=curl.fetch({location:true},"https://google.com/");
    if (res.status == 301 && res2.status == 200)
        return true;
    console.log(res.errMsg,res2.errMsg);
    return false;
});

testFeature("curl https request localhost --insecure", function() {
    var res=curl.fetch("https://localhost:8087/sample");
    var res2=curl.fetch({insecure:true},"https://localhost:8087/sample");
    if (res.errMsg.length && res2.text == "test")
        return true;
    console.log(res.errMsg,res2.errMsg);
    return false;
});

testFeature("curl parallel fetch", function() {
    var a="https://localhost:8087/sample";
    var aa=[a,a,a,a,a,a,a,a,a,a];
    var n=0;
    curl.fetch({insecure:true},aa,function(res){
        if(res.text=='test') n++;
    });
    return n == 10;
});

testFeature("server modpath", function (){
    var res=curl.fetch({insecure:true},"https://localhost:8087/modtest/simpledbtest.html");
    var j=JSON.parse(res.text);
    return res.status == 200;
});

testFeature("server custom not found", function (){
    var res=curl.fetch({insecure:true},"https://localhost:8087/nowhere");
    return res.status == 404 && res.text == "notfound";
});

testFeature("server script timeout", function (){
    var res=curl.fetch({insecure:true},"https://localhost:8087/timeout");
    return res.status == 500;
});

kill(pid,15);