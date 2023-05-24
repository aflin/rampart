rampart.globalize(rampart.utils);

var thread = rampart.thread;

var nruns=4;

var Sql=require('rampart-sql');
var sql=new Sql.init(process.scriptPath+'/testdb',true);

var Lmdb=require('rampart-lmdb');
var lmdb;
var curl   = require('rampart-curl');
var server = require('rampart-server');
var pid;

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
        pid = thread.get("pid");
        if(pid) kill(pid);
        process.exit(1);
    }
    if(error) printf('%J\n',error);
}

testFeature("thread - Open test lmdb database env in main thread", function() {
    lmdb= new Lmdb.init(
        process.scriptPath+"/lmdb-test-db",
        true, /* true means create the database if it doesn't exist */
        {noMetaSync:true, noSync:true, mapSize: 160}
    );
    return true;
});

var dbi;

testFeature("thread - Open lmdb database in main thread", function() {
    dbi = lmdb.openDb(null,true); //open default db
    return true;
});

var mthr=[];
var niv=0;

var total=0;

function global_do_in_thread(arg){
    var x=arg.x;
    while (x<1000000) x++;
    res=sql.exec("select * from SYSTABLES");
    return {x:x, i:arg.i};
}

function global_report_result(res){
    total++;
    mthr[res.i].close();    
    var iv = setInterval(function(){
        var ret=true;
        niv++;
        if(total == nruns){
            total++; // only once
            clearInterval(iv);
            var ps = shell(`ps ax -o ppid,command | grep rampart | grep [s]ql_helper | grep ${process.getpid()}`);
            if(ps.stdout.length) {
                error="Not all sql_helpers were terminated:\n"+ps.stdout;
                ret=false;
            }
            testFeature("thread - multiple threads with sql forks", ret );
        } else if(total>nruns) {
            clearInterval(iv);
        }
        if(niv > 100) {
            clearInterval(iv);
            testFeature("thread - multiple threads with sql forks", false);
        }

    }, 100);
}

var thr = new thread();

thr.exec(
    function(arg){
        return `--${arg}--`;
    },
    'I am an argument',
    function(retval){
        testFeature("thread - inline function", retval=='--I am an argument--');
        thr.close();

        testFeature("thread - reopen fail after close", function(){
            ret=false;
            try {
                thr.exec(function(){return 1});
            } catch(e){
                ret=true;
            }
            return ret
        });

    }    
);


var thr1 = new thread();

thr1.exec(
    function(){
        var thr2 = new thread();
        // new thread from this one
        thr2.exec(
            function(arg){
                return `--${arg}--`;
            },
            'I am an argument',
            function(retval){
                testFeature("thread - thread inside of thread", retval=='--I am an argument--');
                thr2.close();
            }    
        );
    }
);

for (var i=0; i<nruns; i++){
    mthr[i]=new thread();
    mthr[i].exec(global_do_in_thread, {x:0,i:i}, global_report_result);
}

function global_thread_lmdb(arg){
    var x=arg.x;

    var txn = new lmdb.transaction(dbi, true, true);
    
    if(!arg.i){
        var j=0;
        var iv=setInterval(function(){
            j++;
            //if(j>20) {
            if(thread.get("DONE") && thread.get("LMDBDONE")) {
                testFeature("thread - wait for exit while events pending", true);
                clearInterval(iv);
            }
        },50);
    }
    
    // some writes
    for (i=0;i<10000;i++)
    {
        kv = sprintf("%d", Math.random()*100000);
        txn.put(dbi, kv, kv);   
    }
    txn.commit();

    // some reads
    txn = new lmdb.transaction(dbi, false);
    //var j=0;
    for (i=0;i<10000;i++)
    {
        kv = sprintf("%d", Math.random()*100000);
        var kv2=txn.get(kv,true);
        if(kv2) {
            if(kv2 != kv){
                return {error: "failed"};
            }
        }
        //else j++;
    }

    txn.commit();

    var ret={x:x, i:arg.i};
    return ret;
}

var total2=0;
var goterr=0;

function global_report_lmdb_result(res){
    total2++;

    if(res.error){
        testFeature("thread - multiple threads in thread with lmdb transactions", false, res.error);
    }

    mthr[res.i].close();    

    if(goterr)
        return;

    if(res.error) {
        goterr=1;
        error=res.error;
        testFeature("thread - multiple threads in thread with lmdb transactions", false);
    }

    if(!res.i) {
        var iv = setInterval(function(){
            var ret=true;
            niv++;
            if(total2 == nruns){
                total2++; // only once
                clearInterval(iv);
                testFeature("thread - multiple threads in thread with lmdb transactions", true );
                thread.put("LMDBDONE", true);
                return;
            } else if(total2>nruns) { //we've got it already
                clearInterval(iv);
                return;
            }
            if(niv > 50) {
                clearInterval(iv);
                //error=`we only got ${total2} runs`;
                console.log(`we only got ${total2} runs`);
                testFeature("thread - multiple threads in thread with lmdb transactions", false);
            }

        }, 50);
    }
}

