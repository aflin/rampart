/* make printf et. al. global */
rampart.globalize(rampart.utils);

/* load the http server module */
var server=require("rampart-server");

/* load curl module */
var curl=require("rampart-curl");

/* load the lmdb module */

var Lmdb = require("rampart-lmdb");
var pid;  // pid of server, set below
var lmdb; // the db env handle
var dbi;  // the db handle

function testFeature(name,test)
{
    var error=false;
    printf("testing server/lmdb - %-46s - ", name);
    fflush(stdout);
    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }
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
/* these tests fail if errors are thrown */
testFeature("Open test database env", function() {
    /* 
       normally we'd load Lmdb (at lease we would do "new init()") in a module
       after the server forks.  However here we are testing to make sure lmdb
       behaves correctly after forking as well as in multi-threaded use.
    */
    lmdb= new Lmdb.init(
        process.scriptPath+"/lmdb-test-db",
        true, /* true means create the database if it doesn't exist */
        {noMetaSync:true, noSync:true, mapSize: 160}
    );
    return true;
});

testFeature("Open database", function() {
    dbi = lmdb.openDb(null,true); //open default db
    return true;
});



var user = trim(exec("whoami").stdout);

if(user == 'root') {
    var ret = shell(`chown -R nobody ${process.scriptPath}/lmdb-test-db`);
    if(ret.exitStatus)
    {
        printf("chmod error: %s %s\n", ret.stderr, ret.stdout);
        process.exit(ret.exitStatus);
    }
}


function ltest(req) {
    var kv,i;
    var txn;
    if(req.query.firstrun)
    {
        txn = new lmdb.transaction(dbi, true, true);
        txn.commit();
        return{
            text:"complete",
        }
    }
    
    txn = new lmdb.transaction(dbi, true, true);

    /* some writes */
    for (i=0;i<10;i++)
    {
        kv = sprintf("%d", Math.random()*100000);
        txn.put(dbi, kv, kv);   
    }
    txn.commit();

    /* some reads */
    txn = new lmdb.transaction(dbi, false);
    //var j=0;
    for (i=0;i<100;i++)
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
    accessLog: process.scriptPath+"/access.log",    //access log location, instead of stdout. Must be set if daemon==true
    errorLog: process.scriptPath+"/error.log",     //error log location, instead of stderr. Must be set if daemon==true
    map:
    {
        "/ltest":  ltest
    }
});

// if daemon==true then we get the pid of the detached process
// otherwise server.start() doesn't start until end of script.

/* give the forked server a chance to print its info*/
sleep(1);

testFeature("server is running", rampart.utils.kill(pid,0) );

testFeature("new lmdb transaction after fork", function() {
    var url="http://localhost:8086/ltest?firstrun=1";
    var i=0;
    var res, ret;

    res=curl.fetch(url, function(res){
        ret = (res.text=="complete");
    });
    return ret;
});

testFeature("500 requests (10 puts, 100 gets each)", function() {
    var url="http://localhost:8086/ltest";
    var i=0;
    var urls=[]
    var err, res, ret = true;
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
            {
                err=res.errMsg;
                return false;
            }
        });
        if(err)
            throw(new Error(err));
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
    return ret && (count > 1000); //very very unlikely it will be less
});

kill(pid,15);

testFeature("syncing data to disk", function() {
    lmdb.sync();
    return true;
});

