/* make printf et. al. global */
rampart.globalize(rampart.utils);

var net = require("rampart-net");

function testFeature(name,test)
{
    var error=false;
    printf("testing net - %-54s - ", name);
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
        process.exit(1);
    }
    if(error) console.log(error);
}


var listeningcb=false;
var connectcb=false;
var dataok=false;
var data2ok=false;
var connectclose=false;
var timeoutcb=false;
var connectend=false;
var readycb=false;
var to_id;
var resolved=false;
var resolved_async=false;
var once = true;

var server = new net.Server(function(){
    listeningcb=true;    
});

server.on("connection", function(ssocket){
        connectcb=true;
        ssocket.on("data",function(d) {
            if(sprintf("%s",d)=="ok")
            {
                dataok=true;
                ssocket.write("ok");
            }
            else if (sprintf("%s",d)=="sleep") {
                sleep(0.5);
            }
        });
        ssocket.on("close", function(){
            connectclose=true;
        });

        ssocket.on("end", function(){
            connectend=true;
        });

        ssocket.once("data", function(d){
            if(sprintf("%s",d)=="ok")
                data2ok=true;
            else
                once=false;
        });

});

server.listen(
    {
        port: 8085,
        host: 'localhost'
    }
);

var socket = new net.Socket();

socket.on("connect", function(){
    this.write("ok");
    this.on("data", function(d) {
        if(sprintf("%s",d)=="ok")
            this.write("sleep");
    });
});

socket.on("ready", function(){
    readycb=true;
});


socket.on('timeout', function(){
    timeoutcb=true;
    socket.destroy();
    server.close();
});
socket.setTimeout(100);

// first connect
socket.connect(8085,'127.0.0.1');

var res = net.resolve("dns.google.")

if (res.ipv4 == "8.8.8.8" || res.ipv4 == "8.8.4.4")
    resolved=true;

net.resolve_async("one.one.one.one.", function(h){
    if (h.ipv4=="1.1.1.1" || h.ipv4=="1.0.0.1")
        resolved_async=true;
});


function test_all(){
    testFeature("Listening callback", listeningcb);
    testFeature("Connect callback", connectcb);
    testFeature("Data callback", dataok);
    testFeature("Second data callback", data2ok);
    testFeature("Data callback once", once);
    testFeature("Close callback", connectclose);
    testFeature("Read callback", readycb);
    testFeature("Timeout callback", timeoutcb);
    testFeature("End callback", connectend);
    testFeature("Resolve Sync", resolved);
    testFeature("Resolve Async", resolved_async);
    clearTimeout(to_id);
}

//do test a bit after close is done
function dotest() {
    setTimeout(test_all, 500)
}

socket.on("close", dotest);

// do test after 3 seconds even if "close" event above fails
to_id = setTimeout(test_all, 3000);
