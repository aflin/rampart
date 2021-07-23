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
    
    
function indexpage(req) {
    /* check req.params here and formulate your response */

    return { html: `${htop}<h1>THIS IS THE INDEX PAGE</h1>${hend}`};
}

function firstpage(req) {
    /* check req.params here and formulate your response */

    return { html: `${htop}<h1>THIS IS PAGE 1</h1>${hend}`};
}

function secondpage(req) {
    /* check req.params here and formulate your response */

    return { html: `${htop}<h1>THIS IS PAGE 2</h1>${hend}` };
}

function thirdpage(req) {
    /* check req.params here and formulate your response */

    return { html: `${htop}<h1>THIS IS PAGE 3</h1>${hend}` };
}


/* 
since we have 
  map: {
    "/multi/":            {module: "/modules/multi_function.js"},
  }

the functions below map to:
  http://localhost:8088/multi/
  http://localhost:8088/multi/index.html
  http://localhost:8088/multi/page1.html
  http://localhost:8088/multi/page2.html
  http://localhost:8088/multi/virtdir/page3.html
*/

module.exports={
    "/"                  : indexpage,
    "/index.html"        : indexpage,
    "/page1.html"        : firstpage,
    "/page2.html"        : secondpage,
    "/virtdir/page3.html": thirdpage
};
