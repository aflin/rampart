/* make printf et. al. global */
rampart.globalize(rampart.cfunc);

/* load the http server module */
var server=require("rpserver");

/* load sql database module */
var Sql=require("rpsql");

/* sql module can be loaded here (better) or in callback functions (minor check overhead).
     If used to create database, the overhead is not minor, and should be done here rather
     than repeatedly in a callback
*/

var sql= new Sql.init("./testdb",true); /* true means create the database if it doesn't exist */

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

/* same for inserttest table.  And populate the table with 2000 rows */
res=sql.exec("select * from SYSTABLES where NAME='inserttest'");
if(res.results.length==0) {
    sql.exec("create table inserttest ( I int, D double, Text varchar(16) );");
    sql.exec("create index inserttest_I_x on inserttest(I);");
    for (var i=0;i<200;i++)
        inserttest_callback({},true);

    /* test delete of 10 rows */
    console.log("delete 10");
    res=sql.exec("delete from inserttest",
                    {max:10}
    );
    console.log(res);

    console.log("nrows should be 1990");
    res=sql.exec("select count(*) nrows  from inserttest");
    console.log(res);
}


/* *******************************************************************************
sample calls to sql with/without callback
with callback returns one row at a time, and can be cancelled by returning false
without callback returns an array of rows.
Rows are returned as one of 4 different return types:
    default -- object or array of objects {col1name:val1,col2name:val2...
    array   -- [val1, val2, ..]
    arrayh  -- first row is column names, then like array
    novars  -- returns empty array, or empty object if using a callback
* ********************************************************************************* */

/*
console.log("return default object");
res=sql.exec("select * from inserttest",
                {max:10,skip:20}
);
console.log(res);

console.log("return arrayh");
res=sql.exec("select * from inserttest",
                {max:10,returnType:"arrayh"}
);
console.log(res);

console.log("return novars");
res=sql.exec("select * from inserttest",
                {max:10,returnType:"novars"}
);
console.log(res);

console.log("return callback novars");
res=sql.exec("select * from inserttest",
                {max:10,returnType:"novars"},
                function(res,i,info){
                    console.log(res,i,info);
                }

);
console.log("total: "+res);

console.log("return callback arrayh");
res=sql.exec("select * from inserttest",
                {max:10,returnType:"arrayh"},
                function(res,i,info){
                    console.log(res,i,info);
                }
);
console.log("total: "+res);

console.log("return callback normal");
res=sql.exec("select * from inserttest",
                {max:10},
                function(res,i,info){
                    console.log(res,i,info);
                }
);
console.log(res);
*/


/* ******************************************************
   Callbacks for server test
   ****************************************************** */

/* 
   Since the http server is multithreaded, and the javascript interpreter
   is not, each thread must have its own javascript heap where the callback
   will be copied.
   Globally declared functions and server callback functions
   below will be copied to each thread's stack.  Scoped variables that are 
   not global will not be copied.
   Best practice is to think of every function as its own distinct script
   as variables copied to each stack will only be local to that thread.
*/

function inserttest_callback(req,allinserts){
    /* randpic function is naturally accessible to 
       inserttest_callback()
    */
    function randpic(arr,x){
        if(x===undefined)
            x=Math.floor(Math.random()*arr.length);
        else
            x=Math.floor(x*arr.length);

        return(arr[x]);
    }
    var str=["zero","one","two","three","four","five","six","seven","eight","nine"];
    var res;
    var r=Math.random();
    var rf=r*10;
    var rp=Math.floor(r*100);
    var ri=Math.floor(r*10);
    var skip=Math.floor(Math.random()*100);
    var insertmax=25;

    /* to populate the table */
    if(allinserts) insertmax=100;    

    if (rp<insertmax)
    {
//        console.log("insert");
        for (var i=0;i<10;i++)
        {
          res=sql.exec(
            'insert into inserttest values (?,?,?)',
            [ri,rf,randpic(str,r)]
          );
        }
    }
    else if (rp<50)
    {
//        console.log("delete")
          res=sql.exec(
            "delete from inserttest",
            /* Won't fix: skipped rows are deleted too 
            {skip:10,max:10,returnType:"novars"} -> 20 rows deleted
            */
            /* 
               normally deleted rows are also
               selected and returned.  Here we
               don't need them, so "novars" is set
            */
            {max:10,returnType:"novars"}
          );
        //console.log(res);
    }
    else
    {
//        console.log("select");
        res=sql.exec(
            "select * from inserttest where I > ?",
            [5],
            {skip:skip,max:10,returnType:"array"},
            /* sanity check callback */
            function(result) {
                var f2i=parseInt(result[1]);
                if(f2i!=result[0] || result[2]!=str[f2i])
                    console.log("DANGER WILL ROBINSON:",req);
            }
        );
    }

    /* return value is sent to http client
       use file extensions like text (or txt), html, jpg, etc as key to set
       the proper mime-type.  Most extensions are mapped to the correct mime type
       See mime.h for complete listing.
    */    
    return({
        text: "this is for doing multithreaded stress testing, such as: ab -n 10000 -c 100 http://127.0.0.1:8088/dbtest.html\n"
    });
}

