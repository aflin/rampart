/* make printf et. al. global */
rampart.globalize(rampart.utils);

/* load the http server module */
var server=require("rampart-server");

/* load curl module */
var curl=require("rampart-curl");

/* load the lmdb module */

var Lmdb = require("rampart-lmdb");

/* 
   normally we'd load Lmdb (at lease we would do "new init()") in a module
   after the server forks.  However here we are testing to make sure lmdb
   behaves correctly after forking and in multi-threaded use.
*/
var lmdb= new Lmdb.init(
    process.scriptPath+"/lmdb-test-db",
    true, /* true means create the database if it doesn't exist */
    {noMetaSync:true, noSync:true, mapSize: 160}
);

var pid; //pid of server, set below

var dbi = lmdb.openDb("",true); //create and open default db

var user = trim(exec("whoami").stdout);

if(user == 'root') {
    var ret = shell(`chown -R nobody ${process.scriptPath}/lmdb-test-db`);
    if(ret.exitStatus)
    {
        printf("chmod error: %s %s\n", ret.stderr, ret.stdout);
        process.exit(ret.exitStatus);
    }
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
    {
        printf(">>>>> FAILED <<<<<\n");
        if(error) console.log(error);
        kill(pid,15);
        process.exit(1);
    }
    if(error) console.log(error);
}

function ltest(req) {
    var kv,i;
    var txn = new lmdb.transaction("", true, true);

    /* some writes */
    for (i=0;i<100;i++)
    {
        kv = sprintf("%d", Math.random()*100000);
        txn.put(dbi, kv, kv);   
    }
    txn.commit();

    /* some reads */
    txn = new lmdb.transaction(dbi, false);
    //var j=0;
    for (i=0;i<10000;i++)
    {
        kv = sprintf("%d", Math.random()*100000);
        var kv2=txn.get(kv,true);
        if(kv2) {
            if(kv2 != kv){
                printf("FAILED! %s = %s\n",kv, kv2 );
                return {text: "failed"};
            }
        }
        //else j++;
    }
    txn.commit();
    //printf("%d of 10,000 hits\n", (10000-j));
    return{
        text:"success",
    }

}



var pid=server.start(
{
    bind: "127.0.0.1:8086",
    developerMode: true,
    /* only applies if starting as root */
    user: "nobody",

    scriptTimeout: 20.0, /* max time to spend in JS */
    connectTimeout:20.0, /* how long to wait before client sends a req or server can send a response */
    useThreads: true, /* make server multi-threaded. */
    daemon: true,
    log: true,
    accessLog: "./access.log",    //access log location, instead of stdout. Must be set if daemon==true
    errorLog: "./error.log",     //error log location, instead of stderr. Must be set if daemon==true
    map:
    {
        "/ltest":  ltest
    }
});

// if daemon==true then we get the pid of the detached process
// otherwise server.start() doesn't start until end of script.

/* give the forked server a chance to print its info*/
rampart.utils.sleep(1);

testFeature("server is running", rampart.utils.kill(pid,0) );

testFeature("500 curl http lmdb requests", function() {
    var url="http://localhost:8086/ltest";
    var i=0;
    var urls=[]
    var res, ret = true;
    while(i<50)
    {
        urls.push(url);
        i++;
    }

    i=0;
    while(i<10)
    {
        res=curl.fetch(urls, function(res){
            ret = ret && (res.text=="success");
            if(!ret)
                throw(res);
        });
        i++;
    }
    return ret;
});

testFeature("check lmdb table", function() {
    
    var count = lmdb.getCount(dbi);
    var txn = new lmdb.transaction(dbi, false, true);
    var res;
    var ret = true;

    while( (res == txn.cursorNext(true,true)) )
    {
        ret = ret && (res.key == res.value);
    }
    return ret && (count > 10000); //very very unlikely it will be less
});


lmdb.drop("");

kill(pid,15);
