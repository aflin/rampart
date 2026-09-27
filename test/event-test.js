var testFeature = new (require('./test-feature.js'))({
    prefix: "event",
    onFail: function() { if (!process.env.EVENT_TEST_KEEPGOING) process.exit(1); }
});
var thread = rampart.thread;


var usr_var = "Basic Functionality";

function myCallback (uservar,triggervar){

    if(triggervar > 5)
        testFeature(`${uservar} remove event`, false);

    if(triggervar>4)
    {
        rampart.event.remove("myev");
        testFeature(uservar, usr_var == uservar);
        if(!thread.getCurrentId())
            do_thread_test();
        return;
    }
    rampart.event.trigger("myev", triggervar+1);
}

rampart.event.on("myev", "myfunc", myCallback, usr_var);

rampart.event.trigger("myev", 1);


function do_thread_test() {

    usr_var = "Basic Use in thread";

    var thr = new thread();
    thr.exec(
        function(uv) {
            //console.log("set event")
            rampart.event.on("myev", "myfunc3", myCallback, uv);
        },
        usr_var,
        //trigger in callback to make sure event is registered in thread
        function(){
            rampart.event.trigger("myev", 1);
            //console.log("event triggered");
        }
    );
}

var lock = new rampart.lock();

function multi_test(msg, tmsg) {
    var count;
    rampart.event.off("myev2", msg);

    lock.lock();
    count=thread.get("count")
    if(!count) count=1;
    else count++;
    thread.put("count",count);
    lock.unlock();
    if(count == 2)
      testFeature("Multiple threads - success", true);      
}

var thr1 = new thread();
var thr2 = new thread();
var thr3 = new thread();

thr1.exec(function(){
    rampart.event.on("myev2", "myfunc", multi_test, "myfunc");
    thread.put("thr1Done", true);
},"thr1");

thr2.exec(function(){
    rampart.event.on("myev2", "myfunc2", multi_test, "myfunc2");
    thread.put("thr2Done", true);
},"thr2");


thr3.exec(function(){
    testFeature("Multiple threads - start ...", true);
    var t1 = thread.get("thr1Done",500);
    var t2 = thread.get("thr2Done",500);
    //console.log(t1,t2);
    if(!t1 || !t2)
        process.exit(1);
    rampart.event.trigger("myev2", `from thread ${thread.getCurrentId()}`);
}, "thr3");

sleep(0.1);
var cnt=thread.get("count",50);
var x=0
while(cnt!=2)
{
    cnt=thread.get("count");
    x++;
    if(x>10)
        testFeature("Multiple threads - failed", false); 
    sleep(0.05);
}

/* ---- off()/remove() vs. a later on() of the same name ----
   off() and remove() are carried out later, on each thread's event loop.
   They must remove only registrations that existed when they were
   called, never one made afterwards under the same name. */

var regSeen = {};
function regMark(uv) { regSeen[uv] = true; }
function regTrigLater(ev, ms) { setTimeout(function(){ rampart.event.trigger(ev); }, ms || 50); }

