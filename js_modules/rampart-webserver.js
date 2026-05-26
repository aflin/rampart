/* When running from a single-file bundle (process.scriptPath === ':zip:'),
   writable defaults (logs, dataRoot, runtime pid files) cannot live inside
   the zip.  Substitute the directory next to the bundle binary so the user's
   web_server_conf.js doesn't need to know whether it's bundled or not.
   Read-only paths (htmlRoot, appsRoot, wsappsRoot) stay as ':zip:/...'. */
function _writableWd(wd) {
    if (wd !== ':zip:') return wd;
    var argv0 = process.argv0 || '';
    var rp = rampart.utils.realPath(argv0);
    if (!rp) return rampart.utils.getcwd();
    var idx = rp.lastIndexOf('/');
    if (idx < 0) return rampart.utils.getcwd();
    return rp.substring(0, idx);
}

var defaultServerConf = function(wd){
    var ww = _writableWd(wd);
    return {
        ipAddr:       '127.0.0.1',
        ipv6Addr:     '[::1]',
        bindAll:        false,
        ipPort:         8088,
        ipv6Port:       8088,
        port:           -1,
        redirPort:      -1,
        redir:          false,
        htmlRoot:       wd + '/html',
        appsRoot:       wd + '/apps',
        wsappsRoot:     wd + '/wsapps',
        dataRoot:       ww + '/data',
        logRoot:        ww + '/logs',
        accessLog:      ww + '/logs/access.log',
        errorLog:       ww + '/logs/error.log',
        log:            true,
        rotateLogs:     false,
        rotateInterval: 86400,
        rotateStart:    '00:00',
        rotateCount:    30,
        user:           'nobody',
        threads:        -1,
        sslKeyFile:     '',
        sslCertFile:    '',
        secure:         false,
        developerMode:  true,
        letsencrypt:    '',
        rootScripts:    false,
        directoryFunc:  false,
        monitor:        false,
        daemon:         true,
        scriptTimeout:  20,
        connectTimeout: 20,
        quickserver:    false,
        appendProcTitle:false,
        beginFunc:      false,
        beginFuncOnFile:false,
        endFunc:        false,
        irohProxy:      false,
        selfSign:       false,
        defaultCharset: "utf-8",
        serverRoot:     wd
    }
}

var defaultQuickServerConf = function(wd){
    return {
        ipAddr:         '127.0.0.1',
        ipv6Addr:       '[::1]',
        bindAll:        false,
        ipPort:         8088,
        ipv6Port:       8088,
        port:           -1,
        redirPort:      -1,
        redir:          false,
        htmlRoot:       wd + '/',
        appsRoot:       '',
        wsappsRoot:     '',
        dataRoot:       '',
        logRoot:        wd+'/logs',
        accessLog:      "",
        errorLog:       "",
        log:            false,
        rotateLogs:     false,
        rotateInterval: 86400,
        rotateStart:    '00:00',
        rotateCount:    30,
        user:           'nobody',
        threads:        1,
        sslKeyFile:     '',
        sslCertFile:    '',
        secure:         false,
        developerMode:  true,
        letsencrypt:    '',
        rootScripts:    false,
        directoryFunc:  true,
        monitor:        false,
        daemon:         false,
        scriptTimeout:  20,
        connectTimeout: 20,
        quickserver:    true,
        appendProcTitle:false,
        beginFunc:      false,
        beginFuncOnFile:false,
        endFunc:        false,
        irohProxy:      false,
        selfSign:       false,
        defaultCharset: "utf-8",
        serverRoot:     wd
    }
}

var optlist = {
'--ipAddr':         'String. The ipv4 address to bind',
'--ipv6Addr':       'String. The ipv6 address to bind',
'--bindAll':        'Bool.   Set ipAddr and ipv6Addr to \'0.0.0.0\' and \'[::]\' respectively',
'--ipPort':         'Number. Set ipv4 port',
'--ipv6Port':       'Number. Set ipv6 port',
'--port':           'Number. Set both ipv4 and ipv6 port',
'--redirPort':      'Number. Listen on this port and 301-redirect to the https server (in-process)',
'--redir':          'Bool.   Equivalent to --redirPort 80',
'--htmlRoot':       'String. Root directory from which to serve files',
'--appsRoot':       'String. Root directory from which to serve apps',
'--wsappsRoot':     'String. Root directory from which to serve wsapps',
'--dataRoot':       'String. Setting for user scripts',
'--logRoot':        'String. Log directory',
'--accessLog':      'String. Log file name. "" for stdout',
'--errorLog':       'String. error log file name. "" for stderr',
'--irohProxy':      'Bool.   Start the iroh webproxy server for this server',
'--selfSign':       'Bool.   Whether to auto generate a self signed https cert',
'--log':            'Bool.   Whether to log requests and errors',
'--rotateLogs':     'Bool.   Whether to rotate the logs',
'--rotateInterval': 'Number. Interval between log rotations in seconds',
'--rotateStart':    'String. Time to start log rotations',
'--rotateCount':    'Number. Maximum number of old log files to keep (default 30)',
'--user':           'String. If started as root, switch to this user',
'--threads':        'Number. Limit the number of threads used by the server.\n                     Default (-1) is the number of cores on the system',
'--sslKeyFile':     'String. If https, the ssl/tls key file location',
'--sslCertFile':    'String. If https, the ssl/tls cert file location',
'--secure':         'Bool.   Whether to use https.  If true sslKeyFile and sslCertFile must be set',
'--developerMode':  'Bool.   Whether script errors result in 500 and return a stack trace.  Otherwise 404',
'--letsencrypt':    'String. If using letsencrypt, the \'domain.tld\' name for automatic setup of https\n'+
'                     (assumes --secure true and looks for \'/etc/letsencrypt/live/domain.tld/\' directory)\n' +
'                     (if redir is set, also map ./letsencrypt_wd/.well-known/ --> http://mydom.com/.well-known/)\n' +
'                     (if set to "setup", don\'t start https server, but do map ".well-known/" for http)\n' +
'                     (sets port:443 unless set otherwise)',
'--rootScripts':    'Bool.   Whether to treat *.js files in htmlRoot as apps (not secure)',
'--directoryFunc':  'Bool.   Whether to provide a directory listing if no index.html is found',
'--daemon':         'Bool.   whether to detach from terminal',
'--monitor':        'Bool.   whether to launch monitor process to auto restart server if killed or crashes',
'--scriptTimeout':  'Number  Max time to wait for a script module to return a reply in seconds (default 20)',
'--connectTimeout': 'Number  Max time to wait for client send request in seconds (default 20)',
'--defaultCharset': 'String. Charset appended to text/* Content-Type headers (default "utf-8").\n' +
'                     Use "false" to disable.  Applies to static files and dynamic responses.',
'-d':               'alias for \'--daemon true\'',
'--detach':         'alias for \'--daemon true\'',
'--monitor':        'fork and run a monitor as a daemon which restarts server w/in 10 seconds if it dies',
'--stop':           'stop the server.  Also stop the monitor and log rotation, if started'
}

