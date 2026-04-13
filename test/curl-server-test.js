/* make printf et. al. global */
rampart.globalize(rampart.utils);

/* load the http server module */
var server=require("rampart-server");

/* load curl module */
var curl=require("rampart-curl");

/* load crypto module */
var crypto=require("rampart-crypto")


/* sql module can be loaded here (better) or in callback functions (minor check overhead).
     If used to create database, the overhead is not minor, and should be done here rather
     than repeatedly in a callback
*/
var Sql=require("rampart-sql");

var iam = trim(exec('whoami').stdout);

var tmpdir = process.scriptPath + '/tmp-test';
if (!stat(tmpdir)) mkdir(tmpdir);

var sql= new Sql.init(tmpdir+"/testdb",true); /* true means create the database if it doesn't exist */

var pid=0;
/* ******************************************************
    Setup of tables for server callback function tests 
********************************************************* */

/* check if our quicktest table exists.  If not, make it */
var res=sql.exec("select * from SYSTABLES where NAME='quicktest'");
if(res.rows.length==0) {
    res=sql.exec("create table quicktest ( I int, Text varchar(16) );");
    sql.exec("insert into quicktest values(2,'just a test');");
    sql.exec("create index quicktest_I_x on quicktest(I);");
}

function kill_server(pid) {
    if(!pid) return;
    if (!kill(pid, 0)) return;
    kill(pid, 15);
    sleep(0.5);
    if (!kill(pid, 0)) return;
    kill(pid, 9);
    sleep(0.5);
    if (!kill(pid, 0)) return;
    fprintf(stderr, "WARNING: process %d could not be terminated\n", pid);
}

function cleanup() {
    kill_server(pid);
    /* remove generated files */
    if(stat(tmpdir + '/curl-server-test-alog')) rmFile(tmpdir + '/curl-server-test-alog');
    if(stat(tmpdir + '/curl-server-test-elog')) rmFile(tmpdir + '/curl-server-test-elog');
    if(stat(tmpdir + '/coutput')) rmFile(tmpdir + '/coutput');
    if(stat(tmpdir + '/sample-cert.pem')) rmFile(tmpdir + '/sample-cert.pem');
    if(stat(tmpdir + '/sample-key.pem')) rmFile(tmpdir + '/sample-key.pem');
    if(stat(smodPath)) shell("rm -rf " + smodPath);
    if(stat(tmpdir)) shell("rm -rf " + tmpdir);
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
    printf("testing curl/serv - %-48s - ", name);
    if(test)
        printf("passed\n")
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        if(error) console.log(error);
        cleanup();
        process.exit(1);
    }
    if(error) console.log(error);
}

var cert = tmpdir+'/sample-cert.pem';
var key = tmpdir+'/sample-key.pem';

if(
    !(stat(cert) &&
      stat(key) )
)
{
    var r = crypto.gen_cert({
        country: "US",
        state: "Deleware",
        city: "Wilmington",
        organization: "Sample Co",
        organizationUnit: "Sample Department",
        email: "sample@sample.none",
        name: "sample.none",
        bits: 2048,
        days: 365,
        subjectAltName: ["localhost", "*.localhost"]
    });
    fprintf(key, '%s', r.key);
    fprintf(cert, '%s', r.cert);
}

var smodPath = tmpdir + '/smods';

if(!stat(smodPath))
{
    mkdir(smodPath);
    fprintf(smodPath + "/testmod.js", '%s',
        "module.exports=function(res){return {text:'test'} }\n");

    if(iam == 'root')
    {
        chown({user:"nobody", path: smodPath});
        chown({user:"nobody", path: smodPath + "/testmod.js"});
    }
}

var globalvar1={};
globalvar1.x=99;
globalvar1.myself = globalvar1;

function globalfunc(req) {
    if(globalvar1.x == 99 && globalvar1.myself.myself.x == 99)
        return {text:'ok'};

    return {text: 'fail'};
}
function sendchunk(req){
    var chunk = readFile(req.file, req.chunkIndex * req.chunkSize, req.chunkSize);

    if(req.stat.size > (req.chunkIndex+1) * req.chunkSize)
        req.chunkSend(chunk);
    else
        req.chunkEnd(chunk);
}

var ctestfile = process.scriptPath + "/wiki_00";

function chunktest(req) {

    req.chunkSize = 32768; //this size is larger than curls write buffer, so it tests our ability to reassemble the chunk
    req.file=ctestfile;
    req.stat= stat(req.file);
    return {
        "txt": sendchunk,
        chunk:  true,
    };
}

