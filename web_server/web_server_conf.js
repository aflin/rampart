
// the http server module
var server=require("rampart-server");

/* 
    Put printf et. al. into the global namespace
    For convenience so that one can use
        printf("...")
    in place of
        rampart.utils.printf("...");
*/
rampart.globalize(rampart.utils);

// a message to print after server has started"
var message = "Go to http://localhost:8088/ to see the demos in this distribution.";

/*
   a convenient global object to hold configs and
   locations that we might need to access from
   within modules
*/ 

var serverConf = {
    // for localhost only
    ipAddr:       "127.0.0.1",
    ipv6Addr:     "[::1]",

    // bind to all IP Addresses
    //ipAddr:     "0.0.0.0",
    //ipv6Addr:   "[::]",

    ipPort:         8088,
    ipv6Port:       8088,
    htmlRoot:       process.scriptPath + "/html",
    appsRoot:       process.scriptPath + "/apps",
    wsappsRoot:     process.scriptPath + "/wsapps",
    dataRoot:       process.scriptPath + "/data",
    logRoot:        process.scriptPath+"/logs",
    accessLog:      process.scriptPath+"/logs/access.log",
    errorLog:       process.scriptPath+"/logs/error.log",
    logging:        false,
    rotateLogs:     false,
    rotateInterval: 86400,
    rotateStart:    "02:00",  //must be "HH:MM"
    user:           "nobody",
    detach:         true
}

/* For log rotate, the kill signal USR1 is used to reload log
   It derived automatically below.  However in the unlikely
   event that it doesn't work, set it here:
*/
//var usr1 = 10; // linux
//var usr1 = 30; // macos
var usr1;

/* the array holding the ip:port combos we will bind to */
var bind = [];

if(serverConf.ipAddr && serverConf.ipPort)
    bind.push(`${serverConf.ipAddr}:${serverConf.ipPort}`);

if(serverConf.ipv6Addr && serverConf.ipv6Port)
    bind.push(`${serverConf.ipv6Addr}:${serverConf.ipv6Port}`);
    
if(!bind.length)
    throw("No ip addr/port specified");

/* 
   here, we are either "root" (necessary if binding to port 80)
   or we are an unprivileged user (e.g - "nobody")
*/
var iam = trim(exec('whoami').stdout);

/* 
   Throw an error if we attempt to bind to port <1024 as something
   other than root.
*/
if(iam != "root") {
    if(serverConf.ipPort < 1024)
        throw("Error: script must be started as root to bind to IP port " + serverConf.ipPort);
    if(serverConf.ipv6Port < 1024)
        throw("Error: script must be started as root to bind to IPv6 port " + serverConf.ipv6Port);
}

/*** custom 404 page ***/
function notfound(req){
    return {
        status:404,
        html: `<html><head><title>404 Not Found</title></head>
                <body>
                    <center>
                        <h1>Not Found</h1>
                        <p>
                            The requested URL${%H:req.path.path}
                            was not found on this server.
                        </p>
                        <p><img style="width:65%" src="/images/inigo-not-found.jpg"></p>
                    </center>
                </body></html>`
    }
}

/* The dirlist function below is the same as the internal one and is
   provided so that you can make alterations to the default.

   The choice between no dir list vs internal vs this script is 
   set below in: 
       server.start({
           ...,
           directoryFunc:[true|false|function]
       });

   If the standard directory listing is sufficient for your
   needs, you can delete this function and change 
   {directoryFunc: true} below

   In this script, the function is unused as directoryFunc is not set
   below but could be set to this script by setting the following key:value
   pair in server.start({}) below:

     directoryFunc: dirlist,
   
   See directoryFunc setting below.

*/

function dirlist(req) {
    var html="<!DOCTYPE html>\n"+
        '<html><head><meta charset="UTF-8"><title>Index of ' + 
        req.path.path+ 
        "</title><style>td{padding-right:22px;}</style></head><body><h1>"+
        req.path.path+
        '</h1><hr><table>';

    function hsize(size) {
        var ret=sprintf("%d",size);
        if(size >= 1073741824)
            ret=sprintf("%.1fG", size/1073741824);
        else if (size >= 1048576)
            ret=sprintf("%.1fM", size/1048576);
        else if (size >=1024)
            ret=sprintf("%.1fk", size/1024); 
        return ret;
    }

    if(req.path.path != '/')
        html+= '<tr><td><a href="../">Parent Directory</a></td><td></td><td>-</td></tr>';
    readdir(req.fsPath).sort().forEach(function(d){
        var st=stat(req.fsPath+'/'+d);
        if (st.isDirectory())
            d+='/';
        html=sprintf('%s<tr><td><a href="%s">%s</a></td><td>%s</td><td>%s</td></tr>',
            html, d, d, st.mtime.toLocaleString() ,hsize(st.size));
    });
    
    html+="</table></body></html>";
    return {html:html};
}

var serverpid;

/* *******************************************
            LOG ROTATION
   ******************************************* */

var gzip = trim ( exec('which','gzip').stdout );

