#!./bin/rampart

rampart.globalize(rampart.utils);

var isWindows = /MSYS_NT/.test(rampart.buildPlatform);
var iam = trim(exec('whoami').stdout);

var singledir_install = {
    bin:          "/bin",
    include:      "/include",
//    examples:     "/examples",
//    "web_server": "/web_server",
    modules:      "/modules",
}

var local_install = {
    bin:          "/bin",                 // /usr/local/bin
    include:      "/include",             // /usr/local/include
//    examples:     "/rampart/examples",    // /usr/local/rampart/examples
//    "web_server": "/rampart/web_server",   // /usr/local/rampart/web_server
    modules:      "/lib/rampart_modules" // /usr/local/lib/rampart_modules
}
var resp;
function getresp(def, len) {
    var l = (len)? len: 1;
    var ret = stdin.getchar(l);
    if(ret == '\n')
        return def;
    printf("\n");
    return ret.toLowerCase();
}

function clear(){
    printf("\x1b[1;1H\x1b[2J")
    //printf("\x1b[2J");
}

function get_install_choice(home) {
    var homechoice="";
    if(home)
        homechoice = `7) ${home}/rampart\n`;

    printf(
`Install rampart to:

1) /usr/local/rampart
2) /opt/rampart
3) /usr/local
4) /usr (not recommened)
5) custom
6) links only from this dir
${homechoice}`);
    return getresp('X');
}

function greetings(){
    printf(
`Welcome to Rampart Install

Rampart and its modules are licensed under the MIT license.

Rampart modules use many different libraries which all use various
permissive open source licenses.

The one exception is rampart-sql and the texis library used in rampart-sql,
which uses a source available license (see the 'LICENSE' file in the install
directory) and is available without charge for non-profit, personal,
education and certain other uses.  See license for details.

The respective licenses are listed at the beginning of each major section in
the documentation at 'https://rampart.dev/docs/'.

Please type "Yes" to confirm you have read and agree to the licensing terms listed
above.
`);

    resp = trim (readLine(stdin).next());

    if(resp.toLowerCase()!="yes")
        process.exit();
}

function has_write_perm(dir, noPrintErr) {
    var ret=true;
    try {
        touch(dir + '/.testperm');
        rmFile(dir + '/.testperm');
    } catch (e) {
        if(!noPrintErr) {
            printf("You do not have write permissions in '%s'\n", dir);
            printf("Press any key to continue\n");
            getresp("");
        }
        ret=false;
    }
    return ret;
}

function prevdir(dir) {
    var lastslash = dir.lastIndexOf('/');
    var subdir=".";
    if(lastslash>-1)
         subdir = dir.substring(0, lastslash);
    return subdir;
}

function check_create_perm(dir, noPrintErr){
    // does the directory exist?
    var ret=false;
    var subdir = dir;

    if(dir.charAt(dir.length-1) == '/')
        dir = dir.substring(0, dir.length-1);
    
    while (!stat(subdir))
        subdir = prevdir(subdir);

    if(!has_write_perm(subdir, noPrintErr))
        return false;

    return subdir;
}

function check_make_dir(dir) {
    var subdir = check_create_perm(dir);
    
    if(!subdir)
        return false;

    if(subdir != dir) {
        try { mkdir(dir) }
        catch(e){
            printf("Unexpected error: Cannot create directory '%s':\n%s\n",dir,e);
            exit(1);
        }
    }
    return true;
}


function filehash(file) {
    return hash(readFile(file));
}

var madeNew=[];

function copy_single(src, dest, backup) {
    var exists = false;

    try {
        copyFile(src, dest, !backup);
    } catch(e) {
        var msg = sprintf("%s",e);
        if( msg.indexOf("file already exists")>-1 ) {
            exists=true;
        } else {
            if(!backup) {
                //will get 'text file busy' error if replacing the rampart that is currently in use.
                rmFile(dest);
                copyFile(src, dest, true);
            } else {
                console.log(e);
                process.exit();
            }
        }
    }

    if(exists) {
        var shash = filehash(src);
        var dhash = filehash(dest);
        if(shash != dhash)
        {
            copyFile(src, dest+'.new', true);
            madeNew.push(dest);
        }    
    }
}