// avoid rampart.globalize(rampart.utils) here
var exit=process.exit, utils=rampart.utils, fprintf=utils.fprintf,
    printf=utils.printf, sprintf=utils.sprintf, kill=utils.kill,
    stat=utils.stat, getType=utils.getType, trim=utils.trim, 
    exec=utils.exec, sleep=utils.sleep, stderr=utils.stderr, 
    dateFmt=utils.dateFmt, shell=utils.shell, realPath=utils.realPath,
    autoScanDate=utils.autoScanDate, mkdir=utils.mkdir, readFile=utils.readFile;

var wd;
var iam = trim(exec('whoami').stdout);
var unprivUser;

function writePid(name, pid) {
    var pidfile = wd + '/' + name + '.pid';
    try{
        fprintf(pidfile, '%s', pid);
    } catch(e){
        return {error:sprintf('Error: could not write pid to %s - %s\n', pidfile, e.message)};
    }
    if(iam == 'root') {
        try {
            utils.chown({user:unprivUser, path:pidfile});
        } catch(e) {
            fprintf(stderr,'warn: could chown pidfile %s to user "%s" - %s\n', pidfile, unprivUser, e.message);
        }
    }
    return {};
}

var validpids={}

function getPid(name,nolog) {
    var pidfile = wd + '/' + name + '.pid';
    var ret=false;
    try {
        ret = parseInt(readFile(pidfile,{returnString:true}));
    } catch(e) {}
    if(typeof ret != 'number' || Number.isNaN(ret) ) {
        if(validpids[pidfile] && !nolog)
            fprintf(stderr, '%s - Cannot get %s pid from \'%s\'.\n', dateFmt('%Y-%m-%d-%H-%M-%S'), name, pidfile);
        validpids[pidfile]=false;
        return false;
    }
    validpids[pidfile]=true;
    return ret;
}

function killPid(name, sig) {
    var ret={};
    ret.pidfile = wd + '/' + name + '.pid';
    ret.pid;
    if(!sig) sig='SIGTERM';
    try {
        ret.pid = parseInt(readFile(ret.pidfile,{returnString:true}));
    } catch(e) {
        ret.error='could not read from ' + ret.pidfile;
        ret.success=false;
        return ret;
    }
    if(typeof ret.pid != 'number' || Number.isNaN(ret.pid) ) {
        ret.success=false;
        ret.error='bad pid in ' + pidfile;
        return ret;
    }
    try {
        ret.success=kill(ret.pid, sig, true);
        utils.rmFile(ret.pidfile);
    } catch (e) {
        ret.success=false;
        ret.error=sprintf('%s',e.message);
    }
    
    return ret;
}

function serr(msg){
    return {error:msg};
}

function smsg(msg){
    return {message:msg};
}

