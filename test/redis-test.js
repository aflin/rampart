var redis=require("rampart-redis");
rampart.globalize(rampart.utils);

chdir(process.scriptPath);

var rcl;
var rpids=[];

function start_redis(port){
    var ret = shell("which redis-server");

    if (ret.exitStatus != 0) {
	ret = shell("which redis6-server");
        if (ret.exitStatus != 0) {
            fprintf(stderr, "Could not find redis-server!! SKIPPING REDIS TESTS\n");
            process.exit(0);
        }
    }

    var rdexec = trim(ret.stdout);

    ret = exec(rdexec, {background: true}, "--port", port, "--dbfilename", `${port}-dump.rdb`);

    sleep(0.5);

    if (!kill(ret.pid, 0)) {
        fprintf(stderr, "Failed to start redis-server\n");
        process.exit(1);
    }

    rpids.push(ret.pid);

    sleep(0.5);
}

try {
    rcl=new redis.init(13287);
} catch (e) {
    start_redis(13287);
    rcl=new redis.init(13287);
}

rcl.flushall();

var ret = rcl.info();
var ver = ret.match(/redis_version.*/)[0].match(/[\d\./]+/)[0].split(".");
var vers=sprintf("%d%0.2d%0.2d", parseInt(ver[0]), parseInt(ver[1]), parseInt(ver[2]));

//console.log(vers);

if(vers < 60201) {
    cleanup();
    console.log("version 6.2.1 or greater required for test");
    process.exit(1);
}


function cleanup() {
    var killed=true;
    rpids.forEach(function(rpid) {
        if(!kill(rpid, 0))
          return;
        kill(rpid, 15);
        for (var i=0; i<50; i++) {
            if(!kill(rpid, 0)) {
                return;
            }
            sleep(0.1);
            kill(rpid, 15);
        }
        killed=false;
    });
    if(!killed) {
        fprintf(stderr, "Failed to kill redis-server\n");
        process.exit(1);
    }
}

function testFeature(name,test)
{
    var error=false;
    printf("testing redis - %-52s - ", name);
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
        if(rcl && rcl.errMsg) console.log(rcl.errMsg);
        cleanup();
        process.exit(1);
    }
    if(error) console.log(error);
}


testFeature("send set and get", function() {
    var ret1 = rcl.set("key1", "val1");
    var ret2 = rcl.set("key2", "val2");
    if (!ret1 || !ret2 
            || ret1 != "OK" || ret2 != "OK" )
    {
        console.log(ret1,ret2);
        return false;
    }
    ret1=rcl.get("key1");
    ret2=rcl.get("key2");

    return (ret1=="val1" && ret2=="val2");
});

var myset=new rcl.proxyObj("myset");
myset.i={a: "one", b: "two"};
myset._localvar="foo";
testFeature("redis proxyObj -- variables", function() {
    var set=new rcl.proxyObj("myset");
    return set.i.a=="one" && set._localvar == undefined;
});


delete myset.i;

testFeature("redis proxyObj -- delete", function() {
    var set=new rcl.proxyObj("myset");
    return set.i == undefined;
});

myset.a=1;
myset.b=2;

testFeature("redis proxyObj -- keys", function() {
    var set=new rcl.proxyObj("myset");
    var keys=Object.keys(set);
    var ret=0;
    for (var i=0;i<keys.length;i++) {
        var key=keys[i];
        if(key[0]=='_') continue;
        ret+=set[key];
    }

    return ret==3;
});

myset.myvar=[{foo:"bar"},2,bprintf("\x0a\x00\x0a")];
//myset.myvar[3]="some text";// does not work and throws no error

testFeature("redis proxyObj -- buffers", function() {
    var set=new rcl.proxyObj("myset");
    var x=set.myvar[2];
    return x[0]=10 && x[1]==0 && x[2]==10 && set.myvar[0].foo=='bar'; 
});

delete myset.myvar;

testFeature("redis proxyObj -- delete", function() {
    var set=new rcl.proxyObj("myset");
    return set.myvar==undefined;
});


