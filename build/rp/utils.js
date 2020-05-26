var utils = require("./rputils.so");
var i = 1;

try {
  utils.readln("./utils.js", function (line) {
    // print(line);
    i++;
  });
  print("\n-- num of lines --");
  print(i);

  var stat = utils.stat("./utils.js");
  print("\n-- utils.js stat --");
  print("is file: " + stat.is_file());
  print("atime: " + stat.atime);
  print("mode: " + stat.mode);
  print("\n-- exec (ls) --");
  print(utils.exec("/bin/ls", "ls", "-1").stdout);
  
  print("\n-- utils.js mdkir --");
  print(utils.mkdir("this/is/a/test"));
  
  print("\n-- utils.js rmdir --")
  
  print(utils.rmdir("this/is/a/test"));

  print(utils.exec("/bin/ls", "ls -R this", "-1").stdout);

  print(utils.rmdir("this/is/a", true));
} catch (e) {
  print("caught:");
  console.log(e);
}