pid=server.start(
{
    bind: "127.0.0.1:8087",
    developerMode: true,
    /* only applies if starting as root */
    user: "nobody",

    scriptTimeout: 1.0, /* max time to spend in JS */
    connectTimeout:20.0, /* how long to wait before client sends a req or server can send a response */
    useThreads: true, /* make server multi-threaded. */
    daemon: true,
    log: true,
    accessLog: tmpdir + '/curl-server-test-alog',
    errorLog:  tmpdir + '/curl-server-test-elog',
    secure:true,
    sslKeyFile:  key,
    sslCertFile: cert,

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
    rateLimit: {
        "/limited":      {rate: 5, window: 10, key: "ip"},
        "/fp-limited":   {rate: 3, window: 10, key: "fingerprint"},
        "/ck-limited":   {rate: 3, window: 10, key: "cookie:rltest"},
        "/casc/":        {rate: 8, window: 10, key: "ip"},
        "/casc/tight/":  {rate: 3, window: 10, key: "ip"}
    },
    map:
    {
        /*
            filesystem mappings are always folders.  
             "/tetris"    becomes  "/tetris/
             "./mPurpose" becomes  "./mPurpose/"
        */
        "/":                "./",
        '/global':          globalfunc,
        "/sample":          function(req){return "test";},
        "/modtest/":	    {modulePath:smodPath},
        "/timeout":         function(){
                                for (var i=0;i<1000000000;i++);
                                return("done");
                            },
        "/chunk.txt":       chunktest,
        "/limited":         function(req) { return {text: "ok"}; },
        "/fp-limited":      function(req) { return {text: "ok"}; },
        "/ck-limited":      function(req) { return {text: "ok"}; },
        "/unlimited":       function(req) { return {text: "ok"}; },
        "/casc/normal":     function(req) { return {text: "ok"}; },
        "/casc/tight/a":    function(req) { return {text: "ok"}; }
    }
});

// if daemon==true then we get the pid of the detached process
// otherwise server.start() never returns

/* give the forked server a chance to print its info*/
rampart.utils.sleep(0.2);

testFeature("server is running", rampart.utils.kill(pid,0) );