testFeature("redis proxyObj -- in hset", function() {
    var set=rcl.hkeys("myset").sort();
    return set[0]=='a' && set[1]=='b';
});

myset._destroy();

testFeature("redis proxyObj -- destroy", function() {
    var set=rcl.hkeys("myset");
    return (set.length == 0);
});

testFeature("K/V Command - set/get/getdel/keys", function() {
    var r1=rcl.set("testkey", "abcd");
    var r2=rcl.get("testkey");
    var r5=rcl.keys("tes*");
    var r3=rcl.getdel("testkey");
    var r4=rcl.get("testkey");
    return r1=="OK" && r2=="abcd" && r3=="abcd" && r4==null && r5=="testkey";
});

testFeature("K/V Command - setnx/mset/mget/msetnx", function() {
    var r1=rcl.mset("testkey", "abcd", "testkey2", "efgh");
    rcl.setnx("testkey", "xxxxxx");
    var r3=rcl.msetnx("testkey2", "xxxx", "testkey3", "xxxx");
    var r2=rcl.mget("testkey","testkey2");
    var r4=rcl.mget("testkey2","testkey3");
    var r6=rcl.setnx("testkey", "xxxxxx");
    return r1=="OK" && r2[0]=="abcd" && r2[1]=="efgh" && 
           r4[0]==r2[1] && r4[1]===null && r3===false && r6==false;
});

testFeature("K/V Command - append/bitcount", function() {
    var r1=rcl.append("testkey", "efgh");
    var r2=rcl.bitcount("testkey");
    return r1==8 && r2==29;
});

testFeature("K/V Command - bit commands", function() {
    var aa = sprintf("%c", 170);
    var five5 = sprintf("%c", 85);
    var r1=rcl.mset("b1", aa, "b2", five5);
    var r2=rcl.bitop("OR", "b3", "b1", "b2");
    var r3=rcl.get({returnBuffer:true}, "b3");
    rcl.set("b4", bprintf("%c%c",1,6));
    var r4=rcl.bitfield("b4", "INCRBY", "i5", 0, 2, "GET", "u8", 8);
    rcl.set("b1", " ");
    var r5=rcl.bitpos("b1",1);
    rcl.setbit("b1", 7, 1);
    r2=rcl.getbit("b1", 2);
    return r1=="OK" && r3[0]==255 && r5==2 && r2==1 && r4[0]==2 && r4[1]==6;
});

testFeature("K/V Command - incr/decr commands", function() {
    var r1=rcl.set("testint", 5);
    rcl.incr("testint");
    rcl.incrby("testint",10);
    rcl.decrby("testint",4);
    rcl.decr("testint");
    var r2=rcl.get("testint");
    var r3=rcl.set("testfloat", 3.2);
    var r4=rcl.incrbyfloat("testfloat",3.3);
    var r5=rcl.get("testfloat");
    return r1=="OK" && r2==11 && r3=="OK" && r4==6.5 && r5==6.5;
});

testFeature("K/V Command - expire", function() {
    rcl.set("key1","val1");
    rcl.set("key2","val2");
    var r1 = rcl.pexpire("key4",200);
    rcl.pexpire("key1",200);
    rcl.pexpire("key2",200);
    var r2 = rcl.persist("key2");
    sleep(0.21);
    var r3 = rcl.get("key1");
    var r4 = rcl.get("key2");
    return r1==0 && r2==1 && r3==null && r4=="val2";
});

testFeature("K/V Command - getex/setex/ttl", function() {
    var r1 = rcl.ttl("nonexistant");
    var r2 = rcl.ttl("testint");
    var r3 = rcl.getex("testkey", "px", 200);
    var r4 = rcl.get("testkey");
    rcl.psetex("testkey2", 100, "xxxx");
    sleep(0.21);
    var r5 = rcl.get("testkey");
    var r6 = rcl.get("testkey2");
    return r1==-2 && r2==-1 && r3==r4 && r5==null && r6==null;
});