function firstChecks(serverConf)
{
    if(serverConf.redirPort == -1 && serverConf.redir)
        serverConf.redirPort = 80;
    
    if (serverConf.port > 0 ){
        serverConf.ipPort=serverConf.ipv6Port=serverConf.port;
    }

    if(getType(serverConf.accessLog) == 'String'  && serverConf.accessLog.length==0)
        serverConf.accessLog=null;

    if(getType(serverConf.errorLog) == 'String'  && serverConf.errorLog.length==0)
        serverConf.errorLog=null;

         //__don't__ skip letsencrypt check if manually launching redir-server or monitor
         //we need the port set if it changes to 443
    if ( //!serverConf.launchRedir && !serverConf.launchMonitor
         //&&
        getType(serverConf.letsencrypt)=='String' && serverConf.letsencrypt.length)
    {
        if( serverConf.letsencrypt != "setup")
        {
            serverConf.sslKeyFile='/etc/letsencrypt/live/'+serverConf.letsencrypt+'/privkey.pem';
            serverConf.sslCertFile='/etc/letsencrypt/live/'+serverConf.letsencrypt+'/fullchain.pem';
            if(!stat(serverConf.sslKeyFile))
                return serr(sprintf("could not find file '%s'", serverConf.sslKeyFile)); 
            if(!stat(serverConf.sslCertFile))
                return serr(sprintf("could not find file '%s'", serverConf.sslCertFile)); 
            serverConf.secure=true;

            // if no explicit port set, and letsencrypt set, assume 443
            if(serverConf.port < 1 && serverConf.ipPort==8088 && serverConf.ipv6Port==8088)
            serverConf.port=serverConf.ipPort=serverConf.ipv6Port=443;
        }
        else if (serverConf.redirPort == -1)
        {
            return serr( "redir or redirPort must be set when letsencrypt==\"setup\"" );
        }
    }

    if(serverConf.selfSign) {
        if(!serverConf.secure)
            return serr( "selfSigned requires secure: true" );
        if(serverConf.letsencrypt)
            return serr( "selfSigned and letsencrypt cannot both be set true" );
        if( serverConf.sslKeyFile || serverConf.sslCertFile )
            return serr( "when selfSigned is true, sslKeyFile and sslCertFile must be unset" );

        var cert = process.scriptPath+'/selfSign-cert.pem';
        var key = process.scriptPath+'/selfSign-key.pem';

        if(
            !(stat(cert) &&
              stat(key) )
        )
        {
            var crypto = require("rampart-crypto");
            var r = crypto.gen_cert({
                country: "US",
                state: "Deleware",
                city: "Wilmington",
                organization: "Sample Co",
                organizationUnit: "Sample Department",
                email: "sample@sample.none",
                name: "sample.none",
                bits: 4096,
                days: 365,
                subjectAltName: ["localhost", "*.localhost"]
            });
            fprintf(key, '%s', r.key);
            fprintf(cert, '%s', r.cert);
        }

        serverConf.sslKeyFile=key;
        serverConf.sslCertFile=cert;
    }

    var bind = [];

    if(serverConf.bindAll) {
        if(!serverConf.ipPort || !serverConf.ipv6Port)
            return serr('no ip or ipv6 port specified');
        bind = ['0.0.0.0:'+serverConf.ipPort, '[::]:'+serverConf.ipv6Port];
    } else {
        if(serverConf.ipAddr && serverConf.ipPort)
            bind.push(serverConf.ipAddr + ':' + serverConf.ipPort);
        if(serverConf.ipv6Addr && serverConf.ipv6Port)
            bind.push(serverConf.ipv6Addr + ':' + serverConf.ipv6Port);
    }

    if(!bind.length)
        return serr('No ip addr/port specified');

    serverConf.bind=bind;

    unprivUser=serverConf.user;
    if(!serverConf.serverRoot)
        serverConf.serverRoot=wd;

    if(!serverConf.quickserver)
        serverConf.fullServer=1;
    else
        serverConf.fullServer=0;

    if(getType(serverConf.rotateInterval) == 'String')
    {
        switch(serverConf.rotateInterval.toLowerCase()) {
            case "hourly": serverConf.rotateInterval=3600;break;
            case "daily":  serverConf.rotateInterval=86400;break;
            case "weekly": serverConf.rotateInterval=604800;break;
            default:
                return serr('rotateInterval must be a Number (seconds) or ["weekly"|"daily"|"hourly"]');
        }
    }

    if(iam == 'root') {
        var st = stat(wd);
        if(!st)
            return serr(`could not stat server root "${wd}"`);
        if((st.mode & 7) != 7) { //if not world read/write/exec
            if(st.owner != unprivUser)
                return serr(`${wd} should be owned by '${unprivUser}' instead of '${st.owner}'`);
        } 
    }

    return serverConf;
}

/* 
   argv - array for command line opts
        - object for script invocation
*/
function parseOptions (argv){
    var arg, fullServer=1, printdefaults=0;

    wd = utils.getcwd();

    // if no argv - use process.argv for command line processing
    if(!argv)
        argv=process.argv;
    // if an object, argv should be settings to override the defaults
    else if (getType(argv)=='Object'){
        var def;
        // first set up accessLog and errorLog if logRoot is set.
        if( argv.logRoot ){
            if(!argv.accessLog) argv.accessLog = argv.logRoot + '/access.log';
            if(!argv.errorLog)  argv.errorLog  = argv.logRoot + '/error.log';
        }

        if(!argv.serverRoot)
            argv.serverRoot=realPath('.');
        else
            wd = _writableWd(argv.serverRoot);  /* writable runtime dir; differs from serverRoot when bundled */

        if(argv.quickserver)
            def=Object.assign({}, defaultQuickServerConf(argv.serverRoot));
        else
            def=Object.assign({}, defaultServerConf(argv.serverRoot));

        var ret=Object.assign(def, argv);
        return firstChecks(ret);
    }

    /* parsing command line options */
    for (var i=0; i<argv.length; i++)
    {
        arg =  argv[i]
        if(arg == '--server' || arg == '--quickserver') {
            if(arg == '--quickserver')
                fullServer=0;
            argv[i]='--skip';
        }
        else if(arg == "--")
            argv[i]='--skip';

        var st = (arg.charAt(0)=='-'?false:stat(arg));
        if(st){
            if(st.isDirectory) {
                wd=realPath(arg);
                argv[i]='--skip';
            } else if(arg==process.scriptName){
                argv[i]='--skip';
            }
        }
        if (arg == '--lsopts') {
            var optsmsg = '';
            for(var key in optlist) 
                optsmsg=sprintf('%s%-20s %s\n',optsmsg,key,optlist[key]);
            return smsg(optsmsg);
        }
        if (arg == '--help' || arg == '-h' || arg == '--?' || arg == '-?')
        {
            return smsg( 
                sprintf('rampart built-in server help:\n'+
    '\nUsage: rampart --[quick]server [options] [root_dir]\n' +
    '    --server              - run as a full server\n' +
    '    --quickserver         - run as a test server\n' +
    '    --help, -h, -?, --?   - this help message\n' +
    '    --lsopts              - print details on all options\n' +
    '    --showdefaults        - print the list of default settings for --server or --quickserver\n' +
    '    --OPTION [val]        - where OPTION is one of options listed from \'--lsopts\'\n' +
    /*'    OPTION=val      - alternative format for '--OPTION val'\n'  + */
    '\nIf root_dir is not specified, the current directory will be used\n'
                )
            );
        }
        if(arg == '--showdefaults')
            printdefaults=1;
    }

    //for when starting from `rampart --server` shortcut
    if(!process.scriptPath) process.scriptPath = wd;

    var serverConf;
    if(fullServer==1) {
        serverConf = defaultServerConf(wd);
        serverConf.fullServer=1;
    } else {
        serverConf = defaultQuickServerConf(wd);
        serverConf.fullServer=0;
    }

    if(printdefaults) {
        return smsg(sprintf('Defaults for %s:\n%3J\n', fullServer==1 ? '--server' : '--quickserver', serverConf));
    }

    var val;
    serverConf.shutdown=false;
    for (i=0; i<argv.length; i++) {
        var earg = arg = argv[i];

        if(arg=='-d' || arg=='--detach')
                arg='--daemon';
        if(arg=='--skip')
            continue;
        if (arg=='--stop')
        {
            serverConf.shutdown=true;
            continue;
        }
        if(arg.charAt(0)=='-' && arg.charAt(1)=='-') {
            arg = arg.substring(2);
            if(getType(serverConf[arg]) == 'Boolean') {
                if(argv[i+1] == 'true' || argv[i+1] == 'false')
                    val=argv[++i];
                else
                    val=true;
            } else if(i+1 >= argv.length) {
                return serr(sprintf('option \'--%s\' must be followed by a value', arg));
            } else {
                i++;
                val=argv[i];
            }
        } else {
            var argval = arg.split('=');
            if(argval.length>1)
            {
                arg=argval[0];
                if(argval.length>2) {
                    argval.shift();
                    val=argval.join('=');
                } else
                    val=argval[1];
            }
        }
        if(val=='true')
            val=true;
        else if (val=='false')
            val=false;
        else {
            var nval=parseInt(val);
            if(!isNaN(nval) && sprintf('%s',nval)==val)
                val=nval;
        }

        if(serverConf[arg]===undefined)
        {
            if(earg.charAt(0) == '-')
                return serr(sprintf('\'%s\' is an invalid option', earg));
            else
                return serr(sprintf('\'%s\' is an invalid option, a non-existant root directory or a duplicate root directory.', earg));
        }
        if((arg != "rotateInterval" || getType(val) != 'String') &&
           (arg != "defaultCharset" || getType(val) != 'Boolean'))
        {
            if(getType(serverConf[arg]) != getType(val))
                return serr(sprintf('Error: \'%s\' expects a %s but got \'%s\'', earg, getType(serverConf[arg]), val) );
        }
        serverConf[arg]=val;
    }

    serverConf=firstChecks(serverConf);
    return serverConf;
}