function thr_in_thr_lmdb() {
    for (var i=0; i<nruns; i++){
        mthr[i]=new thread();
        mthr[i].exec(global_thread_lmdb, {x:0,i:i,_report:1},global_report_lmdb_result);
    }
}

var thr3 = new thread();
thr3.exec(thr_in_thr_lmdb);

// thr5 will be copied to the js stack of thr4
var thr5 = new thread();
var thr4 = new thread();

thr4.exec(
    function(){ 
        thr5.exec(
            function(arg){
                return arg;
            },
            "test",
            function(res){
                // we never get here
                testFeature("thread - thread (created in main thread) inside of thread -", 
                    (res=='test')
                );
            }
        );
    },
    function(res) {
        testFeature("thread - test 'Cannot run a thread created outside ...'",
            res.error.indexOf('run a thread created') != -1
        );
    }
);

thr4.exec(function(){
    thread.put("myvar", "Hi");
    thread.put("myfunc", testFeature);
});    

thr5.exec(function(){
    var ret;
    var tf;
    do {
        // give it a little time to finish copying
        sleep(0.1);
        ret = thread.get("myvar");
        tf  = thread.get("myfunc");
    } while (!ret || !tf);

    tf("thread - sharing varibles and funcs between threads", ret=="Hi");
});


var thr7;

function server_thread_test(req) {
    // although defined in main thread, thr7 var is copied
    // and each server thread will maintain its own thr7.
    // However, thr7 must be created inside the current server thread 
    if(!thr7) thr7 = new rampart.thread();
    thr7.exec( 

        //thread func
        function(myvar) {
            return myvar;
        },

        //var sent to thread func
        req.params.myvar,

        //callback in current thread with return from thread func
        function(res) {
            req.reply({txt:res});
        }

    );

    return {defer:true}    
}

thr5.exec(function(){

    global.pid=server.start(
    {
        bind: "127.0.0.1:8084",
        developerMode: true,
        /* only applies if starting as root */
        user: "nobody",

        scriptTimeout: 1.0, /* max time to spend in JS */
        connectTimeout:20.0, /* how long to wait before client sends a req or server can send a response */
        useThreads: true, /* make server multi-threaded. */
        //threads: 1,
        daemon: true,
        log: true,
        accessLog: './alog',
        errorLog:  './elog',
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
            "/threadtest.txt": server_thread_test
        }
    });
    /* make sure we are up before sending pid.
    var x=0;
    while(x<10 && !rampart.utils.kill(pid,0)) {
        x++;
        sleep(0.1);
    }*/
    thread.put("server_forked", true);
});


thr5.exec(function(){
    if(!pid)
        testFeature("thread - server forked from thread is running",false, "pid was not set");
    else
        testFeature("thread - server forked from thread is running", rampart.utils.kill(pid,0) );
});

thr5.exec(function(){
    testFeature("thread - server with thread and defer", function(){
        var res=curl.fetch("http://localhost:8084/threadtest.txt?myvar=123abc");
        if (res.text == '123abc')
            return true;
        else {
            console.log(res.errMsg);
            return false;
        }    
        //return res.text == '123abc';
    })

    if(pid) {
        kill(pid);
        pid=false;
    }
},
{threadDelay: 500}
);


//before using locks, server should be forked.
while(!thread.get("server_forked")) sleep(0.1);

// must make locks first or they will not be copied to threads
var lock1 = new rampart.lock();
var lock2 = new rampart.lock();
var lock3 = new rampart.lock();

var lthr1 = new thread();
var lthr2 = new thread();

lthr1.exec(function(){
    var val1, val2, val3;

    //wait for thread 2 to get lock3
    while(lock3.getLockingThread()==-1)
        sleep(0.1);

    lock1.lock();
    lock2.lock();

    val1=thread.get("val"); // undefined

    lock1.unlock();
    lock3.lock()
    val2=thread.get("val"); // 1
    lock3.unlock();
    sleep(0.02); //give lock2.trylock some fails
    lock2.unlock();
    sleep(0.1);
    val3=thread.get("val"); // many

    testFeature("thread - user locking in threads", (!val1 && val2==1 && val3>50) )
    thread.put("DONE", true);
    //console.log(val1, val2, val3);
});

lthr2.exec(function(){
    var x=0;

    lock3.lock();

    //wait for thread 1 to get locks
    while(lock2.getLockingThread()==-1)
        sleep(0.1);


    lock1.lock();
    thread.put('val',1);
    lock1.unlock();
    lock3.unlock();

    while(!lock2.trylock())
        x++;

    thread.put('val',x);

    lock2.unlock();
});