testFeature("K/V Command - getrange/setrange/getset ", function() {
    rcl.set("testkey",1);
    var r1=rcl.getset("testkey", "hijklmno");
    var r3=rcl.setrange("testkey",6, "N")
    var r2=rcl.getrange("testkey", 4, -1);
    return r1==1 && r2=="lmNo" && r3==8;
});

/* this seems to have disappeared in 7.0
testFeature("K/V Command - stralgo/strlen", function() {
    rcl.mset("testkey", "hello there", "testkey2", "here");
    var r1=rcl.stralgo("LCS", "strings", "hello there", "here");
    var r2=rcl.stralgo("LCS", "keys", "testkey", "testkey2");
    var r3=rcl.stralgo("LCS", "keys", "testkey", "testkey2", "len");
    var r4=rcl.stralgo("LCS", "keys", "testkey", "testkey2", "idx");
    var r5=rcl.strlen("testkey");
    return r1==r2 && r3==4 && r4.matches[0][0]==7 && r4.matches[0][1]==10 && r5==11;
});
*/

testFeature("Hash Command - hset/hget/hdel/hexists", function() {
    rcl.del("htest");
    var r1=rcl.hset("htest", "key1", "val1");
    var r2=rcl.hget("htest", "key1");
    var r3=rcl.hexists("htest", "key1");
    var r4=rcl.hdel("htest", "key1");
    var r5=rcl.hexists("htest", "key1");
    return r1==1 && r2=="val1" && r3===true && r4==1 && r5===false;
});

testFeature("Hash Command - hgetall/hmget/hmset/hlen", function() {
    var r1=rcl.hmset("htest", "key1", "val1", "key2", "val2", "key3", "val3");
    var r2=rcl.hmget("htest", "key1", "key2", "key3", "key4");
    var r3=rcl.hgetall("htest");
    var r4=rcl.hlen("htest");
    return r1=="OK" && r2[0]=="val1" && r2[1]=="val2" && r2[2]=="val3" && r2[3]==null &&
           r3.key1=="val1" && r3.key2=="val2" && r3.key3=="val3" && r4==3;
});


testFeature("Hash Command - hkeys/hvals/hincrby/byfloat", function() {
    var r1=rcl.hmset("htest", "keyint", 10, "keyfloat", 12.1);
    var r2=rcl.hincrby("htest", "keyint", 10);
    var r3=rcl.hincrbyfloat("htest", "keyfloat", 12);
    var r4=rcl.hkeys("htest");
    var r5=rcl.hvals("htest");
    return r1=="OK" && r2==20 && r3>24.09 && r3<24.10001 && r4.length==5 && r5.length==5;
});

var reducer = function(sum, cur) {
    return rcl.hget("htest2", cur) + sum;
}

testFeature("Hash Command - hrandfield/hscan/hsetnx/hstrlen", function() {
    rcl.hmset("htest2", "key1", 99, "key2", 100, "key3", 101);
    var r1=rcl.hget("htest2", rcl.hrandfield("htest2"));
    var r4=rcl.hsetnx("htest", "key3", 3);
    var r2=rcl.hrandfield("htest2", 3).reduce(reducer,0);
    var r3=rcl.hscan("htest2",0);
    var r5=rcl.hstrlen("htest", "key1");
    return r1>98 && r1<102 && r2==300 && r3.values.key1==99 && r4===false && r5==4;
});

testFeature("List Command - rpush/lpush/linsert/lrange/llen", function() {
    rcl.del("mylist");
    var r1=rcl.rpush("mylist", "three");
    var r2=rcl.lpush("mylist", "one");
    var r3=rcl.linsert("mylist", "before", "three", "two");
    var r4=rcl.lrange("mylist", 0, -1);
    var r5=rcl.llen("mylist");
    return r1==1 && r2==2 && r3==3 && r4[0]=="one" && r4[1]=="two" && r4[2]=="three" && r5==3;
});


