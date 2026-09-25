rampart.globalize(rampart.utils);
var python = require('rampart-python');
var Sql = require('rampart-sql');
var crypto = require('rampart-crypto');

var tmpdir = process.scriptPath + '/tmp-test';
if (!stat(tmpdir)) mkdir(tmpdir);

var sql = new Sql.init(tmpdir + "/pytest-sql", true);
var dbfile = tmpdir + "/test.db";

function py_cleanup() {
    try { rmFile(dbfile); } catch(e) {}
    try { rmFile(tmpdir + "/tmp.py"); } catch(e) {}
    shell("rm -rf " + tmpdir + "/pytest-sql");
    try { rmdir(tmpdir); } catch(e) {}
}

try{
    sql.exec("drop table test1;");
    sql.exec("drop table test2;");
}catch(e){}

var testFeature = new (require('./test-feature.js'))({
    prefix: "python",
    onFail: function() { py_cleanup(); process.exit(1); }
});

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

// global: called from python via rampart.call
function pyt_boom(x) { throw new Error("pyt boom " + x); }
function pyt_double(x) { return x * 2; }
function pyt_give_third() { return globalThis.pyt_third; }   // set by the test
function pyt_inner(i) { return i * 10; }
function pyt_outer(i) {   // calls python, whose pool calls pyt_inner
    var r = globalThis.pyt_nest_mod.pool('pyt_inner', 3).toValue();
    return i * 1000 + r[0] + r[1] + r[2];
}

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

    // _ctypes must load on any distro (libffi is statically linked)
    testFeature(`python - ${inthr}import ctypes and call libc strlen`, function(){
        var m = python.importString(
            "import ctypes\ndef slen(s):\n    return ctypes.CDLL(None).strlen(s.encode())\n");
        return m.slen("hello").toValue() == 5;
    });

    // dunder methods, obj['key'] on non-dicts, and tuple/list element refcounts
    testFeature(`python - ${inthr}__getitem__/__len__ and subscripts on objects`, function(){
        var m = python.importString(
            "class C:\n    def __getitem__(self, k): return 'item:' + str(k)\n    def __len__(self): return 7\n" +
            "def make(): return C()\ndef tup(): return ('a' * 50, 'b' * 50)\n");
        var c = m.make();
        if (c.__getitem__('x').toValue() != 'item:x' || c.__len__().toValue() != 7 ||
            c['y'].toValue() != 'item:y' || c[3].toValue() != 'item:3')
            return false;
        var tp = m.tup();
        for (var i = 0; i < 1000; i++) { var e = tp[i % 2]; e = null; if (i % 100 == 0) Duktape.gc(); }
        Duktape.gc();
        return tp[0].toValue() + tp[1].toValue() == 'a'.repeat(50) + 'b'.repeat(50);
    });

    // rampart.call: JS errors, concurrent callers, late background thread
    testFeature(`python - ${inthr}rampart.call throws, concurrency, background`, function(){
        var m = python.importString(
            "import rampart, threading, time\nfrom concurrent.futures import ThreadPoolExecutor\n" +
            "def call(f, x):\n    try:\n        return ['ok', rampart.call(f, x)]\n    except Exception as e:\n        return ['raised', str(e)]\n" +
            "def many(f, n):\n    with ThreadPoolExecutor(8) as ex:\n        return list(ex.map(lambda i: call(f, i), range(int(n))))\n" +
            "bg = {}\n" +
            "def start_bg(f):\n    def run():\n        time.sleep(0.1)\n        bg['r'] = call(f, 'late')\n    bg['t'] = threading.Thread(target=run)\n    bg['t'].start()\n" +
            "def bg_result():\n    bg['t'].join()\n    return bg['r']\n");
        var r = m.call('pyt_boom', 1).toValue();
        if (r[0] != 'raised' || !/pyt boom 1/.test(r[1])) return false;
        var many = m.many('pyt_double', 40).toValue();
        for (var i = 0; i < 40; i++) if (many[i][0] != 'ok' || many[i][1] != i * 2) return false;
        m.start_bg('pyt_double');
        var until = Date.now() + 300; while (Date.now() < until) {}
        r = m.bg_result().toValue();
        return r[0] == 'raised' && /not waiting on python/.test(r[1]);
    });

    // nested python objects, zero-arg rampart.call, JS returning a python object
    testFeature(`python - ${inthr}nested python objects and zero-arg rampart.call`, function(){
        var m = python.importString(
            "import fractions, rampart\ndef mk(n): return fractions.Fraction(1, int(n))\n" +
            "def total(*a, items=()): return str(sum(a, fractions.Fraction(0)) + sum(items, fractions.Fraction(0)))\n" +
            "def deep(d): return str(d['a'][0] + d['a'][1]['b'])\n" +
            "def via_js(): return str(rampart.call('pyt_give_third') + 0)\n");
        var a = m.mk(2), b = m.mk(3);
        globalThis.pyt_third = b;
        for (var i = 0; i < 500; i++) { m.total(a, b); if (i % 100 == 0) Duktape.gc(); }
        return m.total(a, b, {pyArgs:{items:[a, b]}}).toValue() == '5/3' &&
               m.deep({a:[a, {b:b}]}).toValue() == '5/6' &&
               m.via_js().toValue() == '1/3';
    });

    // callable python objects: subscripts, calls, method attributes (df.loc['x'])
    testFeature(`python - ${inthr}callable objects: subscript, call, method attrs`, function(){
        var m = python.importString(
            "class Ix:\n    def __call__(self, x): return 'called:' + str(x)\n    def __getitem__(self, k): return 'row:' + str(k)\n" +
            "class F:\n    def __init__(self): self.loc = Ix()\n    def describe(self): return 'd'\n" +
            "def make(): return F()\n");
        var f = m.make();
        return typeof f.loc == 'function' && f.loc['mean'].toValue() == 'row:mean' &&
               f.loc[2].toValue() == 'row:2' && f.loc('x').toValue() == 'called:x' &&
               f.describe.__name__.toValue() == 'describe' && f.describe().toValue() == 'd' &&
               m.make.__name__.toValue() == 'make';
    });

    // lazy lookup: no reads on wrap, fresh values, python names win, cached callables
    testFeature(`python - ${inthr}lazy attribute lookup semantics`, function(){
        var m = python.importString(
            "hits = [0]\n" +
            "class C:\n    def __init__(self): self.v = 1\n" +
            "    @property\n    def watched(self):\n        hits[0] += 1\n        return hits[0]\n" +
            "    def bump(self): self.v += 1\n" +
            "class F:\n    name = 'pyname'\n    def __call__(self): return 'called'\n    def apply(self, x): return 'py-apply:' + str(x)\n" +
            "def make(): return C()\ndef makef(): return F()\ndef nhits(): return hits[0]\n");
        var c = m.make(), f = m.makef();
        if (m.nhits().toValue() !== 0) return false;              // wrapping read nothing
        if (c.watched.toValue() !== 1 || m.nhits().toValue() !== 1) return false;
        c.bump(); c.bump();
        if (c.v.toValue() !== 3) return false;                    // fresh value
        if (c.bump !== c.bump) return false;                      // cached callable
        return f() .toValue() == 'called' && f.apply('x').toValue() == 'py-apply:x' &&
               f.name.toValue() == 'pyname' && typeof f.call == 'function';
    });

    // nested callbacks through python thread pools (used to deadlock)
    testFeature(`python - ${inthr}nested rampart.call with thread pools`, function(){
        var m = python.importString(
            "import rampart\nfrom concurrent.futures import ThreadPoolExecutor\n" +
            "def pool(f, n):\n    with ThreadPoolExecutor(4) as ex:\n        return list(ex.map(lambda i: rampart.call(f, i), range(int(n))))\n");
        globalThis.pyt_nest_mod = m;
        var r = m.pool('pyt_outer', 4).toValue();
        return JSON.stringify(r) == JSON.stringify([30, 1030, 2030, 3030]);
    });

    // Python.h is where sysconfig says (triton, C-extension builds)
    testFeature(`python - ${inthr}Python.h at sysconfig include path`, function(){
        var m = python.importString(
            "import os, sysconfig\ndef hdr():\n    return os.path.exists(os.path.join(sysconfig.get_paths()['include'], 'Python.h'))\n");
        return m.hdr().toValue() === true;
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
        fprintf(tmpdir + "/tmp.py", "%s", iscript);

        var r=python.importFile(tmpdir + "/tmp.py");

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

    var cscr = `
def concat(*args):
  ret=""
  for arg in args:
    ret+=arg

  return ret

def upper(a):
  return a.upper()

def concatkw(**kwargs):
    ret="";
    for key, value in kwargs.items():
        ret+=value
    return ret
`;
    var ps = python.importString(cscr);

    testFeature(`python - ${inthr}python to python variables`, function(){
        var ps = python.importString(cscr);
        var as = ps.upper("aaaa");
        var bs = ps.upper("bbbb");
        var cs = ps.upper("cccc");
        //as, bs & cs hold reference pointers to python strings
        var res = ps.concat(as,bs,cs);
        return res.toValue() == "AAAABBBBCCCC";
    });

    testFeature(`python - ${inthr}python to python keyword args`, function(){
        var ps = python.importString(cscr);
        var as = ps.upper("aaaa");
        var bs = ps.upper("bbbb");
        var cs = ps.upper("cccc");
        //as, bs & cs hold reference pointers to python strings
        var res = ps.concatkw({ pyArgs:{as:as,bs:bs,cs:cs} });
        return res.toValue() == "AAAABBBBCCCC";
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
    if(res.length != 50) {
        printf("got %d results\n", res.length);
        testFeature("python - copy from sqlite to texis tables in two threads", false);
    }
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
    var t1sum = crypto.sha1(readFile(tmpdir + "/pytest-sql/test1.tbl"));
    var t2sum = crypto.sha1(readFile(tmpdir + "/pytest-sql/test2.tbl"));    

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

    py_cleanup();
    testFeature.exit();
});