if(serverConf.logging && serverConf.rotateLogs) {

    var tdelay, mdelay, startTime;

    if (typeof serverConf.rotateInterval != 'number')
    {
        fprintf(stderr, "serverConf.rotateInterval == %J is invalid\n", serverConf.rotateInterval);
        process.exit(1);
    }

    if( serverConf.rotateInterval < 300 ) {
        fprintf(stderr, "serverConf.rotateInterval is set to less than 5 minutes, is that what your really want?\n");
        process.exit(1);
    }

    mdelay = serverConf.rotateInterval * 1000;

    try {
        startTime = scanDate(serverConf.rotateStart + " "+ dateFmt('%z'), "%H:%M %z");

        if( startTime < Date.now() )
            startTime = new Date(startTime.getTime() + 86400000);
    } catch(e) {
        fprintf(stderr, "Error parsing log rotation start time (%s): %J\n", serverConf.rotateStart,e);
        process.exit(1);
    }

    var now = new Date();
    tdelay = startTime.getTime() - now.getTime();

    if (!usr1) {
        try {
            usr1 = parseInt(shell("kill -l").stdout.match(/(\d+)\) SIGUSR1/)[1]);
        } catch (e){}
    }

    if ( typeof usr1 != 'number' ) {
        fprintf(stderr, "Error finding kill signal number for signal 'USR1'\n");
        process.exit(1);
    }

    var prevAbackup, prevEbackup;

    function rotateLogs() {
        var doARotate=false, doErotate=false;
        var ds = dateFmt('%Y-%m-%d-%H-%M-%S');
        var abackup = sprintf('%s-%s', serverConf.accessLog, ds);
        var ebackup = sprintf('%s-%s', serverConf.errorLog,  ds);

        if( stat(serverConf.accessLog) ){
            doARotate=true;
            try {
                rename( serverConf.accessLog, abackup);
            } catch(e) {
                fprintf(serverConf.errorLog, true, "Cannot rename accessLog: %J\n", e);
                doARotate=false;
            }
        }

        if( stat(serverConf.errorLog) ){
            doERotate=true;
            try {
                rename( serverConf.errorLog, ebackup);
            } catch(e) {
                fprintf(serverConf.errorLog, true, "Cannot rename errorLog: %J\n", e);
                doErotate=false;
            }
        }

        if(doARotate||doErotate)
            kill(serverpid, usr1);// close and reopen logs

        // gzip old ones in background
        if(gzip && (prevAbackup || prevEbackup) ) {
            shell(`${gzip} -q ${prevAbackup?prevAbackup:""} ${prevEbackup?prevEbackup:""}`, {background:true} );
        }

        prevAbackup=abackup;
        prevEbackup=ebackup;
    }

    // the first interval to get us to the specified rotateStart time.
    setTimeout( function(){

        // server forks before serverpid is set below.
        if( !serverpid ) {
            try {
                var pid = readFile(process.scriptPath+"/server.pid");
                serverpid = parseInt(readFile(process.scriptPath+"/server.pid",{returnString:true}));
            } catch(e) {}

            if(typeof serverpid != 'number' || Number.isNaN(serverpid) ) {
                fprintf(serverConf.errorLog, true, "Cannot get server pid. Log rotation failed.\n");
                return;
            }
        }

        rotateLogs();

        // subsequent intervals, spaced by rotateInterval
        var iv=setMetronome(rotateLogs, mdelay);

    }, tdelay);

}


