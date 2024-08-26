//first line
rampart.globalize(rampart.utils);

chdir(process.scriptPath);

function testFeature(name,test)
{
    var error=false;
    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }
    printf("testing utils - %-52s - ", name);
    if(test)
        if(typeof test =='string')
            printf("%s\n", test);
        else
            printf("passed\n")
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        if(error) console.log(error);
        process.exit(1);
    }
    if(error) console.log(error);
}


printf("testing utils - %-52s - ", "printf");
printf("passed\n");

fprintf(stdout,"testing utils - %-52s - ", "fprintf(stdout,...)");
fprintf(stdout,"passed\n");

fprintf(stderr,"testing utils - %-52s - ", "fprintf(stderr,...)");
fprintf(stderr,"passed\n");


testFeature("fopen/fseek/fprint/fread/fwrite/rewind",function(){

    var fh=fopen("test.txt","w+");

    fh.fprintf("abcdef");

    fh.fprintf("ghijkl");

    fh.rewind();
    fh.fprintf("123");

    fh.fseek(-3,"SEEK_END");
    fh.fprintf("456");

    fh.rewind();
    var buf=fh.fread(1000);

    var out="789abcdefghijklmnopqrstuvwxyz";
    fh.fwrite(out,3);

    fh.rewind();
//    var res1=bufferToString(fread(fh,1000));
    var res1=fh.fread(1000,true);
    rmFile("test.txt");
    return res1=="123defghi456789";
});

testFeature("fopen - stdout redirect",function(){
    var fh = fopen("test.txt","w", stdout);

    printf("abcdef");
    fh.fclose();

    var res1=readFile("test.txt" ,true);

    return res1=="abcdef";
});

var str=readFile(process.script,-220,-20,true);

testFeature("readFile/string",
    str.length==200
);

var buf=readFile(process.script,{offset:-220,length:-20,retString:false});

testFeature("readFile/buf - bufferToString",function(){
    return buf.length==200 && bufferToString(buf)==str;
});

testFeature("readLine/trim",function(){
    var rl=readLine(process.script);
    var i=0;
    var line;
    var firstline;
    var lastline;
    while ( (line=rl.next()) ) {
        if(i==0)
            firstline=trim(line);
        i++;
        lastline=line;
    }
    return firstline=="//first line" && trim(lastline)=="//lastline";
});

testFeature("stat",function(){
    var st=stat("/dev/null");
    return st.isCharacterDevice;
});



testFeature("execRaw/timeout",function(){
    var ret=execRaw({
        path:"/bin/sleep",
        args:["sleep","10"],
        timeout:200
    });
    return ret.timedOut;
});

testFeature("exec/env/stdin",function(){
    var ret=exec("head", "-n", "1", process.script);
    var ret2=exec('env',{env:{myvar:"myval"}});
    var ret3=exec('cat',{stdin:"hello"});
    return (
        trim(ret.stdout) == "//first line" &&
        trim(ret2.stdout)== "myvar=myval" &&
        ret3.stdout == "hello"
    );

});

testFeature("shell",function(){
    var ret=shell("tail -n 1 "+ process.script, {timeout:2000} );
    return trim(ret.stdout)=="//lastline";
});


testFeature("exec/bkgrnd/kill",function(){
    var ret=exec("sleep", "30", {background:true});
    return kill(ret.pid);
});

testFeature("mkdir/rmdir/stat",function(){
    mkdir("t1/t2",0777);
    var stat1=stat("t1/t2");
    rmdir("t1/t2",true);
    var mode1=sprintf("%o",stat1.mode & 0777);
    var stat2=true;
    stat2=stat("t1");
    return (!stat2 && mode1=="777");
});

testFeature("readdir",function(){
    var gotdot=false;
    var gotthis=false;
    readdir(process.scriptPath,true).forEach(function(d){
        if(d==".") gotdot=true;
        if(d==process.scriptName) gotthis=true
    });
    return gotthis && gotdot;
});

