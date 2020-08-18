var Sql=require("rpsql");
var rexfile=Sql.rexfile;
var re2file=Sql.re2file;
var rex=Sql.rex;

rampart.globalize(rampart.utils);

var sql=new Sql.init("./wikidb",true); /* true means make db if it doesn't exist */

console.log(sql.lastErr);

/* check if our table exists.  If not, make it */
var ret=sql.exec("select * from SYSTABLES where NAME='wikitext'");

if(ret.results.length==0) {
    printf("creating table wikitext\n");
    ret=sql.exec("create table wikitext ( Id int, Title varchar(16), Doc varchar(1024) );");
    console.log(ret);
}

/*
    <doc id="12" url="https://en.wikipedia.org/wiki?curid=12" title="Anarchism">...</doc>
*/




/* import using re2 */
function procfile2(file){
  //                           
  return re2File( /<doc id="(\d+).*title="([^"]+)[^>]+>([^<]+)/,
           file,
           function(match,matchinfo,i){
             var sm=matchinfo.submatches;
             var ret=sql.exec("insert into wikitext values (?,?,?);", sm );
             if (sql.lastErr.length)
               printf('%s',sql.lastErr);
           }
         );
}

/* import using rex */
function procfilex(file){
  //                           0      1      2        3    4    5 6        7
  return rexFile( />><doc id\="=\digit+!title*title\="=[^"]+[^>]+>=!<\/doc>*/,
           file,
           function(match,matchinfo,i){
             var sm=matchinfo.submatches;
             var ret=sql.exec("insert into wikitext values (?,?,?);",
               [sm[1], sm[4], sm[7]]
             );
             if (sql.lastErr.length)
               printf('%s',sql.lastErr);
           }
         );
}


function wimport() {

    var datadir="./wikidata/txt/";
    var ret;
    var ndocs=0;

    var total=null;

    try
    {
        var res=shell("tail -n 3 extractor-output.txt");
        res=rex(/\digit+ articles in=/,res.stdout,{submatches:true});
        total=parseInt(res[0].submatches[0]);
    } catch(e) { total=-1; }

    console.log("Total number of pages = " + total);

    var dirs=readdir(datadir)

    dirs=dirs.sort();

    for (var i=0;i<dirs.length;i++) {
        var dir=datadir+dirs[i]+'/';
        printf("processing directory %s\n", dir);

        var files=readdir(dir).sort();
        files=files.sort();
        for (var j=0;j<files.length;j++){
            var file=dir+files[j];
            //ret=procfile2(file);
            ret=procfilex(file);
            ndocs+=ret;
            printf("Finished: %d of %d, %s\r", ndocs, total, file);
        }
        printf("\n");
    }
}

wimport();
