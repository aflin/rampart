/* ********************************************************************* 
   this is a test of the functions that are part of the sql library, 
   but not directly related to manipulating a database
************************************************************************ */

/* make printf et. al. global */
rampart.globalize(rampart.utils);

var Sql=require("rampart-sql");
var wikifile = process.scriptPath + "/wiki_00";
/*  make sql functions global.  Same as doing this:
var rex=Sql.rex;
var re2=Sql.re2;
var sandr=Sql.sandr;
var sandr2=Sql.sandr2;
var rexFile=Sql.rexFile;
var re2File=Sql.re2File;
var abstract=Sql.abstract;
var stringFormat=Sql.stringFormat;
*/
rampart.globalize(Sql);


function testFeature(name,test)
{
    var error=false;
    printf("testing sql extras - %-47s - ", name);
    fflush(stdout);
    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }
    if(test)
        printf("passed\n")
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        if(error) console.log(error);
        process.exit(1);
    }
    if(error) console.log(error);
}




/* ************************************************
    Test of added functions from rpsql 
*************************************************** */
/* stringFormat at https://docs.thunderstone.com/site/vortexman/fmt_strfmt.html */



testFeature("stringFormat - query markup", function(){
  var res=stringFormat('%mbH','@0 hello there',"a sentence with hello there in it. Hello there!");

  return "a sentence with <b>hello</b> <b>there</b> in it. <b>Hello</b> <b>there</b>!" == res;

});



testFeature("stringFormat - date" , function(){
  var res=stringFormat('%aT','%a %b %d %H:%M:%S %Y',111111111);
  return "Tue Jul 10 00:11:51 1973" == res;
});

testFeature("stringFormat - html un/escape" , function(){
  var res1=stringFormat('%H','<div>');
  var res2=stringFormat('%!H', res1);
  return '<div>' == res2;
});


testFeature("abstract", function(){

  var txt="The abstract will be less than maxsize characters long, and will attempt to end at a word boundary. "+
        "If maxsize is not specified (or is less than or equal to 0) then a default size of 230 characters is used.\n"+
        "The style argument is a string or integer, and allows a choice between several different ways of creating the "+
        "abstract. Note that some of these styles require the query argument as well, which is a Metamorph query to look for";
  /* abstract at https://docs.thunderstone.com/site/texisman/abstract.html */
  /* takes one or two args in any order. Must have a string.  May have an object with options */
  var res=abstract({maxsize:100,query:"metamorph query",style:'querybest'},txt); 

  return res.length==100;
});


/* sandr. see https://docs.thunderstone.com/site/vortexman/sandr.html and for syntax:
     https://docs.thunderstone.com/site/vortexman/rex_expression_syntax.html and
     https://docs.thunderstone.com/site/vortexman/rex_expression_repetition.html
*/


testFeature("sandr - string and array of strings", function(){
  var res = sandr(">>x=", "able", "txs are x to make words searchx");
  var search=["that=", "is=", "\\.=", "ok=",    " the="];
  var repl  =["those", "are", "s."  , "not \\1"        ];
  var txta  =["that is the bomb.","and that is ok"];
  var res1=sandr(search,repl,txta);
  return "tables are able to make words searchable" == res &&
          "those are bombs." == res1[0] &&
          "and those are not ok" == res1[1];
});






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

var search=["th=",'>>is=','this ','his= is='];
var txt='hello this is a message from sql-extras-test.js';

testFeature("rex - exclude duplicate", function() {
  var exclude={exclude:'duplicate'};
  var res=rex(search,txt,exclude);
  return res[0]=="this "&& res[1]=="his is";
});

testFeature("rex - exclude overlap", function() {
  var exclude={exclude:'overlap'};
  var res=rex(search,txt,exclude);

  return res[0] == "his is"
});

testFeature("rex - exclude none", function() {
  var exclude={exclude:'none'};
  var res=rex(search,txt,exclude);

  return res[0] == "this " &&
         res[1] == "th" &&
         res[2] == "his is" &&
         res[3] == "is" && 
         res[4] == "is";
});