var regSteps = [
    function() {
        /* off, then on with the same name */
        rampart.event.on("reg-a", "x", regMark, "a-old");
        rampart.event.off("reg-a", "x");
        rampart.event.on("reg-a", "x", regMark, "a-new");
        regTrigLater("reg-a");
        return function() {
            testFeature("off then on same name - new handler survives",
                regSeen["a-new"] === true && !regSeen["a-old"]);
        };
    },
    function() {
        /* same, but done from inside another event's callback */
        rampart.event.on("reg-b", "x", regMark, "b-old");
        rampart.event.on("reg-b-outer", "o", function() {
            rampart.event.off("reg-b", "x");
            rampart.event.on("reg-b", "x", regMark, "b-new");
            rampart.event.off("reg-b-outer", "o");
        });
        rampart.event.trigger("reg-b-outer");
        regTrigLater("reg-b", 100);
        return function() {
            testFeature("off then on in another event's callback",
                regSeen["b-new"] === true && !regSeen["b-old"]);
        };
    },
    function() {
        /* off still removes the older registration */
        rampart.event.on("reg-c", "x", regMark, "c");
        rampart.event.off("reg-c", "x");
        regTrigLater("reg-c");
        return function() {
            testFeature("off removes an earlier handler", !regSeen["c"]);
        };
    },
    function() {
        rampart.event.on("reg-d", "x", regMark, "d-x");
        rampart.event.on("reg-d", "y", regMark, "d-y");
        rampart.event.off("reg-d", "x");
        regTrigLater("reg-d");
        return function() {
            testFeature("off removes only the named handler",
                regSeen["d-y"] === true && !regSeen["d-x"]);
        };
    },
    function() {
        /* off, on, off: the second off removes the re-registration */
        rampart.event.on("reg-e", "x", regMark, "e-1");
        rampart.event.off("reg-e", "x");
        rampart.event.on("reg-e", "x", regMark, "e-2");
        rampart.event.off("reg-e", "x");
        regTrigLater("reg-e");
        return function() {
            testFeature("off, on, off - nothing left", !regSeen["e-1"] && !regSeen["e-2"]);
        };
    },
    function() {
        /* remove, then on */
        rampart.event.on("reg-f", "a", regMark, "f-a");
        rampart.event.on("reg-f", "b", regMark, "f-b");
        rampart.event.remove("reg-f");
        rampart.event.on("reg-f", "a", regMark, "f-new");
        regTrigLater("reg-f");
        return function() {
            testFeature("remove then on - only the new handler runs",
                regSeen["f-new"] === true && !regSeen["f-a"] && !regSeen["f-b"]);
        };
    },
    function() {
        rampart.event.on("reg-g", "a", regMark, "g-a");
        rampart.event.on("reg-g", "b", regMark, "g-b");
        rampart.event.remove("reg-g");
        regTrigLater("reg-g");
        return function() {
            testFeature("remove removes all earlier handlers", !regSeen["g-a"] && !regSeen["g-b"]);
        };
    },
    function() {
        /* on twice with the same name replaces */
        rampart.event.on("reg-h", "x", regMark, "h-1");
        rampart.event.on("reg-h", "x", regMark, "h-2");
        regTrigLater("reg-h");
        return function() {
            testFeature("on with same name replaces", regSeen["h-2"] === true && !regSeen["h-1"]);
        };
    },
    function() {
        /* another thread re-registers after main's off(), before its
           own loop has run the deferred delete */
        var thr = new thread();
        thr.exec(function() {
            function mk(tag) { return function() { rampart.thread.put(tag, true); }; }
            rampart.event.on("reg-i", "x", mk("reg-i-old"));
            rampart.thread.put("reg-i-ready", true);
            rampart.thread.get("reg-i-go", 5000);       /* main calls off() meanwhile */
            rampart.event.on("reg-i", "x", mk("reg-i-new"));
            rampart.thread.put("reg-i-rereg", true);
        });
        thread.get("reg-i-ready", 5000);
        rampart.event.off("reg-i", "x");
        thread.put("reg-i-go", true);
        thread.get("reg-i-rereg", 5000);
        regTrigLater("reg-i", 100);
        return function() {
            testFeature("off in one thread, on in another afterwards - survives",
                thread.get("reg-i-new") === true && !thread.get("reg-i-old"));
        };
    },
    function() {
        var thr = new thread();
        thr.exec(function() {
            rampart.event.on("reg-j", "x", function() { rampart.thread.put("reg-j-ran", true); });
            rampart.thread.put("reg-j-ready", true);
        });
        thread.get("reg-j-ready", 5000);
        rampart.event.off("reg-j", "x");
        regTrigLater("reg-j", 100);
        return function() {
            testFeature("off removes another thread's earlier handler", !thread.get("reg-j-ran"));
        };
    }
];

/* a thread with a live handler stays alive; clear ours so the test exits */
var regEvents = ["reg-a","reg-b","reg-c","reg-d","reg-e","reg-f","reg-g","reg-h","reg-i","reg-j"];

function regRun(i) {
    if (i >= regSteps.length) {
        regEvents.forEach(function(e) { rampart.event.remove(e); });
        return;
    }
    var check = regSteps[i]();
    setTimeout(function() { check(); regRun(i + 1); }, 300);
}
setTimeout(function() { regRun(0); }, 200);
