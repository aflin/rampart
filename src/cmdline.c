/* Copyright (C) 2022 Aaron Flin - All Rights Reserved
   Copyright (C) 2022 Benjamin Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include "rampart.h"
#include "duktape/register.h"
#include "include/version.h"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <errno.h>

#include "event.h"
#include "event2/thread.h"
#include "event2/dns.h"
#include "linenoise.h"
#include "sys/queue.h"
#include "whereami.h"

//clock_gettime for macos < sierra
#ifdef NEEDS_CLOCK_GETTIME
int clock_gettime(clockid_t type, struct timespec *rettime)
{
    mach_timespec_t clk_ts;
    clock_serv_t clksrv;
    int ret=0;

    host_get_clock_service(mach_host_self(), type, &clksrv);
    ret = (int)clock_get_time(clksrv, &clk_ts);
    mach_port_deallocate(mach_task_self(), clksrv);
    rettime->tv_sec = clk_ts.tv_sec;
    rettime->tv_nsec = clk_ts.tv_nsec;

    return ret;
}
#endif

int RP_TX_isforked=0;  //set to one in fork so we know not to lock sql db;
int totnthreads=0;
char *RP_script_path=NULL;
char *RP_script=NULL;
//duk_context **thread_ctx = NULL;
//__thread int local_thread_number=0;
duk_context *main_ctx;
RPTHR *mainthr=NULL;

struct event_base **thread_base=NULL;

struct evdns_base **thread_dnsbase=NULL;
int nthread_dnsbase=0;

#define ST_NONE 0
#define ST_DQ   1
#define ST_SQ   2
#define ST_BT   3
#define ST_BS   4

#define ST_PM   20
#define ST_PN   21



/* mutex for locking main_ctx when in a thread with other duk stacks open */
pthread_mutex_t ctxlock;
RPTHR_LOCK *rp_ctxlock;

/* mutex for locking around slist operations on the timeout structures*/
pthread_mutex_t slistlock;
RPTHR_LOCK *rp_slistlock;

#define RPCOL_RED   "\x1B[31m"
#define RPCOL_GRN   "\x1B[32m"
#define RPCOL_YEL   "\x1B[33m"
#define RPCOL_BLU   "\x1B[34m"
#define RPCOL_MAG   "\x1B[35m"
#define RPCOL_CYN   "\x1B[36m"
#define RPCOL_WHT   "\x1B[37m"
#define RPCOL_RESET "\x1B[0m"

#define RP_REPL_GREETING   RPCOL_MAG    \
    "         |>>            |>>\n"     \
    "       __|__          __|__\n"     \
    "      \\  |  /         \\   /\n"   \
    "       | ^ |          | ^ |  \n"   \
    "     __| o |__________| o |__\n"   \
    "    [__|_|__|(rp)|  | |______]\n"  \
    "____[|||||||||||||__|||||||||]____\n" \
    "RAMPART__powered_by_Duktape_" DUK_GIT_DESCRIBE RPCOL_RESET

#define RP_REPL_PREFIX "rampart> "
#define RP_REPL_PREFIX_CONT "... "

char *serverscript = 
"var server=require(\"rampart-server\");\n"
"rampart.globalize(rampart.utils);\n"
"var message = \"Go to http://localhost:8088/ to see the demos in this distribution.\";\n"

"var arg, full=-1, printdefaults=0;\n"

"wd = getcwd();\n"

"var opts = {\n"
"\"--ipAddr\":       \"The ipv4 address to bind\",\n"
"\"--ipv6Addr\":     \"The ipv6 address to bind\",\n"
"\"--bind-all\":     \"set ipAddr and ipv6Addr to '0.0.0.0' and '[::]' respectively\",\n"
"\"--ipPort\":       \"set ipv4 port\",\n"
"\"--ipv6Port\":     \"set ipv6 port\",\n"
"\"--port\":         \"set both ipv4 and ipv6 port\",\n"
"\"--htmlRoot\":     \"root directory from which to serve files\",\n"
"\"--appsRoot\":     \"root directory from which to serve apps\",\n"
"\"--wsappsRoot\":   \"root directory from which to serve wsapps\",\n"
"\"--dataRoot\":     \"setting for user scripts\",\n"
"\"--logRoot\":      \"log directory\",\n"
"\"--accessLog\":    \"log file name\",\n"
"\"--errorLog\":     \"error log file name\",\n"
"\"--logging\":      \"boolean - whether to log requests and errors\",\n"
"\"--rotateLogs\":   \"boolean - whether to rotate the logs\",\n"
"\"--rotateInterval\": \"interval between log rotations in seconds\",\n"
"\"--rotateStart\":    \"time to start log rotations\",\n"
"\"--user\":           \"if started as root, switch to this user\",\n"
"\"--threads\":        \"number - limit the number of threads used by the server\",\n"
"\"--sslKeyFile\":     \"if https, the ssl/tls key file location\",\n"
"\"--sslCertFile\":    \"if https, the ssl/tls cert file location\",\n"
"\"--secure\":         \"boolean - whether to use https.  If true\",\n"
"\"--letsencrypt\":    \"if using letsencrypt, the domain name for automatic setup of https\\n                     (looks for '/etc/letsencrypt/live/domain.com/' directory)\",\n"
"\"--rootScripts\":    \"whether to treat *.js files in htmlRoot as apps (not secure)\",\n"
"\"--dirList\":        \"whether to provide a directory listing if no index.html is found\",\n"
"\"--daemon\":         \"whether to detach from terminal\",\n"
"\"-d\":               \"alias for '--daemon true'\",\n"
"\"--detach\":         \"alias for '--daemon true'\",\n"
"}\n"

"for (var i=1; i<process.argv.length; i++)\n"
"{\n"
"    arg =  process.argv[i]\n"

"    if(arg == \"--server\" || arg == \"--quickserver\") {\n"
"        if(full!=-1)\n"
"        {\n"
"            fprintf(stderr, \"Error: arg '%s' -- type of server already set\\n\", arg);\n"
"        }\n"
"        full=0;\n"
"        if(arg == \"--server\")\n"
"            full=1;\n"

"        process.argv[i]='--skip';\n"
"    }\n"

"    var st = stat(arg);\n"
"    if(st && st.isDirectory) {\n"
"        wd=realPath(arg);\n"
"        process.argv[i]='--skip';\n"
"    }\n"

"    if (arg == \"--lsopts\") {\n"
"        for(var key in opts) printf(\"%-20s %s\\n\",key,opts[key]);\n"
"        process.exit(0);\n"
"    }\n"

"    if (arg == \"--help\" || arg == '-h')\n"
"    {\n"
"        printf(\"rampart built-in server help:\\n\"+\n"
"\"\\nUsage: rampart --[quick]server [options] root_dir\\n\" +\n"
"\"    --server        - run as a full server\\n\" +\n"
"\"    --quickserver   - run as a test server\\n\" +\n"
"\"    [--help|-h]     - this help message\\n\" +\n"
"\"    --lsopts        - print details on all options\\n\" +\n"
"\"    --showdefaults  - print the list of default settings for --server or --quickserver\\n\" +\n"
"\"    --bind-all      - bind to all ip addresses (default is localhost only)\\n\" +\n"
"\"    [-d|--detach]   - same as '--daemon true'\\n\" +\n"
"\"    --OPTION [val]  - where OPTION is one of options listed from '--lsopts'\\n\" +\n"
"\"    OPTION=val      - alternative format for '--OPTION val'\\n\"  +\n"
"\"\\nIf root_dir is not specified, the current directory will be used\\n\");\n"
"        process.exit(0);\n"
"    }\n"

"    if(arg == '--showdefaults')\n"
"        printdefaults=1;\n"
"}\n"


"process.scriptPath = wd;\n"

"if(full==1) {\n"
"    var serverConf = {\n"
"        ipAddr:       \"127.0.0.1\",\n"
"        ipv6Addr:     \"[::1]\",\n"
"        ipPort:         8088,\n"
"        ipv6Port:       8088,\n"
"        port:           -1,\n"
"        htmlRoot:       wd + \"/html\",\n"
"        appsRoot:       wd + \"/apps\",\n"
"        wsappsRoot:     wd + \"/wsapps\",\n"
"        dataRoot:       wd + \"/data\",\n"
"        logRoot:        wd+\"/logs\",\n"
"        accessLog:      wd+\"/logs/access.log\",\n"
"        errorLog:       wd+\"/logs/error.log\",\n"
"        logging:        false,\n"
"        rotateLogs:     false,\n"
"        rotateInterval: 86400,\n"
"        rotateStart:    \"02:00\",\n"
"        user:           \"nobody\",\n"
"        threads:        -1,\n"
"        sslKeyFile:     \"\",\n"
"        sslCertFile:    \"\",\n"
"        secure:         false,\n"
"        letsencrypt:    false,\n"
"        rootScripts:    false,\n"
"        dirList:        false,\n"
"        daemon:         true\n"
"    }\n"

"} else {\n"
"    var serverConf = {\n"
"        ipAddr:       \"127.0.0.1\",\n"
"        ipv6Addr:     \"[::1]\",\n"
"        ipPort:         8088,\n"
"        ipv6Port:       8088,\n"
"        port:           -1,\n"
"        htmlRoot:       wd + \"/\",\n"
"        appsRoot:       false,\n"
"        wsappsRoot:     false,\n"
"        dataRoot:       false,\n"
"        logRoot:        wd+\"/logs\",\n"
"        accessLog:      wd+\"/logs/access.log\",\n"
"        errorLog:       wd+\"/logs/error.log\",\n"
"        logging:        false,\n"
"        rotateLogs:     false,\n"
"        rotateInterval: 86400,\n"
"        rotateStart:    \"02:00\",\n"
"        user:           \"nobody\",\n"
"        threads:        1,\n"
"        sslKeyFile:     \"\",\n"
"        sslCertFile:    \"\",\n"
"        secure:         false,\n"
"        letsencrypt:    false,\n"
"        rootScripts:    false,\n"
"        dirList:        true,\n"
"        daemon:         false\n"
"    }\n"
"}\n"

"if(printdefaults) {\n"
"    printf(\"Defaults for %s:\\n%3J\\n\", full==1 ? \"--server\" : \"--quickserver\", serverConf);\n"
"    process.exit(0);\n"
"}\n"


"var val;\n"
"for (i=2; i<process.argv.length; i++)\n"
"{\n"
"    arg=process.argv[i];\n"
/* 100 */
"    if(arg==\"--skip\")\n"
"        continue;\n"

"    if(arg==\"--bind-all\")\n"
"    {\n"
"        serverConf.ipAddr=\"0.0.0.0\";\n"
"        serverConf.ipv6Addr=\"[::]\";\n"
"    }\n"

"    if(arg==\"--bind-all\")\n"
"    {\n"
"        serverConf.ipAddr=\"0.0.0.0\";\n"
"        serverConf.ipv6Addr=\"[::]\";\n"
"        continue;\n"
"    } \n"

"    if(arg==\"--detach\" || arg==\"-d\") {\n"
"        arg=\"daemon\";\n"
"        val=true;\n"
"    } else if(arg.charAt(0)=='-' && arg.charAt(1)=='-') {\n"
"        arg = arg.substring(2);\n"
"        if(arg==\"logging\" || arg==\"rotateLogs\" || arg==\"secure\")\n"
"        {\n"
"            val=true;\n"
"        } else if(i+1 >= process.argv.length)\n"
"        {\n"
"            fprintf(stderr, \"option '--%s' must be followed by a value\\n\", arg);\n"
"            process.exit(1);\n"
"        } else {\n"
"            i++;\n"
"            val=process.argv[i];\n"
"        }\n"
"    } else {\n"
"        var argval = arg.split('=');\n"
"        if(argval.length>1)\n"
"        {\n"
"            arg=argval[0];\n"
"            if(argval.length>2) {\n"
"                argval.shift();\n"
"                val=argval.join('=');\n"
"            } else\n"
"                val=argval[1];\n"
"        }\n"
"    }\n"

