#!/usr/bin/env rampart
/* thread-full.js
 *
 * Comprehensive coverage test for every function in rampart-thread.rst:
 * new rampart.thread (default/persist/keepOpen/bare), thr.exec/close/
 * terminate/getId, rampart.thread.getCurrentId/put/get/onGet/del/waitfor,
 * and new rampart.lock + lock/unlock/trylock.
 * Run from the test/ directory:  rampart thread-full.js
 */
var t = new (require('./test-feature.js'))({prefix:"thread"});
rampart.globalize(rampart.utils);
var thread = rampart.thread;

/* Threading is asynchronous but the harness checks return values
   synchronously.  Technique: a worker puts its result to the shared
   clipboard and the main test blocks on rampart.thread.get(key, timeout)
   (level-then-edge: returns an existing value, else waits for one).
   thread.waitfor() is edge-triggered (only returns on an update AFTER the
   call) so it is used only where that semantic is under test.
   thr.exec callbacks run in the loop of the thread that created the worker;
   the main loop never runs (the suite ends via process.exit), so callbacks
   are exercised by nesting the exec inside a live worker thread.
   Every test uses unique clipboard keys to avoid cross-test collisions. */

/* =================== new rampart.thread() =================== */

t("new thread - returns object with exec/close/getId", function(){
    var thr = new rampart.thread();
    t.mustEq(typeof thr.exec, "function", "has exec");
    t.mustEq(typeof thr.close, "function", "has close");
    t.mustEq(typeof thr.getId, "function", "has getId");
    t.mustEq(typeof thr.terminate, "function", "has terminate");
    thr.close();
    return true;
});

t("new thread - persist boolean true constructs and closes", function(){
    var thr = new rampart.thread(true);
    thr.exec(function(){ rampart.thread.put("nt_persist", 1); });
    t.mustEq(thread.get("nt_persist", 5000), 1, "persistent thread ran");
    thr.close();   // required for persistent threads
    return true;
});

t("new thread - options {keepOpen:true}", function(){
    var thr = new rampart.thread({keepOpen:true});
    thr.exec(function(){ rampart.thread.put("nt_keepopen", 2); });
    t.mustEq(thread.get("nt_keepopen", 5000), 2, "keepOpen thread ran");
    thr.close();
    return true;
});

t("new thread - bare:true has no host globals but has rampart.thread", function(){
    var thr = new rampart.thread({bare:true});
    thr.exec(function(){
        rampart.thread.put("nt_bare", {
            threadPut: typeof rampart.thread.put,
            utils:   typeof rampart.utils,
            proc:    typeof process,
            req:     typeof require
        });
    });
    var r = thread.get("nt_bare", 5000);
    t.mustEq(r.threadPut, "function", "bare thread HAS the rampart.thread surface");
    t.mustEq(r.utils, "undefined", "bare thread has no rampart.utils");
    t.mustEq(r.proc, "undefined", "bare thread has no process");
    t.mustEq(r.req, "undefined", "bare thread has no require");
    thr.close();
    return true;
});

t("new thread - options {bare:true, keepOpen:true}", function(){
    var thr = new rampart.thread({bare:true, keepOpen:true});
    thr.exec(function(){ rampart.thread.put("nt_barekeep", 3); });
    t.mustEq(thread.get("nt_barekeep", 5000), 3, "bare+keepOpen thread ran");
    thr.close();
    return true;
});

/* =================== thr.exec() =================== */

t("exec - positional form runs threadFunc with threadArg", function(){
    var thr = new rampart.thread();
    thr.exec(function(x){ rampart.thread.put("ex_pos", x + 1); }, 41);
    t.mustEq(thread.get("ex_pos", 5000), 42, "positional threadFunc/threadArg");
    thr.close();
    return true;
});

t("exec - options-object form", function(){
    var thr = new rampart.thread();
    thr.exec({ threadFunc: function(x){ rampart.thread.put("ex_opt", x*2); }, threadArg: 21 });
    t.mustEq(thread.get("ex_opt", 5000), 42, "options form threadFunc/threadArg");
    thr.close();
    return true;
});

