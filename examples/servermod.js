/* make printf et. al. global */
rampart.globalize(rampart.utils);

var Sql=require("rpsql");
rampart.globalize(Sql,["sandr"]);


function showreq_callback(req){

    /* decoding of embedded json in querystring and multiple same name parameters is automatic*/
    /*e.g. http://localhost:8088/showreq.html?something={%22this%22:%22that%22,%22array%22:[true,false,null,-1.11,%22string%22]}&something=something_else
      results in a javascript object like:
      {
            "something": [
                {
                    "this": "that",
                    "array": [
                        true,
                        false,
                        null,
                        -1.11,
                        "string"
                    ]
                },
                "something_else"
            ]
      }    
    */

    // http://jsfiddle.net/KJQ9K/554/
    function syntaxHighlight(json) {
        json = json.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
        return json.replace(/("(\\u[a-zA-Z0-9]{4}|\\[^u]|[^\\"])*"(\s*:)?|\b(true|false|null)\b|-?\d+(?:\.\d*)?(?:[eE][+\-]?\d+)?)/g, function (match) {
            var cls = 'number';
            if (/^"/.test(match)) {
                if (/:$/.test(match)) {
                    cls = 'key';
                } else {
                    cls = 'string';
                }
            } else if (/true|false/.test(match)) {
                cls = 'boolean';
            } else if (/null/.test(match)) {
                cls = 'null';
            }
            return '<span class="' + cls + '">' + match + '</span>';
        });
    }

    /* sandr is much faster than the above js regular expression engine */
    function sandrHighlight(json){
        /* first two lines are for objects, second two are for arrays */
        var search=[ '>>"=\\space*:=\\space*"=[^"]+',        '>>"=\\space*:=\\space*true=',         '>>"=\\space*:=\\space*false=', 
                '>>"=\\space*:=\\space*null=',       '>>"=\\space*:=\\space*[+\\-\\.\\digit]+',   '>>"=[^"]+"=\space*:',
                '>>[\\[,]=\\space*"\\P=[^"]+\\F"=\\space*[\\],]=', '>>[\\[,]=\\space\\P*true=\\F\\space*[\\],]=', '>>[\\[,]=\\space\\P*false=\\F\\space*[\\],]=',
                '>>[\\[,]=\\space\\P*null=\\F\\space*[\\],]=', '>>[\\[,]=\\space\\P*[+\\-\\.\\digit]+\\F\\space*[\\],]='];

        var replace=[ '": "<span class="string">\\6</span>', '": <span class="boolean">\\5</span>', '": <span class="boolean">\\5</span>',
                '": <span class="null">\\5</span>',  '": <span class="number">\\5</span>',        '"<span class="key">\\2</span>":',
                '<span class="string">\\4</span>',                 '<span class="boolean">\\3</span>',            '<span class="boolean">\\3</span>',
                '<span class="null">\\3</span>',               '<span class="number">\\3</span>'];
        return sandr(search,replace,json);
    }

//    printf("%J\n",rampart);
    //For testing timeout
    if(req.params.timeout)
    {
        for (var i=0;i<1000000000;i++);
        printf("DONE WASTING TIME IN JS");
    }

    var str=JSON.stringify({req:req,rampart:rampart,process:process},null,4);

    var css=
        "pre {outline: 1px solid #ccc; padding: 5px; margin: 5px; }\n"+
        ".string { color: green; }\n"+
        ".number { color: darkorange; }\n"+
        ".boolean { color: blue; }\n"+
        ".null { color: magenta; }\n"+
        ".key { color: red; }\n";
    
    /* you can set custom headers as well */
    return({
        headers:
            {
                "X-Custom-Header":1
            },
        //only set one of these.  Setting more than one throws error.
        //jpg: "@/home/user/myimage.jpg"
        //babel ver:
        //html:`<html><head><style>${css}</style><body>Object sent to this function(req) and other info (rampart):<br><pre>${sandrHighlight(str)}</pre></body></html>`
        //reg ver:
        html:"<html><head><style>"+css+"</style><body>Object sent to this function(req) and other info (rampart):<br><pre>"+sandrHighlight(str)+"</pre></body></html>"
        /* with no highlighting */
        //html:"<html><head><style>"+css+"</style><body>Object sent to this function(req) and other info (rampart):<br><pre>"+str+"</pre></body></html>"
        
        //text: "\\@home network is a now defunct cable broadband provider." //\\@ for beginning a string with @
    });
    
}

/* export the callback function */
module.exports=showreq_callback;
