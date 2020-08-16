/* load the http server module */
var server=require("rpserver");

function index(req) {
//printf("%J\n",req);

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


server.start(
{
//    ip:"0.0.0.0",  //this binds to all. Default is 127.0.0.1
//    ipv6:"::",     //this binds to all. Default is ::1
    port:8090,     //default is 8080
    ipv6port:8090, //defaults to 'port' above if not set
    scriptTimeout: 10.0,
    connectTimeout:20.0,
    useThreads: true, //make server multithreaded
//    user:"unpriv-user",  //if starting as root
//    log: true,
    map:
    {
        /* url to function mappings */
        "/search.html":       {module:"searchmod"},
        "/":                  index
    }
});