"    if(val==\"true\")\n"
"        val=true;\n"
"    else if (val==\"false\")\n"
"        val=false;\n"
"    else {\n"
"        var nval=parseInt(val);\n"
"        if(!isNaN(nval) && sprintf('%s',nval)==val)\n"
"            val=nval;\n"
"    }\n"
"    if(serverConf[arg]===undefined)\n"
"    {\n"
"        printf(\"'%s' is an invalid option, a non-existant root directory or a duplicate root directory.\\n\", arg);\n"
"        process.exit(1);\n"
"    }\n"
"    serverConf[arg]=val;\n"
"}\n"

"if (serverConf.port > 0 ){\n"
"    serverConf.ipPort=serverConf.ipv6Port=serverConf.port;\n"
"}\n"

"if (serverConf.letsencrypt)\n"
"{\n"
"    serverConf.sslKeyFile=\"/etc/letsencrypt/live/\"+serverConf.letsencrypt+\"/privkey.pem\";\n"
"    serverConf.sslCertFile=\"/etc/letsencrypt/live/\"+serverConf.letsencrypt+\"/fullchain.pem\";\n"
"    serverConf.secure=true;\n"
"}\n"

"var usr1;\n"

"var bind = [];\n"

"if(serverConf.ipAddr && serverConf.ipPort)\n"
"    bind.push(serverConf.ipAddr + \":\" + serverConf.ipPort);\n"

"if(serverConf.ipv6Addr && serverConf.ipv6Port)\n"
"    bind.push(serverConf.ipv6Addr + \":\" + serverConf.ipv6Port);\n"
"    \n"
"if(!bind.length)\n"
"    throw(\"No ip addr/port specified\");\n"

"var iam = trim(exec('whoami').stdout);\n"

"if(iam != \"root\") {\n"
"    if(serverConf.ipPort < 1024)\n"
"        throw(\"Error: script must be started as root to bind to IP port \" + serverConf.ipPort);\n"
"    if(serverConf.ipv6Port < 1024)\n"
"        throw(\"Error: script must be started as root to bind to IPv6 port \" + serverConf.ipv6Port);\n"
"}\n"

"var serverpid;\n"

"var gzip = trim ( exec('which','gzip').stdout );\n"

"if(serverConf.logging && serverConf.rotateLogs) {\n"

"    var tdelay, mdelay, startTime;\n"

"    if (typeof serverConf.rotateInterval != 'number')\n"
"    {\n"
"        fprintf(stderr, \"serverConf.rotateInterval == %J is invalid\\n\", serverConf.rotateInterval);\n"
"        process.exit(1);\n"
"    }\n"

"    if( serverConf.rotateInterval < 300 ) {\n"
"        fprintf(stderr, \"serverConf.rotateInterval is set to less than 5 minutes, is that what your really want?\\n\");\n"
"        process.exit(1);\n"
"    }\n"

"    mdelay = serverConf.rotateInterval * 1000;\n"

"    try {\n"
"        startTime = scanDate(serverConf.rotateStart + \" \"+ dateFmt('%z'), \"%H:%M %z\");\n"

"        if( startTime < Date.now() )\n"
"            startTime = new Date(startTime.getTime() + 86400000);\n"
"    } catch(e) {\n"
"        fprintf(stderr, \"Error parsing log rotation start time (%s): %J\\n\", serverConf.rotateStart,e);\n"
"        process.exit(1);\n"
"    }\n"

"    var now = new Date();\n"
"    tdelay = startTime.getTime() - now.getTime();\n"

"    if (!usr1) {\n"
"        try {\n"
"            usr1 = shell(\"kill -l\").stdout;\n"
"            usr1 = usr1.match( /(\\d+)\\) SIGUSR1/ );\n"
"            if(usr1.length>1) usr1 = parseInt(usr1[1]);\n"
"            else throw(\"Could not get signal number for SIGUSR1\");\n"
"        } catch (e){}\n"
"    }\n"

"    if ( typeof usr1 != 'number' ) {\n"
"        fprintf(stderr, \"Error finding kill signal number for signal 'USR1'\\n\");\n"
"        process.exit(1);\n"
"    }\n"

"    var prevAbackup, prevEbackup;\n"

"    function rotateLogs() {\n"
"        var doARotate=false, doErotate=false;\n"
"        var ds = dateFmt('%Y-%m-%d-%H-%M-%S');\n"
"        var abackup = sprintf('%s-%s', serverConf.accessLog, ds);\n"
"        var ebackup = sprintf('%s-%s', serverConf.errorLog,  ds);\n"

"        if( stat(serverConf.accessLog) ){\n"
"            doARotate=true;\n"
"            try {\n"
"                rename( serverConf.accessLog, abackup);\n"
"            } catch(e) {\n"
"                fprintf(serverConf.errorLog, true, \"Cannot rename accessLog: %J\\n\", e);\n"
"                doARotate=false;\n"
"            }\n"
"        }\n"

"        if( stat(serverConf.errorLog) ){\n"
"            doERotate=true;\n"
"            try {\n"
"                rename( serverConf.errorLog, ebackup);\n"
"            } catch(e) {\n"
"                fprintf(serverConf.errorLog, true, \"Cannot rename errorLog: %J\\n\", e);\n"
"                doErotate=false;\n"
"            }\n"
"        }\n"

"        if(doARotate||doErotate)\n"
"            kill(serverpid, usr1);// close and reopen logs\n"
"        if(gzip && (prevAbackup || prevEbackup) ) {\n"
"            shell(gzip + \" -q \" + (prevAbackup ? prevAbackup:\"\")+ \" \" + (prevEbackup?prevEbackup:\"\"), {background:true} );\n"
"        }\n"

"        prevAbackup=abackup;\n"
"        prevEbackup=ebackup;\n"
"    }\n"
"    setTimeout( function(){\n"
"        if( !serverpid ) {\n"
"            try {\n"
"                var pid = readFile(wd+\"/server.pid\");\n"
"                serverpid = parseInt(readFile(wd+\"/server.pid\",{returnString:true}));\n"
"            } catch(e) {}\n"

"            if(typeof serverpid != 'number' || Number.isNaN(serverpid) ) {\n"
"                fprintf(serverConf.errorLog, true, \"Cannot get server pid. Log rotation failed.\\n\");\n"
"                return;\n"
"            }\n"
"        }\n"
"        rotateLogs();\n"
"        var iv=setMetronome(rotateLogs, mdelay);\n"
"    }, tdelay);\n"
"}\n"

"var map ={\n"
"    \"/\":                serverConf.htmlRoot,\n"
"}\n"
"if(serverConf.appsRoot)\n"
"    map[\"/apps/\"] = {modulePath: serverConf.appsRoot};\n"

"if(serverConf.wsappsRoot)\n"
"    map[\"ws://wsapps/\"] = {modulePath: serverConf.wsappsRoot};\n"

"if(serverConf.rootScripts) {\n"
"    var scripts = readDir(serverConf.htmlRoot).filter(function(f){return /\\.js$/.test(f);});\n"
"    scripts.forEach (function(sn) {\n"
"        var p = '/' + sn.replace(/\\.js$/,\"\") + '/';\n"
"        map[p]={module: serverConf.htmlRoot+'/'+sn};\n"
"    });\n"
"}\n"


"serverpid=server.start(\n"
"{\n"
"    bind:           bind,\n"
"    user:           serverConf.user,\n"
"    scriptTimeout:  20.0,\n"
"    connectTimeout: 20.0,\n"
"    developerMode:  true,\n"
"    log:            serverConf.logging,\n"
"    accessLog:      serverConf.accessLog,\n"
"    errorLog:       serverConf.errorLog,\n"
"    daemon:         serverConf.daemon,\n"
"    threads:        serverConf.threads,\n"
"    secure:         serverConf.secure,\n"
"    sslKeyFile:     serverConf.sslKeyFile,\n"
"    sslCertFile:    serverConf.sslCertFile,\n"
"    mimeMap:        { \"mp3\": \"audio/mp3\" },\n"
"    directoryFunc:  serverConf.dirList,\n"
"    map:            map\n"
"});\n"

"fprintf(wd+\"/server.pid\", \"%d\", serverpid);\n"
"if(iam == \"root\")\n"
"    chown({user:serverConf.user, path:wd+\"/server.pid\"});\n"

"sleep(0.5);\n"

"if(!kill(serverpid, 0)) {\n"
"    printf(\"Failed to start webserver\\n\");\n"
"    process.exit(1);\n"
"}\n"

"if(serverConf.daemon) {\n"
"    printf(\"Server has been started. \" + message + \"\\n\" +\n"
"\"    Server pid is \" + serverpid + \".  To stop server use kill as such:\\n\" +\n"
"\"       kill ${serverpid} \\n\");\n"
"    process.exit(0);\n"
"}\n";



char *words[]={
    "Array",
    "Array.isArray",
    "CBOR",
    "CBOR.encode",
    "CBOR.decode",
    "Date",
    "Date.now",
    "Date.parse",
    "Date.UTC",
    "Duktape",
    "Duktape.verion",
    "Duktape.env",
    "Duktape.fin",
    "Duktape.enc",
    "Duktape.dec",
    "Duktape.info",
    "Duktape.act",
    "Duktape.gc",
    "Duktape.compact",
    "Duktape.errThrow",
    "Duktape.Pointer",
    "Duktape.Thread",
    "Math",
    "Math.E",
    "Math.LN2",
    "Math.LOG2E",
    "Math.LOG10E",
    "Math.PI",
    "Math.SQRT1_2",
    "Math.SQRT2",
    "Math.abs",
    "Math.acos",
    "Math.acosh",
    "Math.asin",
    "Math.asinh",
    "Math.atan",
    "Math.atanh",
    "Math.atan2",
    "Math.cbrt",
    "Math.ceil",
    "Math.clz32",
    "Math.cos",
    "Math.cosh",
    "Math.exp",
    "Math.floor",
    "Math.fround",
    "Math.hypot",
    "Math.imul",
    "Math.log",
    "Math.log1p",
    "Math.log10",
    "Math.log2",
    "Math.max",
    "Math.min",
    "Math.pow",
    "Math.random",
    "Math.round",
    "Math.sign",
    "Math.sin",
    "Math.sinh",
    "Math.sqrt",
    "Math.tan",
    "Math.tanh",
    "Math.trunc",
    "NaN",
    "Number",
    "Object",
    "Object.keys",
    "Object.create",
    "Object.getOwnPropertyNames",
    "String",
    "TextDecoder",
    "TextDecoder.decode",
    "TextEncoder",
    "TextEncoder.encode",
    "abstract",
    "arguments",
    "boolean",
    "break",
    "byte",
    "case",
    "catch",
    "catch (e) {",
    "console",
    "console.log",
    "continue",
    "delete",
    "do",
    "else",
    "eval",
    "false",
    "for",
    "function",
    "hasOwnProperty",
    "if",
    "in",
    "instanceof",
    "isNaN",
    "isPrototypeOf",
    "length",
    "new",
    "null",
    "performance",
    "performance.now",
    "process",
    "process.exit",
    "process.env",
    "process.argv",
    "process.scriptPath",
    "prototype",
    "rampart",
    "rampart.utils",
    "rampart.utils.printf",
    "rampart.utils.sprintf",
    "rampart.utils.bprintf",
    "rampart.utils.fopen",
    "rampart.utils.fclose",
    "rampart.utils.fprintf",
    "rampart.utils.fseek",
    "rampart.utils.rewind",
    "rampart.utils.ftell",
    "rampart.utils.fflush",
    "rampart.utils.fread",
    "rampart.utils.fwrite",
    "rampart.utils.hexify",
    "rampart.utils.dehexify",
    "rampart.utils.stringToBuffer",
    "rampart.utils.bufferToString",
    "rampart.utils.objectToQuery",
    "rampart.utils.queryToObject",
    "rampart.utils.readFile",
    "rampart.utils.stat",
    "rampart.utils.lstat",
    "rampart.utils.exec",
    "rampart.utils.shell",
    "rampart.utils.kill",
    "rampart.utils.mkdir",
    "rampart.utils.rmdir",
    "rampart.utils.readdir",
    "rampart.utils.copyFile",
    "rampart.utils.rmFile",
    "rampart.utils.link",
    "rampart.utils.symlink",
    "rampart.utils.chmod",
    "rampart.utils.touch",
    "rampart.utils.rename",
    "rampart.utils.sleep",
    "rampart.utils.getpid",
    "rampart.utils.getppid",
    "rampart.utils.getType",
    "rampart.utils.stdout",
    "rampart.utils.stderr",
    "rampart.utils.stdin",
    "rampart.utils.load",
    "rampart.utils.use",
    "rampart.globalize",
    "rampart.import",
    "rampart.import.csvFile",
    "rampart.import.csv",
    "rampart.thread()",
    "rampart.thread",
    "require",
    "return",
    "switch",
    "this",
    "throw",
    "true",
    "try",
    "try {",
    "typeof",
    "undefined",
    "valueOf",
    "var",
    "while"
};