testFeature("copy/delete",function(){
    copyFile(process.script,"test1.js",true);
    var stat1=stat("test1.js");
    var diff=shell("diff "+ process.script +" test1.js");
    rmFile("test1.js");
    var stat2=stat("test1.js");
    return stat1.mode && !stat2 && diff.stdout == "";
});

testFeature("symlink/delete/lstat",function(){
    symlink(process.script,"test1.js");
    var islink=lstat("test1.js").isSymbolicLink;
    var diff=shell("diff "+ process.script +" test1.js");
    rmFile("test1.js");
    var stat2=stat("test1.js");
    return islink && !stat2 && diff.stdout == "";
});

testFeature("hard link/delete",function(){
    fprintf("myfile.txt","a message to nobody");
    link({
        src:"myfile.txt",
        target:"test1.txt"
    });
    var stat1=stat("test1.txt");
    var test=shell("if [ test1.txt -ef myfile.txt ]; then echo yes; fi");
    rmFile("test1.txt");
    var stat2=stat("test1.txt");
    return stat1.mode && !stat2 && test.stdout == "yes\n";
});

testFeature("copy over hard/sym link throw",function(){
    link("myfile.txt", "hardlink");
    symlink("hardlink", "symlink");
    var ret=false;
    try{
        copyFile("myfile.txt", "symlink");
    } catch(e) {
        //console.log(e);
        ret=true;
    }
    rmFile("hardlink");
    rmFile("symlink");
    rmFile("myfile.txt");
    return ret;
});

testFeature("touch/rename",function(){
    touch("myfile");
    var stat1=stat("myfile");
    rename("myfile","/tmp/myfile"); //copies if different mounted fs
    var stat2=stat("/tmp/myfile");
    rmFile("/tmp/myfile");
    return stat1 && stat2;
});

testFeature("reference touch",function(){
    touch({
        path:"myfile",
        reference:process.script
    });
    var stat1=stat("myfile");
    var stat2=stat(process.script);
    rmFile("myfile");

    return stat1.atime.getSeconds() == stat2.atime.getSeconds() && stat1.mtime.getSeconds() == stat2.mtime.getSeconds();
});

testFeature("fork and pipe", function(){
    var pipe = newPipe();
    var pipe2 = newPipe();
    var pid = fork(pipe,pipe2);

    if(pid==-1) {
        console.log("fork failed");
        return false;
    }

    if(pid==0){
        //child
        var sz1=pipe.write("my message");
        var sz2=pipe2.write("my ev message");
        process.exit();
    } else {
        pipe2.onRead(function(val,err){
            if(val=="my ev message")
            {
                testFeature("fork and pipe event", true);
                pipe2.close();
            }
            else {
                if(err)
                    console.log("pipe2 error:",err);
                testFeature("fork and pipe event", false);
            }
        });

        var ret=false;

        pipe.read(function(val,err){
            if(val == "my message")
                ret = true;
            if(err)
                console.log(err);
        });
        return ret;
    }

});

testFeature("daemon and pipe", function(){
    var pipe = newPipe();
    var pipe2 = newPipe();
    var pid = daemon(pipe,pipe2);

    if(pid==-1) {
        console.log("fork failed");
        return false;
    }

    if(pid==0){
        //child
        var sz1=pipe.write("my message");
        var sz2=pipe2.write(process.getpid());
        process.exit();
    } else {
        pipe2.onRead(function(val,err){
            if(pid==val)
            {
                testFeature("daemon and pipe event", true);
                pipe2.close();
            }
            else {
                if(err)
                    console.log("pipe2 error:",err);
                testFeature("daemon and pipe event", false);
            }
        });

        var ret=false;

        pipe.read(function(val,err){
            if(val == "my message")
                ret = true;
            if(err)
                console.log(err);
        });
        return ret;
    }

});

