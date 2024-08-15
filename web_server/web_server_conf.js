#!/usr/bin/env rampart

var wserv = require("rampart-webserver");
var wd = process.scriptPath;

var conf = {
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

    /* port            Number. Set both ipv4 and ipv6 port   */
    //port:            -1,

    /* redirPort       Number. Launch http->https redirect server and set port   */
    //redirPort:       -1,

    /* redir           Bool.   Launch http->https redirect server and set to port 80   */
    //redir:           false,

    /* htmlRoot        String. Root directory from which to serve files   */
    //htmlRoot:        wd + '/html',

    /* appsRoot        String. Root directory from which to serve apps   */
    //appsRoot:        wd + '/apps',

    /* wsappsRoot      String. Root directory from which to serve websocket apps   */
    //wsappsRoot:      wd + '/wsapps',

    /* dataRoot        String. Setting for user scripts   */
    //dataRoot:        wd + '/data',

    /* logRoot         String. Log directory   */
    //logRoot:         wd + '/logs',

    /* accessLog       String. Log file name or null for stdout  */
    //accessLog:       wd + '/logs/access.log',

    /* errorLog        String. error log file name or null for stderr*/
    //errorLog:        wd + '/logs/error.log',

    /* log             Bool.   Whether to log requests and errors   */
    //log:             true,

    /* rotateLogs      Bool.   Whether to rotate the logs   */
    //rotateLogs:      false,

    /* rotateStart     String. Time to start log rotations   */
    //rotateStart:     '00:00',

    /* rotateInterval  Number. Interval between log rotations in seconds or
                       String. One of "hourly", "daily" or "weekly"        */
    //rotateInterval:  86400,

    /* user            String. If started as root, switch to this user   */
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

    /* developerMode   Bool.   Whether script errors result in 500 and return a stack trace.  Otherwise 404   */
    //developerMode:   true,

    /* letsencrypt     String. If using letsencrypt, the 'domain.tld' name for automatic setup of https
                               (sets secure true and looks for '/etc/letsencrypt/live/domain.tld/' directory)   */
    //letsencrypt:     '',

    /* rootScripts     Bool.   Whether to treat *.js files in htmlRoot as apps (not secure)   */
    //rootScripts:     false,

    /* directoryFunc   Bool.   Whether to provide a directory listing if no index.html is found   */
    //directoryFunc:   false,

    /* daemon          Bool.   whether to detach from terminal   */
    //daemon:          true,

    /* monitor':       Bool.   whether to launch monitor process to auto restart server if killed or crashes   */
    //monitor:         false,

    /* scriptTimeout   Number. Max time to wait for a script module to return a reply in seconds (default 20)   */
    //scriptTimeout:   20,

    /* connectTimeout  Number. Max time to wait for client send request in seconds (default 20)   */
    //connectTimeout:  20,

    /* quickserver     Bool.   whether to load the alternate quickserver setting which serves 
                               files from serverRoot only and no apps or wsapps unless explicit    */
    //quickserver:     false,

    /* serverRoot      String.  base path for logs, htmlRoot, appsRoot and wsappsRoot.
    //serverRoot:      rampart.utils.realPath('.'),

    /* map             Object  Add to server path mappings. i.e. {'/mypath/myapp.html': myfunc}   */
    //map:             undefined,

    /* mapOverride     Object  Ignore htmlRoot, appRoot and wsappsRoot and use this object for all mappings   */
    //mapOverride:     undefined,

    /* appendProcTitle Bool.  Whether to append ip:port to process name as seen in ps */
    //appendProcTitle: false,

    serverRoot:       wd,
}

var res, printf=rampart.utils.printf, argv=process.argv, kill=rampart.utils.kill;

// fill in the missing pieces and do some checks
conf = wserv.parseOptions(conf);

if(argv[2] == '--stop' || argv[2]=='stop') {

    /* STOP */
    res=wserv.stop(conf);
    if(res.error)
        printf("Server is not running or pid file is invalid\n");
    else if (res.message)
        printf("%s\n", res.message);
    process.exit(0);

} else if(argv[2] == '--restart' || argv[2]=='restart') {

    /* RESTART */
    res=wserv.stop(conf);
    if(res.error)
        printf("Server is not running or pid file is invalid\n");
    else if (res.message)
        printf("%s\n", res.message);

    res=wserv.start(conf);

    if(res.message)
        console.log(res.message);

    if(res.error) {
        console.log(res.error);
        process.exit(1);
    }

} else if(argv[2] == '--status' || argv[2]=='status') {

    /* STATUS */
    res=wserv.status(conf);

    if( res.serverPid && kill(res.serverPid,0) )
        printf("server is running. pid: %s\n", res.serverPid);
    else
        printf("server is not running\n");

    if( res.redirPid && kill(res.redirPid,0) )
        printf("redirect server is running. pid: %s\n", res.redirPid);
    else
        printf("redirect server is not running\n");

    if( res.monitorPid && kill(res.monitorPid,0) )
        printf("monitor process is running. pid: %s\n", res.monitorPid);
    else
        printf("monitor process is not running\n");

} else if (argv[2] == '--dump' || argv[2]=='dump') {

    /* DUMP */
    res=wserv.dumpConfig(conf);
    printf("%3J\n", res);
    process.exit(0);

} else if (argv[2] == '--start' || argv[2]=='start' || !argv[2]) {

    /* START */
    res=wserv.start(conf);

    if(res.message)
        console.log(res.message);

    if(res.error) {
        console.log(res.error);
        process.exit(1);
    }
    // if (res.isMonitor) -- we are the monitor and should on nothing else but finish the script
    //                       so event loop can start and monitor can run its setTimeouts

} else { 
    if (argv[2] != '--help' && argv[2] != 'help')
	printf("unknown command '%s'\n\n", argv[2]);
    printf("usage:\n  %s %s [start|stop|restart|status|dump]\n",argv[0], argv[1]);
    printf("      start   -- start the http(s) server\n");
    printf("      stop    -- stop the http(s) server\n");
    printf("      restart -- stop and restart the http(s) server\n");
    printf("      status  -- show status of server processes\n");
    printf("      dump    -- dump the config object used for server.start()\n");
}
