rampart.globalize(rampart.utils);

var python = require('rampart-python');

var ps = python.importString("def noop(**kwargs):\n    return None\n");

// Get child pid for memory measurement
var pidmod = python.importString("import os\ndef getpid():\n    return os.getpid()\n");
var pid = pidmod.getpid().toValue();
printf("child pid: %s\n", pid);

function getRSS() {
    var ret = shell("ps -o rss= -p " + pid);
    return parseInt(trim(ret.stdout));
}

var rss_before = getRSS();
printf("RSS before: %d KB\n", rss_before);

// Each call with pyArgs leaks one PyDict_New()
for (var i = 0; i < 100000; i++) {
    ps.noop({pyArgs: {a: "hello"}});
}

var rss_after = getRSS();
printf("RSS after:  %d KB\n", rss_after);
printf("Difference: %d KB\n", rss_after - rss_before);

if (rss_after - rss_before > 5000)
    printf("LIKELY LEAK: memory grew by more than 5MB\n");
else
    printf("No significant memory growth detected\n");
