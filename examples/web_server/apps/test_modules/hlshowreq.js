/* make printf et. al. global */
rampart.globalize(rampart.utils);

/* this is loaded once to access the sandr() function */

var Sql=require("rampart-sql");
rampart.globalize(Sql,["sandr"]);


/*
    This corresponds to http://localhost:8088/modtest/hlshowreq.html
    The ".html" extension could be any extension.
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
    /* pretty print our request object, along with rampart and process objects */
    var str=sprintf('%4J', {req:req,rampart:rampart,process:process});

    var css=
        `pre {outline: 1px solid #ccc; padding: 5px; margin: 5px; }
        .string { color: green; }
        .number { color: darkorange; }
        .boolean { color: blue; }
        .null { color: magenta; }
        .key { color: red; }\n`;
    
    /* object key sets the content-type header.
       Thus using the key html sets header to "Content-Type: text/html" */
    return({
        /* with highlighting */
        html:"<html><head><style>"+css+"</style><body>Object sent to this function (req) and other info (rampart,process):<br><pre>"+sandrHighlight(str)+"</pre></body></html>"
        /* with no highlighting */
        //html:"<html><head><style>"+css+"</style><body>Object sent to this function(req) and other info (rampart):<br><pre>"+str+"</pre></body></html>"
        /* just as text/plain */
        //text: 
    });
    
}

/* export the callback function */

module.exports=showreq_callback;
