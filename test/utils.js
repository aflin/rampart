var utils = require("rputils");

function assert(cond) {
  if (!cond) {
    throw new Error("assert failed");
  }
}
function printTest(name) {
  console.log("-- utils.js " + name + " --");
}

function find(arr, fn) {
  var found = null;
  arr.forEach(function (a) {
    if (fn(a)) {
      found = a;
    }
  });
  return found;
}

try {
  printTest("read file");
  var file = utils.readFile({ file: "./utils.js" });
  console.log("length of file: " + file.length);

  printTest("read ln");
  // will be replaced with for (const line of utils.readln("./utils.js")) { ... } when babel is integrated
  var iter = utils.readln("./utils.js")[Symbol.iterator]();
  var nextIt = iter.next();
  assert(nextIt.value != null);
  assert(nextIt.done == false);

  printTest("stat");
  var stat = utils.stat("./utils.js");

  console.log("is file: " + stat.is_file());
  assert(stat.is_file());
  console.log("atime: " + stat.atime);
  console.log("mode: " + stat.mode.toString(8));

  printTest("exec (sleep 0.2)");
  var execRes = utils.exec({
    path: "/bin/sleep",
    args: ["sleep", "0.2"],
    timeout: 100000,
    killSignal: utils.signals.SIGKILL,
  });
  assert(execRes.timedOut);

  printTest("kill");
  var pid = utils.exec({
    path: "/bin/sleep",
    args: ["sleep", "0.2"],
    background: true,
  }).pid;

  console.log("pid: " + pid + " is going to be killed");
  utils.kill(pid, 0);
  utils.kill(pid, 9);

  printTest("readdir");
  console.log(utils.readdir("."));

  printTest("mkdir");
  utils.mkdir("this/is/a/test");

  assert(
    find(utils.readdir("."), function (f) {
      return f == "this";
    })
  );

  printTest("rmdir");

  utils.rmdir("this/is/a/test");

  assert(
    !find(utils.readdir("this/is/a/"), function (f) {
      return f == "test";
    })
  );

  printTest("copyFile");
  utils.copyFile({ src: "utils.js", dest: "utils-2.js", overwrite: true });

  var utils2 = null;
  utils.readdir(".").forEach(function (filename) {
    if (filename == "utils-2.js") {
      utils2 = filename;
    }
  });
  assert(utils2 != null);
  assert(stat.mode == utils.stat("./utils-2.js").mode);

  // cleanup
  utils.delete("utils-2.js");
  utils.rmdir("this/is/a", true);

  printTest("chmod file");
  utils.touch({ path: "sample.txt" });
  var sample_stat = utils.stat("./sample.txt");
  console.log("before chmod: " + sample_stat.mode.toString(8));

  utils.chmod("sample.txt", 0777);
  sample_stat = utils.stat("./sample.txt");
  assert(sample_stat.mode == 0o100777);

  printTest("touch");
  utils.touch({
    path: "sample.txt",
    nocreate: false,
    setaccess: false,
    setmodify: false,
  });

  sample_stat = utils.stat("./sample.txt");
  var atime_before = sample_stat.atime;
  var mtime_before = sample_stat.mtime;

  console.log("atime: " + atime_before);
  console.log("mtime: " + mtime_before);

  console.log("after reference touch");
  utils.touch({
    path: "sample.txt",
    reference: "server.js",
    setaccess: false,
  });

  assert(sample_stat.atime.getTime() != utils.stat("./server.js").atime);

  printTest("link");

  utils.link({ src: "sample.txt", target: "sample_link.txt", symbolic: false });
  sample_stat = utils.stat("./sample.txt");
  assert(sample_stat.nlink == 2);

  printTest("rename");

  utils.rename("sample.txt", "sample-2.txt");

  assert(
    find(utils.readdir("."), function (filename) {
      return filename == "sample-2.txt";
    })
  );

  utils.rename("sample-2.txt", "sample.txt");

//   printTest("chown");
//   utils.chown({ path: "sample.txt", group_name: "everyone" });
//   sample_stat = utils.stat("./sample.txt");
//   assert(sample_stat.gid == 12);

  printTest("delete");
  console.log(utils.readdir("."));
  utils.delete("sample_link.txt");
  utils.delete("sample.txt");
  assert(
    !find(utils.readdir("."), function (filename) {
      return filename == "sample.txt";
    })
  );
} catch (e) {
  console.log("caught:");
  console.log(e);
}