int nwords = sizeof(words)/sizeof(char*);

void completion(const char *inbuf, linenoiseCompletions *lc) {
    int i=0, indots=0, outdots=0;
    char *buf=NULL;
    char *s, c;
    char *endchar = " (;{=<>/*-+|&!^?:[";

    /* get last occurence of one of ' ', '(', etc */
    /* and yes I know if this was meant to be efficient,
       I'd start at the end of the string and search backwards for
       each char in endchar.  Doesn't matter here in interactive setting.
    */
    while((c=endchar[i]))
    {
        s=strrchr(inbuf, c);
        if(s>buf)
            buf=s;
        i++;
    }
    if(!buf)
        buf=(char*)inbuf;
    else
        buf++;

    s=(char *)buf;

    while(s)
    {
        s=strchr(s,'.');
        if(s)
        {
            indots++;
            s++;
        }
    }

    for (i=0;i<nwords;i++)
    {
        char *sugg=words[i];
        outdots=0;
        if(!strncmp(sugg, buf, strlen(buf))){
            s=sugg;
            while(s)
            {
                s=strchr(s,'.');
                if(s)
                {
                    outdots++;
                    s++;
                }
            }
            if (outdots == indots)
            {
                if(buf != inbuf)
                {
                    int l = strlen(inbuf) - strlen(buf);
                    char *newsugg = NULL;
                    REMALLOC(newsugg, strlen(inbuf) + strlen(sugg) + 1);
                    strcpy(newsugg, inbuf);
                    *(newsugg+l)='\0';
                    strcat(newsugg, sugg);
                    linenoiseAddCompletion(lc, newsugg);
                    free(newsugg);
                }
                else
                    linenoiseAddCompletion(lc, sugg);
            }
        }
    }
    return;
}

#define EXIT_FUNC struct rp_exit_funcs_s
EXIT_FUNC {
    rp_vfunc func;
    void     *arg;
//char *nl;
};

EXIT_FUNC **exit_funcs = NULL;
EXIT_FUNC **b4loop_funcs = NULL;

pthread_mutex_t exlock;


//void add_exit_func_2(rp_vfunc func, void *arg, char *nl)
void add_exit_func(rp_vfunc func, void *arg)
{
    int n=0;
    EXIT_FUNC *ef;
    RP_PTLOCK(&exlock);
    if(exit_funcs)
    {
        /* count number of funcs */
        while( (ef=exit_funcs[n++]) );

        n++;
    }
    else
        n=2;

    ef=NULL;

    REMALLOC(ef, sizeof(EXIT_FUNC));
    REMALLOC(exit_funcs, n * sizeof (EXIT_FUNC *));
    ef->func = func;
    ef->arg=arg;
    //ef->nl=nl;
    exit_funcs[n-2]=ef;
    exit_funcs[n-1]=NULL;
    RP_PTUNLOCK(&exlock);
}

void add_b4loop_func(rp_vfunc func, void *arg)
{
    int n=0;
    EXIT_FUNC *ef;
    RP_PTLOCK(&exlock);
    if(b4loop_funcs)
    {
        /* count number of funcs */
        while( (ef=b4loop_funcs[n++]) );

        n++;
    }
    else
        n=2;

    ef=NULL;

    REMALLOC(ef, sizeof(EXIT_FUNC));
    REMALLOC(b4loop_funcs, n * sizeof (EXIT_FUNC *));
    ef->func = func;
    ef->arg=arg;
    //ef->nl=nl;
    b4loop_funcs[n-2]=ef;
    b4loop_funcs[n-1]=NULL;
    RP_PTUNLOCK(&exlock);
}

//lock is probably not necessary now
static void free_dns(void)
{
    int i=0;

    if(!thread_dnsbase)
        return;
    CTXLOCK;
    while(i<nthread_dnsbase)
    {
        evdns_base_free(thread_dnsbase[i], 0);
        i++;
    }
    nthread_dnsbase=0;
    free(thread_dnsbase);
    thread_dnsbase=NULL;
    CTXUNLOCK;
}


void duk_rp_exit(duk_context *ctx, int ec)
{
    int i=0,len=0;

    static int ran_already=0;
//printf("%d exiting once%s\n",(int)getpid(), ran_already?" again":"");
    if(ran_already)
        exit(ec);
    ran_already=1;

    free_dns();

    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "exitfuncs");
    len=duk_get_length(ctx, -1);

    for(;i<len;i++)
    {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        duk_call(ctx,0);
        duk_pop(ctx);
    }

    duk_destroy_heap(ctx);
    free(RP_script_path);
    free(RP_script);

    //for lmdb. TODO: make this a generic array of function pointers if/when others need it.
    if(exit_funcs)
    {
        EXIT_FUNC *ef;
        int n=0;

        while( (ef=exit_funcs[n++]) )
        {
            (ef->func)(ef->arg);
            free(ef);
        }
        free(exit_funcs);
    }

    exit(ec);
}

void run_b4loop_funcs()
{
    if(b4loop_funcs)
    {
        EXIT_FUNC *ef;
        int n=0;

        while( (ef=b4loop_funcs[n++]) )
        {
            (ef->func)(ef->arg);
            free(ef);
        }
        free(b4loop_funcs);
    }

}

static void evhandler_repl(int sig, short events, void *unused)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
}


char * tickify(char *src, size_t sz, int *err, int *ln);

pthread_mutex_t repl_lock;
#define REPL_LOCK    do {RP_PTLOCK(&repl_lock); /*printf("Locked\n"); */ } while(0)
#define REPL_UNLOCK  do {RP_PTUNLOCK(&repl_lock); /* printf("Unlocked\n"); */} while (0)

static void *repl_thr(void *arg)
{
    printf("%s\n", RP_REPL_GREETING);
    char *line=NULL, *lastline=NULL;
    char *prefix=RP_REPL_PREFIX;
    char histfn[PATH_MAX];
    char *hfn=NULL;
    char *home = getenv("HOME");
    int err;
    duk_context *ctx = (duk_context *) arg;

    linenoiseSetMultiLine(1);
    linenoiseSetCompletionCallback(completion);
    linenoiseHistorySetMaxLen(1024);
    if(home){
        strcpy(histfn, home);
        strcat(histfn, "/.rampart_history");
        hfn = histfn;
        linenoiseHistoryLoad(hfn);
    }

    while (1)
    {
        int ln;
        char *oldline=NULL;

        if(lastline)
            prefix=RP_REPL_PREFIX_CONT;
        else
            prefix=RP_REPL_PREFIX;

        errno=0;
        line = linenoise(prefix);

        if(!line)
        {
            if(errno == EAGAIN) /* ctrl-c */
            {
                printf("%spress ctrl-d to exit%s\n",RPCOL_BLU, RPCOL_RESET);
                if(lastline)
                {
                    free(lastline);
                    lastline=NULL;
                }
                continue;
            }
            duk_rp_exit(ctx, 0);
        }

        oldline = line;
        linenoiseHistoryAdd(oldline);
        line = tickify(line, strlen(line), &err, &ln);
        if (!line)
            line=oldline;
        else
            free(oldline);

        if(lastline){

            lastline = strjoin(lastline, line, '\n');
            free(line);
            line=lastline;

            oldline=line;
            line = tickify(line, strlen(line), &err, &ln);
            if (!line)
                line=oldline;
            else
                free(oldline);
        }

        if(*line=='\0')
        {
            free(line);
            continue;
        }

        REPL_LOCK;

        duk_push_string(ctx, line);
        lastline=NULL;

        // evaluate input
       if (duk_peval(ctx) != 0)
        {
            const char *errmsg=duk_safe_to_string(ctx, -1);
            if(strstr(errmsg, "end of input") || (err && err < 4) ) //command likely spans multiple lines
            {
                lastline=line;
            }
            else
                printf("%sERR: %s%s\n", RPCOL_RED, errmsg, RPCOL_RESET);
        }
        else
        {
            printf("%s%s%s\n", RPCOL_BLU, duk_safe_to_stacktrace(ctx, -1), RPCOL_RESET);
        }
        duk_pop(ctx); //the results

        //resume loop by locking main thread
        REPL_UNLOCK;

        if(hfn)
            linenoiseHistorySave(hfn);

        if(!lastline)
            free(line);

    }
    return NULL;
}


static int repl(duk_context *ctx)
{
    pthread_t thr;
    pthread_attr_t attr;
    struct event ev_sig;
    struct timeval to={0};

    RP_PTINIT(&repl_lock);

    pthread_attr_init(&attr);

    if( pthread_create( &thr, &attr, repl_thr, (void*)ctx) )
        RP_THROW(ctx, "Could not create thread\n");

    event_assign(&ev_sig, mainthr->base, -1,  0, evhandler_repl, NULL);
    event_add(&ev_sig, &to);

    /* start event loop, but don't block repl thread */
    while(1)
    {
        REPL_LOCK;
        event_base_loop(mainthr->base, EVLOOP_NONBLOCK);
        REPL_UNLOCK;
        usleep(50000);
    }

    // won't get here
    pthread_join(thr, NULL);

    return 0;
}