var wai=trim(shell("whoami").stdout);
if (wai=="root")
{
    testFeature("chown", function(){
        touch("myfile");
        chown({
            path:"myfile",
            group:101,
            user: 100
        });

        var stat1=stat("myfile");
        rmFile("myfile");
        return stat1.uid == 100 && stat1.gid == 101;
    });

} else
    testFeature("chown", "skipping");

var asdfmts=[
    "%Y-%m-%d %I:%M:%S %p %z",          //  0: 1999-12-31 11:59:59 pm -0800
    "%A %B %d %I:%M:%S %p %Y %z",       //  1: Fri Dec 31 11:59:59 pm 1999 -0800
    "%Y-%m-%d %I:%M:%S %p",             //  2: 1999-12-31 11:59:59 pm
    "%A %B %d %I:%M:%S %p %Y",          //  3: Fri Dec 31 11:59:59 pm 1999
    "%Y-%m-%d %H:%M:%S %z",             //  4: 1999-12-31 23:59:59 -0800
    "%A %B %d %H:%M:%S %Y %z",          //  5: Fri Dec 31 23:59:59 1999 -0800
    "%Y-%m-%d %H:%M:%S",                //  6: 1999-12-31 23:59:59
    "%A %B %d %H:%M:%S %Y",             //  7: Fri Dec 31 23:59:59 1999
    "%Y-%m-%dT%H:%M:%S ",               //  8: javascript style from console.log(new Date()). space is for erased '.123Z' below
    // standard without seconds
    "%Y-%m-%d %I:%M %p %z",             //  9: 1999-12-31 11:59 pm -0800
    "%A %B %d %I:%M %p %Y %z",          // 10: Fri Dec 31 11:59 pm 1999 -0800
    "%Y-%m-%d %I:%M %p",                // 11: 1999-12-31 11:59 pm
    "%A %B %d %I:%M %p %Y",             // 12: Fri Dec 31 11:59 pm 1999
    "%Y-%m-%d %H:%M %z",                // 13: 1999-12-31 23:59 -0800
    "%A %B %d %H:%M %Y %z",             // 14: Fri Dec 31 23:59 1999 -0800
    "%Y-%m-%d %H:%M",                   // 15: 1999-12-31 23:59
    "%A %B %d %H:%M %Y",                // 16: Fri Dec 31 23:59 1999
    // locale dependent:
    "%x %r %z",                         // 17: date varies, time: 11:59:59 pm -0800
    "%x %r",                            // 18: date varies, time: 11:59:59 pm
    "%c",                               // 18: varies
    "%x %X %z",                         // 20: varies
    "%x %X",                            // 21: varies
    // others
    "%b %e %I:%M:%S %p %Y %z",          // 22: Dec 31 11:59:59 pm 1999 -0800
    "%b %e %I:%M:%S %p %Y",             // 23: Dec 31 11:59:59 pm 1999
    "%b %e %I:%M %p %Y %z",             // 24: Dec 31 11:59 pm 1999 -0800
    "%b %e %I:%M %p %Y",                // 25: Dec 31 11:59 pm 1999
    "%b %e %H:%M:%S %Y %z",             // 26: Dec 31 23:59:59 1999 -0800
    "%b %e %H:%M:%S %Y",                // 27: Dec 31 23:59:59 1999
    "%b %e %H:%M %Y %z",                // 28: Dec 31 23:59 1999 -0800
    "%b %e %H:%M %Y",                   // 29: Dec 31 23:59 1999
    "%e %b %I:%M:%S %p %Y %z",          // 30: 31 Dec 11:59:59 pm 1999 -0800
    "%e %b %I:%M:%S %p %Y",             // 31: 31 Dec 11:59:59 pm 1999
    "%e %b %I:%M %p %Y %z",             // 32: 31 Dec 11:59 pm 1999 -0800
    "%e %b %I:%M %p %Y",                // 33: 31 Dec 11:59 pm 1999
    "%e %b %H:%M:%S %Y %z",             // 34: 31 Dec 23:59:59 1999 -0800
    "%e %b %H:%M:%S %Y",                // 35: 31 Dec 23:59:59 1999
    "%e %b %H:%M %Y %z",                // 36: 31 Dec 23:59 1999 -0800
    "%e %b %H:%M %Y",                   // 37: 31 Dec 23:59 1999
    "%m/%d/%y %I:%M:%S %p %z",          // 38: 12/31/99 11:59:59 pm -0800
    "%m/%d/%y %I:%M:%S %p",             // 39: 12/31/99 11:59:59 pm
    "%m/%d/%y %I:%M %p %z",             // 40: 12/31/99 11:59 pm -0800
    "%m/%d/%y %I:%M %p",                // 41: 12/31/99 11:59 pm
    "%m/%d/%y %H:%M:%S %z",             // 42: 12/31/99 23:59:59 -0800
    "%m/%d/%y %H:%M:%S",                // 43: 12/31/99 23:59:59
    "%m/%d/%y %H:%M %z",                // 44: 12/31/99 23:59 -0800
    "%m/%d/%y %H:%M",                   // 45: 12/31/99 23:59
    "%m/%d/%Y %I:%M:%S %p %z",          // 46: 12/31/1999 11:59:59 pm -0800
    "%m/%d/%Y %I:%M:%S %p",             // 47: 12/31/1999 11:59:59 pm
    "%m/%d/%Y %I:%M %p %z",             // 48: 12/31/1999 11:59 pm -0800
    "%m/%d/%Y %I:%M %p",                // 49: 12/31/1999 11:59 pm
    "%m/%d/%Y %H:%M:%S %z",             // 50: 12/31/1999 23:59:59 -0800
    "%m/%d/%Y %H:%M:%S",                // 51: 12/31/1999 23:59:59
    "%m/%d/%Y %H:%M %z",                // 52: 12/31/1999 23:59 -0800
    "%m/%d/%Y %H:%M",                   // 53: 12/31/1999 23:59
    // date only
    "%m/%d/%y %z",                      // 54: 12/31/99 -0800
    "%m/%d/%y",                         // 55: 12/31/99
    "%m/%d/%Y %z",                      // 56: 12/31/1999 -0800
    "%m/%d/%Y",                         // 57: 12/31/1999
    "%Y-%m-%d %z",                      // 58: 1999-12-31 -0800
    "%Y-%m-%d",                         // 59: 1999-12-31
    "%x"                                // 60: varies
]

