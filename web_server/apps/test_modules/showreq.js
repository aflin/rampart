/* make printf et. al. global */
rampart.globalize(rampart.utils);

/* 
   load more modules or other 
   initialization code goes here.
   Code outside the module.exports
   function will be run once when the
   script is loaded or changed.
*/

//var Sql=require("rampart-sql");


function showreq_callback(req){

    //var pretext=JSON.stringify(req, null, 4);
    // or
    var pretext=sprintf("%4J", req);
    
    return({
        /* you can set custom headers as well */
        headers:
            {
                "X-Custom-Header":1
            },

        /* a different status code can also be sent.  Default is 200 */
        // status 302,

        //only set one of these.  Setting more than one throws error.
        html:"<html><body>Object sent to this function(req):<br><pre>"+pretext+"</pre></body></html>"
        //jpg: "@/home/user/myimage.jpg"
        //text: pretext
    });
    
}

/* export the callback function */
module.exports=showreq_callback;
