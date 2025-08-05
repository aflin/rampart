rampart.globalize(rampart.utils);

var out={};

var skipPrefixes=true;

var lc = process.argv[2].match(/\/([^\/]+)\/[^\/]+$/);
if(lc.length<2) {
    fprintf(stderr, "Could not get lang code from path '%s'\n", process.argv[2]);
    process.exit(1);
}

lc=lc[1];

var ncol=3;
switch(lc) {
    case 'en':
    case 'enxl':
    case 'es':
        ncol=4;
        break;
}
fprintf(stderr, "NCOLS=%d for %s\n", ncol, lc);
function parse(){
    var argi=2;
    var file;
    for (file=process.argv[argi]; (file=process.argv[argi]); argi++) {
        var r = readLine(file);
        var nlines = parseInt(exec('wc','-l',file).stdout.trim())
        var x=0;
        var i=0, words, key;
        fprintf(stderr,"reading %s\n", file);
        while( (l=r.next()) ) {
            x++;
            if(! (x%100) )
                fprintf(stderr, "doing line %d of %d  \r", x, nlines);
            var words=l.split('\t');
            if(words.length < ncol ) {
                fprintf(stderr, 'bad line "%s" on line %d', l.trim(), x);
                continue;
            }

            for (i=0; i< ncol; i++)
                words[i]=words[i].trim();

            if(ncol>3 && skipPrefixes && words[3].charAt(words[3].length-1)=='-')
                continue;

            key=words[0];
            val=words[1];

            if(key.charAt(0) =='-' || val.charAt(0) == '-')
                continue;

            if(key == val || !key.length || !val.length)
                continue;

            printf("%s,%s\n", key, val);
        }
        fprintf(stderr,'\n');
    }
}
parse();
