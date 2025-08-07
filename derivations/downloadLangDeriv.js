#!/usr/bin/env rampart

rampart.globalize(rampart.utils);

load.curl;


function download(o) {
    printf("Downloading %s\n", o.lc);
    mkdir(o.dir);
    var f = fopen(o.file,'w+');
    var nchunks=0;
    curl.fetchAsync(
        o.url, 
        {
            location: true,
            returnText: false,
            skipFinalRes: true,

            chunkCallback: function(res) {
                f.fprintf('%s', res.body);
            },

            progressCallback: function(res) {
                nchunks++;
                if(nchunks%30)
                    return;

                var tot = res.progress;
                var rate = tot/(res.totalTime*1024);
                var unit = "Kbs";
                var perc = 100 * tot / res.expectedTotal;

                if(rate > 10000) {
                    rate/=1024;
                    unit = "Mbs"
                }
                var out;
                if(res.expectedTotal != -1)
                    out=sprintf("    %Ad%%, %d of %d bytes, (%.2f %s)",
                        'green', perc, tot, res.expectedTotal, rate, unit );
                else
                    out=sprintf("    %d bytes", o.file, tot);
                printf('%M', [`downloading file ${o.url} ->`, `  ${o.file}`,out]);
            },

            callback: function(res) {
                printf("done\n")
                f.fclose();
            }
        }
    );
}

var res = curl.fetch(
    "https://api.github.com/repos/aflin/rampart_lang_derivs/contents/",
    {
        header: [ 
            "Accept: application/vnd.github+json",
            "X-GitHub-Api-Version: 2022-11-28"
           ]
    }
);

if(res.status != 200) {
    printf("Error getting dir listing from aflin/rampart_lang_derivs on github:\n%s\n",
        res.statusText);
    process.exit(1);
}

var lst;
try {
    lst = JSON.parse(res.text);
} catch(e) {
    printf("could not parse JSON response:\n%s\n", e.message);
    process.exit(1);
}

var lpacks=[],j=1;

printf("Available Language Derivations:\n") 

var dirs = readDir(process.scriptPath);

console.log(dirs);


for (var i=0;i<lst.length;i++) {
    var entry = lst[i];
    if(entry.type=='dir') {
        var lc = entry.name;
        lpacks.push({
            lc: lc,
            dir: process.scriptPath + '/' + lc,
            url: `https://github.com/aflin/rampart_lang_derivs/raw/refs/heads/main/${lc}/${lc}-deriv`,
            file: process.scriptPath + '/' + lc + '/' + lc + '-deriv'
        });
        printf("  %d) %s%15s\n", j++, lc, dirs.includes(lc)? " (installed)":"" );
    }
}

//printf("%3J\n", lpacks);

var r;
while(true) {
    printf ("please choose: ");
    r=fgets(stdin, 255).trim();
    r = parseInt(r);
    if(isNaN(r) || lpacks.length < r) {
        printf("invalid response\n");
        r=null;
    } else
        break;
}

download(lpacks[r-1]);

