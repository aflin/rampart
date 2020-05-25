#!/usr/local/src/TexisCoreApiTest5b/build/duk/duk
server=require("rpserver");

var sql=new Sql("./testdb",true); /* true means make db if it doesn't exist */

var res=sql.exec("select * from SYSTABLES where NAME='quicktest'");
if(res.length==0) {
    res=sql.exec("create table quicktest ( I int, Text varchar(16) );");
    sql.exec("insert into quicktest values(2,'just a test');");
    sql.exec("create index quicktest_I_x on quicktest(I);");
}

res=sql.exec("select * from SYSTABLES where NAME='dbtest'");
if(res.length==0) {
    res=sql.exec("create table dbtest ( I int, D double, Text varchar(16) );");
    for (var i=0;i<200;i++)
        dbtest_callback({},true);
}

function rst() {
    return("return some text");
}

function dbtest_callback(req,allinserts){
    function randpic(arr){
        x=Math.floor(Math.random()*arr.length);
        return(arr[x]);
    }

    var sql=new Sql('./testdb');
    var str=["red","orange","yellow","teal","green","cyan","blue","purple","violet"]
    var arr;
    var rf=Math.random();
    var rp=Math.floor(rf*100);
    var ri=Math.floor(rf*1000000);
    var insertmax=5;
    
    if(allinserts) insertmax=100;    
    if (rp<insertmax)
    {
        arr=sql.exec(
            'insert into dbtest values (?,?,?)',
            [ri,rf,randpic(str)]
        );
        //console.log("insert "+ri);
    }
    else if (rp<10)
    {
        arr=sql.exec(
            "delete from dbtest where I < ?",
            [50000],
            {skip:rp, max:1,returnType:"array"}
        );
        //console.log("delete "+ri);
    }
    else
    {
        arr=sql.exec(
            "select * from dbtest where I > ?",
            [50000],
            {skip:rp,max:10,returnType:"array"}
        );
//        console.log("select");
    }
    
    return({
        text: "this is for doing stress testing, such as: ab -n 10000 -c 100 http://127.0.0.1:8088/simpledbtest.html"
    });
}

function simple_callback(req){
    var sql=new Sql('./testdb');
    var arr=sql.exec(
        'select * from quicktest',
        {max:1}
    );
    if(sql.lastErr) return(sql.lastErr);
    return(JSON.stringify(arr));
}

function showreq_callback(req){
    // http://jsfiddle.net/KJQ9K/554/
    function syntaxHighlight(json) {
        json = json.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
        return json.replace(/("(\\u[a-zA-Z0-9]{4}|\\[^u]|[^\\"])*"(\s*:)?|\b(true|false|null)\b|-?\d+(?:\.\d*)?(?:[eE][+\-]?\d+)?)/g, function (match) {
            var cls = 'number';
            if (/^"/.test(match)) {
                if (/:$/.test(match)) {
                    cls = 'key';
                } else {
                    cls = 'string';
                }
            } else if (/true|false/.test(match)) {
                cls = 'boolean';
            } else if (/null/.test(match)) {
                cls = 'null';
            }
            return '<span class="' + cls + '">' + match + '</span>';
        });
    }
    var str=JSON.stringify(req,null,4);
    var css=
        "pre {outline: 1px solid #ccc; padding: 5px; margin: 5px; }\n"+
        ".string { color: green; }\n"+
        ".number { color: darkorange; }\n"+
        ".boolean { color: blue; }\n"+
        ".null { color: magenta; }\n"+
        ".key { color: red; }\n";
    
    //var extra=Math.random();
    var extra="";
    return({
        headers:
            {
                "Custom-Header":1
            },
        //jpg: "@/home/user/myimage.jpg"
        html:"<html><head><style>"+css+"</style><body>Object sent to this function:<br><pre>"+syntaxHighlight(str)+"</pre>"+extra+"</body></html>"
        //text: rst()  //DONT DO THIS!!! see badRef()
    });
    
}

function badRef_callback(req){
    return rst;
}

print("try a url like http://127.0.0.1:8088/showreq.html?var1=val1&color=red&size=15");
print("or see a sample website at http://127.0.0.1:8088/");
print("");
server.start(

{
    ip:"0.0.0.0",
    ipv6:"::",
    ipv6port:8088,
    port:8088,
    /* ordered by priority.  '/' should always be last */
    map:
     {
         "/dbtest.html":dbtest_callback,
         "/simpledbtest.html":simple_callback,
         "/showreq*":showreq_callback,
         "/badref.html":badRef_callback,
         "/tetris": "./tetris-tutorial/",
         "/": "./mPurpose/"
     }
});
