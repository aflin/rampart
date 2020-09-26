/* *******************************************************************
     This example demonstrates most of the functionality of the http server
     as well as some from the sql module.

     The first part sets up a test database for use by the http server.

     The second part defines functions used as callbacks for the http server

     The third part sets up and runs the http server.

   ******************************************************************* */


/* 
    Put printf et. al. into the global namespace
    For convenience so that one can use
        printf("...")
    in place of
        rampart.utils.printf("...");
*/
rampart.globalize(rampart.utils);

/* load the http server module */
var server=require("rpserver");

/* ***********  PART I - sql db setup ************* */

/* load sql database module */
var Sql=require("rpsql");

/* 
   sql module can be loaded here (better) or in callback functions (minor check overhead).
     If used to create database, the overhead is not minor, and should be done here rather
     than repeatedly in a callback
*/

var sql= new Sql.init(process.scriptPath+"/testdb",true); /* true means create the database if it doesn't exist */

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
sample calls to sql with/without callback for illustration purposes.
With callback returns one row at a time, and can be cancelled by returning false
Without callback returns an array of rows.
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

/* ***********  PART II - Callback functions for http server ************* */


/* 
   Since the http server is multithreaded, and the javascript interpreter
   is not, each thread has its own javascript stack/heap and all callback
   will be copied to each thread/stack/heap.
   Globally declared functions and server callback functions
   below will be copied to each thread's stack.  Scoped variables that are 
   not global will not be copied.
   Best practice is to think of every function as its own distinct script
   as variables copied to each stack will only be local to that thread.
*/


/* randomly insert and update a table 10 times*/
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
          res=sql.exec(
            "delete from inserttest",
            /* WARNING: skipped rows are deleted too 
            {skip:10,max:10,returnType:"novars"} -> 20 rows deleted
            */
            /* 
               normally deleted rows are also
               selected and returned.  Here we
               don't need them, so "novars" is set
            */
            {max:10,returnType:"novars"}
          );
    }
    else
    {
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
    /* since Sql and sql vars are global, they copied to each thread
        and it is not necessary to re-require() or re-init() on
        each invocation of this callback
    */
    //var Sql=require("rpsql");
    //var sql=new Sql.init('./testdb');
    var arr=sql.exec(
        'select * from quicktest',
        {max:1}
    );
    /* default mime type is text/plain in the case where 
        a string is returned instead of an object */
    return(JSON.stringify(arr));
}

/* this works as expected since x.func is global
 
   If this entire script was wrapped in a function and that function was called,
   this would produce an error since x.func would no longer be global.
   Best practice for functions in the main script is to put functions 
   you need inside the callback function and treat each callback as if 
   it were its own script, and as if it is being executed and exits after 
   each webpage is served.
*/ 

var x={msg:"HELLO WORLD",func:simple_callback};

function globalRef_callback(req){
    return ( x.func(req) );
}


/* a redis server must be running on its default port*/
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

/* this is a sample function to return a webpage listing the contents of a directory.
   see directoryFunc in server.start() below.  This function is the same as the
   built in server.defaultDirList() function.
*/

function dirlist(req) {
    var html="<html><head><title>Index of " + 
        req.path.path+ 
        "</title><style>td{padding-right:22px;}</style></head><body><h1>"+
        req.path.path+
        '</h1><hr><table>';

    function hsize(size) {
        var ret=sprintf("%d",size);
        if(size >= 1073741824)
            ret=sprintf("%.1fG", size/1073741824);
        else if (size >= 1048576)
            ret=sprintf("%.1fM", size/1048576);
        else if (size >=1024)
            ret=sprintf("%.1fk", size/1024); 
        return ret;
    }

    if(req.path.path != '/')
        html+= '<tr><td><a href="../">Parent Directory</a></td><td></td><td>-</td></tr>';
    readdir(req.fsPath).sort().forEach(function(d){
        var st=stat(req.fsPath+'/'+d);
        if (st.isDirectory())
            d+='/';
        html=sprintf('%s<tr><td><a href="%s">%s</a></td><td>%s</td><td>%s</td></tr>',
            html, d, d, st.mtime.toLocaleString() ,hsize(st.size));
    });
    
    html+="</table></body></html>";
    return {html:html};
}


printf("\ntry a url like http://127.0.0.1:8088/showreq.html?var1=val1&color=red&size=15\n");
printf("or see a sample website at http://127.0.0.1:8088/\n\nSTARTING SERVER:\n");

/* ****************************   PART III ***************************************** 
    Configuration and Start of Webserver:

    By default, the server is run in a single thread.  If useThreads is set to true,
    the server will start with one thread per cpu core, or with the number configured 

    Each thread has its own JS interpreter/stack/heap.
    Global variables and ECMA functions from the main thread will be copied
    to all of the server threads.  Modules loaded before server starts
    will be available in each thread as well. Non global variables
    that are in scope will not be copied.

    Functions are copied as compiled bytecode. Not all functionality will 
    transfer.  See the Bytecode limitations section here: 
    https://github.com/svaarala/duktape/blob/master/doc/bytecode.rst

    Best practice for full and expected functionality and flexibility is to 
    define callbacks using {module: function} or {modulePath: path}

    All limitations apply regardless of whether the server is single or multi-threaded.
*/