t("exec - threadArg passes any type (object)", function(){
    var thr = new rampart.thread();
    thr.exec(function(o){ rampart.thread.put("ex_obj", o.a + o.b); }, {a:10, b:5});
    t.mustEq(thread.get("ex_obj", 5000), 15, "object threadArg");
    thr.close();
    return true;
});

t("exec - threadDelay defers execution ~N ms", function(){
    var thr = new rampart.thread();
    var start = Date.now();
    thr.exec({ threadFunc: function(){ rampart.thread.put("ex_delay", Date.now()); }, threadDelay: 400 });
    var when = thread.get("ex_delay", 5000);
    var elapsed = when - start;
    t.must(elapsed >= 350, "threadDelay honored (elapsed " + elapsed + "ms >= ~400)");
    thr.close();
    return true;
});

t("exec - only globals set BEFORE creation are copied to the thread", function(){
    global.ex_copied = "yes";          // set before thread creation -> copied
    var thr = new rampart.thread();
    global.ex_notcopied = "later";     // set after creation -> NOT copied
    thr.exec(function(){
        rampart.thread.put("ex_globals", {
            copied:    (typeof ex_copied !== "undefined") ? ex_copied : "(undef)",
            notcopied: (typeof ex_notcopied !== "undefined") ? ex_notcopied : "(undef)"
        });
    });
    var r = thread.get("ex_globals", 5000);
    t.mustEq(r.copied, "yes", "global set before creation is visible");
    t.mustEq(r.notcopied, "(undef)", "global set after creation is not visible");
    delete global.ex_copied; delete global.ex_notcopied;
    thr.close();
    return true;
});

t("exec - callbackFunc receives return value (err undefined)", function(){
    /* exec+callback nested in a live worker so the callback's loop runs */
    var outer = new rampart.thread();
    outer.exec(function(){
        var sub = new rampart.thread();
        sub.exec(function(x){ return x * 2; }, 21,
            function(val, err){
                rampart.thread.put("cb_val", val);
                rampart.thread.put("cb_errUndef", (err === undefined));
            });
    });
    t.mustEq(thread.get("cb_val", 5000), 42, "callback got threadFunc return value");
    t.mustEq(thread.get("cb_errUndef", 5000), true, "callback err is undefined on success");
    outer.close();
    return true;
});

t("exec - callbackFunc receives error when threadFunc throws", function(){
    var outer = new rampart.thread();
    outer.exec(function(){
        var sub = new rampart.thread();
        sub.exec(function(){ throw new Error("boom-xyz"); }, null,
            function(val, err){
                rampart.thread.put("cb2_valUndef", (val === undefined));
                rampart.thread.put("cb2_errMsg", (err && err.message) ? err.message : String(err));
            });
    });
    t.mustEq(thread.get("cb2_valUndef", 5000), true, "value undefined on error");
    t.mustContain(thread.get("cb2_errMsg", 5000), "boom-xyz", "callback error carries the thrown message");
    outer.close();
    return true;
});

/* =================== thr.close() =================== */

t("close - returns undefined and releases a persistent thread", function(){
    var thr = new rampart.thread(true);
    thr.exec(function(){ rampart.thread.put("cl_ran", 1); });
    t.mustEq(thread.get("cl_ran", 5000), 1, "thread ran");
    t.mustEq(thr.close(), undefined, "close() returns undefined");
    return true;
});

/* =================== thr.terminate() =================== */

t("terminate - forcibly shuts down a thread kept alive by onGet", function(){
    var thr = new rampart.thread();
    thr.exec(function(){
        rampart.thread.onGet("term_cmd", function(key,val){
            rampart.thread.put("term_got", val);
        });
        rampart.thread.put("term_ready", 1);   // subscription registered
    });
    t.mustEq(thread.get("term_ready", 5000), 1, "worker subscribed");
    thread.put("term_cmd", "hi");
    t.mustEq(thread.get("term_got", 5000), "hi", "onGet fired in worker");
    /* close() would hang (onGet keeps loop non-empty); terminate() must not */
    t.mustEq(thr.terminate(), undefined, "terminate() returns undefined");
    t.mustEq(thr.terminate(), undefined, "second terminate() is a safe no-op");
    return true;
});

/* =================== thr.getId / getCurrentId =================== */

