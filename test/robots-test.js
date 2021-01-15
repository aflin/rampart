var rtxt=require("rampart-robots");

var x=1;
function expect_true(b)
{
    rampart.utils.printf("Test %2d  --  ",x++);
    if(b==true) console.log("passed");
    else console.log("FAILED")
}
function expect_false(b)
{
    rampart.utils.printf("Test %2d  --  ",x++);
    if(b==false) console.log("passed");
    else console.log("FAILED")
}


var txt =
      "allow: /foo/bar/\n"+
      "\n"+
      "user-agent: FooBot\n"+
      "disallow: /\n"+
      "allow: /x/\n"+
      "user-agent: BarBot\n"+
      "disallow: /\n"+
      "allow: /y/\n"+
      "\n"+
      "\n"+
      "allow: /w/\n"+
      "user-agent: BazBot\n"+
      "\n"+
      "user-agent: FooBot\n"+
      "allow: /z/\n"+
      "disallow: /\n";

var url="http://foo.bar/y/";

var url_w = "http://foo.bar/w/a";
var url_x = "http://foo.bar/x/b";
var url_y = "http://foo.bar/y/c";
var url_z = "http://foo.bar/z/d";
var url_foo = "http://foo.bar/foo/bar/";

expect_true( rtxt("FooBot", txt, url_x) );
expect_true( rtxt("FooBot", txt, url_z) );
expect_false( rtxt("FooBot", txt, url_y) );
expect_true( rtxt("BarBot", txt, url_y) );
expect_true( rtxt("BarBot", txt, url_w) );
expect_false( rtxt("BarBot", txt, url_z) );
expect_true( rtxt("BazBot", txt, url_z) );

  // Lines with rules outside groups are ignored.
expect_false( rtxt("FooBot", txt, url_foo) );
expect_false( rtxt("BarBot", txt, url_foo) );
expect_false( rtxt("BazBot", txt, url_foo) );

expect_true( rtxt("NOBOT", txt, url_foo) );

