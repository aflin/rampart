#!/usr/bin/env rampart

/* 
the server can be started by running:
  rampart web_server_config.js
         or
  rampart web_server_config.js start

Help:
  ./web_server_conf.js help
  usage:
    rampart web_server_conf.js [start|stop|restart|letssetup|status|dump|help]
        start     -- start the http(s) server
        stop      -- stop the http(s) server
        restart   -- stop and restart the http(s) server
        letssetup -- start http only to allow letsencrypt verification
        status    -- show status of server processes
        dump      -- dump the config object used for server.start()
        help      -- show this message
*/

//set working directory to the location of this script
var working_directory = process.scriptPath;

/* ****************************************************** *
 *  UNCOMMENT AND CHANGE DEFAULTS BELOW TO CONFIG SERVER  *
 * ****************************************************** */

var serverConf = {
    //the defaults for full server

    /* ipAddr          String. The ipv4 address to bind   */
    //ipAddr:          '127.0.0.1',

    /* ipv6Addr        String. The ipv6 address to bind   */
    //ipv6Addr:        '[::1]',

    /* bindAll         Bool.   Set ipAddr and ipv6Addr to 0.0.0.0 and [::] respectively   */
    //bindAll:         false,

    /* ipPort          Number. Set ipv4 port   */
    //ipPort:          8088,

    /* ipv6Port        Number. Set ipv6 port   */
    //ipv6Port:        8088,

    /* port            Number. Set both ipv4 and ipv6 port if > -1   */
    //port:            -1,

    /* htmlRoot        String. Root directory from which to serve files   */
    //htmlRoot:        working_directory + '/html',

    /* appsRoot        String. Root directory from which to serve apps   */
    //appsRoot:        working_directory + '/apps',

    /* wsappsRoot      String. Root directory from which to serve websocket apps   */
    //wsappsRoot:      working_directory + '/wsapps',

    /* dataRoot        String. Setting for user scripts   */
    //dataRoot:        working_directory + '/data',

    /* logRoot         String. Log directory   */
    //logRoot:         working_directory + '/logs',

    /* redirPort       Number. Launch http->https redirect server and set port if < -1  */
    //redirPort:       -1,

    /* redir           Bool.   Launch http->https redirect server and set to port 80   */
    //redir:           false,

    /* redirTemp       Bool. If true, and if redir is true or redirPort is set, send a
                             302 Moved Temporarily instead of a 301 Moved Permanently   */
    //redirTemp        false,

    /* accessLog       String. Log file name or null for stdout  */
    //accessLog:       working_directory + '/logs/access.log',

    /* errorLog        String. error log file name or null for stderr*/
    //errorLog:        working_directory + '/logs/error.log',

    /* log             Bool.   Whether to log requests and errors   */
    //log:             true,

    /* rotateLogs      Bool.   Whether to rotate the logs   */
    //rotateLogs:      false,

    /* rotateStart     String. Time to start log rotations   */
    //rotateStart:     '00:00',

    /* rotateInterval  Number. Interval between log rotations in seconds or
                       String. One of "hourly", "daily" or "weekly"        */
    //rotateInterval:  86400,

    /* user            String. If started as root, switch to this user
                               It is necessary to start as root if using ports < 1024   */
    //user:            'nobody',

    /* threads         Number. Limit the number of threads used by the server.
                               Default (-1) is the number of cores on the system   */
    //threads:         -1,

    /* secure          Bool.   Whether to use https.  If true sslKeyFile and sslCertFile must be set   */
    //secure:          false,

    /* sslKeyFile      String. If https, the ssl/tls key file location   */
    //sslKeyFile:      '',

    /* sslCertFile     String. If https, the ssl/tls cert file location   */
    //sslCertFile:     '',

    /* developerMode   Bool.   Whether JavaScript errors result in 500 and return a stack trace.
                               Otherwise errors return 404 Not Found                             */
    //developerMode:   true,

    /* letsencrypt     String. If using letsencrypt, the 'domain.tld' name for automatic setup of https
                               ( sets secure true and looks for '/etc/letsencrypt/live/domain.tld/' directory
                                 to set sslKeyFile and sslCertFile ).
                               ( also sets "port" to 443 ).                                                      */
    //letsencrypt:     "",     //empty string - don't configure using letsencrypt

    /* rootScripts     Bool.   Whether to treat *.js files in htmlRoot as apps
                               (not secure; don't use on a public facing server)      */
    //rootScripts:     false,

    /* directoryFunc   Bool.   Whether to provide a directory listing if no index.html is found   */
    //directoryFunc:   false,

    /* daemon          Bool.   whether to detach from terminal and run as a daemon  */
    //daemon:          true,

    /* monitor':       Bool.   whether to launch monitor process to auto restart server if
                               killed or unrecoverable error */
    //monitor:         false,

    /* scriptTimeout   Number. Max time to wait for a script module to return a reply in
                       seconds (default 20). Script callbacks normally should be crafted
                       to return in a reasonable period of time.  Timeout and reconstruction
                       of environment is expensive, so this should be a last resort fallback.   */
    //scriptTimeout:   20,

    /* connectTimeout  Number. Max time to wait for client send request in seconds (default 20)   */
    //connectTimeout:  20,

    /* quickserver     Bool.   whether to load the alternate quickserver setting which serves 
                               files from serverRoot only and no apps or wsapps unless 
                               explicity set                                                    */
    //quickserver:     false,

    /* serverRoot      String.  base path for logs, htmlRoot, appsRoot and wsappsRoot.
    //serverRoot:      rampart.utils.realPath('.'),  Note: here ere serverRoot is defined below

    /* map             Object.  Define filesystem and script mappings, set from htmlRoot,
                       appsRoot and wsappsRoot above.                                         */
    /*map:             {
                           "/":                working_directory + '/html',
                           "/apps/":           {modulePath: working_directory + '/apps'},
                           "ws://wsapps/":     {modulePath: working_directory + '/wsapps'}
                       }
                       // note: if this is changed, serverConf.htmlRoot defaults et al will not be used or correct.
    */

    /* appendMap       Object.  Append the default map above with more mappings
                       e.g - {"/images": working_directory + '/images'}
                       or  - {"myfunc.html" : function(req) { ...} }
                       or  - {
                                 "/images": working_directory + '/images',
                                 myfunc.html: {module: working_directory + '/myfuncmod.js'}
                             }                                                                 */
    //appendMap:       undefined,

    /* appendProcTitle Bool.  Whether to append ip:port to process name as seen in ps */
    //appendProcTitle: false,

    /* beginFunc       Bool/Obj/Function.  A function to run at the beginning of each JavaScript
                       function or on file load
                       e.g. -
       beginFunc:      {module: working_directory+'/apps/beginfunc.js'}, //where beginfunc.js is "modules.exports=function(req) {...}"
       or
       beginFunc:      myglobalbeginfunc,
       or
       beginFunc:      function(req) { ... }
       or
       beginFunc:      undefined|false|null  // begin function disabled

                       The function, like all server callback function takes
                       req, which if altered will be reflected in the call
                       of the normal callback for the requested page.
                       Returning false will skip the normal callback and
                       send a 404 Not Found page.  Returning an object (ie
                       {html:myhtml}) will skip the normal callback and send
                       that content.

                       For "file" `req.fsPath` will be set to the file being
                       retrieved.  If `req.fsPath` is set to a new path and
                       the function returns true, the updated file will be
                       sent instead.

                       For websocket connections, it is run only befor the
                       first connect (when req.count == 0)                    */
    //beginFunc:       false,

    /* beginFuncOnFile Whether to run the begin function before serving a
                       file (-i.e. files from the web_server/html/ directory)  */
    //beginFuncOnFile: false,

    /* endFunc         Bool/Obj/Function.  A function to run after each JavaScript function

                       Value (i.e. {module: mymod}) is the same as beginFunc above.

                       It will also receive the `req` object.  In addition,
                       `req.reply` will be set to the return value of the
                       normal server callback function and req.reply can be
                       modified before it is sent.

                       For websocket connections, it is run after websockets
                       disconnects and after the req.wsOnDisconnect
                       callback, if any.  `req.reply` is an empty object,
                       modifying it has no effect and return value from
                       endFunc has not effect.

                       End function is never run on file requests.                     */
    //endfunc:         false,

    /* logFunc         Function - a function to replace normal logging, if log:true set above
                       See two examples below.
                       -e.g.
                       logFunc: myloggingfunc,                                                 */
    //logFunc:         false,

    serverRoot:        working_directory,
}

/*  Example logging functions :
    logdata: an object of various individual logging datum
    logline: the line which would have been written but for logFunc being set

// example logging func - log output abbreviated if not 200
function myloggingfunc (logdata, logline) {
    if(logdata.code != 200)
        rampart.utils.fprintf(rampart.utils.accessLog,
            '%s %s "%s %s%s%s %d"\n',
            logdata.addr, logdata.dateStr, logdata.method,
            logdata.path, logdata.query?"?":"", logdata.query,
            logdata.code );
    else
        rampart.utils.fprintf(rampart.utils.accessLog,
            "%s\n", logline); 
}

// example logging func - skip logging for connections from localhost
function myloggingfunc_alt (logdata, logline) {
    if(logdata.addr=="127.0.0.1" || logdata.addr=="::1")
        return;
    rampart.utils.fprintf(rampart.utils.accessLog,
        "%s\n", logline);
}
*/


/* **************************************************** *
 *  process command line options and start/stop server  *
 * **************************************************** */
require("rampart-webserver").web_server_conf(serverConf);
