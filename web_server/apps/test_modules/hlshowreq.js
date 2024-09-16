/* make printf et. al. global */
rampart.globalize(rampart.utils);

/* this is loaded once to access the sandr() function */

var Sql=require("rampart-sql");
rampart.globalize(Sql,["sandr"]);


/*
    This corresponds to http://localhost:8088/apps/test_modules/hlshowreq.html
    The ".html" extension can be any extension.
    However the content-type is controlled in 
       return {html: ...};
    below.
*/


function showreq_callback(req){
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
        /* highlight objects and arrays */
        var search_and_replace = [
            //double quotes before a ':'
            ['>>"=\\space*:=\\space*"=[^"]+' ,                          '": "<span class="string">\\6</span>'],
            // ':' followed by true or false
            ['>>"=\\space*:=\\space*true=' ,                            '": <span class="boolean">\\5</span>'],
            ['>>"=\\space*:=\\space*false=' ,                           '": <span class="boolean">\\5</span>'],
            // ':' followed by null
            ['>>"=\\space*:=\\space*null=' ,                            '": <span class="null">\\5</span>'   ],
            // ':' followed by a number
            ['>>"=\\space*:=\\space*[+\\-\\.\\digit]+' ,                '": <span class="number">\\5</span>' ],
            // the quoted key before the ':'
            ['>>"=[^"]+"=\space*:' ,                                    '"<span class="key">\\2</span>":'    ],
            // string in an array
            ['>>[\\[,]=\\space*"\\P=[^"]+\\F"=\\space*[\\],]=' ,        '<span class="string">\\4</span>'    ],
            // true/false/null in an array
            ['>>[\\[,]=\\space\\P*true=\\F\\space*[\\],]=' ,            '<span class="boolean">\\3</span>'   ],
            ['>>[\\[,]=\\space\\P*false=\\F\\space*[\\],]=' ,           '<span class="boolean">\\3</span>'   ],
            ['>>[\\[,]=\\space\\P*null=\\F\\space*[\\],]=' ,            '<span class="null">\\3</span>'      ],
            // number in an array
            ['>>[\\[,]=\\space\\P*[+\\-\\.\\digit]+\\F\\space*[\\],]=', '<span class="number">\\3</span>'    ]
        ];

        return sandr(search_and_replace,json);
    }
    /* pretty print our request object, along with rampart and process objects */
    var str=sprintf('%4J', {req:req,serverConf:serverConf,rampart:rampart,process:process});

    var css=
        `pre { padding: 5px; margin: 5px; }
         body { background-color: #272822; color: #f8f8f2;font-family:sans-serif;}}
        .number { color: #ae81ff; }
        .string { color: #e6db74; }
        .boolean { color: #ae81ff; }
        .null { color: #ae81ff; }
        .key { color: #a6e22e; }\n`;
    
    /* object key sets the content-type header.
       Thus using the key html sets header to "Content-Type: text/html" */
    return({
        /* with highlighting */
        html:`
<html>
   <head>
      <title> Rampart Variable Dump </title>
      <style>${%s:css}</style>
   </head>   
   <body>
      <div>
      	Object sent to this function (req) and other info (serverConf, rampart, process):
      </div>   
      <pre>${%s:sandrHighlight(str)}</pre>
   </body>
</html>`
        /* with no highlighting */
        //html:"<html><head><style>"+css+"</style></head><body>Object sent to this function(req) and other info (rampart):<br><pre>"+str+"</pre></body></html>"
        /* just as text/plain */
        //text: str 
    });
    
}

/* export the callback function */
module.exports=showreq_callback;
