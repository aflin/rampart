/* *******************************************************************
     This example demonstrates most of the functionality of the http server

     see also scripts in the ./servermods/ directory
   ******************************************************************* */


/* 
    Put printf et. al. into the global namespace
    For convenience so that one can use
        printf("...")
    in place of
        rampart.utils.printf("...");
*/
rampart.globalize(rampart.utils);

/* load the http server module */
var server=require("rampart-server");


/* this function is the same as the internal one and is 
   provided so that you can make alterations to the default.

   The choice between no dir list vs internal vs this script is 
   set in: 
       server.start({
           ...,
           directoryFunc:[true|false|function]
       });

   If the standard directory listing is sufficient for your
   needs, you can delete this function and change 
   {directoryFunc: true} below
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



function myinlinefunc(req) {
    var ret = "<html><body><h1>Hello World</h1><p>Hello world from myinlinefunc()</p></body></html>";
    
    return {html: ret };
}



/* ********************************************************************* 
    Configuration and Start of Webserver:

    By default, the server is run in a single thread.  If useThreads is set to true,
    the server will start with one thread per cpu core, or with the number configured 

    Each thread has its own JS interpreter/stack/heap.
    Global variables and ECMA functions from the main thread will be copied
    to all of the server threads.  Modules loaded before server starts
    will be available in each thread as well. Non global variables
    that are in scope will not be copied.

    Functions are copied as compiled bytecode. Not all functionality will 
    transfer.  See the Bytecode limitations section here: 
    https://github.com/svaarala/duktape/blob/master/doc/bytecode.rst

    Best practice for full and expected functionality and flexibility is to 
    define callbacks using {module: function} or {modulePath: path}

    All limitations apply regardless of whether the server is single or multi-threaded.
*/

var pid=server.start(
{
    /* bind: string|[array,of,strings]
       default: [ "[::1]:8088", "127.0.0.1:8088" ] 
        ipv6 format: [2001:db8::1111:2222]:80
        ipv4 format: 127.0.0.1:80
        spaces are ignored (i.e. " [ 2001:db8::1111:2222 ] : 80" is ok)
    */
    bind: [ "[::]:8088", "0.0.0.0:8088" ], /* bind to all */

    /* if started as root, you must set user.  
       If not root, option "user" is ignored. */
    user: "nobody",

    scriptTimeout: 10.0, /* max time to spend in JS */
    connectTimeout:20.0, /* how long to wait before client sends a req or server can send a response */

    /*** logging ***/
    log: true,           //turn logging on, by default goes to stdout/stderr
    //accessLog: "./access.log",    //access log location, instead of stdout. Must be set if daemon==true
    //errorLog: "./error.log",     //error log location, instead of stderr. Must be set if daemon==true
    
    /*  fork and continue after server start (see end of the script) */
    //daemon: true, // fork and run in background. stdin and stderr are closed, so logging must go to a file.

    /* uncomment to make server single-threaded. */
    //useThreads: false,
    /*  By default, number of threads is set to cpu core count.
        "threads" has no effect unless useThreads is set true.
        The number can be changed here:
    */
    //threads: 8, /* for a 4 core, 8 virtual core hyper-threaded processor. */

    /* 
        for https support, these three are the minimum number of options needed:
    */
//    secure:true,
//    sslKeyFile:  "/etc/letsencrypt/live/mydom.com/privkey.pem",
//    sslCertFile: "/etc/letsencrypt/live/mydom.com/fullchain.pem",

    /* sslMinVersion (ssl3|tls1|tls1.1|tls1.2). "tls1.2" is default*/
    // sslMinVersion: "tls1.2",

    /* a custom 404 page */
    notFoundFunc: function(req){
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
    },    

    /* if a function is given, directoryFunc will be called each time a url
        which corresponds to a directory is called if there is no index.htm(l)
        present in the directory.  Added to the normal request object
        will be the property (string) "fsPath" (req.fsPath), which can be used
        to create a directory listing.  See function dirlist() above.
        It is substantially equivelant to the built-in server.defaultDirList function.

        If directoryFunc is not set, a url pointing to a directory without an index.htm(l)
        will return a 403 Forbidden error.

        these two lines perform the same function (in this script):
    */
    directoryFunc: dirlist,
    // or
    //directoryFunc: true, //use default directory list function

    /* **********************************************************
       map urls to functions or paths on the filesystem 
       If it ends in a '/' then matches everything in that path
       except a more specific ('/something.html') path
       
       priority is given to Exact Paths (Begins with '/' and no '*' in path), then
         regular expressions, then globs.
         
       If mapSort: false, then in each of these groups
         is left unsorted.
       Otherwise they are then ordered by length, with longest 
         having priority.
       If you wish to specify your own priority, set:

    mapSort: false,

       and then put them in your prefered order below.
       ********************************************************** */
    map:
    {
        /* ************************************************
            filesystem mappings are always folders. Thus:
               "/tetris"    becomes  "/tetris/
               "./mPurpose" becomes  "./mPurpose/"
        **************************************************** */
        "/":                  process.scriptPath + "/mPurpose/",
        "/tetris/":            process.scriptPath + "/tetris-tutorial",

        /* *************************************************
             url to function mappings 
        **************************************************** */


        /* 
           inline functions are set once and cannot be edited 
           while the server is running
        */
        "/inlinefunc.html":  myinlinefunc,

        /* 
            matching a glob to a function

            Notice the '*'.  It will match:
              http://localhost:8088/showreq/ and
              http://localhost:8088/showreq/show.html and
              http://localhost:8088/showreq.html 
            all match this function
        
        */
        /* 
            This example also uses the function from 
             the module "servermod.js". Changes to a 
             {module: function} or files in {modulePath: path}
             while the server is running do not require a 
             server restart, and thus should be the preferred
             method of url-to-function mapping.
        */
        "/showreq*":          {module:"showreq"},
        "/scripts/":          {module:"servermod"},

        /* regular expressions can also be used. prepend a '~' to
           your expression: e.g.:

        "~/show[rR]eq.*":    {module:"servermod"},

          see https://github.com/kkos/oniguruma/blob/master/doc/RE
          for full syntax
        */

        /* 
            Load modules dynamically from a path.
            **** File additions, deletions and changes do not 
             require a server restart. ****

            see files in ./servermods/ directory.
            
        */

        "/modtest/":	      {modulePath: process.scriptPath + "/servermods/"}

    }
});

/* 
    If daemon==true then server.start() returns the pid of the detached 
    process and begins immediately. 

    Otherwise server.start() returns the pid of the current process and 
    the server functions are processed from within the duktape main 
    event loop.  The main event loop starts at the end of the script.
*/

/* if daemon==true/forking, give the forked server a chance to print its info*/
//rampart.utils.sleep(0.2);

/* if daemon==true  - check that forked daemon is running */
if(rampart.utils.kill(pid,0))
    console.log("pid of rampart-server: " + pid);
else
{
    console.log("server start failed, pid = "+pid);
    process.exit(1);
}
printf("\ntry a url like http://127.0.0.1:8088/showreq.html?var1=val1&color=red&size=15\n");
printf("or see a sample website at http://127.0.0.1:8088/\n\nSTARTING SERVER:\n");
