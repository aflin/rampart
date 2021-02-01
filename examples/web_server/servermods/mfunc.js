/* make printf et. al. global */
rampart.globalize(rampart.utils);

/* 
   load more modules or other initialization code goes here.
   Code outside the module.exports function will be run once when the
   script is loaded or changed.
*/

var Sql=require("rampart-sql");


var htop = 
`<html>
    <head>
        <title>My Sample Webpage</title>
    </head>
    <body>
`;


var hend = 
`    </body>
</html>
`;
    
    
function firstpage(req) {
    /* check req.params here and formulate your response */

    return { html: `${htop}<h1>THIS IS PAGE 1</h1>${hend}`};
}

function secondpage(req) {
    /* check req.params here and formulate your response */

    return { html: `${htop}<h1>THIS IS PAGE 2</h1>${hend}` };
}


function indexpage(req) {
    /* check req.params here and formulate your response */

    return { html: `${htop}<h1>THIS IS THE INDEX PAGE</h1>${hend}` };
}


/* 
since we have 
  map: {
    "/modtest/":          {modulePath: process.scriptPath + "/servermods/"},
  }
and this file is mfunc.js in servermods/
in server.js, these map to
http://localhost:8088/modtest/mfunc/page1.html
http://localhost:8088/modtest/mfunc/page2.html
http://localhost:8088/modtest/mfunc/
http://localhost:8088/modtest/mfunc/index.html
*/

module.exports={
    "/page1.html": firstpage,
    "/page2.html": secondpage,
    "/": indexpage,
    "index.html": indexpage
};
