rampart.globalize(rampart.utils);
var python = require('rampart-python');
var Sql = require('rampart-sql');
var sql = new Sql.init("./pytest-sql", true);
var crypto = require('rampart-crypto');
var dbfile="./test.db";

try{
    sql.exec("drop table test1;");
    sql.exec("drop table test2;");
}catch(e){}

function testFeature(name,test,error)
{
    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }
    //printf("testing(%d) %-58s - ", thread.getId(), name);
    printf("testing %-60s - ", name);
    if(test)
        printf("passed\n")
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        if(error) printf('%J\n',error);
        process.exit(1);
    }
    if(error) printf('%J\n',error);
}

//var pip=python.import('pip');
//var res=pip.main({pyType:'list', value:['install', 'Pillow']});
//console.log(res.toString());
function get_cursor(dbfile) {
    var pysql = python.import('sqlite3');
    var connection = pysql.connect(dbfile);
    var cursor = connection.cursor();
    //functions are automatically registered, but variables are not
    cursor.connection=connection;
    return cursor;
}

function make_sqlite_db (){

    try{rmFile(dbfile);}catch(e){}
    var cursor = get_cursor(dbfile);

    cursor.execute("create table IF NOT EXISTS test(i int, i2 int);");
    cursor.execute("SELECT name FROM sqlite_master WHERE type='table' and name='test'");
    var res = cursor.fetchmany({pyType:'integer', value:1});
    res=res.toValue();
    return res[0][0] == "test";
}

function sqlite_insert() {

    var cursor = get_cursor(dbfile);
    var itotal=0;
    for (var i=0; i<100; i+=4) {
        cursor.execute("insert into test values(?,?)", [i,   i+1]);
        cursor.execute("insert into test values(?,?)", [i+2, i+3]);
        itotal += 4*i + 6;
    }

    cursor.execute("select * from test");
    res = cursor.fetchall().toValue();
    var total=0;
    for (i=0;i<res.length;i++) {
        total+= res[i][0]+res[i][1];
    }
    cursor.connection.commit();
    return total == itotal;
}



var thr=new rampart.thread();

function tests(inthr){

    testFeature(`python - ${inthr}import pathlib and resolve './'`, function(){
        var pathlib = python.import('pathlib');
        var p=pathlib.PosixPath('./');
        return p.resolve().toValue() == getcwd();
    });

    testFeature(`python - ${inthr}import hashlib and sha256`, function(){

        var hash = python.import('hashlib');

        var m = hash.sha256();
        m.update(stringToBuffer("hello"));
        var res = m.hexdigest();
        return res.toValue() == crypto.sha256('hello');
    });

    var iscript = 
`def retself(s):
    return s

def evalstr(s):
    return eval(s)

x=5.5
`   ;

    testFeature(`python - ${inthr}importString - funcs and eval - valueOf()`, function(){
        var r=python.importString(iscript);

        r=python.importString("y=6.6");

        var x = r.evalstr("4.2 * x * y");

        return x == 152.46 && r.retself("yo") == "yo" ;
    });

    testFeature(`python - ${inthr}importFile - funcs and eval - valueOf()`, function(){
        fprintf("./tmp.py", "%s", iscript);

        var r=python.importFile("./tmp.py");

        r=python.importString("y=6.6");

        var x = r.evalstr("4.2 * x * y");

        return x == 152.46 && r.retself("yo") == "yo" ;
    });

    testFeature( `python - ${inthr}import sqlite3, create table`, function(){
        return make_sqlite_db();
    });

    testFeature(`python - ${inthr}insert and read from sqlite3 table`, function(){
        return sqlite_insert();
    });

    var scr=`
x=20

def pyEval(code):
    return eval(code)

def echo(val):
    print(val)
    return(val)

echo.y="test"

def retecho():
    return echo

def addone(x):
    x['z'] = x['z'] + 1.0
    return x

def retdict(z):
    d = {'z':z}
    return d
    `;

    var ps = python.importString(scr);
    var echo = ps.retecho();

    testFeature(`python - ${inthr}toString() of module function`, function(){
        return (ps.echo.toString().indexOf("function echo") != -1);
    });

    testFeature(`python - ${inthr}proxy lookup in dictionary`, function(){
        var rd = ps.retdict({a:"b"});
        return ("b" == rd.z.a.toValue());    
    });

    testFeature(`python - ${inthr}dictionary get() method from JS`, function(){
        var rd = ps.retdict(1);
        return(1 == rd.get('z').toValue());
    });

    /* we don't need echo.y.toValue() here because JS does that automatically with comparisons (valueOf is same as toValue)*/
    testFeature(`python - ${inthr}get attribute of a returned function`, function(){
        return echo.y=="test"
    });

    /* because we cannot set a proxy on a function like we can on an object. */
    testFeature(`python - ${inthr}get attribute of a function`, function(){
        return ps.echo.y == 'test';
    });

    testFeature(`python - ${inthr}call method on string`, function(){
        return echo.y.capitalize()=="Test"
    });


    testFeature(`python - ${inthr}get undefined for non-existent attributes`, function(){
        return echo.notfound === undefined;
    });

    testFeature(`python - ${inthr}get undefined for non-existent items`, function(){
        var rd = ps.retdict(1);
        return rd.x === undefined;
    });


}


tests("");

thr.exec(function(){
    tests("in thread ");
    rampart.thread.put("testdone",true);
});

function copy_to_texis(tbname)
{
    var cursor = get_cursor(dbfile);
    sql.exec(`create table ${tbname} (i int, i2 int);`);    
    cursor.execute("select * from test");
    res = cursor.fetchall()
    res = res.toValue();
    if(res.length != 50)
        testFeature("python - copy from sqlite to texis tables in two threads", false, `got ${res.length} results`);
    for (i=0;i<res.length;i++) {
        sql.exec(`insert into ${tbname} values(?,?);`,res[i]);
    }
}

while(!rampart.thread.get("testdone")) sleep(0.1);

var thr1 = new rampart.thread();
var thr2 = new rampart.thread();

thr1.exec( function(){
    copy_to_texis("test1");
    rampart.thread.put("done", true);
});

thr2.exec( function(){
    copy_to_texis("test2");
    while (!rampart.thread.get("done")) {
        sleep(0.1);
    }
    //the two texis table files should be identical
    var t1sum = crypto.sha1(readFile("./pytest-sql/test1.tbl"));    
    var t2sum = crypto.sha1(readFile("./pytest-sql/test2.tbl"));    

    testFeature("python - copy from sqlite to texis tables in two threads", t1sum==t2sum);
});


thr.exec(function(){
    rampart.event.on("myev", "myfunc", function(uv,tv) {
        testFeature("python - import rampart, rampart.trigger, rampart.call",
            (tv[0]==123 && tv[1]==456) );
        rampart.event.remove("myev");
    },"uservar");
});


function testfunc(a,b) {
    return [a,b];
}

thr2.exec(function(){

    var pyscript=`
import rampart

def docall(a,b,c):
    return rampart.call(a,b,c)

def trigger():
    x=docall("testfunc",123,456)
    rampart.triggerEvent("myev",x)

`   ;

    var mymod = python.importString(pyscript);
    mymod.trigger();
});

