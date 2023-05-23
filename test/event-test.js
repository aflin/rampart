/* make printf et. al. global */
rampart.globalize(rampart.utils);

var thread = rampart.thread;

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
    if(test){
        printf("testing event - %-52s - passed\n", name);
        fflush(stdout);
    } else {
        printf("testing event - %-52s - >>>>> FAILED <<<<<\n", name);
        if(error) console.log(error);
        process.exit(1);
    }
    if(error) console.log(error);
}


var usr_var = "Basic Functionality";

function myCallback (uservar,triggervar){

    if(triggervar > 5)
        testFeature(`${uservar} remove event`, false);

    if(triggervar>4)
    {
        rampart.event.remove("myev");
        testFeature(uservar, usr_var == uservar);
        if(!thread.getId())
            do_thread_test();
        return;
    }

    //printf("recall %d in %d\n", triggervar+1, thread.getId());
    rampart.event.trigger("myev", triggervar+1);
}

rampart.event.on("myev", "myfunc", myCallback, usr_var);

rampart.event.trigger("myev", 1);


function do_thread_test() {

    usr_var = "Basic Use in thread";

    var thr1 = new thread();
    thr1.exec(
        function(uv) {
            rampart.event.on("myev", "myfunc3", myCallback, uv);
        },
        usr_var,
        //trigger in callback to make sure event is registered in thread
        function(){
            rampart.event.trigger("myev", 1);
        }
    );
}

var lock = new rampart.lock();

function multi_test(msg, tmsg) {
    var count;
    //console.log(`thread ${thread.getId()} - removing ${msg}, triggered ${tmsg}`);
    rampart.event.off("myev2", msg);

    lock.lock();
    count=thread.get("count")
    if(!count) count=1;
    else count++;
    thread.put("count",count);
    if(count == 2)
      testFeature("Multiple threads - success", true);      
    lock.unlock();
}

var thr2 = new thread();
var thr3 = new thread();
var thr4 = new thread();

thr2.exec(function(){
    rampart.event.on("myev2", "myfunc", multi_test, "myfunc");
});

thr3.exec(function(){
    rampart.event.on("myev2", "myfunc2", multi_test, "myfunc2");
});


thr4.exec(function(){
    testFeature("Multiple threads - start ...", true);
    rampart.event.trigger("myev2", "from thread 4");
});