function simple_callback(req){
//    var Sql=require("rpsql");
//    var sql=new Sql.init('./testdb');
    var arr=sql.exec(
        'select * from quicktest',
        {max:1}
    );
    /* default mime type is text/plain, if just given a string */
    return(JSON.stringify(arr));
}

/* print out some info about the request */


/* this works as expected since x.func is global
 
   If this entire script was wrapped in a function and that function was called,
   this would produce an error since x.func would no longer be global.
   Best practice currently is to put functions you need inside the callback
   function and treat each callback as if it were its own script, and
   as if it is being executed and exits after each webpage is served.
*/ 

var x={msg:"HELLO WORLD",func:simple_callback};


function globalRef_callback(req){
    return ( x.func(req) );
}

/* currently a redis server must be running */
function ramistest(req) {
    var ra=new Ramis();
    var insertvar="0123456789abcdef";
    var rno=Math.floor(Math.random()*1000000);

    for (var i=0;i<333;i++) {
        resp=ra.exec("SET key%d%d %b",rno, i, insertvar, insertvar.length);
        if(resp[0]!="OK") {
            print("get error. got" + resp[0]);
            return("GET error");
        }
    }

    for (var i=0;i<333;i++) {
        resp=ra.exec("GET key%d%d",rno,i);
        if(resp[0].length != 16){
            print("set error. got" + resp[0].length);
            return("SET error");
        }
    }

    for (var i=0;i<333;i++) {
        resp=ra.exec("DEL key%d%d",rno,i);
        if(resp[0] != "1") {
            print("del error. got "+resp[0]);
            return("DEL error");
        }
    }

    return("OK");
}

printf("\ntry a url like http://127.0.0.1:8088/showreq.html?var1=val1&color=red&size=15\n");
printf("or see a sample website at http://127.0.0.1:8088/\n\nSTARTING SERVER:\n");

/* 
    Configuration and Start of Webserver:
    server.start() never returns and no code after this will be run

    It will be started with one thread per cpu core, or with the number configured 
    Each thread has its own JS interpreter/stack/heap.
    Global variables and ECMA functions from the main thread will be copied
    to all of the server threads.  Modules loaded before server starts
    will be available in each thread as well. Non global variables
    that are in scope will not be copied.

    Functions are copied as compiled bytecode. Not all functionality will 
    transfer.  See the Bytecode limitations section here: 
    https://github.com/svaarala/duktape/blob/master/doc/bytecode.rst
*/

server.start(
{
    ip:"0.0.0.0",  //this binds to all. Default is 127.0.0.1
    ipv6:"::",     //this binds to all. Default is ::1
    port:8088,     //This is the default.  If set to <1024 (i.e. 80), you must be root and set user below
    ipv6port:8088, //defaults to port above if not set
    // if you run as root, you must set user.  If not root, user is ignored.
    //user: "nobody",
    scriptTimeout: 10.0, /* max time to spend in JS */
    connectTimeout:20.0, /* how long to wait before client sends a req or server can send a response */
    useThreads: true, /* make server multi-threaded. */

    //user:"unpriv_user", /* if binding to, e.g. port 80, start as root and drop privileges as the user named here */

    /*  By default, number of threads is set to cpu core count.
        ipv6 and ipv4 are separate servers and each get this number of threads.
        This has no effect unless useThreads is set true.
        The number can be changed here:
    */
    //threads: 8, /* for a 4 core, 8 virtual core hyper-threaded processor. */

    /* for experimental https support, this is the minimum (more options to come): */
    /*
    secure:true,
    sslkeyfile:  "/etc/letsencrypt/live/mydom.com/privkey.pem",
    sslcertfile: "/etc/letsencrypt/live/mydom.com/fullchain.pem",
    // if files above are invalid, it will silently revert to http
    */
    
    /*  By default server binds to both ipv4 and ipv6.
        To turn off ipv6 or ipv4, set one of these to false */
    // useIpv6: false,
    // useIpv4: false,
    

    /* **********************************************************
       map urls to functions or paths on the filesystem 
       order by specificity.  '/' should always be last  
       If it ends in a '/' then matches everything in that path
       except a more specific ('/something.html') path
       ********************************************************** */
    map:
    {
        /* url to function mappings */
        "/dbtest.html":       inserttest_callback,
        "/simpledbtest.html": simple_callback,
        "/globalref.html":    globalRef_callback,
        "/ramistest" :        ramistest,

        /* matching a glob to a function */
        /* use function from a module */
        "/showreq*":          {module:"servermod"},

        //  filesystem mappings are always folders.  
        //   "/tetris"    becomes  "/tetris/
        //   "./mPurpose" becomes  "./mPurpose/"
        "/tetris":            "./tetris-tutorial/",
        "/":                  "./mPurpose"
    }
     /* 
        including a function here will match everything not matched above
        matching "/" if "/" : "./mPurpose/" was not present
     */
     /* ,function(){} */
});