search=["th",'is','this ','his is'];

testFeature("re2 - exclude duplicate", function() {
  var exclude={exclude:'duplicate'};
  var res=re2(search,txt,exclude);

  return res[0]=="this " && res[1]=="his is";
});

testFeature("re2 - exclude overlap", function() {
  var exclude={exclude:'overlap'};
  var res=re2(search,txt,exclude);

  return res[0] == "his is"
});

testFeature("re2 - exclude none", function() {
  var exclude={exclude:'none'};
  var res=re2(search,txt,exclude);

  return res[0] == "this " &&
         res[1] == "th" &&
         res[2] == "his is" &&
         res[3] == "is" && 
         res[4] == "is";
});


testFeature("rex - with callback", function() {
  var ret=true;
  search=[
    ">>h=[^\\space]+\\space*this=\\space*i.=",
    ">>hel=lo= ="    
  ];

  rex(search, txt, function(match,info,i){
    if(i) {
      ret = ret && match == "hello" &&
            info.expressionIndex == 1 &&
            info.submatches[0] == "hel" &&
            info.submatches[1] == "lo" &&
            info.submatches[2] == " ";
    } else {
      ret = ret && match == 'hello this is' &&
            info.expressionIndex == 0 &&
            info.submatches[0] == "h" &&
            info.submatches[1] == "ello" &&
            info.submatches[2] == " " &&
            info.submatches[3] == "this" &&
            info.submatches[4] == " " &&
            info.submatches[5] == "is";
    }
  });

  return ret;
});

testFeature("rex - with callback and cancel", function() {
  var ret=true;

  rex(search, txt, function(match,info,i){
    if(i) {
      ret = false;
    } else {
      ret = ret && match == 'hello this is' &&
            info.expressionIndex == 0 &&
            info.submatches[0] == "h" &&
            info.submatches[1] == "ello" &&
            info.submatches[2] == " " &&
            info.submatches[3] == "this" &&
            info.submatches[4] == " " &&
            info.submatches[5] == "is";
      return false;
    }
  });

  return ret;
});


/***************************** rexFile ******************************/

  /* rexFile|re2File( 
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

testFeature("rexfile - basic", function() {
  var res=rexFile(">>function=[^\n]+",process.scriptPath+"/sql-extras-test.js");
  //console.log(res.length);
  return res.length == 38;
});


testFeature("rexfile - submatches", function() {
  var sum=0;
  var nhits=rexFile('>><doc id\\="=\\digit+[^>]*>=',wikifile,function(match,info,i){
      //printf("match='%J', matchno=%d info=%J\n",match,i,info);
      sum+=parseInt(info.submatches[1]);
  });
  return 15084 == sum;
});

testFeature("re2file - submatches", function() {
  var sum=0;
  var nhits=re2File('<doc id="(\\d+)[^>]*>',wikifile,function(match,info,i){
      //printf("match='%J', matchno=%d info=%J\n",match,i,info);
      sum+=parseInt(info.submatches[0]);
  });
  return 15084 == sum;
});

testFeature("rexfile - full record(rex is fast)", function() {
  var sum=0;
  var nhits=rexFile('>><doc id\\="=\\digit+[^>]*>=[^<]+',wikifile,function(match,info,i){
      //printf("match='%J', matchno=%d info=%J\n",match,i,info);
      sum+=parseInt(info.submatches[1]);
  });
  return 15084 == sum;
});

  //this takes waaaay too long.  Why?  if the doc match ( ([^<]+) ) is not included, it runs much faster. 
  //Perhaps they have buffer crossing problems? or it checks conditions for a longer match with every char?
testFeature("re2file - full record(re2 is slow)", function() {
  var sum=0;
  var nhits=re2File('<doc id="(\\d+)[^>]*>[^<]+',wikifile,function(match,info,i){
      //printf("match='%J', matchno=%d info=%J\n",match,i,info);
      sum+=parseInt(info.submatches[0]);
  });
  return 15084 == sum;
});

