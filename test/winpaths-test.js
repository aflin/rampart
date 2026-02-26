rampart.globalize(rampart.utils);

var isWindows = /MSYS_NT/.test(rampart.buildPlatform);

if (!isWindows) {
    printf("testing winpaths - %-49s - skipping (non-Windows)\n", "all tests");
    process.exit(0);
}

chdir(process.scriptPath);

var pathre = /^\/[a-z]\//;  /* matches /c/..., /d/..., etc. */

var tmpdir = process.scriptPath + "/winpaths_tmp";

/* recursively remove a directory and all its contents */
function rmrf(dir) {
    var st = stat(dir);
    if (!st) return;
    if (st.isDirectory) {
        readdir(dir).forEach(function(e){
            if (e != "." && e != "..")
                rmrf(dir + "/" + e);
        });
        rmdir(dir);
    } else {
        rmFile(dir);
    }
}

/* clean up from any previous failed run */
rmrf(tmpdir);

mkdir(tmpdir);

function testFeature(name, test)
{
    var error = false;
    if (typeof test == 'function') {
        try {
            test = test();
        } catch(e) {
            error = e;
            test = false;
        }
    }
    printf("testing winpaths - %-49s - ", name);
    if (test)
        if (typeof test == 'string')
            printf("%s\n", test);
        else
            printf("passed\n");
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        if (error) console.log(error);
        /* clean up before exit */
        try { chdir(process.scriptPath); rmrf(tmpdir); } catch(e) {}
        process.exit(1);
    }
    if (error) console.log(error);
}


/* ================================================================
   Section 1: process path properties use /c/... format
   ================================================================ */

testFeature("process.installPath is /c/...", function(){
    return typeof process.installPath == 'string' && pathre.test(process.installPath);
});

testFeature("process.installPathBin is /c/...", function(){
    return typeof process.installPathBin == 'string' && pathre.test(process.installPathBin);
});

testFeature("process.installPathExec is /c/...", function(){
    return typeof process.installPathExec == 'string' && pathre.test(process.installPathExec);
});

testFeature("process.modulesPath is /c/...", function(){
    return typeof process.modulesPath == 'string' && pathre.test(process.modulesPath);
});

testFeature("process.scriptPath is /c/...", function(){
    return typeof process.scriptPath == 'string' && pathre.test(process.scriptPath);
});

testFeature("process.script is /c/...", function(){
    return typeof process.script == 'string' && pathre.test(process.script);
});

testFeature("process.argv0 is /c/...", function(){
    return typeof process.argv0 == 'string' && pathre.test(process.argv0);
});

testFeature("process.argv[0] is /c/...", function(){
    return typeof process.argv[0] == 'string' && pathre.test(process.argv[0]);
});


/* ================================================================
   Section 2: path-returning utility functions use /c/... format
   ================================================================ */

testFeature("getCwd() returns /c/...", function(){
    return pathre.test(getCwd());
});

testFeature("realPath('.') returns /c/...", function(){
    return pathre.test(realPath("."));
});

testFeature("realPath(tmpdir) returns /c/...", function(){
    var rp = realPath(tmpdir);
    return pathre.test(rp) && rp == tmpdir;
});


/* ================================================================
   Section 3: filesystem operations work with /c/... paths
   ================================================================ */

testFeature("chdir to /c/... path", function(){
    chdir(tmpdir);
    var cwd = getCwd();
    return cwd == tmpdir;
});

testFeature("mkdir with /c/... path", function(){
    var subdir = tmpdir + "/subdir1/subdir2";
    mkdir(subdir, 0777);
    var st = stat(subdir);
    rmdir(tmpdir + "/subdir1/subdir2");
    rmdir(tmpdir + "/subdir1");
    return st && st.isDirectory;
});

var testfile = tmpdir + "/testfile.txt";
var testcontent = "hello from winpaths test\nsecond line\n";

testFeature("fprintf to /c/... path", function(){
    fprintf(testfile, "%s", testcontent);
    return stat(testfile) && stat(testfile).isFile;
});

testFeature("readFile from /c/... path", function(){
    var content = readFile(testfile, true);
    return content == testcontent;
});

testFeature("readLine from /c/... path", function(){
    var rl = readLine(testfile);
    var line1 = rl.next();
    var line2 = rl.next();
    return trim(line1) == "hello from winpaths test" &&
           trim(line2) == "second line";
});

var fopen_file = tmpdir + "/fopen_test.txt";

testFeature("fopen/fprintf/fread with /c/... path", function(){
    var fh = fopen(fopen_file, "w+");
    fh.fprintf("fopen content");
    fh.rewind();
    var content = fh.fread(100, true);
    fh.fclose();
    return content == "fopen content";
});

testFeature("stat file at /c/... path", function(){
    var st = stat(testfile);
    return st && st.isFile && st.size == testcontent.length;
});

var touchfile = tmpdir + "/touched.txt";

testFeature("touch at /c/... path", function(){
    touch(touchfile);
    var st = stat(touchfile);
    return st && st.isFile;
});

var copyfile = tmpdir + "/copied.txt";

testFeature("copyFile with /c/... paths", function(){
    copyFile(testfile, copyfile, true);
    var content = readFile(copyfile, true);
    return content == testcontent;
});

var renamefile = tmpdir + "/renamed.txt";

testFeature("rename with /c/... paths", function(){
    rename(copyfile, renamefile);
    var gone = !stat(copyfile);
    var content = readFile(renamefile, true);
    return gone && content == testcontent;
});

var linkfile = tmpdir + "/linked.txt";

testFeature("link (hard) with /c/... paths", function(){
    link(testfile, linkfile);
    var st1 = stat(testfile);
    var st2 = stat(linkfile);
    return st1 && st2 && st1.ino == st2.ino;
});

testFeature("readdir with /c/... path", function(){
    var entries = readdir(tmpdir);
    var found_testfile = false;
    var found_fopen = false;
    var found_touched = false;
    entries.forEach(function(e){
        if (e == "testfile.txt") found_testfile = true;
        if (e == "fopen_test.txt") found_fopen = true;
        if (e == "touched.txt") found_touched = true;
    });
    return found_testfile && found_fopen && found_touched;
});

testFeature("rmFile with /c/... path", function(){
    rmFile(linkfile);
    rmFile(renamefile);
    rmFile(touchfile);
    rmFile(fopen_file);
    rmFile(testfile);
    return !stat(linkfile) && !stat(renamefile) && !stat(touchfile) &&
           !stat(fopen_file) && !stat(testfile);
});

testFeature("rmdir with /c/... path", function(){
    /* tmpdir should now be empty */
    rmdir(tmpdir);
    return !stat(tmpdir);
});


/* ================================================================
   Cleanup - chdir back
   ================================================================ */

chdir(process.scriptPath);