function copy_files(src, dest, backup){
    var srcfiles = exec('find', src, "-type",'f').stdout.split('\n');
    for (var i=0;i<srcfiles.length; i++) {
        var sfile = srcfiles[i];
        if(sfile == "")
            continue;
        var dfile = dest + srcfiles[i].substring(src.length);
        var lastslash = dfile.lastIndexOf('/');
        var ddir = dfile.substring(0, lastslash);
        if(!stat(ddir))
            mkdir(ddir);

        copy_single(sfile, dfile, backup);
        
    }
}

// JS-native recursive copy for Windows where 'cp' may not be in PATH
function js_copy_entry(src, dest_dir) {
    var basename = src.substring(src.lastIndexOf('/') + 1);
    var dest_path = dest_dir + '/' + basename;
    var srcstat = stat(src);  // follows symlinks

    if(!srcstat) return;  // broken symlink or doesn't exist

    if(srcstat.isDirectory) {
        if(!stat(dest_path))
            mkdir(dest_path);
        var entries = readDir(src, true).filter(function(d){ return d != '.' && d != '..'; });
        for(var i = 0; i < entries.length; i++) {
            js_copy_entry(src + '/' + entries[i], dest_path);
        }
    } else {
        try {
            copyFile(src, dest_path, true);
        } catch(e) {
            // Handle 'text file busy' or existing file
            try { rmFile(dest_path); } catch(e2) {}
            copyFile(src, dest_path, true);
        }
    }
}

function copy_dir_recursive(src, dest, backup) {
    if(backup && stat(dest))
        rename(dest, dest + rampart.utils.dateFmt("-%Y-%m-%d-%H-%M-%S"));

    if(!stat(dest))
        mkdir(dest);

    var cp_files = readDir(src, true).filter(function(d){if(d=='.'||d=='..') return false; return true;});

    for(var i=0; i<cp_files.length; i++) {
        var cpsrc = src + '/' + cp_files[i];
        var dest_file = dest + '/' + cp_files[i];
        var cpstat = lstat(dest_file);
        // if we installed before and left links, those need to be removed
        // mostly for /usr/local/bin links if reinstalling in /usr/local
        if(cpstat.isSymbolicLink)
            rmFile(dest_file);
        if(isWindows) {
            try {
                js_copy_entry(cpsrc, dest);
            } catch(e) {
                printf("Error copying files:\n    %s\n", e);
                process.exit(1);
            }
        } else {
            var res = exec('cp', '-a', cpsrc, dest);  //dest is a directory
            if(res.stderr.length)
            {
                printf("Error copying files:\n    %s\n", res.stderr);
                process.exit(1);
            }
        }
    }
}

var with_err="";

var link_bin_files = ['rampart', 'tsql', 'rex', 'texislockd', 'addtable', 'kdbfchk', "pip3r", "python3r" ];

function do_make_links(prefix,target_bindir) {
    var bindir = prefix + '/bin/';
    var i=0;

    if(!target_bindir)
        target_bindir="/usr/local/bin/";


    for(;i<link_bin_files.length;i++){
        var lfile = bindir + link_bin_files[i]; 
        var link  = target_bindir + link_bin_files[i];

        var linkstat = lstat(link);
        if(linkstat)
        {
            //link already exists
            if(linkstat.isSymbolicLink || linkstat.isFile) {
                rmFile(link);                
            } else {
                printf("Cannot create link '%s', file exists and is not a link or a file\n", link);
                with_err += sprintf("\n    - Link creation failed for '%s'.",link);
                continue;
            }
        } 
        symlink(lfile,link);
    }
}

/* we will overwrite files everywhere except in
   'examples' and 'web_server'.  If files differ
   there, the new file will be filename +'.new'   */

/* UPDATE: we are no longer copying the examples and web_server dirs. */

