/* load the http server module */
var server = require("./rpserver");

/* sql db is built in.  Call with new Sql("/path/to/db") */
var sql = new Sql(
  "./testdb",
  true
); /* true means make db if it doesn't exist */

/* check if our quicktest table exists.  If not, make it */
var res = sql.exec("select * from SYSTABLES where NAME='quicktest'");
if (res.length == 0) {
  res = sql.exec("create table quicktest ( I int, Text varchar(16) );");
  sql.exec("insert into quicktest values(2,'just a test');");
  sql.exec("create index quicktest_I_x on quicktest(I);");
}

/* same for inserttest table.  And populate the table with 2000 rows */
res = sql.exec("select * from SYSTABLES where NAME='inserttest'");
if (res.length == 0) {
  res = sql.exec(
    "create table inserttest ( I int, D double, Text varchar(16) );"
  );
  for (var i = 0; i < 200; i++) inserttest_callback({}, true);

  /* test delete of 10 rows */
  console.log("delete 10");
  res = sql.exec("delete from inserttest", { max: 10 });
  console.log(res);

  console.log("count should be 1990");
  res = sql.exec("select count(*) from inserttest", { max: 10 });
  console.log(res);
}

/*
sample calls to sql with/without callback
with callback returns one row at a time, and can be cancelled by returning false
without callback returns an array of rows.
Rows are returned as one of 4 different return types:
    default -- object or array of objects {col1name:val1,col2name:val2...
    array   -- [val1, val2, ..]
    arrayh  -- first row is column names, then like array
    novars  -- returns empty array, or empty object if using a callback
*/
/*
console.log("return normal");
res=sql.exec("select * from inserttest",
                {max:10}
);
console.log("no callback results:");
console.log(res);

console.log("return arrayh");
res=sql.exec("select * from inserttest",
                {max:10,returnType:"arrayh"}
);
console.log("no callback results:");
console.log(res);

console.log("return novars");
res=sql.exec("select * from inserttest",
                {max:10,returnType:"novars"}
);
console.log("no callback results:");
console.log(res);

console.log("return callback novars");
res=sql.exec("select * from inserttest",
                {max:10,returnType:"novars"},
                function(res,i){
                    console.log(res,i);
                }

);
console.log("total: "+res);

console.log("return callback arrayh");
res=sql.exec("select * from inserttest",
                {max:10,returnType:"arrayh"},
                function(res,i){
                    console.log(res,i);
                }

);
console.log("total: "+res);

console.log("return callback normal");
res=sql.exec("select * from inserttest",
                {max:10},
                function(res,i){
                    console.log(res,i);
                }

);
console.log(res);
*/

/* 
   Since the http server is multithreaded, and the javascript interpreter
   is not, each thread must have its own javascript heap where the callback
   will be copied.
   This function is outside the scope of server callback functions
   below and thus cannot be reached from within the webserver.
   Think of every function as its own distinct xyz.js file
   for the http server.
*/
function rst() {
  return "return some text";
}

function inserttest_callback(req, allinserts) {
  /* randpic function is naturally accessible to 
       inserttest_callback()
    */
  function randpic(arr, x) {
    if (x === undefined) x = Math.floor(Math.random() * arr.length);
    else x = Math.floor(x * arr.length);

    return arr[x];
  }

  var sql = new Sql("./testdb");
  var str = [
    "zero",
    "one",
    "two",
    "three",
    "four",
    "five",
    "six",
    "seven",
    "eight",
    "nine",
  ];
  var arr;
  var r = Math.random();
  var rf = r * 10;
  var rp = Math.floor(r * 100);
  var ri = Math.floor(r * 10);
  var skip = Math.floor(Math.random() * 100);
  var insertmax = 25;

  /* to populate the table */
  if (allinserts) insertmax = 100;

  if (rp < insertmax) {
    for (var i = 0; i < 10; i++) {
      arr = sql.exec("insert into inserttest values (?,?,?)", [
        ri,
        rf,
        randpic(str, r),
      ]);
    }
    //console.log("insert");
  } else if (rp < 50) {
    arr = sql.exec(
      "delete from inserttest",
      /* WTF: skipped rows are deleted too 
            {skip:skip,max:10,returnType:"novars"}
            */
      /* 
               normally deleted rows are also
               selected and returned.  Here we
               don't need them
            */
      { max: 10, returnType: "novars" }
    );
    //console.log("delete")
    //console.log(arr);
  } else {
    arr = sql.exec(
      "select * from inserttest where I > ?",
      [5],
      { skip: skip, max: 10, returnType: "array" },
      /* sanity check callback */
      function (req) {
        var f2i = parseInt(req[1]);
        if (f2i != req[0] || req[2] != str[f2i])
          console.log("DANGER WILL ROBINSON:", req);
      }
    );
    //console.log("select");
  }

  /* return value is sent to http client
       use file extensions like text (or txt), html, jpg, etc as key to set
       the proper mime-type.  Most extensions are mapped to the correct mime type
       See mime.h for complete listing.
    */

  return {
    text:
      "this is for doing multithreaded stress testing, such as: ab -n 10000 -c 100 http://127.0.0.1:8088/dbtest.html",
  };
}

function simple_callback(req) {
  var sql = new Sql("./testdb");
  var arr = sql.exec("select * from quicktest", { max: 1 });

  /* default mime type is text/plain, if just given a string */
  return JSON.stringify(arr);
}
var x = { msg: "HELLO WORLD", func: showreq_callback };