testFeature("List Command - lindex/(l|r)pushx/(l|r)pop/rpoplpush", function(){
    rcl.del("mylist2");
    var r1=rcl.lindex("mylist", 1);
    var r2=rcl.lpushx("mylist2","one");
    var r3=rcl.rpushx("mylist2","two");
    var r4=rcl.lrange("mylist2", 0, -1);
    var r5=rcl.lpop("mylist");
    var r6=rcl.rpop("mylist");
    var r9=rcl.lpushx("mylist", "one");
    var r7=rcl.rpoplpush("mylist","mylist2");
    rcl.rpoplpush("mylist","mylist2");
    var r8=rcl.lindex("mylist2", 0);
    rcl.del("mylist2");
    rcl.del("mylist");
    return r1=="two" && r2==0 && r3==0 && r4.length==0 && r9==2 &&
           r5=="one" && r6=="three" && r7=="two" && r8=="one";
});


testFeature("List Command - sort/lmove/lpos/ltrim/lrem/lset", function(){
    rcl.rpush("mylist", "one");
    rcl.rpush("mylist", "two");
    rcl.rpush("mylist", "three");
    var r8 = rcl.sort("mylist", "DESC", "ALPHA");
    var r1=rcl.lmove("mylist", "mylist2", "RIGHT", "LEFT");
    rcl.lmove("mylist", "mylist2", "RIGHT", "LEFT");
    rcl.lmove("mylist", "mylist2", "LEFT", "RIGHT");
    var r2=rcl.lrange("mylist2", 0, -1);
    var r3=rcl.ltrim("mylist2", 1, 1);
    var r4=rcl.lrange("mylist2", 0, -1);
    rcl.lpush("mylist2","two");
    rcl.lpush("mylist2","two");
    rcl.lpush("mylist2","one");
    var r5=rcl.lrem("mylist2", -1, "two");
    var r6=rcl.lset("mylist2", 2, "THREE");
    var r7=rcl.lrange("mylist2", 0, -1);

    return r1=="three" && r2[0]=="two" && r3=="OK" && r4[0]=="three";
           r5==1 && r6=="OK" && r7[2]=="THREE" && r8[2]=="one";
});

testFeature("Set Command - sadd/srem/smove/scard/sismember", function(){
    rcl.del("set1","set2");
    var nums=["one", "two", "three", "four"];
    var r1=rcl.sadd("set1", nums);
    rcl.sadd("set2", "three", "four", "five", "six", "seven");
    var r2=rcl.smove("set2", "set1", "seven");
    var r3=rcl.scard("set2");
    var r4=rcl.srem("set1", "seven");
    var r5=rcl.sismember("set1", "three");
    
    return r1==4 && r2==1 && r3==4 && r4==1 && r5===true;
});


testFeature("Set Command - sdiff/sinter/sunion/*store", function(){
    var r1=rcl.sdiff("set1","set2");
    var r2=rcl.sinter("set1","set2");
    var r3=rcl.sunion("set1","set2");
    var r4=rcl.sdiffstore("setd","set1","set2");
    var r5=rcl.sinterstore("seti","set1","set2");
    var r6=rcl.sunionstore("setu","set1","set2");
    rcl.del("seti","setu","setd");
    return( ( (r1[0]=="one" && r1[1]=="two") || (r1[1]=="one" && r1[0]=="two")) &&
           ( (r2[0]=="three" && r2[1]=="four") || (r2[1]=="three" && r2[0]=="four")) &&            
           r3.length==6 && r4==2 && r5==2 && r6==6
          );
});

testFeature("Set Command - smembers/smismember/sscan/spop/srandm", function(){
    var r1=rcl.smembers("set1");
    var r2=rcl.smismember("set1", ["one", "nine"]);
    var r3=rcl.sscan("set1",0);
    var r4=rcl.spop("set1",2);
    var r5=rcl.smembers("set1");
    var r6=rcl.srandmember("set2",2);
    return r1.length==4 && r2[0]===true && r2[1]===false &&
           r3.cursor==0 && r3.values.length==4 &&
           r5.length==2 && r6.length==2;

});