function do_install(prefix, map, makelinks){
    var dirs = Object.keys(map);
    var i=0;

    if(!check_make_dir(prefix))
        return false;

    printf("Installing to '%s', please wait...\n", prefix);
    stdout.fflush();

    for (i=0;i<dirs.length;i++) {
        var dir=dirs[i];
        var src = realPath(dir);
        var dest = realPath(prefix) + map[dir];
        var backup = (dir == "web_server" || dir == 'examples' ) ? true:false;

        if(src == dest)
            continue;
        copy_dir_recursive(src, dest, backup);
        if( dir == 'web_server' ) {
            try {
                shell("chown -R nobody " + dest);
            } catch(e){
                with_err += sprintf("\n    - could not 'chown nobody' %s", dest);
            }
        }
    }

    printf("Done copying files.\n");

    if(makelinks)
    {
        var bindir = "/usr/local/bin/";
        if(getType(makelinks) == "String")
            bindir = makelinks;

        do{
            if(!stat(bindir))
            {
                if(!check_create_perm(bindir))
                    return false;
                printf("'%s' does not exist. Create it? (y/n):\n", bindir);
                resp = getresp('');
                if (resp != 'y')
                {
                    printf("No links to binaries will be made\n");
                    break;
                }
                else if (!check_make_dir(bindir))
                {
                    printf("No links to binaries will be made\n");
                    break;
                }
            }

            if(!has_write_perm(bindir))
                printf("No links to binaries will be made\n");
            else
                do_make_links(prefix,bindir);

        } while (false);
    }    

    // rebase python paths
    var rebase_script = realPath(prefix) + map.modules +'/python/rebase-python.sh';
    if(stat(rebase_script)) {
        if(isWindows) {
            // On Windows/relocated Cygwin, bash may not be in PATH
            try {
                var cmdres = exec('bash', '-c', "'" + rebase_script + "'");
                if(cmdres.stderr.length)
                    printf("Warning: error rebasing python paths:\n    %s\n", cmdres.stderr);
            } catch(e) {
                printf("Note: could not run rebase-python.sh (bash not available).\n");
                printf("Python module paths may need manual adjustment.\n");
            }
        } else {
            var cmdres = exec('bash', '-c', "'" + rebase_script + "'");
            if(cmdres.stderr.length)
            {
                printf("error executing %s\n:   %s\n", rebase_script, cmdres.stderr);
                process.exit(1);
            }
        }
    }

    // make a more movable version of pip3r and python3r
    var pip3r_fn = realPath(prefix) + map.bin + '/pip3r';
    var python3r_fn = realPath(prefix) + map.bin + '/python3r';

    rmFile(pip3r_fn);
    rmFile(python3r_fn);

    fprintf(pip3r_fn, '%s', `#!/bin/bash

RAMPART=\$(which rampart)

if [ "\$RAMPART" == "" ]; then
        CURSRC=\$(readlink -f \${BASH_SOURCE[0]})
        CURDIR=\$(dirname \${CURSRC})
        RP="\${CURDIR}/rampart"
        if [ -e \$RP ]; then
                RAMPART="\$RP"
        else
                echo "could not find the rampart executable"
                exit 1;
        fi
fi

RP_DIR=\$(\$RAMPART -c "rampart.utils.printf('%s/python/bin/', process.modulesPath)");

python3_exec="\${RP_DIR}/python3"

pip3_exec="\${RP_DIR}/pip3"

\$python3_exec \$pip3_exec "\$@"
`   );

    fprintf(python3r_fn, "%s", `#!/bin/bash
RAMPART=\$(which rampart)

if [ "\$RAMPART" == "" ]; then
        CURSRC=\$(readlink -f \${BASH_SOURCE[0]})
        CURDIR=\$(dirname \${CURSRC})
        RP="\${CURDIR}/rampart"
        if [ -e \$RP ]; then
                RAMPART="\$RP"
        else
                echo "could not find the rampart executable"
                exit 1;
        fi
fi

python3_exec=\$(\$RAMPART -c "rampart.utils.printf('%s/python/bin/python3', process.modulesPath)");

\$python3_exec "\$@"
`   );

    chmod(pip3r_fn, "755");
    chmod(python3r_fn, "755");

    return true;
}