function showreq_callback(req) {
  // http://jsfiddle.net/KJQ9K/554/
  function syntaxHighlight(json) {
    json = json
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
    return json.replace(
      /("(\\u[a-zA-Z0-9]{4}|\\[^u]|[^\\"])*"(\s*:)?|\b(true|false|null)\b|-?\d+(?:\.\d*)?(?:[eE][+\-]?\d+)?)/g,
      function (match) {
        var cls = "number";
        if (/^"/.test(match)) {
          if (/:$/.test(match)) {
            cls = "key";
          } else {
            cls = "string";
          }
        } else if (/true|false/.test(match)) {
          cls = "boolean";
        } else if (/null/.test(match)) {
          cls = "null";
        }
        return '<span class="' + cls + '">' + match + "</span>";
      }
    );
  }
  //For testing timeout:this takes about 5 sec on a semi modern intel i7 3930k.
  //    for (var i=0;i<10000000;i++);
  //    console.log("DONE WASTING TIME IN JS");
  var str = JSON.stringify(req, null, 4);
  var css =
    "pre {outline: 1px solid #ccc; padding: 5px; margin: 5px; }\n" +
    ".string { color: green; }\n" +
    ".number { color: darkorange; }\n" +
    ".boolean { color: blue; }\n" +
    ".null { color: magenta; }\n" +
    ".key { color: red; }\n";

  /* you can set custom headers as well */
  return {
    headers: {
      "X-Custom-Header": 1,
    },
    //only set one of these.  Setting more than one throws error.
    //jpg: "@/home/user/myimage.jpg"
    html:
      "<html><head><style>" +
      css +
      "</style><body>Object sent to this function:<br><pre>" +
      syntaxHighlight(str) +
      "</pre>" +
      x.msg +
      "</body></html>",
    //,text: "\\@home network is a now defunct cable broadband provider."
  };
}
/* this will no longer print out an error since rst() is global
 
   If this entire script was wrapped in a function and that function was called,
   this would produce an error since rst() would no longer be global.
   Best practice currently is to put functions you need inside the callback
   function and treat each callback as if it were its own script, and
   as if it is being executed and exits after each webpage is served.
*/

function badRef_callback(req) {
  return x.func(req);
}

function ramistest(req) {
  var ra = new Ramis();
  var insertvar = "0123456789abcdef";
  var rno = Math.floor(Math.random() * 1000000);

  for (var i = 0; i < 333; i++) {
    resp = ra.exec("SET key%d%d %b", rno, i, insertvar, insertvar.length);
    if (resp[0] != "OK") {
      console.log("get error. got" + resp[0]);
      return "GET error";
    }
  }

  for (var i = 0; i < 333; i++) {
    resp = ra.exec("GET key%d%d", rno, i);
    if (resp[0].length != 16) {
      console.log("set error. got" + resp[0].length);
      return "SET error";
    }
  }

  for (var i = 0; i < 333; i++) {
    resp = ra.exec("DEL key%d%d", rno, i);
    if (resp[0] != "1") {
      console.log("del error. got " + resp[0]);
      return "DEL error";
    }
  }

  return "OK";
}

console.log(
  "try a url like http://127.0.0.1:8088/showreq.html?var1=val1&color=red&size=15"
);
console.log("or see a sample website at http://127.0.0.1:8088/");
console.log("");

/* 
    Configuration and Start of Webserver:
    server.start() never returns and no code after this will be run

    It will create one thread per cpu core, or with the number configured 
    each with its own JS interpreter.
    Global variables and ECMA functions from the main thread will be copied
    to all of the server threads.

    Functions are copied as compiled bytecode. Not all functionality will 
    transfer.  See the Bytecode limitations section here: 
    https://github.com/svaarala/duktape/blob/master/doc/bytecode.rst
*/

server.start({
  ip: "0.0.0.0", //this binds to all. Default is 127.0.0.1
  ipv6: "::", //this binds to all. Default is ::1
  port: 8088, //this is the default
  ipv6port: 8088, //defaults to port above if not set
  scriptTimeout: 2.0,
  connectTimeout: 20.0,
  /* for https support, this is the minimum (more options to come): */
  /*

    secure:true,
    sslkeyfile:  "/etc/letsencrypt/live/mydom.com/privkey.pem",
    sslcertfile: "/etc/letsencrypt/live/mydom.com/fullchain.pem",
    
    // if files above are invalid, it will silently revert to http
    */

  /*  By default server binds to both ipv4 and ipv6.
        To turn off ipv6 or ipv4, use one of these only */
  // useipv6: false,
  // useipv4: false,

  /*  By default, number of threads is set to cpu core count.
        ipv6 and ipv4 are separate servers and each get this number of threads.
        The number can be changed here:
    */
  //threads: 4,

  /* map urls to functions or paths on the filesystem */
  /* order by specificity.  '/' should always be last  */
  map: {
    /* url to function mappings */
    "/dbtest.html": inserttest_callback,
    "/simpledbtest.html": simple_callback,
    "/showreq*": showreq_callback,
    "/badref.html": badRef_callback,
    "/ramistest": ramistest,
    //"/br":badRef_callback,
    /* filesystem mappings are always folders.  "/tetris" => "/tetris/ */
    "/tetris": "./tetris-tutorial/",
    //"/docs/" :          "/usr/share/doc/libevhtp-doc/html/",
    "/": "./mPurpose/",
  },
  /* 
        including a function here will match everything not matched above
        matching "/" if "/" : "./mPurpose/" was not present
     */
  /* ,function(){} */
});
