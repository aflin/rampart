/* ********************************************************************* 
   this is a test of the functions that are part of the sql library, 
   but not directly related to manipulating a database
************************************************************************ */

var Sql=require("rpsql");

var rex=Sql.rex;
var re2=Sql.re2;
var sandr=Sql.sandr;
var sandr2=Sql.sandr2;
var rexfile=Sql.rexfile;
var re2file=Sql.re2file;
var abstract=Sql.abstract;
var stringformat=Sql.stringformat;

/* ************************************************
    Test of added functions from rpsql 
*************************************************** */
/* stringformat at https://docs.thunderstone.com/site/vortexman/fmt_strfmt.html */
printf("TEST OF DB FUNCS:\n");
printf("query markup:\n\t%s\n",stringformat('%mbH','@0 hello there',"a sentence with hello there in it. Hello there!"));
printf("time format:\n\t%s\n",stringformat('%at','%c','now'));
printf("html escape:\n\t%s\n",stringformat('"%H" = htmlesc("%!H")','<div>','&lt;div&gt;' ));
var txt="The abstract will be less than maxsize characters long, and will attempt to end at a word boundary. "+
        "If maxsize is not specified (or is less than or equal to 0) then a default size of 230 characters is used.\n"+
        "The style argument is a string or integer, and allows a choice between several different ways of creating the "+
        "abstract. Note that some of these styles require the query argument as well, which is a Metamorph query to look for";
/* abstract at https://docs.thunderstone.com/site/texisman/abstract.html */
/* takes one or two args in any order. Must have a string.  May have an object with options */
printf("abstract:\n\t%s\n",abstract({maxsize:100,query:"metamorph query",style:'querybest'},txt)); 

/* sandr. see https://docs.thunderstone.com/site/vortexman/sandr.html and for syntax:
     https://docs.thunderstone.com/site/vortexman/rex_expression_syntax.html and
     https://docs.thunderstone.com/site/vortexman/rex_expression_repetition.html
*/
printf('sandr(">>x=", "able", "txs are x to make words searchx"):\n\t%s\n',sandr(">>x=", "able", "txs are x to make words searchx"));

var search=["that=", "is=", "\\.=", "ok=",    " the="];
var repl  =["those", "are", "s."  , "not \\1"        ];
var txta  =["that is the bomb.","and that is ok"];

printf('sandr(%J,%J,%J):\n\t%J\n',search,repl,txta,sandr(search,repl,txta));

/************ rex *************/
  /* rex|re2( 
          expression,                     //string or array of strings 
          searchItem,                     //string or buffer
          callback,                       // optional callback function
          options  -
            {
              exclude:                    // string: "none"      - return all hits
                                          //         "overlap"   - remove the shorter hit if matches overlap
                                          //         "duplicate" - current default - remove smaller if one hit entirely encompasses another
              submatches:		  true|false - include submatches in an array.
                                          if have callback function (true is default)
                                            - true  --  function(
                                                          match,
                                                          submatchinfo={expressionIndex:matchedExpressionNo,submatches:["array","of","submatches"]},{...}...]},
                                                          matchindex
                                                        )
                                            - false --  function(match,matchindex)
                                          if no function (false is default)
                                            - true  --  ret= [{match:"match1",expressionIndex:matchedExpressionNo,submatches:["array","of","submatches"]},{...}...]
                                            - false --  ret= ["match1","match2"...]
            }
        );
   return value is an array of matches.
   If callback is specified, return value is number of matches.
for rex syntax, see:
     https://docs.thunderstone.com/site/vortexman/rex_expression_syntax.html and
     https://docs.thunderstone.com/site/vortexman/rex_expression_repetition.html
  */

// error in expression
//printf('%J\n',rex(">>!","string"));


search=["th=",'>>is=','this ','his= is='];
var txt='hello this is a message from dbfunc-test.js';
var exclude={exclude:'duplicate'};

printf("rex(%J,'%s',function(match,minfo,i){\n\tprintf(\"match='%%J', info=%%J, matchno=%%d\\n\",match,minfo,i);\n},%J);\n",
    search,txt,exclude);
rex(search,txt,function (match,minfo,i){
    printf("match='%J', info=%J, matchno=%d\n",match,minfo,i);
},exclude);

printf("rex(%J,'%s','%J'):\n\t%J\n", search,txt,exclude,rex(search,txt,exclude));


exclude={exclude:'duplicate'};
printf("rex(%J,'%s','%J'):\n\t%J\n", search,txt,exclude,rex(search,txt,exclude));

exclude={exclude:'overlap'};
printf("rex(%J,'%s','%J'):\n\t%J\n", search,txt,exclude,rex(search,txt,exclude));

exclude={exclude:'none'};
printf("rex(%J,'%s','%J'):\n\t%J\n", search,txt,exclude,rex(search,txt,exclude));


