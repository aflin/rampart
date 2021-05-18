var server=require("rampart-server");
rampart.globalize(rampart.utils);

//  IF YOU HAVE A CERT AND WANT SSL, SET THIS TRUE AND EDIT CERT LOCATION BELOW
//var secure=true;
var secure   = false;
var webuser  = "nobody";


if(secure)
    port = 443;
else
    port = 80;

var bind = [ `[::]:${port}`, `0.0.0.0:${port}` ];

var iam = trim(exec('whoami').stdout);

if(port < 1024 && iam != "root")
    throw("Error: script must be started as root to bind to port " + port);

/* startup scripts */
var scripts = readdir(process.scriptPath+"/startup_scripts");

for (var i=0; i<scripts.length; i++) {
    var mod;

    if(!/\.js$/.test(scripts[i]))
        continue;

    printf("running startup script ->     %s\n", scripts[i]);
    try {
        mod = require(process.scriptPath+"/startup_scripts/"+scripts[i]);
        mod();
    } catch (e) {
        printf("Error, file '%s' is not a module or contains errors\n%J\n",scripts[i],e);
    }
}

if (secure) {
    /* http -> https redirect server */
    try {
        var rserverpid=server.start(
        {
            bind: [ "[::]:80", "0.0.0.0:80" ],
            daemon: true,
            user: "rampart",
            map: {
                "/": function(req) 
                {
                    var newurl = "https://" + req.path.host + req.path.path;
                    return {
                        html:rampart.utils.sprintf
                            (
                                "<html><body><h1>302 Moved Temporarily</h1>"+
                                '<p>Document moved <a href="%s">here</a></p></body></html>',
                                newurl
                            ),
                        status:302,
                        headers: { "location": newurl}
                    }
                }
            }
        });
    } catch(e) {
        //server/daemon already forked if we get here.
        if(/could not bind/.test(e))
            console.log("redirect server already running?\n");
        else
            console.log(e);
        exit();//only fork exits
    }
}
/*** custom 404 page ***/
function notfound(req){
    return {
        status:404,
        html: `<html><head><title>404 Not Found</title></head>
                <body style="background: url(/img/page-background.png);">
                    <center>
                        <h1>Not Found</h1>
                        <p>
                            The requested URL${%H:req.path.path}
                            was not found on this server.
                        </p>
                        <p><img style="width:65%" src="/inigo-not-fount.jpg"></p>
                    </center>
                </body></html>`
    }
}


/****** START SERVER *******/
printf("Starting https server\n");
var serverpid=server.start(
{
    bind: bind,          // see top of script
    user: webuser,     // when binding to 80 or 443, must be started as root, change to user:nobody after load
    scriptTimeout: 5.0,  // max time to spend running javascript callbacks
    connectTimeout:20.0, // how long to wait before client sends a req or server can send a response. N/A for websockets.

    //log: true,                   // turn logging on, by default goes to stdout/stderr
    //accessLog: "./access.log",   // access log location, instead of stdout. Must be set if daemon==true
    //errorLog: "./error.log",     // error log location, instead of stderr. Must be set if daemon==true
    
    //daemon: true,       // fork and run in background. stdin and stderr are closed, so any logging must go to a file.
    //useThreads: false,  // if true, override and set threads:1
    //threads: 8,         // e.g for a 4 core, 8 virtual core hyper-threaded processor. Default is the # of system cores.

    secure:secure,
    sslKeyFile:  process.scriptPath+"/certs/privkey.pem",   // only if secure is true
    sslCertFile: process.scriptPath+"/certs/fullchain.pem", // only if secure is true

    /* sslMinVersion: (ssl3|tls1|tls1.1|tls1.2). "tls1.2" is default*/
    // sslMinVersion: "tls1.2",

    notFoundFunc: notfound, // custom 404 page/function defined above

    mimeMap: { "mp3": "audio/mp3" },

    //directoryFunc: true,  // use default directory list function. If false/unset, return 404 when there is no index.html

    map:
    {
        "/":                    process.scriptPath + "/html/",               // static file location
        "/apps/":	        {modulePath: process.scriptPath+"/apps/"},   // js scripts for normal GET/POST
        "ws://wsapps/":		{modulePath: process.scriptPath+"/wsapps/"}  // js scripts for websockets
    }
});

// if daemon:false - server starts at the end of the script when event loop begins