function status(serverConf){
    if(!serverConf)
        serverConf=defaultServerConf(utils.realPath('.'));

    if(!unprivUser)
        unprivUser=serverConf.user;

    wd = _writableWd(serverConf.serverRoot);
    if(!wd)
        serverConf.serverRoot=wd=utils.realPath('.');

    var ret={};
    var pret=getPid('server', true);
    ret.serverPid=pret;
    var pret=getPid('monitor', true);
    ret.monitorPid=pret;
    var iret=getPid('iroh-server', true);
    ret.irohPid=iret;
    return ret;    
}

function start(serverConf, dump) {
    var server=require('rampart-server');

    wd = _writableWd(serverConf.serverRoot);
    if(!wd)
        serverConf.serverRoot=wd=utils.realPath('.');

    if(!serverConf)
        serverConf=defaultServerConf(utils.realPath('.'));

    serverConf.launchServer = true;
    serverConf.launchMonitor = (serverConf.log && serverConf.rotateLogs) || serverConf.monitor;
    serverConf.launchRedir = serverConf.redirPort > 0 ;

    /* Setup mode: initial letsencrypt issuance, no https yet.
       Override serverConf so the regular start_server flow runs as
       a single minimal HTTP listener serving /.well-known/ only.
       No monitor, no iroh, no second daemon. Skip the rewrite when
       we're being called to stop/shutdown — those paths only need
       killPid('server') and shouldn't touch serverConf. */
    if (serverConf.letsencrypt == "setup"
        && !serverConf.stop && !serverConf.shutdown) {
        var le_wd = serverConf.serverRoot + '/letsencrypt_wd/.well-known';
        var le_st = stat(le_wd);
        if (!le_st) {
            try { mkdir(le_wd); } catch(e) {
                return serr('letsencrypt setup: could not create ' + le_wd + ': ' + e.message);
            }
            if (iam == 'root') {
                try {
                    utils.chown({user: serverConf.user, path: serverConf.serverRoot + '/letsencrypt_wd'});
                    utils.chown({user: serverConf.user, path: le_wd});
                } catch(e) {
                    fprintf(stderr, 'warn: could not chown %s to "%s" - %s\n', le_wd, serverConf.user, e.message);
                }
            }
        } else if (!le_st.isDirectory) {
            return serr('letsencrypt setup: ' + le_wd + ' exists but is not a directory');
        }
        serverConf.secure       = false;
        serverConf.sslKeyFile   = '';
        serverConf.sslCertFile  = '';
        serverConf.ipPort       = serverConf.redirPort;
        serverConf.ipv6Port     = serverConf.redirPort;
        /* parseOptions has already built serverConf.bind from the
           pre-override ipPort/ipv6Port — rebuild it on the redir port. */
        var setup_bind = [];
        if (serverConf.bindAll) {
            setup_bind = ['0.0.0.0:'+serverConf.redirPort, '[::]:'+serverConf.redirPort];
        } else {
            if (serverConf.ipAddr)   setup_bind.push(serverConf.ipAddr   + ':' + serverConf.redirPort);
            if (serverConf.ipv6Addr) setup_bind.push(serverConf.ipv6Addr + ':' + serverConf.redirPort);
        }
        serverConf.bind         = setup_bind;
        serverConf.map          = { '/.well-known/': le_wd + '/' };
        serverConf.launchServer = true;
        serverConf.launchRedir  = false;
        serverConf.launchMonitor = false;
        serverConf.redirPort    = -1;
        delete serverConf.httpRedirect;
    }

    /* When redirPort is set AND we're running https, use the in-process
       C-level httpRedirect option on the main server instead of forking
       a second process. The letsencrypt case adds a /.well-known/
       passthrough so ACME HTTP-01 challenges still serve over plain
       HTTP for renewals; everything else 301's to https.
       If the user has already set serverConf.httpRedirect (number,
       string, or object), respect it — they want full control. */
    if (serverConf.launchRedir
        && serverConf.secure
        && serverConf.letsencrypt != "setup")
    {
        if (!serverConf.httpRedirect) {
            var hr = { port: serverConf.redirPort };
            if (getType(serverConf.letsencrypt) == 'String' && serverConf.letsencrypt.length) {
                var le_wd = serverConf.serverRoot + '/letsencrypt_wd/.well-known';
                var le_st = stat(le_wd);
                if (!le_st) {
                    try { mkdir(le_wd); } catch(e) {
                        console.log("Error making directory for letsencrypt challenge updates:", e.message);
                    }
                    if (iam == 'root') {
                        try {
                            utils.chown({user:serverConf.user, path:serverConf.serverRoot + '/letsencrypt_wd'});
                            utils.chown({user:serverConf.user, path:le_wd});
                        } catch(e) {
                            fprintf(stderr, 'warn: could not chown dir %s to user "%s" - %s\n', le_wd, serverConf.user, e.message);
                        }
                    }
                } else if (!le_st.isDirectory) {
                    console.log("Error: " + le_wd + " is not a directory");
                }
                hr.passthrough = { "/.well-known/": le_wd + '/' };
            }
            serverConf.httpRedirect = hr;
        }
        serverConf.launchRedir = false;
    }

    if(!unprivUser)
        unprivUser=serverConf.user;

    if(serverConf.shutdown || serverConf.stop) {
        var res = killPid('server');
        var msg = 'Server has been stopped';
        if(!res.success)
            msg = 'Server is not running or pid file is invalid';

        res=killPid('monitor');
        if(res.success)
            msg += '\nMonitor process has been stopped';

        /* http->https redirect is now handled in-process by the
           server (httpRedirect), so there is no separate redirect
           server to track or kill. */

        res=killPid('iroh-server');
        if(res.success)
            msg += '\nIroh Server has been stopped';

        return {message:msg};
    }

    if(iam != 'root') {
        if(serverConf.ipPort < 1024)
            return serr('Error: script must be started as root to bind to IPv4 port ' + serverConf.ipPort);
        if(serverConf.ipv6Port < 1024)
            return serr('Error: script must be started as root to bind to IPv6 port ' + serverConf.ipv6Port);
        if(serverConf.redirPort < 1024 && serverConf.redirPort > 0)
            return serr('Error: script must be started as root to bind the redirect server to port ' + serverConf.redirPort);
    }

    var serverpid;

    if(!serverConf.notFoundFunc) {
        global._server_notFoundImg='';
        if(stat(serverConf.htmlRoot+'/images/not-found.jpg'))
            global._server_notFoundImg='<p><img style="width:65%" src="/images/not-found.jpg"></p>'
        if(stat(serverConf.htmlRoot+'/images/not-found.gif'))
            global._server_notFoundImg='<p><img style="width:65%" src="/images/not-found.gif"></p>'
        if(stat(serverConf.htmlRoot+'/images/not-found.png'))
            global._server_notFoundImg='<p><img style="width:65%" src="/images/not-found.png"></p>'

        serverConf.notFoundFunc = function (req){
            return {
                status:404,
                html: '<html><head><title>404 Not Found</title></head><body><center><h1>Not Found</h1>'+
                    '<p>The requested URL ' + rampart.utils.sprintf('%H',req.path.path) + 
                    ' was not found on this server.</p>' +
                    global._server_notFoundImg +
                    '</center></body></html>'
            }
        }
    }

    if(!serverConf.scriptTimeout)
        serverConf.scriptTimeout=20;
    if(!serverConf.connectTimeout)
        serverConf.connectTimeout=20;
    if(!serverConf.mimeMap)
        serverConf.mimeMap={ 'mp3': 'audio/mp3' };

    var map;
    if(serverConf.map && getType(serverConf.map)=='Object')
        map=serverConf.map;
    else {
        map={};

        if(serverConf.htmlRoot && serverConf.htmlRoot.length)
            map['/']=serverConf.htmlRoot;

        if(serverConf.appsRoot && serverConf.appsRoot.length)
            map['/apps/'] = {modulePath: serverConf.appsRoot};

        if(serverConf.wsappsRoot && serverConf.appsRoot.length)
            map['ws://wsapps/'] = {modulePath: serverConf.wsappsRoot};
    }

    if(serverConf.appendMap && getType(serverConf.appendMap)=='Object')
        Object.assign(map, serverConf.appendMap);

    if(serverConf.rootScripts && serverConf.htmlRoot && serverConf.htmlRoot.length) {
        var scripts = utils.readDir(serverConf.htmlRoot).filter(function(f){return /\.js$/.test(f);});
        scripts.forEach (function(sn) {
            var p = '/' + sn.replace(/\.js$/,'') + '/';
            map[p]={module: serverConf.htmlRoot+'/'+sn};
        });
    }

    serverConf.map=map;

    /************ START THE SERVER ***************/
    function start_server(restart){

        if(!serverConf.launchServer)
            return {};

        //set global serverConf for app/*.js and wsapp/*.js scripts
        global.serverConf=serverConf;
        serverpid=server.start(serverConf);

        if(serverConf.daemon) { 
            sleep(0.5); //give time for server to exit if error
            if(!kill(serverpid, 0)) {
                return serr(sprintf('Failed to start webserver'));
            }
        }

        var wpres = writePid('server', serverpid);

        if( serverConf.daemon && wpres.error) {
            kill(serverpid);
            var p = getPid('monitor');
            if(p) kill(p);
            return wpres;
        }

        /* if authMod is enabled and we started as root, chown/chmod the
           auth data directory so the unprivileged user can read/write it */
        if (serverConf.authMod && serverConf.authModConf && iam == 'root' && unprivUser) {
            try {
                var authConf = require(serverConf.authModConf);
                var authDbPath = authConf.dbPath || 'data/auth';
                if (authDbPath.charAt(0) !== '/')
                    authDbPath = wd + '/' + authDbPath;
                if (stat(authDbPath)) {
                    utils.chown({user: unprivUser, path: authDbPath});
                    var authFiles = utils.readDir(authDbPath);
                    for (var afi = 0; afi < authFiles.length; afi++) {
                        var afp = authDbPath + '/' + authFiles[afi];
                        utils.chmod(afp, '0600');
                        utils.chown({user: unprivUser, path: afp});
                    }
                }
            } catch(e) {
                fprintf(stderr, 'warn: could not fix auth data permissions: %s\n', e.message);
            }
        }

        var ret = smsg(sprintf('Server has been started.'));
        ret.pid=serverpid;
        return ret;
    }

    /* http->https redirect is handled in-process via the C-level
       httpRedirect option (server.start()). No second daemon or JS
       redirect callback is needed. */
    if (serverConf.httpRedirect && !serverConf.secure) {
        return serr('options --redir[Port] requires --secure');
    }

    /* start_redir() and the doredir/redircode/redirHtmlFmt JS handlers
       used to live here. They spawned a second daemon to do http->https
       redirects (and serve /.well-known/ for letsencrypt). Both jobs
       are now done in-process via server.start({httpRedirect: ...}) —
       see the promotion block above. Setup-mode (letsencrypt=="setup")
       was overridden at the top of start() to become a plain HTTP
       server with /.well-known/ as its only map. */

    /* ****************** START THE IROH SERVER ************************ */

    function start_iroh() {
        var addr = '127.0.0.1', tls='', irohbin;

        if(!serverConf.irohProxy)
            return {};

        if(serverConf.secure)
            tls = '--tls --insecure';

        if(serverConf.ipAddr != "0.0.0.0" && ! serverConf.bindAll)
            addr = serverConf.ipAddr;

        irohbin = rampart.utils.shell('which iroh-webproxy');

        if(irohbin.exitStatus == 0)
            irohbin=irohbin.stdout.trim();
        else
            return serr('Server is configured with irohProxy:true but iroh-webproxy executable not found');

        var cmd = sprintf(
            '%s server %s --key-file "%s/iroh-webserver-secret.txt" --target %s:%s --daemon --pidfile %s/iroh-server.pid',
            irohbin, tls, serverConf.serverRoot, addr, serverConf.ipPort, serverConf.serverRoot
        );

        var reti = rampart.utils.shell(cmd);
        if(reti.exitStatus != 0)
            return serr(`iroh start failed: ${reti.stderr}`);

        rampart.utils.fprintf(`${serverConf.serverRoot}/iroh-nodeId.txt`,'%s', reti.stdout);

        var ret = smsg(sprintf('Iroh Server has been started.'));
        ret.pid = rampart.utils.readFile(`${serverConf.serverRoot}/iroh-server.pid`);

        return ret;
    }

    /************ START THE MONITOR PROCESS ***************/
    function checkMonitor()
    {
        if(!serverConf.launchMonitor)
            return true; // no monitor requested, continue and run server

        if(serverConf.fullServer!=1)
            return serr('options --rotateLogs or --monitor not available with --quickserver');

        if(!serverConf.daemon)
            return serr('options --rotateLogs and --monitor require --daemon');

        var gzip = trim ( exec('which','gzip').stdout );

        var pid = utils.daemon(); //fork as daemon

        if(pid==-1) //fork failed
            return serr('could not fork a new monitor process');

        if(pid) //parent
            return true;//run server in parent

        //child daemon below

        var wpres = writePid('monitor', process.getpid());
        if(wpres.error) {
            fprintf(stderr, "%s Monitor exiting.\n", wpres.error);
            exit(1);
        }

        process.setProcTitle('rampart serverMonitor ' + wd);
        sleep(1);

        /**************** LOG ROTATION **********************/
        if(serverConf.log && serverConf.rotateLogs) {
            var tdelay, mdelay, startTime, now;
            if (typeof serverConf.rotateInterval != 'number')
            {
                fprintf(stderr, 'serverConf.rotateInterval == %J is invalid\n', serverConf.rotateInterval);
                exit(1);
            }
            if( serverConf.rotateInterval < 300 ) {
                fprintf(stderr, 'serverConf.rotateInterval is set to less than 5 minutes, is that what your really want?\n');
            }

            if(typeof serverConf.rotateCount != 'number') {
                serverConf.rotateCount = -1;
                fprintf(serverConf.logRoot + '/rotation-error.log', true,
                    '%s - rotateCount is not a number, skipping deletion of old logs\n', dateFmt('%Y-%m-%d %H:%M:%S'));
            }

            serverConf.isLocal=false;
            function getStartTime() {
                var mdelay = serverConf.rotateInterval * 1000;
                var now = new Date();
                var StartTime;
                try {
                    if(serverConf.rotateStart=='now')
                    {
                        startTime=now;
                        serverConf.isLocal=true;
                    }
                    else
                    {
                        var dres = autoScanDate(serverConf.rotateStart);
                        if(!dres)
                        {
                            fprintf(stderr, 'Error parsing log rotation start time "%s": Monitor Exiting\n', serverConf.rotateStart);
                            exit(1);
                        }
                        if(dres.offset==0){
                            //assume localtime if no timezone provided
                            dres = autoScanDate(serverConf.rotateStart + " " + dateFmt('%z'));
                            serverConf.isLocal=true;
                        }
                        startTime = dres.date;
                    }
                    var origStart = startTime;
                    while( startTime < now )
                        startTime = new Date(startTime.getTime() + serverConf.rotateInterval*1000);

                } catch(e) {
                    fprintf(stderr, 'Error parsing log rotation start time (%s): %s - Monitor Exiting\n', serverConf.rotateStart,e.message);
                    exit(1);
                }
                if(serverConf.isLocal && ! (serverConf.rotateInterval % 86400) )
                    serverConf.rotateStart = dateFmt("%Y-%m-%d %z", (startTime.getTime() + mdelay)/1000).substring(0,11) + dateFmt("%H:%M:%S %z", origStart);
                else
                    serverConf.rotateStart = dateFmt("%Y-%m-%d %H:%M:%S", (startTime.getTime() + mdelay)/1000);

                return {now:now, mdelay:mdelay, startTime: startTime}
            }

            var dres = getStartTime();
            now = dres.now;
            startTime=dres.startTime;
            mdelay=dres.mdelay;

            tdelay = startTime.getTime() - now.getTime();

            var prevAbackup=false, prevEbackup=false;

            var rotErrLog = serverConf.logRoot + '/rotation-error.log';

            function rotErr(msg) {
                fprintf(rotErrLog, true, '%s - %s\n', dateFmt('%Y-%m-%d %H:%M:%S'), msg);
            }

            function rotateLogs() {
                serverpid=getPid('server');
                if(!serverpid || !kill(serverpid,0)) {
                    if(serverConf.monitor)
                        return;//monitor might restart?
                    else
                        exit(0);
                }
                var doArotate=false, doErotate=false;
                var ds = dateFmt('%Y-%m-%d-%H-%M-%S');
                var abackup = sprintf('%s-%s', serverConf.accessLog, ds);
                var ebackup = sprintf('%s-%s', serverConf.errorLog,  ds);

                var logstat = stat(serverConf.accessLog);
                if( logstat && logstat.size ){
                    doArotate=true;
                    try {
                        utils.rename( serverConf.accessLog, abackup);
                    } catch(e) {
                        rotErr('Cannot rename accessLog: ' + e.message);
                        doArotate=false;
                    }
                }

                logstat = stat(serverConf.errorLog);
                if( logstat && logstat.size ){
                    doErotate=true;
                    try {
                        utils.rename( serverConf.errorLog, ebackup);
                    } catch(e) {
                        rotErr('Cannot rename errorLog: ' + e.message);
                        doErotate=false;
                    }
                }

                if(doArotate||doErotate)
                    kill(serverpid, 'SIGUSR1');// close and reopen logs
                else
                    return;

                if(gzip && (prevAbackup || prevEbackup) )
                    shell(gzip + ' -q ' + (prevAbackup ? prevAbackup:'')+ ' ' + (prevEbackup?prevEbackup:''), {background:true} );

                if(doArotate) prevAbackup=abackup;

                if(doErotate) prevEbackup=ebackup;

                // delete old rotated logs beyond rotateCount
                if(serverConf.rotateCount > 0) {
                    try {
                        var logdir = serverConf.logRoot;
                        var files = utils.readDir(logdir);
                        function cleanOld(baselog) {
                            var base = baselog.substring(baselog.lastIndexOf('/') + 1);
                            var old = files.filter(function(f) {
                                return f.indexOf(base + '-') === 0;
                            }).sort();
                            while(old.length > serverConf.rotateCount) {
                                try {
                                    utils.rmFile(logdir + '/' + old.shift());
                                } catch(e) {
                                    rotErr('Cannot delete old log: ' + e.message);
                                }
                            }
                        }
                        if(doArotate) cleanOld(serverConf.accessLog);
                        if(doErotate) cleanOld(serverConf.errorLog);
                    } catch(e) {
                        rotErr('Error cleaning old logs: ' + e.message);
                    }
                }
            }

            if(!serverConf.isLocal) {
                setTimeout( function(){
                    rotateLogs();
                    var iv=setMetronome(rotateLogs, mdelay);
                }, tdelay);
            } else {
                // for a local time, need to re-evaluate hour in case of daylight savings change.
                function dorotate(){
                    rotateLogs();
                    var dres = getStartTime();
                    tdelay = dres.startTime.getTime() - dres.now.getTime();
                    setTimeout(dorotate, tdelay);
                }

                setTimeout(dorotate,tdelay);
            }
        }

        /**************** PROCESS MONITOR **********************/
        if(serverConf.monitor)
        {
            var iv2 = setMetronome(function(){
                serverpid=getPid('server');
                if(!serverpid)
                    return;
                if(!kill(serverpid,0))
                {
                    fprintf(serverConf.errorLog, true, '%s - monitor: restarting server\n', dateFmt('%Y-%m-%d-%H-%M-%S'));
                    var res=start_server(true);
                    if(res.error) {
                        fprintf(serverConf.errorLog, true, '%s - monitor: restarting server failed -%s. Monitor exiting\n', dateFmt('%Y-%m-%d-%H-%M-%S'), res.error);
                        process.exit(1);
                    }
                }
                /* redirect server is no longer tracked separately —
                   http->https redirect lives in the main server via
                   httpRedirect, so it dies/restarts with the main pid. */
            }, 10000);
            // check that servers return something via http(s).
            var curl = require("rampart-curl");
            var thisurl = serverConf.secure ? "https://" : "http://";

            if(serverConf.bindAll)
            {
                thisurl += "127.0.0.1:" + serverConf.ipPort + '/';
            } else {
                thisurl += serverConf.bind[0] + '/';
            }
            var iv3 = setMetronome(function(){
                var res = curl.fetch({insecure:true, "max-time": 10}, thisurl);
                if(res.status==0) {
                    fprintf(serverConf.errorLog, true, '%s - monitor: failed to fetch %s - %s\n',
                        dateFmt('%Y-%m-%d %H:%M:%S %z'), thisurl, res.errMsg);
                    serverpid=getPid('server');
                    if(serverpid)
                        kill(serverpid,9);  //don't be nice
                    //let the function above restart
                }
            },60000);

        }
        return false; //In monitor fork, do not run server
    }

    if(dump)
        return serverConf;

    // start iroh (order doesn't matter)
    var reti = start_iroh();
    if(reti.error)
        return reti;

    // start the main server
    var ret=start_server();

    if(ret.error) {
        if(reti.pid)
            try { kill(reti.pid); } catch(e){}
        return ret;
    }

    if(serverConf.letsencrypt=="setup") {
        printf(
`Server started in letsencrypt setup mode.
Now run letsencrypt to generate the certificate.
Example:
    certbot certonly --webroot \\
    --webroot-path %s/letsencrypt_wd \\
    -d %s\n`, serverConf.serverRoot, serverConf.letsencryptHost);
    }

    var status = checkMonitor();

    // status is true, we are the parent, or we never forked
    if(status)
        return ret;

    // status is false, we are the forked monitor
    return {isMonitor:true};

}