var dateonly=53; //res after this will be at midnight, because format has no %H

//var nowgmt = new Date("2024-01-01T08:30:00.000Z");
//var nowgmt = new Date("2024-07-01T08:30:00.000Z");
var nowgmt = new Date();
var m = dateFmt("%Y-%m-%dT00:00:00.000Z",nowgmt);
var midnight = new Date(m);


var midnightlocal = new Date(nowgmt);
midnightlocal.setHours(0);
midnightlocal.setMinutes(0);
midnightlocal.setSeconds(0);
midnightlocal.setMilliseconds(0);

//printf("midnight=%s\n", midnight);
//printf("midnightlocal=%s\n", midnightlocal);

for (var i=0; i<asdfmts.length; i++)
{
    var cfmt = asdfmts[i];
    var res=false, diff=0;

    var df = dateFmt(cfmt, nowgmt);
    var asd=autoScanDate(df);
    var cmpdate;

    if(i>dateonly) // local midnight and gmt midnight are different gmt times.
    {
        if ( cfmt.indexOf('z')==-1 )
            cmpdate=midnight
        else
            cmpdate=midnightlocal;
    }
    else // always the same gmt time
    {
            cmpdate=nowgmt;
    }

    diff=asd.date.getTime() - cmpdate.getTime();

    // some formats don't have seconds.  So no diff should be greater than 60000 ms
    if(diff<0) diff*=-1;

    if(diff < 60000)
        res=true
    else
        res=false;

    testFeature('autoScanDate "' + cfmt +'"', function(){
        if(res)
            return true;
        else
        {
            printf("failed %s vs %s\n", asd.date, cmpdate);
            printf("input = '%s'\n", df);
            return false;
        }
    });

}