static char *checkbabel(char *src)
{
    char *s=src, *bline, *ret=NULL;

    /* skip comments at top of file */
    while(isspace(*s)) s++;
    while (*s=='/')
    {
        s++;
        if(*s=='/')
        {
            while(*s && *s!='\n') s++;
            if(!*s) break;
        }
        else if (*s=='*')
        {
            char *e=(char*)memmem(s,strlen(s),"*/",2);
            if(e)
                s=e+2;
            else
                break;
        }
        else
            break;
        while(isspace(*s)) s++;
    }
    //printf("'%s'\n",s);
    bline=s;
    /* check for use "babel:{options}" or use "babel" */
#define invalidformat do{\
    fprintf(stderr,"invalid format: \"use babel:{ options }\"\n");\
    exit(1);\
}while(0)

    if(!strncmp("\"use ",s,5) )
    {
        if(*s=='\n') s++;
        s+=5;
        if (!strncmp("babel",s,5))
        {
            char *e;
            s+=5;
            while(isspace(*s)) s++;
            if(*s==':')
            {
                s++;
                while(isspace(*s)) s++;
                if(*s!='{')
                    invalidformat;
                e=s;
                /* must end with } before a " or \n */
                while(*e != '"' && *e != '\n') e++;
                if(*e!='"')
                    invalidformat;

                e--;
                while(isspace(*e)) e--;

                if(*e!='}')
                    invalidformat;

                e++;

                {
                    char opt[1+e-s];

                    strncpy(opt,s,e-s);
                    opt[e-s]='\0';
                    //file_src=(char *)duk_rp_babelize(ctx, argv[0], file_src, opt, entry_file_stat.st_mtime);
                    ret=strdup(opt);
                }
            }
            else if (*s=='"')
            {
                //file_src=(char *)duk_rp_babelize(ctx, argv[0], file_src, NULL, entry_file_stat.st_mtime);
                ret=strdup ("{ presets: ['env'],retainLines:true }");
            }
            /* replace "use babel" line with spaces, to preserve line nums */
            while (*bline && *bline!='\n') *bline++ = ' ';
            return(ret);
        }
        return NULL;
    }
    return NULL;
}

char *main_babel_opt=NULL;

/* babelized source is left on top of stack*/
const char *duk_rp_babelize(duk_context *ctx, char *fn, char *src, time_t src_mtime, int exclude_strict, char *opt)
{
    char *s, *babelcode=NULL;
    struct stat babstat;
    char babelsrc[strlen(fn)+10];
    FILE *f;
    size_t read;
    char *pfill="babel-polyfill.js";
    char *pfill_bc=".babel-polyfill.bytecode";
    duk_size_t bsz=0;
    void *buf;
    RPPATH rppath;

    babelsrc[0]='\0';

    if(!opt)
    {
        opt=checkbabel(src);
        if(!opt) return NULL;
    }

    main_babel_opt=opt;

    /* check if polyfill is already loaded */
    duk_eval_string(ctx,"global._babelPolyfill");
    if(duk_get_boolean_default(ctx,-1,0))
    {
        duk_pop(ctx);
        goto transpile;
    }
    duk_pop(ctx);

    rppath=rp_find_path(pfill_bc,"lib/rampart_modules/");
    if(!strlen(rppath.path))
        rppath=rp_find_path(pfill_bc,"modules/");

    if(strlen(rppath.path))
    {
        pfill_bc=rppath.path;
        /* load polyfill bytecode cache */
        f=fopen(pfill_bc,"r");
        if(!f)
        {
            fprintf(stderr,"cannot open '%s': %s\n",pfill_bc,strerror(errno));
        }
        else
        {
            buf=duk_push_fixed_buffer(ctx,(duk_size_t)rppath.stat.st_size);

            read=fread(buf,1,rppath.stat.st_size,f);
            if(read != rppath.stat.st_size)
            {
                fprintf(stderr,"error fread(): error reading file '%s'\n",pfill_bc);
            }
            else
            {
                duk_load_function(ctx);
                goto callpoly;
            }
        }
    }

    /* not found, so load and save it */
    rppath=rp_find_path(pfill,"lib/rampart_modules/");
    if (!strlen(rppath.path))
        rppath=rp_find_path(pfill,"modules/");

    if (!strlen(rppath.path))
    {
        fprintf(stderr,"cannot locate babel-polyfill.min.js\n");
        exit(1);
    }
    pfill=rppath.path;

    f=fopen(pfill,"r");
    if(!f)
    {
        fprintf(stderr,"cannot open '%s': %s\n",pfill,strerror(errno));
        exit(1);
    }
    REMALLOC(babelcode, rppath.stat.st_size);
    read=fread(babelcode, 1, rppath.stat.st_size,f);
    if(read != rppath.stat.st_size)
    {
        fprintf(stderr,"error fread(): error reading file '%s'\n",pfill);
        exit(1);
    }
    duk_push_lstring(ctx,babelcode,(duk_size_t)rppath.stat.st_size);
    free(babelcode);
    babelcode=NULL;
    fclose(f);

    duk_push_string(ctx, pfill);
    if (duk_pcompile(ctx, 0) == DUK_EXEC_ERROR)
    {
        fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
        duk_rp_exit(ctx, 1);
    }

    /* write bytecode out */
    rppath=rp_get_home_path(pfill_bc,"modules/");
    pfill_bc=rppath.path;
    duk_dup(ctx,-1);
    duk_dump_function(ctx);
    buf=duk_get_buffer_data(ctx,-1,&bsz);
    if(!strlen(pfill_bc) || !(f=fopen(pfill_bc,"w")) )
    {
        fprintf(stderr,"cannot open '%s' for write: %s\n",pfill_bc,strerror(errno));
    }
    else
    {
        size_t wrote;
        wrote=fwrite(buf,1,(size_t)bsz,f);
        if(wrote!=(size_t)bsz)
        {
            fprintf(stderr,"error fwrite(): error writing file '%s'\n",pfill_bc);
            if(wrote>0 && unlink(pfill_bc))
            {
                fprintf(stderr,"error unlink(): error removing '%s'\n",pfill_bc);
            }
        }
        fclose(f);
    }
    duk_pop(ctx);

    /* call the polyfill */
    callpoly:

    if (duk_pcall(ctx, 0) == DUK_EXEC_ERROR)
    {
        fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
        duk_rp_exit(ctx, 1);
    }
    duk_pop(ctx);
    duk_eval_string(ctx,"global._babelPolyfill=true;");
    duk_pop(ctx);

    transpile:
    if(strcmp("stdin",fn) != 0  && strcmp("eval_code",fn) != 0)
    {
        /* file.js => file.babel.js */
        /* skip the first char in case of "./file" */
        s=strrchr(fn+1,'.');
        if(s)
        {
            size_t l=s-fn;
            strncpy(babelsrc,fn,l);
            babelsrc[l]='\0';
            strcat(babelsrc,".babel");
            strcat(babelsrc,s);
        }
        else
        {
            strcpy (babelsrc, fn);
            strcat (babelsrc, ".babel.js");
        }

        /* does the file.babel.js exist? */
        if (stat(babelsrc, &babstat) != -1)
        {
            /* is it newer than the file.js */
            if(babstat.st_mtime >= src_mtime)
            {
                /* load the cached file.babel.js */
                REMALLOC(babelcode,babstat.st_size);

                f=fopen(babelsrc,"r");
                if(f==NULL)
                {
                    fprintf(stderr,"error fopen(): error opening file '%s': %s\n",babelsrc,strerror(errno));
                }
                else
                {
                    read=fread(babelcode, 1, babstat.st_size, f);

                    if(read != babstat.st_size)
                    {
                        fprintf(stderr,"error fread(): error reading file '%s'\n", babelsrc);
                    }
                    if(exclude_strict)
                    {
                        int k=0;
                        while(k<13)
                            babelcode[k++]=' ';
                    }
                    duk_push_lstring(ctx, babelcode, (duk_size_t)babstat.st_size);
                    free(babelcode);
                    babelcode=(char *)duk_get_string(ctx,-1);
                    fclose(f);
                    goto end;
                }
            }
        }
    }
    /* file.babel.js does not exist */
    /* load babel.min.js as a module and convert file.js */
    duk_push_sprintf(ctx, "function(input){var b=require('babel');return b.transform(input, %s ).code;}", opt);
    duk_push_string(ctx,fn);
    if (duk_pcompile(ctx, DUK_COMPILE_FUNCTION) == DUK_EXEC_ERROR)
    {
        fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
        duk_rp_exit(ctx, 1);
    }
    duk_push_string(ctx,src);
    if (duk_pcall(ctx, 1) == DUK_EXEC_ERROR)
    {
        fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
        duk_rp_exit(ctx, 1);
    }
    babelcode=(char *)duk_get_lstring(ctx,-1,&bsz);
    if(strcmp("stdin",fn) != 0  && strcmp("eval_code",fn) != 0)
    {
        f=fopen(babelsrc,"w");
        if(f==NULL)
        {
            fprintf(stderr,"error fopen(): error opening file '%s'\n",babelsrc);
        }
        else
        {
            size_t wrote;
            wrote=fwrite(babelcode,1,(size_t)bsz,f);
            if(wrote!=(size_t)bsz)
            {
                fprintf(stderr,"error fwrite(): error writing file '%s'\n",babelsrc);
                if(wrote>0 && unlink(babelsrc))
                {
                    fprintf(stderr,"error unlink(): error removing '%s'\nNot continuing\n",babelsrc);
                    exit(1);
                }
            }
            fclose(f);
        }
    }

    if(exclude_strict)
    {
        duk_push_string(ctx, babelcode+13);
        duk_replace(ctx, -2);
    }

    end:
    //free(opt);
    return (const char*) (strlen(babelsrc)) ? strdup(babelsrc): strdup(fn);
}

struct slisthead tohead={0};

/* Pretty sure there should be unfreed timeout structs only if
   there is an explicit process.exit() before the timeout expires */

static void free_tos (void *arg)
{
    EVARGS *e;
    SLISTLOCK;  //probably not needed
    while (!SLIST_EMPTY(&tohead))
    {
        e = SLIST_FIRST(&tohead);
        SLIST_REMOVE_HEAD(&tohead, entries);
        if(RPTHR_TEST(get_current_thread(), RPTHR_FLAG_BASE))
        {
            event_del(e->e);
            event_free(e->e);
        }
        free(e);
    }
    SLISTUNLOCK;
}

void timespec_add_ms(struct timespec *ts, duk_double_t add)
{
    time_t secs = (time_t) add / 1000;

    add -= (double) (secs*1000.0);
    add *= 1000000;

    ts->tv_sec += secs;

    ts->tv_nsec += (long)add;

    if(ts->tv_nsec > 1000000000)
        ts->tv_sec++;
    else if (ts->tv_nsec < 0)
        ts->tv_sec--;
    else
        return;

    ts->tv_nsec = ts->tv_nsec % 1000000000;
}

duk_double_t timespec_diff_ms(struct timespec *ts1, struct timespec *ts2)
{
    double ret;

    ret = 1000.0 * ( (double)ts1->tv_sec - (double)ts2->tv_sec );

    ret += ( (double)ts1->tv_nsec - (double)ts2->tv_nsec ) / 1000000.0;

    return ret;
}


