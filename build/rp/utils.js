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
  print("mode: " + stat.mode.toString(8));
  print("\n-- exec (sleep 0.2) --");
  print(
    "timed_out: " +
      utils.exec({
        path: "/bin/sleep",
        args: ["sleep", "0.2"],
        timeout: 1000000,
        kill_signal: utils.signals.SIGKILL,
      }).timed_out
  );

  print("\n-- utils.js readdir --");
  print(utils.readdir("."));

  print("\n-- utils.js mkdir --");
  utils.mkdir("this/is/a/test");

  print(
    utils.exec({ path: "/usr/local/bin/tree", args: ["tree", "this"] }).stdout
  );

  print("\n-- utils.js rmdir --");

  utils.rmdir("this/is/a/test");

  print(
    utils.exec({ path: "/usr/local/bin/tree", args: ["tree", "this"] }).stdout
  );

  print("\n-- utils.js copy file --");
  utils.copy_file({ src: "utils.js", dest: "utils-2.js", overwrite: true });

  utils.readdir(".").forEach(function (v) {
    if (v == "utils-2.js") {
      print("found utils-2.js");
      print(
        "copied == src stat?: " + (stat.mode == utils.stat("./utils-2.js").mode)
      );
    }
  });

  utils.exec({ path: "/bin/rm", args: ["rm", "utils-2.js"] });

  utils.rmdir("this/is/a", true);

  print("\n-- utils.js chmod file --");

  utils.touch({path: "sample.txt"});

  var sample_stat = utils.stat("./sample.txt");
  print("mode: " + sample_stat.mode.toString(8));

  utils.chmod("sample.txt", 0777);
  sample_stat = utils.stat("./sample.txt");
  print("mode after chmod: " + sample_stat.mode.toString(8));

  print("\n-- utils.js touch --");
  utils.touch({path: "sample.txt", nocreate: false});

  var example_stat = utils.stat("./sample.txt");
  print("atime: " + example_stat.atime);
  print("mtime: " + example_stat.mtime);

  print("after reference touch")
  utils.touch({path: "sample.txt", reference: "server.js", setaccess: false});
  var example_stat = utils.stat("./sample.txt");
  print("atime: " + example_stat.atime);
  print("mtime: " + example_stat.mtime);

  print("\n-- utils.js link --");
  utils.link({ src: "sample.txt", target: "sample_link.txt", symbolic: false });
  sample_stat = utils.stat("./sample.txt");
  print("symbolic link?: " + (sample_stat.nlink == 2));

  utils.exec({ path: "/bin/rm", args: ["rm", "sample_link.txt"] });
  utils.exec({ path: "/bin/rm", args: ["rm", "sample.txt"] });
  
} catch (e) {
  print("caught:");
  console.log(e);
}