nowgmt=new Date( (""+nowgmt).replace(/^20/, "18") )
midnight=new Date( (""+midnight).replace(/^20/, "18") );

midnightlocal = new Date(nowgmt);
midnightlocal.setHours(0);
midnightlocal.setMinutes(0);
midnightlocal.setSeconds(0);
midnightlocal.setMilliseconds(0);

// duktape bug(??): erroneously sets dst for dates before 1901, or maybe not eroneous cuz node does the same.
// either way, /usr/share/zonenfo has no data before 1901 and if we have '1824/06/01 -0800', we expect the equiv gmt to be 8 hours ahead.
// And apparently (according to wikipedia) "Port Arthur, Ontario, Canada, was the first city in the world to enact DST, on 1 July 1908"
// - https://en.wikipedia.org/w/index.php?title=Daylight_saving_time&oldid=1234547798#History
if( midnightlocal.getTimezoneOffset() != new Date("2024-01-01T12:00:00.000Z").getTimezoneOffset() )
    midnightlocal.setHours(1);

var len = 0;
if(rampart.versionBits == 64)
    len = asdfmts.length;
else
   testFeature('autoScanDate 1800s - 32bit platform', "skipping"); 

for (var i=0; i<len; i++)
{
    var cfmt = asdfmts[i];
    var res=false, diff=0;
    var df = dateFmt(cfmt, nowgmt);

    var asd=autoScanDate(df);

    if(!asd)
    {
        testFeature('autoScanDate 1800s', "unsupported by OS");
        break;
    }

    // will naturally fail if %y is used
    if(cfmt.indexOf('y')!=-1 || cfmt.indexOf('x')!=-1)
        continue;

    if(i>dateonly) // local midnight and gmt midnight are different gmt times.
    {
        if ( cfmt.indexOf('z')==-1 )
            cmpdate=midnight;
        else
            //continue;// despite giving it MM/DD/1824 -0800, duktape is insisting that this is 7 hours from GMT
            cmpdate=midnightlocal;
    }
    else // always the same gmt time
    {
            cmpdate=nowgmt;
    }

    diff=asd.date.getTime() - cmpdate.getTime();

    // some formats don't have seconds.  So no diff should be greater than 60000 ms
    if(diff<0) diff*=-1;

    if(diff < 60000)
        res=true
    else
        res=false;

    testFeature('autoScanDate 1800s "' + cfmt +'"', function(){
        if(res)
            return true;
        else
        {
            printf("failed got %s, want %s\n", asd.date, cmpdate);
            printf("input = '%s'\n", df);
            return false;
        }
    });

}


testFeature("autoScanDate Abbr", function(){
    var d = autoScanDate('01/25/2003 PST');
    if(d && !d.errMsg && d.offset == -28800)
        return true;
});

testFeature("autoScanDate invalid Abbr", function(){
    var d = autoScanDate('01/25/2003 PDT');
    if(d && d.errMsg && d.offset == 0)
        return true;
});



testFeature("dateFmt localtime", function(){
    var d=autoScanDate("01/01/2000");
    var tz=dateFmt('%z', d.date, -7);
    var hour = dateFmt('%H', d.date, -7);
    return (tz=="-0700" && hour == "17");
});

testFeature("dateFmt from string", function(){
    var tz=dateFmt('%z', "01/01/2000", -7);
    var hour = dateFmt('%H', "01/01/2000", -7);
    return (tz=="-0700" && hour == "17");
});


//lastline