testFeature("curl secure request/redirect/follow", function() {
    var res=curl.fetch("https://yahoo.com/");
    var res2=curl.fetch({location:true},"https://yahoo.com/");
    if ((res.status == 301|| res.status == 302) && (res2.status == 200 || res2.status > 399) )
        return true;
    console.log(res.status,res2.status);
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

testFeature("server, global func and copy self ref object", function() {
    var res=curl.fetch({insecure:true},"https://localhost:8087/global");
    if (res.text == "ok")
        return true;
    console.log(res.text);
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
    var res=curl.fetch({insecure:true},"https://localhost:8087/modtest/testmod.txt");
    return res.status == 200 && res.text == "test";
});

testFeature("server custom not found", function (){
    var res=curl.fetch({insecure:true},"https://localhost:8087/nowhere");
    return res.status == 404 && res.text == "notfound";
});

testFeature("server script timeout", function (){
    var res=curl.fetch({insecure:true},"https://localhost:8087/timeout");
    return res.status == 500;
});

testFeature("server/curl chunking", function(){
    var lastprogsz, res1, res2;
    var coutput = tmpdir + '/coutput'
    var f = fopen(coutput, 'w+');
    var shortsizes=0;
    curl.fetch('https://localhost:8087/chunk.txt',
    {
        insecure:true,

        progressCallback: function(res) {
            lastprogsz=res.progress;
        },

    //    skipFinalRes: true,

        chunkCallback: function(res){
            res2=res.body;
            fprintf(f , '%s', res.body);
            // this should only happen on the last chunk
            if(res.body.length != 32768)
                shortsizes++;
        },

        callback: function(res) {
            res1=res.body
        }
    });

    f.fclose();

    var hash1 = crypto.sha256(readFile(ctestfile));
    var hash2 = crypto.sha256(res1);
    var hash3 = crypto.sha256(readFile(coutput));

    rmFile(coutput);

    return ( hash1 == hash2 && hash2==hash3 && lastprogsz == res1.length && shortsizes==1);
});


/* On MSYS/Cygwin, SSL handshakes are slower and prior keepalive connections
   occupy server threads, so async transfers need longer timeouts. */
var isMsys = /Msys/i.test(rampart.buildPlatform);
var asyncTimeout1 = isMsys ? 10000 : 2000;
var asyncTimeout2 = isMsys ? 5000  : 500;

/* ---- rate limiter tests ---- */


testFeature("rate limit: requests within limit pass", function() {
    /* 5 parallel requests, limit is 5/10s — all should pass */
    var urls = [];
    for (var i = 0; i < 5; i++) urls.push("https://127.0.0.1:8087/limited");
    var ok = 0;
    curl.fetch({insecure:true}, urls, function(res) { if (res.status == 200) ok++; });
    return ok === 5;
});

testFeature("rate limit: excess requests get 429", function() {
    /* 5 more — should all be 429 since we just used up the tokens */
    var urls = [];
    for (var i = 0; i < 5; i++) urls.push("https://127.0.0.1:8087/limited");
    var blocked = 0;
    curl.fetch({insecure:true}, urls, function(res) { if (res.status == 429) blocked++; });
    return blocked === 5;
});

testFeature("rate limit: unrated path not affected", function() {
    var res = curl.fetch({insecure:true}, "https://127.0.0.1:8087/unlimited");
    return res.status === 200;
});

testFeature("rate limit: tokens refill over time", function() {
    sleep(3); /* at 0.5 tokens/sec, 3s = 1.5 tokens = 1 request */
    var res = curl.fetch({insecure:true}, "https://127.0.0.1:8087/limited");
    return res.status === 200;
});

testFeature("rate limit: fingerprint key works", function() {
    /* 4 requests with same headers, limit is 3 */
    var n200 = 0, n429 = 0;
    for (var i = 0; i < 4; i++) {
        var res = curl.fetch({insecure:true}, "https://127.0.0.1:8087/fp-limited");
        if (res.status === 200) n200++;
        else if (res.status === 429) n429++;
    }
    return n200 === 3 && n429 === 1;
});

testFeature("rate limit: cookie key, same cookie", function() {
    sleep(2); /* let fp tokens refill a bit */
    var n200 = 0, n429 = 0;
    for (var i = 0; i < 4; i++) {
        var res = curl.fetch({insecure:true, headers: ["Cookie: rltest=user1"]},
                             "https://127.0.0.1:8087/ck-limited");
        if (res.status === 200) n200++;
        else if (res.status === 429) n429++;
    }
    return n200 === 3 && n429 === 1;
});

testFeature("rate limit: different cookie not affected", function() {
    var res = curl.fetch({insecure:true, headers: ["Cookie: rltest=user2"]},
                         "https://127.0.0.1:8087/ck-limited");
    return res.status === 200;
});

testFeature("rate limit: cascading, tight path", function() {
    /* /casc/tight/ has limit 3, and also counts against /casc/ limit 8 */
    var n200 = 0, n429 = 0;
    for (var i = 0; i < 4; i++) {
        var res = curl.fetch({insecure:true}, "https://127.0.0.1:8087/casc/tight/a");
        if (res.status === 200) n200++;
        else if (res.status === 429) n429++;
    }
    /* 3 allowed by /casc/tight/, 4th blocked */
    return n200 === 3 && n429 === 1;
});

testFeature("rate limit: cascading, global exhausted", function() {
    /* /casc/ has 8 tokens. 3 /casc/tight/ requests consumed 3 from /casc/.
       Remaining: 8 - 1 (first entry creation) - 2 (requests 2-3) = 5.
       But time(NULL) granularity may consume an extra token.
       Expect 4-5 allowed, then blocked. */
    var n200 = 0, n429 = 0;
    for (var i = 0; i < 6; i++) {
        var res = curl.fetch({insecure:true}, "https://127.0.0.1:8087/casc/normal");
        if (res.status === 200) n200++;
        else if (res.status === 429) n429++;
    }
    /* at least 4 allowed, at least 1 blocked */
    return n200 >= 4 && n429 >= 1;
});
/* ---- end rate limiter tests ---- */

var thr = new rampart.thread();

thr.exec(function() {
    var a="https://localhost:8087/sample";
    var aa=[a,a,a,a,a,a,a,a,a,a];
    var ao = {url:a, insecure:true};
    var aao = [ao,ao,ao,ao,ao,ao,ao,ao,ao,ao];
    var n=0,n2=0;
    curl.fetchAsync({insecure:true},aa,function(res){
        if(res.text=='test') n++;
    }).finally(function(){
        rampart.thread.put("res",n);
    })

    curl.submitAsync(aao,function(res){
        if(res.text=='test') n2++;
    }).finally(function(){
        rampart.thread.put("res2",n2);
    });
});

setTimeout( function(){
    testFeature("fetchAsync & submitAsync in thread w/ finally", function (){
        var res=rampart.thread.get("res", asyncTimeout1);
        var res2=rampart.thread.get("res2", asyncTimeout2);
        return res==10 && res2==10;
    });

    cleanup();
}, 2);