static void rp_el_doevent(evutil_socket_t fd, short events, void* arg)
{
    EVARGS *evargs = (EVARGS *) arg;
    duk_context *ctx= evargs->ctx;
    double key= evargs->key;
    duk_idx_t nargs = 0, func_idx;
    int cbret=1, do_js_cb=0;
    const char *fname = "setTimeout/setInterval";

    duk_set_top(ctx, 0);

    duk_push_global_stash(ctx);
    if( !duk_get_prop_string(ctx,-1, "ev_callback_object") )
    {
        RP_THROW(ctx, "internal error in rp_el_doevent()");
    }


    // do C timeout callback immediately before the JS callback
    // this is for a generic callback, not used for setTimeout/setInterval
    if(evargs->cb)
        cbret=(evargs->cb)(evargs->cbarg, 0);

    if(!cbret) //if return 0, skip js callback and second c callback
        goto to_doevent_end;


    // the JS callback function
    duk_push_number(ctx, evargs->key);
    duk_get_prop(ctx, -2);


    if(duk_is_function(ctx, -1))
    {
        func_idx = duk_get_top_index(ctx);
        do_js_cb=1;
        // get array and extract parameters.
        // ugly hack from duk_rp_set_to below
        duk_push_number(ctx, evargs->key+0.2);
        if(duk_get_prop(ctx, -3))
        {
            //this should always be an array.
            duk_idx_t i = 0, arr_idx = duk_get_top_index(ctx);
            nargs = (duk_idx_t) duk_get_length(ctx,-1);
            for(; i<nargs; i++)
                duk_get_prop_index(ctx, arr_idx, i);

            duk_remove(ctx, arr_idx); //array
        }
        else
            duk_pop(ctx); //undefined
    }
    // if we don't have a function, skip call
    if(!do_js_cb)
        goto to_post_callback;

    if(cbret == 1) //normal, no 'this' not bound to callback
    {
        if(duk_pcall(ctx, nargs) != 0)
        {
            // the function name
            duk_push_number(ctx, evargs->key +0.3);
            duk_get_prop(ctx, 1);
            fname=duk_get_string_default(ctx, -1, fname);
            duk_pop(ctx);

            if (duk_is_error(ctx, -1) )
            {
                duk_get_prop_string(ctx, -1, "stack");
                fprintf(stderr, "Error in %s callback: %s\n", fname, duk_get_string(ctx, -1));
                duk_pop(ctx);
            }
            else if (duk_is_string(ctx, -1))
            {
                fprintf(stderr, "Error in %s callback: %s\n", fname, duk_get_string(ctx, -1));
            }
            else
            {
                fprintf(stderr, "Error in %s callback\n", fname);
            }
        }
    }
    else
    {
        // if cbret == 2, this binding must be left on top of stack from c callback above
        duk_insert(ctx, func_idx+1); //insert 'this' after function

        if(duk_pcall_method(ctx, nargs) != 0)
        {
            // the function name
            duk_push_number(ctx, evargs->key +0.3);
            duk_get_prop(ctx, 1);
            fname=duk_get_string_default(ctx, -1, fname);
            duk_pop(ctx);
            if (duk_is_error(ctx, -1) )
            {
                duk_get_prop_string(ctx, -1, "stack");
                fprintf(stderr, "Error in %s callback: %s\n", fname, duk_get_string(ctx, -1));
                duk_pop(ctx);
            }
            else if (duk_is_string(ctx, -1))
            {
                fprintf(stderr, "Error in %s callback: %s\n", fname, duk_get_string(ctx, -1));
            }
            else
            {
                fprintf(stderr, "Error in %s callback\n", fname);
            }
        }
    }
    //discard return
    duk_pop(ctx);

    /* evargs may have been freed if clearInterval was called from within the function */
    /* if so, function stored in ev_callback_object[key] will have been deleted */
    duk_push_number(ctx, key);
    if(!duk_has_prop(ctx, -2) )
    {
        duk_set_top(ctx, 0);
        return;
    }

    to_post_callback:

    // do post callback
    if(evargs->cb)
        evargs->repeat=(evargs->cb)(evargs->cbarg, 1); // if returns 1, we repeat


    to_doevent_end:
    //setTimeout
    if(evargs->repeat==0)
    {
        SLISTLOCK;
        SLIST_REMOVE(&tohead, evargs, ev_args, entries);
        SLISTUNLOCK;
        event_del(evargs->e);
        event_free(evargs->e);
        duk_push_number(ctx, key);
        duk_del_prop(ctx, -2);
        duk_push_number(ctx, key+0.2);
        duk_del_prop(ctx, -2);
        duk_push_number(ctx, key+0.3);
        duk_del_prop(ctx, -2);
        free(evargs);
    }
    //setInterval, but event has expired.
    else if (evargs->repeat==1 && ( !event_pending(evargs->e, 0, NULL) )) // the event expired
    {
        //setInterval callback may have taken longer than the given interval.
        event_del(evargs->e);
        event_add(evargs->e, &evargs->timeout);
    }
    //setMetronome
    else if(evargs->repeat==2)
    {
        duk_double_t delay=0.0;
        struct timespec now;
        duk_double_t timediff_ms = 0.0;
        struct timeval newto;

        delay = ( (duk_double_t)evargs->timeout.tv_sec * 1000.0) +
                ( (duk_double_t)evargs->timeout.tv_usec/ 1000);

        clock_gettime(CLOCK_MONOTONIC, &now);

        //add next time to our clock.  That is the time we were aiming for.
        timespec_add_ms(&evargs->start_time, delay);

        //get the actual amount of time
        timediff_ms = delay + timespec_diff_ms(&now, &evargs->start_time);

        /* we may need to skip "frames", but will attempt to keep the timing */
        while( timediff_ms > delay)
        {
            timespec_add_ms(&evargs->start_time, delay);
            timediff_ms -= delay;
        }

        if(timediff_ms<0.0) timediff_ms=0.0;
        delay = (delay - timediff_ms)/1000.0;
        newto.tv_sec=(time_t) delay;
        newto.tv_usec=(suseconds_t)1000000.0 * (delay - (double)newto.tv_sec);
        event_del(evargs->e);
        event_add(evargs->e, &newto);
    }

    duk_set_top(ctx, 0);
}


/* It is not terribly important that this is thread safe (I hope).
   If we are threading, it just needs to be unique on each thread (until it loops).
   The id will be used on separate duk stacks for each thread */
volatile uint32_t ev_id=0;

/* this will insert a javascript callback into the event loop for settimeout et al
    ctx              -   thread's duk_context
    repeat           -   0:setTimeout, 1:setInterval, 2:setMetronome
    func_name        -   a javascript function name for error messages (i.e. "setTimeout")
    timeout_callback -   A c callback, int cb(void *arg, int after)
                           called twice, once with after=0, right before the javascript callback function is called,
                           and once with after=1, right after javascript callback function is called.
                         Return value from func with after=0 should be
                                0 for skip js callback and second c callback,
                                1 for ok
                                    OR
                                2 - cbfunc pushed 'this' onto stack for JS callback.
                         Return value from func with after=1 is used to set a new 'repeat' value (0, 1 or 2).
    arg              -   void pointer for above callback.
    func_idx         -   where to find the js callback.  If DUK_INVALID_INDEX, js callback will be skipped
    arg_start_idx    -   where to start looking for arguments in duktape stack to be eventually passed to js callback
                     -   DUK_INVALID_INDEX means don't look for arguments
    to               -   timeout value in seconds

*/
duk_ret_t duk_rp_insert_timeout(duk_context *ctx, int repeat, const char *fname, timeout_callback *cb, void *arg,
        duk_idx_t func_idx, duk_idx_t arg_start_idx, double to)
{
    EVARGS *evargs=NULL;
    duk_idx_t top=duk_get_top(ctx);
    RPTHR *thr=NULL;

    if(func_idx != DUK_INVALID_INDEX)
    {
        func_idx = duk_normalize_index(ctx, func_idx);
        REQUIRE_FUNCTION(ctx, func_idx, "%s(): Callback must be a function", fname ? fname : "setTimeout/setInterval");
    }

    if(arg_start_idx != DUK_INVALID_INDEX)
    {
        arg_start_idx = duk_normalize_index(ctx, arg_start_idx);
    }

    thr=get_current_thread();

    if(!thr->base)
        RP_THROW(ctx, "event base not found.");

    /* set up struct to be passed to callback */
    REMALLOC(evargs,sizeof(EVARGS));
    evargs->key = (double)ev_id++;
    evargs->ctx=ctx;
    evargs->repeat=repeat;
    evargs->cb=cb;
    evargs->cbarg=arg;
    clock_gettime(CLOCK_MONOTONIC, &evargs->start_time);

    SLISTLOCK;
    SLIST_INSERT_HEAD(&tohead, evargs, entries);
    SLISTUNLOCK;

    /* get the timeout */
    evargs->timeout.tv_sec=(time_t)to;
    evargs->timeout.tv_usec=(suseconds_t)1000000.0 * (to - (double)evargs->timeout.tv_sec);

    /* get object of callback functions from global stash */
    duk_push_global_stash(ctx);
    if( !duk_get_prop_string(ctx,-1, "ev_callback_object") )
    {
        /* if in threads, we need to set this up on new duk_context stack */
        duk_pop(ctx);// remove undefined
        duk_push_object(ctx);//new object
        duk_dup(ctx, -1); //make a reference copy
        duk_put_prop_string(ctx, -3, "ev_callback_object"); // put one reference in stash, leave other reference on top
    }
    duk_push_number(ctx, evargs->key); //array-like access with number as key

    if(func_idx != DUK_INVALID_INDEX)
        duk_dup(ctx,func_idx); //the JS callback function
    else
        duk_push_null(ctx);

    duk_put_prop(ctx, -3);

    // parameters to function
    if( func_idx != DUK_INVALID_INDEX && arg_start_idx != DUK_INVALID_INDEX && top > arg_start_idx )
    {
        duk_uarridx_t aidx=0, i;
        duk_push_number(ctx, evargs->key + 0.2);
        duk_push_array(ctx);
        for (i=arg_start_idx; i<top; i++)
        {
            duk_dup(ctx,i);
            duk_put_prop_index(ctx, -2, aidx++);
        }
        duk_put_prop(ctx, -3);
    }

    //the function name
    duk_push_number(ctx, evargs->key + 0.3);
    duk_push_string(ctx, fname);
    duk_put_prop(ctx, -3);

    duk_pop_2(ctx); //ev_callback_object and global stash

    /* create a new event for js callback and specify the c callback to handle it*/
    evargs->e = event_new(thr->base, -1, EV_PERSIST, rp_el_doevent, evargs);

    /* add event; return object { hidden(eventargs): evargs_pointer, eventId: evargs->key} */
    event_add(evargs->e, &evargs->timeout);
    duk_push_object(ctx);
    duk_push_pointer(ctx,(void*)evargs);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("eventargs") );
    duk_push_number(ctx,evargs->key);
    duk_put_prop_string(ctx, -2, "eventId");

    return 1;
}

inline duk_ret_t duk_rp_set_to(duk_context *ctx, int repeat, const char *fname, timeout_callback *cb, void *arg)
{
    return duk_rp_insert_timeout(ctx, repeat, fname, cb, arg, 0, 2,
        duk_get_number_default(ctx,1, 0) / 1000.0
    );
}

duk_ret_t duk_rp_set_timeout(duk_context *ctx)
{
    return duk_rp_set_to(ctx, 0, "setTimeout", NULL, NULL);
}

duk_ret_t duk_rp_set_interval(duk_context *ctx)
{
    return duk_rp_set_to(ctx, 1, "setInterval", NULL, NULL);
}

duk_ret_t duk_rp_set_metronome(duk_context *ctx)
{
    return duk_rp_set_to(ctx, 2, "setMetronome", NULL, NULL);
}