function dumpConfig(serverConf) {
    var conf = start(serverConf, true);
    var ret={};
    var props = ["bind","scriptTimeout","connectTimeout","log",'logRoot', 'dataRoot',"accessLog","errorLog","daemon","useThreads","threads","maxRead","maxWrite","secure","sslKeyFile","sslCertFile","sslMinVersion","notFoundFunc","developerMode","directoryFunc","user","cacheControl","defaultCharset","compressFiles","compressScripts","compressLevel","compressMinSize","mimeMap","map","appendProcTitle"];
    for (var i=0; i<props.length; i++)
        ret[props[i]]=conf[props[i]];
    return ret;
}

function stop(serverConf){
    if(getType(serverConf)=='Object')
        serverConf=Object.assign({},serverConf,{stop:true});
    else
        serverConf={stop:true};
    return start(serverConf);    
}

function cmdLine(nslice) {
    var args = process.argv.slice(nslice);

    function printmsg(o, exitOnMsg) {
        if(o.error)
        {
            fprintf(stderr,"Error: %s\n", o.error);
            exit(1);
        } else if (o.message) {
            printf("%s\n", o.message);
            if(exitOnMsg)
                exit(0);
        }
    }

    var conf=parseOptions(args);
    printmsg(conf,true);
    var ret=start(conf);
    printmsg(ret);
}

