var robots=require("rampart-robots");

/* this is mostly a copy and paste of robots_test.cc */

var x=1;
var name="";

var npass=0;
var nfail=0;
var total=0;

function expect_true(b)
{
    total++;
    rampart.utils.printf("%-20s %2d  --  ", name + " Test", x++);
    if(b==true){
      console.log("passed");
      npass++;
    } else {
      console.log("FAILED");
      nfail++;
    }
}
function expect_false(b)
{
    total++;
    rampart.utils.printf("%-20s %2d  --  ", name + " Test", x++);
    if(b==false) {
      console.log("passed");
      npass++;
    } else {
      console.log("FAILED");
      nfail++;
    }
}

function IsUserAgentAllowed(robotstxt, bot, url) {
  return robots.isAllowed(bot, robotstxt, url);
}


function systemtest() {
  name="System";
  x=1;
  var robotstxt = "user-agent: FooBot\n" +
      "disallow: /\n";
  // Empty robots.txt: everything allowed.
  expect_true(IsUserAgentAllowed("", "FooBot", ""));

  // Empty user-agent to be matched: everything allowed.
  expect_true(IsUserAgentAllowed(robotstxt, "", ""));

  // Empty url: implicitly disallowed, see method comment for GetPathParamsQuery
  // in robots.cc.
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot", ""));

  // All params empty: same as robots.txt empty, everything allowed.
  expect_true(IsUserAgentAllowed("", "", ""));

}



function grouptest() {
    name="Group";
    x=1;
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

    expect_true( robots.isAllowed("FooBot", txt, url_x) );
    expect_true( robots.isAllowed("FooBot", txt, url_z) );
    expect_false( robots.isAllowed("FooBot", txt, url_y) );
    expect_true( robots.isAllowed("BarBot", txt, url_y) );
    expect_true( robots.isAllowed("BarBot", txt, url_w) );
    expect_false( robots.isAllowed("BarBot", txt, url_z) );
    expect_true( robots.isAllowed("BazBot", txt, url_z) );

      // Lines with rules outside groups are ignored.
    expect_false( robots.isAllowed("FooBot", txt, url_foo) );
    expect_false( robots.isAllowed("BarBot", txt, url_foo) );
    expect_false( robots.isAllowed("BazBot", txt, url_foo) );

    expect_true( robots.isAllowed("NOBOT", txt, url_foo) );
}

function linetest() {
  name="Line";
  x=1;
  var robotstxt_correct =
      "user-agent: FooBot\n"+
      "disallow: /\n";
  var robotstxt_incorrect =
      "foo: FooBot\n"+
      "bar: /\n";
  var robotstxt_incorrect_accepted =
      "user-agent FooBot\n"+
      "disallow /\n";

  var url = "http://foo.bar/x/y";

  expect_false(IsUserAgentAllowed(robotstxt_correct, "FooBot", url));
  expect_true(IsUserAgentAllowed(robotstxt_incorrect, "FooBot", url));
  expect_false(IsUserAgentAllowed(robotstxt_incorrect_accepted, "FooBot", url));

}

function casetest() {
  name="Case";
  x=1;

  var robotstxt_upper =
      "USER-AGENT: FooBot\n"+
      "ALLOW: /x/\n"+
      "DISALLOW: /\n";
  var robotstxt_lower =
      "user-agent: FooBot\n"+
      "allow: /x/\n"+
      "disallow: /\n";
  var robotstxt_camel =
      "uSeR-aGeNt: FooBot\n" +
      "AlLoW: /x/\n"+
      "dIsAlLoW: /\n";
  var url_allowed = "http://foo.bar/x/y";
  var url_disallowed = "http://foo.bar/a/b";

  expect_true(IsUserAgentAllowed(robotstxt_upper, "FooBot", url_allowed));
  expect_true(IsUserAgentAllowed(robotstxt_lower, "FooBot", url_allowed));
  expect_true(IsUserAgentAllowed(robotstxt_camel, "FooBot", url_allowed));
  expect_false(IsUserAgentAllowed(robotstxt_upper, "FooBot", url_disallowed));
  expect_false(IsUserAgentAllowed(robotstxt_lower, "FooBot", url_disallowed));
  expect_false(IsUserAgentAllowed(robotstxt_camel, "FooBot", url_disallowed));
}

