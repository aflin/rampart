

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

    printf("%J\n",rampart);
    //For testing timeout
    //for (var i=0;i<100000000;i++);
    //print("DONE WASTING TIME IN JS");

    var str=JSON.stringify({req:req,rampart:rampart},null,4);
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
        html:"<html><head><style>"+css+"</style><body>Object sent to this function(req) and other info (rampart):<br><pre>"+syntaxHighlight(str)+"</pre></body></html>"
        //,text: "\\@home network is a now defunct cable broadband provider." //\\@ for beginning a string with @
    });
    
}


module.exports=showreq_callback;