var pid=server.start(
{
    /* bind: string|[array,of,strings]
       default: [ "[::1]:8088", "127.0.0.1:8088" ] 
        ipv6 format: [2001:db8::1111:2222]:80
        ipv4 format: 127.0.0.1:80
        spaces are ignored (i.e. " [ 2001:db8::1111:2222 ] : 80" is ok)
    */
    bind: [ "[::]:8088", "0.0.0.0:8088" ], /* bind to all */

    /* if started as root, you must set user.  
       If not root, option "user" is ignored. */
    //user: "nobody",

    scriptTimeout: 10.0, /* max time to spend in JS */
    connectTimeout:20.0, /* how long to wait before client sends a req or server can send a response */

    /*** logging ***/
    log: true,           //turn logging on, by default goes to stdout/stderr
    //accessLog: "./access.log",    //access log location, instead of stdout. Must be set if daemon==true
    //errorLog: "./error.log",     //error log location, instead of stderr. Must be set if daemon==true
    
    /*  fork and continue after server start (see end of the script) */
    //daemon: true, // fork and run in background. stdin and stderr are closed, so logging must go to a file.

    /* make server multi-threaded. */
    useThreads: true,
    /*  By default, number of threads is set to cpu core count.
        "threads" has no effect unless useThreads is set true.
        The number can be changed here:
    */
    //threads: 8, /* for a 4 core, 8 virtual core hyper-threaded processor. */

    /* 
        for https support, these three are the minimum number of options needed:
    */
//    secure:true,
//    sslKeyFile:  "/etc/letsencrypt/live/mydom.com/privkey.pem",
//    sslCertFile: "/etc/letsencrypt/live/mydom.com/fullchain.pem",

    /* sslMinVersion (ssl3|tls1|tls1.1|tls1.2). "tls1.2" is default*/
    // sslMinVersion: "tls1.2",

    /* a custom 404 page */
    notFoundFunc: function(req){
        return {
            status:404,
            html: '<html><head><title>404 Not Found</title></head><body style="background: url(/img/page-background.png);"><center><h1>Not Found</h1><p>The requested URL '+
                    req.path.path+
            ' was not found on this server.</p><p><img style="width:65%" src="/inigo-not-fount.jpg"></p></center></body></html>'
        }
    },    

    /* if a function is given, directoryFunc will be called each time a url
        which corresponds to a directory is called if there is no index.htm(l)
        present in the directory.  Added to the normal request object
        will be the property (string) "fsPath" (req.fsPath), which can be used
        to create a directory listing.  See function dirlist() above.
        It is substantially equivelant to the built-in server.defaultDirList function.

        If directoryFunc is not set, a url pointing to a directory without an index.htm(l)
        will return a 403 Forbidden error.
    */
    //directoryFunc: dirlist,
    directoryFunc: server.defaultDirList,

    /* **********************************************************
       map urls to functions or paths on the filesystem 
       If it ends in a '/' then matches everything in that path
       except a more specific ('/something.html') path
       ********************************************************** */
    map:
    {
        /*
            filesystem mappings are always folders. Thus:
               "/tetris"    becomes  "/tetris/
               "./mPurpose" becomes  "./mPurpose/"
        */
        "/":                  "./mPurpose",
        "/tetris":            "./tetris-tutorial/",

        /* url to function mappings */
        "/dbtest.html":       inserttest_callback,
        "/simpledbtest.html": simple_callback,
        "/globalref.html":    globalRef_callback,
        "/ramistest" :        ramistest,

        /* 
            matching a glob to a function:
              http://localhost:8088/showreq/ and
              http://localhost:8088/showreq/show.html and
              http://localhost:8088/showreq.html 
            all match this function
        
        */
        /* 
            This example also uses the function from 
             the module "servermod.js". Changes to a 
             {module: function} or files in {modulePath: path}
             while the server is running do not require a 
             server restart, and thus should be the preferred
             method of url-to-function mapping.
        */
        "/showreq*":          {module:"servermod"},

        /* this also works. However you lose the ability to update 
            the module while the server is running              */
        //"/showreq*":          require("servermod.js"),

        /* 
            Load modules dynamically from a path.
            File additions, deletions and changes do not 
             require a server restart.
        */
        "/modtest/":	      {modulePath:"./servermods/"}
    }
});

/* 
    If daemon==true then server.start() returns the pid of the detached 
    process and begins immediately. 

    Otherwise server.start() returns the pid of the current process and 
    the server functions are processed from within the duktape main 
    event loop.  The main event loop starts at the end of the script.
*/

/* if daemon==true/forking, give the forked server a chance to print its info*/
//rampart.utils.sleep(0.2);

/* if daemon==true  - check that forked daemon is running */
if(rampart.utils.kill(pid,0))
    console.log("pid of rampart-server: " + pid);
else
    console.log("server start failed, pid = "+pid);