function uacasetest() {
  name="User-Agent Case";
  x=1;

  var robotstxt_upper =
      "User-Agent: FOO BAR\n" +
      "Allow: /x/\n"+
      "Disallow: /\n";
  var robotstxt_lower =
      "User-Agent: foo bar\n" +
      "Allow: /x/\n" +
      "Disallow: /\n";
  var robotstxt_camel =
      "User-Agent: FoO bAr\n" +
      "Allow: /x/\n" +
      "Disallow: /\n";
  var url_allowed = "http://foo.bar/x/y";
  var url_disallowed = "http://foo.bar/a/b";

  expect_true(IsUserAgentAllowed(robotstxt_upper, "Foo", url_allowed));
  expect_true(IsUserAgentAllowed(robotstxt_lower, "Foo", url_allowed));
  expect_true(IsUserAgentAllowed(robotstxt_camel, "Foo", url_allowed));
  expect_false(IsUserAgentAllowed(robotstxt_upper, "Foo", url_disallowed));
  expect_false(IsUserAgentAllowed(robotstxt_lower, "Foo", url_disallowed));
  expect_false(IsUserAgentAllowed(robotstxt_camel, "Foo", url_disallowed));
  expect_true(IsUserAgentAllowed(robotstxt_upper, "foo", url_allowed));
  expect_true(IsUserAgentAllowed(robotstxt_lower, "foo", url_allowed));
  expect_true(IsUserAgentAllowed(robotstxt_camel, "foo", url_allowed));
  expect_false(IsUserAgentAllowed(robotstxt_upper, "foo", url_disallowed));
  expect_false(IsUserAgentAllowed(robotstxt_lower, "foo", url_disallowed));
  expect_false(IsUserAgentAllowed(robotstxt_camel, "foo", url_disallowed));
}

function spacetest(){
  name="Space";
  x=1;

  var robotstxt_upper =
      "User-Agent: FOO BAR\n" +
      "Allow: /x/\n"+
      "Disallow: /\n";
  var robotstxt_lower =
      "User-Agent: foo bar\n"+
      "Allow: /x/\n"+
      "Disallow: /\n";
  var robotstxt_camel =
      "User-Agent: FoO bAr\n"+
      "Allow: /x/\n"+
      "Disallow: /\n";
  var url_allowed = "http://foo.bar/x/y";
  var url_disallowed = "http://foo.bar/a/b";

  expect_true(IsUserAgentAllowed(robotstxt_upper, "Foo", url_allowed));
  expect_true(IsUserAgentAllowed(robotstxt_lower, "Foo", url_allowed));
  expect_true(IsUserAgentAllowed(robotstxt_camel, "Foo", url_allowed));
  expect_false(IsUserAgentAllowed(robotstxt_upper, "Foo", url_disallowed));
  expect_false(IsUserAgentAllowed(robotstxt_lower, "Foo", url_disallowed));
  expect_false(IsUserAgentAllowed(robotstxt_camel, "Foo", url_disallowed));
  expect_true(IsUserAgentAllowed(robotstxt_upper, "foo", url_allowed));
  expect_true(IsUserAgentAllowed(robotstxt_lower, "foo", url_allowed));
  expect_true(IsUserAgentAllowed(robotstxt_camel, "foo", url_allowed));
  expect_false(IsUserAgentAllowed(robotstxt_upper, "foo", url_disallowed));
  expect_false(IsUserAgentAllowed(robotstxt_lower, "foo", url_disallowed));
  expect_false(IsUserAgentAllowed(robotstxt_camel, "foo", url_disallowed));


}

function firstspacetest() {
  name="First Space";
  x=1;

  var robotstxt =
      "User-Agent: *\n"+
      "Disallow: /\n"+
      "User-Agent: Foo Bar\n"+
      "Allow: /x/\n"+
      "Disallow: /\n";
  var url = "http://foo.bar/x/y";

  expect_true(IsUserAgentAllowed(robotstxt, "Foo", url));
  expect_false(IsUserAgentAllowed(robotstxt, "Foo Bar", url));
}