var lex = [0,"a",0,"b",0,"c",0,"d",0,"e",0,"f",0,"g",0,"h",0,"i",0,"j",0,"k",0,"l",0,"m",0,"n",0,"o",0,"p",0,"q",0,"r",0,"s",0,"t",0,"u",0,"v",0,"w",0,"x",0,"y",0,"z"];

var ranked = [0,"a",1,"b",2,"c",3,"d",4,"e",5,"f"];

testFeature("Zset Command - zadd/zcard/zcount/zincrby", function(){
    rcl.del("zset1","zset2","zseti","zsetd","zsetu");
    var r1=rcl.zadd("zset1",lex);
    var r2=rcl.zcard("zset1");
    var r3=rcl.zincrby("zset1", 1, "a");
    var r4=rcl.zcount("zset1", 1,1);
    return r1==26 && r2==26 && r3==1 && r4==1;
});

testFeature("Zset Command - zrange/zdiff/zinter/zunion/*store", function(){
    var r2=false;
    var r1=rcl.zrange("zset1",0,27,"withscores",
        function(x){
            if(x.score==1 && x.value=="a")
                r2=true;
        }
    );
    var r3=rcl.zadd("zset2",ranked);
    var r4=rcl.zdiff(2,"zset1","zset2");
    rcl.zdiffstore("zsetd",2,"zset1","zset2");
    var r5=rcl.zrange("zsetd",0,-1,"withscores");

    var r6=rcl.zinter(2,"zset1","zset2");
    rcl.zinterstore("zseti",2,"zset1","zset2");
    var r7=rcl.zrange("zseti",0,-1,"withscores");

    var r8=rcl.zunion(2,"zset1","zset2");
    rcl.zunionstore("zsetu",2,"zset1","zset2");
    var r9=rcl.zrange("zsetu",0,-1,"withscores");
    rcl.del("zseti","zsetd","zsetu");

    return r1==26 && r2 && r3==6 && r4.length==20 && r5.length==20 &&
           r6.length==6 && r7.length==6 && r8.length==26 && r9.length==26;
});


testFeature("Zset Command - zrank/zrange(bylex|byscore|store)", function(){
    var r1=rcl.zrangebylex("zset1","[a", "[z");
    var r2=rcl.zrangebyscore("zset2", "-inf", "+inf", "withscores");
    var r3=rcl.zrangestore("zset3","zset1", 20, -1);
    var r4=rcl.zrange("zset3",0,-1);
    var r5=rcl.zrank("zset1", "z");
    rcl.del("zset3");
    return r1[0]=='b' && r1[25]=='a' && r2[2].score==2 && r2[3].value=="d" &&
           r3==6 &&r4[4]=="z" && r5==24;
});

testFeature("ZsetCommand - zscore/zmscore/zpopmax/zpopmin/zrandm", function(){
    var r1=rcl.zscore("zset1","a");
    var r2=rcl.zmscore("zset1", "z", "a");
    var r3=rcl.zpopmax("zset1", 2);
    var r4=rcl.zpopmin("zset1", 2);
    var r5=rcl.zrandmember({returnBuffer:true},"zset2", 1, "WITHSCORES");

    rcl.zadd("zset1",lex);
    rcl.zincrby("zset1", 1, "a");
    return r1==1 && r2[0]==0 && r2[1]==1 &&
           r3[0].value=='a' && r4[0].value=='b' &&
           r5[0].value[0] == r5[0].score+97;
});

testFeature("Zset Command - zrem/zremrange(bylex|byrank|byscore)", function(){
    var r1=rcl.zrem("zset1","b");
    var r2=rcl.zrange("zset1",0,-1);
    var r3=rcl.zremrangebylex("zset1","[a","[d");
    var r4=rcl.zrange("zset1",0,-1);
    var r5=rcl.zremrangebyrank("zset1",0,1);
    var r6=rcl.zrange("zset1",0,-1);
    var r7=rcl.zremrangebyscore("zset1",1,1);
    var r8=rcl.zrange("zset1",0,-1);
    rcl.zadd("zset1",lex);
    rcl.zincrby("zset1", 1, "a");
    return r1==1 && r2.length==25 && r3==2 && r4.length==23 && r5==2&&r6.length==21 &&
           r7==1 && r8.length==20;
});