/****** START SERVER *******/
printf("Starting https server\n");
serverpid=server.start(
{
    /* bind: string|[array,of,strings]
       default: [ "[::1]:8088", "127.0.0.1:8088" ] 
        ipv6 format: [2001:db8::1111:2222]:80
        ipv4 format: 127.0.0.1:80
        spaces are ignored (i.e. " [ 2001:db8::1111:2222 ] : 80" is ok)
    */
    /* use the following to bind to all ipv4 and ipv6 addresses */
    //bind: [ "[::]:8088", "0.0.0.0:8088" ],
    bind: bind,          // see top of script.

    /* When binding to 80 or 443, must be started as root, 
       Privileges will be dropped to user:rampart after port is bound.  */
    user: serverConf.user, //ignored if not root

    /* max time to spend running javascript callbacks */
    scriptTimeout: 20.0,

    /* how long to wait before client sends a req or server can send a response. 
       N/A for websockets.                                                        */
    connectTimeout:20.0, // how long to wait before client sends a req or server can send a response. N/A for websockets.

    /* return javascript errors and "500 internal error"
       instead of "404 not found" when a JS error is thrown  */
    developerMode: true,

    /* turn logging on, by default goes to stdout/stderr */
    log: serverConf.logging,

    /* access log location, instead of stdout. Must be set if daemon==true && log==true */
    accessLog: serverConf.accessLog,

    /* error log location, instead of stderr.  Must be set if daemon==true && log==true */
    errorLog:  serverConf.errorLog,
    
    /* Fork and run in background. stdin and stderr are closed, 
       so any logging must go to a file.                          */
    daemon: serverConf.detach,

    /* if false, override threads below and set threads:1 */
    //useThreads: false,

    /* e.g for a 4 core, 8 virtual core hyper-threaded processor. Default is the # of system cores. */
    //threads: 8,

    /* 
        for https support, these three are needed:
    */
    //secure:true,
    //sslKeyFile:  "/etc/letsencrypt/live/mydom.com/privkey.pem",
    //sslCertFile: "/etc/letsencrypt/live/mydom.com/fullchain.pem",

    /* sslMinVersion: (ssl3|tls1|tls1.1|tls1.2). "tls1.2" is default*/
    // sslMinVersion: "tls1.2",

    /* custom 404 page/function defined above */
    notFoundFunc: notfound, // custom 404 page/function defined above

    /* 
       adjust/override the default mime mappings.  The defaults can be found at
       https://rampart.dev/docs/rampart-server.html#key-to-mime-mappings
    */
    mimeMap: { "mp3": "audio/mp3" },

    /*
      use default directory list function. If false/unset, return 404 when there is no index.html. 
      See above and https://rampart.dev/docs/rampart-server.html#built-in-directory-function
    */
    //directoryFunc: true,
          /* or */
    //directoryFunc: dirlist,
          /* or the default */
    directoryFunc: false,
          
    map:
    {
        /* static html files location, set above to process.scriptPath + "/html/" */
        "/":                serverConf.htmlRoot,

        /* 
           Location of JS scripts for normal GET/POST requests, 
           set above to process.scriptPath + "/apps/" 
        */
        "/apps/":	        {modulePath: serverConf.appsRoot},

        /* 
           Location of JS scripts for websocket connections,
           set above to process.scriptPath + "/wsapps/"
        */
        "ws://wsapps/":		{modulePath: serverConf.wsappsRoot}

        /************** other mapping examples ************/

        /* 
           ***  Specifying an Inline Function:  ***

             Inline functions are set once and cannot be edited 
             while the server is running.
        */
        //"/inlinefunc.html":   function(req) {/*produce output here*/},

        /* 
           ***  Specifying a Global Function:  ***

             The line below assumes a "function myinlinefunc(){ ...}" is
             declared somewhere in this script.

             Global functions are set once and cannot be edited 
             while the server is running.
        */
        //"/globalfunc.html":   myglobalfunc,

        /* 
            ***  Matching a Glob to a Function:  ***
              The example below ("/myscript*") assumes a JS module is
              located at "modules/mysamplescript.js"

              Notice the '*'.  It will match:
                http://localhost:8088/myscript/ and
                http://localhost:8088/myscript/show.html and
                http://localhost:8088/myscript.html 
              all match this function
        */
        //"/myscript*":          {module: "modules/mysamplescript.js"},
                        /* or */
        //"/myscript*":          {module: "modules/mysamplescript"},

        /* 
            *** Specifying a Function or Functions from a Module:  ***

              There are two ways to specify the location of modules: Using
                the {module: file} syntax or the {modulePath: path} syntax.

              The former specifies a JavaScript script in single file while
              the latter specifies a folder/directory that may contain
              several files.  Any Changes to a file specified in
              {module: file} or files located in {modulePath: path}
              while the server is running do not require a server restart,
              and thus should be the preferred method of url-to-function
              mapping.
        */

        /* 
           *** Module File, single function example:  ***

             A module which returns a single function
             module.exports=function(){}              
        */
        //"/single.html":       {module: "apps/single_function.js"},

        /* 
           *** Module File, Multiple functions example:  ***

           Below is an example which sets an Object with keys set to URL
           paths and values set to functions in a hypothetical file named 
           "modules/multi_function.js":

                module.exports={
                    "/"                  : indexpage,
                    "/index.html"        : indexpage,
                    "/page1.html"        : firstpage,
                    "/page2.html"        : secondpage,
                    "/virtdir/page3.html": thirdpage
                };
           
           Where "indexpage", "firstpage", etc are functions in the 
           "modules/multi_function.js" script.

           This would map respectively to these URLs:
                    http://localhost:8088/multi/
                    http://localhost:8088/multi/index.html
                    http://localhost:8088/multi/page1.html
                    http://localhost:8088/multi/page2.html
                    http://localhost:8088/multi/virtdir/page3.html

           And below is how it would be specified in this map object:
        */
        //"/multi/":            {module: "apps/multi_function.js" },

    }
});

/*
  If daemon:false - server will start at the end of the script when event loop begins.
  Otherwise the server forks and starts immediately, and var serverpid is the process id
*/

fprintf(process.scriptPath+"/server.pid", "%d", serverpid);
if(iam == 'root')
  chown({user:serverConf.user, path:process.scriptPath+"/server.pid"});

sleep(0.5); //wait half a sec, so messages from forked server can print first in the case that logging is turned off.

if(!kill(serverpid, 0)) {
    printf("Failed to start webserver\n");
    process.exit(1);
}

printf(`Server has been started. ${message}
Server pid is ${serverpid}.  To stop server use kill as such:
   kill ${serverpid}
`);

if(serverConf.daemon) {
  // do not run setTimeout in parent process:
  process.exit(0);
}
