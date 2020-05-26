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
  print("\n-- exec (sleep 0.2) --");
  print(
    utils.exec({ path: "/bin/sleep", args: ["sleep", "0.2"], timeout: 100000 })
      .timed_out
  );

  print("\n-- utils.js mdkir --");
  utils.mkdir("this/is/a/test");

  print(
    utils.exec({ path: "/usr/local/bin/tree", args: ["tree", "this"] }).stdout
  );

  print("\n-- utils.js rmdir --");

  utils.rmdir("this/is/a/test");

  print(utils.exec("/usr/local/bin/tree", "tree", "this").stdout);

  utils.rmdir("this/is/a", true);
} catch (e) {
  print("caught:");
  console.log(e);
}
