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
    
    
function page(req) {
    /* check req.params here and formulate your response */

    return { html: `${htop}<h1>THIS IS A PAGE</h1>${hend}`};
}

/* 
since we have 
  map: {
    "/scripts/":            {modulePath: "/modules/mymodulepath"},
  }
  and this module is at modules/mymodulepath/single.js

the function assigned to module.exports below maps to 
  http://localhost:8088/scripts/single.html
*/

module.exports=page;