duk_ret_t duk_rp_clear_either(duk_context *ctx)
{
    EVARGS *evargs=NULL, *p;
    int found=0;

    if(!duk_is_object(ctx,0))
        RP_THROW(ctx, "clearTimeout()/clearInteral() requires variable returned from setTimeout()/setInterval()");

    if( !duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("eventargs") ) )
        RP_THROW(ctx, "clearTimeout()/clearInteral() requires variable returned from setTimeout()/setInterval()");

    evargs=(EVARGS *)duk_get_pointer(ctx, 1);

    if(!evargs)
        return 0;


    SLISTLOCK;
    SLIST_FOREACH(p, &tohead, entries)
    {
        if(p == evargs)
        {
            found=1;
            break;
        }
    }
    if(found)
        SLIST_REMOVE(&tohead, evargs, ev_args, entries);
    SLISTUNLOCK;

    if(!found)
        return 0;

    event_del(evargs->e);
    event_free(evargs->e);
    free(evargs);

    duk_push_global_stash(ctx);
    if( !duk_get_prop_string(ctx, -1, "ev_callback_object") )
        RP_THROW(ctx, "internal error in rp_el_doevent()");

    if( !duk_get_prop_string(ctx, 0, "eventId" ) )
        RP_THROW(ctx, "clearTimeout()/clearInteral() requires variable returned from setTimeout()/setInterval()");

    duk_del_prop(ctx, -2);

    return 0;
}

/* tickify (template literal parsing) section */
/* this has grown out of control and needs to be replaced by a proper parser */

#define adv ({in++;})

#define pushstate(state) do{\
    sstack_no++;\
    sstack[sstack_no] = state;\
}while(0)

#define popstate() sstack_no--

#define getstate() sstack[sstack_no]

#define copy(input) do{\
    if ( ! (getstate() == ST_BT || ( sstack_no>1 && sstack[sstack_no-1]==ST_BT) )){\
        if(out==outbeg+osz-1){\
            int pos = out - outbeg;\
            osz+=1024;\
            REMALLOC(outbeg, osz);\
            out = outbeg + pos;\
        }\
        *out=(input);\
        out++;\
    }\
}while(0)

#define stringcopy2(st) do{\
    char *s=(st);\
    while(*s) {\
        copy(*s);\
        s++;\
    }\
}while(0)


#define scopy(input) do{\
    if(!mute) {\
        if(out==outbeg+*osize-1){\
            int pos = out - outbeg;\
            *osize+=4096;\
            REMALLOC(outbeg, *osize);\
            out = outbeg + pos;\
        }\
        *out=(input);\
        out++;\
    }\
    if(input=='\n' && type < 2)(*lineno)++;\
}while(0)

#define stringcopy(st) do{\
    char *s=(st);\
    while(*s) {\
        scopy(*s);\
        s++;\
    }\
}while(0)

/*
    type==0 - template literal
    type==1 - tag function first pass
    type==2 - tag function second pass
*/

static int proc_backtick(char *bt_start, char *end, char **ob, char **o, size_t *osize, int *lineno, int type)
{
    char *out=*o;
    char *in=bt_start;
    char *outbeg = *ob;
    int lastwasbs=0, mute = 0;

    if(type==2)
        mute=1;
    scopy('(');
    /* tag function */
    if(type ==1)
        scopy('[');
    stringcopy("\"\"+\"");
    adv;
    while(in < end)
    {
        switch(*in)
        {
            case '\\':
                scopy('\\');
                lastwasbs = !lastwasbs;
                break;
            case '$':
                if(in+1<end && *(in+1)=='{' && *(in-1) != '\\')
                {
                    char *s, c, *startquote=NULL, *endquote=NULL;
                    in++;
                    if(*in == '\n') (*lineno)++;
                    in++;
                    if(*in == '\n') (*lineno)++;
                    if (type == 1)
                    {
                        stringcopy("\",\"");
                        mute=1;
                    }
                    else
                        mute=0;
                    s=in;
                    while (isspace(*s)) { if (*s=='\n')(*lineno)++; s++;}
                    /* a quoted string followed by ':' */
                    if(s<end && (*s =='\'' || *s == '"') )
                    {
                        startquote=s+1;
                        c=*s;
                        s++;
                        while (  s<end && (*s != c || ( *(s-1)=='\\' && *(s-2)!='\\')  ) )
                        {
                            s++;
                        }
                        if(*s == c)
                        {
                            endquote=s;
                            s++;
                            while (isspace(*s)) { if (*s=='\n')(*lineno)++; s++;}
                            if(*s == ':')
                            {
                                char *p;
                                int inbs=0;
                                if(type == 2)
                                    scopy(',');
                                else
                                    stringcopy("\"+");
                                stringcopy("rampart.utils.sprintf('");
                                p=startquote;
                                while(p<endquote)
                                {
                                    inbs=0;
                                    switch(*p)
                                    {
                                        case '\\':
                                            inbs= !inbs;
                                            p++;
                                            if(*p=='\\')
                                            {
                                                inbs=0;
                                                scopy('\\');
                                                scopy('\\');
                                                break;
                                            }
                                            else if(*p!='\'')
                                            {
                                                scopy('\\');
                                                scopy(*p);
                                                break;
                                            }
                                            /*skip the ' case for fallthrough */
                                        case '\'':
                                            /* possible fall through from above
                                               If in a double quote from input, we need to escape the
                                               single quote, because the output will be single
                                               quoted */
                                            if(*p=='\'' && (inbs || c == '"') )
                                            {
                                                inbs=0;
                                                scopy('\\');
                                            }
                                            scopy(*p);
                                            break;
                                        case '"':
                                            scopy('"');
                                            break;
                                        case '\n':
                                            (*lineno)++;
                                            stringcopy("\\n");
                                            break;
                                        case '\r':
                                            scopy('\\');
                                            scopy('r');
                                            break;
                                        case '\t':
                                            scopy('\\');
                                            scopy('t');
                                            break;
                                        default:
                                            scopy(*p);
                                    }
                                    p++;
                                }
                                stringcopy("',");
                                in = s+1;
                            }
                            else
                                stringcopy("\"+(");
                        }
                        else
                            stringcopy("\"+(");
                    }
                    /* starts with % and unquoted */
                    else if (*s == '%')
                    {
                        if(type == 2)
                            scopy(',');
                        else
                            stringcopy("\"+");
                        stringcopy("rampart.utils.sprintf('");
                        while (in < end && !(*in == ':' && *(in-1)!='\\') )
                        {
                            switch(*in)
                            {
                                case '\n':
                                    stringcopy("\\n");
                                    (*lineno)++;
                                    break;
                                case '\'':
                                    scopy('\\');
                                    scopy('\'');
                                    break;
                                case '\r':
                                    scopy('\\');
                                    scopy('r');
                                    break;
                                case '\t':
                                    scopy('\\');
                                    scopy('t');
                                    break;
                                default:
                                    scopy(*in);
                            }
                            adv;
                        }
                        if(*in != ':')
                        {
                            *ob=outbeg;
                            *o=out;
                            return 0;
                        }
                        adv;
                        stringcopy("',");
                    }
                    /* end special sprintf formatting */
                    else if(type == 2)
                        stringcopy(",(");
                    else if(type == 1)
                        stringcopy("\",(");
                    else
                    {
                        if(*(out-1) == '"' && *(out-2) != '\\')
                            *(out-1) = '('; //skip empty quotes: "",( => (
                        else
                            stringcopy("\"+(");
                    }
                    /* FIXME: properly this should go back to tickify somehow */
                    while (in < end && *in != '}')
                    {
                        if(*in == '`' && *(in-1)!='\\' )
                        {
                            int r=proc_backtick(in, end, &outbeg, &out, osize, lineno, 0);
                            if(!r)
                            {
                                *ob=outbeg;
                                *o=out;
                                return 0;
                            }
                            in+=r;
                        }
                        else
                        {
                            if(*in=='\n') (*lineno)++;
                            scopy(*in);
                            adv;
                        }
                    }
                    if(*in != '}')
                    {
                        *ob=outbeg;
                        *o=out;
                        return 0;
                    }
                    if(type==2)
                    {
                        scopy(')');
                        mute=1;
                    }
                    else
                    {
                        stringcopy(")+\"");
                        mute=0;
                    }
                }
                else
                    scopy(*in);
                break;
            case '\n':
                stringcopy("\\n\"+\n\"");
                break;
            case '\r':
                scopy('\\');
                scopy('r');
                break;
            case '\t':
                scopy('\\');
                scopy('t');
                break;
            case '`':
                if(lastwasbs)
                {
                    out--;
                    scopy('`');
                    lastwasbs=0;
                }
                else
                {
                    /* remove the '+ ""' */
                    if( *(out-1) == '"')
                    {
                        if (bt_start < out - 3 && *(out-2) == '\\' && *(out-3) == '"')
                            scopy('"');
                        else
                        {
                            int nls=0;
                            //back up
                            out-=2;
                            while(isspace(*out))
                            {
                                if(*out == '\n') nls++;
                                out--;
                            }
                            //cur char is '+' or ',' and will be overwritten
                            while(nls--)
                                scopy('\n');
                        }
                    }
                    else
                        scopy('"');
                    if(type == 1)
                    {
                        int r;
                        mute=0;
                        scopy(']');
                        /* second pass, get ${} and comma separate */
                        r=proc_backtick(bt_start, end, &outbeg, &out, osize, lineno, 2);
                        if(!r)
                        {
                            *ob=outbeg;
                            *o=out;
                            return 0;
                        }
                    }
                    scopy(')');
                    adv;
                    *ob=outbeg;
                    *o=out;
                    return (int) (in - bt_start);
                }
                break;
            case '"':
                scopy('\\');
                scopy('"');
                break;
            default:
                if(lastwasbs)
                    lastwasbs=0;
                scopy(*in);
        }
        adv;
    }

    *ob=outbeg;
    *o=out;
    return 0;
}

