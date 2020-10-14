var ramis=require("rampart-redis");
var ra=new ramis.createClient();


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
    rampart.utils.printf("testing %-40s - ", name);
    if(test)
        rampart.utils.printf("passed\n")
    else
        rampart.utils.printf(">>>>> FAILED <<<<<\n");
    if(error) console.log(error);
}


testFeature("send set and get", function() {
    var ret1 = ra.set("key1", "val1");
    var ret2 = ra.set("key2", "val2");
    if (!ret1.length || !ret2.length 
            || ret1[0] != "OK" || ret2[0] != "OK" )
    {
        console.log(ret1,ret2);
        return false;
    }
    ret1=ra.get("key1");
    ret2=ra.get("key2");

    return (ret1=="val1" && ret2=="val2");
});

var myset=new ra.ramvar("myset");
myset.i={a: "one", b: "two"};
myset._localvar="foo";
testFeature("redis ramvar variables", function() {
    var set=new ra.ramvar("myset");
    return set.i.a=="one" && set._localvar == undefined;
});


delete myset.i;

testFeature("redis ramvar variables -- delete", function() {
    var set=new ra.ramvar("myset");
    return set.i == undefined;
});

myset.a=1;
myset.b=2;

testFeature("redis ramvar variables -- keys", function() {
    var set=new ra.ramvar("myset");
    var keys=Object.keys(set);
    var ret=0;

    for (var i=0;i<keys.length;i++) {
        var key=keys[i];
        if(key[0]=='_') continue;
        ret+=set[key];
    }

    return ret==3;
});

myset.myvar=[{foo:"bar"},2,rampart.utils.bprintf("\x0a\x00\x0a")];
//myset.myvar[3]="some text";// does not work and throws no error

testFeature("redis ramvar variables -- buffers", function() {
    var set=new ra.ramvar("myset");
    var x=set.myvar[2];
    return x[0]=10 && x[1]==0 && x[2]==10 && set.myvar[0].foo=='bar'; 
});

delete myset.myvar;

testFeature("redis ramvar variables -- delete", function() {
    var set=new ra.ramvar("myset");
    return set.myvar==undefined;
});


testFeature("redis ramvar variables -- in hset", function() {
    var set=ra.hkeys("myset")[0].sort();
    return set[0]=='a' && set[1]=='b';
});

myset._destroy();

testFeature("redis ramvar variables -- destroy", function() {
    var set=ra.hkeys("myset")[0];
    return !set.length;
});
