#!./bin/rampart

rampart.globalize(rampart.utils);

var singledir_install = {
    bin:          "/bin",
    include:      "/include",
    examples:     "/examples",
    modules:      "/modules",
    "web_server": "/web_server"
}

var local_install = {
    bin:          "/bin",                 // /usr/local/bin
    include:      "/include",             // /usr/local/include
    examples:     "/rampart/examples",    // /usr/local/rampart/examples
    modules:      "/lib/rampart_modules", // /usr/local/lib/rampart_modules
    "web_server": "/rampart/web_server"   // /usr/local/rampart/web_server
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

function get_install_choice() {
    printf(
`Install rampart to:

1) /usr/local/rampart
2) /opt/rampart
3) /usr/local
4) /usr (not recommened)
5) custom
h) help
`);
    return getresp('h');
}

function greetings(){
    printf(
`Welcome to Rampart Install

Rampart and its modules are licensed under the MIT license.

Rampart modules use many different libraries which all use various
permissive open source licenses.

The one exception is rampart-sql and the texis library used in rampart-sql,
which uses a source available license and is available without charge for
certain uses.  See license for details.

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
        if(!noPrintErr)
            printf("You do not have write permissions in '%s'\n", dir);
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

function check_create_perm(dir){
    // does the directory exist?
    var ret=false;
    var subdir = dir;

    if(dir.charAt(dir.length-1) == '/')
        dir = dir.substring(0, dir.length-1);
    
    while (!stat(subdir))
        subdir = prevdir(subdir);

    if(!has_write_perm(subdir))
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
                //will get 'text file busy' error if replacing the rampart that we are using.
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

var with_err="";

var link_bin_files = ['rampart', 'tsql', 'rex', 'texislockd', 'addtable', 'kdbfchk' ];

function do_make_links(prefix) {
    var bindir = prefix + '/bin';
    var i=0;

    for(;i<link_bin_files.length;i++){
        var lfile = bindir + '/' + link_bin_files[i]; 
        var link  = "/usr/local/bin/" + link_bin_files[i];

        var linkstat = lstat(link);
        if(linkstat)
        {
            //link already exists
            if(linkstat.isSymbolicLink) {
                rmFile(link);                
            } else {
                printf("Cannot create link '%s', file exists and is not a link\n", link);
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

function do_install(prefix, map, makelinks){
    var dirs = Object.keys(map);
    var i=0;

    if(!check_make_dir(prefix))
        return false;

    for (i=0;i<dirs.length;i++) {
        var dir=dirs[i];
        var src = realPath(dir);
        var dest = realPath(prefix) + map[dir];
        var backup = (dir == "web_server" || dir == 'examples' ) ? true:false;

        if(src == dest)
            continue;
        copy_files(src, dest, backup);
        if( dir == 'web_server' ) {
            try {
                shell("chown -R nobody " + dest);
            } catch(e){
                with_err += sprintf("\n    - could not 'chown nobody' %s", dest);
            }
        }
    }

    if(makelinks)
    {
        if(!has_write_perm("/usr/local/bin"))
            printf("No links to binaries will be made\n");
        else
            do_make_links(prefix);
    }    
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
            }
            printf("create links to binaries in '/usr/local/bin'? [Y/n]\n");
            resp=getresp("y");            
            if (resp == 'y')
                do_install(loc, singledir_install, true);
            else
                do_install(loc, singledir_install, false);

            break;
    }
    return true;
}

function choose_install() {
    var choice = get_install_choice();

    while( "12345".indexOf(choice) == -1){
        clear();
        printf('%s', 
`Install help:

1) /usr/local/rampart
    - All files are installed into this directory.
    - Soft links for binaries are made in '/usr/local/bin'.

2) /opt/rampart
    - same as 1 but into '/opt/rampart'.

3) /usr/local
    - Binaries are installed to '/usr/local/bin'.
    - Modules are installed to '/usr/local/lib/rampart-modules'.
    - Examples and tests are installed in '/usr/local/rampart'.

4) /usr
    - Same as 3, but with prefix '/usr' instead of '/usr/local'.
      **NOTE** install into '/usr' is highly discouraged.  Don't do this
      unless you know what you are doing and have a very good reason.

5) custom
    - Same as 1, but script will ask for the location of the custom directory.

h) help
    - This Message.

* NOTE that files in the 'examples' and 'web_server' directories will not
  overwrite existing files in the installation directory.  If files differ,
  they will be written to a new file with '.new' extension added.

--------------------
`);
        choice=get_install_choice();
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
    printf(
`Welcome to Rampart

Installing this distribution is optional.  It is designed to function from
within any folder, so long as the directory structure herein is maintained.
If you would like to use this folder as rampart's install location, you can
exit this script and, optionally, manually make a link as such:

  ln -s %s/bin/rampart /usr/local/bin/rampart

If you are installing from and to the same directory, no files will be copied,
however soft links to still be made in the /usr/local/bin directory.

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

must be present.  However it is missing on your system. Without this file
you will either have to download pages from https websites by specifying an
alternate location, or by specifying that the request be performed
insecurely (the 'cacert' and 'insecure' options documented at
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
    
    console.log(`${curl.default_ca_file} was successfully created`);
}


/* ************ main ******************* */

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

var choice = choose_install();

while(!do_install_choice(choice))
    choice = choose_install();

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
    printf(" with the following error(s):%s", with_err);

printf("\n");