t("getId / getCurrentId - thread id consistency", function(){
    var thr = new rampart.thread();
    var id = thr.getId();
    t.mustEq(typeof id, "number", "getId returns a Number");
    t.must(id > 0, "child thread id is a positive integer");
    thr.exec(function(){ rampart.thread.put("gid_cur", rampart.thread.getCurrentId()); });
    t.mustEq(thread.get("gid_cur", 5000), id, "getCurrentId inside thread == thr.getId()");
    t.mustEq(thread.getCurrentId(), 0, "main thread getCurrentId is 0");
    thr.close();
    return true;
});

/* =================== put / get =================== */

t("put/get - returns a value put to the clipboard", function(){
    thread.put("pg_simple", {n:5, s:"hi"});
    t.mustEq(thread.get("pg_simple"), {n:5, s:"hi"}, "get returns put value");
    return true;
});

t("put/get - get returns a deep copy (independent of original)", function(){
    var orig = {arr:[1,2], nested:{v:1}};
    thread.put("pg_copy", orig);
    orig.arr.push(3);          // mutate AFTER put
    orig.nested.v = 99;
    var got = thread.get("pg_copy");
    t.mustEq(got.arr.length, 2, "array copy unaffected by later mutation");
    t.mustEq(got.nested.v, 1, "nested copy unaffected by later mutation");
    return true;
});

t("put/get - get with timeout waits for a worker to set it", function(){
    var thr = new rampart.thread();
    thr.exec(function(){ rampart.utils.sleep(0.2); rampart.thread.put("pg_wait", "ready"); }, null);
    t.mustEq(thread.get("pg_wait", 5000), "ready", "get blocked until worker put the value");
    thr.close();
    return true;
});

t("put/get - get of an unset key returns undefined", function(){
    t.mustEq(thread.get("pg_never_set_xyz"), undefined, "unset key -> undefined");
    return true;
});

/* =================== onGet =================== */

t("onGet - exact key, glob, callback args, this==ev, remove()", function(){
    /* NOTE: ack keys (z_*) must NOT match the watched glob (ogm_*) or the
       callback's own put would re-trigger the watcher -> infinite recursion. */
    var thr = new rampart.thread();
    thr.exec(function(){
        var ev = rampart.thread.onGet("ogm_*", function(key, val, match){
            rampart.thread.put("z_glob_last", {key:key, val:val, match:match, isEv:(this===ev)});
            if (val >= 3) {
                rampart.thread.put("z_done", {count:val});
                this.remove();
            }
        });
        rampart.thread.onGet("ogx_one", function(key, val, match){
            rampart.thread.put("z_exact_last", {key:key, val:val, match:match});
        });
        rampart.thread.put("z_ready", 1);
    });
    t.mustEq(thread.get("z_ready", 5000), 1, "subscriptions registered");

    thread.put("ogm_a", 1);     thread.get("z_glob_last", 5000);  /* sync each step */
    rampart.utils.sleep(0.05);
    thread.put("ogx_one", 2);   thread.get("z_exact_last", 5000);
    rampart.utils.sleep(0.05);
    thread.put("ogm_b", 3);

    var done = thread.get("z_done", 5000);
    t.mustEq(done.count, 3, "glob onGet saw the final value");
    var gl = thread.get("z_glob_last");
    t.mustEq(gl.match, "ogm_*", "glob match string passed to callback");
    t.mustEq(gl.isEv, true, "this === returned event object");
    var ex = thread.get("z_exact_last");
    t.mustEq(ex.key, "ogx_one", "exact key passed to callback");
    t.mustEq(ex.val, 2, "exact value passed to callback");
    t.mustEq(ex.match, "ogx_one", "exact match string passed to callback");
    thr.terminate();
    return true;
});