function secondarygrouptest(){
  name="Secondary Group"
  x=1;
  var robotstxt_empty = "";
  var robotstxt_global =
      "user-agent: *\n" +
      "allow: /\n" +
      "user-agent: FooBot\n" +
      "disallow: /\n";
  var robotstxt_only_specific =
      "user-agent: FooBot\n" +
      "allow: /\n" +
      "user-agent: BarBot\n" +
      "disallow: /\n" +
      "user-agent: BazBot\n" +
      "disallow: /\n";
  var url = "http://foo.bar/x/y";

  expect_true(IsUserAgentAllowed(robotstxt_empty, "FooBot", url));
  expect_false(IsUserAgentAllowed(robotstxt_global, "FooBot", url));
  expect_true(IsUserAgentAllowed(robotstxt_global, "BarBot", url));
  expect_true(IsUserAgentAllowed(robotstxt_only_specific, "QuxBot", url));

}

function casesensitivetest(){
  name="Case Sensitive";
  x=1;
  var robotstxt_lowercase_url =
      "user-agent: FooBot\n" +
      "disallow: /x/\n";
  var robotstxt_uppercase_url =
      "user-agent: FooBot\n"+
      "disallow: /X/\n";
  var url = "http://foo.bar/x/y";

  expect_false(IsUserAgentAllowed(robotstxt_lowercase_url, "FooBot", url));
  expect_true(IsUserAgentAllowed(robotstxt_uppercase_url, "FooBot", url));

}

function longestmatchtest() {
  name="Longest Match";
  x=1;
  var url = "http://foo.bar/x/page.html";
  var robotstxt;
  robotstxt =
      "user-agent: FooBot\n" +
      "disallow: /x/page.html\n" +
      "allow: /x/\n";

  expect_false(IsUserAgentAllowed(robotstxt, "FooBot", url));

  robotstxt =
      "user-agent: FooBot\n" +
      "allow: /x/page.html\n" +
      "disallow: /x/\n";

  expect_true(IsUserAgentAllowed(robotstxt, "FooBot", url));
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/x/"));

  robotstxt =
      "user-agent: FooBot\n" +
      "disallow: \n" +
      "allow: \n";
  // In case of equivalent disallow and allow patterns for the same
  // user-agent, allow is used.
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot", url));

  robotstxt =
      "user-agent: FooBot\n" +
      "disallow: /\n" +
      "allow: /\n";
  // In case of equivalent disallow and allow patterns for the same
  // user-agent, allow is used.
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot", url));

  var url_a = "http://foo.bar/x";
  var url_b = "http://foo.bar/x/";
  robotstxt =
      "user-agent: FooBot\n" +
      "disallow: /x\n" +
      "allow: /x/\n";
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot", url_a));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot", url_b));


  robotstxt =
      "user-agent: FooBot\n" +
      "disallow: /x/page.html\n" +
      "allow: /x/page.html\n";
  // In case of equivalent disallow and allow patterns for the same
  // user-agent, allow is used.
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot", url));

  robotstxt =
      "user-agent: FooBot\n" +
      "allow: /page\n" +
      "disallow: /*.html\n";
  // Longest match wins.
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/page.html"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/page"));

  robotstxt =
      "user-agent: FooBot\n" +
      "allow: /x/page.\n" +
      "disallow: /*.html\n";
  // Longest match wins.
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot", url));
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/x/y.html"));

  robotstxt =
      "User-agent: *\n" +
      "Disallow: /x/\n" +
      "User-agent: FooBot\n" +
      "Disallow: /y/\n";
  // Most specific group for FooBot allows implicitly /x/page.
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/x/page"));
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/y/page"));
}


function idencodingtest(){
  name="ID Encoding";
  x=1;
  var robotstxt =
      "User-agent: FooBot\n" +
      "Disallow: /\n" +
      "Allow: /foo/bar?qux=taz&baz=http://foo.bar?tar&par\n";
  expect_true(IsUserAgentAllowed(
      robotstxt, "FooBot",
      "http://foo.bar/foo/bar?qux=taz&baz=http://foo.bar?tar&par"));

  // 3 byte character: /foo/bar/ツ -> /foo/bar/%E3%83%84
  robotstxt =
      "User-agent: FooBot\n" +
      "Disallow: /\n" +
      "Allow: /foo/bar/ツ\n";
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/foo/bar/%E3%83%84"));
  // The parser encodes the 3-byte character, but the URL is not %-encoded.
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/foo/bar/ツ"));
  // Percent encoded 3 byte character: /foo/bar/%E3%83%84 -> /foo/bar/%E3%83%84
  robotstxt =
      "User-agent: FooBot\n" +
      "Disallow: /\n" +
      "Allow: /foo/bar/%E3%83%84\n";
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/foo/bar/%E3%83%84"));
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/foo/bar/ツ"));
  // Percent encoded unreserved US-ASCII: /foo/bar/%62%61%7A -> NULL
  // This is illegal according to RFC3986 and while it may work here due to
  // simple string matching, it should not be relied on.
  robotstxt =
      "User-agent: FooBot\n" +
      "Disallow: /\n" +
      "Allow: /foo/bar/%62%61%7A\n";
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/foo/bar/baz"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/foo/bar/%62%61%7A"));
}

