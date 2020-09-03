/* make printf et. al. global */
rampart.globalize(rampart.utils);

/* load the http server module */
var server=require("rpserver");

/* load sql database module */
var Sql=require("rpsql");

//console.log(process.scriptPath);
//var sql= new Sql.init(process.scriptPath+"/testdb",true); /* true means create the database if it doesn't exist */

function index(req) {

    if(req.path.path != '/' && req.path.path != '/index.html')
        return {status:404};

    var html="<!DOCTYPE HTML>\n"+
        "<html><head>\n"+
        "\n"+
        "</head><body>\n"+
        '<form action="search.html">\n'+
            '<input name="q" type="text">\n'+
            '<input name="fetch" type="submit">\n'+
        '</form>\n'+
        '\n'+
        "</body></html>\n";

    return({html:html});
}


printf("\ntry a url like http://127.0.0.1:8090/search?q=paris\n");
//printf("or see a sample website at http://127.0.0.1:8088/\n\nSTARTING SERVER:\n");

var pid=server.start(
{
    bind: [ "[::]:8090", "0.0.0.0:8090" ], /* bind to all */

    scriptTimeout: 20.0, /* max time to spend in JS */
    connectTimeout:20.0, /* how long to wait before client sends a req or server can send a response */
    useThreads: true, /* make server multi-threaded. */
    user: "unpriv-user",
    //log: true,           //turn logging on, by default goes to stdout/stderr
    //accessLog: "./access.log",    //access log location, instead of stdout
    //errorLog: "./error.log",     //error log location, instead of stderr
    
    daemon: true, // fork and run in background.    
    /*
    secure:true,
    sslkeyfile:  "/etc/letsencrypt/live/mydom.com/privkey.pem",
    sslcertfile: "/etc/letsencrypt/live/mydom.com/fullchain.pem",
    */
    
    /* **********************************************************
       map urls to functions or paths on the filesystem 
       If it ends in a '/' then matches everything in that path
       except a more specific ('/something.html') path
       ********************************************************** */
    map:
      {
        "/":                  index,
        "/search*":           {module:"metamod"}
      }
});

// if daemon==true then we get the pid of the detached process
// otherwise server.start never returns
console.log("pid of rampart-server: " + pid);