char * tickify(char *src, size_t sz, int *err, int *ln)
{
    size_t osz=sz+1024;
    char *out = NULL, *outbeg;
    char *in=src, *end=src+sz;
    int line=1, qline=0;
    int sstack_no=0;
    int sstack[8];// only need 3?
    int startexp=0;
    int infuncp=0;

    *err=0;
    *ln=0;
    sstack[0]=ST_NONE;

    REMALLOC(out, osz);
    outbeg=out;
    while(*in)
    {
        switch (*in)
        {
            case '/' :
                if(getstate()==ST_NONE)
                {
                    /* skip over comments */
                    if( in+1<end && *(in+1) == '/')
                    {
                        while(*in && *in != '\n')
                        {
                            copy(*in);
                            adv;
                        }
                    }
                    else if ( in+1<end && *(in+1) == '*')
                    {
                        copy(*in);
                        adv;
                        copy(*in);
                        adv;
                        while(in<end)
                        {
                            if(*in=='\n')
                                line++;
                            copy(*in);
                            adv;
                            if(!*in)
                                break;
                            if(*in == '*' && in+1<end && *(in+1)=='/')
                            {
                                copy(*in);
                                adv;
                                copy(*in);
                                adv;
                                break;
                            }
                            if( in >= end )
                                break;
                        }
                    }
                    else if (startexp)
                    {
                        regex:
                        /* regular expression */
                        copy(*in);
                        adv;
                        while (in<end)
                        {
                            copy(*in);
                            if(*in == '/' && ( *(in-1) !='\\' || *(in-2) =='\\' ) )
                                break;
                            if(*in == '\n') /* did we mess up? */
                                break;
                            //if(*in == '\n' || *in == ';')
                            //    break;
                            //if( (*in == ')' && *(in-1) !='\\') )
                            //    break;
                            adv;
                        }
                        adv;
                    }
                    else
                    {
                        char *s=in;

                        s--;
                        while(s>=src && isspace(*s))s--;
                        while(s>=src && isalpha(*s))s--;
                        s++;
                        if(
                            /* reserved words which don't require a (), {} or [] next
                               this prevents >>return /abc`def/;<< from being run through
                               proc_backtick above                                        */
                            !strncmp(s,"return",6) || !strncmp(s,"yield",5)     ||
                            !strncmp(s,"break",5)  || !strncmp(s,"continue",8)  ||
                            !strncmp(s,"case",4)   || !strncmp(s,"else",4)      ||
                            !strncmp(s,"typeof",6) || !strncmp(s,"delete",6)    ||
                            !strncmp(s,"new",3)    || !strncmp(s,"var",3)
                        )
                            goto regex;
                        /* division */
                        copy(*in);
                        adv;
                    }
                    break;
                }
                else
                {
                    if (getstate() == ST_BS)
                        popstate();
                    copy(*in);
                    adv;
                }
                break;
            case '\n':
                if (getstate() == ST_BS)
                    popstate();
                copy(*in);
                line++;
                adv;
                /* This succeeds for some (like return and `` on different lines), but not if
                   function name and tag template are in different lines
                   BTW: ASI..., STBY
                if (!startexp)
                    startexp=1;
                */
                break;
            case '\\':
                copy(*in);
                adv;
                if (getstate() == ST_BS)
                    popstate();
                else
                    pushstate(ST_BS);
                qline=line;
                break;
            case '"':
                if(getstate() == ST_NONE)
                {
                    qline=line;
                    pushstate(ST_DQ);
                }
                else if (getstate() == ST_DQ)
                    popstate();
                else if (getstate() == ST_BS)
                    popstate();

                copy(*in);
                adv;
                break;
            case '\'':
                if(getstate() == ST_NONE)
                {
                    qline=line;
                    pushstate(ST_SQ);
                }
                else if (getstate() == ST_SQ)
                    popstate();
                else if (getstate() == ST_BS)
                    popstate();

                copy(*in);
                adv;
                break;
            case '`':
                if (getstate() == ST_NONE)
                {
                    int r;
                    char *s=in;

                    qline=line;
                    s--;
                    while(s>=src && isspace(*s))s--;
                    while(s>=src && isalpha(*s))s--;
                    s++;
                    if(
                        // reserved words which don't require a (), {} or [] next
                        // not all make sense in terms of being followed by a `literal`
                        // but covering the cases anyway.
                        !strncmp(s,"return",6) || !strncmp(s,"yield",5)     ||
                        !strncmp(s,"break",5)  || !strncmp(s,"continue",8)  ||
                        !strncmp(s,"case",4)   || !strncmp(s,"else",4)      ||
                        !strncmp(s,"typeof",6) || !strncmp(s,"delete",6)    ||
                        !strncmp(s,"new",3)    || !strncmp(s,"var",3)
                    )
                    {
                        r=proc_backtick(in, end, &outbeg, &out, &osz, &line, startexp);
                    }
                    else
                        r=proc_backtick(in, end, &outbeg, &out, &osz, &line, !startexp);
                    if(!r)
                    {
                        *err=ST_BT;
                        *ln=qline;
                        free(outbeg);
                        return NULL;
                    }
                    in+=r;
                }
                else if (getstate() == ST_BS)
                {
                    popstate();
                    copy(*in);
                    adv;
                }
                else
                {
                    copy(*in);
                    adv;
                }
                break;
            default:
#define islegitchar(x) ( ((unsigned char)(x)) > 0x79 || (x) == '$' || (x) == '_' || isalnum((x)) )
                if (getstate() == ST_NONE)
                {
                    /*  looking for ...arg in "function(x, ...arg) {"
                        to rewrite as "function(x){var arg=Object.values(arguments).slice(x);"
                        where x is the number of preceding arguments.
                    */
                    if( infuncp )
                    {
                        if(*in == ',' || infuncp==1)
                        {
                            char *s = in;

                            if(*s==',')
                                s++;
                            infuncp++;
                            while (isspace(*s))
                                s++;
                            if( *s=='.' && *(s+1)=='.' && *(s+2)=='.' )
                            {
                                char *varname;
                                s+=3;
                                varname=s;
                                while (islegitchar(*s)) s++;

                                if( !isspace(*s) && *s!=')' )
                                {
                                    if(*s==',')
                                        *err=ST_PM;
                                    else
                                        *err=ST_PN;
                                    *ln=line;
                                    free(outbeg);
                                    return NULL;
                                }
                                while (isspace(*s))s++;
                                if(*s != ')')
                                {
                                    *err=ST_PM;
                                    *ln=line;
                                    free(outbeg);
                                    return NULL;
                                }
                                else
                                {
                                    /* check for '{', if not bail and let duktape report error */
                                    s++;
                                    while(isspace(*s))
                                    {
                                        s++;
                                    }
                                    if(*s=='{')
                                    {
                                        //good to go, write altered function
                                        //char *varname,
                                        char nbuf[16];
                                        if(*in==',')
                                            in++; // skip ','
                                        while(isspace(*in))
                                        {
                                            copy(*in);
                                            if(*in=='\n') line++;
                                            adv;
                                        }
                                        //varname=in+3; //advance past the "..."
                                        while( *in != ')')
                                        {
                                            /* only copy white space here */
                                            if(isspace(*in))
                                            {
                                                copy(*in);
                                                if(*in=='\n') line++;
                                            }
                                            adv;
                                        }
                                        copy(*in);// ')'
                                        adv;
                                        while( *in != '{')
                                        {
                                            copy(*in);
                                            if(*in=='\n') line++;
                                            adv;
                                        }
                                        copy(*in);// '{'
                                        adv;
                                        stringcopy2("var ");
                                        while(*varname!=')' && !isspace(*varname) )
                                        {
                                            copy(*varname);
                                            varname++;
                                        }
                                        stringcopy2("=Object.values(arguments).slice(");
                                        snprintf(nbuf,16,"%d",infuncp-2);
                                        stringcopy2(nbuf);
                                        stringcopy2(");");
                                    }
                                }
                                infuncp=0;
                            }
                        }
                        else if (*in ==')')
                            infuncp=0;
                    }

                    if (!startexp && *in == '(')
                    {
                        char *s = in -1;
                        while (s>src && isspace(*s))s--;
                        while (s>src && islegitchar(*s) )s--;
                        s++;
                        /* anonymous function */
                        if (!strncmp(s,"function",8))
                        {
                            infuncp=1;
                        }
                        else
                        {
                            s--;
                            if( isspace(*s) )
                            {
                                while (s>src && isspace(*s))s--;
                                while ( s>src && islegitchar(*s) )s--;
                                s++;
                                if (!strncmp(s,"function",8))
                                {
                                    infuncp=1;
                                }
                            }
                        }
                    }
                    /* end function(...var) processing */

                    /* for the "/regexp/" vs "var x = 2/3" cases, tag function and (...rest) processing,
                       we need to know where we are. This is a horrible hack, but it seems to work
                       Failings might be Automatic Semicolon Insertion at '\n'. See case '\n' above.
                    */
                    if (strchr("{([=;+-/*:,%^&|?", *in))
                        startexp=1;
                    else if (isalnum(*in) || *in =='}' || *in == ')' || *in == ']')
                        startexp=0;
                    /*
                    else if(
                        (*in == '&' && in+1<end && *(in+1)=='&')
                            ||
                        (*in == '|' && in+1<end && *(in+1)=='|')
                    )
                        startexp=1;
                    */
                }
                copy(*in);
                if (getstate() == ST_BS)
                    popstate();
                adv;
        }
    }
    char *db = getenv("RPDEBUG");

    if( db && !strcmp (db, "preparser") )
        fprintf(stderr, "BEGIN SCRIPT\n%s\nEND SCRIPT\n",outbeg);
    *err=getstate();
    if(*err)
    {
        *ln=qline;
        free(outbeg);
        return NULL;
    }

    copy('\0');
    return outbeg;
}
/* end tickify */

void duk_rp_fatal(void *udata, const char *msg){
    fprintf(stderr, "*** FATAL ERROR: %s\n", (msg ? msg : "no message"));
    fflush(stderr);
    abort();
}

static void sigint_handler(int sig) {
    duk_rp_exit(main_ctx, 0);
}

static void evhandler(int sig, short events, void *base)
{
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
}

char **rampart_argv;
int   rampart_argc;
char argv0[PATH_MAX];
char rampart_exec[PATH_MAX];
char rampart_dir[PATH_MAX];
char rampart_bin[PATH_MAX];
int base_loop_exited=0;

/* mutex for locking in rampart.thread */
pthread_mutex_t thr_lock;
RPTHR_LOCK *rp_thr_lock;