function specialcharstest(){
  name="Special Chars";
  x=1;
  var robotstxt =
      "User-agent: FooBot\n" +
      "Disallow: /foo/bar/quz\n" +
      "Allow: /foo/*/qux\n";
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/foo/bar/quz"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/foo/quz"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/foo//quz"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/foo/bax/quz"));

  robotstxt =
      "User-agent: FooBot\n" +
      "Disallow: /foo/bar$\n" +
      "Allow: /foo/bar/qux\n";
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/foo/bar"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/foo/bar/qux"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/foo/bar/"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/foo/bar/baz"));

  robotstxt =
      "User-agent: FooBot\n" +
      "# Disallow: /\n" +
      "Disallow: /foo/quz#qux\n" +
      "Allow: /\n";
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/foo/bar"));
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/foo/quz"));

}

function htmldirtest(){
  name="HTML dir";
  x=1;

  var robotstxt =
      "User-Agent: *\n" +
      "Allow: /allowed-slash/index.html\n"+
      "Disallow: /\n";
  // If index.html is allowed, we interpret this as / being allowed too.
  expect_true(
      IsUserAgentAllowed(robotstxt, "foobot", "http://foo.com/allowed-slash/"));
  // Does not exatly match.
  expect_false(IsUserAgentAllowed(robotstxt, "foobot",
                                  "http://foo.com/allowed-slash/index.htm"));
  // Exact match.
  expect_true(IsUserAgentAllowed(robotstxt, "foobot",
                                 "http://foo.com/allowed-slash/index.html"));
  expect_false(
      IsUserAgentAllowed(robotstxt, "foobot", "http://foo.com/anyother-url"));

}

function lengthlimittest(){

  name="Line Length";
  x=1;

  var kEOLLen = 1;
  var kMaxLineLen = 2083 * 8;
  var allow = "allow: ";
  var disallow = "disallow: ";
  var robotstxt;

  // Disallow rule pattern matches the URL after being cut off at kMaxLineLen.
  robotstxt = "user-agent: FooBot\n";
  var longline = "/x/";
  var max_length =
      kMaxLineLen - longline.length - disallow.length + kEOLLen;


  while (longline.length < max_length) {
    longline += "a";
  }
  robotstxt = robotstxt + disallow + longline + "/qux\n";

  // Matches nothing, so URL is allowed.
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/fux"));
  // Matches cut off disallow rule.
  expect_false(IsUserAgentAllowed(
      robotstxt, "FooBot", "http://foo.bar" + longline + "/fux") );


  robotstxt =
      "user-agent: FooBot\n" +
      "disallow: /\n";
  var longline_a = "/x/";
  var longline_b = "/x/";
  var max_length =
      kMaxLineLen - longline_a.length - allow.length + kEOLLen;

  while (longline_a.length < max_length) {
    longline_a += "a";
    longline_b += "b";
  }
  robotstxt = robotstxt + allow + longline_a + "/qux\n"
    + allow + longline_b + "/qux\n";

  // URL matches the disallow rule.
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/"));
  // Matches the allow rule exactly.
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot",
                         "http://foo.bar" + longline_a +"/qux"));
  // Matches cut off allow rule.
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot",
                         "http://foo.bar" + longline_b + "/fux"));

}