search="is.*";
printf("re2(%J,'%J','%J'):\n\t%J\n", search,txt,exclude,re2(search,txt,exclude));

//printf("%J\n",txt.match(/is.*/));

search=[
    ">>h=[^\\space]+\\space*this=\\space*i.=",
    ">>hel=lo= ="    
];
rex(search,txt,
function(match,minfo,i){
    printf("match='%J', info=%J, matchno=%d\n",match,minfo,i);    
},exclude);

/* callback can cancel by returning false */
printf("var nhits=rex('\\alnum*>>is=','hello this is a message from this server.js',function(hit,i){\n\tprintf(\"%%d: %%s\\n\",i,hit);\n\tif(i==1) return false;\n});\n");
nhits=rex('\\alnum*>>is=','hello this is a message from this server.js',function(hit,subs,i){
    printf("%d: %s\n",i,hit);
    if(i==1) return false;
});
printf("nhits=%d\n",nhits);



/***************************** rexfile ******************************/

  /* rexfile|re2file( 
          expression,                     //string or array of strings 
          filename,                       //file with text to be searched
          callback,                       // optional callback function
          options  -
            {
              exclude:                    // string: "none"      - return all hits
                                          //         "overlap"   - remove the shorter hit if matches overlap
                                          //         "duplicate" - current default - remove smaller if one hit entirely encompasses another
              submatches:		  true|false - include submatches in an array.
                                          if have callback function (true is default)
                                            - true  --  function(
                                                          match,
                                                          submatchinfo={expressionIndex:matchedExpressionNo,submatches:["array","of","submatches"]},{...}...]},
                                                          matchindex
                                                        )
                                            - false --  function(match,matchindex)
                                          if no function (false is default)
                                            - true  --  ret= [{match:"match1",expressionIndex:matchedExpressionNo,submatches:["array","of","submatches"]},{...}...]
                                            - false --  ret= ["match1","match2"...]
              delimiter:		  expression to match -- delimiter for end of buffer.  Default is "$" (end of line).  If your pattern crosses lines, specify
                                                                 a delimiter which will not do so and you will be guaranteed to match even if a match crosses internal read buffer boundry
            }
        );
        
   return value is an array of matches.
   If callback is specified, return value is number of matches.
  */

console.log(rexfile(">>function=[^\n]+","server.js"));

printf('var nhits=rexfile(">><doc=[^>]+>=","wiki_00",function(match,i){\n\tprintf("match="%%J", matchno=%%d\\n",match,i);\n},{submatches:false});\nprintf("nhits=%%d\\n",nhits);\n');

var nhits=rexfile(">><doc=[^>]+>=","wiki_00",function(match,i){
    printf("match='%J', matchno=%d\n",match,i);
},{submatches:false});
printf("nhits=%d\n",nhits);

printf('var ret=rexfile(">><doc=[^>]+>=","wiki_00",{submatches:true});\n');
var ret=rexfile(">><doc=[^>]+>=","wiki_00",{submatches:true});
ret=sprintf('%J',ret);
ret=sandr('>>\\},\\{=','\\},\\\n\\{',ret);
console.log("ret=",ret);

printf('ret=re2file("<doc[^>]+>","wiki_00",{submatches:true});\n');
ret=re2file("(<doc)([^>]+)(>)","wiki_00",{submatches:true});
ret=sprintf('%J',ret);
ret=sandr('>>\\},\\{=','\\},\\\n\\{',ret);
console.log("ret=",ret);


/* /expr/ can also be used to avoid double backslashes.  It is marginally slower */
console.log(rex(/\alnum{4}/,"hits are fun"));
console.log(sandr2(/fun/,"funtimes","we are having fun"));


//this takes waaaay too long.  Why?  if the doc match ( ([^<]+) ) is not included, it runs much faster. 
//Perhaps they have buffer crossing problems? or it checks conditions for a longer match with every char?
printf("re2file of wiki_00 (slow)\n");
ret=re2file( /<doc id="(\d+)[^>]*?title="([^"]+)[^>]+>([^<]+)/,"wiki_00",{submatches:true},function(match,info,i){
  var rep=sprintf("<Doc of %d length>",info.submatches[info.submatches.length-1].length);
  info.submatches[info.submatches.length-1]=rep;
  console.log(info);
});


//but this is very fast
printf("rexfile of wiki_00 (fast)\n");
ret=rexfile ( />><doc id\="=\digit+!title*title\="=[^"]+[^>]+>=!<\/doc>*/, "wiki_00",{submatches:true},function(match,info,i){
  var rep=sprintf("<Doc of %d length>",info.submatches[info.submatches.length-1].length);
  info.submatches[info.submatches.length-1]=rep;
  console.log(info);
});