void check_version_help(int argc, char *argv[])
{
    if(argc==2)
    {

        if(!strcmp("-v", argv[1]) || !strcmp("--version", argv[1]))
        {
            printf("v%d.%d.%d\n", RAMPART_VERSION_MAJOR, RAMPART_VERSION_MINOR, RAMPART_VERSION_PATCH);
            exit(0);
        }

        if(!strcmp("-h", argv[1]) || !strcmp("--help", argv[1]))
        {
            printf("Usage:\n\
    %s file_name [args]       -- load a script from 'file_name' and run\n\
    %s [-g]                   -- interactive mode (-g globalizes rampart.utils)\n\
    %s -c \"script\"            -- load script from argument string\n\
    %s [-v|--version]         -- print version\n\
    %s --server               -- run rampart-server with default configuration\n\
    %s --quickserver          -- run rampart-server with alternate configuration\n\
    %s --[quick]server --help -- show help for built-in server\n\
    %s [-h|--help]            -- this help message\n\
\n\
    Documentation can be found at https://rampart.dev/docs/\n",
                argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
            exit(0);
        }
    }
}

int main(int argc, char *argv[])
{
    struct rlimit rlp;
    int isstdin=0, len, dirlen;
    char *ptr, *cmdline_src=NULL;
    struct stat entry_file_stat;

    check_version_help(argc, argv);

    /* do this first */
    rp_thread_preinit();
    evthread_use_pthreads();

    SLIST_INIT(&tohead); //timeout list

    // must be init before new thread created below
    rp_thr_lock = RP_MINIT(&thr_lock);

    RP_PTINIT(&exlock);



    /* for later use */
    rampart_argv=argv;
    rampart_argc=argc;
    access_fh=stdout;
    error_fh=stderr;

    strcpy(argv0, argv[0]);

    len = wai_getExecutablePath(NULL, 0, NULL);
    wai_getExecutablePath(rampart_exec, len, &dirlen);
    rampart_exec[len]='\0';

    strcpy(rampart_dir, rampart_exec);
    ptr=strrchr(rampart_dir, '/');
    if(!ptr)
    {
        fprintf(stderr,"could not get subpath of '%s'\n", rampart_dir);
        exit(1);
    }
    if( ptr-rampart_dir > 4 &&
        *(ptr-1)=='n' &&
        *(ptr-2)=='i' &&
        *(ptr-3)=='b' &&
        *(ptr-4)=='/'
      )
        ptr-=4;
    *ptr='\0';

    strcpy(rampart_bin, rampart_exec);
    ptr=strrchr(rampart_bin, '/');
    if(!ptr)
    {
        fprintf(stderr,"could not get subpath of '%s'\n", rampart_dir);
        exit(1);
    }
    *ptr='\0';

    /* timeout cleanups */
    add_exit_func(free_tos, NULL);

    /* initialze some locks */
    rp_ctxlock=RP_MINIT(&ctxlock);
    rp_slistlock = RP_MINIT(&slistlock);

    /* get script path */
    if(rampart_argc>1)
    {
        char p[PATH_MAX], *s;
        int n=1;

        //script is either first arg or last
        if((stat(argv[1], &entry_file_stat)))
        {
            n=argc-1;
        }

        strcpy(p, rampart_argv[n]);

        //a copy of the complete path/script.js
        RP_script=realpath(p, NULL);

        s=strrchr(p,'/');
        if (s)
        {
            char *dupp;

            *s='\0';
            dupp=strdup(p);
            s=realpath(dupp,p);
            free (dupp);
        }
        else
        {
            if( !(s=getcwd(p,PATH_MAX)) )
            {
                fprintf(stderr,"path to script is longer than allowed\n");
                exit(1);
            }
        }
        if (!s)
            s="";

        RP_script_path=strdup(s);
    }
    else
        RP_script_path=strdup("");


    /* set max files open limit to hard limit */
    getrlimit(RLIMIT_NOFILE, &rlp);
    rlp.rlim_cur = rlp.rlim_max;
    setrlimit(RLIMIT_NOFILE, &rlp);

    mainthr=rp_new_thread(RPTHR_FLAG_THR_SAFE, NULL);
    if (!mainthr)
    {
        fprintf(stderr,"could not create duktape context\n");
        return 1;
    }
    duk_context *ctx = mainthr->ctx;
    main_ctx = ctx; //global var
    mainthr->self=pthread_self(); //not really necessary, but better set than uninitialized garbage

    /* for cleanup, an array of functions */
    duk_push_global_stash(ctx);
    duk_push_array(ctx);
    duk_put_prop_string(ctx, -2, "exitfuncs");
    duk_pop(ctx);

    /* some control over our exit */
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    /* init setproctitle() as required */
#ifdef RP_SPT_NEEDS_INIT
    spt_init(argc, argv);
#endif

    /* skip past process name */
    argc--;
    argv++;

    /* first check if we have -c "script_src" */
    if(argc>1 && strcmp(argv[0],"-c")==0)
    {
        cmdline_src=argv[1];
        argc-=2;
        argv+=2;
    }
    /* second check if we are using the server shortcut */
    else if(argc>0 && (strcmp(argv[0],"--server")==0 || strcmp(argv[0],"--quickserver")==0) )
    {
        cmdline_src=serverscript;
    }

    /* check if filename is first, for #! script */
    if(argc>0 && (stat(argv[0], &entry_file_stat)))
    {
        /* skip to filename, if any, which should be last */
        while( argc > 1)
        {
            argc--;
            argv++;
        }
    }
    {
        char *file_src=NULL, *free_file_src=NULL, *fn=NULL, *s;
        const char *babel_source_filename;
        FILE *entry_file;
        size_t src_sz=0;
        struct evdns_base *dnsbase=NULL;


        /* setTimeout and related functions */
        duk_push_c_function(ctx,duk_rp_set_timeout, DUK_VARARGS);
        duk_put_global_string(ctx,"setTimeout");
        duk_push_c_function(ctx, duk_rp_clear_either, 1);
        duk_put_global_string(ctx,"clearTimeout");
        duk_push_c_function(ctx,duk_rp_set_interval, DUK_VARARGS);
        duk_put_global_string(ctx,"setInterval");
        duk_push_c_function(ctx, duk_rp_clear_either, 1);
        duk_put_global_string(ctx,"clearInterval");
        duk_push_c_function(ctx,duk_rp_set_metronome, DUK_VARARGS);
        duk_put_global_string(ctx,"setMetronome");
        duk_push_c_function(ctx, duk_rp_clear_either, 1);
        duk_put_global_string(ctx,"clearMetronome");

        /* set up object to hold timeout callback function */
        duk_push_global_stash(ctx);
        duk_push_object(ctx);//new object
        duk_put_prop_string(ctx, -2, "ev_callback_object");
        duk_pop(ctx);//global stash

        if(cmdline_src)
        {
            file_src=strdup(cmdline_src);
            fn="command_line_script";
            goto have_src;
        }

        /* REPL */
        if (argc == 0 || (argc == 1 && !strcmp(argv[0],"-g")) )
        {
            if(!isatty(fileno(stdin)))
            {
                isstdin=1;
                goto dofile;
            }

            if(argc == 1 && !strcmp(argv[0],"-g") )
            {
                duk_get_global_string(ctx, "rampart");
                duk_get_prop_string(ctx, -1, "globalize");
                duk_get_prop_string(ctx, -2, "utils");
                duk_call(ctx, 1);
                duk_pop_2(ctx); //return and rampart
            }

            /* ********** set up event loop for repl *************** */


            /* set up main event base */
            mainthr->base = event_base_new();
            RPTHR_SET(mainthr, RPTHR_FLAG_BASE);

            dnsbase = evdns_base_new(mainthr->base,
                EVDNS_BASE_DISABLE_WHEN_INACTIVE);
            if(!dnsbase)
                RP_THROW(ctx, "rampart - error creating dnsbase");

            /* for unknown reasons, setting EVDNS_BASE_INITIALIZE_NAMESERVERS
               above results in dnsbase not exiting when event loop is otherwise empty */
            evdns_base_resolv_conf_parse(dnsbase, DNS_OPTIONS_ALL, "/etc/resolv.conf");

            duk_push_global_stash(ctx);
            duk_push_pointer(ctx, dnsbase);
            duk_put_prop_string(ctx, -2, "dns_elbase");
            duk_pop(ctx);
            REMALLOC(thread_dnsbase, (nthread_dnsbase + 1) * sizeof(struct evdns_base *) );
            thread_dnsbase[nthread_dnsbase++]=dnsbase;
            /* end dns */

            /* run things that need to be run before the loop starts */
            run_b4loop_funcs();

            int ret = repl(ctx);
            return ret;
        }
        else
        {
            dofile:

            if (!strcmp("-",argv[0]))
                isstdin=1;
            if(isstdin)
            {
                size_t read=0;

                fn="stdin";
                /*
                REMALLOC(file_src,1024);
                while( (read=fread(file_src+src_sz, 1, 1024, stdin)) > 0 )
                {
                    src_sz+=read;
                    if(read<1024)
                        break;
                    REMALLOC(file_src,src_sz+1024);
                }
                */
                read=getdelim(&file_src, &read, '\0', stdin);
            }
            else
            {
                if (stat(argv[0], &entry_file_stat))
                {
                    duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not find entry file '%s': %s", argv[0], strerror(errno));
                    fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                    duk_rp_exit(ctx, 1);
                }
                entry_file = fopen(argv[0], "r");
                if (entry_file == NULL)
                {
                    duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not open entry file '%s': %s", argv[0], strerror(errno));
                    fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                    duk_rp_exit(ctx, 1);
                }
                src_sz=entry_file_stat.st_size + 1;
                file_src = malloc(src_sz);
                if(!file_src)
                {
                    fprintf(stderr,"Error allocating memory for source file\n");
                    duk_rp_exit(ctx, 1);
                }

                if (fread(file_src, 1, entry_file_stat.st_size, entry_file) != entry_file_stat.st_size)
                {
                    duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not read entry file '%s': %s", argv[0], strerror(errno));
                    fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                    free(free_file_src);
                    duk_rp_exit(ctx, 1);
                }
                fn=argv[0];
                fclose(entry_file);
            }
            have_src:
            if(!cmdline_src)
                file_src[src_sz-1]='\0';
            else
                src_sz=strlen(cmdline_src);
            free_file_src=file_src;

            /* skip over #!/path/to/rampart */
            if(*file_src=='#')
            {
                s=strchr(file_src,'\n');

                /* leave '\n' to preserve line numbering */
                if(!s)
                {
                    duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not read beyond first line in entry file '%s'\n", fn);
                    fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                    free(free_file_src);
                    duk_rp_exit(ctx, 1);
                }
                file_src=s;
            }

            /* ********** set up event loop *************** */

            /* set up main event base */
            mainthr->base = event_base_new();
            RPTHR_SET(mainthr, RPTHR_FLAG_BASE);

            /* push babelized source to stack if available */
            if (! (babel_source_filename=duk_rp_babelize(ctx, fn, file_src, entry_file_stat.st_mtime, 0, NULL)) )
            {
                /* No babel, normal compile */
                int err, lineno;
                /* process basic template literals (backticks) */
                char *tickified = tickify(file_src, src_sz, &err, &lineno);
                free(free_file_src);
                file_src = free_file_src = tickified;
                if (err)
                {
                    char *msg="";
                    switch (err) {
                        case ST_BT:
                            msg="unterminated or illegal template literal"; break;
                        case ST_SQ:
                            msg="unterminated string"; break;
                        case ST_DQ:
                            msg="unterminated string"; break;
                        case ST_BS:
                            msg="invalid escape"; break;
                        case ST_PM:
                            msg="Rest parameter must be last formal parameter";break;
                        case ST_PN:
                            msg="Illegal parameter name";break;
                    }
                    fprintf(stderr, "SyntaxError: %s (line %d)\n", msg, lineno);
                    /* file_src is NULL*/
                    return(1);
                }

                duk_push_string(ctx, file_src);
                /* push filename */
                duk_push_string(ctx, fn);
            }
            else
            {
                /* babel src on stack, push babel filename */
                duk_push_string(ctx, babel_source_filename);
                free((char*)babel_source_filename);
            }

            free(free_file_src);

            /*  add dns base for rampart-net
             *  if added before event loop starts, it won't block exit
             */
            dnsbase = evdns_base_new(mainthr->base,
                EVDNS_BASE_DISABLE_WHEN_INACTIVE);
            if(!dnsbase)
                RP_THROW(ctx, "rampart - error creating dnsbase");

            /* for unknown reasons, setting EVDNS_BASE_INITIALIZE_NAMESERVERS
               above results in dnsbase not exiting when event loop is otherwise empty */
            evdns_base_resolv_conf_parse(dnsbase, DNS_OPTIONS_ALL, "/etc/resolv.conf");

            duk_push_global_stash(ctx);
            duk_push_pointer(ctx, dnsbase);
            duk_put_prop_string(ctx, -2, "dns_elbase");
            duk_pop(ctx);
            REMALLOC(thread_dnsbase, (nthread_dnsbase + 1) * sizeof(struct evdns_base *) );
            thread_dnsbase[nthread_dnsbase++]=dnsbase;
            /* end dns */

            /* run the script */
            if (duk_pcompile(ctx, 0) == DUK_EXEC_ERROR)
            {
                fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                duk_rp_exit(ctx, 1);
            }

            if (duk_pcall(ctx, 0) == DUK_EXEC_ERROR)
            {
                fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                duk_rp_exit(ctx, 1);
            }
            if (!mainthr->base)
            {
                fprintf(stderr,"Eventloop error: could not initialize event base\n");
                duk_rp_exit(ctx, 1);
            }

            /* sigint for libevent2: insert a one time event to register a sigint handler *
             * libeven2 otherwise erases our SIGINT and SIGTERM event handler set above   */
            struct event ev_sig;
            event_assign(&ev_sig, mainthr->base, -1,  0, evhandler, NULL);
            struct timeval to={0};
            event_add(&ev_sig, &to);

            /* run things that need to be run before the loop starts */
            run_b4loop_funcs();

            /* start event loop */
            int sent_finalizers=0;
            do {
                sent_finalizers=0;
                //printf("ENTER main loop\n");
                event_base_loop(mainthr->base, 0);
                // at this point, if children have no active events, they aint gonna get any
                // as main thread is done with all it's events
                sent_finalizers = rp_thread_close_children();
                //printf("END OF LOOP %d children\n", mainthr->nchildren);
                //printf("EXIT main loop with %d children and %s finalizers set\n", mainthr->nchildren, (sent_finalizers?"some":"no"));
                usleep(50000);
                // we restart the main loop in case children insert an event before they are done
            } while (mainthr->nchildren || sent_finalizers);
            //printf("END MAIN LOOP FOR GOOD\n");
        }
    }

    duk_rp_exit(ctx, 0);

    return 0;
}