function googlespecstest(){
  name="Google Specs";
  x=1;
  var robotstxt, url, url_page;

  robotstxt =
      "user-agent: FooBot\n" +
      "disallow: /\n" +
      "allow: /fish\n";
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/bar"));

  expect_true(IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/fish"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/fish.html"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/fish/salmon.html"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/fishheads"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/fishheads/yummy.html"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/fish.html?id=anything"));

  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/Fish.asp"));
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/catfish"));
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/?id=fish"));

  // "/fish*" equals "/fish"
  robotstxt =
      "user-agent: FooBot\n" +
      "disallow: /\n" +
      "allow: /fish*\n";
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/bar"));

  expect_true(IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/fish"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/fish.html"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/fish/salmon.html"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/fishheads"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/fishheads/yummy.html"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/fish.html?id=anything"));

  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/Fish.bar"));
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/catfish"));
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/?id=fish"));

  // "/fish/" does not equal "/fish"
  robotstxt =
      "user-agent: FooBot\n" +
      "disallow: /\n" +
      "allow: /fish/\n";
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/bar"));

  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/fish/"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/fish/salmon"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/fish/?salmon"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/fish/salmon.html"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/fish/?id=anything"));

  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/fish"));
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/fish.html"));
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot",
                                  "http://foo.bar/Fish/Salmon.html"));

  // "/*.php"
  robotstxt =
      "user-agent: FooBot\n" +
      "disallow: /\n" +
      "allow: /*.php\n";
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/bar"));

  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/filename.php"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/folder/filename.php"));
  expect_true(IsUserAgentAllowed(
      robotstxt, "FooBot", "http://foo.bar/folder/filename.php?parameters"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar//folder/any.php.file.html"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/filename.php/"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/index?f=filename.php/"));
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot",
                                  "http://foo.bar/php/"));
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot",
                                  "http://foo.bar/index?php"));

  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/windows.PHP"));

  // "/*.php$"
  robotstxt =
      "user-agent: FooBot\n" +
      "disallow: /\n" +
      "allow: /*.php$\n";
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/bar"));

  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/filename.php"));
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot",
                                 "http://foo.bar/folder/filename.php"));

  expect_false(IsUserAgentAllowed(robotstxt, "FooBot",
                                  "http://foo.bar/filename.php?parameters"));
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot",
                                  "http://foo.bar/filename.php/"));
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot",
                                  "http://foo.bar/filename.php5"));
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot",
                                  "http://foo.bar/php/"));
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot",
                                  "http://foo.bar/filename?php"));
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot",
                                  "http://foo.bar/aaaphpaaa"));
  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar//windows.PHP"));

  // "/fish*.php"
  robotstxt =
      "user-agent: FooBot\n" +
      "disallow: /\n" +
      "allow: /fish*.php\n";
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/bar"));

  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/fish.php"));
  expect_true(
      IsUserAgentAllowed(robotstxt, "FooBot",
                         "http://foo.bar/fishheads/catfish.php?parameters"));

  expect_false(
      IsUserAgentAllowed(robotstxt, "FooBot", "http://foo.bar/Fish.PHP"));

  // Section "Order of precedence for group-member records".
  robotstxt =
      "user-agent: FooBot\n" +
      "allow: /p\n" +
      "disallow: /\n";
  url = "http://example.com/page";
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot", url));

  robotstxt =
      "user-agent: FooBot\n" +
      "allow: /folder\n" +
      "disallow: /folder\n";
  url = "http://example.com/folder/page";
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot", url));

  robotstxt =
      "user-agent: FooBot\n" +
      "allow: /page\n" +
      "disallow: /*.htm\n";
  url = "http://example.com/page.htm";
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot", url));

  robotstxt =
      "user-agent: FooBot\n" +
      "allow: /$\n" +
      "disallow: /\n";
  url = "http://example.com/";
  url_page = "http://example.com/page.html";
  expect_true(IsUserAgentAllowed(robotstxt, "FooBot", url));
  expect_false(IsUserAgentAllowed(robotstxt, "FooBot", url_page));

}


systemtest();
linetest()
grouptest();
casetest();
uacasetest();
spacetest();
firstspacetest();
secondarygrouptest();
casesensitivetest();
longestmatchtest();
idencodingtest();
specialcharstest();
htmldirtest();
lengthlimittest();
googlespecstest();

rampart.utils.printf("%d passed, %d failed of %d tests\n", npass, nfail, total);

if (nfail)
  process.exit(1);