function web_server_conf(conf) {
    var res, printf=rampart.utils.printf, argv=process.argv, kill=rampart.utils.kill;


    if (argv[2] == '--letssetup' || argv[2]=='letssetup') {
        conf.letsencryptHost = conf.letsencrypt;
        conf.letsencrypt="setup"; //flag we are doing letsencrypt, but don't start https
        argv[2]="start";
    }

    /* Preserve the user's original conf (just serverRoot is needed)
       so stop/restart can find the pid files even if parseOptions
       errored — e.g., letsencrypt cert files unreadable as a non-root
       user trying to stop a server running as root. */
    var originalServerRoot = conf && conf.serverRoot;

    // fill in the missing pieces and do some checks
    conf = parseOptions(conf);
    if (conf && !conf.serverRoot && originalServerRoot)
        conf.serverRoot = originalServerRoot;

    if (conf.dumpObj)
        return start(conf, /* dump = */ true);

    function check_conf_err() {
        if(conf.error)
        {
            printf("%s\n", conf.error);
            process.exit(1);
        }
    }

    //try to stop even if conf errors returned from parseOptions
    if(argv[2] == '--stop' || argv[2]=='stop') {

        /* STOP */
        res=stop(conf);
        if(res.error)
            printf("Server is not running or pid file is invalid\n");
        else if (res.message)
            printf("%s\n", res.message);
        process.exit(0);

    } else if(argv[2] == '--restart' || argv[2]=='restart') {

        /* RESTART */
        check_conf_err();
        res=stop(conf);
        if(res.error)
            printf("Server is not running or pid file is invalid\n");
        else if (res.message)
            printf("%s\n", res.message);

        res=start(conf);

        if(res.message)
            console.log(res.message);

        if(res.error) {
            console.log(res.error);
            process.exit(1);
        }

    } else if(argv[2] == '--status' || argv[2]=='status') {

        /* STATUS */
        res=status(conf);

        if( res.serverPid && kill(res.serverPid,0) )
            printf("server is running. pid: %s\n", res.serverPid);
        else
            printf("server is not running\n");

        if( res.monitorPid && kill(res.monitorPid,0) )
            printf("monitor process is running. pid: %s\n", res.monitorPid);
        else
            printf("monitor process is not running\n");

        if( res.irohPid && kill(res.irohPid,0) )
            printf("iroh-webproxy process is running. pid: %s\n", res.irohPid);

    } else if (argv[2] == '--dump' || argv[2]=='dump') {

        /* DUMP */
        check_conf_err();
        res=dumpConfig(conf);
        printf("%3J\n", res);
        process.exit(0);

    } else if (argv[2] == '--start' || argv[2]=='start' || !argv[2]) { //if no arg, run start

        /* START */
        conf.start=true;
        check_conf_err();

        res=start(conf);

        if(res.message)
            console.log(res.message);

        if(res.error) {
            console.log(res.error);
            process.exit(1);
        }
        // if (res.isMonitor) -- we are the monitor and should do nothing else but finish the script
        //                       so event loop can start and monitor can run its setTimeouts
        // else               -- we just exit.

    } else { 

        /* HELP */
        if (argv[2] != '-h' && argv[2] != '--help' && argv[2] != 'help')
        printf("unknown command '%s'\n\n", argv[2]);
        printf("usage:\n  %s %s [start|stop|restart|letssetup|status|dump|help]\n",argv[0], argv[1]);
        printf("      start     -- start the http(s) server\n");
        printf("      stop      -- stop the http(s) server\n");
        printf("      restart   -- stop and restart the http(s) server\n");
        printf("      letssetup -- start http only to allow letsencrypt verification\n");
        printf("      status    -- show status of server processes\n");
        printf("      dump      -- dump the config object used for server.start()\n");
        printf("      help      -- show this message\n");

    }
}

if(module && module.exports){

    module.exports= {
        parseOptions: parseOptions,
        start: start,
        stop:  stop,
        status:status,
        dumpConfig: dumpConfig,
        cmdLine: cmdLine,
        web_server_conf: web_server_conf
    }

} else {
    //skip first two (["rampart", "scriptname"])
    cmdLine(2);
}