function do_install_choice(choice) {
    switch(choice) {
        case '1':
            return do_install('/usr/local/rampart',singledir_install, true);
            break;
        case '2':
            return do_install('/opt/rampart',singledir_install,true);
            break;
        case '3':
            return do_install('/usr/local',local_install, false);
            break;
        case '4':
            return do_install('/usr',local_install, false);
            break;
        case '5':
            var loc;
            while (!loc) {
                printf("Enter location:\n");
                loc = trim (readLine(stdin).next());
                if(loc == ''){
                    clear();
                    return false;
                } else if (loc.lastIndexOf('/') == loc.length-1) {
                    loc = loc.substring(0, loc.length-1);
                } 
                printf("Install into '%s'? (y/n/b[ack])\n",loc)
                resp = getresp('');
                if(resp == '' || resp=='b'){
                    clear();
                    return false;
                }
                if(resp != 'y')
                    loc=undefined;
            }
            if(!stat(loc))
            {
                if(!check_create_perm(loc))
                    return false;
                printf("'%s' does not exist. Create it? (y/n):\n", loc);
                resp = getresp('');
                if (resp != 'y')
                    return false;
                if (!check_make_dir(loc))
                    return false;
            } else {
                if(!check_create_perm(loc)) {
                    return false;
                }
            }
            printf("create links to binaries in '/usr/local/bin'? [Y/n]\n");
            resp=getresp("y");            
            if (resp == 'y')
                do_install(loc, singledir_install, true);
            else
                do_install(loc, singledir_install, false);

            break;

        case '6':
            var src = realPath('./');
            var dest = "/usr/local/bin/";
            var homebin;
            if(process.env.HOME)
            {
                homebin = process.env.HOME + '/bin/';
                resp="X"
                while (resp != '1' && resp != '2')
                {
                    printf(`create links to binaries in:
    1) '/usr/local/bin'
    2) '${homebin}'
[1/2]
`                   );
                    resp=getresp("X");
                }
                if(resp == 2)
                    dest = homebin;
                if(!stat(dest))
                {
                    if(!check_create_perm(dest))
                        return false;
                    printf("'%s' does not exist. Create it? (y/n):\n", dest);
                    resp = getresp('');
                    if (resp != 'y')
                    {
                        printf("No links to binaries will be made\n");
                        break;
                    }
                    else if (!check_make_dir(dest))
                    {
                        printf("No links to binaries will be made\n");
                        break;
                    }
                }
                else if(!check_create_perm(dest))
                    return false;
            }
            console.log(`creating links from "${src}/bin/*" to ${dest}`);
            do_make_links(src,dest);
            break;
        case '7':
            var loc = process.env.HOME + "/rampart/"
            if(!stat(loc))
            {
                if(!check_create_perm(loc))
                    return false;
                printf("'%s' does not exist. Create it? (y/n):\n", loc);
                resp = getresp('');
                if (resp != 'y')
                    return false;
                if (!check_make_dir(loc))
                    return false;
            } else {
                if(!check_create_perm(loc)) {
                    return false;
                }
            }
            printf(`create links to binaries in '${process.env.HOME}/bin'? [Y/n]\n`);
            resp=getresp("y");            
            if (resp == 'y')
                do_install(loc, singledir_install, process.env.HOME+'/bin/');
            else
                do_install(loc, singledir_install, false);

            break;

    }
    return true;
}

function choose_install() {
    var i=0, choice='X';// = get_install_choice();

    var home = process.env.HOME;
    var homeopt = "", homebinopt="";
    // we already checked /usr/local/bin at the beginning, below
    var optWriteWarn = (iam=='root' || has_write_perm("/opt", true)) ? "":"*";
    var usrWriteWarn = (iam=='root' || has_write_perm("/usr", true)) ? "":"*";

    // /usr/local/rampart might exist and we don't have permission
    var usrLocalRampartWriteWarn = "";
    if ( iam!='root' && stat("/usr/local/rampart") && !has_write_perm("/usr/local/rampart",true) )
        usrLocalRampartWriteWarn = "*";

    //check /usr/local/lib/rampart
    var usrLocalLibRampartWarn = "*";
    if ( iam!='root') {
        if (stat("/usr/local/lib/rampart-modules") && has_write_perm("/usr/local/lib/rampart-modules",true))
            usrLocalLibRampartWarn = "";
        else if (check_create_perm("/usr/local/lib/rampart-modules",true))
            usrLocalLibRampartWarn = "";
    } else
        usrLocalLibRampartWarn = "";

    var warnText = '';
    if (optWriteWarn.length || usrWriteWarn.length)
        warnText = `
* You do not currently have write permissions to this directory or a necessary subdirectory.
`;
    var choices = "123456";
    if(home) {
        homebinopt = ` or in ${home}/bin`;
        homeopt = `
7) ${home}
    - same as 1 but to ${home}/rampart
      and links are optionally placed in ${home}/bin
`
        choices = "1234567";
    }
    while( choices.indexOf(choice) == -1){
        clear();
        printf('%s', 
`Install help:

1) /usr/local/rampart${usrLocalRampartWriteWarn}
    - All necessary files are installed into the above directory.
    - Soft links for binaries are made in '/usr/local/bin'.

2) /opt/rampart${optWriteWarn}
    - same as 1 but into '/opt/rampart'.

3) /usr/local${usrLocalLibRampartWarn}
    - Binaries are installed to '/usr/local/bin'.
    - Modules are installed to '/usr/local/lib/rampart-modules'.${usrLocalLibRampartWarn}

4) /usr${usrWriteWarn}
    - Same as 3, but with prefix '/usr' instead of '/usr/local'.
      NOTE: install into '/usr' is discouraged.  Don't do this unless you
      know what you are doing.

5) custom
    - Same as 1, but script will ask for the location of the custom directory.

6) links only
    - Keep files in this directory. Make links in '/usr/local/bin${homebinopt}'.
${homeopt}
  NOTE that this install utility will overwrite an old installation.  If you
  would like to keep your old installation, install the current in a
  different directory, or alternatively press ctrl-c to exit, move the
  current installation to a new location and re-run this script.
${warnText}
--------------------
`);
        choice=get_install_choice(home);
    }
    return choice;
}

