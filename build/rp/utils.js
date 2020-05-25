var utils = require("./libdukutils.so");
var i = 0;

try {
  utils.readln("./curltest2.js", function (line) {
    // print(line);
    i++;
    if (i > 55) return false;
  });
  var stat = utils.stat("./curltest2.js");
  print(stat.last_access);
  print(utils.exec("/bin/ls", "ls", "-1"));
} catch (e) {
  print("caught:");
  console.log(e);
}
