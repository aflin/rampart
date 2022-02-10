// Load the sql module
var Sql=require("rampart-sql");

// shortcut for Sql properties/functions
var rexFile=Sql.rexFile;
var re2File=Sql.re2File;
var rex=Sql.rex;

// shortcuts for all the rampart.utils properties/functions
rampart.globalize(rampart.utils);

// Function to check for and handle sql errors
function check_err() {
    if(sql.errMsg.length) {
        console.log(sql.errMsg);
        process.exit(1);
    }
}

// init the database, create if necessary.
// if the directory "./wikidb" exists, but is not a texis db, an error will be thrown
var sql=new Sql.init("./wikidb",true); // true means make db if it doesn't exist

// if newly created, we will get a message via sql.errMsg starting with '100';
if(sql.errMsg.length) {
    console.log(sql.errMsg);
    if( ! /^100/.test(sql.errMsg) )
        process.exit(1);
}

// check the SYSTEM TABLES table to see if our table exists.  
//   If so, ask if it should be dropped

// sql.one returns a single row, or undefined if no row is found
if( sql.one("select * from SYSTABLES where NAME='wikitext';") ){

    // function to get a single char from the command line
    var resp=null;
    function getresp(def, len) {
        var l = (len)? len: 1;
        var ret = stdin.getchar(l);
        if(ret == '\n')
            return def;
        printf("\n");
        return ret.toLowerCase();
    }

    // ask to drop the existing "wikitext" table
    while (resp!='y') {
        printf('The table "wikitext" already exists in the "./wikidb" database directory.\n   Delete it? (y/N): ');
        fflush(stdout); //flush text after newline above
        resp = getresp("n");
        if(resp == 'n') {
            printf('The table "wikitext" was NOT dropped.  Cannot continue.\n');
            process.exit(1);
        }
    }

    sql.exec("drop table wikitext;");
    // check for errors
    check_err();
}

printf("creating table wikitext\n");
sql.exec("create table wikitext ( Id int, Title varchar(16), Doc varchar(1024) );");
check_err();


/*  Sample record:
      <doc id="12" url="https://en.wikipedia.org/wiki?curid=12" title="Anarchism">...</doc>
*/

/* import using re2 -- for illustration purposes:
function procfile2(file){
                           
  return re2File( /<doc id="(\d+).*title="([^"]+)[^>]+>([^<]+)/,
           file,
           function(match,matchinfo,i){
             var sm=matchinfo.submatches;
             var ret=sql.exec("insert into wikitext values (?,?,?);", sm );
             check_err();
           }
         );
}
*/

/* import using rex */
function procfilex(file){
  //                           0       1      2         3    4    5 6       7
  return rexFile('>><doc id\\="=\\digit+!title*title\\="=[^"]+[^>]+>=!</doc>*',
           file,
           function(match,matchinfo,i){
             var sm=matchinfo.submatches;
             var ret=sql.exec("insert into wikitext values (?,?,?);",
               [sm[1], sm[4], sm[7]]
             );
             check_err();
           }
         );
}

/* The main import function: 
     It imports the files created by WikiExtractor.py 
     from within make-wiki-search.sh                      */
function wimport() {

    var datadir="./wikidata/txt/";
    var ret;
    var ndocs=0;

    var highest_rec_no=null;

    try
    {
        // try to find the last article id number from the output of WikiExtractor.py
        var res=shell("tail -n 3 extractor-output.txt");
        res=rex(/\digit+ articles in=/,res.stdout,{submatches:true});
        highest_rec_no=parseInt(res[0].submatches[0]);

    } catch(e) { highest_rec_no=-1; }

    console.log("Last Record id is = " + highest_rec_no);

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
            // print status with carriage return but without newline.
            printf("Finished: %d of %d, %s\r", ndocs, highest_rec_no, file);
            // with no newline, the line must be manually flushed
            fflush(stdout);
        }
        printf("\n");
    }
}

// start the import
wimport();
