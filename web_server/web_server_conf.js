
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

/*
   a convenient global object to hold
   locations that we might need to access in
   modules
*/ 

var serverConf = {
    bindPort:   8088,
    htmlRoot:   process.scriptPath + "/html/",
    appsRoot:   process.scriptPath + "/apps/",
    wsappsRoot: process.scriptPath + "/wsapps/",
    user:       "nobody"  //only if started as root, switch to this user
}

/* the array holding the ip:port pairs we will bind to */
var bind = [  `0.0.0.0:${serverConf.bindPort}`, `[::]:${serverConf.bindPort}` ];

/* 
   here, we are either "root" (necessary if binding to port 80)
   or we are an unprivileged user ("rampart")
*/
var iam = trim(exec('whoami').stdout);

/* 
   Throw an error if we attempt to bind to port <1024 as something
   other than root.
*/
if(serverConf.bindPort < 1024 && iam != "root")
    throw("Error: script must be started as root to bind to port " + port);

/* check for /logs dir */

var ldir = stat(process.scriptPath + "/logs");

// doesnt exist
if (!ldir) {
    printf("Log folder/directory does not exist, creating \"" + process.scriptPath + "/logs\"\n");
    mkdir(process.scriptPath + "/logs");
// isn't a directory
} else if (!ldir.isDirectory) {
    fprintf(stderr, process.scriptPath + "/logs must be a directory\n");
    process.exit(1);
}

/*** CUSTOM 404 PAGE ***/
function notfound(req){
    return {
        status:404,
        html: `<html><head><title>404 Not Found</title></head>
                <body style="background: url(/images/page-background.png);">
                    <center>
                        <h1>Not Found</h1>
                        <p>
                            The requested URL${%H:req.path.path}
                            was not found on this server.
                        </p>
                        <p><img style="width:65%" src="/images/inigo-not-fount.jpg"></p>
                    </center>
                </body></html>`
    }
}

/* 
   DIRECTORY LISTINGS OF FILE SYSTEM FOLDERS:

   This function is the same as the internal one and is 
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

   In this script, the function is unused as directoryFunc is set
   to false below.
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




/****** START SERVER *******/
printf("Starting http server on port %d\n", serverConf.bindPort);
var serverpid=server.start(
{
    /* bind: string|[array,of,strings]
       default: [ "[::1]:8088", "127.0.0.1:8088" ] 
        ipv6 format: [2001:db8::1111:2222]:80
        ipv4 format: 127.0.0.1:80
        spaces are ignored (i.e. " [ 2001:db8::1111:2222 ] : 80" is ok)
    */
    /* use the following to bind to all ipv4 and ipv6 addresses */
    //bind: [ "[::]:8088", "0.0.0.0:8088" ],
    bind: bind,          // see top of script

    /* When binding to 80 or 443, must be started as root, 
       The server will run as serverConf.user after port is bound.  */
    user: serverConf.user,

    /* max time to spend running javascript callbacks */
    scriptTimeout: 20.0,

    /* how long to wait before client sends a req or server can send a response. 
       N/A for websockets.                                                        */
    connectTimeout:20.0,

    /* return javascript errors and "500 internal error"
       instead of "404 not found" when a JS error is thrown  */
    developerMode: true,

    /* turn logging on, by default goes to stdout/stderr */
    log: true,

    /* access log location, instead of stdout. Must be set if daemon==true && log==true */
    accessLog: process.scriptPath+"/logs/access.log",

    /* error log location, instead of stderr.  Must be set if daemon==true && log==true */
    errorLog:  process.scriptPath+"/logs/error.log",
    
    /* Fork and run in background. stdin and stderr are closed, 
       so any logging must go to a file.                          */
    daemon: true,

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
    //mimeMap: { "mp3": "audio/mp3" },

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
           INLINE FUNCTION:

           This example assumes a "function myinlinefunc(){ ...}" is
           declared above.
           Inline functions are set once and cannot be edited 
           while the server is running.
        */
        //"/inlinefunc.html":   myinlinefunc,

        /* 
           FUNCTION FROM A MODULE (external file)

           This example also uses the function from 
           a module named "single_function.js". Changes to a 
           {module: function} or files in {modulePath: path}
           while the server is running do not require a 
           server restart, and thus should be the preferred
           method of url-to-function mapping.
        */

        /* 
           Example from a module which returns a single function:
                module.exports=function(){...}
        */
        //"/single.html":       {module: "modules/single_function.js"},
                   /* or */
        //"/single.html":       {module: "modules/single_function"},

        /* 
            MATCHING A GLOB TO A FUNCTION:

            This assumes a JS module is located at "modules/mysamplescript.js"

            Notice the '*'.  These urls:
                    http://localhost:8088/myscript/ and
                    http://localhost:8088/myscript/show.html and
                    http://localhost:8088/myscript.html 
            all match this function.
        */
        //"/myscript*":          {module: "modules/mysamplescript.js"},

        /* 
           MATCHING MULTIPLE URLS IN ONE MODULE:

           Code from a module which returns an Object with values set to functions:
                module.exports={
                    "/"                  : indexpage,
                    "/index.html"        : indexpage,
                    "/page1.html"        : firstpage,
                    "/page2.html"        : secondpage,
                    "/virtdir/page3.html": thirdpage
                };

           These would map to:
                    http://localhost:8088/multi/
                    http://localhost:8088/multi/
                    http://localhost:8088/multi/page1.html
                    http://localhost:8088/multi/page2.html
                    http://localhost:8088/multi/virtdir/page3.html
        */
        //"/multi/":            {module: "modules/multi_function.js" },


        // SEE ALSO: the scripts in 'apps/test_modules/'.
    }
});

/*
  if daemon:false - server starts at the end of the script when event loop begins
  otherwise server forks and starts immediately, and var serverpid is the process id
*/

fprintf(process.scriptPath+"/server.pid", "%d", serverpid);

if(iam == "root")
    chown({user:serverConf.user, path:process.scriptPath+"/server.pid"});