var allfiles;

function get_subdir_files(subdir) {
    var ret=[];
    var sd = process.scriptPath+"/"+subdir+"/";
    if (!allfiles)
        allfiles = exec('find', process.scriptPath).stdout.split('\n');
    for (var i=0;i<allfiles.length;i++) {
        var file = allfiles[i];
        if(file.indexOf(sd)==0)
            ret.push(file);
    }
    return ret;
}

function choose_continue(){
    if(isWindows) {
        printf(
`Welcome to Rampart

Installing this distribution is optional.  Rampart is designed to function
from within any folder, so long as the directory structure herein is
maintained.

1) Continue to installation options
2) Use rampart from this directory (no installation needed)

[1/2] `);
        var resp = getresp('X');
        if(resp == '2') {
            check_dev_shm(process.scriptPath, true);
            printf(`
Rampart is ready to use from this directory.

To add it to your PATH, run:

    setx PATH "%%PATH%%;%s\\bin"

Or add the bin directory to your System PATH via:
    System Properties > Environment Variables > Path > Edit > New

Enjoy!
`, process.scriptPath.replace(/\//g, '\\'));
            process.exit();
        }
        if(resp != '1') {
            clear();
            return choose_continue();
        }
    } else {
        printf(
`Welcome to Rampart

Installing this distribution is optional.  It is designed to function from
within any folder, so long as the directory structure herein is maintained.

If you would like to use this folder as rampart's install location, you can
choose the "links only" option on the next page.

Alternatively you can exit this script and, optionally, manually make a link
as such:

  ln -s %s/bin/rampart /usr/local/bin/rampart

Otherwise for automatic installation choices, you may continue.

Continue? [Y/n]
`, process.scriptPath);

        var resp = getresp('y');

        if ( resp != 'y')
        {
            printf("Enjoy!\n");
            process.exit();
        }
    }
}

function check_curl(){
    var curl = require("rampart-curl");

    if(stat(curl.default_ca_file))
        return;

    clear();
    printf(
`WARNING:

In order to use the rampart-curl module to securely fetch pages from https
websites, the CA certificate bundle file:

    '${curl.default_ca_file}'

must be present.  However it is missing on your system.  Without this file
you will either have to download pages from https websites by specifying an
alternate location for the ca file, or by specifying that the request be
performed insecurely (the 'cacert' and 'insecure' options documented at
'https://rampart.dev/docs/rampart-curl.html#curl-long-options'
respectively).

Your system may already have the necessary file in a different location, in which
case you can copy it to '${curl.default_ca_file}'. Or this script can download
the libcurl maintained version.

Would you like this script to attempt to download the necessary file from

    https://curl.se/ca/cacert.pem
    
and install in the above location?] [Y/n] `);

    stdout.fflush();
    resp = getresp('y');
    printf("\n");

    if(resp != 'y') {

        printf("\nMore information about getting the CA bundle can be found at:\n"+
               "   https://daniel.haxx.se/blog/2018/11/07/get-the-ca-cert-for-curl/\n\n");
        return;
    }

    var crypto = require('rampart-crypto');

    var res = curl.fetch("https://curl.se/ca/cacert.pem", {insecure:true});


    if(res.status !=200)
    {
        console.log(`Error getting '${res.url}'\n  ${res.statusText}\n   ${res.errMsg}`);
        process.exit(1);
    }

    var res2 = curl.fetch("https://curl.se/ca/cacert.pem.sha256", {insecure:true});

    if(res2.status !=200)
    {
        console.log(`Error getting '${res2.url}'\n  ${res2.statusText}\n   ${res2.errMsg}`);
        process.exit(1);
    }


    var verified_sha = res2.text.match(/\S{64}/);

    if (verified_sha.length == 1)
        verified_sha = verified_sha[0]
    else {
        console.log("Error getting checksum at 'https://curl.se/ca/cacert.pem.sha256'");
        process.exit(1);
    }

    var calc_sha = crypto.sha256(res.text);

    if(verified_sha != calc_sha)
    {
        console.log("Error: checksum from https://curl.se/ca/cacert.pem does not match https://curl.se/ca/cacert.pem.sha256");
        process.exit(1);
    }

    var certdir = curl.default_ca_file.substring(0, curl.default_ca_file.lastIndexOf('/'));
    if(! check_create_perm(certdir) )
    {
        // 'you don't have write permissions...' printed in check_create_perm
        printf("Attempting to save cert bundle file to '/tmp/cacert.pem'.\n");
        fprintf( "/tmp/cacert.pem", '%s', res.text);
        printf(`
File has been saved to '/tmp/cacert.pem'.
You will need to manually copy this file with, e.g.:
    sudo cp /tmp/cacert.pem ${curl.default_ca_file}

`);
        return;
    }

    if(!stat(certdir))
        mkdir(certdir);

    fprintf( curl.default_ca_file, '%s', res.text);
    
    console.log(`${curl.default_ca_file} was successfully created.
    
Press any key to continue.`);
    getresp(' ');

}


/* ************ Windows install ******************* */

function check_dev_shm(prefix, ask) {
    var rp = prefix.replace(/\\/g, '/');
    var lastslash = rp.lastIndexOf('/');
    var parent = lastslash > 0 ? rp.substring(0, lastslash) : '/';
    var devshm = parent + '/dev/shm';
    if(!stat(devshm)) {
        if(ask) {
            printf("\nThe directory '%s' is required by the Cygwin runtime but does not exist.\n", devshm);
            printf("Create it? [Y/n]\n");
            var resp = getresp('y');
            if(resp != 'y') {
                printf("You will need to create this directory manually before running rampart.\n");
                process.exit(1);
            }
        }
        try {
            mkdir(devshm);
            printf("Created '%s'\n", devshm);
        } catch(e) {
            printf("Could not create '%s': %s\n", devshm, e);
            printf("You will need to create this directory manually before running rampart.\n");
            process.exit(1);
        }
    }
}

function do_windows_install() {
    var default_prefix = '/c/Program Files/mcs/rampart';
    var home = process.env.HOME || process.env.USERPROFILE;
    var homeopt = "";
    var choices = "13";

    if(home) {
        // Convert backslashes to forward slashes for MSYS paths
        home = home.replace(/\\/g, '/');
        // On relocated Cygwin, HOME may be root-relative (e.g. //rampart).
        // Resolve to /c/... form via realPath.
        try { home = realPath(home); } catch(e) {}
        homeopt = `2) ${home}/rampart\n`;
        choices = "123";
    }

    clear();
    printf(
`Install rampart to:

1) /c/Program Files/mcs/rampart (recommended)
${homeopt}3) custom location
`);
    var choice = getresp('1');

    if(choices.indexOf(choice) == -1) {
        clear();
        return do_windows_install();
    }

    var prefix;

    var ask_dev_shm = true;
    if(choice == '1') {
        prefix = default_prefix;
        ask_dev_shm = false;
    } else if(choice == '2' && home) {
        prefix = home + '/rampart';
    } else if(choice == '3') {
        printf("Enter location (use forward slashes, e.g. /c/tools/rampart):\n");
        prefix = trim(readLine(stdin).next());
        if(prefix == '') {
            clear();
            return do_windows_install();
        }
        if(prefix.lastIndexOf('/') == prefix.length - 1)
            prefix = prefix.substring(0, prefix.length - 1);
        printf("Install into '%s'? (y/n)\n", prefix);
        var resp = getresp('');
        if(resp != 'y') {
            clear();
            return do_windows_install();
        }
    }

    var ret = do_install(prefix, singledir_install, false);

    if(ret) {
        check_dev_shm(prefix, ask_dev_shm);

        // Convert MSYS path to Windows path for display
        var winpath = prefix;
        if(winpath.match(/^\/([a-zA-Z])\//))
            winpath = winpath.replace(/^\/([a-zA-Z])\//, function(m, drive){ return drive.toUpperCase() + ':\\'; }).replace(/\//g, '\\');

        var binpath = winpath + '\\bin';
        var curpath = process.env.PATH || '';
        // Check if already in PATH (case-insensitive, check both / and \ forms)
        var inPath = curpath.toLowerCase().indexOf(binpath.toLowerCase()) > -1
                  || curpath.toLowerCase().indexOf(prefix.toLowerCase() + '/bin') > -1;

        if(inPath) {
            printf("\n'%s' is already in your PATH.\n", binpath);
        } else {
            printf("\nAdd '%s' to your PATH? [Y/n]\n", binpath);
            var resp = getresp('y');
            if(resp == 'y') {
                // Read just the User PATH via PowerShell (not the full env PATH which includes system paths)
                var psres = exec('/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe', '-c',
                    "[Environment]::GetEnvironmentVariable('Path','User')");
                var userpath = trim(psres.stdout);
                var newpath = userpath.length ? userpath + ';' + binpath : binpath;
                var res = exec('/c/Windows/System32/setx.exe', 'PATH', newpath);
                if(res.exitStatus === 0)
                    printf("PATH updated. Restart your terminal for changes to take effect.\n");
                else
                    printf("Could not update PATH:\n    %s\n", res.stderr);
            } else {
                printf(`
To add it manually later, run:

    setx PATH "%%PATH%%;${binpath}"

Or add '${binpath}' to your System PATH via:
    System Properties > Environment Variables > Path > Edit > New

`);
            }
        }
    }

    return ret;
}

/* ************ main ******************* */


/* prevents install to /home/user/rampart
if (iam != 'root') {
    if(!has_write_perm("/usr/local/bin",true))
    {
        printf (`
_________________________________________________________
|  You do not have write permissions for /usr/local/bin. |
|  You should be root or use:                            |
|                                                        |
|       sudo ./install.js                                |
|                                                        |
|  to proceed.                                           |
|________________________________________________________|

ALTERNATIVELY:

  If you want to use rampart right here where it is, that is fine too.
  You will likely want to add the bin directory to your path like such:

        PATH=${process.scriptPath}/bin:\$PATH

  And to make it permanent, add the above line to the bottom of, e.g., ~/.bashrc

  In addition, see LICENSE file in this directory and also pay a visit to
        https://rampart.dev/docs/
`);
        process.exit(1);
    }
}

*/
// change to script directory
chdir(process.scriptPath);

/* make sure all our needed dirs are in the current dir */
//var needed_dirs = [ 'modules', 'bin', 'examples', 'test', 'web_server', 'include' ];
var needed_dirs = Object.keys(local_install);

for (var i=0; i<needed_dirs.length; i++) {
    if (!stat(process.scriptPath+'/'+needed_dirs[i])) {
        printf("Error: a needed directory '%s/%s' is missing. Cannot continue.\n", process.scriptPath,needed_dirs[i]);
        process.exit(1);
    }
}

clear();

greetings();

clear();

choose_continue();

clear();

check_curl();

if(isWindows) {
    while(!do_windows_install()) {}
} else {
    var choice = choose_install();

    while(!do_install_choice(choice))
        choice = choose_install();
}

if(madeNew.length) {
    printf(
`
The following files were not overwritten and instead 
written to a file with the extension '.new':
`);
    for (var i =0; i<madeNew.length; i++)
        printf("    %s\n",madeNew[i]+'.new');
}

printf("\nInstallation complete");

if(with_err != '')
    printf(" with the following error(s):%s\n", with_err);
else
    printf(`


Also of interest in this directory (but not copied):

  1)  The "run_tests.sh" script and the "./test" directory.  If you'd like
      to run some tests, or just have a look to see what is possible.

  2)  The "./web_server" directory contains a template configuration you 
      might want to use for serving web pages.

  3)  The "./examples" directory has a sample module written in C which
      you can use as a template for making your own module written in C.

  4)  The "./unsupported_extras" directory includes a websocket client and 
      a utility to create template rampart module projects with Makefile, 
      *.c and *-test.js files (c_module_template_maker/make_cmod_template.js).

Thank you for installing.  Enjoy!
`);