testFeature("Zset Command - zrev(rank|range(bylex|byscore))/zscan", function(){
    var r1=rcl.zrevrank("zset1", "z");
    var r2=rcl.zrevrange("zset1", 0, -1, "withscores");
    var r3=rcl.zrevrangebylex("zset1", "[z", "-");
    var r4=rcl.zrevrangebyscore("zset1", "+inf", "-inf");
    var r5=rcl.zscan("zset1",0);
    return r1==1 && r2[2].value=='y' && r3[0]=='a' && r4[25]=='b' &&
           r5.cursor==0 && r5.values[0].value=='b';
});

testFeature("Xstream Command -- xadd/xrange/xread", function(){
    var ms= new Date().getTime();
    var r1=rcl.xadd("x1","*", "mess1a","val1a", "mess2a", "val2a");
    var r1=rcl.xadd("x1","*", "mess1","val1", "mess2", "val2");
    rcl.xadd("x2","*", "mess1","val1", "mess2", "val2");
    var r2=rcl.xadd("x1", "MAXLEN", "=", 2, "*", "mess3","val3");
    var r3=rcl.xrange("x1", sprintf("%d",ms), "+");
    var r4=rcl.xread("STREAMS", "x1", "x2", "0-0", "0-0");
    var r5 = ( (r4[0].data.length==2 || r4[0].stream=="x1") &&
               (r4[1].data.length==1 || r4[1].stream=="x2") );
    return Object.keys(r3).length==2 && r5;
});

testFeature("Misc Command - time/touch/object/type/unlink", function(){
  rcl.set("key1", "val1");
  rcl.set("key2", "val2");
  sleep(1);
  rcl.touch("key2");
  var r1 = rcl.object("idletime", "key1")
  var r2 = rcl.object("idletime", "key2")

  var r3 = rcl.time();

  var r4 = rcl.type("key1");
  var r5 = rcl.unlink("key1");
  var r6 = rcl.get("key1");

  return (typeof r3[0]) == "number" && (typeof r3[1]) == "number" && 
           r2 == 0 && r1 == 1 && r4 == "string" && r5 == 1 && r6 == null;
});


var ret1 = rcl.set("key3", "val3");
if (!ret1 || ret1 != "OK") {
    console.log(ret1,rcl.errMsg);
    testFeature("send set and get async", false);
} else {
    rcl.get({async:true}, "key3",function(res,err){
        testFeature("send set and get async", (res == 'val3' && !err) )
    });
}

rcl.set("key4", function(res,err){
    if(err && err.match(/wrong number/))
        testFeature("set async callback error", true);
    else
        testFeature("set async callback error", false);
});

/* todo - test async stream and xread_auto_async somehow

console.log(
rcl.format("XINFO STREAM x1")
);

process.exit();
rcl.xread("STREAMS", "x1", "x2", "0-0", "0-0", function(x) {
    printf("Sync Callback:\n%3J\n", x);
});



rcl.subscribe("one", "two", function(x){ 
    console.log("callback:",x);
    if(x[0]=="message")
     setTimeout(function(){
        rcl.publish("one", "on going...");
     },1000);
    if(x[0]=="unsubscribe" && x[2] == 0 )
    {
        console.log("closing");
        this.close();
    }
});

rcl.publish("one","sync hello");




rcl.xread_auto_async({ x1:"0-0", x2:"0-0", x3:"0-0", x4:"0-0"}, function(x)
{
    this.xreadArgs.x1=false;
    printf("Callback:\n%3J\n", x);
});


rcl.xread_block_async(0, "STREAMS", "x1", "x2", "$", "$", function(x)
{
    printf("BLOCK Callback:\n%3J\n", x);
//    rcl.close_async();
});

*/

setTimeout(function(){
    rcl.flushall();
    cleanup();
},10);
