var defaultServerConf = function(wd){
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
        dataRoot:       wd + '/data',
        logRoot:        wd + '/logs',
        accessLog:      wd + '/logs/access.log',
        errorLog:       wd + '/logs/error.log',
        log:            true,
        rotateLogs:     false,
        rotateInterval: 86400,
        rotateStart:    '00:00',
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
'--redirPort':      'Number. Launch http->https redirect server and set port',
'--redir':          'Bool.   Launch http->https redirect server and set to port 80',
'--htmlRoot':       'String. Root directory from which to serve files',
'--appsRoot':       'String. Root directory from which to serve apps',
'--wsappsRoot':     'String. Root directory from which to serve wsapps',
'--dataRoot':       'String. Setting for user scripts',
'--logRoot':        'String. Log directory',
'--accessLog':      'String. Log file name. "" for stdout',
'--errorLog':       'String. error log file name. "" for stderr',
'--log':            'Bool.   Whether to log requests and errors',
'--rotateLogs':     'Bool.   Whether to rotate the logs',
'--rotateInterval': 'Number. Interval between log rotations in seconds',
'--rotateStart':    'String. Time to start log rotations',
'--user':           'String. If started as root, switch to this user',
'--threads':        'Number. Limit the number of threads used by the server.\n                     Default (-1) is the number of cores on the system',
'--sslKeyFile':     'String. If https, the ssl/tls key file location',
'--sslCertFile':    'String. If https, the ssl/tls cert file location',
'--secure':         'Bool.   Whether to use https.  If true sslKeyFile and sslCertFile must be set',
'--developerMode':  'Bool.   Whether script errors result in 500 and return a stack trace.  Otherwise 404',
'--letsencrypt':    'String. If using letsencrypt, the \'domain.tld\' name for automatic setup of https\n                     (assumes --secure true and looks for \'/etc/letsencrypt/live/domain.tld/\' directory)',
'--rootScripts':    'Bool.   Whether to treat *.js files in htmlRoot as apps (not secure)',
'--directoryFunc':  'Bool.   Whether to provide a directory listing if no index.html is found',
'--daemon':         'Bool.   whether to detach from terminal',
'--monitor':        'Bool.   whether to launch monitor process to auto restart server if killed or crashes',
'--scriptTimeout':  'Number  Max time to wait for a script module to return a reply in seconds (default 20)',
'--connectTimeout': 'Number  Max time to wait for client send request in seconds (default 20)',
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
    autoScanDate=utils.autoScanDate;

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
        ret = parseInt(utils.readFile(pidfile,{returnString:true}));
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
        ret.pid = parseInt(utils.readFile(ret.pidfile,{returnString:true}));
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

    if (getType(serverConf.letsencrypt)=='String' && serverConf.letsencrypt.length)
    {
        serverConf.sslKeyFile='/etc/letsencrypt/live/'+serverConf.letsencrypt+'/privkey.pem';
        serverConf.sslCertFile='/etc/letsencrypt/live/'+serverConf.letsencrypt+'/fullchain.pem';
        if(!stat(serverConf.sslKeyFile))
            return serr(sprintf("could not find file '%s'", serverConf.sslKeyFile)); 
        if(!stat(serverConf.sslCertFile))
            return serr(sprintf("could not find file '%s'", serverConf.sslCertFile)); 
        serverConf.secure=true;
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
        if(!argv.serverRoot)
            argv.serverRoot=realPath('.');

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
        if (arg == '--help' || arg == '-h')
        {
            return smsg( 
                sprintf('rampart built-in server help:\n'+
    '\nUsage: rampart --[quick]server [options] [root_dir]\n' +
    '    --server        - run as a full server\n' +
    '    --quickserver   - run as a test server\n' +
    '    --help, -h      - this help message\n' +
    '    --lsopts        - print details on all options\n' +
    '    --showdefaults  - print the list of default settings for --server or --quickserver\n' +
    '    --OPTION [val]  - where OPTION is one of options listed from \'--lsopts\'\n' +
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
        if(arg != "rotateInterval" || getType(val) != 'String')
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

    wd = serverConf.serverRoot;
    if(!wd)
        serverConf.serverRoot=wd=utils.realPath('.');

    var ret={};
    var pret=getPid('server', true);
    ret.serverPid=pret;
    var pret=getPid('monitor', true);
    ret.monitorPid=pret;
    var pret=getPid('redir-server', true);
    ret.redirPid=pret;
    return ret;    
}

function start(serverConf, dump) {
    var server=require('rampart-server'); 

    if(!serverConf)
        serverConf=defaultServerConf(utils.realPath('.'));

    if(!unprivUser)
        unprivUser=serverConf.user;

    wd = serverConf.serverRoot;
    if(!wd)
        serverConf.serverRoot=wd=utils.realPath('.');

    if(serverConf.shutdown || serverConf.stop) {
        var res = killPid('server');
        if(!res.success)
            return {error:'Server is not running or pid file is invalid'}
        res=killPid('monitor');
        res=killPid('redir-server');
        return {message:'server stopped'};
    }


    if(iam != 'root') {
        if(serverConf.ipPort < 1024)
            return serr('Error: script must be started as root to bind to IPv4 port ' + serverConf.ipPort);
        if(serverConf.ipv6Port < 1024)
            return serr('Error: script must be started as root to bind to IPv6 port ' + serverConf.ipv6Port);
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
    if(serverConf.mapOverride && getType(serverConf.mapOverride)=='Object')
        map=serverConf.mapOverride;
    else {
        map={};

        if(serverConf.map && getType(serverConf.map)=='Object')
            Object.assign(map, serverConf.map);

        map['/']=serverConf.htmlRoot;

        if(serverConf.appsRoot && serverConf.appsRoot.length)
            map['/apps/'] = {modulePath: serverConf.appsRoot};

        if(serverConf.wsappsRoot && serverConf.appsRoot.length)
            map['ws://wsapps/'] = {modulePath: serverConf.wsappsRoot};
    }

    if(serverConf.rootScripts) {
        var scripts = utils.readDir(serverConf.htmlRoot).filter(function(f){return /\.js$/.test(f);});
        scripts.forEach (function(sn) {
            var p = '/' + sn.replace(/\.js$/,'') + '/';
            map[p]={module: serverConf.htmlRoot+'/'+sn};
        });
    }

    serverConf.map=map;

    function start_server(restart){

        //set global serverConf for app/*.js and wsapp/*.js scripts
        global.serverConf=serverConf;
        serverpid=server.start(serverConf);

        if(serverConf.daemon) { 
            sleep(0.5); //give time for server to exit if error
            if(!kill(serverpid, 0)) {
                return serr(sprintf('Failed to start webserver'));
            }
            var wpres = writePid('server', serverpid);
            if(wpres.error) {
                kill(serverpid);
                var p = getPid('monitor');
                if(p) kill(p);
                p = getPid('redir-server');
                if(p) kill(p);
                return wpres;
            }
        }
        var ret = smsg(sprintf('Server has been started.'));
        ret.pid=serverpid;
        return ret;
    }

    function doredir(req)
    {
        var url = 'https://' + req.path.host.replace(/:\d+/,'') + req.path.path;
        return {
            html:rampart.utils.sprintf(
                '<html><body><h1>302 Moved Permanently</h1>'+
                '<p>Document moved <a href="\%s\">here</a></p></body></html>',
                url
            ),
            status:301,
            headers: { 'location': url}
        }
    }

    if( (!serverConf.daemon || !serverConf.secure) && serverConf.fullServer==1 && serverConf.redirPort!=-1){
        return serr('options --redir[Port] requires --daemon and --secure');
    }

    function start_redir(restart) {
        var redirbind=[];

        if(serverConf.redirPort==-1 || serverConf.redirPort===undefined)
            return{};

        if(serverConf.bindAll) {
            redirbind = ['0.0.0.0:'+serverConf.redirPort, '[::]:'+serverConf.redirPort];
        } else {
            if(serverConf.ipAddr)
                redirbind.push(serverConf.ipAddr + ':' + serverConf.redirPort);
            if(serverConf.ipv6Addr)
                redirbind.push(serverConf.ipv6Addr + ':' + serverConf.redirPort);
        }

        var rpid=server.start(
        {
            bind: redirbind,
            user: serverConf.user,
            scriptTimeout: 20.0,
            connectTimeout:20.0,
            developerMode: true,
            log: true,
            accessLog: "/dev/null",
            errorLog: "/dev/null",
            daemon: true,
            threads: 2,
            directoryFunc: false,
            map: { '/':  doredir },
            appendProcTitle: serverConf.appendProcTitle
        });

        sleep(0.5); //give proc time to exit if error

        if(!kill(rpid, 0)) {
            return serr('Failed to start redirect webserver');
        }

        var wpres = writePid('redir-server', rpid);
        if(wpres.error) {
            kill(rpid);
            var p = getPid('monitor');
            if(p) kill(p);
            p = getPid('server');
            if(p) kill(p);
            return wpres;
        }

        var ret = smsg('Redirect Server has been started');
        ret.pid=rpid;
        return ret;
    }

    function checkMonitor()
    {
        if( !(serverConf.log && serverConf.rotateLogs) && !serverConf.monitor )
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
                        fpreintf(serverConf.errorLog, true, 'Cannot rename accessLog: %J\n', e.message);
                        doArotate=false;
                    }
                }

                logstat = stat(serverConf.errorLog);
                if( logstat && logstat.size ){
                    doErotate=true;
                    try {
                        utils.rename( serverConf.errorLog, ebackup);
                    } catch(e) {
                        fprintf(serverConf.errorLog, true, 'Cannot rename errorLog: %J\n', e.message);
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
                if(serverConf.redirPort!=-1)
                {
                    var redirpid=getPid('redir-server');
                    if(!redirpid)
                        return;
                    if(!kill(redirpid,0))
                    {
                        fprintf(serverConf.errorLog, true, '%s - monitor: restarting redirect server\n', dateFmt('%Y-%m-%d-%H-%M-%S'));
                        var res=start_redir(true);
                        if(res.error) {
                            fprintf(serverConf.errorLog, true, '%s - monitor: restarting redirect server failed -%s. Monitor exiting\n', dateFmt('%Y-%m-%d-%H-%M-%S'), res.error);
                            process.exit(1);
                        }
                    }
                }
            }, 10000);
            // check that servers return something via http(s).
            var curl = require("rampart-curl");
            var thisurl = serverConf.secure ? "https://" : "http://";
            var thisredirurl = serverConf.redirPort>0 ? "http://" : false;

            if(serverConf.bindAll)
            {
                thisurl += "127.0.0.1:" + serverConf.ipPort + '/';
                if(thisredirurl)
                    thisredirurl +=  "127.0.0.1:" + serverConf.redirPort + '/';
            } else {
                thisurl += serverConf.bind[0] + '/';
                if(thisredirurl)
                {
                    var u=serverConf.bind[0];
                    thisredirurl += u.substring(0, u.lastIndexOf(':'))  + serverConf.redirPort + '/';
                }
            }
            var iv3 = setMetronome(function(){
                var res = curl.fetch({insecure:true, "max-time": 10}, thisurl);
                if(res.status==0) {
                    fprintf(serverConf.errorLog, true, '%s - monitor: failed to fetch %s - %s\n', 
                        dateFmt('%Y-%m-%d-%H-%M-%S'), thisurl, res.errMsg);
                    serverpid=getPid('server');
                    if(serverpid)
                        kill(serverpid,9);  //don't be nice
                    //let the function above restart
                }
                if(thisredirurl)
                {
                    res = curl.fetch({"max-time": 10}, thisredirurl);
                    if(res.status==0) {
                        fprintf(serverConf.errorLog, true, '%s - monitor: failed to fetch %s - %s\n', 
                            dateFmt('%Y-%m-%d-%H-%M-%S'), thisredirurl, res.errMsg);
                        var rserverpid=getPid('redir-server');
                        if(rserverpid)
                            kill(rserverpid,9);  //don't be nice
                        //let the function above restart
                    }
                }
            },60000);

        }
        return false; //In monitor fork, do not run server
    }

    if(dump)
        return serverConf;

    // start redir server if so configured
    var retr=start_redir();

    if(retr.error)
        return retr;

    // start the main server
    var ret=start_server();

    if(ret.error) {
        if(retr.pid)
            kill(retr.pid);
        return ret;
    }

    // add redir pid if redir server launched
    if(retr.pid)
        ret.redirPid=retr.pid;

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
    var props = ["bind","scriptTimeout","connectTimeout","log","accessLog","errorLog","daemon","useThreads","threads","maxRead","maxWrite","secure","sslKeyFile","sslCertFile","sslMinVersion","notFoundFunc","developerMode","directoryFunc","user","cacheControl","compressFiles","compressScripts","compressLevel","compressMinSize","mimeMap","map","appendProcTitle"];
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

if(module && module.exports){

    module.exports= {
        parseOptions: parseOptions,
        start: start,
        stop:  stop,
        status:status,
        dumpConfig: dumpConfig,
        cmdLine: cmdLine
    }

} else {
    //skip first two (["rampart", "scriptname"])
    cmdLine(2);
}