t("onGet - remove() stops further callbacks", function(){
    var thr = new rampart.thread();
    thr.exec(function(){
        var n = 0;
        var ev = rampart.thread.onGet("ogr_key", function(key, val){
            n++;
            rampart.thread.put("ogr_count", n);
            ev.remove();   // remove after first
        });
        rampart.thread.put("ogr_ready", 1);
    });
    t.mustEq(thread.get("ogr_ready", 5000), 1, "subscribed");
    thread.put("ogr_key", 1);
    t.mustEq(thread.get("ogr_count", 5000), 1, "fired once");
    rampart.utils.sleep(0.2);
    thread.put("ogr_key", 2);     // should NOT fire (removed)
    rampart.utils.sleep(0.2);
    t.mustEq(thread.get("ogr_count"), 1, "count still 1 after remove()");
    thr.terminate();
    return true;
});

/* =================== del =================== */

t("del - returns the value and removes it from the clipboard", function(){
    thread.put("del_key", 99);
    t.mustEq(thread.del("del_key"), 99, "del returns the stored value");
    t.mustEq(thread.get("del_key"), undefined, "key removed after del");
    return true;
});

/* =================== waitfor =================== */

t("waitfor - times out and returns undefined when no update", function(){
    var start = Date.now();
    var r = thread.waitfor("wf_none_xyz", 300);
    t.mustEq(r, undefined, "timeout -> undefined");
    t.must(Date.now() - start >= 250, "waited ~timeout before returning");
    return true;
});

t("waitfor - returns value when a worker updates it (edge-triggered)", function(){
    var thr = new rampart.thread();
    thr.exec(function(){ rampart.utils.sleep(0.2); rampart.thread.put("wf_upd", "changed"); });
    t.mustEq(thread.waitfor("wf_upd", 5000), "changed", "waitfor returns on update");
    thr.close();
    return true;
});

t("waitfor - waits even when key already defined (returns only on change)", function(){
    thread.put("wf_pre", "first");
    /* already defined; waitfor must NOT return immediately -> times out */
    t.mustEq(thread.waitfor("wf_pre", 300), undefined, "waitfor ignores existing value, times out");
    return true;
});

/* =================== lock =================== */

t("lock - new lock returns object with lock/unlock/trylock", function(){
    var lk = new rampart.lock();
    t.mustEq(typeof lk.lock, "function", "has lock");
    t.mustEq(typeof lk.unlock, "function", "has unlock");
    t.mustEq(typeof lk.trylock, "function", "has trylock");
    return true;
});

t("lock - trylock true when free, false when held by another thread", function(){
    var lk = new rampart.lock();
    var thr = new rampart.thread();
    /* worker grabs the lock and holds it until told to release */
    thr.exec(function(lock){
        lock.lock();
        rampart.thread.put("lk_held", 1);
        rampart.thread.waitfor("lk_release", 5000);
        lock.unlock();
        rampart.thread.put("lk_released", 1);
    }, lk);
    t.mustEq(thread.get("lk_held", 5000), 1, "worker holds the lock");
    t.mustEq(lk.trylock(), false, "trylock fails while another thread holds it");
    thread.put("lk_release", 1);
    t.mustEq(thread.get("lk_released", 5000), 1, "worker released");
    t.mustEq(lk.trylock(), true, "trylock succeeds once free");
    lk.unlock();
    thr.close();
    return true;
});

t("lock - mutual exclusion: two threads increment shared counter", function(){
    var lk = new rampart.lock();
    thread.put("lk_i", 0);
    var thr1 = new rampart.thread();
    var thr2 = new rampart.thread();
    /* each worker increments 50x under the lock, then signals its done key */
    thr1.exec(function(lock){
        var i, j;
        for (j=0;j<50;j++){ lock.lock(); i=rampart.thread.get("lk_i"); i++; rampart.thread.put("lk_i", i); lock.unlock(); }
        rampart.thread.put("lk_done1", 1);
    }, lk);
    thr2.exec(function(lock){
        var i, j;
        for (j=0;j<50;j++){ lock.lock(); i=rampart.thread.get("lk_i"); i++; rampart.thread.put("lk_i", i); lock.unlock(); }
        rampart.thread.put("lk_done2", 1);
    }, lk);
    t.mustEq(thread.get("lk_done1", 10000), 1, "thread 1 finished");
    t.mustEq(thread.get("lk_done2", 10000), 1, "thread 2 finished");
    t.mustEq(thread.get("lk_i"), 100, "no lost updates: 50 + 50 == 100");
    thr1.close(); thr2.close();
    return true;
});

t.exit();
