if(!global.wd) global.wd=process.scriptPath;
var conf = {
    //the defaults for full server

    /* ipAddr         String. The ipv4 address to bind   */
    //ipAddr:         '127.0.0.1',

    /* ipv6Addr       String. The ipv6 address to bind   */
    //ipv6Addr:       '[::1]',

    /* bindAll        Bool.   Set ipAddr and ipv6Addr to 0.0.0.0 and [::] respectively   */
    //bindAll:        false,

    /* ipPort         Number. Set ipv4 port   */
    //ipPort:         8088,

    /* ipv6Port       Number. Set ipv6 port   */
    //ipv6Port:       8088,

    /* port           Number. Set both ipv4 and ipv6 port   */
    //port:           -1,

    /* redirPort      Number. Launch http->https redirect server and set port   */
    //redirPort:      -1,

    /* redir          Bool.   Launch http->https redirect server and set to port 80   */
    //redir:          false,

    /* htmlRoot       String. Root directory from which to serve files   */
    //htmlRoot:       wd + '/html',

    /* appsRoot       String. Root directory from which to serve apps   */
    //appsRoot:       wd + '/apps',

    /* wsappsRoot     String. Root directory from which to serve websocket apps   */
    //wsappsRoot:     wd + '/wsapps',

    /* dataRoot       String. Setting for user scripts   */
    //dataRoot:       wd + '/data',

    /* logRoot        String. Log directory   */
    //logRoot:        wd + '/logs',

    /* accessLog      String. Log file name or null for stdout  */
    //accessLog:      wd + '/logs/access.log',

    /* errorLog       String. error log file name or null for stderr*/
    //errorLog:       wd + '/logs/error.log',

    /* log            Bool.   Whether to log requests and errors   */
    //log:            true,

    /* rotateLogs     Bool.   Whether to rotate the logs   */
    //rotateLogs:     false,

    /* rotateStart    String. Time to start log rotations   */
    //rotateStart:    '00:00',

    /* rotateInterval Number. Interval between log rotations in seconds or
                      String. One of "hourly", "daily" or "weekly"        */
    //rotateInterval: 86400,

    /* user           String. If started as root, switch to this user   */
    //user:           'nobody',

    /* threads        Number. Limit the number of threads used by the server.
                              Default (-1) is the number of cores on the system   */
    //threads:        -1,

    /* secure         Bool.   Whether to use https.  If true sslKeyFile and sslCertFile must be set   */
    //secure:         false,

    /* sslKeyFile     String. If https, the ssl/tls key file location   */
    //sslKeyFile:     '',

    /* sslCertFile    String. If https, the ssl/tls cert file location   */
    //sslCertFile:    '',

    /* developerMode  Bool.   Whether script errors result in 500 and return a stack trace.  Otherwise 404   */
    //developerMode:  true,

    /* letsencrypt    String. If using letsencrypt, the 'domain.tld' name for automatic setup of https
                              (sets secure true and looks for '/etc/letsencrypt/live/domain.tld/' directory)   */
    //letsencrypt:    '',

    /* rootScripts    Bool.   Whether to treat *.js files in htmlRoot as apps (not secure)   */
    //rootScripts:    false,

    /* directoryFunc  Bool.   Whether to provide a directory listing if no index.html is found   */
    //directoryFunc:  false,

    /* daemon         Bool.   whether to detach from terminal   */
    //daemon:         true,

    /* monitor':      Bool.   whether to launch monitor process to auto restart server if killed or crashes   */
    //monitor:        false,

    /* scriptTimeout  Number. Max time to wait for a script module to return a reply in seconds (default 20)   */
    //scriptTimeout:  20,

    /* connectTimeout Number. Max time to wait for client send request in seconds (default 20)   */
    //connectTimeout: 20,

    /* quickserver    Bool.   whether to load the alternate quickserver setting which serves 
                              files from serverRoot only and no apps or wsapps unless explicit    */
    //quickserver:    false,

    /* serverRoot     String.  base path for logs, htmlRoot, appsRoot and wsappsRoot.
    //serverRoot:     rampart.utils.realPath('.'),

    /* map            Object  Add to server path mappings. i.e. {'/mypath/myapp.html': myfunc}   */
    //map:            undefined,

    // mapOverride    Object  Ignore htmlRoot, appRoot and wsappsRoot and use this object for all mappings   */
    //mapOverride:    undefined,

    serverRoot:       wd
}
